// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Intel Corporation. */

#include "ice_common.h"
#include "ice_sched.h"
#include "ice_dcb.h"

/**
 * ice_aq_get_lldp_mib
 * @hw: pointer to the HW struct
 * @bridge_type: type of bridge requested
 * @mib_type: Local, Remote or both Local and Remote MIBs
 * @buf: pointer to the caller-supplied buffer to store the MIB block
 * @buf_size: size of the buffer (in bytes)
 * @local_len: length of the returned Local LLDP MIB
 * @remote_len: length of the returned Remote LLDP MIB
 * @cd: pointer to command details structure or NULL
 *
 * Requests the complete LLDP MIB (entire packet). (0x0A00)
 */
static enum ice_status
ice_aq_get_lldp_mib(struct ice_hw *hw, u8 bridge_type, u8 mib_type, void *buf,
		    u16 buf_size, u16 *local_len, u16 *remote_len,
		    struct ice_sq_cd *cd)
{
	struct ice_aqc_lldp_get_mib *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	cmd = &desc.params.lldp_get_mib;

	if (buf_size == 0 || !buf)
		return ICE_ERR_PARAM;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_lldp_get_mib);

	cmd->type = mib_type & ICE_AQ_LLDP_MIB_TYPE_M;
	cmd->type |= (bridge_type << ICE_AQ_LLDP_BRID_TYPE_S) &
		ICE_AQ_LLDP_BRID_TYPE_M;

	desc.datalen = cpu_to_le16(buf_size);

	status = ice_aq_send_cmd(hw, &desc, buf, buf_size, cd);
	if (!status) {
		if (local_len)
			*local_len = le16_to_cpu(cmd->local_len);
		if (remote_len)
			*remote_len = le16_to_cpu(cmd->remote_len);
	}

	return status;
}

/**
 * ice_aq_cfg_lldp_mib_change
 * @hw: pointer to the HW struct
 * @ena_update: Enable or Disable event posting
 * @cd: pointer to command details structure or NULL
 *
 * Enable or Disable posting of an event on ARQ when LLDP MIB
 * associated with the interface changes (0x0A01)
 */
static enum ice_status
ice_aq_cfg_lldp_mib_change(struct ice_hw *hw, bool ena_update,
			   struct ice_sq_cd *cd)
{
	struct ice_aqc_lldp_set_mib_change *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.lldp_set_event;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_lldp_set_mib_change);

	if (!ena_update)
		cmd->command |= ICE_AQ_LLDP_MIB_UPDATE_DIS;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_aq_start_lldp
 * @hw: pointer to the HW struct
 * @cd: pointer to command details structure or NULL
 *
 * Start the embedded LLDP Agent on all ports. (0x0A06)
 */
enum ice_status ice_aq_start_lldp(struct ice_hw *hw, struct ice_sq_cd *cd)
{
	struct ice_aqc_lldp_start *cmd;
	struct ice_aq_desc desc;

	cmd = &desc.params.lldp_start;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_lldp_start);

	cmd->command = ICE_AQ_LLDP_AGENT_START;

	return ice_aq_send_cmd(hw, &desc, NULL, 0, cd);
}

/**
 * ice_get_dcbx_status
 * @hw: pointer to the HW struct
 *
 * Get the DCBX status from the Firmware
 */
u8 ice_get_dcbx_status(struct ice_hw *hw)
{
	u32 reg;

	reg = rd32(hw, PRTDCB_GENS);
	return (u8)((reg & PRTDCB_GENS_DCBX_STATUS_M) >>
		    PRTDCB_GENS_DCBX_STATUS_S);
}

/**
 * ice_parse_ieee_ets_common_tlv
 * @buf: Data buffer to be parsed for ETS CFG/REC data
 * @ets_cfg: Container to store parsed data
 *
 * Parses the common data of IEEE 802.1Qaz ETS CFG/REC TLV
 */
