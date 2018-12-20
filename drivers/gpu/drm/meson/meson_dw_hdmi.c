/*
 * Copyright (C) 2016 BayLibre, SAS
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/component.h>
#include <linux/of_graph.h>
#include <linux/reset.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include <drm/drmP.h>
#include <drm/drm_edid.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/bridge/dw_hdmi.h>

#include <uapi/linux/media-bus-format.h>
#include <uapi/linux/videodev2.h>

#include "meson_drv.h"
#include "meson_venc.h"
#include "meson_vclk.h"
#include "meson_dw_hdmi.h"
#include "meson_registers.h"

#define DRIVER_NAME "meson-dw-hdmi"
#define DRIVER_DESC "Amlogic Meson HDMI-TX DRM driver"

/**
 * DOC: HDMI Output
 *
 * HDMI Output is composed of :
 *
 * - A Synopsys DesignWare HDMI Controller IP
 * - A TOP control block controlling the Clocks and PHY
 * - A custom HDMI PHY in order convert video to TMDS signal
 *
 * .. code::
 *
 *    ___________________________________
 *   |            HDMI TOP               |<= HPD
 *   |___________________________________|
 *   |                  |                |
 *   |  Synopsys HDMI   |   HDMI PHY     |=> TMDS
 *   |    Controller    |________________|
 *   |___________________________________|<=> DDC
 *
 *
 * The HDMI TOP block only supports HPD sensing.
 * The Synopsys HDMI Controller interrupt is routed
 * through the TOP Block interrupt.
 * Communication to the TOP Block and the Synopsys
 * HDMI Controller is done a pair of addr+read/write
 * registers.
 * The HDMI PHY is configured by registers in the
 * HHI register block.
 *
 * Pixel data arrives in 4:4:4 format from the VENC
 * block and the VPU HDMI mux selects either the ENCI
 * encoder for the 576i or 480i formats or the ENCP
 * encoder for all the other formats including
 * interlaced HD formats.
 * The VENC uses a DVI encoder on top of the ENCI
 * or ENCP encoders to generate DVI timings for the
 * HDMI controller.
 *
 * GXBB, GXL and GXM embeds the Synopsys DesignWare
 * HDMI TX IP version 2.01a with HDCP and I2C & S/PDIF
 * audio source interfaces.
 *
 * We handle the following features :
 *
 * - HPD Rise & Fall interrupt
 * - HDMI Controller Interrupt
 * - HDMI PHY Init for 480i to 1080p60
 * - VENC & HDMI Clock setup for 480i to 1080p60
 * - VENC Mode setup for 480i to 1080p60
 *
 * What is missing :
 *
 * - PHY, Clock and Mode setup for 2k && 4k modes
 * - SDDC Scrambling mode for HDMI 2.0a
 * - HDCP Setup
 * - CEC Management
 */

/* TOP Block Communication Channel */
#define HDMITX_TOP_ADDR_REG	0x0
#define HDMITX_TOP_DATA_REG	0x4
#define HDMITX_TOP_CTRL_REG	0x8

/* Controller Communication Channel */
#define HDMITX_DWC_ADDR_REG	0x10
#define HDMITX_DWC_DATA_REG	0x14
#define HDMITX_DWC_CTRL_REG	0x18

/* HHI Registers */
#define HHI_MEM_PD_REG0		0x100 /* 0x40 */
#define HHI_HDMI_CLK_CNTL	0x1cc /* 0x73 */
#define HHI_HDMI_PHY_CNTL0	0x3a0 /* 0xe8 */
#define HHI_HDMI_PHY_CNTL1	0x3a4 /* 0xe9 */
#define HHI_HDMI_PHY_CNTL2	0x3a8 /* 0xea */
#define HHI_HDMI_PHY_CNTL3	0x3ac /* 0xeb */

static DEFINE_SPINLOCK(reg_lock);

enum meson_venc_source {
	MESON_VENC_SOURCE_NONE = 0,
	MESON_VENC_SOURCE_ENCI = 1,
	MESON_VENC_SOURCE_ENCP = 2,
};

