// SPDX-License-Identifier: GPL-2.0+
//
// Exynos specific support for Samsung pinctrl/gpiolib driver with eint support.
//
// Copyright (c) 2012 Samsung Electronics Co., Ltd.
//		http://www.samsung.com
// Copyright (c) 2012 Linaro Ltd
//		http://www.linaro.org
//
// Author: Thomas Abraham <thomas.ab@samsung.com>
//
// This file contains the Samsung Exynos specific information required by the
// the Samsung pinctrl/gpiolib driver. It also includes the implementation of
// external gpio and wakeup interrupt support.

#include <linux/device.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/err.h>
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

/* Retention control for S5PV210 are located at the end of clock controller */
#define S5P_OTHERS 0xE000

#define S5P_OTHERS_RET_IO		(1 << 31)
#define S5P_OTHERS_RET_CF		(1 << 30)
#define S5P_OTHERS_RET_MMC		(1 << 29)
#define S5P_OTHERS_RET_UART		(1 << 28)

static void s5pv210_retention_disable(struct samsung_pinctrl_drv_data *drvdata)
{
	void __iomem *clk_base = (void __iomem *)drvdata->retention_ctrl->priv;
	u32 tmp;

	tmp = __raw_readl(clk_base + S5P_OTHERS);
	tmp |= (S5P_OTHERS_RET_IO | S5P_OTHERS_RET_CF | S5P_OTHERS_RET_MMC |
		S5P_OTHERS_RET_UART);
	__raw_writel(tmp, clk_base + S5P_OTHERS);
}