static void
ice_parse_ieee_ets_common_tlv(u8 *buf, struct ice_dcb_ets_cfg *ets_cfg)
{
	u8 offset = 0;
	int i;

	/* Priority Assignment Table (4 octets)
	 * Octets:|    1    |    2    |    3    |    4    |
	 *        -----------------------------------------
	 *        |pri0|pri1|pri2|pri3|pri4|pri5|pri6|pri7|
	 *        -----------------------------------------
	 *   Bits:|7  4|3  0|7  4|3  0|7  4|3  0|7  4|3  0|
	 *        -----------------------------------------
	 */
	for (i = 0; i < 4; i++) {
		ets_cfg->prio_table[i * 2] =
			((buf[offset] & ICE_IEEE_ETS_PRIO_1_M) >>
			 ICE_IEEE_ETS_PRIO_1_S);
		ets_cfg->prio_table[i * 2 + 1] =
			((buf[offset] & ICE_IEEE_ETS_PRIO_0_M) >>
			 ICE_IEEE_ETS_PRIO_0_S);
		offset++;
	}

	/* TC Bandwidth Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 *
	 * TSA Assignment Table (8 octets)
	 * Octets:| 9 | 10| 11| 12| 13| 14| 15| 16|
	 *        ---------------------------------
	 *        |tc0|tc1|tc2|tc3|tc4|tc5|tc6|tc7|
	 *        ---------------------------------
	 */
	ice_for_each_traffic_class(i) {
		ets_cfg->tcbwtable[i] = buf[offset];
		ets_cfg->tsatable[i] = buf[ICE_MAX_TRAFFIC_CLASS + offset++];
	}
}

/**
 * ice_parse_ieee_etscfg_tlv
 * @tlv: IEEE 802.1Qaz ETS CFG TLV
 * @dcbcfg: Local store to update ETS CFG data
 *
 * Parses IEEE 802.1Qaz ETS CFG TLV
 */
static void
ice_parse_ieee_etscfg_tlv(struct ice_lldp_org_tlv *tlv,
			  struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_dcb_ets_cfg *etscfg;
	u8 *buf = tlv->tlvinfo;

	/* First Octet post subtype
	 * --------------------------
	 * |will-|CBS  | Re-  | Max |
	 * |ing  |     |served| TCs |
	 * --------------------------
	 * |1bit | 1bit|3 bits|3bits|
	 */
	etscfg = &dcbcfg->etscfg;
	etscfg->willing = ((buf[0] & ICE_IEEE_ETS_WILLING_M) >>
			   ICE_IEEE_ETS_WILLING_S);
	etscfg->cbs = ((buf[0] & ICE_IEEE_ETS_CBS_M) >> ICE_IEEE_ETS_CBS_S);
	etscfg->maxtcs = ((buf[0] & ICE_IEEE_ETS_MAXTC_M) >>
			  ICE_IEEE_ETS_MAXTC_S);

	/* Begin parsing at Priority Assignment Table (offset 1 in buf) */
	ice_parse_ieee_ets_common_tlv(&buf[1], etscfg);
}

/**
 * ice_parse_ieee_etsrec_tlv
 * @tlv: IEEE 802.1Qaz ETS REC TLV
 * @dcbcfg: Local store to update ETS REC data
 *
 * Parses IEEE 802.1Qaz ETS REC TLV
 */
static void
ice_parse_ieee_etsrec_tlv(struct ice_lldp_org_tlv *tlv,
			  struct ice_dcbx_cfg *dcbcfg)
{
	u8 *buf = tlv->tlvinfo;

	/* Begin parsing at Priority Assignment Table (offset 1 in buf) */
	ice_parse_ieee_ets_common_tlv(&buf[1], &dcbcfg->etsrec);
}

/**
 * ice_parse_ieee_pfccfg_tlv
 * @tlv: IEEE 802.1Qaz PFC CFG TLV
 * @dcbcfg: Local store to update PFC CFG data
 *
 * Parses IEEE 802.1Qaz PFC CFG TLV
 */
static void
ice_parse_ieee_pfccfg_tlv(struct ice_lldp_org_tlv *tlv,
			  struct ice_dcbx_cfg *dcbcfg)
{
	u8 *buf = tlv->tlvinfo;

	/* ----------------------------------------
	 * |will-|MBC  | Re-  | PFC |  PFC Enable  |
	 * |ing  |     |served| cap |              |
	 * -----------------------------------------
	 * |1bit | 1bit|2 bits|4bits| 1 octet      |
	 */
	dcbcfg->pfc.willing = ((buf[0] & ICE_IEEE_PFC_WILLING_M) >>
			       ICE_IEEE_PFC_WILLING_S);
	dcbcfg->pfc.mbc = ((buf[0] & ICE_IEEE_PFC_MBC_M) >> ICE_IEEE_PFC_MBC_S);
	dcbcfg->pfc.pfccap = ((buf[0] & ICE_IEEE_PFC_CAP_M) >>
			      ICE_IEEE_PFC_CAP_S);
	dcbcfg->pfc.pfcena = buf[1];
}

