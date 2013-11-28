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
#include <drm/drmP.h>

/**
 * DOC: dp helpers
 *
 * These functions contain some common logic and helpers at various abstraction
 * levels to deal with Display Port sink devices and related things like DP aux
 * channel transfers, EDID reading over DP aux channels, decoding certain DPCD
 * blocks, ...
 */

/* Run a single AUX_CH I2C transaction, writing/reading data as necessary */
static int
i2c_algo_dp_aux_transaction(struct i2c_adapter *adapter, int mode,
			    uint8_t write_byte, uint8_t *read_byte)
{
	struct i2c_algo_dp_aux_data *algo_data = adapter->algo_data;
	int ret;

	ret = (*algo_data->aux_ch)(adapter, mode,
				   write_byte, read_byte);
	return ret;
}

/*
 * I2C over AUX CH
 */

/*
 * Send the address. If the I2C link is running, this 'restarts'
 * the connection with the new address, this is used for doing
 * a write followed by a read (as needed for DDC)
 */
static int
i2c_algo_dp_aux_address(struct i2c_adapter *adapter, u16 address, bool reading)
{
	struct i2c_algo_dp_aux_data *algo_data = adapter->algo_data;
	int mode = MODE_I2C_START;
	int ret;

	if (reading)
		mode |= MODE_I2C_READ;
	else
		mode |= MODE_I2C_WRITE;
	algo_data->address = address;
	algo_data->running = true;
	ret = i2c_algo_dp_aux_transaction(adapter, mode, 0, NULL);
	return ret;
}

/*
 * Stop the I2C transaction. This closes out the link, sending
 * a bare address packet with the MOT bit turned off
 */
static void
i2c_algo_dp_aux_stop(struct i2c_adapter *adapter, bool reading)
{
	struct i2c_algo_dp_aux_data *algo_data = adapter->algo_data;
	int mode = MODE_I2C_STOP;

	if (reading)
		mode |= MODE_I2C_READ;
	else
		mode |= MODE_I2C_WRITE;
	if (algo_data->running) {
		(void) i2c_algo_dp_aux_transaction(adapter, mode, 0, NULL);
		algo_data->running = false;
	}
}

/*
 * Write a single byte to the current I2C address, the
 * the I2C link must be running or this returns -EIO
 */
static int
i2c_algo_dp_aux_put_byte(struct i2c_adapter *adapter, u8 byte)
{
	struct i2c_algo_dp_aux_data *algo_data = adapter->algo_data;
	int ret;

	if (!algo_data->running)
		return -EIO;

	ret = i2c_algo_dp_aux_transaction(adapter, MODE_I2C_WRITE, byte, NULL);
	return ret;
}

/*
 * Read a single byte from the current I2C address, the
 * I2C link must be running or this returns -EIO
 */
static int
i2c_algo_dp_aux_get_byte(struct i2c_adapter *adapter, u8 *byte_ret)
{
	struct i2c_algo_dp_aux_data *algo_data = adapter->algo_data;
	int ret;

	if (!algo_data->running)
		return -EIO;

	ret = i2c_algo_dp_aux_transaction(adapter, MODE_I2C_READ, 0, byte_ret);
	return ret;
}

static int
i2c_algo_dp_aux_xfer(struct i2c_adapter *adapter,
		     struct i2c_msg *msgs,
		     int num)
{
	int ret = 0;
	bool reading = false;
	int m;
	int b;

	for (m = 0; m < num; m++) {
		u16 len = msgs[m].len;
		u8 *buf = msgs[m].buf;
		reading = (msgs[m].flags & I2C_M_RD) != 0;
		ret = i2c_algo_dp_aux_address(adapter, msgs[m].addr, reading);
		if (ret < 0)
			break;
		if (reading) {
			for (b = 0; b < len; b++) {
				ret = i2c_algo_dp_aux_get_byte(adapter, &buf[b]);
				if (ret < 0)
					break;
			}
		} else {
			for (b = 0; b < len; b++) {
				ret = i2c_algo_dp_aux_put_byte(adapter, buf[b]);
				if (ret < 0)
					break;
			}
		}
		if (ret < 0)
			break;
	}
	if (ret >= 0)
		ret = num;
	i2c_algo_dp_aux_stop(adapter, reading);
	DRM_DEBUG_KMS("dp_aux_xfer return %d\n", ret);
	return ret;
}

