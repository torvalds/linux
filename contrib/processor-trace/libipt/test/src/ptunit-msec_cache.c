/*
 * Copyright (c) 2017-2018, Intel Corporation
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

#include "pt_msec_cache.h"

#include "intel-pt.h"


int pt_section_get(struct pt_section *section)
{
	uint16_t ucount;

	if (!section)
		return -pte_internal;

	ucount = section->ucount + 1;
	if (!ucount)
		return -pte_overflow;

	section->ucount = ucount;
	return 0;
}

int pt_section_put(struct pt_section *section)
{
	uint16_t ucount;

	if (!section)
		return -pte_internal;

	ucount = section->ucount;
	if (!ucount)
		return -pte_overflow;

	section->ucount = ucount - 1;
	return 0;
}

int pt_section_map(struct pt_section *section)
{
	uint16_t ucount, mcount;

	if (!section)
		return -pte_internal;

	ucount = section->ucount;
	if (!ucount)
		return -pte_internal;

	mcount = section->mcount + 1;
	if (!mcount)
		return -pte_overflow;

	section->mcount = mcount;
	return 0;
}

int pt_section_unmap(struct pt_section *section)
{
	uint16_t ucount, mcount;

	if (!section)
		return -pte_internal;

	ucount = section->ucount;
	if (!ucount)
		return -pte_internal;

	mcount = section->mcount;
	if (!mcount)
		return -pte_overflow;

	section->mcount = mcount - 1;
	return 0;
}

/* A mock image. */
struct pt_image {
	/* The section stored in the image.
	 *
	 * This is either the fixture's section or NULL.
	 */
	struct pt_section *section;
};

extern int pt_image_validate(struct pt_image *, struct pt_mapped_section *,
			     uint64_t, int);
extern int pt_image_find(struct pt_image *, struct pt_mapped_section *,
			 const struct pt_asid *, uint64_t);

int pt_image_validate(struct pt_image *image, struct pt_mapped_section *msec,
		      uint64_t vaddr, int isid)
{
	struct pt_section *section;

	(void) vaddr;
	(void) isid;

	if (!image || !msec)
		return -pte_internal;

	section = image->section;
	if (!section)
		return -pte_nomap;

	if (section != msec->section)
		return -pte_nomap;

	return 0;
}

int pt_image_find(struct pt_image *image, struct pt_mapped_section *msec,
		  const struct pt_asid *asid, uint64_t vaddr)
{
	struct pt_section *section;

	(void) vaddr;

	if (!image || !msec || !asid)
		return -pte_internal;

	section = image->section;
	if (!section)
		return -pte_nomap;

	if (msec->section)
		return -pte_internal;

	msec->section = section;

	return pt_section_get(section);
}

/* A test fixture providing a section and checking the use and map count. */
struct test_fixture {
	/* A test section. */
	struct pt_section section;

	/* A test cache. */
	struct pt_msec_cache mcache;

	/* A test image. */
	struct pt_image image;

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct test_fixture *);
	struct ptunit_result (*fini)(struct test_fixture *);
};

static struct ptunit_result init_null(void)
{
	int status;

