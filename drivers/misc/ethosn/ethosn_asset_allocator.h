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

#ifndef _ETHOSN_ASSET_ALLOCATOR_H_
#define _ETHOSN_ASSET_ALLOCATOR_H_

#include "ethosn_dma.h"
#include "ethosn_device.h"

#include <linux/types.h>

#define ETHOSN_ASSET_ALLOC_DRIVER_NAME  "ethosn-asset_allocator"
#define ETHOSN_ASSET_ALLOC_NUM_MAX 64

#define ETHOSN_DEFAULT_ASSET_ALLOC 0
#define ETHOSN_INVALID_PID INT_MIN

void ethosn_asset_allocator_get(struct ethosn_dma_allocator *asset_allocator);

int ethosn_asset_allocator_put(struct ethosn_dma_allocator *asset_allocator);

struct ethosn_dma_allocator *ethosn_asset_allocator_find(
	const struct ethosn_device *ethosn,
	pid_t pid);

struct ethosn_dma_allocator *ethosn_asset_allocator_reserve(
	struct ethosn_device *ethosn,
	pid_t pid);

int ethosn_asset_allocator_platform_driver_register(void);

void ethosn_asset_allocator_platform_driver_unregister(void);

#endif /* _ETHOSN_ASSET_ALLOCATOR_H_ */
