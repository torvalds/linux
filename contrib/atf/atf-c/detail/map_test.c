/* Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#include "atf-c/detail/map.h"

#include <stdio.h>
#include <string.h>

#include <atf-c.h>

#include "atf-c/detail/test_helpers.h"
#include "atf-c/utils.h"

/* ---------------------------------------------------------------------
 * Tests for the "atf_map" type.
 * --------------------------------------------------------------------- */

/*
 * Constructors and destructors.
 */

ATF_TC(map_init);
ATF_TC_HEAD(map_init, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_map_init function");
}
ATF_TC_BODY(map_init, tc)
{
    atf_map_t map;

    RE(atf_map_init(&map));
    ATF_REQUIRE_EQ(atf_map_size(&map), 0);
    atf_map_fini(&map);
}

ATF_TC_WITHOUT_HEAD(map_init_charpp_null);
ATF_TC_BODY(map_init_charpp_null, tc)
{
    atf_map_t map;

    RE(atf_map_init_charpp(&map, NULL));
    ATF_REQUIRE_EQ(atf_map_size(&map), 0);
    atf_map_fini(&map);
}

ATF_TC_WITHOUT_HEAD(map_init_charpp_empty);
ATF_TC_BODY(map_init_charpp_empty, tc)
{
    const char *const array[] = { NULL };
    atf_map_t map;

    RE(atf_map_init_charpp(&map, array));
    ATF_REQUIRE_EQ(atf_map_size(&map), 0);
    atf_map_fini(&map);
}

ATF_TC_WITHOUT_HEAD(map_init_charpp_some);
ATF_TC_BODY(map_init_charpp_some, tc)
{
    const char *const array[] = { "K1", "V1", "K2", "V2", NULL };
    atf_map_t map;
    atf_map_citer_t iter;

    RE(atf_map_init_charpp(&map, array));
    ATF_REQUIRE_EQ(atf_map_size(&map), 2);

    iter = atf_map_find_c(&map, "K1");
    ATF_REQUIRE(!atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));
    ATF_REQUIRE(strcmp(atf_map_citer_key(iter), "K1") == 0);
    ATF_REQUIRE(strcmp(atf_map_citer_data(iter), "V1") == 0);

    iter = atf_map_find_c(&map, "K2");
    ATF_REQUIRE(!atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));
    ATF_REQUIRE(strcmp(atf_map_citer_key(iter), "K2") == 0);
    ATF_REQUIRE(strcmp(atf_map_citer_data(iter), "V2") == 0);

    atf_map_fini(&map);
}

ATF_TC_WITHOUT_HEAD(map_init_charpp_short);
ATF_TC_BODY(map_init_charpp_short, tc)
{
    const char *const array[] = { "K1", "V1", "K2", NULL };
    atf_map_t map;

    atf_error_t err = atf_map_init_charpp(&map, array);
    ATF_REQUIRE(atf_is_error(err));
    ATF_REQUIRE(atf_error_is(err, "libc"));
}

/*
 * Getters.
 */

ATF_TC(find);
ATF_TC_HEAD(find, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_map_find function");
}
ATF_TC_BODY(find, tc)
{
    atf_map_t map;
    char val1[] = "V1";
    char val2[] = "V2";
    atf_map_iter_t iter;

    RE(atf_map_init(&map));
    RE(atf_map_insert(&map, "K1", val1, false));
    RE(atf_map_insert(&map, "K2", val2, false));

    iter = atf_map_find(&map, "K0");
    ATF_REQUIRE(atf_equal_map_iter_map_iter(iter, atf_map_end(&map)));

    iter = atf_map_find(&map, "K1");
    ATF_REQUIRE(!atf_equal_map_iter_map_iter(iter, atf_map_end(&map)));
    ATF_REQUIRE(strcmp(atf_map_iter_key(iter), "K1") == 0);
    ATF_REQUIRE(strcmp(atf_map_iter_data(iter), "V1") == 0);
    strcpy(atf_map_iter_data(iter), "Z1");

    iter = atf_map_find(&map, "K1");
    ATF_REQUIRE(!atf_equal_map_iter_map_iter(iter, atf_map_end(&map)));
    ATF_REQUIRE(strcmp(atf_map_iter_key(iter), "K1") == 0);
    ATF_REQUIRE(strcmp(atf_map_iter_data(iter), "Z1") == 0);

    iter = atf_map_find(&map, "K2");
    ATF_REQUIRE(!atf_equal_map_iter_map_iter(iter, atf_map_end(&map)));
    ATF_REQUIRE(strcmp(atf_map_iter_key(iter), "K2") == 0);
    ATF_REQUIRE(strcmp(atf_map_iter_data(iter), "V2") == 0);

    atf_map_fini(&map);
}

