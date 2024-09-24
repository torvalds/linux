/*
 *
 * (C) COPYRIGHT 2018-2022 Arm Limited.
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

#ifndef _ETHOSN_BUFFER_H_
#define _ETHOSN_BUFFER_H_

#include "ethosn_device.h"
#include "ethosn_dma.h"
#include "uapi/ethosn.h"

#include <linux/fs.h>
#include <linux/types.h>

struct ethosn_buffer {
	struct ethosn_device        *ethosn;
	struct ethosn_dma_allocator *asset_allocator;
	struct ethosn_dma_info      *dma_info;
	/* file pointer used for user-space mmap and for ref-counting */
	struct file                 *file;
};

int ethosn_buffer_register(struct ethosn_device *ethosn,
			   struct ethosn_dma_allocator *asset_allocator,
			   struct ethosn_buffer_req *buf_req);
struct ethosn_buffer *ethosn_buffer_get(int fd);
void put_ethosn_buffer(struct ethosn_buffer *buf);

const struct file_operations *ethosn_get_dma_view_fops(void);

int ethosn_buffer_import(struct ethosn_device *ethosn,
			 struct ethosn_dma_allocator *asset_allocator,
			 struct ethosn_dma_buf_req *buf_req);
#endif /* _ETHOSN_BUFFER_H_ */
