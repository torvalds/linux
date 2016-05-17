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

#include "iwpm_util.h"

static const char iwpm_ulib_name[IWPM_ULIBNAME_SIZE] = "iWarpPortMapperUser";
static int iwpm_ulib_version = 3;
static int iwpm_user_pid = IWPM_PID_UNDEFINED;
static atomic_t echo_nlmsg_seq;

int iwpm_valid_pid(void)
{
	return iwpm_user_pid > 0;
}
EXPORT_SYMBOL(iwpm_valid_pid);

/*
 * iwpm_register_pid - Send a netlink query to user space
 *                     for the iwarp port mapper pid
 *
 * nlmsg attributes:
 *	[IWPM_NLA_REG_PID_SEQ]
 *	[IWPM_NLA_REG_IF_NAME]
 *	[IWPM_NLA_REG_IBDEV_NAME]
 *	[IWPM_NLA_REG_ULIB_NAME]
 */
int iwpm_register_pid(struct iwpm_dev_data *pm_msg, u8 nl_client)
{
	struct sk_buff *skb = NULL;
	struct iwpm_nlmsg_request *nlmsg_request = NULL;
	struct nlmsghdr *nlh;
	u32 msg_seq;
	const char *err_str = "";
	int ret = -EINVAL;

	if (!iwpm_valid_client(nl_client)) {
		err_str = "Invalid port mapper client";
		goto pid_query_error;
	}
	if (iwpm_check_registration(nl_client, IWPM_REG_VALID) ||
			iwpm_user_pid == IWPM_PID_UNAVAILABLE)
		return 0;
	skb = iwpm_create_nlmsg(RDMA_NL_IWPM_REG_PID, &nlh, nl_client);
	if (!skb) {
		err_str = "Unable to create a nlmsg";
		goto pid_query_error;
	}
	nlh->nlmsg_seq = iwpm_get_nlmsg_seq();
	nlmsg_request = iwpm_get_nlmsg_request(nlh->nlmsg_seq, nl_client, GFP_KERNEL);
	if (!nlmsg_request) {
		err_str = "Unable to allocate netlink request";
		goto pid_query_error;
	}
	msg_seq = atomic_read(&echo_nlmsg_seq);

	/* fill in the pid request message */
	err_str = "Unable to put attribute of the nlmsg";
	ret = ibnl_put_attr(skb, nlh, sizeof(u32), &msg_seq, IWPM_NLA_REG_PID_SEQ);
	if (ret)
		goto pid_query_error;
	ret = ibnl_put_attr(skb, nlh, IFNAMSIZ,
			    pm_msg->if_name, IWPM_NLA_REG_IF_NAME);
	if (ret)
		goto pid_query_error;
	ret = ibnl_put_attr(skb, nlh, IWPM_DEVNAME_SIZE,
				pm_msg->dev_name, IWPM_NLA_REG_IBDEV_NAME);
	if (ret)
		goto pid_query_error;
	ret = ibnl_put_attr(skb, nlh, IWPM_ULIBNAME_SIZE,
				(char *)iwpm_ulib_name, IWPM_NLA_REG_ULIB_NAME);
	if (ret)
		goto pid_query_error;

	pr_debug("%s: Multicasting a nlmsg (dev = %s ifname = %s iwpm = %s)\n",
		__func__, pm_msg->dev_name, pm_msg->if_name, iwpm_ulib_name);

	ret = ibnl_multicast(skb, nlh, RDMA_NL_GROUP_IWPM, GFP_KERNEL);
	if (ret) {
		skb = NULL; /* skb is freed in the netlink send-op handling */
		iwpm_user_pid = IWPM_PID_UNAVAILABLE;
		err_str = "Unable to send a nlmsg";
		goto pid_query_error;
	}
	nlmsg_request->req_buffer = pm_msg;
	ret = iwpm_wait_complete_req(nlmsg_request);
	return ret;
pid_query_error:
	pr_info("%s: %s (client = %d)\n", __func__, err_str, nl_client);
	if (skb)
		dev_kfree_skb(skb);
	if (nlmsg_request)
		iwpm_free_nlmsg_request(&nlmsg_request->kref);
	return ret;
}
EXPORT_SYMBOL(iwpm_register_pid);

/*
 * iwpm_add_mapping - Send a netlink add mapping message
 *                    to the port mapper
 * nlmsg attributes:
 *	[IWPM_NLA_MANAGE_MAPPING_SEQ]
 *	[IWPM_NLA_MANAGE_ADDR]
 */
