/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2013-2015, 2017-2018, The Linux Foundation. All rights reserved. */

#ifndef HEAP_MEM_EXT_SERVICE_01_H
#define HEAP_MEM_EXT_SERVICE_01_H

#include <linux/types.h>
#include <linux/soc/qcom/qmi.h>

#define MEM_SERVICE_SVC_ID 0x00000034
#define MEM_SERVICE_INS_ID 1
#define MEM_SERVICE_VER 1

/* Service Message Definition */
#define MEM_ALLOC_MSG_V01 0x0020
#define MEM_FREE_MSG_V01 0x0021
#define MEM_ALLOC_GENERIC_MSG_V01 0x0022
#define MEM_FREE_GENERIC_MSG_V01 0x0023
#define MEM_QUERY_SIZE_MSG_V01 0x0024

#define MEM_MAX_MSG_LEN_V01 255
#define MAX_ARR_CNT_V01 64

#define MEM_BLOCK_ALIGN_TO_BYTES(x) (2 << (x)) /* 0..11 */

/**
 * Unless stated otherwise, any property X that is paired with X_valid
 * property is an optional property. Other properties are mandatory.
 */

/**
 * struct dhms_mem_alloc_addr_info_type_v01 - Element of memory block array.
 * @phy_addr: Physical address of memory block.
 * @num_bytes: Size of memory block.
 */
struct dhms_mem_alloc_addr_info_type_v01 {
	u64 phy_addr;
	u32 num_bytes;
};

/**
 * struct mem_alloc_req_msg_v01 - Legacy memory allocation request.
 * @num_bytes: Requested size.
 * @block_alignment_valid: Must be set to true if block_alignment is being passed.
 * @block_alignment: The block alignment for the memory block to be allocated.
 *
 * Request Message.
 * This command is used for getting the multiple physically contiguous memory
 * blocks from the server memory subsystem.
 */
struct mem_alloc_req_msg_v01 {
	u32 num_bytes;
	u8 block_alignment_valid;
	u32 block_alignment;
};

/**
 * struct mem_alloc_resp_msg_v01 - Response for legacy allocation memory request.
 * @resp: Result Code. The result of the requested memory operation.
 * @handle_valid: Must be set to true if handle is being passed.
 * @handle: Memory Block Handle. The physical address of the memory allocated on the HLOS.
 * @num_bytes_valid: Must be set to true if num_bytes is being passed.
 * @num_bytes: Memory block size. The number of bytes actually allocated for the request.
 *             This value can be smaller than the size requested in QMI_DHMS_MEM_ALLOC_REQ_MSG.
 *
 * Response Message.
 * This command is used for getting the multiple physically contiguous memory
 * blocks from the server memory subsystem
 */
struct mem_alloc_resp_msg_v01 {
	u16 resp;
	u8 handle_valid;
	u64 handle;
	u8 num_bytes_valid;
	u32 num_bytes;
};

/**
 * struct mem_free_req_msg_v01 - Legacy memory free request.
 * @handle: Physical address of memory to be freed.
 *
 * Request Message.
 * This command is used for releasing the multiple physically contiguous memory
 * blocks to the server memory subsystem.
 */
struct mem_free_req_msg_v01 {
	u64 handle;
};

/**
 * struct mem_free_resp_msg_v01 - Response for legacy memory free request.
 * @resp: Result of the requested memory operation.
 *
 * Response Message.
 * This command is used for releasing the multiple physically contiguous memory
 * blocks to the server memory subsystem.
 */
struct mem_free_resp_msg_v01 {
	u16 resp;
};

/**
 * struct mem_alloc_generic_req_msg_v01 - Memory allocation request.
 * @num_bytes: Requested size.
 * @client_id: Client ID.
 * @proc_id: Peripheral ID.
 * @sequence_id: Sequence ID.
 * @alloc_contiguous_valid: Must be set to true if alloc_contiguous is being passed.
 * @alloc_contiguous: alloc_contiguous is used to identify that clients are requesting
 *                    for contiguous or non contiguous memory, default is contiguous.
 *                    0 = non contiguous else contiguous.
 * @block_alignment_valid: Must be set to true if block_alignment is being passed.
 * @block_alignment: The block alignment for the memory block to be allocated.
 *
 * Request Message.
 * This command is used for getting the multiple physically contiguous memory
 * blocks from the server memory subsystem
 */
