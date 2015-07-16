/*
 * OMAP2plus display device setup / initialization.
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 *	Senthilvadivu Guruswamy
 *	Sumit Semwal
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>

#include <video/omapdss.h>
#include "omap_hwmod.h"
#include "omap_device.h"
#include "omap-pm.h"
#include "common.h"

#include "soc.h"
#include "iomap.h"
#include "control.h"
#include "display.h"
#include "prm.h"

#define DISPC_CONTROL		0x0040
#define DISPC_CONTROL2		0x0238
#define DISPC_CONTROL3		0x0848
#define DISPC_IRQSTATUS		0x0018

#define DSS_SYSCONFIG		0x10
#define DSS_SYSSTATUS		0x14
#define DSS_CONTROL		0x40
#define DSS_SDI_CONTROL		0x44
#define DSS_PLL_CONTROL		0x48

#define LCD_EN_MASK		(0x1 << 0)
#define DIGIT_EN_MASK		(0x1 << 1)

#define FRAMEDONE_IRQ_SHIFT	0
#define EVSYNC_EVEN_IRQ_SHIFT	2
#define EVSYNC_ODD_IRQ_SHIFT	3
#define FRAMEDONE2_IRQ_SHIFT	22
#define FRAMEDONE3_IRQ_SHIFT	30
#define FRAMEDONETV_IRQ_SHIFT	24

/*
 * FRAMEDONE_IRQ_TIMEOUT: how long (in milliseconds) to wait during DISPC
 *     reset before deciding that something has gone wrong
 */
#define FRAMEDONE_IRQ_TIMEOUT		100

static struct platform_device omap_display_device = {
	.name          = "omapdss",
	.id            = -1,
	.dev            = {
		.platform_data = NULL,
	},
};

struct omap_dss_hwmod_data {
	const char *oh_name;
	const char *dev_name;
	const int id;
};

static const struct omap_dss_hwmod_data omap2_dss_hwmod_data[] __initconst = {
	{ "dss_core", "omapdss_dss", -1 },
	{ "dss_dispc", "omapdss_dispc", -1 },
	{ "dss_rfbi", "omapdss_rfbi", -1 },
	{ "dss_venc", "omapdss_venc", -1 },
};

static const struct omap_dss_hwmod_data omap3_dss_hwmod_data[] __initconst = {
	{ "dss_core", "omapdss_dss", -1 },
	{ "dss_dispc", "omapdss_dispc", -1 },
	{ "dss_rfbi", "omapdss_rfbi", -1 },
	{ "dss_venc", "omapdss_venc", -1 },
	{ "dss_dsi1", "omapdss_dsi", 0 },
};

static const struct omap_dss_hwmod_data omap4_dss_hwmod_data[] __initconst = {
	{ "dss_core", "omapdss_dss", -1 },
	{ "dss_dispc", "omapdss_dispc", -1 },
	{ "dss_rfbi", "omapdss_rfbi", -1 },
	{ "dss_dsi1", "omapdss_dsi", 0 },
	{ "dss_dsi2", "omapdss_dsi", 1 },
	{ "dss_hdmi", "omapdss_hdmi", -1 },
};

#define OMAP4_DSIPHY_SYSCON_OFFSET		0x78

static struct regmap *omap4_dsi_mux_syscon;

static int omap4_dsi_mux_pads(int dsi_id, unsigned lanes)
{
	u32 enable_mask, enable_shift;
	u32 pipd_mask, pipd_shift;
	u32 reg;

	if (dsi_id == 0) {
		enable_mask = OMAP4_DSI1_LANEENABLE_MASK;
		enable_shift = OMAP4_DSI1_LANEENABLE_SHIFT;
		pipd_mask = OMAP4_DSI1_PIPD_MASK;
		pipd_shift = OMAP4_DSI1_PIPD_SHIFT;
	} else if (dsi_id == 1) {
		enable_mask = OMAP4_DSI2_LANEENABLE_MASK;
		enable_shift = OMAP4_DSI2_LANEENABLE_SHIFT;
		pipd_mask = OMAP4_DSI2_PIPD_MASK;
		pipd_shift = OMAP4_DSI2_PIPD_SHIFT;
	} else {
		return -ENODEV;
	}

	regmap_read(omap4_dsi_mux_syscon, OMAP4_DSIPHY_SYSCON_OFFSET, &reg);

	reg &= ~enable_mask;
	reg &= ~pipd_mask;

	reg |= (lanes << enable_shift) & enable_mask;
	reg |= (lanes << pipd_shift) & pipd_mask;

	regmap_write(omap4_dsi_mux_syscon, OMAP4_DSIPHY_SYSCON_OFFSET, reg);

	return 0;
}

