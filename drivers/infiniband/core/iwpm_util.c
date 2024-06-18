/*
 * Copyright (c) 2014 Chelsio, Inc. All rights reserved.
 * Copyright (c) 2014 Intel Corporation. All rights reserved.
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

#include "iwpm_util.h"

#define IWPM_MAPINFO_HASH_SIZE	512
#define IWPM_MAPINFO_HASH_MASK	(IWPM_MAPINFO_HASH_SIZE - 1)
#define IWPM_REMINFO_HASH_SIZE	64
#define IWPM_REMINFO_HASH_MASK	(IWPM_REMINFO_HASH_SIZE - 1)
#define IWPM_MSG_SIZE		512

static LIST_HEAD(iwpm_nlmsg_req_list);
static DEFINE_SPINLOCK(iwpm_nlmsg_req_lock);

static struct hlist_head *iwpm_hash_bucket;
static DEFINE_SPINLOCK(iwpm_mapinfo_lock);

static struct hlist_head *iwpm_reminfo_bucket;
static DEFINE_SPINLOCK(iwpm_reminfo_lock);

static struct iwpm_admin_data iwpm_admin;

/**
 * iwpm_init - Allocate resources for the iwarp port mapper
 * @nl_client: The index of the netlink client
 *
 * Should be called when network interface goes up.
 */
int iwpm_init(u8 nl_client)
{
	iwpm_hash_bucket = kcalloc(IWPM_MAPINFO_HASH_SIZE,
				   sizeof(struct hlist_head), GFP_KERNEL);
	if (!iwpm_hash_bucket)
		return -ENOMEM;

	iwpm_reminfo_bucket = kcalloc(IWPM_REMINFO_HASH_SIZE,
				      sizeof(struct hlist_head), GFP_KERNEL);
	if (!iwpm_reminfo_bucket) {
		kfree(iwpm_hash_bucket);
		return -ENOMEM;
	}

	iwpm_set_registration(nl_client, IWPM_REG_UNDEF);
	pr_debug("%s: Mapinfo and reminfo tables are created\n", __func__);
	return 0;
}

static void free_hash_bucket(void);
static void free_reminfo_bucket(void);

/**
 * iwpm_exit - Deallocate resources for the iwarp port mapper
 * @nl_client: The index of the netlink client
 *
 * Should be called when network interface goes down.
 */
int iwpm_exit(u8 nl_client)
{
	free_hash_bucket();
	free_reminfo_bucket();
	pr_debug("%s: Resources are destroyed\n", __func__);
	iwpm_set_registration(nl_client, IWPM_REG_UNDEF);
	return 0;
}

static struct hlist_head *get_mapinfo_hash_bucket(struct sockaddr_storage *,
					       struct sockaddr_storage *);

/**
 * iwpm_create_mapinfo - Store local and mapped IPv4/IPv6 address
 *                       info in a hash table
 * @local_sockaddr: Local ip/tcp address
 * @mapped_sockaddr: Mapped local ip/tcp address
 * @nl_client: The index of the netlink client
 * @map_flags: IWPM mapping flags
 */
int iwpm_create_mapinfo(struct sockaddr_storage *local_sockaddr,
			struct sockaddr_storage *mapped_sockaddr,
			u8 nl_client, u32 map_flags)
{
	struct hlist_head *hash_bucket_head = NULL;
	struct iwpm_mapping_info *map_info;
	unsigned long flags;
	int ret = -EINVAL;

	map_info = kzalloc(sizeof(struct iwpm_mapping_info), GFP_KERNEL);
	if (!map_info)
		return -ENOMEM;

	memcpy(&map_info->local_sockaddr, local_sockaddr,
	       sizeof(struct sockaddr_storage));
	memcpy(&map_info->mapped_sockaddr, mapped_sockaddr,
	       sizeof(struct sockaddr_storage));
	map_info->nl_client = nl_client;
	map_info->map_flags = map_flags;

	spin_lock_irqsave(&iwpm_mapinfo_lock, flags);
	if (iwpm_hash_bucket) {
		hash_bucket_head = get_mapinfo_hash_bucket(
					&map_info->local_sockaddr,
					&map_info->mapped_sockaddr);
		if (hash_bucket_head) {
			hlist_add_head(&map_info->hlist_node, hash_bucket_head);
			ret = 0;
		}
	}
	spin_unlock_irqrestore(&iwpm_mapinfo_lock, flags);

	if (!hash_bucket_head)
		kfree(map_info);
	return ret;
}

