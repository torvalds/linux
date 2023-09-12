// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regmap tables for CS47L92 codec
 *
 * Copyright (C) 2016-2019 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 *
 * Author: Stuart Henderson <stuarth@opensource.cirrus.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/mfd/madera/core.h>
#include <linux/mfd/madera/registers.h>

#include "madera.h"

static const struct reg_sequence cs47l92_reva_16_patch[] = {
	{ 0x3A2,  0x2C29 },
	{ 0x3A3,  0x0E00 },
	{ 0x281,  0x0000 },
	{ 0x282,  0x0000 },
	{ 0x4EA,  0x0100 },
	{ 0x22B,  0x0000 },
	{ 0x4A0,  0x0080 },
	{ 0x4A1,  0x0000 },
	{ 0x4A2,  0x0000 },
	{ 0x180B, 0x033F },
	{ 0x190B, 0x033F },
	{ 0x442,  0x0304 },
	{ 0x34C,  0x0003 },
	{ 0x124,  0x0C49 },
	{ 0x120,  0x0345 },
	{ 0x120,  0x0305 },
	{ 0x4FA,  0x5064 },
	{ 0x1300, 0x050E },
	{ 0x1302, 0x0101 },
	{ 0x1380, 0x02E0 },
	{ 0x1381, 0xF942 },
	{ 0x1382, 0x04CE },
	{ 0x1383, 0xFF06 },
	{ 0x1390, 0x0304 },
	{ 0x1391, 0xF8FF },
	{ 0x1392, 0x04F3 },
	{ 0x1393, 0xFF00 },
	{ 0x13A0, 0x02E0 },
	{ 0x13A1, 0xF942 },
	{ 0x13A2, 0x04CE },
	{ 0x13A3, 0xFF06 },
	{ 0x13B0, 0x0304 },
	{ 0x13B1, 0xF8FF },
	{ 0x13B2, 0x04F3 },
	{ 0x13B3, 0xFF00 },
	{ 0x412,  0x0005 },
	{ 0x41A,  0x0005 },
	{ 0x422,  0x0005 },
};

static const struct reg_sequence cs47l92_reva_32_patch[] = {
	{ 0x3030, 0x04A00C01 },
	{ 0x3032, 0x0225F501 },
	{ 0x3044, 0x04A00C00 },
	{ 0x3046, 0x0225FF01 },
	{ 0x3080, 0x04A00C01 },
	{ 0x3082, 0x0226F501 },
	{ 0x3094, 0x04A00C00 },
	{ 0x3096, 0x0226FF01 },
	{ 0x30D1, 0x04A10C01 },
	{ 0x30D2, 0x0227F501 },
	{ 0x30E4, 0x04A10C00 },
	{ 0x30E6, 0x0227FF01 },
	{ 0x3120, 0x04A10C01 },
	{ 0x3122, 0x0228F501 },
	{ 0x3134, 0x04A10C00 },
	{ 0x3136, 0x0228FF01 },
	{ 0x3170, 0x04A20C01 },
	{ 0x3172, 0x022B0101 },
	{ 0x3174, 0x0229F501 },
	{ 0x3184, 0x04A20C00 },
	{ 0x3186, 0x022B0100 },
	{ 0x3188, 0x0229FF01 },
	{ 0x31C0, 0x04A20C01 },
	{ 0x31C2, 0x022B0001 },
	{ 0x31C4, 0x022AF501 },
	{ 0x31D4, 0x04A20C00 },
	{ 0x31D6, 0x022B0000 },
	{ 0x31D8, 0x022AFF01 },
};