	status = pt_msec_cache_init(NULL);
	ptu_int_eq(status, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result fini_null(void)
{
	pt_msec_cache_fini(NULL);

	return ptu_passed();
}

static struct ptunit_result invalidate_null(void)
{
	int status;

	status = pt_msec_cache_invalidate(NULL);
	ptu_int_eq(status, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result read_null(void)
{
	const struct pt_mapped_section *msec;
	struct pt_msec_cache mcache;
	struct pt_image image;
	int status;

	status = pt_msec_cache_read(NULL, &msec, &image, 0ull);
	ptu_int_eq(status, -pte_internal);

	status = pt_msec_cache_read(&mcache, NULL, &image, 0ull);
	ptu_int_eq(status, -pte_internal);

	status = pt_msec_cache_read(&mcache, &msec, NULL, 0ull);
	ptu_int_eq(status, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result fill_null(void)
{
	const struct pt_mapped_section *msec;
	struct pt_msec_cache mcache;
	struct pt_image image;
	struct pt_asid asid;
	int status;

	memset(&mcache, 0, sizeof(mcache));

	status = pt_msec_cache_fill(NULL, &msec, &image, &asid, 0ull);
	ptu_int_eq(status, -pte_internal);

	status = pt_msec_cache_fill(&mcache, NULL, &image, &asid, 0ull);
	ptu_int_eq(status, -pte_internal);

	status = pt_msec_cache_fill(&mcache, &msec, NULL, &asid, 0ull);
	ptu_int_eq(status, -pte_internal);

	status = pt_msec_cache_fill(&mcache, &msec, &image, NULL, 0ull);
	ptu_int_eq(status, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result invalidate(struct test_fixture *tfix)
{
	struct pt_section *section;
	int status;

	status = pt_msec_cache_invalidate(&tfix->mcache);
	ptu_int_eq(status, 0);

	section = pt_msec_section(&tfix->mcache.msec);
	ptu_null(section);

	ptu_uint_eq(tfix->section.mcount, 0);
	ptu_uint_eq(tfix->section.ucount, 0);

	return ptu_passed();
}

static struct ptunit_result read_nomap(struct test_fixture *tfix)
{
	const struct pt_mapped_section *msec;
	int status;

	msec = NULL;

	status = pt_msec_cache_read(&tfix->mcache, &msec, &tfix->image, 0ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_null(msec);

	return ptu_passed();
}

static struct ptunit_result read(struct test_fixture *tfix)
{
	const struct pt_mapped_section *msec;
	struct pt_section *section;
	int status;

	status = pt_msec_cache_read(&tfix->mcache, &msec, &tfix->image, 0ull);
	ptu_int_eq(status, 0);

	ptu_ptr_eq(msec, &tfix->mcache.msec);

	section = pt_msec_section(msec);
	ptu_ptr_eq(section, &tfix->section);

	return ptu_passed();
}

static struct ptunit_result fill_nomap(struct test_fixture *tfix)
{
	const struct pt_mapped_section *msec;
	struct pt_asid asid;
	struct pt_section *section;
	int status;

	msec = NULL;

	status = pt_msec_cache_fill(&tfix->mcache, &msec, &tfix->image, &asid,
				    0ull);
	ptu_int_eq(status, -pte_nomap);

	section = pt_msec_section(&tfix->mcache.msec);
	ptu_null(section);
	ptu_null(msec);

	ptu_uint_eq(tfix->section.mcount, 0);
	ptu_uint_eq(tfix->section.ucount, 0);

	return ptu_passed();
}

static struct ptunit_result fill(struct test_fixture *tfix)
{
	const struct pt_mapped_section *msec;
	struct pt_section *section;
	struct pt_asid asid;
	int status;

	status = pt_msec_cache_fill(&tfix->mcache, &msec, &tfix->image, &asid,
				    0ull);
	ptu_int_eq(status, 0);

	ptu_ptr_eq(msec, &tfix->mcache.msec);

	section = pt_msec_section(msec);
	ptu_ptr_eq(section, &tfix->section);

	ptu_uint_eq(section->mcount, 1);
	ptu_uint_eq(section->ucount, 1);

	return ptu_passed();
}

static struct ptunit_result sfix_init(struct test_fixture *tfix)
{
	memset(&tfix->section, 0, sizeof(tfix->section));
	memset(&tfix->mcache, 0, sizeof(tfix->mcache));
	memset(&tfix->image, 0, sizeof(tfix->image));

	return ptu_passed();
}

static struct ptunit_result ifix_init(struct test_fixture *tfix)
{
	ptu_test(sfix_init, tfix);

	tfix->image.section = &tfix->section;

	return ptu_passed();
}

static struct ptunit_result cfix_init(struct test_fixture *tfix)
{
	ptu_test(sfix_init, tfix);

	tfix->mcache.msec.section = &tfix->section;

	tfix->section.ucount = 1;
	tfix->section.mcount = 1;

	return ptu_passed();
}

static struct ptunit_result cifix_init(struct test_fixture *tfix)
{
	ptu_test(cfix_init, tfix);

	tfix->image.section = &tfix->section;

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;
	struct test_fixture sfix, ifix, cfix, cifix;

	sfix.init = sfix_init;
	sfix.fini = NULL;

	ifix.init = ifix_init;
	ifix.fini = NULL;

	cfix.init = cfix_init;
	cfix.fini = NULL;

	cifix.init = cifix_init;
	cifix.fini = NULL;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, init_null);
	ptu_run(suite, fini_null);
	ptu_run(suite, invalidate_null);
	ptu_run(suite, read_null);
	ptu_run(suite, fill_null);

	ptu_run_f(suite, invalidate, sfix);
	ptu_run_f(suite, invalidate, cfix);

	ptu_run_f(suite, read_nomap, sfix);
	ptu_run_f(suite, read_nomap, ifix);
	ptu_run_f(suite, read_nomap, cfix);
	ptu_run_f(suite, read, cifix);

	ptu_run_f(suite, fill_nomap, sfix);
	ptu_run_f(suite, fill_nomap, cfix);
	ptu_run_f(suite, fill, ifix);
	ptu_run_f(suite, fill, cifix);

	return ptunit_report(&suite);
}