/**
 * iwpm_remove_mapinfo - Remove local and mapped IPv4/IPv6 address
 *                       info from the hash table
 * @local_sockaddr: Local ip/tcp address
 * @mapped_local_addr: Mapped local ip/tcp address
 *
 * Returns err code if mapping info is not found in the hash table,
 * otherwise returns 0
 */
int iwpm_remove_mapinfo(struct sockaddr_storage *local_sockaddr,
			struct sockaddr_storage *mapped_local_addr)
{
	struct hlist_node *tmp_hlist_node;
	struct hlist_head *hash_bucket_head;
	struct iwpm_mapping_info *map_info = NULL;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&iwpm_mapinfo_lock, flags);
	if (iwpm_hash_bucket) {
		hash_bucket_head = get_mapinfo_hash_bucket(
					local_sockaddr,
					mapped_local_addr);
		if (!hash_bucket_head)
			goto remove_mapinfo_exit;

		hlist_for_each_entry_safe(map_info, tmp_hlist_node,
					hash_bucket_head, hlist_node) {

			if (!iwpm_compare_sockaddr(&map_info->mapped_sockaddr,
						mapped_local_addr)) {

				hlist_del_init(&map_info->hlist_node);
				kfree(map_info);
				ret = 0;
				break;
			}
		}
	}
remove_mapinfo_exit:
	spin_unlock_irqrestore(&iwpm_mapinfo_lock, flags);
	return ret;
}

static void free_hash_bucket(void)
{
	struct hlist_node *tmp_hlist_node;
	struct iwpm_mapping_info *map_info;
	unsigned long flags;
	int i;

	/* remove all the mapinfo data from the list */
	spin_lock_irqsave(&iwpm_mapinfo_lock, flags);
	for (i = 0; i < IWPM_MAPINFO_HASH_SIZE; i++) {
		hlist_for_each_entry_safe(map_info, tmp_hlist_node,
			&iwpm_hash_bucket[i], hlist_node) {

				hlist_del_init(&map_info->hlist_node);
				kfree(map_info);
			}
	}
	/* free the hash list */
	kfree(iwpm_hash_bucket);
	iwpm_hash_bucket = NULL;
	spin_unlock_irqrestore(&iwpm_mapinfo_lock, flags);
}

static void free_reminfo_bucket(void)
{
	struct hlist_node *tmp_hlist_node;
	struct iwpm_remote_info *rem_info;
	unsigned long flags;
	int i;

	/* remove all the remote info from the list */
	spin_lock_irqsave(&iwpm_reminfo_lock, flags);
	for (i = 0; i < IWPM_REMINFO_HASH_SIZE; i++) {
		hlist_for_each_entry_safe(rem_info, tmp_hlist_node,
			&iwpm_reminfo_bucket[i], hlist_node) {

				hlist_del_init(&rem_info->hlist_node);
				kfree(rem_info);
			}
	}
	/* free the hash list */
	kfree(iwpm_reminfo_bucket);
	iwpm_reminfo_bucket = NULL;
	spin_unlock_irqrestore(&iwpm_reminfo_lock, flags);
}

static struct hlist_head *get_reminfo_hash_bucket(struct sockaddr_storage *,
						struct sockaddr_storage *);

void iwpm_add_remote_info(struct iwpm_remote_info *rem_info)
{
	struct hlist_head *hash_bucket_head;
	unsigned long flags;

	spin_lock_irqsave(&iwpm_reminfo_lock, flags);
	if (iwpm_reminfo_bucket) {
		hash_bucket_head = get_reminfo_hash_bucket(
					&rem_info->mapped_loc_sockaddr,
					&rem_info->mapped_rem_sockaddr);
		if (hash_bucket_head)
			hlist_add_head(&rem_info->hlist_node, hash_bucket_head);
	}
	spin_unlock_irqrestore(&iwpm_reminfo_lock, flags);
}