int cs47l92_patch(struct madera *madera)
{
	int ret;

	ret = regmap_register_patch(madera->regmap,
				    cs47l92_reva_16_patch,
				    ARRAY_SIZE(cs47l92_reva_16_patch));
	if (ret < 0) {
		dev_err(madera->dev,
			"Error in applying 16-bit patch: %d\n", ret);
		return ret;
	}

	ret = regmap_register_patch(madera->regmap_32bit,
				    cs47l92_reva_32_patch,
				    ARRAY_SIZE(cs47l92_reva_32_patch));
	if (ret < 0) {
		dev_err(madera->dev,
			"Error in applying 32-bit patch: %d\n", ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cs47l92_patch);

static const struct reg_default cs47l92_reg_default[] = {
	{ 0x00000020, 0x0000 }, /* R32 (0x20) - Tone Generator 1 */
	{ 0x00000021, 0x1000 }, /* R33 (0x21) - Tone Generator 2 */
	{ 0x00000022, 0x0000 }, /* R34 (0x22) - Tone Generator 3 */
	{ 0x00000023, 0x1000 }, /* R35 (0x23) - Tone Generator 4 */
	{ 0x00000024, 0x0000 }, /* R36 (0x24) - Tone Generator 5 */
	{ 0x00000030, 0x0000 }, /* R48 (0x30) - PWM Drive 1 */
	{ 0x00000031, 0x0100 }, /* R49 (0x31) - PWM Drive 2 */
	{ 0x00000032, 0x0100 }, /* R50 (0x32) - PWM Drive 3 */
	{ 0x00000061, 0x01ff }, /* R97 (0x61) - Sample Rate Sequence Select 1 */
	{ 0x00000062, 0x01ff }, /* R98 (0x62) - Sample Rate Sequence Select 2 */
	{ 0x00000063, 0x01ff }, /* R99 (0x63) - Sample Rate Sequence Select 3 */
	{ 0x00000064, 0x01ff }, /* R100 (0x64) - Sample Rate Sequence Select 4 */
	{ 0x00000090, 0x0000 }, /* R144 (0x90) - Haptics Control 1 */
	{ 0x00000091, 0x7fff }, /* R145 (0x91) - Haptics Control 2 */
	{ 0x00000092, 0x0000 }, /* R146 (0x92) - Haptics Phase 1 Intensity */
	{ 0x00000093, 0x0000 }, /* R147 (0x93) - Haptics Phase 1 Duration */
	{ 0x00000094, 0x0000 }, /* R148 (0x94) - Haptics Phase 2 Intensity */
	{ 0x00000095, 0x0000 }, /* R149 (0x95) - Haptics Phase 2 Duration */
	{ 0x00000096, 0x0000 }, /* R150 (0x96) - Haptics Phase 3 Intensity */
	{ 0x00000097, 0x0000 }, /* R151 (0x97) - Haptics Phase 3 Duration */
	{ 0x000000a0, 0x0000 }, /* R160 (0xa0) - Comfort Noise Generator */
	{ 0x00000100, 0x0002 }, /* R256 (0x100) - Clock 32k 1 */
	{ 0x00000101, 0x0404 }, /* R257 (0x101) - System Clock 1 */
	{ 0x00000102, 0x0011 }, /* R258 (0x102) - Sample Rate 1 */
	{ 0x00000103, 0x0011 }, /* R259 (0x103) - Sample Rate 2 */
	{ 0x00000104, 0x0011 }, /* R260 (0x104) - Sample Rate 3 */
	{ 0x00000112, 0x0305 }, /* R274 (0x112) - Async Clock 1 */
	{ 0x00000113, 0x0011 }, /* R275 (0x113) - Async Sample Rate 1 */
	{ 0x00000114, 0x0011 }, /* R276 (0x114) - Async Sample Rate 2 */
	{ 0x00000120, 0x0305 }, /* R288 (0x120) - DSP Clock 1 */
	{ 0x00000122, 0x0000 }, /* R290 (0x122) - DSP Clock 2 */
	{ 0x00000149, 0x0000 }, /* R329 (0x149) - Output System Clock */
	{ 0x0000014a, 0x0000 }, /* R330 (0x14a) - Output Async Clock */
	{ 0x00000152, 0x0000 }, /* R338 (0x152) - Rate Estimator 1 */
	{ 0x00000153, 0x0000 }, /* R339 (0x153) - Rate Estimator 2 */
	{ 0x00000154, 0x0000 }, /* R340 (0x154) - Rate Estimator 3 */
	{ 0x00000155, 0x0000 }, /* R341 (0x155) - Rate Estimator 4 */
	{ 0x00000156, 0x0000 }, /* R342 (0x156) - Rate Estimator 5 */
	{ 0x00000171, 0x7004 }, /* R369 (0x171) - FLL1 Control 1 */
	{ 0x00000172, 0x0004 }, /* R370 (0x172) - FLL1 Control 2 */
	{ 0x00000173, 0x0000 }, /* R371 (0x173) - FLL1 Control 3 */
	{ 0x00000174, 0x0000 }, /* R372 (0x174) - FLL1 Control 4 */
	{ 0x00000175, 0x0001 }, /* R373 (0x175) - FLL1 Control 5 */
	{ 0x00000176, 0x8000 }, /* R374 (0x176) - FLL1 Control 6 */
	{ 0x00000177, 0x0680 }, /* R375 (0x177) - FLL1 Control 7 */
	{ 0x00000178, 0x21f0 }, /* R376 (0x178) - FLL1 Control 8 */
	{ 0x00000179, 0x0000 }, /* R377 (0x179) - FLL1 Control 9 */
	{ 0x0000017a, 0x0000 }, /* R378 (0x17a) - FLL1 Control 10 */
	{ 0x0000017b, 0x0011 }, /* R379 (0x17b) - FLL1 Control 11 */
	{ 0x0000017d, 0x33e8 }, /* R381 (0x17d) - FLL1 Digital Test 1 */
	{ 0x00000181, 0x7000 }, /* R385 (0x181) - FLL1 Synchroniser 1 */
	{ 0x00000182, 0x0004 }, /* R386 (0x182) - FLL1 Synchroniser 2 */
	{ 0x00000183, 0x0000 }, /* R387 (0x183) - FLL1 Synchroniser 3 */
	{ 0x00000184, 0x0000 }, /* R388 (0x184) - FLL1 Synchroniser 4 */
	{ 0x00000185, 0x0001 }, /* R389 (0x185) - FLL1 Synchroniser 5 */
	{ 0x00000186, 0x0000 }, /* R390 (0x186) - FLL1 Synchroniser 6 */
	{ 0x0000018e, 0x0c04 }, /* R398 (0x18e) - FLL1 GPIO Clock */
	{ 0x00000191, 0x7000 }, /* R401 (0x191) - FLL2 Control 1 */
	{ 0x00000192, 0x0004 }, /* R402 (0x192) - FLL2 Control 2 */
	{ 0x00000193, 0x0000 }, /* R403 (0x193) - FLL2 Control 3 */
	{ 0x00000194, 0x0000 }, /* R404 (0x194) - FLL2 Control 4 */
	{ 0x00000195, 0x0001 }, /* R405 (0x195) - FLL2 Control 5 */
	{ 0x00000196, 0x8000 }, /* R406 (0x196) - FLL2 Control 6 */
	{ 0x00000197, 0x0680 }, /* R407 (0x197) - FLL2 Control 7 */
	{ 0x00000198, 0x21f0 }, /* R408 (0x198) - FLL2 Control 8 */
	{ 0x00000199, 0x0000 }, /* R409 (0x199) - FLL2 Control 9 */
	{ 0x0000019a, 0x0000 }, /* R410 (0x19a) - FLL2 Control 10 */
	{ 0x0000019b, 0x0011 }, /* R411 (0x19b) - FLL2 Control 11 */
	{ 0x0000019d, 0x33e8 }, /* R413 (0x19d) - FLL2 Digital Test 1 */
	{ 0x000001a1, 0x7000 }, /* R417 (0x1a1) - FLL2 Synchroniser 1 */
	{ 0x000001a2, 0x0004 }, /* R418 (0x1a2) - FLL2 Synchroniser 2 */
	{ 0x000001a3, 0x0000 }, /* R419 (0x1a3) - FLL2 Synchroniser 3 */
	{ 0x000001a4, 0x0000 }, /* R420 (0x1a4) - FLL2 Synchroniser 4 */
	{ 0x000001a5, 0x0001 }, /* R421 (0x1a5) - FLL2 Synchroniser 5 */
	{ 0x000001a6, 0x0000 }, /* R422 (0x1a6) - FLL2 Synchroniser 6 */
	{ 0x000001ae, 0x0c04 }, /* R430 (0x1ae) - FLL2 GPIO Clock */
	{ 0x00000200, 0x0006 }, /* R512 (0x200) - Mic Charge Pump 1 */
	{ 0x00000213, 0x03e4 }, /* R531 (0x213) - LDO2 Control 1 */
	{ 0x00000218, 0x00e6 }, /* R536 (0x218) - Mic Bias Ctrl 1 */
	{ 0x00000219, 0x00e6 }, /* R537 (0x219) - Mic Bias Ctrl 2 */
	{ 0x0000021c, 0x2222 }, /* R540 (0x21c) - Mic Bias Ctrl 5 */
	{ 0x0000021e, 0x0022 }, /* R542 (0x21e) - Mic Bias Ctrl 6 */
	{ 0x00000293, 0x0080 }, /* R659 (0x293) - Accessory Detect Mode 1 */
	{ 0x00000299, 0x0000 }, /* R665 (0x299) - Headphone Detect 0 */
	{ 0x0000029b, 0x0000 }, /* R667 (0x29b) - Headphone Detect 1 */
	{ 0x000002a2, 0x0010 }, /* R674 (0x2a2) - Mic Detect 1 Control 0 */
	{ 0x000002a3, 0x1102 }, /* R675 (0x2a3) - Mic Detect 1 Control 1 */
	{ 0x000002a4, 0x009f }, /* R676 (0x2a4) - Mic Detect 1 Control 2 */
	{ 0x000002a6, 0x3d3d }, /* R678 (0x2a6) - Mic Detect 1 Level 1 */
	{ 0x000002a7, 0x3d3d }, /* R679 (0x2a7) - Mic Detect 1 Level 2 */
	{ 0x000002a8, 0x333d }, /* R680 (0x2a8) - Mic Detect 1 Level 3 */
	{ 0x000002a9, 0x202d }, /* R681 (0x2a9) - Mic Detect 1 Level 4 */
	{ 0x000002b2, 0x0010 }, /* R690 (0x2b2) - Mic Detect 2 Control 0 */
	{ 0x000002b3, 0x1102 }, /* R691 (0x2b3) - Mic Detect 2 Control 1 */
	{ 0x000002b4, 0x009f }, /* R692 (0x2b4) - Mic Detect 2 Control 2 */
	{ 0x000002b6, 0x3d3d }, /* R694 (0x2b6) - Mic Detect 2 Level 1 */
	{ 0x000002b7, 0x3d3d }, /* R695 (0x2b7) - Mic Detect 2 Level 2 */
	{ 0x000002b8, 0x333d }, /* R696 (0x2b8) - Mic Detect 2 Level 3 */
	{ 0x000002b9, 0x202d }, /* R697 (0x2b9) - Mic Detect 2 Level 4 */
	{ 0x000002c6, 0x0210 }, /* R710 (0x2c6) - Micd Clamp control */
	{ 0x000002c8, 0x0000 }, /* R712 (0x2c8) - GP Switch 1 */
	{ 0x000002d3, 0x0000 }, /* R723 (0x2d3) - Jack Detect Analogue */
	{ 0x00000300, 0x0000 }, /* R768 (0x300) - Input Enables */
	{ 0x00000308, 0x0400 }, /* R776 (0x308) - Input Rate */
	{ 0x00000309, 0x0022 }, /* R777 (0x309) - Input Volume Ramp */
	{ 0x0000030c, 0x0002 }, /* R780 (0x30c) - HPF Control */
	{ 0x00000310, 0x0080 }, /* R784 (0x310) - IN1L Control */
	{ 0x00000311, 0x0180 }, /* R785 (0x311) - ADC Digital Volume 1L */
	{ 0x00000312, 0x0500 }, /* R786 (0x312) - DMIC1L Control */
	{ 0x00000313, 0x0000 }, /* R787 (0x313) - IN1L Rate Control */
	{ 0x00000314, 0x0080 }, /* R788 (0x314) - IN1R Control */
	{ 0x00000315, 0x0180 }, /* R789 (0x315) - ADC Digital Volume 1R */
	{ 0x00000316, 0x0000 }, /* R790 (0x316) - DMIC1R Control */
	{ 0x00000317, 0x0000 }, /* R791 (0x317) - IN1R Rate Control */
	{ 0x00000318, 0x0080 }, /* R792 (0x318) - IN2L Control */
	{ 0x00000319, 0x0180 }, /* R793 (0x319) - ADC Digital Volume 2L */
	{ 0x0000031a, 0x0500 }, /* R794 (0x31a) - DMIC2L Control */
	{ 0x0000031b, 0x0000 }, /* R795 (0x31b) - IN2L Rate Control */
	{ 0x0000031c, 0x0080 }, /* R796 (0x31c) - IN2R Control */
	{ 0x0000031d, 0x0180 }, /* R797 (0x31d) - ADC Digital Volume 2R */
	{ 0x0000031e, 0x0000 }, /* R798 (0x31e) - DMIC2R Control */
	{ 0x0000031f, 0x0000 }, /* R799 (0x31f) - IN2R Rate Control */
	{ 0x00000320, 0x0000 }, /* R800 (0x320) - IN3L Control */
	{ 0x00000321, 0x0180 }, /* R801 (0x321) - ADC Digital Volume 3L */
	{ 0x00000322, 0x0500 }, /* R802 (0x322) - DMIC3L Control */
	{ 0x00000323, 0x0000 }, /* R803 (0x323) - IN3L Rate Control */
	{ 0x00000324, 0x0000 }, /* R804 (0x324) - IN3R Control */
	{ 0x00000325, 0x0180 }, /* R805 (0x325) - ADC Digital Volume 3R */
	{ 0x00000326, 0x0000 }, /* R806 (0x326) - DMIC3R Control */
	{ 0x00000327, 0x0000 }, /* R807 (0x327) - IN3R Rate Control */
	{ 0x00000328, 0x0000 }, /* R808 (0x328) - IN4L Control */
	{ 0x00000329, 0x0180 }, /* R809 (0x329) - ADC Digital Volume 4L */
	{ 0x0000032a, 0x0500 }, /* R810 (0x32a) - DMIC4L Control */
	{ 0x0000032b, 0x0000 }, /* R811 (0x32b) - IN4L Rate Control */
	{ 0x0000032c, 0x0000 }, /* R812 (0x32c) - IN4R Control */
	{ 0x0000032d, 0x0180 }, /* R813 (0x32d) - ADC Digital Volume 4R */
	{ 0x0000032e, 0x0000 }, /* R814 (0x32e) - DMIC4R Control */
	{ 0x0000032f, 0x0000 }, /* R815 (0x32f) - IN4R Rate Control */
	{ 0x00000400, 0x0000 }, /* R1024 (0x400) - Output Enables 1 */
	{ 0x00000408, 0x0040 }, /* R1032 (0x408) - Output Rate 1 */
	{ 0x00000409, 0x0022 }, /* R1033 (0x409) - Output Volume Ramp */
	{ 0x00000410, 0x0080 }, /* R1040 (0x410) - Output Path Config 1L */
	{ 0x00000411, 0x0180 }, /* R1041 (0x411) - DAC Digital Volume 1L */
	{ 0x00000412, 0x0005 }, /* R1042 (0x412) - Output Path Config 1 */
	{ 0x00000413, 0x0001 }, /* R1043 (0x413) - Noise Gate Select 1L */
	{ 0x00000414, 0x0080 }, /* R1044 (0x414) - Output Path Config 1R */
	{ 0x00000415, 0x0180 }, /* R1045 (0x415) - DAC Digital Volume 1R */
	{ 0x00000417, 0x0002 }, /* R1047 (0x417) - Noise Gate Select 1R */
	{ 0x00000418, 0x0080 }, /* R1048 (0x418) - Output Path Config 2L */
	{ 0x00000419, 0x0180 }, /* R1049 (0x419) - DAC Digital Volume 2L */
	{ 0x0000041a, 0x0005 }, /* R1050 (0x41a) - Output Path Config 2 */
	{ 0x0000041b, 0x0004 }, /* R1051 (0x41b) - Noise Gate Select 2L */
	{ 0x0000041c, 0x0080 }, /* R1052 (0x41c) - Output Path Config 2R */
	{ 0x0000041d, 0x0180 }, /* R1053 (0x41d) - DAC Digital Volume 2R */
	{ 0x0000041f, 0x0008 }, /* R1055 (0x41f) - Noise Gate Select 2R */
	{ 0x00000420, 0x0080 }, /* R1056 (0x420) - Output Path Config 3L */
	{ 0x00000421, 0x0180 }, /* R1057 (0x421) - DAC Digital Volume 3L */
	{ 0x00000422, 0x0005 }, /* R1058 (0x422) - Output Path Config 3 */
	{ 0x00000423, 0x0010 }, /* R1059 (0x423) - Noise Gate Select 3L */
	{ 0x00000424, 0x0080 }, /* R1060 (0x424) - Output Path Config 3R */
	{ 0x00000425, 0x0180 }, /* R1061 (0x425) - DAC Digital Volume 3R */
	{ 0x00000427, 0x0020 }, /* R1063 (0x427) - Noise Gate Select 3R */
	{ 0x00000430, 0x0000 }, /* R1072 (0x430) - Output Path Config 5L */
	{ 0x00000431, 0x0180 }, /* R1073 (0x431) - DAC Digital Volume 5L */
	{ 0x00000433, 0x0100 }, /* R1075 (0x433) - Noise Gate Select 5L */
	{ 0x00000434, 0x0000 }, /* R1076 (0x434) - Output Path Config 5R */
	{ 0x00000435, 0x0180 }, /* R1077 (0x435) - DAC Digital Volume 5R */
	{ 0x00000437, 0x0200 }, /* R1079 (0x437) - Noise Gate Select 5R */
	{ 0x00000450, 0x0000 }, /* R1104 (0x450) - DAC AEC Control 1 */
	{ 0x00000451, 0x0000 }, /* R1105 (0x451) - DAC AEC Control 2 */
	{ 0x00000458, 0x0000 }, /* R1112 (0x458) - Noise Gate Control */
	{ 0x00000490, 0x0069 }, /* R1168 (0x490) - PDM SPK1 Ctrl 1 */
	{ 0x00000491, 0x0000 }, /* R1169 (0x491) - PDM SPK1 Ctrl 2 */
	{ 0x000004a0, 0x0080 }, /* R1184 (0x4a0) - HP1 Short Circuit Ctrl */
	{ 0x000004a1, 0x0000 }, /* R1185 (0x4a1) - HP2 Short Circuit Ctrl */
	{ 0x000004a2, 0x0000 }, /* R1186 (0x4a2) - HP3 Short Circuit Ctrl */
	{ 0x00000500, 0x000c }, /* R1280 (0x500) - AIF1 BCLK Ctrl */
	{ 0x00000501, 0x0000 }, /* R1281 (0x501) - AIF1 Tx Pin Ctrl */
	{ 0x00000502, 0x0000 }, /* R1282 (0x502) - AIF1 Rx Pin Ctrl */
	{ 0x00000503, 0x0000 }, /* R1283 (0x503) - AIF1 Rate Ctrl */
	{ 0x00000504, 0x0000 }, /* R1284 (0x504) - AIF1 Format */
	{ 0x00000506, 0x0040 }, /* R1286 (0x506) - AIF1 Rx BCLK Rate */
	{ 0x00000507, 0x1818 }, /* R1287 (0x507) - AIF1 Frame Ctrl 1 */
	{ 0x00000508, 0x1818 }, /* R1288 (0x508) - AIF1 Frame Ctrl 2 */
	{ 0x00000509, 0x0000 }, /* R1289 (0x509) - AIF1 Frame Ctrl 3 */
	{ 0x0000050a, 0x0001 }, /* R1290 (0x50a) - AIF1 Frame Ctrl 4 */
	{ 0x0000050b, 0x0002 }, /* R1291 (0x50b) - AIF1 Frame Ctrl 5 */
	{ 0x0000050c, 0x0003 }, /* R1292 (0x50c) - AIF1 Frame Ctrl 6 */
	{ 0x0000050d, 0x0004 }, /* R1293 (0x50d) - AIF1 Frame Ctrl 7 */
	{ 0x0000050e, 0x0005 }, /* R1294 (0x50e) - AIF1 Frame Ctrl 8 */
	{ 0x0000050f, 0x0006 }, /* R1295 (0x50f) - AIF1 Frame Ctrl 9 */
	{ 0x00000510, 0x0007 }, /* R1296 (0x510) - AIF1 Frame Ctrl 10 */
	{ 0x00000511, 0x0000 }, /* R1297 (0x511) - AIF1 Frame Ctrl 11 */
	{ 0x00000512, 0x0001 }, /* R1298 (0x512) - AIF1 Frame Ctrl 12 */
	{ 0x00000513, 0x0002 }, /* R1299 (0x513) - AIF1 Frame Ctrl 13 */
	{ 0x00000514, 0x0003 }, /* R1300 (0x514) - AIF1 Frame Ctrl 14 */
	{ 0x00000515, 0x0004 }, /* R1301 (0x515) - AIF1 Frame Ctrl 15 */
	{ 0x00000516, 0x0005 }, /* R1302 (0x516) - AIF1 Frame Ctrl 16 */
	{ 0x00000517, 0x0006 }, /* R1303 (0x517) - AIF1 Frame Ctrl 17 */
	{ 0x00000518, 0x0007 }, /* R1304 (0x518) - AIF1 Frame Ctrl 18 */
	{ 0x00000519, 0x0000 }, /* R1305 (0x519) - AIF1 Tx Enables */
	{ 0x0000051a, 0x0000 }, /* R1306 (0x51a) - AIF1 Rx Enables */
	{ 0x00000540, 0x000c }, /* R1344 (0x540) - AIF2 BCLK Ctrl */
	{ 0x00000541, 0x0000 }, /* R1345 (0x541) - AIF2 Tx Pin Ctrl */
	{ 0x00000542, 0x0000 }, /* R1346 (0x542) - AIF2 Rx Pin Ctrl */
	{ 0x00000543, 0x0000 }, /* R1347 (0x543) - AIF2 Rate Ctrl */
	{ 0x00000544, 0x0000 }, /* R1348 (0x544) - AIF2 Format */
	{ 0x00000546, 0x0040 }, /* R1350 (0x546) - AIF2 Rx BCLK Rate */
	{ 0x00000547, 0x1818 }, /* R1351 (0x547) - AIF2 Frame Ctrl 1 */
	{ 0x00000548, 0x1818 }, /* R1352 (0x548) - AIF2 Frame Ctrl 2 */
	{ 0x00000549, 0x0000 }, /* R1353 (0x549) - AIF2 Frame Ctrl 3 */
	{ 0x0000054a, 0x0001 }, /* R1354 (0x54a) - AIF2 Frame Ctrl 4 */
	{ 0x0000054b, 0x0002 }, /* R1355 (0x54b) - AIF2 Frame Ctrl 5 */
	{ 0x0000054c, 0x0003 }, /* R1356 (0x54c) - AIF2 Frame Ctrl 6 */
	{ 0x0000054d, 0x0004 }, /* R1357 (0x54d) - AIF2 Frame Ctrl 7 */
	{ 0x0000054e, 0x0005 }, /* R1358 (0x54e) - AIF2 Frame Ctrl 8 */
	{ 0x0000054f, 0x0006 }, /* R1359 (0x54f) - AIF2 Frame Ctrl 9 */
	{ 0x00000550, 0x0007 }, /* R1360 (0x550) - AIF2 Frame Ctrl 10 */
	{ 0x00000551, 0x0000 }, /* R1361 (0x551) - AIF2 Frame Ctrl 11 */
	{ 0x00000552, 0x0001 }, /* R1362 (0x552) - AIF2 Frame Ctrl 12 */
	{ 0x00000553, 0x0002 }, /* R1363 (0x553) - AIF2 Frame Ctrl 13 */
	{ 0x00000554, 0x0003 }, /* R1364 (0x554) - AIF2 Frame Ctrl 14 */
	{ 0x00000555, 0x0004 }, /* R1365 (0x555) - AIF2 Frame Ctrl 15 */
	{ 0x00000556, 0x0005 }, /* R1366 (0x556) - AIF2 Frame Ctrl 16 */
	{ 0x00000557, 0x0006 }, /* R1367 (0x557) - AIF2 Frame Ctrl 17 */
	{ 0x00000558, 0x0007 }, /* R1368 (0x558) - AIF2 Frame Ctrl 18 */
	{ 0x00000559, 0x0000 }, /* R1369 (0x559) - AIF2 Tx Enables */
	{ 0x0000055a, 0x0000 }, /* R1370 (0x55a) - AIF2 Rx Enables */
	{ 0x00000580, 0x000c }, /* R1408 (0x580) - AIF3 BCLK Ctrl */
	{ 0x00000581, 0x0000 }, /* R1409 (0x581) - AIF3 Tx Pin Ctrl */
	{ 0x00000582, 0x0000 }, /* R1410 (0x582) - AIF3 Rx Pin Ctrl */
	{ 0x00000583, 0x0000 }, /* R1411 (0x583) - AIF3 Rate Ctrl */
	{ 0x00000584, 0x0000 }, /* R1412 (0x584) - AIF3 Format */
	{ 0x00000586, 0x0040 }, /* R1414 (0x586) - AIF3 Rx BCLK Rate */
	{ 0x00000587, 0x1818 }, /* R1415 (0x587) - AIF3 Frame Ctrl 1 */
	{ 0x00000588, 0x1818 }, /* R1416 (0x588) - AIF3 Frame Ctrl 2 */
	{ 0x00000589, 0x0000 }, /* R1417 (0x589) - AIF3 Frame Ctrl 3 */
	{ 0x0000058a, 0x0001 }, /* R1418 (0x58a) - AIF3 Frame Ctrl 4 */
	{ 0x0000058b, 0x0002 }, /* R1419 (0x58b) - AIF3 Frame Ctrl 5 */
	{ 0x0000058c, 0x0003 }, /* R1420 (0x58c) - AIF3 Frame Ctrl 6 */
	{ 0x0000058d, 0x0004 }, /* R1421 (0x58d) - AIF3 Frame Ctrl 7 */
	{ 0x0000058e, 0x0005 }, /* R1422 (0x58e) - AIF3 Frame Ctrl 8 */
	{ 0x0000058f, 0x0006 }, /* R1423 (0x58f) - AIF3 Frame Ctrl 9 */
	{ 0x00000590, 0x0007 }, /* R1424 (0x590) - AIF3 Frame Ctrl 10 */
	{ 0x00000591, 0x0000 }, /* R1425 (0x591) - AIF3 Frame Ctrl 11 */
	{ 0x00000592, 0x0001 }, /* R1426 (0x592) - AIF3 Frame Ctrl 12 */
	{ 0x00000593, 0x0002 }, /* R1427 (0x593) - AIF3 Frame Ctrl 13 */
	{ 0x00000594, 0x0003 }, /* R1428 (0x594) - AIF3 Frame Ctrl 14 */
	{ 0x00000595, 0x0004 }, /* R1429 (0x595) - AIF3 Frame Ctrl 15 */
	{ 0x00000596, 0x0005 }, /* R1430 (0x596) - AIF3 Frame Ctrl 16 */
	{ 0x00000597, 0x0006 }, /* R1431 (0x597) - AIF3 Frame Ctrl 17 */
	{ 0x00000598, 0x0007 }, /* R1432 (0x598) - AIF3 Frame Ctrl 18 */
	{ 0x00000599, 0x0000 }, /* R1433 (0x599) - AIF3 Tx Enables */
	{ 0x0000059a, 0x0000 }, /* R1434 (0x59a) - AIF3 Rx Enables */
	{ 0x000005c2, 0x0000 }, /* R1474 (0x5c2) - SPD1 Tx Control */
	{ 0x000005e3, 0x0000 }, /* R1507 (0x5e3) - SLIMBus Framer Ref Gear */
	{ 0x000005e5, 0x0000 }, /* R1509 (0x5e5) - SLIMBus Rates 1 */
	{ 0x000005e6, 0x0000 }, /* R1510 (0x5e6) - SLIMBus Rates 2 */
	{ 0x000005e7, 0x0000 }, /* R1511 (0x5e7) - SLIMBus Rates 3 */
	{ 0x000005e8, 0x0000 }, /* R1512 (0x5e8) - SLIMBus Rates 4 */
	{ 0x000005e9, 0x0000 }, /* R1513 (0x5e9) - SLIMBus Rates 5 */
	{ 0x000005ea, 0x0000 }, /* R1514 (0x5ea) - SLIMBus Rates 6 */
	{ 0x000005eb, 0x0000 }, /* R1515 (0x5eb) - SLIMBus Rates 7 */
	{ 0x000005ec, 0x0000 }, /* R1516 (0x5ec) - SLIMBus Rates 8 */
	{ 0x000005f5, 0x0000 }, /* R1525 (0x5f5) - SLIMBus RX Channel Enable */
	{ 0x000005f6, 0x0000 }, /* R1526 (0x5f6) - SLIMBus TX Channel Enable */
	{ 0x00000640, 0x0000 }, /* R1600 (0x640) - PWM1MIX Input 1 Source */
	{ 0x00000641, 0x0080 }, /* R1601 (0x641) - PWM1MIX Input 1 Volume */
	{ 0x00000642, 0x0000 }, /* R1602 (0x642) - PWM1MIX Input 2 Source */
	{ 0x00000643, 0x0080 }, /* R1603 (0x643) - PWM1MIX Input 2 Volume */
	{ 0x00000644, 0x0000 }, /* R1604 (0x644) - PWM1MIX Input 3 Source */
	{ 0x00000645, 0x0080 }, /* R1605 (0x645) - PWM1MIX Input 3 Volume */
	{ 0x00000646, 0x0000 }, /* R1606 (0x646) - PWM1MIX Input 4 Source */
	{ 0x00000647, 0x0080 }, /* R1607 (0x647) - PWM1MIX Input 4 Volume */
	{ 0x00000648, 0x0000 }, /* R1608 (0x648) - PWM2MIX Input 1 Source */
	{ 0x00000649, 0x0080 }, /* R1609 (0x649) - PWM2MIX Input 1 Volume */
	{ 0x0000064a, 0x0000 }, /* R1610 (0x64a) - PWM2MIX Input 2 Source */
	{ 0x0000064b, 0x0080 }, /* R1611 (0x64b) - PWM2MIX Input 2 Volume */
	{ 0x0000064c, 0x0000 }, /* R1612 (0x64c) - PWM2MIX Input 3 Source */
	{ 0x0000064d, 0x0080 }, /* R1613 (0x64d) - PWM2MIX Input 3 Volume */
	{ 0x0000064e, 0x0000 }, /* R1614 (0x64e) - PWM2MIX Input 4 Source */
	{ 0x0000064f, 0x0080 }, /* R1615 (0x64f) - PWM2MIX Input 4 Volume */
	{ 0x00000680, 0x0000 }, /* R1664 (0x680) - OUT1LMIX Input 1 Source */
	{ 0x00000681, 0x0080 }, /* R1665 (0x681) - OUT1LMIX Input 1 Volume */
	{ 0x00000682, 0x0000 }, /* R1666 (0x682) - OUT1LMIX Input 2 Source */
	{ 0x00000683, 0x0080 }, /* R1667 (0x683) - OUT1LMIX Input 2 Volume */
	{ 0x00000684, 0x0000 }, /* R1668 (0x684) - OUT1LMIX Input 3 Source */
	{ 0x00000685, 0x0080 }, /* R1669 (0x685) - OUT1LMIX Input 3 Volume */
	{ 0x00000686, 0x0000 }, /* R1670 (0x686) - OUT1LMIX Input 4 Source */
	{ 0x00000687, 0x0080 }, /* R1671 (0x687) - OUT1LMIX Input 4 Volume */
	{ 0x00000688, 0x0000 }, /* R1672 (0x688) - OUT1RMIX Input 1 Source */
	{ 0x00000689, 0x0080 }, /* R1673 (0x689) - OUT1RMIX Input 1 Volume */
	{ 0x0000068a, 0x0000 }, /* R1674 (0x68a) - OUT1RMIX Input 2 Source */
	{ 0x0000068b, 0x0080 }, /* R1675 (0x68b) - OUT1RMIX Input 2 Volume */
	{ 0x0000068c, 0x0000 }, /* R1676 (0x68c) - OUT1RMIX Input 3 Source */
	{ 0x0000068d, 0x0080 }, /* R1677 (0x68d) - OUT1RMIX Input 3 Volume */
	{ 0x0000068e, 0x0000 }, /* R1678 (0x68e) - OUT1RMIX Input 4 Source */
	{ 0x0000068f, 0x0080 }, /* R1679 (0x68f) - OUT1RMIX Input 4 Volume */
	{ 0x00000690, 0x0000 }, /* R1680 (0x690) - OUT2LMIX Input 1 Source */
	{ 0x00000691, 0x0080 }, /* R1681 (0x691) - OUT2LMIX Input 1 Volume */
	{ 0x00000692, 0x0000 }, /* R1682 (0x692) - OUT2LMIX Input 2 Source */
	{ 0x00000693, 0x0080 }, /* R1683 (0x693) - OUT2LMIX Input 2 Volume */
	{ 0x00000694, 0x0000 }, /* R1684 (0x694) - OUT2LMIX Input 3 Source */
	{ 0x00000695, 0x0080 }, /* R1685 (0x695) - OUT2LMIX Input 3 Volume */
	{ 0x00000696, 0x0000 }, /* R1686 (0x696) - OUT2LMIX Input 4 Source */
	{ 0x00000697, 0x0080 }, /* R1687 (0x697) - OUT2LMIX Input 4 Volume */
	{ 0x00000698, 0x0000 }, /* R1688 (0x698) - OUT2RMIX Input 1 Source */
	{ 0x00000699, 0x0080 }, /* R1689 (0x699) - OUT2RMIX Input 1 Volume */
	{ 0x0000069a, 0x0000 }, /* R1690 (0x69a) - OUT2RMIX Input 2 Source */
	{ 0x0000069b, 0x0080 }, /* R1691 (0x69b) - OUT2RMIX Input 2 Volume */
	{ 0x0000069c, 0x0000 }, /* R1692 (0x69c) - OUT2RMIX Input 3 Source */
	{ 0x0000069d, 0x0080 }, /* R1693 (0x69d) - OUT2RMIX Input 3 Volume */
	{ 0x0000069e, 0x0000 }, /* R1694 (0x69e) - OUT2RMIX Input 4 Source */
	{ 0x0000069f, 0x0080 }, /* R1695 (0x69f) - OUT2RMIX Input 4 Volume */
	{ 0x000006a0, 0x0000 }, /* R1696 (0x6a0) - OUT3LMIX Input 1 Source */
	{ 0x000006a1, 0x0080 }, /* R1697 (0x6a1) - OUT3LMIX Input 1 Volume */
	{ 0x000006a2, 0x0000 }, /* R1698 (0x6a2) - OUT3LMIX Input 2 Source */
	{ 0x000006a3, 0x0080 }, /* R1699 (0x6a3) - OUT3LMIX Input 2 Volume */
	{ 0x000006a4, 0x0000 }, /* R1700 (0x6a4) - OUT3LMIX Input 3 Source */
	{ 0x000006a5, 0x0080 }, /* R1701 (0x6a5) - OUT3LMIX Input 3 Volume */
	{ 0x000006a6, 0x0000 }, /* R1702 (0x6a6) - OUT3LMIX Input 4 Source */
	{ 0x000006a7, 0x0080 }, /* R1703 (0x6a7) - OUT3LMIX Input 4 Volume */
	{ 0x000006a8, 0x0000 }, /* R1704 (0x6a8) - OUT3RMIX Input 1 Source */
	{ 0x000006a9, 0x0080 }, /* R1705 (0x6a9) - OUT3RMIX Input 1 Volume */
	{ 0x000006aa, 0x0000 }, /* R1706 (0x6aa) - OUT3RMIX Input 2 Source */
	{ 0x000006ab, 0x0080 }, /* R1707 (0x6ab) - OUT3RMIX Input 2 Volume */
	{ 0x000006ac, 0x0000 }, /* R1708 (0x6ac) - OUT3RMIX Input 3 Source */
	{ 0x000006ad, 0x0080 }, /* R1709 (0x6ad) - OUT3RMIX Input 3 Volume */
	{ 0x000006ae, 0x0000 }, /* R1710 (0x6ae) - OUT3RMIX Input 4 Source */
	{ 0x000006af, 0x0080 }, /* R1711 (0x6af) - OUT3RMIX Input 4 Volume */
	{ 0x000006c0, 0x0000 }, /* R1728 (0x6c0) - OUT5LMIX Input 1 Source */
	{ 0x000006c1, 0x0080 }, /* R1729 (0x6c1) - OUT5LMIX Input 1 Volume */
	{ 0x000006c2, 0x0000 }, /* R1730 (0x6c2) - OUT5LMIX Input 2 Source */
	{ 0x000006c3, 0x0080 }, /* R1731 (0x6c3) - OUT5LMIX Input 2 Volume */
	{ 0x000006c4, 0x0000 }, /* R1732 (0x6c4) - OUT5LMIX Input 3 Source */
	{ 0x000006c5, 0x0080 }, /* R1733 (0x6c5) - OUT5LMIX Input 3 Volume */
	{ 0x000006c6, 0x0000 }, /* R1734 (0x6c6) - OUT5LMIX Input 4 Source */
	{ 0x000006c7, 0x0080 }, /* R1735 (0x6c7) - OUT5LMIX Input 4 Volume */
	{ 0x000006c8, 0x0000 }, /* R1736 (0x6c8) - OUT5RMIX Input 1 Source */
	{ 0x000006c9, 0x0080 }, /* R1737 (0x6c9) - OUT5RMIX Input 1 Volume */
	{ 0x000006ca, 0x0000 }, /* R1738 (0x6ca) - OUT5RMIX Input 2 Source */
	{ 0x000006cb, 0x0080 }, /* R1739 (0x6cb) - OUT5RMIX Input 2 Volume */
	{ 0x000006cc, 0x0000 }, /* R1740 (0x6cc) - OUT5RMIX Input 3 Source */
	{ 0x000006cd, 0x0080 }, /* R1741 (0x6cd) - OUT5RMIX Input 3 Volume */
	{ 0x000006ce, 0x0000 }, /* R1742 (0x6ce) - OUT5RMIX Input 4 Source */
	{ 0x000006cf, 0x0080 }, /* R1743 (0x6cf) - OUT5RMIX Input 4 Volume */
	{ 0x00000700, 0x0000 }, /* R1792 (0x700) - AIF1TX1MIX Input 1 Source */
	{ 0x00000701, 0x0080 }, /* R1793 (0x701) - AIF1TX1MIX Input 1 Volume */
	{ 0x00000702, 0x0000 }, /* R1794 (0x702) - AIF1TX1MIX Input 2 Source */
	{ 0x00000703, 0x0080 }, /* R1795 (0x703) - AIF1TX1MIX Input 2 Volume */
	{ 0x00000704, 0x0000 }, /* R1796 (0x704) - AIF1TX1MIX Input 3 Source */
	{ 0x00000705, 0x0080 }, /* R1797 (0x705) - AIF1TX1MIX Input 3 Volume */
	{ 0x00000706, 0x0000 }, /* R1798 (0x706) - AIF1TX1MIX Input 4 Source */
	{ 0x00000707, 0x0080 }, /* R1799 (0x707) - AIF1TX1MIX Input 4 Volume */
	{ 0x00000708, 0x0000 }, /* R1800 (0x708) - AIF1TX2MIX Input 1 Source */
	{ 0x00000709, 0x0080 }, /* R1801 (0x709) - AIF1TX2MIX Input 1 Volume */
	{ 0x0000070a, 0x0000 }, /* R1802 (0x70a) - AIF1TX2MIX Input 2 Source */
	{ 0x0000070b, 0x0080 }, /* R1803 (0x70b) - AIF1TX2MIX Input 2 Volume */
	{ 0x0000070c, 0x0000 }, /* R1804 (0x70c) - AIF1TX2MIX Input 3 Source */
	{ 0x0000070d, 0x0080 }, /* R1805 (0x70d) - AIF1TX2MIX Input 3 Volume */
	{ 0x0000070e, 0x0000 }, /* R1806 (0x70e) - AIF1TX2MIX Input 4 Source */
	{ 0x0000070f, 0x0080 }, /* R1807 (0x70f) - AIF1TX2MIX Input 4 Volume */
	{ 0x00000710, 0x0000 }, /* R1808 (0x710) - AIF1TX3MIX Input 1 Source */
	{ 0x00000711, 0x0080 }, /* R1809 (0x711) - AIF1TX3MIX Input 1 Volume */
	{ 0x00000712, 0x0000 }, /* R1810 (0x712) - AIF1TX3MIX Input 2 Source */
	{ 0x00000713, 0x0080 }, /* R1811 (0x713) - AIF1TX3MIX Input 2 Volume */
	{ 0x00000714, 0x0000 }, /* R1812 (0x714) - AIF1TX3MIX Input 3 Source */
	{ 0x00000715, 0x0080 }, /* R1813 (0x715) - AIF1TX3MIX Input 3 Volume */
	{ 0x00000716, 0x0000 }, /* R1814 (0x716) - AIF1TX3MIX Input 4 Source */
	{ 0x00000717, 0x0080 }, /* R1815 (0x717) - AIF1TX3MIX Input 4 Volume */
	{ 0x00000718, 0x0000 }, /* R1816 (0x718) - AIF1TX4MIX Input 1 Source */
	{ 0x00000719, 0x0080 }, /* R1817 (0x719) - AIF1TX4MIX Input 1 Volume */
	{ 0x0000071a, 0x0000 }, /* R1818 (0x71a) - AIF1TX4MIX Input 2 Source */
	{ 0x0000071b, 0x0080 }, /* R1819 (0x71b) - AIF1TX4MIX Input 2 Volume */
	{ 0x0000071c, 0x0000 }, /* R1820 (0x71c) - AIF1TX4MIX Input 3 Source */
	{ 0x0000071d, 0x0080 }, /* R1821 (0x71d) - AIF1TX4MIX Input 3 Volume */
	{ 0x0000071e, 0x0000 }, /* R1822 (0x71e) - AIF1TX4MIX Input 4 Source */
	{ 0x0000071f, 0x0080 }, /* R1823 (0x71f) - AIF1TX4MIX Input 4 Volume */
	{ 0x00000720, 0x0000 }, /* R1824 (0x720) - AIF1TX5MIX Input 1 Source */
	{ 0x00000721, 0x0080 }, /* R1825 (0x721) - AIF1TX5MIX Input 1 Volume */
	{ 0x00000722, 0x0000 }, /* R1826 (0x722) - AIF1TX5MIX Input 2 Source */
	{ 0x00000723, 0x0080 }, /* R1827 (0x723) - AIF1TX5MIX Input 2 Volume */
	{ 0x00000724, 0x0000 }, /* R1828 (0x724) - AIF1TX5MIX Input 3 Source */
	{ 0x00000725, 0x0080 }, /* R1829 (0x725) - AIF1TX5MIX Input 3 Volume */
	{ 0x00000726, 0x0000 }, /* R1830 (0x726) - AIF1TX5MIX Input 4 Source */
	{ 0x00000727, 0x0080 }, /* R1831 (0x727) - AIF1TX5MIX Input 4 Volume */
	{ 0x00000728, 0x0000 }, /* R1832 (0x728) - AIF1TX6MIX Input 1 Source */
	{ 0x00000729, 0x0080 }, /* R1833 (0x729) - AIF1TX6MIX Input 1 Volume */
	{ 0x0000072a, 0x0000 }, /* R1834 (0x72a) - AIF1TX6MIX Input 2 Source */
	{ 0x0000072b, 0x0080 }, /* R1835 (0x72b) - AIF1TX6MIX Input 2 Volume */
	{ 0x0000072c, 0x0000 }, /* R1836 (0x72c) - AIF1TX6MIX Input 3 Source */
	{ 0x0000072d, 0x0080 }, /* R1837 (0x72d) - AIF1TX6MIX Input 3 Volume */
	{ 0x0000072e, 0x0000 }, /* R1838 (0x72e) - AIF1TX6MIX Input 4 Source */
	{ 0x0000072f, 0x0080 }, /* R1839 (0x72f) - AIF1TX6MIX Input 4 Volume */
	{ 0x00000730, 0x0000 }, /* R1840 (0x730) - AIF1TX7MIX Input 1 Source */
	{ 0x00000731, 0x0080 }, /* R1841 (0x731) - AIF1TX7MIX Input 1 Volume */
	{ 0x00000732, 0x0000 }, /* R1842 (0x732) - AIF1TX7MIX Input 2 Source */
	{ 0x00000733, 0x0080 }, /* R1843 (0x733) - AIF1TX7MIX Input 2 Volume */
	{ 0x00000734, 0x0000 }, /* R1844 (0x734) - AIF1TX7MIX Input 3 Source */
	{ 0x00000735, 0x0080 }, /* R1845 (0x735) - AIF1TX7MIX Input 3 Volume */
	{ 0x00000736, 0x0000 }, /* R1846 (0x736) - AIF1TX7MIX Input 4 Source */
	{ 0x00000737, 0x0080 }, /* R1847 (0x737) - AIF1TX7MIX Input 4 Volume */
	{ 0x00000738, 0x0000 }, /* R1848 (0x738) - AIF1TX8MIX Input 1 Source */
	{ 0x00000739, 0x0080 }, /* R1849 (0x739) - AIF1TX8MIX Input 1 Volume */
	{ 0x0000073a, 0x0000 }, /* R1850 (0x73a) - AIF1TX8MIX Input 2 Source */
	{ 0x0000073b, 0x0080 }, /* R1851 (0x73b) - AIF1TX8MIX Input 2 Volume */
	{ 0x0000073c, 0x0000 }, /* R1852 (0x73c) - AIF1TX8MIX Input 3 Source */
	{ 0x0000073d, 0x0080 }, /* R1853 (0x73d) - AIF1TX8MIX Input 3 Volume */
	{ 0x0000073e, 0x0000 }, /* R1854 (0x73e) - AIF1TX8MIX Input 4 Source */
	{ 0x0000073f, 0x0080 }, /* R1855 (0x73f) - AIF1TX8MIX Input 4 Volume */
	{ 0x00000740, 0x0000 }, /* R1856 (0x740) - AIF2TX1MIX Input 1 Source */
	{ 0x00000741, 0x0080 }, /* R1857 (0x741) - AIF2TX1MIX Input 1 Volume */
	{ 0x00000742, 0x0000 }, /* R1858 (0x742) - AIF2TX1MIX Input 2 Source */
	{ 0x00000743, 0x0080 }, /* R1859 (0x743) - AIF2TX1MIX Input 2 Volume */
	{ 0x00000744, 0x0000 }, /* R1860 (0x744) - AIF2TX1MIX Input 3 Source */
	{ 0x00000745, 0x0080 }, /* R1861 (0x745) - AIF2TX1MIX Input 3 Volume */
	{ 0x00000746, 0x0000 }, /* R1862 (0x746) - AIF2TX1MIX Input 4 Source */
	{ 0x00000747, 0x0080 }, /* R1863 (0x747) - AIF2TX1MIX Input 4 Volume */
	{ 0x00000748, 0x0000 }, /* R1864 (0x748) - AIF2TX2MIX Input 1 Source */
	{ 0x00000749, 0x0080 }, /* R1865 (0x749) - AIF2TX2MIX Input 1 Volume */
	{ 0x0000074a, 0x0000 }, /* R1866 (0x74a) - AIF2TX2MIX Input 2 Source */
	{ 0x0000074b, 0x0080 }, /* R1867 (0x74b) - AIF2TX2MIX Input 2 Volume */
	{ 0x0000074c, 0x0000 }, /* R1868 (0x74c) - AIF2TX2MIX Input 3 Source */
	{ 0x0000074d, 0x0080 }, /* R1869 (0x74d) - AIF2TX2MIX Input 3 Volume */
	{ 0x0000074e, 0x0000 }, /* R1870 (0x74e) - AIF2TX2MIX Input 4 Source */
	{ 0x0000074f, 0x0080 }, /* R1871 (0x74f) - AIF2TX2MIX Input 4 Volume */
	{ 0x00000750, 0x0000 }, /* R1872 (0x750) - AIF2TX3MIX Input 1 Source */
	{ 0x00000751, 0x0080 }, /* R1873 (0x751) - AIF2TX3MIX Input 1 Volume */
	{ 0x00000752, 0x0000 }, /* R1874 (0x752) - AIF2TX3MIX Input 2 Source */
	{ 0x00000753, 0x0080 }, /* R1875 (0x753) - AIF2TX3MIX Input 2 Volume */
	{ 0x00000754, 0x0000 }, /* R1876 (0x754) - AIF2TX3MIX Input 3 Source */
	{ 0x00000755, 0x0080 }, /* R1877 (0x755) - AIF2TX3MIX Input 3 Volume */
	{ 0x00000756, 0x0000 }, /* R1878 (0x756) - AIF2TX3MIX Input 4 Source */
	{ 0x00000757, 0x0080 }, /* R1879 (0x757) - AIF2TX3MIX Input 4 Volume */
	{ 0x00000758, 0x0000 }, /* R1880 (0x758) - AIF2TX4MIX Input 1 Source */
	{ 0x00000759, 0x0080 }, /* R1881 (0x759) - AIF2TX4MIX Input 1 Volume */
	{ 0x0000075a, 0x0000 }, /* R1882 (0x75a) - AIF2TX4MIX Input 2 Source */
	{ 0x0000075b, 0x0080 }, /* R1883 (0x75b) - AIF2TX4MIX Input 2 Volume */
	{ 0x0000075c, 0x0000 }, /* R1884 (0x75c) - AIF2TX4MIX Input 3 Source */
	{ 0x0000075d, 0x0080 }, /* R1885 (0x75d) - AIF2TX4MIX Input 3 Volume */
	{ 0x0000075e, 0x0000 }, /* R1886 (0x75e) - AIF2TX4MIX Input 4 Source */
	{ 0x0000075f, 0x0080 }, /* R1887 (0x75f) - AIF2TX4MIX Input 4 Volume */
	{ 0x00000760, 0x0000 }, /* R1888 (0x760) - AIF2TX5MIX Input 1 Source */
	{ 0x00000761, 0x0080 }, /* R1889 (0x761) - AIF2TX5MIX Input 1 Volume */
	{ 0x00000762, 0x0000 }, /* R1890 (0x762) - AIF2TX5MIX Input 2 Source */
	{ 0x00000763, 0x0080 }, /* R1891 (0x763) - AIF2TX5MIX Input 2 Volume */
	{ 0x00000764, 0x0000 }, /* R1892 (0x764) - AIF2TX5MIX Input 3 Source */
	{ 0x00000765, 0x0080 }, /* R1893 (0x765) - AIF2TX5MIX Input 3 Volume */
	{ 0x00000766, 0x0000 }, /* R1894 (0x766) - AIF2TX5MIX Input 4 Source */
	{ 0x00000767, 0x0080 }, /* R1895 (0x767) - AIF2TX5MIX Input 4 Volume */
	{ 0x00000768, 0x0000 }, /* R1896 (0x768) - AIF2TX6MIX Input 1 Source */
	{ 0x00000769, 0x0080 }, /* R1897 (0x769) - AIF2TX6MIX Input 1 Volume */
	{ 0x0000076a, 0x0000 }, /* R1898 (0x76a) - AIF2TX6MIX Input 2 Source */
	{ 0x0000076b, 0x0080 }, /* R1899 (0x76b) - AIF2TX6MIX Input 2 Volume */
	{ 0x0000076c, 0x0000 }, /* R1900 (0x76c) - AIF2TX6MIX Input 3 Source */
	{ 0x0000076d, 0x0080 }, /* R1901 (0x76d) - AIF2TX6MIX Input 3 Volume */
	{ 0x0000076e, 0x0000 }, /* R1902 (0x76e) - AIF2TX6MIX Input 4 Source */
	{ 0x0000076f, 0x0080 }, /* R1903 (0x76f) - AIF2TX6MIX Input 4 Volume */
	{ 0x00000770, 0x0000 }, /* R1904 (0x770) - AIF2TX7MIX Input 1 Source */
	{ 0x00000771, 0x0080 }, /* R1905 (0x771) - AIF2TX7MIX Input 1 Volume */
	{ 0x00000772, 0x0000 }, /* R1906 (0x772) - AIF2TX7MIX Input 2 Source */
	{ 0x00000773, 0x0080 }, /* R1907 (0x773) - AIF2TX7MIX Input 2 Volume */
	{ 0x00000774, 0x0000 }, /* R1908 (0x774) - AIF2TX7MIX Input 3 Source */
	{ 0x00000775, 0x0080 }, /* R1909 (0x775) - AIF2TX7MIX Input 3 Volume */
	{ 0x00000776, 0x0000 }, /* R1910 (0x776) - AIF2TX7MIX Input 4 Source */
	{ 0x00000777, 0x0080 }, /* R1911 (0x777) - AIF2TX7MIX Input 4 Volume */
	{ 0x00000778, 0x0000 }, /* R1912 (0x778) - AIF2TX8MIX Input 1 Source */
	{ 0x00000779, 0x0080 }, /* R1913 (0x779) - AIF2TX8MIX Input 1 Volume */
	{ 0x0000077a, 0x0000 }, /* R1914 (0x77a) - AIF2TX8MIX Input 2 Source */
	{ 0x0000077b, 0x0080 }, /* R1915 (0x77b) - AIF2TX8MIX Input 2 Volume */
	{ 0x0000077c, 0x0000 }, /* R1916 (0x77c) - AIF2TX8MIX Input 3 Source */
	{ 0x0000077d, 0x0080 }, /* R1917 (0x77d) - AIF2TX8MIX Input 3 Volume */
	{ 0x0000077e, 0x0000 }, /* R1918 (0x77e) - AIF2TX8MIX Input 4 Source */
	{ 0x0000077f, 0x0080 }, /* R1919 (0x77f) - AIF2TX8MIX Input 4 Volume */
	{ 0x00000780, 0x0000 }, /* R1920 (0x780) - AIF3TX1MIX Input 1 Source */
	{ 0x00000781, 0x0080 }, /* R1921 (0x781) - AIF3TX1MIX Input 1 Volume */
	{ 0x00000782, 0x0000 }, /* R1922 (0x782) - AIF3TX1MIX Input 2 Source */
	{ 0x00000783, 0x0080 }, /* R1923 (0x783) - AIF3TX1MIX Input 2 Volume */
	{ 0x00000784, 0x0000 }, /* R1924 (0x784) - AIF3TX1MIX Input 3 Source */
	{ 0x00000785, 0x0080 }, /* R1925 (0x785) - AIF3TX1MIX Input 3 Volume */
	{ 0x00000786, 0x0000 }, /* R1926 (0x786) - AIF3TX1MIX Input 4 Source */
	{ 0x00000787, 0x0080 }, /* R1927 (0x787) - AIF3TX1MIX Input 4 Volume */
	{ 0x00000788, 0x0000 }, /* R1928 (0x788) - AIF3TX2MIX Input 1 Source */
	{ 0x00000789, 0x0080 }, /* R1929 (0x789) - AIF3TX2MIX Input 1 Volume */
	{ 0x0000078a, 0x0000 }, /* R1930 (0x78a) - AIF3TX2MIX Input 2 Source */
	{ 0x0000078b, 0x0080 }, /* R1931 (0x78b) - AIF3TX2MIX Input 2 Volume */
	{ 0x0000078c, 0x0000 }, /* R1932 (0x78c) - AIF3TX2MIX Input 3 Source */
	{ 0x0000078d, 0x0080 }, /* R1933 (0x78d) - AIF3TX2MIX Input 3 Volume */
	{ 0x0000078e, 0x0000 }, /* R1934 (0x78e) - AIF3TX2MIX Input 4 Source */
	{ 0x0000078f, 0x0080 }, /* R1935 (0x78f) - AIF3TX2MIX Input 4 Volume */
	{ 0x00000790, 0x0000 }, /* R1936 (0x790) - AIF3TX3MIX Input 1 Source */
	{ 0x00000791, 0x0080 }, /* R1937 (0x791) - AIF3TX3MIX Input 1 Volume */
	{ 0x00000792, 0x0000 }, /* R1938 (0x792) - AIF3TX3MIX Input 2 Source */
	{ 0x00000793, 0x0080 }, /* R1939 (0x793) - AIF3TX3MIX Input 2 Volume */
	{ 0x00000794, 0x0000 }, /* R1940 (0x794) - AIF3TX3MIX Input 3 Source */
	{ 0x00000795, 0x0080 }, /* R1941 (0x795) - AIF3TX3MIX Input 3 Volume */
	{ 0x00000796, 0x0000 }, /* R1942 (0x796) - AIF3TX3MIX Input 4 Source */
	{ 0x00000797, 0x0080 }, /* R1943 (0x797) - AIF3TX3MIX Input 4 Volume */
	{ 0x00000798, 0x0000 }, /* R1944 (0x798) - AIF3TX4MIX Input 1 Source */
	{ 0x00000799, 0x0080 }, /* R1945 (0x799) - AIF3TX4MIX Input 1 Volume */
	{ 0x0000079a, 0x0000 }, /* R1946 (0x79a) - AIF3TX4MIX Input 2 Source */
	{ 0x0000079b, 0x0080 }, /* R1947 (0x79b) - AIF3TX4MIX Input 2 Volume */
	{ 0x0000079c, 0x0000 }, /* R1948 (0x79c) - AIF3TX4MIX Input 3 Source */
	{ 0x0000079d, 0x0080 }, /* R1949 (0x79d) - AIF3TX4MIX Input 3 Volume */
	{ 0x0000079e, 0x0000 }, /* R1950 (0x79e) - AIF3TX4MIX Input 4 Source */
	{ 0x0000079f, 0x0080 }, /* R1951 (0x79f) - AIF3TX4MIX Input 4 Volume */
	{ 0x000007a0, 0x0000 }, /* R1952 (0x7a0) - AIF3TX5MIX Input 1 Source */
	{ 0x000007a1, 0x0080 }, /* R1953 (0x7a1) - AIF3TX5MIX Input 1 Volume */
	{ 0x000007a2, 0x0000 }, /* R1954 (0x7a2) - AIF3TX5MIX Input 2 Source */
	{ 0x000007a3, 0x0080 }, /* R1955 (0x7a3) - AIF3TX5MIX Input 2 Volume */
	{ 0x000007a4, 0x0000 }, /* R1956 (0x7a4) - AIF3TX5MIX Input 3 Source */
	{ 0x000007a5, 0x0080 }, /* R1957 (0x7a5) - AIF3TX5MIX Input 3 Volume */
	{ 0x000007a6, 0x0000 }, /* R1958 (0x7a6) - AIF3TX5MIX Input 4 Source */
	{ 0x000007a7, 0x0080 }, /* R1959 (0x7a7) - AIF3TX5MIX Input 4 Volume */
	{ 0x000007a8, 0x0000 }, /* R1960 (0x7a8) - AIF3TX6MIX Input 1 Source */
	{ 0x000007a9, 0x0080 }, /* R1961 (0x7a9) - AIF3TX6MIX Input 1 Volume */
	{ 0x000007aa, 0x0000 }, /* R1962 (0x7aa) - AIF3TX6MIX Input 2 Source */
	{ 0x000007ab, 0x0080 }, /* R1963 (0x7ab) - AIF3TX6MIX Input 2 Volume */
	{ 0x000007ac, 0x0000 }, /* R1964 (0x7ac) - AIF3TX6MIX Input 3 Source */
	{ 0x000007ad, 0x0080 }, /* R1965 (0x7ad) - AIF3TX6MIX Input 3 Volume */
	{ 0x000007ae, 0x0000 }, /* R1966 (0x7ae) - AIF3TX6MIX Input 4 Source */
	{ 0x000007af, 0x0080 }, /* R1967 (0x7af) - AIF3TX6MIX Input 4 Volume */
	{ 0x000007b0, 0x0000 }, /* R1968 (0x7b0) - AIF3TX7MIX Input 1 Source */
	{ 0x000007b1, 0x0080 }, /* R1969 (0x7b1) - AIF3TX7MIX Input 1 Volume */
	{ 0x000007b2, 0x0000 }, /* R1970 (0x7b2) - AIF3TX7MIX Input 2 Source */
	{ 0x000007b3, 0x0080 }, /* R1971 (0x7b3) - AIF3TX7MIX Input 2 Volume */
	{ 0x000007b4, 0x0000 }, /* R1972 (0x7b4) - AIF3TX7MIX Input 3 Source */
	{ 0x000007b5, 0x0080 }, /* R1973 (0x7b5) - AIF3TX7MIX Input 3 Volume */
	{ 0x000007b6, 0x0000 }, /* R1974 (0x7b6) - AIF3TX7MIX Input 4 Source */
	{ 0x000007b7, 0x0080 }, /* R1975 (0x7b7) - AIF3TX7MIX Input 4 Volume */
	{ 0x000007b8, 0x0000 }, /* R1976 (0x7b8) - AIF3TX8MIX Input 1 Source */
	{ 0x000007b9, 0x0080 }, /* R1977 (0x7b9) - AIF3TX8MIX Input 1 Volume */
	{ 0x000007ba, 0x0000 }, /* R1978 (0x7ba) - AIF3TX8MIX Input 2 Source */
	{ 0x000007bb, 0x0080 }, /* R1979 (0x7bb) - AIF3TX8MIX Input 2 Volume */
	{ 0x000007bc, 0x0000 }, /* R1980 (0x7bc) - AIF3TX8MIX Input 3 Source */
	{ 0x000007bd, 0x0080 }, /* R1981 (0x7bd) - AIF3TX8MIX Input 3 Volume */
	{ 0x000007be, 0x0000 }, /* R1982 (0x7be) - AIF3TX8MIX Input 4 Source */
	{ 0x000007bf, 0x0080 }, /* R1983 (0x7bf) - AIF3TX8MIX Input 4 Volume */
	{ 0x000007c0, 0x0000 }, /* R1984 (0x7c0) - SLIMTX1MIX Input 1 Source */
	{ 0x000007c1, 0x0080 }, /* R1985 (0x7c1) - SLIMTX1MIX Input 1 Volume */
	{ 0x000007c2, 0x0000 }, /* R1986 (0x7c2) - SLIMTX1MIX Input 2 Source */
	{ 0x000007c3, 0x0080 }, /* R1987 (0x7c3) - SLIMTX1MIX Input 2 Volume */
	{ 0x000007c4, 0x0000 }, /* R1988 (0x7c4) - SLIMTX1MIX Input 3 Source */
	{ 0x000007c5, 0x0080 }, /* R1989 (0x7c5) - SLIMTX1MIX Input 3 Volume */
	{ 0x000007c6, 0x0000 }, /* R1990 (0x7c6) - SLIMTX1MIX Input 4 Source */
	{ 0x000007c7, 0x0080 }, /* R1991 (0x7c7) - SLIMTX1MIX Input 4 Volume */
	{ 0x000007c8, 0x0000 }, /* R1992 (0x7c8) - SLIMTX2MIX Input 1 Source */
	{ 0x000007c9, 0x0080 }, /* R1993 (0x7c9) - SLIMTX2MIX Input 1 Volume */
	{ 0x000007ca, 0x0000 }, /* R1994 (0x7ca) - SLIMTX2MIX Input 2 Source */
	{ 0x000007cb, 0x0080 }, /* R1995 (0x7cb) - SLIMTX2MIX Input 2 Volume */
	{ 0x000007cc, 0x0000 }, /* R1996 (0x7cc) - SLIMTX2MIX Input 3 Source */
	{ 0x000007cd, 0x0080 }, /* R1997 (0x7cd) - SLIMTX2MIX Input 3 Volume */
	{ 0x000007ce, 0x0000 }, /* R1998 (0x7ce) - SLIMTX2MIX Input 4 Source */
	{ 0x000007cf, 0x0080 }, /* R1999 (0x7cf) - SLIMTX2MIX Input 4 Volume */
	{ 0x000007d0, 0x0000 }, /* R2000 (0x7d0) - SLIMTX3MIX Input 1 Source */
	{ 0x000007d1, 0x0080 }, /* R2001 (0x7d1) - SLIMTX3MIX Input 1 Volume */
	{ 0x000007d2, 0x0000 }, /* R2002 (0x7d2) - SLIMTX3MIX Input 2 Source */
	{ 0x000007d3, 0x0080 }, /* R2003 (0x7d3) - SLIMTX3MIX Input 2 Volume */
	{ 0x000007d4, 0x0000 }, /* R2004 (0x7d4) - SLIMTX3MIX Input 3 Source */
	{ 0x000007d5, 0x0080 }, /* R2005 (0x7d5) - SLIMTX3MIX Input 3 Volume */
	{ 0x000007d6, 0x0000 }, /* R2006 (0x7d6) - SLIMTX3MIX Input 4 Source */
	{ 0x000007d7, 0x0080 }, /* R2007 (0x7d7) - SLIMTX3MIX Input 4 Volume */
	{ 0x000007d8, 0x0000 }, /* R2008 (0x7d8) - SLIMTX4MIX Input 1 Source */
	{ 0x000007d9, 0x0080 }, /* R2009 (0x7d9) - SLIMTX4MIX Input 1 Volume */
	{ 0x000007da, 0x0000 }, /* R2010 (0x7da) - SLIMTX4MIX Input 2 Source */
	{ 0x000007db, 0x0080 }, /* R2011 (0x7db) - SLIMTX4MIX Input 2 Volume */
	{ 0x000007dc, 0x0000 }, /* R2012 (0x7dc) - SLIMTX4MIX Input 3 Source */
	{ 0x000007dd, 0x0080 }, /* R2013 (0x7dd) - SLIMTX4MIX Input 3 Volume */
	{ 0x000007de, 0x0000 }, /* R2014 (0x7de) - SLIMTX4MIX Input 4 Source */
	{ 0x000007df, 0x0080 }, /* R2015 (0x7df) - SLIMTX4MIX Input 4 Volume */
	{ 0x000007e0, 0x0000 }, /* R2016 (0x7e0) - SLIMTX5MIX Input 1 Source */
	{ 0x000007e1, 0x0080 }, /* R2017 (0x7e1) - SLIMTX5MIX Input 1 Volume */
	{ 0x000007e2, 0x0000 }, /* R2018 (0x7e2) - SLIMTX5MIX Input 2 Source */
	{ 0x000007e3, 0x0080 }, /* R2019 (0x7e3) - SLIMTX5MIX Input 2 Volume */
	{ 0x000007e4, 0x0000 }, /* R2020 (0x7e4) - SLIMTX5MIX Input 3 Source */
	{ 0x000007e5, 0x0080 }, /* R2021 (0x7e5) - SLIMTX5MIX Input 3 Volume */
	{ 0x000007e6, 0x0000 }, /* R2022 (0x7e6) - SLIMTX5MIX Input 4 Source */
	{ 0x000007e7, 0x0080 }, /* R2023 (0x7e7) - SLIMTX5MIX Input 4 Volume */
	{ 0x000007e8, 0x0000 }, /* R2024 (0x7e8) - SLIMTX6MIX Input 1 Source */
	{ 0x000007e9, 0x0080 }, /* R2025 (0x7e9) - SLIMTX6MIX Input 1 Volume */
	{ 0x000007ea, 0x0000 }, /* R2026 (0x7ea) - SLIMTX6MIX Input 2 Source */
	{ 0x000007eb, 0x0080 }, /* R2027 (0x7eb) - SLIMTX6MIX Input 2 Volume */
	{ 0x000007ec, 0x0000 }, /* R2028 (0x7ec) - SLIMTX6MIX Input 3 Source */
	{ 0x000007ed, 0x0080 }, /* R2029 (0x7ed) - SLIMTX6MIX Input 3 Volume */
	{ 0x000007ee, 0x0000 }, /* R2030 (0x7ee) - SLIMTX6MIX Input 4 Source */
	{ 0x000007ef, 0x0080 }, /* R2031 (0x7ef) - SLIMTX6MIX Input 4 Volume */
	{ 0x000007f0, 0x0000 }, /* R2032 (0x7f0) - SLIMTX7MIX Input 1 Source */
	{ 0x000007f1, 0x0080 }, /* R2033 (0x7f1) - SLIMTX7MIX Input 1 Volume */
	{ 0x000007f2, 0x0000 }, /* R2034 (0x7f2) - SLIMTX7MIX Input 2 Source */
	{ 0x000007f3, 0x0080 }, /* R2035 (0x7f3) - SLIMTX7MIX Input 2 Volume */
	{ 0x000007f4, 0x0000 }, /* R2036 (0x7f4) - SLIMTX7MIX Input 3 Source */
	{ 0x000007f5, 0x0080 }, /* R2037 (0x7f5) - SLIMTX7MIX Input 3 Volume */
	{ 0x000007f6, 0x0000 }, /* R2038 (0x7f6) - SLIMTX7MIX Input 4 Source */
	{ 0x000007f7, 0x0080 }, /* R2039 (0x7f7) - SLIMTX7MIX Input 4 Volume */
	{ 0x000007f8, 0x0000 }, /* R2040 (0x7f8) - SLIMTX8MIX Input 1 Source */
	{ 0x000007f9, 0x0080 }, /* R2041 (0x7f9) - SLIMTX8MIX Input 1 Volume */
	{ 0x000007fa, 0x0000 }, /* R2042 (0x7fa) - SLIMTX8MIX Input 2 Source */
	{ 0x000007fb, 0x0080 }, /* R2043 (0x7fb) - SLIMTX8MIX Input 2 Volume */
	{ 0x000007fc, 0x0000 }, /* R2044 (0x7fc) - SLIMTX8MIX Input 3 Source */
	{ 0x000007fd, 0x0080 }, /* R2045 (0x7fd) - SLIMTX8MIX Input 3 Volume */
	{ 0x000007fe, 0x0000 }, /* R2046 (0x7fe) - SLIMTX8MIX Input 4 Source */
	{ 0x000007ff, 0x0080 }, /* R2047 (0x7ff) - SLIMTX8MIX Input 4 Volume */
	{ 0x00000800, 0x0000 }, /* R2048 (0x800) - SPDIF1TX1MIX Input 1 Source */
	{ 0x00000801, 0x0080 }, /* R2049 (0x801) - SPDIF1TX1MIX Input 1 Volume */
	{ 0x00000808, 0x0000 }, /* R2056 (0x808) - SPDIF1TX2MIX Input 1 Source */
	{ 0x00000809, 0x0080 }, /* R2057 (0x809) - SPDIF1TX2MIX Input 1 Volume */
	{ 0x00000880, 0x0000 }, /* R2176 (0x880) - EQ1MIX Input 1 Source */
	{ 0x00000881, 0x0080 }, /* R2177 (0x881) - EQ1MIX Input 1 Volume */
	{ 0x00000882, 0x0000 }, /* R2178 (0x882) - EQ1MIX Input 2 Source */
	{ 0x00000883, 0x0080 }, /* R2179 (0x883) - EQ1MIX Input 2 Volume */
	{ 0x00000884, 0x0000 }, /* R2180 (0x884) - EQ1MIX Input 3 Source */
	{ 0x00000885, 0x0080 }, /* R2181 (0x885) - EQ1MIX Input 3 Volume */
	{ 0x00000886, 0x0000 }, /* R2182 (0x886) - EQ1MIX Input 4 Source */
	{ 0x00000887, 0x0080 }, /* R2183 (0x887) - EQ1MIX Input 4 Volume */
	{ 0x00000888, 0x0000 }, /* R2184 (0x888) - EQ2MIX Input 1 Source */
	{ 0x00000889, 0x0080 }, /* R2185 (0x889) - EQ2MIX Input 1 Volume */
	{ 0x0000088a, 0x0000 }, /* R2186 (0x88a) - EQ2MIX Input 2 Source */
	{ 0x0000088b, 0x0080 }, /* R2187 (0x88b) - EQ2MIX Input 2 Volume */
	{ 0x0000088c, 0x0000 }, /* R2188 (0x88c) - EQ2MIX Input 3 Source */
	{ 0x0000088d, 0x0080 }, /* R2189 (0x88d) - EQ2MIX Input 3 Volume */
	{ 0x0000088e, 0x0000 }, /* R2190 (0x88e) - EQ2MIX Input 4 Source */
	{ 0x0000088f, 0x0080 }, /* R2191 (0x88f) - EQ2MIX Input 4 Volume */
	{ 0x00000890, 0x0000 }, /* R2192 (0x890) - EQ3MIX Input 1 Source */
	{ 0x00000891, 0x0080 }, /* R2193 (0x891) - EQ3MIX Input 1 Volume */
	{ 0x00000892, 0x0000 }, /* R2194 (0x892) - EQ3MIX Input 2 Source */
	{ 0x00000893, 0x0080 }, /* R2195 (0x893) - EQ3MIX Input 2 Volume */
	{ 0x00000894, 0x0000 }, /* R2196 (0x894) - EQ3MIX Input 3 Source */
	{ 0x00000895, 0x0080 }, /* R2197 (0x895) - EQ3MIX Input 3 Volume */
	{ 0x00000896, 0x0000 }, /* R2198 (0x896) - EQ3MIX Input 4 Source */
	{ 0x00000897, 0x0080 }, /* R2199 (0x897) - EQ3MIX Input 4 Volume */
	{ 0x00000898, 0x0000 }, /* R2200 (0x898) - EQ4MIX Input 1 Source */
	{ 0x00000899, 0x0080 }, /* R2201 (0x899) - EQ4MIX Input 1 Volume */
	{ 0x0000089a, 0x0000 }, /* R2202 (0x89a) - EQ4MIX Input 2 Source */
	{ 0x0000089b, 0x0080 }, /* R2203 (0x89b) - EQ4MIX Input 2 Volume */
	{ 0x0000089c, 0x0000 }, /* R2204 (0x89c) - EQ4MIX Input 3 Source */
	{ 0x0000089d, 0x0080 }, /* R2205 (0x89d) - EQ4MIX Input 3 Volume */
	{ 0x0000089e, 0x0000 }, /* R2206 (0x89e) - EQ4MIX Input 4 Source */
	{ 0x0000089f, 0x0080 }, /* R2207 (0x89f) - EQ4MIX Input 4 Volume */
	{ 0x000008c0, 0x0000 }, /* R2240 (0x8c0) - DRC1LMIX Input 1 Source */
	{ 0x000008c1, 0x0080 }, /* R2241 (0x8c1) - DRC1LMIX Input 1 Volume */
	{ 0x000008c2, 0x0000 }, /* R2242 (0x8c2) - DRC1LMIX Input 2 Source */
	{ 0x000008c3, 0x0080 }, /* R2243 (0x8c3) - DRC1LMIX Input 2 Volume */
	{ 0x000008c4, 0x0000 }, /* R2244 (0x8c4) - DRC1LMIX Input 3 Source */
	{ 0x000008c5, 0x0080 }, /* R2245 (0x8c5) - DRC1LMIX Input 3 Volume */
	{ 0x000008c6, 0x0000 }, /* R2246 (0x8c6) - DRC1LMIX Input 4 Source */
	{ 0x000008c7, 0x0080 }, /* R2247 (0x8c7) - DRC1LMIX Input 4 Volume */
	{ 0x000008c8, 0x0000 }, /* R2248 (0x8c8) - DRC1RMIX Input 1 Source */
	{ 0x000008c9, 0x0080 }, /* R2249 (0x8c9) - DRC1RMIX Input 1 Volume */
	{ 0x000008ca, 0x0000 }, /* R2250 (0x8ca) - DRC1RMIX Input 2 Source */
	{ 0x000008cb, 0x0080 }, /* R2251 (0x8cb) - DRC1RMIX Input 2 Volume */
	{ 0x000008cc, 0x0000 }, /* R2252 (0x8cc) - DRC1RMIX Input 3 Source */
	{ 0x000008cd, 0x0080 }, /* R2253 (0x8cd) - DRC1RMIX Input 3 Volume */
	{ 0x000008ce, 0x0000 }, /* R2254 (0x8ce) - DRC1RMIX Input 4 Source */
	{ 0x000008cf, 0x0080 }, /* R2255 (0x8cf) - DRC1RMIX Input 4 Volume */
	{ 0x000008d0, 0x0000 }, /* R2256 (0x8d0) - DRC2LMIX Input 1 Source */
	{ 0x000008d1, 0x0080 }, /* R2257 (0x8d1) - DRC2LMIX Input 1 Volume */
	{ 0x000008d2, 0x0000 }, /* R2258 (0x8d2) - DRC2LMIX Input 2 Source */
	{ 0x000008d3, 0x0080 }, /* R2259 (0x8d3) - DRC2LMIX Input 2 Volume */
	{ 0x000008d4, 0x0000 }, /* R2260 (0x8d4) - DRC2LMIX Input 3 Source */
	{ 0x000008d5, 0x0080 }, /* R2261 (0x8d5) - DRC2LMIX Input 3 Volume */
	{ 0x000008d6, 0x0000 }, /* R2262 (0x8d6) - DRC2LMIX Input 4 Source */
	{ 0x000008d7, 0x0080 }, /* R2263 (0x8d7) - DRC2LMIX Input 4 Volume */
	{ 0x000008d8, 0x0000 }, /* R2264 (0x8d8) - DRC2RMIX Input 1 Source */
	{ 0x000008d9, 0x0080 }, /* R2265 (0x8d9) - DRC2RMIX Input 1 Volume */
	{ 0x000008da, 0x0000 }, /* R2266 (0x8da) - DRC2RMIX Input 2 Source */
	{ 0x000008db, 0x0080 }, /* R2267 (0x8db) - DRC2RMIX Input 2 Volume */
	{ 0x000008dc, 0x0000 }, /* R2268 (0x8dc) - DRC2RMIX Input 3 Source */
	{ 0x000008dd, 0x0080 }, /* R2269 (0x8dd) - DRC2RMIX Input 3 Volume */
	{ 0x000008de, 0x0000 }, /* R2270 (0x8de) - DRC2RMIX Input 4 Source */
	{ 0x000008df, 0x0080 }, /* R2271 (0x8df) - DRC2RMIX Input 4 Volume */
	{ 0x00000900, 0x0000 }, /* R2304 (0x900) - HPLP1MIX Input 1 Source */
	{ 0x00000901, 0x0080 }, /* R2305 (0x901) - HPLP1MIX Input 1 Volume */
	{ 0x00000902, 0x0000 }, /* R2306 (0x902) - HPLP1MIX Input 2 Source */
	{ 0x00000903, 0x0080 }, /* R2307 (0x903) - HPLP1MIX Input 2 Volume */
	{ 0x00000904, 0x0000 }, /* R2308 (0x904) - HPLP1MIX Input 3 Source */
	{ 0x00000905, 0x0080 }, /* R2309 (0x905) - HPLP1MIX Input 3 Volume */
	{ 0x00000906, 0x0000 }, /* R2310 (0x906) - HPLP1MIX Input 4 Source */
	{ 0x00000907, 0x0080 }, /* R2311 (0x907) - HPLP1MIX Input 4 Volume */
	{ 0x00000908, 0x0000 }, /* R2312 (0x908) - HPLP2MIX Input 1 Source */
	{ 0x00000909, 0x0080 }, /* R2313 (0x909) - HPLP2MIX Input 1 Volume */
	{ 0x0000090a, 0x0000 }, /* R2314 (0x90a) - HPLP2MIX Input 2 Source */
	{ 0x0000090b, 0x0080 }, /* R2315 (0x90b) - HPLP2MIX Input 2 Volume */
	{ 0x0000090c, 0x0000 }, /* R2316 (0x90c) - HPLP2MIX Input 3 Source */
	{ 0x0000090d, 0x0080 }, /* R2317 (0x90d) - HPLP2MIX Input 3 Volume */
	{ 0x0000090e, 0x0000 }, /* R2318 (0x90e) - HPLP2MIX Input 4 Source */
	{ 0x0000090f, 0x0080 }, /* R2319 (0x90f) - HPLP2MIX Input 4 Volume */
	{ 0x00000910, 0x0000 }, /* R2320 (0x910) - HPLP3MIX Input 1 Source */
	{ 0x00000911, 0x0080 }, /* R2321 (0x911) - HPLP3MIX Input 1 Volume */
	{ 0x00000912, 0x0000 }, /* R2322 (0x912) - HPLP3MIX Input 2 Source */
	{ 0x00000913, 0x0080 }, /* R2323 (0x913) - HPLP3MIX Input 2 Volume */
	{ 0x00000914, 0x0000 }, /* R2324 (0x914) - HPLP3MIX Input 3 Source */
	{ 0x00000915, 0x0080 }, /* R2325 (0x915) - HPLP3MIX Input 3 Volume */
	{ 0x00000916, 0x0000 }, /* R2326 (0x916) - HPLP3MIX Input 4 Source */
	{ 0x00000917, 0x0080 }, /* R2327 (0x917) - HPLP3MIX Input 4 Volume */
	{ 0x00000918, 0x0000 }, /* R2328 (0x918) - HPLP4MIX Input 1 Source */
	{ 0x00000919, 0x0080 }, /* R2329 (0x919) - HPLP4MIX Input 1 Volume */
	{ 0x0000091a, 0x0000 }, /* R2330 (0x91a) - HPLP4MIX Input 2 Source */
	{ 0x0000091b, 0x0080 }, /* R2331 (0x91b) - HPLP4MIX Input 2 Volume */
	{ 0x0000091c, 0x0000 }, /* R2332 (0x91c) - HPLP4MIX Input 3 Source */
	{ 0x0000091d, 0x0080 }, /* R2333 (0x91d) - HPLP4MIX Input 3 Volume */
	{ 0x0000091e, 0x0000 }, /* R2334 (0x91e) - HPLP4MIX Input 4 Source */
	{ 0x0000091f, 0x0080 }, /* R2335 (0x91f) - HPLP4MIX Input 4 Volume */
	{ 0x00000940, 0x0000 }, /* R2368 (0x940) - DSP1LMIX Input 1 Source */
	{ 0x00000941, 0x0080 }, /* R2369 (0x941) - DSP1LMIX Input 1 Volume */
	{ 0x00000942, 0x0000 }, /* R2370 (0x942) - DSP1LMIX Input 2 Source */
	{ 0x00000943, 0x0080 }, /* R2371 (0x943) - DSP1LMIX Input 2 Volume */
	{ 0x00000944, 0x0000 }, /* R2372 (0x944) - DSP1LMIX Input 3 Source */
	{ 0x00000945, 0x0080 }, /* R2373 (0x945) - DSP1LMIX Input 3 Volume */
	{ 0x00000946, 0x0000 }, /* R2374 (0x946) - DSP1LMIX Input 4 Source */
	{ 0x00000947, 0x0080 }, /* R2375 (0x947) - DSP1LMIX Input 4 Volume */
	{ 0x00000948, 0x0000 }, /* R2376 (0x948) - DSP1RMIX Input 1 Source */
	{ 0x00000949, 0x0080 }, /* R2377 (0x949) - DSP1RMIX Input 1 Volume */
	{ 0x0000094a, 0x0000 }, /* R2378 (0x94a) - DSP1RMIX Input 2 Source */
	{ 0x0000094b, 0x0080 }, /* R2379 (0x94b) - DSP1RMIX Input 2 Volume */
	{ 0x0000094c, 0x0000 }, /* R2380 (0x94c) - DSP1RMIX Input 3 Source */
	{ 0x0000094d, 0x0080 }, /* R2381 (0x94d) - DSP1RMIX Input 3 Volume */
	{ 0x0000094e, 0x0000 }, /* R2382 (0x94e) - DSP1RMIX Input 4 Source */
	{ 0x0000094f, 0x0080 }, /* R2383 (0x94f) - DSP1RMIX Input 4 Volume */
	{ 0x00000950, 0x0000 }, /* R2384 (0x950) - DSP1AUX1MIX Input 1 Source */
	{ 0x00000958, 0x0000 }, /* R2392 (0x958) - DSP1AUX2MIX Input 1 Source */
	{ 0x00000960, 0x0000 }, /* R2400 (0x960) - DSP1AUX3MIX Input 1 Source */
	{ 0x00000968, 0x0000 }, /* R2408 (0x968) - DSP1AUX4MIX Input 1 Source */
	{ 0x00000970, 0x0000 }, /* R2416 (0x970) - DSP1AUX5MIX Input 1 Source */
	{ 0x00000978, 0x0000 }, /* R2424 (0x978) - DSP1AUX6MIX Input 1 Source */
	{ 0x00000a80, 0x0000 }, /* R2688 (0xa80) - ASRC1 1LMIX Input 1 Source */
	{ 0x00000a88, 0x0000 }, /* R2696 (0xa88) - ASRC1 1RMIX Input 1 Source */
	{ 0x00000a90, 0x0000 }, /* R2704 (0xa90) - ASRC1 2LMIX Input 1 Source */
	{ 0x00000a98, 0x0000 }, /* R2712 (0xa98) - ASRC1 2RMIX Input 1 Source */
	{ 0x00000b00, 0x0000 }, /* R2816 (0xb00) - ISRC1DEC1MIX Input 1 Source */
	{ 0x00000b08, 0x0000 }, /* R2824 (0xb08) - ISRC1DEC2MIX Input 1 Source */
	{ 0x00000b20, 0x0000 }, /* R2848 (0xb20) - ISRC1INT1MIX Input 1 Source */
	{ 0x00000b28, 0x0000 }, /* R2856 (0xb28) - ISRC1INT2MIX Input 1 Source */
	{ 0x00000b40, 0x0000 }, /* R2880 (0xb40) - ISRC2DEC1MIX Input 1 Source */
	{ 0x00000b48, 0x0000 }, /* R2888 (0xb48) - ISRC2DEC2MIX Input 1 Source */
	{ 0x00000b60, 0x0000 }, /* R2912 (0xb60) - ISRC2INT1MIX Input 1 Source */
	{ 0x00000b68, 0x0000 }, /* R2920 (0xb68) - ISRC2INT2MIX Input 1 Source */
	{ 0x00000dc0, 0x0000 }, /* R3520 (0xdc0) - DFC1MIX Input 1 Source */
	{ 0x00000dc8, 0x0000 }, /* R3528 (0xdc8) - DFC2MIX Input 1 Source */
	{ 0x00000dd0, 0x0000 }, /* R3536 (0xdd0) - DFC3MIX Input 1 Source */
	{ 0x00000dd8, 0x0000 }, /* R3544 (0xdd8) - DFC4MIX Input 1 Source */
	{ 0x00000de0, 0x0000 }, /* R3552 (0xde0) - DFC5MIX Input 1 Source */
	{ 0x00000de8, 0x0000 }, /* R3560 (0xde8) - DFC6MIX Input 1 Source */
	{ 0x00000df0, 0x0000 }, /* R3568 (0xdf0) - DFC7MIX Input 1 Source */
	{ 0x00000df8, 0x0000 }, /* R3576 (0xdf8) - DFC8MIX Input 1 Source */
	{ 0x00000e00, 0x0000 }, /* R3584 (0xe00) - FX Ctrl 1 */
	{ 0x00000e10, 0x6318 }, /* R3600 (0xe10) - EQ1 1 */
	{ 0x00000e11, 0x6300 }, /* R3601 (0xe11) - EQ1 2 */
	{ 0x00000e12, 0x0fc8 }, /* R3602 (0xe12) - EQ1 3 */
	{ 0x00000e13, 0x03fe }, /* R3603 (0xe13) - EQ1 4 */
	{ 0x00000e14, 0x00e0 }, /* R3604 (0xe14) - EQ1 5 */
	{ 0x00000e15, 0x1ec4 }, /* R3605 (0xe15) - EQ1 6 */
	{ 0x00000e16, 0xf136 }, /* R3606 (0xe16) - EQ1 7 */
	{ 0x00000e17, 0x0409 }, /* R3607 (0xe17) - EQ1 8 */
	{ 0x00000e18, 0x04cc }, /* R3608 (0xe18) - EQ1 9 */
	{ 0x00000e19, 0x1c9b }, /* R3609 (0xe19) - EQ1 10 */
	{ 0x00000e1a, 0xf337 }, /* R3610 (0xe1a) - EQ1 11 */
	{ 0x00000e1b, 0x040b }, /* R3611 (0xe1b) - EQ1 12 */
	{ 0x00000e1c, 0x0cbb }, /* R3612 (0xe1c) - EQ1 13 */
	{ 0x00000e1d, 0x16f8 }, /* R3613 (0xe1d) - EQ1 14 */
	{ 0x00000e1e, 0xf7d9 }, /* R3614 (0xe1e) - EQ1 15 */
	{ 0x00000e1f, 0x040a }, /* R3615 (0xe1f) - EQ1 16 */
	{ 0x00000e20, 0x1f14 }, /* R3616 (0xe20) - EQ1 17 */
	{ 0x00000e21, 0x058c }, /* R3617 (0xe21) - EQ1 18 */
	{ 0x00000e22, 0x0563 }, /* R3618 (0xe22) - EQ1 19 */
	{ 0x00000e23, 0x4000 }, /* R3619 (0xe23) - EQ1 20 */
	{ 0x00000e24, 0x0b75 }, /* R3620 (0xe24) - EQ1 21 */
	{ 0x00000e26, 0x6318 }, /* R3622 (0xe26) - EQ2 1 */
	{ 0x00000e27, 0x6300 }, /* R3623 (0xe27) - EQ2 2 */
	{ 0x00000e28, 0x0fc8 }, /* R3624 (0xe28) - EQ2 3 */
	{ 0x00000e29, 0x03fe }, /* R3625 (0xe29) - EQ2 4 */
	{ 0x00000e2a, 0x00e0 }, /* R3626 (0xe2a) - EQ2 5 */
	{ 0x00000e2b, 0x1ec4 }, /* R3627 (0xe2b) - EQ2 6 */
	{ 0x00000e2c, 0xf136 }, /* R3628 (0xe2c) - EQ2 7 */
	{ 0x00000e2d, 0x0409 }, /* R3629 (0xe2d) - EQ2 8 */
	{ 0x00000e2e, 0x04cc }, /* R3630 (0xe2e) - EQ2 9 */
	{ 0x00000e2f, 0x1c9b }, /* R3631 (0xe2f) - EQ2 10 */
	{ 0x00000e30, 0xf337 }, /* R3632 (0xe30) - EQ2 11 */
	{ 0x00000e31, 0x040b }, /* R3633 (0xe31) - EQ2 12 */
	{ 0x00000e32, 0x0cbb }, /* R3634 (0xe32) - EQ2 13 */
	{ 0x00000e33, 0x16f8 }, /* R3635 (0xe33) - EQ2 14 */
	{ 0x00000e34, 0xf7d9 }, /* R3636 (0xe34) - EQ2 15 */
	{ 0x00000e35, 0x040a }, /* R3637 (0xe35) - EQ2 16 */
	{ 0x00000e36, 0x1f14 }, /* R3638 (0xe36) - EQ2 17 */
	{ 0x00000e37, 0x058c }, /* R3639 (0xe37) - EQ2 18 */
	{ 0x00000e38, 0x0563 }, /* R3640 (0xe38) - EQ2 19 */
	{ 0x00000e39, 0x4000 }, /* R3641 (0xe39) - EQ2 20 */
	{ 0x00000e3a, 0x0b75 }, /* R3642 (0xe3a) - EQ2 21 */
	{ 0x00000e3c, 0x6318 }, /* R3644 (0xe3c) - EQ3 1 */
	{ 0x00000e3d, 0x6300 }, /* R3645 (0xe3d) - EQ3 2 */
	{ 0x00000e3e, 0x0fc8 }, /* R3646 (0xe3e) - EQ3 3 */
	{ 0x00000e3f, 0x03fe }, /* R3647 (0xe3f) - EQ3 4 */
	{ 0x00000e40, 0x00e0 }, /* R3648 (0xe40) - EQ3 5 */
	{ 0x00000e41, 0x1ec4 }, /* R3649 (0xe41) - EQ3 6 */
	{ 0x00000e42, 0xf136 }, /* R3650 (0xe42) - EQ3 7 */
	{ 0x00000e43, 0x0409 }, /* R3651 (0xe43) - EQ3 8 */
	{ 0x00000e44, 0x04cc }, /* R3652 (0xe44) - EQ3 9 */
	{ 0x00000e45, 0x1c9b }, /* R3653 (0xe45) - EQ3 10 */
	{ 0x00000e46, 0xf337 }, /* R3654 (0xe46) - EQ3 11 */
	{ 0x00000e47, 0x040b }, /* R3655 (0xe47) - EQ3 12 */
	{ 0x00000e48, 0x0cbb }, /* R3656 (0xe48) - EQ3 13 */
	{ 0x00000e49, 0x16f8 }, /* R3657 (0xe49) - EQ3 14 */
	{ 0x00000e4a, 0xf7d9 }, /* R3658 (0xe4a) - EQ3 15 */
	{ 0x00000e4b, 0x040a }, /* R3659 (0xe4b) - EQ3 16 */
	{ 0x00000e4c, 0x1f14 }, /* R3660 (0xe4c) - EQ3 17 */
	{ 0x00000e4d, 0x058c }, /* R3661 (0xe4d) - EQ3 18 */
	{ 0x00000e4e, 0x0563 }, /* R3662 (0xe4e) - EQ3 19 */
	{ 0x00000e4f, 0x4000 }, /* R3663 (0xe4f) - EQ3 20 */
	{ 0x00000e50, 0x0b75 }, /* R3664 (0xe50) - EQ3 21 */
	{ 0x00000e52, 0x6318 }, /* R3666 (0xe52) - EQ4 1 */
	{ 0x00000e53, 0x6300 }, /* R3667 (0xe53) - EQ4 2 */
	{ 0x00000e54, 0x0fc8 }, /* R3668 (0xe54) - EQ4 3 */
	{ 0x00000e55, 0x03fe }, /* R3669 (0xe55) - EQ4 4 */
	{ 0x00000e56, 0x00e0 }, /* R3670 (0xe56) - EQ4 5 */
	{ 0x00000e57, 0x1ec4 }, /* R3671 (0xe57) - EQ4 6 */
	{ 0x00000e58, 0xf136 }, /* R3672 (0xe58) - EQ4 7 */
	{ 0x00000e59, 0x0409 }, /* R3673 (0xe59) - EQ4 8 */
	{ 0x00000e5a, 0x04cc }, /* R3674 (0xe5a) - EQ4 9 */
	{ 0x00000e5b, 0x1c9b }, /* R3675 (0xe5b) - EQ4 10 */
	{ 0x00000e5c, 0xf337 }, /* R3676 (0xe5c) - EQ4 11 */
	{ 0x00000e5d, 0x040b }, /* R3677 (0xe5d) - EQ4 12 */
	{ 0x00000e5e, 0x0cbb }, /* R3678 (0xe5e) - EQ4 13 */
	{ 0x00000e5f, 0x16f8 }, /* R3679 (0xe5f) - EQ4 14 */
	{ 0x00000e60, 0xf7d9 }, /* R3680 (0xe60) - EQ4 15 */
	{ 0x00000e61, 0x040a }, /* R3681 (0xe61) - EQ4 16 */
	{ 0x00000e62, 0x1f14 }, /* R3682 (0xe62) - EQ4 17 */
	{ 0x00000e63, 0x058c }, /* R3683 (0xe63) - EQ4 18 */
	{ 0x00000e64, 0x0563 }, /* R3684 (0xe64) - EQ4 19 */
	{ 0x00000e65, 0x4000 }, /* R3685 (0xe65) - EQ4 20 */
	{ 0x00000e66, 0x0b75 }, /* R3686 (0xe66) - EQ4 21 */
	{ 0x00000e80, 0x0018 }, /* R3712 (0xe80) - DRC1 Ctrl 1 */
	{ 0x00000e81, 0x0933 }, /* R3713 (0xe81) - DRC1 Ctrl 2 */
	{ 0x00000e82, 0x0018 }, /* R3714 (0xe82) - DRC1 Ctrl 3 */
	{ 0x00000e83, 0x0000 }, /* R3715 (0xe83) - DRC1 Ctrl 4 */
	{ 0x00000e84, 0x0000 }, /* R3716 (0xe84) - DRC1 Ctrl 5 */
	{ 0x00000e88, 0x0018 }, /* R3720 (0xe88) - DRC2 Ctrl 1 */
	{ 0x00000e89, 0x0933 }, /* R3721 (0xe89) - DRC2 Ctrl 2 */
	{ 0x00000e8a, 0x0018 }, /* R3722 (0xe8a) - DRC2 Ctrl 3 */
	{ 0x00000e8b, 0x0000 }, /* R3723 (0xe8b) - DRC2 Ctrl 4 */
	{ 0x00000e8c, 0x0000 }, /* R3724 (0xe8c) - DRC2 Ctrl 5 */
	{ 0x00000ec0, 0x0000 }, /* R3776 (0xec0) - HPLPF1 1 */
	{ 0x00000ec1, 0x0000 }, /* R3777 (0xec1) - HPLPF1 2 */
	{ 0x00000ec4, 0x0000 }, /* R3780 (0xec4) - HPLPF2 1 */
	{ 0x00000ec5, 0x0000 }, /* R3781 (0xec5) - HPLPF2 2 */
	{ 0x00000ec8, 0x0000 }, /* R3784 (0xec8) - HPLPF3 1 */
	{ 0x00000ec9, 0x0000 }, /* R3785 (0xec9) - HPLPF3 2 */
	{ 0x00000ecc, 0x0000 }, /* R3788 (0xecc) - HPLPF4 1 */
	{ 0x00000ecd, 0x0000 }, /* R3789 (0xecd) - HPLPF4 2 */
	{ 0x00000ee0, 0x0000 }, /* R3808 (0xee0) - ASRC1 Enable */
	{ 0x00000ee2, 0x0000 }, /* R3810 (0xee2) - ASRC1 Rate 1 */
	{ 0x00000ee3, 0x4000 }, /* R3811 (0xee3) - ASRC1 Rate 2 */
	{ 0x00000ef0, 0x0000 }, /* R3824 (0xef0) - ISRC1 Ctrl 1 */
	{ 0x00000ef1, 0x0001 }, /* R3825 (0xef1) - ISRC1 Ctrl 2 */
	{ 0x00000ef2, 0x0000 }, /* R3826 (0xef2) - ISRC1 Ctrl 3 */
	{ 0x00000ef3, 0x0000 }, /* R3827 (0xef3) - ISRC2 Ctrl 1 */
	{ 0x00000ef4, 0x0001 }, /* R3828 (0xef4) - ISRC2 Ctrl 2 */
	{ 0x00000ef5, 0x0000 }, /* R3829 (0xef5) - ISRC2 Ctrl 3 */
	{ 0x000010c0, 0x0008 }, /* R4288 (0x10c0) - AUXPDM1 Ctrl 0 */
	{ 0x000010c1, 0x4000 }, /* R4289 (0x10c1) - AUXPDM1 Ctrl 1 */
	{ 0x00001480, 0x0000 }, /* R5248 (0x1480) - DFC1 Ctrl W0 */
	{ 0x00001482, 0x1f00 }, /* R5250 (0x1482) - DFC1 Rx W0 */
	{ 0x00001484, 0x1f00 }, /* R5252 (0x1484) - DFC1 Tx W0 */
	{ 0x00001486, 0x0000 }, /* R5254 (0x1486) - DFC2 Ctrl W0 */
	{ 0x00001488, 0x1f00 }, /* R5256 (0x1488) - DFC2 Rx W0 */
	{ 0x0000148a, 0x1f00 }, /* R5258 (0x148a) - DFC2 Tx W0 */
	{ 0x0000148c, 0x0000 }, /* R5260 (0x148c) - DFC3 Ctrl W0 */
	{ 0x0000148e, 0x1f00 }, /* R5262 (0x148e) - DFC3 Rx W0 */
	{ 0x00001490, 0x1f00 }, /* R5264 (0x1490) - DFC3 Tx W0 */
	{ 0x00001492, 0x0000 }, /* R5266 (0x1492) - DFC4 Ctrl W0 */
	{ 0x00001494, 0x1f00 }, /* R5268 (0x1494) - DFC4 Rx W0 */
	{ 0x00001496, 0x1f00 }, /* R5270 (0x1496) - DFC4 Tx W0 */
	{ 0x00001498, 0x0000 }, /* R5272 (0x1498) - DFC5 Ctrl W0 */
	{ 0x0000149a, 0x1f00 }, /* R5274 (0x149a) - DFC5 Rx W0 */
	{ 0x0000149c, 0x1f00 }, /* R5276 (0x149c) - DFC5 Tx W0 */
	{ 0x0000149e, 0x0000 }, /* R5278 (0x149e) - DFC6 Ctrl W0 */
	{ 0x000014a0, 0x1f00 }, /* R5280 (0x14a0) - DFC6 Rx W0 */
	{ 0x000014a2, 0x1f00 }, /* R5282 (0x14a2) - DFC6 Tx W0 */
	{ 0x000014a4, 0x0000 }, /* R5284 (0x14a4) - DFC7 Ctrl W0 */
	{ 0x000014a6, 0x1f00 }, /* R5286 (0x14a6) - DFC7 Rx W0 */
	{ 0x000014a8, 0x1f00 }, /* R5288 (0x14a8) - DFC7 Tx W0 */
	{ 0x000014aa, 0x0000 }, /* R5290 (0x14aa) - DFC8 Ctrl W0 */
	{ 0x000014ac, 0x1f00 }, /* R5292 (0x14ac) - DFC8 Rx W0 */
	{ 0x000014ae, 0x1f00 }, /* R5294 (0x14ae) - DFC8 Tx W0 */
	{ 0x00001700, 0x2001 }, /* R5888 (0x1700) - GPIO1 Ctrl 1 */
	{ 0x00001701, 0xf000 }, /* R5889 (0x1701) - GPIO1 Ctrl 2 */
	{ 0x00001702, 0x2001 }, /* R5890 (0x1702) - GPIO2 Ctrl 1 */
	{ 0x00001703, 0xf000 }, /* R5891 (0x1703) - GPIO2 Ctrl 2 */
	{ 0x00001704, 0x2001 }, /* R5892 (0x1704) - GPIO3 Ctrl 1 */
	{ 0x00001705, 0xf000 }, /* R5893 (0x1705) - GPIO3 Ctrl 2 */
	{ 0x00001706, 0x2001 }, /* R5894 (0x1706) - GPIO4 Ctrl 1 */
	{ 0x00001707, 0xf000 }, /* R5895 (0x1707) - GPIO4 Ctrl 2 */
	{ 0x00001708, 0x2001 }, /* R5896 (0x1708) - GPIO5 Ctrl 1 */
	{ 0x00001709, 0xf000 }, /* R5897 (0x1709) - GPIO5 Ctrl 2 */
	{ 0x0000170a, 0x2001 }, /* R5898 (0x170a) - GPIO6 Ctrl 1 */
	{ 0x0000170b, 0xf000 }, /* R5899 (0x170b) - GPIO6 Ctrl 2 */
	{ 0x0000170c, 0x2001 }, /* R5900 (0x170c) - GPIO7 Ctrl 1 */
	{ 0x0000170d, 0xf000 }, /* R5901 (0x170d) - GPIO7 Ctrl 2 */
	{ 0x0000170e, 0x2001 }, /* R5902 (0x170e) - GPIO8 Ctrl 1 */
	{ 0x0000170f, 0xf000 }, /* R5903 (0x170f) - GPIO8 Ctrl 2 */
	{ 0x00001710, 0x2001 }, /* R5904 (0x1710) - GPIO9 Ctrl 1 */
	{ 0x00001711, 0xf000 }, /* R5905 (0x1711) - GPIO9 Ctrl 2 */
	{ 0x00001712, 0x2001 }, /* R5906 (0x1712) - GPIO10 Ctrl 1 */
	{ 0x00001713, 0xf000 }, /* R5907 (0x1713) - GPIO10 Ctrl 2 */
	{ 0x00001714, 0x2001 }, /* R5908 (0x1714) - GPIO11 Ctrl 1 */
	{ 0x00001715, 0xf000 }, /* R5909 (0x1715) - GPIO11 Ctrl 2 */
	{ 0x00001716, 0x2001 }, /* R5910 (0x1716) - GPIO12 Ctrl 1 */
	{ 0x00001717, 0xf000 }, /* R5911 (0x1717) - GPIO12 Ctrl 2 */
	{ 0x00001718, 0x2001 }, /* R5912 (0x1718) - GPIO13 Ctrl 1 */
	{ 0x00001719, 0xf000 }, /* R5913 (0x1719) - GPIO13 Ctrl 2 */
	{ 0x0000171a, 0x2001 }, /* R5914 (0x171a) - GPIO14 Ctrl 1 */
	{ 0x0000171b, 0xf000 }, /* R5915 (0x171b) - GPIO14 Ctrl 2 */
	{ 0x0000171c, 0x2001 }, /* R5916 (0x171c) - GPIO15 Ctrl 1 */
	{ 0x0000171d, 0xf000 }, /* R5917 (0x171d) - GPIO15 Ctrl 2 */
	{ 0x0000171e, 0x2001 }, /* R5918 (0x171e) - GPIO16 Ctrl 1 */
	{ 0x0000171f, 0xf000 }, /* R5919 (0x171f) - GPIO16 Ctrl 2 */
	{ 0x00001840, 0x1200 }, /* R6208 (0x1840) - IRQ1 Mask 1 */
	{ 0x00001841, 0x77e0 }, /* R6209 (0x1841) - IRQ1 Mask 2 */
	{ 0x00001842, 0xffff }, /* R6210 (0x1842) - IRQ1 Mask 3 */
	{ 0x00001843, 0xffff }, /* R6211 (0x1843) - IRQ1 Mask 4 */
	{ 0x00001844, 0xffff }, /* R6212 (0x1844) - IRQ1 Mask 5 */
	{ 0x00001845, 0x0301 }, /* R6213 (0x1845) - IRQ1 Mask 6 */
	{ 0x00001846, 0x0f3f }, /* R6214 (0x1846) - IRQ1 Mask 7 */
	{ 0x00001847, 0xffff }, /* R6215 (0x1847) - IRQ1 Mask 8 */
	{ 0x00001848, 0x031f }, /* R6216 (0x1848) - IRQ1 Mask 9 */
	{ 0x00001849, 0x031f }, /* R6217 (0x1849) - IRQ1 Mask 10 */
	{ 0x0000184a, 0xffff }, /* R6218 (0x184a) - IRQ1 Mask 11 */
	{ 0x0000184b, 0x033f }, /* R6219 (0x184b) - IRQ1 Mask 12 */
	{ 0x0000184c, 0x003f }, /* R6220 (0x184c) - IRQ1 Mask 13 */
	{ 0x0000184d, 0x003f }, /* R6221 (0x184d) - IRQ1 Mask 14 */
	{ 0x0000184e, 0x1000 }, /* R6222 (0x184e) - IRQ1 Mask 15 */
	{ 0x0000184f, 0xffff }, /* R6223 (0x184f) - IRQ1 Mask 16 */
	{ 0x00001850, 0xffff }, /* R6224 (0x1850) - IRQ1 Mask 17 */
	{ 0x00001851, 0xffff }, /* R6225 (0x1851) - IRQ1 Mask 18 */
	{ 0x00001852, 0xffff }, /* R6226 (0x1852) - IRQ1 Mask 19 */
	{ 0x00001853, 0xffff }, /* R6227 (0x1853) - IRQ1 Mask 20 */
	{ 0x00001854, 0x0001 }, /* R6228 (0x1854) - IRQ1 Mask 21 */
	{ 0x00001855, 0x0001 }, /* R6229 (0x1855) - IRQ1 Mask 22 */
	{ 0x00001856, 0x0001 }, /* R6230 (0x1856) - IRQ1 Mask 23 */
	{ 0x00001857, 0x0001 }, /* R6231 (0x1857) - IRQ1 Mask 24 */
	{ 0x00001858, 0x0001 }, /* R6232 (0x1858) - IRQ1 Mask 25 */
	{ 0x00001859, 0xffff }, /* R6233 (0x1859) - IRQ1 Mask 26 */
	{ 0x0000185a, 0x0001 }, /* R6234 (0x185a) - IRQ1 Mask 27 */
	{ 0x0000185b, 0x0001 }, /* R6235 (0x185b) - IRQ1 Mask 28 */
	{ 0x0000185c, 0xffff }, /* R6236 (0x185c) - IRQ1 Mask 29 */
	{ 0x0000185d, 0x0001 }, /* R6237 (0x185d) - IRQ1 Mask 30 */
	{ 0x0000185e, 0xffff }, /* R6238 (0x185e) - IRQ1 Mask 31 */
	{ 0x0000185f, 0xffff }, /* R6239 (0x185f) - IRQ1 Mask 32 */
	{ 0x00001860, 0x0001 }, /* R6240 (0x1860) - IRQ1 Mask 33 */
	{ 0x00001a06, 0x0000 }, /* R6662 (0x1a06) - Interrupt Debounce 7 */
	{ 0x00001a80, 0x4400 }, /* R6784 (0x1a80) - IRQ1 Ctrl */
};

static bool cs47l92_is_adsp_memory(unsigned int reg)
{
	switch (reg) {
	case 0x080000 ... 0x082ffe:
	case 0x0a0000 ... 0x0a1ffe:
	case 0x0c0000 ... 0x0c1ffe:
	case 0x0e0000 ... 0x0e1ffe:
		return true;
	default:
		return false;
	}
}

static bool cs47l92_16bit_readable_register(struct device *dev,
					    unsigned int reg)
{
	switch (reg) {
	case MADERA_SOFTWARE_RESET:
	case MADERA_HARDWARE_REVISION:
	case MADERA_WRITE_SEQUENCER_CTRL_0 ... MADERA_WRITE_SEQUENCER_CTRL_2:
	case MADERA_TONE_GENERATOR_1 ... MADERA_TONE_GENERATOR_5:
	case MADERA_PWM_DRIVE_1 ... MADERA_PWM_DRIVE_3:
	case MADERA_SAMPLE_RATE_SEQUENCE_SELECT_1:
	case MADERA_SAMPLE_RATE_SEQUENCE_SELECT_2:
	case MADERA_SAMPLE_RATE_SEQUENCE_SELECT_3:
	case MADERA_SAMPLE_RATE_SEQUENCE_SELECT_4:
	case MADERA_HAPTICS_CONTROL_1 ... MADERA_HAPTICS_CONTROL_2:
	case MADERA_HAPTICS_PHASE_1_INTENSITY:
	case MADERA_HAPTICS_PHASE_1_DURATION:
	case MADERA_HAPTICS_PHASE_2_INTENSITY:
	case MADERA_HAPTICS_PHASE_2_DURATION:
	case MADERA_HAPTICS_PHASE_3_INTENSITY:
	case MADERA_HAPTICS_PHASE_3_DURATION:
	case MADERA_HAPTICS_STATUS:
	case MADERA_COMFORT_NOISE_GENERATOR:
	case MADERA_CLOCK_32K_1:
	case MADERA_SYSTEM_CLOCK_1:
	case MADERA_SAMPLE_RATE_1 ... MADERA_SAMPLE_RATE_3:
	case MADERA_SAMPLE_RATE_1_STATUS:
	case MADERA_SAMPLE_RATE_2_STATUS:
	case MADERA_SAMPLE_RATE_3_STATUS:
	case MADERA_ASYNC_CLOCK_1:
	case MADERA_ASYNC_SAMPLE_RATE_1:
	case MADERA_ASYNC_SAMPLE_RATE_1_STATUS:
	case MADERA_ASYNC_SAMPLE_RATE_2:
	case MADERA_ASYNC_SAMPLE_RATE_2_STATUS:
	case MADERA_DSP_CLOCK_1:
	case MADERA_DSP_CLOCK_2:
	case MADERA_OUTPUT_SYSTEM_CLOCK:
	case MADERA_OUTPUT_ASYNC_CLOCK:
	case MADERA_RATE_ESTIMATOR_1 ...  MADERA_RATE_ESTIMATOR_5:
	case MADERA_FLL1_CONTROL_1 ... MADERA_FLL1_CONTROL_6:
	case CS47L92_FLL1_CONTROL_7 ...  CS47L92_FLL1_CONTROL_10:
	case MADERA_FLL1_CONTROL_11:
	case MADERA_FLL1_DIGITAL_TEST_1:
	case MADERA_FLL1_SYNCHRONISER_1 ... MADERA_FLL1_SYNCHRONISER_6:
	case CS47L92_FLL1_GPIO_CLOCK:
	case MADERA_FLL2_CONTROL_1 ... MADERA_FLL2_CONTROL_6:
	case CS47L92_FLL2_CONTROL_7 ... CS47L92_FLL2_CONTROL_10:
	case MADERA_FLL2_CONTROL_11:
	case MADERA_FLL2_DIGITAL_TEST_1:
	case MADERA_FLL2_SYNCHRONISER_1 ... MADERA_FLL2_SYNCHRONISER_6:
	case CS47L92_FLL2_GPIO_CLOCK:
	case MADERA_MIC_CHARGE_PUMP_1:
	case MADERA_LDO2_CONTROL_1:
	case MADERA_MIC_BIAS_CTRL_1:
	case MADERA_MIC_BIAS_CTRL_2:
	case MADERA_MIC_BIAS_CTRL_5:
	case MADERA_MIC_BIAS_CTRL_6:
	case MADERA_HP_CTRL_1L:
	case MADERA_HP_CTRL_1R:
	case MADERA_HP_CTRL_2L:
	case MADERA_HP_CTRL_2R:
	case MADERA_HP_CTRL_3L:
	case MADERA_HP_CTRL_3R:
	case MADERA_ACCESSORY_DETECT_MODE_1:
	case MADERA_HEADPHONE_DETECT_0:
	case MADERA_HEADPHONE_DETECT_1:
	case MADERA_HEADPHONE_DETECT_2:
	case MADERA_HEADPHONE_DETECT_3:
	case MADERA_HEADPHONE_DETECT_5:
	case MADERA_MICD_CLAMP_CONTROL:
	case MADERA_MIC_DETECT_1_CONTROL_0:
	case MADERA_MIC_DETECT_1_CONTROL_1:
	case MADERA_MIC_DETECT_1_CONTROL_2:
	case MADERA_MIC_DETECT_1_CONTROL_3:
	case MADERA_MIC_DETECT_1_CONTROL_4:
	case MADERA_MIC_DETECT_1_LEVEL_1 ... MADERA_MIC_DETECT_1_LEVEL_4:
	case MADERA_MIC_DETECT_2_CONTROL_0:
	case MADERA_MIC_DETECT_2_CONTROL_1:
	case MADERA_MIC_DETECT_2_CONTROL_2:
	case MADERA_MIC_DETECT_2_CONTROL_3:
	case MADERA_MIC_DETECT_2_CONTROL_4:
	case MADERA_MIC_DETECT_2_LEVEL_1 ... MADERA_MIC_DETECT_2_LEVEL_4:
	case MADERA_GP_SWITCH_1:
	case MADERA_JACK_DETECT_ANALOGUE:
	case MADERA_INPUT_ENABLES:
	case MADERA_INPUT_ENABLES_STATUS:
	case MADERA_INPUT_RATE:
	case MADERA_INPUT_VOLUME_RAMP:
	case MADERA_HPF_CONTROL:
	case MADERA_IN1L_CONTROL:
	case MADERA_ADC_DIGITAL_VOLUME_1L:
	case MADERA_DMIC1L_CONTROL:
	case MADERA_IN1L_RATE_CONTROL:
	case MADERA_IN1R_CONTROL:
	case MADERA_ADC_DIGITAL_VOLUME_1R:
	case MADERA_DMIC1R_CONTROL:
	case MADERA_IN1R_RATE_CONTROL:
	case MADERA_IN2L_CONTROL:
	case MADERA_ADC_DIGITAL_VOLUME_2L:
	case MADERA_DMIC2L_CONTROL:
	case MADERA_IN2L_RATE_CONTROL:
	case MADERA_IN2R_CONTROL:
	case MADERA_ADC_DIGITAL_VOLUME_2R:
	case MADERA_DMIC2R_CONTROL:
	case MADERA_IN2R_RATE_CONTROL:
	case MADERA_IN3L_CONTROL:
	case MADERA_ADC_DIGITAL_VOLUME_3L:
	case MADERA_DMIC3L_CONTROL:
	case MADERA_IN3L_RATE_CONTROL:
	case MADERA_IN3R_CONTROL:
	case MADERA_ADC_DIGITAL_VOLUME_3R:
	case MADERA_DMIC3R_CONTROL:
	case MADERA_IN3R_RATE_CONTROL:
	case MADERA_IN4L_CONTROL:
	case MADERA_ADC_DIGITAL_VOLUME_4L:
	case MADERA_DMIC4L_CONTROL:
	case MADERA_IN4L_RATE_CONTROL:
	case MADERA_IN4R_CONTROL:
	case MADERA_ADC_DIGITAL_VOLUME_4R:
	case MADERA_DMIC4R_CONTROL:
	case MADERA_IN4R_RATE_CONTROL:
	case MADERA_OUTPUT_ENABLES_1:
	case MADERA_OUTPUT_STATUS_1:
	case MADERA_RAW_OUTPUT_STATUS_1:
	case MADERA_OUTPUT_RATE_1:
	case MADERA_OUTPUT_VOLUME_RAMP:
	case MADERA_OUTPUT_PATH_CONFIG_1L:
	case MADERA_DAC_DIGITAL_VOLUME_1L:
	case MADERA_OUTPUT_PATH_CONFIG_1:
	case MADERA_NOISE_GATE_SELECT_1L:
	case MADERA_OUTPUT_PATH_CONFIG_1R:
	case MADERA_DAC_DIGITAL_VOLUME_1R:
	case MADERA_NOISE_GATE_SELECT_1R:
	case MADERA_OUTPUT_PATH_CONFIG_2L:
	case MADERA_DAC_DIGITAL_VOLUME_2L:
	case MADERA_OUTPUT_PATH_CONFIG_2:
	case MADERA_NOISE_GATE_SELECT_2L:
	case MADERA_OUTPUT_PATH_CONFIG_2R:
	case MADERA_DAC_DIGITAL_VOLUME_2R:
	case MADERA_NOISE_GATE_SELECT_2R:
	case MADERA_OUTPUT_PATH_CONFIG_3L:
	case MADERA_DAC_DIGITAL_VOLUME_3L:
	case MADERA_OUTPUT_PATH_CONFIG_3:
	case MADERA_NOISE_GATE_SELECT_3L:
	case MADERA_OUTPUT_PATH_CONFIG_3R:
	case MADERA_DAC_DIGITAL_VOLUME_3R:
	case MADERA_NOISE_GATE_SELECT_3R:
	case MADERA_OUTPUT_PATH_CONFIG_5L:
	case MADERA_DAC_DIGITAL_VOLUME_5L:
	case MADERA_NOISE_GATE_SELECT_5L:
	case MADERA_OUTPUT_PATH_CONFIG_5R:
	case MADERA_DAC_DIGITAL_VOLUME_5R:
	case MADERA_NOISE_GATE_SELECT_5R:
	case MADERA_DAC_AEC_CONTROL_1 ...  MADERA_DAC_AEC_CONTROL_2:
	case MADERA_NOISE_GATE_CONTROL:
	case MADERA_PDM_SPK1_CTRL_1 ... MADERA_PDM_SPK1_CTRL_2:
	case MADERA_HP1_SHORT_CIRCUIT_CTRL:
	case MADERA_HP2_SHORT_CIRCUIT_CTRL:
	case MADERA_HP3_SHORT_CIRCUIT_CTRL:
	case MADERA_AIF1_BCLK_CTRL:
	case MADERA_AIF1_TX_PIN_CTRL:
	case MADERA_AIF1_RX_PIN_CTRL:
	case MADERA_AIF1_RATE_CTRL:
	case MADERA_AIF1_FORMAT:
	case MADERA_AIF1_RX_BCLK_RATE:
	case MADERA_AIF1_FRAME_CTRL_1 ... MADERA_AIF1_FRAME_CTRL_18:
	case MADERA_AIF1_TX_ENABLES:
	case MADERA_AIF1_RX_ENABLES:
	case MADERA_AIF2_BCLK_CTRL:
	case MADERA_AIF2_TX_PIN_CTRL:
	case MADERA_AIF2_RX_PIN_CTRL:
	case MADERA_AIF2_RATE_CTRL:
	case MADERA_AIF2_FORMAT:
	case MADERA_AIF2_RX_BCLK_RATE:
	case MADERA_AIF2_FRAME_CTRL_1 ... MADERA_AIF2_FRAME_CTRL_18:
	case MADERA_AIF2_TX_ENABLES:
	case MADERA_AIF2_RX_ENABLES:
	case MADERA_AIF3_BCLK_CTRL:
	case MADERA_AIF3_TX_PIN_CTRL:
	case MADERA_AIF3_RX_PIN_CTRL:
	case MADERA_AIF3_RATE_CTRL:
	case MADERA_AIF3_FORMAT:
	case MADERA_AIF3_RX_BCLK_RATE:
	case MADERA_AIF3_FRAME_CTRL_1 ... MADERA_AIF3_FRAME_CTRL_18:
	case MADERA_AIF3_TX_ENABLES:
	case MADERA_AIF3_RX_ENABLES:
	case MADERA_SPD1_TX_CONTROL:
	case MADERA_SPD1_TX_CHANNEL_STATUS_1:
	case MADERA_SPD1_TX_CHANNEL_STATUS_2:
	case MADERA_SPD1_TX_CHANNEL_STATUS_3:
	case MADERA_SLIMBUS_FRAMER_REF_GEAR:
	case MADERA_SLIMBUS_RATES_1 ... MADERA_SLIMBUS_RATES_8:
	case MADERA_SLIMBUS_RX_CHANNEL_ENABLE:
	case MADERA_SLIMBUS_TX_CHANNEL_ENABLE:
	case MADERA_SLIMBUS_RX_PORT_STATUS:
	case MADERA_SLIMBUS_TX_PORT_STATUS:
	case MADERA_PWM1MIX_INPUT_1_SOURCE:
	case MADERA_PWM1MIX_INPUT_1_VOLUME:
	case MADERA_PWM1MIX_INPUT_2_SOURCE:
	case MADERA_PWM1MIX_INPUT_2_VOLUME:
	case MADERA_PWM1MIX_INPUT_3_SOURCE:
	case MADERA_PWM1MIX_INPUT_3_VOLUME:
	case MADERA_PWM1MIX_INPUT_4_SOURCE:
	case MADERA_PWM1MIX_INPUT_4_VOLUME:
	case MADERA_PWM2MIX_INPUT_1_SOURCE:
	case MADERA_PWM2MIX_INPUT_1_VOLUME:
	case MADERA_PWM2MIX_INPUT_2_SOURCE:
	case MADERA_PWM2MIX_INPUT_2_VOLUME:
	case MADERA_PWM2MIX_INPUT_3_SOURCE:
	case MADERA_PWM2MIX_INPUT_3_VOLUME:
	case MADERA_PWM2MIX_INPUT_4_SOURCE:
	case MADERA_PWM2MIX_INPUT_4_VOLUME:
	case MADERA_OUT1LMIX_INPUT_1_SOURCE:
	case MADERA_OUT1LMIX_INPUT_1_VOLUME:
	case MADERA_OUT1LMIX_INPUT_2_SOURCE:
	case MADERA_OUT1LMIX_INPUT_2_VOLUME:
	case MADERA_OUT1LMIX_INPUT_3_SOURCE:
	case MADERA_OUT1LMIX_INPUT_3_VOLUME:
	case MADERA_OUT1LMIX_INPUT_4_SOURCE:
	case MADERA_OUT1LMIX_INPUT_4_VOLUME:
	case MADERA_OUT1RMIX_INPUT_1_SOURCE:
	case MADERA_OUT1RMIX_INPUT_1_VOLUME:
	case MADERA_OUT1RMIX_INPUT_2_SOURCE:
	case MADERA_OUT1RMIX_INPUT_2_VOLUME:
	case MADERA_OUT1RMIX_INPUT_3_SOURCE:
	case MADERA_OUT1RMIX_INPUT_3_VOLUME:
	case MADERA_OUT1RMIX_INPUT_4_SOURCE:
	case MADERA_OUT1RMIX_INPUT_4_VOLUME:
	case MADERA_OUT2LMIX_INPUT_1_SOURCE:
	case MADERA_OUT2LMIX_INPUT_1_VOLUME:
	case MADERA_OUT2LMIX_INPUT_2_SOURCE:
	case MADERA_OUT2LMIX_INPUT_2_VOLUME:
	case MADERA_OUT2LMIX_INPUT_3_SOURCE:
	case MADERA_OUT2LMIX_INPUT_3_VOLUME:
	case MADERA_OUT2LMIX_INPUT_4_SOURCE:
	case MADERA_OUT2LMIX_INPUT_4_VOLUME:
	case MADERA_OUT2RMIX_INPUT_1_SOURCE:
	case MADERA_OUT2RMIX_INPUT_1_VOLUME:
	case MADERA_OUT2RMIX_INPUT_2_SOURCE:
	case MADERA_OUT2RMIX_INPUT_2_VOLUME:
	case MADERA_OUT2RMIX_INPUT_3_SOURCE:
	case MADERA_OUT2RMIX_INPUT_3_VOLUME:
	case MADERA_OUT2RMIX_INPUT_4_SOURCE:
	case MADERA_OUT2RMIX_INPUT_4_VOLUME:
	case MADERA_OUT3LMIX_INPUT_1_SOURCE:
	case MADERA_OUT3LMIX_INPUT_1_VOLUME:
	case MADERA_OUT3LMIX_INPUT_2_SOURCE:
	case MADERA_OUT3LMIX_INPUT_2_VOLUME:
	case MADERA_OUT3LMIX_INPUT_3_SOURCE:
	case MADERA_OUT3LMIX_INPUT_3_VOLUME:
	case MADERA_OUT3LMIX_INPUT_4_SOURCE:
	case MADERA_OUT3LMIX_INPUT_4_VOLUME:
	case MADERA_OUT3RMIX_INPUT_1_SOURCE:
	case MADERA_OUT3RMIX_INPUT_1_VOLUME:
	case MADERA_OUT3RMIX_INPUT_2_SOURCE:
	case MADERA_OUT3RMIX_INPUT_2_VOLUME:
	case MADERA_OUT3RMIX_INPUT_3_SOURCE:
	case MADERA_OUT3RMIX_INPUT_3_VOLUME:
	case MADERA_OUT3RMIX_INPUT_4_SOURCE:
	case MADERA_OUT3RMIX_INPUT_4_VOLUME:
	case MADERA_OUT5LMIX_INPUT_1_SOURCE:
	case MADERA_OUT5LMIX_INPUT_1_VOLUME:
	case MADERA_OUT5LMIX_INPUT_2_SOURCE:
	case MADERA_OUT5LMIX_INPUT_2_VOLUME:
	case MADERA_OUT5LMIX_INPUT_3_SOURCE:
	case MADERA_OUT5LMIX_INPUT_3_VOLUME:
	case MADERA_OUT5LMIX_INPUT_4_SOURCE:
	case MADERA_OUT5LMIX_INPUT_4_VOLUME:
	case MADERA_OUT5RMIX_INPUT_1_SOURCE:
	case MADERA_OUT5RMIX_INPUT_1_VOLUME:
	case MADERA_OUT5RMIX_INPUT_2_SOURCE:
	case MADERA_OUT5RMIX_INPUT_2_VOLUME:
	case MADERA_OUT5RMIX_INPUT_3_SOURCE:
	case MADERA_OUT5RMIX_INPUT_3_VOLUME:
	case MADERA_OUT5RMIX_INPUT_4_SOURCE:
	case MADERA_OUT5RMIX_INPUT_4_VOLUME:
	case MADERA_AIF1TX1MIX_INPUT_1_SOURCE:
	case MADERA_AIF1TX1MIX_INPUT_1_VOLUME:
	case MADERA_AIF1TX1MIX_INPUT_2_SOURCE:
	case MADERA_AIF1TX1MIX_INPUT_2_VOLUME:
	case MADERA_AIF1TX1MIX_INPUT_3_SOURCE:
	case MADERA_AIF1TX1MIX_INPUT_3_VOLUME:
	case MADERA_AIF1TX1MIX_INPUT_4_SOURCE:
	case MADERA_AIF1TX1MIX_INPUT_4_VOLUME:
	case MADERA_AIF1TX2MIX_INPUT_1_SOURCE:
	case MADERA_AIF1TX2MIX_INPUT_1_VOLUME:
	case MADERA_AIF1TX2MIX_INPUT_2_SOURCE:
	case MADERA_AIF1TX2MIX_INPUT_2_VOLUME:
	case MADERA_AIF1TX2MIX_INPUT_3_SOURCE:
	case MADERA_AIF1TX2MIX_INPUT_3_VOLUME:
	case MADERA_AIF1TX2MIX_INPUT_4_SOURCE:
	case MADERA_AIF1TX2MIX_INPUT_4_VOLUME:
	case MADERA_AIF1TX3MIX_INPUT_1_SOURCE:
	case MADERA_AIF1TX3MIX_INPUT_1_VOLUME:
	case MADERA_AIF1TX3MIX_INPUT_2_SOURCE:
	case MADERA_AIF1TX3MIX_INPUT_2_VOLUME:
	case MADERA_AIF1TX3MIX_INPUT_3_SOURCE:
	case MADERA_AIF1TX3MIX_INPUT_3_VOLUME:
	case MADERA_AIF1TX3MIX_INPUT_4_SOURCE:
	case MADERA_AIF1TX3MIX_INPUT_4_VOLUME:
	case MADERA_AIF1TX4MIX_INPUT_1_SOURCE:
	case MADERA_AIF1TX4MIX_INPUT_1_VOLUME:
	case MADERA_AIF1TX4MIX_INPUT_2_SOURCE:
	case MADERA_AIF1TX4MIX_INPUT_2_VOLUME:
	case MADERA_AIF1TX4MIX_INPUT_3_SOURCE:
	case MADERA_AIF1TX4MIX_INPUT_3_VOLUME:
	case MADERA_AIF1TX4MIX_INPUT_4_SOURCE:
	case MADERA_AIF1TX4MIX_INPUT_4_VOLUME:
	case MADERA_AIF1TX5MIX_INPUT_1_SOURCE:
	case MADERA_AIF1TX5MIX_INPUT_1_VOLUME:
	case MADERA_AIF1TX5MIX_INPUT_2_SOURCE:
	case MADERA_AIF1TX5MIX_INPUT_2_VOLUME:
	case MADERA_AIF1TX5MIX_INPUT_3_SOURCE:
	case MADERA_AIF1TX5MIX_INPUT_3_VOLUME:
	case MADERA_AIF1TX5MIX_INPUT_4_SOURCE:
	case MADERA_AIF1TX5MIX_INPUT_4_VOLUME:
	case MADERA_AIF1TX6MIX_INPUT_1_SOURCE:
	case MADERA_AIF1TX6MIX_INPUT_1_VOLUME:
	case MADERA_AIF1TX6MIX_INPUT_2_SOURCE:
	case MADERA_AIF1TX6MIX_INPUT_2_VOLUME:
	case MADERA_AIF1TX6MIX_INPUT_3_SOURCE:
	case MADERA_AIF1TX6MIX_INPUT_3_VOLUME:
	case MADERA_AIF1TX6MIX_INPUT_4_SOURCE:
	case MADERA_AIF1TX6MIX_INPUT_4_VOLUME:
	case MADERA_AIF1TX7MIX_INPUT_1_SOURCE:
	case MADERA_AIF1TX7MIX_INPUT_1_VOLUME:
	case MADERA_AIF1TX7MIX_INPUT_2_SOURCE:
	case MADERA_AIF1TX7MIX_INPUT_2_VOLUME:
	case MADERA_AIF1TX7MIX_INPUT_3_SOURCE:
	case MADERA_AIF1TX7MIX_INPUT_3_VOLUME:
	case MADERA_AIF1TX7MIX_INPUT_4_SOURCE:
	case MADERA_AIF1TX7MIX_INPUT_4_VOLUME:
	case MADERA_AIF1TX8MIX_INPUT_1_SOURCE:
	case MADERA_AIF1TX8MIX_INPUT_1_VOLUME:
	case MADERA_AIF1TX8MIX_INPUT_2_SOURCE:
	case MADERA_AIF1TX8MIX_INPUT_2_VOLUME:
	case MADERA_AIF1TX8MIX_INPUT_3_SOURCE:
	case MADERA_AIF1TX8MIX_INPUT_3_VOLUME:
	case MADERA_AIF1TX8MIX_INPUT_4_SOURCE:
	case MADERA_AIF1TX8MIX_INPUT_4_VOLUME:
	case MADERA_AIF2TX1MIX_INPUT_1_SOURCE:
	case MADERA_AIF2TX1MIX_INPUT_1_VOLUME:
	case MADERA_AIF2TX1MIX_INPUT_2_SOURCE:
	case MADERA_AIF2TX1MIX_INPUT_2_VOLUME:
	case MADERA_AIF2TX1MIX_INPUT_3_SOURCE:
	case MADERA_AIF2TX1MIX_INPUT_3_VOLUME:
	case MADERA_AIF2TX1MIX_INPUT_4_SOURCE:
	case MADERA_AIF2TX1MIX_INPUT_4_VOLUME:
	case MADERA_AIF2TX2MIX_INPUT_1_SOURCE:
	case MADERA_AIF2TX2MIX_INPUT_1_VOLUME:
	case MADERA_AIF2TX2MIX_INPUT_2_SOURCE:
	case MADERA_AIF2TX2MIX_INPUT_2_VOLUME:
	case MADERA_AIF2TX2MIX_INPUT_3_SOURCE:
	case MADERA_AIF2TX2MIX_INPUT_3_VOLUME:
	case MADERA_AIF2TX2MIX_INPUT_4_SOURCE:
	case MADERA_AIF2TX2MIX_INPUT_4_VOLUME:
	case MADERA_AIF2TX3MIX_INPUT_1_SOURCE:
	case MADERA_AIF2TX3MIX_INPUT_1_VOLUME:
	case MADERA_AIF2TX3MIX_INPUT_2_SOURCE:
	case MADERA_AIF2TX3MIX_INPUT_2_VOLUME:
	case MADERA_AIF2TX3MIX_INPUT_3_SOURCE:
	case MADERA_AIF2TX3MIX_INPUT_3_VOLUME:
	case MADERA_AIF2TX3MIX_INPUT_4_SOURCE:
	case MADERA_AIF2TX3MIX_INPUT_4_VOLUME:
	case MADERA_AIF2TX4MIX_INPUT_1_SOURCE:
	case MADERA_AIF2TX4MIX_INPUT_1_VOLUME:
	case MADERA_AIF2TX4MIX_INPUT_2_SOURCE:
	case MADERA_AIF2TX4MIX_INPUT_2_VOLUME:
	case MADERA_AIF2TX4MIX_INPUT_3_SOURCE:
	case MADERA_AIF2TX4MIX_INPUT_3_VOLUME:
	case MADERA_AIF2TX4MIX_INPUT_4_SOURCE:
	case MADERA_AIF2TX4MIX_INPUT_4_VOLUME:
	case MADERA_AIF2TX5MIX_INPUT_1_SOURCE:
	case MADERA_AIF2TX5MIX_INPUT_1_VOLUME:
	case MADERA_AIF2TX5MIX_INPUT_2_SOURCE:
	case MADERA_AIF2TX5MIX_INPUT_2_VOLUME:
	case MADERA_AIF2TX5MIX_INPUT_3_SOURCE:
	case MADERA_AIF2TX5MIX_INPUT_3_VOLUME:
	case MADERA_AIF2TX5MIX_INPUT_4_SOURCE:
	case MADERA_AIF2TX5MIX_INPUT_4_VOLUME:
	case MADERA_AIF2TX6MIX_INPUT_1_SOURCE:
	case MADERA_AIF2TX6MIX_INPUT_1_VOLUME:
	case MADERA_AIF2TX6MIX_INPUT_2_SOURCE:
	case MADERA_AIF2TX6MIX_INPUT_2_VOLUME:
	case MADERA_AIF2TX6MIX_INPUT_3_SOURCE:
	case MADERA_AIF2TX6MIX_INPUT_3_VOLUME:
	case MADERA_AIF2TX6MIX_INPUT_4_SOURCE:
	case MADERA_AIF2TX6MIX_INPUT_4_VOLUME:
	case MADERA_AIF2TX7MIX_INPUT_1_SOURCE:
	case MADERA_AIF2TX7MIX_INPUT_1_VOLUME:
	case MADERA_AIF2TX7MIX_INPUT_2_SOURCE:
	case MADERA_AIF2TX7MIX_INPUT_2_VOLUME:
	case MADERA_AIF2TX7MIX_INPUT_3_SOURCE:
	case MADERA_AIF2TX7MIX_INPUT_3_VOLUME:
	case MADERA_AIF2TX7MIX_INPUT_4_SOURCE:
	case MADERA_AIF2TX7MIX_INPUT_4_VOLUME:
	case MADERA_AIF2TX8MIX_INPUT_1_SOURCE:
	case MADERA_AIF2TX8MIX_INPUT_1_VOLUME:
	case MADERA_AIF2TX8MIX_INPUT_2_SOURCE:
	case MADERA_AIF2TX8MIX_INPUT_2_VOLUME:
	case MADERA_AIF2TX8MIX_INPUT_3_SOURCE:
	case MADERA_AIF2TX8MIX_INPUT_3_VOLUME:
	case MADERA_AIF2TX8MIX_INPUT_4_SOURCE:
	case MADERA_AIF2TX8MIX_INPUT_4_VOLUME:
	case MADERA_AIF3TX1MIX_INPUT_1_SOURCE:
	case MADERA_AIF3TX1MIX_INPUT_1_VOLUME:
	case MADERA_AIF3TX1MIX_INPUT_2_SOURCE:
	case MADERA_AIF3TX1MIX_INPUT_2_VOLUME:
	case MADERA_AIF3TX1MIX_INPUT_3_SOURCE:
	case MADERA_AIF3TX1MIX_INPUT_3_VOLUME:
	case MADERA_AIF3TX1MIX_INPUT_4_SOURCE:
	case MADERA_AIF3TX1MIX_INPUT_4_VOLUME:
	case MADERA_AIF3TX2MIX_INPUT_1_SOURCE:
	case MADERA_AIF3TX2MIX_INPUT_1_VOLUME:
	case MADERA_AIF3TX2MIX_INPUT_2_SOURCE:
	case MADERA_AIF3TX2MIX_INPUT_2_VOLUME:
	case MADERA_AIF3TX2MIX_INPUT_3_SOURCE:
	case MADERA_AIF3TX2MIX_INPUT_3_VOLUME:
	case MADERA_AIF3TX2MIX_INPUT_4_SOURCE:
	case MADERA_AIF3TX2MIX_INPUT_4_VOLUME:
	case MADERA_AIF3TX3MIX_INPUT_1_SOURCE:
	case MADERA_AIF3TX3MIX_INPUT_1_VOLUME:
	case MADERA_AIF3TX3MIX_INPUT_2_SOURCE:
	case MADERA_AIF3TX3MIX_INPUT_2_VOLUME:
	case MADERA_AIF3TX3MIX_INPUT_3_SOURCE:
	case MADERA_AIF3TX3MIX_INPUT_3_VOLUME:
	case MADERA_AIF3TX3MIX_INPUT_4_SOURCE:
	case MADERA_AIF3TX3MIX_INPUT_4_VOLUME:
	case MADERA_AIF3TX4MIX_INPUT_1_SOURCE:
	case MADERA_AIF3TX4MIX_INPUT_1_VOLUME:
	case MADERA_AIF3TX4MIX_INPUT_2_SOURCE:
	case MADERA_AIF3TX4MIX_INPUT_2_VOLUME:
	case MADERA_AIF3TX4MIX_INPUT_3_SOURCE:
	case MADERA_AIF3TX4MIX_INPUT_3_VOLUME:
	case MADERA_AIF3TX4MIX_INPUT_4_SOURCE:
	case MADERA_AIF3TX4MIX_INPUT_4_VOLUME:
	case CS47L92_AIF3TX5MIX_INPUT_1_SOURCE:
	case CS47L92_AIF3TX5MIX_INPUT_1_VOLUME:
	case CS47L92_AIF3TX5MIX_INPUT_2_SOURCE:
	case CS47L92_AIF3TX5MIX_INPUT_2_VOLUME:
	case CS47L92_AIF3TX5MIX_INPUT_3_SOURCE:
	case CS47L92_AIF3TX5MIX_INPUT_3_VOLUME:
	case CS47L92_AIF3TX5MIX_INPUT_4_SOURCE:
	case CS47L92_AIF3TX5MIX_INPUT_4_VOLUME:
	case CS47L92_AIF3TX6MIX_INPUT_1_SOURCE:
	case CS47L92_AIF3TX6MIX_INPUT_1_VOLUME:
	case CS47L92_AIF3TX6MIX_INPUT_2_SOURCE:
	case CS47L92_AIF3TX6MIX_INPUT_2_VOLUME:
	case CS47L92_AIF3TX6MIX_INPUT_3_SOURCE:
	case CS47L92_AIF3TX6MIX_INPUT_3_VOLUME:
	case CS47L92_AIF3TX6MIX_INPUT_4_SOURCE:
	case CS47L92_AIF3TX6MIX_INPUT_4_VOLUME:
	case CS47L92_AIF3TX7MIX_INPUT_1_SOURCE:
	case CS47L92_AIF3TX7MIX_INPUT_1_VOLUME:
	case CS47L92_AIF3TX7MIX_INPUT_2_SOURCE:
	case CS47L92_AIF3TX7MIX_INPUT_2_VOLUME:
	case CS47L92_AIF3TX7MIX_INPUT_3_SOURCE:
	case CS47L92_AIF3TX7MIX_INPUT_3_VOLUME:
	case CS47L92_AIF3TX7MIX_INPUT_4_SOURCE:
	case CS47L92_AIF3TX7MIX_INPUT_4_VOLUME:
	case CS47L92_AIF3TX8MIX_INPUT_1_SOURCE:
	case CS47L92_AIF3TX8MIX_INPUT_1_VOLUME:
	case CS47L92_AIF3TX8MIX_INPUT_2_SOURCE:
	case CS47L92_AIF3TX8MIX_INPUT_2_VOLUME:
	case CS47L92_AIF3TX8MIX_INPUT_3_SOURCE:
	case CS47L92_AIF3TX8MIX_INPUT_3_VOLUME:
	case CS47L92_AIF3TX8MIX_INPUT_4_SOURCE:
	case CS47L92_AIF3TX8MIX_INPUT_4_VOLUME:
	case MADERA_SLIMTX1MIX_INPUT_1_SOURCE:
	case MADERA_SLIMTX1MIX_INPUT_1_VOLUME:
	case MADERA_SLIMTX1MIX_INPUT_2_SOURCE:
	case MADERA_SLIMTX1MIX_INPUT_2_VOLUME:
	case MADERA_SLIMTX1MIX_INPUT_3_SOURCE:
	case MADERA_SLIMTX1MIX_INPUT_3_VOLUME:
	case MADERA_SLIMTX1MIX_INPUT_4_SOURCE:
	case MADERA_SLIMTX1MIX_INPUT_4_VOLUME:
	case MADERA_SLIMTX2MIX_INPUT_1_SOURCE:
	case MADERA_SLIMTX2MIX_INPUT_1_VOLUME:
	case MADERA_SLIMTX2MIX_INPUT_2_SOURCE:
	case MADERA_SLIMTX2MIX_INPUT_2_VOLUME:
	case MADERA_SLIMTX2MIX_INPUT_3_SOURCE:
	case MADERA_SLIMTX2MIX_INPUT_3_VOLUME:
	case MADERA_SLIMTX2MIX_INPUT_4_SOURCE:
	case MADERA_SLIMTX2MIX_INPUT_4_VOLUME:
	case MADERA_SLIMTX3MIX_INPUT_1_SOURCE:
	case MADERA_SLIMTX3MIX_INPUT_1_VOLUME:
	case MADERA_SLIMTX3MIX_INPUT_2_SOURCE:
	case MADERA_SLIMTX3MIX_INPUT_2_VOLUME:
	case MADERA_SLIMTX3MIX_INPUT_3_SOURCE:
	case MADERA_SLIMTX3MIX_INPUT_3_VOLUME:
	case MADERA_SLIMTX3MIX_INPUT_4_SOURCE:
	case MADERA_SLIMTX3MIX_INPUT_4_VOLUME:
	case MADERA_SLIMTX4MIX_INPUT_1_SOURCE:
	case MADERA_SLIMTX4MIX_INPUT_1_VOLUME:
	case MADERA_SLIMTX4MIX_INPUT_2_SOURCE:
	case MADERA_SLIMTX4MIX_INPUT_2_VOLUME:
	case MADERA_SLIMTX4MIX_INPUT_3_SOURCE:
	case MADERA_SLIMTX4MIX_INPUT_3_VOLUME:
	case MADERA_SLIMTX4MIX_INPUT_4_SOURCE:
	case MADERA_SLIMTX4MIX_INPUT_4_VOLUME:
	case MADERA_SLIMTX5MIX_INPUT_1_SOURCE:
	case MADERA_SLIMTX5MIX_INPUT_1_VOLUME:
	case MADERA_SLIMTX5MIX_INPUT_2_SOURCE:
	case MADERA_SLIMTX5MIX_INPUT_2_VOLUME:
	case MADERA_SLIMTX5MIX_INPUT_3_SOURCE:
	case MADERA_SLIMTX5MIX_INPUT_3_VOLUME:
	case MADERA_SLIMTX5MIX_INPUT_4_SOURCE:
	case MADERA_SLIMTX5MIX_INPUT_4_VOLUME:
	case MADERA_SLIMTX6MIX_INPUT_1_SOURCE:
	case MADERA_SLIMTX6MIX_INPUT_1_VOLUME:
	case MADERA_SLIMTX6MIX_INPUT_2_SOURCE:
	case MADERA_SLIMTX6MIX_INPUT_2_VOLUME:
	case MADERA_SLIMTX6MIX_INPUT_3_SOURCE:
	case MADERA_SLIMTX6MIX_INPUT_3_VOLUME:
	case MADERA_SLIMTX6MIX_INPUT_4_SOURCE:
	case MADERA_SLIMTX6MIX_INPUT_4_VOLUME:
	case MADERA_SLIMTX7MIX_INPUT_1_SOURCE:
	case MADERA_SLIMTX7MIX_INPUT_1_VOLUME:
	case MADERA_SLIMTX7MIX_INPUT_2_SOURCE:
	case MADERA_SLIMTX7MIX_INPUT_2_VOLUME:
	case MADERA_SLIMTX7MIX_INPUT_3_SOURCE:
	case MADERA_SLIMTX7MIX_INPUT_3_VOLUME:
	case MADERA_SLIMTX7MIX_INPUT_4_SOURCE:
	case MADERA_SLIMTX7MIX_INPUT_4_VOLUME:
	case MADERA_SLIMTX8MIX_INPUT_1_SOURCE:
	case MADERA_SLIMTX8MIX_INPUT_1_VOLUME:
	case MADERA_SLIMTX8MIX_INPUT_2_SOURCE:
	case MADERA_SLIMTX8MIX_INPUT_2_VOLUME:
	case MADERA_SLIMTX8MIX_INPUT_3_SOURCE:
	case MADERA_SLIMTX8MIX_INPUT_3_VOLUME:
	case MADERA_SLIMTX8MIX_INPUT_4_SOURCE:
	case MADERA_SLIMTX8MIX_INPUT_4_VOLUME:
	case MADERA_SPDIF1TX1MIX_INPUT_1_SOURCE:
	case MADERA_SPDIF1TX1MIX_INPUT_1_VOLUME:
	case MADERA_SPDIF1TX2MIX_INPUT_1_SOURCE:
	case MADERA_SPDIF1TX2MIX_INPUT_1_VOLUME:
	case MADERA_EQ1MIX_INPUT_1_SOURCE:
	case MADERA_EQ1MIX_INPUT_1_VOLUME:
	case MADERA_EQ1MIX_INPUT_2_SOURCE:
	case MADERA_EQ1MIX_INPUT_2_VOLUME:
	case MADERA_EQ1MIX_INPUT_3_SOURCE:
	case MADERA_EQ1MIX_INPUT_3_VOLUME:
	case MADERA_EQ1MIX_INPUT_4_SOURCE:
	case MADERA_EQ1MIX_INPUT_4_VOLUME:
	case MADERA_EQ2MIX_INPUT_1_SOURCE:
	case MADERA_EQ2MIX_INPUT_1_VOLUME:
	case MADERA_EQ2MIX_INPUT_2_SOURCE:
	case MADERA_EQ2MIX_INPUT_2_VOLUME:
	case MADERA_EQ2MIX_INPUT_3_SOURCE:
	case MADERA_EQ2MIX_INPUT_3_VOLUME:
	case MADERA_EQ2MIX_INPUT_4_SOURCE:
	case MADERA_EQ2MIX_INPUT_4_VOLUME:
	case MADERA_EQ3MIX_INPUT_1_SOURCE:
	case MADERA_EQ3MIX_INPUT_1_VOLUME:
	case MADERA_EQ3MIX_INPUT_2_SOURCE:
	case MADERA_EQ3MIX_INPUT_2_VOLUME:
	case MADERA_EQ3MIX_INPUT_3_SOURCE:
	case MADERA_EQ3MIX_INPUT_3_VOLUME:
	case MADERA_EQ3MIX_INPUT_4_SOURCE:
	case MADERA_EQ3MIX_INPUT_4_VOLUME:
	case MADERA_EQ4MIX_INPUT_1_SOURCE:
	case MADERA_EQ4MIX_INPUT_1_VOLUME:
	case MADERA_EQ4MIX_INPUT_2_SOURCE:
	case MADERA_EQ4MIX_INPUT_2_VOLUME:
	case MADERA_EQ4MIX_INPUT_3_SOURCE:
	case MADERA_EQ4MIX_INPUT_3_VOLUME:
	case MADERA_EQ4MIX_INPUT_4_SOURCE:
	case MADERA_EQ4MIX_INPUT_4_VOLUME:
	case MADERA_DRC1LMIX_INPUT_1_SOURCE:
	case MADERA_DRC1LMIX_INPUT_1_VOLUME:
	case MADERA_DRC1LMIX_INPUT_2_SOURCE:
	case MADERA_DRC1LMIX_INPUT_2_VOLUME:
	case MADERA_DRC1LMIX_INPUT_3_SOURCE:
	case MADERA_DRC1LMIX_INPUT_3_VOLUME:
	case MADERA_DRC1LMIX_INPUT_4_SOURCE:
	case MADERA_DRC1LMIX_INPUT_4_VOLUME:
	case MADERA_DRC1RMIX_INPUT_1_SOURCE:
	case MADERA_DRC1RMIX_INPUT_1_VOLUME:
	case MADERA_DRC1RMIX_INPUT_2_SOURCE:
	case MADERA_DRC1RMIX_INPUT_2_VOLUME:
	case MADERA_DRC1RMIX_INPUT_3_SOURCE:
	case MADERA_DRC1RMIX_INPUT_3_VOLUME:
	case MADERA_DRC1RMIX_INPUT_4_SOURCE:
	case MADERA_DRC1RMIX_INPUT_4_VOLUME:
	case MADERA_DRC2LMIX_INPUT_1_SOURCE:
	case MADERA_DRC2LMIX_INPUT_1_VOLUME:
	case MADERA_DRC2LMIX_INPUT_2_SOURCE:
	case MADERA_DRC2LMIX_INPUT_2_VOLUME:
	case MADERA_DRC2LMIX_INPUT_3_SOURCE:
	case MADERA_DRC2LMIX_INPUT_3_VOLUME:
	case MADERA_DRC2LMIX_INPUT_4_SOURCE:
	case MADERA_DRC2LMIX_INPUT_4_VOLUME:
	case MADERA_DRC2RMIX_INPUT_1_SOURCE:
	case MADERA_DRC2RMIX_INPUT_1_VOLUME:
	case MADERA_DRC2RMIX_INPUT_2_SOURCE:
	case MADERA_DRC2RMIX_INPUT_2_VOLUME:
	case MADERA_DRC2RMIX_INPUT_3_SOURCE:
	case MADERA_DRC2RMIX_INPUT_3_VOLUME:
	case MADERA_DRC2RMIX_INPUT_4_SOURCE:
	case MADERA_DRC2RMIX_INPUT_4_VOLUME:
	case MADERA_HPLP1MIX_INPUT_1_SOURCE:
	case MADERA_HPLP1MIX_INPUT_1_VOLUME:
	case MADERA_HPLP1MIX_INPUT_2_SOURCE:
	case MADERA_HPLP1MIX_INPUT_2_VOLUME:
	case MADERA_HPLP1MIX_INPUT_3_SOURCE:
	case MADERA_HPLP1MIX_INPUT_3_VOLUME:
	case MADERA_HPLP1MIX_INPUT_4_SOURCE:
	case MADERA_HPLP1MIX_INPUT_4_VOLUME:
	case MADERA_HPLP2MIX_INPUT_1_SOURCE:
	case MADERA_HPLP2MIX_INPUT_1_VOLUME:
	case MADERA_HPLP2MIX_INPUT_2_SOURCE:
	case MADERA_HPLP2MIX_INPUT_2_VOLUME:
	case MADERA_HPLP2MIX_INPUT_3_SOURCE:
	case MADERA_HPLP2MIX_INPUT_3_VOLUME:
	case MADERA_HPLP2MIX_INPUT_4_SOURCE:
	case MADERA_HPLP2MIX_INPUT_4_VOLUME:
	case MADERA_HPLP3MIX_INPUT_1_SOURCE:
	case MADERA_HPLP3MIX_INPUT_1_VOLUME:
	case MADERA_HPLP3MIX_INPUT_2_SOURCE:
	case MADERA_HPLP3MIX_INPUT_2_VOLUME:
	case MADERA_HPLP3MIX_INPUT_3_SOURCE:
	case MADERA_HPLP3MIX_INPUT_3_VOLUME:
	case MADERA_HPLP3MIX_INPUT_4_SOURCE:
	case MADERA_HPLP3MIX_INPUT_4_VOLUME:
	case MADERA_HPLP4MIX_INPUT_1_SOURCE:
	case MADERA_HPLP4MIX_INPUT_1_VOLUME:
	case MADERA_HPLP4MIX_INPUT_2_SOURCE:
	case MADERA_HPLP4MIX_INPUT_2_VOLUME:
	case MADERA_HPLP4MIX_INPUT_3_SOURCE:
	case MADERA_HPLP4MIX_INPUT_3_VOLUME:
	case MADERA_HPLP4MIX_INPUT_4_SOURCE:
	case MADERA_HPLP4MIX_INPUT_4_VOLUME:
	case MADERA_DSP1LMIX_INPUT_1_SOURCE:
	case MADERA_DSP1LMIX_INPUT_1_VOLUME:
	case MADERA_DSP1LMIX_INPUT_2_SOURCE:
	case MADERA_DSP1LMIX_INPUT_2_VOLUME:
	case MADERA_DSP1LMIX_INPUT_3_SOURCE:
	case MADERA_DSP1LMIX_INPUT_3_VOLUME:
	case MADERA_DSP1LMIX_INPUT_4_SOURCE:
	case MADERA_DSP1LMIX_INPUT_4_VOLUME:
	case MADERA_DSP1RMIX_INPUT_1_SOURCE:
	case MADERA_DSP1RMIX_INPUT_1_VOLUME:
	case MADERA_DSP1RMIX_INPUT_2_SOURCE:
	case MADERA_DSP1RMIX_INPUT_2_VOLUME:
	case MADERA_DSP1RMIX_INPUT_3_SOURCE:
	case MADERA_DSP1RMIX_INPUT_3_VOLUME:
	case MADERA_DSP1RMIX_INPUT_4_SOURCE:
	case MADERA_DSP1RMIX_INPUT_4_VOLUME:
	case MADERA_DSP1AUX1MIX_INPUT_1_SOURCE:
	case MADERA_DSP1AUX2MIX_INPUT_1_SOURCE:
	case MADERA_DSP1AUX3MIX_INPUT_1_SOURCE:
	case MADERA_DSP1AUX4MIX_INPUT_1_SOURCE:
	case MADERA_DSP1AUX5MIX_INPUT_1_SOURCE:
	case MADERA_DSP1AUX6MIX_INPUT_1_SOURCE:
	case MADERA_ASRC1_1LMIX_INPUT_1_SOURCE:
	case MADERA_ASRC1_1RMIX_INPUT_1_SOURCE:
	case MADERA_ASRC1_2LMIX_INPUT_1_SOURCE:
	case MADERA_ASRC1_2RMIX_INPUT_1_SOURCE:
	case MADERA_ISRC1DEC1MIX_INPUT_1_SOURCE:
	case MADERA_ISRC1DEC2MIX_INPUT_1_SOURCE:
	case MADERA_ISRC1INT1MIX_INPUT_1_SOURCE:
	case MADERA_ISRC1INT2MIX_INPUT_1_SOURCE:
	case MADERA_ISRC2DEC1MIX_INPUT_1_SOURCE:
	case MADERA_ISRC2DEC2MIX_INPUT_1_SOURCE:
	case MADERA_ISRC2INT1MIX_INPUT_1_SOURCE:
	case MADERA_ISRC2INT2MIX_INPUT_1_SOURCE:
	case MADERA_DFC1MIX_INPUT_1_SOURCE:
	case MADERA_DFC2MIX_INPUT_1_SOURCE:
	case MADERA_DFC3MIX_INPUT_1_SOURCE:
	case MADERA_DFC4MIX_INPUT_1_SOURCE:
	case MADERA_DFC5MIX_INPUT_1_SOURCE:
	case MADERA_DFC6MIX_INPUT_1_SOURCE:
	case MADERA_DFC7MIX_INPUT_1_SOURCE:
	case MADERA_DFC8MIX_INPUT_1_SOURCE:
	case MADERA_FX_CTRL1 ... MADERA_FX_CTRL2:
	case MADERA_EQ1_1 ... MADERA_EQ1_21:
	case MADERA_EQ2_1 ... MADERA_EQ2_21:
	case MADERA_EQ3_1 ... MADERA_EQ3_21:
	case MADERA_EQ4_1 ... MADERA_EQ4_21:
	case MADERA_DRC1_CTRL1 ... MADERA_DRC1_CTRL5:
	case MADERA_DRC2_CTRL1 ... MADERA_DRC2_CTRL5:
	case MADERA_HPLPF1_1 ... MADERA_HPLPF1_2:
	case MADERA_HPLPF2_1 ... MADERA_HPLPF2_2:
	case MADERA_HPLPF3_1 ... MADERA_HPLPF3_2:
	case MADERA_HPLPF4_1 ... MADERA_HPLPF4_2:
	case MADERA_ASRC1_ENABLE:
	case MADERA_ASRC1_STATUS:
	case MADERA_ASRC1_RATE1 ... MADERA_ASRC1_RATE2:
	case MADERA_ISRC_1_CTRL_1 ... MADERA_ISRC_1_CTRL_3:
	case MADERA_ISRC_2_CTRL_1 ... MADERA_ISRC_2_CTRL_3:
	case MADERA_AUXPDM1_CTRL_0 ... MADERA_AUXPDM1_CTRL_1:
	case MADERA_DFC1_CTRL:
	case MADERA_DFC1_RX:
	case MADERA_DFC1_TX:
	case MADERA_DFC2_CTRL:
	case MADERA_DFC2_RX:
	case MADERA_DFC2_TX:
	case MADERA_DFC3_CTRL:
	case MADERA_DFC3_RX:
	case MADERA_DFC3_TX:
	case MADERA_DFC4_CTRL:
	case MADERA_DFC4_RX:
	case MADERA_DFC4_TX:
	case MADERA_DFC5_CTRL:
	case MADERA_DFC5_RX:
	case MADERA_DFC5_TX:
	case MADERA_DFC6_CTRL:
	case MADERA_DFC6_RX:
	case MADERA_DFC6_TX:
	case MADERA_DFC7_CTRL:
	case MADERA_DFC7_RX:
	case MADERA_DFC7_TX:
	case MADERA_DFC8_CTRL:
	case MADERA_DFC8_RX:
	case MADERA_DFC8_TX:
	case MADERA_DFC_STATUS:
	case MADERA_GPIO1_CTRL_1 ... MADERA_GPIO16_CTRL_2:
	case MADERA_IRQ1_STATUS_1 ... MADERA_IRQ1_STATUS_33:
	case MADERA_IRQ1_MASK_1 ... MADERA_IRQ1_MASK_33:
	case MADERA_IRQ1_RAW_STATUS_1 ... MADERA_IRQ1_RAW_STATUS_33:
	case MADERA_INTERRUPT_DEBOUNCE_7:
	case MADERA_IRQ1_CTRL:
		return true;
	default:
		return false;
	}
}

static bool cs47l92_16bit_volatile_register(struct device *dev,
					    unsigned int reg)
{
	switch (reg) {
	case MADERA_SOFTWARE_RESET:
	case MADERA_HARDWARE_REVISION:
	case MADERA_WRITE_SEQUENCER_CTRL_0 ... MADERA_WRITE_SEQUENCER_CTRL_2:
	case MADERA_HAPTICS_STATUS:
	case MADERA_SAMPLE_RATE_1_STATUS:
	case MADERA_SAMPLE_RATE_2_STATUS:
	case MADERA_SAMPLE_RATE_3_STATUS:
	case MADERA_ASYNC_SAMPLE_RATE_1_STATUS:
	case MADERA_ASYNC_SAMPLE_RATE_2_STATUS:
	case MADERA_HP_CTRL_1L:
	case MADERA_HP_CTRL_1R:
	case MADERA_HP_CTRL_2L:
	case MADERA_HP_CTRL_2R:
	case MADERA_HP_CTRL_3L:
	case MADERA_HP_CTRL_3R:
	case MADERA_MIC_DETECT_1_CONTROL_3:
	case MADERA_MIC_DETECT_1_CONTROL_4:
	case MADERA_MIC_DETECT_2_CONTROL_3:
	case MADERA_MIC_DETECT_2_CONTROL_4:
	case MADERA_HEADPHONE_DETECT_2:
	case MADERA_HEADPHONE_DETECT_3:
	case MADERA_HEADPHONE_DETECT_5:
	case MADERA_INPUT_ENABLES_STATUS:
	case MADERA_OUTPUT_STATUS_1:
	case MADERA_RAW_OUTPUT_STATUS_1:
	case MADERA_SPD1_TX_CHANNEL_STATUS_1:
	case MADERA_SPD1_TX_CHANNEL_STATUS_2:
	case MADERA_SPD1_TX_CHANNEL_STATUS_3:
	case MADERA_SLIMBUS_RX_PORT_STATUS:
	case MADERA_SLIMBUS_TX_PORT_STATUS:
	case MADERA_FX_CTRL2:
	case MADERA_ASRC1_STATUS:
	case MADERA_DFC_STATUS:
	case MADERA_IRQ1_STATUS_1 ... MADERA_IRQ1_STATUS_33:
	case MADERA_IRQ1_RAW_STATUS_1 ... MADERA_IRQ1_RAW_STATUS_33:
		return true;
	default:
		return false;
	}
}

static bool cs47l92_32bit_readable_register(struct device *dev,
					    unsigned int reg)
{
	switch (reg) {
	case MADERA_WSEQ_SEQUENCE_1 ... MADERA_WSEQ_SEQUENCE_508:
	case MADERA_OTP_HPDET_CAL_1 ... MADERA_OTP_HPDET_CAL_2:
	case MADERA_DSP1_CONFIG_1 ... MADERA_DSP1_PMEM_ERR_ADDR___XMEM_ERR_ADDR:
		return true;
	default:
		return cs47l92_is_adsp_memory(reg);
	}
}

static bool cs47l92_32bit_volatile_register(struct device *dev,
					    unsigned int reg)
{
	switch (reg) {
	case MADERA_WSEQ_SEQUENCE_1 ... MADERA_WSEQ_SEQUENCE_508:
	case MADERA_OTP_HPDET_CAL_1 ... MADERA_OTP_HPDET_CAL_2:
	case MADERA_DSP1_CONFIG_1 ... MADERA_DSP1_PMEM_ERR_ADDR___XMEM_ERR_ADDR:
		return true;
	default:
		return cs47l92_is_adsp_memory(reg);
	}
}

const struct regmap_config cs47l92_16bit_spi_regmap = {
	.name = "cs47l92_16bit",
	.reg_bits = 32,
	.pad_bits = 16,
	.val_bits = 16,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = MADERA_INTERRUPT_RAW_STATUS_1,
	.readable_reg = &cs47l92_16bit_readable_register,
	.volatile_reg = &cs47l92_16bit_volatile_register,

	.cache_type = REGCACHE_MAPLE,
	.reg_defaults = cs47l92_reg_default,
	.num_reg_defaults = ARRAY_SIZE(cs47l92_reg_default),
};
EXPORT_SYMBOL_GPL(cs47l92_16bit_spi_regmap);

const struct regmap_config cs47l92_16bit_i2c_regmap = {
	.name = "cs47l92_16bit",
	.reg_bits = 32,
	.val_bits = 16,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = MADERA_INTERRUPT_RAW_STATUS_1,
	.readable_reg = &cs47l92_16bit_readable_register,
	.volatile_reg = &cs47l92_16bit_volatile_register,

	.cache_type = REGCACHE_MAPLE,
	.reg_defaults = cs47l92_reg_default,
	.num_reg_defaults = ARRAY_SIZE(cs47l92_reg_default),
};
EXPORT_SYMBOL_GPL(cs47l92_16bit_i2c_regmap);

const struct regmap_config cs47l92_32bit_spi_regmap = {
	.name = "cs47l92_32bit",
	.reg_bits = 32,
	.reg_stride = 2,
	.pad_bits = 16,
	.val_bits = 32,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = MADERA_DSP1_PMEM_ERR_ADDR___XMEM_ERR_ADDR,
	.readable_reg = &cs47l92_32bit_readable_register,
	.volatile_reg = &cs47l92_32bit_volatile_register,

	.cache_type = REGCACHE_MAPLE,
};
EXPORT_SYMBOL_GPL(cs47l92_32bit_spi_regmap);

const struct regmap_config cs47l92_32bit_i2c_regmap = {
	.name = "cs47l92_32bit",
	.reg_bits = 32,
	.reg_stride = 2,
	.val_bits = 32,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = MADERA_DSP1_PMEM_ERR_ADDR___XMEM_ERR_ADDR,
	.readable_reg = &cs47l92_32bit_readable_register,
	.volatile_reg = &cs47l92_32bit_volatile_register,

	.cache_type = REGCACHE_MAPLE,
};
EXPORT_SYMBOL_GPL(cs47l92_32bit_i2c_regmap);
