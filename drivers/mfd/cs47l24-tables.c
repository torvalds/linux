/*
 * Data tables for CS47L24 codec
 *
 * Copyright 2015 Cirrus Logic, Inc.
 *
 * Author: Richard Fitzgerald <rf@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/registers.h>
#include <linux/device.h>

#include "arizona.h"

#define CS47L24_NUM_ISR 5

static const struct reg_sequence cs47l24_reva_patch[] = {
	{ 0x80,  0x3 },
	{ 0x27C, 0x0010 },
	{ 0x221, 0x0070 },
	{ 0x80,  0x0 },
};

int cs47l24_patch(struct arizona *arizona)
{
	return regmap_register_patch(arizona->regmap,
				     cs47l24_reva_patch,
				     ARRAY_SIZE(cs47l24_reva_patch));
}
EXPORT_SYMBOL_GPL(cs47l24_patch);

static const struct regmap_irq cs47l24_irqs[ARIZONA_NUM_IRQ] = {
	[ARIZONA_IRQ_GP2] = { .reg_offset = 0, .mask = ARIZONA_GP2_EINT1 },
	[ARIZONA_IRQ_GP1] = { .reg_offset = 0, .mask = ARIZONA_GP1_EINT1 },

	[ARIZONA_IRQ_DSP3_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP3_RAM_RDY_EINT1
	},
	[ARIZONA_IRQ_DSP2_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP2_RAM_RDY_EINT1
	},
	[ARIZONA_IRQ_DSP_IRQ8] = {
		.reg_offset = 1, .mask = ARIZONA_DSP_IRQ8_EINT1
	},
	[ARIZONA_IRQ_DSP_IRQ7] = {
		.reg_offset = 1, .mask = ARIZONA_DSP_IRQ7_EINT1
	},
	[ARIZONA_IRQ_DSP_IRQ6] = {
		.reg_offset = 1, .mask = ARIZONA_DSP_IRQ6_EINT1
	},
	[ARIZONA_IRQ_DSP_IRQ5] = {
		.reg_offset = 1, .mask = ARIZONA_DSP_IRQ5_EINT1
	},
	[ARIZONA_IRQ_DSP_IRQ4] = {
		.reg_offset = 1, .mask = ARIZONA_DSP_IRQ4_EINT1
	},
	[ARIZONA_IRQ_DSP_IRQ3] = {
		.reg_offset = 1, .mask = ARIZONA_DSP_IRQ3_EINT1
	},
	[ARIZONA_IRQ_DSP_IRQ2] = {
		.reg_offset = 1, .mask = ARIZONA_DSP_IRQ2_EINT1
	},
	[ARIZONA_IRQ_DSP_IRQ1] = {
		.reg_offset = 1, .mask = ARIZONA_DSP_IRQ1_EINT1
	},

	[ARIZONA_IRQ_SPK_OVERHEAT_WARN] = {
		.reg_offset = 2, .mask = ARIZONA_SPK_OVERHEAT_WARN_EINT1
	},
	[ARIZONA_IRQ_SPK_OVERHEAT] = {
		.reg_offset = 2, .mask = ARIZONA_SPK_OVERHEAT_EINT1
	},
	[ARIZONA_IRQ_WSEQ_DONE] = {
		.reg_offset = 2, .mask = ARIZONA_WSEQ_DONE_EINT1
	},
	[ARIZONA_IRQ_DRC2_SIG_DET] = {
		.reg_offset = 2, .mask = ARIZONA_DRC2_SIG_DET_EINT1
	},
	[ARIZONA_IRQ_DRC1_SIG_DET] = {
		.reg_offset = 2, .mask = ARIZONA_DRC1_SIG_DET_EINT1
	},
	[ARIZONA_IRQ_ASRC2_LOCK] = {
		.reg_offset = 2, .mask = ARIZONA_ASRC2_LOCK_EINT1
	},
	[ARIZONA_IRQ_ASRC1_LOCK] = {
		.reg_offset = 2, .mask = ARIZONA_ASRC1_LOCK_EINT1
	},
	[ARIZONA_IRQ_UNDERCLOCKED] = {
		.reg_offset = 2, .mask = ARIZONA_UNDERCLOCKED_EINT1
	},
	[ARIZONA_IRQ_OVERCLOCKED] = {
		.reg_offset = 2, .mask = ARIZONA_OVERCLOCKED_EINT1
	},
	[ARIZONA_IRQ_FLL2_LOCK] = {
		.reg_offset = 2, .mask = ARIZONA_FLL2_LOCK_EINT1
	},
	[ARIZONA_IRQ_FLL1_LOCK] = {
		.reg_offset = 2, .mask = ARIZONA_FLL1_LOCK_EINT1
	},
	[ARIZONA_IRQ_CLKGEN_ERR] = {
		.reg_offset = 2, .mask = ARIZONA_CLKGEN_ERR_EINT1
	},
	[ARIZONA_IRQ_CLKGEN_ERR_ASYNC] = {
		.reg_offset = 2, .mask = ARIZONA_CLKGEN_ERR_ASYNC_EINT1
	},

	[ARIZONA_IRQ_CTRLIF_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_V2_CTRLIF_ERR_EINT1
	},
	[ARIZONA_IRQ_MIXER_DROPPED_SAMPLES] = {
		.reg_offset = 3, .mask = ARIZONA_V2_MIXER_DROPPED_SAMPLE_EINT1
	},
	[ARIZONA_IRQ_ASYNC_CLK_ENA_LOW] = {
		.reg_offset = 3, .mask = ARIZONA_V2_ASYNC_CLK_ENA_LOW_EINT1
	},
	[ARIZONA_IRQ_SYSCLK_ENA_LOW] = {
		.reg_offset = 3, .mask = ARIZONA_V2_SYSCLK_ENA_LOW_EINT1
	},
	[ARIZONA_IRQ_ISRC1_CFG_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_V2_ISRC1_CFG_ERR_EINT1
	},
	[ARIZONA_IRQ_ISRC2_CFG_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_V2_ISRC2_CFG_ERR_EINT1
	},
	[ARIZONA_IRQ_ISRC3_CFG_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_V2_ISRC3_CFG_ERR_EINT1
	},
	[ARIZONA_IRQ_HP1R_DONE] = {
		.reg_offset = 3, .mask = ARIZONA_HP1R_DONE_EINT1
	},
	[ARIZONA_IRQ_HP1L_DONE] = {
		.reg_offset = 3, .mask = ARIZONA_HP1L_DONE_EINT1
	},

	[ARIZONA_IRQ_BOOT_DONE] = {
		.reg_offset = 4, .mask = ARIZONA_BOOT_DONE_EINT1
	},
	[ARIZONA_IRQ_ASRC_CFG_ERR] = {
		.reg_offset = 4, .mask = ARIZONA_V2_ASRC_CFG_ERR_EINT1
	},
	[ARIZONA_IRQ_FLL2_CLOCK_OK] = {
		.reg_offset = 4, .mask = ARIZONA_FLL2_CLOCK_OK_EINT1
	},
	[ARIZONA_IRQ_FLL1_CLOCK_OK] = {
		.reg_offset = 4, .mask = ARIZONA_FLL1_CLOCK_OK_EINT1
	},

	[ARIZONA_IRQ_DSP_SHARED_WR_COLL] = {
		.reg_offset = 5, .mask = ARIZONA_DSP_SHARED_WR_COLL_EINT1
	},
	[ARIZONA_IRQ_SPK_SHUTDOWN] = {
		.reg_offset = 5, .mask = ARIZONA_SPK_SHUTDOWN_EINT1
	},
	[ARIZONA_IRQ_SPK1R_SHORT] = {
		.reg_offset = 5, .mask = ARIZONA_SPK1R_SHORT_EINT1
	},
	[ARIZONA_IRQ_SPK1L_SHORT] = {
		.reg_offset = 5, .mask = ARIZONA_SPK1L_SHORT_EINT1
	},
	[ARIZONA_IRQ_HP1R_SC_POS] = {
		.reg_offset = 5, .mask = ARIZONA_HP1R_SC_POS_EINT1
	},
	[ARIZONA_IRQ_HP1L_SC_POS] = {
		.reg_offset = 5, .mask = ARIZONA_HP1L_SC_POS_EINT1
	},
};

const struct regmap_irq_chip cs47l24_irq = {
	.name = "cs47l24 IRQ",
	.status_base = ARIZONA_INTERRUPT_STATUS_1,
	.mask_base = ARIZONA_INTERRUPT_STATUS_1_MASK,
	.ack_base = ARIZONA_INTERRUPT_STATUS_1,
	.num_regs = 6,
	.irqs = cs47l24_irqs,
	.num_irqs = ARRAY_SIZE(cs47l24_irqs),
};
EXPORT_SYMBOL_GPL(cs47l24_irq);

static const struct reg_default cs47l24_reg_default[] = {
	{ 0x00000008, 0x0019 },    /* R8     - Ctrl IF SPI CFG 1 */
	{ 0x00000020, 0x0000 },    /* R32    - Tone Generator 1 */
	{ 0x00000021, 0x1000 },    /* R33    - Tone Generator 2 */
	{ 0x00000022, 0x0000 },    /* R34    - Tone Generator 3 */
	{ 0x00000023, 0x1000 },    /* R35    - Tone Generator 4 */
	{ 0x00000024, 0x0000 },    /* R36    - Tone Generator 5 */
	{ 0x00000030, 0x0000 },    /* R48    - PWM Drive 1 */
	{ 0x00000031, 0x0100 },    /* R49    - PWM Drive 2 */
	{ 0x00000032, 0x0100 },    /* R50    - PWM Drive 3 */
	{ 0x00000041, 0x0000 },    /* R65    - Sequence control */
	{ 0x00000061, 0x01FF },    /* R97    - Sample Rate Sequence Select 1 */
	{ 0x00000062, 0x01FF },    /* R98    - Sample Rate Sequence Select 2 */
	{ 0x00000063, 0x01FF },    /* R99    - Sample Rate Sequence Select 3 */
	{ 0x00000064, 0x01FF },    /* R100   - Sample Rate Sequence Select 4 */
	{ 0x00000070, 0x0000 },    /* R112   - Comfort Noise Generator */
	{ 0x00000090, 0x0000 },    /* R144   - Haptics Control 1 */
	{ 0x00000091, 0x7FFF },    /* R145   - Haptics Control 2 */
	{ 0x00000092, 0x0000 },    /* R146   - Haptics phase 1 intensity */
	{ 0x00000093, 0x0000 },    /* R147   - Haptics phase 1 duration */
	{ 0x00000094, 0x0000 },    /* R148   - Haptics phase 2 intensity */
	{ 0x00000095, 0x0000 },    /* R149   - Haptics phase 2 duration */
	{ 0x00000096, 0x0000 },    /* R150   - Haptics phase 3 intensity */
	{ 0x00000097, 0x0000 },    /* R151   - Haptics phase 3 duration */
	{ 0x00000100, 0x0002 },    /* R256   - Clock 32k 1 */
	{ 0x00000101, 0x0504 },    /* R257   - System Clock 1 */
	{ 0x00000102, 0x0011 },    /* R258   - Sample rate 1 */
	{ 0x00000103, 0x0011 },    /* R259   - Sample rate 2 */
	{ 0x00000104, 0x0011 },    /* R260   - Sample rate 3 */
	{ 0x00000112, 0x0305 },    /* R274   - Async clock 1 */
	{ 0x00000113, 0x0011 },    /* R275   - Async sample rate 1 */
	{ 0x00000114, 0x0011 },    /* R276   - Async sample rate 2 */
	{ 0x00000149, 0x0000 },    /* R329   - Output system clock */
	{ 0x0000014A, 0x0000 },    /* R330   - Output async clock */
	{ 0x00000152, 0x0000 },    /* R338   - Rate Estimator 1 */
	{ 0x00000153, 0x0000 },    /* R339   - Rate Estimator 2 */
	{ 0x00000154, 0x0000 },    /* R340   - Rate Estimator 3 */
	{ 0x00000155, 0x0000 },    /* R341   - Rate Estimator 4 */
	{ 0x00000156, 0x0000 },    /* R342   - Rate Estimator 5 */
	{ 0x00000171, 0x0002 },    /* R369   - FLL1 Control 1 */
	{ 0x00000172, 0x0008 },    /* R370   - FLL1 Control 2 */
	{ 0x00000173, 0x0018 },    /* R371   - FLL1 Control 3 */
	{ 0x00000174, 0x007D },    /* R372   - FLL1 Control 4 */
	{ 0x00000175, 0x0006 },    /* R373   - FLL1 Control 5 */
	{ 0x00000176, 0x0000 },    /* R374   - FLL1 Control 6 */
	{ 0x00000179, 0x0000 },    /* R376   - FLL1 Control 7 */
	{ 0x00000181, 0x0000 },    /* R385   - FLL1 Synchroniser 1 */
	{ 0x00000182, 0x0000 },    /* R386   - FLL1 Synchroniser 2 */
	{ 0x00000183, 0x0000 },    /* R387   - FLL1 Synchroniser 3 */
	{ 0x00000184, 0x0000 },    /* R388   - FLL1 Synchroniser 4 */
	{ 0x00000185, 0x0000 },    /* R389   - FLL1 Synchroniser 5 */
	{ 0x00000186, 0x0000 },    /* R390   - FLL1 Synchroniser 6 */
	{ 0x00000187, 0x0001 },    /* R390   - FLL1 Synchroniser 7 */
	{ 0x00000189, 0x0000 },    /* R393   - FLL1 Spread Spectrum */
	{ 0x0000018A, 0x000C },    /* R394   - FLL1 GPIO Clock */
	{ 0x00000191, 0x0002 },    /* R401   - FLL2 Control 1 */
	{ 0x00000192, 0x0008 },    /* R402   - FLL2 Control 2 */
	{ 0x00000193, 0x0018 },    /* R403   - FLL2 Control 3 */
	{ 0x00000194, 0x007D },    /* R404   - FLL2 Control 4 */
	{ 0x00000195, 0x000C },    /* R405   - FLL2 Control 5 */
	{ 0x00000196, 0x0000 },    /* R406   - FLL2 Control 6 */
	{ 0x00000199, 0x0000 },    /* R408   - FLL2 Control 7 */
	{ 0x000001A1, 0x0000 },    /* R417   - FLL2 Synchroniser 1 */
	{ 0x000001A2, 0x0000 },    /* R418   - FLL2 Synchroniser 2 */
	{ 0x000001A3, 0x0000 },    /* R419   - FLL2 Synchroniser 3 */
	{ 0x000001A4, 0x0000 },    /* R420   - FLL2 Synchroniser 4 */
	{ 0x000001A5, 0x0000 },    /* R421   - FLL2 Synchroniser 5 */
	{ 0x000001A6, 0x0000 },    /* R422   - FLL2 Synchroniser 6 */
	{ 0x000001A7, 0x0001 },    /* R422   - FLL2 Synchroniser 7 */
	{ 0x000001A9, 0x0000 },    /* R425   - FLL2 Spread Spectrum */
	{ 0x000001AA, 0x000C },    /* R426   - FLL2 GPIO Clock */
	{ 0x00000218, 0x00E6 },    /* R536   - Mic Bias Ctrl 1 */
	{ 0x00000219, 0x00E6 },    /* R537   - Mic Bias Ctrl 2 */
	{ 0x00000300, 0x0000 },    /* R768   - Input Enables */
	{ 0x00000308, 0x0000 },    /* R776   - Input Rate */
	{ 0x00000309, 0x0022 },    /* R777   - Input Volume Ramp */
	{ 0x0000030C, 0x0002 },    /* R780   - HPF Control */
	{ 0x00000310, 0x2000 },    /* R784   - IN1L Control */
	{ 0x00000311, 0x0180 },    /* R785   - ADC Digital Volume 1L */
	{ 0x00000312, 0x0000 },    /* R786   - DMIC1L Control */
	{ 0x00000314, 0x0000 },    /* R788   - IN1R Control */
	{ 0x00000315, 0x0180 },    /* R789   - ADC Digital Volume 1R */
	{ 0x00000316, 0x0000 },    /* R790   - DMIC1R Control */
	{ 0x00000318, 0x2000 },    /* R792   - IN2L Control */
	{ 0x00000319, 0x0180 },    /* R793   - ADC Digital Volume 2L */
	{ 0x0000031A, 0x0000 },    /* R794   - DMIC2L Control */
	{ 0x0000031C, 0x0000 },    /* R796   - IN2R Control */
	{ 0x0000031D, 0x0180 },    /* R797   - ADC Digital Volume 2R */
	{ 0x0000031E, 0x0000 },    /* R798   - DMIC2R Control */
	{ 0x00000400, 0x0000 },    /* R1024  - Output Enables 1 */
	{ 0x00000408, 0x0000 },    /* R1032  - Output Rate 1 */
	{ 0x00000409, 0x0022 },    /* R1033  - Output Volume Ramp */
	{ 0x00000410, 0x0080 },    /* R1040  - Output Path Config 1L */
	{ 0x00000411, 0x0180 },    /* R1041  - DAC Digital Volume 1L */
	{ 0x00000412, 0x0081 },    /* R1042  - DAC Volume Limit 1L */
	{ 0x00000413, 0x0001 },    /* R1043  - Noise Gate Select 1L */
	{ 0x00000415, 0x0180 },    /* R1045  - DAC Digital Volume 1R */
	{ 0x00000416, 0x0081 },    /* R1046  - DAC Volume Limit 1R */
	{ 0x00000417, 0x0002 },    /* R1047  - Noise Gate Select 1R */
	{ 0x00000429, 0x0180 },    /* R1065  - DAC Digital Volume 4L */
	{ 0x0000042A, 0x0081 },    /* R1066  - Out Volume 4L */
	{ 0x0000042B, 0x0040 },    /* R1067  - Noise Gate Select 4L */
	{ 0x00000450, 0x0000 },    /* R1104  - DAC AEC Control 1 */
	{ 0x00000458, 0x0000 },    /* R1112  - Noise Gate Control */
	{ 0x000004A0, 0x3480 },    /* R1184  - HP1 Short Circuit Ctrl */
	{ 0x00000500, 0x000C },    /* R1280  - AIF1 BCLK Ctrl */
	{ 0x00000501, 0x0008 },    /* R1281  - AIF1 Tx Pin Ctrl */
	{ 0x00000502, 0x0000 },    /* R1282  - AIF1 Rx Pin Ctrl */
	{ 0x00000503, 0x0000 },    /* R1283  - AIF1 Rate Ctrl */
	{ 0x00000504, 0x0000 },    /* R1284  - AIF1 Format */
	{ 0x00000506, 0x0040 },    /* R1286  - AIF1 Rx BCLK Rate */
	{ 0x00000507, 0x1818 },    /* R1287  - AIF1 Frame Ctrl 1 */
	{ 0x00000508, 0x1818 },    /* R1288  - AIF1 Frame Ctrl 2 */
	{ 0x00000509, 0x0000 },    /* R1289  - AIF1 Frame Ctrl 3 */
	{ 0x0000050A, 0x0001 },    /* R1290  - AIF1 Frame Ctrl 4 */
	{ 0x0000050B, 0x0002 },    /* R1291  - AIF1 Frame Ctrl 5 */
	{ 0x0000050C, 0x0003 },    /* R1292  - AIF1 Frame Ctrl 6 */
	{ 0x0000050D, 0x0004 },    /* R1293  - AIF1 Frame Ctrl 7 */
	{ 0x0000050E, 0x0005 },    /* R1294  - AIF1 Frame Ctrl 8 */
	{ 0x0000050F, 0x0006 },    /* R1295  - AIF1 Frame Ctrl 9 */
	{ 0x00000510, 0x0007 },    /* R1296  - AIF1 Frame Ctrl 10 */
	{ 0x00000511, 0x0000 },    /* R1297  - AIF1 Frame Ctrl 11 */
	{ 0x00000512, 0x0001 },    /* R1298  - AIF1 Frame Ctrl 12 */
	{ 0x00000513, 0x0002 },    /* R1299  - AIF1 Frame Ctrl 13 */
	{ 0x00000514, 0x0003 },    /* R1300  - AIF1 Frame Ctrl 14 */
	{ 0x00000515, 0x0004 },    /* R1301  - AIF1 Frame Ctrl 15 */
	{ 0x00000516, 0x0005 },    /* R1302  - AIF1 Frame Ctrl 16 */
	{ 0x00000517, 0x0006 },    /* R1303  - AIF1 Frame Ctrl 17 */
	{ 0x00000518, 0x0007 },    /* R1304  - AIF1 Frame Ctrl 18 */
	{ 0x00000519, 0x0000 },    /* R1305  - AIF1 Tx Enables */
	{ 0x0000051A, 0x0000 },    /* R1306  - AIF1 Rx Enables */
	{ 0x00000540, 0x000C },    /* R1344  - AIF2 BCLK Ctrl */
	{ 0x00000541, 0x0008 },    /* R1345  - AIF2 Tx Pin Ctrl */
	{ 0x00000542, 0x0000 },    /* R1346  - AIF2 Rx Pin Ctrl */
	{ 0x00000543, 0x0000 },    /* R1347  - AIF2 Rate Ctrl */
	{ 0x00000544, 0x0000 },    /* R1348  - AIF2 Format */
	{ 0x00000546, 0x0040 },    /* R1350  - AIF2 Rx BCLK Rate */
	{ 0x00000547, 0x1818 },    /* R1351  - AIF2 Frame Ctrl 1 */
	{ 0x00000548, 0x1818 },    /* R1352  - AIF2 Frame Ctrl 2 */
	{ 0x00000549, 0x0000 },    /* R1353  - AIF2 Frame Ctrl 3 */
	{ 0x0000054A, 0x0001 },    /* R1354  - AIF2 Frame Ctrl 4 */
	{ 0x0000054B, 0x0002 },    /* R1355  - AIF2 Frame Ctrl 5 */
	{ 0x0000054C, 0x0003 },    /* R1356  - AIF2 Frame Ctrl 6 */
	{ 0x0000054D, 0x0004 },    /* R1357  - AIF2 Frame Ctrl 7 */
	{ 0x0000054E, 0x0005 },    /* R1358  - AIF2 Frame Ctrl 8 */
	{ 0x00000551, 0x0000 },    /* R1361  - AIF2 Frame Ctrl 11 */
	{ 0x00000552, 0x0001 },    /* R1362  - AIF2 Frame Ctrl 12 */
	{ 0x00000553, 0x0002 },    /* R1363  - AIF2 Frame Ctrl 13 */
	{ 0x00000554, 0x0003 },    /* R1364  - AIF2 Frame Ctrl 14 */
	{ 0x00000555, 0x0004 },    /* R1365  - AIF2 Frame Ctrl 15 */
	{ 0x00000556, 0x0005 },    /* R1366  - AIF2 Frame Ctrl 16 */
	{ 0x00000559, 0x0000 },    /* R1369  - AIF2 Tx Enables */
	{ 0x0000055A, 0x0000 },    /* R1370  - AIF2 Rx Enables */
	{ 0x00000580, 0x000C },    /* R1408  - AIF3 BCLK Ctrl */
	{ 0x00000581, 0x0008 },    /* R1409  - AIF3 Tx Pin Ctrl */
	{ 0x00000582, 0x0000 },    /* R1410  - AIF3 Rx Pin Ctrl */
	{ 0x00000583, 0x0000 },    /* R1411  - AIF3 Rate Ctrl */
	{ 0x00000584, 0x0000 },    /* R1412  - AIF3 Format */
	{ 0x00000586, 0x0040 },    /* R1414  - AIF3 Rx BCLK Rate */
	{ 0x00000587, 0x1818 },    /* R1415  - AIF3 Frame Ctrl 1 */
	{ 0x00000588, 0x1818 },    /* R1416  - AIF3 Frame Ctrl 2 */
	{ 0x00000589, 0x0000 },    /* R1417  - AIF3 Frame Ctrl 3 */
	{ 0x0000058A, 0x0001 },    /* R1418  - AIF3 Frame Ctrl 4 */
	{ 0x00000591, 0x0000 },    /* R1425  - AIF3 Frame Ctrl 11 */
	{ 0x00000592, 0x0001 },    /* R1426  - AIF3 Frame Ctrl 12 */
	{ 0x00000599, 0x0000 },    /* R1433  - AIF3 Tx Enables */
	{ 0x0000059A, 0x0000 },    /* R1434  - AIF3 Rx Enables */
	{ 0x00000640, 0x0000 },    /* R1600  - PWM1MIX Input 1 Source */
	{ 0x00000641, 0x0080 },    /* R1601  - PWM1MIX Input 1 Volume */
	{ 0x00000642, 0x0000 },    /* R1602  - PWM1MIX Input 2 Source */
	{ 0x00000643, 0x0080 },    /* R1603  - PWM1MIX Input 2 Volume */
	{ 0x00000644, 0x0000 },    /* R1604  - PWM1MIX Input 3 Source */
	{ 0x00000645, 0x0080 },    /* R1605  - PWM1MIX Input 3 Volume */
	{ 0x00000646, 0x0000 },    /* R1606  - PWM1MIX Input 4 Source */
	{ 0x00000647, 0x0080 },    /* R1607  - PWM1MIX Input 4 Volume */
	{ 0x00000648, 0x0000 },    /* R1608  - PWM2MIX Input 1 Source */
	{ 0x00000649, 0x0080 },    /* R1609  - PWM2MIX Input 1 Volume */
	{ 0x0000064A, 0x0000 },    /* R1610  - PWM2MIX Input 2 Source */
	{ 0x0000064B, 0x0080 },    /* R1611  - PWM2MIX Input 2 Volume */
	{ 0x0000064C, 0x0000 },    /* R1612  - PWM2MIX Input 3 Source */
	{ 0x0000064D, 0x0080 },    /* R1613  - PWM2MIX Input 3 Volume */
	{ 0x0000064E, 0x0000 },    /* R1614  - PWM2MIX Input 4 Source */
	{ 0x0000064F, 0x0080 },    /* R1615  - PWM2MIX Input 4 Volume */
	{ 0x00000680, 0x0000 },    /* R1664  - OUT1LMIX Input 1 Source */
	{ 0x00000681, 0x0080 },    /* R1665  - OUT1LMIX Input 1 Volume */
	{ 0x00000682, 0x0000 },    /* R1666  - OUT1LMIX Input 2 Source */
	{ 0x00000683, 0x0080 },    /* R1667  - OUT1LMIX Input 2 Volume */
	{ 0x00000684, 0x0000 },    /* R1668  - OUT1LMIX Input 3 Source */
	{ 0x00000685, 0x0080 },    /* R1669  - OUT1LMIX Input 3 Volume */
	{ 0x00000686, 0x0000 },    /* R1670  - OUT1LMIX Input 4 Source */
	{ 0x00000687, 0x0080 },    /* R1671  - OUT1LMIX Input 4 Volume */
	{ 0x00000688, 0x0000 },    /* R1672  - OUT1RMIX Input 1 Source */
	{ 0x00000689, 0x0080 },    /* R1673  - OUT1RMIX Input 1 Volume */
	{ 0x0000068A, 0x0000 },    /* R1674  - OUT1RMIX Input 2 Source */
	{ 0x0000068B, 0x0080 },    /* R1675  - OUT1RMIX Input 2 Volume */
	{ 0x0000068C, 0x0000 },    /* R1676  - OUT1RMIX Input 3 Source */
	{ 0x0000068D, 0x0080 },    /* R1677  - OUT1RMIX Input 3 Volume */
	{ 0x0000068E, 0x0000 },    /* R1678  - OUT1RMIX Input 4 Source */
	{ 0x0000068F, 0x0080 },    /* R1679  - OUT1RMIX Input 4 Volume */
	{ 0x000006B0, 0x0000 },    /* R1712  - OUT4LMIX Input 1 Source */
	{ 0x000006B1, 0x0080 },    /* R1713  - OUT4LMIX Input 1 Volume */
	{ 0x000006B2, 0x0000 },    /* R1714  - OUT4LMIX Input 2 Source */
	{ 0x000006B3, 0x0080 },    /* R1715  - OUT4LMIX Input 2 Volume */
	{ 0x000006B4, 0x0000 },    /* R1716  - OUT4LMIX Input 3 Source */
	{ 0x000006B5, 0x0080 },    /* R1717  - OUT4LMIX Input 3 Volume */
	{ 0x000006B6, 0x0000 },    /* R1718  - OUT4LMIX Input 4 Source */
	{ 0x000006B7, 0x0080 },    /* R1719  - OUT4LMIX Input 4 Volume */
	{ 0x00000700, 0x0000 },    /* R1792  - AIF1TX1MIX Input 1 Source */
	{ 0x00000701, 0x0080 },    /* R1793  - AIF1TX1MIX Input 1 Volume */
	{ 0x00000702, 0x0000 },    /* R1794  - AIF1TX1MIX Input 2 Source */
	{ 0x00000703, 0x0080 },    /* R1795  - AIF1TX1MIX Input 2 Volume */
	{ 0x00000704, 0x0000 },    /* R1796  - AIF1TX1MIX Input 3 Source */
	{ 0x00000705, 0x0080 },    /* R1797  - AIF1TX1MIX Input 3 Volume */
	{ 0x00000706, 0x0000 },    /* R1798  - AIF1TX1MIX Input 4 Source */
	{ 0x00000707, 0x0080 },    /* R1799  - AIF1TX1MIX Input 4 Volume */
	{ 0x00000708, 0x0000 },    /* R1800  - AIF1TX2MIX Input 1 Source */
	{ 0x00000709, 0x0080 },    /* R1801  - AIF1TX2MIX Input 1 Volume */
	{ 0x0000070A, 0x0000 },    /* R1802  - AIF1TX2MIX Input 2 Source */
	{ 0x0000070B, 0x0080 },    /* R1803  - AIF1TX2MIX Input 2 Volume */
	{ 0x0000070C, 0x0000 },    /* R1804  - AIF1TX2MIX Input 3 Source */
	{ 0x0000070D, 0x0080 },    /* R1805  - AIF1TX2MIX Input 3 Volume */
	{ 0x0000070E, 0x0000 },    /* R1806  - AIF1TX2MIX Input 4 Source */
	{ 0x0000070F, 0x0080 },    /* R1807  - AIF1TX2MIX Input 4 Volume */
	{ 0x00000710, 0x0000 },    /* R1808  - AIF1TX3MIX Input 1 Source */
	{ 0x00000711, 0x0080 },    /* R1809  - AIF1TX3MIX Input 1 Volume */
	{ 0x00000712, 0x0000 },    /* R1810  - AIF1TX3MIX Input 2 Source */
	{ 0x00000713, 0x0080 },    /* R1811  - AIF1TX3MIX Input 2 Volume */
	{ 0x00000714, 0x0000 },    /* R1812  - AIF1TX3MIX Input 3 Source */
	{ 0x00000715, 0x0080 },    /* R1813  - AIF1TX3MIX Input 3 Volume */
	{ 0x00000716, 0x0000 },    /* R1814  - AIF1TX3MIX Input 4 Source */
	{ 0x00000717, 0x0080 },    /* R1815  - AIF1TX3MIX Input 4 Volume */
	{ 0x00000718, 0x0000 },    /* R1816  - AIF1TX4MIX Input 1 Source */
	{ 0x00000719, 0x0080 },    /* R1817  - AIF1TX4MIX Input 1 Volume */
	{ 0x0000071A, 0x0000 },    /* R1818  - AIF1TX4MIX Input 2 Source */
	{ 0x0000071B, 0x0080 },    /* R1819  - AIF1TX4MIX Input 2 Volume */
	{ 0x0000071C, 0x0000 },    /* R1820  - AIF1TX4MIX Input 3 Source */
	{ 0x0000071D, 0x0080 },    /* R1821  - AIF1TX4MIX Input 3 Volume */
	{ 0x0000071E, 0x0000 },    /* R1822  - AIF1TX4MIX Input 4 Source */
	{ 0x0000071F, 0x0080 },    /* R1823  - AIF1TX4MIX Input 4 Volume */
	{ 0x00000720, 0x0000 },    /* R1824  - AIF1TX5MIX Input 1 Source */
	{ 0x00000721, 0x0080 },    /* R1825  - AIF1TX5MIX Input 1 Volume */
	{ 0x00000722, 0x0000 },    /* R1826  - AIF1TX5MIX Input 2 Source */
	{ 0x00000723, 0x0080 },    /* R1827  - AIF1TX5MIX Input 2 Volume */
	{ 0x00000724, 0x0000 },    /* R1828  - AIF1TX5MIX Input 3 Source */
	{ 0x00000725, 0x0080 },    /* R1829  - AIF1TX5MIX Input 3 Volume */
	{ 0x00000726, 0x0000 },    /* R1830  - AIF1TX5MIX Input 4 Source */
	{ 0x00000727, 0x0080 },    /* R1831  - AIF1TX5MIX Input 4 Volume */
	{ 0x00000728, 0x0000 },    /* R1832  - AIF1TX6MIX Input 1 Source */
	{ 0x00000729, 0x0080 },    /* R1833  - AIF1TX6MIX Input 1 Volume */
	{ 0x0000072A, 0x0000 },    /* R1834  - AIF1TX6MIX Input 2 Source */
	{ 0x0000072B, 0x0080 },    /* R1835  - AIF1TX6MIX Input 2 Volume */
	{ 0x0000072C, 0x0000 },    /* R1836  - AIF1TX6MIX Input 3 Source */
	{ 0x0000072D, 0x0080 },    /* R1837  - AIF1TX6MIX Input 3 Volume */
	{ 0x0000072E, 0x0000 },    /* R1838  - AIF1TX6MIX Input 4 Source */
	{ 0x0000072F, 0x0080 },    /* R1839  - AIF1TX6MIX Input 4 Volume */
	{ 0x00000730, 0x0000 },    /* R1840  - AIF1TX7MIX Input 1 Source */
	{ 0x00000731, 0x0080 },    /* R1841  - AIF1TX7MIX Input 1 Volume */
	{ 0x00000732, 0x0000 },    /* R1842  - AIF1TX7MIX Input 2 Source */
	{ 0x00000733, 0x0080 },    /* R1843  - AIF1TX7MIX Input 2 Volume */
	{ 0x00000734, 0x0000 },    /* R1844  - AIF1TX7MIX Input 3 Source */
	{ 0x00000735, 0x0080 },    /* R1845  - AIF1TX7MIX Input 3 Volume */
	{ 0x00000736, 0x0000 },    /* R1846  - AIF1TX7MIX Input 4 Source */
	{ 0x00000737, 0x0080 },    /* R1847  - AIF1TX7MIX Input 4 Volume */
	{ 0x00000738, 0x0000 },    /* R1848  - AIF1TX8MIX Input 1 Source */
	{ 0x00000739, 0x0080 },    /* R1849  - AIF1TX8MIX Input 1 Volume */
	{ 0x0000073A, 0x0000 },    /* R1850  - AIF1TX8MIX Input 2 Source */
	{ 0x0000073B, 0x0080 },    /* R1851  - AIF1TX8MIX Input 2 Volume */
	{ 0x0000073C, 0x0000 },    /* R1852  - AIF1TX8MIX Input 3 Source */
	{ 0x0000073D, 0x0080 },    /* R1853  - AIF1TX8MIX Input 3 Volume */
	{ 0x0000073E, 0x0000 },    /* R1854  - AIF1TX8MIX Input 4 Source */
	{ 0x0000073F, 0x0080 },    /* R1855  - AIF1TX8MIX Input 4 Volume */
	{ 0x00000740, 0x0000 },    /* R1856  - AIF2TX1MIX Input 1 Source */
	{ 0x00000741, 0x0080 },    /* R1857  - AIF2TX1MIX Input 1 Volume */
	{ 0x00000742, 0x0000 },    /* R1858  - AIF2TX1MIX Input 2 Source */
	{ 0x00000743, 0x0080 },    /* R1859  - AIF2TX1MIX Input 2 Volume */
	{ 0x00000744, 0x0000 },    /* R1860  - AIF2TX1MIX Input 3 Source */
	{ 0x00000745, 0x0080 },    /* R1861  - AIF2TX1MIX Input 3 Volume */
	{ 0x00000746, 0x0000 },    /* R1862  - AIF2TX1MIX Input 4 Source */
	{ 0x00000747, 0x0080 },    /* R1863  - AIF2TX1MIX Input 4 Volume */
	{ 0x00000748, 0x0000 },    /* R1864  - AIF2TX2MIX Input 1 Source */
	{ 0x00000749, 0x0080 },    /* R1865  - AIF2TX2MIX Input 1 Volume */
	{ 0x0000074A, 0x0000 },    /* R1866  - AIF2TX2MIX Input 2 Source */
	{ 0x0000074B, 0x0080 },    /* R1867  - AIF2TX2MIX Input 2 Volume */
	{ 0x0000074C, 0x0000 },    /* R1868  - AIF2TX2MIX Input 3 Source */
	{ 0x0000074D, 0x0080 },    /* R1869  - AIF2TX2MIX Input 3 Volume */
	{ 0x0000074E, 0x0000 },    /* R1870  - AIF2TX2MIX Input 4 Source */
	{ 0x0000074F, 0x0080 },    /* R1871  - AIF2TX2MIX Input 4 Volume */
	{ 0x00000750, 0x0000 },    /* R1872  - AIF2TX3MIX Input 1 Source */
	{ 0x00000751, 0x0080 },    /* R1873  - AIF2TX3MIX Input 1 Volume */
	{ 0x00000752, 0x0000 },    /* R1874  - AIF2TX3MIX Input 2 Source */
	{ 0x00000753, 0x0080 },    /* R1875  - AIF2TX3MIX Input 2 Volume */
	{ 0x00000754, 0x0000 },    /* R1876  - AIF2TX3MIX Input 3 Source */
	{ 0x00000755, 0x0080 },    /* R1877  - AIF2TX3MIX Input 3 Volume */
	{ 0x00000756, 0x0000 },    /* R1878  - AIF2TX3MIX Input 4 Source */
	{ 0x00000757, 0x0080 },    /* R1879  - AIF2TX3MIX Input 4 Volume */
	{ 0x00000758, 0x0000 },    /* R1880  - AIF2TX4MIX Input 1 Source */
	{ 0x00000759, 0x0080 },    /* R1881  - AIF2TX4MIX Input 1 Volume */
	{ 0x0000075A, 0x0000 },    /* R1882  - AIF2TX4MIX Input 2 Source */
	{ 0x0000075B, 0x0080 },    /* R1883  - AIF2TX4MIX Input 2 Volume */
	{ 0x0000075C, 0x0000 },    /* R1884  - AIF2TX4MIX Input 3 Source */
	{ 0x0000075D, 0x0080 },    /* R1885  - AIF2TX4MIX Input 3 Volume */
	{ 0x0000075E, 0x0000 },    /* R1886  - AIF2TX4MIX Input 4 Source */
	{ 0x0000075F, 0x0080 },    /* R1887  - AIF2TX4MIX Input 4 Volume */
	{ 0x00000760, 0x0000 },    /* R1888  - AIF2TX5MIX Input 1 Source */
	{ 0x00000761, 0x0080 },    /* R1889  - AIF2TX5MIX Input 1 Volume */
	{ 0x00000762, 0x0000 },    /* R1890  - AIF2TX5MIX Input 2 Source */
	{ 0x00000763, 0x0080 },    /* R1891  - AIF2TX5MIX Input 2 Volume */
	{ 0x00000764, 0x0000 },    /* R1892  - AIF2TX5MIX Input 3 Source */
	{ 0x00000765, 0x0080 },    /* R1893  - AIF2TX5MIX Input 3 Volume */
	{ 0x00000766, 0x0000 },    /* R1894  - AIF2TX5MIX Input 4 Source */
	{ 0x00000767, 0x0080 },    /* R1895  - AIF2TX5MIX Input 4 Volume */
	{ 0x00000768, 0x0000 },    /* R1896  - AIF2TX6MIX Input 1 Source */
	{ 0x00000769, 0x0080 },    /* R1897  - AIF2TX6MIX Input 1 Volume */
	{ 0x0000076A, 0x0000 },    /* R1898  - AIF2TX6MIX Input 2 Source */
	{ 0x0000076B, 0x0080 },    /* R1899  - AIF2TX6MIX Input 2 Volume */
	{ 0x0000076C, 0x0000 },    /* R1900  - AIF2TX6MIX Input 3 Source */
	{ 0x0000076D, 0x0080 },    /* R1901  - AIF2TX6MIX Input 3 Volume */
	{ 0x0000076E, 0x0000 },    /* R1902  - AIF2TX6MIX Input 4 Source */
	{ 0x0000076F, 0x0080 },    /* R1903  - AIF2TX6MIX Input 4 Volume */
	{ 0x00000780, 0x0000 },    /* R1920  - AIF3TX1MIX Input 1 Source */
	{ 0x00000781, 0x0080 },    /* R1921  - AIF3TX1MIX Input 1 Volume */
	{ 0x00000782, 0x0000 },    /* R1922  - AIF3TX1MIX Input 2 Source */
	{ 0x00000783, 0x0080 },    /* R1923  - AIF3TX1MIX Input 2 Volume */
	{ 0x00000784, 0x0000 },    /* R1924  - AIF3TX1MIX Input 3 Source */
	{ 0x00000785, 0x0080 },    /* R1925  - AIF3TX1MIX Input 3 Volume */
	{ 0x00000786, 0x0000 },    /* R1926  - AIF3TX1MIX Input 4 Source */
	{ 0x00000787, 0x0080 },    /* R1927  - AIF3TX1MIX Input 4 Volume */
	{ 0x00000788, 0x0000 },    /* R1928  - AIF3TX2MIX Input 1 Source */
	{ 0x00000789, 0x0080 },    /* R1929  - AIF3TX2MIX Input 1 Volume */
	{ 0x0000078A, 0x0000 },    /* R1930  - AIF3TX2MIX Input 2 Source */
	{ 0x0000078B, 0x0080 },    /* R1931  - AIF3TX2MIX Input 2 Volume */
	{ 0x0000078C, 0x0000 },    /* R1932  - AIF3TX2MIX Input 3 Source */
	{ 0x0000078D, 0x0080 },    /* R1933  - AIF3TX2MIX Input 3 Volume */
	{ 0x0000078E, 0x0000 },    /* R1934  - AIF3TX2MIX Input 4 Source */
	{ 0x0000078F, 0x0080 },    /* R1935  - AIF3TX2MIX Input 4 Volume */
	{ 0x00000880, 0x0000 },    /* R2176  - EQ1MIX Input 1 Source */
	{ 0x00000881, 0x0080 },    /* R2177  - EQ1MIX Input 1 Volume */
	{ 0x00000882, 0x0000 },    /* R2178  - EQ1MIX Input 2 Source */
	{ 0x00000883, 0x0080 },    /* R2179  - EQ1MIX Input 2 Volume */
	{ 0x00000884, 0x0000 },    /* R2180  - EQ1MIX Input 3 Source */
	{ 0x00000885, 0x0080 },    /* R2181  - EQ1MIX Input 3 Volume */
	{ 0x00000886, 0x0000 },    /* R2182  - EQ1MIX Input 4 Source */
	{ 0x00000887, 0x0080 },    /* R2183  - EQ1MIX Input 4 Volume */
	{ 0x00000888, 0x0000 },    /* R2184  - EQ2MIX Input 1 Source */
	{ 0x00000889, 0x0080 },    /* R2185  - EQ2MIX Input 1 Volume */
	{ 0x0000088A, 0x0000 },    /* R2186  - EQ2MIX Input 2 Source */
	{ 0x0000088B, 0x0080 },    /* R2187  - EQ2MIX Input 2 Volume */
	{ 0x0000088C, 0x0000 },    /* R2188  - EQ2MIX Input 3 Source */
	{ 0x0000088D, 0x0080 },    /* R2189  - EQ2MIX Input 3 Volume */
	{ 0x0000088E, 0x0000 },    /* R2190  - EQ2MIX Input 4 Source */
	{ 0x0000088F, 0x0080 },    /* R2191  - EQ2MIX Input 4 Volume */
	{ 0x000008C0, 0x0000 },    /* R2240  - DRC1LMIX Input 1 Source */
	{ 0x000008C1, 0x0080 },    /* R2241  - DRC1LMIX Input 1 Volume */
	{ 0x000008C2, 0x0000 },    /* R2242  - DRC1LMIX Input 2 Source */
	{ 0x000008C3, 0x0080 },    /* R2243  - DRC1LMIX Input 2 Volume */
	{ 0x000008C4, 0x0000 },    /* R2244  - DRC1LMIX Input 3 Source */
	{ 0x000008C5, 0x0080 },    /* R2245  - DRC1LMIX Input 3 Volume */
	{ 0x000008C6, 0x0000 },    /* R2246  - DRC1LMIX Input 4 Source */
	{ 0x000008C7, 0x0080 },    /* R2247  - DRC1LMIX Input 4 Volume */
	{ 0x000008C8, 0x0000 },    /* R2248  - DRC1RMIX Input 1 Source */
	{ 0x000008C9, 0x0080 },    /* R2249  - DRC1RMIX Input 1 Volume */
	{ 0x000008CA, 0x0000 },    /* R2250  - DRC1RMIX Input 2 Source */
	{ 0x000008CB, 0x0080 },    /* R2251  - DRC1RMIX Input 2 Volume */
	{ 0x000008CC, 0x0000 },    /* R2252  - DRC1RMIX Input 3 Source */
	{ 0x000008CD, 0x0080 },    /* R2253  - DRC1RMIX Input 3 Volume */
	{ 0x000008CE, 0x0000 },    /* R2254  - DRC1RMIX Input 4 Source */
	{ 0x000008CF, 0x0080 },    /* R2255  - DRC1RMIX Input 4 Volume */
	{ 0x000008D0, 0x0000 },    /* R2256  - DRC2LMIX Input 1 Source */
	{ 0x000008D1, 0x0080 },    /* R2257  - DRC2LMIX Input 1 Volume */
	{ 0x000008D2, 0x0000 },    /* R2258  - DRC2LMIX Input 2 Source */
	{ 0x000008D3, 0x0080 },    /* R2259  - DRC2LMIX Input 2 Volume */
	{ 0x000008D4, 0x0000 },    /* R2260  - DRC2LMIX Input 3 Source */
	{ 0x000008D5, 0x0080 },    /* R2261  - DRC2LMIX Input 3 Volume */
	{ 0x000008D6, 0x0000 },    /* R2262  - DRC2LMIX Input 4 Source */
	{ 0x000008D7, 0x0080 },    /* R2263  - DRC2LMIX Input 4 Volume */
	{ 0x000008D8, 0x0000 },    /* R2264  - DRC2RMIX Input 1 Source */
	{ 0x000008D9, 0x0080 },    /* R2265  - DRC2RMIX Input 1 Volume */
	{ 0x000008DA, 0x0000 },    /* R2266  - DRC2RMIX Input 2 Source */
	{ 0x000008DB, 0x0080 },    /* R2267  - DRC2RMIX Input 2 Volume */
	{ 0x000008DC, 0x0000 },    /* R2268  - DRC2RMIX Input 3 Source */
	{ 0x000008DD, 0x0080 },    /* R2269  - DRC2RMIX Input 3 Volume */
	{ 0x000008DE, 0x0000 },    /* R2270  - DRC2RMIX Input 4 Source */
	{ 0x000008DF, 0x0080 },    /* R2271  - DRC2RMIX Input 4 Volume */
	{ 0x00000900, 0x0000 },    /* R2304  - HPLP1MIX Input 1 Source */
	{ 0x00000901, 0x0080 },    /* R2305  - HPLP1MIX Input 1 Volume */
	{ 0x00000902, 0x0000 },    /* R2306  - HPLP1MIX Input 2 Source */
	{ 0x00000903, 0x0080 },    /* R2307  - HPLP1MIX Input 2 Volume */
	{ 0x00000904, 0x0000 },    /* R2308  - HPLP1MIX Input 3 Source */
	{ 0x00000905, 0x0080 },    /* R2309  - HPLP1MIX Input 3 Volume */
	{ 0x00000906, 0x0000 },    /* R2310  - HPLP1MIX Input 4 Source */
	{ 0x00000907, 0x0080 },    /* R2311  - HPLP1MIX Input 4 Volume */
	{ 0x00000908, 0x0000 },    /* R2312  - HPLP2MIX Input 1 Source */
	{ 0x00000909, 0x0080 },    /* R2313  - HPLP2MIX Input 1 Volume */
	{ 0x0000090A, 0x0000 },    /* R2314  - HPLP2MIX Input 2 Source */
	{ 0x0000090B, 0x0080 },    /* R2315  - HPLP2MIX Input 2 Volume */
	{ 0x0000090C, 0x0000 },    /* R2316  - HPLP2MIX Input 3 Source */
	{ 0x0000090D, 0x0080 },    /* R2317  - HPLP2MIX Input 3 Volume */
	{ 0x0000090E, 0x0000 },    /* R2318  - HPLP2MIX Input 4 Source */
	{ 0x0000090F, 0x0080 },    /* R2319  - HPLP2MIX Input 4 Volume */
	{ 0x00000910, 0x0000 },    /* R2320  - HPLP3MIX Input 1 Source */
	{ 0x00000911, 0x0080 },    /* R2321  - HPLP3MIX Input 1 Volume */
	{ 0x00000912, 0x0000 },    /* R2322  - HPLP3MIX Input 2 Source */
	{ 0x00000913, 0x0080 },    /* R2323  - HPLP3MIX Input 2 Volume */
	{ 0x00000914, 0x0000 },    /* R2324  - HPLP3MIX Input 3 Source */
	{ 0x00000915, 0x0080 },    /* R2325  - HPLP3MIX Input 3 Volume */
	{ 0x00000916, 0x0000 },    /* R2326  - HPLP3MIX Input 4 Source */
	{ 0x00000917, 0x0080 },    /* R2327  - HPLP3MIX Input 4 Volume */
	{ 0x00000918, 0x0000 },    /* R2328  - HPLP4MIX Input 1 Source */
	{ 0x00000919, 0x0080 },    /* R2329  - HPLP4MIX Input 1 Volume */
	{ 0x0000091A, 0x0000 },    /* R2330  - HPLP4MIX Input 2 Source */
	{ 0x0000091B, 0x0080 },    /* R2331  - HPLP4MIX Input 2 Volume */
	{ 0x0000091C, 0x0000 },    /* R2332  - HPLP4MIX Input 3 Source */
	{ 0x0000091D, 0x0080 },    /* R2333  - HPLP4MIX Input 3 Volume */
	{ 0x0000091E, 0x0000 },    /* R2334  - HPLP4MIX Input 4 Source */
	{ 0x0000091F, 0x0080 },    /* R2335  - HPLP4MIX Input 4 Volume */
	{ 0x00000980, 0x0000 },    /* R2432  - DSP2LMIX Input 1 Source */
	{ 0x00000981, 0x0080 },    /* R2433  - DSP2LMIX Input 1 Volume */
	{ 0x00000982, 0x0000 },    /* R2434  - DSP2LMIX Input 2 Source */
	{ 0x00000983, 0x0080 },    /* R2435  - DSP2LMIX Input 2 Volume */
	{ 0x00000984, 0x0000 },    /* R2436  - DSP2LMIX Input 3 Source */
	{ 0x00000985, 0x0080 },    /* R2437  - DSP2LMIX Input 3 Volume */
	{ 0x00000986, 0x0000 },    /* R2438  - DSP2LMIX Input 4 Source */
	{ 0x00000987, 0x0080 },    /* R2439  - DSP2LMIX Input 4 Volume */
	{ 0x00000988, 0x0000 },    /* R2440  - DSP2RMIX Input 1 Source */
	{ 0x00000989, 0x0080 },    /* R2441  - DSP2RMIX Input 1 Volume */
	{ 0x0000098A, 0x0000 },    /* R2442  - DSP2RMIX Input 2 Source */
	{ 0x0000098B, 0x0080 },    /* R2443  - DSP2RMIX Input 2 Volume */
	{ 0x0000098C, 0x0000 },    /* R2444  - DSP2RMIX Input 3 Source */
	{ 0x0000098D, 0x0080 },    /* R2445  - DSP2RMIX Input 3 Volume */
	{ 0x0000098E, 0x0000 },    /* R2446  - DSP2RMIX Input 4 Source */
	{ 0x0000098F, 0x0080 },    /* R2447  - DSP2RMIX Input 4 Volume */
	{ 0x00000990, 0x0000 },    /* R2448  - DSP2AUX1MIX Input 1 Source */
	{ 0x00000998, 0x0000 },    /* R2456  - DSP2AUX2MIX Input 1 Source */
	{ 0x000009A0, 0x0000 },    /* R2464  - DSP2AUX3MIX Input 1 Source */
	{ 0x000009A8, 0x0000 },    /* R2472  - DSP2AUX4MIX Input 1 Source */
	{ 0x000009B0, 0x0000 },    /* R2480  - DSP2AUX5MIX Input 1 Source */
	{ 0x000009B8, 0x0000 },    /* R2488  - DSP2AUX6MIX Input 1 Source */
	{ 0x000009C0, 0x0000 },    /* R2496  - DSP3LMIX Input 1 Source */
	{ 0x000009C1, 0x0080 },    /* R2497  - DSP3LMIX Input 1 Volume */
	{ 0x000009C2, 0x0000 },    /* R2498  - DSP3LMIX Input 2 Source */
	{ 0x000009C3, 0x0080 },    /* R2499  - DSP3LMIX Input 2 Volume */
	{ 0x000009C4, 0x0000 },    /* R2500  - DSP3LMIX Input 3 Source */
	{ 0x000009C5, 0x0080 },    /* R2501  - DSP3LMIX Input 3 Volume */
	{ 0x000009C6, 0x0000 },    /* R2502  - DSP3LMIX Input 4 Source */
	{ 0x000009C7, 0x0080 },    /* R2503  - DSP3LMIX Input 4 Volume */
	{ 0x000009C8, 0x0000 },    /* R2504  - DSP3RMIX Input 1 Source */
	{ 0x000009C9, 0x0080 },    /* R2505  - DSP3RMIX Input 1 Volume */
	{ 0x000009CA, 0x0000 },    /* R2506  - DSP3RMIX Input 2 Source */
	{ 0x000009CB, 0x0080 },    /* R2507  - DSP3RMIX Input 2 Volume */
	{ 0x000009CC, 0x0000 },    /* R2508  - DSP3RMIX Input 3 Source */
	{ 0x000009CD, 0x0080 },    /* R2509  - DSP3RMIX Input 3 Volume */
	{ 0x000009CE, 0x0000 },    /* R2510  - DSP3RMIX Input 4 Source */
	{ 0x000009CF, 0x0080 },    /* R2511  - DSP3RMIX Input 4 Volume */
	{ 0x000009D0, 0x0000 },    /* R2512  - DSP3AUX1MIX Input 1 Source */
	{ 0x000009D8, 0x0000 },    /* R2520  - DSP3AUX2MIX Input 1 Source */
	{ 0x000009E0, 0x0000 },    /* R2528  - DSP3AUX3MIX Input 1 Source */
	{ 0x000009E8, 0x0000 },    /* R2536  - DSP3AUX4MIX Input 1 Source */
	{ 0x000009F0, 0x0000 },    /* R2544  - DSP3AUX5MIX Input 1 Source */
	{ 0x000009F8, 0x0000 },    /* R2552  - DSP3AUX6MIX Input 1 Source */
	{ 0x00000A80, 0x0000 },    /* R2688  - ASRC1LMIX Input 1 Source */
	{ 0x00000A88, 0x0000 },    /* R2696  - ASRC1RMIX Input 1 Source */
	{ 0x00000A90, 0x0000 },    /* R2704  - ASRC2LMIX Input 1 Source */
	{ 0x00000A98, 0x0000 },    /* R2712  - ASRC2RMIX Input 1 Source */
	{ 0x00000B00, 0x0000 },    /* R2816  - ISRC1DEC1MIX Input 1 Source */
	{ 0x00000B08, 0x0000 },    /* R2824  - ISRC1DEC2MIX Input 1 Source */
	{ 0x00000B10, 0x0000 },    /* R2832  - ISRC1DEC3MIX Input 1 Source */
	{ 0x00000B18, 0x0000 },    /* R2840  - ISRC1DEC4MIX Input 1 Source */
	{ 0x00000B20, 0x0000 },    /* R2848  - ISRC1INT1MIX Input 1 Source */
	{ 0x00000B28, 0x0000 },    /* R2856  - ISRC1INT2MIX Input 1 Source */
	{ 0x00000B30, 0x0000 },    /* R2864  - ISRC1INT3MIX Input 1 Source */
	{ 0x00000B38, 0x0000 },    /* R2872  - ISRC1INT4MIX Input 1 Source */
	{ 0x00000B40, 0x0000 },    /* R2880  - ISRC2DEC1MIX Input 1 Source */
	{ 0x00000B48, 0x0000 },    /* R2888  - ISRC2DEC2MIX Input 1 Source */
	{ 0x00000B50, 0x0000 },    /* R2896  - ISRC2DEC3MIX Input 1 Source */
	{ 0x00000B58, 0x0000 },    /* R2904  - ISRC2DEC4MIX Input 1 Source */
	{ 0x00000B60, 0x0000 },    /* R2912  - ISRC2INT1MIX Input 1 Source */
	{ 0x00000B68, 0x0000 },    /* R2920  - ISRC2INT2MIX Input 1 Source */
	{ 0x00000B70, 0x0000 },    /* R2928  - ISRC2INT3MIX Input 1 Source */
	{ 0x00000B78, 0x0000 },    /* R2936  - ISRC2INT4MIX Input 1 Source */
	{ 0x00000B80, 0x0000 },    /* R2944  - ISRC3DEC1MIX Input 1 Source */
	{ 0x00000B88, 0x0000 },    /* R2952  - ISRC3DEC2MIX Input 1 Source */
	{ 0x00000B90, 0x0000 },    /* R2960  - ISRC3DEC3MIX Input 1 Source */
	{ 0x00000B98, 0x0000 },    /* R2968  - ISRC3DEC4MIX Input 1 Source */
	{ 0x00000BA0, 0x0000 },    /* R2976  - ISRC3INT1MIX Input 1 Source */
	{ 0x00000BA8, 0x0000 },    /* R2984  - ISRC3INT2MIX Input 1 Source */
	{ 0x00000BB0, 0x0000 },    /* R2992  - ISRC3INT3MIX Input 1 Source */
	{ 0x00000BB8, 0x0000 },    /* R3000  - ISRC3INT4MIX Input 1 Source */
	{ 0x00000C00, 0xA101 },    /* R3072  - GPIO1 CTRL */
	{ 0x00000C01, 0xA101 },    /* R3073  - GPIO2 CTRL */
	{ 0x00000C0F, 0x0400 },    /* R3087  - IRQ CTRL 1 */
	{ 0x00000C10, 0x1000 },    /* R3088  - GPIO Debounce Config */
	{ 0x00000C20, 0x0002 },    /* R3104  - Misc Pad Ctrl 1 */
	{ 0x00000C21, 0x0000 },    /* R3105  - Misc Pad Ctrl 2 */
	{ 0x00000C22, 0x0000 },    /* R3106  - Misc Pad Ctrl 3 */
	{ 0x00000C23, 0x0000 },    /* R3107  - Misc Pad Ctrl 4 */
	{ 0x00000C24, 0x0000 },    /* R3108  - Misc Pad Ctrl 5 */
	{ 0x00000C25, 0x0000 },    /* R3109  - Misc Pad Ctrl 6 */
	{ 0x00000C30, 0x0404 },    /* R3120  - Misc Pad Ctrl 7 */
	{ 0x00000C32, 0x0404 },    /* R3122  - Misc Pad Ctrl 9 */
	{ 0x00000C33, 0x0404 },    /* R3123  - Misc Pad Ctrl 10 */
	{ 0x00000C34, 0x0404 },    /* R3124  - Misc Pad Ctrl 11 */
	{ 0x00000C35, 0x0404 },    /* R3125  - Misc Pad Ctrl 12 */
	{ 0x00000C36, 0x0400 },    /* R3126  - Misc Pad Ctrl 13 */
	{ 0x00000C37, 0x0404 },    /* R3127  - Misc Pad Ctrl 14 */
	{ 0x00000C39, 0x0400 },    /* R3129  - Misc Pad Ctrl 16 */
	{ 0x00000D08, 0x0007 },    /* R3336  - Interrupt Status 1 Mask */
	{ 0x00000D09, 0x06FF },    /* R3337  - Interrupt Status 2 Mask */
	{ 0x00000D0A, 0xCFEF },    /* R3338  - Interrupt Status 3 Mask */
	{ 0x00000D0B, 0xFFC3 },    /* R3339  - Interrupt Status 4 Mask */
	{ 0x00000D0C, 0x000B },    /* R3340  - Interrupt Status 5 Mask */
	{ 0x00000D0D, 0xD005 },    /* R3341  - Interrupt Status 6 Mask */
	{ 0x00000D0F, 0x0000 },    /* R3343  - Interrupt Control */
	{ 0x00000D18, 0x0007 },    /* R3352  - IRQ2 Status 1 Mask */
	{ 0x00000D19, 0x06FF },    /* R3353  - IRQ2 Status 2 Mask */
	{ 0x00000D1A, 0xCFEF },    /* R3354  - IRQ2 Status 3 Mask */
	{ 0x00000D1B, 0xFFC3 },    /* R3355  - IRQ2 Status 4 Mask */
	{ 0x00000D1C, 0x000B },    /* R3356  - IRQ2 Status 5 Mask */
	{ 0x00000D1D, 0xD005 },    /* R3357  - IRQ2 Status 6 Mask */
	{ 0x00000D1F, 0x0000 },    /* R3359  - IRQ2 Control */
	{ 0x00000E00, 0x0000 },    /* R3584  - FX_Ctrl1 */
	{ 0x00000E10, 0x6318 },    /* R3600  - EQ1_1 */
	{ 0x00000E11, 0x6300 },    /* R3601  - EQ1_2 */
	{ 0x00000E12, 0x0FC8 },    /* R3602  - EQ1_3 */
	{ 0x00000E13, 0x03FE },    /* R3603  - EQ1_4 */
	{ 0x00000E14, 0x00E0 },    /* R3604  - EQ1_5 */
	{ 0x00000E15, 0x1EC4 },    /* R3605  - EQ1_6 */
	{ 0x00000E16, 0xF136 },    /* R3606  - EQ1_7 */
	{ 0x00000E17, 0x0409 },    /* R3607  - EQ1_8 */
	{ 0x00000E18, 0x04CC },    /* R3608  - EQ1_9 */
	{ 0x00000E19, 0x1C9B },    /* R3609  - EQ1_10 */
	{ 0x00000E1A, 0xF337 },    /* R3610  - EQ1_11 */
	{ 0x00000E1B, 0x040B },    /* R3611  - EQ1_12 */
	{ 0x00000E1C, 0x0CBB },    /* R3612  - EQ1_13 */
	{ 0x00000E1D, 0x16F8 },    /* R3613  - EQ1_14 */
	{ 0x00000E1E, 0xF7D9 },    /* R3614  - EQ1_15 */
	{ 0x00000E1F, 0x040A },    /* R3615  - EQ1_16 */
	{ 0x00000E20, 0x1F14 },    /* R3616  - EQ1_17 */
	{ 0x00000E21, 0x058C },    /* R3617  - EQ1_18 */
	{ 0x00000E22, 0x0563 },    /* R3618  - EQ1_19 */
	{ 0x00000E23, 0x4000 },    /* R3619  - EQ1_20 */
	{ 0x00000E24, 0x0B75 },    /* R3620  - EQ1_21 */
	{ 0x00000E26, 0x6318 },    /* R3622  - EQ2_1 */
	{ 0x00000E27, 0x6300 },    /* R3623  - EQ2_2 */
	{ 0x00000E28, 0x0FC8 },    /* R3624  - EQ2_3 */
	{ 0x00000E29, 0x03FE },    /* R3625  - EQ2_4 */
	{ 0x00000E2A, 0x00E0 },    /* R3626  - EQ2_5 */
	{ 0x00000E2B, 0x1EC4 },    /* R3627  - EQ2_6 */
	{ 0x00000E2C, 0xF136 },    /* R3628  - EQ2_7 */
	{ 0x00000E2D, 0x0409 },    /* R3629  - EQ2_8 */
	{ 0x00000E2E, 0x04CC },    /* R3630  - EQ2_9 */
	{ 0x00000E2F, 0x1C9B },    /* R3631  - EQ2_10 */
	{ 0x00000E30, 0xF337 },    /* R3632  - EQ2_11 */
	{ 0x00000E31, 0x040B },    /* R3633  - EQ2_12 */
	{ 0x00000E32, 0x0CBB },    /* R3634  - EQ2_13 */
	{ 0x00000E33, 0x16F8 },    /* R3635  - EQ2_14 */
	{ 0x00000E34, 0xF7D9 },    /* R3636  - EQ2_15 */
	{ 0x00000E35, 0x040A },    /* R3637  - EQ2_16 */
	{ 0x00000E36, 0x1F14 },    /* R3638  - EQ2_17 */
	{ 0x00000E37, 0x058C },    /* R3639  - EQ2_18 */
	{ 0x00000E38, 0x0563 },    /* R3640  - EQ2_19 */
	{ 0x00000E39, 0x4000 },    /* R3641  - EQ2_20 */
	{ 0x00000E3A, 0x0B75 },    /* R3642  - EQ2_21 */
	{ 0x00000E80, 0x0018 },    /* R3712  - DRC1 ctrl1 */
	{ 0x00000E81, 0x0933 },    /* R3713  - DRC1 ctrl2 */
	{ 0x00000E82, 0x0018 },    /* R3714  - DRC1 ctrl3 */
	{ 0x00000E83, 0x0000 },    /* R3715  - DRC1 ctrl4 */
	{ 0x00000E84, 0x0000 },    /* R3716  - DRC1 ctrl5 */
	{ 0x00000E89, 0x0018 },    /* R3721  - DRC2 ctrl1 */
	{ 0x00000E8A, 0x0933 },    /* R3722  - DRC2 ctrl2 */
	{ 0x00000E8B, 0x0018 },    /* R3723  - DRC2 ctrl3 */
	{ 0x00000E8C, 0x0000 },    /* R3724  - DRC2 ctrl4 */
	{ 0x00000E8D, 0x0000 },    /* R3725  - DRC2 ctrl5 */
	{ 0x00000EC0, 0x0000 },    /* R3776  - HPLPF1_1 */
	{ 0x00000EC1, 0x0000 },    /* R3777  - HPLPF1_2 */
	{ 0x00000EC4, 0x0000 },    /* R3780  - HPLPF2_1 */
	{ 0x00000EC5, 0x0000 },    /* R3781  - HPLPF2_2 */
	{ 0x00000EC8, 0x0000 },    /* R3784  - HPLPF3_1 */
	{ 0x00000EC9, 0x0000 },    /* R3785  - HPLPF3_2 */
	{ 0x00000ECC, 0x0000 },    /* R3788  - HPLPF4_1 */
	{ 0x00000ECD, 0x0000 },    /* R3789  - HPLPF4_2 */
	{ 0x00000EE0, 0x0000 },    /* R3808  - ASRC_ENABLE */
	{ 0x00000EE2, 0x0000 },    /* R3810  - ASRC_RATE1 */
	{ 0x00000EE3, 0x4000 },    /* R3811  - ASRC_RATE2 */
	{ 0x00000EF0, 0x0000 },    /* R3824  - ISRC 1 CTRL 1 */
	{ 0x00000EF1, 0x0000 },    /* R3825  - ISRC 1 CTRL 2 */
	{ 0x00000EF2, 0x0000 },    /* R3826  - ISRC 1 CTRL 3 */
	{ 0x00000EF3, 0x0000 },    /* R3827  - ISRC 2 CTRL 1 */
	{ 0x00000EF4, 0x0000 },    /* R3828  - ISRC 2 CTRL 2 */
	{ 0x00000EF5, 0x0000 },    /* R3829  - ISRC 2 CTRL 3 */
	{ 0x00000EF6, 0x0000 },    /* R3830  - ISRC 3 CTRL 1 */
	{ 0x00000EF7, 0x0000 },    /* R3831  - ISRC 3 CTRL 2 */
	{ 0x00000EF8, 0x0000 },    /* R3832  - ISRC 3 CTRL 3 */
	{ 0x00001200, 0x0010 },    /* R4608  - DSP2 Control 1 */
	{ 0x00001300, 0x0010 },    /* R4864  - DSP3 Control 1 */
};