static int omap_dsi_enable_pads(int dsi_id, unsigned lane_mask)
{
	if (cpu_is_omap44xx())
		return omap4_dsi_mux_pads(dsi_id, lane_mask);

	return 0;
}

static void omap_dsi_disable_pads(int dsi_id, unsigned lane_mask)
{
	if (cpu_is_omap44xx())
		omap4_dsi_mux_pads(dsi_id, 0);
}

static int omap_dss_set_min_bus_tput(struct device *dev, unsigned long tput)
{
	return omap_pm_set_min_bus_tput(dev, OCP_INITIATOR_AGENT, tput);
}

static struct platform_device *create_dss_pdev(const char *pdev_name,
		int pdev_id, const char *oh_name, void *pdata, int pdata_len,
		struct platform_device *parent)
{
	struct platform_device *pdev;
	struct omap_device *od;
	struct omap_hwmod *ohs[1];
	struct omap_hwmod *oh;
	int r;

	oh = omap_hwmod_lookup(oh_name);
	if (!oh) {
		pr_err("Could not look up %s\n", oh_name);
		r = -ENODEV;
		goto err;
	}

	pdev = platform_device_alloc(pdev_name, pdev_id);
	if (!pdev) {
		pr_err("Could not create pdev for %s\n", pdev_name);
		r = -ENOMEM;
		goto err;
	}

	if (parent != NULL)
		pdev->dev.parent = &parent->dev;

	if (pdev->id != -1)
		dev_set_name(&pdev->dev, "%s.%d", pdev->name, pdev->id);
	else
		dev_set_name(&pdev->dev, "%s", pdev->name);

	ohs[0] = oh;
	od = omap_device_alloc(pdev, ohs, 1);
	if (IS_ERR(od)) {
		pr_err("Could not alloc omap_device for %s\n", pdev_name);
		r = -ENOMEM;
		goto err;
	}

	r = platform_device_add_data(pdev, pdata, pdata_len);
	if (r) {
		pr_err("Could not set pdata for %s\n", pdev_name);
		goto err;
	}

	r = omap_device_register(pdev);
	if (r) {
		pr_err("Could not register omap_device for %s\n", pdev_name);
		goto err;
	}

	return pdev;

err:
	return ERR_PTR(r);
}

static struct platform_device *create_simple_dss_pdev(const char *pdev_name,
		int pdev_id, void *pdata, int pdata_len,
		struct platform_device *parent)
{
	struct platform_device *pdev;
	int r;

	pdev = platform_device_alloc(pdev_name, pdev_id);
	if (!pdev) {
		pr_err("Could not create pdev for %s\n", pdev_name);
		r = -ENOMEM;
		goto err;
	}

	if (parent != NULL)
		pdev->dev.parent = &parent->dev;

	if (pdev->id != -1)
		dev_set_name(&pdev->dev, "%s.%d", pdev->name, pdev->id);
	else
		dev_set_name(&pdev->dev, "%s", pdev->name);

	r = platform_device_add_data(pdev, pdata, pdata_len);
	if (r) {
		pr_err("Could not set pdata for %s\n", pdev_name);
		goto err;
	}

	r = platform_device_add(pdev);
	if (r) {
		pr_err("Could not register platform_device for %s\n", pdev_name);
		goto err;
	}

	return pdev;

err:
	return ERR_PTR(r);
}

