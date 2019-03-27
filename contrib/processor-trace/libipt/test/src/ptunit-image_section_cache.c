/*
 * Copyright (c) 2016-2018, Intel Corporation
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

#include "pt_image_section_cache.h"

#include "ptunit_threads.h"

#include "intel-pt.h"

#include <stdlib.h>


struct pt_section {
	/* The filename.  We only support string literals for testing. */
	const char *filename;

	/* The file offset and size. */
	uint64_t offset;
	uint64_t size;

	/* The bcache size. */
	uint64_t bcsize;

	/* The iscache back link. */
	struct pt_image_section_cache *iscache;

	/* The file content. */
	uint8_t content[0x10];

	/* The use count. */
	int ucount;

	/* The attach count. */
	int acount;

	/* The map count. */
	int mcount;

#if defined(FEATURE_THREADS)
	/* A lock protecting this section. */
	mtx_t lock;
	/* A lock protecting the iscache and acount fields. */
	mtx_t alock;
#endif /* defined(FEATURE_THREADS) */
};

extern struct pt_section *pt_mk_section(const char *filename, uint64_t offset,
					uint64_t size);

extern int pt_section_get(struct pt_section *section);
extern int pt_section_put(struct pt_section *section);
extern int pt_section_attach(struct pt_section *section,
			     struct pt_image_section_cache *iscache);
extern int pt_section_detach(struct pt_section *section,
			     struct pt_image_section_cache *iscache);

extern int pt_section_map(struct pt_section *section);
extern int pt_section_map_share(struct pt_section *section);
extern int pt_section_unmap(struct pt_section *section);
extern int pt_section_request_bcache(struct pt_section *section);

extern const char *pt_section_filename(const struct pt_section *section);
extern uint64_t pt_section_offset(const struct pt_section *section);
extern uint64_t pt_section_size(const struct pt_section *section);
extern int pt_section_memsize(struct pt_section *section, uint64_t *size);

extern int pt_section_read(const struct pt_section *section, uint8_t *buffer,
			   uint16_t size, uint64_t offset);


struct pt_section *pt_mk_section(const char *filename, uint64_t offset,
				 uint64_t size)
{
	struct pt_section *section;

	section = malloc(sizeof(*section));
	if (section) {
		uint8_t idx;

		memset(section, 0, sizeof(*section));
		section->filename = filename;
		section->offset = offset;
		section->size = size;
		section->ucount = 1;

		for (idx = 0; idx < sizeof(section->content); ++idx)
			section->content[idx] = idx;

#if defined(FEATURE_THREADS)
		{
			int errcode;

			errcode = mtx_init(&section->lock, mtx_plain);
			if (errcode != thrd_success) {
				free(section);
				section = NULL;
			}

			errcode = mtx_init(&section->alock, mtx_plain);
			if (errcode != thrd_success) {
				mtx_destroy(&section->lock);
				free(section);
				section = NULL;
			}
		}
#endif /* defined(FEATURE_THREADS) */
	}

	return section;
}

static int pt_section_lock(struct pt_section *section)
{
	if (!section)
		return -pte_internal;

#if defined(FEATURE_THREADS)
	{
		int errcode;

		errcode = mtx_lock(&section->lock);
		if (errcode != thrd_success)
			return -pte_bad_lock;
	}
#endif /* defined(FEATURE_THREADS) */

	return 0;
}

static int pt_section_unlock(struct pt_section *section)
{
	if (!section)
		return -pte_internal;

#if defined(FEATURE_THREADS)
	{
		int errcode;

		errcode = mtx_unlock(&section->lock);
		if (errcode != thrd_success)
			return -pte_bad_lock;
	}
#endif /* defined(FEATURE_THREADS) */

	return 0;
}

static int pt_section_lock_attach(struct pt_section *section)
{
	if (!section)
		return -pte_internal;

#if defined(FEATURE_THREADS)
	{
		int errcode;

		errcode = mtx_lock(&section->alock);
		if (errcode != thrd_success)
			return -pte_bad_lock;
	}
#endif /* defined(FEATURE_THREADS) */

	return 0;
}

static int pt_section_unlock_attach(struct pt_section *section)
{
	if (!section)
		return -pte_internal;

#if defined(FEATURE_THREADS)
	{
		int errcode;

		errcode = mtx_unlock(&section->alock);
		if (errcode != thrd_success)
			return -pte_bad_lock;
	}
#endif /* defined(FEATURE_THREADS) */

	return 0;
}

int pt_section_get(struct pt_section *section)
{
	int errcode, ucount;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	ucount = ++section->ucount;

	errcode = pt_section_unlock(section);
	if (errcode < 0)
		return errcode;

	if (!ucount)
		return -pte_internal;

	return 0;
}

int pt_section_put(struct pt_section *section)
{
	int errcode, ucount;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	ucount = --section->ucount;

	errcode = pt_section_unlock(section);
	if (errcode < 0)
		return errcode;

	if (!ucount) {
#if defined(FEATURE_THREADS)
		mtx_destroy(&section->alock);
		mtx_destroy(&section->lock);
#endif /* defined(FEATURE_THREADS) */
		free(section);
	}

	return 0;
}

int pt_section_attach(struct pt_section *section,
		      struct pt_image_section_cache *iscache)
{
	int errcode, ucount, acount;

	if (!section || !iscache)
		return -pte_internal;

	errcode = pt_section_lock_attach(section);
	if (errcode < 0)
		return errcode;

	ucount = section->ucount;
	acount = section->acount;
	if (!acount) {
		if (section->iscache || !ucount)
			goto out_unlock;

		section->iscache = iscache;
		section->acount = 1;

		return pt_section_unlock_attach(section);
	}

	acount += 1;
	if (!acount) {
		(void) pt_section_unlock_attach(section);
		return -pte_overflow;
	}

