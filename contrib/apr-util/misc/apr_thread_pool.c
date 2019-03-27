/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed
 * with this work for additional information regarding copyright
 * ownership.  The ASF licenses this file to you under the Apache
 * License, Version 2.0 (the "License"); you may not use this file
 * except in compliance with the License.  You may obtain a copy of
 * the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include <assert.h>
#include "apr_thread_pool.h"
#include "apr_ring.h"
#include "apr_thread_cond.h"
#include "apr_portable.h"

#if APR_HAS_THREADS

#define TASK_PRIORITY_SEGS 4
#define TASK_PRIORITY_SEG(x) (((x)->dispatch.priority & 0xFF) / 64)

typedef struct apr_thread_pool_task
{
    APR_RING_ENTRY(apr_thread_pool_task) link;
    apr_thread_start_t func;
    void *param;
    void *owner;
    union
    {
        apr_byte_t priority;
        apr_time_t time;
    } dispatch;
} apr_thread_pool_task_t;

APR_RING_HEAD(apr_thread_pool_tasks, apr_thread_pool_task);

struct apr_thread_list_elt
{
    APR_RING_ENTRY(apr_thread_list_elt) link;
    apr_thread_t *thd;
    volatile void *current_owner;
    volatile enum { TH_RUN, TH_STOP, TH_PROBATION } state;
};

APR_RING_HEAD(apr_thread_list, apr_thread_list_elt);

struct apr_thread_pool
{
    apr_pool_t *pool;
    volatile apr_size_t thd_max;
    volatile apr_size_t idle_max;
    volatile apr_interval_time_t idle_wait;
    volatile apr_size_t thd_cnt;
    volatile apr_size_t idle_cnt;
    volatile apr_size_t task_cnt;
    volatile apr_size_t scheduled_task_cnt;
    volatile apr_size_t threshold;
    volatile apr_size_t tasks_run;
    volatile apr_size_t tasks_high;
    volatile apr_size_t thd_high;
    volatile apr_size_t thd_timed_out;
    struct apr_thread_pool_tasks *tasks;
    struct apr_thread_pool_tasks *scheduled_tasks;
    struct apr_thread_list *busy_thds;
    struct apr_thread_list *idle_thds;
    apr_thread_mutex_t *lock;
    apr_thread_cond_t *cond;
    volatile int terminated;
    struct apr_thread_pool_tasks *recycled_tasks;
    struct apr_thread_list *recycled_thds;
    apr_thread_pool_task_t *task_idx[TASK_PRIORITY_SEGS];
};

static apr_status_t thread_pool_construct(apr_thread_pool_t * me,
                                          apr_size_t init_threads,
                                          apr_size_t max_threads)
{
    apr_status_t rv;
    int i;

    me->thd_max = max_threads;
    me->idle_max = init_threads;
    me->threshold = init_threads / 2;
    rv = apr_thread_mutex_create(&me->lock, APR_THREAD_MUTEX_NESTED,
                                 me->pool);
    if (APR_SUCCESS != rv) {
        return rv;
    }
    rv = apr_thread_cond_create(&me->cond, me->pool);
    if (APR_SUCCESS != rv) {
        apr_thread_mutex_destroy(me->lock);
        return rv;
    }
    me->tasks = apr_palloc(me->pool, sizeof(*me->tasks));
    if (!me->tasks) {
        goto CATCH_ENOMEM;
    }
    APR_RING_INIT(me->tasks, apr_thread_pool_task, link);
    me->scheduled_tasks = apr_palloc(me->pool, sizeof(*me->scheduled_tasks));
    if (!me->scheduled_tasks) {
        goto CATCH_ENOMEM;
    }
    APR_RING_INIT(me->scheduled_tasks, apr_thread_pool_task, link);
    me->recycled_tasks = apr_palloc(me->pool, sizeof(*me->recycled_tasks));
    if (!me->recycled_tasks) {
        goto CATCH_ENOMEM;
    }
    APR_RING_INIT(me->recycled_tasks, apr_thread_pool_task, link);
    me->busy_thds = apr_palloc(me->pool, sizeof(*me->busy_thds));
    if (!me->busy_thds) {
        goto CATCH_ENOMEM;
    }
    APR_RING_INIT(me->busy_thds, apr_thread_list_elt, link);
    me->idle_thds = apr_palloc(me->pool, sizeof(*me->idle_thds));
    if (!me->idle_thds) {
        goto CATCH_ENOMEM;
    }
    APR_RING_INIT(me->idle_thds, apr_thread_list_elt, link);
    me->recycled_thds = apr_palloc(me->pool, sizeof(*me->recycled_thds));
    if (!me->recycled_thds) {
        goto CATCH_ENOMEM;
    }
    APR_RING_INIT(me->recycled_thds, apr_thread_list_elt, link);
    me->thd_cnt = me->idle_cnt = me->task_cnt = me->scheduled_task_cnt = 0;
    me->tasks_run = me->tasks_high = me->thd_high = me->thd_timed_out = 0;
    me->idle_wait = 0;
    me->terminated = 0;
    for (i = 0; i < TASK_PRIORITY_SEGS; i++) {
        me->task_idx[i] = NULL;
    }
    goto FINAL_EXIT;
  CATCH_ENOMEM:
    rv = APR_ENOMEM;
    apr_thread_mutex_destroy(me->lock);
    apr_thread_cond_destroy(me->cond);
  FINAL_EXIT:
    return rv;
}

