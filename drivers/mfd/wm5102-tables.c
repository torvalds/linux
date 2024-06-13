// SPDX-License-Identifier: GPL-2.0-only
/*
 * wm5102-tables.c  --  WM5102 data tables
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 */

#include <linux/device.h>
#include <linux/module.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/registers.h>

#include "arizona.h"

#define WM5102_NUM_AOD_ISR 2
#define WM5102_NUM_ISR 5

static const struct reg_sequence wm5102_reva_patch[] = {
	{ 0x80, 0x0003 },
	{ 0x221, 0x0090 },
	{ 0x211, 0x0014 },
	{ 0x212, 0x0000 },
	{ 0x214, 0x000C },
	{ 0x171, 0x0002 },
	{ 0x171, 0x0000 },
	{ 0x461, 0x8000 },
	{ 0x463, 0x50F0 },
	{ 0x465, 0x4820 },
	{ 0x467, 0x4040 },
	{ 0x469, 0x3940 },
	{ 0x46B, 0x3310 },
	{ 0x46D, 0x2D80 },
	{ 0x46F, 0x2890 },
	{ 0x471, 0x1990 },
	{ 0x473, 0x1450 },
	{ 0x475, 0x1020 },
	{ 0x477, 0x0CD0 },
	{ 0x479, 0x0A30 },
	{ 0x47B, 0x0810 },
	{ 0x47D, 0x0510 },
	{ 0x4D1, 0x017F },
	{ 0x500, 0x000D },
	{ 0x507, 0x1820 },
	{ 0x508, 0x1820 },
	{ 0x540, 0x000D },
	{ 0x547, 0x1820 },
	{ 0x548, 0x1820 },
	{ 0x580, 0x000D },
	{ 0x587, 0x1820 },
	{ 0x588, 0x1820 },
	{ 0x80, 0x0000 },
};

static const struct reg_sequence wm5102_revb_patch[] = {
	{ 0x19, 0x0001 },
	{ 0x80, 0x0003 },
	{ 0x081, 0xE022 },
	{ 0x410, 0x6080 },
	{ 0x418, 0xa080 },
	{ 0x420, 0xa080 },
	{ 0x428, 0xe000 },
	{ 0x442, 0x3F0A },
	{ 0x443, 0xDC1F },
	{ 0x4B0, 0x0066 },
	{ 0x458, 0x000b },
	{ 0x212, 0x0000 },
	{ 0x171, 0x0000 },
	{ 0x35E, 0x000C },
	{ 0x2D4, 0x0000 },
	{ 0x4DC, 0x0900 },
	{ 0x80, 0x0000 },
};

/* We use a function so we can use ARRAY_SIZE() */
int wm5102_patch(struct arizona *arizona)
{
	const struct reg_sequence *wm5102_patch;
	int patch_size;

	switch (arizona->rev) {
	case 0:
		wm5102_patch = wm5102_reva_patch;
		patch_size = ARRAY_SIZE(wm5102_reva_patch);
		break;
	default:
		wm5102_patch = wm5102_revb_patch;
		patch_size = ARRAY_SIZE(wm5102_revb_patch);
	}

	return regmap_multi_reg_write_bypassed(arizona->regmap,
					       wm5102_patch,
					       patch_size);
}

static const struct regmap_irq wm5102_aod_irqs[ARIZONA_NUM_IRQ] = {
	[ARIZONA_IRQ_MICD_CLAMP_FALL] = {
		.mask = ARIZONA_MICD_CLAMP_FALL_EINT1
	},
	[ARIZONA_IRQ_MICD_CLAMP_RISE] = {
		.mask = ARIZONA_MICD_CLAMP_RISE_EINT1
	},
	[ARIZONA_IRQ_GP5_FALL] = { .mask = ARIZONA_GP5_FALL_EINT1 },
	[ARIZONA_IRQ_GP5_RISE] = { .mask = ARIZONA_GP5_RISE_EINT1 },
	[ARIZONA_IRQ_JD_FALL] = { .mask = ARIZONA_JD1_FALL_EINT1 },
	[ARIZONA_IRQ_JD_RISE] = { .mask = ARIZONA_JD1_RISE_EINT1 },
};

const struct regmap_irq_chip wm5102_aod = {
	.name = "wm5102 AOD",
	.status_base = ARIZONA_AOD_IRQ1,
	.mask_base = ARIZONA_AOD_IRQ_MASK_IRQ1,
	.ack_base = ARIZONA_AOD_IRQ1,
	.wake_base = ARIZONA_WAKE_CONTROL,
	.wake_invert = 1,
	.num_regs = 1,
	.irqs = wm5102_aod_irqs,
	.num_irqs = ARRAY_SIZE(wm5102_aod_irqs),
};

