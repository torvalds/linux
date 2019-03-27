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
#include "pt_block_cache.h"
#include "pt_image_section_cache.h"

#include "intel-pt.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


static char *dupstr(const char *str)
{
	char *dup;
	size_t len;

	if (!str)
		return NULL;

	len = strlen(str);
	dup = malloc(len + 1);
	if (!dup)
		return NULL;

	return strcpy(dup, str);
}

struct pt_section *pt_mk_section(const char *filename, uint64_t offset,
				 uint64_t size)
{
	struct pt_section *section;
	uint64_t fsize;
	void *status;
	int errcode;

	errcode = pt_section_mk_status(&status, &fsize, filename);
	if (errcode < 0)
		return NULL;

	/* Fail if the requested @offset lies beyond the end of @file. */
	if (fsize <= offset)
		goto out_status;

	/* Truncate @size so the entire range lies within @file. */
	fsize -= offset;
	if (fsize < size)
		size = fsize;

	section = malloc(sizeof(*section));
	if (!section)
		goto out_status;

	memset(section, 0, sizeof(*section));

	section->filename = dupstr(filename);
	section->status = status;
	section->offset = offset;
	section->size = size;
	section->ucount = 1;

#if defined(FEATURE_THREADS)

	errcode = mtx_init(&section->lock, mtx_plain);
	if (errcode != thrd_success) {
		free(section->filename);
		free(section);
		goto out_status;
	}

	errcode = mtx_init(&section->alock, mtx_plain);
	if (errcode != thrd_success) {
		mtx_destroy(&section->lock);
		free(section->filename);
		free(section);
		goto out_status;
	}

#endif /* defined(FEATURE_THREADS) */

	return section;

out_status:
	free(status);
	return NULL;
}

int pt_section_lock(struct pt_section *section)
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

int pt_section_unlock(struct pt_section *section)
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

static void pt_section_free(struct pt_section *section)
{
	if (!section)
		return;

#if defined(FEATURE_THREADS)

	mtx_destroy(&section->alock);
	mtx_destroy(&section->lock);

#endif /* defined(FEATURE_THREADS) */

	free(section->filename);
	free(section->status);
	free(section);
}

int pt_section_get(struct pt_section *section)
{
	uint16_t ucount;
	int errcode;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	ucount = section->ucount + 1;
	if (!ucount) {
		(void) pt_section_unlock(section);
		return -pte_overflow;
	}

	section->ucount = ucount;

	return pt_section_unlock(section);
}

