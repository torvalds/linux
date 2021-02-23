// SPDX-License-Identifier: GPL-2.0-or-later
#include <net/dsa.h>

#include "chip.h"
#include "devlink.h"
#include "global1.h"
#include "global2.h"
#include "port.h"

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

static int mv88e6xxx_region_global_snapshot(struct devlink *dl,
					    const struct devlink_region_ops *ops,
					    struct netlink_ext_ack *extack,
					    u8 **data)
{
	struct mv88e6xxx_region_priv *region_priv = ops->priv;
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);
	struct mv88e6xxx_chip *chip = ds->priv;
	u16 *registers;
	int i, err;

	registers = kmalloc_array(32, sizeof(u16), GFP_KERNEL);
	if (!registers)
		return -ENOMEM;

	mv88e6xxx_reg_lock(chip);
	for (i = 0; i < 32; i++) {
		switch (region_priv->id) {
		case MV88E6XXX_REGION_GLOBAL1:
			err = mv88e6xxx_g1_read(chip, i, &registers[i]);
			break;
		case MV88E6XXX_REGION_GLOBAL2:
			err = mv88e6xxx_g2_read(chip, i, &registers[i]);
			break;
		default:
			err = -EOPNOTSUPP;
		}

		if (err) {
			kfree(registers);
			goto out;
		}
	}
	*data = (u8 *)registers;
out:
	mv88e6xxx_reg_unlock(chip);

	return err;
}

/* The ATU entry varies between mv88e6xxx chipset generations. Define
 * a generic format which covers all the current and hopefully future
 * mv88e6xxx generations
 */

struct mv88e6xxx_devlink_atu_entry {
	/* The FID is scattered over multiple registers. */
	u16 fid;
	u16 atu_op;
	u16 atu_data;
	u16 atu_01;
	u16 atu_23;
	u16 atu_45;
};

static int mv88e6xxx_region_atu_snapshot_fid(struct mv88e6xxx_chip *chip,
					     int fid,
					     struct mv88e6xxx_devlink_atu_entry *table,
					     int *count)
{
	u16 atu_op, atu_data, atu_01, atu_23, atu_45;
	struct mv88e6xxx_atu_entry addr;
	int err;

	addr.state = 0;
	eth_broadcast_addr(addr.mac);

	do {
		err = mv88e6xxx_g1_atu_getnext(chip, fid, &addr);
		if (err)
			return err;

		if (!addr.state)
			break;

		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_OP, &atu_op);
		if (err)
			return err;

		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_DATA, &atu_data);
		if (err)
			return err;

		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_MAC01, &atu_01);
		if (err)
			return err;

		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_MAC23, &atu_23);
		if (err)
			return err;

		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_MAC45, &atu_45);
		if (err)
			return err;

		table[*count].fid = fid;
		table[*count].atu_op = atu_op;
		table[*count].atu_data = atu_data;
		table[*count].atu_01 = atu_01;
		table[*count].atu_23 = atu_23;
		table[*count].atu_45 = atu_45;
		(*count)++;
	} while (!is_broadcast_ether_addr(addr.mac));

	return 0;
}

static int mv88e6xxx_region_atu_snapshot(struct devlink *dl,
					 const struct devlink_region_ops *ops,
					 struct netlink_ext_ack *extack,
					 u8 **data)
{
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);
	DECLARE_BITMAP(fid_bitmap, MV88E6XXX_N_FID);
	struct mv88e6xxx_devlink_atu_entry *table;
	struct mv88e6xxx_chip *chip = ds->priv;
	int fid = -1, count, err;

	table = kmalloc_array(mv88e6xxx_num_databases(chip),
			      sizeof(struct mv88e6xxx_devlink_atu_entry),
			      GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	memset(table, 0, mv88e6xxx_num_databases(chip) *
	       sizeof(struct mv88e6xxx_devlink_atu_entry));

	count = 0;

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_fid_map(chip, fid_bitmap);
	if (err) {
		kfree(table);
		goto out;
	}

	while (1) {
		fid = find_next_bit(fid_bitmap, MV88E6XXX_N_FID, fid + 1);
		if (fid == MV88E6XXX_N_FID)
			break;

		err =  mv88e6xxx_region_atu_snapshot_fid(chip, fid, table,
							 &count);
		if (err) {
			kfree(table);
			goto out;
		}
	}
	*data = (u8 *)table;
out:
	mv88e6xxx_reg_unlock(chip);

	return err;
}

/**
 * struct mv88e6xxx_devlink_vtu_entry - Devlink VTU entry
 * @fid:   Global1/2:   FID and VLAN policy.
 * @sid:   Global1/3:   SID, unknown filters and learning.
 * @op:    Global1/5:   FID (old chipsets).
 * @vid:   Global1/6:   VID, valid, and page.
 * @data:  Global1/7-9: Membership data and priority override.
 * @resvd: Reserved. Also happens to align the size to 16B.
 *
 * The VTU entry format varies between chipset generations, the
 * descriptions above represent the superset of all possible
 * information, not all fields are valid on all devices. Since this is
 * a low-level debug interface, copy all data verbatim and defer
 * parsing to the consumer.
 */
