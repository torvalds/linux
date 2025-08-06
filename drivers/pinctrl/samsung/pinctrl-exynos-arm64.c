// SPDX-License-Identifier: GPL-2.0+
//
// Exynos ARMv8 specific support for Samsung pinctrl/gpiolib driver
// with eint support.
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
// Copyright (c) 2012 Linaro Ltd
//		http://www.linaro.org
// Copyright (c) 2017 Krzysztof Kozlowski <krzk@kernel.org>
//
// This file contains the Samsung Exynos specific information required by the
// the Samsung pinctrl/gpiolib driver. It also includes the implementation of
// external gpio and wakeup interrupt support.

#include <linux/slab.h>
#include <linux/soc/samsung/exynos-regs-pmu.h>

#include "pinctrl-samsung.h"
#include "pinctrl-exynos.h"

static const struct samsung_pin_bank_type bank_type_off = {
	.fld_width = { 4, 1, 2, 2, 2, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, },
};

static const struct samsung_pin_bank_type bank_type_alive = {
	.fld_width = { 4, 1, 2, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, 0x0c, },
};

/* Exynos5433 has the 4bit widths for PINCFG_TYPE_DRV bitfields. */
static const struct samsung_pin_bank_type exynos5433_bank_type_off = {
	.fld_width = { 4, 1, 2, 4, 2, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, },
};

static const struct samsung_pin_bank_type exynos5433_bank_type_alive = {
	.fld_width = { 4, 1, 2, 4, },
	.reg_offset = { 0x00, 0x04, 0x08, 0x0c, },
};

/*
 * Bank type for alive type. Bit fields:
 * CON: 4, DAT: 1, PUD: 2, DRV: 3
 */
static const struct samsung_pin_bank_type exynos7870_bank_type_alive = {
	.fld_width = { 4, 1, 2, 3, },
	.reg_offset = { 0x00, 0x04, 0x08, 0x0c, },
};

/*
 * Bank type for non-alive type. Bit fields:
 * CON: 4, DAT: 1, PUD: 4, DRV: 4, CONPDN: 2, PUDPDN: 4
 */
static const struct samsung_pin_bank_type exynos850_bank_type_off  = {
	.fld_width = { 4, 1, 4, 4, 2, 4, },
	.reg_offset = { 0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, },
};

/*
 * Bank type for alive type. Bit fields:
 * CON: 4, DAT: 1, PUD: 4, DRV: 4
 */
static const struct samsung_pin_bank_type exynos850_bank_type_alive = {
	.fld_width = { 4, 1, 4, 4, },
	.reg_offset = { 0x00, 0x04, 0x08, 0x0c, },
};

/*
 * Bank type for non-alive type. Bit fields:
 * CON: 4, DAT: 1, PUD: 2, DRV: 3, CONPDN: 2, PUDPDN: 2
 */
static const struct samsung_pin_bank_type exynos8895_bank_type_off  = {
	.fld_width = { 4, 1, 2, 3, 2, 2, },
	.reg_offset = { 0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, },
};

/* Pad retention control code for accessing PMU regmap */
static atomic_t exynos_shared_retention_refcnt;

/* pin banks of exynos2200 pin-controller - ALIVE */
static const struct samsung_pin_bank_data exynos2200_pin_banks0[] __initconst = {
	EXYNOS850_PIN_BANK_EINTW(8, 0x0, "gpa0", 0x00),
	EXYNOS850_PIN_BANK_EINTW(8, 0x20, "gpa1", 0x04),
	EXYNOS850_PIN_BANK_EINTW(8, 0x40, "gpa2", 0x08),
	EXYNOS850_PIN_BANK_EINTW(8, 0x60, "gpa3", 0x0c),
	EXYNOS850_PIN_BANK_EINTW(2, 0x80, "gpa4", 0x10),
	EXYNOS_PIN_BANK_EINTN(4, 0xa0, "gpq0"),
	EXYNOS_PIN_BANK_EINTN(2, 0xc0, "gpq1"),
	EXYNOS_PIN_BANK_EINTN(2, 0xe0, "gpq2"),
};

/* pin banks of exynos2200 pin-controller - CMGP */
static const struct samsung_pin_bank_data exynos2200_pin_banks1[] __initconst = {
	EXYNOS850_PIN_BANK_EINTW(2, 0x0, "gpm0", 0x00),
	EXYNOS850_PIN_BANK_EINTW(2, 0x20, "gpm1", 0x04),
	EXYNOS850_PIN_BANK_EINTW(2, 0x40, "gpm2", 0x08),
	EXYNOS850_PIN_BANK_EINTW(2, 0x60, "gpm3", 0x0c),
	EXYNOS850_PIN_BANK_EINTW(2, 0x80, "gpm4", 0x10),
	EXYNOS850_PIN_BANK_EINTW(2, 0xa0, "gpm5", 0x14),
	EXYNOS850_PIN_BANK_EINTW(2, 0xc0, "gpm6", 0x18),
	EXYNOS850_PIN_BANK_EINTW(2, 0xe0, "gpm7", 0x1c),
	EXYNOS850_PIN_BANK_EINTW(2, 0x100, "gpm8", 0x20),
	EXYNOS850_PIN_BANK_EINTW(2, 0x120, "gpm9", 0x24),
	EXYNOS850_PIN_BANK_EINTW(2, 0x140, "gpm10", 0x28),
	EXYNOS850_PIN_BANK_EINTW(2, 0x160, "gpm11", 0x2c),
	EXYNOS850_PIN_BANK_EINTW(2, 0x180, "gpm12", 0x30),
	EXYNOS850_PIN_BANK_EINTW(2, 0x1a0, "gpm13", 0x34),
	EXYNOS850_PIN_BANK_EINTW(1, 0x1c0, "gpm14", 0x38),
	EXYNOS850_PIN_BANK_EINTW(1, 0x1e0, "gpm15", 0x3c),
	EXYNOS850_PIN_BANK_EINTW(1, 0x200, "gpm16", 0x40),
	EXYNOS850_PIN_BANK_EINTW(1, 0x220, "gpm17", 0x44),
	EXYNOS850_PIN_BANK_EINTW(1, 0x240, "gpm20", 0x48),
	EXYNOS850_PIN_BANK_EINTW(1, 0x260, "gpm21", 0x4c),
	EXYNOS850_PIN_BANK_EINTW(1, 0x280, "gpm22", 0x50),
	EXYNOS850_PIN_BANK_EINTW(1, 0x2a0, "gpm23", 0x54),
	EXYNOS850_PIN_BANK_EINTW(1, 0x2c0, "gpm24", 0x58),
};

/* pin banks of exynos2200 pin-controller - HSI1 */
static const struct samsung_pin_bank_data exynos2200_pin_banks2[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(4, 0x0, "gpf0", 0x00),
};

/* pin banks of exynos2200 pin-controller - UFS */
static const struct samsung_pin_bank_data exynos2200_pin_banks3[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(7, 0x0, "gpf1", 0x00),
};

/* pin banks of exynos2200 pin-controller - HSI1UFS */
static const struct samsung_pin_bank_data exynos2200_pin_banks4[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(2, 0x0, "gpf2", 0x00),
};

/* pin banks of exynos2200 pin-controller - PERIC0 */
static const struct samsung_pin_bank_data exynos2200_pin_banks5[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(4, 0x0, "gpb0",  0x00),
	EXYNOS850_PIN_BANK_EINTG(4, 0x20, "gpb1",  0x04),
	EXYNOS850_PIN_BANK_EINTG(4, 0x40, "gpb2",  0x08),
	EXYNOS850_PIN_BANK_EINTG(4, 0x60, "gpb3",  0x0c),
	EXYNOS850_PIN_BANK_EINTG(4, 0x80, "gpp4",  0x10),
	EXYNOS850_PIN_BANK_EINTG(2, 0xa0, "gpc0",  0x14),
	EXYNOS850_PIN_BANK_EINTG(2, 0xc0, "gpc1",  0x18),
	EXYNOS850_PIN_BANK_EINTG(2, 0xe0, "gpc2",  0x1c),
	EXYNOS850_PIN_BANK_EINTG(7, 0x100, "gpg1",  0x20),
	EXYNOS850_PIN_BANK_EINTG(2, 0x120, "gpg2",  0x24),
};

/* pin banks of exynos2200 pin-controller - PERIC1 */
static const struct samsung_pin_bank_data exynos2200_pin_banks6[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(4, 0x0,  "gpp7",  0x00),
	EXYNOS850_PIN_BANK_EINTG(4, 0x20, "gpp8",  0x04),
	EXYNOS850_PIN_BANK_EINTG(4, 0x40, "gpp9",  0x08),
	EXYNOS850_PIN_BANK_EINTG(4, 0x60, "gpp10", 0x0c),
};

/* pin banks of exynos2200 pin-controller - PERIC2 */
static const struct samsung_pin_bank_data exynos2200_pin_banks7[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(4, 0x0, "gpp0",  0x00),
	EXYNOS850_PIN_BANK_EINTG(4, 0x20, "gpp1",  0x04),
	EXYNOS850_PIN_BANK_EINTG(4, 0x40, "gpp2",  0x08),
	EXYNOS850_PIN_BANK_EINTG(4, 0x60, "gpp3",  0x0c),
	EXYNOS850_PIN_BANK_EINTG(4, 0x80, "gpp5",  0x10),
	EXYNOS850_PIN_BANK_EINTG(4, 0xa0, "gpp6",  0x14),
	EXYNOS850_PIN_BANK_EINTG(4, 0xc0, "gpp11", 0x18),
	EXYNOS850_PIN_BANK_EINTG(2, 0xe0, "gpc3",  0x1c),
	EXYNOS850_PIN_BANK_EINTG(2, 0x100, "gpc4",  0x20),
	EXYNOS850_PIN_BANK_EINTG(2, 0x120, "gpc5",  0x24),
	EXYNOS850_PIN_BANK_EINTG(2, 0x140, "gpc6",  0x28),
	EXYNOS850_PIN_BANK_EINTG(2, 0x160, "gpc7",  0x2c),
	EXYNOS850_PIN_BANK_EINTG(2, 0x180, "gpc8",  0x30),
	EXYNOS850_PIN_BANK_EINTG(2, 0x1a0, "gpc9",  0x34),
	EXYNOS850_PIN_BANK_EINTG(5, 0x1c0, "gpg0",  0x38),
};

/* pin banks of exynos2200 pin-controller - VTS */
static const struct samsung_pin_bank_data exynos2200_pin_banks8[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(7, 0x0, "gpv0", 0x00),
};

