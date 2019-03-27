/*
 * Copyright (c) 2008 Voltaire Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */
#ifndef _UMAD_SYSFS_H
#define _UMAD_SYSFS_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <infiniband/types.h>
#include <infiniband/umad.h>

struct dirent;

extern int sys_read_string(const char *dir_name, const char *file_name, char *str, int len);
extern int sys_read_guid(const char *dir_name, const char *file_name, __be64 * net_guid);
extern int sys_read_gid(const char *dir_name, const char *file_name,
			union umad_gid *gid);
extern int sys_read_uint64(const char *dir_name, const char *file_name, uint64_t * u);
extern int sys_read_uint(const char *dir_name, const char *file_name, unsigned *u);
extern int sys_scandir(const char *dirname, struct dirent ***namelist,
    int (*select)(const struct dirent *),
    int (*compar)(const struct dirent **, const struct dirent **));

#ifdef __FreeBSD__
static inline const char *
path_to_sysctl(const char *path, int out_len, char *out)
{
	const char *retval = out;

	/* Validate that out is at least as long as the original path */
	if (out_len < (strlen(path) + 1))
		return NULL;

	while (*path == '/')
		path++;

	while (*path) {
		if (*path == '/') {
			if (*(path + 1) == '/')
				*out = '.';
			else
				*out++ = '.';
		} else
			*out++ = *path;
		path++;
	}
	*out = 0;
	return (retval);
}

#define	PATH_TO_SYS(str) \
	path_to_sysctl(str, strlen(str) + 1, alloca(strlen(str) + 1))
#else
#define	PATH_TO_SYS(str) str
#endif

#endif /* _UMAD_SYSFS_H */