/*
 * NOTE: This function is not thread safe by itself. Caller should hold the lock
 */
static apr_thread_pool_task_t *pop_task(apr_thread_pool_t * me)
{
    apr_thread_pool_task_t *task = NULL;
    int seg;

    /* check for scheduled tasks */
    if (me->scheduled_task_cnt > 0) {
        task = APR_RING_FIRST(me->scheduled_tasks);
        assert(task != NULL);
        assert(task !=
               APR_RING_SENTINEL(me->scheduled_tasks, apr_thread_pool_task,
                                 link));
        /* if it's time */
        if (task->dispatch.time <= apr_time_now()) {
            --me->scheduled_task_cnt;
            APR_RING_REMOVE(task, link);
            return task;
        }
    }
    /* check for normal tasks if we're not returning a scheduled task */
    if (me->task_cnt == 0) {
        return NULL;
    }

    task = APR_RING_FIRST(me->tasks);
    assert(task != NULL);
    assert(task != APR_RING_SENTINEL(me->tasks, apr_thread_pool_task, link));
    --me->task_cnt;
    seg = TASK_PRIORITY_SEG(task);
    if (task == me->task_idx[seg]) {
        me->task_idx[seg] = APR_RING_NEXT(task, link);
        if (me->task_idx[seg] == APR_RING_SENTINEL(me->tasks,
                                                   apr_thread_pool_task, link)
            || TASK_PRIORITY_SEG(me->task_idx[seg]) != seg) {
            me->task_idx[seg] = NULL;
        }
    }
    APR_RING_REMOVE(task, link);
    return task;
}

static apr_interval_time_t waiting_time(apr_thread_pool_t * me)
{
    apr_thread_pool_task_t *task = NULL;

    task = APR_RING_FIRST(me->scheduled_tasks);
    assert(task != NULL);
    assert(task !=
           APR_RING_SENTINEL(me->scheduled_tasks, apr_thread_pool_task,
                             link));
    return task->dispatch.time - apr_time_now();
}

/*
 * NOTE: This function is not thread safe by itself. Caller should hold the lock
 */
static struct apr_thread_list_elt *elt_new(apr_thread_pool_t * me,
                                           apr_thread_t * t)
{
    struct apr_thread_list_elt *elt;

    if (APR_RING_EMPTY(me->recycled_thds, apr_thread_list_elt, link)) {
        elt = apr_pcalloc(me->pool, sizeof(*elt));
        if (NULL == elt) {
            return NULL;
        }
    }
    else {
        elt = APR_RING_FIRST(me->recycled_thds);
        APR_RING_REMOVE(elt, link);
    }

    APR_RING_ELEM_INIT(elt, link);
    elt->thd = t;
    elt->current_owner = NULL;
    elt->state = TH_RUN;
    return elt;
}

