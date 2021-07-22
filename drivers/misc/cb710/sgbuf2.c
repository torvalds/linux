// SPDX-License-Identifier: GPL-2.0-only
/*
 *  cb710/sgbuf2.c
 *
 *  Copyright by Michał Mirosław, 2008-2009
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cb710.h>

static bool sg_dwiter_next(struct sg_mapping_iter *miter)
{
	if (sg_miter_next(miter)) {
		miter->consumed = 0;
		return true;
	} else
		return false;
}

static bool sg_dwiter_is_at_end(struct sg_mapping_iter *miter)
{
	return miter->length == miter->consumed && !sg_dwiter_next(miter);
}

static uint32_t sg_dwiter_read_buffer(struct sg_mapping_iter *miter)
{
	size_t len, left = 4;
	uint32_t data;
	void *addr = &data;

	do {
		len = min(miter->length - miter->consumed, left);
		memcpy(addr, miter->addr + miter->consumed, len);
		miter->consumed += len;
		left -= len;
		if (!left)
			return data;
		addr += len;
	} while (sg_dwiter_next(miter));

	memset(addr, 0, left);
	return data;
}

static inline bool needs_unaligned_copy(const void *ptr)
{
#ifdef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	return false;
#else
	return ((ptr - NULL) & 3) != 0;
#endif
}

static bool sg_dwiter_get_next_block(struct sg_mapping_iter *miter, uint32_t **ptr)
{
	size_t len;

	if (sg_dwiter_is_at_end(miter))
		return true;

	len = miter->length - miter->consumed;

	if (likely(len >= 4 && !needs_unaligned_copy(
			miter->addr + miter->consumed))) {
		*ptr = miter->addr + miter->consumed;
		miter->consumed += 4;
		return true;
	}

	return false;
}

/**
 * cb710_sg_dwiter_read_next_block() - get next 32-bit word from sg buffer
 * @miter: sg mapping iterator used for reading
 *
 * Description:
 *   Returns 32-bit word starting at byte pointed to by @miter@
 *   handling any alignment issues.  Bytes past the buffer's end
 *   are not accessed (read) but are returned as zeroes.  @miter@
 *   is advanced by 4 bytes or to the end of buffer whichever is
 *   closer.
 *
 * Context:
 *   Same requirements as in sg_miter_next().
 *
 * Returns:
 *   32-bit word just read.
 */
uint32_t cb710_sg_dwiter_read_next_block(struct sg_mapping_iter *miter)
{
	uint32_t *ptr = NULL;

	if (likely(sg_dwiter_get_next_block(miter, &ptr)))
		return ptr ? *ptr : 0;

	return sg_dwiter_read_buffer(miter);
}
EXPORT_SYMBOL_GPL(cb710_sg_dwiter_read_next_block);

static void sg_dwiter_write_slow(struct sg_mapping_iter *miter, uint32_t data)
{
	size_t len, left = 4;
	void *addr = &data;

	do {
		len = min(miter->length - miter->consumed, left);
		memcpy(miter->addr, addr, len);
		miter->consumed += len;
		left -= len;
		if (!left)
			return;
		addr += len;
	} while (sg_dwiter_next(miter));
}

/**
 * cb710_sg_dwiter_write_next_block() - write next 32-bit word to sg buffer
 * @miter: sg mapping iterator used for writing
 * @data: data to write to sg buffer
 *
 * Description:
 *   Writes 32-bit word starting at byte pointed to by @miter@
 *   handling any alignment issues.  Bytes which would be written
 *   past the buffer's end are silently discarded. @miter@ is
 *   advanced by 4 bytes or to the end of buffer whichever is closer.
 *
 * Context:
 *   Same requirements as in sg_miter_next().
 */
void cb710_sg_dwiter_write_next_block(struct sg_mapping_iter *miter, uint32_t data)
{
	uint32_t *ptr = NULL;

	if (likely(sg_dwiter_get_next_block(miter, &ptr))) {
		if (ptr)
			*ptr = data;
		else
			return;
	} else
		sg_dwiter_write_slow(miter, data);
}
EXPORT_SYMBOL_GPL(cb710_sg_dwiter_write_next_block);

