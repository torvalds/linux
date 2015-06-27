/*
 * Copyright (c) 2006 - 2014 Intel Corporation.  All rights reserved.
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
 *
 */


#define TCPOPT_TIMESTAMP 8

#include <linux/atomic.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/init.h>
#include <linux/if_arp.h>
#include <linux/if_vlan.h>
#include <linux/notifier.h>
#include <linux/net.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/random.h>
#include <linux/list.h>
#include <linux/threads.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <net/arp.h>
#include <net/neighbour.h>
#include <net/route.h>
#include <net/ip_fib.h>
#include <net/tcp.h>
#include <linux/fcntl.h>

#include "nes.h"

u32 cm_packets_sent;
u32 cm_packets_bounced;
u32 cm_packets_dropped;
u32 cm_packets_retrans;
u32 cm_packets_created;
u32 cm_packets_received;
atomic_t cm_listens_created;
atomic_t cm_listens_destroyed;
u32 cm_backlog_drops;
atomic_t cm_loopbacks;
atomic_t cm_nodes_created;
atomic_t cm_nodes_destroyed;
atomic_t cm_accel_dropped_pkts;
atomic_t cm_resets_recvd;

static inline int mini_cm_accelerated(struct nes_cm_core *, struct nes_cm_node *);
static struct nes_cm_listener *mini_cm_listen(struct nes_cm_core *, struct nes_vnic *, struct nes_cm_info *);
static int mini_cm_del_listen(struct nes_cm_core *, struct nes_cm_listener *);
static struct nes_cm_node *mini_cm_connect(struct nes_cm_core *, struct nes_vnic *, u16, void *, struct nes_cm_info *);
static int mini_cm_close(struct nes_cm_core *, struct nes_cm_node *);
static int mini_cm_accept(struct nes_cm_core *, struct nes_cm_node *);
static int mini_cm_reject(struct nes_cm_core *, struct nes_cm_node *);
static int mini_cm_recv_pkt(struct nes_cm_core *, struct nes_vnic *, struct sk_buff *);
static int mini_cm_dealloc_core(struct nes_cm_core *);
static int mini_cm_get(struct nes_cm_core *);
static int mini_cm_set(struct nes_cm_core *, u32, u32);

static void form_cm_frame(struct sk_buff *, struct nes_cm_node *, void *, u32, void *, u32, u8);
static int add_ref_cm_node(struct nes_cm_node *);
static int rem_ref_cm_node(struct nes_cm_core *, struct nes_cm_node *);

static int nes_cm_disconn_true(struct nes_qp *);
static int nes_cm_post_event(struct nes_cm_event *event);
static int nes_disconnect(struct nes_qp *nesqp, int abrupt);
static void nes_disconnect_worker(struct work_struct *work);

static int send_mpa_request(struct nes_cm_node *, struct sk_buff *);
static int send_mpa_reject(struct nes_cm_node *);
static int send_syn(struct nes_cm_node *, u32, struct sk_buff *);
static int send_reset(struct nes_cm_node *, struct sk_buff *);
static int send_ack(struct nes_cm_node *cm_node, struct sk_buff *skb);
static int send_fin(struct nes_cm_node *cm_node, struct sk_buff *skb);
static void process_packet(struct nes_cm_node *, struct sk_buff *, struct nes_cm_core *);

static void active_open_err(struct nes_cm_node *, struct sk_buff *, int);
static void passive_open_err(struct nes_cm_node *, struct sk_buff *, int);
static void cleanup_retrans_entry(struct nes_cm_node *);
static void handle_rcv_mpa(struct nes_cm_node *, struct sk_buff *);
static void free_retrans_entry(struct nes_cm_node *cm_node);
static int handle_tcp_options(struct nes_cm_node *cm_node, struct tcphdr *tcph, struct sk_buff *skb, int optionsize, int passive);

/* CM event handler functions */
static void cm_event_connected(struct nes_cm_event *);
static void cm_event_connect_error(struct nes_cm_event *);
static void cm_event_reset(struct nes_cm_event *);
static void cm_event_mpa_req(struct nes_cm_event *);
static void cm_event_mpa_reject(struct nes_cm_event *);
static void handle_recv_entry(struct nes_cm_node *cm_node, u32 rem_node);

/* MPA build functions */
static int cm_build_mpa_frame(struct nes_cm_node *, u8 **, u16 *, u8 *, u8);
static void build_mpa_v2(struct nes_cm_node *, void *, u8);
static void build_mpa_v1(struct nes_cm_node *, void *, u8);
static void build_rdma0_msg(struct nes_cm_node *, struct nes_qp **);

static void print_core(struct nes_cm_core *core);
static void record_ird_ord(struct nes_cm_node *, u16, u16);

/* External CM API Interface */
/* instance of function pointers for client API */
/* set address of this instance to cm_core->cm_ops at cm_core alloc */
static struct nes_cm_ops nes_cm_api = {
	mini_cm_accelerated,
	mini_cm_listen,
	mini_cm_del_listen,
	mini_cm_connect,
	mini_cm_close,
	mini_cm_accept,
	mini_cm_reject,
	mini_cm_recv_pkt,
	mini_cm_dealloc_core,
	mini_cm_get,
	mini_cm_set
};

static struct nes_cm_core *g_cm_core;

atomic_t cm_connects;
atomic_t cm_accepts;
atomic_t cm_disconnects;
atomic_t cm_closes;
atomic_t cm_connecteds;
atomic_t cm_connect_reqs;
atomic_t cm_rejects;

int nes_add_ref_cm_node(struct nes_cm_node *cm_node)
{
	return add_ref_cm_node(cm_node);
}

int nes_rem_ref_cm_node(struct nes_cm_node *cm_node)
{
	return rem_ref_cm_node(cm_node->cm_core, cm_node);
}
/**
 * create_event
 */
static struct nes_cm_event *create_event(struct nes_cm_node *	cm_node,
					 enum nes_cm_event_type type)
{
	struct nes_cm_event *event;

	if (!cm_node->cm_id)
		return NULL;

	/* allocate an empty event */
	event = kzalloc(sizeof(*event), GFP_ATOMIC);

	if (!event)
		return NULL;

	event->type = type;
	event->cm_node = cm_node;
	event->cm_info.rem_addr = cm_node->rem_addr;
	event->cm_info.loc_addr = cm_node->loc_addr;
	event->cm_info.rem_port = cm_node->rem_port;
	event->cm_info.loc_port = cm_node->loc_port;
	event->cm_info.cm_id = cm_node->cm_id;

	nes_debug(NES_DBG_CM, "cm_node=%p Created event=%p, type=%u, "
		  "dst_addr=%08x[%x], src_addr=%08x[%x]\n",
		  cm_node, event, type, event->cm_info.loc_addr,
		  event->cm_info.loc_port, event->cm_info.rem_addr,
		  event->cm_info.rem_port);

	nes_cm_post_event(event);
	return event;
}


/**
 * send_mpa_request
 */
static int send_mpa_request(struct nes_cm_node *cm_node, struct sk_buff *skb)
{
	u8 start_addr = 0;
	u8 *start_ptr = &start_addr;
	u8 **start_buff = &start_ptr;
	u16 buff_len = 0;

	if (!skb) {
		nes_debug(NES_DBG_CM, "skb set to NULL\n");
		return -1;
	}

	/* send an MPA Request frame */
	cm_build_mpa_frame(cm_node, start_buff, &buff_len, NULL, MPA_KEY_REQUEST);
	form_cm_frame(skb, cm_node, NULL, 0, *start_buff, buff_len, SET_ACK);

	return schedule_nes_timer(cm_node, skb, NES_TIMER_TYPE_SEND, 1, 0);
}



static int send_mpa_reject(struct nes_cm_node *cm_node)
{
	struct sk_buff *skb = NULL;
	u8 start_addr = 0;
	u8 *start_ptr = &start_addr;
	u8 **start_buff = &start_ptr;
	u16 buff_len = 0;
	struct ietf_mpa_v1 *mpa_frame;

	skb = dev_alloc_skb(MAX_CM_BUFFER);
	if (!skb) {
		nes_debug(NES_DBG_CM, "Failed to get a Free pkt\n");
		return -ENOMEM;
	}

	/* send an MPA reject frame */
	cm_build_mpa_frame(cm_node, start_buff, &buff_len, NULL, MPA_KEY_REPLY);
	mpa_frame = (struct ietf_mpa_v1 *)*start_buff;
	mpa_frame->flags |= IETF_MPA_FLAGS_REJECT;
	form_cm_frame(skb, cm_node, NULL, 0, *start_buff, buff_len, SET_ACK | SET_FIN);

	cm_node->state = NES_CM_STATE_FIN_WAIT1;
	return schedule_nes_timer(cm_node, skb, NES_TIMER_TYPE_SEND, 1, 0);
}


/**
 * recv_mpa - process a received TCP pkt, we are expecting an
 * IETF MPA frame
 */
static int parse_mpa(struct nes_cm_node *cm_node, u8 *buffer, u32 *type,
		     u32 len)
{
	struct ietf_mpa_v1 *mpa_frame;
	struct ietf_mpa_v2 *mpa_v2_frame;
	struct ietf_rtr_msg *rtr_msg;
	int mpa_hdr_len;
	int priv_data_len;

	*type = NES_MPA_REQUEST_ACCEPT;

	/* assume req frame is in tcp data payload */
	if (len < sizeof(struct ietf_mpa_v1)) {
		nes_debug(NES_DBG_CM, "The received ietf buffer was too small (%x)\n", len);
		return -EINVAL;
	}

	/* points to the beginning of the frame, which could be MPA V1 or V2 */
	mpa_frame = (struct ietf_mpa_v1 *)buffer;
	mpa_hdr_len = sizeof(struct ietf_mpa_v1);
	priv_data_len = ntohs(mpa_frame->priv_data_len);

	/* make sure mpa private data len is less than 512 bytes */
	if (priv_data_len > IETF_MAX_PRIV_DATA_LEN) {
		nes_debug(NES_DBG_CM, "The received Length of Private"
			  " Data field exceeds 512 octets\n");
		return -EINVAL;
	}
	/*
	 * make sure MPA receiver interoperate with the
	 * received MPA version and MPA key information
	 *
	 */
	if (mpa_frame->rev != IETF_MPA_V1 && mpa_frame->rev != IETF_MPA_V2) {
		nes_debug(NES_DBG_CM, "The received mpa version"
			  " is not supported\n");
		return -EINVAL;
	}
	/*
	* backwards compatibility only
	*/
	if (mpa_frame->rev > cm_node->mpa_frame_rev) {
		nes_debug(NES_DBG_CM, "The received mpa version"
			" can not be interoperated\n");
		return -EINVAL;
	} else {
		cm_node->mpa_frame_rev = mpa_frame->rev;
	}

	if (cm_node->state != NES_CM_STATE_MPAREQ_SENT) {
		if (memcmp(mpa_frame->key, IEFT_MPA_KEY_REQ, IETF_MPA_KEY_SIZE)) {
			nes_debug(NES_DBG_CM, "Unexpected MPA Key received \n");
			return -EINVAL;
		}
	} else {
		if (memcmp(mpa_frame->key, IEFT_MPA_KEY_REP, IETF_MPA_KEY_SIZE)) {
			nes_debug(NES_DBG_CM, "Unexpected MPA Key received \n");
			return -EINVAL;
		}
	}

	if (priv_data_len + mpa_hdr_len != len) {
		nes_debug(NES_DBG_CM, "The received ietf buffer was not right"
			" complete (%x + %x != %x)\n",
			priv_data_len, mpa_hdr_len, len);
		return -EINVAL;
	}
	/* make sure it does not exceed the max size */
	if (len > MAX_CM_BUFFER) {
		nes_debug(NES_DBG_CM, "The received ietf buffer was too large"
			" (%x + %x != %x)\n",
			priv_data_len, mpa_hdr_len, len);
		return -EINVAL;
	}

	cm_node->mpa_frame_size = priv_data_len;

	switch (mpa_frame->rev) {
	case IETF_MPA_V2: {
		u16 ird_size;
		u16 ord_size;
		u16 rtr_ctrl_ird;
		u16 rtr_ctrl_ord;

		mpa_v2_frame = (struct ietf_mpa_v2 *)buffer;
		mpa_hdr_len += IETF_RTR_MSG_SIZE;
		cm_node->mpa_frame_size -= IETF_RTR_MSG_SIZE;
		rtr_msg = &mpa_v2_frame->rtr_msg;

		/* parse rtr message */
		rtr_ctrl_ird = ntohs(rtr_msg->ctrl_ird);
		rtr_ctrl_ord = ntohs(rtr_msg->ctrl_ord);
		ird_size = rtr_ctrl_ird & IETF_NO_IRD_ORD;
		ord_size = rtr_ctrl_ord & IETF_NO_IRD_ORD;

		if (!(rtr_ctrl_ird & IETF_PEER_TO_PEER)) {
			/* send reset */
			return -EINVAL;
		}
		if (ird_size == IETF_NO_IRD_ORD || ord_size == IETF_NO_IRD_ORD)
			cm_node->mpav2_ird_ord = IETF_NO_IRD_ORD;

		if (cm_node->mpav2_ird_ord != IETF_NO_IRD_ORD) {
			/* responder */
			if (cm_node->state != NES_CM_STATE_MPAREQ_SENT) {
				/* we are still negotiating */
				if (ord_size > NES_MAX_IRD) {
					cm_node->ird_size = NES_MAX_IRD;
				} else {
					cm_node->ird_size = ord_size;
					if (ord_size == 0 &&
					(rtr_ctrl_ord & IETF_RDMA0_READ)) {
						cm_node->ird_size = 1;
						nes_debug(NES_DBG_CM,
						"%s: Remote peer doesn't support RDMA0_READ (ord=%u)\n",
							__func__, ord_size);
					}
				}
				if (ird_size > NES_MAX_ORD)
					cm_node->ord_size = NES_MAX_ORD;
				else
					cm_node->ord_size = ird_size;
			} else { /* initiator */
				if (ord_size > NES_MAX_IRD) {
					nes_debug(NES_DBG_CM,
					"%s: Unable to support the requested (ord =%u)\n",
							__func__, ord_size);
					return -EINVAL;
				}
				cm_node->ird_size = ord_size;

				if (ird_size > NES_MAX_ORD) {
					cm_node->ord_size = NES_MAX_ORD;
				} else {
					if (ird_size == 0 &&
					(rtr_ctrl_ord & IETF_RDMA0_READ)) {
						nes_debug(NES_DBG_CM,
						"%s: Remote peer doesn't support RDMA0_READ (ird=%u)\n",
							__func__, ird_size);
						return -EINVAL;
					} else {
						cm_node->ord_size = ird_size;
					}
				}
			}
		}

		if (rtr_ctrl_ord & IETF_RDMA0_READ) {
			cm_node->send_rdma0_op = SEND_RDMA_READ_ZERO;

		} else if (rtr_ctrl_ord & IETF_RDMA0_WRITE) {
			cm_node->send_rdma0_op = SEND_RDMA_WRITE_ZERO;
		} else {        /* Not supported RDMA0 operation */
			return -EINVAL;
		}
		break;
	}
	case IETF_MPA_V1:
	default:
		break;
	}

	/* copy entire MPA frame to our cm_node's frame */
	memcpy(cm_node->mpa_frame_buf, buffer + mpa_hdr_len, cm_node->mpa_frame_size);

	if (mpa_frame->flags & IETF_MPA_FLAGS_REJECT)
		*type = NES_MPA_REQUEST_REJECT;
	return 0;
}


/**
 * form_cm_frame - get a free packet and build empty frame Use
 * node info to build.
 */
static void form_cm_frame(struct sk_buff *skb,
			  struct nes_cm_node *cm_node, void *options, u32 optionsize,
			  void *data, u32 datasize, u8 flags)
{
	struct tcphdr *tcph;
	struct iphdr *iph;
	struct ethhdr *ethh;
	u8 *buf;
	u16 packetsize = sizeof(*iph);

	packetsize += sizeof(*tcph);
	packetsize += optionsize + datasize;

	skb_trim(skb, 0);
	memset(skb->data, 0x00, ETH_HLEN + sizeof(*iph) + sizeof(*tcph));

	buf = skb_put(skb, packetsize + ETH_HLEN);

	ethh = (struct ethhdr *)buf;
	buf += ETH_HLEN;

	iph = (struct iphdr *)buf;
	buf += sizeof(*iph);
	tcph = (struct tcphdr *)buf;
	skb_reset_mac_header(skb);
	skb_set_network_header(skb, ETH_HLEN);
	skb_set_transport_header(skb, ETH_HLEN + sizeof(*iph));
	buf += sizeof(*tcph);

	skb->ip_summed = CHECKSUM_PARTIAL;
	if (!(cm_node->netdev->features & NETIF_F_IP_CSUM))
		skb->ip_summed = CHECKSUM_NONE;
	skb->protocol = htons(0x800);
	skb->data_len = 0;
	skb->mac_len = ETH_HLEN;

	memcpy(ethh->h_dest, cm_node->rem_mac, ETH_ALEN);
	memcpy(ethh->h_source, cm_node->loc_mac, ETH_ALEN);
	ethh->h_proto = htons(0x0800);

	iph->version = IPVERSION;
	iph->ihl = 5;           /* 5 * 4Byte words, IP headr len */
	iph->tos = 0;
	iph->tot_len = htons(packetsize);
	iph->id = htons(++cm_node->tcp_cntxt.loc_id);

	iph->frag_off = htons(0x4000);
	iph->ttl = 0x40;
	iph->protocol = 0x06;   /* IPPROTO_TCP */

	iph->saddr = htonl(cm_node->mapped_loc_addr);
	iph->daddr = htonl(cm_node->mapped_rem_addr);

	tcph->source = htons(cm_node->mapped_loc_port);
	tcph->dest = htons(cm_node->mapped_rem_port);
	tcph->seq = htonl(cm_node->tcp_cntxt.loc_seq_num);

	if (flags & SET_ACK) {
		cm_node->tcp_cntxt.loc_ack_num = cm_node->tcp_cntxt.rcv_nxt;
		tcph->ack_seq = htonl(cm_node->tcp_cntxt.loc_ack_num);
		tcph->ack = 1;
	} else {
		tcph->ack_seq = 0;
	}

	if (flags & SET_SYN) {
		cm_node->tcp_cntxt.loc_seq_num++;
		tcph->syn = 1;
	} else {
		cm_node->tcp_cntxt.loc_seq_num += datasize;
	}

	if (flags & SET_FIN) {
		cm_node->tcp_cntxt.loc_seq_num++;
		tcph->fin = 1;
	}

	if (flags & SET_RST)
		tcph->rst = 1;

	tcph->doff = (u16)((sizeof(*tcph) + optionsize + 3) >> 2);
	tcph->window = htons(cm_node->tcp_cntxt.rcv_wnd);
	tcph->urg_ptr = 0;
	if (optionsize)
		memcpy(buf, options, optionsize);
	buf += optionsize;
	if (datasize)
		memcpy(buf, data, datasize);

	skb_shinfo(skb)->nr_frags = 0;
	cm_packets_created++;
}

/*
 * nes_create_sockaddr - Record ip addr and tcp port in a sockaddr struct
 */
static void nes_create_sockaddr(__be32 ip_addr, __be16 port,
				struct sockaddr_storage *addr)
{
	struct sockaddr_in *nes_sockaddr = (struct sockaddr_in *)addr;
	nes_sockaddr->sin_family = AF_INET;
	memcpy(&nes_sockaddr->sin_addr.s_addr, &ip_addr, sizeof(__be32));
	nes_sockaddr->sin_port = port;
}

/*
 * nes_create_mapinfo - Create a mapinfo object in the port mapper data base
 */
static int nes_create_mapinfo(struct nes_cm_info *cm_info)
{
	struct sockaddr_storage local_sockaddr;
	struct sockaddr_storage mapped_sockaddr;

	nes_create_sockaddr(htonl(cm_info->loc_addr), htons(cm_info->loc_port),
				&local_sockaddr);
	nes_create_sockaddr(htonl(cm_info->mapped_loc_addr),
			htons(cm_info->mapped_loc_port), &mapped_sockaddr);

	return iwpm_create_mapinfo(&local_sockaddr,
				&mapped_sockaddr, RDMA_NL_NES);
}

