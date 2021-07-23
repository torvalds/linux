/*
 * Copyright (c) 2014 Intel Corporation. All rights reserved.
 * Copyright (c) 2014 Chelsio, Inc. All rights reserved.
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
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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
#ifndef _IWPM_UTIL_H
#define _IWPM_UTIL_H

#include <linux/module.h>
#include <linux/io.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/jhash.h>
#include <linux/kref.h>
#include <net/netlink.h>
#include <linux/errno.h>
#include <rdma/iw_portmap.h>
#include <rdma/rdma_netlink.h>


#define IWPM_NL_RETRANS		3
#define IWPM_NL_TIMEOUT		(10*HZ)
#define IWPM_MAPINFO_SKB_COUNT	20

#define IWPM_PID_UNDEFINED     -1
#define IWPM_PID_UNAVAILABLE   -2

#define IWPM_REG_UNDEF          0x01
#define IWPM_REG_VALID          0x02
#define IWPM_REG_INCOMPL        0x04

struct iwpm_nlmsg_request {
	struct list_head    inprocess_list;
	__u32               nlmsg_seq;
	void                *req_buffer;
	u8	            nl_client;
	u8                  request_done;
	u16                 err_code;
	struct semaphore    sem;
	struct kref         kref;
};

struct iwpm_mapping_info {
	struct hlist_node hlist_node;
	struct sockaddr_storage local_sockaddr;
	struct sockaddr_storage mapped_sockaddr;
	u8     nl_client;
	u32    map_flags;
};

struct iwpm_remote_info {
	struct hlist_node hlist_node;
	struct sockaddr_storage remote_sockaddr;
	struct sockaddr_storage mapped_loc_sockaddr;
	struct sockaddr_storage mapped_rem_sockaddr;
	u8     nl_client;
};

struct iwpm_admin_data {
	atomic_t nlmsg_seq;
	u32      reg_list[RDMA_NL_NUM_CLIENTS];
};

/**
 * iwpm_get_nlmsg_request - Allocate and initialize netlink message request
 * @nlmsg_seq: Sequence number of the netlink message
 * @nl_client: The index of the netlink client
 * @gfp: Indicates how the memory for the request should be allocated
 *
 * Returns the newly allocated netlink request object if successful,
 * otherwise returns NULL
 */
struct iwpm_nlmsg_request *iwpm_get_nlmsg_request(__u32 nlmsg_seq,
						u8 nl_client, gfp_t gfp);

/**
 * iwpm_free_nlmsg_request - Deallocate netlink message request
 * @kref: Holds reference of netlink message request
 */
void iwpm_free_nlmsg_request(struct kref *kref);

/**
 * iwpm_find_nlmsg_request - Find netlink message request in the request list
 * @echo_seq: Sequence number of the netlink request to find
 *
 * Returns the found netlink message request,
 * if not found, returns NULL
 */
struct iwpm_nlmsg_request *iwpm_find_nlmsg_request(__u32 echo_seq);

/**
 * iwpm_wait_complete_req - Block while servicing the netlink request
 * @nlmsg_request: Netlink message request to service
 *
 * Wakes up, after the request is completed or expired
 * Returns 0 if the request is complete without error
 */
int iwpm_wait_complete_req(struct iwpm_nlmsg_request *nlmsg_request);

/**
 * iwpm_get_nlmsg_seq - Get the sequence number for a netlink
 *			message to send to the port mapper
 *
 * Returns the sequence number for the netlink message.
 */
int iwpm_get_nlmsg_seq(void);

/**
 * iwpm_add_remote_info - Add remote address info of the connecting peer
 *                    to the remote info hash table
 * @reminfo: The remote info to be added
 */
void iwpm_add_remote_info(struct iwpm_remote_info *reminfo);

/**
 * iwpm_check_registration - Check if the client registration
 *			      matches the given one
 * @nl_client: The index of the netlink client
 * @reg: The given registration type to compare with
 *
 * Call iwpm_register_pid() to register a client
 * Returns true if the client registration matches reg,
 * otherwise returns false
 */
