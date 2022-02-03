/*
 * Copyright © 2009 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/seq_file.h>

#include <drm/drm_dp_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>
#include <drm/drm_dp_mst_helper.h>
#include <drm/drm_panel.h>

#include "drm_crtc_helper_internal.h"

struct dp_aux_backlight {
	struct backlight_device *base;
	struct drm_dp_aux *aux;
	struct drm_edp_backlight_info info;
	bool enabled;
};

/**
 * DOC: dp helpers
 *
 * These functions contain some common logic and helpers at various abstraction
 * levels to deal with Display Port sink devices and related things like DP aux
 * channel transfers, EDID reading over DP aux channels, decoding certain DPCD
 * blocks, ...
 */

/* Helpers for DP link training */
static u8 dp_link_status(const u8 link_status[DP_LINK_STATUS_SIZE], int r)
{
	return link_status[r - DP_LANE0_1_STATUS];
}

static u8 dp_get_lane_status(const u8 link_status[DP_LINK_STATUS_SIZE],
			     int lane)
{
	int i = DP_LANE0_1_STATUS + (lane >> 1);
	int s = (lane & 1) * 4;
	u8 l = dp_link_status(link_status, i);

	return (l >> s) & 0xf;
}

bool drm_dp_channel_eq_ok(const u8 link_status[DP_LINK_STATUS_SIZE],
			  int lane_count)
{
	u8 lane_align;
	u8 lane_status;
	int lane;

	lane_align = dp_link_status(link_status,
				    DP_LANE_ALIGN_STATUS_UPDATED);
	if ((lane_align & DP_INTERLANE_ALIGN_DONE) == 0)
		return false;
	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_CHANNEL_EQ_BITS) != DP_CHANNEL_EQ_BITS)
			return false;
	}
	return true;
}
EXPORT_SYMBOL(drm_dp_channel_eq_ok);

bool drm_dp_clock_recovery_ok(const u8 link_status[DP_LINK_STATUS_SIZE],
			      int lane_count)
{
	int lane;
	u8 lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_LANE_CR_DONE) == 0)
			return false;
	}
	return true;
}
EXPORT_SYMBOL(drm_dp_clock_recovery_ok);

u8 drm_dp_get_adjust_request_voltage(const u8 link_status[DP_LINK_STATUS_SIZE],
				     int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT :
		 DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_VOLTAGE_SWING_SHIFT;
}
EXPORT_SYMBOL(drm_dp_get_adjust_request_voltage);

u8 drm_dp_get_adjust_request_pre_emphasis(const u8 link_status[DP_LINK_STATUS_SIZE],
					  int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT :
		 DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_PRE_EMPHASIS_SHIFT;
}
EXPORT_SYMBOL(drm_dp_get_adjust_request_pre_emphasis);

/* DP 2.0 128b/132b */
u8 drm_dp_get_adjust_tx_ffe_preset(const u8 link_status[DP_LINK_STATUS_SIZE],
				   int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_TX_FFE_PRESET_LANE1_SHIFT :
		 DP_ADJUST_TX_FFE_PRESET_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return (l >> s) & 0xf;
}
EXPORT_SYMBOL(drm_dp_get_adjust_tx_ffe_preset);

u8 drm_dp_get_adjust_request_post_cursor(const u8 link_status[DP_LINK_STATUS_SIZE],
					 unsigned int lane)
{
	unsigned int offset = DP_ADJUST_REQUEST_POST_CURSOR2;
	u8 value = dp_link_status(link_status, offset);

	return (value >> (lane << 1)) & 0x3;
}
EXPORT_SYMBOL(drm_dp_get_adjust_request_post_cursor);

static int __8b10b_clock_recovery_delay_us(const struct drm_dp_aux *aux, u8 rd_interval)
{
	if (rd_interval > 4)
		drm_dbg_kms(aux->drm_dev, "%s: invalid AUX interval 0x%02x (max 4)\n",
			    aux->name, rd_interval);

	if (rd_interval == 0)
		return 100;

	return rd_interval * 4 * USEC_PER_MSEC;
}

static int __8b10b_channel_eq_delay_us(const struct drm_dp_aux *aux, u8 rd_interval)
{
	if (rd_interval > 4)
		drm_dbg_kms(aux->drm_dev, "%s: invalid AUX interval 0x%02x (max 4)\n",
			    aux->name, rd_interval);

	if (rd_interval == 0)
		return 400;

	return rd_interval * 4 * USEC_PER_MSEC;
}

static int __128b132b_channel_eq_delay_us(const struct drm_dp_aux *aux, u8 rd_interval)
{
	switch (rd_interval) {
	default:
		drm_dbg_kms(aux->drm_dev, "%s: invalid AUX interval 0x%02x\n",
			    aux->name, rd_interval);
		fallthrough;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_400_US:
		return 400;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_4_MS:
		return 4000;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_8_MS:
		return 8000;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_12_MS:
		return 12000;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_16_MS:
		return 16000;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_32_MS:
		return 32000;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_64_MS:
		return 64000;
	}
}

/*
 * The link training delays are different for:
 *
 *  - Clock recovery vs. channel equalization
 *  - DPRX vs. LTTPR
 *  - 128b/132b vs. 8b/10b
 *  - DPCD rev 1.3 vs. later
 *
 * Get the correct delay in us, reading DPCD if necessary.
 */
static int __read_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			enum drm_dp_phy dp_phy, bool uhbr, bool cr)
{
	int (*parse)(const struct drm_dp_aux *aux, u8 rd_interval);
	unsigned int offset;
	u8 rd_interval, mask;

	if (dp_phy == DP_PHY_DPRX) {
		if (uhbr) {
			if (cr)
				return 100;

			offset = DP_128B132B_TRAINING_AUX_RD_INTERVAL;
			mask = DP_128B132B_TRAINING_AUX_RD_INTERVAL_MASK;
			parse = __128b132b_channel_eq_delay_us;
		} else {
			if (cr && dpcd[DP_DPCD_REV] >= DP_DPCD_REV_14)
				return 100;

			offset = DP_TRAINING_AUX_RD_INTERVAL;
			mask = DP_TRAINING_AUX_RD_MASK;
			if (cr)
				parse = __8b10b_clock_recovery_delay_us;
			else
				parse = __8b10b_channel_eq_delay_us;
		}
	} else {
		if (uhbr) {
			offset = DP_128B132B_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER(dp_phy);
			mask = DP_128B132B_TRAINING_AUX_RD_INTERVAL_MASK;
			parse = __128b132b_channel_eq_delay_us;
		} else {
			if (cr)
				return 100;

			offset = DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER(dp_phy);
			mask = DP_TRAINING_AUX_RD_MASK;
			parse = __8b10b_channel_eq_delay_us;
		}
	}

	if (offset < DP_RECEIVER_CAP_SIZE) {
		rd_interval = dpcd[offset];
	} else {
		if (drm_dp_dpcd_readb(aux, offset, &rd_interval) != 1) {
			drm_dbg_kms(aux->drm_dev, "%s: failed rd interval read\n",
				    aux->name);
			/* arbitrary default delay */
			return 400;
		}
	}

	return parse(aux, rd_interval & mask);
}

int drm_dp_read_clock_recovery_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				     enum drm_dp_phy dp_phy, bool uhbr)
{
	return __read_delay(aux, dpcd, dp_phy, uhbr, true);
}
EXPORT_SYMBOL(drm_dp_read_clock_recovery_delay);

int drm_dp_read_channel_eq_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				 enum drm_dp_phy dp_phy, bool uhbr)
{
	return __read_delay(aux, dpcd, dp_phy, uhbr, false);
}
EXPORT_SYMBOL(drm_dp_read_channel_eq_delay);

/* Per DP 2.0 Errata */
int drm_dp_128b132b_read_aux_rd_interval(struct drm_dp_aux *aux)
{
	int unit;
	u8 val;

	if (drm_dp_dpcd_readb(aux, DP_128B132B_TRAINING_AUX_RD_INTERVAL, &val) != 1) {
		drm_err(aux->drm_dev, "%s: failed rd interval read\n",
			aux->name);
		/* default to max */
		val = DP_128B132B_TRAINING_AUX_RD_INTERVAL_MASK;
	}

	unit = (val & DP_128B132B_TRAINING_AUX_RD_INTERVAL_1MS_UNIT) ? 1 : 2;
	val &= DP_128B132B_TRAINING_AUX_RD_INTERVAL_MASK;

	return (val + 1) * unit * 1000;
}
EXPORT_SYMBOL(drm_dp_128b132b_read_aux_rd_interval);

void drm_dp_link_train_clock_recovery_delay(const struct drm_dp_aux *aux,
					    const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	u8 rd_interval = dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
		DP_TRAINING_AUX_RD_MASK;
	int delay_us;

	if (dpcd[DP_DPCD_REV] >= DP_DPCD_REV_14)
		delay_us = 100;
	else
		delay_us = __8b10b_clock_recovery_delay_us(aux, rd_interval);

	usleep_range(delay_us, delay_us * 2);
}
EXPORT_SYMBOL(drm_dp_link_train_clock_recovery_delay);

static void __drm_dp_link_train_channel_eq_delay(const struct drm_dp_aux *aux,
						 u8 rd_interval)
{
	int delay_us = __8b10b_channel_eq_delay_us(aux, rd_interval);

	usleep_range(delay_us, delay_us * 2);
}

void drm_dp_link_train_channel_eq_delay(const struct drm_dp_aux *aux,
					const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	__drm_dp_link_train_channel_eq_delay(aux,
					     dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
					     DP_TRAINING_AUX_RD_MASK);
}
EXPORT_SYMBOL(drm_dp_link_train_channel_eq_delay);

void drm_dp_lttpr_link_train_clock_recovery_delay(void)
{
	usleep_range(100, 200);
}
EXPORT_SYMBOL(drm_dp_lttpr_link_train_clock_recovery_delay);

static u8 dp_lttpr_phy_cap(const u8 phy_cap[DP_LTTPR_PHY_CAP_SIZE], int r)
{
	return phy_cap[r - DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1];
}

void drm_dp_lttpr_link_train_channel_eq_delay(const struct drm_dp_aux *aux,
					      const u8 phy_cap[DP_LTTPR_PHY_CAP_SIZE])
{
	u8 interval = dp_lttpr_phy_cap(phy_cap,
				       DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1) &
		      DP_TRAINING_AUX_RD_MASK;

	__drm_dp_link_train_channel_eq_delay(aux, interval);
}
EXPORT_SYMBOL(drm_dp_lttpr_link_train_channel_eq_delay);

u8 drm_dp_link_rate_to_bw_code(int link_rate)
{
	switch (link_rate) {
	case 1000000:
		return DP_LINK_BW_10;
	case 1350000:
		return DP_LINK_BW_13_5;
	case 2000000:
		return DP_LINK_BW_20;
	default:
		/* Spec says link_bw = link_rate / 0.27Gbps */
		return link_rate / 27000;
	}
}
EXPORT_SYMBOL(drm_dp_link_rate_to_bw_code);

int drm_dp_bw_code_to_link_rate(u8 link_bw)
{
	switch (link_bw) {
	case DP_LINK_BW_10:
		return 1000000;
	case DP_LINK_BW_13_5:
		return 1350000;
	case DP_LINK_BW_20:
		return 2000000;
	default:
		/* Spec says link_rate = link_bw * 0.27Gbps */
		return link_bw * 27000;
	}
}
EXPORT_SYMBOL(drm_dp_bw_code_to_link_rate);

#define AUX_RETRY_INTERVAL 500 /* us */

static inline void
drm_dp_dump_access(const struct drm_dp_aux *aux,
		   u8 request, uint offset, void *buffer, int ret)
{
	const char *arrow = request == DP_AUX_NATIVE_READ ? "->" : "<-";

	if (ret > 0)
		drm_dbg_dp(aux->drm_dev, "%s: 0x%05x AUX %s (ret=%3d) %*ph\n",
			   aux->name, offset, arrow, ret, min(ret, 20), buffer);
	else
		drm_dbg_dp(aux->drm_dev, "%s: 0x%05x AUX %s (ret=%3d)\n",
			   aux->name, offset, arrow, ret);
}

/**
 * DOC: dp helpers
 *
 * The DisplayPort AUX channel is an abstraction to allow generic, driver-
 * independent access to AUX functionality. Drivers can take advantage of
 * this by filling in the fields of the drm_dp_aux structure.
 *
 * Transactions are described using a hardware-independent drm_dp_aux_msg
 * structure, which is passed into a driver's .transfer() implementation.
 * Both native and I2C-over-AUX transactions are supported.
 */

static int drm_dp_dpcd_access(struct drm_dp_aux *aux, u8 request,
			      unsigned int offset, void *buffer, size_t size)
{
	struct drm_dp_aux_msg msg;
	unsigned int retry, native_reply;
	int err = 0, ret = 0;

	memset(&msg, 0, sizeof(msg));
	msg.address = offset;
	msg.request = request;
	msg.buffer = buffer;
	msg.size = size;

	mutex_lock(&aux->hw_mutex);

	/*
	 * The specification doesn't give any recommendation on how often to
	 * retry native transactions. We used to retry 7 times like for
	 * aux i2c transactions but real world devices this wasn't
	 * sufficient, bump to 32 which makes Dell 4k monitors happier.
	 */
	for (retry = 0; retry < 32; retry++) {
		if (ret != 0 && ret != -ETIMEDOUT) {
			usleep_range(AUX_RETRY_INTERVAL,
				     AUX_RETRY_INTERVAL + 100);
		}

		ret = aux->transfer(aux, &msg);
		if (ret >= 0) {
			native_reply = msg.reply & DP_AUX_NATIVE_REPLY_MASK;
			if (native_reply == DP_AUX_NATIVE_REPLY_ACK) {
				if (ret == size)
					goto unlock;

				ret = -EPROTO;
			} else
				ret = -EIO;
		}

		/*
		 * We want the error we return to be the error we received on
		 * the first transaction, since we may get a different error the
		 * next time we retry
		 */
		if (!err)
			err = ret;
	}

	drm_dbg_kms(aux->drm_dev, "%s: Too many retries, giving up. First error: %d\n",
		    aux->name, err);
	ret = err;

unlock:
	mutex_unlock(&aux->hw_mutex);
	return ret;
}

/**
 * drm_dp_dpcd_read() - read a series of bytes from the DPCD
 * @aux: DisplayPort AUX channel (SST or MST)
 * @offset: address of the (first) register to read
 * @buffer: buffer to store the register values
 * @size: number of bytes in @buffer
 *
 * Returns the number of bytes transferred on success, or a negative error
 * code on failure. -EIO is returned if the request was NAKed by the sink or
 * if the retry count was exceeded. If not all bytes were transferred, this
 * function returns -EPROTO. Errors from the underlying AUX channel transfer
 * function, with the exception of -EBUSY (which causes the transaction to
 * be retried), are propagated to the caller.
 */
ssize_t drm_dp_dpcd_read(struct drm_dp_aux *aux, unsigned int offset,
			 void *buffer, size_t size)
{
	int ret;

	/*
	 * HP ZR24w corrupts the first DPCD access after entering power save
	 * mode. Eg. on a read, the entire buffer will be filled with the same
	 * byte. Do a throw away read to avoid corrupting anything we care
	 * about. Afterwards things will work correctly until the monitor
	 * gets woken up and subsequently re-enters power save mode.
	 *
	 * The user pressing any button on the monitor is enough to wake it
	 * up, so there is no particularly good place to do the workaround.
	 * We just have to do it before any DPCD access and hope that the
	 * monitor doesn't power down exactly after the throw away read.
	 */
	if (!aux->is_remote) {
		ret = drm_dp_dpcd_access(aux, DP_AUX_NATIVE_READ, DP_DPCD_REV,
					 buffer, 1);
		if (ret != 1)
			goto out;
	}

	if (aux->is_remote)
		ret = drm_dp_mst_dpcd_read(aux, offset, buffer, size);
	else
		ret = drm_dp_dpcd_access(aux, DP_AUX_NATIVE_READ, offset,
					 buffer, size);

out:
	drm_dp_dump_access(aux, DP_AUX_NATIVE_READ, offset, buffer, ret);
	return ret;
}
EXPORT_SYMBOL(drm_dp_dpcd_read);

/**
 * drm_dp_dpcd_write() - write a series of bytes to the DPCD
 * @aux: DisplayPort AUX channel (SST or MST)
 * @offset: address of the (first) register to write
 * @buffer: buffer containing the values to write
 * @size: number of bytes in @buffer
 *
 * Returns the number of bytes transferred on success, or a negative error
 * code on failure. -EIO is returned if the request was NAKed by the sink or
 * if the retry count was exceeded. If not all bytes were transferred, this
 * function returns -EPROTO. Errors from the underlying AUX channel transfer
 * function, with the exception of -EBUSY (which causes the transaction to
 * be retried), are propagated to the caller.
 */