/*
 * nes_remove_mapinfo - Remove a mapinfo object from the port mapper data base
 *                      and send a remove mapping op message to
 *                      the userspace port mapper
 */
static int nes_remove_mapinfo(u32 loc_addr, u16 loc_port,
			u32 mapped_loc_addr, u16 mapped_loc_port)
{
	struct sockaddr_storage local_sockaddr;
	struct sockaddr_storage mapped_sockaddr;

	nes_create_sockaddr(htonl(loc_addr), htons(loc_port), &local_sockaddr);
	nes_create_sockaddr(htonl(mapped_loc_addr), htons(mapped_loc_port),
				&mapped_sockaddr);

	iwpm_remove_mapinfo(&local_sockaddr, &mapped_sockaddr);
	return iwpm_remove_mapping(&local_sockaddr, RDMA_NL_NES);
}

/*
 * nes_form_pm_msg - Form a port mapper message with mapping info
 */
static void nes_form_pm_msg(struct nes_cm_info *cm_info,
				struct iwpm_sa_data *pm_msg)
{
	nes_create_sockaddr(htonl(cm_info->loc_addr), htons(cm_info->loc_port),
				&pm_msg->loc_addr);
	nes_create_sockaddr(htonl(cm_info->rem_addr), htons(cm_info->rem_port),
				&pm_msg->rem_addr);
}

/*
 * nes_form_reg_msg - Form a port mapper message with dev info
 */
static void nes_form_reg_msg(struct nes_vnic *nesvnic,
			struct iwpm_dev_data *pm_msg)
{
	memcpy(pm_msg->dev_name, nesvnic->nesibdev->ibdev.name,
				IWPM_DEVNAME_SIZE);
	memcpy(pm_msg->if_name, nesvnic->netdev->name, IWPM_IFNAME_SIZE);
}

static void record_sockaddr_info(struct sockaddr_storage *addr_info,
					nes_addr_t *ip_addr, u16 *port_num)
{
	struct sockaddr_in *in_addr = (struct sockaddr_in *)addr_info;

	if (in_addr->sin_family == AF_INET) {
		*ip_addr = ntohl(in_addr->sin_addr.s_addr);
		*port_num = ntohs(in_addr->sin_port);
	}
}

/*
 * nes_record_pm_msg - Save the received mapping info
 */
static void nes_record_pm_msg(struct nes_cm_info *cm_info,
			struct iwpm_sa_data *pm_msg)
{
	record_sockaddr_info(&pm_msg->mapped_loc_addr,
		&cm_info->mapped_loc_addr, &cm_info->mapped_loc_port);

	record_sockaddr_info(&pm_msg->mapped_rem_addr,
		&cm_info->mapped_rem_addr, &cm_info->mapped_rem_port);
}

/*
 * nes_get_reminfo - Get the address info of the remote connecting peer
 */
static int nes_get_remote_addr(struct nes_cm_node *cm_node)
{
	struct sockaddr_storage mapped_loc_addr, mapped_rem_addr;
	struct sockaddr_storage remote_addr;
	int ret;

	nes_create_sockaddr(htonl(cm_node->mapped_loc_addr),
			htons(cm_node->mapped_loc_port), &mapped_loc_addr);
	nes_create_sockaddr(htonl(cm_node->mapped_rem_addr),
			htons(cm_node->mapped_rem_port), &mapped_rem_addr);

	ret = iwpm_get_remote_info(&mapped_loc_addr, &mapped_rem_addr,
				&remote_addr, RDMA_NL_NES);
	if (ret)
		nes_debug(NES_DBG_CM, "Unable to find remote peer address info\n");
	else
		record_sockaddr_info(&remote_addr, &cm_node->rem_addr,
				&cm_node->rem_port);
	return ret;
}

/**
 * print_core - dump a cm core
 */
static void print_core(struct nes_cm_core *core)
{
	nes_debug(NES_DBG_CM, "---------------------------------------------\n");
	nes_debug(NES_DBG_CM, "CM Core  -- (core = %p )\n", core);
	if (!core)
		return;
	nes_debug(NES_DBG_CM, "---------------------------------------------\n");

	nes_debug(NES_DBG_CM, "State         : %u \n", core->state);

	nes_debug(NES_DBG_CM, "Listen Nodes  : %u \n", atomic_read(&core->listen_node_cnt));
	nes_debug(NES_DBG_CM, "Active Nodes  : %u \n", atomic_read(&core->node_cnt));

	nes_debug(NES_DBG_CM, "core          : %p \n", core);

	nes_debug(NES_DBG_CM, "-------------- end core ---------------\n");
}

static void record_ird_ord(struct nes_cm_node *cm_node,
					u16 conn_ird, u16 conn_ord)
{
	if (conn_ird > NES_MAX_IRD)
		conn_ird = NES_MAX_IRD;

	if (conn_ord > NES_MAX_ORD)
		conn_ord = NES_MAX_ORD;

	cm_node->ird_size = conn_ird;
	cm_node->ord_size = conn_ord;
}

/**
 * cm_build_mpa_frame - build a MPA V1 frame or MPA V2 frame
 */
static int cm_build_mpa_frame(struct nes_cm_node *cm_node, u8 **start_buff,
			      u16 *buff_len, u8 *pci_mem, u8 mpa_key)
{
	int ret = 0;

	*start_buff = (pci_mem) ? pci_mem : &cm_node->mpa_frame_buf[0];

	switch (cm_node->mpa_frame_rev) {
	case IETF_MPA_V1:
		*start_buff = (u8 *)*start_buff + sizeof(struct ietf_rtr_msg);
		*buff_len = sizeof(struct ietf_mpa_v1) + cm_node->mpa_frame_size;
		build_mpa_v1(cm_node, *start_buff, mpa_key);
		break;
	case IETF_MPA_V2:
		*buff_len = sizeof(struct ietf_mpa_v2) + cm_node->mpa_frame_size;
		build_mpa_v2(cm_node, *start_buff, mpa_key);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

/**
 * build_mpa_v2 - build a MPA V2 frame
 */
static void build_mpa_v2(struct nes_cm_node *cm_node,
			 void *start_addr, u8 mpa_key)
{
	struct ietf_mpa_v2 *mpa_frame = (struct ietf_mpa_v2 *)start_addr;
	struct ietf_rtr_msg *rtr_msg = &mpa_frame->rtr_msg;
	u16 ctrl_ird;
	u16 ctrl_ord;

	/* initialize the upper 5 bytes of the frame */
	build_mpa_v1(cm_node, start_addr, mpa_key);
	mpa_frame->flags |= IETF_MPA_V2_FLAG; /* set a bit to indicate MPA V2 */
	mpa_frame->priv_data_len += htons(IETF_RTR_MSG_SIZE);

	/* initialize RTR msg */
	if (cm_node->mpav2_ird_ord == IETF_NO_IRD_ORD) {
		ctrl_ird = IETF_NO_IRD_ORD;
		ctrl_ord = IETF_NO_IRD_ORD;
	} else {
		ctrl_ird = cm_node->ird_size & IETF_NO_IRD_ORD;
		ctrl_ord = cm_node->ord_size & IETF_NO_IRD_ORD;
	}
	ctrl_ird |= IETF_PEER_TO_PEER;
	ctrl_ird |= IETF_FLPDU_ZERO_LEN;

	switch (mpa_key) {
	case MPA_KEY_REQUEST:
		ctrl_ord |= IETF_RDMA0_WRITE;
		ctrl_ord |= IETF_RDMA0_READ;
		break;
	case MPA_KEY_REPLY:
		switch (cm_node->send_rdma0_op) {
		case SEND_RDMA_WRITE_ZERO:
			ctrl_ord |= IETF_RDMA0_WRITE;
			break;
		case SEND_RDMA_READ_ZERO:
			ctrl_ord |= IETF_RDMA0_READ;
			break;
		}
	}
	rtr_msg->ctrl_ird = htons(ctrl_ird);
	rtr_msg->ctrl_ord = htons(ctrl_ord);
}

/**
 * build_mpa_v1 - build a MPA V1 frame
 */
static void build_mpa_v1(struct nes_cm_node *cm_node, void *start_addr, u8 mpa_key)
{
	struct ietf_mpa_v1 *mpa_frame = (struct ietf_mpa_v1 *)start_addr;

	switch (mpa_key) {
	case MPA_KEY_REQUEST:
		memcpy(mpa_frame->key, IEFT_MPA_KEY_REQ, IETF_MPA_KEY_SIZE);
		break;
	case MPA_KEY_REPLY:
		memcpy(mpa_frame->key, IEFT_MPA_KEY_REP, IETF_MPA_KEY_SIZE);
		break;
	}
	mpa_frame->flags = IETF_MPA_FLAGS_CRC;
	mpa_frame->rev = cm_node->mpa_frame_rev;
	mpa_frame->priv_data_len = htons(cm_node->mpa_frame_size);
}

static void build_rdma0_msg(struct nes_cm_node *cm_node, struct nes_qp **nesqp_addr)
{
	u64 u64temp;
	struct nes_qp *nesqp = *nesqp_addr;
	struct nes_hw_qp_wqe *wqe = &nesqp->hwqp.sq_vbase[0];

	u64temp = (unsigned long)nesqp->nesuqp_addr;
	u64temp |= NES_SW_CONTEXT_ALIGN >> 1;
	set_wqe_64bit_value(wqe->wqe_words, NES_IWARP_SQ_WQE_COMP_CTX_LOW_IDX, u64temp);

	wqe->wqe_words[NES_IWARP_SQ_WQE_FRAG0_LOW_IDX] = 0;
	wqe->wqe_words[NES_IWARP_SQ_WQE_FRAG0_HIGH_IDX] = 0;

	switch (cm_node->send_rdma0_op) {
	case SEND_RDMA_WRITE_ZERO:
		nes_debug(NES_DBG_CM, "Sending first write.\n");
		wqe->wqe_words[NES_IWARP_SQ_WQE_MISC_IDX] =
			cpu_to_le32(NES_IWARP_SQ_OP_RDMAW);
		wqe->wqe_words[NES_IWARP_SQ_WQE_TOTAL_PAYLOAD_IDX] = 0;
		wqe->wqe_words[NES_IWARP_SQ_WQE_LENGTH0_IDX] = 0;
		wqe->wqe_words[NES_IWARP_SQ_WQE_STAG0_IDX] = 0;
		break;

	case SEND_RDMA_READ_ZERO:
	default:
		if (cm_node->send_rdma0_op != SEND_RDMA_READ_ZERO)
			WARN(1, "Unsupported RDMA0 len operation=%u\n",
			     cm_node->send_rdma0_op);
		nes_debug(NES_DBG_CM, "Sending first rdma operation.\n");
		wqe->wqe_words[NES_IWARP_SQ_WQE_MISC_IDX] =
			cpu_to_le32(NES_IWARP_SQ_OP_RDMAR);
		wqe->wqe_words[NES_IWARP_SQ_WQE_RDMA_TO_LOW_IDX] = 1;
		wqe->wqe_words[NES_IWARP_SQ_WQE_RDMA_TO_HIGH_IDX] = 0;
		wqe->wqe_words[NES_IWARP_SQ_WQE_RDMA_LENGTH_IDX] = 0;
		wqe->wqe_words[NES_IWARP_SQ_WQE_RDMA_STAG_IDX] = 1;
		wqe->wqe_words[NES_IWARP_SQ_WQE_STAG0_IDX] = 1;
		break;
	}

	if (nesqp->sq_kmapped) {
		nesqp->sq_kmapped = 0;
		kunmap(nesqp->page);
	}

	/*use the reserved spot on the WQ for the extra first WQE*/
	nesqp->nesqp_context->ird_ord_sizes &= cpu_to_le32(~(NES_QPCONTEXT_ORDIRD_LSMM_PRESENT |
							     NES_QPCONTEXT_ORDIRD_WRPDU |
							     NES_QPCONTEXT_ORDIRD_ALSMM));
	nesqp->skip_lsmm = 1;
	nesqp->hwqp.sq_tail = 0;
}

/**
 * schedule_nes_timer
 * note - cm_node needs to be protected before calling this. Encase in:
 *			rem_ref_cm_node(cm_core, cm_node);add_ref_cm_node(cm_node);
 */
int schedule_nes_timer(struct nes_cm_node *cm_node, struct sk_buff *skb,
		       enum nes_timer_type type, int send_retrans,
		       int close_when_complete)
{
	unsigned long flags;
	struct nes_cm_core *cm_core = cm_node->cm_core;
	struct nes_timer_entry *new_send;
	int ret = 0;

	new_send = kzalloc(sizeof(*new_send), GFP_ATOMIC);
	if (!new_send)
		return -ENOMEM;

	/* new_send->timetosend = currenttime */
	new_send->retrycount = NES_DEFAULT_RETRYS;
	new_send->retranscount = NES_DEFAULT_RETRANS;
	new_send->skb = skb;
	new_send->timetosend = jiffies;
	new_send->type = type;
	new_send->netdev = cm_node->netdev;
	new_send->send_retrans = send_retrans;
	new_send->close_when_complete = close_when_complete;

	if (type == NES_TIMER_TYPE_CLOSE) {
		new_send->timetosend += (HZ / 10);
		if (cm_node->recv_entry) {
			kfree(new_send);
			WARN_ON(1);
			return -EINVAL;
		}
		cm_node->recv_entry = new_send;
	}

	if (type == NES_TIMER_TYPE_SEND) {
		new_send->seq_num = ntohl(tcp_hdr(skb)->seq);
		atomic_inc(&new_send->skb->users);
		spin_lock_irqsave(&cm_node->retrans_list_lock, flags);
		cm_node->send_entry = new_send;
		add_ref_cm_node(cm_node);
		spin_unlock_irqrestore(&cm_node->retrans_list_lock, flags);
		new_send->timetosend = jiffies + NES_RETRY_TIMEOUT;

		ret = nes_nic_cm_xmit(new_send->skb, cm_node->netdev);
		if (ret != NETDEV_TX_OK) {
			nes_debug(NES_DBG_CM, "Error sending packet %p "
				  "(jiffies = %lu)\n", new_send, jiffies);
			new_send->timetosend = jiffies;
			ret = NETDEV_TX_OK;
		} else {
			cm_packets_sent++;
			if (!send_retrans) {
				cleanup_retrans_entry(cm_node);
				if (close_when_complete)
					rem_ref_cm_node(cm_core, cm_node);
				return ret;
			}
		}
	}

	if (!timer_pending(&cm_core->tcp_timer))
		mod_timer(&cm_core->tcp_timer, new_send->timetosend);

	return ret;
}

static void nes_retrans_expired(struct nes_cm_node *cm_node)
{
	struct iw_cm_id *cm_id = cm_node->cm_id;
	enum nes_cm_node_state state = cm_node->state;
	cm_node->state = NES_CM_STATE_CLOSED;

	switch (state) {
	case NES_CM_STATE_SYN_RCVD:
	case NES_CM_STATE_CLOSING:
		rem_ref_cm_node(cm_node->cm_core, cm_node);
		break;
	case NES_CM_STATE_LAST_ACK:
	case NES_CM_STATE_FIN_WAIT1:
		if (cm_node->cm_id)
			cm_id->rem_ref(cm_id);
		send_reset(cm_node, NULL);
		break;
	default:
		add_ref_cm_node(cm_node);
		send_reset(cm_node, NULL);
		create_event(cm_node, NES_CM_EVENT_ABORTED);
	}
}

static void handle_recv_entry(struct nes_cm_node *cm_node, u32 rem_node)
{
	struct nes_timer_entry *recv_entry = cm_node->recv_entry;
	struct iw_cm_id *cm_id = cm_node->cm_id;
	struct nes_qp *nesqp;
	unsigned long qplockflags;

	if (!recv_entry)
		return;
	nesqp = (struct nes_qp *)recv_entry->skb;
	if (nesqp) {
		spin_lock_irqsave(&nesqp->lock, qplockflags);
		if (nesqp->cm_id) {
			nes_debug(NES_DBG_CM, "QP%u: cm_id = %p, "
				  "refcount = %d: HIT A "
				  "NES_TIMER_TYPE_CLOSE with something "
				  "to do!!!\n", nesqp->hwqp.qp_id, cm_id,
				  atomic_read(&nesqp->refcount));
			nesqp->hw_tcp_state = NES_AEQE_TCP_STATE_CLOSED;
			nesqp->last_aeq = NES_AEQE_AEID_RESET_SENT;
			nesqp->ibqp_state = IB_QPS_ERR;
			spin_unlock_irqrestore(&nesqp->lock, qplockflags);
			nes_cm_disconn(nesqp);
		} else {
			spin_unlock_irqrestore(&nesqp->lock, qplockflags);
			nes_debug(NES_DBG_CM, "QP%u: cm_id = %p, "
				  "refcount = %d: HIT A "
				  "NES_TIMER_TYPE_CLOSE with nothing "
				  "to do!!!\n", nesqp->hwqp.qp_id, cm_id,
				  atomic_read(&nesqp->refcount));
		}
	} else if (rem_node) {
		/* TIME_WAIT state */
		rem_ref_cm_node(cm_node->cm_core, cm_node);
	}
	if (cm_node->cm_id)
		cm_id->rem_ref(cm_id);
	kfree(recv_entry);
	cm_node->recv_entry = NULL;
}

/**
 * nes_cm_timer_tick
 */
static void nes_cm_timer_tick(unsigned long pass)
{
	unsigned long flags;
	unsigned long nexttimeout = jiffies + NES_LONG_TIME;
	struct nes_cm_node *cm_node;
	struct nes_timer_entry *send_entry, *recv_entry;
	struct list_head *list_core_temp;
	struct list_head *list_node;
	struct nes_cm_core *cm_core = g_cm_core;
	u32 settimer = 0;
	unsigned long timetosend;
	int ret = NETDEV_TX_OK;

	struct list_head timer_list;

	INIT_LIST_HEAD(&timer_list);
	spin_lock_irqsave(&cm_core->ht_lock, flags);

	list_for_each_safe(list_node, list_core_temp,
			   &cm_core->connected_nodes) {
		cm_node = container_of(list_node, struct nes_cm_node, list);
		if ((cm_node->recv_entry) || (cm_node->send_entry)) {
			add_ref_cm_node(cm_node);
			list_add(&cm_node->timer_entry, &timer_list);
		}
	}
	spin_unlock_irqrestore(&cm_core->ht_lock, flags);

	list_for_each_safe(list_node, list_core_temp, &timer_list) {
		cm_node = container_of(list_node, struct nes_cm_node,
				       timer_entry);
		recv_entry = cm_node->recv_entry;

		if (recv_entry) {
			if (time_after(recv_entry->timetosend, jiffies)) {
				if (nexttimeout > recv_entry->timetosend ||
				    !settimer) {
					nexttimeout = recv_entry->timetosend;
					settimer = 1;
				}
			} else {
				handle_recv_entry(cm_node, 1);
			}
		}

		spin_lock_irqsave(&cm_node->retrans_list_lock, flags);
		do {
			send_entry = cm_node->send_entry;
			if (!send_entry)
				break;
			if (time_after(send_entry->timetosend, jiffies)) {
				if (cm_node->state != NES_CM_STATE_TSA) {
					if ((nexttimeout >
					     send_entry->timetosend) ||
					    !settimer) {
						nexttimeout =
							send_entry->timetosend;
						settimer = 1;
					}
				} else {
					free_retrans_entry(cm_node);
				}
				break;
			}

			if ((cm_node->state == NES_CM_STATE_TSA) ||
			    (cm_node->state == NES_CM_STATE_CLOSED)) {
				free_retrans_entry(cm_node);
				break;
			}

			if (!send_entry->retranscount ||
			    !send_entry->retrycount) {
				cm_packets_dropped++;
				free_retrans_entry(cm_node);

				spin_unlock_irqrestore(
					&cm_node->retrans_list_lock, flags);
				nes_retrans_expired(cm_node);
				cm_node->state = NES_CM_STATE_CLOSED;
				spin_lock_irqsave(&cm_node->retrans_list_lock,
						  flags);
				break;
			}
			atomic_inc(&send_entry->skb->users);
			cm_packets_retrans++;
			nes_debug(NES_DBG_CM, "Retransmitting send_entry %p "
				  "for node %p, jiffies = %lu, time to send = "
				  "%lu, retranscount = %u, send_entry->seq_num = "
				  "0x%08X, cm_node->tcp_cntxt.rem_ack_num = "
				  "0x%08X\n", send_entry, cm_node, jiffies,
				  send_entry->timetosend,
				  send_entry->retranscount,
				  send_entry->seq_num,
				  cm_node->tcp_cntxt.rem_ack_num);

			spin_unlock_irqrestore(&cm_node->retrans_list_lock,
					       flags);
			ret = nes_nic_cm_xmit(send_entry->skb, cm_node->netdev);
			spin_lock_irqsave(&cm_node->retrans_list_lock, flags);
			if (ret != NETDEV_TX_OK) {
				nes_debug(NES_DBG_CM, "rexmit failed for "
					  "node=%p\n", cm_node);
				cm_packets_bounced++;
				send_entry->retrycount--;
				nexttimeout = jiffies + NES_SHORT_TIME;
				settimer = 1;
				break;
			} else {
				cm_packets_sent++;
			}
			nes_debug(NES_DBG_CM, "Packet Sent: retrans count = "
				  "%u, retry count = %u.\n",
				  send_entry->retranscount,
				  send_entry->retrycount);
			if (send_entry->send_retrans) {
				send_entry->retranscount--;
				timetosend = (NES_RETRY_TIMEOUT <<
					      (NES_DEFAULT_RETRANS - send_entry->retranscount));

				send_entry->timetosend = jiffies +
							 min(timetosend, NES_MAX_TIMEOUT);
				if (nexttimeout > send_entry->timetosend ||
				    !settimer) {
					nexttimeout = send_entry->timetosend;
					settimer = 1;
				}
			} else {
				int close_when_complete;
				close_when_complete =
					send_entry->close_when_complete;
				nes_debug(NES_DBG_CM, "cm_node=%p state=%d\n",
					  cm_node, cm_node->state);
				free_retrans_entry(cm_node);
				if (close_when_complete)
					rem_ref_cm_node(cm_node->cm_core,
							cm_node);
			}
		} while (0);

		spin_unlock_irqrestore(&cm_node->retrans_list_lock, flags);
		rem_ref_cm_node(cm_node->cm_core, cm_node);
	}

	if (settimer) {
		if (!timer_pending(&cm_core->tcp_timer))
			mod_timer(&cm_core->tcp_timer, nexttimeout);
	}
}


/**
 * send_syn
 */
static int send_syn(struct nes_cm_node *cm_node, u32 sendack,
		    struct sk_buff *skb)
{
	int ret;
	int flags = SET_SYN;
	char optionsbuffer[sizeof(struct option_mss) +
			   sizeof(struct option_windowscale) + sizeof(struct option_base) +
			   TCP_OPTIONS_PADDING];

	int optionssize = 0;
	/* Sending MSS option */
	union all_known_options *options;

	if (!cm_node)
		return -EINVAL;

	options = (union all_known_options *)&optionsbuffer[optionssize];
	options->as_mss.optionnum = OPTION_NUMBER_MSS;
	options->as_mss.length = sizeof(struct option_mss);
	options->as_mss.mss = htons(cm_node->tcp_cntxt.mss);
	optionssize += sizeof(struct option_mss);

	options = (union all_known_options *)&optionsbuffer[optionssize];
	options->as_windowscale.optionnum = OPTION_NUMBER_WINDOW_SCALE;
	options->as_windowscale.length = sizeof(struct option_windowscale);
	options->as_windowscale.shiftcount = cm_node->tcp_cntxt.rcv_wscale;
	optionssize += sizeof(struct option_windowscale);

	if (sendack && !(NES_DRV_OPT_SUPRESS_OPTION_BC & nes_drv_opt)) {
		options = (union all_known_options *)&optionsbuffer[optionssize];
		options->as_base.optionnum = OPTION_NUMBER_WRITE0;
		options->as_base.length = sizeof(struct option_base);
		optionssize += sizeof(struct option_base);
		/* we need the size to be a multiple of 4 */
		options = (union all_known_options *)&optionsbuffer[optionssize];
		options->as_end = 1;
		optionssize += 1;
		options = (union all_known_options *)&optionsbuffer[optionssize];
		options->as_end = 1;
		optionssize += 1;
	}

	options = (union all_known_options *)&optionsbuffer[optionssize];
	options->as_end = OPTION_NUMBER_END;
	optionssize += 1;

	if (!skb)
		skb = dev_alloc_skb(MAX_CM_BUFFER);
	if (!skb) {
		nes_debug(NES_DBG_CM, "Failed to get a Free pkt\n");
		return -1;
	}

	if (sendack)
		flags |= SET_ACK;

	form_cm_frame(skb, cm_node, optionsbuffer, optionssize, NULL, 0, flags);
	ret = schedule_nes_timer(cm_node, skb, NES_TIMER_TYPE_SEND, 1, 0);

	return ret;
}


/**
 * send_reset
 */
static int send_reset(struct nes_cm_node *cm_node, struct sk_buff *skb)
{
	int ret;
	int flags = SET_RST | SET_ACK;

	if (!skb)
		skb = dev_alloc_skb(MAX_CM_BUFFER);
	if (!skb) {
		nes_debug(NES_DBG_CM, "Failed to get a Free pkt\n");
		return -ENOMEM;
	}

	form_cm_frame(skb, cm_node, NULL, 0, NULL, 0, flags);
	ret = schedule_nes_timer(cm_node, skb, NES_TIMER_TYPE_SEND, 0, 1);

	return ret;
}


/**
 * send_ack
 */
static int send_ack(struct nes_cm_node *cm_node, struct sk_buff *skb)
{
	int ret;

	if (!skb)
		skb = dev_alloc_skb(MAX_CM_BUFFER);

	if (!skb) {
		nes_debug(NES_DBG_CM, "Failed to get a Free pkt\n");
		return -1;
	}

	form_cm_frame(skb, cm_node, NULL, 0, NULL, 0, SET_ACK);
	ret = schedule_nes_timer(cm_node, skb, NES_TIMER_TYPE_SEND, 0, 0);

	return ret;
}


/**
 * send_fin
 */
static int send_fin(struct nes_cm_node *cm_node, struct sk_buff *skb)
{
	int ret;

	/* if we didn't get a frame get one */
	if (!skb)
		skb = dev_alloc_skb(MAX_CM_BUFFER);

	if (!skb) {
		nes_debug(NES_DBG_CM, "Failed to get a Free pkt\n");
		return -1;
	}

	form_cm_frame(skb, cm_node, NULL, 0, NULL, 0, SET_ACK | SET_FIN);
	ret = schedule_nes_timer(cm_node, skb, NES_TIMER_TYPE_SEND, 1, 0);

	return ret;
}


/**
 * find_node - find a cm node that matches the reference cm node
 */
static struct nes_cm_node *find_node(struct nes_cm_core *cm_core,
				     u16 rem_port, nes_addr_t rem_addr, u16 loc_port, nes_addr_t loc_addr)
{
	unsigned long flags;
	struct list_head *hte;
	struct nes_cm_node *cm_node;

	/* get a handle on the hte */
	hte = &cm_core->connected_nodes;

	/* walk list and find cm_node associated with this session ID */
	spin_lock_irqsave(&cm_core->ht_lock, flags);
	list_for_each_entry(cm_node, hte, list) {
		/* compare quad, return node handle if a match */
		nes_debug(NES_DBG_CM, "finding node %x:%x =? %x:%x ^ %x:%x =? %x:%x\n",
			  cm_node->loc_addr, cm_node->loc_port,
			  loc_addr, loc_port,
			  cm_node->rem_addr, cm_node->rem_port,
			  rem_addr, rem_port);
		if ((cm_node->mapped_loc_addr == loc_addr) &&
			(cm_node->mapped_loc_port == loc_port) &&
			(cm_node->mapped_rem_addr == rem_addr) &&
			(cm_node->mapped_rem_port == rem_port)) {

			add_ref_cm_node(cm_node);
			spin_unlock_irqrestore(&cm_core->ht_lock, flags);
			return cm_node;
		}
	}
	spin_unlock_irqrestore(&cm_core->ht_lock, flags);

	/* no owner node */
	return NULL;
}


/**
 * find_listener - find a cm node listening on this addr-port pair
 */
static struct nes_cm_listener *find_listener(struct nes_cm_core *cm_core,
					nes_addr_t dst_addr, u16 dst_port,
					enum nes_cm_listener_state listener_state, int local)
{
	unsigned long flags;
	struct nes_cm_listener *listen_node;
	nes_addr_t listen_addr;
	u16 listen_port;

	/* walk list and find cm_node associated with this session ID */
	spin_lock_irqsave(&cm_core->listen_list_lock, flags);
	list_for_each_entry(listen_node, &cm_core->listen_list.list, list) {
		if (local) {
			listen_addr = listen_node->loc_addr;
			listen_port = listen_node->loc_port;
		} else {
			listen_addr = listen_node->mapped_loc_addr;
			listen_port = listen_node->mapped_loc_port;
		}
		/* compare node pair, return node handle if a match */
		if (((listen_addr == dst_addr) ||
		     listen_addr == 0x00000000) &&
		    (listen_port == dst_port) &&
		    (listener_state & listen_node->listener_state)) {
			atomic_inc(&listen_node->ref_count);
			spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);
			return listen_node;
		}
	}
	spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);

	/* no listener */
	return NULL;
}

