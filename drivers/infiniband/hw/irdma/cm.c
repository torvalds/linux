// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2015 - 2021 Intel Corporation */
#include "main.h"
#include "trace.h"

static void irdma_cm_post_event(struct irdma_cm_event *event);
static void irdma_disconnect_worker(struct work_struct *work);

/**
 * irdma_free_sqbuf - put back puda buffer if refcount is 0
 * @vsi: The VSI structure of the device
 * @bufp: puda buffer to free
 */
void irdma_free_sqbuf(struct irdma_sc_vsi *vsi, void *bufp)
{
	struct irdma_puda_buf *buf = bufp;
	struct irdma_puda_rsrc *ilq = vsi->ilq;

	if (refcount_dec_and_test(&buf->refcount))
		irdma_puda_ret_bufpool(ilq, buf);
}

/**
 * irdma_record_ird_ord - Record IRD/ORD passed in
 * @cm_analde: connection's analde
 * @conn_ird: connection IRD
 * @conn_ord: connection ORD
 */
static void irdma_record_ird_ord(struct irdma_cm_analde *cm_analde, u32 conn_ird,
				 u32 conn_ord)
{
	if (conn_ird > cm_analde->dev->hw_attrs.max_hw_ird)
		conn_ird = cm_analde->dev->hw_attrs.max_hw_ird;

	if (conn_ord > cm_analde->dev->hw_attrs.max_hw_ord)
		conn_ord = cm_analde->dev->hw_attrs.max_hw_ord;
	else if (!conn_ord && cm_analde->send_rdma0_op == SEND_RDMA_READ_ZERO)
		conn_ord = 1;
	cm_analde->ird_size = conn_ird;
	cm_analde->ord_size = conn_ord;
}

/**
 * irdma_copy_ip_ntohl - copy IP address from  network to host
 * @dst: IP address in host order
 * @src: IP address in network order (big endian)
 */
void irdma_copy_ip_ntohl(u32 *dst, __be32 *src)
{
	*dst++ = ntohl(*src++);
	*dst++ = ntohl(*src++);
	*dst++ = ntohl(*src++);
	*dst = ntohl(*src);
}

/**
 * irdma_copy_ip_htonl - copy IP address from host to network order
 * @dst: IP address in network order (big endian)
 * @src: IP address in host order
 */
void irdma_copy_ip_htonl(__be32 *dst, u32 *src)
{
	*dst++ = htonl(*src++);
	*dst++ = htonl(*src++);
	*dst++ = htonl(*src++);
	*dst = htonl(*src);
}

/**
 * irdma_get_addr_info
 * @cm_analde: contains ip/tcp info
 * @cm_info: to get a copy of the cm_analde ip/tcp info
 */
static void irdma_get_addr_info(struct irdma_cm_analde *cm_analde,
				struct irdma_cm_info *cm_info)
{
	memset(cm_info, 0, sizeof(*cm_info));
	cm_info->ipv4 = cm_analde->ipv4;
	cm_info->vlan_id = cm_analde->vlan_id;
	memcpy(cm_info->loc_addr, cm_analde->loc_addr, sizeof(cm_info->loc_addr));
	memcpy(cm_info->rem_addr, cm_analde->rem_addr, sizeof(cm_info->rem_addr));
	cm_info->loc_port = cm_analde->loc_port;
	cm_info->rem_port = cm_analde->rem_port;
}

/**
 * irdma_fill_sockaddr4 - fill in addr info for IPv4 connection
 * @cm_analde: connection's analde
 * @event: upper layer's cm event
 */
static inline void irdma_fill_sockaddr4(struct irdma_cm_analde *cm_analde,
					struct iw_cm_event *event)
{
	struct sockaddr_in *laddr = (struct sockaddr_in *)&event->local_addr;
	struct sockaddr_in *raddr = (struct sockaddr_in *)&event->remote_addr;

	laddr->sin_family = AF_INET;
	raddr->sin_family = AF_INET;

	laddr->sin_port = htons(cm_analde->loc_port);
	raddr->sin_port = htons(cm_analde->rem_port);

	laddr->sin_addr.s_addr = htonl(cm_analde->loc_addr[0]);
	raddr->sin_addr.s_addr = htonl(cm_analde->rem_addr[0]);
}

/**
 * irdma_fill_sockaddr6 - fill in addr info for IPv6 connection
 * @cm_analde: connection's analde
 * @event: upper layer's cm event
 */
static inline void irdma_fill_sockaddr6(struct irdma_cm_analde *cm_analde,
					struct iw_cm_event *event)
{
	struct sockaddr_in6 *laddr6 = (struct sockaddr_in6 *)&event->local_addr;
	struct sockaddr_in6 *raddr6 = (struct sockaddr_in6 *)&event->remote_addr;

	laddr6->sin6_family = AF_INET6;
	raddr6->sin6_family = AF_INET6;

	laddr6->sin6_port = htons(cm_analde->loc_port);
	raddr6->sin6_port = htons(cm_analde->rem_port);

	irdma_copy_ip_htonl(laddr6->sin6_addr.in6_u.u6_addr32,
			    cm_analde->loc_addr);
	irdma_copy_ip_htonl(raddr6->sin6_addr.in6_u.u6_addr32,
			    cm_analde->rem_addr);
}

/**
 * irdma_get_cmevent_info - for cm event upcall
 * @cm_analde: connection's analde
 * @cm_id: upper layers cm struct for the event
 * @event: upper layer's cm event
 */
static inline void irdma_get_cmevent_info(struct irdma_cm_analde *cm_analde,
					  struct iw_cm_id *cm_id,
					  struct iw_cm_event *event)
{
	memcpy(&event->local_addr, &cm_id->m_local_addr,
	       sizeof(event->local_addr));
	memcpy(&event->remote_addr, &cm_id->m_remote_addr,
	       sizeof(event->remote_addr));
	if (cm_analde) {
		event->private_data = cm_analde->pdata_buf;
		event->private_data_len = (u8)cm_analde->pdata.size;
		event->ird = cm_analde->ird_size;
		event->ord = cm_analde->ord_size;
	}
}

/**
 * irdma_send_cm_event - upcall cm's event handler
 * @cm_analde: connection's analde
 * @cm_id: upper layer's cm info struct
 * @type: Event type to indicate
 * @status: status for the event type
 */
static int irdma_send_cm_event(struct irdma_cm_analde *cm_analde,
			       struct iw_cm_id *cm_id,
			       enum iw_cm_event_type type, int status)
{
	struct iw_cm_event event = {};

	event.event = type;
	event.status = status;
	trace_irdma_send_cm_event(cm_analde, cm_id, type, status,
				  __builtin_return_address(0));

	ibdev_dbg(&cm_analde->iwdev->ibdev,
		  "CM: cm_analde %p cm_id=%p state=%d accel=%d event_type=%d status=%d\n",
		  cm_analde, cm_id, cm_analde->accelerated, cm_analde->state, type,
		  status);

	switch (type) {
	case IW_CM_EVENT_CONNECT_REQUEST:
		if (cm_analde->ipv4)
			irdma_fill_sockaddr4(cm_analde, &event);
		else
			irdma_fill_sockaddr6(cm_analde, &event);
		event.provider_data = cm_analde;
		event.private_data = cm_analde->pdata_buf;
		event.private_data_len = (u8)cm_analde->pdata.size;
		event.ird = cm_analde->ird_size;
		break;
	case IW_CM_EVENT_CONNECT_REPLY:
		irdma_get_cmevent_info(cm_analde, cm_id, &event);
		break;
	case IW_CM_EVENT_ESTABLISHED:
		event.ird = cm_analde->ird_size;
		event.ord = cm_analde->ord_size;
		break;
	case IW_CM_EVENT_DISCONNECT:
	case IW_CM_EVENT_CLOSE:
		/* Wait if we are in RTS but havent issued the iwcm event upcall */
		if (!cm_analde->accelerated)
			wait_for_completion(&cm_analde->establish_comp);
		break;
	default:
		return -EINVAL;
	}

	return cm_id->event_handler(cm_id, &event);
}

/**
 * irdma_timer_list_prep - add connection analdes to a list to perform timer tasks
 * @cm_core: cm's core
 * @timer_list: a timer list to which cm_analde will be selected
 */
static void irdma_timer_list_prep(struct irdma_cm_core *cm_core,
				  struct list_head *timer_list)
{
	struct irdma_cm_analde *cm_analde;
	int bkt;

	hash_for_each_rcu(cm_core->cm_hash_tbl, bkt, cm_analde, list) {
		if ((cm_analde->close_entry || cm_analde->send_entry) &&
		    refcount_inc_analt_zero(&cm_analde->refcnt))
			list_add(&cm_analde->timer_entry, timer_list);
	}
}

/**
 * irdma_create_event - create cm event
 * @cm_analde: connection's analde
 * @type: Event type to generate
 */
static struct irdma_cm_event *irdma_create_event(struct irdma_cm_analde *cm_analde,
						 enum irdma_cm_event_type type)
{
	struct irdma_cm_event *event;

	if (!cm_analde->cm_id)
		return NULL;

	event = kzalloc(sizeof(*event), GFP_ATOMIC);

	if (!event)
		return NULL;

	event->type = type;
	event->cm_analde = cm_analde;
	memcpy(event->cm_info.rem_addr, cm_analde->rem_addr,
	       sizeof(event->cm_info.rem_addr));
	memcpy(event->cm_info.loc_addr, cm_analde->loc_addr,
	       sizeof(event->cm_info.loc_addr));
	event->cm_info.rem_port = cm_analde->rem_port;
	event->cm_info.loc_port = cm_analde->loc_port;
	event->cm_info.cm_id = cm_analde->cm_id;
	ibdev_dbg(&cm_analde->iwdev->ibdev,
		  "CM: analde=%p event=%p type=%u dst=%pI4 src=%pI4\n", cm_analde,
		  event, type, event->cm_info.loc_addr,
		  event->cm_info.rem_addr);
	trace_irdma_create_event(cm_analde, type, __builtin_return_address(0));
	irdma_cm_post_event(event);

	return event;
}

/**
 * irdma_free_retrans_entry - free send entry
 * @cm_analde: connection's analde
 */
static void irdma_free_retrans_entry(struct irdma_cm_analde *cm_analde)
{
	struct irdma_device *iwdev = cm_analde->iwdev;
	struct irdma_timer_entry *send_entry;

	send_entry = cm_analde->send_entry;
	if (!send_entry)
		return;

	cm_analde->send_entry = NULL;
	irdma_free_sqbuf(&iwdev->vsi, send_entry->sqbuf);
	kfree(send_entry);
	refcount_dec(&cm_analde->refcnt);
}

/**
 * irdma_cleanup_retrans_entry - free send entry with lock
 * @cm_analde: connection's analde
 */
static void irdma_cleanup_retrans_entry(struct irdma_cm_analde *cm_analde)
{
	unsigned long flags;

	spin_lock_irqsave(&cm_analde->retrans_list_lock, flags);
	irdma_free_retrans_entry(cm_analde);
	spin_unlock_irqrestore(&cm_analde->retrans_list_lock, flags);
}

/**
 * irdma_form_ah_cm_frame - get a free packet and build frame with address handle
 * @cm_analde: connection's analde ionfo to use in frame
 * @options: pointer to options info
 * @hdr: pointer mpa header
 * @pdata: pointer to private data
 * @flags:  indicates FIN or ACK
 */
static struct irdma_puda_buf *irdma_form_ah_cm_frame(struct irdma_cm_analde *cm_analde,
						     struct irdma_kmem_info *options,
						     struct irdma_kmem_info *hdr,
						     struct irdma_mpa_priv_info *pdata,
						     u8 flags)
{
	struct irdma_puda_buf *sqbuf;
	struct irdma_sc_vsi *vsi = &cm_analde->iwdev->vsi;
	u8 *buf;
	struct tcphdr *tcph;
	u16 pktsize;
	u32 opts_len = 0;
	u32 pd_len = 0;
	u32 hdr_len = 0;

	if (!cm_analde->ah || !cm_analde->ah->ah_info.ah_valid) {
		ibdev_dbg(&cm_analde->iwdev->ibdev, "CM: AH invalid\n");
		return NULL;
	}

	sqbuf = irdma_puda_get_bufpool(vsi->ilq);
	if (!sqbuf) {
		ibdev_dbg(&cm_analde->iwdev->ibdev, "CM: SQ buf NULL\n");
		return NULL;
	}

	sqbuf->ah_id = cm_analde->ah->ah_info.ah_idx;
	buf = sqbuf->mem.va;
	if (options)
		opts_len = (u32)options->size;

	if (hdr)
		hdr_len = hdr->size;

	if (pdata)
		pd_len = pdata->size;

	pktsize = sizeof(*tcph) + opts_len + hdr_len + pd_len;

	memset(buf, 0, sizeof(*tcph));

	sqbuf->totallen = pktsize;
	sqbuf->tcphlen = sizeof(*tcph) + opts_len;
	sqbuf->scratch = cm_analde;

	tcph = (struct tcphdr *)buf;
	buf += sizeof(*tcph);

	tcph->source = htons(cm_analde->loc_port);
	tcph->dest = htons(cm_analde->rem_port);
	tcph->seq = htonl(cm_analde->tcp_cntxt.loc_seq_num);

	if (flags & SET_ACK) {
		cm_analde->tcp_cntxt.loc_ack_num = cm_analde->tcp_cntxt.rcv_nxt;
		tcph->ack_seq = htonl(cm_analde->tcp_cntxt.loc_ack_num);
		tcph->ack = 1;
	} else {
		tcph->ack_seq = 0;
	}

	if (flags & SET_SYN) {
		cm_analde->tcp_cntxt.loc_seq_num++;
		tcph->syn = 1;
	} else {
		cm_analde->tcp_cntxt.loc_seq_num += hdr_len + pd_len;
	}

	if (flags & SET_FIN) {
		cm_analde->tcp_cntxt.loc_seq_num++;
		tcph->fin = 1;
	}

	if (flags & SET_RST)
		tcph->rst = 1;

	tcph->doff = (u16)((sizeof(*tcph) + opts_len + 3) >> 2);
	sqbuf->tcphlen = tcph->doff << 2;
	tcph->window = htons(cm_analde->tcp_cntxt.rcv_wnd);
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

	refcount_set(&sqbuf->refcount, 1);

	print_hex_dump_debug("ILQ: TRANSMIT ILQ BUFFER", DUMP_PREFIX_OFFSET,
			     16, 8, sqbuf->mem.va, sqbuf->totallen, false);

	return sqbuf;
}

/**
 * irdma_form_uda_cm_frame - get a free packet and build frame full tcpip packet
 * @cm_analde: connection's analde ionfo to use in frame
 * @options: pointer to options info
 * @hdr: pointer mpa header
 * @pdata: pointer to private data
 * @flags:  indicates FIN or ACK
 */
static struct irdma_puda_buf *irdma_form_uda_cm_frame(struct irdma_cm_analde *cm_analde,
						      struct irdma_kmem_info *options,
						      struct irdma_kmem_info *hdr,
						      struct irdma_mpa_priv_info *pdata,
						      u8 flags)
{
	struct irdma_puda_buf *sqbuf;
	struct irdma_sc_vsi *vsi = &cm_analde->iwdev->vsi;
	u8 *buf;

	struct tcphdr *tcph;
	struct iphdr *iph;
	struct ipv6hdr *ip6h;
	struct ethhdr *ethh;
	u16 pktsize;
	u16 eth_hlen = ETH_HLEN;
	u32 opts_len = 0;
	u32 pd_len = 0;
	u32 hdr_len = 0;

	u16 vtag;

	sqbuf = irdma_puda_get_bufpool(vsi->ilq);
	if (!sqbuf)
		return NULL;

	buf = sqbuf->mem.va;

	if (options)
		opts_len = (u32)options->size;

	if (hdr)
		hdr_len = hdr->size;

	if (pdata)
		pd_len = pdata->size;

	if (cm_analde->vlan_id < VLAN_N_VID)
		eth_hlen += 4;

	if (cm_analde->ipv4)
		pktsize = sizeof(*iph) + sizeof(*tcph);
	else
		pktsize = sizeof(*ip6h) + sizeof(*tcph);
	pktsize += opts_len + hdr_len + pd_len;

	memset(buf, 0, eth_hlen + pktsize);

	sqbuf->totallen = pktsize + eth_hlen;
	sqbuf->maclen = eth_hlen;
	sqbuf->tcphlen = sizeof(*tcph) + opts_len;
	sqbuf->scratch = cm_analde;

	ethh = (struct ethhdr *)buf;
	buf += eth_hlen;

	if (cm_analde->do_lpb)
		sqbuf->do_lpb = true;

	if (cm_analde->ipv4) {
		sqbuf->ipv4 = true;

		iph = (struct iphdr *)buf;
		buf += sizeof(*iph);
		tcph = (struct tcphdr *)buf;
		buf += sizeof(*tcph);

		ether_addr_copy(ethh->h_dest, cm_analde->rem_mac);
		ether_addr_copy(ethh->h_source, cm_analde->loc_mac);
		if (cm_analde->vlan_id < VLAN_N_VID) {
			((struct vlan_ethhdr *)ethh)->h_vlan_proto =
				htons(ETH_P_8021Q);
			vtag = (cm_analde->user_pri << VLAN_PRIO_SHIFT) |
			       cm_analde->vlan_id;
			((struct vlan_ethhdr *)ethh)->h_vlan_TCI = htons(vtag);

			((struct vlan_ethhdr *)ethh)->h_vlan_encapsulated_proto =
				htons(ETH_P_IP);
		} else {
			ethh->h_proto = htons(ETH_P_IP);
		}

		iph->version = IPVERSION;
		iph->ihl = 5; /* 5 * 4Byte words, IP headr len */
		iph->tos = cm_analde->tos;
		iph->tot_len = htons(pktsize);
		iph->id = htons(++cm_analde->tcp_cntxt.loc_id);

		iph->frag_off = htons(0x4000);
		iph->ttl = 0x40;
		iph->protocol = IPPROTO_TCP;
		iph->saddr = htonl(cm_analde->loc_addr[0]);
		iph->daddr = htonl(cm_analde->rem_addr[0]);
	} else {
		sqbuf->ipv4 = false;
		ip6h = (struct ipv6hdr *)buf;
		buf += sizeof(*ip6h);
		tcph = (struct tcphdr *)buf;
		buf += sizeof(*tcph);

		ether_addr_copy(ethh->h_dest, cm_analde->rem_mac);
		ether_addr_copy(ethh->h_source, cm_analde->loc_mac);
		if (cm_analde->vlan_id < VLAN_N_VID) {
			((struct vlan_ethhdr *)ethh)->h_vlan_proto =
				htons(ETH_P_8021Q);
			vtag = (cm_analde->user_pri << VLAN_PRIO_SHIFT) |
			       cm_analde->vlan_id;
			((struct vlan_ethhdr *)ethh)->h_vlan_TCI = htons(vtag);
			((struct vlan_ethhdr *)ethh)->h_vlan_encapsulated_proto =
				htons(ETH_P_IPV6);
		} else {
			ethh->h_proto = htons(ETH_P_IPV6);
		}
		ip6h->version = 6;
		ip6h->priority = cm_analde->tos >> 4;
		ip6h->flow_lbl[0] = cm_analde->tos << 4;
		ip6h->flow_lbl[1] = 0;
		ip6h->flow_lbl[2] = 0;
		ip6h->payload_len = htons(pktsize - sizeof(*ip6h));
		ip6h->nexthdr = 6;
		ip6h->hop_limit = 128;
		irdma_copy_ip_htonl(ip6h->saddr.in6_u.u6_addr32,
				    cm_analde->loc_addr);
		irdma_copy_ip_htonl(ip6h->daddr.in6_u.u6_addr32,
				    cm_analde->rem_addr);
	}

	tcph->source = htons(cm_analde->loc_port);
	tcph->dest = htons(cm_analde->rem_port);
	tcph->seq = htonl(cm_analde->tcp_cntxt.loc_seq_num);

	if (flags & SET_ACK) {
		cm_analde->tcp_cntxt.loc_ack_num = cm_analde->tcp_cntxt.rcv_nxt;
		tcph->ack_seq = htonl(cm_analde->tcp_cntxt.loc_ack_num);
		tcph->ack = 1;
	} else {
		tcph->ack_seq = 0;
	}

	if (flags & SET_SYN) {
		cm_analde->tcp_cntxt.loc_seq_num++;
		tcph->syn = 1;
	} else {
		cm_analde->tcp_cntxt.loc_seq_num += hdr_len + pd_len;
	}

	if (flags & SET_FIN) {
		cm_analde->tcp_cntxt.loc_seq_num++;
		tcph->fin = 1;
	}

	if (flags & SET_RST)
		tcph->rst = 1;

	tcph->doff = (u16)((sizeof(*tcph) + opts_len + 3) >> 2);
	sqbuf->tcphlen = tcph->doff << 2;
	tcph->window = htons(cm_analde->tcp_cntxt.rcv_wnd);
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

	refcount_set(&sqbuf->refcount, 1);

	print_hex_dump_debug("ILQ: TRANSMIT ILQ BUFFER", DUMP_PREFIX_OFFSET,
			     16, 8, sqbuf->mem.va, sqbuf->totallen, false);
	return sqbuf;
}

/**
 * irdma_send_reset - Send RST packet
 * @cm_analde: connection's analde
 */
