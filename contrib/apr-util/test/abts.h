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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#ifndef ABTS_H
#define ABTS_H

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE  1
#endif

struct sub_suite {
    const char *name;
    int num_test;
    int failed;
    int not_run;
    int not_impl;
    struct sub_suite *next;
};
typedef struct sub_suite sub_suite;

struct abts_suite {
    sub_suite *head;
    sub_suite *tail;
};
typedef struct abts_suite abts_suite;

struct abts_case {
    int failed;
    sub_suite *suite;
};
typedef struct abts_case abts_case;

typedef void (*test_func)(abts_case *tc, void *data);

#define ADD_SUITE(suite) abts_add_suite(suite, __FILE__);

abts_suite *abts_add_suite(abts_suite *suite, const char *suite_name);
void abts_run_test(abts_suite *ts, test_func f, void *value);
void abts_log_message(const char *fmt, ...);

void abts_int_equal(abts_case *tc, const int expected, const int actual, int lineno);
void abts_int_nequal(abts_case *tc, const int expected, const int actual, int lineno);
void abts_str_equal(abts_case *tc, const char *expected, const char *actual, int lineno);
void abts_str_nequal(abts_case *tc, const char *expected, const char *actual,
                       size_t n, int lineno);
void abts_ptr_notnull(abts_case *tc, const void *ptr, int lineno);
void abts_ptr_equal(abts_case *tc, const void *expected, const void *actual, int lineno);
void abts_true(abts_case *tc, int condition, int lineno);
void abts_fail(abts_case *tc, const char *message, int lineno);
void abts_not_impl(abts_case *tc, const char *message, int lineno);
void abts_assert(abts_case *tc, const char *message, int condition, int lineno);

/* Convenience macros. Ryan hates these! */
#define ABTS_INT_EQUAL(a, b, c)     abts_int_equal(a, b, c, __LINE__)
#define ABTS_INT_NEQUAL(a, b, c)    abts_int_nequal(a, b, c, __LINE__)
#define ABTS_STR_EQUAL(a, b, c)     abts_str_equal(a, b, c, __LINE__)
#define ABTS_STR_NEQUAL(a, b, c, d) abts_str_nequal(a, b, c, d, __LINE__)
#define ABTS_PTR_NOTNULL(a, b)      abts_ptr_notnull(a, b, __LINE__)
#define ABTS_PTR_EQUAL(a, b, c)     abts_ptr_equal(a, b, c, __LINE__)
#define ABTS_TRUE(a, b)             abts_true(a, b, __LINE__);
#define ABTS_FAIL(a, b)             abts_fail(a, b, __LINE__);
#define ABTS_NOT_IMPL(a, b)         abts_not_impl(a, b, __LINE__);
#define ABTS_ASSERT(a, b, c)        abts_assert(a, b, c, __LINE__);

#endif

#ifdef __cplusplus
}
#endif