struct mv88e6xxx_devlink_vtu_entry {
	u16 fid;
	u16 sid;
	u16 op;
	u16 vid;
	u16 data[3];
	u16 resvd;
};

static int mv88e6xxx_region_vtu_snapshot(struct devlink *dl,
					 const struct devlink_region_ops *ops,
					 struct netlink_ext_ack *extack,
					 u8 **data)
{
	struct mv88e6xxx_devlink_vtu_entry *table, *entry;
	struct dsa_switch *ds = dsa_devlink_to_ds(dl);
	struct mv88e6xxx_chip *chip = ds->priv;
	struct mv88e6xxx_vtu_entry vlan;
	int err;

	table = kcalloc(mv88e6xxx_max_vid(chip) + 1,
			sizeof(struct mv88e6xxx_devlink_vtu_entry),
			GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	entry = table;
	vlan.vid = mv88e6xxx_max_vid(chip);
	vlan.valid = false;

	mv88e6xxx_reg_lock(chip);

	do {
		err = mv88e6xxx_g1_vtu_getnext(chip, &vlan);
		if (err)
			break;

		if (!vlan.valid)
			break;

		err = err ? : mv88e6xxx_g1_read(chip, MV88E6352_G1_VTU_FID,
						&entry->fid);
		err = err ? : mv88e6xxx_g1_read(chip, MV88E6352_G1_VTU_SID,
						&entry->sid);
		err = err ? : mv88e6xxx_g1_read(chip, MV88E6XXX_G1_VTU_OP,
						&entry->op);
		err = err ? : mv88e6xxx_g1_read(chip, MV88E6XXX_G1_VTU_VID,
						&entry->vid);
		err = err ? : mv88e6xxx_g1_read(chip, MV88E6XXX_G1_VTU_DATA1,
						&entry->data[0]);
		err = err ? : mv88e6xxx_g1_read(chip, MV88E6XXX_G1_VTU_DATA2,
						&entry->data[1]);
		err = err ? : mv88e6xxx_g1_read(chip, MV88E6XXX_G1_VTU_DATA3,
						&entry->data[2]);
		if (err)
			break;

		entry++;
	} while (vlan.vid < mv88e6xxx_max_vid(chip));

	mv88e6xxx_reg_unlock(chip);

	if (err) {
		kfree(table);
		return err;
	}

	*data = (u8 *)table;
	return 0;
}

static int mv88e6xxx_region_port_snapshot(struct devlink_port *devlink_port,
					  const struct devlink_port_region_ops *ops,
					  struct netlink_ext_ack *extack,
					  u8 **data)
{
	struct dsa_switch *ds = dsa_devlink_port_to_ds(devlink_port);
	int port = dsa_devlink_port_to_port(devlink_port);
	struct mv88e6xxx_chip *chip = ds->priv;
	u16 *registers;
	int i, err;

	registers = kmalloc_array(32, sizeof(u16), GFP_KERNEL);
	if (!registers)
		return -ENOMEM;

	mv88e6xxx_reg_lock(chip);
	for (i = 0; i < 32; i++) {
		err = mv88e6xxx_port_read(chip, port, i, &registers[i]);
		if (err) {
			kfree(registers);
			goto out;
		}
	}
	*data = (u8 *)registers;
out:
	mv88e6xxx_reg_unlock(chip);

	return err;
}

static struct mv88e6xxx_region_priv mv88e6xxx_region_global1_priv = {
	.id = MV88E6XXX_REGION_GLOBAL1,
};

static struct devlink_region_ops mv88e6xxx_region_global1_ops = {
	.name = "global1",
	.snapshot = mv88e6xxx_region_global_snapshot,
	.destructor = kfree,
	.priv = &mv88e6xxx_region_global1_priv,
};

static struct mv88e6xxx_region_priv mv88e6xxx_region_global2_priv = {
	.id = MV88E6XXX_REGION_GLOBAL2,
};

static struct devlink_region_ops mv88e6xxx_region_global2_ops = {
	.name = "global2",
	.snapshot = mv88e6xxx_region_global_snapshot,
	.destructor = kfree,
	.priv = &mv88e6xxx_region_global2_priv,
};

static struct devlink_region_ops mv88e6xxx_region_atu_ops = {
	.name = "atu",
	.snapshot = mv88e6xxx_region_atu_snapshot,
	.destructor = kfree,
};

static struct devlink_region_ops mv88e6xxx_region_vtu_ops = {
	.name = "vtu",
	.snapshot = mv88e6xxx_region_vtu_snapshot,
	.destructor = kfree,
};

static const struct devlink_port_region_ops mv88e6xxx_region_port_ops = {
	.name = "port",
	.snapshot = mv88e6xxx_region_port_snapshot,
	.destructor = kfree,
};

struct mv88e6xxx_region {
	struct devlink_region_ops *ops;
	u64 size;
};

static struct mv88e6xxx_region mv88e6xxx_regions[] = {
	[MV88E6XXX_REGION_GLOBAL1] = {
		.ops = &mv88e6xxx_region_global1_ops,
		.size = 32 * sizeof(u16)
	},
	[MV88E6XXX_REGION_GLOBAL2] = {
		.ops = &mv88e6xxx_region_global2_ops,
		.size = 32 * sizeof(u16) },
	[MV88E6XXX_REGION_ATU] = {
		.ops = &mv88e6xxx_region_atu_ops
	  /* calculated at runtime */
	},
	[MV88E6XXX_REGION_VTU] = {
		.ops = &mv88e6xxx_region_vtu_ops
	  /* calculated at runtime */
	},
};

static void
mv88e6xxx_teardown_devlink_regions_global(struct mv88e6xxx_chip *chip)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mv88e6xxx_regions); i++)
		dsa_devlink_region_destroy(chip->regions[i]);
}