struct meson_dw_hdmi {
	struct drm_encoder encoder;
	struct dw_hdmi_plat_data dw_plat_data;
	struct meson_drm *priv;
	struct device *dev;
	void __iomem *hdmitx;
	struct reset_control *hdmitx_apb;
	struct reset_control *hdmitx_ctrl;
	struct reset_control *hdmitx_phy;
	struct clk *hdmi_pclk;
	struct clk *venci_clk;
	struct regulator *hdmi_supply;
	u32 irq_stat;
	struct dw_hdmi *hdmi;
};
#define encoder_to_meson_dw_hdmi(x) \
	container_of(x, struct meson_dw_hdmi, encoder)

static inline int dw_hdmi_is_compatible(struct meson_dw_hdmi *dw_hdmi,
					const char *compat)
{
	return of_device_is_compatible(dw_hdmi->dev->of_node, compat);
}

/* PHY (via TOP bridge) and Controller dedicated register interface */

static unsigned int dw_hdmi_top_read(struct meson_dw_hdmi *dw_hdmi,
				     unsigned int addr)
{
	unsigned long flags;
	unsigned int data;

	spin_lock_irqsave(&reg_lock, flags);

	/* ADDR must be written twice */
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_TOP_ADDR_REG);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_TOP_ADDR_REG);

	/* Read needs a second DATA read */
	data = readl(dw_hdmi->hdmitx + HDMITX_TOP_DATA_REG);
	data = readl(dw_hdmi->hdmitx + HDMITX_TOP_DATA_REG);

	spin_unlock_irqrestore(&reg_lock, flags);

	return data;
}

static inline void dw_hdmi_top_write(struct meson_dw_hdmi *dw_hdmi,
				     unsigned int addr, unsigned int data)
{
	unsigned long flags;

	spin_lock_irqsave(&reg_lock, flags);

	/* ADDR must be written twice */
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_TOP_ADDR_REG);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_TOP_ADDR_REG);

	/* Write needs single DATA write */
	writel(data, dw_hdmi->hdmitx + HDMITX_TOP_DATA_REG);

	spin_unlock_irqrestore(&reg_lock, flags);
}

/* Helper to change specific bits in PHY registers */
static inline void dw_hdmi_top_write_bits(struct meson_dw_hdmi *dw_hdmi,
					  unsigned int addr,
					  unsigned int mask,
					  unsigned int val)
{
	unsigned int data = dw_hdmi_top_read(dw_hdmi, addr);

	data &= ~mask;
	data |= val;

	dw_hdmi_top_write(dw_hdmi, addr, data);
}

static unsigned int dw_hdmi_dwc_read(struct meson_dw_hdmi *dw_hdmi,
				     unsigned int addr)
{
	unsigned long flags;
	unsigned int data;

	spin_lock_irqsave(&reg_lock, flags);

	/* ADDR must be written twice */
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_DWC_ADDR_REG);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_DWC_ADDR_REG);

	/* Read needs a second DATA read */
	data = readl(dw_hdmi->hdmitx + HDMITX_DWC_DATA_REG);
	data = readl(dw_hdmi->hdmitx + HDMITX_DWC_DATA_REG);

	spin_unlock_irqrestore(&reg_lock, flags);

	return data;
}

static inline void dw_hdmi_dwc_write(struct meson_dw_hdmi *dw_hdmi,
				     unsigned int addr, unsigned int data)
{
	unsigned long flags;

	spin_lock_irqsave(&reg_lock, flags);

	/* ADDR must be written twice */
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_DWC_ADDR_REG);
	writel(addr & 0xffff, dw_hdmi->hdmitx + HDMITX_DWC_ADDR_REG);

	/* Write needs single DATA write */
	writel(data, dw_hdmi->hdmitx + HDMITX_DWC_DATA_REG);

	spin_unlock_irqrestore(&reg_lock, flags);
}

/* Helper to change specific bits in controller registers */
static inline void dw_hdmi_dwc_write_bits(struct meson_dw_hdmi *dw_hdmi,
					  unsigned int addr,
					  unsigned int mask,
					  unsigned int val)
{
	unsigned int data = dw_hdmi_dwc_read(dw_hdmi, addr);

	data &= ~mask;
	data |= val;

	dw_hdmi_dwc_write(dw_hdmi, addr, data);
}

/* Bridge */