static struct samsung_retention_ctrl *
s5pv210_retention_init(struct samsung_pinctrl_drv_data *drvdata,
		       const struct samsung_retention_data *data)
{
	struct samsung_retention_ctrl *ctrl;
	struct device_node *np;
	void __iomem *clk_base;

	ctrl = devm_kzalloc(drvdata->dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return ERR_PTR(-ENOMEM);

	np = of_find_compatible_node(NULL, NULL, "samsung,s5pv210-clock");
	if (!np) {
		pr_err("%s: failed to find clock controller DT node\n",
			__func__);
		return ERR_PTR(-ENODEV);
	}

	clk_base = of_iomap(np, 0);
	if (!clk_base) {
		pr_err("%s: failed to map clock registers\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	ctrl->priv = (void __force *)clk_base;
	ctrl->disable = s5pv210_retention_disable;

	return ctrl;
}

static const struct samsung_retention_data s5pv210_retention_data __initconst = {
	.init	 = s5pv210_retention_init,
};

/* pin banks of s5pv210 pin-controller */
static const struct samsung_pin_bank_data s5pv210_pin_bank[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(4, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpb", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpc0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpc1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0a0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(6, 0x0c0, "gpd1", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x0e0, "gpe0", 0x1c),
	EXYNOS_PIN_BANK_EINTG(5, 0x100, "gpe1", 0x20),
	EXYNOS_PIN_BANK_EINTG(8, 0x120, "gpf0", 0x24),
	EXYNOS_PIN_BANK_EINTG(8, 0x140, "gpf1", 0x28),
	EXYNOS_PIN_BANK_EINTG(8, 0x160, "gpf2", 0x2c),
	EXYNOS_PIN_BANK_EINTG(6, 0x180, "gpf3", 0x30),
	EXYNOS_PIN_BANK_EINTG(7, 0x1a0, "gpg0", 0x34),
	EXYNOS_PIN_BANK_EINTG(7, 0x1c0, "gpg1", 0x38),
	EXYNOS_PIN_BANK_EINTG(7, 0x1e0, "gpg2", 0x3c),
	EXYNOS_PIN_BANK_EINTG(7, 0x200, "gpg3", 0x40),
	EXYNOS_PIN_BANK_EINTG(8, 0x240, "gpj0", 0x44),
	EXYNOS_PIN_BANK_EINTG(6, 0x260, "gpj1", 0x48),
	EXYNOS_PIN_BANK_EINTG(8, 0x280, "gpj2", 0x4c),
	EXYNOS_PIN_BANK_EINTG(8, 0x2a0, "gpj3", 0x50),
	EXYNOS_PIN_BANK_EINTG(5, 0x2c0, "gpj4", 0x54),
	EXYNOS_PIN_BANK_EINTN(7, 0x220, "gpi"),
	EXYNOS_PIN_BANK_EINTN(8, 0x2e0, "mp01"),
	EXYNOS_PIN_BANK_EINTN(4, 0x300, "mp02"),
	EXYNOS_PIN_BANK_EINTN(8, 0x320, "mp03"),
	EXYNOS_PIN_BANK_EINTN(8, 0x340, "mp04"),
	EXYNOS_PIN_BANK_EINTN(8, 0x360, "mp05"),
	EXYNOS_PIN_BANK_EINTN(8, 0x380, "mp06"),
	EXYNOS_PIN_BANK_EINTN(8, 0x3a0, "mp07"),
	EXYNOS_PIN_BANK_EINTW(8, 0xc00, "gph0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xc20, "gph1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xc40, "gph2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xc60, "gph3", 0x0c),
};

static const struct samsung_pin_ctrl s5pv210_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= s5pv210_pin_bank,
		.nr_banks	= ARRAY_SIZE(s5pv210_pin_bank),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &s5pv210_retention_data,
	},
};

const struct samsung_pinctrl_of_match_data s5pv210_of_data __initconst = {
	.ctrl		= s5pv210_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(s5pv210_pin_ctrl),
};

/* Pad retention control code for accessing PMU regmap */
static atomic_t exynos_shared_retention_refcnt;

/* pin banks of exynos3250 pin-controller 0 */
static const struct samsung_pin_bank_data exynos3250_pin_banks0[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpb",  0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpc0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpc1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0a0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0c0, "gpd1", 0x18),
};

/* pin banks of exynos3250 pin-controller 1 */
static const struct samsung_pin_bank_data exynos3250_pin_banks1[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTN(8, 0x120, "gpe0"),
	EXYNOS_PIN_BANK_EINTN(8, 0x140, "gpe1"),
	EXYNOS_PIN_BANK_EINTN(3, 0x180, "gpe2"),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpk0", 0x08),
	EXYNOS_PIN_BANK_EINTG(7, 0x060, "gpk1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(7, 0x080, "gpk2", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0c0, "gpl0", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x260, "gpm0", 0x24),
	EXYNOS_PIN_BANK_EINTG(7, 0x280, "gpm1", 0x28),
	EXYNOS_PIN_BANK_EINTG(5, 0x2a0, "gpm2", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x2c0, "gpm3", 0x30),
	EXYNOS_PIN_BANK_EINTG(8, 0x2e0, "gpm4", 0x34),
	EXYNOS_PIN_BANK_EINTW(8, 0xc00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xc20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xc40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xc60, "gpx3", 0x0c),
};

/*
 * PMU pad retention groups for Exynos3250 doesn't match pin banks, so handle
 * them all together
 */
static const u32 exynos3250_retention_regs[] = {
	S5P_PAD_RET_MAUDIO_OPTION,
	S5P_PAD_RET_GPIO_OPTION,
	S5P_PAD_RET_UART_OPTION,
	S5P_PAD_RET_MMCA_OPTION,
	S5P_PAD_RET_MMCB_OPTION,
	S5P_PAD_RET_EBIA_OPTION,
	S5P_PAD_RET_EBIB_OPTION,
	S5P_PAD_RET_MMC2_OPTION,
	S5P_PAD_RET_SPI_OPTION,
};

static const struct samsung_retention_data exynos3250_retention_data __initconst = {
	.regs	 = exynos3250_retention_regs,
	.nr_regs = ARRAY_SIZE(exynos3250_retention_regs),
	.value	 = EXYNOS_WAKEUP_FROM_LOWPWR,
	.refcnt	 = &exynos_shared_retention_refcnt,
	.init	 = exynos_retention_init,
};

/*
 * Samsung pinctrl driver data for Exynos3250 SoC. Exynos3250 SoC includes
 * two gpio/pin-mux/pinconfig controllers.
 */
static const struct samsung_pin_ctrl exynos3250_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos3250_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos3250_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos3250_retention_data,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos3250_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos3250_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos3250_retention_data,
	},
};

const struct samsung_pinctrl_of_match_data exynos3250_of_data __initconst = {
	.ctrl		= exynos3250_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos3250_pin_ctrl),
};

/* pin banks of exynos4210 pin-controller 0 */
static const struct samsung_pin_bank_data exynos4210_pin_banks0[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpb", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpc0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpc1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0A0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0C0, "gpd1", 0x18),
	EXYNOS_PIN_BANK_EINTG(5, 0x0E0, "gpe0", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gpe1", 0x20),
	EXYNOS_PIN_BANK_EINTG(6, 0x120, "gpe2", 0x24),
	EXYNOS_PIN_BANK_EINTG(8, 0x140, "gpe3", 0x28),
	EXYNOS_PIN_BANK_EINTG(8, 0x160, "gpe4", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpf0", 0x30),
	EXYNOS_PIN_BANK_EINTG(8, 0x1A0, "gpf1", 0x34),
	EXYNOS_PIN_BANK_EINTG(8, 0x1C0, "gpf2", 0x38),
	EXYNOS_PIN_BANK_EINTG(6, 0x1E0, "gpf3", 0x3c),
};

/* pin banks of exynos4210 pin-controller 1 */
static const struct samsung_pin_bank_data exynos4210_pin_banks1[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpj0", 0x00),
	EXYNOS_PIN_BANK_EINTG(5, 0x020, "gpj1", 0x04),
	EXYNOS_PIN_BANK_EINTG(7, 0x040, "gpk0", 0x08),
	EXYNOS_PIN_BANK_EINTG(7, 0x060, "gpk1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(7, 0x080, "gpk2", 0x10),
	EXYNOS_PIN_BANK_EINTG(7, 0x0A0, "gpk3", 0x14),
	EXYNOS_PIN_BANK_EINTG(8, 0x0C0, "gpl0", 0x18),
	EXYNOS_PIN_BANK_EINTG(3, 0x0E0, "gpl1", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gpl2", 0x20),
	EXYNOS_PIN_BANK_EINTN(6, 0x120, "gpy0"),
	EXYNOS_PIN_BANK_EINTN(4, 0x140, "gpy1"),
	EXYNOS_PIN_BANK_EINTN(6, 0x160, "gpy2"),
	EXYNOS_PIN_BANK_EINTN(8, 0x180, "gpy3"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1A0, "gpy4"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1C0, "gpy5"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1E0, "gpy6"),
	EXYNOS_PIN_BANK_EINTW(8, 0xC00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xC40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xC60, "gpx3", 0x0c),
};

/* pin banks of exynos4210 pin-controller 2 */
static const struct samsung_pin_bank_data exynos4210_pin_banks2[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTN(7, 0x000, "gpz"),
};

/* PMU pad retention groups registers for Exynos4 (without audio) */
static const u32 exynos4_retention_regs[] = {
	S5P_PAD_RET_GPIO_OPTION,
	S5P_PAD_RET_UART_OPTION,
	S5P_PAD_RET_MMCA_OPTION,
	S5P_PAD_RET_MMCB_OPTION,
	S5P_PAD_RET_EBIA_OPTION,
	S5P_PAD_RET_EBIB_OPTION,
};

static const struct samsung_retention_data exynos4_retention_data __initconst = {
	.regs	 = exynos4_retention_regs,
	.nr_regs = ARRAY_SIZE(exynos4_retention_regs),
	.value	 = EXYNOS_WAKEUP_FROM_LOWPWR,
	.refcnt	 = &exynos_shared_retention_refcnt,
	.init	 = exynos_retention_init,
};

/* PMU retention control for audio pins can be tied to audio pin bank */
static const u32 exynos4_audio_retention_regs[] = {
	S5P_PAD_RET_MAUDIO_OPTION,
};

static const struct samsung_retention_data exynos4_audio_retention_data __initconst = {
	.regs	 = exynos4_audio_retention_regs,
	.nr_regs = ARRAY_SIZE(exynos4_audio_retention_regs),
	.value	 = EXYNOS_WAKEUP_FROM_LOWPWR,
	.init	 = exynos_retention_init,
};

/*
 * Samsung pinctrl driver data for Exynos4210 SoC. Exynos4210 SoC includes
 * three gpio/pin-mux/pinconfig controllers.
 */
static const struct samsung_pin_ctrl exynos4210_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos4210_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos4210_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos4_retention_data,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos4210_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos4210_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos4_retention_data,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos4210_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos4210_pin_banks2),
		.retention_data	= &exynos4_audio_retention_data,
	},
};

const struct samsung_pinctrl_of_match_data exynos4210_of_data __initconst = {
	.ctrl		= exynos4210_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos4210_pin_ctrl),
};

/* pin banks of exynos4x12 pin-controller 0 */
static const struct samsung_pin_bank_data exynos4x12_pin_banks0[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpb", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpc0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpc1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0A0, "gpd0", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0C0, "gpd1", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpf0", 0x30),
	EXYNOS_PIN_BANK_EINTG(8, 0x1A0, "gpf1", 0x34),
	EXYNOS_PIN_BANK_EINTG(8, 0x1C0, "gpf2", 0x38),
	EXYNOS_PIN_BANK_EINTG(6, 0x1E0, "gpf3", 0x3c),
	EXYNOS_PIN_BANK_EINTG(8, 0x240, "gpj0", 0x40),
	EXYNOS_PIN_BANK_EINTG(5, 0x260, "gpj1", 0x44),
};