static u32
i2c_algo_dp_aux_functionality(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL |
	       I2C_FUNC_SMBUS_READ_BLOCK_DATA |
	       I2C_FUNC_SMBUS_BLOCK_PROC_CALL |
	       I2C_FUNC_10BIT_ADDR;
}

static const struct i2c_algorithm i2c_dp_aux_algo = {
	.master_xfer	= i2c_algo_dp_aux_xfer,
	.functionality	= i2c_algo_dp_aux_functionality,
};

static void
i2c_dp_aux_reset_bus(struct i2c_adapter *adapter)
{
	(void) i2c_algo_dp_aux_address(adapter, 0, false);
	(void) i2c_algo_dp_aux_stop(adapter, false);
}

static int
i2c_dp_aux_prepare_bus(struct i2c_adapter *adapter)
{
	adapter->algo = &i2c_dp_aux_algo;
	adapter->retries = 3;
	i2c_dp_aux_reset_bus(adapter);
	return 0;
}

/**
 * i2c_dp_aux_add_bus() - register an i2c adapter using the aux ch helper
 * @adapter: i2c adapter to register
 *
 * This registers an i2c adapater that uses dp aux channel as it's underlaying
 * transport. The driver needs to fill out the &i2c_algo_dp_aux_data structure
 * and store it in the algo_data member of the @adapter argument. This will be
 * used by the i2c over dp aux algorithm to drive the hardware.
 *
 * RETURNS:
 * 0 on success, -ERRNO on failure.
 */
int
i2c_dp_aux_add_bus(struct i2c_adapter *adapter)
{
	int error;

	error = i2c_dp_aux_prepare_bus(adapter);
	if (error)
		return error;
	error = i2c_add_adapter(adapter);
	return error;
}
EXPORT_SYMBOL(i2c_dp_aux_add_bus);

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
 *
 * An AUX channel can also be used to transport I2C messages to a sink. A
 * typical application of that is to access an EDID that's present in the
 * sink device. The .transfer() function can also be used to execute such
 * transactions. The drm_dp_aux_register_i2c_bus() function registers an
 * I2C adapter that can be passed to drm_probe_ddc(). Upon removal, drivers
 * should call drm_dp_aux_unregister_i2c_bus() to remove the I2C adapter.
 */

static int drm_dp_dpcd_access(struct drm_dp_aux *aux, u8 request,
			      unsigned int offset, void *buffer, size_t size)
{
	struct drm_dp_aux_msg msg;
	unsigned int retry;
	int err;

	memset(&msg, 0, sizeof(msg));
	msg.address = offset;
	msg.request = request;
	msg.buffer = buffer;
	msg.size = size;

	/*
	 * The specification doesn't give any recommendation on how often to
	 * retry native transactions, so retry 7 times like for I2C-over-AUX
	 * transactions.
	 */
	for (retry = 0; retry < 7; retry++) {
		err = aux->transfer(aux, &msg);
		if (err < 0) {
			if (err == -EBUSY)
				continue;

			return err;
		}

		if (err < size)
			return -EPROTO;

		switch (msg.reply & DP_AUX_NATIVE_REPLY_MASK) {
		case DP_AUX_NATIVE_REPLY_ACK:
			return err;

		case DP_AUX_NATIVE_REPLY_NACK:
			return -EIO;

		case DP_AUX_NATIVE_REPLY_DEFER:
			usleep_range(400, 500);
			break;
		}
	}

	DRM_ERROR("too many retries, giving up\n");
	return -EIO;
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
