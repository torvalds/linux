/*
 * Copyright (c) 2005-2011 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef RDMA_CMA_ABI_H
#define RDMA_CMA_ABI_H

#include <infiniband/kern-abi.h>
#include <rdma/ib_user_sa.h>
#include <infiniband/sa.h>
#include <netinet/in.h>

/*
 * This file must be kept in sync with the kernel's version of rdma_user_cm.h
 */

#define RDMA_USER_CM_MIN_ABI_VERSION	3
#define RDMA_USER_CM_MAX_ABI_VERSION	4

#define RDMA_MAX_PRIVATE_DATA		256

enum {
	UCMA_CMD_CREATE_ID,
	UCMA_CMD_DESTROY_ID,
	UCMA_CMD_BIND_IP,
	UCMA_CMD_RESOLVE_IP,
	UCMA_CMD_RESOLVE_ROUTE,
	UCMA_CMD_QUERY_ROUTE,
	UCMA_CMD_CONNECT,
	UCMA_CMD_LISTEN,
	UCMA_CMD_ACCEPT,
	UCMA_CMD_REJECT,
	UCMA_CMD_DISCONNECT,
	UCMA_CMD_INIT_QP_ATTR,
	UCMA_CMD_GET_EVENT,
	UCMA_CMD_GET_OPTION,
	UCMA_CMD_SET_OPTION,
	UCMA_CMD_NOTIFY,
 	UCMA_CMD_JOIN_IP_MCAST,
 	UCMA_CMD_LEAVE_MCAST,
	UCMA_CMD_MIGRATE_ID,
	UCMA_CMD_QUERY,
	UCMA_CMD_BIND,
	UCMA_CMD_RESOLVE_ADDR,
	UCMA_CMD_JOIN_MCAST
};

struct ucma_abi_cmd_hdr {
	__u32 cmd;
	__u16 in;
	__u16 out;
};

struct ucma_abi_create_id {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 uid;
	__u64 response;
	__u16 ps;
	__u8  qp_type;
	__u8  reserved[5];
};

struct ucma_abi_create_id_resp {
	__u32 id;
};

struct ucma_abi_destroy_id {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 response;
	__u32 id;
	__u32 reserved;
};

struct ucma_abi_destroy_id_resp {
	__u32 events_reported;
};

struct ucma_abi_bind_ip {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 response;
	struct sockaddr_in6 addr;
	__u32 id;
};

struct ucma_abi_bind {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u32 id;
	__u16 addr_size;
	__u16 reserved;
	struct sockaddr_storage addr;
};

struct ucma_abi_resolve_ip {
	__u32 cmd;
	__u16 in;
	__u16 out;
	struct sockaddr_in6 src_addr;
	struct sockaddr_in6 dst_addr;
	__u32 id;
	__u32 timeout_ms;
};

struct ucma_abi_resolve_addr {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u32 id;
	__u32 timeout_ms;
	__u16 src_size;
	__u16 dst_size;
	__u32 reserved;
	struct sockaddr_storage src_addr;
	struct sockaddr_storage dst_addr;
};

struct ucma_abi_resolve_route {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u32 id;
	__u32 timeout_ms;
};

enum {
	UCMA_QUERY_ADDR,
	UCMA_QUERY_PATH,
	UCMA_QUERY_GID
};

struct ucma_abi_query {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 response;
	__u32 id;
	__u32 option;
};

struct ucma_abi_query_route_resp {
	__be64 node_guid;
	struct ib_user_path_rec ib_route[2];
	struct sockaddr_in6 src_addr;
	struct sockaddr_in6 dst_addr;
	__u32 num_paths;
	__u8 port_num;
	__u8 reserved[3];
};

struct ucma_abi_query_addr_resp {
	__be64 node_guid;
	__u8  port_num;
	__u8  reserved;
	__be16 pkey;
	__u16 src_size;
	__u16 dst_size;
	struct sockaddr_storage src_addr;
	struct sockaddr_storage dst_addr;
};

struct ucma_abi_query_path_resp {
	__u32 num_paths;
	__u32 reserved;
	struct ibv_path_data path_data[0];
};

struct ucma_abi_conn_param {
	__u32 qp_num;
	__u32 reserved;
	__u8  private_data[RDMA_MAX_PRIVATE_DATA];
	__u8  private_data_len;
	__u8  srq;
	__u8  responder_resources;
	__u8  initiator_depth;
	__u8  flow_control;
	__u8  retry_count;
	__u8  rnr_retry_count;
	__u8  valid;
};

struct ucma_abi_ud_param {
	__u32 qp_num;
	__u32 qkey;
	struct ibv_kern_ah_attr ah_attr;
	__u8 private_data[RDMA_MAX_PRIVATE_DATA];
	__u8 private_data_len;
	__u8 reserved[7];
	__u8 reserved2[4];  /* Round to 8-byte boundary to support 32/64 */
};

struct ucma_abi_connect {
	__u32 cmd;
	__u16 in;
	__u16 out;
	struct ucma_abi_conn_param conn_param;
	__u32 id;
	__u32 reserved;
};

struct ucma_abi_listen {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u32 id;
	__u32 backlog;
};

struct ucma_abi_accept {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 uid;
	struct ucma_abi_conn_param conn_param;
	__u32 id;
	__u32 reserved;
};

struct ucma_abi_reject {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u32 id;
	__u8  private_data_len;
	__u8  reserved[3];
	__u8  private_data[RDMA_MAX_PRIVATE_DATA];
};

struct ucma_abi_disconnect {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u32 id;
};

struct ucma_abi_init_qp_attr {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 response;
	__u32 id;
	__u32 qp_state;
};

struct ucma_abi_notify {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u32 id;
	__u32 event;
};

struct ucma_abi_join_ip_mcast {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 response;		/* ucma_abi_create_id_resp */
	__u64 uid;
	struct sockaddr_in6 addr;
	__u32 id;
};

struct ucma_abi_join_mcast {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 response;		/* rdma_ucma_create_id_resp */
	__u64 uid;
	__u32 id;
	__u16 addr_size;
	__u16 reserved;
	struct sockaddr_storage addr;
};

struct ucma_abi_get_event {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 response;
};

struct ucma_abi_event_resp {
	__u64 uid;
	__u32 id;
	__u32 event;
	__u32 status;
	union {
		struct ucma_abi_conn_param conn;
		struct ucma_abi_ud_param   ud;
	} param;
};

struct ucma_abi_set_option {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 optval;
	__u32 id;
	__u32 level;
	__u32 optname;
	__u32 optlen;
};

struct ucma_abi_migrate_id {
	__u32 cmd;
	__u16 in;
	__u16 out;
	__u64 response;
	__u32 id;
	__u32 fd;
};

struct ucma_abi_migrate_resp {
	__u32 events_reported;
};

#endif /* RDMA_CMA_ABI_H */
