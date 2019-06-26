/*
 * wm5110-tables.c  --  WM5110 data tables
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
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

#define WM5110_NUM_AOD_ISR 2
#define WM5110_NUM_ISR 5

static const struct reg_sequence wm5110_reva_patch[] = {
	{ 0x80, 0x3 },
	{ 0x44, 0x20 },
	{ 0x45, 0x40 },
	{ 0x46, 0x60 },
	{ 0x47, 0x80 },
	{ 0x48, 0xa0 },
	{ 0x51, 0x13 },
	{ 0x52, 0x33 },
	{ 0x53, 0x53 },
	{ 0x54, 0x73 },
	{ 0x55, 0x75 },
	{ 0x56, 0xb3 },
	{ 0x2ef, 0x124 },
	{ 0x2ef, 0x124 },
	{ 0x2f0, 0x124 },
	{ 0x2f0, 0x124 },
	{ 0x2f1, 0x124 },
	{ 0x2f1, 0x124 },
	{ 0x2f2, 0x124 },
	{ 0x2f2, 0x124 },
	{ 0x2f3, 0x124 },
	{ 0x2f3, 0x124 },
	{ 0x2f4, 0x124 },
	{ 0x2f4, 0x124 },
	{ 0x2eb, 0x60 },
	{ 0x2ec, 0x60 },
	{ 0x2ed, 0x60 },
	{ 0xc30, 0x3e3e },
	{ 0xc30, 0x3e3e },
	{ 0xc31, 0x3e },
	{ 0xc32, 0x3e3e },
	{ 0xc32, 0x3e3e },
	{ 0xc33, 0x3e3e },
	{ 0xc33, 0x3e3e },
	{ 0xc34, 0x3e3e },
	{ 0xc34, 0x3e3e },
	{ 0xc35, 0x3e3e },
	{ 0xc35, 0x3e3e },
	{ 0xc36, 0x3e3e },
	{ 0xc36, 0x3e3e },
	{ 0xc37, 0x3e3e },
	{ 0xc37, 0x3e3e },
	{ 0xc38, 0x3e3e },
	{ 0xc38, 0x3e3e },
	{ 0xc30, 0x3e3e },
	{ 0xc30, 0x3e3e },
	{ 0xc39, 0x3e3e },
	{ 0xc39, 0x3e3e },
	{ 0xc3a, 0x3e3e },
	{ 0xc3a, 0x3e3e },
	{ 0xc3b, 0x3e3e },
	{ 0xc3b, 0x3e3e },
	{ 0xc3c, 0x3e },
	{ 0x201, 0x18a5 },
	{ 0x201, 0x18a5 },
	{ 0x201, 0x18a5 },
	{ 0x202, 0x4100 },
	{ 0x460, 0xc00 },
	{ 0x461, 0x8000 },
	{ 0x462, 0xc01 },
	{ 0x463, 0x50f0 },
	{ 0x464, 0xc01 },
	{ 0x465, 0x4820 },
	{ 0x466, 0xc01 },
	{ 0x466, 0xc01 },
	{ 0x467, 0x4040 },
	{ 0x468, 0xc01 },
	{ 0x468, 0xc01 },
	{ 0x469, 0x3940 },
	{ 0x46a, 0xc01 },
	{ 0x46a, 0xc01 },
	{ 0x46a, 0xc01 },
	{ 0x46b, 0x3310 },
	{ 0x46c, 0x801 },
	{ 0x46c, 0x801 },
	{ 0x46d, 0x2d80 },
	{ 0x46e, 0x801 },
	{ 0x46e, 0x801 },
	{ 0x46f, 0x2890 },
	{ 0x470, 0x801 },
	{ 0x470, 0x801 },
	{ 0x471, 0x1990 },
	{ 0x472, 0x801 },
	{ 0x472, 0x801 },
	{ 0x473, 0x1450 },
	{ 0x474, 0x801 },
	{ 0x474, 0x801 },
	{ 0x474, 0x801 },
	{ 0x475, 0x1020 },
	{ 0x476, 0x801 },
	{ 0x476, 0x801 },
	{ 0x476, 0x801 },
	{ 0x477, 0xcd0 },
	{ 0x478, 0x806 },
	{ 0x478, 0x806 },
	{ 0x479, 0xa30 },
	{ 0x47a, 0x806 },
	{ 0x47a, 0x806 },
	{ 0x47b, 0x810 },
	{ 0x47c, 0x80e },
	{ 0x47c, 0x80e },
	{ 0x47d, 0x510 },
	{ 0x47e, 0x81f },
	{ 0x47e, 0x81f },
	{ 0x2DB, 0x0A00 },
	{ 0x2DD, 0x0023 },
	{ 0x2DF, 0x0102 },
	{ 0x80, 0x0 },
	{ 0xC20, 0x0002 },
	{ 0x209, 0x002A },
};

static const struct reg_sequence wm5110_revb_patch[] = {
	{ 0x80, 0x3 },
	{ 0x36e, 0x0210 },
	{ 0x370, 0x0210 },
	{ 0x372, 0x0210 },
	{ 0x374, 0x0210 },
	{ 0x376, 0x0210 },
	{ 0x378, 0x0210 },
	{ 0x36d, 0x0028 },
	{ 0x36f, 0x0028 },
	{ 0x371, 0x0028 },
	{ 0x373, 0x0028 },
	{ 0x375, 0x0028 },
	{ 0x377, 0x0028 },
	{ 0x280, 0x2002 },
	{ 0x44, 0x20 },
	{ 0x45, 0x40 },
	{ 0x46, 0x60 },
	{ 0x47, 0x80 },
	{ 0x48, 0xa0 },
	{ 0x51, 0x13 },
	{ 0x52, 0x33 },
	{ 0x53, 0x53 },
	{ 0x54, 0x73 },
	{ 0x55, 0x93 },
	{ 0x56, 0xb3 },
	{ 0xc30, 0x3e3e },
	{ 0xc31, 0x3e },
	{ 0xc32, 0x3e3e },
	{ 0xc33, 0x3e3e },
	{ 0xc34, 0x3e3e },
	{ 0xc35, 0x3e3e },
	{ 0xc36, 0x3e3e },
	{ 0xc37, 0x3e3e },
	{ 0xc38, 0x3e3e },
	{ 0xc39, 0x3e3e },
	{ 0xc3a, 0x3e3e },
	{ 0xc3b, 0x3e3e },
	{ 0xc3c, 0x3e },
	{ 0x201, 0x18a5 },
	{ 0x202, 0x4100 },
	{ 0x460, 0x0c40 },
	{ 0x461, 0x8000 },
	{ 0x462, 0x0c41 },
	{ 0x463, 0x4820 },
	{ 0x464, 0x0c41 },
	{ 0x465, 0x4040 },
	{ 0x466, 0x0841 },
	{ 0x467, 0x3940 },
	{ 0x468, 0x0841 },
	{ 0x469, 0x2030 },
	{ 0x46a, 0x0842 },
	{ 0x46b, 0x1990 },
	{ 0x46c, 0x08c2 },
	{ 0x46d, 0x1450 },
	{ 0x46e, 0x08c6 },
	{ 0x46f, 0x1020 },
	{ 0x470, 0x08c6 },
	{ 0x471, 0x0cd0 },
	{ 0x472, 0x08c6 },
	{ 0x473, 0x0a30 },
	{ 0x474, 0x0442 },
	{ 0x475, 0x0660 },
	{ 0x476, 0x0446 },
	{ 0x477, 0x0510 },
	{ 0x478, 0x04c6 },
	{ 0x479, 0x0400 },
	{ 0x47a, 0x04ce },
	{ 0x47b, 0x0330 },
	{ 0x47c, 0x05df },
	{ 0x47d, 0x0001 },
	{ 0x47e, 0x07ff },
	{ 0x2db, 0x0a00 },
	{ 0x2dd, 0x0023 },
	{ 0x2df, 0x0102 },
	{ 0x2ef, 0x924 },
	{ 0x2f0, 0x924 },
	{ 0x2f1, 0x924 },
	{ 0x2f2, 0x924 },
	{ 0x2f3, 0x924 },
	{ 0x2f4, 0x924 },
	{ 0x2eb, 0x60 },
	{ 0x2ec, 0x60 },
	{ 0x2ed, 0x60 },
	{ 0x4f2, 0x33e },
	{ 0x458, 0x0000 },
	{ 0x15a, 0x0003 },
	{ 0x80, 0x0 },
};

static const struct reg_sequence wm5110_revd_patch[] = {
	{ 0x80, 0x3 },
	{ 0x80, 0x3 },
	{ 0x393, 0x27 },
	{ 0x394, 0x27 },
	{ 0x395, 0x27 },
	{ 0x396, 0x27 },
	{ 0x397, 0x27 },
	{ 0x398, 0x26 },
	{ 0x221, 0x90 },
	{ 0x211, 0x8 },
	{ 0x36c, 0x1fb },
	{ 0x26e, 0x64 },
	{ 0x26f, 0xea },
	{ 0x270, 0x1f16 },
	{ 0x51b, 0x1 },
	{ 0x55b, 0x1 },
	{ 0x59b, 0x1 },
	{ 0x4f0, 0x633 },
	{ 0x441, 0xc059 },
	{ 0x209, 0x27 },
	{ 0x80, 0x0 },
	{ 0x80, 0x0 },
};

/* Add extra headphone write sequence locations */
static const struct reg_sequence wm5110_reve_patch[] = {
	{ 0x80, 0x3 },
	{ 0x80, 0x3 },
	{ 0x4b, 0x138 },
	{ 0x4c, 0x13d },
	{ 0x80, 0x0 },
	{ 0x80, 0x0 },
};

/* We use a function so we can use ARRAY_SIZE() */
int wm5110_patch(struct arizona *arizona)
{
	switch (arizona->rev) {
	case 0:
		return regmap_register_patch(arizona->regmap,
					     wm5110_reva_patch,
					     ARRAY_SIZE(wm5110_reva_patch));
	case 1:
		return regmap_register_patch(arizona->regmap,
					     wm5110_revb_patch,
					     ARRAY_SIZE(wm5110_revb_patch));
	case 3:
		return regmap_register_patch(arizona->regmap,
					     wm5110_revd_patch,
					     ARRAY_SIZE(wm5110_revd_patch));
	default:
		return regmap_register_patch(arizona->regmap,
					     wm5110_reve_patch,
					     ARRAY_SIZE(wm5110_reve_patch));
	}
}
EXPORT_SYMBOL_GPL(wm5110_patch);

