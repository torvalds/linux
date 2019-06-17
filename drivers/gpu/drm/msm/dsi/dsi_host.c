// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/pinctrl/consumer.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/spinlock.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <video/mipi_display.h>

#include "dsi.h"
#include "dsi.xml.h"
#include "sfpb.xml.h"
#include "dsi_cfg.h"
#include "msm_kms.h"

static int dsi_get_version(const void __iomem *base, u32 *major, u32 *minor)
{
	u32 ver;

	if (!major || !minor)
		return -EINVAL;

	/*
	 * From DSI6G(v3), addition of a 6G_HW_VERSION register at offset 0
	 * makes all other registers 4-byte shifted down.
	 *
	 * In order to identify between DSI6G(v3) and beyond, and DSIv2 and
	 * older, we read the DSI_VERSION register without any shift(offset
	 * 0x1f0). In the case of DSIv2, this hast to be a non-zero value. In
	 * the case of DSI6G, this has to be zero (the offset points to a
	 * scratch register which we never touch)
	 */

	ver = msm_readl(base + REG_DSI_VERSION);
	if (ver) {
		/* older dsi host, there is no register shift */
		ver = FIELD(ver, DSI_VERSION_MAJOR);
		if (ver <= MSM_DSI_VER_MAJOR_V2) {
			/* old versions */
			*major = ver;
			*minor = 0;
			return 0;
		} else {
			return -EINVAL;
		}
	} else {
		/*
		 * newer host, offset 0 has 6G_HW_VERSION, the rest of the
		 * registers are shifted down, read DSI_VERSION again with
		 * the shifted offset
		 */
		ver = msm_readl(base + DSI_6G_REG_SHIFT + REG_DSI_VERSION);
		ver = FIELD(ver, DSI_VERSION_MAJOR);
		if (ver == MSM_DSI_VER_MAJOR_6G) {
			/* 6G version */
			*major = ver;
			*minor = msm_readl(base + REG_DSI_6G_HW_VERSION);
			return 0;
		} else {
			return -EINVAL;
		}
	}
}

#define DSI_ERR_STATE_ACK			0x0000
#define DSI_ERR_STATE_TIMEOUT			0x0001
#define DSI_ERR_STATE_DLN0_PHY			0x0002
#define DSI_ERR_STATE_FIFO			0x0004
#define DSI_ERR_STATE_MDP_FIFO_UNDERFLOW	0x0008
#define DSI_ERR_STATE_INTERLEAVE_OP_CONTENTION	0x0010
#define DSI_ERR_STATE_PLL_UNLOCKED		0x0020

#define DSI_CLK_CTRL_ENABLE_CLKS	\
		(DSI_CLK_CTRL_AHBS_HCLK_ON | DSI_CLK_CTRL_AHBM_SCLK_ON | \
		DSI_CLK_CTRL_PCLK_ON | DSI_CLK_CTRL_DSICLK_ON | \
		DSI_CLK_CTRL_BYTECLK_ON | DSI_CLK_CTRL_ESCCLK_ON | \
		DSI_CLK_CTRL_FORCE_ON_DYN_AHBM_HCLK)

struct msm_dsi_host {
	struct mipi_dsi_host base;

	struct platform_device *pdev;
	struct drm_device *dev;

	int id;

	void __iomem *ctrl_base;
	struct regulator_bulk_data supplies[DSI_DEV_REGULATOR_MAX];

	struct clk *bus_clks[DSI_BUS_CLK_MAX];

	struct clk *byte_clk;
	struct clk *esc_clk;
	struct clk *pixel_clk;
	struct clk *byte_clk_src;
	struct clk *pixel_clk_src;
	struct clk *byte_intf_clk;

	u32 byte_clk_rate;
	u32 pixel_clk_rate;
	u32 esc_clk_rate;

	/* DSI v2 specific clocks */
	struct clk *src_clk;
	struct clk *esc_clk_src;
	struct clk *dsi_clk_src;

	u32 src_clk_rate;

	struct gpio_desc *disp_en_gpio;
	struct gpio_desc *te_gpio;

	const struct msm_dsi_cfg_handler *cfg_hnd;

	struct completion dma_comp;
	struct completion video_comp;
	struct mutex dev_mutex;
	struct mutex cmd_mutex;
	spinlock_t intr_lock; /* Protect interrupt ctrl register */

	u32 err_work_state;
	struct work_struct err_work;
	struct work_struct hpd_work;
	struct workqueue_struct *workqueue;

	/* DSI 6G TX buffer*/
	struct drm_gem_object *tx_gem_obj;

	/* DSI v2 TX buffer */
	void *tx_buf;
	dma_addr_t tx_buf_paddr;

	int tx_size;

	u8 *rx_buf;

	struct regmap *sfpb;

	struct drm_display_mode *mode;

	/* connected device info */
	struct device_node *device_node;
	unsigned int channel;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;

	/* lane data parsed via DT */
	int dlane_swap;
	int num_data_lanes;

	u32 dma_cmd_ctrl_restore;

	bool registered;
	bool power_on;
	bool enabled;
	int irq;
};

static u32 dsi_get_bpp(const enum mipi_dsi_pixel_format fmt)
{
	switch (fmt) {
	case MIPI_DSI_FMT_RGB565:		return 16;
	case MIPI_DSI_FMT_RGB666_PACKED:	return 18;
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB888:
	default:				return 24;
	}
}

static inline u32 dsi_read(struct msm_dsi_host *msm_host, u32 reg)
{
	return msm_readl(msm_host->ctrl_base + reg);
}
static inline void dsi_write(struct msm_dsi_host *msm_host, u32 reg, u32 data)
{
	msm_writel(data, msm_host->ctrl_base + reg);
}

static int dsi_host_regulator_enable(struct msm_dsi_host *msm_host);
static void dsi_host_regulator_disable(struct msm_dsi_host *msm_host);

static const struct msm_dsi_cfg_handler *dsi_get_config(
						struct msm_dsi_host *msm_host)
{
	const struct msm_dsi_cfg_handler *cfg_hnd = NULL;
	struct device *dev = &msm_host->pdev->dev;
	struct regulator *gdsc_reg;
	struct clk *ahb_clk;
	int ret;
	u32 major = 0, minor = 0;

	gdsc_reg = regulator_get(dev, "gdsc");
	if (IS_ERR(gdsc_reg)) {
		pr_err("%s: cannot get gdsc\n", __func__);
		goto exit;
	}

	ahb_clk = msm_clk_get(msm_host->pdev, "iface");
	if (IS_ERR(ahb_clk)) {
		pr_err("%s: cannot get interface clock\n", __func__);
		goto put_gdsc;
	}

	pm_runtime_get_sync(dev);

	ret = regulator_enable(gdsc_reg);
	if (ret) {
		pr_err("%s: unable to enable gdsc\n", __func__);
		goto put_gdsc;
	}

	ret = clk_prepare_enable(ahb_clk);
	if (ret) {
		pr_err("%s: unable to enable ahb_clk\n", __func__);
		goto disable_gdsc;
	}

	ret = dsi_get_version(msm_host->ctrl_base, &major, &minor);
	if (ret) {
		pr_err("%s: Invalid version\n", __func__);
		goto disable_clks;
	}

	cfg_hnd = msm_dsi_cfg_get(major, minor);

	DBG("%s: Version %x:%x\n", __func__, major, minor);

disable_clks:
	clk_disable_unprepare(ahb_clk);
disable_gdsc:
	regulator_disable(gdsc_reg);
	pm_runtime_put_sync(dev);
put_gdsc:
	regulator_put(gdsc_reg);
exit:
	return cfg_hnd;
}

static inline struct msm_dsi_host *to_msm_dsi_host(struct mipi_dsi_host *host)
{
	return container_of(host, struct msm_dsi_host, base);
}

static void dsi_host_regulator_disable(struct msm_dsi_host *msm_host)
{
	struct regulator_bulk_data *s = msm_host->supplies;
	const struct dsi_reg_entry *regs = msm_host->cfg_hnd->cfg->reg_cfg.regs;
	int num = msm_host->cfg_hnd->cfg->reg_cfg.num;
	int i;

	DBG("");
	for (i = num - 1; i >= 0; i--)
		if (regs[i].disable_load >= 0)
			regulator_set_load(s[i].consumer,
					   regs[i].disable_load);

	regulator_bulk_disable(num, s);
}

static int dsi_host_regulator_enable(struct msm_dsi_host *msm_host)
{
	struct regulator_bulk_data *s = msm_host->supplies;
	const struct dsi_reg_entry *regs = msm_host->cfg_hnd->cfg->reg_cfg.regs;
	int num = msm_host->cfg_hnd->cfg->reg_cfg.num;
	int ret, i;

	DBG("");
	for (i = 0; i < num; i++) {
		if (regs[i].enable_load >= 0) {
			ret = regulator_set_load(s[i].consumer,
						 regs[i].enable_load);
			if (ret < 0) {
				pr_err("regulator %d set op mode failed, %d\n",
					i, ret);
				goto fail;
			}
		}
	}

	ret = regulator_bulk_enable(num, s);
	if (ret < 0) {
		pr_err("regulator enable failed, %d\n", ret);
		goto fail;
	}

	return 0;

fail:
	for (i--; i >= 0; i--)
		regulator_set_load(s[i].consumer, regs[i].disable_load);
	return ret;
}

static int dsi_regulator_init(struct msm_dsi_host *msm_host)
{
	struct regulator_bulk_data *s = msm_host->supplies;
	const struct dsi_reg_entry *regs = msm_host->cfg_hnd->cfg->reg_cfg.regs;
	int num = msm_host->cfg_hnd->cfg->reg_cfg.num;
	int i, ret;

	for (i = 0; i < num; i++)
		s[i].supply = regs[i].name;

	ret = devm_regulator_bulk_get(&msm_host->pdev->dev, num, s);
	if (ret < 0) {
		pr_err("%s: failed to init regulator, ret=%d\n",
						__func__, ret);
		return ret;
	}

	return 0;
}

