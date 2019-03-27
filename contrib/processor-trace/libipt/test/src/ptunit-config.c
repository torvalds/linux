/*
 * Copyright (c) 2015-2018, Intel Corporation
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

#include "pt_config.h"
#include "pt_opcodes.h"

#include "intel-pt.h"

#include <stddef.h>


/* A global fake buffer to pacify static analyzers. */
static uint8_t buffer[8];

static struct ptunit_result from_user_null(void)
{
	struct pt_config config;
	int errcode;

	errcode = pt_config_from_user(NULL, &config);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_config_from_user(&config, NULL);
	ptu_int_eq(errcode, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result from_user_too_small(void)
{
	struct pt_config config, user;
	int errcode;

	user.size = sizeof(config.size);

	errcode = pt_config_from_user(&config, &user);
	ptu_int_eq(errcode, -pte_bad_config);

	return ptu_passed();
}

static struct ptunit_result from_user_bad_buffer(void)
{
	struct pt_config config, user;
	int errcode;

	pt_config_init(&user);

	errcode = pt_config_from_user(&config, &user);
	ptu_int_eq(errcode, -pte_bad_config);

	user.begin = buffer;

	errcode = pt_config_from_user(&config, &user);
	ptu_int_eq(errcode, -pte_bad_config);

	user.begin = NULL;
	user.end = buffer;

	errcode = pt_config_from_user(&config, &user);
	ptu_int_eq(errcode, -pte_bad_config);

	user.begin = &buffer[1];
	user.end = buffer;

	errcode = pt_config_from_user(&config, &user);
	ptu_int_eq(errcode, -pte_bad_config);

	return ptu_passed();
}

static struct ptunit_result from_user(void)
{
	struct pt_config config, user;
	int errcode;

	user.size = sizeof(user);
	user.begin = buffer;
	user.end = &buffer[sizeof(buffer)];
	user.cpu.vendor = pcv_intel;
	user.errata.bdm70 = 1;

	errcode = pt_config_from_user(&config, &user);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(config.size, sizeof(config));
	ptu_ptr_eq(config.begin, buffer);
	ptu_ptr_eq(config.end, &buffer[sizeof(buffer)]);
	ptu_int_eq(config.cpu.vendor, pcv_intel);
	ptu_uint_eq(config.errata.bdm70, 1);

	return ptu_passed();
}

static struct ptunit_result from_user_small(void)
{
	struct pt_config config, user;
	int errcode;

	memset(&config, 0xcd, sizeof(config));

	user.size = offsetof(struct pt_config, cpu);
	user.begin = buffer;
	user.end = &buffer[sizeof(buffer)];

	errcode = pt_config_from_user(&config, &user);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(config.size, offsetof(struct pt_config, cpu));
	ptu_ptr_eq(config.begin, buffer);
	ptu_ptr_eq(config.end, &buffer[sizeof(buffer)]);
	ptu_int_eq(config.cpu.vendor, pcv_unknown);
	ptu_uint_eq(config.errata.bdm70, 0);

	return ptu_passed();
}

static struct ptunit_result from_user_big(void)
{
	struct pt_config config, user;
	int errcode;

	user.size = sizeof(user) + 4;
	user.begin = buffer;
	user.end = &buffer[sizeof(buffer)];
	user.cpu.vendor = pcv_intel;
	user.errata.bdm70 = 1;

	errcode = pt_config_from_user(&config, &user);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(config.size, sizeof(config));
	ptu_ptr_eq(config.begin, buffer);
	ptu_ptr_eq(config.end, &buffer[sizeof(buffer)]);
	ptu_int_eq(config.cpu.vendor, pcv_intel);
	ptu_uint_eq(config.errata.bdm70, 1);

	return ptu_passed();
}

static struct ptunit_result size(void)
{
	ptu_uint_eq(sizeof(struct pt_errata), 16 * 4);

	return ptu_passed();
}

static struct ptunit_result addr_filter_size(void)
{
	struct pt_conf_addr_filter conf;

	ptu_uint_eq(sizeof(conf.config), 8);

	return ptu_passed();
}

static struct ptunit_result addr_filter_none(void)
{
	struct pt_config config;
	uint8_t filter;

	pt_config_init(&config);

	ptu_uint_eq(config.addr_filter.config.addr_cfg, 0ull);

	for (filter = 0; filter < 4; ++filter) {
		uint32_t addr_cfg;

		addr_cfg = pt_filter_addr_cfg(&config.addr_filter, filter);

		ptu_uint_eq(addr_cfg, pt_addr_cfg_disabled);
	}

	return ptu_passed();
}

static struct ptunit_result addr_filter_0(void)
{
	struct pt_config config;
	uint64_t addr_a, addr_b;
	uint32_t addr_cfg;
	uint8_t filter;

	pt_config_init(&config);
	config.addr_filter.config.ctl.addr0_cfg = pt_addr_cfg_filter;
	config.addr_filter.addr0_a = 0xa000ull;
	config.addr_filter.addr0_b = 0xb000ull;

	ptu_uint_ne(config.addr_filter.config.addr_cfg, 0ull);

	addr_cfg = pt_filter_addr_cfg(&config.addr_filter, 0);
	ptu_uint_eq(addr_cfg, pt_addr_cfg_filter);

	addr_a = pt_filter_addr_a(&config.addr_filter, 0);
	ptu_uint_eq(addr_a, 0xa000ull);

	addr_b = pt_filter_addr_b(&config.addr_filter, 0);
	ptu_uint_eq(addr_b, 0xb000ull);

	for (filter = 1; filter < 4; ++filter) {

		addr_cfg = pt_filter_addr_cfg(&config.addr_filter, filter);

		ptu_uint_eq(addr_cfg, pt_addr_cfg_disabled);
	}

	return ptu_passed();
}

static struct ptunit_result addr_filter_1_3(void)
{
	struct pt_config config;
	uint64_t addr_a, addr_b;
	uint32_t addr_cfg;

	pt_config_init(&config);
	config.addr_filter.config.ctl.addr1_cfg = pt_addr_cfg_filter;
	config.addr_filter.addr1_a = 0xa000ull;
	config.addr_filter.addr1_b = 0xb000ull;
	config.addr_filter.config.ctl.addr3_cfg = pt_addr_cfg_stop;
	config.addr_filter.addr3_a = 0x100a000ull;
	config.addr_filter.addr3_b = 0x100b000ull;

	ptu_uint_ne(config.addr_filter.config.addr_cfg, 0ull);

	addr_cfg = pt_filter_addr_cfg(&config.addr_filter, 0);
	ptu_uint_eq(addr_cfg, pt_addr_cfg_disabled);

	addr_cfg = pt_filter_addr_cfg(&config.addr_filter, 1);
	ptu_uint_eq(addr_cfg, pt_addr_cfg_filter);

	addr_a = pt_filter_addr_a(&config.addr_filter, 1);
	ptu_uint_eq(addr_a, 0xa000ull);

	addr_b = pt_filter_addr_b(&config.addr_filter, 1);
	ptu_uint_eq(addr_b, 0xb000ull);

	addr_cfg = pt_filter_addr_cfg(&config.addr_filter, 2);
	ptu_uint_eq(addr_cfg, pt_addr_cfg_disabled);

	addr_cfg = pt_filter_addr_cfg(&config.addr_filter, 3);
	ptu_uint_eq(addr_cfg, pt_addr_cfg_stop);

	addr_a = pt_filter_addr_a(&config.addr_filter, 3);
	ptu_uint_eq(addr_a, 0x100a000ull);

	addr_b = pt_filter_addr_b(&config.addr_filter, 3);
	ptu_uint_eq(addr_b, 0x100b000ull);

	return ptu_passed();
}

static struct ptunit_result addr_filter_oob(uint8_t filter)
{
	struct pt_config config;
	uint64_t addr_a, addr_b;
	uint32_t addr_cfg;

	pt_config_init(&config);

	memset(&config.addr_filter, 0xcc, sizeof(config.addr_filter));

	addr_cfg = pt_filter_addr_cfg(&config.addr_filter, filter);
	ptu_uint_eq(addr_cfg, pt_addr_cfg_disabled);

	addr_a = pt_filter_addr_a(&config.addr_filter, filter);
	ptu_uint_eq(addr_a, 0ull);

	addr_b = pt_filter_addr_b(&config.addr_filter, filter);
	ptu_uint_eq(addr_b, 0ull);

	return ptu_passed();
}

static struct ptunit_result addr_filter_ip_in(void)
{
	struct pt_config config;
	int status;

	pt_config_init(&config);
	config.addr_filter.config.ctl.addr1_cfg = pt_addr_cfg_filter;
	config.addr_filter.addr1_a = 0xa000;
	config.addr_filter.addr1_b = 0xb000;
	config.addr_filter.config.ctl.addr3_cfg = pt_addr_cfg_filter;
	config.addr_filter.addr3_a = 0x10a000;
	config.addr_filter.addr3_b = 0x10b000;

	status = pt_filter_addr_check(&config.addr_filter, 0xa000);
	ptu_int_eq(status, 1);

	status = pt_filter_addr_check(&config.addr_filter, 0xaf00);
	ptu_int_eq(status, 1);

	status = pt_filter_addr_check(&config.addr_filter, 0xb000);
	ptu_int_eq(status, 1);

	status = pt_filter_addr_check(&config.addr_filter, 0x10a000);
	ptu_int_eq(status, 1);

	status = pt_filter_addr_check(&config.addr_filter, 0x10af00);
	ptu_int_eq(status, 1);

	status = pt_filter_addr_check(&config.addr_filter, 0x10b000);
	ptu_int_eq(status, 1);

	return ptu_passed();
}

static struct ptunit_result addr_filter_ip_out(void)
{
	struct pt_config config;
	int status;

	pt_config_init(&config);
	config.addr_filter.config.ctl.addr1_cfg = pt_addr_cfg_filter;
	config.addr_filter.addr1_a = 0xa000;
	config.addr_filter.addr1_b = 0xb000;
	config.addr_filter.config.ctl.addr3_cfg = pt_addr_cfg_filter;
	config.addr_filter.addr3_a = 0x10a000;
	config.addr_filter.addr3_b = 0x10b000;

	status = pt_filter_addr_check(&config.addr_filter, 0xfff);
	ptu_int_eq(status, 0);

	status = pt_filter_addr_check(&config.addr_filter, 0xb001);
	ptu_int_eq(status, 0);

	status = pt_filter_addr_check(&config.addr_filter, 0x100fff);
	ptu_int_eq(status, 0);

	status = pt_filter_addr_check(&config.addr_filter, 0x10b001);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result addr_filter_stop_in(void)
{
	struct pt_config config;
	int status;

	pt_config_init(&config);
	config.addr_filter.config.ctl.addr1_cfg = pt_addr_cfg_stop;
	config.addr_filter.addr1_a = 0xa000;
	config.addr_filter.addr1_b = 0xb000;
	config.addr_filter.config.ctl.addr3_cfg = pt_addr_cfg_stop;
	config.addr_filter.addr3_a = 0x10a000;
	config.addr_filter.addr3_b = 0x10b000;

	status = pt_filter_addr_check(&config.addr_filter, 0xa000);
	ptu_int_eq(status, 0);

	status = pt_filter_addr_check(&config.addr_filter, 0xaf00);
	ptu_int_eq(status, 0);

	status = pt_filter_addr_check(&config.addr_filter, 0xb000);
	ptu_int_eq(status, 0);

	status = pt_filter_addr_check(&config.addr_filter, 0x10a000);
	ptu_int_eq(status, 0);

	status = pt_filter_addr_check(&config.addr_filter, 0x10af00);
	ptu_int_eq(status, 0);

	status = pt_filter_addr_check(&config.addr_filter, 0x10b000);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result addr_filter_stop_out(void)
{
	struct pt_config config;
	int status;

	pt_config_init(&config);
	config.addr_filter.config.ctl.addr1_cfg = pt_addr_cfg_stop;
	config.addr_filter.addr1_a = 0xa000;
	config.addr_filter.addr1_b = 0xb000;
	config.addr_filter.config.ctl.addr3_cfg = pt_addr_cfg_stop;
	config.addr_filter.addr3_a = 0x10a000;
	config.addr_filter.addr3_b = 0x10b000;

	status = pt_filter_addr_check(&config.addr_filter, 0xfff);
	ptu_int_eq(status, 1);

	status = pt_filter_addr_check(&config.addr_filter, 0xb001);
	ptu_int_eq(status, 1);

	status = pt_filter_addr_check(&config.addr_filter, 0x100fff);
	ptu_int_eq(status, 1);

	status = pt_filter_addr_check(&config.addr_filter, 0x10b001);
	ptu_int_eq(status, 1);

	return ptu_passed();
}

static struct ptunit_result addr_filter_ip_out_stop_in(void)
{
	struct pt_config config;
	int status;

	pt_config_init(&config);
	config.addr_filter.config.ctl.addr1_cfg = pt_addr_cfg_filter;
	config.addr_filter.addr1_a = 0x100f00;
	config.addr_filter.addr1_b = 0x10af00;
	config.addr_filter.config.ctl.addr3_cfg = pt_addr_cfg_stop;
	config.addr_filter.addr3_a = 0x10a000;
	config.addr_filter.addr3_b = 0x10b000;

	status = pt_filter_addr_check(&config.addr_filter, 0x10af01);
	ptu_int_eq(status, 0);

	status = pt_filter_addr_check(&config.addr_filter, 0x10b000);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result addr_filter_ip_in_stop_in(void)
{
	struct pt_config config;
	int status;

	pt_config_init(&config);
	config.addr_filter.config.ctl.addr1_cfg = pt_addr_cfg_filter;
	config.addr_filter.addr1_a = 0x100f00;
	config.addr_filter.addr1_b = 0x10af00;
	config.addr_filter.config.ctl.addr3_cfg = pt_addr_cfg_stop;
	config.addr_filter.addr3_a = 0x10a000;
	config.addr_filter.addr3_b = 0x10b000;

	status = pt_filter_addr_check(&config.addr_filter, 0x10af00);
	ptu_int_eq(status, 0);

	status = pt_filter_addr_check(&config.addr_filter, 0x10a0ff);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, from_user_null);
	ptu_run(suite, from_user_too_small);
	ptu_run(suite, from_user_bad_buffer);
	ptu_run(suite, from_user);
	ptu_run(suite, from_user_small);
	ptu_run(suite, from_user_big);
	ptu_run(suite, size);

	ptu_run(suite, addr_filter_size);
	ptu_run(suite, addr_filter_none);
	ptu_run(suite, addr_filter_0);
	ptu_run(suite, addr_filter_1_3);
	ptu_run_p(suite, addr_filter_oob, 255);
	ptu_run_p(suite, addr_filter_oob, 8);

	ptu_run(suite, addr_filter_ip_in);
	ptu_run(suite, addr_filter_ip_out);
	ptu_run(suite, addr_filter_stop_in);
	ptu_run(suite, addr_filter_stop_out);
	ptu_run(suite, addr_filter_ip_out_stop_in);
	ptu_run(suite, addr_filter_ip_in_stop_in);

	return ptunit_report(&suite);
}
