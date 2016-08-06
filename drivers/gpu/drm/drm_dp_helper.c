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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_dp_aux_dev.h>
#include <drm/drmP.h>

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

void drm_dp_link_train_clock_recovery_delay(const u8 dpcd[DP_RECEIVER_CAP_SIZE]) {
	if (dpcd[DP_TRAINING_AUX_RD_INTERVAL] == 0)
		udelay(100);
	else
		mdelay(dpcd[DP_TRAINING_AUX_RD_INTERVAL] * 4);
}
EXPORT_SYMBOL(drm_dp_link_train_clock_recovery_delay);

void drm_dp_link_train_channel_eq_delay(const u8 dpcd[DP_RECEIVER_CAP_SIZE]) {
	if (dpcd[DP_TRAINING_AUX_RD_INTERVAL] == 0)
		udelay(400);
	else
		mdelay(dpcd[DP_TRAINING_AUX_RD_INTERVAL] * 4);
}
EXPORT_SYMBOL(drm_dp_link_train_channel_eq_delay);

u8 drm_dp_link_rate_to_bw_code(int link_rate)
{
	switch (link_rate) {
	case 162000:
	default:
		return DP_LINK_BW_1_62;
	case 270000:
		return DP_LINK_BW_2_7;
	case 540000:
		return DP_LINK_BW_5_4;
	}
}
EXPORT_SYMBOL(drm_dp_link_rate_to_bw_code);

int drm_dp_bw_code_to_link_rate(u8 link_bw)
{
	switch (link_bw) {
	case DP_LINK_BW_1_62:
	default:
		return 162000;
	case DP_LINK_BW_2_7:
		return 270000;
	case DP_LINK_BW_5_4:
		return 540000;
	}
}
EXPORT_SYMBOL(drm_dp_bw_code_to_link_rate);

#define AUX_RETRY_INTERVAL 500 /* us */

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

	DRM_DEBUG_KMS("Too many retries, giving up. First error: %d\n", err);
	ret = err;

unlock:
	mutex_unlock(&aux->hw_mutex);
	return ret;
}

/**
 * drm_dp_dpcd_read() - read a series of bytes from the DPCD
 * @aux: DisplayPort AUX channel
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
	ret = drm_dp_dpcd_access(aux, DP_AUX_NATIVE_READ, DP_DPCD_REV, buffer,
				 1);
	if (ret != 1)
		return ret;

	return drm_dp_dpcd_access(aux, DP_AUX_NATIVE_READ, offset, buffer,
				  size);
}
EXPORT_SYMBOL(drm_dp_dpcd_read);

/**
 * drm_dp_dpcd_write() - write a series of bytes to the DPCD
 * @aux: DisplayPort AUX channel
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
	return drm_dp_dpcd_access(aux, DP_AUX_NATIVE_WRITE, offset, buffer,
				  size);
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
 * drm_dp_link_probe() - probe a DisplayPort link for capabilities
 * @aux: DisplayPort AUX channel
 * @link: pointer to structure in which to return link capabilities
 *
 * The structure filled in by this function can usually be passed directly
 * into drm_dp_link_power_up() and drm_dp_link_configure() to power up and
 * configure the link based on the link's capabilities.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_link_probe(struct drm_dp_aux *aux, struct drm_dp_link *link)
{
	u8 values[3];
	int err;

	memset(link, 0, sizeof(*link));

	err = drm_dp_dpcd_read(aux, DP_DPCD_REV, values, sizeof(values));
	if (err < 0)
		return err;

	link->revision = values[0];
	link->rate = drm_dp_bw_code_to_link_rate(values[1]);
	link->num_lanes = values[2] & DP_MAX_LANE_COUNT_MASK;

	if (values[2] & DP_ENHANCED_FRAME_CAP)
		link->capabilities |= DP_LINK_CAP_ENHANCED_FRAMING;

	return 0;
}
EXPORT_SYMBOL(drm_dp_link_probe);

/**
 * drm_dp_link_power_up() - power up a DisplayPort link
 * @aux: DisplayPort AUX channel
 * @link: pointer to a structure containing the link configuration
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_link_power_up(struct drm_dp_aux *aux, struct drm_dp_link *link)
{
	u8 value;
	int err;

	/* DP_SET_POWER register is only available on DPCD v1.1 and later */
	if (link->revision < 0x11)
		return 0;

	err = drm_dp_dpcd_readb(aux, DP_SET_POWER, &value);
	if (err < 0)
		return err;

	value &= ~DP_SET_POWER_MASK;
	value |= DP_SET_POWER_D0;

	err = drm_dp_dpcd_writeb(aux, DP_SET_POWER, value);
	if (err < 0)
		return err;

	/*
	 * According to the DP 1.1 specification, a "Sink Device must exit the
	 * power saving state within 1 ms" (Section 2.5.3.1, Table 5-52, "Sink
	 * Control Field" (register 0x600).
	 */
	usleep_range(1000, 2000);

	return 0;
}
EXPORT_SYMBOL(drm_dp_link_power_up);