int pt_section_put(struct pt_section *section)
{
	uint16_t ucount, mcount;
	int errcode;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	mcount = section->mcount;
	ucount = section->ucount;
	if (ucount > 1) {
		section->ucount = ucount - 1;
		return pt_section_unlock(section);
	}

	errcode = pt_section_unlock(section);
	if (errcode < 0)
		return errcode;

	if (!ucount || mcount)
		return -pte_internal;

	pt_section_free(section);
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

int pt_section_attach(struct pt_section *section,
		      struct pt_image_section_cache *iscache)
{
	uint16_t acount, ucount;
	int errcode;

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
	uint16_t acount, ucount;
	int errcode;

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

const char *pt_section_filename(const struct pt_section *section)
{
	if (!section)
		return NULL;

	return section->filename;
}

uint64_t pt_section_size(const struct pt_section *section)
{
	if (!section)
		return 0ull;

	return section->size;
}

static int pt_section_bcache_memsize(const struct pt_section *section,
				     uint64_t *psize)
{
	struct pt_block_cache *bcache;

	if (!section || !psize)
		return -pte_internal;

	bcache = section->bcache;
	if (!bcache) {
		*psize = 0ull;
		return 0;
	}

	*psize = sizeof(*bcache) +
		(bcache->nentries * sizeof(struct pt_bcache_entry));

	return 0;
}

static int pt_section_memsize_locked(const struct pt_section *section,
				     uint64_t *psize)
{
	uint64_t msize, bcsize;
	int (*memsize)(const struct pt_section *section, uint64_t *size);
	int errcode;

	if (!section || !psize)
		return -pte_internal;

	memsize = section->memsize;
	if (!memsize) {
		if (section->mcount)
			return -pte_internal;

		*psize = 0ull;
		return 0;
	}

	errcode = memsize(section, &msize);
	if (errcode < 0)
		return errcode;

	errcode = pt_section_bcache_memsize(section, &bcsize);
	if (errcode < 0)
		return errcode;

	*psize = msize + bcsize;

	return 0;
}

int pt_section_memsize(struct pt_section *section, uint64_t *size)
{
	int errcode, status;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	status = pt_section_memsize_locked(section, size);

	errcode = pt_section_unlock(section);
	if (errcode < 0)
		return errcode;

	return status;
}

uint64_t pt_section_offset(const struct pt_section *section)
{
	if (!section)
		return 0ull;

	return section->offset;
}

int pt_section_alloc_bcache(struct pt_section *section)
{
	struct pt_image_section_cache *iscache;
	struct pt_block_cache *bcache;
	uint64_t ssize, memsize;
	uint32_t csize;
	int errcode;

	if (!section)
		return -pte_internal;

	if (!section->mcount)
		return -pte_internal;

	ssize = pt_section_size(section);
	csize = (uint32_t) ssize;

	if (csize != ssize)
		return -pte_not_supported;

	memsize = 0ull;

	/* We need to take both the attach and the section lock in order to pair
	 * the block cache allocation and the resize notification.
	 *
	 * This allows map notifications in between but they only change the
	 * order of sections in the cache.
	 *
	 * The attach lock needs to be taken first.
	 */
	errcode = pt_section_lock_attach(section);
	if (errcode < 0)
		return errcode;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		goto out_alock;

	bcache = pt_section_bcache(section);
	if (bcache) {
		errcode = 0;
		goto out_lock;
	}

	bcache = pt_bcache_alloc(csize);
	if (!bcache) {
		errcode = -pte_nomem;
		goto out_lock;
	}

	/* Install the block cache.  It will become visible and may be used
	 * immediately.
	 *
	 * If we fail later on, we leave the block cache and report the error to
	 * the allocating decoder thread.
	 */
	section->bcache = bcache;

	errcode = pt_section_memsize_locked(section, &memsize);
	if (errcode < 0)
		goto out_lock;

	errcode = pt_section_unlock(section);
	if (errcode < 0)
		goto out_alock;

	if (memsize) {
		iscache = section->iscache;
		if (iscache) {
			errcode = pt_iscache_notify_resize(iscache, section,
							  memsize);
			if (errcode < 0)
				goto out_alock;
		}
	}

	return pt_section_unlock_attach(section);


out_lock:
	(void) pt_section_unlock(section);

out_alock:
	(void) pt_section_unlock_attach(section);
	return errcode;
}

int pt_section_on_map_lock(struct pt_section *section)
{
	struct pt_image_section_cache *iscache;
	int errcode, status;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock_attach(section);
	if (errcode < 0)
		return errcode;

	iscache = section->iscache;
	if (!iscache)
		return pt_section_unlock_attach(section);

	/* There is a potential deadlock when @section was unmapped again and
	 * @iscache tries to map it.  This would cause this function to be
	 * re-entered while we're still holding the attach lock.
	 *
	 * This scenario is very unlikely, though, since our caller does not yet
	 * know whether pt_section_map() succeeded.
	 */
	status = pt_iscache_notify_map(iscache, section);

	errcode = pt_section_unlock_attach(section);
	if (errcode < 0)
		return errcode;

	return status;
}

int pt_section_map_share(struct pt_section *section)
{
	uint16_t mcount;
	int errcode;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	mcount = section->mcount;
	if (!mcount) {
		(void) pt_section_unlock(section);
		return -pte_internal;
	}

	mcount += 1;
	if (!mcount) {
		(void) pt_section_unlock(section);
		return -pte_overflow;
	}

	section->mcount = mcount;

	return pt_section_unlock(section);
}

int pt_section_unmap(struct pt_section *section)
{
	uint16_t mcount;
	int errcode, status;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	mcount = section->mcount;

	errcode = -pte_nomap;
	if (!mcount)
		goto out_unlock;

	section->mcount = mcount -= 1;
	if (mcount)
		return pt_section_unlock(section);

	errcode = -pte_internal;
	if (!section->unmap)
		goto out_unlock;

	status = section->unmap(section);

	pt_bcache_free(section->bcache);
	section->bcache = NULL;

	errcode = pt_section_unlock(section);
	if (errcode < 0)
		return errcode;

	return status;

out_unlock:
	(void) pt_section_unlock(section);
	return errcode;
}

int pt_section_read(const struct pt_section *section, uint8_t *buffer,
		    uint16_t size, uint64_t offset)
{
	uint64_t limit, space;

	if (!section)
		return -pte_internal;

	if (!section->read)
		return -pte_nomap;

	limit = section->size;
	if (limit <= offset)
		return -pte_nomap;

	/* Truncate if we try to read past the end of the section. */
	space = limit - offset;
	if (space < size)
		size = (uint16_t) space;

	return section->read(section, buffer, size, offset);
}