/**
 * iwpm_get_remote_info - Get the remote connecting peer address info
 *
 * @mapped_loc_addr: Mapped local address of the listening peer
 * @mapped_rem_addr: Mapped remote address of the connecting peer
 * @remote_addr: To store the remote address of the connecting peer
 * @nl_client: The index of the netlink client
 *
 * The remote address info is retrieved and provided to the client in
 * the remote_addr. After that it is removed from the hash table
 */
int iwpm_get_remote_info(struct sockaddr_storage *mapped_loc_addr,
			 struct sockaddr_storage *mapped_rem_addr,
			 struct sockaddr_storage *remote_addr,
			 u8 nl_client)
{
	struct hlist_node *tmp_hlist_node;
	struct hlist_head *hash_bucket_head;
	struct iwpm_remote_info *rem_info = NULL;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&iwpm_reminfo_lock, flags);
	if (iwpm_reminfo_bucket) {
		hash_bucket_head = get_reminfo_hash_bucket(
					mapped_loc_addr,
					mapped_rem_addr);
		if (!hash_bucket_head)
			goto get_remote_info_exit;
		hlist_for_each_entry_safe(rem_info, tmp_hlist_node,
					hash_bucket_head, hlist_node) {

			if (!iwpm_compare_sockaddr(&rem_info->mapped_loc_sockaddr,
				mapped_loc_addr) &&
				!iwpm_compare_sockaddr(&rem_info->mapped_rem_sockaddr,
				mapped_rem_addr)) {

				memcpy(remote_addr, &rem_info->remote_sockaddr,
					sizeof(struct sockaddr_storage));
				iwpm_print_sockaddr(remote_addr,
						"get_remote_info: Remote sockaddr:");

				hlist_del_init(&rem_info->hlist_node);
				kfree(rem_info);
				ret = 0;
				break;
			}
		}
	}
get_remote_info_exit:
	spin_unlock_irqrestore(&iwpm_reminfo_lock, flags);
	return ret;
}

struct iwpm_nlmsg_request *iwpm_get_nlmsg_request(__u32 nlmsg_seq,
					u8 nl_client, gfp_t gfp)
{
	struct iwpm_nlmsg_request *nlmsg_request;
	unsigned long flags;

	nlmsg_request = kzalloc(sizeof(struct iwpm_nlmsg_request), gfp);
	if (!nlmsg_request)
		return NULL;

	spin_lock_irqsave(&iwpm_nlmsg_req_lock, flags);
	list_add_tail(&nlmsg_request->inprocess_list, &iwpm_nlmsg_req_list);
	spin_unlock_irqrestore(&iwpm_nlmsg_req_lock, flags);

	kref_init(&nlmsg_request->kref);
	kref_get(&nlmsg_request->kref);
	nlmsg_request->nlmsg_seq = nlmsg_seq;
	nlmsg_request->nl_client = nl_client;
	nlmsg_request->request_done = 0;
	nlmsg_request->err_code = 0;
	sema_init(&nlmsg_request->sem, 1);
	down(&nlmsg_request->sem);
	return nlmsg_request;
}

void iwpm_free_nlmsg_request(struct kref *kref)
{
	struct iwpm_nlmsg_request *nlmsg_request;
	unsigned long flags;

	nlmsg_request = container_of(kref, struct iwpm_nlmsg_request, kref);

	spin_lock_irqsave(&iwpm_nlmsg_req_lock, flags);
	list_del_init(&nlmsg_request->inprocess_list);
	spin_unlock_irqrestore(&iwpm_nlmsg_req_lock, flags);

	if (!nlmsg_request->request_done)
		pr_debug("%s Freeing incomplete nlmsg request (seq = %u).\n",
			__func__, nlmsg_request->nlmsg_seq);
	kfree(nlmsg_request);
}

struct iwpm_nlmsg_request *iwpm_find_nlmsg_request(__u32 echo_seq)
{
	struct iwpm_nlmsg_request *nlmsg_request;
	struct iwpm_nlmsg_request *found_request = NULL;
	unsigned long flags;

