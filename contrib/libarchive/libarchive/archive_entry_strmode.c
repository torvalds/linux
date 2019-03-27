/*-
 * Copyright (c) 2003-2007 Tim Kientzle
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "archive_platform.h"
__FBSDID("$FreeBSD$");

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "archive_entry.h"
#include "archive_entry_private.h"

const char *
archive_entry_strmode(struct archive_entry *entry)
{
	static const mode_t permbits[] =
	    { 0400, 0200, 0100, 0040, 0020, 0010, 0004, 0002, 0001 };
	char *bp = entry->strmode;
	mode_t mode;
	int i;

	/* Fill in a default string, then selectively override. */
	strcpy(bp, "?rwxrwxrwx ");

	mode = archive_entry_mode(entry);
	switch (archive_entry_filetype(entry)) {
	case AE_IFREG:  bp[0] = '-'; break;
	case AE_IFBLK:  bp[0] = 'b'; break;
	case AE_IFCHR:  bp[0] = 'c'; break;
	case AE_IFDIR:  bp[0] = 'd'; break;
	case AE_IFLNK:  bp[0] = 'l'; break;
	case AE_IFSOCK: bp[0] = 's'; break;
	case AE_IFIFO:  bp[0] = 'p'; break;
	default:
		if (archive_entry_hardlink(entry) != NULL) {
			bp[0] = 'h';
			break;
		}
	}

	for (i = 0; i < 9; i++)
		if (!(mode & permbits[i]))
			bp[i+1] = '-';

	if (mode & S_ISUID) {
		if (mode & 0100) bp[3] = 's';
		else bp[3] = 'S';
	}
	if (mode & S_ISGID) {
		if (mode & 0010) bp[6] = 's';
		else bp[6] = 'S';
	}
	if (mode & S_ISVTX) {
		if (mode & 0001) bp[9] = 't';
		else bp[9] = 'T';
	}
	if (archive_entry_acl_types(entry) != 0)
		bp[10] = '+';

	return (bp);
}