static const struct samsung_pin_ctrl exynos2200_pin_ctrl[] = {
	{
		/* pin-controller instance 0 ALIVE data */
		.pin_banks	= exynos2200_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos2200_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 CMGP data */
		.pin_banks	= exynos2200_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos2200_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 2 HSI1 data */
		.pin_banks	= exynos2200_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos2200_pin_banks2),
	}, {
		/* pin-controller instance 3 UFS data */
		.pin_banks	= exynos2200_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos2200_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 4 HSI1UFS data */
		.pin_banks	= exynos2200_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos2200_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 5 PERIC0 data */
		.pin_banks	= exynos2200_pin_banks5,
		.nr_banks	= ARRAY_SIZE(exynos2200_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 6 PERIC1 data */
		.pin_banks	= exynos2200_pin_banks6,
		.nr_banks	= ARRAY_SIZE(exynos2200_pin_banks6),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 7 PERIC2 data */
		.pin_banks	= exynos2200_pin_banks7,
		.nr_banks	= ARRAY_SIZE(exynos2200_pin_banks7),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 8 VTS data */
		.pin_banks	= exynos2200_pin_banks8,
		.nr_banks	= ARRAY_SIZE(exynos2200_pin_banks8),
	},
};

const struct samsung_pinctrl_of_match_data exynos2200_of_data __initconst = {
	.ctrl		= exynos2200_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos2200_pin_ctrl),
};

/* pin banks of exynos5433 pin-controller - ALIVE */
static const struct samsung_pin_bank_data exynos5433_pin_banks0[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS5433_PIN_BANK_EINTW(8, 0x000, "gpa0", 0x00),
	EXYNOS5433_PIN_BANK_EINTW(8, 0x020, "gpa1", 0x04),
	EXYNOS5433_PIN_BANK_EINTW(8, 0x040, "gpa2", 0x08),
	EXYNOS5433_PIN_BANK_EINTW(8, 0x060, "gpa3", 0x0c),
	EXYNOS5433_PIN_BANK_EINTW_EXT(8, 0x020, "gpf1", 0x1004, 1),
	EXYNOS5433_PIN_BANK_EINTW_EXT(4, 0x040, "gpf2", 0x1008, 1),
	EXYNOS5433_PIN_BANK_EINTW_EXT(4, 0x060, "gpf3", 0x100c, 1),
	EXYNOS5433_PIN_BANK_EINTW_EXT(8, 0x080, "gpf4", 0x1010, 1),
	EXYNOS5433_PIN_BANK_EINTW_EXT(8, 0x0a0, "gpf5", 0x1014, 1),
};

/* pin banks of exynos5433 pin-controller - AUD */
static const struct samsung_pin_bank_data exynos5433_pin_banks1[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS5433_PIN_BANK_EINTG(7, 0x000, "gpz0", 0x00),
	EXYNOS5433_PIN_BANK_EINTG(4, 0x020, "gpz1", 0x04),
};

/* pin banks of exynos5433 pin-controller - CPIF */
static const struct samsung_pin_bank_data exynos5433_pin_banks2[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS5433_PIN_BANK_EINTG(2, 0x000, "gpv6", 0x00),
};

/* pin banks of exynos5433 pin-controller - eSE */
static const struct samsung_pin_bank_data exynos5433_pin_banks3[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS5433_PIN_BANK_EINTG(3, 0x000, "gpj2", 0x00),
};

/* pin banks of exynos5433 pin-controller - FINGER */
static const struct samsung_pin_bank_data exynos5433_pin_banks4[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS5433_PIN_BANK_EINTG(4, 0x000, "gpd5", 0x00),
};

/* pin banks of exynos5433 pin-controller - FSYS */
static const struct samsung_pin_bank_data exynos5433_pin_banks5[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS5433_PIN_BANK_EINTG(6, 0x000, "gph1", 0x00),
	EXYNOS5433_PIN_BANK_EINTG(7, 0x020, "gpr4", 0x04),
	EXYNOS5433_PIN_BANK_EINTG(5, 0x040, "gpr0", 0x08),
	EXYNOS5433_PIN_BANK_EINTG(8, 0x060, "gpr1", 0x0c),
	EXYNOS5433_PIN_BANK_EINTG(2, 0x080, "gpr2", 0x10),
	EXYNOS5433_PIN_BANK_EINTG(8, 0x0a0, "gpr3", 0x14),
};

/* pin banks of exynos5433 pin-controller - IMEM */
static const struct samsung_pin_bank_data exynos5433_pin_banks6[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS5433_PIN_BANK_EINTG(8, 0x000, "gpf0", 0x00),
};

/* pin banks of exynos5433 pin-controller - NFC */
static const struct samsung_pin_bank_data exynos5433_pin_banks7[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS5433_PIN_BANK_EINTG(3, 0x000, "gpj0", 0x00),
};

/* pin banks of exynos5433 pin-controller - PERIC */
static const struct samsung_pin_bank_data exynos5433_pin_banks8[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS5433_PIN_BANK_EINTG(6, 0x000, "gpv7", 0x00),
	EXYNOS5433_PIN_BANK_EINTG(5, 0x020, "gpb0", 0x04),
	EXYNOS5433_PIN_BANK_EINTG(8, 0x040, "gpc0", 0x08),
	EXYNOS5433_PIN_BANK_EINTG(2, 0x060, "gpc1", 0x0c),
	EXYNOS5433_PIN_BANK_EINTG(6, 0x080, "gpc2", 0x10),
	EXYNOS5433_PIN_BANK_EINTG(8, 0x0a0, "gpc3", 0x14),
	EXYNOS5433_PIN_BANK_EINTG(2, 0x0c0, "gpg0", 0x18),
	EXYNOS5433_PIN_BANK_EINTG(4, 0x0e0, "gpd0", 0x1c),
	EXYNOS5433_PIN_BANK_EINTG(6, 0x100, "gpd1", 0x20),
	EXYNOS5433_PIN_BANK_EINTG(8, 0x120, "gpd2", 0x24),
	EXYNOS5433_PIN_BANK_EINTG(5, 0x140, "gpd4", 0x28),
	EXYNOS5433_PIN_BANK_EINTG(2, 0x160, "gpd8", 0x2c),
	EXYNOS5433_PIN_BANK_EINTG(7, 0x180, "gpd6", 0x30),
	EXYNOS5433_PIN_BANK_EINTG(3, 0x1a0, "gpd7", 0x34),
	EXYNOS5433_PIN_BANK_EINTG(5, 0x1c0, "gpg1", 0x38),
	EXYNOS5433_PIN_BANK_EINTG(2, 0x1e0, "gpg2", 0x3c),
	EXYNOS5433_PIN_BANK_EINTG(8, 0x200, "gpg3", 0x40),
};

/* pin banks of exynos5433 pin-controller - TOUCH */
static const struct samsung_pin_bank_data exynos5433_pin_banks9[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS5433_PIN_BANK_EINTG(3, 0x000, "gpj1", 0x00),
};

/* PMU pin retention groups registers for Exynos5433 (without audio & fsys) */
static const u32 exynos5433_retention_regs[] = {
	EXYNOS5433_PAD_RETENTION_TOP_OPTION,
	EXYNOS5433_PAD_RETENTION_UART_OPTION,
	EXYNOS5433_PAD_RETENTION_EBIA_OPTION,
	EXYNOS5433_PAD_RETENTION_EBIB_OPTION,
	EXYNOS5433_PAD_RETENTION_SPI_OPTION,
	EXYNOS5433_PAD_RETENTION_MIF_OPTION,
	EXYNOS5433_PAD_RETENTION_USBXTI_OPTION,
	EXYNOS5433_PAD_RETENTION_BOOTLDO_OPTION,
	EXYNOS5433_PAD_RETENTION_UFS_OPTION,
	EXYNOS5433_PAD_RETENTION_FSYSGENIO_OPTION,
};

static const struct samsung_retention_data exynos5433_retention_data __initconst = {
	.regs	 = exynos5433_retention_regs,
	.nr_regs = ARRAY_SIZE(exynos5433_retention_regs),
	.value	 = EXYNOS_WAKEUP_FROM_LOWPWR,
	.refcnt	 = &exynos_shared_retention_refcnt,
	.init	 = exynos_retention_init,
};

/* PMU retention control for audio pins can be tied to audio pin bank */
static const u32 exynos5433_audio_retention_regs[] = {
	EXYNOS5433_PAD_RETENTION_AUD_OPTION,
};

static const struct samsung_retention_data exynos5433_audio_retention_data __initconst = {
	.regs	 = exynos5433_audio_retention_regs,
	.nr_regs = ARRAY_SIZE(exynos5433_audio_retention_regs),
	.value	 = EXYNOS_WAKEUP_FROM_LOWPWR,
	.init	 = exynos_retention_init,
};

/* PMU retention control for mmc pins can be tied to fsys pin bank */
static const u32 exynos5433_fsys_retention_regs[] = {
	EXYNOS5433_PAD_RETENTION_MMC0_OPTION,
	EXYNOS5433_PAD_RETENTION_MMC1_OPTION,
	EXYNOS5433_PAD_RETENTION_MMC2_OPTION,
};

static const struct samsung_retention_data exynos5433_fsys_retention_data __initconst = {
	.regs	 = exynos5433_fsys_retention_regs,
	.nr_regs = ARRAY_SIZE(exynos5433_fsys_retention_regs),
	.value	 = EXYNOS_WAKEUP_FROM_LOWPWR,
	.init	 = exynos_retention_init,
};

/*
 * Samsung pinctrl driver data for Exynos5433 SoC. Exynos5433 SoC includes
 * ten gpio/pin-mux/pinconfig controllers.
 */
static const struct samsung_pin_ctrl exynos5433_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos5433_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks0),
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.nr_ext_resources = 1,
		.retention_data	= &exynos5433_retention_data,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos5433_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos5433_audio_retention_data,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos5433_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos5433_retention_data,
	}, {
		/* pin-controller instance 3 data */
		.pin_banks	= exynos5433_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos5433_retention_data,
	}, {
		/* pin-controller instance 4 data */
		.pin_banks	= exynos5433_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos5433_retention_data,
	}, {
		/* pin-controller instance 5 data */
		.pin_banks	= exynos5433_pin_banks5,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos5433_fsys_retention_data,
	}, {
		/* pin-controller instance 6 data */
		.pin_banks	= exynos5433_pin_banks6,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks6),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos5433_retention_data,
	}, {
		/* pin-controller instance 7 data */
		.pin_banks	= exynos5433_pin_banks7,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks7),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos5433_retention_data,
	}, {
		/* pin-controller instance 8 data */
		.pin_banks	= exynos5433_pin_banks8,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks8),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos5433_retention_data,
	}, {
		/* pin-controller instance 9 data */
		.pin_banks	= exynos5433_pin_banks9,
		.nr_banks	= ARRAY_SIZE(exynos5433_pin_banks9),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos5433_retention_data,
	},
};

