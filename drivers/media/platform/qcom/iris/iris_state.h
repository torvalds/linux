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

#define IRIS_INST_SUB_STATES		8
#define IRIS_INST_MAX_SUB_STATE_VALUE	((1 << IRIS_INST_SUB_STATES) - 1)

/**
 * enum iris_inst_sub_state
 *
 * @IRIS_INST_SUB_FIRST_IPSC: indicates source change is received from firmware
 *			     when output port is not yet streaming.
 * @IRIS_INST_SUB_DRC: indicates source change is received from firmware
 *		      when output port is streaming and source change event is
 *		      sent to client.
 * @IRIS_INST_SUB_DRC_LAST: indicates last buffer is received from firmware
 *                         as part of source change.
 * @IRIS_INST_SUB_DRAIN: indicates drain is in progress.
 * @IRIS_INST_SUB_DRAIN_LAST: indicates last buffer is received from firmware
 *                           as part of drain sequence.
 * @IRIS_INST_SUB_INPUT_PAUSE: source change is received form firmware. This
 *                            indicates that firmware is paused to process
 *                            any further input frames.
 * @IRIS_INST_SUB_OUTPUT_PAUSE: last buffer is received form firmware as part
 *                             of drc sequence. This indicates that
 *                             firmware is paused to process any further output frames.
 * @IRIS_INST_SUB_LOAD_RESOURCES: indicates all the resources have been loaded by the
 *                               firmware and it is ready for processing.
 */
enum iris_inst_sub_state {
	IRIS_INST_SUB_FIRST_IPSC	= BIT(0),
	IRIS_INST_SUB_DRC		= BIT(1),
	IRIS_INST_SUB_DRC_LAST		= BIT(2),
	IRIS_INST_SUB_DRAIN		= BIT(3),
	IRIS_INST_SUB_DRAIN_LAST	= BIT(4),
	IRIS_INST_SUB_INPUT_PAUSE	= BIT(5),
	IRIS_INST_SUB_OUTPUT_PAUSE	= BIT(6),
	IRIS_INST_SUB_LOAD_RESOURCES	= BIT(7),
};

int iris_inst_change_state(struct iris_inst *inst,
			   enum iris_inst_state request_state);
int iris_inst_change_sub_state(struct iris_inst *inst,
			       enum iris_inst_sub_state clear_sub_state,
			       enum iris_inst_sub_state set_sub_state);

int iris_inst_state_change_streamon(struct iris_inst *inst, u32 plane);
int iris_inst_state_change_streamoff(struct iris_inst *inst, u32 plane);
int iris_inst_sub_state_change_drc(struct iris_inst *inst);
int iris_inst_sub_state_change_drain_last(struct iris_inst *inst);
int iris_inst_sub_state_change_drc_last(struct iris_inst *inst);
int iris_inst_sub_state_change_pause(struct iris_inst *inst, u32 plane);
bool iris_allow_cmd(struct iris_inst *inst, u32 cmd);
bool iris_drc_pending(struct iris_inst *inst);
bool iris_drain_pending(struct iris_inst *inst);

#endif
