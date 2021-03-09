// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Intel Corporation. */

/* flow director ethtool support for iavf */

#include "iavf.h"

static const struct in6_addr ipv6_addr_full_mask = {
	.in6_u = {
		.u6_addr8 = {
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
			0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		}
	}
};

/**
 * iavf_fill_fdir_ip4_hdr - fill the IPv4 protocol header
 * @fltr: Flow Director filter data structure
 * @proto_hdrs: Flow Director protocol headers data structure
 *
 * Returns 0 if the IPv4 protocol header is set successfully
 */
static int
iavf_fill_fdir_ip4_hdr(struct iavf_fdir_fltr *fltr,
		       struct virtchnl_proto_hdrs *proto_hdrs)
{
	struct virtchnl_proto_hdr *hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];
	struct iphdr *iph = (struct iphdr *)hdr->buffer;

	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, IPV4);

	if (fltr->ip_mask.tos == U8_MAX) {
		iph->tos = fltr->ip_data.tos;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV4, DSCP);
	}

	if (fltr->ip_mask.proto == U8_MAX) {
		iph->protocol = fltr->ip_data.proto;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV4, PROT);
	}

	if (fltr->ip_mask.v4_addrs.src_ip == htonl(U32_MAX)) {
		iph->saddr = fltr->ip_data.v4_addrs.src_ip;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV4, SRC);
	}

	if (fltr->ip_mask.v4_addrs.dst_ip == htonl(U32_MAX)) {
		iph->daddr = fltr->ip_data.v4_addrs.dst_ip;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV4, DST);
	}

	return 0;
}

/**
 * iavf_fill_fdir_ip6_hdr - fill the IPv6 protocol header
 * @fltr: Flow Director filter data structure
 * @proto_hdrs: Flow Director protocol headers data structure
 *
 * Returns 0 if the IPv6 protocol header is set successfully
 */
static int
iavf_fill_fdir_ip6_hdr(struct iavf_fdir_fltr *fltr,
		       struct virtchnl_proto_hdrs *proto_hdrs)
{
	struct virtchnl_proto_hdr *hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];
	struct ipv6hdr *iph = (struct ipv6hdr *)hdr->buffer;

	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, IPV6);

	if (fltr->ip_mask.tclass == U8_MAX) {
		iph->priority = (fltr->ip_data.tclass >> 4) & 0xF;
		iph->flow_lbl[0] = (fltr->ip_data.tclass << 4) & 0xF0;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV6, TC);
	}

	if (fltr->ip_mask.proto == U8_MAX) {
		iph->nexthdr = fltr->ip_data.proto;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV6, PROT);
	}

	if (!memcmp(&fltr->ip_mask.v6_addrs.src_ip, &ipv6_addr_full_mask,
		    sizeof(struct in6_addr))) {
		memcpy(&iph->saddr, &fltr->ip_data.v6_addrs.src_ip,
		       sizeof(struct in6_addr));
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV6, SRC);
	}

	if (!memcmp(&fltr->ip_mask.v6_addrs.dst_ip, &ipv6_addr_full_mask,
		    sizeof(struct in6_addr))) {
		memcpy(&iph->daddr, &fltr->ip_data.v6_addrs.dst_ip,
		       sizeof(struct in6_addr));
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV6, DST);
	}

	return 0;
}

/**
 * iavf_fill_fdir_tcp_hdr - fill the TCP protocol header
 * @fltr: Flow Director filter data structure
 * @proto_hdrs: Flow Director protocol headers data structure
 *
 * Returns 0 if the TCP protocol header is set successfully
 */
static int
iavf_fill_fdir_tcp_hdr(struct iavf_fdir_fltr *fltr,
		       struct virtchnl_proto_hdrs *proto_hdrs)
{
	struct virtchnl_proto_hdr *hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];
	struct tcphdr *tcph = (struct tcphdr *)hdr->buffer;

	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, TCP);

	if (fltr->ip_mask.src_port == htons(U16_MAX)) {
		tcph->source = fltr->ip_data.src_port;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, TCP, SRC_PORT);
	}

	if (fltr->ip_mask.dst_port == htons(U16_MAX)) {
		tcph->dest = fltr->ip_data.dst_port;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, TCP, DST_PORT);
	}

	return 0;
}

/**
 * iavf_fill_fdir_udp_hdr - fill the UDP protocol header
 * @fltr: Flow Director filter data structure
 * @proto_hdrs: Flow Director protocol headers data structure
 *
 * Returns 0 if the UDP protocol header is set successfully
 */
