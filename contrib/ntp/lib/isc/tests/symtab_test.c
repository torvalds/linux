/*
 * Copyright (C) 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*! \file */

#include <config.h>

#include <atf-c.h>

#include <unistd.h>

#include <isc/symtab.h>
#include <isc/print.h>

#include "isctest.h"

static void
undefine(char *key, unsigned int type, isc_symvalue_t value, void *arg) {
	UNUSED(arg);

	ATF_REQUIRE_EQ(type, 1);
	isc_mem_free(mctx, key);
	isc_mem_free(mctx, value.as_pointer);
}

/*
 * Individual unit tests
 */

ATF_TC(symtab_grow);
ATF_TC_HEAD(symtab_grow, tc) {
	atf_tc_set_md_var(tc, "descr", "symbol table growth");
}
ATF_TC_BODY(symtab_grow, tc) {
	isc_result_t result;
	isc_symtab_t *st = NULL;
	isc_symvalue_t value;
	isc_symexists_t policy = isc_symexists_reject;
	int i;

	UNUSED(tc);

	result = isc_test_begin(NULL, ISC_TRUE);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);

	result = isc_symtab_create(mctx, 3, undefine, NULL, ISC_FALSE, &st);
	ATF_REQUIRE_EQ(result, ISC_R_SUCCESS);
	ATF_REQUIRE(st != NULL);

	/* Nothing should be in the table yet */

	/*
	 * Put 1024 entries in the table (this should necessate
	 * regrowing the hash table several times
	 */
	for (i = 0; i < 1024; i++) {
		char str[16], *key;

		snprintf(str, sizeof(str), "%04x", i);
		key = isc_mem_strdup(mctx, str);
		ATF_REQUIRE(key != NULL);
		value.as_pointer = isc_mem_strdup(mctx, str);
		ATF_REQUIRE(value.as_pointer != NULL);
		result = isc_symtab_define(st, key, 1, value, policy);
		ATF_CHECK_EQ(result, ISC_R_SUCCESS);
		if (result != ISC_R_SUCCESS)
			undefine(key, 1, value, NULL);
	}

	/*
	 * Try to put them in again; this should fail
	 */
	for (i = 0; i < 1024; i++) {
		char str[16], *key;

		snprintf(str, sizeof(str), "%04x", i);
		key = isc_mem_strdup(mctx, str);
		ATF_REQUIRE(key != NULL);
		value.as_pointer = isc_mem_strdup(mctx, str);
		ATF_REQUIRE(value.as_pointer != NULL);
		result = isc_symtab_define(st, key, 1, value, policy);
		ATF_CHECK_EQ(result, ISC_R_EXISTS);
		undefine(key, 1, value, NULL);
	}

	/*
	 * Retrieve them; this should succeed
	 */
	for (i = 0; i < 1024; i++) {
		char str[16];

		snprintf(str, sizeof(str), "%04x", i);
		result = isc_symtab_lookup(st, str, 0, &value);
		ATF_CHECK_EQ(result, ISC_R_SUCCESS);
		ATF_CHECK_STREQ(str, value.as_pointer);
	}

	/*
	 * Undefine them
	 */
	for (i = 0; i < 1024; i++) {
		char str[16];

		snprintf(str, sizeof(str), "%04x", i);
		result = isc_symtab_undefine(st, str, 1);
		ATF_CHECK_EQ(result, ISC_R_SUCCESS);
	}

	/*
	 * Retrieve them again; this should fail
	 */
	for (i = 0; i < 1024; i++) {
		char str[16];

		snprintf(str, sizeof(str), "%04x", i);
		result = isc_symtab_lookup(st, str, 0, &value);
		ATF_CHECK_EQ(result, ISC_R_NOTFOUND);
	}

	isc_symtab_destroy(&st);
	isc_test_end();
}

/*
 * Main
 */
ATF_TP_ADD_TCS(tp) {
	ATF_TP_ADD_TC(tp, symtab_grow);

	return (atf_no_error());
}

