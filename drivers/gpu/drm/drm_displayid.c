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
	const struct displayid_hdr *base;

	base = (const struct displayid_hdr *)&displayid[idx];

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

const u8 *drm_find_displayid_extension(const struct edid *edid,
				       int *length, int *idx,
				       int *ext_index)
{
	const u8 *displayid = drm_find_edid_extension(edid, DISPLAYID_EXT, ext_index);
	const struct displayid_hdr *base;
	int ret;

	if (!displayid)
		return NULL;

	/* EDID extensions block checksum isn't for us */
	*length = EDID_LENGTH - 1;
	*idx = 1;

	ret = validate_displayid(displayid, *length, *idx);
	if (ret)
		return NULL;

	base = (const struct displayid_hdr *)&displayid[*idx];
	*length = *idx + sizeof(*base) + base->bytes;

	return displayid;
}
