/*
 * Copyright (c) 2016-2019 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include "events/equeue.h"
#include "platform/Callback.h"
#include "platform/NonCopyable.h"
#include <cstddef>
#include <new>

namespace events {
/**
 * \addtogroup events-public-api
 * @{
 */

/** EVENTS_EVENT_SIZE
 *  Minimum size of an event
 *  This size fits a Callback<void()> at minimum
 */
#define EVENTS_EVENT_SIZE \
    (EQUEUE_EVENT_SIZE - 2*sizeof(void*) + sizeof(mbed::Callback<void()>))

/** EVENTS_QUEUE_SIZE
 *  Default size of buffer for events
 */
#define EVENTS_QUEUE_SIZE (32*EVENTS_EVENT_SIZE)

// Predeclared classes
template <typename F>
class Event;

/**
 * \defgroup events_EventQueue EventQueue class
 * @{
 */

/** EventQueue
 *
 *  Flexible event queue for dispatching events
 */
class EventQueue : private mbed::NonCopyable<EventQueue> {
public:
    /** Create an EventQueue
     *
     *  Create an event queue. The event queue either allocates a buffer of
     *  the specified size with malloc or uses the user provided buffer.
     *
     *  @param size     Size of buffer to use for events in bytes
     *                  (default to EVENTS_QUEUE_SIZE)
     *  @param buffer   Pointer to buffer to use for events
     *                  (default to NULL)
     */
    EventQueue(unsigned size = EVENTS_QUEUE_SIZE, unsigned char *buffer = NULL);

    /** Destroy an EventQueue
     */
    ~EventQueue();

    /** Dispatch events
     *
     *  Executes events until the specified milliseconds have passed.
     *  If ms is negative, the dispatch function will dispatch events
     *  indefinitely or until break_dispatch is called on this queue.
     *
     *  When called with a finite timeout, the dispatch function is guaranteed
     *  to terminate. When called with a timeout of 0, the dispatch function
     *  does not wait and is IRQ safe.
     *
     *  @param ms       Time to wait for events in milliseconds, a negative
     *                  value will dispatch events indefinitely
     *                  (default to -1)
     */
    void dispatch(int ms = -1);

    /** Dispatch events without a timeout
     *
     *  This is equivalent to EventQueue::dispatch with no arguments, but
     *  avoids overload ambiguities when passed as a callback.
     *
     *  @see EventQueue::dispatch
     */
    void dispatch_forever()
    {
        dispatch();
    }

    /** Break out of a running event loop
     *
     *  Forces the specified event queue's dispatch loop to terminate. Pending
     *  events may finish executing, but no new events will be executed.
     */
    void break_dispatch();

    /** Millisecond counter
     *
     *  Returns the underlying tick of the event queue represented as the
     *  number of milliseconds that have passed since an arbitrary point in
     *  time. Intentionally overflows to 0 after 2^32-1.
     *
     *  @return         The underlying tick of the event queue in milliseconds
     */
    unsigned tick();

    /** Cancel an in-flight event
     *
     *  Attempts to cancel an event referenced by the unique id returned from
     *  one of the call functions. It is safe to call cancel after an event
     *  has already been dispatched.
     *
     *  id must be valid i.e. event must have not finished executing.
     *
     *  The cancel function is IRQ safe.
     *
     *  If called while the event queue's dispatch loop is active in another thread,
     *  the cancel function does not guarantee that the event will not execute after it
     *  returns, as the event may have already begun executing. A call made from
     *  the same thread as the dispatch loop will always succeed with a valid id.
     *
     *  @param id       Unique id of the event
     *  @return         true  if event was successfully cancelled
     *                  false if event was not cancelled (invalid id or executing already begun)
     */
    bool cancel(int id);

    /** Query how much time is left for delayed event
     *
     *  If the event is delayed, this function can be used to query how much time
     *  is left until the event is due to be dispatched.
     *
     *  id must be valid i.e. event must have not finished executing.
     *
     *  This function is IRQ safe.
     *
     *  @param id       Unique id of the event
     *
     *  @return         Remaining time in milliseconds or
     *                   0 if event is already due to be dispatched or
     *                     is currently executing.
     *                  Undefined if id is invalid.
     *
     */
    int time_left(int id);

    /** Background an event queue onto a single-shot timer-interrupt
     *
     *  When updated, the event queue will call the provided update function
     *  with a timeout indicating when the queue should be dispatched. A
     *  negative timeout will be passed to the update function when the
     *  timer-interrupt is no longer needed.
     *
     *  Passing a null function disables the existing update function.
     *
     *  The background function allows an event queue to take advantage of
     *  hardware timers or other event loops, allowing an event queue to be
     *  ran in the background without consuming the foreground thread.
     *
     *  @param update   Function called to indicate when the queue should be
     *                  dispatched
     */
    void background(mbed::Callback<void(int)> update);

