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
#include "pt_section_posix.h"
#include "pt_section_file.h"

#include "intel-pt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>


int pt_section_mk_status(void **pstatus, uint64_t *psize, const char *filename)
{
	struct pt_sec_posix_status *status;
	struct stat buffer;
	int errcode;

	if (!pstatus || !psize)
		return -pte_internal;

	errcode = stat(filename, &buffer);
	if (errcode < 0)
		return errcode;

	if (buffer.st_size < 0)
		return -pte_bad_image;

	status = malloc(sizeof(*status));
	if (!status)
		return -pte_nomem;

	status->stat = buffer;

	*pstatus = status;
	*psize = buffer.st_size;

	return 0;
}

static int check_file_status(struct pt_section *section, int fd)
{
	struct pt_sec_posix_status *status;
	struct stat stat;
	int errcode;

	if (!section)
		return -pte_internal;

	errcode = fstat(fd, &stat);
	if (errcode)
		return -pte_bad_image;

	status = section->status;
	if (!status)
		return -pte_internal;

	if (stat.st_size != status->stat.st_size)
		return -pte_bad_image;

	if (stat.st_mtime != status->stat.st_mtime)
		return -pte_bad_image;

	return 0;
}

int pt_sec_posix_map(struct pt_section *section, int fd)
{
	struct pt_sec_posix_mapping *mapping;
	uint64_t offset, size, adjustment;
	uint8_t *base;
	int errcode;

	if (!section)
		return -pte_internal;

	offset = section->offset;
	size = section->size;

	adjustment = offset % sysconf(_SC_PAGESIZE);

	offset -= adjustment;
	size += adjustment;

	/* The section is supposed to fit into the file so we shouldn't
	 * see any overflows, here.
	 */
	if (size < section->size)
		return -pte_internal;

	if (SIZE_MAX < size)
		return -pte_nomem;

	if (INT_MAX < offset)
		return -pte_nomem;

	base = mmap(NULL, (size_t) size, PROT_READ, MAP_SHARED, fd,
		    (off_t) offset);
	if (base == MAP_FAILED)
		return -pte_nomem;

	mapping = malloc(sizeof(*mapping));
	if (!mapping) {
		errcode = -pte_nomem;
		goto out_map;
	}

	mapping->base = base;
	mapping->size = size;
	mapping->begin = base + adjustment;
	mapping->end = base + size;

	section->mapping = mapping;
	section->unmap = pt_sec_posix_unmap;
	section->read = pt_sec_posix_read;
	section->memsize = pt_sec_posix_memsize;

	return 0;

out_map:
	munmap(base, (size_t) size);
	return errcode;
}

static int pt_sec_posix_map_success(struct pt_section *section)
{
	uint16_t mcount;
	int errcode, status;

	if (!section)
		return -pte_internal;

	mcount = section->mcount + 1;
	if (!mcount) {
		(void) pt_section_unlock(section);
		return -pte_overflow;
	}

	section->mcount = mcount;

	errcode = pt_section_unlock(section);
	if (errcode < 0)
		return errcode;

	status = pt_section_on_map(section);
	if (status < 0) {
		/* We had to release the section lock for pt_section_on_map() so
		 * @section may have meanwhile been mapped by other threads.
		 *
		 * We still want to return the error so we release our mapping.
		 * Our caller does not yet know whether pt_section_map()
		 * succeeded.
		 */
		(void) pt_section_unmap(section);
		return status;
	}

	return 0;
}

int pt_section_map(struct pt_section *section)
{
	const char *filename;
	FILE *file;
	int fd, errcode;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	if (section->mcount)
		return pt_sec_posix_map_success(section);

	if (section->mapping)
		goto out_unlock;

	filename = section->filename;
	if (!filename)
		goto out_unlock;

	errcode = -pte_bad_image;
	fd = open(filename, O_RDONLY);
	if (fd == -1)
		goto out_unlock;

	errcode = check_file_status(section, fd);
	if (errcode < 0)
		goto out_fd;

	/* We close the file on success.  This does not unmap the section. */
	errcode = pt_sec_posix_map(section, fd);
	if (!errcode) {
		close(fd);

		return pt_sec_posix_map_success(section);
	}

	/* Fall back to file based sections - report the original error
	 * if we fail to convert the file descriptor.
	 */
	file = fdopen(fd, "rb");
	if (!file)
		goto out_fd;

	/* We need to keep the file open on success.  It will be closed when
	 * the section is unmapped.
	 */
	errcode = pt_sec_file_map(section, file);
	if (!errcode)
		return pt_sec_posix_map_success(section);

	fclose(file);
	goto out_unlock;

out_fd:
	close(fd);

out_unlock:
	(void) pt_section_unlock(section);
	return errcode;
}

int pt_sec_posix_unmap(struct pt_section *section)
{
	struct pt_sec_posix_mapping *mapping;

	if (!section)
		return -pte_internal;

	mapping = section->mapping;
	if (!mapping || !section->unmap || !section->read || !section->memsize)
		return -pte_internal;

	section->mapping = NULL;
	section->unmap = NULL;
	section->read = NULL;
	section->memsize = NULL;

	munmap(mapping->base, (size_t) mapping->size);
	free(mapping);

	return 0;
}

int pt_sec_posix_read(const struct pt_section *section, uint8_t *buffer,
		      uint16_t size, uint64_t offset)
{
	struct pt_sec_posix_mapping *mapping;
	const uint8_t *begin;

	if (!buffer || !section)
		return -pte_internal;

	mapping = section->mapping;
	if (!mapping)
		return -pte_internal;

	/* We already checked in pt_section_read() that the requested memory
	 * lies within the section's boundaries.
	 *
	 * And we checked that the entire section was mapped.  There's no need
	 * to check for overflows, again.
	 */
	begin = mapping->begin + offset;

	memcpy(buffer, begin, size);
	return (int) size;
}

int pt_sec_posix_memsize(const struct pt_section *section, uint64_t *size)
{
	struct pt_sec_posix_mapping *mapping;
	const uint8_t *begin, *end;

	if (!section || !size)
		return -pte_internal;

	mapping = section->mapping;
	if (!mapping)
		return -pte_internal;

	begin = mapping->base;
	end = mapping->end;

	if (!begin || !end || end < begin)
		return -pte_internal;

	*size = (uint64_t) (end - begin);

	return 0;
}
