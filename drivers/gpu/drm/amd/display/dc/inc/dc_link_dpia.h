/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef __DC_LINK_DPIA_H__
#define __DC_LINK_DPIA_H__

/* This module implements functionality for training DPIA links. */

struct dc_link;
struct dc_link_settings;

/* The approximate time (us) it takes to transmit 9 USB4 DP clock sync packets. */
#define DPIA_CLK_SYNC_DELAY 16000

/* Extend interval between training status checks for manual testing. */
#define DPIA_DEBUG_EXTENDED_AUX_RD_INTERVAL_US 60000000

/** @note Can remove once DP tunneling registers in upstream include/drm/drm_dp_helper.h */
/* DPCD DP Tunneling over USB4 */
#define DP_TUNNELING_CAPABILITIES_SUPPORT 0xe000d
#define DP_IN_ADAPTER_INFO                0xe000e
#define DP_USB4_DRIVER_ID                 0xe000f
#define DP_USB4_ROUTER_TOPOLOGY_ID        0xe001b

/* SET_CONFIG message types sent by driver. */
enum dpia_set_config_type {
	DPIA_SET_CFG_SET_LINK = 0x01,
	DPIA_SET_CFG_SET_PHY_TEST_MODE = 0x05,
	DPIA_SET_CFG_SET_TRAINING = 0x18,
	DPIA_SET_CFG_SET_VSPE = 0x19
};

/* Training stages (TS) in SET_CONFIG(SET_TRAINING) message. */
enum dpia_set_config_ts {
	DPIA_TS_DPRX_DONE = 0x00, /* Done training DPRX. */
	DPIA_TS_TPS1 = 0x01,
	DPIA_TS_TPS2 = 0x02,
	DPIA_TS_TPS3 = 0x03,
	DPIA_TS_TPS4 = 0x07,
	DPIA_TS_UFP_DONE = 0xff /* Done training DPTX-to-DPIA hop. */
};

/* SET_CONFIG message data associated with messages sent by driver. */
union dpia_set_config_data {
	struct {
		uint8_t mode : 1;
		uint8_t reserved : 7;
	} set_link;
	struct {
		uint8_t stage;
	} set_training;
	struct {
		uint8_t swing : 2;
		uint8_t max_swing_reached : 1;
		uint8_t pre_emph : 2;
		uint8_t max_pre_emph_reached : 1;
		uint8_t reserved : 2;
	} set_vspe;
	uint8_t raw;
};

/* Read tunneling device capability from DPCD and update link capability
 * accordingly.
 */
enum dc_status dpcd_get_tunneling_device_data(struct dc_link *link);

/* Train DP tunneling link for USB4 DPIA display endpoint.
 * DPIA equivalent of dc_link_dp_perfrorm_link_training.
 * Aborts link training upon detection of sink unplug.
 */
enum link_training_result dc_link_dpia_perform_link_training(
	struct dc_link *link,
	const struct link_resource *link_res,
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern);

#endif /* __DC_LINK_DPIA_H__ */
