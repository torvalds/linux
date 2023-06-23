// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2020, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt)	"[drm-dp] %s: " fmt, __func__

#include <drm/drm_print.h>

#include "dp_link.h"
#include "dp_panel.h"

#define DP_TEST_REQUEST_MASK		0x7F

enum audio_sample_rate {
	AUDIO_SAMPLE_RATE_32_KHZ	= 0x00,
	AUDIO_SAMPLE_RATE_44_1_KHZ	= 0x01,
	AUDIO_SAMPLE_RATE_48_KHZ	= 0x02,
	AUDIO_SAMPLE_RATE_88_2_KHZ	= 0x03,
	AUDIO_SAMPLE_RATE_96_KHZ	= 0x04,
	AUDIO_SAMPLE_RATE_176_4_KHZ	= 0x05,
	AUDIO_SAMPLE_RATE_192_KHZ	= 0x06,
};

enum audio_pattern_type {
	AUDIO_TEST_PATTERN_OPERATOR_DEFINED	= 0x00,
	AUDIO_TEST_PATTERN_SAWTOOTH		= 0x01,
};

struct dp_link_request {
	u32 test_requested;
	u32 test_link_rate;
	u32 test_lane_count;
};

struct dp_link_private {
	u32 prev_sink_count;
	struct device *dev;
	struct drm_device *drm_dev;
	struct drm_dp_aux *aux;
	struct dp_link dp_link;

	struct dp_link_request request;
	struct mutex psm_mutex;
	u8 link_status[DP_LINK_STATUS_SIZE];
};

static int dp_aux_link_power_up(struct drm_dp_aux *aux,
					struct dp_link_info *link)
{
	u8 value;
	int err;

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

	usleep_range(1000, 2000);

	return 0;
}

