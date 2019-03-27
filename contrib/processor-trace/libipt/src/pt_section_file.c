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

#include "pt_section.h"
#include "pt_section_file.h"

#include "intel-pt.h"

#include <stdlib.h>
#include <string.h>


static int fmap_init(struct pt_sec_file_mapping *mapping)
{
	if (!mapping)
		return -pte_internal;

	memset(mapping, 0, sizeof(*mapping));

#if defined(FEATURE_THREADS)
	{
		int errcode;

		errcode = mtx_init(&mapping->lock, mtx_plain);
		if (errcode != thrd_success)
			return -pte_bad_lock;
	}
#endif /* defined(FEATURE_THREADS) */

	return 0;
}

static void fmap_fini(struct pt_sec_file_mapping *mapping)
{
	if (!mapping)
		return;

	fclose(mapping->file);

#if defined(FEATURE_THREADS)

	mtx_destroy(&mapping->lock);

#endif /* defined(FEATURE_THREADS) */
}

static int fmap_lock(struct pt_sec_file_mapping *mapping)
{
	if (!mapping)
		return -pte_internal;

#if defined(FEATURE_THREADS)
	{
		int errcode;

		errcode = mtx_lock(&mapping->lock);
		if (errcode != thrd_success)
			return -pte_bad_lock;
	}
#endif /* defined(FEATURE_THREADS) */

	return 0;
}

static int fmap_unlock(struct pt_sec_file_mapping *mapping)
{
	if (!mapping)
		return -pte_internal;

#if defined(FEATURE_THREADS)
	{
		int errcode;

		errcode = mtx_unlock(&mapping->lock);
		if (errcode != thrd_success)
			return -pte_bad_lock;
	}
#endif /* defined(FEATURE_THREADS) */

	return 0;
}

int pt_sec_file_map(struct pt_section *section, FILE *file)
{
	struct pt_sec_file_mapping *mapping;
	uint64_t offset, size;
	long begin, end, fsize;
	int errcode;

	if (!section)
		return -pte_internal;

	mapping = section->mapping;
	if (mapping)
		return -pte_internal;

	offset = section->offset;
	size = section->size;

	begin = (long) offset;
	end = begin + (long) size;

	/* Check for overflows. */
	if ((uint64_t) begin != offset)
		return -pte_bad_image;

	if ((uint64_t) end != (offset + size))
		return -pte_bad_image;

	if (end < begin)
		return -pte_bad_image;

	/* Validate that the section lies within the file. */
	errcode = fseek(file, 0, SEEK_END);
	if (errcode)
		return -pte_bad_image;

	fsize = ftell(file);
	if (fsize < 0)
		return -pte_bad_image;

	if (fsize < end)
		return -pte_bad_image;

	mapping = malloc(sizeof(*mapping));
	if (!mapping)
		return -pte_nomem;

	errcode = fmap_init(mapping);
	if (errcode < 0)
		goto out_mem;

	mapping->file = file;
	mapping->begin = begin;
	mapping->end = end;

	section->mapping = mapping;
	section->unmap = pt_sec_file_unmap;
	section->read = pt_sec_file_read;
	section->memsize = pt_sec_file_memsize;

	return 0;

out_mem:
	free(mapping);
	return errcode;
}

int pt_sec_file_unmap(struct pt_section *section)
{
	struct pt_sec_file_mapping *mapping;

	if (!section)
		return -pte_internal;

	mapping = section->mapping;

	if (!mapping || !section->unmap || !section->read || !section->memsize)
		return -pte_internal;

	section->mapping = NULL;
	section->unmap = NULL;
	section->read = NULL;
	section->memsize = NULL;

	fmap_fini(mapping);
	free(mapping);

	return 0;
}

int pt_sec_file_read(const struct pt_section *section, uint8_t *buffer,
		     uint16_t size, uint64_t offset)
{
	struct pt_sec_file_mapping *mapping;
	FILE *file;
	long begin;
	size_t read;
	int errcode;

	if (!buffer || !section)
		return -pte_internal;

	mapping = section->mapping;
	if (!mapping)
		return -pte_internal;

	file = mapping->file;

	/* We already checked in pt_section_read() that the requested memory
	 * lies within the section's boundaries.
	 *
	 * And we checked that the file covers the entire section in
	 * pt_sec_file_map().  There's no need to check for overflows, again.
	 */
	begin = mapping->begin + (long) offset;

	errcode = fmap_lock(mapping);
	if (errcode < 0)
		return errcode;

	errcode = fseek(file, begin, SEEK_SET);
	if (errcode)
		goto out_unlock;

	read = fread(buffer, 1, size, file);

	errcode = fmap_unlock(mapping);
	if (errcode < 0)
		return errcode;

	return (int) read;

out_unlock:
	(void) fmap_unlock(mapping);
	return -pte_nomap;
}

int pt_sec_file_memsize(const struct pt_section *section, uint64_t *size)
{
	if (!section || !size)
		return -pte_internal;

	if (!section->mapping)
		return -pte_internal;

	*size = 0ull;

	return 0;
}
