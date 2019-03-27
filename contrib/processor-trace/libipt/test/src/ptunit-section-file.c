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
#include "pt_section_file.h"

#include "intel-pt.h"

#include <stdlib.h>
#include <stdio.h>


/* This is a variation of ptunit-section.c.
 *
 * We provide pt_section_map() et.al. that are normally provided by mmap-based
 * section implementations.  Our implementation falls back to file-based
 * sections so we're able to test them.
 *
 * The actual test is in ptunit-section.c.
 */

/* The file status used for detecting changes to a file between unmap and map.
 *
 * In our case, the changes always affect the size of the file.
 */
struct pt_file_status {
	/* The size in bytes. */
	long size;
};

int pt_section_mk_status(void **pstatus, uint64_t *psize, const char *filename)
{
	struct pt_file_status *status;
	FILE *file;
	long size;
	int errcode;

	if (!pstatus || !psize)
		return -pte_internal;

	file = fopen(filename, "rb");
	if (!file)
		return -pte_bad_image;

	errcode = fseek(file, 0, SEEK_END);
	if (errcode) {
		errcode = -pte_bad_image;
		goto out_file;
	}

	size = ftell(file);
	if (size < 0) {
		errcode = -pte_bad_image;
		goto out_file;
	}

	status = malloc(sizeof(*status));
	if (!status) {
		errcode = -pte_nomem;
		goto out_file;
	}

	status->size = size;

	*pstatus = status;
	*psize = (uint64_t) size;

	errcode = 0;

out_file:
	fclose(file);
	return errcode;
}

static int pt_section_map_success(struct pt_section *section)
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
		(void) pt_section_unmap(section);
		return status;
	}

	return 0;
}

int pt_section_map(struct pt_section *section)
{
	struct pt_file_status *status;
	const char *filename;
	uint16_t mcount;
	FILE *file;
	long size;
	int errcode;

	if (!section)
		return -pte_internal;

	errcode = pt_section_lock(section);
	if (errcode < 0)
		return errcode;

	mcount = section->mcount;
	if (mcount)
		return pt_section_map_success(section);

	if (section->mapping)
		goto out_unlock;

	filename = section->filename;
	if (!filename)
		goto out_unlock;

	status = section->status;
	if (!status)
		goto out_unlock;

	errcode = -pte_bad_image;
	file = fopen(filename, "rb");
	if (!file)
		goto out_unlock;

	errcode = fseek(file, 0, SEEK_END);
	if (errcode) {
		errcode = -pte_bad_image;
		goto out_file;
	}

	errcode = -pte_bad_image;
	size = ftell(file);
	if (size < 0)
		goto out_file;

	if (size != status->size)
		goto out_file;

	/* We need to keep the file open on success.  It will be closed when
	 * the section is unmapped.
	 */
	errcode = pt_sec_file_map(section, file);
	if (!errcode)
		return pt_section_map_success(section);

out_file:
	fclose(file);

out_unlock:
	(void) pt_section_unlock(section);
	return errcode;
}