u32 iwpm_check_registration(u8 nl_client, u32 reg);

/**
 * iwpm_set_registration - Set the client registration
 * @nl_client: The index of the netlink client
 * @reg: Registration type to set
 */
void iwpm_set_registration(u8 nl_client, u32 reg);

/**
 * iwpm_get_registration - Get the client registration
 * @nl_client: The index of the netlink client
 *
 * Returns the client registration type
 */
u32 iwpm_get_registration(u8 nl_client);

/**
 * iwpm_send_mapinfo - Send local and mapped IPv4/IPv6 address info of
 *                     a client to the user space port mapper
 * @nl_client: The index of the netlink client
 * @iwpm_pid: The pid of the user space port mapper
 *
 * If successful, returns the number of sent mapping info records
 */
int iwpm_send_mapinfo(u8 nl_client, int iwpm_pid);

/**
 * iwpm_mapinfo_available - Check if any mapping info records is available
 *		            in the hash table
 *
 * Returns 1 if mapping information is available, otherwise returns 0
 */
int iwpm_mapinfo_available(void);

/**
 * iwpm_compare_sockaddr - Compare two sockaddr storage structs
 * @a_sockaddr: first sockaddr to compare
 * @b_sockaddr: second sockaddr to compare
 *
 * Return: 0 if they are holding the same ip/tcp address info,
 * otherwise returns 1
 */
int iwpm_compare_sockaddr(struct sockaddr_storage *a_sockaddr,
			struct sockaddr_storage *b_sockaddr);

/**
 * iwpm_validate_nlmsg_attr - Check for NULL netlink attributes
 * @nltb: Holds address of each netlink message attributes
 * @nla_count: Number of netlink message attributes
 *
 * Returns error if any of the nla_count attributes is NULL
 */
static inline int iwpm_validate_nlmsg_attr(struct nlattr *nltb[],
					   int nla_count)
{
	int i;
	for (i = 1; i < nla_count; i++) {
		if (!nltb[i])
			return -EINVAL;
	}
	return 0;
}

/**
 * iwpm_create_nlmsg - Allocate skb and form a netlink message
 * @nl_op: Netlink message opcode
 * @nlh: Holds address of the netlink message header in skb
 * @nl_client: The index of the netlink client
 *
 * Returns the newly allcated skb, or NULL if the tailroom of the skb
 * is insufficient to store the message header and payload
 */
struct sk_buff *iwpm_create_nlmsg(u32 nl_op, struct nlmsghdr **nlh,
					int nl_client);

/**
 * iwpm_parse_nlmsg - Validate and parse the received netlink message
 * @cb: Netlink callback structure
 * @policy_max: Maximum attribute type to be expected
 * @nlmsg_policy: Validation policy
 * @nltb: Array to store policy_max parsed elements
 * @msg_type: Type of netlink message
 *
 * Returns 0 on success or a negative error code
 */
int iwpm_parse_nlmsg(struct netlink_callback *cb, int policy_max,
				const struct nla_policy *nlmsg_policy,
				struct nlattr *nltb[], const char *msg_type);

/**
 * iwpm_print_sockaddr - Print IPv4/IPv6 address and TCP port
 * @sockaddr: Socket address to print
 * @msg: Message to print
 */
void iwpm_print_sockaddr(struct sockaddr_storage *sockaddr, char *msg);

/**
 * iwpm_send_hello - Send hello response to iwpmd
 *
 * @nl_client: The index of the netlink client
 * @iwpm_pid: The pid of the user space port mapper
 * @abi_version: The kernel's abi_version
 *
 * Returns 0 on success or a negative error code
 */
int iwpm_send_hello(u8 nl_client, int iwpm_pid, u16 abi_version);
extern u16 iwpm_ulib_version;
#endif