int dsi_clk_init_v2(struct msm_dsi_host *msm_host)
{
	struct platform_device *pdev = msm_host->pdev;
	int ret = 0;

	msm_host->src_clk = msm_clk_get(pdev, "src");

	if (IS_ERR(msm_host->src_clk)) {
		ret = PTR_ERR(msm_host->src_clk);
		pr_err("%s: can't find src clock. ret=%d\n",
			__func__, ret);
		msm_host->src_clk = NULL;
		return ret;
	}

	msm_host->esc_clk_src = clk_get_parent(msm_host->esc_clk);
	if (!msm_host->esc_clk_src) {
		ret = -ENODEV;
		pr_err("%s: can't get esc clock parent. ret=%d\n",
			__func__, ret);
		return ret;
	}

	msm_host->dsi_clk_src = clk_get_parent(msm_host->src_clk);
	if (!msm_host->dsi_clk_src) {
		ret = -ENODEV;
		pr_err("%s: can't get src clock parent. ret=%d\n",
			__func__, ret);
	}

	return ret;
}

int dsi_clk_init_6g_v2(struct msm_dsi_host *msm_host)
{
	struct platform_device *pdev = msm_host->pdev;
	int ret = 0;

	msm_host->byte_intf_clk = msm_clk_get(pdev, "byte_intf");
	if (IS_ERR(msm_host->byte_intf_clk)) {
		ret = PTR_ERR(msm_host->byte_intf_clk);
		pr_err("%s: can't find byte_intf clock. ret=%d\n",
			__func__, ret);
	}

	return ret;
}

static int dsi_clk_init(struct msm_dsi_host *msm_host)
{
	struct platform_device *pdev = msm_host->pdev;
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;
	const struct msm_dsi_config *cfg = cfg_hnd->cfg;
	int i, ret = 0;

	/* get bus clocks */
	for (i = 0; i < cfg->num_bus_clks; i++) {
		msm_host->bus_clks[i] = msm_clk_get(pdev,
						cfg->bus_clk_names[i]);
		if (IS_ERR(msm_host->bus_clks[i])) {
			ret = PTR_ERR(msm_host->bus_clks[i]);
			pr_err("%s: Unable to get %s clock, ret = %d\n",
				__func__, cfg->bus_clk_names[i], ret);
			goto exit;
		}
	}

	/* get link and source clocks */
	msm_host->byte_clk = msm_clk_get(pdev, "byte");
	if (IS_ERR(msm_host->byte_clk)) {
		ret = PTR_ERR(msm_host->byte_clk);
		pr_err("%s: can't find dsi_byte clock. ret=%d\n",
			__func__, ret);
		msm_host->byte_clk = NULL;
		goto exit;
	}

	msm_host->pixel_clk = msm_clk_get(pdev, "pixel");
	if (IS_ERR(msm_host->pixel_clk)) {
		ret = PTR_ERR(msm_host->pixel_clk);
		pr_err("%s: can't find dsi_pixel clock. ret=%d\n",
			__func__, ret);
		msm_host->pixel_clk = NULL;
		goto exit;
	}

	msm_host->esc_clk = msm_clk_get(pdev, "core");
	if (IS_ERR(msm_host->esc_clk)) {
		ret = PTR_ERR(msm_host->esc_clk);
		pr_err("%s: can't find dsi_esc clock. ret=%d\n",
			__func__, ret);
		msm_host->esc_clk = NULL;
		goto exit;
	}

	msm_host->byte_clk_src = clk_get_parent(msm_host->byte_clk);
	if (!msm_host->byte_clk_src) {
		ret = -ENODEV;
		pr_err("%s: can't find byte_clk clock. ret=%d\n", __func__, ret);
		goto exit;
	}

	msm_host->pixel_clk_src = clk_get_parent(msm_host->pixel_clk);
	if (!msm_host->pixel_clk_src) {
		ret = -ENODEV;
		pr_err("%s: can't find pixel_clk clock. ret=%d\n", __func__, ret);
		goto exit;
	}

	if (cfg_hnd->ops->clk_init_ver)
		ret = cfg_hnd->ops->clk_init_ver(msm_host);
exit:
	return ret;
}

static int dsi_bus_clk_enable(struct msm_dsi_host *msm_host)
{
	const struct msm_dsi_config *cfg = msm_host->cfg_hnd->cfg;
	int i, ret;

	DBG("id=%d", msm_host->id);

	for (i = 0; i < cfg->num_bus_clks; i++) {
		ret = clk_prepare_enable(msm_host->bus_clks[i]);
		if (ret) {
			pr_err("%s: failed to enable bus clock %d ret %d\n",
				__func__, i, ret);
			goto err;
		}
	}

	return 0;
err:
	for (; i > 0; i--)
		clk_disable_unprepare(msm_host->bus_clks[i]);

	return ret;
}

static void dsi_bus_clk_disable(struct msm_dsi_host *msm_host)
{
	const struct msm_dsi_config *cfg = msm_host->cfg_hnd->cfg;
	int i;

	DBG("");

	for (i = cfg->num_bus_clks - 1; i >= 0; i--)
		clk_disable_unprepare(msm_host->bus_clks[i]);
}

int msm_dsi_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_dsi *msm_dsi = platform_get_drvdata(pdev);
	struct mipi_dsi_host *host = msm_dsi->host;
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	if (!msm_host->cfg_hnd)
		return 0;

	dsi_bus_clk_disable(msm_host);

	return 0;
}

int msm_dsi_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct msm_dsi *msm_dsi = platform_get_drvdata(pdev);
	struct mipi_dsi_host *host = msm_dsi->host;
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	if (!msm_host->cfg_hnd)
		return 0;

	return dsi_bus_clk_enable(msm_host);
}

int dsi_link_clk_enable_6g(struct msm_dsi_host *msm_host)
{
	int ret;

	DBG("Set clk rates: pclk=%d, byteclk=%d",
		msm_host->mode->clock, msm_host->byte_clk_rate);

	ret = clk_set_rate(msm_host->byte_clk, msm_host->byte_clk_rate);
	if (ret) {
		pr_err("%s: Failed to set rate byte clk, %d\n", __func__, ret);
		goto error;
	}

	ret = clk_set_rate(msm_host->pixel_clk, msm_host->pixel_clk_rate);
	if (ret) {
		pr_err("%s: Failed to set rate pixel clk, %d\n", __func__, ret);
		goto error;
	}

	if (msm_host->byte_intf_clk) {
		ret = clk_set_rate(msm_host->byte_intf_clk,
				   msm_host->byte_clk_rate / 2);
		if (ret) {
			pr_err("%s: Failed to set rate byte intf clk, %d\n",
			       __func__, ret);
			goto error;
		}
	}

	ret = clk_prepare_enable(msm_host->esc_clk);
	if (ret) {
		pr_err("%s: Failed to enable dsi esc clk\n", __func__);
		goto error;
	}

	ret = clk_prepare_enable(msm_host->byte_clk);
	if (ret) {
		pr_err("%s: Failed to enable dsi byte clk\n", __func__);
		goto byte_clk_err;
	}

	ret = clk_prepare_enable(msm_host->pixel_clk);
	if (ret) {
		pr_err("%s: Failed to enable dsi pixel clk\n", __func__);
		goto pixel_clk_err;
	}

	if (msm_host->byte_intf_clk) {
		ret = clk_prepare_enable(msm_host->byte_intf_clk);
		if (ret) {
			pr_err("%s: Failed to enable byte intf clk\n",
			       __func__);
			goto byte_intf_clk_err;
		}
	}

	return 0;

byte_intf_clk_err:
	clk_disable_unprepare(msm_host->pixel_clk);
pixel_clk_err:
	clk_disable_unprepare(msm_host->byte_clk);
byte_clk_err:
	clk_disable_unprepare(msm_host->esc_clk);
error:
	return ret;
}

int dsi_link_clk_enable_v2(struct msm_dsi_host *msm_host)
{
	int ret;

	DBG("Set clk rates: pclk=%d, byteclk=%d, esc_clk=%d, dsi_src_clk=%d",
		msm_host->mode->clock, msm_host->byte_clk_rate,
		msm_host->esc_clk_rate, msm_host->src_clk_rate);

	ret = clk_set_rate(msm_host->byte_clk, msm_host->byte_clk_rate);
	if (ret) {
		pr_err("%s: Failed to set rate byte clk, %d\n", __func__, ret);
		goto error;
	}

	ret = clk_set_rate(msm_host->esc_clk, msm_host->esc_clk_rate);
	if (ret) {
		pr_err("%s: Failed to set rate esc clk, %d\n", __func__, ret);
		goto error;
	}

	ret = clk_set_rate(msm_host->src_clk, msm_host->src_clk_rate);
	if (ret) {
		pr_err("%s: Failed to set rate src clk, %d\n", __func__, ret);
		goto error;
	}

	ret = clk_set_rate(msm_host->pixel_clk, msm_host->pixel_clk_rate);
	if (ret) {
		pr_err("%s: Failed to set rate pixel clk, %d\n", __func__, ret);
		goto error;
	}

	ret = clk_prepare_enable(msm_host->byte_clk);
	if (ret) {
		pr_err("%s: Failed to enable dsi byte clk\n", __func__);
		goto error;
	}

	ret = clk_prepare_enable(msm_host->esc_clk);
	if (ret) {
		pr_err("%s: Failed to enable dsi esc clk\n", __func__);
		goto esc_clk_err;
	}

	ret = clk_prepare_enable(msm_host->src_clk);
	if (ret) {
		pr_err("%s: Failed to enable dsi src clk\n", __func__);
		goto src_clk_err;
	}

	ret = clk_prepare_enable(msm_host->pixel_clk);
	if (ret) {
		pr_err("%s: Failed to enable dsi pixel clk\n", __func__);
		goto pixel_clk_err;
	}

	return 0;

pixel_clk_err:
	clk_disable_unprepare(msm_host->src_clk);
src_clk_err:
	clk_disable_unprepare(msm_host->esc_clk);
esc_clk_err:
	clk_disable_unprepare(msm_host->byte_clk);
error:
	return ret;
}

void dsi_link_clk_disable_6g(struct msm_dsi_host *msm_host)
{
	clk_disable_unprepare(msm_host->esc_clk);
	clk_disable_unprepare(msm_host->pixel_clk);
	if (msm_host->byte_intf_clk)
		clk_disable_unprepare(msm_host->byte_intf_clk);
	clk_disable_unprepare(msm_host->byte_clk);
}