/**
 * ice_parse_ieee_app_tlv
 * @tlv: IEEE 802.1Qaz APP TLV
 * @dcbcfg: Local store to update APP PRIO data
 *
 * Parses IEEE 802.1Qaz APP PRIO TLV
 */
static void
ice_parse_ieee_app_tlv(struct ice_lldp_org_tlv *tlv,
		       struct ice_dcbx_cfg *dcbcfg)
{
	u16 offset = 0;
	u16 typelen;
	int i = 0;
	u16 len;
	u8 *buf;

	typelen = ntohs(tlv->typelen);
	len = ((typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S);
	buf = tlv->tlvinfo;

	/* Removing sizeof(ouisubtype) and reserved byte from len.
	 * Remaining len div 3 is number of APP TLVs.
	 */
	len -= (sizeof(tlv->ouisubtype) + 1);

	/* Move offset to App Priority Table */
	offset++;

	/* Application Priority Table (3 octets)
	 * Octets:|         1          |    2    |    3    |
	 *        -----------------------------------------
	 *        |Priority|Rsrvd| Sel |    Protocol ID    |
	 *        -----------------------------------------
	 *   Bits:|23    21|20 19|18 16|15                0|
	 *        -----------------------------------------
	 */
	while (offset < len) {
		dcbcfg->app[i].priority = ((buf[offset] &
					    ICE_IEEE_APP_PRIO_M) >>
					   ICE_IEEE_APP_PRIO_S);
		dcbcfg->app[i].selector = ((buf[offset] &
					    ICE_IEEE_APP_SEL_M) >>
					   ICE_IEEE_APP_SEL_S);
		dcbcfg->app[i].prot_id = (buf[offset + 1] << 0x8) |
			buf[offset + 2];
		/* Move to next app */
		offset += 3;
		i++;
		if (i >= ICE_DCBX_MAX_APPS)
			break;
	}

	dcbcfg->numapps = i;
}

/**
 * ice_parse_ieee_tlv
 * @tlv: IEEE 802.1Qaz TLV
 * @dcbcfg: Local store to update ETS REC data
 *
 * Get the TLV subtype and send it to parsing function
 * based on the subtype value
 */
static void
ice_parse_ieee_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	u32 ouisubtype;
	u8 subtype;

	ouisubtype = ntohl(tlv->ouisubtype);
	subtype = (u8)((ouisubtype & ICE_LLDP_TLV_SUBTYPE_M) >>
		       ICE_LLDP_TLV_SUBTYPE_S);
	switch (subtype) {
	case ICE_IEEE_SUBTYPE_ETS_CFG:
		ice_parse_ieee_etscfg_tlv(tlv, dcbcfg);
		break;
	case ICE_IEEE_SUBTYPE_ETS_REC:
		ice_parse_ieee_etsrec_tlv(tlv, dcbcfg);
		break;
	case ICE_IEEE_SUBTYPE_PFC_CFG:
		ice_parse_ieee_pfccfg_tlv(tlv, dcbcfg);
		break;
	case ICE_IEEE_SUBTYPE_APP_PRI:
		ice_parse_ieee_app_tlv(tlv, dcbcfg);
		break;
	default:
		break;
	}
}

/**
 * ice_parse_cee_pgcfg_tlv
 * @tlv: CEE DCBX PG CFG TLV
 * @dcbcfg: Local store to update ETS CFG data
 *
 * Parses CEE DCBX PG CFG TLV
 */
static void
ice_parse_cee_pgcfg_tlv(struct ice_cee_feat_tlv *tlv,
			struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_dcb_ets_cfg *etscfg;
	u8 *buf = tlv->tlvinfo;
	u16 offset = 0;
	int i;

	etscfg = &dcbcfg->etscfg;

	if (tlv->en_will_err & ICE_CEE_FEAT_TLV_WILLING_M)
		etscfg->willing = 1;

	etscfg->cbs = 0;
	/* Priority Group Table (4 octets)
	 * Octets:|    1    |    2    |    3    |    4    |
	 *        -----------------------------------------
	 *        |pri0|pri1|pri2|pri3|pri4|pri5|pri6|pri7|
	 *        -----------------------------------------
	 *   Bits:|7  4|3  0|7  4|3  0|7  4|3  0|7  4|3  0|
	 *        -----------------------------------------
	 */
	for (i = 0; i < 4; i++) {
		etscfg->prio_table[i * 2] =
			((buf[offset] & ICE_CEE_PGID_PRIO_1_M) >>
			 ICE_CEE_PGID_PRIO_1_S);
		etscfg->prio_table[i * 2 + 1] =
			((buf[offset] & ICE_CEE_PGID_PRIO_0_M) >>
			 ICE_CEE_PGID_PRIO_0_S);
		offset++;
	}

	/* PG Percentage Table (8 octets)
	 * Octets:| 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
	 *        ---------------------------------
	 *        |pg0|pg1|pg2|pg3|pg4|pg5|pg6|pg7|
	 *        ---------------------------------
	 */
	ice_for_each_traffic_class(i)
		etscfg->tcbwtable[i] = buf[offset++];

	/* Number of TCs supported (1 octet) */
	etscfg->maxtcs = buf[offset];
}

