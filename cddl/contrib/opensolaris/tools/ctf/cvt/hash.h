/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _HASH_H
#define	_HASH_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines for manipulating hash tables
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hash hash_t;

hash_t *hash_new(int, int (*)(int, void *), int (*)(void *, void *));
void hash_add(hash_t *, void *);
void hash_merge(hash_t *, hash_t *);
void hash_remove(hash_t *, void *);
int hash_find(hash_t *, void *, void **);
int hash_find_iter(hash_t *, void *, int (*)(void *, void *), void *);
int hash_iter(hash_t *, int (*)(void *, void *), void *);
int hash_match(hash_t *, void *, int (*)(void *, void *), void *);
int hash_count(hash_t *);
int hash_name(int, const char *);
void hash_stats(hash_t *, int);
void hash_free(hash_t *, void (*)(void *, void *), void *);

#ifdef __cplusplus
}
#endif

#endif /* _HASH_H */