int iwpm_add_mapping(struct iwpm_sa_data *pm_msg, u8 nl_client)
{
	struct sk_buff *skb = NULL;
	struct iwpm_nlmsg_request *nlmsg_request = NULL;
	struct nlmsghdr *nlh;
	u32 msg_seq;
	const char *err_str = "";
	int ret = -EINVAL;

	if (!iwpm_valid_client(nl_client)) {
		err_str = "Invalid port mapper client";
		goto add_mapping_error;
	}
	if (!iwpm_valid_pid())
		return 0;
	if (!iwpm_check_registration(nl_client, IWPM_REG_VALID)) {
		err_str = "Unregistered port mapper client";
		goto add_mapping_error;
	}
	skb = iwpm_create_nlmsg(RDMA_NL_IWPM_ADD_MAPPING, &nlh, nl_client);
	if (!skb) {
		err_str = "Unable to create a nlmsg";
		goto add_mapping_error;
	}
	nlh->nlmsg_seq = iwpm_get_nlmsg_seq();
	nlmsg_request = iwpm_get_nlmsg_request(nlh->nlmsg_seq, nl_client, GFP_KERNEL);
	if (!nlmsg_request) {
		err_str = "Unable to allocate netlink request";
		goto add_mapping_error;
	}
	msg_seq = atomic_read(&echo_nlmsg_seq);
	/* fill in the add mapping message */
	err_str = "Unable to put attribute of the nlmsg";
	ret = ibnl_put_attr(skb, nlh, sizeof(u32), &msg_seq,
				IWPM_NLA_MANAGE_MAPPING_SEQ);
	if (ret)
		goto add_mapping_error;
	ret = ibnl_put_attr(skb, nlh, sizeof(struct sockaddr_storage),
				&pm_msg->loc_addr, IWPM_NLA_MANAGE_ADDR);
	if (ret)
		goto add_mapping_error;
	nlmsg_request->req_buffer = pm_msg;

	ret = ibnl_unicast(skb, nlh, iwpm_user_pid);
	if (ret) {
		skb = NULL; /* skb is freed in the netlink send-op handling */
		iwpm_user_pid = IWPM_PID_UNDEFINED;
		err_str = "Unable to send a nlmsg";
		goto add_mapping_error;
	}
	ret = iwpm_wait_complete_req(nlmsg_request);
	return ret;
add_mapping_error:
	pr_info("%s: %s (client = %d)\n", __func__, err_str, nl_client);
	if (skb)
		dev_kfree_skb(skb);
	if (nlmsg_request)
		iwpm_free_nlmsg_request(&nlmsg_request->kref);
	return ret;
}
EXPORT_SYMBOL(iwpm_add_mapping);

/*
 * iwpm_add_and_query_mapping - Send a netlink add and query
 *                              mapping message to the port mapper
 * nlmsg attributes:
 *	[IWPM_NLA_QUERY_MAPPING_SEQ]
 *	[IWPM_NLA_QUERY_LOCAL_ADDR]
 *	[IWPM_NLA_QUERY_REMOTE_ADDR]
 */
