/*******************************************************************************
*
* Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
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
*******************************************************************************/

#include <linux/atomic.h>
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
#include <net/arp.h>
#include <net/ndisc.h>
#include <net/neighbour.h>
#include <net/route.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/ip_fib.h>
#include <net/tcp.h>
#include <asm/checksum.h>

#include "i40iw.h"

static void i40iw_rem_ref_cm_node(struct i40iw_cm_node *);
static void i40iw_cm_post_event(struct i40iw_cm_event *event);
static void i40iw_disconnect_worker(struct work_struct *work);

/**
 * i40iw_free_sqbuf - put back puda buffer if refcount = 0
 * @vsi: pointer to vsi structure
 * @buf: puda buffer to free
 */
void i40iw_free_sqbuf(struct i40iw_sc_vsi *vsi, void *bufp)
{
	struct i40iw_puda_buf *buf = (struct i40iw_puda_buf *)bufp;
	struct i40iw_puda_rsrc *ilq = vsi->ilq;

	if (!atomic_dec_return(&buf->refcount))
		i40iw_puda_ret_bufpool(ilq, buf);
}

/**
 * i40iw_derive_hw_ird_setting - Calculate IRD
 *
 * @cm_ird: IRD of connection's node
 *
 * The ird from the connection is rounded to a supported HW
 * setting (2,8,32,64) and then encoded for ird_size field of
 * qp_ctx
 */
static u8 i40iw_derive_hw_ird_setting(u16 cm_ird)
{
	u8 encoded_ird_size;
	u8 pof2_cm_ird = 1;

	/* round-off to next powerof2 */
	while (pof2_cm_ird < cm_ird)
		pof2_cm_ird *= 2;

	/* ird_size field is encoded in qp_ctx */
	switch (pof2_cm_ird) {
	case I40IW_HW_IRD_SETTING_64:
		encoded_ird_size = 3;
		break;
	case I40IW_HW_IRD_SETTING_32:
	case I40IW_HW_IRD_SETTING_16:
		encoded_ird_size = 2;
		break;
	case I40IW_HW_IRD_SETTING_8:
	case I40IW_HW_IRD_SETTING_4:
		encoded_ird_size = 1;
		break;
	case I40IW_HW_IRD_SETTING_2:
	default:
		encoded_ird_size = 0;
		break;
	}
	return encoded_ird_size;
}

/**
 * i40iw_record_ird_ord - Record IRD/ORD passed in
 * @cm_node: connection's node
 * @conn_ird: connection IRD
 * @conn_ord: connection ORD
 */
static void i40iw_record_ird_ord(struct i40iw_cm_node *cm_node, u16 conn_ird, u16 conn_ord)
{
	if (conn_ird > I40IW_MAX_IRD_SIZE)
		conn_ird = I40IW_MAX_IRD_SIZE;

	if (conn_ord > I40IW_MAX_ORD_SIZE)
		conn_ord = I40IW_MAX_ORD_SIZE;

	cm_node->ird_size = conn_ird;
	cm_node->ord_size = conn_ord;
}

/**
 * i40iw_copy_ip_ntohl - change network to host ip
 * @dst: host ip
 * @src: big endian
 */
void i40iw_copy_ip_ntohl(u32 *dst, __be32 *src)
{
	*dst++ = ntohl(*src++);
	*dst++ = ntohl(*src++);
	*dst++ = ntohl(*src++);
	*dst = ntohl(*src);
}

/**
 * i40iw_copy_ip_htonl - change host addr to network ip
 * @dst: host ip
 * @src: little endian
 */
static inline void i40iw_copy_ip_htonl(__be32 *dst, u32 *src)
{
	*dst++ = htonl(*src++);
	*dst++ = htonl(*src++);
	*dst++ = htonl(*src++);
	*dst = htonl(*src);
}

/**
 * i40iw_fill_sockaddr4 - get addr info for passive connection
 * @cm_node: connection's node
 * @event: upper layer's cm event
 */
static inline void i40iw_fill_sockaddr4(struct i40iw_cm_node *cm_node,
					struct iw_cm_event *event)
{
	struct sockaddr_in *laddr = (struct sockaddr_in *)&event->local_addr;
	struct sockaddr_in *raddr = (struct sockaddr_in *)&event->remote_addr;

	laddr->sin_family = AF_INET;
	raddr->sin_family = AF_INET;

	laddr->sin_port = htons(cm_node->loc_port);
	raddr->sin_port = htons(cm_node->rem_port);

	laddr->sin_addr.s_addr = htonl(cm_node->loc_addr[0]);
	raddr->sin_addr.s_addr = htonl(cm_node->rem_addr[0]);
}

/**
 * i40iw_fill_sockaddr6 - get ipv6 addr info for passive side
 * @cm_node: connection's node
 * @event: upper layer's cm event
 */
static inline void i40iw_fill_sockaddr6(struct i40iw_cm_node *cm_node,
					struct iw_cm_event *event)
{
	struct sockaddr_in6 *laddr6 = (struct sockaddr_in6 *)&event->local_addr;
	struct sockaddr_in6 *raddr6 = (struct sockaddr_in6 *)&event->remote_addr;

	laddr6->sin6_family = AF_INET6;
	raddr6->sin6_family = AF_INET6;

	laddr6->sin6_port = htons(cm_node->loc_port);
	raddr6->sin6_port = htons(cm_node->rem_port);

	i40iw_copy_ip_htonl(laddr6->sin6_addr.in6_u.u6_addr32,
			    cm_node->loc_addr);
	i40iw_copy_ip_htonl(raddr6->sin6_addr.in6_u.u6_addr32,
			    cm_node->rem_addr);
}

/**
 * i40iw_get_addr_info
 * @cm_node: contains ip/tcp info
 * @cm_info: to get a copy of the cm_node ip/tcp info
*/
static void i40iw_get_addr_info(struct i40iw_cm_node *cm_node,
				struct i40iw_cm_info *cm_info)
{
	cm_info->ipv4 = cm_node->ipv4;
	cm_info->vlan_id = cm_node->vlan_id;
	memcpy(cm_info->loc_addr, cm_node->loc_addr, sizeof(cm_info->loc_addr));
	memcpy(cm_info->rem_addr, cm_node->rem_addr, sizeof(cm_info->rem_addr));
	cm_info->loc_port = cm_node->loc_port;
	cm_info->rem_port = cm_node->rem_port;
	cm_info->user_pri = cm_node->user_pri;
}

/**
 * i40iw_get_cmevent_info - for cm event upcall
 * @cm_node: connection's node
 * @cm_id: upper layers cm struct for the event
 * @event: upper layer's cm event
 */
static inline void i40iw_get_cmevent_info(struct i40iw_cm_node *cm_node,
					  struct iw_cm_id *cm_id,
					  struct iw_cm_event *event)
{
	memcpy(&event->local_addr, &cm_id->m_local_addr,
	       sizeof(event->local_addr));
	memcpy(&event->remote_addr, &cm_id->m_remote_addr,
	       sizeof(event->remote_addr));
	if (cm_node) {
		event->private_data = (void *)cm_node->pdata_buf;
		event->private_data_len = (u8)cm_node->pdata.size;
		event->ird = cm_node->ird_size;
		event->ord = cm_node->ord_size;
	}
}

/**
 * i40iw_send_cm_event - upcall cm's event handler
 * @cm_node: connection's node
 * @cm_id: upper layer's cm info struct
 * @type: Event type to indicate
 * @status: status for the event type
 */
static int i40iw_send_cm_event(struct i40iw_cm_node *cm_node,
			       struct iw_cm_id *cm_id,
			       enum iw_cm_event_type type,
			       int status)
{
	struct iw_cm_event event;

	memset(&event, 0, sizeof(event));
	event.event = type;
	event.status = status;
	switch (type) {
	case IW_CM_EVENT_CONNECT_REQUEST:
		if (cm_node->ipv4)
			i40iw_fill_sockaddr4(cm_node, &event);
		else
			i40iw_fill_sockaddr6(cm_node, &event);
		event.provider_data = (void *)cm_node;
		event.private_data = (void *)cm_node->pdata_buf;
		event.private_data_len = (u8)cm_node->pdata.size;
		event.ird = cm_node->ird_size;
		break;
	case IW_CM_EVENT_CONNECT_REPLY:
		i40iw_get_cmevent_info(cm_node, cm_id, &event);
		break;
	case IW_CM_EVENT_ESTABLISHED:
		event.ird = cm_node->ird_size;
		event.ord = cm_node->ord_size;
		break;
	case IW_CM_EVENT_DISCONNECT:
		break;
	case IW_CM_EVENT_CLOSE:
		break;
	default:
		i40iw_pr_err("event type received type = %d\n", type);
		return -1;
	}
	return cm_id->event_handler(cm_id, &event);
}

/**
 * i40iw_create_event - create cm event
 * @cm_node: connection's node
 * @type: Event type to generate
 */
static struct i40iw_cm_event *i40iw_create_event(struct i40iw_cm_node *cm_node,
						 enum i40iw_cm_event_type type)
{
	struct i40iw_cm_event *event;

	if (!cm_node->cm_id)
		return NULL;

	event = kzalloc(sizeof(*event), GFP_ATOMIC);

	if (!event)
		return NULL;

	event->type = type;
	event->cm_node = cm_node;
	memcpy(event->cm_info.rem_addr, cm_node->rem_addr, sizeof(event->cm_info.rem_addr));
	memcpy(event->cm_info.loc_addr, cm_node->loc_addr, sizeof(event->cm_info.loc_addr));
	event->cm_info.rem_port = cm_node->rem_port;
	event->cm_info.loc_port = cm_node->loc_port;
	event->cm_info.cm_id = cm_node->cm_id;

	i40iw_debug(cm_node->dev,
		    I40IW_DEBUG_CM,
		    "node=%p event=%p type=%u dst=%pI4 src=%pI4\n",
		    cm_node,
		    event,
		    type,
		    event->cm_info.loc_addr,
		    event->cm_info.rem_addr);

	i40iw_cm_post_event(event);
	return event;
}

/**
 * i40iw_free_retrans_entry - free send entry
 * @cm_node: connection's node
 */
static void i40iw_free_retrans_entry(struct i40iw_cm_node *cm_node)
{
	struct i40iw_device *iwdev = cm_node->iwdev;
	struct i40iw_timer_entry *send_entry;

	send_entry = cm_node->send_entry;
	if (send_entry) {
		cm_node->send_entry = NULL;
		i40iw_free_sqbuf(&iwdev->vsi, (void *)send_entry->sqbuf);
		kfree(send_entry);
		atomic_dec(&cm_node->ref_count);
	}
}

/**
 * i40iw_cleanup_retrans_entry - free send entry with lock
 * @cm_node: connection's node
 */
static void i40iw_cleanup_retrans_entry(struct i40iw_cm_node *cm_node)
{
	unsigned long flags;

	spin_lock_irqsave(&cm_node->retrans_list_lock, flags);
	i40iw_free_retrans_entry(cm_node);
	spin_unlock_irqrestore(&cm_node->retrans_list_lock, flags);
}

/**
 * i40iw_form_cm_frame - get a free packet and build frame
 * @cm_node: connection's node ionfo to use in frame
 * @options: pointer to options info
 * @hdr: pointer mpa header
 * @pdata: pointer to private data
 * @flags:  indicates FIN or ACK
 */
static struct i40iw_puda_buf *i40iw_form_cm_frame(struct i40iw_cm_node *cm_node,
						  struct i40iw_kmem_info *options,
						  struct i40iw_kmem_info *hdr,
						  struct i40iw_kmem_info *pdata,
						  u8 flags)
{
	struct i40iw_puda_buf *sqbuf;
	struct i40iw_sc_vsi *vsi = &cm_node->iwdev->vsi;
	u8 *buf;

	struct tcphdr *tcph;
	struct iphdr *iph;
	struct ipv6hdr *ip6h;
	struct ethhdr *ethh;
	u16 packetsize;
	u16 eth_hlen = ETH_HLEN;
	u32 opts_len = 0;
	u32 pd_len = 0;
	u32 hdr_len = 0;
	u16 vtag;

	sqbuf = i40iw_puda_get_bufpool(vsi->ilq);
	if (!sqbuf)
		return NULL;
	buf = sqbuf->mem.va;

	if (options)
		opts_len = (u32)options->size;

	if (hdr)
		hdr_len = hdr->size;

	if (pdata)
		pd_len = pdata->size;

	if (cm_node->vlan_id < VLAN_TAG_PRESENT)
		eth_hlen += 4;

	if (cm_node->ipv4)
		packetsize = sizeof(*iph) + sizeof(*tcph);
	else
		packetsize = sizeof(*ip6h) + sizeof(*tcph);
	packetsize += opts_len + hdr_len + pd_len;

	memset(buf, 0x00, eth_hlen + packetsize);

	sqbuf->totallen = packetsize + eth_hlen;
	sqbuf->maclen = eth_hlen;
	sqbuf->tcphlen = sizeof(*tcph) + opts_len;
	sqbuf->scratch = (void *)cm_node;

	ethh = (struct ethhdr *)buf;
	buf += eth_hlen;

	if (cm_node->ipv4) {
		sqbuf->ipv4 = true;

		iph = (struct iphdr *)buf;
		buf += sizeof(*iph);
		tcph = (struct tcphdr *)buf;
		buf += sizeof(*tcph);

		ether_addr_copy(ethh->h_dest, cm_node->rem_mac);
		ether_addr_copy(ethh->h_source, cm_node->loc_mac);
		if (cm_node->vlan_id < VLAN_TAG_PRESENT) {
			((struct vlan_ethhdr *)ethh)->h_vlan_proto = htons(ETH_P_8021Q);
			vtag = (cm_node->user_pri << VLAN_PRIO_SHIFT) | cm_node->vlan_id;
			((struct vlan_ethhdr *)ethh)->h_vlan_TCI = htons(vtag);

			((struct vlan_ethhdr *)ethh)->h_vlan_encapsulated_proto = htons(ETH_P_IP);
		} else {
			ethh->h_proto = htons(ETH_P_IP);
		}

		iph->version = IPVERSION;
		iph->ihl = 5;	/* 5 * 4Byte words, IP headr len */
		iph->tos = cm_node->tos;
		iph->tot_len = htons(packetsize);
		iph->id = htons(++cm_node->tcp_cntxt.loc_id);

		iph->frag_off = htons(0x4000);
		iph->ttl = 0x40;
		iph->protocol = IPPROTO_TCP;
		iph->saddr = htonl(cm_node->loc_addr[0]);
		iph->daddr = htonl(cm_node->rem_addr[0]);
	} else {
		sqbuf->ipv4 = false;
		ip6h = (struct ipv6hdr *)buf;
		buf += sizeof(*ip6h);
		tcph = (struct tcphdr *)buf;
		buf += sizeof(*tcph);

		ether_addr_copy(ethh->h_dest, cm_node->rem_mac);
		ether_addr_copy(ethh->h_source, cm_node->loc_mac);
		if (cm_node->vlan_id < VLAN_TAG_PRESENT) {
			((struct vlan_ethhdr *)ethh)->h_vlan_proto = htons(ETH_P_8021Q);
			vtag = (cm_node->user_pri << VLAN_PRIO_SHIFT) | cm_node->vlan_id;
			((struct vlan_ethhdr *)ethh)->h_vlan_TCI = htons(vtag);
			((struct vlan_ethhdr *)ethh)->h_vlan_encapsulated_proto = htons(ETH_P_IPV6);
		} else {
			ethh->h_proto = htons(ETH_P_IPV6);
		}
		ip6h->version = 6;
		ip6h->priority = cm_node->tos >> 4;
		ip6h->flow_lbl[0] = cm_node->tos << 4;
		ip6h->flow_lbl[1] = 0;
		ip6h->flow_lbl[2] = 0;
		ip6h->payload_len = htons(packetsize - sizeof(*ip6h));
		ip6h->nexthdr = 6;
		ip6h->hop_limit = 128;
		i40iw_copy_ip_htonl(ip6h->saddr.in6_u.u6_addr32,
				    cm_node->loc_addr);
		i40iw_copy_ip_htonl(ip6h->daddr.in6_u.u6_addr32,
				    cm_node->rem_addr);
	}

	tcph->source = htons(cm_node->loc_port);
	tcph->dest = htons(cm_node->rem_port);

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
		cm_node->tcp_cntxt.loc_seq_num += hdr_len + pd_len;
	}

	if (flags & SET_FIN) {
		cm_node->tcp_cntxt.loc_seq_num++;
		tcph->fin = 1;
	}

	if (flags & SET_RST)
		tcph->rst = 1;

	tcph->doff = (u16)((sizeof(*tcph) + opts_len + 3) >> 2);
	sqbuf->tcphlen = tcph->doff << 2;
	tcph->window = htons(cm_node->tcp_cntxt.rcv_wnd);
	tcph->urg_ptr = 0;

	if (opts_len) {
		memcpy(buf, options->addr, opts_len);
		buf += opts_len;
	}

	if (hdr_len) {
		memcpy(buf, hdr->addr, hdr_len);
		buf += hdr_len;
	}

	if (pdata && pdata->addr)
		memcpy(buf, pdata->addr, pdata->size);

	atomic_set(&sqbuf->refcount, 1);

	return sqbuf;
}

/**
 * i40iw_send_reset - Send RST packet
 * @cm_node: connection's node
 */
static int i40iw_send_reset(struct i40iw_cm_node *cm_node)
{
	struct i40iw_puda_buf *sqbuf;
	int flags = SET_RST | SET_ACK;

	sqbuf = i40iw_form_cm_frame(cm_node, NULL, NULL, NULL, flags);
	if (!sqbuf) {
		i40iw_pr_err("no sqbuf\n");
		return -1;
	}

	return i40iw_schedule_cm_timer(cm_node, sqbuf, I40IW_TIMER_TYPE_SEND, 0, 1);
}

/**
 * i40iw_active_open_err - send event for active side cm error
 * @cm_node: connection's node
 * @reset: Flag to send reset or not
 */
static void i40iw_active_open_err(struct i40iw_cm_node *cm_node, bool reset)
{
	i40iw_cleanup_retrans_entry(cm_node);
	cm_node->cm_core->stats_connect_errs++;
	if (reset) {
		i40iw_debug(cm_node->dev,
			    I40IW_DEBUG_CM,
			    "%s cm_node=%p state=%d\n",
			    __func__,
			    cm_node,
			    cm_node->state);
		atomic_inc(&cm_node->ref_count);
		i40iw_send_reset(cm_node);
	}

	cm_node->state = I40IW_CM_STATE_CLOSED;
	i40iw_create_event(cm_node, I40IW_CM_EVENT_ABORTED);
}

/**
 * i40iw_passive_open_err - handle passive side cm error
 * @cm_node: connection's node
 * @reset: send reset or just free cm_node
 */
static void i40iw_passive_open_err(struct i40iw_cm_node *cm_node, bool reset)
{
	i40iw_cleanup_retrans_entry(cm_node);
	cm_node->cm_core->stats_passive_errs++;
	cm_node->state = I40IW_CM_STATE_CLOSED;
	i40iw_debug(cm_node->dev,
		    I40IW_DEBUG_CM,
		    "%s cm_node=%p state =%d\n",
		    __func__,
		    cm_node,
		    cm_node->state);
	if (reset)
		i40iw_send_reset(cm_node);
	else
		i40iw_rem_ref_cm_node(cm_node);
}

/**
 * i40iw_event_connect_error - to create connect error event
 * @event: cm information for connect event
 */
static void i40iw_event_connect_error(struct i40iw_cm_event *event)
{
	struct i40iw_qp *iwqp;
	struct iw_cm_id *cm_id;

	cm_id = event->cm_node->cm_id;
	if (!cm_id)
		return;

	iwqp = cm_id->provider_data;

	if (!iwqp || !iwqp->iwdev)
		return;

	iwqp->cm_id = NULL;
	cm_id->provider_data = NULL;
	i40iw_send_cm_event(event->cm_node, cm_id,
			    IW_CM_EVENT_CONNECT_REPLY,
			    -ECONNRESET);
	cm_id->rem_ref(cm_id);
	i40iw_rem_ref_cm_node(event->cm_node);
}