/**
 * drm_dp_link_power_down() - power down a DisplayPort link
 * @aux: DisplayPort AUX channel
 * @link: pointer to a structure containing the link configuration
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_link_power_down(struct drm_dp_aux *aux, struct drm_dp_link *link)
{
	u8 value;
	int err;

	/* DP_SET_POWER register is only available on DPCD v1.1 and later */
	if (link->revision < 0x11)
		return 0;

	err = drm_dp_dpcd_readb(aux, DP_SET_POWER, &value);
	if (err < 0)
		return err;

	value &= ~DP_SET_POWER_MASK;
	value |= DP_SET_POWER_D3;

	err = drm_dp_dpcd_writeb(aux, DP_SET_POWER, value);
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(drm_dp_link_power_down);

/**
 * drm_dp_link_configure() - configure a DisplayPort link
 * @aux: DisplayPort AUX channel
 * @link: pointer to a structure containing the link configuration
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_link_configure(struct drm_dp_aux *aux, struct drm_dp_link *link)
{
	u8 values[2];
	int err;

	values[0] = drm_dp_link_rate_to_bw_code(link->rate);
	values[1] = link->num_lanes;

	if (link->capabilities & DP_LINK_CAP_ENHANCED_FRAMING)
		values[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	err = drm_dp_dpcd_write(aux, DP_LINK_BW_SET, values, sizeof(values));
	if (err < 0)
		return err;

	return 0;
}
EXPORT_SYMBOL(drm_dp_link_configure);

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
 * Deterine how many retries should be attempted to successfully transfer
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
 * aux->transfer function does not modify anything in the msg other than the
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

			DRM_DEBUG_KMS("transaction failed: %d\n", ret);
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
			DRM_DEBUG_KMS("native nack (result=%d, size=%zu)\n", ret, msg->size);
			return -EREMOTEIO;

		case DP_AUX_NATIVE_REPLY_DEFER:
			DRM_DEBUG_KMS("native defer\n");
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
			DRM_ERROR("invalid native reply %#04x\n", msg->reply);
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
			DRM_DEBUG_KMS("I2C nack (result=%d, size=%zu\n", ret, msg->size);
			aux->i2c_nack_count++;
			return -EREMOTEIO;

		case DP_AUX_I2C_REPLY_DEFER:
			DRM_DEBUG_KMS("I2C defer\n");
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
			DRM_ERROR("invalid I2C reply %#04x\n", msg->reply);
			return -EREMOTEIO;
		}
	}

	DRM_DEBUG_KMS("too many retries, giving up\n");
	return -EREMOTEIO;
}

static void drm_dp_i2c_msg_set_request(struct drm_dp_aux_msg *msg,
				       const struct i2c_msg *i2c_msg)
{
	msg->request = (i2c_msg->flags & I2C_M_RD) ?
		DP_AUX_I2C_READ : DP_AUX_I2C_WRITE;
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
			DRM_DEBUG_KMS("Partial I2C reply: requested %zu bytes got %d bytes\n",
				      msg.size, err);
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

/**
 * drm_dp_aux_init() - minimally initialise an aux channel
 * @aux: DisplayPort AUX channel
 *
 * If you need to use the drm_dp_aux's i2c adapter prior to registering it
 * with the outside world, call drm_dp_aux_init() first. You must still
 * call drm_dp_aux_register() once the connector has been registered to
 * allow userspace access to the auxiliary DP channel.
 */
void drm_dp_aux_init(struct drm_dp_aux *aux)
{
	mutex_init(&aux->hw_mutex);

	aux->ddc.algo = &drm_dp_i2c_algo;
	aux->ddc.algo_data = aux;
	aux->ddc.retries = 3;

	aux->ddc.lock_bus = lock_bus;
	aux->ddc.trylock_bus = trylock_bus;
	aux->ddc.unlock_bus = unlock_bus;
}
EXPORT_SYMBOL(drm_dp_aux_init);

/**
 * drm_dp_aux_register() - initialise and register aux channel
 * @aux: DisplayPort AUX channel
 *
 * Automatically calls drm_dp_aux_init() if this hasn't been done yet.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_aux_register(struct drm_dp_aux *aux)
{
	int ret;

	if (!aux->ddc.algo)
		drm_dp_aux_init(aux);

	aux->ddc.class = I2C_CLASS_DDC;
	aux->ddc.owner = THIS_MODULE;
	aux->ddc.dev.parent = aux->dev;
	aux->ddc.dev.of_node = aux->dev->of_node;

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
