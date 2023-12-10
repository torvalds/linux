// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/drm_edid.h>
#include <drm/drm_eld.h>

#include "drm_internal.h"

/**
 * drm_eld_sad_get - get SAD from ELD to struct cea_sad
 * @eld: ELD buffer
 * @sad_index: SAD index
 * @cta_sad: destination struct cea_sad
 *
 * @return: 0 on success, or negative on errors
 */
int drm_eld_sad_get(const u8 *eld, int sad_index, struct cea_sad *cta_sad)
{
	const u8 *sad;

	if (sad_index >= drm_eld_sad_count(eld))
		return -EINVAL;

	sad = eld + DRM_ELD_CEA_SAD(drm_eld_mnl(eld), sad_index);

	drm_edid_cta_sad_set(cta_sad, sad);

	return 0;
}
EXPORT_SYMBOL(drm_eld_sad_get);

/**
 * drm_eld_sad_set - set SAD to ELD from struct cea_sad
 * @eld: ELD buffer
 * @sad_index: SAD index
 * @cta_sad: source struct cea_sad
 *
 * @return: 0 on success, or negative on errors
 */
int drm_eld_sad_set(u8 *eld, int sad_index, const struct cea_sad *cta_sad)
{
	u8 *sad;

	if (sad_index >= drm_eld_sad_count(eld))
		return -EINVAL;

	sad = eld + DRM_ELD_CEA_SAD(drm_eld_mnl(eld), sad_index);

	drm_edid_cta_sad_get(cta_sad, sad);

	return 0;
}
EXPORT_SYMBOL(drm_eld_sad_set);
