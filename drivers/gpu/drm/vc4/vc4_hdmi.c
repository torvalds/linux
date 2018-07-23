/*
 * Copyright (C) 2015 Broadcom
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * DOC: VC4 Falcon HDMI module
 *
 * The HDMI core has a state machine and a PHY.  On BCM2835, most of
 * the unit operates off of the HSM clock from CPRMAN.  It also
 * internally uses the PLLH_PIX clock for the PHY.
 *
 * HDMI infoframes are kept within a small packet ram, where each
 * packet can be individually enabled for including in a frame.
 *
 * HDMI audio is implemented entirely within the HDMI IP block.  A
 * register in the HDMI encoder takes SPDIF frames from the DMA engine
 * and transfers them over an internal MAI (multi-channel audio
 * interconnect) bus to the encoder side for insertion into the video
 * blank regions.
 *
 * The driver's HDMI encoder does not yet support power management.
 * The HDMI encoder's power domain and the HSM/pixel clocks are kept
 * continuously running, and only the HDMI logic and packet ram are
 * powered off/on at disable/enable time.
 *
 * The driver does not yet support CEC control, though the HDMI
 * encoder block has CEC support.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/i2c.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/rational.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_drm_eld.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "media/cec.h"
#include "vc4_drv.h"
#include "vc4_regs.h"

#define HSM_CLOCK_FREQ 163682864
#define CEC_CLOCK_FREQ 40000
#define CEC_CLOCK_DIV  (HSM_CLOCK_FREQ / CEC_CLOCK_FREQ)

/* HDMI audio information */
struct vc4_hdmi_audio {
	struct snd_soc_card card;
	struct snd_soc_dai_link link;
	int samplerate;
	int channels;
	struct snd_dmaengine_dai_dma_data dma_data;
	struct snd_pcm_substream *substream;
};

/* General HDMI hardware state. */
struct vc4_hdmi {
	struct platform_device *pdev;

	struct drm_encoder *encoder;
	struct drm_connector *connector;

	struct vc4_hdmi_audio audio;

	struct i2c_adapter *ddc;
	void __iomem *hdmicore_regs;
	void __iomem *hd_regs;
	int hpd_gpio;
	bool hpd_active_low;

	struct cec_adapter *cec_adap;
	struct cec_msg cec_rx_msg;
	bool cec_tx_ok;
	bool cec_irq_was_rx;

	struct clk *pixel_clock;
	struct clk *hsm_clock;
};

#define HDMI_READ(offset) readl(vc4->hdmi->hdmicore_regs + offset)
#define HDMI_WRITE(offset, val) writel(val, vc4->hdmi->hdmicore_regs + offset)
#define HD_READ(offset) readl(vc4->hdmi->hd_regs + offset)
#define HD_WRITE(offset, val) writel(val, vc4->hdmi->hd_regs + offset)

/* VC4 HDMI encoder KMS struct */
struct vc4_hdmi_encoder {
	struct vc4_encoder base;
	bool hdmi_monitor;
	bool limited_rgb_range;
	bool rgb_range_selectable;
};

static inline struct vc4_hdmi_encoder *
to_vc4_hdmi_encoder(struct drm_encoder *encoder)
{
	return container_of(encoder, struct vc4_hdmi_encoder, base.base);
}

/* VC4 HDMI connector KMS struct */
struct vc4_hdmi_connector {
	struct drm_connector base;

	/* Since the connector is attached to just the one encoder,
	 * this is the reference to it so we can do the best_encoder()
	 * hook.
	 */
	struct drm_encoder *encoder;
};

static inline struct vc4_hdmi_connector *
to_vc4_hdmi_connector(struct drm_connector *connector)
{
	return container_of(connector, struct vc4_hdmi_connector, base);
}

#define HDMI_REG(reg) { reg, #reg }
static const struct {
	u32 reg;
	const char *name;
} hdmi_regs[] = {
	HDMI_REG(VC4_HDMI_CORE_REV),
	HDMI_REG(VC4_HDMI_SW_RESET_CONTROL),
	HDMI_REG(VC4_HDMI_HOTPLUG_INT),
	HDMI_REG(VC4_HDMI_HOTPLUG),
	HDMI_REG(VC4_HDMI_MAI_CHANNEL_MAP),
	HDMI_REG(VC4_HDMI_MAI_CONFIG),
	HDMI_REG(VC4_HDMI_MAI_FORMAT),
	HDMI_REG(VC4_HDMI_AUDIO_PACKET_CONFIG),
	HDMI_REG(VC4_HDMI_RAM_PACKET_CONFIG),
	HDMI_REG(VC4_HDMI_HORZA),
	HDMI_REG(VC4_HDMI_HORZB),
	HDMI_REG(VC4_HDMI_FIFO_CTL),
	HDMI_REG(VC4_HDMI_SCHEDULER_CONTROL),
	HDMI_REG(VC4_HDMI_VERTA0),
	HDMI_REG(VC4_HDMI_VERTA1),
	HDMI_REG(VC4_HDMI_VERTB0),
	HDMI_REG(VC4_HDMI_VERTB1),
	HDMI_REG(VC4_HDMI_TX_PHY_RESET_CTL),
	HDMI_REG(VC4_HDMI_TX_PHY_CTL0),

	HDMI_REG(VC4_HDMI_CEC_CNTRL_1),
	HDMI_REG(VC4_HDMI_CEC_CNTRL_2),
	HDMI_REG(VC4_HDMI_CEC_CNTRL_3),
	HDMI_REG(VC4_HDMI_CEC_CNTRL_4),
	HDMI_REG(VC4_HDMI_CEC_CNTRL_5),
	HDMI_REG(VC4_HDMI_CPU_STATUS),
	HDMI_REG(VC4_HDMI_CPU_MASK_STATUS),

	HDMI_REG(VC4_HDMI_CEC_RX_DATA_1),
	HDMI_REG(VC4_HDMI_CEC_RX_DATA_2),
	HDMI_REG(VC4_HDMI_CEC_RX_DATA_3),
	HDMI_REG(VC4_HDMI_CEC_RX_DATA_4),
	HDMI_REG(VC4_HDMI_CEC_TX_DATA_1),
	HDMI_REG(VC4_HDMI_CEC_TX_DATA_2),
	HDMI_REG(VC4_HDMI_CEC_TX_DATA_3),
	HDMI_REG(VC4_HDMI_CEC_TX_DATA_4),
};

static const struct {
	u32 reg;
	const char *name;
} hd_regs[] = {
	HDMI_REG(VC4_HD_M_CTL),
	HDMI_REG(VC4_HD_MAI_CTL),
	HDMI_REG(VC4_HD_MAI_THR),
	HDMI_REG(VC4_HD_MAI_FMT),
	HDMI_REG(VC4_HD_MAI_SMP),
	HDMI_REG(VC4_HD_VID_CTL),
	HDMI_REG(VC4_HD_CSC_CTL),
	HDMI_REG(VC4_HD_FRAME_COUNT),
};

#ifdef CONFIG_DEBUG_FS
int vc4_hdmi_debugfs_regs(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct drm_device *dev = node->minor->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_regs); i++) {
		seq_printf(m, "%s (0x%04x): 0x%08x\n",
			   hdmi_regs[i].name, hdmi_regs[i].reg,
			   HDMI_READ(hdmi_regs[i].reg));
	}

	for (i = 0; i < ARRAY_SIZE(hd_regs); i++) {
		seq_printf(m, "%s (0x%04x): 0x%08x\n",
			   hd_regs[i].name, hd_regs[i].reg,
			   HD_READ(hd_regs[i].reg));
	}

	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static void vc4_hdmi_dump_regs(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_regs); i++) {
		DRM_INFO("0x%04x (%s): 0x%08x\n",
			 hdmi_regs[i].reg, hdmi_regs[i].name,
			 HDMI_READ(hdmi_regs[i].reg));
	}
	for (i = 0; i < ARRAY_SIZE(hd_regs); i++) {
		DRM_INFO("0x%04x (%s): 0x%08x\n",
			 hd_regs[i].reg, hd_regs[i].name,
			 HD_READ(hd_regs[i].reg));
	}
}

