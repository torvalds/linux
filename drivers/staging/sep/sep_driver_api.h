/*
 *
 *  sep_driver_api.h - Security Processor Driver api definitions
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
 *  2010.09.14  Upgrade to Medfield
 *
 */

#ifndef __SEP_DRIVER_API_H__
#define __SEP_DRIVER_API_H__

/* Type of request from device */
#define SEP_DRIVER_SRC_REPLY		1
#define SEP_DRIVER_SRC_REQ		2
#define SEP_DRIVER_SRC_PRINTF		3


/*-------------------------------------------
    TYPEDEFS
----------------------------------------------*/

/*
 * Note that several members of these structres are only here
 * for campatability with the middleware; they are not used
 * by this driver.
 * All user space buffer addresses are set to aligned u64
 * in order to ensure compatibility with 64 bit systems
 */

/*
  init command struct; this will go away when SCU does init
*/
struct init_struct {
	/* address that SEP can access for message */
	aligned_u64   message_addr;

	/* message size */
	u32   message_size_in_words;

	/* offset of the init message in the sep sram */
	aligned_u64   sep_sram_addr;

	/* -not used- resident size in bytes*/
	u32   unused_resident_size_in_bytes;

	/* -not used- cache size in bytes*/
	u32   unused_cache_size_in_bytes;

	/* -not used- ext cache current address */
	aligned_u64   unused_extcache_addr;

	/* -not used- ext cache size in bytes*/
	u32   unused_extcache_size_in_bytes;
};

struct realloc_ext_struct {
	/* -not used- current external cache address */
	aligned_u64   unused_ext_cache_addr;

	/* -not used- external cache size in bytes*/
	u32   unused_ext_cache_size_in_bytes;
};

struct alloc_struct {
	/* offset from start of shared pool area */
	u32  offset;
	/* number of bytes to allocate */
	u32  num_bytes;
};

/*
	Note that all app addresses are cast as u32; the sep
	middleware sends them as fixed 32 bit words
*/
struct bld_syn_tab_struct {
	/* address value of the data in (user space addr) */
	aligned_u64 app_in_address;

	/* size of data in */
	u32 data_in_size;

	/* address of the data out (user space addr) */
	aligned_u64 app_out_address;

	/* the size of the block of the operation - if needed,
	   every table will be modulo this parameter */
	u32 block_size;

	/* -not used- distinct user/kernel layout */
	bool isKernelVirtualAddress;

};

/* command struct for getting caller id value and address */
struct caller_id_struct {
	/* pid of the process */
	u32 pid;
	/* virtual address of the caller id hash */
	aligned_u64 callerIdAddress;
	/* caller id hash size in bytes */
	u32 callerIdSizeInBytes;
};

/*
  structure that represents DCB
*/
struct sep_dcblock {
	/* physical address of the first input mlli */
	u32	input_mlli_address;
	/* num of entries in the first input mlli */
	u32	input_mlli_num_entries;
	/* size of data in the first input mlli */
	u32	input_mlli_data_size;
	/* physical address of the first output mlli */
	u32	output_mlli_address;
	/* num of entries in the first output mlli */
	u32	output_mlli_num_entries;
	/* size of data in the first output mlli */
	u32	output_mlli_data_size;
	/* pointer to the output virtual tail */
	u32	out_vr_tail_pt;
	/* size of tail data */
	u32	tail_data_size;
	/* input tail data array */
	u8	tail_data[64];
};

struct sep_caller_id_entry {
	int pid;
	unsigned char callerIdHash[SEP_CALLER_ID_HASH_SIZE_IN_BYTES];
};

/*
	command structure for building dcb block (currently for ext app only
*/
struct build_dcb_struct {
	/* address value of the data in */
	aligned_u64 app_in_address;
	/* size of data in */
	u32  data_in_size;
	/* address of the data out */
	aligned_u64 app_out_address;
	/* the size of the block of the operation - if needed,
	every table will be modulo this parameter */
	u32  block_size;
	/* the size of the block of the operation - if needed,
	every table will be modulo this parameter */
	u32  tail_block_size;
};

