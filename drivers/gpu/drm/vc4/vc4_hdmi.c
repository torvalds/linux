// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Broadcom
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
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
#include <drm/drm_edid.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_scdc_helper.h>
#include <linux/clk.h>
#include <linux/component.h>
#include <linux/i2c.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/pm_runtime.h>
#include <linux/rational.h>
#include <linux/reset.h>
#include <sound/dmaengine_pcm.h>
#include <sound/hdmi-codec.h>
#include <sound/pcm_drm_eld.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "media/cec.h"
#include "vc4_drv.h"
#include "vc4_hdmi.h"
#include "vc4_hdmi_regs.h"
#include "vc4_regs.h"

#define VC5_HDMI_HORZA_HFP_SHIFT		16
#define VC5_HDMI_HORZA_HFP_MASK			VC4_MASK(28, 16)
#define VC5_HDMI_HORZA_VPOS			BIT(15)
#define VC5_HDMI_HORZA_HPOS			BIT(14)
#define VC5_HDMI_HORZA_HAP_SHIFT		0
#define VC5_HDMI_HORZA_HAP_MASK			VC4_MASK(13, 0)

#define VC5_HDMI_HORZB_HBP_SHIFT		16
#define VC5_HDMI_HORZB_HBP_MASK			VC4_MASK(26, 16)
#define VC5_HDMI_HORZB_HSP_SHIFT		0
#define VC5_HDMI_HORZB_HSP_MASK			VC4_MASK(10, 0)

#define VC5_HDMI_VERTA_VSP_SHIFT		24
#define VC5_HDMI_VERTA_VSP_MASK			VC4_MASK(28, 24)
#define VC5_HDMI_VERTA_VFP_SHIFT		16
#define VC5_HDMI_VERTA_VFP_MASK			VC4_MASK(22, 16)
#define VC5_HDMI_VERTA_VAL_SHIFT		0
#define VC5_HDMI_VERTA_VAL_MASK			VC4_MASK(12, 0)

#define VC5_HDMI_VERTB_VSPO_SHIFT		16
#define VC5_HDMI_VERTB_VSPO_MASK		VC4_MASK(29, 16)

#define VC5_HDMI_SCRAMBLER_CTL_ENABLE		BIT(0)

#define VC5_HDMI_DEEP_COLOR_CONFIG_1_INIT_PACK_PHASE_SHIFT	8
#define VC5_HDMI_DEEP_COLOR_CONFIG_1_INIT_PACK_PHASE_MASK	VC4_MASK(10, 8)

#define VC5_HDMI_DEEP_COLOR_CONFIG_1_COLOR_DEPTH_SHIFT		0
#define VC5_HDMI_DEEP_COLOR_CONFIG_1_COLOR_DEPTH_MASK		VC4_MASK(3, 0)

#define VC5_HDMI_GCP_CONFIG_GCP_ENABLE		BIT(31)

#define VC5_HDMI_GCP_WORD_1_GCP_SUBPACKET_BYTE_1_SHIFT	8
#define VC5_HDMI_GCP_WORD_1_GCP_SUBPACKET_BYTE_1_MASK	VC4_MASK(15, 8)

# define VC4_HD_M_SW_RST			BIT(2)
# define VC4_HD_M_ENABLE			BIT(0)

#define CEC_CLOCK_FREQ 40000

#define HDMI_14_MAX_TMDS_CLK   (340 * 1000 * 1000)

static bool vc4_hdmi_mode_needs_scrambling(const struct drm_display_mode *mode)
{
	return (mode->clock * 1000) > HDMI_14_MAX_TMDS_CLK;
}

static int vc4_hdmi_debugfs_regs(struct seq_file *m, void *unused)
{
	struct drm_info_node *node = (struct drm_info_node *)m->private;
	struct vc4_hdmi *vc4_hdmi = node->info_ent->data;
	struct drm_printer p = drm_seq_file_printer(m);

	drm_print_regset32(&p, &vc4_hdmi->hdmi_regset);
	drm_print_regset32(&p, &vc4_hdmi->hd_regset);

	return 0;
}

static void vc4_hdmi_reset(struct vc4_hdmi *vc4_hdmi)
{
	HDMI_WRITE(HDMI_M_CTL, VC4_HD_M_SW_RST);
	udelay(1);
	HDMI_WRITE(HDMI_M_CTL, 0);

	HDMI_WRITE(HDMI_M_CTL, VC4_HD_M_ENABLE);

	HDMI_WRITE(HDMI_SW_RESET_CONTROL,
		   VC4_HDMI_SW_RESET_HDMI |
		   VC4_HDMI_SW_RESET_FORMAT_DETECT);

	HDMI_WRITE(HDMI_SW_RESET_CONTROL, 0);
}

static void vc5_hdmi_reset(struct vc4_hdmi *vc4_hdmi)
{
	reset_control_reset(vc4_hdmi->reset);

	HDMI_WRITE(HDMI_DVP_CTL, 0);

	HDMI_WRITE(HDMI_CLOCK_STOP,
		   HDMI_READ(HDMI_CLOCK_STOP) | VC4_DVP_HT_CLOCK_STOP_PIXEL);
}

#ifdef CONFIG_DRM_VC4_HDMI_CEC
static void vc4_hdmi_cec_update_clk_div(struct vc4_hdmi *vc4_hdmi)
{
	u16 clk_cnt;
	u32 value;

	value = HDMI_READ(HDMI_CEC_CNTRL_1);
	value &= ~VC4_HDMI_CEC_DIV_CLK_CNT_MASK;

	/*
	 * Set the clock divider: the hsm_clock rate and this divider
	 * setting will give a 40 kHz CEC clock.
	 */
	clk_cnt = clk_get_rate(vc4_hdmi->cec_clock) / CEC_CLOCK_FREQ;
	value |= clk_cnt << VC4_HDMI_CEC_DIV_CLK_CNT_SHIFT;
	HDMI_WRITE(HDMI_CEC_CNTRL_1, value);
}
#else
static void vc4_hdmi_cec_update_clk_div(struct vc4_hdmi *vc4_hdmi) {}
#endif

static enum drm_connector_status
vc4_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct vc4_hdmi *vc4_hdmi = connector_to_vc4_hdmi(connector);
	bool connected = false;

	if (vc4_hdmi->hpd_gpio &&
	    gpiod_get_value_cansleep(vc4_hdmi->hpd_gpio)) {
		connected = true;
	} else if (drm_probe_ddc(vc4_hdmi->ddc)) {
		connected = true;
	} else if (HDMI_READ(HDMI_HOTPLUG) & VC4_HDMI_HOTPLUG_CONNECTED) {
		connected = true;
	}

	if (connected) {
		if (connector->status != connector_status_connected) {
			struct edid *edid = drm_get_edid(connector, vc4_hdmi->ddc);

			if (edid) {
				cec_s_phys_addr_from_edid(vc4_hdmi->cec_adap, edid);
				vc4_hdmi->encoder.hdmi_monitor = drm_detect_hdmi_monitor(edid);
				kfree(edid);
			}
		}

		return connector_status_connected;
	}

	cec_phys_addr_invalidate(vc4_hdmi->cec_adap);
	return connector_status_disconnected;
}

static void vc4_hdmi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int vc4_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct vc4_hdmi *vc4_hdmi = connector_to_vc4_hdmi(connector);
	struct vc4_hdmi_encoder *vc4_encoder = &vc4_hdmi->encoder;
	int ret = 0;
	struct edid *edid;

	edid = drm_get_edid(connector, vc4_hdmi->ddc);
	cec_s_phys_addr_from_edid(vc4_hdmi->cec_adap, edid);
	if (!edid)
		return -ENODEV;

	vc4_encoder->hdmi_monitor = drm_detect_hdmi_monitor(edid);

	drm_connector_update_edid_property(connector, edid);
	ret = drm_add_edid_modes(connector, edid);
	kfree(edid);

	if (vc4_hdmi->disable_4kp60) {
		struct drm_device *drm = connector->dev;
		struct drm_display_mode *mode;

		list_for_each_entry(mode, &connector->probed_modes, head) {
			if (vc4_hdmi_mode_needs_scrambling(mode)) {
				drm_warn_once(drm, "The core clock cannot reach frequencies high enough to support 4k @ 60Hz.");
				drm_warn_once(drm, "Please change your config.txt file to add hdmi_enable_4kp60.");
			}
		}
	}

	return ret;
}

static int vc4_hdmi_connector_atomic_check(struct drm_connector *connector,
					   struct drm_atomic_state *state)
{
	struct drm_connector_state *old_state =
		drm_atomic_get_old_connector_state(state, connector);
	struct drm_connector_state *new_state =
		drm_atomic_get_new_connector_state(state, connector);
	struct drm_crtc *crtc = new_state->crtc;

	if (!crtc)
		return 0;

	if (old_state->colorspace != new_state->colorspace ||
	    !drm_connector_atomic_hdr_metadata_equal(old_state, new_state)) {
		struct drm_crtc_state *crtc_state;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);

		crtc_state->mode_changed = true;
	}

	return 0;
}

static void vc4_hdmi_connector_reset(struct drm_connector *connector)
{
	struct vc4_hdmi_connector_state *old_state =
		conn_state_to_vc4_hdmi_conn_state(connector->state);
	struct vc4_hdmi_connector_state *new_state =
		kzalloc(sizeof(*new_state), GFP_KERNEL);

	if (connector->state)
		__drm_atomic_helper_connector_destroy_state(connector->state);

	kfree(old_state);
	__drm_atomic_helper_connector_reset(connector, &new_state->base);

	if (!new_state)
		return;

	new_state->base.max_bpc = 8;
	new_state->base.max_requested_bpc = 8;
	drm_atomic_helper_connector_tv_reset(connector);
}

static struct drm_connector_state *
vc4_hdmi_connector_duplicate_state(struct drm_connector *connector)
{
	struct drm_connector_state *conn_state = connector->state;
	struct vc4_hdmi_connector_state *vc4_state = conn_state_to_vc4_hdmi_conn_state(conn_state);
	struct vc4_hdmi_connector_state *new_state;

	new_state = kzalloc(sizeof(*new_state), GFP_KERNEL);
	if (!new_state)
		return NULL;

	new_state->pixel_rate = vc4_state->pixel_rate;
	__drm_atomic_helper_connector_duplicate_state(connector, &new_state->base);

	return &new_state->base;
}

