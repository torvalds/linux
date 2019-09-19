/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019, Intel Corporation. */

#ifndef _ICE_DCB_H_
#define _ICE_DCB_H_

#include "ice_type.h"

#define ICE_DCBX_STATUS_NOT_STARTED	0
#define ICE_DCBX_STATUS_IN_PROGRESS	1
#define ICE_DCBX_STATUS_DONE		2
#define ICE_DCBX_STATUS_DIS		7

#define ICE_TLV_TYPE_END		0
#define ICE_TLV_TYPE_ORG		127

#define ICE_IEEE_8021QAZ_OUI		0x0080C2
#define ICE_IEEE_SUBTYPE_ETS_CFG	9
#define ICE_IEEE_SUBTYPE_ETS_REC	10
#define ICE_IEEE_SUBTYPE_PFC_CFG	11
#define ICE_IEEE_SUBTYPE_APP_PRI	12

#define ICE_CEE_DCBX_OUI		0x001B21
#define ICE_CEE_DCBX_TYPE		2
#define ICE_CEE_SUBTYPE_PG_CFG		2
#define ICE_CEE_SUBTYPE_PFC_CFG		3
#define ICE_CEE_SUBTYPE_APP_PRI		4
#define ICE_CEE_MAX_FEAT_TYPE		3
/* Defines for LLDP TLV header */
#define ICE_LLDP_TLV_LEN_S		0
#define ICE_LLDP_TLV_LEN_M		(0x01FF << ICE_LLDP_TLV_LEN_S)
#define ICE_LLDP_TLV_TYPE_S		9
#define ICE_LLDP_TLV_TYPE_M		(0x7F << ICE_LLDP_TLV_TYPE_S)
#define ICE_LLDP_TLV_SUBTYPE_S		0
#define ICE_LLDP_TLV_SUBTYPE_M		(0xFF << ICE_LLDP_TLV_SUBTYPE_S)
#define ICE_LLDP_TLV_OUI_S		8
#define ICE_LLDP_TLV_OUI_M		(0xFFFFFFUL << ICE_LLDP_TLV_OUI_S)

/* Defines for IEEE ETS TLV */
#define ICE_IEEE_ETS_MAXTC_S	0
#define ICE_IEEE_ETS_MAXTC_M		(0x7 << ICE_IEEE_ETS_MAXTC_S)
#define ICE_IEEE_ETS_CBS_S		6
#define ICE_IEEE_ETS_CBS_M		BIT(ICE_IEEE_ETS_CBS_S)
#define ICE_IEEE_ETS_WILLING_S		7
#define ICE_IEEE_ETS_WILLING_M		BIT(ICE_IEEE_ETS_WILLING_S)
#define ICE_IEEE_ETS_PRIO_0_S		0
#define ICE_IEEE_ETS_PRIO_0_M		(0x7 << ICE_IEEE_ETS_PRIO_0_S)
#define ICE_IEEE_ETS_PRIO_1_S		4
#define ICE_IEEE_ETS_PRIO_1_M		(0x7 << ICE_IEEE_ETS_PRIO_1_S)
#define ICE_CEE_PGID_PRIO_0_S		0
#define ICE_CEE_PGID_PRIO_0_M		(0xF << ICE_CEE_PGID_PRIO_0_S)
#define ICE_CEE_PGID_PRIO_1_S		4
#define ICE_CEE_PGID_PRIO_1_M		(0xF << ICE_CEE_PGID_PRIO_1_S)
#define ICE_CEE_PGID_STRICT		15

/* Defines for IEEE TSA types */
#define ICE_IEEE_TSA_STRICT		0
#define ICE_IEEE_TSA_ETS		2

/* Defines for IEEE PFC TLV */
#define ICE_IEEE_PFC_CAP_S		0
#define ICE_IEEE_PFC_CAP_M		(0xF << ICE_IEEE_PFC_CAP_S)
#define ICE_IEEE_PFC_MBC_S		6
#define ICE_IEEE_PFC_MBC_M		BIT(ICE_IEEE_PFC_MBC_S)
#define ICE_IEEE_PFC_WILLING_S		7
#define ICE_IEEE_PFC_WILLING_M		BIT(ICE_IEEE_PFC_WILLING_S)

/* Defines for IEEE APP TLV */
#define ICE_IEEE_APP_SEL_S		0
#define ICE_IEEE_APP_SEL_M		(0x7 << ICE_IEEE_APP_SEL_S)
#define ICE_IEEE_APP_PRIO_S		5
#define ICE_IEEE_APP_PRIO_M		(0x7 << ICE_IEEE_APP_PRIO_S)

