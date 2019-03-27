/*
 * Copyright (c) 2014-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "ptunit.h"

#include "pt_asid.h"

#include "intel-pt.h"

#include <stddef.h>


static struct ptunit_result from_user_null(void)
{
	struct pt_asid user;
	int errcode;

	pt_asid_init(&user);

	errcode = pt_asid_from_user(NULL, NULL);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_asid_from_user(NULL, &user);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result from_user_default(void)
{
	struct pt_asid asid;
	int errcode;

	errcode = pt_asid_from_user(&asid, NULL);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(asid.size, sizeof(asid));
	ptu_uint_eq(asid.cr3, pt_asid_no_cr3);
	ptu_uint_eq(asid.vmcs, pt_asid_no_vmcs);

	return ptu_passed();
}

static struct ptunit_result from_user_small(void)
{
	struct pt_asid asid, user;
	int errcode;

	user.size = sizeof(user.size);

	errcode = pt_asid_from_user(&asid, &user);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(asid.size, sizeof(asid));
	ptu_uint_eq(asid.cr3, pt_asid_no_cr3);
	ptu_uint_eq(asid.vmcs, pt_asid_no_vmcs);

	return ptu_passed();
}

static struct ptunit_result from_user_big(void)
{
	struct pt_asid asid, user;
	int errcode;

	user.size = sizeof(user) + 4;
	user.cr3 = 0x4200ull;
	user.vmcs = 0x23000ull;

	errcode = pt_asid_from_user(&asid, &user);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(asid.size, sizeof(asid));
	ptu_uint_eq(asid.cr3, 0x4200ull);
	ptu_uint_eq(asid.vmcs, 0x23000ull);

	return ptu_passed();
}

static struct ptunit_result from_user(void)
{
	struct pt_asid asid, user;
	int errcode;

	user.size = sizeof(user);
	user.cr3 = 0x4200ull;
	user.vmcs = 0x23000ull;

	errcode = pt_asid_from_user(&asid, &user);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(asid.size, sizeof(asid));
	ptu_uint_eq(asid.cr3, 0x4200ull);
	ptu_uint_eq(asid.vmcs, 0x23000ull);

	return ptu_passed();
}

static struct ptunit_result from_user_cr3(void)
{
	struct pt_asid asid, user;
	int errcode;

	user.size = offsetof(struct pt_asid, vmcs);
	user.cr3 = 0x4200ull;
	user.vmcs = 0x23000ull;

	errcode = pt_asid_from_user(&asid, &user);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(asid.size, sizeof(asid));
	ptu_uint_eq(asid.cr3, 0x4200ull);
	ptu_uint_eq(asid.vmcs, pt_asid_no_vmcs);

	return ptu_passed();
}

static struct ptunit_result to_user_null(void)
{
	struct pt_asid asid;
	int errcode;

	pt_asid_init(&asid);

	errcode = pt_asid_to_user(NULL, NULL, sizeof(asid));
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_asid_to_user(NULL, &asid, sizeof(asid));
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result to_user_too_small(void)
{
	struct pt_asid asid, user;
	int errcode;

	pt_asid_init(&asid);

	errcode = pt_asid_to_user(&user, &asid, 0);
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_asid_to_user(&user, &asid, sizeof(user.size) - 1);
	ptu_int_eq(errcode, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result to_user_small(void)
{
	struct pt_asid asid, user;
	int errcode;

	memset(&user, 0xcc, sizeof(user));
	pt_asid_init(&asid);

	errcode = pt_asid_to_user(&user, &asid, sizeof(user.size));
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(user.size, sizeof(user.size));
	ptu_uint_eq(user.cr3, 0xccccccccccccccccull);
	ptu_uint_eq(user.vmcs, 0xccccccccccccccccull);

	return ptu_passed();
}

static struct ptunit_result to_user_big(void)
{
	struct pt_asid asid, user;
	int errcode;

	memset(&user, 0xcc, sizeof(user));
	pt_asid_init(&asid);
	asid.cr3 = 0x4200ull;
	asid.vmcs = 0x23000ull;

	errcode = pt_asid_to_user(&user, &asid, sizeof(user) + 8);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(user.size, sizeof(asid));
	ptu_uint_eq(user.cr3, 0x4200ull);
	ptu_uint_eq(user.vmcs, 0x23000ull);

	return ptu_passed();
}

static struct ptunit_result to_user(void)
{
	struct pt_asid asid, user;
	int errcode;

	memset(&user, 0xcc, sizeof(user));
	pt_asid_init(&asid);
	asid.cr3 = 0x4200ull;
	asid.vmcs = 0x23000ull;

	errcode = pt_asid_to_user(&user, &asid, sizeof(user));
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(user.size, sizeof(asid));
	ptu_uint_eq(user.cr3, 0x4200ull);
	ptu_uint_eq(user.vmcs, 0x23000ull);

	return ptu_passed();
}

static struct ptunit_result to_user_cr3(void)
{
	struct pt_asid asid, user;
	int errcode;

	memset(&user, 0xcc, sizeof(user));
	pt_asid_init(&asid);
	asid.cr3 = 0x4200ull;

	errcode = pt_asid_to_user(&user, &asid, offsetof(struct pt_asid, vmcs));
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(user.size, offsetof(struct pt_asid, vmcs));
	ptu_uint_eq(user.cr3, 0x4200ull);
	ptu_uint_eq(user.vmcs, 0xccccccccccccccccull);

	return ptu_passed();
}

static struct ptunit_result match_null(void)
{
	struct pt_asid asid;
	int errcode;

	pt_asid_init(&asid);

	errcode = pt_asid_match(NULL, NULL);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_asid_match(NULL, &asid);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_asid_match(&asid, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result match_default(void)
{
	struct pt_asid lhs, rhs;
	int errcode;

	pt_asid_init(&lhs);
	pt_asid_init(&rhs);

	errcode = pt_asid_match(&lhs, &rhs);
	ptu_int_eq(errcode, 1);

	lhs.cr3 = 0x2300ull;
	lhs.vmcs = 0x42000ull;

	errcode = pt_asid_match(&lhs, &rhs);
	ptu_int_eq(errcode, 1);

	errcode = pt_asid_match(&rhs, &lhs);
	ptu_int_eq(errcode, 1);

	return ptu_passed();
}

static struct ptunit_result match_default_mixed(void)
{
	struct pt_asid lhs, rhs;
	int errcode;

	pt_asid_init(&lhs);
	pt_asid_init(&rhs);

	errcode = pt_asid_match(&lhs, &rhs);
	ptu_int_eq(errcode, 1);

	lhs.cr3 = 0x2300ull;
	rhs.vmcs = 0x42000ull;

	errcode = pt_asid_match(&lhs, &rhs);
	ptu_int_eq(errcode, 1);

	errcode = pt_asid_match(&rhs, &lhs);
	ptu_int_eq(errcode, 1);

	return ptu_passed();
}

static struct ptunit_result match_cr3(void)
{
	struct pt_asid lhs, rhs;
	int errcode;

	pt_asid_init(&lhs);
	pt_asid_init(&rhs);

	lhs.cr3 = 0x2300ull;
	rhs.cr3 = 0x2300ull;

	errcode = pt_asid_match(&lhs, &rhs);
	ptu_int_eq(errcode, 1);

	return ptu_passed();
}

static struct ptunit_result match_vmcs(void)
{
	struct pt_asid lhs, rhs;
	int errcode;

	pt_asid_init(&lhs);
	pt_asid_init(&rhs);

	lhs.vmcs = 0x23000ull;
	rhs.vmcs = 0x23000ull;

	errcode = pt_asid_match(&lhs, &rhs);
	ptu_int_eq(errcode, 1);

	return ptu_passed();
}

static struct ptunit_result match(void)
{
	struct pt_asid lhs, rhs;
	int errcode;

	pt_asid_init(&lhs);
	pt_asid_init(&rhs);

	lhs.cr3 = 0x2300ull;
	rhs.cr3 = 0x2300ull;
	lhs.vmcs = 0x23000ull;
	rhs.vmcs = 0x23000ull;

	errcode = pt_asid_match(&lhs, &rhs);
	ptu_int_eq(errcode, 1);

	return ptu_passed();
}

static struct ptunit_result match_cr3_false(void)
{
	struct pt_asid lhs, rhs;
	int errcode;

	pt_asid_init(&lhs);
	pt_asid_init(&rhs);

	lhs.cr3 = 0x4200ull;
	rhs.cr3 = 0x2300ull;

	errcode = pt_asid_match(&lhs, &rhs);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result match_vmcs_false(void)
{
	struct pt_asid lhs, rhs;
	int errcode;

	pt_asid_init(&lhs);
	pt_asid_init(&rhs);

	lhs.vmcs = 0x42000ull;
	rhs.vmcs = 0x23000ull;

	errcode = pt_asid_match(&lhs, &rhs);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, from_user_null);
	ptu_run(suite, from_user_default);
	ptu_run(suite, from_user_small);
	ptu_run(suite, from_user_big);
	ptu_run(suite, from_user);
	ptu_run(suite, from_user_cr3);

	ptu_run(suite, to_user_null);
	ptu_run(suite, to_user_too_small);
	ptu_run(suite, to_user_small);
	ptu_run(suite, to_user_big);
	ptu_run(suite, to_user);
	ptu_run(suite, to_user_cr3);

	ptu_run(suite, match_null);
	ptu_run(suite, match_default);
	ptu_run(suite, match_default_mixed);
	ptu_run(suite, match_cr3);
	ptu_run(suite, match_vmcs);
	ptu_run(suite, match);
	ptu_run(suite, match_cr3_false);
	ptu_run(suite, match_vmcs_false);

	return ptunit_report(&suite);
}
