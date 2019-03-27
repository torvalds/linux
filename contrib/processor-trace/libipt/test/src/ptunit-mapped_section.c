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

#include "pt_mapped_section.h"

#include "intel-pt.h"


static struct ptunit_result begin(void)
{
	struct pt_mapped_section msec;
	struct pt_section sec;
	uint64_t begin;

	pt_msec_init(&msec, &sec, NULL, 0x2000ull, 0x100ull, 0x1000ull);

	begin = pt_msec_begin(&msec);
	ptu_uint_eq(begin, 0x2000);

	return ptu_passed();
}

static struct ptunit_result end(void)
{
	struct pt_mapped_section msec;
	struct pt_section sec;
	uint64_t end;

	pt_msec_init(&msec, &sec, NULL, 0x2000ull, 0x100ull, 0x1000ull);

	end = pt_msec_end(&msec);
	ptu_uint_eq(end, 0x3000);

	return ptu_passed();
}

static struct ptunit_result offset(void)
{
	struct pt_mapped_section msec;
	struct pt_section sec;
	uint64_t offset;

	pt_msec_init(&msec, &sec, NULL, 0x2000ull, 0x100ull, 0x1000ull);

	offset = pt_msec_offset(&msec);
	ptu_uint_eq(offset, 0x100ull);

	return ptu_passed();
}

static struct ptunit_result size(void)
{
	struct pt_mapped_section msec;
	struct pt_section sec;
	uint64_t size;

	pt_msec_init(&msec, &sec, NULL, 0x2000ull, 0x100ull, 0x1000ull);

	size = pt_msec_size(&msec);
	ptu_uint_eq(size, 0x1000ull);

	return ptu_passed();
}

static struct ptunit_result asid(void)
{
	struct pt_mapped_section msec;
	struct pt_asid asid;
	const struct pt_asid *pasid;

	pt_asid_init(&asid);
	asid.cr3 = 0xa00000ull;
	asid.vmcs = 0xb00000ull;

	pt_msec_init(&msec, NULL, &asid, 0x2000ull, 0x100ull, 0x1000ull);

	pasid = pt_msec_asid(&msec);
	ptu_ptr(pasid);
	ptu_uint_eq(pasid->cr3, asid.cr3);
	ptu_uint_eq(pasid->vmcs, asid.vmcs);

	return ptu_passed();
}

static struct ptunit_result asid_null(void)
{
	struct pt_mapped_section msec;
	const struct pt_asid *pasid;

	pt_msec_init(&msec, NULL, NULL, 0x2000ull, 0x100ull, 0x1000ull);

	pasid = pt_msec_asid(&msec);
	ptu_ptr(pasid);
	ptu_uint_eq(pasid->cr3, pt_asid_no_cr3);
	ptu_uint_eq(pasid->vmcs, pt_asid_no_vmcs);

	return ptu_passed();
}

static struct ptunit_result map(void)
{
	struct pt_mapped_section msec;
	uint64_t mapped;

	pt_msec_init(&msec, NULL, NULL, 0x2000ull, 0x100ull, 0x1000ull);

	mapped = pt_msec_map(&msec, 0x900);
	ptu_uint_eq(mapped, 0x2800);

	return ptu_passed();
}

static struct ptunit_result unmap(void)
{
	struct pt_mapped_section msec;
	uint64_t offset;

	pt_msec_init(&msec, NULL, NULL, 0x2000ull, 0x100ull, 0x1000ull);

	offset = pt_msec_unmap(&msec, 0x3000);
	ptu_uint_eq(offset, 0x1100);

	return ptu_passed();
}

static struct ptunit_result section(void)
{
	static struct pt_section section;
	struct pt_mapped_section msec;
	struct pt_section *psection;

	pt_msec_init(&msec, &section, NULL, 0x2000ull, 0x100ull, 0x1000ull);

	psection = pt_msec_section(&msec);
	ptu_ptr_eq(psection, &section);

	return ptu_passed();
}

static struct ptunit_result section_null(void)
{
	struct pt_mapped_section msec;
	struct pt_section *psection;

	pt_msec_init(&msec, NULL, NULL, 0x2000ull, 0x100ull, 0x1000ull);

	psection = pt_msec_section(&msec);
	ptu_ptr_eq(psection, NULL);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, begin);
	ptu_run(suite, end);
	ptu_run(suite, offset);
	ptu_run(suite, size);
	ptu_run(suite, asid);
	ptu_run(suite, asid_null);
	ptu_run(suite, map);
	ptu_run(suite, unmap);
	ptu_run(suite, section);
	ptu_run(suite, section_null);

	return ptunit_report(&suite);
}