ssize_t drm_dp_dpcd_write(struct drm_dp_aux *aux, unsigned int offset,
			  void *buffer, size_t size)
{
	int ret;

	if (aux->is_remote)
		ret = drm_dp_mst_dpcd_write(aux, offset, buffer, size);
	else
		ret = drm_dp_dpcd_access(aux, DP_AUX_NATIVE_WRITE, offset,
					 buffer, size);

	drm_dp_dump_access(aux, DP_AUX_NATIVE_WRITE, offset, buffer, ret);
	return ret;
}
EXPORT_SYMBOL(drm_dp_dpcd_write);

/**
 * drm_dp_dpcd_read_link_status() - read DPCD link status (bytes 0x202-0x207)
 * @aux: DisplayPort AUX channel
 * @status: buffer to store the link status in (must be at least 6 bytes)
 *
 * Returns the number of bytes transferred on success or a negative error
 * code on failure.
 */
int drm_dp_dpcd_read_link_status(struct drm_dp_aux *aux,
				 u8 status[DP_LINK_STATUS_SIZE])
{
	return drm_dp_dpcd_read(aux, DP_LANE0_1_STATUS, status,
				DP_LINK_STATUS_SIZE);
}
EXPORT_SYMBOL(drm_dp_dpcd_read_link_status);

/**
 * drm_dp_dpcd_read_phy_link_status - get the link status information for a DP PHY
 * @aux: DisplayPort AUX channel
 * @dp_phy: the DP PHY to get the link status for
 * @link_status: buffer to return the status in
 *
 * Fetch the AUX DPCD registers for the DPRX or an LTTPR PHY link status. The
 * layout of the returned @link_status matches the DPCD register layout of the
 * DPRX PHY link status.
 *
 * Returns 0 if the information was read successfully or a negative error code
 * on failure.
 */
int drm_dp_dpcd_read_phy_link_status(struct drm_dp_aux *aux,
				     enum drm_dp_phy dp_phy,
				     u8 link_status[DP_LINK_STATUS_SIZE])
{
	int ret;

	if (dp_phy == DP_PHY_DPRX) {
		ret = drm_dp_dpcd_read(aux,
				       DP_LANE0_1_STATUS,
				       link_status,
				       DP_LINK_STATUS_SIZE);

		if (ret < 0)
			return ret;

		WARN_ON(ret != DP_LINK_STATUS_SIZE);

		return 0;
	}

	ret = drm_dp_dpcd_read(aux,
			       DP_LANE0_1_STATUS_PHY_REPEATER(dp_phy),
			       link_status,
			       DP_LINK_STATUS_SIZE - 1);

	if (ret < 0)
		return ret;

	WARN_ON(ret != DP_LINK_STATUS_SIZE - 1);

	/* Convert the LTTPR to the sink PHY link status layout */
	memmove(&link_status[DP_SINK_STATUS - DP_LANE0_1_STATUS + 1],
		&link_status[DP_SINK_STATUS - DP_LANE0_1_STATUS],
		DP_LINK_STATUS_SIZE - (DP_SINK_STATUS - DP_LANE0_1_STATUS) - 1);
	link_status[DP_SINK_STATUS - DP_LANE0_1_STATUS] = 0;

	return 0;
}
EXPORT_SYMBOL(drm_dp_dpcd_read_phy_link_status);

static bool is_edid_digital_input_dp(const struct edid *edid)
{
	return edid && edid->revision >= 4 &&
		edid->input & DRM_EDID_INPUT_DIGITAL &&
		(edid->input & DRM_EDID_DIGITAL_TYPE_MASK) == DRM_EDID_DIGITAL_TYPE_DP;
}

/**
 * drm_dp_downstream_is_type() - is the downstream facing port of certain type?
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 * @type: port type to be checked. Can be:
 * 	  %DP_DS_PORT_TYPE_DP, %DP_DS_PORT_TYPE_VGA, %DP_DS_PORT_TYPE_DVI,
 * 	  %DP_DS_PORT_TYPE_HDMI, %DP_DS_PORT_TYPE_NON_EDID,
 *	  %DP_DS_PORT_TYPE_DP_DUALMODE or %DP_DS_PORT_TYPE_WIRELESS.
 *
 * Caveat: Only works with DPCD 1.1+ port caps.
 *
 * Returns: whether the downstream facing port matches the type.
 */
bool drm_dp_downstream_is_type(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			       const u8 port_cap[4], u8 type)
{
	return drm_dp_is_branch(dpcd) &&
		dpcd[DP_DPCD_REV] >= 0x11 &&
		(port_cap[0] & DP_DS_PORT_TYPE_MASK) == type;
}
EXPORT_SYMBOL(drm_dp_downstream_is_type);

/**
 * drm_dp_downstream_is_tmds() - is the downstream facing port TMDS?
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 * @edid: EDID
 *
 * Returns: whether the downstream facing port is TMDS (HDMI/DVI).
 */
