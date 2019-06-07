/*
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _TA_XGMI_IF_H
#define _TA_XGMI_IF_H

/* Responses have bit 31 set */
#define RSP_ID_MASK (1U << 31)
#define RSP_ID(cmdId) (((uint32_t)(cmdId)) | RSP_ID_MASK)

enum ta_command_xgmi {
	TA_COMMAND_XGMI__INITIALIZE			= 0x00,
	TA_COMMAND_XGMI__GET_NODE_ID			= 0x01,
	TA_COMMAND_XGMI__GET_HIVE_ID			= 0x02,
	TA_COMMAND_XGMI__GET_GET_TOPOLOGY_INFO		= 0x03,
	TA_COMMAND_XGMI__SET_TOPOLOGY_INFO		= 0x04
};

/* XGMI related enumerations */
/**********************************************************/;
enum ta_xgmi_connected_nodes {
	TA_XGMI__MAX_CONNECTED_NODES			= 64
};

enum ta_xgmi_status {
	TA_XGMI_STATUS__SUCCESS				= 0x00,
	TA_XGMI_STATUS__GENERIC_FAILURE			= 0x01,
	TA_XGMI_STATUS__NULL_POINTER			= 0x02,
	TA_XGMI_STATUS__INVALID_PARAMETER		= 0x03,
	TA_XGMI_STATUS__NOT_INITIALIZED			= 0x04,
	TA_XGMI_STATUS__INVALID_NODE_NUM		= 0x05,
	TA_XGMI_STATUS__INVALID_NODE_ID			= 0x06,
	TA_XGMI_STATUS__INVALID_TOPOLOGY		= 0x07,
	TA_XGMI_STATUS__FAILED_ID_GEN			= 0x08,
	TA_XGMI_STATUS__FAILED_TOPOLOGY_INIT		= 0x09,
	TA_XGMI_STATUS__SET_SHARING_ERROR		= 0x0A
};

enum ta_xgmi_assigned_sdma_engine {
	TA_XGMI_ASSIGNED_SDMA_ENGINE__NOT_ASSIGNED	= -1,
	TA_XGMI_ASSIGNED_SDMA_ENGINE__SDMA0		= 0,
	TA_XGMI_ASSIGNED_SDMA_ENGINE__SDMA1		= 1,
	TA_XGMI_ASSIGNED_SDMA_ENGINE__SDMA2		= 2,
	TA_XGMI_ASSIGNED_SDMA_ENGINE__SDMA3		= 3,
	TA_XGMI_ASSIGNED_SDMA_ENGINE__SDMA4		= 4,
	TA_XGMI_ASSIGNED_SDMA_ENGINE__SDMA5		= 5
};

/* input/output structures for XGMI commands */
/**********************************************************/
struct ta_xgmi_node_info {
	uint64_t				node_id;
	uint8_t					num_hops;
	uint8_t					is_sharing_enabled;
	enum ta_xgmi_assigned_sdma_engine	sdma_engine;
};

struct ta_xgmi_cmd_initialize_output {
	uint32_t	status;
};

struct ta_xgmi_cmd_get_node_id_output {
	uint64_t	node_id;
};

struct ta_xgmi_cmd_get_hive_id_output {
	uint64_t	hive_id;
};

struct ta_xgmi_cmd_get_topology_info_input {
	uint32_t			num_nodes;
	struct ta_xgmi_node_info	nodes[TA_XGMI__MAX_CONNECTED_NODES];
};

struct ta_xgmi_cmd_get_topology_info_output {
	uint32_t			num_nodes;
	struct ta_xgmi_node_info	nodes[TA_XGMI__MAX_CONNECTED_NODES];
};

struct ta_xgmi_cmd_set_topology_info_input {
	uint32_t			num_nodes;
	struct ta_xgmi_node_info	nodes[TA_XGMI__MAX_CONNECTED_NODES];
};

/**********************************************************/
/* Common input structure for XGMI callbacks */
union ta_xgmi_cmd_input {
	struct ta_xgmi_cmd_get_topology_info_input	get_topology_info;
	struct ta_xgmi_cmd_set_topology_info_input	set_topology_info;
};

/* Common output structure for XGMI callbacks */
union ta_xgmi_cmd_output {
	struct ta_xgmi_cmd_initialize_output		initialize;
	struct ta_xgmi_cmd_get_node_id_output		get_node_id;
	struct ta_xgmi_cmd_get_hive_id_output		get_hive_id;
	struct ta_xgmi_cmd_get_topology_info_output	get_topology_info;
};
/**********************************************************/

struct ta_xgmi_shared_memory {
	uint32_t			cmd_id;
	uint32_t			resp_id;
	enum ta_xgmi_status		xgmi_status;
	uint32_t			reserved;
	union ta_xgmi_cmd_input		xgmi_in_message;
	union ta_xgmi_cmd_output	xgmi_out_message;
};

#endif   //_TA_XGMI_IF_H