static enum omapdss_version __init omap_display_get_version(void)
{
	if (cpu_is_omap24xx())
		return OMAPDSS_VER_OMAP24xx;
	else if (cpu_is_omap3630())
		return OMAPDSS_VER_OMAP3630;
	else if (cpu_is_omap34xx()) {
		if (soc_is_am35xx()) {
			return OMAPDSS_VER_AM35xx;
		} else {
			if (omap_rev() < OMAP3430_REV_ES3_0)
				return OMAPDSS_VER_OMAP34xx_ES1;
			else
				return OMAPDSS_VER_OMAP34xx_ES3;
		}
	} else if (omap_rev() == OMAP4430_REV_ES1_0)
		return OMAPDSS_VER_OMAP4430_ES1;
	else if (omap_rev() == OMAP4430_REV_ES2_0 ||
			omap_rev() == OMAP4430_REV_ES2_1 ||
			omap_rev() == OMAP4430_REV_ES2_2)
		return OMAPDSS_VER_OMAP4430_ES2;
	else if (cpu_is_omap44xx())
		return OMAPDSS_VER_OMAP4;
	else if (soc_is_omap54xx())
		return OMAPDSS_VER_OMAP5;
	else if (soc_is_am43xx())
		return OMAPDSS_VER_AM43xx;
	else if (soc_is_dra7xx())
		return OMAPDSS_VER_DRA7xx;
	else
		return OMAPDSS_VER_UNKNOWN;
}

int __init omap_display_init(struct omap_dss_board_info *board_data)
{
	int r = 0;
	struct platform_device *pdev;
	int i, oh_count;
	const struct omap_dss_hwmod_data *curr_dss_hwmod;
	struct platform_device *dss_pdev;
	enum omapdss_version ver;

	/* create omapdss device */

	ver = omap_display_get_version();

	if (ver == OMAPDSS_VER_UNKNOWN) {
		pr_err("DSS not supported on this SoC\n");
		return -ENODEV;
	}

	board_data->version = ver;
	board_data->dsi_enable_pads = omap_dsi_enable_pads;
	board_data->dsi_disable_pads = omap_dsi_disable_pads;
	board_data->set_min_bus_tput = omap_dss_set_min_bus_tput;

	omap_display_device.dev.platform_data = board_data;

	r = platform_device_register(&omap_display_device);
	if (r < 0) {
		pr_err("Unable to register omapdss device\n");
		return r;
	}

	/* create devices for dss hwmods */

	if (cpu_is_omap24xx()) {
		curr_dss_hwmod = omap2_dss_hwmod_data;
		oh_count = ARRAY_SIZE(omap2_dss_hwmod_data);
	} else if (cpu_is_omap34xx()) {
		curr_dss_hwmod = omap3_dss_hwmod_data;
		oh_count = ARRAY_SIZE(omap3_dss_hwmod_data);
	} else {
		curr_dss_hwmod = omap4_dss_hwmod_data;
		oh_count = ARRAY_SIZE(omap4_dss_hwmod_data);
	}

	/*
	 * First create the pdev for dss_core, which is used as a parent device
	 * by the other dss pdevs. Note: dss_core has to be the first item in
	 * the hwmod list.
	 */
	dss_pdev = create_dss_pdev(curr_dss_hwmod[0].dev_name,
			curr_dss_hwmod[0].id,
			curr_dss_hwmod[0].oh_name,
			board_data, sizeof(*board_data),
			NULL);

	if (IS_ERR(dss_pdev)) {
		pr_err("Could not build omap_device for %s\n",
				curr_dss_hwmod[0].oh_name);

		return PTR_ERR(dss_pdev);
	}

	for (i = 1; i < oh_count; i++) {
		pdev = create_dss_pdev(curr_dss_hwmod[i].dev_name,
				curr_dss_hwmod[i].id,
				curr_dss_hwmod[i].oh_name,
				board_data, sizeof(*board_data),
				dss_pdev);

		if (IS_ERR(pdev)) {
			pr_err("Could not build omap_device for %s\n",
					curr_dss_hwmod[i].oh_name);

			return PTR_ERR(pdev);
		}
	}

	/* Create devices for DPI and SDI */

	pdev = create_simple_dss_pdev("omapdss_dpi", 0,
			board_data, sizeof(*board_data), dss_pdev);
	if (IS_ERR(pdev)) {
		pr_err("Could not build platform_device for omapdss_dpi\n");
		return PTR_ERR(pdev);
	}

	if (cpu_is_omap34xx()) {
		pdev = create_simple_dss_pdev("omapdss_sdi", 0,
				board_data, sizeof(*board_data), dss_pdev);
		if (IS_ERR(pdev)) {
			pr_err("Could not build platform_device for omapdss_sdi\n");
			return PTR_ERR(pdev);
		}
	}

	/* create DRM device */
	r = omap_init_drm();
	if (r < 0) {
		pr_err("Unable to register omapdrm device\n");
		return r;
	}

	/* create vrfb device */
	r = omap_init_vrfb();
	if (r < 0) {
		pr_err("Unable to register omapvrfb device\n");
		return r;
	}

	/* create FB device */
	r = omap_init_fb();
	if (r < 0) {
		pr_err("Unable to register omapfb device\n");
		return r;
	}

	/* create V4L2 display device */
	r = omap_init_vout();
	if (r < 0) {
		pr_err("Unable to register omap_vout device\n");
		return r;
	}

	return 0;
}

