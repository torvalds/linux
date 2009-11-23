/*
 * Copyright 2007-8 Advanced Micro Devices, Inc.
 * Copyright 2008 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie
 *          Alex Deucher
 */
#include "drmP.h"
#include "radeon_drm.h"
#include "radeon.h"

#include "atom.h"
#include "atom-bits.h"
#include "drm_dp_helper.h"

#define DP_LINK_STATUS_SIZE	6

/* move these to drm_dp_helper.c/h */

static const int dp_clocks[] = {
	54000,  // 1 lane, 1.62 Ghz
	90000,  // 1 lane, 2.70 Ghz
	108000, // 2 lane, 1.62 Ghz
	180000, // 2 lane, 2.70 Ghz
	216000, // 4 lane, 1.62 Ghz
	360000, // 4 lane, 2.70 Ghz
};

static const int num_dp_clocks = sizeof(dp_clocks) / sizeof(int);

int dp_lanes_for_mode_clock(int max_link_bw, int mode_clock)
{
	int i;

	switch (max_link_bw) {
	case DP_LINK_BW_1_62:
	default:
		for (i = 0; i < num_dp_clocks; i++) {
			if (i % 2)
				continue;
			if (dp_clocks[i] > mode_clock) {
				if (i < 2)
					return 1;
				else if (i < 4)
					return 2;
				else
					return 4;
			}
		}
		break;
	case DP_LINK_BW_2_7:
		for (i = 0; i < num_dp_clocks; i++) {
			if (dp_clocks[i] > mode_clock) {
				if (i < 2)
					return 1;
				else if (i < 4)
					return 2;
				else
					return 4;
			}
		}
		break;
	}

	return 0;
}

int dp_link_clock_for_mode_clock(int max_link_bw, int mode_clock)
{
	int i;

	switch (max_link_bw) {
	case DP_LINK_BW_1_62:
	default:
		return 162000;
		break;
	case DP_LINK_BW_2_7:
		for (i = 0; i < num_dp_clocks; i++) {
			if (dp_clocks[i] > mode_clock)
				return (i % 2) ? 270000 : 162000;
		}
	}

	return 0;
}

bool radeon_process_aux_ch(struct radeon_i2c_chan *chan, u8 *req_bytes,
			   int num_bytes, u8 *read_byte,
			   u8 read_buf_len, u8 delay)
{
	struct drm_device *dev = chan->dev;
	struct radeon_device *rdev = dev->dev_private;
	PROCESS_AUX_CHANNEL_TRANSACTION_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, ProcessAuxChannelTransaction);
	unsigned char *base;

	memset(&args, 0, sizeof(args));

	base = (unsigned char *)rdev->mode_info.atom_context->scratch;

	memcpy(base, req_bytes, num_bytes);

	args.lpAuxRequest = 0;
	args.lpDataOut = 16;
	args.ucDataOutLen = 0;
	args.ucChannelID = chan->rec.i2c_id;
	args.ucDelay = delay / 10;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

	if (args.ucReplyStatus) {
		DRM_ERROR("failed to get auxch %02x%02x %02x %02x 0x%02x %02x\n",
			  req_bytes[1], req_bytes[0], req_bytes[2], req_bytes[3],
			  chan->rec.i2c_id, args.ucReplyStatus);
		return false;
	}

	if (args.ucDataOutLen && read_byte && read_buf_len) {
		if (read_buf_len < args.ucDataOutLen) {
			DRM_ERROR("Buffer to small for return answer %d %d\n",
				  read_buf_len, args.ucDataOutLen);
			return false;
		}
		{
			int len = min(read_buf_len, args.ucDataOutLen);
			memcpy(read_byte, base + 16, len);
		}
	}
	return true;
}

static u8 radeon_dp_encoder_service(struct radeon_device *rdev, int action, int dp_clock,
				    uint8_t ucconfig, uint8_t lane_num)
{
	DP_ENCODER_SERVICE_PARAMETERS args;
	int index = GetIndexIntoMasterTable(COMMAND, DPEncoderService);

	memset(&args, 0, sizeof(args));
	args.ucLinkClock = dp_clock / 10;
	args.ucConfig = ucconfig;
	args.ucAction = action;
	args.ucLaneNum = lane_num;
	args.ucStatus = 0;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);
	return args.ucStatus;
}

u8 radeon_dp_getsinktype(struct radeon_connector *radeon_connector)
{
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;
	struct drm_device *dev = radeon_connector->base.dev;
	struct radeon_device *rdev = dev->dev_private;

	return radeon_dp_encoder_service(rdev, ATOM_DP_ACTION_GET_SINK_TYPE, 0,
					 radeon_dig_connector->dp_i2c_bus->rec.i2c_id, 0);
}

union dig_transmitter_control {
	DIG_TRANSMITTER_CONTROL_PS_ALLOCATION v1;
	DIG_TRANSMITTER_CONTROL_PARAMETERS_V2 v2;
};