static const struct regmap_irq wm5102_irqs[ARIZONA_NUM_IRQ] = {
	[ARIZONA_IRQ_GP4] = { .reg_offset = 0, .mask = ARIZONA_GP4_EINT1 },
	[ARIZONA_IRQ_GP3] = { .reg_offset = 0, .mask = ARIZONA_GP3_EINT1 },
	[ARIZONA_IRQ_GP2] = { .reg_offset = 0, .mask = ARIZONA_GP2_EINT1 },
	[ARIZONA_IRQ_GP1] = { .reg_offset = 0, .mask = ARIZONA_GP1_EINT1 },

	[ARIZONA_IRQ_DSP1_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP1_RAM_RDY_EINT1
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
	[ARIZONA_IRQ_HPDET] = {
		.reg_offset = 2, .mask = ARIZONA_HPDET_EINT1
	},
	[ARIZONA_IRQ_MICDET] = {
		.reg_offset = 2, .mask = ARIZONA_MICDET_EINT1
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

	[ARIZONA_IRQ_ASRC_CFG_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_ASRC_CFG_ERR_EINT1
	},
	[ARIZONA_IRQ_AIF3_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_AIF3_ERR_EINT1
	},
	[ARIZONA_IRQ_AIF2_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_AIF2_ERR_EINT1
	},
	[ARIZONA_IRQ_AIF1_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_AIF1_ERR_EINT1
	},
	[ARIZONA_IRQ_CTRLIF_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_CTRLIF_ERR_EINT1
	},
	[ARIZONA_IRQ_MIXER_DROPPED_SAMPLES] = {
		.reg_offset = 3, .mask = ARIZONA_MIXER_DROPPED_SAMPLE_EINT1
	},
	[ARIZONA_IRQ_ASYNC_CLK_ENA_LOW] = {
		.reg_offset = 3, .mask = ARIZONA_ASYNC_CLK_ENA_LOW_EINT1
	},
	[ARIZONA_IRQ_SYSCLK_ENA_LOW] = {
		.reg_offset = 3, .mask = ARIZONA_SYSCLK_ENA_LOW_EINT1
	},
	[ARIZONA_IRQ_ISRC1_CFG_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_ISRC1_CFG_ERR_EINT1
	},
	[ARIZONA_IRQ_ISRC2_CFG_ERR] = {
		.reg_offset = 3, .mask = ARIZONA_ISRC2_CFG_ERR_EINT1
	},

	[ARIZONA_IRQ_BOOT_DONE] = {
		.reg_offset = 4, .mask = ARIZONA_BOOT_DONE_EINT1
	},
	[ARIZONA_IRQ_DCS_DAC_DONE] = {
		.reg_offset = 4, .mask = ARIZONA_DCS_DAC_DONE_EINT1
	},
	[ARIZONA_IRQ_DCS_HP_DONE] = {
		.reg_offset = 4, .mask = ARIZONA_DCS_HP_DONE_EINT1
	},
	[ARIZONA_IRQ_FLL2_CLOCK_OK] = {
		.reg_offset = 4, .mask = ARIZONA_FLL2_CLOCK_OK_EINT1
	},
	[ARIZONA_IRQ_FLL1_CLOCK_OK] = {
		.reg_offset = 4, .mask = ARIZONA_FLL1_CLOCK_OK_EINT1
	},
};

const struct regmap_irq_chip wm5102_irq = {
	.name = "wm5102 IRQ",
	.status_base = ARIZONA_INTERRUPT_STATUS_1,
	.mask_base = ARIZONA_INTERRUPT_STATUS_1_MASK,
	.ack_base = ARIZONA_INTERRUPT_STATUS_1,
	.num_regs = 5,
	.irqs = wm5102_irqs,
	.num_irqs = ARRAY_SIZE(wm5102_irqs),
};

static const struct reg_default wm5102_reg_default[] = {
	{ 0x00000008, 0x0019 },   /* R8     - Ctrl IF SPI CFG 1 */
	{ 0x00000009, 0x0001 },   /* R9     - Ctrl IF I2C1 CFG 1 */
	{ 0x00000020, 0x0000 },   /* R32    - Tone Generator 1 */
	{ 0x00000021, 0x1000 },   /* R33    - Tone Generator 2 */
	{ 0x00000022, 0x0000 },   /* R34    - Tone Generator 3 */
	{ 0x00000023, 0x1000 },   /* R35    - Tone Generator 4 */
	{ 0x00000024, 0x0000 },   /* R36    - Tone Generator 5 */
	{ 0x00000030, 0x0000 },   /* R48    - PWM Drive 1 */
	{ 0x00000031, 0x0100 },   /* R49    - PWM Drive 2 */
	{ 0x00000032, 0x0100 },   /* R50    - PWM Drive 3 */
	{ 0x00000040, 0x0000 },   /* R64    - Wake control */
	{ 0x00000041, 0x0000 },   /* R65    - Sequence control */
	{ 0x00000061, 0x01FF },   /* R97    - Sample Rate Sequence Select 1 */
	{ 0x00000062, 0x01FF },   /* R98    - Sample Rate Sequence Select 2 */
	{ 0x00000063, 0x01FF },   /* R99    - Sample Rate Sequence Select 3 */
	{ 0x00000064, 0x01FF },   /* R100   - Sample Rate Sequence Select 4 */
	{ 0x00000066, 0x01FF },   /* R102   - Always On Triggers Sequence Select 1 */
	{ 0x00000067, 0x01FF },   /* R103   - Always On Triggers Sequence Select 2 */
	{ 0x00000068, 0x01FF },   /* R104   - Always On Triggers Sequence Select 3 */
	{ 0x00000069, 0x01FF },   /* R105   - Always On Triggers Sequence Select 4 */
	{ 0x0000006A, 0x01FF },   /* R106   - Always On Triggers Sequence Select 5 */
	{ 0x0000006B, 0x01FF },   /* R107   - Always On Triggers Sequence Select 6 */
	{ 0x00000070, 0x0000 },   /* R112   - Comfort Noise Generator */
	{ 0x00000090, 0x0000 },   /* R144   - Haptics Control 1 */
	{ 0x00000091, 0x7FFF },   /* R145   - Haptics Control 2 */
	{ 0x00000092, 0x0000 },   /* R146   - Haptics phase 1 intensity */
	{ 0x00000093, 0x0000 },   /* R147   - Haptics phase 1 duration */
	{ 0x00000094, 0x0000 },   /* R148   - Haptics phase 2 intensity */
	{ 0x00000095, 0x0000 },   /* R149   - Haptics phase 2 duration */
	{ 0x00000096, 0x0000 },   /* R150   - Haptics phase 3 intensity */
	{ 0x00000097, 0x0000 },   /* R151   - Haptics phase 3 duration */
	{ 0x00000100, 0x0002 },   /* R256   - Clock 32k 1 */
	{ 0x00000101, 0x0304 },   /* R257   - System Clock 1 */
	{ 0x00000102, 0x0011 },   /* R258   - Sample rate 1 */
	{ 0x00000103, 0x0011 },   /* R259   - Sample rate 2 */
	{ 0x00000104, 0x0011 },   /* R260   - Sample rate 3 */
	{ 0x00000112, 0x0305 },   /* R274   - Async clock 1 */
	{ 0x00000113, 0x0011 },   /* R275   - Async sample rate 1 */
	{ 0x00000114, 0x0011 },   /* R276   - Async sample rate 2 */
	{ 0x00000149, 0x0000 },   /* R329   - Output system clock */
	{ 0x0000014A, 0x0000 },   /* R330   - Output async clock */
	{ 0x00000152, 0x0000 },   /* R338   - Rate Estimator 1 */
	{ 0x00000153, 0x0000 },   /* R339   - Rate Estimator 2 */
	{ 0x00000154, 0x0000 },   /* R340   - Rate Estimator 3 */
	{ 0x00000155, 0x0000 },   /* R341   - Rate Estimator 4 */
	{ 0x00000156, 0x0000 },   /* R342   - Rate Estimator 5 */
	{ 0x00000161, 0x0000 },   /* R353   - Dynamic Frequency Scaling 1 */
	{ 0x00000171, 0x0000 },   /* R369   - FLL1 Control 1 */
	{ 0x00000172, 0x0008 },   /* R370   - FLL1 Control 2 */
	{ 0x00000173, 0x0018 },   /* R371   - FLL1 Control 3 */
	{ 0x00000174, 0x007D },   /* R372   - FLL1 Control 4 */
	{ 0x00000175, 0x0004 },   /* R373   - FLL1 Control 5 */
	{ 0x00000176, 0x0000 },   /* R374   - FLL1 Control 6 */
	{ 0x00000179, 0x0000 },   /* R377   - FLL1 Control 7 */
	{ 0x00000181, 0x0000 },   /* R385   - FLL1 Synchroniser 1 */
	{ 0x00000182, 0x0000 },   /* R386   - FLL1 Synchroniser 2 */
	{ 0x00000183, 0x0000 },   /* R387   - FLL1 Synchroniser 3 */
	{ 0x00000184, 0x0000 },   /* R388   - FLL1 Synchroniser 4 */
	{ 0x00000185, 0x0000 },   /* R389   - FLL1 Synchroniser 5 */
	{ 0x00000186, 0x0000 },   /* R390   - FLL1 Synchroniser 6 */
	{ 0x00000187, 0x0001 },   /* R391   - FLL1 Synchroniser 7 */
	{ 0x00000189, 0x0000 },   /* R393   - FLL1 Spread Spectrum */
	{ 0x0000018A, 0x0004 },   /* R394   - FLL1 GPIO Clock */
	{ 0x00000191, 0x0000 },   /* R401   - FLL2 Control 1 */
	{ 0x00000192, 0x0008 },   /* R402   - FLL2 Control 2 */
	{ 0x00000193, 0x0018 },   /* R403   - FLL2 Control 3 */
	{ 0x00000194, 0x007D },   /* R404   - FLL2 Control 4 */
	{ 0x00000195, 0x0004 },   /* R405   - FLL2 Control 5 */
	{ 0x00000196, 0x0000 },   /* R406   - FLL2 Control 6 */
	{ 0x00000199, 0x0000 },   /* R409   - FLL2 Control 7 */
	{ 0x000001A1, 0x0000 },   /* R417   - FLL2 Synchroniser 1 */
	{ 0x000001A2, 0x0000 },   /* R418   - FLL2 Synchroniser 2 */
	{ 0x000001A3, 0x0000 },   /* R419   - FLL2 Synchroniser 3 */
	{ 0x000001A4, 0x0000 },   /* R420   - FLL2 Synchroniser 4 */
	{ 0x000001A5, 0x0000 },   /* R421   - FLL2 Synchroniser 5 */
	{ 0x000001A6, 0x0000 },   /* R422   - FLL2 Synchroniser 6 */
	{ 0x000001A7, 0x0001 },   /* R423   - FLL2 Synchroniser 7 */
	{ 0x000001A9, 0x0000 },   /* R425   - FLL2 Spread Spectrum */
	{ 0x000001AA, 0x0004 },   /* R426   - FLL2 GPIO Clock */
	{ 0x00000200, 0x0006 },   /* R512   - Mic Charge Pump 1 */
	{ 0x00000210, 0x00D4 },   /* R528   - LDO1 Control 1 */
	{ 0x00000212, 0x0000 },   /* R530   - LDO1 Control 2 */
	{ 0x00000213, 0x0344 },   /* R531   - LDO2 Control 1 */
	{ 0x00000218, 0x01A6 },   /* R536   - Mic Bias Ctrl 1 */
	{ 0x00000219, 0x01A6 },   /* R537   - Mic Bias Ctrl 2 */
	{ 0x0000021A, 0x01A6 },   /* R538   - Mic Bias Ctrl 3 */
	{ 0x00000293, 0x0000 },   /* R659   - Accessory Detect Mode 1 */
	{ 0x0000029B, 0x0020 },   /* R667   - Headphone Detect 1 */
	{ 0x000002A2, 0x0000 },   /* R674   - Micd clamp control */
	{ 0x000002A3, 0x1102 },   /* R675   - Mic Detect 1 */
	{ 0x000002A4, 0x009F },   /* R676   - Mic Detect 2 */
	{ 0x000002A6, 0x3737 },   /* R678   - Mic Detect Level 1 */
	{ 0x000002A7, 0x2C37 },   /* R679   - Mic Detect Level 2 */
	{ 0x000002A8, 0x1422 },   /* R680   - Mic Detect Level 3 */
	{ 0x000002A9, 0x030A },   /* R681   - Mic Detect Level 4 */
	{ 0x000002C3, 0x0000 },   /* R707   - Mic noise mix control 1 */
	{ 0x000002CB, 0x0000 },   /* R715   - Isolation control */
	{ 0x000002D3, 0x0000 },   /* R723   - Jack detect analogue */
	{ 0x00000300, 0x0000 },   /* R768   - Input Enables */
	{ 0x00000308, 0x0000 },   /* R776   - Input Rate */
	{ 0x00000309, 0x0022 },   /* R777   - Input Volume Ramp */
	{ 0x00000310, 0x2080 },   /* R784   - IN1L Control */
	{ 0x00000311, 0x0180 },   /* R785   - ADC Digital Volume 1L */
	{ 0x00000312, 0x0000 },   /* R786   - DMIC1L Control */
	{ 0x00000314, 0x0080 },   /* R788   - IN1R Control */
	{ 0x00000315, 0x0180 },   /* R789   - ADC Digital Volume 1R */
	{ 0x00000316, 0x0000 },   /* R790   - DMIC1R Control */
	{ 0x00000318, 0x2080 },   /* R792   - IN2L Control */
	{ 0x00000319, 0x0180 },   /* R793   - ADC Digital Volume 2L */
	{ 0x0000031A, 0x0000 },   /* R794   - DMIC2L Control */
	{ 0x0000031C, 0x0080 },   /* R796   - IN2R Control */
	{ 0x0000031D, 0x0180 },   /* R797   - ADC Digital Volume 2R */
	{ 0x0000031E, 0x0000 },   /* R798   - DMIC2R Control */
	{ 0x00000320, 0x2080 },   /* R800   - IN3L Control */
	{ 0x00000321, 0x0180 },   /* R801   - ADC Digital Volume 3L */
	{ 0x00000322, 0x0000 },   /* R802   - DMIC3L Control */
	{ 0x00000324, 0x0080 },   /* R804   - IN3R Control */
	{ 0x00000325, 0x0180 },   /* R805   - ADC Digital Volume 3R */
	{ 0x00000326, 0x0000 },   /* R806   - DMIC3R Control */
	{ 0x00000400, 0x0000 },   /* R1024  - Output Enables 1 */
	{ 0x00000408, 0x0000 },   /* R1032  - Output Rate 1 */
	{ 0x00000409, 0x0022 },   /* R1033  - Output Volume Ramp */
	{ 0x00000410, 0x6080 },   /* R1040  - Output Path Config 1L */
	{ 0x00000411, 0x0180 },   /* R1041  - DAC Digital Volume 1L */
	{ 0x00000412, 0x0081 },   /* R1042  - DAC Volume Limit 1L */
	{ 0x00000413, 0x0001 },   /* R1043  - Noise Gate Select 1L */
	{ 0x00000414, 0x0080 },   /* R1044  - Output Path Config 1R */
	{ 0x00000415, 0x0180 },   /* R1045  - DAC Digital Volume 1R */
	{ 0x00000416, 0x0081 },   /* R1046  - DAC Volume Limit 1R */
	{ 0x00000417, 0x0002 },   /* R1047  - Noise Gate Select 1R */
	{ 0x00000418, 0xA080 },   /* R1048  - Output Path Config 2L */
	{ 0x00000419, 0x0180 },   /* R1049  - DAC Digital Volume 2L */
	{ 0x0000041A, 0x0081 },   /* R1050  - DAC Volume Limit 2L */
	{ 0x0000041B, 0x0004 },   /* R1051  - Noise Gate Select 2L */
	{ 0x0000041C, 0x0080 },   /* R1052  - Output Path Config 2R */
	{ 0x0000041D, 0x0180 },   /* R1053  - DAC Digital Volume 2R */
	{ 0x0000041E, 0x0081 },   /* R1054  - DAC Volume Limit 2R */
	{ 0x0000041F, 0x0008 },   /* R1055  - Noise Gate Select 2R */
	{ 0x00000420, 0xA080 },   /* R1056  - Output Path Config 3L */
	{ 0x00000421, 0x0180 },   /* R1057  - DAC Digital Volume 3L */
	{ 0x00000422, 0x0081 },   /* R1058  - DAC Volume Limit 3L */
	{ 0x00000423, 0x0010 },   /* R1059  - Noise Gate Select 3L */
	{ 0x00000428, 0xE000 },   /* R1064  - Output Path Config 4L */
	{ 0x00000429, 0x0180 },   /* R1065  - DAC Digital Volume 4L */
	{ 0x0000042A, 0x0081 },   /* R1066  - Out Volume 4L */
	{ 0x0000042B, 0x0040 },   /* R1067  - Noise Gate Select 4L */
	{ 0x0000042D, 0x0180 },   /* R1069  - DAC Digital Volume 4R */
	{ 0x0000042E, 0x0081 },   /* R1070  - Out Volume 4R */
	{ 0x0000042F, 0x0080 },   /* R1071  - Noise Gate Select 4R */
	{ 0x00000430, 0x0000 },   /* R1072  - Output Path Config 5L */
	{ 0x00000431, 0x0180 },   /* R1073  - DAC Digital Volume 5L */
	{ 0x00000432, 0x0081 },   /* R1074  - DAC Volume Limit 5L */
	{ 0x00000433, 0x0100 },   /* R1075  - Noise Gate Select 5L */
	{ 0x00000435, 0x0180 },   /* R1077  - DAC Digital Volume 5R */
	{ 0x00000436, 0x0081 },   /* R1078  - DAC Volume Limit 5R */
	{ 0x00000437, 0x0200 },   /* R1079  - Noise Gate Select 5R */
	{ 0x00000440, 0x0FFF },   /* R1088  - DRE Enable */
	{ 0x00000442, 0x3F0A },   /* R1090  - DRE Control 2 */
	{ 0x00000443, 0xDC1F },   /* R1090  - DRE Control 3 */
	{ 0x00000450, 0x0000 },   /* R1104  - DAC AEC Control 1 */
	{ 0x00000458, 0x000B },   /* R1112  - Noise Gate Control */
	{ 0x00000490, 0x0069 },   /* R1168  - PDM SPK1 CTRL 1 */
	{ 0x00000491, 0x0000 },   /* R1169  - PDM SPK1 CTRL 2 */
	{ 0x00000500, 0x000C },   /* R1280  - AIF1 BCLK Ctrl */
	{ 0x00000501, 0x0008 },   /* R1281  - AIF1 Tx Pin Ctrl */
	{ 0x00000502, 0x0000 },   /* R1282  - AIF1 Rx Pin Ctrl */
	{ 0x00000503, 0x0000 },   /* R1283  - AIF1 Rate Ctrl */
	{ 0x00000504, 0x0000 },   /* R1284  - AIF1 Format */
	{ 0x00000505, 0x0040 },   /* R1285  - AIF1 Tx BCLK Rate */
	{ 0x00000506, 0x0040 },   /* R1286  - AIF1 Rx BCLK Rate */
	{ 0x00000507, 0x1818 },   /* R1287  - AIF1 Frame Ctrl 1 */
	{ 0x00000508, 0x1818 },   /* R1288  - AIF1 Frame Ctrl 2 */
	{ 0x00000509, 0x0000 },   /* R1289  - AIF1 Frame Ctrl 3 */
	{ 0x0000050A, 0x0001 },   /* R1290  - AIF1 Frame Ctrl 4 */
	{ 0x0000050B, 0x0002 },   /* R1291  - AIF1 Frame Ctrl 5 */
	{ 0x0000050C, 0x0003 },   /* R1292  - AIF1 Frame Ctrl 6 */
	{ 0x0000050D, 0x0004 },   /* R1293  - AIF1 Frame Ctrl 7 */
	{ 0x0000050E, 0x0005 },   /* R1294  - AIF1 Frame Ctrl 8 */
	{ 0x0000050F, 0x0006 },   /* R1295  - AIF1 Frame Ctrl 9 */
	{ 0x00000510, 0x0007 },   /* R1296  - AIF1 Frame Ctrl 10 */
	{ 0x00000511, 0x0000 },   /* R1297  - AIF1 Frame Ctrl 11 */
	{ 0x00000512, 0x0001 },   /* R1298  - AIF1 Frame Ctrl 12 */
	{ 0x00000513, 0x0002 },   /* R1299  - AIF1 Frame Ctrl 13 */
	{ 0x00000514, 0x0003 },   /* R1300  - AIF1 Frame Ctrl 14 */
	{ 0x00000515, 0x0004 },   /* R1301  - AIF1 Frame Ctrl 15 */
	{ 0x00000516, 0x0005 },   /* R1302  - AIF1 Frame Ctrl 16 */
	{ 0x00000517, 0x0006 },   /* R1303  - AIF1 Frame Ctrl 17 */
	{ 0x00000518, 0x0007 },   /* R1304  - AIF1 Frame Ctrl 18 */
	{ 0x00000519, 0x0000 },   /* R1305  - AIF1 Tx Enables */
	{ 0x0000051A, 0x0000 },   /* R1306  - AIF1 Rx Enables */
	{ 0x00000540, 0x000C },   /* R1344  - AIF2 BCLK Ctrl */
	{ 0x00000541, 0x0008 },   /* R1345  - AIF2 Tx Pin Ctrl */
	{ 0x00000542, 0x0000 },   /* R1346  - AIF2 Rx Pin Ctrl */
	{ 0x00000543, 0x0000 },   /* R1347  - AIF2 Rate Ctrl */
	{ 0x00000544, 0x0000 },   /* R1348  - AIF2 Format */
	{ 0x00000545, 0x0040 },   /* R1349  - AIF2 Tx BCLK Rate */
	{ 0x00000546, 0x0040 },   /* R1350  - AIF2 Rx BCLK Rate */
	{ 0x00000547, 0x1818 },   /* R1351  - AIF2 Frame Ctrl 1 */
	{ 0x00000548, 0x1818 },   /* R1352  - AIF2 Frame Ctrl 2 */
	{ 0x00000549, 0x0000 },   /* R1353  - AIF2 Frame Ctrl 3 */
	{ 0x0000054A, 0x0001 },   /* R1354  - AIF2 Frame Ctrl 4 */
	{ 0x00000551, 0x0000 },   /* R1361  - AIF2 Frame Ctrl 11 */
	{ 0x00000552, 0x0001 },   /* R1362  - AIF2 Frame Ctrl 12 */
	{ 0x00000559, 0x0000 },   /* R1369  - AIF2 Tx Enables */
	{ 0x0000055A, 0x0000 },   /* R1370  - AIF2 Rx Enables */
	{ 0x00000580, 0x000C },   /* R1408  - AIF3 BCLK Ctrl */
	{ 0x00000581, 0x0008 },   /* R1409  - AIF3 Tx Pin Ctrl */
	{ 0x00000582, 0x0000 },   /* R1410  - AIF3 Rx Pin Ctrl */
	{ 0x00000583, 0x0000 },   /* R1411  - AIF3 Rate Ctrl */
	{ 0x00000584, 0x0000 },   /* R1412  - AIF3 Format */
	{ 0x00000585, 0x0040 },   /* R1413  - AIF3 Tx BCLK Rate */
	{ 0x00000586, 0x0040 },   /* R1414  - AIF3 Rx BCLK Rate */
	{ 0x00000587, 0x1818 },   /* R1415  - AIF3 Frame Ctrl 1 */
	{ 0x00000588, 0x1818 },   /* R1416  - AIF3 Frame Ctrl 2 */
	{ 0x00000589, 0x0000 },   /* R1417  - AIF3 Frame Ctrl 3 */
	{ 0x0000058A, 0x0001 },   /* R1418  - AIF3 Frame Ctrl 4 */
	{ 0x00000591, 0x0000 },   /* R1425  - AIF3 Frame Ctrl 11 */
	{ 0x00000592, 0x0001 },   /* R1426  - AIF3 Frame Ctrl 12 */
	{ 0x00000599, 0x0000 },   /* R1433  - AIF3 Tx Enables */
	{ 0x0000059A, 0x0000 },   /* R1434  - AIF3 Rx Enables */
	{ 0x000005E3, 0x0004 },   /* R1507  - SLIMbus Framer Ref Gear */
	{ 0x000005E5, 0x0000 },   /* R1509  - SLIMbus Rates 1 */
	{ 0x000005E6, 0x0000 },   /* R1510  - SLIMbus Rates 2 */
	{ 0x000005E7, 0x0000 },   /* R1511  - SLIMbus Rates 3 */
	{ 0x000005E8, 0x0000 },   /* R1512  - SLIMbus Rates 4 */
	{ 0x000005E9, 0x0000 },   /* R1513  - SLIMbus Rates 5 */
	{ 0x000005EA, 0x0000 },   /* R1514  - SLIMbus Rates 6 */
	{ 0x000005EB, 0x0000 },   /* R1515  - SLIMbus Rates 7 */
	{ 0x000005EC, 0x0000 },   /* R1516  - SLIMbus Rates 8 */
	{ 0x000005F5, 0x0000 },   /* R1525  - SLIMbus RX Channel Enable */
	{ 0x000005F6, 0x0000 },   /* R1526  - SLIMbus TX Channel Enable */
	{ 0x00000640, 0x0000 },   /* R1600  - PWM1MIX Input 1 Source */
	{ 0x00000641, 0x0080 },   /* R1601  - PWM1MIX Input 1 Volume */
	{ 0x00000642, 0x0000 },   /* R1602  - PWM1MIX Input 2 Source */
	{ 0x00000643, 0x0080 },   /* R1603  - PWM1MIX Input 2 Volume */
	{ 0x00000644, 0x0000 },   /* R1604  - PWM1MIX Input 3 Source */
	{ 0x00000645, 0x0080 },   /* R1605  - PWM1MIX Input 3 Volume */
	{ 0x00000646, 0x0000 },   /* R1606  - PWM1MIX Input 4 Source */
	{ 0x00000647, 0x0080 },   /* R1607  - PWM1MIX Input 4 Volume */
	{ 0x00000648, 0x0000 },   /* R1608  - PWM2MIX Input 1 Source */
	{ 0x00000649, 0x0080 },   /* R1609  - PWM2MIX Input 1 Volume */
	{ 0x0000064A, 0x0000 },   /* R1610  - PWM2MIX Input 2 Source */
	{ 0x0000064B, 0x0080 },   /* R1611  - PWM2MIX Input 2 Volume */
	{ 0x0000064C, 0x0000 },   /* R1612  - PWM2MIX Input 3 Source */
	{ 0x0000064D, 0x0080 },   /* R1613  - PWM2MIX Input 3 Volume */
	{ 0x0000064E, 0x0000 },   /* R1614  - PWM2MIX Input 4 Source */
	{ 0x0000064F, 0x0080 },   /* R1615  - PWM2MIX Input 4 Volume */
	{ 0x00000660, 0x0000 },   /* R1632  - MICMIX Input 1 Source */
	{ 0x00000661, 0x0080 },   /* R1633  - MICMIX Input 1 Volume */
	{ 0x00000662, 0x0000 },   /* R1634  - MICMIX Input 2 Source */
	{ 0x00000663, 0x0080 },   /* R1635  - MICMIX Input 2 Volume */
	{ 0x00000664, 0x0000 },   /* R1636  - MICMIX Input 3 Source */
	{ 0x00000665, 0x0080 },   /* R1637  - MICMIX Input 3 Volume */
	{ 0x00000666, 0x0000 },   /* R1638  - MICMIX Input 4 Source */
	{ 0x00000667, 0x0080 },   /* R1639  - MICMIX Input 4 Volume */
	{ 0x00000668, 0x0000 },   /* R1640  - NOISEMIX Input 1 Source */
	{ 0x00000669, 0x0080 },   /* R1641  - NOISEMIX Input 1 Volume */
	{ 0x0000066A, 0x0000 },   /* R1642  - NOISEMIX Input 2 Source */
	{ 0x0000066B, 0x0080 },   /* R1643  - NOISEMIX Input 2 Volume */
	{ 0x0000066C, 0x0000 },   /* R1644  - NOISEMIX Input 3 Source */
	{ 0x0000066D, 0x0080 },   /* R1645  - NOISEMIX Input 3 Volume */
	{ 0x0000066E, 0x0000 },   /* R1646  - NOISEMIX Input 4 Source */
	{ 0x0000066F, 0x0080 },   /* R1647  - NOISEMIX Input 4 Volume */
	{ 0x00000680, 0x0000 },   /* R1664  - OUT1LMIX Input 1 Source */
	{ 0x00000681, 0x0080 },   /* R1665  - OUT1LMIX Input 1 Volume */
	{ 0x00000682, 0x0000 },   /* R1666  - OUT1LMIX Input 2 Source */
	{ 0x00000683, 0x0080 },   /* R1667  - OUT1LMIX Input 2 Volume */
	{ 0x00000684, 0x0000 },   /* R1668  - OUT1LMIX Input 3 Source */
	{ 0x00000685, 0x0080 },   /* R1669  - OUT1LMIX Input 3 Volume */
	{ 0x00000686, 0x0000 },   /* R1670  - OUT1LMIX Input 4 Source */
	{ 0x00000687, 0x0080 },   /* R1671  - OUT1LMIX Input 4 Volume */
	{ 0x00000688, 0x0000 },   /* R1672  - OUT1RMIX Input 1 Source */
	{ 0x00000689, 0x0080 },   /* R1673  - OUT1RMIX Input 1 Volume */
	{ 0x0000068A, 0x0000 },   /* R1674  - OUT1RMIX Input 2 Source */
	{ 0x0000068B, 0x0080 },   /* R1675  - OUT1RMIX Input 2 Volume */
	{ 0x0000068C, 0x0000 },   /* R1676  - OUT1RMIX Input 3 Source */
	{ 0x0000068D, 0x0080 },   /* R1677  - OUT1RMIX Input 3 Volume */
	{ 0x0000068E, 0x0000 },   /* R1678  - OUT1RMIX Input 4 Source */
	{ 0x0000068F, 0x0080 },   /* R1679  - OUT1RMIX Input 4 Volume */
	{ 0x00000690, 0x0000 },   /* R1680  - OUT2LMIX Input 1 Source */
	{ 0x00000691, 0x0080 },   /* R1681  - OUT2LMIX Input 1 Volume */
	{ 0x00000692, 0x0000 },   /* R1682  - OUT2LMIX Input 2 Source */
	{ 0x00000693, 0x0080 },   /* R1683  - OUT2LMIX Input 2 Volume */
	{ 0x00000694, 0x0000 },   /* R1684  - OUT2LMIX Input 3 Source */
	{ 0x00000695, 0x0080 },   /* R1685  - OUT2LMIX Input 3 Volume */
	{ 0x00000696, 0x0000 },   /* R1686  - OUT2LMIX Input 4 Source */
	{ 0x00000697, 0x0080 },   /* R1687  - OUT2LMIX Input 4 Volume */
	{ 0x00000698, 0x0000 },   /* R1688  - OUT2RMIX Input 1 Source */
	{ 0x00000699, 0x0080 },   /* R1689  - OUT2RMIX Input 1 Volume */
	{ 0x0000069A, 0x0000 },   /* R1690  - OUT2RMIX Input 2 Source */
	{ 0x0000069B, 0x0080 },   /* R1691  - OUT2RMIX Input 2 Volume */
	{ 0x0000069C, 0x0000 },   /* R1692  - OUT2RMIX Input 3 Source */
	{ 0x0000069D, 0x0080 },   /* R1693  - OUT2RMIX Input 3 Volume */
	{ 0x0000069E, 0x0000 },   /* R1694  - OUT2RMIX Input 4 Source */
	{ 0x0000069F, 0x0080 },   /* R1695  - OUT2RMIX Input 4 Volume */
	{ 0x000006A0, 0x0000 },   /* R1696  - OUT3LMIX Input 1 Source */
	{ 0x000006A1, 0x0080 },   /* R1697  - OUT3LMIX Input 1 Volume */
	{ 0x000006A2, 0x0000 },   /* R1698  - OUT3LMIX Input 2 Source */
	{ 0x000006A3, 0x0080 },   /* R1699  - OUT3LMIX Input 2 Volume */
	{ 0x000006A4, 0x0000 },   /* R1700  - OUT3LMIX Input 3 Source */
	{ 0x000006A5, 0x0080 },   /* R1701  - OUT3LMIX Input 3 Volume */
	{ 0x000006A6, 0x0000 },   /* R1702  - OUT3LMIX Input 4 Source */
	{ 0x000006A7, 0x0080 },   /* R1703  - OUT3LMIX Input 4 Volume */
	{ 0x000006B0, 0x0000 },   /* R1712  - OUT4LMIX Input 1 Source */
	{ 0x000006B1, 0x0080 },   /* R1713  - OUT4LMIX Input 1 Volume */
	{ 0x000006B2, 0x0000 },   /* R1714  - OUT4LMIX Input 2 Source */
	{ 0x000006B3, 0x0080 },   /* R1715  - OUT4LMIX Input 2 Volume */
	{ 0x000006B4, 0x0000 },   /* R1716  - OUT4LMIX Input 3 Source */
	{ 0x000006B5, 0x0080 },   /* R1717  - OUT4LMIX Input 3 Volume */
	{ 0x000006B6, 0x0000 },   /* R1718  - OUT4LMIX Input 4 Source */
	{ 0x000006B7, 0x0080 },   /* R1719  - OUT4LMIX Input 4 Volume */
	{ 0x000006B8, 0x0000 },   /* R1720  - OUT4RMIX Input 1 Source */
	{ 0x000006B9, 0x0080 },   /* R1721  - OUT4RMIX Input 1 Volume */
	{ 0x000006BA, 0x0000 },   /* R1722  - OUT4RMIX Input 2 Source */
	{ 0x000006BB, 0x0080 },   /* R1723  - OUT4RMIX Input 2 Volume */
	{ 0x000006BC, 0x0000 },   /* R1724  - OUT4RMIX Input 3 Source */
	{ 0x000006BD, 0x0080 },   /* R1725  - OUT4RMIX Input 3 Volume */
	{ 0x000006BE, 0x0000 },   /* R1726  - OUT4RMIX Input 4 Source */
	{ 0x000006BF, 0x0080 },   /* R1727  - OUT4RMIX Input 4 Volume */
	{ 0x000006C0, 0x0000 },   /* R1728  - OUT5LMIX Input 1 Source */
	{ 0x000006C1, 0x0080 },   /* R1729  - OUT5LMIX Input 1 Volume */
	{ 0x000006C2, 0x0000 },   /* R1730  - OUT5LMIX Input 2 Source */
	{ 0x000006C3, 0x0080 },   /* R1731  - OUT5LMIX Input 2 Volume */
	{ 0x000006C4, 0x0000 },   /* R1732  - OUT5LMIX Input 3 Source */
	{ 0x000006C5, 0x0080 },   /* R1733  - OUT5LMIX Input 3 Volume */
	{ 0x000006C6, 0x0000 },   /* R1734  - OUT5LMIX Input 4 Source */
	{ 0x000006C7, 0x0080 },   /* R1735  - OUT5LMIX Input 4 Volume */
	{ 0x000006C8, 0x0000 },   /* R1736  - OUT5RMIX Input 1 Source */
	{ 0x000006C9, 0x0080 },   /* R1737  - OUT5RMIX Input 1 Volume */
	{ 0x000006CA, 0x0000 },   /* R1738  - OUT5RMIX Input 2 Source */
	{ 0x000006CB, 0x0080 },   /* R1739  - OUT5RMIX Input 2 Volume */
	{ 0x000006CC, 0x0000 },   /* R1740  - OUT5RMIX Input 3 Source */
	{ 0x000006CD, 0x0080 },   /* R1741  - OUT5RMIX Input 3 Volume */
	{ 0x000006CE, 0x0000 },   /* R1742  - OUT5RMIX Input 4 Source */
	{ 0x000006CF, 0x0080 },   /* R1743  - OUT5RMIX Input 4 Volume */
	{ 0x00000700, 0x0000 },   /* R1792  - AIF1TX1MIX Input 1 Source */
	{ 0x00000701, 0x0080 },   /* R1793  - AIF1TX1MIX Input 1 Volume */
	{ 0x00000702, 0x0000 },   /* R1794  - AIF1TX1MIX Input 2 Source */
	{ 0x00000703, 0x0080 },   /* R1795  - AIF1TX1MIX Input 2 Volume */
	{ 0x00000704, 0x0000 },   /* R1796  - AIF1TX1MIX Input 3 Source */
	{ 0x00000705, 0x0080 },   /* R1797  - AIF1TX1MIX Input 3 Volume */
	{ 0x00000706, 0x0000 },   /* R1798  - AIF1TX1MIX Input 4 Source */
	{ 0x00000707, 0x0080 },   /* R1799  - AIF1TX1MIX Input 4 Volume */
	{ 0x00000708, 0x0000 },   /* R1800  - AIF1TX2MIX Input 1 Source */
	{ 0x00000709, 0x0080 },   /* R1801  - AIF1TX2MIX Input 1 Volume */
	{ 0x0000070A, 0x0000 },   /* R1802  - AIF1TX2MIX Input 2 Source */
	{ 0x0000070B, 0x0080 },   /* R1803  - AIF1TX2MIX Input 2 Volume */
	{ 0x0000070C, 0x0000 },   /* R1804  - AIF1TX2MIX Input 3 Source */
	{ 0x0000070D, 0x0080 },   /* R1805  - AIF1TX2MIX Input 3 Volume */
	{ 0x0000070E, 0x0000 },   /* R1806  - AIF1TX2MIX Input 4 Source */
	{ 0x0000070F, 0x0080 },   /* R1807  - AIF1TX2MIX Input 4 Volume */
	{ 0x00000710, 0x0000 },   /* R1808  - AIF1TX3MIX Input 1 Source */
	{ 0x00000711, 0x0080 },   /* R1809  - AIF1TX3MIX Input 1 Volume */
	{ 0x00000712, 0x0000 },   /* R1810  - AIF1TX3MIX Input 2 Source */
	{ 0x00000713, 0x0080 },   /* R1811  - AIF1TX3MIX Input 2 Volume */
	{ 0x00000714, 0x0000 },   /* R1812  - AIF1TX3MIX Input 3 Source */
	{ 0x00000715, 0x0080 },   /* R1813  - AIF1TX3MIX Input 3 Volume */
	{ 0x00000716, 0x0000 },   /* R1814  - AIF1TX3MIX Input 4 Source */
	{ 0x00000717, 0x0080 },   /* R1815  - AIF1TX3MIX Input 4 Volume */
	{ 0x00000718, 0x0000 },   /* R1816  - AIF1TX4MIX Input 1 Source */
	{ 0x00000719, 0x0080 },   /* R1817  - AIF1TX4MIX Input 1 Volume */
	{ 0x0000071A, 0x0000 },   /* R1818  - AIF1TX4MIX Input 2 Source */
	{ 0x0000071B, 0x0080 },   /* R1819  - AIF1TX4MIX Input 2 Volume */
	{ 0x0000071C, 0x0000 },   /* R1820  - AIF1TX4MIX Input 3 Source */
	{ 0x0000071D, 0x0080 },   /* R1821  - AIF1TX4MIX Input 3 Volume */
	{ 0x0000071E, 0x0000 },   /* R1822  - AIF1TX4MIX Input 4 Source */
	{ 0x0000071F, 0x0080 },   /* R1823  - AIF1TX4MIX Input 4 Volume */
	{ 0x00000720, 0x0000 },   /* R1824  - AIF1TX5MIX Input 1 Source */
	{ 0x00000721, 0x0080 },   /* R1825  - AIF1TX5MIX Input 1 Volume */
	{ 0x00000722, 0x0000 },   /* R1826  - AIF1TX5MIX Input 2 Source */
	{ 0x00000723, 0x0080 },   /* R1827  - AIF1TX5MIX Input 2 Volume */
	{ 0x00000724, 0x0000 },   /* R1828  - AIF1TX5MIX Input 3 Source */
	{ 0x00000725, 0x0080 },   /* R1829  - AIF1TX5MIX Input 3 Volume */
	{ 0x00000726, 0x0000 },   /* R1830  - AIF1TX5MIX Input 4 Source */
	{ 0x00000727, 0x0080 },   /* R1831  - AIF1TX5MIX Input 4 Volume */
	{ 0x00000728, 0x0000 },   /* R1832  - AIF1TX6MIX Input 1 Source */
	{ 0x00000729, 0x0080 },   /* R1833  - AIF1TX6MIX Input 1 Volume */
	{ 0x0000072A, 0x0000 },   /* R1834  - AIF1TX6MIX Input 2 Source */
	{ 0x0000072B, 0x0080 },   /* R1835  - AIF1TX6MIX Input 2 Volume */
	{ 0x0000072C, 0x0000 },   /* R1836  - AIF1TX6MIX Input 3 Source */
	{ 0x0000072D, 0x0080 },   /* R1837  - AIF1TX6MIX Input 3 Volume */
	{ 0x0000072E, 0x0000 },   /* R1838  - AIF1TX6MIX Input 4 Source */
	{ 0x0000072F, 0x0080 },   /* R1839  - AIF1TX6MIX Input 4 Volume */
	{ 0x00000730, 0x0000 },   /* R1840  - AIF1TX7MIX Input 1 Source */
	{ 0x00000731, 0x0080 },   /* R1841  - AIF1TX7MIX Input 1 Volume */
	{ 0x00000732, 0x0000 },   /* R1842  - AIF1TX7MIX Input 2 Source */
	{ 0x00000733, 0x0080 },   /* R1843  - AIF1TX7MIX Input 2 Volume */
	{ 0x00000734, 0x0000 },   /* R1844  - AIF1TX7MIX Input 3 Source */
	{ 0x00000735, 0x0080 },   /* R1845  - AIF1TX7MIX Input 3 Volume */
	{ 0x00000736, 0x0000 },   /* R1846  - AIF1TX7MIX Input 4 Source */
	{ 0x00000737, 0x0080 },   /* R1847  - AIF1TX7MIX Input 4 Volume */
	{ 0x00000738, 0x0000 },   /* R1848  - AIF1TX8MIX Input 1 Source */
	{ 0x00000739, 0x0080 },   /* R1849  - AIF1TX8MIX Input 1 Volume */
	{ 0x0000073A, 0x0000 },   /* R1850  - AIF1TX8MIX Input 2 Source */
	{ 0x0000073B, 0x0080 },   /* R1851  - AIF1TX8MIX Input 2 Volume */
	{ 0x0000073C, 0x0000 },   /* R1852  - AIF1TX8MIX Input 3 Source */
	{ 0x0000073D, 0x0080 },   /* R1853  - AIF1TX8MIX Input 3 Volume */
	{ 0x0000073E, 0x0000 },   /* R1854  - AIF1TX8MIX Input 4 Source */
	{ 0x0000073F, 0x0080 },   /* R1855  - AIF1TX8MIX Input 4 Volume */
	{ 0x00000740, 0x0000 },   /* R1856  - AIF2TX1MIX Input 1 Source */
	{ 0x00000741, 0x0080 },   /* R1857  - AIF2TX1MIX Input 1 Volume */
	{ 0x00000742, 0x0000 },   /* R1858  - AIF2TX1MIX Input 2 Source */
	{ 0x00000743, 0x0080 },   /* R1859  - AIF2TX1MIX Input 2 Volume */
	{ 0x00000744, 0x0000 },   /* R1860  - AIF2TX1MIX Input 3 Source */
	{ 0x00000745, 0x0080 },   /* R1861  - AIF2TX1MIX Input 3 Volume */
	{ 0x00000746, 0x0000 },   /* R1862  - AIF2TX1MIX Input 4 Source */
	{ 0x00000747, 0x0080 },   /* R1863  - AIF2TX1MIX Input 4 Volume */
	{ 0x00000748, 0x0000 },   /* R1864  - AIF2TX2MIX Input 1 Source */
	{ 0x00000749, 0x0080 },   /* R1865  - AIF2TX2MIX Input 1 Volume */
	{ 0x0000074A, 0x0000 },   /* R1866  - AIF2TX2MIX Input 2 Source */
	{ 0x0000074B, 0x0080 },   /* R1867  - AIF2TX2MIX Input 2 Volume */
	{ 0x0000074C, 0x0000 },   /* R1868  - AIF2TX2MIX Input 3 Source */
	{ 0x0000074D, 0x0080 },   /* R1869  - AIF2TX2MIX Input 3 Volume */
	{ 0x0000074E, 0x0000 },   /* R1870  - AIF2TX2MIX Input 4 Source */
	{ 0x0000074F, 0x0080 },   /* R1871  - AIF2TX2MIX Input 4 Volume */
	{ 0x00000780, 0x0000 },   /* R1920  - AIF3TX1MIX Input 1 Source */
	{ 0x00000781, 0x0080 },   /* R1921  - AIF3TX1MIX Input 1 Volume */
	{ 0x00000782, 0x0000 },   /* R1922  - AIF3TX1MIX Input 2 Source */
	{ 0x00000783, 0x0080 },   /* R1923  - AIF3TX1MIX Input 2 Volume */
	{ 0x00000784, 0x0000 },   /* R1924  - AIF3TX1MIX Input 3 Source */
	{ 0x00000785, 0x0080 },   /* R1925  - AIF3TX1MIX Input 3 Volume */
	{ 0x00000786, 0x0000 },   /* R1926  - AIF3TX1MIX Input 4 Source */
	{ 0x00000787, 0x0080 },   /* R1927  - AIF3TX1MIX Input 4 Volume */
	{ 0x00000788, 0x0000 },   /* R1928  - AIF3TX2MIX Input 1 Source */
	{ 0x00000789, 0x0080 },   /* R1929  - AIF3TX2MIX Input 1 Volume */
	{ 0x0000078A, 0x0000 },   /* R1930  - AIF3TX2MIX Input 2 Source */
	{ 0x0000078B, 0x0080 },   /* R1931  - AIF3TX2MIX Input 2 Volume */
	{ 0x0000078C, 0x0000 },   /* R1932  - AIF3TX2MIX Input 3 Source */
	{ 0x0000078D, 0x0080 },   /* R1933  - AIF3TX2MIX Input 3 Volume */
	{ 0x0000078E, 0x0000 },   /* R1934  - AIF3TX2MIX Input 4 Source */
	{ 0x0000078F, 0x0080 },   /* R1935  - AIF3TX2MIX Input 4 Volume */
	{ 0x000007C0, 0x0000 },   /* R1984  - SLIMTX1MIX Input 1 Source */
	{ 0x000007C1, 0x0080 },   /* R1985  - SLIMTX1MIX Input 1 Volume */
	{ 0x000007C2, 0x0000 },   /* R1986  - SLIMTX1MIX Input 2 Source */
	{ 0x000007C3, 0x0080 },   /* R1987  - SLIMTX1MIX Input 2 Volume */
	{ 0x000007C4, 0x0000 },   /* R1988  - SLIMTX1MIX Input 3 Source */
	{ 0x000007C5, 0x0080 },   /* R1989  - SLIMTX1MIX Input 3 Volume */
	{ 0x000007C6, 0x0000 },   /* R1990  - SLIMTX1MIX Input 4 Source */
	{ 0x000007C7, 0x0080 },   /* R1991  - SLIMTX1MIX Input 4 Volume */
	{ 0x000007C8, 0x0000 },   /* R1992  - SLIMTX2MIX Input 1 Source */
	{ 0x000007C9, 0x0080 },   /* R1993  - SLIMTX2MIX Input 1 Volume */
	{ 0x000007CA, 0x0000 },   /* R1994  - SLIMTX2MIX Input 2 Source */
	{ 0x000007CB, 0x0080 },   /* R1995  - SLIMTX2MIX Input 2 Volume */
	{ 0x000007CC, 0x0000 },   /* R1996  - SLIMTX2MIX Input 3 Source */
	{ 0x000007CD, 0x0080 },   /* R1997  - SLIMTX2MIX Input 3 Volume */
	{ 0x000007CE, 0x0000 },   /* R1998  - SLIMTX2MIX Input 4 Source */
	{ 0x000007CF, 0x0080 },   /* R1999  - SLIMTX2MIX Input 4 Volume */
	{ 0x000007D0, 0x0000 },   /* R2000  - SLIMTX3MIX Input 1 Source */
	{ 0x000007D1, 0x0080 },   /* R2001  - SLIMTX3MIX Input 1 Volume */
	{ 0x000007D2, 0x0000 },   /* R2002  - SLIMTX3MIX Input 2 Source */
	{ 0x000007D3, 0x0080 },   /* R2003  - SLIMTX3MIX Input 2 Volume */
	{ 0x000007D4, 0x0000 },   /* R2004  - SLIMTX3MIX Input 3 Source */
	{ 0x000007D5, 0x0080 },   /* R2005  - SLIMTX3MIX Input 3 Volume */
	{ 0x000007D6, 0x0000 },   /* R2006  - SLIMTX3MIX Input 4 Source */
	{ 0x000007D7, 0x0080 },   /* R2007  - SLIMTX3MIX Input 4 Volume */
	{ 0x000007D8, 0x0000 },   /* R2008  - SLIMTX4MIX Input 1 Source */
	{ 0x000007D9, 0x0080 },   /* R2009  - SLIMTX4MIX Input 1 Volume */
	{ 0x000007DA, 0x0000 },   /* R2010  - SLIMTX4MIX Input 2 Source */
	{ 0x000007DB, 0x0080 },   /* R2011  - SLIMTX4MIX Input 2 Volume */
	{ 0x000007DC, 0x0000 },   /* R2012  - SLIMTX4MIX Input 3 Source */
	{ 0x000007DD, 0x0080 },   /* R2013  - SLIMTX4MIX Input 3 Volume */
	{ 0x000007DE, 0x0000 },   /* R2014  - SLIMTX4MIX Input 4 Source */
	{ 0x000007DF, 0x0080 },   /* R2015  - SLIMTX4MIX Input 4 Volume */
	{ 0x000007E0, 0x0000 },   /* R2016  - SLIMTX5MIX Input 1 Source */
	{ 0x000007E1, 0x0080 },   /* R2017  - SLIMTX5MIX Input 1 Volume */
	{ 0x000007E2, 0x0000 },   /* R2018  - SLIMTX5MIX Input 2 Source */
	{ 0x000007E3, 0x0080 },   /* R2019  - SLIMTX5MIX Input 2 Volume */
	{ 0x000007E4, 0x0000 },   /* R2020  - SLIMTX5MIX Input 3 Source */
	{ 0x000007E5, 0x0080 },   /* R2021  - SLIMTX5MIX Input 3 Volume */
	{ 0x000007E6, 0x0000 },   /* R2022  - SLIMTX5MIX Input 4 Source */
	{ 0x000007E7, 0x0080 },   /* R2023  - SLIMTX5MIX Input 4 Volume */
	{ 0x000007E8, 0x0000 },   /* R2024  - SLIMTX6MIX Input 1 Source */
	{ 0x000007E9, 0x0080 },   /* R2025  - SLIMTX6MIX Input 1 Volume */
	{ 0x000007EA, 0x0000 },   /* R2026  - SLIMTX6MIX Input 2 Source */
	{ 0x000007EB, 0x0080 },   /* R2027  - SLIMTX6MIX Input 2 Volume */
	{ 0x000007EC, 0x0000 },   /* R2028  - SLIMTX6MIX Input 3 Source */
	{ 0x000007ED, 0x0080 },   /* R2029  - SLIMTX6MIX Input 3 Volume */
	{ 0x000007EE, 0x0000 },   /* R2030  - SLIMTX6MIX Input 4 Source */
	{ 0x000007EF, 0x0080 },   /* R2031  - SLIMTX6MIX Input 4 Volume */
	{ 0x000007F0, 0x0000 },   /* R2032  - SLIMTX7MIX Input 1 Source */
	{ 0x000007F1, 0x0080 },   /* R2033  - SLIMTX7MIX Input 1 Volume */
	{ 0x000007F2, 0x0000 },   /* R2034  - SLIMTX7MIX Input 2 Source */
	{ 0x000007F3, 0x0080 },   /* R2035  - SLIMTX7MIX Input 2 Volume */
	{ 0x000007F4, 0x0000 },   /* R2036  - SLIMTX7MIX Input 3 Source */
	{ 0x000007F5, 0x0080 },   /* R2037  - SLIMTX7MIX Input 3 Volume */
	{ 0x000007F6, 0x0000 },   /* R2038  - SLIMTX7MIX Input 4 Source */
	{ 0x000007F7, 0x0080 },   /* R2039  - SLIMTX7MIX Input 4 Volume */
	{ 0x000007F8, 0x0000 },   /* R2040  - SLIMTX8MIX Input 1 Source */
	{ 0x000007F9, 0x0080 },   /* R2041  - SLIMTX8MIX Input 1 Volume */
	{ 0x000007FA, 0x0000 },   /* R2042  - SLIMTX8MIX Input 2 Source */
	{ 0x000007FB, 0x0080 },   /* R2043  - SLIMTX8MIX Input 2 Volume */
	{ 0x000007FC, 0x0000 },   /* R2044  - SLIMTX8MIX Input 3 Source */
	{ 0x000007FD, 0x0080 },   /* R2045  - SLIMTX8MIX Input 3 Volume */
	{ 0x000007FE, 0x0000 },   /* R2046  - SLIMTX8MIX Input 4 Source */
	{ 0x000007FF, 0x0080 },   /* R2047  - SLIMTX8MIX Input 4 Volume */
	{ 0x00000880, 0x0000 },   /* R2176  - EQ1MIX Input 1 Source */
	{ 0x00000881, 0x0080 },   /* R2177  - EQ1MIX Input 1 Volume */
	{ 0x00000882, 0x0000 },   /* R2178  - EQ1MIX Input 2 Source */
	{ 0x00000883, 0x0080 },   /* R2179  - EQ1MIX Input 2 Volume */
	{ 0x00000884, 0x0000 },   /* R2180  - EQ1MIX Input 3 Source */
	{ 0x00000885, 0x0080 },   /* R2181  - EQ1MIX Input 3 Volume */
	{ 0x00000886, 0x0000 },   /* R2182  - EQ1MIX Input 4 Source */
	{ 0x00000887, 0x0080 },   /* R2183  - EQ1MIX Input 4 Volume */
	{ 0x00000888, 0x0000 },   /* R2184  - EQ2MIX Input 1 Source */
	{ 0x00000889, 0x0080 },   /* R2185  - EQ2MIX Input 1 Volume */
	{ 0x0000088A, 0x0000 },   /* R2186  - EQ2MIX Input 2 Source */
	{ 0x0000088B, 0x0080 },   /* R2187  - EQ2MIX Input 2 Volume */
	{ 0x0000088C, 0x0000 },   /* R2188  - EQ2MIX Input 3 Source */
	{ 0x0000088D, 0x0080 },   /* R2189  - EQ2MIX Input 3 Volume */
	{ 0x0000088E, 0x0000 },   /* R2190  - EQ2MIX Input 4 Source */
	{ 0x0000088F, 0x0080 },   /* R2191  - EQ2MIX Input 4 Volume */
	{ 0x00000890, 0x0000 },   /* R2192  - EQ3MIX Input 1 Source */
	{ 0x00000891, 0x0080 },   /* R2193  - EQ3MIX Input 1 Volume */
	{ 0x00000892, 0x0000 },   /* R2194  - EQ3MIX Input 2 Source */
	{ 0x00000893, 0x0080 },   /* R2195  - EQ3MIX Input 2 Volume */
	{ 0x00000894, 0x0000 },   /* R2196  - EQ3MIX Input 3 Source */
	{ 0x00000895, 0x0080 },   /* R2197  - EQ3MIX Input 3 Volume */
	{ 0x00000896, 0x0000 },   /* R2198  - EQ3MIX Input 4 Source */
	{ 0x00000897, 0x0080 },   /* R2199  - EQ3MIX Input 4 Volume */
	{ 0x00000898, 0x0000 },   /* R2200  - EQ4MIX Input 1 Source */
	{ 0x00000899, 0x0080 },   /* R2201  - EQ4MIX Input 1 Volume */
	{ 0x0000089A, 0x0000 },   /* R2202  - EQ4MIX Input 2 Source */
	{ 0x0000089B, 0x0080 },   /* R2203  - EQ4MIX Input 2 Volume */
	{ 0x0000089C, 0x0000 },   /* R2204  - EQ4MIX Input 3 Source */
	{ 0x0000089D, 0x0080 },   /* R2205  - EQ4MIX Input 3 Volume */
	{ 0x0000089E, 0x0000 },   /* R2206  - EQ4MIX Input 4 Source */
	{ 0x0000089F, 0x0080 },   /* R2207  - EQ4MIX Input 4 Volume */
	{ 0x000008C0, 0x0000 },   /* R2240  - DRC1LMIX Input 1 Source */
	{ 0x000008C1, 0x0080 },   /* R2241  - DRC1LMIX Input 1 Volume */
	{ 0x000008C2, 0x0000 },   /* R2242  - DRC1LMIX Input 2 Source */
	{ 0x000008C3, 0x0080 },   /* R2243  - DRC1LMIX Input 2 Volume */
	{ 0x000008C4, 0x0000 },   /* R2244  - DRC1LMIX Input 3 Source */
	{ 0x000008C5, 0x0080 },   /* R2245  - DRC1LMIX Input 3 Volume */
	{ 0x000008C6, 0x0000 },   /* R2246  - DRC1LMIX Input 4 Source */
	{ 0x000008C7, 0x0080 },   /* R2247  - DRC1LMIX Input 4 Volume */
	{ 0x000008C8, 0x0000 },   /* R2248  - DRC1RMIX Input 1 Source */
	{ 0x000008C9, 0x0080 },   /* R2249  - DRC1RMIX Input 1 Volume */
	{ 0x000008CA, 0x0000 },   /* R2250  - DRC1RMIX Input 2 Source */
	{ 0x000008CB, 0x0080 },   /* R2251  - DRC1RMIX Input 2 Volume */
	{ 0x000008CC, 0x0000 },   /* R2252  - DRC1RMIX Input 3 Source */
	{ 0x000008CD, 0x0080 },   /* R2253  - DRC1RMIX Input 3 Volume */
	{ 0x000008CE, 0x0000 },   /* R2254  - DRC1RMIX Input 4 Source */
	{ 0x000008CF, 0x0080 },   /* R2255  - DRC1RMIX Input 4 Volume */
	{ 0x00000900, 0x0000 },   /* R2304  - HPLP1MIX Input 1 Source */
	{ 0x00000901, 0x0080 },   /* R2305  - HPLP1MIX Input 1 Volume */
	{ 0x00000902, 0x0000 },   /* R2306  - HPLP1MIX Input 2 Source */
	{ 0x00000903, 0x0080 },   /* R2307  - HPLP1MIX Input 2 Volume */
	{ 0x00000904, 0x0000 },   /* R2308  - HPLP1MIX Input 3 Source */
	{ 0x00000905, 0x0080 },   /* R2309  - HPLP1MIX Input 3 Volume */
	{ 0x00000906, 0x0000 },   /* R2310  - HPLP1MIX Input 4 Source */
	{ 0x00000907, 0x0080 },   /* R2311  - HPLP1MIX Input 4 Volume */
	{ 0x00000908, 0x0000 },   /* R2312  - HPLP2MIX Input 1 Source */
	{ 0x00000909, 0x0080 },   /* R2313  - HPLP2MIX Input 1 Volume */
	{ 0x0000090A, 0x0000 },   /* R2314  - HPLP2MIX Input 2 Source */
	{ 0x0000090B, 0x0080 },   /* R2315  - HPLP2MIX Input 2 Volume */
	{ 0x0000090C, 0x0000 },   /* R2316  - HPLP2MIX Input 3 Source */
	{ 0x0000090D, 0x0080 },   /* R2317  - HPLP2MIX Input 3 Volume */
	{ 0x0000090E, 0x0000 },   /* R2318  - HPLP2MIX Input 4 Source */
	{ 0x0000090F, 0x0080 },   /* R2319  - HPLP2MIX Input 4 Volume */
	{ 0x00000910, 0x0000 },   /* R2320  - HPLP3MIX Input 1 Source */
	{ 0x00000911, 0x0080 },   /* R2321  - HPLP3MIX Input 1 Volume */
	{ 0x00000912, 0x0000 },   /* R2322  - HPLP3MIX Input 2 Source */
	{ 0x00000913, 0x0080 },   /* R2323  - HPLP3MIX Input 2 Volume */
	{ 0x00000914, 0x0000 },   /* R2324  - HPLP3MIX Input 3 Source */
	{ 0x00000915, 0x0080 },   /* R2325  - HPLP3MIX Input 3 Volume */
	{ 0x00000916, 0x0000 },   /* R2326  - HPLP3MIX Input 4 Source */
	{ 0x00000917, 0x0080 },   /* R2327  - HPLP3MIX Input 4 Volume */
	{ 0x00000918, 0x0000 },   /* R2328  - HPLP4MIX Input 1 Source */
	{ 0x00000919, 0x0080 },   /* R2329  - HPLP4MIX Input 1 Volume */
	{ 0x0000091A, 0x0000 },   /* R2330  - HPLP4MIX Input 2 Source */
	{ 0x0000091B, 0x0080 },   /* R2331  - HPLP4MIX Input 2 Volume */
	{ 0x0000091C, 0x0000 },   /* R2332  - HPLP4MIX Input 3 Source */
	{ 0x0000091D, 0x0080 },   /* R2333  - HPLP4MIX Input 3 Volume */
	{ 0x0000091E, 0x0000 },   /* R2334  - HPLP4MIX Input 4 Source */
	{ 0x0000091F, 0x0080 },   /* R2335  - HPLP4MIX Input 4 Volume */
	{ 0x00000940, 0x0000 },   /* R2368  - DSP1LMIX Input 1 Source */
	{ 0x00000941, 0x0080 },   /* R2369  - DSP1LMIX Input 1 Volume */
	{ 0x00000942, 0x0000 },   /* R2370  - DSP1LMIX Input 2 Source */
	{ 0x00000943, 0x0080 },   /* R2371  - DSP1LMIX Input 2 Volume */
	{ 0x00000944, 0x0000 },   /* R2372  - DSP1LMIX Input 3 Source */
	{ 0x00000945, 0x0080 },   /* R2373  - DSP1LMIX Input 3 Volume */
	{ 0x00000946, 0x0000 },   /* R2374  - DSP1LMIX Input 4 Source */
	{ 0x00000947, 0x0080 },   /* R2375  - DSP1LMIX Input 4 Volume */
	{ 0x00000948, 0x0000 },   /* R2376  - DSP1RMIX Input 1 Source */
	{ 0x00000949, 0x0080 },   /* R2377  - DSP1RMIX Input 1 Volume */
	{ 0x0000094A, 0x0000 },   /* R2378  - DSP1RMIX Input 2 Source */
	{ 0x0000094B, 0x0080 },   /* R2379  - DSP1RMIX Input 2 Volume */
	{ 0x0000094C, 0x0000 },   /* R2380  - DSP1RMIX Input 3 Source */
	{ 0x0000094D, 0x0080 },   /* R2381  - DSP1RMIX Input 3 Volume */
	{ 0x0000094E, 0x0000 },   /* R2382  - DSP1RMIX Input 4 Source */
	{ 0x0000094F, 0x0080 },   /* R2383  - DSP1RMIX Input 4 Volume */
	{ 0x00000950, 0x0000 },   /* R2384  - DSP1AUX1MIX Input 1 Source */
	{ 0x00000958, 0x0000 },   /* R2392  - DSP1AUX2MIX Input 1 Source */
	{ 0x00000960, 0x0000 },   /* R2400  - DSP1AUX3MIX Input 1 Source */
	{ 0x00000968, 0x0000 },   /* R2408  - DSP1AUX4MIX Input 1 Source */
	{ 0x00000970, 0x0000 },   /* R2416  - DSP1AUX5MIX Input 1 Source */
	{ 0x00000978, 0x0000 },   /* R2424  - DSP1AUX6MIX Input 1 Source */
	{ 0x00000A80, 0x0000 },   /* R2688  - ASRC1LMIX Input 1 Source */
	{ 0x00000A88, 0x0000 },   /* R2696  - ASRC1RMIX Input 1 Source */
	{ 0x00000A90, 0x0000 },   /* R2704  - ASRC2LMIX Input 1 Source */
	{ 0x00000A98, 0x0000 },   /* R2712  - ASRC2RMIX Input 1 Source */
	{ 0x00000B00, 0x0000 },   /* R2816  - ISRC1DEC1MIX Input 1 Source */
	{ 0x00000B08, 0x0000 },   /* R2824  - ISRC1DEC2MIX Input 1 Source */
	{ 0x00000B20, 0x0000 },   /* R2848  - ISRC1INT1MIX Input 1 Source */
	{ 0x00000B28, 0x0000 },   /* R2856  - ISRC1INT2MIX Input 1 Source */
	{ 0x00000B40, 0x0000 },   /* R2880  - ISRC2DEC1MIX Input 1 Source */
	{ 0x00000B48, 0x0000 },   /* R2888  - ISRC2DEC2MIX Input 1 Source */
	{ 0x00000B60, 0x0000 },   /* R2912  - ISRC2INT1MIX Input 1 Source */
	{ 0x00000B68, 0x0000 },   /* R2920  - ISRC2INT2MIX Input 1 Source */
	{ 0x00000C00, 0xA101 },   /* R3072  - GPIO1 CTRL */
	{ 0x00000C01, 0xA101 },   /* R3073  - GPIO2 CTRL */
	{ 0x00000C02, 0xA101 },   /* R3074  - GPIO3 CTRL */
	{ 0x00000C03, 0xA101 },   /* R3075  - GPIO4 CTRL */
	{ 0x00000C04, 0xA101 },   /* R3076  - GPIO5 CTRL */
	{ 0x00000C0F, 0x0400 },   /* R3087  - IRQ CTRL 1 */
	{ 0x00000C10, 0x1000 },   /* R3088  - GPIO Debounce Config */
	{ 0x00000C20, 0x8002 },   /* R3104  - Misc Pad Ctrl 1 */
	{ 0x00000C21, 0x0001 },   /* R3105  - Misc Pad Ctrl 2 */
	{ 0x00000C22, 0x0000 },   /* R3106  - Misc Pad Ctrl 3 */
	{ 0x00000C23, 0x0000 },   /* R3107  - Misc Pad Ctrl 4 */
	{ 0x00000C24, 0x0000 },   /* R3108  - Misc Pad Ctrl 5 */
	{ 0x00000C25, 0x0000 },   /* R3109  - Misc Pad Ctrl 6 */
	{ 0x00000D08, 0xFFFF },   /* R3336  - Interrupt Status 1 Mask */
	{ 0x00000D09, 0xFFFF },   /* R3337  - Interrupt Status 2 Mask */
	{ 0x00000D0A, 0xFFFF },   /* R3338  - Interrupt Status 3 Mask */
	{ 0x00000D0B, 0xFFFF },   /* R3339  - Interrupt Status 4 Mask */
	{ 0x00000D0C, 0xFEFF },   /* R3340  - Interrupt Status 5 Mask */
	{ 0x00000D0F, 0x0000 },   /* R3343  - Interrupt Control */
	{ 0x00000D18, 0xFFFF },   /* R3352  - IRQ2 Status 1 Mask */
	{ 0x00000D19, 0xFFFF },   /* R3353  - IRQ2 Status 2 Mask */
	{ 0x00000D1A, 0xFFFF },   /* R3354  - IRQ2 Status 3 Mask */
	{ 0x00000D1B, 0xFFFF },   /* R3355  - IRQ2 Status 4 Mask */
	{ 0x00000D1C, 0xFFFF },   /* R3356  - IRQ2 Status 5 Mask */
	{ 0x00000D1F, 0x0000 },   /* R3359  - IRQ2 Control */
	{ 0x00000D41, 0x0000 },   /* R3393  - ADSP2 IRQ0 */
	{ 0x00000D53, 0xFFFF },   /* R3411  - AOD IRQ Mask IRQ1 */
	{ 0x00000D54, 0xFFFF },   /* R3412  - AOD IRQ Mask IRQ2 */
	{ 0x00000D56, 0x0000 },   /* R3414  - Jack detect debounce */
	{ 0x00000E00, 0x0000 },   /* R3584  - FX_Ctrl1 */
	{ 0x00000E10, 0x6318 },   /* R3600  - EQ1_1 */
	{ 0x00000E11, 0x6300 },   /* R3601  - EQ1_2 */
	{ 0x00000E12, 0x0FC8 },   /* R3602  - EQ1_3 */
	{ 0x00000E13, 0x03FE },   /* R3603  - EQ1_4 */
	{ 0x00000E14, 0x00E0 },   /* R3604  - EQ1_5 */
	{ 0x00000E15, 0x1EC4 },   /* R3605  - EQ1_6 */
	{ 0x00000E16, 0xF136 },   /* R3606  - EQ1_7 */
	{ 0x00000E17, 0x0409 },   /* R3607  - EQ1_8 */
	{ 0x00000E18, 0x04CC },   /* R3608  - EQ1_9 */
	{ 0x00000E19, 0x1C9B },   /* R3609  - EQ1_10 */
	{ 0x00000E1A, 0xF337 },   /* R3610  - EQ1_11 */
	{ 0x00000E1B, 0x040B },   /* R3611  - EQ1_12 */
	{ 0x00000E1C, 0x0CBB },   /* R3612  - EQ1_13 */
	{ 0x00000E1D, 0x16F8 },   /* R3613  - EQ1_14 */
	{ 0x00000E1E, 0xF7D9 },   /* R3614  - EQ1_15 */
	{ 0x00000E1F, 0x040A },   /* R3615  - EQ1_16 */
	{ 0x00000E20, 0x1F14 },   /* R3616  - EQ1_17 */
	{ 0x00000E21, 0x058C },   /* R3617  - EQ1_18 */
	{ 0x00000E22, 0x0563 },   /* R3618  - EQ1_19 */
	{ 0x00000E23, 0x4000 },   /* R3619  - EQ1_20 */
	{ 0x00000E24, 0x0B75 },   /* R3620  - EQ1_21 */
	{ 0x00000E26, 0x6318 },   /* R3622  - EQ2_1 */
	{ 0x00000E27, 0x6300 },   /* R3623  - EQ2_2 */
	{ 0x00000E28, 0x0FC8 },   /* R3624  - EQ2_3 */
	{ 0x00000E29, 0x03FE },   /* R3625  - EQ2_4 */
	{ 0x00000E2A, 0x00E0 },   /* R3626  - EQ2_5 */
	{ 0x00000E2B, 0x1EC4 },   /* R3627  - EQ2_6 */
	{ 0x00000E2C, 0xF136 },   /* R3628  - EQ2_7 */
	{ 0x00000E2D, 0x0409 },   /* R3629  - EQ2_8 */
	{ 0x00000E2E, 0x04CC },   /* R3630  - EQ2_9 */
	{ 0x00000E2F, 0x1C9B },   /* R3631  - EQ2_10 */
	{ 0x00000E30, 0xF337 },   /* R3632  - EQ2_11 */
	{ 0x00000E31, 0x040B },   /* R3633  - EQ2_12 */
	{ 0x00000E32, 0x0CBB },   /* R3634  - EQ2_13 */
	{ 0x00000E33, 0x16F8 },   /* R3635  - EQ2_14 */
	{ 0x00000E34, 0xF7D9 },   /* R3636  - EQ2_15 */
	{ 0x00000E35, 0x040A },   /* R3637  - EQ2_16 */
	{ 0x00000E36, 0x1F14 },   /* R3638  - EQ2_17 */
	{ 0x00000E37, 0x058C },   /* R3639  - EQ2_18 */
	{ 0x00000E38, 0x0563 },   /* R3640  - EQ2_19 */
	{ 0x00000E39, 0x4000 },   /* R3641  - EQ2_20 */
	{ 0x00000E3A, 0x0B75 },   /* R3642  - EQ2_21 */
	{ 0x00000E3C, 0x6318 },   /* R3644  - EQ3_1 */
	{ 0x00000E3D, 0x6300 },   /* R3645  - EQ3_2 */
	{ 0x00000E3E, 0x0FC8 },   /* R3646  - EQ3_3 */
	{ 0x00000E3F, 0x03FE },   /* R3647  - EQ3_4 */
	{ 0x00000E40, 0x00E0 },   /* R3648  - EQ3_5 */
	{ 0x00000E41, 0x1EC4 },   /* R3649  - EQ3_6 */
	{ 0x00000E42, 0xF136 },   /* R3650  - EQ3_7 */
	{ 0x00000E43, 0x0409 },   /* R3651  - EQ3_8 */
	{ 0x00000E44, 0x04CC },   /* R3652  - EQ3_9 */
	{ 0x00000E45, 0x1C9B },   /* R3653  - EQ3_10 */
	{ 0x00000E46, 0xF337 },   /* R3654  - EQ3_11 */
	{ 0x00000E47, 0x040B },   /* R3655  - EQ3_12 */
	{ 0x00000E48, 0x0CBB },   /* R3656  - EQ3_13 */
	{ 0x00000E49, 0x16F8 },   /* R3657  - EQ3_14 */
	{ 0x00000E4A, 0xF7D9 },   /* R3658  - EQ3_15 */
	{ 0x00000E4B, 0x040A },   /* R3659  - EQ3_16 */
	{ 0x00000E4C, 0x1F14 },   /* R3660  - EQ3_17 */
	{ 0x00000E4D, 0x058C },   /* R3661  - EQ3_18 */
	{ 0x00000E4E, 0x0563 },   /* R3662  - EQ3_19 */
	{ 0x00000E4F, 0x4000 },   /* R3663  - EQ3_20 */
	{ 0x00000E50, 0x0B75 },   /* R3664  - EQ3_21 */
	{ 0x00000E52, 0x6318 },   /* R3666  - EQ4_1 */
	{ 0x00000E53, 0x6300 },   /* R3667  - EQ4_2 */
	{ 0x00000E54, 0x0FC8 },   /* R3668  - EQ4_3 */
	{ 0x00000E55, 0x03FE },   /* R3669  - EQ4_4 */
	{ 0x00000E56, 0x00E0 },   /* R3670  - EQ4_5 */
	{ 0x00000E57, 0x1EC4 },   /* R3671  - EQ4_6 */
	{ 0x00000E58, 0xF136 },   /* R3672  - EQ4_7 */
	{ 0x00000E59, 0x0409 },   /* R3673  - EQ4_8 */
	{ 0x00000E5A, 0x04CC },   /* R3674  - EQ4_9 */
	{ 0x00000E5B, 0x1C9B },   /* R3675  - EQ4_10 */
	{ 0x00000E5C, 0xF337 },   /* R3676  - EQ4_11 */
	{ 0x00000E5D, 0x040B },   /* R3677  - EQ4_12 */
	{ 0x00000E5E, 0x0CBB },   /* R3678  - EQ4_13 */
	{ 0x00000E5F, 0x16F8 },   /* R3679  - EQ4_14 */
	{ 0x00000E60, 0xF7D9 },   /* R3680  - EQ4_15 */
	{ 0x00000E61, 0x040A },   /* R3681  - EQ4_16 */
	{ 0x00000E62, 0x1F14 },   /* R3682  - EQ4_17 */
	{ 0x00000E63, 0x058C },   /* R3683  - EQ4_18 */
	{ 0x00000E64, 0x0563 },   /* R3684  - EQ4_19 */
	{ 0x00000E65, 0x4000 },   /* R3685  - EQ4_20 */
	{ 0x00000E66, 0x0B75 },   /* R3686  - EQ4_21 */
	{ 0x00000E80, 0x0018 },   /* R3712  - DRC1 ctrl1 */
	{ 0x00000E81, 0x0933 },   /* R3713  - DRC1 ctrl2 */
	{ 0x00000E82, 0x0018 },   /* R3714  - DRC1 ctrl3 */
	{ 0x00000E83, 0x0000 },   /* R3715  - DRC1 ctrl4 */
	{ 0x00000E84, 0x0000 },   /* R3716  - DRC1 ctrl5 */
	{ 0x00000EC0, 0x0000 },   /* R3776  - HPLPF1_1 */
	{ 0x00000EC1, 0x0000 },   /* R3777  - HPLPF1_2 */
	{ 0x00000EC4, 0x0000 },   /* R3780  - HPLPF2_1 */
	{ 0x00000EC5, 0x0000 },   /* R3781  - HPLPF2_2 */
	{ 0x00000EC8, 0x0000 },   /* R3784  - HPLPF3_1 */
	{ 0x00000EC9, 0x0000 },   /* R3785  - HPLPF3_2 */
	{ 0x00000ECC, 0x0000 },   /* R3788  - HPLPF4_1 */
	{ 0x00000ECD, 0x0000 },   /* R3789  - HPLPF4_2 */
	{ 0x00000EE0, 0x0000 },   /* R3808  - ASRC_ENABLE */
	{ 0x00000EE2, 0x0000 },   /* R3810  - ASRC_RATE1 */
	{ 0x00000EE3, 0x4000 },   /* R3811  - ASRC_RATE2 */
	{ 0x00000EF0, 0x0000 },   /* R3824  - ISRC 1 CTRL 1 */
	{ 0x00000EF1, 0x0000 },   /* R3825  - ISRC 1 CTRL 2 */
	{ 0x00000EF2, 0x0000 },   /* R3826  - ISRC 1 CTRL 3 */
	{ 0x00000EF3, 0x0000 },   /* R3827  - ISRC 2 CTRL 1 */
	{ 0x00000EF4, 0x0000 },   /* R3828  - ISRC 2 CTRL 2 */
	{ 0x00000EF5, 0x0000 },   /* R3829  - ISRC 2 CTRL 3 */
	{ 0x00001100, 0x0010 },   /* R4352  - DSP1 Control 1 */
};

static bool wm5102_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ARIZONA_SOFTWARE_RESET:
	case ARIZONA_DEVICE_REVISION:
	case ARIZONA_CTRL_IF_SPI_CFG_1:
	case ARIZONA_CTRL_IF_I2C1_CFG_1:
	case ARIZONA_WRITE_SEQUENCER_CTRL_0:
	case ARIZONA_WRITE_SEQUENCER_CTRL_1:
	case ARIZONA_WRITE_SEQUENCER_CTRL_2:
	case ARIZONA_WRITE_SEQUENCER_CTRL_3:
	case ARIZONA_TONE_GENERATOR_1:
	case ARIZONA_TONE_GENERATOR_2:
	case ARIZONA_TONE_GENERATOR_3:
	case ARIZONA_TONE_GENERATOR_4:
	case ARIZONA_TONE_GENERATOR_5:
	case ARIZONA_PWM_DRIVE_1:
	case ARIZONA_PWM_DRIVE_2:
	case ARIZONA_PWM_DRIVE_3:
	case ARIZONA_WAKE_CONTROL:
	case ARIZONA_SEQUENCE_CONTROL:
	case ARIZONA_SAMPLE_RATE_SEQUENCE_SELECT_1:
	case ARIZONA_SAMPLE_RATE_SEQUENCE_SELECT_2:
	case ARIZONA_SAMPLE_RATE_SEQUENCE_SELECT_3:
	case ARIZONA_SAMPLE_RATE_SEQUENCE_SELECT_4:
	case ARIZONA_ALWAYS_ON_TRIGGERS_SEQUENCE_SELECT_1:
	case ARIZONA_ALWAYS_ON_TRIGGERS_SEQUENCE_SELECT_2:
	case ARIZONA_ALWAYS_ON_TRIGGERS_SEQUENCE_SELECT_3:
	case ARIZONA_ALWAYS_ON_TRIGGERS_SEQUENCE_SELECT_4:
	case ARIZONA_ALWAYS_ON_TRIGGERS_SEQUENCE_SELECT_5:
	case ARIZONA_ALWAYS_ON_TRIGGERS_SEQUENCE_SELECT_6:
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
	case ARIZONA_DYNAMIC_FREQUENCY_SCALING_1:
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
	case ARIZONA_MIC_CHARGE_PUMP_1:
	case ARIZONA_LDO1_CONTROL_1:
	case ARIZONA_LDO1_CONTROL_2:
	case ARIZONA_LDO2_CONTROL_1:
	case ARIZONA_MIC_BIAS_CTRL_1:
	case ARIZONA_MIC_BIAS_CTRL_2:
	case ARIZONA_MIC_BIAS_CTRL_3:
	case ARIZONA_HP_CTRL_1L:
	case ARIZONA_HP_CTRL_1R:
	case ARIZONA_ACCESSORY_DETECT_MODE_1:
	case ARIZONA_HEADPHONE_DETECT_1:
	case ARIZONA_HEADPHONE_DETECT_2:
	case ARIZONA_HP_DACVAL:
	case ARIZONA_MICD_CLAMP_CONTROL:
	case ARIZONA_MIC_DETECT_1:
	case ARIZONA_MIC_DETECT_2:
	case ARIZONA_MIC_DETECT_3:
	case ARIZONA_MIC_DETECT_LEVEL_1:
	case ARIZONA_MIC_DETECT_LEVEL_2:
	case ARIZONA_MIC_DETECT_LEVEL_3:
	case ARIZONA_MIC_DETECT_LEVEL_4:
	case ARIZONA_MIC_NOISE_MIX_CONTROL_1:
	case ARIZONA_ISOLATION_CONTROL:
	case ARIZONA_JACK_DETECT_ANALOGUE:
	case ARIZONA_INPUT_ENABLES:
	case ARIZONA_INPUT_RATE:
	case ARIZONA_INPUT_VOLUME_RAMP:
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
	case ARIZONA_IN3L_CONTROL:
	case ARIZONA_ADC_DIGITAL_VOLUME_3L:
	case ARIZONA_DMIC3L_CONTROL:
	case ARIZONA_IN3R_CONTROL:
	case ARIZONA_ADC_DIGITAL_VOLUME_3R:
	case ARIZONA_DMIC3R_CONTROL:
	case ARIZONA_OUTPUT_ENABLES_1:
	case ARIZONA_OUTPUT_STATUS_1:
	case ARIZONA_OUTPUT_RATE_1:
	case ARIZONA_OUTPUT_VOLUME_RAMP:
	case ARIZONA_OUTPUT_PATH_CONFIG_1L:
	case ARIZONA_DAC_DIGITAL_VOLUME_1L:
	case ARIZONA_DAC_VOLUME_LIMIT_1L:
	case ARIZONA_NOISE_GATE_SELECT_1L:
	case ARIZONA_OUTPUT_PATH_CONFIG_1R:
	case ARIZONA_DAC_DIGITAL_VOLUME_1R:
	case ARIZONA_DAC_VOLUME_LIMIT_1R:
	case ARIZONA_NOISE_GATE_SELECT_1R:
	case ARIZONA_OUTPUT_PATH_CONFIG_2L:
	case ARIZONA_DAC_DIGITAL_VOLUME_2L:
	case ARIZONA_DAC_VOLUME_LIMIT_2L:
	case ARIZONA_NOISE_GATE_SELECT_2L:
	case ARIZONA_OUTPUT_PATH_CONFIG_2R:
	case ARIZONA_DAC_DIGITAL_VOLUME_2R:
	case ARIZONA_DAC_VOLUME_LIMIT_2R:
	case ARIZONA_NOISE_GATE_SELECT_2R:
	case ARIZONA_OUTPUT_PATH_CONFIG_3L:
	case ARIZONA_DAC_DIGITAL_VOLUME_3L:
	case ARIZONA_DAC_VOLUME_LIMIT_3L:
	case ARIZONA_NOISE_GATE_SELECT_3L:
	case ARIZONA_OUTPUT_PATH_CONFIG_4L:
	case ARIZONA_DAC_DIGITAL_VOLUME_4L:
	case ARIZONA_OUT_VOLUME_4L:
	case ARIZONA_NOISE_GATE_SELECT_4L:
	case ARIZONA_DAC_DIGITAL_VOLUME_4R:
	case ARIZONA_OUT_VOLUME_4R:
	case ARIZONA_NOISE_GATE_SELECT_4R:
	case ARIZONA_OUTPUT_PATH_CONFIG_5L:
	case ARIZONA_DAC_DIGITAL_VOLUME_5L:
	case ARIZONA_DAC_VOLUME_LIMIT_5L:
	case ARIZONA_NOISE_GATE_SELECT_5L:
	case ARIZONA_DAC_DIGITAL_VOLUME_5R:
	case ARIZONA_DAC_VOLUME_LIMIT_5R:
	case ARIZONA_NOISE_GATE_SELECT_5R:
	case ARIZONA_DRE_ENABLE:
	case ARIZONA_DRE_CONTROL_2:
	case ARIZONA_DRE_CONTROL_3:
	case ARIZONA_DAC_AEC_CONTROL_1:
	case ARIZONA_NOISE_GATE_CONTROL:
	case ARIZONA_PDM_SPK1_CTRL_1:
	case ARIZONA_PDM_SPK1_CTRL_2:
	case ARIZONA_DAC_COMP_1:
	case ARIZONA_DAC_COMP_2:
	case ARIZONA_DAC_COMP_3:
	case ARIZONA_DAC_COMP_4:
	case ARIZONA_AIF1_BCLK_CTRL:
	case ARIZONA_AIF1_TX_PIN_CTRL:
	case ARIZONA_AIF1_RX_PIN_CTRL:
	case ARIZONA_AIF1_RATE_CTRL:
	case ARIZONA_AIF1_FORMAT:
	case ARIZONA_AIF1_TX_BCLK_RATE:
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
	case ARIZONA_AIF2_TX_BCLK_RATE:
	case ARIZONA_AIF2_RX_BCLK_RATE:
	case ARIZONA_AIF2_FRAME_CTRL_1:
	case ARIZONA_AIF2_FRAME_CTRL_2:
	case ARIZONA_AIF2_FRAME_CTRL_3:
	case ARIZONA_AIF2_FRAME_CTRL_4:
	case ARIZONA_AIF2_FRAME_CTRL_11:
	case ARIZONA_AIF2_FRAME_CTRL_12:
	case ARIZONA_AIF2_TX_ENABLES:
	case ARIZONA_AIF2_RX_ENABLES:
	case ARIZONA_AIF3_BCLK_CTRL:
	case ARIZONA_AIF3_TX_PIN_CTRL:
	case ARIZONA_AIF3_RX_PIN_CTRL:
	case ARIZONA_AIF3_RATE_CTRL:
	case ARIZONA_AIF3_FORMAT:
	case ARIZONA_AIF3_TX_BCLK_RATE:
	case ARIZONA_AIF3_RX_BCLK_RATE:
	case ARIZONA_AIF3_FRAME_CTRL_1:
	case ARIZONA_AIF3_FRAME_CTRL_2:
	case ARIZONA_AIF3_FRAME_CTRL_3:
	case ARIZONA_AIF3_FRAME_CTRL_4:
	case ARIZONA_AIF3_FRAME_CTRL_11:
	case ARIZONA_AIF3_FRAME_CTRL_12:
	case ARIZONA_AIF3_TX_ENABLES:
	case ARIZONA_AIF3_RX_ENABLES:
	case ARIZONA_SLIMBUS_FRAMER_REF_GEAR:
	case ARIZONA_SLIMBUS_RATES_1:
	case ARIZONA_SLIMBUS_RATES_2:
	case ARIZONA_SLIMBUS_RATES_3:
	case ARIZONA_SLIMBUS_RATES_4:
	case ARIZONA_SLIMBUS_RATES_5:
	case ARIZONA_SLIMBUS_RATES_6:
	case ARIZONA_SLIMBUS_RATES_7:
	case ARIZONA_SLIMBUS_RATES_8:
	case ARIZONA_SLIMBUS_RX_CHANNEL_ENABLE:
	case ARIZONA_SLIMBUS_TX_CHANNEL_ENABLE:
	case ARIZONA_SLIMBUS_RX_PORT_STATUS:
	case ARIZONA_SLIMBUS_TX_PORT_STATUS:
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
	case ARIZONA_MICMIX_INPUT_1_SOURCE:
	case ARIZONA_MICMIX_INPUT_1_VOLUME:
	case ARIZONA_MICMIX_INPUT_2_SOURCE:
	case ARIZONA_MICMIX_INPUT_2_VOLUME:
	case ARIZONA_MICMIX_INPUT_3_SOURCE:
	case ARIZONA_MICMIX_INPUT_3_VOLUME:
	case ARIZONA_MICMIX_INPUT_4_SOURCE:
	case ARIZONA_MICMIX_INPUT_4_VOLUME:
	case ARIZONA_NOISEMIX_INPUT_1_SOURCE:
	case ARIZONA_NOISEMIX_INPUT_1_VOLUME:
	case ARIZONA_NOISEMIX_INPUT_2_SOURCE:
	case ARIZONA_NOISEMIX_INPUT_2_VOLUME:
	case ARIZONA_NOISEMIX_INPUT_3_SOURCE:
	case ARIZONA_NOISEMIX_INPUT_3_VOLUME:
	case ARIZONA_NOISEMIX_INPUT_4_SOURCE:
	case ARIZONA_NOISEMIX_INPUT_4_VOLUME:
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
	case ARIZONA_OUT2LMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT2LMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT2LMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT2LMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT2LMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT2LMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT2LMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT2LMIX_INPUT_4_VOLUME:
	case ARIZONA_OUT2RMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT2RMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT2RMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT2RMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT2RMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT2RMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT2RMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT2RMIX_INPUT_4_VOLUME:
	case ARIZONA_OUT3LMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT3LMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT3LMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT3LMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT3LMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT3LMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT3LMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT3LMIX_INPUT_4_VOLUME:
	case ARIZONA_OUT4LMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT4LMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT4LMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT4LMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT4LMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT4LMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT4LMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT4LMIX_INPUT_4_VOLUME:
	case ARIZONA_OUT4RMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT4RMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT4RMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT4RMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT4RMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT4RMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT4RMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT4RMIX_INPUT_4_VOLUME:
	case ARIZONA_OUT5LMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT5LMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT5LMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT5LMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT5LMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT5LMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT5LMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT5LMIX_INPUT_4_VOLUME:
	case ARIZONA_OUT5RMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT5RMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT5RMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT5RMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT5RMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT5RMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT5RMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT5RMIX_INPUT_4_VOLUME:
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
	case ARIZONA_SLIMTX1MIX_INPUT_1_SOURCE:
	case ARIZONA_SLIMTX1MIX_INPUT_1_VOLUME:
	case ARIZONA_SLIMTX1MIX_INPUT_2_SOURCE:
	case ARIZONA_SLIMTX1MIX_INPUT_2_VOLUME:
	case ARIZONA_SLIMTX1MIX_INPUT_3_SOURCE:
	case ARIZONA_SLIMTX1MIX_INPUT_3_VOLUME:
	case ARIZONA_SLIMTX1MIX_INPUT_4_SOURCE:
	case ARIZONA_SLIMTX1MIX_INPUT_4_VOLUME:
	case ARIZONA_SLIMTX2MIX_INPUT_1_SOURCE:
	case ARIZONA_SLIMTX2MIX_INPUT_1_VOLUME:
	case ARIZONA_SLIMTX2MIX_INPUT_2_SOURCE:
	case ARIZONA_SLIMTX2MIX_INPUT_2_VOLUME:
	case ARIZONA_SLIMTX2MIX_INPUT_3_SOURCE:
	case ARIZONA_SLIMTX2MIX_INPUT_3_VOLUME:
	case ARIZONA_SLIMTX2MIX_INPUT_4_SOURCE:
	case ARIZONA_SLIMTX2MIX_INPUT_4_VOLUME:
	case ARIZONA_SLIMTX3MIX_INPUT_1_SOURCE:
	case ARIZONA_SLIMTX3MIX_INPUT_1_VOLUME:
	case ARIZONA_SLIMTX3MIX_INPUT_2_SOURCE:
	case ARIZONA_SLIMTX3MIX_INPUT_2_VOLUME:
	case ARIZONA_SLIMTX3MIX_INPUT_3_SOURCE:
	case ARIZONA_SLIMTX3MIX_INPUT_3_VOLUME:
	case ARIZONA_SLIMTX3MIX_INPUT_4_SOURCE:
	case ARIZONA_SLIMTX3MIX_INPUT_4_VOLUME:
	case ARIZONA_SLIMTX4MIX_INPUT_1_SOURCE:
	case ARIZONA_SLIMTX4MIX_INPUT_1_VOLUME:
	case ARIZONA_SLIMTX4MIX_INPUT_2_SOURCE:
	case ARIZONA_SLIMTX4MIX_INPUT_2_VOLUME:
	case ARIZONA_SLIMTX4MIX_INPUT_3_SOURCE:
	case ARIZONA_SLIMTX4MIX_INPUT_3_VOLUME:
	case ARIZONA_SLIMTX4MIX_INPUT_4_SOURCE:
	case ARIZONA_SLIMTX4MIX_INPUT_4_VOLUME:
	case ARIZONA_SLIMTX5MIX_INPUT_1_SOURCE:
	case ARIZONA_SLIMTX5MIX_INPUT_1_VOLUME:
	case ARIZONA_SLIMTX5MIX_INPUT_2_SOURCE:
	case ARIZONA_SLIMTX5MIX_INPUT_2_VOLUME:
	case ARIZONA_SLIMTX5MIX_INPUT_3_SOURCE:
	case ARIZONA_SLIMTX5MIX_INPUT_3_VOLUME:
	case ARIZONA_SLIMTX5MIX_INPUT_4_SOURCE:
	case ARIZONA_SLIMTX5MIX_INPUT_4_VOLUME:
	case ARIZONA_SLIMTX6MIX_INPUT_1_SOURCE:
	case ARIZONA_SLIMTX6MIX_INPUT_1_VOLUME:
	case ARIZONA_SLIMTX6MIX_INPUT_2_SOURCE:
	case ARIZONA_SLIMTX6MIX_INPUT_2_VOLUME:
	case ARIZONA_SLIMTX6MIX_INPUT_3_SOURCE:
	case ARIZONA_SLIMTX6MIX_INPUT_3_VOLUME:
	case ARIZONA_SLIMTX6MIX_INPUT_4_SOURCE:
	case ARIZONA_SLIMTX6MIX_INPUT_4_VOLUME:
	case ARIZONA_SLIMTX7MIX_INPUT_1_SOURCE:
	case ARIZONA_SLIMTX7MIX_INPUT_1_VOLUME:
	case ARIZONA_SLIMTX7MIX_INPUT_2_SOURCE:
	case ARIZONA_SLIMTX7MIX_INPUT_2_VOLUME:
	case ARIZONA_SLIMTX7MIX_INPUT_3_SOURCE:
	case ARIZONA_SLIMTX7MIX_INPUT_3_VOLUME:
	case ARIZONA_SLIMTX7MIX_INPUT_4_SOURCE:
	case ARIZONA_SLIMTX7MIX_INPUT_4_VOLUME:
	case ARIZONA_SLIMTX8MIX_INPUT_1_SOURCE:
	case ARIZONA_SLIMTX8MIX_INPUT_1_VOLUME:
	case ARIZONA_SLIMTX8MIX_INPUT_2_SOURCE:
	case ARIZONA_SLIMTX8MIX_INPUT_2_VOLUME:
	case ARIZONA_SLIMTX8MIX_INPUT_3_SOURCE:
	case ARIZONA_SLIMTX8MIX_INPUT_3_VOLUME:
	case ARIZONA_SLIMTX8MIX_INPUT_4_SOURCE:
	case ARIZONA_SLIMTX8MIX_INPUT_4_VOLUME:
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
	case ARIZONA_EQ3MIX_INPUT_1_SOURCE:
	case ARIZONA_EQ3MIX_INPUT_1_VOLUME:
	case ARIZONA_EQ3MIX_INPUT_2_SOURCE:
	case ARIZONA_EQ3MIX_INPUT_2_VOLUME:
	case ARIZONA_EQ3MIX_INPUT_3_SOURCE:
	case ARIZONA_EQ3MIX_INPUT_3_VOLUME:
	case ARIZONA_EQ3MIX_INPUT_4_SOURCE:
	case ARIZONA_EQ3MIX_INPUT_4_VOLUME:
	case ARIZONA_EQ4MIX_INPUT_1_SOURCE:
	case ARIZONA_EQ4MIX_INPUT_1_VOLUME:
	case ARIZONA_EQ4MIX_INPUT_2_SOURCE:
	case ARIZONA_EQ4MIX_INPUT_2_VOLUME:
	case ARIZONA_EQ4MIX_INPUT_3_SOURCE:
	case ARIZONA_EQ4MIX_INPUT_3_VOLUME:
	case ARIZONA_EQ4MIX_INPUT_4_SOURCE:
	case ARIZONA_EQ4MIX_INPUT_4_VOLUME:
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
	case ARIZONA_DSP1LMIX_INPUT_1_SOURCE:
	case ARIZONA_DSP1LMIX_INPUT_1_VOLUME:
	case ARIZONA_DSP1LMIX_INPUT_2_SOURCE:
	case ARIZONA_DSP1LMIX_INPUT_2_VOLUME:
	case ARIZONA_DSP1LMIX_INPUT_3_SOURCE:
	case ARIZONA_DSP1LMIX_INPUT_3_VOLUME:
	case ARIZONA_DSP1LMIX_INPUT_4_SOURCE:
	case ARIZONA_DSP1LMIX_INPUT_4_VOLUME:
	case ARIZONA_DSP1RMIX_INPUT_1_SOURCE:
	case ARIZONA_DSP1RMIX_INPUT_1_VOLUME:
	case ARIZONA_DSP1RMIX_INPUT_2_SOURCE:
	case ARIZONA_DSP1RMIX_INPUT_2_VOLUME:
	case ARIZONA_DSP1RMIX_INPUT_3_SOURCE:
	case ARIZONA_DSP1RMIX_INPUT_3_VOLUME:
	case ARIZONA_DSP1RMIX_INPUT_4_SOURCE:
	case ARIZONA_DSP1RMIX_INPUT_4_VOLUME:
	case ARIZONA_DSP1AUX1MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP1AUX2MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP1AUX3MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP1AUX4MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP1AUX5MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP1AUX6MIX_INPUT_1_SOURCE:
	case ARIZONA_ASRC1LMIX_INPUT_1_SOURCE:
	case ARIZONA_ASRC1RMIX_INPUT_1_SOURCE:
	case ARIZONA_ASRC2LMIX_INPUT_1_SOURCE:
	case ARIZONA_ASRC2RMIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1DEC1MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1DEC2MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1INT1MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC1INT2MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2DEC1MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2DEC2MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2INT1MIX_INPUT_1_SOURCE:
	case ARIZONA_ISRC2INT2MIX_INPUT_1_SOURCE:
	case ARIZONA_GPIO1_CTRL:
	case ARIZONA_GPIO2_CTRL:
	case ARIZONA_GPIO3_CTRL:
	case ARIZONA_GPIO4_CTRL:
	case ARIZONA_GPIO5_CTRL:
	case ARIZONA_IRQ_CTRL_1:
	case ARIZONA_GPIO_DEBOUNCE_CONFIG:
	case ARIZONA_MISC_PAD_CTRL_1:
	case ARIZONA_MISC_PAD_CTRL_2:
	case ARIZONA_MISC_PAD_CTRL_3:
	case ARIZONA_MISC_PAD_CTRL_4:
	case ARIZONA_MISC_PAD_CTRL_5:
	case ARIZONA_MISC_PAD_CTRL_6:
	case ARIZONA_INTERRUPT_STATUS_1:
	case ARIZONA_INTERRUPT_STATUS_2:
	case ARIZONA_INTERRUPT_STATUS_3:
	case ARIZONA_INTERRUPT_STATUS_4:
	case ARIZONA_INTERRUPT_STATUS_5:
	case ARIZONA_INTERRUPT_STATUS_1_MASK:
	case ARIZONA_INTERRUPT_STATUS_2_MASK:
	case ARIZONA_INTERRUPT_STATUS_3_MASK:
	case ARIZONA_INTERRUPT_STATUS_4_MASK:
	case ARIZONA_INTERRUPT_STATUS_5_MASK:
	case ARIZONA_INTERRUPT_CONTROL:
	case ARIZONA_IRQ2_STATUS_1:
	case ARIZONA_IRQ2_STATUS_2:
	case ARIZONA_IRQ2_STATUS_3:
	case ARIZONA_IRQ2_STATUS_4:
	case ARIZONA_IRQ2_STATUS_5:
	case ARIZONA_IRQ2_STATUS_1_MASK:
	case ARIZONA_IRQ2_STATUS_2_MASK:
	case ARIZONA_IRQ2_STATUS_3_MASK:
	case ARIZONA_IRQ2_STATUS_4_MASK:
	case ARIZONA_IRQ2_STATUS_5_MASK:
	case ARIZONA_IRQ2_CONTROL:
	case ARIZONA_INTERRUPT_RAW_STATUS_2:
	case ARIZONA_INTERRUPT_RAW_STATUS_3:
	case ARIZONA_INTERRUPT_RAW_STATUS_4:
	case ARIZONA_INTERRUPT_RAW_STATUS_5:
	case ARIZONA_INTERRUPT_RAW_STATUS_6:
	case ARIZONA_INTERRUPT_RAW_STATUS_7:
	case ARIZONA_INTERRUPT_RAW_STATUS_8:
	case ARIZONA_IRQ_PIN_STATUS:
	case ARIZONA_ADSP2_IRQ0:
	case ARIZONA_AOD_WKUP_AND_TRIG:
	case ARIZONA_AOD_IRQ1:
	case ARIZONA_AOD_IRQ2:
	case ARIZONA_AOD_IRQ_MASK_IRQ1:
	case ARIZONA_AOD_IRQ_MASK_IRQ2:
	case ARIZONA_AOD_IRQ_RAW_STATUS:
	case ARIZONA_JACK_DETECT_DEBOUNCE:
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
	case ARIZONA_EQ3_1:
	case ARIZONA_EQ3_2:
	case ARIZONA_EQ3_3:
	case ARIZONA_EQ3_4:
	case ARIZONA_EQ3_5:
	case ARIZONA_EQ3_6:
	case ARIZONA_EQ3_7:
	case ARIZONA_EQ3_8:
	case ARIZONA_EQ3_9:
	case ARIZONA_EQ3_10:
	case ARIZONA_EQ3_11:
	case ARIZONA_EQ3_12:
	case ARIZONA_EQ3_13:
	case ARIZONA_EQ3_14:
	case ARIZONA_EQ3_15:
	case ARIZONA_EQ3_16:
	case ARIZONA_EQ3_17:
	case ARIZONA_EQ3_18:
	case ARIZONA_EQ3_19:
	case ARIZONA_EQ3_20:
	case ARIZONA_EQ3_21:
	case ARIZONA_EQ4_1:
	case ARIZONA_EQ4_2:
	case ARIZONA_EQ4_3:
	case ARIZONA_EQ4_4:
	case ARIZONA_EQ4_5:
	case ARIZONA_EQ4_6:
	case ARIZONA_EQ4_7:
	case ARIZONA_EQ4_8:
	case ARIZONA_EQ4_9:
	case ARIZONA_EQ4_10:
	case ARIZONA_EQ4_11:
	case ARIZONA_EQ4_12:
	case ARIZONA_EQ4_13:
	case ARIZONA_EQ4_14:
	case ARIZONA_EQ4_15:
	case ARIZONA_EQ4_16:
	case ARIZONA_EQ4_17:
	case ARIZONA_EQ4_18:
	case ARIZONA_EQ4_19:
	case ARIZONA_EQ4_20:
	case ARIZONA_EQ4_21:
	case ARIZONA_DRC1_CTRL1:
	case ARIZONA_DRC1_CTRL2:
	case ARIZONA_DRC1_CTRL3:
	case ARIZONA_DRC1_CTRL4:
	case ARIZONA_DRC1_CTRL5:
	case ARIZONA_HPLPF1_1:
	case ARIZONA_HPLPF1_2:
	case ARIZONA_HPLPF2_1:
	case ARIZONA_HPLPF2_2:
	case ARIZONA_HPLPF3_1:
	case ARIZONA_HPLPF3_2:
	case ARIZONA_HPLPF4_1:
	case ARIZONA_HPLPF4_2:
	case ARIZONA_ASRC_ENABLE:
	case ARIZONA_ASRC_RATE1:
	case ARIZONA_ASRC_RATE2:
	case ARIZONA_ISRC_1_CTRL_1:
	case ARIZONA_ISRC_1_CTRL_2:
	case ARIZONA_ISRC_1_CTRL_3:
	case ARIZONA_ISRC_2_CTRL_1:
	case ARIZONA_ISRC_2_CTRL_2:
	case ARIZONA_ISRC_2_CTRL_3:
	case ARIZONA_DSP1_CONTROL_1:
	case ARIZONA_DSP1_CLOCKING_1:
	case ARIZONA_DSP1_STATUS_1:
	case ARIZONA_DSP1_STATUS_2:
	case ARIZONA_DSP1_STATUS_3:
	case ARIZONA_DSP1_WDMA_BUFFER_1:
	case ARIZONA_DSP1_WDMA_BUFFER_2:
	case ARIZONA_DSP1_WDMA_BUFFER_3:
	case ARIZONA_DSP1_WDMA_BUFFER_4:
	case ARIZONA_DSP1_WDMA_BUFFER_5:
	case ARIZONA_DSP1_WDMA_BUFFER_6:
	case ARIZONA_DSP1_WDMA_BUFFER_7:
	case ARIZONA_DSP1_WDMA_BUFFER_8:
	case ARIZONA_DSP1_RDMA_BUFFER_1:
	case ARIZONA_DSP1_RDMA_BUFFER_2:
	case ARIZONA_DSP1_RDMA_BUFFER_3:
	case ARIZONA_DSP1_RDMA_BUFFER_4:
	case ARIZONA_DSP1_RDMA_BUFFER_5:
	case ARIZONA_DSP1_RDMA_BUFFER_6:
	case ARIZONA_DSP1_WDMA_CONFIG_1:
	case ARIZONA_DSP1_WDMA_CONFIG_2:
	case ARIZONA_DSP1_RDMA_CONFIG_1:
	case ARIZONA_DSP1_SCRATCH_0:
	case ARIZONA_DSP1_SCRATCH_1:
	case ARIZONA_DSP1_SCRATCH_2:
	case ARIZONA_DSP1_SCRATCH_3:
		return true;
	default:
		if ((reg >= 0x100000 && reg < 0x106000) ||
		    (reg >= 0x180000 && reg < 0x180800) ||
		    (reg >= 0x190000 && reg < 0x194800) ||
		    (reg >= 0x1a8000 && reg < 0x1a9800))
			return true;
		else
			return false;
	}
}

