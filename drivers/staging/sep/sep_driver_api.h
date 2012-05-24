/*
 *
 *  sep_driver_api.h - Security Processor Driver api definitions
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
 *  2010.09.14  Upgrade to Medfield
 *  2011.02.22  Enable kernel crypto
 *
 */

#ifndef __SEP_DRIVER_API_H__
#define __SEP_DRIVER_API_H__

/* Type of request from device */
#define SEP_DRIVER_SRC_REPLY		1
#define SEP_DRIVER_SRC_REQ		2
#define SEP_DRIVER_SRC_PRINTF		3

/* Power state */
#define SEP_DRIVER_POWERON		1
#define SEP_DRIVER_POWEROFF		2

/* Following enums are used only for kernel crypto api */
enum type_of_request {
	NO_REQUEST,
	AES_CBC,
	AES_ECB,
	DES_CBC,
	DES_ECB,
	DES3_ECB,
	DES3_CBC,
	SHA1,
	MD5,
	SHA224,
	SHA256
	};

enum hash_stage {
	HASH_INIT,
	HASH_UPDATE,
	HASH_FINISH,
	HASH_DIGEST,
	HASH_FINUP_DATA,
	HASH_FINUP_FINISH
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
	aligned_u64 out_vr_tail_pt;
	/* size of tail data */
	u32	tail_data_size;
	/* input tail data array */
	u8	tail_data[68];
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

	/* which application calls the driver DX or applet */
	u32  is_applet;
};

/*
	command structure for building dcb block for kernel crypto
*/
struct build_dcb_struct_kernel {
	/* address value of the data in */
	void *app_in_address;
	/* size of data in */
	ssize_t  data_in_size;
	/* address of the data out */
	void *app_out_address;
	/* the size of the block of the operation - if needed,
	every table will be modulo this parameter */
	u32  block_size;
	/* the size of the block of the operation - if needed,
	every table will be modulo this parameter */
	u32  tail_block_size;

	/* which application calls the driver DX or applet */
	u32  is_applet;

	struct scatterlist *src_sg;
	struct scatterlist *dst_sg;
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