/* Setup PHY bandwidth modes */
static void meson_hdmi_phy_setup_mode(struct meson_dw_hdmi *dw_hdmi,
				      struct drm_display_mode *mode)
{
	struct meson_drm *priv = dw_hdmi->priv;
	unsigned int pixel_clock = mode->clock;

	if (dw_hdmi_is_compatible(dw_hdmi, "amlogic,meson-gxl-dw-hdmi") ||
	    dw_hdmi_is_compatible(dw_hdmi, "amlogic,meson-gxm-dw-hdmi")) {
		if (pixel_clock >= 371250) {
			/* 5.94Gbps, 3.7125Gbps */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x333d3282);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x2136315b);
		} else if (pixel_clock >= 297000) {
			/* 2.97Gbps */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33303382);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x2036315b);
		} else if (pixel_clock >= 148500) {
			/* 1.485Gbps */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33303362);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x2016315b);
		} else {
			/* 742.5Mbps, and below */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33604142);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x0016315b);
		}
	} else if (dw_hdmi_is_compatible(dw_hdmi,
					 "amlogic,meson-gxbb-dw-hdmi")) {
		if (pixel_clock >= 371250) {
			/* 5.94Gbps, 3.7125Gbps */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33353245);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x2100115b);
		} else if (pixel_clock >= 297000) {
			/* 2.97Gbps */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33634283);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0xb000115b);
		} else {
			/* 1.485Gbps, and below */
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0x33632122);
			regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL3, 0x2000115b);
		}
	}
}

static inline void meson_dw_hdmi_phy_reset(struct meson_dw_hdmi *dw_hdmi)
{
	struct meson_drm *priv = dw_hdmi->priv;

	/* Enable and software reset */
	regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1, 0xf, 0xf);

	mdelay(2);

	/* Enable and unreset */
	regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1, 0xf, 0xe);

	mdelay(2);
}

static void dw_hdmi_set_vclk(struct meson_dw_hdmi *dw_hdmi,
			     struct drm_display_mode *mode)
{
	struct meson_drm *priv = dw_hdmi->priv;
	int vic = drm_match_cea_mode(mode);
	unsigned int vclk_freq;
	unsigned int venc_freq;
	unsigned int hdmi_freq;

	vclk_freq = mode->clock;

	if (!vic) {
		meson_vclk_setup(priv, MESON_VCLK_TARGET_DMT, vclk_freq,
				 vclk_freq, vclk_freq, false);
		return;
	}

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		vclk_freq *= 2;

	venc_freq = vclk_freq;
	hdmi_freq = vclk_freq;

	if (meson_venc_hdmi_venc_repeat(vic))
		venc_freq *= 2;

	vclk_freq = max(venc_freq, hdmi_freq);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		venc_freq /= 2;

	DRM_DEBUG_DRIVER("vclk:%d venc=%d hdmi=%d enci=%d\n",
		vclk_freq, venc_freq, hdmi_freq,
		priv->venc.hdmi_use_enci);

	meson_vclk_setup(priv, MESON_VCLK_TARGET_HDMI, vclk_freq,
			 venc_freq, hdmi_freq, priv->venc.hdmi_use_enci);
}

static int dw_hdmi_phy_init(struct dw_hdmi *hdmi, void *data,
			    struct drm_display_mode *mode)
{
	struct meson_dw_hdmi *dw_hdmi = (struct meson_dw_hdmi *)data;
	struct meson_drm *priv = dw_hdmi->priv;
	unsigned int wr_clk =
		readl_relaxed(priv->io_base + _REG(VPU_HDMI_SETTING));

	DRM_DEBUG_DRIVER("%d:\"%s\"\n", mode->base.id, mode->name);

