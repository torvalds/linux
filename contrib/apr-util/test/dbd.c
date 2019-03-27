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

#include "apu.h"
#include "apr_pools.h"
#include "apr_dbd.h"

#include <stdio.h>

#define TEST(msg,func)					\
    printf("======== %s ========\n", msg);		\
    rv = func(pool, sql, driver);			\
    if (rv != 0) {					\
        printf("Error in %s: rc=%d\n\n", msg, rv);	\
    }							\
    else {						\
        printf("%s test successful\n\n", msg);		\
    }							\
    fflush(stdout);

static int create_table(apr_pool_t* pool, apr_dbd_t* handle,
                        const apr_dbd_driver_t* driver)
{
    int rv = 0;
    int nrows;
    const char *statement = "CREATE TABLE apr_dbd_test ("
        "col1 varchar(40) not null,"
        "col2 varchar(40),"
        "col3 integer)" ;
    rv = apr_dbd_query(driver, handle, &nrows, statement);
    return rv;
}
static int drop_table(apr_pool_t* pool, apr_dbd_t* handle,
                      const apr_dbd_driver_t* driver)
{
    int rv = 0;
    int nrows;
    const char *statement = "DROP TABLE apr_dbd_test" ;
    rv = apr_dbd_query(driver, handle, &nrows, statement);
    return rv;
}
static int insert_rows(apr_pool_t* pool, apr_dbd_t* handle,
                       const apr_dbd_driver_t* driver)
{
    int i;
    int rv = 0;
    int nrows;
    int nerrors = 0;
    const char *statement =
        "INSERT into apr_dbd_test (col1) values ('foo');"
        "INSERT into apr_dbd_test values ('wibble', 'other', 5);"
        "INSERT into apr_dbd_test values ('wibble', 'nothing', 5);"
        "INSERT into apr_dbd_test values ('qwerty', 'foo', 0);"
        "INSERT into apr_dbd_test values ('asdfgh', 'bar', 1);"
    ;
    rv = apr_dbd_query(driver, handle, &nrows, statement);
    if (rv) {
        const char* stmt[] = {
            "INSERT into apr_dbd_test (col1) values ('foo');",
            "INSERT into apr_dbd_test values ('wibble', 'other', 5);",
            "INSERT into apr_dbd_test values ('wibble', 'nothing', 5);",
            "INSERT into apr_dbd_test values ('qwerty', 'foo', 0);",
            "INSERT into apr_dbd_test values ('asdfgh', 'bar', 1);",
            NULL
        };
        printf("Compound insert failed; trying statements one-by-one\n") ;
        for (i=0; stmt[i] != NULL; ++i) {
            statement = stmt[i];
            rv = apr_dbd_query(driver, handle, &nrows, statement);
            if (rv) {
                nerrors++;
            }
        }
        if (nerrors) {
            printf("%d single inserts failed too.\n", nerrors) ;
        }
    }
    return rv;
}
static int invalid_op(apr_pool_t* pool, apr_dbd_t* handle,
                      const apr_dbd_driver_t* driver)
{
    int rv = 0;
    int nrows;
    const char *statement = "INSERT into apr_dbd_test1 (col2) values ('foo')" ;
    rv = apr_dbd_query(driver, handle, &nrows, statement);
    printf("invalid op returned %d (should be nonzero).  Error msg follows\n", rv);
    printf("'%s'\n", apr_dbd_error(driver, handle, rv));
    statement = "INSERT into apr_dbd_test (col1, col2) values ('bar', 'foo')" ;
    rv = apr_dbd_query(driver, handle, &nrows, statement);
    printf("valid op returned %d (should be zero; error shouldn't affect subsequent ops)\n", rv);
    return rv;
}
static int select_sequential(apr_pool_t* pool, apr_dbd_t* handle,
                             const apr_dbd_driver_t* driver)
{
    int rv = 0;
    int i = 0;
    int n;
    const char* entry;
    const char* statement = "SELECT * FROM apr_dbd_test ORDER BY col1, col2";
    apr_dbd_results_t *res = NULL;
    apr_dbd_row_t *row = NULL;
    rv = apr_dbd_select(driver,pool,handle,&res,statement,0);
    if (rv) {
        printf("Select failed: %s", apr_dbd_error(driver, handle, rv));
        return rv;
    }
    for (rv = apr_dbd_get_row(driver, pool, res, &row, -1);
         rv == 0;
         rv = apr_dbd_get_row(driver, pool, res, &row, -1)) {
        printf("ROW %d:	", ++i) ;
        for (n = 0; n < apr_dbd_num_cols(driver, res); ++n) {
            entry = apr_dbd_get_entry(driver, row, n);
            if (entry == NULL) {
                printf("(null)	") ;
            }
            else {
                printf("%s	", entry);
            }
        }
	fputs("\n", stdout);
    }
    return (rv == -1) ? 0 : 1;
}
static int select_random(apr_pool_t* pool, apr_dbd_t* handle,
                         const apr_dbd_driver_t* driver)
{
    int rv = 0;
    int n;
    const char* entry;
    const char* statement = "SELECT * FROM apr_dbd_test ORDER BY col1, col2";
    apr_dbd_results_t *res = NULL;
    apr_dbd_row_t *row = NULL;
    rv = apr_dbd_select(driver,pool,handle,&res,statement,1);
    if (rv) {
        printf("Select failed: %s", apr_dbd_error(driver, handle, rv));
        return rv;
    }
    rv = apr_dbd_get_row(driver, pool, res, &row, 5) ;
    if (rv) {
        printf("get_row failed: %s", apr_dbd_error(driver, handle, rv));
        return rv;
    }
    printf("ROW 5:	");
    for (n = 0; n < apr_dbd_num_cols(driver, res); ++n) {
        entry = apr_dbd_get_entry(driver, row, n);
        if (entry == NULL) {
            printf("(null)	") ;
        }
        else {
            printf("%s	", entry);
        }
    }
    fputs("\n", stdout);
    rv = apr_dbd_get_row(driver, pool, res, &row, 1) ;
    if (rv) {
        printf("get_row failed: %s", apr_dbd_error(driver, handle, rv));
        return rv;
    }
    printf("ROW 1:	");
    for (n = 0; n < apr_dbd_num_cols(driver, res); ++n) {
        entry = apr_dbd_get_entry(driver, row, n);
        if (entry == NULL) {
            printf("(null)	") ;
        }
        else {
            printf("%s	", entry);
        }
    }
    fputs("\n", stdout);
    rv = apr_dbd_get_row(driver, pool, res, &row, 11) ;
    if (rv != -1) {
        printf("Oops!  get_row out of range but thinks it succeeded!\n%s\n",
                apr_dbd_error(driver, handle, rv));
        return -1;
    }
    rv = 0;

    return rv;
}
static int test_transactions(apr_pool_t* pool, apr_dbd_t* handle,
                             const apr_dbd_driver_t* driver)
{
    int rv = 0;
    int nrows;
    apr_dbd_transaction_t *trans = NULL;
    const char* statement;

    /* trans 1 - error out early */
    printf("Transaction 1\n");
    rv = apr_dbd_transaction_start(driver, pool, handle, &trans);
    if (rv) {
        printf("Start transaction failed!\n%s\n",
               apr_dbd_error(driver, handle, rv));
        return rv;
    }
    statement = "UPDATE apr_dbd_test SET col2 = 'failed'";
    rv = apr_dbd_query(driver, handle, &nrows, statement);
    if (rv) {
        printf("Update failed: '%s'\n", apr_dbd_error(driver, handle, rv));
        apr_dbd_transaction_end(driver, pool, trans);
        return rv;
    }
    printf("%d rows updated\n", nrows);

    statement = "INSERT INTO apr_dbd_test1 (col3) values (3)";
    rv = apr_dbd_query(driver, handle, &nrows, statement);
    if (!rv) {
        printf("Oops, invalid op succeeded but shouldn't!\n");
    }
    statement = "INSERT INTO apr_dbd_test values ('zzz', 'aaa', 3)";
    rv = apr_dbd_query(driver, handle, &nrows, statement);
    printf("Valid insert returned %d.  Should be nonzero (fail) because transaction is bad\n", rv) ;

    rv = apr_dbd_transaction_end(driver, pool, trans);
    if (rv) {
        printf("End transaction failed!\n%s\n",
               apr_dbd_error(driver, handle, rv));
        return rv;
    }
    printf("Transaction ended (should be rollback) - viewing table\n"
           "A column of \"failed\" indicates transaction failed (no rollback)\n");
    select_sequential(pool, handle, driver);

    /* trans 2 - complete successfully */
    printf("Transaction 2\n");
    rv = apr_dbd_transaction_start(driver, pool, handle, &trans);
    if (rv) {
        printf("Start transaction failed!\n%s\n",
               apr_dbd_error(driver, handle, rv));
        return rv;
    }
    statement = "UPDATE apr_dbd_test SET col2 = 'success'";
    rv = apr_dbd_query(driver, handle, &nrows, statement);
    if (rv) {
        printf("Update failed: '%s'\n", apr_dbd_error(driver, handle, rv));
        apr_dbd_transaction_end(driver, pool, trans);
        return rv;
    }
    printf("%d rows updated\n", nrows);
    statement = "INSERT INTO apr_dbd_test values ('aaa', 'zzz', 3)";
    rv = apr_dbd_query(driver, handle, &nrows, statement);
    printf("Valid insert returned %d.  Should be zero (OK)\n", rv) ;
    rv = apr_dbd_transaction_end(driver, pool, trans);
    if (rv) {
        printf("End transaction failed!\n%s\n",
               apr_dbd_error(driver, handle, rv));
        return rv;
    }
    printf("Transaction ended (should be commit) - viewing table\n");
    select_sequential(pool, handle, driver);
    return rv;
}
static int test_pselect(apr_pool_t* pool, apr_dbd_t* handle,
                        const apr_dbd_driver_t* driver)
{
    int rv = 0;
    int i, n;
    const char *query =
        "SELECT * FROM apr_dbd_test WHERE col3 <= %s or col1 = 'bar'" ;
    const char *label = "lowvalues";
    apr_dbd_prepared_t *statement = NULL;
    apr_dbd_results_t *res = NULL;
    apr_dbd_row_t *row = NULL;
    const char *entry = NULL;

    rv = apr_dbd_prepare(driver, pool, handle, query, label, &statement);
    if (rv) {
        printf("Prepare statement failed!\n%s\n",
               apr_dbd_error(driver, handle, rv));
        return rv;
    }
    rv = apr_dbd_pvselect(driver, pool, handle, &res, statement, 0, "3", NULL);
    if (rv) {
        printf("Exec of prepared statement failed!\n%s\n",
               apr_dbd_error(driver, handle, rv));
        return rv;
    }
    i = 0;
    printf("Selecting rows where col3 <= 3 and bar row where it's unset.\nShould show four rows.\n");
    for (rv = apr_dbd_get_row(driver, pool, res, &row, -1);
         rv == 0;
         rv = apr_dbd_get_row(driver, pool, res, &row, -1)) {
        printf("ROW %d:	", ++i) ;
        for (n = 0; n < apr_dbd_num_cols(driver, res); ++n) {
            entry = apr_dbd_get_entry(driver, row, n);
            if (entry == NULL) {
                printf("(null)	") ;
            }
            else {
                printf("%s	", entry);
            }
        }
	fputs("\n", stdout);
    }
    return (rv == -1) ? 0 : 1;
}
static int test_pquery(apr_pool_t* pool, apr_dbd_t* handle,
                       const apr_dbd_driver_t* driver)
{
    int rv = 0;
    const char *query = "INSERT INTO apr_dbd_test VALUES (%s, %s, %d)";
    apr_dbd_prepared_t *statement = NULL;
    const char *label = "testpquery";
    int nrows;
    apr_dbd_transaction_t *trans =0;

    rv = apr_dbd_prepare(driver, pool, handle, query, label, &statement);
    /* rv = apr_dbd_prepare(driver, pool, handle, query, NULL, &statement); */
    if (rv) {
        printf("Prepare statement failed!\n%s\n",
               apr_dbd_error(driver, handle, rv));
        return rv;
    }
    apr_dbd_transaction_start(driver, pool, handle, &trans);
    rv = apr_dbd_pvquery(driver, pool, handle, &nrows, statement,
                         "prepared", "insert", "2", NULL);
    apr_dbd_transaction_end(driver, pool, trans);
    if (rv) {
        printf("Exec of prepared statement failed!\n%s\n",
               apr_dbd_error(driver, handle, rv));
        return rv;
    }
    printf("Showing table (should now contain row \"prepared insert 2\")\n");
    select_sequential(pool, handle, driver);
    return rv;
}
int main(int argc, char** argv)
{
    const char *name;
    const char *params;
    apr_pool_t *pool = NULL;
    apr_dbd_t *sql = NULL;
    const apr_dbd_driver_t *driver = NULL;
    int rv;

    apr_initialize();
    apr_pool_create(&pool, NULL);

    if (argc >= 2 && argc <= 3) {
        name = argv[1];
        params = ( argc == 3 ) ? argv[2] : "";
        apr_dbd_init(pool);
        setbuf(stdout,NULL);
        rv = apr_dbd_get_driver(pool, name, &driver);
        switch (rv) {
        case APR_SUCCESS:
           printf("Loaded %s driver OK.\n", name);
           break;
        case APR_EDSOOPEN:
           printf("Failed to load driver file apr_dbd_%s.so\n", name);
           goto finish;
        case APR_ESYMNOTFOUND:
           printf("Failed to load driver apr_dbd_%s_driver.\n", name);
           goto finish;
        case APR_ENOTIMPL:
           printf("No driver available for %s.\n", name);
           goto finish;
        default:        /* it's a bug if none of the above happen */
           printf("Internal error loading %s.\n", name);
           goto finish;
        }
        rv = apr_dbd_open(driver, pool, params, &sql);
        switch (rv) {
        case APR_SUCCESS:
           printf("Opened %s[%s] OK\n", name, params);
           break;
        case APR_EGENERAL:
           printf("Failed to open %s[%s]\n", name, params);
           goto finish;
        default:        /* it's a bug if none of the above happen */
           printf("Internal error opening %s[%s]\n", name, params);
           goto finish;
        }
        TEST("create table", create_table);
        TEST("insert rows", insert_rows);
        TEST("invalid op", invalid_op);
        TEST("select random", select_random);
        TEST("select sequential", select_sequential);
        TEST("transactions", test_transactions);
        TEST("prepared select", test_pselect);
        TEST("prepared query", test_pquery);
        TEST("drop table", drop_table);
        apr_dbd_close(driver, sql);
    }
    else {
        fprintf(stderr, "Usage: %s driver-name [params]\n", argv[0]);
    }
finish:
    apr_pool_destroy(pool);
    apr_terminate();
    return 0;
}
