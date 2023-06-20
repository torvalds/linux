// SPDX-License-Identifier: MIT

#include <linux/module.h>

#include "drm_dp_helper_internal.h"

MODULE_DESCRIPTION("DRM display adapter helper");
MODULE_LICENSE("GPL and additional rights");

static int __init drm_display_helper_module_init(void)
{
	return drm_dp_aux_dev_init();
}

static void __exit drm_display_helper_module_exit(void)
{
	/* Call exit functions from specific dp helpers here */
	drm_dp_aux_dev_exit();
}

module_init(drm_display_helper_module_init);
module_exit(drm_display_helper_module_exit);
