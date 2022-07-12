/*
 * Bloom filter support
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#ifndef _bcmbloom_h_
#define _bcmbloom_h_

#include <typedefs.h>
#ifdef BCMDRIVER
#include <osl.h>
#else
#include <stddef.h>  /* For size_t */
#endif // endif

struct bcm_bloom_filter;
typedef struct bcm_bloom_filter bcm_bloom_filter_t;

typedef void* (*bcm_bloom_alloc_t)(void *ctx, uint size);
typedef void (*bcm_bloom_free_t)(void *ctx, void *buf, uint size);
typedef uint (*bcm_bloom_hash_t)(void* ctx, uint idx, const uint8 *tag, uint len);

/* create/allocate a bloom filter. filter size can be 0 for validate only filters */
int bcm_bloom_create(bcm_bloom_alloc_t alloc_cb,
	bcm_bloom_free_t free_cb, void *callback_ctx, uint max_hash,
	uint filter_size /* bytes */, bcm_bloom_filter_t **bloom);

/* destroy bloom filter */
int bcm_bloom_destroy(bcm_bloom_filter_t **bloom, bcm_bloom_free_t free_cb);

/* add a hash function to filter, return an index */
int bcm_bloom_add_hash(bcm_bloom_filter_t *filter, bcm_bloom_hash_t hash, uint *idx);

/* remove the hash function at index from filter */
int bcm_bloom_remove_hash(bcm_bloom_filter_t *filter, uint idx);

/* check if given tag is member of the filter. If buf is NULL and/or buf_len is 0
 * then use the internal state. BCME_OK if member, BCME_NOTFOUND if not,
 * or other error (e.g. BADARG)
 */
bool bcm_bloom_is_member(bcm_bloom_filter_t *filter,
	const uint8 *tag, uint tag_len, const uint8 *buf, uint buf_len);

/* add a member to the filter. invalid for validate_only filters */
int bcm_bloom_add_member(bcm_bloom_filter_t *filter, const uint8 *tag, uint tag_len);

/* no support for remove member */

/* get the filter data from state. BCME_BUFTOOSHORT w/ required length in buf_len
 * if supplied size is insufficient
 */
int bcm_bloom_get_filter_data(bcm_bloom_filter_t *filter,
	uint buf_size, uint8 *buf, uint *buf_len);

#endif /* _bcmbloom_h_ */