static const struct drm_connector_funcs vc4_hdmi_connector_funcs = {
	.detect = vc4_hdmi_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = vc4_hdmi_connector_destroy,
	.reset = vc4_hdmi_connector_reset,
	.atomic_duplicate_state = vc4_hdmi_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static const struct drm_connector_helper_funcs vc4_hdmi_connector_helper_funcs = {
	.get_modes = vc4_hdmi_connector_get_modes,
	.atomic_check = vc4_hdmi_connector_atomic_check,
};

static int vc4_hdmi_connector_init(struct drm_device *dev,
				   struct vc4_hdmi *vc4_hdmi)
{
	struct drm_connector *connector = &vc4_hdmi->connector;
	struct drm_encoder *encoder = &vc4_hdmi->encoder.base.base;
	int ret;

	drm_connector_init_with_ddc(dev, connector,
				    &vc4_hdmi_connector_funcs,
				    DRM_MODE_CONNECTOR_HDMIA,
				    vc4_hdmi->ddc);
	drm_connector_helper_add(connector, &vc4_hdmi_connector_helper_funcs);

	/*
	 * Some of the properties below require access to state, like bpc.
	 * Allocate some default initial connector state with our reset helper.
	 */
	if (connector->funcs->reset)
		connector->funcs->reset(connector);

	/* Create and attach TV margin props to this connector. */
	ret = drm_mode_create_tv_margin_properties(dev);
	if (ret)
		return ret;

	ret = drm_mode_create_hdmi_colorspace_property(connector);
	if (ret)
		return ret;

	drm_connector_attach_colorspace_property(connector);
	drm_connector_attach_tv_margin_properties(connector);
	drm_connector_attach_max_bpc_property(connector, 8, 12);

	connector->polled = (DRM_CONNECTOR_POLL_CONNECT |
			     DRM_CONNECTOR_POLL_DISCONNECT);

	connector->interlace_allowed = 1;
	connector->doublescan_allowed = 0;

	if (vc4_hdmi->variant->supports_hdr)
		drm_connector_attach_hdr_output_metadata_property(connector);

	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

static int vc4_hdmi_stop_packet(struct drm_encoder *encoder,
				enum hdmi_infoframe_type type,
				bool poll)
{
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	u32 packet_id = type - 0x80;

	HDMI_WRITE(HDMI_RAM_PACKET_CONFIG,
		   HDMI_READ(HDMI_RAM_PACKET_CONFIG) & ~BIT(packet_id));

	if (!poll)
		return 0;

	return wait_for(!(HDMI_READ(HDMI_RAM_PACKET_STATUS) &
			  BIT(packet_id)), 100);
}

static void vc4_hdmi_write_infoframe(struct drm_encoder *encoder,
				     union hdmi_infoframe *frame)
{
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	u32 packet_id = frame->any.type - 0x80;
	const struct vc4_hdmi_register *ram_packet_start =
		&vc4_hdmi->variant->registers[HDMI_RAM_PACKET_START];
	u32 packet_reg = ram_packet_start->offset + VC4_HDMI_PACKET_STRIDE * packet_id;
	void __iomem *base = __vc4_hdmi_get_field_base(vc4_hdmi,
						       ram_packet_start->reg);
	uint8_t buffer[VC4_HDMI_PACKET_STRIDE];
	ssize_t len, i;
	int ret;

	WARN_ONCE(!(HDMI_READ(HDMI_RAM_PACKET_CONFIG) &
		    VC4_HDMI_RAM_PACKET_ENABLE),
		  "Packet RAM has to be on to store the packet.");

	len = hdmi_infoframe_pack(frame, buffer, sizeof(buffer));
	if (len < 0)
		return;

	ret = vc4_hdmi_stop_packet(encoder, frame->any.type, true);
	if (ret) {
		DRM_ERROR("Failed to wait for infoframe to go idle: %d\n", ret);
		return;
	}

	for (i = 0; i < len; i += 7) {
		writel(buffer[i + 0] << 0 |
		       buffer[i + 1] << 8 |
		       buffer[i + 2] << 16,
		       base + packet_reg);
		packet_reg += 4;

		writel(buffer[i + 3] << 0 |
		       buffer[i + 4] << 8 |
		       buffer[i + 5] << 16 |
		       buffer[i + 6] << 24,
		       base + packet_reg);
		packet_reg += 4;
	}

	HDMI_WRITE(HDMI_RAM_PACKET_CONFIG,
		   HDMI_READ(HDMI_RAM_PACKET_CONFIG) | BIT(packet_id));
	ret = wait_for((HDMI_READ(HDMI_RAM_PACKET_STATUS) &
			BIT(packet_id)), 100);
	if (ret)
		DRM_ERROR("Failed to wait for infoframe to start: %d\n", ret);
}

static void vc4_hdmi_set_avi_infoframe(struct drm_encoder *encoder)
{
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	struct vc4_hdmi_encoder *vc4_encoder = to_vc4_hdmi_encoder(encoder);
	struct drm_connector *connector = &vc4_hdmi->connector;
	struct drm_connector_state *cstate = connector->state;
	struct drm_crtc *crtc = encoder->crtc;
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	union hdmi_infoframe frame;
	int ret;

	ret = drm_hdmi_avi_infoframe_from_display_mode(&frame.avi,
						       connector, mode);
	if (ret < 0) {
		DRM_ERROR("couldn't fill AVI infoframe\n");
		return;
	}

	drm_hdmi_avi_infoframe_quant_range(&frame.avi,
					   connector, mode,
					   vc4_encoder->limited_rgb_range ?
					   HDMI_QUANTIZATION_RANGE_LIMITED :
					   HDMI_QUANTIZATION_RANGE_FULL);
	drm_hdmi_avi_infoframe_colorspace(&frame.avi, cstate);
	drm_hdmi_avi_infoframe_bars(&frame.avi, cstate);

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
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	struct hdmi_audio_infoframe *audio = &vc4_hdmi->audio.infoframe;
	union hdmi_infoframe frame;

	memcpy(&frame.audio, audio, sizeof(*audio));
	vc4_hdmi_write_infoframe(encoder, &frame);
}

static void vc4_hdmi_set_hdr_infoframe(struct drm_encoder *encoder)
{
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	struct drm_connector *connector = &vc4_hdmi->connector;
	struct drm_connector_state *conn_state = connector->state;
	union hdmi_infoframe frame;

	if (!vc4_hdmi->variant->supports_hdr)
		return;

	if (!conn_state->hdr_output_metadata)
		return;

	if (drm_hdmi_infoframe_set_hdr_metadata(&frame.drm, conn_state))
		return;

	vc4_hdmi_write_infoframe(encoder, &frame);
}

static void vc4_hdmi_set_infoframes(struct drm_encoder *encoder)
{
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);

	vc4_hdmi_set_avi_infoframe(encoder);
	vc4_hdmi_set_spd_infoframe(encoder);
	/*
	 * If audio was streaming, then we need to reenabled the audio
	 * infoframe here during encoder_enable.
	 */
	if (vc4_hdmi->audio.streaming)
		vc4_hdmi_set_audio_infoframe(encoder);

	vc4_hdmi_set_hdr_infoframe(encoder);
}

static bool vc4_hdmi_supports_scrambling(struct drm_encoder *encoder,
					 struct drm_display_mode *mode)
{
	struct vc4_hdmi_encoder *vc4_encoder = to_vc4_hdmi_encoder(encoder);
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	struct drm_display_info *display = &vc4_hdmi->connector.display_info;

	if (!vc4_encoder->hdmi_monitor)
		return false;

	if (!display->hdmi.scdc.supported ||
	    !display->hdmi.scdc.scrambling.supported)
		return false;

	return true;
}

#define SCRAMBLING_POLLING_DELAY_MS	1000

static void vc4_hdmi_enable_scrambling(struct drm_encoder *encoder)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);

	if (!vc4_hdmi_supports_scrambling(encoder, mode))
		return;

	if (!vc4_hdmi_mode_needs_scrambling(mode))
		return;

	drm_scdc_set_high_tmds_clock_ratio(vc4_hdmi->ddc, true);
	drm_scdc_set_scrambling(vc4_hdmi->ddc, true);

	HDMI_WRITE(HDMI_SCRAMBLER_CTL, HDMI_READ(HDMI_SCRAMBLER_CTL) |
		   VC5_HDMI_SCRAMBLER_CTL_ENABLE);

	queue_delayed_work(system_wq, &vc4_hdmi->scrambling_work,
			   msecs_to_jiffies(SCRAMBLING_POLLING_DELAY_MS));
}

static void vc4_hdmi_disable_scrambling(struct drm_encoder *encoder)
{
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	struct drm_crtc *crtc = encoder->crtc;

	/*
	 * At boot, encoder->crtc will be NULL. Since we don't know the
	 * state of the scrambler and in order to avoid any
	 * inconsistency, let's disable it all the time.
	 */
	if (crtc && !vc4_hdmi_supports_scrambling(encoder, &crtc->mode))
		return;

	if (crtc && !vc4_hdmi_mode_needs_scrambling(&crtc->mode))
		return;

	if (delayed_work_pending(&vc4_hdmi->scrambling_work))
		cancel_delayed_work_sync(&vc4_hdmi->scrambling_work);

	HDMI_WRITE(HDMI_SCRAMBLER_CTL, HDMI_READ(HDMI_SCRAMBLER_CTL) &
		   ~VC5_HDMI_SCRAMBLER_CTL_ENABLE);

	drm_scdc_set_scrambling(vc4_hdmi->ddc, false);
	drm_scdc_set_high_tmds_clock_ratio(vc4_hdmi->ddc, false);
}

static void vc4_hdmi_scrambling_wq(struct work_struct *work)
{
	struct vc4_hdmi *vc4_hdmi = container_of(to_delayed_work(work),
						 struct vc4_hdmi,
						 scrambling_work);

	if (drm_scdc_get_scrambling_status(vc4_hdmi->ddc))
		return;

	drm_scdc_set_high_tmds_clock_ratio(vc4_hdmi->ddc, true);
	drm_scdc_set_scrambling(vc4_hdmi->ddc, true);

	queue_delayed_work(system_wq, &vc4_hdmi->scrambling_work,
			   msecs_to_jiffies(SCRAMBLING_POLLING_DELAY_MS));
}

static void vc4_hdmi_encoder_post_crtc_disable(struct drm_encoder *encoder,
					       struct drm_atomic_state *state)
{
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);

	HDMI_WRITE(HDMI_RAM_PACKET_CONFIG, 0);

	HDMI_WRITE(HDMI_VID_CTL, HDMI_READ(HDMI_VID_CTL) | VC4_HD_VID_CTL_CLRRGB);

	mdelay(1);

	HDMI_WRITE(HDMI_VID_CTL,
		   HDMI_READ(HDMI_VID_CTL) & ~VC4_HD_VID_CTL_ENABLE);
	vc4_hdmi_disable_scrambling(encoder);
}

static void vc4_hdmi_encoder_post_crtc_powerdown(struct drm_encoder *encoder,
						 struct drm_atomic_state *state)
{
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	int ret;

	HDMI_WRITE(HDMI_VID_CTL,
		   HDMI_READ(HDMI_VID_CTL) | VC4_HD_VID_CTL_BLANKPIX);

	if (vc4_hdmi->variant->phy_disable)
		vc4_hdmi->variant->phy_disable(vc4_hdmi);

	clk_disable_unprepare(vc4_hdmi->pixel_bvb_clock);
	clk_disable_unprepare(vc4_hdmi->hsm_clock);
	clk_disable_unprepare(vc4_hdmi->pixel_clock);

	ret = pm_runtime_put(&vc4_hdmi->pdev->dev);
	if (ret < 0)
		DRM_ERROR("Failed to release power domain: %d\n", ret);
}

static void vc4_hdmi_encoder_disable(struct drm_encoder *encoder)
{
}

static void vc4_hdmi_csc_setup(struct vc4_hdmi *vc4_hdmi, bool enable)
{
	u32 csc_ctl;

	csc_ctl = VC4_SET_FIELD(VC4_HD_CSC_CTL_ORDER_BGR,
				VC4_HD_CSC_CTL_ORDER);

	if (enable) {
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

		HDMI_WRITE(HDMI_CSC_12_11, (0x000 << 16) | 0x000);
		HDMI_WRITE(HDMI_CSC_14_13, (0x100 << 16) | 0x6e0);
		HDMI_WRITE(HDMI_CSC_22_21, (0x6e0 << 16) | 0x000);
		HDMI_WRITE(HDMI_CSC_24_23, (0x100 << 16) | 0x000);
		HDMI_WRITE(HDMI_CSC_32_31, (0x000 << 16) | 0x6e0);
		HDMI_WRITE(HDMI_CSC_34_33, (0x100 << 16) | 0x000);
	}

	/* The RGB order applies even when CSC is disabled. */
	HDMI_WRITE(HDMI_CSC_CTL, csc_ctl);
}