/**
 * i40iw_process_options
 * @cm_node: connection's node
 * @optionsloc: point to start of options
 * @optionsize: size of all options
 * @syn_packet: flag if syn packet
 */
static int i40iw_process_options(struct i40iw_cm_node *cm_node,
				 u8 *optionsloc,
				 u32 optionsize,
				 u32 syn_packet)
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
			i40iw_debug(cm_node->dev,
				    I40IW_DEBUG_CM,
				    "%s: MSS Length: %d Offset: %d Size: %d\n",
				    __func__,
				    all_options->as_mss.length,
				    offset,
				    optionsize);
			got_mss_option = 1;
			if (all_options->as_mss.length != 4)
				return -1;
			tmp = ntohs(all_options->as_mss.mss);
			if (tmp > 0 && tmp < cm_node->tcp_cntxt.mss)
				cm_node->tcp_cntxt.mss = tmp;
			break;
		case OPTION_NUMBER_WINDOW_SCALE:
			cm_node->tcp_cntxt.snd_wscale =
			    all_options->as_windowscale.shiftcount;
			break;
		default:
			i40iw_debug(cm_node->dev,
				    I40IW_DEBUG_CM,
				    "TCP Option not understood: %x\n",
				    all_options->as_base.optionnum);
			break;
		}
		offset += all_options->as_base.length;
	}
	if (!got_mss_option && syn_packet)
		cm_node->tcp_cntxt.mss = I40IW_CM_DEFAULT_MSS;
	return 0;
}

/**
 * i40iw_handle_tcp_options -
 * @cm_node: connection's node
 * @tcph: pointer tcp header
 * @optionsize: size of options rcvd
 * @passive: active or passive flag
 */
static int i40iw_handle_tcp_options(struct i40iw_cm_node *cm_node,
				    struct tcphdr *tcph,
				    int optionsize,
				    int passive)
{
	u8 *optionsloc = (u8 *)&tcph[1];

	if (optionsize) {
		if (i40iw_process_options(cm_node,
					  optionsloc,
					  optionsize,
					  (u32)tcph->syn)) {
			i40iw_debug(cm_node->dev,
				    I40IW_DEBUG_CM,
				    "%s: Node %p, Sending RESET\n",
				    __func__,
				    cm_node);
			if (passive)
				i40iw_passive_open_err(cm_node, true);
			else
				i40iw_active_open_err(cm_node, true);
			return -1;
		}
	}

	cm_node->tcp_cntxt.snd_wnd = ntohs(tcph->window) <<
	    cm_node->tcp_cntxt.snd_wscale;

	if (cm_node->tcp_cntxt.snd_wnd > cm_node->tcp_cntxt.max_snd_wnd)
		cm_node->tcp_cntxt.max_snd_wnd = cm_node->tcp_cntxt.snd_wnd;
	return 0;
}

/**
 * i40iw_build_mpa_v1 - build a MPA V1 frame
 * @cm_node: connection's node
 * @mpa_key: to do read0 or write0
 */
static void i40iw_build_mpa_v1(struct i40iw_cm_node *cm_node,
			       void *start_addr,
			       u8 mpa_key)
{
	struct ietf_mpa_v1 *mpa_frame = (struct ietf_mpa_v1 *)start_addr;

	switch (mpa_key) {
	case MPA_KEY_REQUEST:
		memcpy(mpa_frame->key, IEFT_MPA_KEY_REQ, IETF_MPA_KEY_SIZE);
		break;
	case MPA_KEY_REPLY:
		memcpy(mpa_frame->key, IEFT_MPA_KEY_REP, IETF_MPA_KEY_SIZE);
		break;
	default:
		break;
	}
	mpa_frame->flags = IETF_MPA_FLAGS_CRC;
	mpa_frame->rev = cm_node->mpa_frame_rev;
	mpa_frame->priv_data_len = htons(cm_node->pdata.size);
}

/**
 * i40iw_build_mpa_v2 - build a MPA V2 frame
 * @cm_node: connection's node
 * @start_addr: buffer start address
 * @mpa_key: to do read0 or write0
 */
static void i40iw_build_mpa_v2(struct i40iw_cm_node *cm_node,
			       void *start_addr,
			       u8 mpa_key)
{
	struct ietf_mpa_v2 *mpa_frame = (struct ietf_mpa_v2 *)start_addr;
	struct ietf_rtr_msg *rtr_msg = &mpa_frame->rtr_msg;
	u16 ctrl_ird, ctrl_ord;

	/* initialize the upper 5 bytes of the frame */
	i40iw_build_mpa_v1(cm_node, start_addr, mpa_key);
	mpa_frame->flags |= IETF_MPA_V2_FLAG;
	mpa_frame->priv_data_len += htons(IETF_RTR_MSG_SIZE);

	/* initialize RTR msg */
	if (cm_node->mpav2_ird_ord == IETF_NO_IRD_ORD) {
		ctrl_ird = IETF_NO_IRD_ORD;
		ctrl_ord = IETF_NO_IRD_ORD;
	} else {
		ctrl_ird = (cm_node->ird_size > IETF_NO_IRD_ORD) ?
			IETF_NO_IRD_ORD : cm_node->ird_size;
		ctrl_ord = (cm_node->ord_size > IETF_NO_IRD_ORD) ?
			IETF_NO_IRD_ORD : cm_node->ord_size;
	}

	ctrl_ird |= IETF_PEER_TO_PEER;

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
		break;
	default:
		break;
	}
	rtr_msg->ctrl_ird = htons(ctrl_ird);
	rtr_msg->ctrl_ord = htons(ctrl_ord);
}

/**
 * i40iw_cm_build_mpa_frame - build mpa frame for mpa version 1 or version 2
 * @cm_node: connection's node
 * @mpa: mpa: data buffer
 * @mpa_key: to do read0 or write0
 */
static int i40iw_cm_build_mpa_frame(struct i40iw_cm_node *cm_node,
				    struct i40iw_kmem_info *mpa,
				    u8 mpa_key)
{
	int hdr_len = 0;

	switch (cm_node->mpa_frame_rev) {
	case IETF_MPA_V1:
		hdr_len = sizeof(struct ietf_mpa_v1);
		i40iw_build_mpa_v1(cm_node, mpa->addr, mpa_key);
		break;
	case IETF_MPA_V2:
		hdr_len = sizeof(struct ietf_mpa_v2);
		i40iw_build_mpa_v2(cm_node, mpa->addr, mpa_key);
		break;
	default:
		break;
	}

	return hdr_len;
}

/**
 * i40iw_send_mpa_request - active node send mpa request to passive node
 * @cm_node: connection's node
 */
static int i40iw_send_mpa_request(struct i40iw_cm_node *cm_node)
{
	struct i40iw_puda_buf *sqbuf;

	if (!cm_node) {
		i40iw_pr_err("cm_node == NULL\n");
		return -1;
	}

	cm_node->mpa_hdr.addr = &cm_node->mpa_frame;
	cm_node->mpa_hdr.size = i40iw_cm_build_mpa_frame(cm_node,
							 &cm_node->mpa_hdr,
							 MPA_KEY_REQUEST);
	if (!cm_node->mpa_hdr.size) {
		i40iw_pr_err("mpa size = %d\n", cm_node->mpa_hdr.size);
		return -1;
	}

	sqbuf = i40iw_form_cm_frame(cm_node,
				    NULL,
				    &cm_node->mpa_hdr,
				    &cm_node->pdata,
				    SET_ACK);
	if (!sqbuf) {
		i40iw_pr_err("sq_buf == NULL\n");
		return -1;
	}
	return i40iw_schedule_cm_timer(cm_node, sqbuf, I40IW_TIMER_TYPE_SEND, 1, 0);
}

/**
 * i40iw_send_mpa_reject -
 * @cm_node: connection's node
 * @pdata: reject data for connection
 * @plen: length of reject data
 */
static int i40iw_send_mpa_reject(struct i40iw_cm_node *cm_node,
				 const void *pdata,
				 u8 plen)
{
	struct i40iw_puda_buf *sqbuf;
	struct i40iw_kmem_info priv_info;

	cm_node->mpa_hdr.addr = &cm_node->mpa_frame;
	cm_node->mpa_hdr.size = i40iw_cm_build_mpa_frame(cm_node,
							 &cm_node->mpa_hdr,
							 MPA_KEY_REPLY);

	cm_node->mpa_frame.flags |= IETF_MPA_FLAGS_REJECT;
	priv_info.addr = (void *)pdata;
	priv_info.size = plen;

	sqbuf = i40iw_form_cm_frame(cm_node,
				    NULL,
				    &cm_node->mpa_hdr,
				    &priv_info,
				    SET_ACK | SET_FIN);
	if (!sqbuf) {
		i40iw_pr_err("no sqbuf\n");
		return -ENOMEM;
	}
	cm_node->state = I40IW_CM_STATE_FIN_WAIT1;
	return i40iw_schedule_cm_timer(cm_node, sqbuf, I40IW_TIMER_TYPE_SEND, 1, 0);
}

/**
 * recv_mpa - process an IETF MPA frame
 * @cm_node: connection's node
 * @buffer: Data pointer
 * @type: to return accept or reject
 * @len: Len of mpa buffer
 */
static int i40iw_parse_mpa(struct i40iw_cm_node *cm_node, u8 *buffer, u32 *type, u32 len)
{
	struct ietf_mpa_v1 *mpa_frame;
	struct ietf_mpa_v2 *mpa_v2_frame;
	struct ietf_rtr_msg *rtr_msg;
	int mpa_hdr_len;
	int priv_data_len;

	*type = I40IW_MPA_REQUEST_ACCEPT;

	if (len < sizeof(struct ietf_mpa_v1)) {
		i40iw_pr_err("ietf buffer small (%x)\n", len);
		return -1;
	}

	mpa_frame = (struct ietf_mpa_v1 *)buffer;
	mpa_hdr_len = sizeof(struct ietf_mpa_v1);
	priv_data_len = ntohs(mpa_frame->priv_data_len);

	if (priv_data_len > IETF_MAX_PRIV_DATA_LEN) {
		i40iw_pr_err("large pri_data %d\n", priv_data_len);
		return -1;
	}
	if (mpa_frame->rev != IETF_MPA_V1 && mpa_frame->rev != IETF_MPA_V2) {
		i40iw_pr_err("unsupported mpa rev = %d\n", mpa_frame->rev);
		return -1;
	}
	if (mpa_frame->rev > cm_node->mpa_frame_rev) {
		i40iw_pr_err("rev %d\n", mpa_frame->rev);
		return -1;
	}
	cm_node->mpa_frame_rev = mpa_frame->rev;

	if (cm_node->state != I40IW_CM_STATE_MPAREQ_SENT) {
		if (memcmp(mpa_frame->key, IEFT_MPA_KEY_REQ, IETF_MPA_KEY_SIZE)) {
			i40iw_pr_err("Unexpected MPA Key received\n");
			return -1;
		}
	} else {
		if (memcmp(mpa_frame->key, IEFT_MPA_KEY_REP, IETF_MPA_KEY_SIZE)) {
			i40iw_pr_err("Unexpected MPA Key received\n");
			return -1;
		}
	}

	if (priv_data_len + mpa_hdr_len > len) {
		i40iw_pr_err("ietf buffer len(%x + %x != %x)\n",
			     priv_data_len, mpa_hdr_len, len);
		return -1;
	}
	if (len > MAX_CM_BUFFER) {
		i40iw_pr_err("ietf buffer large len = %d\n", len);
		return -1;
	}

	switch (mpa_frame->rev) {
	case IETF_MPA_V2:{
			u16 ird_size;
			u16 ord_size;
			u16 ctrl_ord;
			u16 ctrl_ird;

			mpa_v2_frame = (struct ietf_mpa_v2 *)buffer;
			mpa_hdr_len += IETF_RTR_MSG_SIZE;
			rtr_msg = &mpa_v2_frame->rtr_msg;

			/* parse rtr message */
			ctrl_ord = ntohs(rtr_msg->ctrl_ord);
			ctrl_ird = ntohs(rtr_msg->ctrl_ird);
			ird_size = ctrl_ird & IETF_NO_IRD_ORD;
			ord_size = ctrl_ord & IETF_NO_IRD_ORD;

			if (!(ctrl_ird & IETF_PEER_TO_PEER))
				return -1;

			if (ird_size == IETF_NO_IRD_ORD || ord_size == IETF_NO_IRD_ORD) {
				cm_node->mpav2_ird_ord = IETF_NO_IRD_ORD;
				goto negotiate_done;
			}

			if (cm_node->state != I40IW_CM_STATE_MPAREQ_SENT) {
				/* responder */
				if (!ord_size && (ctrl_ord & IETF_RDMA0_READ))
					cm_node->ird_size = 1;
				if (cm_node->ord_size > ird_size)
					cm_node->ord_size = ird_size;
			} else {
				/* initiator */
				if (!ird_size && (ctrl_ord & IETF_RDMA0_READ))
					return -1;
				if (cm_node->ord_size > ird_size)
					cm_node->ord_size = ird_size;

				if (cm_node->ird_size < ord_size)
					/* no resources available */
					return -1;
			}

negotiate_done:
			if (ctrl_ord & IETF_RDMA0_READ)
				cm_node->send_rdma0_op = SEND_RDMA_READ_ZERO;
			else if (ctrl_ord & IETF_RDMA0_WRITE)
				cm_node->send_rdma0_op = SEND_RDMA_WRITE_ZERO;
			else	/* Not supported RDMA0 operation */
				return -1;
			i40iw_debug(cm_node->dev, I40IW_DEBUG_CM,
				    "MPAV2: Negotiated ORD: %d, IRD: %d\n",
				    cm_node->ord_size, cm_node->ird_size);
			break;
		}
		break;
	case IETF_MPA_V1:
	default:
		break;
	}

	memcpy(cm_node->pdata_buf, buffer + mpa_hdr_len, priv_data_len);
	cm_node->pdata.size = priv_data_len;

	if (mpa_frame->flags & IETF_MPA_FLAGS_REJECT)
		*type = I40IW_MPA_REQUEST_REJECT;

	if (mpa_frame->flags & IETF_MPA_FLAGS_MARKERS)
		cm_node->snd_mark_en = true;

	return 0;
}

/**
 * i40iw_schedule_cm_timer
 * @@cm_node: connection's node
 * @sqbuf: buffer to send
 * @type: if it is send or close
 * @send_retrans: if rexmits to be done
 * @close_when_complete: is cm_node to be removed
 *
 * note - cm_node needs to be protected before calling this. Encase in:
 *		i40iw_rem_ref_cm_node(cm_core, cm_node);
 *		i40iw_schedule_cm_timer(...)
 *		atomic_inc(&cm_node->ref_count);
 */
int i40iw_schedule_cm_timer(struct i40iw_cm_node *cm_node,
			    struct i40iw_puda_buf *sqbuf,
			    enum i40iw_timer_type type,
			    int send_retrans,
			    int close_when_complete)
{
	struct i40iw_sc_vsi *vsi = &cm_node->iwdev->vsi;
	struct i40iw_cm_core *cm_core = cm_node->cm_core;
	struct i40iw_timer_entry *new_send;
	int ret = 0;
	u32 was_timer_set;
	unsigned long flags;

	new_send = kzalloc(sizeof(*new_send), GFP_ATOMIC);
	if (!new_send) {
		if (type != I40IW_TIMER_TYPE_CLOSE)
			i40iw_free_sqbuf(vsi, (void *)sqbuf);
		return -ENOMEM;
	}
	new_send->retrycount = I40IW_DEFAULT_RETRYS;
	new_send->retranscount = I40IW_DEFAULT_RETRANS;
	new_send->sqbuf = sqbuf;
	new_send->timetosend = jiffies;
	new_send->type = type;
	new_send->send_retrans = send_retrans;
	new_send->close_when_complete = close_when_complete;

	if (type == I40IW_TIMER_TYPE_CLOSE) {
		new_send->timetosend += (HZ / 10);
		if (cm_node->close_entry) {
			kfree(new_send);
			i40iw_pr_err("already close entry\n");
			return -EINVAL;
		}
		cm_node->close_entry = new_send;
	}

	if (type == I40IW_TIMER_TYPE_SEND) {
		spin_lock_irqsave(&cm_node->retrans_list_lock, flags);
		cm_node->send_entry = new_send;
		atomic_inc(&cm_node->ref_count);
		spin_unlock_irqrestore(&cm_node->retrans_list_lock, flags);
		new_send->timetosend = jiffies + I40IW_RETRY_TIMEOUT;

		atomic_inc(&sqbuf->refcount);
		i40iw_puda_send_buf(vsi->ilq, sqbuf);
		if (!send_retrans) {
			i40iw_cleanup_retrans_entry(cm_node);
			if (close_when_complete)
				i40iw_rem_ref_cm_node(cm_node);
			return ret;
		}
	}

	spin_lock_irqsave(&cm_core->ht_lock, flags);
	was_timer_set = timer_pending(&cm_core->tcp_timer);

	if (!was_timer_set) {
		cm_core->tcp_timer.expires = new_send->timetosend;
		add_timer(&cm_core->tcp_timer);
	}
	spin_unlock_irqrestore(&cm_core->ht_lock, flags);

	return ret;
}

/**
 * i40iw_retrans_expired - Could not rexmit the packet
 * @cm_node: connection's node
 */
static void i40iw_retrans_expired(struct i40iw_cm_node *cm_node)
{
	struct iw_cm_id *cm_id = cm_node->cm_id;
	enum i40iw_cm_node_state state = cm_node->state;

	cm_node->state = I40IW_CM_STATE_CLOSED;
	switch (state) {
	case I40IW_CM_STATE_SYN_RCVD:
	case I40IW_CM_STATE_CLOSING:
		i40iw_rem_ref_cm_node(cm_node);
		break;
	case I40IW_CM_STATE_FIN_WAIT1:
	case I40IW_CM_STATE_LAST_ACK:
		if (cm_node->cm_id)
			cm_id->rem_ref(cm_id);
		i40iw_send_reset(cm_node);
		break;
	default:
		atomic_inc(&cm_node->ref_count);
		i40iw_send_reset(cm_node);
		i40iw_create_event(cm_node, I40IW_CM_EVENT_ABORTED);
		break;
	}
}

/**
 * i40iw_handle_close_entry - for handling retry/timeouts
 * @cm_node: connection's node
 * @rem_node: flag for remove cm_node
 */
static void i40iw_handle_close_entry(struct i40iw_cm_node *cm_node, u32 rem_node)
{
	struct i40iw_timer_entry *close_entry = cm_node->close_entry;
	struct iw_cm_id *cm_id = cm_node->cm_id;
	struct i40iw_qp *iwqp;
	unsigned long flags;

	if (!close_entry)
		return;
	iwqp = (struct i40iw_qp *)close_entry->sqbuf;
	if (iwqp) {
		spin_lock_irqsave(&iwqp->lock, flags);
		if (iwqp->cm_id) {
			iwqp->hw_tcp_state = I40IW_TCP_STATE_CLOSED;
			iwqp->hw_iwarp_state = I40IW_QP_STATE_ERROR;
			iwqp->last_aeq = I40IW_AE_RESET_SENT;
			iwqp->ibqp_state = IB_QPS_ERR;
			spin_unlock_irqrestore(&iwqp->lock, flags);
			i40iw_cm_disconn(iwqp);
		} else {
			spin_unlock_irqrestore(&iwqp->lock, flags);
		}
	} else if (rem_node) {
		/* TIME_WAIT state */
		i40iw_rem_ref_cm_node(cm_node);
	}
	if (cm_id)
		cm_id->rem_ref(cm_id);
	kfree(close_entry);
	cm_node->close_entry = NULL;
}

/**
 * i40iw_cm_timer_tick - system's timer expired callback
 * @pass: Pointing to cm_core
 */