/* pin banks of exynos4x12 pin-controller 1 */
static const struct samsung_pin_bank_data exynos4x12_pin_banks1[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(7, 0x040, "gpk0", 0x08),
	EXYNOS_PIN_BANK_EINTG(7, 0x060, "gpk1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(7, 0x080, "gpk2", 0x10),
	EXYNOS_PIN_BANK_EINTG(7, 0x0A0, "gpk3", 0x14),
	EXYNOS_PIN_BANK_EINTG(7, 0x0C0, "gpl0", 0x18),
	EXYNOS_PIN_BANK_EINTG(2, 0x0E0, "gpl1", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gpl2", 0x20),
	EXYNOS_PIN_BANK_EINTG(8, 0x260, "gpm0", 0x24),
	EXYNOS_PIN_BANK_EINTG(7, 0x280, "gpm1", 0x28),
	EXYNOS_PIN_BANK_EINTG(5, 0x2A0, "gpm2", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x2C0, "gpm3", 0x30),
	EXYNOS_PIN_BANK_EINTG(8, 0x2E0, "gpm4", 0x34),
	EXYNOS_PIN_BANK_EINTN(6, 0x120, "gpy0"),
	EXYNOS_PIN_BANK_EINTN(4, 0x140, "gpy1"),
	EXYNOS_PIN_BANK_EINTN(6, 0x160, "gpy2"),
	EXYNOS_PIN_BANK_EINTN(8, 0x180, "gpy3"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1A0, "gpy4"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1C0, "gpy5"),
	EXYNOS_PIN_BANK_EINTN(8, 0x1E0, "gpy6"),
	EXYNOS_PIN_BANK_EINTW(8, 0xC00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xC40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xC60, "gpx3", 0x0c),
};

/* pin banks of exynos4x12 pin-controller 2 */
static const struct samsung_pin_bank_data exynos4x12_pin_banks2[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz", 0x00),
};

/* pin banks of exynos4x12 pin-controller 3 */
static const struct samsung_pin_bank_data exynos4x12_pin_banks3[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpv0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpv1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpv2", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpv3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(2, 0x080, "gpv4", 0x10),
};

/*
 * Samsung pinctrl driver data for Exynos4x12 SoC. Exynos4x12 SoC includes
 * four gpio/pin-mux/pinconfig controllers.
 */
static const struct samsung_pin_ctrl exynos4x12_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos4x12_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos4x12_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos4_retention_data,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos4x12_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos4x12_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos4_retention_data,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos4x12_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos4x12_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos4_audio_retention_data,
	}, {
		/* pin-controller instance 3 data */
		.pin_banks	= exynos4x12_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos4x12_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

const struct samsung_pinctrl_of_match_data exynos4x12_of_data __initconst = {
	.ctrl		= exynos4x12_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos4x12_pin_ctrl),
};

/* pin banks of exynos5250 pin-controller 0 */
static const struct samsung_pin_bank_data exynos5250_pin_banks0[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpb0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpb1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0A0, "gpb2", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0C0, "gpb3", 0x18),
	EXYNOS_PIN_BANK_EINTG(7, 0x0E0, "gpc0", 0x1c),
	EXYNOS_PIN_BANK_EINTG(4, 0x100, "gpc1", 0x20),
	EXYNOS_PIN_BANK_EINTG(7, 0x120, "gpc2", 0x24),
	EXYNOS_PIN_BANK_EINTG(7, 0x140, "gpc3", 0x28),
	EXYNOS_PIN_BANK_EINTG(4, 0x160, "gpd0", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpd1", 0x30),
	EXYNOS_PIN_BANK_EINTG(7, 0x2E0, "gpc4", 0x34),
	EXYNOS_PIN_BANK_EINTN(6, 0x1A0, "gpy0"),
	EXYNOS_PIN_BANK_EINTN(4, 0x1C0, "gpy1"),
	EXYNOS_PIN_BANK_EINTN(6, 0x1E0, "gpy2"),
	EXYNOS_PIN_BANK_EINTN(8, 0x200, "gpy3"),
	EXYNOS_PIN_BANK_EINTN(8, 0x220, "gpy4"),
	EXYNOS_PIN_BANK_EINTN(8, 0x240, "gpy5"),
	EXYNOS_PIN_BANK_EINTN(8, 0x260, "gpy6"),
	EXYNOS_PIN_BANK_EINTW(8, 0xC00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xC40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xC60, "gpx3", 0x0c),
};

/* pin banks of exynos5250 pin-controller 1 */
static const struct samsung_pin_bank_data exynos5250_pin_banks1[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpe0", 0x00),
	EXYNOS_PIN_BANK_EINTG(2, 0x020, "gpe1", 0x04),
	EXYNOS_PIN_BANK_EINTG(4, 0x040, "gpf0", 0x08),
	EXYNOS_PIN_BANK_EINTG(4, 0x060, "gpf1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(8, 0x080, "gpg0", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0A0, "gpg1", 0x14),
	EXYNOS_PIN_BANK_EINTG(2, 0x0C0, "gpg2", 0x18),
	EXYNOS_PIN_BANK_EINTG(4, 0x0E0, "gph0", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gph1", 0x20),
};

/* pin banks of exynos5250 pin-controller 2 */
static const struct samsung_pin_bank_data exynos5250_pin_banks2[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpv0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpv1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpv2", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x080, "gpv3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(2, 0x0C0, "gpv4", 0x10),
};

/* pin banks of exynos5250 pin-controller 3 */
static const struct samsung_pin_bank_data exynos5250_pin_banks3[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz", 0x00),
};

/*
 * Samsung pinctrl driver data for Exynos5250 SoC. Exynos5250 SoC includes
 * four gpio/pin-mux/pinconfig controllers.
 */
static const struct samsung_pin_ctrl exynos5250_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos5250_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos5250_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos4_retention_data,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos5250_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos5250_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos4_retention_data,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos5250_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos5250_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 3 data */
		.pin_banks	= exynos5250_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos5250_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
		.retention_data	= &exynos4_audio_retention_data,
	},
};

const struct samsung_pinctrl_of_match_data exynos5250_of_data __initconst = {
	.ctrl		= exynos5250_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos5250_pin_ctrl),
};

