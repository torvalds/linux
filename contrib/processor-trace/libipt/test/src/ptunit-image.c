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

#include "pt_image.h"
#include "pt_section.h"
#include "pt_mapped_section.h"

#include "intel-pt.h"


struct image_fixture;

/* A test mapping. */
struct ifix_mapping {
	/* The contents. */
	uint8_t content[0x10];

	/* The size - between 0 and sizeof(content). */
	uint64_t size;

	/* An artificial error code to be injected into pt_section_read().
	 *
	 * If @errcode is non-zero, pt_section_read() fails with @errcode.
	 */
	int errcode;
};

/* A test file status - turned into a section status. */
struct ifix_status {
	/* Delete indication:
	 * - zero if initialized and not (yet) deleted
	 * - non-zero if deleted and not (re-)initialized
	 */
	int deleted;

	/* Put with use-count of zero indication. */
	int bad_put;

	/* The test mapping to be used. */
	struct ifix_mapping *mapping;

	/* A link back to the test fixture providing this section. */
	struct image_fixture *ifix;
};

enum {
	ifix_nsecs = 5
};

/* A fake image section cache. */
struct pt_image_section_cache {
	/* The cached sections. */
	struct pt_section *section[ifix_nsecs];

	/* Their load addresses. */
	uint64_t laddr[ifix_nsecs];

	/* The number of used sections. */
	int nsecs;
};

extern int pt_iscache_lookup(struct pt_image_section_cache *iscache,
			     struct pt_section **section, uint64_t *laddr,
			     int isid);


/* A test fixture providing an image, test sections, and asids. */
struct image_fixture {
	/* The image. */
	struct pt_image image;

	/* The test states. */
	struct ifix_status status[ifix_nsecs];

	/* The test mappings. */
	struct ifix_mapping mapping[ifix_nsecs];

	/* The sections. */
	struct pt_section section[ifix_nsecs];

	/* The asids. */
	struct pt_asid asid[3];

	/* The number of used sections/mappings/states. */
	int nsecs;

	/* An initially empty image as destination for image copies. */
	struct pt_image copy;

	/* A test section cache. */
	struct pt_image_section_cache iscache;

	/* The test fixture initialization and finalization functions. */
	struct ptunit_result (*init)(struct image_fixture *);
	struct ptunit_result (*fini)(struct image_fixture *);
};

static void ifix_init_section(struct pt_section *section, char *filename,
			      struct ifix_status *status,
			      struct ifix_mapping *mapping,
			      struct image_fixture *ifix)
{
	uint8_t i;

	memset(section, 0, sizeof(*section));

	section->filename = filename;
	section->status = status;
	section->size = mapping->size = sizeof(mapping->content);
	section->offset = 0x10;

	for (i = 0; i < mapping->size; ++i)
		mapping->content[i] = i;

	status->deleted = 0;
	status->bad_put = 0;
	status->mapping = mapping;
	status->ifix = ifix;
}

static int ifix_add_section(struct image_fixture *ifix, char *filename)
{
	int index;

	if (!ifix)
		return -pte_internal;

	index = ifix->nsecs;
	if (ifix_nsecs <= index)
		return -pte_internal;

	ifix_init_section(&ifix->section[index], filename, &ifix->status[index],
			  &ifix->mapping[index], ifix);

	ifix->nsecs += 1;
	return index;
}

static int ifix_cache_section(struct image_fixture *ifix,
			      struct pt_section *section, uint64_t laddr)
{
	int index;

	if (!ifix)
		return -pte_internal;

	index = ifix->iscache.nsecs;
	if (ifix_nsecs <= index)
		return -pte_internal;

	ifix->iscache.section[index] = section;
	ifix->iscache.laddr[index] = laddr;

	index += 1;
	ifix->iscache.nsecs = index;

	return index;
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

struct pt_section *pt_mk_section(const char *file, uint64_t offset,
				 uint64_t size)
{
	(void) file;
	(void) offset;
	(void) size;

	/* This function is not used by our tests. */
	return NULL;
}

int pt_section_get(struct pt_section *section)
{
	if (!section)
		return -pte_internal;

	section->ucount += 1;
	return 0;
}

int pt_section_put(struct pt_section *section)
{
	struct ifix_status *status;
	uint16_t ucount;

	if (!section)
		return -pte_internal;

	status = section->status;
	if (!status)
		return -pte_internal;

	ucount = section->ucount;
	if (!ucount) {
		status->bad_put += 1;

		return -pte_internal;
	}

	ucount = --section->ucount;
	if (!ucount) {
		status->deleted += 1;

		if (status->deleted > 1)
			return -pte_internal;
	}

	return 0;
}

int pt_iscache_lookup(struct pt_image_section_cache *iscache,
		      struct pt_section **section, uint64_t *laddr, int isid)
{
	if (!iscache || !section || !laddr)
		return -pte_internal;