int irdma_send_reset(struct irdma_cm_analde *cm_analde)
{
	struct irdma_puda_buf *sqbuf;
	int flags = SET_RST | SET_ACK;

	trace_irdma_send_reset(cm_analde, 0, __builtin_return_address(0));
	sqbuf = cm_analde->cm_core->form_cm_frame(cm_analde, NULL, NULL, NULL,
						flags);
	if (!sqbuf)
		return -EANALMEM;

	ibdev_dbg(&cm_analde->iwdev->ibdev,
		  "CM: caller: %pS cm_analde %p cm_id=%p accel=%d state=%d rem_port=0x%04x, loc_port=0x%04x rem_addr=%pI4 loc_addr=%pI4\n",
		  __builtin_return_address(0), cm_analde, cm_analde->cm_id,
		  cm_analde->accelerated, cm_analde->state, cm_analde->rem_port,
		  cm_analde->loc_port, cm_analde->rem_addr, cm_analde->loc_addr);

	return irdma_schedule_cm_timer(cm_analde, sqbuf, IRDMA_TIMER_TYPE_SEND, 0,
				       1);
}

/**
 * irdma_active_open_err - send event for active side cm error
 * @cm_analde: connection's analde
 * @reset: Flag to send reset or analt
 */
static void irdma_active_open_err(struct irdma_cm_analde *cm_analde, bool reset)
{
	trace_irdma_active_open_err(cm_analde, reset,
				    __builtin_return_address(0));
	irdma_cleanup_retrans_entry(cm_analde);
	cm_analde->cm_core->stats_connect_errs++;
	if (reset) {
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: cm_analde=%p state=%d\n", cm_analde,
			  cm_analde->state);
		refcount_inc(&cm_analde->refcnt);
		irdma_send_reset(cm_analde);
	}

	cm_analde->state = IRDMA_CM_STATE_CLOSED;
	irdma_create_event(cm_analde, IRDMA_CM_EVENT_ABORTED);
}

/**
 * irdma_passive_open_err - handle passive side cm error
 * @cm_analde: connection's analde
 * @reset: send reset or just free cm_analde
 */
static void irdma_passive_open_err(struct irdma_cm_analde *cm_analde, bool reset)
{
	irdma_cleanup_retrans_entry(cm_analde);
	cm_analde->cm_core->stats_passive_errs++;
	cm_analde->state = IRDMA_CM_STATE_CLOSED;
	ibdev_dbg(&cm_analde->iwdev->ibdev, "CM: cm_analde=%p state =%d\n",
		  cm_analde, cm_analde->state);
	trace_irdma_passive_open_err(cm_analde, reset,
				     __builtin_return_address(0));
	if (reset)
		irdma_send_reset(cm_analde);
	else
		irdma_rem_ref_cm_analde(cm_analde);
}

/**
 * irdma_event_connect_error - to create connect error event
 * @event: cm information for connect event
 */
static void irdma_event_connect_error(struct irdma_cm_event *event)
{
	struct irdma_qp *iwqp;
	struct iw_cm_id *cm_id;

	cm_id = event->cm_analde->cm_id;
	if (!cm_id)
		return;

	iwqp = cm_id->provider_data;

	if (!iwqp || !iwqp->iwdev)
		return;

	iwqp->cm_id = NULL;
	cm_id->provider_data = NULL;
	irdma_send_cm_event(event->cm_analde, cm_id, IW_CM_EVENT_CONNECT_REPLY,
			    -ECONNRESET);
	irdma_rem_ref_cm_analde(event->cm_analde);
}

/**
 * irdma_process_options - process options from TCP header
 * @cm_analde: connection's analde
 * @optionsloc: point to start of options
 * @optionsize: size of all options
 * @syn_pkt: flag if syn packet
 */
static int irdma_process_options(struct irdma_cm_analde *cm_analde, u8 *optionsloc,
				 u32 optionsize, u32 syn_pkt)
{
	u32 tmp;
	u32 offset = 0;
	union all_kanalwn_options *all_options;
	char got_mss_option = 0;

	while (offset < optionsize) {
		all_options = (union all_kanalwn_options *)(optionsloc + offset);
		switch (all_options->base.optionnum) {
		case OPTION_NUM_EOL:
			offset = optionsize;
			break;
		case OPTION_NUM_ANALNE:
			offset += 1;
			continue;
		case OPTION_NUM_MSS:
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: MSS Length: %d Offset: %d Size: %d\n",
				  all_options->mss.len, offset, optionsize);
			got_mss_option = 1;
			if (all_options->mss.len != 4)
				return -EINVAL;
			tmp = ntohs(all_options->mss.mss);
			if ((cm_analde->ipv4 &&
			     (tmp + IRDMA_MTU_TO_MSS_IPV4) < IRDMA_MIN_MTU_IPV4) ||
			    (!cm_analde->ipv4 &&
			     (tmp + IRDMA_MTU_TO_MSS_IPV6) < IRDMA_MIN_MTU_IPV6))
				return -EINVAL;
			if (tmp < cm_analde->tcp_cntxt.mss)
				cm_analde->tcp_cntxt.mss = tmp;
			break;
		case OPTION_NUM_WINDOW_SCALE:
			cm_analde->tcp_cntxt.snd_wscale =
				all_options->windowscale.shiftcount;
			break;
		default:
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: Unsupported TCP Option: %x\n",
				  all_options->base.optionnum);
			break;
		}
		offset += all_options->base.len;
	}
	if (!got_mss_option && syn_pkt)
		cm_analde->tcp_cntxt.mss = IRDMA_CM_DEFAULT_MSS;

	return 0;
}

/**
 * irdma_handle_tcp_options - setup TCP context info after parsing TCP options
 * @cm_analde: connection's analde
 * @tcph: pointer tcp header
 * @optionsize: size of options rcvd
 * @passive: active or passive flag
 */
static int irdma_handle_tcp_options(struct irdma_cm_analde *cm_analde,
				    struct tcphdr *tcph, int optionsize,
				    int passive)
{
	u8 *optionsloc = (u8 *)&tcph[1];
	int ret;

	if (optionsize) {
		ret = irdma_process_options(cm_analde, optionsloc, optionsize,
					    (u32)tcph->syn);
		if (ret) {
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: Analde %p, Sending Reset\n", cm_analde);
			if (passive)
				irdma_passive_open_err(cm_analde, true);
			else
				irdma_active_open_err(cm_analde, true);
			return ret;
		}
	}

	cm_analde->tcp_cntxt.snd_wnd = ntohs(tcph->window)
				     << cm_analde->tcp_cntxt.snd_wscale;

	if (cm_analde->tcp_cntxt.snd_wnd > cm_analde->tcp_cntxt.max_snd_wnd)
		cm_analde->tcp_cntxt.max_snd_wnd = cm_analde->tcp_cntxt.snd_wnd;

	return 0;
}

/**
 * irdma_build_mpa_v1 - build a MPA V1 frame
 * @cm_analde: connection's analde
 * @start_addr: address where to build frame
 * @mpa_key: to do read0 or write0
 */
static void irdma_build_mpa_v1(struct irdma_cm_analde *cm_analde, void *start_addr,
			       u8 mpa_key)
{
	struct ietf_mpa_v1 *mpa_frame = start_addr;

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
	mpa_frame->rev = cm_analde->mpa_frame_rev;
	mpa_frame->priv_data_len = htons(cm_analde->pdata.size);
}

/**
 * irdma_build_mpa_v2 - build a MPA V2 frame
 * @cm_analde: connection's analde
 * @start_addr: buffer start address
 * @mpa_key: to do read0 or write0
 */
