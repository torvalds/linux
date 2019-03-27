/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2001  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: dir.h,v 1.21 2007/06/19 23:47:19 tbox Exp $ */

/* Principal Authors: DCL */

#ifndef ISC_DIR_H
#define ISC_DIR_H 1

/*! \file */

#include <sys/types.h>		/* Required on some systems. */
#include <dirent.h>

#include <isc/lang.h>
#include <isc/result.h>

#define ISC_DIR_NAMEMAX 256
#define ISC_DIR_PATHMAX 1024

/*% Directory Entry */
typedef struct isc_direntry {
	/*!
	 * Ideally, this should be NAME_MAX, but AIX does not define it by
	 * default and dynamically allocating the space based on pathconf()
	 * complicates things undesirably, as does adding special conditionals
	 * just for AIX.  So a comfortably sized buffer is chosen instead.
	 */
	char 		name[ISC_DIR_NAMEMAX];
	unsigned int	length;
} isc_direntry_t;

/*% Directory */
typedef struct isc_dir {
	unsigned int	magic;
	/*!
	 * As with isc_direntry_t->name, making this "right" for all systems
	 * is slightly problematic because AIX does not define PATH_MAX.
	 */
	char		dirname[ISC_DIR_PATHMAX];
	isc_direntry_t	entry;
	DIR *		handle;
} isc_dir_t;

ISC_LANG_BEGINDECLS

void
isc_dir_init(isc_dir_t *dir);

isc_result_t
isc_dir_open(isc_dir_t *dir, const char *dirname);

isc_result_t
isc_dir_read(isc_dir_t *dir);

isc_result_t
isc_dir_reset(isc_dir_t *dir);

void
isc_dir_close(isc_dir_t *dir);

isc_result_t
isc_dir_chdir(const char *dirname);

isc_result_t
isc_dir_chroot(const char *dirname);

isc_result_t
isc_dir_createunique(char *templet);
/*!<
 * Use a templet (such as from isc_file_mktemplate()) to create a uniquely
 * named, empty directory.  The templet string is modified in place.
 * If result == ISC_R_SUCCESS, it is the name of the directory that was
 * created.
 */

ISC_LANG_ENDDECLS

#endif /* ISC_DIR_H */