	spin_lock_irqsave(&iwpm_nlmsg_req_lock, flags);
	list_for_each_entry(nlmsg_request, &iwpm_nlmsg_req_list,
			    inprocess_list) {
		if (nlmsg_request->nlmsg_seq == echo_seq) {
			found_request = nlmsg_request;
			kref_get(&nlmsg_request->kref);
			break;
		}
	}
	spin_unlock_irqrestore(&iwpm_nlmsg_req_lock, flags);
	return found_request;
}

int iwpm_wait_complete_req(struct iwpm_nlmsg_request *nlmsg_request)
{
	int ret;

	ret = down_timeout(&nlmsg_request->sem, IWPM_NL_TIMEOUT);
	if (ret) {
		ret = -EINVAL;
		pr_info("%s: Timeout %d sec for netlink request (seq = %u)\n",
			__func__, (IWPM_NL_TIMEOUT/HZ), nlmsg_request->nlmsg_seq);
	} else {
		ret = nlmsg_request->err_code;
	}
	kref_put(&nlmsg_request->kref, iwpm_free_nlmsg_request);
	return ret;
}

int iwpm_get_nlmsg_seq(void)
{
	return atomic_inc_return(&iwpm_admin.nlmsg_seq);
}

/* valid client */
u32 iwpm_get_registration(u8 nl_client)
{
	return iwpm_admin.reg_list[nl_client];
}

/* valid client */
void iwpm_set_registration(u8 nl_client, u32 reg)
{
	iwpm_admin.reg_list[nl_client] = reg;
}

/* valid client */
u32 iwpm_check_registration(u8 nl_client, u32 reg)
{
	return (iwpm_get_registration(nl_client) & reg);
}

int iwpm_compare_sockaddr(struct sockaddr_storage *a_sockaddr,
				struct sockaddr_storage *b_sockaddr)
{
	if (a_sockaddr->ss_family != b_sockaddr->ss_family)
		return 1;
	if (a_sockaddr->ss_family == AF_INET) {
		struct sockaddr_in *a4_sockaddr =
			(struct sockaddr_in *)a_sockaddr;
		struct sockaddr_in *b4_sockaddr =
			(struct sockaddr_in *)b_sockaddr;
		if (!memcmp(&a4_sockaddr->sin_addr,
			&b4_sockaddr->sin_addr, sizeof(struct in_addr))
			&& a4_sockaddr->sin_port == b4_sockaddr->sin_port)
				return 0;

	} else if (a_sockaddr->ss_family == AF_INET6) {
		struct sockaddr_in6 *a6_sockaddr =
			(struct sockaddr_in6 *)a_sockaddr;
		struct sockaddr_in6 *b6_sockaddr =
			(struct sockaddr_in6 *)b_sockaddr;
		if (!memcmp(&a6_sockaddr->sin6_addr,
			&b6_sockaddr->sin6_addr, sizeof(struct in6_addr))
			&& a6_sockaddr->sin6_port == b6_sockaddr->sin6_port)
				return 0;

	} else {
		pr_err("%s: Invalid sockaddr family\n", __func__);
	}
	return 1;
}

struct sk_buff *iwpm_create_nlmsg(u32 nl_op, struct nlmsghdr **nlh,
						int nl_client)
{
	struct sk_buff *skb = NULL;

	skb = dev_alloc_skb(IWPM_MSG_SIZE);
	if (!skb)
		goto create_nlmsg_exit;

	if (!(ibnl_put_msg(skb, nlh, 0, 0, nl_client, nl_op,
			   NLM_F_REQUEST))) {
		pr_warn("%s: Unable to put the nlmsg header\n", __func__);
		dev_kfree_skb(skb);
		skb = NULL;
	}
create_nlmsg_exit:
	return skb;
}

int iwpm_parse_nlmsg(struct netlink_callback *cb, int policy_max,
				   const struct nla_policy *nlmsg_policy,
				   struct nlattr *nltb[], const char *msg_type)
{
	int nlh_len = 0;
	int ret;
	const char *err_str = "";

