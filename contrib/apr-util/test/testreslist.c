/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>

#include "apr_general.h"
#include "apu.h"
#include "apr_reslist.h"
#include "apr_thread_pool.h"

#if APR_HAVE_TIME_H
#include <time.h>
#endif /* APR_HAVE_TIME_H */

#include "abts.h"
#include "testutil.h"

#if APR_HAS_THREADS

#define RESLIST_MIN   3
#define RESLIST_SMAX 10
#define RESLIST_HMAX 20
#define RESLIST_TTL  APR_TIME_C(35000) /* 35 ms */
#define CONSUMER_THREADS 25
#define CONSUMER_ITERATIONS 250
#define CONSTRUCT_SLEEP_TIME  APR_TIME_C(25000) /* 25 ms */
#define DESTRUCT_SLEEP_TIME   APR_TIME_C(10000) /* 10 ms */
#define WORK_DELAY_SLEEP_TIME APR_TIME_C(15000) /* 15 ms */

typedef struct {
    apr_interval_time_t sleep_upon_construct;
    apr_interval_time_t sleep_upon_destruct;
    int c_count;
    int d_count;
} my_parameters_t;

typedef struct {
    int id;
} my_resource_t;

/* Linear congruential generator */
static apr_uint32_t lgc(apr_uint32_t a)
{
    apr_uint64_t z = a;
    z *= 279470273;
    z %= APR_UINT64_C(4294967291);
    return (apr_uint32_t)z;
}

static apr_status_t my_constructor(void **resource, void *params,
                                   apr_pool_t *pool)
{
    my_resource_t *res;
    my_parameters_t *my_params = params;

    /* Create some resource */
    res = apr_palloc(pool, sizeof(*res));
    res->id = my_params->c_count++;

    /* Sleep for awhile, to simulate construction overhead. */
    apr_sleep(my_params->sleep_upon_construct);

    /* Set the resource so it can be managed by the reslist */
    *resource = res;
    return APR_SUCCESS;
}

static apr_status_t my_destructor(void *resource, void *params,
                                  apr_pool_t *pool)
{
    my_resource_t *res = resource;
    my_parameters_t *my_params = params;
    res->id = my_params->d_count++;

    apr_sleep(my_params->sleep_upon_destruct);

    return APR_SUCCESS;
}

typedef struct {
    int tid;
    abts_case *tc;
    apr_reslist_t *reslist;
    apr_interval_time_t work_delay_sleep;
} my_thread_info_t;

/* MAX_UINT * .95 = 2**32 * .95 = 4080218931u */
#define PERCENT95th 4080218931u

static void * APR_THREAD_FUNC resource_consuming_thread(apr_thread_t *thd,
                                                        void *data)
{
    int i;
    apr_uint32_t chance;
    void *vp;
    apr_status_t rv;
    my_resource_t *res;
    my_thread_info_t *thread_info = data;
    apr_reslist_t *rl = thread_info->reslist;

#if APR_HAS_RANDOM
    apr_generate_random_bytes((void*)&chance, sizeof(chance));
#else
    chance = (apr_uint32_t)(apr_time_now() % APR_TIME_C(4294967291));
#endif

    for (i = 0; i < CONSUMER_ITERATIONS; i++) {
        rv = apr_reslist_acquire(rl, &vp);
        ABTS_INT_EQUAL(thread_info->tc, APR_SUCCESS, rv);
        res = vp;
        apr_sleep(thread_info->work_delay_sleep);

        /* simulate a 5% chance of the resource being bad */
        chance = lgc(chance);
        if ( chance < PERCENT95th ) {
            rv = apr_reslist_release(rl, res);
            ABTS_INT_EQUAL(thread_info->tc, APR_SUCCESS, rv);
        } else {
            rv = apr_reslist_invalidate(rl, res);
            ABTS_INT_EQUAL(thread_info->tc, APR_SUCCESS, rv);
        }
    }

    return APR_SUCCESS;
}