static enum drm_connector_status
vc4_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct drm_device *dev = connector->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	if (vc4->hdmi->hpd_gpio) {
		if (gpio_get_value_cansleep(vc4->hdmi->hpd_gpio) ^
		    vc4->hdmi->hpd_active_low)
			return connector_status_connected;
		cec_phys_addr_invalidate(vc4->hdmi->cec_adap);
		return connector_status_disconnected;
	}

	if (drm_probe_ddc(vc4->hdmi->ddc))
		return connector_status_connected;

	if (HDMI_READ(VC4_HDMI_HOTPLUG) & VC4_HDMI_HOTPLUG_CONNECTED)
		return connector_status_connected;
	cec_phys_addr_invalidate(vc4->hdmi->cec_adap);
	return connector_status_disconnected;
}

static void vc4_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int vc4_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct vc4_hdmi_connector *vc4_connector =
		to_vc4_hdmi_connector(connector);
	struct drm_encoder *encoder = vc4_connector->encoder;
	struct vc4_hdmi_encoder *vc4_encoder = to_vc4_hdmi_encoder(encoder);
	struct drm_device *dev = connector->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int ret = 0;
	struct edid *edid;

	edid = drm_get_edid(connector, vc4->hdmi->ddc);
	cec_s_phys_addr_from_edid(vc4->hdmi->cec_adap, edid);
	if (!edid)
		return -ENODEV;

	vc4_encoder->hdmi_monitor = drm_detect_hdmi_monitor(edid);

	if (edid && edid->input & DRM_EDID_INPUT_DIGITAL) {
		vc4_encoder->rgb_range_selectable =
			drm_rgb_quant_range_selectable(edid);
	}

	drm_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);

	return ret;
}

