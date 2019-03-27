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

#include "pt_last_ip.h"

#include "intel-pt.h"

#include <string.h>


static struct ptunit_result init(void)
{
	struct pt_last_ip last_ip;

	memset(&last_ip, 0xcd, sizeof(last_ip));

	pt_last_ip_init(&last_ip);

	ptu_uint_eq(last_ip.ip, 0ull);
	ptu_uint_eq(last_ip.have_ip, 0);
	ptu_uint_eq(last_ip.suppressed, 0);

	return ptu_passed();
}

static struct ptunit_result init_null(void)
{
	pt_last_ip_init(NULL);

	return ptu_passed();
}

static struct ptunit_result status_initial(void)
{
	struct pt_last_ip last_ip;
	int errcode;

	pt_last_ip_init(&last_ip);

	errcode = pt_last_ip_query(NULL, &last_ip);
	ptu_int_eq(errcode, -pte_noip);

	return ptu_passed();
}

static struct ptunit_result status(void)
{
	struct pt_last_ip last_ip;
	int errcode;

	last_ip.have_ip = 1;
	last_ip.suppressed = 0;

	errcode = pt_last_ip_query(NULL, &last_ip);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result status_null(void)
{
	int errcode;

	errcode = pt_last_ip_query(NULL, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result status_noip(void)
{
	struct pt_last_ip last_ip;
	int errcode;

	last_ip.have_ip = 0;
	last_ip.suppressed = 0;

	errcode = pt_last_ip_query(NULL, &last_ip);
	ptu_int_eq(errcode, -pte_noip);

	return ptu_passed();
}

static struct ptunit_result status_suppressed(void)
{
	struct pt_last_ip last_ip;
	int errcode;

	last_ip.have_ip = 1;
	last_ip.suppressed = 1;

	errcode = pt_last_ip_query(NULL, &last_ip);
	ptu_int_eq(errcode, -pte_ip_suppressed);

	return ptu_passed();
}

static struct ptunit_result query_initial(void)
{
	struct pt_last_ip last_ip;
	uint64_t ip;
	int errcode;

	pt_last_ip_init(&last_ip);

	errcode = pt_last_ip_query(&ip, &last_ip);
	ptu_int_eq(errcode, -pte_noip);

	return ptu_passed();
}

static struct ptunit_result query(void)
{
	struct pt_last_ip last_ip;
	uint64_t ip, exp = 42ull;
	int errcode;

	last_ip.ip = 42ull;
	last_ip.have_ip = 1;
	last_ip.suppressed = 0;

	errcode = pt_last_ip_query(&ip, &last_ip);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(last_ip.ip, exp);

	return ptu_passed();
}

static struct ptunit_result query_null(void)
{
	uint64_t ip = 13ull;
	int errcode;

	errcode = pt_last_ip_query(&ip, NULL);
	ptu_int_eq(errcode, -pte_internal);
	ptu_uint_eq(ip, 13ull);

	return ptu_passed();
}

static struct ptunit_result query_noip(void)
{
	struct pt_last_ip last_ip;
	uint64_t ip = 13ull;
	int errcode;

	last_ip.ip = 42ull;
	last_ip.have_ip = 0;
	last_ip.suppressed = 0;

	errcode = pt_last_ip_query(&ip, &last_ip);
	ptu_int_eq(errcode, -pte_noip);
	ptu_uint_eq(ip, 0ull);

	return ptu_passed();
}

static struct ptunit_result query_suppressed(void)
{
	struct pt_last_ip last_ip;
	uint64_t ip = 13ull;
	int errcode;

	last_ip.ip = 42ull;
	last_ip.have_ip = 1;
	last_ip.suppressed = 1;

	errcode = pt_last_ip_query(&ip, &last_ip);
	ptu_int_eq(errcode, -pte_ip_suppressed);
	ptu_uint_eq(ip, 0ull);

	return ptu_passed();
}

static struct ptunit_result update_ip_suppressed(uint32_t have_ip)
{
	struct pt_last_ip last_ip;
	struct pt_packet_ip packet;
	int errcode;

	last_ip.ip = 42ull;
	last_ip.have_ip = have_ip;
	last_ip.suppressed = 0;

	packet.ipc = pt_ipc_suppressed;
	packet.ip = 13ull;

	errcode = pt_last_ip_update_ip(&last_ip, &packet, NULL);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(last_ip.ip, 42ull);
	ptu_uint_eq(last_ip.have_ip, have_ip);
	ptu_uint_eq(last_ip.suppressed, 1);

	return ptu_passed();
}

static struct ptunit_result update_ip_upd16(uint32_t have_ip)
{
	struct pt_last_ip last_ip;
	struct pt_packet_ip packet;
	int errcode;

	last_ip.ip = 0xff0042ull;
	last_ip.have_ip = have_ip;
	last_ip.suppressed = 0;

	packet.ipc = pt_ipc_update_16;
	packet.ip = 0xccc013ull;

	errcode = pt_last_ip_update_ip(&last_ip, &packet, NULL);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(last_ip.ip, 0xffc013ull);
	ptu_uint_eq(last_ip.have_ip, 1);
	ptu_uint_eq(last_ip.suppressed, 0);

	return ptu_passed();
}

static struct ptunit_result update_ip_upd32(uint32_t have_ip)
{
	struct pt_last_ip last_ip;
	struct pt_packet_ip packet;
	int errcode;

	last_ip.ip = 0xff00000420ull;
	last_ip.have_ip = have_ip;
	last_ip.suppressed = 0;

	packet.ipc = pt_ipc_update_32;
	packet.ip = 0xcc0000c013ull;

	errcode = pt_last_ip_update_ip(&last_ip, &packet, NULL);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(last_ip.ip, 0xff0000c013ull);
	ptu_uint_eq(last_ip.have_ip, 1);
	ptu_uint_eq(last_ip.suppressed, 0);

	return ptu_passed();
}

static struct ptunit_result update_ip_sext48(uint32_t have_ip)
{
	struct pt_last_ip last_ip;
	struct pt_packet_ip packet;
	int errcode;

	last_ip.ip = 0x7fffffffffffffffull;
	last_ip.have_ip = have_ip;
	last_ip.suppressed = 0;

	packet.ipc = pt_ipc_sext_48;
	packet.ip = 0xff00000000ffull;

	errcode = pt_last_ip_update_ip(&last_ip, &packet, NULL);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(last_ip.ip, 0xffffff00000000ffull);
	ptu_uint_eq(last_ip.have_ip, 1);
	ptu_uint_eq(last_ip.suppressed, 0);

	return ptu_passed();
}

static struct ptunit_result update_ip_bad_packet(uint32_t have_ip)
{
	struct pt_last_ip last_ip;
	struct pt_packet_ip packet;
	int errcode;

	last_ip.ip = 0x7fffffffffffffffull;
	last_ip.have_ip = have_ip;
	last_ip.suppressed = 0;

	packet.ipc = (enum pt_ip_compression) 0xff;
	packet.ip = 0ull;

	errcode = pt_last_ip_update_ip(&last_ip, &packet, NULL);
	ptu_int_eq(errcode, -pte_bad_packet);
	ptu_uint_eq(last_ip.ip, 0x7fffffffffffffffull);
	ptu_uint_eq(last_ip.have_ip, have_ip);
	ptu_uint_eq(last_ip.suppressed, 0);

	return ptu_passed();
}

static struct ptunit_result update_ip_null_ip(void)
{
	struct pt_packet_ip packet;
	int errcode;

	errcode = pt_last_ip_update_ip(NULL, &packet, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result update_ip_null_packet(uint32_t have_ip)
{
	struct pt_last_ip last_ip;
	int errcode;

	last_ip.ip = 0x7fffffffffffffffull;
	last_ip.have_ip = have_ip;
	last_ip.suppressed = 0;

	errcode = pt_last_ip_update_ip(&last_ip, NULL, NULL);
	ptu_int_eq(errcode, -pte_internal);
	ptu_uint_eq(last_ip.ip, 0x7fffffffffffffffull);
	ptu_uint_eq(last_ip.have_ip, have_ip);
	ptu_uint_eq(last_ip.suppressed, 0);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, init);
	ptu_run(suite, init_null);
	ptu_run(suite, status_initial);
	ptu_run(suite, status);
	ptu_run(suite, status_null);
	ptu_run(suite, status_noip);
	ptu_run(suite, status_suppressed);
	ptu_run(suite, query_initial);
	ptu_run(suite, query);
	ptu_run(suite, query_null);
	ptu_run(suite, query_noip);
	ptu_run(suite, query_suppressed);
	ptu_run_p(suite, update_ip_suppressed, 0);
	ptu_run_p(suite, update_ip_suppressed, 1);
	ptu_run_p(suite, update_ip_upd16, 0);
	ptu_run_p(suite, update_ip_upd16, 1);
	ptu_run_p(suite, update_ip_upd32, 0);
	ptu_run_p(suite, update_ip_upd32, 1);
	ptu_run_p(suite, update_ip_sext48, 0);
	ptu_run_p(suite, update_ip_sext48, 1);
	ptu_run_p(suite, update_ip_bad_packet, 0);
	ptu_run_p(suite, update_ip_bad_packet, 1);
	ptu_run(suite, update_ip_null_ip);
	ptu_run_p(suite, update_ip_null_packet, 0);
	ptu_run_p(suite, update_ip_null_packet, 1);

	return ptunit_report(&suite);
}