static void vc5_hdmi_csc_setup(struct vc4_hdmi *vc4_hdmi, bool enable)
{
	u32 csc_ctl;

	csc_ctl = 0x07;	/* RGB_CONVERT_MODE = custom matrix, || USE_RGB_TO_YCBCR */

	if (enable) {
		/* CEA VICs other than #1 requre limited range RGB
		 * output unless overridden by an AVI infoframe.
		 * Apply a colorspace conversion to squash 0-255 down
		 * to 16-235.  The matrix here is:
		 *
		 * [ 0.8594 0      0      16]
		 * [ 0      0.8594 0      16]
		 * [ 0      0      0.8594 16]
		 * [ 0      0      0       1]
		 * Matrix is signed 2p13 fixed point, with signed 9p6 offsets
		 */
		HDMI_WRITE(HDMI_CSC_12_11, (0x0000 << 16) | 0x1b80);
		HDMI_WRITE(HDMI_CSC_14_13, (0x0400 << 16) | 0x0000);
		HDMI_WRITE(HDMI_CSC_22_21, (0x1b80 << 16) | 0x0000);
		HDMI_WRITE(HDMI_CSC_24_23, (0x0400 << 16) | 0x0000);
		HDMI_WRITE(HDMI_CSC_32_31, (0x0000 << 16) | 0x0000);
		HDMI_WRITE(HDMI_CSC_34_33, (0x0400 << 16) | 0x1b80);
	} else {
		/* Still use the matrix for full range, but make it unity.
		 * Matrix is signed 2p13 fixed point, with signed 9p6 offsets
		 */
		HDMI_WRITE(HDMI_CSC_12_11, (0x0000 << 16) | 0x2000);
		HDMI_WRITE(HDMI_CSC_14_13, (0x0000 << 16) | 0x0000);
		HDMI_WRITE(HDMI_CSC_22_21, (0x2000 << 16) | 0x0000);
		HDMI_WRITE(HDMI_CSC_24_23, (0x0000 << 16) | 0x0000);
		HDMI_WRITE(HDMI_CSC_32_31, (0x0000 << 16) | 0x0000);
		HDMI_WRITE(HDMI_CSC_34_33, (0x0000 << 16) | 0x2000);
	}

	HDMI_WRITE(HDMI_CSC_CTL, csc_ctl);
}

static void vc4_hdmi_set_timings(struct vc4_hdmi *vc4_hdmi,
				 struct drm_connector_state *state,
				 struct drm_display_mode *mode)
{
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

	HDMI_WRITE(HDMI_HORZA,
		   (vsync_pos ? VC4_HDMI_HORZA_VPOS : 0) |
		   (hsync_pos ? VC4_HDMI_HORZA_HPOS : 0) |
		   VC4_SET_FIELD(mode->hdisplay * pixel_rep,
				 VC4_HDMI_HORZA_HAP));

	HDMI_WRITE(HDMI_HORZB,
		   VC4_SET_FIELD((mode->htotal -
				  mode->hsync_end) * pixel_rep,
				 VC4_HDMI_HORZB_HBP) |
		   VC4_SET_FIELD((mode->hsync_end -
				  mode->hsync_start) * pixel_rep,
				 VC4_HDMI_HORZB_HSP) |
		   VC4_SET_FIELD((mode->hsync_start -
				  mode->hdisplay) * pixel_rep,
				 VC4_HDMI_HORZB_HFP));

	HDMI_WRITE(HDMI_VERTA0, verta);
	HDMI_WRITE(HDMI_VERTA1, verta);

	HDMI_WRITE(HDMI_VERTB0, vertb_even);
	HDMI_WRITE(HDMI_VERTB1, vertb);
}

static void vc5_hdmi_set_timings(struct vc4_hdmi *vc4_hdmi,
				 struct drm_connector_state *state,
				 struct drm_display_mode *mode)
{
	bool hsync_pos = mode->flags & DRM_MODE_FLAG_PHSYNC;
	bool vsync_pos = mode->flags & DRM_MODE_FLAG_PVSYNC;
	bool interlaced = mode->flags & DRM_MODE_FLAG_INTERLACE;
	u32 pixel_rep = (mode->flags & DRM_MODE_FLAG_DBLCLK) ? 2 : 1;
	u32 verta = (VC4_SET_FIELD(mode->crtc_vsync_end - mode->crtc_vsync_start,
				   VC5_HDMI_VERTA_VSP) |
		     VC4_SET_FIELD(mode->crtc_vsync_start - mode->crtc_vdisplay,
				   VC5_HDMI_VERTA_VFP) |
		     VC4_SET_FIELD(mode->crtc_vdisplay, VC5_HDMI_VERTA_VAL));
	u32 vertb = (VC4_SET_FIELD(0, VC5_HDMI_VERTB_VSPO) |
		     VC4_SET_FIELD(mode->crtc_vtotal - mode->crtc_vsync_end,
				   VC4_HDMI_VERTB_VBP));
	u32 vertb_even = (VC4_SET_FIELD(0, VC5_HDMI_VERTB_VSPO) |
			  VC4_SET_FIELD(mode->crtc_vtotal -
					mode->crtc_vsync_end -
					interlaced,
					VC4_HDMI_VERTB_VBP));
	unsigned char gcp;
	bool gcp_en;
	u32 reg;

	HDMI_WRITE(HDMI_VEC_INTERFACE_XBAR, 0x354021);
	HDMI_WRITE(HDMI_HORZA,
		   (vsync_pos ? VC5_HDMI_HORZA_VPOS : 0) |
		   (hsync_pos ? VC5_HDMI_HORZA_HPOS : 0) |
		   VC4_SET_FIELD(mode->hdisplay * pixel_rep,
				 VC5_HDMI_HORZA_HAP) |
		   VC4_SET_FIELD((mode->hsync_start -
				  mode->hdisplay) * pixel_rep,
				 VC5_HDMI_HORZA_HFP));

	HDMI_WRITE(HDMI_HORZB,
		   VC4_SET_FIELD((mode->htotal -
				  mode->hsync_end) * pixel_rep,
				 VC5_HDMI_HORZB_HBP) |
		   VC4_SET_FIELD((mode->hsync_end -
				  mode->hsync_start) * pixel_rep,
				 VC5_HDMI_HORZB_HSP));

	HDMI_WRITE(HDMI_VERTA0, verta);
	HDMI_WRITE(HDMI_VERTA1, verta);

	HDMI_WRITE(HDMI_VERTB0, vertb_even);
	HDMI_WRITE(HDMI_VERTB1, vertb);

	switch (state->max_bpc) {
	case 12:
		gcp = 6;
		gcp_en = true;
		break;
	case 10:
		gcp = 5;
		gcp_en = true;
		break;
	case 8:
	default:
		gcp = 4;
		gcp_en = false;
		break;
	}

	reg = HDMI_READ(HDMI_DEEP_COLOR_CONFIG_1);
	reg &= ~(VC5_HDMI_DEEP_COLOR_CONFIG_1_INIT_PACK_PHASE_MASK |
		 VC5_HDMI_DEEP_COLOR_CONFIG_1_COLOR_DEPTH_MASK);
	reg |= VC4_SET_FIELD(2, VC5_HDMI_DEEP_COLOR_CONFIG_1_INIT_PACK_PHASE) |
	       VC4_SET_FIELD(gcp, VC5_HDMI_DEEP_COLOR_CONFIG_1_COLOR_DEPTH);
	HDMI_WRITE(HDMI_DEEP_COLOR_CONFIG_1, reg);

	reg = HDMI_READ(HDMI_GCP_WORD_1);
	reg &= ~VC5_HDMI_GCP_WORD_1_GCP_SUBPACKET_BYTE_1_MASK;
	reg |= VC4_SET_FIELD(gcp, VC5_HDMI_GCP_WORD_1_GCP_SUBPACKET_BYTE_1);
	HDMI_WRITE(HDMI_GCP_WORD_1, reg);

	reg = HDMI_READ(HDMI_GCP_CONFIG);
	reg &= ~VC5_HDMI_GCP_CONFIG_GCP_ENABLE;
	reg |= gcp_en ? VC5_HDMI_GCP_CONFIG_GCP_ENABLE : 0;
	HDMI_WRITE(HDMI_GCP_CONFIG, reg);

	HDMI_WRITE(HDMI_CLOCK_STOP, 0);
}

static void vc4_hdmi_recenter_fifo(struct vc4_hdmi *vc4_hdmi)
{
	u32 drift;
	int ret;

	drift = HDMI_READ(HDMI_FIFO_CTL);
	drift &= VC4_HDMI_FIFO_VALID_WRITE_MASK;

	HDMI_WRITE(HDMI_FIFO_CTL,
		   drift & ~VC4_HDMI_FIFO_CTL_RECENTER);
	HDMI_WRITE(HDMI_FIFO_CTL,
		   drift | VC4_HDMI_FIFO_CTL_RECENTER);
	usleep_range(1000, 1100);
	HDMI_WRITE(HDMI_FIFO_CTL,
		   drift & ~VC4_HDMI_FIFO_CTL_RECENTER);
	HDMI_WRITE(HDMI_FIFO_CTL,
		   drift | VC4_HDMI_FIFO_CTL_RECENTER);

	ret = wait_for(HDMI_READ(HDMI_FIFO_CTL) &
		       VC4_HDMI_FIFO_CTL_RECENTER_DONE, 1);
	WARN_ONCE(ret, "Timeout waiting for "
		  "VC4_HDMI_FIFO_CTL_RECENTER_DONE");
}

static struct drm_connector_state *
vc4_hdmi_encoder_get_connector_state(struct drm_encoder *encoder,
				     struct drm_atomic_state *state)
{
	struct drm_connector_state *conn_state;
	struct drm_connector *connector;
	unsigned int i;

	for_each_new_connector_in_state(state, connector, conn_state, i) {
		if (conn_state->best_encoder == encoder)
			return conn_state;
	}

	return NULL;
}

static void vc4_hdmi_encoder_pre_crtc_configure(struct drm_encoder *encoder,
						struct drm_atomic_state *state)
{
	struct drm_connector_state *conn_state =
		vc4_hdmi_encoder_get_connector_state(encoder, state);
	struct vc4_hdmi_connector_state *vc4_conn_state =
		conn_state_to_vc4_hdmi_conn_state(conn_state);
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	unsigned long bvb_rate, pixel_rate, hsm_rate;
	int ret;

	ret = pm_runtime_resume_and_get(&vc4_hdmi->pdev->dev);
	if (ret < 0) {
		DRM_ERROR("Failed to retain power domain: %d\n", ret);
		return;
	}

	pixel_rate = vc4_conn_state->pixel_rate;
	ret = clk_set_rate(vc4_hdmi->pixel_clock, pixel_rate);
	if (ret) {
		DRM_ERROR("Failed to set pixel clock rate: %d\n", ret);
		return;
	}

	ret = clk_prepare_enable(vc4_hdmi->pixel_clock);
	if (ret) {
		DRM_ERROR("Failed to turn on pixel clock: %d\n", ret);
		return;
	}

	/*
	 * As stated in RPi's vc4 firmware "HDMI state machine (HSM) clock must
	 * be faster than pixel clock, infinitesimally faster, tested in
	 * simulation. Otherwise, exact value is unimportant for HDMI
	 * operation." This conflicts with bcm2835's vc4 documentation, which
	 * states HSM's clock has to be at least 108% of the pixel clock.
	 *
	 * Real life tests reveal that vc4's firmware statement holds up, and
	 * users are able to use pixel clocks closer to HSM's, namely for
	 * 1920x1200@60Hz. So it was decided to have leave a 1% margin between
	 * both clocks. Which, for RPi0-3 implies a maximum pixel clock of
	 * 162MHz.
	 *
	 * Additionally, the AXI clock needs to be at least 25% of
	 * pixel clock, but HSM ends up being the limiting factor.
	 */
	hsm_rate = max_t(unsigned long, 120000000, (pixel_rate / 100) * 101);
	ret = clk_set_min_rate(vc4_hdmi->hsm_clock, hsm_rate);
	if (ret) {
		DRM_ERROR("Failed to set HSM clock rate: %d\n", ret);
		return;
	}

