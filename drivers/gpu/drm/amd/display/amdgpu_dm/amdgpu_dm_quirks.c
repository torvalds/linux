// SPDX-License-Identifier: MIT
/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#include <linux/dmi.h>

#include "amdgpu.h"
#include "amdgpu_dm.h"

struct amdgpu_dm_quirks {
	bool aux_hpd_discon;
	bool support_edp0_on_dp1;
};

static struct amdgpu_dm_quirks quirk_entries = {
	.aux_hpd_discon = false,
	.support_edp0_on_dp1 = false
};

static int edp0_on_dp1_callback(const struct dmi_system_id *id)
{
	quirk_entries.support_edp0_on_dp1 = true;
	return 0;
}

static int aux_hpd_discon_callback(const struct dmi_system_id *id)
{
	quirk_entries.aux_hpd_discon = true;
	return 0;
}

static const struct dmi_system_id dmi_quirk_table[] = {
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision 3660"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision 3260"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision 3460"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Tower Plus 7010"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Tower 7010"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex SFF Plus 7010"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex SFF 7010"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Micro Plus 7010"),
		},
	},
	{
		.callback = aux_hpd_discon_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex Micro 7010"),
		},
	},
	{
		.callback = edp0_on_dp1_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Elite mt645 G8 Mobile Thin Client"),
		},
	},
	{
		.callback = edp0_on_dp1_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP EliteBook 645 14 inch G11 Notebook PC"),
		},
	},
	{
		.callback = edp0_on_dp1_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP EliteBook 665 16 inch G11 Notebook PC"),
		},
	},
	{
		.callback = edp0_on_dp1_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP ProBook 445 14 inch G11 Notebook PC"),
		},
	},
	{
		.callback = edp0_on_dp1_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "HP"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP ProBook 465 16 inch G11 Notebook PC"),
		},
	},
	{}
	/* TODO: refactor this from a fixed table to a dynamic option */
};

void retrieve_dmi_info(struct amdgpu_display_manager *dm)
{
	struct drm_device *dev = dm->ddev;
	int dmi_id;

	dm->aux_hpd_discon_quirk = false;
	dm->edp0_on_dp1_quirk = false;

	dmi_id = dmi_check_system(dmi_quirk_table);

	if (!dmi_id)
		return;

	if (quirk_entries.aux_hpd_discon) {
		dm->aux_hpd_discon_quirk = true;
		drm_info(dev, "aux_hpd_discon_quirk attached\n");
	}
	if (quirk_entries.support_edp0_on_dp1) {
		dm->edp0_on_dp1_quirk = true;
		drm_info(dev, "support_edp0_on_dp1 attached\n");
	}
}