static int dp_aux_link_power_down(struct drm_dp_aux *aux,
					struct dp_link_info *link)
{
	u8 value;
	int err;

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

static int dp_link_get_period(struct dp_link_private *link, int const addr)
{
	int ret = 0;
	u8 data;
	u32 const max_audio_period = 0xA;

	/* TEST_AUDIO_PERIOD_CH_XX */
	if (drm_dp_dpcd_readb(link->aux, addr, &data) < 0) {
		DRM_ERROR("failed to read test_audio_period (0x%x)\n", addr);
		ret = -EINVAL;
		goto exit;
	}

	/* Period - Bits 3:0 */
	data = data & 0xF;
	if ((int)data > max_audio_period) {
		DRM_ERROR("invalid test_audio_period_ch_1 = 0x%x\n", data);
		ret = -EINVAL;
		goto exit;
	}

	ret = data;
exit:
	return ret;
}

static int dp_link_parse_audio_channel_period(struct dp_link_private *link)
{
	int ret = 0;
	struct dp_link_test_audio *req = &link->dp_link.test_audio;

	ret = dp_link_get_period(link, DP_TEST_AUDIO_PERIOD_CH1);
	if (ret == -EINVAL)
		goto exit;

	req->test_audio_period_ch_1 = ret;
	drm_dbg_dp(link->drm_dev, "test_audio_period_ch_1 = 0x%x\n", ret);

	ret = dp_link_get_period(link, DP_TEST_AUDIO_PERIOD_CH2);
	if (ret == -EINVAL)
		goto exit;

	req->test_audio_period_ch_2 = ret;
	drm_dbg_dp(link->drm_dev, "test_audio_period_ch_2 = 0x%x\n", ret);

	/* TEST_AUDIO_PERIOD_CH_3 (Byte 0x275) */
	ret = dp_link_get_period(link, DP_TEST_AUDIO_PERIOD_CH3);
	if (ret == -EINVAL)
		goto exit;

	req->test_audio_period_ch_3 = ret;
	drm_dbg_dp(link->drm_dev, "test_audio_period_ch_3 = 0x%x\n", ret);

	ret = dp_link_get_period(link, DP_TEST_AUDIO_PERIOD_CH4);
	if (ret == -EINVAL)
		goto exit;

	req->test_audio_period_ch_4 = ret;
	drm_dbg_dp(link->drm_dev, "test_audio_period_ch_4 = 0x%x\n", ret);

	ret = dp_link_get_period(link, DP_TEST_AUDIO_PERIOD_CH5);
	if (ret == -EINVAL)
		goto exit;

	req->test_audio_period_ch_5 = ret;
	drm_dbg_dp(link->drm_dev, "test_audio_period_ch_5 = 0x%x\n", ret);

	ret = dp_link_get_period(link, DP_TEST_AUDIO_PERIOD_CH6);
	if (ret == -EINVAL)
		goto exit;

	req->test_audio_period_ch_6 = ret;
	drm_dbg_dp(link->drm_dev, "test_audio_period_ch_6 = 0x%x\n", ret);

	ret = dp_link_get_period(link, DP_TEST_AUDIO_PERIOD_CH7);
	if (ret == -EINVAL)
		goto exit;

	req->test_audio_period_ch_7 = ret;
	drm_dbg_dp(link->drm_dev, "test_audio_period_ch_7 = 0x%x\n", ret);

	ret = dp_link_get_period(link, DP_TEST_AUDIO_PERIOD_CH8);
	if (ret == -EINVAL)
		goto exit;

	req->test_audio_period_ch_8 = ret;
	drm_dbg_dp(link->drm_dev, "test_audio_period_ch_8 = 0x%x\n", ret);
exit:
	return ret;
}

static int dp_link_parse_audio_pattern_type(struct dp_link_private *link)
{
	int ret = 0;
	u8 data;
	ssize_t rlen;
	int const max_audio_pattern_type = 0x1;

	rlen = drm_dp_dpcd_readb(link->aux,
				DP_TEST_AUDIO_PATTERN_TYPE, &data);
	if (rlen < 0) {
		DRM_ERROR("failed to read link audio mode. rlen=%zd\n", rlen);
		return rlen;
	}

	/* Audio Pattern Type - Bits 7:0 */
	if ((int)data > max_audio_pattern_type) {
		DRM_ERROR("invalid audio pattern type = 0x%x\n", data);
		ret = -EINVAL;
		goto exit;
	}

	link->dp_link.test_audio.test_audio_pattern_type = data;
	drm_dbg_dp(link->drm_dev, "audio pattern type = 0x%x\n", data);
exit:
	return ret;
}

static int dp_link_parse_audio_mode(struct dp_link_private *link)
{
	int ret = 0;
	u8 data;
	ssize_t rlen;
	int const max_audio_sampling_rate = 0x6;
	int const max_audio_channel_count = 0x8;
	int sampling_rate = 0x0;
	int channel_count = 0x0;

	rlen = drm_dp_dpcd_readb(link->aux, DP_TEST_AUDIO_MODE, &data);
	if (rlen < 0) {
		DRM_ERROR("failed to read link audio mode. rlen=%zd\n", rlen);
		return rlen;
	}

	/* Sampling Rate - Bits 3:0 */
	sampling_rate = data & 0xF;
	if (sampling_rate > max_audio_sampling_rate) {
		DRM_ERROR("sampling rate (0x%x) greater than max (0x%x)\n",
				sampling_rate, max_audio_sampling_rate);
		ret = -EINVAL;
		goto exit;
	}

	/* Channel Count - Bits 7:4 */
	channel_count = ((data & 0xF0) >> 4) + 1;
	if (channel_count > max_audio_channel_count) {
		DRM_ERROR("channel_count (0x%x) greater than max (0x%x)\n",
				channel_count, max_audio_channel_count);
		ret = -EINVAL;
		goto exit;
	}

	link->dp_link.test_audio.test_audio_sampling_rate = sampling_rate;
	link->dp_link.test_audio.test_audio_channel_count = channel_count;
	drm_dbg_dp(link->drm_dev,
			"sampling_rate = 0x%x, channel_count = 0x%x\n",
			sampling_rate, channel_count);
exit:
	return ret;
}

static int dp_link_parse_audio_pattern_params(struct dp_link_private *link)
{
	int ret = 0;

	ret = dp_link_parse_audio_mode(link);
	if (ret)
		goto exit;

	ret = dp_link_parse_audio_pattern_type(link);
	if (ret)
		goto exit;

	ret = dp_link_parse_audio_channel_period(link);

exit:
	return ret;
}

static bool dp_link_is_video_pattern_valid(u32 pattern)
{
	switch (pattern) {
	case DP_NO_TEST_PATTERN:
	case DP_COLOR_RAMP:
	case DP_BLACK_AND_WHITE_VERTICAL_LINES:
	case DP_COLOR_SQUARE:
		return true;
	default:
		return false;
	}
}

/**
 * dp_link_is_bit_depth_valid() - validates the bit depth requested
 * @tbd: bit depth requested by the sink
 *
 * Returns true if the requested bit depth is supported.
 */
static bool dp_link_is_bit_depth_valid(u32 tbd)
{
	/* DP_TEST_VIDEO_PATTERN_NONE is treated as invalid */
	switch (tbd) {
	case DP_TEST_BIT_DEPTH_6:
	case DP_TEST_BIT_DEPTH_8:
	case DP_TEST_BIT_DEPTH_10:
		return true;
	default:
		return false;
	}
}

static int dp_link_parse_timing_params1(struct dp_link_private *link,
					int addr, int len, u32 *val)
{
	u8 bp[2];
	int rlen;

	if (len != 2)
		return -EINVAL;

	/* Read the requested video link pattern (Byte 0x221). */
	rlen = drm_dp_dpcd_read(link->aux, addr, bp, len);
	if (rlen < len) {
		DRM_ERROR("failed to read 0x%x\n", addr);
		return -EINVAL;
	}

	*val = bp[1] | (bp[0] << 8);

	return 0;
}

static int dp_link_parse_timing_params2(struct dp_link_private *link,
					int addr, int len,
					u32 *val1, u32 *val2)
{
	u8 bp[2];
	int rlen;

	if (len != 2)
		return -EINVAL;

	/* Read the requested video link pattern (Byte 0x221). */
	rlen = drm_dp_dpcd_read(link->aux, addr, bp, len);
	if (rlen < len) {
		DRM_ERROR("failed to read 0x%x\n", addr);
		return -EINVAL;
	}

	*val1 = (bp[0] & BIT(7)) >> 7;
	*val2 = bp[1] | ((bp[0] & 0x7F) << 8);

	return 0;
}

static int dp_link_parse_timing_params3(struct dp_link_private *link,
					int addr, u32 *val)
{
	u8 bp;
	u32 len = 1;
	int rlen;

	rlen = drm_dp_dpcd_read(link->aux, addr, &bp, len);
	if (rlen < 1) {
		DRM_ERROR("failed to read 0x%x\n", addr);
		return -EINVAL;
	}
	*val = bp;

	return 0;
}

/**
 * dp_link_parse_video_pattern_params() - parses video pattern parameters from DPCD
 * @link: Display Port Driver data
 *
 * Returns 0 if it successfully parses the video link pattern and the link
 * bit depth requested by the sink and, and if the values parsed are valid.
 */
static int dp_link_parse_video_pattern_params(struct dp_link_private *link)
{
	int ret = 0;
	ssize_t rlen;
	u8 bp;

	rlen = drm_dp_dpcd_readb(link->aux, DP_TEST_PATTERN, &bp);
	if (rlen < 0) {
		DRM_ERROR("failed to read link video pattern. rlen=%zd\n",
			rlen);
		return rlen;
	}

	if (!dp_link_is_video_pattern_valid(bp)) {
		DRM_ERROR("invalid link video pattern = 0x%x\n", bp);
		ret = -EINVAL;
		return ret;
	}

	link->dp_link.test_video.test_video_pattern = bp;

	/* Read the requested color bit depth and dynamic range (Byte 0x232) */
	rlen = drm_dp_dpcd_readb(link->aux, DP_TEST_MISC0, &bp);
	if (rlen < 0) {
		DRM_ERROR("failed to read link bit depth. rlen=%zd\n", rlen);
		return rlen;
	}

	/* Dynamic Range */
	link->dp_link.test_video.test_dyn_range =
			(bp & DP_TEST_DYNAMIC_RANGE_CEA);

	/* Color bit depth */
	bp &= DP_TEST_BIT_DEPTH_MASK;
	if (!dp_link_is_bit_depth_valid(bp)) {
		DRM_ERROR("invalid link bit depth = 0x%x\n", bp);
		ret = -EINVAL;
		return ret;
	}

	link->dp_link.test_video.test_bit_depth = bp;

	/* resolution timing params */
	ret = dp_link_parse_timing_params1(link, DP_TEST_H_TOTAL_HI, 2,
			&link->dp_link.test_video.test_h_total);
	if (ret) {
		DRM_ERROR("failed to parse test_htotal(DP_TEST_H_TOTAL_HI)\n");
		return ret;
	}

	ret = dp_link_parse_timing_params1(link, DP_TEST_V_TOTAL_HI, 2,
			&link->dp_link.test_video.test_v_total);
	if (ret) {
		DRM_ERROR("failed to parse test_v_total(DP_TEST_V_TOTAL_HI)\n");
		return ret;
	}

	ret = dp_link_parse_timing_params1(link, DP_TEST_H_START_HI, 2,
			&link->dp_link.test_video.test_h_start);
	if (ret) {
		DRM_ERROR("failed to parse test_h_start(DP_TEST_H_START_HI)\n");
		return ret;
	}

	ret = dp_link_parse_timing_params1(link, DP_TEST_V_START_HI, 2,
			&link->dp_link.test_video.test_v_start);
	if (ret) {
		DRM_ERROR("failed to parse test_v_start(DP_TEST_V_START_HI)\n");
		return ret;
	}

	ret = dp_link_parse_timing_params2(link, DP_TEST_HSYNC_HI, 2,
			&link->dp_link.test_video.test_hsync_pol,
			&link->dp_link.test_video.test_hsync_width);
	if (ret) {
		DRM_ERROR("failed to parse (DP_TEST_HSYNC_HI)\n");
		return ret;
	}

	ret = dp_link_parse_timing_params2(link, DP_TEST_VSYNC_HI, 2,
			&link->dp_link.test_video.test_vsync_pol,
			&link->dp_link.test_video.test_vsync_width);
	if (ret) {
		DRM_ERROR("failed to parse (DP_TEST_VSYNC_HI)\n");
		return ret;
	}

	ret = dp_link_parse_timing_params1(link, DP_TEST_H_WIDTH_HI, 2,
			&link->dp_link.test_video.test_h_width);
	if (ret) {
		DRM_ERROR("failed to parse test_h_width(DP_TEST_H_WIDTH_HI)\n");
		return ret;
	}

	ret = dp_link_parse_timing_params1(link, DP_TEST_V_HEIGHT_HI, 2,
			&link->dp_link.test_video.test_v_height);
	if (ret) {
		DRM_ERROR("failed to parse test_v_height\n");
		return ret;
	}

	ret = dp_link_parse_timing_params3(link, DP_TEST_MISC1,
		&link->dp_link.test_video.test_rr_d);
	link->dp_link.test_video.test_rr_d &= DP_TEST_REFRESH_DENOMINATOR;
	if (ret) {
		DRM_ERROR("failed to parse test_rr_d (DP_TEST_MISC1)\n");
		return ret;
	}

	ret = dp_link_parse_timing_params3(link, DP_TEST_REFRESH_RATE_NUMERATOR,
		&link->dp_link.test_video.test_rr_n);
	if (ret) {
		DRM_ERROR("failed to parse test_rr_n\n");
		return ret;
	}

	drm_dbg_dp(link->drm_dev,
		"link video pattern = 0x%x\n"
		"link dynamic range = 0x%x\n"
		"link bit depth = 0x%x\n"
		"TEST_H_TOTAL = %d, TEST_V_TOTAL = %d\n"
		"TEST_H_START = %d, TEST_V_START = %d\n"
		"TEST_HSYNC_POL = %d\n"
		"TEST_HSYNC_WIDTH = %d\n"
		"TEST_VSYNC_POL = %d\n"
		"TEST_VSYNC_WIDTH = %d\n"
		"TEST_H_WIDTH = %d\n"
		"TEST_V_HEIGHT = %d\n"
		"TEST_REFRESH_DENOMINATOR = %d\n"
		 "TEST_REFRESH_NUMERATOR = %d\n",
		link->dp_link.test_video.test_video_pattern,
		link->dp_link.test_video.test_dyn_range,
		link->dp_link.test_video.test_bit_depth,
		link->dp_link.test_video.test_h_total,
		link->dp_link.test_video.test_v_total,
		link->dp_link.test_video.test_h_start,
		link->dp_link.test_video.test_v_start,
		link->dp_link.test_video.test_hsync_pol,
		link->dp_link.test_video.test_hsync_width,
		link->dp_link.test_video.test_vsync_pol,
		link->dp_link.test_video.test_vsync_width,
		link->dp_link.test_video.test_h_width,
		link->dp_link.test_video.test_v_height,
		link->dp_link.test_video.test_rr_d,
		link->dp_link.test_video.test_rr_n);

	return ret;
}

/**
 * dp_link_parse_link_training_params() - parses link training parameters from
 * DPCD
 * @link: Display Port Driver data
 *
 * Returns 0 if it successfully parses the link rate (Byte 0x219) and lane
 * count (Byte 0x220), and if these values parse are valid.
 */
static int dp_link_parse_link_training_params(struct dp_link_private *link)
{
	u8 bp;
	ssize_t rlen;

	rlen = drm_dp_dpcd_readb(link->aux, DP_TEST_LINK_RATE,	&bp);
	if (rlen < 0) {
		DRM_ERROR("failed to read link rate. rlen=%zd\n", rlen);
		return rlen;
	}

	if (!is_link_rate_valid(bp)) {
		DRM_ERROR("invalid link rate = 0x%x\n", bp);
		return -EINVAL;
	}

	link->request.test_link_rate = bp;
	drm_dbg_dp(link->drm_dev, "link rate = 0x%x\n",
				link->request.test_link_rate);

	rlen = drm_dp_dpcd_readb(link->aux, DP_TEST_LANE_COUNT, &bp);
	if (rlen < 0) {
		DRM_ERROR("failed to read lane count. rlen=%zd\n", rlen);
		return rlen;
	}
	bp &= DP_MAX_LANE_COUNT_MASK;

	if (!is_lane_count_valid(bp)) {
		DRM_ERROR("invalid lane count = 0x%x\n", bp);
		return -EINVAL;
	}

	link->request.test_lane_count = bp;
	drm_dbg_dp(link->drm_dev, "lane count = 0x%x\n",
				link->request.test_lane_count);
	return 0;
}

/**
 * dp_link_parse_phy_test_params() - parses the phy link parameters
 * @link: Display Port Driver data
 *
 * Parses the DPCD (Byte 0x248) for the DP PHY link pattern that is being
 * requested.
 */
static int dp_link_parse_phy_test_params(struct dp_link_private *link)
{
	u8 data;
	ssize_t rlen;

	rlen = drm_dp_dpcd_readb(link->aux, DP_PHY_TEST_PATTERN,
					&data);
	if (rlen < 0) {
		DRM_ERROR("failed to read phy link pattern. rlen=%zd\n", rlen);
		return rlen;
	}

	link->dp_link.phy_params.phy_test_pattern_sel = data & 0x07;

	drm_dbg_dp(link->drm_dev, "phy_test_pattern_sel = 0x%x\n", data);

	switch (data) {
	case DP_PHY_TEST_PATTERN_SEL_MASK:
	case DP_PHY_TEST_PATTERN_NONE:
	case DP_PHY_TEST_PATTERN_D10_2:
	case DP_PHY_TEST_PATTERN_ERROR_COUNT:
	case DP_PHY_TEST_PATTERN_PRBS7:
	case DP_PHY_TEST_PATTERN_80BIT_CUSTOM:
	case DP_PHY_TEST_PATTERN_CP2520:
		return 0;
	default:
		return -EINVAL;
	}
}

/**
 * dp_link_is_video_audio_test_requested() - checks for audio/video link request
 * @link: link requested by the sink
 *
 * Returns true if the requested link is a permitted audio/video link.
 */
static bool dp_link_is_video_audio_test_requested(u32 link)
{
	u8 video_audio_test = (DP_TEST_LINK_VIDEO_PATTERN |
				DP_TEST_LINK_AUDIO_PATTERN |
				DP_TEST_LINK_AUDIO_DISABLED_VIDEO);

	return ((link & video_audio_test) &&
		!(link & ~video_audio_test));
}

/**
 * dp_link_parse_request() - parses link request parameters from sink
 * @link: Display Port Driver data
 *
 * Parses the DPCD to check if an automated link is requested (Byte 0x201),
 * and what type of link automation is being requested (Byte 0x218).
 */
static int dp_link_parse_request(struct dp_link_private *link)
{
	int ret = 0;
	u8 data;
	ssize_t rlen;

	/**
	 * Read the device service IRQ vector (Byte 0x201) to determine
	 * whether an automated link has been requested by the sink.
	 */
	rlen = drm_dp_dpcd_readb(link->aux,
				DP_DEVICE_SERVICE_IRQ_VECTOR, &data);
	if (rlen < 0) {
		DRM_ERROR("aux read failed. rlen=%zd\n", rlen);
		return rlen;
	}

	drm_dbg_dp(link->drm_dev, "device service irq vector = 0x%x\n", data);

	if (!(data & DP_AUTOMATED_TEST_REQUEST)) {
		drm_dbg_dp(link->drm_dev, "no test requested\n");
		return 0;
	}

	/**
	 * Read the link request byte (Byte 0x218) to determine what type
	 * of automated link has been requested by the sink.
	 */
	rlen = drm_dp_dpcd_readb(link->aux, DP_TEST_REQUEST, &data);
	if (rlen < 0) {
		DRM_ERROR("aux read failed. rlen=%zd\n", rlen);
		return rlen;
	}

	if (!data || (data == DP_TEST_LINK_FAUX_PATTERN)) {
		drm_dbg_dp(link->drm_dev, "link 0x%x not supported\n", data);
		goto end;
	}

	drm_dbg_dp(link->drm_dev, "Test:(0x%x) requested\n", data);
	link->request.test_requested = data;
	if (link->request.test_requested == DP_TEST_LINK_PHY_TEST_PATTERN) {
		ret = dp_link_parse_phy_test_params(link);
		if (ret)
			goto end;
		ret = dp_link_parse_link_training_params(link);
		if (ret)
			goto end;
	}

	if (link->request.test_requested == DP_TEST_LINK_TRAINING) {
		ret = dp_link_parse_link_training_params(link);
		if (ret)
			goto end;
	}

	if (dp_link_is_video_audio_test_requested(
			link->request.test_requested)) {
		ret = dp_link_parse_video_pattern_params(link);
		if (ret)
			goto end;

		ret = dp_link_parse_audio_pattern_params(link);
	}
end:
	/*
	 * Send a DP_TEST_ACK if all link parameters are valid, otherwise send
	 * a DP_TEST_NAK.
	 */
	if (ret) {
		link->dp_link.test_response = DP_TEST_NAK;
	} else {
		if (link->request.test_requested != DP_TEST_LINK_EDID_READ)
			link->dp_link.test_response = DP_TEST_ACK;
		else
			link->dp_link.test_response =
				DP_TEST_EDID_CHECKSUM_WRITE;
	}

	return ret;
}

/**
 * dp_link_parse_sink_count() - parses the sink count
 * @dp_link: pointer to link module data
 *
 * Parses the DPCD to check if there is an update to the sink count
 * (Byte 0x200), and whether all the sink devices connected have Content
 * Protection enabled.
 */
static int dp_link_parse_sink_count(struct dp_link *dp_link)
{
	ssize_t rlen;
	bool cp_ready;

	struct dp_link_private *link = container_of(dp_link,
			struct dp_link_private, dp_link);

	rlen = drm_dp_dpcd_readb(link->aux, DP_SINK_COUNT,
				 &link->dp_link.sink_count);
	if (rlen < 0) {
		DRM_ERROR("sink count read failed. rlen=%zd\n", rlen);
		return rlen;
	}

	cp_ready = link->dp_link.sink_count & DP_SINK_CP_READY;

	link->dp_link.sink_count =
		DP_GET_SINK_COUNT(link->dp_link.sink_count);

	drm_dbg_dp(link->drm_dev, "sink_count = 0x%x, cp_ready = 0x%x\n",
				link->dp_link.sink_count, cp_ready);
	return 0;
}

static int dp_link_parse_sink_status_field(struct dp_link_private *link)
{
	int len = 0;

	link->prev_sink_count = link->dp_link.sink_count;
	len = dp_link_parse_sink_count(&link->dp_link);
	if (len < 0) {
		DRM_ERROR("DP parse sink count failed\n");
		return len;
	}

	len = drm_dp_dpcd_read_link_status(link->aux,
		link->link_status);
	if (len < DP_LINK_STATUS_SIZE) {
		DRM_ERROR("DP link status read failed\n");
		return len;
	}

	return dp_link_parse_request(link);
}

/**
 * dp_link_process_link_training_request() - processes new training requests
 * @link: Display Port link data
 *
 * This function will handle new link training requests that are initiated by
 * the sink. In particular, it will update the requested lane count and link
 * rate, and then trigger the link retraining procedure.
 *
 * The function will return 0 if a link training request has been processed,
 * otherwise it will return -EINVAL.
 */
static int dp_link_process_link_training_request(struct dp_link_private *link)
{
	if (link->request.test_requested != DP_TEST_LINK_TRAINING)
		return -EINVAL;

	drm_dbg_dp(link->drm_dev,
			"Test:0x%x link rate = 0x%x, lane count = 0x%x\n",
			DP_TEST_LINK_TRAINING,
			link->request.test_link_rate,
			link->request.test_lane_count);

	link->dp_link.link_params.num_lanes = link->request.test_lane_count;
	link->dp_link.link_params.rate =
		drm_dp_bw_code_to_link_rate(link->request.test_link_rate);

	return 0;
}

bool dp_link_send_test_response(struct dp_link *dp_link)
{
	struct dp_link_private *link = NULL;
	int ret = 0;

	if (!dp_link) {
		DRM_ERROR("invalid input\n");
		return false;
	}

	link = container_of(dp_link, struct dp_link_private, dp_link);

	ret = drm_dp_dpcd_writeb(link->aux, DP_TEST_RESPONSE,
			dp_link->test_response);

	return ret == 1;
}

int dp_link_psm_config(struct dp_link *dp_link,
			      struct dp_link_info *link_info, bool enable)
{
	struct dp_link_private *link = NULL;
	int ret = 0;

