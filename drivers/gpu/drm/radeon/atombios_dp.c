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

/* move these to drm_dp_helper.c/h */
#define DP_LINK_CONFIGURATION_SIZE 9
#define DP_LINK_STATUS_SIZE	   6
#define DP_DPCD_SIZE	           8

static char *voltage_names[] = {
        "0.4V", "0.6V", "0.8V", "1.2V"
};
static char *pre_emph_names[] = {
        "0dB", "3.5dB", "6dB", "9.5dB"
};

static const int dp_clocks[] = {
	54000,  /* 1 lane, 1.62 Ghz */
	90000,  /* 1 lane, 2.70 Ghz */
	108000, /* 2 lane, 1.62 Ghz */
	180000, /* 2 lane, 2.70 Ghz */
	216000, /* 4 lane, 1.62 Ghz */
	360000, /* 4 lane, 2.70 Ghz */
};

static const int num_dp_clocks = sizeof(dp_clocks) / sizeof(int);

/* common helper functions */
static int dp_lanes_for_mode_clock(u8 dpcd[DP_DPCD_SIZE], int mode_clock)
{
	int i;
	u8 max_link_bw;
	u8 max_lane_count;

	if (!dpcd)
		return 0;

	max_link_bw = dpcd[DP_MAX_LINK_RATE];
	max_lane_count = dpcd[DP_MAX_LANE_COUNT] & DP_MAX_LANE_COUNT_MASK;

	switch (max_link_bw) {
	case DP_LINK_BW_1_62:
	default:
		for (i = 0; i < num_dp_clocks; i++) {
			if (i % 2)
				continue;
			switch (max_lane_count) {
			case 1:
				if (i > 1)
					return 0;
				break;
			case 2:
				if (i > 3)
					return 0;
				break;
			case 4:
			default:
				break;
			}
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
			switch (max_lane_count) {
			case 1:
				if (i > 1)
					return 0;
				break;
			case 2:
				if (i > 3)
					return 0;
				break;
			case 4:
			default:
				break;
			}
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

static int dp_link_clock_for_mode_clock(u8 dpcd[DP_DPCD_SIZE], int mode_clock)
{
	int i;
	u8 max_link_bw;
	u8 max_lane_count;

	if (!dpcd)
		return 0;

	max_link_bw = dpcd[DP_MAX_LINK_RATE];
	max_lane_count = dpcd[DP_MAX_LANE_COUNT] & DP_MAX_LANE_COUNT_MASK;

	switch (max_link_bw) {
	case DP_LINK_BW_1_62:
	default:
		for (i = 0; i < num_dp_clocks; i++) {
			if (i % 2)
				continue;
			switch (max_lane_count) {
			case 1:
				if (i > 1)
					return 0;
				break;
			case 2:
				if (i > 3)
					return 0;
				break;
			case 4:
			default:
				break;
			}
			if (dp_clocks[i] > mode_clock)
				return 162000;
		}
		break;
	case DP_LINK_BW_2_7:
		for (i = 0; i < num_dp_clocks; i++) {
			switch (max_lane_count) {
			case 1:
				if (i > 1)
					return 0;
				break;
			case 2:
				if (i > 3)
					return 0;
				break;
			case 4:
			default:
				break;
			}
			if (dp_clocks[i] > mode_clock)
				return (i % 2) ? 270000 : 162000;
		}
	}

	return 0;
}

int dp_mode_valid(u8 dpcd[DP_DPCD_SIZE], int mode_clock)
{
	int lanes = dp_lanes_for_mode_clock(dpcd, mode_clock);
	int bw = dp_lanes_for_mode_clock(dpcd, mode_clock);

	if ((lanes == 0) || (bw == 0))
		return MODE_CLOCK_HIGH;

	return MODE_OK;
}

static u8 dp_link_status(u8 link_status[DP_LINK_STATUS_SIZE], int r)
{
	return link_status[r - DP_LANE0_1_STATUS];
}

static u8 dp_get_lane_status(u8 link_status[DP_LINK_STATUS_SIZE],
			     int lane)
{
	int i = DP_LANE0_1_STATUS + (lane >> 1);
	int s = (lane & 1) * 4;
	u8 l = dp_link_status(link_status, i);
	return (l >> s) & 0xf;
}

static bool dp_clock_recovery_ok(u8 link_status[DP_LINK_STATUS_SIZE],
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

static bool dp_channel_eq_ok(u8 link_status[DP_LINK_STATUS_SIZE],
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

static u8 dp_get_adjust_request_voltage(uint8_t link_status[DP_LINK_STATUS_SIZE],
					int lane)

{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT :
		 DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_VOLTAGE_SWING_SHIFT;
}

static u8 dp_get_adjust_request_pre_emphasis(uint8_t link_status[DP_LINK_STATUS_SIZE],
					     int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT :
		 DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_PRE_EMPHASIS_SHIFT;
}

/* XXX fix me -- chip specific */
#define DP_VOLTAGE_MAX         DP_TRAIN_VOLTAGE_SWING_1200
static u8 dp_pre_emphasis_max(u8 voltage_swing)
{
	switch (voltage_swing & DP_TRAIN_VOLTAGE_SWING_MASK) {
	case DP_TRAIN_VOLTAGE_SWING_400:
		return DP_TRAIN_PRE_EMPHASIS_6;
	case DP_TRAIN_VOLTAGE_SWING_600:
		return DP_TRAIN_PRE_EMPHASIS_6;
	case DP_TRAIN_VOLTAGE_SWING_800:
		return DP_TRAIN_PRE_EMPHASIS_3_5;
	case DP_TRAIN_VOLTAGE_SWING_1200:
	default:
		return DP_TRAIN_PRE_EMPHASIS_0;
	}
}

static void dp_get_adjust_train(u8 link_status[DP_LINK_STATUS_SIZE],
				int lane_count,
				u8 train_set[4])
{
	u8 v = 0;
	u8 p = 0;
	int lane;

	for (lane = 0; lane < lane_count; lane++) {
		u8 this_v = dp_get_adjust_request_voltage(link_status, lane);
		u8 this_p = dp_get_adjust_request_pre_emphasis(link_status, lane);

		DRM_DEBUG("requested signal parameters: lane %d voltage %s pre_emph %s\n",
			  lane,
			  voltage_names[this_v >> DP_TRAIN_VOLTAGE_SWING_SHIFT],
			  pre_emph_names[this_p >> DP_TRAIN_PRE_EMPHASIS_SHIFT]);

		if (this_v > v)
			v = this_v;
		if (this_p > p)
			p = this_p;
	}

	if (v >= DP_VOLTAGE_MAX)
		v = DP_VOLTAGE_MAX | DP_TRAIN_MAX_SWING_REACHED;

	if (p >= dp_pre_emphasis_max(v))
		p = dp_pre_emphasis_max(v) | DP_TRAIN_MAX_PRE_EMPHASIS_REACHED;

	DRM_DEBUG("using signal parameters: voltage %s pre_emph %s\n",
		  voltage_names[(v & DP_TRAIN_VOLTAGE_SWING_MASK) >> DP_TRAIN_VOLTAGE_SWING_SHIFT],
		  pre_emph_names[(p & DP_TRAIN_PRE_EMPHASIS_MASK) >> DP_TRAIN_PRE_EMPHASIS_SHIFT]);

	for (lane = 0; lane < 4; lane++)
		train_set[lane] = v | p;
}


/* radeon aux chan functions */
bool radeon_process_aux_ch(struct radeon_i2c_chan *chan, u8 *req_bytes,
			   int num_bytes, u8 *read_byte,
			   u8 read_buf_len, u8 delay)
{
	struct drm_device *dev = chan->dev;
	struct radeon_device *rdev = dev->dev_private;
	PROCESS_AUX_CHANNEL_TRANSACTION_PS_ALLOCATION args;
	int index = GetIndexIntoMasterTable(COMMAND, ProcessAuxChannelTransaction);
	unsigned char *base;
	int retry_count = 0;

	memset(&args, 0, sizeof(args));

	base = (unsigned char *)rdev->mode_info.atom_context->scratch;

retry:
	memcpy(base, req_bytes, num_bytes);

	args.lpAuxRequest = 0;
	args.lpDataOut = 16;
	args.ucDataOutLen = 0;
	args.ucChannelID = chan->rec.i2c_id;
	args.ucDelay = delay / 10;

	atom_execute_table(rdev->mode_info.atom_context, index, (uint32_t *)&args);

	if (args.ucReplyStatus && !args.ucDataOutLen) {
		if (args.ucReplyStatus == 0x20 && retry_count++ < 10)
			goto retry;
		DRM_DEBUG("failed to get auxch %02x%02x %02x %02x 0x%02x %02x after %d retries\n",
			  req_bytes[1], req_bytes[0], req_bytes[2], req_bytes[3],
			  chan->rec.i2c_id, args.ucReplyStatus, retry_count);
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

bool radeon_dp_aux_native_write(struct radeon_connector *radeon_connector, uint16_t address,
				uint8_t send_bytes, uint8_t *send)
{
	struct radeon_connector_atom_dig *dig_connector = radeon_connector->con_priv;
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
	ret = radeon_process_aux_ch(dig_connector->dp_i2c_bus, msg, msg_len, NULL, 0, 0);
	return ret;
}

bool radeon_dp_aux_native_read(struct radeon_connector *radeon_connector, uint16_t address,
			       uint8_t delay, uint8_t expected_bytes,
			       uint8_t *read_p)
{
	struct radeon_connector_atom_dig *dig_connector = radeon_connector->con_priv;
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

	ret = radeon_process_aux_ch(dig_connector->dp_i2c_bus, msg, msg_len, read_p, expected_bytes, delay);
	return ret;
}

/* radeon dp functions */
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
	struct radeon_connector_atom_dig *dig_connector = radeon_connector->con_priv;
	struct drm_device *dev = radeon_connector->base.dev;
	struct radeon_device *rdev = dev->dev_private;

	return radeon_dp_encoder_service(rdev, ATOM_DP_ACTION_GET_SINK_TYPE, 0,
					 dig_connector->dp_i2c_bus->rec.i2c_id, 0);
}

bool radeon_dp_getdpcd(struct radeon_connector *radeon_connector)
{
	struct radeon_connector_atom_dig *dig_connector = radeon_connector->con_priv;
	u8 msg[25];
	int ret;

	ret = radeon_dp_aux_native_read(radeon_connector, DP_DPCD_REV, 0, 8, msg);
	if (ret) {
		memcpy(dig_connector->dpcd, msg, 8);
		{
			int i;
			DRM_DEBUG("DPCD: ");
			for (i = 0; i < 8; i++)
				DRM_DEBUG("%02x ", msg[i]);
			DRM_DEBUG("\n");
		}
		return true;
	}
	dig_connector->dpcd[0] = 0;
	return false;
}

void radeon_dp_set_link_config(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	struct radeon_connector *radeon_connector;
	struct radeon_connector_atom_dig *dig_connector;

	if ((connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort) &&
	    (connector->connector_type != DRM_MODE_CONNECTOR_eDP))
		return;

	radeon_connector = to_radeon_connector(connector);
	if (!radeon_connector->con_priv)
		return;
	dig_connector = radeon_connector->con_priv;

	dig_connector->dp_clock =
		dp_link_clock_for_mode_clock(dig_connector->dpcd, mode->clock);
	dig_connector->dp_lane_count =
		dp_lanes_for_mode_clock(dig_connector->dpcd, mode->clock);
}

int radeon_dp_mode_valid_helper(struct radeon_connector *radeon_connector,
				struct drm_display_mode *mode)
{
	struct radeon_connector_atom_dig *dig_connector = radeon_connector->con_priv;

	return dp_mode_valid(dig_connector->dpcd, mode->clock);
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

	DRM_DEBUG("link status %02x %02x %02x %02x %02x %02x\n",
		  link_status[0], link_status[1], link_status[2],
		  link_status[3], link_status[4], link_status[5]);
	return true;
}

bool radeon_dp_needs_link_train(struct radeon_connector *radeon_connector)
{
	struct radeon_connector_atom_dig *dig_connector = radeon_connector->con_priv;
	u8 link_status[DP_LINK_STATUS_SIZE];

	if (!atom_dp_get_link_status(radeon_connector, link_status))
		return false;
	if (dp_channel_eq_ok(link_status, dig_connector->dp_lane_count))
		return false;
	return true;
}

static void dp_set_power(struct radeon_connector *radeon_connector, u8 power_state)
{
	struct radeon_connector_atom_dig *dig_connector = radeon_connector->con_priv;

	if (dig_connector->dpcd[0] >= 0x11) {
		radeon_dp_aux_native_write(radeon_connector, DP_SET_POWER, 1,
					   &power_state);
	}
}

static void dp_set_downspread(struct radeon_connector *radeon_connector, u8 downspread)
{
	radeon_dp_aux_native_write(radeon_connector, DP_DOWNSPREAD_CTRL, 1,
				   &downspread);
}

static void dp_set_link_bw_lanes(struct radeon_connector *radeon_connector,
				 u8 link_configuration[DP_LINK_CONFIGURATION_SIZE])
{
	radeon_dp_aux_native_write(radeon_connector, DP_LINK_BW_SET, 2,
				   link_configuration);
}

static void dp_update_dpvs_emph(struct radeon_connector *radeon_connector,
				struct drm_encoder *encoder,
				u8 train_set[4])
{
	struct radeon_connector_atom_dig *dig_connector = radeon_connector->con_priv;
	int i;

	for (i = 0; i < dig_connector->dp_lane_count; i++)
		atombios_dig_transmitter_setup(encoder,
					       ATOM_TRANSMITTER_ACTION_SETUP_VSEMPH,
					       i, train_set[i]);

	radeon_dp_aux_native_write(radeon_connector, DP_TRAINING_LANE0_SET,
				   dig_connector->dp_lane_count, train_set);
}

static void dp_set_training(struct radeon_connector *radeon_connector,
			    u8 training)
{
	radeon_dp_aux_native_write(radeon_connector, DP_TRAINING_PATTERN_SET,
				   1, &training);
}

void dp_link_train(struct drm_encoder *encoder,
		   struct drm_connector *connector)
{
	struct drm_device *dev = encoder->dev;
	struct radeon_device *rdev = dev->dev_private;
	struct radeon_encoder *radeon_encoder = to_radeon_encoder(encoder);
	struct radeon_encoder_atom_dig *dig;
	struct radeon_connector *radeon_connector;
	struct radeon_connector_atom_dig *dig_connector;
	int enc_id = 0;
	bool clock_recovery, channel_eq;
	u8 link_status[DP_LINK_STATUS_SIZE];
	u8 link_configuration[DP_LINK_CONFIGURATION_SIZE];
	u8 tries, voltage;
	u8 train_set[4];
	int i;

	if ((connector->connector_type != DRM_MODE_CONNECTOR_DisplayPort) &&
	    (connector->connector_type != DRM_MODE_CONNECTOR_eDP))
		return;

	if (!radeon_encoder->enc_priv)
		return;
	dig = radeon_encoder->enc_priv;

	radeon_connector = to_radeon_connector(connector);
	if (!radeon_connector->con_priv)
		return;
	dig_connector = radeon_connector->con_priv;

	if (dig->dig_encoder)
		enc_id |= ATOM_DP_CONFIG_DIG2_ENCODER;
	else
		enc_id |= ATOM_DP_CONFIG_DIG1_ENCODER;
	if (dig_connector->linkb)
		enc_id |= ATOM_DP_CONFIG_LINK_B;
	else
		enc_id |= ATOM_DP_CONFIG_LINK_A;

	memset(link_configuration, 0, DP_LINK_CONFIGURATION_SIZE);
	if (dig_connector->dp_clock == 270000)
		link_configuration[0] = DP_LINK_BW_2_7;
	else
		link_configuration[0] = DP_LINK_BW_1_62;
	link_configuration[1] = dig_connector->dp_lane_count;
	if (dig_connector->dpcd[0] >= 0x11)
		link_configuration[1] |= DP_LANE_COUNT_ENHANCED_FRAME_EN;

	/* power up the sink */
	dp_set_power(radeon_connector, DP_SET_POWER_D0);
	/* disable the training pattern on the sink */
	dp_set_training(radeon_connector, DP_TRAINING_PATTERN_DISABLE);
	/* set link bw and lanes on the sink */
	dp_set_link_bw_lanes(radeon_connector, link_configuration);
	/* disable downspread on the sink */
	dp_set_downspread(radeon_connector, 0);
	/* start training on the source */
	radeon_dp_encoder_service(rdev, ATOM_DP_ACTION_TRAINING_START,
				  dig_connector->dp_clock, enc_id, 0);
	/* set training pattern 1 on the source */
	radeon_dp_encoder_service(rdev, ATOM_DP_ACTION_TRAINING_PATTERN_SEL,
				  dig_connector->dp_clock, enc_id, 0);

	/* set initial vs/emph */
	memset(train_set, 0, 4);
	udelay(400);
	/* set training pattern 1 on the sink */
	dp_set_training(radeon_connector, DP_TRAINING_PATTERN_1);

	dp_update_dpvs_emph(radeon_connector, encoder, train_set);

	/* clock recovery loop */
	clock_recovery = false;
	tries = 0;
	voltage = 0xff;
	for (;;) {
		udelay(100);
		if (!atom_dp_get_link_status(radeon_connector, link_status))
			break;

		if (dp_clock_recovery_ok(link_status, dig_connector->dp_lane_count)) {
			clock_recovery = true;
			break;
		}

		for (i = 0; i < dig_connector->dp_lane_count; i++) {
			if ((train_set[i] & DP_TRAIN_MAX_SWING_REACHED) == 0)
				break;
		}
		if (i == dig_connector->dp_lane_count) {
			DRM_ERROR("clock recovery reached max voltage\n");
			break;
		}

		if ((train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK) == voltage) {
			++tries;
			if (tries == 5) {
				DRM_ERROR("clock recovery tried 5 times\n");
				break;
			}
		} else
			tries = 0;

		voltage = train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK;

		/* Compute new train_set as requested by sink */
		dp_get_adjust_train(link_status, dig_connector->dp_lane_count, train_set);
		dp_update_dpvs_emph(radeon_connector, encoder, train_set);
	}
	if (!clock_recovery)
		DRM_ERROR("clock recovery failed\n");
	else
		DRM_DEBUG("clock recovery at voltage %d pre-emphasis %d\n",
			  train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK,
			  (train_set[0] & DP_TRAIN_PRE_EMPHASIS_MASK) >>
			  DP_TRAIN_PRE_EMPHASIS_SHIFT);


	/* set training pattern 2 on the sink */
	dp_set_training(radeon_connector, DP_TRAINING_PATTERN_2);
	/* set training pattern 2 on the source */
	radeon_dp_encoder_service(rdev, ATOM_DP_ACTION_TRAINING_PATTERN_SEL,
				  dig_connector->dp_clock, enc_id, 1);

	/* channel equalization loop */
	tries = 0;
	channel_eq = false;
	for (;;) {
		udelay(400);
		if (!atom_dp_get_link_status(radeon_connector, link_status))
			break;

		if (dp_channel_eq_ok(link_status, dig_connector->dp_lane_count)) {
			channel_eq = true;
			break;
		}

		/* Try 5 times */
		if (tries > 5) {
			DRM_ERROR("channel eq failed: 5 tries\n");
			break;
		}

		/* Compute new train_set as requested by sink */
		dp_get_adjust_train(link_status, dig_connector->dp_lane_count, train_set);
		dp_update_dpvs_emph(radeon_connector, encoder, train_set);

		tries++;
	}

	if (!channel_eq)
		DRM_ERROR("channel eq failed\n");
	else
		DRM_DEBUG("channel eq at voltage %d pre-emphasis %d\n",
			  train_set[0] & DP_TRAIN_VOLTAGE_SWING_MASK,
			  (train_set[0] & DP_TRAIN_PRE_EMPHASIS_MASK)
			  >> DP_TRAIN_PRE_EMPHASIS_SHIFT);

	/* disable the training pattern on the sink */
	dp_set_training(radeon_connector, DP_TRAINING_PATTERN_DISABLE);

	radeon_dp_encoder_service(rdev, ATOM_DP_ACTION_TRAINING_COMPLETE,
				  dig_connector->dp_clock, enc_id, 0);
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

