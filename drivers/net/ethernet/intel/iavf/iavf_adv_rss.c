// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021, Intel Corporation. */

/* advanced RSS configuration ethtool support for iavf */

#include "iavf.h"

/**
 * iavf_fill_adv_rss_ip4_hdr - fill the IPv4 RSS protocol header
 * @hdr: the virtchnl message protocol header data structure
 * @hash_flds: the RSS configuration protocol hash fields
 */
static void
iavf_fill_adv_rss_ip4_hdr(struct virtchnl_proto_hdr *hdr, u64 hash_flds)
{
	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, IPV4);

	if (hash_flds & IAVF_ADV_RSS_HASH_FLD_IPV4_SA)
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV4, SRC);

	if (hash_flds & IAVF_ADV_RSS_HASH_FLD_IPV4_DA)
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV4, DST);
}

/**
 * iavf_fill_adv_rss_ip6_hdr - fill the IPv6 RSS protocol header
 * @hdr: the virtchnl message protocol header data structure
 * @hash_flds: the RSS configuration protocol hash fields
 */
static void
iavf_fill_adv_rss_ip6_hdr(struct virtchnl_proto_hdr *hdr, u64 hash_flds)
{
	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, IPV6);

	if (hash_flds & IAVF_ADV_RSS_HASH_FLD_IPV6_SA)
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV6, SRC);

	if (hash_flds & IAVF_ADV_RSS_HASH_FLD_IPV6_DA)
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, IPV6, DST);
}

/**
 * iavf_fill_adv_rss_tcp_hdr - fill the TCP RSS protocol header
 * @hdr: the virtchnl message protocol header data structure
 * @hash_flds: the RSS configuration protocol hash fields
 */
static void
iavf_fill_adv_rss_tcp_hdr(struct virtchnl_proto_hdr *hdr, u64 hash_flds)
{
	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, TCP);

	if (hash_flds & IAVF_ADV_RSS_HASH_FLD_TCP_SRC_PORT)
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, TCP, SRC_PORT);

	if (hash_flds & IAVF_ADV_RSS_HASH_FLD_TCP_DST_PORT)
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, TCP, DST_PORT);
}

/**
 * iavf_fill_adv_rss_udp_hdr - fill the UDP RSS protocol header
 * @hdr: the virtchnl message protocol header data structure
 * @hash_flds: the RSS configuration protocol hash fields
 */
static void
iavf_fill_adv_rss_udp_hdr(struct virtchnl_proto_hdr *hdr, u64 hash_flds)
{
	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, UDP);

	if (hash_flds & IAVF_ADV_RSS_HASH_FLD_UDP_SRC_PORT)
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, UDP, SRC_PORT);

	if (hash_flds & IAVF_ADV_RSS_HASH_FLD_UDP_DST_PORT)
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, UDP, DST_PORT);
}

/**
 * iavf_fill_adv_rss_sctp_hdr - fill the SCTP RSS protocol header
 * @hdr: the virtchnl message protocol header data structure
 * @hash_flds: the RSS configuration protocol hash fields
 */
static void
iavf_fill_adv_rss_sctp_hdr(struct virtchnl_proto_hdr *hdr, u64 hash_flds)
{
	VIRTCHNL_SET_PROTO_HDR_TYPE(hdr, SCTP);

	if (hash_flds & IAVF_ADV_RSS_HASH_FLD_SCTP_SRC_PORT)
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, SCTP, SRC_PORT);

	if (hash_flds & IAVF_ADV_RSS_HASH_FLD_SCTP_DST_PORT)
		VIRTCHNL_ADD_PROTO_HDR_FIELD_BIT(hdr, SCTP, DST_PORT);
}

/**
 * iavf_fill_adv_rss_cfg_msg - fill the RSS configuration into virtchnl message
 * @rss_cfg: the virtchnl message to be filled with RSS configuration setting
 * @packet_hdrs: the RSS configuration protocol header types
 * @hash_flds: the RSS configuration protocol hash fields
 * @symm: if true, symmetric hash is required
 *
 * Returns 0 if the RSS configuration virtchnl message is filled successfully
 */
