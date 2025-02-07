/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_STATE_H__
#define __IRIS_STATE_H__

struct iris_inst;

/**
 * enum iris_core_state
 *
 * @IRIS_CORE_DEINIT: default state.
 * @IRIS_CORE_INIT:   core state with core initialized. FW loaded and
 *                   HW brought out of reset, shared queues established
 *                   between host driver and firmware.
 * @IRIS_CORE_ERROR:  error state.
 *
 *        -----------
 *             |
 *             V
 *        -----------
 *   +--->| DEINIT  |<---+
 *   |   -----------    |
 *   |         |        |
 *   |         v        |
 *   |   -----------    |
 *   |     /     \      |
 *   |    /       \     |
 *   |   /         \    |
 *   |  v           v   v
 * -----------    -----------
 * |  INIT  |--->|  ERROR  |
 * -----------    -----------
 */
enum iris_core_state {
	IRIS_CORE_DEINIT,
	IRIS_CORE_INIT,
	IRIS_CORE_ERROR,
};

/**
 * enum iris_inst_state
 *
 * @IRIS_INST_INIT: video instance is opened.
 * @IRIS_INST_INPUT_STREAMING: stream on is completed on output plane.
 * @IRIS_INST_OUTPUT_STREAMING: stream on is completed on capture plane.
 * @IRIS_INST_STREAMING: stream on is completed on both output and capture planes.
 * @IRIS_INST_DEINIT: video instance is closed.
 * @IRIS_INST_ERROR: error state.
 *                    |
 *                    V
 *             -------------
 *   +--------|     INIT    |----------+
 *   |         -------------           |
 *   |            ^   ^                |
 *   |           /      \              |
 *   |          /        \             |
 *   |         v          v            |
 *   |   -----------    -----------    |
 *   |   |   INPUT         OUTPUT  |   |
 *   |---| STREAMING     STREAMING |---|
 *   |   -----------    -----------    |
 *   |       ^            ^            |
 *   |         \          /            |
 *   |          \        /             |
 *   |           v      v              |
 *   |         -------------           |
 *   |--------|  STREAMING |-----------|
 *   |        -------------            |
 *   |               |                 |
 *   |               |                 |
 *   |               v                 |
 *   |          -----------            |
 *   +-------->|  DEINIT   |<----------+
 *   |          -----------            |
 *   |               |                 |
 *   |               |                 |
 *   |               v                 |
 *   |          ----------             |
 *   +-------->|   ERROR |<------------+
 *              ----------
 */
enum iris_inst_state {
	IRIS_INST_DEINIT,
	IRIS_INST_INIT,
	IRIS_INST_INPUT_STREAMING,
	IRIS_INST_OUTPUT_STREAMING,
	IRIS_INST_STREAMING,
	IRIS_INST_ERROR,
};

int iris_inst_change_state(struct iris_inst *inst,
			   enum iris_inst_state request_state);
int iris_inst_state_change_streamon(struct iris_inst *inst, u32 plane);
int iris_inst_state_change_streamoff(struct iris_inst *inst, u32 plane);

#endif