static int
iavf_fill_fdir_udp_hdr(struct iavf_fdir_fltr *fltr,
		       struct virtchnl_proto_hdrs *proto_hdrs)
{
	struct virtchnl_proto_hdr *hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];
	struct udphdr *udph = (struct udphdr *)hdr->buffer;

	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, UDP);

	if (fltr->ip_mask.src_port == htons(U16_MAX)) {
		udph->source = fltr->ip_data.src_port;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, UDP, SRC_PORT);
	}

	if (fltr->ip_mask.dst_port == htons(U16_MAX)) {
		udph->dest = fltr->ip_data.dst_port;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, UDP, DST_PORT);
	}

	return 0;
}

/**
 * iavf_fill_fdir_sctp_hdr - fill the SCTP protocol header
 * @fltr: Flow Director filter data structure
 * @proto_hdrs: Flow Director protocol headers data structure
 *
 * Returns 0 if the SCTP protocol header is set successfully
 */
static int
iavf_fill_fdir_sctp_hdr(struct iavf_fdir_fltr *fltr,
			struct virtchnl_proto_hdrs *proto_hdrs)
{
	struct virtchnl_proto_hdr *hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];
	struct sctphdr *sctph = (struct sctphdr *)hdr->buffer;

	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, SCTP);

	if (fltr->ip_mask.src_port == htons(U16_MAX)) {
		sctph->source = fltr->ip_data.src_port;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, SCTP, SRC_PORT);
	}

	if (fltr->ip_mask.dst_port == htons(U16_MAX)) {
		sctph->dest = fltr->ip_data.dst_port;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, SCTP, DST_PORT);
	}

	return 0;
}

/**
 * iavf_fill_fdir_ah_hdr - fill the AH protocol header
 * @fltr: Flow Director filter data structure
 * @proto_hdrs: Flow Director protocol headers data structure
 *
 * Returns 0 if the AH protocol header is set successfully
 */
static int
iavf_fill_fdir_ah_hdr(struct iavf_fdir_fltr *fltr,
		      struct virtchnl_proto_hdrs *proto_hdrs)
{
	struct virtchnl_proto_hdr *hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];
	struct ip_auth_hdr *ah = (struct ip_auth_hdr *)hdr->buffer;

	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, AH);

	if (fltr->ip_mask.spi == htonl(U32_MAX)) {
		ah->spi = fltr->ip_data.spi;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, AH, SPI);
	}

	return 0;
}

/**
 * iavf_fill_fdir_esp_hdr - fill the ESP protocol header
 * @fltr: Flow Director filter data structure
 * @proto_hdrs: Flow Director protocol headers data structure
 *
 * Returns 0 if the ESP protocol header is set successfully
 */
static int
iavf_fill_fdir_esp_hdr(struct iavf_fdir_fltr *fltr,
		       struct virtchnl_proto_hdrs *proto_hdrs)
{
	struct virtchnl_proto_hdr *hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];
	struct ip_esp_hdr *esph = (struct ip_esp_hdr *)hdr->buffer;

	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, ESP);

	if (fltr->ip_mask.spi == htonl(U32_MAX)) {
		esph->spi = fltr->ip_data.spi;
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, ESP, SPI);
	}

	return 0;
}

/**
 * iavf_fill_fdir_l4_hdr - fill the L4 protocol header
 * @fltr: Flow Director filter data structure
 * @proto_hdrs: Flow Director protocol headers data structure
 *
 * Returns 0 if the L4 protocol header is set successfully
 */
static int
iavf_fill_fdir_l4_hdr(struct iavf_fdir_fltr *fltr,
		      struct virtchnl_proto_hdrs *proto_hdrs)
{
	struct virtchnl_proto_hdr *hdr;
	__be32 *l4_4_data;

	if (!fltr->ip_mask.proto) /* IPv4/IPv6 header only */
		return 0;

	hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];
	l4_4_data = (__be32 *)hdr->buffer;

	/* L2TPv3 over IP with 'Session ID' */
	if (fltr->ip_data.proto == 115 && fltr->ip_mask.l4_header == htonl(U32_MAX)) {
		VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, L2TPV3);
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, L2TPV3, SESS_ID);

		*l4_4_data = fltr->ip_data.l4_header;
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * iavf_fill_fdir_eth_hdr - fill the Ethernet protocol header
 * @fltr: Flow Director filter data structure
 * @proto_hdrs: Flow Director protocol headers data structure
 *
 * Returns 0 if the Ethernet protocol header is set successfully
 */
static int
iavf_fill_fdir_eth_hdr(struct iavf_fdir_fltr *fltr,
		       struct virtchnl_proto_hdrs *proto_hdrs)
{
	struct virtchnl_proto_hdr *hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];

	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, ETH);

	return 0;
}

