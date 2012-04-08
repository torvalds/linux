/*
 *
 *  sep_driver_config.h - Security Processor Driver configuration
 *
 *  Copyright(c) 2009-2011 Intel Corporation. All rights reserved.
 *  Contributions(c) 2009-2011 Discretix. All rights reserved.
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
 *  2011.02.22  Enable kernel crypto
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

/* Critical message area contents for sanity checking */
#define SEP_START_MSG_TOKEN				0x02558808
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

	SYNCHRONIC_DMA_TABLES_AREA              29K

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
	the minimum length of the message - includes 2 reserved fields
	at the start, then token, message size and opcode fields. all dwords
*/
#define SEP_DRIVER_MIN_MESSAGE_SIZE_IN_BYTES			(5*sizeof(u32))

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
#define SYNCHRONIC_DMA_TABLES_AREA_SIZE_BYTES	(1024 * 29)

/* Placeholder until driver changes */
#define SEP_DRIVER_FLOW_DMA_TABLES_AREA_SIZE_IN_BYTES		(1024 * 4)

/* system data (time, caller id etc') pool */
#define SEP_DRIVER_SYSTEM_DATA_MEMORY_SIZE_IN_BYTES		(1024 * 3)

/* Offset of the sep printf buffer in the message area */
#define SEP_DRIVER_PRINTF_OFFSET_IN_BYTES			(5888)

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

/* size of the caller id hash (sha2) in 32 bit words */
#define SEP_CALLER_ID_HASH_SIZE_IN_WORDS                8

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
#define SEP_TRANSACTION_STARTED_LOCK_BIT                      0

/* bit that lock access to the poll  - after send_command */
#define SEP_WORKING_LOCK_BIT                                  1

/* the token that defines the static pool address address */
#define SEP_STATIC_POOL_VAL_TOKEN                             0xABBAABBA

/* the token that defines the data pool pointers address */
#define SEP_DATA_POOL_POINTERS_VAL_TOKEN                      0xEDDEEDDE

/* the token that defines the data pool pointers address */
#define SEP_EXT_CACHE_ADDR_VAL_TOKEN                          0xBABABABA

/* Time limit for SEP to finish */
#define WAIT_TIME 10

/* Delay for pm runtime suspend (reduces pm thrashing with bursty traffic */
#define SUSPEND_DELAY 10

/* Number of delays to wait until scu boots after runtime resume */
#define SCU_DELAY_MAX 50

/* Delay for each iteration (usec) wait for scu boots after runtime resume */
#define SCU_DELAY_ITERATION 10


/*
 * Bits used in struct sep_call_status to check that
 * driver's APIs are called in valid order
 */

/* Bit offset which indicates status of sep_write() */
#define SEP_FASTCALL_WRITE_DONE_OFFSET		0

/* Bit offset which indicates status of sep_mmap() */
#define SEP_LEGACY_MMAP_DONE_OFFSET		1

/* Bit offset which indicates status of the SEP_IOCSENDSEPCOMMAND ioctl */
#define SEP_LEGACY_SENDMSG_DONE_OFFSET		2

/* Bit offset which indicates status of sep_poll() */
#define SEP_LEGACY_POLL_DONE_OFFSET		3

/* Bit offset which indicates status of the SEP_IOCENDTRANSACTION ioctl */
#define SEP_LEGACY_ENDTRANSACTION_DONE_OFFSET	4

/*
 * Used to limit number of concurrent processes
 * allowed to allocte dynamic buffers in fastcall
 * interface.
 */
#define SEP_DOUBLEBUF_USERS_LIMIT		3

/* Identifier for valid fastcall header */
#define SEP_FC_MAGIC				0xFFAACCAA

/*
 * Used for enabling driver runtime power management.
 * Useful for enabling/disabling it during performance
 * testing
 */
#define SEP_ENABLE_RUNTIME_PM

#endif /* SEP DRIVER CONFIG */
