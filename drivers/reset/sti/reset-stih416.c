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

#include <dt-bindings/reset/stih416-resets.h>

#include "reset-syscfg.h"

/*
 * STiH416 Peripheral powerdown definitions.
 */
static const char stih416_front[] = "st,stih416-front-syscfg";
static const char stih416_rear[] = "st,stih416-rear-syscfg";
static const char stih416_sbc[] = "st,stih416-sbc-syscfg";
static const char stih416_lpm[] = "st,stih416-lpm-syscfg";
static const char stih416_cpu[] = "st,stih416-cpu-syscfg";

#define STIH416_PDN_FRONT(_bit) \
	_SYSCFG_RST_CH(stih416_front, SYSCFG_1500, _bit, SYSSTAT_1578, _bit)

#define STIH416_PDN_REAR(_cntl, _stat) \
	_SYSCFG_RST_CH(stih416_rear, SYSCFG_2525, _cntl, SYSSTAT_2583, _stat)

#define SYSCFG_1500	0x7d0 /* Powerdown request EMI/NAND/Keyscan */
#define SYSSTAT_1578	0x908 /* Powerdown status EMI/NAND/Keyscan */

#define SYSCFG_2525	0x834 /* Powerdown request USB/SATA/PCIe */
#define SYSSTAT_2583	0x91c /* Powerdown status USB/SATA/PCIe */

#define SYSCFG_2552	0x8A0 /* Reset Generator control 0 */
#define SYSCFG_1539	0x86c /* Softreset Ethernet 0 */
#define SYSCFG_510	0x7f8 /* Softreset Ethernet 1 */
#define LPM_SYSCFG_1	0x4 /* Softreset IRB */
#define SYSCFG_2553	0x8a4 /* Softreset SATA0/1, PCIE0/1 */
#define SYSCFG_7563	0x8cc /* MPE softresets 0 */
#define SYSCFG_7564	0x8d0 /* MPE softresets 1 */

#define STIH416_SRST_CPU(_reg, _bit) \
	 _SYSCFG_RST_CH_NO_ACK(stih416_cpu, _reg, _bit)

#define STIH416_SRST_FRONT(_reg, _bit) \
	 _SYSCFG_RST_CH_NO_ACK(stih416_front, _reg, _bit)

#define STIH416_SRST_REAR(_reg, _bit) \
	 _SYSCFG_RST_CH_NO_ACK(stih416_rear, _reg, _bit)

#define STIH416_SRST_LPM(_reg, _bit) \
	 _SYSCFG_RST_CH_NO_ACK(stih416_lpm, _reg, _bit)

#define STIH416_SRST_SBC(_reg, _bit) \
	 _SYSCFG_RST_CH_NO_ACK(stih416_sbc, _reg, _bit)

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

static const struct syscfg_reset_channel_data stih416_softresets[] = {
	[STIH416_ETH0_SOFTRESET] = STIH416_SRST_FRONT(SYSCFG_1539, 0),
	[STIH416_ETH1_SOFTRESET] = STIH416_SRST_SBC(SYSCFG_510, 0),
	[STIH416_IRB_SOFTRESET]	 = STIH416_SRST_LPM(LPM_SYSCFG_1, 6),
	[STIH416_USB0_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2552, 9),
	[STIH416_USB1_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2552, 10),
	[STIH416_USB2_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2552, 11),
	[STIH416_USB3_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2552, 28),
	[STIH416_SATA0_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2553, 7),
	[STIH416_SATA1_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2553, 3),
	[STIH416_PCIE0_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2553, 15),
	[STIH416_PCIE1_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2553, 2),
	[STIH416_AUD_DAC_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2553, 14),
	[STIH416_HDTVOUT_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2552, 5),
	[STIH416_VTAC_M_RX_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2552, 25),
	[STIH416_VTAC_A_RX_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2552, 26),
	[STIH416_SYNC_HD_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2553, 5),
	[STIH416_SYNC_SD_SOFTRESET] = STIH416_SRST_REAR(SYSCFG_2553, 6),
	[STIH416_BLITTER_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7563, 10),
	[STIH416_GPU_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7563, 11),
	[STIH416_VTAC_M_TX_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7563, 18),
	[STIH416_VTAC_A_TX_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7563, 19),
	[STIH416_VTG_AUX_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7563, 21),
	[STIH416_JPEG_DEC_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7563, 23),
	[STIH416_HVA_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7564, 2),
	[STIH416_COMPO_M_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7564, 3),
	[STIH416_COMPO_A_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7564, 4),
	[STIH416_VP8_DEC_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7564, 10),
	[STIH416_VTG_MAIN_SOFTRESET] = STIH416_SRST_CPU(SYSCFG_7564, 16),
	[STIH416_KEYSCAN_SOFTRESET] = STIH416_SRST_LPM(LPM_SYSCFG_1, 8),
};

static struct syscfg_reset_controller_data stih416_powerdown_controller = {
	.wait_for_ack	= true,
	.nr_channels	= ARRAY_SIZE(stih416_powerdowns),
	.channels	= stih416_powerdowns,
};

static struct syscfg_reset_controller_data stih416_softreset_controller = {
	.wait_for_ack = false,
	.active_low = true,
	.nr_channels = ARRAY_SIZE(stih416_softresets),
	.channels = stih416_softresets,
};

static const struct of_device_id stih416_reset_match[] = {
	{ .compatible = "st,stih416-powerdown",
	  .data = &stih416_powerdown_controller, },
	{ .compatible = "st,stih416-softreset",
	  .data = &stih416_softreset_controller, },
	{},
};

static struct platform_driver stih416_reset_driver = {
	.probe = syscfg_reset_probe,
	.driver = {
		.name = "reset-stih416",
		.of_match_table = stih416_reset_match,
	},
};

static int __init stih416_reset_init(void)
{
	return platform_driver_register(&stih416_reset_driver);
}
arch_initcall(stih416_reset_init);
