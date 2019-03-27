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

#include "abts.h"
#include "testutil.h"
#include "apr_buckets.h"
#include "apr_strings.h"

static void test_create(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba;
    apr_bucket_brigade *bb;

    ba = apr_bucket_alloc_create(p);
    bb = apr_brigade_create(p, ba);

    ABTS_ASSERT(tc, "new brigade not NULL", bb != NULL);
    ABTS_ASSERT(tc, "new brigade is empty", APR_BRIGADE_EMPTY(bb));

    apr_brigade_destroy(bb);
    apr_bucket_alloc_destroy(ba);
}

static void test_simple(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba;
    apr_bucket_brigade *bb;
    apr_bucket *fb, *tb;
    
    ba = apr_bucket_alloc_create(p);
    bb = apr_brigade_create(p, ba);
    
    fb = APR_BRIGADE_FIRST(bb);
    ABTS_ASSERT(tc, "first bucket of empty brigade is sentinel",
                fb == APR_BRIGADE_SENTINEL(bb));

    fb = apr_bucket_flush_create(ba);
    APR_BRIGADE_INSERT_HEAD(bb, fb);

    ABTS_ASSERT(tc, "first bucket of brigade is flush",
                APR_BRIGADE_FIRST(bb) == fb);

    ABTS_ASSERT(tc, "bucket after flush is sentinel",
                APR_BUCKET_NEXT(fb) == APR_BRIGADE_SENTINEL(bb));

    tb = apr_bucket_transient_create("aaa", 3, ba);
    APR_BUCKET_INSERT_BEFORE(fb, tb);

    ABTS_ASSERT(tc, "bucket before flush now transient",
                APR_BUCKET_PREV(fb) == tb);
    ABTS_ASSERT(tc, "bucket after transient is flush",
                APR_BUCKET_NEXT(tb) == fb);
    ABTS_ASSERT(tc, "bucket before transient is sentinel",
                APR_BUCKET_PREV(tb) == APR_BRIGADE_SENTINEL(bb));

    apr_brigade_cleanup(bb);

    ABTS_ASSERT(tc, "cleaned up brigade was empty", APR_BRIGADE_EMPTY(bb));

    apr_brigade_destroy(bb);
    apr_bucket_alloc_destroy(ba);
}

static apr_bucket_brigade *make_simple_brigade(apr_bucket_alloc_t *ba,
                                               const char *first, 
                                               const char *second)
{
    apr_bucket_brigade *bb = apr_brigade_create(p, ba);
    apr_bucket *e;
 
    e = apr_bucket_transient_create(first, strlen(first), ba);
    APR_BRIGADE_INSERT_TAIL(bb, e);

    e = apr_bucket_transient_create(second, strlen(second), ba);
    APR_BRIGADE_INSERT_TAIL(bb, e);

    return bb;
}

/* tests that 'bb' flattens to string 'expect'. */
static void flatten_match(abts_case *tc, const char *ctx,
                          apr_bucket_brigade *bb,
                          const char *expect)
{
    apr_size_t elen = strlen(expect);
    char *buf = malloc(elen);
    apr_size_t len = elen;
    char msg[200];

    sprintf(msg, "%s: flatten brigade", ctx);
    apr_assert_success(tc, msg, apr_brigade_flatten(bb, buf, &len));
    sprintf(msg, "%s: length match (%ld not %ld)", ctx,
            (long)len, (long)elen);
    ABTS_ASSERT(tc, msg, len == elen);
    sprintf(msg, "%s: result match", msg);
    ABTS_STR_NEQUAL(tc, expect, buf, len);
    free(buf);
}

static void test_flatten(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bb;

    bb = make_simple_brigade(ba, "hello, ", "world");

    flatten_match(tc, "flatten brigade", bb, "hello, world");

    apr_brigade_destroy(bb);
    apr_bucket_alloc_destroy(ba);    
}

static int count_buckets(apr_bucket_brigade *bb)
{
    apr_bucket *e;
    int count = 0;

    for (e = APR_BRIGADE_FIRST(bb); 
         e != APR_BRIGADE_SENTINEL(bb);
         e = APR_BUCKET_NEXT(e)) {
        count++;
    }
    
    return count;
}

static void test_split(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bb, *bb2;
    apr_bucket *e;

    bb = make_simple_brigade(ba, "hello, ", "world");

    /* split at the "world" bucket */
    e = APR_BRIGADE_LAST(bb);
    bb2 = apr_brigade_split(bb, e);

    ABTS_ASSERT(tc, "split brigade contains one bucket",
                count_buckets(bb2) == 1);
    ABTS_ASSERT(tc, "original brigade contains one bucket",
                count_buckets(bb) == 1);

    flatten_match(tc, "match original brigade", bb, "hello, ");
    flatten_match(tc, "match split brigade", bb2, "world");

    apr_brigade_destroy(bb2);
    apr_brigade_destroy(bb);
    apr_bucket_alloc_destroy(ba);
}