	/* Scatter list for kernel operations */
	struct scatterlist *src_sg;
	struct scatterlist *dst_sg;
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

/*
 * header format for each fastcall write operation
 */
struct sep_fastcall_hdr {
	u32 magic;
	u32 secure_dma;
	u32 msg_len;
	u32 num_dcbs;
};

/*
 * structure used in file pointer's private data field
 * to track the status of the calls to the various
 * driver interface
 */
struct sep_call_status {
	unsigned long status;
};

/*
 * format of dma context buffer used to store all DMA-related
 * context information of a particular transaction
 */
struct sep_dma_context {
	/* number of data control blocks */
	u32 nr_dcb_creat;
	/* number of the lli tables created in the current transaction */
	u32 num_lli_tables_created;
	/* size of currently allocated dma tables region */
	u32 dmatables_len;
	/* size of input data */
	u32 input_data_len;
	/* secure dma use (for imr memory restriced area in output */
	bool secure_dma;
	struct sep_dma_resource dma_res_arr[SEP_MAX_NUM_SYNC_DMA_OPS];
	/* Scatter gather for kernel crypto */
	struct scatterlist *src_sg;
	struct scatterlist *dst_sg;
};

/*
 * format for file pointer's private_data field
 */
struct sep_private_data {
	struct sep_queue_info *my_queue_elem;
	struct sep_device *device;
	struct sep_call_status call_status;
	struct sep_dma_context *dma_ctx;
};


/* Functions used by sep_crypto */

/**
 * sep_queue_status_remove - Removes transaction from status queue
 * @sep: SEP device
 * @sep_queue_info: pointer to status queue
 *
 * This function will removes information about transaction from the queue.
 */
void sep_queue_status_remove(struct sep_device *sep,
				      struct sep_queue_info **queue_elem);
/**
 * sep_queue_status_add - Adds transaction to status queue
 * @sep: SEP device
 * @opcode: transaction opcode
 * @size: input data size
 * @pid: pid of current process
 * @name: current process name
 * @name_len: length of name (current process)
 *
 * This function adds information about about transaction started to the status
 * queue.
 */
struct sep_queue_info *sep_queue_status_add(
						struct sep_device *sep,
						u32 opcode,
						u32 size,
						u32 pid,
						u8 *name, size_t name_len);

/**
 *	sep_create_dcb_dmatables_context_kernel - Creates DCB & MLLI/DMA table context
 *      for kernel crypto
 *	@sep: SEP device
 *	@dcb_region: DCB region buf to create for current transaction
 *	@dmatables_region: MLLI/DMA tables buf to create for current transaction
 *	@dma_ctx: DMA context buf to create for current transaction
 *	@user_dcb_args: User arguments for DCB/MLLI creation
 *	@num_dcbs: Number of DCBs to create
 */
int sep_create_dcb_dmatables_context_kernel(struct sep_device *sep,
			struct sep_dcblock **dcb_region,
			void **dmatables_region,
			struct sep_dma_context **dma_ctx,
			const struct build_dcb_struct_kernel *dcb_data,
			const u32 num_dcbs);

/**
 *	sep_activate_dcb_dmatables_context - Takes DCB & DMA tables
 *						contexts into use
 *	@sep: SEP device
 *	@dcb_region: DCB region copy
 *	@dmatables_region: MLLI/DMA tables copy
 *	@dma_ctx: DMA context for current transaction
 */
ssize_t sep_activate_dcb_dmatables_context(struct sep_device *sep,
					struct sep_dcblock **dcb_region,
					void **dmatables_region,
					struct sep_dma_context *dma_ctx);

/**
 * sep_prepare_input_output_dma_table_in_dcb - prepare control blocks
 * @app_in_address: unsigned long; for data buffer in (user space)
 * @app_out_address: unsigned long; for data buffer out (user space)
 * @data_in_size: u32; for size of data
 * @block_size: u32; for block size
 * @tail_block_size: u32; for size of tail block
 * @isapplet: bool; to indicate external app
 * @is_kva: bool; kernel buffer; only used for kernel crypto module
 * @secure_dma; indicates whether this is secure_dma using IMR
 *
 * This function prepares the linked DMA tables and puts the
 * address for the linked list of tables inta a DCB (data control
 * block) the address of which is known by the SEP hardware
 * Note that all bus addresses that are passed to the SEP
 * are in 32 bit format; the SEP is a 32 bit device
 */
int sep_prepare_input_output_dma_table_in_dcb(struct sep_device *sep,
	unsigned long  app_in_address,
	unsigned long  app_out_address,
	u32  data_in_size,
	u32  block_size,
	u32  tail_block_size,
	bool isapplet,
	bool	is_kva,
	bool    secure_dma,
	struct sep_dcblock *dcb_region,
	void **dmatables_region,
	struct sep_dma_context **dma_ctx,
	struct scatterlist *src_sg,
	struct scatterlist *dst_sg);

/**
 * sep_free_dma_table_data_handler - free DMA table
 * @sep: pointere to struct sep_device
 * @dma_ctx: dma context
 *
 * Handles the request to  free DMA table for synchronic actions
 */
int sep_free_dma_table_data_handler(struct sep_device *sep,
					   struct sep_dma_context **dma_ctx);
/**
 * sep_send_command_handler - kick off a command
 * @sep: SEP being signalled
 *
 * This function raises interrupt to SEP that signals that is has a new
 * command from the host
 *
 * Note that this function does fall under the ioctl lock
 */
int sep_send_command_handler(struct sep_device *sep);

/**
 *	sep_wait_transaction - Used for synchronizing transactions
 *	@sep: SEP device
 */
int sep_wait_transaction(struct sep_device *sep);

/**
 * IOCTL command defines
 */
/* magic number 1 of the sep IOCTL command */
#define SEP_IOC_MAGIC_NUMBER	's'

/* sends interrupt to sep that message is ready */
#define SEP_IOCSENDSEPCOMMAND	 \
	_IO(SEP_IOC_MAGIC_NUMBER, 0)

/* end transaction command */
#define SEP_IOCENDTRANSACTION	 \
	_IO(SEP_IOC_MAGIC_NUMBER, 15)

#define SEP_IOCPREPAREDCB					\
	_IOW(SEP_IOC_MAGIC_NUMBER, 35, struct build_dcb_struct)

#define SEP_IOCFREEDCB					\
	_IO(SEP_IOC_MAGIC_NUMBER, 36)

struct sep_device;

#define SEP_IOCPREPAREDCB_SECURE_DMA	\
	_IOW(SEP_IOC_MAGIC_NUMBER, 38, struct build_dcb_struct)

#define SEP_IOCFREEDCB_SECURE_DMA	\
	_IO(SEP_IOC_MAGIC_NUMBER, 39)

#endif