/* TLV definitions for preparing MIB */
#define ICE_IEEE_TLV_ID_ETS_CFG		3
#define ICE_IEEE_TLV_ID_ETS_REC		4
#define ICE_IEEE_TLV_ID_PFC_CFG		5
#define ICE_IEEE_TLV_ID_APP_PRI		6
#define ICE_TLV_ID_END_OF_LLDPPDU	7
#define ICE_TLV_ID_START		ICE_IEEE_TLV_ID_ETS_CFG

#define ICE_IEEE_ETS_TLV_LEN		25
#define ICE_IEEE_PFC_TLV_LEN		6
#define ICE_IEEE_APP_TLV_LEN		11

/* IEEE 802.1AB LLDP Organization specific TLV */
struct ice_lldp_org_tlv {
	__be16 typelen;
	__be32 ouisubtype;
	u8 tlvinfo[1];
} __packed;

struct ice_cee_tlv_hdr {
	__be16 typelen;
	u8 operver;
	u8 maxver;
};

struct ice_cee_ctrl_tlv {
	struct ice_cee_tlv_hdr hdr;
	__be32 seqno;
	__be32 ackno;
};

struct ice_cee_feat_tlv {
	struct ice_cee_tlv_hdr hdr;
	u8 en_will_err; /* Bits: |En|Will|Err|Reserved(5)| */
#define ICE_CEE_FEAT_TLV_ENA_M		0x80
#define ICE_CEE_FEAT_TLV_WILLING_M	0x40
#define ICE_CEE_FEAT_TLV_ERR_M		0x20
	u8 subtype;
	u8 tlvinfo[1];
};

struct ice_cee_app_prio {
	__be16 protocol;
	u8 upper_oui_sel; /* Bits: |Upper OUI(6)|Selector(2)| */
#define ICE_CEE_APP_SELECTOR_M	0x03
	__be16 lower_oui;
	u8 prio_map;
} __packed;

enum ice_status
ice_aq_get_dcb_cfg(struct ice_hw *hw, u8 mib_type, u8 bridgetype,
		   struct ice_dcbx_cfg *dcbcfg);
enum ice_status ice_get_dcb_cfg(struct ice_port_info *pi);
enum ice_status ice_set_dcb_cfg(struct ice_port_info *pi);
enum ice_status ice_init_dcb(struct ice_hw *hw);
enum ice_status
ice_query_port_ets(struct ice_port_info *pi,
		   struct ice_aqc_port_ets_elem *buf, u16 buf_size,
		   struct ice_sq_cd *cmd_details);
#ifdef CONFIG_DCB
enum ice_status
ice_aq_stop_lldp(struct ice_hw *hw, bool shutdown_lldp_agent, bool persist,
		 struct ice_sq_cd *cd);
enum ice_status
ice_aq_start_lldp(struct ice_hw *hw, bool persist, struct ice_sq_cd *cd);
enum ice_status
ice_aq_start_stop_dcbx(struct ice_hw *hw, bool start_dcbx_agent,
		       bool *dcbx_agent_status, struct ice_sq_cd *cd);
enum ice_status
ice_aq_cfg_lldp_mib_change(struct ice_hw *hw, bool ena_update,
			   struct ice_sq_cd *cd);
#else /* CONFIG_DCB */
static inline enum ice_status
ice_aq_stop_lldp(struct ice_hw __always_unused *hw,
		 bool __always_unused shutdown_lldp_agent,
		 bool __always_unused persist,
		 struct ice_sq_cd __always_unused *cd)
{
	return 0;
}

static inline enum ice_status
ice_aq_start_lldp(struct ice_hw __always_unused *hw,
		  bool __always_unused persist,
		  struct ice_sq_cd __always_unused *cd)
{
	return 0;
}

static inline enum ice_status
ice_aq_start_stop_dcbx(struct ice_hw __always_unused *hw,
		       bool __always_unused start_dcbx_agent,
		       bool *dcbx_agent_status,
		       struct ice_sq_cd __always_unused *cd)
{
	*dcbx_agent_status = false;

	return 0;
}

static inline enum ice_status
ice_aq_cfg_lldp_mib_change(struct ice_hw __always_unused *hw,
			   bool __always_unused ena_update,
			   struct ice_sq_cd __always_unused *cd)
{
	return 0;
}

#endif /* CONFIG_DCB */
#endif /* _ICE_DCB_H_ */