	ret = clk_prepare_enable(vc4_hdmi->hsm_clock);
	if (ret) {
		DRM_ERROR("Failed to turn on HSM clock: %d\n", ret);
		clk_disable_unprepare(vc4_hdmi->pixel_clock);
		return;
	}

	vc4_hdmi_cec_update_clk_div(vc4_hdmi);

	if (pixel_rate > 297000000)
		bvb_rate = 300000000;
	else if (pixel_rate > 148500000)
		bvb_rate = 150000000;
	else
		bvb_rate = 75000000;

	ret = clk_set_min_rate(vc4_hdmi->pixel_bvb_clock, bvb_rate);
	if (ret) {
		DRM_ERROR("Failed to set pixel bvb clock rate: %d\n", ret);
		clk_disable_unprepare(vc4_hdmi->hsm_clock);
		clk_disable_unprepare(vc4_hdmi->pixel_clock);
		return;
	}

	ret = clk_prepare_enable(vc4_hdmi->pixel_bvb_clock);
	if (ret) {
		DRM_ERROR("Failed to turn on pixel bvb clock: %d\n", ret);
		clk_disable_unprepare(vc4_hdmi->hsm_clock);
		clk_disable_unprepare(vc4_hdmi->pixel_clock);
		return;
	}

	if (vc4_hdmi->variant->phy_init)
		vc4_hdmi->variant->phy_init(vc4_hdmi, vc4_conn_state);

	HDMI_WRITE(HDMI_SCHEDULER_CONTROL,
		   HDMI_READ(HDMI_SCHEDULER_CONTROL) |
		   VC4_HDMI_SCHEDULER_CONTROL_MANUAL_FORMAT |
		   VC4_HDMI_SCHEDULER_CONTROL_IGNORE_VSYNC_PREDICTS);

	if (vc4_hdmi->variant->set_timings)
		vc4_hdmi->variant->set_timings(vc4_hdmi, conn_state, mode);
}

static void vc4_hdmi_encoder_pre_crtc_enable(struct drm_encoder *encoder,
					     struct drm_atomic_state *state)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct vc4_hdmi_encoder *vc4_encoder = to_vc4_hdmi_encoder(encoder);
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);

	if (vc4_encoder->hdmi_monitor &&
	    drm_default_rgb_quant_range(mode) == HDMI_QUANTIZATION_RANGE_LIMITED) {
		if (vc4_hdmi->variant->csc_setup)
			vc4_hdmi->variant->csc_setup(vc4_hdmi, true);

		vc4_encoder->limited_rgb_range = true;
	} else {
		if (vc4_hdmi->variant->csc_setup)
			vc4_hdmi->variant->csc_setup(vc4_hdmi, false);

		vc4_encoder->limited_rgb_range = false;
	}

	HDMI_WRITE(HDMI_FIFO_CTL, VC4_HDMI_FIFO_CTL_MASTER_SLAVE_N);
}

static void vc4_hdmi_encoder_post_crtc_enable(struct drm_encoder *encoder,
					      struct drm_atomic_state *state)
{
	struct drm_display_mode *mode = &encoder->crtc->state->adjusted_mode;
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	struct vc4_hdmi_encoder *vc4_encoder = to_vc4_hdmi_encoder(encoder);
	bool hsync_pos = mode->flags & DRM_MODE_FLAG_PHSYNC;
	bool vsync_pos = mode->flags & DRM_MODE_FLAG_PVSYNC;
	int ret;

	HDMI_WRITE(HDMI_VID_CTL,
		   VC4_HD_VID_CTL_ENABLE |
		   VC4_HD_VID_CTL_CLRRGB |
		   VC4_HD_VID_CTL_UNDERFLOW_ENABLE |
		   VC4_HD_VID_CTL_FRAME_COUNTER_RESET |
		   (vsync_pos ? 0 : VC4_HD_VID_CTL_VSYNC_LOW) |
		   (hsync_pos ? 0 : VC4_HD_VID_CTL_HSYNC_LOW));

	HDMI_WRITE(HDMI_VID_CTL,
		   HDMI_READ(HDMI_VID_CTL) & ~VC4_HD_VID_CTL_BLANKPIX);

	if (vc4_encoder->hdmi_monitor) {
		HDMI_WRITE(HDMI_SCHEDULER_CONTROL,
			   HDMI_READ(HDMI_SCHEDULER_CONTROL) |
			   VC4_HDMI_SCHEDULER_CONTROL_MODE_HDMI);

		ret = wait_for(HDMI_READ(HDMI_SCHEDULER_CONTROL) &
			       VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE, 1000);
		WARN_ONCE(ret, "Timeout waiting for "
			  "VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE\n");
	} else {
		HDMI_WRITE(HDMI_RAM_PACKET_CONFIG,
			   HDMI_READ(HDMI_RAM_PACKET_CONFIG) &
			   ~(VC4_HDMI_RAM_PACKET_ENABLE));
		HDMI_WRITE(HDMI_SCHEDULER_CONTROL,
			   HDMI_READ(HDMI_SCHEDULER_CONTROL) &
			   ~VC4_HDMI_SCHEDULER_CONTROL_MODE_HDMI);

		ret = wait_for(!(HDMI_READ(HDMI_SCHEDULER_CONTROL) &
				 VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE), 1000);
		WARN_ONCE(ret, "Timeout waiting for "
			  "!VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE\n");
	}

	if (vc4_encoder->hdmi_monitor) {
		WARN_ON(!(HDMI_READ(HDMI_SCHEDULER_CONTROL) &
			  VC4_HDMI_SCHEDULER_CONTROL_HDMI_ACTIVE));
		HDMI_WRITE(HDMI_SCHEDULER_CONTROL,
			   HDMI_READ(HDMI_SCHEDULER_CONTROL) |
			   VC4_HDMI_SCHEDULER_CONTROL_VERT_ALWAYS_KEEPOUT);

		HDMI_WRITE(HDMI_RAM_PACKET_CONFIG,
			   VC4_HDMI_RAM_PACKET_ENABLE);

		vc4_hdmi_set_infoframes(encoder);
	}

	vc4_hdmi_recenter_fifo(vc4_hdmi);
	vc4_hdmi_enable_scrambling(encoder);
}

static void vc4_hdmi_encoder_enable(struct drm_encoder *encoder)
{
}

#define WIFI_2_4GHz_CH1_MIN_FREQ	2400000000ULL
#define WIFI_2_4GHz_CH1_MAX_FREQ	2422000000ULL

static int vc4_hdmi_encoder_atomic_check(struct drm_encoder *encoder,
					 struct drm_crtc_state *crtc_state,
					 struct drm_connector_state *conn_state)
{
	struct vc4_hdmi_connector_state *vc4_state = conn_state_to_vc4_hdmi_conn_state(conn_state);
	struct drm_display_mode *mode = &crtc_state->adjusted_mode;
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);
	unsigned long long pixel_rate = mode->clock * 1000;
	unsigned long long tmds_rate;

	if (vc4_hdmi->variant->unsupported_odd_h_timings &&
	    ((mode->hdisplay % 2) || (mode->hsync_start % 2) ||
	     (mode->hsync_end % 2) || (mode->htotal % 2)))
		return -EINVAL;

	/*
	 * The 1440p@60 pixel rate is in the same range than the first
	 * WiFi channel (between 2.4GHz and 2.422GHz with 22MHz
	 * bandwidth). Slightly lower the frequency to bring it out of
	 * the WiFi range.
	 */
	tmds_rate = pixel_rate * 10;
	if (vc4_hdmi->disable_wifi_frequencies &&
	    (tmds_rate >= WIFI_2_4GHz_CH1_MIN_FREQ &&
	     tmds_rate <= WIFI_2_4GHz_CH1_MAX_FREQ)) {
		mode->clock = 238560;
		pixel_rate = mode->clock * 1000;
	}

	if (conn_state->max_bpc == 12) {
		pixel_rate = pixel_rate * 150;
		do_div(pixel_rate, 100);
	} else if (conn_state->max_bpc == 10) {
		pixel_rate = pixel_rate * 125;
		do_div(pixel_rate, 100);
	}

	if (mode->flags & DRM_MODE_FLAG_DBLCLK)
		pixel_rate = pixel_rate * 2;

	if (pixel_rate > vc4_hdmi->variant->max_pixel_clock)
		return -EINVAL;

	if (vc4_hdmi->disable_4kp60 && (pixel_rate > HDMI_14_MAX_TMDS_CLK))
		return -EINVAL;

	vc4_state->pixel_rate = pixel_rate;

	return 0;
}