#define COUNT 3000
#define THESTR "hello"

static void test_bwrite(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bb = apr_brigade_create(p, ba);
    apr_off_t length;
    int n;

    for (n = 0; n < COUNT; n++) {
        apr_assert_success(tc, "brigade_write", 
                           apr_brigade_write(bb, NULL, NULL,
                                             THESTR, sizeof THESTR));
    }

    apr_assert_success(tc, "determine brigade length",
                       apr_brigade_length(bb, 1, &length));

    ABTS_ASSERT(tc, "brigade has correct length",
                length == (COUNT * sizeof THESTR));
    
    apr_brigade_destroy(bb);
    apr_bucket_alloc_destroy(ba);
}

static void test_splitline(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bin, *bout;

    bin = make_simple_brigade(ba, "blah blah blah-",
                              "end of line.\nfoo foo foo");
    bout = apr_brigade_create(p, ba);

    apr_assert_success(tc, "split line",
                       apr_brigade_split_line(bout, bin,
                                              APR_BLOCK_READ, 100));

    flatten_match(tc, "split line", bout, "blah blah blah-end of line.\n");
    flatten_match(tc, "remainder", bin, "foo foo foo");

    apr_brigade_destroy(bout);
    apr_brigade_destroy(bin);
    apr_bucket_alloc_destroy(ba);
}

/* Test that bucket E has content EDATA of length ELEN. */
static void test_bucket_content(abts_case *tc,
                                apr_bucket *e,
                                const char *edata,
                                apr_size_t elen)
{
    const char *adata;
    apr_size_t alen;

    apr_assert_success(tc, "read from bucket",
                       apr_bucket_read(e, &adata, &alen, 
                                       APR_BLOCK_READ));

    ABTS_ASSERT(tc, "read expected length", alen == elen);
    ABTS_STR_NEQUAL(tc, edata, adata, elen);
}

static void test_splits(abts_case *tc, void *ctx)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bb;
    apr_bucket *e;
    char *str = "alphabeta";
    int n;

    bb = apr_brigade_create(p, ba);

    APR_BRIGADE_INSERT_TAIL(bb,
                            apr_bucket_immortal_create(str, 9, ba));
    APR_BRIGADE_INSERT_TAIL(bb, 
                            apr_bucket_transient_create(str, 9, ba));
    APR_BRIGADE_INSERT_TAIL(bb, 
                            apr_bucket_heap_create(strdup(str), 9, free, ba));
    APR_BRIGADE_INSERT_TAIL(bb, 
                            apr_bucket_pool_create(apr_pstrdup(p, str), 9, p, 
                                                   ba));

    ABTS_ASSERT(tc, "four buckets inserted", count_buckets(bb) == 4);
    
    /* now split each of the buckets after byte 5 */
    for (n = 0, e = APR_BRIGADE_FIRST(bb); n < 4; n++) {
        ABTS_ASSERT(tc, "reached end of brigade", 
                    e != APR_BRIGADE_SENTINEL(bb));
        ABTS_ASSERT(tc, "split bucket OK",
                    apr_bucket_split(e, 5) == APR_SUCCESS);
        e = APR_BUCKET_NEXT(e);
        ABTS_ASSERT(tc, "split OK", e != APR_BRIGADE_SENTINEL(bb));
        e = APR_BUCKET_NEXT(e);
    }
    
    ABTS_ASSERT(tc, "four buckets split into eight", 
                count_buckets(bb) == 8);

    for (n = 0, e = APR_BRIGADE_FIRST(bb); n < 4; n++) {
        const char *data;
        apr_size_t len;
        
        apr_assert_success(tc, "read alpha from bucket",
                           apr_bucket_read(e, &data, &len, APR_BLOCK_READ));
        ABTS_ASSERT(tc, "read 5 bytes", len == 5);
        ABTS_STR_NEQUAL(tc, "alpha", data, 5);

        e = APR_BUCKET_NEXT(e);

        apr_assert_success(tc, "read beta from bucket",
                           apr_bucket_read(e, &data, &len, APR_BLOCK_READ));
        ABTS_ASSERT(tc, "read 4 bytes", len == 4);
        ABTS_STR_NEQUAL(tc, "beta", data, 5);

        e = APR_BUCKET_NEXT(e);
    }

    /* now delete the "alpha" buckets */
    for (n = 0, e = APR_BRIGADE_FIRST(bb); n < 4; n++) {
        apr_bucket *f;

        ABTS_ASSERT(tc, "reached end of brigade",
                    e != APR_BRIGADE_SENTINEL(bb));
        f = APR_BUCKET_NEXT(e);
        apr_bucket_delete(e);
        e = APR_BUCKET_NEXT(f);
    }    
    
    ABTS_ASSERT(tc, "eight buckets reduced to four", 
                count_buckets(bb) == 4);

    flatten_match(tc, "flatten beta brigade", bb,
                  "beta" "beta" "beta" "beta");

    apr_brigade_destroy(bb);
    apr_bucket_alloc_destroy(ba);
}