/**
 * add_hte_node - add a cm node to the hash table
 */
static int add_hte_node(struct nes_cm_core *cm_core, struct nes_cm_node *cm_node)
{
	unsigned long flags;
	struct list_head *hte;

	if (!cm_node || !cm_core)
		return -EINVAL;

	nes_debug(NES_DBG_CM, "Adding Node %p to Active Connection HT\n",
		  cm_node);

	spin_lock_irqsave(&cm_core->ht_lock, flags);

	/* get a handle on the hash table element (list head for this slot) */
	hte = &cm_core->connected_nodes;
	list_add_tail(&cm_node->list, hte);
	atomic_inc(&cm_core->ht_node_cnt);

	spin_unlock_irqrestore(&cm_core->ht_lock, flags);

	return 0;
}


/**
 * mini_cm_dec_refcnt_listen
 */
static int mini_cm_dec_refcnt_listen(struct nes_cm_core *cm_core,
				     struct nes_cm_listener *listener, int free_hanging_nodes)
{
	int ret = -EINVAL;
	int err = 0;
	unsigned long flags;
	struct list_head *list_pos = NULL;
	struct list_head *list_temp = NULL;
	struct nes_cm_node *cm_node = NULL;
	struct list_head reset_list;

	nes_debug(NES_DBG_CM, "attempting listener= %p free_nodes= %d, "
		  "refcnt=%d\n", listener, free_hanging_nodes,
		  atomic_read(&listener->ref_count));
	/* free non-accelerated child nodes for this listener */
	INIT_LIST_HEAD(&reset_list);
	if (free_hanging_nodes) {
		spin_lock_irqsave(&cm_core->ht_lock, flags);
		list_for_each_safe(list_pos, list_temp,
				   &g_cm_core->connected_nodes) {
			cm_node = container_of(list_pos, struct nes_cm_node,
					       list);
			if ((cm_node->listener == listener) &&
			    (!cm_node->accelerated)) {
				add_ref_cm_node(cm_node);
				list_add(&cm_node->reset_entry, &reset_list);
			}
		}
		spin_unlock_irqrestore(&cm_core->ht_lock, flags);
	}

	list_for_each_safe(list_pos, list_temp, &reset_list) {
		cm_node = container_of(list_pos, struct nes_cm_node,
				       reset_entry);
		{
			struct nes_cm_node *loopback = cm_node->loopbackpartner;
			enum nes_cm_node_state old_state;
			if (NES_CM_STATE_FIN_WAIT1 <= cm_node->state) {
				rem_ref_cm_node(cm_node->cm_core, cm_node);
			} else {
				if (!loopback) {
					cleanup_retrans_entry(cm_node);
					err = send_reset(cm_node, NULL);
					if (err) {
						cm_node->state =
							NES_CM_STATE_CLOSED;
						WARN_ON(1);
					} else {
						old_state = cm_node->state;
						cm_node->state = NES_CM_STATE_LISTENER_DESTROYED;
						if (old_state != NES_CM_STATE_MPAREQ_RCVD)
							rem_ref_cm_node(
								cm_node->cm_core,
								cm_node);
					}
				} else {
					struct nes_cm_event event;

					event.cm_node = loopback;
					event.cm_info.rem_addr =
							loopback->rem_addr;
					event.cm_info.loc_addr =
							loopback->loc_addr;
					event.cm_info.rem_port =
							loopback->rem_port;
					event.cm_info.loc_port =
							 loopback->loc_port;
					event.cm_info.cm_id = loopback->cm_id;
					add_ref_cm_node(loopback);
					loopback->state = NES_CM_STATE_CLOSED;
					cm_event_connect_error(&event);
					cm_node->state = NES_CM_STATE_LISTENER_DESTROYED;

					rem_ref_cm_node(cm_node->cm_core,
							 cm_node);

				}
			}
		}
	}

	spin_lock_irqsave(&cm_core->listen_list_lock, flags);
	if (!atomic_dec_return(&listener->ref_count)) {
		list_del(&listener->list);

		/* decrement our listen node count */
		atomic_dec(&cm_core->listen_node_cnt);

		spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);

		if (listener->nesvnic) {
			nes_manage_apbvt(listener->nesvnic,
				listener->mapped_loc_port,
				PCI_FUNC(listener->nesvnic->nesdev->pcidev->devfn),
				NES_MANAGE_APBVT_DEL);

			nes_remove_mapinfo(listener->loc_addr,
					listener->loc_port,
					listener->mapped_loc_addr,
					listener->mapped_loc_port);
			nes_debug(NES_DBG_NLMSG,
					"Delete APBVT mapped_loc_port = %04X\n",
					listener->mapped_loc_port);
		}

		nes_debug(NES_DBG_CM, "destroying listener (%p)\n", listener);

		kfree(listener);
		listener = NULL;
		ret = 0;
		atomic_inc(&cm_listens_destroyed);
	} else {
		spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);
	}
	if (listener) {
		if (atomic_read(&listener->pend_accepts_cnt) > 0)
			nes_debug(NES_DBG_CM, "destroying listener (%p)"
				  " with non-zero pending accepts=%u\n",
				  listener, atomic_read(&listener->pend_accepts_cnt));
	}

	return ret;
}


/**
 * mini_cm_del_listen
 */
static int mini_cm_del_listen(struct nes_cm_core *cm_core,
			      struct nes_cm_listener *listener)
{
	listener->listener_state = NES_CM_LISTENER_PASSIVE_STATE;
	listener->cm_id = NULL; /* going to be destroyed pretty soon */
	return mini_cm_dec_refcnt_listen(cm_core, listener, 1);
}


/**
 * mini_cm_accelerated
 */
static inline int mini_cm_accelerated(struct nes_cm_core *cm_core,
				      struct nes_cm_node *cm_node)
{
	cm_node->accelerated = 1;

	if (cm_node->accept_pend) {
		BUG_ON(!cm_node->listener);
		atomic_dec(&cm_node->listener->pend_accepts_cnt);
		cm_node->accept_pend = 0;
		BUG_ON(atomic_read(&cm_node->listener->pend_accepts_cnt) < 0);
	}

	if (!timer_pending(&cm_core->tcp_timer))
		mod_timer(&cm_core->tcp_timer, (jiffies + NES_SHORT_TIME));

	return 0;
}


/**
 * nes_addr_resolve_neigh
 */
static int nes_addr_resolve_neigh(struct nes_vnic *nesvnic, u32 dst_ip, int arpindex)
{
	struct rtable *rt;
	struct neighbour *neigh;
	int rc = arpindex;
	struct net_device *netdev;
	struct nes_adapter *nesadapter = nesvnic->nesdev->nesadapter;

	rt = ip_route_output(&init_net, htonl(dst_ip), 0, 0, 0);
	if (IS_ERR(rt)) {
		printk(KERN_ERR "%s: ip_route_output_key failed for 0x%08X\n",
		       __func__, dst_ip);
		return rc;
	}

	if (netif_is_bond_slave(nesvnic->netdev))
		netdev = netdev_master_upper_dev_get(nesvnic->netdev);
	else
		netdev = nesvnic->netdev;

	neigh = neigh_lookup(&arp_tbl, &rt->rt_gateway, netdev);

	rcu_read_lock();
	if (neigh) {
		if (neigh->nud_state & NUD_VALID) {
			nes_debug(NES_DBG_CM, "Neighbor MAC address for 0x%08X"
				  " is %pM, Gateway is 0x%08X \n", dst_ip,
				  neigh->ha, ntohl(rt->rt_gateway));

			if (arpindex >= 0) {
				if (ether_addr_equal(nesadapter->arp_table[arpindex].mac_addr, neigh->ha)) {
					/* Mac address same as in nes_arp_table */
					goto out;
				}

				nes_manage_arp_cache(nesvnic->netdev,
						     nesadapter->arp_table[arpindex].mac_addr,
						     dst_ip, NES_ARP_DELETE);
			}

			nes_manage_arp_cache(nesvnic->netdev, neigh->ha,
					     dst_ip, NES_ARP_ADD);
			rc = nes_arp_table(nesvnic->nesdev, dst_ip, NULL,
					   NES_ARP_RESOLVE);
		} else {
			neigh_event_send(neigh, NULL);
		}
	}
out:
	rcu_read_unlock();

	if (neigh)
		neigh_release(neigh);

	ip_rt_put(rt);
	return rc;
}

/**
 * make_cm_node - create a new instance of a cm node
 */
static struct nes_cm_node *make_cm_node(struct nes_cm_core *cm_core,
					struct nes_vnic *nesvnic, struct nes_cm_info *cm_info,
					struct nes_cm_listener *listener)
{
	struct nes_cm_node *cm_node;
	struct timespec ts;
	int oldarpindex = 0;
	int arpindex = 0;
	struct nes_device *nesdev;
	struct nes_adapter *nesadapter;

	/* create an hte and cm_node for this instance */
	cm_node = kzalloc(sizeof(*cm_node), GFP_ATOMIC);
	if (!cm_node)
		return NULL;

	/* set our node specific transport info */
	if (listener) {
		cm_node->loc_addr = listener->loc_addr;
		cm_node->loc_port = listener->loc_port;
	} else {
		cm_node->loc_addr = cm_info->loc_addr;
		cm_node->loc_port = cm_info->loc_port;
	}
	cm_node->rem_addr = cm_info->rem_addr;
	cm_node->rem_port = cm_info->rem_port;

	cm_node->mapped_loc_addr = cm_info->mapped_loc_addr;
	cm_node->mapped_rem_addr = cm_info->mapped_rem_addr;
	cm_node->mapped_loc_port = cm_info->mapped_loc_port;
	cm_node->mapped_rem_port = cm_info->mapped_rem_port;

	cm_node->mpa_frame_rev = mpa_version;
	cm_node->send_rdma0_op = SEND_RDMA_READ_ZERO;
	cm_node->mpav2_ird_ord = 0;
	cm_node->ird_size = 0;
	cm_node->ord_size = 0;

	nes_debug(NES_DBG_CM, "Make node addresses : loc = %pI4:%x, rem = %pI4:%x\n",
		  &cm_node->loc_addr, cm_node->loc_port,
		  &cm_node->rem_addr, cm_node->rem_port);
	cm_node->listener = listener;
	if (listener)
		cm_node->tos = listener->tos;
	cm_node->netdev = nesvnic->netdev;
	cm_node->cm_id = cm_info->cm_id;
	memcpy(cm_node->loc_mac, nesvnic->netdev->dev_addr, ETH_ALEN);

	nes_debug(NES_DBG_CM, "listener=%p, cm_id=%p\n", cm_node->listener,
		  cm_node->cm_id);

	spin_lock_init(&cm_node->retrans_list_lock);

	cm_node->loopbackpartner = NULL;
	atomic_set(&cm_node->ref_count, 1);
	/* associate our parent CM core */
	cm_node->cm_core = cm_core;
	cm_node->tcp_cntxt.loc_id = NES_CM_DEF_LOCAL_ID;
	cm_node->tcp_cntxt.rcv_wscale = NES_CM_DEFAULT_RCV_WND_SCALE;
	cm_node->tcp_cntxt.rcv_wnd = NES_CM_DEFAULT_RCV_WND_SCALED >>
				     NES_CM_DEFAULT_RCV_WND_SCALE;
	ts = current_kernel_time();
	cm_node->tcp_cntxt.loc_seq_num = htonl(ts.tv_nsec);
	cm_node->tcp_cntxt.mss = nesvnic->max_frame_size - sizeof(struct iphdr) -
				 sizeof(struct tcphdr) - ETH_HLEN - VLAN_HLEN;
	cm_node->tcp_cntxt.rcv_nxt = 0;
	/* get a unique session ID , add thread_id to an upcounter to handle race */
	atomic_inc(&cm_core->node_cnt);
	cm_node->conn_type = cm_info->conn_type;
	cm_node->apbvt_set = 0;
	cm_node->accept_pend = 0;

