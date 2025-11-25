// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/V2H System controller (SYS) driver
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>

#include "rz-sysc.h"

/* Register Offsets */
#define SYS_LSI_MODE		0x300
/*
 * BOOTPLLCA[1:0]
 *	    [0,0] => 1.1GHZ
 *	    [0,1] => 1.5GHZ
 *	    [1,0] => 1.6GHZ
 *	    [1,1] => 1.7GHZ
 */
#define SYS_LSI_MODE_STAT_BOOTPLLCA55	GENMASK(12, 11)
#define SYS_LSI_MODE_CA55_1_7GHZ	0x3

#define SYS_LSI_PRR			0x308
#define SYS_LSI_PRR_GPU_DIS		BIT(0)
#define SYS_LSI_PRR_ISP_DIS		BIT(4)

#define SYS_LSI_OTPTSU0TRMVAL0		0x320
#define SYS_LSI_OTPTSU0TRMVAL1		0x324
#define SYS_LSI_OTPTSU1TRMVAL0		0x330
#define SYS_LSI_OTPTSU1TRMVAL1		0x334
#define SYS_GBETH0_CFG			0xf00
#define SYS_GBETH1_CFG			0xf04
#define SYS_PCIE_INTX_CH0		0x1000
#define SYS_PCIE_MSI1_CH0		0x1004
#define SYS_PCIE_MSI2_CH0		0x1008
#define SYS_PCIE_MSI3_CH0		0x100c
#define SYS_PCIE_MSI4_CH0		0x1010
#define SYS_PCIE_MSI5_CH0		0x1014
#define SYS_PCIE_PME_CH0		0x1018
#define SYS_PCIE_ACK_CH0		0x101c
#define SYS_PCIE_MISC_CH0		0x1020
#define SYS_PCIE_MODE_CH0		0x1024
#define SYS_PCIE_INTX_CH1		0x1030
#define SYS_PCIE_MSI1_CH1		0x1034
#define SYS_PCIE_MSI2_CH1		0x1038
#define SYS_PCIE_MSI3_CH1		0x103c
#define SYS_PCIE_MSI4_CH1		0x1040
#define SYS_PCIE_MSI5_CH1		0x1044
#define SYS_PCIE_PME_CH1		0x1048
#define SYS_PCIE_ACK_CH1		0x104c
#define SYS_PCIE_MISC_CH1		0x1050
#define SYS_PCIE_MODE_CH1		0x1054
#define SYS_PCIE_MODE			0x1060
#define SYS_ADC_CFG			0x1600

static void rzv2h_sys_print_id(struct device *dev,
				void __iomem *sysc_base,
				struct soc_device_attribute *soc_dev_attr)
{
	bool gpu_enabled, isp_enabled;
	u32 prr_val, mode_val;

	prr_val = readl(sysc_base + SYS_LSI_PRR);
	mode_val = readl(sysc_base + SYS_LSI_MODE);

	/* Check GPU and ISP configuration */
	gpu_enabled = !(prr_val & SYS_LSI_PRR_GPU_DIS);
	isp_enabled = !(prr_val & SYS_LSI_PRR_ISP_DIS);

	dev_info(dev, "Detected Renesas %s %s Rev %s%s%s\n",
		 soc_dev_attr->family, soc_dev_attr->soc_id, soc_dev_attr->revision,
		 gpu_enabled ? " with GE3D (Mali-G31)" : "",
		 isp_enabled ? " with ISP (Mali-C55)" : "");

	/* Check CA55 PLL configuration */
	if (FIELD_GET(SYS_LSI_MODE_STAT_BOOTPLLCA55, mode_val) != SYS_LSI_MODE_CA55_1_7GHZ)
		dev_warn(dev, "CA55 PLL is not set to 1.7GHz\n");
}

static const struct rz_sysc_soc_id_init_data rzv2h_sys_soc_id_init_data __initconst = {
	.family = "RZ/V2H",
	.id = 0x847a447,
	.devid_offset = 0x304,
	.revision_mask = GENMASK(31, 28),
	.specific_id_mask = GENMASK(27, 0),
	.print_id = rzv2h_sys_print_id,
};

static bool rzv2h_regmap_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SYS_LSI_OTPTSU0TRMVAL0:
	case SYS_LSI_OTPTSU0TRMVAL1:
	case SYS_LSI_OTPTSU1TRMVAL0:
	case SYS_LSI_OTPTSU1TRMVAL1:
	case SYS_GBETH0_CFG:
	case SYS_GBETH1_CFG:
	case SYS_PCIE_INTX_CH0:
	case SYS_PCIE_MSI1_CH0:
	case SYS_PCIE_MSI2_CH0:
	case SYS_PCIE_MSI3_CH0:
	case SYS_PCIE_MSI4_CH0:
	case SYS_PCIE_MSI5_CH0:
	case SYS_PCIE_PME_CH0:
	case SYS_PCIE_ACK_CH0:
	case SYS_PCIE_MISC_CH0:
	case SYS_PCIE_MODE_CH0:
	case SYS_PCIE_INTX_CH1:
	case SYS_PCIE_MSI1_CH1:
	case SYS_PCIE_MSI2_CH1:
	case SYS_PCIE_MSI3_CH1:
	case SYS_PCIE_MSI4_CH1:
	case SYS_PCIE_MSI5_CH1:
	case SYS_PCIE_PME_CH1:
	case SYS_PCIE_ACK_CH1:
	case SYS_PCIE_MISC_CH1:
	case SYS_PCIE_MODE_CH1:
	case SYS_PCIE_MODE:
	case SYS_ADC_CFG:
		return true;
	default:
		return false;
	}
}

static bool rzv2h_regmap_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SYS_GBETH0_CFG:
	case SYS_GBETH1_CFG:
	case SYS_PCIE_INTX_CH0:
	case SYS_PCIE_MSI1_CH0:
	case SYS_PCIE_MSI2_CH0:
	case SYS_PCIE_MSI3_CH0:
	case SYS_PCIE_MSI4_CH0:
	case SYS_PCIE_MSI5_CH0:
	case SYS_PCIE_PME_CH0:
	case SYS_PCIE_ACK_CH0:
	case SYS_PCIE_MISC_CH0:
	case SYS_PCIE_MODE_CH0:
	case SYS_PCIE_INTX_CH1:
	case SYS_PCIE_MSI1_CH1:
	case SYS_PCIE_MSI2_CH1:
	case SYS_PCIE_MSI3_CH1:
	case SYS_PCIE_MSI4_CH1:
	case SYS_PCIE_MSI5_CH1:
	case SYS_PCIE_PME_CH1:
	case SYS_PCIE_ACK_CH1:
	case SYS_PCIE_MISC_CH1:
	case SYS_PCIE_MODE_CH1:
	case SYS_PCIE_MODE:
	case SYS_ADC_CFG:
		return true;
	default:
		return false;
	}
}

const struct rz_sysc_init_data rzv2h_sys_init_data = {
	.soc_id_init_data = &rzv2h_sys_soc_id_init_data,
	.readable_reg = rzv2h_regmap_readable_reg,
	.writeable_reg = rzv2h_regmap_writeable_reg,
	.max_register = 0x170c,
};