ATF_TC(find_c);
ATF_TC_HEAD(find_c, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_map_find_c function");
}
ATF_TC_BODY(find_c, tc)
{
    atf_map_t map;
    char val1[] = "V1";
    char val2[] = "V2";
    atf_map_citer_t iter;

    RE(atf_map_init(&map));
    RE(atf_map_insert(&map, "K1", val1, false));
    RE(atf_map_insert(&map, "K2", val2, false));

    iter = atf_map_find_c(&map, "K0");
    ATF_REQUIRE(atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));

    iter = atf_map_find_c(&map, "K1");
    ATF_REQUIRE(!atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));
    ATF_REQUIRE(strcmp(atf_map_citer_key(iter), "K1") == 0);
    ATF_REQUIRE(strcmp(atf_map_citer_data(iter), "V1") == 0);

    iter = atf_map_find_c(&map, "K2");
    ATF_REQUIRE(!atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));
    ATF_REQUIRE(strcmp(atf_map_citer_key(iter), "K2") == 0);
    ATF_REQUIRE(strcmp(atf_map_citer_data(iter), "V2") == 0);

    atf_map_fini(&map);
}

ATF_TC_WITHOUT_HEAD(to_charpp_empty);
ATF_TC_BODY(to_charpp_empty, tc)
{
    atf_map_t map;
    char **array;

    RE(atf_map_init(&map));
    ATF_REQUIRE((array = atf_map_to_charpp(&map)) != NULL);
    atf_map_fini(&map);

    ATF_CHECK_EQ(NULL, array[0]);
    atf_utils_free_charpp(array);
}

ATF_TC_WITHOUT_HEAD(to_charpp_some);
ATF_TC_BODY(to_charpp_some, tc)
{
    atf_map_t map;
    char **array;

    char s1[] = "one";
    char s2[] = "two";
    char s3[] = "three";

    RE(atf_map_init(&map));
    RE(atf_map_insert(&map, "K1", s1, false));
    RE(atf_map_insert(&map, "K2", s2, false));
    RE(atf_map_insert(&map, "K3", s3, false));
    ATF_REQUIRE((array = atf_map_to_charpp(&map)) != NULL);
    atf_map_fini(&map);

    ATF_CHECK_STREQ("K1", array[0]);
    ATF_CHECK_STREQ("one", array[1]);
    ATF_CHECK_STREQ("K2", array[2]);
    ATF_CHECK_STREQ("two", array[3]);
    ATF_CHECK_STREQ("K3", array[4]);
    ATF_CHECK_STREQ("three", array[5]);
    ATF_CHECK_EQ(NULL, array[6]);
    atf_utils_free_charpp(array);
}

/*
 * Modifiers.
 */

ATF_TC(map_insert);
ATF_TC_HEAD(map_insert, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_map_insert function");
}
ATF_TC_BODY(map_insert, tc)
{
    atf_map_t map;
    char buf[] = "1st test string";
    char buf2[] = "2nd test string";
    const char *ptr;
    atf_map_citer_t iter;

    RE(atf_map_init(&map));

    printf("Inserting some values\n");
    ATF_REQUIRE_EQ(atf_map_size(&map), 0);
    RE(atf_map_insert(&map, "K1", buf, false));
    ATF_REQUIRE_EQ(atf_map_size(&map), 1);
    RE(atf_map_insert(&map, "K2", buf, false));
    ATF_REQUIRE_EQ(atf_map_size(&map), 2);
    RE(atf_map_insert(&map, "K3", buf, false));
    ATF_REQUIRE_EQ(atf_map_size(&map), 3);

    printf("Replacing a value\n");
    iter = atf_map_find_c(&map, "K3");
    ATF_REQUIRE(!atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));
    ptr = atf_map_citer_data(iter);
    ATF_REQUIRE_EQ(ptr, buf);
    RE(atf_map_insert(&map, "K3", buf2, false));
    ATF_REQUIRE_EQ(atf_map_size(&map), 3);
    iter = atf_map_find_c(&map, "K3");
    ATF_REQUIRE(!atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));
    ptr = atf_map_citer_data(iter);
    ATF_REQUIRE_EQ(ptr, buf2);

    atf_map_fini(&map);
}

/*
 * Macros.
 */

