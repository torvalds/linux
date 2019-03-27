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

#include "pt_msec_cache.h"
#include "pt_section.h"
#include "pt_image.h"

#include <string.h>


int pt_msec_cache_init(struct pt_msec_cache *cache)
{
	if (!cache)
		return -pte_internal;

	memset(cache, 0, sizeof(*cache));

	return 0;
}

void pt_msec_cache_fini(struct pt_msec_cache *cache)
{
	if (!cache)
		return;

	(void) pt_msec_cache_invalidate(cache);
	pt_msec_fini(&cache->msec);
}

int pt_msec_cache_invalidate(struct pt_msec_cache *cache)
{
	struct pt_section *section;
	int errcode;

	if (!cache)
		return -pte_internal;

	section = pt_msec_section(&cache->msec);
	if (!section)
		return 0;

	errcode = pt_section_unmap(section);
	if (errcode < 0)
		return errcode;

	cache->msec.section = NULL;

	return pt_section_put(section);
}

int pt_msec_cache_read(struct pt_msec_cache *cache,
		       const struct pt_mapped_section **pmsec,
		       struct pt_image *image, uint64_t vaddr)
{
	struct pt_mapped_section *msec;
	int isid, errcode;

	if (!cache || !pmsec)
		return -pte_internal;

	msec = &cache->msec;
	isid = cache->isid;

	errcode = pt_image_validate(image, msec, vaddr, isid);
	if (errcode < 0)
		return errcode;

	*pmsec = msec;

	return isid;

}

int pt_msec_cache_fill(struct pt_msec_cache *cache,
		       const struct pt_mapped_section **pmsec,
		       struct pt_image *image, const struct pt_asid *asid,
		       uint64_t vaddr)
{
	struct pt_mapped_section *msec;
	struct pt_section *section;
	int errcode, isid;

	if (!cache || !pmsec)
		return -pte_internal;

	errcode = pt_msec_cache_invalidate(cache);
	if (errcode < 0)
		return errcode;

	msec = &cache->msec;

	isid = pt_image_find(image, msec, asid, vaddr);
	if (isid < 0)
		return isid;

	section = pt_msec_section(msec);

	errcode = pt_section_map(section);
	if (errcode < 0) {
		(void) pt_section_put(section);
		msec->section = NULL;

		return errcode;
	}

	*pmsec = msec;

	cache->isid = isid;

	return isid;
}
