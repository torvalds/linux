/* $NetBSD: t_basic.c,v 1.4 2011/04/20 20:02:58 martin Exp $ */

/*
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Written by Jason Thorpe 5/26/2006.
 * Public domain.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_basic.c,v 1.4 2011/04/20 20:02:58 martin Exp $");

#include <stdlib.h>
#include <string.h>
#include <prop/proplib.h>

#include <atf-c.h>

static const char compare1[] =
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
"<!DOCTYPE plist PUBLIC \"-//Apple Computer//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
"<plist version=\"1.0\">\n"
"<dict>\n"
"	<key>false-val</key>\n"
"	<false/>\n"
"	<key>one</key>\n"
"	<integer>1</integer>\n"
"	<key>three</key>\n"
"	<array>\n"
"		<dict>\n"
"			<key>one</key>\n"
"			<integer>1</integer>\n"
"			<key>two</key>\n"
"			<string>number-two</string>\n"
"		</dict>\n"
"		<dict>\n"
"			<key>one</key>\n"
"			<integer>1</integer>\n"
"			<key>two</key>\n"
"			<string>number-two</string>\n"
"		</dict>\n"
"		<dict>\n"
"			<key>one</key>\n"
"			<integer>1</integer>\n"
"			<key>two</key>\n"
"			<string>number-two</string>\n"
"		</dict>\n"
"	</array>\n"
"	<key>true-val</key>\n"
"	<true/>\n"
"	<key>two</key>\n"
"	<string>number-two</string>\n"
"</dict>\n"
"</plist>\n";

ATF_TC(prop_basic);
ATF_TC_HEAD(prop_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of proplib(3)");
}

ATF_TC_BODY(prop_basic, tc)
{
	prop_dictionary_t dict;
	char *ext1;

	dict = prop_dictionary_create();
	ATF_REQUIRE(dict != NULL);

	{
		prop_number_t num = prop_number_create_integer(1);
		ATF_REQUIRE(num != NULL);

		ATF_REQUIRE_EQ(prop_dictionary_set(dict, "one", num), true);
		prop_object_release(num);
	}

	{
		prop_string_t str = prop_string_create_cstring("number-two");
		ATF_REQUIRE(str != NULL);

		ATF_REQUIRE_EQ(prop_dictionary_set(dict, "two", str), true);
		prop_object_release(str);
	}

	{
		prop_array_t arr;
		prop_dictionary_t dict_copy;
		int i;

		arr = prop_array_create();
		ATF_REQUIRE(arr != NULL);

		for (i = 0; i < 3; ++i) {
			dict_copy = prop_dictionary_copy(dict);
			ATF_REQUIRE(dict_copy != NULL);
			ATF_REQUIRE_EQ(prop_array_add(arr, dict_copy), true);
			prop_object_release(dict_copy);
		}

		ATF_REQUIRE_EQ(prop_dictionary_set(dict, "three", arr), true);
		prop_object_release(arr);
	}

	{
		prop_bool_t val = prop_bool_create(true);
		ATF_REQUIRE(val != NULL);
		ATF_REQUIRE_EQ(prop_dictionary_set(dict, "true-val", val), true);
		prop_object_release(val);

		val = prop_bool_create(false);
		ATF_REQUIRE(val != NULL);
		ATF_REQUIRE_EQ(prop_dictionary_set(dict, "false-val", val), true);
		prop_object_release(val);
	}

	ext1 = prop_dictionary_externalize(dict);
	ATF_REQUIRE(ext1 != NULL);
	ATF_REQUIRE_STREQ(compare1, ext1);

	{
		prop_dictionary_t dict2;
		char *ext2;

		dict2 = prop_dictionary_internalize(ext1);
		ATF_REQUIRE(dict2 != NULL);
		ext2 = prop_dictionary_externalize(dict2);
		ATF_REQUIRE(ext2 != NULL);
		ATF_REQUIRE_STREQ(ext1, ext2);
		prop_object_release(dict2);
		free(ext2);
	}

	prop_object_release(dict);
	free(ext1);
}

ATF_TC(prop_dictionary_equals);
ATF_TC_HEAD(prop_dictionary_equals, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test prop_dictionary_equals(3)");
}

ATF_TC_BODY(prop_dictionary_equals, tc)
{
	prop_dictionary_t c, d;

	/*
	 * Fixed, should not fail any more...
	 *
	atf_tc_expect_death("PR lib/43964");
	 *
	 */

	d = prop_dictionary_internalize(compare1);

	ATF_REQUIRE(d != NULL);

	c = prop_dictionary_copy(d);

	ATF_REQUIRE(c != NULL);

	if (prop_dictionary_equals(c, d) != true)
		atf_tc_fail("dictionaries are not equal");

	prop_object_release(c);
	prop_object_release(d);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, prop_basic);
	ATF_TP_ADD_TC(tp, prop_dictionary_equals);

	return atf_no_error();
}