static bool wm5102_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ARIZONA_SOFTWARE_RESET:
	case ARIZONA_DEVICE_REVISION:
	case ARIZONA_WRITE_SEQUENCER_CTRL_0:
	case ARIZONA_WRITE_SEQUENCER_CTRL_1:
	case ARIZONA_WRITE_SEQUENCER_CTRL_2:
	case ARIZONA_WRITE_SEQUENCER_CTRL_3:
	case ARIZONA_OUTPUT_STATUS_1:
	case ARIZONA_SLIMBUS_RX_PORT_STATUS:
	case ARIZONA_SLIMBUS_TX_PORT_STATUS:
	case ARIZONA_SAMPLE_RATE_1_STATUS:
	case ARIZONA_SAMPLE_RATE_2_STATUS:
	case ARIZONA_SAMPLE_RATE_3_STATUS:
	case ARIZONA_HAPTICS_STATUS:
	case ARIZONA_ASYNC_SAMPLE_RATE_1_STATUS:
	case ARIZONA_ASYNC_SAMPLE_RATE_2_STATUS:
	case ARIZONA_DAC_COMP_1:
	case ARIZONA_DAC_COMP_2:
	case ARIZONA_DAC_COMP_3:
	case ARIZONA_DAC_COMP_4:
	case ARIZONA_FX_CTRL2:
	case ARIZONA_INTERRUPT_STATUS_1:
	case ARIZONA_INTERRUPT_STATUS_2:
	case ARIZONA_INTERRUPT_STATUS_3:
	case ARIZONA_INTERRUPT_STATUS_4:
	case ARIZONA_INTERRUPT_STATUS_5:
	case ARIZONA_IRQ2_STATUS_1:
	case ARIZONA_IRQ2_STATUS_2:
	case ARIZONA_IRQ2_STATUS_3:
	case ARIZONA_IRQ2_STATUS_4:
	case ARIZONA_IRQ2_STATUS_5:
	case ARIZONA_INTERRUPT_RAW_STATUS_2:
	case ARIZONA_INTERRUPT_RAW_STATUS_3:
	case ARIZONA_INTERRUPT_RAW_STATUS_4:
	case ARIZONA_INTERRUPT_RAW_STATUS_5:
	case ARIZONA_INTERRUPT_RAW_STATUS_6:
	case ARIZONA_INTERRUPT_RAW_STATUS_7:
	case ARIZONA_INTERRUPT_RAW_STATUS_8:
	case ARIZONA_IRQ_PIN_STATUS:
	case ARIZONA_AOD_WKUP_AND_TRIG:
	case ARIZONA_AOD_IRQ1:
	case ARIZONA_AOD_IRQ2:
	case ARIZONA_AOD_IRQ_RAW_STATUS:
	case ARIZONA_DSP1_CLOCKING_1:
	case ARIZONA_DSP1_STATUS_1:
	case ARIZONA_DSP1_STATUS_2:
	case ARIZONA_DSP1_STATUS_3:
	case ARIZONA_DSP1_WDMA_BUFFER_1:
	case ARIZONA_DSP1_WDMA_BUFFER_2:
	case ARIZONA_DSP1_WDMA_BUFFER_3:
	case ARIZONA_DSP1_WDMA_BUFFER_4:
	case ARIZONA_DSP1_WDMA_BUFFER_5:
	case ARIZONA_DSP1_WDMA_BUFFER_6:
	case ARIZONA_DSP1_WDMA_BUFFER_7:
	case ARIZONA_DSP1_WDMA_BUFFER_8:
	case ARIZONA_DSP1_RDMA_BUFFER_1:
	case ARIZONA_DSP1_RDMA_BUFFER_2:
	case ARIZONA_DSP1_RDMA_BUFFER_3:
	case ARIZONA_DSP1_RDMA_BUFFER_4:
	case ARIZONA_DSP1_RDMA_BUFFER_5:
	case ARIZONA_DSP1_RDMA_BUFFER_6:
	case ARIZONA_DSP1_WDMA_CONFIG_1:
	case ARIZONA_DSP1_WDMA_CONFIG_2:
	case ARIZONA_DSP1_RDMA_CONFIG_1:
	case ARIZONA_DSP1_SCRATCH_0:
	case ARIZONA_DSP1_SCRATCH_1:
	case ARIZONA_DSP1_SCRATCH_2:
	case ARIZONA_DSP1_SCRATCH_3:
	case ARIZONA_HP_CTRL_1L:
	case ARIZONA_HP_CTRL_1R:
	case ARIZONA_HEADPHONE_DETECT_2:
	case ARIZONA_HP_DACVAL:
	case ARIZONA_MIC_DETECT_3:
		return true;
	default:
		if ((reg >= 0x100000 && reg < 0x106000) ||
		    (reg >= 0x180000 && reg < 0x180800) ||
		    (reg >= 0x190000 && reg < 0x194800) ||
		    (reg >= 0x1a8000 && reg < 0x1a9800))
			return true;
		else
			return false;
	}
}

#define WM5102_MAX_REGISTER 0x1a9800

const struct regmap_config wm5102_spi_regmap = {
	.reg_bits = 32,
	.pad_bits = 16,
	.val_bits = 16,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = WM5102_MAX_REGISTER,
	.readable_reg = wm5102_readable_register,
	.volatile_reg = wm5102_volatile_register,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm5102_reg_default,
	.num_reg_defaults = ARRAY_SIZE(wm5102_reg_default),
};
EXPORT_SYMBOL_GPL(wm5102_spi_regmap);

const struct regmap_config wm5102_i2c_regmap = {
	.reg_bits = 32,
	.val_bits = 16,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = WM5102_MAX_REGISTER,
	.readable_reg = wm5102_readable_register,
	.volatile_reg = wm5102_volatile_register,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm5102_reg_default,
	.num_reg_defaults = ARRAY_SIZE(wm5102_reg_default),
};
EXPORT_SYMBOL_GPL(wm5102_i2c_regmap);
