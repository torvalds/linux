/*
 * Copyright (C) 2013 STMicroelectronics (R&D) Limited
 * Author: Stephen Gallimore <stephen.gallimore@st.com>
 * Author: Srinivas Kandagatla <srinivas.kandagatla@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include <dt-bindings/reset-controller/stih416-resets.h>

#include "reset-syscfg.h"

/*
 * STiH416 Peripheral powerdown definitions.
 */
static const char stih416_front[] = "st,stih416-front-syscfg";
static const char stih416_rear[] = "st,stih416-rear-syscfg";
static const char stih416_sbc[] = "st,stih416-sbc-syscfg";
static const char stih416_lpm[] = "st,stih416-lpm-syscfg";

#define STIH416_PDN_FRONT(_bit) \
	_SYSCFG_RST_CH(stih416_front, SYSCFG_1500, _bit, SYSSTAT_1578, _bit)

#define STIH416_PDN_REAR(_cntl, _stat) \
	_SYSCFG_RST_CH(stih416_rear, SYSCFG_2525, _cntl, SYSSTAT_2583, _stat)

#define SYSCFG_1500	0x7d0 /* Powerdown request EMI/NAND/Keyscan */
#define SYSSTAT_1578	0x908 /* Powerdown status EMI/NAND/Keyscan */

#define SYSCFG_2525	0x834 /* Powerdown request USB/SATA/PCIe */
#define SYSSTAT_2583	0x91c /* Powerdown status USB/SATA/PCIe */

static const struct syscfg_reset_channel_data stih416_powerdowns[] = {
	[STIH416_EMISS_POWERDOWN]	= STIH416_PDN_FRONT(0),
	[STIH416_NAND_POWERDOWN]	= STIH416_PDN_FRONT(1),
	[STIH416_KEYSCAN_POWERDOWN]	= STIH416_PDN_FRONT(2),
	[STIH416_USB0_POWERDOWN]	= STIH416_PDN_REAR(0, 0),
	[STIH416_USB1_POWERDOWN]	= STIH416_PDN_REAR(1, 1),
	[STIH416_USB2_POWERDOWN]	= STIH416_PDN_REAR(2, 2),
	[STIH416_USB3_POWERDOWN]	= STIH416_PDN_REAR(6, 5),
	[STIH416_SATA0_POWERDOWN]	= STIH416_PDN_REAR(3, 3),
	[STIH416_SATA1_POWERDOWN]	= STIH416_PDN_REAR(4, 4),
	[STIH416_PCIE0_POWERDOWN]	= STIH416_PDN_REAR(7, 9),
	[STIH416_PCIE1_POWERDOWN]	= STIH416_PDN_REAR(5, 8),
};

static struct syscfg_reset_controller_data stih416_powerdown_controller = {
	.wait_for_ack	= true,
	.nr_channels	= ARRAY_SIZE(stih416_powerdowns),
	.channels	= stih416_powerdowns,
};

static struct of_device_id stih416_reset_match[] = {
	{ .compatible = "st,stih416-powerdown",
	  .data = &stih416_powerdown_controller, },
	{},
};

static struct platform_driver stih416_reset_driver = {
	.probe = syscfg_reset_probe,
	.driver = {
		.name = "reset-stih416",
		.owner = THIS_MODULE,
		.of_match_table = stih416_reset_match,
	},
};

static int __init stih416_reset_init(void)
{
	return platform_driver_register(&stih416_reset_driver);
}
arch_initcall(stih416_reset_init);
