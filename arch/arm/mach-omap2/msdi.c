/*
 * MSDI IP block reset
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * XXX What about pad muxing?
 */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/platform_data/gpio-omap.h>

#include "prm.h"
#include "common.h"
#include "control.h"
#include "omap_hwmod.h"
#include "omap_device.h"
#include "mux.h"
#include "mmc.h"

/*
 * MSDI_CON_OFFSET: offset in bytes of the MSDI IP block's CON register
 *     from the IP block's base address
 */
#define MSDI_CON_OFFSET				0x0c

/* Register bitfields in the CON register */
#define MSDI_CON_POW_MASK			BIT(11)
#define MSDI_CON_CLKD_MASK			(0x3f << 0)
#define MSDI_CON_CLKD_SHIFT			0

/* MSDI_TARGET_RESET_CLKD: clock divisor to use throughout the reset */
#define MSDI_TARGET_RESET_CLKD		0x3ff

/**
 * omap_msdi_reset - reset the MSDI IP block
 * @oh: struct omap_hwmod *
 *
 * The MSDI IP block on OMAP2420 has to have both the POW and CLKD
 * fields set inside its CON register for a reset to complete
 * successfully.  This is not documented in the TRM.  For CLKD, we use
 * the value that results in the lowest possible clock rate, to attempt
 * to avoid disturbing any cards.
 */
int omap_msdi_reset(struct omap_hwmod *oh)
{
	u16 v = 0;
	int c = 0;

	/* Write to the SOFTRESET bit */
	omap_hwmod_softreset(oh);

	/* Enable the MSDI core and internal clock */
	v |= MSDI_CON_POW_MASK;
	v |= MSDI_TARGET_RESET_CLKD << MSDI_CON_CLKD_SHIFT;
	omap_hwmod_write(v, oh, MSDI_CON_OFFSET);

	/* Poll on RESETDONE bit */
	omap_test_timeout((omap_hwmod_read(oh, oh->class->sysc->syss_offs)
			   & SYSS_RESETDONE_MASK),
			  MAX_MODULE_SOFTRESET_WAIT, c);

	if (c == MAX_MODULE_SOFTRESET_WAIT)
		pr_warning("%s: %s: softreset failed (waited %d usec)\n",
			   __func__, oh->name, MAX_MODULE_SOFTRESET_WAIT);
	else
		pr_debug("%s: %s: softreset in %d usec\n", __func__,
			 oh->name, c);

	/* Disable the MSDI internal clock */
	v &= ~MSDI_CON_CLKD_MASK;
	omap_hwmod_write(v, oh, MSDI_CON_OFFSET);

	return 0;
}

#if defined(CONFIG_MMC_OMAP) || defined(CONFIG_MMC_OMAP_MODULE)

static inline void omap242x_mmc_mux(struct omap_mmc_platform_data
				    *mmc_controller)
{
	if ((mmc_controller->slots[0].switch_pin > 0) && \
		(mmc_controller->slots[0].switch_pin < OMAP_MAX_GPIO_LINES))
		omap_mux_init_gpio(mmc_controller->slots[0].switch_pin,
					OMAP_PIN_INPUT_PULLUP);
	if ((mmc_controller->slots[0].gpio_wp > 0) && \
		(mmc_controller->slots[0].gpio_wp < OMAP_MAX_GPIO_LINES))
		omap_mux_init_gpio(mmc_controller->slots[0].gpio_wp,
					OMAP_PIN_INPUT_PULLUP);

	omap_mux_init_signal("sdmmc_cmd", 0);
	omap_mux_init_signal("sdmmc_clki", 0);
	omap_mux_init_signal("sdmmc_clko", 0);
	omap_mux_init_signal("sdmmc_dat0", 0);
	omap_mux_init_signal("sdmmc_dat_dir0", 0);
	omap_mux_init_signal("sdmmc_cmd_dir", 0);
	if (mmc_controller->slots[0].caps & MMC_CAP_4_BIT_DATA) {
		omap_mux_init_signal("sdmmc_dat1", 0);
		omap_mux_init_signal("sdmmc_dat2", 0);
		omap_mux_init_signal("sdmmc_dat3", 0);
		omap_mux_init_signal("sdmmc_dat_dir1", 0);
		omap_mux_init_signal("sdmmc_dat_dir2", 0);
		omap_mux_init_signal("sdmmc_dat_dir3", 0);
	}

	/*
	 * Use internal loop-back in MMC/SDIO Module Input Clock
	 * selection
	 */
	if (mmc_controller->slots[0].internal_clock) {
		u32 v = omap_ctrl_readl(OMAP2_CONTROL_DEVCONF0);
		v |= (1 << 24);
		omap_ctrl_writel(v, OMAP2_CONTROL_DEVCONF0);
	}
}

void __init omap242x_init_mmc(struct omap_mmc_platform_data **mmc_data)
{
	struct platform_device *pdev;
	struct omap_hwmod *oh;
	int id = 0;
	char *oh_name = "msdi1";
	char *dev_name = "mmci-omap";

	if (!mmc_data[0]) {
		pr_err("%s fails: Incomplete platform data\n", __func__);
		return;
	}

	omap242x_mmc_mux(mmc_data[0]);

	oh = omap_hwmod_lookup(oh_name);
	if (!oh) {
		pr_err("Could not look up %s\n", oh_name);
		return;
	}
	pdev = omap_device_build(dev_name, id, oh, mmc_data[0],
				 sizeof(struct omap_mmc_platform_data));
	if (IS_ERR(pdev))
		WARN(1, "Can'd build omap_device for %s:%s.\n",
					dev_name, oh->name);
}

#endif
