/*
 *
 * (C) COPYRIGHT 2022 Arm Limited.
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

#ifndef _ETHOSN_PROC_MEM_ALLOCATOR_H_
#define _ETHOSN_PROC_MEM_ALLOCATOR_H_

#include "ethosn_dma.h"
#include "ethosn_device.h"

#include <linux/types.h>

struct ethosn_allocator {
	struct ethosn_device *ethosn;
	struct file *file;                                  // 用于将 ethosn_allocator 对象抽象为文件, 提供 ioctl 操作
	struct ethosn_dma_allocator *asset_allocator;       // 当前 ethosn_allocator 对象占用的资源管理器
};

// process_mem_allocator 主要用来确保在同一进程中, 创建缓冲区, 注册网络模型以及执行推理的全流程使用同一内存资源管理单元
// 从 process_mem_allocator 创建到释放的流程, 该内存管理单元都是独占的
int ethosn_process_mem_allocator_create(struct ethosn_device *ethosn, pid_t pid, bool proteced);

#endif /* _ETHOSN_PROC_MEM_ALLOCATOR_H_ */