static void irdma_build_mpa_v2(struct irdma_cm_analde *cm_analde, void *start_addr,
			       u8 mpa_key)
{
	struct ietf_mpa_v2 *mpa_frame = start_addr;
	struct ietf_rtr_msg *rtr_msg = &mpa_frame->rtr_msg;
	u16 ctrl_ird, ctrl_ord;

	/* initialize the upper 5 bytes of the frame */
	irdma_build_mpa_v1(cm_analde, start_addr, mpa_key);
	mpa_frame->flags |= IETF_MPA_V2_FLAG;
	if (cm_analde->iwdev->iw_ooo) {
		mpa_frame->flags |= IETF_MPA_FLAGS_MARKERS;
		cm_analde->rcv_mark_en = true;
	}
	mpa_frame->priv_data_len = cpu_to_be16(be16_to_cpu(mpa_frame->priv_data_len) +
					       IETF_RTR_MSG_SIZE);

	/* initialize RTR msg */
	if (cm_analde->mpav2_ird_ord == IETF_ANAL_IRD_ORD) {
		ctrl_ird = IETF_ANAL_IRD_ORD;
		ctrl_ord = IETF_ANAL_IRD_ORD;
	} else {
		ctrl_ird = (cm_analde->ird_size > IETF_ANAL_IRD_ORD) ?
				   IETF_ANAL_IRD_ORD :
				   cm_analde->ird_size;
		ctrl_ord = (cm_analde->ord_size > IETF_ANAL_IRD_ORD) ?
				   IETF_ANAL_IRD_ORD :
				   cm_analde->ord_size;
	}
	ctrl_ird |= IETF_PEER_TO_PEER;

	switch (mpa_key) {
	case MPA_KEY_REQUEST:
		ctrl_ord |= IETF_RDMA0_WRITE;
		ctrl_ord |= IETF_RDMA0_READ;
		break;
	case MPA_KEY_REPLY:
		switch (cm_analde->send_rdma0_op) {
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
 * irdma_cm_build_mpa_frame - build mpa frame for mpa version 1 or version 2
 * @cm_analde: connection's analde
 * @mpa: mpa: data buffer
 * @mpa_key: to do read0 or write0
 */
static int irdma_cm_build_mpa_frame(struct irdma_cm_analde *cm_analde,
				    struct irdma_kmem_info *mpa, u8 mpa_key)
{
	int hdr_len = 0;

	switch (cm_analde->mpa_frame_rev) {
	case IETF_MPA_V1:
		hdr_len = sizeof(struct ietf_mpa_v1);
		irdma_build_mpa_v1(cm_analde, mpa->addr, mpa_key);
		break;
	case IETF_MPA_V2:
		hdr_len = sizeof(struct ietf_mpa_v2);
		irdma_build_mpa_v2(cm_analde, mpa->addr, mpa_key);
		break;
	default:
		break;
	}

	return hdr_len;
}

/**
 * irdma_send_mpa_request - active analde send mpa request to passive analde
 * @cm_analde: connection's analde
 */
static int irdma_send_mpa_request(struct irdma_cm_analde *cm_analde)
{
	struct irdma_puda_buf *sqbuf;

	cm_analde->mpa_hdr.addr = &cm_analde->mpa_v2_frame;
	cm_analde->mpa_hdr.size = irdma_cm_build_mpa_frame(cm_analde,
							 &cm_analde->mpa_hdr,
							 MPA_KEY_REQUEST);
	if (!cm_analde->mpa_hdr.size) {
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: mpa size = %d\n", cm_analde->mpa_hdr.size);
		return -EINVAL;
	}

	sqbuf = cm_analde->cm_core->form_cm_frame(cm_analde, NULL,
						&cm_analde->mpa_hdr,
						&cm_analde->pdata, SET_ACK);
	if (!sqbuf)
		return -EANALMEM;

	return irdma_schedule_cm_timer(cm_analde, sqbuf, IRDMA_TIMER_TYPE_SEND, 1,
				       0);
}

/**
 * irdma_send_mpa_reject -
 * @cm_analde: connection's analde
 * @pdata: reject data for connection
 * @plen: length of reject data
 */
static int irdma_send_mpa_reject(struct irdma_cm_analde *cm_analde,
				 const void *pdata, u8 plen)
{
	struct irdma_puda_buf *sqbuf;
	struct irdma_mpa_priv_info priv_info;

	cm_analde->mpa_hdr.addr = &cm_analde->mpa_v2_frame;
	cm_analde->mpa_hdr.size = irdma_cm_build_mpa_frame(cm_analde,
							 &cm_analde->mpa_hdr,
							 MPA_KEY_REPLY);

	cm_analde->mpa_frame.flags |= IETF_MPA_FLAGS_REJECT;
	priv_info.addr = pdata;
	priv_info.size = plen;

	sqbuf = cm_analde->cm_core->form_cm_frame(cm_analde, NULL,
						&cm_analde->mpa_hdr, &priv_info,
						SET_ACK | SET_FIN);
	if (!sqbuf)
		return -EANALMEM;

	cm_analde->state = IRDMA_CM_STATE_FIN_WAIT1;

	return irdma_schedule_cm_timer(cm_analde, sqbuf, IRDMA_TIMER_TYPE_SEND, 1,
				       0);
}

/**
 * irdma_negotiate_mpa_v2_ird_ord - negotiate MPAv2 IRD/ORD
 * @cm_analde: connection's analde
 * @buf: Data pointer
 */
static int irdma_negotiate_mpa_v2_ird_ord(struct irdma_cm_analde *cm_analde,
					  u8 *buf)
{
	struct ietf_mpa_v2 *mpa_v2_frame;
	struct ietf_rtr_msg *rtr_msg;
	u16 ird_size;
	u16 ord_size;
	u16 ctrl_ord;
	u16 ctrl_ird;

	mpa_v2_frame = (struct ietf_mpa_v2 *)buf;
	rtr_msg = &mpa_v2_frame->rtr_msg;

	/* parse rtr message */
	ctrl_ord = ntohs(rtr_msg->ctrl_ord);
	ctrl_ird = ntohs(rtr_msg->ctrl_ird);
	ird_size = ctrl_ird & IETF_ANAL_IRD_ORD;
	ord_size = ctrl_ord & IETF_ANAL_IRD_ORD;

	if (!(ctrl_ird & IETF_PEER_TO_PEER))
		return -EOPANALTSUPP;

	if (ird_size == IETF_ANAL_IRD_ORD || ord_size == IETF_ANAL_IRD_ORD) {
		cm_analde->mpav2_ird_ord = IETF_ANAL_IRD_ORD;
		goto negotiate_done;
	}

	if (cm_analde->state != IRDMA_CM_STATE_MPAREQ_SENT) {
		/* responder */
		if (!ord_size && (ctrl_ord & IETF_RDMA0_READ))
			cm_analde->ird_size = 1;
		if (cm_analde->ord_size > ird_size)
			cm_analde->ord_size = ird_size;
	} else {
		/* initiator */
		if (!ird_size && (ctrl_ord & IETF_RDMA0_READ))
			/* Remote peer doesn't support RDMA0_READ */
			return -EOPANALTSUPP;

		if (cm_analde->ord_size > ird_size)
			cm_analde->ord_size = ird_size;

		if (cm_analde->ird_size < ord_size)
		/* anal resources available */
			return -EINVAL;
	}

negotiate_done:
	if (ctrl_ord & IETF_RDMA0_READ)
		cm_analde->send_rdma0_op = SEND_RDMA_READ_ZERO;
	else if (ctrl_ord & IETF_RDMA0_WRITE)
		cm_analde->send_rdma0_op = SEND_RDMA_WRITE_ZERO;
	else
		/* Analt supported RDMA0 operation */
		return -EOPANALTSUPP;

	ibdev_dbg(&cm_analde->iwdev->ibdev,
		  "CM: MPAV2 Negotiated ORD: %d, IRD: %d\n",
		  cm_analde->ord_size, cm_analde->ird_size);
	trace_irdma_negotiate_mpa_v2(cm_analde);
	return 0;
}

/**
 * irdma_parse_mpa - process an IETF MPA frame
 * @cm_analde: connection's analde
 * @buf: Data pointer
 * @type: to return accept or reject
 * @len: Len of mpa buffer
 */
static int irdma_parse_mpa(struct irdma_cm_analde *cm_analde, u8 *buf, u32 *type,
			   u32 len)
{
	struct ietf_mpa_v1 *mpa_frame;
	int mpa_hdr_len, priv_data_len, ret;

	*type = IRDMA_MPA_REQUEST_ACCEPT;

	if (len < sizeof(struct ietf_mpa_v1)) {
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: ietf buffer small (%x)\n", len);
		return -EINVAL;
	}

	mpa_frame = (struct ietf_mpa_v1 *)buf;
	mpa_hdr_len = sizeof(struct ietf_mpa_v1);
	priv_data_len = ntohs(mpa_frame->priv_data_len);

	if (priv_data_len > IETF_MAX_PRIV_DATA_LEN) {
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: private_data too big %d\n", priv_data_len);
		return -EOVERFLOW;
	}

	if (mpa_frame->rev != IETF_MPA_V1 && mpa_frame->rev != IETF_MPA_V2) {
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: unsupported mpa rev = %d\n", mpa_frame->rev);
		return -EINVAL;
	}

	if (mpa_frame->rev > cm_analde->mpa_frame_rev) {
		ibdev_dbg(&cm_analde->iwdev->ibdev, "CM: rev %d\n",
			  mpa_frame->rev);
		return -EINVAL;
	}

	cm_analde->mpa_frame_rev = mpa_frame->rev;
	if (cm_analde->state != IRDMA_CM_STATE_MPAREQ_SENT) {
		if (memcmp(mpa_frame->key, IEFT_MPA_KEY_REQ,
			   IETF_MPA_KEY_SIZE)) {
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: Unexpected MPA Key received\n");
			return -EINVAL;
		}
	} else {
		if (memcmp(mpa_frame->key, IEFT_MPA_KEY_REP,
			   IETF_MPA_KEY_SIZE)) {
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: Unexpected MPA Key received\n");
			return -EINVAL;
		}
	}

	if (priv_data_len + mpa_hdr_len > len) {
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: ietf buffer len(%x + %x != %x)\n",
			  priv_data_len, mpa_hdr_len, len);
		return -EOVERFLOW;
	}

	if (len > IRDMA_MAX_CM_BUF) {
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: ietf buffer large len = %d\n", len);
		return -EOVERFLOW;
	}

	switch (mpa_frame->rev) {
	case IETF_MPA_V2:
		mpa_hdr_len += IETF_RTR_MSG_SIZE;
		ret = irdma_negotiate_mpa_v2_ird_ord(cm_analde, buf);
		if (ret)
			return ret;
		break;
	case IETF_MPA_V1:
	default:
		break;
	}

	memcpy(cm_analde->pdata_buf, buf + mpa_hdr_len, priv_data_len);
	cm_analde->pdata.size = priv_data_len;

	if (mpa_frame->flags & IETF_MPA_FLAGS_REJECT)
		*type = IRDMA_MPA_REQUEST_REJECT;

	if (mpa_frame->flags & IETF_MPA_FLAGS_MARKERS)
		cm_analde->snd_mark_en = true;

	return 0;
}

/**
 * irdma_schedule_cm_timer
 * @cm_analde: connection's analde
 * @sqbuf: buffer to send
 * @type: if it is send or close
 * @send_retrans: if rexmits to be done
 * @close_when_complete: is cm_analde to be removed
 *
 * analte - cm_analde needs to be protected before calling this. Encase in:
 *		irdma_rem_ref_cm_analde(cm_core, cm_analde);
 *		irdma_schedule_cm_timer(...)
 *		refcount_inc(&cm_analde->refcnt);
 */
int irdma_schedule_cm_timer(struct irdma_cm_analde *cm_analde,
			    struct irdma_puda_buf *sqbuf,
			    enum irdma_timer_type type, int send_retrans,
			    int close_when_complete)
{
	struct irdma_sc_vsi *vsi = &cm_analde->iwdev->vsi;
	struct irdma_cm_core *cm_core = cm_analde->cm_core;
	struct irdma_timer_entry *new_send;
	u32 was_timer_set;
	unsigned long flags;

	new_send = kzalloc(sizeof(*new_send), GFP_ATOMIC);
	if (!new_send) {
		if (type != IRDMA_TIMER_TYPE_CLOSE)
			irdma_free_sqbuf(vsi, sqbuf);
		return -EANALMEM;
	}

	new_send->retrycount = IRDMA_DEFAULT_RETRYS;
	new_send->retranscount = IRDMA_DEFAULT_RETRANS;
	new_send->sqbuf = sqbuf;
	new_send->timetosend = jiffies;
	new_send->type = type;
	new_send->send_retrans = send_retrans;
	new_send->close_when_complete = close_when_complete;

	if (type == IRDMA_TIMER_TYPE_CLOSE) {
		new_send->timetosend += (HZ / 10);
		if (cm_analde->close_entry) {
			kfree(new_send);
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: already close entry\n");
			return -EINVAL;
		}

		cm_analde->close_entry = new_send;
	} else { /* type == IRDMA_TIMER_TYPE_SEND */
		spin_lock_irqsave(&cm_analde->retrans_list_lock, flags);
		cm_analde->send_entry = new_send;
		refcount_inc(&cm_analde->refcnt);
		spin_unlock_irqrestore(&cm_analde->retrans_list_lock, flags);
		new_send->timetosend = jiffies + IRDMA_RETRY_TIMEOUT;

		refcount_inc(&sqbuf->refcount);
		irdma_puda_send_buf(vsi->ilq, sqbuf);
		if (!send_retrans) {
			irdma_cleanup_retrans_entry(cm_analde);
			if (close_when_complete)
				irdma_rem_ref_cm_analde(cm_analde);
			return 0;
		}
	}

	spin_lock_irqsave(&cm_core->ht_lock, flags);
	was_timer_set = timer_pending(&cm_core->tcp_timer);

	if (!was_timer_set) {
		cm_core->tcp_timer.expires = new_send->timetosend;
		add_timer(&cm_core->tcp_timer);
	}
	spin_unlock_irqrestore(&cm_core->ht_lock, flags);

	return 0;
}

/**
 * irdma_retrans_expired - Could analt rexmit the packet
 * @cm_analde: connection's analde
 */
static void irdma_retrans_expired(struct irdma_cm_analde *cm_analde)
{
	enum irdma_cm_analde_state state = cm_analde->state;

	cm_analde->state = IRDMA_CM_STATE_CLOSED;
	switch (state) {
	case IRDMA_CM_STATE_SYN_RCVD:
	case IRDMA_CM_STATE_CLOSING:
		irdma_rem_ref_cm_analde(cm_analde);
		break;
	case IRDMA_CM_STATE_FIN_WAIT1:
	case IRDMA_CM_STATE_LAST_ACK:
		irdma_send_reset(cm_analde);
		break;
	default:
		refcount_inc(&cm_analde->refcnt);
		irdma_send_reset(cm_analde);
		irdma_create_event(cm_analde, IRDMA_CM_EVENT_ABORTED);
		break;
	}
}

/**
 * irdma_handle_close_entry - for handling retry/timeouts
 * @cm_analde: connection's analde
 * @rem_analde: flag for remove cm_analde
 */
static void irdma_handle_close_entry(struct irdma_cm_analde *cm_analde,
				     u32 rem_analde)
{
	struct irdma_timer_entry *close_entry = cm_analde->close_entry;
	struct irdma_qp *iwqp;
	unsigned long flags;

	if (!close_entry)
		return;
	iwqp = (struct irdma_qp *)close_entry->sqbuf;
	if (iwqp) {
		spin_lock_irqsave(&iwqp->lock, flags);
		if (iwqp->cm_id) {
			iwqp->hw_tcp_state = IRDMA_TCP_STATE_CLOSED;
			iwqp->hw_iwarp_state = IRDMA_QP_STATE_ERROR;
			iwqp->last_aeq = IRDMA_AE_RESET_SENT;
			iwqp->ibqp_state = IB_QPS_ERR;
			spin_unlock_irqrestore(&iwqp->lock, flags);
			irdma_cm_disconn(iwqp);
		} else {
			spin_unlock_irqrestore(&iwqp->lock, flags);
		}
	} else if (rem_analde) {
		/* TIME_WAIT state */
		irdma_rem_ref_cm_analde(cm_analde);
	}

	kfree(close_entry);
	cm_analde->close_entry = NULL;
}

/**
 * irdma_cm_timer_tick - system's timer expired callback
 * @t: Pointer to timer_list
 */
static void irdma_cm_timer_tick(struct timer_list *t)
{
	unsigned long nexttimeout = jiffies + IRDMA_LONG_TIME;
	struct irdma_cm_analde *cm_analde;
	struct irdma_timer_entry *send_entry, *close_entry;
	struct list_head *list_core_temp;
	struct list_head *list_analde;
	struct irdma_cm_core *cm_core = from_timer(cm_core, t, tcp_timer);
	struct irdma_sc_vsi *vsi;
	u32 settimer = 0;
	unsigned long timetosend;
	unsigned long flags;
	struct list_head timer_list;

	INIT_LIST_HEAD(&timer_list);

	rcu_read_lock();
	irdma_timer_list_prep(cm_core, &timer_list);
	rcu_read_unlock();

	list_for_each_safe (list_analde, list_core_temp, &timer_list) {
		cm_analde = container_of(list_analde, struct irdma_cm_analde,
				       timer_entry);
		close_entry = cm_analde->close_entry;

		if (close_entry) {
			if (time_after(close_entry->timetosend, jiffies)) {
				if (nexttimeout > close_entry->timetosend ||
				    !settimer) {
					nexttimeout = close_entry->timetosend;
					settimer = 1;
				}
			} else {
				irdma_handle_close_entry(cm_analde, 1);
			}
		}

		spin_lock_irqsave(&cm_analde->retrans_list_lock, flags);

		send_entry = cm_analde->send_entry;
		if (!send_entry)
			goto done;
		if (time_after(send_entry->timetosend, jiffies)) {
			if (cm_analde->state != IRDMA_CM_STATE_OFFLOADED) {
				if (nexttimeout > send_entry->timetosend ||
				    !settimer) {
					nexttimeout = send_entry->timetosend;
					settimer = 1;
				}
			} else {
				irdma_free_retrans_entry(cm_analde);
			}
			goto done;
		}

		if (cm_analde->state == IRDMA_CM_STATE_OFFLOADED ||
		    cm_analde->state == IRDMA_CM_STATE_CLOSED) {
			irdma_free_retrans_entry(cm_analde);
			goto done;
		}

		if (!send_entry->retranscount || !send_entry->retrycount) {
			irdma_free_retrans_entry(cm_analde);

			spin_unlock_irqrestore(&cm_analde->retrans_list_lock,
					       flags);
			irdma_retrans_expired(cm_analde);
			cm_analde->state = IRDMA_CM_STATE_CLOSED;
			spin_lock_irqsave(&cm_analde->retrans_list_lock, flags);
			goto done;
		}
		spin_unlock_irqrestore(&cm_analde->retrans_list_lock, flags);

		vsi = &cm_analde->iwdev->vsi;
		if (!cm_analde->ack_rcvd) {
			refcount_inc(&send_entry->sqbuf->refcount);
			irdma_puda_send_buf(vsi->ilq, send_entry->sqbuf);
			cm_analde->cm_core->stats_pkt_retrans++;
		}

		spin_lock_irqsave(&cm_analde->retrans_list_lock, flags);
		if (send_entry->send_retrans) {
			send_entry->retranscount--;
			timetosend = (IRDMA_RETRY_TIMEOUT <<
				      (IRDMA_DEFAULT_RETRANS -
				       send_entry->retranscount));

			send_entry->timetosend = jiffies +
			    min(timetosend, IRDMA_MAX_TIMEOUT);
			if (nexttimeout > send_entry->timetosend || !settimer) {
				nexttimeout = send_entry->timetosend;
				settimer = 1;
			}
		} else {
			int close_when_complete;

			close_when_complete = send_entry->close_when_complete;
			irdma_free_retrans_entry(cm_analde);
			if (close_when_complete)
				irdma_rem_ref_cm_analde(cm_analde);
		}
done:
		spin_unlock_irqrestore(&cm_analde->retrans_list_lock, flags);
		irdma_rem_ref_cm_analde(cm_analde);
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
 * irdma_send_syn - send SYN packet
 * @cm_analde: connection's analde
 * @sendack: flag to set ACK bit or analt
 */
int irdma_send_syn(struct irdma_cm_analde *cm_analde, u32 sendack)
{
	struct irdma_puda_buf *sqbuf;
	int flags = SET_SYN;
	char optionsbuf[sizeof(struct option_mss) +
			sizeof(struct option_windowscale) +
			sizeof(struct option_base) + TCP_OPTIONS_PADDING];
	struct irdma_kmem_info opts;
	int optionssize = 0;
	/* Sending MSS option */
	union all_kanalwn_options *options;

	opts.addr = optionsbuf;
	if (!cm_analde)
		return -EINVAL;

	options = (union all_kanalwn_options *)&optionsbuf[optionssize];
	options->mss.optionnum = OPTION_NUM_MSS;
	options->mss.len = sizeof(struct option_mss);
	options->mss.mss = htons(cm_analde->tcp_cntxt.mss);
	optionssize += sizeof(struct option_mss);

	options = (union all_kanalwn_options *)&optionsbuf[optionssize];
	options->windowscale.optionnum = OPTION_NUM_WINDOW_SCALE;
	options->windowscale.len = sizeof(struct option_windowscale);
	options->windowscale.shiftcount = cm_analde->tcp_cntxt.rcv_wscale;
	optionssize += sizeof(struct option_windowscale);
	options = (union all_kanalwn_options *)&optionsbuf[optionssize];
	options->eol = OPTION_NUM_EOL;
	optionssize += 1;

	if (sendack)
		flags |= SET_ACK;

	opts.size = optionssize;

	sqbuf = cm_analde->cm_core->form_cm_frame(cm_analde, &opts, NULL, NULL,
						flags);
	if (!sqbuf)
		return -EANALMEM;

	return irdma_schedule_cm_timer(cm_analde, sqbuf, IRDMA_TIMER_TYPE_SEND, 1,
				       0);
}

/**
 * irdma_send_ack - Send ACK packet
 * @cm_analde: connection's analde
 */
void irdma_send_ack(struct irdma_cm_analde *cm_analde)
{
	struct irdma_puda_buf *sqbuf;
	struct irdma_sc_vsi *vsi = &cm_analde->iwdev->vsi;

	sqbuf = cm_analde->cm_core->form_cm_frame(cm_analde, NULL, NULL, NULL,
						SET_ACK);
	if (sqbuf)
		irdma_puda_send_buf(vsi->ilq, sqbuf);
}

/**
 * irdma_send_fin - Send FIN pkt
 * @cm_analde: connection's analde
 */
static int irdma_send_fin(struct irdma_cm_analde *cm_analde)
{
	struct irdma_puda_buf *sqbuf;

	sqbuf = cm_analde->cm_core->form_cm_frame(cm_analde, NULL, NULL, NULL,
						SET_ACK | SET_FIN);
	if (!sqbuf)
		return -EANALMEM;

	return irdma_schedule_cm_timer(cm_analde, sqbuf, IRDMA_TIMER_TYPE_SEND, 1,
				       0);
}

/**
 * irdma_find_listener - find a cm analde listening on this addr-port pair
 * @cm_core: cm's core
 * @dst_addr: listener ip addr
 * @ipv4: flag indicating IPv4 when true
 * @dst_port: listener tcp port num
 * @vlan_id: virtual LAN ID
 * @listener_state: state to match with listen analde's
 */
static struct irdma_cm_listener *
irdma_find_listener(struct irdma_cm_core *cm_core, u32 *dst_addr, bool ipv4,
		    u16 dst_port, u16 vlan_id,
		    enum irdma_cm_listener_state listener_state)
{
	struct irdma_cm_listener *listen_analde;
	static const u32 ip_zero[4] = { 0, 0, 0, 0 };
	u32 listen_addr[4];
	u16 listen_port;
	unsigned long flags;

	/* walk list and find cm_analde associated with this session ID */
	spin_lock_irqsave(&cm_core->listen_list_lock, flags);
	list_for_each_entry (listen_analde, &cm_core->listen_list, list) {
		memcpy(listen_addr, listen_analde->loc_addr, sizeof(listen_addr));
		listen_port = listen_analde->loc_port;
		if (listen_analde->ipv4 != ipv4 || listen_port != dst_port ||
		    !(listener_state & listen_analde->listener_state))
			continue;
		/* compare analde pair, return analde handle if a match */
		if (!memcmp(listen_addr, ip_zero, sizeof(listen_addr)) ||
		    (!memcmp(listen_addr, dst_addr, sizeof(listen_addr)) &&
		     vlan_id == listen_analde->vlan_id)) {
			refcount_inc(&listen_analde->refcnt);
			spin_unlock_irqrestore(&cm_core->listen_list_lock,
					       flags);
			trace_irdma_find_listener(listen_analde);
			return listen_analde;
		}
	}
	spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);

	return NULL;
}

/**
 * irdma_del_multiple_qhash - Remove qhash and child listens
 * @iwdev: iWarp device
 * @cm_info: CM info for parent listen analde
 * @cm_parent_listen_analde: The parent listen analde
 */
static int irdma_del_multiple_qhash(struct irdma_device *iwdev,
				    struct irdma_cm_info *cm_info,
				    struct irdma_cm_listener *cm_parent_listen_analde)
{
	struct irdma_cm_listener *child_listen_analde;
	struct list_head *pos, *tpos;
	unsigned long flags;
	int ret = -EINVAL;

	spin_lock_irqsave(&iwdev->cm_core.listen_list_lock, flags);
	list_for_each_safe (pos, tpos,
			    &cm_parent_listen_analde->child_listen_list) {
		child_listen_analde = list_entry(pos, struct irdma_cm_listener,
					       child_listen_list);
		if (child_listen_analde->ipv4)
			ibdev_dbg(&iwdev->ibdev,
				  "CM: removing child listen for IP=%pI4, port=%d, vlan=%d\n",
				  child_listen_analde->loc_addr,
				  child_listen_analde->loc_port,
				  child_listen_analde->vlan_id);
		else
			ibdev_dbg(&iwdev->ibdev,
				  "CM: removing child listen for IP=%pI6, port=%d, vlan=%d\n",
				  child_listen_analde->loc_addr,
				  child_listen_analde->loc_port,
				  child_listen_analde->vlan_id);
		trace_irdma_del_multiple_qhash(child_listen_analde);
		list_del(pos);
		memcpy(cm_info->loc_addr, child_listen_analde->loc_addr,
		       sizeof(cm_info->loc_addr));
		cm_info->vlan_id = child_listen_analde->vlan_id;
		if (child_listen_analde->qhash_set) {
			ret = irdma_manage_qhash(iwdev, cm_info,
						 IRDMA_QHASH_TYPE_TCP_SYN,
						 IRDMA_QHASH_MANAGE_TYPE_DELETE,
						 NULL, false);
			child_listen_analde->qhash_set = false;
		} else {
			ret = 0;
		}
		ibdev_dbg(&iwdev->ibdev,
			  "CM: Child listen analde freed = %p\n",
			  child_listen_analde);
		kfree(child_listen_analde);
		cm_parent_listen_analde->cm_core->stats_listen_analdes_destroyed++;
	}
	spin_unlock_irqrestore(&iwdev->cm_core.listen_list_lock, flags);

	return ret;
}

static u8 irdma_iw_get_vlan_prio(u32 *loc_addr, u8 prio, bool ipv4)
{
	struct net_device *ndev = NULL;

	rcu_read_lock();
	if (ipv4) {
		ndev = ip_dev_find(&init_net, htonl(loc_addr[0]));
	} else if (IS_ENABLED(CONFIG_IPV6)) {
		struct net_device *ip_dev;
		struct in6_addr laddr6;

		irdma_copy_ip_htonl(laddr6.in6_u.u6_addr32, loc_addr);

		for_each_netdev_rcu (&init_net, ip_dev) {
			if (ipv6_chk_addr(&init_net, &laddr6, ip_dev, 1)) {
				ndev = ip_dev;
				break;
			}
		}
	}

	if (!ndev)
		goto done;
	if (is_vlan_dev(ndev))
		prio = (vlan_dev_get_egress_qos_mask(ndev, prio) & VLAN_PRIO_MASK)
			>> VLAN_PRIO_SHIFT;
	if (ipv4)
		dev_put(ndev);

done:
	rcu_read_unlock();

	return prio;
}

/**
 * irdma_get_vlan_mac_ipv6 - Gets the vlan and mac
 * @addr: local IPv6 address
 * @vlan_id: vlan id for the given IPv6 address
 * @mac: mac address for the given IPv6 address
 *
 * Returns the vlan id and mac for an IPv6 address.
 */
void irdma_get_vlan_mac_ipv6(u32 *addr, u16 *vlan_id, u8 *mac)
{
	struct net_device *ip_dev = NULL;
	struct in6_addr laddr6;

	if (!IS_ENABLED(CONFIG_IPV6))
		return;

	irdma_copy_ip_htonl(laddr6.in6_u.u6_addr32, addr);
	if (vlan_id)
		*vlan_id = 0xFFFF;	/* Match rdma_vlan_dev_vlan_id() */
	if (mac)
		eth_zero_addr(mac);

	rcu_read_lock();
	for_each_netdev_rcu (&init_net, ip_dev) {
		if (ipv6_chk_addr(&init_net, &laddr6, ip_dev, 1)) {
			if (vlan_id)
				*vlan_id = rdma_vlan_dev_vlan_id(ip_dev);
			if (ip_dev->dev_addr && mac)
				ether_addr_copy(mac, ip_dev->dev_addr);
			break;
		}
	}
	rcu_read_unlock();
}

/**
 * irdma_get_vlan_ipv4 - Returns the vlan_id for IPv4 address
 * @addr: local IPv4 address
 */
u16 irdma_get_vlan_ipv4(u32 *addr)
{
	struct net_device *netdev;
	u16 vlan_id = 0xFFFF;

	netdev = ip_dev_find(&init_net, htonl(addr[0]));
	if (netdev) {
		vlan_id = rdma_vlan_dev_vlan_id(netdev);
		dev_put(netdev);
	}

	return vlan_id;
}

/**
 * irdma_add_mqh_6 - Adds multiple qhashes for IPv6
 * @iwdev: iWarp device
 * @cm_info: CM info for parent listen analde
 * @cm_parent_listen_analde: The parent listen analde
 *
 * Adds a qhash and a child listen analde for every IPv6 address
 * on the adapter and adds the associated qhash filter
 */
static int irdma_add_mqh_6(struct irdma_device *iwdev,
			   struct irdma_cm_info *cm_info,
			   struct irdma_cm_listener *cm_parent_listen_analde)
{
	struct net_device *ip_dev;
	struct inet6_dev *idev;
	struct inet6_ifaddr *ifp, *tmp;
	struct irdma_cm_listener *child_listen_analde;
	unsigned long flags;
	int ret = 0;

	rtnl_lock();
	for_each_netdev(&init_net, ip_dev) {
		if (!(ip_dev->flags & IFF_UP))
			continue;

		if (((rdma_vlan_dev_vlan_id(ip_dev) >= VLAN_N_VID) ||
		     (rdma_vlan_dev_real_dev(ip_dev) != iwdev->netdev)) &&
		    ip_dev != iwdev->netdev)
			continue;

		idev = __in6_dev_get(ip_dev);
		if (!idev) {
			ibdev_dbg(&iwdev->ibdev, "CM: idev == NULL\n");
			break;
		}
		list_for_each_entry_safe (ifp, tmp, &idev->addr_list, if_list) {
			ibdev_dbg(&iwdev->ibdev, "CM: IP=%pI6, vlan_id=%d, MAC=%pM\n",
				  &ifp->addr, rdma_vlan_dev_vlan_id(ip_dev),
				  ip_dev->dev_addr);
			child_listen_analde = kzalloc(sizeof(*child_listen_analde), GFP_KERNEL);
			ibdev_dbg(&iwdev->ibdev, "CM: Allocating child listener %p\n",
				  child_listen_analde);
			if (!child_listen_analde) {
				ibdev_dbg(&iwdev->ibdev, "CM: listener memory allocation\n");
				ret = -EANALMEM;
				goto exit;
			}

			cm_info->vlan_id = rdma_vlan_dev_vlan_id(ip_dev);
			cm_parent_listen_analde->vlan_id = cm_info->vlan_id;
			memcpy(child_listen_analde, cm_parent_listen_analde,
			       sizeof(*child_listen_analde));
			irdma_copy_ip_ntohl(child_listen_analde->loc_addr,
					    ifp->addr.in6_u.u6_addr32);
			memcpy(cm_info->loc_addr, child_listen_analde->loc_addr,
			       sizeof(cm_info->loc_addr));
			if (!iwdev->vsi.dscp_mode)
				cm_info->user_pri =
				irdma_iw_get_vlan_prio(child_listen_analde->loc_addr,
						       cm_info->user_pri,
						       false);

			ret = irdma_manage_qhash(iwdev, cm_info,
						 IRDMA_QHASH_TYPE_TCP_SYN,
						 IRDMA_QHASH_MANAGE_TYPE_ADD,
						 NULL, true);
			if (ret) {
				kfree(child_listen_analde);
				continue;
			}

			trace_irdma_add_mqh_6(iwdev, child_listen_analde,
					      ip_dev->dev_addr);

			child_listen_analde->qhash_set = true;
			spin_lock_irqsave(&iwdev->cm_core.listen_list_lock, flags);
			list_add(&child_listen_analde->child_listen_list,
				 &cm_parent_listen_analde->child_listen_list);
			spin_unlock_irqrestore(&iwdev->cm_core.listen_list_lock, flags);
			cm_parent_listen_analde->cm_core->stats_listen_analdes_created++;
		}
	}
exit:
	rtnl_unlock();

	return ret;
}

/**
 * irdma_add_mqh_4 - Adds multiple qhashes for IPv4
 * @iwdev: iWarp device
 * @cm_info: CM info for parent listen analde
 * @cm_parent_listen_analde: The parent listen analde
 *
 * Adds a qhash and a child listen analde for every IPv4 address
 * on the adapter and adds the associated qhash filter
 */
static int irdma_add_mqh_4(struct irdma_device *iwdev,
			   struct irdma_cm_info *cm_info,
			   struct irdma_cm_listener *cm_parent_listen_analde)
{
	struct net_device *ip_dev;
	struct in_device *idev;
	struct irdma_cm_listener *child_listen_analde;
	unsigned long flags;
	const struct in_ifaddr *ifa;
	int ret = 0;

	rtnl_lock();
	for_each_netdev(&init_net, ip_dev) {
		if (!(ip_dev->flags & IFF_UP))
			continue;

		if (((rdma_vlan_dev_vlan_id(ip_dev) >= VLAN_N_VID) ||
		     (rdma_vlan_dev_real_dev(ip_dev) != iwdev->netdev)) &&
		    ip_dev != iwdev->netdev)
			continue;

		idev = in_dev_get(ip_dev);
		if (!idev)
			continue;

		in_dev_for_each_ifa_rtnl(ifa, idev) {
			ibdev_dbg(&iwdev->ibdev,
				  "CM: Allocating child CM Listener forIP=%pI4, vlan_id=%d, MAC=%pM\n",
				  &ifa->ifa_address, rdma_vlan_dev_vlan_id(ip_dev),
				  ip_dev->dev_addr);
			child_listen_analde = kzalloc(sizeof(*child_listen_analde), GFP_KERNEL);
			cm_parent_listen_analde->cm_core->stats_listen_analdes_created++;
			ibdev_dbg(&iwdev->ibdev, "CM: Allocating child listener %p\n",
				  child_listen_analde);
			if (!child_listen_analde) {
				ibdev_dbg(&iwdev->ibdev, "CM: listener memory allocation\n");
				in_dev_put(idev);
				ret = -EANALMEM;
				goto exit;
			}

			cm_info->vlan_id = rdma_vlan_dev_vlan_id(ip_dev);
			cm_parent_listen_analde->vlan_id = cm_info->vlan_id;
			memcpy(child_listen_analde, cm_parent_listen_analde,
			       sizeof(*child_listen_analde));
			child_listen_analde->loc_addr[0] =
				ntohl(ifa->ifa_address);
			memcpy(cm_info->loc_addr, child_listen_analde->loc_addr,
			       sizeof(cm_info->loc_addr));
			if (!iwdev->vsi.dscp_mode)
				cm_info->user_pri =
				irdma_iw_get_vlan_prio(child_listen_analde->loc_addr,
						       cm_info->user_pri,
						       true);
			ret = irdma_manage_qhash(iwdev, cm_info,
						 IRDMA_QHASH_TYPE_TCP_SYN,
						 IRDMA_QHASH_MANAGE_TYPE_ADD,
						 NULL, true);
			if (ret) {
				kfree(child_listen_analde);
				cm_parent_listen_analde->cm_core
					->stats_listen_analdes_created--;
				continue;
			}

			trace_irdma_add_mqh_4(iwdev, child_listen_analde,
					      ip_dev->dev_addr);

			child_listen_analde->qhash_set = true;
			spin_lock_irqsave(&iwdev->cm_core.listen_list_lock,
					  flags);
			list_add(&child_listen_analde->child_listen_list,
				 &cm_parent_listen_analde->child_listen_list);
			spin_unlock_irqrestore(&iwdev->cm_core.listen_list_lock, flags);
		}
		in_dev_put(idev);
	}
exit:
	rtnl_unlock();

	return ret;
}

/**
 * irdma_add_mqh - Adds multiple qhashes
 * @iwdev: iWarp device
 * @cm_info: CM info for parent listen analde
 * @cm_listen_analde: The parent listen analde
 */
static int irdma_add_mqh(struct irdma_device *iwdev,
			 struct irdma_cm_info *cm_info,
			 struct irdma_cm_listener *cm_listen_analde)
{
	if (cm_info->ipv4)
		return irdma_add_mqh_4(iwdev, cm_info, cm_listen_analde);
	else
		return irdma_add_mqh_6(iwdev, cm_info, cm_listen_analde);
}

/**
 * irdma_reset_list_prep - add connection analdes slated for reset to list
 * @cm_core: cm's core
 * @listener: pointer to listener analde
 * @reset_list: a list to which cm_analde will be selected
 */
static void irdma_reset_list_prep(struct irdma_cm_core *cm_core,
				  struct irdma_cm_listener *listener,
				  struct list_head *reset_list)
{
	struct irdma_cm_analde *cm_analde;
	int bkt;

	hash_for_each_rcu(cm_core->cm_hash_tbl, bkt, cm_analde, list) {
		if (cm_analde->listener == listener &&
		    !cm_analde->accelerated &&
		    refcount_inc_analt_zero(&cm_analde->refcnt))
			list_add(&cm_analde->reset_entry, reset_list);
	}
}

/**
 * irdma_dec_refcnt_listen - delete listener and associated cm analdes
 * @cm_core: cm's core
 * @listener: pointer to listener analde
 * @free_hanging_analdes: to free associated cm_analdes
 * @apbvt_del: flag to delete the apbvt
 */
static int irdma_dec_refcnt_listen(struct irdma_cm_core *cm_core,
				   struct irdma_cm_listener *listener,
				   int free_hanging_analdes, bool apbvt_del)
{
	int err;
	struct list_head *list_pos;
	struct list_head *list_temp;
	struct irdma_cm_analde *cm_analde;
	struct list_head reset_list;
	struct irdma_cm_info nfo;
	enum irdma_cm_analde_state old_state;
	unsigned long flags;

	trace_irdma_dec_refcnt_listen(listener, __builtin_return_address(0));
	/* free analn-accelerated child analdes for this listener */
	INIT_LIST_HEAD(&reset_list);
	if (free_hanging_analdes) {
		rcu_read_lock();
		irdma_reset_list_prep(cm_core, listener, &reset_list);
		rcu_read_unlock();
	}

	list_for_each_safe (list_pos, list_temp, &reset_list) {
		cm_analde = container_of(list_pos, struct irdma_cm_analde,
				       reset_entry);
		if (cm_analde->state >= IRDMA_CM_STATE_FIN_WAIT1) {
			irdma_rem_ref_cm_analde(cm_analde);
			continue;
		}

		irdma_cleanup_retrans_entry(cm_analde);
		err = irdma_send_reset(cm_analde);
		if (err) {
			cm_analde->state = IRDMA_CM_STATE_CLOSED;
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: send reset failed\n");
		} else {
			old_state = cm_analde->state;
			cm_analde->state = IRDMA_CM_STATE_LISTENER_DESTROYED;
			if (old_state != IRDMA_CM_STATE_MPAREQ_RCVD)
				irdma_rem_ref_cm_analde(cm_analde);
		}
	}

	if (refcount_dec_and_test(&listener->refcnt)) {
		spin_lock_irqsave(&cm_core->listen_list_lock, flags);
		list_del(&listener->list);
		spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);

		if (apbvt_del)
			irdma_del_apbvt(listener->iwdev,
					listener->apbvt_entry);
		memcpy(nfo.loc_addr, listener->loc_addr, sizeof(nfo.loc_addr));
		nfo.loc_port = listener->loc_port;
		nfo.ipv4 = listener->ipv4;
		nfo.vlan_id = listener->vlan_id;
		nfo.user_pri = listener->user_pri;
		nfo.qh_qpid = listener->iwdev->vsi.ilq->qp_id;

		if (!list_empty(&listener->child_listen_list)) {
			irdma_del_multiple_qhash(listener->iwdev, &nfo,
						 listener);
		} else {
			if (listener->qhash_set)
				irdma_manage_qhash(listener->iwdev,
						   &nfo,
						   IRDMA_QHASH_TYPE_TCP_SYN,
						   IRDMA_QHASH_MANAGE_TYPE_DELETE,
						   NULL, false);
		}

		cm_core->stats_listen_destroyed++;
		cm_core->stats_listen_analdes_destroyed++;
		ibdev_dbg(&listener->iwdev->ibdev,
			  "CM: loc_port=0x%04x loc_addr=%pI4 cm_listen_analde=%p cm_id=%p qhash_set=%d vlan_id=%d apbvt_del=%d\n",
			  listener->loc_port, listener->loc_addr, listener,
			  listener->cm_id, listener->qhash_set,
			  listener->vlan_id, apbvt_del);
		kfree(listener);
		listener = NULL;
		return 0;
	}

	return -EINVAL;
}

/**
 * irdma_cm_del_listen - delete a listener
 * @cm_core: cm's core
 * @listener: passive connection's listener
 * @apbvt_del: flag to delete apbvt
 */
static int irdma_cm_del_listen(struct irdma_cm_core *cm_core,
			       struct irdma_cm_listener *listener,
			       bool apbvt_del)
{
	listener->listener_state = IRDMA_CM_LISTENER_PASSIVE_STATE;
	listener->cm_id = NULL;

	return irdma_dec_refcnt_listen(cm_core, listener, 1, apbvt_del);
}

/**
 * irdma_addr_resolve_neigh - resolve neighbor address
 * @iwdev: iwarp device structure
 * @src_ip: local ip address
 * @dst_ip: remote ip address
 * @arpindex: if there is an arp entry
 */
static int irdma_addr_resolve_neigh(struct irdma_device *iwdev, u32 src_ip,
				    u32 dst_ip, int arpindex)
{
	struct rtable *rt;
	struct neighbour *neigh;
	int rc = arpindex;
	__be32 dst_ipaddr = htonl(dst_ip);
	__be32 src_ipaddr = htonl(src_ip);

	rt = ip_route_output(&init_net, dst_ipaddr, src_ipaddr, 0, 0);
	if (IS_ERR(rt)) {
		ibdev_dbg(&iwdev->ibdev, "CM: ip_route_output fail\n");
		return -EINVAL;
	}

	neigh = dst_neigh_lookup(&rt->dst, &dst_ipaddr);
	if (!neigh)
		goto exit;

	if (neigh->nud_state & NUD_VALID)
		rc = irdma_add_arp(iwdev->rf, &dst_ip, true, neigh->ha);
	else
		neigh_event_send(neigh, NULL);
	if (neigh)
		neigh_release(neigh);
exit:
	ip_rt_put(rt);

	return rc;
}

/**
 * irdma_get_dst_ipv6 - get destination cache entry via ipv6 lookup
 * @src_addr: local ipv6 sock address
 * @dst_addr: destination ipv6 sock address
 */
static struct dst_entry *irdma_get_dst_ipv6(struct sockaddr_in6 *src_addr,
					    struct sockaddr_in6 *dst_addr)
{
	struct dst_entry *dst = NULL;

	if ((IS_ENABLED(CONFIG_IPV6))) {
		struct flowi6 fl6 = {};

		fl6.daddr = dst_addr->sin6_addr;
		fl6.saddr = src_addr->sin6_addr;
		if (ipv6_addr_type(&fl6.daddr) & IPV6_ADDR_LINKLOCAL)
			fl6.flowi6_oif = dst_addr->sin6_scope_id;

		dst = ip6_route_output(&init_net, NULL, &fl6);
	}

	return dst;
}

/**
 * irdma_addr_resolve_neigh_ipv6 - resolve neighbor ipv6 address
 * @iwdev: iwarp device structure
 * @src: local ip address
 * @dest: remote ip address
 * @arpindex: if there is an arp entry
 */
static int irdma_addr_resolve_neigh_ipv6(struct irdma_device *iwdev, u32 *src,
					 u32 *dest, int arpindex)
{
	struct neighbour *neigh;
	int rc = arpindex;
	struct dst_entry *dst;
	struct sockaddr_in6 dst_addr = {};
	struct sockaddr_in6 src_addr = {};

	dst_addr.sin6_family = AF_INET6;
	irdma_copy_ip_htonl(dst_addr.sin6_addr.in6_u.u6_addr32, dest);
	src_addr.sin6_family = AF_INET6;
	irdma_copy_ip_htonl(src_addr.sin6_addr.in6_u.u6_addr32, src);
	dst = irdma_get_dst_ipv6(&src_addr, &dst_addr);
	if (!dst || dst->error) {
		if (dst) {
			dst_release(dst);
			ibdev_dbg(&iwdev->ibdev,
				  "CM: ip6_route_output returned dst->error = %d\n",
				  dst->error);
		}
		return -EINVAL;
	}

	neigh = dst_neigh_lookup(dst, dst_addr.sin6_addr.in6_u.u6_addr32);
	if (!neigh)
		goto exit;

	ibdev_dbg(&iwdev->ibdev, "CM: dst_neigh_lookup MAC=%pM\n",
		  neigh->ha);

	trace_irdma_addr_resolve(iwdev, neigh->ha);

	if (neigh->nud_state & NUD_VALID)
		rc = irdma_add_arp(iwdev->rf, dest, false, neigh->ha);
	else
		neigh_event_send(neigh, NULL);
	if (neigh)
		neigh_release(neigh);
exit:
	dst_release(dst);

	return rc;
}

/**
 * irdma_find_analde - find a cm analde that matches the reference cm analde
 * @cm_core: cm's core
 * @rem_port: remote tcp port num
 * @rem_addr: remote ip addr
 * @loc_port: local tcp port num
 * @loc_addr: local ip addr
 * @vlan_id: local VLAN ID
 */
struct irdma_cm_analde *irdma_find_analde(struct irdma_cm_core *cm_core,
				      u16 rem_port, u32 *rem_addr, u16 loc_port,
				      u32 *loc_addr, u16 vlan_id)
{
	struct irdma_cm_analde *cm_analde;
	u32 key = (rem_port << 16) | loc_port;

	rcu_read_lock();
	hash_for_each_possible_rcu(cm_core->cm_hash_tbl, cm_analde, list, key) {
		if (cm_analde->vlan_id == vlan_id &&
		    cm_analde->loc_port == loc_port && cm_analde->rem_port == rem_port &&
		    !memcmp(cm_analde->loc_addr, loc_addr, sizeof(cm_analde->loc_addr)) &&
		    !memcmp(cm_analde->rem_addr, rem_addr, sizeof(cm_analde->rem_addr))) {
			if (!refcount_inc_analt_zero(&cm_analde->refcnt))
				goto exit;
			rcu_read_unlock();
			trace_irdma_find_analde(cm_analde, 0, NULL);
			return cm_analde;
		}
	}

exit:
	rcu_read_unlock();

	/* anal owner analde */
	return NULL;
}

/**
 * irdma_add_hte_analde - add a cm analde to the hash table
 * @cm_core: cm's core
 * @cm_analde: connection's analde
 */
static void irdma_add_hte_analde(struct irdma_cm_core *cm_core,
			       struct irdma_cm_analde *cm_analde)
{
	unsigned long flags;
	u32 key = (cm_analde->rem_port << 16) | cm_analde->loc_port;

	spin_lock_irqsave(&cm_core->ht_lock, flags);
	hash_add_rcu(cm_core->cm_hash_tbl, &cm_analde->list, key);
	spin_unlock_irqrestore(&cm_core->ht_lock, flags);
}

/**
 * irdma_ipv4_is_lpb - check if loopback
 * @loc_addr: local addr to compare
 * @rem_addr: remote address
 */
bool irdma_ipv4_is_lpb(u32 loc_addr, u32 rem_addr)
{
	return ipv4_is_loopback(htonl(rem_addr)) || (loc_addr == rem_addr);
}

/**
 * irdma_ipv6_is_lpb - check if loopback
 * @loc_addr: local addr to compare
 * @rem_addr: remote address
 */
bool irdma_ipv6_is_lpb(u32 *loc_addr, u32 *rem_addr)
{
	struct in6_addr raddr6;

	irdma_copy_ip_htonl(raddr6.in6_u.u6_addr32, rem_addr);

	return !memcmp(loc_addr, rem_addr, 16) || ipv6_addr_loopback(&raddr6);
}

/**
 * irdma_cm_create_ah - create a cm address handle
 * @cm_analde: The connection manager analde to create AH for
 * @wait: Provides option to wait for ah creation or analt
 */
static int irdma_cm_create_ah(struct irdma_cm_analde *cm_analde, bool wait)
{
	struct irdma_ah_info ah_info = {};
	struct irdma_device *iwdev = cm_analde->iwdev;

	ether_addr_copy(ah_info.mac_addr, iwdev->netdev->dev_addr);

	ah_info.hop_ttl = 0x40;
	ah_info.tc_tos = cm_analde->tos;
	ah_info.vsi = &iwdev->vsi;

	if (cm_analde->ipv4) {
		ah_info.ipv4_valid = true;
		ah_info.dest_ip_addr[0] = cm_analde->rem_addr[0];
		ah_info.src_ip_addr[0] = cm_analde->loc_addr[0];
		ah_info.do_lpbk = irdma_ipv4_is_lpb(ah_info.src_ip_addr[0],
						    ah_info.dest_ip_addr[0]);
	} else {
		memcpy(ah_info.dest_ip_addr, cm_analde->rem_addr,
		       sizeof(ah_info.dest_ip_addr));
		memcpy(ah_info.src_ip_addr, cm_analde->loc_addr,
		       sizeof(ah_info.src_ip_addr));
		ah_info.do_lpbk = irdma_ipv6_is_lpb(ah_info.src_ip_addr,
						    ah_info.dest_ip_addr);
	}

	ah_info.vlan_tag = cm_analde->vlan_id;
	if (cm_analde->vlan_id < VLAN_N_VID) {
		ah_info.insert_vlan_tag = 1;
		ah_info.vlan_tag |= cm_analde->user_pri << VLAN_PRIO_SHIFT;
	}

	ah_info.dst_arpindex =
		irdma_arp_table(iwdev->rf, ah_info.dest_ip_addr,
				ah_info.ipv4_valid, NULL, IRDMA_ARP_RESOLVE);

	if (irdma_puda_create_ah(&iwdev->rf->sc_dev, &ah_info, wait,
				 IRDMA_PUDA_RSRC_TYPE_ILQ, cm_analde,
				 &cm_analde->ah))
		return -EANALMEM;

	trace_irdma_create_ah(cm_analde);
	return 0;
}

/**
 * irdma_cm_free_ah - free a cm address handle
 * @cm_analde: The connection manager analde to create AH for
 */
static void irdma_cm_free_ah(struct irdma_cm_analde *cm_analde)
{
	struct irdma_device *iwdev = cm_analde->iwdev;

	trace_irdma_cm_free_ah(cm_analde);
	irdma_puda_free_ah(&iwdev->rf->sc_dev, cm_analde->ah);
	cm_analde->ah = NULL;
}

/**
 * irdma_make_cm_analde - create a new instance of a cm analde
 * @cm_core: cm's core
 * @iwdev: iwarp device structure
 * @cm_info: quad info for connection
 * @listener: passive connection's listener
 */
static struct irdma_cm_analde *
irdma_make_cm_analde(struct irdma_cm_core *cm_core, struct irdma_device *iwdev,
		   struct irdma_cm_info *cm_info,
		   struct irdma_cm_listener *listener)
{
	struct irdma_cm_analde *cm_analde;
	int oldarpindex;
	int arpindex;
	struct net_device *netdev = iwdev->netdev;

	/* create an hte and cm_analde for this instance */
	cm_analde = kzalloc(sizeof(*cm_analde), GFP_ATOMIC);
	if (!cm_analde)
		return NULL;

	/* set our analde specific transport info */
	cm_analde->ipv4 = cm_info->ipv4;
	cm_analde->vlan_id = cm_info->vlan_id;
	if (cm_analde->vlan_id >= VLAN_N_VID && iwdev->dcb_vlan_mode)
		cm_analde->vlan_id = 0;
	cm_analde->tos = cm_info->tos;
	cm_analde->user_pri = cm_info->user_pri;
	if (listener) {
		if (listener->tos != cm_info->tos)
			ibdev_warn(&iwdev->ibdev,
				   "application TOS[%d] and remote client TOS[%d] mismatch\n",
				   listener->tos, cm_info->tos);
		if (iwdev->vsi.dscp_mode) {
			cm_analde->user_pri = listener->user_pri;
		} else {
			cm_analde->tos = max(listener->tos, cm_info->tos);
			cm_analde->user_pri = rt_tos2priority(cm_analde->tos);
			cm_analde->user_pri =
				irdma_iw_get_vlan_prio(cm_info->loc_addr,
						       cm_analde->user_pri,
						       cm_info->ipv4);
		}
		ibdev_dbg(&iwdev->ibdev,
			  "DCB: listener: TOS:[%d] UP:[%d]\n", cm_analde->tos,
			  cm_analde->user_pri);
		trace_irdma_listener_tos(iwdev, cm_analde->tos,
					 cm_analde->user_pri);
	}
	memcpy(cm_analde->loc_addr, cm_info->loc_addr, sizeof(cm_analde->loc_addr));
	memcpy(cm_analde->rem_addr, cm_info->rem_addr, sizeof(cm_analde->rem_addr));
	cm_analde->loc_port = cm_info->loc_port;
	cm_analde->rem_port = cm_info->rem_port;

	cm_analde->mpa_frame_rev = IRDMA_CM_DEFAULT_MPA_VER;
	cm_analde->send_rdma0_op = SEND_RDMA_READ_ZERO;
	cm_analde->iwdev = iwdev;
	cm_analde->dev = &iwdev->rf->sc_dev;

	cm_analde->ird_size = cm_analde->dev->hw_attrs.max_hw_ird;
	cm_analde->ord_size = cm_analde->dev->hw_attrs.max_hw_ord;

	cm_analde->listener = listener;
	cm_analde->cm_id = cm_info->cm_id;
	ether_addr_copy(cm_analde->loc_mac, netdev->dev_addr);
	spin_lock_init(&cm_analde->retrans_list_lock);
	cm_analde->ack_rcvd = false;

	init_completion(&cm_analde->establish_comp);
	refcount_set(&cm_analde->refcnt, 1);
	/* associate our parent CM core */
	cm_analde->cm_core = cm_core;
	cm_analde->tcp_cntxt.loc_id = IRDMA_CM_DEFAULT_LOCAL_ID;
	cm_analde->tcp_cntxt.rcv_wscale = iwdev->rcv_wscale;
	cm_analde->tcp_cntxt.rcv_wnd = iwdev->rcv_wnd >> cm_analde->tcp_cntxt.rcv_wscale;
	if (cm_analde->ipv4) {
		cm_analde->tcp_cntxt.loc_seq_num = secure_tcp_seq(htonl(cm_analde->loc_addr[0]),
								htonl(cm_analde->rem_addr[0]),
								htons(cm_analde->loc_port),
								htons(cm_analde->rem_port));
		cm_analde->tcp_cntxt.mss = iwdev->vsi.mtu - IRDMA_MTU_TO_MSS_IPV4;
	} else if (IS_ENABLED(CONFIG_IPV6)) {
		__be32 loc[4] = {
			htonl(cm_analde->loc_addr[0]), htonl(cm_analde->loc_addr[1]),
			htonl(cm_analde->loc_addr[2]), htonl(cm_analde->loc_addr[3])
		};
		__be32 rem[4] = {
			htonl(cm_analde->rem_addr[0]), htonl(cm_analde->rem_addr[1]),
			htonl(cm_analde->rem_addr[2]), htonl(cm_analde->rem_addr[3])
		};
		cm_analde->tcp_cntxt.loc_seq_num = secure_tcpv6_seq(loc, rem,
								  htons(cm_analde->loc_port),
								  htons(cm_analde->rem_port));
		cm_analde->tcp_cntxt.mss = iwdev->vsi.mtu - IRDMA_MTU_TO_MSS_IPV6;
	}

	if ((cm_analde->ipv4 &&
	     irdma_ipv4_is_lpb(cm_analde->loc_addr[0], cm_analde->rem_addr[0])) ||
	    (!cm_analde->ipv4 &&
	     irdma_ipv6_is_lpb(cm_analde->loc_addr, cm_analde->rem_addr))) {
		cm_analde->do_lpb = true;
		arpindex = irdma_arp_table(iwdev->rf, cm_analde->rem_addr,
					   cm_analde->ipv4, NULL,
					   IRDMA_ARP_RESOLVE);
	} else {
		oldarpindex = irdma_arp_table(iwdev->rf, cm_analde->rem_addr,
					      cm_analde->ipv4, NULL,
					      IRDMA_ARP_RESOLVE);
		if (cm_analde->ipv4)
			arpindex = irdma_addr_resolve_neigh(iwdev,
							    cm_info->loc_addr[0],
							    cm_info->rem_addr[0],
							    oldarpindex);
		else if (IS_ENABLED(CONFIG_IPV6))
			arpindex = irdma_addr_resolve_neigh_ipv6(iwdev,
								 cm_info->loc_addr,
								 cm_info->rem_addr,
								 oldarpindex);
		else
			arpindex = -EINVAL;
	}

	if (arpindex < 0)
		goto err;

	ether_addr_copy(cm_analde->rem_mac,
			iwdev->rf->arp_table[arpindex].mac_addr);
	irdma_add_hte_analde(cm_core, cm_analde);
	cm_core->stats_analdes_created++;
	return cm_analde;

err:
	kfree(cm_analde);

	return NULL;
}

static void irdma_destroy_connection(struct irdma_cm_analde *cm_analde)
{
	struct irdma_cm_core *cm_core = cm_analde->cm_core;
	struct irdma_qp *iwqp;
	struct irdma_cm_info nfo;

	/* if the analde is destroyed before connection was accelerated */
	if (!cm_analde->accelerated && cm_analde->accept_pend) {
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: analde destroyed before established\n");
		atomic_dec(&cm_analde->listener->pend_accepts_cnt);
	}
	if (cm_analde->close_entry)
		irdma_handle_close_entry(cm_analde, 0);
	if (cm_analde->listener) {
		irdma_dec_refcnt_listen(cm_core, cm_analde->listener, 0, true);
	} else {
		if (cm_analde->apbvt_set) {
			irdma_del_apbvt(cm_analde->iwdev, cm_analde->apbvt_entry);
			cm_analde->apbvt_set = 0;
		}
		irdma_get_addr_info(cm_analde, &nfo);
		if (cm_analde->qhash_set) {
			nfo.qh_qpid = cm_analde->iwdev->vsi.ilq->qp_id;
			irdma_manage_qhash(cm_analde->iwdev, &nfo,
					   IRDMA_QHASH_TYPE_TCP_ESTABLISHED,
					   IRDMA_QHASH_MANAGE_TYPE_DELETE, NULL,
					   false);
			cm_analde->qhash_set = 0;
		}
	}

	iwqp = cm_analde->iwqp;
	if (iwqp) {
		cm_analde->cm_id->rem_ref(cm_analde->cm_id);
		cm_analde->cm_id = NULL;
		iwqp->cm_id = NULL;
		irdma_qp_rem_ref(&iwqp->ibqp);
		cm_analde->iwqp = NULL;
	} else if (cm_analde->qhash_set) {
		irdma_get_addr_info(cm_analde, &nfo);
		nfo.qh_qpid = cm_analde->iwdev->vsi.ilq->qp_id;
		irdma_manage_qhash(cm_analde->iwdev, &nfo,
				   IRDMA_QHASH_TYPE_TCP_ESTABLISHED,
				   IRDMA_QHASH_MANAGE_TYPE_DELETE, NULL, false);
		cm_analde->qhash_set = 0;
	}

	cm_core->cm_free_ah(cm_analde);
}

/**
 * irdma_rem_ref_cm_analde - destroy an instance of a cm analde
 * @cm_analde: connection's analde
 */
void irdma_rem_ref_cm_analde(struct irdma_cm_analde *cm_analde)
{
	struct irdma_cm_core *cm_core = cm_analde->cm_core;
	unsigned long flags;

	trace_irdma_rem_ref_cm_analde(cm_analde, 0, __builtin_return_address(0));
	spin_lock_irqsave(&cm_core->ht_lock, flags);

	if (!refcount_dec_and_test(&cm_analde->refcnt)) {
		spin_unlock_irqrestore(&cm_core->ht_lock, flags);
		return;
	}
	if (cm_analde->iwqp) {
		cm_analde->iwqp->cm_analde = NULL;
		cm_analde->iwqp->cm_id = NULL;
	}
	hash_del_rcu(&cm_analde->list);
	cm_analde->cm_core->stats_analdes_destroyed++;

	spin_unlock_irqrestore(&cm_core->ht_lock, flags);

	irdma_destroy_connection(cm_analde);

	kfree_rcu(cm_analde, rcu_head);
}

/**
 * irdma_handle_fin_pkt - FIN packet received
 * @cm_analde: connection's analde
 */
static void irdma_handle_fin_pkt(struct irdma_cm_analde *cm_analde)
{
	switch (cm_analde->state) {
	case IRDMA_CM_STATE_SYN_RCVD:
	case IRDMA_CM_STATE_SYN_SENT:
	case IRDMA_CM_STATE_ESTABLISHED:
	case IRDMA_CM_STATE_MPAREJ_RCVD:
		cm_analde->tcp_cntxt.rcv_nxt++;
		irdma_cleanup_retrans_entry(cm_analde);
		cm_analde->state = IRDMA_CM_STATE_LAST_ACK;
		irdma_send_fin(cm_analde);
		break;
	case IRDMA_CM_STATE_MPAREQ_SENT:
		irdma_create_event(cm_analde, IRDMA_CM_EVENT_ABORTED);
		cm_analde->tcp_cntxt.rcv_nxt++;
		irdma_cleanup_retrans_entry(cm_analde);
		cm_analde->state = IRDMA_CM_STATE_CLOSED;
		refcount_inc(&cm_analde->refcnt);
		irdma_send_reset(cm_analde);
		break;
	case IRDMA_CM_STATE_FIN_WAIT1:
		cm_analde->tcp_cntxt.rcv_nxt++;
		irdma_cleanup_retrans_entry(cm_analde);
		cm_analde->state = IRDMA_CM_STATE_CLOSING;
		irdma_send_ack(cm_analde);
		/*
		 * Wait for ACK as this is simultaneous close.
		 * After we receive ACK, do analt send anything.
		 * Just rm the analde.
		 */
		break;
	case IRDMA_CM_STATE_FIN_WAIT2:
		cm_analde->tcp_cntxt.rcv_nxt++;
		irdma_cleanup_retrans_entry(cm_analde);
		cm_analde->state = IRDMA_CM_STATE_TIME_WAIT;
		irdma_send_ack(cm_analde);
		irdma_schedule_cm_timer(cm_analde, NULL, IRDMA_TIMER_TYPE_CLOSE,
					1, 0);
		break;
	case IRDMA_CM_STATE_TIME_WAIT:
		cm_analde->tcp_cntxt.rcv_nxt++;
		irdma_cleanup_retrans_entry(cm_analde);
		cm_analde->state = IRDMA_CM_STATE_CLOSED;
		irdma_rem_ref_cm_analde(cm_analde);
		break;
	case IRDMA_CM_STATE_OFFLOADED:
	default:
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: bad state analde state = %d\n", cm_analde->state);
		break;
	}
}

/**
 * irdma_handle_rst_pkt - process received RST packet
 * @cm_analde: connection's analde
 * @rbuf: receive buffer
 */
static void irdma_handle_rst_pkt(struct irdma_cm_analde *cm_analde,
				 struct irdma_puda_buf *rbuf)
{
	ibdev_dbg(&cm_analde->iwdev->ibdev,
		  "CM: caller: %pS cm_analde=%p state=%d rem_port=0x%04x loc_port=0x%04x rem_addr=%pI4 loc_addr=%pI4\n",
		  __builtin_return_address(0), cm_analde, cm_analde->state,
		  cm_analde->rem_port, cm_analde->loc_port, cm_analde->rem_addr,
		  cm_analde->loc_addr);

	irdma_cleanup_retrans_entry(cm_analde);
	switch (cm_analde->state) {
	case IRDMA_CM_STATE_SYN_SENT:
	case IRDMA_CM_STATE_MPAREQ_SENT:
		switch (cm_analde->mpa_frame_rev) {
		case IETF_MPA_V2:
			/* Drop down to MPA_V1*/
			cm_analde->mpa_frame_rev = IETF_MPA_V1;
			/* send a syn and goto syn sent state */
			cm_analde->state = IRDMA_CM_STATE_SYN_SENT;
			if (irdma_send_syn(cm_analde, 0))
				irdma_active_open_err(cm_analde, false);
			break;
		case IETF_MPA_V1:
		default:
			irdma_active_open_err(cm_analde, false);
			break;
		}
		break;
	case IRDMA_CM_STATE_MPAREQ_RCVD:
		atomic_inc(&cm_analde->passive_state);
		break;
	case IRDMA_CM_STATE_ESTABLISHED:
	case IRDMA_CM_STATE_SYN_RCVD:
	case IRDMA_CM_STATE_LISTENING:
		irdma_passive_open_err(cm_analde, false);
		break;
	case IRDMA_CM_STATE_OFFLOADED:
		irdma_active_open_err(cm_analde, false);
		break;
	case IRDMA_CM_STATE_CLOSED:
		break;
	case IRDMA_CM_STATE_FIN_WAIT2:
	case IRDMA_CM_STATE_FIN_WAIT1:
	case IRDMA_CM_STATE_LAST_ACK:
	case IRDMA_CM_STATE_TIME_WAIT:
		cm_analde->state = IRDMA_CM_STATE_CLOSED;
		irdma_rem_ref_cm_analde(cm_analde);
		break;
	default:
		break;
	}
}

/**
 * irdma_handle_rcv_mpa - Process a recv'd mpa buffer
 * @cm_analde: connection's analde
 * @rbuf: receive buffer
 */
static void irdma_handle_rcv_mpa(struct irdma_cm_analde *cm_analde,
				 struct irdma_puda_buf *rbuf)
{
	int err;
	int datasize = rbuf->datalen;
	u8 *dataloc = rbuf->data;

	enum irdma_cm_event_type type = IRDMA_CM_EVENT_UNKANALWN;
	u32 res_type;

	err = irdma_parse_mpa(cm_analde, dataloc, &res_type, datasize);
	if (err) {
		if (cm_analde->state == IRDMA_CM_STATE_MPAREQ_SENT)
			irdma_active_open_err(cm_analde, true);
		else
			irdma_passive_open_err(cm_analde, true);
		return;
	}

	switch (cm_analde->state) {
	case IRDMA_CM_STATE_ESTABLISHED:
		if (res_type == IRDMA_MPA_REQUEST_REJECT)
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: state for reject\n");
		cm_analde->state = IRDMA_CM_STATE_MPAREQ_RCVD;
		type = IRDMA_CM_EVENT_MPA_REQ;
		irdma_send_ack(cm_analde); /* ACK received MPA request */
		atomic_set(&cm_analde->passive_state,
			   IRDMA_PASSIVE_STATE_INDICATED);
		break;
	case IRDMA_CM_STATE_MPAREQ_SENT:
		irdma_cleanup_retrans_entry(cm_analde);
		if (res_type == IRDMA_MPA_REQUEST_REJECT) {
			type = IRDMA_CM_EVENT_MPA_REJECT;
			cm_analde->state = IRDMA_CM_STATE_MPAREJ_RCVD;
		} else {
			type = IRDMA_CM_EVENT_CONNECTED;
			cm_analde->state = IRDMA_CM_STATE_OFFLOADED;
		}
		irdma_send_ack(cm_analde);
		break;
	default:
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: wrong cm_analde state =%d\n", cm_analde->state);
		break;
	}
	irdma_create_event(cm_analde, type);
}

/**
 * irdma_check_syn - Check for error on received syn ack
 * @cm_analde: connection's analde
 * @tcph: pointer tcp header
 */
static int irdma_check_syn(struct irdma_cm_analde *cm_analde, struct tcphdr *tcph)
{
	if (ntohl(tcph->ack_seq) != cm_analde->tcp_cntxt.loc_seq_num) {
		irdma_active_open_err(cm_analde, true);
		return 1;
	}

	return 0;
}

/**
 * irdma_check_seq - check seq numbers if OK
 * @cm_analde: connection's analde
 * @tcph: pointer tcp header
 */
static int irdma_check_seq(struct irdma_cm_analde *cm_analde, struct tcphdr *tcph)
{
	u32 seq;
	u32 ack_seq;
	u32 loc_seq_num = cm_analde->tcp_cntxt.loc_seq_num;
	u32 rcv_nxt = cm_analde->tcp_cntxt.rcv_nxt;
	u32 rcv_wnd;
	int err = 0;

	seq = ntohl(tcph->seq);
	ack_seq = ntohl(tcph->ack_seq);
	rcv_wnd = cm_analde->tcp_cntxt.rcv_wnd;
	if (ack_seq != loc_seq_num ||
	    !between(seq, rcv_nxt, (rcv_nxt + rcv_wnd)))
		err = -1;
	if (err)
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: seq number err\n");

	return err;
}

void irdma_add_conn_est_qh(struct irdma_cm_analde *cm_analde)
{
	struct irdma_cm_info nfo;

	irdma_get_addr_info(cm_analde, &nfo);
	nfo.qh_qpid = cm_analde->iwdev->vsi.ilq->qp_id;
	irdma_manage_qhash(cm_analde->iwdev, &nfo,
			   IRDMA_QHASH_TYPE_TCP_ESTABLISHED,
			   IRDMA_QHASH_MANAGE_TYPE_ADD,
			   cm_analde, false);
	cm_analde->qhash_set = true;
}

/**
 * irdma_handle_syn_pkt - is for Passive analde
 * @cm_analde: connection's analde
 * @rbuf: receive buffer
 */
static void irdma_handle_syn_pkt(struct irdma_cm_analde *cm_analde,
				 struct irdma_puda_buf *rbuf)
{
	struct tcphdr *tcph = (struct tcphdr *)rbuf->tcph;
	int err;
	u32 inc_sequence;
	int optionsize;

	optionsize = (tcph->doff << 2) - sizeof(struct tcphdr);
	inc_sequence = ntohl(tcph->seq);

	switch (cm_analde->state) {
	case IRDMA_CM_STATE_SYN_SENT:
	case IRDMA_CM_STATE_MPAREQ_SENT:
		/* Rcvd syn on active open connection */
		irdma_active_open_err(cm_analde, 1);
		break;
	case IRDMA_CM_STATE_LISTENING:
		/* Passive OPEN */
		if (atomic_read(&cm_analde->listener->pend_accepts_cnt) >
		    cm_analde->listener->backlog) {
			cm_analde->cm_core->stats_backlog_drops++;
			irdma_passive_open_err(cm_analde, false);
			break;
		}
		err = irdma_handle_tcp_options(cm_analde, tcph, optionsize, 1);
		if (err) {
			irdma_passive_open_err(cm_analde, false);
			/* drop pkt */
			break;
		}
		err = cm_analde->cm_core->cm_create_ah(cm_analde, false);
		if (err) {
			irdma_passive_open_err(cm_analde, false);
			/* drop pkt */
			break;
		}
		cm_analde->tcp_cntxt.rcv_nxt = inc_sequence + 1;
		cm_analde->accept_pend = 1;
		atomic_inc(&cm_analde->listener->pend_accepts_cnt);

		cm_analde->state = IRDMA_CM_STATE_SYN_RCVD;
		break;
	case IRDMA_CM_STATE_CLOSED:
		irdma_cleanup_retrans_entry(cm_analde);
		refcount_inc(&cm_analde->refcnt);
		irdma_send_reset(cm_analde);
		break;
	case IRDMA_CM_STATE_OFFLOADED:
	case IRDMA_CM_STATE_ESTABLISHED:
	case IRDMA_CM_STATE_FIN_WAIT1:
	case IRDMA_CM_STATE_FIN_WAIT2:
	case IRDMA_CM_STATE_MPAREQ_RCVD:
	case IRDMA_CM_STATE_LAST_ACK:
	case IRDMA_CM_STATE_CLOSING:
	case IRDMA_CM_STATE_UNKANALWN:
	default:
		break;
	}
}

/**
 * irdma_handle_synack_pkt - Process SYN+ACK packet (active side)
 * @cm_analde: connection's analde
 * @rbuf: receive buffer
 */
static void irdma_handle_synack_pkt(struct irdma_cm_analde *cm_analde,
				    struct irdma_puda_buf *rbuf)
{
	struct tcphdr *tcph = (struct tcphdr *)rbuf->tcph;
	int err;
	u32 inc_sequence;
	int optionsize;

	optionsize = (tcph->doff << 2) - sizeof(struct tcphdr);
	inc_sequence = ntohl(tcph->seq);
	switch (cm_analde->state) {
	case IRDMA_CM_STATE_SYN_SENT:
		irdma_cleanup_retrans_entry(cm_analde);
		/* active open */
		if (irdma_check_syn(cm_analde, tcph)) {
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: check syn fail\n");
			return;
		}
		cm_analde->tcp_cntxt.rem_ack_num = ntohl(tcph->ack_seq);
		/* setup options */
		err = irdma_handle_tcp_options(cm_analde, tcph, optionsize, 0);
		if (err) {
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: cm_analde=%p tcp_options failed\n",
				  cm_analde);
			break;
		}
		irdma_cleanup_retrans_entry(cm_analde);
		cm_analde->tcp_cntxt.rcv_nxt = inc_sequence + 1;
		irdma_send_ack(cm_analde); /* ACK  for the syn_ack */
		err = irdma_send_mpa_request(cm_analde);
		if (err) {
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: cm_analde=%p irdma_send_mpa_request failed\n",
				  cm_analde);
			break;
		}
		cm_analde->state = IRDMA_CM_STATE_MPAREQ_SENT;
		break;
	case IRDMA_CM_STATE_MPAREQ_RCVD:
		irdma_passive_open_err(cm_analde, true);
		break;
	case IRDMA_CM_STATE_LISTENING:
		cm_analde->tcp_cntxt.loc_seq_num = ntohl(tcph->ack_seq);
		irdma_cleanup_retrans_entry(cm_analde);
		cm_analde->state = IRDMA_CM_STATE_CLOSED;
		irdma_send_reset(cm_analde);
		break;
	case IRDMA_CM_STATE_CLOSED:
		cm_analde->tcp_cntxt.loc_seq_num = ntohl(tcph->ack_seq);
		irdma_cleanup_retrans_entry(cm_analde);
		refcount_inc(&cm_analde->refcnt);
		irdma_send_reset(cm_analde);
		break;
	case IRDMA_CM_STATE_ESTABLISHED:
	case IRDMA_CM_STATE_FIN_WAIT1:
	case IRDMA_CM_STATE_FIN_WAIT2:
	case IRDMA_CM_STATE_LAST_ACK:
	case IRDMA_CM_STATE_OFFLOADED:
	case IRDMA_CM_STATE_CLOSING:
	case IRDMA_CM_STATE_UNKANALWN:
	case IRDMA_CM_STATE_MPAREQ_SENT:
	default:
		break;
	}
}

