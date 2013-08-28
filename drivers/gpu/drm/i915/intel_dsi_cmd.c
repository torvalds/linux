/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author: Jani Nikula <jani.nikula@intel.com>
 */

#include <linux/export.h>
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <video/mipi_display.h>
#include "i915_drv.h"
#include "intel_drv.h"
#include "intel_dsi.h"
#include "intel_dsi_cmd.h"

/*
 * XXX: MIPI_DATA_ADDRESS, MIPI_DATA_LENGTH, MIPI_COMMAND_LENGTH, and
 * MIPI_COMMAND_ADDRESS registers.
 *
 * Apparently these registers provide a MIPI adapter level way to send (lots of)
 * commands and data to the receiver, without having to write the commands and
 * data to MIPI_{HS,LP}_GEN_{CTRL,DATA} registers word by word.
 *
 * Presumably for anything other than MIPI_DCS_WRITE_MEMORY_START and
 * MIPI_DCS_WRITE_MEMORY_CONTINUE (which are used to update the external
 * framebuffer in command mode displays) these are just an optimization that can
 * come later.
 *
 * For memory writes, these should probably be used for performance.
 */

static void print_stat(struct intel_dsi *intel_dsi)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	enum pipe pipe = intel_crtc->pipe;
	u32 val;

	val = I915_READ(MIPI_INTR_STAT(pipe));

#define STAT_BIT(val, bit) (val) & (bit) ? " " #bit : ""
	DRM_DEBUG_KMS("MIPI_INTR_STAT(%d) = %08x"
		      "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s"
		      "\n", pipe, val,
		      STAT_BIT(val, TEARING_EFFECT),
		      STAT_BIT(val, SPL_PKT_SENT_INTERRUPT),
		      STAT_BIT(val, GEN_READ_DATA_AVAIL),
		      STAT_BIT(val, LP_GENERIC_WR_FIFO_FULL),
		      STAT_BIT(val, HS_GENERIC_WR_FIFO_FULL),
		      STAT_BIT(val, RX_PROT_VIOLATION),
		      STAT_BIT(val, RX_INVALID_TX_LENGTH),
		      STAT_BIT(val, ACK_WITH_NO_ERROR),
		      STAT_BIT(val, TURN_AROUND_ACK_TIMEOUT),
		      STAT_BIT(val, LP_RX_TIMEOUT),
		      STAT_BIT(val, HS_TX_TIMEOUT),
		      STAT_BIT(val, DPI_FIFO_UNDERRUN),
		      STAT_BIT(val, LOW_CONTENTION),
		      STAT_BIT(val, HIGH_CONTENTION),
		      STAT_BIT(val, TXDSI_VC_ID_INVALID),
		      STAT_BIT(val, TXDSI_DATA_TYPE_NOT_RECOGNISED),
		      STAT_BIT(val, TXCHECKSUM_ERROR),
		      STAT_BIT(val, TXECC_MULTIBIT_ERROR),
		      STAT_BIT(val, TXECC_SINGLE_BIT_ERROR),
		      STAT_BIT(val, TXFALSE_CONTROL_ERROR),
		      STAT_BIT(val, RXDSI_VC_ID_INVALID),
		      STAT_BIT(val, RXDSI_DATA_TYPE_NOT_REGOGNISED),
		      STAT_BIT(val, RXCHECKSUM_ERROR),
		      STAT_BIT(val, RXECC_MULTIBIT_ERROR),
		      STAT_BIT(val, RXECC_SINGLE_BIT_ERROR),
		      STAT_BIT(val, RXFALSE_CONTROL_ERROR),
		      STAT_BIT(val, RXHS_RECEIVE_TIMEOUT_ERROR),
		      STAT_BIT(val, RX_LP_TX_SYNC_ERROR),
		      STAT_BIT(val, RXEXCAPE_MODE_ENTRY_ERROR),
		      STAT_BIT(val, RXEOT_SYNC_ERROR),
		      STAT_BIT(val, RXSOT_SYNC_ERROR),
		      STAT_BIT(val, RXSOT_ERROR));
#undef STAT_BIT
}

enum dsi_type {
	DSI_DCS,
	DSI_GENERIC,
};

/* enable or disable command mode hs transmissions */
void dsi_hs_mode_enable(struct intel_dsi *intel_dsi, bool enable)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	enum pipe pipe = intel_crtc->pipe;
	u32 temp;
	u32 mask = DBI_FIFO_EMPTY;

	if (wait_for((I915_READ(MIPI_GEN_FIFO_STAT(pipe)) & mask) == mask, 50))
		DRM_ERROR("Timeout waiting for DBI FIFO empty\n");

	temp = I915_READ(MIPI_HS_LP_DBI_ENABLE(pipe));
	temp &= DBI_HS_LP_MODE_MASK;
	I915_WRITE(MIPI_HS_LP_DBI_ENABLE(pipe), enable ? DBI_HS_MODE : DBI_LP_MODE);

	intel_dsi->hs = enable;
}