    /** Chain an event queue onto another event queue
     *
     *  After chaining a queue to a target, calling dispatch on the target
     *  queue will also dispatch events from this queue. The queues use
     *  their own buffers and events must be handled independently.
     *
     *  A null queue as the target will unchain the existing queue.
     *
     *  The chain function allows multiple event queues to be composed,
     *  sharing the context of a dispatch loop while still being managed
     *  independently
     *
     *  @param target   Queue that will dispatch this queue's events as a
     *                  part of its dispatch loop
     *
     *  @return Zero on success and negative error code value if chaining fails
     *
     */
    int chain(EventQueue *target);



#if defined(DOXYGEN_ONLY)
    /** Calls an event on the queue
     *
     *  The specified callback will be executed in the context of the event
     *  queue's dispatch loop.
     *
     *  The call function is IRQ safe and can act as a mechanism for moving
     *  events out of IRQ contexts.
     *
     *  @param f        Function to execute in the context of the dispatch loop
     *  @param args     Arguments to pass to the callback
     *  @return         A unique id that represents the posted event and can
     *                  be passed to cancel, or an id of 0 if there is not
     *                  enough memory to allocate the event.
     *                  Returned id will remain valid until event has finished
     *                  executing.
     *
     * @code
     *     #include "mbed.h"
     *
     *     int main() {
     *         // creates a queue with the default size
     *         EventQueue queue;
     *
     *         // events are simple callbacks
     *         queue.call(printf, "called immediately\n");
     *
     *         // the dispatch method executes events
     *         queue.dispatch();
     *     }
     * @endcode
     */
    template <typename F, typename ...Args>
    int call(F f, Args ...args);

    /** Calls an event on the queue
     *
     *  The specified callback is executed in the context of the event
     *  queue's dispatch loop.
     *
     *  The call function is IRQ safe and can act as a mechanism for moving
     *  events out of IRQ contexts.
     *
     *  @param obj        Object to call with the member function
     *  @param method     Member function to execute in the context of the dispatch loop
     *  @param args       Arguments to pass to the callback
     *  @return           A unique ID that represents the posted event and can
     *                    be passed to cancel, or an ID of 0 if there is not
     *                    enough memory to allocate the event.
     *                    Returned ID remains valid until event has finished
     *                    executing.
     *
     * @code
     *     #include "mbed.h"
     *
     *     class EventHandler {
     *         int _id;
     *     public:
     *         EventHandler(int id) : _id(id) { }
     *
     *         void handler(int c) {
     *             printf("ID: %d Param: %d\r\n", _id, c);
     *         }
     *     };
     *
     *     int main() {
     *         // creates a queue with the default size
     *         EventQueue queue;
     *
     *         // Create EventHandler object with state
     *         EventHandler handler_cb(1);
     *
     *         // events are simple callbacks, call object method
     *         // with provided parameter
     *         queue.call(&handler_cb, &EventHandler::handler, 2);
     *
     *         // the dispath method executes events
     *         queue.dispatch();
     *     }
     * @endcode
     */
    // AStyle ignore, not handling correctly below
    // *INDENT-OFF*
    template <typename T, typename R, typename ...Args>
    int call(T *obj, R (T::*method)(Args ...args), Args ...args);
    // *INDENT-ON*

    /** Calls an event on the queue after a specified delay
     *
     *  The specified callback is executed in the context of the event
     *  queue's dispatch loop.
     *
     *  The call_in function is IRQ safe and can act as a mechanism for moving
     *  events out of IRQ contexts.
     *
     *  @param ms       Time to delay in milliseconds
     *  @param args     Arguments to pass to the callback
     *  @return         A unique ID that represents the posted event and can
     *                  be passed to cancel, or an ID of 0 if there is not
     *                  enough memory to allocate the event.
     *
     * @code
     *     #include "mbed.h"
     *
     *     int main() {
     *         // creates a queue with the default size
     *         EventQueue queue;
     *
     *         // events are simple callbacks
     *         queue.call_in(2000, printf, "called in 2 seconds\n");
     *
     *         // the dispatch methods executes events
     *         queue.dispatch();
     *     }
     * @endcode
     */
    template <typename F, typename ...Args>
    int call_in(int ms, Args ...args);