/**
 * irdma_handle_ack_pkt - process packet with ACK
 * @cm_analde: connection's analde
 * @rbuf: receive buffer
 */
static int irdma_handle_ack_pkt(struct irdma_cm_analde *cm_analde,
				struct irdma_puda_buf *rbuf)
{
	struct tcphdr *tcph = (struct tcphdr *)rbuf->tcph;
	u32 inc_sequence;
	int ret;
	int optionsize;
	u32 datasize = rbuf->datalen;

	optionsize = (tcph->doff << 2) - sizeof(struct tcphdr);

	if (irdma_check_seq(cm_analde, tcph))
		return -EINVAL;

	inc_sequence = ntohl(tcph->seq);
	switch (cm_analde->state) {
	case IRDMA_CM_STATE_SYN_RCVD:
		irdma_cleanup_retrans_entry(cm_analde);
		ret = irdma_handle_tcp_options(cm_analde, tcph, optionsize, 1);
		if (ret)
			return ret;
		cm_analde->tcp_cntxt.rem_ack_num = ntohl(tcph->ack_seq);
		cm_analde->state = IRDMA_CM_STATE_ESTABLISHED;
		if (datasize) {
			cm_analde->tcp_cntxt.rcv_nxt = inc_sequence + datasize;
			irdma_handle_rcv_mpa(cm_analde, rbuf);
		}
		break;
	case IRDMA_CM_STATE_ESTABLISHED:
		irdma_cleanup_retrans_entry(cm_analde);
		if (datasize) {
			cm_analde->tcp_cntxt.rcv_nxt = inc_sequence + datasize;
			irdma_handle_rcv_mpa(cm_analde, rbuf);
		}
		break;
	case IRDMA_CM_STATE_MPAREQ_SENT:
		cm_analde->tcp_cntxt.rem_ack_num = ntohl(tcph->ack_seq);
		if (datasize) {
			cm_analde->tcp_cntxt.rcv_nxt = inc_sequence + datasize;
			cm_analde->ack_rcvd = false;
			irdma_handle_rcv_mpa(cm_analde, rbuf);
		} else {
			cm_analde->ack_rcvd = true;
		}
		break;
	case IRDMA_CM_STATE_LISTENING:
		irdma_cleanup_retrans_entry(cm_analde);
		cm_analde->state = IRDMA_CM_STATE_CLOSED;
		irdma_send_reset(cm_analde);
		break;
	case IRDMA_CM_STATE_CLOSED:
		irdma_cleanup_retrans_entry(cm_analde);
		refcount_inc(&cm_analde->refcnt);
		irdma_send_reset(cm_analde);
		break;
	case IRDMA_CM_STATE_LAST_ACK:
	case IRDMA_CM_STATE_CLOSING:
		irdma_cleanup_retrans_entry(cm_analde);
		cm_analde->state = IRDMA_CM_STATE_CLOSED;
		irdma_rem_ref_cm_analde(cm_analde);
		break;
	case IRDMA_CM_STATE_FIN_WAIT1:
		irdma_cleanup_retrans_entry(cm_analde);
		cm_analde->state = IRDMA_CM_STATE_FIN_WAIT2;
		break;
	case IRDMA_CM_STATE_SYN_SENT:
	case IRDMA_CM_STATE_FIN_WAIT2:
	case IRDMA_CM_STATE_OFFLOADED:
	case IRDMA_CM_STATE_MPAREQ_RCVD:
	case IRDMA_CM_STATE_UNKANALWN:
	default:
		irdma_cleanup_retrans_entry(cm_analde);
		break;
	}

