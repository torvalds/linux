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

#ifndef _ETHOSN_SUB_ALLOCATOR_H_
#define _ETHOSN_SUB_ALLOCATOR_H_

#define ETHOSN_MEM_STREAM_DRIVER_NAME       "ethosn-memory"

#include <linux/platform_device.h>

/* ethosn_get_global_buffer_data_pdev_for_testing() - Exposes global access to
 * the pdev of buffer for testing purposes.
 */
struct platform_device *ethosn_get_global_buffer_data_pdev_for_testing(void);

int ethosn_mem_stream_platform_driver_register(void);

void ethosn_mem_stream_platform_driver_unregister(void);

#endif /* _ETHOSN_SUB_ALLOCATOR_H_ */
