// SPDX-License-Identifier: GPL-2.0-only
/******************************************************************************
 * features.c
 *
 * Xen feature flags.
 *
 * Copyright (c) 2006, Ian Campbell, XenSource Inc.
 */
#include <linux/types.h>
#include <linux/cache.h>
#include <linux/export.h>

#include <asm/xen/hypercall.h>

#include <xen/interface/xen.h>
#include <xen/interface/version.h>
#include <xen/features.h>

u8 xen_features[XENFEAT_NR_SUBMAPS * 32] __read_mostly;
EXPORT_SYMBOL_GPL(xen_features);

void xen_setup_features(void)
{
	struct xen_feature_info fi;
	int i, j;

	for (i = 0; i < XENFEAT_NR_SUBMAPS; i++) {
		fi.submap_idx = i;
		if (HYPERVISOR_xen_version(XENVER_get_features, &fi) < 0)
			break;
		for (j = 0; j < 32; j++)
			xen_features[i * 32 + j] = !!(fi.submap & 1<<j);
	}
}
