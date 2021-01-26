// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics SA 2014
 * Author: Vincent Abriou <vincent.abriou@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/debugfs.h>
#include <linux/hdmi.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include <sound/hdmi-codec.h>

#include "sti_hdmi.h"
#include "sti_hdmi_tx3g4c28phy.h"
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
#define HDMI_AUDIO_CFG                  0x0200
#define HDMI_SPDIF_FIFO_STATUS          0x0204
#define HDMI_SW_DI_1_HEAD_WORD          0x0210
#define HDMI_SW_DI_1_PKT_WORD0          0x0214
#define HDMI_SW_DI_1_PKT_WORD1          0x0218
#define HDMI_SW_DI_1_PKT_WORD2          0x021C
#define HDMI_SW_DI_1_PKT_WORD3          0x0220
#define HDMI_SW_DI_1_PKT_WORD4          0x0224
#define HDMI_SW_DI_1_PKT_WORD5          0x0228
#define HDMI_SW_DI_1_PKT_WORD6          0x022C
#define HDMI_SW_DI_CFG                  0x0230
#define HDMI_SAMPLE_FLAT_MASK           0x0244
#define HDMI_AUDN                       0x0400
#define HDMI_AUD_CTS                    0x0404
#define HDMI_SW_DI_2_HEAD_WORD          0x0600
#define HDMI_SW_DI_2_PKT_WORD0          0x0604
#define HDMI_SW_DI_2_PKT_WORD1          0x0608
#define HDMI_SW_DI_2_PKT_WORD2          0x060C
#define HDMI_SW_DI_2_PKT_WORD3          0x0610
#define HDMI_SW_DI_2_PKT_WORD4          0x0614
#define HDMI_SW_DI_2_PKT_WORD5          0x0618
#define HDMI_SW_DI_2_PKT_WORD6          0x061C
#define HDMI_SW_DI_3_HEAD_WORD          0x0620
#define HDMI_SW_DI_3_PKT_WORD0          0x0624
#define HDMI_SW_DI_3_PKT_WORD1          0x0628
#define HDMI_SW_DI_3_PKT_WORD2          0x062C
#define HDMI_SW_DI_3_PKT_WORD3          0x0630
#define HDMI_SW_DI_3_PKT_WORD4          0x0634
#define HDMI_SW_DI_3_PKT_WORD5          0x0638
#define HDMI_SW_DI_3_PKT_WORD6          0x063C

#define HDMI_IFRAME_SLOT_AVI            1
#define HDMI_IFRAME_SLOT_AUDIO          2
#define HDMI_IFRAME_SLOT_VENDOR         3

#define  XCAT(prefix, x, suffix)        prefix ## x ## suffix
#define  HDMI_SW_DI_N_HEAD_WORD(x)      XCAT(HDMI_SW_DI_, x, _HEAD_WORD)
#define  HDMI_SW_DI_N_PKT_WORD0(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD0)
#define  HDMI_SW_DI_N_PKT_WORD1(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD1)
#define  HDMI_SW_DI_N_PKT_WORD2(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD2)
#define  HDMI_SW_DI_N_PKT_WORD3(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD3)
#define  HDMI_SW_DI_N_PKT_WORD4(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD4)
#define  HDMI_SW_DI_N_PKT_WORD5(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD5)
#define  HDMI_SW_DI_N_PKT_WORD6(x)      XCAT(HDMI_SW_DI_, x, _PKT_WORD6)

#define HDMI_SW_DI_MAX_WORD             7

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
#define HDMI_INT_AUDIO_FIFO_XRUN        BIT(8)
#define HDMI_INT_SINK_TERM_PRESENT      BIT(11)

#define HDMI_DEFAULT_INT (HDMI_INT_SINK_TERM_PRESENT \
			| HDMI_INT_DLL_LCK \
			| HDMI_INT_HOT_PLUG \
			| HDMI_INT_GLOBAL)

#define HDMI_WORKING_INT (HDMI_INT_SINK_TERM_PRESENT \
			| HDMI_INT_AUDIO_FIFO_XRUN \
			| HDMI_INT_GENCTRL_PKT \
			| HDMI_INT_NEW_FRAME \
			| HDMI_INT_DLL_LCK \
			| HDMI_INT_HOT_PLUG \
			| HDMI_INT_PIX_CAP \
			| HDMI_INT_SW_RST \
			| HDMI_INT_GLOBAL)

#define HDMI_STA_SW_RST                 BIT(1)

