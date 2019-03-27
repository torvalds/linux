/* $Id: t_ppath.c,v 1.1 2011/08/25 19:09:46 dyoung Exp $ */

/* Copyright (c) 2010 David Young.  All rights reserved. */

#include <sys/cdefs.h>
__RCSID("$Id: t_ppath.c,v 1.1 2011/08/25 19:09:46 dyoung Exp $");

#include <assert.h>
#include <atf-c.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <ppath/ppath.h>
#include "personnel.h"

void test_ppath_extant_inc(void);
void test_ppath_extant_dec(void);
void test_ppath_component_extant_inc(void);
void test_ppath_component_extant_dec(void);

__strong_alias(ppath_extant_inc, test_ppath_extant_inc);
__strong_alias(ppath_extant_dec, test_ppath_extant_dec);
__strong_alias(ppath_component_extant_inc, test_ppath_component_extant_inc);
__strong_alias(ppath_component_extant_dec, test_ppath_component_extant_dec);

static uint64_t nppath = 0, nppath_component = 0;

static bool
dictionary_equals(prop_dictionary_t ld, prop_dictionary_t rd)
{
	bool eq;
	char *lt, *rt;

	lt = prop_dictionary_externalize(ld);
	rt = prop_dictionary_externalize(rd);

	assert(lt != NULL && rt != NULL);

	eq = (strcmp(lt, rt) == 0);

	free(lt);
	free(rt);

	return eq;
}

static void
assert_no_ppath_extant(void)
{
	ATF_CHECK_EQ(nppath, 0);
}

static void
assert_no_ppath_component_extant(void)
{
	ATF_CHECK_EQ(nppath_component, 0);
}

void
test_ppath_extant_inc(void)
{
	if (++nppath == 0)
		atf_tc_fail("count of extant paths overflowed");
}

void
test_ppath_extant_dec(void)
{
	if (nppath-- == 0)
		atf_tc_fail("count of extant path underflowed");
}

void
test_ppath_component_extant_inc(void)
{
	if (++nppath_component == 0)
		atf_tc_fail("count of extant path components overflowed");
}

void
test_ppath_component_extant_dec(void)
{
	if (nppath_component-- == 0)
		atf_tc_fail("count of extant path components underflowed");
}

ATF_TC(push_until_full);

ATF_TC_HEAD(push_until_full, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_push() returns error "
	    "after ppath_t reaches maximum length");
}

ATF_TC_BODY(push_until_full, tc)
{
	ppath_t *p, *rp;
	ppath_component_t *pc;
	int i;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if ((pc = ppath_idx(0)) == NULL)
		atf_tc_fail("ppath_idx failed");

	for (i = 0; i < PPATH_MAX_COMPONENTS; i++) {
		rp = ppath_push(p, pc);
		ATF_CHECK_EQ(rp, p);
	}

	rp = ppath_push(p, pc);
	ATF_CHECK_EQ(rp, NULL);

	rp = ppath_push(p, pc);
	ATF_CHECK_EQ(rp, NULL);

	ppath_component_release(pc);
	ppath_release(p);

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(pop_until_empty);
ATF_TC_HEAD(pop_until_empty, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_pop() returns error "
	    "after ppath_t is empty");
}