static void i40iw_cm_timer_tick(struct timer_list *t)
{
	unsigned long nexttimeout = jiffies + I40IW_LONG_TIME;
	struct i40iw_cm_node *cm_node;
	struct i40iw_timer_entry *send_entry, *close_entry;
	struct list_head *list_core_temp;
	struct i40iw_sc_vsi *vsi;
	struct list_head *list_node;
	struct i40iw_cm_core *cm_core = from_timer(cm_core, t, tcp_timer);
	u32 settimer = 0;
	unsigned long timetosend;
	unsigned long flags;

	struct list_head timer_list;

	INIT_LIST_HEAD(&timer_list);
	spin_lock_irqsave(&cm_core->ht_lock, flags);

	list_for_each_safe(list_node, list_core_temp, &cm_core->connected_nodes) {
		cm_node = container_of(list_node, struct i40iw_cm_node, list);
		if (cm_node->close_entry || cm_node->send_entry) {
			atomic_inc(&cm_node->ref_count);
			list_add(&cm_node->timer_entry, &timer_list);
		}
	}
	spin_unlock_irqrestore(&cm_core->ht_lock, flags);

	list_for_each_safe(list_node, list_core_temp, &timer_list) {
		cm_node = container_of(list_node,
				       struct i40iw_cm_node,
				       timer_entry);
		close_entry = cm_node->close_entry;

		if (close_entry) {
			if (time_after(close_entry->timetosend, jiffies)) {
				if (nexttimeout > close_entry->timetosend ||
				    !settimer) {
					nexttimeout = close_entry->timetosend;
					settimer = 1;
				}
			} else {
				i40iw_handle_close_entry(cm_node, 1);
			}
		}

		spin_lock_irqsave(&cm_node->retrans_list_lock, flags);

		send_entry = cm_node->send_entry;
		if (!send_entry)
			goto done;
		if (time_after(send_entry->timetosend, jiffies)) {
			if (cm_node->state != I40IW_CM_STATE_OFFLOADED) {
				if ((nexttimeout > send_entry->timetosend) ||
				    !settimer) {
					nexttimeout = send_entry->timetosend;
					settimer = 1;
				}
			} else {
				i40iw_free_retrans_entry(cm_node);
			}
			goto done;
		}

		if ((cm_node->state == I40IW_CM_STATE_OFFLOADED) ||
		    (cm_node->state == I40IW_CM_STATE_CLOSED)) {
			i40iw_free_retrans_entry(cm_node);
			goto done;
		}

		if (!send_entry->retranscount || !send_entry->retrycount) {
			i40iw_free_retrans_entry(cm_node);

			spin_unlock_irqrestore(&cm_node->retrans_list_lock, flags);
			i40iw_retrans_expired(cm_node);
			cm_node->state = I40IW_CM_STATE_CLOSED;
			spin_lock_irqsave(&cm_node->retrans_list_lock, flags);
			goto done;
		}
		spin_unlock_irqrestore(&cm_node->retrans_list_lock, flags);

		vsi = &cm_node->iwdev->vsi;

		if (!cm_node->ack_rcvd) {
			atomic_inc(&send_entry->sqbuf->refcount);
			i40iw_puda_send_buf(vsi->ilq, send_entry->sqbuf);
			cm_node->cm_core->stats_pkt_retrans++;
		}
		spin_lock_irqsave(&cm_node->retrans_list_lock, flags);
		if (send_entry->send_retrans) {
			send_entry->retranscount--;
			timetosend = (I40IW_RETRY_TIMEOUT <<
				      (I40IW_DEFAULT_RETRANS -
				       send_entry->retranscount));

			send_entry->timetosend = jiffies +
			    min(timetosend, I40IW_MAX_TIMEOUT);
			if (nexttimeout > send_entry->timetosend || !settimer) {
				nexttimeout = send_entry->timetosend;
				settimer = 1;
			}
		} else {
			int close_when_complete;

			close_when_complete = send_entry->close_when_complete;
			i40iw_debug(cm_node->dev,
				    I40IW_DEBUG_CM,
				    "cm_node=%p state=%d\n",
				    cm_node,
				    cm_node->state);
			i40iw_free_retrans_entry(cm_node);
			if (close_when_complete)
				i40iw_rem_ref_cm_node(cm_node);
		}
done:
		spin_unlock_irqrestore(&cm_node->retrans_list_lock, flags);
		i40iw_rem_ref_cm_node(cm_node);
	}

	if (settimer) {
		spin_lock_irqsave(&cm_core->ht_lock, flags);
		if (!timer_pending(&cm_core->tcp_timer)) {
			cm_core->tcp_timer.expires = nexttimeout;
			add_timer(&cm_core->tcp_timer);
		}
		spin_unlock_irqrestore(&cm_core->ht_lock, flags);
	}
}

/**
 * i40iw_send_syn - send SYN packet
 * @cm_node: connection's node
 * @sendack: flag to set ACK bit or not
 */
int i40iw_send_syn(struct i40iw_cm_node *cm_node, u32 sendack)
{
	struct i40iw_puda_buf *sqbuf;
	int flags = SET_SYN;
	char optionsbuffer[sizeof(struct option_mss) +
			   sizeof(struct option_windowscale) +
			   sizeof(struct option_base) + TCP_OPTIONS_PADDING];
	struct i40iw_kmem_info opts;

	int optionssize = 0;
	/* Sending MSS option */
	union all_known_options *options;

	opts.addr = optionsbuffer;
	if (!cm_node) {
		i40iw_pr_err("no cm_node\n");
		return -EINVAL;
	}

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
	options = (union all_known_options *)&optionsbuffer[optionssize];
	options->as_end = OPTION_NUMBER_END;
	optionssize += 1;

	if (sendack)
		flags |= SET_ACK;

	opts.size = optionssize;

	sqbuf = i40iw_form_cm_frame(cm_node, &opts, NULL, NULL, flags);
	if (!sqbuf) {
		i40iw_pr_err("no sqbuf\n");
		return -1;
	}
	return i40iw_schedule_cm_timer(cm_node, sqbuf, I40IW_TIMER_TYPE_SEND, 1, 0);
}

/**
 * i40iw_send_ack - Send ACK packet
 * @cm_node: connection's node
 */
static void i40iw_send_ack(struct i40iw_cm_node *cm_node)
{
	struct i40iw_puda_buf *sqbuf;
	struct i40iw_sc_vsi *vsi = &cm_node->iwdev->vsi;

	sqbuf = i40iw_form_cm_frame(cm_node, NULL, NULL, NULL, SET_ACK);
	if (sqbuf)
		i40iw_puda_send_buf(vsi->ilq, sqbuf);
	else
		i40iw_pr_err("no sqbuf\n");
}

/**
 * i40iw_send_fin - Send FIN pkt
 * @cm_node: connection's node
 */
static int i40iw_send_fin(struct i40iw_cm_node *cm_node)
{
	struct i40iw_puda_buf *sqbuf;

	sqbuf = i40iw_form_cm_frame(cm_node, NULL, NULL, NULL, SET_ACK | SET_FIN);
	if (!sqbuf) {
		i40iw_pr_err("no sqbuf\n");
		return -1;
	}
	return i40iw_schedule_cm_timer(cm_node, sqbuf, I40IW_TIMER_TYPE_SEND, 1, 0);
}

/**
 * i40iw_find_node - find a cm node that matches the reference cm node
 * @cm_core: cm's core
 * @rem_port: remote tcp port num
 * @rem_addr: remote ip addr
 * @loc_port: local tcp port num
 * @loc_addr: loc ip addr
 * @add_refcnt: flag to increment refcount of cm_node
 */
struct i40iw_cm_node *i40iw_find_node(struct i40iw_cm_core *cm_core,
				      u16 rem_port,
				      u32 *rem_addr,
				      u16 loc_port,
				      u32 *loc_addr,
				      bool add_refcnt)
{
	struct list_head *hte;
	struct i40iw_cm_node *cm_node;
	unsigned long flags;

	hte = &cm_core->connected_nodes;

	/* walk list and find cm_node associated with this session ID */
	spin_lock_irqsave(&cm_core->ht_lock, flags);
	list_for_each_entry(cm_node, hte, list) {
		if (!memcmp(cm_node->loc_addr, loc_addr, sizeof(cm_node->loc_addr)) &&
		    (cm_node->loc_port == loc_port) &&
		    !memcmp(cm_node->rem_addr, rem_addr, sizeof(cm_node->rem_addr)) &&
		    (cm_node->rem_port == rem_port)) {
			if (add_refcnt)
				atomic_inc(&cm_node->ref_count);
			spin_unlock_irqrestore(&cm_core->ht_lock, flags);
			return cm_node;
		}
	}
	spin_unlock_irqrestore(&cm_core->ht_lock, flags);

	/* no owner node */
	return NULL;
}

/**
 * i40iw_find_listener - find a cm node listening on this addr-port pair
 * @cm_core: cm's core
 * @dst_port: listener tcp port num
 * @dst_addr: listener ip addr
 * @listener_state: state to match with listen node's
 */
static struct i40iw_cm_listener *i40iw_find_listener(
						     struct i40iw_cm_core *cm_core,
						     u32 *dst_addr,
						     u16 dst_port,
						     u16 vlan_id,
						     enum i40iw_cm_listener_state
						     listener_state)
{
	struct i40iw_cm_listener *listen_node;
	static const u32 ip_zero[4] = { 0, 0, 0, 0 };
	u32 listen_addr[4];
	u16 listen_port;
	unsigned long flags;

	/* walk list and find cm_node associated with this session ID */
	spin_lock_irqsave(&cm_core->listen_list_lock, flags);
	list_for_each_entry(listen_node, &cm_core->listen_nodes, list) {
		memcpy(listen_addr, listen_node->loc_addr, sizeof(listen_addr));
		listen_port = listen_node->loc_port;
		/* compare node pair, return node handle if a match */
		if ((!memcmp(listen_addr, dst_addr, sizeof(listen_addr)) ||
		     !memcmp(listen_addr, ip_zero, sizeof(listen_addr))) &&
		     (listen_port == dst_port) &&
		     (listener_state & listen_node->listener_state)) {
			atomic_inc(&listen_node->ref_count);
			spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);
			return listen_node;
		}
	}
	spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);
	return NULL;
}

/**
 * i40iw_add_hte_node - add a cm node to the hash table
 * @cm_core: cm's core
 * @cm_node: connection's node
 */
static void i40iw_add_hte_node(struct i40iw_cm_core *cm_core,
			       struct i40iw_cm_node *cm_node)
{
	struct list_head *hte;
	unsigned long flags;

	if (!cm_node || !cm_core) {
		i40iw_pr_err("cm_node or cm_core == NULL\n");
		return;
	}
	spin_lock_irqsave(&cm_core->ht_lock, flags);

	/* get a handle on the hash table element (list head for this slot) */
	hte = &cm_core->connected_nodes;
	list_add_tail(&cm_node->list, hte);
	spin_unlock_irqrestore(&cm_core->ht_lock, flags);
}

/**
 * i40iw_port_in_use - determine if port is in use
 * @port: port number
 * @active_side: flag for listener side vs active side
 */
static bool i40iw_port_in_use(struct i40iw_cm_core *cm_core, u16 port, bool active_side)
{
	struct i40iw_cm_listener *listen_node;
	struct i40iw_cm_node *cm_node;
	unsigned long flags;
	bool ret = false;

	if (active_side) {
		/* search connected node list */
		spin_lock_irqsave(&cm_core->ht_lock, flags);
		list_for_each_entry(cm_node, &cm_core->connected_nodes, list) {
			if (cm_node->loc_port == port) {
				ret = true;
				break;
			}
		}
		if (!ret)
			clear_bit(port, cm_core->active_side_ports);
		spin_unlock_irqrestore(&cm_core->ht_lock, flags);
	} else {
		spin_lock_irqsave(&cm_core->listen_list_lock, flags);
		list_for_each_entry(listen_node, &cm_core->listen_nodes, list) {
			if (listen_node->loc_port == port) {
				ret = true;
				break;
			}
		}
		spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);
	}

	return ret;
}

/**
 * i40iw_del_multiple_qhash - Remove qhash and child listens
 * @iwdev: iWarp device
 * @cm_info: CM info for parent listen node
 * @cm_parent_listen_node: The parent listen node
 */
static enum i40iw_status_code i40iw_del_multiple_qhash(
						       struct i40iw_device *iwdev,
						       struct i40iw_cm_info *cm_info,
						       struct i40iw_cm_listener *cm_parent_listen_node)
{
	struct i40iw_cm_listener *child_listen_node;
	enum i40iw_status_code ret = I40IW_ERR_CONFIG;
	struct list_head *pos, *tpos;
	unsigned long flags;

	spin_lock_irqsave(&iwdev->cm_core.listen_list_lock, flags);
	list_for_each_safe(pos, tpos, &cm_parent_listen_node->child_listen_list) {
		child_listen_node = list_entry(pos, struct i40iw_cm_listener, child_listen_list);
		if (child_listen_node->ipv4)
			i40iw_debug(&iwdev->sc_dev,
				    I40IW_DEBUG_CM,
				    "removing child listen for IP=%pI4, port=%d, vlan=%d\n",
				    child_listen_node->loc_addr,
				    child_listen_node->loc_port,
				    child_listen_node->vlan_id);
		else
			i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_CM,
				    "removing child listen for IP=%pI6, port=%d, vlan=%d\n",
				    child_listen_node->loc_addr,
				    child_listen_node->loc_port,
				    child_listen_node->vlan_id);
		list_del(pos);
		memcpy(cm_info->loc_addr, child_listen_node->loc_addr,
		       sizeof(cm_info->loc_addr));
		cm_info->vlan_id = child_listen_node->vlan_id;
		if (child_listen_node->qhash_set) {
			ret = i40iw_manage_qhash(iwdev, cm_info,
						 I40IW_QHASH_TYPE_TCP_SYN,
						 I40IW_QHASH_MANAGE_TYPE_DELETE,
						 NULL, false);
			child_listen_node->qhash_set = false;
		} else {
			ret = I40IW_SUCCESS;
		}
		i40iw_debug(&iwdev->sc_dev,
			    I40IW_DEBUG_CM,
			    "freed pointer = %p\n",
			    child_listen_node);
		kfree(child_listen_node);
		cm_parent_listen_node->cm_core->stats_listen_nodes_destroyed++;
	}
	spin_unlock_irqrestore(&iwdev->cm_core.listen_list_lock, flags);

	return ret;
}

/**
 * i40iw_netdev_vlan_ipv6 - Gets the netdev and vlan
 * @addr: local IPv6 address
 * @vlan_id: vlan id for the given IPv6 address
 *
 * Returns the net_device of the IPv6 address and also sets the
 * vlan id for that address.
 */
static struct net_device *i40iw_netdev_vlan_ipv6(u32 *addr, u16 *vlan_id)
{
	struct net_device *ip_dev = NULL;
	struct in6_addr laddr6;

	if (!IS_ENABLED(CONFIG_IPV6))
		return NULL;
	i40iw_copy_ip_htonl(laddr6.in6_u.u6_addr32, addr);
	if (vlan_id)
		*vlan_id = I40IW_NO_VLAN;
	rcu_read_lock();
	for_each_netdev_rcu(&init_net, ip_dev) {
		if (ipv6_chk_addr(&init_net, &laddr6, ip_dev, 1)) {
			if (vlan_id)
				*vlan_id = rdma_vlan_dev_vlan_id(ip_dev);
			break;
		}
	}
	rcu_read_unlock();
	return ip_dev;
}

/**
 * i40iw_get_vlan_ipv4 - Returns the vlan_id for IPv4 address
 * @addr: local IPv4 address
 */
static u16 i40iw_get_vlan_ipv4(u32 *addr)
{
	struct net_device *netdev;
	u16 vlan_id = I40IW_NO_VLAN;

	netdev = ip_dev_find(&init_net, htonl(addr[0]));
	if (netdev) {
		vlan_id = rdma_vlan_dev_vlan_id(netdev);
		dev_put(netdev);
	}
	return vlan_id;
}

/**
 * i40iw_add_mqh_6 - Adds multiple qhashes for IPv6
 * @iwdev: iWarp device
 * @cm_info: CM info for parent listen node
 * @cm_parent_listen_node: The parent listen node
 *
 * Adds a qhash and a child listen node for every IPv6 address
 * on the adapter and adds the associated qhash filter
 */
static enum i40iw_status_code i40iw_add_mqh_6(struct i40iw_device *iwdev,
					      struct i40iw_cm_info *cm_info,
					      struct i40iw_cm_listener *cm_parent_listen_node)
{
	struct net_device *ip_dev;
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifp, *tmp;
	enum i40iw_status_code ret = 0;
	struct i40iw_cm_listener *child_listen_node;
	unsigned long flags;

	rtnl_lock();
	for_each_netdev_rcu(&init_net, ip_dev) {
		if ((((rdma_vlan_dev_vlan_id(ip_dev) < I40IW_NO_VLAN) &&
		      (rdma_vlan_dev_real_dev(ip_dev) == iwdev->netdev)) ||
		     (ip_dev == iwdev->netdev)) && (ip_dev->flags & IFF_UP)) {
			idev = __in6_dev_get(ip_dev);
			if (!idev) {
				i40iw_pr_err("idev == NULL\n");
				break;
			}
			list_for_each_entry_safe(ifp, tmp, &idev->addr_list, if_list) {
				i40iw_debug(&iwdev->sc_dev,
					    I40IW_DEBUG_CM,
					    "IP=%pI6, vlan_id=%d, MAC=%pM\n",
					    &ifp->addr,
					    rdma_vlan_dev_vlan_id(ip_dev),
					    ip_dev->dev_addr);
				child_listen_node =
					kzalloc(sizeof(*child_listen_node), GFP_ATOMIC);
				i40iw_debug(&iwdev->sc_dev,
					    I40IW_DEBUG_CM,
					    "Allocating child listener %p\n",
					    child_listen_node);
				if (!child_listen_node) {
					ret = I40IW_ERR_NO_MEMORY;
					goto exit;
				}
				cm_info->vlan_id = rdma_vlan_dev_vlan_id(ip_dev);
				cm_parent_listen_node->vlan_id = cm_info->vlan_id;

				memcpy(child_listen_node, cm_parent_listen_node,
				       sizeof(*child_listen_node));

				i40iw_copy_ip_ntohl(child_listen_node->loc_addr,
						    ifp->addr.in6_u.u6_addr32);
				memcpy(cm_info->loc_addr, child_listen_node->loc_addr,
				       sizeof(cm_info->loc_addr));

				ret = i40iw_manage_qhash(iwdev, cm_info,
							 I40IW_QHASH_TYPE_TCP_SYN,
							 I40IW_QHASH_MANAGE_TYPE_ADD,
							 NULL, true);
				if (!ret) {
					child_listen_node->qhash_set = true;
					spin_lock_irqsave(&iwdev->cm_core.listen_list_lock, flags);
					list_add(&child_listen_node->child_listen_list,
						 &cm_parent_listen_node->child_listen_list);
					spin_unlock_irqrestore(&iwdev->cm_core.listen_list_lock, flags);
					cm_parent_listen_node->cm_core->stats_listen_nodes_created++;
				} else {
					kfree(child_listen_node);
				}
			}
		}
	}
exit:
	rtnl_unlock();
	return ret;
}

/**
 * i40iw_add_mqh_4 - Adds multiple qhashes for IPv4
 * @iwdev: iWarp device
 * @cm_info: CM info for parent listen node
 * @cm_parent_listen_node: The parent listen node
 *
 * Adds a qhash and a child listen node for every IPv4 address
 * on the adapter and adds the associated qhash filter
 */
static enum i40iw_status_code i40iw_add_mqh_4(
				struct i40iw_device *iwdev,
				struct i40iw_cm_info *cm_info,
				struct i40iw_cm_listener *cm_parent_listen_node)
{
	struct net_device *dev;
	struct in_device *idev;
	struct i40iw_cm_listener *child_listen_node;
	enum i40iw_status_code ret = 0;
	unsigned long flags;