	cm_node->nesvnic = nesvnic;
	/* get some device handles, for arp lookup */
	nesdev = nesvnic->nesdev;
	nesadapter = nesdev->nesadapter;

	cm_node->loopbackpartner = NULL;

	/* get the mac addr for the remote node */
	oldarpindex = nes_arp_table(nesdev, cm_node->mapped_rem_addr,
				NULL, NES_ARP_RESOLVE);
	arpindex = nes_addr_resolve_neigh(nesvnic,
				cm_node->mapped_rem_addr, oldarpindex);
	if (arpindex < 0) {
		kfree(cm_node);
		return NULL;
	}

	/* copy the mac addr to node context */
	memcpy(cm_node->rem_mac, nesadapter->arp_table[arpindex].mac_addr, ETH_ALEN);
	nes_debug(NES_DBG_CM, "Remote mac addr from arp table: %pM\n",
		  cm_node->rem_mac);

	add_hte_node(cm_core, cm_node);
	atomic_inc(&cm_nodes_created);

	return cm_node;
}


/**
 * add_ref_cm_node - destroy an instance of a cm node
 */
static int add_ref_cm_node(struct nes_cm_node *cm_node)
{
	atomic_inc(&cm_node->ref_count);
	return 0;
}


/**
 * rem_ref_cm_node - destroy an instance of a cm node
 */
static int rem_ref_cm_node(struct nes_cm_core *cm_core,
			   struct nes_cm_node *cm_node)
{
	unsigned long flags;
	struct nes_qp *nesqp;

	if (!cm_node)
		return -EINVAL;

	spin_lock_irqsave(&cm_node->cm_core->ht_lock, flags);
	if (atomic_dec_return(&cm_node->ref_count)) {
		spin_unlock_irqrestore(&cm_node->cm_core->ht_lock, flags);
		return 0;
	}
	list_del(&cm_node->list);
	atomic_dec(&cm_core->ht_node_cnt);
	spin_unlock_irqrestore(&cm_node->cm_core->ht_lock, flags);

	/* if the node is destroyed before connection was accelerated */
	if (!cm_node->accelerated && cm_node->accept_pend) {
		BUG_ON(!cm_node->listener);
		atomic_dec(&cm_node->listener->pend_accepts_cnt);
		BUG_ON(atomic_read(&cm_node->listener->pend_accepts_cnt) < 0);
	}
	WARN_ON(cm_node->send_entry);
	if (cm_node->recv_entry)
		handle_recv_entry(cm_node, 0);
	if (cm_node->listener) {
		mini_cm_dec_refcnt_listen(cm_core, cm_node->listener, 0);
	} else {
		if (cm_node->apbvt_set && cm_node->nesvnic) {
			nes_manage_apbvt(cm_node->nesvnic, cm_node->mapped_loc_port,
					 PCI_FUNC(cm_node->nesvnic->nesdev->pcidev->devfn),
					 NES_MANAGE_APBVT_DEL);
		}
		nes_debug(NES_DBG_NLMSG, "Delete APBVT mapped_loc_port = %04X\n",
					cm_node->mapped_loc_port);
		nes_remove_mapinfo(cm_node->loc_addr, cm_node->loc_port,
			cm_node->mapped_loc_addr, cm_node->mapped_loc_port);
	}

	atomic_dec(&cm_core->node_cnt);
	atomic_inc(&cm_nodes_destroyed);
	nesqp = cm_node->nesqp;
	if (nesqp) {
		nesqp->cm_node = NULL;
		nes_rem_ref(&nesqp->ibqp);
		cm_node->nesqp = NULL;
	}

	kfree(cm_node);
	return 0;
}

/**
 * process_options
 */
static int process_options(struct nes_cm_node *cm_node, u8 *optionsloc,
			   u32 optionsize, u32 syn_packet)
{
	u32 tmp;
	u32 offset = 0;
	union all_known_options *all_options;
	char got_mss_option = 0;

	while (offset < optionsize) {
		all_options = (union all_known_options *)(optionsloc + offset);
		switch (all_options->as_base.optionnum) {
		case OPTION_NUMBER_END:
			offset = optionsize;
			break;
		case OPTION_NUMBER_NONE:
			offset += 1;
			continue;
		case OPTION_NUMBER_MSS:
			nes_debug(NES_DBG_CM, "%s: MSS Length: %d Offset: %d "
				  "Size: %d\n", __func__,
				  all_options->as_mss.length, offset, optionsize);
			got_mss_option = 1;
			if (all_options->as_mss.length != 4) {
				return 1;
			} else {
				tmp = ntohs(all_options->as_mss.mss);
				if (tmp > 0 && tmp <
				    cm_node->tcp_cntxt.mss)
					cm_node->tcp_cntxt.mss = tmp;
			}
			break;
		case OPTION_NUMBER_WINDOW_SCALE:
			cm_node->tcp_cntxt.snd_wscale =
				all_options->as_windowscale.shiftcount;
			break;
		default:
			nes_debug(NES_DBG_CM, "TCP Option not understood: %x\n",
				  all_options->as_base.optionnum);
			break;
		}
		offset += all_options->as_base.length;
	}
	if ((!got_mss_option) && (syn_packet))
		cm_node->tcp_cntxt.mss = NES_CM_DEFAULT_MSS;
	return 0;
}

static void drop_packet(struct sk_buff *skb)
{
	atomic_inc(&cm_accel_dropped_pkts);
	dev_kfree_skb_any(skb);
}

static void handle_fin_pkt(struct nes_cm_node *cm_node)
{
	nes_debug(NES_DBG_CM, "Received FIN, cm_node = %p, state = %u. "
		  "refcnt=%d\n", cm_node, cm_node->state,
		  atomic_read(&cm_node->ref_count));
	switch (cm_node->state) {
	case NES_CM_STATE_SYN_RCVD:
	case NES_CM_STATE_SYN_SENT:
	case NES_CM_STATE_ESTABLISHED:
	case NES_CM_STATE_MPAREJ_RCVD:
		cm_node->tcp_cntxt.rcv_nxt++;
		cleanup_retrans_entry(cm_node);
		cm_node->state = NES_CM_STATE_LAST_ACK;
		send_fin(cm_node, NULL);
		break;
	case NES_CM_STATE_MPAREQ_SENT:
		create_event(cm_node, NES_CM_EVENT_ABORTED);
		cm_node->tcp_cntxt.rcv_nxt++;
		cleanup_retrans_entry(cm_node);
		cm_node->state = NES_CM_STATE_CLOSED;
		add_ref_cm_node(cm_node);
		send_reset(cm_node, NULL);
		break;
	case NES_CM_STATE_FIN_WAIT1:
		cm_node->tcp_cntxt.rcv_nxt++;
		cleanup_retrans_entry(cm_node);
		cm_node->state = NES_CM_STATE_CLOSING;
		send_ack(cm_node, NULL);
		/* Wait for ACK as this is simultaneous close..
		* After we receive ACK, do not send anything..
		* Just rm the node.. Done.. */
		break;
	case NES_CM_STATE_FIN_WAIT2:
		cm_node->tcp_cntxt.rcv_nxt++;
		cleanup_retrans_entry(cm_node);
		cm_node->state = NES_CM_STATE_TIME_WAIT;
		send_ack(cm_node, NULL);
		schedule_nes_timer(cm_node, NULL,  NES_TIMER_TYPE_CLOSE, 1, 0);
		break;
	case NES_CM_STATE_TIME_WAIT:
		cm_node->tcp_cntxt.rcv_nxt++;
		cleanup_retrans_entry(cm_node);
		cm_node->state = NES_CM_STATE_CLOSED;
		rem_ref_cm_node(cm_node->cm_core, cm_node);
		break;
	case NES_CM_STATE_TSA:
	default:
		nes_debug(NES_DBG_CM, "Error Rcvd FIN for node-%p state = %d\n",
			cm_node, cm_node->state);
		break;
	}
}


static void handle_rst_pkt(struct nes_cm_node *cm_node, struct sk_buff *skb,
	struct tcphdr *tcph)
{

	int	reset = 0;	/* whether to send reset in case of err.. */
	atomic_inc(&cm_resets_recvd);
	nes_debug(NES_DBG_CM, "Received Reset, cm_node = %p, state = %u."
			" refcnt=%d\n", cm_node, cm_node->state,
			atomic_read(&cm_node->ref_count));
	cleanup_retrans_entry(cm_node);
	switch (cm_node->state) {
	case NES_CM_STATE_SYN_SENT:
	case NES_CM_STATE_MPAREQ_SENT:
		nes_debug(NES_DBG_CM, "%s[%u] create abort for cm_node=%p "
			"listener=%p state=%d\n", __func__, __LINE__, cm_node,
			cm_node->listener, cm_node->state);
		switch (cm_node->mpa_frame_rev) {
		case IETF_MPA_V2:
			cm_node->mpa_frame_rev = IETF_MPA_V1;
			/* send a syn and goto syn sent state */
			cm_node->state = NES_CM_STATE_SYN_SENT;
			if (send_syn(cm_node, 0, NULL)) {
				active_open_err(cm_node, skb, reset);
			}
			break;
		case IETF_MPA_V1:
		default:
			active_open_err(cm_node, skb, reset);
			break;
		}
		break;
	case NES_CM_STATE_MPAREQ_RCVD:
		atomic_inc(&cm_node->passive_state);
		dev_kfree_skb_any(skb);
		break;
	case NES_CM_STATE_ESTABLISHED:
	case NES_CM_STATE_SYN_RCVD:
	case NES_CM_STATE_LISTENING:
		nes_debug(NES_DBG_CM, "Bad state %s[%u]\n", __func__, __LINE__);
		passive_open_err(cm_node, skb, reset);
		break;
	case NES_CM_STATE_TSA:
		active_open_err(cm_node, skb, reset);
		break;
	case NES_CM_STATE_CLOSED:
		drop_packet(skb);
		break;
	case NES_CM_STATE_FIN_WAIT2:
	case NES_CM_STATE_FIN_WAIT1:
	case NES_CM_STATE_LAST_ACK:
		cm_node->cm_id->rem_ref(cm_node->cm_id);
	case NES_CM_STATE_TIME_WAIT:
		cm_node->state = NES_CM_STATE_CLOSED;
		rem_ref_cm_node(cm_node->cm_core, cm_node);
		drop_packet(skb);
		break;
	default:
		drop_packet(skb);
		break;
	}
}


static void handle_rcv_mpa(struct nes_cm_node *cm_node, struct sk_buff *skb)
{
	int ret = 0;
	int datasize = skb->len;
	u8 *dataloc = skb->data;

	enum nes_cm_event_type type = NES_CM_EVENT_UNKNOWN;
	u32 res_type;

	ret = parse_mpa(cm_node, dataloc, &res_type, datasize);
	if (ret) {
		nes_debug(NES_DBG_CM, "didn't like MPA Request\n");
		if (cm_node->state == NES_CM_STATE_MPAREQ_SENT) {
			nes_debug(NES_DBG_CM, "%s[%u] create abort for "
				  "cm_node=%p listener=%p state=%d\n", __func__,
				  __LINE__, cm_node, cm_node->listener,
				  cm_node->state);
			active_open_err(cm_node, skb, 1);
		} else {
			passive_open_err(cm_node, skb, 1);
		}
		return;
	}

	switch (cm_node->state) {
	case NES_CM_STATE_ESTABLISHED:
		if (res_type == NES_MPA_REQUEST_REJECT)
			/*BIG problem as we are receiving the MPA.. So should
			 * not be REJECT.. This is Passive Open.. We can
			 * only receive it Reject for Active Open...*/
			WARN_ON(1);
		cm_node->state = NES_CM_STATE_MPAREQ_RCVD;
		type = NES_CM_EVENT_MPA_REQ;
		atomic_set(&cm_node->passive_state,
			   NES_PASSIVE_STATE_INDICATED);
		break;
	case NES_CM_STATE_MPAREQ_SENT:
		cleanup_retrans_entry(cm_node);
		if (res_type == NES_MPA_REQUEST_REJECT) {
			type = NES_CM_EVENT_MPA_REJECT;
			cm_node->state = NES_CM_STATE_MPAREJ_RCVD;
		} else {
			type = NES_CM_EVENT_CONNECTED;
			cm_node->state = NES_CM_STATE_TSA;
		}

		break;
	default:
		WARN_ON(1);
		break;
	}
	dev_kfree_skb_any(skb);
	create_event(cm_node, type);
}

static void indicate_pkt_err(struct nes_cm_node *cm_node, struct sk_buff *skb)
{
	switch (cm_node->state) {
	case NES_CM_STATE_SYN_SENT:
	case NES_CM_STATE_MPAREQ_SENT:
		nes_debug(NES_DBG_CM, "%s[%u] create abort for cm_node=%p "
			  "listener=%p state=%d\n", __func__, __LINE__, cm_node,
			  cm_node->listener, cm_node->state);
		active_open_err(cm_node, skb, 1);
		break;
	case NES_CM_STATE_ESTABLISHED:
	case NES_CM_STATE_SYN_RCVD:
		passive_open_err(cm_node, skb, 1);
		break;
	case NES_CM_STATE_TSA:
	default:
		drop_packet(skb);
	}
}

static int check_syn(struct nes_cm_node *cm_node, struct tcphdr *tcph,
		     struct sk_buff *skb)
{
	int err;

	err = ((ntohl(tcph->ack_seq) == cm_node->tcp_cntxt.loc_seq_num)) ? 0 : 1;
	if (err)
		active_open_err(cm_node, skb, 1);

	return err;
}

static int check_seq(struct nes_cm_node *cm_node, struct tcphdr *tcph,
		     struct sk_buff *skb)
{
	int err = 0;
	u32 seq;
	u32 ack_seq;
	u32 loc_seq_num = cm_node->tcp_cntxt.loc_seq_num;
	u32 rcv_nxt = cm_node->tcp_cntxt.rcv_nxt;
	u32 rcv_wnd;

	seq = ntohl(tcph->seq);
	ack_seq = ntohl(tcph->ack_seq);
	rcv_wnd = cm_node->tcp_cntxt.rcv_wnd;
	if (ack_seq != loc_seq_num)
		err = 1;
	else if (!between(seq, rcv_nxt, (rcv_nxt + rcv_wnd)))
		err = 1;
	if (err) {
		nes_debug(NES_DBG_CM, "%s[%u] create abort for cm_node=%p "
			  "listener=%p state=%d\n", __func__, __LINE__, cm_node,
			  cm_node->listener, cm_node->state);
		indicate_pkt_err(cm_node, skb);
		nes_debug(NES_DBG_CM, "seq ERROR cm_node =%p seq=0x%08X "
			  "rcv_nxt=0x%08X rcv_wnd=0x%x\n", cm_node, seq, rcv_nxt,
			  rcv_wnd);
	}
	return err;
}

/*
 * handle_syn_pkt() is for Passive node. The syn packet is received when a node
 * is created with a listener or it may comein as rexmitted packet which in
 * that case will be just dropped.
 */
static void handle_syn_pkt(struct nes_cm_node *cm_node, struct sk_buff *skb,
			   struct tcphdr *tcph)
{
	int ret;
	u32 inc_sequence;
	int optionsize;

	optionsize = (tcph->doff << 2) - sizeof(struct tcphdr);
	skb_trim(skb, 0);
	inc_sequence = ntohl(tcph->seq);

	switch (cm_node->state) {
	case NES_CM_STATE_SYN_SENT:
	case NES_CM_STATE_MPAREQ_SENT:
		/* Rcvd syn on active open connection*/
		active_open_err(cm_node, skb, 1);
		break;
	case NES_CM_STATE_LISTENING:
		/* Passive OPEN */
		if (atomic_read(&cm_node->listener->pend_accepts_cnt) >
		    cm_node->listener->backlog) {
			nes_debug(NES_DBG_CM, "drop syn due to backlog "
				  "pressure \n");
			cm_backlog_drops++;
			passive_open_err(cm_node, skb, 0);
			break;
		}
		ret = handle_tcp_options(cm_node, tcph, skb, optionsize,
					 1);
		if (ret) {
			passive_open_err(cm_node, skb, 0);
			/* drop pkt */
			break;
		}
		cm_node->tcp_cntxt.rcv_nxt = inc_sequence + 1;
		BUG_ON(cm_node->send_entry);
		cm_node->accept_pend = 1;
		atomic_inc(&cm_node->listener->pend_accepts_cnt);

		cm_node->state = NES_CM_STATE_SYN_RCVD;
		send_syn(cm_node, 1, skb);
		break;
	case NES_CM_STATE_CLOSED:
		cleanup_retrans_entry(cm_node);
		add_ref_cm_node(cm_node);
		send_reset(cm_node, skb);
		break;
	case NES_CM_STATE_TSA:
	case NES_CM_STATE_ESTABLISHED:
	case NES_CM_STATE_FIN_WAIT1:
	case NES_CM_STATE_FIN_WAIT2:
	case NES_CM_STATE_MPAREQ_RCVD:
	case NES_CM_STATE_LAST_ACK:
	case NES_CM_STATE_CLOSING:
	case NES_CM_STATE_UNKNOWN:
	default:
		drop_packet(skb);
		break;
	}
}

static void handle_synack_pkt(struct nes_cm_node *cm_node, struct sk_buff *skb,
			      struct tcphdr *tcph)
{
	int ret;
	u32 inc_sequence;
	int optionsize;

	optionsize = (tcph->doff << 2) - sizeof(struct tcphdr);
	skb_trim(skb, 0);
	inc_sequence = ntohl(tcph->seq);
	switch (cm_node->state) {
	case NES_CM_STATE_SYN_SENT:
		cleanup_retrans_entry(cm_node);
		/* active open */
		if (check_syn(cm_node, tcph, skb))
			return;
		cm_node->tcp_cntxt.rem_ack_num = ntohl(tcph->ack_seq);
		/* setup options */
		ret = handle_tcp_options(cm_node, tcph, skb, optionsize, 0);
		if (ret) {
			nes_debug(NES_DBG_CM, "cm_node=%p tcp_options failed\n",
				  cm_node);
			break;
		}
		cleanup_retrans_entry(cm_node);
		cm_node->tcp_cntxt.rcv_nxt = inc_sequence + 1;
		send_mpa_request(cm_node, skb);
		cm_node->state = NES_CM_STATE_MPAREQ_SENT;
		break;
	case NES_CM_STATE_MPAREQ_RCVD:
		/* passive open, so should not be here */
		passive_open_err(cm_node, skb, 1);
		break;
	case NES_CM_STATE_LISTENING:
		cm_node->tcp_cntxt.loc_seq_num = ntohl(tcph->ack_seq);
		cleanup_retrans_entry(cm_node);
		cm_node->state = NES_CM_STATE_CLOSED;
		send_reset(cm_node, skb);
		break;
	case NES_CM_STATE_CLOSED:
		cm_node->tcp_cntxt.loc_seq_num = ntohl(tcph->ack_seq);
		cleanup_retrans_entry(cm_node);
		add_ref_cm_node(cm_node);
		send_reset(cm_node, skb);
		break;
	case NES_CM_STATE_ESTABLISHED:
	case NES_CM_STATE_FIN_WAIT1:
	case NES_CM_STATE_FIN_WAIT2:
	case NES_CM_STATE_LAST_ACK:
	case NES_CM_STATE_TSA:
	case NES_CM_STATE_CLOSING:
	case NES_CM_STATE_UNKNOWN:
	case NES_CM_STATE_MPAREQ_SENT:
	default:
		drop_packet(skb);
		break;
	}
}

static int handle_ack_pkt(struct nes_cm_node *cm_node, struct sk_buff *skb,
			  struct tcphdr *tcph)
{
	int datasize = 0;
	u32 inc_sequence;
	int ret = 0;
	int optionsize;