/* pin banks of exynos5260 pin-controller 0 */
static const struct samsung_pin_bank_data exynos5260_pin_banks0[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(4, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(7, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpb0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(4, 0x080, "gpb1", 0x10),
	EXYNOS_PIN_BANK_EINTG(5, 0x0a0, "gpb2", 0x14),
	EXYNOS_PIN_BANK_EINTG(8, 0x0c0, "gpb3", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x0e0, "gpb4", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gpb5", 0x20),
	EXYNOS_PIN_BANK_EINTG(8, 0x120, "gpd0", 0x24),
	EXYNOS_PIN_BANK_EINTG(7, 0x140, "gpd1", 0x28),
	EXYNOS_PIN_BANK_EINTG(5, 0x160, "gpd2", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpe0", 0x30),
	EXYNOS_PIN_BANK_EINTG(5, 0x1a0, "gpe1", 0x34),
	EXYNOS_PIN_BANK_EINTG(4, 0x1c0, "gpf0", 0x38),
	EXYNOS_PIN_BANK_EINTG(8, 0x1e0, "gpf1", 0x3c),
	EXYNOS_PIN_BANK_EINTG(2, 0x200, "gpk0", 0x40),
	EXYNOS_PIN_BANK_EINTW(8, 0xc00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xc20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xc40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xc60, "gpx3", 0x0c),
};

/* pin banks of exynos5260 pin-controller 1 */
static const struct samsung_pin_bank_data exynos5260_pin_banks1[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpc0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpc1", 0x04),
	EXYNOS_PIN_BANK_EINTG(7, 0x040, "gpc2", 0x08),
	EXYNOS_PIN_BANK_EINTG(4, 0x060, "gpc3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(4, 0x080, "gpc4", 0x10),
};

/* pin banks of exynos5260 pin-controller 2 */
static const struct samsung_pin_bank_data exynos5260_pin_banks2[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz0", 0x00),
	EXYNOS_PIN_BANK_EINTG(4, 0x020, "gpz1", 0x04),
};

/*
 * Samsung pinctrl driver data for Exynos5260 SoC. Exynos5260 SoC includes
 * three gpio/pin-mux/pinconfig controllers.
 */
static const struct samsung_pin_ctrl exynos5260_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos5260_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos5260_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos5260_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos5260_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos5260_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos5260_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
	},
};

const struct samsung_pinctrl_of_match_data exynos5260_of_data __initconst = {
	.ctrl		= exynos5260_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos5260_pin_ctrl),
};

/* pin banks of exynos5410 pin-controller 0 */
static const struct samsung_pin_bank_data exynos5410_pin_banks0[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpb0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpb1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0A0, "gpb2", 0x14),
	EXYNOS_PIN_BANK_EINTG(4, 0x0C0, "gpb3", 0x18),
	EXYNOS_PIN_BANK_EINTG(7, 0x0E0, "gpc0", 0x1c),
	EXYNOS_PIN_BANK_EINTG(4, 0x100, "gpc3", 0x20),
	EXYNOS_PIN_BANK_EINTG(7, 0x120, "gpc1", 0x24),
	EXYNOS_PIN_BANK_EINTG(7, 0x140, "gpc2", 0x28),
	EXYNOS_PIN_BANK_EINTG(8, 0x180, "gpd1", 0x2c),
	EXYNOS_PIN_BANK_EINTG(8, 0x1A0, "gpe0", 0x30),
	EXYNOS_PIN_BANK_EINTG(2, 0x1C0, "gpe1", 0x34),
	EXYNOS_PIN_BANK_EINTG(6, 0x1E0, "gpf0", 0x38),
	EXYNOS_PIN_BANK_EINTG(8, 0x200, "gpf1", 0x3c),
	EXYNOS_PIN_BANK_EINTG(8, 0x220, "gpg0", 0x40),
	EXYNOS_PIN_BANK_EINTG(8, 0x240, "gpg1", 0x44),
	EXYNOS_PIN_BANK_EINTG(2, 0x260, "gpg2", 0x48),
	EXYNOS_PIN_BANK_EINTG(4, 0x280, "gph0", 0x4c),
	EXYNOS_PIN_BANK_EINTG(8, 0x2A0, "gph1", 0x50),
	EXYNOS_PIN_BANK_EINTN(2, 0x160, "gpm5"),
	EXYNOS_PIN_BANK_EINTN(8, 0x2C0, "gpm7"),
	EXYNOS_PIN_BANK_EINTN(6, 0x2E0, "gpy0"),
	EXYNOS_PIN_BANK_EINTN(4, 0x300, "gpy1"),
	EXYNOS_PIN_BANK_EINTN(6, 0x320, "gpy2"),
	EXYNOS_PIN_BANK_EINTN(8, 0x340, "gpy3"),
	EXYNOS_PIN_BANK_EINTN(8, 0x360, "gpy4"),
	EXYNOS_PIN_BANK_EINTN(8, 0x380, "gpy5"),
	EXYNOS_PIN_BANK_EINTN(8, 0x3A0, "gpy6"),
	EXYNOS_PIN_BANK_EINTN(8, 0x3C0, "gpy7"),
	EXYNOS_PIN_BANK_EINTW(8, 0xC00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xC40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xC60, "gpx3", 0x0c),
};

/* pin banks of exynos5410 pin-controller 1 */
static const struct samsung_pin_bank_data exynos5410_pin_banks1[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(5, 0x000, "gpj0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpj1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpj2", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpj3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(2, 0x080, "gpj4", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0A0, "gpk0", 0x14),
	EXYNOS_PIN_BANK_EINTG(8, 0x0C0, "gpk1", 0x18),
	EXYNOS_PIN_BANK_EINTG(8, 0x0E0, "gpk2", 0x1c),
	EXYNOS_PIN_BANK_EINTG(7, 0x100, "gpk3", 0x20),
};

/* pin banks of exynos5410 pin-controller 2 */
static const struct samsung_pin_bank_data exynos5410_pin_banks2[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpv0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpv1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpv2", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x080, "gpv3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(2, 0x0C0, "gpv4", 0x10),
};

/* pin banks of exynos5410 pin-controller 3 */
static const struct samsung_pin_bank_data exynos5410_pin_banks3[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz", 0x00),
};

/*
 * Samsung pinctrl driver data for Exynos5410 SoC. Exynos5410 SoC includes
 * four gpio/pin-mux/pinconfig controllers.
 */
static const struct samsung_pin_ctrl exynos5410_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos5410_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos5410_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos5410_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos5410_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos5410_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos5410_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	}, {
		/* pin-controller instance 3 data */
		.pin_banks	= exynos5410_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos5410_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.suspend	= exynos_pinctrl_suspend,
		.resume		= exynos_pinctrl_resume,
	},
};

