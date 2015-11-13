/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Vincent Abriou <vincent.abriou@st.com> for STMicroelectronics.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/hdmi.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>

#include "sti_hdmi.h"
#include "sti_hdmi_tx3g4c28phy.h"
#include "sti_hdmi_tx3g0c55phy.h"
#include "sti_vtg.h"

#define HDMI_CFG                        0x0000
#define HDMI_INT_EN                     0x0004
#define HDMI_INT_STA                    0x0008
#define HDMI_INT_CLR                    0x000C
#define HDMI_STA                        0x0010
#define HDMI_ACTIVE_VID_XMIN            0x0100
#define HDMI_ACTIVE_VID_XMAX            0x0104
#define HDMI_ACTIVE_VID_YMIN            0x0108
#define HDMI_ACTIVE_VID_YMAX            0x010C
#define HDMI_DFLT_CHL0_DAT              0x0110
#define HDMI_DFLT_CHL1_DAT              0x0114
#define HDMI_DFLT_CHL2_DAT              0x0118
#define HDMI_SW_DI_1_HEAD_WORD          0x0210
#define HDMI_SW_DI_1_PKT_WORD0          0x0214
#define HDMI_SW_DI_1_PKT_WORD1          0x0218
#define HDMI_SW_DI_1_PKT_WORD2          0x021C
#define HDMI_SW_DI_1_PKT_WORD3          0x0220
#define HDMI_SW_DI_1_PKT_WORD4          0x0224
#define HDMI_SW_DI_1_PKT_WORD5          0x0228
#define HDMI_SW_DI_1_PKT_WORD6          0x022C
#define HDMI_SW_DI_CFG                  0x0230
#define HDMI_SW_DI_2_HEAD_WORD          0x0600
#define HDMI_SW_DI_2_PKT_WORD0          0x0604
#define HDMI_SW_DI_2_PKT_WORD1          0x0608
#define HDMI_SW_DI_2_PKT_WORD2          0x060C
#define HDMI_SW_DI_2_PKT_WORD3          0x0610
#define HDMI_SW_DI_2_PKT_WORD4          0x0614
#define HDMI_SW_DI_2_PKT_WORD5          0x0618
#define HDMI_SW_DI_2_PKT_WORD6          0x061C

#define HDMI_IFRAME_SLOT_AVI            1
#define HDMI_IFRAME_SLOT_AUDIO          2

#define  XCAT(prefix, x, suffix)        prefix ## x ## suffix
#define  HDMI_SW_DI_N_HEAD_WORD(x)      XCAT(HDMI_SW_DI_, x, _HEAD_WORD)
#define  HDMI_SW_DI_N_PKT_WORD0(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD0)
#define  HDMI_SW_DI_N_PKT_WORD1(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD1)
#define  HDMI_SW_DI_N_PKT_WORD2(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD2)
#define  HDMI_SW_DI_N_PKT_WORD3(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD3)
#define  HDMI_SW_DI_N_PKT_WORD4(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD4)
#define  HDMI_SW_DI_N_PKT_WORD5(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD5)
#define  HDMI_SW_DI_N_PKT_WORD6(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD6)

#define HDMI_IFRAME_DISABLED            0x0
#define HDMI_IFRAME_SINGLE_SHOT         0x1
#define HDMI_IFRAME_FIELD               0x2
#define HDMI_IFRAME_FRAME               0x3
#define HDMI_IFRAME_MASK                0x3
#define HDMI_IFRAME_CFG_DI_N(x, n)       ((x) << ((n-1)*4)) /* n from 1 to 6 */

#define HDMI_CFG_DEVICE_EN              BIT(0)
#define HDMI_CFG_HDMI_NOT_DVI           BIT(1)
#define HDMI_CFG_HDCP_EN                BIT(2)
#define HDMI_CFG_ESS_NOT_OESS           BIT(3)
#define HDMI_CFG_H_SYNC_POL_NEG         BIT(4)
#define HDMI_CFG_SINK_TERM_DET_EN       BIT(5)
#define HDMI_CFG_V_SYNC_POL_NEG         BIT(6)
#define HDMI_CFG_422_EN                 BIT(8)
#define HDMI_CFG_FIFO_OVERRUN_CLR       BIT(12)
#define HDMI_CFG_FIFO_UNDERRUN_CLR      BIT(13)
#define HDMI_CFG_SW_RST_EN              BIT(31)

