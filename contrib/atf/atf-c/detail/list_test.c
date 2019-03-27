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

#include "atf-c/detail/list.h"

#include <stdio.h>
#include <string.h>

#include <atf-c.h>

#include "atf-c/detail/test_helpers.h"
#include "atf-c/utils.h"

/* ---------------------------------------------------------------------
 * Tests for the "atf_list" type.
 * --------------------------------------------------------------------- */

/*
 * Constructors and destructors.
 */

ATF_TC(list_init);
ATF_TC_HEAD(list_init, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_list_init function");
}
ATF_TC_BODY(list_init, tc)
{
    atf_list_t list;

    RE(atf_list_init(&list));
    ATF_REQUIRE_EQ(atf_list_size(&list), 0);
    atf_list_fini(&list);
}

/*
 * Getters.
 */

ATF_TC(list_index);
ATF_TC_HEAD(list_index, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_list_index function");
}
ATF_TC_BODY(list_index, tc)
{
    atf_list_t list;
    int i1 = 1;
    int i2 = 5;
    int i3 = 9;

    RE(atf_list_init(&list));
    RE(atf_list_append(&list, &i1, false));
    RE(atf_list_append(&list, &i2, false));
    RE(atf_list_append(&list, &i3, false));

    ATF_CHECK_EQ(*(int *)atf_list_index(&list, 0), 1);
    ATF_CHECK_EQ(*(int *)atf_list_index(&list, 1), 5);
    ATF_CHECK_EQ(*(int *)atf_list_index(&list, 2), 9);

    atf_list_fini(&list);
}

ATF_TC(list_index_c);
ATF_TC_HEAD(list_index_c, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_list_index_c function");
}
ATF_TC_BODY(list_index_c, tc)
{
    atf_list_t list;
    int i1 = 1;
    int i2 = 5;
    int i3 = 9;

    RE(atf_list_init(&list));
    RE(atf_list_append(&list, &i1, false));
    RE(atf_list_append(&list, &i2, false));
    RE(atf_list_append(&list, &i3, false));

    ATF_CHECK_EQ(*(const int *)atf_list_index_c(&list, 0), 1);
    ATF_CHECK_EQ(*(const int *)atf_list_index_c(&list, 1), 5);
    ATF_CHECK_EQ(*(const int *)atf_list_index_c(&list, 2), 9);

    atf_list_fini(&list);
}

ATF_TC_WITHOUT_HEAD(list_to_charpp_empty);
ATF_TC_BODY(list_to_charpp_empty, tc)
{
    atf_list_t list;
    char **array;

    RE(atf_list_init(&list));
    ATF_REQUIRE((array = atf_list_to_charpp(&list)) != NULL);
    atf_list_fini(&list);

    ATF_CHECK_EQ(NULL, array[0]);
    atf_utils_free_charpp(array);
}

ATF_TC_WITHOUT_HEAD(list_to_charpp_some);
ATF_TC_BODY(list_to_charpp_some, tc)
{
    atf_list_t list;
    char **array;

    char s1[] = "one";
    char s2[] = "two";
    char s3[] = "three";

    RE(atf_list_init(&list));
    RE(atf_list_append(&list, s1, false));
    RE(atf_list_append(&list, s2, false));
    RE(atf_list_append(&list, s3, false));
    ATF_REQUIRE((array = atf_list_to_charpp(&list)) != NULL);
    atf_list_fini(&list);

    ATF_CHECK_STREQ("one", array[0]);
    ATF_CHECK_STREQ("two", array[1]);
    ATF_CHECK_STREQ("three", array[2]);
    ATF_CHECK_EQ(NULL, array[3]);
    atf_utils_free_charpp(array);
}

/*
 * Modifiers.
 */

ATF_TC(list_append);
ATF_TC_HEAD(list_append, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_list_append function");
}
ATF_TC_BODY(list_append, tc)
{
    atf_list_t list;
    size_t i;
    char buf[] = "Test string";

    RE(atf_list_init(&list));
    for (i = 0; i < 1024; i++) {
        ATF_REQUIRE_EQ(atf_list_size(&list), i);
        RE(atf_list_append(&list, buf, false));
    }
    atf_list_fini(&list);
}