ATF_TC_BODY(pop_until_empty, tc)
{
	ppath_t *p, *rp;
	ppath_component_t *pc, *rpc;
	int i;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if ((pc = ppath_idx(0)) == NULL)
		atf_tc_fail("ppath_idx failed");

	for (i = 0; i < PPATH_MAX_COMPONENTS; i++) {
		rp = ppath_push(p, pc);
		ATF_CHECK_EQ(rp, p);
	}

	for (i = 0; i < PPATH_MAX_COMPONENTS; i++) {
		rp = ppath_pop(p, &rpc);
		ATF_CHECK_EQ(rp, p);
		ATF_CHECK_EQ(rpc, pc);
		ppath_component_release(rpc);
	}

	rp = ppath_pop(p, &rpc);
	ATF_CHECK_EQ(rp, NULL);
	rp = ppath_pop(p, &rpc);
	ATF_CHECK_EQ(rp, NULL);

	ppath_component_release(pc);
	ppath_release(p);

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(length);

ATF_TC_HEAD(length, tc)
{
	atf_tc_set_md_var(tc, "descr", "check that ppath_push() "
	    "and ppath_pop() affect ppath_length() correctly");
}

ATF_TC_BODY(length, tc)
{
	ppath_t *p, *rp;
	ppath_component_t *pc;
	unsigned int i, len;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if ((pc = ppath_idx(0)) == NULL)
		atf_tc_fail("ppath_idx failed");

	for (i = 0; i < PPATH_MAX_COMPONENTS; i++) {
		len = ppath_length(p);
		ATF_CHECK_EQ(len, i);
		rp = ppath_push(p, pc);
		ATF_CHECK_EQ(rp, p);
	}

	for (i = 0; i < PPATH_MAX_COMPONENTS; i++) {
		len = ppath_length(p);
		ATF_CHECK_EQ(len, PPATH_MAX_COMPONENTS - i);
		rp = ppath_pop(p, NULL);
		ATF_CHECK_EQ(rp, p);
	}
	ppath_component_release(pc);
	ppath_release(p);

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(component_at);

ATF_TC_HEAD(component_at, tc)
{
	atf_tc_set_md_var(tc, "descr", "check that ppath_component_at() "
	    "returns the expected component");
}

ATF_TC_BODY(component_at, tc)
{
	ppath_t *p, *rp;
	ppath_component_t *pc;
	unsigned int i;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	for (i = 0; i < PPATH_MAX_COMPONENTS; i++) {
		if ((pc = ppath_idx(i)) == NULL)
			atf_tc_fail("ppath_idx failed");
		rp = ppath_push(p, pc);
		ppath_component_release(pc);
		ATF_CHECK_EQ(rp, p);
	}

	for (i = 0; i < PPATH_MAX_COMPONENTS; i++) {
		pc = ppath_component_at(p, i);
		ATF_CHECK_EQ(ppath_component_idx(pc), (int)i);
		ppath_component_release(pc);
	}
	ppath_release(p);

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(get_idx_key);

ATF_TC_HEAD(get_idx_key, tc)
{
	atf_tc_set_md_var(tc, "descr", "check that ppath_component_idx() "
	    "and ppath_component_key() return -1 and NULL, respectively, if "
	    "the component is a key or an index, respectively.");
}

ATF_TC_BODY(get_idx_key, tc)
{
	ppath_component_t *idx, *key;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((idx = ppath_idx(0)) == NULL)
		atf_tc_fail("ppath_idx failed");
	if ((key = ppath_key("key")) == NULL)
		atf_tc_fail("ppath_idx failed");

	ATF_CHECK_EQ(ppath_component_key(idx), NULL);
	ATF_CHECK_EQ(ppath_component_idx(key), -1);

	ppath_component_release(idx);
	ppath_component_release(key);

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(ppath_copy);

ATF_TC_HEAD(ppath_copy, tc)
{
	atf_tc_set_md_var(tc, "descr", "check that ppath_copy() "
	    "creates an exact replica of a path, and that no "
	    "resources are leaked.");
}

ATF_TC_BODY(ppath_copy, tc)
{
	ppath_component_t *pc, *cpc;
	ppath_t *p, *cp, *rp;
	unsigned int i;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	for (i = 0; i < PPATH_MAX_COMPONENTS; i++) {
		if ((pc = ppath_idx(i)) == NULL)
			atf_tc_fail("ppath_idx failed");
		rp = ppath_push(p, pc);
		ppath_component_release(pc);
		ATF_CHECK_EQ(rp, p);
	}

	if ((cp = ppath_copy(p)) == NULL)
		atf_tc_fail("ppath_copy failed");

	for (i = 0; i < PPATH_MAX_COMPONENTS; i++) {
		pc = ppath_component_at(p, i);
		cpc = ppath_component_at(cp, i);
		ATF_CHECK_EQ(pc, cpc);
		ppath_component_release(pc);
		ppath_component_release(cpc);
	}

	ppath_release(cp);
	ppath_release(p);

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(replace);

ATF_TC_HEAD(replace, tc)
{
	atf_tc_set_md_var(tc, "descr", "check that ppath_replace_idx() "
	    "and ppath_replace_key() produce the paths we expect without "
	    "leaking resources.");
}

ATF_TC_BODY(replace, tc)
{
	ppath_component_t *pc;
	ppath_t *p, *cp, *rp;
	unsigned int i;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	/* index replacement fails on an empty path */
	rp = ppath_replace_idx(p, 0);
	ATF_CHECK_EQ(rp, NULL);

	/* key replacement fails on an empty path */
	rp = ppath_replace_key(p, "key");
	ATF_CHECK_EQ(rp, NULL);

	for (i = 0; i < PPATH_MAX_COMPONENTS; i++) {
		if ((pc = ppath_idx(i)) == NULL)
			atf_tc_fail("ppath_idx failed");
		rp = ppath_push(p, pc);
		ppath_component_release(pc);
		ATF_CHECK_EQ(rp, p);
	}

	if ((cp = ppath_copy(p)) == NULL)
		atf_tc_fail("ppath_copy failed");

	rp = ppath_pop(cp, NULL);
	ATF_CHECK_EQ(rp, cp);
	rp = ppath_push_key(cp, "key");
	ATF_CHECK_EQ(rp, cp);

	ppath_replace_idx(p, 0);

	if ((pc = ppath_component_at(p, PPATH_MAX_COMPONENTS - 1)) == NULL)
		atf_tc_fail("ppath_idx failed");
	ATF_CHECK_EQ(ppath_component_idx(pc), 0);
	ppath_component_release(pc);

	for (i = 0; i < PPATH_MAX_COMPONENTS - 1; i++) {
		if ((pc = ppath_component_at(p, i)) == NULL)
			atf_tc_fail("ppath_idx failed");
		ATF_CHECK_EQ(ppath_component_idx(pc), (int)i);
		ppath_component_release(pc);
	}

	for (i = 0; i < PPATH_MAX_COMPONENTS - 1; i++) {
		if ((pc = ppath_component_at(cp, i)) == NULL)
			atf_tc_fail("ppath_idx failed");
		ATF_CHECK_EQ(ppath_component_idx(pc), (int)i);
		ppath_component_release(pc);
	}

	if ((pc = ppath_component_at(cp, PPATH_MAX_COMPONENTS - 1)) == NULL)
		atf_tc_fail("ppath_idx failed");
	if (ppath_component_key(pc) == NULL ||
	    strcmp(ppath_component_key(pc), "key") != 0)
		atf_tc_fail("last path component expected to be \"key\"");
	ppath_component_release(pc);
	ppath_release(p);
	ppath_release(cp);

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(copyset_object_twice_success);

ATF_TC_HEAD(copyset_object_twice_success, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "check that after back-to-back ppath_copyset_object() calls, "
	    "changing the \"u.s. citizen\" property and the first property "
	    "in \"children\" in the \"John Doe\" record in the "
	    "\"personnel\" property list, the properties are changed "
	    "in the new dictionary and unchanged in the old dictionary");
}

ATF_TC_BODY(copyset_object_twice_success, tc)
{
	const char *s;
	char *oext, *next;
	int rc;
	bool v = false;
	prop_dictionary_t d, od;
	prop_object_t nd = NULL, ond;
	prop_object_t r, or;
	ppath_t *p, *p2, *p3;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");
	od = prop_dictionary_copy(d);

	if (!dictionary_equals(od, d)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(d);
		atf_tc_fail("dictionaries are unequal from the outset, argh! "
		    "original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	if ((p = ppath_create()) == NULL || (p2 = ppath_create()) == NULL ||
	    (p3 = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "u.s. citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	if (ppath_push_key(p2, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p2, "children") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_idx(p2, 0) == NULL)
		atf_tc_fail("ppath_push_idx failed");

	if (ppath_push_key(p3, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, true);

	s = "";
	rc = ppath_get_string(d, p2, &s);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_STREQ(s, "Jane Doe");

	rc = ppath_copyset_bool(d, &nd, p, false);
	ATF_CHECK_EQ(rc, 0);

	rc = ppath_get_object(nd, p3, &r);
	ATF_CHECK_EQ(rc, 0);

	ond = nd;

	rc = ppath_copyset_string(d, &nd, p2, "Martha Doe");
	ATF_CHECK_EQ(rc, 0);

	ATF_CHECK_EQ(nd, ond);

	rc = ppath_get_object(nd, p3, &or);
	ATF_CHECK_EQ(rc, 0);

	ATF_CHECK_EQ(r, or);

	v = true;
	rc = ppath_get_bool(nd, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, false);

	s = "";
	rc = ppath_get_string(nd, p2, &s);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_STREQ(s, "Martha Doe");

	if (!dictionary_equals(od, d)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(d);
		atf_tc_fail("copydel modified original dictionary, "
		    "original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	if (dictionary_equals(od, nd)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(nd);
		atf_tc_fail("copydel made no change to the new "
		    "dictionary, original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	rc = ppath_set_bool(od, p, false);
	ATF_CHECK_EQ(rc, 0);

	rc = ppath_set_string(od, p2, "Martha Doe");
	ATF_CHECK_EQ(rc, 0);

	if (!dictionary_equals(od, nd)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(nd);
		atf_tc_fail("copydel made an out-of-bounds change to the new "
		    "dictionary, original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	ppath_release(p);
	ppath_release(p2);
	ppath_release(p3);
	prop_object_release(d);
	prop_object_release(od);
	prop_object_release(nd);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(copydel_object_twice_success);

ATF_TC_HEAD(copydel_object_twice_success, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "check that after back-to-back ppath_copydel_object() calls, "
	    "removing the \"u.s. citizen\" property and the first property "
	    "in \"children\" from the \"John Doe\" record in the "
	    "\"personnel\" property list, the properties are missing "
	    "from the new dictionary and unchanged in the old dictionary");
}

ATF_TC_BODY(copydel_object_twice_success, tc)
{
	const char *s;
	char *oext, *next;
	int rc;
	bool v = false;
	prop_dictionary_t d, od;
	prop_object_t nd = NULL, ond;
	prop_object_t r, or;
	ppath_t *p, *p2, *p3;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");
	od = prop_dictionary_copy(d);

	if (!dictionary_equals(od, d)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(d);
		atf_tc_fail("dictionaries are unequal from the outset, argh! "
		    "original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	if ((p = ppath_create()) == NULL || (p2 = ppath_create()) == NULL ||
	    (p3 = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "u.s. citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	if (ppath_push_key(p2, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p2, "children") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_idx(p2, 0) == NULL)
		atf_tc_fail("ppath_push_idx failed");

	if (ppath_push_key(p3, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, true);

	s = "";
	rc = ppath_get_string(d, p2, &s);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_STREQ(s, "Jane Doe");

	rc = ppath_copydel_bool(d, &nd, p);
	ATF_CHECK_EQ(rc, 0);

	ond = nd;

	rc = ppath_get_object(nd, p3, &r);
	ATF_CHECK_EQ(rc, 0);

	rc = ppath_copydel_string(d, &nd, p2);
	ATF_CHECK_EQ(rc, 0);

	ATF_CHECK_EQ(nd, ond);

	rc = ppath_get_object(nd, p3, &or);
	ATF_CHECK_EQ(rc, 0);

	ATF_CHECK_EQ(r, or);

	v = true;
	rc = ppath_get_bool(nd, p, &v);
	ATF_CHECK_EQ(rc, ENOENT);
	ATF_CHECK_EQ(v, true);

	if (!dictionary_equals(od, d)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(d);
		atf_tc_fail("copydel modified original dictionary, "
		    "original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	if (dictionary_equals(od, nd)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(nd);
		atf_tc_fail("copydel made no change to the new "
		    "dictionary, original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	rc = ppath_delete_bool(od, p);
	ATF_CHECK_EQ(rc, 0);

	rc = ppath_delete_string(od, p2);
	ATF_CHECK_EQ(rc, 0);

	if (!dictionary_equals(od, nd)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(nd);
		atf_tc_fail("copydel made an out-of-bounds change to the new "
		    "dictionary, original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	ppath_release(p);
	ppath_release(p2);
	ppath_release(p3);
	prop_object_release(d);
	prop_object_release(od);
	prop_object_release(nd);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(copydel_bool_success);

ATF_TC_HEAD(copydel_bool_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_copydel_bool() deletes "
	    "the \"u.s. citizen\" property in the \"John Doe\" record in the "
	    "\"personnel\" property list and verifies the value is missing "
	    "from the new dictionary and unchanged in the old dictionary");
}

ATF_TC_BODY(copydel_bool_success, tc)
{
	char *oext, *next;
	int rc;
	bool v = false;
	prop_dictionary_t d, od;
	prop_object_t nd = NULL;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");
	od = prop_dictionary_copy(d);

	if (!dictionary_equals(od, d)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(d);
		atf_tc_fail("dictionaries are unequal from the outset, argh! "
		    "original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "u.s. citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, true);

	rc = ppath_copydel_bool(d, &nd, p);
	ATF_CHECK_EQ(rc, 0);

	v = true;
	rc = ppath_get_bool(nd, p, &v);
	ATF_CHECK_EQ(rc, ENOENT);
	ATF_CHECK_EQ(v, true);

	if (!dictionary_equals(od, d)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(d);
		atf_tc_fail("copydel modified original dictionary, "
		    "original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	if (dictionary_equals(od, nd)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(nd);
		atf_tc_fail("copydel made no change to the new "
		    "dictionary, original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	rc = ppath_delete_bool(od, p);
	ATF_CHECK_EQ(rc, 0);

	if (!dictionary_equals(od, nd)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(nd);
		atf_tc_fail("copydel made an out-of-bounds change to the new "
		    "dictionary, original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	ppath_release(p);
	prop_object_release(d);
	prop_object_release(od);
	prop_object_release(nd);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(copyset_bool_success);

ATF_TC_HEAD(copyset_bool_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_copyset_bool() sets "
	    "the \"u.s. citizen\" property in the \"John Doe\" record in the "
	    "\"personnel\" property list to false and verifies the new value "
	    "in the new dictionary and that the old dictionary is unchanged");
}

ATF_TC_BODY(copyset_bool_success, tc)
{
	char *oext, *next;
	int rc;
	bool v = false;
	prop_dictionary_t d, od;
	prop_object_t nd = NULL;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");
	od = prop_dictionary_copy(d);

	if (!dictionary_equals(od, d)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(d);
		atf_tc_fail("dictionaries are unequal from the outset, argh! "
		    "original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "u.s. citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, true);

	rc = ppath_copyset_bool(d, &nd, p, false);
	ATF_CHECK_EQ(rc, 0);

	v = true;
	rc = ppath_get_bool(nd, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, false);

	if (!dictionary_equals(od, d)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(d);
		atf_tc_fail("copyset modified original dictionary, "
		    "original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	if (dictionary_equals(od, nd)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(nd);
		atf_tc_fail("copyset made no change to the new "
		    "dictionary, original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	rc = ppath_set_bool(nd, p, true);
	ATF_CHECK_EQ(rc, 0);

	if (!dictionary_equals(od, nd)) {
		oext = prop_dictionary_externalize(od);
		next = prop_dictionary_externalize(nd);
		atf_tc_fail("copyset made an out-of-bounds change to the new "
		    "dictionary, original\n%s\nnew\n%s", oext, next);
		free(oext);
		free(next);
	}

	ppath_release(p);
	prop_object_release(d);
	prop_object_release(od);
	prop_object_release(nd);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(set_bool_eftype);

ATF_TC_HEAD(set_bool_eftype, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_set_bool() does not "
	    "overwrite with a bool "
	    "the \"job title\" property in the \"John Doe\" record in "
	    "the "
	    "\"personnel\" property list");
}

ATF_TC_BODY(set_bool_eftype, tc)
{
	int rc;
	bool v = false;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "job title") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, EFTYPE);
	ATF_CHECK_EQ(v, false);

	rc = ppath_set_bool(d, p, false);
	ATF_CHECK_EQ(rc, EFTYPE);

	v = true;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, EFTYPE);
	ATF_CHECK_EQ(v, true);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(set_bool_enoent);

ATF_TC_HEAD(set_bool_enoent, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_set_bool() does not create "
	    "the \"russian citizen\" property in the \"John Doe\" record in "
	    "the "
	    "\"personnel\" property list");
}

ATF_TC_BODY(set_bool_enoent, tc)
{
	int rc;
	bool v = false;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "russian citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, ENOENT);
	ATF_CHECK_EQ(v, false);

	rc = ppath_set_bool(d, p, false);
	ATF_CHECK_EQ(rc, ENOENT);

	v = true;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, ENOENT);
	ATF_CHECK_EQ(v, true);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(create_bool_eexist);

ATF_TC_HEAD(create_bool_eexist, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_create_bool() returns "
	    "EEXIST because the \"u.s. citizen\" property in the "
	    "\"John Doe\" record in the \"personnel\" property list "
	    "already exists");
}

ATF_TC_BODY(create_bool_eexist, tc)
{
	int rc;
	bool v = false;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "u.s. citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, true);

	rc = ppath_create_bool(d, p, false);
	ATF_CHECK_EQ(rc, EEXIST);

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, true);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(create_bool_success);

ATF_TC_HEAD(create_bool_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_create_bool() creates "
	    "the \"russian citizen\" property in the \"John Doe\" record in "
	    "the \"personnel\" property list and sets it to false");
}

ATF_TC_BODY(create_bool_success, tc)
{
	int rc;
	bool v = false;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "russian citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, ENOENT);
	ATF_CHECK_EQ(v, false);

	rc = ppath_create_bool(d, p, false);
	ATF_CHECK_EQ(rc, 0);

	v = true;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, false);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(set_bool_success);

ATF_TC_HEAD(set_bool_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_set_bool() sets "
	    "the \"u.s. citizen\" property in the \"John Doe\" record in the "
	    "\"personnel\" property list to false and verifies the new value");
}

ATF_TC_BODY(set_bool_success, tc)
{
	int rc;
	bool v = false;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "u.s. citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, true);

	rc = ppath_set_bool(d, p, v);
	ATF_CHECK_EQ(rc, 0);

	v = true;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, true);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(get_bool_success);

ATF_TC_HEAD(get_bool_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_get_bool() fetches "
	    "the \"u.s. citizen\" property from the \"John Doe\" record in the "
	    "\"personnel\" property list, and compares it with the expected "
	    "value, true");
}

ATF_TC_BODY(get_bool_success, tc)
{
	int rc;
	bool v = false;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "u.s. citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_EQ(v, true);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(delete_bool_success);

ATF_TC_HEAD(delete_bool_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_delete_bool() succeeds "
	    "for the path (\"John Doe\", \"u.s. citizen\") in the "
	    "\"personnel\" property list");
}

ATF_TC_BODY(delete_bool_success, tc)
{
	int rc;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "u.s. citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	rc = ppath_delete_bool(d, p);
	ATF_CHECK_EQ(rc, 0);

	rc = ppath_get_bool(d, p, NULL);
	ATF_CHECK_EQ(rc, ENOENT);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(delete_bool_eftype);

ATF_TC_HEAD(delete_bool_eftype, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_delete_bool() returns "
	    "EFTYPE for the path (\"John Doe\", \"job title\") in the "
	    "\"personnel\" property list and does not delete the path");
}

ATF_TC_BODY(delete_bool_eftype, tc)
{
	int rc;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "job title") == NULL)
		atf_tc_fail("ppath_push_key failed");

	rc = ppath_delete_bool(d, p);
	ATF_CHECK_EQ(rc, EFTYPE);

	rc = ppath_get_object(d, p, NULL);
	ATF_CHECK_EQ(rc, 0);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(delete_bool_enoent);

ATF_TC_HEAD(delete_bool_enoent, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_delete_bool() returns "
	    "ENOENT for the path (\"John Doe\", \"citizen\") in the "
	    "\"personnel\" property list");
}

ATF_TC_BODY(delete_bool_enoent, tc)
{
	int rc;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	rc = ppath_delete_bool(d, p);
	ATF_CHECK_EQ(rc, ENOENT);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(get_bool_enoent);

ATF_TC_HEAD(get_bool_enoent, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_get_bool() returns "
	    "ENOENT for the path (\"John Doe\", \"citizen\") in the "
	    "\"personnel\" property list, and the bool * argument is "
	    "unchanged");
}

ATF_TC_BODY(get_bool_enoent, tc)
{
	int rc;
	bool v;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = true;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, ENOENT);
	ATF_CHECK_EQ(v, true);

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, ENOENT);
	ATF_CHECK_EQ(v, false);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(get_bool_eftype);

ATF_TC_HEAD(get_bool_eftype, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_get_bool() returns "
	    "EFTYPE for the path (\"John Doe\", \"job title\") in the "
	    "\"personnel\" property list, and the bool * argument is "
	    "unchanged");
}

ATF_TC_BODY(get_bool_eftype, tc)
{
	int rc;
	bool v;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "job title") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = true;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, EFTYPE);
	ATF_CHECK_EQ(v, true);

	v = false;
	rc = ppath_get_bool(d, p, &v);
	ATF_CHECK_EQ(rc, EFTYPE);
	ATF_CHECK_EQ(v, false);

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(get_string_eftype);

ATF_TC_HEAD(get_string_eftype, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_get_string() returns "
	    "EFTYPE for the path (\"John Doe\", \"u.s. citizen\") in the "
	    "\"personnel\" property list, and the const char ** argument is "
	    "unchanged");
}

ATF_TC_BODY(get_string_eftype, tc)
{
	int rc;
	const char *v;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "u.s. citizen") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = NULL;
	rc = ppath_get_string(d, p, &v);
	ATF_CHECK_EQ(rc, EFTYPE);
	ATF_CHECK_EQ(v, NULL);

	v = "xyz";
	rc = ppath_get_string(d, p, &v);
	ATF_CHECK_EQ(rc, EFTYPE);
	ATF_CHECK_STREQ(v, "xyz");

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(get_string_enoent);

ATF_TC_HEAD(get_string_enoent, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_get_string() returns "
	    "ENOENT for the path (\"John Doe\", \"title\") in the "
	    "\"personnel\" property list, and the const char ** argument is "
	    "unchanged");
}

ATF_TC_BODY(get_string_enoent, tc)
{
	int rc;
	const char *v;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "title") == NULL)
		atf_tc_fail("ppath_push_key failed");

	v = NULL;
	rc = ppath_get_string(d, p, &v);
	ATF_CHECK_EQ(rc, ENOENT);
	ATF_CHECK_EQ(v, NULL);

	v = "xyz";
	rc = ppath_get_string(d, p, &v);
	ATF_CHECK_EQ(rc, ENOENT);
	ATF_CHECK_STREQ(v, "xyz");

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TC(get_string_success);

ATF_TC_HEAD(get_string_success, tc)
{
	atf_tc_set_md_var(tc, "descr", "check ppath_get_string() fetches "
	    "the \"job title\" property from the \"John Doe\" record in the "
	    "\"personnel\" property list and compares it with the expected "
	    "value, \"computer programmer\"");
}

ATF_TC_BODY(get_string_success, tc)
{
	int rc;
	const char *v = NULL;;
	prop_dictionary_t d;
	ppath_t *p;

	assert_no_ppath_extant();
	assert_no_ppath_component_extant();

	if ((d = prop_dictionary_internalize(personnel)) == NULL)
		atf_tc_fail("prop_dictionary_internalize failed");

	if ((p = ppath_create()) == NULL)
		atf_tc_fail("ppath_create failed");

	if (ppath_push_key(p, "John Doe") == NULL)
		atf_tc_fail("ppath_push_key failed");
	if (ppath_push_key(p, "job title") == NULL)
		atf_tc_fail("ppath_push_key failed");

	rc = ppath_get_string(d, p, &v);
	ATF_CHECK_EQ(rc, 0);
	ATF_CHECK_STREQ(v, "computer programmer");

	ppath_release(p);
	prop_object_release(d);
	assert_no_ppath_extant();
	assert_no_ppath_component_extant();
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, push_until_full);
	ATF_TP_ADD_TC(tp, pop_until_empty);
	ATF_TP_ADD_TC(tp, length);
	ATF_TP_ADD_TC(tp, component_at);
	ATF_TP_ADD_TC(tp, get_idx_key);
	ATF_TP_ADD_TC(tp, ppath_copy);
	ATF_TP_ADD_TC(tp, replace);

	ATF_TP_ADD_TC(tp, delete_bool_eftype);
	ATF_TP_ADD_TC(tp, delete_bool_enoent);
	ATF_TP_ADD_TC(tp, delete_bool_success);

	ATF_TP_ADD_TC(tp, get_bool_eftype);
	ATF_TP_ADD_TC(tp, get_bool_enoent);
	ATF_TP_ADD_TC(tp, get_bool_success);

	ATF_TP_ADD_TC(tp, copydel_bool_success);
	ATF_TP_ADD_TC(tp, copydel_object_twice_success);
	ATF_TP_ADD_TC(tp, copyset_object_twice_success);
	ATF_TP_ADD_TC(tp, copyset_bool_success);
	ATF_TP_ADD_TC(tp, create_bool_eexist);
	ATF_TP_ADD_TC(tp, create_bool_success);
	ATF_TP_ADD_TC(tp, set_bool_enoent);
	ATF_TP_ADD_TC(tp, set_bool_eftype);
	ATF_TP_ADD_TC(tp, set_bool_success);

	ATF_TP_ADD_TC(tp, get_string_eftype);
	ATF_TP_ADD_TC(tp, get_string_enoent);
	ATF_TP_ADD_TC(tp, get_string_success);

	return atf_no_error();
}
