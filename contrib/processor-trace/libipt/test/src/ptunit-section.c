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

#include "ptunit_threads.h"
#include "ptunit_mkfile.h"

#include "pt_section.h"
#include "pt_block_cache.h"

#include "intel-pt.h"

#include <stdlib.h>
#include <stdio.h>



struct pt_image_section_cache {
	int map;
};

extern int pt_iscache_notify_map(struct pt_image_section_cache *iscache,
				 struct pt_section *section);
extern int pt_iscache_notify_resize(struct pt_image_section_cache *iscache,
				    struct pt_section *section, uint64_t size);

int pt_iscache_notify_map(struct pt_image_section_cache *iscache,
			  struct pt_section *section)
{
	if (!iscache)
		return -pte_internal;

	if (iscache->map <= 0)
		return iscache->map;

	/* Avoid recursion. */
	iscache->map = 0;

	return pt_section_map_share(section);
}

int pt_iscache_notify_resize(struct pt_image_section_cache *iscache,
			     struct pt_section *section, uint64_t size)
{
	uint64_t memsize;
	int errcode;

	if (!iscache)
		return -pte_internal;

	if (iscache->map <= 0)
		return iscache->map;

	/* Avoid recursion. */
	iscache->map = 0;

	errcode = pt_section_memsize(section, &memsize);
	if (errcode < 0)
		return errcode;

	if (size != memsize)
		return -pte_internal;

	return pt_section_map_share(section);
}

struct pt_block_cache *pt_bcache_alloc(uint64_t nentries)
{
	struct pt_block_cache *bcache;

	if (!nentries || (UINT32_MAX < nentries))
		return NULL;

	/* The cache is not really used by tests.  It suffices to allocate only
	 * the cache struct with the single default entry.
	 *
	 * We still set the number of entries to the requested size.
	 */
	bcache = malloc(sizeof(*bcache));
	if (bcache)
		bcache->nentries = (uint32_t) nentries;

	return bcache;
}

void pt_bcache_free(struct pt_block_cache *bcache)
{
	free(bcache);
}

/* A test fixture providing a temporary file and an initially NULL section. */
struct section_fixture {
	/* Threading support. */
	struct ptunit_thrd_fixture thrd;

	/* A temporary file name. */
	char *name;

	/* That file opened for writing. */
	FILE *file;

	/* The section. */
	struct pt_section *section;

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct section_fixture *);
	struct ptunit_result (*fini)(struct section_fixture *);
};

enum {
#if defined(FEATURE_THREADS)

	num_threads	= 4,

#endif /* defined(FEATURE_THREADS) */

	num_work	= 0x4000
};

static struct ptunit_result sfix_write_aux(struct section_fixture *sfix,
					   const uint8_t *buffer, size_t size)
{
	size_t written;

	written = fwrite(buffer, 1, size, sfix->file);
	ptu_uint_eq(written, size);

	fflush(sfix->file);

	return ptu_passed();
}

#define sfix_write(sfix, buffer)				\
	ptu_check(sfix_write_aux, sfix, buffer, sizeof(buffer))

static struct ptunit_result create(struct section_fixture *sfix)
{
	const char *name;
	uint8_t bytes[] = { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc };
	uint64_t offset, size;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	name = pt_section_filename(sfix->section);
	ptu_str_eq(name, sfix->name);

	offset = pt_section_offset(sfix->section);
	ptu_uint_eq(offset, 0x1ull);

	size = pt_section_size(sfix->section);
	ptu_uint_eq(size, 0x3ull);

	return ptu_passed();
}

static struct ptunit_result create_bad_offset(struct section_fixture *sfix)
{
	sfix->section = pt_mk_section(sfix->name, 0x10ull, 0x0ull);
	ptu_null(sfix->section);

	return ptu_passed();
}

static struct ptunit_result create_truncated(struct section_fixture *sfix)
{
	const char *name;
	uint8_t bytes[] = { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc };
	uint64_t offset, size;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, UINT64_MAX);
	ptu_ptr(sfix->section);

	name = pt_section_filename(sfix->section);
	ptu_str_eq(name, sfix->name);

	offset = pt_section_offset(sfix->section);
	ptu_uint_eq(offset, 0x1ull);

	size = pt_section_size(sfix->section);
	ptu_uint_eq(size, sizeof(bytes) - 1);

	return ptu_passed();
}

static struct ptunit_result create_empty(struct section_fixture *sfix)
{
	sfix->section = pt_mk_section(sfix->name, 0x0ull, 0x10ull);
	ptu_null(sfix->section);