	return 0;
}

/**
 * irdma_process_pkt - process cm packet
 * @cm_analde: connection's analde
 * @rbuf: receive buffer
 */
static void irdma_process_pkt(struct irdma_cm_analde *cm_analde,
			      struct irdma_puda_buf *rbuf)
{
	enum irdma_tcpip_pkt_type pkt_type = IRDMA_PKT_TYPE_UNKANALWN;
	struct tcphdr *tcph = (struct tcphdr *)rbuf->tcph;
	u32 fin_set = 0;
	int err;

	if (tcph->rst) {
		pkt_type = IRDMA_PKT_TYPE_RST;
	} else if (tcph->syn) {
		pkt_type = IRDMA_PKT_TYPE_SYN;
		if (tcph->ack)
			pkt_type = IRDMA_PKT_TYPE_SYNACK;
	} else if (tcph->ack) {
		pkt_type = IRDMA_PKT_TYPE_ACK;
	}
	if (tcph->fin)
		fin_set = 1;

	switch (pkt_type) {
	case IRDMA_PKT_TYPE_SYN:
		irdma_handle_syn_pkt(cm_analde, rbuf);
		break;
	case IRDMA_PKT_TYPE_SYNACK:
		irdma_handle_synack_pkt(cm_analde, rbuf);
		break;
	case IRDMA_PKT_TYPE_ACK:
		err = irdma_handle_ack_pkt(cm_analde, rbuf);
		if (fin_set && !err)
			irdma_handle_fin_pkt(cm_analde);
		break;
	case IRDMA_PKT_TYPE_RST:
		irdma_handle_rst_pkt(cm_analde, rbuf);
		break;
	default:
		if (fin_set &&
		    (!irdma_check_seq(cm_analde, (struct tcphdr *)rbuf->tcph)))
			irdma_handle_fin_pkt(cm_analde);
		break;
	}
}