	optionsize = (tcph->doff << 2) - sizeof(struct tcphdr);

	if (check_seq(cm_node, tcph, skb))
		return -EINVAL;

	skb_pull(skb, tcph->doff << 2);
	inc_sequence = ntohl(tcph->seq);
	datasize = skb->len;
	switch (cm_node->state) {
	case NES_CM_STATE_SYN_RCVD:
		/* Passive OPEN */
		cleanup_retrans_entry(cm_node);
		ret = handle_tcp_options(cm_node, tcph, skb, optionsize, 1);
		if (ret)
			break;
		cm_node->tcp_cntxt.rem_ack_num = ntohl(tcph->ack_seq);
		cm_node->state = NES_CM_STATE_ESTABLISHED;
		if (datasize) {
			cm_node->tcp_cntxt.rcv_nxt = inc_sequence + datasize;
			nes_get_remote_addr(cm_node);
			handle_rcv_mpa(cm_node, skb);
		} else { /* rcvd ACK only */
			dev_kfree_skb_any(skb);
		}
		break;
	case NES_CM_STATE_ESTABLISHED:
		/* Passive OPEN */
		cleanup_retrans_entry(cm_node);
		if (datasize) {
			cm_node->tcp_cntxt.rcv_nxt = inc_sequence + datasize;
			handle_rcv_mpa(cm_node, skb);
		} else {
			drop_packet(skb);
		}
		break;
	case NES_CM_STATE_MPAREQ_SENT:
		cm_node->tcp_cntxt.rem_ack_num = ntohl(tcph->ack_seq);
		if (datasize) {
			cm_node->tcp_cntxt.rcv_nxt = inc_sequence + datasize;
			handle_rcv_mpa(cm_node, skb);
		} else { /* Could be just an ack pkt.. */
			dev_kfree_skb_any(skb);
		}
		break;
	case NES_CM_STATE_LISTENING:
		cleanup_retrans_entry(cm_node);
		cm_node->state = NES_CM_STATE_CLOSED;
		send_reset(cm_node, skb);
		break;
	case NES_CM_STATE_CLOSED:
		cleanup_retrans_entry(cm_node);
		add_ref_cm_node(cm_node);
		send_reset(cm_node, skb);
		break;
	case NES_CM_STATE_LAST_ACK:
	case NES_CM_STATE_CLOSING:
		cleanup_retrans_entry(cm_node);
		cm_node->state = NES_CM_STATE_CLOSED;
		cm_node->cm_id->rem_ref(cm_node->cm_id);
		rem_ref_cm_node(cm_node->cm_core, cm_node);
		drop_packet(skb);
		break;
	case NES_CM_STATE_FIN_WAIT1:
		cleanup_retrans_entry(cm_node);
		drop_packet(skb);
		cm_node->state = NES_CM_STATE_FIN_WAIT2;
		break;
	case NES_CM_STATE_SYN_SENT:
	case NES_CM_STATE_FIN_WAIT2:
	case NES_CM_STATE_TSA:
	case NES_CM_STATE_MPAREQ_RCVD:
	case NES_CM_STATE_UNKNOWN:
	default:
		cleanup_retrans_entry(cm_node);
		drop_packet(skb);
		break;
	}
	return ret;
}



static int handle_tcp_options(struct nes_cm_node *cm_node, struct tcphdr *tcph,
			      struct sk_buff *skb, int optionsize, int passive)
{
	u8 *optionsloc = (u8 *)&tcph[1];

	if (optionsize) {
		if (process_options(cm_node, optionsloc, optionsize,
				    (u32)tcph->syn)) {
			nes_debug(NES_DBG_CM, "%s: Node %p, Sending RESET\n",
				  __func__, cm_node);
			if (passive)
				passive_open_err(cm_node, skb, 1);
			else
				active_open_err(cm_node, skb, 1);
			return 1;
		}
	}

	cm_node->tcp_cntxt.snd_wnd = ntohs(tcph->window) <<
				     cm_node->tcp_cntxt.snd_wscale;

	if (cm_node->tcp_cntxt.snd_wnd > cm_node->tcp_cntxt.max_snd_wnd)
		cm_node->tcp_cntxt.max_snd_wnd = cm_node->tcp_cntxt.snd_wnd;
	return 0;
}

/*
 * active_open_err() will send reset() if flag set..
 * It will also send ABORT event.
 */
static void active_open_err(struct nes_cm_node *cm_node, struct sk_buff *skb,
			    int reset)
{
	cleanup_retrans_entry(cm_node);
	if (reset) {
		nes_debug(NES_DBG_CM, "ERROR active err called for cm_node=%p, "
			  "state=%d\n", cm_node, cm_node->state);
		add_ref_cm_node(cm_node);
		send_reset(cm_node, skb);
	} else {
		dev_kfree_skb_any(skb);
	}

	cm_node->state = NES_CM_STATE_CLOSED;
	create_event(cm_node, NES_CM_EVENT_ABORTED);
}

/*
 * passive_open_err() will either do a reset() or will free up the skb and
 * remove the cm_node.
 */
static void passive_open_err(struct nes_cm_node *cm_node, struct sk_buff *skb,
			     int reset)
{
	cleanup_retrans_entry(cm_node);
	cm_node->state = NES_CM_STATE_CLOSED;
	if (reset) {
		nes_debug(NES_DBG_CM, "passive_open_err sending RST for "
			  "cm_node=%p state =%d\n", cm_node, cm_node->state);
		send_reset(cm_node, skb);
	} else {
		dev_kfree_skb_any(skb);
		rem_ref_cm_node(cm_node->cm_core, cm_node);
	}
}

/*
 * free_retrans_entry() routines assumes that the retrans_list_lock has
 * been acquired before calling.
 */
static void free_retrans_entry(struct nes_cm_node *cm_node)
{
	struct nes_timer_entry *send_entry;

	send_entry = cm_node->send_entry;
	if (send_entry) {
		cm_node->send_entry = NULL;
		dev_kfree_skb_any(send_entry->skb);
		kfree(send_entry);
		rem_ref_cm_node(cm_node->cm_core, cm_node);
	}
}

static void cleanup_retrans_entry(struct nes_cm_node *cm_node)
{
	unsigned long flags;

	spin_lock_irqsave(&cm_node->retrans_list_lock, flags);
	free_retrans_entry(cm_node);
	spin_unlock_irqrestore(&cm_node->retrans_list_lock, flags);
}

/**
 * process_packet
 * Returns skb if to be freed, else it will return NULL if already used..
 */
static void process_packet(struct nes_cm_node *cm_node, struct sk_buff *skb,
			   struct nes_cm_core *cm_core)
{
	enum nes_tcpip_pkt_type pkt_type = NES_PKT_TYPE_UNKNOWN;
	struct tcphdr *tcph = tcp_hdr(skb);
	u32 fin_set = 0;
	int ret = 0;

	skb_pull(skb, ip_hdr(skb)->ihl << 2);

	nes_debug(NES_DBG_CM, "process_packet: cm_node=%p state =%d syn=%d "
		  "ack=%d rst=%d fin=%d\n", cm_node, cm_node->state, tcph->syn,
		  tcph->ack, tcph->rst, tcph->fin);

	if (tcph->rst) {
		pkt_type = NES_PKT_TYPE_RST;
	} else if (tcph->syn) {
		pkt_type = NES_PKT_TYPE_SYN;
		if (tcph->ack)
			pkt_type = NES_PKT_TYPE_SYNACK;
	} else if (tcph->ack) {
		pkt_type = NES_PKT_TYPE_ACK;
	}
	if (tcph->fin)
		fin_set = 1;

	switch (pkt_type) {
	case NES_PKT_TYPE_SYN:
		handle_syn_pkt(cm_node, skb, tcph);
		break;
	case NES_PKT_TYPE_SYNACK:
		handle_synack_pkt(cm_node, skb, tcph);
		break;
	case NES_PKT_TYPE_ACK:
		ret = handle_ack_pkt(cm_node, skb, tcph);
		if (fin_set && !ret)
			handle_fin_pkt(cm_node);
		break;
	case NES_PKT_TYPE_RST:
		handle_rst_pkt(cm_node, skb, tcph);
		break;
	default:
		if ((fin_set) && (!check_seq(cm_node, tcph, skb)))
			handle_fin_pkt(cm_node);
		drop_packet(skb);
		break;
	}
}

/**
 * mini_cm_listen - create a listen node with params
 */
static struct nes_cm_listener *mini_cm_listen(struct nes_cm_core *cm_core,
			struct nes_vnic *nesvnic, struct nes_cm_info *cm_info)
{
	struct nes_cm_listener *listener;
	struct iwpm_dev_data pm_reg_msg;
	struct iwpm_sa_data pm_msg;
	unsigned long flags;
	int iwpm_err = 0;

	nes_debug(NES_DBG_CM, "Search for 0x%08x : 0x%04x\n",
		  cm_info->loc_addr, cm_info->loc_port);

	/* cannot have multiple matching listeners */
	listener = find_listener(cm_core, cm_info->loc_addr, cm_info->loc_port,
				NES_CM_LISTENER_EITHER_STATE, 1);

	if (listener && listener->listener_state == NES_CM_LISTENER_ACTIVE_STATE) {
		/* find automatically incs ref count ??? */
		atomic_dec(&listener->ref_count);
		nes_debug(NES_DBG_CM, "Not creating listener since it already exists\n");
		return NULL;
	}

	if (!listener) {
		nes_form_reg_msg(nesvnic, &pm_reg_msg);
		iwpm_err = iwpm_register_pid(&pm_reg_msg, RDMA_NL_NES);
		if (iwpm_err) {
			nes_debug(NES_DBG_NLMSG,
			"Port Mapper reg pid fail (err = %d).\n", iwpm_err);
		}
		if (iwpm_valid_pid() && !iwpm_err) {
			nes_form_pm_msg(cm_info, &pm_msg);
			iwpm_err = iwpm_add_mapping(&pm_msg, RDMA_NL_NES);
			if (iwpm_err)
				nes_debug(NES_DBG_NLMSG,
				"Port Mapper query fail (err = %d).\n", iwpm_err);
			else
				nes_record_pm_msg(cm_info, &pm_msg);
		}

		/* create a CM listen node (1/2 node to compare incoming traffic to) */
		listener = kzalloc(sizeof(*listener), GFP_ATOMIC);
		if (!listener) {
			nes_debug(NES_DBG_CM, "Not creating listener memory allocation failed\n");
			return NULL;
		}

		listener->loc_addr = cm_info->loc_addr;
		listener->loc_port = cm_info->loc_port;
		listener->mapped_loc_addr = cm_info->mapped_loc_addr;
		listener->mapped_loc_port = cm_info->mapped_loc_port;
		listener->reused_node = 0;

		atomic_set(&listener->ref_count, 1);
	}
	/* pasive case */
	/* find already inc'ed the ref count */
	else {
		listener->reused_node = 1;
	}

	listener->cm_id = cm_info->cm_id;
	atomic_set(&listener->pend_accepts_cnt, 0);
	listener->cm_core = cm_core;
	listener->nesvnic = nesvnic;
	atomic_inc(&cm_core->node_cnt);

	listener->conn_type = cm_info->conn_type;
	listener->backlog = cm_info->backlog;
	listener->listener_state = NES_CM_LISTENER_ACTIVE_STATE;

	if (!listener->reused_node) {
		spin_lock_irqsave(&cm_core->listen_list_lock, flags);
		list_add(&listener->list, &cm_core->listen_list.list);
		spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);
		atomic_inc(&cm_core->listen_node_cnt);
	}

	nes_debug(NES_DBG_CM, "Api - listen(): addr=0x%08X, port=0x%04x,"
		  " listener = %p, backlog = %d, cm_id = %p.\n",
		  cm_info->loc_addr, cm_info->loc_port,
		  listener, listener->backlog, listener->cm_id);

	return listener;
}


/**
 * mini_cm_connect - make a connection node with params
 */
static struct nes_cm_node *mini_cm_connect(struct nes_cm_core *cm_core,
					   struct nes_vnic *nesvnic, u16 private_data_len,
					   void *private_data, struct nes_cm_info *cm_info)
{
	int ret = 0;
	struct nes_cm_node *cm_node;
	struct nes_cm_listener *loopbackremotelistener;
	struct nes_cm_node *loopbackremotenode;
	struct nes_cm_info loopback_cm_info;
	u8 *start_buff;

	/* create a CM connection node */
	cm_node = make_cm_node(cm_core, nesvnic, cm_info, NULL);
	if (!cm_node)
		return NULL;

	/* set our node side to client (active) side */
	cm_node->tcp_cntxt.client = 1;
	cm_node->tcp_cntxt.rcv_wscale = NES_CM_DEFAULT_RCV_WND_SCALE;

	if (cm_info->loc_addr == cm_info->rem_addr) {
		loopbackremotelistener = find_listener(cm_core,
			cm_node->mapped_loc_addr, cm_node->mapped_rem_port,
			NES_CM_LISTENER_ACTIVE_STATE, 0);
		if (loopbackremotelistener == NULL) {
			create_event(cm_node, NES_CM_EVENT_ABORTED);
		} else {
			loopback_cm_info = *cm_info;
			loopback_cm_info.loc_port = cm_info->rem_port;
			loopback_cm_info.rem_port = cm_info->loc_port;
			loopback_cm_info.mapped_loc_port =
				cm_info->mapped_rem_port;
			loopback_cm_info.mapped_rem_port =
				cm_info->mapped_loc_port;
			loopback_cm_info.cm_id = loopbackremotelistener->cm_id;
			loopbackremotenode = make_cm_node(cm_core, nesvnic,
							  &loopback_cm_info, loopbackremotelistener);
			if (!loopbackremotenode) {
				rem_ref_cm_node(cm_node->cm_core, cm_node);
				return NULL;
			}
			atomic_inc(&cm_loopbacks);
			loopbackremotenode->loopbackpartner = cm_node;
			loopbackremotenode->tcp_cntxt.rcv_wscale =
				NES_CM_DEFAULT_RCV_WND_SCALE;
			cm_node->loopbackpartner = loopbackremotenode;
			memcpy(loopbackremotenode->mpa_frame_buf, private_data,
			       private_data_len);
			loopbackremotenode->mpa_frame_size = private_data_len;

			/* we are done handling this state. */
			/* set node to a TSA state */
			cm_node->state = NES_CM_STATE_TSA;
			cm_node->tcp_cntxt.rcv_nxt =
				loopbackremotenode->tcp_cntxt.loc_seq_num;
			loopbackremotenode->tcp_cntxt.rcv_nxt =
				cm_node->tcp_cntxt.loc_seq_num;
			cm_node->tcp_cntxt.max_snd_wnd =
				loopbackremotenode->tcp_cntxt.rcv_wnd;
			loopbackremotenode->tcp_cntxt.max_snd_wnd =
				cm_node->tcp_cntxt.rcv_wnd;
			cm_node->tcp_cntxt.snd_wnd =
				loopbackremotenode->tcp_cntxt.rcv_wnd;
			loopbackremotenode->tcp_cntxt.snd_wnd =
				cm_node->tcp_cntxt.rcv_wnd;
			cm_node->tcp_cntxt.snd_wscale =
				loopbackremotenode->tcp_cntxt.rcv_wscale;
			loopbackremotenode->tcp_cntxt.snd_wscale =
				cm_node->tcp_cntxt.rcv_wscale;
			loopbackremotenode->state = NES_CM_STATE_MPAREQ_RCVD;
			create_event(loopbackremotenode, NES_CM_EVENT_MPA_REQ);
		}
		return cm_node;
	}

	start_buff = &cm_node->mpa_frame_buf[0] + sizeof(struct ietf_mpa_v2);
	cm_node->mpa_frame_size = private_data_len;

	memcpy(start_buff, private_data, private_data_len);

	/* send a syn and goto syn sent state */
	cm_node->state = NES_CM_STATE_SYN_SENT;
	ret = send_syn(cm_node, 0, NULL);

	if (ret) {
		/* error in sending the syn free up the cm_node struct */
		nes_debug(NES_DBG_CM, "Api - connect() FAILED: dest "
			  "addr=0x%08X, port=0x%04x, cm_node=%p, cm_id = %p.\n",
			  cm_node->rem_addr, cm_node->rem_port, cm_node,
			  cm_node->cm_id);
		rem_ref_cm_node(cm_node->cm_core, cm_node);
		cm_node = NULL;
	}

	if (cm_node) {
		nes_debug(NES_DBG_CM, "Api - connect(): dest addr=0x%08X,"
			  "port=0x%04x, cm_node=%p, cm_id = %p.\n",
			  cm_node->rem_addr, cm_node->rem_port, cm_node,
			  cm_node->cm_id);
	}

	return cm_node;
}


/**
 * mini_cm_accept - accept a connection
 * This function is never called
 */
static int mini_cm_accept(struct nes_cm_core *cm_core, struct nes_cm_node *cm_node)
{
	return 0;
}


/**
 * mini_cm_reject - reject and teardown a connection
 */
static int mini_cm_reject(struct nes_cm_core *cm_core, struct nes_cm_node *cm_node)
{
	int ret = 0;
	int err = 0;
	int passive_state;
	struct nes_cm_event event;
	struct iw_cm_id *cm_id = cm_node->cm_id;
	struct nes_cm_node *loopback = cm_node->loopbackpartner;

	nes_debug(NES_DBG_CM, "%s cm_node=%p type=%d state=%d\n",
		  __func__, cm_node, cm_node->tcp_cntxt.client, cm_node->state);

	if (cm_node->tcp_cntxt.client)
		return ret;
	cleanup_retrans_entry(cm_node);

	if (!loopback) {
		passive_state = atomic_add_return(1, &cm_node->passive_state);
		if (passive_state == NES_SEND_RESET_EVENT) {
			cm_node->state = NES_CM_STATE_CLOSED;
			rem_ref_cm_node(cm_core, cm_node);
		} else {
			if (cm_node->state == NES_CM_STATE_LISTENER_DESTROYED) {
				rem_ref_cm_node(cm_core, cm_node);
			} else {
				ret = send_mpa_reject(cm_node);
				if (ret) {
					cm_node->state = NES_CM_STATE_CLOSED;
					err = send_reset(cm_node, NULL);
					if (err)
						WARN_ON(1);
				} else {
					cm_id->add_ref(cm_id);
				}
			}
		}
	} else {
		cm_node->cm_id = NULL;
		if (cm_node->state == NES_CM_STATE_LISTENER_DESTROYED) {
			rem_ref_cm_node(cm_core, cm_node);
			rem_ref_cm_node(cm_core, loopback);
		} else {
			event.cm_node = loopback;
			event.cm_info.rem_addr = loopback->rem_addr;
			event.cm_info.loc_addr = loopback->loc_addr;
			event.cm_info.rem_port = loopback->rem_port;
			event.cm_info.loc_port = loopback->loc_port;
			event.cm_info.cm_id = loopback->cm_id;
			cm_event_mpa_reject(&event);
			rem_ref_cm_node(cm_core, cm_node);
			loopback->state = NES_CM_STATE_CLOSING;

			cm_id = loopback->cm_id;
			rem_ref_cm_node(cm_core, loopback);
			cm_id->rem_ref(cm_id);
		}
	}

	return ret;
}


/**
 * mini_cm_close
 */
static int mini_cm_close(struct nes_cm_core *cm_core, struct nes_cm_node *cm_node)
{
	int ret = 0;

	if (!cm_core || !cm_node)
		return -EINVAL;

	switch (cm_node->state) {
	case NES_CM_STATE_SYN_RCVD:
	case NES_CM_STATE_SYN_SENT:
	case NES_CM_STATE_ONE_SIDE_ESTABLISHED:
	case NES_CM_STATE_ESTABLISHED:
	case NES_CM_STATE_ACCEPTING:
	case NES_CM_STATE_MPAREQ_SENT:
	case NES_CM_STATE_MPAREQ_RCVD:
		cleanup_retrans_entry(cm_node);
		send_reset(cm_node, NULL);
		break;
	case NES_CM_STATE_CLOSE_WAIT:
		cm_node->state = NES_CM_STATE_LAST_ACK;
		send_fin(cm_node, NULL);
		break;
	case NES_CM_STATE_FIN_WAIT1:
	case NES_CM_STATE_FIN_WAIT2:
	case NES_CM_STATE_LAST_ACK:
	case NES_CM_STATE_TIME_WAIT:
	case NES_CM_STATE_CLOSING:
		ret = -1;
		break;
	case NES_CM_STATE_LISTENING:
		cleanup_retrans_entry(cm_node);
		send_reset(cm_node, NULL);
		break;
	case NES_CM_STATE_MPAREJ_RCVD:
	case NES_CM_STATE_UNKNOWN:
	case NES_CM_STATE_INITED:
	case NES_CM_STATE_CLOSED:
	case NES_CM_STATE_LISTENER_DESTROYED:
		ret = rem_ref_cm_node(cm_core, cm_node);
		break;
	case NES_CM_STATE_TSA:
		if (cm_node->send_entry)
			printk(KERN_ERR "ERROR Close got called from STATE_TSA "
			       "send_entry=%p\n", cm_node->send_entry);
		ret = rem_ref_cm_node(cm_core, cm_node);
		break;
	}
	return ret;
}