    /** Calls an event on the queue after a specified delay
     *
     *  The specified callback is executed in the context of the event
     *  queue's dispatch loop.
     *
     *  The call_in function is IRQ safe and can act as a mechanism for moving
     *  events out of IRQ contexts.
     *
     *  @param ms       Time to delay in milliseconds
     *  @param obj      Object to call with the member function
     *  @param method   Member function to execute in the context of the dispatch loop
     *  @param args     Arguments to pass to the callback
     *  @return         A unique ID that represents the posted event and can
     *                  be passed to cancel, or an ID of 0 if there is not
     *                  enough memory to allocate the event.
     *
     * @code
     *     #include "mbed.h"
     *
     *     class EventHandler {
     *         int _id;
     *     public:
     *         EventHandler(int id) : _id(id) { }
     *
     *         void handler(int c) {
     *             printf("ID: %d Param: %d\r\n", _id, c);
     *         }
     *     };
     *
     *     int main() {
     *         // creates a queue with the default size
     *         EventQueue queue;
     *
     *         // Create EventHandler object with state
     *         EventHandler handler_cb(3);
     *
     *         // events are simple callbacks, call object method in 2 seconds
     *         // with provided parameter
     *         queue.call_in(2000, &handler_cb, &EventHandler::handler, 4);
     *
     *         // the dispatch method executes events
     *         queue.dispatch();
     *     }
     * @endcode
     */
    // AStyle ignore, not handling correctly below
    // *INDENT-OFF*
    template <typename T, typename R, typename ...Args>
    int call_in(int ms, T *obj, R (T::*method)(Args ...args), Args ...args);
    // *INDENT-ON*

    /** Calls an event on the queue periodically
     *
     *  @note The first call_every event occurs after the specified delay.
     *  To create a periodic event that fires immediately, @see Event.
     *
     *  The specified callback is executed in the context of the event
     *  queue's dispatch loop.
     *
     *  The call_every function is IRQ safe and can act as a mechanism for
     *  moving events out of IRQ contexts.
     *
     *  @param ms       Period of the event in milliseconds
     *  @param f        Function to execute in the context of the dispatch loop
     *  @param args     Arguments to pass to the callback
     *  @return         A unique ID that represents the posted event and can
     *                  be passed to cancel, or an ID of 0 if there is not
     *                  enough memory to allocate the event.
     *
     * @code
     *     #include "mbed.h"
     *
     *     class EventHandler {
     *         int _id;
     *     public:
     *         EventHandler(int id) : _id(id) { }
     *
     *         void handler(int c) {
     *             printf("ID: %d Param: %d\r\n", _id, c);
     *         }
     *     };
     *
     *     int main() {
     *         // creates a queue with the default size
     *         EventQueue queue;
     *
     *         // events are simple callbacks, call every 2 seconds
     *         queue.call_every(2000, printf, "Calling every 2 seconds\n");
     *
     *         // the dispatch method executes events
     *         queue.dispatch();
     *     }
     * @endcode
     */
    template <typename F, typename ...Args>
    int call_every(int ms, F f, Args ...args);

    /** Calls an event on the queue periodically
     *
     *  @note The first call_every event occurs after the specified delay.
     *  To create a periodic event that fires immediately, @see Event.
     *
     *  The specified callback is executed in the context of the event
     *  queue's dispatch loop.
     *
     *  The call_every function is IRQ safe and can act as a mechanism for
     *  moving events out of IRQ contexts.
     *
     *  @param ms       Period of the event in milliseconds
     *  @param obj      Object to call with the member function
     *  @param method   Member function to execute in the context of the dispatch loop
     *  @param args     Arguments to pass to the callback
     *
     * @code
     *     #include "mbed.h"
     *
     *     class EventHandler {
     *         int _id;
     *     public:
     *         EventHandler(int id) : _id(id) { }
     *
     *         void handler(int c) {
     *             printf("ID: %d Param: %d\r\n", _id, c);
     *         }
     *     };
     *
     *     int main() {
     *         // creates a queue with the default size
     *         EventQueue queue;
     *
     *         // Create EventHandler object with state
     *         EventHandler handler_cb(5);
     *
     *         // events are simple callbacks, call object method every 2 seconds
     *         // with provided parameter
     *         queue.call_every(2000, &handler_cb, &EventHandler::handler, 6);
     *
     *         // the dispatch method executes events
     *         queue.dispatch();
     *     }
     * @endcode
     */
    // AStyle ignore, not handling correctly below
    // *INDENT-OFF*
    template <typename T, typename R, typename ...Args>
    int call_every(int ms, T *obj, R (T::*method)(Args ...args), Args ...args);
    // *INDENT-ON*

