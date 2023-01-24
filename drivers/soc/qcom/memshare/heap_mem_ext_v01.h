/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2013-2015, 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef HEAP_MEM_EXT_SERVICE_01_H
#define HEAP_MEM_EXT_SERVICE_01_H

#include <linux/soc/qcom/qmi.h>

#define MEM_ALLOC_REQ_MAX_MSG_LEN_V01 255
#define MEM_FREE_REQ_MAX_MSG_LEN_V01 255
#define MEM_QUERY_MAX_MSG_LEN_V01 255
#define MAX_ARR_CNT_V01 64

struct dhms_mem_alloc_addr_info_type_v01 {
	uint64_t phy_addr;
	uint32_t num_bytes;
};

enum dhms_mem_proc_id_v01 {
	/* To force a 32 bit signed enum.  Do not change or use */
	DHMS_MEM_PROC_ID_MIN_ENUM_VAL_V01 = -2147483647,
	/* Request from MPSS processor */
	DHMS_MEM_PROC_MPSS_V01 = 0,
	/* Request from ADSP processor */
	DHMS_MEM_PROC_ADSP_V01 = 1,
	/* Request from WCNSS processor */
	DHMS_MEM_PROC_WCNSS_V01 = 2,
	/* To force a 32 bit signed enum.  Do not change or use */
	DHMS_MEM_PROC_ID_MAX_ENUM_VAL_V01 = 2147483647
};

enum dhms_mem_client_id_v01 {
	/*To force a 32 bit signed enum.  Do not change or use*/
	DHMS_MEM_CLIENT_ID_MIN_ENUM_VAL_V01 = -2147483647,
	/*  Request from GPS Client    */
	DHMS_MEM_CLIENT_GPS_V01 = 0,
	/* Invalid Client */
	DHMS_MEM_CLIENT_INVALID = 1000,
	/* To force a 32 bit signed enum.  Do not change or use */
	DHMS_MEM_CLIENT_ID_MAX_ENUM_VAL_V01 = 2147483647
};

enum dhms_mem_block_align_enum_v01 {
	/* To force a 32 bit signed enum.  Do not change or use
	 */
	DHMS_MEM_BLOCK_ALIGN_ENUM_MIN_ENUM_VAL_V01 = -2147483647,
	/* Align allocated memory by 2 bytes  */
	DHMS_MEM_BLOCK_ALIGN_2_V01 = 0,
	/* Align allocated memory by 4 bytes  */
	DHMS_MEM_BLOCK_ALIGN_4_V01 = 1,
	/**<  Align allocated memory by 8 bytes */
	DHMS_MEM_BLOCK_ALIGN_8_V01 = 2,
	/**<  Align allocated memory by 16 bytes */
	DHMS_MEM_BLOCK_ALIGN_16_V01 = 3,
	/**<  Align allocated memory by 32 bytes */
	DHMS_MEM_BLOCK_ALIGN_32_V01 = 4,
	/**<  Align allocated memory by 64 bytes */
	DHMS_MEM_BLOCK_ALIGN_64_V01 = 5,
	/**<  Align allocated memory by 128 bytes */
	DHMS_MEM_BLOCK_ALIGN_128_V01 = 6,
	/**<  Align allocated memory by 256 bytes */
	DHMS_MEM_BLOCK_ALIGN_256_V01 = 7,
	/**<  Align allocated memory by 512 bytes */
	DHMS_MEM_BLOCK_ALIGN_512_V01 = 8,
	/**<  Align allocated memory by 1024 bytes */
	DHMS_MEM_BLOCK_ALIGN_1K_V01 = 9,
	/**<  Align allocated memory by 2048 bytes */
	DHMS_MEM_BLOCK_ALIGN_2K_V01 = 10,
	/**<  Align allocated memory by 4096 bytes */
	DHMS_MEM_BLOCK_ALIGN_4K_V01 = 11,
	DHMS_MEM_BLOCK_ALIGN_ENUM_MAX_ENUM_VAL_V01 = 2147483647
	/* To force a 32 bit signed enum.  Do not change or use
	 */
};

/* Request Message; This command is used for getting
 * the multiple physically contiguous
 * memory blocks from the server memory subsystem
 */
struct mem_alloc_generic_req_msg_v01 {

	/* Mandatory */
	/*requested size*/
	uint32_t num_bytes;

	/* Mandatory */
	/* client id */
	enum dhms_mem_client_id_v01 client_id;

	/* Mandatory */
	/* Peripheral Id*/
	enum dhms_mem_proc_id_v01 proc_id;

	/* Mandatory */
	/* Sequence id */
	uint32_t sequence_id;

	/* Optional */
	/*  alloc_contiguous */
	/* Must be set to true if alloc_contiguous is being passed */
	uint8_t alloc_contiguous_valid;