static int dsi_vc_send_short(struct intel_dsi *intel_dsi, int channel,
			     u8 data_type, u16 data)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	enum pipe pipe = intel_crtc->pipe;
	u32 ctrl_reg;
	u32 ctrl;
	u32 mask;

	DRM_DEBUG_KMS("channel %d, data_type %d, data %04x\n",
		      channel, data_type, data);

	if (intel_dsi->hs) {
		ctrl_reg = MIPI_HS_GEN_CTRL(pipe);
		mask = HS_CTRL_FIFO_FULL;
	} else {
		ctrl_reg = MIPI_LP_GEN_CTRL(pipe);
		mask = LP_CTRL_FIFO_FULL;
	}

	if (wait_for((I915_READ(MIPI_GEN_FIFO_STAT(pipe)) & mask) == 0, 50)) {
		DRM_ERROR("Timeout waiting for HS/LP CTRL FIFO !full\n");
		print_stat(intel_dsi);
	}

	/*
	 * Note: This function is also used for long packets, with length passed
	 * as data, since SHORT_PACKET_PARAM_SHIFT ==
	 * LONG_PACKET_WORD_COUNT_SHIFT.
	 */
	ctrl = data << SHORT_PACKET_PARAM_SHIFT |
		channel << VIRTUAL_CHANNEL_SHIFT |
		data_type << DATA_TYPE_SHIFT;

	I915_WRITE(ctrl_reg, ctrl);

	return 0;
}

static int dsi_vc_send_long(struct intel_dsi *intel_dsi, int channel,
			    u8 data_type, const u8 *data, int len)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	enum pipe pipe = intel_crtc->pipe;
	u32 data_reg;
	int i, j, n;
	u32 mask;

	DRM_DEBUG_KMS("channel %d, data_type %d, len %04x\n",
		      channel, data_type, len);

	if (intel_dsi->hs) {
		data_reg = MIPI_HS_GEN_DATA(pipe);
		mask = HS_DATA_FIFO_FULL;
	} else {
		data_reg = MIPI_LP_GEN_DATA(pipe);
		mask = LP_DATA_FIFO_FULL;
	}

	if (wait_for((I915_READ(MIPI_GEN_FIFO_STAT(pipe)) & mask) == 0, 50))
		DRM_ERROR("Timeout waiting for HS/LP DATA FIFO !full\n");

	for (i = 0; i < len; i += n) {
		u32 val = 0;
		n = min_t(int, len - i, 4);

		for (j = 0; j < n; j++)
			val |= *data++ << 8 * j;

		I915_WRITE(data_reg, val);
		/* XXX: check for data fifo full, once that is set, write 4
		 * dwords, then wait for not set, then continue. */
	}

	return dsi_vc_send_short(intel_dsi, channel, data_type, len);
}

static int dsi_vc_write_common(struct intel_dsi *intel_dsi,
			       int channel, const u8 *data, int len,
			       enum dsi_type type)
{
	int ret;

	if (len == 0) {
		BUG_ON(type == DSI_GENERIC);
		ret = dsi_vc_send_short(intel_dsi, channel,
					MIPI_DSI_GENERIC_SHORT_WRITE_0_PARAM,
					0);
	} else if (len == 1) {
		ret = dsi_vc_send_short(intel_dsi, channel,
					type == DSI_GENERIC ?
					MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM :
					MIPI_DSI_DCS_SHORT_WRITE, data[0]);
	} else if (len == 2) {
		ret = dsi_vc_send_short(intel_dsi, channel,
					type == DSI_GENERIC ?
					MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM :
					MIPI_DSI_DCS_SHORT_WRITE_PARAM,
					(data[1] << 8) | data[0]);
	} else {
		ret = dsi_vc_send_long(intel_dsi, channel,
				       type == DSI_GENERIC ?
				       MIPI_DSI_GENERIC_LONG_WRITE :
				       MIPI_DSI_DCS_LONG_WRITE, data, len);
	}

	return ret;
}

int dsi_vc_dcs_write(struct intel_dsi *intel_dsi, int channel,
		     const u8 *data, int len)
{
	return dsi_vc_write_common(intel_dsi, channel, data, len, DSI_DCS);
}

int dsi_vc_generic_write(struct intel_dsi *intel_dsi, int channel,
			 const u8 *data, int len)
{
	return dsi_vc_write_common(intel_dsi, channel, data, len, DSI_GENERIC);
}

static int dsi_vc_dcs_send_read_request(struct intel_dsi *intel_dsi,
					int channel, u8 dcs_cmd)
{
	return dsi_vc_send_short(intel_dsi, channel, MIPI_DSI_DCS_READ,
				 dcs_cmd);
}