#define HDMI_AUD_CFG_8CH		BIT(0)
#define HDMI_AUD_CFG_SPDIF_DIV_2	BIT(1)
#define HDMI_AUD_CFG_SPDIF_DIV_3	BIT(2)
#define HDMI_AUD_CFG_SPDIF_CLK_DIV_4	(BIT(1) | BIT(2))
#define HDMI_AUD_CFG_CTS_CLK_256FS	BIT(12)
#define HDMI_AUD_CFG_DTS_INVALID	BIT(16)
#define HDMI_AUD_CFG_ONE_BIT_INVALID	(BIT(18) | BIT(19) | BIT(20) |  BIT(21))
#define HDMI_AUD_CFG_CH12_VALID	BIT(28)
#define HDMI_AUD_CFG_CH34_VALID	BIT(29)
#define HDMI_AUD_CFG_CH56_VALID	BIT(30)
#define HDMI_AUD_CFG_CH78_VALID	BIT(31)

/* sample flat mask */
#define HDMI_SAMPLE_FLAT_NO	 0
#define HDMI_SAMPLE_FLAT_SP0 BIT(0)
#define HDMI_SAMPLE_FLAT_SP1 BIT(1)
#define HDMI_SAMPLE_FLAT_SP2 BIT(2)
#define HDMI_SAMPLE_FLAT_SP3 BIT(3)
#define HDMI_SAMPLE_FLAT_ALL (HDMI_SAMPLE_FLAT_SP0 | HDMI_SAMPLE_FLAT_SP1 |\
			      HDMI_SAMPLE_FLAT_SP2 | HDMI_SAMPLE_FLAT_SP3)

#define HDMI_INFOFRAME_HEADER_TYPE(x)    (((x) & 0xff) <<  0)
#define HDMI_INFOFRAME_HEADER_VERSION(x) (((x) & 0xff) <<  8)
#define HDMI_INFOFRAME_HEADER_LEN(x)     (((x) & 0x0f) << 16)

struct sti_hdmi_connector {
	struct drm_connector drm_connector;
	struct drm_encoder *encoder;
	struct sti_hdmi *hdmi;
	struct drm_property *colorspace_property;
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

	/* Audio FIFO underrun IRQ */
	if (hdmi->irq_status & HDMI_INT_AUDIO_FIFO_XRUN)
		DRM_INFO("Warning: audio FIFO underrun occurs!\n");

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

	/* Select encryption type and the framing mode */
	conf |= HDMI_CFG_ESS_NOT_OESS;
	if (hdmi->hdmi_monitor)
		conf |= HDMI_CFG_HDMI_NOT_DVI;

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

/*
 * Helper to reset info frame
 *
 * @hdmi: pointer on the hdmi internal structure
 * @slot: infoframe to reset
 */
static void hdmi_infoframe_reset(struct sti_hdmi *hdmi,
				 u32 slot)
{
	u32 val, i;
	u32 head_offset, pack_offset;

	switch (slot) {
	case HDMI_IFRAME_SLOT_AVI:
		head_offset = HDMI_SW_DI_N_HEAD_WORD(HDMI_IFRAME_SLOT_AVI);
		pack_offset = HDMI_SW_DI_N_PKT_WORD0(HDMI_IFRAME_SLOT_AVI);
		break;
	case HDMI_IFRAME_SLOT_AUDIO:
		head_offset = HDMI_SW_DI_N_HEAD_WORD(HDMI_IFRAME_SLOT_AUDIO);
		pack_offset = HDMI_SW_DI_N_PKT_WORD0(HDMI_IFRAME_SLOT_AUDIO);
		break;
	case HDMI_IFRAME_SLOT_VENDOR:
		head_offset = HDMI_SW_DI_N_HEAD_WORD(HDMI_IFRAME_SLOT_VENDOR);
		pack_offset = HDMI_SW_DI_N_PKT_WORD0(HDMI_IFRAME_SLOT_VENDOR);
		break;
	default:
		DRM_ERROR("unsupported infoframe slot: %#x\n", slot);
		return;
	}

	/* Disable transmission for the selected slot */
	val = hdmi_read(hdmi, HDMI_SW_DI_CFG);
	val &= ~HDMI_IFRAME_CFG_DI_N(HDMI_IFRAME_MASK, slot);
	hdmi_write(hdmi, val, HDMI_SW_DI_CFG);

	/* Reset info frame registers */
	hdmi_write(hdmi, 0x0, head_offset);
	for (i = 0; i < HDMI_SW_DI_MAX_WORD; i += sizeof(u32))
		hdmi_write(hdmi, 0x0, pack_offset + i);
}

/**
 * Helper to concatenate infoframe in 32 bits word
 *
 * @ptr: pointer on the hdmi internal structure
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
static void hdmi_infoframe_write_infopack(struct sti_hdmi *hdmi,
					  const u8 *data,
					  size_t size)
{
	const u8 *ptr = data;
	u32 val, slot, mode, i;
	u32 head_offset, pack_offset;

	switch (*ptr) {
	case HDMI_INFOFRAME_TYPE_AVI:
		slot = HDMI_IFRAME_SLOT_AVI;
		mode = HDMI_IFRAME_FIELD;
		head_offset = HDMI_SW_DI_N_HEAD_WORD(HDMI_IFRAME_SLOT_AVI);
		pack_offset = HDMI_SW_DI_N_PKT_WORD0(HDMI_IFRAME_SLOT_AVI);
		break;
	case HDMI_INFOFRAME_TYPE_AUDIO:
		slot = HDMI_IFRAME_SLOT_AUDIO;
		mode = HDMI_IFRAME_FRAME;
		head_offset = HDMI_SW_DI_N_HEAD_WORD(HDMI_IFRAME_SLOT_AUDIO);
		pack_offset = HDMI_SW_DI_N_PKT_WORD0(HDMI_IFRAME_SLOT_AUDIO);
		break;
	case HDMI_INFOFRAME_TYPE_VENDOR:
		slot = HDMI_IFRAME_SLOT_VENDOR;
		mode = HDMI_IFRAME_FRAME;
		head_offset = HDMI_SW_DI_N_HEAD_WORD(HDMI_IFRAME_SLOT_VENDOR);
		pack_offset = HDMI_SW_DI_N_PKT_WORD0(HDMI_IFRAME_SLOT_VENDOR);
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
	 * Packet size is increase by one.
	 */
	size = size - HDMI_INFOFRAME_HEADER_SIZE + 1;
	for (i = 0; i < size; i += sizeof(u32)) {
		size_t num;

		num = min_t(size_t, size - i, sizeof(u32));
		val = hdmi_infoframe_subpack(ptr, num);
		ptr += sizeof(u32);
		writel(val, hdmi->regs + pack_offset + i);
	}