static enum drm_mode_status
vc4_hdmi_encoder_mode_valid(struct drm_encoder *encoder,
			    const struct drm_display_mode *mode)
{
	struct vc4_hdmi *vc4_hdmi = encoder_to_vc4_hdmi(encoder);

	if (vc4_hdmi->variant->unsupported_odd_h_timings &&
	    ((mode->hdisplay % 2) || (mode->hsync_start % 2) ||
	     (mode->hsync_end % 2) || (mode->htotal % 2)))
		return MODE_H_ILLEGAL;

	if ((mode->clock * 1000) > vc4_hdmi->variant->max_pixel_clock)
		return MODE_CLOCK_HIGH;

	if (vc4_hdmi->disable_4kp60 && vc4_hdmi_mode_needs_scrambling(mode))
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static const struct drm_encoder_helper_funcs vc4_hdmi_encoder_helper_funcs = {
	.atomic_check = vc4_hdmi_encoder_atomic_check,
	.mode_valid = vc4_hdmi_encoder_mode_valid,
	.disable = vc4_hdmi_encoder_disable,
	.enable = vc4_hdmi_encoder_enable,
};

static u32 vc4_hdmi_channel_map(struct vc4_hdmi *vc4_hdmi, u32 channel_mask)
{
	int i;
	u32 channel_map = 0;

	for (i = 0; i < 8; i++) {
		if (channel_mask & BIT(i))
			channel_map |= i << (3 * i);
	}
	return channel_map;
}

static u32 vc5_hdmi_channel_map(struct vc4_hdmi *vc4_hdmi, u32 channel_mask)
{
	int i;
	u32 channel_map = 0;

	for (i = 0; i < 8; i++) {
		if (channel_mask & BIT(i))
			channel_map |= i << (4 * i);
	}
	return channel_map;
}

/* HDMI audio codec callbacks */
static void vc4_hdmi_audio_set_mai_clock(struct vc4_hdmi *vc4_hdmi,
					 unsigned int samplerate)
{
	u32 hsm_clock = clk_get_rate(vc4_hdmi->audio_clock);
	unsigned long n, m;

	rational_best_approximation(hsm_clock, samplerate,
				    VC4_HD_MAI_SMP_N_MASK >>
				    VC4_HD_MAI_SMP_N_SHIFT,
				    (VC4_HD_MAI_SMP_M_MASK >>
				     VC4_HD_MAI_SMP_M_SHIFT) + 1,
				    &n, &m);

	HDMI_WRITE(HDMI_MAI_SMP,
		   VC4_SET_FIELD(n, VC4_HD_MAI_SMP_N) |
		   VC4_SET_FIELD(m - 1, VC4_HD_MAI_SMP_M));
}

static void vc4_hdmi_set_n_cts(struct vc4_hdmi *vc4_hdmi, unsigned int samplerate)
{
	struct drm_encoder *encoder = &vc4_hdmi->encoder.base.base;
	struct drm_crtc *crtc = encoder->crtc;
	const struct drm_display_mode *mode = &crtc->state->adjusted_mode;
	u32 n, cts;
	u64 tmp;

	n = 128 * samplerate / 1000;
	tmp = (u64)(mode->clock * 1000) * n;
	do_div(tmp, 128 * samplerate);
	cts = tmp;

	HDMI_WRITE(HDMI_CRP_CFG,
		   VC4_HDMI_CRP_CFG_EXTERNAL_CTS_EN |
		   VC4_SET_FIELD(n, VC4_HDMI_CRP_CFG_N));

	/*
	 * We could get slightly more accurate clocks in some cases by
	 * providing a CTS_1 value.  The two CTS values are alternated
	 * between based on the period fields
	 */
	HDMI_WRITE(HDMI_CTS_0, cts);
	HDMI_WRITE(HDMI_CTS_1, cts);
}

static inline struct vc4_hdmi *dai_to_hdmi(struct snd_soc_dai *dai)
{
	struct snd_soc_card *card = snd_soc_dai_get_drvdata(dai);

	return snd_soc_card_get_drvdata(card);
}

static int vc4_hdmi_audio_startup(struct device *dev, void *data)
{
	struct vc4_hdmi *vc4_hdmi = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &vc4_hdmi->encoder.base.base;

	/*
	 * If the HDMI encoder hasn't probed, or the encoder is
	 * currently in DVI mode, treat the codec dai as missing.
	 */
	if (!encoder->crtc || !(HDMI_READ(HDMI_RAM_PACKET_CONFIG) &
				VC4_HDMI_RAM_PACKET_ENABLE))
		return -ENODEV;

	vc4_hdmi->audio.streaming = true;

	HDMI_WRITE(HDMI_MAI_CTL,
		   VC4_HD_MAI_CTL_RESET |
		   VC4_HD_MAI_CTL_FLUSH |
		   VC4_HD_MAI_CTL_DLATE |
		   VC4_HD_MAI_CTL_ERRORE |
		   VC4_HD_MAI_CTL_ERRORF);

	if (vc4_hdmi->variant->phy_rng_enable)
		vc4_hdmi->variant->phy_rng_enable(vc4_hdmi);

	return 0;
}

static void vc4_hdmi_audio_reset(struct vc4_hdmi *vc4_hdmi)
{
	struct drm_encoder *encoder = &vc4_hdmi->encoder.base.base;
	struct device *dev = &vc4_hdmi->pdev->dev;
	int ret;

	vc4_hdmi->audio.streaming = false;
	ret = vc4_hdmi_stop_packet(encoder, HDMI_INFOFRAME_TYPE_AUDIO, false);
	if (ret)
		dev_err(dev, "Failed to stop audio infoframe: %d\n", ret);

	HDMI_WRITE(HDMI_MAI_CTL, VC4_HD_MAI_CTL_RESET);
	HDMI_WRITE(HDMI_MAI_CTL, VC4_HD_MAI_CTL_ERRORF);
	HDMI_WRITE(HDMI_MAI_CTL, VC4_HD_MAI_CTL_FLUSH);
}

static void vc4_hdmi_audio_shutdown(struct device *dev, void *data)
{
	struct vc4_hdmi *vc4_hdmi = dev_get_drvdata(dev);

	HDMI_WRITE(HDMI_MAI_CTL,
		   VC4_HD_MAI_CTL_DLATE |
		   VC4_HD_MAI_CTL_ERRORE |
		   VC4_HD_MAI_CTL_ERRORF);

	if (vc4_hdmi->variant->phy_rng_disable)
		vc4_hdmi->variant->phy_rng_disable(vc4_hdmi);

	vc4_hdmi->audio.streaming = false;
	vc4_hdmi_audio_reset(vc4_hdmi);
}

static int sample_rate_to_mai_fmt(int samplerate)
{
	switch (samplerate) {
	case 8000:
		return VC4_HDMI_MAI_SAMPLE_RATE_8000;
	case 11025:
		return VC4_HDMI_MAI_SAMPLE_RATE_11025;
	case 12000:
		return VC4_HDMI_MAI_SAMPLE_RATE_12000;
	case 16000:
		return VC4_HDMI_MAI_SAMPLE_RATE_16000;
	case 22050:
		return VC4_HDMI_MAI_SAMPLE_RATE_22050;
	case 24000:
		return VC4_HDMI_MAI_SAMPLE_RATE_24000;
	case 32000:
		return VC4_HDMI_MAI_SAMPLE_RATE_32000;
	case 44100:
		return VC4_HDMI_MAI_SAMPLE_RATE_44100;
	case 48000:
		return VC4_HDMI_MAI_SAMPLE_RATE_48000;
	case 64000:
		return VC4_HDMI_MAI_SAMPLE_RATE_64000;
	case 88200:
		return VC4_HDMI_MAI_SAMPLE_RATE_88200;
	case 96000:
		return VC4_HDMI_MAI_SAMPLE_RATE_96000;
	case 128000:
		return VC4_HDMI_MAI_SAMPLE_RATE_128000;
	case 176400:
		return VC4_HDMI_MAI_SAMPLE_RATE_176400;
	case 192000:
		return VC4_HDMI_MAI_SAMPLE_RATE_192000;
	default:
		return VC4_HDMI_MAI_SAMPLE_RATE_NOT_INDICATED;
	}
}

/* HDMI audio codec callbacks */
static int vc4_hdmi_audio_prepare(struct device *dev, void *data,
				  struct hdmi_codec_daifmt *daifmt,
				  struct hdmi_codec_params *params)
{
	struct vc4_hdmi *vc4_hdmi = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &vc4_hdmi->encoder.base.base;
	unsigned int sample_rate = params->sample_rate;
	unsigned int channels = params->channels;
	u32 audio_packet_config, channel_mask;
	u32 channel_map;
	u32 mai_audio_format;
	u32 mai_sample_rate;

	dev_dbg(dev, "%s: %u Hz, %d bit, %d channels\n", __func__,
		sample_rate, params->sample_width, channels);

	HDMI_WRITE(HDMI_MAI_CTL,
		   VC4_SET_FIELD(channels, VC4_HD_MAI_CTL_CHNUM) |
		   VC4_HD_MAI_CTL_WHOLSMP |
		   VC4_HD_MAI_CTL_CHALIGN |
		   VC4_HD_MAI_CTL_ENABLE);

	vc4_hdmi_audio_set_mai_clock(vc4_hdmi, sample_rate);

	mai_sample_rate = sample_rate_to_mai_fmt(sample_rate);
	if (params->iec.status[0] & IEC958_AES0_NONAUDIO &&
	    params->channels == 8)
		mai_audio_format = VC4_HDMI_MAI_FORMAT_HBR;
	else
		mai_audio_format = VC4_HDMI_MAI_FORMAT_PCM;
	HDMI_WRITE(HDMI_MAI_FMT,
		   VC4_SET_FIELD(mai_sample_rate,
				 VC4_HDMI_MAI_FORMAT_SAMPLE_RATE) |
		   VC4_SET_FIELD(mai_audio_format,
				 VC4_HDMI_MAI_FORMAT_AUDIO_FORMAT));

	/* The B frame identifier should match the value used by alsa-lib (8) */
	audio_packet_config =
		VC4_HDMI_AUDIO_PACKET_ZERO_DATA_ON_SAMPLE_FLAT |
		VC4_HDMI_AUDIO_PACKET_ZERO_DATA_ON_INACTIVE_CHANNELS |
		VC4_SET_FIELD(0x8, VC4_HDMI_AUDIO_PACKET_B_FRAME_IDENTIFIER);

	channel_mask = GENMASK(channels - 1, 0);
	audio_packet_config |= VC4_SET_FIELD(channel_mask,
					     VC4_HDMI_AUDIO_PACKET_CEA_MASK);

	/* Set the MAI threshold */
	HDMI_WRITE(HDMI_MAI_THR,
		   VC4_SET_FIELD(0x10, VC4_HD_MAI_THR_PANICHIGH) |
		   VC4_SET_FIELD(0x10, VC4_HD_MAI_THR_PANICLOW) |
		   VC4_SET_FIELD(0x10, VC4_HD_MAI_THR_DREQHIGH) |
		   VC4_SET_FIELD(0x10, VC4_HD_MAI_THR_DREQLOW));

	HDMI_WRITE(HDMI_MAI_CONFIG,
		   VC4_HDMI_MAI_CONFIG_BIT_REVERSE |
		   VC4_HDMI_MAI_CONFIG_FORMAT_REVERSE |
		   VC4_SET_FIELD(channel_mask, VC4_HDMI_MAI_CHANNEL_MASK));

	channel_map = vc4_hdmi->variant->channel_map(vc4_hdmi, channel_mask);
	HDMI_WRITE(HDMI_MAI_CHANNEL_MAP, channel_map);
	HDMI_WRITE(HDMI_AUDIO_PACKET_CONFIG, audio_packet_config);
	vc4_hdmi_set_n_cts(vc4_hdmi, sample_rate);

	memcpy(&vc4_hdmi->audio.infoframe, &params->cea, sizeof(params->cea));
	vc4_hdmi_set_audio_infoframe(encoder);

	return 0;
}

static const struct snd_soc_component_driver vc4_hdmi_audio_cpu_dai_comp = {
	.name = "vc4-hdmi-cpu-dai-component",
};

static int vc4_hdmi_audio_cpu_dai_probe(struct snd_soc_dai *dai)
{
	struct vc4_hdmi *vc4_hdmi = dai_to_hdmi(dai);

	snd_soc_dai_init_dma_data(dai, &vc4_hdmi->audio.dma_data, NULL);

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
};

static const struct snd_dmaengine_pcm_config pcm_conf = {
	.chan_names[SNDRV_PCM_STREAM_PLAYBACK] = "audio-rx",
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
};

static int vc4_hdmi_audio_get_eld(struct device *dev, void *data,
				  uint8_t *buf, size_t len)
{
	struct vc4_hdmi *vc4_hdmi = dev_get_drvdata(dev);
	struct drm_connector *connector = &vc4_hdmi->connector;

	memcpy(buf, connector->eld, min(sizeof(connector->eld), len));

	return 0;
}

static const struct hdmi_codec_ops vc4_hdmi_codec_ops = {
	.get_eld = vc4_hdmi_audio_get_eld,
	.prepare = vc4_hdmi_audio_prepare,
	.audio_shutdown = vc4_hdmi_audio_shutdown,
	.audio_startup = vc4_hdmi_audio_startup,
};

static struct hdmi_codec_pdata vc4_hdmi_codec_pdata = {
	.ops = &vc4_hdmi_codec_ops,
	.max_i2s_channels = 8,
	.i2s = 1,
};

static int vc4_hdmi_audio_init(struct vc4_hdmi *vc4_hdmi)
{
	const struct vc4_hdmi_register *mai_data =
		&vc4_hdmi->variant->registers[HDMI_MAI_DATA];
	struct snd_soc_dai_link *dai_link = &vc4_hdmi->audio.link;
	struct snd_soc_card *card = &vc4_hdmi->audio.card;
	struct device *dev = &vc4_hdmi->pdev->dev;
	struct platform_device *codec_pdev;
	const __be32 *addr;
	int index;
	int ret;

	if (!of_find_property(dev->of_node, "dmas", NULL)) {
		dev_warn(dev,
			 "'dmas' DT property is missing, no HDMI audio\n");
		return 0;
	}

	if (mai_data->reg != VC4_HD) {
		WARN_ONCE(true, "MAI isn't in the HD block\n");
		return -EINVAL;
	}

	/*
	 * Get the physical address of VC4_HD_MAI_DATA. We need to retrieve
	 * the bus address specified in the DT, because the physical address
	 * (the one returned by platform_get_resource()) is not appropriate
	 * for DMA transfers.
	 * This VC/MMU should probably be exposed to avoid this kind of hacks.
	 */
	index = of_property_match_string(dev->of_node, "reg-names", "hd");
	/* Before BCM2711, we don't have a named register range */
	if (index < 0)
		index = 1;

	addr = of_get_address(dev->of_node, index, NULL, NULL);

	vc4_hdmi->audio.dma_data.addr = be32_to_cpup(addr) + mai_data->offset;
	vc4_hdmi->audio.dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	vc4_hdmi->audio.dma_data.maxburst = 2;

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

	codec_pdev = platform_device_register_data(dev, HDMI_CODEC_DRV_NAME,
						   PLATFORM_DEVID_AUTO,
						   &vc4_hdmi_codec_pdata,
						   sizeof(vc4_hdmi_codec_pdata));
	if (IS_ERR(codec_pdev)) {
		dev_err(dev, "Couldn't register the HDMI codec: %ld\n", PTR_ERR(codec_pdev));
		return PTR_ERR(codec_pdev);
	}

	dai_link->cpus		= &vc4_hdmi->audio.cpu;
	dai_link->codecs	= &vc4_hdmi->audio.codec;
	dai_link->platforms	= &vc4_hdmi->audio.platform;

	dai_link->num_cpus	= 1;
	dai_link->num_codecs	= 1;
	dai_link->num_platforms	= 1;

	dai_link->name = "MAI";
	dai_link->stream_name = "MAI PCM";
	dai_link->codecs->dai_name = "i2s-hifi";
	dai_link->cpus->dai_name = dev_name(dev);
	dai_link->codecs->name = dev_name(&codec_pdev->dev);
	dai_link->platforms->name = dev_name(dev);

	card->dai_link = dai_link;
	card->num_links = 1;
	card->name = vc4_hdmi->variant->card_name;
	card->driver_name = "vc4-hdmi";
	card->dev = dev;
	card->owner = THIS_MODULE;

	/*
	 * Be careful, snd_soc_register_card() calls dev_set_drvdata() and
	 * stores a pointer to the snd card object in dev->driver_data. This
	 * means we cannot use it for something else. The hdmi back-pointer is
	 * now stored in card->drvdata and should be retrieved with
	 * snd_soc_card_get_drvdata() if needed.
	 */
	snd_soc_card_set_drvdata(card, vc4_hdmi);
	ret = devm_snd_soc_register_card(dev, card);
	if (ret)
		dev_err_probe(dev, ret, "Could not register sound card\n");

	return ret;

}

static irqreturn_t vc4_hdmi_hpd_irq_thread(int irq, void *priv)
{
	struct vc4_hdmi *vc4_hdmi = priv;
	struct drm_device *dev = vc4_hdmi->connector.dev;

	if (dev && dev->registered)
		drm_kms_helper_hotplug_event(dev);

	return IRQ_HANDLED;
}

static int vc4_hdmi_hotplug_init(struct vc4_hdmi *vc4_hdmi)
{
	struct drm_connector *connector = &vc4_hdmi->connector;
	struct platform_device *pdev = vc4_hdmi->pdev;
	int ret;

	if (vc4_hdmi->variant->external_irq_controller) {
		unsigned int hpd_con = platform_get_irq_byname(pdev, "hpd-connected");
		unsigned int hpd_rm = platform_get_irq_byname(pdev, "hpd-removed");

		ret = request_threaded_irq(hpd_con,
					   NULL,
					   vc4_hdmi_hpd_irq_thread, IRQF_ONESHOT,
					   "vc4 hdmi hpd connected", vc4_hdmi);
		if (ret)
			return ret;

		ret = request_threaded_irq(hpd_rm,
					   NULL,
					   vc4_hdmi_hpd_irq_thread, IRQF_ONESHOT,
					   "vc4 hdmi hpd disconnected", vc4_hdmi);
		if (ret) {
			free_irq(hpd_con, vc4_hdmi);
			return ret;
		}

		connector->polled = DRM_CONNECTOR_POLL_HPD;
	}

	return 0;
}

static void vc4_hdmi_hotplug_exit(struct vc4_hdmi *vc4_hdmi)
{
	struct platform_device *pdev = vc4_hdmi->pdev;

	if (vc4_hdmi->variant->external_irq_controller) {
		free_irq(platform_get_irq_byname(pdev, "hpd-connected"), vc4_hdmi);
		free_irq(platform_get_irq_byname(pdev, "hpd-removed"), vc4_hdmi);
	}
}

#ifdef CONFIG_DRM_VC4_HDMI_CEC
static irqreturn_t vc4_cec_irq_handler_rx_thread(int irq, void *priv)
{
	struct vc4_hdmi *vc4_hdmi = priv;

	if (vc4_hdmi->cec_rx_msg.len)
		cec_received_msg(vc4_hdmi->cec_adap,
				 &vc4_hdmi->cec_rx_msg);

	return IRQ_HANDLED;
}

static irqreturn_t vc4_cec_irq_handler_tx_thread(int irq, void *priv)
{
	struct vc4_hdmi *vc4_hdmi = priv;

	if (vc4_hdmi->cec_tx_ok) {
		cec_transmit_done(vc4_hdmi->cec_adap, CEC_TX_STATUS_OK,
				  0, 0, 0, 0);
	} else {
		/*
		 * This CEC implementation makes 1 retry, so if we
		 * get a NACK, then that means it made 2 attempts.
		 */
		cec_transmit_done(vc4_hdmi->cec_adap, CEC_TX_STATUS_NACK,
				  0, 2, 0, 0);
	}
	return IRQ_HANDLED;
}

static irqreturn_t vc4_cec_irq_handler_thread(int irq, void *priv)
{
	struct vc4_hdmi *vc4_hdmi = priv;
	irqreturn_t ret;

	if (vc4_hdmi->cec_irq_was_rx)
		ret = vc4_cec_irq_handler_rx_thread(irq, priv);
	else
		ret = vc4_cec_irq_handler_tx_thread(irq, priv);

	return ret;
}

static void vc4_cec_read_msg(struct vc4_hdmi *vc4_hdmi, u32 cntrl1)
{
	struct drm_device *dev = vc4_hdmi->connector.dev;
	struct cec_msg *msg = &vc4_hdmi->cec_rx_msg;
	unsigned int i;

	msg->len = 1 + ((cntrl1 & VC4_HDMI_CEC_REC_WRD_CNT_MASK) >>
					VC4_HDMI_CEC_REC_WRD_CNT_SHIFT);

	if (msg->len > 16) {
		drm_err(dev, "Attempting to read too much data (%d)\n", msg->len);
		return;
	}

	for (i = 0; i < msg->len; i += 4) {
		u32 val = HDMI_READ(HDMI_CEC_RX_DATA_1 + (i >> 2));

		msg->msg[i] = val & 0xff;
		msg->msg[i + 1] = (val >> 8) & 0xff;
		msg->msg[i + 2] = (val >> 16) & 0xff;
		msg->msg[i + 3] = (val >> 24) & 0xff;
	}
}

static irqreturn_t vc4_cec_irq_handler_tx_bare(int irq, void *priv)
{
	struct vc4_hdmi *vc4_hdmi = priv;
	u32 cntrl1;

	cntrl1 = HDMI_READ(HDMI_CEC_CNTRL_1);
	vc4_hdmi->cec_tx_ok = cntrl1 & VC4_HDMI_CEC_TX_STATUS_GOOD;
	cntrl1 &= ~VC4_HDMI_CEC_START_XMIT_BEGIN;
	HDMI_WRITE(HDMI_CEC_CNTRL_1, cntrl1);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t vc4_cec_irq_handler_rx_bare(int irq, void *priv)
{
	struct vc4_hdmi *vc4_hdmi = priv;
	u32 cntrl1;

	vc4_hdmi->cec_rx_msg.len = 0;
	cntrl1 = HDMI_READ(HDMI_CEC_CNTRL_1);
	vc4_cec_read_msg(vc4_hdmi, cntrl1);
	cntrl1 |= VC4_HDMI_CEC_CLEAR_RECEIVE_OFF;
	HDMI_WRITE(HDMI_CEC_CNTRL_1, cntrl1);
	cntrl1 &= ~VC4_HDMI_CEC_CLEAR_RECEIVE_OFF;

	HDMI_WRITE(HDMI_CEC_CNTRL_1, cntrl1);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t vc4_cec_irq_handler(int irq, void *priv)
{
	struct vc4_hdmi *vc4_hdmi = priv;
	u32 stat = HDMI_READ(HDMI_CEC_CPU_STATUS);
	irqreturn_t ret;
	u32 cntrl5;

	if (!(stat & VC4_HDMI_CPU_CEC))
		return IRQ_NONE;

	cntrl5 = HDMI_READ(HDMI_CEC_CNTRL_5);
	vc4_hdmi->cec_irq_was_rx = cntrl5 & VC4_HDMI_CEC_RX_CEC_INT;
	if (vc4_hdmi->cec_irq_was_rx)
		ret = vc4_cec_irq_handler_rx_bare(irq, priv);
	else
		ret = vc4_cec_irq_handler_tx_bare(irq, priv);

	HDMI_WRITE(HDMI_CEC_CPU_CLEAR, VC4_HDMI_CPU_CEC);
	return ret;
}

static int vc4_hdmi_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct vc4_hdmi *vc4_hdmi = cec_get_drvdata(adap);
	/* clock period in microseconds */
	const u32 usecs = 1000000 / CEC_CLOCK_FREQ;
	u32 val = HDMI_READ(HDMI_CEC_CNTRL_5);

	val &= ~(VC4_HDMI_CEC_TX_SW_RESET | VC4_HDMI_CEC_RX_SW_RESET |
		 VC4_HDMI_CEC_CNT_TO_4700_US_MASK |
		 VC4_HDMI_CEC_CNT_TO_4500_US_MASK);
	val |= ((4700 / usecs) << VC4_HDMI_CEC_CNT_TO_4700_US_SHIFT) |
	       ((4500 / usecs) << VC4_HDMI_CEC_CNT_TO_4500_US_SHIFT);

	if (enable) {
		HDMI_WRITE(HDMI_CEC_CNTRL_5, val |
			   VC4_HDMI_CEC_TX_SW_RESET | VC4_HDMI_CEC_RX_SW_RESET);
		HDMI_WRITE(HDMI_CEC_CNTRL_5, val);
		HDMI_WRITE(HDMI_CEC_CNTRL_2,
			   ((1500 / usecs) << VC4_HDMI_CEC_CNT_TO_1500_US_SHIFT) |
			   ((1300 / usecs) << VC4_HDMI_CEC_CNT_TO_1300_US_SHIFT) |
			   ((800 / usecs) << VC4_HDMI_CEC_CNT_TO_800_US_SHIFT) |
			   ((600 / usecs) << VC4_HDMI_CEC_CNT_TO_600_US_SHIFT) |
			   ((400 / usecs) << VC4_HDMI_CEC_CNT_TO_400_US_SHIFT));
		HDMI_WRITE(HDMI_CEC_CNTRL_3,
			   ((2750 / usecs) << VC4_HDMI_CEC_CNT_TO_2750_US_SHIFT) |
			   ((2400 / usecs) << VC4_HDMI_CEC_CNT_TO_2400_US_SHIFT) |
			   ((2050 / usecs) << VC4_HDMI_CEC_CNT_TO_2050_US_SHIFT) |
			   ((1700 / usecs) << VC4_HDMI_CEC_CNT_TO_1700_US_SHIFT));
		HDMI_WRITE(HDMI_CEC_CNTRL_4,
			   ((4300 / usecs) << VC4_HDMI_CEC_CNT_TO_4300_US_SHIFT) |
			   ((3900 / usecs) << VC4_HDMI_CEC_CNT_TO_3900_US_SHIFT) |
			   ((3600 / usecs) << VC4_HDMI_CEC_CNT_TO_3600_US_SHIFT) |
			   ((3500 / usecs) << VC4_HDMI_CEC_CNT_TO_3500_US_SHIFT));

		if (!vc4_hdmi->variant->external_irq_controller)
			HDMI_WRITE(HDMI_CEC_CPU_MASK_CLEAR, VC4_HDMI_CPU_CEC);
	} else {
		if (!vc4_hdmi->variant->external_irq_controller)
			HDMI_WRITE(HDMI_CEC_CPU_MASK_SET, VC4_HDMI_CPU_CEC);
		HDMI_WRITE(HDMI_CEC_CNTRL_5, val |
			   VC4_HDMI_CEC_TX_SW_RESET | VC4_HDMI_CEC_RX_SW_RESET);
	}
	return 0;
}

static int vc4_hdmi_cec_adap_log_addr(struct cec_adapter *adap, u8 log_addr)
{
	struct vc4_hdmi *vc4_hdmi = cec_get_drvdata(adap);

	HDMI_WRITE(HDMI_CEC_CNTRL_1,
		   (HDMI_READ(HDMI_CEC_CNTRL_1) & ~VC4_HDMI_CEC_ADDR_MASK) |
		   (log_addr & 0xf) << VC4_HDMI_CEC_ADDR_SHIFT);
	return 0;
}

static int vc4_hdmi_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				      u32 signal_free_time, struct cec_msg *msg)
{
	struct vc4_hdmi *vc4_hdmi = cec_get_drvdata(adap);
	struct drm_device *dev = vc4_hdmi->connector.dev;
	u32 val;
	unsigned int i;

	if (msg->len > 16) {
		drm_err(dev, "Attempting to transmit too much data (%d)\n", msg->len);
		return -ENOMEM;
	}

	for (i = 0; i < msg->len; i += 4)
		HDMI_WRITE(HDMI_CEC_TX_DATA_1 + (i >> 2),
			   (msg->msg[i]) |
			   (msg->msg[i + 1] << 8) |
			   (msg->msg[i + 2] << 16) |
			   (msg->msg[i + 3] << 24));

	val = HDMI_READ(HDMI_CEC_CNTRL_1);
	val &= ~VC4_HDMI_CEC_START_XMIT_BEGIN;
	HDMI_WRITE(HDMI_CEC_CNTRL_1, val);
	val &= ~VC4_HDMI_CEC_MESSAGE_LENGTH_MASK;
	val |= (msg->len - 1) << VC4_HDMI_CEC_MESSAGE_LENGTH_SHIFT;
	val |= VC4_HDMI_CEC_START_XMIT_BEGIN;

	HDMI_WRITE(HDMI_CEC_CNTRL_1, val);
	return 0;
}

static const struct cec_adap_ops vc4_hdmi_cec_adap_ops = {
	.adap_enable = vc4_hdmi_cec_adap_enable,
	.adap_log_addr = vc4_hdmi_cec_adap_log_addr,
	.adap_transmit = vc4_hdmi_cec_adap_transmit,
};

static int vc4_hdmi_cec_init(struct vc4_hdmi *vc4_hdmi)
{
	struct cec_connector_info conn_info;
	struct platform_device *pdev = vc4_hdmi->pdev;
	struct device *dev = &pdev->dev;
	u32 value;
	int ret;

	if (!of_find_property(dev->of_node, "interrupts", NULL)) {
		dev_warn(dev, "'interrupts' DT property is missing, no CEC\n");
		return 0;
	}

	vc4_hdmi->cec_adap = cec_allocate_adapter(&vc4_hdmi_cec_adap_ops,
						  vc4_hdmi, "vc4",
						  CEC_CAP_DEFAULTS |
						  CEC_CAP_CONNECTOR_INFO, 1);
	ret = PTR_ERR_OR_ZERO(vc4_hdmi->cec_adap);
	if (ret < 0)
		return ret;

	cec_fill_conn_info_from_drm(&conn_info, &vc4_hdmi->connector);
	cec_s_conn_info(vc4_hdmi->cec_adap, &conn_info);

	value = HDMI_READ(HDMI_CEC_CNTRL_1);
	/* Set the logical address to Unregistered */
	value |= VC4_HDMI_CEC_ADDR_MASK;
	HDMI_WRITE(HDMI_CEC_CNTRL_1, value);

	vc4_hdmi_cec_update_clk_div(vc4_hdmi);

	if (vc4_hdmi->variant->external_irq_controller) {
		ret = request_threaded_irq(platform_get_irq_byname(pdev, "cec-rx"),
					   vc4_cec_irq_handler_rx_bare,
					   vc4_cec_irq_handler_rx_thread, 0,
					   "vc4 hdmi cec rx", vc4_hdmi);
		if (ret)
			goto err_delete_cec_adap;

		ret = request_threaded_irq(platform_get_irq_byname(pdev, "cec-tx"),
					   vc4_cec_irq_handler_tx_bare,
					   vc4_cec_irq_handler_tx_thread, 0,
					   "vc4 hdmi cec tx", vc4_hdmi);
		if (ret)
			goto err_remove_cec_rx_handler;
	} else {
		HDMI_WRITE(HDMI_CEC_CPU_MASK_SET, 0xffffffff);

		ret = request_threaded_irq(platform_get_irq(pdev, 0),
					   vc4_cec_irq_handler,
					   vc4_cec_irq_handler_thread, 0,
					   "vc4 hdmi cec", vc4_hdmi);
		if (ret)
			goto err_delete_cec_adap;
	}

	ret = cec_register_adapter(vc4_hdmi->cec_adap, &pdev->dev);
	if (ret < 0)
		goto err_remove_handlers;

	return 0;

err_remove_handlers:
	if (vc4_hdmi->variant->external_irq_controller)
		free_irq(platform_get_irq_byname(pdev, "cec-tx"), vc4_hdmi);
	else
		free_irq(platform_get_irq(pdev, 0), vc4_hdmi);

err_remove_cec_rx_handler:
	if (vc4_hdmi->variant->external_irq_controller)
		free_irq(platform_get_irq_byname(pdev, "cec-rx"), vc4_hdmi);

err_delete_cec_adap:
	cec_delete_adapter(vc4_hdmi->cec_adap);

	return ret;
}

static void vc4_hdmi_cec_exit(struct vc4_hdmi *vc4_hdmi)
{
	struct platform_device *pdev = vc4_hdmi->pdev;

	if (vc4_hdmi->variant->external_irq_controller) {
		free_irq(platform_get_irq_byname(pdev, "cec-rx"), vc4_hdmi);
		free_irq(platform_get_irq_byname(pdev, "cec-tx"), vc4_hdmi);
	} else {
		free_irq(platform_get_irq(pdev, 0), vc4_hdmi);
	}

	cec_unregister_adapter(vc4_hdmi->cec_adap);
}
#else
static int vc4_hdmi_cec_init(struct vc4_hdmi *vc4_hdmi)
{
	return 0;
}

static void vc4_hdmi_cec_exit(struct vc4_hdmi *vc4_hdmi) {};

#endif

static int vc4_hdmi_build_regset(struct vc4_hdmi *vc4_hdmi,
				 struct debugfs_regset32 *regset,
				 enum vc4_hdmi_regs reg)
{
	const struct vc4_hdmi_variant *variant = vc4_hdmi->variant;
	struct debugfs_reg32 *regs, *new_regs;
	unsigned int count = 0;
	unsigned int i;

	regs = kcalloc(variant->num_registers, sizeof(*regs),
		       GFP_KERNEL);
	if (!regs)
		return -ENOMEM;

	for (i = 0; i < variant->num_registers; i++) {
		const struct vc4_hdmi_register *field =	&variant->registers[i];

		if (field->reg != reg)
			continue;

		regs[count].name = field->name;
		regs[count].offset = field->offset;
		count++;
	}

	new_regs = krealloc(regs, count * sizeof(*regs), GFP_KERNEL);
	if (!new_regs)
		return -ENOMEM;

	regset->base = __vc4_hdmi_get_field_base(vc4_hdmi, reg);
	regset->regs = new_regs;
	regset->nregs = count;

	return 0;
}

static int vc4_hdmi_init_resources(struct vc4_hdmi *vc4_hdmi)
{
	struct platform_device *pdev = vc4_hdmi->pdev;
	struct device *dev = &pdev->dev;
	int ret;

	vc4_hdmi->hdmicore_regs = vc4_ioremap_regs(pdev, 0);
	if (IS_ERR(vc4_hdmi->hdmicore_regs))
		return PTR_ERR(vc4_hdmi->hdmicore_regs);

	vc4_hdmi->hd_regs = vc4_ioremap_regs(pdev, 1);
	if (IS_ERR(vc4_hdmi->hd_regs))
		return PTR_ERR(vc4_hdmi->hd_regs);

	ret = vc4_hdmi_build_regset(vc4_hdmi, &vc4_hdmi->hd_regset, VC4_HD);
	if (ret)
		return ret;

	ret = vc4_hdmi_build_regset(vc4_hdmi, &vc4_hdmi->hdmi_regset, VC4_HDMI);
	if (ret)
		return ret;

	vc4_hdmi->pixel_clock = devm_clk_get(dev, "pixel");
	if (IS_ERR(vc4_hdmi->pixel_clock)) {
		ret = PTR_ERR(vc4_hdmi->pixel_clock);
		if (ret != -EPROBE_DEFER)
			DRM_ERROR("Failed to get pixel clock\n");
		return ret;
	}

	vc4_hdmi->hsm_clock = devm_clk_get(dev, "hdmi");
	if (IS_ERR(vc4_hdmi->hsm_clock)) {
		DRM_ERROR("Failed to get HDMI state machine clock\n");
		return PTR_ERR(vc4_hdmi->hsm_clock);
	}
	vc4_hdmi->audio_clock = vc4_hdmi->hsm_clock;
	vc4_hdmi->cec_clock = vc4_hdmi->hsm_clock;

	return 0;
}

static int vc5_hdmi_init_resources(struct vc4_hdmi *vc4_hdmi)
{
	struct platform_device *pdev = vc4_hdmi->pdev;
	struct device *dev = &pdev->dev;
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hdmi");
	if (!res)
		return -ENODEV;

	vc4_hdmi->hdmicore_regs = devm_ioremap(dev, res->start,
					       resource_size(res));
	if (!vc4_hdmi->hdmicore_regs)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hd");
	if (!res)
		return -ENODEV;

	vc4_hdmi->hd_regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!vc4_hdmi->hd_regs)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "cec");
	if (!res)
		return -ENODEV;

	vc4_hdmi->cec_regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!vc4_hdmi->cec_regs)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "csc");
	if (!res)
		return -ENODEV;

	vc4_hdmi->csc_regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!vc4_hdmi->csc_regs)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "dvp");
	if (!res)
		return -ENODEV;

	vc4_hdmi->dvp_regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!vc4_hdmi->dvp_regs)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "phy");
	if (!res)
		return -ENODEV;

	vc4_hdmi->phy_regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!vc4_hdmi->phy_regs)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "packet");
	if (!res)
		return -ENODEV;

	vc4_hdmi->ram_regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!vc4_hdmi->ram_regs)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "rm");
	if (!res)
		return -ENODEV;

	vc4_hdmi->rm_regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!vc4_hdmi->rm_regs)
		return -ENOMEM;

	vc4_hdmi->hsm_clock = devm_clk_get(dev, "hdmi");
	if (IS_ERR(vc4_hdmi->hsm_clock)) {
		DRM_ERROR("Failed to get HDMI state machine clock\n");
		return PTR_ERR(vc4_hdmi->hsm_clock);
	}

	vc4_hdmi->pixel_bvb_clock = devm_clk_get(dev, "bvb");
	if (IS_ERR(vc4_hdmi->pixel_bvb_clock)) {
		DRM_ERROR("Failed to get pixel bvb clock\n");
		return PTR_ERR(vc4_hdmi->pixel_bvb_clock);
	}

	vc4_hdmi->audio_clock = devm_clk_get(dev, "audio");
	if (IS_ERR(vc4_hdmi->audio_clock)) {
		DRM_ERROR("Failed to get audio clock\n");
		return PTR_ERR(vc4_hdmi->audio_clock);
	}

	vc4_hdmi->cec_clock = devm_clk_get(dev, "cec");
	if (IS_ERR(vc4_hdmi->cec_clock)) {
		DRM_ERROR("Failed to get CEC clock\n");
		return PTR_ERR(vc4_hdmi->cec_clock);
	}

	vc4_hdmi->reset = devm_reset_control_get(dev, NULL);
	if (IS_ERR(vc4_hdmi->reset)) {
		DRM_ERROR("Failed to get HDMI reset line\n");
		return PTR_ERR(vc4_hdmi->reset);
	}

	return 0;
}