	/* Enable clocks */
	regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL, 0xffff, 0x100);

	/* Bring HDMITX MEM output of power down */
	regmap_update_bits(priv->hhi, HHI_MEM_PD_REG0, 0xff << 8, 0);

	/* Bring out of reset */
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_SW_RESET,  0);

	/* Enable internal pixclk, tmds_clk, spdif_clk, i2s_clk, cecclk */
	dw_hdmi_top_write_bits(dw_hdmi, HDMITX_TOP_CLK_CNTL,
			       0x3, 0x3);
	dw_hdmi_top_write_bits(dw_hdmi, HDMITX_TOP_CLK_CNTL,
			       0x3 << 4, 0x3 << 4);

	/* Enable normal output to PHY */
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_BIST_CNTL, BIT(12));

	/* TMDS pattern setup (TOFIX pattern for 4k2k scrambling) */
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_TMDS_CLK_PTTN_01, 0x001f001f);
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_TMDS_CLK_PTTN_23, 0x001f001f);

	/* Load TMDS pattern */
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x1);
	msleep(20);
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_TMDS_CLK_PTTN_CNTL, 0x2);

	/* Setup PHY parameters */
	meson_hdmi_phy_setup_mode(dw_hdmi, mode);

	/* Setup PHY */
	regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1,
			   0xffff << 16, 0x0390 << 16);

	/* BIT_INVERT */
	if (dw_hdmi_is_compatible(dw_hdmi, "amlogic,meson-gxl-dw-hdmi") ||
	    dw_hdmi_is_compatible(dw_hdmi, "amlogic,meson-gxm-dw-hdmi"))
		regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1,
				   BIT(17), 0);
	else
		regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1,
				   BIT(17), BIT(17));

	/* Disable clock, fifo, fifo_wr */
	regmap_update_bits(priv->hhi, HHI_HDMI_PHY_CNTL1, 0xf, 0);

	msleep(100);

	/* Reset PHY 3 times in a row */
	meson_dw_hdmi_phy_reset(dw_hdmi);
	meson_dw_hdmi_phy_reset(dw_hdmi);
	meson_dw_hdmi_phy_reset(dw_hdmi);

	/* Temporary Disable VENC video stream */
	if (priv->venc.hdmi_use_enci)
		writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_EN));
	else
		writel_relaxed(0, priv->io_base + _REG(ENCP_VIDEO_EN));

	/* Temporary Disable HDMI video stream to HDMI-TX */
	writel_bits_relaxed(0x3, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));
	writel_bits_relaxed(0xf << 8, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));

	/* Re-Enable VENC video stream */
	if (priv->venc.hdmi_use_enci)
		writel_relaxed(1, priv->io_base + _REG(ENCI_VIDEO_EN));
	else
		writel_relaxed(1, priv->io_base + _REG(ENCP_VIDEO_EN));

	/* Push back HDMI clock settings */
	writel_bits_relaxed(0xf << 8, wr_clk & (0xf << 8),
			    priv->io_base + _REG(VPU_HDMI_SETTING));

	/* Enable and Select HDMI video source for HDMI-TX */
	if (priv->venc.hdmi_use_enci)
		writel_bits_relaxed(0x3, MESON_VENC_SOURCE_ENCI,
				    priv->io_base + _REG(VPU_HDMI_SETTING));
	else
		writel_bits_relaxed(0x3, MESON_VENC_SOURCE_ENCP,
				    priv->io_base + _REG(VPU_HDMI_SETTING));

	return 0;
}

static void dw_hdmi_phy_disable(struct dw_hdmi *hdmi,
				void *data)
{
	struct meson_dw_hdmi *dw_hdmi = (struct meson_dw_hdmi *)data;
	struct meson_drm *priv = dw_hdmi->priv;

	DRM_DEBUG_DRIVER("\n");

	regmap_write(priv->hhi, HHI_HDMI_PHY_CNTL0, 0);
}

static enum drm_connector_status dw_hdmi_read_hpd(struct dw_hdmi *hdmi,
			     void *data)
{
	struct meson_dw_hdmi *dw_hdmi = (struct meson_dw_hdmi *)data;

	return !!dw_hdmi_top_read(dw_hdmi, HDMITX_TOP_STAT0) ?
		connector_status_connected : connector_status_disconnected;
}

static void dw_hdmi_setup_hpd(struct dw_hdmi *hdmi,
			      void *data)
{
	struct meson_dw_hdmi *dw_hdmi = (struct meson_dw_hdmi *)data;

	/* Setup HPD Filter */
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_HPD_FILTER,
			  (0xa << 12) | 0xa0);

	/* Clear interrupts */
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_INTR_STAT_CLR,
			  HDMITX_TOP_INTR_HPD_RISE | HDMITX_TOP_INTR_HPD_FALL);

	/* Unmask interrupts */
	dw_hdmi_top_write_bits(dw_hdmi, HDMITX_TOP_INTR_MASKN,
			HDMITX_TOP_INTR_HPD_RISE | HDMITX_TOP_INTR_HPD_FALL,
			HDMITX_TOP_INTR_HPD_RISE | HDMITX_TOP_INTR_HPD_FALL);
}