	/* Enable transmission slot for updated infoframe */
	val = hdmi_read(hdmi, HDMI_SW_DI_CFG);
	val |= HDMI_IFRAME_CFG_DI_N(mode, slot);
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

	ret = drm_hdmi_avi_infoframe_from_display_mode(&infoframe,
						       hdmi->drm_connector, mode);
	if (ret < 0) {
		DRM_ERROR("failed to setup AVI infoframe: %d\n", ret);
		return ret;
	}

	/* fixed infoframe configuration not linked to the mode */
	infoframe.colorspace = hdmi->colorspace;
	infoframe.quantization_range = HDMI_QUANTIZATION_RANGE_DEFAULT;
	infoframe.colorimetry = HDMI_COLORIMETRY_NONE;

	ret = hdmi_avi_infoframe_pack(&infoframe, buffer, sizeof(buffer));
	if (ret < 0) {
		DRM_ERROR("failed to pack AVI infoframe: %d\n", ret);
		return ret;
	}

	hdmi_infoframe_write_infopack(hdmi, buffer, ret);

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
	struct hdmi_audio_params *audio = &hdmi->audio;
	u8 buffer[HDMI_INFOFRAME_SIZE(AUDIO)];
	int ret, val;

	DRM_DEBUG_DRIVER("enter %s, AIF %s\n", __func__,
			 audio->enabled ? "enable" : "disable");
	if (audio->enabled) {
		/* set audio parameters stored*/
		ret = hdmi_audio_infoframe_pack(&audio->cea, buffer,
						sizeof(buffer));
		if (ret < 0) {
			DRM_ERROR("failed to pack audio infoframe: %d\n", ret);
			return ret;
		}
		hdmi_infoframe_write_infopack(hdmi, buffer, ret);
	} else {
		/*disable audio info frame transmission */
		val = hdmi_read(hdmi, HDMI_SW_DI_CFG);
		val &= ~HDMI_IFRAME_CFG_DI_N(HDMI_IFRAME_MASK,
					     HDMI_IFRAME_SLOT_AUDIO);
		hdmi_write(hdmi, val, HDMI_SW_DI_CFG);
	}

	return 0;
}

/*
 * Prepare and configure the VS infoframe
 *
 * Vendor Specific infoframe are transmitted once per frame and
 * contains vendor specific information.
 *
 * @hdmi: pointer on the hdmi internal structure
 *
 * Return negative value if error occurs
 */
#define HDMI_VENDOR_INFOFRAME_MAX_SIZE 6
static int hdmi_vendor_infoframe_config(struct sti_hdmi *hdmi)
{
	struct drm_display_mode *mode = &hdmi->mode;
	struct hdmi_vendor_infoframe infoframe;
	u8 buffer[HDMI_INFOFRAME_HEADER_SIZE + HDMI_VENDOR_INFOFRAME_MAX_SIZE];
	int ret;

	DRM_DEBUG_DRIVER("\n");

	ret = drm_hdmi_vendor_infoframe_from_display_mode(&infoframe,
							  hdmi->drm_connector,
							  mode);
	if (ret < 0) {
		/*
		 * Going into that statement does not means vendor infoframe
		 * fails. It just informed us that vendor infoframe is not
		 * needed for the selected mode. Only  4k or stereoscopic 3D
		 * mode requires vendor infoframe. So just simply return 0.
		 */
		return 0;
	}

	ret = hdmi_vendor_infoframe_pack(&infoframe, buffer, sizeof(buffer));
	if (ret < 0) {
		DRM_ERROR("failed to pack VS infoframe: %d\n", ret);
		return ret;
	}

	hdmi_infoframe_write_infopack(hdmi, buffer, ret);

	return 0;
}