int iwpm_add_and_query_mapping(struct iwpm_sa_data *pm_msg, u8 nl_client)
{
	struct sk_buff *skb = NULL;
	struct iwpm_nlmsg_request *nlmsg_request = NULL;
	struct nlmsghdr *nlh;
	u32 msg_seq;
	const char *err_str = "";
	int ret = -EINVAL;

	if (!iwpm_valid_client(nl_client)) {
		err_str = "Invalid port mapper client";
		goto query_mapping_error;
	}
	if (!iwpm_valid_pid())
		return 0;
	if (!iwpm_check_registration(nl_client, IWPM_REG_VALID)) {
		err_str = "Unregistered port mapper client";
		goto query_mapping_error;
	}
	ret = -ENOMEM;
	skb = iwpm_create_nlmsg(RDMA_NL_IWPM_QUERY_MAPPING, &nlh, nl_client);
	if (!skb) {
		err_str = "Unable to create a nlmsg";
		goto query_mapping_error;
	}
	nlh->nlmsg_seq = iwpm_get_nlmsg_seq();
	nlmsg_request = iwpm_get_nlmsg_request(nlh->nlmsg_seq,
				nl_client, GFP_KERNEL);
	if (!nlmsg_request) {
		err_str = "Unable to allocate netlink request";
		goto query_mapping_error;
	}
	msg_seq = atomic_read(&echo_nlmsg_seq);

	/* fill in the query message */
	err_str = "Unable to put attribute of the nlmsg";
	ret = ibnl_put_attr(skb, nlh, sizeof(u32), &msg_seq,
				IWPM_NLA_QUERY_MAPPING_SEQ);
	if (ret)
		goto query_mapping_error;
	ret = ibnl_put_attr(skb, nlh, sizeof(struct sockaddr_storage),
				&pm_msg->loc_addr, IWPM_NLA_QUERY_LOCAL_ADDR);
	if (ret)
		goto query_mapping_error;
	ret = ibnl_put_attr(skb, nlh, sizeof(struct sockaddr_storage),
				&pm_msg->rem_addr, IWPM_NLA_QUERY_REMOTE_ADDR);
	if (ret)
		goto query_mapping_error;
	nlmsg_request->req_buffer = pm_msg;

	ret = ibnl_unicast(skb, nlh, iwpm_user_pid);
	if (ret) {
		skb = NULL; /* skb is freed in the netlink send-op handling */
		err_str = "Unable to send a nlmsg";
		goto query_mapping_error;
	}
	ret = iwpm_wait_complete_req(nlmsg_request);
	return ret;
query_mapping_error:
	pr_info("%s: %s (client = %d)\n", __func__, err_str, nl_client);
	if (skb)
		dev_kfree_skb(skb);
	if (nlmsg_request)
		iwpm_free_nlmsg_request(&nlmsg_request->kref);
	return ret;
}
EXPORT_SYMBOL(iwpm_add_and_query_mapping);

/*
 * iwpm_remove_mapping - Send a netlink remove mapping message
 *                       to the port mapper
 * nlmsg attributes:
 *	[IWPM_NLA_MANAGE_MAPPING_SEQ]
 *	[IWPM_NLA_MANAGE_ADDR]
 */
int iwpm_remove_mapping(struct sockaddr_storage *local_addr, u8 nl_client)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	u32 msg_seq;
	const char *err_str = "";
	int ret = -EINVAL;

	if (!iwpm_valid_client(nl_client)) {
		err_str = "Invalid port mapper client";
		goto remove_mapping_error;
	}
	if (!iwpm_valid_pid())
		return 0;
	if (iwpm_check_registration(nl_client, IWPM_REG_UNDEF)) {
		err_str = "Unregistered port mapper client";
		goto remove_mapping_error;
	}
	skb = iwpm_create_nlmsg(RDMA_NL_IWPM_REMOVE_MAPPING, &nlh, nl_client);
	if (!skb) {
		ret = -ENOMEM;
		err_str = "Unable to create a nlmsg";
		goto remove_mapping_error;
	}
	msg_seq = atomic_read(&echo_nlmsg_seq);
	nlh->nlmsg_seq = iwpm_get_nlmsg_seq();
	err_str = "Unable to put attribute of the nlmsg";
	ret = ibnl_put_attr(skb, nlh, sizeof(u32), &msg_seq,
				IWPM_NLA_MANAGE_MAPPING_SEQ);
	if (ret)
		goto remove_mapping_error;
	ret = ibnl_put_attr(skb, nlh, sizeof(struct sockaddr_storage),
				local_addr, IWPM_NLA_MANAGE_ADDR);
	if (ret)
		goto remove_mapping_error;

	ret = ibnl_unicast(skb, nlh, iwpm_user_pid);
	if (ret) {
		skb = NULL; /* skb is freed in the netlink send-op handling */
		iwpm_user_pid = IWPM_PID_UNDEFINED;
		err_str = "Unable to send a nlmsg";
		goto remove_mapping_error;
	}
	iwpm_print_sockaddr(local_addr,
			"remove_mapping: Local sockaddr:");
	return 0;
remove_mapping_error:
	pr_info("%s: %s (client = %d)\n", __func__, err_str, nl_client);
	if (skb)
		dev_kfree_skb_any(skb);
	return ret;
}
EXPORT_SYMBOL(iwpm_remove_mapping);

