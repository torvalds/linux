/*
 *
 *  sep_driver_api.h - Security Processor Driver api definitions
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

#ifndef __SEP_DRIVER_API_H__
#define __SEP_DRIVER_API_H__



/*----------------------------------------------------------------
  IOCTL command defines
  -----------------------------------------------------------------*/

/* magic number 1 of the sep IOCTL command */
#define SEP_IOC_MAGIC_NUMBER                           's'

/* sends interrupt to sep that message is ready */
#define SEP_IOCSENDSEPCOMMAND                 _IO(SEP_IOC_MAGIC_NUMBER , 0)

/* sends interrupt to sep that message is ready */
#define SEP_IOCSENDSEPRPLYCOMMAND             _IO(SEP_IOC_MAGIC_NUMBER , 1)

/* allocate memory in data pool */
#define SEP_IOCALLOCDATAPOLL                  _IO(SEP_IOC_MAGIC_NUMBER , 2)

/* write to pre-allocated  memory in data pool */
#define SEP_IOCWRITEDATAPOLL                  _IO(SEP_IOC_MAGIC_NUMBER , 3)

/* read from  pre-allocated  memory in data pool */
#define SEP_IOCREADDATAPOLL                   _IO(SEP_IOC_MAGIC_NUMBER , 4)

/* create sym dma lli tables */
#define SEP_IOCCREATESYMDMATABLE              _IO(SEP_IOC_MAGIC_NUMBER , 5)

/* create flow dma lli tables */
#define SEP_IOCCREATEFLOWDMATABLE             _IO(SEP_IOC_MAGIC_NUMBER , 6)

/* free dynamic data aalocated during table creation */
#define SEP_IOCFREEDMATABLEDATA                _IO(SEP_IOC_MAGIC_NUMBER , 7)

/* get the static pool area addersses (physical and virtual) */
#define SEP_IOCGETSTATICPOOLADDR               _IO(SEP_IOC_MAGIC_NUMBER , 8)

/* set flow id command */
#define SEP_IOCSETFLOWID                       _IO(SEP_IOC_MAGIC_NUMBER , 9)

/* add tables to the dynamic flow */
#define SEP_IOCADDFLOWTABLE                    _IO(SEP_IOC_MAGIC_NUMBER , 10)

/* add flow add tables message */
#define SEP_IOCADDFLOWMESSAGE                  _IO(SEP_IOC_MAGIC_NUMBER , 11)

/* start sep command */
#define SEP_IOCSEPSTART                        _IO(SEP_IOC_MAGIC_NUMBER , 12)

/* init sep command */
#define SEP_IOCSEPINIT                         _IO(SEP_IOC_MAGIC_NUMBER , 13)

/* end transaction command */
#define SEP_IOCENDTRANSACTION                  _IO(SEP_IOC_MAGIC_NUMBER , 15)

/* reallocate cache and resident */
#define SEP_IOCREALLOCCACHERES                 _IO(SEP_IOC_MAGIC_NUMBER , 16)

/* get the offset of the address starting from the beginnnig of the map area */
#define SEP_IOCGETMAPPEDADDROFFSET             _IO(SEP_IOC_MAGIC_NUMBER , 17)

/* get time address and value */
#define SEP_IOCGETIME                          _IO(SEP_IOC_MAGIC_NUMBER , 19)

/*-------------------------------------------
    TYPEDEFS
----------------------------------------------*/

/*
  init command struct
*/
struct sep_driver_init_t {
	/* start of the 1G of the host memory address that SEP can access */
	unsigned long message_addr;

	/* start address of resident */
	unsigned long message_size_in_words;

};


/*
  realloc cache resident command
*/
struct sep_driver_realloc_cache_resident_t {
	/* new cache address */
	u64 new_cache_addr;
	/* new resident address */
	u64 new_resident_addr;
	/* new resident address */
	u64  new_shared_area_addr;
	/* new base address */
	u64 new_base_addr;
};

struct sep_driver_alloc_t {
	/* virtual address of allocated space */
	unsigned long offset;

	/* physical address of allocated space */
	unsigned long phys_address;

	/* number of bytes to allocate */
	unsigned long num_bytes;
};

/*
 */
struct sep_driver_write_t {
	/* application space address */
	unsigned long app_address;

	/* address of the data pool */
	unsigned long datapool_address;

	/* number of bytes to write */
	unsigned long num_bytes;
};

/*
 */
struct sep_driver_read_t {
	/* application space address */
	unsigned long app_address;

	/* address of the data pool */
	unsigned long datapool_address;

	/* number of bytes to read */
	unsigned long num_bytes;
};

/*
*/
struct sep_driver_build_sync_table_t {
	/* address value of the data in */
	unsigned long app_in_address;

	/* size of data in */
	unsigned long data_in_size;

	/* address of the data out */
	unsigned long app_out_address;

	/* the size of the block of the operation - if needed,
	   every table will be modulo this parameter */
	unsigned long block_size;

	/* the physical address of the first input DMA table */
	unsigned long in_table_address;

	/* number of entries in the first input DMA table */
	unsigned long in_table_num_entries;

	/* the physical address of the first output DMA table */
	unsigned long out_table_address;

	/* number of entries in the first output DMA table */
	unsigned long out_table_num_entries;

	/* data in the first input table */
	unsigned long table_data_size;

	/* distinct user/kernel layout */
	bool isKernelVirtualAddress;

};