ATF_TC(map_for_each);
ATF_TC_HEAD(map_for_each, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_map_for_each macro");
}
ATF_TC_BODY(map_for_each, tc)
{
    atf_map_t map;
    atf_map_iter_t iter;
    size_t count, i, size;
    char keys[10][5];
    int nums[10];

    printf("Iterating over empty map\n");
    RE(atf_map_init(&map));
    count = 0;
    atf_map_for_each(iter, &map) {
        count++;
        printf("Item count is now %zd\n", count);
    }
    ATF_REQUIRE_EQ(count, 0);
    atf_map_fini(&map);

    for (size = 0; size <= 10; size++) {
        printf("Iterating over map of %zd elements\n", size);
        RE(atf_map_init(&map));
        for (i = 0; i < size; i++) {
            nums[i] = i + 1;
            snprintf(keys[i], sizeof(keys[i]), "%d", nums[i]);
            RE(atf_map_insert(&map, keys[i], &nums[i], false));
        }
        count = 0;
        atf_map_for_each(iter, &map) {
            printf("Retrieved item: %d\n", *(int *)atf_map_iter_data(iter));
            count++;
        }
        ATF_REQUIRE_EQ(count, size);
        atf_map_fini(&map);
    }
}

ATF_TC(map_for_each_c);
ATF_TC_HEAD(map_for_each_c, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_map_for_each_c macro");
}
ATF_TC_BODY(map_for_each_c, tc)
{
    atf_map_t map;
    atf_map_citer_t iter;
    size_t count, i, size;
    char keys[10][5];
    int nums[10];

    printf("Iterating over empty map\n");
    RE(atf_map_init(&map));
    count = 0;
    atf_map_for_each_c(iter, &map) {
        count++;
        printf("Item count is now %zd\n", count);
    }
    ATF_REQUIRE_EQ(count, 0);
    atf_map_fini(&map);

    for (size = 0; size <= 10; size++) {
        printf("Iterating over map of %zd elements\n", size);
        RE(atf_map_init(&map));
        for (i = 0; i < size; i++) {
            nums[i] = i + 1;
            snprintf(keys[i], sizeof(keys[i]), "%d", nums[i]);
            RE(atf_map_insert(&map, keys[i], &nums[i], false));
        }
        count = 0;
        atf_map_for_each_c(iter, &map) {
            printf("Retrieved item: %d\n",
                   *(const int *)atf_map_citer_data(iter));
            count++;
        }
        ATF_REQUIRE_EQ(count, size);
        atf_map_fini(&map);
    }
}

/*
 * Other.
 */

ATF_TC(stable_keys);
ATF_TC_HEAD(stable_keys, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks that the keys do not change "
                      "even if their original values do");
}
ATF_TC_BODY(stable_keys, tc)
{
    atf_map_t map;
    atf_map_citer_t iter;
    char key[] = "K1";

    RE(atf_map_init(&map));

    RE(atf_map_insert(&map, key, strdup("test-value"), true));
    iter = atf_map_find_c(&map, "K1");
    ATF_REQUIRE(!atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));
    iter = atf_map_find_c(&map, "K2");
    ATF_REQUIRE(atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));

    strcpy(key, "K2");
    iter = atf_map_find_c(&map, "K1");
    ATF_REQUIRE(!atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));
    iter = atf_map_find_c(&map, "K2");
    ATF_REQUIRE(atf_equal_map_citer_map_citer(iter, atf_map_end_c(&map)));

    atf_map_fini(&map);
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    /* Constructors and destructors. */
    ATF_TP_ADD_TC(tp, map_init);
    ATF_TP_ADD_TC(tp, map_init_charpp_null);
    ATF_TP_ADD_TC(tp, map_init_charpp_empty);
    ATF_TP_ADD_TC(tp, map_init_charpp_some);
    ATF_TP_ADD_TC(tp, map_init_charpp_short);

    /* Getters. */
    ATF_TP_ADD_TC(tp, find);
    ATF_TP_ADD_TC(tp, find_c);
    ATF_TP_ADD_TC(tp, to_charpp_empty);
    ATF_TP_ADD_TC(tp, to_charpp_some);

    /* Modifiers. */
    ATF_TP_ADD_TC(tp, map_insert);

    /* Macros. */
    ATF_TP_ADD_TC(tp, map_for_each);
    ATF_TP_ADD_TC(tp, map_for_each_c);

    /* Other. */
    ATF_TP_ADD_TC(tp, stable_keys);

    return atf_no_error();
}
