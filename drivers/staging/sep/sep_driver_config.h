/*
 *
 *  sep_driver_config.h - Security Processor Driver configuration
 *
 *  Copyright(c) 2009,2010 Intel Corporation. All rights reserved.
 *  Contributions(c) 2009,2010 Discretix. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 *  CONTACTS:
 *
 *  Mark Allyn		mark.a.allyn@intel.com
 *  Jayant Mangalampalli jayant.mangalampalli@intel.com
 *
 *  CHANGES:
 *
 *  2010.06.26	Upgrade to Medfield
 *
 */

#ifndef __SEP_DRIVER_CONFIG_H__
#define __SEP_DRIVER_CONFIG_H__


/*--------------------------------------
  DRIVER CONFIGURATION FLAGS
  -------------------------------------*/

/* if flag is on , then the driver is running in polling and
	not interrupt mode */
#define SEP_DRIVER_POLLING_MODE                         0

/* flag which defines if the shared area address should be
	reconfiged (send to SEP anew) during init of the driver */
#define SEP_DRIVER_RECONFIG_MESSAGE_AREA                0

/* the mode for running on the ARM1172 Evaluation platform (flag is 1) */
#define SEP_DRIVER_ARM_DEBUG_MODE                       0

/*-------------------------------------------
	INTERNAL DATA CONFIGURATION
	-------------------------------------------*/

/* flag for the input array */
#define SEP_DRIVER_IN_FLAG                              0

/* flag for output array */
#define SEP_DRIVER_OUT_FLAG                             1

/* maximum number of entries in one LLI tables */
#define SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP             31

/* minimum data size of the MLLI table */
#define SEP_DRIVER_MIN_DATA_SIZE_PER_TABLE		16

/* flag that signifies tah the lock is
currently held by the proccess (struct file) */
#define SEP_DRIVER_OWN_LOCK_FLAG                        1

/* flag that signifies tah the lock is currently NOT
held by the proccess (struct file) */
#define SEP_DRIVER_DISOWN_LOCK_FLAG                     0

/* indicates whether driver has mapped/unmapped shared area */
#define SEP_REQUEST_DAEMON_MAPPED 1
#define SEP_REQUEST_DAEMON_UNMAPPED 0

/*--------------------------------------------------------
	SHARED AREA  memory total size is 36K
	it is divided is following:

	SHARED_MESSAGE_AREA                     8K         }
									}
	STATIC_POOL_AREA                        4K         } MAPPED AREA ( 24 K)
									}
	DATA_POOL_AREA                          12K        }

	SYNCHRONIC_DMA_TABLES_AREA              5K

	placeholder until drver changes
	FLOW_DMA_TABLES_AREA                    4K

	SYSTEM_MEMORY_AREA                      3k

	SYSTEM_MEMORY total size is 3k
	it is divided as following:

	TIME_MEMORY_AREA                     8B
-----------------------------------------------------------*/

#define SEP_DEV_NAME "sep_sec_driver"
#define SEP_DEV_SINGLETON "sep_sec_singleton_driver"
#define SEP_DEV_DAEMON "sep_req_daemon_driver"


/*
	the maximum length of the message - the rest of the message shared
	area will be dedicated to the dma lli tables
*/
#define SEP_DRIVER_MAX_MESSAGE_SIZE_IN_BYTES			(8 * 1024)

/* the size of the message shared area in pages */
#define SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES		(8 * 1024)

/* the size of the data pool static area in pages */
#define SEP_DRIVER_STATIC_AREA_SIZE_IN_BYTES			(4 * 1024)

/* the size of the data pool shared area size in pages */
#define SEP_DRIVER_DATA_POOL_SHARED_AREA_SIZE_IN_BYTES		(16 * 1024)

/* the size of the message shared area in pages */
#define SYNCHRONIC_DMA_TABLES_AREA_SIZE_BYTES	(1024 * 5)

/* Placeholder until driver changes */
#define SEP_DRIVER_FLOW_DMA_TABLES_AREA_SIZE_IN_BYTES		(1024 * 4)

/* system data (time, caller id etc') pool */
#define SEP_DRIVER_SYSTEM_DATA_MEMORY_SIZE_IN_BYTES		(1024 * 3)

/* the size in bytes of the time memory */
#define SEP_DRIVER_TIME_MEMORY_SIZE_IN_BYTES			8

/* the size in bytes of the RAR parameters memory */
#define SEP_DRIVER_SYSTEM_RAR_MEMORY_SIZE_IN_BYTES		8

/* area size that is mapped  - we map the MESSAGE AREA, STATIC POOL and
	DATA POOL areas. area must be module 4k */
#define SEP_DRIVER_MMMAP_AREA_SIZE				(1024 * 28)

/*-----------------------------------------------
	offsets of the areas starting from the shared area start address
*/

/* message area offset */
#define SEP_DRIVER_MESSAGE_AREA_OFFSET_IN_BYTES			0

/* static pool area offset */
#define SEP_DRIVER_STATIC_AREA_OFFSET_IN_BYTES \
	(SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES)

