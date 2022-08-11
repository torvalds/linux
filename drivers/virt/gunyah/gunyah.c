// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "gunyah: " fmt

#include <linux/gunyah.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>

static struct gh_hypercall_hyp_identify_resp gh_api;

u16 gh_api_version(void)
{
	return FIELD_GET(GH_API_INFO_API_VERSION_MASK, gh_api.api_info);
}
EXPORT_SYMBOL_GPL(gh_api_version);

bool gh_api_has_feature(enum gh_api_feature feature)
{
	switch (feature) {
	case GH_FEATURE_DOORBELL:
	case GH_FEATURE_MSGQUEUE:
	case GH_FEATURE_VCPU:
	case GH_FEATURE_MEMEXTENT:
		return !!(gh_api.flags[0] & BIT_ULL(feature));
	default:
		return false;
	}
}
EXPORT_SYMBOL_GPL(gh_api_has_feature);

static int __init gh_init(void)
{
	if (!arch_is_gh_guest())
		return -ENODEV;

	gh_hypercall_hyp_identify(&gh_api);

	pr_info("Running under Gunyah hypervisor %llx/v%u\n",
		FIELD_GET(GH_API_INFO_VARIANT_MASK, gh_api.api_info),
		gh_api_version());

	/* We might move this out to individual drivers if there's ever an API version bump */
	if (gh_api_version() != GH_API_V1) {
		pr_info("Unsupported Gunyah version: %u\n", gh_api_version());
		return -ENODEV;
	}

	return 0;
}
arch_initcall(gh_init);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gunyah Hypervisor Driver");