static const struct drm_connector_funcs vc4_hdmi_connector_funcs = {
	.detect = vc4_hdmi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vc4_hdmi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs vc4_hdmi_connector_helper_funcs = {
	.get_modes = vc4_hdmi_connector_get_modes,
};

static struct drm_connector *vc4_hdmi_connector_init(struct drm_device *dev,
						     struct drm_encoder *encoder)
{
	struct drm_connector *connector;
	struct vc4_hdmi_connector *hdmi_connector;

	hdmi_connector = devm_kzalloc(dev->dev, sizeof(*hdmi_connector),
				      GFP_KERNEL);
	if (!hdmi_connector)
		return ERR_PTR(-ENOMEM);
	connector = &hdmi_connector->base;

	hdmi_connector->encoder = encoder;

	drm_connector_init(dev, connector, &vc4_hdmi_connector_funcs,
			   DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(connector, &vc4_hdmi_connector_helper_funcs);

	connector->polled = (DRM_CONNECTOR_POLL_CONNECT |
			     DRM_CONNECTOR_POLL_DISCONNECT);

	connector->interlace_allowed = 1;
	connector->doublescan_allowed = 0;

	drm_connector_attach_encoder(connector, encoder);

	return connector;
}

static void vc4_hdmi_encoder_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs vc4_hdmi_encoder_funcs = {
	.destroy = vc4_hdmi_encoder_destroy,
};

static int vc4_hdmi_stop_packet(struct drm_encoder *encoder,
				enum hdmi_infoframe_type type)
{
	struct drm_device *dev = encoder->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	u32 packet_id = type - 0x80;

	HDMI_WRITE(VC4_HDMI_RAM_PACKET_CONFIG,
		   HDMI_READ(VC4_HDMI_RAM_PACKET_CONFIG) & ~BIT(packet_id));

	return wait_for(!(HDMI_READ(VC4_HDMI_RAM_PACKET_STATUS) &
			  BIT(packet_id)), 100);
}

static void vc4_hdmi_write_infoframe(struct drm_encoder *encoder,
				     union hdmi_infoframe *frame)
{
	struct drm_device *dev = encoder->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	u32 packet_id = frame->any.type - 0x80;
	u32 packet_reg = VC4_HDMI_RAM_PACKET(packet_id);
	uint8_t buffer[VC4_HDMI_PACKET_STRIDE];
	ssize_t len, i;
	int ret;

	WARN_ONCE(!(HDMI_READ(VC4_HDMI_RAM_PACKET_CONFIG) &
		    VC4_HDMI_RAM_PACKET_ENABLE),
		  "Packet RAM has to be on to store the packet.");

	len = hdmi_infoframe_pack(frame, buffer, sizeof(buffer));
	if (len < 0)
		return;

	ret = vc4_hdmi_stop_packet(encoder, frame->any.type);
	if (ret) {
		DRM_ERROR("Failed to wait for infoframe to go idle: %d\n", ret);
		return;
	}

	for (i = 0; i < len; i += 7) {
		HDMI_WRITE(packet_reg,
			   buffer[i + 0] << 0 |
			   buffer[i + 1] << 8 |
			   buffer[i + 2] << 16);
		packet_reg += 4;

		HDMI_WRITE(packet_reg,
			   buffer[i + 3] << 0 |
			   buffer[i + 4] << 8 |
			   buffer[i + 5] << 16 |
			   buffer[i + 6] << 24);
		packet_reg += 4;
	}

	HDMI_WRITE(VC4_HDMI_RAM_PACKET_CONFIG,
		   HDMI_READ(VC4_HDMI_RAM_PACKET_CONFIG) | BIT(packet_id));
	ret = wait_for((HDMI_READ(VC4_HDMI_RAM_PACKET_STATUS) &
			BIT(packet_id)), 100);
	if (ret)
		DRM_ERROR("Failed to wait for infoframe to start: %d\n", ret);
}

static void vc4_hdmi_set_avi_infoframe(struct drm_encoder *encoder)
{
	struct vc4_hdmi_encoder *vc4_encoder = to_vc4_hdmi_encoder(encoder);
	struct drm_crtc *crtc = encoder->crtc;
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	union hdmi_infoframe frame;
	int ret;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi, mode, false);
	if (ret < 0) {
		DRM_ERROR("couldn't fill AVI infoframe\n");
		return;
	}

	drm_hdmi_avi_infoframe_quant_range(&frame.avi, mode,
					   vc4_encoder->limited_rgb_range ?
					   HDMI_QUANTIZATION_RANGE_LIMITED :
					   HDMI_QUANTIZATION_RANGE_FULL,
					   vc4_encoder->rgb_range_selectable,
					   false);

	vc4_hdmi_write_infoframe(encoder, &frame);
}

static void vc4_hdmi_set_spd_infoframe(struct drm_encoder *encoder)
{
	union hdmi_infoframe frame;
	int ret;

	ret = hdmi_spd_infoframe_init(&frame.spd, "Broadcom", "Videocore");
	if (ret < 0) {
		DRM_ERROR("couldn't fill SPD infoframe\n");
		return;
	}

	frame.spd.sdi = HDMI_SPD_SDI_PC;

	vc4_hdmi_write_infoframe(encoder, &frame);
}

static void vc4_hdmi_set_audio_infoframe(struct drm_encoder *encoder)
{
	struct drm_device *drm = encoder->dev;
	struct vc4_dev *vc4 = drm->dev_private;
	struct vc4_hdmi *hdmi = vc4->hdmi;
	union hdmi_infoframe frame;
	int ret;

	ret = hdmi_audio_infoframe_init(&frame.audio);

	frame.audio.coding_type = HDMI_AUDIO_CODING_TYPE_STREAM;
	frame.audio.sample_frequency = HDMI_AUDIO_SAMPLE_FREQUENCY_STREAM;
	frame.audio.sample_size = HDMI_AUDIO_SAMPLE_SIZE_STREAM;
	frame.audio.channels = hdmi->audio.channels;

	vc4_hdmi_write_infoframe(encoder, &frame);
}

static void vc4_hdmi_set_infoframes(struct drm_encoder *encoder)
{
	vc4_hdmi_set_avi_infoframe(encoder);
	vc4_hdmi_set_spd_infoframe(encoder);
}

static void vc4_hdmi_encoder_disable(struct drm_encoder *encoder)
{
	struct drm_device *dev = encoder->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_hdmi *hdmi = vc4->hdmi;
	int ret;

	HDMI_WRITE(VC4_HDMI_RAM_PACKET_CONFIG, 0);

	HDMI_WRITE(VC4_HDMI_TX_PHY_RESET_CTL, 0xf << 16);
	HD_WRITE(VC4_HD_VID_CTL,
		 HD_READ(VC4_HD_VID_CTL) & ~VC4_HD_VID_CTL_ENABLE);

	clk_disable_unprepare(hdmi->pixel_clock);

	ret = pm_runtime_put(&hdmi->pdev->dev);
	if (ret < 0)
		DRM_ERROR("Failed to release power domain: %d\n", ret);
}

static void vc4_hdmi_encoder_enable(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct vc4_hdmi_encoder *vc4_encoder = to_vc4_hdmi_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_hdmi *hdmi = vc4->hdmi;
	bool debug_dump_regs = false;
	bool hsync_pos = mode->flags & DRM_MODE_FLAG_PHSYNC;
	bool vsync_pos = mode->flags & DRM_MODE_FLAG_PVSYNC;
	bool interlaced = mode->flags & DRM_MODE_FLAG_INTERLACE;
	u32 pixel_rep = (mode->flags & DRM_MODE_FLAG_DBLCLK) ? 2 : 1;
	u32 verta = (VC4_SET_FIELD(mode->crtc_vsync_end - mode->crtc_vsync_start,
				   VC4_HDMI_VERTA_VSP) |
		     VC4_SET_FIELD(mode->crtc_vsync_start - mode->crtc_vdisplay,
				   VC4_HDMI_VERTA_VFP) |
		     VC4_SET_FIELD(mode->crtc_vdisplay, VC4_HDMI_VERTA_VAL));
	u32 vertb = (VC4_SET_FIELD(0, VC4_HDMI_VERTB_VSPO) |
		     VC4_SET_FIELD(mode->crtc_vtotal - mode->crtc_vsync_end,
				   VC4_HDMI_VERTB_VBP));
	u32 vertb_even = (VC4_SET_FIELD(0, VC4_HDMI_VERTB_VSPO) |
			  VC4_SET_FIELD(mode->crtc_vtotal -
					mode->crtc_vsync_end -
					interlaced,
					VC4_HDMI_VERTB_VBP));
	u32 csc_ctl;
	int ret;

	ret = pm_runtime_get_sync(&hdmi->pdev->dev);
	if (ret < 0) {
		DRM_ERROR("Failed to retain power domain: %d\n", ret);
		return;
	}

	ret = clk_set_rate(hdmi->pixel_clock,
			   mode->clock * 1000 *
			   ((mode->flags & DRM_MODE_FLAG_DBLCLK) ? 2 : 1));
	if (ret) {
		DRM_ERROR("Failed to set pixel clock rate: %d\n", ret);
		return;
	}

	ret = clk_prepare_enable(hdmi->pixel_clock);
	if (ret) {
		DRM_ERROR("Failed to turn on pixel clock: %d\n", ret);
		return;
	}

	HDMI_WRITE(VC4_HDMI_SW_RESET_CONTROL,
		   VC4_HDMI_SW_RESET_HDMI |
		   VC4_HDMI_SW_RESET_FORMAT_DETECT);

	HDMI_WRITE(VC4_HDMI_SW_RESET_CONTROL, 0);

	/* PHY should be in reset, like
	 * vc4_hdmi_encoder_disable() does.
	 */
	HDMI_WRITE(VC4_HDMI_TX_PHY_RESET_CTL, 0xf << 16);

	HDMI_WRITE(VC4_HDMI_TX_PHY_RESET_CTL, 0);

	if (debug_dump_regs) {
		DRM_INFO("HDMI regs before:\n");
		vc4_hdmi_dump_regs(dev);
	}

	HD_WRITE(VC4_HD_VID_CTL, 0);

	HDMI_WRITE(VC4_HDMI_SCHEDULER_CONTROL,
		   HDMI_READ(VC4_HDMI_SCHEDULER_CONTROL) |
		   VC4_HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT |
		   VC4_HDMI_SCHEDULER_CONTROL_IGNORE_VSYNC_PREDICTS);

	HDMI_WRITE(VC4_HDMI_HORZA,
		   (vsync_pos ? VC4_HDMI_HORZA_VPOS : 0) |
		   (hsync_pos ? VC4_HDMI_HORZA_HPOS : 0) |
		   VC4_SET_FIELD(mode->hdisplay * pixel_rep,
				 VC4_HDMI_HORZA_HAP));

	HDMI_WRITE(VC4_HDMI_HORZB,
		   VC4_SET_FIELD((mode->htotal -
				  mode->hsync_end) * pixel_rep,
				 VC4_HDMI_HORZB_HBP) |
		   VC4_SET_FIELD((mode->hsync_end -
				  mode->hsync_start) * pixel_rep,
				 VC4_HDMI_HORZB_HSP) |
		   VC4_SET_FIELD((mode->hsync_start -
				  mode->hdisplay) * pixel_rep,
				 VC4_HDMI_HORZB_HFP));

	HDMI_WRITE(VC4_HDMI_VERTA0, verta);
	HDMI_WRITE(VC4_HDMI_VERTA1, verta);

	HDMI_WRITE(VC4_HDMI_VERTB0, vertb_even);
	HDMI_WRITE(VC4_HDMI_VERTB1, vertb);

	HD_WRITE(VC4_HD_VID_CTL,
		 (vsync_pos ? 0 : VC4_HD_VID_CTL_VSYNC_LOW) |
		 (hsync_pos ? 0 : VC4_HD_VID_CTL_HSYNC_LOW));

	csc_ctl = VC4_SET_FIELD(VC4_HD_CSC_CTL_ORDER_BGR,
				VC4_HD_CSC_CTL_ORDER);

	if (vc4_encoder->hdmi_monitor &&
	    drm_default_rgb_quant_range(mode) ==
	    HDMI_QUANTIZATION_RANGE_LIMITED) {
		/* CEA VICs other than #1 requre limited range RGB
		 * output unless overridden by an AVI infoframe.
		 * Apply a colorspace conversion to squash 0-255 down
		 * to 16-235.  The matrix here is:
		 *
		 * [ 0      0      0.8594 16]
		 * [ 0      0.8594 0      16]
		 * [ 0.8594 0      0      16]
		 * [ 0      0      0       1]
		 */
		csc_ctl |= VC4_HD_CSC_CTL_ENABLE;
		csc_ctl |= VC4_HD_CSC_CTL_RGB2YCC;
		csc_ctl |= VC4_SET_FIELD(VC4_HD_CSC_CTL_MODE_CUSTOM,
					 VC4_HD_CSC_CTL_MODE);

		HD_WRITE(VC4_HD_CSC_12_11, (0x000 << 16) | 0x000);
		HD_WRITE(VC4_HD_CSC_14_13, (0x100 << 16) | 0x6e0);
		HD_WRITE(VC4_HD_CSC_22_21, (0x6e0 << 16) | 0x000);
		HD_WRITE(VC4_HD_CSC_24_23, (0x100 << 16) | 0x000);
		HD_WRITE(VC4_HD_CSC_32_31, (0x000 << 16) | 0x6e0);
		HD_WRITE(VC4_HD_CSC_34_33, (0x100 << 16) | 0x000);
		vc4_encoder->limited_rgb_range = true;
	} else {
		vc4_encoder->limited_rgb_range = false;
	}

	/* The RGB order applies even when CSC is disabled. */
	HD_WRITE(VC4_HD_CSC_CTL, csc_ctl);

	HDMI_WRITE(VC4_HDMI_FIFO_CTL, VC4_HDMI_FIFO_CTL_MASTER_SLAVE_N);

	if (debug_dump_regs) {
		DRM_INFO("HDMI regs after:\n");
		vc4_hdmi_dump_regs(dev);
	}

	HD_WRITE(VC4_HD_VID_CTL,
		 HD_READ(VC4_HD_VID_CTL) |
		 VC4_HD_VID_CTL_ENABLE |
		 VC4_HD_VID_CTL_UNDERFLOW_ENABLE |
		 VC4_HD_VID_CTL_FRAME_COUNTER_RESET);

	if (vc4_encoder->hdmi_monitor) {
		HDMI_WRITE(VC4_HDMI_SCHEDULER_CONTROL,
			   HDMI_READ(VC4_HDMI_SCHEDULER_CONTROL) |
			   VC4_HDMI_SCHEDULER_CONTROL_MODE_HDMI);

		ret = wait_for(HDMI_READ(VC4_HDMI_SCHEDULER_CONTROL) &
			       VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE, 1000);
		WARN_ONCE(ret, "Timeout waiting for "
			  "VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE\n");
	} else {
		HDMI_WRITE(VC4_HDMI_RAM_PACKET_CONFIG,
			   HDMI_READ(VC4_HDMI_RAM_PACKET_CONFIG) &
			   ~(VC4_HDMI_RAM_PACKET_ENABLE));
		HDMI_WRITE(VC4_HDMI_SCHEDULER_CONTROL,
			   HDMI_READ(VC4_HDMI_SCHEDULER_CONTROL) &
			   ~VC4_HDMI_SCHEDULER_CONTROL_MODE_HDMI);

		ret = wait_for(!(HDMI_READ(VC4_HDMI_SCHEDULER_CONTROL) &
				 VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE), 1000);
		WARN_ONCE(ret, "Timeout waiting for "
			  "!VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE\n");
	}

	if (vc4_encoder->hdmi_monitor) {
		u32 drift;

		WARN_ON(!(HDMI_READ(VC4_HDMI_SCHEDULER_CONTROL) &
			  VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE));
		HDMI_WRITE(VC4_HDMI_SCHEDULER_CONTROL,
			   HDMI_READ(VC4_HDMI_SCHEDULER_CONTROL) |
			   VC4_HDMI_SCHEDULER_CONTROL_VERT_ALWAYS_KEEPOUT);

		HDMI_WRITE(VC4_HDMI_RAM_PACKET_CONFIG,
			   VC4_HDMI_RAM_PACKET_ENABLE);

		vc4_hdmi_set_infoframes(encoder);

		drift = HDMI_READ(VC4_HDMI_FIFO_CTL);
		drift &= VC4_HDMI_FIFO_VALID_WRITE_MASK;

		HDMI_WRITE(VC4_HDMI_FIFO_CTL,
			   drift & ~VC4_HDMI_FIFO_CTL_RECENTER);
		HDMI_WRITE(VC4_HDMI_FIFO_CTL,
			   drift | VC4_HDMI_FIFO_CTL_RECENTER);
		usleep_range(1000, 1100);
		HDMI_WRITE(VC4_HDMI_FIFO_CTL,
			   drift & ~VC4_HDMI_FIFO_CTL_RECENTER);
		HDMI_WRITE(VC4_HDMI_FIFO_CTL,
			   drift | VC4_HDMI_FIFO_CTL_RECENTER);

		ret = wait_for(HDMI_READ(VC4_HDMI_FIFO_CTL) &
			       VC4_HDMI_FIFO_CTL_RECENTER_DONE, 1);
		WARN_ONCE(ret, "Timeout waiting for "
			  "VC4_HDMI_FIFO_CTL_RECENTER_DONE");
	}
}

static enum drm_mode_status
vc4_hdmi_encoder_mode_valid(struct drm_encoder *crtc,
			    const struct drm_display_mode *mode)
{
	/* HSM clock must be 108% of the pixel clock.  Additionally,
	 * the AXI clock needs to be at least 25% of pixel clock, but
	 * HSM ends up being the limiting factor.
	 */
	if (mode->clock > HSM_CLOCK_FREQ / (1000 * 108 / 100))
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_encoder_helper_funcs vc4_hdmi_encoder_helper_funcs = {
	.mode_valid = vc4_hdmi_encoder_mode_valid,
	.disable = vc4_hdmi_encoder_disable,
	.enable = vc4_hdmi_encoder_enable,
};

/* HDMI audio codec callbacks */
static void vc4_hdmi_audio_set_mai_clock(struct vc4_hdmi *hdmi)
{
	struct drm_device *drm = hdmi->encoder->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	u32 hsm_clock = clk_get_rate(hdmi->hsm_clock);
	unsigned long n, m;

	rational_best_approximation(hsm_clock, hdmi->audio.samplerate,
				    VC4_HD_MAI_SMP_N_MASK >>
				    VC4_HD_MAI_SMP_N_SHIFT,
				    (VC4_HD_MAI_SMP_M_MASK >>
				     VC4_HD_MAI_SMP_M_SHIFT) + 1,
				    &n, &m);

	HD_WRITE(VC4_HD_MAI_SMP,
		 VC4_SET_FIELD(n, VC4_HD_MAI_SMP_N) |
		 VC4_SET_FIELD(m - 1, VC4_HD_MAI_SMP_M));
}

static void vc4_hdmi_set_n_cts(struct vc4_hdmi *hdmi)
{
	struct drm_encoder *encoder = hdmi->encoder;
	struct drm_crtc *crtc = encoder->crtc;
	struct drm_device *drm = encoder->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	u32 samplerate = hdmi->audio.samplerate;
	u32 n, cts;
	u64 tmp;

	n = 128 * samplerate / 1000;
	tmp = (u64)(mode->clock * 1000) * n;
	do_div(tmp, 128 * samplerate);
	cts = tmp;

	HDMI_WRITE(VC4_HDMI_CRP_CFG,
		   VC4_HDMI_CRP_CFG_EXTERNAL_CTS_EN |
		   VC4_SET_FIELD(n, VC4_HDMI_CRP_CFG_N));

	/*
	 * We could get slightly more accurate clocks in some cases by
	 * providing a CTS_1 value.  The two CTS values are alternated
	 * between based on the period fields
	 */
	HDMI_WRITE(VC4_HDMI_CTS_0, cts);
	HDMI_WRITE(VC4_HDMI_CTS_1, cts);
}

static inline struct vc4_hdmi *dai_to_hdmi(struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(dai);

	return snd_soc_card_get_drvdata(card);
}

static int vc4_hdmi_audio_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct vc4_hdmi *hdmi = dai_to_hdmi(dai);
	struct drm_encoder *encoder = hdmi->encoder;
	struct vc4_dev *vc4 = to_vc4_dev(encoder->dev);
	int ret;

	if (hdmi->audio.substream && hdmi->audio.substream != substream)
		return -EINVAL;

	hdmi->audio.substream = substream;

	/*
	 * If the HDMI encoder hasn't probed, or the encoder is
	 * currently in DVI mode, treat the codec dai as missing.
	 */
	if (!encoder->crtc || !(HDMI_READ(VC4_HDMI_RAM_PACKET_CONFIG) &
				VC4_HDMI_RAM_PACKET_ENABLE))
		return -ENODEV;

	ret = snd_pcm_hw_constraint_eld(substream->runtime,
					hdmi->connector->eld);
	if (ret)
		return ret;

	return 0;
}

static int vc4_hdmi_audio_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	return 0;
}