	rtnl_lock();
	for_each_netdev(&init_net, dev) {
		if ((((rdma_vlan_dev_vlan_id(dev) < I40IW_NO_VLAN) &&
		      (rdma_vlan_dev_real_dev(dev) == iwdev->netdev)) ||
		    (dev == iwdev->netdev)) && (dev->flags & IFF_UP)) {
			idev = in_dev_get(dev);
			for_ifa(idev) {
				i40iw_debug(&iwdev->sc_dev,
					    I40IW_DEBUG_CM,
					    "Allocating child CM Listener forIP=%pI4, vlan_id=%d, MAC=%pM\n",
					    &ifa->ifa_address,
					    rdma_vlan_dev_vlan_id(dev),
					    dev->dev_addr);
				child_listen_node = kzalloc(sizeof(*child_listen_node), GFP_ATOMIC);
				cm_parent_listen_node->cm_core->stats_listen_nodes_created++;
				i40iw_debug(&iwdev->sc_dev,
					    I40IW_DEBUG_CM,
					    "Allocating child listener %p\n",
					    child_listen_node);
				if (!child_listen_node) {
					in_dev_put(idev);
					ret = I40IW_ERR_NO_MEMORY;
					goto exit;
				}
				cm_info->vlan_id = rdma_vlan_dev_vlan_id(dev);
				cm_parent_listen_node->vlan_id = cm_info->vlan_id;
				memcpy(child_listen_node,
				       cm_parent_listen_node,
				       sizeof(*child_listen_node));

				child_listen_node->loc_addr[0] = ntohl(ifa->ifa_address);
				memcpy(cm_info->loc_addr, child_listen_node->loc_addr,
				       sizeof(cm_info->loc_addr));

				ret = i40iw_manage_qhash(iwdev,
							 cm_info,
							 I40IW_QHASH_TYPE_TCP_SYN,
							 I40IW_QHASH_MANAGE_TYPE_ADD,
							 NULL,
							 true);
				if (!ret) {
					child_listen_node->qhash_set = true;
					spin_lock_irqsave(&iwdev->cm_core.listen_list_lock, flags);
					list_add(&child_listen_node->child_listen_list,
						 &cm_parent_listen_node->child_listen_list);
					spin_unlock_irqrestore(&iwdev->cm_core.listen_list_lock, flags);
				} else {
					kfree(child_listen_node);
					cm_parent_listen_node->cm_core->stats_listen_nodes_created--;
				}
			}
			endfor_ifa(idev);
			in_dev_put(idev);
		}
	}
exit:
	rtnl_unlock();
	return ret;
}

/**
 * i40iw_dec_refcnt_listen - delete listener and associated cm nodes
 * @cm_core: cm's core
 * @free_hanging_nodes: to free associated cm_nodes
 * @apbvt_del: flag to delete the apbvt
 */
static int i40iw_dec_refcnt_listen(struct i40iw_cm_core *cm_core,
				   struct i40iw_cm_listener *listener,
				   int free_hanging_nodes, bool apbvt_del)
{
	int ret = -EINVAL;
	int err = 0;
	struct list_head *list_pos;
	struct list_head *list_temp;
	struct i40iw_cm_node *cm_node;
	struct list_head reset_list;
	struct i40iw_cm_info nfo;
	struct i40iw_cm_node *loopback;
	enum i40iw_cm_node_state old_state;
	unsigned long flags;

	/* free non-accelerated child nodes for this listener */
	INIT_LIST_HEAD(&reset_list);
	if (free_hanging_nodes) {
		spin_lock_irqsave(&cm_core->ht_lock, flags);
		list_for_each_safe(list_pos, list_temp, &cm_core->connected_nodes) {
			cm_node = container_of(list_pos, struct i40iw_cm_node, list);
			if ((cm_node->listener == listener) && !cm_node->accelerated) {
				atomic_inc(&cm_node->ref_count);
				list_add(&cm_node->reset_entry, &reset_list);
			}
		}
		spin_unlock_irqrestore(&cm_core->ht_lock, flags);
	}

	list_for_each_safe(list_pos, list_temp, &reset_list) {
		cm_node = container_of(list_pos, struct i40iw_cm_node, reset_entry);
		loopback = cm_node->loopbackpartner;
		if (cm_node->state >= I40IW_CM_STATE_FIN_WAIT1) {
			i40iw_rem_ref_cm_node(cm_node);
		} else {
			if (!loopback) {
				i40iw_cleanup_retrans_entry(cm_node);
				err = i40iw_send_reset(cm_node);
				if (err) {
					cm_node->state = I40IW_CM_STATE_CLOSED;
					i40iw_pr_err("send reset\n");
				} else {
					old_state = cm_node->state;
					cm_node->state = I40IW_CM_STATE_LISTENER_DESTROYED;
					if (old_state != I40IW_CM_STATE_MPAREQ_RCVD)
						i40iw_rem_ref_cm_node(cm_node);
				}
			} else {
				struct i40iw_cm_event event;

				event.cm_node = loopback;
				memcpy(event.cm_info.rem_addr,
				       loopback->rem_addr, sizeof(event.cm_info.rem_addr));
				memcpy(event.cm_info.loc_addr,
				       loopback->loc_addr, sizeof(event.cm_info.loc_addr));
				event.cm_info.rem_port = loopback->rem_port;
				event.cm_info.loc_port = loopback->loc_port;
				event.cm_info.cm_id = loopback->cm_id;
				event.cm_info.ipv4 = loopback->ipv4;
				atomic_inc(&loopback->ref_count);
				loopback->state = I40IW_CM_STATE_CLOSED;
				i40iw_event_connect_error(&event);
				cm_node->state = I40IW_CM_STATE_LISTENER_DESTROYED;
				i40iw_rem_ref_cm_node(cm_node);
			}
		}
	}

	if (!atomic_dec_return(&listener->ref_count)) {
		spin_lock_irqsave(&cm_core->listen_list_lock, flags);
		list_del(&listener->list);
		spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);

		if (listener->iwdev) {
			if (apbvt_del && !i40iw_port_in_use(cm_core, listener->loc_port, false))
				i40iw_manage_apbvt(listener->iwdev,
						   listener->loc_port,
						   I40IW_MANAGE_APBVT_DEL);

			memcpy(nfo.loc_addr, listener->loc_addr, sizeof(nfo.loc_addr));
			nfo.loc_port = listener->loc_port;
			nfo.ipv4 = listener->ipv4;
			nfo.vlan_id = listener->vlan_id;
			nfo.user_pri = listener->user_pri;

			if (!list_empty(&listener->child_listen_list)) {
				i40iw_del_multiple_qhash(listener->iwdev, &nfo, listener);
			} else {
				if (listener->qhash_set)
					i40iw_manage_qhash(listener->iwdev,
							   &nfo,
							   I40IW_QHASH_TYPE_TCP_SYN,
							   I40IW_QHASH_MANAGE_TYPE_DELETE,
							   NULL,
							   false);
			}
		}

		cm_core->stats_listen_destroyed++;
		kfree(listener);
		cm_core->stats_listen_nodes_destroyed++;
		listener = NULL;
		ret = 0;
	}

	if (listener) {
		if (atomic_read(&listener->pend_accepts_cnt) > 0)
			i40iw_debug(cm_core->dev,
				    I40IW_DEBUG_CM,
				    "%s: listener (%p) pending accepts=%u\n",
				    __func__,
				    listener,
				    atomic_read(&listener->pend_accepts_cnt));
	}

	return ret;
}

/**
 * i40iw_cm_del_listen - delete a linstener
 * @cm_core: cm's core
  * @listener: passive connection's listener
 * @apbvt_del: flag to delete apbvt
 */
static int i40iw_cm_del_listen(struct i40iw_cm_core *cm_core,
			       struct i40iw_cm_listener *listener,
			       bool apbvt_del)
{
	listener->listener_state = I40IW_CM_LISTENER_PASSIVE_STATE;
	listener->cm_id = NULL;	/* going to be destroyed pretty soon */
	return i40iw_dec_refcnt_listen(cm_core, listener, 1, apbvt_del);
}

/**
 * i40iw_addr_resolve_neigh - resolve neighbor address
 * @iwdev: iwarp device structure
 * @src_ip: local ip address
 * @dst_ip: remote ip address
 * @arpindex: if there is an arp entry
 */
static int i40iw_addr_resolve_neigh(struct i40iw_device *iwdev,
				    u32 src_ip,
				    u32 dst_ip,
				    int arpindex)
{
	struct rtable *rt;
	struct neighbour *neigh;
	int rc = arpindex;
	struct net_device *netdev = iwdev->netdev;
	__be32 dst_ipaddr = htonl(dst_ip);
	__be32 src_ipaddr = htonl(src_ip);

	rt = ip_route_output(&init_net, dst_ipaddr, src_ipaddr, 0, 0);
	if (IS_ERR(rt)) {
		i40iw_pr_err("ip_route_output\n");
		return rc;
	}

	if (netif_is_bond_slave(netdev))
		netdev = netdev_master_upper_dev_get(netdev);

	neigh = dst_neigh_lookup(&rt->dst, &dst_ipaddr);

	rcu_read_lock();
	if (neigh) {
		if (neigh->nud_state & NUD_VALID) {
			if (arpindex >= 0) {
				if (ether_addr_equal(iwdev->arp_table[arpindex].mac_addr,
						     neigh->ha))
					/* Mac address same as arp table */
					goto resolve_neigh_exit;
				i40iw_manage_arp_cache(iwdev,
						       iwdev->arp_table[arpindex].mac_addr,
						       &dst_ip,
						       true,
						       I40IW_ARP_DELETE);
			}

			i40iw_manage_arp_cache(iwdev, neigh->ha, &dst_ip, true, I40IW_ARP_ADD);
			rc = i40iw_arp_table(iwdev, &dst_ip, true, NULL, I40IW_ARP_RESOLVE);
		} else {
			neigh_event_send(neigh, NULL);
		}
	}
 resolve_neigh_exit:

	rcu_read_unlock();
	if (neigh)
		neigh_release(neigh);

	ip_rt_put(rt);
	return rc;
}

/**
 * i40iw_get_dst_ipv6
 */
static struct dst_entry *i40iw_get_dst_ipv6(struct sockaddr_in6 *src_addr,
					    struct sockaddr_in6 *dst_addr)
{
	struct dst_entry *dst;
	struct flowi6 fl6;

	memset(&fl6, 0, sizeof(fl6));
	fl6.daddr = dst_addr->sin6_addr;
	fl6.saddr = src_addr->sin6_addr;
	if (ipv6_addr_type(&fl6.daddr) & IPV6_ADDR_LINKLOCAL)
		fl6.flowi6_oif = dst_addr->sin6_scope_id;

	dst = ip6_route_output(&init_net, NULL, &fl6);
	return dst;
}

/**
 * i40iw_addr_resolve_neigh_ipv6 - resolve neighbor ipv6 address
 * @iwdev: iwarp device structure
 * @dst_ip: remote ip address
 * @arpindex: if there is an arp entry
 */
static int i40iw_addr_resolve_neigh_ipv6(struct i40iw_device *iwdev,
					 u32 *src,
					 u32 *dest,
					 int arpindex)
{
	struct neighbour *neigh;
	int rc = arpindex;
	struct net_device *netdev = iwdev->netdev;
	struct dst_entry *dst;
	struct sockaddr_in6 dst_addr;
	struct sockaddr_in6 src_addr;

	memset(&dst_addr, 0, sizeof(dst_addr));
	dst_addr.sin6_family = AF_INET6;
	i40iw_copy_ip_htonl(dst_addr.sin6_addr.in6_u.u6_addr32, dest);
	memset(&src_addr, 0, sizeof(src_addr));
	src_addr.sin6_family = AF_INET6;
	i40iw_copy_ip_htonl(src_addr.sin6_addr.in6_u.u6_addr32, src);
	dst = i40iw_get_dst_ipv6(&src_addr, &dst_addr);
	if (!dst || dst->error) {
		if (dst) {
			dst_release(dst);
			i40iw_pr_err("ip6_route_output returned dst->error = %d\n",
				     dst->error);
		}
		return rc;
	}

	if (netif_is_bond_slave(netdev))
		netdev = netdev_master_upper_dev_get(netdev);

	neigh = dst_neigh_lookup(dst, &dst_addr);

	rcu_read_lock();
	if (neigh) {
		i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_CM, "dst_neigh_lookup MAC=%pM\n", neigh->ha);
		if (neigh->nud_state & NUD_VALID) {
			if (arpindex >= 0) {
				if (ether_addr_equal
				    (iwdev->arp_table[arpindex].mac_addr,
				     neigh->ha)) {
					/* Mac address same as in arp table */
					goto resolve_neigh_exit6;
				}
				i40iw_manage_arp_cache(iwdev,
						       iwdev->arp_table[arpindex].mac_addr,
						       dest,
						       false,
						       I40IW_ARP_DELETE);
			}
			i40iw_manage_arp_cache(iwdev,
					       neigh->ha,
					       dest,
					       false,
					       I40IW_ARP_ADD);
			rc = i40iw_arp_table(iwdev,
					     dest,
					     false,
					     NULL,
					     I40IW_ARP_RESOLVE);
		} else {
			neigh_event_send(neigh, NULL);
		}
	}

 resolve_neigh_exit6:
	rcu_read_unlock();
	if (neigh)
		neigh_release(neigh);
	dst_release(dst);
	return rc;
}

/**
 * i40iw_ipv4_is_loopback - check if loopback
 * @loc_addr: local addr to compare
 * @rem_addr: remote address
 */
static bool i40iw_ipv4_is_loopback(u32 loc_addr, u32 rem_addr)
{
	return ipv4_is_loopback(htonl(rem_addr)) || (loc_addr == rem_addr);
}

/**
 * i40iw_ipv6_is_loopback - check if loopback
 * @loc_addr: local addr to compare
 * @rem_addr: remote address
 */
static bool i40iw_ipv6_is_loopback(u32 *loc_addr, u32 *rem_addr)
{
	struct in6_addr raddr6;

	i40iw_copy_ip_htonl(raddr6.in6_u.u6_addr32, rem_addr);
	return !memcmp(loc_addr, rem_addr, 16) || ipv6_addr_loopback(&raddr6);
}

/**
 * i40iw_make_cm_node - create a new instance of a cm node
 * @cm_core: cm's core
 * @iwdev: iwarp device structure
 * @cm_info: quad info for connection
 * @listener: passive connection's listener
 */
static struct i40iw_cm_node *i40iw_make_cm_node(
				   struct i40iw_cm_core *cm_core,
				   struct i40iw_device *iwdev,
				   struct i40iw_cm_info *cm_info,
				   struct i40iw_cm_listener *listener)
{
	struct i40iw_cm_node *cm_node;
	struct timespec ts;
	int oldarpindex;
	int arpindex;
	struct net_device *netdev = iwdev->netdev;

	/* create an hte and cm_node for this instance */
	cm_node = kzalloc(sizeof(*cm_node), GFP_ATOMIC);
	if (!cm_node)
		return NULL;

	/* set our node specific transport info */
	cm_node->ipv4 = cm_info->ipv4;
	cm_node->vlan_id = cm_info->vlan_id;
	if ((cm_node->vlan_id == I40IW_NO_VLAN) && iwdev->dcb)
		cm_node->vlan_id = 0;
	cm_node->tos = cm_info->tos;
	cm_node->user_pri = cm_info->user_pri;
	if (listener) {
		if (listener->tos != cm_info->tos)
			i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_DCB,
				    "application TOS[%d] and remote client TOS[%d] mismatch\n",
				     listener->tos, cm_info->tos);
		cm_node->tos = max(listener->tos, cm_info->tos);
		cm_node->user_pri = rt_tos2priority(cm_node->tos);
		i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_DCB, "listener: TOS:[%d] UP:[%d]\n",
			    cm_node->tos, cm_node->user_pri);
	}
	memcpy(cm_node->loc_addr, cm_info->loc_addr, sizeof(cm_node->loc_addr));
	memcpy(cm_node->rem_addr, cm_info->rem_addr, sizeof(cm_node->rem_addr));
	cm_node->loc_port = cm_info->loc_port;
	cm_node->rem_port = cm_info->rem_port;

	cm_node->mpa_frame_rev = iwdev->mpa_version;
	cm_node->send_rdma0_op = SEND_RDMA_READ_ZERO;
	cm_node->ird_size = I40IW_MAX_IRD_SIZE;
	cm_node->ord_size = I40IW_MAX_ORD_SIZE;

	cm_node->listener = listener;
	cm_node->cm_id = cm_info->cm_id;
	ether_addr_copy(cm_node->loc_mac, netdev->dev_addr);
	spin_lock_init(&cm_node->retrans_list_lock);
	cm_node->ack_rcvd = false;

	atomic_set(&cm_node->ref_count, 1);
	/* associate our parent CM core */
	cm_node->cm_core = cm_core;
	cm_node->tcp_cntxt.loc_id = I40IW_CM_DEF_LOCAL_ID;
	cm_node->tcp_cntxt.rcv_wscale = I40IW_CM_DEFAULT_RCV_WND_SCALE;
	cm_node->tcp_cntxt.rcv_wnd =
			I40IW_CM_DEFAULT_RCV_WND_SCALED >> I40IW_CM_DEFAULT_RCV_WND_SCALE;
	ts = current_kernel_time();
	cm_node->tcp_cntxt.loc_seq_num = ts.tv_nsec;
	cm_node->tcp_cntxt.mss = (cm_node->ipv4) ? (iwdev->vsi.mtu - I40IW_MTU_TO_MSS_IPV4) :
				 (iwdev->vsi.mtu - I40IW_MTU_TO_MSS_IPV6);

	cm_node->iwdev = iwdev;
	cm_node->dev = &iwdev->sc_dev;

	if ((cm_node->ipv4 &&
	     i40iw_ipv4_is_loopback(cm_node->loc_addr[0], cm_node->rem_addr[0])) ||
	     (!cm_node->ipv4 && i40iw_ipv6_is_loopback(cm_node->loc_addr,
						       cm_node->rem_addr))) {
		arpindex = i40iw_arp_table(iwdev,
					   cm_node->rem_addr,
					   false,
					   NULL,
					   I40IW_ARP_RESOLVE);
	} else {
		oldarpindex = i40iw_arp_table(iwdev,
					      cm_node->rem_addr,
					      false,
					      NULL,
					      I40IW_ARP_RESOLVE);
		if (cm_node->ipv4)
			arpindex = i40iw_addr_resolve_neigh(iwdev,
							    cm_info->loc_addr[0],
							    cm_info->rem_addr[0],
							    oldarpindex);
		else if (IS_ENABLED(CONFIG_IPV6))
			arpindex = i40iw_addr_resolve_neigh_ipv6(iwdev,
								 cm_info->loc_addr,
								 cm_info->rem_addr,
								 oldarpindex);
		else
			arpindex = -EINVAL;
	}
	if (arpindex < 0) {
		i40iw_pr_err("cm_node arpindex\n");
		kfree(cm_node);
		return NULL;
	}
	ether_addr_copy(cm_node->rem_mac, iwdev->arp_table[arpindex].mac_addr);
	i40iw_add_hte_node(cm_core, cm_node);
	cm_core->stats_nodes_created++;
	return cm_node;
}

/**
 * i40iw_rem_ref_cm_node - destroy an instance of a cm node
 * @cm_node: connection's node
 */
static void i40iw_rem_ref_cm_node(struct i40iw_cm_node *cm_node)
{
	struct i40iw_cm_core *cm_core = cm_node->cm_core;
	struct i40iw_qp *iwqp;
	struct i40iw_cm_info nfo;
	unsigned long flags;

	spin_lock_irqsave(&cm_node->cm_core->ht_lock, flags);
	if (atomic_dec_return(&cm_node->ref_count)) {
		spin_unlock_irqrestore(&cm_node->cm_core->ht_lock, flags);
		return;
	}
	list_del(&cm_node->list);
	spin_unlock_irqrestore(&cm_node->cm_core->ht_lock, flags);

	/* if the node is destroyed before connection was accelerated */
	if (!cm_node->accelerated && cm_node->accept_pend) {
		pr_err("node destroyed before established\n");
		atomic_dec(&cm_node->listener->pend_accepts_cnt);
	}
	if (cm_node->close_entry)
		i40iw_handle_close_entry(cm_node, 0);
	if (cm_node->listener) {
		i40iw_dec_refcnt_listen(cm_core, cm_node->listener, 0, true);
	} else {
		if (!i40iw_port_in_use(cm_core, cm_node->loc_port, true) && cm_node->apbvt_set) {
			i40iw_manage_apbvt(cm_node->iwdev,
					   cm_node->loc_port,
					   I40IW_MANAGE_APBVT_DEL);
			cm_node->apbvt_set = 0;
		}
		i40iw_get_addr_info(cm_node, &nfo);
		if (cm_node->qhash_set) {
			i40iw_manage_qhash(cm_node->iwdev,
					   &nfo,
					   I40IW_QHASH_TYPE_TCP_ESTABLISHED,
					   I40IW_QHASH_MANAGE_TYPE_DELETE,
					   NULL,
					   false);
			cm_node->qhash_set = 0;
		}
	}

	iwqp = cm_node->iwqp;
	if (iwqp) {
		iwqp->cm_node = NULL;
		i40iw_rem_ref(&iwqp->ibqp);
		cm_node->iwqp = NULL;
	} else if (cm_node->qhash_set) {
		i40iw_get_addr_info(cm_node, &nfo);
		i40iw_manage_qhash(cm_node->iwdev,
				   &nfo,
				   I40IW_QHASH_TYPE_TCP_ESTABLISHED,
				   I40IW_QHASH_MANAGE_TYPE_DELETE,
				   NULL,
				   false);
		cm_node->qhash_set = 0;
	}

	cm_node->cm_core->stats_nodes_destroyed++;
	kfree(cm_node);
}