	ret = nlmsg_validate_deprecated(cb->nlh, nlh_len, policy_max - 1,
					nlmsg_policy, NULL);
	if (ret) {
		err_str = "Invalid attribute";
		goto parse_nlmsg_error;
	}
	ret = nlmsg_parse_deprecated(cb->nlh, nlh_len, nltb, policy_max - 1,
				     nlmsg_policy, NULL);
	if (ret) {
		err_str = "Unable to parse the nlmsg";
		goto parse_nlmsg_error;
	}
	ret = iwpm_validate_nlmsg_attr(nltb, policy_max);
	if (ret) {
		err_str = "Invalid NULL attribute";
		goto parse_nlmsg_error;
	}
	return 0;
parse_nlmsg_error:
	pr_warn("%s: %s (msg type %s ret = %d)\n",
			__func__, err_str, msg_type, ret);
	return ret;
}

void iwpm_print_sockaddr(struct sockaddr_storage *sockaddr, char *msg)
{
	struct sockaddr_in6 *sockaddr_v6;
	struct sockaddr_in *sockaddr_v4;

	switch (sockaddr->ss_family) {
	case AF_INET:
		sockaddr_v4 = (struct sockaddr_in *)sockaddr;
		pr_debug("%s IPV4 %pI4: %u(0x%04X)\n",
			msg, &sockaddr_v4->sin_addr,
			ntohs(sockaddr_v4->sin_port),
			ntohs(sockaddr_v4->sin_port));
		break;
	case AF_INET6:
		sockaddr_v6 = (struct sockaddr_in6 *)sockaddr;
		pr_debug("%s IPV6 %pI6: %u(0x%04X)\n",
			msg, &sockaddr_v6->sin6_addr,
			ntohs(sockaddr_v6->sin6_port),
			ntohs(sockaddr_v6->sin6_port));
		break;
	default:
		break;
	}
}

static u32 iwpm_ipv6_jhash(struct sockaddr_in6 *ipv6_sockaddr)
{
	u32 ipv6_hash = jhash(&ipv6_sockaddr->sin6_addr, sizeof(struct in6_addr), 0);
	u32 hash = jhash_2words(ipv6_hash, (__force u32) ipv6_sockaddr->sin6_port, 0);
	return hash;
}

static u32 iwpm_ipv4_jhash(struct sockaddr_in *ipv4_sockaddr)
{
	u32 ipv4_hash = jhash(&ipv4_sockaddr->sin_addr, sizeof(struct in_addr), 0);
	u32 hash = jhash_2words(ipv4_hash, (__force u32) ipv4_sockaddr->sin_port, 0);
	return hash;
}

static int get_hash_bucket(struct sockaddr_storage *a_sockaddr,
				struct sockaddr_storage *b_sockaddr, u32 *hash)
{
	u32 a_hash, b_hash;

	if (a_sockaddr->ss_family == AF_INET) {
		a_hash = iwpm_ipv4_jhash((struct sockaddr_in *) a_sockaddr);
		b_hash = iwpm_ipv4_jhash((struct sockaddr_in *) b_sockaddr);

	} else if (a_sockaddr->ss_family == AF_INET6) {
		a_hash = iwpm_ipv6_jhash((struct sockaddr_in6 *) a_sockaddr);
		b_hash = iwpm_ipv6_jhash((struct sockaddr_in6 *) b_sockaddr);
	} else {
		pr_err("%s: Invalid sockaddr family\n", __func__);
		return -EINVAL;
	}

	if (a_hash == b_hash) /* if port mapper isn't available */
		*hash = a_hash;
	else
		*hash = jhash_2words(a_hash, b_hash, 0);
	return 0;
}

static struct hlist_head *get_mapinfo_hash_bucket(struct sockaddr_storage
				*local_sockaddr, struct sockaddr_storage
				*mapped_sockaddr)
{
	u32 hash;
	int ret;

	ret = get_hash_bucket(local_sockaddr, mapped_sockaddr, &hash);
	if (ret)
		return NULL;
	return &iwpm_hash_bucket[hash & IWPM_MAPINFO_HASH_MASK];
}

static struct hlist_head *get_reminfo_hash_bucket(struct sockaddr_storage
				*mapped_loc_sockaddr, struct sockaddr_storage
				*mapped_rem_sockaddr)
{
	u32 hash;
	int ret;

	ret = get_hash_bucket(mapped_loc_sockaddr, mapped_rem_sockaddr, &hash);
	if (ret)
		return NULL;
	return &iwpm_reminfo_bucket[hash & IWPM_REMINFO_HASH_MASK];
}

