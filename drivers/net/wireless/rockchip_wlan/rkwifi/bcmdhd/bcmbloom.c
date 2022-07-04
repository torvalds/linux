/*
 * Bloom filter support
 *
 * Copyright (C) 2020, Broadcom.
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
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 */

#include <typedefs.h>
#include <bcmdefs.h>

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0))
#include <stdarg.h>
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(5, 16, 0) */

#ifdef BCMDRIVER
#include <osl.h>
#include <bcmutils.h>
#else /* !BCMDRIVER */
#include <stdio.h>
#include <string.h>
#ifndef ASSERT
#define ASSERT(exp)
#endif
#endif /* !BCMDRIVER */
#include <bcmutils.h>

#include <bcmbloom.h>

#define BLOOM_BIT_LEN(_x) ((_x) << 3)

struct bcm_bloom_filter {
	void *cb_ctx;
	uint max_hash;
	bcm_bloom_hash_t *hash;	/* array of hash functions */
	uint filter_size; 		/* in bytes */
	uint8 *filter; 			/* can be NULL for validate only */
};

/* public interface */
int
bcm_bloom_create(bcm_bloom_alloc_t alloc_cb,
	bcm_bloom_free_t free_cb, void *cb_ctx, uint max_hash,
	uint filter_size, bcm_bloom_filter_t **bloom)
{
	int err = BCME_OK;
	bcm_bloom_filter_t *bp = NULL;

	if (!bloom || !alloc_cb || (max_hash == 0)) {
		err = BCME_BADARG;
		goto done;
	}

	bp = (*alloc_cb)(cb_ctx, sizeof(*bp));
	if (!bp) {
		err = BCME_NOMEM;
		goto done;
	}
	memset(bp, 0, sizeof(*bp));

	bp->cb_ctx = cb_ctx;
	bp->max_hash = max_hash;
	bp->hash = (*alloc_cb)(cb_ctx, sizeof(*bp->hash) * max_hash);
	if (!bp->hash) {
		err = BCME_NOMEM;
		goto done;
	}
	memset(bp->hash, 0, sizeof(*bp->hash) * max_hash);

	if (filter_size > 0) {
		bp->filter = (*alloc_cb)(cb_ctx, filter_size);
		if (!bp->filter) {
			err = BCME_NOMEM;
			goto done;
		}
		bp->filter_size = filter_size;
		memset(bp->filter, 0, filter_size);
	}

	*bloom = bp;

done:
	if (err != BCME_OK)
		bcm_bloom_destroy(&bp, free_cb);

	return err;
}

int
bcm_bloom_destroy(bcm_bloom_filter_t **bloom, bcm_bloom_free_t free_cb)
{
	int err = BCME_OK;
	bcm_bloom_filter_t *bp;

	if (!bloom || !*bloom || !free_cb)
		goto done;

	bp = *bloom;
	*bloom = NULL;

	if (bp->filter)
		(*free_cb)(bp->cb_ctx, bp->filter, bp->filter_size);
	if (bp->hash)
		(*free_cb)(bp->cb_ctx, bp->hash,
			sizeof(*bp->hash) * bp->max_hash);
	(*free_cb)(bp->cb_ctx, bp, sizeof(*bp));

done:
	return err;
}

int
bcm_bloom_add_hash(bcm_bloom_filter_t *bp, bcm_bloom_hash_t hash, uint *idx)
{
	uint i;

	if (!bp || !hash || !idx)
		return BCME_BADARG;

	for (i = 0; i < bp->max_hash; ++i) {
		if (bp->hash[i] == NULL)
			break;
	}

	if (i >= bp->max_hash)
		return BCME_NORESOURCE;

	bp->hash[i] = hash;
	*idx = i;
	return BCME_OK;
}

int
bcm_bloom_remove_hash(bcm_bloom_filter_t *bp, uint idx)
{
	if (!bp)
		return BCME_BADARG;

	if (idx >= bp->max_hash)
		return BCME_NOTFOUND;

	bp->hash[idx] = NULL;
	return BCME_OK;
}

bool
bcm_bloom_is_member(bcm_bloom_filter_t *bp,
	const uint8 *tag, uint tag_len, const uint8 *buf, uint buf_len)
{
	uint i;
	int err = BCME_OK;

	if (!tag || (tag_len == 0)) /* empty tag is always a member */
		goto done;

	/* use internal buffer if none was specified */
	if (!buf || (buf_len == 0)) {
		if (!bp->filter)	/* every one is a member of empty filter */
			goto done;

		buf = bp->filter;
		buf_len = bp->filter_size;
	}

	for (i = 0; i < bp->max_hash; ++i) {
		uint pos;
		if (!bp->hash[i])
			continue;
		pos = (*bp->hash[i])(bp->cb_ctx, i, tag, tag_len);

		/* all bits must be set for a match */
		if (isclr(buf, pos % BLOOM_BIT_LEN(buf_len))) {
			err = BCME_NOTFOUND;
			break;
		}
	}

done:
	return err;
}

int
bcm_bloom_add_member(bcm_bloom_filter_t *bp, const uint8 *tag, uint tag_len)
{
	uint i;

	if (!bp || !tag || (tag_len == 0))
		return BCME_BADARG;

	if (!bp->filter)		/* validate only */
		return BCME_UNSUPPORTED;

	for (i = 0; i < bp->max_hash; ++i) {
		uint pos;
		if (!bp->hash[i])
			continue;
		pos = (*bp->hash[i])(bp->cb_ctx, i, tag, tag_len);
		setbit(bp->filter, pos % BLOOM_BIT_LEN(bp->filter_size));
	}

	return BCME_OK;
}

int bcm_bloom_get_filter_data(bcm_bloom_filter_t *bp,
	uint buf_size, uint8 *buf, uint *buf_len)
{
	if (!bp)
		return BCME_BADARG;

	if (buf_len)
		*buf_len = bp->filter_size;

	if (buf_size < bp->filter_size)
		return BCME_BUFTOOSHORT;

	if (bp->filter && bp->filter_size)
		memcpy(buf, bp->filter, bp->filter_size);

	return BCME_OK;
}