static void vc4_hdmi_audio_reset(struct vc4_hdmi *hdmi)
{
	struct drm_encoder *encoder = hdmi->encoder;
	struct drm_device *drm = encoder->dev;
	struct device *dev = &hdmi->pdev->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	int ret;

	ret = vc4_hdmi_stop_packet(encoder, HDMI_INFOFRAME_TYPE_AUDIO);
	if (ret)
		dev_err(dev, "Failed to stop audio infoframe: %d\n", ret);

	HD_WRITE(VC4_HD_MAI_CTL, VC4_HD_MAI_CTL_RESET);
	HD_WRITE(VC4_HD_MAI_CTL, VC4_HD_MAI_CTL_ERRORF);
	HD_WRITE(VC4_HD_MAI_CTL, VC4_HD_MAI_CTL_FLUSH);
}

static void vc4_hdmi_audio_shutdown(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct vc4_hdmi *hdmi = dai_to_hdmi(dai);

	if (substream != hdmi->audio.substream)
		return;

	vc4_hdmi_audio_reset(hdmi);

	hdmi->audio.substream = NULL;
}

/* HDMI audio codec callbacks */
static int vc4_hdmi_audio_hw_params(struct snd_pcm_substream *substream,
				    struct snd_pcm_hw_params *params,
				    struct snd_soc_dai *dai)
{
	struct vc4_hdmi *hdmi = dai_to_hdmi(dai);
	struct drm_encoder *encoder = hdmi->encoder;
	struct drm_device *drm = encoder->dev;
	struct device *dev = &hdmi->pdev->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);
	u32 audio_packet_config, channel_mask;
	u32 channel_map, i;

	if (substream != hdmi->audio.substream)
		return -EINVAL;

	dev_dbg(dev, "%s: %u Hz, %d bit, %d channels\n", __func__,
		params_rate(params), params_width(params),
		params_channels(params));

	hdmi->audio.channels = params_channels(params);
	hdmi->audio.samplerate = params_rate(params);

	HD_WRITE(VC4_HD_MAI_CTL,
		 VC4_HD_MAI_CTL_RESET |
		 VC4_HD_MAI_CTL_FLUSH |
		 VC4_HD_MAI_CTL_DLATE |
		 VC4_HD_MAI_CTL_ERRORE |
		 VC4_HD_MAI_CTL_ERRORF);

	vc4_hdmi_audio_set_mai_clock(hdmi);

	audio_packet_config =
		VC4_HDMI_AUDIO_PACKET_ZERO_DATA_ON_SAMPLE_FLAT |
		VC4_HDMI_AUDIO_PACKET_ZERO_DATA_ON_INACTIVE_CHANNELS |
		VC4_SET_FIELD(0xf, VC4_HDMI_AUDIO_PACKET_B_FRAME_IDENTIFIER);

	channel_mask = GENMASK(hdmi->audio.channels - 1, 0);
	audio_packet_config |= VC4_SET_FIELD(channel_mask,
					     VC4_HDMI_AUDIO_PACKET_CEA_MASK);

	/* Set the MAI threshold.  This logic mimics the firmware's. */
	if (hdmi->audio.samplerate > 96000) {
		HD_WRITE(VC4_HD_MAI_THR,
			 VC4_SET_FIELD(0x12, VC4_HD_MAI_THR_DREQHIGH) |
			 VC4_SET_FIELD(0x12, VC4_HD_MAI_THR_DREQLOW));
	} else if (hdmi->audio.samplerate > 48000) {
		HD_WRITE(VC4_HD_MAI_THR,
			 VC4_SET_FIELD(0x14, VC4_HD_MAI_THR_DREQHIGH) |
			 VC4_SET_FIELD(0x12, VC4_HD_MAI_THR_DREQLOW));
	} else {
		HD_WRITE(VC4_HD_MAI_THR,
			 VC4_SET_FIELD(0x10, VC4_HD_MAI_THR_PANICHIGH) |
			 VC4_SET_FIELD(0x10, VC4_HD_MAI_THR_PANICLOW) |
			 VC4_SET_FIELD(0x10, VC4_HD_MAI_THR_DREQHIGH) |
			 VC4_SET_FIELD(0x10, VC4_HD_MAI_THR_DREQLOW));
	}

	HDMI_WRITE(VC4_HDMI_MAI_CONFIG,
		   VC4_HDMI_MAI_CONFIG_BIT_REVERSE |
		   VC4_SET_FIELD(channel_mask, VC4_HDMI_MAI_CHANNEL_MASK));

	channel_map = 0;
	for (i = 0; i < 8; i++) {
		if (channel_mask & BIT(i))
			channel_map |= i << (3 * i);
	}

	HDMI_WRITE(VC4_HDMI_MAI_CHANNEL_MAP, channel_map);
	HDMI_WRITE(VC4_HDMI_AUDIO_PACKET_CONFIG, audio_packet_config);
	vc4_hdmi_set_n_cts(hdmi);

	return 0;
}