static int send_mapinfo_num(u32 mapping_num, u8 nl_client, int iwpm_pid)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	u32 msg_seq;
	const char *err_str = "";
	int ret = -EINVAL;

	skb = iwpm_create_nlmsg(RDMA_NL_IWPM_MAPINFO_NUM, &nlh, nl_client);
	if (!skb) {
		err_str = "Unable to create a nlmsg";
		goto mapinfo_num_error;
	}
	nlh->nlmsg_seq = iwpm_get_nlmsg_seq();
	msg_seq = 0;
	err_str = "Unable to put attribute of mapinfo number nlmsg";
	ret = ibnl_put_attr(skb, nlh, sizeof(u32), &msg_seq, IWPM_NLA_MAPINFO_SEQ);
	if (ret)
		goto mapinfo_num_error;
	ret = ibnl_put_attr(skb, nlh, sizeof(u32),
				&mapping_num, IWPM_NLA_MAPINFO_SEND_NUM);
	if (ret)
		goto mapinfo_num_error;

	nlmsg_end(skb, nlh);

	ret = rdma_nl_unicast(&init_net, skb, iwpm_pid);
	if (ret) {
		skb = NULL;
		err_str = "Unable to send a nlmsg";
		goto mapinfo_num_error;
	}
	pr_debug("%s: Sent mapping number = %u\n", __func__, mapping_num);
	return 0;
mapinfo_num_error:
	pr_info("%s: %s\n", __func__, err_str);
	dev_kfree_skb(skb);
	return ret;
}

static int send_nlmsg_done(struct sk_buff *skb, u8 nl_client, int iwpm_pid)
{
	struct nlmsghdr *nlh = NULL;
	int ret = 0;

	if (!skb)
		return ret;
	if (!(ibnl_put_msg(skb, &nlh, 0, 0, nl_client,
			   RDMA_NL_IWPM_MAPINFO, NLM_F_MULTI))) {
		pr_warn("%s Unable to put NLMSG_DONE\n", __func__);
		dev_kfree_skb(skb);
		return -ENOMEM;
	}
	nlh->nlmsg_type = NLMSG_DONE;
	ret = rdma_nl_unicast(&init_net, skb, iwpm_pid);
	if (ret)
		pr_warn("%s Unable to send a nlmsg\n", __func__);
	return ret;
}