/**
 * i40iw_handle_fin_pkt - FIN packet received
 * @cm_node: connection's node
 */
static void i40iw_handle_fin_pkt(struct i40iw_cm_node *cm_node)
{
	u32 ret;

	switch (cm_node->state) {
	case I40IW_CM_STATE_SYN_RCVD:
	case I40IW_CM_STATE_SYN_SENT:
	case I40IW_CM_STATE_ESTABLISHED:
	case I40IW_CM_STATE_MPAREJ_RCVD:
		cm_node->tcp_cntxt.rcv_nxt++;
		i40iw_cleanup_retrans_entry(cm_node);
		cm_node->state = I40IW_CM_STATE_LAST_ACK;
		i40iw_send_fin(cm_node);
		break;
	case I40IW_CM_STATE_MPAREQ_SENT:
		i40iw_create_event(cm_node, I40IW_CM_EVENT_ABORTED);
		cm_node->tcp_cntxt.rcv_nxt++;
		i40iw_cleanup_retrans_entry(cm_node);
		cm_node->state = I40IW_CM_STATE_CLOSED;
		atomic_inc(&cm_node->ref_count);
		i40iw_send_reset(cm_node);
		break;
	case I40IW_CM_STATE_FIN_WAIT1:
		cm_node->tcp_cntxt.rcv_nxt++;
		i40iw_cleanup_retrans_entry(cm_node);
		cm_node->state = I40IW_CM_STATE_CLOSING;
		i40iw_send_ack(cm_node);
		/*
		 * Wait for ACK as this is simultaneous close.
		 * After we receive ACK, do not send anything.
		 * Just rm the node.
		 */
		break;
	case I40IW_CM_STATE_FIN_WAIT2:
		cm_node->tcp_cntxt.rcv_nxt++;
		i40iw_cleanup_retrans_entry(cm_node);
		cm_node->state = I40IW_CM_STATE_TIME_WAIT;
		i40iw_send_ack(cm_node);
		ret =
		    i40iw_schedule_cm_timer(cm_node, NULL, I40IW_TIMER_TYPE_CLOSE, 1, 0);
		if (ret)
			i40iw_pr_err("node %p state = %d\n", cm_node, cm_node->state);
		break;
	case I40IW_CM_STATE_TIME_WAIT:
		cm_node->tcp_cntxt.rcv_nxt++;
		i40iw_cleanup_retrans_entry(cm_node);
		cm_node->state = I40IW_CM_STATE_CLOSED;
		i40iw_rem_ref_cm_node(cm_node);
		break;
	case I40IW_CM_STATE_OFFLOADED:
	default:
		i40iw_pr_err("bad state node %p state = %d\n", cm_node, cm_node->state);
		break;
	}
}

/**
 * i40iw_handle_rst_pkt - process received RST packet
 * @cm_node: connection's node
 * @rbuf: receive buffer
 */
static void i40iw_handle_rst_pkt(struct i40iw_cm_node *cm_node,
				 struct i40iw_puda_buf *rbuf)
{
	i40iw_cleanup_retrans_entry(cm_node);
	switch (cm_node->state) {
	case I40IW_CM_STATE_SYN_SENT:
	case I40IW_CM_STATE_MPAREQ_SENT:
		switch (cm_node->mpa_frame_rev) {
		case IETF_MPA_V2:
			cm_node->mpa_frame_rev = IETF_MPA_V1;
			/* send a syn and goto syn sent state */
			cm_node->state = I40IW_CM_STATE_SYN_SENT;
			if (i40iw_send_syn(cm_node, 0))
				i40iw_active_open_err(cm_node, false);
			break;
		case IETF_MPA_V1:
		default:
			i40iw_active_open_err(cm_node, false);
			break;
		}
		break;
	case I40IW_CM_STATE_MPAREQ_RCVD:
		atomic_add_return(1, &cm_node->passive_state);
		break;
	case I40IW_CM_STATE_ESTABLISHED:
	case I40IW_CM_STATE_SYN_RCVD:
	case I40IW_CM_STATE_LISTENING:
		i40iw_pr_err("Bad state state = %d\n", cm_node->state);
		i40iw_passive_open_err(cm_node, false);
		break;
	case I40IW_CM_STATE_OFFLOADED:
		i40iw_active_open_err(cm_node, false);
		break;
	case I40IW_CM_STATE_CLOSED:
		break;
	case I40IW_CM_STATE_FIN_WAIT2:
	case I40IW_CM_STATE_FIN_WAIT1:
	case I40IW_CM_STATE_LAST_ACK:
		cm_node->cm_id->rem_ref(cm_node->cm_id);
		/* fall through */
	case I40IW_CM_STATE_TIME_WAIT:
		cm_node->state = I40IW_CM_STATE_CLOSED;
		i40iw_rem_ref_cm_node(cm_node);
		break;
	default:
		break;
	}
}

/**
 * i40iw_handle_rcv_mpa - Process a recv'd mpa buffer
 * @cm_node: connection's node
 * @rbuf: receive buffer
 */
static void i40iw_handle_rcv_mpa(struct i40iw_cm_node *cm_node,
				 struct i40iw_puda_buf *rbuf)
{
	int ret;
	int datasize = rbuf->datalen;
	u8 *dataloc = rbuf->data;

	enum i40iw_cm_event_type type = I40IW_CM_EVENT_UNKNOWN;
	u32 res_type;

	ret = i40iw_parse_mpa(cm_node, dataloc, &res_type, datasize);
	if (ret) {
		if (cm_node->state == I40IW_CM_STATE_MPAREQ_SENT)
			i40iw_active_open_err(cm_node, true);
		else
			i40iw_passive_open_err(cm_node, true);
		return;
	}

	switch (cm_node->state) {
	case I40IW_CM_STATE_ESTABLISHED:
		if (res_type == I40IW_MPA_REQUEST_REJECT)
			i40iw_pr_err("state for reject\n");
		cm_node->state = I40IW_CM_STATE_MPAREQ_RCVD;
		type = I40IW_CM_EVENT_MPA_REQ;
		i40iw_send_ack(cm_node);	/* ACK received MPA request */
		atomic_set(&cm_node->passive_state,
			   I40IW_PASSIVE_STATE_INDICATED);
		break;
	case I40IW_CM_STATE_MPAREQ_SENT:
		i40iw_cleanup_retrans_entry(cm_node);
		if (res_type == I40IW_MPA_REQUEST_REJECT) {
			type = I40IW_CM_EVENT_MPA_REJECT;
			cm_node->state = I40IW_CM_STATE_MPAREJ_RCVD;
		} else {
			type = I40IW_CM_EVENT_CONNECTED;
			cm_node->state = I40IW_CM_STATE_OFFLOADED;
		}
		i40iw_send_ack(cm_node);
		break;
	default:
		pr_err("%s wrong cm_node state =%d\n", __func__, cm_node->state);
		break;
	}
	i40iw_create_event(cm_node, type);
}

/**
 * i40iw_indicate_pkt_err - Send up err event to cm
 * @cm_node: connection's node
 */
static void i40iw_indicate_pkt_err(struct i40iw_cm_node *cm_node)
{
	switch (cm_node->state) {
	case I40IW_CM_STATE_SYN_SENT:
	case I40IW_CM_STATE_MPAREQ_SENT:
		i40iw_active_open_err(cm_node, true);
		break;
	case I40IW_CM_STATE_ESTABLISHED:
	case I40IW_CM_STATE_SYN_RCVD:
		i40iw_passive_open_err(cm_node, true);
		break;
	case I40IW_CM_STATE_OFFLOADED:
	default:
		break;
	}
}

/**
 * i40iw_check_syn - Check for error on received syn ack
 * @cm_node: connection's node
 * @tcph: pointer tcp header
 */
static int i40iw_check_syn(struct i40iw_cm_node *cm_node, struct tcphdr *tcph)
{
	int err = 0;

	if (ntohl(tcph->ack_seq) != cm_node->tcp_cntxt.loc_seq_num) {
		err = 1;
		i40iw_active_open_err(cm_node, true);
	}
	return err;
}

/**
 * i40iw_check_seq - check seq numbers if OK
 * @cm_node: connection's node
 * @tcph: pointer tcp header
 */
static int i40iw_check_seq(struct i40iw_cm_node *cm_node, struct tcphdr *tcph)
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
		err = -1;
	else if (!between(seq, rcv_nxt, (rcv_nxt + rcv_wnd)))
		err = -1;
	if (err) {
		i40iw_pr_err("seq number\n");
		i40iw_indicate_pkt_err(cm_node);
	}
	return err;
}

/**
 * i40iw_handle_syn_pkt - is for Passive node
 * @cm_node: connection's node
 * @rbuf: receive buffer
 */
static void i40iw_handle_syn_pkt(struct i40iw_cm_node *cm_node,
				 struct i40iw_puda_buf *rbuf)
{
	struct tcphdr *tcph = (struct tcphdr *)rbuf->tcph;
	int ret;
	u32 inc_sequence;
	int optionsize;
	struct i40iw_cm_info nfo;

	optionsize = (tcph->doff << 2) - sizeof(struct tcphdr);
	inc_sequence = ntohl(tcph->seq);

	switch (cm_node->state) {
	case I40IW_CM_STATE_SYN_SENT:
	case I40IW_CM_STATE_MPAREQ_SENT:
		/* Rcvd syn on active open connection */
		i40iw_active_open_err(cm_node, 1);
		break;
	case I40IW_CM_STATE_LISTENING:
		/* Passive OPEN */
		if (atomic_read(&cm_node->listener->pend_accepts_cnt) >
		    cm_node->listener->backlog) {
			cm_node->cm_core->stats_backlog_drops++;
			i40iw_passive_open_err(cm_node, false);
			break;
		}
		ret = i40iw_handle_tcp_options(cm_node, tcph, optionsize, 1);
		if (ret) {
			i40iw_passive_open_err(cm_node, false);
			/* drop pkt */
			break;
		}
		cm_node->tcp_cntxt.rcv_nxt = inc_sequence + 1;
		cm_node->accept_pend = 1;
		atomic_inc(&cm_node->listener->pend_accepts_cnt);

		cm_node->state = I40IW_CM_STATE_SYN_RCVD;
		i40iw_get_addr_info(cm_node, &nfo);
		ret = i40iw_manage_qhash(cm_node->iwdev,
					 &nfo,
					 I40IW_QHASH_TYPE_TCP_ESTABLISHED,
					 I40IW_QHASH_MANAGE_TYPE_ADD,
					 (void *)cm_node,
					 false);
		cm_node->qhash_set = true;
		break;
	case I40IW_CM_STATE_CLOSED:
		i40iw_cleanup_retrans_entry(cm_node);
		atomic_inc(&cm_node->ref_count);
		i40iw_send_reset(cm_node);
		break;
	case I40IW_CM_STATE_OFFLOADED:
	case I40IW_CM_STATE_ESTABLISHED:
	case I40IW_CM_STATE_FIN_WAIT1:
	case I40IW_CM_STATE_FIN_WAIT2:
	case I40IW_CM_STATE_MPAREQ_RCVD:
	case I40IW_CM_STATE_LAST_ACK:
	case I40IW_CM_STATE_CLOSING:
	case I40IW_CM_STATE_UNKNOWN:
	default:
		break;
	}
}

/**
 * i40iw_handle_synack_pkt - Process SYN+ACK packet (active side)
 * @cm_node: connection's node
 * @rbuf: receive buffer
 */
static void i40iw_handle_synack_pkt(struct i40iw_cm_node *cm_node,
				    struct i40iw_puda_buf *rbuf)
{
	struct tcphdr *tcph = (struct tcphdr *)rbuf->tcph;
	int ret;
	u32 inc_sequence;
	int optionsize;

	optionsize = (tcph->doff << 2) - sizeof(struct tcphdr);
	inc_sequence = ntohl(tcph->seq);
	switch (cm_node->state) {
	case I40IW_CM_STATE_SYN_SENT:
		i40iw_cleanup_retrans_entry(cm_node);
		/* active open */
		if (i40iw_check_syn(cm_node, tcph)) {
			i40iw_pr_err("check syn fail\n");
			return;
		}
		cm_node->tcp_cntxt.rem_ack_num = ntohl(tcph->ack_seq);
		/* setup options */
		ret = i40iw_handle_tcp_options(cm_node, tcph, optionsize, 0);
		if (ret) {
			i40iw_debug(cm_node->dev,
				    I40IW_DEBUG_CM,
				    "cm_node=%p tcp_options failed\n",
				    cm_node);
			break;
		}
		i40iw_cleanup_retrans_entry(cm_node);
		cm_node->tcp_cntxt.rcv_nxt = inc_sequence + 1;
		i40iw_send_ack(cm_node);	/* ACK  for the syn_ack */
		ret = i40iw_send_mpa_request(cm_node);
		if (ret) {
			i40iw_debug(cm_node->dev,
				    I40IW_DEBUG_CM,
				    "cm_node=%p i40iw_send_mpa_request failed\n",
				    cm_node);
			break;
		}
		cm_node->state = I40IW_CM_STATE_MPAREQ_SENT;
		break;
	case I40IW_CM_STATE_MPAREQ_RCVD:
		i40iw_passive_open_err(cm_node, true);
		break;
	case I40IW_CM_STATE_LISTENING:
		cm_node->tcp_cntxt.loc_seq_num = ntohl(tcph->ack_seq);
		i40iw_cleanup_retrans_entry(cm_node);
		cm_node->state = I40IW_CM_STATE_CLOSED;
		i40iw_send_reset(cm_node);
		break;
	case I40IW_CM_STATE_CLOSED:
		cm_node->tcp_cntxt.loc_seq_num = ntohl(tcph->ack_seq);
		i40iw_cleanup_retrans_entry(cm_node);
		atomic_inc(&cm_node->ref_count);
		i40iw_send_reset(cm_node);
		break;
	case I40IW_CM_STATE_ESTABLISHED:
	case I40IW_CM_STATE_FIN_WAIT1:
	case I40IW_CM_STATE_FIN_WAIT2:
	case I40IW_CM_STATE_LAST_ACK:
	case I40IW_CM_STATE_OFFLOADED:
	case I40IW_CM_STATE_CLOSING:
	case I40IW_CM_STATE_UNKNOWN:
	case I40IW_CM_STATE_MPAREQ_SENT:
	default:
		break;
	}
}

/**
 * i40iw_handle_ack_pkt - process packet with ACK
 * @cm_node: connection's node
 * @rbuf: receive buffer
 */
static int i40iw_handle_ack_pkt(struct i40iw_cm_node *cm_node,
				struct i40iw_puda_buf *rbuf)
{
	struct tcphdr *tcph = (struct tcphdr *)rbuf->tcph;
	u32 inc_sequence;
	int ret = 0;
	int optionsize;
	u32 datasize = rbuf->datalen;

	optionsize = (tcph->doff << 2) - sizeof(struct tcphdr);

	if (i40iw_check_seq(cm_node, tcph))
		return -EINVAL;

	inc_sequence = ntohl(tcph->seq);
	switch (cm_node->state) {
	case I40IW_CM_STATE_SYN_RCVD:
		i40iw_cleanup_retrans_entry(cm_node);
		ret = i40iw_handle_tcp_options(cm_node, tcph, optionsize, 1);
		if (ret)
			break;
		cm_node->tcp_cntxt.rem_ack_num = ntohl(tcph->ack_seq);
		cm_node->state = I40IW_CM_STATE_ESTABLISHED;
		if (datasize) {
			cm_node->tcp_cntxt.rcv_nxt = inc_sequence + datasize;
			i40iw_handle_rcv_mpa(cm_node, rbuf);
		}
		break;
	case I40IW_CM_STATE_ESTABLISHED:
		i40iw_cleanup_retrans_entry(cm_node);
		if (datasize) {
			cm_node->tcp_cntxt.rcv_nxt = inc_sequence + datasize;
			i40iw_handle_rcv_mpa(cm_node, rbuf);
		}
		break;
	case I40IW_CM_STATE_MPAREQ_SENT:
		cm_node->tcp_cntxt.rem_ack_num = ntohl(tcph->ack_seq);
		if (datasize) {
			cm_node->tcp_cntxt.rcv_nxt = inc_sequence + datasize;
			cm_node->ack_rcvd = false;
			i40iw_handle_rcv_mpa(cm_node, rbuf);
		} else {
			cm_node->ack_rcvd = true;
		}
		break;
	case I40IW_CM_STATE_LISTENING:
		i40iw_cleanup_retrans_entry(cm_node);
		cm_node->state = I40IW_CM_STATE_CLOSED;
		i40iw_send_reset(cm_node);
		break;
	case I40IW_CM_STATE_CLOSED:
		i40iw_cleanup_retrans_entry(cm_node);
		atomic_inc(&cm_node->ref_count);
		i40iw_send_reset(cm_node);
		break;
	case I40IW_CM_STATE_LAST_ACK:
	case I40IW_CM_STATE_CLOSING:
		i40iw_cleanup_retrans_entry(cm_node);
		cm_node->state = I40IW_CM_STATE_CLOSED;
		if (!cm_node->accept_pend)
			cm_node->cm_id->rem_ref(cm_node->cm_id);
		i40iw_rem_ref_cm_node(cm_node);
		break;
	case I40IW_CM_STATE_FIN_WAIT1:
		i40iw_cleanup_retrans_entry(cm_node);
		cm_node->state = I40IW_CM_STATE_FIN_WAIT2;
		break;
	case I40IW_CM_STATE_SYN_SENT:
	case I40IW_CM_STATE_FIN_WAIT2:
	case I40IW_CM_STATE_OFFLOADED:
	case I40IW_CM_STATE_MPAREQ_RCVD:
	case I40IW_CM_STATE_UNKNOWN:
	default:
		i40iw_cleanup_retrans_entry(cm_node);
		break;
	}
	return ret;
}

/**
 * i40iw_process_packet - process cm packet
 * @cm_node: connection's node
 * @rbuf: receive buffer
 */
static void i40iw_process_packet(struct i40iw_cm_node *cm_node,
				 struct i40iw_puda_buf *rbuf)
{
	enum i40iw_tcpip_pkt_type pkt_type = I40IW_PKT_TYPE_UNKNOWN;
	struct tcphdr *tcph = (struct tcphdr *)rbuf->tcph;
	u32 fin_set = 0;
	int ret;

	if (tcph->rst) {
		pkt_type = I40IW_PKT_TYPE_RST;
	} else if (tcph->syn) {
		pkt_type = I40IW_PKT_TYPE_SYN;
		if (tcph->ack)
			pkt_type = I40IW_PKT_TYPE_SYNACK;
	} else if (tcph->ack) {
		pkt_type = I40IW_PKT_TYPE_ACK;
	}
	if (tcph->fin)
		fin_set = 1;

	switch (pkt_type) {
	case I40IW_PKT_TYPE_SYN:
		i40iw_handle_syn_pkt(cm_node, rbuf);
		break;
	case I40IW_PKT_TYPE_SYNACK:
		i40iw_handle_synack_pkt(cm_node, rbuf);
		break;
	case I40IW_PKT_TYPE_ACK:
		ret = i40iw_handle_ack_pkt(cm_node, rbuf);
		if (fin_set && !ret)
			i40iw_handle_fin_pkt(cm_node);
		break;
	case I40IW_PKT_TYPE_RST:
		i40iw_handle_rst_pkt(cm_node, rbuf);
		break;
	default:
		if (fin_set &&
		    (!i40iw_check_seq(cm_node, (struct tcphdr *)rbuf->tcph)))
			i40iw_handle_fin_pkt(cm_node);
		break;
	}
}