static int vc4_hdmi_audio_trigger(struct snd_pcm_substream *substream, int cmd,
				  struct snd_soc_dai *dai)
{
	struct vc4_hdmi *hdmi = dai_to_hdmi(dai);
	struct drm_encoder *encoder = hdmi->encoder;
	struct drm_device *drm = encoder->dev;
	struct vc4_dev *vc4 = to_vc4_dev(drm);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		vc4_hdmi_set_audio_infoframe(encoder);
		HDMI_WRITE(VC4_HDMI_TX_PHY_CTL0,
			   HDMI_READ(VC4_HDMI_TX_PHY_CTL0) &
			   ~VC4_HDMI_TX_PHY_RNG_PWRDN);
		HD_WRITE(VC4_HD_MAI_CTL,
			 VC4_SET_FIELD(hdmi->audio.channels,
				       VC4_HD_MAI_CTL_CHNUM) |
			 VC4_HD_MAI_CTL_ENABLE);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		HD_WRITE(VC4_HD_MAI_CTL,
			 VC4_HD_MAI_CTL_DLATE |
			 VC4_HD_MAI_CTL_ERRORE |
			 VC4_HD_MAI_CTL_ERRORF);
		HDMI_WRITE(VC4_HDMI_TX_PHY_CTL0,
			   HDMI_READ(VC4_HDMI_TX_PHY_CTL0) |
			   VC4_HDMI_TX_PHY_RNG_PWRDN);
		break;
	default:
		break;
	}

	return 0;
}

static inline struct vc4_hdmi *
snd_component_to_hdmi(struct snd_soc_component *component)
{
	struct snd_soc_card *card = snd_soc_component_get_drvdata(component);

	return snd_soc_card_get_drvdata(card);
}

