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

/* Some simple functions to make the test apps easier to write and
 * a bit more consistent...
 * this is a >copy< of apr_test.h
 */

/* Things to bear in mind when using these...
 *
 * If you include '\t' within the string passed in it won't be included
 * in the spacing, so use spaces instead :)
 * 
 */ 

#ifndef APU_TEST_INCLUDES
#define APU_TEST_INCLUDES

#include "apr_strings.h"
#include "apr_time.h"

#define TEST_EQ(str, func, value, good, bad) \
    printf("%-60s", str); \
    { \
    apr_status_t rv; \
    if ((rv = func) == value){ \
        char errmsg[200]; \
        printf("%s\n", bad); \
        fprintf(stderr, "Error was %d : %s\n", rv, \
                apr_strerror(rv, (char*)&errmsg, 200)); \
        exit(-1); \
    } \
    printf("%s\n", good); \
    }

#define TEST_NEQ(str, func, value, good, bad) \
    printf("%-60s", str); \
    { \
    apr_status_t rv; \
    if ((rv = func) != value){ \
        char errmsg[200]; \
        printf("%s\n", bad); \
        fprintf(stderr, "Error was %d : %s\n", rv, \
                apr_strerror(rv, (char*)&errmsg, 200)); \
        exit(-1); \
    } \
    printf("%s\n", good); \
    }

#define TEST_STATUS(str, func, testmacro, good, bad) \
    printf("%-60s", str); \
    { \
        apr_status_t rv = func; \
        if (!testmacro(rv)) { \
            char errmsg[200]; \
            printf("%s\n", bad); \
            fprintf(stderr, "Error was %d : %s\n", rv, \
                    apr_strerror(rv, (char*)&errmsg, 200)); \
            exit(-1); \
        } \
        printf("%s\n", good); \
    }

#define STD_TEST_NEQ(str, func) \
	TEST_NEQ(str, func, APR_SUCCESS, "OK", "Failed");

#define PRINT_ERROR(rv) \
    { \
        char errmsg[200]; \
        fprintf(stderr, "Error was %d : %s\n", rv, \
                apr_strerror(rv, (char*)&errmsg, 200)); \
        exit(-1); \
    }

#define MSG_AND_EXIT(msg) \
    printf("%s\n", msg); \
    exit (-1);

#define TIME_FUNCTION(time, function) \
    { \
        apr_time_t tt = apr_time_now(); \
        function; \
        time = apr_time_now() - tt; \
    }
    
    
#endif /* APU_TEST_INCLUDES */
