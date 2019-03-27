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

#include "apr.h"
#include "apr_general.h"
#include "apr_pools.h"
#include "apr_errno.h"
#include "apr_dbm.h"
#include "apr_uuid.h"
#include "apr_strings.h"
#include "abts.h"
#include "testutil.h"

#define NUM_TABLE_ROWS  1024

typedef struct {
    apr_datum_t key;
    apr_datum_t val;
    int deleted;
    int visited;
} dbm_table_t;

static dbm_table_t *generate_table(void)
{
    unsigned int i;
    apr_uuid_t uuid;
    dbm_table_t *table = apr_pcalloc(p, sizeof(*table) * NUM_TABLE_ROWS);

    for (i = 0; i < NUM_TABLE_ROWS/2; i++) {
        apr_uuid_get(&uuid);
        table[i].key.dptr = apr_pmemdup(p, uuid.data, sizeof(uuid.data));
        table[i].key.dsize = sizeof(uuid.data);
        table[i].val.dptr = apr_palloc(p, APR_UUID_FORMATTED_LENGTH);
        table[i].val.dsize = APR_UUID_FORMATTED_LENGTH;
        apr_uuid_format(table[i].val.dptr, &uuid);
    }

    for (; i < NUM_TABLE_ROWS; i++) {
        apr_uuid_get(&uuid);
        table[i].val.dptr = apr_pmemdup(p, uuid.data, sizeof(uuid.data));
        table[i].val.dsize = sizeof(uuid.data);
        table[i].key.dptr = apr_palloc(p, APR_UUID_FORMATTED_LENGTH);
        table[i].key.dsize = APR_UUID_FORMATTED_LENGTH;
        apr_uuid_format(table[i].key.dptr, &uuid);
    }

    return table;
}

static void test_dbm_store(abts_case *tc, apr_dbm_t *db, dbm_table_t *table)
{
    apr_status_t rv;
    unsigned int i = NUM_TABLE_ROWS - 1;

    for (; i >= NUM_TABLE_ROWS/2; i--) {
        rv = apr_dbm_store(db, table[i].key, table[i].val);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
        table[i].deleted = FALSE;
    }

    for (i = 0; i < NUM_TABLE_ROWS/2; i++) {
        rv = apr_dbm_store(db, table[i].key, table[i].val);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
        table[i].deleted = FALSE;
    }
}

static void test_dbm_fetch(abts_case *tc, apr_dbm_t *db, dbm_table_t *table)
{
    apr_status_t rv;
    unsigned int i;
    apr_datum_t val;

    for (i = 0; i < NUM_TABLE_ROWS; i++) {
        memset(&val, 0, sizeof(val));
        rv = apr_dbm_fetch(db, table[i].key, &val);
        if (!table[i].deleted) {
            ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
            ABTS_INT_EQUAL(tc, table[i].val.dsize, val.dsize);
            ABTS_INT_EQUAL(tc, 0, memcmp(table[i].val.dptr, val.dptr, val.dsize));
            apr_dbm_freedatum(db, val);
        } else {
            ABTS_INT_EQUAL(tc, 0, val.dsize);
        }
    }
}

static void test_dbm_delete(abts_case *tc, apr_dbm_t *db, dbm_table_t *table)
{
    apr_status_t rv;
    unsigned int i;

    for (i = 0; i < NUM_TABLE_ROWS; i++) {
        /* XXX: random */
        if (i & 1)
            continue;
        rv = apr_dbm_delete(db, table[i].key);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
        table[i].deleted = TRUE;
    }
}

static void test_dbm_exists(abts_case *tc, apr_dbm_t *db, dbm_table_t *table)
{
    unsigned int i;
    int cond;

    for (i = 0; i < NUM_TABLE_ROWS; i++) {
        cond = apr_dbm_exists(db, table[i].key);
        if (table[i].deleted) {
            ABTS_TRUE(tc, cond == 0);
        } else {
            ABTS_TRUE(tc, cond != 0);
        }
    }
}

static void test_dbm_traversal(abts_case *tc, apr_dbm_t *db, dbm_table_t *table)
{
    apr_status_t rv;
    unsigned int i;
    apr_datum_t key;

    rv = apr_dbm_firstkey(db, &key);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    do {
        if (key.dptr == NULL || key.dsize == 0)
            break;

        for (i = 0; i < NUM_TABLE_ROWS; i++) {
            if (table[i].key.dsize != key.dsize)
                continue;
            if (memcmp(table[i].key.dptr, key.dptr, key.dsize))
                continue;
            ABTS_INT_EQUAL(tc, 0, table[i].deleted);
            ABTS_INT_EQUAL(tc, 0, table[i].visited);
            table[i].visited++;
        }

        rv = apr_dbm_nextkey(db, &key);
        ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);
    } while (1);

    for (i = 0; i < NUM_TABLE_ROWS; i++) {
        if (table[i].deleted)
            continue;
        ABTS_INT_EQUAL(tc, 1, table[i].visited);
        table[i].visited = 0;
    }
}

static void test_dbm(abts_case *tc, void *data)
{
    apr_dbm_t *db;
    apr_status_t rv;
    dbm_table_t *table;
    const char *type = data;
    const char *file = apr_pstrcat(p, "data/test-", type, NULL);

    rv = apr_dbm_open_ex(&db, type, file, APR_DBM_RWCREATE, APR_OS_DEFAULT, p);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    if (rv != APR_SUCCESS)
        return;

    table = generate_table();

    test_dbm_store(tc, db, table);
    test_dbm_fetch(tc, db, table);
    test_dbm_delete(tc, db, table);
    test_dbm_exists(tc, db, table);
    test_dbm_traversal(tc, db, table);

    apr_dbm_close(db);

    rv = apr_dbm_open_ex(&db, type, file, APR_DBM_READONLY, APR_OS_DEFAULT, p);
    ABTS_INT_EQUAL(tc, APR_SUCCESS, rv);

    if (rv != APR_SUCCESS)
        return;

    test_dbm_exists(tc, db, table);
    test_dbm_traversal(tc, db, table);
    test_dbm_fetch(tc, db, table);

    apr_dbm_close(db);
}

abts_suite *testdbm(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

#if APU_HAVE_GDBM
    abts_run_test(suite, test_dbm, "gdbm");
#endif
#if APU_HAVE_NDBM
    abts_run_test(suite, test_dbm, "ndbm");
#endif
#if APU_HAVE_SDBM
    abts_run_test(suite, test_dbm, "sdbm");
#endif
#if APU_HAVE_DB
    abts_run_test(suite, test_dbm, "db");
#endif

    return suite;
}