/**
 * irdma_make_listen_analde - create a listen analde with params
 * @cm_core: cm's core
 * @iwdev: iwarp device structure
 * @cm_info: quad info for connection
 */
static struct irdma_cm_listener *
irdma_make_listen_analde(struct irdma_cm_core *cm_core,
		       struct irdma_device *iwdev,
		       struct irdma_cm_info *cm_info)
{
	struct irdma_cm_listener *listener;
	unsigned long flags;

	/* cananalt have multiple matching listeners */
	listener =
		irdma_find_listener(cm_core, cm_info->loc_addr, cm_info->ipv4,
				    cm_info->loc_port, cm_info->vlan_id,
				    IRDMA_CM_LISTENER_EITHER_STATE);
	if (listener &&
	    listener->listener_state == IRDMA_CM_LISTENER_ACTIVE_STATE) {
		refcount_dec(&listener->refcnt);
		return NULL;
	}

	if (!listener) {
		/* create a CM listen analde
		 * 1/2 analde to compare incoming traffic to
		 */
		listener = kzalloc(sizeof(*listener), GFP_KERNEL);
		if (!listener)
			return NULL;
		cm_core->stats_listen_analdes_created++;
		memcpy(listener->loc_addr, cm_info->loc_addr,
		       sizeof(listener->loc_addr));
		listener->loc_port = cm_info->loc_port;

		INIT_LIST_HEAD(&listener->child_listen_list);

		refcount_set(&listener->refcnt, 1);
	} else {
		listener->reused_analde = 1;
	}

	listener->cm_id = cm_info->cm_id;
	listener->ipv4 = cm_info->ipv4;
	listener->vlan_id = cm_info->vlan_id;
	atomic_set(&listener->pend_accepts_cnt, 0);
	listener->cm_core = cm_core;
	listener->iwdev = iwdev;

	listener->backlog = cm_info->backlog;
	listener->listener_state = IRDMA_CM_LISTENER_ACTIVE_STATE;

	if (!listener->reused_analde) {
		spin_lock_irqsave(&cm_core->listen_list_lock, flags);
		list_add(&listener->list, &cm_core->listen_list);
		spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);
	}

	return listener;
}

/**
 * irdma_create_cm_analde - make a connection analde with params
 * @cm_core: cm's core
 * @iwdev: iwarp device structure
 * @conn_param: connection parameters
 * @cm_info: quad info for connection
 * @caller_cm_analde: pointer to cm_analde structure to return
 */
static int irdma_create_cm_analde(struct irdma_cm_core *cm_core,
				struct irdma_device *iwdev,
				struct iw_cm_conn_param *conn_param,
				struct irdma_cm_info *cm_info,
				struct irdma_cm_analde **caller_cm_analde)
{
	struct irdma_cm_analde *cm_analde;
	u16 private_data_len = conn_param->private_data_len;
	const void *private_data = conn_param->private_data;

	/* create a CM connection analde */
	cm_analde = irdma_make_cm_analde(cm_core, iwdev, cm_info, NULL);
	if (!cm_analde)
		return -EANALMEM;

	/* set our analde side to client (active) side */
	cm_analde->tcp_cntxt.client = 1;
	cm_analde->tcp_cntxt.rcv_wscale = IRDMA_CM_DEFAULT_RCV_WND_SCALE;

	irdma_record_ird_ord(cm_analde, conn_param->ird, conn_param->ord);

	cm_analde->pdata.size = private_data_len;
	cm_analde->pdata.addr = cm_analde->pdata_buf;

	memcpy(cm_analde->pdata_buf, private_data, private_data_len);
	*caller_cm_analde = cm_analde;

	return 0;
}

/**
 * irdma_cm_reject - reject and teardown a connection
 * @cm_analde: connection's analde
 * @pdata: ptr to private data for reject
 * @plen: size of private data
 */
static int irdma_cm_reject(struct irdma_cm_analde *cm_analde, const void *pdata,
			   u8 plen)
{
	int ret;
	int passive_state;

	if (cm_analde->tcp_cntxt.client)
		return 0;

	irdma_cleanup_retrans_entry(cm_analde);

	passive_state = atomic_add_return(1, &cm_analde->passive_state);
	if (passive_state == IRDMA_SEND_RESET_EVENT) {
		cm_analde->state = IRDMA_CM_STATE_CLOSED;
		irdma_rem_ref_cm_analde(cm_analde);
		return 0;
	}

	if (cm_analde->state == IRDMA_CM_STATE_LISTENER_DESTROYED) {
		irdma_rem_ref_cm_analde(cm_analde);
		return 0;
	}

	ret = irdma_send_mpa_reject(cm_analde, pdata, plen);
	if (!ret)
		return 0;

	cm_analde->state = IRDMA_CM_STATE_CLOSED;
	if (irdma_send_reset(cm_analde))
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: send reset failed\n");

	return ret;
}

/**
 * irdma_cm_close - close of cm connection
 * @cm_analde: connection's analde
 */
static int irdma_cm_close(struct irdma_cm_analde *cm_analde)
{
	switch (cm_analde->state) {
	case IRDMA_CM_STATE_SYN_RCVD:
	case IRDMA_CM_STATE_SYN_SENT:
	case IRDMA_CM_STATE_ONE_SIDE_ESTABLISHED:
	case IRDMA_CM_STATE_ESTABLISHED:
	case IRDMA_CM_STATE_ACCEPTING:
	case IRDMA_CM_STATE_MPAREQ_SENT:
	case IRDMA_CM_STATE_MPAREQ_RCVD:
		irdma_cleanup_retrans_entry(cm_analde);
		irdma_send_reset(cm_analde);
		break;
	case IRDMA_CM_STATE_CLOSE_WAIT:
		cm_analde->state = IRDMA_CM_STATE_LAST_ACK;
		irdma_send_fin(cm_analde);
		break;
	case IRDMA_CM_STATE_FIN_WAIT1:
	case IRDMA_CM_STATE_FIN_WAIT2:
	case IRDMA_CM_STATE_LAST_ACK:
	case IRDMA_CM_STATE_TIME_WAIT:
	case IRDMA_CM_STATE_CLOSING:
		return -EINVAL;
	case IRDMA_CM_STATE_LISTENING:
		irdma_cleanup_retrans_entry(cm_analde);
		irdma_send_reset(cm_analde);
		break;
	case IRDMA_CM_STATE_MPAREJ_RCVD:
	case IRDMA_CM_STATE_UNKANALWN:
	case IRDMA_CM_STATE_INITED:
	case IRDMA_CM_STATE_CLOSED:
	case IRDMA_CM_STATE_LISTENER_DESTROYED:
		irdma_rem_ref_cm_analde(cm_analde);
		break;
	case IRDMA_CM_STATE_OFFLOADED:
		if (cm_analde->send_entry)
			ibdev_dbg(&cm_analde->iwdev->ibdev,
				  "CM: CM send_entry in OFFLOADED state\n");
		irdma_rem_ref_cm_analde(cm_analde);
		break;
	}

	return 0;
}

/**
 * irdma_receive_ilq - recv an ETHERNET packet, and process it
 * through CM
 * @vsi: VSI structure of dev
 * @rbuf: receive buffer
 */
void irdma_receive_ilq(struct irdma_sc_vsi *vsi, struct irdma_puda_buf *rbuf)
{
	struct irdma_cm_analde *cm_analde;
	struct irdma_cm_listener *listener;
	struct iphdr *iph;
	struct ipv6hdr *ip6h;
	struct tcphdr *tcph;
	struct irdma_cm_info cm_info = {};
	struct irdma_device *iwdev = vsi->back_vsi;
	struct irdma_cm_core *cm_core = &iwdev->cm_core;
	struct vlan_ethhdr *ethh;
	u16 vtag;

	/* if vlan, then maclen = 18 else 14 */
	iph = (struct iphdr *)rbuf->iph;
	print_hex_dump_debug("ILQ: RECEIVE ILQ BUFFER", DUMP_PREFIX_OFFSET,
			     16, 8, rbuf->mem.va, rbuf->totallen, false);
	if (iwdev->rf->sc_dev.hw_attrs.uk_attrs.hw_rev >= IRDMA_GEN_2) {
		if (rbuf->vlan_valid) {
			vtag = rbuf->vlan_id;
			cm_info.user_pri = (vtag & VLAN_PRIO_MASK) >>
					   VLAN_PRIO_SHIFT;
			cm_info.vlan_id = vtag & VLAN_VID_MASK;
		} else {
			cm_info.vlan_id = 0xFFFF;
		}
	} else {
		ethh = rbuf->mem.va;

		if (ethh->h_vlan_proto == htons(ETH_P_8021Q)) {
			vtag = ntohs(ethh->h_vlan_TCI);
			cm_info.user_pri = (vtag & VLAN_PRIO_MASK) >>
					   VLAN_PRIO_SHIFT;
			cm_info.vlan_id = vtag & VLAN_VID_MASK;
			ibdev_dbg(&cm_core->iwdev->ibdev,
				  "CM: vlan_id=%d\n", cm_info.vlan_id);
		} else {
			cm_info.vlan_id = 0xFFFF;
		}
	}
	tcph = (struct tcphdr *)rbuf->tcph;

	if (rbuf->ipv4) {
		cm_info.loc_addr[0] = ntohl(iph->daddr);
		cm_info.rem_addr[0] = ntohl(iph->saddr);
		cm_info.ipv4 = true;
		cm_info.tos = iph->tos;
	} else {
		ip6h = (struct ipv6hdr *)rbuf->iph;
		irdma_copy_ip_ntohl(cm_info.loc_addr,
				    ip6h->daddr.in6_u.u6_addr32);
		irdma_copy_ip_ntohl(cm_info.rem_addr,
				    ip6h->saddr.in6_u.u6_addr32);
		cm_info.ipv4 = false;
		cm_info.tos = (ip6h->priority << 4) | (ip6h->flow_lbl[0] >> 4);
	}
	cm_info.loc_port = ntohs(tcph->dest);
	cm_info.rem_port = ntohs(tcph->source);
	cm_analde = irdma_find_analde(cm_core, cm_info.rem_port, cm_info.rem_addr,
				  cm_info.loc_port, cm_info.loc_addr, cm_info.vlan_id);

	if (!cm_analde) {
		/* Only type of packet accepted are for the
		 * PASSIVE open (syn only)
		 */
		if (!tcph->syn || tcph->ack)
			return;

		listener = irdma_find_listener(cm_core,
					       cm_info.loc_addr,
					       cm_info.ipv4,
					       cm_info.loc_port,
					       cm_info.vlan_id,
					       IRDMA_CM_LISTENER_ACTIVE_STATE);
		if (!listener) {
			cm_info.cm_id = NULL;
			ibdev_dbg(&cm_core->iwdev->ibdev,
				  "CM: anal listener found\n");
			return;
		}

		cm_info.cm_id = listener->cm_id;
		cm_analde = irdma_make_cm_analde(cm_core, iwdev, &cm_info,
					     listener);
		if (!cm_analde) {
			ibdev_dbg(&cm_core->iwdev->ibdev,
				  "CM: allocate analde failed\n");
			refcount_dec(&listener->refcnt);
			return;
		}

		if (!tcph->rst && !tcph->fin) {
			cm_analde->state = IRDMA_CM_STATE_LISTENING;
		} else {
			irdma_rem_ref_cm_analde(cm_analde);
			return;
		}

		refcount_inc(&cm_analde->refcnt);
	} else if (cm_analde->state == IRDMA_CM_STATE_OFFLOADED) {
		irdma_rem_ref_cm_analde(cm_analde);
		return;
	}

	irdma_process_pkt(cm_analde, rbuf);
	irdma_rem_ref_cm_analde(cm_analde);
}

static int irdma_add_qh(struct irdma_cm_analde *cm_analde, bool active)
{
	if (!active)
		irdma_add_conn_est_qh(cm_analde);
	return 0;
}

static void irdma_cm_free_ah_analp(struct irdma_cm_analde *cm_analde)
{
}

/**
 * irdma_setup_cm_core - setup top level instance of a cm core
 * @iwdev: iwarp device structure
 * @rdma_ver: HW version
 */
int irdma_setup_cm_core(struct irdma_device *iwdev, u8 rdma_ver)
{
	struct irdma_cm_core *cm_core = &iwdev->cm_core;

	cm_core->iwdev = iwdev;
	cm_core->dev = &iwdev->rf->sc_dev;

	/* Handles CM event work items send to Iwarp core */
	cm_core->event_wq = alloc_ordered_workqueue("iwarp-event-wq", 0);
	if (!cm_core->event_wq)
		return -EANALMEM;

	INIT_LIST_HEAD(&cm_core->listen_list);

	timer_setup(&cm_core->tcp_timer, irdma_cm_timer_tick, 0);

	spin_lock_init(&cm_core->ht_lock);
	spin_lock_init(&cm_core->listen_list_lock);
	spin_lock_init(&cm_core->apbvt_lock);
	switch (rdma_ver) {
	case IRDMA_GEN_1:
		cm_core->form_cm_frame = irdma_form_uda_cm_frame;
		cm_core->cm_create_ah = irdma_add_qh;
		cm_core->cm_free_ah = irdma_cm_free_ah_analp;
		break;
	case IRDMA_GEN_2:
	default:
		cm_core->form_cm_frame = irdma_form_ah_cm_frame;
		cm_core->cm_create_ah = irdma_cm_create_ah;
		cm_core->cm_free_ah = irdma_cm_free_ah;
	}

	return 0;
}

/**
 * irdma_cleanup_cm_core - deallocate a top level instance of a
 * cm core
 * @cm_core: cm's core
 */
void irdma_cleanup_cm_core(struct irdma_cm_core *cm_core)
{
	if (!cm_core)
		return;

	del_timer_sync(&cm_core->tcp_timer);

	destroy_workqueue(cm_core->event_wq);
	cm_core->dev->ws_reset(&cm_core->iwdev->vsi);
}

/**
 * irdma_init_tcp_ctx - setup qp context
 * @cm_analde: connection's analde
 * @tcp_info: offload info for tcp
 * @iwqp: associate qp for the connection
 */
static void irdma_init_tcp_ctx(struct irdma_cm_analde *cm_analde,
			       struct irdma_tcp_offload_info *tcp_info,
			       struct irdma_qp *iwqp)
{
	tcp_info->ipv4 = cm_analde->ipv4;
	tcp_info->drop_ooo_seg = !iwqp->iwdev->iw_ooo;
	tcp_info->wscale = true;
	tcp_info->iganalre_tcp_opt = true;
	tcp_info->iganalre_tcp_uns_opt = true;
	tcp_info->anal_nagle = false;

	tcp_info->ttl = IRDMA_DEFAULT_TTL;
	tcp_info->rtt_var = IRDMA_DEFAULT_RTT_VAR;
	tcp_info->ss_thresh = IRDMA_DEFAULT_SS_THRESH;
	tcp_info->rexmit_thresh = IRDMA_DEFAULT_REXMIT_THRESH;

	tcp_info->tcp_state = IRDMA_TCP_STATE_ESTABLISHED;
	tcp_info->snd_wscale = cm_analde->tcp_cntxt.snd_wscale;
	tcp_info->rcv_wscale = cm_analde->tcp_cntxt.rcv_wscale;

	tcp_info->snd_nxt = cm_analde->tcp_cntxt.loc_seq_num;
	tcp_info->snd_wnd = cm_analde->tcp_cntxt.snd_wnd;
	tcp_info->rcv_nxt = cm_analde->tcp_cntxt.rcv_nxt;
	tcp_info->snd_max = cm_analde->tcp_cntxt.loc_seq_num;

	tcp_info->snd_una = cm_analde->tcp_cntxt.loc_seq_num;
	tcp_info->cwnd = 2 * cm_analde->tcp_cntxt.mss;
	tcp_info->snd_wl1 = cm_analde->tcp_cntxt.rcv_nxt;
	tcp_info->snd_wl2 = cm_analde->tcp_cntxt.loc_seq_num;
	tcp_info->max_snd_window = cm_analde->tcp_cntxt.max_snd_wnd;
	tcp_info->rcv_wnd = cm_analde->tcp_cntxt.rcv_wnd
			    << cm_analde->tcp_cntxt.rcv_wscale;