#define HDMI_TIMEOUT_SWRESET  100   /*milliseconds */

/**
 * Software reset of the hdmi subsystem
 *
 * @hdmi: pointer on the hdmi internal structure
 *
 */
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
					 hdmi->event_received,
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

#define DBGFS_PRINT_STR(str1, str2) seq_printf(s, "%-24s %s\n", str1, str2)
#define DBGFS_PRINT_INT(str1, int2) seq_printf(s, "%-24s %d\n", str1, int2)
#define DBGFS_DUMP(str, reg) seq_printf(s, "%s  %-25s 0x%08X", str, #reg, \
					hdmi_read(hdmi, reg))
#define DBGFS_DUMP_DI(reg, slot) DBGFS_DUMP("\n", reg(slot))

static void hdmi_dbg_cfg(struct seq_file *s, int val)
{
	int tmp;

	seq_putc(s, '\t');
	tmp = val & HDMI_CFG_HDMI_NOT_DVI;
	DBGFS_PRINT_STR("mode:", tmp ? "HDMI" : "DVI");
	seq_puts(s, "\t\t\t\t\t");
	tmp = val & HDMI_CFG_HDCP_EN;
	DBGFS_PRINT_STR("HDCP:", tmp ? "enable" : "disable");
	seq_puts(s, "\t\t\t\t\t");
	tmp = val & HDMI_CFG_ESS_NOT_OESS;
	DBGFS_PRINT_STR("HDCP mode:", tmp ? "ESS enable" : "OESS enable");
	seq_puts(s, "\t\t\t\t\t");
	tmp = val & HDMI_CFG_H_SYNC_POL_NEG;
	DBGFS_PRINT_STR("Hsync polarity:", tmp ? "inverted" : "normal");
	seq_puts(s, "\t\t\t\t\t");
	tmp = val & HDMI_CFG_V_SYNC_POL_NEG;
	DBGFS_PRINT_STR("Vsync polarity:", tmp ? "inverted" : "normal");
	seq_puts(s, "\t\t\t\t\t");
	tmp = val & HDMI_CFG_422_EN;
	DBGFS_PRINT_STR("YUV422 format:", tmp ? "enable" : "disable");
}

static void hdmi_dbg_sta(struct seq_file *s, int val)
{
	int tmp;

	seq_putc(s, '\t');
	tmp = (val & HDMI_STA_DLL_LCK);
	DBGFS_PRINT_STR("pll:", tmp ? "locked" : "not locked");
	seq_puts(s, "\t\t\t\t\t");
	tmp = (val & HDMI_STA_HOT_PLUG);
	DBGFS_PRINT_STR("hdmi cable:", tmp ? "connected" : "not connected");
}

static void hdmi_dbg_sw_di_cfg(struct seq_file *s, int val)
{
	int tmp;
	char *const en_di[] = {"no transmission",
			       "single transmission",
			       "once every field",
			       "once every frame"};

	seq_putc(s, '\t');
	tmp = (val & HDMI_IFRAME_CFG_DI_N(HDMI_IFRAME_MASK, 1));
	DBGFS_PRINT_STR("Data island 1:", en_di[tmp]);
	seq_puts(s, "\t\t\t\t\t");
	tmp = (val & HDMI_IFRAME_CFG_DI_N(HDMI_IFRAME_MASK, 2)) >> 4;
	DBGFS_PRINT_STR("Data island 2:", en_di[tmp]);
	seq_puts(s, "\t\t\t\t\t");
	tmp = (val & HDMI_IFRAME_CFG_DI_N(HDMI_IFRAME_MASK, 3)) >> 8;
	DBGFS_PRINT_STR("Data island 3:", en_di[tmp]);
	seq_puts(s, "\t\t\t\t\t");
	tmp = (val & HDMI_IFRAME_CFG_DI_N(HDMI_IFRAME_MASK, 4)) >> 12;
	DBGFS_PRINT_STR("Data island 4:", en_di[tmp]);
	seq_puts(s, "\t\t\t\t\t");
	tmp = (val & HDMI_IFRAME_CFG_DI_N(HDMI_IFRAME_MASK, 5)) >> 16;
	DBGFS_PRINT_STR("Data island 5:", en_di[tmp]);
	seq_puts(s, "\t\t\t\t\t");
	tmp = (val & HDMI_IFRAME_CFG_DI_N(HDMI_IFRAME_MASK, 6)) >> 20;
	DBGFS_PRINT_STR("Data island 6:", en_di[tmp]);
}

