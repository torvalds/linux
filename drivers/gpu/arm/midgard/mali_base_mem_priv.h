/*
 *
 * (C) COPYRIGHT 2010-2014 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
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
	base_mem_handle mem_handle;
	u64 user_addr;
	u64 size;
	u8 type;
	u8 padding[7];
};

#endif