int iwpm_send_mapinfo(u8 nl_client, int iwpm_pid)
{
	struct iwpm_mapping_info *map_info;
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	int skb_num = 0, mapping_num = 0;
	int i = 0, nlmsg_bytes = 0;
	unsigned long flags;
	const char *err_str = "";
	int ret;

	skb = dev_alloc_skb(NLMSG_GOODSIZE);
	if (!skb) {
		ret = -ENOMEM;
		err_str = "Unable to allocate skb";
		goto send_mapping_info_exit;
	}
	skb_num++;
	spin_lock_irqsave(&iwpm_mapinfo_lock, flags);
	ret = -EINVAL;
	for (i = 0; i < IWPM_MAPINFO_HASH_SIZE; i++) {
		hlist_for_each_entry(map_info, &iwpm_hash_bucket[i],
				     hlist_node) {
			if (map_info->nl_client != nl_client)
				continue;
			nlh = NULL;
			if (!(ibnl_put_msg(skb, &nlh, 0, 0, nl_client,
					RDMA_NL_IWPM_MAPINFO, NLM_F_MULTI))) {
				ret = -ENOMEM;
				err_str = "Unable to put the nlmsg header";
				goto send_mapping_info_unlock;
			}
			err_str = "Unable to put attribute of the nlmsg";
			ret = ibnl_put_attr(skb, nlh,
					sizeof(struct sockaddr_storage),
					&map_info->local_sockaddr,
					IWPM_NLA_MAPINFO_LOCAL_ADDR);
			if (ret)
				goto send_mapping_info_unlock;

			ret = ibnl_put_attr(skb, nlh,
					sizeof(struct sockaddr_storage),
					&map_info->mapped_sockaddr,
					IWPM_NLA_MAPINFO_MAPPED_ADDR);
			if (ret)
				goto send_mapping_info_unlock;

			if (iwpm_ulib_version > IWPM_UABI_VERSION_MIN) {
				ret = ibnl_put_attr(skb, nlh, sizeof(u32),
						&map_info->map_flags,
						IWPM_NLA_MAPINFO_FLAGS);
				if (ret)
					goto send_mapping_info_unlock;
			}

			nlmsg_end(skb, nlh);

			iwpm_print_sockaddr(&map_info->local_sockaddr,
				"send_mapping_info: Local sockaddr:");
			iwpm_print_sockaddr(&map_info->mapped_sockaddr,
				"send_mapping_info: Mapped local sockaddr:");
			mapping_num++;
			nlmsg_bytes += nlh->nlmsg_len;

			/* check if all mappings can fit in one skb */
			if (NLMSG_GOODSIZE - nlmsg_bytes < nlh->nlmsg_len * 2) {
				/* and leave room for NLMSG_DONE */
				nlmsg_bytes = 0;
				skb_num++;
				spin_unlock_irqrestore(&iwpm_mapinfo_lock,
						       flags);
				/* send the skb */
				ret = send_nlmsg_done(skb, nl_client, iwpm_pid);
				skb = NULL;
				if (ret) {
					err_str = "Unable to send map info";
					goto send_mapping_info_exit;
				}
				if (skb_num == IWPM_MAPINFO_SKB_COUNT) {
					ret = -ENOMEM;
					err_str = "Insufficient skbs for map info";
					goto send_mapping_info_exit;
				}
				skb = dev_alloc_skb(NLMSG_GOODSIZE);
				if (!skb) {
					ret = -ENOMEM;
					err_str = "Unable to allocate skb";
					goto send_mapping_info_exit;
				}
				spin_lock_irqsave(&iwpm_mapinfo_lock, flags);
			}
		}
	}
send_mapping_info_unlock:
	spin_unlock_irqrestore(&iwpm_mapinfo_lock, flags);
send_mapping_info_exit:
	if (ret) {
		pr_warn("%s: %s (ret = %d)\n", __func__, err_str, ret);
		dev_kfree_skb(skb);
		return ret;
	}
	send_nlmsg_done(skb, nl_client, iwpm_pid);
	return send_mapinfo_num(mapping_num, nl_client, iwpm_pid);
}

int iwpm_mapinfo_available(void)
{
	unsigned long flags;
	int full_bucket = 0, i = 0;

	spin_lock_irqsave(&iwpm_mapinfo_lock, flags);
	if (iwpm_hash_bucket) {
		for (i = 0; i < IWPM_MAPINFO_HASH_SIZE; i++) {
			if (!hlist_empty(&iwpm_hash_bucket[i])) {
				full_bucket = 1;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&iwpm_mapinfo_lock, flags);
	return full_bucket;
}

int iwpm_send_hello(u8 nl_client, int iwpm_pid, u16 abi_version)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	const char *err_str;
	int ret = -EINVAL;

	skb = iwpm_create_nlmsg(RDMA_NL_IWPM_HELLO, &nlh, nl_client);
	if (!skb) {
		err_str = "Unable to create a nlmsg";
		goto hello_num_error;
	}
	nlh->nlmsg_seq = iwpm_get_nlmsg_seq();
	err_str = "Unable to put attribute of abi_version into nlmsg";
	ret = ibnl_put_attr(skb, nlh, sizeof(u16), &abi_version,
			    IWPM_NLA_HELLO_ABI_VERSION);
	if (ret)
		goto hello_num_error;
	nlmsg_end(skb, nlh);

	ret = rdma_nl_unicast(&init_net, skb, iwpm_pid);
	if (ret) {
		skb = NULL;
		err_str = "Unable to send a nlmsg";
		goto hello_num_error;
	}
	pr_debug("%s: Sent hello abi_version = %u\n", __func__, abi_version);
	return 0;
hello_num_error:
	pr_info("%s: %s\n", __func__, err_str);
	dev_kfree_skb(skb);
	return ret;
}
