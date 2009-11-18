/*
 *
 *  sep_driver_config.h - Security Processor Driver configuration
 *
 *  Copyright(c) 2009 Intel Corporation. All rights reserved.
 *  Copyright(c) 2009 Discretix. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
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
 *
 *  CHANGES:
 *
 *  2009.06.26	Initial publish
 *
 */

#ifndef __SEP_DRIVER_CONFIG_H__
#define __SEP_DRIVER_CONFIG_H__


/*--------------------------------------
  DRIVER CONFIGURATION FLAGS
  -------------------------------------*/

/* if flag is on , then the driver is running in polling and
	not interrupt mode */
#define SEP_DRIVER_POLLING_MODE                         1

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
#define SEP_DRIVER_ENTRIES_PER_TABLE_IN_SEP             8


/*--------------------------------------------------------
	SHARED AREA  memory total size is 36K
	it is divided is following:

	SHARED_MESSAGE_AREA                     8K         }
									}
	STATIC_POOL_AREA                        4K         } MAPPED AREA ( 24 K)
									}
	DATA_POOL_AREA                          12K        }

	SYNCHRONIC_DMA_TABLES_AREA              5K

	FLOW_DMA_TABLES_AREA                    4K

	SYSTEM_MEMORY_AREA                      3k

	SYSTEM_MEMORY total size is 3k
	it is divided as following:

	TIME_MEMORY_AREA                     8B
-----------------------------------------------------------*/



/*
	the maximum length of the message - the rest of the message shared
	area will be dedicated to the dma lli tables
*/
#define SEP_DRIVER_MAX_MESSAGE_SIZE_IN_BYTES                  (8 * 1024)

/* the size of the message shared area in pages */
#define SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES          (8 * 1024)

/* the size of the data pool static area in pages */
#define SEP_DRIVER_STATIC_AREA_SIZE_IN_BYTES                  (4 * 1024)

/* the size of the data pool shared area size in pages */
#define SEP_DRIVER_DATA_POOL_SHARED_AREA_SIZE_IN_BYTES        (12 * 1024)

/* the size of the message shared area in pages */
#define SEP_DRIVER_SYNCHRONIC_DMA_TABLES_AREA_SIZE_IN_BYTES   (1024 * 5)


/* the size of the data pool shared area size in pages */
#define SEP_DRIVER_FLOW_DMA_TABLES_AREA_SIZE_IN_BYTES         (1024 * 4)

/* system data (time, caller id etc') pool */
#define SEP_DRIVER_SYSTEM_DATA_MEMORY_SIZE_IN_BYTES           100


/* area size that is mapped  - we map the MESSAGE AREA, STATIC POOL and
	DATA POOL areas. area must be module 4k */
#define SEP_DRIVER_MMMAP_AREA_SIZE                            (1024 * 24)


/*-----------------------------------------------
	offsets of the areas starting from the shared area start address
*/

/* message area offset */
#define SEP_DRIVER_MESSAGE_AREA_OFFSET_IN_BYTES               0

/* static pool area offset */
#define SEP_DRIVER_STATIC_AREA_OFFSET_IN_BYTES \
		(SEP_DRIVER_MESSAGE_SHARED_AREA_SIZE_IN_BYTES)

/* data pool area offset */
#define SEP_DRIVER_DATA_POOL_AREA_OFFSET_IN_BYTES \
	(SEP_DRIVER_STATIC_AREA_OFFSET_IN_BYTES + \
	SEP_DRIVER_STATIC_AREA_SIZE_IN_BYTES)

/* synhronic dma tables area offset */
#define SEP_DRIVER_SYNCHRONIC_DMA_TABLES_AREA_OFFSET_IN_BYTES \
	(SEP_DRIVER_DATA_POOL_AREA_OFFSET_IN_BYTES + \
	SEP_DRIVER_DATA_POOL_SHARED_AREA_SIZE_IN_BYTES)

/* sep driver flow dma tables area offset */
#define SEP_DRIVER_FLOW_DMA_TABLES_AREA_OFFSET_IN_BYTES \
	(SEP_DRIVER_SYNCHRONIC_DMA_TABLES_AREA_OFFSET_IN_BYTES + \
	SEP_DRIVER_SYNCHRONIC_DMA_TABLES_AREA_SIZE_IN_BYTES)

/* system memory offset in bytes */
#define SEP_DRIVER_SYSTEM_DATA_MEMORY_OFFSET_IN_BYTES \
	(SEP_DRIVER_FLOW_DMA_TABLES_AREA_OFFSET_IN_BYTES + \
	SEP_DRIVER_FLOW_DMA_TABLES_AREA_SIZE_IN_BYTES)

/* offset of the time area */
#define SEP_DRIVER_SYSTEM_TIME_MEMORY_OFFSET_IN_BYTES \
	(SEP_DRIVER_SYSTEM_DATA_MEMORY_OFFSET_IN_BYTES)



/* start physical address of the SEP registers memory in HOST */
#define SEP_IO_MEM_REGION_START_ADDRESS                       0x80000000

/* size of the SEP registers memory region  in HOST (for now 100 registers) */
#define SEP_IO_MEM_REGION_SIZE                                (2 * 0x100000)

/* define the number of IRQ for SEP interrupts */
#define SEP_DIRVER_IRQ_NUM                                    1

/* maximum number of add buffers */
#define SEP_MAX_NUM_ADD_BUFFERS                               100

/* number of flows */
#define SEP_DRIVER_NUM_FLOWS                                  4

/* maximum number of entries in flow table */
#define SEP_DRIVER_MAX_FLOW_NUM_ENTRIES_IN_TABLE              25

/* offset of the num entries in the block length entry of the LLI */
#define SEP_NUM_ENTRIES_OFFSET_IN_BITS                        24

/* offset of the interrupt flag in the block length entry of the LLI */
#define SEP_INT_FLAG_OFFSET_IN_BITS                           31

/* mask for extracting data size from LLI */
#define SEP_TABLE_DATA_SIZE_MASK                              0xFFFFFF

/* mask for entries after being shifted left */
#define SEP_NUM_ENTRIES_MASK                                  0x7F

/* default flow id */
#define SEP_FREE_FLOW_ID                                      0xFFFFFFFF

/* temp flow id used during cretiong of new flow until receiving
	real flow id from sep */
#define SEP_TEMP_FLOW_ID                   (SEP_DRIVER_NUM_FLOWS + 1)

/* maximum add buffers message length in bytes */
#define SEP_MAX_ADD_MESSAGE_LENGTH_IN_BYTES                   (7 * 4)

/* maximum number of concurrent virtual buffers */
#define SEP_MAX_VIRT_BUFFERS_CONCURRENT                       100

/* the token that defines the start of time address */
#define SEP_TIME_VAL_TOKEN                                    0x12345678

/* DEBUG LEVEL MASKS */
#define SEP_DEBUG_LEVEL_BASIC       0x1

#define SEP_DEBUG_LEVEL_EXTENDED    0x4


/* Debug helpers */

#define dbg(fmt, args...) \
do {\
	if (debug & SEP_DEBUG_LEVEL_BASIC) \
		printk(KERN_DEBUG fmt, ##args); \
} while(0);

#define edbg(fmt, args...) \
do { \
	if (debug & SEP_DEBUG_LEVEL_EXTENDED) \
		printk(KERN_DEBUG fmt, ##args); \
} while(0);



#endif