/**
 * recv_pkt - recv an ETHERNET packet, and process it through CM
 * node state machine
 */
static int mini_cm_recv_pkt(struct nes_cm_core *cm_core,
			    struct nes_vnic *nesvnic, struct sk_buff *skb)
{
	struct nes_cm_node *cm_node = NULL;
	struct nes_cm_listener *listener = NULL;
	struct iphdr *iph;
	struct tcphdr *tcph;
	struct nes_cm_info nfo;
	int skb_handled = 1;
	__be32 tmp_daddr, tmp_saddr;

	if (!skb)
		return 0;
	if (skb->len < sizeof(struct iphdr) + sizeof(struct tcphdr))
		return 0;

	iph = (struct iphdr *)skb->data;
	tcph = (struct tcphdr *)(skb->data + sizeof(struct iphdr));

	nfo.loc_addr = ntohl(iph->daddr);
	nfo.loc_port = ntohs(tcph->dest);
	nfo.rem_addr = ntohl(iph->saddr);
	nfo.rem_port = ntohs(tcph->source);

	/* If port mapper is available these should be mapped address info */
	nfo.mapped_loc_addr = ntohl(iph->daddr);
	nfo.mapped_loc_port = ntohs(tcph->dest);
	nfo.mapped_rem_addr = ntohl(iph->saddr);
	nfo.mapped_rem_port = ntohs(tcph->source);

	tmp_daddr = cpu_to_be32(iph->daddr);
	tmp_saddr = cpu_to_be32(iph->saddr);

	nes_debug(NES_DBG_CM, "Received packet: dest=%pI4:0x%04X src=%pI4:0x%04X\n",
		  &tmp_daddr, tcph->dest, &tmp_saddr, tcph->source);

	do {
		cm_node = find_node(cm_core,
				    nfo.mapped_rem_port, nfo.mapped_rem_addr,
				    nfo.mapped_loc_port, nfo.mapped_loc_addr);

		if (!cm_node) {
			/* Only type of packet accepted are for */
			/* the PASSIVE open (syn only) */
			if ((!tcph->syn) || (tcph->ack)) {
				skb_handled = 0;
				break;
			}
			listener = find_listener(cm_core, nfo.mapped_loc_addr,
					nfo.mapped_loc_port,
					NES_CM_LISTENER_ACTIVE_STATE, 0);
			if (!listener) {
				nfo.cm_id = NULL;
				nfo.conn_type = 0;
				nes_debug(NES_DBG_CM, "Unable to find listener for the pkt\n");
				skb_handled = 0;
				break;
			}
			nfo.cm_id = listener->cm_id;
			nfo.conn_type = listener->conn_type;
			cm_node = make_cm_node(cm_core, nesvnic, &nfo,
					       listener);
			if (!cm_node) {
				nes_debug(NES_DBG_CM, "Unable to allocate "
					  "node\n");
				cm_packets_dropped++;
				atomic_dec(&listener->ref_count);
				dev_kfree_skb_any(skb);
				break;
			}
			if (!tcph->rst && !tcph->fin) {
				cm_node->state = NES_CM_STATE_LISTENING;
			} else {
				cm_packets_dropped++;
				rem_ref_cm_node(cm_core, cm_node);
				dev_kfree_skb_any(skb);
				break;
			}
			add_ref_cm_node(cm_node);
		} else if (cm_node->state == NES_CM_STATE_TSA) {
			if (cm_node->nesqp->pau_mode)
				nes_queue_mgt_skbs(skb, nesvnic, cm_node->nesqp);
			else {
				rem_ref_cm_node(cm_core, cm_node);
				atomic_inc(&cm_accel_dropped_pkts);
				dev_kfree_skb_any(skb);
			}
			break;
		}
		skb_reset_network_header(skb);
		skb_set_transport_header(skb, sizeof(*tcph));
		skb->len = ntohs(iph->tot_len);
		process_packet(cm_node, skb, cm_core);
		rem_ref_cm_node(cm_core, cm_node);
	} while (0);
	return skb_handled;
}


/**
 * nes_cm_alloc_core - allocate a top level instance of a cm core
 */
static struct nes_cm_core *nes_cm_alloc_core(void)
{
	struct nes_cm_core *cm_core;

	/* setup the CM core */
	/* alloc top level core control structure */
	cm_core = kzalloc(sizeof(*cm_core), GFP_KERNEL);
	if (!cm_core)
		return NULL;

	INIT_LIST_HEAD(&cm_core->connected_nodes);
	init_timer(&cm_core->tcp_timer);
	cm_core->tcp_timer.function = nes_cm_timer_tick;

	cm_core->mtu = NES_CM_DEFAULT_MTU;
	cm_core->state = NES_CM_STATE_INITED;
	cm_core->free_tx_pkt_max = NES_CM_DEFAULT_FREE_PKTS;

	atomic_set(&cm_core->events_posted, 0);

	cm_core->api = &nes_cm_api;

	spin_lock_init(&cm_core->ht_lock);
	spin_lock_init(&cm_core->listen_list_lock);

	INIT_LIST_HEAD(&cm_core->listen_list.list);

	nes_debug(NES_DBG_CM, "Init CM Core completed -- cm_core=%p\n", cm_core);

	nes_debug(NES_DBG_CM, "Enable QUEUE EVENTS\n");
	cm_core->event_wq = create_singlethread_workqueue("nesewq");
	cm_core->post_event = nes_cm_post_event;
	nes_debug(NES_DBG_CM, "Enable QUEUE DISCONNECTS\n");
	cm_core->disconn_wq = create_singlethread_workqueue("nesdwq");

	print_core(cm_core);
	return cm_core;
}


/**
 * mini_cm_dealloc_core - deallocate a top level instance of a cm core
 */
static int mini_cm_dealloc_core(struct nes_cm_core *cm_core)
{
	nes_debug(NES_DBG_CM, "De-Alloc CM Core (%p)\n", cm_core);

	if (!cm_core)
		return -EINVAL;

	barrier();

	if (timer_pending(&cm_core->tcp_timer))
		del_timer(&cm_core->tcp_timer);

	destroy_workqueue(cm_core->event_wq);
	destroy_workqueue(cm_core->disconn_wq);
	nes_debug(NES_DBG_CM, "\n");
	kfree(cm_core);

	return 0;
}


/**
 * mini_cm_get
 */
static int mini_cm_get(struct nes_cm_core *cm_core)
{
	return cm_core->state;
}


/**
 * mini_cm_set
 */
static int mini_cm_set(struct nes_cm_core *cm_core, u32 type, u32 value)
{
	int ret = 0;

	switch (type) {
	case NES_CM_SET_PKT_SIZE:
		cm_core->mtu = value;
		break;
	case NES_CM_SET_FREE_PKT_Q_SIZE:
		cm_core->free_tx_pkt_max = value;
		break;
	default:
		/* unknown set option */
		ret = -EINVAL;
	}

	return ret;
}


/**
 * nes_cm_init_tsa_conn setup HW; MPA frames must be
 * successfully exchanged when this is called
 */
static int nes_cm_init_tsa_conn(struct nes_qp *nesqp, struct nes_cm_node *cm_node)
{
	int ret = 0;

	if (!nesqp)
		return -EINVAL;

	nesqp->nesqp_context->misc |= cpu_to_le32(NES_QPCONTEXT_MISC_IPV4 |
						  NES_QPCONTEXT_MISC_NO_NAGLE | NES_QPCONTEXT_MISC_DO_NOT_FRAG |
						  NES_QPCONTEXT_MISC_DROS);

	if (cm_node->tcp_cntxt.snd_wscale || cm_node->tcp_cntxt.rcv_wscale)
		nesqp->nesqp_context->misc |= cpu_to_le32(NES_QPCONTEXT_MISC_WSCALE);

	nesqp->nesqp_context->misc2 |= cpu_to_le32(64 << NES_QPCONTEXT_MISC2_TTL_SHIFT);

	nesqp->nesqp_context->misc2 |= cpu_to_le32(
		cm_node->tos << NES_QPCONTEXT_MISC2_TOS_SHIFT);

	nesqp->nesqp_context->mss |= cpu_to_le32(((u32)cm_node->tcp_cntxt.mss) << 16);

	nesqp->nesqp_context->tcp_state_flow_label |= cpu_to_le32(
		(u32)NES_QPCONTEXT_TCPSTATE_EST << NES_QPCONTEXT_TCPFLOW_TCP_STATE_SHIFT);

	nesqp->nesqp_context->pd_index_wscale |= cpu_to_le32(
		(cm_node->tcp_cntxt.snd_wscale << NES_QPCONTEXT_PDWSCALE_SND_WSCALE_SHIFT) &
		NES_QPCONTEXT_PDWSCALE_SND_WSCALE_MASK);

	nesqp->nesqp_context->pd_index_wscale |= cpu_to_le32(
		(cm_node->tcp_cntxt.rcv_wscale << NES_QPCONTEXT_PDWSCALE_RCV_WSCALE_SHIFT) &
		NES_QPCONTEXT_PDWSCALE_RCV_WSCALE_MASK);

	nesqp->nesqp_context->keepalive = cpu_to_le32(0x80);
	nesqp->nesqp_context->ts_recent = 0;
	nesqp->nesqp_context->ts_age = 0;
	nesqp->nesqp_context->snd_nxt = cpu_to_le32(cm_node->tcp_cntxt.loc_seq_num);
	nesqp->nesqp_context->snd_wnd = cpu_to_le32(cm_node->tcp_cntxt.snd_wnd);
	nesqp->nesqp_context->rcv_nxt = cpu_to_le32(cm_node->tcp_cntxt.rcv_nxt);
	nesqp->nesqp_context->rcv_wnd = cpu_to_le32(cm_node->tcp_cntxt.rcv_wnd <<
						    cm_node->tcp_cntxt.rcv_wscale);
	nesqp->nesqp_context->snd_max = cpu_to_le32(cm_node->tcp_cntxt.loc_seq_num);
	nesqp->nesqp_context->snd_una = cpu_to_le32(cm_node->tcp_cntxt.loc_seq_num);
	nesqp->nesqp_context->srtt = 0;
	nesqp->nesqp_context->rttvar = cpu_to_le32(0x6);
	nesqp->nesqp_context->ssthresh = cpu_to_le32(0x3FFFC000);
	nesqp->nesqp_context->cwnd = cpu_to_le32(2 * cm_node->tcp_cntxt.mss);
	nesqp->nesqp_context->snd_wl1 = cpu_to_le32(cm_node->tcp_cntxt.rcv_nxt);
	nesqp->nesqp_context->snd_wl2 = cpu_to_le32(cm_node->tcp_cntxt.loc_seq_num);
	nesqp->nesqp_context->max_snd_wnd = cpu_to_le32(cm_node->tcp_cntxt.max_snd_wnd);

	nes_debug(NES_DBG_CM, "QP%u: rcv_nxt = 0x%08X, snd_nxt = 0x%08X,"
		  " Setting MSS to %u, PDWscale = 0x%08X, rcv_wnd = %u, context misc = 0x%08X.\n",
		  nesqp->hwqp.qp_id, le32_to_cpu(nesqp->nesqp_context->rcv_nxt),
		  le32_to_cpu(nesqp->nesqp_context->snd_nxt),
		  cm_node->tcp_cntxt.mss, le32_to_cpu(nesqp->nesqp_context->pd_index_wscale),
		  le32_to_cpu(nesqp->nesqp_context->rcv_wnd),
		  le32_to_cpu(nesqp->nesqp_context->misc));
	nes_debug(NES_DBG_CM, "  snd_wnd  = 0x%08X.\n", le32_to_cpu(nesqp->nesqp_context->snd_wnd));
	nes_debug(NES_DBG_CM, "  snd_cwnd = 0x%08X.\n", le32_to_cpu(nesqp->nesqp_context->cwnd));
	nes_debug(NES_DBG_CM, "  max_swnd = 0x%08X.\n", le32_to_cpu(nesqp->nesqp_context->max_snd_wnd));

	nes_debug(NES_DBG_CM, "Change cm_node state to TSA\n");
	cm_node->state = NES_CM_STATE_TSA;

	return ret;
}


/**
 * nes_cm_disconn
 */
int nes_cm_disconn(struct nes_qp *nesqp)
{
	struct disconn_work *work;

	work = kzalloc(sizeof *work, GFP_ATOMIC);
	if (!work)
		return -ENOMEM;  /* Timer will clean up */

	nes_add_ref(&nesqp->ibqp);
	work->nesqp = nesqp;
	INIT_WORK(&work->work, nes_disconnect_worker);
	queue_work(g_cm_core->disconn_wq, &work->work);
	return 0;
}


/**
 * nes_disconnect_worker
 */
static void nes_disconnect_worker(struct work_struct *work)
{
	struct disconn_work *dwork = container_of(work, struct disconn_work, work);
	struct nes_qp *nesqp = dwork->nesqp;

	kfree(dwork);
	nes_debug(NES_DBG_CM, "processing AEQE id 0x%04X for QP%u.\n",
		  nesqp->last_aeq, nesqp->hwqp.qp_id);
	nes_cm_disconn_true(nesqp);
	nes_rem_ref(&nesqp->ibqp);
}


/**
 * nes_cm_disconn_true
 */
static int nes_cm_disconn_true(struct nes_qp *nesqp)
{
	unsigned long flags;
	int ret = 0;
	struct iw_cm_id *cm_id;
	struct iw_cm_event cm_event;
	struct nes_vnic *nesvnic;
	u16 last_ae;
	u8 original_hw_tcp_state;
	u8 original_ibqp_state;
	int disconn_status = 0;
	int issue_disconn = 0;
	int issue_close = 0;
	int issue_flush = 0;
	u32 flush_q = NES_CQP_FLUSH_RQ;
	struct ib_event ibevent;

	if (!nesqp) {
		nes_debug(NES_DBG_CM, "disconnect_worker nesqp is NULL\n");
		return -1;
	}

	spin_lock_irqsave(&nesqp->lock, flags);
	cm_id = nesqp->cm_id;
	/* make sure we havent already closed this connection */
	if (!cm_id) {
		nes_debug(NES_DBG_CM, "QP%u disconnect_worker cmid is NULL\n",
			  nesqp->hwqp.qp_id);
		spin_unlock_irqrestore(&nesqp->lock, flags);
		return -1;
	}

	nesvnic = to_nesvnic(nesqp->ibqp.device);
	nes_debug(NES_DBG_CM, "Disconnecting QP%u\n", nesqp->hwqp.qp_id);

	original_hw_tcp_state = nesqp->hw_tcp_state;
	original_ibqp_state = nesqp->ibqp_state;
	last_ae = nesqp->last_aeq;

	if (nesqp->term_flags) {
		issue_disconn = 1;
		issue_close = 1;
		nesqp->cm_id = NULL;
		del_timer(&nesqp->terminate_timer);
		if (nesqp->flush_issued == 0) {
			nesqp->flush_issued = 1;
			issue_flush = 1;
		}
	} else if ((original_hw_tcp_state == NES_AEQE_TCP_STATE_CLOSE_WAIT) ||
			((original_ibqp_state == IB_QPS_RTS) &&
			(last_ae == NES_AEQE_AEID_LLP_CONNECTION_RESET))) {
		issue_disconn = 1;
		if (last_ae == NES_AEQE_AEID_LLP_CONNECTION_RESET)
			disconn_status = -ECONNRESET;
	}

	if (((original_hw_tcp_state == NES_AEQE_TCP_STATE_CLOSED) ||
		 (original_hw_tcp_state == NES_AEQE_TCP_STATE_TIME_WAIT) ||
		 (last_ae == NES_AEQE_AEID_RDMAP_ROE_BAD_LLP_CLOSE) ||
		 (last_ae == NES_AEQE_AEID_LLP_CONNECTION_RESET))) {
		issue_close = 1;
		nesqp->cm_id = NULL;
		if (nesqp->flush_issued == 0) {
			nesqp->flush_issued = 1;
			issue_flush = 1;
		}
	}

	spin_unlock_irqrestore(&nesqp->lock, flags);

	if ((issue_flush) && (nesqp->destroyed == 0)) {
		/* Flush the queue(s) */
		if (nesqp->hw_iwarp_state >= NES_AEQE_IWARP_STATE_TERMINATE)
			flush_q |= NES_CQP_FLUSH_SQ;
		flush_wqes(nesvnic->nesdev, nesqp, flush_q, 1);

		if (nesqp->term_flags) {
			ibevent.device = nesqp->ibqp.device;
			ibevent.event = nesqp->terminate_eventtype;
			ibevent.element.qp = &nesqp->ibqp;
			if (nesqp->ibqp.event_handler)
				nesqp->ibqp.event_handler(&ibevent, nesqp->ibqp.qp_context);
		}
	}

	if ((cm_id) && (cm_id->event_handler)) {
		if (issue_disconn) {
			atomic_inc(&cm_disconnects);
			cm_event.event = IW_CM_EVENT_DISCONNECT;
			cm_event.status = disconn_status;
			cm_event.local_addr = cm_id->local_addr;
			cm_event.remote_addr = cm_id->remote_addr;
			cm_event.private_data = NULL;
			cm_event.private_data_len = 0;

			nes_debug(NES_DBG_CM, "Generating a CM Disconnect Event"
				  " for  QP%u, SQ Head = %u, SQ Tail = %u. "
				  "cm_id = %p, refcount = %u.\n",
				  nesqp->hwqp.qp_id, nesqp->hwqp.sq_head,
				  nesqp->hwqp.sq_tail, cm_id,
				  atomic_read(&nesqp->refcount));

			ret = cm_id->event_handler(cm_id, &cm_event);
			if (ret)
				nes_debug(NES_DBG_CM, "OFA CM event_handler "
					  "returned, ret=%d\n", ret);
		}

		if (issue_close) {
			atomic_inc(&cm_closes);
			nes_disconnect(nesqp, 1);

			cm_id->provider_data = nesqp;
			/* Send up the close complete event */
			cm_event.event = IW_CM_EVENT_CLOSE;
			cm_event.status = 0;
			cm_event.provider_data = cm_id->provider_data;
			cm_event.local_addr = cm_id->local_addr;
			cm_event.remote_addr = cm_id->remote_addr;
			cm_event.private_data = NULL;
			cm_event.private_data_len = 0;

			ret = cm_id->event_handler(cm_id, &cm_event);
			if (ret)
				nes_debug(NES_DBG_CM, "OFA CM event_handler returned, ret=%d\n", ret);

			cm_id->rem_ref(cm_id);
		}
	}

	return 0;
}


/**
 * nes_disconnect
 */
static int nes_disconnect(struct nes_qp *nesqp, int abrupt)
{
	int ret = 0;
	struct nes_vnic *nesvnic;
	struct nes_device *nesdev;
	struct nes_ib_device *nesibdev;

	nesvnic = to_nesvnic(nesqp->ibqp.device);
	if (!nesvnic)
		return -EINVAL;

	nesdev = nesvnic->nesdev;
	nesibdev = nesvnic->nesibdev;

	nes_debug(NES_DBG_CM, "netdev refcnt = %u.\n",
			netdev_refcnt_read(nesvnic->netdev));

	if (nesqp->active_conn) {

		/* indicate this connection is NOT active */
		nesqp->active_conn = 0;
	} else {
		/* Need to free the Last Streaming Mode Message */
		if (nesqp->ietf_frame) {
			if (nesqp->lsmm_mr)
				nesibdev->ibdev.dereg_mr(nesqp->lsmm_mr);
			pci_free_consistent(nesdev->pcidev,
					    nesqp->private_data_len + nesqp->ietf_frame_size,
					    nesqp->ietf_frame, nesqp->ietf_frame_pbase);
		}
	}

	/* close the CM node down if it is still active */
	if (nesqp->cm_node) {
		nes_debug(NES_DBG_CM, "Call close API\n");

		g_cm_core->api->close(g_cm_core, nesqp->cm_node);
	}

	return ret;
}


