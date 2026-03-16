// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <drm/drm_ras_genl_family.h>
#include "drm_ras_nl.h"

/* Track family registration so the drm_exit can be called at any time */
static bool registered;

/**
 * drm_ras_genl_family_register() - Register drm-ras genl family
 *
 * Only to be called one at drm_drv_init()
 */
int drm_ras_genl_family_register(void)
{
	int ret;

	registered = false;

	ret = genl_register_family(&drm_ras_nl_family);
	if (ret)
		return ret;

	registered = true;
	return 0;
}

/**
 * drm_ras_genl_family_unregister() - Unregister drm-ras genl family
 *
 * To be called one at drm_drv_exit() at any moment, but only once.
 */
void drm_ras_genl_family_unregister(void)
{
	if (registered) {
		genl_unregister_family(&drm_ras_nl_family);
		registered = false;
	}
}