/**
 * i40iw_make_listen_node - create a listen node with params
 * @cm_core: cm's core
 * @iwdev: iwarp device structure
 * @cm_info: quad info for connection
 */
static struct i40iw_cm_listener *i40iw_make_listen_node(
					struct i40iw_cm_core *cm_core,
					struct i40iw_device *iwdev,
					struct i40iw_cm_info *cm_info)
{
	struct i40iw_cm_listener *listener;
	unsigned long flags;

	/* cannot have multiple matching listeners */
	listener = i40iw_find_listener(cm_core, cm_info->loc_addr,
				       cm_info->loc_port,
				       cm_info->vlan_id,
				       I40IW_CM_LISTENER_EITHER_STATE);
	if (listener &&
	    (listener->listener_state == I40IW_CM_LISTENER_ACTIVE_STATE)) {
		atomic_dec(&listener->ref_count);
		i40iw_debug(cm_core->dev,
			    I40IW_DEBUG_CM,
			    "Not creating listener since it already exists\n");
		return NULL;
	}

	if (!listener) {
		/* create a CM listen node (1/2 node to compare incoming traffic to) */
		listener = kzalloc(sizeof(*listener), GFP_ATOMIC);
		if (!listener)
			return NULL;
		cm_core->stats_listen_nodes_created++;
		memcpy(listener->loc_addr, cm_info->loc_addr, sizeof(listener->loc_addr));
		listener->loc_port = cm_info->loc_port;

		INIT_LIST_HEAD(&listener->child_listen_list);

		atomic_set(&listener->ref_count, 1);
	} else {
		listener->reused_node = 1;
	}

	listener->cm_id = cm_info->cm_id;
	listener->ipv4 = cm_info->ipv4;
	listener->vlan_id = cm_info->vlan_id;
	atomic_set(&listener->pend_accepts_cnt, 0);
	listener->cm_core = cm_core;
	listener->iwdev = iwdev;

	listener->backlog = cm_info->backlog;
	listener->listener_state = I40IW_CM_LISTENER_ACTIVE_STATE;

	if (!listener->reused_node) {
		spin_lock_irqsave(&cm_core->listen_list_lock, flags);
		list_add(&listener->list, &cm_core->listen_nodes);
		spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);
	}

	return listener;
}

/**
 * i40iw_create_cm_node - make a connection node with params
 * @cm_core: cm's core
 * @iwdev: iwarp device structure
 * @private_data_len: len to provate data for mpa request
 * @private_data: pointer to private data for connection
 * @cm_info: quad info for connection
 */
static struct i40iw_cm_node *i40iw_create_cm_node(
					struct i40iw_cm_core *cm_core,
					struct i40iw_device *iwdev,
					u16 private_data_len,
					void *private_data,
					struct i40iw_cm_info *cm_info)
{
	struct i40iw_cm_node *cm_node;
	struct i40iw_cm_listener *loopback_remotelistener;
	struct i40iw_cm_node *loopback_remotenode;
	struct i40iw_cm_info loopback_cm_info;

	/* create a CM connection node */
	cm_node = i40iw_make_cm_node(cm_core, iwdev, cm_info, NULL);
	if (!cm_node)
		return ERR_PTR(-ENOMEM);
	/* set our node side to client (active) side */
	cm_node->tcp_cntxt.client = 1;
	cm_node->tcp_cntxt.rcv_wscale = I40IW_CM_DEFAULT_RCV_WND_SCALE;

	if (!memcmp(cm_info->loc_addr, cm_info->rem_addr, sizeof(cm_info->loc_addr))) {
		loopback_remotelistener = i40iw_find_listener(
						cm_core,
						cm_info->rem_addr,
						cm_node->rem_port,
						cm_node->vlan_id,
						I40IW_CM_LISTENER_ACTIVE_STATE);
		if (!loopback_remotelistener) {
			i40iw_rem_ref_cm_node(cm_node);
			return ERR_PTR(-ECONNREFUSED);
		} else {
			loopback_cm_info = *cm_info;
			loopback_cm_info.loc_port = cm_info->rem_port;
			loopback_cm_info.rem_port = cm_info->loc_port;
			loopback_cm_info.cm_id = loopback_remotelistener->cm_id;
			loopback_cm_info.ipv4 = cm_info->ipv4;
			loopback_remotenode = i40iw_make_cm_node(cm_core,
								 iwdev,
								 &loopback_cm_info,
								 loopback_remotelistener);
			if (!loopback_remotenode) {
				i40iw_rem_ref_cm_node(cm_node);
				return ERR_PTR(-ENOMEM);
			}
			cm_core->stats_loopbacks++;
			loopback_remotenode->loopbackpartner = cm_node;
			loopback_remotenode->tcp_cntxt.rcv_wscale =
				I40IW_CM_DEFAULT_RCV_WND_SCALE;
			cm_node->loopbackpartner = loopback_remotenode;
			memcpy(loopback_remotenode->pdata_buf, private_data,
			       private_data_len);
			loopback_remotenode->pdata.size = private_data_len;

			cm_node->state = I40IW_CM_STATE_OFFLOADED;
			cm_node->tcp_cntxt.rcv_nxt =
				loopback_remotenode->tcp_cntxt.loc_seq_num;
			loopback_remotenode->tcp_cntxt.rcv_nxt =
				cm_node->tcp_cntxt.loc_seq_num;
			cm_node->tcp_cntxt.max_snd_wnd =
				loopback_remotenode->tcp_cntxt.rcv_wnd;
			loopback_remotenode->tcp_cntxt.max_snd_wnd = cm_node->tcp_cntxt.rcv_wnd;
			cm_node->tcp_cntxt.snd_wnd = loopback_remotenode->tcp_cntxt.rcv_wnd;
			loopback_remotenode->tcp_cntxt.snd_wnd = cm_node->tcp_cntxt.rcv_wnd;
			cm_node->tcp_cntxt.snd_wscale = loopback_remotenode->tcp_cntxt.rcv_wscale;
			loopback_remotenode->tcp_cntxt.snd_wscale = cm_node->tcp_cntxt.rcv_wscale;
			loopback_remotenode->state = I40IW_CM_STATE_MPAREQ_RCVD;
			i40iw_create_event(loopback_remotenode, I40IW_CM_EVENT_MPA_REQ);
		}
		return cm_node;
	}

	cm_node->pdata.size = private_data_len;
	cm_node->pdata.addr = cm_node->pdata_buf;

	memcpy(cm_node->pdata_buf, private_data, private_data_len);

	cm_node->state = I40IW_CM_STATE_SYN_SENT;
	return cm_node;
}

/**
 * i40iw_cm_reject - reject and teardown a connection
 * @cm_node: connection's node
 * @pdate: ptr to private data for reject
 * @plen: size of private data
 */
static int i40iw_cm_reject(struct i40iw_cm_node *cm_node, const void *pdata, u8 plen)
{
	int ret = 0;
	int err;
	int passive_state;
	struct iw_cm_id *cm_id = cm_node->cm_id;
	struct i40iw_cm_node *loopback = cm_node->loopbackpartner;

	if (cm_node->tcp_cntxt.client)
		return ret;
	i40iw_cleanup_retrans_entry(cm_node);

	if (!loopback) {
		passive_state = atomic_add_return(1, &cm_node->passive_state);
		if (passive_state == I40IW_SEND_RESET_EVENT) {
			cm_node->state = I40IW_CM_STATE_CLOSED;
			i40iw_rem_ref_cm_node(cm_node);
		} else {
			if (cm_node->state == I40IW_CM_STATE_LISTENER_DESTROYED) {
				i40iw_rem_ref_cm_node(cm_node);
			} else {
				ret = i40iw_send_mpa_reject(cm_node, pdata, plen);
				if (ret) {
					cm_node->state = I40IW_CM_STATE_CLOSED;
					err = i40iw_send_reset(cm_node);
					if (err)
						i40iw_pr_err("send reset failed\n");
				} else {
					cm_id->add_ref(cm_id);
				}
			}
		}
	} else {
		cm_node->cm_id = NULL;
		if (cm_node->state == I40IW_CM_STATE_LISTENER_DESTROYED) {
			i40iw_rem_ref_cm_node(cm_node);
			i40iw_rem_ref_cm_node(loopback);
		} else {
			ret = i40iw_send_cm_event(loopback,
						  loopback->cm_id,
						  IW_CM_EVENT_CONNECT_REPLY,
						  -ECONNREFUSED);
			i40iw_rem_ref_cm_node(cm_node);
			loopback->state = I40IW_CM_STATE_CLOSING;

			cm_id = loopback->cm_id;
			i40iw_rem_ref_cm_node(loopback);
			cm_id->rem_ref(cm_id);
		}
	}

	return ret;
}

/**
 * i40iw_cm_close - close of cm connection
 * @cm_node: connection's node
 */
static int i40iw_cm_close(struct i40iw_cm_node *cm_node)
{
	int ret = 0;

	if (!cm_node)
		return -EINVAL;

	switch (cm_node->state) {
	case I40IW_CM_STATE_SYN_RCVD:
	case I40IW_CM_STATE_SYN_SENT:
	case I40IW_CM_STATE_ONE_SIDE_ESTABLISHED:
	case I40IW_CM_STATE_ESTABLISHED:
	case I40IW_CM_STATE_ACCEPTING:
	case I40IW_CM_STATE_MPAREQ_SENT:
	case I40IW_CM_STATE_MPAREQ_RCVD:
		i40iw_cleanup_retrans_entry(cm_node);
		i40iw_send_reset(cm_node);
		break;
	case I40IW_CM_STATE_CLOSE_WAIT:
		cm_node->state = I40IW_CM_STATE_LAST_ACK;
		i40iw_send_fin(cm_node);
		break;
	case I40IW_CM_STATE_FIN_WAIT1:
	case I40IW_CM_STATE_FIN_WAIT2:
	case I40IW_CM_STATE_LAST_ACK:
	case I40IW_CM_STATE_TIME_WAIT:
	case I40IW_CM_STATE_CLOSING:
		ret = -1;
		break;
	case I40IW_CM_STATE_LISTENING:
		i40iw_cleanup_retrans_entry(cm_node);
		i40iw_send_reset(cm_node);
		break;
	case I40IW_CM_STATE_MPAREJ_RCVD:
	case I40IW_CM_STATE_UNKNOWN:
	case I40IW_CM_STATE_INITED:
	case I40IW_CM_STATE_CLOSED:
	case I40IW_CM_STATE_LISTENER_DESTROYED:
		i40iw_rem_ref_cm_node(cm_node);
		break;
	case I40IW_CM_STATE_OFFLOADED:
		if (cm_node->send_entry)
			i40iw_pr_err("send_entry\n");
		i40iw_rem_ref_cm_node(cm_node);
		break;
	}
	return ret;
}

/**
 * i40iw_receive_ilq - recv an ETHERNET packet, and process it
 * through CM
 * @vsi: pointer to the vsi structure
 * @rbuf: receive buffer
 */
void i40iw_receive_ilq(struct i40iw_sc_vsi *vsi, struct i40iw_puda_buf *rbuf)
{
	struct i40iw_cm_node *cm_node;
	struct i40iw_cm_listener *listener;
	struct iphdr *iph;
	struct ipv6hdr *ip6h;
	struct tcphdr *tcph;
	struct i40iw_cm_info cm_info;
	struct i40iw_sc_dev *dev = vsi->dev;
	struct i40iw_device *iwdev = (struct i40iw_device *)dev->back_dev;
	struct i40iw_cm_core *cm_core = &iwdev->cm_core;
	struct vlan_ethhdr *ethh;
	u16 vtag;

	/* if vlan, then maclen = 18 else 14 */
	iph = (struct iphdr *)rbuf->iph;
	memset(&cm_info, 0, sizeof(cm_info));

	i40iw_debug_buf(dev,
			I40IW_DEBUG_ILQ,
			"RECEIVE ILQ BUFFER",
			rbuf->mem.va,
			rbuf->totallen);
	ethh = (struct vlan_ethhdr *)rbuf->mem.va;

	if (ethh->h_vlan_proto == htons(ETH_P_8021Q)) {
		vtag = ntohs(ethh->h_vlan_TCI);
		cm_info.user_pri = (vtag & VLAN_PRIO_MASK) >> VLAN_PRIO_SHIFT;
		cm_info.vlan_id = vtag & VLAN_VID_MASK;
		i40iw_debug(cm_core->dev,
			    I40IW_DEBUG_CM,
			    "%s vlan_id=%d\n",
			    __func__,
			    cm_info.vlan_id);
	} else {
		cm_info.vlan_id = I40IW_NO_VLAN;
	}
	tcph = (struct tcphdr *)rbuf->tcph;

	if (rbuf->ipv4) {
		cm_info.loc_addr[0] = ntohl(iph->daddr);
		cm_info.rem_addr[0] = ntohl(iph->saddr);
		cm_info.ipv4 = true;
		cm_info.tos = iph->tos;
	} else {
		ip6h = (struct ipv6hdr *)rbuf->iph;
		i40iw_copy_ip_ntohl(cm_info.loc_addr,
				    ip6h->daddr.in6_u.u6_addr32);
		i40iw_copy_ip_ntohl(cm_info.rem_addr,
				    ip6h->saddr.in6_u.u6_addr32);
		cm_info.ipv4 = false;
		cm_info.tos = (ip6h->priority << 4) | (ip6h->flow_lbl[0] >> 4);
	}
	cm_info.loc_port = ntohs(tcph->dest);
	cm_info.rem_port = ntohs(tcph->source);
	cm_node = i40iw_find_node(cm_core,
				  cm_info.rem_port,
				  cm_info.rem_addr,
				  cm_info.loc_port,
				  cm_info.loc_addr,
				  true);

	if (!cm_node) {
		/* Only type of packet accepted are for */
		/* the PASSIVE open (syn only) */
		if (!tcph->syn || tcph->ack)
			return;
		listener =
		    i40iw_find_listener(cm_core,
					cm_info.loc_addr,
					cm_info.loc_port,
					cm_info.vlan_id,
					I40IW_CM_LISTENER_ACTIVE_STATE);
		if (!listener) {
			cm_info.cm_id = NULL;
			i40iw_debug(cm_core->dev,
				    I40IW_DEBUG_CM,
				    "%s no listener found\n",
				    __func__);
			return;
		}
		cm_info.cm_id = listener->cm_id;
		cm_node = i40iw_make_cm_node(cm_core, iwdev, &cm_info, listener);
		if (!cm_node) {
			i40iw_debug(cm_core->dev,
				    I40IW_DEBUG_CM,
				    "%s allocate node failed\n",
				    __func__);
			atomic_dec(&listener->ref_count);
			return;
		}
		if (!tcph->rst && !tcph->fin) {
			cm_node->state = I40IW_CM_STATE_LISTENING;
		} else {
			i40iw_rem_ref_cm_node(cm_node);
			return;
		}
		atomic_inc(&cm_node->ref_count);
	} else if (cm_node->state == I40IW_CM_STATE_OFFLOADED) {
		i40iw_rem_ref_cm_node(cm_node);
		return;
	}
	i40iw_process_packet(cm_node, rbuf);
	i40iw_rem_ref_cm_node(cm_node);
}

/**
 * i40iw_setup_cm_core - allocate a top level instance of a cm
 * core
 * @iwdev: iwarp device structure
 */
void i40iw_setup_cm_core(struct i40iw_device *iwdev)
{
	struct i40iw_cm_core *cm_core = &iwdev->cm_core;

	cm_core->iwdev = iwdev;
	cm_core->dev = &iwdev->sc_dev;

	INIT_LIST_HEAD(&cm_core->connected_nodes);
	INIT_LIST_HEAD(&cm_core->listen_nodes);

	timer_setup(&cm_core->tcp_timer, i40iw_cm_timer_tick, 0);

	spin_lock_init(&cm_core->ht_lock);
	spin_lock_init(&cm_core->listen_list_lock);

	cm_core->event_wq = alloc_ordered_workqueue("iwewq",
						    WQ_MEM_RECLAIM);

	cm_core->disconn_wq = alloc_ordered_workqueue("iwdwq",
						      WQ_MEM_RECLAIM);
}

/**
 * i40iw_cleanup_cm_core - deallocate a top level instance of a
 * cm core
 * @cm_core: cm's core
 */
void i40iw_cleanup_cm_core(struct i40iw_cm_core *cm_core)
{
	unsigned long flags;

	if (!cm_core)
		return;

	spin_lock_irqsave(&cm_core->ht_lock, flags);
	if (timer_pending(&cm_core->tcp_timer))
		del_timer_sync(&cm_core->tcp_timer);
	spin_unlock_irqrestore(&cm_core->ht_lock, flags);

	destroy_workqueue(cm_core->event_wq);
	destroy_workqueue(cm_core->disconn_wq);
}

/**
 * i40iw_init_tcp_ctx - setup qp context
 * @cm_node: connection's node
 * @tcp_info: offload info for tcp
 * @iwqp: associate qp for the connection
 */
static void i40iw_init_tcp_ctx(struct i40iw_cm_node *cm_node,
			       struct i40iw_tcp_offload_info *tcp_info,
			       struct i40iw_qp *iwqp)
{
	tcp_info->ipv4 = cm_node->ipv4;
	tcp_info->drop_ooo_seg = true;
	tcp_info->wscale = true;
	tcp_info->ignore_tcp_opt = true;
	tcp_info->ignore_tcp_uns_opt = true;
	tcp_info->no_nagle = false;

	tcp_info->ttl = I40IW_DEFAULT_TTL;
	tcp_info->rtt_var = cpu_to_le32(I40IW_DEFAULT_RTT_VAR);
	tcp_info->ss_thresh = cpu_to_le32(I40IW_DEFAULT_SS_THRESH);
	tcp_info->rexmit_thresh = I40IW_DEFAULT_REXMIT_THRESH;

	tcp_info->tcp_state = I40IW_TCP_STATE_ESTABLISHED;
	tcp_info->snd_wscale = cm_node->tcp_cntxt.snd_wscale;
	tcp_info->rcv_wscale = cm_node->tcp_cntxt.rcv_wscale;

	tcp_info->snd_nxt = cpu_to_le32(cm_node->tcp_cntxt.loc_seq_num);
	tcp_info->snd_wnd = cpu_to_le32(cm_node->tcp_cntxt.snd_wnd);
	tcp_info->rcv_nxt = cpu_to_le32(cm_node->tcp_cntxt.rcv_nxt);
	tcp_info->snd_max = cpu_to_le32(cm_node->tcp_cntxt.loc_seq_num);

	tcp_info->snd_una = cpu_to_le32(cm_node->tcp_cntxt.loc_seq_num);
	tcp_info->cwnd = cpu_to_le32(2 * cm_node->tcp_cntxt.mss);
	tcp_info->snd_wl1 = cpu_to_le32(cm_node->tcp_cntxt.rcv_nxt);
	tcp_info->snd_wl2 = cpu_to_le32(cm_node->tcp_cntxt.loc_seq_num);
	tcp_info->max_snd_window = cpu_to_le32(cm_node->tcp_cntxt.max_snd_wnd);
	tcp_info->rcv_wnd = cpu_to_le32(cm_node->tcp_cntxt.rcv_wnd <<
					cm_node->tcp_cntxt.rcv_wscale);