#define HDMI_INT_GLOBAL                 BIT(0)
#define HDMI_INT_SW_RST                 BIT(1)
#define HDMI_INT_PIX_CAP                BIT(3)
#define HDMI_INT_HOT_PLUG               BIT(4)
#define HDMI_INT_DLL_LCK                BIT(5)
#define HDMI_INT_NEW_FRAME              BIT(6)
#define HDMI_INT_GENCTRL_PKT            BIT(7)
#define HDMI_INT_SINK_TERM_PRESENT      BIT(11)

#define HDMI_DEFAULT_INT (HDMI_INT_SINK_TERM_PRESENT \
			| HDMI_INT_DLL_LCK \
			| HDMI_INT_HOT_PLUG \
			| HDMI_INT_GLOBAL)

#define HDMI_WORKING_INT (HDMI_INT_SINK_TERM_PRESENT \
			| HDMI_INT_GENCTRL_PKT \
			| HDMI_INT_NEW_FRAME \
			| HDMI_INT_DLL_LCK \
			| HDMI_INT_HOT_PLUG \
			| HDMI_INT_PIX_CAP \
			| HDMI_INT_SW_RST \
			| HDMI_INT_GLOBAL)

#define HDMI_STA_SW_RST                 BIT(1)

#define HDMI_INFOFRAME_HEADER_TYPE(x)    (((x) & 0xff) <<  0)
#define HDMI_INFOFRAME_HEADER_VERSION(x) (((x) & 0xff) <<  8)
#define HDMI_INFOFRAME_HEADER_LEN(x)     (((x) & 0x0f) << 16)

struct sti_hdmi_connector {
	struct drm_connector drm_connector;
	struct drm_encoder *encoder;
	struct sti_hdmi *hdmi;
};

#define to_sti_hdmi_connector(x) \
	container_of(x, struct sti_hdmi_connector, drm_connector)

u32 hdmi_read(struct sti_hdmi *hdmi, int offset)
{
	return readl(hdmi->regs + offset);
}

void hdmi_write(struct sti_hdmi *hdmi, u32 val, int offset)
{
	writel(val, hdmi->regs + offset);
}

/**
 * HDMI interrupt handler threaded
 *
 * @irq: irq number
 * @arg: connector structure
 */
static irqreturn_t hdmi_irq_thread(int irq, void *arg)
{
	struct sti_hdmi *hdmi = arg;

	/* Hot plug/unplug IRQ */
	if (hdmi->irq_status & HDMI_INT_HOT_PLUG) {
		hdmi->hpd = readl(hdmi->regs + HDMI_STA) & HDMI_STA_HOT_PLUG;
		if (hdmi->drm_dev)
			drm_helper_hpd_irq_event(hdmi->drm_dev);
	}

	/* Sw reset and PLL lock are exclusive so we can use the same
	 * event to signal them
	 */
	if (hdmi->irq_status & (HDMI_INT_SW_RST | HDMI_INT_DLL_LCK)) {
		hdmi->event_received = true;
		wake_up_interruptible(&hdmi->wait_event);
	}

	return IRQ_HANDLED;
}

/**
 * HDMI interrupt handler
 *
 * @irq: irq number
 * @arg: connector structure
 */
static irqreturn_t hdmi_irq(int irq, void *arg)
{
	struct sti_hdmi *hdmi = arg;

	/* read interrupt status */
	hdmi->irq_status = hdmi_read(hdmi, HDMI_INT_STA);

	/* clear interrupt status */
	hdmi_write(hdmi, hdmi->irq_status, HDMI_INT_CLR);

	/* force sync bus write */
	hdmi_read(hdmi, HDMI_INT_STA);

	return IRQ_WAKE_THREAD;
}

/**
 * Set hdmi active area depending on the drm display mode selected
 *
 * @hdmi: pointer on the hdmi internal structure
 */
