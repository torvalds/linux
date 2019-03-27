/*	$Id: dbm_map.c,v 1.8 2017/02/17 14:43:54 schwarze Exp $ */
/*
 * Copyright (c) 2016 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Low-level routines for the map-based version
 * of the mandoc database, for read-only access.
 * The interface is defined in "dbm_map.h".
 */
#include "config.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#if HAVE_ENDIAN
#include <endian.h>
#elif HAVE_SYS_ENDIAN
#include <sys/endian.h>
#elif HAVE_NTOHL
#include <arpa/inet.h>
#endif
#if HAVE_ERR
#include <err.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mansearch.h"
#include "dbm_map.h"
#include "dbm.h"

static struct stat	 st;
static char		*dbm_base;
static int		 ifd;
static int32_t		 max_offset;

/*
 * Open a disk-based database for read-only access.
 * Validate the file format as far as it is not mandoc-specific.
 * Return 0 on success.  Return -1 and set errno on failure.
 */
int
dbm_map(const char *fname)
{
	int		 save_errno;
	const int32_t	*magic;

	if ((ifd = open(fname, O_RDONLY)) == -1)
		return -1;
	if (fstat(ifd, &st) == -1)
		goto fail;
	if (st.st_size < 5) {
		warnx("dbm_map(%s): File too short", fname);
		errno = EFTYPE;
		goto fail;
	}
	if (st.st_size > INT32_MAX) {
		errno = EFBIG;
		goto fail;
	}
	if ((dbm_base = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED,
	    ifd, 0)) == MAP_FAILED)
		goto fail;
	magic = dbm_getint(0);
	if (be32toh(*magic) != MANDOCDB_MAGIC) {
		if (strncmp(dbm_base, "SQLite format 3", 15))
			warnx("dbm_map(%s): "
			    "Bad initial magic %x (expected %x)",
			    fname, be32toh(*magic), MANDOCDB_MAGIC);
		else
			warnx("dbm_map(%s): "
			    "Obsolete format based on SQLite 3",
			    fname);
		errno = EFTYPE;
		goto fail;
	}
	magic = dbm_getint(1);
	if (be32toh(*magic) != MANDOCDB_VERSION) {
		warnx("dbm_map(%s): Bad version number %d (expected %d)",
		    fname, be32toh(*magic), MANDOCDB_VERSION);
		errno = EFTYPE;
		goto fail;
	}
	max_offset = be32toh(*dbm_getint(3)) + sizeof(int32_t);
	if (st.st_size != max_offset) {
		warnx("dbm_map(%s): Inconsistent file size %lld (expected %d)",
		    fname, (long long)st.st_size, max_offset);
		errno = EFTYPE;
		goto fail;
	}
	if ((magic = dbm_get(*dbm_getint(3))) == NULL) {
		errno = EFTYPE;
		goto fail;
	}
	if (be32toh(*magic) != MANDOCDB_MAGIC) {
		warnx("dbm_map(%s): Bad final magic %x (expected %x)",
		    fname, be32toh(*magic), MANDOCDB_MAGIC);
		errno = EFTYPE;
		goto fail;
	}
	return 0;

fail:
	save_errno = errno;
	close(ifd);
	errno = save_errno;
	return -1;
}

void
dbm_unmap(void)
{
	if (munmap(dbm_base, st.st_size) == -1)
		warn("dbm_unmap: munmap");
	if (close(ifd) == -1)
		warn("dbm_unmap: close");
	dbm_base = (char *)-1;
}

/*
 * Take a raw integer as it was read from the database.
 * Interpret it as an offset into the database file
 * and return a pointer to that place in the file.
 */
void *
dbm_get(int32_t offset)
{
	offset = be32toh(offset);
	if (offset < 0) {
		warnx("dbm_get: Database corrupt: offset %d", offset);
		return NULL;
	}
	if (offset >= max_offset) {
		warnx("dbm_get: Database corrupt: offset %d > %d",
		    offset, max_offset);
		return NULL;
	}
	return dbm_base + offset;
}

/*
 * Assume the database starts with some integers.
 * Assume they are numbered starting from 0, increasing.
 * Get a pointer to one with the number "offset".
 */
int32_t *
dbm_getint(int32_t offset)
{
	return (int32_t *)dbm_base + offset;
}

/*
 * The reverse of dbm_get().
 * Take pointer into the database file
 * and convert it to the raw integer
 * that would be used to refer to that place in the file.
 */
int32_t
dbm_addr(const void *p)
{
	return htobe32((const char *)p - dbm_base);
}

int
dbm_match(const struct dbm_match *match, const char *str)
{
	switch (match->type) {
	case DBM_EXACT:
		return strcmp(str, match->str) == 0;
	case DBM_SUB:
		return strcasestr(str, match->str) != NULL;
	case DBM_REGEX:
		return regexec(match->re, str, 0, NULL, 0) == 0;
	default:
		abort();
	}
}