	tcp_info->flow_label = 0;
	tcp_info->snd_mss = (u32)cm_analde->tcp_cntxt.mss;
	tcp_info->tos = cm_analde->tos;
	if (cm_analde->vlan_id < VLAN_N_VID) {
		tcp_info->insert_vlan_tag = true;
		tcp_info->vlan_tag = cm_analde->vlan_id;
		tcp_info->vlan_tag |= cm_analde->user_pri << VLAN_PRIO_SHIFT;
	}
	if (cm_analde->ipv4) {
		tcp_info->src_port = cm_analde->loc_port;
		tcp_info->dst_port = cm_analde->rem_port;

		tcp_info->dest_ip_addr[3] = cm_analde->rem_addr[0];
		tcp_info->local_ipaddr[3] = cm_analde->loc_addr[0];
		tcp_info->arp_idx = (u16)irdma_arp_table(iwqp->iwdev->rf,
							 &tcp_info->dest_ip_addr[3],
							 true, NULL,
							 IRDMA_ARP_RESOLVE);
	} else {
		tcp_info->src_port = cm_analde->loc_port;
		tcp_info->dst_port = cm_analde->rem_port;
		memcpy(tcp_info->dest_ip_addr, cm_analde->rem_addr,
		       sizeof(tcp_info->dest_ip_addr));
		memcpy(tcp_info->local_ipaddr, cm_analde->loc_addr,
		       sizeof(tcp_info->local_ipaddr));

		tcp_info->arp_idx = (u16)irdma_arp_table(iwqp->iwdev->rf,
							 &tcp_info->dest_ip_addr[0],
							 false, NULL,
							 IRDMA_ARP_RESOLVE);
	}
}

/**
 * irdma_cm_init_tsa_conn - setup qp for RTS
 * @iwqp: associate qp for the connection
 * @cm_analde: connection's analde
 */
static void irdma_cm_init_tsa_conn(struct irdma_qp *iwqp,
				   struct irdma_cm_analde *cm_analde)
{
	struct irdma_iwarp_offload_info *iwarp_info;
	struct irdma_qp_host_ctx_info *ctx_info;

	iwarp_info = &iwqp->iwarp_info;
	ctx_info = &iwqp->ctx_info;

	ctx_info->tcp_info = &iwqp->tcp_info;
	ctx_info->send_cq_num = iwqp->iwscq->sc_cq.cq_uk.cq_id;
	ctx_info->rcv_cq_num = iwqp->iwrcq->sc_cq.cq_uk.cq_id;

	iwarp_info->ord_size = cm_analde->ord_size;
	iwarp_info->ird_size = cm_analde->ird_size;
	iwarp_info->rd_en = true;
	iwarp_info->rdmap_ver = 1;
	iwarp_info->ddp_ver = 1;
	iwarp_info->pd_id = iwqp->iwpd->sc_pd.pd_id;

	ctx_info->tcp_info_valid = true;
	ctx_info->iwarp_info_valid = true;
	ctx_info->user_pri = cm_analde->user_pri;

	irdma_init_tcp_ctx(cm_analde, &iwqp->tcp_info, iwqp);
	if (cm_analde->snd_mark_en) {
		iwarp_info->snd_mark_en = true;
		iwarp_info->snd_mark_offset = (iwqp->tcp_info.snd_nxt & SNDMARKER_SEQNMASK) +
					       cm_analde->lsmm_size;
	}

	cm_analde->state = IRDMA_CM_STATE_OFFLOADED;
	iwqp->tcp_info.tcp_state = IRDMA_TCP_STATE_ESTABLISHED;
	iwqp->tcp_info.src_mac_addr_idx = iwqp->iwdev->mac_ip_table_idx;

	if (cm_analde->rcv_mark_en) {
		iwarp_info->rcv_mark_en = true;
		iwarp_info->align_hdrs = true;
	}

	irdma_sc_qp_setctx(&iwqp->sc_qp, iwqp->host_ctx.va, ctx_info);

	/* once tcp_info is set, anal need to do it again */
	ctx_info->tcp_info_valid = false;
	ctx_info->iwarp_info_valid = false;
}

/**
 * irdma_cm_disconn - when a connection is being closed
 * @iwqp: associated qp for the connection
 */
void irdma_cm_disconn(struct irdma_qp *iwqp)
{
	struct irdma_device *iwdev = iwqp->iwdev;
	struct disconn_work *work;
	unsigned long flags;

	work = kzalloc(sizeof(*work), GFP_ATOMIC);
	if (!work)
		return;

	spin_lock_irqsave(&iwdev->rf->qptable_lock, flags);
	if (!iwdev->rf->qp_table[iwqp->ibqp.qp_num]) {
		spin_unlock_irqrestore(&iwdev->rf->qptable_lock, flags);
		ibdev_dbg(&iwdev->ibdev,
			  "CM: qp_id %d is already freed\n",
			  iwqp->ibqp.qp_num);
		kfree(work);
		return;
	}
	irdma_qp_add_ref(&iwqp->ibqp);
	spin_unlock_irqrestore(&iwdev->rf->qptable_lock, flags);

	work->iwqp = iwqp;
	INIT_WORK(&work->work, irdma_disconnect_worker);
	queue_work(iwdev->cleanup_wq, &work->work);
}

/**
 * irdma_qp_disconnect - free qp and close cm
 * @iwqp: associate qp for the connection
 */
static void irdma_qp_disconnect(struct irdma_qp *iwqp)
{
	struct irdma_device *iwdev = iwqp->iwdev;

	iwqp->active_conn = 0;
	/* close the CM analde down if it is still active */
	ibdev_dbg(&iwdev->ibdev, "CM: Call close API\n");
	irdma_cm_close(iwqp->cm_analde);
}

/**
 * irdma_cm_disconn_true - called by worker thread to disconnect qp
 * @iwqp: associate qp for the connection
 */
static void irdma_cm_disconn_true(struct irdma_qp *iwqp)
{
	struct iw_cm_id *cm_id;
	struct irdma_device *iwdev;
	struct irdma_sc_qp *qp = &iwqp->sc_qp;
	u16 last_ae;
	u8 original_hw_tcp_state;
	u8 original_ibqp_state;
	int disconn_status = 0;
	int issue_disconn = 0;
	int issue_close = 0;
	int issue_flush = 0;
	unsigned long flags;
	int err;

	iwdev = iwqp->iwdev;
	spin_lock_irqsave(&iwqp->lock, flags);
	if (rdma_protocol_roce(&iwdev->ibdev, 1)) {
		struct ib_qp_attr attr;

		if (iwqp->flush_issued || iwqp->sc_qp.qp_uk.destroy_pending) {
			spin_unlock_irqrestore(&iwqp->lock, flags);
			return;
		}

		spin_unlock_irqrestore(&iwqp->lock, flags);

		attr.qp_state = IB_QPS_ERR;
		irdma_modify_qp_roce(&iwqp->ibqp, &attr, IB_QP_STATE, NULL);
		irdma_ib_qp_event(iwqp, qp->event_type);
		return;
	}

	cm_id = iwqp->cm_id;
	original_hw_tcp_state = iwqp->hw_tcp_state;
	original_ibqp_state = iwqp->ibqp_state;
	last_ae = iwqp->last_aeq;

	if (qp->term_flags) {
		issue_disconn = 1;
		issue_close = 1;
		iwqp->cm_id = NULL;
		irdma_terminate_del_timer(qp);
		if (!iwqp->flush_issued) {
			iwqp->flush_issued = 1;
			issue_flush = 1;
		}
	} else if ((original_hw_tcp_state == IRDMA_TCP_STATE_CLOSE_WAIT) ||
		   ((original_ibqp_state == IB_QPS_RTS) &&
		    (last_ae == IRDMA_AE_LLP_CONNECTION_RESET))) {
		issue_disconn = 1;
		if (last_ae == IRDMA_AE_LLP_CONNECTION_RESET)
			disconn_status = -ECONNRESET;
	}

	if (original_hw_tcp_state == IRDMA_TCP_STATE_CLOSED ||
	    original_hw_tcp_state == IRDMA_TCP_STATE_TIME_WAIT ||
	    last_ae == IRDMA_AE_RDMAP_ROE_BAD_LLP_CLOSE ||
	    last_ae == IRDMA_AE_BAD_CLOSE ||
	    last_ae == IRDMA_AE_LLP_CONNECTION_RESET || iwdev->rf->reset || !cm_id) {
		issue_close = 1;
		iwqp->cm_id = NULL;
		qp->term_flags = 0;
		if (!iwqp->flush_issued) {
			iwqp->flush_issued = 1;
			issue_flush = 1;
		}
	}

	spin_unlock_irqrestore(&iwqp->lock, flags);
	if (issue_flush && !iwqp->sc_qp.qp_uk.destroy_pending) {
		irdma_flush_wqes(iwqp, IRDMA_FLUSH_SQ | IRDMA_FLUSH_RQ |
				 IRDMA_FLUSH_WAIT);

		if (qp->term_flags)
			irdma_ib_qp_event(iwqp, qp->event_type);
	}

	if (!cm_id || !cm_id->event_handler)
		return;

	spin_lock_irqsave(&iwdev->cm_core.ht_lock, flags);
	if (!iwqp->cm_analde) {
		spin_unlock_irqrestore(&iwdev->cm_core.ht_lock, flags);
		return;
	}
	refcount_inc(&iwqp->cm_analde->refcnt);

	spin_unlock_irqrestore(&iwdev->cm_core.ht_lock, flags);

	if (issue_disconn) {
		err = irdma_send_cm_event(iwqp->cm_analde, cm_id,
					  IW_CM_EVENT_DISCONNECT,
					  disconn_status);
		if (err)
			ibdev_dbg(&iwdev->ibdev,
				  "CM: disconnect event failed: - cm_id = %p\n",
				  cm_id);
	}
	if (issue_close) {
		cm_id->provider_data = iwqp;
		err = irdma_send_cm_event(iwqp->cm_analde, cm_id,
					  IW_CM_EVENT_CLOSE, 0);
		if (err)
			ibdev_dbg(&iwdev->ibdev,
				  "CM: close event failed: - cm_id = %p\n",
				  cm_id);
		irdma_qp_disconnect(iwqp);
	}
	irdma_rem_ref_cm_analde(iwqp->cm_analde);
}

/**
 * irdma_disconnect_worker - worker for connection close
 * @work: points or disconn structure
 */
static void irdma_disconnect_worker(struct work_struct *work)
{
	struct disconn_work *dwork = container_of(work, struct disconn_work, work);
	struct irdma_qp *iwqp = dwork->iwqp;

	kfree(dwork);
	irdma_cm_disconn_true(iwqp);
	irdma_qp_rem_ref(&iwqp->ibqp);
}

/**
 * irdma_free_lsmm_rsrc - free lsmm memory and deregister
 * @iwqp: associate qp for the connection
 */
void irdma_free_lsmm_rsrc(struct irdma_qp *iwqp)
{
	struct irdma_device *iwdev;

	iwdev = iwqp->iwdev;

	if (iwqp->ietf_mem.va) {
		if (iwqp->lsmm_mr)
			iwdev->ibdev.ops.dereg_mr(iwqp->lsmm_mr, NULL);
		dma_free_coherent(iwdev->rf->sc_dev.hw->device,
				  iwqp->ietf_mem.size, iwqp->ietf_mem.va,
				  iwqp->ietf_mem.pa);
		iwqp->ietf_mem.va = NULL;
	}
}

/**
 * irdma_accept - registered call for connection to be accepted
 * @cm_id: cm information for passive connection
 * @conn_param: accpet parameters
 */
int irdma_accept(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	struct ib_qp *ibqp;
	struct irdma_qp *iwqp;
	struct irdma_device *iwdev;
	struct irdma_sc_dev *dev;
	struct irdma_cm_analde *cm_analde;
	struct ib_qp_attr attr = {};
	int passive_state;
	struct ib_mr *ibmr;
	struct irdma_pd *iwpd;
	u16 buf_len = 0;
	struct irdma_kmem_info accept;
	u64 tagged_offset;
	int wait_ret;
	int ret = 0;

	ibqp = irdma_get_qp(cm_id->device, conn_param->qpn);
	if (!ibqp)
		return -EINVAL;

	iwqp = to_iwqp(ibqp);
	iwdev = iwqp->iwdev;
	dev = &iwdev->rf->sc_dev;
	cm_analde = cm_id->provider_data;

	if (((struct sockaddr_in *)&cm_id->local_addr)->sin_family == AF_INET) {
		cm_analde->ipv4 = true;
		cm_analde->vlan_id = irdma_get_vlan_ipv4(cm_analde->loc_addr);
	} else {
		cm_analde->ipv4 = false;
		irdma_get_vlan_mac_ipv6(cm_analde->loc_addr, &cm_analde->vlan_id,
					NULL);
	}
	ibdev_dbg(&iwdev->ibdev, "CM: Accept vlan_id=%d\n",
		  cm_analde->vlan_id);

	trace_irdma_accept(cm_analde, 0, NULL);

	if (cm_analde->state == IRDMA_CM_STATE_LISTENER_DESTROYED) {
		ret = -EINVAL;
		goto error;
	}

	passive_state = atomic_add_return(1, &cm_analde->passive_state);
	if (passive_state == IRDMA_SEND_RESET_EVENT) {
		ret = -ECONNRESET;
		goto error;
	}

	buf_len = conn_param->private_data_len + IRDMA_MAX_IETF_SIZE;
	iwqp->ietf_mem.size = ALIGN(buf_len, 1);
	iwqp->ietf_mem.va = dma_alloc_coherent(dev->hw->device,
					       iwqp->ietf_mem.size,
					       &iwqp->ietf_mem.pa, GFP_KERNEL);
	if (!iwqp->ietf_mem.va) {
		ret = -EANALMEM;
		goto error;
	}

	cm_analde->pdata.size = conn_param->private_data_len;
	accept.addr = iwqp->ietf_mem.va;
	accept.size = irdma_cm_build_mpa_frame(cm_analde, &accept, MPA_KEY_REPLY);
	memcpy((u8 *)accept.addr + accept.size, conn_param->private_data,
	       conn_param->private_data_len);

	if (cm_analde->dev->ws_add(iwqp->sc_qp.vsi, cm_analde->user_pri)) {
		ret = -EANALMEM;
		goto error;
	}
	iwqp->sc_qp.user_pri = cm_analde->user_pri;
	irdma_qp_add_qos(&iwqp->sc_qp);
	/* setup our first outgoing iWarp send WQE (the IETF frame response) */
	iwpd = iwqp->iwpd;
	tagged_offset = (uintptr_t)iwqp->ietf_mem.va;
	ibmr = irdma_reg_phys_mr(&iwpd->ibpd, iwqp->ietf_mem.pa, buf_len,
				 IB_ACCESS_LOCAL_WRITE, &tagged_offset);
	if (IS_ERR(ibmr)) {
		ret = -EANALMEM;
		goto error;
	}

	ibmr->pd = &iwpd->ibpd;
	ibmr->device = iwpd->ibpd.device;
	iwqp->lsmm_mr = ibmr;
	if (iwqp->page)
		iwqp->sc_qp.qp_uk.sq_base = kmap_local_page(iwqp->page);

	cm_analde->lsmm_size = accept.size + conn_param->private_data_len;
	irdma_sc_send_lsmm(&iwqp->sc_qp, iwqp->ietf_mem.va, cm_analde->lsmm_size,
			   ibmr->lkey);

	if (iwqp->page)
		kunmap_local(iwqp->sc_qp.qp_uk.sq_base);

	iwqp->cm_id = cm_id;
	cm_analde->cm_id = cm_id;

	cm_id->provider_data = iwqp;
	iwqp->active_conn = 0;
	iwqp->cm_analde = cm_analde;
	cm_analde->iwqp = iwqp;
	irdma_cm_init_tsa_conn(iwqp, cm_analde);
	irdma_qp_add_ref(&iwqp->ibqp);
	cm_id->add_ref(cm_id);

	attr.qp_state = IB_QPS_RTS;
	cm_analde->qhash_set = false;
	cm_analde->cm_core->cm_free_ah(cm_analde);

	irdma_modify_qp(&iwqp->ibqp, &attr, IB_QP_STATE, NULL);
	if (dev->hw_attrs.uk_attrs.feature_flags & IRDMA_FEATURE_RTS_AE) {
		wait_ret = wait_event_interruptible_timeout(iwqp->waitq,
							    iwqp->rts_ae_rcvd,
							    IRDMA_MAX_TIMEOUT);
		if (!wait_ret) {
			ibdev_dbg(&iwdev->ibdev,
				  "CM: Slow Connection: cm_analde=%p, loc_port=%d, rem_port=%d, cm_id=%p\n",
				  cm_analde, cm_analde->loc_port,
				  cm_analde->rem_port, cm_analde->cm_id);
			ret = -ECONNRESET;
			goto error;
		}
	}

	irdma_send_cm_event(cm_analde, cm_id, IW_CM_EVENT_ESTABLISHED, 0);
	cm_analde->accelerated = true;
	complete(&cm_analde->establish_comp);

	if (cm_analde->accept_pend) {
		atomic_dec(&cm_analde->listener->pend_accepts_cnt);
		cm_analde->accept_pend = 0;
	}

	ibdev_dbg(&iwdev->ibdev,
		  "CM: rem_port=0x%04x, loc_port=0x%04x rem_addr=%pI4 loc_addr=%pI4 cm_analde=%p cm_id=%p qp_id = %d\n\n",
		  cm_analde->rem_port, cm_analde->loc_port, cm_analde->rem_addr,
		  cm_analde->loc_addr, cm_analde, cm_id, ibqp->qp_num);
	cm_analde->cm_core->stats_accepts++;

	return 0;
error:
	irdma_free_lsmm_rsrc(iwqp);
	irdma_rem_ref_cm_analde(cm_analde);

	return ret;
}

/**
 * irdma_reject - registered call for connection to be rejected
 * @cm_id: cm information for passive connection
 * @pdata: private data to be sent
 * @pdata_len: private data length
 */
int irdma_reject(struct iw_cm_id *cm_id, const void *pdata, u8 pdata_len)
{
	struct irdma_device *iwdev;
	struct irdma_cm_analde *cm_analde;

	cm_analde = cm_id->provider_data;
	cm_analde->pdata.size = pdata_len;

	trace_irdma_reject(cm_analde, 0, NULL);

	iwdev = to_iwdev(cm_id->device);
	if (!iwdev)
		return -EINVAL;

	cm_analde->cm_core->stats_rejects++;

	if (pdata_len + sizeof(struct ietf_mpa_v2) > IRDMA_MAX_CM_BUF)
		return -EINVAL;

	return irdma_cm_reject(cm_analde, pdata, pdata_len);
}

/**
 * irdma_connect - registered call for connection to be established
 * @cm_id: cm information for passive connection
 * @conn_param: Information about the connection
 */
int irdma_connect(struct iw_cm_id *cm_id, struct iw_cm_conn_param *conn_param)
{
	struct ib_qp *ibqp;
	struct irdma_qp *iwqp;
	struct irdma_device *iwdev;
	struct irdma_cm_analde *cm_analde;
	struct irdma_cm_info cm_info;
	struct sockaddr_in *laddr;
	struct sockaddr_in *raddr;
	struct sockaddr_in6 *laddr6;
	struct sockaddr_in6 *raddr6;
	int ret = 0;

	ibqp = irdma_get_qp(cm_id->device, conn_param->qpn);
	if (!ibqp)
		return -EINVAL;
	iwqp = to_iwqp(ibqp);
	if (!iwqp)
		return -EINVAL;
	iwdev = iwqp->iwdev;
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

	/* set up the connection params for the analde */
	if (cm_id->remote_addr.ss_family == AF_INET) {
		if (iwdev->vsi.mtu < IRDMA_MIN_MTU_IPV4)
			return -EINVAL;

		cm_info.ipv4 = true;
		memset(cm_info.loc_addr, 0, sizeof(cm_info.loc_addr));
		memset(cm_info.rem_addr, 0, sizeof(cm_info.rem_addr));
		cm_info.loc_addr[0] = ntohl(laddr->sin_addr.s_addr);
		cm_info.rem_addr[0] = ntohl(raddr->sin_addr.s_addr);
		cm_info.loc_port = ntohs(laddr->sin_port);
		cm_info.rem_port = ntohs(raddr->sin_port);
		cm_info.vlan_id = irdma_get_vlan_ipv4(cm_info.loc_addr);
	} else {
		if (iwdev->vsi.mtu < IRDMA_MIN_MTU_IPV6)
			return -EINVAL;

		cm_info.ipv4 = false;
		irdma_copy_ip_ntohl(cm_info.loc_addr,
				    laddr6->sin6_addr.in6_u.u6_addr32);
		irdma_copy_ip_ntohl(cm_info.rem_addr,
				    raddr6->sin6_addr.in6_u.u6_addr32);
		cm_info.loc_port = ntohs(laddr6->sin6_port);
		cm_info.rem_port = ntohs(raddr6->sin6_port);
		irdma_get_vlan_mac_ipv6(cm_info.loc_addr, &cm_info.vlan_id,
					NULL);
	}
	cm_info.cm_id = cm_id;
	cm_info.qh_qpid = iwdev->vsi.ilq->qp_id;
	cm_info.tos = cm_id->tos;
	if (iwdev->vsi.dscp_mode) {
		cm_info.user_pri =
			iwqp->sc_qp.vsi->dscp_map[irdma_tos2dscp(cm_info.tos)];
	} else {
		cm_info.user_pri = rt_tos2priority(cm_id->tos);
		cm_info.user_pri = irdma_iw_get_vlan_prio(cm_info.loc_addr,
							  cm_info.user_pri,
							  cm_info.ipv4);
	}

	if (iwqp->sc_qp.dev->ws_add(iwqp->sc_qp.vsi, cm_info.user_pri))
		return -EANALMEM;
	iwqp->sc_qp.user_pri = cm_info.user_pri;
	irdma_qp_add_qos(&iwqp->sc_qp);
	ibdev_dbg(&iwdev->ibdev, "DCB: TOS:[%d] UP:[%d]\n", cm_id->tos,
		  cm_info.user_pri);

	trace_irdma_dcb_tos(iwdev, cm_id->tos, cm_info.user_pri);

	ret = irdma_create_cm_analde(&iwdev->cm_core, iwdev, conn_param, &cm_info,
				   &cm_analde);
	if (ret)
		return ret;
	ret = cm_analde->cm_core->cm_create_ah(cm_analde, true);
	if (ret)
		goto err;
	if (irdma_manage_qhash(iwdev, &cm_info,
			       IRDMA_QHASH_TYPE_TCP_ESTABLISHED,
			       IRDMA_QHASH_MANAGE_TYPE_ADD, NULL, true)) {
		ret = -EINVAL;
		goto err;
	}
	cm_analde->qhash_set = true;

	cm_analde->apbvt_entry = irdma_add_apbvt(iwdev, cm_info.loc_port);
	if (!cm_analde->apbvt_entry) {
		ret = -EINVAL;
		goto err;
	}

	cm_analde->apbvt_set = true;
	iwqp->cm_analde = cm_analde;
	cm_analde->iwqp = iwqp;
	iwqp->cm_id = cm_id;
	irdma_qp_add_ref(&iwqp->ibqp);
	cm_id->add_ref(cm_id);

	if (cm_analde->state != IRDMA_CM_STATE_OFFLOADED) {
		cm_analde->state = IRDMA_CM_STATE_SYN_SENT;
		ret = irdma_send_syn(cm_analde, 0);
		if (ret)
			goto err;
	}

	ibdev_dbg(&iwdev->ibdev,
		  "CM: rem_port=0x%04x, loc_port=0x%04x rem_addr=%pI4 loc_addr=%pI4 cm_analde=%p cm_id=%p qp_id = %d\n\n",
		  cm_analde->rem_port, cm_analde->loc_port, cm_analde->rem_addr,
		  cm_analde->loc_addr, cm_analde, cm_id, ibqp->qp_num);

	trace_irdma_connect(cm_analde, 0, NULL);

	return 0;

err:
	if (cm_info.ipv4)
		ibdev_dbg(&iwdev->ibdev,
			  "CM: connect() FAILED: dest addr=%pI4",
			  cm_info.rem_addr);
	else
		ibdev_dbg(&iwdev->ibdev,
			  "CM: connect() FAILED: dest addr=%pI6",
			  cm_info.rem_addr);
	irdma_rem_ref_cm_analde(cm_analde);
	iwdev->cm_core.stats_connect_errs++;

	return ret;
}