	if (!isid || iscache->nsecs < isid)
		return -pte_bad_image;

	isid -= 1;

	*section = iscache->section[isid];
	*laddr = iscache->laddr[isid];

	return pt_section_get(*section);
}

static int ifix_unmap(struct pt_section *section)
{
	uint16_t mcount;

	if (!section)
		return -pte_internal;

	mcount = section->mcount;
	if (!mcount)
		return -pte_internal;

	if (!section->mapping)
		return -pte_internal;

	mcount = --section->mcount;
	if (!mcount)
		section->mapping = NULL;

	return 0;
}

static int ifix_read(const struct pt_section *section, uint8_t *buffer,
		     uint16_t size, uint64_t offset)
{
	struct ifix_mapping *mapping;
	uint64_t begin, end;

	if (!section || !buffer)
		return -pte_internal;

	begin = offset;
	end = begin + size;

	if (end < begin)
		return -pte_nomap;

	mapping = section->mapping;
	if (!mapping)
		return -pte_nomap;

	if (mapping->errcode)
		return mapping->errcode;

	if (mapping->size <= begin)
		return -pte_nomap;

	if (mapping->size < end) {
		end = mapping->size;
		size = (uint16_t) (end - begin);
	}

	memcpy(buffer, &mapping->content[begin], size);

	return size;
}

int pt_section_map(struct pt_section *section)
{
	struct ifix_status *status;
	uint16_t mcount;

	if (!section)
		return -pte_internal;

	mcount = section->mcount++;
	if (mcount)
		return 0;

	if (section->mapping)
		return -pte_internal;

	status = section->status;
	if (!status)
		return -pte_internal;

	section->mapping = status->mapping;
	section->unmap = ifix_unmap;
	section->read = ifix_read;

	return 0;
}

int pt_section_on_map_lock(struct pt_section *section)
{
	if (!section)
		return -pte_internal;

	return 0;
}

int pt_section_unmap(struct pt_section *section)
{
	if (!section)
		return -pte_internal;

	if (!section->unmap)
		return -pte_nomap;

	return section->unmap(section);
}

int pt_section_read(const struct pt_section *section, uint8_t *buffer,
		    uint16_t size, uint64_t offset)
{
	if (!section)
		return -pte_internal;

	if (!section->read)
		return -pte_nomap;

	return section->read(section, buffer, size, offset);
}

/* A test read memory callback. */
static int image_readmem_callback(uint8_t *buffer, size_t size,
				  const struct pt_asid *asid,
				  uint64_t ip, void *context)
{
	const uint8_t *memory;
	size_t idx;

	(void) asid;

	if (!buffer)
		return -pte_invalid;

	/* We use a constant offset of 0x3000. */
	if (ip < 0x3000ull)
		return -pte_nomap;

	ip -= 0x3000ull;

	memory = (const uint8_t *) context;
	if (!memory)
		return -pte_internal;

	for (idx = 0; idx < size; ++idx)
		buffer[idx] = memory[ip + idx];

	return (int) idx;
}

static struct ptunit_result init(void)
{
	struct pt_image image;

	memset(&image, 0xcd, sizeof(image));

	pt_image_init(&image, NULL);
	ptu_null(image.name);
	ptu_null(image.sections);
	ptu_null((void *) (uintptr_t) image.readmem.callback);
	ptu_null(image.readmem.context);

	return ptu_passed();
}

static struct ptunit_result init_name(struct image_fixture *ifix)
{
	memset(&ifix->image, 0xcd, sizeof(ifix->image));

	pt_image_init(&ifix->image, "image-name");
	ptu_str_eq(ifix->image.name, "image-name");
	ptu_null(ifix->image.sections);
	ptu_null((void *) (uintptr_t) ifix->image.readmem.callback);
	ptu_null(ifix->image.readmem.context);

	return ptu_passed();
}

static struct ptunit_result init_null(void)
{
	pt_image_init(NULL, NULL);

	return ptu_passed();
}

static struct ptunit_result fini(void)
{
	struct ifix_mapping mapping;
	struct ifix_status status;
	struct pt_section section;
	struct pt_image image;
	struct pt_asid asid;
	int errcode;

	pt_asid_init(&asid);
	ifix_init_section(&section, NULL, &status, &mapping, NULL);

	pt_image_init(&image, NULL);
	errcode = pt_image_add(&image, &section, &asid, 0x0ull, 0);
	ptu_int_eq(errcode, 0);

	pt_image_fini(&image);
	ptu_int_eq(section.ucount, 0);
	ptu_int_eq(section.mcount, 0);
	ptu_int_eq(status.deleted, 1);
	ptu_int_eq(status.bad_put, 0);