/**
 * @struct sep_dma_map
 *
 * Structure that contains all information needed for mapping the user pages
 *	     or kernel buffers for dma operations
 *
 *
 */
struct sep_dma_map {
	/* mapped dma address */
	dma_addr_t    dma_addr;
	/* size of the mapped data */
	size_t        size;
};

struct sep_dma_resource {
	/* array of pointers to the pages that represent
	input data for the synchronic DMA action */
	struct page **in_page_array;

	/* array of pointers to the pages that represent out
	data for the synchronic DMA action */
	struct page **out_page_array;

	/* number of pages in the sep_in_page_array */
	u32 in_num_pages;

	/* number of pages in the sep_out_page_array */
	u32 out_num_pages;

	/* map array of the input data */
	struct sep_dma_map *in_map_array;

	/* map array of the output data */
	struct sep_dma_map *out_map_array;

	/* number of entries of the input mapp array */
	u32 in_map_num_entries;

	/* number of entries of the output mapp array */
	u32 out_map_num_entries;
};


/* command struct for translating rar handle to bus address
   and setting it at predefined location */
struct rar_hndl_to_bus_struct {

	/* rar handle */
	aligned_u64 rar_handle;
};

/*
  structure that represent one entry in the DMA LLI table
*/
struct sep_lli_entry {
	/* physical address */
	u32 bus_address;

	/* block size */
	u32 block_size;
};

/*----------------------------------------------------------------
	IOCTL command defines
	-----------------------------------------------------------------*/

/* magic number 1 of the sep IOCTL command */
#define SEP_IOC_MAGIC_NUMBER	                     's'

/* sends interrupt to sep that message is ready */
#define SEP_IOCSENDSEPCOMMAND	 \
	_IO(SEP_IOC_MAGIC_NUMBER, 0)

/* sends interrupt to sep that message is ready */
#define SEP_IOCSENDSEPRPLYCOMMAND	 \
	_IO(SEP_IOC_MAGIC_NUMBER, 1)

/* allocate memory in data pool */
#define SEP_IOCALLOCDATAPOLL	\
	_IOW(SEP_IOC_MAGIC_NUMBER, 2, struct alloc_struct)

/* create sym dma lli tables */
#define SEP_IOCCREATESYMDMATABLE	\
	_IOW(SEP_IOC_MAGIC_NUMBER, 5, struct bld_syn_tab_struct)

/* free dynamic data aalocated during table creation */
#define SEP_IOCFREEDMATABLEDATA	 \
	_IO(SEP_IOC_MAGIC_NUMBER, 7)

/* get the static pool area addersses (physical and virtual) */
#define SEP_IOCGETSTATICPOOLADDR	\
	_IO(SEP_IOC_MAGIC_NUMBER, 8)

/* start sep command */
#define SEP_IOCSEPSTART	 \
	_IO(SEP_IOC_MAGIC_NUMBER, 12)

/* init sep command */
#define SEP_IOCSEPINIT	\
	_IOW(SEP_IOC_MAGIC_NUMBER, 13, struct init_struct)

/* end transaction command */
#define SEP_IOCENDTRANSACTION	 \
	_IO(SEP_IOC_MAGIC_NUMBER, 15)

/* reallocate external app; unused structure still needed for
 * compatability with middleware */
#define SEP_IOCREALLOCEXTCACHE	\
	_IOW(SEP_IOC_MAGIC_NUMBER, 18, struct realloc_ext_struct)

#define SEP_IOCRARPREPAREMESSAGE	\
	_IOW(SEP_IOC_MAGIC_NUMBER, 20, struct rar_hndl_to_bus_struct)

#define SEP_IOCTLSETCALLERID	\
	_IOW(SEP_IOC_MAGIC_NUMBER, 34, struct caller_id_struct)

#define SEP_IOCPREPAREDCB					\
	_IOW(SEP_IOC_MAGIC_NUMBER, 35, struct build_dcb_struct)

#define SEP_IOCFREEDCB					\
	_IO(SEP_IOC_MAGIC_NUMBER, 36)

#endif