static const struct dw_hdmi_phy_ops meson_dw_hdmi_phy_ops = {
	.init = dw_hdmi_phy_init,
	.disable = dw_hdmi_phy_disable,
	.read_hpd = dw_hdmi_read_hpd,
	.setup_hpd = dw_hdmi_setup_hpd,
};

static irqreturn_t dw_hdmi_top_irq(int irq, void *dev_id)
{
	struct meson_dw_hdmi *dw_hdmi = dev_id;
	u32 stat;

	stat = dw_hdmi_top_read(dw_hdmi, HDMITX_TOP_INTR_STAT);
	dw_hdmi_top_write(dw_hdmi, HDMITX_TOP_INTR_STAT_CLR, stat);

	/* HPD Events, handle in the threaded interrupt handler */
	if (stat & (HDMITX_TOP_INTR_HPD_RISE | HDMITX_TOP_INTR_HPD_FALL)) {
		dw_hdmi->irq_stat = stat;
		return IRQ_WAKE_THREAD;
	}

	/* HDMI Controller Interrupt */
	if (stat & 1)
		return IRQ_NONE;

	/* TOFIX Handle HDCP Interrupts */

	return IRQ_HANDLED;
}

/* Threaded interrupt handler to manage HPD events */
static irqreturn_t dw_hdmi_top_thread_irq(int irq, void *dev_id)
{
	struct meson_dw_hdmi *dw_hdmi = dev_id;
	u32 stat = dw_hdmi->irq_stat;

	/* HPD Events */
	if (stat & (HDMITX_TOP_INTR_HPD_RISE | HDMITX_TOP_INTR_HPD_FALL)) {
		bool hpd_connected = false;

		if (stat & HDMITX_TOP_INTR_HPD_RISE)
			hpd_connected = true;

		dw_hdmi_setup_rx_sense(dw_hdmi->hdmi, hpd_connected,
				       hpd_connected);

		drm_helper_hpd_irq_event(dw_hdmi->encoder.dev);
	}

	return IRQ_HANDLED;
}

static enum drm_mode_status
dw_hdmi_mode_valid(struct drm_connector *connector,
		   const struct drm_display_mode *mode)
{
	struct meson_drm *priv = connector->dev->dev_private;
	unsigned int vclk_freq;
	unsigned int venc_freq;
	unsigned int hdmi_freq;
	int vic = drm_match_cea_mode(mode);
	enum drm_mode_status status;

	DRM_DEBUG_DRIVER("Modeline %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x\n",
		mode->base.id, mode->name, mode->vrefresh, mode->clock,
		mode->hdisplay, mode->hsync_start,
		mode->hsync_end, mode->htotal,
		mode->vdisplay, mode->vsync_start,
		mode->vsync_end, mode->vtotal, mode->type, mode->flags);

	/* Check against non-VIC supported modes */
	if (!vic) {
		status = meson_venc_hdmi_supported_mode(mode);
		if (status != MODE_OK)
			return status;

		return meson_vclk_dmt_supported_freq(priv, mode->clock);
	/* Check against supported VIC modes */
	} else if (!meson_venc_hdmi_supported_vic(vic))
		return MODE_BAD;

	vclk_freq = mode->clock;

	/* 480i/576i needs global pixel doubling */
	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		vclk_freq *= 2;

	venc_freq = vclk_freq;
	hdmi_freq = vclk_freq;

	/* VENC double pixels for 1080i and 720p modes */
	if (meson_venc_hdmi_venc_repeat(vic))
		venc_freq *= 2;

	vclk_freq = max(venc_freq, hdmi_freq);

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		venc_freq /= 2;

	dev_dbg(connector->dev->dev, "%s: vclk:%d venc=%d hdmi=%d\n", __func__,
		vclk_freq, venc_freq, hdmi_freq);

	/* Finally filter by configurable vclk frequencies for VIC modes */
	switch (vclk_freq) {
	case 54000:
	case 74250:
	case 148500:
	case 297000:
	case 594000:
		return MODE_OK;
	}

	return MODE_CLOCK_RANGE;
}