const struct samsung_pinctrl_of_match_data exynos5433_of_data __initconst = {
	.ctrl		= exynos5433_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos5433_pin_ctrl),
};

/* pin banks of exynos7 pin-controller - ALIVE */
static const struct samsung_pin_bank_data exynos7_pin_banks0[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTW(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0x040, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0x060, "gpa3", 0x0c),
};

/* pin banks of exynos7 pin-controller - BUS0 */
static const struct samsung_pin_bank_data exynos7_pin_banks1[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(5, 0x000, "gpb0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpc0", 0x04),
	EXYNOS_PIN_BANK_EINTG(2, 0x040, "gpc1", 0x08),
	EXYNOS_PIN_BANK_EINTG(6, 0x060, "gpc2", 0x0c),
	EXYNOS_PIN_BANK_EINTG(8, 0x080, "gpc3", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0a0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(6, 0x0c0, "gpd1", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x0e0, "gpd2", 0x1c),
	EXYNOS_PIN_BANK_EINTG(5, 0x100, "gpd4", 0x20),
	EXYNOS_PIN_BANK_EINTG(4, 0x120, "gpd5", 0x24),
	EXYNOS_PIN_BANK_EINTG(6, 0x140, "gpd6", 0x28),
	EXYNOS_PIN_BANK_EINTG(3, 0x160, "gpd7", 0x2c),
	EXYNOS_PIN_BANK_EINTG(2, 0x180, "gpd8", 0x30),
	EXYNOS_PIN_BANK_EINTG(2, 0x1a0, "gpg0", 0x34),
	EXYNOS_PIN_BANK_EINTG(4, 0x1c0, "gpg3", 0x38),
};

/* pin banks of exynos7 pin-controller - NFC */
static const struct samsung_pin_bank_data exynos7_pin_banks2[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(3, 0x000, "gpj0", 0x00),
};

/* pin banks of exynos7 pin-controller - TOUCH */
static const struct samsung_pin_bank_data exynos7_pin_banks3[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(3, 0x000, "gpj1", 0x00),
};

/* pin banks of exynos7 pin-controller - FF */
static const struct samsung_pin_bank_data exynos7_pin_banks4[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(4, 0x000, "gpg4", 0x00),
};

/* pin banks of exynos7 pin-controller - ESE */
static const struct samsung_pin_bank_data exynos7_pin_banks5[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(5, 0x000, "gpv7", 0x00),
};

/* pin banks of exynos7 pin-controller - FSYS0 */
static const struct samsung_pin_bank_data exynos7_pin_banks6[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpr4", 0x00),
};

/* pin banks of exynos7 pin-controller - FSYS1 */
static const struct samsung_pin_bank_data exynos7_pin_banks7[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(4, 0x000, "gpr0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpr1", 0x04),
	EXYNOS_PIN_BANK_EINTG(5, 0x040, "gpr2", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpr3", 0x0c),
};

/* pin banks of exynos7 pin-controller - BUS1 */
static const struct samsung_pin_bank_data exynos7_pin_banks8[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpf0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpf1", 0x04),
	EXYNOS_PIN_BANK_EINTG(4, 0x060, "gpf2", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpf3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(8, 0x0a0, "gpf4", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0c0, "gpf5", 0x14),
	EXYNOS_PIN_BANK_EINTG(5, 0x0e0, "gpg1", 0x18),
	EXYNOS_PIN_BANK_EINTG(5, 0x100, "gpg2", 0x1c),
	EXYNOS_PIN_BANK_EINTG(6, 0x120, "gph1", 0x20),
	EXYNOS_PIN_BANK_EINTG(3, 0x140, "gpv6", 0x24),
};

static const struct samsung_pin_bank_data exynos7_pin_banks9[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz0", 0x00),
	EXYNOS_PIN_BANK_EINTG(4, 0x020, "gpz1", 0x04),
};

static const struct samsung_pin_ctrl exynos7_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 Alive data */
		.pin_banks	= exynos7_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks0),
		.eint_wkup_init = exynos_eint_wkup_init,
	}, {
		/* pin-controller instance 1 BUS0 data */
		.pin_banks	= exynos7_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 2 NFC data */
		.pin_banks	= exynos7_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 3 TOUCH data */
		.pin_banks	= exynos7_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 4 FF data */
		.pin_banks	= exynos7_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 5 ESE data */
		.pin_banks	= exynos7_pin_banks5,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 6 FSYS0 data */
		.pin_banks	= exynos7_pin_banks6,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks6),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 7 FSYS1 data */
		.pin_banks	= exynos7_pin_banks7,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks7),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 8 BUS1 data */
		.pin_banks	= exynos7_pin_banks8,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks8),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 9 AUD data */
		.pin_banks	= exynos7_pin_banks9,
		.nr_banks	= ARRAY_SIZE(exynos7_pin_banks9),
		.eint_gpio_init = exynos_eint_gpio_init,
	},
};

const struct samsung_pinctrl_of_match_data exynos7_of_data __initconst = {
	.ctrl		= exynos7_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos7_pin_ctrl),
};

/* pin banks of exynos7870 pin-controller 0 (ALIVE) */
static const struct samsung_pin_bank_data exynos7870_pin_banks0[] __initconst = {
	EXYNOS7870_PIN_BANK_EINTN(6, 0x000, "etc0"),
	EXYNOS7870_PIN_BANK_EINTN(3, 0x020, "etc1"),
	EXYNOS7870_PIN_BANK_EINTW(8, 0x040, "gpa0", 0x00),
	EXYNOS7870_PIN_BANK_EINTW(8, 0x060, "gpa1", 0x04),
	EXYNOS7870_PIN_BANK_EINTW(8, 0x080, "gpa2", 0x08),
	EXYNOS7870_PIN_BANK_EINTN(2, 0x0c0, "gpq0"),
};

/* pin banks of exynos7870 pin-controller 1 (DISPAUD) */
static const struct samsung_pin_bank_data exynos7870_pin_banks1[] __initconst = {
	EXYNOS8895_PIN_BANK_EINTG(4, 0x000, "gpz0", 0x00),
	EXYNOS8895_PIN_BANK_EINTG(6, 0x020, "gpz1", 0x04),
	EXYNOS8895_PIN_BANK_EINTG(4, 0x040, "gpz2", 0x08),
};

/* pin banks of exynos7870 pin-controller 2 (ESE) */
static const struct samsung_pin_bank_data exynos7870_pin_banks2[] __initconst = {
	EXYNOS8895_PIN_BANK_EINTG(5, 0x000, "gpc7", 0x00),
};

/* pin banks of exynos7870 pin-controller 3 (FSYS) */
static const struct samsung_pin_bank_data exynos7870_pin_banks3[] __initconst = {
	EXYNOS8895_PIN_BANK_EINTG(3, 0x000, "gpr0", 0x00),
	EXYNOS8895_PIN_BANK_EINTG(8, 0x020, "gpr1", 0x04),
	EXYNOS8895_PIN_BANK_EINTG(2, 0x040, "gpr2", 0x08),
	EXYNOS8895_PIN_BANK_EINTG(4, 0x060, "gpr3", 0x0c),
	EXYNOS8895_PIN_BANK_EINTG(6, 0x080, "gpr4", 0x10),
};

/* pin banks of exynos7870 pin-controller 4 (MIF) */
static const struct samsung_pin_bank_data exynos7870_pin_banks4[] __initconst = {
	EXYNOS8895_PIN_BANK_EINTG(2, 0x000, "gpm0", 0x00),
};

/* pin banks of exynos7870 pin-controller 5 (NFC) */
static const struct samsung_pin_bank_data exynos7870_pin_banks5[] __initconst = {
	EXYNOS8895_PIN_BANK_EINTG(4, 0x000, "gpc2", 0x00),
};

/* pin banks of exynos7870 pin-controller 6 (TOP) */
static const struct samsung_pin_bank_data exynos7870_pin_banks6[] __initconst = {
	EXYNOS8895_PIN_BANK_EINTG(4, 0x000, "gpb0", 0x00),
	EXYNOS8895_PIN_BANK_EINTG(3, 0x020, "gpc0", 0x04),
	EXYNOS8895_PIN_BANK_EINTG(4, 0x040, "gpc1", 0x08),
	EXYNOS8895_PIN_BANK_EINTG(4, 0x060, "gpc4", 0x0c),
	EXYNOS8895_PIN_BANK_EINTG(2, 0x080, "gpc5", 0x10),
	EXYNOS8895_PIN_BANK_EINTG(4, 0x0a0, "gpc6", 0x14),
	EXYNOS8895_PIN_BANK_EINTG(2, 0x0c0, "gpc8", 0x18),
	EXYNOS8895_PIN_BANK_EINTG(2, 0x0e0, "gpc9", 0x1c),
	EXYNOS8895_PIN_BANK_EINTG(7, 0x100, "gpd1", 0x20),
	EXYNOS8895_PIN_BANK_EINTG(6, 0x120, "gpd2", 0x24),
	EXYNOS8895_PIN_BANK_EINTG(8, 0x140, "gpd3", 0x28),
	EXYNOS8895_PIN_BANK_EINTG(7, 0x160, "gpd4", 0x2c),
	EXYNOS8895_PIN_BANK_EINTG(3, 0x1a0, "gpe0", 0x34),
	EXYNOS8895_PIN_BANK_EINTG(4, 0x1c0, "gpf0", 0x38),
	EXYNOS8895_PIN_BANK_EINTG(2, 0x1e0, "gpf1", 0x3c),
	EXYNOS8895_PIN_BANK_EINTG(2, 0x200, "gpf2", 0x40),
	EXYNOS8895_PIN_BANK_EINTG(4, 0x220, "gpf3", 0x44),
	EXYNOS8895_PIN_BANK_EINTG(5, 0x240, "gpf4", 0x48),
};

/* pin banks of exynos7870 pin-controller 7 (TOUCH) */
static const struct samsung_pin_bank_data exynos7870_pin_banks7[] __initconst = {
	EXYNOS8895_PIN_BANK_EINTG(3, 0x000, "gpc3", 0x00),
};

