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

#ifndef PT_SECTION_FILE_H
#define PT_SECTION_FILE_H

#include <stdio.h>
#include <stdint.h>

#if defined(FEATURE_THREADS)
#  include <threads.h>
#endif /* defined(FEATURE_THREADS) */

struct pt_section;


/* File-based section mapping information. */
struct pt_sec_file_mapping {
	/* The FILE pointer. */
	FILE *file;

	/* The begin and end of the section as offset into @file. */
	long begin, end;

#if defined(FEATURE_THREADS)
	/* A lock protecting read access to this file.
	 *
	 * Since we need to first set the file position indication before
	 * we can read, there's a race on the file position.
	 */
	mtx_t lock;
#endif /* defined(FEATURE_THREADS) */
};


/* Map a section based on file operations.
 *
 * The caller has already opened the file for reading.
 *
 * On success, sets @section's mapping, unmap, and read pointers.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @section or @file are NULL.
 * Returns -pte_invalid if @section can't be mapped.
 */
extern int pt_sec_file_map(struct pt_section *section, FILE *file);

/* Unmap a section based on file operations.
 *
 * On success, clears @section's mapping, unmap, and read pointers.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @section is NULL.
 * Returns -pte_internal if @section has not been mapped.
 */
extern int pt_sec_file_unmap(struct pt_section *section);

/* Read memory from a file based section.
 *
 * Reads at most @size bytes from @section at @offset into @buffer.
 *
 * Returns the number of bytes read on success, a negative error code otherwise.
 * Returns -pte_invalid if @section or @buffer are NULL.
 * Returns -pte_nomap if @offset is beyond the end of the section.
 */
extern int pt_sec_file_read(const struct pt_section *section, uint8_t *buffer,
			    uint16_t size, uint64_t offset);

/* Compute the memory size of a section based on file operations.
 *
 * On success, provides the amount of memory used for mapping @section in bytes
 * in @size.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @section or @size is NULL.
 * Returns -pte_internal if @section has not been mapped.
 */
extern int pt_sec_file_memsize(const struct pt_section *section,
			       uint64_t *size);

#endif /* PT_SECTION_FILE_H */