static const struct regmap_irq wm5110_aod_irqs[ARIZONA_NUM_IRQ] = {
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

const struct regmap_irq_chip wm5110_aod = {
	.name = "wm5110 AOD",
	.status_base = ARIZONA_AOD_IRQ1,
	.mask_base = ARIZONA_AOD_IRQ_MASK_IRQ1,
	.ack_base = ARIZONA_AOD_IRQ1,
	.wake_base = ARIZONA_WAKE_CONTROL,
	.wake_invert = 1,
	.num_regs = 1,
	.irqs = wm5110_aod_irqs,
	.num_irqs = ARRAY_SIZE(wm5110_aod_irqs),
};
EXPORT_SYMBOL_GPL(wm5110_aod);

static const struct regmap_irq wm5110_irqs[ARIZONA_NUM_IRQ] = {
	[ARIZONA_IRQ_GP4] = { .reg_offset = 0, .mask = ARIZONA_GP4_EINT1 },
	[ARIZONA_IRQ_GP3] = { .reg_offset = 0, .mask = ARIZONA_GP3_EINT1 },
	[ARIZONA_IRQ_GP2] = { .reg_offset = 0, .mask = ARIZONA_GP2_EINT1 },
	[ARIZONA_IRQ_GP1] = { .reg_offset = 0, .mask = ARIZONA_GP1_EINT1 },

	[ARIZONA_IRQ_DSP4_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP4_RAM_RDY_EINT1
	},
	[ARIZONA_IRQ_DSP3_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP3_RAM_RDY_EINT1
	},
	[ARIZONA_IRQ_DSP2_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP2_RAM_RDY_EINT1
	},
	[ARIZONA_IRQ_DSP1_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP1_RAM_RDY_EINT1
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
	[ARIZONA_IRQ_HP3R_DONE] = {
		.reg_offset = 3, .mask = ARIZONA_HP3R_DONE_EINT1
	},
	[ARIZONA_IRQ_HP3L_DONE] = {
		.reg_offset = 3, .mask = ARIZONA_HP3L_DONE_EINT1
	},
	[ARIZONA_IRQ_HP2R_DONE] = {
		.reg_offset = 3, .mask = ARIZONA_HP2R_DONE_EINT1
	},
	[ARIZONA_IRQ_HP2L_DONE] = {
		.reg_offset = 3, .mask = ARIZONA_HP2L_DONE_EINT1
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
	[ARIZONA_IRQ_FLL2_CLOCK_OK] = {
		.reg_offset = 4, .mask = ARIZONA_FLL2_CLOCK_OK_EINT1
	},
	[ARIZONA_IRQ_FLL1_CLOCK_OK] = {
		.reg_offset = 4, .mask = ARIZONA_FLL1_CLOCK_OK_EINT1
	},
};

const struct regmap_irq_chip wm5110_irq = {
	.name = "wm5110 IRQ",
	.status_base = ARIZONA_INTERRUPT_STATUS_1,
	.mask_base = ARIZONA_INTERRUPT_STATUS_1_MASK,
	.ack_base = ARIZONA_INTERRUPT_STATUS_1,
	.num_regs = 5,
	.irqs = wm5110_irqs,
	.num_irqs = ARRAY_SIZE(wm5110_irqs),
};
EXPORT_SYMBOL_GPL(wm5110_irq);

static const struct regmap_irq wm5110_revd_irqs[ARIZONA_NUM_IRQ] = {
	[ARIZONA_IRQ_GP4] = { .reg_offset = 0, .mask = ARIZONA_GP4_EINT1 },
	[ARIZONA_IRQ_GP3] = { .reg_offset = 0, .mask = ARIZONA_GP3_EINT1 },
	[ARIZONA_IRQ_GP2] = { .reg_offset = 0, .mask = ARIZONA_GP2_EINT1 },
	[ARIZONA_IRQ_GP1] = { .reg_offset = 0, .mask = ARIZONA_GP1_EINT1 },

	[ARIZONA_IRQ_DSP4_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP4_RAM_RDY_EINT1
	},
	[ARIZONA_IRQ_DSP3_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP3_RAM_RDY_EINT1
	},
	[ARIZONA_IRQ_DSP2_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP2_RAM_RDY_EINT1
	},
	[ARIZONA_IRQ_DSP1_RAM_RDY] = {
		.reg_offset = 1, .mask = ARIZONA_DSP1_RAM_RDY_EINT1
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
	[ARIZONA_IRQ_HP3R_DONE] = {
		.reg_offset = 3, .mask = ARIZONA_HP3R_DONE_EINT1
	},
	[ARIZONA_IRQ_HP3L_DONE] = {
		.reg_offset = 3, .mask = ARIZONA_HP3L_DONE_EINT1
	},
	[ARIZONA_IRQ_HP2R_DONE] = {
		.reg_offset = 3, .mask = ARIZONA_HP2R_DONE_EINT1
	},
	[ARIZONA_IRQ_HP2L_DONE] = {
		.reg_offset = 3, .mask = ARIZONA_HP2L_DONE_EINT1
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
	[ARIZONA_IRQ_HP3R_SC_NEG] = {
		.reg_offset = 5, .mask = ARIZONA_HP3R_SC_NEG_EINT1
	},
	[ARIZONA_IRQ_HP3R_SC_POS] = {
		.reg_offset = 5, .mask = ARIZONA_HP3R_SC_POS_EINT1
	},
	[ARIZONA_IRQ_HP3L_SC_NEG] = {
		.reg_offset = 5, .mask = ARIZONA_HP3L_SC_NEG_EINT1
	},
	[ARIZONA_IRQ_HP3L_SC_POS] = {
		.reg_offset = 5, .mask = ARIZONA_HP3L_SC_POS_EINT1
	},
	[ARIZONA_IRQ_HP2R_SC_NEG] = {
		.reg_offset = 5, .mask = ARIZONA_HP2R_SC_NEG_EINT1
	},
	[ARIZONA_IRQ_HP2R_SC_POS] = {
		.reg_offset = 5, .mask = ARIZONA_HP2R_SC_POS_EINT1
	},
	[ARIZONA_IRQ_HP2L_SC_NEG] = {
		.reg_offset = 5, .mask = ARIZONA_HP2L_SC_NEG_EINT1
	},
	[ARIZONA_IRQ_HP2L_SC_POS] = {
		.reg_offset = 5, .mask = ARIZONA_HP2L_SC_POS_EINT1
	},
	[ARIZONA_IRQ_HP1R_SC_NEG] = {
		.reg_offset = 5, .mask = ARIZONA_HP1R_SC_NEG_EINT1
	},
	[ARIZONA_IRQ_HP1R_SC_POS] = {
		.reg_offset = 5, .mask = ARIZONA_HP1R_SC_POS_EINT1
	},
	[ARIZONA_IRQ_HP1L_SC_NEG] = {
		.reg_offset = 5, .mask = ARIZONA_HP1L_SC_NEG_EINT1
	},
	[ARIZONA_IRQ_HP1L_SC_POS] = {
		.reg_offset = 5, .mask = ARIZONA_HP1L_SC_POS_EINT1
	},
};

const struct regmap_irq_chip wm5110_revd_irq = {
	.name = "wm5110 IRQ",
	.status_base = ARIZONA_INTERRUPT_STATUS_1,
	.mask_base = ARIZONA_INTERRUPT_STATUS_1_MASK,
	.ack_base = ARIZONA_INTERRUPT_STATUS_1,
	.num_regs = 6,
	.irqs = wm5110_revd_irqs,
	.num_irqs = ARRAY_SIZE(wm5110_revd_irqs),
};
EXPORT_SYMBOL_GPL(wm5110_revd_irq);

static const struct reg_default wm5110_reg_default[] = {
	{ 0x00000008, 0x0019 },    /* R8     - Ctrl IF SPI CFG 1 */
	{ 0x00000009, 0x0001 },    /* R9     - Ctrl IF I2C1 CFG 1 */
	{ 0x0000000A, 0x0001 },    /* R10    - Ctrl IF I2C2 CFG 1 */
	{ 0x0000000B, 0x001A },    /* R11    - Ctrl IF I2C1 CFG 2 */
	{ 0x0000000C, 0x001A },    /* R12    - Ctrl IF I2C2 CFG 2 */
	{ 0x00000020, 0x0000 },    /* R32    - Tone Generator 1 */
	{ 0x00000021, 0x1000 },    /* R33    - Tone Generator 2 */
	{ 0x00000022, 0x0000 },    /* R34    - Tone Generator 3 */
	{ 0x00000023, 0x1000 },    /* R35    - Tone Generator 4 */
	{ 0x00000024, 0x0000 },    /* R36    - Tone Generator 5 */
	{ 0x00000030, 0x0000 },    /* R48    - PWM Drive 1 */
	{ 0x00000031, 0x0100 },    /* R49    - PWM Drive 2 */
	{ 0x00000032, 0x0100 },    /* R50    - PWM Drive 3 */
	{ 0x00000040, 0x0000 },    /* R64    - Wake control */
	{ 0x00000041, 0x0000 },    /* R65    - Sequence control */
	{ 0x00000042, 0x0000 },    /* R66    - Spare Triggers */
	{ 0x00000061, 0x01FF },    /* R97    - Sample Rate Sequence Select 1 */
	{ 0x00000062, 0x01FF },    /* R98    - Sample Rate Sequence Select 2 */
	{ 0x00000063, 0x01FF },    /* R99    - Sample Rate Sequence Select 3 */
	{ 0x00000064, 0x01FF },    /* R100   - Sample Rate Sequence Select 4 */
	{ 0x00000066, 0x01FF },    /* R102   - Always On Triggers Sequence Select 1 */
	{ 0x00000067, 0x01FF },    /* R103   - Always On Triggers Sequence Select 2 */
	{ 0x00000068, 0x01FF },    /* R104   - Always On Triggers Sequence Select 3 */
	{ 0x00000069, 0x01FF },    /* R105   - Always On Triggers Sequence Select 4 */
	{ 0x0000006A, 0x01FF },    /* R106   - Always On Triggers Sequence Select 5 */
	{ 0x0000006B, 0x01FF },    /* R107   - Always On Triggers Sequence Select 6 */
	{ 0x00000070, 0x0000 },    /* R112   - Comfort Noise Generator */
	{ 0x00000090, 0x0000 },    /* R144   - Haptics Control 1 */
	{ 0x00000091, 0x7FFF },    /* R145   - Haptics Control 2 */
	{ 0x00000092, 0x0000 },    /* R146   - Haptics phase 1 intensity */
	{ 0x00000093, 0x0000 },    /* R147   - Haptics phase 1 duration */
	{ 0x00000094, 0x0000 },    /* R148   - Haptics phase 2 intensity */
	{ 0x00000095, 0x0000 },    /* R149   - Haptics phase 2 duration */
	{ 0x00000096, 0x0000 },    /* R150   - Haptics phase 3 intensity */
	{ 0x00000097, 0x0000 },    /* R151   - Haptics phase 3 duration */
	{ 0x00000100, 0x0001 },    /* R256   - Clock 32k 1 */
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
	{ 0x00000200, 0x0006 },    /* R512   - Mic Charge Pump 1 */
	{ 0x00000210, 0x0184 },    /* R528   - LDO1 Control 1 */
	{ 0x00000213, 0x03E4 },    /* R531   - LDO2 Control 1 */
	{ 0x00000218, 0x00E6 },    /* R536   - Mic Bias Ctrl 1 */
	{ 0x00000219, 0x00E6 },    /* R537   - Mic Bias Ctrl 2 */
	{ 0x0000021A, 0x00E6 },    /* R538   - Mic Bias Ctrl 3 */
	{ 0x00000293, 0x0000 },    /* R659   - Accessory Detect Mode 1 */
	{ 0x0000029B, 0x0028 },    /* R667   - Headphone Detect 1 */
	{ 0x000002A2, 0x0000 },    /* R674   - Micd clamp control */
	{ 0x000002A3, 0x1102 },    /* R675   - Mic Detect 1 */
	{ 0x000002A4, 0x009F },    /* R676   - Mic Detect 2 */
	{ 0x000002A6, 0x3737 },    /* R678   - Mic Detect Level 1 */
	{ 0x000002A7, 0x2C37 },    /* R679   - Mic Detect Level 2 */
	{ 0x000002A8, 0x1422 },    /* R680   - Mic Detect Level 3 */
	{ 0x000002A9, 0x030A },    /* R681   - Mic Detect Level 4 */
	{ 0x000002C3, 0x0000 },    /* R707   - Mic noise mix control 1 */
	{ 0x000002CB, 0x0000 },    /* R715   - Isolation control */
	{ 0x000002D3, 0x0000 },    /* R723   - Jack detect analogue */
	{ 0x00000300, 0x0000 },    /* R768   - Input Enables */
	{ 0x00000308, 0x0000 },    /* R776   - Input Rate */
	{ 0x00000309, 0x0022 },    /* R777   - Input Volume Ramp */
	{ 0x0000030C, 0x0002 },    /* R780   - HPF Control */
	{ 0x00000310, 0x2080 },    /* R784   - IN1L Control */
	{ 0x00000311, 0x0180 },    /* R785   - ADC Digital Volume 1L */
	{ 0x00000312, 0x0000 },    /* R786   - DMIC1L Control */
	{ 0x00000314, 0x0080 },    /* R788   - IN1R Control */
	{ 0x00000315, 0x0180 },    /* R789   - ADC Digital Volume 1R */
	{ 0x00000316, 0x0000 },    /* R790   - DMIC1R Control */
	{ 0x00000318, 0x2080 },    /* R792   - IN2L Control */
	{ 0x00000319, 0x0180 },    /* R793   - ADC Digital Volume 2L */
	{ 0x0000031A, 0x0000 },    /* R794   - DMIC2L Control */
	{ 0x0000031C, 0x0080 },    /* R796   - IN2R Control */
	{ 0x0000031D, 0x0180 },    /* R797   - ADC Digital Volume 2R */
	{ 0x0000031E, 0x0000 },    /* R798   - DMIC2R Control */
	{ 0x00000320, 0x2080 },    /* R800   - IN3L Control */
	{ 0x00000321, 0x0180 },    /* R801   - ADC Digital Volume 3L */
	{ 0x00000322, 0x0000 },    /* R802   - DMIC3L Control */
	{ 0x00000324, 0x0080 },    /* R804   - IN3R Control */
	{ 0x00000325, 0x0180 },    /* R805   - ADC Digital Volume 3R */
	{ 0x00000326, 0x0000 },    /* R806   - DMIC3R Control */
	{ 0x00000328, 0x2000 },    /* R808   - IN4L Control */
	{ 0x00000329, 0x0180 },    /* R809   - ADC Digital Volume 4L */
	{ 0x0000032A, 0x0000 },    /* R810   - DMIC4L Control */
	{ 0x0000032C, 0x0000 },    /* R812   - IN4R Control */
	{ 0x0000032D, 0x0180 },    /* R813   - ADC Digital Volume 4R */
	{ 0x0000032E, 0x0000 },    /* R814   - DMIC4R Control */
	{ 0x00000400, 0x0000 },    /* R1024  - Output Enables 1 */
	{ 0x00000408, 0x0000 },    /* R1032  - Output Rate 1 */
	{ 0x00000409, 0x0022 },    /* R1033  - Output Volume Ramp */
	{ 0x00000410, 0x0080 },    /* R1040  - Output Path Config 1L */
	{ 0x00000411, 0x0180 },    /* R1041  - DAC Digital Volume 1L */
	{ 0x00000412, 0x0081 },    /* R1042  - DAC Volume Limit 1L */
	{ 0x00000413, 0x0001 },    /* R1043  - Noise Gate Select 1L */
	{ 0x00000414, 0x0080 },    /* R1044  - Output Path Config 1R */
	{ 0x00000415, 0x0180 },    /* R1045  - DAC Digital Volume 1R */
	{ 0x00000416, 0x0081 },    /* R1046  - DAC Volume Limit 1R */
	{ 0x00000417, 0x0002 },    /* R1047  - Noise Gate Select 1R */
	{ 0x00000418, 0x0080 },    /* R1048  - Output Path Config 2L */
	{ 0x00000419, 0x0180 },    /* R1049  - DAC Digital Volume 2L */
	{ 0x0000041A, 0x0081 },    /* R1050  - DAC Volume Limit 2L */
	{ 0x0000041B, 0x0004 },    /* R1051  - Noise Gate Select 2L */
	{ 0x0000041C, 0x0080 },    /* R1052  - Output Path Config 2R */
	{ 0x0000041D, 0x0180 },    /* R1053  - DAC Digital Volume 2R */
	{ 0x0000041E, 0x0081 },    /* R1054  - DAC Volume Limit 2R */
	{ 0x0000041F, 0x0008 },    /* R1055  - Noise Gate Select 2R */
	{ 0x00000420, 0x0080 },    /* R1056  - Output Path Config 3L */
	{ 0x00000421, 0x0180 },    /* R1057  - DAC Digital Volume 3L */
	{ 0x00000422, 0x0081 },    /* R1058  - DAC Volume Limit 3L */
	{ 0x00000423, 0x0010 },    /* R1059  - Noise Gate Select 3L */
	{ 0x00000424, 0x0080 },    /* R1060  - Output Path Config 3R */
	{ 0x00000425, 0x0180 },    /* R1061  - DAC Digital Volume 3R */
	{ 0x00000426, 0x0081 },    /* R1062  - DAC Volume Limit 3R */
	{ 0x00000427, 0x0020 },    /* R1063  - Noise Gate Select 3R */
	{ 0x00000428, 0x0000 },    /* R1064  - Output Path Config 4L */
	{ 0x00000429, 0x0180 },    /* R1065  - DAC Digital Volume 4L */
	{ 0x0000042A, 0x0081 },    /* R1066  - Out Volume 4L */
	{ 0x0000042B, 0x0040 },    /* R1067  - Noise Gate Select 4L */
	{ 0x0000042C, 0x0000 },    /* R1068  - Output Path Config 4R */
	{ 0x0000042D, 0x0180 },    /* R1069  - DAC Digital Volume 4R */
	{ 0x0000042E, 0x0081 },    /* R1070  - Out Volume 4R */
	{ 0x0000042F, 0x0080 },    /* R1071  - Noise Gate Select 4R */
	{ 0x00000430, 0x0000 },    /* R1072  - Output Path Config 5L */
	{ 0x00000431, 0x0180 },    /* R1073  - DAC Digital Volume 5L */
	{ 0x00000432, 0x0081 },    /* R1074  - DAC Volume Limit 5L */
	{ 0x00000433, 0x0100 },    /* R1075  - Noise Gate Select 5L */
	{ 0x00000434, 0x0000 },    /* R1076  - Output Path Config 5R */
	{ 0x00000435, 0x0180 },    /* R1077  - DAC Digital Volume 5R */
	{ 0x00000436, 0x0081 },    /* R1078  - DAC Volume Limit 5R */
	{ 0x00000437, 0x0200 },    /* R1079  - Noise Gate Select 5R */
	{ 0x00000438, 0x0000 },    /* R1080  - Output Path Config 6L */
	{ 0x00000439, 0x0180 },    /* R1081  - DAC Digital Volume 6L */
	{ 0x0000043A, 0x0081 },    /* R1082  - DAC Volume Limit 6L */
	{ 0x0000043B, 0x0400 },    /* R1083  - Noise Gate Select 6L */
	{ 0x0000043C, 0x0000 },    /* R1084  - Output Path Config 6R */
	{ 0x0000043D, 0x0180 },    /* R1085  - DAC Digital Volume 6R */
	{ 0x0000043E, 0x0081 },    /* R1086  - DAC Volume Limit 6R */
	{ 0x0000043F, 0x0800 },    /* R1087  - Noise Gate Select 6R */
	{ 0x00000440, 0x003F },    /* R1088  - DRE Enable */
	{ 0x00000450, 0x0000 },    /* R1104  - DAC AEC Control 1 */
	{ 0x00000458, 0x0000 },    /* R1112  - Noise Gate Control */
	{ 0x00000490, 0x0069 },    /* R1168  - PDM SPK1 CTRL 1 */
	{ 0x00000491, 0x0000 },    /* R1169  - PDM SPK1 CTRL 2 */
	{ 0x00000492, 0x0069 },    /* R1170  - PDM SPK2 CTRL 1 */
	{ 0x00000493, 0x0000 },    /* R1171  - PDM SPK2 CTRL 2 */
	{ 0x000004A0, 0x3480 },    /* R1184  - HP1 Short Circuit Ctrl */
	{ 0x000004A1, 0x3400 },    /* R1185  - HP2 Short Circuit Ctrl */
	{ 0x000004A2, 0x3400 },    /* R1186  - HP3 Short Circuit Ctrl */
	{ 0x00000500, 0x000C },    /* R1280  - AIF1 BCLK Ctrl */
	{ 0x00000501, 0x0008 },    /* R1281  - AIF1 Tx Pin Ctrl */
	{ 0x00000502, 0x0000 },    /* R1282  - AIF1 Rx Pin Ctrl */
	{ 0x00000503, 0x0000 },    /* R1283  - AIF1 Rate Ctrl */
	{ 0x00000504, 0x0000 },    /* R1284  - AIF1 Format */
	{ 0x00000505, 0x0040 },    /* R1285  - AIF1 Tx BCLK Rate */
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
	{ 0x00000545, 0x0040 },    /* R1349  - AIF2 Tx BCLK Rate */
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
	{ 0x00000585, 0x0040 },    /* R1413  - AIF3 Tx BCLK Rate */
	{ 0x00000586, 0x0040 },    /* R1414  - AIF3 Rx BCLK Rate */
	{ 0x00000587, 0x1818 },    /* R1415  - AIF3 Frame Ctrl 1 */
	{ 0x00000588, 0x1818 },    /* R1416  - AIF3 Frame Ctrl 2 */
	{ 0x00000589, 0x0000 },    /* R1417  - AIF3 Frame Ctrl 3 */
	{ 0x0000058A, 0x0001 },    /* R1418  - AIF3 Frame Ctrl 4 */
	{ 0x00000591, 0x0000 },    /* R1425  - AIF3 Frame Ctrl 11 */
	{ 0x00000592, 0x0001 },    /* R1426  - AIF3 Frame Ctrl 12 */
	{ 0x00000599, 0x0000 },    /* R1433  - AIF3 Tx Enables */
	{ 0x0000059A, 0x0000 },    /* R1434  - AIF3 Rx Enables */
	{ 0x000005E3, 0x0004 },    /* R1507  - SLIMbus Framer Ref Gear */
	{ 0x000005E5, 0x0000 },    /* R1509  - SLIMbus Rates 1 */
	{ 0x000005E6, 0x0000 },    /* R1510  - SLIMbus Rates 2 */
	{ 0x000005E7, 0x0000 },    /* R1511  - SLIMbus Rates 3 */
	{ 0x000005E8, 0x0000 },    /* R1512  - SLIMbus Rates 4 */
	{ 0x000005E9, 0x0000 },    /* R1513  - SLIMbus Rates 5 */
	{ 0x000005EA, 0x0000 },    /* R1514  - SLIMbus Rates 6 */
	{ 0x000005EB, 0x0000 },    /* R1515  - SLIMbus Rates 7 */
	{ 0x000005EC, 0x0000 },    /* R1516  - SLIMbus Rates 8 */
	{ 0x000005F5, 0x0000 },    /* R1525  - SLIMbus RX Channel Enable */
	{ 0x000005F6, 0x0000 },    /* R1526  - SLIMbus TX Channel Enable */
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
	{ 0x00000660, 0x0000 },    /* R1632  - MICMIX Input 1 Source */
	{ 0x00000661, 0x0080 },    /* R1633  - MICMIX Input 1 Volume */
	{ 0x00000662, 0x0000 },    /* R1634  - MICMIX Input 2 Source */
	{ 0x00000663, 0x0080 },    /* R1635  - MICMIX Input 2 Volume */
	{ 0x00000664, 0x0000 },    /* R1636  - MICMIX Input 3 Source */
	{ 0x00000665, 0x0080 },    /* R1637  - MICMIX Input 3 Volume */
	{ 0x00000666, 0x0000 },    /* R1638  - MICMIX Input 4 Source */
	{ 0x00000667, 0x0080 },    /* R1639  - MICMIX Input 4 Volume */
	{ 0x00000668, 0x0000 },    /* R1640  - NOISEMIX Input 1 Source */
	{ 0x00000669, 0x0080 },    /* R1641  - NOISEMIX Input 1 Volume */
	{ 0x0000066A, 0x0000 },    /* R1642  - NOISEMIX Input 2 Source */
	{ 0x0000066B, 0x0080 },    /* R1643  - NOISEMIX Input 2 Volume */
	{ 0x0000066C, 0x0000 },    /* R1644  - NOISEMIX Input 3 Source */
	{ 0x0000066D, 0x0080 },    /* R1645  - NOISEMIX Input 3 Volume */
	{ 0x0000066E, 0x0000 },    /* R1646  - NOISEMIX Input 4 Source */
	{ 0x0000066F, 0x0080 },    /* R1647  - NOISEMIX Input 4 Volume */
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
	{ 0x00000690, 0x0000 },    /* R1680  - OUT2LMIX Input 1 Source */
	{ 0x00000691, 0x0080 },    /* R1681  - OUT2LMIX Input 1 Volume */
	{ 0x00000692, 0x0000 },    /* R1682  - OUT2LMIX Input 2 Source */
	{ 0x00000693, 0x0080 },    /* R1683  - OUT2LMIX Input 2 Volume */
	{ 0x00000694, 0x0000 },    /* R1684  - OUT2LMIX Input 3 Source */
	{ 0x00000695, 0x0080 },    /* R1685  - OUT2LMIX Input 3 Volume */
	{ 0x00000696, 0x0000 },    /* R1686  - OUT2LMIX Input 4 Source */
	{ 0x00000697, 0x0080 },    /* R1687  - OUT2LMIX Input 4 Volume */
	{ 0x00000698, 0x0000 },    /* R1688  - OUT2RMIX Input 1 Source */
	{ 0x00000699, 0x0080 },    /* R1689  - OUT2RMIX Input 1 Volume */
	{ 0x0000069A, 0x0000 },    /* R1690  - OUT2RMIX Input 2 Source */
	{ 0x0000069B, 0x0080 },    /* R1691  - OUT2RMIX Input 2 Volume */
	{ 0x0000069C, 0x0000 },    /* R1692  - OUT2RMIX Input 3 Source */
	{ 0x0000069D, 0x0080 },    /* R1693  - OUT2RMIX Input 3 Volume */
	{ 0x0000069E, 0x0000 },    /* R1694  - OUT2RMIX Input 4 Source */
	{ 0x0000069F, 0x0080 },    /* R1695  - OUT2RMIX Input 4 Volume */
	{ 0x000006A0, 0x0000 },    /* R1696  - OUT3LMIX Input 1 Source */
	{ 0x000006A1, 0x0080 },    /* R1697  - OUT3LMIX Input 1 Volume */
	{ 0x000006A2, 0x0000 },    /* R1698  - OUT3LMIX Input 2 Source */
	{ 0x000006A3, 0x0080 },    /* R1699  - OUT3LMIX Input 2 Volume */
	{ 0x000006A4, 0x0000 },    /* R1700  - OUT3LMIX Input 3 Source */
	{ 0x000006A5, 0x0080 },    /* R1701  - OUT3LMIX Input 3 Volume */
	{ 0x000006A6, 0x0000 },    /* R1702  - OUT3LMIX Input 4 Source */
	{ 0x000006A7, 0x0080 },    /* R1703  - OUT3LMIX Input 4 Volume */
	{ 0x000006A8, 0x0000 },    /* R1704  - OUT3RMIX Input 1 Source */
	{ 0x000006A9, 0x0080 },    /* R1705  - OUT3RMIX Input 1 Volume */
	{ 0x000006AA, 0x0000 },    /* R1706  - OUT3RMIX Input 2 Source */
	{ 0x000006AB, 0x0080 },    /* R1707  - OUT3RMIX Input 2 Volume */
	{ 0x000006AC, 0x0000 },    /* R1708  - OUT3RMIX Input 3 Source */
	{ 0x000006AD, 0x0080 },    /* R1709  - OUT3RMIX Input 3 Volume */
	{ 0x000006AE, 0x0000 },    /* R1710  - OUT3RMIX Input 4 Source */
	{ 0x000006AF, 0x0080 },    /* R1711  - OUT3RMIX Input 4 Volume */
	{ 0x000006B0, 0x0000 },    /* R1712  - OUT4LMIX Input 1 Source */
	{ 0x000006B1, 0x0080 },    /* R1713  - OUT4LMIX Input 1 Volume */
	{ 0x000006B2, 0x0000 },    /* R1714  - OUT4LMIX Input 2 Source */
	{ 0x000006B3, 0x0080 },    /* R1715  - OUT4LMIX Input 2 Volume */
	{ 0x000006B4, 0x0000 },    /* R1716  - OUT4LMIX Input 3 Source */
	{ 0x000006B5, 0x0080 },    /* R1717  - OUT4LMIX Input 3 Volume */
	{ 0x000006B6, 0x0000 },    /* R1718  - OUT4LMIX Input 4 Source */
	{ 0x000006B7, 0x0080 },    /* R1719  - OUT4LMIX Input 4 Volume */
	{ 0x000006B8, 0x0000 },    /* R1720  - OUT4RMIX Input 1 Source */
	{ 0x000006B9, 0x0080 },    /* R1721  - OUT4RMIX Input 1 Volume */
	{ 0x000006BA, 0x0000 },    /* R1722  - OUT4RMIX Input 2 Source */
	{ 0x000006BB, 0x0080 },    /* R1723  - OUT4RMIX Input 2 Volume */
	{ 0x000006BC, 0x0000 },    /* R1724  - OUT4RMIX Input 3 Source */
	{ 0x000006BD, 0x0080 },    /* R1725  - OUT4RMIX Input 3 Volume */
	{ 0x000006BE, 0x0000 },    /* R1726  - OUT4RMIX Input 4 Source */
	{ 0x000006BF, 0x0080 },    /* R1727  - OUT4RMIX Input 4 Volume */
	{ 0x000006C0, 0x0000 },    /* R1728  - OUT5LMIX Input 1 Source */
	{ 0x000006C1, 0x0080 },    /* R1729  - OUT5LMIX Input 1 Volume */
	{ 0x000006C2, 0x0000 },    /* R1730  - OUT5LMIX Input 2 Source */
	{ 0x000006C3, 0x0080 },    /* R1731  - OUT5LMIX Input 2 Volume */
	{ 0x000006C4, 0x0000 },    /* R1732  - OUT5LMIX Input 3 Source */
	{ 0x000006C5, 0x0080 },    /* R1733  - OUT5LMIX Input 3 Volume */
	{ 0x000006C6, 0x0000 },    /* R1734  - OUT5LMIX Input 4 Source */
	{ 0x000006C7, 0x0080 },    /* R1735  - OUT5LMIX Input 4 Volume */
	{ 0x000006C8, 0x0000 },    /* R1736  - OUT5RMIX Input 1 Source */
	{ 0x000006C9, 0x0080 },    /* R1737  - OUT5RMIX Input 1 Volume */
	{ 0x000006CA, 0x0000 },    /* R1738  - OUT5RMIX Input 2 Source */
	{ 0x000006CB, 0x0080 },    /* R1739  - OUT5RMIX Input 2 Volume */
	{ 0x000006CC, 0x0000 },    /* R1740  - OUT5RMIX Input 3 Source */
	{ 0x000006CD, 0x0080 },    /* R1741  - OUT5RMIX Input 3 Volume */
	{ 0x000006CE, 0x0000 },    /* R1742  - OUT5RMIX Input 4 Source */
	{ 0x000006CF, 0x0080 },    /* R1743  - OUT5RMIX Input 4 Volume */
	{ 0x000006D0, 0x0000 },    /* R1744  - OUT6LMIX Input 1 Source */
	{ 0x000006D1, 0x0080 },    /* R1745  - OUT6LMIX Input 1 Volume */
	{ 0x000006D2, 0x0000 },    /* R1746  - OUT6LMIX Input 2 Source */
	{ 0x000006D3, 0x0080 },    /* R1747  - OUT6LMIX Input 2 Volume */
	{ 0x000006D4, 0x0000 },    /* R1748  - OUT6LMIX Input 3 Source */
	{ 0x000006D5, 0x0080 },    /* R1749  - OUT6LMIX Input 3 Volume */
	{ 0x000006D6, 0x0000 },    /* R1750  - OUT6LMIX Input 4 Source */
	{ 0x000006D7, 0x0080 },    /* R1751  - OUT6LMIX Input 4 Volume */
	{ 0x000006D8, 0x0000 },    /* R1752  - OUT6RMIX Input 1 Source */
	{ 0x000006D9, 0x0080 },    /* R1753  - OUT6RMIX Input 1 Volume */
	{ 0x000006DA, 0x0000 },    /* R1754  - OUT6RMIX Input 2 Source */
	{ 0x000006DB, 0x0080 },    /* R1755  - OUT6RMIX Input 2 Volume */
	{ 0x000006DC, 0x0000 },    /* R1756  - OUT6RMIX Input 3 Source */
	{ 0x000006DD, 0x0080 },    /* R1757  - OUT6RMIX Input 3 Volume */
	{ 0x000006DE, 0x0000 },    /* R1758  - OUT6RMIX Input 4 Source */
	{ 0x000006DF, 0x0080 },    /* R1759  - OUT6RMIX Input 4 Volume */
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
	{ 0x000007C0, 0x0000 },    /* R1984  - SLIMTX1MIX Input 1 Source */
	{ 0x000007C1, 0x0080 },    /* R1985  - SLIMTX1MIX Input 1 Volume */
	{ 0x000007C2, 0x0000 },    /* R1986  - SLIMTX1MIX Input 2 Source */
	{ 0x000007C3, 0x0080 },    /* R1987  - SLIMTX1MIX Input 2 Volume */
	{ 0x000007C4, 0x0000 },    /* R1988  - SLIMTX1MIX Input 3 Source */
	{ 0x000007C5, 0x0080 },    /* R1989  - SLIMTX1MIX Input 3 Volume */
	{ 0x000007C6, 0x0000 },    /* R1990  - SLIMTX1MIX Input 4 Source */
	{ 0x000007C7, 0x0080 },    /* R1991  - SLIMTX1MIX Input 4 Volume */
	{ 0x000007C8, 0x0000 },    /* R1992  - SLIMTX2MIX Input 1 Source */
	{ 0x000007C9, 0x0080 },    /* R1993  - SLIMTX2MIX Input 1 Volume */
	{ 0x000007CA, 0x0000 },    /* R1994  - SLIMTX2MIX Input 2 Source */
	{ 0x000007CB, 0x0080 },    /* R1995  - SLIMTX2MIX Input 2 Volume */
	{ 0x000007CC, 0x0000 },    /* R1996  - SLIMTX2MIX Input 3 Source */
	{ 0x000007CD, 0x0080 },    /* R1997  - SLIMTX2MIX Input 3 Volume */
	{ 0x000007CE, 0x0000 },    /* R1998  - SLIMTX2MIX Input 4 Source */
	{ 0x000007CF, 0x0080 },    /* R1999  - SLIMTX2MIX Input 4 Volume */
	{ 0x000007D0, 0x0000 },    /* R2000  - SLIMTX3MIX Input 1 Source */
	{ 0x000007D1, 0x0080 },    /* R2001  - SLIMTX3MIX Input 1 Volume */
	{ 0x000007D2, 0x0000 },    /* R2002  - SLIMTX3MIX Input 2 Source */
	{ 0x000007D3, 0x0080 },    /* R2003  - SLIMTX3MIX Input 2 Volume */
	{ 0x000007D4, 0x0000 },    /* R2004  - SLIMTX3MIX Input 3 Source */
	{ 0x000007D5, 0x0080 },    /* R2005  - SLIMTX3MIX Input 3 Volume */
	{ 0x000007D6, 0x0000 },    /* R2006  - SLIMTX3MIX Input 4 Source */
	{ 0x000007D7, 0x0080 },    /* R2007  - SLIMTX3MIX Input 4 Volume */
	{ 0x000007D8, 0x0000 },    /* R2008  - SLIMTX4MIX Input 1 Source */
	{ 0x000007D9, 0x0080 },    /* R2009  - SLIMTX4MIX Input 1 Volume */
	{ 0x000007DA, 0x0000 },    /* R2010  - SLIMTX4MIX Input 2 Source */
	{ 0x000007DB, 0x0080 },    /* R2011  - SLIMTX4MIX Input 2 Volume */
	{ 0x000007DC, 0x0000 },    /* R2012  - SLIMTX4MIX Input 3 Source */
	{ 0x000007DD, 0x0080 },    /* R2013  - SLIMTX4MIX Input 3 Volume */
	{ 0x000007DE, 0x0000 },    /* R2014  - SLIMTX4MIX Input 4 Source */
	{ 0x000007DF, 0x0080 },    /* R2015  - SLIMTX4MIX Input 4 Volume */
	{ 0x000007E0, 0x0000 },    /* R2016  - SLIMTX5MIX Input 1 Source */
	{ 0x000007E1, 0x0080 },    /* R2017  - SLIMTX5MIX Input 1 Volume */
	{ 0x000007E2, 0x0000 },    /* R2018  - SLIMTX5MIX Input 2 Source */
	{ 0x000007E3, 0x0080 },    /* R2019  - SLIMTX5MIX Input 2 Volume */
	{ 0x000007E4, 0x0000 },    /* R2020  - SLIMTX5MIX Input 3 Source */
	{ 0x000007E5, 0x0080 },    /* R2021  - SLIMTX5MIX Input 3 Volume */
	{ 0x000007E6, 0x0000 },    /* R2022  - SLIMTX5MIX Input 4 Source */
	{ 0x000007E7, 0x0080 },    /* R2023  - SLIMTX5MIX Input 4 Volume */
	{ 0x000007E8, 0x0000 },    /* R2024  - SLIMTX6MIX Input 1 Source */
	{ 0x000007E9, 0x0080 },    /* R2025  - SLIMTX6MIX Input 1 Volume */
	{ 0x000007EA, 0x0000 },    /* R2026  - SLIMTX6MIX Input 2 Source */
	{ 0x000007EB, 0x0080 },    /* R2027  - SLIMTX6MIX Input 2 Volume */
	{ 0x000007EC, 0x0000 },    /* R2028  - SLIMTX6MIX Input 3 Source */
	{ 0x000007ED, 0x0080 },    /* R2029  - SLIMTX6MIX Input 3 Volume */
	{ 0x000007EE, 0x0000 },    /* R2030  - SLIMTX6MIX Input 4 Source */
	{ 0x000007EF, 0x0080 },    /* R2031  - SLIMTX6MIX Input 4 Volume */
	{ 0x000007F0, 0x0000 },    /* R2032  - SLIMTX7MIX Input 1 Source */
	{ 0x000007F1, 0x0080 },    /* R2033  - SLIMTX7MIX Input 1 Volume */
	{ 0x000007F2, 0x0000 },    /* R2034  - SLIMTX7MIX Input 2 Source */
	{ 0x000007F3, 0x0080 },    /* R2035  - SLIMTX7MIX Input 2 Volume */
	{ 0x000007F4, 0x0000 },    /* R2036  - SLIMTX7MIX Input 3 Source */
	{ 0x000007F5, 0x0080 },    /* R2037  - SLIMTX7MIX Input 3 Volume */
	{ 0x000007F6, 0x0000 },    /* R2038  - SLIMTX7MIX Input 4 Source */
	{ 0x000007F7, 0x0080 },    /* R2039  - SLIMTX7MIX Input 4 Volume */
	{ 0x000007F8, 0x0000 },    /* R2040  - SLIMTX8MIX Input 1 Source */
	{ 0x000007F9, 0x0080 },    /* R2041  - SLIMTX8MIX Input 1 Volume */
	{ 0x000007FA, 0x0000 },    /* R2042  - SLIMTX8MIX Input 2 Source */
	{ 0x000007FB, 0x0080 },    /* R2043  - SLIMTX8MIX Input 2 Volume */
	{ 0x000007FC, 0x0000 },    /* R2044  - SLIMTX8MIX Input 3 Source */
	{ 0x000007FD, 0x0080 },    /* R2045  - SLIMTX8MIX Input 3 Volume */
	{ 0x000007FE, 0x0000 },    /* R2046  - SLIMTX8MIX Input 4 Source */
	{ 0x000007FF, 0x0080 },    /* R2047  - SLIMTX8MIX Input 4 Volume */
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
	{ 0x00000890, 0x0000 },    /* R2192  - EQ3MIX Input 1 Source */
	{ 0x00000891, 0x0080 },    /* R2193  - EQ3MIX Input 1 Volume */
	{ 0x00000892, 0x0000 },    /* R2194  - EQ3MIX Input 2 Source */
	{ 0x00000893, 0x0080 },    /* R2195  - EQ3MIX Input 2 Volume */
	{ 0x00000894, 0x0000 },    /* R2196  - EQ3MIX Input 3 Source */
	{ 0x00000895, 0x0080 },    /* R2197  - EQ3MIX Input 3 Volume */
	{ 0x00000896, 0x0000 },    /* R2198  - EQ3MIX Input 4 Source */
	{ 0x00000897, 0x0080 },    /* R2199  - EQ3MIX Input 4 Volume */
	{ 0x00000898, 0x0000 },    /* R2200  - EQ4MIX Input 1 Source */
	{ 0x00000899, 0x0080 },    /* R2201  - EQ4MIX Input 1 Volume */
	{ 0x0000089A, 0x0000 },    /* R2202  - EQ4MIX Input 2 Source */
	{ 0x0000089B, 0x0080 },    /* R2203  - EQ4MIX Input 2 Volume */
	{ 0x0000089C, 0x0000 },    /* R2204  - EQ4MIX Input 3 Source */
	{ 0x0000089D, 0x0080 },    /* R2205  - EQ4MIX Input 3 Volume */
	{ 0x0000089E, 0x0000 },    /* R2206  - EQ4MIX Input 4 Source */
	{ 0x0000089F, 0x0080 },    /* R2207  - EQ4MIX Input 4 Volume */
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
	{ 0x00000940, 0x0000 },    /* R2368  - DSP1LMIX Input 1 Source */
	{ 0x00000941, 0x0080 },    /* R2369  - DSP1LMIX Input 1 Volume */
	{ 0x00000942, 0x0000 },    /* R2370  - DSP1LMIX Input 2 Source */
	{ 0x00000943, 0x0080 },    /* R2371  - DSP1LMIX Input 2 Volume */
	{ 0x00000944, 0x0000 },    /* R2372  - DSP1LMIX Input 3 Source */
	{ 0x00000945, 0x0080 },    /* R2373  - DSP1LMIX Input 3 Volume */
	{ 0x00000946, 0x0000 },    /* R2374  - DSP1LMIX Input 4 Source */
	{ 0x00000947, 0x0080 },    /* R2375  - DSP1LMIX Input 4 Volume */
	{ 0x00000948, 0x0000 },    /* R2376  - DSP1RMIX Input 1 Source */
	{ 0x00000949, 0x0080 },    /* R2377  - DSP1RMIX Input 1 Volume */
	{ 0x0000094A, 0x0000 },    /* R2378  - DSP1RMIX Input 2 Source */
	{ 0x0000094B, 0x0080 },    /* R2379  - DSP1RMIX Input 2 Volume */
	{ 0x0000094C, 0x0000 },    /* R2380  - DSP1RMIX Input 3 Source */
	{ 0x0000094D, 0x0080 },    /* R2381  - DSP1RMIX Input 3 Volume */
	{ 0x0000094E, 0x0000 },    /* R2382  - DSP1RMIX Input 4 Source */
	{ 0x0000094F, 0x0080 },    /* R2383  - DSP1RMIX Input 4 Volume */
	{ 0x00000950, 0x0000 },    /* R2384  - DSP1AUX1MIX Input 1 Source */
	{ 0x00000958, 0x0000 },    /* R2392  - DSP1AUX2MIX Input 1 Source */
	{ 0x00000960, 0x0000 },    /* R2400  - DSP1AUX3MIX Input 1 Source */
	{ 0x00000968, 0x0000 },    /* R2408  - DSP1AUX4MIX Input 1 Source */
	{ 0x00000970, 0x0000 },    /* R2416  - DSP1AUX5MIX Input 1 Source */
	{ 0x00000978, 0x0000 },    /* R2424  - DSP1AUX6MIX Input 1 Source */
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
	{ 0x00000A00, 0x0000 },    /* R2560  - DSP4LMIX Input 1 Source */
	{ 0x00000A01, 0x0080 },    /* R2561  - DSP4LMIX Input 1 Volume */
	{ 0x00000A02, 0x0000 },    /* R2562  - DSP4LMIX Input 2 Source */
	{ 0x00000A03, 0x0080 },    /* R2563  - DSP4LMIX Input 2 Volume */
	{ 0x00000A04, 0x0000 },    /* R2564  - DSP4LMIX Input 3 Source */
	{ 0x00000A05, 0x0080 },    /* R2565  - DSP4LMIX Input 3 Volume */
	{ 0x00000A06, 0x0000 },    /* R2566  - DSP4LMIX Input 4 Source */
	{ 0x00000A07, 0x0080 },    /* R2567  - DSP4LMIX Input 4 Volume */
	{ 0x00000A08, 0x0000 },    /* R2568  - DSP4RMIX Input 1 Source */
	{ 0x00000A09, 0x0080 },    /* R2569  - DSP4RMIX Input 1 Volume */
	{ 0x00000A0A, 0x0000 },    /* R2570  - DSP4RMIX Input 2 Source */
	{ 0x00000A0B, 0x0080 },    /* R2571  - DSP4RMIX Input 2 Volume */
	{ 0x00000A0C, 0x0000 },    /* R2572  - DSP4RMIX Input 3 Source */
	{ 0x00000A0D, 0x0080 },    /* R2573  - DSP4RMIX Input 3 Volume */
	{ 0x00000A0E, 0x0000 },    /* R2574  - DSP4RMIX Input 4 Source */
	{ 0x00000A0F, 0x0080 },    /* R2575  - DSP4RMIX Input 4 Volume */
	{ 0x00000A10, 0x0000 },    /* R2576  - DSP4AUX1MIX Input 1 Source */
	{ 0x00000A18, 0x0000 },    /* R2584  - DSP4AUX2MIX Input 1 Source */
	{ 0x00000A20, 0x0000 },    /* R2592  - DSP4AUX3MIX Input 1 Source */
	{ 0x00000A28, 0x0000 },    /* R2600  - DSP4AUX4MIX Input 1 Source */
	{ 0x00000A30, 0x0000 },    /* R2608  - DSP4AUX5MIX Input 1 Source */
	{ 0x00000A38, 0x0000 },    /* R2616  - DSP4AUX6MIX Input 1 Source */
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
	{ 0x00000C02, 0xA101 },    /* R3074  - GPIO3 CTRL */
	{ 0x00000C03, 0xA101 },    /* R3075  - GPIO4 CTRL */
	{ 0x00000C04, 0xA101 },    /* R3076  - GPIO5 CTRL */
	{ 0x00000C0F, 0x0400 },    /* R3087  - IRQ CTRL 1 */
	{ 0x00000C10, 0x1000 },    /* R3088  - GPIO Debounce Config */
	{ 0x00000C18, 0x0000 },    /* R3096  - GP Switch 1 */
	{ 0x00000C20, 0x8002 },    /* R3104  - Misc Pad Ctrl 1 */
	{ 0x00000C21, 0x0001 },    /* R3105  - Misc Pad Ctrl 2 */
	{ 0x00000C22, 0x0000 },    /* R3106  - Misc Pad Ctrl 3 */
	{ 0x00000C23, 0x0000 },    /* R3107  - Misc Pad Ctrl 4 */
	{ 0x00000C24, 0x0000 },    /* R3108  - Misc Pad Ctrl 5 */
	{ 0x00000C25, 0x0000 },    /* R3109  - Misc Pad Ctrl 6 */
	{ 0x00000C30, 0x0404 },    /* R3120  - Misc Pad Ctrl 7 */
	{ 0x00000C31, 0x0004 },    /* R3121  - Misc Pad Ctrl 8 */
	{ 0x00000C32, 0x0404 },    /* R3122  - Misc Pad Ctrl 9 */
	{ 0x00000C33, 0x0404 },    /* R3123  - Misc Pad Ctrl 10 */
	{ 0x00000C34, 0x0404 },    /* R3124  - Misc Pad Ctrl 11 */
	{ 0x00000C35, 0x0404 },    /* R3125  - Misc Pad Ctrl 12 */
	{ 0x00000C36, 0x0404 },    /* R3126  - Misc Pad Ctrl 13 */
	{ 0x00000C37, 0x0404 },    /* R3127  - Misc Pad Ctrl 14 */
	{ 0x00000C38, 0x0004 },    /* R3128  - Misc Pad Ctrl 15 */
	{ 0x00000C39, 0x0404 },    /* R3129  - Misc Pad Ctrl 16 */
	{ 0x00000C3A, 0x0404 },    /* R3130  - Misc Pad Ctrl 17 */
	{ 0x00000C3B, 0x0404 },    /* R3131  - Misc Pad Ctrl 18 */
	{ 0x00000D08, 0xFFFF },    /* R3336  - Interrupt Status 1 Mask */
	{ 0x00000D09, 0xFFFF },    /* R3337  - Interrupt Status 2 Mask */
	{ 0x00000D0A, 0xFFFF },    /* R3338  - Interrupt Status 3 Mask */
	{ 0x00000D0B, 0xFFFF },    /* R3339  - Interrupt Status 4 Mask */
	{ 0x00000D0C, 0xFEFF },    /* R3340  - Interrupt Status 5 Mask */
	{ 0x00000D0D, 0xFFFF },    /* R3341  - Interrupt Status 6 Mask */
	{ 0x00000D0F, 0x0000 },    /* R3343  - Interrupt Control */
	{ 0x00000D18, 0xFFFF },    /* R3352  - IRQ2 Status 1 Mask */
	{ 0x00000D19, 0xFFFF },    /* R3353  - IRQ2 Status 2 Mask */
	{ 0x00000D1A, 0xFFFF },    /* R3354  - IRQ2 Status 3 Mask */
	{ 0x00000D1B, 0xFFFF },    /* R3355  - IRQ2 Status 4 Mask */
	{ 0x00000D1C, 0xFFFF },    /* R3356  - IRQ2 Status 5 Mask */
	{ 0x00000D1D, 0xFFFF },    /* R3357  - IRQ2 Status 6 Mask */
	{ 0x00000D1F, 0x0000 },    /* R3359  - IRQ2 Control */
	{ 0x00000D53, 0xFFFF },    /* R3411  - AOD IRQ Mask IRQ1 */
	{ 0x00000D54, 0xFFFF },    /* R3412  - AOD IRQ Mask IRQ2 */
	{ 0x00000D56, 0x0000 },    /* R3414  - Jack detect debounce */
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
	{ 0x00000E3C, 0x6318 },    /* R3644  - EQ3_1 */
	{ 0x00000E3D, 0x6300 },    /* R3645  - EQ3_2 */
	{ 0x00000E3E, 0x0FC8 },    /* R3646  - EQ3_3 */
	{ 0x00000E3F, 0x03FE },    /* R3647  - EQ3_4 */
	{ 0x00000E40, 0x00E0 },    /* R3648  - EQ3_5 */
	{ 0x00000E41, 0x1EC4 },    /* R3649  - EQ3_6 */
	{ 0x00000E42, 0xF136 },    /* R3650  - EQ3_7 */
	{ 0x00000E43, 0x0409 },    /* R3651  - EQ3_8 */
	{ 0x00000E44, 0x04CC },    /* R3652  - EQ3_9 */
	{ 0x00000E45, 0x1C9B },    /* R3653  - EQ3_10 */
	{ 0x00000E46, 0xF337 },    /* R3654  - EQ3_11 */
	{ 0x00000E47, 0x040B },    /* R3655  - EQ3_12 */
	{ 0x00000E48, 0x0CBB },    /* R3656  - EQ3_13 */
	{ 0x00000E49, 0x16F8 },    /* R3657  - EQ3_14 */
	{ 0x00000E4A, 0xF7D9 },    /* R3658  - EQ3_15 */
	{ 0x00000E4B, 0x040A },    /* R3659  - EQ3_16 */
	{ 0x00000E4C, 0x1F14 },    /* R3660  - EQ3_17 */
	{ 0x00000E4D, 0x058C },    /* R3661  - EQ3_18 */
	{ 0x00000E4E, 0x0563 },    /* R3662  - EQ3_19 */
	{ 0x00000E4F, 0x4000 },    /* R3663  - EQ3_20 */
	{ 0x00000E50, 0x0B75 },    /* R3664  - EQ3_21 */
	{ 0x00000E52, 0x6318 },    /* R3666  - EQ4_1 */
	{ 0x00000E53, 0x6300 },    /* R3667  - EQ4_2 */
	{ 0x00000E54, 0x0FC8 },    /* R3668  - EQ4_3 */
	{ 0x00000E55, 0x03FE },    /* R3669  - EQ4_4 */
	{ 0x00000E56, 0x00E0 },    /* R3670  - EQ4_5 */
	{ 0x00000E57, 0x1EC4 },    /* R3671  - EQ4_6 */
	{ 0x00000E58, 0xF136 },    /* R3672  - EQ4_7 */
	{ 0x00000E59, 0x0409 },    /* R3673  - EQ4_8 */
	{ 0x00000E5A, 0x04CC },    /* R3674  - EQ4_9 */
	{ 0x00000E5B, 0x1C9B },    /* R3675  - EQ4_10 */
	{ 0x00000E5C, 0xF337 },    /* R3676  - EQ4_11 */
	{ 0x00000E5D, 0x040B },    /* R3677  - EQ4_12 */
	{ 0x00000E5E, 0x0CBB },    /* R3678  - EQ4_13 */
	{ 0x00000E5F, 0x16F8 },    /* R3679  - EQ4_14 */
	{ 0x00000E60, 0xF7D9 },    /* R3680  - EQ4_15 */
	{ 0x00000E61, 0x040A },    /* R3681  - EQ4_16 */
	{ 0x00000E62, 0x1F14 },    /* R3682  - EQ4_17 */
	{ 0x00000E63, 0x058C },    /* R3683  - EQ4_18 */
	{ 0x00000E64, 0x0563 },    /* R3684  - EQ4_19 */
	{ 0x00000E65, 0x4000 },    /* R3685  - EQ4_20 */
	{ 0x00000E66, 0x0B75 },    /* R3686  - EQ4_21 */
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
	{ 0x00000F00, 0x0000 },    /* R3840  - Clock Control */
	{ 0x00000F01, 0x0000 },    /* R3841  - ANC_SRC */
	{ 0x00000F08, 0x001c },    /* R3848  - ANC Coefficient */
	{ 0x00000F09, 0x0000 },    /* R3849  - ANC Coefficient */
	{ 0x00000F0A, 0x0000 },    /* R3850  - ANC Coefficient */
	{ 0x00000F0B, 0x0000 },    /* R3851  - ANC Coefficient */
	{ 0x00000F0C, 0x0000 },    /* R3852  - ANC Coefficient */
	{ 0x00000F0D, 0x0000 },    /* R3853  - ANC Coefficient */
	{ 0x00000F0E, 0x0000 },    /* R3854  - ANC Coefficient */
	{ 0x00000F0F, 0x0000 },    /* R3855  - ANC Coefficient */
	{ 0x00000F10, 0x0001 },    /* R3856  - ANC Coefficient */
	{ 0x00000F11, 0x0000 },    /* R3857  - ANC Coefficient */
	{ 0x00000F12, 0x0000 },    /* R3858  - ANC Coefficient */
	{ 0x00000F15, 0x0000 },    /* R3861  - FCL Filter Control */
	{ 0x00000F17, 0x0004 },    /* R3863  - FCL ADC Reformatter Control */
	{ 0x00000F18, 0x0004 },    /* R3864  - ANC Coefficient */
	{ 0x00000F19, 0x0002 },    /* R3865  - ANC Coefficient */
	{ 0x00000F1A, 0x0000 },    /* R3866  - ANC Coefficient */
	{ 0x00000F1B, 0x0010 },    /* R3867  - ANC Coefficient */
	{ 0x00000F1C, 0x0000 },    /* R3868  - ANC Coefficient */
	{ 0x00000F1D, 0x0000 },    /* R3869  - ANC Coefficient */
	{ 0x00000F1E, 0x0000 },    /* R3870  - ANC Coefficient */
	{ 0x00000F1F, 0x0000 },    /* R3871  - ANC Coefficient */
	{ 0x00000F20, 0x0000 },    /* R3872  - ANC Coefficient */
	{ 0x00000F21, 0x0000 },    /* R3873  - ANC Coefficient */
	{ 0x00000F22, 0x0000 },    /* R3874  - ANC Coefficient */
	{ 0x00000F23, 0x0000 },    /* R3875  - ANC Coefficient */
	{ 0x00000F24, 0x0000 },    /* R3876  - ANC Coefficient */
	{ 0x00000F25, 0x0000 },    /* R3877  - ANC Coefficient */
	{ 0x00000F26, 0x0000 },    /* R3878  - ANC Coefficient */
	{ 0x00000F27, 0x0000 },    /* R3879  - ANC Coefficient */
	{ 0x00000F28, 0x0000 },    /* R3880  - ANC Coefficient */
	{ 0x00000F29, 0x0000 },    /* R3881  - ANC Coefficient */
	{ 0x00000F2A, 0x0000 },    /* R3882  - ANC Coefficient */
	{ 0x00000F2B, 0x0000 },    /* R3883  - ANC Coefficient */
	{ 0x00000F2C, 0x0000 },    /* R3884  - ANC Coefficient */
	{ 0x00000F2D, 0x0000 },    /* R3885  - ANC Coefficient */
	{ 0x00000F2E, 0x0000 },    /* R3886  - ANC Coefficient */
	{ 0x00000F2F, 0x0000 },    /* R3887  - ANC Coefficient */
	{ 0x00000F30, 0x0000 },    /* R3888  - ANC Coefficient */
	{ 0x00000F31, 0x0000 },    /* R3889  - ANC Coefficient */
	{ 0x00000F32, 0x0000 },    /* R3890  - ANC Coefficient */
	{ 0x00000F33, 0x0000 },    /* R3891  - ANC Coefficient */
	{ 0x00000F34, 0x0000 },    /* R3892  - ANC Coefficient */
	{ 0x00000F35, 0x0000 },    /* R3893  - ANC Coefficient */
	{ 0x00000F36, 0x0000 },    /* R3894  - ANC Coefficient */
	{ 0x00000F37, 0x0000 },    /* R3895  - ANC Coefficient */
	{ 0x00000F38, 0x0000 },    /* R3896  - ANC Coefficient */
	{ 0x00000F39, 0x0000 },    /* R3897  - ANC Coefficient */
	{ 0x00000F3A, 0x0000 },    /* R3898  - ANC Coefficient */
	{ 0x00000F3B, 0x0000 },    /* R3899  - ANC Coefficient */
	{ 0x00000F3C, 0x0000 },    /* R3900  - ANC Coefficient */
	{ 0x00000F3D, 0x0000 },    /* R3901  - ANC Coefficient */
	{ 0x00000F3E, 0x0000 },    /* R3902  - ANC Coefficient */
	{ 0x00000F3F, 0x0000 },    /* R3903  - ANC Coefficient */
	{ 0x00000F40, 0x0000 },    /* R3904  - ANC Coefficient */
	{ 0x00000F41, 0x0000 },    /* R3905  - ANC Coefficient */
	{ 0x00000F42, 0x0000 },    /* R3906  - ANC Coefficient */
	{ 0x00000F43, 0x0000 },    /* R3907  - ANC Coefficient */
	{ 0x00000F44, 0x0000 },    /* R3908  - ANC Coefficient */
	{ 0x00000F45, 0x0000 },    /* R3909  - ANC Coefficient */
	{ 0x00000F46, 0x0000 },    /* R3910  - ANC Coefficient */
	{ 0x00000F47, 0x0000 },    /* R3911  - ANC Coefficient */
	{ 0x00000F48, 0x0000 },    /* R3912  - ANC Coefficient */
	{ 0x00000F49, 0x0000 },    /* R3913  - ANC Coefficient */
	{ 0x00000F4A, 0x0000 },    /* R3914  - ANC Coefficient */
	{ 0x00000F4B, 0x0000 },    /* R3915  - ANC Coefficient */
	{ 0x00000F4C, 0x0000 },    /* R3916  - ANC Coefficient */
	{ 0x00000F4D, 0x0000 },    /* R3917  - ANC Coefficient */
	{ 0x00000F4E, 0x0000 },    /* R3918  - ANC Coefficient */
	{ 0x00000F4F, 0x0000 },    /* R3919  - ANC Coefficient */
	{ 0x00000F50, 0x0000 },    /* R3920  - ANC Coefficient */
	{ 0x00000F51, 0x0000 },    /* R3921  - ANC Coefficient */
	{ 0x00000F52, 0x0000 },    /* R3922  - ANC Coefficient */
	{ 0x00000F53, 0x0000 },    /* R3923  - ANC Coefficient */
	{ 0x00000F54, 0x0000 },    /* R3924  - ANC Coefficient */
	{ 0x00000F55, 0x0000 },    /* R3925  - ANC Coefficient */
	{ 0x00000F56, 0x0000 },    /* R3926  - ANC Coefficient */
	{ 0x00000F57, 0x0000 },    /* R3927  - ANC Coefficient */
	{ 0x00000F58, 0x0000 },    /* R3928  - ANC Coefficient */
	{ 0x00000F59, 0x0000 },    /* R3929  - ANC Coefficient */
	{ 0x00000F5A, 0x0000 },    /* R3930  - ANC Coefficient */
	{ 0x00000F5B, 0x0000 },    /* R3931  - ANC Coefficient */
	{ 0x00000F5C, 0x0000 },    /* R3932  - ANC Coefficient */
	{ 0x00000F5D, 0x0000 },    /* R3933  - ANC Coefficient */
	{ 0x00000F5E, 0x0000 },    /* R3934  - ANC Coefficient */
	{ 0x00000F5F, 0x0000 },    /* R3935  - ANC Coefficient */
	{ 0x00000F60, 0x0000 },    /* R3936  - ANC Coefficient */
	{ 0x00000F61, 0x0000 },    /* R3937  - ANC Coefficient */
	{ 0x00000F62, 0x0000 },    /* R3938  - ANC Coefficient */
	{ 0x00000F63, 0x0000 },    /* R3939  - ANC Coefficient */
	{ 0x00000F64, 0x0000 },    /* R3940  - ANC Coefficient */
	{ 0x00000F65, 0x0000 },    /* R3941  - ANC Coefficient */
	{ 0x00000F66, 0x0000 },    /* R3942  - ANC Coefficient */
	{ 0x00000F67, 0x0000 },    /* R3943  - ANC Coefficient */
	{ 0x00000F68, 0x0000 },    /* R3944  - ANC Coefficient */
	{ 0x00000F69, 0x0000 },    /* R3945  - ANC Coefficient */
	{ 0x00000F70, 0x0000 },    /* R3952  - FCR Filter Control */
	{ 0x00000F72, 0x0004 },    /* R3954  - FCR ADC Reformatter Control */
	{ 0x00000F73, 0x0004 },    /* R3955  - ANC Coefficient */
	{ 0x00000F74, 0x0002 },    /* R3956  - ANC Coefficient */
	{ 0x00000F75, 0x0000 },    /* R3957  - ANC Coefficient */
	{ 0x00000F76, 0x0010 },    /* R3958  - ANC Coefficient */
	{ 0x00000F77, 0x0000 },    /* R3959  - ANC Coefficient */
	{ 0x00000F78, 0x0000 },    /* R3960  - ANC Coefficient */
	{ 0x00000F79, 0x0000 },    /* R3961  - ANC Coefficient */
	{ 0x00000F7A, 0x0000 },    /* R3962  - ANC Coefficient */
	{ 0x00000F7B, 0x0000 },    /* R3963  - ANC Coefficient */
	{ 0x00000F7C, 0x0000 },    /* R3964  - ANC Coefficient */
	{ 0x00000F7D, 0x0000 },    /* R3965  - ANC Coefficient */
	{ 0x00000F7E, 0x0000 },    /* R3966  - ANC Coefficient */
	{ 0x00000F7F, 0x0000 },    /* R3967  - ANC Coefficient */
	{ 0x00000F80, 0x0000 },    /* R3968  - ANC Coefficient */
	{ 0x00000F81, 0x0000 },    /* R3969  - ANC Coefficient */
	{ 0x00000F82, 0x0000 },    /* R3970  - ANC Coefficient */
	{ 0x00000F83, 0x0000 },    /* R3971  - ANC Coefficient */
	{ 0x00000F84, 0x0000 },    /* R3972  - ANC Coefficient */
	{ 0x00000F85, 0x0000 },    /* R3973  - ANC Coefficient */
	{ 0x00000F86, 0x0000 },    /* R3974  - ANC Coefficient */
	{ 0x00000F87, 0x0000 },    /* R3975  - ANC Coefficient */
	{ 0x00000F88, 0x0000 },    /* R3976  - ANC Coefficient */
	{ 0x00000F89, 0x0000 },    /* R3977  - ANC Coefficient */
	{ 0x00000F8A, 0x0000 },    /* R3978  - ANC Coefficient */
	{ 0x00000F8B, 0x0000 },    /* R3979  - ANC Coefficient */
	{ 0x00000F8C, 0x0000 },    /* R3980  - ANC Coefficient */
	{ 0x00000F8D, 0x0000 },    /* R3981  - ANC Coefficient */
	{ 0x00000F8E, 0x0000 },    /* R3982  - ANC Coefficient */
	{ 0x00000F8F, 0x0000 },    /* R3983  - ANC Coefficient */
	{ 0x00000F90, 0x0000 },    /* R3984  - ANC Coefficient */
	{ 0x00000F91, 0x0000 },    /* R3985  - ANC Coefficient */
	{ 0x00000F92, 0x0000 },    /* R3986  - ANC Coefficient */
	{ 0x00000F93, 0x0000 },    /* R3987  - ANC Coefficient */
	{ 0x00000F94, 0x0000 },    /* R3988  - ANC Coefficient */
	{ 0x00000F95, 0x0000 },    /* R3989  - ANC Coefficient */
	{ 0x00000F96, 0x0000 },    /* R3990  - ANC Coefficient */
	{ 0x00000F97, 0x0000 },    /* R3991  - ANC Coefficient */
	{ 0x00000F98, 0x0000 },    /* R3992  - ANC Coefficient */
	{ 0x00000F99, 0x0000 },    /* R3993  - ANC Coefficient */
	{ 0x00000F9A, 0x0000 },    /* R3994  - ANC Coefficient */
	{ 0x00000F9B, 0x0000 },    /* R3995  - ANC Coefficient */
	{ 0x00000F9C, 0x0000 },    /* R3996  - ANC Coefficient */
	{ 0x00000F9D, 0x0000 },    /* R3997  - ANC Coefficient */
	{ 0x00000F9E, 0x0000 },    /* R3998  - ANC Coefficient */
	{ 0x00000F9F, 0x0000 },    /* R3999  - ANC Coefficient */
	{ 0x00000FA0, 0x0000 },    /* R4000  - ANC Coefficient */
	{ 0x00000FA1, 0x0000 },    /* R4001  - ANC Coefficient */
	{ 0x00000FA2, 0x0000 },    /* R4002  - ANC Coefficient */
	{ 0x00000FA3, 0x0000 },    /* R4003  - ANC Coefficient */
	{ 0x00000FA4, 0x0000 },    /* R4004  - ANC Coefficient */
	{ 0x00000FA5, 0x0000 },    /* R4005  - ANC Coefficient */
	{ 0x00000FA6, 0x0000 },    /* R4006  - ANC Coefficient */
	{ 0x00000FA7, 0x0000 },    /* R4007  - ANC Coefficient */
	{ 0x00000FA8, 0x0000 },    /* R4008  - ANC Coefficient */
	{ 0x00000FA9, 0x0000 },    /* R4009  - ANC Coefficient */
	{ 0x00000FAA, 0x0000 },    /* R4010  - ANC Coefficient */
	{ 0x00000FAB, 0x0000 },    /* R4011  - ANC Coefficient */
	{ 0x00000FAC, 0x0000 },    /* R4012  - ANC Coefficient */
	{ 0x00000FAD, 0x0000 },    /* R4013  - ANC Coefficient */
	{ 0x00000FAE, 0x0000 },    /* R4014  - ANC Coefficient */
	{ 0x00000FAF, 0x0000 },    /* R4015  - ANC Coefficient */
	{ 0x00000FB0, 0x0000 },    /* R4016  - ANC Coefficient */
	{ 0x00000FB1, 0x0000 },    /* R4017  - ANC Coefficient */
	{ 0x00000FB2, 0x0000 },    /* R4018  - ANC Coefficient */
	{ 0x00000FB3, 0x0000 },    /* R4019  - ANC Coefficient */
	{ 0x00000FB4, 0x0000 },    /* R4020  - ANC Coefficient */
	{ 0x00000FB5, 0x0000 },    /* R4021  - ANC Coefficient */
	{ 0x00000FB6, 0x0000 },    /* R4022  - ANC Coefficient */
	{ 0x00000FB7, 0x0000 },    /* R4023  - ANC Coefficient */
	{ 0x00000FB8, 0x0000 },    /* R4024  - ANC Coefficient */
	{ 0x00000FB9, 0x0000 },    /* R4025  - ANC Coefficient */
	{ 0x00000FBA, 0x0000 },    /* R4026  - ANC Coefficient */
	{ 0x00000FBB, 0x0000 },    /* R4027  - ANC Coefficient */
	{ 0x00000FBC, 0x0000 },    /* R4028  - ANC Coefficient */
	{ 0x00000FBD, 0x0000 },    /* R4029  - ANC Coefficient */
	{ 0x00000FBE, 0x0000 },    /* R4030  - ANC Coefficient */
	{ 0x00000FBF, 0x0000 },    /* R4031  - ANC Coefficient */
	{ 0x00000FC0, 0x0000 },    /* R4032  - ANC Coefficient */
	{ 0x00000FC1, 0x0000 },    /* R4033  - ANC Coefficient */
	{ 0x00000FC2, 0x0000 },    /* R4034  - ANC Coefficient */
	{ 0x00000FC3, 0x0000 },    /* R4035  - ANC Coefficient */
	{ 0x00000FC4, 0x0000 },    /* R4036  - ANC Coefficient */
	{ 0x00001100, 0x0010 },    /* R4352  - DSP1 Control 1 */
	{ 0x00001200, 0x0010 },    /* R4608  - DSP2 Control 1 */
	{ 0x00001300, 0x0010 },    /* R4864  - DSP3 Control 1 */
	{ 0x00001400, 0x0010 },    /* R5120  - DSP4 Control 1 */
};

static bool wm5110_is_rev_b_adsp_memory(unsigned int reg)
{
	if ((reg >= 0x100000 && reg < 0x103000) ||
	    (reg >= 0x180000 && reg < 0x181000) ||
	    (reg >= 0x190000 && reg < 0x192000) ||
	    (reg >= 0x1a8000 && reg < 0x1a9000) ||
	    (reg >= 0x200000 && reg < 0x209000) ||
	    (reg >= 0x280000 && reg < 0x281000) ||
	    (reg >= 0x290000 && reg < 0x29a000) ||
	    (reg >= 0x2a8000 && reg < 0x2aa000) ||
	    (reg >= 0x300000 && reg < 0x30f000) ||
	    (reg >= 0x380000 && reg < 0x382000) ||
	    (reg >= 0x390000 && reg < 0x39e000) ||
	    (reg >= 0x3a8000 && reg < 0x3b6000) ||
	    (reg >= 0x400000 && reg < 0x403000) ||
	    (reg >= 0x480000 && reg < 0x481000) ||
	    (reg >= 0x490000 && reg < 0x492000) ||
	    (reg >= 0x4a8000 && reg < 0x4a9000))
		return true;
	else
		return false;
}

static bool wm5110_is_rev_d_adsp_memory(unsigned int reg)
{
	if ((reg >= 0x100000 && reg < 0x106000) ||
	    (reg >= 0x180000 && reg < 0x182000) ||
	    (reg >= 0x190000 && reg < 0x198000) ||
	    (reg >= 0x1a8000 && reg < 0x1aa000) ||
	    (reg >= 0x200000 && reg < 0x20f000) ||
	    (reg >= 0x280000 && reg < 0x282000) ||
	    (reg >= 0x290000 && reg < 0x29c000) ||
	    (reg >= 0x2a6000 && reg < 0x2b4000) ||
	    (reg >= 0x300000 && reg < 0x30f000) ||
	    (reg >= 0x380000 && reg < 0x382000) ||
	    (reg >= 0x390000 && reg < 0x3a2000) ||
	    (reg >= 0x3a6000 && reg < 0x3b4000) ||
	    (reg >= 0x400000 && reg < 0x406000) ||
	    (reg >= 0x480000 && reg < 0x482000) ||
	    (reg >= 0x490000 && reg < 0x498000) ||
	    (reg >= 0x4a8000 && reg < 0x4aa000))
		return true;
	else
		return false;
}

static bool wm5110_is_adsp_memory(struct device *dev, unsigned int reg)
{
	struct arizona *arizona = dev_get_drvdata(dev);

	switch (arizona->rev) {
	case 0 ... 2:
		return wm5110_is_rev_b_adsp_memory(reg);
	default:
		return wm5110_is_rev_d_adsp_memory(reg);
	}
}

static bool wm5110_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ARIZONA_SOFTWARE_RESET:
	case ARIZONA_DEVICE_REVISION:
	case ARIZONA_CTRL_IF_SPI_CFG_1:
	case ARIZONA_CTRL_IF_I2C1_CFG_1:
	case ARIZONA_CTRL_IF_I2C2_CFG_1:
	case ARIZONA_CTRL_IF_I2C1_CFG_2:
	case ARIZONA_CTRL_IF_I2C2_CFG_2:
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
	case ARIZONA_WAKE_CONTROL:
	case ARIZONA_SEQUENCE_CONTROL:
	case ARIZONA_SPARE_TRIGGERS:
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
	case ARIZONA_LDO2_CONTROL_1:
	case ARIZONA_MIC_BIAS_CTRL_1:
	case ARIZONA_MIC_BIAS_CTRL_2:
	case ARIZONA_MIC_BIAS_CTRL_3:
	case ARIZONA_HP_CTRL_1L:
	case ARIZONA_HP_CTRL_1R:
	case ARIZONA_ACCESSORY_DETECT_MODE_1:
	case ARIZONA_HEADPHONE_DETECT_1:
	case ARIZONA_HEADPHONE_DETECT_2:
	case ARIZONA_MICD_CLAMP_CONTROL:
	case ARIZONA_MIC_DETECT_1:
	case ARIZONA_MIC_DETECT_2:
	case ARIZONA_MIC_DETECT_3:
	case ARIZONA_MIC_DETECT_4:
	case ARIZONA_MIC_DETECT_LEVEL_1:
	case ARIZONA_MIC_DETECT_LEVEL_2:
	case ARIZONA_MIC_DETECT_LEVEL_3:
	case ARIZONA_MIC_DETECT_LEVEL_4:
	case ARIZONA_MIC_NOISE_MIX_CONTROL_1:
	case ARIZONA_ISOLATION_CONTROL:
	case ARIZONA_JACK_DETECT_ANALOGUE:
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
	case ARIZONA_IN3L_CONTROL:
	case ARIZONA_ADC_DIGITAL_VOLUME_3L:
	case ARIZONA_DMIC3L_CONTROL:
	case ARIZONA_IN3R_CONTROL:
	case ARIZONA_ADC_DIGITAL_VOLUME_3R:
	case ARIZONA_DMIC3R_CONTROL:
	case ARIZONA_IN4L_CONTROL:
	case ARIZONA_ADC_DIGITAL_VOLUME_4L:
	case ARIZONA_DMIC4L_CONTROL:
	case ARIZONA_IN4R_CONTROL:
	case ARIZONA_ADC_DIGITAL_VOLUME_4R:
	case ARIZONA_DMIC4R_CONTROL:
	case ARIZONA_OUTPUT_ENABLES_1:
	case ARIZONA_OUTPUT_STATUS_1:
	case ARIZONA_RAW_OUTPUT_STATUS_1:
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
	case ARIZONA_OUTPUT_PATH_CONFIG_3R:
	case ARIZONA_DAC_DIGITAL_VOLUME_3R:
	case ARIZONA_DAC_VOLUME_LIMIT_3R:
	case ARIZONA_NOISE_GATE_SELECT_3R:
	case ARIZONA_OUTPUT_PATH_CONFIG_4L:
	case ARIZONA_DAC_DIGITAL_VOLUME_4L:
	case ARIZONA_OUT_VOLUME_4L:
	case ARIZONA_NOISE_GATE_SELECT_4L:
	case ARIZONA_OUTPUT_PATH_CONFIG_4R:
	case ARIZONA_DAC_DIGITAL_VOLUME_4R:
	case ARIZONA_OUT_VOLUME_4R:
	case ARIZONA_NOISE_GATE_SELECT_4R:
	case ARIZONA_OUTPUT_PATH_CONFIG_5L:
	case ARIZONA_DAC_DIGITAL_VOLUME_5L:
	case ARIZONA_DAC_VOLUME_LIMIT_5L:
	case ARIZONA_NOISE_GATE_SELECT_5L:
	case ARIZONA_OUTPUT_PATH_CONFIG_5R:
	case ARIZONA_DAC_DIGITAL_VOLUME_5R:
	case ARIZONA_DAC_VOLUME_LIMIT_5R:
	case ARIZONA_NOISE_GATE_SELECT_5R:
	case ARIZONA_OUTPUT_PATH_CONFIG_6L:
	case ARIZONA_DAC_DIGITAL_VOLUME_6L:
	case ARIZONA_DAC_VOLUME_LIMIT_6L:
	case ARIZONA_NOISE_GATE_SELECT_6L:
	case ARIZONA_OUTPUT_PATH_CONFIG_6R:
	case ARIZONA_DAC_DIGITAL_VOLUME_6R:
	case ARIZONA_DAC_VOLUME_LIMIT_6R:
	case ARIZONA_NOISE_GATE_SELECT_6R:
	case ARIZONA_DRE_ENABLE:
	case ARIZONA_DAC_AEC_CONTROL_1:
	case ARIZONA_NOISE_GATE_CONTROL:
	case ARIZONA_PDM_SPK1_CTRL_1:
	case ARIZONA_PDM_SPK1_CTRL_2:
	case ARIZONA_PDM_SPK2_CTRL_1:
	case ARIZONA_PDM_SPK2_CTRL_2:
	case ARIZONA_HP1_SHORT_CIRCUIT_CTRL:
	case ARIZONA_HP2_SHORT_CIRCUIT_CTRL:
	case ARIZONA_HP3_SHORT_CIRCUIT_CTRL:
	case ARIZONA_HP_TEST_CTRL_1:
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
	case ARIZONA_OUT3RMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT3RMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT3RMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT3RMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT3RMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT3RMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT3RMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT3RMIX_INPUT_4_VOLUME:
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
	case ARIZONA_OUT6LMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT6LMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT6LMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT6LMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT6LMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT6LMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT6LMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT6LMIX_INPUT_4_VOLUME:
	case ARIZONA_OUT6RMIX_INPUT_1_SOURCE:
	case ARIZONA_OUT6RMIX_INPUT_1_VOLUME:
	case ARIZONA_OUT6RMIX_INPUT_2_SOURCE:
	case ARIZONA_OUT6RMIX_INPUT_2_VOLUME:
	case ARIZONA_OUT6RMIX_INPUT_3_SOURCE:
	case ARIZONA_OUT6RMIX_INPUT_3_VOLUME:
	case ARIZONA_OUT6RMIX_INPUT_4_SOURCE:
	case ARIZONA_OUT6RMIX_INPUT_4_VOLUME:
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
	case ARIZONA_DSP4LMIX_INPUT_1_SOURCE:
	case ARIZONA_DSP4LMIX_INPUT_1_VOLUME:
	case ARIZONA_DSP4LMIX_INPUT_2_SOURCE:
	case ARIZONA_DSP4LMIX_INPUT_2_VOLUME:
	case ARIZONA_DSP4LMIX_INPUT_3_SOURCE:
	case ARIZONA_DSP4LMIX_INPUT_3_VOLUME:
	case ARIZONA_DSP4LMIX_INPUT_4_SOURCE:
	case ARIZONA_DSP4LMIX_INPUT_4_VOLUME:
	case ARIZONA_DSP4RMIX_INPUT_1_SOURCE:
	case ARIZONA_DSP4RMIX_INPUT_1_VOLUME:
	case ARIZONA_DSP4RMIX_INPUT_2_SOURCE:
	case ARIZONA_DSP4RMIX_INPUT_2_VOLUME:
	case ARIZONA_DSP4RMIX_INPUT_3_SOURCE:
	case ARIZONA_DSP4RMIX_INPUT_3_VOLUME:
	case ARIZONA_DSP4RMIX_INPUT_4_SOURCE:
	case ARIZONA_DSP4RMIX_INPUT_4_VOLUME:
	case ARIZONA_DSP4AUX1MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP4AUX2MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP4AUX3MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP4AUX4MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP4AUX5MIX_INPUT_1_SOURCE:
	case ARIZONA_DSP4AUX6MIX_INPUT_1_SOURCE:
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
	case ARIZONA_GPIO3_CTRL:
	case ARIZONA_GPIO4_CTRL:
	case ARIZONA_GPIO5_CTRL:
	case ARIZONA_IRQ_CTRL_1:
	case ARIZONA_GPIO_DEBOUNCE_CONFIG:
	case ARIZONA_GP_SWITCH_1:
	case ARIZONA_MISC_PAD_CTRL_1:
	case ARIZONA_MISC_PAD_CTRL_2:
	case ARIZONA_MISC_PAD_CTRL_3:
	case ARIZONA_MISC_PAD_CTRL_4:
	case ARIZONA_MISC_PAD_CTRL_5:
	case ARIZONA_MISC_PAD_CTRL_6:
	case ARIZONA_MISC_PAD_CTRL_7:
	case ARIZONA_MISC_PAD_CTRL_8:
	case ARIZONA_MISC_PAD_CTRL_9:
	case ARIZONA_MISC_PAD_CTRL_10:
	case ARIZONA_MISC_PAD_CTRL_11:
	case ARIZONA_MISC_PAD_CTRL_12:
	case ARIZONA_MISC_PAD_CTRL_13:
	case ARIZONA_MISC_PAD_CTRL_14:
	case ARIZONA_MISC_PAD_CTRL_15:
	case ARIZONA_MISC_PAD_CTRL_16:
	case ARIZONA_MISC_PAD_CTRL_17:
	case ARIZONA_MISC_PAD_CTRL_18:
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
	case ARIZONA_CLOCK_CONTROL:
	case ARIZONA_ANC_SRC:
	case ARIZONA_DSP_STATUS:
	case ARIZONA_ANC_COEFF_START ... ARIZONA_ANC_COEFF_END:
	case ARIZONA_FCL_FILTER_CONTROL:
	case ARIZONA_FCL_ADC_REFORMATTER_CONTROL:
	case ARIZONA_FCL_COEFF_START ... ARIZONA_FCL_COEFF_END:
	case ARIZONA_FCR_FILTER_CONTROL:
	case ARIZONA_FCR_ADC_REFORMATTER_CONTROL:
	case ARIZONA_FCR_COEFF_START ... ARIZONA_FCR_COEFF_END:
	case ARIZONA_DSP1_CONTROL_1:
	case ARIZONA_DSP1_CLOCKING_1:
	case ARIZONA_DSP1_STATUS_1:
	case ARIZONA_DSP1_STATUS_2:
	case ARIZONA_DSP1_STATUS_3:
	case ARIZONA_DSP1_STATUS_4:
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
	case ARIZONA_DSP1_WDMA_OFFSET_1:
	case ARIZONA_DSP1_RDMA_CONFIG_1:
	case ARIZONA_DSP1_RDMA_OFFSET_1:
	case ARIZONA_DSP1_EXTERNAL_START_SELECT_1:
	case ARIZONA_DSP1_SCRATCH_0:
	case ARIZONA_DSP1_SCRATCH_1:
	case ARIZONA_DSP1_SCRATCH_2:
	case ARIZONA_DSP1_SCRATCH_3:
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
	case ARIZONA_DSP4_CONTROL_1:
	case ARIZONA_DSP4_CLOCKING_1:
	case ARIZONA_DSP4_STATUS_1:
	case ARIZONA_DSP4_STATUS_2:
	case ARIZONA_DSP4_STATUS_3:
	case ARIZONA_DSP4_STATUS_4:
	case ARIZONA_DSP4_WDMA_BUFFER_1:
	case ARIZONA_DSP4_WDMA_BUFFER_2:
	case ARIZONA_DSP4_WDMA_BUFFER_3:
	case ARIZONA_DSP4_WDMA_BUFFER_4:
	case ARIZONA_DSP4_WDMA_BUFFER_5:
	case ARIZONA_DSP4_WDMA_BUFFER_6:
	case ARIZONA_DSP4_WDMA_BUFFER_7:
	case ARIZONA_DSP4_WDMA_BUFFER_8:
	case ARIZONA_DSP4_RDMA_BUFFER_1:
	case ARIZONA_DSP4_RDMA_BUFFER_2:
	case ARIZONA_DSP4_RDMA_BUFFER_3:
	case ARIZONA_DSP4_RDMA_BUFFER_4:
	case ARIZONA_DSP4_RDMA_BUFFER_5:
	case ARIZONA_DSP4_RDMA_BUFFER_6:
	case ARIZONA_DSP4_WDMA_CONFIG_1:
	case ARIZONA_DSP4_WDMA_CONFIG_2:
	case ARIZONA_DSP4_WDMA_OFFSET_1:
	case ARIZONA_DSP4_RDMA_CONFIG_1:
	case ARIZONA_DSP4_RDMA_OFFSET_1:
	case ARIZONA_DSP4_EXTERNAL_START_SELECT_1:
	case ARIZONA_DSP4_SCRATCH_0:
	case ARIZONA_DSP4_SCRATCH_1:
	case ARIZONA_DSP4_SCRATCH_2:
	case ARIZONA_DSP4_SCRATCH_3:
		return true;
	default:
		return wm5110_is_adsp_memory(dev, reg);
	}
}

static bool wm5110_volatile_register(struct device *dev, unsigned int reg)
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
	case ARIZONA_MIC_DETECT_3:
	case ARIZONA_MIC_DETECT_4:
	case ARIZONA_HP_CTRL_1L:
	case ARIZONA_HP_CTRL_1R:
	case ARIZONA_HEADPHONE_DETECT_2:
	case ARIZONA_INPUT_ENABLES_STATUS:
	case ARIZONA_OUTPUT_STATUS_1:
	case ARIZONA_RAW_OUTPUT_STATUS_1:
	case ARIZONA_HP_TEST_CTRL_1:
	case ARIZONA_SLIMBUS_RX_PORT_STATUS:
	case ARIZONA_SLIMBUS_TX_PORT_STATUS:
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
	case ARIZONA_AOD_WKUP_AND_TRIG:
	case ARIZONA_AOD_IRQ1:
	case ARIZONA_AOD_IRQ2:
	case ARIZONA_AOD_IRQ_RAW_STATUS:
	case ARIZONA_FX_CTRL2:
	case ARIZONA_ASRC_STATUS:
	case ARIZONA_CLOCK_CONTROL:
	case ARIZONA_DSP_STATUS:
	case ARIZONA_DSP1_STATUS_1:
	case ARIZONA_DSP1_STATUS_2:
	case ARIZONA_DSP1_STATUS_3:
	case ARIZONA_DSP1_STATUS_4:
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
	case ARIZONA_DSP1_WDMA_OFFSET_1:
	case ARIZONA_DSP1_RDMA_CONFIG_1:
	case ARIZONA_DSP1_RDMA_OFFSET_1:
	case ARIZONA_DSP1_EXTERNAL_START_SELECT_1:
	case ARIZONA_DSP1_SCRATCH_0:
	case ARIZONA_DSP1_SCRATCH_1:
	case ARIZONA_DSP1_SCRATCH_2:
	case ARIZONA_DSP1_SCRATCH_3:
	case ARIZONA_DSP1_CLOCKING_1:
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
	case ARIZONA_DSP4_STATUS_1:
	case ARIZONA_DSP4_STATUS_2:
	case ARIZONA_DSP4_STATUS_3:
	case ARIZONA_DSP4_STATUS_4:
	case ARIZONA_DSP4_WDMA_BUFFER_1:
	case ARIZONA_DSP4_WDMA_BUFFER_2:
	case ARIZONA_DSP4_WDMA_BUFFER_3:
	case ARIZONA_DSP4_WDMA_BUFFER_4:
	case ARIZONA_DSP4_WDMA_BUFFER_5:
	case ARIZONA_DSP4_WDMA_BUFFER_6:
	case ARIZONA_DSP4_WDMA_BUFFER_7:
	case ARIZONA_DSP4_WDMA_BUFFER_8:
	case ARIZONA_DSP4_RDMA_BUFFER_1:
	case ARIZONA_DSP4_RDMA_BUFFER_2:
	case ARIZONA_DSP4_RDMA_BUFFER_3:
	case ARIZONA_DSP4_RDMA_BUFFER_4:
	case ARIZONA_DSP4_RDMA_BUFFER_5:
	case ARIZONA_DSP4_RDMA_BUFFER_6:
	case ARIZONA_DSP4_WDMA_CONFIG_1:
	case ARIZONA_DSP4_WDMA_CONFIG_2:
	case ARIZONA_DSP4_WDMA_OFFSET_1:
	case ARIZONA_DSP4_RDMA_CONFIG_1:
	case ARIZONA_DSP4_RDMA_OFFSET_1:
	case ARIZONA_DSP4_EXTERNAL_START_SELECT_1:
	case ARIZONA_DSP4_SCRATCH_0:
	case ARIZONA_DSP4_SCRATCH_1:
	case ARIZONA_DSP4_SCRATCH_2:
	case ARIZONA_DSP4_SCRATCH_3:
	case ARIZONA_DSP4_CLOCKING_1:
		return true;
	default:
		return wm5110_is_adsp_memory(dev, reg);
	}
}

#define WM5110_MAX_REGISTER 0x4a9fff

const struct regmap_config wm5110_spi_regmap = {
	.reg_bits = 32,
	.pad_bits = 16,
	.val_bits = 16,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = WM5110_MAX_REGISTER,
	.readable_reg = wm5110_readable_register,
	.volatile_reg = wm5110_volatile_register,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm5110_reg_default,
	.num_reg_defaults = ARRAY_SIZE(wm5110_reg_default),
};
EXPORT_SYMBOL_GPL(wm5110_spi_regmap);

const struct regmap_config wm5110_i2c_regmap = {
	.reg_bits = 32,
	.val_bits = 16,
	.reg_format_endian = REGMAP_ENDIAN_BIG,
	.val_format_endian = REGMAP_ENDIAN_BIG,

	.max_register = WM5110_MAX_REGISTER,
	.readable_reg = wm5110_readable_register,
	.volatile_reg = wm5110_volatile_register,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = wm5110_reg_default,
	.num_reg_defaults = ARRAY_SIZE(wm5110_reg_default),
};
EXPORT_SYMBOL_GPL(wm5110_i2c_regmap);
