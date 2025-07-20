// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/V2N System controller (SYS) driver
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
#define SYS_LSI_MODE_SEC_EN	BIT(16)
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

#define SYS_RZV2N_FEATURE_G31		BIT(0)
#define SYS_RZV2N_FEATURE_C55		BIT(1)
#define SYS_RZV2N_FEATURE_SEC		BIT(2)

static void rzv2n_sys_print_id(struct device *dev,
			       void __iomem *sysc_base,
			       struct soc_device_attribute *soc_dev_attr)
{
	u32 prr_val, mode_val;
	u8 feature_flags;

	prr_val = readl(sysc_base + SYS_LSI_PRR);
	mode_val = readl(sysc_base + SYS_LSI_MODE);

	/* Check GPU, ISP and Cryptographic configuration */
	feature_flags = !(prr_val & SYS_LSI_PRR_GPU_DIS) ? SYS_RZV2N_FEATURE_G31 : 0;
	feature_flags |= !(prr_val & SYS_LSI_PRR_ISP_DIS) ? SYS_RZV2N_FEATURE_C55 : 0;
	feature_flags |= (mode_val & SYS_LSI_MODE_SEC_EN) ? SYS_RZV2N_FEATURE_SEC : 0;

	dev_info(dev, "Detected Renesas %s %sn%d Rev %s%s%s%s%s\n", soc_dev_attr->family,
		 soc_dev_attr->soc_id, 41 + feature_flags, soc_dev_attr->revision,
		 feature_flags ?  " with" : "",
		 feature_flags & SYS_RZV2N_FEATURE_G31 ? " GE3D (Mali-G31)" : "",
		 feature_flags & SYS_RZV2N_FEATURE_SEC ? " Cryptographic engine" : "",
		 feature_flags & SYS_RZV2N_FEATURE_C55 ? " ISP (Mali-C55)" : "");

	/* Check CA55 PLL configuration */
	if (FIELD_GET(SYS_LSI_MODE_STAT_BOOTPLLCA55, mode_val) != SYS_LSI_MODE_CA55_1_7GHZ)
		dev_warn(dev, "CA55 PLL is not set to 1.7GHz\n");
}

static const struct rz_sysc_soc_id_init_data rzv2n_sys_soc_id_init_data __initconst = {
	.family = "RZ/V2N",
	.id = 0x867d447,
	.devid_offset = 0x304,
	.revision_mask = GENMASK(31, 28),
	.specific_id_mask = GENMASK(27, 0),
	.print_id = rzv2n_sys_print_id,
};

const struct rz_sysc_init_data rzv2n_sys_init_data = {
	.soc_id_init_data = &rzv2n_sys_soc_id_init_data,
};