/*
*/
struct sep_driver_build_flow_table_t {
	/* flow type */
	unsigned long flow_type;

	/* flag for input output */
	unsigned long input_output_flag;

	/* address value of the data in */
	unsigned long virt_buff_data_addr;

	/* size of data in */
	unsigned long num_virtual_buffers;

	/* the physical address of the first input DMA table */
	unsigned long first_table_addr;

	/* number of entries in the first input DMA table */
	unsigned long first_table_num_entries;

	/* data in the first input table */
	unsigned long first_table_data_size;

	/* distinct user/kernel layout */
	bool isKernelVirtualAddress;
};


struct sep_driver_add_flow_table_t {
	/* flow id  */
	unsigned long flow_id;

	/* flag for input output */
	unsigned long inputOutputFlag;

	/* address value of the data in */
	unsigned long virt_buff_data_addr;

	/* size of data in */
	unsigned long num_virtual_buffers;

	/* address of the first table */
	unsigned long first_table_addr;

	/* number of entries in the first table */
	unsigned long first_table_num_entries;

	/* data size of the first table */
	unsigned long first_table_data_size;

	/* distinct user/kernel layout */
	bool isKernelVirtualAddress;

};

/*
  command struct for set flow id
*/
struct sep_driver_set_flow_id_t {
	/* flow id to set */
	unsigned long flow_id;
};


/* command struct for add tables message */
struct sep_driver_add_message_t {
	/* flow id to set */
	unsigned long flow_id;

	/* message size in bytes */
	unsigned long message_size_in_bytes;

	/* address of the message */
	unsigned long message_address;
};

/* command struct for static pool addresses  */
struct sep_driver_static_pool_addr_t {
	/* physical address of the static pool */
	unsigned long physical_static_address;

	/* virtual address of the static pool */
	unsigned long virtual_static_address;
};

/* command struct for getiing offset of the physical address from
	the start of the mapped area  */
struct sep_driver_get_mapped_offset_t {
	/* physical address of the static pool */
	unsigned long physical_address;

	/* virtual address of the static pool */
	unsigned long offset;
};

/* command struct for getting time value and address */
struct sep_driver_get_time_t {
	/* physical address of stored time */
	unsigned long time_physical_address;

	/* value of the stored time */
	unsigned long time_value;
};


/*
  structure that represent one entry in the DMA LLI table
*/
struct sep_lli_entry_t {
	/* physical address */
	unsigned long physical_address;

	/* block size */
	unsigned long block_size;
};

/*
  structure that reperesents data needed for lli table construction
*/
struct sep_lli_prepare_table_data_t {
	/* pointer to the memory where the first lli entry to be built */
	struct sep_lli_entry_t *lli_entry_ptr;

	/* pointer to the array of lli entries from which the table is to be built */
	struct sep_lli_entry_t *lli_array_ptr;

	/* number of elements in lli array */
	int lli_array_size;

	/* number of entries in the created table */
	int num_table_entries;

	/* number of array entries processed during table creation */
	int num_array_entries_processed;

	/* the totatl data size in the created table */
	int lli_table_total_data_size;
};

/*
  structure that represent tone table - it is not used in code, jkust
  to show what table looks like
*/
struct sep_lli_table_t {
	/* number of pages mapped in this tables. If 0 - means that the table
	   is not defined (used as a valid flag) */
	unsigned long num_pages;
	/*
	   pointer to array of page pointers that represent the mapping of the
	   virtual buffer defined by the table to the physical memory. If this
	   pointer is NULL, it means that the table is not defined
	   (used as a valid flag)
	 */
	struct page **table_page_array_ptr;

	/* maximum flow entries in table */
	struct sep_lli_entry_t lli_entries[SEP_DRIVER_MAX_FLOW_NUM_ENTRIES_IN_TABLE];
};


/*
  structure for keeping the mapping of the virtual buffer into physical pages
*/
struct sep_flow_buffer_data {
	/* pointer to the array of page structs pointers to the pages of the
	   virtual buffer */
	struct page **page_array_ptr;

	/* number of pages taken by the virtual buffer */
	unsigned long num_pages;

	/* this flag signals if this page_array is the last one among many that were
	   sent in one setting to SEP */
	unsigned long last_page_array_flag;
};

/*
  struct that keeps all the data for one flow
*/
struct sep_flow_context_t {
	/*
	   work struct for handling the flow done interrupt in the workqueue
	   this structure must be in the first place, since it will be used
	   forcasting to the containing flow context
	 */
	struct work_struct flow_wq;

	/* flow id */
	unsigned long flow_id;

	/* additional input tables exists */
	unsigned long input_tables_flag;

	/* additional output tables exists */
	unsigned long output_tables_flag;

	/*  data of the first input file */
	struct sep_lli_entry_t first_input_table;

	/* data of the first output table */
	struct sep_lli_entry_t first_output_table;

	/* last input table data */
	struct sep_lli_entry_t last_input_table;

	/* last output table data */
	struct sep_lli_entry_t last_output_table;

	/* first list of table */
	struct sep_lli_entry_t input_tables_in_process;

	/* output table in process (in sep) */
	struct sep_lli_entry_t output_tables_in_process;

	/* size of messages in bytes */
	unsigned long message_size_in_bytes;

	/* message */
	unsigned char message[SEP_MAX_ADD_MESSAGE_LENGTH_IN_BYTES];
};


#endif