/**
 * nes_accept
 */
int nes_accept(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	u64 u64temp;
	struct ib_qp *ibqp;
	struct nes_qp *nesqp;
	struct nes_vnic *nesvnic;
	struct nes_device *nesdev;
	struct nes_cm_node *cm_node;
	struct nes_adapter *adapter;
	struct ib_qp_attr attr;
	struct iw_cm_event cm_event;
	struct nes_hw_qp_wqe *wqe;
	struct nes_v4_quad nes_quad;
	u32 crc_value;
	int ret;
	int passive_state;
	struct nes_ib_device *nesibdev;
	struct ib_mr *ibmr = NULL;
	struct ib_phys_buf ibphysbuf;
	struct nes_pd *nespd;
	u64 tagged_offset;
	u8 mpa_frame_offset = 0;
	struct ietf_mpa_v2 *mpa_v2_frame;
	u8 start_addr = 0;
	u8 *start_ptr = &start_addr;
	u8 **start_buff = &start_ptr;
	u16 buff_len = 0;
	struct sockaddr_in *laddr = (struct sockaddr_in *)&cm_id->local_addr;
	struct sockaddr_in *raddr = (struct sockaddr_in *)&cm_id->remote_addr;

	ibqp = nes_get_qp(cm_id->device, conn_param->qpn);
	if (!ibqp)
		return -EINVAL;

	/* get all our handles */
	nesqp = to_nesqp(ibqp);
	nesvnic = to_nesvnic(nesqp->ibqp.device);
	nesdev = nesvnic->nesdev;
	adapter = nesdev->nesadapter;

	cm_node = (struct nes_cm_node *)cm_id->provider_data;
	nes_debug(NES_DBG_CM, "nes_accept: cm_node= %p nesvnic=%p, netdev=%p,"
		"%s\n", cm_node, nesvnic, nesvnic->netdev,
		nesvnic->netdev->name);

	if (NES_CM_STATE_LISTENER_DESTROYED == cm_node->state) {
		if (cm_node->loopbackpartner)
			rem_ref_cm_node(cm_node->cm_core, cm_node->loopbackpartner);
		rem_ref_cm_node(cm_node->cm_core, cm_node);
		return -EINVAL;
	}

	passive_state = atomic_add_return(1, &cm_node->passive_state);
	if (passive_state == NES_SEND_RESET_EVENT) {
		rem_ref_cm_node(cm_node->cm_core, cm_node);
		return -ECONNRESET;
	}
	/* associate the node with the QP */
	nesqp->cm_node = (void *)cm_node;
	cm_node->nesqp = nesqp;


	nes_debug(NES_DBG_CM, "QP%u, cm_node=%p, jiffies = %lu listener = %p\n",
		nesqp->hwqp.qp_id, cm_node, jiffies, cm_node->listener);
	atomic_inc(&cm_accepts);

	nes_debug(NES_DBG_CM, "netdev refcnt = %u.\n",
			netdev_refcnt_read(nesvnic->netdev));

	nesqp->ietf_frame_size = sizeof(struct ietf_mpa_v2);
	/* allocate the ietf frame and space for private data */
	nesqp->ietf_frame = pci_alloc_consistent(nesdev->pcidev,
						 nesqp->ietf_frame_size + conn_param->private_data_len,
						 &nesqp->ietf_frame_pbase);

	if (!nesqp->ietf_frame) {
		nes_debug(NES_DBG_CM, "Unable to allocate memory for private data\n");
		return -ENOMEM;
	}
	mpa_v2_frame = (struct ietf_mpa_v2 *)nesqp->ietf_frame;

	if (cm_node->mpa_frame_rev == IETF_MPA_V1)
		mpa_frame_offset = 4;

	if (cm_node->mpa_frame_rev == IETF_MPA_V1 ||
			cm_node->mpav2_ird_ord == IETF_NO_IRD_ORD) {
		record_ird_ord(cm_node, (u16)conn_param->ird, (u16)conn_param->ord);
	}

	memcpy(mpa_v2_frame->priv_data, conn_param->private_data,
	       conn_param->private_data_len);

	cm_build_mpa_frame(cm_node, start_buff, &buff_len, nesqp->ietf_frame, MPA_KEY_REPLY);
	nesqp->private_data_len = conn_param->private_data_len;

	/* setup our first outgoing iWarp send WQE (the IETF frame response) */
	wqe = &nesqp->hwqp.sq_vbase[0];

	if (raddr->sin_addr.s_addr != laddr->sin_addr.s_addr) {
		u64temp = (unsigned long)nesqp;
		nesibdev = nesvnic->nesibdev;
		nespd = nesqp->nespd;
		ibphysbuf.addr = nesqp->ietf_frame_pbase + mpa_frame_offset;
		ibphysbuf.size = buff_len;
		tagged_offset = (u64)(unsigned long)*start_buff;
		ibmr = nesibdev->ibdev.reg_phys_mr((struct ib_pd *)nespd,
						   &ibphysbuf, 1,
						   IB_ACCESS_LOCAL_WRITE,
						   &tagged_offset);
		if (!ibmr) {
			nes_debug(NES_DBG_CM, "Unable to register memory region"
				  "for lSMM for cm_node = %p \n",
				  cm_node);
			pci_free_consistent(nesdev->pcidev,
					    nesqp->private_data_len + nesqp->ietf_frame_size,
					    nesqp->ietf_frame, nesqp->ietf_frame_pbase);
			return -ENOMEM;
		}

		ibmr->pd = &nespd->ibpd;
		ibmr->device = nespd->ibpd.device;
		nesqp->lsmm_mr = ibmr;

		u64temp |= NES_SW_CONTEXT_ALIGN >> 1;
		set_wqe_64bit_value(wqe->wqe_words,
				    NES_IWARP_SQ_WQE_COMP_CTX_LOW_IDX,
				    u64temp);
		wqe->wqe_words[NES_IWARP_SQ_WQE_MISC_IDX] =
			cpu_to_le32(NES_IWARP_SQ_WQE_STREAMING |
				    NES_IWARP_SQ_WQE_WRPDU);
		wqe->wqe_words[NES_IWARP_SQ_WQE_TOTAL_PAYLOAD_IDX] =
			cpu_to_le32(buff_len);
		set_wqe_64bit_value(wqe->wqe_words,
				    NES_IWARP_SQ_WQE_FRAG0_LOW_IDX,
				    (u64)(unsigned long)(*start_buff));
		wqe->wqe_words[NES_IWARP_SQ_WQE_LENGTH0_IDX] =
			cpu_to_le32(buff_len);
		wqe->wqe_words[NES_IWARP_SQ_WQE_STAG0_IDX] = ibmr->lkey;
		if (nesqp->sq_kmapped) {
			nesqp->sq_kmapped = 0;
			kunmap(nesqp->page);
		}

		nesqp->nesqp_context->ird_ord_sizes |=
			cpu_to_le32(NES_QPCONTEXT_ORDIRD_LSMM_PRESENT |
				    NES_QPCONTEXT_ORDIRD_WRPDU);
	} else {
		nesqp->nesqp_context->ird_ord_sizes |=
			cpu_to_le32(NES_QPCONTEXT_ORDIRD_WRPDU);
	}
	nesqp->skip_lsmm = 1;

	/* Cache the cm_id in the qp */
	nesqp->cm_id = cm_id;
	cm_node->cm_id = cm_id;

	/*  nesqp->cm_node = (void *)cm_id->provider_data; */
	cm_id->provider_data = nesqp;
	nesqp->active_conn = 0;

	if (cm_node->state == NES_CM_STATE_TSA)
		nes_debug(NES_DBG_CM, "Already state = TSA for cm_node=%p\n",
			  cm_node);

	nes_cm_init_tsa_conn(nesqp, cm_node);

	nesqp->nesqp_context->tcpPorts[0] =
				cpu_to_le16(cm_node->mapped_loc_port);
	nesqp->nesqp_context->tcpPorts[1] =
				cpu_to_le16(cm_node->mapped_rem_port);

	nesqp->nesqp_context->ip0 = cpu_to_le32(cm_node->mapped_rem_addr);

	nesqp->nesqp_context->misc2 |= cpu_to_le32(
		(u32)PCI_FUNC(nesdev->pcidev->devfn) <<
		NES_QPCONTEXT_MISC2_SRC_IP_SHIFT);

	nesqp->nesqp_context->arp_index_vlan |=
		cpu_to_le32(nes_arp_table(nesdev,
					  le32_to_cpu(nesqp->nesqp_context->ip0), NULL,
					  NES_ARP_RESOLVE) << 16);

	nesqp->nesqp_context->ts_val_delta = cpu_to_le32(
		jiffies - nes_read_indexed(nesdev, NES_IDX_TCP_NOW));

	nesqp->nesqp_context->ird_index = cpu_to_le32(nesqp->hwqp.qp_id);

	nesqp->nesqp_context->ird_ord_sizes |= cpu_to_le32(
		((u32)1 << NES_QPCONTEXT_ORDIRD_IWARP_MODE_SHIFT));
	nesqp->nesqp_context->ird_ord_sizes |=
		cpu_to_le32((u32)cm_node->ord_size);

	memset(&nes_quad, 0, sizeof(nes_quad));
	nes_quad.DstIpAdrIndex =
		cpu_to_le32((u32)PCI_FUNC(nesdev->pcidev->devfn) << 24);
	nes_quad.SrcIpadr = htonl(cm_node->mapped_rem_addr);
	nes_quad.TcpPorts[0] = htons(cm_node->mapped_rem_port);
	nes_quad.TcpPorts[1] = htons(cm_node->mapped_loc_port);

	/* Produce hash key */
	crc_value = get_crc_value(&nes_quad);
	nesqp->hte_index = cpu_to_be32(crc_value ^ 0xffffffff);
	nes_debug(NES_DBG_CM, "HTE Index = 0x%08X, CRC = 0x%08X\n",
		  nesqp->hte_index, nesqp->hte_index & adapter->hte_index_mask);

	nesqp->hte_index &= adapter->hte_index_mask;
	nesqp->nesqp_context->hte_index = cpu_to_le32(nesqp->hte_index);

	cm_node->cm_core->api->accelerated(cm_node->cm_core, cm_node);

	nes_debug(NES_DBG_CM, "QP%u, Destination IP = 0x%08X:0x%04X, local = "
		  "0x%08X:0x%04X, rcv_nxt=0x%08X, snd_nxt=0x%08X, mpa + "
		  "private data length=%u.\n", nesqp->hwqp.qp_id,
		  ntohl(raddr->sin_addr.s_addr), ntohs(raddr->sin_port),
		  ntohl(laddr->sin_addr.s_addr), ntohs(laddr->sin_port),
		  le32_to_cpu(nesqp->nesqp_context->rcv_nxt),
		  le32_to_cpu(nesqp->nesqp_context->snd_nxt),
		  buff_len);

	/* notify OF layer that accept event was successful */
	cm_id->add_ref(cm_id);
	nes_add_ref(&nesqp->ibqp);

	cm_event.event = IW_CM_EVENT_ESTABLISHED;
	cm_event.status = 0;
	cm_event.provider_data = (void *)nesqp;
	cm_event.local_addr = cm_id->local_addr;
	cm_event.remote_addr = cm_id->remote_addr;
	cm_event.private_data = NULL;
	cm_event.private_data_len = 0;
	cm_event.ird = cm_node->ird_size;
	cm_event.ord = cm_node->ord_size;

	ret = cm_id->event_handler(cm_id, &cm_event);
	attr.qp_state = IB_QPS_RTS;
	nes_modify_qp(&nesqp->ibqp, &attr, IB_QP_STATE, NULL);
	if (cm_node->loopbackpartner) {
		cm_node->loopbackpartner->mpa_frame_size =
			nesqp->private_data_len;
		/* copy entire MPA frame to our cm_node's frame */
		memcpy(cm_node->loopbackpartner->mpa_frame_buf,
		       conn_param->private_data, conn_param->private_data_len);
		create_event(cm_node->loopbackpartner, NES_CM_EVENT_CONNECTED);
	}
	if (ret)
		printk(KERN_ERR "%s[%u] OFA CM event_handler returned, "
		       "ret=%d\n", __func__, __LINE__, ret);

	return 0;
}


/**
 * nes_reject
 */
int nes_reject(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len)
{
	struct nes_cm_node *cm_node;
	struct nes_cm_node *loopback;
	struct nes_cm_core *cm_core;
	u8 *start_buff;

	atomic_inc(&cm_rejects);
	cm_node = (struct nes_cm_node *)cm_id->provider_data;
	loopback = cm_node->loopbackpartner;
	cm_core = cm_node->cm_core;
	cm_node->cm_id = cm_id;

	if (pdata_len + sizeof(struct ietf_mpa_v2) > MAX_CM_BUFFER)
		return -EINVAL;

	if (loopback) {
		memcpy(&loopback->mpa_frame.priv_data, pdata, pdata_len);
		loopback->mpa_frame.priv_data_len = pdata_len;
		loopback->mpa_frame_size = pdata_len;
	} else {
		start_buff = &cm_node->mpa_frame_buf[0] + sizeof(struct ietf_mpa_v2);
		cm_node->mpa_frame_size = pdata_len;
		memcpy(start_buff, pdata, pdata_len);
	}
	return cm_core->api->reject(cm_core, cm_node);
}


/**
 * nes_connect
 * setup and launch cm connect node
 */
int nes_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	struct ib_qp *ibqp;
	struct nes_qp *nesqp;
	struct nes_vnic *nesvnic;
	struct nes_device *nesdev;
	struct nes_cm_node *cm_node;
	struct nes_cm_info cm_info;
	int apbvt_set = 0;
	struct sockaddr_in *laddr = (struct sockaddr_in *)&cm_id->local_addr;
	struct sockaddr_in *raddr = (struct sockaddr_in *)&cm_id->remote_addr;
	struct iwpm_dev_data pm_reg_msg;
	struct iwpm_sa_data pm_msg;
	int iwpm_err = 0;

	if (cm_id->remote_addr.ss_family != AF_INET)
		return -ENOSYS;
	ibqp = nes_get_qp(cm_id->device, conn_param->qpn);
	if (!ibqp)
		return -EINVAL;
	nesqp = to_nesqp(ibqp);
	if (!nesqp)
		return -EINVAL;
	nesvnic = to_nesvnic(nesqp->ibqp.device);
	if (!nesvnic)
		return -EINVAL;
	nesdev = nesvnic->nesdev;
	if (!nesdev)
		return -EINVAL;

	if (!laddr->sin_port || !raddr->sin_port)
		return -EINVAL;

	nes_debug(NES_DBG_CM, "QP%u, current IP = 0x%08X, Destination IP = "
		  "0x%08X:0x%04X, local = 0x%08X:0x%04X.\n", nesqp->hwqp.qp_id,
		  ntohl(nesvnic->local_ipaddr), ntohl(raddr->sin_addr.s_addr),
		  ntohs(raddr->sin_port), ntohl(laddr->sin_addr.s_addr),
		  ntohs(laddr->sin_port));

	atomic_inc(&cm_connects);
	nesqp->active_conn = 1;

	/* cache the cm_id in the qp */
	nesqp->cm_id = cm_id;
	cm_id->provider_data = nesqp;
	nesqp->private_data_len = conn_param->private_data_len;

	nes_debug(NES_DBG_CM, "requested ord = 0x%08X.\n", (u32)conn_param->ord);
	nes_debug(NES_DBG_CM, "mpa private data len =%u\n",
		  conn_param->private_data_len);

	/* set up the connection params for the node */
	cm_info.loc_addr = ntohl(laddr->sin_addr.s_addr);
	cm_info.loc_port = ntohs(laddr->sin_port);
	cm_info.rem_addr = ntohl(raddr->sin_addr.s_addr);
	cm_info.rem_port = ntohs(raddr->sin_port);
	cm_info.cm_id = cm_id;
	cm_info.conn_type = NES_CM_IWARP_CONN_TYPE;

	/* No port mapper available, go with the specified peer information */
	cm_info.mapped_loc_addr = cm_info.loc_addr;
	cm_info.mapped_loc_port = cm_info.loc_port;
	cm_info.mapped_rem_addr = cm_info.rem_addr;
	cm_info.mapped_rem_port = cm_info.rem_port;

	nes_form_reg_msg(nesvnic, &pm_reg_msg);
	iwpm_err = iwpm_register_pid(&pm_reg_msg, RDMA_NL_NES);
	if (iwpm_err) {
		nes_debug(NES_DBG_NLMSG,
			"Port Mapper reg pid fail (err = %d).\n", iwpm_err);
	}
	if (iwpm_valid_pid() && !iwpm_err) {
		nes_form_pm_msg(&cm_info, &pm_msg);
		iwpm_err = iwpm_add_and_query_mapping(&pm_msg, RDMA_NL_NES);
		if (iwpm_err)
			nes_debug(NES_DBG_NLMSG,
			"Port Mapper query fail (err = %d).\n", iwpm_err);
		else
			nes_record_pm_msg(&cm_info, &pm_msg);
	}

	if (laddr->sin_addr.s_addr != raddr->sin_addr.s_addr) {
		nes_manage_apbvt(nesvnic, cm_info.mapped_loc_port,
			PCI_FUNC(nesdev->pcidev->devfn), NES_MANAGE_APBVT_ADD);
		apbvt_set = 1;
	}

	if (nes_create_mapinfo(&cm_info))
		return -ENOMEM;

	cm_id->add_ref(cm_id);

	/* create a connect CM node connection */
	cm_node = g_cm_core->api->connect(g_cm_core, nesvnic,
					  conn_param->private_data_len, (void *)conn_param->private_data,
					  &cm_info);
	if (!cm_node) {
		if (apbvt_set)
			nes_manage_apbvt(nesvnic, cm_info.mapped_loc_port,
					 PCI_FUNC(nesdev->pcidev->devfn),
					 NES_MANAGE_APBVT_DEL);

		nes_debug(NES_DBG_NLMSG, "Delete mapped_loc_port = %04X\n",
				cm_info.mapped_loc_port);
		nes_remove_mapinfo(cm_info.loc_addr, cm_info.loc_port,
			cm_info.mapped_loc_addr, cm_info.mapped_loc_port);
		cm_id->rem_ref(cm_id);
		return -ENOMEM;
	}

	record_ird_ord(cm_node, (u16)conn_param->ird, (u16)conn_param->ord);
	if (cm_node->send_rdma0_op == SEND_RDMA_READ_ZERO &&
				cm_node->ord_size == 0)
		cm_node->ord_size = 1;

	cm_node->apbvt_set = apbvt_set;
	cm_node->tos = cm_id->tos;
	nesqp->cm_node = cm_node;
	cm_node->nesqp = nesqp;
	nes_add_ref(&nesqp->ibqp);

	return 0;
}


/**
 * nes_create_listen
 */