static bool cs47l24_is_adsp_memory(unsigned int reg)
{
	switch (reg) {
	case 0x200000 ... 0x205fff:	/* DSP2 PM */
	case 0x280000 ... 0x281fff:	/* DSP2 ZM */
	case 0x290000 ... 0x2a7fff:	/* DSP2 XM */
	case 0x2a8000 ... 0x2b3fff:	/* DSP2 YM */
	case 0x300000 ... 0x308fff:	/* DSP3 PM */
	case 0x380000 ... 0x381fff:	/* DSP3 ZM */
	case 0x390000 ... 0x3a7fff:	/* DSP3 XM */
	case 0x3a8000 ... 0x3b3fff:	/* DSP3 YM */
		return true;
	default:
		return false;
	}
}

static bool cs47l24_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ARIZONA_SOFTWARE_RESET:
	case ARIZONA_DEVICE_REVISION:
	case ARIZONA_CTRL_IF_SPI_CFG_1:
	case ARIZONA_WRITE_SEQUENCER_CTRL_0:
	case ARIZONA_WRITE_SEQUENCER_CTRL_1:
	case ARIZONA_WRITE_SEQUENCER_CTRL_2:
	case ARIZONA_TONE_GENERATOR_1:
	case ARIZONA_TONE_GENERATOR_2:
	case ARIZONA_TONE_GENERATOR_3:
	case ARIZONA_TONE_GENERATOR_4:
	case ARIZONA_TONE_GENERATOR_5:
	case ARIZONA_PWM_DRIVE_1:
	case ARIZONA_PWM_DRIVE_2:
	case ARIZONA_PWM_DRIVE_3:
	case ARIZONA_SEQUENCE_CONTROL:
	case ARIZONA_SAMPLE_RATE_SEQUENCE_SELECT_1:
	case ARIZONA_SAMPLE_RATE_SEQUENCE_SELECT_2:
	case ARIZONA_SAMPLE_RATE_SEQUENCE_SELECT_3:
	case ARIZONA_SAMPLE_RATE_SEQUENCE_SELECT_4:
	case ARIZONA_COMFORT_NOISE_GENERATOR:
	case ARIZONA_HAPTICS_CONTROL_1:
	case ARIZONA_HAPTICS_CONTROL_2:
	case ARIZONA_HAPTICS_PHASE_1_INTENSITY:
	case ARIZONA_HAPTICS_PHASE_1_DURATION:
	case ARIZONA_HAPTICS_PHASE_2_INTENSITY:
	case ARIZONA_HAPTICS_PHASE_2_DURATION:
	case ARIZONA_HAPTICS_PHASE_3_INTENSITY:
	case ARIZONA_HAPTICS_PHASE_3_DURATION:
	case ARIZONA_HAPTICS_STATUS:
	case ARIZONA_CLOCK_32K_1:
	case ARIZONA_SYSTEM_CLOCK_1:
	case ARIZONA_SAMPLE_RATE_1:
	case ARIZONA_SAMPLE_RATE_2:
	case ARIZONA_SAMPLE_RATE_3:
	case ARIZONA_SAMPLE_RATE_1_STATUS:
	case ARIZONA_SAMPLE_RATE_2_STATUS:
	case ARIZONA_SAMPLE_RATE_3_STATUS:
	case ARIZONA_ASYNC_CLOCK_1:
	case ARIZONA_ASYNC_SAMPLE_RATE_1:
	case ARIZONA_ASYNC_SAMPLE_RATE_1_STATUS:
	case ARIZONA_ASYNC_SAMPLE_RATE_2:
	case ARIZONA_ASYNC_SAMPLE_RATE_2_STATUS:
	case ARIZONA_OUTPUT_SYSTEM_CLOCK:
	case ARIZONA_OUTPUT_ASYNC_CLOCK:
	case ARIZONA_RATE_ESTIMATOR_1:
	case ARIZONA_RATE_ESTIMATOR_2:
	case ARIZONA_RATE_ESTIMATOR_3:
	case ARIZONA_RATE_ESTIMATOR_4:
	case ARIZONA_RATE_ESTIMATOR_5:
	case ARIZONA_FLL1_CONTROL_1:
	case ARIZONA_FLL1_CONTROL_2:
	case ARIZONA_FLL1_CONTROL_3:
	case ARIZONA_FLL1_CONTROL_4:
	case ARIZONA_FLL1_CONTROL_5:
	case ARIZONA_FLL1_CONTROL_6:
	case ARIZONA_FLL1_CONTROL_7:
	case ARIZONA_FLL1_SYNCHRONISER_1:
	case ARIZONA_FLL1_SYNCHRONISER_2:
	case ARIZONA_FLL1_SYNCHRONISER_3:
	case ARIZONA_FLL1_SYNCHRONISER_4:
	case ARIZONA_FLL1_SYNCHRONISER_5:
	case ARIZONA_FLL1_SYNCHRONISER_6:
	case ARIZONA_FLL1_SYNCHRONISER_7:
	case ARIZONA_FLL1_SPREAD_SPECTRUM:
	case ARIZONA_FLL1_GPIO_CLOCK:
	case ARIZONA_FLL2_CONTROL_1:
	case ARIZONA_FLL2_CONTROL_2:
	case ARIZONA_FLL2_CONTROL_3:
	case ARIZONA_FLL2_CONTROL_4:
	case ARIZONA_FLL2_CONTROL_5:
	case ARIZONA_FLL2_CONTROL_6:
	case ARIZONA_FLL2_CONTROL_7:
	case ARIZONA_FLL2_SYNCHRONISER_1:
	case ARIZONA_FLL2_SYNCHRONISER_2:
	case ARIZONA_FLL2_SYNCHRONISER_3:
	case ARIZONA_FLL2_SYNCHRONISER_4:
	case ARIZONA_FLL2_SYNCHRONISER_5:
	case ARIZONA_FLL2_SYNCHRONISER_6:
	case ARIZONA_FLL2_SYNCHRONISER_7:
	case ARIZONA_FLL2_SPREAD_SPECTRUM:
	case ARIZONA_FLL2_GPIO_CLOCK:
	case ARIZONA_MIC_BIAS_CTRL_1:
	case ARIZONA_MIC_BIAS_CTRL_2:
	case ARIZONA_HP_CTRL_1L:
	case ARIZONA_HP_CTRL_1R:
	case ARIZONA_INPUT_ENABLES:
	case ARIZONA_INPUT_ENABLES_STATUS:
	case ARIZONA_INPUT_RATE:
	case ARIZONA_INPUT_VOLUME_RAMP:
	case ARIZONA_HPF_CONTROL:
	case ARIZONA_IN1L_CONTROL:
	case ARIZONA_ADC_DIGITAL_VOLUME_1L:
	case ARIZONA_DMIC1L_CONTROL:
	case ARIZONA_IN1R_CONTROL:
	case ARIZONA_ADC_DIGITAL_VOLUME_1R:
	case ARIZONA_DMIC1R_CONTROL:
	case ARIZONA_IN2L_CONTROL:
	case ARIZONA_ADC_DIGITAL_VOLUME_2L:
	case ARIZONA_DMIC2L_CONTROL:
	case ARIZONA_IN2R_CONTROL:
	case ARIZONA_ADC_DIGITAL_VOLUME_2R:
	case ARIZONA_DMIC2R_CONTROL:
	case ARIZONA_OUTPUT_ENABLES_1:
	case ARIZONA_OUTPUT_STATUS_1:
	case ARIZONA_RAW_OUTPUT_STATUS_1:
	case ARIZONA_OUTPUT_RATE_1:
	case ARIZONA_OUTPUT_VOLUME_RAMP:
	case ARIZONA_OUTPUT_PATH_CONFIG_1L:
	case ARIZONA_DAC_DIGITAL_VOLUME_1L:
	case ARIZONA_DAC_VOLUME_LIMIT_1L:
	case ARIZONA_NOISE_GATE_SELECT_1L:
	case ARIZONA_DAC_DIGITAL_VOLUME_1R:
	case ARIZONA_DAC_VOLUME_LIMIT_1R:
	case ARIZONA_NOISE_GATE_SELECT_1R:
	case ARIZONA_DAC_DIGITAL_VOLUME_4L:
	case ARIZONA_OUT_VOLUME_4L:
	case ARIZONA_NOISE_GATE_SELECT_4L:
	case ARIZONA_DAC_AEC_CONTROL_1:
	case ARIZONA_NOISE_GATE_CONTROL:
	case ARIZONA_HP1_SHORT_CIRCUIT_CTRL:
	case ARIZONA_AIF1_BCLK_CTRL:
	case ARIZONA_AIF1_TX_PIN_CTRL:
	case ARIZONA_AIF1_RX_PIN_CTRL:
	case ARIZONA_AIF1_RATE_CTRL:
	case ARIZONA_AIF1_FORMAT:
	case ARIZONA_AIF1_RX_BCLK_RATE:
	case ARIZONA_AIF1_FRAME_CTRL_1:
	case ARIZONA_AIF1_FRAME_CTRL_2:
	case ARIZONA_AIF1_FRAME_CTRL_3:
	case ARIZONA_AIF1_FRAME_CTRL_4:
	case ARIZONA_AIF1_FRAME_CTRL_5:
	case ARIZONA_AIF1_FRAME_CTRL_6:
	case ARIZONA_AIF1_FRAME_CTRL_7:
	case ARIZONA_AIF1_FRAME_CTRL_8:
	case ARIZONA_AIF1_FRAME_CTRL_9:
	case ARIZONA_AIF1_FRAME_CTRL_10:
	case ARIZONA_AIF1_FRAME_CTRL_11:
	case ARIZONA_AIF1_FRAME_CTRL_12:
	case ARIZONA_AIF1_FRAME_CTRL_13:
	case ARIZONA_AIF1_FRAME_CTRL_14:
	case ARIZONA_AIF1_FRAME_CTRL_15:
	case ARIZONA_AIF1_FRAME_CTRL_16:
	case ARIZONA_AIF1_FRAME_CTRL_17:
	case ARIZONA_AIF1_FRAME_CTRL_18:
	case ARIZONA_AIF1_TX_ENABLES:
	case ARIZONA_AIF1_RX_ENABLES:
	case ARIZONA_AIF2_BCLK_CTRL:
	case ARIZONA_AIF2_TX_PIN_CTRL:
	case ARIZONA_AIF2_RX_PIN_CTRL:
	case ARIZONA_AIF2_RATE_CTRL:
	case ARIZONA_AIF2_FORMAT:
	case ARIZONA_AIF2_RX_BCLK_RATE:
	case ARIZONA_AIF2_FRAME_CTRL_1:
	case ARIZONA_AIF2_FRAME_CTRL_2:
	case ARIZONA_AIF2_FRAME_CTRL_3:
	case ARIZONA_AIF2_FRAME_CTRL_4:
	case ARIZONA_AIF2_FRAME_CTRL_5:
	case ARIZONA_AIF2_FRAME_CTRL_6:
	case ARIZONA_AIF2_FRAME_CTRL_7:
	case ARIZONA_AIF2_FRAME_CTRL_8:
	case ARIZONA_AIF2_FRAME_CTRL_11:
	case ARIZONA_AIF2_FRAME_CTRL_12:
	case ARIZONA_AIF2_FRAME_CTRL_13:
	case ARIZONA_AIF2_FRAME_CTRL_14:
	case ARIZONA_AIF2_FRAME_CTRL_15:
	case ARIZONA_AIF2_FRAME_CTRL_16:
	case ARIZONA_AIF2_TX_ENABLES:
	case ARIZONA_AIF2_RX_ENABLES:
	case ARIZONA_AIF3_BCLK_CTRL:
	case ARIZONA_AIF3_TX_PIN_CTRL:
	case ARIZONA_AIF3_RX_PIN_CTRL:
	case ARIZONA_AIF3_RATE_CTRL:
	case ARIZONA_AIF3_FORMAT:
	case ARIZONA_AIF3_RX_BCLK_RATE:
	case ARIZONA_AIF3_FRAME_CTRL_1:
	case ARIZONA_AIF3_FRAME_CTRL_2:
	case ARIZONA_AIF3_FRAME_CTRL_3:
	case ARIZONA_AIF3_FRAME_CTRL_4:
	case ARIZONA_AIF3_FRAME_CTRL_11:
	case ARIZONA_AIF3_FRAME_CTRL_12:
	case ARIZONA_AIF3_TX_ENABLES:
	case ARIZONA_AIF3_RX_ENABLES:
	case ARIZONA_PWM1MIX_INPUT_1_SOURCE:
	case ARIZONA_PWM1MIX_INPUT_1_VOLUME:
	case ARIZONA_PWM1MIX_INPUT_2_SOURCE:
	case ARIZONA_PWM1MIX_INPUT_2_VOLUME:
	case ARIZONA_PWM1MIX_INPUT_3_SOURCE:
	case ARIZONA_PWM1MIX_INPUT_3_VOLUME:
	case ARIZONA_PWM1MIX_INPUT_4_SOURCE:
	case ARIZONA_PWM1MIX_INPUT_4_VOLUME:
	case ARIZONA_PWM2MIX_INPUT_1_SOURCE:
	case ARIZONA_PWM2MIX_INPUT_1_VOLUME:
	case ARIZONA_PWM2MIX_INPUT_2_SOURCE:
	case ARIZONA_PWM2MIX_INPUT_2_VOLUME:
	case ARIZONA_PWM2MIX_INPUT_3_SOURCE:
	case ARIZONA_PWM2MIX_INPUT_3_VOLUME:
	case ARIZONA_PWM2MIX_INPUT_4_SOURCE:
	case ARIZONA_PWM2MIX_INPUT_4_VOLUME:
	case ARIZONA_OUT1LMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT1LMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT1LMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT1LMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT1LMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT1LMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT1LMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT1LMIX_INPUT_4_VOLUME:
	case ARIZONA_OUT1RMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT1RMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT1RMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT1RMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT1RMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT1RMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT1RMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT1RMIX_INPUT_4_VOLUME:
	case ARIZONA_OUT4LMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT4LMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT4LMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT4LMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT4LMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT4LMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT4LMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT4LMIX_INPUT_4_VOLUME:
	case ARIZONA_AIF1TX1MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF1TX1MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF1TX1MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF1TX1MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF1TX1MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF1TX1MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF1TX1MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF1TX1MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF1TX2MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF1TX2MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF1TX2MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF1TX2MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF1TX2MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF1TX2MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF1TX2MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF1TX2MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF1TX3MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF1TX3MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF1TX3MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF1TX3MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF1TX3MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF1TX3MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF1TX3MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF1TX3MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF1TX4MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF1TX4MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF1TX4MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF1TX4MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF1TX4MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF1TX4MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF1TX4MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF1TX4MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF1TX5MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF1TX5MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF1TX5MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF1TX5MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF1TX5MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF1TX5MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF1TX5MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF1TX5MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF1TX6MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF1TX6MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF1TX6MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF1TX6MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF1TX6MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF1TX6MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF1TX6MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF1TX6MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF1TX7MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF1TX7MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF1TX7MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF1TX7MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF1TX7MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF1TX7MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF1TX7MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF1TX7MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF1TX8MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF1TX8MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF1TX8MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF1TX8MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF1TX8MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF1TX8MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF1TX8MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF1TX8MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF2TX1MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF2TX1MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF2TX1MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF2TX1MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF2TX1MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF2TX1MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF2TX1MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF2TX1MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF2TX2MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF2TX2MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF2TX2MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF2TX2MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF2TX2MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF2TX2MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF2TX2MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF2TX2MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF2TX3MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF2TX3MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF2TX3MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF2TX3MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF2TX3MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF2TX3MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF2TX3MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF2TX3MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF2TX4MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF2TX4MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF2TX4MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF2TX4MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF2TX4MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF2TX4MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF2TX4MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF2TX4MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF2TX5MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF2TX5MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF2TX5MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF2TX5MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF2TX5MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF2TX5MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF2TX5MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF2TX5MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF2TX6MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF2TX6MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF2TX6MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF2TX6MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF2TX6MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF2TX6MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF2TX6MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF2TX6MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF3TX1MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF3TX1MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF3TX1MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF3TX1MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF3TX1MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF3TX1MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF3TX1MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF3TX1MIX_INPUT_4_VOLUME:
	case ARIZONA_AIF3TX2MIX_INPUT_1_SOURCE:
	case ARIZONA_AIF3TX2MIX_INPUT_1_VOLUME:
	case ARIZONA_AIF3TX2MIX_INPUT_2_SOURCE:
	case ARIZONA_AIF3TX2MIX_INPUT_2_VOLUME:
	case ARIZONA_AIF3TX2MIX_INPUT_3_SOURCE:
	case ARIZONA_AIF3TX2MIX_INPUT_3_VOLUME:
	case ARIZONA_AIF3TX2MIX_INPUT_4_SOURCE:
	case ARIZONA_AIF3TX2MIX_INPUT_4_VOLUME:
	case ARIZONA_EQ1MIX_INPUT_1_SOURCE:
	case ARIZONA_EQ1MIX_INPUT_1_VOLUME:
	case ARIZONA_EQ1MIX_INPUT_2_SOURCE:
	case ARIZONA_EQ1MIX_INPUT_2_VOLUME:
	case ARIZONA_EQ1MIX_INPUT_3_SOURCE:
	case ARIZONA_EQ1MIX_INPUT_3_VOLUME:
	case ARIZONA_EQ1MIX_INPUT_4_SOURCE:
	case ARIZONA_EQ1MIX_INPUT_4_VOLUME:
	case ARIZONA_EQ2MIX_INPUT_1_SOURCE:
	case ARIZONA_EQ2MIX_INPUT_1_VOLUME:
	case ARIZONA_EQ2MIX_INPUT_2_SOURCE:
	case ARIZONA_EQ2MIX_INPUT_2_VOLUME:
	case ARIZONA_EQ2MIX_INPUT_3_SOURCE:
	case ARIZONA_EQ2MIX_INPUT_3_VOLUME:
	case ARIZONA_EQ2MIX_INPUT_4_SOURCE:
	case ARIZONA_EQ2MIX_INPUT_4_VOLUME:
	case ARIZONA_DRC1LMIX_INPUT_1_SOURCE:
	case ARIZONA_DRC1LMIX_INPUT_1_VOLUME:
	case ARIZONA_DRC1LMIX_INPUT_2_SOURCE:
	case ARIZONA_DRC1LMIX_INPUT_2_VOLUME:
	case ARIZONA_DRC1LMIX_INPUT_3_SOURCE:
	case ARIZONA_DRC1LMIX_INPUT_3_VOLUME:
	case ARIZONA_DRC1LMIX_INPUT_4_SOURCE:
	case ARIZONA_DRC1LMIX_INPUT_4_VOLUME:
	case ARIZONA_DRC1RMIX_INPUT_1_SOURCE:
	case ARIZONA_DRC1RMIX_INPUT_1_VOLUME:
	case ARIZONA_DRC1RMIX_INPUT_2_SOURCE:
	case ARIZONA_DRC1RMIX_INPUT_2_VOLUME:
	case ARIZONA_DRC1RMIX_INPUT_3_SOURCE:
	case ARIZONA_DRC1RMIX_INPUT_3_VOLUME:
	case ARIZONA_DRC1RMIX_INPUT_4_SOURCE:
	case ARIZONA_DRC1RMIX_INPUT_4_VOLUME:
	case ARIZONA_DRC2LMIX_INPUT_1_SOURCE:
	case ARIZONA_DRC2LMIX_INPUT_1_VOLUME:
	case ARIZONA_DRC2LMIX_INPUT_2_SOURCE:
	case ARIZONA_DRC2LMIX_INPUT_2_VOLUME:
	case ARIZONA_DRC2LMIX_INPUT_3_SOURCE:
	case ARIZONA_DRC2LMIX_INPUT_3_VOLUME:
	case ARIZONA_DRC2LMIX_INPUT_4_SOURCE:
	case ARIZONA_DRC2LMIX_INPUT_4_VOLUME:
	case ARIZONA_DRC2RMIX_INPUT_1_SOURCE:
	case ARIZONA_DRC2RMIX_INPUT_1_VOLUME:
	case ARIZONA_DRC2RMIX_INPUT_2_SOURCE:
	case ARIZONA_DRC2RMIX_INPUT_2_VOLUME:
	case ARIZONA_DRC2RMIX_INPUT_3_SOURCE:
	case ARIZONA_DRC2RMIX_INPUT_3_VOLUME:
	case ARIZONA_DRC2RMIX_INPUT_4_SOURCE:
	case ARIZONA_DRC2RMIX_INPUT_4_VOLUME:
	case ARIZONA_HPLP1MIX_INPUT_1_SOURCE:
	case ARIZONA_HPLP1MIX_INPUT_1_VOLUME:
	case ARIZONA_HPLP1MIX_INPUT_2_SOURCE:
	case ARIZONA_HPLP1MIX_INPUT_2_VOLUME:
	case ARIZONA_HPLP1MIX_INPUT_3_SOURCE:
	case ARIZONA_HPLP1MIX_INPUT_3_VOLUME:
	case ARIZONA_HPLP1MIX_INPUT_4_SOURCE:
	case ARIZONA_HPLP1MIX_INPUT_4_VOLUME:
	case ARIZONA_HPLP2MIX_INPUT_1_SOURCE:
	case ARIZONA_HPLP2MIX_INPUT_1_VOLUME:
	case ARIZONA_HPLP2MIX_INPUT_2_SOURCE:
	case ARIZONA_HPLP2MIX_INPUT_2_VOLUME:
	case ARIZONA_HPLP2MIX_INPUT_3_SOURCE:
	case ARIZONA_HPLP2MIX_INPUT_3_VOLUME:
	case ARIZONA_HPLP2MIX_INPUT_4_SOURCE:
	case ARIZONA_HPLP2MIX_INPUT_4_VOLUME:
	case ARIZONA_HPLP3MIX_INPUT_1_SOURCE:
	case ARIZONA_HPLP3MIX_INPUT_1_VOLUME:
	case ARIZONA_HPLP3MIX_INPUT_2_SOURCE:
	case ARIZONA_HPLP3MIX_INPUT_2_VOLUME:
	case ARIZONA_HPLP3MIX_INPUT_3_SOURCE:
	case ARIZONA_HPLP3MIX_INPUT_3_VOLUME:
	case ARIZONA_HPLP3MIX_INPUT_4_SOURCE:
	case ARIZONA_HPLP3MIX_INPUT_4_VOLUME:
	case ARIZONA_HPLP4MIX_INPUT_1_SOURCE:
	case ARIZONA_HPLP4MIX_INPUT_1_VOLUME:
	case ARIZONA_HPLP4MIX_INPUT_2_SOURCE:
	case ARIZONA_HPLP4MIX_INPUT_2_VOLUME:
	case ARIZONA_HPLP4MIX_INPUT_3_SOURCE:
	case ARIZONA_HPLP4MIX_INPUT_3_VOLUME:
	case ARIZONA_HPLP4MIX_INPUT_4_SOURCE:
	case ARIZONA_HPLP4MIX_INPUT_4_VOLUME:
	case ARIZONA_DSP2LMIX_INPUT_1_SOURCE:
	case ARIZONA_DSP2LMIX_INPUT_1_VOLUME:
	case ARIZONA_DSP2LMIX_INPUT_2_SOURCE:
	case ARIZONA_DSP2LMIX_INPUT_2_VOLUME:
	case ARIZONA_DSP2LMIX_INPUT_3_SOURCE:
	case ARIZONA_DSP2LMIX_INPUT_3_VOLUME:
	case ARIZONA_DSP2LMIX_INPUT_4_SOURCE:
	case ARIZONA_DSP2LMIX_INPUT_4_VOLUME:
	case ARIZONA_DSP2RMIX_INPUT_1_SOURCE:
	case ARIZONA_DSP2RMIX_INPUT_1_VOLUME:
	case ARIZONA_DSP2RMIX_INPUT_2_SOURCE:
	case ARIZONA_DSP2RMIX_INPUT_2_VOLUME:
	case ARIZONA_DSP2RMIX_INPUT_3_SOURCE:
	case ARIZONA_DSP2RMIX_INPUT_3_VOLUME:
	case ARIZONA_DSP2RMIX_INPUT_4_SOURCE:
	case ARIZONA_DSP2RMIX_INPUT_4_VOLUME:
	case ARIZONA_DSP2AUX1MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP2AUX2MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP2AUX3MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP2AUX4MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP2AUX5MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP2AUX6MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP3LMIX_INPUT_1_SOURCE:
	case ARIZONA_DSP3LMIX_INPUT_1_VOLUME:
	case ARIZONA_DSP3LMIX_INPUT_2_SOURCE:
	case ARIZONA_DSP3LMIX_INPUT_2_VOLUME:
	case ARIZONA_DSP3LMIX_INPUT_3_SOURCE:
	case ARIZONA_DSP3LMIX_INPUT_3_VOLUME:
	case ARIZONA_DSP3LMIX_INPUT_4_SOURCE:
	case ARIZONA_DSP3LMIX_INPUT_4_VOLUME:
	case ARIZONA_DSP3RMIX_INPUT_1_SOURCE:
	case ARIZONA_DSP3RMIX_INPUT_1_VOLUME:
	case ARIZONA_DSP3RMIX_INPUT_2_SOURCE:
	case ARIZONA_DSP3RMIX_INPUT_2_VOLUME:
	case ARIZONA_DSP3RMIX_INPUT_3_SOURCE:
	case ARIZONA_DSP3RMIX_INPUT_3_VOLUME:
	case ARIZONA_DSP3RMIX_INPUT_4_SOURCE:
	case ARIZONA_DSP3RMIX_INPUT_4_VOLUME:
	case ARIZONA_DSP3AUX1MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP3AUX2MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP3AUX3MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP3AUX4MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP3AUX5MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP3AUX6MIX_INPUT_1_SOURCE:
	case ARIZONA_ASRC1LMIX_INPUT_1_SOURCE:
	case ARIZONA_ASRC1RMIX_INPUT_1_SOURCE:
	case ARIZONA_ASRC2LMIX_INPUT_1_SOURCE:
	case ARIZONA_ASRC2RMIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1DEC1MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1DEC2MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1DEC3MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1DEC4MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1INT1MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1INT2MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1INT3MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1INT4MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2DEC1MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2DEC2MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2DEC3MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2DEC4MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2INT1MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2INT2MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2INT3MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2INT4MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC3DEC1MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC3DEC2MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC3DEC3MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC3DEC4MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC3INT1MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC3INT2MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC3INT3MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC3INT4MIX_INPUT_1_SOURCE:
	case ARIZONA_GPIO1_CTRL:
	case ARIZONA_GPIO2_CTRL:
	case ARIZONA_IRQ_CTRL_1:
	case ARIZONA_GPIO_DEBOUNCE_CONFIG:
	case ARIZONA_MISC_PAD_CTRL_1:
	case ARIZONA_MISC_PAD_CTRL_2:
	case ARIZONA_MISC_PAD_CTRL_3:
	case ARIZONA_MISC_PAD_CTRL_4:
	case ARIZONA_MISC_PAD_CTRL_5:
	case ARIZONA_MISC_PAD_CTRL_6:
	case ARIZONA_MISC_PAD_CTRL_7:
	case ARIZONA_MISC_PAD_CTRL_9:
	case ARIZONA_MISC_PAD_CTRL_10:
	case ARIZONA_MISC_PAD_CTRL_11:
	case ARIZONA_MISC_PAD_CTRL_12:
	case ARIZONA_MISC_PAD_CTRL_13:
	case ARIZONA_MISC_PAD_CTRL_14:
	case ARIZONA_MISC_PAD_CTRL_16:
	case ARIZONA_INTERRUPT_STATUS_1:
	case ARIZONA_INTERRUPT_STATUS_2:
	case ARIZONA_INTERRUPT_STATUS_3:
	case ARIZONA_INTERRUPT_STATUS_4:
	case ARIZONA_INTERRUPT_STATUS_5:
	case ARIZONA_INTERRUPT_STATUS_6:
	case ARIZONA_INTERRUPT_STATUS_1_MASK:
	case ARIZONA_INTERRUPT_STATUS_2_MASK:
	case ARIZONA_INTERRUPT_STATUS_3_MASK:
	case ARIZONA_INTERRUPT_STATUS_4_MASK:
	case ARIZONA_INTERRUPT_STATUS_5_MASK:
	case ARIZONA_INTERRUPT_STATUS_6_MASK:
	case ARIZONA_INTERRUPT_CONTROL:
	case ARIZONA_IRQ2_STATUS_1:
	case ARIZONA_IRQ2_STATUS_2:
	case ARIZONA_IRQ2_STATUS_3:
	case ARIZONA_IRQ2_STATUS_4:
	case ARIZONA_IRQ2_STATUS_5:
	case ARIZONA_IRQ2_STATUS_6:
	case ARIZONA_IRQ2_STATUS_1_MASK:
	case ARIZONA_IRQ2_STATUS_2_MASK:
	case ARIZONA_IRQ2_STATUS_3_MASK:
	case ARIZONA_IRQ2_STATUS_4_MASK:
	case ARIZONA_IRQ2_STATUS_5_MASK:
	case ARIZONA_IRQ2_STATUS_6_MASK:
	case ARIZONA_IRQ2_CONTROL:
	case ARIZONA_INTERRUPT_RAW_STATUS_2:
	case ARIZONA_INTERRUPT_RAW_STATUS_3:
	case ARIZONA_INTERRUPT_RAW_STATUS_4:
	case ARIZONA_INTERRUPT_RAW_STATUS_5:
	case ARIZONA_INTERRUPT_RAW_STATUS_6:
	case ARIZONA_INTERRUPT_RAW_STATUS_7:
	case ARIZONA_INTERRUPT_RAW_STATUS_8:
	case ARIZONA_INTERRUPT_RAW_STATUS_9:
	case ARIZONA_IRQ_PIN_STATUS:
	case ARIZONA_FX_CTRL1:
	case ARIZONA_FX_CTRL2:
	case ARIZONA_EQ1_1:
	case ARIZONA_EQ1_2:
	case ARIZONA_EQ1_3:
	case ARIZONA_EQ1_4:
	case ARIZONA_EQ1_5:
	case ARIZONA_EQ1_6:
	case ARIZONA_EQ1_7:
	case ARIZONA_EQ1_8:
	case ARIZONA_EQ1_9:
	case ARIZONA_EQ1_10:
	case ARIZONA_EQ1_11:
	case ARIZONA_EQ1_12:
	case ARIZONA_EQ1_13:
	case ARIZONA_EQ1_14:
	case ARIZONA_EQ1_15:
	case ARIZONA_EQ1_16:
	case ARIZONA_EQ1_17:
	case ARIZONA_EQ1_18:
	case ARIZONA_EQ1_19:
	case ARIZONA_EQ1_20:
	case ARIZONA_EQ1_21:
	case ARIZONA_EQ2_1:
	case ARIZONA_EQ2_2:
	case ARIZONA_EQ2_3:
	case ARIZONA_EQ2_4:
	case ARIZONA_EQ2_5:
	case ARIZONA_EQ2_6:
	case ARIZONA_EQ2_7:
	case ARIZONA_EQ2_8:
	case ARIZONA_EQ2_9:
	case ARIZONA_EQ2_10:
	case ARIZONA_EQ2_11:
	case ARIZONA_EQ2_12:
	case ARIZONA_EQ2_13:
	case ARIZONA_EQ2_14:
	case ARIZONA_EQ2_15:
	case ARIZONA_EQ2_16:
	case ARIZONA_EQ2_17:
	case ARIZONA_EQ2_18:
	case ARIZONA_EQ2_19:
	case ARIZONA_EQ2_20:
	case ARIZONA_EQ2_21:
	case ARIZONA_DRC1_CTRL1:
	case ARIZONA_DRC1_CTRL2:
	case ARIZONA_DRC1_CTRL3:
	case ARIZONA_DRC1_CTRL4:
	case ARIZONA_DRC1_CTRL5:
	case ARIZONA_DRC2_CTRL1:
	case ARIZONA_DRC2_CTRL2:
	case ARIZONA_DRC2_CTRL3:
	case ARIZONA_DRC2_CTRL4:
	case ARIZONA_DRC2_CTRL5:
	case ARIZONA_HPLPF1_1:
	case ARIZONA_HPLPF1_2:
	case ARIZONA_HPLPF2_1:
	case ARIZONA_HPLPF2_2:
	case ARIZONA_HPLPF3_1:
	case ARIZONA_HPLPF3_2:
	case ARIZONA_HPLPF4_1:
	case ARIZONA_HPLPF4_2:
	case ARIZONA_ASRC_ENABLE:
	case ARIZONA_ASRC_STATUS:
	case ARIZONA_ASRC_RATE1:
	case ARIZONA_ASRC_RATE2:
	case ARIZONA_ISRC_1_CTRL_1:
	case ARIZONA_ISRC_1_CTRL_2:
	case ARIZONA_ISRC_1_CTRL_3:
	case ARIZONA_ISRC_2_CTRL_1:
	case ARIZONA_ISRC_2_CTRL_2:
	case ARIZONA_ISRC_2_CTRL_3:
	case ARIZONA_ISRC_3_CTRL_1:
	case ARIZONA_ISRC_3_CTRL_2:
	case ARIZONA_ISRC_3_CTRL_3:
	case ARIZONA_DSP2_CONTROL_1:
	case ARIZONA_DSP2_CLOCKING_1:
	case ARIZONA_DSP2_STATUS_1:
	case ARIZONA_DSP2_STATUS_2:
	case ARIZONA_DSP2_STATUS_3:
	case ARIZONA_DSP2_STATUS_4:
	case ARIZONA_DSP2_WDMA_BUFFER_1:
	case ARIZONA_DSP2_WDMA_BUFFER_2:
	case ARIZONA_DSP2_WDMA_BUFFER_3:
	case ARIZONA_DSP2_WDMA_BUFFER_4:
	case ARIZONA_DSP2_WDMA_BUFFER_5:
	case ARIZONA_DSP2_WDMA_BUFFER_6:
	case ARIZONA_DSP2_WDMA_BUFFER_7:
	case ARIZONA_DSP2_WDMA_BUFFER_8:
	case ARIZONA_DSP2_RDMA_BUFFER_1:
	case ARIZONA_DSP2_RDMA_BUFFER_2:
	case ARIZONA_DSP2_RDMA_BUFFER_3:
	case ARIZONA_DSP2_RDMA_BUFFER_4:
	case ARIZONA_DSP2_RDMA_BUFFER_5:
	case ARIZONA_DSP2_RDMA_BUFFER_6:
	case ARIZONA_DSP2_WDMA_CONFIG_1:
	case ARIZONA_DSP2_WDMA_CONFIG_2:
	case ARIZONA_DSP2_WDMA_OFFSET_1:
	case ARIZONA_DSP2_RDMA_CONFIG_1:
	case ARIZONA_DSP2_RDMA_OFFSET_1:
	case ARIZONA_DSP2_EXTERNAL_START_SELECT_1:
	case ARIZONA_DSP2_SCRATCH_0:
	case ARIZONA_DSP2_SCRATCH_1:
	case ARIZONA_DSP2_SCRATCH_2:
	case ARIZONA_DSP2_SCRATCH_3:
	case ARIZONA_DSP3_CONTROL_1:
	case ARIZONA_DSP3_CLOCKING_1:
	case ARIZONA_DSP3_STATUS_1:
	case ARIZONA_DSP3_STATUS_2:
	case ARIZONA_DSP3_STATUS_3:
	case ARIZONA_DSP3_STATUS_4:
	case ARIZONA_DSP3_WDMA_BUFFER_1:
	case ARIZONA_DSP3_WDMA_BUFFER_2:
	case ARIZONA_DSP3_WDMA_BUFFER_3:
	case ARIZONA_DSP3_WDMA_BUFFER_4:
	case ARIZONA_DSP3_WDMA_BUFFER_5:
	case ARIZONA_DSP3_WDMA_BUFFER_6:
	case ARIZONA_DSP3_WDMA_BUFFER_7:
	case ARIZONA_DSP3_WDMA_BUFFER_8:
	case ARIZONA_DSP3_RDMA_BUFFER_1:
	case ARIZONA_DSP3_RDMA_BUFFER_2:
	case ARIZONA_DSP3_RDMA_BUFFER_3:
	case ARIZONA_DSP3_RDMA_BUFFER_4:
	case ARIZONA_DSP3_RDMA_BUFFER_5:
	case ARIZONA_DSP3_RDMA_BUFFER_6:
	case ARIZONA_DSP3_WDMA_CONFIG_1:
	case ARIZONA_DSP3_WDMA_CONFIG_2:
	case ARIZONA_DSP3_WDMA_OFFSET_1:
	case ARIZONA_DSP3_RDMA_CONFIG_1:
	case ARIZONA_DSP3_RDMA_OFFSET_1:
	case ARIZONA_DSP3_EXTERNAL_START_SELECT_1:
	case ARIZONA_DSP3_SCRATCH_0:
	case ARIZONA_DSP3_SCRATCH_1:
	case ARIZONA_DSP3_SCRATCH_2:
	case ARIZONA_DSP3_SCRATCH_3:
		return true;
	default:
		return cs47l24_is_adsp_memory(reg);
	}
}