const struct samsung_pinctrl_of_match_data exynos5410_of_data __initconst = {
	.ctrl		= exynos5410_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos5410_pin_ctrl),
};

/* pin banks of exynos5420 pin-controller 0 */
static const struct samsung_pin_bank_data exynos5420_pin_banks0[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpy7", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC00, "gpx0", 0x00),
	EXYNOS_PIN_BANK_EINTW(8, 0xC20, "gpx1", 0x04),
	EXYNOS_PIN_BANK_EINTW(8, 0xC40, "gpx2", 0x08),
	EXYNOS_PIN_BANK_EINTW(8, 0xC60, "gpx3", 0x0c),
};

/* pin banks of exynos5420 pin-controller 1 */
static const struct samsung_pin_bank_data exynos5420_pin_banks1[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpc0", 0x00),
	EXYNOS_PIN_BANK_EINTG(8, 0x020, "gpc1", 0x04),
	EXYNOS_PIN_BANK_EINTG(7, 0x040, "gpc2", 0x08),
	EXYNOS_PIN_BANK_EINTG(4, 0x060, "gpc3", 0x0c),
	EXYNOS_PIN_BANK_EINTG(2, 0x080, "gpc4", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0A0, "gpd1", 0x14),
	EXYNOS_PIN_BANK_EINTN(6, 0x0C0, "gpy0"),
	EXYNOS_PIN_BANK_EINTN(4, 0x0E0, "gpy1"),
	EXYNOS_PIN_BANK_EINTN(6, 0x100, "gpy2"),
	EXYNOS_PIN_BANK_EINTN(8, 0x120, "gpy3"),
	EXYNOS_PIN_BANK_EINTN(8, 0x140, "gpy4"),
	EXYNOS_PIN_BANK_EINTN(8, 0x160, "gpy5"),
	EXYNOS_PIN_BANK_EINTN(8, 0x180, "gpy6"),
};

