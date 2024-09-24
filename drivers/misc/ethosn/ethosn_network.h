/*
 *
 * (C) COPYRIGHT 2018-2023 Arm Limited.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 */

#ifndef _ETHOSN_NETWORK_H_
#define _ETHOSN_NETWORK_H_

#include "ethosn_dma.h"
#include "ethosn_device.h"
#include <linux/irqreturn.h>

struct ethosn_core;

struct ethosn_network {
	/* This is the ethosn device for when the network needs to access
	 * members of the ethosn struct. Memory allocation and mapping is
	 * performed using an asset allocator in the ethosn device.
	 */
	struct ethosn_device        *ethosn;
	struct ethosn_dma_allocator *asset_allocator;

	struct ethosn_dma_info      *constant_dma_data;
	struct ethosn_dma_info      *constant_cu_data;
	struct ethosn_dma_info      **inference_data;
	struct ethosn_dma_info      **intermediate_data;

	u32                         num_intermediates;
	struct ethosn_buffer_info   *intermediates;

	u32                         num_inputs;
	struct ethosn_buffer_info   *inputs;

	u32                         num_outputs;
	struct ethosn_buffer_info   *outputs;

	/* file pointer used for ref-counting */
	struct file                 *file;
};

struct ethosn_inference {
	struct ethosn_core    *core;
	struct ethosn_network *network;

	struct list_head      queue_node;

	struct ethosn_buffer  **inputs;
	struct ethosn_buffer  **outputs;

	u32                   status;
	u64                   cycle_count;

	wait_queue_head_t     poll_wqh;
};

struct ethosn_network_req;
struct ethosn_inference_req;

int ethosn_network_register(struct ethosn_device *ethosn,
			    struct ethosn_dma_allocator *asset_allocator,
			    struct ethosn_network_req *net_req);

void ethosn_set_inference_done(struct ethosn_core *core,
			       struct ethosn_inference *inference,
			       int new_status,
			       u64 cycle_count);

void ethosn_schedule_queued_inference(struct ethosn_core *core);

int ethosn_schedule_inference(struct ethosn_inference *inference);

#endif /* _ETHOSN_NETWORK_H_ */