	tcp_info->flow_label = 0;
	tcp_info->snd_mss = cpu_to_le32(((u32)cm_node->tcp_cntxt.mss));
	if (cm_node->vlan_id < VLAN_TAG_PRESENT) {
		tcp_info->insert_vlan_tag = true;
		tcp_info->vlan_tag = cpu_to_le16(((u16)cm_node->user_pri << I40IW_VLAN_PRIO_SHIFT) |
						  cm_node->vlan_id);
	}
	if (cm_node->ipv4) {
		tcp_info->src_port = cpu_to_le16(cm_node->loc_port);
		tcp_info->dst_port = cpu_to_le16(cm_node->rem_port);

		tcp_info->dest_ip_addr3 = cpu_to_le32(cm_node->rem_addr[0]);
		tcp_info->local_ipaddr3 = cpu_to_le32(cm_node->loc_addr[0]);
		tcp_info->arp_idx =
			cpu_to_le16((u16)i40iw_arp_table(
							 iwqp->iwdev,
							 &tcp_info->dest_ip_addr3,
							 true,
							 NULL,
							 I40IW_ARP_RESOLVE));
	} else {
		tcp_info->src_port = cpu_to_le16(cm_node->loc_port);
		tcp_info->dst_port = cpu_to_le16(cm_node->rem_port);
		tcp_info->dest_ip_addr0 = cpu_to_le32(cm_node->rem_addr[0]);
		tcp_info->dest_ip_addr1 = cpu_to_le32(cm_node->rem_addr[1]);
		tcp_info->dest_ip_addr2 = cpu_to_le32(cm_node->rem_addr[2]);
		tcp_info->dest_ip_addr3 = cpu_to_le32(cm_node->rem_addr[3]);
		tcp_info->local_ipaddr0 = cpu_to_le32(cm_node->loc_addr[0]);
		tcp_info->local_ipaddr1 = cpu_to_le32(cm_node->loc_addr[1]);
		tcp_info->local_ipaddr2 = cpu_to_le32(cm_node->loc_addr[2]);
		tcp_info->local_ipaddr3 = cpu_to_le32(cm_node->loc_addr[3]);
		tcp_info->arp_idx =
			cpu_to_le16((u16)i40iw_arp_table(
							 iwqp->iwdev,
							 &tcp_info->dest_ip_addr0,
							 false,
							 NULL,
							 I40IW_ARP_RESOLVE));
	}
}

/**
 * i40iw_cm_init_tsa_conn - setup qp for RTS
 * @iwqp: associate qp for the connection
 * @cm_node: connection's node
 */
static void i40iw_cm_init_tsa_conn(struct i40iw_qp *iwqp,
				   struct i40iw_cm_node *cm_node)
{
	struct i40iw_tcp_offload_info tcp_info;
	struct i40iwarp_offload_info *iwarp_info;
	struct i40iw_qp_host_ctx_info *ctx_info;
	struct i40iw_device *iwdev = iwqp->iwdev;
	struct i40iw_sc_dev *dev = &iwqp->iwdev->sc_dev;

	memset(&tcp_info, 0x00, sizeof(struct i40iw_tcp_offload_info));
	iwarp_info = &iwqp->iwarp_info;
	ctx_info = &iwqp->ctx_info;

	ctx_info->tcp_info = &tcp_info;
	ctx_info->send_cq_num = iwqp->iwscq->sc_cq.cq_uk.cq_id;
	ctx_info->rcv_cq_num = iwqp->iwrcq->sc_cq.cq_uk.cq_id;

	iwarp_info->ord_size = cm_node->ord_size;
	iwarp_info->ird_size = i40iw_derive_hw_ird_setting(cm_node->ird_size);

	if (iwarp_info->ord_size == 1)
		iwarp_info->ord_size = 2;

	iwarp_info->rd_enable = true;
	iwarp_info->rdmap_ver = 1;
	iwarp_info->ddp_ver = 1;

	iwarp_info->pd_id = iwqp->iwpd->sc_pd.pd_id;

	ctx_info->tcp_info_valid = true;
	ctx_info->iwarp_info_valid = true;
	ctx_info->add_to_qoslist = true;
	ctx_info->user_pri = cm_node->user_pri;

	i40iw_init_tcp_ctx(cm_node, &tcp_info, iwqp);
	if (cm_node->snd_mark_en) {
		iwarp_info->snd_mark_en = true;
		iwarp_info->snd_mark_offset = (tcp_info.snd_nxt &
				SNDMARKER_SEQNMASK) + cm_node->lsmm_size;
	}

	cm_node->state = I40IW_CM_STATE_OFFLOADED;
	tcp_info.tcp_state = I40IW_TCP_STATE_ESTABLISHED;
	tcp_info.src_mac_addr_idx = iwdev->mac_ip_table_idx;
	tcp_info.tos = cm_node->tos;

	dev->iw_priv_qp_ops->qp_setctx(&iwqp->sc_qp, (u64 *)(iwqp->host_ctx.va), ctx_info);

	/* once tcp_info is set, no need to do it again */
	ctx_info->tcp_info_valid = false;
	ctx_info->iwarp_info_valid = false;
	ctx_info->add_to_qoslist = false;
}

/**
 * i40iw_cm_disconn - when a connection is being closed
 * @iwqp: associate qp for the connection
 */
void i40iw_cm_disconn(struct i40iw_qp *iwqp)
{
	struct disconn_work *work;
	struct i40iw_device *iwdev = iwqp->iwdev;
	struct i40iw_cm_core *cm_core = &iwdev->cm_core;
	unsigned long flags;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return;	/* Timer will clean up */

	spin_lock_irqsave(&iwdev->qptable_lock, flags);
	if (!iwdev->qp_table[iwqp->ibqp.qp_num]) {
		spin_unlock_irqrestore(&iwdev->qptable_lock, flags);
		i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_CM,
			    "%s qp_id %d is already freed\n",
			     __func__, iwqp->ibqp.qp_num);
		kfree(work);
		return;
	}
	i40iw_add_ref(&iwqp->ibqp);
	spin_unlock_irqrestore(&iwdev->qptable_lock, flags);

	work->iwqp = iwqp;
	INIT_WORK(&work->work, i40iw_disconnect_worker);
	queue_work(cm_core->disconn_wq, &work->work);
	return;
}

/**
 * i40iw_qp_disconnect - free qp and close cm
 * @iwqp: associate qp for the connection
 */
static void i40iw_qp_disconnect(struct i40iw_qp *iwqp)
{
	struct i40iw_device *iwdev;
	struct i40iw_ib_device *iwibdev;

	iwdev = to_iwdev(iwqp->ibqp.device);
	if (!iwdev) {
		i40iw_pr_err("iwdev == NULL\n");
		return;
	}

	iwibdev = iwdev->iwibdev;

	if (iwqp->active_conn) {
		/* indicate this connection is NOT active */
		iwqp->active_conn = 0;
	} else {
		/* Need to free the Last Streaming Mode Message */
		if (iwqp->ietf_mem.va) {
			if (iwqp->lsmm_mr)
				iwibdev->ibdev.dereg_mr(iwqp->lsmm_mr);
			i40iw_free_dma_mem(iwdev->sc_dev.hw, &iwqp->ietf_mem);
		}
	}

	/* close the CM node down if it is still active */
	if (iwqp->cm_node) {
		i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_CM, "%s Call close API\n", __func__);
		i40iw_cm_close(iwqp->cm_node);
	}
}

/**
 * i40iw_cm_disconn_true - called by worker thread to disconnect qp
 * @iwqp: associate qp for the connection
 */
static void i40iw_cm_disconn_true(struct i40iw_qp *iwqp)
{
	struct iw_cm_id *cm_id;
	struct i40iw_device *iwdev;
	struct i40iw_sc_qp *qp = &iwqp->sc_qp;
	u16 last_ae;
	u8 original_hw_tcp_state;
	u8 original_ibqp_state;
	int disconn_status = 0;
	int issue_disconn = 0;
	int issue_close = 0;
	int issue_flush = 0;
	struct ib_event ibevent;
	unsigned long flags;
	int ret;

	if (!iwqp) {
		i40iw_pr_err("iwqp == NULL\n");
		return;
	}

	spin_lock_irqsave(&iwqp->lock, flags);
	cm_id = iwqp->cm_id;
	/* make sure we havent already closed this connection */
	if (!cm_id) {
		spin_unlock_irqrestore(&iwqp->lock, flags);
		return;
	}

	iwdev = to_iwdev(iwqp->ibqp.device);

	original_hw_tcp_state = iwqp->hw_tcp_state;
	original_ibqp_state = iwqp->ibqp_state;
	last_ae = iwqp->last_aeq;

	if (qp->term_flags) {
		issue_disconn = 1;
		issue_close = 1;
		iwqp->cm_id = NULL;
		/*When term timer expires after cm_timer, don't want
		 *terminate-handler to issue cm_disconn which can re-free
		 *a QP even after its refcnt=0.
		 */
		i40iw_terminate_del_timer(qp);
		if (!iwqp->flush_issued) {
			iwqp->flush_issued = 1;
			issue_flush = 1;
		}
	} else if ((original_hw_tcp_state == I40IW_TCP_STATE_CLOSE_WAIT) ||
		   ((original_ibqp_state == IB_QPS_RTS) &&
		    (last_ae == I40IW_AE_LLP_CONNECTION_RESET))) {
		issue_disconn = 1;
		if (last_ae == I40IW_AE_LLP_CONNECTION_RESET)
			disconn_status = -ECONNRESET;
	}

	if (((original_hw_tcp_state == I40IW_TCP_STATE_CLOSED) ||
	     (original_hw_tcp_state == I40IW_TCP_STATE_TIME_WAIT) ||
	     (last_ae == I40IW_AE_RDMAP_ROE_BAD_LLP_CLOSE) ||
	     (last_ae == I40IW_AE_LLP_CONNECTION_RESET) ||
	      iwdev->reset)) {
		issue_close = 1;
		iwqp->cm_id = NULL;
		if (!iwqp->flush_issued) {
			iwqp->flush_issued = 1;
			issue_flush = 1;
		}
	}

	spin_unlock_irqrestore(&iwqp->lock, flags);
	if (issue_flush && !iwqp->destroyed) {
		/* Flush the queues */
		i40iw_flush_wqes(iwdev, iwqp);

		if (qp->term_flags && iwqp->ibqp.event_handler) {
			ibevent.device = iwqp->ibqp.device;
			ibevent.event = (qp->eventtype == TERM_EVENT_QP_FATAL) ?
					IB_EVENT_QP_FATAL : IB_EVENT_QP_ACCESS_ERR;
			ibevent.element.qp = &iwqp->ibqp;
			iwqp->ibqp.event_handler(&ibevent, iwqp->ibqp.qp_context);
		}
	}

	if (cm_id && cm_id->event_handler) {
		if (issue_disconn) {
			ret = i40iw_send_cm_event(NULL,
						  cm_id,
						  IW_CM_EVENT_DISCONNECT,
						  disconn_status);

			if (ret)
				i40iw_debug(&iwdev->sc_dev,
					    I40IW_DEBUG_CM,
					    "disconnect event failed %s: - cm_id = %p\n",
					    __func__, cm_id);
		}
		if (issue_close) {
			i40iw_qp_disconnect(iwqp);
			cm_id->provider_data = iwqp;
			ret = i40iw_send_cm_event(NULL, cm_id, IW_CM_EVENT_CLOSE, 0);
			if (ret)
				i40iw_debug(&iwdev->sc_dev,
					    I40IW_DEBUG_CM,
					    "close event failed %s: - cm_id = %p\n",
					    __func__, cm_id);
			cm_id->rem_ref(cm_id);
		}
	}
}

/**
 * i40iw_disconnect_worker - worker for connection close
 * @work: points or disconn structure
 */
static void i40iw_disconnect_worker(struct work_struct *work)
{
	struct disconn_work *dwork = container_of(work, struct disconn_work, work);
	struct i40iw_qp *iwqp = dwork->iwqp;

	kfree(dwork);
	i40iw_cm_disconn_true(iwqp);
	i40iw_rem_ref(&iwqp->ibqp);
}

/**
 * i40iw_accept - registered call for connection to be accepted
 * @cm_id: cm information for passive connection
 * @conn_param: accpet parameters
 */
int i40iw_accept(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	struct ib_qp *ibqp;
	struct i40iw_qp *iwqp;
	struct i40iw_device *iwdev;
	struct i40iw_sc_dev *dev;
	struct i40iw_cm_node *cm_node;
	struct ib_qp_attr attr;
	int passive_state;
	struct ib_mr *ibmr;
	struct i40iw_pd *iwpd;
	u16 buf_len = 0;
	struct i40iw_kmem_info accept;
	enum i40iw_status_code status;
	u64 tagged_offset;

	memset(&attr, 0, sizeof(attr));
	ibqp = i40iw_get_qp(cm_id->device, conn_param->qpn);
	if (!ibqp)
		return -EINVAL;

	iwqp = to_iwqp(ibqp);
	iwdev = iwqp->iwdev;
	dev = &iwdev->sc_dev;
	cm_node = (struct i40iw_cm_node *)cm_id->provider_data;

	if (((struct sockaddr_in *)&cm_id->local_addr)->sin_family == AF_INET) {
		cm_node->ipv4 = true;
		cm_node->vlan_id = i40iw_get_vlan_ipv4(cm_node->loc_addr);
	} else {
		cm_node->ipv4 = false;
		i40iw_netdev_vlan_ipv6(cm_node->loc_addr, &cm_node->vlan_id);
	}
	i40iw_debug(cm_node->dev,
		    I40IW_DEBUG_CM,
		    "Accept vlan_id=%d\n",
		    cm_node->vlan_id);
	if (cm_node->state == I40IW_CM_STATE_LISTENER_DESTROYED) {
		if (cm_node->loopbackpartner)
			i40iw_rem_ref_cm_node(cm_node->loopbackpartner);
		i40iw_rem_ref_cm_node(cm_node);
		return -EINVAL;
	}

	passive_state = atomic_add_return(1, &cm_node->passive_state);
	if (passive_state == I40IW_SEND_RESET_EVENT) {
		i40iw_rem_ref_cm_node(cm_node);
		return -ECONNRESET;
	}

	cm_node->cm_core->stats_accepts++;
	iwqp->cm_node = (void *)cm_node;
	cm_node->iwqp = iwqp;

	buf_len = conn_param->private_data_len + I40IW_MAX_IETF_SIZE;

	status = i40iw_allocate_dma_mem(dev->hw, &iwqp->ietf_mem, buf_len, 1);

	if (status)
		return -ENOMEM;
	cm_node->pdata.size = conn_param->private_data_len;
	accept.addr = iwqp->ietf_mem.va;
	accept.size = i40iw_cm_build_mpa_frame(cm_node, &accept, MPA_KEY_REPLY);
	memcpy(accept.addr + accept.size, conn_param->private_data,
	       conn_param->private_data_len);

	/* setup our first outgoing iWarp send WQE (the IETF frame response) */
	if ((cm_node->ipv4 &&
	     !i40iw_ipv4_is_loopback(cm_node->loc_addr[0], cm_node->rem_addr[0])) ||
	    (!cm_node->ipv4 &&
	     !i40iw_ipv6_is_loopback(cm_node->loc_addr, cm_node->rem_addr))) {
		iwpd = iwqp->iwpd;
		tagged_offset = (uintptr_t)iwqp->ietf_mem.va;
		ibmr = i40iw_reg_phys_mr(&iwpd->ibpd,
					 iwqp->ietf_mem.pa,
					 buf_len,
					 IB_ACCESS_LOCAL_WRITE,
					 &tagged_offset);
		if (IS_ERR(ibmr)) {
			i40iw_free_dma_mem(dev->hw, &iwqp->ietf_mem);
			return -ENOMEM;
		}

		ibmr->pd = &iwpd->ibpd;
		ibmr->device = iwpd->ibpd.device;
		iwqp->lsmm_mr = ibmr;
		if (iwqp->page)
			iwqp->sc_qp.qp_uk.sq_base = kmap(iwqp->page);
		dev->iw_priv_qp_ops->qp_send_lsmm(&iwqp->sc_qp,
							iwqp->ietf_mem.va,
							(accept.size + conn_param->private_data_len),
							ibmr->lkey);

	} else {
		if (iwqp->page)
			iwqp->sc_qp.qp_uk.sq_base = kmap(iwqp->page);
		dev->iw_priv_qp_ops->qp_send_lsmm(&iwqp->sc_qp, NULL, 0, 0);
	}

	if (iwqp->page)
		kunmap(iwqp->page);

	iwqp->cm_id = cm_id;
	cm_node->cm_id = cm_id;

	cm_id->provider_data = (void *)iwqp;
	iwqp->active_conn = 0;

	cm_node->lsmm_size = accept.size + conn_param->private_data_len;
	i40iw_cm_init_tsa_conn(iwqp, cm_node);
	cm_id->add_ref(cm_id);
	i40iw_add_ref(&iwqp->ibqp);

	i40iw_send_cm_event(cm_node, cm_id, IW_CM_EVENT_ESTABLISHED, 0);

	attr.qp_state = IB_QPS_RTS;
	cm_node->qhash_set = false;
	i40iw_modify_qp(&iwqp->ibqp, &attr, IB_QP_STATE, NULL);
	if (cm_node->loopbackpartner) {
		cm_node->loopbackpartner->pdata.size = conn_param->private_data_len;

		/* copy entire MPA frame to our cm_node's frame */
		memcpy(cm_node->loopbackpartner->pdata_buf,
		       conn_param->private_data,
		       conn_param->private_data_len);
		i40iw_create_event(cm_node->loopbackpartner, I40IW_CM_EVENT_CONNECTED);
	}

	cm_node->accelerated = 1;
	if (cm_node->accept_pend) {
		atomic_dec(&cm_node->listener->pend_accepts_cnt);
		cm_node->accept_pend = 0;
	}
	return 0;
}

/**
 * i40iw_reject - registered call for connection to be rejected
 * @cm_id: cm information for passive connection
 * @pdata: private data to be sent
 * @pdata_len: private data length
 */
int i40iw_reject(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len)
{
	struct i40iw_device *iwdev;
	struct i40iw_cm_node *cm_node;
	struct i40iw_cm_node *loopback;

	cm_node = (struct i40iw_cm_node *)cm_id->provider_data;
	loopback = cm_node->loopbackpartner;
	cm_node->cm_id = cm_id;
	cm_node->pdata.size = pdata_len;

	iwdev = to_iwdev(cm_id->device);
	if (!iwdev)
		return -EINVAL;
	cm_node->cm_core->stats_rejects++;

	if (pdata_len + sizeof(struct ietf_mpa_v2) > MAX_CM_BUFFER)
		return -EINVAL;

	if (loopback) {
		memcpy(&loopback->pdata_buf, pdata, pdata_len);
		loopback->pdata.size = pdata_len;
	}

	return i40iw_cm_reject(cm_node, pdata, pdata_len);
}

/**
 * i40iw_connect - registered call for connection to be established
 * @cm_id: cm information for passive connection
 * @conn_param: Information about the connection
 */