/**
 * iavf_fill_fdir_add_msg - fill the Flow Director filter into virtchnl message
 * @adapter: pointer to the VF adapter structure
 * @fltr: Flow Director filter data structure
 *
 * Returns 0 if the add Flow Director virtchnl message is filled successfully
 */
int iavf_fill_fdir_add_msg(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr)
{
	struct virtchnl_fdir_add *vc_msg = &fltr->vc_add_msg;
	struct virtchnl_proto_hdrs *proto_hdrs;
	int err;

	proto_hdrs = &vc_msg->rule_cfg.proto_hdrs;

	err = iavf_fill_fdir_eth_hdr(fltr, proto_hdrs); /* L2 always exists */
	if (err)
		return err;

	switch (fltr->flow_type) {
	case IAVF_FDIR_FLOW_IPV4_TCP:
		err = iavf_fill_fdir_ip4_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_tcp_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV4_UDP:
		err = iavf_fill_fdir_ip4_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_udp_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV4_SCTP:
		err = iavf_fill_fdir_ip4_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_sctp_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV4_AH:
		err = iavf_fill_fdir_ip4_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_ah_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV4_ESP:
		err = iavf_fill_fdir_ip4_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_esp_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV4_OTHER:
		err = iavf_fill_fdir_ip4_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_l4_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV6_TCP:
		err = iavf_fill_fdir_ip6_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_tcp_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV6_UDP:
		err = iavf_fill_fdir_ip6_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_udp_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV6_SCTP:
		err = iavf_fill_fdir_ip6_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_sctp_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV6_AH:
		err = iavf_fill_fdir_ip6_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_ah_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV6_ESP:
		err = iavf_fill_fdir_ip6_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_esp_hdr(fltr, proto_hdrs);
		break;
	case IAVF_FDIR_FLOW_IPV6_OTHER:
		err = iavf_fill_fdir_ip6_hdr(fltr, proto_hdrs) |
		      iavf_fill_fdir_l4_hdr(fltr, proto_hdrs);
		break;
	default:
		err = -EINVAL;
		break;
	}

	if (err)
		return err;

	vc_msg->vsi_id = adapter->vsi.id;
	vc_msg->rule_cfg.action_set.count = 1;
	vc_msg->rule_cfg.action_set.actions[0].type = fltr->action;
	vc_msg->rule_cfg.action_set.actions[0].act_conf.queue.index = fltr->q_index;

	return 0;
}

/**
 * iavf_fdir_flow_proto_name - get the flow protocol name
 * @flow_type: Flow Director filter flow type
 **/
static const char *iavf_fdir_flow_proto_name(enum iavf_fdir_flow_type flow_type)
{
	switch (flow_type) {
	case IAVF_FDIR_FLOW_IPV4_TCP:
	case IAVF_FDIR_FLOW_IPV6_TCP:
		return "TCP";
	case IAVF_FDIR_FLOW_IPV4_UDP:
	case IAVF_FDIR_FLOW_IPV6_UDP:
		return "UDP";
	case IAVF_FDIR_FLOW_IPV4_SCTP:
	case IAVF_FDIR_FLOW_IPV6_SCTP:
		return "SCTP";
	case IAVF_FDIR_FLOW_IPV4_AH:
	case IAVF_FDIR_FLOW_IPV6_AH:
		return "AH";
	case IAVF_FDIR_FLOW_IPV4_ESP:
	case IAVF_FDIR_FLOW_IPV6_ESP:
		return "ESP";
	case IAVF_FDIR_FLOW_IPV4_OTHER:
	case IAVF_FDIR_FLOW_IPV6_OTHER:
		return "Other";
	default:
		return NULL;
	}
}

/**
 * iavf_print_fdir_fltr
 * @adapter: adapter structure
 * @fltr: Flow Director filter to print
 *
 * Print the Flow Director filter
 **/