	if (ucount < acount)
		goto out_unlock;

	if (section->iscache != iscache)
		goto out_unlock;

	section->acount = acount;

	return pt_section_unlock_attach(section);

 out_unlock:
	(void) pt_section_unlock_attach(section);
	return -pte_internal;
}

int pt_section_detach(struct pt_section *section,
		      struct pt_image_section_cache *iscache)
{
	int errcode, ucount, acount;

	if (!section || !iscache)
		return -pte_internal;

	errcode = pt_section_lock_attach(section);
	if (errcode < 0)
		return errcode;

	if (section->iscache != iscache)
		goto out_unlock;

	acount = section->acount;
	if (!acount)
		goto out_unlock;

	acount -= 1;
	ucount = section->ucount;
	if (ucount < acount)
		goto out_unlock;

	section->acount = acount;
	if (!acount)
		section->iscache = NULL;

	return pt_section_unlock_attach(section);

 out_unlock:
	(void) pt_section_unlock_attach(section);
	return -pte_internal;
}

int pt_section_map(struct pt_section *section)
{
	struct pt_image_section_cache *iscache;
	int errcode, status;

	if (!section)
		return -pte_internal;

	errcode = pt_section_map_share(section);
	if (errcode < 0)
		return errcode;

	errcode = pt_section_lock_attach(section);
	if (errcode < 0)
		return errcode;

	status = 0;
	iscache = section->iscache;
	if (iscache)
		status = pt_iscache_notify_map(iscache, section);

	errcode = pt_section_unlock_attach(section);

	return (status < 0) ? status : errcode;
}

int pt_section_map_share(struct pt_section *section)
{
	int errcode, mcount;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	mcount = ++section->mcount;

	errcode = pt_section_unlock(section);
	if (errcode < 0)
		return errcode;

	if (mcount <= 0)
		return -pte_internal;

	return 0;
}

int pt_section_unmap(struct pt_section *section)
{
	int errcode, mcount;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	section->bcsize = 0ull;
	mcount = --section->mcount;

	errcode = pt_section_unlock(section);
	if (errcode < 0)
		return errcode;

	if (mcount < 0)
		return -pte_internal;

	return 0;
}

int pt_section_request_bcache(struct pt_section *section)
{
	struct pt_image_section_cache *iscache;
	uint64_t memsize;
	int errcode;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock_attach(section);
	if (errcode < 0)
		return errcode;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		goto out_alock;

	if (section->bcsize)
		goto out_lock;

	section->bcsize = section->size * 3;
	memsize = section->size + section->bcsize;

	errcode = pt_section_unlock(section);
	if (errcode < 0)
		goto out_alock;

	iscache = section->iscache;
	if (iscache) {
		errcode = pt_iscache_notify_resize(iscache, section, memsize);
		if (errcode < 0)
			goto out_alock;
	}

	return pt_section_unlock_attach(section);


out_lock:
	(void) pt_section_unlock(section);

out_alock:
	(void) pt_section_unlock_attach(section);
	return errcode;
}

const char *pt_section_filename(const struct pt_section *section)
{
	if (!section)
		return NULL;

	return section->filename;
}

uint64_t pt_section_offset(const struct pt_section *section)
{
	if (!section)
		return 0ull;

	return section->offset;
}

uint64_t pt_section_size(const struct pt_section *section)
{
	if (!section)
		return 0ull;

	return section->size;
}

int pt_section_memsize(struct pt_section *section, uint64_t *size)
{
	if (!section || !size)
		return -pte_internal;

	*size = section->mcount ? section->size + section->bcsize : 0ull;

	return 0;
}

int pt_section_read(const struct pt_section *section, uint8_t *buffer,
		    uint16_t size, uint64_t offset)
{
	uint64_t begin, end, max;

	if (!section || !buffer)
		return -pte_internal;

	begin = offset;
	end = begin + size;
	max = sizeof(section->content);

	if (max <= begin)
		return -pte_nomap;

	if (max < end)
		end = max;

	if (end <= begin)
		return -pte_invalid;

	memcpy(buffer, &section->content[begin], (size_t) (end - begin));
	return (int) (end - begin);
}

enum {
	/* The number of test sections. */
	num_sections	= 8,

#if defined(FEATURE_THREADS)

	num_threads	= 8,

#endif /* defined(FEATURE_THREADS) */

	num_iterations	= 0x1000
};

struct iscache_fixture {
	/* Threading support. */
	struct ptunit_thrd_fixture thrd;

	/* The image section cache under test. */
	struct pt_image_section_cache iscache;

	/* A bunch of test sections. */
	struct pt_section *section[num_sections];

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct iscache_fixture *);
	struct ptunit_result (*fini)(struct iscache_fixture *);
};

static struct ptunit_result dfix_init(struct iscache_fixture *cfix)
{
	int idx;

	ptu_test(ptunit_thrd_init, &cfix->thrd);

	memset(cfix->section, 0, sizeof(cfix->section));

	for (idx = 0; idx < num_sections; ++idx) {
		struct pt_section *section;

		section = pt_mk_section("some-filename",
					idx % 3 == 0 ? 0x1000 : 0x2000,
					idx % 2 == 0 ? 0x1000 : 0x2000);
		ptu_ptr(section);

		cfix->section[idx] = section;
	}

	return ptu_passed();
}

static struct ptunit_result cfix_init(struct iscache_fixture *cfix)
{
	int errcode;

	ptu_test(dfix_init, cfix);

	errcode = pt_iscache_init(&cfix->iscache, NULL);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result sfix_init(struct iscache_fixture *cfix)
{
	int status, idx;

	ptu_test(cfix_init, cfix);

	cfix->iscache.limit = 0x7800;

	for (idx = 0; idx < num_sections; ++idx) {
		status = pt_iscache_add(&cfix->iscache, cfix->section[idx],
					0ull);
		ptu_int_ge(status, 0);
	}

	return ptu_passed();
}

static struct ptunit_result cfix_fini(struct iscache_fixture *cfix)
{
	int idx, errcode;