static int dsi_vc_generic_send_read_request(struct intel_dsi *intel_dsi,
					    int channel, u8 *reqdata,
					    int reqlen)
{
	u16 data;
	u8 data_type;

	switch (reqlen) {
	case 0:
		data_type = MIPI_DSI_GENERIC_READ_REQUEST_0_PARAM;
		data = 0;
		break;
	case 1:
		data_type = MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM;
		data = reqdata[0];
		break;
	case 2:
		data_type = MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM;
		data = (reqdata[1] << 8) | reqdata[0];
		break;
	default:
		BUG();
	}

	return dsi_vc_send_short(intel_dsi, channel, data_type, data);
}

static int dsi_read_data_return(struct intel_dsi *intel_dsi,
				u8 *buf, int buflen)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	enum pipe pipe = intel_crtc->pipe;
	int i, len = 0;
	u32 data_reg, val;

	if (intel_dsi->hs) {
		data_reg = MIPI_HS_GEN_DATA(pipe);
	} else {
		data_reg = MIPI_LP_GEN_DATA(pipe);
	}

	while (len < buflen) {
		val = I915_READ(data_reg);
		for (i = 0; i < 4 && len < buflen; i++, len++)
			buf[len] = val >> 8 * i;
	}

	return len;
}

int dsi_vc_dcs_read(struct intel_dsi *intel_dsi, int channel, u8 dcs_cmd,
		    u8 *buf, int buflen)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	enum pipe pipe = intel_crtc->pipe;
	u32 mask;
	int ret;

	/*
	 * XXX: should issue multiple read requests and reads if request is
	 * longer than MIPI_MAX_RETURN_PKT_SIZE
	 */

	I915_WRITE(MIPI_INTR_STAT(pipe), GEN_READ_DATA_AVAIL);

	ret = dsi_vc_dcs_send_read_request(intel_dsi, channel, dcs_cmd);
	if (ret)
		return ret;

	mask = GEN_READ_DATA_AVAIL;
	if (wait_for((I915_READ(MIPI_INTR_STAT(pipe)) & mask) == mask, 50))
		DRM_ERROR("Timeout waiting for read data.\n");

	ret = dsi_read_data_return(intel_dsi, buf, buflen);
	if (ret < 0)
		return ret;

	if (ret != buflen)
		return -EIO;

	return 0;
}

int dsi_vc_generic_read(struct intel_dsi *intel_dsi, int channel,
			u8 *reqdata, int reqlen, u8 *buf, int buflen)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	enum pipe pipe = intel_crtc->pipe;
	u32 mask;
	int ret;

	/*
	 * XXX: should issue multiple read requests and reads if request is
	 * longer than MIPI_MAX_RETURN_PKT_SIZE
	 */

	I915_WRITE(MIPI_INTR_STAT(pipe), GEN_READ_DATA_AVAIL);

	ret = dsi_vc_generic_send_read_request(intel_dsi, channel, reqdata,
					       reqlen);
	if (ret)
		return ret;

	mask = GEN_READ_DATA_AVAIL;
	if (wait_for((I915_READ(MIPI_INTR_STAT(pipe)) & mask) == mask, 50))
		DRM_ERROR("Timeout waiting for read data.\n");

	ret = dsi_read_data_return(intel_dsi, buf, buflen);
	if (ret < 0)
		return ret;

	if (ret != buflen)
		return -EIO;

	return 0;
}

/*
 * send a video mode command
 *
 * XXX: commands with data in MIPI_DPI_DATA?
 */
int dpi_send_cmd(struct intel_dsi *intel_dsi, u32 cmd)
{
	struct drm_encoder *encoder = &intel_dsi->base.base;
	struct drm_device *dev = encoder->dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct intel_crtc *intel_crtc = to_intel_crtc(encoder->crtc);
	enum pipe pipe = intel_crtc->pipe;
	u32 mask;

	/* XXX: pipe, hs */
	if (intel_dsi->hs)
		cmd &= ~DPI_LP_MODE;
	else
		cmd |= DPI_LP_MODE;

	/* DPI virtual channel?! */

	mask = DPI_FIFO_EMPTY;
	if (wait_for((I915_READ(MIPI_GEN_FIFO_STAT(pipe)) & mask) == mask, 50))
		DRM_ERROR("Timeout waiting for DPI FIFO empty.\n");

	/* clear bit */
	I915_WRITE(MIPI_INTR_STAT(pipe), SPL_PKT_SENT_INTERRUPT);

	/* XXX: old code skips write if control unchanged */
	if (cmd == I915_READ(MIPI_DPI_CONTROL(pipe)))
		DRM_ERROR("Same special packet %02x twice in a row.\n", cmd);

	I915_WRITE(MIPI_DPI_CONTROL(pipe), cmd);

	mask = SPL_PKT_SENT_INTERRUPT;
	if (wait_for((I915_READ(MIPI_INTR_STAT(pipe)) & mask) == mask, 100))
		DRM_ERROR("Video mode command 0x%08x send failed.\n", cmd);

	return 0;
}