static int hdmi_dbg_show(struct seq_file *s, void *data)
{
	struct drm_info_node *node = s->private;
	struct sti_hdmi *hdmi = (struct sti_hdmi *)node->info_ent->data;

	seq_printf(s, "HDMI: (vaddr = 0x%p)", hdmi->regs);
	DBGFS_DUMP("\n", HDMI_CFG);
	hdmi_dbg_cfg(s, hdmi_read(hdmi, HDMI_CFG));
	DBGFS_DUMP("", HDMI_INT_EN);
	DBGFS_DUMP("\n", HDMI_STA);
	hdmi_dbg_sta(s, hdmi_read(hdmi, HDMI_STA));
	DBGFS_DUMP("", HDMI_ACTIVE_VID_XMIN);
	seq_putc(s, '\t');
	DBGFS_PRINT_INT("Xmin:", hdmi_read(hdmi, HDMI_ACTIVE_VID_XMIN));
	DBGFS_DUMP("", HDMI_ACTIVE_VID_XMAX);
	seq_putc(s, '\t');
	DBGFS_PRINT_INT("Xmax:", hdmi_read(hdmi, HDMI_ACTIVE_VID_XMAX));
	DBGFS_DUMP("", HDMI_ACTIVE_VID_YMIN);
	seq_putc(s, '\t');
	DBGFS_PRINT_INT("Ymin:", hdmi_read(hdmi, HDMI_ACTIVE_VID_YMIN));
	DBGFS_DUMP("", HDMI_ACTIVE_VID_YMAX);
	seq_putc(s, '\t');
	DBGFS_PRINT_INT("Ymax:", hdmi_read(hdmi, HDMI_ACTIVE_VID_YMAX));
	DBGFS_DUMP("", HDMI_SW_DI_CFG);
	hdmi_dbg_sw_di_cfg(s, hdmi_read(hdmi, HDMI_SW_DI_CFG));

	DBGFS_DUMP("\n", HDMI_AUDIO_CFG);
	DBGFS_DUMP("\n", HDMI_SPDIF_FIFO_STATUS);
	DBGFS_DUMP("\n", HDMI_AUDN);

	seq_printf(s, "\n AVI Infoframe (Data Island slot N=%d):",
		   HDMI_IFRAME_SLOT_AVI);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_HEAD_WORD, HDMI_IFRAME_SLOT_AVI);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD0, HDMI_IFRAME_SLOT_AVI);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD1, HDMI_IFRAME_SLOT_AVI);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD2, HDMI_IFRAME_SLOT_AVI);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD3, HDMI_IFRAME_SLOT_AVI);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD4, HDMI_IFRAME_SLOT_AVI);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD5, HDMI_IFRAME_SLOT_AVI);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD6, HDMI_IFRAME_SLOT_AVI);
	seq_printf(s, "\n\n AUDIO Infoframe (Data Island slot N=%d):",
		   HDMI_IFRAME_SLOT_AUDIO);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_HEAD_WORD, HDMI_IFRAME_SLOT_AUDIO);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD0, HDMI_IFRAME_SLOT_AUDIO);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD1, HDMI_IFRAME_SLOT_AUDIO);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD2, HDMI_IFRAME_SLOT_AUDIO);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD3, HDMI_IFRAME_SLOT_AUDIO);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD4, HDMI_IFRAME_SLOT_AUDIO);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD5, HDMI_IFRAME_SLOT_AUDIO);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD6, HDMI_IFRAME_SLOT_AUDIO);
	seq_printf(s, "\n\n VENDOR SPECIFIC Infoframe (Data Island slot N=%d):",
		   HDMI_IFRAME_SLOT_VENDOR);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_HEAD_WORD, HDMI_IFRAME_SLOT_VENDOR);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD0, HDMI_IFRAME_SLOT_VENDOR);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD1, HDMI_IFRAME_SLOT_VENDOR);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD2, HDMI_IFRAME_SLOT_VENDOR);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD3, HDMI_IFRAME_SLOT_VENDOR);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD4, HDMI_IFRAME_SLOT_VENDOR);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD5, HDMI_IFRAME_SLOT_VENDOR);
	DBGFS_DUMP_DI(HDMI_SW_DI_N_PKT_WORD6, HDMI_IFRAME_SLOT_VENDOR);
	seq_putc(s, '\n');
	return 0;
}

static struct drm_info_list hdmi_debugfs_files[] = {
	{ "hdmi", hdmi_dbg_show, 0, NULL },
};