	ptu_test(ptunit_thrd_fini, &cfix->thrd);

	for (idx = 0; idx < cfix->thrd.nthreads; ++idx)
		ptu_int_eq(cfix->thrd.result[idx], 0);

	pt_iscache_fini(&cfix->iscache);

	for (idx = 0; idx < num_sections; ++idx) {
		ptu_int_eq(cfix->section[idx]->ucount, 1);
		ptu_int_eq(cfix->section[idx]->acount, 0);
		ptu_int_eq(cfix->section[idx]->mcount, 0);
		ptu_null(cfix->section[idx]->iscache);

		errcode = pt_section_put(cfix->section[idx]);
		ptu_int_eq(errcode, 0);
	}

	return ptu_passed();
}


static struct ptunit_result init_null(void)
{
	int errcode;

	errcode = pt_iscache_init(NULL, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result fini_null(void)
{
	pt_iscache_fini(NULL);

	return ptu_passed();
}

static struct ptunit_result name_null(void)
{
	const char *name;

	name = pt_iscache_name(NULL);
	ptu_null(name);

	return ptu_passed();
}

static struct ptunit_result add_null(void)
{
	struct pt_image_section_cache iscache;
	struct pt_section section;
	int errcode;

	errcode = pt_iscache_add(NULL, &section, 0ull);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_iscache_add(&iscache, NULL, 0ull);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result find_null(void)
{
	int errcode;

	errcode = pt_iscache_find(NULL, "filename", 0ull, 0ull, 0ull);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result lookup_null(void)
{
	struct pt_image_section_cache iscache;
	struct pt_section *section;
	uint64_t laddr;
	int errcode;

	errcode = pt_iscache_lookup(NULL, &section, &laddr, 0);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_iscache_lookup(&iscache, NULL, &laddr, 0);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_iscache_lookup(&iscache, &section, NULL, 0);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result clear_null(void)
{
	int errcode;

	errcode = pt_iscache_clear(NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result free_null(void)
{
	pt_iscache_free(NULL);

	return ptu_passed();
}

static struct ptunit_result add_file_null(void)
{
	struct pt_image_section_cache iscache;
	int errcode;

	errcode = pt_iscache_add_file(NULL, "filename", 0ull, 0ull, 0ull);
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_iscache_add_file(&iscache, NULL, 0ull, 0ull, 0ull);
	ptu_int_eq(errcode, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result read_null(void)
{
	struct pt_image_section_cache iscache;
	uint8_t buffer;
	int errcode;

	errcode = pt_iscache_read(NULL, &buffer, sizeof(buffer), 1ull, 0ull);
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_iscache_read(&iscache, NULL, sizeof(buffer), 1ull, 0ull);
	ptu_int_eq(errcode, -pte_invalid);

	errcode = pt_iscache_read(&iscache, &buffer, 0ull, 1, 0ull);
	ptu_int_eq(errcode, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result init_fini(struct iscache_fixture *cfix)
{
	(void) cfix;

	/* The actual init and fini calls are in cfix_init() and cfix_fini(). */
	return ptu_passed();
}

static struct ptunit_result name(struct iscache_fixture *cfix)
{
	const char *name;

	pt_iscache_init(&cfix->iscache, "iscache-name");

	name = pt_iscache_name(&cfix->iscache);
	ptu_str_eq(name, "iscache-name");

	return ptu_passed();
}

static struct ptunit_result name_none(struct iscache_fixture *cfix)
{
	const char *name;

	pt_iscache_init(&cfix->iscache, NULL);

	name = pt_iscache_name(&cfix->iscache);
	ptu_null(name);

	return ptu_passed();
}

static struct ptunit_result add(struct iscache_fixture *cfix)
{
	int isid;

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0ull);
	ptu_int_gt(isid, 0);

	/* The cache attaches and gets a reference on success. */
	ptu_int_eq(cfix->section[0]->ucount, 2);
	ptu_int_eq(cfix->section[0]->acount, 1);

	/* The added section must be implicitly put in pt_iscache_fini. */
	return ptu_passed();
}

static struct ptunit_result add_no_name(struct iscache_fixture *cfix)
{
	struct pt_section section;
	int errcode;

	memset(&section, 0, sizeof(section));

	errcode = pt_iscache_add(&cfix->iscache, &section, 0ull);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result add_file(struct iscache_fixture *cfix)
{
	int isid;

	isid = pt_iscache_add_file(&cfix->iscache, "name", 0ull, 1ull, 0ull);
	ptu_int_gt(isid, 0);

	return ptu_passed();
}

static struct ptunit_result find(struct iscache_fixture *cfix)
{
	struct pt_section *section;
	int found, isid;

	section = cfix->section[0];
	ptu_ptr(section);

	isid = pt_iscache_add(&cfix->iscache, section, 0ull);
	ptu_int_gt(isid, 0);

	found = pt_iscache_find(&cfix->iscache, section->filename,
				section->offset, section->size, 0ull);
	ptu_int_eq(found, isid);

	return ptu_passed();
}

static struct ptunit_result find_empty(struct iscache_fixture *cfix)
{
	struct pt_section *section;
	int found;

	section = cfix->section[0];
	ptu_ptr(section);

	found = pt_iscache_find(&cfix->iscache, section->filename,
				section->offset, section->size, 0ull);
	ptu_int_eq(found, 0);

	return ptu_passed();
}

static struct ptunit_result find_bad_filename(struct iscache_fixture *cfix)
{
	struct pt_section *section;
	int found, isid;

	section = cfix->section[0];
	ptu_ptr(section);

	isid = pt_iscache_add(&cfix->iscache, section, 0ull);
	ptu_int_gt(isid, 0);

	found = pt_iscache_find(&cfix->iscache, "bad-filename",
				section->offset, section->size, 0ull);
	ptu_int_eq(found, 0);

	return ptu_passed();
}

static struct ptunit_result find_null_filename(struct iscache_fixture *cfix)
{
	int errcode;

	errcode = pt_iscache_find(&cfix->iscache, NULL, 0ull, 0ull, 0ull);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result find_bad_offset(struct iscache_fixture *cfix)
{
	struct pt_section *section;
	int found, isid;

	section = cfix->section[0];
	ptu_ptr(section);

	isid = pt_iscache_add(&cfix->iscache, section, 0ull);
	ptu_int_gt(isid, 0);

	found = pt_iscache_find(&cfix->iscache, section->filename, 0ull,
				section->size, 0ull);
	ptu_int_eq(found, 0);

	return ptu_passed();
}

static struct ptunit_result find_bad_size(struct iscache_fixture *cfix)
{
	struct pt_section *section;
	int found, isid;

	section = cfix->section[0];
	ptu_ptr(section);

	isid = pt_iscache_add(&cfix->iscache, section, 0ull);
	ptu_int_gt(isid, 0);

	found = pt_iscache_find(&cfix->iscache, section->filename,
				section->offset, 0ull, 0ull);
	ptu_int_eq(found, 0);

	return ptu_passed();
}

static struct ptunit_result find_bad_laddr(struct iscache_fixture *cfix)
{
	struct pt_section *section;
	int found, isid;

	section = cfix->section[0];
	ptu_ptr(section);

	isid = pt_iscache_add(&cfix->iscache, section, 0ull);
	ptu_int_gt(isid, 0);

	found = pt_iscache_find(&cfix->iscache, section->filename,
				section->offset, section->size, 1ull);
	ptu_int_eq(found, 0);

	return ptu_passed();
}

static struct ptunit_result lookup(struct iscache_fixture *cfix)
{
	struct pt_section *section;
	uint64_t laddr;
	int errcode, isid;

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0ull);
	ptu_int_gt(isid, 0);

	errcode = pt_iscache_lookup(&cfix->iscache, &section, &laddr, isid);
	ptu_int_eq(errcode, 0);
	ptu_ptr_eq(section, cfix->section[0]);
	ptu_uint_eq(laddr, 0ull);

	errcode = pt_section_put(section);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result lookup_bad_isid(struct iscache_fixture *cfix)
{
	struct pt_section *section;
	uint64_t laddr;
	int errcode, isid;

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0ull);
	ptu_int_gt(isid, 0);

	errcode = pt_iscache_lookup(&cfix->iscache, &section, &laddr, 0);
	ptu_int_eq(errcode, -pte_bad_image);

	errcode = pt_iscache_lookup(&cfix->iscache, &section, &laddr, -isid);
	ptu_int_eq(errcode, -pte_bad_image);

	errcode = pt_iscache_lookup(&cfix->iscache, &section, &laddr, isid + 1);
	ptu_int_eq(errcode, -pte_bad_image);

	return ptu_passed();
}

static struct ptunit_result clear_empty(struct iscache_fixture *cfix)
{
	int errcode;

	errcode = pt_iscache_clear(&cfix->iscache);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result clear_find(struct iscache_fixture *cfix)
{
	struct pt_section *section;
	int errcode, found, isid;

	section = cfix->section[0];
	ptu_ptr(section);

	isid = pt_iscache_add(&cfix->iscache, section, 0ull);
	ptu_int_gt(isid, 0);

	errcode = pt_iscache_clear(&cfix->iscache);
	ptu_int_eq(errcode, 0);


	found = pt_iscache_find(&cfix->iscache, section->filename,
				section->offset, section->size, 0ull);
	ptu_int_eq(found, 0);

	return ptu_passed();
}

static struct ptunit_result clear_lookup(struct iscache_fixture *cfix)
{
	struct pt_section *section;
	uint64_t laddr;
	int errcode, isid;

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0ull);
	ptu_int_gt(isid, 0);

	errcode = pt_iscache_clear(&cfix->iscache);
	ptu_int_eq(errcode, 0);

	errcode = pt_iscache_lookup(&cfix->iscache, &section, &laddr, isid);
	ptu_int_eq(errcode, -pte_bad_image);

	return ptu_passed();
}

static struct ptunit_result add_twice(struct iscache_fixture *cfix)
{
	int isid[2];

	isid[0] = pt_iscache_add(&cfix->iscache, cfix->section[0], 0ull);
	ptu_int_gt(isid[0], 0);

	isid[1] = pt_iscache_add(&cfix->iscache, cfix->section[0], 0ull);
	ptu_int_gt(isid[1], 0);

	/* The second add should be ignored. */
	ptu_int_eq(isid[1], isid[0]);

	return ptu_passed();
}

static struct ptunit_result add_same(struct iscache_fixture *cfix)
{
	int isid[2];

	isid[0] = pt_iscache_add(&cfix->iscache, cfix->section[0], 0ull);
	ptu_int_gt(isid[0], 0);

	cfix->section[1]->offset = cfix->section[0]->offset;
	cfix->section[1]->size = cfix->section[0]->size;

	isid[1] = pt_iscache_add(&cfix->iscache, cfix->section[1], 0ull);
	ptu_int_gt(isid[1], 0);

	/* The second add should be ignored. */
	ptu_int_eq(isid[1], isid[0]);

	return ptu_passed();
}

static struct ptunit_result
add_twice_different_laddr(struct iscache_fixture *cfix)
{
	int isid[2];

	isid[0] = pt_iscache_add(&cfix->iscache, cfix->section[0], 0ull);
	ptu_int_gt(isid[0], 0);

	isid[1] = pt_iscache_add(&cfix->iscache, cfix->section[0], 1ull);
	ptu_int_gt(isid[1], 0);

	/* We must get different identifiers. */
	ptu_int_ne(isid[1], isid[0]);

	/* We attach twice and take two references - one for each entry. */
	ptu_int_eq(cfix->section[0]->ucount, 3);
	ptu_int_eq(cfix->section[0]->acount, 2);

	return ptu_passed();
}

static struct ptunit_result
add_same_different_laddr(struct iscache_fixture *cfix)
{
	int isid[2];

	isid[0] = pt_iscache_add(&cfix->iscache, cfix->section[0], 0ull);
	ptu_int_gt(isid[0], 0);

	cfix->section[1]->offset = cfix->section[0]->offset;
	cfix->section[1]->size = cfix->section[0]->size;

	isid[1] = pt_iscache_add(&cfix->iscache, cfix->section[1], 1ull);
	ptu_int_gt(isid[1], 0);

	/* We must get different identifiers. */
	ptu_int_ne(isid[1], isid[0]);

	return ptu_passed();
}

static struct ptunit_result
add_different_same_laddr(struct iscache_fixture *cfix)
{
	int isid[2];

	isid[0] = pt_iscache_add(&cfix->iscache, cfix->section[0], 0ull);
	ptu_int_gt(isid[0], 0);

	isid[1] = pt_iscache_add(&cfix->iscache, cfix->section[1], 0ull);
	ptu_int_gt(isid[1], 0);

	/* We must get different identifiers. */
	ptu_int_ne(isid[1], isid[0]);

	return ptu_passed();
}

static struct ptunit_result add_file_same(struct iscache_fixture *cfix)
{
	int isid[2];

	isid[0] = pt_iscache_add_file(&cfix->iscache, "name", 0ull, 1ull, 0ull);
	ptu_int_gt(isid[0], 0);

	isid[1] = pt_iscache_add_file(&cfix->iscache, "name", 0ull, 1ull, 0ull);
	ptu_int_gt(isid[1], 0);

	/* The second add should be ignored. */
	ptu_int_eq(isid[1], isid[0]);

	return ptu_passed();
}

static struct ptunit_result
add_file_same_different_laddr(struct iscache_fixture *cfix)
{
	int isid[2];

	isid[0] = pt_iscache_add_file(&cfix->iscache, "name", 0ull, 1ull, 0ull);
	ptu_int_gt(isid[0], 0);

	isid[1] = pt_iscache_add_file(&cfix->iscache, "name", 0ull, 1ull, 1ull);
	ptu_int_gt(isid[1], 0);

	/* We must get different identifiers. */
	ptu_int_ne(isid[1], isid[0]);

	return ptu_passed();
}

static struct ptunit_result
add_file_different_same_laddr(struct iscache_fixture *cfix)
{
	int isid[2];

	isid[0] = pt_iscache_add_file(&cfix->iscache, "name", 0ull, 1ull, 0ull);
	ptu_int_gt(isid[0], 0);

	isid[1] = pt_iscache_add_file(&cfix->iscache, "name", 1ull, 1ull, 0ull);
	ptu_int_gt(isid[1], 0);

	/* We must get different identifiers. */
	ptu_int_ne(isid[1], isid[0]);

	return ptu_passed();
}

static struct ptunit_result read(struct iscache_fixture *cfix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_iscache_read(&cfix->iscache, buffer, 2ull, isid, 0xa008ull);
	ptu_int_eq(status, 2);
	ptu_uint_eq(buffer[0], 0x8);
	ptu_uint_eq(buffer[1], 0x9);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_truncate(struct iscache_fixture *cfix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_iscache_read(&cfix->iscache, buffer, sizeof(buffer), isid,
				 0xa00full);
	ptu_int_eq(status, 1);
	ptu_uint_eq(buffer[0], 0xf);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_bad_vaddr(struct iscache_fixture *cfix)
{
	uint8_t buffer[] = { 0xcc };
	int status, isid;

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_iscache_read(&cfix->iscache, buffer, 1ull, isid, 0xb000ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_uint_eq(buffer[0], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_bad_isid(struct iscache_fixture *cfix)
{
	uint8_t buffer[] = { 0xcc };
	int status, isid;

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_iscache_read(&cfix->iscache, buffer, 1ull, isid + 1,
				 0xa000ull);
	ptu_int_eq(status, -pte_bad_image);
	ptu_uint_eq(buffer[0], 0xcc);

	return ptu_passed();
}

static struct ptunit_result lru_map(struct iscache_fixture *cfix)
{
	int status, isid;

	cfix->iscache.limit = cfix->section[0]->size;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	ptu_ptr(cfix->iscache.lru);
	ptu_ptr_eq(cfix->iscache.lru->section, cfix->section[0]);
	ptu_null(cfix->iscache.lru->next);
	ptu_uint_eq(cfix->iscache.used, cfix->section[0]->size);

	return ptu_passed();
}

static struct ptunit_result lru_read(struct iscache_fixture *cfix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	cfix->iscache.limit = cfix->section[0]->size;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_iscache_read(&cfix->iscache, buffer, 2ull, isid, 0xa008ull);
	ptu_int_eq(status, 2);

	ptu_ptr(cfix->iscache.lru);
	ptu_ptr_eq(cfix->iscache.lru->section, cfix->section[0]);
	ptu_null(cfix->iscache.lru->next);
	ptu_uint_eq(cfix->iscache.used, cfix->section[0]->size);

	return ptu_passed();
}

static struct ptunit_result lru_map_nodup(struct iscache_fixture *cfix)
{
	int status, isid;

	cfix->iscache.limit = 2 * cfix->section[0]->size;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	ptu_ptr(cfix->iscache.lru);
	ptu_ptr_eq(cfix->iscache.lru->section, cfix->section[0]);
	ptu_null(cfix->iscache.lru->next);
	ptu_uint_eq(cfix->iscache.used, cfix->section[0]->size);

	return ptu_passed();
}

static struct ptunit_result lru_map_too_big(struct iscache_fixture *cfix)
{
	int status, isid;

	cfix->iscache.limit = cfix->section[0]->size - 1;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	ptu_null(cfix->iscache.lru);
	ptu_uint_eq(cfix->iscache.used, 0ull);

	return ptu_passed();
}

static struct ptunit_result lru_map_add_front(struct iscache_fixture *cfix)
{
	int status, isid;

	cfix->iscache.limit = cfix->section[0]->size + cfix->section[1]->size;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[1], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_map(cfix->section[1]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[1]);
	ptu_int_eq(status, 0);

	ptu_ptr(cfix->iscache.lru);
	ptu_ptr_eq(cfix->iscache.lru->section, cfix->section[1]);
	ptu_ptr(cfix->iscache.lru->next);
	ptu_ptr_eq(cfix->iscache.lru->next->section, cfix->section[0]);
	ptu_null(cfix->iscache.lru->next->next);
	ptu_uint_eq(cfix->iscache.used,
		    cfix->section[0]->size + cfix->section[1]->size);

	return ptu_passed();
}

static struct ptunit_result lru_map_move_front(struct iscache_fixture *cfix)
{
	int status, isid;

	cfix->iscache.limit = cfix->section[0]->size + cfix->section[1]->size;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[1], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_map(cfix->section[1]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[1]);
	ptu_int_eq(status, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	ptu_ptr(cfix->iscache.lru);
	ptu_ptr_eq(cfix->iscache.lru->section, cfix->section[0]);
	ptu_ptr(cfix->iscache.lru->next);
	ptu_ptr_eq(cfix->iscache.lru->next->section, cfix->section[1]);
	ptu_null(cfix->iscache.lru->next->next);
	ptu_uint_eq(cfix->iscache.used,
		    cfix->section[0]->size + cfix->section[1]->size);

	return ptu_passed();
}

static struct ptunit_result lru_map_evict(struct iscache_fixture *cfix)
{
	int status, isid;

	cfix->iscache.limit = cfix->section[0]->size +
		cfix->section[1]->size - 1;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[1], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_map(cfix->section[1]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[1]);
	ptu_int_eq(status, 0);

	ptu_ptr(cfix->iscache.lru);
	ptu_ptr_eq(cfix->iscache.lru->section, cfix->section[1]);
	ptu_null(cfix->iscache.lru->next);
	ptu_uint_eq(cfix->iscache.used, cfix->section[1]->size);

	return ptu_passed();
}

static struct ptunit_result lru_bcache_evict(struct iscache_fixture *cfix)
{
	int status, isid;

	cfix->iscache.limit = 4 * cfix->section[0]->size +
		cfix->section[1]->size - 1;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[1], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_map(cfix->section[1]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[1]);
	ptu_int_eq(status, 0);

	status = pt_section_request_bcache(cfix->section[0]);
	ptu_int_eq(status, 0);

	ptu_ptr(cfix->iscache.lru);
	ptu_ptr_eq(cfix->iscache.lru->section, cfix->section[0]);
	ptu_null(cfix->iscache.lru->next);
	ptu_uint_eq(cfix->iscache.used, 4 * cfix->section[0]->size);

	return ptu_passed();
}

static struct ptunit_result lru_bcache_clear(struct iscache_fixture *cfix)
{
	int status, isid;

	cfix->iscache.limit = cfix->section[0]->size + cfix->section[1]->size;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[1], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_map(cfix->section[1]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[1]);
	ptu_int_eq(status, 0);

	status = pt_section_request_bcache(cfix->section[0]);
	ptu_int_eq(status, 0);

	ptu_null(cfix->iscache.lru);
	ptu_uint_eq(cfix->iscache.used, 0ull);

	return ptu_passed();
}

static struct ptunit_result lru_limit_evict(struct iscache_fixture *cfix)
{
	int status, isid;

	cfix->iscache.limit = cfix->section[0]->size + cfix->section[1]->size;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[1], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_map(cfix->section[1]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[1]);
	ptu_int_eq(status, 0);

	status = pt_iscache_set_limit(&cfix->iscache,
				      cfix->section[0]->size +
				      cfix->section[1]->size - 1);
	ptu_int_eq(status, 0);

	ptu_ptr(cfix->iscache.lru);
	ptu_ptr_eq(cfix->iscache.lru->section, cfix->section[1]);
	ptu_null(cfix->iscache.lru->next);
	ptu_uint_eq(cfix->iscache.used, cfix->section[1]->size);

	return ptu_passed();
}

static struct ptunit_result lru_clear(struct iscache_fixture *cfix)
{
	int status, isid;

	cfix->iscache.limit = cfix->section[0]->size;
	ptu_uint_eq(cfix->iscache.used, 0ull);
	ptu_null(cfix->iscache.lru);

	isid = pt_iscache_add(&cfix->iscache, cfix->section[0], 0xa000ull);
	ptu_int_gt(isid, 0);

	status = pt_section_map(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_section_unmap(cfix->section[0]);
	ptu_int_eq(status, 0);

	status = pt_iscache_clear(&cfix->iscache);
	ptu_int_eq(status, 0);

	ptu_null(cfix->iscache.lru);
	ptu_uint_eq(cfix->iscache.used, 0ull);

	return ptu_passed();
}

static int worker_add(void *arg)
{
	struct iscache_fixture *cfix;
	int it;

	cfix = arg;
	if (!cfix)
		return -pte_internal;

	for (it = 0; it < num_iterations; ++it) {
		uint64_t laddr;
		int sec;

		laddr = 0x1000ull * (it % 23);

		for (sec = 0; sec < num_sections; ++sec) {
			struct pt_section *section;
			uint64_t addr;
			int isid, errcode;

			isid = pt_iscache_add(&cfix->iscache,
					      cfix->section[sec], laddr);
			if (isid < 0)
				return isid;

			errcode = pt_iscache_lookup(&cfix->iscache, &section,
						    &addr, isid);
			if (errcode < 0)
				return errcode;

			if (laddr != addr)
				return -pte_noip;

			/* We may not get the image we added but the image we
			 * get must have similar attributes.
			 *
			 * We're using the same filename string literal for all
			 * sections, though.
			 */
			if (section->offset != cfix->section[sec]->offset)
				return -pte_bad_image;

			if (section->size != cfix->section[sec]->size)
				return -pte_bad_image;

			errcode = pt_section_put(section);
			if (errcode < 0)
				return errcode;
		}
	}

	return 0;
}

static int worker_add_file(void *arg)
{
	struct iscache_fixture *cfix;
	int it;

	cfix = arg;
	if (!cfix)
		return -pte_internal;

	for (it = 0; it < num_iterations; ++it) {
		uint64_t offset, size, laddr;
		int sec;

		offset = it % 7 == 0 ? 0x1000 : 0x2000;
		size = it % 5 == 0 ? 0x1000 : 0x2000;
		laddr = it % 3 == 0 ? 0x1000 : 0x2000;

		for (sec = 0; sec < num_sections; ++sec) {
			struct pt_section *section;
			uint64_t addr;
			int isid, errcode;

			isid = pt_iscache_add_file(&cfix->iscache, "name",
						   offset, size, laddr);
			if (isid < 0)
				return isid;

			errcode = pt_iscache_lookup(&cfix->iscache, &section,
						    &addr, isid);
			if (errcode < 0)
				return errcode;

			if (laddr != addr)
				return -pte_noip;

			if (section->offset != offset)
				return -pte_bad_image;

			if (section->size != size)
				return -pte_bad_image;

			errcode = pt_section_put(section);
			if (errcode < 0)
				return errcode;
		}
	}

	return 0;
}

static int worker_map(void *arg)
{
	struct iscache_fixture *cfix;
	int it, sec, status;

	cfix = arg;
	if (!cfix)
		return -pte_internal;

	for (it = 0; it < num_iterations; ++it) {
		for (sec = 0; sec < num_sections; ++sec) {

			status = pt_section_map(cfix->section[sec]);
			if (status < 0)
				return status;

			status = pt_section_unmap(cfix->section[sec]);
			if (status < 0)
				return status;
		}
	}

	return 0;
}

static int worker_map_limit(void *arg)
{
	struct iscache_fixture *cfix;
	uint64_t limits[] = { 0x8000, 0x3000, 0x12000, 0x0 }, limit;
	int it, sec, errcode, lim;

	cfix = arg;
	if (!cfix)
		return -pte_internal;

	lim = 0;
	for (it = 0; it < num_iterations; ++it) {
		for (sec = 0; sec < num_sections; ++sec) {

			errcode = pt_section_map(cfix->section[sec]);
			if (errcode < 0)
				return errcode;

			errcode = pt_section_unmap(cfix->section[sec]);
			if (errcode < 0)
				return errcode;
		}

		if (it % 23 != 0)
			continue;

		limit = limits[lim++];
		lim %= sizeof(limits) / sizeof(*limits);

		errcode = pt_iscache_set_limit(&cfix->iscache, limit);
		if (errcode < 0)
			return errcode;
	}

	return 0;
}

static int worker_map_bcache(void *arg)
{
	struct iscache_fixture *cfix;
	int it, sec, status;

	cfix = arg;
	if (!cfix)
		return -pte_internal;

	for (it = 0; it < num_iterations; ++it) {
		for (sec = 0; sec < num_sections; ++sec) {
			struct pt_section *section;

			section = cfix->section[sec];

			status = pt_section_map(section);
			if (status < 0)
				return status;

			if (it % 13 == 0) {
				status = pt_section_request_bcache(section);
				if (status < 0) {
					(void) pt_section_unmap(section);
					return status;
				}
			}

			status = pt_section_unmap(section);
			if (status < 0)
				return status;
		}
	}

	return 0;
}

static int worker_add_map(void *arg)
{
	struct iscache_fixture *cfix;
	struct pt_section *section;
	int it;

	cfix = arg;
	if (!cfix)
		return -pte_internal;

	section = cfix->section[0];
	for (it = 0; it < num_iterations; ++it) {
		uint64_t laddr;
		int isid, errcode;

		laddr = (uint64_t) it << 3;

		isid = pt_iscache_add(&cfix->iscache, section, laddr);
		if (isid < 0)
			return isid;

		errcode = pt_section_map(section);
		if (errcode < 0)
			return errcode;

		errcode = pt_section_unmap(section);
		if (errcode < 0)
			return errcode;
	}

	return 0;
}

static int worker_add_clear(void *arg)
{
	struct iscache_fixture *cfix;
	struct pt_section *section;
	int it;

	cfix = arg;
	if (!cfix)
		return -pte_internal;

	section = cfix->section[0];
	for (it = 0; it < num_iterations; ++it) {
		uint64_t laddr;
		int isid, errcode;

		laddr = (uint64_t) it << 3;

		isid = pt_iscache_add(&cfix->iscache, section, laddr);
		if (isid < 0)
			return isid;

		errcode = pt_iscache_clear(&cfix->iscache);
		if (errcode < 0)
			return errcode;
	}

	return 0;
}

static int worker_add_file_map(void *arg)
{
	struct iscache_fixture *cfix;
	int it;

	cfix = arg;
	if (!cfix)
		return -pte_internal;

	for (it = 0; it < num_iterations; ++it) {
		struct pt_section *section;
		uint64_t offset, size, laddr, addr;
		int isid, errcode;

		offset = it % 7 < 4 ? 0x1000 : 0x2000;
		size = it % 5 < 3 ? 0x1000 : 0x2000;
		laddr = it % 3 < 2 ? 0x1000 : 0x2000;

		isid = pt_iscache_add_file(&cfix->iscache, "name",
					   offset, size, laddr);
		if (isid < 0)
			return isid;

		errcode = pt_iscache_lookup(&cfix->iscache, &section,
					    &addr, isid);
		if (errcode < 0)
			return errcode;

		if (addr != laddr)
			return -pte_internal;

		errcode = pt_section_map(section);
		if (errcode < 0)
			return errcode;

		errcode = pt_section_unmap(section);
		if (errcode < 0)
			return errcode;
	}

	return 0;
}

static int worker_add_file_clear(void *arg)
{
	struct iscache_fixture *cfix;
	int it;

	cfix = arg;
	if (!cfix)
		return -pte_internal;

	for (it = 0; it < num_iterations; ++it) {
		uint64_t offset, size, laddr;
		int isid, errcode;

		offset = it % 7 < 4 ? 0x1000 : 0x2000;
		size = it % 5 < 3 ? 0x1000 : 0x2000;
		laddr = it % 3 < 2 ? 0x1000 : 0x2000;

		isid = pt_iscache_add_file(&cfix->iscache, "name",
					   offset, size, laddr);
		if (isid < 0)
			return isid;

		if (it % 11 < 9)
			continue;

		errcode = pt_iscache_clear(&cfix->iscache);
		if (errcode < 0)
			return errcode;
	}

	return 0;
}

static struct ptunit_result stress(struct iscache_fixture *cfix,
				   int (*worker)(void *))
{
	int errcode;

#if defined(FEATURE_THREADS)
	{
		int thrd;

		for (thrd = 0; thrd < num_threads; ++thrd)
			ptu_test(ptunit_thrd_create, &cfix->thrd, worker, cfix);
	}
#endif /* defined(FEATURE_THREADS) */

	errcode = worker(cfix);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}
int main(int argc, char **argv)
{
	struct iscache_fixture cfix, dfix, sfix;
	struct ptunit_suite suite;

	cfix.init = cfix_init;
	cfix.fini = cfix_fini;

	dfix.init = dfix_init;
	dfix.fini = cfix_fini;

	sfix.init = sfix_init;
	sfix.fini = cfix_fini;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, init_null);
	ptu_run(suite, fini_null);
	ptu_run(suite, name_null);
	ptu_run(suite, add_null);
	ptu_run(suite, find_null);
	ptu_run(suite, lookup_null);
	ptu_run(suite, clear_null);
	ptu_run(suite, free_null);
	ptu_run(suite, add_file_null);
	ptu_run(suite, read_null);

	ptu_run_f(suite, name, dfix);
	ptu_run_f(suite, name_none, dfix);

	ptu_run_f(suite, init_fini, cfix);
	ptu_run_f(suite, add, cfix);
	ptu_run_f(suite, add_no_name, cfix);
	ptu_run_f(suite, add_file, cfix);

	ptu_run_f(suite, find, cfix);
	ptu_run_f(suite, find_empty, cfix);
	ptu_run_f(suite, find_bad_filename, cfix);
	ptu_run_f(suite, find_null_filename, cfix);
	ptu_run_f(suite, find_bad_offset, cfix);
	ptu_run_f(suite, find_bad_size, cfix);
	ptu_run_f(suite, find_bad_laddr, cfix);

	ptu_run_f(suite, lookup, cfix);
	ptu_run_f(suite, lookup_bad_isid, cfix);

	ptu_run_f(suite, clear_empty, cfix);
	ptu_run_f(suite, clear_find, cfix);
	ptu_run_f(suite, clear_lookup, cfix);

	ptu_run_f(suite, add_twice, cfix);
	ptu_run_f(suite, add_same, cfix);
	ptu_run_f(suite, add_twice_different_laddr, cfix);
	ptu_run_f(suite, add_same_different_laddr, cfix);
	ptu_run_f(suite, add_different_same_laddr, cfix);

	ptu_run_f(suite, add_file_same, cfix);
	ptu_run_f(suite, add_file_same_different_laddr, cfix);
	ptu_run_f(suite, add_file_different_same_laddr, cfix);

	ptu_run_f(suite, read, cfix);
	ptu_run_f(suite, read_truncate, cfix);
	ptu_run_f(suite, read_bad_vaddr, cfix);
	ptu_run_f(suite, read_bad_isid, cfix);

	ptu_run_f(suite, lru_map, cfix);
	ptu_run_f(suite, lru_read, cfix);
	ptu_run_f(suite, lru_map_nodup, cfix);
	ptu_run_f(suite, lru_map_too_big, cfix);
	ptu_run_f(suite, lru_map_add_front, cfix);
	ptu_run_f(suite, lru_map_move_front, cfix);
	ptu_run_f(suite, lru_map_evict, cfix);
	ptu_run_f(suite, lru_limit_evict, cfix);
	ptu_run_f(suite, lru_bcache_evict, cfix);
	ptu_run_f(suite, lru_bcache_clear, cfix);
	ptu_run_f(suite, lru_clear, cfix);

	ptu_run_fp(suite, stress, cfix, worker_add);
	ptu_run_fp(suite, stress, cfix, worker_add_file);
	ptu_run_fp(suite, stress, sfix, worker_map);
	ptu_run_fp(suite, stress, sfix, worker_map_limit);
	ptu_run_fp(suite, stress, sfix, worker_map_bcache);
	ptu_run_fp(suite, stress, cfix, worker_add_map);
	ptu_run_fp(suite, stress, cfix, worker_add_clear);
	ptu_run_fp(suite, stress, cfix, worker_add_file_map);
	ptu_run_fp(suite, stress, cfix, worker_add_file_clear);

	return ptunit_report(&suite);
}
