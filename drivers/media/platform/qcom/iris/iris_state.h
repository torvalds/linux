/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_STATE_H__
#define __IRIS_STATE_H__

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

#endif
