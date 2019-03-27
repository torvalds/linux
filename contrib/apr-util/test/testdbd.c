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

#include "testutil.h"
#include "apr.h"
#include "apu.h"
#include "apr_pools.h"
#include "apr_dbd.h"
#include "apr_strings.h"

static void test_dbd_init(abts_case *tc, void *data)
{
    apr_pool_t *pool = p;
    apr_status_t rv;

    rv = apr_dbd_init(pool);
    ABTS_ASSERT(tc, "failed to init apr_dbd", rv == APR_SUCCESS);
}

#if APU_HAVE_SQLITE2 || APU_HAVE_SQLITE3
static void test_statement(abts_case *tc, apr_dbd_t* handle,
                           const apr_dbd_driver_t* driver, const char* sql)
{
    int nrows;
    apr_status_t rv;

    rv = apr_dbd_query(driver, handle, &nrows, sql);

    ABTS_ASSERT(tc, sql, rv == APR_SUCCESS);
}

static void create_table(abts_case *tc, apr_dbd_t* handle,
                         const apr_dbd_driver_t* driver)
{
    const char *sql = "CREATE TABLE apr_dbd_test ("
                             "col1 varchar(40) not null,"
                             "col2 varchar(40),"
                             "col3 integer)";

    test_statement(tc, handle, driver, sql);
}

static void drop_table(abts_case *tc, apr_dbd_t* handle,
                       const apr_dbd_driver_t* driver)
{
    const char *sql = "DROP TABLE apr_dbd_test";
    test_statement(tc, handle, driver, sql);
}

static void delete_rows(abts_case *tc, apr_dbd_t* handle,
                        const apr_dbd_driver_t* driver)
{
    const char *sql = "DELETE FROM apr_dbd_test";
    test_statement(tc, handle, driver, sql);
}


static void insert_data(abts_case *tc, apr_dbd_t* handle,
                        const apr_dbd_driver_t* driver, int count)
{
    apr_pool_t* pool = p;
    const char* sql = "INSERT INTO apr_dbd_test VALUES('%d', '%d', %d)";
    char* sqf = NULL;
    int i;
    int nrows;
    apr_status_t rv;

    for (i=0; i<count; i++) {
        sqf = apr_psprintf(pool, sql, i, i, i);
        rv = apr_dbd_query(driver, handle, &nrows, sqf);
        ABTS_ASSERT(tc, sqf, rv == APR_SUCCESS);
        ABTS_ASSERT(tc, sqf, 1 == nrows);
    }
}

static void select_rows(abts_case *tc, apr_dbd_t* handle,
                        const apr_dbd_driver_t* driver, int count)
{
    apr_status_t rv;
    apr_pool_t* pool = p;
    apr_pool_t* tpool;
    const char* sql = "SELECT * FROM apr_dbd_test ORDER BY col1";
    apr_dbd_results_t *res = NULL;
    apr_dbd_row_t *row = NULL;
    int i;

    rv = apr_dbd_select(driver, pool, handle, &res, sql, 0);
    ABTS_ASSERT(tc, sql, rv == APR_SUCCESS);
    ABTS_PTR_NOTNULL(tc, res);

    apr_pool_create(&tpool, pool);
    i = count;
    while (i > 0) {
        row = NULL;
        rv = apr_dbd_get_row(driver, pool, res, &row, -1);
        ABTS_ASSERT(tc, sql, rv == APR_SUCCESS);
        ABTS_PTR_NOTNULL(tc, row);
        apr_pool_clear(tpool);
        i--;
    }
    ABTS_ASSERT(tc, "Missing Rows!", i == 0);

    res = NULL;
    i = count;

    rv = apr_dbd_select(driver, pool, handle, &res, sql, 1);
    ABTS_ASSERT(tc, sql, rv == APR_SUCCESS);
    ABTS_PTR_NOTNULL(tc, res);

    rv = apr_dbd_num_tuples(driver, res);
    ABTS_ASSERT(tc, "invalid row count", rv == count);

    while (i > 0) {
        row = NULL;
        rv = apr_dbd_get_row(driver, pool, res, &row, i);
        ABTS_ASSERT(tc, sql, rv == APR_SUCCESS);
        ABTS_PTR_NOTNULL(tc, row);
        apr_pool_clear(tpool);
        i--;
    }
    ABTS_ASSERT(tc, "Missing Rows!", i == 0);
    rv = apr_dbd_get_row(driver, pool, res, &row, count+100);
    ABTS_ASSERT(tc, "If we overseek, get_row should return -1", rv == -1);
}

