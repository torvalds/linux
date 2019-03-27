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

#include "pt_block_cache.h"

#include <stdlib.h>
#include <string.h>


struct pt_block_cache *pt_bcache_alloc(uint64_t nentries)
{
	struct pt_block_cache *bcache;
	uint64_t size;

	if (!nentries || (UINT32_MAX < nentries))
		return NULL;

	size = sizeof(*bcache) + (nentries * sizeof(struct pt_bcache_entry));
	if (SIZE_MAX < size)
		return NULL;

	bcache = malloc((size_t) size);
	if (!bcache)
		return NULL;

	memset(bcache, 0, (size_t) size);
	bcache->nentries = (uint32_t) nentries;

	return bcache;
}

void pt_bcache_free(struct pt_block_cache *bcache)
{
	free(bcache);
}

int pt_bcache_add(struct pt_block_cache *bcache, uint64_t index,
		  struct pt_bcache_entry bce)
{
	if (!bcache)
		return -pte_internal;

	if (bcache->nentries <= index)
		return -pte_internal;

	/* We rely on guaranteed atomic operations as specified in section 8.1.1
	 * in Volume 3A of the Intel(R) Software Developer's Manual at
	 * http://www.intel.com/sdm.
	 */
	bcache->entry[(uint32_t) index] = bce;

	return 0;
}

int pt_bcache_lookup(struct pt_bcache_entry *bce,
		     const struct pt_block_cache *bcache, uint64_t index)
{
	if (!bce || !bcache)
		return -pte_internal;

	if (bcache->nentries <= index)
		return -pte_internal;

	/* We rely on guaranteed atomic operations as specified in section 8.1.1
	 * in Volume 3A of the Intel(R) Software Developer's Manual at
	 * http://www.intel.com/sdm.
	 */
	*bce = bcache->entry[(uint32_t) index];

	return 0;
}
