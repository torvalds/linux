// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021, Intel Corporation. */

#include "ice.h"
#include "ice_base.h"
#include "ice_lib.h"
#include "ice_flow.h"

#define to_fltr_conf_from_desc(p) \
	container_of(p, struct virtchnl_fdir_fltr_conf, input)

#define ICE_FLOW_PROF_TYPE_S	0
#define ICE_FLOW_PROF_TYPE_M	(0xFFFFFFFFULL << ICE_FLOW_PROF_TYPE_S)
#define ICE_FLOW_PROF_VSI_S	32
#define ICE_FLOW_PROF_VSI_M	(0xFFFFFFFFULL << ICE_FLOW_PROF_VSI_S)

/* Flow profile ID format:
 * [0:31] - flow type, flow + tun_offs
 * [32:63] - VSI index
 */
#define ICE_FLOW_PROF_FD(vsi, flow, tun_offs) \
	((u64)(((((flow) + (tun_offs)) & ICE_FLOW_PROF_TYPE_M)) | \
	      (((u64)(vsi) << ICE_FLOW_PROF_VSI_S) & ICE_FLOW_PROF_VSI_M)))

#define GTPU_TEID_OFFSET 4
#define GTPU_EH_QFI_OFFSET 1
#define GTPU_EH_QFI_MASK 0x3F
#define PFCP_S_OFFSET 0
#define PFCP_S_MASK 0x1
#define PFCP_PORT_NR 8805

#define FDIR_INSET_FLAG_ESP_S 0
#define FDIR_INSET_FLAG_ESP_M BIT_ULL(FDIR_INSET_FLAG_ESP_S)
#define FDIR_INSET_FLAG_ESP_UDP BIT_ULL(FDIR_INSET_FLAG_ESP_S)
#define FDIR_INSET_FLAG_ESP_IPSEC (0ULL << FDIR_INSET_FLAG_ESP_S)

enum ice_fdir_tunnel_type {
	ICE_FDIR_TUNNEL_TYPE_NONE = 0,
	ICE_FDIR_TUNNEL_TYPE_GTPU,
	ICE_FDIR_TUNNEL_TYPE_GTPU_EH,
};

struct virtchnl_fdir_fltr_conf {
	struct ice_fdir_fltr input;
	enum ice_fdir_tunnel_type ttype;
	u64 inset_flag;
	u32 flow_id;
};