/* pin banks of exynos5420 pin-controller 2 */
static const struct samsung_pin_bank_data exynos5420_pin_banks2[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpe0", 0x00),
	EXYNOS_PIN_BANK_EINTG(2, 0x020, "gpe1", 0x04),
	EXYNOS_PIN_BANK_EINTG(6, 0x040, "gpf0", 0x08),
	EXYNOS_PIN_BANK_EINTG(8, 0x060, "gpf1", 0x0c),
	EXYNOS_PIN_BANK_EINTG(8, 0x080, "gpg0", 0x10),
	EXYNOS_PIN_BANK_EINTG(8, 0x0A0, "gpg1", 0x14),
	EXYNOS_PIN_BANK_EINTG(2, 0x0C0, "gpg2", 0x18),
	EXYNOS_PIN_BANK_EINTG(4, 0x0E0, "gpj4", 0x1c),
};

/* pin banks of exynos5420 pin-controller 3 */
static const struct samsung_pin_bank_data exynos5420_pin_banks3[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(8, 0x000, "gpa0", 0x00),
	EXYNOS_PIN_BANK_EINTG(6, 0x020, "gpa1", 0x04),
	EXYNOS_PIN_BANK_EINTG(8, 0x040, "gpa2", 0x08),
	EXYNOS_PIN_BANK_EINTG(5, 0x060, "gpb0", 0x0c),
	EXYNOS_PIN_BANK_EINTG(5, 0x080, "gpb1", 0x10),
	EXYNOS_PIN_BANK_EINTG(4, 0x0A0, "gpb2", 0x14),
	EXYNOS_PIN_BANK_EINTG(8, 0x0C0, "gpb3", 0x18),
	EXYNOS_PIN_BANK_EINTG(2, 0x0E0, "gpb4", 0x1c),
	EXYNOS_PIN_BANK_EINTG(8, 0x100, "gph0", 0x20),
};