void dsi_link_clk_disable_v2(struct msm_dsi_host *msm_host)
{
	clk_disable_unprepare(msm_host->pixel_clk);
	clk_disable_unprepare(msm_host->src_clk);
	clk_disable_unprepare(msm_host->esc_clk);
	clk_disable_unprepare(msm_host->byte_clk);
}

static u32 dsi_get_pclk_rate(struct msm_dsi_host *msm_host, bool is_dual_dsi)
{
	struct drm_display_mode *mode = msm_host->mode;
	u32 pclk_rate;

	pclk_rate = mode->clock * 1000;

	/*
	 * For dual DSI mode, the current DRM mode has the complete width of the
	 * panel. Since, the complete panel is driven by two DSI controllers,
	 * the clock rates have to be split between the two dsi controllers.
	 * Adjust the byte and pixel clock rates for each dsi host accordingly.
	 */
	if (is_dual_dsi)
		pclk_rate /= 2;

	return pclk_rate;
}

static void dsi_calc_pclk(struct msm_dsi_host *msm_host, bool is_dual_dsi)
{
	u8 lanes = msm_host->lanes;
	u32 bpp = dsi_get_bpp(msm_host->format);
	u32 pclk_rate = dsi_get_pclk_rate(msm_host, is_dual_dsi);
	u64 pclk_bpp = (u64)pclk_rate * bpp;

	if (lanes == 0) {
		pr_err("%s: forcing mdss_dsi lanes to 1\n", __func__);
		lanes = 1;
	}

	do_div(pclk_bpp, (8 * lanes));

	msm_host->pixel_clk_rate = pclk_rate;
	msm_host->byte_clk_rate = pclk_bpp;

	DBG("pclk=%d, bclk=%d", msm_host->pixel_clk_rate,
				msm_host->byte_clk_rate);

}

int dsi_calc_clk_rate_6g(struct msm_dsi_host *msm_host, bool is_dual_dsi)
{
	if (!msm_host->mode) {
		pr_err("%s: mode not set\n", __func__);
		return -EINVAL;
	}

	dsi_calc_pclk(msm_host, is_dual_dsi);
	msm_host->esc_clk_rate = clk_get_rate(msm_host->esc_clk);
	return 0;
}

int dsi_calc_clk_rate_v2(struct msm_dsi_host *msm_host, bool is_dual_dsi)
{
	u32 bpp = dsi_get_bpp(msm_host->format);
	u64 pclk_bpp;
	unsigned int esc_mhz, esc_div;
	unsigned long byte_mhz;

	dsi_calc_pclk(msm_host, is_dual_dsi);

	pclk_bpp = (u64)dsi_get_pclk_rate(msm_host, is_dual_dsi) * bpp;
	do_div(pclk_bpp, 8);
	msm_host->src_clk_rate = pclk_bpp;

	/*
	 * esc clock is byte clock followed by a 4 bit divider,
	 * we need to find an escape clock frequency within the
	 * mipi DSI spec range within the maximum divider limit
	 * We iterate here between an escape clock frequencey
	 * between 20 Mhz to 5 Mhz and pick up the first one
	 * that can be supported by our divider
	 */

	byte_mhz = msm_host->byte_clk_rate / 1000000;

	for (esc_mhz = 20; esc_mhz >= 5; esc_mhz--) {
		esc_div = DIV_ROUND_UP(byte_mhz, esc_mhz);

		/*
		 * TODO: Ideally, we shouldn't know what sort of divider
		 * is available in mmss_cc, we're just assuming that
		 * it'll always be a 4 bit divider. Need to come up with
		 * a better way here.
		 */
		if (esc_div >= 1 && esc_div <= 16)
			break;
	}

	if (esc_mhz < 5)
		return -EINVAL;

	msm_host->esc_clk_rate = msm_host->byte_clk_rate / esc_div;

	DBG("esc=%d, src=%d", msm_host->esc_clk_rate,
		msm_host->src_clk_rate);

	return 0;
}

static void dsi_intr_ctrl(struct msm_dsi_host *msm_host, u32 mask, int enable)
{
	u32 intr;
	unsigned long flags;

	spin_lock_irqsave(&msm_host->intr_lock, flags);
	intr = dsi_read(msm_host, REG_DSI_INTR_CTRL);

	if (enable)
		intr |= mask;
	else
		intr &= ~mask;

	DBG("intr=%x enable=%d", intr, enable);

	dsi_write(msm_host, REG_DSI_INTR_CTRL, intr);
	spin_unlock_irqrestore(&msm_host->intr_lock, flags);
}

static inline enum dsi_traffic_mode dsi_get_traffic_mode(const u32 mode_flags)
{
	if (mode_flags & MIPI_DSI_MODE_VIDEO_BURST)
		return BURST_MODE;
	else if (mode_flags & MIPI_DSI_MODE_VIDEO_SYNC_PULSE)
		return NON_BURST_SYNCH_PULSE;

	return NON_BURST_SYNCH_EVENT;
}

static inline enum dsi_vid_dst_format dsi_get_vid_fmt(
				const enum mipi_dsi_pixel_format mipi_fmt)
{
	switch (mipi_fmt) {
	case MIPI_DSI_FMT_RGB888:	return VID_DST_FORMAT_RGB888;
	case MIPI_DSI_FMT_RGB666:	return VID_DST_FORMAT_RGB666_LOOSE;
	case MIPI_DSI_FMT_RGB666_PACKED:	return VID_DST_FORMAT_RGB666;
	case MIPI_DSI_FMT_RGB565:	return VID_DST_FORMAT_RGB565;
	default:			return VID_DST_FORMAT_RGB888;
	}
}

static inline enum dsi_cmd_dst_format dsi_get_cmd_fmt(
				const enum mipi_dsi_pixel_format mipi_fmt)
{
	switch (mipi_fmt) {
	case MIPI_DSI_FMT_RGB888:	return CMD_DST_FORMAT_RGB888;
	case MIPI_DSI_FMT_RGB666_PACKED:
	case MIPI_DSI_FMT_RGB666:	return CMD_DST_FORMAT_RGB666;
	case MIPI_DSI_FMT_RGB565:	return CMD_DST_FORMAT_RGB565;
	default:			return CMD_DST_FORMAT_RGB888;
	}
}

static void dsi_ctrl_config(struct msm_dsi_host *msm_host, bool enable,
			struct msm_dsi_phy_shared_timings *phy_shared_timings)
{
	u32 flags = msm_host->mode_flags;
	enum mipi_dsi_pixel_format mipi_fmt = msm_host->format;
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;
	u32 data = 0;

	if (!enable) {
		dsi_write(msm_host, REG_DSI_CTRL, 0);
		return;
	}

	if (flags & MIPI_DSI_MODE_VIDEO) {
		if (flags & MIPI_DSI_MODE_VIDEO_HSE)
			data |= DSI_VID_CFG0_PULSE_MODE_HSA_HE;
		if (flags & MIPI_DSI_MODE_VIDEO_HFP)
			data |= DSI_VID_CFG0_HFP_POWER_STOP;
		if (flags & MIPI_DSI_MODE_VIDEO_HBP)
			data |= DSI_VID_CFG0_HBP_POWER_STOP;
		if (flags & MIPI_DSI_MODE_VIDEO_HSA)
			data |= DSI_VID_CFG0_HSA_POWER_STOP;
		/* Always set low power stop mode for BLLP
		 * to let command engine send packets
		 */
		data |= DSI_VID_CFG0_EOF_BLLP_POWER_STOP |
			DSI_VID_CFG0_BLLP_POWER_STOP;
		data |= DSI_VID_CFG0_TRAFFIC_MODE(dsi_get_traffic_mode(flags));
		data |= DSI_VID_CFG0_DST_FORMAT(dsi_get_vid_fmt(mipi_fmt));
		data |= DSI_VID_CFG0_VIRT_CHANNEL(msm_host->channel);
		dsi_write(msm_host, REG_DSI_VID_CFG0, data);

		/* Do not swap RGB colors */
		data = DSI_VID_CFG1_RGB_SWAP(SWAP_RGB);
		dsi_write(msm_host, REG_DSI_VID_CFG1, 0);
	} else {
		/* Do not swap RGB colors */
		data = DSI_CMD_CFG0_RGB_SWAP(SWAP_RGB);
		data |= DSI_CMD_CFG0_DST_FORMAT(dsi_get_cmd_fmt(mipi_fmt));
		dsi_write(msm_host, REG_DSI_CMD_CFG0, data);

		data = DSI_CMD_CFG1_WR_MEM_START(MIPI_DCS_WRITE_MEMORY_START) |
			DSI_CMD_CFG1_WR_MEM_CONTINUE(
					MIPI_DCS_WRITE_MEMORY_CONTINUE);
		/* Always insert DCS command */
		data |= DSI_CMD_CFG1_INSERT_DCS_COMMAND;
		dsi_write(msm_host, REG_DSI_CMD_CFG1, data);
	}

	dsi_write(msm_host, REG_DSI_CMD_DMA_CTRL,
			DSI_CMD_DMA_CTRL_FROM_FRAME_BUFFER |
			DSI_CMD_DMA_CTRL_LOW_POWER);

	data = 0;
	/* Always assume dedicated TE pin */
	data |= DSI_TRIG_CTRL_TE;
	data |= DSI_TRIG_CTRL_MDP_TRIGGER(TRIGGER_NONE);
	data |= DSI_TRIG_CTRL_DMA_TRIGGER(TRIGGER_SW);
	data |= DSI_TRIG_CTRL_STREAM(msm_host->channel);
	if ((cfg_hnd->major == MSM_DSI_VER_MAJOR_6G) &&
		(cfg_hnd->minor >= MSM_DSI_6G_VER_MINOR_V1_2))
		data |= DSI_TRIG_CTRL_BLOCK_DMA_WITHIN_FRAME;
	dsi_write(msm_host, REG_DSI_TRIG_CTRL, data);

	data = DSI_CLKOUT_TIMING_CTRL_T_CLK_POST(phy_shared_timings->clk_post) |
		DSI_CLKOUT_TIMING_CTRL_T_CLK_PRE(phy_shared_timings->clk_pre);
	dsi_write(msm_host, REG_DSI_CLKOUT_TIMING_CTRL, data);