static enum virtchnl_proto_hdr_type vc_pattern_ether[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4_tcp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_TCP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4_udp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_UDP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4_sctp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_SCTP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv6[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV6,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv6_tcp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV6,
	VIRTCHNL_PROTO_HDR_TCP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv6_udp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV6,
	VIRTCHNL_PROTO_HDR_UDP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv6_sctp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV6,
	VIRTCHNL_PROTO_HDR_SCTP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4_gtpu[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_UDP,
	VIRTCHNL_PROTO_HDR_GTPU_IP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4_gtpu_eh[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_UDP,
	VIRTCHNL_PROTO_HDR_GTPU_IP,
	VIRTCHNL_PROTO_HDR_GTPU_EH,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4_l2tpv3[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_L2TPV3,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv6_l2tpv3[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV6,
	VIRTCHNL_PROTO_HDR_L2TPV3,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4_esp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_ESP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv6_esp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV6,
	VIRTCHNL_PROTO_HDR_ESP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4_ah[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_AH,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv6_ah[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV6,
	VIRTCHNL_PROTO_HDR_AH,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4_nat_t_esp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_UDP,
	VIRTCHNL_PROTO_HDR_ESP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv6_nat_t_esp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV6,
	VIRTCHNL_PROTO_HDR_UDP,
	VIRTCHNL_PROTO_HDR_ESP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv4_pfcp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV4,
	VIRTCHNL_PROTO_HDR_UDP,
	VIRTCHNL_PROTO_HDR_PFCP,
	VIRTCHNL_PROTO_HDR_NONE,
};

static enum virtchnl_proto_hdr_type vc_pattern_ipv6_pfcp[] = {
	VIRTCHNL_PROTO_HDR_ETH,
	VIRTCHNL_PROTO_HDR_IPV6,
	VIRTCHNL_PROTO_HDR_UDP,
	VIRTCHNL_PROTO_HDR_PFCP,
	VIRTCHNL_PROTO_HDR_NONE,
};

struct virtchnl_fdir_pattern_match_item {
	enum virtchnl_proto_hdr_type *list;
	u64 input_set;
	u64 *meta;
};

static const struct virtchnl_fdir_pattern_match_item vc_fdir_pattern_os[] = {
	{vc_pattern_ipv4,                     0,         NULL},
	{vc_pattern_ipv4_tcp,                 0,         NULL},
	{vc_pattern_ipv4_udp,                 0,         NULL},
	{vc_pattern_ipv4_sctp,                0,         NULL},
	{vc_pattern_ipv6,                     0,         NULL},
	{vc_pattern_ipv6_tcp,                 0,         NULL},
	{vc_pattern_ipv6_udp,                 0,         NULL},
	{vc_pattern_ipv6_sctp,                0,         NULL},
};

static const struct virtchnl_fdir_pattern_match_item vc_fdir_pattern_comms[] = {
	{vc_pattern_ipv4,                     0,         NULL},
	{vc_pattern_ipv4_tcp,                 0,         NULL},
	{vc_pattern_ipv4_udp,                 0,         NULL},
	{vc_pattern_ipv4_sctp,                0,         NULL},
	{vc_pattern_ipv6,                     0,         NULL},
	{vc_pattern_ipv6_tcp,                 0,         NULL},
	{vc_pattern_ipv6_udp,                 0,         NULL},
	{vc_pattern_ipv6_sctp,                0,         NULL},
	{vc_pattern_ether,                    0,         NULL},
	{vc_pattern_ipv4_gtpu,                0,         NULL},
	{vc_pattern_ipv4_gtpu_eh,             0,         NULL},
	{vc_pattern_ipv4_l2tpv3,              0,         NULL},
	{vc_pattern_ipv6_l2tpv3,              0,         NULL},
	{vc_pattern_ipv4_esp,                 0,         NULL},
	{vc_pattern_ipv6_esp,                 0,         NULL},
	{vc_pattern_ipv4_ah,                  0,         NULL},
	{vc_pattern_ipv6_ah,                  0,         NULL},
	{vc_pattern_ipv4_nat_t_esp,           0,         NULL},
	{vc_pattern_ipv6_nat_t_esp,           0,         NULL},
	{vc_pattern_ipv4_pfcp,                0,         NULL},
	{vc_pattern_ipv6_pfcp,                0,         NULL},
};

struct virtchnl_fdir_inset_map {
	enum virtchnl_proto_hdr_field field;
	enum ice_flow_field fld;
	u64 flag;
	u64 mask;
};

static const struct virtchnl_fdir_inset_map fdir_inset_map[] = {
	{VIRTCHNL_PROTO_HDR_ETH_ETHERTYPE, ICE_FLOW_FIELD_IDX_ETH_TYPE, 0, 0},
	{VIRTCHNL_PROTO_HDR_IPV4_SRC, ICE_FLOW_FIELD_IDX_IPV4_SA, 0, 0},
	{VIRTCHNL_PROTO_HDR_IPV4_DST, ICE_FLOW_FIELD_IDX_IPV4_DA, 0, 0},
	{VIRTCHNL_PROTO_HDR_IPV4_DSCP, ICE_FLOW_FIELD_IDX_IPV4_DSCP, 0, 0},
	{VIRTCHNL_PROTO_HDR_IPV4_TTL, ICE_FLOW_FIELD_IDX_IPV4_TTL, 0, 0},
	{VIRTCHNL_PROTO_HDR_IPV4_PROT, ICE_FLOW_FIELD_IDX_IPV4_PROT, 0, 0},
	{VIRTCHNL_PROTO_HDR_IPV6_SRC, ICE_FLOW_FIELD_IDX_IPV6_SA, 0, 0},
	{VIRTCHNL_PROTO_HDR_IPV6_DST, ICE_FLOW_FIELD_IDX_IPV6_DA, 0, 0},
	{VIRTCHNL_PROTO_HDR_IPV6_TC, ICE_FLOW_FIELD_IDX_IPV6_DSCP, 0, 0},
	{VIRTCHNL_PROTO_HDR_IPV6_HOP_LIMIT, ICE_FLOW_FIELD_IDX_IPV6_TTL, 0, 0},
	{VIRTCHNL_PROTO_HDR_IPV6_PROT, ICE_FLOW_FIELD_IDX_IPV6_PROT, 0, 0},
	{VIRTCHNL_PROTO_HDR_UDP_SRC_PORT, ICE_FLOW_FIELD_IDX_UDP_SRC_PORT, 0, 0},
	{VIRTCHNL_PROTO_HDR_UDP_DST_PORT, ICE_FLOW_FIELD_IDX_UDP_DST_PORT, 0, 0},
	{VIRTCHNL_PROTO_HDR_TCP_SRC_PORT, ICE_FLOW_FIELD_IDX_TCP_SRC_PORT, 0, 0},
	{VIRTCHNL_PROTO_HDR_TCP_DST_PORT, ICE_FLOW_FIELD_IDX_TCP_DST_PORT, 0, 0},
	{VIRTCHNL_PROTO_HDR_SCTP_SRC_PORT, ICE_FLOW_FIELD_IDX_SCTP_SRC_PORT, 0, 0},
	{VIRTCHNL_PROTO_HDR_SCTP_DST_PORT, ICE_FLOW_FIELD_IDX_SCTP_DST_PORT, 0, 0},
	{VIRTCHNL_PROTO_HDR_GTPU_IP_TEID, ICE_FLOW_FIELD_IDX_GTPU_IP_TEID, 0, 0},
	{VIRTCHNL_PROTO_HDR_GTPU_EH_QFI, ICE_FLOW_FIELD_IDX_GTPU_EH_QFI, 0, 0},
	{VIRTCHNL_PROTO_HDR_ESP_SPI, ICE_FLOW_FIELD_IDX_ESP_SPI,
		FDIR_INSET_FLAG_ESP_IPSEC, FDIR_INSET_FLAG_ESP_M},
	{VIRTCHNL_PROTO_HDR_ESP_SPI, ICE_FLOW_FIELD_IDX_NAT_T_ESP_SPI,
		FDIR_INSET_FLAG_ESP_UDP, FDIR_INSET_FLAG_ESP_M},
	{VIRTCHNL_PROTO_HDR_AH_SPI, ICE_FLOW_FIELD_IDX_AH_SPI, 0, 0},
	{VIRTCHNL_PROTO_HDR_L2TPV3_SESS_ID, ICE_FLOW_FIELD_IDX_L2TPV3_SESS_ID, 0, 0},
	{VIRTCHNL_PROTO_HDR_PFCP_S_FIELD, ICE_FLOW_FIELD_IDX_UDP_DST_PORT, 0, 0},
};

/**
 * ice_vc_fdir_param_check
 * @vf: pointer to the VF structure
 * @vsi_id: VF relative VSI ID
 *
 * Check for the valid VSI ID, PF's state and VF's state
 *
 * Return: 0 on success, and -EINVAL on error.
 */
static int
ice_vc_fdir_param_check(struct ice_vf *vf, u16 vsi_id)
{
	struct ice_pf *pf = vf->pf;

	if (!test_bit(ICE_FLAG_FD_ENA, pf->flags))
		return -EINVAL;

	if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
		return -EINVAL;

	if (!(vf->driver_caps & VIRTCHNL_VF_OFFLOAD_FDIR_PF))
		return -EINVAL;

	if (vsi_id != vf->lan_vsi_num)
		return -EINVAL;

	if (!ice_vc_isvalid_vsi_id(vf, vsi_id))
		return -EINVAL;

	if (!pf->vsi[vf->lan_vsi_idx])
		return -EINVAL;

	return 0;
}

/**
 * ice_vf_start_ctrl_vsi
 * @vf: pointer to the VF structure
 *
 * Allocate ctrl_vsi for the first time and open the ctrl_vsi port for VF
 *
 * Return: 0 on success, and other on error.
 */
static int ice_vf_start_ctrl_vsi(struct ice_vf *vf)
{
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *ctrl_vsi;
	struct device *dev;
	int err;

	dev = ice_pf_to_dev(pf);
	if (vf->ctrl_vsi_idx != ICE_NO_VSI)
		return -EEXIST;

	ctrl_vsi = ice_vf_ctrl_vsi_setup(vf);
	if (!ctrl_vsi) {
		dev_dbg(dev, "Could not setup control VSI for VF %d\n",
			vf->vf_id);
		return -ENOMEM;
	}

	err = ice_vsi_open_ctrl(ctrl_vsi);
	if (err) {
		dev_dbg(dev, "Could not open control VSI for VF %d\n",
			vf->vf_id);
		goto err_vsi_open;
	}

	return 0;

err_vsi_open:
	ice_vsi_release(ctrl_vsi);
	if (vf->ctrl_vsi_idx != ICE_NO_VSI) {
		pf->vsi[vf->ctrl_vsi_idx] = NULL;
		vf->ctrl_vsi_idx = ICE_NO_VSI;
	}
	return err;
}

/**
 * ice_vc_fdir_alloc_prof - allocate profile for this filter flow type
 * @vf: pointer to the VF structure
 * @flow: filter flow type
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_fdir_alloc_prof(struct ice_vf *vf, enum ice_fltr_ptype flow)
{
	struct ice_vf_fdir *fdir = &vf->fdir;

	if (!fdir->fdir_prof) {
		fdir->fdir_prof = devm_kcalloc(ice_pf_to_dev(vf->pf),
					       ICE_FLTR_PTYPE_MAX,
					       sizeof(*fdir->fdir_prof),
					       GFP_KERNEL);
		if (!fdir->fdir_prof)
			return -ENOMEM;
	}

	if (!fdir->fdir_prof[flow]) {
		fdir->fdir_prof[flow] = devm_kzalloc(ice_pf_to_dev(vf->pf),
						     sizeof(**fdir->fdir_prof),
						     GFP_KERNEL);
		if (!fdir->fdir_prof[flow])
			return -ENOMEM;
	}

	return 0;
}

/**
 * ice_vc_fdir_free_prof - free profile for this filter flow type
 * @vf: pointer to the VF structure
 * @flow: filter flow type
 */
static void
ice_vc_fdir_free_prof(struct ice_vf *vf, enum ice_fltr_ptype flow)
{
	struct ice_vf_fdir *fdir = &vf->fdir;

	if (!fdir->fdir_prof)
		return;

	if (!fdir->fdir_prof[flow])
		return;

	devm_kfree(ice_pf_to_dev(vf->pf), fdir->fdir_prof[flow]);
	fdir->fdir_prof[flow] = NULL;
}

/**
 * ice_vc_fdir_free_prof_all - free all the profile for this VF
 * @vf: pointer to the VF structure
 */
static void ice_vc_fdir_free_prof_all(struct ice_vf *vf)
{
	struct ice_vf_fdir *fdir = &vf->fdir;
	enum ice_fltr_ptype flow;

	if (!fdir->fdir_prof)
		return;

	for (flow = ICE_FLTR_PTYPE_NONF_NONE; flow < ICE_FLTR_PTYPE_MAX; flow++)
		ice_vc_fdir_free_prof(vf, flow);

	devm_kfree(ice_pf_to_dev(vf->pf), fdir->fdir_prof);
	fdir->fdir_prof = NULL;
}

/**
 * ice_vc_fdir_parse_flow_fld
 * @proto_hdr: virtual channel protocol filter header
 * @conf: FDIR configuration for each filter
 * @fld: field type array
 * @fld_cnt: field counter
 *
 * Parse the virtual channel filter header and store them into field type array
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_fdir_parse_flow_fld(struct virtchnl_proto_hdr *proto_hdr,
			   struct virtchnl_fdir_fltr_conf *conf,
			   enum ice_flow_field *fld, int *fld_cnt)
{
	struct virtchnl_proto_hdr hdr;
	u32 i;

	memcpy(&hdr, proto_hdr, sizeof(hdr));

	for (i = 0; (i < ARRAY_SIZE(fdir_inset_map)) &&
	     VIRTCHNL_GET_PROTO_HDR_FIELD(&hdr); i++)
		if (VIRTCHNL_TEST_PROTO_HDR(&hdr, fdir_inset_map[i].field)) {
			if (fdir_inset_map[i].mask &&
			    ((fdir_inset_map[i].mask & conf->inset_flag) !=
			     fdir_inset_map[i].flag))
				continue;

			fld[*fld_cnt] = fdir_inset_map[i].fld;
			*fld_cnt += 1;
			if (*fld_cnt >= ICE_FLOW_FIELD_IDX_MAX)
				return -EINVAL;
			VIRTCHNL_DEL_PROTO_HDR_FIELD(&hdr,
						     fdir_inset_map[i].field);
		}

	return 0;
}

/**
 * ice_vc_fdir_set_flow_fld
 * @vf: pointer to the VF structure
 * @fltr: virtual channel add cmd buffer
 * @conf: FDIR configuration for each filter
 * @seg: array of one or more packet segments that describe the flow
 *
 * Parse the virtual channel add msg buffer's field vector and store them into
 * flow's packet segment field
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_fdir_set_flow_fld(struct ice_vf *vf, struct virtchnl_fdir_add *fltr,
			 struct virtchnl_fdir_fltr_conf *conf,
			 struct ice_flow_seg_info *seg)
{
	struct virtchnl_fdir_rule *rule = &fltr->rule_cfg;
	enum ice_flow_field fld[ICE_FLOW_FIELD_IDX_MAX];
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct virtchnl_proto_hdrs *proto;
	int fld_cnt = 0;
	int i;

	proto = &rule->proto_hdrs;
	for (i = 0; i < proto->count; i++) {
		struct virtchnl_proto_hdr *hdr = &proto->proto_hdr[i];
		int ret;

		ret = ice_vc_fdir_parse_flow_fld(hdr, conf, fld, &fld_cnt);
		if (ret)
			return ret;
	}

	if (fld_cnt == 0) {
		dev_dbg(dev, "Empty input set for VF %d\n", vf->vf_id);
		return -EINVAL;
	}

	for (i = 0; i < fld_cnt; i++)
		ice_flow_set_fld(seg, fld[i],
				 ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);

	return 0;
}

/**
 * ice_vc_fdir_set_flow_hdr - config the flow's packet segment header
 * @vf: pointer to the VF structure
 * @conf: FDIR configuration for each filter
 * @seg: array of one or more packet segments that describe the flow
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_fdir_set_flow_hdr(struct ice_vf *vf,
			 struct virtchnl_fdir_fltr_conf *conf,
			 struct ice_flow_seg_info *seg)
{
	enum ice_fltr_ptype flow = conf->input.flow_type;
	enum ice_fdir_tunnel_type ttype = conf->ttype;
	struct device *dev = ice_pf_to_dev(vf->pf);

	switch (flow) {
	case ICE_FLTR_PTYPE_NON_IP_L2:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_ETH_NON_IP);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_L2TPV3:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_L2TPV3 |
				  ICE_FLOW_SEG_HDR_IPV4 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_ESP:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_ESP |
				  ICE_FLOW_SEG_HDR_IPV4 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_AH:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_AH |
				  ICE_FLOW_SEG_HDR_IPV4 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_NAT_T_ESP:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_NAT_T_ESP |
				  ICE_FLOW_SEG_HDR_IPV4 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_PFCP_NODE:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_PFCP_NODE |
				  ICE_FLOW_SEG_HDR_IPV4 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_PFCP_SESSION:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_PFCP_SESSION |
				  ICE_FLOW_SEG_HDR_IPV4 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_OTHER:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_IPV4 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_TCP:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_TCP |
				  ICE_FLOW_SEG_HDR_IPV4 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_UDP:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_UDP |
				  ICE_FLOW_SEG_HDR_IPV4 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_GTPU_IPV4_UDP:
	case ICE_FLTR_PTYPE_NONF_IPV4_GTPU_IPV4_TCP:
	case ICE_FLTR_PTYPE_NONF_IPV4_GTPU_IPV4_ICMP:
	case ICE_FLTR_PTYPE_NONF_IPV4_GTPU_IPV4_OTHER:
		if (ttype == ICE_FDIR_TUNNEL_TYPE_GTPU) {
			ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_GTPU_IP |
					  ICE_FLOW_SEG_HDR_IPV4 |
					  ICE_FLOW_SEG_HDR_IPV_OTHER);
		} else if (ttype == ICE_FDIR_TUNNEL_TYPE_GTPU_EH) {
			ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_GTPU_EH |
					  ICE_FLOW_SEG_HDR_GTPU_IP |
					  ICE_FLOW_SEG_HDR_IPV4 |
					  ICE_FLOW_SEG_HDR_IPV_OTHER);
		} else {
			dev_dbg(dev, "Invalid tunnel type 0x%x for VF %d\n",
				flow, vf->vf_id);
			return -EINVAL;
		}
		break;
	case ICE_FLTR_PTYPE_NONF_IPV4_SCTP:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_SCTP |
				  ICE_FLOW_SEG_HDR_IPV4 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV6_L2TPV3:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_L2TPV3 |
				  ICE_FLOW_SEG_HDR_IPV6 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV6_ESP:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_ESP |
				  ICE_FLOW_SEG_HDR_IPV6 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV6_AH:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_AH |
				  ICE_FLOW_SEG_HDR_IPV6 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV6_NAT_T_ESP:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_NAT_T_ESP |
				  ICE_FLOW_SEG_HDR_IPV6 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV6_PFCP_NODE:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_PFCP_NODE |
				  ICE_FLOW_SEG_HDR_IPV6 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV6_PFCP_SESSION:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_PFCP_SESSION |
				  ICE_FLOW_SEG_HDR_IPV6 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV6_OTHER:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_IPV6 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV6_TCP:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_TCP |
				  ICE_FLOW_SEG_HDR_IPV6 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV6_UDP:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_UDP |
				  ICE_FLOW_SEG_HDR_IPV6 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	case ICE_FLTR_PTYPE_NONF_IPV6_SCTP:
		ICE_FLOW_SET_HDRS(seg, ICE_FLOW_SEG_HDR_SCTP |
				  ICE_FLOW_SEG_HDR_IPV6 |
				  ICE_FLOW_SEG_HDR_IPV_OTHER);
		break;
	default:
		dev_dbg(dev, "Invalid flow type 0x%x for VF %d failed\n",
			flow, vf->vf_id);
		return -EINVAL;
	}

	return 0;
}

/**
 * ice_vc_fdir_rem_prof - remove profile for this filter flow type
 * @vf: pointer to the VF structure
 * @flow: filter flow type
 * @tun: 0 implies non-tunnel type filter, 1 implies tunnel type filter
 */
static void
ice_vc_fdir_rem_prof(struct ice_vf *vf, enum ice_fltr_ptype flow, int tun)
{
	struct ice_vf_fdir *fdir = &vf->fdir;
	struct ice_fd_hw_prof *vf_prof;
	struct ice_pf *pf = vf->pf;
	struct ice_vsi *vf_vsi;
	struct device *dev;
	struct ice_hw *hw;
	u64 prof_id;
	int i;

	dev = ice_pf_to_dev(pf);
	hw = &pf->hw;
	if (!fdir->fdir_prof || !fdir->fdir_prof[flow])
		return;

	vf_prof = fdir->fdir_prof[flow];

	vf_vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vf_vsi) {
		dev_dbg(dev, "NULL vf %d vsi pointer\n", vf->vf_id);
		return;
	}

	if (!fdir->prof_entry_cnt[flow][tun])
		return;

	prof_id = ICE_FLOW_PROF_FD(vf_vsi->vsi_num,
				   flow, tun ? ICE_FLTR_PTYPE_MAX : 0);

	for (i = 0; i < fdir->prof_entry_cnt[flow][tun]; i++)
		if (vf_prof->entry_h[i][tun]) {
			u16 vsi_num = ice_get_hw_vsi_num(hw, vf_prof->vsi_h[i]);

			ice_rem_prof_id_flow(hw, ICE_BLK_FD, vsi_num, prof_id);
			ice_flow_rem_entry(hw, ICE_BLK_FD,
					   vf_prof->entry_h[i][tun]);
			vf_prof->entry_h[i][tun] = 0;
		}

	ice_flow_rem_prof(hw, ICE_BLK_FD, prof_id);
	devm_kfree(dev, vf_prof->fdir_seg[tun]);
	vf_prof->fdir_seg[tun] = NULL;

	for (i = 0; i < vf_prof->cnt; i++)
		vf_prof->vsi_h[i] = 0;

	fdir->prof_entry_cnt[flow][tun] = 0;
}

/**
 * ice_vc_fdir_rem_prof_all - remove profile for this VF
 * @vf: pointer to the VF structure
 */
static void ice_vc_fdir_rem_prof_all(struct ice_vf *vf)
{
	enum ice_fltr_ptype flow;

	for (flow = ICE_FLTR_PTYPE_NONF_NONE;
	     flow < ICE_FLTR_PTYPE_MAX; flow++) {
		ice_vc_fdir_rem_prof(vf, flow, 0);
		ice_vc_fdir_rem_prof(vf, flow, 1);
	}
}

/**
 * ice_vc_fdir_write_flow_prof
 * @vf: pointer to the VF structure
 * @flow: filter flow type
 * @seg: array of one or more packet segments that describe the flow
 * @tun: 0 implies non-tunnel type filter, 1 implies tunnel type filter
 *
 * Write the flow's profile config and packet segment into the hardware
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_fdir_write_flow_prof(struct ice_vf *vf, enum ice_fltr_ptype flow,
			    struct ice_flow_seg_info *seg, int tun)
{
	struct ice_vf_fdir *fdir = &vf->fdir;
	struct ice_vsi *vf_vsi, *ctrl_vsi;
	struct ice_flow_seg_info *old_seg;
	struct ice_flow_prof *prof = NULL;
	struct ice_fd_hw_prof *vf_prof;
	enum ice_status status;
	struct device *dev;
	struct ice_pf *pf;
	struct ice_hw *hw;
	u64 entry1_h = 0;
	u64 entry2_h = 0;
	u64 prof_id;
	int ret;

	pf = vf->pf;
	dev = ice_pf_to_dev(pf);
	hw = &pf->hw;
	vf_vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vf_vsi)
		return -EINVAL;

	ctrl_vsi = pf->vsi[vf->ctrl_vsi_idx];
	if (!ctrl_vsi)
		return -EINVAL;

	vf_prof = fdir->fdir_prof[flow];
	old_seg = vf_prof->fdir_seg[tun];
	if (old_seg) {
		if (!memcmp(old_seg, seg, sizeof(*seg))) {
			dev_dbg(dev, "Duplicated profile for VF %d!\n",
				vf->vf_id);
			return -EEXIST;
		}

		if (fdir->fdir_fltr_cnt[flow][tun]) {
			ret = -EINVAL;
			dev_dbg(dev, "Input set conflicts for VF %d\n",
				vf->vf_id);
			goto err_exit;
		}

		/* remove previously allocated profile */
		ice_vc_fdir_rem_prof(vf, flow, tun);
	}

	prof_id = ICE_FLOW_PROF_FD(vf_vsi->vsi_num, flow,
				   tun ? ICE_FLTR_PTYPE_MAX : 0);

	status = ice_flow_add_prof(hw, ICE_BLK_FD, ICE_FLOW_RX, prof_id, seg,
				   tun + 1, &prof);
	ret = ice_status_to_errno(status);
	if (ret) {
		dev_dbg(dev, "Could not add VSI flow 0x%x for VF %d\n",
			flow, vf->vf_id);
		goto err_exit;
	}

	status = ice_flow_add_entry(hw, ICE_BLK_FD, prof_id, vf_vsi->idx,
				    vf_vsi->idx, ICE_FLOW_PRIO_NORMAL,
				    seg, &entry1_h);
	ret = ice_status_to_errno(status);
	if (ret) {
		dev_dbg(dev, "Could not add flow 0x%x VSI entry for VF %d\n",
			flow, vf->vf_id);
		goto err_prof;
	}

	status = ice_flow_add_entry(hw, ICE_BLK_FD, prof_id, vf_vsi->idx,
				    ctrl_vsi->idx, ICE_FLOW_PRIO_NORMAL,
				    seg, &entry2_h);
	ret = ice_status_to_errno(status);
	if (ret) {
		dev_dbg(dev,
			"Could not add flow 0x%x Ctrl VSI entry for VF %d\n",
			flow, vf->vf_id);
		goto err_entry_1;
	}

	vf_prof->fdir_seg[tun] = seg;
	vf_prof->cnt = 0;
	fdir->prof_entry_cnt[flow][tun] = 0;

	vf_prof->entry_h[vf_prof->cnt][tun] = entry1_h;
	vf_prof->vsi_h[vf_prof->cnt] = vf_vsi->idx;
	vf_prof->cnt++;
	fdir->prof_entry_cnt[flow][tun]++;

	vf_prof->entry_h[vf_prof->cnt][tun] = entry2_h;
	vf_prof->vsi_h[vf_prof->cnt] = ctrl_vsi->idx;
	vf_prof->cnt++;
	fdir->prof_entry_cnt[flow][tun]++;

	return 0;

err_entry_1:
	ice_rem_prof_id_flow(hw, ICE_BLK_FD,
			     ice_get_hw_vsi_num(hw, vf_vsi->idx), prof_id);
	ice_flow_rem_entry(hw, ICE_BLK_FD, entry1_h);
err_prof:
	ice_flow_rem_prof(hw, ICE_BLK_FD, prof_id);
err_exit:
	return ret;
}

/**
 * ice_vc_fdir_config_input_set
 * @vf: pointer to the VF structure
 * @fltr: virtual channel add cmd buffer
 * @conf: FDIR configuration for each filter
 * @tun: 0 implies non-tunnel type filter, 1 implies tunnel type filter
 *
 * Config the input set type and value for virtual channel add msg buffer
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_fdir_config_input_set(struct ice_vf *vf, struct virtchnl_fdir_add *fltr,
			     struct virtchnl_fdir_fltr_conf *conf, int tun)
{
	struct ice_fdir_fltr *input = &conf->input;
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_flow_seg_info *seg;
	enum ice_fltr_ptype flow;
	int ret;

	flow = input->flow_type;
	ret = ice_vc_fdir_alloc_prof(vf, flow);
	if (ret) {
		dev_dbg(dev, "Alloc flow prof for VF %d failed\n", vf->vf_id);
		return ret;
	}

	seg = devm_kzalloc(dev, sizeof(*seg), GFP_KERNEL);
	if (!seg)
		return -ENOMEM;

	ret = ice_vc_fdir_set_flow_fld(vf, fltr, conf, seg);
	if (ret) {
		dev_dbg(dev, "Set flow field for VF %d failed\n", vf->vf_id);
		goto err_exit;
	}

	ret = ice_vc_fdir_set_flow_hdr(vf, conf, seg);
	if (ret) {
		dev_dbg(dev, "Set flow hdr for VF %d failed\n", vf->vf_id);
		goto err_exit;
	}

	ret = ice_vc_fdir_write_flow_prof(vf, flow, seg, tun);
	if (ret == -EEXIST) {
		devm_kfree(dev, seg);
	} else if (ret) {
		dev_dbg(dev, "Write flow profile for VF %d failed\n",
			vf->vf_id);
		goto err_exit;
	}

	return 0;

err_exit:
	devm_kfree(dev, seg);
	return ret;
}

/**
 * ice_vc_fdir_match_pattern
 * @fltr: virtual channel add cmd buffer
 * @type: virtual channel protocol filter header type
 *
 * Matching the header type by comparing fltr and type's value.
 *
 * Return: true on success, and false on error.
 */
static bool
ice_vc_fdir_match_pattern(struct virtchnl_fdir_add *fltr,
			  enum virtchnl_proto_hdr_type *type)
{
	struct virtchnl_proto_hdrs *proto = &fltr->rule_cfg.proto_hdrs;
	int i = 0;

	while ((i < proto->count) &&
	       (*type == proto->proto_hdr[i].type) &&
	       (*type != VIRTCHNL_PROTO_HDR_NONE)) {
		type++;
		i++;
	}

	return ((i == proto->count) && (*type == VIRTCHNL_PROTO_HDR_NONE));
}

/**
 * ice_vc_fdir_get_pattern - get while list pattern
 * @vf: pointer to the VF info
 * @len: filter list length
 *
 * Return: pointer to allowed filter list
 */
static const struct virtchnl_fdir_pattern_match_item *
ice_vc_fdir_get_pattern(struct ice_vf *vf, int *len)
{
	const struct virtchnl_fdir_pattern_match_item *item;
	struct ice_pf *pf = vf->pf;
	struct ice_hw *hw;

	hw = &pf->hw;
	if (!strncmp(hw->active_pkg_name, "ICE COMMS Package",
		     sizeof(hw->active_pkg_name))) {
		item = vc_fdir_pattern_comms;
		*len = ARRAY_SIZE(vc_fdir_pattern_comms);
	} else {
		item = vc_fdir_pattern_os;
		*len = ARRAY_SIZE(vc_fdir_pattern_os);
	}

	return item;
}

/**
 * ice_vc_fdir_search_pattern
 * @vf: pointer to the VF info
 * @fltr: virtual channel add cmd buffer
 *
 * Search for matched pattern from supported pattern list
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_fdir_search_pattern(struct ice_vf *vf, struct virtchnl_fdir_add *fltr)
{
	const struct virtchnl_fdir_pattern_match_item *pattern;
	int len, i;

	pattern = ice_vc_fdir_get_pattern(vf, &len);

	for (i = 0; i < len; i++)
		if (ice_vc_fdir_match_pattern(fltr, pattern[i].list))
			return 0;

	return -EINVAL;
}

/**
 * ice_vc_fdir_parse_pattern
 * @vf: pointer to the VF info
 * @fltr: virtual channel add cmd buffer
 * @conf: FDIR configuration for each filter
 *
 * Parse the virtual channel filter's pattern and store them into conf
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_fdir_parse_pattern(struct ice_vf *vf, struct virtchnl_fdir_add *fltr,
			  struct virtchnl_fdir_fltr_conf *conf)
{
	struct virtchnl_proto_hdrs *proto = &fltr->rule_cfg.proto_hdrs;
	enum virtchnl_proto_hdr_type l3 = VIRTCHNL_PROTO_HDR_NONE;
	enum virtchnl_proto_hdr_type l4 = VIRTCHNL_PROTO_HDR_NONE;
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_fdir_fltr *input = &conf->input;
	int i;

	if (proto->count > VIRTCHNL_MAX_NUM_PROTO_HDRS) {
		dev_dbg(dev, "Invalid protocol count:0x%x for VF %d\n",
			proto->count, vf->vf_id);
		return -EINVAL;
	}

	for (i = 0; i < proto->count; i++) {
		struct virtchnl_proto_hdr *hdr = &proto->proto_hdr[i];
		struct ip_esp_hdr *esph;
		struct ip_auth_hdr *ah;
		struct sctphdr *sctph;
		struct ipv6hdr *ip6h;
		struct udphdr *udph;
		struct tcphdr *tcph;
		struct ethhdr *eth;
		struct iphdr *iph;
		u8 s_field;
		u8 *rawh;

		switch (hdr->type) {
		case VIRTCHNL_PROTO_HDR_ETH:
			eth = (struct ethhdr *)hdr->buffer;
			input->flow_type = ICE_FLTR_PTYPE_NON_IP_L2;

			if (hdr->field_selector)
				input->ext_data.ether_type = eth->h_proto;
			break;
		case VIRTCHNL_PROTO_HDR_IPV4:
			iph = (struct iphdr *)hdr->buffer;
			l3 = VIRTCHNL_PROTO_HDR_IPV4;
			input->flow_type = ICE_FLTR_PTYPE_NONF_IPV4_OTHER;

			if (hdr->field_selector) {
				input->ip.v4.src_ip = iph->saddr;
				input->ip.v4.dst_ip = iph->daddr;
				input->ip.v4.tos = iph->tos;
				input->ip.v4.proto = iph->protocol;
			}
			break;
		case VIRTCHNL_PROTO_HDR_IPV6:
			ip6h = (struct ipv6hdr *)hdr->buffer;
			l3 = VIRTCHNL_PROTO_HDR_IPV6;
			input->flow_type = ICE_FLTR_PTYPE_NONF_IPV6_OTHER;

			if (hdr->field_selector) {
				memcpy(input->ip.v6.src_ip,
				       ip6h->saddr.in6_u.u6_addr8,
				       sizeof(ip6h->saddr));
				memcpy(input->ip.v6.dst_ip,
				       ip6h->daddr.in6_u.u6_addr8,
				       sizeof(ip6h->daddr));
				input->ip.v6.tc = ((u8)(ip6h->priority) << 4) |
						  (ip6h->flow_lbl[0] >> 4);
				input->ip.v6.proto = ip6h->nexthdr;
			}
			break;
		case VIRTCHNL_PROTO_HDR_TCP:
			tcph = (struct tcphdr *)hdr->buffer;
			if (l3 == VIRTCHNL_PROTO_HDR_IPV4)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV4_TCP;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV6)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV6_TCP;

			if (hdr->field_selector) {
				if (l3 == VIRTCHNL_PROTO_HDR_IPV4) {
					input->ip.v4.src_port = tcph->source;
					input->ip.v4.dst_port = tcph->dest;
				} else if (l3 == VIRTCHNL_PROTO_HDR_IPV6) {
					input->ip.v6.src_port = tcph->source;
					input->ip.v6.dst_port = tcph->dest;
				}
			}
			break;
		case VIRTCHNL_PROTO_HDR_UDP:
			udph = (struct udphdr *)hdr->buffer;
			if (l3 == VIRTCHNL_PROTO_HDR_IPV4)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV4_UDP;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV6)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV6_UDP;

			if (hdr->field_selector) {
				if (l3 == VIRTCHNL_PROTO_HDR_IPV4) {
					input->ip.v4.src_port = udph->source;
					input->ip.v4.dst_port = udph->dest;
				} else if (l3 == VIRTCHNL_PROTO_HDR_IPV6) {
					input->ip.v6.src_port = udph->source;
					input->ip.v6.dst_port = udph->dest;
				}
			}
			break;
		case VIRTCHNL_PROTO_HDR_SCTP:
			sctph = (struct sctphdr *)hdr->buffer;
			if (l3 == VIRTCHNL_PROTO_HDR_IPV4)
				input->flow_type =
					ICE_FLTR_PTYPE_NONF_IPV4_SCTP;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV6)
				input->flow_type =
					ICE_FLTR_PTYPE_NONF_IPV6_SCTP;

			if (hdr->field_selector) {
				if (l3 == VIRTCHNL_PROTO_HDR_IPV4) {
					input->ip.v4.src_port = sctph->source;
					input->ip.v4.dst_port = sctph->dest;
				} else if (l3 == VIRTCHNL_PROTO_HDR_IPV6) {
					input->ip.v6.src_port = sctph->source;
					input->ip.v6.dst_port = sctph->dest;
				}
			}
			break;
		case VIRTCHNL_PROTO_HDR_L2TPV3:
			if (l3 == VIRTCHNL_PROTO_HDR_IPV4)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV4_L2TPV3;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV6)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV6_L2TPV3;

			if (hdr->field_selector)
				input->l2tpv3_data.session_id = *((__be32 *)hdr->buffer);
			break;
		case VIRTCHNL_PROTO_HDR_ESP:
			esph = (struct ip_esp_hdr *)hdr->buffer;
			if (l3 == VIRTCHNL_PROTO_HDR_IPV4 &&
			    l4 == VIRTCHNL_PROTO_HDR_UDP)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV4_NAT_T_ESP;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV6 &&
				 l4 == VIRTCHNL_PROTO_HDR_UDP)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV6_NAT_T_ESP;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV4 &&
				 l4 == VIRTCHNL_PROTO_HDR_NONE)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV4_ESP;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV6 &&
				 l4 == VIRTCHNL_PROTO_HDR_NONE)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV6_ESP;

			if (l4 == VIRTCHNL_PROTO_HDR_UDP)
				conf->inset_flag |= FDIR_INSET_FLAG_ESP_UDP;
			else
				conf->inset_flag |= FDIR_INSET_FLAG_ESP_IPSEC;

			if (hdr->field_selector) {
				if (l3 == VIRTCHNL_PROTO_HDR_IPV4)
					input->ip.v4.sec_parm_idx = esph->spi;
				else if (l3 == VIRTCHNL_PROTO_HDR_IPV6)
					input->ip.v6.sec_parm_idx = esph->spi;
			}
			break;
		case VIRTCHNL_PROTO_HDR_AH:
			ah = (struct ip_auth_hdr *)hdr->buffer;
			if (l3 == VIRTCHNL_PROTO_HDR_IPV4)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV4_AH;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV6)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV6_AH;

			if (hdr->field_selector) {
				if (l3 == VIRTCHNL_PROTO_HDR_IPV4)
					input->ip.v4.sec_parm_idx = ah->spi;
				else if (l3 == VIRTCHNL_PROTO_HDR_IPV6)
					input->ip.v6.sec_parm_idx = ah->spi;
			}
			break;
		case VIRTCHNL_PROTO_HDR_PFCP:
			rawh = (u8 *)hdr->buffer;
			s_field = (rawh[0] >> PFCP_S_OFFSET) & PFCP_S_MASK;
			if (l3 == VIRTCHNL_PROTO_HDR_IPV4 && s_field == 0)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV4_PFCP_NODE;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV4 && s_field == 1)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV4_PFCP_SESSION;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV6 && s_field == 0)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV6_PFCP_NODE;
			else if (l3 == VIRTCHNL_PROTO_HDR_IPV6 && s_field == 1)
				input->flow_type = ICE_FLTR_PTYPE_NONF_IPV6_PFCP_SESSION;

			if (hdr->field_selector) {
				if (l3 == VIRTCHNL_PROTO_HDR_IPV4)
					input->ip.v4.dst_port = cpu_to_be16(PFCP_PORT_NR);
				else if (l3 == VIRTCHNL_PROTO_HDR_IPV6)
					input->ip.v6.dst_port = cpu_to_be16(PFCP_PORT_NR);
			}
			break;
		case VIRTCHNL_PROTO_HDR_GTPU_IP:
			rawh = (u8 *)hdr->buffer;
			input->flow_type = ICE_FLTR_PTYPE_NONF_IPV4_GTPU_IPV4_OTHER;

			if (hdr->field_selector)
				input->gtpu_data.teid = *(__be32 *)(&rawh[GTPU_TEID_OFFSET]);
			conf->ttype = ICE_FDIR_TUNNEL_TYPE_GTPU;
			break;
		case VIRTCHNL_PROTO_HDR_GTPU_EH:
			rawh = (u8 *)hdr->buffer;

			if (hdr->field_selector)
				input->gtpu_data.qfi = rawh[GTPU_EH_QFI_OFFSET] & GTPU_EH_QFI_MASK;
			conf->ttype = ICE_FDIR_TUNNEL_TYPE_GTPU_EH;
			break;
		default:
			dev_dbg(dev, "Invalid header type 0x:%x for VF %d\n",
				hdr->type, vf->vf_id);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * ice_vc_fdir_parse_action
 * @vf: pointer to the VF info
 * @fltr: virtual channel add cmd buffer
 * @conf: FDIR configuration for each filter
 *
 * Parse the virtual channel filter's action and store them into conf
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_fdir_parse_action(struct ice_vf *vf, struct virtchnl_fdir_add *fltr,
			 struct virtchnl_fdir_fltr_conf *conf)
{
	struct virtchnl_filter_action_set *as = &fltr->rule_cfg.action_set;
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_fdir_fltr *input = &conf->input;
	u32 dest_num = 0;
	u32 mark_num = 0;
	int i;

	if (as->count > VIRTCHNL_MAX_NUM_ACTIONS) {
		dev_dbg(dev, "Invalid action numbers:0x%x for VF %d\n",
			as->count, vf->vf_id);
		return -EINVAL;
	}

	for (i = 0; i < as->count; i++) {
		struct virtchnl_filter_action *action = &as->actions[i];

		switch (action->type) {
		case VIRTCHNL_ACTION_PASSTHRU:
			dest_num++;
			input->dest_ctl = ICE_FLTR_PRGM_DESC_DEST_DIRECT_PKT_OTHER;
			break;
		case VIRTCHNL_ACTION_DROP:
			dest_num++;
			input->dest_ctl = ICE_FLTR_PRGM_DESC_DEST_DROP_PKT;
			break;
		case VIRTCHNL_ACTION_QUEUE:
			dest_num++;
			input->dest_ctl = ICE_FLTR_PRGM_DESC_DEST_DIRECT_PKT_QINDEX;
			input->q_index = action->act_conf.queue.index;
			break;
		case VIRTCHNL_ACTION_Q_REGION:
			dest_num++;
			input->dest_ctl = ICE_FLTR_PRGM_DESC_DEST_DIRECT_PKT_QGROUP;
			input->q_index = action->act_conf.queue.index;
			input->q_region = action->act_conf.queue.region;
			break;
		case VIRTCHNL_ACTION_MARK:
			mark_num++;
			input->fltr_id = action->act_conf.mark_id;
			input->fdid_prio = ICE_FXD_FLTR_QW1_FDID_PRI_THREE;
			break;
		default:
			dev_dbg(dev, "Invalid action type:0x%x for VF %d\n",
				action->type, vf->vf_id);
			return -EINVAL;
		}
	}

	if (dest_num == 0 || dest_num >= 2) {
		dev_dbg(dev, "Invalid destination action for VF %d\n",
			vf->vf_id);
		return -EINVAL;
	}

	if (mark_num >= 2) {
		dev_dbg(dev, "Too many mark actions for VF %d\n", vf->vf_id);
		return -EINVAL;
	}

	return 0;
}

/**
 * ice_vc_validate_fdir_fltr - validate the virtual channel filter
 * @vf: pointer to the VF info
 * @fltr: virtual channel add cmd buffer
 * @conf: FDIR configuration for each filter
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_validate_fdir_fltr(struct ice_vf *vf, struct virtchnl_fdir_add *fltr,
			  struct virtchnl_fdir_fltr_conf *conf)
{
	int ret;

	ret = ice_vc_fdir_search_pattern(vf, fltr);
	if (ret)
		return ret;

	ret = ice_vc_fdir_parse_pattern(vf, fltr, conf);
	if (ret)
		return ret;

	return ice_vc_fdir_parse_action(vf, fltr, conf);
}

/**
 * ice_vc_fdir_comp_rules - compare if two filter rules have the same value
 * @conf_a: FDIR configuration for filter a
 * @conf_b: FDIR configuration for filter b
 *
 * Return: 0 on success, and other on error.
 */
static bool
ice_vc_fdir_comp_rules(struct virtchnl_fdir_fltr_conf *conf_a,
		       struct virtchnl_fdir_fltr_conf *conf_b)
{
	struct ice_fdir_fltr *a = &conf_a->input;
	struct ice_fdir_fltr *b = &conf_b->input;

	if (conf_a->ttype != conf_b->ttype)
		return false;
	if (a->flow_type != b->flow_type)
		return false;
	if (memcmp(&a->ip, &b->ip, sizeof(a->ip)))
		return false;
	if (memcmp(&a->mask, &b->mask, sizeof(a->mask)))
		return false;
	if (memcmp(&a->gtpu_data, &b->gtpu_data, sizeof(a->gtpu_data)))
		return false;
	if (memcmp(&a->gtpu_mask, &b->gtpu_mask, sizeof(a->gtpu_mask)))
		return false;
	if (memcmp(&a->l2tpv3_data, &b->l2tpv3_data, sizeof(a->l2tpv3_data)))
		return false;
	if (memcmp(&a->l2tpv3_mask, &b->l2tpv3_mask, sizeof(a->l2tpv3_mask)))
		return false;
	if (memcmp(&a->ext_data, &b->ext_data, sizeof(a->ext_data)))
		return false;
	if (memcmp(&a->ext_mask, &b->ext_mask, sizeof(a->ext_mask)))
		return false;

	return true;
}

/**
 * ice_vc_fdir_is_dup_fltr
 * @vf: pointer to the VF info
 * @conf: FDIR configuration for each filter
 *
 * Check if there is duplicated rule with same conf value
 *
 * Return: 0 true success, and false on error.
 */
static bool
ice_vc_fdir_is_dup_fltr(struct ice_vf *vf, struct virtchnl_fdir_fltr_conf *conf)
{
	struct ice_fdir_fltr *desc;
	bool ret;

	list_for_each_entry(desc, &vf->fdir.fdir_rule_list, fltr_node) {
		struct virtchnl_fdir_fltr_conf *node =
				to_fltr_conf_from_desc(desc);

		ret = ice_vc_fdir_comp_rules(node, conf);
		if (ret)
			return true;
	}

	return false;
}

/**
 * ice_vc_fdir_insert_entry
 * @vf: pointer to the VF info
 * @conf: FDIR configuration for each filter
 * @id: pointer to ID value allocated by driver
 *
 * Insert FDIR conf entry into list and allocate ID for this filter
 *
 * Return: 0 true success, and other on error.
 */
static int
ice_vc_fdir_insert_entry(struct ice_vf *vf,
			 struct virtchnl_fdir_fltr_conf *conf, u32 *id)
{
	struct ice_fdir_fltr *input = &conf->input;
	int i;

	/* alloc ID corresponding with conf */
	i = idr_alloc(&vf->fdir.fdir_rule_idr, conf, 0,
		      ICE_FDIR_MAX_FLTRS, GFP_KERNEL);
	if (i < 0)
		return -EINVAL;
	*id = i;

	list_add(&input->fltr_node, &vf->fdir.fdir_rule_list);
	return 0;
}

/**
 * ice_vc_fdir_remove_entry - remove FDIR conf entry by ID value
 * @vf: pointer to the VF info
 * @conf: FDIR configuration for each filter
 * @id: filter rule's ID
 */
static void
ice_vc_fdir_remove_entry(struct ice_vf *vf,
			 struct virtchnl_fdir_fltr_conf *conf, u32 id)
{
	struct ice_fdir_fltr *input = &conf->input;

	idr_remove(&vf->fdir.fdir_rule_idr, id);
	list_del(&input->fltr_node);
}

/**
 * ice_vc_fdir_lookup_entry - lookup FDIR conf entry by ID value
 * @vf: pointer to the VF info
 * @id: filter rule's ID
 *
 * Return: NULL on error, and other on success.
 */
static struct virtchnl_fdir_fltr_conf *
ice_vc_fdir_lookup_entry(struct ice_vf *vf, u32 id)
{
	return idr_find(&vf->fdir.fdir_rule_idr, id);
}

/**
 * ice_vc_fdir_flush_entry - remove all FDIR conf entry
 * @vf: pointer to the VF info
 */
static void ice_vc_fdir_flush_entry(struct ice_vf *vf)
{
	struct virtchnl_fdir_fltr_conf *conf;
	struct ice_fdir_fltr *desc, *temp;

	list_for_each_entry_safe(desc, temp,
				 &vf->fdir.fdir_rule_list, fltr_node) {
		conf = to_fltr_conf_from_desc(desc);
		list_del(&desc->fltr_node);
		devm_kfree(ice_pf_to_dev(vf->pf), conf);
	}
}

/**
 * ice_vc_fdir_write_fltr - write filter rule into hardware
 * @vf: pointer to the VF info
 * @conf: FDIR configuration for each filter
 * @add: true implies add rule, false implies del rules
 * @is_tun: false implies non-tunnel type filter, true implies tunnel filter
 *
 * Return: 0 on success, and other on error.
 */
static int ice_vc_fdir_write_fltr(struct ice_vf *vf,
				  struct virtchnl_fdir_fltr_conf *conf,
				  bool add, bool is_tun)
{
	struct ice_fdir_fltr *input = &conf->input;
	struct ice_vsi *vsi, *ctrl_vsi;
	struct ice_fltr_desc desc;
	enum ice_status status;
	struct device *dev;
	struct ice_pf *pf;
	struct ice_hw *hw;
	int ret;
	u8 *pkt;

	pf = vf->pf;
	dev = ice_pf_to_dev(pf);
	hw = &pf->hw;
	vsi = pf->vsi[vf->lan_vsi_idx];
	if (!vsi) {
		dev_dbg(dev, "Invalid vsi for VF %d\n", vf->vf_id);
		return -EINVAL;
	}

	input->dest_vsi = vsi->idx;
	input->comp_report = ICE_FXD_FLTR_QW0_COMP_REPORT_SW;

	ctrl_vsi = pf->vsi[vf->ctrl_vsi_idx];
	if (!ctrl_vsi) {
		dev_dbg(dev, "Invalid ctrl_vsi for VF %d\n", vf->vf_id);
		return -EINVAL;
	}

	pkt = devm_kzalloc(dev, ICE_FDIR_MAX_RAW_PKT_SIZE, GFP_KERNEL);
	if (!pkt)
		return -ENOMEM;

	ice_fdir_get_prgm_desc(hw, input, &desc, add);
	status = ice_fdir_get_gen_prgm_pkt(hw, input, pkt, false, is_tun);
	ret = ice_status_to_errno(status);
	if (ret) {
		dev_dbg(dev, "Gen training pkt for VF %d ptype %d failed\n",
			vf->vf_id, input->flow_type);
		goto err_free_pkt;
	}

	ret = ice_prgm_fdir_fltr(ctrl_vsi, &desc, pkt);
	if (ret)
		goto err_free_pkt;

	return 0;

err_free_pkt:
	devm_kfree(dev, pkt);
	return ret;
}

/**
 * ice_vf_fdir_timer - FDIR program waiting timer interrupt handler
 * @t: pointer to timer_list
 */
static void ice_vf_fdir_timer(struct timer_list *t)
{
	struct ice_vf_fdir_ctx *ctx_irq = from_timer(ctx_irq, t, rx_tmr);
	struct ice_vf_fdir_ctx *ctx_done;
	struct ice_vf_fdir *fdir;
	unsigned long flags;
	struct ice_vf *vf;
	struct ice_pf *pf;

	fdir = container_of(ctx_irq, struct ice_vf_fdir, ctx_irq);
	vf = container_of(fdir, struct ice_vf, fdir);
	ctx_done = &fdir->ctx_done;
	pf = vf->pf;
	spin_lock_irqsave(&fdir->ctx_lock, flags);
	if (!(ctx_irq->flags & ICE_VF_FDIR_CTX_VALID)) {
		spin_unlock_irqrestore(&fdir->ctx_lock, flags);
		WARN_ON_ONCE(1);
		return;
	}

	ctx_irq->flags &= ~ICE_VF_FDIR_CTX_VALID;

	ctx_done->flags |= ICE_VF_FDIR_CTX_VALID;
	ctx_done->conf = ctx_irq->conf;
	ctx_done->stat = ICE_FDIR_CTX_TIMEOUT;
	ctx_done->v_opcode = ctx_irq->v_opcode;
	spin_unlock_irqrestore(&fdir->ctx_lock, flags);

	set_bit(__ICE_FD_VF_FLUSH_CTX, pf->state);
	ice_service_task_schedule(pf);
}

/**
 * ice_vc_fdir_irq_handler - ctrl_vsi Rx queue interrupt handler
 * @ctrl_vsi: pointer to a VF's CTRL VSI
 * @rx_desc: pointer to FDIR Rx queue descriptor
 */
void
ice_vc_fdir_irq_handler(struct ice_vsi *ctrl_vsi,
			union ice_32b_rx_flex_desc *rx_desc)
{
	struct ice_pf *pf = ctrl_vsi->back;
	struct ice_vf_fdir_ctx *ctx_done;
	struct ice_vf_fdir_ctx *ctx_irq;
	struct ice_vf_fdir *fdir;
	unsigned long flags;
	struct device *dev;
	struct ice_vf *vf;
	int ret;

	vf = &pf->vf[ctrl_vsi->vf_id];

	fdir = &vf->fdir;
	ctx_done = &fdir->ctx_done;
	ctx_irq = &fdir->ctx_irq;
	dev = ice_pf_to_dev(pf);
	spin_lock_irqsave(&fdir->ctx_lock, flags);
	if (!(ctx_irq->flags & ICE_VF_FDIR_CTX_VALID)) {
		spin_unlock_irqrestore(&fdir->ctx_lock, flags);
		WARN_ON_ONCE(1);
		return;
	}

	ctx_irq->flags &= ~ICE_VF_FDIR_CTX_VALID;

	ctx_done->flags |= ICE_VF_FDIR_CTX_VALID;
	ctx_done->conf = ctx_irq->conf;
	ctx_done->stat = ICE_FDIR_CTX_IRQ;
	ctx_done->v_opcode = ctx_irq->v_opcode;
	memcpy(&ctx_done->rx_desc, rx_desc, sizeof(*rx_desc));
	spin_unlock_irqrestore(&fdir->ctx_lock, flags);

	ret = del_timer(&ctx_irq->rx_tmr);
	if (!ret)
		dev_err(dev, "VF %d: Unexpected inactive timer!\n", vf->vf_id);

	set_bit(__ICE_FD_VF_FLUSH_CTX, pf->state);
	ice_service_task_schedule(pf);
}

/**
 * ice_vf_fdir_dump_info - dump FDIR information for diagnosis
 * @vf: pointer to the VF info
 */
static void ice_vf_fdir_dump_info(struct ice_vf *vf)
{
	struct ice_vsi *vf_vsi;
	u32 fd_size, fd_cnt;
	struct device *dev;
	struct ice_pf *pf;
	struct ice_hw *hw;
	u16 vsi_num;

	pf = vf->pf;
	hw = &pf->hw;
	dev = ice_pf_to_dev(pf);
	vf_vsi = pf->vsi[vf->lan_vsi_idx];
	vsi_num = ice_get_hw_vsi_num(hw, vf_vsi->idx);

	fd_size = rd32(hw, VSIQF_FD_SIZE(vsi_num));
	fd_cnt = rd32(hw, VSIQF_FD_CNT(vsi_num));
	dev_dbg(dev, "VF %d: space allocated: guar:0x%x, be:0x%x, space consumed: guar:0x%x, be:0x%x",
		vf->vf_id,
		(fd_size & VSIQF_FD_CNT_FD_GCNT_M) >> VSIQF_FD_CNT_FD_GCNT_S,
		(fd_size & VSIQF_FD_CNT_FD_BCNT_M) >> VSIQF_FD_CNT_FD_BCNT_S,
		(fd_cnt & VSIQF_FD_CNT_FD_GCNT_M) >> VSIQF_FD_CNT_FD_GCNT_S,
		(fd_cnt & VSIQF_FD_CNT_FD_BCNT_M) >> VSIQF_FD_CNT_FD_BCNT_S);
}

/**
 * ice_vf_verify_rx_desc - verify received FDIR programming status descriptor
 * @vf: pointer to the VF info
 * @ctx: FDIR context info for post processing
 * @status: virtchnl FDIR program status
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vf_verify_rx_desc(struct ice_vf *vf, struct ice_vf_fdir_ctx *ctx,
		      enum virtchnl_fdir_prgm_status *status)
{
	struct device *dev = ice_pf_to_dev(vf->pf);
	u32 stat_err, error, prog_id;
	int ret;

	stat_err = le16_to_cpu(ctx->rx_desc.wb.status_error0);
	if (((stat_err & ICE_FXD_FLTR_WB_QW1_DD_M) >>
	    ICE_FXD_FLTR_WB_QW1_DD_S) != ICE_FXD_FLTR_WB_QW1_DD_YES) {
		*status = VIRTCHNL_FDIR_FAILURE_RULE_NORESOURCE;
		dev_err(dev, "VF %d: Desc Done not set\n", vf->vf_id);
		ret = -EINVAL;
		goto err_exit;
	}

	prog_id = (stat_err & ICE_FXD_FLTR_WB_QW1_PROG_ID_M) >>
		ICE_FXD_FLTR_WB_QW1_PROG_ID_S;
	if (prog_id == ICE_FXD_FLTR_WB_QW1_PROG_ADD &&
	    ctx->v_opcode != VIRTCHNL_OP_ADD_FDIR_FILTER) {
		dev_err(dev, "VF %d: Desc show add, but ctx not",
			vf->vf_id);
		*status = VIRTCHNL_FDIR_FAILURE_RULE_INVALID;
		ret = -EINVAL;
		goto err_exit;
	}

	if (prog_id == ICE_FXD_FLTR_WB_QW1_PROG_DEL &&
	    ctx->v_opcode != VIRTCHNL_OP_DEL_FDIR_FILTER) {
		dev_err(dev, "VF %d: Desc show del, but ctx not",
			vf->vf_id);
		*status = VIRTCHNL_FDIR_FAILURE_RULE_INVALID;
		ret = -EINVAL;
		goto err_exit;
	}

	error = (stat_err & ICE_FXD_FLTR_WB_QW1_FAIL_M) >>
		ICE_FXD_FLTR_WB_QW1_FAIL_S;
	if (error == ICE_FXD_FLTR_WB_QW1_FAIL_YES) {
		if (prog_id == ICE_FXD_FLTR_WB_QW1_PROG_ADD) {
			dev_err(dev, "VF %d, Failed to add FDIR rule due to no space in the table",
				vf->vf_id);
			*status = VIRTCHNL_FDIR_FAILURE_RULE_NORESOURCE;
		} else {
			dev_err(dev, "VF %d, Failed to remove FDIR rule, attempt to remove non-existent entry",
				vf->vf_id);
			*status = VIRTCHNL_FDIR_FAILURE_RULE_NONEXIST;
		}
		ret = -EINVAL;
		goto err_exit;
	}

	error = (stat_err & ICE_FXD_FLTR_WB_QW1_FAIL_PROF_M) >>
		ICE_FXD_FLTR_WB_QW1_FAIL_PROF_S;
	if (error == ICE_FXD_FLTR_WB_QW1_FAIL_PROF_YES) {
		dev_err(dev, "VF %d: Profile matching error", vf->vf_id);
		*status = VIRTCHNL_FDIR_FAILURE_RULE_NORESOURCE;
		ret = -EINVAL;
		goto err_exit;
	}

	*status = VIRTCHNL_FDIR_SUCCESS;

	return 0;

err_exit:
	ice_vf_fdir_dump_info(vf);
	return ret;
}

/**
 * ice_vc_add_fdir_fltr_post
 * @vf: pointer to the VF structure
 * @ctx: FDIR context info for post processing
 * @status: virtchnl FDIR program status
 * @success: true implies success, false implies failure
 *
 * Post process for flow director add command. If success, then do post process
 * and send back success msg by virtchnl. Otherwise, do context reversion and
 * send back failure msg by virtchnl.
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_add_fdir_fltr_post(struct ice_vf *vf, struct ice_vf_fdir_ctx *ctx,
			  enum virtchnl_fdir_prgm_status status,
			  bool success)
{
	struct virtchnl_fdir_fltr_conf *conf = ctx->conf;
	struct device *dev = ice_pf_to_dev(vf->pf);
	enum virtchnl_status_code v_ret;
	struct virtchnl_fdir_add *resp;
	int ret, len, is_tun;

	v_ret = VIRTCHNL_STATUS_SUCCESS;
	len = sizeof(*resp);
	resp = kzalloc(len, GFP_KERNEL);
	if (!resp) {
		len = 0;
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		dev_dbg(dev, "VF %d: Alloc resp buf fail", vf->vf_id);
		goto err_exit;
	}

	if (!success)
		goto err_exit;

	is_tun = 0;
	resp->status = status;
	resp->flow_id = conf->flow_id;
	vf->fdir.fdir_fltr_cnt[conf->input.flow_type][is_tun]++;

	ret = ice_vc_send_msg_to_vf(vf, ctx->v_opcode, v_ret,
				    (u8 *)resp, len);
	kfree(resp);

	dev_dbg(dev, "VF %d: flow_id:0x%X, FDIR %s success!\n",
		vf->vf_id, conf->flow_id,
		(ctx->v_opcode == VIRTCHNL_OP_ADD_FDIR_FILTER) ?
		"add" : "del");
	return ret;

err_exit:
	if (resp)
		resp->status = status;
	ice_vc_fdir_remove_entry(vf, conf, conf->flow_id);
	devm_kfree(dev, conf);

	ret = ice_vc_send_msg_to_vf(vf, ctx->v_opcode, v_ret,
				    (u8 *)resp, len);
	kfree(resp);
	return ret;
}

/**
 * ice_vc_del_fdir_fltr_post
 * @vf: pointer to the VF structure
 * @ctx: FDIR context info for post processing
 * @status: virtchnl FDIR program status
 * @success: true implies success, false implies failure
 *
 * Post process for flow director del command. If success, then do post process
 * and send back success msg by virtchnl. Otherwise, do context reversion and
 * send back failure msg by virtchnl.
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_del_fdir_fltr_post(struct ice_vf *vf, struct ice_vf_fdir_ctx *ctx,
			  enum virtchnl_fdir_prgm_status status,
			  bool success)
{
	struct virtchnl_fdir_fltr_conf *conf = ctx->conf;
	struct device *dev = ice_pf_to_dev(vf->pf);
	enum virtchnl_status_code v_ret;
	struct virtchnl_fdir_del *resp;
	int ret, len, is_tun;

	v_ret = VIRTCHNL_STATUS_SUCCESS;
	len = sizeof(*resp);
	resp = kzalloc(len, GFP_KERNEL);
	if (!resp) {
		len = 0;
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		dev_dbg(dev, "VF %d: Alloc resp buf fail", vf->vf_id);
		goto err_exit;
	}

	if (!success)
		goto err_exit;

	is_tun = 0;
	resp->status = status;
	ice_vc_fdir_remove_entry(vf, conf, conf->flow_id);
	vf->fdir.fdir_fltr_cnt[conf->input.flow_type][is_tun]--;

	ret = ice_vc_send_msg_to_vf(vf, ctx->v_opcode, v_ret,
				    (u8 *)resp, len);
	kfree(resp);

	dev_dbg(dev, "VF %d: flow_id:0x%X, FDIR %s success!\n",
		vf->vf_id, conf->flow_id,
		(ctx->v_opcode == VIRTCHNL_OP_ADD_FDIR_FILTER) ?
		"add" : "del");
	devm_kfree(dev, conf);
	return ret;

err_exit:
	if (resp)
		resp->status = status;
	if (success)
		devm_kfree(dev, conf);

	ret = ice_vc_send_msg_to_vf(vf, ctx->v_opcode, v_ret,
				    (u8 *)resp, len);
	kfree(resp);
	return ret;
}

/**
 * ice_flush_fdir_ctx
 * @pf: pointer to the PF structure
 *
 * Flush all the pending event on ctx_done list and process them.
 */
void ice_flush_fdir_ctx(struct ice_pf *pf)
{
	int i;

	if (!test_and_clear_bit(__ICE_FD_VF_FLUSH_CTX, pf->state))
		return;

	ice_for_each_vf(pf, i) {
		struct device *dev = ice_pf_to_dev(pf);
		enum virtchnl_fdir_prgm_status status;
		struct ice_vf *vf = &pf->vf[i];
		struct ice_vf_fdir_ctx *ctx;
		unsigned long flags;
		int ret;

		if (!test_bit(ICE_VF_STATE_ACTIVE, vf->vf_states))
			continue;

		if (vf->ctrl_vsi_idx == ICE_NO_VSI)
			continue;

		ctx = &vf->fdir.ctx_done;
		spin_lock_irqsave(&vf->fdir.ctx_lock, flags);
		if (!(ctx->flags & ICE_VF_FDIR_CTX_VALID)) {
			spin_unlock_irqrestore(&vf->fdir.ctx_lock, flags);
			continue;
		}
		spin_unlock_irqrestore(&vf->fdir.ctx_lock, flags);

		WARN_ON(ctx->stat == ICE_FDIR_CTX_READY);
		if (ctx->stat == ICE_FDIR_CTX_TIMEOUT) {
			status = VIRTCHNL_FDIR_FAILURE_RULE_TIMEOUT;
			dev_err(dev, "VF %d: ctrl_vsi irq timeout\n",
				vf->vf_id);
			goto err_exit;
		}

		ret = ice_vf_verify_rx_desc(vf, ctx, &status);
		if (ret)
			goto err_exit;

		if (ctx->v_opcode == VIRTCHNL_OP_ADD_FDIR_FILTER)
			ice_vc_add_fdir_fltr_post(vf, ctx, status, true);
		else if (ctx->v_opcode == VIRTCHNL_OP_DEL_FDIR_FILTER)
			ice_vc_del_fdir_fltr_post(vf, ctx, status, true);
		else
			dev_err(dev, "VF %d: Unsupported opcode\n", vf->vf_id);

		spin_lock_irqsave(&vf->fdir.ctx_lock, flags);
		ctx->flags &= ~ICE_VF_FDIR_CTX_VALID;
		spin_unlock_irqrestore(&vf->fdir.ctx_lock, flags);
		continue;
err_exit:
		if (ctx->v_opcode == VIRTCHNL_OP_ADD_FDIR_FILTER)
			ice_vc_add_fdir_fltr_post(vf, ctx, status, false);
		else if (ctx->v_opcode == VIRTCHNL_OP_DEL_FDIR_FILTER)
			ice_vc_del_fdir_fltr_post(vf, ctx, status, false);
		else
			dev_err(dev, "VF %d: Unsupported opcode\n", vf->vf_id);

		spin_lock_irqsave(&vf->fdir.ctx_lock, flags);
		ctx->flags &= ~ICE_VF_FDIR_CTX_VALID;
		spin_unlock_irqrestore(&vf->fdir.ctx_lock, flags);
	}
}

/**
 * ice_vc_fdir_set_irq_ctx - set FDIR context info for later IRQ handler
 * @vf: pointer to the VF structure
 * @conf: FDIR configuration for each filter
 * @v_opcode: virtual channel operation code
 *
 * Return: 0 on success, and other on error.
 */
static int
ice_vc_fdir_set_irq_ctx(struct ice_vf *vf, struct virtchnl_fdir_fltr_conf *conf,
			enum virtchnl_ops v_opcode)
{
	struct device *dev = ice_pf_to_dev(vf->pf);
	struct ice_vf_fdir_ctx *ctx;
	unsigned long flags;

	ctx = &vf->fdir.ctx_irq;
	spin_lock_irqsave(&vf->fdir.ctx_lock, flags);
	if ((vf->fdir.ctx_irq.flags & ICE_VF_FDIR_CTX_VALID) ||
	    (vf->fdir.ctx_done.flags & ICE_VF_FDIR_CTX_VALID)) {
		spin_unlock_irqrestore(&vf->fdir.ctx_lock, flags);
		dev_dbg(dev, "VF %d: Last request is still in progress\n",
			vf->vf_id);
		return -EBUSY;
	}
	ctx->flags |= ICE_VF_FDIR_CTX_VALID;
	spin_unlock_irqrestore(&vf->fdir.ctx_lock, flags);

	ctx->conf = conf;
	ctx->v_opcode = v_opcode;
	ctx->stat = ICE_FDIR_CTX_READY;
	timer_setup(&ctx->rx_tmr, ice_vf_fdir_timer, 0);

	mod_timer(&ctx->rx_tmr, round_jiffies(msecs_to_jiffies(10) + jiffies));

	return 0;
}

/**
 * ice_vc_fdir_clear_irq_ctx - clear FDIR context info for IRQ handler
 * @vf: pointer to the VF structure
 *
 * Return: 0 on success, and other on error.
 */
static void ice_vc_fdir_clear_irq_ctx(struct ice_vf *vf)
{
	struct ice_vf_fdir_ctx *ctx = &vf->fdir.ctx_irq;
	unsigned long flags;

	del_timer(&ctx->rx_tmr);
	spin_lock_irqsave(&vf->fdir.ctx_lock, flags);
	ctx->flags &= ~ICE_VF_FDIR_CTX_VALID;
	spin_unlock_irqrestore(&vf->fdir.ctx_lock, flags);
}

/**
 * ice_vc_add_fdir_fltr - add a FDIR filter for VF by the msg buffer
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Return: 0 on success, and other on error.
 */
int ice_vc_add_fdir_fltr(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_fdir_add *fltr = (struct virtchnl_fdir_add *)msg;
	struct virtchnl_fdir_add *stat = NULL;
	struct virtchnl_fdir_fltr_conf *conf;
	enum virtchnl_status_code v_ret;
	struct device *dev;
	struct ice_pf *pf;
	int is_tun = 0;
	int len = 0;
	int ret;

	pf = vf->pf;
	dev = ice_pf_to_dev(pf);
	ret = ice_vc_fdir_param_check(vf, fltr->vsi_id);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		dev_dbg(dev, "Parameter check for VF %d failed\n", vf->vf_id);
		goto err_exit;
	}

	ret = ice_vf_start_ctrl_vsi(vf);
	if (ret && (ret != -EEXIST)) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		dev_err(dev, "Init FDIR for VF %d failed, ret:%d\n",
			vf->vf_id, ret);
		goto err_exit;
	}

	stat = kzalloc(sizeof(*stat), GFP_KERNEL);
	if (!stat) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		dev_dbg(dev, "Alloc stat for VF %d failed\n", vf->vf_id);
		goto err_exit;
	}

	conf = devm_kzalloc(dev, sizeof(*conf), GFP_KERNEL);
	if (!conf) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		dev_dbg(dev, "Alloc conf for VF %d failed\n", vf->vf_id);
		goto err_exit;
	}

	len = sizeof(*stat);
	ret = ice_vc_validate_fdir_fltr(vf, fltr, conf);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_FAILURE_RULE_INVALID;
		dev_dbg(dev, "Invalid FDIR filter from VF %d\n", vf->vf_id);
		goto err_free_conf;
	}

	if (fltr->validate_only) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_SUCCESS;
		devm_kfree(dev, conf);
		ret = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ADD_FDIR_FILTER,
					    v_ret, (u8 *)stat, len);
		goto exit;
	}

	ret = ice_vc_fdir_config_input_set(vf, fltr, conf, is_tun);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_FAILURE_RULE_CONFLICT;
		dev_err(dev, "VF %d: FDIR input set configure failed, ret:%d\n",
			vf->vf_id, ret);
		goto err_free_conf;
	}

	ret = ice_vc_fdir_is_dup_fltr(vf, conf);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_FAILURE_RULE_EXIST;
		dev_dbg(dev, "VF %d: duplicated FDIR rule detected\n",
			vf->vf_id);
		goto err_free_conf;
	}

	ret = ice_vc_fdir_insert_entry(vf, conf, &conf->flow_id);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_FAILURE_RULE_NORESOURCE;
		dev_dbg(dev, "VF %d: insert FDIR list failed\n", vf->vf_id);
		goto err_free_conf;
	}

	ret = ice_vc_fdir_set_irq_ctx(vf, conf, VIRTCHNL_OP_ADD_FDIR_FILTER);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_FAILURE_RULE_NORESOURCE;
		dev_dbg(dev, "VF %d: set FDIR context failed\n", vf->vf_id);
		goto err_free_conf;
	}

	ret = ice_vc_fdir_write_fltr(vf, conf, true, is_tun);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_FAILURE_RULE_NORESOURCE;
		dev_err(dev, "VF %d: writing FDIR rule failed, ret:%d\n",
			vf->vf_id, ret);
		goto err_rem_entry;
	}