	return ptu_passed();
}

static struct ptunit_result filename_null(void)
{
	const char *name;

	name = pt_section_filename(NULL);
	ptu_null(name);

	return ptu_passed();
}

static struct ptunit_result size_null(void)
{
	uint64_t size;

	size = pt_section_size(NULL);
	ptu_uint_eq(size, 0ull);

	return ptu_passed();
}

static struct ptunit_result memsize_null(struct section_fixture *sfix)
{
	uint64_t size;
	int errcode;

	errcode = pt_section_memsize(NULL, &size);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_section_memsize(sfix->section, NULL);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_section_memsize(NULL, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result offset_null(void)
{
	uint64_t offset;

	offset = pt_section_offset(NULL);
	ptu_uint_eq(offset, 0ull);

	return ptu_passed();
}

static struct ptunit_result get_null(void)
{
	int errcode;

	errcode = pt_section_get(NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result put_null(void)
{
	int errcode;

	errcode = pt_section_put(NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result attach_null(void)
{
	struct pt_image_section_cache iscache;
	struct pt_section section;
	int errcode;

	errcode = pt_section_attach(NULL, &iscache);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_section_attach(&section, NULL);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_section_attach(NULL, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result detach_null(void)
{
	struct pt_image_section_cache iscache;
	struct pt_section section;
	int errcode;

	errcode = pt_section_detach(NULL, &iscache);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_section_detach(&section, NULL);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_section_detach(NULL, NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result map_null(void)
{
	int errcode;

	errcode = pt_section_map(NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result unmap_null(void)
{
	int errcode;

	errcode = pt_section_unmap(NULL);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result cache_null(void)
{
	struct pt_block_cache *bcache;

	bcache = pt_section_bcache(NULL);
	ptu_null(bcache);

	return ptu_passed();
}

static struct ptunit_result get_overflow(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	sfix->section->ucount = UINT16_MAX;

	errcode = pt_section_get(sfix->section);
	ptu_int_eq(errcode, -pte_overflow);

	sfix->section->ucount = 1;

	return ptu_passed();
}

static struct ptunit_result attach_overflow(struct section_fixture *sfix)
{
	struct pt_image_section_cache iscache;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	sfix->section->acount = UINT16_MAX;

	errcode = pt_section_attach(sfix->section, &iscache);
	ptu_int_eq(errcode, -pte_overflow);

	sfix->section->acount = 0;

	return ptu_passed();
}

static struct ptunit_result attach_bad_ucount(struct section_fixture *sfix)
{
	struct pt_image_section_cache iscache;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	sfix->section->acount = 2;

	errcode = pt_section_attach(sfix->section, &iscache);
	ptu_int_eq(errcode, -pte_internal);

	sfix->section->acount = 0;

	return ptu_passed();
}

static struct ptunit_result map_change(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	sfix_write(sfix, bytes);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, -pte_bad_image);

	return ptu_passed();
}

static struct ptunit_result map_put(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_put(sfix->section);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result unmap_nomap(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, -pte_nomap);

	return ptu_passed();
}

static struct ptunit_result map_overflow(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	sfix->section->mcount = UINT16_MAX;

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, -pte_overflow);

	sfix->section->mcount = 0;

	return ptu_passed();
}

static struct ptunit_result get_put(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_get(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_get(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_put(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_put(sfix->section);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result attach_detach(struct section_fixture *sfix)
{
	struct pt_image_section_cache iscache;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	sfix->section->ucount += 2;

	errcode = pt_section_attach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_attach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_detach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_detach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	sfix->section->ucount -= 2;

	return ptu_passed();
}

static struct ptunit_result attach_bad_iscache(struct section_fixture *sfix)
{
	struct pt_image_section_cache iscache, bad;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	sfix->section->ucount += 2;

	errcode = pt_section_attach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_attach(sfix->section, &bad);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_section_detach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	sfix->section->ucount -= 2;

	return ptu_passed();
}

static struct ptunit_result detach_bad_iscache(struct section_fixture *sfix)
{
	struct pt_image_section_cache iscache, bad;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_attach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_detach(sfix->section, &bad);
	ptu_int_eq(errcode, -pte_internal);

	errcode = pt_section_detach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result map_unmap(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result attach_map(struct section_fixture *sfix)
{
	struct pt_image_section_cache iscache;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	iscache.map = 0;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_attach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	ptu_uint_eq(sfix->section->mcount, 2);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_detach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result attach_bad_map(struct section_fixture *sfix)
{
	struct pt_image_section_cache iscache;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	iscache.map = -pte_eos;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_attach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, -pte_eos);

	errcode = pt_section_detach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result attach_map_overflow(struct section_fixture *sfix)
{
	struct pt_image_section_cache iscache;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	iscache.map = 1;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_attach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	sfix->section->mcount = UINT16_MAX - 1;

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, -pte_overflow);

	errcode = pt_section_detach(sfix->section, &iscache);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result read(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	status = pt_section_map(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, buffer, 2, 0x0ull);
	ptu_int_eq(status, 2);
	ptu_uint_eq(buffer[0], bytes[1]);
	ptu_uint_eq(buffer[1], bytes[2]);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_section_unmap(sfix->section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result read_null(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	uint8_t buffer[] = { 0xcc };
	int status;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	status = pt_section_map(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, NULL, 1, 0x0ull);
	ptu_int_eq(status, -pte_internal);
	ptu_uint_eq(buffer[0], 0xcc);

	status = pt_section_read(NULL, buffer, 1, 0x0ull);
	ptu_int_eq(status, -pte_internal);
	ptu_uint_eq(buffer[0], 0xcc);

	status = pt_section_unmap(sfix->section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result read_offset(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	status = pt_section_map(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, buffer, 2, 0x1ull);
	ptu_int_eq(status, 2);
	ptu_uint_eq(buffer[0], bytes[2]);
	ptu_uint_eq(buffer[1], bytes[3]);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_section_unmap(sfix->section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result read_truncated(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 }, buffer[] = { 0xcc, 0xcc };
	int status;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	status = pt_section_map(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, buffer, 2, 0x2ull);
	ptu_int_eq(status, 1);
	ptu_uint_eq(buffer[0], bytes[3]);
	ptu_uint_eq(buffer[1], 0xcc);

	status = pt_section_unmap(sfix->section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result read_from_truncated(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 }, buffer[] = { 0xcc, 0xcc };
	int status;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x2ull, 0x10ull);
	ptu_ptr(sfix->section);

	status = pt_section_map(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, buffer, 2, 0x1ull);
	ptu_int_eq(status, 1);
	ptu_uint_eq(buffer[0], bytes[3]);
	ptu_uint_eq(buffer[1], 0xcc);

	status = pt_section_unmap(sfix->section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result read_nomem(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 }, buffer[] = { 0xcc };
	int status;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	status = pt_section_map(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, buffer, 1, 0x3ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_uint_eq(buffer[0], 0xcc);

	status = pt_section_unmap(sfix->section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result read_overflow(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 }, buffer[] = { 0xcc };
	int status;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	status = pt_section_map(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, buffer, 1,
				 0xffffffffffff0000ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_uint_eq(buffer[0], 0xcc);

	status = pt_section_unmap(sfix->section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result read_overflow_32bit(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 }, buffer[] = { 0xcc };
	int status;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	status = pt_section_map(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, buffer, 1,
				 0xff00000000ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_uint_eq(buffer[0], 0xcc);

	status = pt_section_unmap(sfix->section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result read_nomap(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 }, buffer[] = { 0xcc };
	int status;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	status = pt_section_read(sfix->section, buffer, 1, 0x0ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_uint_eq(buffer[0], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_unmap_map(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	status = pt_section_map(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, buffer, 2, 0x0ull);
	ptu_int_eq(status, 2);
	ptu_uint_eq(buffer[0], bytes[1]);
	ptu_uint_eq(buffer[1], bytes[2]);
	ptu_uint_eq(buffer[2], 0xcc);

	memset(buffer, 0xcc, sizeof(buffer));

	status = pt_section_unmap(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, buffer, 2, 0x0ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_uint_eq(buffer[0], 0xcc);
	ptu_uint_eq(buffer[1], 0xcc);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_section_map(sfix->section);
	ptu_int_eq(status, 0);

	status = pt_section_read(sfix->section, buffer, 2, 0x0ull);
	ptu_int_eq(status, 2);
	ptu_uint_eq(buffer[0], bytes[1]);
	ptu_uint_eq(buffer[1], bytes[2]);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_section_unmap(sfix->section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static int worker_read(void *arg)
{
	struct section_fixture *sfix;
	int it, errcode;

	sfix = arg;
	if (!sfix)
		return -pte_internal;

	for (it = 0; it < num_work; ++it) {
		uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
		int read;

		errcode = pt_section_get(sfix->section);
		if (errcode < 0)
			return errcode;

		errcode = pt_section_map(sfix->section);
		if (errcode < 0)
			goto out_put;

		read = pt_section_read(sfix->section, buffer, 2, 0x0ull);
		if (read < 0)
			goto out_unmap;

		errcode = -pte_invalid;
		if ((read != 2) || (buffer[0] != 0x2) || (buffer[1] != 0x4))
			goto out_unmap;

		errcode = pt_section_unmap(sfix->section);
		if (errcode < 0)
			goto out_put;

		errcode = pt_section_put(sfix->section);
		if (errcode < 0)
			return errcode;
	}

	return 0;

out_unmap:
	(void) pt_section_unmap(sfix->section);

out_put:
	(void) pt_section_put(sfix->section);
	return errcode;
}

static int worker_bcache(void *arg)
{
	struct section_fixture *sfix;
	int it, errcode;

	sfix = arg;
	if (!sfix)
		return -pte_internal;

	errcode = pt_section_get(sfix->section);
	if (errcode < 0)
		return errcode;

	for (it = 0; it < num_work; ++it) {
		struct pt_block_cache *bcache;

		errcode = pt_section_map(sfix->section);
		if (errcode < 0)
			goto out_put;

		errcode = pt_section_request_bcache(sfix->section);
		if (errcode < 0)
			goto out_unmap;

		bcache = pt_section_bcache(sfix->section);
		if (!bcache) {
			errcode = -pte_nomem;
			goto out_unmap;
		}

		errcode = pt_section_unmap(sfix->section);
		if (errcode < 0)
			goto out_put;
	}

	return pt_section_put(sfix->section);

out_unmap:
	(void) pt_section_unmap(sfix->section);

out_put:
	(void) pt_section_put(sfix->section);
	return errcode;
}

static struct ptunit_result stress(struct section_fixture *sfix,
				   int (*worker)(void *))
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

#if defined(FEATURE_THREADS)
	{
		int thrd;

		for (thrd = 0; thrd < num_threads; ++thrd)
			ptu_test(ptunit_thrd_create, &sfix->thrd, worker, sfix);
	}
#endif /* defined(FEATURE_THREADS) */

	errcode = worker(sfix);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result init_no_bcache(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	struct pt_block_cache *bcache;
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	bcache = pt_section_bcache(sfix->section);
	ptu_null(bcache);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result bcache_alloc_free(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	struct pt_block_cache *bcache;
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_alloc_bcache(sfix->section);
	ptu_int_eq(errcode, 0);

	bcache = pt_section_bcache(sfix->section);
	ptu_ptr(bcache);
	ptu_uint_eq(bcache->nentries, sfix->section->size);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	bcache = pt_section_bcache(sfix->section);
	ptu_null(bcache);

	return ptu_passed();
}

static struct ptunit_result bcache_alloc_twice(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_alloc_bcache(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_alloc_bcache(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result bcache_alloc_nomap(struct section_fixture *sfix)
{
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_alloc_bcache(sfix->section);
	ptu_int_eq(errcode, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result memsize_nomap(struct section_fixture *sfix)
{
	uint64_t memsize;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_memsize(sfix->section, &memsize);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(memsize, 0ull);

	return ptu_passed();
}

static struct ptunit_result memsize_unmap(struct section_fixture *sfix)
{
	uint64_t memsize;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_memsize(sfix->section, &memsize);
	ptu_int_eq(errcode, 0);
	ptu_uint_eq(memsize, 0ull);

	return ptu_passed();
}

static struct ptunit_result memsize_map_nobcache(struct section_fixture *sfix)
{
	uint64_t memsize;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	memsize = 0xfefefefefefefefeull;

	errcode = pt_section_memsize(sfix->section, &memsize);
	ptu_int_eq(errcode, 0);
	ptu_uint_ge(memsize, 0ull);
	ptu_uint_le(memsize, 0x2000ull);

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result memsize_map_bcache(struct section_fixture *sfix)
{
	uint64_t memsize;
	uint8_t bytes[] = { 0xcc, 0x2, 0x4, 0x6 };
	int errcode;

	sfix_write(sfix, bytes);

	sfix->section = pt_mk_section(sfix->name, 0x1ull, 0x3ull);
	ptu_ptr(sfix->section);

	errcode = pt_section_map(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_alloc_bcache(sfix->section);
	ptu_int_eq(errcode, 0);

	errcode = pt_section_memsize(sfix->section, &memsize);
	ptu_int_eq(errcode, 0);
	ptu_uint_ge(memsize,
		    sfix->section->size * sizeof(struct pt_bcache_entry));

	errcode = pt_section_unmap(sfix->section);
	ptu_int_eq(errcode, 0);

	return ptu_passed();
}

static struct ptunit_result sfix_init(struct section_fixture *sfix)
{
	int errcode;

	sfix->section = NULL;
	sfix->file = NULL;
	sfix->name = NULL;

	errcode = ptunit_mkfile(&sfix->file, &sfix->name, "wb");
	ptu_int_eq(errcode, 0);

	ptu_test(ptunit_thrd_init, &sfix->thrd);

	return ptu_passed();
}

static struct ptunit_result sfix_fini(struct section_fixture *sfix)
{
	int thrd;

	ptu_test(ptunit_thrd_fini, &sfix->thrd);

	for (thrd = 0; thrd < sfix->thrd.nthreads; ++thrd)
		ptu_int_eq(sfix->thrd.result[thrd], 0);

	if (sfix->section) {
		pt_section_put(sfix->section);
		sfix->section = NULL;
	}

	if (sfix->file) {
		fclose(sfix->file);
		sfix->file = NULL;

		if (sfix->name)
			remove(sfix->name);
	}

	if (sfix->name) {
		free(sfix->name);
		sfix->name = NULL;
	}

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct section_fixture sfix;
	struct ptunit_suite suite;

	sfix.init = sfix_init;
	sfix.fini = sfix_fini;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run_f(suite, create, sfix);
	ptu_run_f(suite, create_bad_offset, sfix);
	ptu_run_f(suite, create_truncated, sfix);
	ptu_run_f(suite, create_empty, sfix);

	ptu_run(suite, filename_null);
	ptu_run(suite, offset_null);
	ptu_run(suite, size_null);
	ptu_run(suite, get_null);
	ptu_run(suite, put_null);
	ptu_run(suite, attach_null);
	ptu_run(suite, detach_null);
	ptu_run(suite, map_null);
	ptu_run(suite, unmap_null);
	ptu_run(suite, cache_null);

	ptu_run_f(suite, get_overflow, sfix);
	ptu_run_f(suite, attach_overflow, sfix);
	ptu_run_f(suite, attach_bad_ucount, sfix);
	ptu_run_f(suite, map_change, sfix);
	ptu_run_f(suite, map_put, sfix);
	ptu_run_f(suite, unmap_nomap, sfix);
	ptu_run_f(suite, map_overflow, sfix);
	ptu_run_f(suite, get_put, sfix);
	ptu_run_f(suite, attach_detach, sfix);
	ptu_run_f(suite, attach_bad_iscache, sfix);
	ptu_run_f(suite, detach_bad_iscache, sfix);
	ptu_run_f(suite, map_unmap, sfix);
	ptu_run_f(suite, attach_map, sfix);
	ptu_run_f(suite, attach_bad_map, sfix);
	ptu_run_f(suite, attach_map_overflow, sfix);
	ptu_run_f(suite, read, sfix);
	ptu_run_f(suite, read_null, sfix);
	ptu_run_f(suite, read_offset, sfix);
	ptu_run_f(suite, read_truncated, sfix);
	ptu_run_f(suite, read_from_truncated, sfix);
	ptu_run_f(suite, read_nomem, sfix);
	ptu_run_f(suite, read_overflow, sfix);
	ptu_run_f(suite, read_overflow_32bit, sfix);
	ptu_run_f(suite, read_nomap, sfix);
	ptu_run_f(suite, read_unmap_map, sfix);

	ptu_run_f(suite, init_no_bcache, sfix);
	ptu_run_f(suite, bcache_alloc_free, sfix);
	ptu_run_f(suite, bcache_alloc_twice, sfix);
	ptu_run_f(suite, bcache_alloc_nomap, sfix);

	ptu_run_f(suite, memsize_null, sfix);
	ptu_run_f(suite, memsize_nomap, sfix);
	ptu_run_f(suite, memsize_unmap, sfix);
	ptu_run_f(suite, memsize_map_nobcache, sfix);
	ptu_run_f(suite, memsize_map_bcache, sfix);

	ptu_run_fp(suite, stress, sfix, worker_bcache);
	ptu_run_fp(suite, stress, sfix, worker_read);

	return ptunit_report(&suite);
}