	if ((cfg_hnd->major == MSM_DSI_VER_MAJOR_6G) &&
	    (cfg_hnd->minor > MSM_DSI_6G_VER_MINOR_V1_0) &&
	    phy_shared_timings->clk_pre_inc_by_2)
		dsi_write(msm_host, REG_DSI_T_CLK_PRE_EXTEND,
			  DSI_T_CLK_PRE_EXTEND_INC_BY_2_BYTECLK);

	data = 0;
	if (!(flags & MIPI_DSI_MODE_EOT_PACKET))
		data |= DSI_EOT_PACKET_CTRL_TX_EOT_APPEND;
	dsi_write(msm_host, REG_DSI_EOT_PACKET_CTRL, data);

	/* allow only ack-err-status to generate interrupt */
	dsi_write(msm_host, REG_DSI_ERR_INT_MASK0, 0x13ff3fe0);

	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_ERROR, 1);

	dsi_write(msm_host, REG_DSI_CLK_CTRL, DSI_CLK_CTRL_ENABLE_CLKS);

	data = DSI_CTRL_CLK_EN;

	DBG("lane number=%d", msm_host->lanes);
	data |= ((DSI_CTRL_LANE0 << msm_host->lanes) - DSI_CTRL_LANE0);

	dsi_write(msm_host, REG_DSI_LANE_SWAP_CTRL,
		  DSI_LANE_SWAP_CTRL_DLN_SWAP_SEL(msm_host->dlane_swap));

	if (!(flags & MIPI_DSI_CLOCK_NON_CONTINUOUS))
		dsi_write(msm_host, REG_DSI_LANE_CTRL,
			DSI_LANE_CTRL_CLKLN_HS_FORCE_REQUEST);

	data |= DSI_CTRL_ENABLE;

	dsi_write(msm_host, REG_DSI_CTRL, data);
}

static void dsi_timing_setup(struct msm_dsi_host *msm_host, bool is_dual_dsi)
{
	struct drm_display_mode *mode = msm_host->mode;
	u32 hs_start = 0, vs_start = 0; /* take sync start as 0 */
	u32 h_total = mode->htotal;
	u32 v_total = mode->vtotal;
	u32 hs_end = mode->hsync_end - mode->hsync_start;
	u32 vs_end = mode->vsync_end - mode->vsync_start;
	u32 ha_start = h_total - mode->hsync_start;
	u32 ha_end = ha_start + mode->hdisplay;
	u32 va_start = v_total - mode->vsync_start;
	u32 va_end = va_start + mode->vdisplay;
	u32 hdisplay = mode->hdisplay;
	u32 wc;

	DBG("");

	/*
	 * For dual DSI mode, the current DRM mode has
	 * the complete width of the panel. Since, the complete
	 * panel is driven by two DSI controllers, the horizontal
	 * timings have to be split between the two dsi controllers.
	 * Adjust the DSI host timing values accordingly.
	 */
	if (is_dual_dsi) {
		h_total /= 2;
		hs_end /= 2;
		ha_start /= 2;
		ha_end /= 2;
		hdisplay /= 2;
	}

	if (msm_host->mode_flags & MIPI_DSI_MODE_VIDEO) {
		dsi_write(msm_host, REG_DSI_ACTIVE_H,
			DSI_ACTIVE_H_START(ha_start) |
			DSI_ACTIVE_H_END(ha_end));
		dsi_write(msm_host, REG_DSI_ACTIVE_V,
			DSI_ACTIVE_V_START(va_start) |
			DSI_ACTIVE_V_END(va_end));
		dsi_write(msm_host, REG_DSI_TOTAL,
			DSI_TOTAL_H_TOTAL(h_total - 1) |
			DSI_TOTAL_V_TOTAL(v_total - 1));

		dsi_write(msm_host, REG_DSI_ACTIVE_HSYNC,
			DSI_ACTIVE_HSYNC_START(hs_start) |
			DSI_ACTIVE_HSYNC_END(hs_end));
		dsi_write(msm_host, REG_DSI_ACTIVE_VSYNC_HPOS, 0);
		dsi_write(msm_host, REG_DSI_ACTIVE_VSYNC_VPOS,
			DSI_ACTIVE_VSYNC_VPOS_START(vs_start) |
			DSI_ACTIVE_VSYNC_VPOS_END(vs_end));
	} else {		/* command mode */
		/* image data and 1 byte write_memory_start cmd */
		wc = hdisplay * dsi_get_bpp(msm_host->format) / 8 + 1;

		dsi_write(msm_host, REG_DSI_CMD_MDP_STREAM_CTRL,
			DSI_CMD_MDP_STREAM_CTRL_WORD_COUNT(wc) |
			DSI_CMD_MDP_STREAM_CTRL_VIRTUAL_CHANNEL(
					msm_host->channel) |
			DSI_CMD_MDP_STREAM_CTRL_DATA_TYPE(
					MIPI_DSI_DCS_LONG_WRITE));

		dsi_write(msm_host, REG_DSI_CMD_MDP_STREAM_TOTAL,
			DSI_CMD_MDP_STREAM_TOTAL_H_TOTAL(hdisplay) |
			DSI_CMD_MDP_STREAM_TOTAL_V_TOTAL(mode->vdisplay));
	}
}

static void dsi_sw_reset(struct msm_dsi_host *msm_host)
{
	dsi_write(msm_host, REG_DSI_CLK_CTRL, DSI_CLK_CTRL_ENABLE_CLKS);
	wmb(); /* clocks need to be enabled before reset */

	dsi_write(msm_host, REG_DSI_RESET, 1);
	wmb(); /* make sure reset happen */
	dsi_write(msm_host, REG_DSI_RESET, 0);
}

static void dsi_op_mode_config(struct msm_dsi_host *msm_host,
					bool video_mode, bool enable)
{
	u32 dsi_ctrl;

	dsi_ctrl = dsi_read(msm_host, REG_DSI_CTRL);

	if (!enable) {
		dsi_ctrl &= ~(DSI_CTRL_ENABLE | DSI_CTRL_VID_MODE_EN |
				DSI_CTRL_CMD_MODE_EN);
		dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_CMD_MDP_DONE |
					DSI_IRQ_MASK_VIDEO_DONE, 0);
	} else {
		if (video_mode) {
			dsi_ctrl |= DSI_CTRL_VID_MODE_EN;
		} else {		/* command mode */
			dsi_ctrl |= DSI_CTRL_CMD_MODE_EN;
			dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_CMD_MDP_DONE, 1);
		}
		dsi_ctrl |= DSI_CTRL_ENABLE;
	}

	dsi_write(msm_host, REG_DSI_CTRL, dsi_ctrl);
}

static void dsi_set_tx_power_mode(int mode, struct msm_dsi_host *msm_host)
{
	u32 data;

	data = dsi_read(msm_host, REG_DSI_CMD_DMA_CTRL);

	if (mode == 0)
		data &= ~DSI_CMD_DMA_CTRL_LOW_POWER;
	else
		data |= DSI_CMD_DMA_CTRL_LOW_POWER;

	dsi_write(msm_host, REG_DSI_CMD_DMA_CTRL, data);
}

static void dsi_wait4video_done(struct msm_dsi_host *msm_host)
{
	u32 ret = 0;
	struct device *dev = &msm_host->pdev->dev;

	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_VIDEO_DONE, 1);

	reinit_completion(&msm_host->video_comp);

	ret = wait_for_completion_timeout(&msm_host->video_comp,
			msecs_to_jiffies(70));

	if (ret <= 0)
		DRM_DEV_ERROR(dev, "wait for video done timed out\n");

	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_VIDEO_DONE, 0);
}

static void dsi_wait4video_eng_busy(struct msm_dsi_host *msm_host)
{
	if (!(msm_host->mode_flags & MIPI_DSI_MODE_VIDEO))
		return;

	if (msm_host->power_on && msm_host->enabled) {
		dsi_wait4video_done(msm_host);
		/* delay 4 ms to skip BLLP */
		usleep_range(2000, 4000);
	}
}

int dsi_tx_buf_alloc_6g(struct msm_dsi_host *msm_host, int size)
{
	struct drm_device *dev = msm_host->dev;
	struct msm_drm_private *priv = dev->dev_private;
	uint64_t iova;
	u8 *data;

	data = msm_gem_kernel_new(dev, size, MSM_BO_UNCACHED,
					priv->kms->aspace,
					&msm_host->tx_gem_obj, &iova);

	if (IS_ERR(data)) {
		msm_host->tx_gem_obj = NULL;
		return PTR_ERR(data);
	}

	msm_gem_object_set_name(msm_host->tx_gem_obj, "tx_gem");

	msm_host->tx_size = msm_host->tx_gem_obj->size;

	return 0;
}

int dsi_tx_buf_alloc_v2(struct msm_dsi_host *msm_host, int size)
{
	struct drm_device *dev = msm_host->dev;

	msm_host->tx_buf = dma_alloc_coherent(dev->dev, size,
					&msm_host->tx_buf_paddr, GFP_KERNEL);
	if (!msm_host->tx_buf)
		return -ENOMEM;

	msm_host->tx_size = size;

	return 0;
}

static void dsi_tx_buf_free(struct msm_dsi_host *msm_host)
{
	struct drm_device *dev = msm_host->dev;
	struct msm_drm_private *priv;

	/*
	 * This is possible if we're tearing down before we've had a chance to
	 * fully initialize. A very real possibility if our probe is deferred,
	 * in which case we'll hit msm_dsi_host_destroy() without having run
	 * through the dsi_tx_buf_alloc().
	 */
	if (!dev)
		return;

	priv = dev->dev_private;
	if (msm_host->tx_gem_obj) {
		msm_gem_unpin_iova(msm_host->tx_gem_obj, priv->kms->aspace);
		drm_gem_object_put_unlocked(msm_host->tx_gem_obj);
		msm_host->tx_gem_obj = NULL;
	}

	if (msm_host->tx_buf)
		dma_free_coherent(dev->dev, msm_host->tx_size, msm_host->tx_buf,
			msm_host->tx_buf_paddr);
}

void *dsi_tx_buf_get_6g(struct msm_dsi_host *msm_host)
{
	return msm_gem_get_vaddr(msm_host->tx_gem_obj);
}

void *dsi_tx_buf_get_v2(struct msm_dsi_host *msm_host)
{
	return msm_host->tx_buf;
}