/*
 * The worker thread function. Take a task from the queue and perform it if
 * there is any. Otherwise, put itself into the idle thread list and waiting
 * for signal to wake up.
 * The thread terminate directly by detach and exit when it is asked to stop
 * after finishing a task. Otherwise, the thread should be in idle thread list
 * and should be joined.
 */
static void *APR_THREAD_FUNC thread_pool_func(apr_thread_t * t, void *param)
{
    apr_thread_pool_t *me = param;
    apr_thread_pool_task_t *task = NULL;
    apr_interval_time_t wait;
    struct apr_thread_list_elt *elt;

    apr_thread_mutex_lock(me->lock);
    elt = elt_new(me, t);
    if (!elt) {
        apr_thread_mutex_unlock(me->lock);
        apr_thread_exit(t, APR_ENOMEM);
    }

    while (!me->terminated && elt->state != TH_STOP) {
        /* Test if not new element, it is awakened from idle */
        if (APR_RING_NEXT(elt, link) != elt) {
            --me->idle_cnt;
            APR_RING_REMOVE(elt, link);
        }

        APR_RING_INSERT_TAIL(me->busy_thds, elt, apr_thread_list_elt, link);
        task = pop_task(me);
        while (NULL != task && !me->terminated) {
            ++me->tasks_run;
            elt->current_owner = task->owner;
            apr_thread_mutex_unlock(me->lock);
            apr_thread_data_set(task, "apr_thread_pool_task", NULL, t);
            task->func(t, task->param);
            apr_thread_mutex_lock(me->lock);
            APR_RING_INSERT_TAIL(me->recycled_tasks, task,
                                 apr_thread_pool_task, link);
            elt->current_owner = NULL;
            if (TH_STOP == elt->state) {
                break;
            }
            task = pop_task(me);
        }
        assert(NULL == elt->current_owner);
        if (TH_STOP != elt->state)
            APR_RING_REMOVE(elt, link);

        /* Test if a busy thread been asked to stop, which is not joinable */
        if ((me->idle_cnt >= me->idle_max
             && !(me->scheduled_task_cnt && 0 >= me->idle_max)
             && !me->idle_wait)
            || me->terminated || elt->state != TH_RUN) {
            --me->thd_cnt;
            if ((TH_PROBATION == elt->state) && me->idle_wait)
                ++me->thd_timed_out;
            APR_RING_INSERT_TAIL(me->recycled_thds, elt,
                                 apr_thread_list_elt, link);
            apr_thread_mutex_unlock(me->lock);
            apr_thread_detach(t);
            apr_thread_exit(t, APR_SUCCESS);
            return NULL;        /* should not be here, safe net */
        }

        /* busy thread become idle */
        ++me->idle_cnt;
        APR_RING_INSERT_TAIL(me->idle_thds, elt, apr_thread_list_elt, link);

        /* 
         * If there is a scheduled task, always scheduled to perform that task.
         * Since there is no guarantee that current idle threads are scheduled
         * for next scheduled task.
         */
        if (me->scheduled_task_cnt)
            wait = waiting_time(me);
        else if (me->idle_cnt > me->idle_max) {
            wait = me->idle_wait;
            elt->state = TH_PROBATION;
        }
        else
            wait = -1;

        if (wait >= 0) {
            apr_thread_cond_timedwait(me->cond, me->lock, wait);
        }
        else {
            apr_thread_cond_wait(me->cond, me->lock);
        }
    }

    /* idle thread been asked to stop, will be joined */
    --me->thd_cnt;
    apr_thread_mutex_unlock(me->lock);
    apr_thread_exit(t, APR_SUCCESS);
    return NULL;                /* should not be here, safe net */
}