static void
mv88e6xxx_teardown_devlink_regions_port(struct mv88e6xxx_chip *chip,
					int port)
{
	dsa_devlink_region_destroy(chip->ports[port].region);
}

static int mv88e6xxx_setup_devlink_regions_port(struct dsa_switch *ds,
						struct mv88e6xxx_chip *chip,
						int port)
{
	struct devlink_region *region;

	region = dsa_devlink_port_region_create(ds,
						port,
						&mv88e6xxx_region_port_ops, 1,
						32 * sizeof(u16));
	if (IS_ERR(region))
		return PTR_ERR(region);

	chip->ports[port].region = region;

	return 0;
}

static void
mv88e6xxx_teardown_devlink_regions_ports(struct mv88e6xxx_chip *chip)
{
	int port;

	for (port = 0; port < mv88e6xxx_num_ports(chip); port++)
		mv88e6xxx_teardown_devlink_regions_port(chip, port);
}

static int mv88e6xxx_setup_devlink_regions_ports(struct dsa_switch *ds,
						 struct mv88e6xxx_chip *chip)
{
	int port;
	int err;

	for (port = 0; port < mv88e6xxx_num_ports(chip); port++) {
		err = mv88e6xxx_setup_devlink_regions_port(ds, chip, port);
		if (err)
			goto out;
	}

	return 0;

out:
	while (port-- > 0)
		mv88e6xxx_teardown_devlink_regions_port(chip, port);

	return err;
}

static int mv88e6xxx_setup_devlink_regions_global(struct dsa_switch *ds,
						  struct mv88e6xxx_chip *chip)
{
	struct devlink_region_ops *ops;
	struct devlink_region *region;
	u64 size;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(mv88e6xxx_regions); i++) {
		ops = mv88e6xxx_regions[i].ops;
		size = mv88e6xxx_regions[i].size;

		switch (i) {
		case MV88E6XXX_REGION_ATU:
			size = mv88e6xxx_num_databases(chip) *
				sizeof(struct mv88e6xxx_devlink_atu_entry);
			break;
		case MV88E6XXX_REGION_VTU:
			size = mv88e6xxx_max_vid(chip) *
				sizeof(struct mv88e6xxx_devlink_vtu_entry);
			break;
		}

		region = dsa_devlink_region_create(ds, ops, 1, size);
		if (IS_ERR(region))
			goto out;
		chip->regions[i] = region;
	}
	return 0;

out:
	for (j = 0; j < i; j++)
		dsa_devlink_region_destroy(chip->regions[j]);

	return PTR_ERR(region);
}

int mv88e6xxx_setup_devlink_regions(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	err = mv88e6xxx_setup_devlink_regions_global(ds, chip);
	if (err)
		return err;

	err = mv88e6xxx_setup_devlink_regions_ports(ds, chip);
	if (err)
		mv88e6xxx_teardown_devlink_regions_global(chip);

	return err;
}

void mv88e6xxx_teardown_devlink_regions(struct dsa_switch *ds)
{
	struct mv88e6xxx_chip *chip = ds->priv;

	mv88e6xxx_teardown_devlink_regions_ports(chip);
	mv88e6xxx_teardown_devlink_regions_global(chip);
}

int mv88e6xxx_devlink_info_get(struct dsa_switch *ds,
			       struct devlink_info_req *req,
			       struct netlink_ext_ack *extack)
{
	struct mv88e6xxx_chip *chip = ds->priv;
	int err;

	err = devlink_info_driver_name_put(req, "mv88e6xxx");
	if (err)
		return err;

	return devlink_info_version_fixed_put(req,
					      DEVLINK_INFO_VERSION_GENERIC_ASIC_ID,
					      chip->info->name);
}