#define TIF_FNAME "testfile.txt"

static void test_insertfile(abts_case *tc, void *ctx)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bb;
    const apr_off_t bignum = (APR_INT64_C(2) << 32) + 424242;
    apr_off_t count;
    apr_file_t *f;
    apr_bucket *e;

    ABTS_ASSERT(tc, "open test file",
                apr_file_open(&f, TIF_FNAME,
                              APR_FOPEN_WRITE | APR_FOPEN_TRUNCATE
                            | APR_FOPEN_CREATE | APR_FOPEN_SPARSE,
                              APR_OS_DEFAULT, p) == APR_SUCCESS);

    if (apr_file_trunc(f, bignum)) {
        apr_file_close(f);
        apr_file_remove(TIF_FNAME, p);
        ABTS_NOT_IMPL(tc, "Skipped: could not create large file");
        return;
    }
    
    bb = apr_brigade_create(p, ba);

    e = apr_brigade_insert_file(bb, f, 0, bignum, p);
    
    ABTS_ASSERT(tc, "inserted file was not at end of brigade",
                e == APR_BRIGADE_LAST(bb));

    /* check that the total size of inserted buckets is equal to the
     * total size of the file. */
    count = 0;

    for (e = APR_BRIGADE_FIRST(bb);
         e != APR_BRIGADE_SENTINEL(bb);
         e = APR_BUCKET_NEXT(e)) {
        ABTS_ASSERT(tc, "bucket size sane", e->length != (apr_size_t)-1);
        count += e->length;
    }

    ABTS_ASSERT(tc, "total size of buckets incorrect", count == bignum);

    apr_brigade_destroy(bb);

    /* Truncate the file to zero size before close() so that we don't
     * actually write out the large file if we are on a non-sparse file
     * system - like Mac OS X's HFS.  Otherwise, pity the poor user who
     * has to wait for the 8GB file to be written to disk.
     */
    apr_file_trunc(f, 0);

    apr_file_close(f);
    apr_bucket_alloc_destroy(ba);
    apr_file_remove(TIF_FNAME, p);
}

/* Make a test file named FNAME, and write CONTENTS to it. */
static apr_file_t *make_test_file(abts_case *tc, const char *fname,
                                  const char *contents)
{
    apr_file_t *f;

    ABTS_ASSERT(tc, "create test file",
                apr_file_open(&f, fname,
                              APR_FOPEN_READ|APR_FOPEN_WRITE|APR_FOPEN_TRUNCATE|APR_FOPEN_CREATE,
                              APR_OS_DEFAULT, p) == APR_SUCCESS);
    
    ABTS_ASSERT(tc, "write test file contents",
                apr_file_puts(contents, f) == APR_SUCCESS);

    return f;
}

static void test_manyfile(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bb = apr_brigade_create(p, ba);
    apr_file_t *f;

    f = make_test_file(tc, "manyfile.bin",
                       "world" "hello" "brave" " ,\n");

    apr_brigade_insert_file(bb, f, 5, 5, p);
    apr_brigade_insert_file(bb, f, 16, 1, p);
    apr_brigade_insert_file(bb, f, 15, 1, p);
    apr_brigade_insert_file(bb, f, 10, 5, p);
    apr_brigade_insert_file(bb, f, 15, 1, p);
    apr_brigade_insert_file(bb, f, 0, 5, p);
    apr_brigade_insert_file(bb, f, 17, 1, p);

    /* can you tell what it is yet? */
    flatten_match(tc, "file seek test", bb,
                  "hello, brave world\n");

    apr_file_close(f);
    apr_brigade_destroy(bb);
    apr_bucket_alloc_destroy(ba);
}

/* Regression test for PR 34708, where a file bucket will keep
 * duplicating itself on being read() when EOF is reached
 * prematurely. */
