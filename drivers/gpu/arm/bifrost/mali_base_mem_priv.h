/*
 *
 * (C) COPYRIGHT 2010-2015, 2020 ARM Limited. All rights reserved.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */



#ifndef _BASE_MEM_PRIV_H_
#define _BASE_MEM_PRIV_H_

#define BASE_SYNCSET_OP_MSYNC	(1U << 0)
#define BASE_SYNCSET_OP_CSYNC	(1U << 1)

/*
 * This structure describe a basic memory coherency operation.
 * It can either be:
 * @li a sync from CPU to Memory:
 *	- type = ::BASE_SYNCSET_OP_MSYNC
 *	- mem_handle = a handle to the memory object on which the operation
 *	  is taking place
 *	- user_addr = the address of the range to be synced
 *	- size = the amount of data to be synced, in bytes
 *	- offset is ignored.
 * @li a sync from Memory to CPU:
 *	- type = ::BASE_SYNCSET_OP_CSYNC
 *	- mem_handle = a handle to the memory object on which the operation
 *	  is taking place
 *	- user_addr = the address of the range to be synced
 *	- size = the amount of data to be synced, in bytes.
 *	- offset is ignored.
 */
struct basep_syncset {
	struct base_mem_handle mem_handle;
	u64 user_addr;
	u64 size;
	u8 type;
	u8 padding[7];
};

#endif