void dsi_tx_buf_put_6g(struct msm_dsi_host *msm_host)
{
	msm_gem_put_vaddr(msm_host->tx_gem_obj);
}

/*
 * prepare cmd buffer to be txed
 */
static int dsi_cmd_dma_add(struct msm_dsi_host *msm_host,
			   const struct mipi_dsi_msg *msg)
{
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;
	struct mipi_dsi_packet packet;
	int len;
	int ret;
	u8 *data;

	ret = mipi_dsi_create_packet(&packet, msg);
	if (ret) {
		pr_err("%s: create packet failed, %d\n", __func__, ret);
		return ret;
	}
	len = (packet.size + 3) & (~0x3);

	if (len > msm_host->tx_size) {
		pr_err("%s: packet size is too big\n", __func__);
		return -EINVAL;
	}

	data = cfg_hnd->ops->tx_buf_get(msm_host);
	if (IS_ERR(data)) {
		ret = PTR_ERR(data);
		pr_err("%s: get vaddr failed, %d\n", __func__, ret);
		return ret;
	}

	/* MSM specific command format in memory */
	data[0] = packet.header[1];
	data[1] = packet.header[2];
	data[2] = packet.header[0];
	data[3] = BIT(7); /* Last packet */
	if (mipi_dsi_packet_format_is_long(msg->type))
		data[3] |= BIT(6);
	if (msg->rx_buf && msg->rx_len)
		data[3] |= BIT(5);

	/* Long packet */
	if (packet.payload && packet.payload_length)
		memcpy(data + 4, packet.payload, packet.payload_length);

	/* Append 0xff to the end */
	if (packet.size < len)
		memset(data + packet.size, 0xff, len - packet.size);

	if (cfg_hnd->ops->tx_buf_put)
		cfg_hnd->ops->tx_buf_put(msm_host);

	return len;
}

/*
 * dsi_short_read1_resp: 1 parameter
 */
static int dsi_short_read1_resp(u8 *buf, const struct mipi_dsi_msg *msg)
{
	u8 *data = msg->rx_buf;
	if (data && (msg->rx_len >= 1)) {
		*data = buf[1]; /* strip out dcs type */
		return 1;
	} else {
		pr_err("%s: read data does not match with rx_buf len %zu\n",
			__func__, msg->rx_len);
		return -EINVAL;
	}
}

/*
 * dsi_short_read2_resp: 2 parameter
 */
static int dsi_short_read2_resp(u8 *buf, const struct mipi_dsi_msg *msg)
{
	u8 *data = msg->rx_buf;
	if (data && (msg->rx_len >= 2)) {
		data[0] = buf[1]; /* strip out dcs type */
		data[1] = buf[2];
		return 2;
	} else {
		pr_err("%s: read data does not match with rx_buf len %zu\n",
			__func__, msg->rx_len);
		return -EINVAL;
	}
}

static int dsi_long_read_resp(u8 *buf, const struct mipi_dsi_msg *msg)
{
	/* strip out 4 byte dcs header */
	if (msg->rx_buf && msg->rx_len)
		memcpy(msg->rx_buf, buf + 4, msg->rx_len);

	return msg->rx_len;
}

int dsi_dma_base_get_6g(struct msm_dsi_host *msm_host, uint64_t *dma_base)
{
	struct drm_device *dev = msm_host->dev;
	struct msm_drm_private *priv = dev->dev_private;

	if (!dma_base)
		return -EINVAL;

	return msm_gem_get_and_pin_iova(msm_host->tx_gem_obj,
				priv->kms->aspace, dma_base);
}

int dsi_dma_base_get_v2(struct msm_dsi_host *msm_host, uint64_t *dma_base)
{
	if (!dma_base)
		return -EINVAL;

	*dma_base = msm_host->tx_buf_paddr;
	return 0;
}

static int dsi_cmd_dma_tx(struct msm_dsi_host *msm_host, int len)
{
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;
	int ret;
	uint64_t dma_base;
	bool triggered;

	ret = cfg_hnd->ops->dma_base_get(msm_host, &dma_base);
	if (ret) {
		pr_err("%s: failed to get iova: %d\n", __func__, ret);
		return ret;
	}

	reinit_completion(&msm_host->dma_comp);

	dsi_wait4video_eng_busy(msm_host);

	triggered = msm_dsi_manager_cmd_xfer_trigger(
						msm_host->id, dma_base, len);
	if (triggered) {
		ret = wait_for_completion_timeout(&msm_host->dma_comp,
					msecs_to_jiffies(200));
		DBG("ret=%d", ret);
		if (ret == 0)
			ret = -ETIMEDOUT;
		else
			ret = len;
	} else
		ret = len;

	return ret;
}

static int dsi_cmd_dma_rx(struct msm_dsi_host *msm_host,
			u8 *buf, int rx_byte, int pkt_size)
{
	u32 *lp, *temp, data;
	int i, j = 0, cnt;
	u32 read_cnt;
	u8 reg[16];
	int repeated_bytes = 0;
	int buf_offset = buf - msm_host->rx_buf;

	lp = (u32 *)buf;
	temp = (u32 *)reg;
	cnt = (rx_byte + 3) >> 2;
	if (cnt > 4)
		cnt = 4; /* 4 x 32 bits registers only */

	if (rx_byte == 4)
		read_cnt = 4;
	else
		read_cnt = pkt_size + 6;

	/*
	 * In case of multiple reads from the panel, after the first read, there
	 * is possibility that there are some bytes in the payload repeating in
	 * the RDBK_DATA registers. Since we read all the parameters from the
	 * panel right from the first byte for every pass. We need to skip the
	 * repeating bytes and then append the new parameters to the rx buffer.
	 */
	if (read_cnt > 16) {
		int bytes_shifted;
		/* Any data more than 16 bytes will be shifted out.
		 * The temp read buffer should already contain these bytes.
		 * The remaining bytes in read buffer are the repeated bytes.
		 */
		bytes_shifted = read_cnt - 16;
		repeated_bytes = buf_offset - bytes_shifted;
	}

	for (i = cnt - 1; i >= 0; i--) {
		data = dsi_read(msm_host, REG_DSI_RDBK_DATA(i));
		*temp++ = ntohl(data); /* to host byte order */
		DBG("data = 0x%x and ntohl(data) = 0x%x", data, ntohl(data));
	}

	for (i = repeated_bytes; i < 16; i++)
		buf[j++] = reg[i];

	return j;
}

static int dsi_cmds2buf_tx(struct msm_dsi_host *msm_host,
				const struct mipi_dsi_msg *msg)
{
	int len, ret;
	int bllp_len = msm_host->mode->hdisplay *
			dsi_get_bpp(msm_host->format) / 8;

	len = dsi_cmd_dma_add(msm_host, msg);
	if (!len) {
		pr_err("%s: failed to add cmd type = 0x%x\n",
			__func__,  msg->type);
		return -EINVAL;
	}

	/* for video mode, do not send cmds more than
	* one pixel line, since it only transmit it
	* during BLLP.
	*/
	/* TODO: if the command is sent in LP mode, the bit rate is only
	 * half of esc clk rate. In this case, if the video is already
	 * actively streaming, we need to check more carefully if the
	 * command can be fit into one BLLP.
	 */
	if ((msm_host->mode_flags & MIPI_DSI_MODE_VIDEO) && (len > bllp_len)) {
		pr_err("%s: cmd cannot fit into BLLP period, len=%d\n",
			__func__, len);
		return -EINVAL;
	}

	ret = dsi_cmd_dma_tx(msm_host, len);
	if (ret < len) {
		pr_err("%s: cmd dma tx failed, type=0x%x, data0=0x%x, len=%d\n",
			__func__, msg->type, (*(u8 *)(msg->tx_buf)), len);
		return -ECOMM;
	}

	return len;
}

static void dsi_sw_reset_restore(struct msm_dsi_host *msm_host)
{
	u32 data0, data1;

	data0 = dsi_read(msm_host, REG_DSI_CTRL);
	data1 = data0;
	data1 &= ~DSI_CTRL_ENABLE;
	dsi_write(msm_host, REG_DSI_CTRL, data1);
	/*
	 * dsi controller need to be disabled before
	 * clocks turned on
	 */
	wmb();

	dsi_write(msm_host, REG_DSI_CLK_CTRL, DSI_CLK_CTRL_ENABLE_CLKS);
	wmb();	/* make sure clocks enabled */

	/* dsi controller can only be reset while clocks are running */
	dsi_write(msm_host, REG_DSI_RESET, 1);
	wmb();	/* make sure reset happen */
	dsi_write(msm_host, REG_DSI_RESET, 0);
	wmb();	/* controller out of reset */
	dsi_write(msm_host, REG_DSI_CTRL, data0);
	wmb();	/* make sure dsi controller enabled again */
}

static void dsi_hpd_worker(struct work_struct *work)
{
	struct msm_dsi_host *msm_host =
		container_of(work, struct msm_dsi_host, hpd_work);

	drm_helper_hpd_irq_event(msm_host->dev);
}

static void dsi_err_worker(struct work_struct *work)
{
	struct msm_dsi_host *msm_host =
		container_of(work, struct msm_dsi_host, err_work);
	u32 status = msm_host->err_work_state;

	pr_err_ratelimited("%s: status=%x\n", __func__, status);
	if (status & DSI_ERR_STATE_MDP_FIFO_UNDERFLOW)
		dsi_sw_reset_restore(msm_host);

	/* It is safe to clear here because error irq is disabled. */
	msm_host->err_work_state = 0;

	/* enable dsi error interrupt */
	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_ERROR, 1);
}

static void dsi_ack_err_status(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_ACK_ERR_STATUS);

	if (status) {
		dsi_write(msm_host, REG_DSI_ACK_ERR_STATUS, status);
		/* Writing of an extra 0 needed to clear error bits */
		dsi_write(msm_host, REG_DSI_ACK_ERR_STATUS, 0);
		msm_host->err_work_state |= DSI_ERR_STATE_ACK;
	}
}

static void dsi_timeout_status(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_TIMEOUT_STATUS);

	if (status) {
		dsi_write(msm_host, REG_DSI_TIMEOUT_STATUS, status);
		msm_host->err_work_state |= DSI_ERR_STATE_TIMEOUT;
	}
}