static int vc4_hdmi_bind(struct device *dev, struct device *master, void *data)
{
	const struct vc4_hdmi_variant *variant = of_device_get_match_data(dev);
	struct platform_device *pdev = to_platform_device(dev);
	struct drm_device *drm = dev_get_drvdata(master);
	struct vc4_hdmi *vc4_hdmi;
	struct drm_encoder *encoder;
	struct device_node *ddc_node;
	int ret;

	vc4_hdmi = devm_kzalloc(dev, sizeof(*vc4_hdmi), GFP_KERNEL);
	if (!vc4_hdmi)
		return -ENOMEM;
	INIT_DELAYED_WORK(&vc4_hdmi->scrambling_work, vc4_hdmi_scrambling_wq);

	dev_set_drvdata(dev, vc4_hdmi);
	encoder = &vc4_hdmi->encoder.base.base;
	vc4_hdmi->encoder.base.type = variant->encoder_type;
	vc4_hdmi->encoder.base.pre_crtc_configure = vc4_hdmi_encoder_pre_crtc_configure;
	vc4_hdmi->encoder.base.pre_crtc_enable = vc4_hdmi_encoder_pre_crtc_enable;
	vc4_hdmi->encoder.base.post_crtc_enable = vc4_hdmi_encoder_post_crtc_enable;
	vc4_hdmi->encoder.base.post_crtc_disable = vc4_hdmi_encoder_post_crtc_disable;
	vc4_hdmi->encoder.base.post_crtc_powerdown = vc4_hdmi_encoder_post_crtc_powerdown;
	vc4_hdmi->pdev = pdev;
	vc4_hdmi->variant = variant;

	ret = variant->init_resources(vc4_hdmi);
	if (ret)
		return ret;

	ddc_node = of_parse_phandle(dev->of_node, "ddc", 0);
	if (!ddc_node) {
		DRM_ERROR("Failed to find ddc node in device tree\n");
		return -ENODEV;
	}

	vc4_hdmi->ddc = of_find_i2c_adapter_by_node(ddc_node);
	of_node_put(ddc_node);
	if (!vc4_hdmi->ddc) {
		DRM_DEBUG("Failed to get ddc i2c adapter by node\n");
		return -EPROBE_DEFER;
	}

	/* Only use the GPIO HPD pin if present in the DT, otherwise
	 * we'll use the HDMI core's register.
	 */
	vc4_hdmi->hpd_gpio = devm_gpiod_get_optional(dev, "hpd", GPIOD_IN);
	if (IS_ERR(vc4_hdmi->hpd_gpio)) {
		ret = PTR_ERR(vc4_hdmi->hpd_gpio);
		goto err_put_ddc;
	}

	vc4_hdmi->disable_wifi_frequencies =
		of_property_read_bool(dev->of_node, "wifi-2.4ghz-coexistence");

	if (variant->max_pixel_clock == 600000000) {
		struct vc4_dev *vc4 = to_vc4_dev(drm);
		long max_rate = clk_round_rate(vc4->hvs->core_clk, 550000000);

		if (max_rate < 550000000)
			vc4_hdmi->disable_4kp60 = true;
	}

	if (vc4_hdmi->variant->reset)
		vc4_hdmi->variant->reset(vc4_hdmi);

	if ((of_device_is_compatible(dev->of_node, "brcm,bcm2711-hdmi0") ||
	     of_device_is_compatible(dev->of_node, "brcm,bcm2711-hdmi1")) &&
	    HDMI_READ(HDMI_VID_CTL) & VC4_HD_VID_CTL_ENABLE) {
		clk_prepare_enable(vc4_hdmi->pixel_clock);
		clk_prepare_enable(vc4_hdmi->hsm_clock);
		clk_prepare_enable(vc4_hdmi->pixel_bvb_clock);
	}

	pm_runtime_enable(dev);

	drm_simple_encoder_init(drm, encoder, DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &vc4_hdmi_encoder_helper_funcs);

	ret = vc4_hdmi_connector_init(drm, vc4_hdmi);
	if (ret)
		goto err_destroy_encoder;

	ret = vc4_hdmi_hotplug_init(vc4_hdmi);
	if (ret)
		goto err_destroy_conn;

	ret = vc4_hdmi_cec_init(vc4_hdmi);
	if (ret)
		goto err_free_hotplug;

	ret = vc4_hdmi_audio_init(vc4_hdmi);
	if (ret)
		goto err_free_cec;

	vc4_debugfs_add_file(drm, variant->debugfs_name,
			     vc4_hdmi_debugfs_regs,
			     vc4_hdmi);

	return 0;

err_free_cec:
	vc4_hdmi_cec_exit(vc4_hdmi);
err_free_hotplug:
	vc4_hdmi_hotplug_exit(vc4_hdmi);
err_destroy_conn:
	vc4_hdmi_connector_destroy(&vc4_hdmi->connector);
err_destroy_encoder:
	drm_encoder_cleanup(encoder);
	pm_runtime_disable(dev);
err_put_ddc:
	put_device(&vc4_hdmi->ddc->dev);

	return ret;
}