static void hdmi_debugfs_init(struct sti_hdmi *hdmi, struct drm_minor *minor)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(hdmi_debugfs_files); i++)
		hdmi_debugfs_files[i].data = hdmi;

	drm_debugfs_create_files(hdmi_debugfs_files,
				 ARRAY_SIZE(hdmi_debugfs_files),
				 minor->debugfs_root, minor);
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

	/* Reset info frame transmission */
	hdmi_infoframe_reset(hdmi, HDMI_IFRAME_SLOT_AVI);
	hdmi_infoframe_reset(hdmi, HDMI_IFRAME_SLOT_AUDIO);
	hdmi_infoframe_reset(hdmi, HDMI_IFRAME_SLOT_VENDOR);

	/* Set the default channel data to be a dark red */
	hdmi_write(hdmi, 0x0000, HDMI_DFLT_CHL0_DAT);
	hdmi_write(hdmi, 0x0000, HDMI_DFLT_CHL1_DAT);
	hdmi_write(hdmi, 0x0060, HDMI_DFLT_CHL2_DAT);

	/* Disable/unprepare hdmi clock */
	clk_disable_unprepare(hdmi->clk_phy);
	clk_disable_unprepare(hdmi->clk_tmds);
	clk_disable_unprepare(hdmi->clk_pix);

	hdmi->enabled = false;

	cec_notifier_set_phys_addr(hdmi->notifier, CEC_PHYS_ADDR_INVALID);
}

/**
 * sti_hdmi_audio_get_non_coherent_n() - get N parameter for non-coherent
 * clocks. None-coherent clocks means that audio and TMDS clocks have not the
 * same source (drifts between clocks). In this case assumption is that CTS is
 * automatically calculated by hardware.
 *
 * @audio_fs: audio frame clock frequency in Hz
 *
 * Values computed are based on table described in HDMI specification 1.4b
 *
 * Returns n value.
 */
static int sti_hdmi_audio_get_non_coherent_n(unsigned int audio_fs)
{
	unsigned int n;

	switch (audio_fs) {
	case 32000:
		n = 4096;
		break;
	case 44100:
		n = 6272;
		break;
	case 48000:
		n = 6144;
		break;
	case 88200:
		n = 6272 * 2;
		break;
	case 96000:
		n = 6144 * 2;
		break;
	case 176400:
		n = 6272 * 4;
		break;
	case 192000:
		n = 6144 * 4;
		break;
	default:
		/* Not pre-defined, recommended value: 128 * fs / 1000 */
		n = (audio_fs * 128) / 1000;
	}

	return n;
}

static int hdmi_audio_configure(struct sti_hdmi *hdmi)
{
	int audio_cfg, n;
	struct hdmi_audio_params *params = &hdmi->audio;
	struct hdmi_audio_infoframe *info = &params->cea;

	DRM_DEBUG_DRIVER("\n");

	if (!hdmi->enabled)
		return 0;

	/* update N parameter */
	n = sti_hdmi_audio_get_non_coherent_n(params->sample_rate);

	DRM_DEBUG_DRIVER("Audio rate = %d Hz, TMDS clock = %d Hz, n = %d\n",
			 params->sample_rate, hdmi->mode.clock * 1000, n);
	hdmi_write(hdmi, n, HDMI_AUDN);

	/* update HDMI registers according to configuration */
	audio_cfg = HDMI_AUD_CFG_SPDIF_DIV_2 | HDMI_AUD_CFG_DTS_INVALID |
		    HDMI_AUD_CFG_ONE_BIT_INVALID;

	switch (info->channels) {
	case 8:
		audio_cfg |= HDMI_AUD_CFG_CH78_VALID;
		fallthrough;
	case 6:
		audio_cfg |= HDMI_AUD_CFG_CH56_VALID;
		fallthrough;
	case 4:
		audio_cfg |= HDMI_AUD_CFG_CH34_VALID | HDMI_AUD_CFG_8CH;
		fallthrough;
	case 2:
		audio_cfg |= HDMI_AUD_CFG_CH12_VALID;
		break;
	default:
		DRM_ERROR("ERROR: Unsupported number of channels (%d)!\n",
			  info->channels);
		return -EINVAL;
	}

	hdmi_write(hdmi, audio_cfg, HDMI_AUDIO_CFG);

	return hdmi_audio_infoframe_config(hdmi);
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

	if (hdmi->audio.enabled) {
		if (hdmi_audio_configure(hdmi))
			DRM_ERROR("Unable to configure audio\n");
	} else {
		hdmi_audio_infoframe_config(hdmi);
	}

	/* Program VS infoframe */
	if (hdmi_vendor_infoframe_config(hdmi))
		DRM_ERROR("Unable to configure VS infoframe\n");

	/* Sw reset */
	hdmi_swreset(hdmi);
}

static void sti_hdmi_set_mode(struct drm_bridge *bridge,
			      const struct drm_display_mode *mode,
			      const struct drm_display_mode *adjusted_mode)
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

	hdmi->hdmi_monitor = drm_detect_hdmi_monitor(edid);
	DRM_DEBUG_KMS("%s : %dx%d cm\n",
		      (hdmi->hdmi_monitor ? "hdmi monitor" : "dvi monitor"),
		      edid->width_cm, edid->height_cm);
	cec_notifier_set_phys_addr_from_edid(hdmi->notifier, edid);

	count = drm_add_edid_modes(connector, edid);
	drm_connector_update_edid_property(connector, edid);

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