/* data pool area offset */
#define SEP_DRIVER_DATA_POOL_AREA_OFFSET_IN_BYTES \
	(SEP_DRIVER_STATIC_AREA_OFFSET_IN_BYTES + \
	SEP_DRIVER_STATIC_AREA_SIZE_IN_BYTES)

/* synhronic dma tables area offset */
#define SYNCHRONIC_DMA_TABLES_AREA_OFFSET_BYTES \
	(SEP_DRIVER_DATA_POOL_AREA_OFFSET_IN_BYTES + \
	SEP_DRIVER_DATA_POOL_SHARED_AREA_SIZE_IN_BYTES)

/* system memory offset in bytes */
#define SEP_DRIVER_SYSTEM_DATA_MEMORY_OFFSET_IN_BYTES \
	(SYNCHRONIC_DMA_TABLES_AREA_OFFSET_BYTES + \
	SYNCHRONIC_DMA_TABLES_AREA_SIZE_BYTES)

/* offset of the time area */
#define SEP_DRIVER_SYSTEM_TIME_MEMORY_OFFSET_IN_BYTES \
	(SEP_DRIVER_SYSTEM_DATA_MEMORY_OFFSET_IN_BYTES)

/* offset of the RAR area */
#define SEP_DRIVER_SYSTEM_RAR_MEMORY_OFFSET_IN_BYTES \
	(SEP_DRIVER_SYSTEM_TIME_MEMORY_OFFSET_IN_BYTES + \
	SEP_DRIVER_TIME_MEMORY_SIZE_IN_BYTES)

/* offset of the caller id area */
#define SEP_CALLER_ID_OFFSET_BYTES \
	(SEP_DRIVER_SYSTEM_RAR_MEMORY_OFFSET_IN_BYTES + \
    SEP_DRIVER_SYSTEM_RAR_MEMORY_SIZE_IN_BYTES)

/* offset of the DCB area */
#define SEP_DRIVER_SYSTEM_DCB_MEMORY_OFFSET_IN_BYTES \
	(SEP_DRIVER_SYSTEM_DATA_MEMORY_OFFSET_IN_BYTES + \
	0x400)

/* offset of the ext cache area */
#define SEP_DRIVER_SYSTEM_EXT_CACHE_ADDR_OFFSET_IN_BYTES \
	SEP_DRIVER_SYSTEM_RAR_MEMORY_OFFSET_IN_BYTES

/* offset of the allocation data pointer area */
#define SEP_DRIVER_DATA_POOL_ALLOCATION_OFFSET_IN_BYTES \
	(SEP_CALLER_ID_OFFSET_BYTES + \
	SEP_CALLER_ID_HASH_SIZE_IN_BYTES)

/* the token that defines the start of time address */
#define SEP_TIME_VAL_TOKEN                                    0x12345678

#define FAKE_RAR_SIZE (1024*1024) /* used only for mfld */
/* DEBUG LEVEL MASKS */

/* size of the caller id hash (sha2) */
#define SEP_CALLER_ID_HASH_SIZE_IN_BYTES                      32

/* maximum number of entries in the caller id table */
#define SEP_CALLER_ID_TABLE_NUM_ENTRIES                       20

/* maximum number of symetric operation (that require DMA resource)
	per one message */
#define SEP_MAX_NUM_SYNC_DMA_OPS			16

/* the token that defines the start of time address */
#define SEP_RAR_VAL_TOKEN                                     0xABABABAB

/* ioctl error that should be returned when trying
   to realloc the cache/resident second time */
#define SEP_ALREADY_INITIALIZED_ERR                           12

/* bit that locks access to the shared area */
#define SEP_MMAP_LOCK_BIT                                     0

/* bit that lock access to the poll  - after send_command */
#define SEP_SEND_MSG_LOCK_BIT                                 1

/* the token that defines the static pool address address */
#define SEP_STATIC_POOL_VAL_TOKEN                             0xABBAABBA

/* the token that defines the data pool pointers address */
#define SEP_DATA_POOL_POINTERS_VAL_TOKEN                      0xEDDEEDDE

/* the token that defines the data pool pointers address */
#define SEP_EXT_CACHE_ADDR_VAL_TOKEN                          0xBABABABA

/* rar handler */
#ifndef CONFIG_MRST_RAR_HANDLER

/* This stub header is for non Moorestown driver only */

/*
 * Constants that specify different kinds of RAR regions that could be
 * set up.
 */
static __u32 const RAR_TYPE_VIDEO;  /* 0 */
static __u32 const RAR_TYPE_AUDIO = 1;
static __u32 const RAR_TYPE_IMAGE = 2;
static __u32 const RAR_TYPE_DATA  = 3;

/*
 * @struct RAR_stat
 *
 * @brief This structure is used for @c RAR_HANDLER_STAT ioctl and for
 *	@c RAR_get_stat() user space wrapper function.
 */
struct RAR_stat {
	/* Type of RAR memory (e.g., audio vs. video) */
	__u32 type;

	/*
	* Total size of RAR memory region.
	*/
	__u32 capacity;

	/* Size of the largest reservable block. */
	__u32 largest_block_size;
};