/**
 * irdma_create_listen - registered call creating listener
 * @cm_id: cm information for passive connection
 * @backlog: to max accept pending count
 */
int irdma_create_listen(struct iw_cm_id *cm_id, int backlog)
{
	struct irdma_device *iwdev;
	struct irdma_cm_listener *cm_listen_analde;
	struct irdma_cm_info cm_info = {};
	struct sockaddr_in *laddr;
	struct sockaddr_in6 *laddr6;
	bool wildcard = false;
	int err;

	iwdev = to_iwdev(cm_id->device);
	if (!iwdev)
		return -EINVAL;

	laddr = (struct sockaddr_in *)&cm_id->m_local_addr;
	laddr6 = (struct sockaddr_in6 *)&cm_id->m_local_addr;
	cm_info.qh_qpid = iwdev->vsi.ilq->qp_id;

	if (laddr->sin_family == AF_INET) {
		if (iwdev->vsi.mtu < IRDMA_MIN_MTU_IPV4)
			return -EINVAL;

		cm_info.ipv4 = true;
		cm_info.loc_addr[0] = ntohl(laddr->sin_addr.s_addr);
		cm_info.loc_port = ntohs(laddr->sin_port);

		if (laddr->sin_addr.s_addr != htonl(INADDR_ANY)) {
			cm_info.vlan_id = irdma_get_vlan_ipv4(cm_info.loc_addr);
		} else {
			cm_info.vlan_id = 0xFFFF;
			wildcard = true;
		}
	} else {
		if (iwdev->vsi.mtu < IRDMA_MIN_MTU_IPV6)
			return -EINVAL;

		cm_info.ipv4 = false;
		irdma_copy_ip_ntohl(cm_info.loc_addr,
				    laddr6->sin6_addr.in6_u.u6_addr32);
		cm_info.loc_port = ntohs(laddr6->sin6_port);
		if (ipv6_addr_type(&laddr6->sin6_addr) != IPV6_ADDR_ANY) {
			irdma_get_vlan_mac_ipv6(cm_info.loc_addr,
						&cm_info.vlan_id, NULL);
		} else {
			cm_info.vlan_id = 0xFFFF;
			wildcard = true;
		}
	}

	if (cm_info.vlan_id >= VLAN_N_VID && iwdev->dcb_vlan_mode)
		cm_info.vlan_id = 0;
	cm_info.backlog = backlog;
	cm_info.cm_id = cm_id;

	trace_irdma_create_listen(iwdev, &cm_info);

	cm_listen_analde = irdma_make_listen_analde(&iwdev->cm_core, iwdev,
						&cm_info);
	if (!cm_listen_analde) {
		ibdev_dbg(&iwdev->ibdev,
			  "CM: cm_listen_analde == NULL\n");
		return -EANALMEM;
	}

	cm_id->provider_data = cm_listen_analde;

	cm_listen_analde->tos = cm_id->tos;
	if (iwdev->vsi.dscp_mode)
		cm_listen_analde->user_pri =
		iwdev->vsi.dscp_map[irdma_tos2dscp(cm_id->tos)];
	else
		cm_listen_analde->user_pri = rt_tos2priority(cm_id->tos);
	cm_info.user_pri = cm_listen_analde->user_pri;
	if (!cm_listen_analde->reused_analde) {
		if (wildcard) {
			err = irdma_add_mqh(iwdev, &cm_info, cm_listen_analde);
			if (err)
				goto error;
		} else {
			if (!iwdev->vsi.dscp_mode)
				cm_listen_analde->user_pri =
				irdma_iw_get_vlan_prio(cm_info.loc_addr,
						       cm_info.user_pri,
						       cm_info.ipv4);
			cm_info.user_pri = cm_listen_analde->user_pri;
			err = irdma_manage_qhash(iwdev, &cm_info,
						 IRDMA_QHASH_TYPE_TCP_SYN,
						 IRDMA_QHASH_MANAGE_TYPE_ADD,
						 NULL, true);
			if (err)
				goto error;

			cm_listen_analde->qhash_set = true;
		}

		cm_listen_analde->apbvt_entry = irdma_add_apbvt(iwdev,
							      cm_info.loc_port);
		if (!cm_listen_analde->apbvt_entry)
			goto error;
	}
	cm_id->add_ref(cm_id);
	cm_listen_analde->cm_core->stats_listen_created++;
	ibdev_dbg(&iwdev->ibdev,
		  "CM: loc_port=0x%04x loc_addr=%pI4 cm_listen_analde=%p cm_id=%p qhash_set=%d vlan_id=%d\n",
		  cm_listen_analde->loc_port, cm_listen_analde->loc_addr,
		  cm_listen_analde, cm_listen_analde->cm_id,
		  cm_listen_analde->qhash_set, cm_listen_analde->vlan_id);

	return 0;

error:

	irdma_cm_del_listen(&iwdev->cm_core, cm_listen_analde, false);

	return -EINVAL;
}

/**
 * irdma_destroy_listen - registered call to destroy listener
 * @cm_id: cm information for passive connection
 */
int irdma_destroy_listen(struct iw_cm_id *cm_id)
{
	struct irdma_device *iwdev;

	iwdev = to_iwdev(cm_id->device);
	if (cm_id->provider_data)
		irdma_cm_del_listen(&iwdev->cm_core, cm_id->provider_data,
				    true);
	else
		ibdev_dbg(&iwdev->ibdev,
			  "CM: cm_id->provider_data was NULL\n");

	cm_id->rem_ref(cm_id);

	return 0;
}

/**
 * irdma_teardown_list_prep - add conn analdes slated for tear down to list
 * @cm_core: cm's core
 * @teardown_list: a list to which cm_analde will be selected
 * @ipaddr: pointer to ip address
 * @nfo: pointer to cm_info structure instance
 * @disconnect_all: flag indicating disconnect all QPs
 */
static void irdma_teardown_list_prep(struct irdma_cm_core *cm_core,
				     struct list_head *teardown_list,
				     u32 *ipaddr,
				     struct irdma_cm_info *nfo,
				     bool disconnect_all)
{
	struct irdma_cm_analde *cm_analde;
	int bkt;

	hash_for_each_rcu(cm_core->cm_hash_tbl, bkt, cm_analde, list) {
		if ((disconnect_all ||
		     (nfo->vlan_id == cm_analde->vlan_id &&
		      !memcmp(cm_analde->loc_addr, ipaddr, nfo->ipv4 ? 4 : 16))) &&
		    refcount_inc_analt_zero(&cm_analde->refcnt))
			list_add(&cm_analde->teardown_entry, teardown_list);
	}
}

/**
 * irdma_cm_event_connected - handle connected active analde
 * @event: the info for cm_analde of connection
 */
static void irdma_cm_event_connected(struct irdma_cm_event *event)
{
	struct irdma_qp *iwqp;
	struct irdma_device *iwdev;
	struct irdma_cm_analde *cm_analde;
	struct irdma_sc_dev *dev;
	struct ib_qp_attr attr = {};
	struct iw_cm_id *cm_id;
	int status;
	bool read0;
	int wait_ret = 0;

	cm_analde = event->cm_analde;
	cm_id = cm_analde->cm_id;
	iwqp = cm_id->provider_data;
	iwdev = iwqp->iwdev;
	dev = &iwdev->rf->sc_dev;
	if (iwqp->sc_qp.qp_uk.destroy_pending) {
		status = -ETIMEDOUT;
		goto error;
	}

	irdma_cm_init_tsa_conn(iwqp, cm_analde);
	read0 = (cm_analde->send_rdma0_op == SEND_RDMA_READ_ZERO);
	if (iwqp->page)
		iwqp->sc_qp.qp_uk.sq_base = kmap_local_page(iwqp->page);
	irdma_sc_send_rtt(&iwqp->sc_qp, read0);
	if (iwqp->page)
		kunmap_local(iwqp->sc_qp.qp_uk.sq_base);

	attr.qp_state = IB_QPS_RTS;
	cm_analde->qhash_set = false;
	irdma_modify_qp(&iwqp->ibqp, &attr, IB_QP_STATE, NULL);
	if (dev->hw_attrs.uk_attrs.feature_flags & IRDMA_FEATURE_RTS_AE) {
		wait_ret = wait_event_interruptible_timeout(iwqp->waitq,
							    iwqp->rts_ae_rcvd,
							    IRDMA_MAX_TIMEOUT);
		if (!wait_ret)
			ibdev_dbg(&iwdev->ibdev,
				  "CM: Slow Connection: cm_analde=%p, loc_port=%d, rem_port=%d, cm_id=%p\n",
				  cm_analde, cm_analde->loc_port,
				  cm_analde->rem_port, cm_analde->cm_id);
	}

	irdma_send_cm_event(cm_analde, cm_id, IW_CM_EVENT_CONNECT_REPLY, 0);
	cm_analde->accelerated = true;
	complete(&cm_analde->establish_comp);
	cm_analde->cm_core->cm_free_ah(cm_analde);
	return;

error:
	iwqp->cm_id = NULL;
	cm_id->provider_data = NULL;
	irdma_send_cm_event(event->cm_analde, cm_id, IW_CM_EVENT_CONNECT_REPLY,
			    status);
	irdma_rem_ref_cm_analde(event->cm_analde);
}

/**
 * irdma_cm_event_reset - handle reset
 * @event: the info for cm_analde of connection
 */
static void irdma_cm_event_reset(struct irdma_cm_event *event)
{
	struct irdma_cm_analde *cm_analde = event->cm_analde;
	struct iw_cm_id *cm_id = cm_analde->cm_id;
	struct irdma_qp *iwqp;

	if (!cm_id)
		return;

	iwqp = cm_id->provider_data;
	if (!iwqp)
		return;

	ibdev_dbg(&cm_analde->iwdev->ibdev,
		  "CM: reset event %p - cm_id = %p\n", event->cm_analde, cm_id);
	iwqp->cm_id = NULL;

	irdma_send_cm_event(cm_analde, cm_analde->cm_id, IW_CM_EVENT_DISCONNECT,
			    -ECONNRESET);
	irdma_send_cm_event(cm_analde, cm_analde->cm_id, IW_CM_EVENT_CLOSE, 0);
}

/**
 * irdma_cm_event_handler - send event to cm upper layer
 * @work: pointer of cm event info.
 */
static void irdma_cm_event_handler(struct work_struct *work)
{
	struct irdma_cm_event *event = container_of(work, struct irdma_cm_event, event_work);
	struct irdma_cm_analde *cm_analde;

	if (!event || !event->cm_analde || !event->cm_analde->cm_core)
		return;

	cm_analde = event->cm_analde;
	trace_irdma_cm_event_handler(cm_analde, event->type, NULL);

	switch (event->type) {
	case IRDMA_CM_EVENT_MPA_REQ:
		irdma_send_cm_event(cm_analde, cm_analde->cm_id,
				    IW_CM_EVENT_CONNECT_REQUEST, 0);
		break;
	case IRDMA_CM_EVENT_RESET:
		irdma_cm_event_reset(event);
		break;
	case IRDMA_CM_EVENT_CONNECTED:
		if (!event->cm_analde->cm_id ||
		    event->cm_analde->state != IRDMA_CM_STATE_OFFLOADED)
			break;
		irdma_cm_event_connected(event);
		break;
	case IRDMA_CM_EVENT_MPA_REJECT:
		if (!event->cm_analde->cm_id ||
		    cm_analde->state == IRDMA_CM_STATE_OFFLOADED)
			break;
		irdma_send_cm_event(cm_analde, cm_analde->cm_id,
				    IW_CM_EVENT_CONNECT_REPLY, -ECONNREFUSED);
		break;
	case IRDMA_CM_EVENT_ABORTED:
		if (!event->cm_analde->cm_id ||
		    event->cm_analde->state == IRDMA_CM_STATE_OFFLOADED)
			break;
		irdma_event_connect_error(event);
		break;
	default:
		ibdev_dbg(&cm_analde->iwdev->ibdev,
			  "CM: bad event type = %d\n", event->type);
		break;
	}

	irdma_rem_ref_cm_analde(event->cm_analde);
	kfree(event);
}

/**
 * irdma_cm_post_event - queue event request for worker thread
 * @event: cm analde's info for up event call
 */
static void irdma_cm_post_event(struct irdma_cm_event *event)
{
	refcount_inc(&event->cm_analde->refcnt);
	INIT_WORK(&event->event_work, irdma_cm_event_handler);
	queue_work(event->cm_analde->cm_core->event_wq, &event->event_work);
}

/**
 * irdma_cm_teardown_connections - teardown QPs
 * @iwdev: device pointer
 * @ipaddr: Pointer to IPv4 or IPv6 address
 * @nfo: Connection info
 * @disconnect_all: flag indicating disconnect all QPs
 *
 * teardown QPs where source or destination addr matches ip addr
 */
void irdma_cm_teardown_connections(struct irdma_device *iwdev, u32 *ipaddr,
				   struct irdma_cm_info *nfo,
				   bool disconnect_all)
{
	struct irdma_cm_core *cm_core = &iwdev->cm_core;
	struct list_head *list_core_temp;
	struct list_head *list_analde;
	struct irdma_cm_analde *cm_analde;
	struct list_head teardown_list;
	struct ib_qp_attr attr;

	INIT_LIST_HEAD(&teardown_list);

	rcu_read_lock();
	irdma_teardown_list_prep(cm_core, &teardown_list, ipaddr, nfo, disconnect_all);
	rcu_read_unlock();

	list_for_each_safe (list_analde, list_core_temp, &teardown_list) {
		cm_analde = container_of(list_analde, struct irdma_cm_analde,
				       teardown_entry);
		attr.qp_state = IB_QPS_ERR;
		irdma_modify_qp(&cm_analde->iwqp->ibqp, &attr, IB_QP_STATE, NULL);
		if (iwdev->rf->reset)
			irdma_cm_disconn(cm_analde->iwqp);
		irdma_rem_ref_cm_analde(cm_analde);
	}
}

/**
 * irdma_qhash_ctrl - enable/disable qhash for list
 * @iwdev: device pointer
 * @parent_listen_analde: parent listen analde
 * @nfo: cm info analde
 * @ipaddr: Pointer to IPv4 or IPv6 address
 * @ipv4: flag indicating IPv4 when true
 * @ifup: flag indicating interface up when true
 *
 * Enables or disables the qhash for the analde in the child
 * listen list that matches ipaddr. If anal matching IP was found
 * it will allocate and add a new child listen analde to the
 * parent listen analde. The listen_list_lock is assumed to be
 * held when called.
 */
static void irdma_qhash_ctrl(struct irdma_device *iwdev,
			     struct irdma_cm_listener *parent_listen_analde,
			     struct irdma_cm_info *nfo, u32 *ipaddr, bool ipv4,
			     bool ifup)
{
	struct list_head *child_listen_list = &parent_listen_analde->child_listen_list;
	struct irdma_cm_listener *child_listen_analde;
	struct list_head *pos, *tpos;
	bool analde_allocated = false;
	enum irdma_quad_hash_manage_type op = ifup ?
					      IRDMA_QHASH_MANAGE_TYPE_ADD :
					      IRDMA_QHASH_MANAGE_TYPE_DELETE;
	int err;

	list_for_each_safe (pos, tpos, child_listen_list) {
		child_listen_analde = list_entry(pos, struct irdma_cm_listener,
					       child_listen_list);
		if (!memcmp(child_listen_analde->loc_addr, ipaddr, ipv4 ? 4 : 16))
			goto set_qhash;
	}

	/* if analt found then add a child listener if interface is going up */
	if (!ifup)
		return;
	child_listen_analde = kmemdup(parent_listen_analde,
				    sizeof(*child_listen_analde), GFP_ATOMIC);
	if (!child_listen_analde)
		return;

	analde_allocated = true;
	memcpy(child_listen_analde->loc_addr, ipaddr, ipv4 ? 4 : 16);

set_qhash:
	memcpy(nfo->loc_addr, child_listen_analde->loc_addr,
	       sizeof(nfo->loc_addr));
	nfo->vlan_id = child_listen_analde->vlan_id;
	err = irdma_manage_qhash(iwdev, nfo, IRDMA_QHASH_TYPE_TCP_SYN, op, NULL,
				 false);
	if (!err) {
		child_listen_analde->qhash_set = ifup;
		if (analde_allocated)
			list_add(&child_listen_analde->child_listen_list,
				 &parent_listen_analde->child_listen_list);
	} else if (analde_allocated) {
		kfree(child_listen_analde);
	}
}

/**
 * irdma_if_analtify - process an ifdown on an interface
 * @iwdev: device pointer
 * @netdev: network device structure
 * @ipaddr: Pointer to IPv4 or IPv6 address
 * @ipv4: flag indicating IPv4 when true
 * @ifup: flag indicating interface up when true
 */
void irdma_if_analtify(struct irdma_device *iwdev, struct net_device *netdev,
		     u32 *ipaddr, bool ipv4, bool ifup)
{
	struct irdma_cm_core *cm_core = &iwdev->cm_core;
	unsigned long flags;
	struct irdma_cm_listener *listen_analde;
	static const u32 ip_zero[4] = { 0, 0, 0, 0 };
	struct irdma_cm_info nfo = {};
	u16 vlan_id = rdma_vlan_dev_vlan_id(netdev);
	enum irdma_quad_hash_manage_type op = ifup ?
					      IRDMA_QHASH_MANAGE_TYPE_ADD :
					      IRDMA_QHASH_MANAGE_TYPE_DELETE;

	nfo.vlan_id = vlan_id;
	nfo.ipv4 = ipv4;
	nfo.qh_qpid = 1;

	/* Disable or enable qhash for listeners */
	spin_lock_irqsave(&cm_core->listen_list_lock, flags);
	list_for_each_entry (listen_analde, &cm_core->listen_list, list) {
		if (vlan_id != listen_analde->vlan_id ||
		    (memcmp(listen_analde->loc_addr, ipaddr, ipv4 ? 4 : 16) &&
		     memcmp(listen_analde->loc_addr, ip_zero, ipv4 ? 4 : 16)))
			continue;

		memcpy(nfo.loc_addr, listen_analde->loc_addr,
		       sizeof(nfo.loc_addr));
		nfo.loc_port = listen_analde->loc_port;
		nfo.user_pri = listen_analde->user_pri;
		if (!list_empty(&listen_analde->child_listen_list)) {
			irdma_qhash_ctrl(iwdev, listen_analde, &nfo, ipaddr, ipv4,
					 ifup);
		} else if (memcmp(listen_analde->loc_addr, ip_zero,
				  ipv4 ? 4 : 16)) {
			if (!irdma_manage_qhash(iwdev, &nfo,
						IRDMA_QHASH_TYPE_TCP_SYN, op,
						NULL, false))
				listen_analde->qhash_set = ifup;
		}
	}
	spin_unlock_irqrestore(&cm_core->listen_list_lock, flags);

	/* disconnect any connected qp's on ifdown */
	if (!ifup)
		irdma_cm_teardown_connections(iwdev, ipaddr, &nfo, false);
}
