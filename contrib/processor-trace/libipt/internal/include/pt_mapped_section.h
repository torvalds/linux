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

#ifndef PT_MAPPED_SECTION_H
#define PT_MAPPED_SECTION_H

#include "intel-pt.h"
#include "pt_section.h"

#include <stdint.h>


/* A section mapped into memory. */
struct pt_mapped_section {
	/* The section that is mapped. */
	struct pt_section *section;

	/* The address space into which the section is mapped. */
	struct pt_asid asid;

	/* The virtual address at which the section is mapped. */
	uint64_t vaddr;

	/* The offset into the section.
	 *
	 * This is normally zero but when @section is split, @offset is added to
	 * the section/file offset when accessing @section.
	 */
	uint64_t offset;

	/* The size of the section.
	 *
	 * This is normally @section->size but when @section is split, this is
	 * used to determine the size of the sub-section.
	 */
	uint64_t size;
};


static inline void pt_msec_init(struct pt_mapped_section *msec,
				struct pt_section *section,
				const struct pt_asid *asid,
				uint64_t vaddr, uint64_t offset, uint64_t size)
{
	if (!msec)
		return;

	msec->section = section;
	msec->vaddr = vaddr;
	msec->offset = offset;
	msec->size = size;

	if (asid)
		msec->asid = *asid;
	else
		pt_asid_init(&msec->asid);
}

/* Destroy a mapped section - does not free @msec->section. */
static inline void pt_msec_fini(struct pt_mapped_section *msec)
{
	(void) msec;

	/* Nothing to do. */
}

/* Return the virtual address of the beginning of the memory region. */
static inline uint64_t pt_msec_begin(const struct pt_mapped_section *msec)
{
	if (!msec)
		return 0ull;

	return msec->vaddr;
}

/* Return the virtual address one byte past the end of the memory region. */
static inline uint64_t pt_msec_end(const struct pt_mapped_section *msec)
{
	if (!msec)
		return 0ull;

	return msec->vaddr + msec->size;
}

/* Return the section/file offset. */
static inline uint64_t pt_msec_offset(const struct pt_mapped_section *msec)
{
	if (!msec)
		return 0ull;

	return msec->offset;
}

/* Return the section size. */
static inline uint64_t pt_msec_size(const struct pt_mapped_section *msec)
{
	if (!msec)
		return 0ull;

	return msec->size;
}

/* Return the underlying section. */
static inline struct pt_section *
pt_msec_section(const struct pt_mapped_section *msec)
{
	return msec->section;
}

/* Return an identifier for the address-space the section is mapped into. */
static inline const struct pt_asid *
pt_msec_asid(const struct pt_mapped_section *msec)
{
	if (!msec)
		return NULL;

	return &msec->asid;
}

/* Translate a section/file offset into a virtual address. */
static inline uint64_t pt_msec_map(const struct pt_mapped_section *msec,
				   uint64_t offset)
{
	return (offset - msec->offset) + msec->vaddr;
}

/* Translate a virtual address into a section/file offset. */
static inline uint64_t pt_msec_unmap(const struct pt_mapped_section *msec,
				     uint64_t vaddr)
{
	return (vaddr - msec->vaddr) + msec->offset;
}

/* Read memory from a mapped section.
 *
 * The caller must check @msec->asid.
 * The caller must ensure that @msec->section is mapped.
 *
 * Returns the number of bytes read on success.
 * Returns a negative error code otherwise.
 */
static inline int pt_msec_read(const struct pt_mapped_section *msec,
			       uint8_t *buffer, uint16_t size,
			       uint64_t vaddr)
{
	struct pt_section *section;
	uint64_t begin, end, mbegin, mend, offset;

	if (!msec)
		return -pte_internal;

	begin = vaddr;
	end = begin + size;
	if (end < begin)
		end = UINT64_MAX;

	mbegin = pt_msec_begin(msec);
	mend = pt_msec_end(msec);

	if (begin < mbegin || mend <= begin)
		return -pte_nomap;

	if (mend < end)
		end = mend;

	size = (uint16_t) (end - begin);

	section = pt_msec_section(msec);
	offset = pt_msec_unmap(msec, begin);

	return pt_section_read(section, buffer, size, offset);
}

#endif /* PT_MAPPED_SECTION_H */