static void hdmi_active_area(struct sti_hdmi *hdmi)
{
	u32 xmin, xmax;
	u32 ymin, ymax;

	xmin = sti_vtg_get_pixel_number(hdmi->mode, 1);
	xmax = sti_vtg_get_pixel_number(hdmi->mode, hdmi->mode.hdisplay);
	ymin = sti_vtg_get_line_number(hdmi->mode, 0);
	ymax = sti_vtg_get_line_number(hdmi->mode, hdmi->mode.vdisplay - 1);

	hdmi_write(hdmi, xmin, HDMI_ACTIVE_VID_XMIN);
	hdmi_write(hdmi, xmax, HDMI_ACTIVE_VID_XMAX);
	hdmi_write(hdmi, ymin, HDMI_ACTIVE_VID_YMIN);
	hdmi_write(hdmi, ymax, HDMI_ACTIVE_VID_YMAX);
}

/**
 * Overall hdmi configuration
 *
 * @hdmi: pointer on the hdmi internal structure
 */
static void hdmi_config(struct sti_hdmi *hdmi)
{
	u32 conf;

	DRM_DEBUG_DRIVER("\n");

	/* Clear overrun and underrun fifo */
	conf = HDMI_CFG_FIFO_OVERRUN_CLR | HDMI_CFG_FIFO_UNDERRUN_CLR;

	/* Enable HDMI mode not DVI */
	conf |= HDMI_CFG_HDMI_NOT_DVI | HDMI_CFG_ESS_NOT_OESS;

	/* Enable sink term detection */
	conf |= HDMI_CFG_SINK_TERM_DET_EN;

	/* Set Hsync polarity */
	if (hdmi->mode.flags & DRM_MODE_FLAG_NHSYNC) {
		DRM_DEBUG_DRIVER("H Sync Negative\n");
		conf |= HDMI_CFG_H_SYNC_POL_NEG;
	}

	/* Set Vsync polarity */
	if (hdmi->mode.flags & DRM_MODE_FLAG_NVSYNC) {
		DRM_DEBUG_DRIVER("V Sync Negative\n");
		conf |= HDMI_CFG_V_SYNC_POL_NEG;
	}

	/* Enable HDMI */
	conf |= HDMI_CFG_DEVICE_EN;

	hdmi_write(hdmi, conf, HDMI_CFG);
}

/**
 * Helper to concatenate infoframe in 32 bits word
 *
 * @ptr: pointer on the hdmi internal structure
 * @data: infoframe to write
 * @size: size to write
 */
static inline unsigned int hdmi_infoframe_subpack(const u8 *ptr, size_t size)
{
	unsigned long value = 0;
	size_t i;

	for (i = size; i > 0; i--)
		value = (value << 8) | ptr[i - 1];

	return value;
}

/**
 * Helper to write info frame
 *
 * @hdmi: pointer on the hdmi internal structure
 * @data: infoframe to write
 * @size: size to write
 */
