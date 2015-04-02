/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005, 2006, 2007, 2008 Mellanox Technologies.
 * All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef MLX4_FW_QOS_H
#define MLX4_FW_QOS_H

#include <linux/mlx4/cmd.h>
#include <linux/mlx4/device.h>

#define MLX4_NUM_UP 8
#define MLX4_NUM_TC 8

/**
 * mlx4_SET_PORT_PRIO2TC - This routine maps user priorities to traffic
 * classes of a given port and device.
 *
 * @dev: mlx4_dev.
 * @port: Physical port number.
 * @prio2tc: Array of TC associated with each priorities.
 *
 * Returns 0 on success or a negative mlx4_core errno code.
 **/
int mlx4_SET_PORT_PRIO2TC(struct mlx4_dev *dev, u8 port, u8 *prio2tc);

/**
 * mlx4_SET_PORT_SCHEDULER - This routine configures the arbitration between
 * traffic classes (ETS) and configured rate limit for traffic classes.
 * tc_tx_bw, pg and ratelimit are arrays where each index represents a TC.
 * The description for those parameters below refers to a single TC.
 *
 * @dev: mlx4_dev.
 * @port: Physical port number.
 * @tc_tx_bw: The percentage of the bandwidth allocated for traffic class
 *  within a TC group. The sum of the bw_percentage of all the traffic
 *  classes within a TC group must equal 100% for correct operation.
 * @pg: The TC group the traffic class is associated with.
 * @ratelimit: The maximal bandwidth allowed for the use by this traffic class.
 *
 * Returns 0 on success or a negative mlx4_core errno code.
 **/
int mlx4_SET_PORT_SCHEDULER(struct mlx4_dev *dev, u8 port, u8 *tc_tx_bw,
			    u8 *pg, u16 *ratelimit);

#endif /* MLX4_FW_QOS_H */