static apr_status_t thread_pool_cleanup(void *me)
{
    apr_thread_pool_t *_myself = me;

    _myself->terminated = 1;
    apr_thread_pool_idle_max_set(_myself, 0);
    while (_myself->thd_cnt) {
        apr_sleep(20 * 1000);   /* spin lock with 20 ms */
    }
    apr_thread_mutex_destroy(_myself->lock);
    apr_thread_cond_destroy(_myself->cond);
    return APR_SUCCESS;
}

APU_DECLARE(apr_status_t) apr_thread_pool_create(apr_thread_pool_t ** me,
                                                 apr_size_t init_threads,
                                                 apr_size_t max_threads,
                                                 apr_pool_t * pool)
{
    apr_thread_t *t;
    apr_status_t rv = APR_SUCCESS;
    apr_thread_pool_t *tp;

    *me = NULL;
    tp = apr_pcalloc(pool, sizeof(apr_thread_pool_t));

    /*
     * This pool will be used by different threads. As we cannot ensure that
     * our caller won't use the pool without acquiring the mutex, we must
     * create a new sub pool.
     */
    rv = apr_pool_create(&tp->pool, pool);
    if (APR_SUCCESS != rv)
        return rv;
    rv = thread_pool_construct(tp, init_threads, max_threads);
    if (APR_SUCCESS != rv)
        return rv;
    apr_pool_pre_cleanup_register(tp->pool, tp, thread_pool_cleanup);

    while (init_threads) {
        /* Grab the mutex as apr_thread_create() and thread_pool_func() will 
         * allocate from (*me)->pool. This is dangerous if there are multiple 
         * initial threads to create.
         */
        apr_thread_mutex_lock(tp->lock);
        rv = apr_thread_create(&t, NULL, thread_pool_func, tp, tp->pool);
        apr_thread_mutex_unlock(tp->lock);
        if (APR_SUCCESS != rv) {
            break;
        }
        tp->thd_cnt++;
        if (tp->thd_cnt > tp->thd_high) {
            tp->thd_high = tp->thd_cnt;
        }
        --init_threads;
    }

    if (rv == APR_SUCCESS) {
        *me = tp;
    }

    return rv;
}

APU_DECLARE(apr_status_t) apr_thread_pool_destroy(apr_thread_pool_t * me)
{
    apr_pool_destroy(me->pool);
    return APR_SUCCESS;
}

/*
 * NOTE: This function is not thread safe by itself. Caller should hold the lock
 */
static apr_thread_pool_task_t *task_new(apr_thread_pool_t * me,
                                        apr_thread_start_t func,
                                        void *param, apr_byte_t priority,
                                        void *owner, apr_time_t time)
{
    apr_thread_pool_task_t *t;

    if (APR_RING_EMPTY(me->recycled_tasks, apr_thread_pool_task, link)) {
        t = apr_pcalloc(me->pool, sizeof(*t));
        if (NULL == t) {
            return NULL;
        }
    }
    else {
        t = APR_RING_FIRST(me->recycled_tasks);
        APR_RING_REMOVE(t, link);
    }

    APR_RING_ELEM_INIT(t, link);
    t->func = func;
    t->param = param;
    t->owner = owner;
    if (time > 0) {
        t->dispatch.time = apr_time_now() + time;
    }
    else {
        t->dispatch.priority = priority;
    }
    return t;
}

/*
 * Test it the task is the only one within the priority segment. 
 * If it is not, return the first element with same or lower priority. 
 * Otherwise, add the task into the queue and return NULL.
 *
 * NOTE: This function is not thread safe by itself. Caller should hold the lock
 */
static apr_thread_pool_task_t *add_if_empty(apr_thread_pool_t * me,
                                            apr_thread_pool_task_t * const t)
{
    int seg;
    int next;
    apr_thread_pool_task_t *t_next;

    seg = TASK_PRIORITY_SEG(t);
    if (me->task_idx[seg]) {
        assert(APR_RING_SENTINEL(me->tasks, apr_thread_pool_task, link) !=
               me->task_idx[seg]);
        t_next = me->task_idx[seg];
        while (t_next->dispatch.priority > t->dispatch.priority) {
            t_next = APR_RING_NEXT(t_next, link);
            if (APR_RING_SENTINEL(me->tasks, apr_thread_pool_task, link) ==
                t_next) {
                return t_next;
            }
        }
        return t_next;
    }

    for (next = seg - 1; next >= 0; next--) {
        if (me->task_idx[next]) {
            APR_RING_INSERT_BEFORE(me->task_idx[next], t, link);
            break;
        }
    }
    if (0 > next) {
        APR_RING_INSERT_TAIL(me->tasks, t, apr_thread_pool_task, link);
    }
    me->task_idx[seg] = t;
    return NULL;
}