static void dsi_dln0_phy_err(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_DLN0_PHY_ERR);

	if (status & (DSI_DLN0_PHY_ERR_DLN0_ERR_ESC |
			DSI_DLN0_PHY_ERR_DLN0_ERR_SYNC_ESC |
			DSI_DLN0_PHY_ERR_DLN0_ERR_CONTROL |
			DSI_DLN0_PHY_ERR_DLN0_ERR_CONTENTION_LP0 |
			DSI_DLN0_PHY_ERR_DLN0_ERR_CONTENTION_LP1)) {
		dsi_write(msm_host, REG_DSI_DLN0_PHY_ERR, status);
		msm_host->err_work_state |= DSI_ERR_STATE_DLN0_PHY;
	}
}

static void dsi_fifo_status(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_FIFO_STATUS);

	/* fifo underflow, overflow */
	if (status) {
		dsi_write(msm_host, REG_DSI_FIFO_STATUS, status);
		msm_host->err_work_state |= DSI_ERR_STATE_FIFO;
		if (status & DSI_FIFO_STATUS_CMD_MDP_FIFO_UNDERFLOW)
			msm_host->err_work_state |=
					DSI_ERR_STATE_MDP_FIFO_UNDERFLOW;
	}
}

static void dsi_status(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_STATUS0);

	if (status & DSI_STATUS0_INTERLEAVE_OP_CONTENTION) {
		dsi_write(msm_host, REG_DSI_STATUS0, status);
		msm_host->err_work_state |=
			DSI_ERR_STATE_INTERLEAVE_OP_CONTENTION;
	}
}

static void dsi_clk_status(struct msm_dsi_host *msm_host)
{
	u32 status;

	status = dsi_read(msm_host, REG_DSI_CLK_STATUS);

	if (status & DSI_CLK_STATUS_PLL_UNLOCKED) {
		dsi_write(msm_host, REG_DSI_CLK_STATUS, status);
		msm_host->err_work_state |= DSI_ERR_STATE_PLL_UNLOCKED;
	}
}

static void dsi_error(struct msm_dsi_host *msm_host)
{
	/* disable dsi error interrupt */
	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_ERROR, 0);

	dsi_clk_status(msm_host);
	dsi_fifo_status(msm_host);
	dsi_ack_err_status(msm_host);
	dsi_timeout_status(msm_host);
	dsi_status(msm_host);
	dsi_dln0_phy_err(msm_host);

	queue_work(msm_host->workqueue, &msm_host->err_work);
}

static irqreturn_t dsi_host_irq(int irq, void *ptr)
{
	struct msm_dsi_host *msm_host = ptr;
	u32 isr;
	unsigned long flags;

	if (!msm_host->ctrl_base)
		return IRQ_HANDLED;

	spin_lock_irqsave(&msm_host->intr_lock, flags);
	isr = dsi_read(msm_host, REG_DSI_INTR_CTRL);
	dsi_write(msm_host, REG_DSI_INTR_CTRL, isr);
	spin_unlock_irqrestore(&msm_host->intr_lock, flags);

	DBG("isr=0x%x, id=%d", isr, msm_host->id);

	if (isr & DSI_IRQ_ERROR)
		dsi_error(msm_host);

	if (isr & DSI_IRQ_VIDEO_DONE)
		complete(&msm_host->video_comp);

	if (isr & DSI_IRQ_CMD_DMA_DONE)
		complete(&msm_host->dma_comp);

	return IRQ_HANDLED;
}

static int dsi_host_init_panel_gpios(struct msm_dsi_host *msm_host,
			struct device *panel_device)
{
	msm_host->disp_en_gpio = devm_gpiod_get_optional(panel_device,
							 "disp-enable",
							 GPIOD_OUT_LOW);
	if (IS_ERR(msm_host->disp_en_gpio)) {
		DBG("cannot get disp-enable-gpios %ld",
				PTR_ERR(msm_host->disp_en_gpio));
		return PTR_ERR(msm_host->disp_en_gpio);
	}

	msm_host->te_gpio = devm_gpiod_get_optional(panel_device, "disp-te",
								GPIOD_IN);
	if (IS_ERR(msm_host->te_gpio)) {
		DBG("cannot get disp-te-gpios %ld", PTR_ERR(msm_host->te_gpio));
		return PTR_ERR(msm_host->te_gpio);
	}

	return 0;
}

static int dsi_host_attach(struct mipi_dsi_host *host,
					struct mipi_dsi_device *dsi)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	int ret;

	if (dsi->lanes > msm_host->num_data_lanes)
		return -EINVAL;

	msm_host->channel = dsi->channel;
	msm_host->lanes = dsi->lanes;
	msm_host->format = dsi->format;
	msm_host->mode_flags = dsi->mode_flags;

	msm_dsi_manager_attach_dsi_device(msm_host->id, dsi->mode_flags);

	/* Some gpios defined in panel DT need to be controlled by host */
	ret = dsi_host_init_panel_gpios(msm_host, &dsi->dev);
	if (ret)
		return ret;

	DBG("id=%d", msm_host->id);
	if (msm_host->dev)
		queue_work(msm_host->workqueue, &msm_host->hpd_work);

	return 0;
}

static int dsi_host_detach(struct mipi_dsi_host *host,
					struct mipi_dsi_device *dsi)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	msm_host->device_node = NULL;

	DBG("id=%d", msm_host->id);
	if (msm_host->dev)
		queue_work(msm_host->workqueue, &msm_host->hpd_work);

	return 0;
}

static ssize_t dsi_host_transfer(struct mipi_dsi_host *host,
					const struct mipi_dsi_msg *msg)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	int ret;

	if (!msg || !msm_host->power_on)
		return -EINVAL;

	mutex_lock(&msm_host->cmd_mutex);
	ret = msm_dsi_manager_cmd_xfer(msm_host->id, msg);
	mutex_unlock(&msm_host->cmd_mutex);

	return ret;
}

static struct mipi_dsi_host_ops dsi_host_ops = {
	.attach = dsi_host_attach,
	.detach = dsi_host_detach,
	.transfer = dsi_host_transfer,
};

/*
 * List of supported physical to logical lane mappings.
 * For example, the 2nd entry represents the following mapping:
 *
 * "3012": Logic 3->Phys 0; Logic 0->Phys 1; Logic 1->Phys 2; Logic 2->Phys 3;
 */
static const int supported_data_lane_swaps[][4] = {
	{ 0, 1, 2, 3 },
	{ 3, 0, 1, 2 },
	{ 2, 3, 0, 1 },
	{ 1, 2, 3, 0 },
	{ 0, 3, 2, 1 },
	{ 1, 0, 3, 2 },
	{ 2, 1, 0, 3 },
	{ 3, 2, 1, 0 },
};

static int dsi_host_parse_lane_data(struct msm_dsi_host *msm_host,
				    struct device_node *ep)
{
	struct device *dev = &msm_host->pdev->dev;
	struct property *prop;
	u32 lane_map[4];
	int ret, i, len, num_lanes;

	prop = of_find_property(ep, "data-lanes", &len);
	if (!prop) {
		DRM_DEV_DEBUG(dev,
			"failed to find data lane mapping, using default\n");
		return 0;
	}

	num_lanes = len / sizeof(u32);

	if (num_lanes < 1 || num_lanes > 4) {
		DRM_DEV_ERROR(dev, "bad number of data lanes\n");
		return -EINVAL;
	}

	msm_host->num_data_lanes = num_lanes;

	ret = of_property_read_u32_array(ep, "data-lanes", lane_map,
					 num_lanes);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to read lane data\n");
		return ret;
	}

	/*
	 * compare DT specified physical-logical lane mappings with the ones
	 * supported by hardware
	 */
	for (i = 0; i < ARRAY_SIZE(supported_data_lane_swaps); i++) {
		const int *swap = supported_data_lane_swaps[i];
		int j;

		/*
		 * the data-lanes array we get from DT has a logical->physical
		 * mapping. The "data lane swap" register field represents
		 * supported configurations in a physical->logical mapping.
		 * Translate the DT mapping to what we understand and find a
		 * configuration that works.
		 */
		for (j = 0; j < num_lanes; j++) {
			if (lane_map[j] < 0 || lane_map[j] > 3)
				DRM_DEV_ERROR(dev, "bad physical lane entry %u\n",
					lane_map[j]);

			if (swap[lane_map[j]] != j)
				break;
		}

		if (j == num_lanes) {
			msm_host->dlane_swap = i;
			return 0;
		}
	}

	return -EINVAL;
}

static int dsi_host_parse_dt(struct msm_dsi_host *msm_host)
{
	struct device *dev = &msm_host->pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *endpoint, *device_node;
	int ret = 0;

	/*
	 * Get the endpoint of the output port of the DSI host. In our case,
	 * this is mapped to port number with reg = 1. Don't return an error if
	 * the remote endpoint isn't defined. It's possible that there is
	 * nothing connected to the dsi output.
	 */
	endpoint = of_graph_get_endpoint_by_regs(np, 1, -1);
	if (!endpoint) {
		DRM_DEV_DEBUG(dev, "%s: no endpoint\n", __func__);
		return 0;
	}

	ret = dsi_host_parse_lane_data(msm_host, endpoint);
	if (ret) {
		DRM_DEV_ERROR(dev, "%s: invalid lane configuration %d\n",
			__func__, ret);
		ret = -EINVAL;
		goto err;
	}

	/* Get panel node from the output port's endpoint data */
	device_node = of_graph_get_remote_node(np, 1, 0);
	if (!device_node) {
		DRM_DEV_DEBUG(dev, "%s: no valid device\n", __func__);
		ret = -ENODEV;
		goto err;
	}

	msm_host->device_node = device_node;

	if (of_property_read_bool(np, "syscon-sfpb")) {
		msm_host->sfpb = syscon_regmap_lookup_by_phandle(np,
					"syscon-sfpb");
		if (IS_ERR(msm_host->sfpb)) {
			DRM_DEV_ERROR(dev, "%s: failed to get sfpb regmap\n",
				__func__);
			ret = PTR_ERR(msm_host->sfpb);
		}
	}

	of_node_put(device_node);

err:
	of_node_put(endpoint);

	return ret;
}