	if (!dp_link) {
		DRM_ERROR("invalid params\n");
		return -EINVAL;
	}

	link = container_of(dp_link, struct dp_link_private, dp_link);

	mutex_lock(&link->psm_mutex);
	if (enable)
		ret = dp_aux_link_power_down(link->aux, link_info);
	else
		ret = dp_aux_link_power_up(link->aux, link_info);

	if (ret)
		DRM_ERROR("Failed to %s low power mode\n", enable ?
							"enter" : "exit");
	else
		dp_link->psm_enabled = enable;

	mutex_unlock(&link->psm_mutex);
	return ret;
}

bool dp_link_send_edid_checksum(struct dp_link *dp_link, u8 checksum)
{
	struct dp_link_private *link = NULL;
	int ret = 0;

	if (!dp_link) {
		DRM_ERROR("invalid input\n");
		return false;
	}

	link = container_of(dp_link, struct dp_link_private, dp_link);

	ret = drm_dp_dpcd_writeb(link->aux, DP_TEST_EDID_CHECKSUM,
						checksum);
	return ret == 1;
}

static void dp_link_parse_vx_px(struct dp_link_private *link)
{
	drm_dbg_dp(link->drm_dev, "vx: 0=%d, 1=%d, 2=%d, 3=%d\n",
		drm_dp_get_adjust_request_voltage(link->link_status, 0),
		drm_dp_get_adjust_request_voltage(link->link_status, 1),
		drm_dp_get_adjust_request_voltage(link->link_status, 2),
		drm_dp_get_adjust_request_voltage(link->link_status, 3));

	drm_dbg_dp(link->drm_dev, "px: 0=%d, 1=%d, 2=%d, 3=%d\n",
		drm_dp_get_adjust_request_pre_emphasis(link->link_status, 0),
		drm_dp_get_adjust_request_pre_emphasis(link->link_status, 1),
		drm_dp_get_adjust_request_pre_emphasis(link->link_status, 2),
		drm_dp_get_adjust_request_pre_emphasis(link->link_status, 3));

	/**
	 * Update the voltage and pre-emphasis levels as per DPCD request
	 * vector.
	 */
	drm_dbg_dp(link->drm_dev,
			 "Current: v_level = 0x%x, p_level = 0x%x\n",
			link->dp_link.phy_params.v_level,
			link->dp_link.phy_params.p_level);
	link->dp_link.phy_params.v_level =
		drm_dp_get_adjust_request_voltage(link->link_status, 0);
	link->dp_link.phy_params.p_level =
		drm_dp_get_adjust_request_pre_emphasis(link->link_status, 0);

	link->dp_link.phy_params.p_level >>= DP_TRAIN_PRE_EMPHASIS_SHIFT;

	drm_dbg_dp(link->drm_dev,
			"Requested: v_level = 0x%x, p_level = 0x%x\n",
			link->dp_link.phy_params.v_level,
			link->dp_link.phy_params.p_level);
}

/**
 * dp_link_process_phy_test_pattern_request() - process new phy link requests
 * @link: Display Port Driver data
 *
 * This function will handle new phy link pattern requests that are initiated
 * by the sink. The function will return 0 if a phy link pattern has been
 * processed, otherwise it will return -EINVAL.
 */
static int dp_link_process_phy_test_pattern_request(
		struct dp_link_private *link)
{
	if (!(link->request.test_requested & DP_TEST_LINK_PHY_TEST_PATTERN)) {
		drm_dbg_dp(link->drm_dev, "no phy test\n");
		return -EINVAL;
	}