/* netlink attribute policy for the received response to register pid request */
static const struct nla_policy resp_reg_policy[IWPM_NLA_RREG_PID_MAX] = {
	[IWPM_NLA_RREG_PID_SEQ]     = { .type = NLA_U32 },
	[IWPM_NLA_RREG_IBDEV_NAME]  = { .type = NLA_STRING,
					.len = IWPM_DEVNAME_SIZE - 1 },
	[IWPM_NLA_RREG_ULIB_NAME]   = { .type = NLA_STRING,
					.len = IWPM_ULIBNAME_SIZE - 1 },
	[IWPM_NLA_RREG_ULIB_VER]    = { .type = NLA_U16 },
	[IWPM_NLA_RREG_PID_ERR]     = { .type = NLA_U16 }
};

/*
 * iwpm_register_pid_cb - Process a port mapper response to
 *                        iwpm_register_pid()
 */
int iwpm_register_pid_cb(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct iwpm_nlmsg_request *nlmsg_request = NULL;
	struct nlattr *nltb[IWPM_NLA_RREG_PID_MAX];
	struct iwpm_dev_data *pm_msg;
	char *dev_name, *iwpm_name;
	u32 msg_seq;
	u8 nl_client;
	u16 iwpm_version;
	const char *msg_type = "Register Pid response";

	if (iwpm_parse_nlmsg(cb, IWPM_NLA_RREG_PID_MAX,
				resp_reg_policy, nltb, msg_type))
		return -EINVAL;

	msg_seq = nla_get_u32(nltb[IWPM_NLA_RREG_PID_SEQ]);
	nlmsg_request = iwpm_find_nlmsg_request(msg_seq);
	if (!nlmsg_request) {
		pr_info("%s: Could not find a matching request (seq = %u)\n",
				 __func__, msg_seq);
		return -EINVAL;
	}
	pm_msg = nlmsg_request->req_buffer;
	nl_client = nlmsg_request->nl_client;
	dev_name = (char *)nla_data(nltb[IWPM_NLA_RREG_IBDEV_NAME]);
	iwpm_name = (char *)nla_data(nltb[IWPM_NLA_RREG_ULIB_NAME]);
	iwpm_version = nla_get_u16(nltb[IWPM_NLA_RREG_ULIB_VER]);

	/* check device name, ulib name and version */
	if (strcmp(pm_msg->dev_name, dev_name) ||
			strcmp(iwpm_ulib_name, iwpm_name) ||
			iwpm_version != iwpm_ulib_version) {

		pr_info("%s: Incorrect info (dev = %s name = %s version = %d)\n",
				__func__, dev_name, iwpm_name, iwpm_version);
		nlmsg_request->err_code = IWPM_USER_LIB_INFO_ERR;
		goto register_pid_response_exit;
	}
	iwpm_user_pid = cb->nlh->nlmsg_pid;
	atomic_set(&echo_nlmsg_seq, cb->nlh->nlmsg_seq);
	pr_debug("%s: iWarp Port Mapper (pid = %d) is available!\n",
			__func__, iwpm_user_pid);
	if (iwpm_valid_client(nl_client))
		iwpm_set_registration(nl_client, IWPM_REG_VALID);
register_pid_response_exit:
	nlmsg_request->request_done = 1;
	/* always for found nlmsg_request */
	kref_put(&nlmsg_request->kref, iwpm_free_nlmsg_request);
	barrier();
	up(&nlmsg_request->sem);
	return 0;
}
EXPORT_SYMBOL(iwpm_register_pid_cb);

/* netlink attribute policy for the received response to add mapping request */
static const struct nla_policy resp_add_policy[IWPM_NLA_RMANAGE_MAPPING_MAX] = {
	[IWPM_NLA_MANAGE_MAPPING_SEQ]     = { .type = NLA_U32 },
	[IWPM_NLA_MANAGE_ADDR]            = { .len = sizeof(struct sockaddr_storage) },
	[IWPM_NLA_MANAGE_MAPPED_LOC_ADDR] = { .len = sizeof(struct sockaddr_storage) },
	[IWPM_NLA_RMANAGE_MAPPING_ERR]	  = { .type = NLA_U16 }
};

/*
 * iwpm_add_mapping_cb - Process a port mapper response to
 *                       iwpm_add_mapping()
 */