static const struct samsung_pin_ctrl exynos7870_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 Alive data */
		.pin_banks	= exynos7870_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos7870_pin_banks0),
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 DISPAUD data */
		.pin_banks	= exynos7870_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos7870_pin_banks1),
	}, {
		/* pin-controller instance 2 ESE data */
		.pin_banks	= exynos7870_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos7870_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 3 FSYS data */
		.pin_banks	= exynos7870_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos7870_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 4 MIF data */
		.pin_banks	= exynos7870_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos7870_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 5 NFC data */
		.pin_banks	= exynos7870_pin_banks5,
		.nr_banks	= ARRAY_SIZE(exynos7870_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 6 TOP data */
		.pin_banks	= exynos7870_pin_banks6,
		.nr_banks	= ARRAY_SIZE(exynos7870_pin_banks6),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 7 TOUCH data */
		.pin_banks	= exynos7870_pin_banks7,
		.nr_banks	= ARRAY_SIZE(exynos7870_pin_banks7),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

const struct samsung_pinctrl_of_match_data exynos7870_of_data __initconst = {
	.ctrl		= exynos7870_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos7870_pin_ctrl),
};

/* pin banks of exynos7885 pin-controller 0 (ALIVE) */
static const struct samsung_pin_bank_data exynos7885_pin_banks0[] __initconst = {
	EXYNOS_PIN_BANK_EINTN(3, 0x000, "etc0"),
	EXYNOS_PIN_BANK_EINTN(3, 0x020, "etc1"),
	EXYNOS850_PIN_BANK_EINTW(8, 0x040, "gpa0", 0x00),
	EXYNOS850_PIN_BANK_EINTW(8, 0x060, "gpa1", 0x04),
	EXYNOS850_PIN_BANK_EINTW(8, 0x080, "gpa2", 0x08),
	EXYNOS850_PIN_BANK_EINTW(5, 0x0a0, "gpq0", 0x0c),
};

/* pin banks of exynos7885 pin-controller 1 (DISPAUD) */
static const struct samsung_pin_bank_data exynos7885_pin_banks1[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(5, 0x000, "gpb0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(4, 0x020, "gpb1", 0x04),
	EXYNOS850_PIN_BANK_EINTG(5, 0x040, "gpb2", 0x08),
};

/* pin banks of exynos7885 pin-controller 2 (FSYS) */
static const struct samsung_pin_bank_data exynos7885_pin_banks2[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(4, 0x000, "gpf0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpf2", 0x04),
	EXYNOS850_PIN_BANK_EINTG(6, 0x040, "gpf3", 0x08),
	EXYNOS850_PIN_BANK_EINTG(6, 0x060, "gpf4", 0x0c),
};

/* pin banks of exynos7885 pin-controller 3 (TOP) */
static const struct samsung_pin_bank_data exynos7885_pin_banks3[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(4, 0x000, "gpp0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(3, 0x020, "gpg0", 0x04),
	EXYNOS850_PIN_BANK_EINTG(4, 0x040, "gpp1", 0x08),
	EXYNOS850_PIN_BANK_EINTG(4, 0x060, "gpp2", 0x0c),
	EXYNOS850_PIN_BANK_EINTG(3, 0x080, "gpp3", 0x10),
	EXYNOS850_PIN_BANK_EINTG(6, 0x0a0, "gpp4", 0x14),
	EXYNOS850_PIN_BANK_EINTG(4, 0x0c0, "gpp5", 0x18),
	EXYNOS850_PIN_BANK_EINTG(5, 0x0e0, "gpp6", 0x1c),
	EXYNOS850_PIN_BANK_EINTG(2, 0x100, "gpp7", 0x20),
	EXYNOS850_PIN_BANK_EINTG(2, 0x120, "gpp8", 0x24),
	EXYNOS850_PIN_BANK_EINTG(8, 0x140, "gpg1", 0x28),
	EXYNOS850_PIN_BANK_EINTG(8, 0x160, "gpg2", 0x2c),
	EXYNOS850_PIN_BANK_EINTG(8, 0x180, "gpg3", 0x30),
	EXYNOS850_PIN_BANK_EINTG(2, 0x1a0, "gpg4", 0x34),
	EXYNOS850_PIN_BANK_EINTG(4, 0x1c0, "gpc0", 0x38),
	EXYNOS850_PIN_BANK_EINTG(8, 0x1e0, "gpc1", 0x3c),
	EXYNOS850_PIN_BANK_EINTG(8, 0x200, "gpc2", 0x40),
};

static const struct samsung_pin_ctrl exynos7885_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 Alive data */
		.pin_banks	= exynos7885_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos7885_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 DISPAUD data */
		.pin_banks	= exynos7885_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos7885_pin_banks1),
	}, {
		/* pin-controller instance 2 FSYS data */
		.pin_banks	= exynos7885_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos7885_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 3 TOP data */
		.pin_banks	= exynos7885_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos7885_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

const struct samsung_pinctrl_of_match_data exynos7885_of_data __initconst = {
	.ctrl		= exynos7885_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos7885_pin_ctrl),
};

/* pin banks of exynos850 pin-controller 0 (ALIVE) */
static const struct samsung_pin_bank_data exynos850_pin_banks0[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTW(8, 0x000, "gpa0", 0x00),
	EXYNOS850_PIN_BANK_EINTW(8, 0x020, "gpa1", 0x04),
	EXYNOS850_PIN_BANK_EINTW(8, 0x040, "gpa2", 0x08),
	EXYNOS850_PIN_BANK_EINTW(8, 0x060, "gpa3", 0x0c),
	EXYNOS850_PIN_BANK_EINTW(4, 0x080, "gpa4", 0x10),
	EXYNOS850_PIN_BANK_EINTN(3, 0x0a0, "gpq0"),
};

/* pin banks of exynos850 pin-controller 1 (CMGP) */
static const struct samsung_pin_bank_data exynos850_pin_banks1[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTW(1, 0x000, "gpm0", 0x00),
	EXYNOS850_PIN_BANK_EINTW(1, 0x020, "gpm1", 0x04),
	EXYNOS850_PIN_BANK_EINTW(1, 0x040, "gpm2", 0x08),
	EXYNOS850_PIN_BANK_EINTW(1, 0x060, "gpm3", 0x0c),
	EXYNOS850_PIN_BANK_EINTW(1, 0x080, "gpm4", 0x10),
	EXYNOS850_PIN_BANK_EINTW(1, 0x0a0, "gpm5", 0x14),
	EXYNOS850_PIN_BANK_EINTW(1, 0x0c0, "gpm6", 0x18),
	EXYNOS850_PIN_BANK_EINTW(1, 0x0e0, "gpm7", 0x1c),
};

/* pin banks of exynos850 pin-controller 2 (AUD) */
static const struct samsung_pin_bank_data exynos850_pin_banks2[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTG(5, 0x000, "gpb0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(5, 0x020, "gpb1", 0x04),
};

/* pin banks of exynos850 pin-controller 3 (HSI) */
static const struct samsung_pin_bank_data exynos850_pin_banks3[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTG(6, 0x000, "gpf2", 0x00),
};

/* pin banks of exynos850 pin-controller 4 (CORE) */
static const struct samsung_pin_bank_data exynos850_pin_banks4[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTG(4, 0x000, "gpf0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpf1", 0x04),
};

/* pin banks of exynos850 pin-controller 5 (PERI) */
static const struct samsung_pin_bank_data exynos850_pin_banks5[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTG(2, 0x000, "gpg0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(6, 0x020, "gpp0", 0x04),
	EXYNOS850_PIN_BANK_EINTG(4, 0x040, "gpp1", 0x08),
	EXYNOS850_PIN_BANK_EINTG(4, 0x060, "gpp2", 0x0c),
	EXYNOS850_PIN_BANK_EINTG(8, 0x080, "gpg1", 0x10),
	EXYNOS850_PIN_BANK_EINTG(8, 0x0a0, "gpg2", 0x14),
	EXYNOS850_PIN_BANK_EINTG(1, 0x0c0, "gpg3", 0x18),
	EXYNOS850_PIN_BANK_EINTG(3, 0x0e0, "gpc0", 0x1c),
	EXYNOS850_PIN_BANK_EINTG(6, 0x100, "gpc1", 0x20),
};

static const struct samsung_pin_ctrl exynos850_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 ALIVE data */
		.pin_banks	= exynos850_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos850_pin_banks0),
		.eint_wkup_init = exynos_eint_wkup_init,
	}, {
		/* pin-controller instance 1 CMGP data */
		.pin_banks	= exynos850_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos850_pin_banks1),
		.eint_wkup_init = exynos_eint_wkup_init,
	}, {
		/* pin-controller instance 2 AUD data */
		.pin_banks	= exynos850_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos850_pin_banks2),
	}, {
		/* pin-controller instance 3 HSI data */
		.pin_banks	= exynos850_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos850_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 4 CORE data */
		.pin_banks	= exynos850_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos850_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 5 PERI data */
		.pin_banks	= exynos850_pin_banks5,
		.nr_banks	= ARRAY_SIZE(exynos850_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
	},
};

const struct samsung_pinctrl_of_match_data exynos850_of_data __initconst = {
	.ctrl		= exynos850_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos850_pin_ctrl),
};

/* pin banks of exynos990 pin-controller 0 (ALIVE) */
static struct samsung_pin_bank_data exynos990_pin_banks0[] = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTW(8, 0x000, "gpa0", 0x00),
	EXYNOS850_PIN_BANK_EINTW(8, 0x020, "gpa1", 0x04),
	EXYNOS850_PIN_BANK_EINTW(8, 0x040, "gpa2", 0x08),
	EXYNOS850_PIN_BANK_EINTW(8, 0x060, "gpa3", 0x0c),
	EXYNOS850_PIN_BANK_EINTW(2, 0x080, "gpa4", 0x10),
	EXYNOS850_PIN_BANK_EINTN(7, 0x0A0, "gpq0"),
};