/**
 * ice_parse_cee_pfccfg_tlv
 * @tlv: CEE DCBX PFC CFG TLV
 * @dcbcfg: Local store to update PFC CFG data
 *
 * Parses CEE DCBX PFC CFG TLV
 */
static void
ice_parse_cee_pfccfg_tlv(struct ice_cee_feat_tlv *tlv,
			 struct ice_dcbx_cfg *dcbcfg)
{
	u8 *buf = tlv->tlvinfo;

	if (tlv->en_will_err & ICE_CEE_FEAT_TLV_WILLING_M)
		dcbcfg->pfc.willing = 1;

	/* ------------------------
	 * | PFC Enable | PFC TCs |
	 * ------------------------
	 * | 1 octet    | 1 octet |
	 */
	dcbcfg->pfc.pfcena = buf[0];
	dcbcfg->pfc.pfccap = buf[1];
}

/**
 * ice_parse_cee_app_tlv
 * @tlv: CEE DCBX APP TLV
 * @dcbcfg: Local store to update APP PRIO data
 *
 * Parses CEE DCBX APP PRIO TLV
 */
static void
ice_parse_cee_app_tlv(struct ice_cee_feat_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	u16 len, typelen, offset = 0;
	struct ice_cee_app_prio *app;
	u8 i;

	typelen = ntohs(tlv->hdr.typelen);
	len = ((typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S);

	dcbcfg->numapps = len / sizeof(*app);
	if (!dcbcfg->numapps)
		return;
	if (dcbcfg->numapps > ICE_DCBX_MAX_APPS)
		dcbcfg->numapps = ICE_DCBX_MAX_APPS;

	for (i = 0; i < dcbcfg->numapps; i++) {
		u8 up, selector;

		app = (struct ice_cee_app_prio *)(tlv->tlvinfo + offset);
		for (up = 0; up < ICE_MAX_USER_PRIORITY; up++)
			if (app->prio_map & BIT(up))
				break;

		dcbcfg->app[i].priority = up;

		/* Get Selector from lower 2 bits, and convert to IEEE */
		selector = (app->upper_oui_sel & ICE_CEE_APP_SELECTOR_M);
		switch (selector) {
		case ICE_CEE_APP_SEL_ETHTYPE:
			dcbcfg->app[i].selector = ICE_APP_SEL_ETHTYPE;
			break;
		case ICE_CEE_APP_SEL_TCPIP:
			dcbcfg->app[i].selector = ICE_APP_SEL_TCPIP;
			break;
		default:
			/* Keep selector as it is for unknown types */
			dcbcfg->app[i].selector = selector;
		}

		dcbcfg->app[i].prot_id = ntohs(app->protocol);
		/* Move to next app */
		offset += sizeof(*app);
	}
}

/**
 * ice_parse_cee_tlv
 * @tlv: CEE DCBX TLV
 * @dcbcfg: Local store to update DCBX config data
 *
 * Get the TLV subtype and send it to parsing function
 * based on the subtype value
 */
static void
ice_parse_cee_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_cee_feat_tlv *sub_tlv;
	u8 subtype, feat_tlv_count = 0;
	u16 len, tlvlen, typelen;
	u32 ouisubtype;

	ouisubtype = ntohl(tlv->ouisubtype);
	subtype = (u8)((ouisubtype & ICE_LLDP_TLV_SUBTYPE_M) >>
		       ICE_LLDP_TLV_SUBTYPE_S);
	/* Return if not CEE DCBX */
	if (subtype != ICE_CEE_DCBX_TYPE)
		return;

	typelen = ntohs(tlv->typelen);
	tlvlen = ((typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S);
	len = sizeof(tlv->typelen) + sizeof(ouisubtype) +
		sizeof(struct ice_cee_ctrl_tlv);
	/* Return if no CEE DCBX Feature TLVs */
	if (tlvlen <= len)
		return;

	sub_tlv = (struct ice_cee_feat_tlv *)((char *)tlv + len);
	while (feat_tlv_count < ICE_CEE_MAX_FEAT_TYPE) {
		u16 sublen;

		typelen = ntohs(sub_tlv->hdr.typelen);
		sublen = ((typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S);
		subtype = (u8)((typelen & ICE_LLDP_TLV_TYPE_M) >>
			       ICE_LLDP_TLV_TYPE_S);
		switch (subtype) {
		case ICE_CEE_SUBTYPE_PG_CFG:
			ice_parse_cee_pgcfg_tlv(sub_tlv, dcbcfg);
			break;
		case ICE_CEE_SUBTYPE_PFC_CFG:
			ice_parse_cee_pfccfg_tlv(sub_tlv, dcbcfg);
			break;
		case ICE_CEE_SUBTYPE_APP_PRI:
			ice_parse_cee_app_tlv(sub_tlv, dcbcfg);
			break;
		default:
			return;	/* Invalid Sub-type return */
		}
		feat_tlv_count++;
		/* Move to next sub TLV */
		sub_tlv = (struct ice_cee_feat_tlv *)
			  ((char *)sub_tlv + sizeof(sub_tlv->hdr.typelen) +
			   sublen);
	}
}

/**
 * ice_parse_org_tlv
 * @tlv: Organization specific TLV
 * @dcbcfg: Local store to update ETS REC data
 *
 * Currently only IEEE 802.1Qaz TLV is supported, all others
 * will be returned
 */
static void
ice_parse_org_tlv(struct ice_lldp_org_tlv *tlv, struct ice_dcbx_cfg *dcbcfg)
{
	u32 ouisubtype;
	u32 oui;

	ouisubtype = ntohl(tlv->ouisubtype);
	oui = ((ouisubtype & ICE_LLDP_TLV_OUI_M) >> ICE_LLDP_TLV_OUI_S);
	switch (oui) {
	case ICE_IEEE_8021QAZ_OUI:
		ice_parse_ieee_tlv(tlv, dcbcfg);
		break;
	case ICE_CEE_DCBX_OUI:
		ice_parse_cee_tlv(tlv, dcbcfg);
		break;
	default:
		break;
	}
}

/**
 * ice_lldp_to_dcb_cfg
 * @lldpmib: LLDPDU to be parsed
 * @dcbcfg: store for LLDPDU data
 *
 * Parse DCB configuration from the LLDPDU
 */
static enum ice_status
ice_lldp_to_dcb_cfg(u8 *lldpmib, struct ice_dcbx_cfg *dcbcfg)
{
	struct ice_lldp_org_tlv *tlv;
	enum ice_status ret = 0;
	u16 offset = 0;
	u16 typelen;
	u16 type;
	u16 len;

	if (!lldpmib || !dcbcfg)
		return ICE_ERR_PARAM;

	/* set to the start of LLDPDU */
	lldpmib += ETH_HLEN;
	tlv = (struct ice_lldp_org_tlv *)lldpmib;
	while (1) {
		typelen = ntohs(tlv->typelen);
		type = ((typelen & ICE_LLDP_TLV_TYPE_M) >> ICE_LLDP_TLV_TYPE_S);
		len = ((typelen & ICE_LLDP_TLV_LEN_M) >> ICE_LLDP_TLV_LEN_S);
		offset += sizeof(typelen) + len;

		/* END TLV or beyond LLDPDU size */
		if (type == ICE_TLV_TYPE_END || offset > ICE_LLDPDU_SIZE)
			break;

		switch (type) {
		case ICE_TLV_TYPE_ORG:
			ice_parse_org_tlv(tlv, dcbcfg);
			break;
		default:
			break;
		}

		/* Move to next TLV */
		tlv = (struct ice_lldp_org_tlv *)
		      ((char *)tlv + sizeof(tlv->typelen) + len);
	}

	return ret;
}

/**
 * ice_aq_get_dcb_cfg
 * @hw: pointer to the HW struct
 * @mib_type: mib type for the query
 * @bridgetype: bridge type for the query (remote)
 * @dcbcfg: store for LLDPDU data
 *
 * Query DCB configuration from the firmware
 */
static enum ice_status
ice_aq_get_dcb_cfg(struct ice_hw *hw, u8 mib_type, u8 bridgetype,
		   struct ice_dcbx_cfg *dcbcfg)
{
	enum ice_status ret;
	u8 *lldpmib;

	/* Allocate the LLDPDU */
	lldpmib = devm_kzalloc(ice_hw_to_dev(hw), ICE_LLDPDU_SIZE, GFP_KERNEL);
	if (!lldpmib)
		return ICE_ERR_NO_MEMORY;

	ret = ice_aq_get_lldp_mib(hw, bridgetype, mib_type, (void *)lldpmib,
				  ICE_LLDPDU_SIZE, NULL, NULL, NULL);

	if (!ret)
		/* Parse LLDP MIB to get DCB configuration */
		ret = ice_lldp_to_dcb_cfg(lldpmib, dcbcfg);

	devm_kfree(ice_hw_to_dev(hw), lldpmib);

	return ret;
}

/**
 * ice_aq_start_stop_dcbx - Start/Stop DCBx service in FW
 * @hw: pointer to the HW struct
 * @start_dcbx_agent: True if DCBx Agent needs to be started
 *		      False if DCBx Agent needs to be stopped
 * @dcbx_agent_status: FW indicates back the DCBx agent status
 *		       True if DCBx Agent is active
 *		       False if DCBx Agent is stopped
 * @cd: pointer to command details structure or NULL
 *
 * Start/Stop the embedded dcbx Agent. In case that this wrapper function
 * returns ICE_SUCCESS, caller will need to check if FW returns back the same
 * value as stated in dcbx_agent_status, and react accordingly. (0x0A09)
 */
enum ice_status
ice_aq_start_stop_dcbx(struct ice_hw *hw, bool start_dcbx_agent,
		       bool *dcbx_agent_status, struct ice_sq_cd *cd)
{
	struct ice_aqc_lldp_stop_start_specific_agent *cmd;
	enum ice_status status;
	struct ice_aq_desc desc;
	u16 opcode;

	cmd = &desc.params.lldp_agent_ctrl;

	opcode = ice_aqc_opc_lldp_stop_start_specific_agent;

	ice_fill_dflt_direct_cmd_desc(&desc, opcode);

	if (start_dcbx_agent)
		cmd->command = ICE_AQC_START_STOP_AGENT_START_DCBX;

	status = ice_aq_send_cmd(hw, &desc, NULL, 0, cd);

	*dcbx_agent_status = false;

	if (!status &&
	    cmd->command == ICE_AQC_START_STOP_AGENT_START_DCBX)
		*dcbx_agent_status = true;

	return status;
}

/**
 * ice_aq_get_cee_dcb_cfg
 * @hw: pointer to the HW struct
 * @buff: response buffer that stores CEE operational configuration
 * @cd: pointer to command details structure or NULL
 *
 * Get CEE DCBX mode operational configuration from firmware (0x0A07)
 */
static enum ice_status
ice_aq_get_cee_dcb_cfg(struct ice_hw *hw,
		       struct ice_aqc_get_cee_dcb_cfg_resp *buff,
		       struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_cee_dcb_cfg);

	return ice_aq_send_cmd(hw, &desc, (void *)buff, sizeof(*buff), cd);
}

/**
 * ice_cee_to_dcb_cfg
 * @cee_cfg: pointer to CEE configuration struct
 * @dcbcfg: DCB configuration struct
 *
 * Convert CEE configuration from firmware to DCB configuration
 */
static void
ice_cee_to_dcb_cfg(struct ice_aqc_get_cee_dcb_cfg_resp *cee_cfg,
		   struct ice_dcbx_cfg *dcbcfg)
{
	u32 status, tlv_status = le32_to_cpu(cee_cfg->tlv_status);
	u32 ice_aqc_cee_status_mask, ice_aqc_cee_status_shift;
	u16 app_prio = le16_to_cpu(cee_cfg->oper_app_prio);
	u8 i, err, sync, oper, app_index, ice_app_sel_type;
	u16 ice_aqc_cee_app_mask, ice_aqc_cee_app_shift;
	u16 ice_app_prot_id_type;

	/* CEE PG data to ETS config */
	dcbcfg->etscfg.maxtcs = cee_cfg->oper_num_tc;

	/* Note that the FW creates the oper_prio_tc nibbles reversed
	 * from those in the CEE Priority Group sub-TLV.
	 */
	for (i = 0; i < ICE_MAX_TRAFFIC_CLASS / 2; i++) {
		dcbcfg->etscfg.prio_table[i * 2] =
			((cee_cfg->oper_prio_tc[i] & ICE_CEE_PGID_PRIO_0_M) >>
			 ICE_CEE_PGID_PRIO_0_S);
		dcbcfg->etscfg.prio_table[i * 2 + 1] =
			((cee_cfg->oper_prio_tc[i] & ICE_CEE_PGID_PRIO_1_M) >>
			 ICE_CEE_PGID_PRIO_1_S);
	}

	ice_for_each_traffic_class(i) {
		dcbcfg->etscfg.tcbwtable[i] = cee_cfg->oper_tc_bw[i];

		if (dcbcfg->etscfg.prio_table[i] == ICE_CEE_PGID_STRICT) {
			/* Map it to next empty TC */
			dcbcfg->etscfg.prio_table[i] = cee_cfg->oper_num_tc - 1;
			dcbcfg->etscfg.tsatable[i] = ICE_IEEE_TSA_STRICT;
		} else {
			dcbcfg->etscfg.tsatable[i] = ICE_IEEE_TSA_ETS;
		}
	}

	/* CEE PFC data to ETS config */
	dcbcfg->pfc.pfcena = cee_cfg->oper_pfc_en;
	dcbcfg->pfc.pfccap = ICE_MAX_TRAFFIC_CLASS;

	app_index = 0;
	for (i = 0; i < 3; i++) {
		if (i == 0) {
			/* FCoE APP */
			ice_aqc_cee_status_mask = ICE_AQC_CEE_FCOE_STATUS_M;
			ice_aqc_cee_status_shift = ICE_AQC_CEE_FCOE_STATUS_S;
			ice_aqc_cee_app_mask = ICE_AQC_CEE_APP_FCOE_M;
			ice_aqc_cee_app_shift = ICE_AQC_CEE_APP_FCOE_S;
			ice_app_sel_type = ICE_APP_SEL_ETHTYPE;
			ice_app_prot_id_type = ICE_APP_PROT_ID_FCOE;
		} else if (i == 1) {
			/* iSCSI APP */
			ice_aqc_cee_status_mask = ICE_AQC_CEE_ISCSI_STATUS_M;
			ice_aqc_cee_status_shift = ICE_AQC_CEE_ISCSI_STATUS_S;
			ice_aqc_cee_app_mask = ICE_AQC_CEE_APP_ISCSI_M;
			ice_aqc_cee_app_shift = ICE_AQC_CEE_APP_ISCSI_S;
			ice_app_sel_type = ICE_APP_SEL_TCPIP;
			ice_app_prot_id_type = ICE_APP_PROT_ID_ISCSI;
		} else {
			/* FIP APP */
			ice_aqc_cee_status_mask = ICE_AQC_CEE_FIP_STATUS_M;
			ice_aqc_cee_status_shift = ICE_AQC_CEE_FIP_STATUS_S;
			ice_aqc_cee_app_mask = ICE_AQC_CEE_APP_FIP_M;
			ice_aqc_cee_app_shift = ICE_AQC_CEE_APP_FIP_S;
			ice_app_sel_type = ICE_APP_SEL_ETHTYPE;
			ice_app_prot_id_type = ICE_APP_PROT_ID_FIP;
		}

		status = (tlv_status & ice_aqc_cee_status_mask) >>
			 ice_aqc_cee_status_shift;
		err = (status & ICE_TLV_STATUS_ERR) ? 1 : 0;
		sync = (status & ICE_TLV_STATUS_SYNC) ? 1 : 0;
		oper = (status & ICE_TLV_STATUS_OPER) ? 1 : 0;
		/* Add FCoE/iSCSI/FIP APP if Error is False and
		 * Oper/Sync is True
		 */
		if (!err && sync && oper) {
			dcbcfg->app[app_index].priority =
				(app_prio & ice_aqc_cee_app_mask) >>
				ice_aqc_cee_app_shift;
			dcbcfg->app[app_index].selector = ice_app_sel_type;
			dcbcfg->app[app_index].prot_id = ice_app_prot_id_type;
			app_index++;
		}
	}

	dcbcfg->numapps = app_index;
}

/**
 * ice_get_ieee_dcb_cfg
 * @pi: port information structure
 * @dcbx_mode: mode of DCBX (IEEE or CEE)
 *
 * Get IEEE or CEE mode DCB configuration from the Firmware
 */
static enum ice_status
ice_get_ieee_or_cee_dcb_cfg(struct ice_port_info *pi, u8 dcbx_mode)
{
	struct ice_dcbx_cfg *dcbx_cfg = NULL;
	enum ice_status ret;

	if (!pi)
		return ICE_ERR_PARAM;

	if (dcbx_mode == ICE_DCBX_MODE_IEEE)
		dcbx_cfg = &pi->local_dcbx_cfg;
	else if (dcbx_mode == ICE_DCBX_MODE_CEE)
		dcbx_cfg = &pi->desired_dcbx_cfg;

	/* Get Local DCB Config in case of ICE_DCBX_MODE_IEEE
	 * or get CEE DCB Desired Config in case of ICE_DCBX_MODE_CEE
	 */
	ret = ice_aq_get_dcb_cfg(pi->hw, ICE_AQ_LLDP_MIB_LOCAL,
				 ICE_AQ_LLDP_BRID_TYPE_NEAREST_BRID, dcbx_cfg);
	if (ret)
		goto out;

	/* Get Remote DCB Config */
	dcbx_cfg = &pi->remote_dcbx_cfg;
	ret = ice_aq_get_dcb_cfg(pi->hw, ICE_AQ_LLDP_MIB_REMOTE,
				 ICE_AQ_LLDP_BRID_TYPE_NEAREST_BRID, dcbx_cfg);
	/* Don't treat ENOENT as an error for Remote MIBs */
	if (pi->hw->adminq.sq_last_status == ICE_AQ_RC_ENOENT)
		ret = 0;

out:
	return ret;
}

/**
 * ice_get_dcb_cfg
 * @pi: port information structure
 *
 * Get DCB configuration from the Firmware
 */
static enum ice_status ice_get_dcb_cfg(struct ice_port_info *pi)
{
	struct ice_aqc_get_cee_dcb_cfg_resp cee_cfg;
	struct ice_dcbx_cfg *dcbx_cfg;
	enum ice_status ret;

	if (!pi)
		return ICE_ERR_PARAM;

	ret = ice_aq_get_cee_dcb_cfg(pi->hw, &cee_cfg, NULL);
	if (!ret) {
		/* CEE mode */
		dcbx_cfg = &pi->local_dcbx_cfg;
		dcbx_cfg->dcbx_mode = ICE_DCBX_MODE_CEE;
		dcbx_cfg->tlv_status = le32_to_cpu(cee_cfg.tlv_status);
		ice_cee_to_dcb_cfg(&cee_cfg, dcbx_cfg);
		ret = ice_get_ieee_or_cee_dcb_cfg(pi, ICE_DCBX_MODE_CEE);
	} else if (pi->hw->adminq.sq_last_status == ICE_AQ_RC_ENOENT) {
		/* CEE mode not enabled try querying IEEE data */
		dcbx_cfg = &pi->local_dcbx_cfg;
		dcbx_cfg->dcbx_mode = ICE_DCBX_MODE_IEEE;
		ret = ice_get_ieee_or_cee_dcb_cfg(pi, ICE_DCBX_MODE_IEEE);
	}

	return ret;
}

/**
 * ice_init_dcb
 * @hw: pointer to the HW struct
 *
 * Update DCB configuration from the Firmware
 */
enum ice_status ice_init_dcb(struct ice_hw *hw)
{
	struct ice_port_info *pi = hw->port_info;
	enum ice_status ret = 0;

	if (!hw->func_caps.common_cap.dcb)
		return ICE_ERR_NOT_SUPPORTED;

	pi->is_sw_lldp = true;

	/* Get DCBX status */
	pi->dcbx_status = ice_get_dcbx_status(hw);

	if (pi->dcbx_status == ICE_DCBX_STATUS_DONE ||
	    pi->dcbx_status == ICE_DCBX_STATUS_IN_PROGRESS) {
		/* Get current DCBX configuration */
		ret = ice_get_dcb_cfg(pi);
		pi->is_sw_lldp = (hw->adminq.sq_last_status == ICE_AQ_RC_EPERM);
		if (ret)
			return ret;
	} else if (pi->dcbx_status == ICE_DCBX_STATUS_DIS) {
		return ICE_ERR_NOT_READY;
	}

	/* Configure the LLDP MIB change event */
	ret = ice_aq_cfg_lldp_mib_change(hw, true, NULL);
	if (!ret)
		pi->is_sw_lldp = false;

	return ret;
}