int iwpm_add_mapping_cb(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct iwpm_sa_data *pm_msg;
	struct iwpm_nlmsg_request *nlmsg_request = NULL;
	struct nlattr *nltb[IWPM_NLA_RMANAGE_MAPPING_MAX];
	struct sockaddr_storage *local_sockaddr;
	struct sockaddr_storage *mapped_sockaddr;
	const char *msg_type;
	u32 msg_seq;

	msg_type = "Add Mapping response";
	if (iwpm_parse_nlmsg(cb, IWPM_NLA_RMANAGE_MAPPING_MAX,
				resp_add_policy, nltb, msg_type))
		return -EINVAL;

	atomic_set(&echo_nlmsg_seq, cb->nlh->nlmsg_seq);

	msg_seq = nla_get_u32(nltb[IWPM_NLA_MANAGE_MAPPING_SEQ]);
	nlmsg_request = iwpm_find_nlmsg_request(msg_seq);
	if (!nlmsg_request) {
		pr_info("%s: Could not find a matching request (seq = %u)\n",
				 __func__, msg_seq);
		return -EINVAL;
	}
	pm_msg = nlmsg_request->req_buffer;
	local_sockaddr = (struct sockaddr_storage *)
			nla_data(nltb[IWPM_NLA_MANAGE_ADDR]);
	mapped_sockaddr = (struct sockaddr_storage *)
			nla_data(nltb[IWPM_NLA_MANAGE_MAPPED_LOC_ADDR]);

	if (iwpm_compare_sockaddr(local_sockaddr, &pm_msg->loc_addr)) {
		nlmsg_request->err_code = IWPM_USER_LIB_INFO_ERR;
		goto add_mapping_response_exit;
	}
	if (mapped_sockaddr->ss_family != local_sockaddr->ss_family) {
		pr_info("%s: Sockaddr family doesn't match the requested one\n",
				__func__);
		nlmsg_request->err_code = IWPM_USER_LIB_INFO_ERR;
		goto add_mapping_response_exit;
	}
	memcpy(&pm_msg->mapped_loc_addr, mapped_sockaddr,
			sizeof(*mapped_sockaddr));
	iwpm_print_sockaddr(&pm_msg->loc_addr,
			"add_mapping: Local sockaddr:");
	iwpm_print_sockaddr(&pm_msg->mapped_loc_addr,
			"add_mapping: Mapped local sockaddr:");

add_mapping_response_exit:
	nlmsg_request->request_done = 1;
	/* always for found request */
	kref_put(&nlmsg_request->kref, iwpm_free_nlmsg_request);
	barrier();
	up(&nlmsg_request->sem);
	return 0;
}
EXPORT_SYMBOL(iwpm_add_mapping_cb);

/* netlink attribute policy for the response to add and query mapping request
 * and response with remote address info */
static const struct nla_policy resp_query_policy[IWPM_NLA_RQUERY_MAPPING_MAX] = {
	[IWPM_NLA_QUERY_MAPPING_SEQ]      = { .type = NLA_U32 },
	[IWPM_NLA_QUERY_LOCAL_ADDR]       = { .len = sizeof(struct sockaddr_storage) },
	[IWPM_NLA_QUERY_REMOTE_ADDR]      = { .len = sizeof(struct sockaddr_storage) },
	[IWPM_NLA_RQUERY_MAPPED_LOC_ADDR] = { .len = sizeof(struct sockaddr_storage) },
	[IWPM_NLA_RQUERY_MAPPED_REM_ADDR] = { .len = sizeof(struct sockaddr_storage) },
	[IWPM_NLA_RQUERY_MAPPING_ERR]	  = { .type = NLA_U16 }
};

/*
 * iwpm_add_and_query_mapping_cb - Process a port mapper response to
 *                                 iwpm_add_and_query_mapping()
 */
int iwpm_add_and_query_mapping_cb(struct sk_buff *skb,
				struct netlink_callback *cb)
{
	struct iwpm_sa_data *pm_msg;
	struct iwpm_nlmsg_request *nlmsg_request = NULL;
	struct nlattr *nltb[IWPM_NLA_RQUERY_MAPPING_MAX];
	struct sockaddr_storage *local_sockaddr, *remote_sockaddr;
	struct sockaddr_storage *mapped_loc_sockaddr, *mapped_rem_sockaddr;
	const char *msg_type;
	u32 msg_seq;
	u16 err_code;

	msg_type = "Query Mapping response";
	if (iwpm_parse_nlmsg(cb, IWPM_NLA_RQUERY_MAPPING_MAX,
				resp_query_policy, nltb, msg_type))
		return -EINVAL;
	atomic_set(&echo_nlmsg_seq, cb->nlh->nlmsg_seq);

