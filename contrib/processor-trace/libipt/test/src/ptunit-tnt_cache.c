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

#include "pt_tnt_cache.h"

#include "intel-pt.h"

#include <string.h>


static struct ptunit_result init(void)
{
	struct pt_tnt_cache tnt_cache;

	memset(&tnt_cache, 0xcd, sizeof(tnt_cache));

	pt_tnt_cache_init(&tnt_cache);

	ptu_uint_eq(tnt_cache.tnt, 0ull);
	ptu_uint_eq(tnt_cache.index, 0ull);

	return ptu_passed();
}

static struct ptunit_result init_null(void)
{
	pt_tnt_cache_init(NULL);

	return ptu_passed();
}

static struct ptunit_result is_empty_initial(void)
{
	struct pt_tnt_cache tnt_cache;
	int status;

	pt_tnt_cache_init(&tnt_cache);

	status = pt_tnt_cache_is_empty(&tnt_cache);
	ptu_int_eq(status, 1);

	return ptu_passed();
}

static struct ptunit_result is_empty_no(void)
{
	struct pt_tnt_cache tnt_cache;
	int status;

	tnt_cache.index = 1ull;

	status = pt_tnt_cache_is_empty(&tnt_cache);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result is_empty_yes(void)
{
	struct pt_tnt_cache tnt_cache;
	int status;

	tnt_cache.index = 0ull;

	status = pt_tnt_cache_is_empty(&tnt_cache);
	ptu_int_eq(status, 1);

	return ptu_passed();
}

static struct ptunit_result is_empty_null(void)
{
	int status;

	status = pt_tnt_cache_is_empty(NULL);
	ptu_int_eq(status, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result query_taken(void)
{
	struct pt_tnt_cache tnt_cache;
	int status;

	tnt_cache.tnt = 1ull;
	tnt_cache.index = 1ull;

	status = pt_tnt_cache_query(&tnt_cache);
	ptu_int_eq(status, 1);
	ptu_uint_eq(tnt_cache.index, 0);

	return ptu_passed();
}

static struct ptunit_result query_not_taken(void)
{
	struct pt_tnt_cache tnt_cache;
	int status;

	tnt_cache.tnt = 0ull;
	tnt_cache.index = 1ull;

	status = pt_tnt_cache_query(&tnt_cache);
	ptu_int_eq(status, 0);
	ptu_uint_eq(tnt_cache.index, 0);

	return ptu_passed();
}

static struct ptunit_result query_empty(void)
{
	struct pt_tnt_cache tnt_cache;
	int status;

	tnt_cache.index = 0ull;

	status = pt_tnt_cache_query(&tnt_cache);
	ptu_int_eq(status, -pte_bad_query);

	return ptu_passed();
}

static struct ptunit_result query_null(void)
{
	int status;

	status = pt_tnt_cache_query(NULL);
	ptu_int_eq(status, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result update_tnt(void)
{
	struct pt_tnt_cache tnt_cache;
	struct pt_packet_tnt packet;
	int errcode;

	pt_tnt_cache_init(&tnt_cache);

	packet.bit_size = 4ull;
	packet.payload = 8ull;

	errcode = pt_tnt_cache_update_tnt(&tnt_cache, &packet, NULL);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(tnt_cache.tnt, 8ull);
	ptu_uint_eq(tnt_cache.index, 1ull << 3);

	return ptu_passed();
}

static struct ptunit_result update_tnt_not_empty(void)
{
	struct pt_tnt_cache tnt_cache;
	struct pt_packet_tnt packet;
	int errcode;

	tnt_cache.tnt = 42ull;
	tnt_cache.index = 12ull;

	errcode = pt_tnt_cache_update_tnt(&tnt_cache, &packet, NULL);
	ptu_int_eq(errcode, -pte_bad_context);
	ptu_uint_eq(tnt_cache.tnt, 42ull);
	ptu_uint_eq(tnt_cache.index, 12ull);

	return ptu_passed();
}

static struct ptunit_result update_tnt_null_tnt(void)
{
	struct pt_packet_tnt packet;
	int errcode;

	errcode = pt_tnt_cache_update_tnt(NULL, &packet, NULL);
	ptu_int_eq(errcode, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result update_tnt_null_packet(void)
{
	struct pt_tnt_cache tnt_cache;
	int errcode;

	tnt_cache.tnt = 42ull;
	tnt_cache.index = 12ull;

	errcode = pt_tnt_cache_update_tnt(&tnt_cache, NULL, NULL);
	ptu_int_eq(errcode, -pte_invalid);
	ptu_uint_eq(tnt_cache.tnt, 42ull);
	ptu_uint_eq(tnt_cache.index, 12ull);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, init);
	ptu_run(suite, init_null);
	ptu_run(suite, is_empty_initial);
	ptu_run(suite, is_empty_no);
	ptu_run(suite, is_empty_yes);
	ptu_run(suite, is_empty_null);
	ptu_run(suite, query_taken);
	ptu_run(suite, query_not_taken);
	ptu_run(suite, query_empty);
	ptu_run(suite, query_null);
	ptu_run(suite, update_tnt);
	ptu_run(suite, update_tnt_not_empty);
	ptu_run(suite, update_tnt_null_tnt);
	ptu_run(suite, update_tnt_null_packet);

	return ptunit_report(&suite);
}