static int vc4_hdmi_audio_eld_ctl_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct vc4_hdmi *hdmi = snd_component_to_hdmi(component);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = sizeof(hdmi->connector->eld);

	return 0;
}

static int vc4_hdmi_audio_eld_ctl_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct vc4_hdmi *hdmi = snd_component_to_hdmi(component);

	memcpy(ucontrol->value.bytes.data, hdmi->connector->eld,
	       sizeof(hdmi->connector->eld));

	return 0;
}

static const struct snd_kcontrol_new vc4_hdmi_audio_controls[] = {
	{
		.access = SNDRV_CTL_ELEM_ACCESS_READ |
			  SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = "ELD",
		.info = vc4_hdmi_audio_eld_ctl_info,
		.get = vc4_hdmi_audio_eld_ctl_get,
	},
};

static const struct snd_soc_dapm_widget vc4_hdmi_audio_widgets[] = {
	SND_SOC_DAPM_OUTPUT("TX"),
};

static const struct snd_soc_dapm_route vc4_hdmi_audio_routes[] = {
	{ "TX", NULL, "Playback" },
};

static const struct snd_soc_component_driver vc4_hdmi_audio_component_drv = {
	.controls		= vc4_hdmi_audio_controls,
	.num_controls		= ARRAY_SIZE(vc4_hdmi_audio_controls),
	.dapm_widgets		= vc4_hdmi_audio_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(vc4_hdmi_audio_widgets),
	.dapm_routes		= vc4_hdmi_audio_routes,
	.num_dapm_routes	= ARRAY_SIZE(vc4_hdmi_audio_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct snd_soc_dai_ops vc4_hdmi_audio_dai_ops = {
	.startup = vc4_hdmi_audio_startup,
	.shutdown = vc4_hdmi_audio_shutdown,
	.hw_params = vc4_hdmi_audio_hw_params,
	.set_fmt = vc4_hdmi_audio_set_fmt,
	.trigger = vc4_hdmi_audio_trigger,
};

static struct snd_soc_dai_driver vc4_hdmi_audio_codec_dai_drv = {
	.name = "vc4-hdmi-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			 SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE,
	},
};

static const struct snd_soc_component_driver vc4_hdmi_audio_cpu_dai_comp = {
	.name = "vc4-hdmi-cpu-dai-component",
};

static int vc4_hdmi_audio_cpu_dai_probe(struct snd_soc_dai *dai)
{
	struct vc4_hdmi *hdmi = dai_to_hdmi(dai);

	snd_soc_dai_init_dma_data(dai, &hdmi->audio.dma_data, NULL);

	return 0;
}

static struct snd_soc_dai_driver vc4_hdmi_audio_cpu_dai_drv = {
	.name = "vc4-hdmi-cpu-dai",
	.probe  = vc4_hdmi_audio_cpu_dai_probe,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
			 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
			 SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
			 SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE,
	},
	.ops = &vc4_hdmi_audio_dai_ops,
};

static const struct snd_dmaengine_pcm_config pcm_conf = {
	.chan_names[SNDRV_PCM_STREAM_PLAYBACK] = "audio-rx",
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
};