static void hdmi_infoframe_write_infopack(struct sti_hdmi *hdmi, const u8 *data)
{
	const u8 *ptr = data;
	u32 val, slot, mode, i;
	u32 head_offset, pack_offset;
	size_t size;

	switch (*ptr) {
	case HDMI_INFOFRAME_TYPE_AVI:
		slot = HDMI_IFRAME_SLOT_AVI;
		mode = HDMI_IFRAME_FIELD;
		head_offset = HDMI_SW_DI_N_HEAD_WORD(HDMI_IFRAME_SLOT_AVI);
		pack_offset = HDMI_SW_DI_N_PKT_WORD0(HDMI_IFRAME_SLOT_AVI);
		size = HDMI_AVI_INFOFRAME_SIZE;
		break;

	case HDMI_INFOFRAME_TYPE_AUDIO:
		slot = HDMI_IFRAME_SLOT_AUDIO;
		mode = HDMI_IFRAME_FRAME;
		head_offset = HDMI_SW_DI_N_HEAD_WORD(HDMI_IFRAME_SLOT_AUDIO);
		pack_offset = HDMI_SW_DI_N_PKT_WORD0(HDMI_IFRAME_SLOT_AUDIO);
		size = HDMI_AUDIO_INFOFRAME_SIZE;
		break;

	default:
		DRM_ERROR("unsupported infoframe type: %#x\n", *ptr);
		return;
	}

	/* Disable transmission slot for updated infoframe */
	val = hdmi_read(hdmi, HDMI_SW_DI_CFG);
	val &= ~HDMI_IFRAME_CFG_DI_N(HDMI_IFRAME_MASK, slot);
	hdmi_write(hdmi, val, HDMI_SW_DI_CFG);

	val = HDMI_INFOFRAME_HEADER_TYPE(*ptr++);
	val |= HDMI_INFOFRAME_HEADER_VERSION(*ptr++);
	val |= HDMI_INFOFRAME_HEADER_LEN(*ptr++);
	writel(val, hdmi->regs + head_offset);

	/*
	 * Each subpack contains 4 bytes
	 * The First Bytes of the first subpacket must contain the checksum
	 * Packet size in increase by one.
	 */
	for (i = 0; i < size; i += sizeof(u32)) {
		size_t num;

		num = min_t(size_t, size - i, sizeof(u32));
		val = hdmi_infoframe_subpack(ptr, num);
		ptr += sizeof(u32);
		writel(val, hdmi->regs + pack_offset + i);
	}

	/* Enable transmission slot for updated infoframe */
	val = hdmi_read(hdmi, HDMI_SW_DI_CFG);
	val |= HDMI_IFRAME_CFG_DI_N(HDMI_IFRAME_FIELD, slot);
	hdmi_write(hdmi, val, HDMI_SW_DI_CFG);
}

/**
 * Prepare and configure the AVI infoframe
 *
 * AVI infoframe are transmitted at least once per two video field and
 * contains information about HDMI transmission mode such as color space,
 * colorimetry, ...
 *
 * @hdmi: pointer on the hdmi internal structure
 *
 * Return negative value if error occurs
 */
static int hdmi_avi_infoframe_config(struct sti_hdmi *hdmi)
{
	struct drm_display_mode *mode = &hdmi->mode;
	struct hdmi_avi_infoframe infoframe;
	u8 buffer[HDMI_INFOFRAME_SIZE(AVI)];
	int ret;

	DRM_DEBUG_DRIVER("\n");

	ret = drm_hdmi_avi_infoframe_from_display_mode(&infoframe, mode);
	if (ret < 0) {
		DRM_ERROR("failed to setup AVI infoframe: %d\n", ret);
		return ret;
	}

	/* fixed infoframe configuration not linked to the mode */
	infoframe.colorspace = HDMI_COLORSPACE_RGB;
	infoframe.quantization_range = HDMI_QUANTIZATION_RANGE_DEFAULT;
	infoframe.colorimetry = HDMI_COLORIMETRY_NONE;

	ret = hdmi_avi_infoframe_pack(&infoframe, buffer, sizeof(buffer));
	if (ret < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %d\n", ret);
		return ret;
	}

	hdmi_infoframe_write_infopack(hdmi, buffer);

	return 0;
}

/**
 * Prepare and configure the AUDIO infoframe
 *
 * AUDIO infoframe are transmitted once per frame and
 * contains information about HDMI transmission mode such as audio codec,
 * sample size, ...
 *
 * @hdmi: pointer on the hdmi internal structure
 *
 * Return negative value if error occurs
 */
static int hdmi_audio_infoframe_config(struct sti_hdmi *hdmi)
{
	struct hdmi_audio_infoframe infofame;
	u8 buffer[HDMI_INFOFRAME_SIZE(AUDIO)];
	int ret;

	ret = hdmi_audio_infoframe_init(&infofame);
	if (ret < 0) {
		DRM_ERROR("failed to setup audio infoframe: %d\n", ret);
		return ret;
	}

	infofame.channels = 2;

	ret = hdmi_audio_infoframe_pack(&infofame, buffer, sizeof(buffer));
	if (ret < 0) {
		DRM_ERROR("failed to pack audio infoframe: %d\n", ret);
		return ret;
	}

	hdmi_infoframe_write_infopack(hdmi, buffer);

	return 0;
}

/**
 * Software reset of the hdmi subsystem
 *
 * @hdmi: pointer on the hdmi internal structure
 *
 */