/* pin banks of exynos990 pin-controller 1 (CMGP) */
static struct samsung_pin_bank_data exynos990_pin_banks1[] = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTN(1, 0x000, "gpm0"),
	EXYNOS850_PIN_BANK_EINTN(1, 0x020, "gpm1"),
	EXYNOS850_PIN_BANK_EINTN(1, 0x040, "gpm2"),
	EXYNOS850_PIN_BANK_EINTN(1, 0x060, "gpm3"),
	EXYNOS850_PIN_BANK_EINTW(1, 0x080, "gpm4", 0x00),
	EXYNOS850_PIN_BANK_EINTW(1, 0x0A0, "gpm5", 0x04),
	EXYNOS850_PIN_BANK_EINTW(1, 0x0C0, "gpm6", 0x08),
	EXYNOS850_PIN_BANK_EINTW(1, 0x0E0, "gpm7", 0x0c),
	EXYNOS850_PIN_BANK_EINTW(1, 0x100, "gpm8", 0x10),
	EXYNOS850_PIN_BANK_EINTW(1, 0x120, "gpm9", 0x14),
	EXYNOS850_PIN_BANK_EINTW(1, 0x140, "gpm10", 0x18),
	EXYNOS850_PIN_BANK_EINTW(1, 0x160, "gpm11", 0x1c),
	EXYNOS850_PIN_BANK_EINTW(1, 0x180, "gpm12", 0x20),
	EXYNOS850_PIN_BANK_EINTW(1, 0x1A0, "gpm13", 0x24),
	EXYNOS850_PIN_BANK_EINTW(1, 0x1C0, "gpm14", 0x28),
	EXYNOS850_PIN_BANK_EINTW(1, 0x1E0, "gpm15", 0x2c),
	EXYNOS850_PIN_BANK_EINTW(1, 0x200, "gpm16", 0x30),
	EXYNOS850_PIN_BANK_EINTW(1, 0x220, "gpm17", 0x34),
	EXYNOS850_PIN_BANK_EINTW(1, 0x240, "gpm18", 0x38),
	EXYNOS850_PIN_BANK_EINTW(1, 0x260, "gpm19", 0x3c),
	EXYNOS850_PIN_BANK_EINTW(1, 0x280, "gpm20", 0x40),
	EXYNOS850_PIN_BANK_EINTW(1, 0x2A0, "gpm21", 0x44),
	EXYNOS850_PIN_BANK_EINTW(1, 0x2C0, "gpm22", 0x48),
	EXYNOS850_PIN_BANK_EINTW(1, 0x2E0, "gpm23", 0x4c),
	EXYNOS850_PIN_BANK_EINTW(1, 0x300, "gpm24", 0x50),
	EXYNOS850_PIN_BANK_EINTW(1, 0x320, "gpm25", 0x54),
	EXYNOS850_PIN_BANK_EINTW(1, 0x340, "gpm26", 0x58),
	EXYNOS850_PIN_BANK_EINTW(1, 0x360, "gpm27", 0x5c),
	EXYNOS850_PIN_BANK_EINTW(1, 0x380, "gpm28", 0x60),
	EXYNOS850_PIN_BANK_EINTW(1, 0x3A0, "gpm29", 0x64),
	EXYNOS850_PIN_BANK_EINTW(1, 0x3C0, "gpm30", 0x68),
	EXYNOS850_PIN_BANK_EINTW(1, 0x3E0, "gpm31", 0x6c),
	EXYNOS850_PIN_BANK_EINTW(1, 0x400, "gpm32", 0x70),
	EXYNOS850_PIN_BANK_EINTW(1, 0x420, "gpm33", 0x74),

};

/* pin banks of exynos990 pin-controller 2 (HSI1) */
static struct samsung_pin_bank_data exynos990_pin_banks2[] = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTG(4, 0x000, "gpf0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(6, 0x020, "gpf1", 0x04),
	EXYNOS850_PIN_BANK_EINTG(3, 0x040, "gpf2", 0x08),
};

/* pin banks of exynos990 pin-controller 3 (HSI2) */
static struct samsung_pin_bank_data exynos990_pin_banks3[] = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTG(2, 0x000, "gpf3", 0x00),
};

/* pin banks of exynos990 pin-controller 4 (PERIC0) */
static struct samsung_pin_bank_data exynos990_pin_banks4[] = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTG(8, 0x000, "gpp0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpp1", 0x04),
	EXYNOS850_PIN_BANK_EINTG(8, 0x040, "gpp2", 0x08),
	EXYNOS850_PIN_BANK_EINTG(8, 0x060, "gpp3", 0x0C),
	EXYNOS850_PIN_BANK_EINTG(8, 0x080, "gpp4", 0x10),
	EXYNOS850_PIN_BANK_EINTG(2, 0x0A0, "gpg0", 0x14),
};

/* pin banks of exynos990 pin-controller 5 (PERIC1) */
static struct samsung_pin_bank_data exynos990_pin_banks5[] = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTG(8, 0x000, "gpp5", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpp6", 0x04),
	EXYNOS850_PIN_BANK_EINTG(8, 0x040, "gpp7", 0x08),
	EXYNOS850_PIN_BANK_EINTG(8, 0x060, "gpp8", 0x0C),
	EXYNOS850_PIN_BANK_EINTG(8, 0x080, "gpp9", 0x10),
	EXYNOS850_PIN_BANK_EINTG(6, 0x0A0, "gpc0", 0x14),
	EXYNOS850_PIN_BANK_EINTG(4, 0x0C0, "gpg1", 0x18),
	EXYNOS850_PIN_BANK_EINTG(8, 0x0E0, "gpb0", 0x1C),
	EXYNOS850_PIN_BANK_EINTG(8, 0x100, "gpb1", 0x20),
	EXYNOS850_PIN_BANK_EINTG(8, 0x120, "gpb2", 0x24),
};

/* pin banks of exynos990 pin-controller 6 (VTS) */
static struct samsung_pin_bank_data exynos990_pin_banks6[] = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS850_PIN_BANK_EINTG(7, 0x000, "gpv0", 0x00),
};

static const struct samsung_pin_ctrl exynos990_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 ALIVE data */
		.pin_banks	= exynos990_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos990_pin_banks0),
		.eint_wkup_init = exynos_eint_wkup_init,
	}, {
		/* pin-controller instance 1 CMGP data */
		.pin_banks	= exynos990_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos990_pin_banks1),
		.eint_wkup_init = exynos_eint_wkup_init,
	}, {
		/* pin-controller instance 2 HSI1 data */
		.pin_banks	= exynos990_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos990_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 3 HSI2 data */
		.pin_banks	= exynos990_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos990_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 4 PERIC0 data */
		.pin_banks	= exynos990_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos990_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 5 PERIC1 data */
		.pin_banks	= exynos990_pin_banks5,
		.nr_banks	= ARRAY_SIZE(exynos990_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 6 VTS data */
		.pin_banks	= exynos990_pin_banks6,
		.nr_banks	= ARRAY_SIZE(exynos990_pin_banks6),
	},
};

const struct samsung_pinctrl_of_match_data exynos990_of_data __initconst = {
	.ctrl		= exynos990_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos990_pin_ctrl),
};

/* pin banks of exynos9810 pin-controller 0 (ALIVE) */
static const struct samsung_pin_bank_data exynos9810_pin_banks0[] __initconst = {
	EXYNOS850_PIN_BANK_EINTN(6, 0x000, "etc1"),
	EXYNOS850_PIN_BANK_EINTW(8, 0x020, "gpa0", 0x00),
	EXYNOS850_PIN_BANK_EINTW(8, 0x040, "gpa1", 0x04),
	EXYNOS850_PIN_BANK_EINTW(8, 0x060, "gpa2", 0x08),
	EXYNOS850_PIN_BANK_EINTW(8, 0x080, "gpa3", 0x0c),
	EXYNOS850_PIN_BANK_EINTN(6, 0x0A0, "gpq0"),
	EXYNOS850_PIN_BANK_EINTW(2, 0x0C0, "gpa4", 0x10),
};

/* pin banks of exynos9810 pin-controller 1 (AUD) */
static const struct samsung_pin_bank_data exynos9810_pin_banks1[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(5, 0x000, "gpb0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpb1", 0x04),
	EXYNOS850_PIN_BANK_EINTG(4, 0x040, "gpb2", 0x08),
};

