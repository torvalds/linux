// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/G3E System controller (SYS) driver
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
#define SYS_LSI_PRR_CA55_DIS		BIT(8)
#define SYS_LSI_PRR_NPU_DIS		BIT(1)

#define SYS_LSI_OTPTSU1TRMVAL0		0x330
#define SYS_LSI_OTPTSU1TRMVAL1		0x334
#define SYS_SPI_STAADDCS0		0x900
#define SYS_SPI_ENDADDCS0		0x904
#define SYS_SPI_STAADDCS1		0x908
#define SYS_SPI_ENDADDCS1		0x90c
#define SYS_VSP_CLK			0xe00
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
#define SYS_ADC_CFG			0x1600

static void rzg3e_sys_print_id(struct device *dev,
				void __iomem *sysc_base,
				struct soc_device_attribute *soc_dev_attr)
{
	bool is_quad_core, npu_enabled;
	u32 prr_val, mode_val;

	prr_val = readl(sysc_base + SYS_LSI_PRR);
	mode_val = readl(sysc_base + SYS_LSI_MODE);

	/* Check CPU and NPU configuration */
	is_quad_core = !(prr_val & SYS_LSI_PRR_CA55_DIS);
	npu_enabled = !(prr_val & SYS_LSI_PRR_NPU_DIS);

	dev_info(dev, "Detected Renesas %s Core %s %s Rev %s%s\n",
		 is_quad_core ? "Quad" : "Dual", soc_dev_attr->family,
		 soc_dev_attr->soc_id, soc_dev_attr->revision,
		 npu_enabled ? " with Ethos-U55" : "");

	/* Check CA55 PLL configuration */
	if (FIELD_GET(SYS_LSI_MODE_STAT_BOOTPLLCA55, mode_val) != SYS_LSI_MODE_CA55_1_7GHZ)
		dev_warn(dev, "CA55 PLL is not set to 1.7GHz\n");
}

static const struct rz_sysc_soc_id_init_data rzg3e_sys_soc_id_init_data __initconst = {
	.family = "RZ/G3E",
	.id = 0x8679447,
	.devid_offset = 0x304,
	.revision_mask = GENMASK(31, 28),
	.specific_id_mask = GENMASK(27, 0),
	.print_id = rzg3e_sys_print_id,
};

static bool rzg3e_regmap_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SYS_LSI_OTPTSU1TRMVAL0:
	case SYS_LSI_OTPTSU1TRMVAL1:
	case SYS_SPI_STAADDCS0:
	case SYS_SPI_ENDADDCS0:
	case SYS_SPI_STAADDCS1:
	case SYS_SPI_ENDADDCS1:
	case SYS_VSP_CLK:
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
	case SYS_ADC_CFG:
		return true;
	default:
		return false;
	}
}

static bool rzg3e_regmap_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SYS_SPI_STAADDCS0:
	case SYS_SPI_ENDADDCS0:
	case SYS_SPI_STAADDCS1:
	case SYS_SPI_ENDADDCS1:
	case SYS_VSP_CLK:
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
	case SYS_ADC_CFG:
		return true;
	default:
		return false;
	}
}

const struct rz_sysc_init_data rzg3e_sys_init_data = {
	.soc_id_init_data = &rzg3e_sys_soc_id_init_data,
	.readable_reg = rzg3e_regmap_readable_reg,
	.writeable_reg = rzg3e_regmap_writeable_reg,
	.max_register = 0x170c,
};