int i40iw_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	struct ib_qp *ibqp;
	struct i40iw_qp *iwqp;
	struct i40iw_device *iwdev;
	struct i40iw_cm_node *cm_node;
	struct i40iw_cm_info cm_info;
	struct sockaddr_in *laddr;
	struct sockaddr_in *raddr;
	struct sockaddr_in6 *laddr6;
	struct sockaddr_in6 *raddr6;
	int ret = 0;
	unsigned long flags;

	ibqp = i40iw_get_qp(cm_id->device, conn_param->qpn);
	if (!ibqp)
		return -EINVAL;
	iwqp = to_iwqp(ibqp);
	if (!iwqp)
		return -EINVAL;
	iwdev = to_iwdev(iwqp->ibqp.device);
	if (!iwdev)
		return -EINVAL;

	laddr = (struct sockaddr_in *)&cm_id->m_local_addr;
	raddr = (struct sockaddr_in *)&cm_id->m_remote_addr;
	laddr6 = (struct sockaddr_in6 *)&cm_id->m_local_addr;
	raddr6 = (struct sockaddr_in6 *)&cm_id->m_remote_addr;

	if (!(laddr->sin_port) || !(raddr->sin_port))
		return -EINVAL;

	iwqp->active_conn = 1;
	iwqp->cm_id = NULL;
	cm_id->provider_data = iwqp;

	/* set up the connection params for the node */
	if (cm_id->remote_addr.ss_family == AF_INET) {
		cm_info.ipv4 = true;
		memset(cm_info.loc_addr, 0, sizeof(cm_info.loc_addr));
		memset(cm_info.rem_addr, 0, sizeof(cm_info.rem_addr));
		cm_info.loc_addr[0] = ntohl(laddr->sin_addr.s_addr);
		cm_info.rem_addr[0] = ntohl(raddr->sin_addr.s_addr);
		cm_info.loc_port = ntohs(laddr->sin_port);
		cm_info.rem_port = ntohs(raddr->sin_port);
		cm_info.vlan_id = i40iw_get_vlan_ipv4(cm_info.loc_addr);
	} else {
		cm_info.ipv4 = false;
		i40iw_copy_ip_ntohl(cm_info.loc_addr,
				    laddr6->sin6_addr.in6_u.u6_addr32);
		i40iw_copy_ip_ntohl(cm_info.rem_addr,
				    raddr6->sin6_addr.in6_u.u6_addr32);
		cm_info.loc_port = ntohs(laddr6->sin6_port);
		cm_info.rem_port = ntohs(raddr6->sin6_port);
		i40iw_netdev_vlan_ipv6(cm_info.loc_addr, &cm_info.vlan_id);
	}
	cm_info.cm_id = cm_id;
	cm_info.tos = cm_id->tos;
	cm_info.user_pri = rt_tos2priority(cm_id->tos);
	i40iw_debug(&iwdev->sc_dev, I40IW_DEBUG_DCB, "%s TOS:[%d] UP:[%d]\n",
		    __func__, cm_id->tos, cm_info.user_pri);
	cm_id->add_ref(cm_id);
	cm_node = i40iw_create_cm_node(&iwdev->cm_core, iwdev,
				       conn_param->private_data_len,
				       (void *)conn_param->private_data,
				       &cm_info);

	if (IS_ERR(cm_node)) {
		ret = PTR_ERR(cm_node);
		cm_id->rem_ref(cm_id);
		return ret;
	}

	if ((cm_info.ipv4 && (laddr->sin_addr.s_addr != raddr->sin_addr.s_addr)) ||
	    (!cm_info.ipv4 && memcmp(laddr6->sin6_addr.in6_u.u6_addr32,
				     raddr6->sin6_addr.in6_u.u6_addr32,
				     sizeof(laddr6->sin6_addr.in6_u.u6_addr32)))) {
		if (i40iw_manage_qhash(iwdev, &cm_info, I40IW_QHASH_TYPE_TCP_ESTABLISHED,
				       I40IW_QHASH_MANAGE_TYPE_ADD, NULL, true)) {
			ret = -EINVAL;
			goto err;
		}
		cm_node->qhash_set = true;
	}

	spin_lock_irqsave(&iwdev->cm_core.ht_lock, flags);
	if (!test_and_set_bit(cm_info.loc_port, iwdev->cm_core.active_side_ports)) {
		spin_unlock_irqrestore(&iwdev->cm_core.ht_lock, flags);
		if (i40iw_manage_apbvt(iwdev, cm_info.loc_port, I40IW_MANAGE_APBVT_ADD)) {
			ret =  -EINVAL;
			goto err;
		}
	} else {
		spin_unlock_irqrestore(&iwdev->cm_core.ht_lock, flags);
	}

	cm_node->apbvt_set = true;
	i40iw_record_ird_ord(cm_node, (u16)conn_param->ird, (u16)conn_param->ord);
	if (cm_node->send_rdma0_op == SEND_RDMA_READ_ZERO &&
	    !cm_node->ord_size)
		cm_node->ord_size = 1;

	iwqp->cm_node = cm_node;
	cm_node->iwqp = iwqp;
	iwqp->cm_id = cm_id;
	i40iw_add_ref(&iwqp->ibqp);

	if (cm_node->state != I40IW_CM_STATE_OFFLOADED) {
		cm_node->state = I40IW_CM_STATE_SYN_SENT;
		ret = i40iw_send_syn(cm_node, 0);
		if (ret)
			goto err;
	}

	i40iw_debug(cm_node->dev,
		    I40IW_DEBUG_CM,
		    "Api - connect(): port=0x%04x, cm_node=%p, cm_id = %p.\n",
		    cm_node->rem_port,
		    cm_node,
		    cm_node->cm_id);

	return 0;

err:
	if (cm_info.ipv4)
		i40iw_debug(&iwdev->sc_dev,
			    I40IW_DEBUG_CM,
			    "Api - connect() FAILED: dest addr=%pI4",
			    cm_info.rem_addr);
	else
		i40iw_debug(&iwdev->sc_dev,
			    I40IW_DEBUG_CM,
			    "Api - connect() FAILED: dest addr=%pI6",
			    cm_info.rem_addr);

	i40iw_rem_ref_cm_node(cm_node);
	cm_id->rem_ref(cm_id);
	iwdev->cm_core.stats_connect_errs++;
	return ret;
}

/**
 * i40iw_create_listen - registered call creating listener
 * @cm_id: cm information for passive connection
 * @backlog: to max accept pending count
 */
int i40iw_create_listen(struct iw_cm_id *cm_id, int backlog)
{
	struct i40iw_device *iwdev;
	struct i40iw_cm_listener *cm_listen_node;
	struct i40iw_cm_info cm_info;
	enum i40iw_status_code ret;
	struct sockaddr_in *laddr;
	struct sockaddr_in6 *laddr6;
	bool wildcard = false;

	iwdev = to_iwdev(cm_id->device);
	if (!iwdev)
		return -EINVAL;

	laddr = (struct sockaddr_in *)&cm_id->m_local_addr;
	laddr6 = (struct sockaddr_in6 *)&cm_id->m_local_addr;
	memset(&cm_info, 0, sizeof(cm_info));
	if (laddr->sin_family == AF_INET) {
		cm_info.ipv4 = true;
		cm_info.loc_addr[0] = ntohl(laddr->sin_addr.s_addr);
		cm_info.loc_port = ntohs(laddr->sin_port);

		if (laddr->sin_addr.s_addr != INADDR_ANY)
			cm_info.vlan_id = i40iw_get_vlan_ipv4(cm_info.loc_addr);
		else
			wildcard = true;

	} else {
		cm_info.ipv4 = false;
		i40iw_copy_ip_ntohl(cm_info.loc_addr,
				    laddr6->sin6_addr.in6_u.u6_addr32);
		cm_info.loc_port = ntohs(laddr6->sin6_port);
		if (ipv6_addr_type(&laddr6->sin6_addr) != IPV6_ADDR_ANY)
			i40iw_netdev_vlan_ipv6(cm_info.loc_addr,
					       &cm_info.vlan_id);
		else
			wildcard = true;
	}
	cm_info.backlog = backlog;
	cm_info.cm_id = cm_id;

	cm_listen_node = i40iw_make_listen_node(&iwdev->cm_core, iwdev, &cm_info);
	if (!cm_listen_node) {
		i40iw_pr_err("cm_listen_node == NULL\n");
		return -ENOMEM;
	}

	cm_id->provider_data = cm_listen_node;

	cm_listen_node->tos = cm_id->tos;
	cm_listen_node->user_pri = rt_tos2priority(cm_id->tos);
	cm_info.user_pri = cm_listen_node->user_pri;

	if (!cm_listen_node->reused_node) {
		if (wildcard) {
			if (cm_info.ipv4)
				ret = i40iw_add_mqh_4(iwdev,
						      &cm_info,
						      cm_listen_node);
			else
				ret = i40iw_add_mqh_6(iwdev,
						      &cm_info,
						      cm_listen_node);
			if (ret)
				goto error;

			ret = i40iw_manage_apbvt(iwdev,
						 cm_info.loc_port,
						 I40IW_MANAGE_APBVT_ADD);

			if (ret)
				goto error;
		} else {
			ret = i40iw_manage_qhash(iwdev,
						 &cm_info,
						 I40IW_QHASH_TYPE_TCP_SYN,
						 I40IW_QHASH_MANAGE_TYPE_ADD,
						 NULL,
						 true);
			if (ret)
				goto error;
			cm_listen_node->qhash_set = true;
			ret = i40iw_manage_apbvt(iwdev,
						 cm_info.loc_port,
						 I40IW_MANAGE_APBVT_ADD);
			if (ret)
				goto error;
		}
	}
	cm_id->add_ref(cm_id);
	cm_listen_node->cm_core->stats_listen_created++;
	return 0;
 error:
	i40iw_cm_del_listen(&iwdev->cm_core, (void *)cm_listen_node, false);
	return -EINVAL;
}

/**
 * i40iw_destroy_listen - registered call to destroy listener
 * @cm_id: cm information for passive connection
 */
int i40iw_destroy_listen(struct iw_cm_id *cm_id)
{
	struct i40iw_device *iwdev;

	iwdev = to_iwdev(cm_id->device);
	if (cm_id->provider_data)
		i40iw_cm_del_listen(&iwdev->cm_core, cm_id->provider_data, true);
	else
		i40iw_pr_err("cm_id->provider_data was NULL\n");

	cm_id->rem_ref(cm_id);

	return 0;
}

/**
 * i40iw_cm_event_connected - handle connected active node
 * @event: the info for cm_node of connection
 */
static void i40iw_cm_event_connected(struct i40iw_cm_event *event)
{
	struct i40iw_qp *iwqp;
	struct i40iw_device *iwdev;
	struct i40iw_cm_node *cm_node;
	struct i40iw_sc_dev *dev;
	struct ib_qp_attr attr;
	struct iw_cm_id *cm_id;
	int status;
	bool read0;

	cm_node = event->cm_node;
	cm_id = cm_node->cm_id;
	iwqp = (struct i40iw_qp *)cm_id->provider_data;
	iwdev = to_iwdev(iwqp->ibqp.device);
	dev = &iwdev->sc_dev;

	if (iwqp->destroyed) {
		status = -ETIMEDOUT;
		goto error;
	}
	i40iw_cm_init_tsa_conn(iwqp, cm_node);
	read0 = (cm_node->send_rdma0_op == SEND_RDMA_READ_ZERO);
	if (iwqp->page)
		iwqp->sc_qp.qp_uk.sq_base = kmap(iwqp->page);
	dev->iw_priv_qp_ops->qp_send_rtt(&iwqp->sc_qp, read0);
	if (iwqp->page)
		kunmap(iwqp->page);
	status = i40iw_send_cm_event(cm_node, cm_id, IW_CM_EVENT_CONNECT_REPLY, 0);
	if (status)
		i40iw_pr_err("send cm event\n");

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IB_QPS_RTS;
	cm_node->qhash_set = false;
	i40iw_modify_qp(&iwqp->ibqp, &attr, IB_QP_STATE, NULL);

	cm_node->accelerated = 1;

	return;

error:
	iwqp->cm_id = NULL;
	cm_id->provider_data = NULL;
	i40iw_send_cm_event(event->cm_node,
			    cm_id,
			    IW_CM_EVENT_CONNECT_REPLY,
			    status);
	cm_id->rem_ref(cm_id);
	i40iw_rem_ref_cm_node(event->cm_node);
}

/**
 * i40iw_cm_event_reset - handle reset
 * @event: the info for cm_node of connection
 */
static void i40iw_cm_event_reset(struct i40iw_cm_event *event)
{
	struct i40iw_cm_node *cm_node = event->cm_node;
	struct iw_cm_id   *cm_id = cm_node->cm_id;
	struct i40iw_qp *iwqp;

	if (!cm_id)
		return;

	iwqp = cm_id->provider_data;
	if (!iwqp)
		return;

	i40iw_debug(cm_node->dev,
		    I40IW_DEBUG_CM,
		    "reset event %p - cm_id = %p\n",
		     event->cm_node, cm_id);
	iwqp->cm_id = NULL;

	i40iw_send_cm_event(cm_node, cm_node->cm_id, IW_CM_EVENT_DISCONNECT, -ECONNRESET);
	i40iw_send_cm_event(cm_node, cm_node->cm_id, IW_CM_EVENT_CLOSE, 0);
}

/**
 * i40iw_cm_event_handler - worker thread callback to send event to cm upper layer
 * @work: pointer of cm event info.
 */
static void i40iw_cm_event_handler(struct work_struct *work)
{
	struct i40iw_cm_event *event = container_of(work,
						    struct i40iw_cm_event,
						    event_work);
	struct i40iw_cm_node *cm_node;

	if (!event || !event->cm_node || !event->cm_node->cm_core)
		return;

	cm_node = event->cm_node;

	switch (event->type) {
	case I40IW_CM_EVENT_MPA_REQ:
		i40iw_send_cm_event(cm_node,
				    cm_node->cm_id,
				    IW_CM_EVENT_CONNECT_REQUEST,
				    0);
		break;
	case I40IW_CM_EVENT_RESET:
		i40iw_cm_event_reset(event);
		break;
	case I40IW_CM_EVENT_CONNECTED:
		if (!event->cm_node->cm_id ||
		    (event->cm_node->state != I40IW_CM_STATE_OFFLOADED))
			break;
		i40iw_cm_event_connected(event);
		break;
	case I40IW_CM_EVENT_MPA_REJECT:
		if (!event->cm_node->cm_id ||
		    (cm_node->state == I40IW_CM_STATE_OFFLOADED))
			break;
		i40iw_send_cm_event(cm_node,
				    cm_node->cm_id,
				    IW_CM_EVENT_CONNECT_REPLY,
				    -ECONNREFUSED);
		break;
	case I40IW_CM_EVENT_ABORTED:
		if (!event->cm_node->cm_id ||
		    (event->cm_node->state == I40IW_CM_STATE_OFFLOADED))
			break;
		i40iw_event_connect_error(event);
		break;
	default:
		i40iw_pr_err("event type = %d\n", event->type);
		break;
	}

	event->cm_info.cm_id->rem_ref(event->cm_info.cm_id);
	i40iw_rem_ref_cm_node(event->cm_node);
	kfree(event);
}

/**
 * i40iw_cm_post_event - queue event request for worker thread
 * @event: cm node's info for up event call
 */
static void i40iw_cm_post_event(struct i40iw_cm_event *event)
{
	atomic_inc(&event->cm_node->ref_count);
	event->cm_info.cm_id->add_ref(event->cm_info.cm_id);
	INIT_WORK(&event->event_work, i40iw_cm_event_handler);

	queue_work(event->cm_node->cm_core->event_wq, &event->event_work);
}

/**
 * i40iw_qhash_ctrl - enable/disable qhash for list
 * @iwdev: device pointer
 * @parent_listen_node: parent listen node
 * @nfo: cm info node
 * @ipaddr: Pointer to IPv4 or IPv6 address
 * @ipv4: flag indicating IPv4 when true
 * @ifup: flag indicating interface up when true
 *
 * Enables or disables the qhash for the node in the child
 * listen list that matches ipaddr. If no matching IP was found
 * it will allocate and add a new child listen node to the
 * parent listen node. The listen_list_lock is assumed to be
 * held when called.
 */
static void i40iw_qhash_ctrl(struct i40iw_device *iwdev,
			     struct i40iw_cm_listener *parent_listen_node,
			     struct i40iw_cm_info *nfo,
			     u32 *ipaddr, bool ipv4, bool ifup)
{
	struct list_head *child_listen_list = &parent_listen_node->child_listen_list;
	struct i40iw_cm_listener *child_listen_node;
	struct list_head *pos, *tpos;
	enum i40iw_status_code ret;
	bool node_allocated = false;
	enum i40iw_quad_hash_manage_type op =
		ifup ? I40IW_QHASH_MANAGE_TYPE_ADD : I40IW_QHASH_MANAGE_TYPE_DELETE;

	list_for_each_safe(pos, tpos, child_listen_list) {
		child_listen_node =
			list_entry(pos,
				   struct i40iw_cm_listener,
				   child_listen_list);
		if (!memcmp(child_listen_node->loc_addr, ipaddr, ipv4 ? 4 : 16))
			goto set_qhash;
	}

	/* if not found then add a child listener if interface is going up */
	if (!ifup)
		return;
	child_listen_node = kzalloc(sizeof(*child_listen_node), GFP_ATOMIC);
	if (!child_listen_node)
		return;
	node_allocated = true;
	memcpy(child_listen_node, parent_listen_node, sizeof(*child_listen_node));

	memcpy(child_listen_node->loc_addr, ipaddr,  ipv4 ? 4 : 16);

set_qhash:
	memcpy(nfo->loc_addr,
	       child_listen_node->loc_addr,
	       sizeof(nfo->loc_addr));
	nfo->vlan_id = child_listen_node->vlan_id;
	ret = i40iw_manage_qhash(iwdev, nfo,
				 I40IW_QHASH_TYPE_TCP_SYN,
				 op,
				 NULL, false);
	if (!ret) {
		child_listen_node->qhash_set = ifup;
		if (node_allocated)
			list_add(&child_listen_node->child_listen_list,
				 &parent_listen_node->child_listen_list);
	} else if (node_allocated) {
		kfree(child_listen_node);
	}
}

/**
 * i40iw_cm_disconnect_all - disconnect all connected qp's
 * @iwdev: device pointer
 */
void i40iw_cm_disconnect_all(struct i40iw_device *iwdev)
{
	struct i40iw_cm_core *cm_core = &iwdev->cm_core;
	struct list_head *list_core_temp;
	struct list_head *list_node;
	struct i40iw_cm_node *cm_node;
	unsigned long flags;
	struct list_head connected_list;
	struct ib_qp_attr attr;

	INIT_LIST_HEAD(&connected_list);
	spin_lock_irqsave(&cm_core->ht_lock, flags);
	list_for_each_safe(list_node, list_core_temp, &cm_core->connected_nodes) {
		cm_node = container_of(list_node, struct i40iw_cm_node, list);
		atomic_inc(&cm_node->ref_count);
		list_add(&cm_node->connected_entry, &connected_list);
	}
	spin_unlock_irqrestore(&cm_core->ht_lock, flags);

	list_for_each_safe(list_node, list_core_temp, &connected_list) {
		cm_node = container_of(list_node, struct i40iw_cm_node, connected_entry);
		attr.qp_state = IB_QPS_ERR;
		i40iw_modify_qp(&cm_node->iwqp->ibqp, &attr, IB_QP_STATE, NULL);
		if (iwdev->reset)
			i40iw_cm_disconn(cm_node->iwqp);
		i40iw_rem_ref_cm_node(cm_node);
	}
}

/**
 * i40iw_ifdown_notify - process an ifdown on an interface
 * @iwdev: device pointer
 * @ipaddr: Pointer to IPv4 or IPv6 address
 * @ipv4: flag indicating IPv4 when true
 * @ifup: flag indicating interface up when true
 */
void i40iw_if_notify(struct i40iw_device *iwdev, struct net_device *netdev,
		     u32 *ipaddr, bool ipv4, bool ifup)
{
	struct i40iw_cm_core *cm_core = &iwdev->cm_core;
	unsigned long flags;
	struct i40iw_cm_listener *listen_node;
	static const u32 ip_zero[4] = { 0, 0, 0, 0 };
	struct i40iw_cm_info nfo;
	u16 vlan_id = rdma_vlan_dev_vlan_id(netdev);
	enum i40iw_status_code ret;
	enum i40iw_quad_hash_manage_type op =
		ifup ? I40IW_QHASH_MANAGE_TYPE_ADD : I40IW_QHASH_MANAGE_TYPE_DELETE;

	/* Disable or enable qhash for listeners */
	spin_lock_irqsave(&cm_core->listen_list_lock, flags);
	list_for_each_entry(listen_node, &cm_core->listen_nodes, list) {
		if (vlan_id == listen_node->vlan_id &&
		    (!memcmp(listen_node->loc_addr, ipaddr, ipv4 ? 4 : 16) ||
		    !memcmp(listen_node->loc_addr, ip_zero, ipv4 ? 4 : 16))) {
			memcpy(nfo.loc_addr, listen_node->loc_addr,
			       sizeof(nfo.loc_addr));
			nfo.loc_port = listen_node->loc_port;
			nfo.ipv4 = listen_node->ipv4;
			nfo.vlan_id = listen_node->vlan_id;
			nfo.user_pri = listen_node->user_pri;
			if (!list_empty(&listen_node->child_listen_list)) {
				i40iw_qhash_ctrl(iwdev,
						 listen_node,
						 &nfo,
						 ipaddr, ipv4, ifup);
			} else if (memcmp(listen_node->loc_addr, ip_zero,
					  ipv4 ? 4 : 16)) {
				ret = i40iw_manage_qhash(iwdev,
							 &nfo,
							 I40IW_QHASH_TYPE_TCP_SYN,
							 op,
							 NULL,
							 false);
				if (!ret)
					listen_node->qhash_set = ifup;
			}
		}
	}
	spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);

	/* disconnect any connected qp's on ifdown */
	if (!ifup)
		i40iw_cm_disconnect_all(iwdev);
}
