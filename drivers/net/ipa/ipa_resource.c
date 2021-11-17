// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2012-2018, The Linux Foundation. All rights reserved.
 * Copyright (C) 2018-2021 Linaro Ltd.
 */

#include <linux/types.h>
#include <linux/kernel.h>

#include "ipa.h"
#include "ipa_data.h"
#include "ipa_reg.h"
#include "ipa_resource.h"

/**
 * DOC: IPA Resources
 *
 * The IPA manages a set of resources internally for various purposes.
 * A given IPA version has a fixed number of resource types, and a fixed
 * total number of resources of each type.  "Source" resource types
 * are separate from "destination" resource types.
 *
 * Each version of IPA also has some number of resource groups.  Each
 * endpoint is assigned to a resource group, and all endpoints in the
 * same group share pools of each type of resource.  A subset of the
 * total resources of each type is assigned for use by each group.
 */

static bool ipa_resource_limits_valid(struct ipa *ipa,
				      const struct ipa_resource_data *data)
{
	u32 group_count;
	u32 i;
	u32 j;

	/* We program at most 8 source or destination resource group limits */
	BUILD_BUG_ON(IPA_RESOURCE_GROUP_MAX > 8);

	group_count = data->rsrc_group_src_count;
	if (!group_count || group_count > IPA_RESOURCE_GROUP_MAX)
		return false;

	/* Return an error if a non-zero resource limit is specified
	 * for a resource group not supported by hardware.
	 */
	for (i = 0; i < data->resource_src_count; i++) {
		const struct ipa_resource *resource;

		resource = &data->resource_src[i];
		for (j = group_count; j < IPA_RESOURCE_GROUP_MAX; j++)
			if (resource->limits[j].min || resource->limits[j].max)
				return false;
	}

	group_count = data->rsrc_group_src_count;
	if (!group_count || group_count > IPA_RESOURCE_GROUP_MAX)
		return false;

	for (i = 0; i < data->resource_dst_count; i++) {
		const struct ipa_resource *resource;

		resource = &data->resource_dst[i];
		for (j = group_count; j < IPA_RESOURCE_GROUP_MAX; j++)
			if (resource->limits[j].min || resource->limits[j].max)
				return false;
	}

	return true;
}

static void
ipa_resource_config_common(struct ipa *ipa, u32 offset,
			   const struct ipa_resource_limits *xlimits,
			   const struct ipa_resource_limits *ylimits)
{
	u32 val;

	val = u32_encode_bits(xlimits->min, X_MIN_LIM_FMASK);
	val |= u32_encode_bits(xlimits->max, X_MAX_LIM_FMASK);
	if (ylimits) {
		val |= u32_encode_bits(ylimits->min, Y_MIN_LIM_FMASK);
		val |= u32_encode_bits(ylimits->max, Y_MAX_LIM_FMASK);
	}

	iowrite32(val, ipa->reg_virt + offset);
}

static void ipa_resource_config_src(struct ipa *ipa, u32 resource_type,
				    const struct ipa_resource_data *data)
{
	u32 group_count = data->rsrc_group_src_count;
	const struct ipa_resource_limits *ylimits;
	const struct ipa_resource *resource;
	u32 offset;

	resource = &data->resource_src[resource_type];

	offset = IPA_REG_SRC_RSRC_GRP_01_RSRC_TYPE_N_OFFSET(resource_type);
	ylimits = group_count == 1 ? NULL : &resource->limits[1];
	ipa_resource_config_common(ipa, offset, &resource->limits[0], ylimits);

	if (group_count < 3)
		return;

	offset = IPA_REG_SRC_RSRC_GRP_23_RSRC_TYPE_N_OFFSET(resource_type);
	ylimits = group_count == 3 ? NULL : &resource->limits[3];
	ipa_resource_config_common(ipa, offset, &resource->limits[2], ylimits);

	if (group_count < 5)
		return;

	offset = IPA_REG_SRC_RSRC_GRP_45_RSRC_TYPE_N_OFFSET(resource_type);
	ylimits = group_count == 5 ? NULL : &resource->limits[5];
	ipa_resource_config_common(ipa, offset, &resource->limits[4], ylimits);

	if (group_count < 7)
		return;

	offset = IPA_REG_SRC_RSRC_GRP_67_RSRC_TYPE_N_OFFSET(resource_type);
	ylimits = group_count == 7 ? NULL : &resource->limits[7];
	ipa_resource_config_common(ipa, offset, &resource->limits[6], ylimits);
}

static void ipa_resource_config_dst(struct ipa *ipa, u32 resource_type,
				    const struct ipa_resource_data *data)
{
	u32 group_count = data->rsrc_group_dst_count;
	const struct ipa_resource_limits *ylimits;
	const struct ipa_resource *resource;
	u32 offset;

	resource = &data->resource_dst[resource_type];

	offset = IPA_REG_DST_RSRC_GRP_01_RSRC_TYPE_N_OFFSET(resource_type);
	ylimits = group_count == 1 ? NULL : &resource->limits[1];
	ipa_resource_config_common(ipa, offset, &resource->limits[0], ylimits);

	if (group_count < 3)
		return;

	offset = IPA_REG_DST_RSRC_GRP_23_RSRC_TYPE_N_OFFSET(resource_type);
	ylimits = group_count == 3 ? NULL : &resource->limits[3];
	ipa_resource_config_common(ipa, offset, &resource->limits[2], ylimits);

	if (group_count < 5)
		return;

	offset = IPA_REG_DST_RSRC_GRP_45_RSRC_TYPE_N_OFFSET(resource_type);
	ylimits = group_count == 5 ? NULL : &resource->limits[5];
	ipa_resource_config_common(ipa, offset, &resource->limits[4], ylimits);

	if (group_count < 7)
		return;

	offset = IPA_REG_DST_RSRC_GRP_67_RSRC_TYPE_N_OFFSET(resource_type);
	ylimits = group_count == 7 ? NULL : &resource->limits[7];
	ipa_resource_config_common(ipa, offset, &resource->limits[6], ylimits);
}

/* Configure resources; there is no ipa_resource_deconfig() */
int ipa_resource_config(struct ipa *ipa, const struct ipa_resource_data *data)
{
	u32 i;

	if (!ipa_resource_limits_valid(ipa, data))
		return -EINVAL;

	for (i = 0; i < data->resource_src_count; i++)
		ipa_resource_config_src(ipa, i, data);

	for (i = 0; i < data->resource_dst_count; i++)
		ipa_resource_config_dst(ipa, i, data);

	return 0;
}