static int vc4_hdmi_audio_init(struct vc4_hdmi *hdmi)
{
	struct snd_soc_dai_link *dai_link = &hdmi->audio.link;
	struct snd_soc_card *card = &hdmi->audio.card;
	struct device *dev = &hdmi->pdev->dev;
	const __be32 *addr;
	int ret;

	if (!of_find_property(dev->of_node, "dmas", NULL)) {
		dev_warn(dev,
			 "'dmas' DT property is missing, no HDMI audio\n");
		return 0;
	}

	/*
	 * Get the physical address of VC4_HD_MAI_DATA. We need to retrieve
	 * the bus address specified in the DT, because the physical address
	 * (the one returned by platform_get_resource()) is not appropriate
	 * for DMA transfers.
	 * This VC/MMU should probably be exposed to avoid this kind of hacks.
	 */
	addr = of_get_address(dev->of_node, 1, NULL, NULL);
	hdmi->audio.dma_data.addr = be32_to_cpup(addr) + VC4_HD_MAI_DATA;
	hdmi->audio.dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	hdmi->audio.dma_data.maxburst = 2;

	ret = devm_snd_dmaengine_pcm_register(dev, &pcm_conf, 0);
	if (ret) {
		dev_err(dev, "Could not register PCM component: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(dev, &vc4_hdmi_audio_cpu_dai_comp,
					      &vc4_hdmi_audio_cpu_dai_drv, 1);
	if (ret) {
		dev_err(dev, "Could not register CPU DAI: %d\n", ret);
		return ret;
	}

	/* register component and codec dai */
	ret = devm_snd_soc_register_component(dev, &vc4_hdmi_audio_component_drv,
				     &vc4_hdmi_audio_codec_dai_drv, 1);
	if (ret) {
		dev_err(dev, "Could not register component: %d\n", ret);
		return ret;
	}

	dai_link->name = "MAI";
	dai_link->stream_name = "MAI PCM";
	dai_link->codec_dai_name = vc4_hdmi_audio_codec_dai_drv.name;
	dai_link->cpu_dai_name = dev_name(dev);
	dai_link->codec_name = dev_name(dev);
	dai_link->platform_name = dev_name(dev);

	card->dai_link = dai_link;
	card->num_links = 1;
	card->name = "vc4-hdmi";
	card->dev = dev;

	/*
	 * Be careful, snd_soc_register_card() calls dev_set_drvdata() and
	 * stores a pointer to the snd card object in dev->driver_data. This
	 * means we cannot use it for something else. The hdmi back-pointer is
	 * now stored in card->drvdata and should be retrieved with
	 * snd_soc_card_get_drvdata() if needed.
	 */
	snd_soc_card_set_drvdata(card, hdmi);
	ret = devm_snd_soc_register_card(dev, card);
	if (ret)
		dev_err(dev, "Could not register sound card: %d\n", ret);

	return ret;

}

#ifdef CONFIG_DRM_VC4_HDMI_CEC
static irqreturn_t vc4_cec_irq_handler_thread(int irq, void *priv)
{
	struct vc4_dev *vc4 = priv;
	struct vc4_hdmi *hdmi = vc4->hdmi;

	if (hdmi->cec_irq_was_rx) {
		if (hdmi->cec_rx_msg.len)
			cec_received_msg(hdmi->cec_adap, &hdmi->cec_rx_msg);
	} else if (hdmi->cec_tx_ok) {
		cec_transmit_done(hdmi->cec_adap, CEC_TX_STATUS_OK,
				  0, 0, 0, 0);
	} else {
		/*
		 * This CEC implementation makes 1 retry, so if we
		 * get a NACK, then that means it made 2 attempts.
		 */
		cec_transmit_done(hdmi->cec_adap, CEC_TX_STATUS_NACK,
				  0, 2, 0, 0);
	}
	return IRQ_HANDLED;
}

static void vc4_cec_read_msg(struct vc4_dev *vc4, u32 cntrl1)
{
	struct cec_msg *msg = &vc4->hdmi->cec_rx_msg;
	unsigned int i;

	msg->len = 1 + ((cntrl1 & VC4_HDMI_CEC_REC_WRD_CNT_MASK) >>
					VC4_HDMI_CEC_REC_WRD_CNT_SHIFT);
	for (i = 0; i < msg->len; i += 4) {
		u32 val = HDMI_READ(VC4_HDMI_CEC_RX_DATA_1 + i);

		msg->msg[i] = val & 0xff;
		msg->msg[i + 1] = (val >> 8) & 0xff;
		msg->msg[i + 2] = (val >> 16) & 0xff;
		msg->msg[i + 3] = (val >> 24) & 0xff;
	}
}

static irqreturn_t vc4_cec_irq_handler(int irq, void *priv)
{
	struct vc4_dev *vc4 = priv;
	struct vc4_hdmi *hdmi = vc4->hdmi;
	u32 stat = HDMI_READ(VC4_HDMI_CPU_STATUS);
	u32 cntrl1, cntrl5;

	if (!(stat & VC4_HDMI_CPU_CEC))
		return IRQ_NONE;
	hdmi->cec_rx_msg.len = 0;
	cntrl1 = HDMI_READ(VC4_HDMI_CEC_CNTRL_1);
	cntrl5 = HDMI_READ(VC4_HDMI_CEC_CNTRL_5);
	hdmi->cec_irq_was_rx = cntrl5 & VC4_HDMI_CEC_RX_CEC_INT;
	if (hdmi->cec_irq_was_rx) {
		vc4_cec_read_msg(vc4, cntrl1);
		cntrl1 |= VC4_HDMI_CEC_CLEAR_RECEIVE_OFF;
		HDMI_WRITE(VC4_HDMI_CEC_CNTRL_1, cntrl1);
		cntrl1 &= ~VC4_HDMI_CEC_CLEAR_RECEIVE_OFF;
	} else {
		hdmi->cec_tx_ok = cntrl1 & VC4_HDMI_CEC_TX_STATUS_GOOD;
		cntrl1 &= ~VC4_HDMI_CEC_START_XMIT_BEGIN;
	}
	HDMI_WRITE(VC4_HDMI_CEC_CNTRL_1, cntrl1);
	HDMI_WRITE(VC4_HDMI_CPU_CLEAR, VC4_HDMI_CPU_CEC);

	return IRQ_WAKE_THREAD;
}

static int vc4_hdmi_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct vc4_dev *vc4 = cec_get_drvdata(adap);
	/* clock period in microseconds */
	const u32 usecs = 1000000 / CEC_CLOCK_FREQ;
	u32 val = HDMI_READ(VC4_HDMI_CEC_CNTRL_5);

	val &= ~(VC4_HDMI_CEC_TX_SW_RESET | VC4_HDMI_CEC_RX_SW_RESET |
		 VC4_HDMI_CEC_CNT_TO_4700_US_MASK |
		 VC4_HDMI_CEC_CNT_TO_4500_US_MASK);
	val |= ((4700 / usecs) << VC4_HDMI_CEC_CNT_TO_4700_US_SHIFT) |
	       ((4500 / usecs) << VC4_HDMI_CEC_CNT_TO_4500_US_SHIFT);

	if (enable) {
		HDMI_WRITE(VC4_HDMI_CEC_CNTRL_5, val |
			   VC4_HDMI_CEC_TX_SW_RESET | VC4_HDMI_CEC_RX_SW_RESET);
		HDMI_WRITE(VC4_HDMI_CEC_CNTRL_5, val);
		HDMI_WRITE(VC4_HDMI_CEC_CNTRL_2,
			 ((1500 / usecs) << VC4_HDMI_CEC_CNT_TO_1500_US_SHIFT) |
			 ((1300 / usecs) << VC4_HDMI_CEC_CNT_TO_1300_US_SHIFT) |
			 ((800 / usecs) << VC4_HDMI_CEC_CNT_TO_800_US_SHIFT) |
			 ((600 / usecs) << VC4_HDMI_CEC_CNT_TO_600_US_SHIFT) |
			 ((400 / usecs) << VC4_HDMI_CEC_CNT_TO_400_US_SHIFT));
		HDMI_WRITE(VC4_HDMI_CEC_CNTRL_3,
			 ((2750 / usecs) << VC4_HDMI_CEC_CNT_TO_2750_US_SHIFT) |
			 ((2400 / usecs) << VC4_HDMI_CEC_CNT_TO_2400_US_SHIFT) |
			 ((2050 / usecs) << VC4_HDMI_CEC_CNT_TO_2050_US_SHIFT) |
			 ((1700 / usecs) << VC4_HDMI_CEC_CNT_TO_1700_US_SHIFT));
		HDMI_WRITE(VC4_HDMI_CEC_CNTRL_4,
			 ((4300 / usecs) << VC4_HDMI_CEC_CNT_TO_4300_US_SHIFT) |
			 ((3900 / usecs) << VC4_HDMI_CEC_CNT_TO_3900_US_SHIFT) |
			 ((3600 / usecs) << VC4_HDMI_CEC_CNT_TO_3600_US_SHIFT) |
			 ((3500 / usecs) << VC4_HDMI_CEC_CNT_TO_3500_US_SHIFT));

		HDMI_WRITE(VC4_HDMI_CPU_MASK_CLEAR, VC4_HDMI_CPU_CEC);
	} else {
		HDMI_WRITE(VC4_HDMI_CPU_MASK_SET, VC4_HDMI_CPU_CEC);
		HDMI_WRITE(VC4_HDMI_CEC_CNTRL_5, val |
			   VC4_HDMI_CEC_TX_SW_RESET | VC4_HDMI_CEC_RX_SW_RESET);
	}
	return 0;
}

static int vc4_hdmi_cec_adap_log_addr(struct cec_adapter *adap, u8 log_addr)
{
	struct vc4_dev *vc4 = cec_get_drvdata(adap);

	HDMI_WRITE(VC4_HDMI_CEC_CNTRL_1,
		   (HDMI_READ(VC4_HDMI_CEC_CNTRL_1) & ~VC4_HDMI_CEC_ADDR_MASK) |
		   (log_addr & 0xf) << VC4_HDMI_CEC_ADDR_SHIFT);
	return 0;
}

static int vc4_hdmi_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				      u32 signal_free_time, struct cec_msg *msg)
{
	struct vc4_dev *vc4 = cec_get_drvdata(adap);
	u32 val;
	unsigned int i;

	for (i = 0; i < msg->len; i += 4)
		HDMI_WRITE(VC4_HDMI_CEC_TX_DATA_1 + i,
			   (msg->msg[i]) |
			   (msg->msg[i + 1] << 8) |
			   (msg->msg[i + 2] << 16) |
			   (msg->msg[i + 3] << 24));

	val = HDMI_READ(VC4_HDMI_CEC_CNTRL_1);
	val &= ~VC4_HDMI_CEC_START_XMIT_BEGIN;
	HDMI_WRITE(VC4_HDMI_CEC_CNTRL_1, val);
	val &= ~VC4_HDMI_CEC_MESSAGE_LENGTH_MASK;
	val |= (msg->len - 1) << VC4_HDMI_CEC_MESSAGE_LENGTH_SHIFT;
	val |= VC4_HDMI_CEC_START_XMIT_BEGIN;

	HDMI_WRITE(VC4_HDMI_CEC_CNTRL_1, val);
	return 0;
}

static const struct cec_adap_ops vc4_hdmi_cec_adap_ops = {
	.adap_enable = vc4_hdmi_cec_adap_enable,
	.adap_log_addr = vc4_hdmi_cec_adap_log_addr,
	.adap_transmit = vc4_hdmi_cec_adap_transmit,
};
#endif