    /** Creates an event bound to the event queue
     *
     *  Constructs an event bound to the specified event queue. The specified
     *  callback acts as the target for the event and is executed in the
     *  context of the event queue's dispatch loop once posted.
     *
     *  @param  func            Function to execute when the event is dispatched
     *  @param  context_args    Arguments to pass to the callback
     *  @return                 Event that dispatches on the specific queue
     *
     * @code
     *     #include "mbed.h"
     *
     *     void handler(int c) {
     *         printf("Param: %d\r\n", c);
     *     }
     *
     *     int main()
     *     {
     *         EventQueue queue;
     *
     *         // Create event with parameter
     *         Event<void()>    e  = queue.event(handler, 1);
     *         e();
     *
     *         // Create event and post parameter later
     *         Event<void(int)> e2 = queue.event(handler);
     *
     *         // Post the event with paramter 8
     *         e.post(8);
     *
     *         // The dispatch method executes events
     *         queue.dispatch();
     *
     *         e2.post(2);
     *
     *         queue.dispatch();
     *     }
     * @endcode
     */
    // AStyle ignore, not handling correctly below
    // *INDENT-OFF*
    template <typename R, typename ...BoundArgs, typename ...ContextArgs, typename ...Args>
    Event<void(Args...)> event(R (*func)(BoundArgs..., Args...), ContextArgs ...context_args);
    // *INDENT-ON*

    /** Creates an event bound to the event queue
     *
     *  Constructs an event bound to the specified event queue. The specified
     *  callback acts as the target for the event and is executed in the
     *  context of the event queue's dispatch loop once posted.
     *
     *  @param obj             Object to call with the member function
     *  @param method          Member function to execute in the context of the dispatch loop
     *  @param context_args    Arguments to pass to the callback
     *  @return                Event that dispatches on the specific queue
     *
     * @code
     *     #include "mbed.h"
     *
     *     class EventHandler {
     *         int _id;
     *
     *     public:
     *         EventHandler(int id) : _id(id) { }
     *
     *         void handler(int c) {
     *             printf("ID: %d Param: %d\r\n", _id, c);
     *         }
     *     };
     *
     *     int main()
     *     {
     *         EventQueue queue;
     *
     *         EventHandler handler_cb(10);
     *
     *         // Create event on the eventqueue with a method callback
     *         Event<void(int)> e = queue.event(&handler_cb, &EventHandler::handler);
     *
     *         // Post the event with paramter 8
     *         e.post(11);
     *
     *         // The dispatch method executes events
     *         queue.dispatch();
     *     }
     * @endcode
     */
    // AStyle ignore, not handling correctly below
    // *INDENT-OFF*
    template <typename T, typename R, typename ...BoundArgs, typename ...ContextArgs, typename ...Args>
    Event<void(Args...)> event(T *obj, R (T::*method)(BoundArgs..., Args...), ContextArgs ...context_args);
    // *INDENT-ON*

    /** Creates an event bound to the event queue
     *
     *  Constructs an event bound to the specified event queue. The specified
     *  callback acts as the target for the event and is executed in the
     *  context of the event queue's dispatch loop once posted.
     *
     *  @param  cb             Callback object
     *  @param  context_args   Arguments to pass to the callback
     *  @return                Event that dispatches on the specific queue
     *
     *  @code
     *     #include "mbed.h"
     *
     *     void handler(int c) {
     *         printf("Param: %d\r\n", c);
     *     }
     *
     *     int main()
     *     {
     *         EventQueue queue;
     *         // Create callback object acting as a function
     *         // pointer to handler
     *         Callback<void(int)> cb(handler);
     *
     *         // Pass the callback object to the eventqueue
     *         Event<void(int)> e = queue.event(cb);
     *
     *         // Post the event with parameter 8
     *         e.post(9);
     *
     *         // The dispatch method executes events
     *         q.dispatch();
     *     }
     *  @endcode
     */
    template <typename R, typename ...BoundArgs, typename ...ContextArgs, typename ...Args>
    Event<void(Args...)> event(mbed::Callback<R(BoundArgs..., Args...)> cb, ContextArgs ...context_args);

#else

    /** Calls an event on the queue
     *
     *  The specified callback is executed in the context of the event
     *  queue's dispatch loop.
     *
     *  The call function is IRQ safe and can act as a mechanism for moving
     *  events out of IRQ contexts.
     *
     *  @param f        Function to execute in the context of the dispatch loop
     *  @return         A unique ID that represents the posted event and can
     *                  be passed to cancel, or an ID of 0 if there is not
     *                  enough memory to allocate the event.
     *                  Returned ID remains valid until event has finished
     *                  executing.
     *
     * @code
     *     #include "mbed.h"
     *
     *     int main()
     *     {
     *         EventQueue queue;
     *
     *         Callback<void(int)> cb(handler);
     *
     *         // Create event on the eventqueue with a separate callback object
     *         Event<void(int)> e = queue.event(cb);
     *         e.post(1);
     *         queue.dispatch();
     *     }
     * @endcode
     */
    template <typename F>
    int call(F f)
    {
        void *p = equeue_alloc(&_equeue, sizeof(F));
        if (!p) {
            return 0;
        }

        F *e = new (p) F(f);
        equeue_event_dtor(e, &EventQueue::function_dtor<F>);
        return equeue_post(&_equeue, &EventQueue::function_call<F>, e);
    }


