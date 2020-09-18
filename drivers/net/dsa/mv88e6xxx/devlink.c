// SPDX-License-Identifier: GPL-2.0-or-later
#include <net/dsa.h>

#include "chip.h"
#include "devlink.h"
#include "global1.h"
#include "global2.h"

static int mv88e6xxx_atu_get_hash(struct mv88e6xxx_chip *chip, u8 *hash)
{
	if (chip->info->ops->atu_get_hash)
		return chip->info->ops->atu_get_hash(chip, hash);

	return -EOPNOTSUPP;
}

static int mv88e6xxx_atu_set_hash(struct mv88e6xxx_chip *chip, u8 hash)
{
	if (chip->info->ops->atu_set_hash)
		return chip->info->ops->atu_set_hash(chip, hash);

	return -EOPNOTSUPP;
}

enum mv88e6xxx_devlink_param_id {
	MV88E6XXX_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	MV88E6XXX_DEVLINK_PARAM_ID_ATU_HASH,
};

int mv88e6xxx_devlink_param_get(struct dsa_switch *ds, u32 id,
				struct devlink_param_gset_ctx *ctx)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);

	switch (id) {
	case MV88E6XXX_DEVLINK_PARAM_ID_ATU_HASH:
		err = mv88e6xxx_atu_get_hash(chip, &ctx->val.vu8);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	mv88e6xxx_reg_unlock(chip);

	return err;
}

int mv88e6xxx_devlink_param_set(struct dsa_switch *ds, u32 id,
				struct devlink_param_gset_ctx *ctx)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	mv88e6xxx_reg_lock(chip);

	switch (id) {
	case MV88E6XXX_DEVLINK_PARAM_ID_ATU_HASH:
		err = mv88e6xxx_atu_set_hash(chip, ctx->val.vu8);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	mv88e6xxx_reg_unlock(chip);

	return err;
}

static const struct devlink_param mv88e6xxx_devlink_params[] = {
	DSA_DEVLINK_PARAM_DRIVER(MV88E6XXX_DEVLINK_PARAM_ID_ATU_HASH,
				 "ATU_hash", DEVLINK_PARAM_TYPE_U8,
				 BIT(DEVLINK_PARAM_CMODE_RUNTIME)),
};

int mv88e6xxx_setup_devlink_params(struct dsa_switch *ds)
{
	return dsa_devlink_params_register(ds, mv88e6xxx_devlink_params,
					   ARRAY_SIZE(mv88e6xxx_devlink_params));
}

void mv88e6xxx_teardown_devlink_params(struct dsa_switch *ds)
{
	dsa_devlink_params_unregister(ds, mv88e6xxx_devlink_params,
				      ARRAY_SIZE(mv88e6xxx_devlink_params));
}

enum mv88e6xxx_devlink_resource_id {
	MV88E6XXX_RESOURCE_ID_ATU,
	MV88E6XXX_RESOURCE_ID_ATU_BIN_0,
	MV88E6XXX_RESOURCE_ID_ATU_BIN_1,
	MV88E6XXX_RESOURCE_ID_ATU_BIN_2,
	MV88E6XXX_RESOURCE_ID_ATU_BIN_3,
};

static u64 mv88e6xxx_devlink_atu_bin_get(struct mv88e6xxx_chip *chip,
					 u16 bin)
{
	u16 occupancy = 0;
	int err;

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_g2_atu_stats_set(chip, MV88E6XXX_G2_ATU_STATS_MODE_ALL,
					 bin);
	if (err) {
		dev_err(chip->dev, "failed to set ATU stats kind/bin\n");
		goto unlock;
	}

	err = mv88e6xxx_g1_atu_get_next(chip, 0);
	if (err) {
		dev_err(chip->dev, "failed to perform ATU get next\n");
		goto unlock;
	}

	err = mv88e6xxx_g2_atu_stats_get(chip, &occupancy);
	if (err) {
		dev_err(chip->dev, "failed to get ATU stats\n");
		goto unlock;
	}

	occupancy &= MV88E6XXX_G2_ATU_STATS_MASK;

unlock:
	mv88e6xxx_reg_unlock(chip);

	return occupancy;
}

static u64 mv88e6xxx_devlink_atu_bin_0_get(void *priv)
{
	struct mv88e6xxx_chip *chip = priv;

	return mv88e6xxx_devlink_atu_bin_get(chip,
					     MV88E6XXX_G2_ATU_STATS_BIN_0);
}

