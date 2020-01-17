// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019, Intel Corporation. */

#include "ice_common.h"
#include "ice_flow.h"

/* Describe properties of a protocol header field */
struct ice_flow_field_info {
	enum ice_flow_seg_hdr hdr;
	s16 off;	/* Offset from start of a protocol header, in bits */
	u16 size;	/* Size of fields in bits */
};

#define ICE_FLOW_FLD_INFO(_hdr, _offset_bytes, _size_bytes) { \
	.hdr = _hdr, \
	.off = (_offset_bytes) * BITS_PER_BYTE, \
	.size = (_size_bytes) * BITS_PER_BYTE, \
}

/* Table containing properties of supported protocol header fields */
static const
struct ice_flow_field_info ice_flds_info[ICE_FLOW_FIELD_IDX_MAX] = {
	/* IPv4 / IPv6 */
	/* ICE_FLOW_FIELD_IDX_IPV4_SA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV4, 12, sizeof(struct in_addr)),
	/* ICE_FLOW_FIELD_IDX_IPV4_DA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV4, 16, sizeof(struct in_addr)),
	/* ICE_FLOW_FIELD_IDX_IPV6_SA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV6, 8, sizeof(struct in6_addr)),
	/* ICE_FLOW_FIELD_IDX_IPV6_DA */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_IPV6, 24, sizeof(struct in6_addr)),
	/* Transport */
	/* ICE_FLOW_FIELD_IDX_TCP_SRC_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_TCP, 0, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_TCP_DST_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_TCP, 2, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_UDP_SRC_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_UDP, 0, sizeof(__be16)),
	/* ICE_FLOW_FIELD_IDX_UDP_DST_PORT */
	ICE_FLOW_FLD_INFO(ICE_FLOW_SEG_HDR_UDP, 2, sizeof(__be16)),
};

/* Bitmaps indicating relevant packet types for a particular protocol header
 *
 * Packet types for packets with an Outer/First/Single IPv4 header
 */
