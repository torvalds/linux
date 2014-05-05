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

#include <dt-bindings/reset-controller/stih415-resets.h>

#include "reset-syscfg.h"

/*
 * STiH415 Peripheral powerdown definitions.
 */
static const char stih415_front[] = "st,stih415-front-syscfg";
static const char stih415_rear[] = "st,stih415-rear-syscfg";
static const char stih415_sbc[] = "st,stih415-sbc-syscfg";
static const char stih415_lpm[] = "st,stih415-lpm-syscfg";

#define STIH415_PDN_FRONT(_bit) \
	_SYSCFG_RST_CH(stih415_front, SYSCFG_114, _bit, SYSSTAT_187, _bit)

#define STIH415_PDN_REAR(_cntl, _stat) \
	_SYSCFG_RST_CH(stih415_rear, SYSCFG_336, _cntl, SYSSTAT_384, _stat)

#define STIH415_SRST_REAR(_reg, _bit) \
	_SYSCFG_RST_CH_NO_ACK(stih415_rear, _reg, _bit)

#define STIH415_SRST_SBC(_reg, _bit) \
	_SYSCFG_RST_CH_NO_ACK(stih415_sbc, _reg, _bit)

#define STIH415_SRST_FRONT(_reg, _bit) \
	_SYSCFG_RST_CH_NO_ACK(stih415_front, _reg, _bit)

#define STIH415_SRST_LPM(_reg, _bit) \
	_SYSCFG_RST_CH_NO_ACK(stih415_lpm, _reg, _bit)

#define SYSCFG_114	0x38 /* Powerdown request EMI/NAND/Keyscan */
#define SYSSTAT_187	0x15c /* Powerdown status EMI/NAND/Keyscan */

#define SYSCFG_336	0x90 /* Powerdown request USB/SATA/PCIe */
#define SYSSTAT_384	0x150 /* Powerdown status USB/SATA/PCIe */

#define SYSCFG_376	0x130 /* Reset generator 0 control 0 */
#define SYSCFG_166	0x108 /* Softreset Ethernet 0 */
#define SYSCFG_31	0x7c /* Softreset Ethernet 1 */
#define LPM_SYSCFG_1	0x4 /* Softreset IRB */

static const struct syscfg_reset_channel_data stih415_powerdowns[] = {
	[STIH415_EMISS_POWERDOWN]	= STIH415_PDN_FRONT(0),
	[STIH415_NAND_POWERDOWN]	= STIH415_PDN_FRONT(1),
	[STIH415_KEYSCAN_POWERDOWN]	= STIH415_PDN_FRONT(2),
	[STIH415_USB0_POWERDOWN]	= STIH415_PDN_REAR(0, 0),
	[STIH415_USB1_POWERDOWN]	= STIH415_PDN_REAR(1, 1),
	[STIH415_USB2_POWERDOWN]	= STIH415_PDN_REAR(2, 2),
	[STIH415_SATA0_POWERDOWN]	= STIH415_PDN_REAR(3, 3),
	[STIH415_SATA1_POWERDOWN]	= STIH415_PDN_REAR(4, 4),
	[STIH415_PCIE_POWERDOWN]	= STIH415_PDN_REAR(5, 8),
};

static const struct syscfg_reset_channel_data stih415_softresets[] = {
	[STIH415_ETH0_SOFTRESET] = STIH415_SRST_FRONT(SYSCFG_166, 0),
	[STIH415_ETH1_SOFTRESET] = STIH415_SRST_SBC(SYSCFG_31, 0),
	[STIH415_IRB_SOFTRESET]	 = STIH415_SRST_LPM(LPM_SYSCFG_1, 6),
	[STIH415_USB0_SOFTRESET] = STIH415_SRST_REAR(SYSCFG_376, 9),
	[STIH415_USB1_SOFTRESET] = STIH415_SRST_REAR(SYSCFG_376, 10),
	[STIH415_USB2_SOFTRESET] = STIH415_SRST_REAR(SYSCFG_376, 11),
};

static struct syscfg_reset_controller_data stih415_powerdown_controller = {
	.wait_for_ack = true,
	.nr_channels = ARRAY_SIZE(stih415_powerdowns),
	.channels = stih415_powerdowns,
};

static struct syscfg_reset_controller_data stih415_softreset_controller = {
	.wait_for_ack = false,
	.active_low = true,
	.nr_channels = ARRAY_SIZE(stih415_softresets),
	.channels = stih415_softresets,
};

static struct of_device_id stih415_reset_match[] = {
	{ .compatible = "st,stih415-powerdown",
	  .data = &stih415_powerdown_controller, },
	{ .compatible = "st,stih415-softreset",
	  .data = &stih415_softreset_controller, },
	{},
};

static struct platform_driver stih415_reset_driver = {
	.probe = syscfg_reset_probe,
	.driver = {
		.name = "reset-stih415",
		.owner = THIS_MODULE,
		.of_match_table = stih415_reset_match,
	},
};

static int __init stih415_reset_init(void)
{
	return platform_driver_register(&stih415_reset_driver);
}
arch_initcall(stih415_reset_init);
