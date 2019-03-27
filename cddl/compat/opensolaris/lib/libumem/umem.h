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
 *
 * $FreeBSD$
 */
/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _UMEM_H
#define	_UMEM_H



#include <sys/types.h>
#include <stdlib.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	UMEM_DEFAULT	0x0000	/* normal -- may fail */
#define	UMEM_NOFAIL	0x0100	/* Never fails -- may call exit(2) */

#define	UMEM_FLAGS	0xffff	/* all settable umem flags */

extern void *umem_alloc(size_t, int);
extern void *umem_alloc_align(size_t, size_t, int);
extern void *umem_zalloc(size_t, int);
extern void umem_free(void *, size_t);
extern void umem_free_align(void *, size_t);

/*
 * Flags for umem_cache_create()
 */
#define	UMC_NOTOUCH	0x00010000
#define	UMC_NODEBUG	0x00020000
#define	UMC_NOMAGAZINE	0x00040000
#define	UMC_NOHASH	0x00080000

struct umem_cache;		/* cache structure is opaque to umem clients */

typedef struct umem_cache umem_cache_t;
typedef int umem_constructor_t(void *, void *, int);
typedef void umem_destructor_t(void *, void *);
typedef void umem_reclaim_t(void *);

typedef int umem_nofail_callback_t(void);
#define	UMEM_CALLBACK_RETRY		0
#define	UMEM_CALLBACK_EXIT(status)	(0x100 | ((status) & 0xFF))

extern void umem_nofail_callback(umem_nofail_callback_t *);

extern umem_cache_t *umem_cache_create(char *, size_t,
    size_t, umem_constructor_t *, umem_destructor_t *, umem_reclaim_t *,
    void *, void *, int);
extern void umem_cache_destroy(umem_cache_t *);

extern void *umem_cache_alloc(umem_cache_t *, int);
extern void umem_cache_free(umem_cache_t *, void *);

extern void umem_reap(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _UMEM_H */
