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
	struct ethosn_device *ethosn;
	struct ethosn_dma_allocator *asset_allocator;

    // 保存权重数据的 DMA 内存空间, 在注册网络时分配
	struct ethosn_dma_info *constant_dma_data;

    // 保存命令流数据的 DMA 内存空间, 在注册网络时分配
	struct ethosn_dma_info *constant_cu_data;
    
    // 对于每一个核中的 *inference_data, 其内容是描述一次 ethosn_network_req 中全部 buffer 信息的 ethosn_buffer_array
	struct ethosn_dma_info **inference_data;

    // 中间层数据也是核内处理的, 其内容正是 ethosn_network_req.ethosn_intermediate_desc.ethosn_memory描述的, 分配或导入的中间层DMA内存空间
	struct ethosn_dma_info **intermediate_data;

	u32 num_intermediates;
	struct ethosn_buffer_info *intermediates;

    // 描述输入缓冲区布局信息, 网络模型是不包含输入缓冲区数据的
	u32 num_inputs;
	struct ethosn_buffer_info *inputs;
 
    // 描述输出缓冲区布局信息, 网络模型是不包含输出缓冲区数据的
	u32 num_outputs;
	struct ethosn_buffer_info *outputs;

	/* file pointer used for ref-counting */
	struct file *file;
};

// 推理任务
struct ethosn_inference {
    // 当前推理任务绑定的核心
	struct ethosn_core *core;
    // 当前推理任务绑定的网络模型
	struct ethosn_network *network;

	struct list_head queue_node;

    // 根据推理请求获得的输入输出缓冲区信息
	struct ethosn_buffer **inputs;
	struct ethosn_buffer **outputs;

	u32 status;
	u64 cycle_count;

    // 等待队列的粒度也是推理任务级别的
	wait_queue_head_t poll_wqh;
};

struct ethosn_network_req;
struct ethosn_inference_req;

int ethosn_network_register(struct ethosn_device *ethosn, struct ethosn_dma_allocator *asset_allocator, struct ethosn_network_req *net_req);

void ethosn_set_inference_done(struct ethosn_core *core, struct ethosn_inference *inference, int new_status, u64 cycle_count);

void ethosn_schedule_queued_inference(struct ethosn_core *core);

int ethosn_schedule_inference(struct ethosn_inference *inference);

#endif /* _ETHOSN_NETWORK_H_ */
