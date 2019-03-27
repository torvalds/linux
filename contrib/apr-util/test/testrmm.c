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

#include "apr_shm.h"
#include "apr_rmm.h"
#include "apr_errno.h"
#include "apr_general.h"
#include "apr_lib.h"
#include "apr_strings.h"
#include "apr_time.h"
#include "abts.h"
#include "testutil.h"

#if APR_HAS_SHARED_MEMORY

#define FRAG_SIZE 80
#define FRAG_COUNT 10
#define SHARED_SIZE (apr_size_t)(FRAG_SIZE * FRAG_COUNT * sizeof(char*))

static void test_rmm(abts_case *tc, void *data)
{
    apr_status_t rv;
    apr_pool_t *pool;
    apr_shm_t *shm;
    apr_rmm_t *rmm;
    apr_size_t size, fragsize;
    apr_rmm_off_t *off, off2;
    int i;
    void *entity;

    rv = apr_pool_create(&pool, p);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    /* We're going to want 10 blocks of data from our target rmm. */
    size = SHARED_SIZE + apr_rmm_overhead_get(FRAG_COUNT + 1);
    rv = apr_shm_create(&shm, size, NULL, pool);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    if (rv != APR_SUCCESS)
        return;

    rv = apr_rmm_init(&rmm, NULL, apr_shm_baseaddr_get(shm), size, pool);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    if (rv != APR_SUCCESS)
        return;

    /* Creating each fragment of size fragsize */
    fragsize = SHARED_SIZE / FRAG_COUNT;
    off = apr_palloc(pool, FRAG_COUNT * sizeof(apr_rmm_off_t));
    for (i = 0; i < FRAG_COUNT; i++) {
        off[i] = apr_rmm_malloc(rmm, fragsize);
    }

    /* Checking for out of memory allocation */
    off2 = apr_rmm_malloc(rmm, FRAG_SIZE * FRAG_COUNT);
    ABTS_TRUE(tc, !off2);

    /* Checking each fragment for address alignment */
    for (i = 0; i < FRAG_COUNT; i++) {
        char *c = apr_rmm_addr_get(rmm, off[i]);
        apr_size_t sc = (apr_size_t)c;

        ABTS_TRUE(tc, !!off[i]);
        ABTS_TRUE(tc, !(sc & 7));
    }

    /* Setting each fragment to a unique value */
    for (i = 0; i < FRAG_COUNT; i++) {
        int j;
        char **c = apr_rmm_addr_get(rmm, off[i]);
        for (j = 0; j < FRAG_SIZE; j++, c++) {
            *c = apr_itoa(pool, i + j);
        }
    }

    /* Checking each fragment for its unique value */
    for (i = 0; i < FRAG_COUNT; i++) {
        int j;
        char **c = apr_rmm_addr_get(rmm, off[i]);
        for (j = 0; j < FRAG_SIZE; j++, c++) {
            char *d = apr_itoa(pool, i + j);
            ABTS_STR_EQUAL(tc, d, *c);
        }
    }

    /* Freeing each fragment */
    for (i = 0; i < FRAG_COUNT; i++) {
        rv = apr_rmm_free(rmm, off[i]);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }

    /* Creating one large segment */
    off[0] = apr_rmm_calloc(rmm, SHARED_SIZE);

    /* Setting large segment */
    for (i = 0; i < FRAG_COUNT * FRAG_SIZE; i++) {
        char **c = apr_rmm_addr_get(rmm, off[0]);
        c[i] = apr_itoa(pool, i);
    }

    /* Freeing large segment */
    rv = apr_rmm_free(rmm, off[0]);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    /* Creating each fragment of size fragsize */
    for (i = 0; i < FRAG_COUNT; i++) {
        off[i] = apr_rmm_malloc(rmm, fragsize);
    }

    /* Freeing each fragment backwards */
    for (i = FRAG_COUNT - 1; i >= 0; i--) {
        rv = apr_rmm_free(rmm, off[i]);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    }

    /* Creating one large segment (again) */
    off[0] = apr_rmm_calloc(rmm, SHARED_SIZE);

    /* Freeing large segment */
    rv = apr_rmm_free(rmm, off[0]);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    /* Checking realloc */
    off[0] = apr_rmm_calloc(rmm, SHARED_SIZE - 100);
    off[1] = apr_rmm_calloc(rmm, 100);
    ABTS_TRUE(tc, !!off[0]);
    ABTS_TRUE(tc, !!off[1]);

    entity = apr_rmm_addr_get(rmm, off[1]);
    rv = apr_rmm_free(rmm, off[0]);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    {
        unsigned char *c = entity;

        /* Fill in the region; the first half with zereos, which will
         * likely catch the apr_rmm_realloc offset calculation bug by
         * making it think the old region was zero length. */
        for (i = 0; i < 100; i++) {
            c[i] = (i < 50) ? 0 : i;
        }
    }

    /* now we can realloc off[1] and get many more bytes */
    off[0] = apr_rmm_realloc(rmm, entity, SHARED_SIZE - 100);
    ABTS_TRUE(tc, !!off[0]);

    {
        unsigned char *c = apr_rmm_addr_get(rmm, off[0]);

        /* fill in the region */
        for (i = 0; i < 100; i++) {
            ABTS_TRUE(tc, c[i] == (i < 50 ? 0 : i));
        }
    }

    rv = apr_rmm_destroy(rmm);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    rv = apr_shm_destroy(shm);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    apr_pool_destroy(pool);
}

#endif /* APR_HAS_SHARED_MEMORY */

abts_suite *testrmm(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

#if APR_HAS_SHARED_MEMORY
    abts_run_test(suite, test_rmm, NULL);
#endif

    return suite;
}