/*
*   schedule a task to run in "time" microseconds. Find the spot in the ring where
*   the time fits. Adjust the short_time so the thread wakes up when the time is reached.
*/
static apr_status_t schedule_task(apr_thread_pool_t *me,
                                  apr_thread_start_t func, void *param,
                                  void *owner, apr_interval_time_t time)
{
    apr_thread_pool_task_t *t;
    apr_thread_pool_task_t *t_loc;
    apr_thread_t *thd;
    apr_status_t rv = APR_SUCCESS;
    apr_thread_mutex_lock(me->lock);

    t = task_new(me, func, param, 0, owner, time);
    if (NULL == t) {
        apr_thread_mutex_unlock(me->lock);
        return APR_ENOMEM;
    }
    t_loc = APR_RING_FIRST(me->scheduled_tasks);
    while (NULL != t_loc) {
        /* if the time is less than the entry insert ahead of it */
        if (t->dispatch.time < t_loc->dispatch.time) {
            ++me->scheduled_task_cnt;
            APR_RING_INSERT_BEFORE(t_loc, t, link);
            break;
        }
        else {
            t_loc = APR_RING_NEXT(t_loc, link);
            if (t_loc ==
                APR_RING_SENTINEL(me->scheduled_tasks, apr_thread_pool_task,
                                  link)) {
                ++me->scheduled_task_cnt;
                APR_RING_INSERT_TAIL(me->scheduled_tasks, t,
                                     apr_thread_pool_task, link);
                break;
            }
        }
    }
    /* there should be at least one thread for scheduled tasks */
    if (0 == me->thd_cnt) {
        rv = apr_thread_create(&thd, NULL, thread_pool_func, me, me->pool);
        if (APR_SUCCESS == rv) {
            ++me->thd_cnt;
            if (me->thd_cnt > me->thd_high)
                me->thd_high = me->thd_cnt;
        }
    }
    apr_thread_cond_signal(me->cond);
    apr_thread_mutex_unlock(me->lock);
    return rv;
}

static apr_status_t add_task(apr_thread_pool_t *me, apr_thread_start_t func,
                             void *param, apr_byte_t priority, int push,
                             void *owner)
{
    apr_thread_pool_task_t *t;
    apr_thread_pool_task_t *t_loc;
    apr_thread_t *thd;
    apr_status_t rv = APR_SUCCESS;

    apr_thread_mutex_lock(me->lock);

    t = task_new(me, func, param, priority, owner, 0);
    if (NULL == t) {
        apr_thread_mutex_unlock(me->lock);
        return APR_ENOMEM;
    }

    t_loc = add_if_empty(me, t);
    if (NULL == t_loc) {
        goto FINAL_EXIT;
    }

    if (push) {
        while (APR_RING_SENTINEL(me->tasks, apr_thread_pool_task, link) !=
               t_loc && t_loc->dispatch.priority >= t->dispatch.priority) {
            t_loc = APR_RING_NEXT(t_loc, link);
        }
    }
    APR_RING_INSERT_BEFORE(t_loc, t, link);
    if (!push) {
        if (t_loc == me->task_idx[TASK_PRIORITY_SEG(t)]) {
            me->task_idx[TASK_PRIORITY_SEG(t)] = t;
        }
    }

  FINAL_EXIT:
    me->task_cnt++;
    if (me->task_cnt > me->tasks_high)
        me->tasks_high = me->task_cnt;
    if (0 == me->thd_cnt || (0 == me->idle_cnt && me->thd_cnt < me->thd_max &&
                             me->task_cnt > me->threshold)) {
        rv = apr_thread_create(&thd, NULL, thread_pool_func, me, me->pool);
        if (APR_SUCCESS == rv) {
            ++me->thd_cnt;
            if (me->thd_cnt > me->thd_high)
                me->thd_high = me->thd_cnt;
        }
    }

    apr_thread_cond_signal(me->cond);
    apr_thread_mutex_unlock(me->lock);

    return rv;
}