static void test_timeout(abts_case *tc, apr_reslist_t *rl)
{
    apr_status_t rv;
    my_resource_t *resources[RESLIST_HMAX];
    void *vp;
    int i;

    apr_reslist_timeout_set(rl, 1000);

    /* deplete all possible resources from the resource list
     * so that the next call will block until timeout is reached
     * (since there are no other threads to make a resource
     * available)
     */

    for (i = 0; i < RESLIST_HMAX; i++) {
        rv = apr_reslist_acquire(rl, (void**)&resources[i]);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }

    /* next call will block until timeout is reached */
    rv = apr_reslist_acquire(rl, &vp);
    ABTS_TRUE(tc, APR_STATUS_IS_TIMEUP(rv));

    /* release the resources; otherwise the destroy operation
     * will blow
     */
    for (i = 0; i < RESLIST_HMAX; i++) {
        rv = apr_reslist_release(rl, resources[i]);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }
}

static void test_shrinking(abts_case *tc, apr_reslist_t *rl)
{
    apr_status_t rv;
    my_resource_t *resources[RESLIST_HMAX];
    my_resource_t *res;
    void *vp;
    int i;
    int sleep_time = RESLIST_TTL / RESLIST_HMAX;

    /* deplete all possible resources from the resource list */
    for (i = 0; i < RESLIST_HMAX; i++) {
        rv = apr_reslist_acquire(rl, (void**)&resources[i]);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }

    /* Free all resources above RESLIST_SMAX - 1 */
    for (i = RESLIST_SMAX - 1; i < RESLIST_HMAX; i++) {
        rv = apr_reslist_release(rl, resources[i]);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }

    for (i = 0; i < RESLIST_HMAX; i++) {
        rv = apr_reslist_acquire(rl, &vp);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
        res = vp;
        apr_sleep(sleep_time);
        rv = apr_reslist_release(rl, res);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }
    apr_sleep(sleep_time);

    /*
     * Now free the remaining elements. This should trigger the shrinking of
     * the list
     */
    for (i = 0; i < RESLIST_SMAX - 1; i++) {
        rv = apr_reslist_release(rl, resources[i]);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }
}

static void test_reslist(abts_case *tc, void *data)
{
    int i;
    apr_status_t rv;
    apr_reslist_t *rl;
    my_parameters_t *params;
    apr_thread_pool_t *thrp;
    my_thread_info_t thread_info[CONSUMER_THREADS];

    rv = apr_thread_pool_create(&thrp, CONSUMER_THREADS/2, CONSUMER_THREADS, p);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    /* Create some parameters that will be passed into each
     * constructor and destructor call. */
    params = apr_pcalloc(p, sizeof(*params));
    params->sleep_upon_construct = CONSTRUCT_SLEEP_TIME;
    params->sleep_upon_destruct = DESTRUCT_SLEEP_TIME;

    /* We're going to want 10 blocks of data from our target rmm. */
    rv = apr_reslist_create(&rl, RESLIST_MIN, RESLIST_SMAX, RESLIST_HMAX,
                            RESLIST_TTL, my_constructor, my_destructor,
                            params, p);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    for (i = 0; i < CONSUMER_THREADS; i++) {
        thread_info[i].tid = i;
        thread_info[i].tc = tc;
        thread_info[i].reslist = rl;
        thread_info[i].work_delay_sleep = WORK_DELAY_SLEEP_TIME;
        rv = apr_thread_pool_push(thrp, resource_consuming_thread,
                                  &thread_info[i], 0, NULL);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }

    rv = apr_thread_pool_destroy(thrp);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    test_timeout(tc, rl);

    test_shrinking(tc, rl);
    ABTS_INT_EQUAL(tc, RESLIST_SMAX, params->c_count - params->d_count);

    rv = apr_reslist_destroy(rl);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
}

#endif /* APR_HAS_THREADS */

abts_suite *testreslist(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

#if APR_HAS_THREADS
    abts_run_test(suite, test_reslist, NULL);
#endif

    return suite;
}