int nes_create_listen(struct iw_cm_id *cm_id, int backlog)
{
	struct nes_vnic *nesvnic;
	struct nes_cm_listener *cm_node;
	struct nes_cm_info cm_info;
	int err;
	struct sockaddr_in *laddr = (struct sockaddr_in *)&cm_id->local_addr;

	nes_debug(NES_DBG_CM, "cm_id = %p, local port = 0x%04X.\n",
		  cm_id, ntohs(laddr->sin_port));

	if (cm_id->local_addr.ss_family != AF_INET)
		return -ENOSYS;
	nesvnic = to_nesvnic(cm_id->device);
	if (!nesvnic)
		return -EINVAL;

	nes_debug(NES_DBG_CM, "nesvnic=%p, netdev=%p, %s\n",
			nesvnic, nesvnic->netdev, nesvnic->netdev->name);

	nes_debug(NES_DBG_CM, "nesvnic->local_ipaddr=0x%08x, sin_addr.s_addr=0x%08x\n",
			nesvnic->local_ipaddr, laddr->sin_addr.s_addr);

	/* setup listen params in our api call struct */
	cm_info.loc_addr = ntohl(nesvnic->local_ipaddr);
	cm_info.loc_port = ntohs(laddr->sin_port);
	cm_info.backlog = backlog;
	cm_info.cm_id = cm_id;

	cm_info.conn_type = NES_CM_IWARP_CONN_TYPE;

	/* No port mapper available, go with the specified info */
	cm_info.mapped_loc_addr = cm_info.loc_addr;
	cm_info.mapped_loc_port = cm_info.loc_port;

	cm_node = g_cm_core->api->listen(g_cm_core, nesvnic, &cm_info);
	if (!cm_node) {
		printk(KERN_ERR "%s[%u] Error returned from listen API call\n",
		       __func__, __LINE__);
		return -ENOMEM;
	}

	cm_id->provider_data = cm_node;
	cm_node->tos = cm_id->tos;

	if (!cm_node->reused_node) {
		if (nes_create_mapinfo(&cm_info))
			return -ENOMEM;

		err = nes_manage_apbvt(nesvnic, cm_node->mapped_loc_port,
				       PCI_FUNC(nesvnic->nesdev->pcidev->devfn),
				       NES_MANAGE_APBVT_ADD);
		if (err) {
			printk(KERN_ERR "nes_manage_apbvt call returned %d.\n",
			       err);
			g_cm_core->api->stop_listener(g_cm_core, (void *)cm_node);
			return err;
		}
		atomic_inc(&cm_listens_created);
	}

	cm_id->add_ref(cm_id);
	cm_id->provider_data = (void *)cm_node;


	return 0;
}


/**
 * nes_destroy_listen
 */
int nes_destroy_listen(struct iw_cm_id *cm_id)
{
	if (cm_id->provider_data)
		g_cm_core->api->stop_listener(g_cm_core, cm_id->provider_data);
	else
		nes_debug(NES_DBG_CM, "cm_id->provider_data was NULL\n");

	cm_id->rem_ref(cm_id);

	return 0;
}


/**
 * nes_cm_recv
 */
int nes_cm_recv(struct sk_buff *skb, struct net_device *netdevice)
{
	int rc = 0;

	cm_packets_received++;
	if ((g_cm_core) && (g_cm_core->api))
		rc = g_cm_core->api->recv_pkt(g_cm_core, netdev_priv(netdevice), skb);
	else
		nes_debug(NES_DBG_CM, "Unable to process packet for CM,"
			  " cm is not setup properly.\n");

	return rc;
}


/**
 * nes_cm_start
 * Start and init a cm core module
 */
int nes_cm_start(void)
{
	nes_debug(NES_DBG_CM, "\n");
	/* create the primary CM core, pass this handle to subsequent core inits */
	g_cm_core = nes_cm_alloc_core();
	if (g_cm_core)
		return 0;
	else
		return -ENOMEM;
}


/**
 * nes_cm_stop
 * stop and dealloc all cm core instances
 */
int nes_cm_stop(void)
{
	g_cm_core->api->destroy_cm_core(g_cm_core);
	return 0;
}


/**
 * cm_event_connected
 * handle a connected event, setup QPs and HW
 */
static void cm_event_connected(struct nes_cm_event *event)
{
	struct nes_qp *nesqp;
	struct nes_vnic *nesvnic;
	struct nes_device *nesdev;
	struct nes_cm_node *cm_node;
	struct nes_adapter *nesadapter;
	struct ib_qp_attr attr;
	struct iw_cm_id *cm_id;
	struct iw_cm_event cm_event;
	struct nes_v4_quad nes_quad;
	u32 crc_value;
	int ret;
	struct sockaddr_in *laddr;
	struct sockaddr_in *raddr;
	struct sockaddr_in *cm_event_laddr;

	/* get all our handles */
	cm_node = event->cm_node;
	cm_id = cm_node->cm_id;
	nes_debug(NES_DBG_CM, "cm_event_connected - %p - cm_id = %p\n", cm_node, cm_id);
	nesqp = (struct nes_qp *)cm_id->provider_data;
	nesvnic = to_nesvnic(nesqp->ibqp.device);
	nesdev = nesvnic->nesdev;
	nesadapter = nesdev->nesadapter;
	laddr = (struct sockaddr_in *)&cm_id->local_addr;
	raddr = (struct sockaddr_in *)&cm_id->remote_addr;
	cm_event_laddr = (struct sockaddr_in *)&cm_event.local_addr;

	if (nesqp->destroyed)
		return;
	atomic_inc(&cm_connecteds);
	nes_debug(NES_DBG_CM, "QP%u attempting to connect to  0x%08X:0x%04X on"
		  " local port 0x%04X. jiffies = %lu.\n",
		  nesqp->hwqp.qp_id, ntohl(raddr->sin_addr.s_addr),
		  ntohs(raddr->sin_port), ntohs(laddr->sin_port), jiffies);

	nes_cm_init_tsa_conn(nesqp, cm_node);

	/* set the QP tsa context */
	nesqp->nesqp_context->tcpPorts[0] =
			cpu_to_le16(cm_node->mapped_loc_port);
	nesqp->nesqp_context->tcpPorts[1] =
			cpu_to_le16(cm_node->mapped_rem_port);
	nesqp->nesqp_context->ip0 = cpu_to_le32(cm_node->mapped_rem_addr);

	nesqp->nesqp_context->misc2 |= cpu_to_le32(
			(u32)PCI_FUNC(nesdev->pcidev->devfn) <<
			NES_QPCONTEXT_MISC2_SRC_IP_SHIFT);
	nesqp->nesqp_context->arp_index_vlan |= cpu_to_le32(
			nes_arp_table(nesdev,
			le32_to_cpu(nesqp->nesqp_context->ip0),
			NULL, NES_ARP_RESOLVE) << 16);
	nesqp->nesqp_context->ts_val_delta = cpu_to_le32(
			jiffies - nes_read_indexed(nesdev, NES_IDX_TCP_NOW));
	nesqp->nesqp_context->ird_index = cpu_to_le32(nesqp->hwqp.qp_id);
	nesqp->nesqp_context->ird_ord_sizes |=
			cpu_to_le32((u32)1 <<
			NES_QPCONTEXT_ORDIRD_IWARP_MODE_SHIFT);
	nesqp->nesqp_context->ird_ord_sizes |=
			cpu_to_le32((u32)cm_node->ord_size);

	/* Adjust tail for not having a LSMM */
	/*nesqp->hwqp.sq_tail = 1;*/

	build_rdma0_msg(cm_node, &nesqp);

	nes_write32(nesdev->regs + NES_WQE_ALLOC,
		    (1 << 24) | 0x00800000 | nesqp->hwqp.qp_id);

	memset(&nes_quad, 0, sizeof(nes_quad));

	nes_quad.DstIpAdrIndex =
		cpu_to_le32((u32)PCI_FUNC(nesdev->pcidev->devfn) << 24);
	nes_quad.SrcIpadr = htonl(cm_node->mapped_rem_addr);
	nes_quad.TcpPorts[0] = htons(cm_node->mapped_rem_port);
	nes_quad.TcpPorts[1] = htons(cm_node->mapped_loc_port);

	/* Produce hash key */
	crc_value = get_crc_value(&nes_quad);
	nesqp->hte_index = cpu_to_be32(crc_value ^ 0xffffffff);
	nes_debug(NES_DBG_CM, "HTE Index = 0x%08X, After CRC = 0x%08X\n",
		  nesqp->hte_index, nesqp->hte_index & nesadapter->hte_index_mask);

	nesqp->hte_index &= nesadapter->hte_index_mask;
	nesqp->nesqp_context->hte_index = cpu_to_le32(nesqp->hte_index);

	nesqp->ietf_frame = &cm_node->mpa_frame;
	nesqp->private_data_len = (u8)cm_node->mpa_frame_size;
	cm_node->cm_core->api->accelerated(cm_node->cm_core, cm_node);

	/* notify OF layer we successfully created the requested connection */
	cm_event.event = IW_CM_EVENT_CONNECT_REPLY;
	cm_event.status = 0;
	cm_event.provider_data = cm_id->provider_data;
	cm_event_laddr->sin_family = AF_INET;
	cm_event_laddr->sin_port = laddr->sin_port;
	cm_event.remote_addr = cm_id->remote_addr;

	cm_event.private_data = (void *)event->cm_node->mpa_frame_buf;
	cm_event.private_data_len = (u8)event->cm_node->mpa_frame_size;
	cm_event.ird = cm_node->ird_size;
	cm_event.ord = cm_node->ord_size;

	cm_event_laddr->sin_addr.s_addr = htonl(event->cm_info.rem_addr);
	ret = cm_id->event_handler(cm_id, &cm_event);
	nes_debug(NES_DBG_CM, "OFA CM event_handler returned, ret=%d\n", ret);

	if (ret)
		printk(KERN_ERR "%s[%u] OFA CM event_handler returned, "
		       "ret=%d\n", __func__, __LINE__, ret);
	attr.qp_state = IB_QPS_RTS;
	nes_modify_qp(&nesqp->ibqp, &attr, IB_QP_STATE, NULL);

	nes_debug(NES_DBG_CM, "Exiting connect thread for QP%u. jiffies = "
		  "%lu\n", nesqp->hwqp.qp_id, jiffies);

	return;
}


/**
 * cm_event_connect_error
 */
static void cm_event_connect_error(struct nes_cm_event *event)
{
	struct nes_qp *nesqp;
	struct iw_cm_id *cm_id;
	struct iw_cm_event cm_event;
	/* struct nes_cm_info cm_info; */
	int ret;

	if (!event->cm_node)
		return;

	cm_id = event->cm_node->cm_id;
	if (!cm_id)
		return;

	nes_debug(NES_DBG_CM, "cm_node=%p, cm_id=%p\n", event->cm_node, cm_id);
	nesqp = cm_id->provider_data;

	if (!nesqp)
		return;

	/* notify OF layer about this connection error event */
	/* cm_id->rem_ref(cm_id); */
	nesqp->cm_id = NULL;
	cm_id->provider_data = NULL;
	cm_event.event = IW_CM_EVENT_CONNECT_REPLY;
	cm_event.status = -ECONNRESET;
	cm_event.provider_data = cm_id->provider_data;
	cm_event.local_addr = cm_id->local_addr;
	cm_event.remote_addr = cm_id->remote_addr;
	cm_event.private_data = NULL;
	cm_event.private_data_len = 0;

#ifdef CONFIG_INFINIBAND_NES_DEBUG
	{
		struct sockaddr_in *cm_event_laddr = (struct sockaddr_in *)
						     &cm_event.local_addr;
		struct sockaddr_in *cm_event_raddr = (struct sockaddr_in *)
						     &cm_event.remote_addr;
		nes_debug(NES_DBG_CM, "call CM_EVENT REJECTED, local_addr=%08x, remote_addr=%08x\n",
			  cm_event_laddr->sin_addr.s_addr, cm_event_raddr->sin_addr.s_addr);
	}
#endif

	ret = cm_id->event_handler(cm_id, &cm_event);
	nes_debug(NES_DBG_CM, "OFA CM event_handler returned, ret=%d\n", ret);
	if (ret)
		printk(KERN_ERR "%s[%u] OFA CM event_handler returned, "
		       "ret=%d\n", __func__, __LINE__, ret);
	cm_id->rem_ref(cm_id);

	rem_ref_cm_node(event->cm_node->cm_core, event->cm_node);
	return;
}


/**
 * cm_event_reset
 */
static void cm_event_reset(struct nes_cm_event *event)
{
	struct nes_qp *nesqp;
	struct iw_cm_id *cm_id;
	struct iw_cm_event cm_event;
	/* struct nes_cm_info cm_info; */
	int ret;

	if (!event->cm_node)
		return;

	if (!event->cm_node->cm_id)
		return;

	cm_id = event->cm_node->cm_id;

	nes_debug(NES_DBG_CM, "%p - cm_id = %p\n", event->cm_node, cm_id);
	nesqp = cm_id->provider_data;
	if (!nesqp)
		return;

	nesqp->cm_id = NULL;
	/* cm_id->provider_data = NULL; */
	cm_event.event = IW_CM_EVENT_DISCONNECT;
	cm_event.status = -ECONNRESET;
	cm_event.provider_data = cm_id->provider_data;
	cm_event.local_addr = cm_id->local_addr;
	cm_event.remote_addr = cm_id->remote_addr;
	cm_event.private_data = NULL;
	cm_event.private_data_len = 0;

	cm_id->add_ref(cm_id);
	ret = cm_id->event_handler(cm_id, &cm_event);
	atomic_inc(&cm_closes);
	cm_event.event = IW_CM_EVENT_CLOSE;
	cm_event.status = 0;
	cm_event.provider_data = cm_id->provider_data;
	cm_event.local_addr = cm_id->local_addr;
	cm_event.remote_addr = cm_id->remote_addr;
	cm_event.private_data = NULL;
	cm_event.private_data_len = 0;
	nes_debug(NES_DBG_CM, "NODE %p Generating CLOSE\n", event->cm_node);
	ret = cm_id->event_handler(cm_id, &cm_event);

	nes_debug(NES_DBG_CM, "OFA CM event_handler returned, ret=%d\n", ret);


	/* notify OF layer about this connection error event */
	cm_id->rem_ref(cm_id);

	return;
}


/**
 * cm_event_mpa_req
 */
static void cm_event_mpa_req(struct nes_cm_event *event)
{
	struct iw_cm_id *cm_id;
	struct iw_cm_event cm_event;
	int ret;
	struct nes_cm_node *cm_node;
	struct sockaddr_in *cm_event_laddr = (struct sockaddr_in *)
					     &cm_event.local_addr;
	struct sockaddr_in *cm_event_raddr = (struct sockaddr_in *)
					     &cm_event.remote_addr;

	cm_node = event->cm_node;
	if (!cm_node)
		return;
	cm_id = cm_node->cm_id;

	atomic_inc(&cm_connect_reqs);
	nes_debug(NES_DBG_CM, "cm_node = %p - cm_id = %p, jiffies = %lu\n",
		  cm_node, cm_id, jiffies);

	cm_event.event = IW_CM_EVENT_CONNECT_REQUEST;
	cm_event.status = 0;
	cm_event.provider_data = (void *)cm_node;

	cm_event_laddr->sin_family = AF_INET;
	cm_event_laddr->sin_port = htons(event->cm_info.loc_port);
	cm_event_laddr->sin_addr.s_addr = htonl(event->cm_info.loc_addr);

	cm_event_raddr->sin_family = AF_INET;
	cm_event_raddr->sin_port = htons(event->cm_info.rem_port);
	cm_event_raddr->sin_addr.s_addr = htonl(event->cm_info.rem_addr);
	cm_event.private_data = cm_node->mpa_frame_buf;
	cm_event.private_data_len = (u8)cm_node->mpa_frame_size;
	if (cm_node->mpa_frame_rev == IETF_MPA_V1) {
		cm_event.ird = NES_MAX_IRD;
		cm_event.ord = NES_MAX_ORD;
	} else {
	cm_event.ird = cm_node->ird_size;
	cm_event.ord = cm_node->ord_size;
	}

	ret = cm_id->event_handler(cm_id, &cm_event);
	if (ret)
		printk(KERN_ERR "%s[%u] OFA CM event_handler returned, ret=%d\n",
		       __func__, __LINE__, ret);
	return;
}


static void cm_event_mpa_reject(struct nes_cm_event *event)
{
	struct iw_cm_id *cm_id;
	struct iw_cm_event cm_event;
	struct nes_cm_node *cm_node;
	int ret;
	struct sockaddr_in *cm_event_laddr = (struct sockaddr_in *)
					     &cm_event.local_addr;
	struct sockaddr_in *cm_event_raddr = (struct sockaddr_in *)
					     &cm_event.remote_addr;

	cm_node = event->cm_node;
	if (!cm_node)
		return;
	cm_id = cm_node->cm_id;

	atomic_inc(&cm_connect_reqs);
	nes_debug(NES_DBG_CM, "cm_node = %p - cm_id = %p, jiffies = %lu\n",
		  cm_node, cm_id, jiffies);

	cm_event.event = IW_CM_EVENT_CONNECT_REPLY;
	cm_event.status = -ECONNREFUSED;
	cm_event.provider_data = cm_id->provider_data;

	cm_event_laddr->sin_family = AF_INET;
	cm_event_laddr->sin_port = htons(event->cm_info.loc_port);
	cm_event_laddr->sin_addr.s_addr = htonl(event->cm_info.loc_addr);

	cm_event_raddr->sin_family = AF_INET;
	cm_event_raddr->sin_port = htons(event->cm_info.rem_port);
	cm_event_raddr->sin_addr.s_addr = htonl(event->cm_info.rem_addr);

	cm_event.private_data = cm_node->mpa_frame_buf;
	cm_event.private_data_len = (u8)cm_node->mpa_frame_size;

	nes_debug(NES_DBG_CM, "call CM_EVENT_MPA_REJECTED, local_addr=%08x, "
		  "remove_addr=%08x\n",
		  cm_event_laddr->sin_addr.s_addr,
		  cm_event_raddr->sin_addr.s_addr);

	ret = cm_id->event_handler(cm_id, &cm_event);
	if (ret)
		printk(KERN_ERR "%s[%u] OFA CM event_handler returned, ret=%d\n",
		       __func__, __LINE__, ret);

	return;
}


static void nes_cm_event_handler(struct work_struct *);

/**
 * nes_cm_post_event
 * post an event to the cm event handler
 */
static int nes_cm_post_event(struct nes_cm_event *event)
{
	atomic_inc(&event->cm_node->cm_core->events_posted);
	add_ref_cm_node(event->cm_node);
	event->cm_info.cm_id->add_ref(event->cm_info.cm_id);
	INIT_WORK(&event->event_work, nes_cm_event_handler);
	nes_debug(NES_DBG_CM, "cm_node=%p queue_work, event=%p\n",
		  event->cm_node, event);

	queue_work(event->cm_node->cm_core->event_wq, &event->event_work);

	nes_debug(NES_DBG_CM, "Exit\n");
	return 0;
}


/**
 * nes_cm_event_handler
 * worker function to handle cm events
 * will free instance of nes_cm_event
 */
static void nes_cm_event_handler(struct work_struct *work)
{
	struct nes_cm_event *event = container_of(work, struct nes_cm_event,
						  event_work);
	struct nes_cm_core *cm_core;

	if ((!event) || (!event->cm_node) || (!event->cm_node->cm_core))
		return;

	cm_core = event->cm_node->cm_core;
	nes_debug(NES_DBG_CM, "event=%p, event->type=%u, events posted=%u\n",
		  event, event->type, atomic_read(&cm_core->events_posted));

	switch (event->type) {
	case NES_CM_EVENT_MPA_REQ:
		cm_event_mpa_req(event);
		nes_debug(NES_DBG_CM, "cm_node=%p CM Event: MPA REQUEST\n",
			  event->cm_node);
		break;
	case NES_CM_EVENT_RESET:
		nes_debug(NES_DBG_CM, "cm_node = %p CM Event: RESET\n",
			  event->cm_node);
		cm_event_reset(event);
		break;
	case NES_CM_EVENT_CONNECTED:
		if ((!event->cm_node->cm_id) ||
		    (event->cm_node->state != NES_CM_STATE_TSA))
			break;
		cm_event_connected(event);
		nes_debug(NES_DBG_CM, "CM Event: CONNECTED\n");
		break;
	case NES_CM_EVENT_MPA_REJECT:
		if ((!event->cm_node->cm_id) ||
		    (event->cm_node->state == NES_CM_STATE_TSA))
			break;
		cm_event_mpa_reject(event);
		nes_debug(NES_DBG_CM, "CM Event: REJECT\n");
		break;

	case NES_CM_EVENT_ABORTED:
		if ((!event->cm_node->cm_id) ||
		    (event->cm_node->state == NES_CM_STATE_TSA))
			break;
		cm_event_connect_error(event);
		nes_debug(NES_DBG_CM, "CM Event: ABORTED\n");
		break;
	case NES_CM_EVENT_DROPPED_PKT:
		nes_debug(NES_DBG_CM, "CM Event: DROPPED PKT\n");
		break;
	default:
		nes_debug(NES_DBG_CM, "CM Event: UNKNOWN EVENT TYPE\n");
		break;
	}

	atomic_dec(&cm_core->events_posted);
	event->cm_info.cm_id->rem_ref(event->cm_info.cm_id);
	rem_ref_cm_node(cm_core, event->cm_node);
	kfree(event);

	return;
}