#define HDMI_TIMEOUT_SWRESET  100   /*milliseconds */
static void hdmi_swreset(struct sti_hdmi *hdmi)
{
	u32 val;

	DRM_DEBUG_DRIVER("\n");

	/* Enable hdmi_audio clock only during hdmi reset */
	if (clk_prepare_enable(hdmi->clk_audio))
		DRM_INFO("Failed to prepare/enable hdmi_audio clk\n");

	/* Sw reset */
	hdmi->event_received = false;

	val = hdmi_read(hdmi, HDMI_CFG);
	val |= HDMI_CFG_SW_RST_EN;
	hdmi_write(hdmi, val, HDMI_CFG);

	/* Wait reset completed */
	wait_event_interruptible_timeout(hdmi->wait_event,
					 hdmi->event_received == true,
					 msecs_to_jiffies
					 (HDMI_TIMEOUT_SWRESET));

	/*
	 * HDMI_STA_SW_RST bit is set to '1' when SW_RST bit in HDMI_CFG is
	 * set to '1' and clk_audio is running.
	 */
	if ((hdmi_read(hdmi, HDMI_STA) & HDMI_STA_SW_RST) == 0)
		DRM_DEBUG_DRIVER("Warning: HDMI sw reset timeout occurs\n");

	val = hdmi_read(hdmi, HDMI_CFG);
	val &= ~HDMI_CFG_SW_RST_EN;
	hdmi_write(hdmi, val, HDMI_CFG);

	/* Disable hdmi_audio clock. Not used anymore for drm purpose */
	clk_disable_unprepare(hdmi->clk_audio);
}

static void sti_hdmi_disable(struct drm_bridge *bridge)
{
	struct sti_hdmi *hdmi = bridge->driver_private;

	u32 val = hdmi_read(hdmi, HDMI_CFG);

	if (!hdmi->enabled)
		return;

	DRM_DEBUG_DRIVER("\n");

	/* Disable HDMI */
	val &= ~HDMI_CFG_DEVICE_EN;
	hdmi_write(hdmi, val, HDMI_CFG);

	hdmi_write(hdmi, 0xffffffff, HDMI_INT_CLR);

	/* Stop the phy */
	hdmi->phy_ops->stop(hdmi);

	/* Set the default channel data to be a dark red */
	hdmi_write(hdmi, 0x0000, HDMI_DFLT_CHL0_DAT);
	hdmi_write(hdmi, 0x0000, HDMI_DFLT_CHL1_DAT);
	hdmi_write(hdmi, 0x0060, HDMI_DFLT_CHL2_DAT);

	/* Disable/unprepare hdmi clock */
	clk_disable_unprepare(hdmi->clk_phy);
	clk_disable_unprepare(hdmi->clk_tmds);
	clk_disable_unprepare(hdmi->clk_pix);

	hdmi->enabled = false;
}

static void sti_hdmi_pre_enable(struct drm_bridge *bridge)
{
	struct sti_hdmi *hdmi = bridge->driver_private;

	DRM_DEBUG_DRIVER("\n");

	if (hdmi->enabled)
		return;

	/* Prepare/enable clocks */
	if (clk_prepare_enable(hdmi->clk_pix))
		DRM_ERROR("Failed to prepare/enable hdmi_pix clk\n");
	if (clk_prepare_enable(hdmi->clk_tmds))
		DRM_ERROR("Failed to prepare/enable hdmi_tmds clk\n");
	if (clk_prepare_enable(hdmi->clk_phy))
		DRM_ERROR("Failed to prepare/enable hdmi_rejec_pll clk\n");

	hdmi->enabled = true;

	/* Program hdmi serializer and start phy */
	if (!hdmi->phy_ops->start(hdmi)) {
		DRM_ERROR("Unable to start hdmi phy\n");
		return;
	}

	/* Program hdmi active area */
	hdmi_active_area(hdmi);

	/* Enable working interrupts */
	hdmi_write(hdmi, HDMI_WORKING_INT, HDMI_INT_EN);

	/* Program hdmi config */
	hdmi_config(hdmi);

	/* Program AVI infoframe */
	if (hdmi_avi_infoframe_config(hdmi))
		DRM_ERROR("Unable to configure AVI infoframe\n");

	/* Program AUDIO infoframe */
	if (hdmi_audio_infoframe_config(hdmi))
		DRM_ERROR("Unable to configure AUDIO infoframe\n");

	/* Sw reset */
	hdmi_swreset(hdmi);
}