/* pin banks of exynos5420 pin-controller 4 */
static const struct samsung_pin_bank_data exynos5420_pin_banks4[] __initconst = {
	/* Must start with EINTG banks, ordered by EINT group number. */
	EXYNOS_PIN_BANK_EINTG(7, 0x000, "gpz", 0x00),
};

/* PMU pad retention groups registers for Exynos5420 (without audio) */
static const u32 exynos5420_retention_regs[] = {
	EXYNOS_PAD_RET_DRAM_OPTION,
	EXYNOS_PAD_RET_JTAG_OPTION,
	EXYNOS5420_PAD_RET_GPIO_OPTION,
	EXYNOS5420_PAD_RET_UART_OPTION,
	EXYNOS5420_PAD_RET_MMCA_OPTION,
	EXYNOS5420_PAD_RET_MMCB_OPTION,
	EXYNOS5420_PAD_RET_MMCC_OPTION,
	EXYNOS5420_PAD_RET_HSI_OPTION,
	EXYNOS_PAD_RET_EBIA_OPTION,
	EXYNOS_PAD_RET_EBIB_OPTION,
	EXYNOS5420_PAD_RET_SPI_OPTION,
	EXYNOS5420_PAD_RET_DRAM_COREBLK_OPTION,
};

static const struct samsung_retention_data exynos5420_retention_data __initconst = {
	.regs	 = exynos5420_retention_regs,
	.nr_regs = ARRAY_SIZE(exynos5420_retention_regs),
	.value	 = EXYNOS_WAKEUP_FROM_LOWPWR,
	.refcnt	 = &exynos_shared_retention_refcnt,
	.init	 = exynos_retention_init,
};