/* Encoder */

static void meson_venc_hdmi_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs meson_venc_hdmi_encoder_funcs = {
	.destroy        = meson_venc_hdmi_encoder_destroy,
};

static int meson_venc_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	return 0;
}

static void meson_venc_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct meson_dw_hdmi *dw_hdmi = encoder_to_meson_dw_hdmi(encoder);
	struct meson_drm *priv = dw_hdmi->priv;

	DRM_DEBUG_DRIVER("\n");

	writel_bits_relaxed(0x3, 0,
			    priv->io_base + _REG(VPU_HDMI_SETTING));

	writel_relaxed(0, priv->io_base + _REG(ENCI_VIDEO_EN));
	writel_relaxed(0, priv->io_base + _REG(ENCP_VIDEO_EN));
}

static void meson_venc_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct meson_dw_hdmi *dw_hdmi = encoder_to_meson_dw_hdmi(encoder);
	struct meson_drm *priv = dw_hdmi->priv;

	DRM_DEBUG_DRIVER("%s\n", priv->venc.hdmi_use_enci ? "VENCI" : "VENCP");

	if (priv->venc.hdmi_use_enci)
		writel_relaxed(1, priv->io_base + _REG(ENCI_VIDEO_EN));
	else
		writel_relaxed(1, priv->io_base + _REG(ENCP_VIDEO_EN));
}

static void meson_venc_hdmi_encoder_mode_set(struct drm_encoder *encoder,
				   struct drm_display_mode *mode,
				   struct drm_display_mode *adjusted_mode)
{
	struct meson_dw_hdmi *dw_hdmi = encoder_to_meson_dw_hdmi(encoder);
	struct meson_drm *priv = dw_hdmi->priv;
	int vic = drm_match_cea_mode(mode);

	DRM_DEBUG_DRIVER("%d:\"%s\" vic %d\n",
			 mode->base.id, mode->name, vic);

	/* VENC + VENC-DVI Mode setup */
	meson_venc_hdmi_mode_set(priv, vic, mode);

	/* VCLK Set clock */
	dw_hdmi_set_vclk(dw_hdmi, mode);

	/* Setup YUV444 to HDMI-TX, no 10bit diphering */
	writel_relaxed(0, priv->io_base + _REG(VPU_HDMI_FMT_CTRL));
}

static const struct drm_encoder_helper_funcs
				meson_venc_hdmi_encoder_helper_funcs = {
	.atomic_check	= meson_venc_hdmi_encoder_atomic_check,
	.disable	= meson_venc_hdmi_encoder_disable,
	.enable		= meson_venc_hdmi_encoder_enable,
	.mode_set	= meson_venc_hdmi_encoder_mode_set,
};

/* DW HDMI Regmap */

static int meson_dw_hdmi_reg_read(void *context, unsigned int reg,
				  unsigned int *result)
{
	*result = dw_hdmi_dwc_read(context, reg);

	return 0;

}

static int meson_dw_hdmi_reg_write(void *context, unsigned int reg,
				   unsigned int val)
{
	dw_hdmi_dwc_write(context, reg, val);

	return 0;
}

static const struct regmap_config meson_dw_hdmi_regmap_config = {
	.reg_bits = 32,
	.val_bits = 8,
	.reg_read = meson_dw_hdmi_reg_read,
	.reg_write = meson_dw_hdmi_reg_write,
	.max_register = 0x10000,
	.fast_io = true,
};

static bool meson_hdmi_connector_is_available(struct device *dev)
{
	struct device_node *ep, *remote;

	/* HDMI Connector is on the second port, first endpoint */
	ep = of_graph_get_endpoint_by_regs(dev->of_node, 1, 0);
	if (!ep)
		return false;

	/* If the endpoint node exists, consider it enabled */
	remote = of_graph_get_remote_port(ep);
	if (remote) {
		of_node_put(ep);
		return true;
	}

	of_node_put(ep);
	of_node_put(remote);

	return false;
}