static bool cs47l24_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ARIZONA_SOFTWARE_RESET:
	case ARIZONA_DEVICE_REVISION:
	case ARIZONA_WRITE_SEQUENCER_CTRL_0:
	case ARIZONA_WRITE_SEQUENCER_CTRL_1:
	case ARIZONA_WRITE_SEQUENCER_CTRL_2:
	case ARIZONA_HAPTICS_STATUS:
	case ARIZONA_SAMPLE_RATE_1_STATUS:
	case ARIZONA_SAMPLE_RATE_2_STATUS:
	case ARIZONA_SAMPLE_RATE_3_STATUS:
	case ARIZONA_ASYNC_SAMPLE_RATE_1_STATUS:
	case ARIZONA_ASYNC_SAMPLE_RATE_2_STATUS:
	case ARIZONA_HP_CTRL_1L:
	case ARIZONA_HP_CTRL_1R:
	case ARIZONA_INPUT_ENABLES_STATUS:
	case ARIZONA_OUTPUT_STATUS_1:
	case ARIZONA_RAW_OUTPUT_STATUS_1:
	case ARIZONA_INTERRUPT_STATUS_1:
	case ARIZONA_INTERRUPT_STATUS_2:
	case ARIZONA_INTERRUPT_STATUS_3:
	case ARIZONA_INTERRUPT_STATUS_4:
	case ARIZONA_INTERRUPT_STATUS_5:
	case ARIZONA_INTERRUPT_STATUS_6:
	case ARIZONA_IRQ2_STATUS_1:
	case ARIZONA_IRQ2_STATUS_2:
	case ARIZONA_IRQ2_STATUS_3:
	case ARIZONA_IRQ2_STATUS_4:
	case ARIZONA_IRQ2_STATUS_5:
	case ARIZONA_IRQ2_STATUS_6:
	case ARIZONA_INTERRUPT_RAW_STATUS_2:
	case ARIZONA_INTERRUPT_RAW_STATUS_3:
	case ARIZONA_INTERRUPT_RAW_STATUS_4:
	case ARIZONA_INTERRUPT_RAW_STATUS_5:
	case ARIZONA_INTERRUPT_RAW_STATUS_6:
	case ARIZONA_INTERRUPT_RAW_STATUS_7:
	case ARIZONA_INTERRUPT_RAW_STATUS_8:
	case ARIZONA_INTERRUPT_RAW_STATUS_9:
	case ARIZONA_IRQ_PIN_STATUS:
	case ARIZONA_FX_CTRL2:
	case ARIZONA_ASRC_STATUS:
	case ARIZONA_DSP2_STATUS_1:
	case ARIZONA_DSP2_STATUS_2:
	case ARIZONA_DSP2_STATUS_3:
	case ARIZONA_DSP2_STATUS_4:
	case ARIZONA_DSP2_WDMA_BUFFER_1:
	case ARIZONA_DSP2_WDMA_BUFFER_2:
	case ARIZONA_DSP2_WDMA_BUFFER_3:
	case ARIZONA_DSP2_WDMA_BUFFER_4:
	case ARIZONA_DSP2_WDMA_BUFFER_5:
	case ARIZONA_DSP2_WDMA_BUFFER_6:
	case ARIZONA_DSP2_WDMA_BUFFER_7:
	case ARIZONA_DSP2_WDMA_BUFFER_8:
	case ARIZONA_DSP2_RDMA_BUFFER_1:
	case ARIZONA_DSP2_RDMA_BUFFER_2:
	case ARIZONA_DSP2_RDMA_BUFFER_3:
	case ARIZONA_DSP2_RDMA_BUFFER_4:
	case ARIZONA_DSP2_RDMA_BUFFER_5:
	case ARIZONA_DSP2_RDMA_BUFFER_6:
	case ARIZONA_DSP2_WDMA_CONFIG_1:
	case ARIZONA_DSP2_WDMA_CONFIG_2:
	case ARIZONA_DSP2_WDMA_OFFSET_1:
	case ARIZONA_DSP2_RDMA_CONFIG_1:
	case ARIZONA_DSP2_RDMA_OFFSET_1:
	case ARIZONA_DSP2_EXTERNAL_START_SELECT_1:
	case ARIZONA_DSP2_SCRATCH_0:
	case ARIZONA_DSP2_SCRATCH_1:
	case ARIZONA_DSP2_SCRATCH_2:
	case ARIZONA_DSP2_SCRATCH_3:
	case ARIZONA_DSP2_CLOCKING_1:
	case ARIZONA_DSP3_STATUS_1:
	case ARIZONA_DSP3_STATUS_2:
	case ARIZONA_DSP3_STATUS_3:
	case ARIZONA_DSP3_STATUS_4:
	case ARIZONA_DSP3_WDMA_BUFFER_1:
	case ARIZONA_DSP3_WDMA_BUFFER_2:
	case ARIZONA_DSP3_WDMA_BUFFER_3:
	case ARIZONA_DSP3_WDMA_BUFFER_4:
	case ARIZONA_DSP3_WDMA_BUFFER_5:
	case ARIZONA_DSP3_WDMA_BUFFER_6:
	case ARIZONA_DSP3_WDMA_BUFFER_7:
	case ARIZONA_DSP3_WDMA_BUFFER_8:
	case ARIZONA_DSP3_RDMA_BUFFER_1:
	case ARIZONA_DSP3_RDMA_BUFFER_2:
	case ARIZONA_DSP3_RDMA_BUFFER_3:
	case ARIZONA_DSP3_RDMA_BUFFER_4:
	case ARIZONA_DSP3_RDMA_BUFFER_5:
	case ARIZONA_DSP3_RDMA_BUFFER_6:
	case ARIZONA_DSP3_WDMA_CONFIG_1:
	case ARIZONA_DSP3_WDMA_CONFIG_2:
	case ARIZONA_DSP3_WDMA_OFFSET_1:
	case ARIZONA_DSP3_RDMA_CONFIG_1:
	case ARIZONA_DSP3_RDMA_OFFSET_1:
	case ARIZONA_DSP3_EXTERNAL_START_SELECT_1:
	case ARIZONA_DSP3_SCRATCH_0:
	case ARIZONA_DSP3_SCRATCH_1:
	case ARIZONA_DSP3_SCRATCH_2:
	case ARIZONA_DSP3_SCRATCH_3:
	case ARIZONA_DSP3_CLOCKING_1:
		return true;
	default:
		return cs47l24_is_adsp_memory(reg);
	}
}

#define CS47L24_MAX_REGISTER 0x3b3fff

const struct regmap_config cs47l24_spi_regmap = {
	.reg_bits = 32,
	.pad_bits = 16,
	.val_bits = 16,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = CS47L24_MAX_REGISTER,
	.readable_reg = cs47l24_readable_register,
	.volatile_reg = cs47l24_volatile_register,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = cs47l24_reg_default,
	.num_reg_defaults = ARRAY_SIZE(cs47l24_reg_default),
};
EXPORT_SYMBOL_GPL(cs47l24_spi_regmap);