static void sti_hdmi_set_mode(struct drm_bridge *bridge,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	struct sti_hdmi *hdmi = bridge->driver_private;
	int ret;

	DRM_DEBUG_DRIVER("\n");

	/* Copy the drm display mode in the connector local structure */
	memcpy(&hdmi->mode, mode, sizeof(struct drm_display_mode));

	/* Update clock framerate according to the selected mode */
	ret = clk_set_rate(hdmi->clk_pix, mode->clock * 1000);
	if (ret < 0) {
		DRM_ERROR("Cannot set rate (%dHz) for hdmi_pix clk\n",
			  mode->clock * 1000);
		return;
	}
	ret = clk_set_rate(hdmi->clk_phy, mode->clock * 1000);
	if (ret < 0) {
		DRM_ERROR("Cannot set rate (%dHz) for hdmi_rejection_pll clk\n",
			  mode->clock * 1000);
		return;
	}
}

static void sti_hdmi_bridge_nope(struct drm_bridge *bridge)
{
	/* do nothing */
}

static const struct drm_bridge_funcs sti_hdmi_bridge_funcs = {
	.pre_enable = sti_hdmi_pre_enable,
	.enable = sti_hdmi_bridge_nope,
	.disable = sti_hdmi_disable,
	.post_disable = sti_hdmi_bridge_nope,
	.mode_set = sti_hdmi_set_mode,
};

static int sti_hdmi_connector_get_modes(struct drm_connector *connector)
{
	struct sti_hdmi_connector *hdmi_connector
		= to_sti_hdmi_connector(connector);
	struct sti_hdmi *hdmi = hdmi_connector->hdmi;
	struct edid *edid;
	int count;

	DRM_DEBUG_DRIVER("\n");

	edid = drm_get_edid(connector, hdmi->ddc_adapt);
	if (!edid)
		goto fail;

	count = drm_add_edid_modes(connector, edid);
	drm_mode_connector_update_edid_property(connector, edid);

	kfree(edid);
	return count;

fail:
	DRM_ERROR("Can't read HDMI EDID\n");
	return 0;
}

#define CLK_TOLERANCE_HZ 50

static int sti_hdmi_connector_mode_valid(struct drm_connector *connector,
					struct drm_display_mode *mode)
{
	int target = mode->clock * 1000;
	int target_min = target - CLK_TOLERANCE_HZ;
	int target_max = target + CLK_TOLERANCE_HZ;
	int result;
	struct sti_hdmi_connector *hdmi_connector
		= to_sti_hdmi_connector(connector);
	struct sti_hdmi *hdmi = hdmi_connector->hdmi;


	result = clk_round_rate(hdmi->clk_pix, target);

	DRM_DEBUG_DRIVER("target rate = %d => available rate = %d\n",
			 target, result);

	if ((result < target_min) || (result > target_max)) {
		DRM_DEBUG_DRIVER("hdmi pixclk=%d not supported\n", target);
		return MODE_BAD;
	}

	return MODE_OK;
}

struct drm_encoder *sti_hdmi_best_encoder(struct drm_connector *connector)
{
	struct sti_hdmi_connector *hdmi_connector
		= to_sti_hdmi_connector(connector);

	/* Best encoder is the one associated during connector creation */
	return hdmi_connector->encoder;
}

static const
struct drm_connector_helper_funcs sti_hdmi_connector_helper_funcs = {
	.get_modes = sti_hdmi_connector_get_modes,
	.mode_valid = sti_hdmi_connector_mode_valid,
	.best_encoder = sti_hdmi_best_encoder,
};