    /** Calls an event on the queue
     *  @see                    EventQueue::call
     *  @param f                Function to execute in the context of the dispatch loop
     *  @param args             Arguments to pass to the callback
     */
    template <typename F, typename... ArgTs>
    int call(F f, ArgTs... args)
    {
        return call(context<F, ArgTs...>(f, args...));
    }

    /** Calls an event on the queue
     *  @see EventQueue::call
     */
    template <typename T, typename R, typename... ArgTs>
    int call(T *obj, R(T::*method)(ArgTs...), ArgTs... args)
    {
        return call(mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue
     *  @see EventQueue::call
     */
    template <typename T, typename R, typename... ArgTs>
    int call(const T *obj, R(T::*method)(ArgTs...) const, ArgTs... args)
    {
        return call(mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue
     *  @see EventQueue::call
     */
    template <typename T, typename R, typename... ArgTs>
    int call(volatile T *obj, R(T::*method)(ArgTs...) volatile, ArgTs... args)
    {
        return call(mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue
     *  @see EventQueue::call
     */
    template <typename T, typename R, typename... ArgTs>
    int call(const volatile T *obj, R(T::*method)(ArgTs...) const volatile, ArgTs... args)
    {
        return call(mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue after a specified delay
     *
     *  The specified callback will be executed in the context of the event
     *  queue's dispatch loop.
     *
     *  The call_in function is IRQ safe and can act as a mechanism for moving
     *  events out of IRQ contexts.
     *
     *  @param ms       Time to delay in milliseconds
     *  @param f        Function to execute in the context of the dispatch loop
     *  @return         A unique id that represents the posted event and can
     *                  be passed to cancel, or an id of 0 if there is not
     *                  enough memory to allocate the event.
     */
    template <typename F>
    int call_in(int ms, F f)
    {
        void *p = equeue_alloc(&_equeue, sizeof(F));
        if (!p) {
            return 0;
        }

        F *e = new (p) F(f);
        equeue_event_delay(e, ms);
        equeue_event_dtor(e, &EventQueue::function_dtor<F>);
        return equeue_post(&_equeue, &EventQueue::function_call<F>, e);
    }

    /** Calls an event on the queue after a specified delay
     *  @see                        EventQueue::call_in
     *  @param ms                   Time to delay in milliseconds
     *  @param f                    Function to execute in the context of the dispatch loop
     *  @param args                 Arguments to pass to the callback
     */
    template <typename F, typename... ArgTs>
    int call_in(int ms, F f, ArgTs... args)
    {
        return call_in(ms, context<F, ArgTs...>(f, args...));
    }

    /** Calls an event on the queue after a specified delay
     *  @see EventQueue::call_in
     */
    template <typename T, typename R, typename... ArgTs>
    int call_in(int ms, T *obj, R(T::*method)(ArgTs...), ArgTs... args)
    {
        return call_in(ms, mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue after a specified delay
     *  @see EventQueue::call_in
     */
    template <typename T, typename R, typename... ArgTs>
    int call_in(int ms, const T *obj, R(T::*method)(ArgTs...) const, ArgTs... args)
    {
        return call_in(ms, mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue after a specified delay
     *  @see EventQueue::call_in
     */
    template <typename T, typename R, typename... ArgTs>
    int call_in(int ms, volatile T *obj, R(T::*method)(ArgTs...) volatile, ArgTs... args)
    {
        return call_in(ms, mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue after a specified delay
     *  @see EventQueue::call_in
     */
    template <typename T, typename R, typename... ArgTs>
    int call_in(int ms, const volatile T *obj, R(T::*method)(ArgTs...) const volatile, ArgTs... args)
    {
        return call_in(ms, mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue periodically
     *
     *  @note The first call_every event occurs after the specified delay.
     *  To create a periodic event that fires immediately, @see Event.
     *
     *  The specified callback will be executed in the context of the event
     *  queue's dispatch loop.
     *
     *  The call_every function is IRQ safe and can act as a mechanism for
     *  moving events out of IRQ contexts.
     *
     *  @param f        Function to execute in the context of the dispatch loop
     *  @param ms       Period of the event in milliseconds
     *  @return         A unique id that represents the posted event and can
     *                  be passed to cancel, or an id of 0 if there is not
     *                  enough memory to allocate the event.
     */
    template <typename F>
    int call_every(int ms, F f)
    {
        void *p = equeue_alloc(&_equeue, sizeof(F));
        if (!p) {
            return 0;
        }

        F *e = new (p) F(f);
        equeue_event_delay(e, ms);
        equeue_event_period(e, ms);
        equeue_event_dtor(e, &EventQueue::function_dtor<F>);
        return equeue_post(&_equeue, &EventQueue::function_call<F>, e);
    }

    /** Calls an event on the queue periodically
     *  @see                    EventQueue::call_every
     *  @param f                Function to execute in the context of the dispatch loop
     *  @param args             Arguments to pass to the callback
     *  @param ms               Period of the event in milliseconds
     */
    template <typename F, typename... ArgTs>
    int call_every(int ms, F f, ArgTs... args)
    {
        return call_every(ms, context<F, ArgTs...>(f, args...));
    }

    /** Calls an event on the queue periodically
     *  @see EventQueue::call_every
     */
    template <typename T, typename R, typename... ArgTs>
    int call_every(int ms, T *obj, R(T::*method)(ArgTs...), ArgTs... args)
    {
        return call_every(ms, mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue periodically
     *  @see EventQueue::call_every
     */
    template <typename T, typename R, typename... ArgTs>
    int call_every(int ms, const T *obj, R(T::*method)(ArgTs...) const, ArgTs... args)
    {
        return call_every(ms, mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue periodically
     *  @see EventQueue::call_every
     */
    template <typename T, typename R, typename... ArgTs>
    int call_every(int ms, volatile T *obj, R(T::*method)(ArgTs...) volatile, ArgTs... args)
    {
        return call_every(ms, mbed::callback(obj, method), args...);
    }

    /** Calls an event on the queue periodically
     *  @see EventQueue::call_every
     */
    template <typename T, typename R, typename... ArgTs>
    int call_every(int ms, const volatile T *obj, R(T::*method)(ArgTs...) const volatile, ArgTs... args)
    {
        return call_every(ms, mbed::callback(obj, method), args...);
    }

    /** Creates an event bound to the event queue
     *
     *  Constructs an event bound to the specified event queue. The specified
     *  callback acts as the target for the event and is executed in the
     *  context of the event queue's dispatch loop once posted.
     *
     *  @param func        Function to execute when the event is dispatched
     *  @return            Event that will dispatch on the specific queue
     */
    template <typename R, typename... ArgTs>
    Event<void(ArgTs...)> event(R(*func)(ArgTs...));

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename... ArgTs>
    Event<void(ArgTs...)> event(T *obj, R(T::*method)(ArgTs...));

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename... ArgTs>
    Event<void(ArgTs...)> event(const T *obj, R(T::*method)(ArgTs...) const);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename... ArgTs>
    Event<void(ArgTs...)> event(volatile T *obj, R(T::*method)(ArgTs...) volatile);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename... ArgTs>
    Event<void(ArgTs...)> event(const volatile T *obj, R(T::*method)(ArgTs...) const volatile);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename... ArgTs>
    Event<void(ArgTs...)> event(mbed::Callback<R(ArgTs...)> cb);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename B0, typename C0, typename... ArgTs>
    Event<void(ArgTs...)> event(R(*func)(B0, ArgTs...), C0 c0);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename C0, typename... ArgTs>
    Event<void(ArgTs...)> event(T *obj, R(T::*method)(B0, ArgTs...), C0 c0);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename C0, typename... ArgTs>
    Event<void(ArgTs...)> event(const T *obj, R(T::*method)(B0, ArgTs...) const, C0 c0);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename C0, typename... ArgTs>
    Event<void(ArgTs...)> event(volatile T *obj, R(T::*method)(B0, ArgTs...) volatile, C0 c0);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename C0, typename... ArgTs>
    Event<void(ArgTs...)> event(const volatile T *obj, R(T::*method)(B0, ArgTs...) const volatile, C0 c0);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename B0, typename C0, typename... ArgTs>
    Event<void(ArgTs...)> event(mbed::Callback<R(B0, ArgTs...)> cb, C0 c0);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename B0, typename B1, typename C0, typename C1, typename... ArgTs>
    Event<void(ArgTs...)> event(R(*func)(B0, B1, ArgTs...), C0 c0, C1 c1);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename C0, typename C1, typename... ArgTs>
    Event<void(ArgTs...)> event(T *obj, R(T::*method)(B0, B1, ArgTs...), C0 c0, C1 c1);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename C0, typename C1, typename... ArgTs>
    Event<void(ArgTs...)> event(const T *obj, R(T::*method)(B0, B1, ArgTs...) const, C0 c0, C1 c1);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename C0, typename C1, typename... ArgTs>
    Event<void(ArgTs...)> event(volatile T *obj, R(T::*method)(B0, B1, ArgTs...) volatile, C0 c0, C1 c1);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename C0, typename C1, typename... ArgTs>
    Event<void(ArgTs...)> event(const volatile T *obj, R(T::*method)(B0, B1, ArgTs...) const volatile, C0 c0, C1 c1);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename B0, typename B1, typename C0, typename C1, typename... ArgTs>
    Event<void(ArgTs...)> event(mbed::Callback<R(B0, B1, ArgTs...)> cb, C0 c0, C1 c1);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename B0, typename B1, typename B2, typename C0, typename C1, typename C2, typename... ArgTs>
    Event<void(ArgTs...)> event(R(*func)(B0, B1, B2, ArgTs...), C0 c0, C1 c1, C2 c2);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename C0, typename C1, typename C2, typename... ArgTs>
    Event<void(ArgTs...)> event(T *obj, R(T::*method)(B0, B1, B2, ArgTs...), C0 c0, C1 c1, C2 c2);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename C0, typename C1, typename C2, typename... ArgTs>
    Event<void(ArgTs...)> event(const T *obj, R(T::*method)(B0, B1, B2, ArgTs...) const, C0 c0, C1 c1, C2 c2);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename C0, typename C1, typename C2, typename... ArgTs>
    Event<void(ArgTs...)> event(volatile T *obj, R(T::*method)(B0, B1, B2, ArgTs...) volatile, C0 c0, C1 c1, C2 c2);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename C0, typename C1, typename C2, typename... ArgTs>
    Event<void(ArgTs...)> event(const volatile T *obj, R(T::*method)(B0, B1, B2, ArgTs...) const volatile, C0 c0, C1 c1, C2 c2);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename B0, typename B1, typename B2, typename C0, typename C1, typename C2, typename... ArgTs>
    Event<void(ArgTs...)> event(mbed::Callback<R(B0, B1, B2, ArgTs...)> cb, C0 c0, C1 c1, C2 c2);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename B0, typename B1, typename B2, typename B3, typename C0, typename C1, typename C2, typename C3, typename... ArgTs>
    Event<void(ArgTs...)> event(R(*func)(B0, B1, B2, B3, ArgTs...), C0 c0, C1 c1, C2 c2, C3 c3);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename B3, typename C0, typename C1, typename C2, typename C3, typename... ArgTs>
    Event<void(ArgTs...)> event(T *obj, R(T::*method)(B0, B1, B2, B3, ArgTs...), C0 c0, C1 c1, C2 c2, C3 c3);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename B3, typename C0, typename C1, typename C2, typename C3, typename... ArgTs>
    Event<void(ArgTs...)> event(const T *obj, R(T::*method)(B0, B1, B2, B3, ArgTs...) const, C0 c0, C1 c1, C2 c2, C3 c3);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename B3, typename C0, typename C1, typename C2, typename C3, typename... ArgTs>
    Event<void(ArgTs...)> event(volatile T *obj, R(T::*method)(B0, B1, B2, B3, ArgTs...) volatile, C0 c0, C1 c1, C2 c2, C3 c3);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename B3, typename C0, typename C1, typename C2, typename C3, typename... ArgTs>
    Event<void(ArgTs...)> event(const volatile T *obj, R(T::*method)(B0, B1, B2, B3, ArgTs...) const volatile, C0 c0, C1 c1, C2 c2, C3 c3);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename B0, typename B1, typename B2, typename B3, typename C0, typename C1, typename C2, typename C3, typename... ArgTs>
    Event<void(ArgTs...)> event(mbed::Callback<R(B0, B1, B2, B3, ArgTs...)> cb, C0 c0, C1 c1, C2 c2, C3 c3);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename B0, typename B1, typename B2, typename B3, typename B4, typename C0, typename C1, typename C2, typename C3, typename C4, typename... ArgTs>
    Event<void(ArgTs...)> event(R(*func)(B0, B1, B2, B3, B4, ArgTs...), C0 c0, C1 c1, C2 c2, C3 c3, C4 c4);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename B3, typename B4, typename C0, typename C1, typename C2, typename C3, typename C4, typename... ArgTs>
    Event<void(ArgTs...)> event(T *obj, R(T::*method)(B0, B1, B2, B3, B4, ArgTs...), C0 c0, C1 c1, C2 c2, C3 c3, C4 c4);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename B3, typename B4, typename C0, typename C1, typename C2, typename C3, typename C4, typename... ArgTs>
    Event<void(ArgTs...)> event(const T *obj, R(T::*method)(B0, B1, B2, B3, B4, ArgTs...) const, C0 c0, C1 c1, C2 c2, C3 c3, C4 c4);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename B3, typename B4, typename C0, typename C1, typename C2, typename C3, typename C4, typename... ArgTs>
    Event<void(ArgTs...)> event(volatile T *obj, R(T::*method)(B0, B1, B2, B3, B4, ArgTs...) volatile, C0 c0, C1 c1, C2 c2, C3 c3, C4 c4);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename T, typename R, typename B0, typename B1, typename B2, typename B3, typename B4, typename C0, typename C1, typename C2, typename C3, typename C4, typename... ArgTs>
    Event<void(ArgTs...)> event(const volatile T *obj, R(T::*method)(B0, B1, B2, B3, B4, ArgTs...) const volatile, C0 c0, C1 c1, C2 c2, C3 c3, C4 c4);

    /** Creates an event bound to the event queue
     *  @see EventQueue::event
     */
    template <typename R, typename B0, typename B1, typename B2, typename B3, typename B4, typename C0, typename C1, typename C2, typename C3, typename C4, typename... ArgTs>
    Event<void(ArgTs...)> event(mbed::Callback<R(B0, B1, B2, B3, B4, ArgTs...)> cb, C0 c0, C1 c1, C2 c2, C3 c3, C4 c4);
#endif

protected:
#if !defined(DOXYGEN_ONLY)
    template <typename F>
    friend class Event;
    struct equeue _equeue;
    mbed::Callback<void(int)> _update;

    // Function attributes
    template <typename F>
    static void function_call(void *p)
    {
        (*(F *)p)();
    }

    template <typename F>
    static void function_dtor(void *p)
    {
        ((F *)p)->~F();
    }

    // Context structures
    template <typename F, typename... ContextArgTs>
    struct context;

    template <typename F>
    struct context<F> {
        F f;

        context(F f)
            : f(f) {}

        template <typename... ArgTs>
        void operator()(ArgTs... args)
        {
            f(args...);
        }
    };

    template <typename F, typename C0>
    struct context<F, C0> {
        F f;
        C0 c0;

        context(F f, C0 c0)
            : f(f), c0(c0) {}

        template <typename... ArgTs>
        void operator()(ArgTs... args)
        {
            f(c0, args...);
        }
    };

    template <typename F, typename C0, typename C1>
    struct context<F, C0, C1> {
        F f;
        C0 c0;
        C1 c1;

        context(F f, C0 c0, C1 c1)
            : f(f), c0(c0), c1(c1) {}

        template <typename... ArgTs>
        void operator()(ArgTs... args)
        {
            f(c0, c1, args...);
        }
    };

    template <typename F, typename C0, typename C1, typename C2>
    struct context<F, C0, C1, C2> {
        F f;
        C0 c0;
        C1 c1;
        C2 c2;

        context(F f, C0 c0, C1 c1, C2 c2)
            : f(f), c0(c0), c1(c1), c2(c2) {}

        template <typename... ArgTs>
        void operator()(ArgTs... args)
        {
            f(c0, c1, c2, args...);
        }
    };

    template <typename F, typename C0, typename C1, typename C2, typename C3>
    struct context<F, C0, C1, C2, C3> {
        F f;
        C0 c0;
        C1 c1;
        C2 c2;
        C3 c3;

        context(F f, C0 c0, C1 c1, C2 c2, C3 c3)
            : f(f), c0(c0), c1(c1), c2(c2), c3(c3) {}

        template <typename... ArgTs>
        void operator()(ArgTs... args)
        {
            f(c0, c1, c2, c3, args...);
        }
    };

    template <typename F, typename C0, typename C1, typename C2, typename C3, typename C4>
    struct context<F, C0, C1, C2, C3, C4> {
        F f;
        C0 c0;
        C1 c1;
        C2 c2;
        C3 c3;
        C4 c4;

        context(F f, C0 c0, C1 c1, C2 c2, C3 c3, C4 c4)
            : f(f), c0(c0), c1(c1), c2(c2), c3(c3), c4(c4) {}

        template <typename... ArgTs>
        void operator()(ArgTs... args)
        {
            f(c0, c1, c2, c3, c4, args...);
        }
    };
#endif //!defined(DOXYGEN_ONLY)
};

/** @}*/
/** @}*/

}

#endif
