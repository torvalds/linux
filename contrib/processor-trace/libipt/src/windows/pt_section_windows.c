/*
 * Copyright (c) 2015-2018, Intel Corporation
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
#include "pt_section_windows.h"
#include "pt_section_file.h"

#include "intel-pt.h"

#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <io.h>


static int pt_sec_windows_fstat(const char *filename, struct _stat *stat)
{
	int fd, errcode;

	if (!filename || !stat)
		return -pte_internal;

	fd = _open(filename, _O_RDONLY);
	if (fd == -1)
		return -pte_bad_image;

	errcode = _fstat(fd, stat);

	_close(fd);

	if (errcode)
		return -pte_bad_image;

	return 0;
}

int pt_section_mk_status(void **pstatus, uint64_t *psize, const char *filename)
{
	struct pt_sec_windows_status *status;
	struct _stat stat;
	int errcode;

	if (!pstatus || !psize)
		return -pte_internal;

	errcode = pt_sec_windows_fstat(filename, &stat);
	if (errcode < 0)
		return errcode;

	if (stat.st_size < 0)
		return -pte_bad_image;

	status = malloc(sizeof(*status));
	if (!status)
		return -pte_nomem;

	status->stat = stat;

	*pstatus = status;
	*psize = stat.st_size;

	return 0;
}

static int check_file_status(struct pt_section *section, int fd)
{
	struct pt_sec_windows_status *status;
	struct _stat stat;
	int errcode;

	if (!section)
		return -pte_internal;

	errcode = _fstat(fd, &stat);
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

static DWORD granularity(void)
{
	struct _SYSTEM_INFO sysinfo;

	GetSystemInfo(&sysinfo);

	return sysinfo.dwAllocationGranularity;
}

int pt_sec_windows_map(struct pt_section *section, int fd)
{
	struct pt_sec_windows_mapping *mapping;
	uint64_t offset, size, adjustment;
	HANDLE fh, mh;
	DWORD dsize;
	uint8_t *base;
	int errcode;

	if (!section)
		return -pte_internal;

	offset = section->offset;
	size = section->size;

	adjustment = offset % granularity();

	offset -= adjustment;
	size += adjustment;

	/* The section is supposed to fit into the file so we shouldn't
	 * see any overflows, here.
	 */
	if (size < section->size)
		return -pte_internal;

	dsize = (DWORD) size;
	if ((uint64_t) dsize != size)
		return -pte_internal;

	fh = (HANDLE) _get_osfhandle(fd);

	mh = CreateFileMapping(fh, NULL, PAGE_READONLY, 0, 0, NULL);
	if (!mh)
		return -pte_bad_image;

	base = MapViewOfFile(mh, FILE_MAP_READ, (DWORD) (offset >> 32),
			     (DWORD) (uint32_t) offset, dsize);
	if (!base) {
		errcode = -pte_bad_image;
		goto out_mh;
	}

	mapping = malloc(sizeof(*mapping));
	if (!mapping) {
		errcode = -pte_nomem;
		goto out_map;
	}

	mapping->fd = fd;
	mapping->mh = mh;
	mapping->base = base;
	mapping->begin = base + adjustment;
	mapping->end = base + size;

	section->mapping = mapping;
	section->unmap = pt_sec_windows_unmap;
	section->read = pt_sec_windows_read;
	section->memsize = pt_sec_windows_memsize;

	return 0;

out_map:
	UnmapViewOfFile(base);

out_mh:
	CloseHandle(mh);
	return errcode;
}

static int pt_sec_windows_map_success(struct pt_section *section)
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
	HANDLE fh;
	FILE *file;
	int fd, errcode;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	if (section->mcount)
		return pt_sec_windows_map_success(section);

	if (section->mapping) {
		errcode = -pte_internal;
		goto out_unlock;
	}

	filename = section->filename;
	if (!filename) {
		errcode = -pte_internal;
		goto out_unlock;
	}

	fh = CreateFile(filename, GENERIC_READ, FILE_SHARE_READ, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (fh == INVALID_HANDLE_VALUE) {
		/* We failed to open the file read-only.  Let's try to open it
		 * read-write; maybe our user has the file open for writing.
		 *
		 * We will detect changes to the file via fstat().
		 */

		fh = CreateFile(filename, GENERIC_READ, FILE_SHARE_WRITE, NULL,
				OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		if (fh == INVALID_HANDLE_VALUE) {
			errcode = -pte_bad_image;
			goto out_unlock;
		}
	}

	fd = _open_osfhandle((intptr_t) fh, _O_RDONLY);
	if (fd == -1) {
		errcode = -pte_bad_image;
		goto out_fh;
	}

	errcode = check_file_status(section, fd);
	if (errcode < 0) {
		errcode = -pte_bad_image;
		goto out_fd;
	}

	/* We leave the file open on success.  It will be closed when the
	 * section is unmapped.
	 */
	errcode = pt_sec_windows_map(section, fd);
	if (!errcode)
		return pt_sec_windows_map_success(section);

	/* Fall back to file based sections - report the original error
	 * if we fail to convert the file descriptor.
	 */
	file = _fdopen(fd, "rb");
	if (!file) {
		errcode = -pte_bad_image;
		goto out_fd;
	}

	/* We need to keep the file open on success.  It will be closed when
	 * the section is unmapped.
	 */
	errcode = pt_sec_file_map(section, file);
	if (!errcode)
		return pt_sec_windows_map_success(section);

	fclose(file);
	goto out_unlock;

out_fd:
	_close(fd);
	return errcode;

out_fh:
	CloseHandle(fh);

out_unlock:
	(void) pt_section_unlock(section);
	return errcode;
}

int pt_sec_windows_unmap(struct pt_section *section)
{
	struct pt_sec_windows_mapping *mapping;

	if (!section)
		return -pte_internal;

	mapping = section->mapping;
	if (!mapping || !section->unmap || !section->read || !section->memsize)
		return -pte_internal;

	section->mapping = NULL;
	section->unmap = NULL;
	section->read = NULL;
	section->memsize = NULL;

	UnmapViewOfFile(mapping->begin);
	CloseHandle(mapping->mh);
	_close(mapping->fd);
	free(mapping);

	return 0;
}

int pt_sec_windows_read(const struct pt_section *section, uint8_t *buffer,
		      uint16_t size, uint64_t offset)
{
	struct pt_sec_windows_mapping *mapping;
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


int pt_sec_windows_memsize(const struct pt_section *section, uint64_t *size)
{
	struct pt_sec_windows_mapping *mapping;
	const uint8_t *begin, *end;

	if (!section || !size)
		return -pte_internal;

	mapping = section->mapping;
	if (!mapping)
		return -pte_internal;

	begin = mapping->base;
	end =  mapping->end;

	if (!begin || !end || end < begin)
		return -pte_internal;

	*size = (uint64_t) (end - begin);

	return 0;
}