APU_DECLARE(apr_status_t) apr_thread_pool_push(apr_thread_pool_t *me,
                                               apr_thread_start_t func,
                                               void *param,
                                               apr_byte_t priority,
                                               void *owner)
{
    return add_task(me, func, param, priority, 1, owner);
}

APU_DECLARE(apr_status_t) apr_thread_pool_schedule(apr_thread_pool_t *me,
                                                   apr_thread_start_t func,
                                                   void *param,
                                                   apr_interval_time_t time,
                                                   void *owner)
{
    return schedule_task(me, func, param, owner, time);
}

APU_DECLARE(apr_status_t) apr_thread_pool_top(apr_thread_pool_t *me,
                                              apr_thread_start_t func,
                                              void *param,
                                              apr_byte_t priority,
                                              void *owner)
{
    return add_task(me, func, param, priority, 0, owner);
}

static apr_status_t remove_scheduled_tasks(apr_thread_pool_t *me,
                                           void *owner)
{
    apr_thread_pool_task_t *t_loc;
    apr_thread_pool_task_t *next;

    t_loc = APR_RING_FIRST(me->scheduled_tasks);
    while (t_loc !=
           APR_RING_SENTINEL(me->scheduled_tasks, apr_thread_pool_task,
                             link)) {
        next = APR_RING_NEXT(t_loc, link);
        /* if this is the owner remove it */
        if (t_loc->owner == owner) {
            --me->scheduled_task_cnt;
            APR_RING_REMOVE(t_loc, link);
        }
        t_loc = next;
    }
    return APR_SUCCESS;
}

static apr_status_t remove_tasks(apr_thread_pool_t *me, void *owner)
{
    apr_thread_pool_task_t *t_loc;
    apr_thread_pool_task_t *next;
    int seg;

    t_loc = APR_RING_FIRST(me->tasks);
    while (t_loc != APR_RING_SENTINEL(me->tasks, apr_thread_pool_task, link)) {
        next = APR_RING_NEXT(t_loc, link);
        if (t_loc->owner == owner) {
            --me->task_cnt;
            seg = TASK_PRIORITY_SEG(t_loc);
            if (t_loc == me->task_idx[seg]) {
                me->task_idx[seg] = APR_RING_NEXT(t_loc, link);
                if (me->task_idx[seg] == APR_RING_SENTINEL(me->tasks,
                                                           apr_thread_pool_task,
                                                           link)
                    || TASK_PRIORITY_SEG(me->task_idx[seg]) != seg) {
                    me->task_idx[seg] = NULL;
                }
            }
            APR_RING_REMOVE(t_loc, link);
        }
        t_loc = next;
    }
    return APR_SUCCESS;
}

static void wait_on_busy_threads(apr_thread_pool_t *me, void *owner)
{
#ifndef NDEBUG
    apr_os_thread_t *os_thread;
#endif
    struct apr_thread_list_elt *elt;
    apr_thread_mutex_lock(me->lock);
    elt = APR_RING_FIRST(me->busy_thds);
    while (elt != APR_RING_SENTINEL(me->busy_thds, apr_thread_list_elt, link)) {
        if (elt->current_owner != owner) {
            elt = APR_RING_NEXT(elt, link);
            continue;
        }
#ifndef NDEBUG
        /* make sure the thread is not the one calling tasks_cancel */
        apr_os_thread_get(&os_thread, elt->thd);
#ifdef WIN32
        /* hack for apr win32 bug */
        assert(!apr_os_thread_equal(apr_os_thread_current(), os_thread));
#else
        assert(!apr_os_thread_equal(apr_os_thread_current(), *os_thread));
#endif
#endif
        while (elt->current_owner == owner) {
            apr_thread_mutex_unlock(me->lock);
            apr_sleep(200 * 1000);
            apr_thread_mutex_lock(me->lock);
        }
        elt = APR_RING_FIRST(me->busy_thds);
    }
    apr_thread_mutex_unlock(me->lock);
    return;
}

