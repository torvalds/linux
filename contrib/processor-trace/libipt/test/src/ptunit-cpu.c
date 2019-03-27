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

#include "pt_cpu.h"
#include "pt_cpuid.h"

#include "intel-pt.h"

#include <stdlib.h>


void pt_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx,
	      uint32_t *edx)
{
	(void) leaf;
	(void) eax;
	(void) ebx;
	(void) ecx;
	(void) edx;
}


static struct ptunit_result cpu_valid(void)
{
	struct pt_cpu cpu;
	int error;

	error = pt_cpu_parse(&cpu, "6/44/2");
	ptu_int_eq(error, 0);
	ptu_int_eq(cpu.vendor, pcv_intel);
	ptu_uint_eq(cpu.family, 6);
	ptu_uint_eq(cpu.model, 44);
	ptu_uint_eq(cpu.stepping, 2);

	error = pt_cpu_parse(&cpu, "0xf/0x2c/0xf");
	ptu_int_eq(error, 0);
	ptu_int_eq(cpu.vendor, pcv_intel);
	ptu_uint_eq(cpu.family, 0xf);
	ptu_uint_eq(cpu.model, 0x2c);
	ptu_uint_eq(cpu.stepping, 0xf);

	error = pt_cpu_parse(&cpu, "022/054/017");
	ptu_int_eq(error, 0);
	ptu_int_eq(cpu.vendor, pcv_intel);
	ptu_uint_eq(cpu.family, 022);
	ptu_uint_eq(cpu.model, 054);
	ptu_uint_eq(cpu.stepping, 017);

	error = pt_cpu_parse(&cpu, "6/44");
	ptu_int_eq(error, 0);
	ptu_int_eq(cpu.vendor, pcv_intel);
	ptu_uint_eq(cpu.family, 6);
	ptu_uint_eq(cpu.model, 44);
	ptu_uint_eq(cpu.stepping, 0);

	return ptu_passed();
}

static struct ptunit_result cpu_null(void)
{
	struct pt_cpu cpu;
	int error;

	error = pt_cpu_parse(&cpu, NULL);
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(NULL, "");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(NULL, NULL);
	ptu_int_eq(error, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result cpu_incomplete(void)
{
	struct pt_cpu cpu;
	int error;

	error = pt_cpu_parse(&cpu, "");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "6");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "6/");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "6//2");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "//");
	ptu_int_eq(error, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result cpu_invalid(void)
{
	struct pt_cpu cpu;
	int error;

	error = pt_cpu_parse(&cpu, "e/44/2");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "6/e/2");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "6/44/e");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "65536/44/2");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "6/256/2");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "6/44/256");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "-1/44/2");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "6/-1/2");
	ptu_int_eq(error, -pte_invalid);

	error = pt_cpu_parse(&cpu, "6/44/-1");
	ptu_int_eq(error, -pte_invalid);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, cpu_valid);
	ptu_run(suite, cpu_null);
	ptu_run(suite, cpu_incomplete);
	ptu_run(suite, cpu_invalid);

	return ptunit_report(&suite);
}