	msg_seq = nla_get_u32(nltb[IWPM_NLA_QUERY_MAPPING_SEQ]);
	nlmsg_request = iwpm_find_nlmsg_request(msg_seq);
	if (!nlmsg_request) {
		pr_info("%s: Could not find a matching request (seq = %u)\n",
				 __func__, msg_seq);
			return -EINVAL;
	}
	pm_msg = nlmsg_request->req_buffer;
	local_sockaddr = (struct sockaddr_storage *)
			nla_data(nltb[IWPM_NLA_QUERY_LOCAL_ADDR]);
	remote_sockaddr = (struct sockaddr_storage *)
			nla_data(nltb[IWPM_NLA_QUERY_REMOTE_ADDR]);
	mapped_loc_sockaddr = (struct sockaddr_storage *)
			nla_data(nltb[IWPM_NLA_RQUERY_MAPPED_LOC_ADDR]);
	mapped_rem_sockaddr = (struct sockaddr_storage *)
			nla_data(nltb[IWPM_NLA_RQUERY_MAPPED_REM_ADDR]);

	err_code = nla_get_u16(nltb[IWPM_NLA_RQUERY_MAPPING_ERR]);
	if (err_code == IWPM_REMOTE_QUERY_REJECT) {
		pr_info("%s: Received a Reject (pid = %u, echo seq = %u)\n",
			__func__, cb->nlh->nlmsg_pid, msg_seq);
		nlmsg_request->err_code = IWPM_REMOTE_QUERY_REJECT;
	}
	if (iwpm_compare_sockaddr(local_sockaddr, &pm_msg->loc_addr) ||
		iwpm_compare_sockaddr(remote_sockaddr, &pm_msg->rem_addr)) {
		pr_info("%s: Incorrect local sockaddr\n", __func__);
		nlmsg_request->err_code = IWPM_USER_LIB_INFO_ERR;
		goto query_mapping_response_exit;
	}
	if (mapped_loc_sockaddr->ss_family != local_sockaddr->ss_family ||
		mapped_rem_sockaddr->ss_family != remote_sockaddr->ss_family) {
		pr_info("%s: Sockaddr family doesn't match the requested one\n",
				__func__);
		nlmsg_request->err_code = IWPM_USER_LIB_INFO_ERR;
		goto query_mapping_response_exit;
	}
	memcpy(&pm_msg->mapped_loc_addr, mapped_loc_sockaddr,
			sizeof(*mapped_loc_sockaddr));
	memcpy(&pm_msg->mapped_rem_addr, mapped_rem_sockaddr,
			sizeof(*mapped_rem_sockaddr));

	iwpm_print_sockaddr(&pm_msg->loc_addr,
			"query_mapping: Local sockaddr:");
	iwpm_print_sockaddr(&pm_msg->mapped_loc_addr,
			"query_mapping: Mapped local sockaddr:");
	iwpm_print_sockaddr(&pm_msg->rem_addr,
			"query_mapping: Remote sockaddr:");
	iwpm_print_sockaddr(&pm_msg->mapped_rem_addr,
			"query_mapping: Mapped remote sockaddr:");
query_mapping_response_exit:
	nlmsg_request->request_done = 1;
	/* always for found request */
	kref_put(&nlmsg_request->kref, iwpm_free_nlmsg_request);
	barrier();
	up(&nlmsg_request->sem);
	return 0;
}
EXPORT_SYMBOL(iwpm_add_and_query_mapping_cb);

/*
 * iwpm_remote_info_cb - Process a port mapper message, containing
 *			  the remote connecting peer address info
 */