APU_DECLARE(apr_status_t) apr_thread_pool_tasks_cancel(apr_thread_pool_t *me,
                                                       void *owner)
{
    apr_status_t rv = APR_SUCCESS;

    apr_thread_mutex_lock(me->lock);
    if (me->task_cnt > 0) {
        rv = remove_tasks(me, owner);
    }
    if (me->scheduled_task_cnt > 0) {
        rv = remove_scheduled_tasks(me, owner);
    }
    apr_thread_mutex_unlock(me->lock);
    wait_on_busy_threads(me, owner);

    return rv;
}

APU_DECLARE(apr_size_t) apr_thread_pool_tasks_count(apr_thread_pool_t *me)
{
    return me->task_cnt;
}

APU_DECLARE(apr_size_t)
    apr_thread_pool_scheduled_tasks_count(apr_thread_pool_t *me)
{
    return me->scheduled_task_cnt;
}

APU_DECLARE(apr_size_t) apr_thread_pool_threads_count(apr_thread_pool_t *me)
{
    return me->thd_cnt;
}

APU_DECLARE(apr_size_t) apr_thread_pool_busy_count(apr_thread_pool_t *me)
{
    return me->thd_cnt - me->idle_cnt;
}

APU_DECLARE(apr_size_t) apr_thread_pool_idle_count(apr_thread_pool_t *me)
{
    return me->idle_cnt;
}

APU_DECLARE(apr_size_t)
    apr_thread_pool_tasks_run_count(apr_thread_pool_t * me)
{
    return me->tasks_run;
}

APU_DECLARE(apr_size_t)
    apr_thread_pool_tasks_high_count(apr_thread_pool_t * me)
{
    return me->tasks_high;
}

APU_DECLARE(apr_size_t)
    apr_thread_pool_threads_high_count(apr_thread_pool_t * me)
{
    return me->thd_high;
}

APU_DECLARE(apr_size_t)
    apr_thread_pool_threads_idle_timeout_count(apr_thread_pool_t * me)
{
    return me->thd_timed_out;
}


APU_DECLARE(apr_size_t) apr_thread_pool_idle_max_get(apr_thread_pool_t *me)
{
    return me->idle_max;
}

APU_DECLARE(apr_interval_time_t)
    apr_thread_pool_idle_wait_get(apr_thread_pool_t * me)
{
    return me->idle_wait;
}

/*
 * This function stop extra idle threads to the cnt.
 * @return the number of threads stopped
 * NOTE: There could be busy threads become idle during this function
 */
static struct apr_thread_list_elt *trim_threads(apr_thread_pool_t *me,
                                                apr_size_t *cnt, int idle)
{
    struct apr_thread_list *thds;
    apr_size_t n, n_dbg, i;
    struct apr_thread_list_elt *head, *tail, *elt;

    apr_thread_mutex_lock(me->lock);
    if (idle) {
        thds = me->idle_thds;
        n = me->idle_cnt;
    }
    else {
        thds = me->busy_thds;
        n = me->thd_cnt - me->idle_cnt;
    }
    if (n <= *cnt) {
        apr_thread_mutex_unlock(me->lock);
        *cnt = 0;
        return NULL;
    }
    n -= *cnt;

    head = APR_RING_FIRST(thds);
    for (i = 0; i < *cnt; i++) {
        head = APR_RING_NEXT(head, link);
    }
    tail = APR_RING_LAST(thds);
    if (idle) {
        APR_RING_UNSPLICE(head, tail, link);
        me->idle_cnt = *cnt;
    }

    n_dbg = 0;
    for (elt = head; elt != tail; elt = APR_RING_NEXT(elt, link)) {
        elt->state = TH_STOP;
        n_dbg++;
    }
    elt->state = TH_STOP;
    n_dbg++;
    assert(n == n_dbg);
    *cnt = n;

    apr_thread_mutex_unlock(me->lock);

    APR_RING_PREV(head, link) = NULL;
    APR_RING_NEXT(tail, link) = NULL;
    return head;
}