int
iavf_fill_adv_rss_cfg_msg(struct virtchnl_rss_cfg *rss_cfg,
			  u32 packet_hdrs, u64 hash_flds, bool symm)
{
	struct virtchnl_proto_hdrs *proto_hdrs = &rss_cfg->proto_hdrs;
	struct virtchnl_proto_hdr *hdr;

	if (symm)
		rss_cfg->rss_algorithm = VIRTCHNL_RSS_ALG_TOEPLITZ_SYMMETRIC;
	else
		rss_cfg->rss_algorithm = VIRTCHNL_RSS_ALG_TOEPLITZ_ASYMMETRIC;

	proto_hdrs->tunnel_level = 0;	/* always outer layer */

	hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];
	switch (packet_hdrs & IAVF_ADV_RSS_FLOW_SEG_HDR_L3) {
	case IAVF_ADV_RSS_FLOW_SEG_HDR_IPV4:
		iavf_fill_adv_rss_ip4_hdr(hdr, hash_flds);
		break;
	case IAVF_ADV_RSS_FLOW_SEG_HDR_IPV6:
		iavf_fill_adv_rss_ip6_hdr(hdr, hash_flds);
		break;
	default:
		return -EINVAL;
	}

	hdr = &proto_hdrs->proto_hdr[proto_hdrs->count++];
	switch (packet_hdrs & IAVF_ADV_RSS_FLOW_SEG_HDR_L4) {
	case IAVF_ADV_RSS_FLOW_SEG_HDR_TCP:
		iavf_fill_adv_rss_tcp_hdr(hdr, hash_flds);
		break;
	case IAVF_ADV_RSS_FLOW_SEG_HDR_UDP:
		iavf_fill_adv_rss_udp_hdr(hdr, hash_flds);
		break;
	case IAVF_ADV_RSS_FLOW_SEG_HDR_SCTP:
		iavf_fill_adv_rss_sctp_hdr(hdr, hash_flds);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/**
 * iavf_find_adv_rss_cfg_by_hdrs - find RSS configuration with header type
 * @adapter: pointer to the VF adapter structure
 * @packet_hdrs: protocol header type to find.
 *
 * Returns pointer to advance RSS configuration if found or null
 */
struct iavf_adv_rss *
iavf_find_adv_rss_cfg_by_hdrs(struct iavf_adapter *adapter, u32 packet_hdrs)
{
	struct iavf_adv_rss *rss;

	list_for_each_entry(rss, &adapter->adv_rss_list_head, list)
		if (rss->packet_hdrs == packet_hdrs)
			return rss;

	return NULL;
}

/**
 * iavf_print_adv_rss_cfg
 * @adapter: pointer to the VF adapter structure
 * @rss: pointer to the advance RSS configuration to print
 * @action: the string description about how to handle the RSS
 * @result: the string description about the virtchnl result
 *
 * Print the advance RSS configuration
 **/
void
iavf_print_adv_rss_cfg(struct iavf_adapter *adapter, struct iavf_adv_rss *rss,
		       const char *action, const char *result)
{
	u32 packet_hdrs = rss->packet_hdrs;
	u64 hash_flds = rss->hash_flds;
	static char hash_opt[300];
	const char *proto;

	if (packet_hdrs & IAVF_ADV_RSS_FLOW_SEG_HDR_TCP)
		proto = "TCP";
	else if (packet_hdrs & IAVF_ADV_RSS_FLOW_SEG_HDR_UDP)
		proto = "UDP";
	else if (packet_hdrs & IAVF_ADV_RSS_FLOW_SEG_HDR_SCTP)
		proto = "SCTP";
	else
		return;

	memset(hash_opt, 0, sizeof(hash_opt));

	strcat(hash_opt, proto);
	if (packet_hdrs & IAVF_ADV_RSS_FLOW_SEG_HDR_IPV4)
		strcat(hash_opt, "v4 ");
	else
		strcat(hash_opt, "v6 ");

	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_IPV4_SA |
			 IAVF_ADV_RSS_HASH_FLD_IPV6_SA))
		strcat(hash_opt, "IP SA,");
	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_IPV4_DA |
			 IAVF_ADV_RSS_HASH_FLD_IPV6_DA))
		strcat(hash_opt, "IP DA,");
	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_TCP_SRC_PORT |
			 IAVF_ADV_RSS_HASH_FLD_UDP_SRC_PORT |
			 IAVF_ADV_RSS_HASH_FLD_SCTP_SRC_PORT))
		strcat(hash_opt, "src port,");
	if (hash_flds & (IAVF_ADV_RSS_HASH_FLD_TCP_DST_PORT |
			 IAVF_ADV_RSS_HASH_FLD_UDP_DST_PORT |
			 IAVF_ADV_RSS_HASH_FLD_SCTP_DST_PORT))
		strcat(hash_opt, "dst port,");

	if (!action)
		action = "";

	if (!result)
		result = "";

	dev_info(&adapter->pdev->dev, "%s %s %s\n", action, hash_opt, result);
}
