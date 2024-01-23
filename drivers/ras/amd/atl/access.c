// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD Address Translation Library
 *
 * access.c : DF Indirect Access functions
 *
 * Copyright (c) 2023, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Yazen Ghannam <Yazen.Ghannam@amd.com>
 */

#include "internal.h"

/* Protect the PCI config register pairs used for DF indirect access. */
static DEFINE_MUTEX(df_indirect_mutex);

/*
 * Data Fabric Indirect Access uses FICAA/FICAD.
 *
 * Fabric Indirect Configuration Access Address (FICAA): constructed based
 * on the device's Instance Id and the PCI function and register offset of
 * the desired register.
 *
 * Fabric Indirect Configuration Access Data (FICAD): there are FICAD
 * low and high registers but so far only the low register is needed.
 *
 * Use Instance Id 0xFF to indicate a broadcast read.
 */
#define DF_BROADCAST		0xFF

#define DF_FICAA_INST_EN	BIT(0)
#define DF_FICAA_REG_NUM	GENMASK(10, 1)
#define DF_FICAA_FUNC_NUM	GENMASK(13, 11)
#define DF_FICAA_INST_ID	GENMASK(23, 16)

#define DF_FICAA_REG_NUM_LEGACY	GENMASK(10, 2)

static int __df_indirect_read(u16 node, u8 func, u16 reg, u8 instance_id, u32 *lo)
{
	u32 ficaa_addr = 0x8C, ficad_addr = 0xB8;
	struct pci_dev *F4;
	int err = -ENODEV;
	u32 ficaa = 0;

	if (node >= amd_nb_num())
		goto out;

	F4 = node_to_amd_nb(node)->link;
	if (!F4)
		goto out;

	/* Enable instance-specific access. */
	if (instance_id != DF_BROADCAST) {
		ficaa |= FIELD_PREP(DF_FICAA_INST_EN, 1);
		ficaa |= FIELD_PREP(DF_FICAA_INST_ID, instance_id);
	}

	/*
	 * The two least-significant bits are masked when inputing the
	 * register offset to FICAA.
	 */
	reg >>= 2;

	if (df_cfg.flags.legacy_ficaa) {
		ficaa_addr = 0x5C;
		ficad_addr = 0x98;

		ficaa |= FIELD_PREP(DF_FICAA_REG_NUM_LEGACY, reg);
	} else {
		ficaa |= FIELD_PREP(DF_FICAA_REG_NUM, reg);
	}

	ficaa |= FIELD_PREP(DF_FICAA_FUNC_NUM, func);

	mutex_lock(&df_indirect_mutex);

	err = pci_write_config_dword(F4, ficaa_addr, ficaa);
	if (err) {
		pr_warn("Error writing DF Indirect FICAA, FICAA=0x%x\n", ficaa);
		goto out_unlock;
	}

	err = pci_read_config_dword(F4, ficad_addr, lo);
	if (err)
		pr_warn("Error reading DF Indirect FICAD LO, FICAA=0x%x.\n", ficaa);

	pr_debug("node=%u inst=0x%x func=0x%x reg=0x%x val=0x%x",
		 node, instance_id, func, reg << 2, *lo);

out_unlock:
	mutex_unlock(&df_indirect_mutex);

out:
	return err;
}

int df_indirect_read_instance(u16 node, u8 func, u16 reg, u8 instance_id, u32 *lo)
{
	return __df_indirect_read(node, func, reg, instance_id, lo);
}

int df_indirect_read_broadcast(u16 node, u8 func, u16 reg, u32 *lo)
{
	return __df_indirect_read(node, func, reg, DF_BROADCAST, lo);
}