static u64 mv88e6xxx_devlink_atu_bin_1_get(void *priv)
{
	struct mv88e6xxx_chip *chip = priv;

	return mv88e6xxx_devlink_atu_bin_get(chip,
					     MV88E6XXX_G2_ATU_STATS_BIN_1);
}

static u64 mv88e6xxx_devlink_atu_bin_2_get(void *priv)
{
	struct mv88e6xxx_chip *chip = priv;

	return mv88e6xxx_devlink_atu_bin_get(chip,
					     MV88E6XXX_G2_ATU_STATS_BIN_2);
}

static u64 mv88e6xxx_devlink_atu_bin_3_get(void *priv)
{
	struct mv88e6xxx_chip *chip = priv;

	return mv88e6xxx_devlink_atu_bin_get(chip,
					     MV88E6XXX_G2_ATU_STATS_BIN_3);
}

static u64 mv88e6xxx_devlink_atu_get(void *priv)
{
	return mv88e6xxx_devlink_atu_bin_0_get(priv) +
		mv88e6xxx_devlink_atu_bin_1_get(priv) +
		mv88e6xxx_devlink_atu_bin_2_get(priv) +
		mv88e6xxx_devlink_atu_bin_3_get(priv);
}

int mv88e6xxx_setup_devlink_resources(struct dsa_switch *ds)
{
	struct devlink_resource_size_params size_params;
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	devlink_resource_size_params_init(&size_params,
					  mv88e6xxx_num_macs(chip),
					  mv88e6xxx_num_macs(chip),
					  1, DEVLINK_RESOURCE_UNIT_ENTRY);

	err = dsa_devlink_resource_register(ds, "ATU",
					    mv88e6xxx_num_macs(chip),
					    MV88E6XXX_RESOURCE_ID_ATU,
					    DEVLINK_RESOURCE_ID_PARENT_TOP,
					    &size_params);
	if (err)
		goto out;

	devlink_resource_size_params_init(&size_params,
					  mv88e6xxx_num_macs(chip) / 4,
					  mv88e6xxx_num_macs(chip) / 4,
					  1, DEVLINK_RESOURCE_UNIT_ENTRY);

	err = dsa_devlink_resource_register(ds, "ATU_bin_0",
					    mv88e6xxx_num_macs(chip) / 4,
					    MV88E6XXX_RESOURCE_ID_ATU_BIN_0,
					    MV88E6XXX_RESOURCE_ID_ATU,
					    &size_params);
	if (err)
		goto out;

	err = dsa_devlink_resource_register(ds, "ATU_bin_1",
					    mv88e6xxx_num_macs(chip) / 4,
					    MV88E6XXX_RESOURCE_ID_ATU_BIN_1,
					    MV88E6XXX_RESOURCE_ID_ATU,
					    &size_params);
	if (err)
		goto out;

	err = dsa_devlink_resource_register(ds, "ATU_bin_2",
					    mv88e6xxx_num_macs(chip) / 4,
					    MV88E6XXX_RESOURCE_ID_ATU_BIN_2,
					    MV88E6XXX_RESOURCE_ID_ATU,
					    &size_params);
	if (err)
		goto out;

	err = dsa_devlink_resource_register(ds, "ATU_bin_3",
					    mv88e6xxx_num_macs(chip) / 4,
					    MV88E6XXX_RESOURCE_ID_ATU_BIN_3,
					    MV88E6XXX_RESOURCE_ID_ATU,
					    &size_params);
	if (err)
		goto out;

	dsa_devlink_resource_occ_get_register(ds,
					      MV88E6XXX_RESOURCE_ID_ATU,
					      mv88e6xxx_devlink_atu_get,
					      chip);

	dsa_devlink_resource_occ_get_register(ds,
					      MV88E6XXX_RESOURCE_ID_ATU_BIN_0,
					      mv88e6xxx_devlink_atu_bin_0_get,
					      chip);

	dsa_devlink_resource_occ_get_register(ds,
					      MV88E6XXX_RESOURCE_ID_ATU_BIN_1,
					      mv88e6xxx_devlink_atu_bin_1_get,
					      chip);

	dsa_devlink_resource_occ_get_register(ds,
					      MV88E6XXX_RESOURCE_ID_ATU_BIN_2,
					      mv88e6xxx_devlink_atu_bin_2_get,
					      chip);

	dsa_devlink_resource_occ_get_register(ds,
					      MV88E6XXX_RESOURCE_ID_ATU_BIN_3,
					      mv88e6xxx_devlink_atu_bin_3_get,
					      chip);

	return 0;

out:
	dsa_devlink_resources_unregister(ds);
	return err;
}