int iwpm_remote_info_cb(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nlattr *nltb[IWPM_NLA_RQUERY_MAPPING_MAX];
	struct sockaddr_storage *local_sockaddr, *remote_sockaddr;
	struct sockaddr_storage *mapped_loc_sockaddr, *mapped_rem_sockaddr;
	struct iwpm_remote_info *rem_info;
	const char *msg_type;
	u8 nl_client;
	int ret = -EINVAL;

	msg_type = "Remote Mapping info";
	if (iwpm_parse_nlmsg(cb, IWPM_NLA_RQUERY_MAPPING_MAX,
				resp_query_policy, nltb, msg_type))
		return ret;

	nl_client = RDMA_NL_GET_CLIENT(cb->nlh->nlmsg_type);
	if (!iwpm_valid_client(nl_client)) {
		pr_info("%s: Invalid port mapper client = %d\n",
				__func__, nl_client);
		return ret;
	}
	atomic_set(&echo_nlmsg_seq, cb->nlh->nlmsg_seq);

	local_sockaddr = (struct sockaddr_storage *)
			nla_data(nltb[IWPM_NLA_QUERY_LOCAL_ADDR]);
	remote_sockaddr = (struct sockaddr_storage *)
			nla_data(nltb[IWPM_NLA_QUERY_REMOTE_ADDR]);
	mapped_loc_sockaddr = (struct sockaddr_storage *)
			nla_data(nltb[IWPM_NLA_RQUERY_MAPPED_LOC_ADDR]);
	mapped_rem_sockaddr = (struct sockaddr_storage *)
			nla_data(nltb[IWPM_NLA_RQUERY_MAPPED_REM_ADDR]);

	if (mapped_loc_sockaddr->ss_family != local_sockaddr->ss_family ||
		mapped_rem_sockaddr->ss_family != remote_sockaddr->ss_family) {
		pr_info("%s: Sockaddr family doesn't match the requested one\n",
				__func__);
		return ret;
	}
	rem_info = kzalloc(sizeof(struct iwpm_remote_info), GFP_ATOMIC);
	if (!rem_info) {
		pr_err("%s: Unable to allocate a remote info\n", __func__);
		ret = -ENOMEM;
		return ret;
	}
	memcpy(&rem_info->mapped_loc_sockaddr, mapped_loc_sockaddr,
	       sizeof(struct sockaddr_storage));
	memcpy(&rem_info->remote_sockaddr, remote_sockaddr,
	       sizeof(struct sockaddr_storage));
	memcpy(&rem_info->mapped_rem_sockaddr, mapped_rem_sockaddr,
	       sizeof(struct sockaddr_storage));
	rem_info->nl_client = nl_client;

	iwpm_add_remote_info(rem_info);

	iwpm_print_sockaddr(local_sockaddr,
			"remote_info: Local sockaddr:");
	iwpm_print_sockaddr(mapped_loc_sockaddr,
			"remote_info: Mapped local sockaddr:");
	iwpm_print_sockaddr(remote_sockaddr,
			"remote_info: Remote sockaddr:");
	iwpm_print_sockaddr(mapped_rem_sockaddr,
			"remote_info: Mapped remote sockaddr:");
	return ret;
}
EXPORT_SYMBOL(iwpm_remote_info_cb);

/* netlink attribute policy for the received request for mapping info */
static const struct nla_policy resp_mapinfo_policy[IWPM_NLA_MAPINFO_REQ_MAX] = {
	[IWPM_NLA_MAPINFO_ULIB_NAME] = { .type = NLA_STRING,
					.len = IWPM_ULIBNAME_SIZE - 1 },
	[IWPM_NLA_MAPINFO_ULIB_VER]  = { .type = NLA_U16 }
};

/*
 * iwpm_mapping_info_cb - Process a port mapper request for mapping info
 */
int iwpm_mapping_info_cb(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nlattr *nltb[IWPM_NLA_MAPINFO_REQ_MAX];
	const char *msg_type = "Mapping Info response";
	u8 nl_client;
	char *iwpm_name;
	u16 iwpm_version;
	int ret = -EINVAL;

	if (iwpm_parse_nlmsg(cb, IWPM_NLA_MAPINFO_REQ_MAX,
				resp_mapinfo_policy, nltb, msg_type)) {
		pr_info("%s: Unable to parse nlmsg\n", __func__);
		return ret;
	}
	iwpm_name = (char *)nla_data(nltb[IWPM_NLA_MAPINFO_ULIB_NAME]);
	iwpm_version = nla_get_u16(nltb[IWPM_NLA_MAPINFO_ULIB_VER]);
	if (strcmp(iwpm_ulib_name, iwpm_name) ||
			iwpm_version != iwpm_ulib_version) {
		pr_info("%s: Invalid port mapper name = %s version = %d\n",
				__func__, iwpm_name, iwpm_version);
		return ret;
	}
	nl_client = RDMA_NL_GET_CLIENT(cb->nlh->nlmsg_type);
	if (!iwpm_valid_client(nl_client)) {
		pr_info("%s: Invalid port mapper client = %d\n",
				__func__, nl_client);
		return ret;
	}
	iwpm_set_registration(nl_client, IWPM_REG_INCOMPL);
	atomic_set(&echo_nlmsg_seq, cb->nlh->nlmsg_seq);
	iwpm_user_pid = cb->nlh->nlmsg_pid;
	if (!iwpm_mapinfo_available())
		return 0;
	pr_debug("%s: iWarp Port Mapper (pid = %d) is available!\n",
		 __func__, iwpm_user_pid);
	ret = iwpm_send_mapinfo(nl_client, iwpm_user_pid);
	return ret;
}
EXPORT_SYMBOL(iwpm_mapping_info_cb);