static void test_escape(abts_case *tc, apr_dbd_t *handle,
                        const apr_dbd_driver_t *driver)
{
  const char *escaped = apr_dbd_escape(driver, p, "foo'bar", handle);

  ABTS_STR_EQUAL(tc, "foo''bar", escaped);
}

static void test_dbd_generic(abts_case *tc, apr_dbd_t* handle,
                             const apr_dbd_driver_t* driver)
{
    void* native;
    apr_pool_t *pool = p;
    apr_status_t rv;

    native = apr_dbd_native_handle(driver, handle);
    ABTS_PTR_NOTNULL(tc, native);

    rv = apr_dbd_check_conn(driver, pool, handle);

    create_table(tc, handle, driver);
    select_rows(tc, handle, driver, 0);
    insert_data(tc, handle, driver, 5);
    select_rows(tc, handle, driver, 5);
    delete_rows(tc, handle, driver);
    select_rows(tc, handle, driver, 0);
    drop_table(tc, handle, driver);

    test_escape(tc, handle, driver);

    rv = apr_dbd_close(driver, handle);
    ABTS_ASSERT(tc, "failed to close database", rv == APR_SUCCESS);
}
#endif

#if APU_HAVE_SQLITE2
static void test_dbd_sqlite2(abts_case *tc, void *data)
{
    apr_pool_t *pool = p;
    apr_status_t rv;
    const apr_dbd_driver_t* driver = NULL;
    apr_dbd_t* handle = NULL;

    rv = apr_dbd_get_driver(pool, "sqlite2", &driver);
    ABTS_ASSERT(tc, "failed to fetch sqlite2 driver", rv == APR_SUCCESS);
    ABTS_PTR_NOTNULL(tc, driver);
    if (!driver) {
    	return;
    }

    ABTS_STR_EQUAL(tc, "sqlite2", apr_dbd_name(driver));

    rv = apr_dbd_open(driver, pool, "data/sqlite2.db:600", &handle);
    ABTS_ASSERT(tc, "failed to open sqlite2 atabase", rv == APR_SUCCESS);
    ABTS_PTR_NOTNULL(tc, handle);
    if (!handle) {
    	return;
    }

    test_dbd_generic(tc, handle, driver);
}
#endif

#if APU_HAVE_SQLITE3
static void test_dbd_sqlite3(abts_case *tc, void *data)
{
    apr_pool_t *pool = p;
    apr_status_t rv;
    const apr_dbd_driver_t* driver = NULL;
    apr_dbd_t* handle = NULL;

    rv = apr_dbd_get_driver(pool, "sqlite3", &driver);
    ABTS_ASSERT(tc, "failed to fetch sqlite3 driver", rv == APR_SUCCESS);
    ABTS_PTR_NOTNULL(tc, driver);
    if (!driver) {
    	return;
    }

    ABTS_STR_EQUAL(tc, "sqlite3", apr_dbd_name(driver));

    rv = apr_dbd_open(driver, pool, "data/sqlite3.db", &handle);
    ABTS_ASSERT(tc, "failed to open sqlite3 database", rv == APR_SUCCESS);
    ABTS_PTR_NOTNULL(tc, handle);
    if (!handle) {
    	return;
    }

    test_dbd_generic(tc, handle, driver);
}
#endif

abts_suite *testdbd(abts_suite *suite)
{
    suite = ADD_SUITE(suite);


    abts_run_test(suite, test_dbd_init, NULL);

#if APU_HAVE_SQLITE2
    abts_run_test(suite, test_dbd_sqlite2, NULL);
#endif

#if APU_HAVE_SQLITE3
    abts_run_test(suite, test_dbd_sqlite3, NULL);
#endif
    return suite;
}