static apr_size_t trim_idle_threads(apr_thread_pool_t *me, apr_size_t cnt)
{
    apr_size_t n_dbg;
    struct apr_thread_list_elt *elt, *head, *tail;
    apr_status_t rv;

    elt = trim_threads(me, &cnt, 1);

    apr_thread_mutex_lock(me->lock);
    apr_thread_cond_broadcast(me->cond);
    apr_thread_mutex_unlock(me->lock);

    n_dbg = 0;
    if (NULL != (head = elt)) {
        while (elt) {
            tail = elt;
            apr_thread_join(&rv, elt->thd);
            elt = APR_RING_NEXT(elt, link);
            ++n_dbg;
        }
        apr_thread_mutex_lock(me->lock);
        APR_RING_SPLICE_TAIL(me->recycled_thds, head, tail,
                             apr_thread_list_elt, link);
        apr_thread_mutex_unlock(me->lock);
    }
    assert(cnt == n_dbg);

    return cnt;
}

/* don't join on busy threads for performance reasons, who knows how long will
 * the task takes to perform
 */
static apr_size_t trim_busy_threads(apr_thread_pool_t *me, apr_size_t cnt)
{
    trim_threads(me, &cnt, 0);
    return cnt;
}

APU_DECLARE(apr_size_t) apr_thread_pool_idle_max_set(apr_thread_pool_t *me,
                                                     apr_size_t cnt)
{
    me->idle_max = cnt;
    cnt = trim_idle_threads(me, cnt);
    return cnt;
}

APU_DECLARE(apr_interval_time_t)
    apr_thread_pool_idle_wait_set(apr_thread_pool_t * me,
                                  apr_interval_time_t timeout)
{
    apr_interval_time_t oldtime;

    oldtime = me->idle_wait;
    me->idle_wait = timeout;

    return oldtime;
}

APU_DECLARE(apr_size_t) apr_thread_pool_thread_max_get(apr_thread_pool_t *me)
{
    return me->thd_max;
}

/*
 * This function stop extra working threads to the new limit.
 * NOTE: There could be busy threads become idle during this function
 */
APU_DECLARE(apr_size_t) apr_thread_pool_thread_max_set(apr_thread_pool_t *me,
                                                       apr_size_t cnt)
{
    unsigned int n;

    me->thd_max = cnt;
    if (0 == cnt || me->thd_cnt <= cnt) {
        return 0;
    }

    n = me->thd_cnt - cnt;
    if (n >= me->idle_cnt) {
        trim_busy_threads(me, n - me->idle_cnt);
        trim_idle_threads(me, 0);
    }
    else {
        trim_idle_threads(me, me->idle_cnt - n);
    }
    return n;
}

APU_DECLARE(apr_size_t) apr_thread_pool_threshold_get(apr_thread_pool_t *me)
{
    return me->threshold;
}

APU_DECLARE(apr_size_t) apr_thread_pool_threshold_set(apr_thread_pool_t *me,
                                                      apr_size_t val)
{
    apr_size_t ov;

    ov = me->threshold;
    me->threshold = val;
    return ov;
}

APU_DECLARE(apr_status_t) apr_thread_pool_task_owner_get(apr_thread_t *thd,
                                                         void **owner)
{
    apr_status_t rv;
    apr_thread_pool_task_t *task;
    void *data;

    rv = apr_thread_data_get(&data, "apr_thread_pool_task", thd);
    if (rv != APR_SUCCESS) {
        return rv;
    }

    task = data;
    if (!task) {
        *owner = NULL;
        return APR_BADARG;
    }

    *owner = task->owner;
    return APR_SUCCESS;
}

#endif /* APR_HAS_THREADS */

/* vim: set ts=4 sw=4 et cin tw=80: */
