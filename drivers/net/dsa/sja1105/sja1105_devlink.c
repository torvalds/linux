// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 * Copyright 2020 NXP
 */
#include "sja1105.h"

/* Since devlink regions have a fixed size and the static config has a variable
 * size, we need to calculate the maximum possible static config size by
 * creating a dummy config with all table entries populated to the max, and get
 * its packed length. This is done dynamically as opposed to simply hardcoding
 * a number, since currently not all static config tables are implemented, so
 * we are avoiding a possible code desynchronization.
 */
static size_t sja1105_static_config_get_max_size(struct sja1105_private *priv)
{
	struct sja1105_static_config config;
	enum sja1105_blk_idx blk_idx;
	int rc;

	rc = sja1105_static_config_init(&config,
					priv->info->static_ops,
					priv->info->device_id);
	if (rc)
		return 0;

	for (blk_idx = 0; blk_idx < BLK_IDX_MAX; blk_idx++) {
		struct sja1105_table *table = &config.tables[blk_idx];

		table->entry_count = table->ops->max_entry_count;
	}

	return sja1105_static_config_get_length(&config);
}

static int
sja1105_region_static_config_snapshot(struct devlink *dl,
				      const struct devlink_region_ops *ops,
				      struct netlink_ext_ack *extack,
				      u8 **data)
{
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);
	struct sja1105_private *priv = ds->priv;
	size_t max_len, len;

	len = sja1105_static_config_get_length(&priv->static_config);
	max_len = sja1105_static_config_get_max_size(priv);

	*data = kcalloc(max_len, sizeof(u8), GFP_KERNEL);
	if (!*data)
		return -ENOMEM;

	return static_config_buf_prepare_for_upload(priv, *data, len);
}

static struct devlink_region_ops sja1105_region_static_config_ops = {
	.name = "static-config",
	.snapshot = sja1105_region_static_config_snapshot,
	.destructor = kfree,
};

enum sja1105_region_id {
	SJA1105_REGION_STATIC_CONFIG = 0,
};

struct sja1105_region {
	const struct devlink_region_ops *ops;
	size_t (*get_size)(struct sja1105_private *priv);
};

static struct sja1105_region sja1105_regions[] = {
	[SJA1105_REGION_STATIC_CONFIG] = {
		.ops = &sja1105_region_static_config_ops,
		.get_size = sja1105_static_config_get_max_size,
	},
};

static int sja1105_setup_devlink_regions(struct dsa_switch *ds)
{
	int i, num_regions = ARRAY_SIZE(sja1105_regions);
	struct sja1105_private *priv = ds->priv;
	const struct devlink_region_ops *ops;
	struct devlink_region *region;
	u64 size;

	priv->regions = kcalloc(num_regions, sizeof(struct devlink_region *),
				GFP_KERNEL);
	if (!priv->regions)
		return -ENOMEM;

	for (i = 0; i < num_regions; i++) {
		size = sja1105_regions[i].get_size(priv);
		ops = sja1105_regions[i].ops;

		region = dsa_devlink_region_create(ds, ops, 1, size);
		if (IS_ERR(region)) {
			while (--i >= 0)
				dsa_devlink_region_destroy(priv->regions[i]);

			kfree(priv->regions);
			return PTR_ERR(region);
		}

		priv->regions[i] = region;
	}

	return 0;
}

static void sja1105_teardown_devlink_regions(struct dsa_switch *ds)
{
	int i, num_regions = ARRAY_SIZE(sja1105_regions);
	struct sja1105_private *priv = ds->priv;

	for (i = 0; i < num_regions; i++)
		dsa_devlink_region_destroy(priv->regions[i]);

	kfree(priv->regions);
}

int sja1105_devlink_info_get(struct dsa_switch *ds,
			     struct devlink_info_req *req,
			     struct netlink_ext_ack *extack)
{
	struct sja1105_private *priv = ds->priv;

	return devlink_info_version_fixed_put(req,
					      DEVLINK_INFO_VERSION_GENERIC_ASIC_ID,
					      priv->info->name);
}

int sja1105_devlink_setup(struct dsa_switch *ds)
{
	return sja1105_setup_devlink_regions(ds);
}

void sja1105_devlink_teardown(struct dsa_switch *ds)
{
	sja1105_teardown_devlink_regions(ds);
}