static const
struct drm_connector_helper_funcs sti_hdmi_connector_helper_funcs = {
	.get_modes = sti_hdmi_connector_get_modes,
	.mode_valid = sti_hdmi_connector_mode_valid,
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
	cec_notifier_set_phys_addr(hdmi->notifier, CEC_PHYS_ADDR_INVALID);
	return connector_status_disconnected;
}

static void sti_hdmi_connector_init_property(struct drm_device *drm_dev,
					     struct drm_connector *connector)
{
	struct sti_hdmi_connector *hdmi_connector
		= to_sti_hdmi_connector(connector);
	struct sti_hdmi *hdmi = hdmi_connector->hdmi;
	struct drm_property *prop;

	/* colorspace property */
	hdmi->colorspace = DEFAULT_COLORSPACE_MODE;
	prop = drm_property_create_enum(drm_dev, 0, "colorspace",
					colorspace_mode_names,
					ARRAY_SIZE(colorspace_mode_names));
	if (!prop) {
		DRM_ERROR("fails to create colorspace property\n");
		return;
	}
	hdmi_connector->colorspace_property = prop;
	drm_object_attach_property(&connector->base, prop, hdmi->colorspace);
}

static int
sti_hdmi_connector_set_property(struct drm_connector *connector,
				struct drm_connector_state *state,
				struct drm_property *property,
				uint64_t val)
{
	struct sti_hdmi_connector *hdmi_connector
		= to_sti_hdmi_connector(connector);
	struct sti_hdmi *hdmi = hdmi_connector->hdmi;

	if (property == hdmi_connector->colorspace_property) {
		hdmi->colorspace = val;
		return 0;
	}

	DRM_ERROR("failed to set hdmi connector property\n");
	return -EINVAL;
}

static int
sti_hdmi_connector_get_property(struct drm_connector *connector,
				const struct drm_connector_state *state,
				struct drm_property *property,
				uint64_t *val)
{
	struct sti_hdmi_connector *hdmi_connector
		= to_sti_hdmi_connector(connector);
	struct sti_hdmi *hdmi = hdmi_connector->hdmi;

	if (property == hdmi_connector->colorspace_property) {
		*val = hdmi->colorspace;
		return 0;
	}

	DRM_ERROR("failed to get hdmi connector property\n");
	return -EINVAL;
}

static int sti_hdmi_late_register(struct drm_connector *connector)
{
	struct sti_hdmi_connector *hdmi_connector
		= to_sti_hdmi_connector(connector);
	struct sti_hdmi *hdmi = hdmi_connector->hdmi;

	hdmi_debugfs_init(hdmi, hdmi->drm_dev->primary);

	return 0;
}

static const struct drm_connector_funcs sti_hdmi_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = sti_hdmi_connector_detect,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_set_property = sti_hdmi_connector_set_property,
	.atomic_get_property = sti_hdmi_connector_get_property,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.late_register = sti_hdmi_late_register,
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

static void hdmi_audio_shutdown(struct device *dev, void *data)
{
	struct sti_hdmi *hdmi = dev_get_drvdata(dev);
	int audio_cfg;

	DRM_DEBUG_DRIVER("\n");

	/* disable audio */
	audio_cfg = HDMI_AUD_CFG_SPDIF_DIV_2 | HDMI_AUD_CFG_DTS_INVALID |
		    HDMI_AUD_CFG_ONE_BIT_INVALID;
	hdmi_write(hdmi, audio_cfg, HDMI_AUDIO_CFG);

	hdmi->audio.enabled = false;
	hdmi_audio_infoframe_config(hdmi);
}

static int hdmi_audio_hw_params(struct device *dev,
				void *data,
				struct hdmi_codec_daifmt *daifmt,
				struct hdmi_codec_params *params)
{
	struct sti_hdmi *hdmi = dev_get_drvdata(dev);
	int ret;

	DRM_DEBUG_DRIVER("\n");

	if ((daifmt->fmt != HDMI_I2S) || daifmt->bit_clk_inv ||
	    daifmt->frame_clk_inv || daifmt->bit_clk_master ||
	    daifmt->frame_clk_master) {
		dev_err(dev, "%s: Bad flags %d %d %d %d\n", __func__,
			daifmt->bit_clk_inv, daifmt->frame_clk_inv,
			daifmt->bit_clk_master,
			daifmt->frame_clk_master);
		return -EINVAL;
	}

	hdmi->audio.sample_width = params->sample_width;
	hdmi->audio.sample_rate = params->sample_rate;
	hdmi->audio.cea = params->cea;

	hdmi->audio.enabled = true;

	ret = hdmi_audio_configure(hdmi);
	if (ret < 0)
		return ret;

	return 0;
}

static int hdmi_audio_mute(struct device *dev, void *data,
			   bool enable, int direction)
{
	struct sti_hdmi *hdmi = dev_get_drvdata(dev);

	DRM_DEBUG_DRIVER("%s\n", enable ? "enable" : "disable");

	if (enable)
		hdmi_write(hdmi, HDMI_SAMPLE_FLAT_ALL, HDMI_SAMPLE_FLAT_MASK);
	else
		hdmi_write(hdmi, HDMI_SAMPLE_FLAT_NO, HDMI_SAMPLE_FLAT_MASK);

	return 0;
}