/* netlink attribute policy for the received mapping info ack */
static const struct nla_policy ack_mapinfo_policy[IWPM_NLA_MAPINFO_NUM_MAX] = {
	[IWPM_NLA_MAPINFO_SEQ]    =   { .type = NLA_U32 },
	[IWPM_NLA_MAPINFO_SEND_NUM] = { .type = NLA_U32 },
	[IWPM_NLA_MAPINFO_ACK_NUM] =  { .type = NLA_U32 }
};

/*
 * iwpm_ack_mapping_info_cb - Process a port mapper ack for
 *                            the provided mapping info records
 */
int iwpm_ack_mapping_info_cb(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct nlattr *nltb[IWPM_NLA_MAPINFO_NUM_MAX];
	u32 mapinfo_send, mapinfo_ack;
	const char *msg_type = "Mapping Info Ack";

	if (iwpm_parse_nlmsg(cb, IWPM_NLA_MAPINFO_NUM_MAX,
				ack_mapinfo_policy, nltb, msg_type))
		return -EINVAL;
	mapinfo_send = nla_get_u32(nltb[IWPM_NLA_MAPINFO_SEND_NUM]);
	mapinfo_ack = nla_get_u32(nltb[IWPM_NLA_MAPINFO_ACK_NUM]);
	if (mapinfo_ack != mapinfo_send)
		pr_info("%s: Invalid mapinfo number (sent = %u ack-ed = %u)\n",
			__func__, mapinfo_send, mapinfo_ack);
	atomic_set(&echo_nlmsg_seq, cb->nlh->nlmsg_seq);
	return 0;
}
EXPORT_SYMBOL(iwpm_ack_mapping_info_cb);

/* netlink attribute policy for the received port mapper error message */
static const struct nla_policy map_error_policy[IWPM_NLA_ERR_MAX] = {
	[IWPM_NLA_ERR_SEQ]        = { .type = NLA_U32 },
	[IWPM_NLA_ERR_CODE]       = { .type = NLA_U16 },
};

/*
 * iwpm_mapping_error_cb - Process a port mapper error message
 */
int iwpm_mapping_error_cb(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct iwpm_nlmsg_request *nlmsg_request = NULL;
	int nl_client = RDMA_NL_GET_CLIENT(cb->nlh->nlmsg_type);
	struct nlattr *nltb[IWPM_NLA_ERR_MAX];
	u32 msg_seq;
	u16 err_code;
	const char *msg_type = "Mapping Error Msg";

	if (iwpm_parse_nlmsg(cb, IWPM_NLA_ERR_MAX,
				map_error_policy, nltb, msg_type))
		return -EINVAL;

	msg_seq = nla_get_u32(nltb[IWPM_NLA_ERR_SEQ]);
	err_code = nla_get_u16(nltb[IWPM_NLA_ERR_CODE]);
	pr_info("%s: Received msg seq = %u err code = %u client = %d\n",
				__func__, msg_seq, err_code, nl_client);
	/* look for nlmsg_request */
	nlmsg_request = iwpm_find_nlmsg_request(msg_seq);
	if (!nlmsg_request) {
		/* not all errors have associated requests */
		pr_debug("Could not find matching req (seq = %u)\n", msg_seq);
		return 0;
	}
	atomic_set(&echo_nlmsg_seq, cb->nlh->nlmsg_seq);
	nlmsg_request->err_code = err_code;
	nlmsg_request->request_done = 1;
	/* always for found request */
	kref_put(&nlmsg_request->kref, iwpm_free_nlmsg_request);
	barrier();
	up(&nlmsg_request->sem);
	return 0;
}
EXPORT_SYMBOL(iwpm_mapping_error_cb);
