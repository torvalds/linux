/*
 * Copyright (C) 2004, 2007-2009, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id$ */

/* Principal Authors: DCL */

#include <config.h>

#include <string.h>
#include <direct.h>
#include <process.h>
#include <io.h>

#include <sys/stat.h>

#include <isc/dir.h>
#include <isc/magic.h>
#include <isc/assertions.h>
#include <isc/util.h>

#include "errno2result.h"

#define ISC_DIR_MAGIC		ISC_MAGIC('D', 'I', 'R', '*')
#define VALID_DIR(dir)		ISC_MAGIC_VALID(dir, ISC_DIR_MAGIC)

static isc_result_t
start_directory(isc_dir_t *p);

void
isc_dir_init(isc_dir_t *dir) {
	REQUIRE(dir != NULL);

	dir->dirname[0] = '\0';

	dir->entry.name[0] = '\0';
	dir->entry.length = 0;
	memset(&(dir->entry.find_data), 0, sizeof(dir->entry.find_data));

	dir->entry_filled = ISC_FALSE;
	dir->search_handle = INVALID_HANDLE_VALUE;

	dir->magic = ISC_DIR_MAGIC;
}

/*
 * Allocate workspace and open directory stream. If either one fails,
 * NULL will be returned.
 */
isc_result_t
isc_dir_open(isc_dir_t *dir, const char *dirname) {
	char *p;
	isc_result_t result;

	REQUIRE(dirname != NULL);
	REQUIRE(VALID_DIR(dir) && dir->search_handle == INVALID_HANDLE_VALUE);

	/*
	 * Copy directory name.  Need to have enough space for the name,
	 * a possible path separator, the wildcard, and the final NUL.
	 */
	if (strlen(dirname) + 3 > sizeof(dir->dirname))
		/* XXXDCL ? */
		return (ISC_R_NOSPACE);
	strcpy(dir->dirname, dirname);

	/*
	 * Append path separator, if needed, and "*".
	 */
	p = dir->dirname + strlen(dir->dirname);
	if (dir->dirname < p && *(p - 1) != '\\' && *(p - 1) != ':')
		*p++ = '\\';
	*p++ = '*';
	*p = '\0';

	/*
	 * Open stream.
	 */
	result = start_directory(dir);

	return (result);
}

/*
 * Return previously retrieved file or get next one.  Unix's dirent has
 * separate open and read functions, but the Win32 and DOS interfaces open
 * the dir stream and reads the first file in one operation.
 */
isc_result_t
isc_dir_read(isc_dir_t *dir) {
	REQUIRE(VALID_DIR(dir) && dir->search_handle != INVALID_HANDLE_VALUE);

	if (dir->entry_filled)
		/*
		 * start_directory() already filled in the first entry.
		 */
		dir->entry_filled = ISC_FALSE;

	else {
		/*
		 * Fetch next file in directory.
		 */
		if (FindNextFile(dir->search_handle,
				 &dir->entry.find_data) == FALSE)
			/*
			 * Either the last file has been processed or
			 * an error has occurred.  The former is not
			 * really an error, but the latter is.
			 */
			if (GetLastError() == ERROR_NO_MORE_FILES)
				return (ISC_R_NOMORE);
			else
				return (ISC_R_UNEXPECTED);
	}

	/*
	 * Make sure that the space for the name is long enough.
	 */
	strcpy(dir->entry.name, dir->entry.find_data.cFileName);
	dir->entry.length = strlen(dir->entry.name);

	return (ISC_R_SUCCESS);
}

/*
 * Close directory stream.
 */
void
isc_dir_close(isc_dir_t *dir) {
       REQUIRE(VALID_DIR(dir) && dir->search_handle != INVALID_HANDLE_VALUE);

       FindClose(dir->search_handle);
       dir->search_handle = INVALID_HANDLE_VALUE;
}

/*
 * Reposition directory stream at start.
 */
isc_result_t
isc_dir_reset(isc_dir_t *dir) {
	isc_result_t result;

	REQUIRE(VALID_DIR(dir) && dir->search_handle != INVALID_HANDLE_VALUE);
	REQUIRE(dir->dirname != NULL);

	/*
	 * NT cannot reposition the seek pointer to the beginning of the
	 * the directory stream, but rather the directory needs to be
	 * closed and reopened.  The latter might fail.
	 */

	isc_dir_close(dir);

	result = start_directory(dir);

	return (result);
}

/*
 * Initialize isc_dir_t structure with new directory. The function
 * returns 0 on failure and nonzero on success.
 *
 * Note:
 * - Be sure to close previous stream before opening new one
 */
static isc_result_t
start_directory(isc_dir_t *dir)
{
	REQUIRE(VALID_DIR(dir));
	REQUIRE(dir->search_handle == INVALID_HANDLE_VALUE);

	dir->entry_filled = ISC_FALSE;

	/*
	 * Open stream and retrieve first file.
	 */
	dir->search_handle = FindFirstFile(dir->dirname,
					    &dir->entry.find_data);

	if (dir->search_handle == INVALID_HANDLE_VALUE) {
		/*
		 * Something went wrong but we don't know what. GetLastError()
		 * could give us more information about the error, but the
		 * MSDN documentation is frustratingly thin about what
		 * possible errors could have resulted.  (Score one for
		 * the Unix manual pages.)  So there is just this lame error
		 * instead of being able to differentiate ISC_R_NOTFOUND
		 * from ISC_R_UNEXPECTED.
		 */
		return (ISC_R_FAILURE);
	}

	/*
	 * Make sure that the space for the name is long enough.
	 */
	INSIST(sizeof(dir->entry.name) >
	       strlen(dir->entry.find_data.cFileName));

	/*
	 * Fill in the data for the first entry of the directory.
	 */
	strcpy(dir->entry.name, dir->entry.find_data.cFileName);
	dir->entry.length = strlen(dir->entry.name);

	dir->entry_filled = ISC_TRUE;

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_dir_chdir(const char *dirname) {
	/*
	 * Change the current directory to 'dirname'.
	 */

	REQUIRE(dirname != NULL);

	if (chdir(dirname) < 0)
		return (isc__errno2result(errno));

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_dir_chroot(const char *dirname) {
	return (ISC_R_NOTIMPLEMENTED);
}

isc_result_t
isc_dir_createunique(char *templet) {
	isc_result_t result;
	char *x;
	char *p;
	int i;
	int pid;

	REQUIRE(templet != NULL);

	/*
	 * mkdtemp is not portable, so this emulates it.
	 */

	pid = getpid();

	/*
	 * Replace trailing Xs with the process-id, zero-filled.
	 */
	for (x = templet + strlen(templet) - 1; *x == 'X' && x >= templet;
	     x--, pid /= 10)
		*x = pid % 10 + '0';

	x++;			/* Set x to start of ex-Xs. */

	do {
		i = mkdir(templet);
		i = chmod(templet, 0700);

		if (i == 0 || errno != EEXIST)
			break;

		/*
		 * The BSD algorithm.
		 */
		p = x;
		while (*p != '\0') {
			if (isdigit(*p & 0xff))
				*p = 'a';
			else if (*p != 'z')
				++*p;
			else {
				/*
				 * Reset character and move to next.
				 */
				*p++ = 'a';
				continue;
			}

			break;
		}

		if (*p == '\0') {
			/*
			 * Tried all combinations.  errno should already
			 * be EEXIST, but ensure it is anyway for
			 * isc__errno2result().
			 */
			errno = EEXIST;
			break;
		}
	} while (1);

	if (i == -1)
		result = isc__errno2result(errno);
	else
		result = ISC_R_SUCCESS;

	return (result);
}
