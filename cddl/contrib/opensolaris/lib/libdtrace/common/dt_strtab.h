/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_DT_STRTAB_H
#define	_DT_STRTAB_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct dt_strhash {
	const char *str_data;		/* pointer to actual string data */
	ulong_t str_buf;		/* index of string data buffer */
	size_t str_off;			/* offset in bytes of this string */
	size_t str_len;			/* length in bytes of this string */
	struct dt_strhash *str_next;	/* next string in hash chain */
} dt_strhash_t;

typedef struct dt_strtab {
	dt_strhash_t **str_hash;	/* array of hash buckets */
	ulong_t str_hashsz;		/* size of hash bucket array */
	char **str_bufs;		/* array of buffer pointers */
	char *str_ptr;			/* pointer to current buffer location */
	ulong_t str_nbufs;		/* size of buffer pointer array */
	size_t str_bufsz;		/* size of individual buffer */
	ulong_t str_nstrs;		/* total number of strings in strtab */
	size_t str_size;		/* total size of strings in bytes */
} dt_strtab_t;

typedef ssize_t dt_strtab_write_f(const char *, size_t, size_t, void *);

extern dt_strtab_t *dt_strtab_create(size_t);
extern void dt_strtab_destroy(dt_strtab_t *);
extern ssize_t dt_strtab_index(dt_strtab_t *, const char *);
extern ssize_t dt_strtab_insert(dt_strtab_t *, const char *);
extern size_t dt_strtab_size(const dt_strtab_t *);
extern ssize_t dt_strtab_write(const dt_strtab_t *,
    dt_strtab_write_f *, void *);
extern ulong_t dt_strtab_hash(const char *, size_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* _DT_STRTAB_H */
