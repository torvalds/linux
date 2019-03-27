/*
 * Copyright (c) 2013-2018, Intel Corporation
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

#include "pt_retstack.h"

#include "intel-pt.h"


static struct ptunit_result init(void)
{
	struct pt_retstack retstack;
	int status;

	memset(&retstack, 0xcd, sizeof(retstack));

	pt_retstack_init(&retstack);

	status = pt_retstack_is_empty(&retstack);
	ptu_int_ne(status, 0);

	return ptu_passed();
}

static struct ptunit_result init_null(void)
{
	pt_retstack_init(NULL);

	return ptu_passed();
}

static struct ptunit_result query(void)
{
	struct pt_retstack retstack;
	uint64_t ip;
	int status;

	pt_retstack_init(&retstack);

	status = pt_retstack_push(&retstack, 0x42ull);
	ptu_int_eq(status, 0);

	status = pt_retstack_is_empty(&retstack);
	ptu_int_eq(status, 0);

	status = pt_retstack_pop(&retstack, &ip);
	ptu_int_eq(status, 0);
	ptu_uint_eq(ip, 0x42ull);

	status = pt_retstack_is_empty(&retstack);
	ptu_int_ne(status, 0);

	return ptu_passed();
}

static struct ptunit_result query_empty(void)
{
	struct pt_retstack retstack;
	uint64_t ip;
	int status;

	pt_retstack_init(&retstack);

	ip = 0x42ull;
	status = pt_retstack_pop(&retstack, &ip);
	ptu_int_eq(status, -pte_retstack_empty);
	ptu_uint_eq(ip, 0x42ull);

	return ptu_passed();
}

static struct ptunit_result query_null(void)
{
	uint64_t ip;
	int status;

	ip = 0x42ull;
	status = pt_retstack_pop(NULL, &ip);
	ptu_int_eq(status, -pte_invalid);
	ptu_uint_eq(ip, 0x42ull);

	return ptu_passed();
}

static struct ptunit_result pop(void)
{
	struct pt_retstack retstack;
	int status;

	pt_retstack_init(&retstack);

	status = pt_retstack_push(&retstack, 0x42ull);
	ptu_int_eq(status, 0);

	status = pt_retstack_is_empty(&retstack);
	ptu_int_eq(status, 0);

	status = pt_retstack_pop(&retstack, NULL);
	ptu_int_eq(status, 0);

	status = pt_retstack_is_empty(&retstack);
	ptu_int_ne(status, 0);

	return ptu_passed();
}

static struct ptunit_result pop_empty(void)
{
	struct pt_retstack retstack;
	int status;

	pt_retstack_init(&retstack);

	status = pt_retstack_pop(&retstack, NULL);
	ptu_int_eq(status, -pte_retstack_empty);

	return ptu_passed();
}

static struct ptunit_result pop_null(void)
{
	int status;

	status = pt_retstack_pop(NULL, NULL);
	ptu_int_eq(status, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result full(void)
{
	struct pt_retstack retstack;
	uint64_t ip, idx;
	int status;

	pt_retstack_init(&retstack);

	for (idx = 0; idx < pt_retstack_size; ++idx) {
		status = pt_retstack_push(&retstack, idx);
		ptu_int_eq(status, 0);
	}

	status = pt_retstack_is_empty(&retstack);
	ptu_int_eq(status, 0);

	for (idx = pt_retstack_size; idx > 0;) {
		idx -= 1;

		status = pt_retstack_pop(&retstack, &ip);
		ptu_int_eq(status, 0);
		ptu_uint_eq(ip, idx);
	}

	status = pt_retstack_is_empty(&retstack);
	ptu_int_ne(status, 0);

	return ptu_passed();
}

static struct ptunit_result overflow(void)
{
	struct pt_retstack retstack;
	uint64_t ip, idx;
	int status;

	pt_retstack_init(&retstack);

	for (idx = 0; idx <= pt_retstack_size; ++idx) {
		status = pt_retstack_push(&retstack, idx);
		ptu_int_eq(status, 0);
	}

	status = pt_retstack_is_empty(&retstack);
	ptu_int_eq(status, 0);

	for (idx = pt_retstack_size; idx > 0; --idx) {
		status = pt_retstack_pop(&retstack, &ip);
		ptu_int_eq(status, 0);
		ptu_uint_eq(ip, idx);
	}

	status = pt_retstack_is_empty(&retstack);
	ptu_int_ne(status, 0);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, init);
	ptu_run(suite, init_null);
	ptu_run(suite, query);
	ptu_run(suite, query_empty);
	ptu_run(suite, query_null);
	ptu_run(suite, pop);
	ptu_run(suite, pop_empty);
	ptu_run(suite, pop_null);
	ptu_run(suite, full);
	ptu_run(suite, overflow);

	return ptunit_report(&suite);
}