static int dsi_host_get_id(struct msm_dsi_host *msm_host)
{
	struct platform_device *pdev = msm_host->pdev;
	const struct msm_dsi_config *cfg = msm_host->cfg_hnd->cfg;
	struct resource *res;
	int i;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dsi_ctrl");
	if (!res)
		return -EINVAL;

	for (i = 0; i < cfg->num_dsi; i++) {
		if (cfg->io_start[i] == res->start)
			return i;
	}

	return -EINVAL;
}

int msm_dsi_host_init(struct msm_dsi *msm_dsi)
{
	struct msm_dsi_host *msm_host = NULL;
	struct platform_device *pdev = msm_dsi->pdev;
	int ret;

	msm_host = devm_kzalloc(&pdev->dev, sizeof(*msm_host), GFP_KERNEL);
	if (!msm_host) {
		pr_err("%s: FAILED: cannot alloc dsi host\n",
		       __func__);
		ret = -ENOMEM;
		goto fail;
	}

	msm_host->pdev = pdev;
	msm_dsi->host = &msm_host->base;

	ret = dsi_host_parse_dt(msm_host);
	if (ret) {
		pr_err("%s: failed to parse dt\n", __func__);
		goto fail;
	}

	msm_host->ctrl_base = msm_ioremap(pdev, "dsi_ctrl", "DSI CTRL");
	if (IS_ERR(msm_host->ctrl_base)) {
		pr_err("%s: unable to map Dsi ctrl base\n", __func__);
		ret = PTR_ERR(msm_host->ctrl_base);
		goto fail;
	}

	pm_runtime_enable(&pdev->dev);

	msm_host->cfg_hnd = dsi_get_config(msm_host);
	if (!msm_host->cfg_hnd) {
		ret = -EINVAL;
		pr_err("%s: get config failed\n", __func__);
		goto fail;
	}

	msm_host->id = dsi_host_get_id(msm_host);
	if (msm_host->id < 0) {
		ret = msm_host->id;
		pr_err("%s: unable to identify DSI host index\n", __func__);
		goto fail;
	}

	/* fixup base address by io offset */
	msm_host->ctrl_base += msm_host->cfg_hnd->cfg->io_offset;

	ret = dsi_regulator_init(msm_host);
	if (ret) {
		pr_err("%s: regulator init failed\n", __func__);
		goto fail;
	}

	ret = dsi_clk_init(msm_host);
	if (ret) {
		pr_err("%s: unable to initialize dsi clks\n", __func__);
		goto fail;
	}

	msm_host->rx_buf = devm_kzalloc(&pdev->dev, SZ_4K, GFP_KERNEL);
	if (!msm_host->rx_buf) {
		ret = -ENOMEM;
		pr_err("%s: alloc rx temp buf failed\n", __func__);
		goto fail;
	}

	init_completion(&msm_host->dma_comp);
	init_completion(&msm_host->video_comp);
	mutex_init(&msm_host->dev_mutex);
	mutex_init(&msm_host->cmd_mutex);
	spin_lock_init(&msm_host->intr_lock);

	/* setup workqueue */
	msm_host->workqueue = alloc_ordered_workqueue("dsi_drm_work", 0);
	INIT_WORK(&msm_host->err_work, dsi_err_worker);
	INIT_WORK(&msm_host->hpd_work, dsi_hpd_worker);

	msm_dsi->id = msm_host->id;

	DBG("Dsi Host %d initialized", msm_host->id);
	return 0;

fail:
	return ret;
}

void msm_dsi_host_destroy(struct mipi_dsi_host *host)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	DBG("");
	dsi_tx_buf_free(msm_host);
	if (msm_host->workqueue) {
		flush_workqueue(msm_host->workqueue);
		destroy_workqueue(msm_host->workqueue);
		msm_host->workqueue = NULL;
	}

	mutex_destroy(&msm_host->cmd_mutex);
	mutex_destroy(&msm_host->dev_mutex);

	pm_runtime_disable(&msm_host->pdev->dev);
}

int msm_dsi_host_modeset_init(struct mipi_dsi_host *host,
					struct drm_device *dev)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;
	struct platform_device *pdev = msm_host->pdev;
	int ret;

	msm_host->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (msm_host->irq < 0) {
		ret = msm_host->irq;
		DRM_DEV_ERROR(dev->dev, "failed to get irq: %d\n", ret);
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, msm_host->irq,
			dsi_host_irq, IRQF_TRIGGER_HIGH | IRQF_ONESHOT,
			"dsi_isr", msm_host);
	if (ret < 0) {
		DRM_DEV_ERROR(&pdev->dev, "failed to request IRQ%u: %d\n",
				msm_host->irq, ret);
		return ret;
	}

	msm_host->dev = dev;
	ret = cfg_hnd->ops->tx_buf_alloc(msm_host, SZ_4K);
	if (ret) {
		pr_err("%s: alloc tx gem obj failed, %d\n", __func__, ret);
		return ret;
	}

	return 0;
}

int msm_dsi_host_register(struct mipi_dsi_host *host, bool check_defer)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	int ret;

	/* Register mipi dsi host */
	if (!msm_host->registered) {
		host->dev = &msm_host->pdev->dev;
		host->ops = &dsi_host_ops;
		ret = mipi_dsi_host_register(host);
		if (ret)
			return ret;

		msm_host->registered = true;

		/* If the panel driver has not been probed after host register,
		 * we should defer the host's probe.
		 * It makes sure panel is connected when fbcon detects
		 * connector status and gets the proper display mode to
		 * create framebuffer.
		 * Don't try to defer if there is nothing connected to the dsi
		 * output
		 */
		if (check_defer && msm_host->device_node) {
			if (IS_ERR(of_drm_find_panel(msm_host->device_node)))
				if (!of_drm_find_bridge(msm_host->device_node))
					return -EPROBE_DEFER;
		}
	}

	return 0;
}

void msm_dsi_host_unregister(struct mipi_dsi_host *host)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	if (msm_host->registered) {
		mipi_dsi_host_unregister(host);
		host->dev = NULL;
		host->ops = NULL;
		msm_host->registered = false;
	}
}

int msm_dsi_host_xfer_prepare(struct mipi_dsi_host *host,
				const struct mipi_dsi_msg *msg)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;

	/* TODO: make sure dsi_cmd_mdp is idle.
	 * Since DSI6G v1.2.0, we can set DSI_TRIG_CTRL.BLOCK_DMA_WITHIN_FRAME
	 * to ask H/W to wait until cmd mdp is idle. S/W wait is not needed.
	 * How to handle the old versions? Wait for mdp cmd done?
	 */

	/*
	 * mdss interrupt is generated in mdp core clock domain
	 * mdp clock need to be enabled to receive dsi interrupt
	 */
	pm_runtime_get_sync(&msm_host->pdev->dev);
	cfg_hnd->ops->link_clk_enable(msm_host);

	/* TODO: vote for bus bandwidth */

	if (!(msg->flags & MIPI_DSI_MSG_USE_LPM))
		dsi_set_tx_power_mode(0, msm_host);

	msm_host->dma_cmd_ctrl_restore = dsi_read(msm_host, REG_DSI_CTRL);
	dsi_write(msm_host, REG_DSI_CTRL,
		msm_host->dma_cmd_ctrl_restore |
		DSI_CTRL_CMD_MODE_EN |
		DSI_CTRL_ENABLE);
	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_CMD_DMA_DONE, 1);

	return 0;
}

void msm_dsi_host_xfer_restore(struct mipi_dsi_host *host,
				const struct mipi_dsi_msg *msg)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;

	dsi_intr_ctrl(msm_host, DSI_IRQ_MASK_CMD_DMA_DONE, 0);
	dsi_write(msm_host, REG_DSI_CTRL, msm_host->dma_cmd_ctrl_restore);

	if (!(msg->flags & MIPI_DSI_MSG_USE_LPM))
		dsi_set_tx_power_mode(1, msm_host);

	/* TODO: unvote for bus bandwidth */

	cfg_hnd->ops->link_clk_disable(msm_host);
	pm_runtime_put_autosuspend(&msm_host->pdev->dev);
}

int msm_dsi_host_cmd_tx(struct mipi_dsi_host *host,
				const struct mipi_dsi_msg *msg)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	return dsi_cmds2buf_tx(msm_host, msg);
}

int msm_dsi_host_cmd_rx(struct mipi_dsi_host *host,
				const struct mipi_dsi_msg *msg)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;
	int data_byte, rx_byte, dlen, end;
	int short_response, diff, pkt_size, ret = 0;
	char cmd;
	int rlen = msg->rx_len;
	u8 *buf;

	if (rlen <= 2) {
		short_response = 1;
		pkt_size = rlen;
		rx_byte = 4;
	} else {
		short_response = 0;
		data_byte = 10;	/* first read */
		if (rlen < data_byte)
			pkt_size = rlen;
		else
			pkt_size = data_byte;
		rx_byte = data_byte + 6; /* 4 header + 2 crc */
	}

	buf = msm_host->rx_buf;
	end = 0;
	while (!end) {
		u8 tx[2] = {pkt_size & 0xff, pkt_size >> 8};
		struct mipi_dsi_msg max_pkt_size_msg = {
			.channel = msg->channel,
			.type = MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE,
			.tx_len = 2,
			.tx_buf = tx,
		};

		DBG("rlen=%d pkt_size=%d rx_byte=%d",
			rlen, pkt_size, rx_byte);

		ret = dsi_cmds2buf_tx(msm_host, &max_pkt_size_msg);
		if (ret < 2) {
			pr_err("%s: Set max pkt size failed, %d\n",
				__func__, ret);
			return -EINVAL;
		}

		if ((cfg_hnd->major == MSM_DSI_VER_MAJOR_6G) &&
			(cfg_hnd->minor >= MSM_DSI_6G_VER_MINOR_V1_1)) {
			/* Clear the RDBK_DATA registers */
			dsi_write(msm_host, REG_DSI_RDBK_DATA_CTRL,
					DSI_RDBK_DATA_CTRL_CLR);
			wmb(); /* make sure the RDBK registers are cleared */
			dsi_write(msm_host, REG_DSI_RDBK_DATA_CTRL, 0);
			wmb(); /* release cleared status before transfer */
		}

		ret = dsi_cmds2buf_tx(msm_host, msg);
		if (ret < msg->tx_len) {
			pr_err("%s: Read cmd Tx failed, %d\n", __func__, ret);
			return ret;
		}

		/*
		 * once cmd_dma_done interrupt received,
		 * return data from client is ready and stored
		 * at RDBK_DATA register already
		 * since rx fifo is 16 bytes, dcs header is kept at first loop,
		 * after that dcs header lost during shift into registers
		 */
		dlen = dsi_cmd_dma_rx(msm_host, buf, rx_byte, pkt_size);

		if (dlen <= 0)
			return 0;

		if (short_response)
			break;

		if (rlen <= data_byte) {
			diff = data_byte - rlen;
			end = 1;
		} else {
			diff = 0;
			rlen -= data_byte;
		}

		if (!end) {
			dlen -= 2; /* 2 crc */
			dlen -= diff;
			buf += dlen;	/* next start position */
			data_byte = 14;	/* NOT first read */
			if (rlen < data_byte)
				pkt_size += rlen;
			else
				pkt_size += data_byte;
			DBG("buf=%p dlen=%d diff=%d", buf, dlen, diff);
		}
	}

	/*
	 * For single Long read, if the requested rlen < 10,
	 * we need to shift the start position of rx
	 * data buffer to skip the bytes which are not
	 * updated.
	 */
	if (pkt_size < 10 && !short_response)
		buf = msm_host->rx_buf + (10 - rlen);
	else
		buf = msm_host->rx_buf;

	cmd = buf[0];
	switch (cmd) {
	case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
		pr_err("%s: rx ACK_ERR_PACLAGE\n", __func__);
		ret = 0;
		break;
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
		ret = dsi_short_read1_resp(buf, msg);
		break;
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
		ret = dsi_short_read2_resp(buf, msg);
		break;
	case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
	case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
		ret = dsi_long_read_resp(buf, msg);
		break;
	default:
		pr_warn("%s:Invalid response cmd\n", __func__);
		ret = 0;
	}

	return ret;
}