static int meson_dw_hdmi_bind(struct device *dev, struct device *master,
				void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct meson_dw_hdmi *meson_dw_hdmi;
	struct drm_device *drm = data;
	struct meson_drm *priv = drm->dev_private;
	struct dw_hdmi_plat_data *dw_plat_data;
	struct drm_encoder *encoder;
	struct resource *res;
	int irq;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	if (!meson_hdmi_connector_is_available(dev)) {
		dev_info(drm->dev, "HDMI Output connector not available\n");
		return -ENODEV;
	}

	meson_dw_hdmi = devm_kzalloc(dev, sizeof(*meson_dw_hdmi),
				     GFP_KERNEL);
	if (!meson_dw_hdmi)
		return -ENOMEM;

	meson_dw_hdmi->priv = priv;
	meson_dw_hdmi->dev = dev;
	dw_plat_data = &meson_dw_hdmi->dw_plat_data;
	encoder = &meson_dw_hdmi->encoder;

	meson_dw_hdmi->hdmi_supply = devm_regulator_get_optional(dev, "hdmi");
	if (IS_ERR(meson_dw_hdmi->hdmi_supply)) {
		if (PTR_ERR(meson_dw_hdmi->hdmi_supply) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		meson_dw_hdmi->hdmi_supply = NULL;
	} else {
		ret = regulator_enable(meson_dw_hdmi->hdmi_supply);
		if (ret)
			return ret;
	}

	meson_dw_hdmi->hdmitx_apb = devm_reset_control_get_exclusive(dev,
						"hdmitx_apb");
	if (IS_ERR(meson_dw_hdmi->hdmitx_apb)) {
		dev_err(dev, "Failed to get hdmitx_apb reset\n");
		return PTR_ERR(meson_dw_hdmi->hdmitx_apb);
	}

	meson_dw_hdmi->hdmitx_ctrl = devm_reset_control_get_exclusive(dev,
						"hdmitx");
	if (IS_ERR(meson_dw_hdmi->hdmitx_ctrl)) {
		dev_err(dev, "Failed to get hdmitx reset\n");
		return PTR_ERR(meson_dw_hdmi->hdmitx_ctrl);
	}

	meson_dw_hdmi->hdmitx_phy = devm_reset_control_get_exclusive(dev,
						"hdmitx_phy");
	if (IS_ERR(meson_dw_hdmi->hdmitx_phy)) {
		dev_err(dev, "Failed to get hdmitx_phy reset\n");
		return PTR_ERR(meson_dw_hdmi->hdmitx_phy);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	meson_dw_hdmi->hdmitx = devm_ioremap_resource(dev, res);
	if (IS_ERR(meson_dw_hdmi->hdmitx))
		return PTR_ERR(meson_dw_hdmi->hdmitx);

	meson_dw_hdmi->hdmi_pclk = devm_clk_get(dev, "isfr");
	if (IS_ERR(meson_dw_hdmi->hdmi_pclk)) {
		dev_err(dev, "Unable to get HDMI pclk\n");
		return PTR_ERR(meson_dw_hdmi->hdmi_pclk);
	}
	clk_prepare_enable(meson_dw_hdmi->hdmi_pclk);

	meson_dw_hdmi->venci_clk = devm_clk_get(dev, "venci");
	if (IS_ERR(meson_dw_hdmi->venci_clk)) {
		dev_err(dev, "Unable to get venci clk\n");
		return PTR_ERR(meson_dw_hdmi->venci_clk);
	}
	clk_prepare_enable(meson_dw_hdmi->venci_clk);

	dw_plat_data->regm = devm_regmap_init(dev, NULL, meson_dw_hdmi,
					      &meson_dw_hdmi_regmap_config);
	if (IS_ERR(dw_plat_data->regm))
		return PTR_ERR(dw_plat_data->regm);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "Failed to get hdmi top irq\n");
		return irq;
	}

	ret = devm_request_threaded_irq(dev, irq, dw_hdmi_top_irq,
					dw_hdmi_top_thread_irq, IRQF_SHARED,
					"dw_hdmi_top_irq", meson_dw_hdmi);
	if (ret) {
		dev_err(dev, "Failed to request hdmi top irq\n");
		return ret;
	}

	/* Encoder */

	drm_encoder_helper_add(encoder, &meson_venc_hdmi_encoder_helper_funcs);

	ret = drm_encoder_init(drm, encoder, &meson_venc_hdmi_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, "meson_hdmi");
	if (ret) {
		dev_err(priv->dev, "Failed to init HDMI encoder\n");
		return ret;
	}

	encoder->possible_crtcs = BIT(0);

	DRM_DEBUG_DRIVER("encoder initialized\n");

	/* Enable clocks */
	regmap_update_bits(priv->hhi, HHI_HDMI_CLK_CNTL, 0xffff, 0x100);

	/* Bring HDMITX MEM output of power down */
	regmap_update_bits(priv->hhi, HHI_MEM_PD_REG0, 0xff << 8, 0);

	/* Reset HDMITX APB & TX & PHY */
	reset_control_reset(meson_dw_hdmi->hdmitx_apb);
	reset_control_reset(meson_dw_hdmi->hdmitx_ctrl);
	reset_control_reset(meson_dw_hdmi->hdmitx_phy);

	/* Enable APB3 fail on error */
	writel_bits_relaxed(BIT(15), BIT(15),
			    meson_dw_hdmi->hdmitx + HDMITX_TOP_CTRL_REG);
	writel_bits_relaxed(BIT(15), BIT(15),
			    meson_dw_hdmi->hdmitx + HDMITX_DWC_CTRL_REG);

	/* Bring out of reset */
	dw_hdmi_top_write(meson_dw_hdmi, HDMITX_TOP_SW_RESET,  0);

	msleep(20);

	dw_hdmi_top_write(meson_dw_hdmi, HDMITX_TOP_CLK_CNTL, 0xff);

	/* Enable HDMI-TX Interrupt */
	dw_hdmi_top_write(meson_dw_hdmi, HDMITX_TOP_INTR_STAT_CLR,
			  HDMITX_TOP_INTR_CORE);

	dw_hdmi_top_write(meson_dw_hdmi, HDMITX_TOP_INTR_MASKN,
			  HDMITX_TOP_INTR_CORE);

	/* Bridge / Connector */

	dw_plat_data->mode_valid = dw_hdmi_mode_valid;
	dw_plat_data->phy_ops = &meson_dw_hdmi_phy_ops;
	dw_plat_data->phy_name = "meson_dw_hdmi_phy";
	dw_plat_data->phy_data = meson_dw_hdmi;
	dw_plat_data->input_bus_format = MEDIA_BUS_FMT_YUV8_1X24;
	dw_plat_data->input_bus_encoding = V4L2_YCBCR_ENC_709;

	platform_set_drvdata(pdev, meson_dw_hdmi);

	meson_dw_hdmi->hdmi = dw_hdmi_bind(pdev, encoder,
					   &meson_dw_hdmi->dw_plat_data);
	if (IS_ERR(meson_dw_hdmi->hdmi))
		return PTR_ERR(meson_dw_hdmi->hdmi);

	DRM_DEBUG_DRIVER("HDMI controller initialized\n");

	return 0;
}