exit:
	kfree(stat);
	return ret;

err_rem_entry:
	ice_vc_fdir_clear_irq_ctx(vf);
	ice_vc_fdir_remove_entry(vf, conf, conf->flow_id);
err_free_conf:
	devm_kfree(dev, conf);
err_exit:
	ret = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_ADD_FDIR_FILTER, v_ret,
				    (u8 *)stat, len);
	kfree(stat);
	return ret;
}

/**
 * ice_vc_del_fdir_fltr - delete a FDIR filter for VF by the msg buffer
 * @vf: pointer to the VF info
 * @msg: pointer to the msg buffer
 *
 * Return: 0 on success, and other on error.
 */
int ice_vc_del_fdir_fltr(struct ice_vf *vf, u8 *msg)
{
	struct virtchnl_fdir_del *fltr = (struct virtchnl_fdir_del *)msg;
	struct virtchnl_fdir_del *stat = NULL;
	struct virtchnl_fdir_fltr_conf *conf;
	enum virtchnl_status_code v_ret;
	struct device *dev;
	struct ice_pf *pf;
	int is_tun = 0;
	int len = 0;
	int ret;

	pf = vf->pf;
	dev = ice_pf_to_dev(pf);
	ret = ice_vc_fdir_param_check(vf, fltr->vsi_id);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_ERR_PARAM;
		dev_dbg(dev, "Parameter check for VF %d failed\n", vf->vf_id);
		goto err_exit;
	}

	stat = kzalloc(sizeof(*stat), GFP_KERNEL);
	if (!stat) {
		v_ret = VIRTCHNL_STATUS_ERR_NO_MEMORY;
		dev_dbg(dev, "Alloc stat for VF %d failed\n", vf->vf_id);
		goto err_exit;
	}

	len = sizeof(*stat);

	conf = ice_vc_fdir_lookup_entry(vf, fltr->flow_id);
	if (!conf) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_FAILURE_RULE_NONEXIST;
		dev_dbg(dev, "VF %d: FDIR invalid flow_id:0x%X\n",
			vf->vf_id, fltr->flow_id);
		goto err_exit;
	}

	/* Just return failure when ctrl_vsi idx is invalid */
	if (vf->ctrl_vsi_idx == ICE_NO_VSI) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_FAILURE_RULE_NORESOURCE;
		dev_err(dev, "Invalid FDIR ctrl_vsi for VF %d\n", vf->vf_id);
		goto err_exit;
	}

	ret = ice_vc_fdir_set_irq_ctx(vf, conf, VIRTCHNL_OP_DEL_FDIR_FILTER);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_FAILURE_RULE_NORESOURCE;
		dev_dbg(dev, "VF %d: set FDIR context failed\n", vf->vf_id);
		goto err_exit;
	}

	ret = ice_vc_fdir_write_fltr(vf, conf, false, is_tun);
	if (ret) {
		v_ret = VIRTCHNL_STATUS_SUCCESS;
		stat->status = VIRTCHNL_FDIR_FAILURE_RULE_NORESOURCE;
		dev_err(dev, "VF %d: writing FDIR rule failed, ret:%d\n",
			vf->vf_id, ret);
		goto err_del_tmr;
	}

	kfree(stat);

	return ret;

err_del_tmr:
	ice_vc_fdir_clear_irq_ctx(vf);
err_exit:
	ret = ice_vc_send_msg_to_vf(vf, VIRTCHNL_OP_DEL_FDIR_FILTER, v_ret,
				    (u8 *)stat, len);
	kfree(stat);
	return ret;
}

/**
 * ice_vf_fdir_init - init FDIR resource for VF
 * @vf: pointer to the VF info
 */
void ice_vf_fdir_init(struct ice_vf *vf)
{
	struct ice_vf_fdir *fdir = &vf->fdir;

	idr_init(&fdir->fdir_rule_idr);
	INIT_LIST_HEAD(&fdir->fdir_rule_list);

	spin_lock_init(&fdir->ctx_lock);
	fdir->ctx_irq.flags = 0;
	fdir->ctx_done.flags = 0;
}

/**
 * ice_vf_fdir_exit - destroy FDIR resource for VF
 * @vf: pointer to the VF info
 */
void ice_vf_fdir_exit(struct ice_vf *vf)
{
	ice_vc_fdir_flush_entry(vf);
	idr_destroy(&vf->fdir.fdir_rule_idr);
	ice_vc_fdir_rem_prof_all(vf);
	ice_vc_fdir_free_prof_all(vf);
}