static void dispc_disable_outputs(void)
{
	u32 v, irq_mask = 0;
	bool lcd_en, digit_en, lcd2_en = false, lcd3_en = false;
	int i;
	struct omap_dss_dispc_dev_attr *da;
	struct omap_hwmod *oh;

	oh = omap_hwmod_lookup("dss_dispc");
	if (!oh) {
		WARN(1, "display: could not disable outputs during reset - could not find dss_dispc hwmod\n");
		return;
	}

	if (!oh->dev_attr) {
		pr_err("display: could not disable outputs during reset due to missing dev_attr\n");
		return;
	}

	da = (struct omap_dss_dispc_dev_attr *)oh->dev_attr;

	/* store value of LCDENABLE and DIGITENABLE bits */
	v = omap_hwmod_read(oh, DISPC_CONTROL);
	lcd_en = v & LCD_EN_MASK;
	digit_en = v & DIGIT_EN_MASK;

	/* store value of LCDENABLE for LCD2 */
	if (da->manager_count > 2) {
		v = omap_hwmod_read(oh, DISPC_CONTROL2);
		lcd2_en = v & LCD_EN_MASK;
	}

	/* store value of LCDENABLE for LCD3 */
	if (da->manager_count > 3) {
		v = omap_hwmod_read(oh, DISPC_CONTROL3);
		lcd3_en = v & LCD_EN_MASK;
	}

	if (!(lcd_en | digit_en | lcd2_en | lcd3_en))
		return; /* no managers currently enabled */

	/*
	 * If any manager was enabled, we need to disable it before
	 * DSS clocks are disabled or DISPC module is reset
	 */
	if (lcd_en)
		irq_mask |= 1 << FRAMEDONE_IRQ_SHIFT;

	if (digit_en) {
		if (da->has_framedonetv_irq) {
			irq_mask |= 1 << FRAMEDONETV_IRQ_SHIFT;
		} else {
			irq_mask |= 1 << EVSYNC_EVEN_IRQ_SHIFT |
				1 << EVSYNC_ODD_IRQ_SHIFT;
		}
	}

	if (lcd2_en)
		irq_mask |= 1 << FRAMEDONE2_IRQ_SHIFT;
	if (lcd3_en)
		irq_mask |= 1 << FRAMEDONE3_IRQ_SHIFT;

	/*
	 * clear any previous FRAMEDONE, FRAMEDONETV,
	 * EVSYNC_EVEN/ODD, FRAMEDONE2 or FRAMEDONE3 interrupts
	 */
	omap_hwmod_write(irq_mask, oh, DISPC_IRQSTATUS);

	/* disable LCD and TV managers */
	v = omap_hwmod_read(oh, DISPC_CONTROL);
	v &= ~(LCD_EN_MASK | DIGIT_EN_MASK);
	omap_hwmod_write(v, oh, DISPC_CONTROL);

	/* disable LCD2 manager */
	if (da->manager_count > 2) {
		v = omap_hwmod_read(oh, DISPC_CONTROL2);
		v &= ~LCD_EN_MASK;
		omap_hwmod_write(v, oh, DISPC_CONTROL2);
	}

	/* disable LCD3 manager */
	if (da->manager_count > 3) {
		v = omap_hwmod_read(oh, DISPC_CONTROL3);
		v &= ~LCD_EN_MASK;
		omap_hwmod_write(v, oh, DISPC_CONTROL3);
	}

	i = 0;
	while ((omap_hwmod_read(oh, DISPC_IRQSTATUS) & irq_mask) !=
	       irq_mask) {
		i++;
		if (i > FRAMEDONE_IRQ_TIMEOUT) {
			pr_err("didn't get FRAMEDONE1/2/3 or TV interrupt\n");
			break;
		}
		mdelay(1);
	}
}