static void meson_dw_hdmi_unbind(struct device *dev, struct device *master,
				   void *data)
{
	struct meson_dw_hdmi *meson_dw_hdmi = dev_get_drvdata(dev);

	dw_hdmi_unbind(meson_dw_hdmi->hdmi);
}

static const struct component_ops meson_dw_hdmi_ops = {
	.bind	= meson_dw_hdmi_bind,
	.unbind	= meson_dw_hdmi_unbind,
};

static int meson_dw_hdmi_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &meson_dw_hdmi_ops);
}

static int meson_dw_hdmi_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &meson_dw_hdmi_ops);

	return 0;
}

static const struct of_device_id meson_dw_hdmi_of_table[] = {
	{ .compatible = "amlogic,meson-gxbb-dw-hdmi" },
	{ .compatible = "amlogic,meson-gxl-dw-hdmi" },
	{ .compatible = "amlogic,meson-gxm-dw-hdmi" },
	{ }
};
MODULE_DEVICE_TABLE(of, meson_dw_hdmi_of_table);

static struct platform_driver meson_dw_hdmi_platform_driver = {
	.probe		= meson_dw_hdmi_probe,
	.remove		= meson_dw_hdmi_remove,
	.driver		= {
		.name		= DRIVER_NAME,
		.of_match_table	= meson_dw_hdmi_of_table,
	},
};
module_platform_driver(meson_dw_hdmi_platform_driver);

MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