/* pin banks of exynos9810 pin-controller 2 (CHUB) */
static const struct samsung_pin_bank_data exynos9810_pin_banks2[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(8, 0x000, "gph0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(5, 0x020, "gph1", 0x04),
};

/* pin banks of exynos9810 pin-controller 3 (CMGP) */
static const struct samsung_pin_bank_data exynos9810_pin_banks3[] __initconst = {
	EXYNOS850_PIN_BANK_EINTW(1, 0x000, "gpm0", 0x00),
	EXYNOS850_PIN_BANK_EINTW(1, 0x020, "gpm1", 0x04),
	EXYNOS850_PIN_BANK_EINTW(1, 0x040, "gpm2", 0x08),
	EXYNOS850_PIN_BANK_EINTW(1, 0x060, "gpm3", 0x0C),
	EXYNOS850_PIN_BANK_EINTW(1, 0x080, "gpm4", 0x10),
	EXYNOS850_PIN_BANK_EINTW(1, 0x0A0, "gpm5", 0x14),
	EXYNOS850_PIN_BANK_EINTW(1, 0x0C0, "gpm6", 0x18),
	EXYNOS850_PIN_BANK_EINTW(1, 0x0E0, "gpm7", 0x1C),
	EXYNOS850_PIN_BANK_EINTW(1, 0x100, "gpm10", 0x20),
	EXYNOS850_PIN_BANK_EINTW(1, 0x120, "gpm11", 0x24),
	EXYNOS850_PIN_BANK_EINTW(1, 0x140, "gpm12", 0x28),
	EXYNOS850_PIN_BANK_EINTW(1, 0x160, "gpm13", 0x2C),
	EXYNOS850_PIN_BANK_EINTW(1, 0x180, "gpm14", 0x30),
	EXYNOS850_PIN_BANK_EINTW(1, 0x1A0, "gpm15", 0x34),
	EXYNOS850_PIN_BANK_EINTW(1, 0x1C0, "gpm16", 0x38),
	EXYNOS850_PIN_BANK_EINTW(1, 0x1E0, "gpm17", 0x3C),
	EXYNOS850_PIN_BANK_EINTW(1, 0x200, "gpm40", 0x40),
	EXYNOS850_PIN_BANK_EINTW(1, 0x220, "gpm41", 0x44),
	EXYNOS850_PIN_BANK_EINTW(1, 0x240, "gpm42", 0x48),
	EXYNOS850_PIN_BANK_EINTW(1, 0x260, "gpm43", 0x4C),
};

/* pin banks of exynos9810 pin-controller 4 (FSYS0) */
static const struct samsung_pin_bank_data exynos9810_pin_banks4[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(2, 0x000, "gpf0", 0x00),
};

/* pin banks of exynos9810 pin-controller 5 (FSYS1) */
static const struct samsung_pin_bank_data exynos9810_pin_banks5[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(7, 0x000, "gpf1", 0x00),
	EXYNOS850_PIN_BANK_EINTG(6, 0x020, "gpf2", 0x04),
};

/* pin banks of exynos9810 pin-controller 6 (PERIC0) */
static const struct samsung_pin_bank_data exynos9810_pin_banks6[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(8, 0x000, "gpp0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpp1", 0x04),
	EXYNOS850_PIN_BANK_EINTG(8, 0x040, "gpp2", 0x08),
	EXYNOS850_PIN_BANK_EINTG(4, 0x060, "gpp3", 0x0C),
	EXYNOS850_PIN_BANK_EINTG(8, 0x080, "gpg0", 0x10),
	EXYNOS850_PIN_BANK_EINTG(8, 0x0A0, "gpg1", 0x14),
	EXYNOS850_PIN_BANK_EINTG(8, 0x0C0, "gpg2", 0x18),
};

/* pin banks of exynos9810 pin-controller 7 (PERIC1) */
static const struct samsung_pin_bank_data exynos9810_pin_banks7[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(8, 0x000, "gpp4", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpp5", 0x04),
	EXYNOS850_PIN_BANK_EINTG(4, 0x040, "gpp6", 0x08),
	EXYNOS850_PIN_BANK_EINTG(8, 0x060, "gpc0", 0x0C),
	EXYNOS850_PIN_BANK_EINTG(8, 0x080, "gpc1", 0x10),
	EXYNOS850_PIN_BANK_EINTG(4, 0x0A0, "gpd0", 0x14),
	EXYNOS850_PIN_BANK_EINTG(7, 0x0C0, "gpg3", 0x18),
};

/* pin banks of exynos9810 pin-controller 8 (VTS) */
static const struct samsung_pin_bank_data exynos9810_pin_banks8[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(3, 0x000, "gpt0", 0x00),
};

static const struct samsung_pin_ctrl exynos9810_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 ALIVE data */
		.pin_banks      = exynos9810_pin_banks0,
		.nr_banks       = ARRAY_SIZE(exynos9810_pin_banks0),
		.eint_wkup_init = exynos_eint_wkup_init,
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 AUD data */
		.pin_banks      = exynos9810_pin_banks1,
		.nr_banks       = ARRAY_SIZE(exynos9810_pin_banks1),
	}, {
		/* pin-controller instance 2 CHUB data */
		.pin_banks      = exynos9810_pin_banks2,
		.nr_banks       = ARRAY_SIZE(exynos9810_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 3 CMGP data */
		.pin_banks      = exynos9810_pin_banks3,
		.nr_banks       = ARRAY_SIZE(exynos9810_pin_banks3),
		.eint_wkup_init = exynos_eint_wkup_init,
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 4 FSYS0 data */
		.pin_banks      = exynos9810_pin_banks4,
		.nr_banks       = ARRAY_SIZE(exynos9810_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 5 FSYS1 data */
		.pin_banks      = exynos9810_pin_banks5,
		.nr_banks       = ARRAY_SIZE(exynos9810_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 6 PERIC0 data */
		.pin_banks      = exynos9810_pin_banks6,
		.nr_banks       = ARRAY_SIZE(exynos9810_pin_banks6),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 7 PERIC1 data */
		.pin_banks      = exynos9810_pin_banks7,
		.nr_banks       = ARRAY_SIZE(exynos9810_pin_banks7),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 8 VTS data */
		.pin_banks      = exynos9810_pin_banks8,
		.nr_banks       = ARRAY_SIZE(exynos9810_pin_banks8),
	},
};

const struct samsung_pinctrl_of_match_data exynos9810_of_data __initconst = {
	.ctrl		= exynos9810_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos9810_pin_ctrl),
};

/* pin banks of exynosautov9 pin-controller 0 (ALIVE) */
static const struct samsung_pin_bank_data exynosautov9_pin_banks0[] __initconst = {
	EXYNOS850_PIN_BANK_EINTW(8, 0x000, "gpa0", 0x00),
	EXYNOS850_PIN_BANK_EINTW(2, 0x020, "gpa1", 0x04),
	EXYNOS850_PIN_BANK_EINTN(2, 0x040, "gpq0"),
};

/* pin banks of exynosautov9 pin-controller 1 (AUD) */
static const struct samsung_pin_bank_data exynosautov9_pin_banks1[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(5, 0x000, "gpb0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpb1", 0x04),
	EXYNOS850_PIN_BANK_EINTG(8, 0x040, "gpb2", 0x08),
	EXYNOS850_PIN_BANK_EINTG(8, 0x060, "gpb3", 0x0C),
};

/* pin banks of exynosautov9 pin-controller 2 (FSYS0) */
static const struct samsung_pin_bank_data exynosautov9_pin_banks2[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(6, 0x000, "gpf0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(6, 0x020, "gpf1", 0x04),
};

/* pin banks of exynosautov9 pin-controller 3 (FSYS1) */
static const struct samsung_pin_bank_data exynosautov9_pin_banks3[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(6, 0x000, "gpf8", 0x00),
};

/* pin banks of exynosautov9 pin-controller 4 (FSYS2) */
static const struct samsung_pin_bank_data exynosautov9_pin_banks4[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(4, 0x000, "gpf2", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpf3", 0x04),
	EXYNOS850_PIN_BANK_EINTG(7, 0x040, "gpf4", 0x08),
	EXYNOS850_PIN_BANK_EINTG(8, 0x060, "gpf5", 0x0C),
	EXYNOS850_PIN_BANK_EINTG(7, 0x080, "gpf6", 0x10),
};

/* pin banks of exynosautov9 pin-controller 5 (PERIC0) */
static const struct samsung_pin_bank_data exynosautov9_pin_banks5[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(8, 0x000, "gpp0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpp1", 0x04),
	EXYNOS850_PIN_BANK_EINTG(8, 0x040, "gpp2", 0x08),
	EXYNOS850_PIN_BANK_EINTG(5, 0x060, "gpg0", 0x0C),
};

/* pin banks of exynosautov9 pin-controller 6 (PERIC1) */
static const struct samsung_pin_bank_data exynosautov9_pin_banks6[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(8, 0x000, "gpp3", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x020, "gpp4", 0x04),
	EXYNOS850_PIN_BANK_EINTG(8, 0x040, "gpp5", 0x08),
	EXYNOS850_PIN_BANK_EINTG(8, 0x060, "gpg1", 0x0C),
	EXYNOS850_PIN_BANK_EINTG(8, 0x080, "gpg2", 0x10),
	EXYNOS850_PIN_BANK_EINTG(4, 0x0A0, "gpg3", 0x14),
};

static const struct samsung_pin_ctrl exynosautov9_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 ALIVE data */
		.pin_banks      = exynosautov9_pin_banks0,
		.nr_banks       = ARRAY_SIZE(exynosautov9_pin_banks0),
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 AUD data */
		.pin_banks      = exynosautov9_pin_banks1,
		.nr_banks       = ARRAY_SIZE(exynosautov9_pin_banks1),
	}, {
		/* pin-controller instance 2 FSYS0 data */
		.pin_banks      = exynosautov9_pin_banks2,
		.nr_banks       = ARRAY_SIZE(exynosautov9_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 3 FSYS1 data */
		.pin_banks      = exynosautov9_pin_banks3,
		.nr_banks       = ARRAY_SIZE(exynosautov9_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 4 FSYS2 data */
		.pin_banks      = exynosautov9_pin_banks4,
		.nr_banks       = ARRAY_SIZE(exynosautov9_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 5 PERIC0 data */
		.pin_banks      = exynosautov9_pin_banks5,
		.nr_banks       = ARRAY_SIZE(exynosautov9_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 6 PERIC1 data */
		.pin_banks      = exynosautov9_pin_banks6,
		.nr_banks       = ARRAY_SIZE(exynosautov9_pin_banks6),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend        = exynos_pinctrl_suspend,
		.resume         = exynos_pinctrl_resume,
	},
};

const struct samsung_pinctrl_of_match_data exynosautov9_of_data __initconst = {
	.ctrl		= exynosautov9_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynosautov9_pin_ctrl),
};

/* pin banks of exynosautov920 pin-controller 0 (ALIVE) */
static const struct samsung_pin_bank_data exynosautov920_pin_banks0[] = {
	EXYNOSV920_PIN_BANK_EINTW(8, 0x0000, "gpa0", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTW(2, 0x1000, "gpa1", 0x18, 0x20, 0x24),
	EXYNOS850_PIN_BANK_EINTN(2, 0x2000, "gpq0"),
};

/* pin banks of exynosautov920 pin-controller 1 (AUD) */
static const struct samsung_pin_bank_data exynosautov920_pin_banks1[] = {
	EXYNOSV920_PIN_BANK_EINTG(7, 0x0000, "gpb0", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(6, 0x1000, "gpb1", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(8, 0x2000, "gpb2", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(8, 0x3000, "gpb3", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(8, 0x4000, "gpb4", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(5, 0x5000, "gpb5", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(5, 0x6000, "gpb6", 0x18, 0x24, 0x28),
};

/* pin banks of exynosautov920 pin-controller 2 (HSI0) */
static const struct samsung_pin_bank_data exynosautov920_pin_banks2[] = {
	EXYNOSV920_PIN_BANK_EINTG(6, 0x0000, "gph0", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(2, 0x1000, "gph1", 0x18, 0x20, 0x24),
};

/* pin banks of exynosautov920 pin-controller 3 (HSI1) */
static const struct samsung_pin_bank_data exynosautov920_pin_banks3[] = {
	EXYNOSV920_PIN_BANK_EINTG(7, 0x000, "gph8", 0x18, 0x24, 0x28),
};

/* pin banks of exynosautov920 pin-controller 4 (HSI2) */
static const struct samsung_pin_bank_data exynosautov920_pin_banks4[] = {
	EXYNOSV920_PIN_BANK_EINTG(8, 0x0000, "gph3", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(7, 0x1000, "gph4", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(8, 0x2000, "gph5", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(7, 0x3000, "gph6", 0x18, 0x24, 0x28),
};

/* pin banks of exynosautov920 pin-controller 5 (HSI2UFS) */
static const struct samsung_pin_bank_data exynosautov920_pin_banks5[] = {
	EXYNOSV920_PIN_BANK_EINTG(4, 0x000, "gph2", 0x18, 0x20, 0x24),
};

/* pin banks of exynosautov920 pin-controller 6 (PERIC0) */
static const struct samsung_pin_bank_data exynosautov920_pin_banks6[] = {
	EXYNOSV920_PIN_BANK_EINTG(8, 0x0000, "gpp0", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(8, 0x1000, "gpp1", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(8, 0x2000, "gpp2", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(5, 0x3000, "gpg0", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(8, 0x4000, "gpp3", 0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(4, 0x5000, "gpp4", 0x18, 0x20, 0x24),
	EXYNOSV920_PIN_BANK_EINTG(4, 0x6000, "gpg2", 0x18, 0x20, 0x24),
	EXYNOSV920_PIN_BANK_EINTG(4, 0x7000, "gpg5", 0x18, 0x20, 0x24),
	EXYNOSV920_PIN_BANK_EINTG(3, 0x8000, "gpg3", 0x18, 0x20, 0x24),
	EXYNOSV920_PIN_BANK_EINTG(5, 0x9000, "gpg4", 0x18, 0x24, 0x28),
};

/* pin banks of exynosautov920 pin-controller 7 (PERIC1) */
static const struct samsung_pin_bank_data exynosautov920_pin_banks7[] = {
	EXYNOSV920_PIN_BANK_EINTG(8, 0x0000, "gpp5",  0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(5, 0x1000, "gpp6",  0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(4, 0x2000, "gpp10", 0x18, 0x20, 0x24),
	EXYNOSV920_PIN_BANK_EINTG(8, 0x3000, "gpp7",  0x18, 0x24, 0x28),
	EXYNOSV920_PIN_BANK_EINTG(4, 0x4000, "gpp8",  0x18, 0x20, 0x24),
	EXYNOSV920_PIN_BANK_EINTG(4, 0x5000, "gpp11", 0x18, 0x20, 0x24),
	EXYNOSV920_PIN_BANK_EINTG(4, 0x6000, "gpp9",  0x18, 0x20, 0x24),
	EXYNOSV920_PIN_BANK_EINTG(4, 0x7000, "gpp12", 0x18, 0x20, 0x24),
	EXYNOSV920_PIN_BANK_EINTG(8, 0x8000, "gpg1",  0x18, 0x24, 0x28),
};

static const struct samsung_retention_data exynosautov920_retention_data __initconst = {
	.regs	 = NULL,
	.nr_regs = 0,
	.value	 = 0,
	.refcnt	 = &exynos_shared_retention_refcnt,
	.init	 = exynos_retention_init,
};

static const struct samsung_pin_ctrl exynosautov920_pin_ctrl[] = {
	{
		/* pin-controller instance 0 ALIVE data */
		.pin_banks	= exynosautov920_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynosautov920_pin_banks0),
		.eint_wkup_init	= exynos_eint_wkup_init,
		.suspend	= exynosautov920_pinctrl_suspend,
		.resume		= exynosautov920_pinctrl_resume,
		.retention_data	= &exynosautov920_retention_data,
	}, {
		/* pin-controller instance 1 AUD data */
		.pin_banks	= exynosautov920_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynosautov920_pin_banks1),
	}, {
		/* pin-controller instance 2 HSI0 data */
		.pin_banks	= exynosautov920_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynosautov920_pin_banks2),
		.eint_gpio_init	= exynos_eint_gpio_init,
		.suspend	= exynosautov920_pinctrl_suspend,
		.resume		= exynosautov920_pinctrl_resume,
	}, {
		/* pin-controller instance 3 HSI1 data */
		.pin_banks	= exynosautov920_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynosautov920_pin_banks3),
		.eint_gpio_init	= exynos_eint_gpio_init,
		.suspend	= exynosautov920_pinctrl_suspend,
		.resume		= exynosautov920_pinctrl_resume,
	}, {
		/* pin-controller instance 4 HSI2 data */
		.pin_banks	= exynosautov920_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynosautov920_pin_banks4),
		.eint_gpio_init	= exynos_eint_gpio_init,
		.suspend	= exynosautov920_pinctrl_suspend,
		.resume		= exynosautov920_pinctrl_resume,
	}, {
		/* pin-controller instance 5 HSI2UFS data */
		.pin_banks	= exynosautov920_pin_banks5,
		.nr_banks	= ARRAY_SIZE(exynosautov920_pin_banks5),
		.eint_gpio_init	= exynos_eint_gpio_init,
		.suspend	= exynosautov920_pinctrl_suspend,
		.resume		= exynosautov920_pinctrl_resume,
	}, {
		/* pin-controller instance 6 PERIC0 data */
		.pin_banks	= exynosautov920_pin_banks6,
		.nr_banks	= ARRAY_SIZE(exynosautov920_pin_banks6),
		.eint_gpio_init	= exynos_eint_gpio_init,
		.suspend	= exynosautov920_pinctrl_suspend,
		.resume		= exynosautov920_pinctrl_resume,
	}, {
		/* pin-controller instance 7 PERIC1 data */
		.pin_banks	= exynosautov920_pin_banks7,
		.nr_banks	= ARRAY_SIZE(exynosautov920_pin_banks7),
		.eint_gpio_init	= exynos_eint_gpio_init,
		.suspend	= exynosautov920_pinctrl_suspend,
		.resume		= exynosautov920_pinctrl_resume,
	},
};

const struct samsung_pinctrl_of_match_data exynosautov920_of_data __initconst = {
	.ctrl		= exynosautov920_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynosautov920_pin_ctrl),
};

/* pin banks of exynos8895 pin-controller 0 (ALIVE) */
static const struct samsung_pin_bank_data exynos8895_pin_banks0[] __initconst = {
	EXYNOS_PIN_BANK_EINTW(8, 0x020, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0x040, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0x060, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0x080, "gpa3", 0x0c),
	EXYNOS_PIN_BANK_EINTW(7, 0x0a0, "gpa4", 0x24),
};

/* pin banks of exynos8895 pin-controller 1 (ABOX) */
static const struct samsung_pin_bank_data exynos8895_pin_banks1[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gph0", 0x00),
	EXYNOS_PIN_BANK_EINTG(7, 0x020, "gph1", 0x04),
	EXYNOS_PIN_BANK_EINTG(4, 0x040, "gph3", 0x08),
};

/* pin banks of exynos8895 pin-controller 2 (VTS) */
static const struct samsung_pin_bank_data exynos8895_pin_banks2[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(3, 0x000, "gph2", 0x00),
};

/* pin banks of exynos8895 pin-controller 3 (FSYS0) */
static const struct samsung_pin_bank_data exynos8895_pin_banks3[] __initconst = {
	EXYNOS8895_PIN_BANK_EINTG(3, 0x000, "gpi0", 0x00),
	EXYNOS8895_PIN_BANK_EINTG(8, 0x020, "gpi1", 0x04),
};

/* pin banks of exynos8895 pin-controller 4 (FSYS1) */
static const struct samsung_pin_bank_data exynos8895_pin_banks4[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpj1", 0x00),
	EXYNOS_PIN_BANK_EINTG(7, 0x020, "gpj0", 0x04),
};

/* pin banks of exynos8895 pin-controller 5 (BUSC) */
static const struct samsung_pin_bank_data exynos8895_pin_banks5[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(2, 0x000, "gpb2", 0x00),
};

/* pin banks of exynos8895 pin-controller 6 (PERIC0) */
static const struct samsung_pin_bank_data exynos8895_pin_banks6[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpd0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpd1", 0x04),
	EXYNOS_PIN_BANK_EINTG(4, 0x040, "gpd2", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpd3", 0x0C),
	EXYNOS_PIN_BANK_EINTG(4, 0x080, "gpb1", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0a0, "gpe7", 0x14),
	EXYNOS_PIN_BANK_EINTG(8, 0x0c0, "gpf1", 0x18),
};

/* pin banks of exynos8895 pin-controller 7 (PERIC1) */
static const struct samsung_pin_bank_data exynos8895_pin_banks7[] __initconst = {
	EXYNOS_PIN_BANK_EINTG(3, 0x000, "gpb0", 0x00),
	EXYNOS_PIN_BANK_EINTG(5, 0x020, "gpc0", 0x04),
	EXYNOS_PIN_BANK_EINTG(5, 0x040, "gpc1", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpc2", 0x0C),
	EXYNOS_PIN_BANK_EINTG(8, 0x080, "gpc3", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0a0, "gpk0", 0x14),
	EXYNOS_PIN_BANK_EINTG(8, 0x0c0, "gpe5", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x0e0, "gpe6", 0x1C),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gpe2", 0x20),
	EXYNOS_PIN_BANK_EINTG(8, 0x120, "gpe3", 0x24),
	EXYNOS_PIN_BANK_EINTG(8, 0x140, "gpe4", 0x28),
	EXYNOS_PIN_BANK_EINTG(4, 0x160, "gpf0", 0x2C),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpe1", 0x30),
	EXYNOS_PIN_BANK_EINTG(2, 0x1a0, "gpg0", 0x34),
};

static const struct samsung_pin_ctrl exynos8895_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 ALIVE data */
		.pin_banks	= exynos8895_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos8895_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 ABOX data */
		.pin_banks	= exynos8895_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos8895_pin_banks1),
	}, {
		/* pin-controller instance 2 VTS data */
		.pin_banks	= exynos8895_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos8895_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 3 FSYS0 data */
		.pin_banks	= exynos8895_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos8895_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 4 FSYS1 data */
		.pin_banks	= exynos8895_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos8895_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 5 BUSC data */
		.pin_banks	= exynos8895_pin_banks5,
		.nr_banks	= ARRAY_SIZE(exynos8895_pin_banks5),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 6 PERIC0 data */
		.pin_banks	= exynos8895_pin_banks6,
		.nr_banks	= ARRAY_SIZE(exynos8895_pin_banks6),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 7 PERIC1 data */
		.pin_banks	= exynos8895_pin_banks7,
		.nr_banks	= ARRAY_SIZE(exynos8895_pin_banks7),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

const struct samsung_pinctrl_of_match_data exynos8895_of_data __initconst = {
	.ctrl		= exynos8895_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos8895_pin_ctrl),
};

/*
 * Pinctrl driver data for Tesla FSD SoC. FSD SoC includes three
 * gpio/pin-mux/pinconfig controllers.
 */

/* pin banks of FSD pin-controller 0 (FSYS) */
static const struct samsung_pin_bank_data fsd_pin_banks0[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(7, 0x00, "gpf0", 0x00),
	EXYNOS850_PIN_BANK_EINTG(8, 0x20, "gpf1", 0x04),
	EXYNOS850_PIN_BANK_EINTG(3, 0x40, "gpf6", 0x08),
	EXYNOS850_PIN_BANK_EINTG(2, 0x60, "gpf4", 0x0c),
	EXYNOS850_PIN_BANK_EINTG(6, 0x80, "gpf5", 0x10),
};

/* pin banks of FSD pin-controller 1 (PERIC) */
static const struct samsung_pin_bank_data fsd_pin_banks1[] __initconst = {
	EXYNOS850_PIN_BANK_EINTG(4, 0x000, "gpc8", 0x00),
	EXYNOS850_PIN_BANK_EINTG(7, 0x020, "gpf2", 0x04),
	EXYNOS850_PIN_BANK_EINTG(8, 0x040, "gpf3", 0x08),
	EXYNOS850_PIN_BANK_EINTG(8, 0x060, "gpd0", 0x0c),
	EXYNOS850_PIN_BANK_EINTG(8, 0x080, "gpb0", 0x10),
	EXYNOS850_PIN_BANK_EINTG(8, 0x0a0, "gpb1", 0x14),
	EXYNOS850_PIN_BANK_EINTG(8, 0x0c0, "gpb4", 0x18),
	EXYNOS850_PIN_BANK_EINTG(4, 0x0e0, "gpb5", 0x1c),
	EXYNOS850_PIN_BANK_EINTG(8, 0x100, "gpb6", 0x20),
	EXYNOS850_PIN_BANK_EINTG(8, 0x120, "gpb7", 0x24),
	EXYNOS850_PIN_BANK_EINTG(5, 0x140, "gpd1", 0x28),
	EXYNOS850_PIN_BANK_EINTG(5, 0x160, "gpd2", 0x2c),
	EXYNOS850_PIN_BANK_EINTG(7, 0x180, "gpd3", 0x30),
	EXYNOS850_PIN_BANK_EINTG(8, 0x1a0, "gpg0", 0x34),
	EXYNOS850_PIN_BANK_EINTG(8, 0x1c0, "gpg1", 0x38),
	EXYNOS850_PIN_BANK_EINTG(8, 0x1e0, "gpg2", 0x3c),
	EXYNOS850_PIN_BANK_EINTG(8, 0x200, "gpg3", 0x40),
	EXYNOS850_PIN_BANK_EINTG(8, 0x220, "gpg4", 0x44),
	EXYNOS850_PIN_BANK_EINTG(8, 0x240, "gpg5", 0x48),
	EXYNOS850_PIN_BANK_EINTG(8, 0x260, "gpg6", 0x4c),
	EXYNOS850_PIN_BANK_EINTG(8, 0x280, "gpg7", 0x50),
};

/* pin banks of FSD pin-controller 2 (PMU) */
static const struct samsung_pin_bank_data fsd_pin_banks2[] __initconst = {
	EXYNOS850_PIN_BANK_EINTN(3, 0x00, "gpq0"),
};

static const struct samsung_pin_ctrl fsd_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 FSYS0 data */
		.pin_banks	= fsd_pin_banks0,
		.nr_banks	= ARRAY_SIZE(fsd_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 PERIC data */
		.pin_banks	= fsd_pin_banks1,
		.nr_banks	= ARRAY_SIZE(fsd_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 2 PMU data */
		.pin_banks	= fsd_pin_banks2,
		.nr_banks	= ARRAY_SIZE(fsd_pin_banks2),
	},
};

const struct samsung_pinctrl_of_match_data fsd_of_data __initconst = {
	.ctrl		= fsd_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(fsd_pin_ctrl),
};

/* pin banks of gs101 pin-controller (ALIVE) */
static const struct samsung_pin_bank_data gs101_pin_alive[] = {
	GS101_PIN_BANK_EINTW(8, 0x0, "gpa0", 0x00, 0x00),
	GS101_PIN_BANK_EINTW(7, 0x20, "gpa1", 0x04, 0x08),
	GS101_PIN_BANK_EINTW(5, 0x40, "gpa2", 0x08, 0x10),
	GS101_PIN_BANK_EINTW(4, 0x60, "gpa3", 0x0c, 0x18),
	GS101_PIN_BANK_EINTW(4, 0x80, "gpa4", 0x10, 0x1c),
	GS101_PIN_BANK_EINTW(7, 0xa0, "gpa5", 0x14, 0x20),
	GS101_PIN_BANK_EINTW(8, 0xc0, "gpa9", 0x18, 0x28),
	GS101_PIN_BANK_EINTW(2, 0xe0, "gpa10", 0x1c, 0x30),
};

/* pin banks of gs101 pin-controller (FAR_ALIVE) */
static const struct samsung_pin_bank_data gs101_pin_far_alive[] = {
	GS101_PIN_BANK_EINTW(8, 0x0, "gpa6", 0x00, 0x00),
	GS101_PIN_BANK_EINTW(4, 0x20, "gpa7", 0x04, 0x08),
	GS101_PIN_BANK_EINTW(8, 0x40, "gpa8", 0x08, 0x0c),
	GS101_PIN_BANK_EINTW(2, 0x60, "gpa11", 0x0c, 0x14),
};

/* pin banks of gs101 pin-controller (GSACORE) */
static const struct samsung_pin_bank_data gs101_pin_gsacore[] = {
	GS101_PIN_BANK_EINTG(2, 0x0, "gps0", 0x00, 0x00),
	GS101_PIN_BANK_EINTG(8, 0x20, "gps1", 0x04, 0x04),
	GS101_PIN_BANK_EINTG(3, 0x40, "gps2", 0x08, 0x0c),
};

/* pin banks of gs101 pin-controller (GSACTRL) */
static const struct samsung_pin_bank_data gs101_pin_gsactrl[] = {
	GS101_PIN_BANK_EINTW(6, 0x0, "gps3", 0x00, 0x00),
};

/* pin banks of gs101 pin-controller (PERIC0) */
static const struct samsung_pin_bank_data gs101_pin_peric0[] = {
	GS101_PIN_BANK_EINTG(5, 0x0, "gpp0", 0x00, 0x00),
	GS101_PIN_BANK_EINTG(4, 0x20, "gpp1", 0x04, 0x08),
	GS101_PIN_BANK_EINTG(4, 0x40, "gpp2", 0x08, 0x0c),
	GS101_PIN_BANK_EINTG(2, 0x60, "gpp3", 0x0c, 0x10),
	GS101_PIN_BANK_EINTG(4, 0x80, "gpp4", 0x10, 0x14),
	GS101_PIN_BANK_EINTG(2, 0xa0, "gpp5", 0x14, 0x18),
	GS101_PIN_BANK_EINTG(4, 0xc0, "gpp6", 0x18, 0x1c),
	GS101_PIN_BANK_EINTG(2, 0xe0, "gpp7", 0x1c, 0x20),
	GS101_PIN_BANK_EINTG(4, 0x100, "gpp8", 0x20, 0x24),
	GS101_PIN_BANK_EINTG(2, 0x120, "gpp9", 0x24, 0x28),
	GS101_PIN_BANK_EINTG(4, 0x140, "gpp10", 0x28, 0x2c),
	GS101_PIN_BANK_EINTG(2, 0x160, "gpp11", 0x2c, 0x30),
	GS101_PIN_BANK_EINTG(4, 0x180, "gpp12", 0x30, 0x34),
	GS101_PIN_BANK_EINTG(2, 0x1a0, "gpp13", 0x34, 0x38),
	GS101_PIN_BANK_EINTG(4, 0x1c0, "gpp14", 0x38, 0x3c),
	GS101_PIN_BANK_EINTG(2, 0x1e0, "gpp15", 0x3c, 0x40),
	GS101_PIN_BANK_EINTG(4, 0x200, "gpp16", 0x40, 0x44),
	GS101_PIN_BANK_EINTG(2, 0x220, "gpp17", 0x44, 0x48),
	GS101_PIN_BANK_EINTG(4, 0x240, "gpp18", 0x48, 0x4c),
	GS101_PIN_BANK_EINTG(4, 0x260, "gpp19", 0x4c, 0x50),
};

/* pin banks of gs101 pin-controller (PERIC1) */
static const struct samsung_pin_bank_data gs101_pin_peric1[] = {
	GS101_PIN_BANK_EINTG(8, 0x0, "gpp20", 0x00, 0x00),
	GS101_PIN_BANK_EINTG(4, 0x20, "gpp21", 0x04, 0x08),
	GS101_PIN_BANK_EINTG(2, 0x40, "gpp22", 0x08, 0x0c),
	GS101_PIN_BANK_EINTG(8, 0x60, "gpp23", 0x0c, 0x10),
	GS101_PIN_BANK_EINTG(4, 0x80, "gpp24", 0x10, 0x18),
	GS101_PIN_BANK_EINTG(4, 0xa0, "gpp25", 0x14, 0x1c),
	GS101_PIN_BANK_EINTG(5, 0xc0, "gpp26", 0x18, 0x20),
	GS101_PIN_BANK_EINTG(4, 0xe0, "gpp27", 0x1c, 0x28),
};

/* pin banks of gs101 pin-controller (HSI1) */
static const struct samsung_pin_bank_data gs101_pin_hsi1[] = {
	GS101_PIN_BANK_EINTG(6, 0x0, "gph0", 0x00, 0x00),
	GS101_PIN_BANK_EINTG(7, 0x20, "gph1", 0x04, 0x08),
};

/* pin banks of gs101 pin-controller (HSI2) */
static const struct samsung_pin_bank_data gs101_pin_hsi2[] = {
	GS101_PIN_BANK_EINTG(6, 0x0, "gph2", 0x00, 0x00),
	GS101_PIN_BANK_EINTG(2, 0x20, "gph3", 0x04, 0x08),
	GS101_PIN_BANK_EINTG(6, 0x40, "gph4", 0x08, 0x0c),
};

static const struct samsung_pin_ctrl gs101_pin_ctrl[] __initconst = {
	{
		/* pin banks of gs101 pin-controller (ALIVE) */
		.pin_banks	= gs101_pin_alive,
		.nr_banks	= ARRAY_SIZE(gs101_pin_alive),
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= gs101_pinctrl_suspend,
		.resume		= gs101_pinctrl_resume,
	}, {
		/* pin banks of gs101 pin-controller (FAR_ALIVE) */
		.pin_banks	= gs101_pin_far_alive,
		.nr_banks	= ARRAY_SIZE(gs101_pin_far_alive),
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= gs101_pinctrl_suspend,
		.resume		= gs101_pinctrl_resume,
	}, {
		/* pin banks of gs101 pin-controller (GSACORE) */
		.pin_banks	= gs101_pin_gsacore,
		.nr_banks	= ARRAY_SIZE(gs101_pin_gsacore),
	}, {
		/* pin banks of gs101 pin-controller (GSACTRL) */
		.pin_banks	= gs101_pin_gsactrl,
		.nr_banks	= ARRAY_SIZE(gs101_pin_gsactrl),
	}, {
		/* pin banks of gs101 pin-controller (PERIC0) */
		.pin_banks	= gs101_pin_peric0,
		.nr_banks	= ARRAY_SIZE(gs101_pin_peric0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= gs101_pinctrl_suspend,
		.resume		= gs101_pinctrl_resume,
	}, {
		/* pin banks of gs101 pin-controller (PERIC1) */
		.pin_banks	= gs101_pin_peric1,
		.nr_banks	= ARRAY_SIZE(gs101_pin_peric1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= gs101_pinctrl_suspend,
		.resume		= gs101_pinctrl_resume,
	}, {
		/* pin banks of gs101 pin-controller (HSI1) */
		.pin_banks	= gs101_pin_hsi1,
		.nr_banks	= ARRAY_SIZE(gs101_pin_hsi1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= gs101_pinctrl_suspend,
		.resume		= gs101_pinctrl_resume,
	}, {
		/* pin banks of gs101 pin-controller (HSI2) */
		.pin_banks	= gs101_pin_hsi2,
		.nr_banks	= ARRAY_SIZE(gs101_pin_hsi2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= gs101_pinctrl_suspend,
		.resume		= gs101_pinctrl_resume,
	},
};

const struct samsung_pinctrl_of_match_data gs101_of_data __initconst = {
	.ctrl		= gs101_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(gs101_pin_ctrl),
};