bool drm_dp_downstream_is_tmds(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			       const u8 port_cap[4],
			       const struct edid *edid)
{
	if (dpcd[DP_DPCD_REV] < 0x11) {
		switch (dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DWN_STRM_PORT_TYPE_MASK) {
		case DP_DWN_STRM_PORT_TYPE_TMDS:
			return true;
		default:
			return false;
		}
	}

	switch (port_cap[0] & DP_DS_PORT_TYPE_MASK) {
	case DP_DS_PORT_TYPE_DP_DUALMODE:
		if (is_edid_digital_input_dp(edid))
			return false;
		fallthrough;
	case DP_DS_PORT_TYPE_DVI:
	case DP_DS_PORT_TYPE_HDMI:
		return true;
	default:
		return false;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_is_tmds);

/**
 * drm_dp_send_real_edid_checksum() - send back real edid checksum value
 * @aux: DisplayPort AUX channel
 * @real_edid_checksum: real edid checksum for the last block
 *
 * Returns:
 * True on success
 */
bool drm_dp_send_real_edid_checksum(struct drm_dp_aux *aux,
				    u8 real_edid_checksum)
{
	u8 link_edid_read = 0, auto_test_req = 0, test_resp = 0;

	if (drm_dp_dpcd_read(aux, DP_DEVICE_SERVICE_IRQ_VECTOR,
			     &auto_test_req, 1) < 1) {
		drm_err(aux->drm_dev, "%s: DPCD failed read at register 0x%x\n",
			aux->name, DP_DEVICE_SERVICE_IRQ_VECTOR);
		return false;
	}
	auto_test_req &= DP_AUTOMATED_TEST_REQUEST;

	if (drm_dp_dpcd_read(aux, DP_TEST_REQUEST, &link_edid_read, 1) < 1) {
		drm_err(aux->drm_dev, "%s: DPCD failed read at register 0x%x\n",
			aux->name, DP_TEST_REQUEST);
		return false;
	}
	link_edid_read &= DP_TEST_LINK_EDID_READ;

	if (!auto_test_req || !link_edid_read) {
		drm_dbg_kms(aux->drm_dev, "%s: Source DUT does not support TEST_EDID_READ\n",
			    aux->name);
		return false;
	}

	if (drm_dp_dpcd_write(aux, DP_DEVICE_SERVICE_IRQ_VECTOR,
			      &auto_test_req, 1) < 1) {
		drm_err(aux->drm_dev, "%s: DPCD failed write at register 0x%x\n",
			aux->name, DP_DEVICE_SERVICE_IRQ_VECTOR);
		return false;
	}

	/* send back checksum for the last edid extension block data */
	if (drm_dp_dpcd_write(aux, DP_TEST_EDID_CHECKSUM,
			      &real_edid_checksum, 1) < 1) {
		drm_err(aux->drm_dev, "%s: DPCD failed write at register 0x%x\n",
			aux->name, DP_TEST_EDID_CHECKSUM);
		return false;
	}

	test_resp |= DP_TEST_EDID_CHECKSUM_WRITE;
	if (drm_dp_dpcd_write(aux, DP_TEST_RESPONSE, &test_resp, 1) < 1) {
		drm_err(aux->drm_dev, "%s: DPCD failed write at register 0x%x\n",
			aux->name, DP_TEST_RESPONSE);
		return false;
	}

	return true;
}
EXPORT_SYMBOL(drm_dp_send_real_edid_checksum);

static u8 drm_dp_downstream_port_count(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	u8 port_count = dpcd[DP_DOWN_STREAM_PORT_COUNT] & DP_PORT_COUNT_MASK;

	if (dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DETAILED_CAP_INFO_AVAILABLE && port_count > 4)
		port_count = 4;

	return port_count;
}

static int drm_dp_read_extended_dpcd_caps(struct drm_dp_aux *aux,
					  u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	u8 dpcd_ext[DP_RECEIVER_CAP_SIZE];
	int ret;

	/*
	 * Prior to DP1.3 the bit represented by
	 * DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT was reserved.
	 * If it is set DP_DPCD_REV at 0000h could be at a value less than
	 * the true capability of the panel. The only way to check is to
	 * then compare 0000h and 2200h.
	 */
	if (!(dpcd[DP_TRAINING_AUX_RD_INTERVAL] &
	      DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT))
		return 0;

	ret = drm_dp_dpcd_read(aux, DP_DP13_DPCD_REV, &dpcd_ext,
			       sizeof(dpcd_ext));
	if (ret < 0)
		return ret;
	if (ret != sizeof(dpcd_ext))
		return -EIO;

	if (dpcd[DP_DPCD_REV] > dpcd_ext[DP_DPCD_REV]) {
		drm_dbg_kms(aux->drm_dev,
			    "%s: Extended DPCD rev less than base DPCD rev (%d > %d)\n",
			    aux->name, dpcd[DP_DPCD_REV], dpcd_ext[DP_DPCD_REV]);
		return 0;
	}

	if (!memcmp(dpcd, dpcd_ext, sizeof(dpcd_ext)))
		return 0;

	drm_dbg_kms(aux->drm_dev, "%s: Base DPCD: %*ph\n", aux->name, DP_RECEIVER_CAP_SIZE, dpcd);

	memcpy(dpcd, dpcd_ext, sizeof(dpcd_ext));

	return 0;
}

/**
 * drm_dp_read_dpcd_caps() - read DPCD caps and extended DPCD caps if
 * available
 * @aux: DisplayPort AUX channel
 * @dpcd: Buffer to store the resulting DPCD in
 *
 * Attempts to read the base DPCD caps for @aux. Additionally, this function
 * checks for and reads the extended DPRX caps (%DP_DP13_DPCD_REV) if
 * present.
 *
 * Returns: %0 if the DPCD was read successfully, negative error code
 * otherwise.
 */
int drm_dp_read_dpcd_caps(struct drm_dp_aux *aux,
			  u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	int ret;

	ret = drm_dp_dpcd_read(aux, DP_DPCD_REV, dpcd, DP_RECEIVER_CAP_SIZE);
	if (ret < 0)
		return ret;
	if (ret != DP_RECEIVER_CAP_SIZE || dpcd[DP_DPCD_REV] == 0)
		return -EIO;

	ret = drm_dp_read_extended_dpcd_caps(aux, dpcd);
	if (ret < 0)
		return ret;

	drm_dbg_kms(aux->drm_dev, "%s: DPCD: %*ph\n", aux->name, DP_RECEIVER_CAP_SIZE, dpcd);

	return ret;
}
EXPORT_SYMBOL(drm_dp_read_dpcd_caps);

/**
 * drm_dp_read_downstream_info() - read DPCD downstream port info if available
 * @aux: DisplayPort AUX channel
 * @dpcd: A cached copy of the port's DPCD
 * @downstream_ports: buffer to store the downstream port info in
 *
 * See also:
 * drm_dp_downstream_max_clock()
 * drm_dp_downstream_max_bpc()
 *
 * Returns: 0 if either the downstream port info was read successfully or
 * there was no downstream info to read, or a negative error code otherwise.
 */
int drm_dp_read_downstream_info(struct drm_dp_aux *aux,
				const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				u8 downstream_ports[DP_MAX_DOWNSTREAM_PORTS])
{
	int ret;
	u8 len;

	memset(downstream_ports, 0, DP_MAX_DOWNSTREAM_PORTS);

	/* No downstream info to read */
	if (!drm_dp_is_branch(dpcd) || dpcd[DP_DPCD_REV] == DP_DPCD_REV_10)
		return 0;

	/* Some branches advertise having 0 downstream ports, despite also advertising they have a
	 * downstream port present. The DP spec isn't clear on if this is allowed or not, but since
	 * some branches do it we need to handle it regardless.
	 */
	len = drm_dp_downstream_port_count(dpcd);
	if (!len)
		return 0;

	if (dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DETAILED_CAP_INFO_AVAILABLE)
		len *= 4;

	ret = drm_dp_dpcd_read(aux, DP_DOWNSTREAM_PORT_0, downstream_ports, len);
	if (ret < 0)
		return ret;
	if (ret != len)
		return -EIO;

	drm_dbg_kms(aux->drm_dev, "%s: DPCD DFP: %*ph\n", aux->name, len, downstream_ports);

	return 0;
}
EXPORT_SYMBOL(drm_dp_read_downstream_info);

/**
 * drm_dp_downstream_max_dotclock() - extract downstream facing port max dot clock
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 *
 * Returns: Downstream facing port max dot clock in kHz on success,
 * or 0 if max clock not defined
 */
int drm_dp_downstream_max_dotclock(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				   const u8 port_cap[4])
{
	if (!drm_dp_is_branch(dpcd))
		return 0;

	if (dpcd[DP_DPCD_REV] < 0x11)
		return 0;

	switch (port_cap[0] & DP_DS_PORT_TYPE_MASK) {
	case DP_DS_PORT_TYPE_VGA:
		if ((dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DETAILED_CAP_INFO_AVAILABLE) == 0)
			return 0;
		return port_cap[1] * 8000;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_max_dotclock);

/**
 * drm_dp_downstream_max_tmds_clock() - extract downstream facing port max TMDS clock
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 * @edid: EDID
 *
 * Returns: HDMI/DVI downstream facing port max TMDS clock in kHz on success,
 * or 0 if max TMDS clock not defined
 */
int drm_dp_downstream_max_tmds_clock(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				     const u8 port_cap[4],
				     const struct edid *edid)
{
	if (!drm_dp_is_branch(dpcd))
		return 0;

	if (dpcd[DP_DPCD_REV] < 0x11) {
		switch (dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DWN_STRM_PORT_TYPE_MASK) {
		case DP_DWN_STRM_PORT_TYPE_TMDS:
			return 165000;
		default:
			return 0;
		}
	}

	switch (port_cap[0] & DP_DS_PORT_TYPE_MASK) {
	case DP_DS_PORT_TYPE_DP_DUALMODE:
		if (is_edid_digital_input_dp(edid))
			return 0;
		/*
		 * It's left up to the driver to check the
		 * DP dual mode adapter's max TMDS clock.
		 *
		 * Unfortunately it looks like branch devices
		 * may not fordward that the DP dual mode i2c
		 * access so we just usually get i2c nak :(
		 */
		fallthrough;
	case DP_DS_PORT_TYPE_HDMI:
		 /*
		  * We should perhaps assume 165 MHz when detailed cap
		  * info is not available. But looks like many typical
		  * branch devices fall into that category and so we'd
		  * probably end up with users complaining that they can't
		  * get high resolution modes with their favorite dongle.
		  *
		  * So let's limit to 300 MHz instead since DPCD 1.4
		  * HDMI 2.0 DFPs are required to have the detailed cap
		  * info. So it's more likely we're dealing with a HDMI 1.4
		  * compatible* device here.
		  */
		if ((dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DETAILED_CAP_INFO_AVAILABLE) == 0)
			return 300000;
		return port_cap[1] * 2500;
	case DP_DS_PORT_TYPE_DVI:
		if ((dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DETAILED_CAP_INFO_AVAILABLE) == 0)
			return 165000;
		/* FIXME what to do about DVI dual link? */
		return port_cap[1] * 2500;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_max_tmds_clock);

/**
 * drm_dp_downstream_min_tmds_clock() - extract downstream facing port min TMDS clock
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 * @edid: EDID
 *
 * Returns: HDMI/DVI downstream facing port min TMDS clock in kHz on success,
 * or 0 if max TMDS clock not defined
 */
int drm_dp_downstream_min_tmds_clock(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				     const u8 port_cap[4],
				     const struct edid *edid)
{
	if (!drm_dp_is_branch(dpcd))
		return 0;

	if (dpcd[DP_DPCD_REV] < 0x11) {
		switch (dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DWN_STRM_PORT_TYPE_MASK) {
		case DP_DWN_STRM_PORT_TYPE_TMDS:
			return 25000;
		default:
			return 0;
		}
	}

	switch (port_cap[0] & DP_DS_PORT_TYPE_MASK) {
	case DP_DS_PORT_TYPE_DP_DUALMODE:
		if (is_edid_digital_input_dp(edid))
			return 0;
		fallthrough;
	case DP_DS_PORT_TYPE_DVI:
	case DP_DS_PORT_TYPE_HDMI:
		/*
		 * Unclear whether the protocol converter could
		 * utilize pixel replication. Assume it won't.
		 */
		return 25000;
	default:
		return 0;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_min_tmds_clock);

/**
 * drm_dp_downstream_max_bpc() - extract downstream facing port max
 *                               bits per component
 * @dpcd: DisplayPort configuration data
 * @port_cap: downstream facing port capabilities
 * @edid: EDID
 *
 * Returns: Max bpc on success or 0 if max bpc not defined
 */
int drm_dp_downstream_max_bpc(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			      const u8 port_cap[4],
			      const struct edid *edid)
{
	if (!drm_dp_is_branch(dpcd))
		return 0;

	if (dpcd[DP_DPCD_REV] < 0x11) {
		switch (dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DWN_STRM_PORT_TYPE_MASK) {
		case DP_DWN_STRM_PORT_TYPE_DP:
			return 0;
		default:
			return 8;
		}
	}

	switch (port_cap[0] & DP_DS_PORT_TYPE_MASK) {
	case DP_DS_PORT_TYPE_DP:
		return 0;
	case DP_DS_PORT_TYPE_DP_DUALMODE:
		if (is_edid_digital_input_dp(edid))
			return 0;
		fallthrough;
	case DP_DS_PORT_TYPE_HDMI:
	case DP_DS_PORT_TYPE_DVI:
	case DP_DS_PORT_TYPE_VGA:
		if ((dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DETAILED_CAP_INFO_AVAILABLE) == 0)
			return 8;

		switch (port_cap[2] & DP_DS_MAX_BPC_MASK) {
		case DP_DS_8BPC:
			return 8;
		case DP_DS_10BPC:
			return 10;
		case DP_DS_12BPC:
			return 12;
		case DP_DS_16BPC:
			return 16;
		default:
			return 8;
		}
		break;
	default:
		return 8;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_max_bpc);

/**
 * drm_dp_downstream_420_passthrough() - determine downstream facing port
 *                                       YCbCr 4:2:0 pass-through capability
 * @dpcd: DisplayPort configuration data
 * @port_cap: downstream facing port capabilities
 *
 * Returns: whether the downstream facing port can pass through YCbCr 4:2:0
 */
bool drm_dp_downstream_420_passthrough(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				       const u8 port_cap[4])
{
	if (!drm_dp_is_branch(dpcd))
		return false;

	if (dpcd[DP_DPCD_REV] < 0x13)
		return false;

	switch (port_cap[0] & DP_DS_PORT_TYPE_MASK) {
	case DP_DS_PORT_TYPE_DP:
		return true;
	case DP_DS_PORT_TYPE_HDMI:
		if ((dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DETAILED_CAP_INFO_AVAILABLE) == 0)
			return false;

		return port_cap[3] & DP_DS_HDMI_YCBCR420_PASS_THROUGH;
	default:
		return false;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_420_passthrough);

/**
 * drm_dp_downstream_444_to_420_conversion() - determine downstream facing port
 *                                             YCbCr 4:4:4->4:2:0 conversion capability
 * @dpcd: DisplayPort configuration data
 * @port_cap: downstream facing port capabilities
 *
 * Returns: whether the downstream facing port can convert YCbCr 4:4:4 to 4:2:0
 */
bool drm_dp_downstream_444_to_420_conversion(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
					     const u8 port_cap[4])
{
	if (!drm_dp_is_branch(dpcd))
		return false;

	if (dpcd[DP_DPCD_REV] < 0x13)
		return false;

	switch (port_cap[0] & DP_DS_PORT_TYPE_MASK) {
	case DP_DS_PORT_TYPE_HDMI:
		if ((dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DETAILED_CAP_INFO_AVAILABLE) == 0)
			return false;

		return port_cap[3] & DP_DS_HDMI_YCBCR444_TO_420_CONV;
	default:
		return false;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_444_to_420_conversion);

/**
 * drm_dp_downstream_rgb_to_ycbcr_conversion() - determine downstream facing port
 *                                               RGB->YCbCr conversion capability
 * @dpcd: DisplayPort configuration data
 * @port_cap: downstream facing port capabilities
 * @color_spc: Colorspace for which conversion cap is sought
 *
 * Returns: whether the downstream facing port can convert RGB->YCbCr for a given
 * colorspace.
 */
bool drm_dp_downstream_rgb_to_ycbcr_conversion(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
					       const u8 port_cap[4],
					       u8 color_spc)
{
	if (!drm_dp_is_branch(dpcd))
		return false;

	if (dpcd[DP_DPCD_REV] < 0x13)
		return false;

	switch (port_cap[0] & DP_DS_PORT_TYPE_MASK) {
	case DP_DS_PORT_TYPE_HDMI:
		if ((dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DETAILED_CAP_INFO_AVAILABLE) == 0)
			return false;

		return port_cap[3] & color_spc;
	default:
		return false;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_rgb_to_ycbcr_conversion);

/**
 * drm_dp_downstream_mode() - return a mode for downstream facing port
 * @dev: DRM device
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 *
 * Provides a suitable mode for downstream facing ports without EDID.
 *
 * Returns: A new drm_display_mode on success or NULL on failure
 */
struct drm_display_mode *
drm_dp_downstream_mode(struct drm_device *dev,
		       const u8 dpcd[DP_RECEIVER_CAP_SIZE],
		       const u8 port_cap[4])

{
	u8 vic;

	if (!drm_dp_is_branch(dpcd))
		return NULL;

	if (dpcd[DP_DPCD_REV] < 0x11)
		return NULL;

	switch (port_cap[0] & DP_DS_PORT_TYPE_MASK) {
	case DP_DS_PORT_TYPE_NON_EDID:
		switch (port_cap[0] & DP_DS_NON_EDID_MASK) {
		case DP_DS_NON_EDID_720x480i_60:
			vic = 6;
			break;
		case DP_DS_NON_EDID_720x480i_50:
			vic = 21;
			break;
		case DP_DS_NON_EDID_1920x1080i_60:
			vic = 5;
			break;
		case DP_DS_NON_EDID_1920x1080i_50:
			vic = 20;
			break;
		case DP_DS_NON_EDID_1280x720_60:
			vic = 4;
			break;
		case DP_DS_NON_EDID_1280x720_50:
			vic = 19;
			break;
		default:
			return NULL;
		}
		return drm_display_mode_from_cea_vic(dev, vic);
	default:
		return NULL;
	}
}
EXPORT_SYMBOL(drm_dp_downstream_mode);

/**
 * drm_dp_downstream_id() - identify branch device
 * @aux: DisplayPort AUX channel
 * @id: DisplayPort branch device id
 *
 * Returns branch device id on success or NULL on failure
 */
int drm_dp_downstream_id(struct drm_dp_aux *aux, char id[6])
{
	return drm_dp_dpcd_read(aux, DP_BRANCH_ID, id, 6);
}
EXPORT_SYMBOL(drm_dp_downstream_id);

/**
 * drm_dp_downstream_debug() - debug DP branch devices
 * @m: pointer for debugfs file
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 * @edid: EDID
 * @aux: DisplayPort AUX channel
 *
 */
void drm_dp_downstream_debug(struct seq_file *m,
			     const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			     const u8 port_cap[4],
			     const struct edid *edid,
			     struct drm_dp_aux *aux)
{
	bool detailed_cap_info = dpcd[DP_DOWNSTREAMPORT_PRESENT] &
				 DP_DETAILED_CAP_INFO_AVAILABLE;
	int clk;
	int bpc;
	char id[7];
	int len;
	uint8_t rev[2];
	int type = port_cap[0] & DP_DS_PORT_TYPE_MASK;
	bool branch_device = drm_dp_is_branch(dpcd);

	seq_printf(m, "\tDP branch device present: %s\n",
		   branch_device ? "yes" : "no");

	if (!branch_device)
		return;

	switch (type) {
	case DP_DS_PORT_TYPE_DP:
		seq_puts(m, "\t\tType: DisplayPort\n");
		break;
	case DP_DS_PORT_TYPE_VGA:
		seq_puts(m, "\t\tType: VGA\n");
		break;
	case DP_DS_PORT_TYPE_DVI:
		seq_puts(m, "\t\tType: DVI\n");
		break;
	case DP_DS_PORT_TYPE_HDMI:
		seq_puts(m, "\t\tType: HDMI\n");
		break;
	case DP_DS_PORT_TYPE_NON_EDID:
		seq_puts(m, "\t\tType: others without EDID support\n");
		break;
	case DP_DS_PORT_TYPE_DP_DUALMODE:
		seq_puts(m, "\t\tType: DP++\n");
		break;
	case DP_DS_PORT_TYPE_WIRELESS:
		seq_puts(m, "\t\tType: Wireless\n");
		break;
	default:
		seq_puts(m, "\t\tType: N/A\n");
	}

	memset(id, 0, sizeof(id));
	drm_dp_downstream_id(aux, id);
	seq_printf(m, "\t\tID: %s\n", id);

	len = drm_dp_dpcd_read(aux, DP_BRANCH_HW_REV, &rev[0], 1);
	if (len > 0)
		seq_printf(m, "\t\tHW: %d.%d\n",
			   (rev[0] & 0xf0) >> 4, rev[0] & 0xf);

	len = drm_dp_dpcd_read(aux, DP_BRANCH_SW_REV, rev, 2);
	if (len > 0)
		seq_printf(m, "\t\tSW: %d.%d\n", rev[0], rev[1]);

	if (detailed_cap_info) {
		clk = drm_dp_downstream_max_dotclock(dpcd, port_cap);
		if (clk > 0)
			seq_printf(m, "\t\tMax dot clock: %d kHz\n", clk);

		clk = drm_dp_downstream_max_tmds_clock(dpcd, port_cap, edid);
		if (clk > 0)
			seq_printf(m, "\t\tMax TMDS clock: %d kHz\n", clk);

		clk = drm_dp_downstream_min_tmds_clock(dpcd, port_cap, edid);
		if (clk > 0)
			seq_printf(m, "\t\tMin TMDS clock: %d kHz\n", clk);

		bpc = drm_dp_downstream_max_bpc(dpcd, port_cap, edid);

		if (bpc > 0)
			seq_printf(m, "\t\tMax bpc: %d\n", bpc);
	}
}
EXPORT_SYMBOL(drm_dp_downstream_debug);

/**
 * drm_dp_subconnector_type() - get DP branch device type
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 */
enum drm_mode_subconnector
drm_dp_subconnector_type(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			 const u8 port_cap[4])
{
	int type;
	if (!drm_dp_is_branch(dpcd))
		return DRM_MODE_SUBCONNECTOR_Native;
	/* DP 1.0 approach */
	if (dpcd[DP_DPCD_REV] == DP_DPCD_REV_10) {
		type = dpcd[DP_DOWNSTREAMPORT_PRESENT] &
		       DP_DWN_STRM_PORT_TYPE_MASK;

		switch (type) {
		case DP_DWN_STRM_PORT_TYPE_TMDS:
			/* Can be HDMI or DVI-D, DVI-D is a safer option */
			return DRM_MODE_SUBCONNECTOR_DVID;
		case DP_DWN_STRM_PORT_TYPE_ANALOG:
			/* Can be VGA or DVI-A, VGA is more popular */
			return DRM_MODE_SUBCONNECTOR_VGA;
		case DP_DWN_STRM_PORT_TYPE_DP:
			return DRM_MODE_SUBCONNECTOR_DisplayPort;
		case DP_DWN_STRM_PORT_TYPE_OTHER:
		default:
			return DRM_MODE_SUBCONNECTOR_Unknown;
		}
	}
	type = port_cap[0] & DP_DS_PORT_TYPE_MASK;

	switch (type) {
	case DP_DS_PORT_TYPE_DP:
	case DP_DS_PORT_TYPE_DP_DUALMODE:
		return DRM_MODE_SUBCONNECTOR_DisplayPort;
	case DP_DS_PORT_TYPE_VGA:
		return DRM_MODE_SUBCONNECTOR_VGA;
	case DP_DS_PORT_TYPE_DVI:
		return DRM_MODE_SUBCONNECTOR_DVID;
	case DP_DS_PORT_TYPE_HDMI:
		return DRM_MODE_SUBCONNECTOR_HDMIA;
	case DP_DS_PORT_TYPE_WIRELESS:
		return DRM_MODE_SUBCONNECTOR_Wireless;
	case DP_DS_PORT_TYPE_NON_EDID:
	default:
		return DRM_MODE_SUBCONNECTOR_Unknown;
	}
}
EXPORT_SYMBOL(drm_dp_subconnector_type);

/**
 * drm_dp_set_subconnector_property - set subconnector for DP connector
 * @connector: connector to set property on
 * @status: connector status
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 *
 * Called by a driver on every detect event.
 */
void drm_dp_set_subconnector_property(struct drm_connector *connector,
				      enum drm_connector_status status,
				      const u8 *dpcd,
				      const u8 port_cap[4])
{
	enum drm_mode_subconnector subconnector = DRM_MODE_SUBCONNECTOR_Unknown;

	if (status == connector_status_connected)
		subconnector = drm_dp_subconnector_type(dpcd, port_cap);
	drm_object_property_set_value(&connector->base,
			connector->dev->mode_config.dp_subconnector_property,
			subconnector);
}
EXPORT_SYMBOL(drm_dp_set_subconnector_property);

/**
 * drm_dp_read_sink_count_cap() - Check whether a given connector has a valid sink
 * count
 * @connector: The DRM connector to check
 * @dpcd: A cached copy of the connector's DPCD RX capabilities
 * @desc: A cached copy of the connector's DP descriptor
 *
 * See also: drm_dp_read_sink_count()
 *
 * Returns: %True if the (e)DP connector has a valid sink count that should
 * be probed, %false otherwise.
 */
bool drm_dp_read_sink_count_cap(struct drm_connector *connector,
				const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				const struct drm_dp_desc *desc)
{
	/* Some eDP panels don't set a valid value for the sink count */
	return connector->connector_type != DRM_MODE_CONNECTOR_eDP &&
		dpcd[DP_DPCD_REV] >= DP_DPCD_REV_11 &&
		dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DWN_STRM_PORT_PRESENT &&
		!drm_dp_has_quirk(desc, DP_DPCD_QUIRK_NO_SINK_COUNT);
}
EXPORT_SYMBOL(drm_dp_read_sink_count_cap);

/**
 * drm_dp_read_sink_count() - Retrieve the sink count for a given sink
 * @aux: The DP AUX channel to use
 *
 * See also: drm_dp_read_sink_count_cap()
 *
 * Returns: The current sink count reported by @aux, or a negative error code
 * otherwise.
 */
int drm_dp_read_sink_count(struct drm_dp_aux *aux)
{
	u8 count;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_SINK_COUNT, &count);
	if (ret < 0)
		return ret;
	if (ret != 1)
		return -EIO;

	return DP_GET_SINK_COUNT(count);
}
EXPORT_SYMBOL(drm_dp_read_sink_count);

/*
 * I2C-over-AUX implementation
 */

static u32 drm_dp_i2c_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA |
	       I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
	       I2C_FUNC_10BIT_ADDR;
}

static void drm_dp_i2c_msg_write_status_update(struct drm_dp_aux_msg *msg)
{
	/*
	 * In case of i2c defer or short i2c ack reply to a write,
	 * we need to switch to WRITE_STATUS_UPDATE to drain the
	 * rest of the message
	 */
	if ((msg->request & ~DP_AUX_I2C_MOT) == DP_AUX_I2C_WRITE) {
		msg->request &= DP_AUX_I2C_MOT;
		msg->request |= DP_AUX_I2C_WRITE_STATUS_UPDATE;
	}
}

#define AUX_PRECHARGE_LEN 10 /* 10 to 16 */
#define AUX_SYNC_LEN (16 + 4) /* preamble + AUX_SYNC_END */
#define AUX_STOP_LEN 4
#define AUX_CMD_LEN 4
#define AUX_ADDRESS_LEN 20
#define AUX_REPLY_PAD_LEN 4
#define AUX_LENGTH_LEN 8

/*
 * Calculate the duration of the AUX request/reply in usec. Gives the
 * "best" case estimate, ie. successful while as short as possible.
 */
static int drm_dp_aux_req_duration(const struct drm_dp_aux_msg *msg)
{
	int len = AUX_PRECHARGE_LEN + AUX_SYNC_LEN + AUX_STOP_LEN +
		AUX_CMD_LEN + AUX_ADDRESS_LEN + AUX_LENGTH_LEN;

	if ((msg->request & DP_AUX_I2C_READ) == 0)
		len += msg->size * 8;

	return len;
}

static int drm_dp_aux_reply_duration(const struct drm_dp_aux_msg *msg)
{
	int len = AUX_PRECHARGE_LEN + AUX_SYNC_LEN + AUX_STOP_LEN +
		AUX_CMD_LEN + AUX_REPLY_PAD_LEN;

	/*
	 * For read we expect what was asked. For writes there will
	 * be 0 or 1 data bytes. Assume 0 for the "best" case.
	 */
	if (msg->request & DP_AUX_I2C_READ)
		len += msg->size * 8;

	return len;
}

#define I2C_START_LEN 1
#define I2C_STOP_LEN 1
#define I2C_ADDR_LEN 9 /* ADDRESS + R/W + ACK/NACK */
#define I2C_DATA_LEN 9 /* DATA + ACK/NACK */

/*
 * Calculate the length of the i2c transfer in usec, assuming
 * the i2c bus speed is as specified. Gives the the "worst"
 * case estimate, ie. successful while as long as possible.
 * Doesn't account the the "MOT" bit, and instead assumes each
 * message includes a START, ADDRESS and STOP. Neither does it
 * account for additional random variables such as clock stretching.
 */
static int drm_dp_i2c_msg_duration(const struct drm_dp_aux_msg *msg,
				   int i2c_speed_khz)
{
	/* AUX bitrate is 1MHz, i2c bitrate as specified */
	return DIV_ROUND_UP((I2C_START_LEN + I2C_ADDR_LEN +
			     msg->size * I2C_DATA_LEN +
			     I2C_STOP_LEN) * 1000, i2c_speed_khz);
}

/*
 * Determine how many retries should be attempted to successfully transfer
 * the specified message, based on the estimated durations of the
 * i2c and AUX transfers.
 */
static int drm_dp_i2c_retry_count(const struct drm_dp_aux_msg *msg,
			      int i2c_speed_khz)
{
	int aux_time_us = drm_dp_aux_req_duration(msg) +
		drm_dp_aux_reply_duration(msg);
	int i2c_time_us = drm_dp_i2c_msg_duration(msg, i2c_speed_khz);

	return DIV_ROUND_UP(i2c_time_us, aux_time_us + AUX_RETRY_INTERVAL);
}

/*
 * FIXME currently assumes 10 kHz as some real world devices seem
 * to require it. We should query/set the speed via DPCD if supported.
 */
static int dp_aux_i2c_speed_khz __read_mostly = 10;
module_param_unsafe(dp_aux_i2c_speed_khz, int, 0644);
MODULE_PARM_DESC(dp_aux_i2c_speed_khz,
		 "Assumed speed of the i2c bus in kHz, (1-400, default 10)");

/*
 * Transfer a single I2C-over-AUX message and handle various error conditions,
 * retrying the transaction as appropriate.  It is assumed that the
 * &drm_dp_aux.transfer function does not modify anything in the msg other than the
 * reply field.
 *
 * Returns bytes transferred on success, or a negative error code on failure.
 */
static int drm_dp_i2c_do_msg(struct drm_dp_aux *aux, struct drm_dp_aux_msg *msg)
{
	unsigned int retry, defer_i2c;
	int ret;
	/*
	 * DP1.2 sections 2.7.7.1.5.6.1 and 2.7.7.1.6.6.1: A DP Source device
	 * is required to retry at least seven times upon receiving AUX_DEFER
	 * before giving up the AUX transaction.
	 *
	 * We also try to account for the i2c bus speed.
	 */
	int max_retries = max(7, drm_dp_i2c_retry_count(msg, dp_aux_i2c_speed_khz));

	for (retry = 0, defer_i2c = 0; retry < (max_retries + defer_i2c); retry++) {
		ret = aux->transfer(aux, msg);
		if (ret < 0) {
			if (ret == -EBUSY)
				continue;

			/*
			 * While timeouts can be errors, they're usually normal
			 * behavior (for instance, when a driver tries to
			 * communicate with a non-existent DisplayPort device).
			 * Avoid spamming the kernel log with timeout errors.
			 */
			if (ret == -ETIMEDOUT)
				drm_dbg_kms_ratelimited(aux->drm_dev, "%s: transaction timed out\n",
							aux->name);
			else
				drm_dbg_kms(aux->drm_dev, "%s: transaction failed: %d\n",
					    aux->name, ret);
			return ret;
		}


		switch (msg->reply & DP_AUX_NATIVE_REPLY_MASK) {
		case DP_AUX_NATIVE_REPLY_ACK:
			/*
			 * For I2C-over-AUX transactions this isn't enough, we
			 * need to check for the I2C ACK reply.
			 */
			break;

		case DP_AUX_NATIVE_REPLY_NACK:
			drm_dbg_kms(aux->drm_dev, "%s: native nack (result=%d, size=%zu)\n",
				    aux->name, ret, msg->size);
			return -EREMOTEIO;

		case DP_AUX_NATIVE_REPLY_DEFER:
			drm_dbg_kms(aux->drm_dev, "%s: native defer\n", aux->name);
			/*
			 * We could check for I2C bit rate capabilities and if
			 * available adjust this interval. We could also be
			 * more careful with DP-to-legacy adapters where a
			 * long legacy cable may force very low I2C bit rates.
			 *
			 * For now just defer for long enough to hopefully be
			 * safe for all use-cases.
			 */
			usleep_range(AUX_RETRY_INTERVAL, AUX_RETRY_INTERVAL + 100);
			continue;

		default:
			drm_err(aux->drm_dev, "%s: invalid native reply %#04x\n",
				aux->name, msg->reply);
			return -EREMOTEIO;
		}

		switch (msg->reply & DP_AUX_I2C_REPLY_MASK) {
		case DP_AUX_I2C_REPLY_ACK:
			/*
			 * Both native ACK and I2C ACK replies received. We
			 * can assume the transfer was successful.
			 */
			if (ret != msg->size)
				drm_dp_i2c_msg_write_status_update(msg);
			return ret;

		case DP_AUX_I2C_REPLY_NACK:
			drm_dbg_kms(aux->drm_dev, "%s: I2C nack (result=%d, size=%zu)\n",
				    aux->name, ret, msg->size);
			aux->i2c_nack_count++;
			return -EREMOTEIO;

		case DP_AUX_I2C_REPLY_DEFER:
			drm_dbg_kms(aux->drm_dev, "%s: I2C defer\n", aux->name);
			/* DP Compliance Test 4.2.2.5 Requirement:
			 * Must have at least 7 retries for I2C defers on the
			 * transaction to pass this test
			 */
			aux->i2c_defer_count++;
			if (defer_i2c < 7)
				defer_i2c++;
			usleep_range(AUX_RETRY_INTERVAL, AUX_RETRY_INTERVAL + 100);
			drm_dp_i2c_msg_write_status_update(msg);

			continue;

		default:
			drm_err(aux->drm_dev, "%s: invalid I2C reply %#04x\n",
				aux->name, msg->reply);
			return -EREMOTEIO;
		}
	}

	drm_dbg_kms(aux->drm_dev, "%s: Too many retries, giving up\n", aux->name);
	return -EREMOTEIO;
}

static void drm_dp_i2c_msg_set_request(struct drm_dp_aux_msg *msg,
				       const struct i2c_msg *i2c_msg)
{
	msg->request = (i2c_msg->flags & I2C_M_RD) ?
		DP_AUX_I2C_READ : DP_AUX_I2C_WRITE;
	if (!(i2c_msg->flags & I2C_M_STOP))
		msg->request |= DP_AUX_I2C_MOT;
}

/*
 * Keep retrying drm_dp_i2c_do_msg until all data has been transferred.
 *
 * Returns an error code on failure, or a recommended transfer size on success.
 */
static int drm_dp_i2c_drain_msg(struct drm_dp_aux *aux, struct drm_dp_aux_msg *orig_msg)
{
	int err, ret = orig_msg->size;
	struct drm_dp_aux_msg msg = *orig_msg;

	while (msg.size > 0) {
		err = drm_dp_i2c_do_msg(aux, &msg);
		if (err <= 0)
			return err == 0 ? -EPROTO : err;

		if (err < msg.size && err < ret) {
			drm_dbg_kms(aux->drm_dev,
				    "%s: Partial I2C reply: requested %zu bytes got %d bytes\n",
				    aux->name, msg.size, err);
			ret = err;
		}

		msg.size -= err;
		msg.buffer += err;
	}

	return ret;
}

/*
 * Bizlink designed DP->DVI-D Dual Link adapters require the I2C over AUX
 * packets to be as large as possible. If not, the I2C transactions never
 * succeed. Hence the default is maximum.
 */
static int dp_aux_i2c_transfer_size __read_mostly = DP_AUX_MAX_PAYLOAD_BYTES;
module_param_unsafe(dp_aux_i2c_transfer_size, int, 0644);
MODULE_PARM_DESC(dp_aux_i2c_transfer_size,
		 "Number of bytes to transfer in a single I2C over DP AUX CH message, (1-16, default 16)");

static int drm_dp_i2c_xfer(struct i2c_adapter *adapter, struct i2c_msg *msgs,
			   int num)
{
	struct drm_dp_aux *aux = adapter->algo_data;
	unsigned int i, j;
	unsigned transfer_size;
	struct drm_dp_aux_msg msg;
	int err = 0;

	dp_aux_i2c_transfer_size = clamp(dp_aux_i2c_transfer_size, 1, DP_AUX_MAX_PAYLOAD_BYTES);

	memset(&msg, 0, sizeof(msg));

	for (i = 0; i < num; i++) {
		msg.address = msgs[i].addr;
		drm_dp_i2c_msg_set_request(&msg, &msgs[i]);
		/* Send a bare address packet to start the transaction.
		 * Zero sized messages specify an address only (bare
		 * address) transaction.
		 */
		msg.buffer = NULL;
		msg.size = 0;
		err = drm_dp_i2c_do_msg(aux, &msg);

		/*
		 * Reset msg.request in case in case it got
		 * changed into a WRITE_STATUS_UPDATE.
		 */
		drm_dp_i2c_msg_set_request(&msg, &msgs[i]);

		if (err < 0)
			break;
		/* We want each transaction to be as large as possible, but
		 * we'll go to smaller sizes if the hardware gives us a
		 * short reply.
		 */
		transfer_size = dp_aux_i2c_transfer_size;
		for (j = 0; j < msgs[i].len; j += msg.size) {
			msg.buffer = msgs[i].buf + j;
			msg.size = min(transfer_size, msgs[i].len - j);

			err = drm_dp_i2c_drain_msg(aux, &msg);

			/*
			 * Reset msg.request in case in case it got
			 * changed into a WRITE_STATUS_UPDATE.
			 */
			drm_dp_i2c_msg_set_request(&msg, &msgs[i]);

			if (err < 0)
				break;
			transfer_size = err;
		}
		if (err < 0)
			break;
	}
	if (err >= 0)
		err = num;
	/* Send a bare address packet to close out the transaction.
	 * Zero sized messages specify an address only (bare
	 * address) transaction.
	 */
	msg.request &= ~DP_AUX_I2C_MOT;
	msg.buffer = NULL;
	msg.size = 0;
	(void)drm_dp_i2c_do_msg(aux, &msg);

	return err;
}

static const struct i2c_algorithm drm_dp_i2c_algo = {
	.functionality = drm_dp_i2c_functionality,
	.master_xfer = drm_dp_i2c_xfer,
};

static struct drm_dp_aux *i2c_to_aux(struct i2c_adapter *i2c)
{
	return container_of(i2c, struct drm_dp_aux, ddc);
}

static void lock_bus(struct i2c_adapter *i2c, unsigned int flags)
{
	mutex_lock(&i2c_to_aux(i2c)->hw_mutex);
}

static int trylock_bus(struct i2c_adapter *i2c, unsigned int flags)
{
	return mutex_trylock(&i2c_to_aux(i2c)->hw_mutex);
}

static void unlock_bus(struct i2c_adapter *i2c, unsigned int flags)
{
	mutex_unlock(&i2c_to_aux(i2c)->hw_mutex);
}

static const struct i2c_lock_operations drm_dp_i2c_lock_ops = {
	.lock_bus = lock_bus,
	.trylock_bus = trylock_bus,
	.unlock_bus = unlock_bus,
};

static int drm_dp_aux_get_crc(struct drm_dp_aux *aux, u8 *crc)
{
	u8 buf, count;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_TEST_SINK, &buf);
	if (ret < 0)
		return ret;

	WARN_ON(!(buf & DP_TEST_SINK_START));

	ret = drm_dp_dpcd_readb(aux, DP_TEST_SINK_MISC, &buf);
	if (ret < 0)
		return ret;

	count = buf & DP_TEST_COUNT_MASK;
	if (count == aux->crc_count)
		return -EAGAIN; /* No CRC yet */

	aux->crc_count = count;

	/*
	 * At DP_TEST_CRC_R_CR, there's 6 bytes containing CRC data, 2 bytes
	 * per component (RGB or CrYCb).
	 */
	ret = drm_dp_dpcd_read(aux, DP_TEST_CRC_R_CR, crc, 6);
	if (ret < 0)
		return ret;

	return 0;
}

static void drm_dp_aux_crc_work(struct work_struct *work)
{
	struct drm_dp_aux *aux = container_of(work, struct drm_dp_aux,
					      crc_work);
	struct drm_crtc *crtc;
	u8 crc_bytes[6];
	uint32_t crcs[3];
	int ret;

	if (WARN_ON(!aux->crtc))
		return;

	crtc = aux->crtc;
	while (crtc->crc.opened) {
		drm_crtc_wait_one_vblank(crtc);
		if (!crtc->crc.opened)
			break;

		ret = drm_dp_aux_get_crc(aux, crc_bytes);
		if (ret == -EAGAIN) {
			usleep_range(1000, 2000);
			ret = drm_dp_aux_get_crc(aux, crc_bytes);
		}

		if (ret == -EAGAIN) {
			drm_dbg_kms(aux->drm_dev, "%s: Get CRC failed after retrying: %d\n",
				    aux->name, ret);
			continue;
		} else if (ret) {
			drm_dbg_kms(aux->drm_dev, "%s: Failed to get a CRC: %d\n", aux->name, ret);
			continue;
		}

		crcs[0] = crc_bytes[0] | crc_bytes[1] << 8;
		crcs[1] = crc_bytes[2] | crc_bytes[3] << 8;
		crcs[2] = crc_bytes[4] | crc_bytes[5] << 8;
		drm_crtc_add_crc_entry(crtc, false, 0, crcs);
	}
}

/**
 * drm_dp_remote_aux_init() - minimally initialise a remote aux channel
 * @aux: DisplayPort AUX channel
 *
 * Used for remote aux channel in general. Merely initialize the crc work
 * struct.
 */
void drm_dp_remote_aux_init(struct drm_dp_aux *aux)
{
	INIT_WORK(&aux->crc_work, drm_dp_aux_crc_work);
}
EXPORT_SYMBOL(drm_dp_remote_aux_init);

/**
 * drm_dp_aux_init() - minimally initialise an aux channel
 * @aux: DisplayPort AUX channel
 *
 * If you need to use the drm_dp_aux's i2c adapter prior to registering it with
 * the outside world, call drm_dp_aux_init() first. For drivers which are
 * grandparents to their AUX adapters (e.g. the AUX adapter is parented by a
 * &drm_connector), you must still call drm_dp_aux_register() once the connector
 * has been registered to allow userspace access to the auxiliary DP channel.
 * Likewise, for such drivers you should also assign &drm_dp_aux.drm_dev as
 * early as possible so that the &drm_device that corresponds to the AUX adapter
 * may be mentioned in debugging output from the DRM DP helpers.
 *
 * For devices which use a separate platform device for their AUX adapters, this
 * may be called as early as required by the driver.
 *
 */
void drm_dp_aux_init(struct drm_dp_aux *aux)
{
	mutex_init(&aux->hw_mutex);
	mutex_init(&aux->cec.lock);
	INIT_WORK(&aux->crc_work, drm_dp_aux_crc_work);

	aux->ddc.algo = &drm_dp_i2c_algo;
	aux->ddc.algo_data = aux;
	aux->ddc.retries = 3;

	aux->ddc.lock_ops = &drm_dp_i2c_lock_ops;
}
EXPORT_SYMBOL(drm_dp_aux_init);

/**
 * drm_dp_aux_register() - initialise and register aux channel
 * @aux: DisplayPort AUX channel
 *
 * Automatically calls drm_dp_aux_init() if this hasn't been done yet. This
 * should only be called once the parent of @aux, &drm_dp_aux.dev, is
 * initialized. For devices which are grandparents of their AUX channels,
 * &drm_dp_aux.dev will typically be the &drm_connector &device which
 * corresponds to @aux. For these devices, it's advised to call
 * drm_dp_aux_register() in &drm_connector_funcs.late_register, and likewise to
 * call drm_dp_aux_unregister() in &drm_connector_funcs.early_unregister.
 * Functions which don't follow this will likely Oops when
 * %CONFIG_DRM_DP_AUX_CHARDEV is enabled.
 *
 * For devices where the AUX channel is a device that exists independently of
 * the &drm_device that uses it, such as SoCs and bridge devices, it is
 * recommended to call drm_dp_aux_register() after a &drm_device has been
 * assigned to &drm_dp_aux.drm_dev, and likewise to call
 * drm_dp_aux_unregister() once the &drm_device should no longer be associated
 * with the AUX channel (e.g. on bridge detach).
 *
 * Drivers which need to use the aux channel before either of the two points
 * mentioned above need to call drm_dp_aux_init() in order to use the AUX
 * channel before registration.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_aux_register(struct drm_dp_aux *aux)
{
	int ret;

	WARN_ON_ONCE(!aux->drm_dev);

	if (!aux->ddc.algo)
		drm_dp_aux_init(aux);

	aux->ddc.class = I2C_CLASS_DDC;
	aux->ddc.owner = THIS_MODULE;
	aux->ddc.dev.parent = aux->dev;

	strlcpy(aux->ddc.name, aux->name ? aux->name : dev_name(aux->dev),
		sizeof(aux->ddc.name));

	ret = drm_dp_aux_register_devnode(aux);
	if (ret)
		return ret;

	ret = i2c_add_adapter(&aux->ddc);
	if (ret) {
		drm_dp_aux_unregister_devnode(aux);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_aux_register);

/**
 * drm_dp_aux_unregister() - unregister an AUX adapter
 * @aux: DisplayPort AUX channel
 */
void drm_dp_aux_unregister(struct drm_dp_aux *aux)
{
	drm_dp_aux_unregister_devnode(aux);
	i2c_del_adapter(&aux->ddc);
}
EXPORT_SYMBOL(drm_dp_aux_unregister);

#define PSR_SETUP_TIME(x) [DP_PSR_SETUP_TIME_ ## x >> DP_PSR_SETUP_TIME_SHIFT] = (x)

/**
 * drm_dp_psr_setup_time() - PSR setup in time usec
 * @psr_cap: PSR capabilities from DPCD
 *
 * Returns:
 * PSR setup time for the panel in microseconds,  negative
 * error code on failure.
 */
int drm_dp_psr_setup_time(const u8 psr_cap[EDP_PSR_RECEIVER_CAP_SIZE])
{
	static const u16 psr_setup_time_us[] = {
		PSR_SETUP_TIME(330),
		PSR_SETUP_TIME(275),
		PSR_SETUP_TIME(220),
		PSR_SETUP_TIME(165),
		PSR_SETUP_TIME(110),
		PSR_SETUP_TIME(55),
		PSR_SETUP_TIME(0),
	};
	int i;

	i = (psr_cap[1] & DP_PSR_SETUP_TIME_MASK) >> DP_PSR_SETUP_TIME_SHIFT;
	if (i >= ARRAY_SIZE(psr_setup_time_us))
		return -EINVAL;

	return psr_setup_time_us[i];
}
EXPORT_SYMBOL(drm_dp_psr_setup_time);

#undef PSR_SETUP_TIME

/**
 * drm_dp_start_crc() - start capture of frame CRCs
 * @aux: DisplayPort AUX channel
 * @crtc: CRTC displaying the frames whose CRCs are to be captured
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_start_crc(struct drm_dp_aux *aux, struct drm_crtc *crtc)
{
	u8 buf;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_TEST_SINK, &buf);
	if (ret < 0)
		return ret;

	ret = drm_dp_dpcd_writeb(aux, DP_TEST_SINK, buf | DP_TEST_SINK_START);
	if (ret < 0)
		return ret;

	aux->crc_count = 0;
	aux->crtc = crtc;
	schedule_work(&aux->crc_work);

	return 0;
}
EXPORT_SYMBOL(drm_dp_start_crc);

/**
 * drm_dp_stop_crc() - stop capture of frame CRCs
 * @aux: DisplayPort AUX channel
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_stop_crc(struct drm_dp_aux *aux)
{
	u8 buf;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_TEST_SINK, &buf);
	if (ret < 0)
		return ret;

	ret = drm_dp_dpcd_writeb(aux, DP_TEST_SINK, buf & ~DP_TEST_SINK_START);
	if (ret < 0)
		return ret;

	flush_work(&aux->crc_work);
	aux->crtc = NULL;

	return 0;
}
EXPORT_SYMBOL(drm_dp_stop_crc);

struct dpcd_quirk {
	u8 oui[3];
	u8 device_id[6];
	bool is_branch;
	u32 quirks;
};

#define OUI(first, second, third) { (first), (second), (third) }
#define DEVICE_ID(first, second, third, fourth, fifth, sixth) \
	{ (first), (second), (third), (fourth), (fifth), (sixth) }

#define DEVICE_ID_ANY	DEVICE_ID(0, 0, 0, 0, 0, 0)

static const struct dpcd_quirk dpcd_quirk_list[] = {
	/* Analogix 7737 needs reduced M and N at HBR2 link rates */
	{ OUI(0x00, 0x22, 0xb9), DEVICE_ID_ANY, true, BIT(DP_DPCD_QUIRK_CONSTANT_N) },
	/* LG LP140WF6-SPM1 eDP panel */
	{ OUI(0x00, 0x22, 0xb9), DEVICE_ID('s', 'i', 'v', 'a', 'r', 'T'), false, BIT(DP_DPCD_QUIRK_CONSTANT_N) },
	/* Apple panels need some additional handling to support PSR */
	{ OUI(0x00, 0x10, 0xfa), DEVICE_ID_ANY, false, BIT(DP_DPCD_QUIRK_NO_PSR) },
	/* CH7511 seems to leave SINK_COUNT zeroed */
	{ OUI(0x00, 0x00, 0x00), DEVICE_ID('C', 'H', '7', '5', '1', '1'), false, BIT(DP_DPCD_QUIRK_NO_SINK_COUNT) },
	/* Synaptics DP1.4 MST hubs can support DSC without virtual DPCD */
	{ OUI(0x90, 0xCC, 0x24), DEVICE_ID_ANY, true, BIT(DP_DPCD_QUIRK_DSC_WITHOUT_VIRTUAL_DPCD) },
	/* Apple MacBookPro 2017 15 inch eDP Retina panel reports too low DP_MAX_LINK_RATE */
	{ OUI(0x00, 0x10, 0xfa), DEVICE_ID(101, 68, 21, 101, 98, 97), false, BIT(DP_DPCD_QUIRK_CAN_DO_MAX_LINK_RATE_3_24_GBPS) },
};

#undef OUI

/*
 * Get a bit mask of DPCD quirks for the sink/branch device identified by
 * ident. The quirk data is shared but it's up to the drivers to act on the
 * data.
 *
 * For now, only the OUI (first three bytes) is used, but this may be extended
 * to device identification string and hardware/firmware revisions later.
 */
static u32
drm_dp_get_quirks(const struct drm_dp_dpcd_ident *ident, bool is_branch)
{
	const struct dpcd_quirk *quirk;
	u32 quirks = 0;
	int i;
	u8 any_device[] = DEVICE_ID_ANY;

	for (i = 0; i < ARRAY_SIZE(dpcd_quirk_list); i++) {
		quirk = &dpcd_quirk_list[i];

		if (quirk->is_branch != is_branch)
			continue;

		if (memcmp(quirk->oui, ident->oui, sizeof(ident->oui)) != 0)
			continue;

		if (memcmp(quirk->device_id, any_device, sizeof(any_device)) != 0 &&
		    memcmp(quirk->device_id, ident->device_id, sizeof(ident->device_id)) != 0)
			continue;

		quirks |= quirk->quirks;
	}

	return quirks;
}

#undef DEVICE_ID_ANY
#undef DEVICE_ID

/**
 * drm_dp_read_desc - read sink/branch descriptor from DPCD
 * @aux: DisplayPort AUX channel
 * @desc: Device descriptor to fill from DPCD
 * @is_branch: true for branch devices, false for sink devices
 *
 * Read DPCD 0x400 (sink) or 0x500 (branch) into @desc. Also debug log the
 * identification.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_read_desc(struct drm_dp_aux *aux, struct drm_dp_desc *desc,
		     bool is_branch)
{
	struct drm_dp_dpcd_ident *ident = &desc->ident;
	unsigned int offset = is_branch ? DP_BRANCH_OUI : DP_SINK_OUI;
	int ret, dev_id_len;

	ret = drm_dp_dpcd_read(aux, offset, ident, sizeof(*ident));
	if (ret < 0)
		return ret;

	desc->quirks = drm_dp_get_quirks(ident, is_branch);

	dev_id_len = strnlen(ident->device_id, sizeof(ident->device_id));

	drm_dbg_kms(aux->drm_dev,
		    "%s: DP %s: OUI %*phD dev-ID %*pE HW-rev %d.%d SW-rev %d.%d quirks 0x%04x\n",
		    aux->name, is_branch ? "branch" : "sink",
		    (int)sizeof(ident->oui), ident->oui, dev_id_len,
		    ident->device_id, ident->hw_rev >> 4, ident->hw_rev & 0xf,
		    ident->sw_major_rev, ident->sw_minor_rev, desc->quirks);

	return 0;
}
EXPORT_SYMBOL(drm_dp_read_desc);

/**
 * drm_dp_dsc_sink_max_slice_count() - Get the max slice count
 * supported by the DSC sink.
 * @dsc_dpcd: DSC capabilities from DPCD
 * @is_edp: true if its eDP, false for DP
 *
 * Read the slice capabilities DPCD register from DSC sink to get
 * the maximum slice count supported. This is used to populate
 * the DSC parameters in the &struct drm_dsc_config by the driver.
 * Driver creates an infoframe using these parameters to populate
 * &struct drm_dsc_pps_infoframe. These are sent to the sink using DSC
 * infoframe using the helper function drm_dsc_pps_infoframe_pack()
 *
 * Returns:
 * Maximum slice count supported by DSC sink or 0 its invalid
 */
u8 drm_dp_dsc_sink_max_slice_count(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE],
				   bool is_edp)
{
	u8 slice_cap1 = dsc_dpcd[DP_DSC_SLICE_CAP_1 - DP_DSC_SUPPORT];

	if (is_edp) {
		/* For eDP, register DSC_SLICE_CAPABILITIES_1 gives slice count */
		if (slice_cap1 & DP_DSC_4_PER_DP_DSC_SINK)
			return 4;
		if (slice_cap1 & DP_DSC_2_PER_DP_DSC_SINK)
			return 2;
		if (slice_cap1 & DP_DSC_1_PER_DP_DSC_SINK)
			return 1;
	} else {
		/* For DP, use values from DSC_SLICE_CAP_1 and DSC_SLICE_CAP2 */
		u8 slice_cap2 = dsc_dpcd[DP_DSC_SLICE_CAP_2 - DP_DSC_SUPPORT];

		if (slice_cap2 & DP_DSC_24_PER_DP_DSC_SINK)
			return 24;
		if (slice_cap2 & DP_DSC_20_PER_DP_DSC_SINK)
			return 20;
		if (slice_cap2 & DP_DSC_16_PER_DP_DSC_SINK)
			return 16;
		if (slice_cap1 & DP_DSC_12_PER_DP_DSC_SINK)
			return 12;
		if (slice_cap1 & DP_DSC_10_PER_DP_DSC_SINK)
			return 10;
		if (slice_cap1 & DP_DSC_8_PER_DP_DSC_SINK)
			return 8;
		if (slice_cap1 & DP_DSC_6_PER_DP_DSC_SINK)
			return 6;
		if (slice_cap1 & DP_DSC_4_PER_DP_DSC_SINK)
			return 4;
		if (slice_cap1 & DP_DSC_2_PER_DP_DSC_SINK)
			return 2;
		if (slice_cap1 & DP_DSC_1_PER_DP_DSC_SINK)
			return 1;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_dsc_sink_max_slice_count);

/**
 * drm_dp_dsc_sink_line_buf_depth() - Get the line buffer depth in bits
 * @dsc_dpcd: DSC capabilities from DPCD
 *
 * Read the DSC DPCD register to parse the line buffer depth in bits which is
 * number of bits of precision within the decoder line buffer supported by
 * the DSC sink. This is used to populate the DSC parameters in the
 * &struct drm_dsc_config by the driver.
 * Driver creates an infoframe using these parameters to populate
 * &struct drm_dsc_pps_infoframe. These are sent to the sink using DSC
 * infoframe using the helper function drm_dsc_pps_infoframe_pack()
 *
 * Returns:
 * Line buffer depth supported by DSC panel or 0 its invalid
 */
u8 drm_dp_dsc_sink_line_buf_depth(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE])
{
	u8 line_buf_depth = dsc_dpcd[DP_DSC_LINE_BUF_BIT_DEPTH - DP_DSC_SUPPORT];

	switch (line_buf_depth & DP_DSC_LINE_BUF_BIT_DEPTH_MASK) {
	case DP_DSC_LINE_BUF_BIT_DEPTH_9:
		return 9;
	case DP_DSC_LINE_BUF_BIT_DEPTH_10:
		return 10;
	case DP_DSC_LINE_BUF_BIT_DEPTH_11:
		return 11;
	case DP_DSC_LINE_BUF_BIT_DEPTH_12:
		return 12;
	case DP_DSC_LINE_BUF_BIT_DEPTH_13:
		return 13;
	case DP_DSC_LINE_BUF_BIT_DEPTH_14:
		return 14;
	case DP_DSC_LINE_BUF_BIT_DEPTH_15:
		return 15;
	case DP_DSC_LINE_BUF_BIT_DEPTH_16:
		return 16;
	case DP_DSC_LINE_BUF_BIT_DEPTH_8:
		return 8;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_dsc_sink_line_buf_depth);

/**
 * drm_dp_dsc_sink_supported_input_bpcs() - Get all the input bits per component
 * values supported by the DSC sink.
 * @dsc_dpcd: DSC capabilities from DPCD
 * @dsc_bpc: An array to be filled by this helper with supported
 *           input bpcs.
 *
 * Read the DSC DPCD from the sink device to parse the supported bits per
 * component values. This is used to populate the DSC parameters
 * in the &struct drm_dsc_config by the driver.
 * Driver creates an infoframe using these parameters to populate
 * &struct drm_dsc_pps_infoframe. These are sent to the sink using DSC
 * infoframe using the helper function drm_dsc_pps_infoframe_pack()
 *
 * Returns:
 * Number of input BPC values parsed from the DPCD
 */
int drm_dp_dsc_sink_supported_input_bpcs(const u8 dsc_dpcd[DP_DSC_RECEIVER_CAP_SIZE],
					 u8 dsc_bpc[3])
{
	int num_bpc = 0;
	u8 color_depth = dsc_dpcd[DP_DSC_DEC_COLOR_DEPTH_CAP - DP_DSC_SUPPORT];

	if (color_depth & DP_DSC_12_BPC)
		dsc_bpc[num_bpc++] = 12;
	if (color_depth & DP_DSC_10_BPC)
		dsc_bpc[num_bpc++] = 10;
	if (color_depth & DP_DSC_8_BPC)
		dsc_bpc[num_bpc++] = 8;

	return num_bpc;
}
EXPORT_SYMBOL(drm_dp_dsc_sink_supported_input_bpcs);

/**
 * drm_dp_read_lttpr_common_caps - read the LTTPR common capabilities
 * @aux: DisplayPort AUX channel
 * @caps: buffer to return the capability info in
 *
 * Read capabilities common to all LTTPRs.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_read_lttpr_common_caps(struct drm_dp_aux *aux,
				  u8 caps[DP_LTTPR_COMMON_CAP_SIZE])
{
	int ret;

	ret = drm_dp_dpcd_read(aux,
			       DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV,
			       caps, DP_LTTPR_COMMON_CAP_SIZE);
	if (ret < 0)
		return ret;

	WARN_ON(ret != DP_LTTPR_COMMON_CAP_SIZE);

	return 0;
}
EXPORT_SYMBOL(drm_dp_read_lttpr_common_caps);

/**
 * drm_dp_read_lttpr_phy_caps - read the capabilities for a given LTTPR PHY
 * @aux: DisplayPort AUX channel
 * @dp_phy: LTTPR PHY to read the capabilities for
 * @caps: buffer to return the capability info in
 *
 * Read the capabilities for the given LTTPR PHY.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_read_lttpr_phy_caps(struct drm_dp_aux *aux,
			       enum drm_dp_phy dp_phy,
			       u8 caps[DP_LTTPR_PHY_CAP_SIZE])
{
	int ret;

	ret = drm_dp_dpcd_read(aux,
			       DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER(dp_phy),
			       caps, DP_LTTPR_PHY_CAP_SIZE);
	if (ret < 0)
		return ret;

	WARN_ON(ret != DP_LTTPR_PHY_CAP_SIZE);

	return 0;
}
EXPORT_SYMBOL(drm_dp_read_lttpr_phy_caps);

static u8 dp_lttpr_common_cap(const u8 caps[DP_LTTPR_COMMON_CAP_SIZE], int r)
{
	return caps[r - DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];
}

/**
 * drm_dp_lttpr_count - get the number of detected LTTPRs
 * @caps: LTTPR common capabilities
 *
 * Get the number of detected LTTPRs from the LTTPR common capabilities info.
 *
 * Returns:
 *   -ERANGE if more than supported number (8) of LTTPRs are detected
 *   -EINVAL if the DP_PHY_REPEATER_CNT register contains an invalid value
 *   otherwise the number of detected LTTPRs
 */
int drm_dp_lttpr_count(const u8 caps[DP_LTTPR_COMMON_CAP_SIZE])
{
	u8 count = dp_lttpr_common_cap(caps, DP_PHY_REPEATER_CNT);

	switch (hweight8(count)) {
	case 0:
		return 0;
	case 1:
		return 8 - ilog2(count);
	case 8:
		return -ERANGE;
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(drm_dp_lttpr_count);

/**
 * drm_dp_lttpr_max_link_rate - get the maximum link rate supported by all LTTPRs
 * @caps: LTTPR common capabilities
 *
 * Returns the maximum link rate supported by all detected LTTPRs.
 */
int drm_dp_lttpr_max_link_rate(const u8 caps[DP_LTTPR_COMMON_CAP_SIZE])
{
	u8 rate = dp_lttpr_common_cap(caps, DP_MAX_LINK_RATE_PHY_REPEATER);

	return drm_dp_bw_code_to_link_rate(rate);
}
EXPORT_SYMBOL(drm_dp_lttpr_max_link_rate);

/**
 * drm_dp_lttpr_max_lane_count - get the maximum lane count supported by all LTTPRs
 * @caps: LTTPR common capabilities
 *
 * Returns the maximum lane count supported by all detected LTTPRs.
 */
int drm_dp_lttpr_max_lane_count(const u8 caps[DP_LTTPR_COMMON_CAP_SIZE])
{
	u8 max_lanes = dp_lttpr_common_cap(caps, DP_MAX_LANE_COUNT_PHY_REPEATER);

	return max_lanes & DP_MAX_LANE_COUNT_MASK;
}
EXPORT_SYMBOL(drm_dp_lttpr_max_lane_count);

/**
 * drm_dp_lttpr_voltage_swing_level_3_supported - check for LTTPR vswing3 support
 * @caps: LTTPR PHY capabilities
 *
 * Returns true if the @caps for an LTTPR TX PHY indicate support for
 * voltage swing level 3.
 */
bool
drm_dp_lttpr_voltage_swing_level_3_supported(const u8 caps[DP_LTTPR_PHY_CAP_SIZE])
{
	u8 txcap = dp_lttpr_phy_cap(caps, DP_TRANSMITTER_CAPABILITY_PHY_REPEATER1);

	return txcap & DP_VOLTAGE_SWING_LEVEL_3_SUPPORTED;
}
EXPORT_SYMBOL(drm_dp_lttpr_voltage_swing_level_3_supported);

/**
 * drm_dp_lttpr_pre_emphasis_level_3_supported - check for LTTPR preemph3 support
 * @caps: LTTPR PHY capabilities
 *
 * Returns true if the @caps for an LTTPR TX PHY indicate support for
 * pre-emphasis level 3.
 */
bool
drm_dp_lttpr_pre_emphasis_level_3_supported(const u8 caps[DP_LTTPR_PHY_CAP_SIZE])
{
	u8 txcap = dp_lttpr_phy_cap(caps, DP_TRANSMITTER_CAPABILITY_PHY_REPEATER1);

	return txcap & DP_PRE_EMPHASIS_LEVEL_3_SUPPORTED;
}
EXPORT_SYMBOL(drm_dp_lttpr_pre_emphasis_level_3_supported);

/**
 * drm_dp_get_phy_test_pattern() - get the requested pattern from the sink.
 * @aux: DisplayPort AUX channel
 * @data: DP phy compliance test parameters.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_get_phy_test_pattern(struct drm_dp_aux *aux,
				struct drm_dp_phy_test_params *data)
{
	int err;
	u8 rate, lanes;

	err = drm_dp_dpcd_readb(aux, DP_TEST_LINK_RATE, &rate);
	if (err < 0)
		return err;
	data->link_rate = drm_dp_bw_code_to_link_rate(rate);

	err = drm_dp_dpcd_readb(aux, DP_TEST_LANE_COUNT, &lanes);
	if (err < 0)
		return err;
	data->num_lanes = lanes & DP_MAX_LANE_COUNT_MASK;

	if (lanes & DP_ENHANCED_FRAME_CAP)
		data->enhanced_frame_cap = true;

	err = drm_dp_dpcd_readb(aux, DP_PHY_TEST_PATTERN, &data->phy_pattern);
	if (err < 0)
		return err;

	switch (data->phy_pattern) {
	case DP_PHY_TEST_PATTERN_80BIT_CUSTOM:
		err = drm_dp_dpcd_read(aux, DP_TEST_80BIT_CUSTOM_PATTERN_7_0,
				       &data->custom80, sizeof(data->custom80));
		if (err < 0)
			return err;

		break;
	case DP_PHY_TEST_PATTERN_CP2520:
		err = drm_dp_dpcd_read(aux, DP_TEST_HBR2_SCRAMBLER_RESET,
				       &data->hbr2_reset,
				       sizeof(data->hbr2_reset));
		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_get_phy_test_pattern);

/**
 * drm_dp_set_phy_test_pattern() - set the pattern to the sink.
 * @aux: DisplayPort AUX channel
 * @data: DP phy compliance test parameters.
 * @dp_rev: DP revision to use for compliance testing
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_set_phy_test_pattern(struct drm_dp_aux *aux,
				struct drm_dp_phy_test_params *data, u8 dp_rev)
{
	int err, i;
	u8 link_config[2];
	u8 test_pattern;

	link_config[0] = drm_dp_link_rate_to_bw_code(data->link_rate);
	link_config[1] = data->num_lanes;
	if (data->enhanced_frame_cap)
		link_config[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;
	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, link_config, 2);
	if (err < 0)
		return err;

	test_pattern = data->phy_pattern;
	if (dp_rev < 0x12) {
		test_pattern = (test_pattern << 2) &
			       DP_LINK_QUAL_PATTERN_11_MASK;
		err = drm_dp_dpcd_writeb(aux, DP_TRAINING_PATTERN_SET,
					 test_pattern);
		if (err < 0)
			return err;
	} else {
		for (i = 0; i < data->num_lanes; i++) {
			err = drm_dp_dpcd_writeb(aux,
						 DP_LINK_QUAL_LANE0_SET + i,
						 test_pattern);
			if (err < 0)
				return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_set_phy_test_pattern);

static const char *dp_pixelformat_get_name(enum dp_pixelformat pixelformat)
{
	if (pixelformat < 0 || pixelformat > DP_PIXELFORMAT_RESERVED)
		return "Invalid";

	switch (pixelformat) {
	case DP_PIXELFORMAT_RGB:
		return "RGB";
	case DP_PIXELFORMAT_YUV444:
		return "YUV444";
	case DP_PIXELFORMAT_YUV422:
		return "YUV422";
	case DP_PIXELFORMAT_YUV420:
		return "YUV420";
	case DP_PIXELFORMAT_Y_ONLY:
		return "Y_ONLY";
	case DP_PIXELFORMAT_RAW:
		return "RAW";
	default:
		return "Reserved";
	}
}

static const char *dp_colorimetry_get_name(enum dp_pixelformat pixelformat,
					   enum dp_colorimetry colorimetry)
{
	if (pixelformat < 0 || pixelformat > DP_PIXELFORMAT_RESERVED)
		return "Invalid";

	switch (colorimetry) {
	case DP_COLORIMETRY_DEFAULT:
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "sRGB";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.601";
		case DP_PIXELFORMAT_Y_ONLY:
			return "DICOM PS3.14";
		case DP_PIXELFORMAT_RAW:
			return "Custom Color Profile";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_RGB_WIDE_FIXED: /* and DP_COLORIMETRY_BT709_YCC */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "Wide Fixed";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.709";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_RGB_WIDE_FLOAT: /* and DP_COLORIMETRY_XVYCC_601 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "Wide Float";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "xvYCC 601";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_OPRGB: /* and DP_COLORIMETRY_XVYCC_709 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "OpRGB";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "xvYCC 709";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_DCI_P3_RGB: /* and DP_COLORIMETRY_SYCC_601 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "DCI-P3";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "sYCC 601";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_RGB_CUSTOM: /* and DP_COLORIMETRY_OPYCC_601 */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "Custom Profile";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "OpYCC 601";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_BT2020_RGB: /* and DP_COLORIMETRY_BT2020_CYCC */
		switch (pixelformat) {
		case DP_PIXELFORMAT_RGB:
			return "BT.2020 RGB";
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.2020 CYCC";
		default:
			return "Reserved";
		}
	case DP_COLORIMETRY_BT2020_YCC:
		switch (pixelformat) {
		case DP_PIXELFORMAT_YUV444:
		case DP_PIXELFORMAT_YUV422:
		case DP_PIXELFORMAT_YUV420:
			return "BT.2020 YCC";
		default:
			return "Reserved";
		}
	default:
		return "Invalid";
	}
}

static const char *dp_dynamic_range_get_name(enum dp_dynamic_range dynamic_range)
{
	switch (dynamic_range) {
	case DP_DYNAMIC_RANGE_VESA:
		return "VESA range";
	case DP_DYNAMIC_RANGE_CTA:
		return "CTA range";
	default:
		return "Invalid";
	}
}

static const char *dp_content_type_get_name(enum dp_content_type content_type)
{
	switch (content_type) {
	case DP_CONTENT_TYPE_NOT_DEFINED:
		return "Not defined";
	case DP_CONTENT_TYPE_GRAPHICS:
		return "Graphics";
	case DP_CONTENT_TYPE_PHOTO:
		return "Photo";
	case DP_CONTENT_TYPE_VIDEO:
		return "Video";
	case DP_CONTENT_TYPE_GAME:
		return "Game";
	default:
		return "Reserved";
	}
}

void drm_dp_vsc_sdp_log(const char *level, struct device *dev,
			const struct drm_dp_vsc_sdp *vsc)
{
#define DP_SDP_LOG(fmt, ...) dev_printk(level, dev, fmt, ##__VA_ARGS__)
	DP_SDP_LOG("DP SDP: %s, revision %u, length %u\n", "VSC",
		   vsc->revision, vsc->length);
	DP_SDP_LOG("    pixelformat: %s\n",
		   dp_pixelformat_get_name(vsc->pixelformat));
	DP_SDP_LOG("    colorimetry: %s\n",
		   dp_colorimetry_get_name(vsc->pixelformat, vsc->colorimetry));
	DP_SDP_LOG("    bpc: %u\n", vsc->bpc);
	DP_SDP_LOG("    dynamic range: %s\n",
		   dp_dynamic_range_get_name(vsc->dynamic_range));
	DP_SDP_LOG("    content type: %s\n",
		   dp_content_type_get_name(vsc->content_type));
#undef DP_SDP_LOG
}
EXPORT_SYMBOL(drm_dp_vsc_sdp_log);

/**
 * drm_dp_get_pcon_max_frl_bw() - maximum frl supported by PCON
 * @dpcd: DisplayPort configuration data
 * @port_cap: port capabilities
 *
 * Returns maximum frl bandwidth supported by PCON in GBPS,
 * returns 0 if not supported.
 */
int drm_dp_get_pcon_max_frl_bw(const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			       const u8 port_cap[4])
{
	int bw;
	u8 buf;

	buf = port_cap[2];
	bw = buf & DP_PCON_MAX_FRL_BW;

	switch (bw) {
	case DP_PCON_MAX_9GBPS:
		return 9;
	case DP_PCON_MAX_18GBPS:
		return 18;
	case DP_PCON_MAX_24GBPS:
		return 24;
	case DP_PCON_MAX_32GBPS:
		return 32;
	case DP_PCON_MAX_40GBPS:
		return 40;
	case DP_PCON_MAX_48GBPS:
		return 48;
	case DP_PCON_MAX_0GBPS:
	default:
		return 0;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_get_pcon_max_frl_bw);

/**
 * drm_dp_pcon_frl_prepare() - Prepare PCON for FRL.
 * @aux: DisplayPort AUX channel
 * @enable_frl_ready_hpd: Configure DP_PCON_ENABLE_HPD_READY.
 *
 * Returns 0 if success, else returns negative error code.
 */
int drm_dp_pcon_frl_prepare(struct drm_dp_aux *aux, bool enable_frl_ready_hpd)
{
	int ret;
	u8 buf = DP_PCON_ENABLE_SOURCE_CTL_MODE |
		 DP_PCON_ENABLE_LINK_FRL_MODE;

	if (enable_frl_ready_hpd)
		buf |= DP_PCON_ENABLE_HPD_READY;

	ret = drm_dp_dpcd_writeb(aux, DP_PCON_HDMI_LINK_CONFIG_1, buf);

	return ret;
}
EXPORT_SYMBOL(drm_dp_pcon_frl_prepare);

/**
 * drm_dp_pcon_is_frl_ready() - Is PCON ready for FRL
 * @aux: DisplayPort AUX channel
 *
 * Returns true if success, else returns false.
 */
bool drm_dp_pcon_is_frl_ready(struct drm_dp_aux *aux)
{
	int ret;
	u8 buf;

	ret = drm_dp_dpcd_readb(aux, DP_PCON_HDMI_TX_LINK_STATUS, &buf);
	if (ret < 0)
		return false;

	if (buf & DP_PCON_FRL_READY)
		return true;

	return false;
}
EXPORT_SYMBOL(drm_dp_pcon_is_frl_ready);

/**
 * drm_dp_pcon_frl_configure_1() - Set HDMI LINK Configuration-Step1
 * @aux: DisplayPort AUX channel
 * @max_frl_gbps: maximum frl bw to be configured between PCON and HDMI sink
 * @frl_mode: FRL Training mode, it can be either Concurrent or Sequential.
 * In Concurrent Mode, the FRL link bring up can be done along with
 * DP Link training. In Sequential mode, the FRL link bring up is done prior to
 * the DP Link training.
 *
 * Returns 0 if success, else returns negative error code.
 */

int drm_dp_pcon_frl_configure_1(struct drm_dp_aux *aux, int max_frl_gbps,
				u8 frl_mode)
{
	int ret;
	u8 buf;

	ret = drm_dp_dpcd_readb(aux, DP_PCON_HDMI_LINK_CONFIG_1, &buf);
	if (ret < 0)
		return ret;

	if (frl_mode == DP_PCON_ENABLE_CONCURRENT_LINK)
		buf |= DP_PCON_ENABLE_CONCURRENT_LINK;
	else
		buf &= ~DP_PCON_ENABLE_CONCURRENT_LINK;

	switch (max_frl_gbps) {
	case 9:
		buf |=  DP_PCON_ENABLE_MAX_BW_9GBPS;
		break;
	case 18:
		buf |=  DP_PCON_ENABLE_MAX_BW_18GBPS;
		break;
	case 24:
		buf |=  DP_PCON_ENABLE_MAX_BW_24GBPS;
		break;
	case 32:
		buf |=  DP_PCON_ENABLE_MAX_BW_32GBPS;
		break;
	case 40:
		buf |=  DP_PCON_ENABLE_MAX_BW_40GBPS;
		break;
	case 48:
		buf |=  DP_PCON_ENABLE_MAX_BW_48GBPS;
		break;
	case 0:
		buf |=  DP_PCON_ENABLE_MAX_BW_0GBPS;
		break;
	default:
		return -EINVAL;
	}

	ret = drm_dp_dpcd_writeb(aux, DP_PCON_HDMI_LINK_CONFIG_1, buf);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_dp_pcon_frl_configure_1);

/**
 * drm_dp_pcon_frl_configure_2() - Set HDMI Link configuration Step-2
 * @aux: DisplayPort AUX channel
 * @max_frl_mask : Max FRL BW to be tried by the PCON with HDMI Sink
 * @frl_type : FRL training type, can be Extended, or Normal.
 * In Normal FRL training, the PCON tries each frl bw from the max_frl_mask
 * starting from min, and stops when link training is successful. In Extended
 * FRL training, all frl bw selected in the mask are trained by the PCON.
 *
 * Returns 0 if success, else returns negative error code.
 */
int drm_dp_pcon_frl_configure_2(struct drm_dp_aux *aux, int max_frl_mask,
				u8 frl_type)
{
	int ret;
	u8 buf = max_frl_mask;

	if (frl_type == DP_PCON_FRL_LINK_TRAIN_EXTENDED)
		buf |= DP_PCON_FRL_LINK_TRAIN_EXTENDED;
	else
		buf &= ~DP_PCON_FRL_LINK_TRAIN_EXTENDED;

	ret = drm_dp_dpcd_writeb(aux, DP_PCON_HDMI_LINK_CONFIG_2, buf);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_dp_pcon_frl_configure_2);

/**
 * drm_dp_pcon_reset_frl_config() - Re-Set HDMI Link configuration.
 * @aux: DisplayPort AUX channel
 *
 * Returns 0 if success, else returns negative error code.
 */
int drm_dp_pcon_reset_frl_config(struct drm_dp_aux *aux)
{
	int ret;

	ret = drm_dp_dpcd_writeb(aux, DP_PCON_HDMI_LINK_CONFIG_1, 0x0);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_dp_pcon_reset_frl_config);

/**
 * drm_dp_pcon_frl_enable() - Enable HDMI link through FRL
 * @aux: DisplayPort AUX channel
 *
 * Returns 0 if success, else returns negative error code.
 */
int drm_dp_pcon_frl_enable(struct drm_dp_aux *aux)
{
	int ret;
	u8 buf = 0;

	ret = drm_dp_dpcd_readb(aux, DP_PCON_HDMI_LINK_CONFIG_1, &buf);
	if (ret < 0)
		return ret;
	if (!(buf & DP_PCON_ENABLE_SOURCE_CTL_MODE)) {
		drm_dbg_kms(aux->drm_dev, "%s: PCON in Autonomous mode, can't enable FRL\n",
			    aux->name);
		return -EINVAL;
	}
	buf |= DP_PCON_ENABLE_HDMI_LINK;
	ret = drm_dp_dpcd_writeb(aux, DP_PCON_HDMI_LINK_CONFIG_1, buf);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_dp_pcon_frl_enable);

/**
 * drm_dp_pcon_hdmi_link_active() - check if the PCON HDMI LINK status is active.
 * @aux: DisplayPort AUX channel
 *
 * Returns true if link is active else returns false.
 */
bool drm_dp_pcon_hdmi_link_active(struct drm_dp_aux *aux)
{
	u8 buf;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_PCON_HDMI_TX_LINK_STATUS, &buf);
	if (ret < 0)
		return false;

	return buf & DP_PCON_HDMI_TX_LINK_ACTIVE;
}
EXPORT_SYMBOL(drm_dp_pcon_hdmi_link_active);

/**
 * drm_dp_pcon_hdmi_link_mode() - get the PCON HDMI LINK MODE
 * @aux: DisplayPort AUX channel
 * @frl_trained_mask: pointer to store bitmask of the trained bw configuration.
 * Valid only if the MODE returned is FRL. For Normal Link training mode
 * only 1 of the bits will be set, but in case of Extended mode, more than
 * one bits can be set.
 *
 * Returns the link mode : TMDS or FRL on success, else returns negative error
 * code.
 */
int drm_dp_pcon_hdmi_link_mode(struct drm_dp_aux *aux, u8 *frl_trained_mask)
{
	u8 buf;
	int mode;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_PCON_HDMI_POST_FRL_STATUS, &buf);
	if (ret < 0)
		return ret;

	mode = buf & DP_PCON_HDMI_LINK_MODE;

	if (frl_trained_mask && DP_PCON_HDMI_MODE_FRL == mode)
		*frl_trained_mask = (buf & DP_PCON_HDMI_FRL_TRAINED_BW) >> 1;

	return mode;
}
EXPORT_SYMBOL(drm_dp_pcon_hdmi_link_mode);

/**
 * drm_dp_pcon_hdmi_frl_link_error_count() - print the error count per lane
 * during link failure between PCON and HDMI sink
 * @aux: DisplayPort AUX channel
 * @connector: DRM connector
 * code.
 **/

void drm_dp_pcon_hdmi_frl_link_error_count(struct drm_dp_aux *aux,
					   struct drm_connector *connector)
{
	u8 buf, error_count;
	int i, num_error;
	struct drm_hdmi_info *hdmi = &connector->display_info.hdmi;

	for (i = 0; i < hdmi->max_lanes; i++) {
		if (drm_dp_dpcd_readb(aux, DP_PCON_HDMI_ERROR_STATUS_LN0 + i, &buf) < 0)
			return;

		error_count = buf & DP_PCON_HDMI_ERROR_COUNT_MASK;
		switch (error_count) {
		case DP_PCON_HDMI_ERROR_COUNT_HUNDRED_PLUS:
			num_error = 100;
			break;
		case DP_PCON_HDMI_ERROR_COUNT_TEN_PLUS:
			num_error = 10;
			break;
		case DP_PCON_HDMI_ERROR_COUNT_THREE_PLUS:
			num_error = 3;
			break;
		default:
			num_error = 0;
		}

		drm_err(aux->drm_dev, "%s: More than %d errors since the last read for lane %d",
			aux->name, num_error, i);
	}
}
EXPORT_SYMBOL(drm_dp_pcon_hdmi_frl_link_error_count);

/*
 * drm_dp_pcon_enc_is_dsc_1_2 - Does PCON Encoder supports DSC 1.2
 * @pcon_dsc_dpcd: DSC capabilities of the PCON DSC Encoder
 *
 * Returns true is PCON encoder is DSC 1.2 else returns false.
 */
bool drm_dp_pcon_enc_is_dsc_1_2(const u8 pcon_dsc_dpcd[DP_PCON_DSC_ENCODER_CAP_SIZE])
{
	u8 buf;
	u8 major_v, minor_v;

	buf = pcon_dsc_dpcd[DP_PCON_DSC_VERSION - DP_PCON_DSC_ENCODER];
	major_v = (buf & DP_PCON_DSC_MAJOR_MASK) >> DP_PCON_DSC_MAJOR_SHIFT;
	minor_v = (buf & DP_PCON_DSC_MINOR_MASK) >> DP_PCON_DSC_MINOR_SHIFT;

	if (major_v == 1 && minor_v == 2)
		return true;

	return false;
}
EXPORT_SYMBOL(drm_dp_pcon_enc_is_dsc_1_2);

/*
 * drm_dp_pcon_dsc_max_slices - Get max slices supported by PCON DSC Encoder
 * @pcon_dsc_dpcd: DSC capabilities of the PCON DSC Encoder
 *
 * Returns maximum no. of slices supported by the PCON DSC Encoder.
 */
int drm_dp_pcon_dsc_max_slices(const u8 pcon_dsc_dpcd[DP_PCON_DSC_ENCODER_CAP_SIZE])
{
	u8 slice_cap1, slice_cap2;

	slice_cap1 = pcon_dsc_dpcd[DP_PCON_DSC_SLICE_CAP_1 - DP_PCON_DSC_ENCODER];
	slice_cap2 = pcon_dsc_dpcd[DP_PCON_DSC_SLICE_CAP_2 - DP_PCON_DSC_ENCODER];

	if (slice_cap2 & DP_PCON_DSC_24_PER_DSC_ENC)
		return 24;
	if (slice_cap2 & DP_PCON_DSC_20_PER_DSC_ENC)
		return 20;
	if (slice_cap2 & DP_PCON_DSC_16_PER_DSC_ENC)
		return 16;
	if (slice_cap1 & DP_PCON_DSC_12_PER_DSC_ENC)
		return 12;
	if (slice_cap1 & DP_PCON_DSC_10_PER_DSC_ENC)
		return 10;
	if (slice_cap1 & DP_PCON_DSC_8_PER_DSC_ENC)
		return 8;
	if (slice_cap1 & DP_PCON_DSC_6_PER_DSC_ENC)
		return 6;
	if (slice_cap1 & DP_PCON_DSC_4_PER_DSC_ENC)
		return 4;
	if (slice_cap1 & DP_PCON_DSC_2_PER_DSC_ENC)
		return 2;
	if (slice_cap1 & DP_PCON_DSC_1_PER_DSC_ENC)
		return 1;

	return 0;
}
EXPORT_SYMBOL(drm_dp_pcon_dsc_max_slices);

/*
 * drm_dp_pcon_dsc_max_slice_width() - Get max slice width for Pcon DSC encoder
 * @pcon_dsc_dpcd: DSC capabilities of the PCON DSC Encoder
 *
 * Returns maximum width of the slices in pixel width i.e. no. of pixels x 320.
 */
int drm_dp_pcon_dsc_max_slice_width(const u8 pcon_dsc_dpcd[DP_PCON_DSC_ENCODER_CAP_SIZE])
{
	u8 buf;

	buf = pcon_dsc_dpcd[DP_PCON_DSC_MAX_SLICE_WIDTH - DP_PCON_DSC_ENCODER];

	return buf * DP_DSC_SLICE_WIDTH_MULTIPLIER;
}
EXPORT_SYMBOL(drm_dp_pcon_dsc_max_slice_width);

/*
 * drm_dp_pcon_dsc_bpp_incr() - Get bits per pixel increment for PCON DSC encoder
 * @pcon_dsc_dpcd: DSC capabilities of the PCON DSC Encoder
 *
 * Returns the bpp precision supported by the PCON encoder.
 */
int drm_dp_pcon_dsc_bpp_incr(const u8 pcon_dsc_dpcd[DP_PCON_DSC_ENCODER_CAP_SIZE])
{
	u8 buf;

	buf = pcon_dsc_dpcd[DP_PCON_DSC_BPP_INCR - DP_PCON_DSC_ENCODER];

	switch (buf & DP_PCON_DSC_BPP_INCR_MASK) {
	case DP_PCON_DSC_ONE_16TH_BPP:
		return 16;
	case DP_PCON_DSC_ONE_8TH_BPP:
		return 8;
	case DP_PCON_DSC_ONE_4TH_BPP:
		return 4;
	case DP_PCON_DSC_ONE_HALF_BPP:
		return 2;
	case DP_PCON_DSC_ONE_BPP:
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(drm_dp_pcon_dsc_bpp_incr);

static
int drm_dp_pcon_configure_dsc_enc(struct drm_dp_aux *aux, u8 pps_buf_config)
{
	u8 buf;
	int ret;

	ret = drm_dp_dpcd_readb(aux, DP_PROTOCOL_CONVERTER_CONTROL_2, &buf);
	if (ret < 0)
		return ret;

	buf |= DP_PCON_ENABLE_DSC_ENCODER;

	if (pps_buf_config <= DP_PCON_ENC_PPS_OVERRIDE_EN_BUFFER) {
		buf &= ~DP_PCON_ENCODER_PPS_OVERRIDE_MASK;
		buf |= pps_buf_config << 2;
	}

	ret = drm_dp_dpcd_writeb(aux, DP_PROTOCOL_CONVERTER_CONTROL_2, buf);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * drm_dp_pcon_pps_default() - Let PCON fill the default pps parameters
 * for DSC1.2 between PCON & HDMI2.1 sink
 * @aux: DisplayPort AUX channel
 *
 * Returns 0 on success, else returns negative error code.
 */
int drm_dp_pcon_pps_default(struct drm_dp_aux *aux)
{
	int ret;

	ret = drm_dp_pcon_configure_dsc_enc(aux, DP_PCON_ENC_PPS_OVERRIDE_DISABLED);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_dp_pcon_pps_default);

/**
 * drm_dp_pcon_pps_override_buf() - Configure PPS encoder override buffer for
 * HDMI sink
 * @aux: DisplayPort AUX channel
 * @pps_buf: 128 bytes to be written into PPS buffer for HDMI sink by PCON.
 *
 * Returns 0 on success, else returns negative error code.
 */
int drm_dp_pcon_pps_override_buf(struct drm_dp_aux *aux, u8 pps_buf[128])
{
	int ret;

	ret = drm_dp_dpcd_write(aux, DP_PCON_HDMI_PPS_OVERRIDE_BASE, &pps_buf, 128);
	if (ret < 0)
		return ret;

	ret = drm_dp_pcon_configure_dsc_enc(aux, DP_PCON_ENC_PPS_OVERRIDE_EN_BUFFER);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_dp_pcon_pps_override_buf);

/*
 * drm_dp_pcon_pps_override_param() - Write PPS parameters to DSC encoder
 * override registers
 * @aux: DisplayPort AUX channel
 * @pps_param: 3 Parameters (2 Bytes each) : Slice Width, Slice Height,
 * bits_per_pixel.
 *
 * Returns 0 on success, else returns negative error code.
 */
int drm_dp_pcon_pps_override_param(struct drm_dp_aux *aux, u8 pps_param[6])
{
	int ret;

	ret = drm_dp_dpcd_write(aux, DP_PCON_HDMI_PPS_OVRD_SLICE_HEIGHT, &pps_param[0], 2);
	if (ret < 0)
		return ret;
	ret = drm_dp_dpcd_write(aux, DP_PCON_HDMI_PPS_OVRD_SLICE_WIDTH, &pps_param[2], 2);
	if (ret < 0)
		return ret;
	ret = drm_dp_dpcd_write(aux, DP_PCON_HDMI_PPS_OVRD_BPP, &pps_param[4], 2);
	if (ret < 0)
		return ret;

	ret = drm_dp_pcon_configure_dsc_enc(aux, DP_PCON_ENC_PPS_OVERRIDE_EN_BUFFER);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_dp_pcon_pps_override_param);

/*
 * drm_dp_pcon_convert_rgb_to_ycbcr() - Configure the PCon to convert RGB to Ycbcr
 * @aux: displayPort AUX channel
 * @color_spc: Color-space/s for which conversion is to be enabled, 0 for disable.
 *
 * Returns 0 on success, else returns negative error code.
 */
int drm_dp_pcon_convert_rgb_to_ycbcr(struct drm_dp_aux *aux, u8 color_spc)
{
	int ret;
	u8 buf;

	ret = drm_dp_dpcd_readb(aux, DP_PROTOCOL_CONVERTER_CONTROL_2, &buf);
	if (ret < 0)
		return ret;

	if (color_spc & DP_CONVERSION_RGB_YCBCR_MASK)
		buf |= (color_spc & DP_CONVERSION_RGB_YCBCR_MASK);
	else
		buf &= ~DP_CONVERSION_RGB_YCBCR_MASK;

	ret = drm_dp_dpcd_writeb(aux, DP_PROTOCOL_CONVERTER_CONTROL_2, buf);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_dp_pcon_convert_rgb_to_ycbcr);

/**
 * drm_edp_backlight_set_level() - Set the backlight level of an eDP panel via AUX
 * @aux: The DP AUX channel to use
 * @bl: Backlight capability info from drm_edp_backlight_init()
 * @level: The brightness level to set
 *
 * Sets the brightness level of an eDP panel's backlight. Note that the panel's backlight must
 * already have been enabled by the driver by calling drm_edp_backlight_enable().
 *
 * Returns: %0 on success, negative error code on failure
 */
int drm_edp_backlight_set_level(struct drm_dp_aux *aux, const struct drm_edp_backlight_info *bl,
				u16 level)
{
	int ret;
	u8 buf[2] = { 0 };

	/* The panel uses the PWM for controlling brightness levels */
	if (!bl->aux_set)
		return 0;

	if (bl->lsb_reg_used) {
		buf[0] = (level & 0xff00) >> 8;
		buf[1] = (level & 0x00ff);
	} else {
		buf[0] = level;
	}

	ret = drm_dp_dpcd_write(aux, DP_EDP_BACKLIGHT_BRIGHTNESS_MSB, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		drm_err(aux->drm_dev,
			"%s: Failed to write aux backlight level: %d\n",
			aux->name, ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}
EXPORT_SYMBOL(drm_edp_backlight_set_level);

static int
drm_edp_backlight_set_enable(struct drm_dp_aux *aux, const struct drm_edp_backlight_info *bl,
			     bool enable)
{
	int ret;
	u8 buf;

	/* This panel uses the EDP_BL_PWR GPIO for enablement */
	if (!bl->aux_enable)
		return 0;

	ret = drm_dp_dpcd_readb(aux, DP_EDP_DISPLAY_CONTROL_REGISTER, &buf);
	if (ret != 1) {
		drm_err(aux->drm_dev, "%s: Failed to read eDP display control register: %d\n",
			aux->name, ret);
		return ret < 0 ? ret : -EIO;
	}
	if (enable)
		buf |= DP_EDP_BACKLIGHT_ENABLE;
	else
		buf &= ~DP_EDP_BACKLIGHT_ENABLE;

	ret = drm_dp_dpcd_writeb(aux, DP_EDP_DISPLAY_CONTROL_REGISTER, buf);
	if (ret != 1) {
		drm_err(aux->drm_dev, "%s: Failed to write eDP display control register: %d\n",
			aux->name, ret);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

/**
 * drm_edp_backlight_enable() - Enable an eDP panel's backlight using DPCD
 * @aux: The DP AUX channel to use
 * @bl: Backlight capability info from drm_edp_backlight_init()
 * @level: The initial backlight level to set via AUX, if there is one
 *
 * This function handles enabling DPCD backlight controls on a panel over DPCD, while additionally
 * restoring any important backlight state such as the given backlight level, the brightness byte
 * count, backlight frequency, etc.
 *
 * Note that certain panels do not support being enabled or disabled via DPCD, but instead require
 * that the driver handle enabling/disabling the panel through implementation-specific means using
 * the EDP_BL_PWR GPIO. For such panels, &drm_edp_backlight_info.aux_enable will be set to %false,
 * this function becomes a no-op, and the driver is expected to handle powering the panel on using
 * the EDP_BL_PWR GPIO.
 *
 * Returns: %0 on success, negative error code on failure.
 */
int drm_edp_backlight_enable(struct drm_dp_aux *aux, const struct drm_edp_backlight_info *bl,
			     const u16 level)
{
	int ret;
	u8 dpcd_buf;

	if (bl->aux_set)
		dpcd_buf = DP_EDP_BACKLIGHT_CONTROL_MODE_DPCD;
	else
		dpcd_buf = DP_EDP_BACKLIGHT_CONTROL_MODE_PWM;

	if (bl->pwmgen_bit_count) {
		ret = drm_dp_dpcd_writeb(aux, DP_EDP_PWMGEN_BIT_COUNT, bl->pwmgen_bit_count);
		if (ret != 1)
			drm_dbg_kms(aux->drm_dev, "%s: Failed to write aux pwmgen bit count: %d\n",
				    aux->name, ret);
	}

	if (bl->pwm_freq_pre_divider) {
		ret = drm_dp_dpcd_writeb(aux, DP_EDP_BACKLIGHT_FREQ_SET, bl->pwm_freq_pre_divider);
		if (ret != 1)
			drm_dbg_kms(aux->drm_dev,
				    "%s: Failed to write aux backlight frequency: %d\n",
				    aux->name, ret);
		else
			dpcd_buf |= DP_EDP_BACKLIGHT_FREQ_AUX_SET_ENABLE;
	}

	ret = drm_dp_dpcd_writeb(aux, DP_EDP_BACKLIGHT_MODE_SET_REGISTER, dpcd_buf);
	if (ret != 1) {
		drm_dbg_kms(aux->drm_dev, "%s: Failed to write aux backlight mode: %d\n",
			    aux->name, ret);
		return ret < 0 ? ret : -EIO;
	}

	ret = drm_edp_backlight_set_level(aux, bl, level);
	if (ret < 0)
		return ret;
	ret = drm_edp_backlight_set_enable(aux, bl, true);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_edp_backlight_enable);

/**
 * drm_edp_backlight_disable() - Disable an eDP backlight using DPCD, if supported
 * @aux: The DP AUX channel to use
 * @bl: Backlight capability info from drm_edp_backlight_init()
 *
 * This function handles disabling DPCD backlight controls on a panel over AUX.
 *
 * Note that certain panels do not support being enabled or disabled via DPCD, but instead require
 * that the driver handle enabling/disabling the panel through implementation-specific means using
 * the EDP_BL_PWR GPIO. For such panels, &drm_edp_backlight_info.aux_enable will be set to %false,
 * this function becomes a no-op, and the driver is expected to handle powering the panel off using
 * the EDP_BL_PWR GPIO.
 *
 * Returns: %0 on success or no-op, negative error code on failure.
 */
int drm_edp_backlight_disable(struct drm_dp_aux *aux, const struct drm_edp_backlight_info *bl)
{
	int ret;

	ret = drm_edp_backlight_set_enable(aux, bl, false);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL(drm_edp_backlight_disable);

static inline int
drm_edp_backlight_probe_max(struct drm_dp_aux *aux, struct drm_edp_backlight_info *bl,
			    u16 driver_pwm_freq_hz, const u8 edp_dpcd[EDP_DISPLAY_CTL_CAP_SIZE])
{
	int fxp, fxp_min, fxp_max, fxp_actual, f = 1;
	int ret;
	u8 pn, pn_min, pn_max;

	if (!bl->aux_set)
		return 0;

	ret = drm_dp_dpcd_readb(aux, DP_EDP_PWMGEN_BIT_COUNT, &pn);
	if (ret != 1) {
		drm_dbg_kms(aux->drm_dev, "%s: Failed to read pwmgen bit count cap: %d\n",
			    aux->name, ret);
		return -ENODEV;
	}

	pn &= DP_EDP_PWMGEN_BIT_COUNT_MASK;
	bl->max = (1 << pn) - 1;
	if (!driver_pwm_freq_hz)
		return 0;

	/*
	 * Set PWM Frequency divider to match desired frequency provided by the driver.
	 * The PWM Frequency is calculated as 27Mhz / (F x P).
	 * - Where F = PWM Frequency Pre-Divider value programmed by field 7:0 of the
	 *             EDP_BACKLIGHT_FREQ_SET register (DPCD Address 00728h)
	 * - Where P = 2^Pn, where Pn is the value programmed by field 4:0 of the
	 *             EDP_PWMGEN_BIT_COUNT register (DPCD Address 00724h)
	 */

	/* Find desired value of (F x P)
	 * Note that, if F x P is out of supported range, the maximum value or minimum value will
	 * applied automatically. So no need to check that.
	 */
	fxp = DIV_ROUND_CLOSEST(1000 * DP_EDP_BACKLIGHT_FREQ_BASE_KHZ, driver_pwm_freq_hz);

	/* Use highest possible value of Pn for more granularity of brightness adjustment while
	 * satisfying the conditions below.
	 * - Pn is in the range of Pn_min and Pn_max
	 * - F is in the range of 1 and 255
	 * - FxP is within 25% of desired value.
	 *   Note: 25% is arbitrary value and may need some tweak.
	 */
	ret = drm_dp_dpcd_readb(aux, DP_EDP_PWMGEN_BIT_COUNT_CAP_MIN, &pn_min);
	if (ret != 1) {
		drm_dbg_kms(aux->drm_dev, "%s: Failed to read pwmgen bit count cap min: %d\n",
			    aux->name, ret);
		return 0;
	}
	ret = drm_dp_dpcd_readb(aux, DP_EDP_PWMGEN_BIT_COUNT_CAP_MAX, &pn_max);
	if (ret != 1) {
		drm_dbg_kms(aux->drm_dev, "%s: Failed to read pwmgen bit count cap max: %d\n",
			    aux->name, ret);
		return 0;
	}
	pn_min &= DP_EDP_PWMGEN_BIT_COUNT_MASK;
	pn_max &= DP_EDP_PWMGEN_BIT_COUNT_MASK;

	/* Ensure frequency is within 25% of desired value */
	fxp_min = DIV_ROUND_CLOSEST(fxp * 3, 4);
	fxp_max = DIV_ROUND_CLOSEST(fxp * 5, 4);
	if (fxp_min < (1 << pn_min) || (255 << pn_max) < fxp_max) {
		drm_dbg_kms(aux->drm_dev,
			    "%s: Driver defined backlight frequency (%d) out of range\n",
			    aux->name, driver_pwm_freq_hz);
		return 0;
	}

	for (pn = pn_max; pn >= pn_min; pn--) {
		f = clamp(DIV_ROUND_CLOSEST(fxp, 1 << pn), 1, 255);
		fxp_actual = f << pn;
		if (fxp_min <= fxp_actual && fxp_actual <= fxp_max)
			break;
	}

	ret = drm_dp_dpcd_writeb(aux, DP_EDP_PWMGEN_BIT_COUNT, pn);
	if (ret != 1) {
		drm_dbg_kms(aux->drm_dev, "%s: Failed to write aux pwmgen bit count: %d\n",
			    aux->name, ret);
		return 0;
	}
	bl->pwmgen_bit_count = pn;
	bl->max = (1 << pn) - 1;

	if (edp_dpcd[2] & DP_EDP_BACKLIGHT_FREQ_AUX_SET_CAP) {
		bl->pwm_freq_pre_divider = f;
		drm_dbg_kms(aux->drm_dev, "%s: Using backlight frequency from driver (%dHz)\n",
			    aux->name, driver_pwm_freq_hz);
	}

	return 0;
}

static inline int
drm_edp_backlight_probe_state(struct drm_dp_aux *aux, struct drm_edp_backlight_info *bl,
			      u8 *current_mode)
{
	int ret;
	u8 buf[2];
	u8 mode_reg;

	ret = drm_dp_dpcd_readb(aux, DP_EDP_BACKLIGHT_MODE_SET_REGISTER, &mode_reg);
	if (ret != 1) {
		drm_dbg_kms(aux->drm_dev, "%s: Failed to read backlight mode: %d\n",
			    aux->name, ret);
		return ret < 0 ? ret : -EIO;
	}

	*current_mode = (mode_reg & DP_EDP_BACKLIGHT_CONTROL_MODE_MASK);
	if (!bl->aux_set)
		return 0;

	if (*current_mode == DP_EDP_BACKLIGHT_CONTROL_MODE_DPCD) {
		int size = 1 + bl->lsb_reg_used;

		ret = drm_dp_dpcd_read(aux, DP_EDP_BACKLIGHT_BRIGHTNESS_MSB, buf, size);
		if (ret != size) {
			drm_dbg_kms(aux->drm_dev, "%s: Failed to read backlight level: %d\n",
				    aux->name, ret);
			return ret < 0 ? ret : -EIO;
		}

		if (bl->lsb_reg_used)
			return (buf[0] << 8) | buf[1];
		else
			return buf[0];
	}

	/*
	 * If we're not in DPCD control mode yet, the programmed brightness value is meaningless and
	 * the driver should assume max brightness
	 */
	return bl->max;
}

/**
 * drm_edp_backlight_init() - Probe a display panel's TCON using the standard VESA eDP backlight
 * interface.
 * @aux: The DP aux device to use for probing
 * @bl: The &drm_edp_backlight_info struct to fill out with information on the backlight
 * @driver_pwm_freq_hz: Optional PWM frequency from the driver in hz
 * @edp_dpcd: A cached copy of the eDP DPCD
 * @current_level: Where to store the probed brightness level, if any
 * @current_mode: Where to store the currently set backlight control mode
 *
 * Initializes a &drm_edp_backlight_info struct by probing @aux for it's backlight capabilities,
 * along with also probing the current and maximum supported brightness levels.
 *
 * If @driver_pwm_freq_hz is non-zero, this will be used as the backlight frequency. Otherwise, the
 * default frequency from the panel is used.
 *
 * Returns: %0 on success, negative error code on failure.
 */
int
drm_edp_backlight_init(struct drm_dp_aux *aux, struct drm_edp_backlight_info *bl,
		       u16 driver_pwm_freq_hz, const u8 edp_dpcd[EDP_DISPLAY_CTL_CAP_SIZE],
		       u16 *current_level, u8 *current_mode)
{
	int ret;

	if (edp_dpcd[1] & DP_EDP_BACKLIGHT_AUX_ENABLE_CAP)
		bl->aux_enable = true;
	if (edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_AUX_SET_CAP)
		bl->aux_set = true;
	if (edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_BYTE_COUNT)
		bl->lsb_reg_used = true;

	/* Sanity check caps */
	if (!bl->aux_set && !(edp_dpcd[2] & DP_EDP_BACKLIGHT_BRIGHTNESS_PWM_PIN_CAP)) {
		drm_dbg_kms(aux->drm_dev,
			    "%s: Panel supports neither AUX or PWM brightness control? Aborting\n",
			    aux->name);
		return -EINVAL;
	}

	ret = drm_edp_backlight_probe_max(aux, bl, driver_pwm_freq_hz, edp_dpcd);
	if (ret < 0)
		return ret;

	ret = drm_edp_backlight_probe_state(aux, bl, current_mode);
	if (ret < 0)
		return ret;
	*current_level = ret;

	drm_dbg_kms(aux->drm_dev,
		    "%s: Found backlight: aux_set=%d aux_enable=%d mode=%d\n",
		    aux->name, bl->aux_set, bl->aux_enable, *current_mode);
	if (bl->aux_set) {
		drm_dbg_kms(aux->drm_dev,
			    "%s: Backlight caps: level=%d/%d pwm_freq_pre_divider=%d lsb_reg_used=%d\n",
			    aux->name, *current_level, bl->max, bl->pwm_freq_pre_divider,
			    bl->lsb_reg_used);
	}

	return 0;
}
EXPORT_SYMBOL(drm_edp_backlight_init);

#if IS_BUILTIN(CONFIG_BACKLIGHT_CLASS_DEVICE) || \
	(IS_MODULE(CONFIG_DRM_KMS_HELPER) && IS_MODULE(CONFIG_BACKLIGHT_CLASS_DEVICE))

static int dp_aux_backlight_update_status(struct backlight_device *bd)
{
	struct dp_aux_backlight *bl = bl_get_data(bd);
	u16 brightness = backlight_get_brightness(bd);
	int ret = 0;

	if (!backlight_is_blank(bd)) {
		if (!bl->enabled) {
			drm_edp_backlight_enable(bl->aux, &bl->info, brightness);
			bl->enabled = true;
			return 0;
		}
		ret = drm_edp_backlight_set_level(bl->aux, &bl->info, brightness);
	} else {
		if (bl->enabled) {
			drm_edp_backlight_disable(bl->aux, &bl->info);
			bl->enabled = false;
		}
	}

	return ret;
}

static const struct backlight_ops dp_aux_bl_ops = {
	.update_status = dp_aux_backlight_update_status,
};

/**
 * drm_panel_dp_aux_backlight - create and use DP AUX backlight
 * @panel: DRM panel
 * @aux: The DP AUX channel to use
 *
 * Use this function to create and handle backlight if your panel
 * supports backlight control over DP AUX channel using DPCD
 * registers as per VESA's standard backlight control interface.
 *
 * When the panel is enabled backlight will be enabled after a
 * successful call to &drm_panel_funcs.enable()
 *
 * When the panel is disabled backlight will be disabled before the
 * call to &drm_panel_funcs.disable().
 *
 * A typical implementation for a panel driver supporting backlight
 * control over DP AUX will call this function at probe time.
 * Backlight will then be handled transparently without requiring
 * any intervention from the driver.
 *
 * drm_panel_dp_aux_backlight() must be called after the call to drm_panel_init().
 *
 * Return: 0 on success or a negative error code on failure.
 */
int drm_panel_dp_aux_backlight(struct drm_panel *panel, struct drm_dp_aux *aux)
{
	struct dp_aux_backlight *bl;
	struct backlight_properties props = { 0 };
	u16 current_level;
	u8 current_mode;
	u8 edp_dpcd[EDP_DISPLAY_CTL_CAP_SIZE];
	int ret;

	if (!panel || !panel->dev || !aux)
		return -EINVAL;

	ret = drm_dp_dpcd_read(aux, DP_EDP_DPCD_REV, edp_dpcd,
			       EDP_DISPLAY_CTL_CAP_SIZE);
	if (ret < 0)
		return ret;

	if (!drm_edp_backlight_supported(edp_dpcd)) {
		DRM_DEV_INFO(panel->dev, "DP AUX backlight is not supported\n");
		return 0;
	}

	bl = devm_kzalloc(panel->dev, sizeof(*bl), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;

	bl->aux = aux;

	ret = drm_edp_backlight_init(aux, &bl->info, 0, edp_dpcd,
				     &current_level, &current_mode);
	if (ret < 0)
		return ret;

	props.type = BACKLIGHT_RAW;
	props.brightness = current_level;
	props.max_brightness = bl->info.max;

	bl->base = devm_backlight_device_register(panel->dev, "dp_aux_backlight",
						  panel->dev, bl,
						  &dp_aux_bl_ops, &props);
	if (IS_ERR(bl->base))
		return PTR_ERR(bl->base);

	backlight_disable(bl->base);

	panel->backlight = bl->base;

	return 0;
}
EXPORT_SYMBOL(drm_panel_dp_aux_backlight);

#endif