	/* Alloc_contiguous is used to identify that clients are requesting
	 * for contiguous or non contiguous memory, default is contiguous
	 * 0 = non contiguous else contiguous
	 */
	uint8_t alloc_contiguous;

	/* Optional */
	/* Must be set to true if block_alignment
	 * is being passed
	 */
	uint8_t block_alignment_valid;

	/* The block alignment for the memory block to be allocated
	 */
	enum dhms_mem_block_align_enum_v01 block_alignment;

};  /* Message */

/* Response Message; This command is used for getting
 * the multiple physically contiguous memory blocks
 * from the server memory subsystem
 */
struct mem_alloc_generic_resp_msg_v01 {

	/* Mandatory */
	/*  Result Code */
	/* The result of the requested memory operation
	 */
	struct qmi_response_type_v01 resp;

	/* Optional */
	/* Sequence ID */
	/* Must be set to true if sequence_id is being passed */
	uint8_t sequence_id_valid;


	/* Mandatory */
	/* Sequence id */
	uint32_t sequence_id;

	/* Optional */
	/*  Memory Block Handle
	 */
	/* Must be set to true if handle is being passed
	 */
	uint8_t dhms_mem_alloc_addr_info_valid;

	/* Optional */
	/* Handle Size */
	uint32_t dhms_mem_alloc_addr_info_len;

	/* Optional */
	/* The physical address of the memory allocated on the HLOS
	 */
	struct dhms_mem_alloc_addr_info_type_v01
		dhms_mem_alloc_addr_info[MAX_ARR_CNT_V01];

};  /* Message */

/* Request Message; This command is used for releasing
 * the multiple physically contiguous
 * memory blocks to the server memory subsystem
 */
struct mem_free_generic_req_msg_v01 {

	/* Mandatory */
	/* Must be set to # of  elments in array*/
	uint32_t dhms_mem_alloc_addr_info_len;

	/* Mandatory */
	/* Physical address and size of the memory allocated
	 * on the HLOS to be freed.
	 */
	struct dhms_mem_alloc_addr_info_type_v01
			dhms_mem_alloc_addr_info[MAX_ARR_CNT_V01];

	/* Optional */
	/* Client ID */
	/* Must be set to true if client_id is being passed */
	uint8_t client_id_valid;

	/* Optional */
	/* Client Id */
	enum dhms_mem_client_id_v01 client_id;

	/* Optional */
	/* Proc ID */
	/* Must be set to true if proc_id is being passed */
	uint8_t proc_id_valid;

	/* Optional */
	/* Peripheral */
	enum dhms_mem_proc_id_v01 proc_id;

};  /* Message */

/* Response Message; This command is used for releasing
 * the multiple physically contiguous
 * memory blocks to the server memory subsystem
 */
struct mem_free_generic_resp_msg_v01 {

	/*
	 * Mandatory
	 * Result of the requested memory operation, todo,
	 * need to check the async operation for free
	 */
	struct qmi_response_type_v01 resp;

};  /* Message */

struct mem_query_size_req_msg_v01 {

	/* Mandatory */
	enum dhms_mem_client_id_v01 client_id;

	/*
	 * Optional
	 * Proc ID
	 * proc_id_valid must be set to true if proc_id is being passed
	 */
	uint8_t proc_id_valid;

	enum dhms_mem_proc_id_v01 proc_id;
};  /* Message */

struct mem_query_size_rsp_msg_v01 {

	/*
	 * Mandatory
	 * Result Code
	 */
	struct qmi_response_type_v01 resp;

	/*
	 * Optional
	 * size_valid must be set to true if size is being passed
	 */
	uint8_t size_valid;

	uint32_t size;
};  /* Message */


extern struct qmi_elem_info mem_alloc_generic_req_msg_data_v01_ei[];
extern struct qmi_elem_info mem_alloc_generic_resp_msg_data_v01_ei[];
extern struct qmi_elem_info mem_free_generic_req_msg_data_v01_ei[];
extern struct qmi_elem_info mem_free_generic_resp_msg_data_v01_ei[];
extern struct qmi_elem_info mem_query_size_req_msg_data_v01_ei[];
extern struct qmi_elem_info mem_query_size_resp_msg_data_v01_ei[];

/*Service Message Definition*/
#define MEM_ALLOC_GENERIC_REQ_MSG_V01 0x0022
#define MEM_ALLOC_GENERIC_RESP_MSG_V01 0x0022
#define MEM_FREE_GENERIC_REQ_MSG_V01 0x0023
#define MEM_FREE_GENERIC_RESP_MSG_V01 0x0023
#define MEM_QUERY_SIZE_REQ_MSG_V01	0x0024
#define MEM_QUERY_SIZE_RESP_MSG_V01	0x0024

#endif