static void vc4_hdmi_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct vc4_hdmi *vc4_hdmi;

	/*
	 * ASoC makes it a bit hard to retrieve a pointer to the
	 * vc4_hdmi structure. Registering the card will overwrite our
	 * device drvdata with a pointer to the snd_soc_card structure,
	 * which can then be used to retrieve whatever drvdata we want
	 * to associate.
	 *
	 * However, that doesn't fly in the case where we wouldn't
	 * register an ASoC card (because of an old DT that is missing
	 * the dmas properties for example), then the card isn't
	 * registered and the device drvdata wouldn't be set.
	 *
	 * We can deal with both cases by making sure a snd_soc_card
	 * pointer and a vc4_hdmi structure are pointing to the same
	 * memory address, so we can treat them indistinctly without any
	 * issue.
	 */
	BUILD_BUG_ON(offsetof(struct vc4_hdmi_audio, card) != 0);
	BUILD_BUG_ON(offsetof(struct vc4_hdmi, audio) != 0);
	vc4_hdmi = dev_get_drvdata(dev);

	kfree(vc4_hdmi->hdmi_regset.regs);
	kfree(vc4_hdmi->hd_regset.regs);

	vc4_hdmi_cec_exit(vc4_hdmi);
	vc4_hdmi_hotplug_exit(vc4_hdmi);
	vc4_hdmi_connector_destroy(&vc4_hdmi->connector);
	drm_encoder_cleanup(&vc4_hdmi->encoder.base.base);

	pm_runtime_disable(dev);

	put_device(&vc4_hdmi->ddc->dev);
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

