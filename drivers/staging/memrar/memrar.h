/*
 *      RAR Handler (/dev/memrar) internal driver API.
 *      Copyright (C) 2010 Intel Corporation. All rights reserved.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of version 2 of the GNU General
 *      Public License as published by the Free Software Foundation.
 *
 *      This program is distributed in the hope that it will be
 *      useful, but WITHOUT ANY WARRANTY; without even the implied
 *      warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *      PURPOSE.  See the GNU General Public License for more details.
 *      You should have received a copy of the GNU General Public
 *      License along with this program; if not, write to the Free
 *      Software Foundation, Inc., 59 Temple Place - Suite 330,
 *      Boston, MA  02111-1307, USA.
 *      The full GNU General Public License is included in this
 *      distribution in the file called COPYING.
 */


#ifndef _MEMRAR_H
#define _MEMRAR_H

#include <linux/ioctl.h>
#include <linux/types.h>


/**
 * struct RAR_stat - RAR statistics structure
 * @type:		Type of RAR memory (e.g., audio vs. video)
 * @capacity:		Total size of RAR memory region.
 * @largest_block_size:	Size of the largest reservable block.
 *
 * This structure is used for RAR_HANDLER_STAT ioctl and for the
 * RAR_get_stat() user space wrapper function.
 */
struct RAR_stat {
	__u32 type;
	__u32 capacity;
	__u32 largest_block_size;
};


/**
 * struct RAR_block_info - user space struct that describes RAR buffer
 * @type:	Type of RAR memory (e.g., audio vs. video)
 * @size:	Requested size of a block to be reserved in RAR.
 * @handle:	Handle that can be used to refer to reserved block.
 *
 * This is the basic structure exposed to the user space that
 * describes a given RAR buffer.  The buffer's underlying bus address
 * is not exposed to the user.  User space code refers to the buffer
 * entirely by "handle".
 */
struct RAR_block_info {
	__u32 type;
	__u32 size;
	__u32 handle;
};


#define RAR_IOCTL_BASE 0xE0

/* Reserve RAR block. */
#define RAR_HANDLER_RESERVE _IOWR(RAR_IOCTL_BASE, 0x00, struct RAR_block_info)

/* Release previously reserved RAR block. */
#define RAR_HANDLER_RELEASE _IOW(RAR_IOCTL_BASE, 0x01, __u32)

/* Get RAR stats. */
#define RAR_HANDLER_STAT    _IOWR(RAR_IOCTL_BASE, 0x02, struct RAR_stat)


#ifdef __KERNEL__

/* -------------------------------------------------------------- */
/*               Kernel Side RAR Handler Interface                */
/* -------------------------------------------------------------- */

/**
 * struct RAR_buffer - kernel space struct that describes RAR buffer
 * @info:		structure containing base RAR buffer information
 * @bus_address:	buffer bus address
 *
 * Structure that contains all information related to a given block of
 * memory in RAR.  It is generally only used when retrieving RAR
 * related bus addresses.
 *
 * Note: This structure is used only by RAR-enabled drivers, and is
 *       not intended to be exposed to the user space.
 */
struct RAR_buffer {
	struct RAR_block_info info;
	dma_addr_t bus_address;
};

/**
 * rar_reserve() - reserve RAR buffers
 * @buffers:	array of RAR_buffers where type and size of buffers to
 *		reserve are passed in, handle and bus address are
 *		passed out
 * @count:	number of RAR_buffers in the "buffers" array
 *
 * This function will reserve buffers in the restricted access regions
 * of given types.
 *
 * It returns the number of successfully reserved buffers.  Successful
 * buffer reservations will have the corresponding bus_address field
 * set to a non-zero value in the given buffers vector.
 */
extern size_t rar_reserve(struct RAR_buffer *buffers,
			  size_t count);

/**
 * rar_release() - release RAR buffers
 * @buffers:	array of RAR_buffers where handles to buffers to be
 *		released are passed in
 * @count:	number of RAR_buffers in the "buffers" array
 *
 * This function will release RAR buffers that were retrieved through
 * a call to rar_reserve() or rar_handle_to_bus() by decrementing the
 * reference count.  The RAR buffer will be reclaimed when the
 * reference count drops to zero.
 *
 * It returns the number of successfully released buffers.  Successful
 * releases will have their handle field set to zero in the given
 * buffers vector.
 */
extern size_t rar_release(struct RAR_buffer *buffers,
			  size_t count);

/**
 * rar_handle_to_bus() - convert a vector of RAR handles to bus addresses
 * @buffers:	array of RAR_buffers containing handles to be
 *		converted to bus_addresses
 * @count:	number of RAR_buffers in the "buffers" array

 * This function will retrieve the RAR buffer bus addresses, type and
 * size corresponding to the RAR handles provided in the buffers
 * vector.
 *
 * It returns the number of successfully converted buffers.  The bus
 * address will be set to 0 for unrecognized handles.
 *
 * The reference count for each corresponding buffer in RAR will be
 * incremented.  Call rar_release() when done with the buffers.
 */
extern size_t rar_handle_to_bus(struct RAR_buffer *buffers,
				size_t count);


#endif  /* __KERNEL__ */

#endif  /* _MEMRAR_H */