/* get detection status of display device */
static enum drm_connector_status
sti_hdmi_connector_detect(struct drm_connector *connector, bool force)
{
	struct sti_hdmi_connector *hdmi_connector
		= to_sti_hdmi_connector(connector);
	struct sti_hdmi *hdmi = hdmi_connector->hdmi;

	DRM_DEBUG_DRIVER("\n");

	if (hdmi->hpd) {
		DRM_DEBUG_DRIVER("hdmi cable connected\n");
		return connector_status_connected;
	}

	DRM_DEBUG_DRIVER("hdmi cable disconnected\n");
	return connector_status_disconnected;
}

static void sti_hdmi_connector_destroy(struct drm_connector *connector)
{
	struct sti_hdmi_connector *hdmi_connector
		= to_sti_hdmi_connector(connector);

	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(hdmi_connector);
}

static const struct drm_connector_funcs sti_hdmi_connector_funcs = {
	.dpms = drm_atomic_helper_connector_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = sti_hdmi_connector_detect,
	.destroy = sti_hdmi_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_encoder *sti_hdmi_find_encoder(struct drm_device *dev)
{
	struct drm_encoder *encoder;

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->encoder_type == DRM_MODE_ENCODER_TMDS)
			return encoder;
	}

	return NULL;
}

static int sti_hdmi_bind(struct device *dev, struct device *master, void *data)
{
	struct sti_hdmi *hdmi = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder;
	struct sti_hdmi_connector *connector;
	struct drm_connector *drm_connector;
	struct drm_bridge *bridge;
	int err;

	/* Set the drm device handle */
	hdmi->drm_dev = drm_dev;

	encoder = sti_hdmi_find_encoder(drm_dev);
	if (!encoder)
		return -EINVAL;

	connector = devm_kzalloc(dev, sizeof(*connector), GFP_KERNEL);
	if (!connector)
		return -EINVAL;

	connector->hdmi = hdmi;

	bridge = devm_kzalloc(dev, sizeof(*bridge), GFP_KERNEL);
	if (!bridge)
		return -EINVAL;

	bridge->driver_private = hdmi;
	bridge->funcs = &sti_hdmi_bridge_funcs;
	drm_bridge_attach(drm_dev, bridge);

	encoder->bridge = bridge;
	connector->encoder = encoder;

	drm_connector = (struct drm_connector *)connector;

	drm_connector->polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_init(drm_dev, drm_connector,
			&sti_hdmi_connector_funcs, DRM_MODE_CONNECTOR_HDMIA);
	drm_connector_helper_add(drm_connector,
			&sti_hdmi_connector_helper_funcs);

	err = drm_connector_register(drm_connector);
	if (err)
		goto err_connector;

	err = drm_mode_connector_attach_encoder(drm_connector, encoder);
	if (err) {
		DRM_ERROR("Failed to attach a connector to a encoder\n");
		goto err_sysfs;
	}

	/* Enable default interrupts */
	hdmi_write(hdmi, HDMI_DEFAULT_INT, HDMI_INT_EN);

	return 0;

err_sysfs:
	drm_connector_unregister(drm_connector);
err_connector:
	drm_connector_cleanup(drm_connector);

	return -EINVAL;
}

static void sti_hdmi_unbind(struct device *dev,
		struct device *master, void *data)
{
	/* do nothing */
}

static const struct component_ops sti_hdmi_ops = {
	.bind = sti_hdmi_bind,
	.unbind = sti_hdmi_unbind,
};

static const struct of_device_id hdmi_of_match[] = {
	{
		.compatible = "st,stih416-hdmi",
		.data = &tx3g0c55phy_ops,
	}, {
		.compatible = "st,stih407-hdmi",
		.data = &tx3g4c28phy_ops,
	}, {
		/* end node */
	}
};
MODULE_DEVICE_TABLE(of, hdmi_of_match);

