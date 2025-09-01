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

const struct rz_sysc_init_data rzv2h_sys_init_data = {
	.soc_id_init_data = &rzv2h_sys_soc_id_init_data,
	.max_register = 0x170c,
};
