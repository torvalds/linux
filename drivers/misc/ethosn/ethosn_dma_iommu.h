/*
 *
 * (C) COPYRIGHT 2018-2020,2022-2023 Arm Limited.
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

#ifndef _ETHOSN_DMA_IOMMU_H_
#define _ETHOSN_DMA_IOMMU_H_

#include "ethosn_dma.h"

#include <linux/types.h>

#define IOMMU_FIRMWARE_ADDR_BASE        0x20000000UL
#define IOMMU_WORKING_DATA_ADDR_BASE    0x40000000UL

/**
 * IOMMU_COMMAND_STREAM includes the region for command stream and all other
 * constant cu data (ie weights metadata and binding table).
 */
#define IOMMU_COMMAND_STREAM_ADDR_BASE           0x60000000UL
#define IOMMU_WEIGHT_DATA_ADDR_BASE              0x80000000UL
#define IOMMU_INTERMEDIATE_BUFFER_ADDR_BASE      0xA0000000UL
#define IOMMU_BUFFER_ADDR_BASE                   0xC0000000UL

/* IOMMU address space size, use the same for all streams. */
#define IOMMU_ADDR_SIZE 0x20000000UL

struct ethosn_dma_sub_allocator *ethosn_dma_iommu_allocator_create(
	struct device *dev,
	enum ethosn_stream_type stream_type,
	dma_addr_t addr_base,
	phys_addr_t speculative_page_addr);

#endif /* _ETHOSN_DMA_IOMMU_H_ */