bool radeon_dp_aux_native_write(struct radeon_connector *radeon_connector, uint16_t address,
				uint8_t send_bytes, uint8_t *send)
{
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;
	struct drm_device *dev = radeon_connector->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	u8 msg[20];
	u8 msg_len, dp_msg_len;
	bool ret;

	dp_msg_len = 4;
	msg[0] = address;
	msg[1] = address >> 8;
	msg[2] = AUX_NATIVE_WRITE << 4;
	dp_msg_len += send_bytes;
	msg[3] = (dp_msg_len << 4) | (send_bytes - 1);

	if (send_bytes > 16)
		return false;

	memcpy(&msg[4], send, send_bytes);
	msg_len = 4 + send_bytes;
	ret = radeon_process_aux_ch(radeon_dig_connector->dp_i2c_bus, msg, msg_len, NULL, 0, 0);
	return ret;
}

bool radeon_dp_aux_native_read(struct radeon_connector *radeon_connector, uint16_t address,
			       uint8_t delay, uint8_t expected_bytes,
			       uint8_t *read_p)
{
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;
	struct drm_device *dev = radeon_connector->base.dev;
	struct radeon_device *rdev = dev->dev_private;
	u8 msg[20];
	u8 msg_len, dp_msg_len;
	bool ret = false;
	msg_len = 4;
	dp_msg_len = 4;
	msg[0] = address;
	msg[1] = address >> 8;
	msg[2] = AUX_NATIVE_READ << 4;
	msg[3] = (dp_msg_len) << 4;
	msg[3] |= expected_bytes - 1;

	ret = radeon_process_aux_ch(radeon_dig_connector->dp_i2c_bus, msg, msg_len, read_p, expected_bytes, delay);
	return ret;
}

void radeon_dp_getdpcd(struct radeon_connector *radeon_connector)
{
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;
	u8 msg[25];
	int ret;

	ret = radeon_dp_aux_native_read(radeon_connector, DP_DPCD_REV, 0, 8, msg);
	if (ret) {
		memcpy(radeon_dig_connector->dpcd, msg, 8);
		{
			int i;
			printk("DPCD: ");
			for (i = 0; i < 8; i++)
				printk("%02x ", msg[i]);
			printk("\n");
		}
	}
	radeon_dig_connector->dpcd[0] = 0;
	return;
}

static bool atom_dp_get_link_status(struct radeon_connector *radeon_connector,
				    u8 link_status[DP_LINK_STATUS_SIZE])
{
	int ret;
	ret = radeon_dp_aux_native_read(radeon_connector, DP_LANE0_1_STATUS, 100,
					DP_LINK_STATUS_SIZE, link_status);
	if (!ret) {
		DRM_ERROR("displayport link status failed\n");
		return false;
	}

	DRM_INFO("link status %02x %02x %02x %02x %02x %02x\n",
		 link_status[0], link_status[1], link_status[2],
		 link_status[3], link_status[4], link_status[5]);
	return true;
}

static void dp_set_power(struct radeon_connector *radeon_connector, u8 power_state)
{
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;
	if (radeon_dig_connector->dpcd[0] >= 0x11) {
		radeon_dp_aux_native_write(radeon_connector, DP_SET_POWER, 1,
					   &power_state);
	}
}

static void dp_update_dpvs_emph(struct radeon_connector *radeon_connector,
				u8 train_set[4])
{
	struct radeon_connector_atom_dig *radeon_dig_connector = radeon_connector->con_priv;

//	radeon_dp_digtransmitter_setup_vsemph();
	radeon_dp_aux_native_write(radeon_connector, DP_TRAINING_LANE0_SET,
				   0/* lc */, train_set);
}

static void dp_set_training(struct radeon_connector *radeon_connector,
			    u8 training)
{
	radeon_dp_aux_native_write(radeon_connector, DP_TRAINING_PATTERN_SET,
				   1, &training);
}

int radeon_dp_i2c_aux_ch(struct i2c_adapter *adapter, int mode,
			 uint8_t write_byte, uint8_t *read_byte)
{
	struct i2c_algo_dp_aux_data *algo_data = adapter->algo_data;
	struct radeon_i2c_chan *auxch = (struct radeon_i2c_chan *)adapter;
	int ret = 0;
	uint16_t address = algo_data->address;
	uint8_t msg[5];
	uint8_t reply[2];
	int msg_len, dp_msg_len;
	int reply_bytes;

	/* Set up the command byte */
	if (mode & MODE_I2C_READ)
		msg[2] = AUX_I2C_READ << 4;
	else
		msg[2] = AUX_I2C_WRITE << 4;

	if (!(mode & MODE_I2C_STOP))
		msg[2] |= AUX_I2C_MOT << 4;

	msg[0] = address;
	msg[1] = address >> 8;

	reply_bytes = 1;

	msg_len = 4;
	dp_msg_len = 3;
	switch (mode) {
	case MODE_I2C_WRITE:
		msg[4] = write_byte;
		msg_len++;
		dp_msg_len += 2;
		break;
	case MODE_I2C_READ:
		dp_msg_len += 1;
		break;
	default:
		break;
	}

	msg[3] = (dp_msg_len) << 4;
	ret = radeon_process_aux_ch(auxch, msg, msg_len, reply, reply_bytes, 0);

	if (ret) {
		if (read_byte)
			*read_byte = reply[0];
		return reply_bytes;
	}
	return -EREMOTEIO;
}