ATF_TC(list_append_list);
ATF_TC_HEAD(list_append_list, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_list_append_list "
                      "function");
}
ATF_TC_BODY(list_append_list, tc)
{
    {
        atf_list_t l1, l2;

        RE(atf_list_init(&l1));
        RE(atf_list_init(&l2));

        atf_list_append_list(&l1, &l2);
        ATF_CHECK_EQ(atf_list_size(&l1), 0);

        atf_list_fini(&l1);
    }

    {
        atf_list_t l1, l2;
        int item = 5;

        RE(atf_list_init(&l1));
        RE(atf_list_append(&l1, &item, false));
        RE(atf_list_init(&l2));

        atf_list_append_list(&l1, &l2);
        ATF_CHECK_EQ(atf_list_size(&l1), 1);
        ATF_CHECK_EQ(*(int *)atf_list_index(&l1, 0), item);

        atf_list_fini(&l1);
    }

    {
        atf_list_t l1, l2;
        int item = 5;

        RE(atf_list_init(&l1));
        RE(atf_list_init(&l2));
        RE(atf_list_append(&l2, &item, false));

        atf_list_append_list(&l1, &l2);
        ATF_CHECK_EQ(atf_list_size(&l1), 1);
        ATF_CHECK_EQ(*(int *)atf_list_index(&l1, 0), item);

        atf_list_fini(&l1);
    }

    {
        atf_list_t l1, l2;
        int item1 = 5;
        int item2 = 9;

        RE(atf_list_init(&l1));
        RE(atf_list_append(&l1, &item1, false));
        RE(atf_list_init(&l2));
        RE(atf_list_append(&l2, &item2, false));

        atf_list_append_list(&l1, &l2);
        ATF_CHECK_EQ(atf_list_size(&l1), 2);
        ATF_CHECK_EQ(*(int *)atf_list_index(&l1, 0), item1);
        ATF_CHECK_EQ(*(int *)atf_list_index(&l1, 1), item2);

        atf_list_fini(&l1);
    }

    {
        atf_list_t l1, l2;
        atf_list_citer_t end1, end2;

        RE(atf_list_init(&l1));
        RE(atf_list_init(&l2));

        end1 = atf_list_end_c(&l1);
        end2 = atf_list_end_c(&l2);
        /* XXX Shouldn't query m_entry here. */
        ATF_CHECK(end1.m_entry != end2.m_entry);

        atf_list_append_list(&l1, &l2);
        ATF_CHECK(atf_list_end_c(&l1).m_entry == end2.m_entry);

        atf_list_fini(&l1);
    }
}

/*
 * Macros.
 */

ATF_TC(list_for_each);
ATF_TC_HEAD(list_for_each, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_list_for_each macro");
}
ATF_TC_BODY(list_for_each, tc)
{
    atf_list_t list;
    atf_list_iter_t iter;
    size_t count, i, size;
    int nums[10];

    printf("Iterating over empty list\n");
    RE(atf_list_init(&list));
    count = 0;
    atf_list_for_each(iter, &list) {
        count++;
        printf("Item count is now %zd\n", count);
    }
    ATF_REQUIRE_EQ(count, 0);
    atf_list_fini(&list);

    for (size = 0; size <= 10; size++) {
        printf("Iterating over list of %zd elements\n", size);
        RE(atf_list_init(&list));
        for (i = 0; i < size; i++) {
            nums[i] = i + 1;
            RE(atf_list_append(&list, &nums[i], false));
        }
        count = 0;
        atf_list_for_each(iter, &list) {
            printf("Retrieved item: %d\n", *(int *)atf_list_iter_data(iter));
            count++;
        }
        ATF_REQUIRE_EQ(count, size);
        atf_list_fini(&list);
    }
}

ATF_TC(list_for_each_c);
ATF_TC_HEAD(list_for_each_c, tc)
{
    atf_tc_set_md_var(tc, "descr", "Checks the atf_list_for_each_c macro");
}
ATF_TC_BODY(list_for_each_c, tc)
{
    atf_list_t list;
    atf_list_citer_t iter;
    size_t count, i, size;
    int nums[10];

    printf("Iterating over empty list\n");
    RE(atf_list_init(&list));
    count = 0;
    atf_list_for_each_c(iter, &list) {
        count++;
        printf("Item count is now %zd\n", count);
    }
    ATF_REQUIRE_EQ(count, 0);
    atf_list_fini(&list);

    for (size = 0; size <= 10; size++) {
        printf("Iterating over list of %zd elements\n", size);
        RE(atf_list_init(&list));
        for (i = 0; i < size; i++) {
            nums[i] = i + 1;
            RE(atf_list_append(&list, &nums[i], false));
        }
        count = 0;
        atf_list_for_each_c(iter, &list) {
            printf("Retrieved item: %d\n",
                   *(const int *)atf_list_citer_data(iter));
            count++;
        }
        ATF_REQUIRE_EQ(count, size);
        atf_list_fini(&list);
    }
}

/* ---------------------------------------------------------------------
 * Main.
 * --------------------------------------------------------------------- */

ATF_TP_ADD_TCS(tp)
{
    /* Constructors and destructors. */
    ATF_TP_ADD_TC(tp, list_init);

    /* Getters. */
    ATF_TP_ADD_TC(tp, list_index);
    ATF_TP_ADD_TC(tp, list_index_c);
    ATF_TP_ADD_TC(tp, list_to_charpp_empty);
    ATF_TP_ADD_TC(tp, list_to_charpp_some);

    /* Modifiers. */
    ATF_TP_ADD_TC(tp, list_append);
    ATF_TP_ADD_TC(tp, list_append_list);

    /* Macros. */
    ATF_TP_ADD_TC(tp, list_for_each);
    ATF_TP_ADD_TC(tp, list_for_each_c);

    return atf_no_error();
}