static const u32 ice_ptypes_ipv4_ofos[] = {
	0x1DC00000, 0x04000800, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last IPv4 header */
static const u32 ice_ptypes_ipv4_il[] = {
	0xE0000000, 0xB807700E, 0x80000003, 0xE01DC03B,
	0x0000000E, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Outer/First/Single IPv6 header */
static const u32 ice_ptypes_ipv6_ofos[] = {
	0x00000000, 0x00000000, 0x77000000, 0x10002000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last IPv6 header */
static const u32 ice_ptypes_ipv6_il[] = {
	0x00000000, 0x03B80770, 0x000001DC, 0x0EE00000,
	0x00000770, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* UDP Packet types for non-tunneled packets or tunneled
 * packets with inner UDP.
 */
static const u32 ice_ptypes_udp_il[] = {
	0x81000000, 0x20204040, 0x04000010, 0x80810102,
	0x00000040, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last TCP header */
static const u32 ice_ptypes_tcp_il[] = {
	0x04000000, 0x80810102, 0x10000040, 0x02040408,
	0x00000102, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Packet types for packets with an Innermost/Last SCTP header */
static const u32 ice_ptypes_sctp_il[] = {
	0x08000000, 0x01020204, 0x20000081, 0x04080810,
	0x00000204, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
};

/* Manage parameters and info. used during the creation of a flow profile */
struct ice_flow_prof_params {
	enum ice_block blk;
	u16 entry_length; /* # of bytes formatted entry will require */
	u8 es_cnt;
	struct ice_flow_prof *prof;

	/* For ACL, the es[0] will have the data of ICE_RX_MDID_PKT_FLAGS_15_0
	 * This will give us the direction flags.
	 */
	struct ice_fv_word es[ICE_MAX_FV_WORDS];
	DECLARE_BITMAP(ptypes, ICE_FLOW_PTYPE_MAX);
};

#define ICE_FLOW_SEG_HDRS_L3_MASK	\
	(ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV6)
#define ICE_FLOW_SEG_HDRS_L4_MASK	\
	(ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_UDP | ICE_FLOW_SEG_HDR_SCTP)

/**
 * ice_flow_val_hdrs - validates packet segments for valid protocol headers
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 */
static enum ice_status
ice_flow_val_hdrs(struct ice_flow_seg_info *segs, u8 segs_cnt)
{
	u8 i;

	for (i = 0; i < segs_cnt; i++) {
		/* Multiple L3 headers */
		if (segs[i].hdrs & ICE_FLOW_SEG_HDRS_L3_MASK &&
		    !is_power_of_2(segs[i].hdrs & ICE_FLOW_SEG_HDRS_L3_MASK))
			return ICE_ERR_PARAM;

		/* Multiple L4 headers */
		if (segs[i].hdrs & ICE_FLOW_SEG_HDRS_L4_MASK &&
		    !is_power_of_2(segs[i].hdrs & ICE_FLOW_SEG_HDRS_L4_MASK))
			return ICE_ERR_PARAM;
	}

	return 0;
}

/**
 * ice_flow_proc_seg_hdrs - process protocol headers present in pkt segments
 * @params: information about the flow to be processed
 *
 * This function identifies the packet types associated with the protocol
 * headers being present in packet segments of the specified flow profile.
 */
static enum ice_status
ice_flow_proc_seg_hdrs(struct ice_flow_prof_params *params)
{
	struct ice_flow_prof *prof;
	u8 i;

	memset(params->ptypes, 0xff, sizeof(params->ptypes));

	prof = params->prof;

	for (i = 0; i < params->prof->segs_cnt; i++) {
		const unsigned long *src;
		u32 hdrs;

		hdrs = prof->segs[i].hdrs;

		if (hdrs & ICE_FLOW_SEG_HDR_IPV4) {
			src = !i ? (const unsigned long *)ice_ptypes_ipv4_ofos :
				(const unsigned long *)ice_ptypes_ipv4_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_IPV6) {
			src = !i ? (const unsigned long *)ice_ptypes_ipv6_ofos :
				(const unsigned long *)ice_ptypes_ipv6_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		}

		if (hdrs & ICE_FLOW_SEG_HDR_UDP) {
			src = (const unsigned long *)ice_ptypes_udp_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_TCP) {
			bitmap_and(params->ptypes, params->ptypes,
				   (const unsigned long *)ice_ptypes_tcp_il,
				   ICE_FLOW_PTYPE_MAX);
		} else if (hdrs & ICE_FLOW_SEG_HDR_SCTP) {
			src = (const unsigned long *)ice_ptypes_sctp_il;
			bitmap_and(params->ptypes, params->ptypes, src,
				   ICE_FLOW_PTYPE_MAX);
		}
	}

	return 0;
}

/**
 * ice_flow_xtract_fld - Create an extraction sequence entry for the given field
 * @hw: pointer to the HW struct
 * @params: information about the flow to be processed
 * @seg: packet segment index of the field to be extracted
 * @fld: ID of field to be extracted
 *
 * This function determines the protocol ID, offset, and size of the given
 * field. It then allocates one or more extraction sequence entries for the
 * given field, and fill the entries with protocol ID and offset information.
 */
static enum ice_status
ice_flow_xtract_fld(struct ice_hw *hw, struct ice_flow_prof_params *params,
		    u8 seg, enum ice_flow_field fld)
{
	enum ice_prot_id prot_id = ICE_PROT_ID_INVAL;
	u8 fv_words = hw->blk[params->blk].es.fvw;
	struct ice_flow_fld_info *flds;
	u16 cnt, ese_bits, i;
	u16 off;

	flds = params->prof->segs[seg].fields;

	switch (fld) {
	case ICE_FLOW_FIELD_IDX_IPV4_SA:
	case ICE_FLOW_FIELD_IDX_IPV4_DA:
		prot_id = seg == 0 ? ICE_PROT_IPV4_OF_OR_S : ICE_PROT_IPV4_IL;
		break;
	case ICE_FLOW_FIELD_IDX_IPV6_SA:
	case ICE_FLOW_FIELD_IDX_IPV6_DA:
		prot_id = seg == 0 ? ICE_PROT_IPV6_OF_OR_S : ICE_PROT_IPV6_IL;
		break;
	case ICE_FLOW_FIELD_IDX_TCP_SRC_PORT:
	case ICE_FLOW_FIELD_IDX_TCP_DST_PORT:
		prot_id = ICE_PROT_TCP_IL;
		break;
	case ICE_FLOW_FIELD_IDX_UDP_SRC_PORT:
	case ICE_FLOW_FIELD_IDX_UDP_DST_PORT:
		prot_id = ICE_PROT_UDP_IL_OR_S;
		break;
	default:
		return ICE_ERR_NOT_IMPL;
	}

	/* Each extraction sequence entry is a word in size, and extracts a
	 * word-aligned offset from a protocol header.
	 */
	ese_bits = ICE_FLOW_FV_EXTRACT_SZ * BITS_PER_BYTE;

	flds[fld].xtrct.prot_id = prot_id;
	flds[fld].xtrct.off = (ice_flds_info[fld].off / ese_bits) *
		ICE_FLOW_FV_EXTRACT_SZ;
	flds[fld].xtrct.disp = (u8)(ice_flds_info[fld].off % ese_bits);
	flds[fld].xtrct.idx = params->es_cnt;

	/* Adjust the next field-entry index after accommodating the number of
	 * entries this field consumes
	 */
	cnt = DIV_ROUND_UP(flds[fld].xtrct.disp + ice_flds_info[fld].size,
			   ese_bits);

	/* Fill in the extraction sequence entries needed for this field */
	off = flds[fld].xtrct.off;
	for (i = 0; i < cnt; i++) {
		u8 idx;

		/* Make sure the number of extraction sequence required
		 * does not exceed the block's capability
		 */
		if (params->es_cnt >= fv_words)
			return ICE_ERR_MAX_LIMIT;

		/* some blocks require a reversed field vector layout */
		if (hw->blk[params->blk].es.reverse)
			idx = fv_words - params->es_cnt - 1;
		else
			idx = params->es_cnt;

		params->es[idx].prot_id = prot_id;
		params->es[idx].off = off;
		params->es_cnt++;

		off += ICE_FLOW_FV_EXTRACT_SZ;
	}

	return 0;
}

/**
 * ice_flow_create_xtrct_seq - Create an extraction sequence for given segments
 * @hw: pointer to the HW struct
 * @params: information about the flow to be processed
 *
 * This function iterates through all matched fields in the given segments, and
 * creates an extraction sequence for the fields.
 */
static enum ice_status
ice_flow_create_xtrct_seq(struct ice_hw *hw,
			  struct ice_flow_prof_params *params)
{
	struct ice_flow_prof *prof = params->prof;
	enum ice_status status = 0;
	u8 i;

	for (i = 0; i < prof->segs_cnt; i++) {
		u8 j;

		for_each_set_bit(j, (unsigned long *)&prof->segs[i].match,
				 ICE_FLOW_FIELD_IDX_MAX) {
			status = ice_flow_xtract_fld(hw, params, i,
						     (enum ice_flow_field)j);
			if (status)
				return status;
		}
	}

	return status;
}

/**
 * ice_flow_proc_segs - process all packet segments associated with a profile
 * @hw: pointer to the HW struct
 * @params: information about the flow to be processed
 */
static enum ice_status
ice_flow_proc_segs(struct ice_hw *hw, struct ice_flow_prof_params *params)
{
	enum ice_status status;

	status = ice_flow_proc_seg_hdrs(params);
	if (status)
		return status;

	status = ice_flow_create_xtrct_seq(hw, params);
	if (status)
		return status;

	switch (params->blk) {
	case ICE_BLK_RSS:
		/* Only header information is provided for RSS configuration.
		 * No further processing is needed.
		 */
		status = 0;
		break;
	default:
		return ICE_ERR_NOT_IMPL;
	}

	return status;
}

/**
 * ice_flow_add_prof_sync - Add a flow profile for packet segments and fields
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @dir: flow direction
 * @prof_id: unique ID to identify this flow profile
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 * @prof: stores the returned flow profile added
 *
 * Assumption: the caller has acquired the lock to the profile list
 */
static enum ice_status
ice_flow_add_prof_sync(struct ice_hw *hw, enum ice_block blk,
		       enum ice_flow_dir dir, u64 prof_id,
		       struct ice_flow_seg_info *segs, u8 segs_cnt,
		       struct ice_flow_prof **prof)
{
	struct ice_flow_prof_params params;
	enum ice_status status;
	u8 i;

	if (!prof)
		return ICE_ERR_BAD_PTR;

	memset(&params, 0, sizeof(params));
	params.prof = devm_kzalloc(ice_hw_to_dev(hw), sizeof(*params.prof),
				   GFP_KERNEL);
	if (!params.prof)
		return ICE_ERR_NO_MEMORY;

	/* initialize extraction sequence to all invalid (0xff) */
	for (i = 0; i < ICE_MAX_FV_WORDS; i++) {
		params.es[i].prot_id = ICE_PROT_INVALID;
		params.es[i].off = ICE_FV_OFFSET_INVAL;
	}

	params.blk = blk;
	params.prof->id = prof_id;
	params.prof->dir = dir;
	params.prof->segs_cnt = segs_cnt;

	/* Make a copy of the segments that need to be persistent in the flow
	 * profile instance
	 */
	for (i = 0; i < segs_cnt; i++)
		memcpy(&params.prof->segs[i], &segs[i], sizeof(*segs));

	status = ice_flow_proc_segs(hw, &params);
	if (status) {
		ice_debug(hw, ICE_DBG_FLOW,
			  "Error processing a flow's packet segments\n");
		goto out;
	}

	/* Add a HW profile for this flow profile */
	status = ice_add_prof(hw, blk, prof_id, (u8 *)params.ptypes, params.es);
	if (status) {
		ice_debug(hw, ICE_DBG_FLOW, "Error adding a HW flow profile\n");
		goto out;
	}

	INIT_LIST_HEAD(&params.prof->entries);
	mutex_init(&params.prof->entries_lock);
	*prof = params.prof;

out:
	if (status)
		devm_kfree(ice_hw_to_dev(hw), params.prof);

	return status;
}

/**
 * ice_flow_add_prof - Add a flow profile for packet segments and matched fields
 * @hw: pointer to the HW struct
 * @blk: classification stage
 * @dir: flow direction
 * @prof_id: unique ID to identify this flow profile
 * @segs: array of one or more packet segments that describe the flow
 * @segs_cnt: number of packet segments provided
 */
static enum ice_status
ice_flow_add_prof(struct ice_hw *hw, enum ice_block blk, enum ice_flow_dir dir,
		  u64 prof_id, struct ice_flow_seg_info *segs, u8 segs_cnt)
{
	struct ice_flow_prof *prof = NULL;
	enum ice_status status;

	if (segs_cnt > ICE_FLOW_SEG_MAX)
		return ICE_ERR_MAX_LIMIT;

	if (!segs_cnt)
		return ICE_ERR_PARAM;

	if (!segs)
		return ICE_ERR_BAD_PTR;

	status = ice_flow_val_hdrs(segs, segs_cnt);
	if (status)
		return status;

	mutex_lock(&hw->fl_profs_locks[blk]);

	status = ice_flow_add_prof_sync(hw, blk, dir, prof_id, segs, segs_cnt,
					&prof);
	if (!status)
		list_add(&prof->l_entry, &hw->fl_profs[blk]);

	mutex_unlock(&hw->fl_profs_locks[blk]);

	return status;
}

/**
 * ice_flow_set_fld_ext - specifies locations of field from entry's input buffer
 * @seg: packet segment the field being set belongs to
 * @fld: field to be set
 * @type: type of the field
 * @val_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of the value to match from
 *           entry's input buffer
 * @mask_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of mask value from entry's
 *            input buffer
 * @last_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of last/upper value from
 *            entry's input buffer
 *
 * This helper function stores information of a field being matched, including
 * the type of the field and the locations of the value to match, the mask, and
 * and the upper-bound value in the start of the input buffer for a flow entry.
 * This function should only be used for fixed-size data structures.
 *
 * This function also opportunistically determines the protocol headers to be
 * present based on the fields being set. Some fields cannot be used alone to
 * determine the protocol headers present. Sometimes, fields for particular
 * protocol headers are not matched. In those cases, the protocol headers
 * must be explicitly set.
 */
static void
ice_flow_set_fld_ext(struct ice_flow_seg_info *seg, enum ice_flow_field fld,
		     enum ice_flow_fld_match_type type, u16 val_loc,
		     u16 mask_loc, u16 last_loc)
{
	u64 bit = BIT_ULL(fld);

	seg->match |= bit;
	if (type == ICE_FLOW_FLD_TYPE_RANGE)
		seg->range |= bit;

	seg->fields[fld].type = type;
	seg->fields[fld].src.val = val_loc;
	seg->fields[fld].src.mask = mask_loc;
	seg->fields[fld].src.last = last_loc;

	ICE_FLOW_SET_HDRS(seg, ice_flds_info[fld].hdr);
}

/**
 * ice_flow_set_fld - specifies locations of field from entry's input buffer
 * @seg: packet segment the field being set belongs to
 * @fld: field to be set
 * @val_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of the value to match from
 *           entry's input buffer
 * @mask_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of mask value from entry's
 *            input buffer
 * @last_loc: if not ICE_FLOW_FLD_OFF_INVAL, location of last/upper value from
 *            entry's input buffer
 * @range: indicate if field being matched is to be in a range
 *
 * This function specifies the locations, in the form of byte offsets from the
 * start of the input buffer for a flow entry, from where the value to match,
 * the mask value, and upper value can be extracted. These locations are then
 * stored in the flow profile. When adding a flow entry associated with the
 * flow profile, these locations will be used to quickly extract the values and
 * create the content of a match entry. This function should only be used for
 * fixed-size data structures.
 */
static void
ice_flow_set_fld(struct ice_flow_seg_info *seg, enum ice_flow_field fld,
		 u16 val_loc, u16 mask_loc, u16 last_loc, bool range)
{
	enum ice_flow_fld_match_type t = range ?
		ICE_FLOW_FLD_TYPE_RANGE : ICE_FLOW_FLD_TYPE_REG;

	ice_flow_set_fld_ext(seg, fld, t, val_loc, mask_loc, last_loc);
}

#define ICE_FLOW_RSS_SEG_HDR_L3_MASKS \
	(ICE_FLOW_SEG_HDR_IPV4 | ICE_FLOW_SEG_HDR_IPV6)

#define ICE_FLOW_RSS_SEG_HDR_L4_MASKS \
	(ICE_FLOW_SEG_HDR_TCP | ICE_FLOW_SEG_HDR_UDP | ICE_FLOW_SEG_HDR_SCTP)

#define ICE_FLOW_RSS_SEG_HDR_VAL_MASKS \
	(ICE_FLOW_RSS_SEG_HDR_L3_MASKS | \
	 ICE_FLOW_RSS_SEG_HDR_L4_MASKS)

/**
 * ice_flow_set_rss_seg_info - setup packet segments for RSS
 * @segs: pointer to the flow field segment(s)
 * @hash_fields: fields to be hashed on for the segment(s)
 * @flow_hdr: protocol header fields within a packet segment
 *
 * Helper function to extract fields from hash bitmap and use flow
 * header value to set flow field segment for further use in flow
 * profile entry or removal.
 */
static enum ice_status
ice_flow_set_rss_seg_info(struct ice_flow_seg_info *segs, u64 hash_fields,
			  u32 flow_hdr)
{
	u64 val;
	u8 i;

	for_each_set_bit(i, (unsigned long *)&hash_fields,
			 ICE_FLOW_FIELD_IDX_MAX)
		ice_flow_set_fld(segs, (enum ice_flow_field)i,
				 ICE_FLOW_FLD_OFF_INVAL, ICE_FLOW_FLD_OFF_INVAL,
				 ICE_FLOW_FLD_OFF_INVAL, false);

	ICE_FLOW_SET_HDRS(segs, flow_hdr);

	if (segs->hdrs & ~ICE_FLOW_RSS_SEG_HDR_VAL_MASKS)
		return ICE_ERR_PARAM;

	val = (u64)(segs->hdrs & ICE_FLOW_RSS_SEG_HDR_L3_MASKS);
	if (val && !is_power_of_2(val))
		return ICE_ERR_CFG;

	val = (u64)(segs->hdrs & ICE_FLOW_RSS_SEG_HDR_L4_MASKS);
	if (val && !is_power_of_2(val))
		return ICE_ERR_CFG;

	return 0;
}

#define ICE_FLOW_PROF_HASH_S	0
#define ICE_FLOW_PROF_HASH_M	(0xFFFFFFFFULL << ICE_FLOW_PROF_HASH_S)
#define ICE_FLOW_PROF_HDR_S	32
#define ICE_FLOW_PROF_HDR_M	(0x3FFFFFFFULL << ICE_FLOW_PROF_HDR_S)
#define ICE_FLOW_PROF_ENCAP_S	63
#define ICE_FLOW_PROF_ENCAP_M	(BIT_ULL(ICE_FLOW_PROF_ENCAP_S))

#define ICE_RSS_OUTER_HEADERS	1

/* Flow profile ID format:
 * [0:31] - Packet match fields
 * [32:62] - Protocol header
 * [63] - Encapsulation flag, 0 if non-tunneled, 1 if tunneled
 */
#define ICE_FLOW_GEN_PROFID(hash, hdr, segs_cnt) \
	(u64)(((u64)(hash) & ICE_FLOW_PROF_HASH_M) | \
	      (((u64)(hdr) << ICE_FLOW_PROF_HDR_S) & ICE_FLOW_PROF_HDR_M) | \
	      ((u8)((segs_cnt) - 1) ? ICE_FLOW_PROF_ENCAP_M : 0))

/**
 * ice_add_rss_cfg_sync - add an RSS configuration
 * @hw: pointer to the hardware structure
 * @hashed_flds: hash bit fields (ICE_FLOW_HASH_*) to configure
 * @addl_hdrs: protocol header fields
 * @segs_cnt: packet segment count
 *
 * Assumption: lock has already been acquired for RSS list
 */
static enum ice_status
ice_add_rss_cfg_sync(struct ice_hw *hw, u64 hashed_flds, u32 addl_hdrs,
		     u8 segs_cnt)
{
	struct ice_flow_seg_info *segs;
	enum ice_status status;

	if (!segs_cnt || segs_cnt > ICE_FLOW_SEG_MAX)
		return ICE_ERR_PARAM;

	segs = kcalloc(segs_cnt, sizeof(*segs), GFP_KERNEL);
	if (!segs)
		return ICE_ERR_NO_MEMORY;

	/* Construct the packet segment info from the hashed fields */
	status = ice_flow_set_rss_seg_info(&segs[segs_cnt - 1], hashed_flds,
					   addl_hdrs);
	if (status)
		goto exit;

	/* Create a new flow profile with generated profile and packet
	 * segment information.
	 */
	status = ice_flow_add_prof(hw, ICE_BLK_RSS, ICE_FLOW_RX,
				   ICE_FLOW_GEN_PROFID(hashed_flds,
						       segs[segs_cnt - 1].hdrs,
						       segs_cnt),
				   segs, segs_cnt);

exit:
	kfree(segs);
	return status;
}

/**
 * ice_add_rss_cfg - add an RSS configuration with specified hashed fields
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 * @hashed_flds: hash bit fields (ICE_FLOW_HASH_*) to configure
 * @addl_hdrs: protocol header fields
 *
 * This function will generate a flow profile based on fields associated with
 * the input fields to hash on, the flow type and use the VSI number to add
 * a flow entry to the profile.
 */
enum ice_status
ice_add_rss_cfg(struct ice_hw *hw, u16 vsi_handle, u64 hashed_flds,
		u32 addl_hdrs)
{
	enum ice_status status;

	if (hashed_flds == ICE_HASH_INVALID ||
	    !ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;

	mutex_lock(&hw->rss_locks);
	status = ice_add_rss_cfg_sync(hw, hashed_flds, addl_hdrs,
				      ICE_RSS_OUTER_HEADERS);
	mutex_unlock(&hw->rss_locks);

	return status;
}

/**
 * ice_replay_rss_cfg - replay RSS configurations associated with VSI
 * @hw: pointer to the hardware structure
 * @vsi_handle: software VSI handle
 */
enum ice_status ice_replay_rss_cfg(struct ice_hw *hw, u16 vsi_handle)
{
	enum ice_status status = 0;
	struct ice_rss_cfg *r;

	if (!ice_is_vsi_valid(hw, vsi_handle))
		return ICE_ERR_PARAM;

	mutex_lock(&hw->rss_locks);
	list_for_each_entry(r, &hw->rss_list_head, l_entry) {
		if (test_bit(vsi_handle, r->vsis)) {
			status = ice_add_rss_cfg_sync(hw, r->hashed_flds,
						      r->packet_hdr,
						      ICE_RSS_OUTER_HEADERS);
			if (status)
				break;
		}
	}
	mutex_unlock(&hw->rss_locks);

	return status;
}