	return ptu_passed();
}

static struct ptunit_result fini_empty(void)
{
	struct pt_image image;

	pt_image_init(&image, NULL);
	pt_image_fini(&image);

	return ptu_passed();
}

static struct ptunit_result fini_null(void)
{
	pt_image_fini(NULL);

	return ptu_passed();
}

static struct ptunit_result name(struct image_fixture *ifix)
{
	const char *name;

	pt_image_init(&ifix->image, "image-name");

	name = pt_image_name(&ifix->image);
	ptu_str_eq(name, "image-name");

	return ptu_passed();
}

static struct ptunit_result name_none(void)
{
	struct pt_image image;
	const char *name;

	pt_image_init(&image, NULL);

	name = pt_image_name(&image);
	ptu_null(name);

	return ptu_passed();
}

static struct ptunit_result name_null(void)
{
	const char *name;

	name = pt_image_name(NULL);
	ptu_null(name);

	return ptu_passed();
}

static struct ptunit_result read_empty(struct image_fixture *ifix)
{
	struct pt_asid asid;
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	pt_asid_init(&asid);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, sizeof(buffer),
			       &asid, 0x1000ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_int_eq(isid, -1);
	ptu_uint_eq(buffer[0], 0xcc);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result overlap_front(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1001ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[0],
			      0x1000ull, 2);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1010ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x0f);
	ptu_uint_eq(buffer[1], 0xcc);

	buffer[0] = 0xcc;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x100full);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x0f);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result overlap_back(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[0],
			      0x1001ull, 2);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1000ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x00);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1010ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x0f);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1001ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x00);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result overlap_multiple(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1010ull, 2);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1008ull, 3);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1007ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x07);
	ptu_uint_eq(buffer[1], 0xcc);

	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1008ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x00);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1017ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x0f);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1018ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x08);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result overlap_mid(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 1);
	ptu_int_eq(status, 0);

	ifix->section[1].size = 0x8;
	ifix->mapping[1].size = 0x8;
	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[0],
			      0x1004ull, 2);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1003ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1004ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x00);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x100bull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x07);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x100cull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x0c);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result contained(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	ifix->section[0].size = 0x8;
	ifix->mapping[0].size = 0x8;
	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1004ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[0],
			      0x1000ull, 2);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1008ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x08);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result contained_multiple(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	ifix->section[0].size = 0x2;
	ifix->mapping[0].size = 0x2;
	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1004ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1008ull, 2);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[0],
			      0x1000ull, 3);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1004ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x04);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1008ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x08);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result contained_back(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	ifix->section[0].size = 0x8;
	ifix->mapping[0].size = 0x8;
	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1004ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x100cull, 2);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[0],
			      0x1000ull, 3);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1004ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x04);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x100cull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x0c);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x100full);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x0f);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1010ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x04);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result same(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 1);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1008ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x08);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result same_different_isid(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 2);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1008ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x08);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result same_different_offset(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc }, i;
	int status, isid, index;

	/* Add another section from a different part of the same file as an
	 * existing section.
	 */
	index = ifix_add_section(ifix, ifix->section[0].filename);
	ptu_int_gt(index, 0);

	ifix->section[index].offset = ifix->section[0].offset + 0x10;
	ptu_uint_eq(ifix->section[index].size, ifix->section[0].size);

	/* Change the content of the new section so we can distinguish them. */
	for (i = 0; i < ifix->mapping[index].size; ++i)
		ifix->mapping[index].content[i] += 0x10;


	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 0);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[index],
			      &ifix->asid[0], 0x1000ull, 0);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1000ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 0);
	ptu_uint_eq(buffer[0], 0x10);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x100full);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 0);
	ptu_uint_eq(buffer[0], 0x1f);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result adjacent(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[0],
			      0x1000ull - ifix->section[1].size, 2);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[2], &ifix->asid[0],
			      0x1000ull + ifix->section[0].size, 3);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1000ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x00);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0xfffull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0],
		    ifix->mapping[1].content[ifix->mapping[1].size - 1]);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1000ull + ifix->section[0].size);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x00);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_null(struct image_fixture *ifix)
{
	uint8_t buffer;
	int status, isid;

	status = pt_image_read(NULL, &isid, &buffer, 1, &ifix->asid[0],
			       0x1000ull);
	ptu_int_eq(status, -pte_internal);

	status = pt_image_read(&ifix->image, NULL, &buffer, 1, &ifix->asid[0],
			       0x1000ull);
	ptu_int_eq(status, -pte_internal);

	status = pt_image_read(&ifix->image, &isid, NULL, 1, &ifix->asid[0],
			       0x1000ull);
	ptu_int_eq(status, -pte_internal);

	status = pt_image_read(&ifix->image, &isid, &buffer, 1, NULL,
			       0x1000ull);
	ptu_int_eq(status, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result read(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[1],
			       0x2003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_asid(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[1],
			      0x1008ull, 2);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1009ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x09);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[1],
			       0x1009ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_bad_asid(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, sizeof(buffer),
			       &ifix->asid[0], 0x2003ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_int_eq(isid, -1);
	ptu_uint_eq(buffer[0], 0xcc);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_null_asid(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, NULL, 0x2003ull);
	ptu_int_eq(status, -pte_internal);
	ptu_int_eq(isid, -1);
	ptu_uint_eq(buffer[0], 0xcc);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_callback(struct image_fixture *ifix)
{
	uint8_t memory[] = { 0xdd, 0x01, 0x02, 0xdd };
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	status = pt_image_set_callback(&ifix->image, image_readmem_callback,
				       memory);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x3001ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 0);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_nomem(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, sizeof(buffer),
			       &ifix->asid[1], 0x1010ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_int_eq(isid, -1);
	ptu_uint_eq(buffer[0], 0xcc);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_truncated(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, sizeof(buffer),
			       &ifix->asid[0], 0x100full);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x0f);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_error(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc };
	int status, isid;

	ifix->mapping[0].errcode = -pte_nosync;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1000ull);
	ptu_int_eq(status, -pte_nosync);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0xcc);

	return ptu_passed();
}