static void test_truncfile(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bb = apr_brigade_create(p, ba);
    apr_file_t *f = make_test_file(tc, "testfile.txt", "hello");
    apr_bucket *e;
    const char *buf;
    apr_size_t len;

    apr_brigade_insert_file(bb, f, 0, 5, p);

    apr_file_trunc(f, 0);

    e = APR_BRIGADE_FIRST(bb);

    ABTS_ASSERT(tc, "single bucket in brigade",
                APR_BUCKET_NEXT(e) == APR_BRIGADE_SENTINEL(bb));

    apr_bucket_file_enable_mmap(e, 0);

    ABTS_ASSERT(tc, "read gave APR_EOF",
                apr_bucket_read(e, &buf, &len, APR_BLOCK_READ) == APR_EOF);

    ABTS_ASSERT(tc, "read length 0", len == 0);
    
    ABTS_ASSERT(tc, "still a single bucket in brigade",
                APR_BUCKET_NEXT(e) == APR_BRIGADE_SENTINEL(bb));

    apr_file_close(f);
    apr_brigade_destroy(bb);
    apr_bucket_alloc_destroy(ba);
}

static const char hello[] = "hello, world";

static void test_partition(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bb = apr_brigade_create(p, ba);
    apr_bucket *e;

    e = apr_bucket_immortal_create(hello, strlen(hello), ba);
    APR_BRIGADE_INSERT_HEAD(bb, e);

    apr_assert_success(tc, "partition brigade",
                       apr_brigade_partition(bb, 5, &e));

    test_bucket_content(tc, APR_BRIGADE_FIRST(bb),
                        "hello", 5);

    test_bucket_content(tc, APR_BRIGADE_LAST(bb),
                        ", world", 7);

    ABTS_ASSERT(tc, "partition returns APR_INCOMPLETE",
                apr_brigade_partition(bb, 8192, &e));

    ABTS_ASSERT(tc, "APR_INCOMPLETE partition returned sentinel",
                e == APR_BRIGADE_SENTINEL(bb));

    apr_brigade_destroy(bb);
    apr_bucket_alloc_destroy(ba);
}

static void test_write_split(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bb1 = apr_brigade_create(p, ba);
    apr_bucket_brigade *bb2;
    apr_bucket *e;

    e = apr_bucket_heap_create(hello, strlen(hello), NULL, ba);
    APR_BRIGADE_INSERT_HEAD(bb1, e);
    apr_bucket_split(e, strlen("hello, "));
    bb2 = apr_brigade_split(bb1, APR_BRIGADE_LAST(bb1));
    apr_brigade_write(bb1, NULL, NULL, "foo", strlen("foo"));
    test_bucket_content(tc, APR_BRIGADE_FIRST(bb2), "world", 5);

    apr_brigade_destroy(bb1);
    apr_brigade_destroy(bb2);
    apr_bucket_alloc_destroy(ba);
}

static void test_write_putstrs(abts_case *tc, void *data)
{
    apr_bucket_alloc_t *ba = apr_bucket_alloc_create(p);
    apr_bucket_brigade *bb = apr_brigade_create(p, ba);
    apr_bucket *e;
    char buf[30];
    apr_size_t len = sizeof(buf);
    const char *expect = "123456789abcdefghij";

    e = apr_bucket_heap_create("1", 1, NULL, ba);
    APR_BRIGADE_INSERT_HEAD(bb, e);

    apr_brigade_putstrs(bb, NULL, NULL, "2", "34", "567", "8", "9a", "bcd",
                        "e", "f", "gh", "i", NULL);
    apr_brigade_putstrs(bb, NULL, NULL, "j", NULL);
    apr_assert_success(tc, "apr_brigade_flatten",
                       apr_brigade_flatten(bb, buf, &len));
    ABTS_STR_NEQUAL(tc, expect, buf, strlen(expect));

    apr_brigade_destroy(bb);
    apr_bucket_alloc_destroy(ba);
}

abts_suite *testbuckets(abts_suite *suite)
{
    suite = ADD_SUITE(suite);

    abts_run_test(suite, test_create, NULL);
    abts_run_test(suite, test_simple, NULL);
    abts_run_test(suite, test_flatten, NULL);
    abts_run_test(suite, test_split, NULL);
    abts_run_test(suite, test_bwrite, NULL);
    abts_run_test(suite, test_splitline, NULL);
    abts_run_test(suite, test_splits, NULL);
    abts_run_test(suite, test_insertfile, NULL);
    abts_run_test(suite, test_manyfile, NULL);
    abts_run_test(suite, test_truncfile, NULL);
    abts_run_test(suite, test_partition, NULL);
    abts_run_test(suite, test_write_split, NULL);
    abts_run_test(suite, test_write_putstrs, NULL);

    return suite;
}