	if (!is_link_rate_valid(link->request.test_link_rate) ||
		!is_lane_count_valid(link->request.test_lane_count)) {
		DRM_ERROR("Invalid: link rate = 0x%x,lane count = 0x%x\n",
				link->request.test_link_rate,
				link->request.test_lane_count);
		return -EINVAL;
	}

	drm_dbg_dp(link->drm_dev,
			"Current: rate = 0x%x, lane count = 0x%x\n",
			link->dp_link.link_params.rate,
			link->dp_link.link_params.num_lanes);

	drm_dbg_dp(link->drm_dev,
			"Requested: rate = 0x%x, lane count = 0x%x\n",
			link->request.test_link_rate,
			link->request.test_lane_count);

	link->dp_link.link_params.num_lanes = link->request.test_lane_count;
	link->dp_link.link_params.rate =
		drm_dp_bw_code_to_link_rate(link->request.test_link_rate);

	dp_link_parse_vx_px(link);

	return 0;
}

static u8 get_link_status(const u8 link_status[DP_LINK_STATUS_SIZE], int r)
{
	return link_status[r - DP_LANE0_1_STATUS];
}

/**
 * dp_link_process_link_status_update() - processes link status updates
 * @link: Display Port link module data
 *
 * This function will check for changes in the link status, e.g. clock
 * recovery done on all lanes, and trigger link training if there is a
 * failure/error on the link.
 *
 * The function will return 0 if the a link status update has been processed,
 * otherwise it will return -EINVAL.
 */
static int dp_link_process_link_status_update(struct dp_link_private *link)
{
	bool channel_eq_done = drm_dp_channel_eq_ok(link->link_status,
			link->dp_link.link_params.num_lanes);

	bool clock_recovery_done = drm_dp_clock_recovery_ok(link->link_status,
			link->dp_link.link_params.num_lanes);

	drm_dbg_dp(link->drm_dev,
		       "channel_eq_done = %d, clock_recovery_done = %d\n",
                        channel_eq_done, clock_recovery_done);

	if (channel_eq_done && clock_recovery_done)
		return -EINVAL;

	return 0;
}

/**
 * dp_link_process_ds_port_status_change() - process port status changes
 * @link: Display Port Driver data
 *
 * This function will handle downstream port updates that are initiated by
 * the sink. If the downstream port status has changed, the EDID is read via
 * AUX.
 *
 * The function will return 0 if a downstream port update has been
 * processed, otherwise it will return -EINVAL.
 */
static int dp_link_process_ds_port_status_change(struct dp_link_private *link)
{
	if (get_link_status(link->link_status, DP_LANE_ALIGN_STATUS_UPDATED) &
					DP_DOWNSTREAM_PORT_STATUS_CHANGED)
		goto reset;

	if (link->prev_sink_count == link->dp_link.sink_count)
		return -EINVAL;

reset:
	/* reset prev_sink_count */
	link->prev_sink_count = link->dp_link.sink_count;

	return 0;
}

static bool dp_link_is_video_pattern_requested(struct dp_link_private *link)
{
	return (link->request.test_requested & DP_TEST_LINK_VIDEO_PATTERN)
		&& !(link->request.test_requested &
		DP_TEST_LINK_AUDIO_DISABLED_VIDEO);
}

static bool dp_link_is_audio_pattern_requested(struct dp_link_private *link)
{
	return (link->request.test_requested & DP_TEST_LINK_AUDIO_PATTERN);
}

static void dp_link_reset_data(struct dp_link_private *link)
{
	link->request = (const struct dp_link_request){ 0 };
	link->dp_link.test_video = (const struct dp_link_test_video){ 0 };
	link->dp_link.test_video.test_bit_depth = DP_TEST_BIT_DEPTH_UNKNOWN;
	link->dp_link.test_audio = (const struct dp_link_test_audio){ 0 };
	link->dp_link.phy_params.phy_test_pattern_sel = 0;
	link->dp_link.sink_request = 0;
	link->dp_link.test_response = 0;
}

/**
 * dp_link_process_request() - handle HPD IRQ transition to HIGH
 * @dp_link: pointer to link module data
 *
 * This function will handle the HPD IRQ state transitions from LOW to HIGH
 * (including cases when there are back to back HPD IRQ HIGH) indicating
 * the start of a new link training request or sink status update.
 */
int dp_link_process_request(struct dp_link *dp_link)
{
	int ret = 0;
	struct dp_link_private *link;

	if (!dp_link) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	link = container_of(dp_link, struct dp_link_private, dp_link);

	dp_link_reset_data(link);

	ret = dp_link_parse_sink_status_field(link);
	if (ret)
		return ret;

	if (link->request.test_requested == DP_TEST_LINK_EDID_READ) {
		dp_link->sink_request |= DP_TEST_LINK_EDID_READ;
	} else if (!dp_link_process_ds_port_status_change(link)) {
		dp_link->sink_request |= DS_PORT_STATUS_CHANGED;
	} else if (!dp_link_process_link_training_request(link)) {
		dp_link->sink_request |= DP_TEST_LINK_TRAINING;
	} else if (!dp_link_process_phy_test_pattern_request(link)) {
		dp_link->sink_request |= DP_TEST_LINK_PHY_TEST_PATTERN;
	} else {
		ret = dp_link_process_link_status_update(link);
		if (!ret) {
			dp_link->sink_request |= DP_LINK_STATUS_UPDATED;
		} else {
			if (dp_link_is_video_pattern_requested(link)) {
				ret = 0;
				dp_link->sink_request |= DP_TEST_LINK_VIDEO_PATTERN;
			}
			if (dp_link_is_audio_pattern_requested(link)) {
				dp_link->sink_request |= DP_TEST_LINK_AUDIO_PATTERN;
				ret = -EINVAL;
			}
		}
	}

	drm_dbg_dp(link->drm_dev, "sink request=%#x",
				dp_link->sink_request);
	return ret;
}

int dp_link_get_colorimetry_config(struct dp_link *dp_link)
{
	u32 cc;
	struct dp_link_private *link;

	if (!dp_link) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	link = container_of(dp_link, struct dp_link_private, dp_link);

	/*
	 * Unless a video pattern CTS test is ongoing, use RGB_VESA
	 * Only RGB_VESA and RGB_CEA supported for now
	 */
	if (dp_link_is_video_pattern_requested(link))
		cc = link->dp_link.test_video.test_dyn_range;
	else
		cc = DP_TEST_DYNAMIC_RANGE_VESA;

	return cc;
}

int dp_link_adjust_levels(struct dp_link *dp_link, u8 *link_status)
{
	int i;
	int v_max = 0, p_max = 0;
	struct dp_link_private *link;

	if (!dp_link) {
		DRM_ERROR("invalid input\n");
		return -EINVAL;
	}

	link = container_of(dp_link, struct dp_link_private, dp_link);

	/* use the max level across lanes */
	for (i = 0; i < dp_link->link_params.num_lanes; i++) {
		u8 data_v = drm_dp_get_adjust_request_voltage(link_status, i);
		u8 data_p = drm_dp_get_adjust_request_pre_emphasis(link_status,
									 i);
		drm_dbg_dp(link->drm_dev,
				"lane=%d req_vol_swing=%d req_pre_emphasis=%d\n",
				i, data_v, data_p);
		if (v_max < data_v)
			v_max = data_v;
		if (p_max < data_p)
			p_max = data_p;
	}

	dp_link->phy_params.v_level = v_max >> DP_TRAIN_VOLTAGE_SWING_SHIFT;
	dp_link->phy_params.p_level = p_max >> DP_TRAIN_PRE_EMPHASIS_SHIFT;

	/**
	 * Adjust the voltage swing and pre-emphasis level combination to within
	 * the allowable range.
	 */
	if (dp_link->phy_params.v_level > DP_TRAIN_VOLTAGE_SWING_MAX) {
		drm_dbg_dp(link->drm_dev,
			"Requested vSwingLevel=%d, change to %d\n",
			dp_link->phy_params.v_level,
			DP_TRAIN_VOLTAGE_SWING_MAX);
		dp_link->phy_params.v_level = DP_TRAIN_VOLTAGE_SWING_MAX;
	}

	if (dp_link->phy_params.p_level > DP_TRAIN_PRE_EMPHASIS_MAX) {
		drm_dbg_dp(link->drm_dev,
			"Requested preEmphasisLevel=%d, change to %d\n",
			dp_link->phy_params.p_level,
			DP_TRAIN_PRE_EMPHASIS_MAX);
		dp_link->phy_params.p_level = DP_TRAIN_PRE_EMPHASIS_MAX;
	}

	if ((dp_link->phy_params.p_level > DP_TRAIN_PRE_EMPHASIS_LVL_1)
		&& (dp_link->phy_params.v_level ==
			DP_TRAIN_VOLTAGE_SWING_LVL_2)) {
		drm_dbg_dp(link->drm_dev,
			"Requested preEmphasisLevel=%d, change to %d\n",
			dp_link->phy_params.p_level,
			DP_TRAIN_PRE_EMPHASIS_LVL_1);
		dp_link->phy_params.p_level = DP_TRAIN_PRE_EMPHASIS_LVL_1;
	}

	drm_dbg_dp(link->drm_dev, "adjusted: v_level=%d, p_level=%d\n",
		dp_link->phy_params.v_level, dp_link->phy_params.p_level);

	return 0;
}

void dp_link_reset_phy_params_vx_px(struct dp_link *dp_link)
{
	dp_link->phy_params.v_level = 0;
	dp_link->phy_params.p_level = 0;
}

u32 dp_link_get_test_bits_depth(struct dp_link *dp_link, u32 bpp)
{
	u32 tbd;

	/*
	 * Few simplistic rules and assumptions made here:
	 *    1. Test bit depth is bit depth per color component
	 *    2. Assume 3 color components
	 */
	switch (bpp) {
	case 18:
		tbd = DP_TEST_BIT_DEPTH_6;
		break;
	case 24:
		tbd = DP_TEST_BIT_DEPTH_8;
		break;
	case 30:
		tbd = DP_TEST_BIT_DEPTH_10;
		break;
	default:
		tbd = DP_TEST_BIT_DEPTH_UNKNOWN;
		break;
	}

	if (tbd != DP_TEST_BIT_DEPTH_UNKNOWN)
		tbd = (tbd >> DP_TEST_BIT_DEPTH_SHIFT);

	return tbd;
}

struct dp_link *dp_link_get(struct device *dev, struct drm_dp_aux *aux)
{
	struct dp_link_private *link;
	struct dp_link *dp_link;

	if (!dev || !aux) {
		DRM_ERROR("invalid input\n");
		return ERR_PTR(-EINVAL);
	}

	link = devm_kzalloc(dev, sizeof(*link), GFP_KERNEL);
	if (!link)
		return ERR_PTR(-ENOMEM);

	link->dev   = dev;
	link->aux   = aux;

	mutex_init(&link->psm_mutex);
	dp_link = &link->dp_link;

	return dp_link;
}
