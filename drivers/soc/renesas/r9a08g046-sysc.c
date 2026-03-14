// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G3L System controller (SYSC) driver
 *
 * Copyright (C) 2026 Renesas Electronics Corp.
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/init.h>

#include "rz-sysc.h"

#define SYS_XSPI_MAP_STAADD_CS0		0x348
#define SYS_XSPI_MAP_ENDADD_CS0		0x34c
#define SYS_XSPI_MAP_STAADD_CS1		0x350
#define SYS_XSPI_MAP_ENDADD_CS1		0x354
#define SYS_GETH0_CFG			0x380
#define SYS_GETH1_CFG			0x390
#define SYS_PCIE_CFG			0x3a0
#define SYS_PCIE_MON			0x3a4
#define SYS_PCIE_PHY			0x3b4
#define SYS_I2C0_CFG			0x400
#define SYS_I2C1_CFG			0x410
#define SYS_I2C2_CFG			0x420
#define SYS_I2C3_CFG			0x430
#define SYS_I3C_CFG			0x440
#define SYS_PWRRDY_N			0xd70
#define SYS_IPCONT_SEL_CLONECH		0xe2c

static bool rzg3l_regmap_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SYS_XSPI_MAP_STAADD_CS0:
	case SYS_XSPI_MAP_ENDADD_CS0:
	case SYS_XSPI_MAP_STAADD_CS1:
	case SYS_XSPI_MAP_ENDADD_CS1:
	case SYS_GETH0_CFG:
	case SYS_GETH1_CFG:
	case SYS_PCIE_CFG:
	case SYS_PCIE_MON:
	case SYS_PCIE_PHY:
	case SYS_I2C0_CFG:
	case SYS_I2C1_CFG:
	case SYS_I2C2_CFG:
	case SYS_I2C3_CFG:
	case SYS_I3C_CFG:
	case SYS_PWRRDY_N:
	case SYS_IPCONT_SEL_CLONECH:
		return true;
	default:
		return false;
	}
}

static bool rzg3l_regmap_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SYS_XSPI_MAP_STAADD_CS0:
	case SYS_XSPI_MAP_ENDADD_CS0:
	case SYS_XSPI_MAP_STAADD_CS1:
	case SYS_XSPI_MAP_ENDADD_CS1:
	case SYS_PCIE_CFG:
	case SYS_PCIE_PHY:
	case SYS_I2C0_CFG:
	case SYS_I2C1_CFG:
	case SYS_I2C2_CFG:
	case SYS_I2C3_CFG:
	case SYS_I3C_CFG:
	case SYS_PWRRDY_N:
	case SYS_IPCONT_SEL_CLONECH:
		return true;
	default:
		return false;
	}
}

static const struct rz_sysc_soc_id_init_data rzg3l_sysc_soc_id_init_data __initconst = {
	.family = "RZ/G3L",
	.id = 0x87d9447,
	.devid_offset = 0xa04,
	.revision_mask = GENMASK(31, 28),
	.specific_id_mask = GENMASK(27, 0),
};

const struct rz_sysc_init_data rzg3l_sysc_init_data __initconst = {
	.soc_id_init_data = &rzg3l_sysc_soc_id_init_data,
	.readable_reg = rzg3l_regmap_readable_reg,
	.writeable_reg = rzg3l_regmap_writeable_reg,
	.max_register = 0xe2c,
};