void msm_dsi_host_cmd_xfer_commit(struct mipi_dsi_host *host, u32 dma_base,
				  u32 len)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	dsi_write(msm_host, REG_DSI_DMA_BASE, dma_base);
	dsi_write(msm_host, REG_DSI_DMA_LEN, len);
	dsi_write(msm_host, REG_DSI_TRIG_DMA, 1);

	/* Make sure trigger happens */
	wmb();
}

int msm_dsi_host_set_src_pll(struct mipi_dsi_host *host,
	struct msm_dsi_pll *src_pll)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	struct clk *byte_clk_provider, *pixel_clk_provider;
	int ret;

	ret = msm_dsi_pll_get_clk_provider(src_pll,
				&byte_clk_provider, &pixel_clk_provider);
	if (ret) {
		pr_info("%s: can't get provider from pll, don't set parent\n",
			__func__);
		return 0;
	}

	ret = clk_set_parent(msm_host->byte_clk_src, byte_clk_provider);
	if (ret) {
		pr_err("%s: can't set parent to byte_clk_src. ret=%d\n",
			__func__, ret);
		goto exit;
	}

	ret = clk_set_parent(msm_host->pixel_clk_src, pixel_clk_provider);
	if (ret) {
		pr_err("%s: can't set parent to pixel_clk_src. ret=%d\n",
			__func__, ret);
		goto exit;
	}

	if (msm_host->dsi_clk_src) {
		ret = clk_set_parent(msm_host->dsi_clk_src, pixel_clk_provider);
		if (ret) {
			pr_err("%s: can't set parent to dsi_clk_src. ret=%d\n",
				__func__, ret);
			goto exit;
		}
	}

	if (msm_host->esc_clk_src) {
		ret = clk_set_parent(msm_host->esc_clk_src, byte_clk_provider);
		if (ret) {
			pr_err("%s: can't set parent to esc_clk_src. ret=%d\n",
				__func__, ret);
			goto exit;
		}
	}

exit:
	return ret;
}

void msm_dsi_host_reset_phy(struct mipi_dsi_host *host)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	DBG("");
	dsi_write(msm_host, REG_DSI_PHY_RESET, DSI_PHY_RESET_RESET);
	/* Make sure fully reset */
	wmb();
	udelay(1000);
	dsi_write(msm_host, REG_DSI_PHY_RESET, 0);
	udelay(100);
}

void msm_dsi_host_get_phy_clk_req(struct mipi_dsi_host *host,
			struct msm_dsi_phy_clk_request *clk_req,
			bool is_dual_dsi)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;
	int ret;

	ret = cfg_hnd->ops->calc_clk_rate(msm_host, is_dual_dsi);
	if (ret) {
		pr_err("%s: unable to calc clk rate, %d\n", __func__, ret);
		return;
	}

	clk_req->bitclk_rate = msm_host->byte_clk_rate * 8;
	clk_req->escclk_rate = msm_host->esc_clk_rate;
}

int msm_dsi_host_enable(struct mipi_dsi_host *host)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	dsi_op_mode_config(msm_host,
		!!(msm_host->mode_flags & MIPI_DSI_MODE_VIDEO), true);

	/* TODO: clock should be turned off for command mode,
	 * and only turned on before MDP START.
	 * This part of code should be enabled once mdp driver support it.
	 */
	/* if (msm_panel->mode == MSM_DSI_CMD_MODE) {
	 *	dsi_link_clk_disable(msm_host);
	 *	pm_runtime_put_autosuspend(&msm_host->pdev->dev);
	 * }
	 */
	msm_host->enabled = true;
	return 0;
}

int msm_dsi_host_disable(struct mipi_dsi_host *host)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	msm_host->enabled = false;
	dsi_op_mode_config(msm_host,
		!!(msm_host->mode_flags & MIPI_DSI_MODE_VIDEO), false);

	/* Since we have disabled INTF, the video engine won't stop so that
	 * the cmd engine will be blocked.
	 * Reset to disable video engine so that we can send off cmd.
	 */
	dsi_sw_reset(msm_host);

	return 0;
}

static void msm_dsi_sfpb_config(struct msm_dsi_host *msm_host, bool enable)
{
	enum sfpb_ahb_arb_master_port_en en;

	if (!msm_host->sfpb)
		return;

	en = enable ? SFPB_MASTER_PORT_ENABLE : SFPB_MASTER_PORT_DISABLE;

	regmap_update_bits(msm_host->sfpb, REG_SFPB_GPREG,
			SFPB_GPREG_MASTER_PORT_EN__MASK,
			SFPB_GPREG_MASTER_PORT_EN(en));
}

int msm_dsi_host_power_on(struct mipi_dsi_host *host,
			struct msm_dsi_phy_shared_timings *phy_shared_timings,
			bool is_dual_dsi)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;
	int ret = 0;

	mutex_lock(&msm_host->dev_mutex);
	if (msm_host->power_on) {
		DBG("dsi host already on");
		goto unlock_ret;
	}

	msm_dsi_sfpb_config(msm_host, true);

	ret = dsi_host_regulator_enable(msm_host);
	if (ret) {
		pr_err("%s:Failed to enable vregs.ret=%d\n",
			__func__, ret);
		goto unlock_ret;
	}

	pm_runtime_get_sync(&msm_host->pdev->dev);
	ret = cfg_hnd->ops->link_clk_enable(msm_host);
	if (ret) {
		pr_err("%s: failed to enable link clocks. ret=%d\n",
		       __func__, ret);
		goto fail_disable_reg;
	}

	ret = pinctrl_pm_select_default_state(&msm_host->pdev->dev);
	if (ret) {
		pr_err("%s: failed to set pinctrl default state, %d\n",
			__func__, ret);
		goto fail_disable_clk;
	}

	dsi_timing_setup(msm_host, is_dual_dsi);
	dsi_sw_reset(msm_host);
	dsi_ctrl_config(msm_host, true, phy_shared_timings);

	if (msm_host->disp_en_gpio)
		gpiod_set_value(msm_host->disp_en_gpio, 1);

	msm_host->power_on = true;
	mutex_unlock(&msm_host->dev_mutex);

	return 0;

fail_disable_clk:
	cfg_hnd->ops->link_clk_disable(msm_host);
	pm_runtime_put_autosuspend(&msm_host->pdev->dev);
fail_disable_reg:
	dsi_host_regulator_disable(msm_host);
unlock_ret:
	mutex_unlock(&msm_host->dev_mutex);
	return ret;
}

int msm_dsi_host_power_off(struct mipi_dsi_host *host)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	const struct msm_dsi_cfg_handler *cfg_hnd = msm_host->cfg_hnd;

	mutex_lock(&msm_host->dev_mutex);
	if (!msm_host->power_on) {
		DBG("dsi host already off");
		goto unlock_ret;
	}

	dsi_ctrl_config(msm_host, false, NULL);

	if (msm_host->disp_en_gpio)
		gpiod_set_value(msm_host->disp_en_gpio, 0);

	pinctrl_pm_select_sleep_state(&msm_host->pdev->dev);

	cfg_hnd->ops->link_clk_disable(msm_host);
	pm_runtime_put_autosuspend(&msm_host->pdev->dev);

	dsi_host_regulator_disable(msm_host);

	msm_dsi_sfpb_config(msm_host, false);

	DBG("-");

	msm_host->power_on = false;

unlock_ret:
	mutex_unlock(&msm_host->dev_mutex);
	return 0;
}

int msm_dsi_host_set_display_mode(struct mipi_dsi_host *host,
				  const struct drm_display_mode *mode)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	if (msm_host->mode) {
		drm_mode_destroy(msm_host->dev, msm_host->mode);
		msm_host->mode = NULL;
	}

	msm_host->mode = drm_mode_duplicate(msm_host->dev, mode);
	if (!msm_host->mode) {
		pr_err("%s: cannot duplicate mode\n", __func__);
		return -ENOMEM;
	}

	return 0;
}

struct drm_panel *msm_dsi_host_get_panel(struct mipi_dsi_host *host,
				unsigned long *panel_flags)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);
	struct drm_panel *panel;

	panel = of_drm_find_panel(msm_host->device_node);
	if (panel_flags)
			*panel_flags = msm_host->mode_flags;

	return panel;
}

struct drm_bridge *msm_dsi_host_get_bridge(struct mipi_dsi_host *host)
{
	struct msm_dsi_host *msm_host = to_msm_dsi_host(host);

	return of_drm_find_bridge(msm_host->device_node);
}