static int sti_hdmi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sti_hdmi *hdmi;
	struct device_node *np = dev->of_node;
	struct resource *res;
	struct device_node *ddc;
	int ret;

	DRM_INFO("%s\n", __func__);

	hdmi = devm_kzalloc(dev, sizeof(*hdmi), GFP_KERNEL);
	if (!hdmi)
		return -ENOMEM;

	ddc = of_parse_phandle(pdev->dev.of_node, "ddc", 0);
	if (ddc) {
		hdmi->ddc_adapt = of_get_i2c_adapter_by_node(ddc);
		of_node_put(ddc);
		if (!hdmi->ddc_adapt)
			return -EPROBE_DEFER;
	}

	hdmi->dev = pdev->dev;

	/* Get resources */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "hdmi-reg");
	if (!res) {
		DRM_ERROR("Invalid hdmi resource\n");
		ret = -ENOMEM;
		goto release_adapter;
	}
	hdmi->regs = devm_ioremap_nocache(dev, res->start, resource_size(res));
	if (!hdmi->regs) {
		ret = -ENOMEM;
		goto release_adapter;
	}

	if (of_device_is_compatible(np, "st,stih416-hdmi")) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "syscfg");
		if (!res) {
			DRM_ERROR("Invalid syscfg resource\n");
			ret = -ENOMEM;
			goto release_adapter;
		}
		hdmi->syscfg = devm_ioremap_nocache(dev, res->start,
						    resource_size(res));
		if (!hdmi->syscfg) {
			ret = -ENOMEM;
			goto release_adapter;
		}
	}

	hdmi->phy_ops = (struct hdmi_phy_ops *)
		of_match_node(hdmi_of_match, np)->data;

	/* Get clock resources */
	hdmi->clk_pix = devm_clk_get(dev, "pix");
	if (IS_ERR(hdmi->clk_pix)) {
		DRM_ERROR("Cannot get hdmi_pix clock\n");
		ret = PTR_ERR(hdmi->clk_pix);
		goto release_adapter;
	}

	hdmi->clk_tmds = devm_clk_get(dev, "tmds");
	if (IS_ERR(hdmi->clk_tmds)) {
		DRM_ERROR("Cannot get hdmi_tmds clock\n");
		ret = PTR_ERR(hdmi->clk_tmds);
		goto release_adapter;
	}

	hdmi->clk_phy = devm_clk_get(dev, "phy");
	if (IS_ERR(hdmi->clk_phy)) {
		DRM_ERROR("Cannot get hdmi_phy clock\n");
		ret = PTR_ERR(hdmi->clk_phy);
		goto release_adapter;
	}

	hdmi->clk_audio = devm_clk_get(dev, "audio");
	if (IS_ERR(hdmi->clk_audio)) {
		DRM_ERROR("Cannot get hdmi_audio clock\n");
		ret = PTR_ERR(hdmi->clk_audio);
		goto release_adapter;
	}

	hdmi->hpd = readl(hdmi->regs + HDMI_STA) & HDMI_STA_HOT_PLUG;

	init_waitqueue_head(&hdmi->wait_event);

	hdmi->irq = platform_get_irq_byname(pdev, "irq");

	ret = devm_request_threaded_irq(dev, hdmi->irq, hdmi_irq,
			hdmi_irq_thread, IRQF_ONESHOT, dev_name(dev), hdmi);
	if (ret) {
		DRM_ERROR("Failed to register HDMI interrupt\n");
		goto release_adapter;
	}

	hdmi->reset = devm_reset_control_get(dev, "hdmi");
	/* Take hdmi out of reset */
	if (!IS_ERR(hdmi->reset))
		reset_control_deassert(hdmi->reset);

	platform_set_drvdata(pdev, hdmi);

	return component_add(&pdev->dev, &sti_hdmi_ops);

 release_adapter:
	i2c_put_adapter(hdmi->ddc_adapt);

	return ret;
}

static int sti_hdmi_remove(struct platform_device *pdev)
{
	struct sti_hdmi *hdmi = dev_get_drvdata(&pdev->dev);

	i2c_put_adapter(hdmi->ddc_adapt);
	component_del(&pdev->dev, &sti_hdmi_ops);

	return 0;
}

struct platform_driver sti_hdmi_driver = {
	.driver = {
		.name = "sti-hdmi",
		.owner = THIS_MODULE,
		.of_match_table = hdmi_of_match,
	},
	.probe = sti_hdmi_probe,
	.remove = sti_hdmi_remove,
};

MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@st.com>");
MODULE_DESCRIPTION("STMicroelectronics SoC DRM driver");
MODULE_LICENSE("GPL");