struct mem_alloc_generic_req_msg_v01 {
	u32 num_bytes;
	u32 client_id;
	u32 proc_id;
	u32 sequence_id;
	u8 alloc_contiguous_valid;
	u8 alloc_contiguous;
	u8 block_alignment_valid;
	u32 block_alignment;
};

/**
 * struct mem_alloc_generic_resp_msg_v01 - Response for memory allocation request.
 * @resp: Result Code. The result of the requested memory operation.
 * @sequence_id_valid: Must be set to true if sequence_id is being passed.
 * @sequence_id: Sequence ID. This property is marked as mandatory.
 * @dhms_mem_alloc_addr_info_valid: Must be set to true if handle is being passed.
 * @dhms_mem_alloc_addr_info_len: Handle Size.
 * @dhms_mem_alloc_addr_info: Memory Block Handle. The physical address of the
 *                            memory allocated on the HLOS.
 *
 * Response Message.
 * This command is used for getting the multiple physically contiguous memory
 * blocks from the server memory subsystem
 */
struct mem_alloc_generic_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
	u8 sequence_id_valid;
	u32 sequence_id;
	u8 dhms_mem_alloc_addr_info_valid;
	u8 dhms_mem_alloc_addr_info_len;
	struct dhms_mem_alloc_addr_info_type_v01 dhms_mem_alloc_addr_info[MAX_ARR_CNT_V01];
};

/**
 * struct mem_free_generic_req_msg_v01 - Memory free request.
 * @dhms_mem_alloc_addr_info_len: Must be set to # of elments in array.
 * @dhms_mem_alloc_addr_info: Physical address and size of the memory allocated
 *                            on the HLOS to be freed.
 * @client_id_valid: Must be set to true if client_id is being passed.
 * @client_id: Client ID.
 * @proc_id_valid: Must be set to true if proc_id is being passed.
 * @proc_id: Peripheral ID.
 *
 * Request Message.
 * This command is used for releasing the multiple physically contiguous memory
 * blocks to the server memory subsystem
 */
struct mem_free_generic_req_msg_v01 {
	u8 dhms_mem_alloc_addr_info_len;
	struct dhms_mem_alloc_addr_info_type_v01 dhms_mem_alloc_addr_info[MAX_ARR_CNT_V01];
	u8 client_id_valid;
	u32 client_id;
	u8 proc_id_valid;
	u32 proc_id;
};

/**
 * struct mem_free_generic_resp_msg_v01 - Response for memory free request.
 * @resp: Result of the requested memory operation.
 *
 * Response Message.
 * This command is used for releasing the multiple physically contiguous memory
 * blocks to the server memory subsystem
 */
struct mem_free_generic_resp_msg_v01 {
	struct qmi_response_type_v01 resp;
};

/**
 * struct mem_query_size_req_msg_v01 - Size query request.
 * @client_id: Client ID.
 * @proc_id_valid: proc_id_valid must be set to true if proc_id is being passed.
 * @proc_id: Proc ID.
 *
 * Request Message.
 */
struct mem_query_size_req_msg_v01 {
	u32 client_id;
	u8 proc_id_valid;
	u32 proc_id;
};

/**
 * struct mem_query_size_rsp_msg_v01 - Response for Size query request.
 * @resp: Result Code.
 * @size_valid: size_valid must be set to true if size is being passed.
 * @size: Size.
 *
 * Response Message.
 */
struct mem_query_size_rsp_msg_v01 {
	struct qmi_response_type_v01 resp;
	u8 size_valid;
	u32 size;
};

/* Message structure definitions defined in "memshare_qmi_msg.c" */
extern struct qmi_elem_info mem_alloc_req_msg_data_v01_ei[];
extern struct qmi_elem_info mem_alloc_resp_msg_data_v01_ei[];
extern struct qmi_elem_info mem_free_req_msg_data_v01_ei[];
extern struct qmi_elem_info mem_free_resp_msg_data_v01_ei[];
extern struct qmi_elem_info mem_alloc_generic_req_msg_data_v01_ei[];
extern struct qmi_elem_info mem_alloc_generic_resp_msg_data_v01_ei[];
extern struct qmi_elem_info mem_free_generic_req_msg_data_v01_ei[];
extern struct qmi_elem_info mem_free_generic_resp_msg_data_v01_ei[];
extern struct qmi_elem_info mem_query_size_req_msg_data_v01_ei[];
extern struct qmi_elem_info mem_query_size_resp_msg_data_v01_ei[];

#endif