int omap_dss_reset(struct omap_hwmod *oh)
{
	struct omap_hwmod_opt_clk *oc;
	int c = 0;
	int i, r;

	if (!(oh->class->sysc->sysc_flags & SYSS_HAS_RESET_STATUS)) {
		pr_err("dss_core: hwmod data doesn't contain reset data\n");
		return -EINVAL;
	}

	for (i = oh->opt_clks_cnt, oc = oh->opt_clks; i > 0; i--, oc++)
		if (oc->_clk)
			clk_prepare_enable(oc->_clk);

	dispc_disable_outputs();

	/* clear SDI registers */
	if (cpu_is_omap3430()) {
		omap_hwmod_write(0x0, oh, DSS_SDI_CONTROL);
		omap_hwmod_write(0x0, oh, DSS_PLL_CONTROL);
	}

	/*
	 * clear DSS_CONTROL register to switch DSS clock sources to
	 * PRCM clock, if any
	 */
	omap_hwmod_write(0x0, oh, DSS_CONTROL);

	omap_test_timeout((omap_hwmod_read(oh, oh->class->sysc->syss_offs)
				& SYSS_RESETDONE_MASK),
			MAX_MODULE_SOFTRESET_WAIT, c);

	if (c == MAX_MODULE_SOFTRESET_WAIT)
		pr_warn("dss_core: waiting for reset to finish failed\n");
	else
		pr_debug("dss_core: softreset done\n");

	for (i = oh->opt_clks_cnt, oc = oh->opt_clks; i > 0; i--, oc++)
		if (oc->_clk)
			clk_disable_unprepare(oc->_clk);

	r = (c == MAX_MODULE_SOFTRESET_WAIT) ? -ETIMEDOUT : 0;

	return r;
}

void __init omapdss_early_init_of(void)
{

}

static const char * const omapdss_compat_names[] __initconst = {
	"ti,omap2-dss",
	"ti,omap3-dss",
	"ti,omap4-dss",
	"ti,omap5-dss",
	"ti,dra7-dss",
};

struct device_node * __init omapdss_find_dss_of_node(void)
{
	struct device_node *node;
	int i;

	for (i = 0; i < ARRAY_SIZE(omapdss_compat_names); ++i) {
		node = of_find_compatible_node(NULL, NULL,
			omapdss_compat_names[i]);
		if (node)
			return node;
	}

	return NULL;
}

int __init omapdss_init_of(void)
{
	int r;
	enum omapdss_version ver;
	struct device_node *node;
	struct platform_device *pdev;

	static struct omap_dss_board_info board_data = {
		.dsi_enable_pads = omap_dsi_enable_pads,
		.dsi_disable_pads = omap_dsi_disable_pads,
		.set_min_bus_tput = omap_dss_set_min_bus_tput,
	};

	/* only create dss helper devices if dss is enabled in the .dts */

	node = omapdss_find_dss_of_node();
	if (!node)
		return 0;

	if (!of_device_is_available(node))
		return 0;

	ver = omap_display_get_version();

	if (ver == OMAPDSS_VER_UNKNOWN) {
		pr_err("DSS not supported on this SoC\n");
		return -ENODEV;
	}

	pdev = of_find_device_by_node(node);

	if (!pdev) {
		pr_err("Unable to find DSS platform device\n");
		return -ENODEV;
	}

	r = of_platform_populate(node, NULL, NULL, &pdev->dev);
	if (r) {
		pr_err("Unable to populate DSS submodule devices\n");
		return r;
	}

	board_data.version = ver;

	omap_display_device.dev.platform_data = &board_data;

	r = platform_device_register(&omap_display_device);
	if (r < 0) {
		pr_err("Unable to register omapdss device\n");
		return r;
	}

	/* create DRM device */
	r = omap_init_drm();
	if (r < 0) {
		pr_err("Unable to register omapdrm device\n");
		return r;
	}

	/* create vrfb device */
	r = omap_init_vrfb();
	if (r < 0) {
		pr_err("Unable to register omapvrfb device\n");
		return r;
	}

	/* create FB device */
	r = omap_init_fb();
	if (r < 0) {
		pr_err("Unable to register omapfb device\n");
		return r;
	}

	/* create V4L2 display device */
	r = omap_init_vout();
	if (r < 0) {
		pr_err("Unable to register omap_vout device\n");
		return r;
	}

	/* add DSI info for omap4 */
	node = of_find_node_by_name(NULL, "omap4_padconf_global");
	if (node)
		omap4_dsi_mux_syscon = syscon_node_to_regmap(node);

	return 0;
}