static int hdmi_audio_get_eld(struct device *dev, void *data, uint8_t *buf, size_t len)
{
	struct sti_hdmi *hdmi = dev_get_drvdata(dev);
	struct drm_connector *connector = hdmi->drm_connector;

	DRM_DEBUG_DRIVER("\n");
	memcpy(buf, connector->eld, min(sizeof(connector->eld), len));

	return 0;
}

static const struct hdmi_codec_ops audio_codec_ops = {
	.hw_params = hdmi_audio_hw_params,
	.audio_shutdown = hdmi_audio_shutdown,
	.mute_stream = hdmi_audio_mute,
	.get_eld = hdmi_audio_get_eld,
	.no_capture_mute = 1,
};

static int sti_hdmi_register_audio_driver(struct device *dev,
					  struct sti_hdmi *hdmi)
{
	struct hdmi_codec_pdata codec_data = {
		.ops = &audio_codec_ops,
		.max_i2s_channels = 8,
		.i2s = 1,
	};

	DRM_DEBUG_DRIVER("\n");

	hdmi->audio.enabled = false;

	hdmi->audio_pdev = platform_device_register_data(
		dev, HDMI_CODEC_DRV_NAME, PLATFORM_DEVID_AUTO,
		&codec_data, sizeof(codec_data));

	if (IS_ERR(hdmi->audio_pdev))
		return PTR_ERR(hdmi->audio_pdev);

	DRM_INFO("%s Driver bound %s\n", HDMI_CODEC_DRV_NAME, dev_name(dev));

	return 0;
}

static int sti_hdmi_bind(struct device *dev, struct device *master, void *data)
{
	struct sti_hdmi *hdmi = dev_get_drvdata(dev);
	struct drm_device *drm_dev = data;
	struct drm_encoder *encoder;
	struct sti_hdmi_connector *connector;
	struct cec_connector_info conn_info;
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
	drm_bridge_attach(encoder, bridge, NULL, 0);

	connector->encoder = encoder;

	drm_connector = (struct drm_connector *)connector;

	drm_connector->polled = DRM_CONNECTOR_POLL_HPD;

	drm_connector_init_with_ddc(drm_dev, drm_connector,
				    &sti_hdmi_connector_funcs,
				    DRM_MODE_CONNECTOR_HDMIA,
				    hdmi->ddc_adapt);
	drm_connector_helper_add(drm_connector,
			&sti_hdmi_connector_helper_funcs);

	/* initialise property */
	sti_hdmi_connector_init_property(drm_dev, drm_connector);

	hdmi->drm_connector = drm_connector;

	err = drm_connector_attach_encoder(drm_connector, encoder);
	if (err) {
		DRM_ERROR("Failed to attach a connector to a encoder\n");
		goto err_sysfs;
	}

	err = sti_hdmi_register_audio_driver(dev, hdmi);
	if (err) {
		DRM_ERROR("Failed to attach an audio codec\n");
		goto err_sysfs;
	}

	/* Initialize audio infoframe */
	err = hdmi_audio_infoframe_init(&hdmi->audio.cea);
	if (err) {
		DRM_ERROR("Failed to init audio infoframe\n");
		goto err_sysfs;
	}

	cec_fill_conn_info_from_drm(&conn_info, drm_connector);
	hdmi->notifier = cec_notifier_conn_register(&hdmi->dev, NULL,
						    &conn_info);
	if (!hdmi->notifier) {
		hdmi->drm_connector = NULL;
		return -ENOMEM;
	}

	/* Enable default interrupts */
	hdmi_write(hdmi, HDMI_DEFAULT_INT, HDMI_INT_EN);

	return 0;

err_sysfs:
	hdmi->drm_connector = NULL;
	return -EINVAL;
}

static void sti_hdmi_unbind(struct device *dev,
		struct device *master, void *data)
{
	struct sti_hdmi *hdmi = dev_get_drvdata(dev);

	cec_notifier_conn_unregister(hdmi->notifier);
}

static const struct component_ops sti_hdmi_ops = {
	.bind = sti_hdmi_bind,
	.unbind = sti_hdmi_unbind,
};

static const struct of_device_id hdmi_of_match[] = {
	{
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
	hdmi->regs = devm_ioremap(dev, res->start, resource_size(res));
	if (!hdmi->regs) {
		ret = -ENOMEM;
		goto release_adapter;
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
	if (hdmi->irq < 0) {
		DRM_ERROR("Cannot get HDMI irq\n");
		ret = hdmi->irq;
		goto release_adapter;
	}

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
	if (hdmi->audio_pdev)
		platform_device_unregister(hdmi->audio_pdev);
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