/*
 * Samsung pinctrl driver data for Exynos5420 SoC. Exynos5420 SoC includes
 * four gpio/pin-mux/pinconfig controllers.
 */
static const struct samsung_pin_ctrl exynos5420_pin_ctrl[] __initconst = {
	{
		/* pin-controller instance 0 data */
		.pin_banks	= exynos5420_pin_banks0,
		.nr_banks	= ARRAY_SIZE(exynos5420_pin_banks0),
		.eint_gpio_init = exynos_eint_gpio_init,
		.eint_wkup_init = exynos_eint_wkup_init,
		.retention_data	= &exynos5420_retention_data,
	}, {
		/* pin-controller instance 1 data */
		.pin_banks	= exynos5420_pin_banks1,
		.nr_banks	= ARRAY_SIZE(exynos5420_pin_banks1),
		.eint_gpio_init = exynos_eint_gpio_init,
		.retention_data	= &exynos5420_retention_data,
	}, {
		/* pin-controller instance 2 data */
		.pin_banks	= exynos5420_pin_banks2,
		.nr_banks	= ARRAY_SIZE(exynos5420_pin_banks2),
		.eint_gpio_init = exynos_eint_gpio_init,
		.retention_data	= &exynos5420_retention_data,
	}, {
		/* pin-controller instance 3 data */
		.pin_banks	= exynos5420_pin_banks3,
		.nr_banks	= ARRAY_SIZE(exynos5420_pin_banks3),
		.eint_gpio_init = exynos_eint_gpio_init,
		.retention_data	= &exynos5420_retention_data,
	}, {
		/* pin-controller instance 4 data */
		.pin_banks	= exynos5420_pin_banks4,
		.nr_banks	= ARRAY_SIZE(exynos5420_pin_banks4),
		.eint_gpio_init = exynos_eint_gpio_init,
		.retention_data	= &exynos4_audio_retention_data,
	},
};

const struct samsung_pinctrl_of_match_data exynos5420_of_data __initconst = {
	.ctrl		= exynos5420_pin_ctrl,
	.num_ctrl	= ARRAY_SIZE(exynos5420_pin_ctrl),
};