void iavf_print_fdir_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr)
{
	const char *proto = iavf_fdir_flow_proto_name(fltr->flow_type);

	if (!proto)
		return;

	switch (fltr->flow_type) {
	case IAVF_FDIR_FLOW_IPV4_TCP:
	case IAVF_FDIR_FLOW_IPV4_UDP:
	case IAVF_FDIR_FLOW_IPV4_SCTP:
		dev_info(&adapter->pdev->dev, "Rule ID: %u dst_ip: %pI4 src_ip %pI4 %s: dst_port %hu src_port %hu\n",
			 fltr->loc,
			 &fltr->ip_data.v4_addrs.dst_ip,
			 &fltr->ip_data.v4_addrs.src_ip,
			 proto,
			 ntohs(fltr->ip_data.dst_port),
			 ntohs(fltr->ip_data.src_port));
		break;
	case IAVF_FDIR_FLOW_IPV4_AH:
	case IAVF_FDIR_FLOW_IPV4_ESP:
		dev_info(&adapter->pdev->dev, "Rule ID: %u dst_ip: %pI4 src_ip %pI4 %s: SPI %u\n",
			 fltr->loc,
			 &fltr->ip_data.v4_addrs.dst_ip,
			 &fltr->ip_data.v4_addrs.src_ip,
			 proto,
			 ntohl(fltr->ip_data.spi));
		break;
	case IAVF_FDIR_FLOW_IPV4_OTHER:
		dev_info(&adapter->pdev->dev, "Rule ID: %u dst_ip: %pI4 src_ip %pI4 proto: %u L4_bytes: 0x%x\n",
			 fltr->loc,
			 &fltr->ip_data.v4_addrs.dst_ip,
			 &fltr->ip_data.v4_addrs.src_ip,
			 fltr->ip_data.proto,
			 ntohl(fltr->ip_data.l4_header));
		break;
	case IAVF_FDIR_FLOW_IPV6_TCP:
	case IAVF_FDIR_FLOW_IPV6_UDP:
	case IAVF_FDIR_FLOW_IPV6_SCTP:
		dev_info(&adapter->pdev->dev, "Rule ID: %u dst_ip: %pI6 src_ip %pI6 %s: dst_port %hu src_port %hu\n",
			 fltr->loc,
			 &fltr->ip_data.v6_addrs.dst_ip,
			 &fltr->ip_data.v6_addrs.src_ip,
			 proto,
			 ntohs(fltr->ip_data.dst_port),
			 ntohs(fltr->ip_data.src_port));
		break;
	case IAVF_FDIR_FLOW_IPV6_AH:
	case IAVF_FDIR_FLOW_IPV6_ESP:
		dev_info(&adapter->pdev->dev, "Rule ID: %u dst_ip: %pI6 src_ip %pI6 %s: SPI %u\n",
			 fltr->loc,
			 &fltr->ip_data.v6_addrs.dst_ip,
			 &fltr->ip_data.v6_addrs.src_ip,
			 proto,
			 ntohl(fltr->ip_data.spi));
		break;
	case IAVF_FDIR_FLOW_IPV6_OTHER:
		dev_info(&adapter->pdev->dev, "Rule ID: %u dst_ip: %pI6 src_ip %pI6 proto: %u L4_bytes: 0x%x\n",
			 fltr->loc,
			 &fltr->ip_data.v6_addrs.dst_ip,
			 &fltr->ip_data.v6_addrs.src_ip,
			 fltr->ip_data.proto,
			 ntohl(fltr->ip_data.l4_header));
		break;
	default:
		break;
	}
}

/**
 * iavf_fdir_is_dup_fltr - test if filter is already in list
 * @adapter: pointer to the VF adapter structure
 * @fltr: Flow Director filter data structure
 *
 * Returns true if the filter is found in the list
 */
bool iavf_fdir_is_dup_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr)
{
	struct iavf_fdir_fltr *tmp;
	bool ret = false;

	list_for_each_entry(tmp, &adapter->fdir_list_head, list) {
		if (tmp->flow_type != fltr->flow_type)
			continue;

		if (!memcmp(&tmp->ip_data, &fltr->ip_data,
			    sizeof(fltr->ip_data))) {
			ret = true;
			break;
		}
	}

	return ret;
}

/**
 * iavf_find_fdir_fltr_by_loc - find filter with location
 * @adapter: pointer to the VF adapter structure
 * @loc: location to find.
 *
 * Returns pointer to Flow Director filter if found or null
 */
struct iavf_fdir_fltr *iavf_find_fdir_fltr_by_loc(struct iavf_adapter *adapter, u32 loc)
{
	struct iavf_fdir_fltr *rule;

	list_for_each_entry(rule, &adapter->fdir_list_head, list)
		if (rule->loc == loc)
			return rule;

	return NULL;
}

/**
 * iavf_fdir_list_add_fltr - add a new node to the flow director filter list
 * @adapter: pointer to the VF adapter structure
 * @fltr: filter node to add to structure
 */
void iavf_fdir_list_add_fltr(struct iavf_adapter *adapter, struct iavf_fdir_fltr *fltr)
{
	struct iavf_fdir_fltr *rule, *parent = NULL;

	list_for_each_entry(rule, &adapter->fdir_list_head, list) {
		if (rule->loc >= fltr->loc)
			break;
		parent = rule;
	}

	if (parent)
		list_add(&fltr->list, &parent->list);
	else
		list_add(&fltr->list, &adapter->fdir_list_head);
}