/*
 * @struct RAR_block_info
 *
 * @brief The argument for the @c RAR_HANDLER_RESERVE @c ioctl.
 *
 */
struct RAR_block_info {
	/* Type of RAR memory (e.g., audio vs. video) */
	__u32 type;

	/* Requested size of a block to be reserved in RAR. */
	__u32 size;

	/* Handle that can be used to refer to reserved block. */
	__u32 handle;
};

/*
 * @struct RAR_buffer
 *
 * Structure that contains all information related to a given block of
 * memory in RAR.  It is generally only used when retrieving bus
 * addresses.
 *
 * @note This structure is used only by RAR-enabled drivers, and is
 *	 not intended to be exposed to the user space.
 */
struct RAR_buffer {
	/* Structure containing base RAR buffer information */
	struct RAR_block_info info;

	/* Buffer bus address */
	__u32 bus_address;
};


#define RAR_IOCTL_BASE 0xE0

/* Reserve RAR block. */
#define RAR_HANDLER_RESERVE _IOWR(RAR_IOCTL_BASE, 0x00, struct RAR_block_info)

/* Release previously reserved RAR block. */
#define RAR_HANDLER_RELEASE _IOW(RAR_IOCTL_BASE, 0x01, __u32)

/* Get RAR stats. */
#define RAR_HANDLER_STAT    _IOWR(RAR_IOCTL_BASE, 0x02, struct RAR_stat)


/* -------------------------------------------------------------- */
/*		 Kernel Side RAR Handler Interface		*/
/* -------------------------------------------------------------- */

/*
 * @function rar_reserve
 *
 * @brief Reserve RAR buffers.
 *
 * This function will reserve buffers in the restricted access regions
 * of given types.
 *
 * @return Number of successfully reserved buffers.
 *	 Successful buffer reservations will have the corresponding
 *	 @c bus_address field set to a non-zero value in the
 *	 given @a buffers vector.
 */
#define rar_reserve(a, b) ((size_t)NULL)

/*
 * @function rar_release
 *
 * @brief Release RAR buffers retrieved through call to
 *	@c rar_reserve() or @c rar_handle_to_bus().
 *
 * This function will release RAR buffers that were retrieved through
 * a call to @c rar_reserve() or @c rar_handle_to_bus() by
 * decrementing the reference count.  The RAR buffer will be reclaimed
 * when the reference count drops to zero.
 *
 * @return Number of successfully released buffers.
 *	 Successful releases will have their handle field set to
 *	 zero in the given @a buffers vector.
 */
#define rar_release(a, b) ((size_t)NULL)

/*
 * @function rar_handle_to_bus
 *
 * @brief Convert a vector of RAR handles to bus addresses.
 *
 * This function will retrieve the RAR buffer bus addresses, type and
 * size corresponding to the RAR handles provided in the @a buffers
 * vector.
 *
 * @return Number of successfully converted buffers.
 *	 The bus address will be set to @c 0 for unrecognized
 *	 handles.
 *
 * @note The reference count for each corresponding buffer in RAR will
 *	 be incremented.  Call @c rar_release() when done with the
 *	 buffers.
 */
#define rar_handle_to_bus(a, b) ((size_t)NULL)

#else /* using rear memrar */

#include "../memrar/memrar.h"

#endif  /* MEMRAR */

/* rar_register */
#ifndef CONFIG_RAR_REGISTER
/* This stub header is for non Moorestown driver only */

/* The register_rar function is to used by other device drivers
 * to ensure that this driver is ready. As we cannot be sure of
 * the compile/execute order of dirvers in ther kernel, it is
 * best to give this driver a callback function to call when
 * it is ready to give out addresses. The callback function
 * would have those steps that continue the initialization of
 * a driver that do require a valid RAR address. One of those
 * steps would be to call get_rar_address()
 * This function return 0 on success an -1 on failure.
 */
#define register_rar(a, b, c) (-ENODEV)

/* The get_rar_address function is used by other device drivers
 * to obtain RAR address information on a RAR. It takes two
 * parameter:
 *
 * int rar_index
 * The rar_index is an index to the rar for which you wish to retrieve
 * the address information.
 * Values can be 0,1, or 2.
 *
 * struct RAR_address_struct is a pointer to a place to which the function
 * can return the address structure for the RAR.
 *
 * The function returns a 0 upon success or a -1 if there is no RAR
 * facility on this system.
 */
#define rar_get_address(a, b, c) (-ENODEV)

/* The lock_rar function is ued by other device drivers to lock an RAR.
 * once an RAR is locked, it stays locked until the next system reboot.
 * The function takes one parameter:
 *
 * int rar_index
 * The rar_index is an index to the rar that you want to lock.
 * Values can be 0,1, or 2.
 *
 * The function returns a 0 upon success or a -1 if there is no RAR
 * facility on this system.
 */
#define rar_lock(a) (-1)

#else /* using real RAR_REGISTER */

#include <linux/rar_register.h>

#endif  /* CONFIG_RAR_REGISTER */

#endif /* SEP DRIVER CONFIG */
