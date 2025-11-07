// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_edid.h>
#include <drm/drm_print.h>

#include "drm_crtc_internal.h"
#include "drm_displayid_internal.h"

enum {
	QUIRK_IGNORE_CHECKSUM,
};

struct displayid_quirk {
	const struct drm_edid_ident ident;
	u8 quirks;
};

static const struct displayid_quirk quirks[] = {
	{
		.ident = DRM_EDID_IDENT_INIT('C', 'S', 'O', 5142, "MNE007ZA1-5"),
		.quirks = BIT(QUIRK_IGNORE_CHECKSUM),
	},
};

static u8 get_quirks(const struct drm_edid *drm_edid)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(quirks); i++) {
		if (drm_edid_match(drm_edid, &quirks[i].ident))
			return quirks[i].quirks;
	}

	return 0;
}

static const struct displayid_header *
displayid_get_header(const u8 *displayid, int length, int index)
{
	const struct displayid_header *base;

	if (sizeof(*base) > length - index)
		return ERR_PTR(-EINVAL);

	base = (const struct displayid_header *)&displayid[index];

	return base;
}

static const struct displayid_header *
validate_displayid(const u8 *displayid, int length, int idx, bool ignore_checksum)
{
	int i, dispid_length;
	u8 csum = 0;
	const struct displayid_header *base;

	base = displayid_get_header(displayid, length, idx);
	if (IS_ERR(base))
		return base;

	/* +1 for DispID checksum */
	dispid_length = sizeof(*base) + base->bytes + 1;
	if (dispid_length > length - idx)
		return ERR_PTR(-EINVAL);

	for (i = 0; i < dispid_length; i++)
		csum += displayid[idx + i];
	if (csum) {
		DRM_NOTE("DisplayID checksum invalid, remainder is %d%s\n", csum,
			 ignore_checksum ? " (ignoring)" : "");

		if (!ignore_checksum)
			return ERR_PTR(-EINVAL);
	}

	return base;
}

static const u8 *find_next_displayid_extension(struct displayid_iter *iter)
{
	const struct displayid_header *base;
	const u8 *displayid;
	bool ignore_checksum = iter->quirks & BIT(QUIRK_IGNORE_CHECKSUM);

	displayid = drm_edid_find_extension(iter->drm_edid, DISPLAYID_EXT, &iter->ext_index);
	if (!displayid)
		return NULL;

	/* EDID extensions block checksum isn't for us */
	iter->length = EDID_LENGTH - 1;
	iter->idx = 1;

	base = validate_displayid(displayid, iter->length, iter->idx, ignore_checksum);
	if (IS_ERR(base))
		return NULL;

	iter->length = iter->idx + sizeof(*base) + base->bytes;

	return displayid;
}

void displayid_iter_edid_begin(const struct drm_edid *drm_edid,
			       struct displayid_iter *iter)
{
	memset(iter, 0, sizeof(*iter));

	iter->drm_edid = drm_edid;
	iter->quirks = get_quirks(drm_edid);
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

	if (!iter->drm_edid)
		return NULL;

	if (iter->section) {
		/* current block should always be valid */
		block = displayid_iter_block(iter);
		if (WARN_ON(!block)) {
			iter->section = NULL;
			iter->drm_edid = NULL;
			return NULL;
		}

		/* next block in section */
		iter->idx += sizeof(*block) + block->num_bytes;

		block = displayid_iter_block(iter);
		if (block)
			return block;
	}

	for (;;) {
		/* The first section we encounter is the base section */
		bool base_section = !iter->section;

		iter->section = find_next_displayid_extension(iter);
		if (!iter->section) {
			iter->drm_edid = NULL;
			return NULL;
		}

		/* Save the structure version and primary use case. */
		if (base_section) {
			const struct displayid_header *base;

			base = displayid_get_header(iter->section, iter->length,
						    iter->idx);
			if (!IS_ERR(base)) {
				iter->version = base->rev;
				iter->primary_use = base->prod_id;
			}
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

/* DisplayID Structure Version/Revision from the Base Section. */
u8 displayid_version(const struct displayid_iter *iter)
{
	return iter->version;
}

/*
 * DisplayID Primary Use Case (2.0+) or Product Type Identifier (1.0-1.3) from
 * the Base Section.
 */
u8 displayid_primary_use(const struct displayid_iter *iter)
{
	return iter->primary_use;
}
