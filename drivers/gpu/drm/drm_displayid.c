// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_displayid.h>
#include <drm/drm_edid.h>
#include <drm/drm_print.h>

static int validate_displayid(const u8 *displayid, int length, int idx)
{
	int i, dispid_length;
	u8 csum = 0;
	const struct displayid_header *base;

	base = (const struct displayid_header *)&displayid[idx];

	DRM_DEBUG_KMS("base revision 0x%x, length %d, %d %d\n",
		      base->rev, base->bytes, base->prod_id, base->ext_count);

	/* +1 for DispID checksum */
	dispid_length = sizeof(*base) + base->bytes + 1;
	if (dispid_length > length - idx)
		return -EINVAL;

	for (i = 0; i < dispid_length; i++)
		csum += displayid[idx + i];
	if (csum) {
		DRM_NOTE("DisplayID checksum invalid, remainder is %d\n", csum);
		return -EINVAL;
	}

	return 0;
}

static const u8 *drm_find_displayid_extension(const struct edid *edid,
					      int *length, int *idx,
					      int *ext_index)
{
	const u8 *displayid = drm_find_edid_extension(edid, DISPLAYID_EXT, ext_index);
	const struct displayid_header *base;
	int ret;

	if (!displayid)
		return NULL;

	/* EDID extensions block checksum isn't for us */
	*length = EDID_LENGTH - 1;
	*idx = 1;

	ret = validate_displayid(displayid, *length, *idx);
	if (ret)
		return NULL;

	base = (const struct displayid_header *)&displayid[*idx];
	*length = *idx + sizeof(*base) + base->bytes;

	return displayid;
}

void displayid_iter_edid_begin(const struct edid *edid,
			       struct displayid_iter *iter)
{
	memset(iter, 0, sizeof(*iter));

	iter->edid = edid;
}

static const struct displayid_block *
displayid_iter_block(const struct displayid_iter *iter)
{
	const struct displayid_block *block;

	if (!iter->section)
		return NULL;

	block = (const struct displayid_block *)&iter->section[iter->idx];

	if (iter->idx + sizeof(*block) <= iter->length &&
	    iter->idx + sizeof(*block) + block->num_bytes <= iter->length)
		return block;

	return NULL;
}

const struct displayid_block *
__displayid_iter_next(struct displayid_iter *iter)
{
	const struct displayid_block *block;

	if (!iter->edid)
		return NULL;

	if (iter->section) {
		/* current block should always be valid */
		block = displayid_iter_block(iter);
		if (WARN_ON(!block)) {
			iter->section = NULL;
			iter->edid = NULL;
			return NULL;
		}

		/* next block in section */
		iter->idx += sizeof(*block) + block->num_bytes;

		block = displayid_iter_block(iter);
		if (block)
			return block;
	}

	for (;;) {
		iter->section = drm_find_displayid_extension(iter->edid,
							     &iter->length,
							     &iter->idx,
							     &iter->ext_index);
		if (!iter->section) {
			iter->edid = NULL;
			return NULL;
		}

		iter->idx += sizeof(struct displayid_header);

		block = displayid_iter_block(iter);
		if (block)
			return block;
	}
}

void displayid_iter_end(struct displayid_iter *iter)
{
	memset(iter, 0, sizeof(*iter));
}