static int vc4_hdmi_bind(struct device *dev, struct device *master, void *data)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = drm->dev_private;
	struct vc4_hdmi *hdmi;
	struct vc4_hdmi_encoder *vc4_hdmi_encoder;
	struct device_node *ddc_node;
	u32 value;
	int ret;

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	vc4_hdmi_encoder = devm_kzalloc(dev, sizeof(*vc4_hdmi_encoder),
					GFP_KERNEL);
	if (!vc4_hdmi_encoder)
		return -ENOMEM;
	vc4_hdmi_encoder->base.type = VC4_ENCODER_TYPE_HDMI;
	hdmi->encoder = &vc4_hdmi_encoder->base.base;

	hdmi->pdev = pdev;
	hdmi->hdmicore_regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(hdmi->hdmicore_regs))
		return PTR_ERR(hdmi->hdmicore_regs);

	hdmi->hd_regs = vc4_ioremap_regs(pdev, 1);
	if (IS_ERR(hdmi->hd_regs))
		return PTR_ERR(hdmi->hd_regs);

	hdmi->pixel_clock = devm_clk_get(dev, "pixel");
	if (IS_ERR(hdmi->pixel_clock)) {
		DRM_ERROR("Failed to get pixel clock\n");
		return PTR_ERR(hdmi->pixel_clock);
	}
	hdmi->hsm_clock = devm_clk_get(dev, "hdmi");
	if (IS_ERR(hdmi->hsm_clock)) {
		DRM_ERROR("Failed to get HDMI state machine clock\n");
		return PTR_ERR(hdmi->hsm_clock);
	}

	ddc_node = of_parse_phandle(dev->of_node, "ddc", 0);
	if (!ddc_node) {
		DRM_ERROR("Failed to find ddc node in device tree\n");
		return -ENODEV;
	}

	hdmi->ddc = of_find_i2c_adapter_by_node(ddc_node);
	of_node_put(ddc_node);
	if (!hdmi->ddc) {
		DRM_DEBUG("Failed to get ddc i2c adapter by node\n");
		return -EPROBE_DEFER;
	}

	/* This is the rate that is set by the firmware.  The number
	 * needs to be a bit higher than the pixel clock rate
	 * (generally 148.5Mhz).
	 */
	ret = clk_set_rate(hdmi->hsm_clock, HSM_CLOCK_FREQ);
	if (ret) {
		DRM_ERROR("Failed to set HSM clock rate: %d\n", ret);
		goto err_put_i2c;
	}

	ret = clk_prepare_enable(hdmi->hsm_clock);
	if (ret) {
		DRM_ERROR("Failed to turn on HDMI state machine clock: %d\n",
			  ret);
		goto err_put_i2c;
	}

	/* Only use the GPIO HPD pin if present in the DT, otherwise
	 * we'll use the HDMI core's register.
	 */
	if (of_find_property(dev->of_node, "hpd-gpios", &value)) {
		enum of_gpio_flags hpd_gpio_flags;

		hdmi->hpd_gpio = of_get_named_gpio_flags(dev->of_node,
							 "hpd-gpios", 0,
							 &hpd_gpio_flags);
		if (hdmi->hpd_gpio < 0) {
			ret = hdmi->hpd_gpio;
			goto err_unprepare_hsm;
		}

		hdmi->hpd_active_low = hpd_gpio_flags & OF_GPIO_ACTIVE_LOW;
	}

	vc4->hdmi = hdmi;

	/* HDMI core must be enabled. */
	if (!(HD_READ(VC4_HD_M_CTL) & VC4_HD_M_ENABLE)) {
		HD_WRITE(VC4_HD_M_CTL, VC4_HD_M_SW_RST);
		udelay(1);
		HD_WRITE(VC4_HD_M_CTL, 0);

		HD_WRITE(VC4_HD_M_CTL, VC4_HD_M_ENABLE);
	}
	pm_runtime_enable(dev);

	drm_encoder_init(drm, hdmi->encoder, &vc4_hdmi_encoder_funcs,
			 DRM_MODE_ENCODER_TMDS, NULL);
	drm_encoder_helper_add(hdmi->encoder, &vc4_hdmi_encoder_helper_funcs);

	hdmi->connector = vc4_hdmi_connector_init(drm, hdmi->encoder);
	if (IS_ERR(hdmi->connector)) {
		ret = PTR_ERR(hdmi->connector);
		goto err_destroy_encoder;
	}
#ifdef CONFIG_DRM_VC4_HDMI_CEC
	hdmi->cec_adap = cec_allocate_adapter(&vc4_hdmi_cec_adap_ops,
					      vc4, "vc4",
					      CEC_CAP_TRANSMIT |
					      CEC_CAP_LOG_ADDRS |
					      CEC_CAP_PASSTHROUGH |
					      CEC_CAP_RC, 1);
	ret = PTR_ERR_OR_ZERO(hdmi->cec_adap);
	if (ret < 0)
		goto err_destroy_conn;
	HDMI_WRITE(VC4_HDMI_CPU_MASK_SET, 0xffffffff);
	value = HDMI_READ(VC4_HDMI_CEC_CNTRL_1);
	value &= ~VC4_HDMI_CEC_DIV_CLK_CNT_MASK;
	/*
	 * Set the logical address to Unregistered and set the clock
	 * divider: the hsm_clock rate and this divider setting will
	 * give a 40 kHz CEC clock.
	 */
	value |= VC4_HDMI_CEC_ADDR_MASK |
		 (4091 << VC4_HDMI_CEC_DIV_CLK_CNT_SHIFT);
	HDMI_WRITE(VC4_HDMI_CEC_CNTRL_1, value);
	ret = devm_request_threaded_irq(dev, platform_get_irq(pdev, 0),
					vc4_cec_irq_handler,
					vc4_cec_irq_handler_thread, 0,
					"vc4 hdmi cec", vc4);
	if (ret)
		goto err_delete_cec_adap;
	ret = cec_register_adapter(hdmi->cec_adap, dev);
	if (ret < 0)
		goto err_delete_cec_adap;
#endif

	ret = vc4_hdmi_audio_init(hdmi);
	if (ret)
		goto err_destroy_encoder;

	return 0;

#ifdef CONFIG_DRM_VC4_HDMI_CEC
err_delete_cec_adap:
	cec_delete_adapter(hdmi->cec_adap);
err_destroy_conn:
	vc4_hdmi_connector_destroy(hdmi->connector);
#endif
err_destroy_encoder:
	vc4_hdmi_encoder_destroy(hdmi->encoder);
err_unprepare_hsm:
	clk_disable_unprepare(hdmi->hsm_clock);
	pm_runtime_disable(dev);
err_put_i2c:
	put_device(&hdmi->ddc->dev);

	return ret;
}

static void vc4_hdmi_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_dev *vc4 = drm->dev_private;
	struct vc4_hdmi *hdmi = vc4->hdmi;

	cec_unregister_adapter(hdmi->cec_adap);
	vc4_hdmi_connector_destroy(hdmi->connector);
	vc4_hdmi_encoder_destroy(hdmi->encoder);

	clk_disable_unprepare(hdmi->hsm_clock);
	pm_runtime_disable(dev);

	put_device(&hdmi->ddc->dev);

	vc4->hdmi = NULL;
}

static const struct component_ops vc4_hdmi_ops = {
	.bind   = vc4_hdmi_bind,
	.unbind = vc4_hdmi_unbind,
};

static int vc4_hdmi_dev_probe(struct platform_device *pdev)
{
	return component_add(&pdev->dev, &vc4_hdmi_ops);
}

static int vc4_hdmi_dev_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &vc4_hdmi_ops);
	return 0;
}

static const struct of_device_id vc4_hdmi_dt_match[] = {
	{ .compatible = "brcm,bcm2835-hdmi" },
	{}
};

struct platform_driver vc4_hdmi_driver = {
	.probe = vc4_hdmi_dev_probe,
	.remove = vc4_hdmi_dev_remove,
	.driver = {
		.name = "vc4_hdmi",
		.of_match_table = vc4_hdmi_dt_match,
	},
};