static struct ptunit_result read_spurious_error(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1000ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x00);
	ptu_uint_eq(buffer[1], 0xcc);

	ifix->mapping[0].errcode = -pte_nosync;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 1, &ifix->asid[0],
			       0x1005ull);
	ptu_int_eq(status, -pte_nosync);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x00);

	return ptu_passed();
}

static struct ptunit_result remove_section(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1001ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_image_remove(&ifix->image, &ifix->section[0],
				 &ifix->asid[0], 0x1000ull);
	ptu_int_eq(status, 0);

	ptu_int_ne(ifix->status[0].deleted, 0);
	ptu_int_eq(ifix->status[1].deleted, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, sizeof(buffer),
			       &ifix->asid[0], 0x1003ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_int_eq(isid, -1);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[1],
			       0x2003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result remove_bad_vaddr(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1001ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_image_remove(&ifix->image, &ifix->section[0],
				 &ifix->asid[0], 0x2000ull);
	ptu_int_eq(status, -pte_bad_image);

	ptu_int_eq(ifix->status[0].deleted, 0);
	ptu_int_eq(ifix->status[1].deleted, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[1],
			       0x2005ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x05);
	ptu_uint_eq(buffer[1], 0x06);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result remove_bad_asid(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1001ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_image_remove(&ifix->image, &ifix->section[0],
				 &ifix->asid[1], 0x1000ull);
	ptu_int_eq(status, -pte_bad_image);

	ptu_int_eq(ifix->status[0].deleted, 0);
	ptu_int_eq(ifix->status[1].deleted, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[1],
			       0x2005ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x05);
	ptu_uint_eq(buffer[1], 0x06);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result remove_by_filename(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1001ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_image_remove_by_filename(&ifix->image,
					     ifix->section[0].filename,
					     &ifix->asid[0]);
	ptu_int_eq(status, 1);

	ptu_int_ne(ifix->status[0].deleted, 0);
	ptu_int_eq(ifix->status[1].deleted, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, sizeof(buffer),
			       &ifix->asid[0], 0x1003ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_int_eq(isid, -1);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[1],
			       0x2003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result
remove_by_filename_bad_asid(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1001ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_image_remove_by_filename(&ifix->image,
					     ifix->section[0].filename,
					     &ifix->asid[1]);
	ptu_int_eq(status, 0);

	ptu_int_eq(ifix->status[0].deleted, 0);
	ptu_int_eq(ifix->status[1].deleted, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[1],
			       0x2005ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x05);
	ptu_uint_eq(buffer[1], 0x06);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result remove_none_by_filename(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	status = pt_image_remove_by_filename(&ifix->image, "bad-name",
					     &ifix->asid[0]);
	ptu_int_eq(status, 0);

	ptu_int_eq(ifix->status[0].deleted, 0);
	ptu_int_eq(ifix->status[1].deleted, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[1],
			       0x2001ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result remove_all_by_filename(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	ifix->section[0].filename = "same-name";
	ifix->section[1].filename = "same-name";

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[0],
			      0x2000ull, 2);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1001ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_image_remove_by_filename(&ifix->image, "same-name",
					     &ifix->asid[0]);
	ptu_int_eq(status, 2);

	ptu_int_ne(ifix->status[0].deleted, 0);
	ptu_int_ne(ifix->status[1].deleted, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, sizeof(buffer),
			       &ifix->asid[0], 0x1003ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_int_eq(isid, -1);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, sizeof(buffer),
			       &ifix->asid[0], 0x2003ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_int_eq(isid, -1);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result remove_by_asid(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1001ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 10);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	status = pt_image_remove_by_asid(&ifix->image, &ifix->asid[0]);
	ptu_int_eq(status, 1);

	ptu_int_ne(ifix->status[0].deleted, 0);
	ptu_int_eq(ifix->status[1].deleted, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, sizeof(buffer),
			       &ifix->asid[0], 0x1003ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_int_eq(isid, -1);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0x02);
	ptu_uint_eq(buffer[2], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[1],
			       0x2003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result copy_empty(struct image_fixture *ifix)
{
	struct pt_asid asid;
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	pt_asid_init(&asid);

	status = pt_image_copy(&ifix->copy, &ifix->image);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, sizeof(buffer),
			       &asid, 0x1000ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_int_eq(isid, -1);
	ptu_uint_eq(buffer[0], 0xcc);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result copy(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	status = pt_image_copy(&ifix->copy, &ifix->image);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 2, &ifix->asid[1],
			       0x2003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result copy_self(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	status = pt_image_copy(&ifix->image, &ifix->image);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[1],
			       0x2003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result copy_shrink(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->copy, &ifix->section[1], &ifix->asid[1],
			      0x2000ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_copy(&ifix->copy, &ifix->image);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 2, &ifix->asid[1],
			       0x2003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 11);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result copy_split(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->copy, &ifix->section[0], &ifix->asid[0],
			      0x2000ull, 1);
	ptu_int_eq(status, 0);

	ifix->section[1].size = 0x7;
	ifix->mapping[1].size = 0x7;

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[0],
			      0x2001ull, 2);
	ptu_int_eq(status, 0);

	ifix->section[2].size = 0x8;
	ifix->mapping[2].size = 0x8;

	status = pt_image_add(&ifix->image, &ifix->section[2], &ifix->asid[0],
			      0x2008ull, 3);
	ptu_int_eq(status, 0);

	status = pt_image_copy(&ifix->copy, &ifix->image);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 1, &ifix->asid[0],
			       0x2003ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x02);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 1, &ifix->asid[0],
			       0x2009ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x01);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 1, &ifix->asid[0],
			       0x2000ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x00);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result copy_merge(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	ifix->section[1].size = 0x8;
	ifix->mapping[1].size = 0x8;

	status = pt_image_add(&ifix->copy, &ifix->section[1], &ifix->asid[0],
			      0x2000ull, 1);
	ptu_int_eq(status, 0);

	ifix->section[2].size = 0x8;
	ifix->mapping[2].size = 0x8;

	status = pt_image_add(&ifix->copy, &ifix->section[2], &ifix->asid[0],
			      0x2008ull, 2);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x2000ull, 3);
	ptu_int_eq(status, 0);

	status = pt_image_copy(&ifix->copy, &ifix->image);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 1, &ifix->asid[0],
			       0x2003ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 1, &ifix->asid[0],
			       0x200aull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x0a);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result copy_overlap(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add(&ifix->copy, &ifix->section[0], &ifix->asid[0],
			      0x2000ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->copy, &ifix->section[1], &ifix->asid[0],
			      0x2010ull, 2);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[2], &ifix->asid[0],
			      0x2008ull, 3);
	ptu_int_eq(status, 0);

	status = pt_image_copy(&ifix->copy, &ifix->image);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 1, &ifix->asid[0],
			       0x2003ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 1);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 1, &ifix->asid[0],
			       0x200aull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x02);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 1, &ifix->asid[0],
			       0x2016ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 3);
	ptu_uint_eq(buffer[0], 0x0e);
	ptu_uint_eq(buffer[1], 0xcc);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 1, &ifix->asid[0],
			       0x2019ull);
	ptu_int_eq(status, 1);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x09);
	ptu_uint_eq(buffer[1], 0xcc);

	return ptu_passed();
}

static struct ptunit_result copy_replace(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	ifix->section[0].size = 0x8;
	ifix->mapping[0].size = 0x8;

	status = pt_image_add(&ifix->copy, &ifix->section[0], &ifix->asid[0],
			      0x1004ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[0],
			      0x1000ull, 2);
	ptu_int_eq(status, 0);

	status = pt_image_copy(&ifix->copy, &ifix->image);
	ptu_int_eq(status, 0);

	isid = -1;
	status = pt_image_read(&ifix->copy, &isid, buffer, 2, &ifix->asid[0],
			       0x1003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(isid, 2);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result add_cached_null(void)
{
	struct pt_image_section_cache iscache;
	struct pt_image image;
	int status;

	status = pt_image_add_cached(NULL, &iscache, 0, NULL);
	ptu_int_eq(status, -pte_invalid);

	status = pt_image_add_cached(&image, NULL, 0, NULL);
	ptu_int_eq(status, -pte_invalid);

	return ptu_passed();
}

static struct ptunit_result add_cached(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid, risid;

	isid = ifix_cache_section(ifix, &ifix->section[0], 0x1000ull);
	ptu_int_gt(isid, 0);

	status = pt_image_add_cached(&ifix->image, &ifix->iscache, isid,
				      &ifix->asid[0]);
	ptu_int_eq(status, 0);

	risid = -1;
	status = pt_image_read(&ifix->image, &risid, buffer, 2, &ifix->asid[0],
			       0x1003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(risid, isid);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result add_cached_null_asid(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid, risid;

	isid = ifix_cache_section(ifix, &ifix->section[0], 0x1000ull);
	ptu_int_gt(isid, 0);

	status = pt_image_add_cached(&ifix->image, &ifix->iscache, isid, NULL);
	ptu_int_eq(status, 0);

	risid = -1;
	status = pt_image_read(&ifix->image, &risid, buffer, 2, &ifix->asid[0],
			       0x1003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(risid, isid);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result add_cached_twice(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid, risid;

	isid = ifix_cache_section(ifix, &ifix->section[0], 0x1000ull);
	ptu_int_gt(isid, 0);

	status = pt_image_add_cached(&ifix->image, &ifix->iscache, isid,
				      &ifix->asid[0]);
	ptu_int_eq(status, 0);

	status = pt_image_add_cached(&ifix->image, &ifix->iscache, isid,
				      &ifix->asid[0]);
	ptu_int_eq(status, 0);

	risid = -1;
	status = pt_image_read(&ifix->image, &risid, buffer, 2, &ifix->asid[0],
			       0x1003ull);
	ptu_int_eq(status, 2);
	ptu_int_eq(risid, isid);
	ptu_uint_eq(buffer[0], 0x03);
	ptu_uint_eq(buffer[1], 0x04);
	ptu_uint_eq(buffer[2], 0xcc);

	return ptu_passed();
}

static struct ptunit_result add_cached_bad_isid(struct image_fixture *ifix)
{
	uint8_t buffer[] = { 0xcc, 0xcc, 0xcc };
	int status, isid;

	status = pt_image_add_cached(&ifix->image, &ifix->iscache, 1,
				      &ifix->asid[0]);
	ptu_int_eq(status, -pte_bad_image);

	isid = -1;
	status = pt_image_read(&ifix->image, &isid, buffer, 2, &ifix->asid[0],
			       0x1003ull);
	ptu_int_eq(status, -pte_nomap);
	ptu_int_eq(isid, -1);

	return ptu_passed();
}

static struct ptunit_result find_null(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int status;

	status = pt_image_find(NULL, &msec, &ifix->asid[0],
			       0x1000ull);
	ptu_int_eq(status, -pte_internal);

	status = pt_image_find(&ifix->image, NULL, &ifix->asid[0],
			       0x1000ull);
	ptu_int_eq(status, -pte_internal);

	status = pt_image_find(&ifix->image, &msec, NULL, 0x1000ull);
	ptu_int_eq(status, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result find(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int status;

	status = pt_image_find(&ifix->image, &msec, &ifix->asid[1], 0x2003ull);
	ptu_int_eq(status, 11);
	ptu_ptr_eq(msec.section, &ifix->section[1]);
	ptu_uint_eq(msec.vaddr, 0x2000ull);

	status = pt_section_put(msec.section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result find_asid(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int status;

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 1);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[1],
			      0x1008ull, 2);
	ptu_int_eq(status, 0);

	status = pt_image_find(&ifix->image, &msec, &ifix->asid[0], 0x1009ull);
	ptu_int_eq(status, 1);
	ptu_ptr_eq(msec.section, &ifix->section[0]);
	ptu_uint_eq(msec.vaddr, 0x1000ull);

	status = pt_section_put(msec.section);
	ptu_int_eq(status, 0);

	status = pt_image_find(&ifix->image, &msec, &ifix->asid[1], 0x1009ull);
	ptu_int_eq(status, 2);
	ptu_ptr_eq(msec.section, &ifix->section[0]);
	ptu_uint_eq(msec.vaddr, 0x1008ull);

	status = pt_section_put(msec.section);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result find_bad_asid(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int status;

	status = pt_image_find(&ifix->image, &msec, &ifix->asid[0], 0x2003ull);
	ptu_int_eq(status, -pte_nomap);

	return ptu_passed();
}

static struct ptunit_result find_nomem(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int status;

	status = pt_image_find(&ifix->image, &msec, &ifix->asid[1], 0x1010ull);
	ptu_int_eq(status, -pte_nomap);

	return ptu_passed();
}

static struct ptunit_result validate_null(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int status;

	status = pt_image_validate(NULL, &msec, 0x1004ull, 10);
	ptu_int_eq(status, -pte_internal);

	status = pt_image_validate(&ifix->image, NULL, 0x1004ull, 10);
	ptu_int_eq(status, -pte_internal);

	return ptu_passed();
}

static struct ptunit_result validate(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int isid, status;

	isid = pt_image_find(&ifix->image, &msec, &ifix->asid[0], 0x1003ull);
	ptu_int_ge(isid, 0);

	status = pt_section_put(msec.section);
	ptu_int_eq(status, 0);

	status = pt_image_validate(&ifix->image, &msec, 0x1004ull, isid);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result validate_bad_asid(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int isid, status;

	isid = pt_image_find(&ifix->image, &msec, &ifix->asid[0], 0x1003ull);
	ptu_int_ge(isid, 0);

	status = pt_section_put(msec.section);
	ptu_int_eq(status, 0);

	msec.asid = ifix->asid[1];

	status = pt_image_validate(&ifix->image, &msec, 0x1004ull, isid);
	ptu_int_eq(status, -pte_nomap);

	return ptu_passed();
}

static struct ptunit_result validate_bad_vaddr(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int isid, status;

	isid = pt_image_find(&ifix->image, &msec, &ifix->asid[0], 0x1003ull);
	ptu_int_ge(isid, 0);

	status = pt_section_put(msec.section);
	ptu_int_eq(status, 0);

	msec.vaddr = 0x2000ull;

	status = pt_image_validate(&ifix->image, &msec, 0x1004ull, isid);
	ptu_int_eq(status, -pte_nomap);

	return ptu_passed();
}

static struct ptunit_result validate_bad_offset(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int isid, status;

	isid = pt_image_find(&ifix->image, &msec, &ifix->asid[0], 0x1003ull);
	ptu_int_ge(isid, 0);

	status = pt_section_put(msec.section);
	ptu_int_eq(status, 0);

	msec.offset = 0x8ull;

	status = pt_image_validate(&ifix->image, &msec, 0x1004ull, isid);
	ptu_int_eq(status, -pte_nomap);

	return ptu_passed();
}

static struct ptunit_result validate_bad_size(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int isid, status;

	isid = pt_image_find(&ifix->image, &msec, &ifix->asid[0], 0x1003ull);
	ptu_int_ge(isid, 0);

	status = pt_section_put(msec.section);
	ptu_int_eq(status, 0);

	msec.size = 0x8ull;

	status = pt_image_validate(&ifix->image, &msec, 0x1004ull, isid);
	ptu_int_eq(status, -pte_nomap);

	return ptu_passed();
}

static struct ptunit_result validate_bad_isid(struct image_fixture *ifix)
{
	struct pt_mapped_section msec;
	int isid, status;

	isid = pt_image_find(&ifix->image, &msec, &ifix->asid[0], 0x1003ull);
	ptu_int_ge(isid, 0);

	status = pt_section_put(msec.section);
	ptu_int_eq(status, 0);

	status = pt_image_validate(&ifix->image, &msec, 0x1004ull, isid + 1);
	ptu_int_eq(status, -pte_nomap);

	return ptu_passed();
}

static struct ptunit_result ifix_init(struct image_fixture *ifix)
{
	int index;

	pt_image_init(&ifix->image, NULL);
	pt_image_init(&ifix->copy, NULL);

	memset(ifix->status, 0, sizeof(ifix->status));
	memset(ifix->mapping, 0, sizeof(ifix->mapping));
	memset(ifix->section, 0, sizeof(ifix->section));
	memset(&ifix->iscache, 0, sizeof(ifix->iscache));

	ifix->nsecs = 0;

	index = ifix_add_section(ifix, "file-0");
	ptu_int_eq(index, 0);

	index = ifix_add_section(ifix, "file-1");
	ptu_int_eq(index, 1);

	index = ifix_add_section(ifix, "file-2");
	ptu_int_eq(index, 2);

	pt_asid_init(&ifix->asid[0]);
	ifix->asid[0].cr3 = 0xa000;

	pt_asid_init(&ifix->asid[1]);
	ifix->asid[1].cr3 = 0xb000;

	pt_asid_init(&ifix->asid[2]);
	ifix->asid[2].cr3 = 0xc000;

	return ptu_passed();
}

static struct ptunit_result rfix_init(struct image_fixture *ifix)
{
	int status;

	ptu_check(ifix_init, ifix);

	status = pt_image_add(&ifix->image, &ifix->section[0], &ifix->asid[0],
			      0x1000ull, 10);
	ptu_int_eq(status, 0);

	status = pt_image_add(&ifix->image, &ifix->section[1], &ifix->asid[1],
			      0x2000ull, 11);
	ptu_int_eq(status, 0);

	return ptu_passed();
}

static struct ptunit_result dfix_fini(struct image_fixture *ifix)
{
	pt_image_fini(&ifix->image);

	return ptu_passed();
}

static struct ptunit_result ifix_fini(struct image_fixture *ifix)
{
	int sec;

	ptu_check(dfix_fini, ifix);

	pt_image_fini(&ifix->copy);

	for (sec = 0; sec < ifix_nsecs; ++sec) {
		ptu_int_eq(ifix->section[sec].ucount, 0);
		ptu_int_eq(ifix->section[sec].mcount, 0);
		ptu_int_le(ifix->status[sec].deleted, 1);
		ptu_int_eq(ifix->status[sec].bad_put, 0);
	}

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct image_fixture dfix, ifix, rfix;
	struct ptunit_suite suite;

	/* Dfix provides image destruction. */
	dfix.init = NULL;
	dfix.fini = dfix_fini;

	/* Ifix provides an empty image. */
	ifix.init = ifix_init;
	ifix.fini = ifix_fini;

	/* Rfix provides an image with two sections added. */
	rfix.init = rfix_init;
	rfix.fini = ifix_fini;

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, init);
	ptu_run_f(suite, init_name, dfix);
	ptu_run(suite, init_null);

	ptu_run(suite, fini);
	ptu_run(suite, fini_empty);
	ptu_run(suite, fini_null);

	ptu_run_f(suite, name, dfix);
	ptu_run(suite, name_none);
	ptu_run(suite, name_null);

	ptu_run_f(suite, read_empty, ifix);
	ptu_run_f(suite, overlap_front, ifix);
	ptu_run_f(suite, overlap_back, ifix);
	ptu_run_f(suite, overlap_multiple, ifix);
	ptu_run_f(suite, overlap_mid, ifix);
	ptu_run_f(suite, contained, ifix);
	ptu_run_f(suite, contained_multiple, ifix);
	ptu_run_f(suite, contained_back, ifix);
	ptu_run_f(suite, same, ifix);
	ptu_run_f(suite, same_different_isid, ifix);
	ptu_run_f(suite, same_different_offset, ifix);
	ptu_run_f(suite, adjacent, ifix);

	ptu_run_f(suite, read_null, rfix);
	ptu_run_f(suite, read, rfix);
	ptu_run_f(suite, read_null, rfix);
	ptu_run_f(suite, read_asid, ifix);
	ptu_run_f(suite, read_bad_asid, rfix);
	ptu_run_f(suite, read_null_asid, rfix);
	ptu_run_f(suite, read_callback, rfix);
	ptu_run_f(suite, read_nomem, rfix);
	ptu_run_f(suite, read_truncated, rfix);
	ptu_run_f(suite, read_error, rfix);
	ptu_run_f(suite, read_spurious_error, rfix);

	ptu_run_f(suite, remove_section, rfix);
	ptu_run_f(suite, remove_bad_vaddr, rfix);
	ptu_run_f(suite, remove_bad_asid, rfix);
	ptu_run_f(suite, remove_by_filename, rfix);
	ptu_run_f(suite, remove_by_filename_bad_asid, rfix);
	ptu_run_f(suite, remove_none_by_filename, rfix);
	ptu_run_f(suite, remove_all_by_filename, ifix);
	ptu_run_f(suite, remove_by_asid, rfix);

	ptu_run_f(suite, copy_empty, ifix);
	ptu_run_f(suite, copy, rfix);
	ptu_run_f(suite, copy_self, rfix);
	ptu_run_f(suite, copy_shrink, rfix);
	ptu_run_f(suite, copy_split, ifix);
	ptu_run_f(suite, copy_merge, ifix);
	ptu_run_f(suite, copy_overlap, ifix);
	ptu_run_f(suite, copy_replace, ifix);

	ptu_run(suite, add_cached_null);
	ptu_run_f(suite, add_cached, ifix);
	ptu_run_f(suite, add_cached_null_asid, ifix);
	ptu_run_f(suite, add_cached_twice, ifix);
	ptu_run_f(suite, add_cached_bad_isid, ifix);

	ptu_run_f(suite, find_null, rfix);
	ptu_run_f(suite, find, rfix);
	ptu_run_f(suite, find_asid, ifix);
	ptu_run_f(suite, find_bad_asid, rfix);
	ptu_run_f(suite, find_nomem, rfix);

	ptu_run_f(suite, validate_null, rfix);
	ptu_run_f(suite, validate, rfix);
	ptu_run_f(suite, validate_bad_asid, rfix);
	ptu_run_f(suite, validate_bad_vaddr, rfix);
	ptu_run_f(suite, validate_bad_offset, rfix);
	ptu_run_f(suite, validate_bad_size, rfix);
	ptu_run_f(suite, validate_bad_isid, rfix);

	return ptunit_report(&suite);
}