static const struct vc4_hdmi_variant bcm2835_variant = {
	.encoder_type		= VC4_ENCODER_TYPE_HDMI0,
	.debugfs_name		= "hdmi_regs",
	.card_name		= "vc4-hdmi",
	.max_pixel_clock	= 162000000,
	.registers		= vc4_hdmi_fields,
	.num_registers		= ARRAY_SIZE(vc4_hdmi_fields),

	.init_resources		= vc4_hdmi_init_resources,
	.csc_setup		= vc4_hdmi_csc_setup,
	.reset			= vc4_hdmi_reset,
	.set_timings		= vc4_hdmi_set_timings,
	.phy_init		= vc4_hdmi_phy_init,
	.phy_disable		= vc4_hdmi_phy_disable,
	.phy_rng_enable		= vc4_hdmi_phy_rng_enable,
	.phy_rng_disable	= vc4_hdmi_phy_rng_disable,
	.channel_map		= vc4_hdmi_channel_map,
	.supports_hdr		= false,
};

static const struct vc4_hdmi_variant bcm2711_hdmi0_variant = {
	.encoder_type		= VC4_ENCODER_TYPE_HDMI0,
	.debugfs_name		= "hdmi0_regs",
	.card_name		= "vc4-hdmi-0",
	.max_pixel_clock	= HDMI_14_MAX_TMDS_CLK,
	.registers		= vc5_hdmi_hdmi0_fields,
	.num_registers		= ARRAY_SIZE(vc5_hdmi_hdmi0_fields),
	.phy_lane_mapping	= {
		PHY_LANE_0,
		PHY_LANE_1,
		PHY_LANE_2,
		PHY_LANE_CK,
	},
	.unsupported_odd_h_timings	= true,
	.external_irq_controller	= true,

	.init_resources		= vc5_hdmi_init_resources,
	.csc_setup		= vc5_hdmi_csc_setup,
	.reset			= vc5_hdmi_reset,
	.set_timings		= vc5_hdmi_set_timings,
	.phy_init		= vc5_hdmi_phy_init,
	.phy_disable		= vc5_hdmi_phy_disable,
	.phy_rng_enable		= vc5_hdmi_phy_rng_enable,
	.phy_rng_disable	= vc5_hdmi_phy_rng_disable,
	.channel_map		= vc5_hdmi_channel_map,
	.supports_hdr		= true,
};

static const struct vc4_hdmi_variant bcm2711_hdmi1_variant = {
	.encoder_type		= VC4_ENCODER_TYPE_HDMI1,
	.debugfs_name		= "hdmi1_regs",
	.card_name		= "vc4-hdmi-1",
	.max_pixel_clock	= HDMI_14_MAX_TMDS_CLK,
	.registers		= vc5_hdmi_hdmi1_fields,
	.num_registers		= ARRAY_SIZE(vc5_hdmi_hdmi1_fields),
	.phy_lane_mapping	= {
		PHY_LANE_1,
		PHY_LANE_0,
		PHY_LANE_CK,
		PHY_LANE_2,
	},
	.unsupported_odd_h_timings	= true,
	.external_irq_controller	= true,

	.init_resources		= vc5_hdmi_init_resources,
	.csc_setup		= vc5_hdmi_csc_setup,
	.reset			= vc5_hdmi_reset,
	.set_timings		= vc5_hdmi_set_timings,
	.phy_init		= vc5_hdmi_phy_init,
	.phy_disable		= vc5_hdmi_phy_disable,
	.phy_rng_enable		= vc5_hdmi_phy_rng_enable,
	.phy_rng_disable	= vc5_hdmi_phy_rng_disable,
	.channel_map		= vc5_hdmi_channel_map,
	.supports_hdr		= true,
};

static const struct of_device_id vc4_hdmi_dt_match[] = {
	{ .compatible = "brcm,bcm2835-hdmi", .data = &bcm2835_variant },
	{ .compatible = "brcm,bcm2711-hdmi0", .data = &bcm2711_hdmi0_variant },
	{ .compatible = "brcm,bcm2711-hdmi1", .data = &bcm2711_hdmi1_variant },
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
