// SPDX-License-Identifier: GPL-2.0
/* Marvell OcteonTx2 RVU Admin Function driver
 *
 * Copyright (C) 2020 Marvell.
 */

#include <linux/bitfield.h>

#include "rvu_struct.h"
#include "rvu_reg.h"
#include "rvu.h"
#include "npc.h"

#define NPC_BYTESM		GENMASK_ULL(19, 16)
#define NPC_HDR_OFFSET		GENMASK_ULL(15, 8)
#define NPC_KEY_OFFSET		GENMASK_ULL(5, 0)
#define NPC_LDATA_EN		BIT_ULL(7)

static const char * const npc_flow_names[] = {
	[NPC_DMAC]	= "dmac",
	[NPC_SMAC]	= "smac",
	[NPC_ETYPE]	= "ether type",
	[NPC_OUTER_VID]	= "outer vlan id",
	[NPC_TOS]	= "tos",
	[NPC_SIP_IPV4]	= "ipv4 source ip",
	[NPC_DIP_IPV4]	= "ipv4 destination ip",
	[NPC_SIP_IPV6]	= "ipv6 source ip",
	[NPC_DIP_IPV6]	= "ipv6 destination ip",
	[NPC_SPORT_TCP]	= "tcp source port",
	[NPC_DPORT_TCP]	= "tcp destination port",
	[NPC_SPORT_UDP]	= "udp source port",
	[NPC_DPORT_UDP]	= "udp destination port",
	[NPC_SPORT_SCTP] = "sctp source port",
	[NPC_DPORT_SCTP] = "sctp destination port",
	[NPC_UNKNOWN]	= "unknown",
};

const char *npc_get_field_name(u8 hdr)
{
	if (hdr >= ARRAY_SIZE(npc_flow_names))
		return npc_flow_names[NPC_UNKNOWN];

	return npc_flow_names[hdr];
}

/* Compute keyword masks and figure out the number of keywords a field
 * spans in the key.
 */
static void npc_set_kw_masks(struct npc_mcam *mcam, u8 type,
			     u8 nr_bits, int start_kwi, int offset, u8 intf)
{
	struct npc_key_field *field = &mcam->rx_key_fields[type];
	u8 bits_in_kw;
	int max_kwi;

	if (mcam->banks_per_entry == 1)
		max_kwi = 1; /* NPC_MCAM_KEY_X1 */
	else if (mcam->banks_per_entry == 2)
		max_kwi = 3; /* NPC_MCAM_KEY_X2 */
	else
		max_kwi = 6; /* NPC_MCAM_KEY_X4 */

	if (is_npc_intf_tx(intf))
		field = &mcam->tx_key_fields[type];

	if (offset + nr_bits <= 64) {
		/* one KW only */
		if (start_kwi > max_kwi)
			return;
		field->kw_mask[start_kwi] |= GENMASK_ULL(nr_bits - 1, 0)
					     << offset;
		field->nr_kws = 1;
	} else if (offset + nr_bits > 64 &&
		   offset + nr_bits <= 128) {
		/* two KWs */
		if (start_kwi + 1 > max_kwi)
			return;
		/* first KW mask */
		bits_in_kw = 64 - offset;
		field->kw_mask[start_kwi] |= GENMASK_ULL(bits_in_kw - 1, 0)
					     << offset;
		/* second KW mask i.e. mask for rest of bits */
		bits_in_kw = nr_bits + offset - 64;
		field->kw_mask[start_kwi + 1] |= GENMASK_ULL(bits_in_kw - 1, 0);
		field->nr_kws = 2;
	} else {
		/* three KWs */
		if (start_kwi + 2 > max_kwi)
			return;
		/* first KW mask */
		bits_in_kw = 64 - offset;
		field->kw_mask[start_kwi] |= GENMASK_ULL(bits_in_kw - 1, 0)
					     << offset;
		/* second KW mask */
		field->kw_mask[start_kwi + 1] = ~0ULL;
		/* third KW mask i.e. mask for rest of bits */
		bits_in_kw = nr_bits + offset - 128;
		field->kw_mask[start_kwi + 2] |= GENMASK_ULL(bits_in_kw - 1, 0);
		field->nr_kws = 3;
	}
}

/* Helper function to figure out whether field exists in the key */
static bool npc_is_field_present(struct rvu *rvu, enum key_fields type, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct npc_key_field *input;

	input  = &mcam->rx_key_fields[type];
	if (is_npc_intf_tx(intf))
		input  = &mcam->tx_key_fields[type];

	return input->nr_kws > 0;
}

static bool npc_is_same(struct npc_key_field *input,
			struct npc_key_field *field)
{
	int ret;

	ret = memcmp(&input->layer_mdata, &field->layer_mdata,
		     sizeof(struct npc_layer_mdata));
	return ret == 0;
}

static void npc_set_layer_mdata(struct npc_mcam *mcam, enum key_fields type,
				u64 cfg, u8 lid, u8 lt, u8 intf)
{
	struct npc_key_field *input = &mcam->rx_key_fields[type];

	if (is_npc_intf_tx(intf))
		input = &mcam->tx_key_fields[type];

	input->layer_mdata.hdr = FIELD_GET(NPC_HDR_OFFSET, cfg);
	input->layer_mdata.key = FIELD_GET(NPC_KEY_OFFSET, cfg);
	input->layer_mdata.len = FIELD_GET(NPC_BYTESM, cfg) + 1;
	input->layer_mdata.ltype = lt;
	input->layer_mdata.lid = lid;
}

static bool npc_check_overlap_fields(struct npc_key_field *input1,
				     struct npc_key_field *input2)
{
	int kwi;

	/* Fields with same layer id and different ltypes are mutually
	 * exclusive hence they can be overlapped
	 */
	if (input1->layer_mdata.lid == input2->layer_mdata.lid &&
	    input1->layer_mdata.ltype != input2->layer_mdata.ltype)
		return false;

	for (kwi = 0; kwi < NPC_MAX_KWS_IN_KEY; kwi++) {
		if (input1->kw_mask[kwi] & input2->kw_mask[kwi])
			return true;
	}

	return false;
}

/* Helper function to check whether given field overlaps with any other fields
 * in the key. Due to limitations on key size and the key extraction profile in
 * use higher layers can overwrite lower layer's header fields. Hence overlap
 * needs to be checked.
 */
static bool npc_check_overlap(struct rvu *rvu, int blkaddr,
			      enum key_fields type, u8 start_lid, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct npc_key_field *dummy, *input;
	int start_kwi, offset;
	u8 nr_bits, lid, lt, ld;
	u64 cfg;

	dummy = &mcam->rx_key_fields[NPC_UNKNOWN];
	input = &mcam->rx_key_fields[type];

	if (is_npc_intf_tx(intf)) {
		dummy = &mcam->tx_key_fields[NPC_UNKNOWN];
		input = &mcam->tx_key_fields[type];
	}

	for (lid = start_lid; lid < NPC_MAX_LID; lid++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			for (ld = 0; ld < NPC_MAX_LD; ld++) {
				cfg = rvu_read64(rvu, blkaddr,
						 NPC_AF_INTFX_LIDX_LTX_LDX_CFG
						 (intf, lid, lt, ld));
				if (!FIELD_GET(NPC_LDATA_EN, cfg))
					continue;
				memset(dummy, 0, sizeof(struct npc_key_field));
				npc_set_layer_mdata(mcam, NPC_UNKNOWN, cfg,
						    lid, lt, intf);
				/* exclude input */
				if (npc_is_same(input, dummy))
					continue;
				start_kwi = dummy->layer_mdata.key / 8;
				offset = (dummy->layer_mdata.key * 8) % 64;
				nr_bits = dummy->layer_mdata.len * 8;
				/* form KW masks */
				npc_set_kw_masks(mcam, NPC_UNKNOWN, nr_bits,
						 start_kwi, offset, intf);
				/* check any input field bits falls in any
				 * other field bits.
				 */
				if (npc_check_overlap_fields(dummy, input))
					return true;
			}
		}
	}

	return false;
}

static int npc_check_field(struct rvu *rvu, int blkaddr, enum key_fields type,
			   u8 intf)
{
	if (!npc_is_field_present(rvu, type, intf) ||
	    npc_check_overlap(rvu, blkaddr, type, 0, intf))
		return -EOPNOTSUPP;
	return 0;
}

static void npc_scan_parse_result(struct npc_mcam *mcam, u8 bit_number,
				  u8 key_nibble, u8 intf)
{
	u8 offset = (key_nibble * 4) % 64; /* offset within key word */
	u8 kwi = (key_nibble * 4) / 64; /* which word in key */
	u8 nr_bits = 4; /* bits in a nibble */
	u8 type;

	switch (bit_number) {
	case 0 ... 2:
		type = NPC_CHAN;
		break;
	case 3:
		type = NPC_ERRLEV;
		break;
	case 4 ... 5:
		type = NPC_ERRCODE;
		break;
	case 6:
		type = NPC_LXMB;
		break;
	/* check for LTYPE only as of now */
	case 9:
		type = NPC_LA;
		break;
	case 12:
		type = NPC_LB;
		break;
	case 15:
		type = NPC_LC;
		break;
	case 18:
		type = NPC_LD;
		break;
	case 21:
		type = NPC_LE;
		break;
	case 24:
		type = NPC_LF;
		break;
	case 27:
		type = NPC_LG;
		break;
	case 30:
		type = NPC_LH;
		break;
	default:
		return;
	};
	npc_set_kw_masks(mcam, type, nr_bits, kwi, offset, intf);
}

static void npc_handle_multi_layer_fields(struct rvu *rvu, int blkaddr, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	struct npc_key_field *key_fields;
	/* Ether type can come from three layers
	 * (ethernet, single tagged, double tagged)
	 */
	struct npc_key_field *etype_ether;
	struct npc_key_field *etype_tag1;
	struct npc_key_field *etype_tag2;
	/* Outer VLAN TCI can come from two layers
	 * (single tagged, double tagged)
	 */
	struct npc_key_field *vlan_tag1;
	struct npc_key_field *vlan_tag2;
	u64 *features;
	u8 start_lid;
	int i;

	key_fields = mcam->rx_key_fields;
	features = &mcam->rx_features;

	if (is_npc_intf_tx(intf)) {
		key_fields = mcam->tx_key_fields;
		features = &mcam->tx_features;
	}

	/* Handle header fields which can come from multiple layers like
	 * etype, outer vlan tci. These fields should have same position in
	 * the key otherwise to install a mcam rule more than one entry is
	 * needed which complicates mcam space management.
	 */
	etype_ether = &key_fields[NPC_ETYPE_ETHER];
	etype_tag1 = &key_fields[NPC_ETYPE_TAG1];
	etype_tag2 = &key_fields[NPC_ETYPE_TAG2];
	vlan_tag1 = &key_fields[NPC_VLAN_TAG1];
	vlan_tag2 = &key_fields[NPC_VLAN_TAG2];

	/* if key profile programmed does not extract Ethertype at all */
	if (!etype_ether->nr_kws && !etype_tag1->nr_kws && !etype_tag2->nr_kws)
		goto vlan_tci;

	/* if key profile programmed extracts Ethertype from one layer */
	if (etype_ether->nr_kws && !etype_tag1->nr_kws && !etype_tag2->nr_kws)
		key_fields[NPC_ETYPE] = *etype_ether;
	if (!etype_ether->nr_kws && etype_tag1->nr_kws && !etype_tag2->nr_kws)
		key_fields[NPC_ETYPE] = *etype_tag1;
	if (!etype_ether->nr_kws && !etype_tag1->nr_kws && etype_tag2->nr_kws)
		key_fields[NPC_ETYPE] = *etype_tag2;

	/* if key profile programmed extracts Ethertype from multiple layers */
	if (etype_ether->nr_kws && etype_tag1->nr_kws) {
		for (i = 0; i < NPC_MAX_KWS_IN_KEY; i++) {
			if (etype_ether->kw_mask[i] != etype_tag1->kw_mask[i])
				goto vlan_tci;
		}
		key_fields[NPC_ETYPE] = *etype_tag1;
	}
	if (etype_ether->nr_kws && etype_tag2->nr_kws) {
		for (i = 0; i < NPC_MAX_KWS_IN_KEY; i++) {
			if (etype_ether->kw_mask[i] != etype_tag2->kw_mask[i])
				goto vlan_tci;
		}
		key_fields[NPC_ETYPE] = *etype_tag2;
	}
	if (etype_tag1->nr_kws && etype_tag2->nr_kws) {
		for (i = 0; i < NPC_MAX_KWS_IN_KEY; i++) {
			if (etype_tag1->kw_mask[i] != etype_tag2->kw_mask[i])
				goto vlan_tci;
		}
		key_fields[NPC_ETYPE] = *etype_tag2;
	}

	/* check none of higher layers overwrite Ethertype */
	start_lid = key_fields[NPC_ETYPE].layer_mdata.lid + 1;
	if (npc_check_overlap(rvu, blkaddr, NPC_ETYPE, start_lid, intf))
		goto vlan_tci;
	*features |= BIT_ULL(NPC_ETYPE);
vlan_tci:
	/* if key profile does not extract outer vlan tci at all */
	if (!vlan_tag1->nr_kws && !vlan_tag2->nr_kws)
		goto done;

	/* if key profile extracts outer vlan tci from one layer */
	if (vlan_tag1->nr_kws && !vlan_tag2->nr_kws)
		key_fields[NPC_OUTER_VID] = *vlan_tag1;
	if (!vlan_tag1->nr_kws && vlan_tag2->nr_kws)
		key_fields[NPC_OUTER_VID] = *vlan_tag2;

	/* if key profile extracts outer vlan tci from multiple layers */
	if (vlan_tag1->nr_kws && vlan_tag2->nr_kws) {
		for (i = 0; i < NPC_MAX_KWS_IN_KEY; i++) {
			if (vlan_tag1->kw_mask[i] != vlan_tag2->kw_mask[i])
				goto done;
		}
		key_fields[NPC_OUTER_VID] = *vlan_tag2;
	}
	/* check none of higher layers overwrite outer vlan tci */
	start_lid = key_fields[NPC_OUTER_VID].layer_mdata.lid + 1;
	if (npc_check_overlap(rvu, blkaddr, NPC_OUTER_VID, start_lid, intf))
		goto done;
	*features |= BIT_ULL(NPC_OUTER_VID);
done:
	return;
}

static void npc_scan_ldata(struct rvu *rvu, int blkaddr, u8 lid,
			   u8 lt, u64 cfg, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u8 hdr, key, nr_bytes, bit_offset;
	u8 la_ltype, la_start;
	/* starting KW index and starting bit position */
	int start_kwi, offset;

	nr_bytes = FIELD_GET(NPC_BYTESM, cfg) + 1;
	hdr = FIELD_GET(NPC_HDR_OFFSET, cfg);
	key = FIELD_GET(NPC_KEY_OFFSET, cfg);
	start_kwi = key / 8;
	offset = (key * 8) % 64;

	/* For Tx, Layer A has NIX_INST_HDR_S(64 bytes) preceding
	 * ethernet header.
	 */
	if (is_npc_intf_tx(intf)) {
		la_ltype = NPC_LT_LA_IH_NIX_ETHER;
		la_start = 8;
	} else {
		la_ltype = NPC_LT_LA_ETHER;
		la_start = 0;
	}

#define NPC_SCAN_HDR(name, hlid, hlt, hstart, hlen)			       \
do {									       \
	if (lid == (hlid) && lt == (hlt)) {				       \
		if ((hstart) >= hdr &&					       \
		    ((hstart) + (hlen)) <= (hdr + nr_bytes)) {	               \
			bit_offset = (hdr + nr_bytes - (hstart) - (hlen)) * 8; \
			npc_set_layer_mdata(mcam, (name), cfg, lid, lt, intf); \
			npc_set_kw_masks(mcam, (name), (hlen) * 8,	       \
					 start_kwi, offset + bit_offset, intf);\
		}							       \
	}								       \
} while (0)

	/* List LID, LTYPE, start offset from layer and length(in bytes) of
	 * packet header fields below.
	 * Example: Source IP is 4 bytes and starts at 12th byte of IP header
	 */
	NPC_SCAN_HDR(NPC_SIP_IPV4, NPC_LID_LC, NPC_LT_LC_IP, 12, 4);
	NPC_SCAN_HDR(NPC_DIP_IPV4, NPC_LID_LC, NPC_LT_LC_IP, 16, 4);
	NPC_SCAN_HDR(NPC_SIP_IPV6, NPC_LID_LC, NPC_LT_LC_IP6, 8, 16);
	NPC_SCAN_HDR(NPC_DIP_IPV6, NPC_LID_LC, NPC_LT_LC_IP6, 24, 16);
	NPC_SCAN_HDR(NPC_SPORT_UDP, NPC_LID_LD, NPC_LT_LD_UDP, 0, 2);
	NPC_SCAN_HDR(NPC_DPORT_UDP, NPC_LID_LD, NPC_LT_LD_UDP, 2, 2);
	NPC_SCAN_HDR(NPC_SPORT_TCP, NPC_LID_LD, NPC_LT_LD_TCP, 0, 2);
	NPC_SCAN_HDR(NPC_DPORT_TCP, NPC_LID_LD, NPC_LT_LD_TCP, 2, 2);
	NPC_SCAN_HDR(NPC_SPORT_SCTP, NPC_LID_LD, NPC_LT_LD_SCTP, 0, 2);
	NPC_SCAN_HDR(NPC_DPORT_SCTP, NPC_LID_LD, NPC_LT_LD_SCTP, 2, 2);
	NPC_SCAN_HDR(NPC_ETYPE_ETHER, NPC_LID_LA, NPC_LT_LA_ETHER, 12, 2);
	NPC_SCAN_HDR(NPC_ETYPE_TAG1, NPC_LID_LB, NPC_LT_LB_CTAG, 4, 2);
	NPC_SCAN_HDR(NPC_ETYPE_TAG2, NPC_LID_LB, NPC_LT_LB_STAG_QINQ, 8, 2);
	NPC_SCAN_HDR(NPC_VLAN_TAG1, NPC_LID_LB, NPC_LT_LB_CTAG, 2, 2);
	NPC_SCAN_HDR(NPC_VLAN_TAG2, NPC_LID_LB, NPC_LT_LB_STAG_QINQ, 2, 2);
	NPC_SCAN_HDR(NPC_DMAC, NPC_LID_LA, la_ltype, la_start, 6);
	NPC_SCAN_HDR(NPC_SMAC, NPC_LID_LA, la_ltype, la_start, 6);
	/* PF_FUNC is 2 bytes at 0th byte of NPC_LT_LA_IH_NIX_ETHER */
	NPC_SCAN_HDR(NPC_PF_FUNC, NPC_LID_LA, NPC_LT_LA_IH_NIX_ETHER, 0, 2);
}

static void npc_set_features(struct rvu *rvu, int blkaddr, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u64 *features = &mcam->rx_features;
	u64 tcp_udp_sctp;
	int err, hdr;

	if (is_npc_intf_tx(intf))
		features = &mcam->tx_features;

	for (hdr = NPC_DMAC; hdr < NPC_HEADER_FIELDS_MAX; hdr++) {
		err = npc_check_field(rvu, blkaddr, hdr, intf);
		if (!err)
			*features |= BIT_ULL(hdr);
	}

	tcp_udp_sctp = BIT_ULL(NPC_SPORT_TCP) | BIT_ULL(NPC_SPORT_UDP) |
		       BIT_ULL(NPC_DPORT_TCP) | BIT_ULL(NPC_DPORT_UDP) |
		       BIT_ULL(NPC_SPORT_SCTP) | BIT_ULL(NPC_DPORT_SCTP);

	/* for tcp/udp/sctp corresponding layer type should be in the key */
	if (*features & tcp_udp_sctp)
		if (npc_check_field(rvu, blkaddr, NPC_LD, intf))
			*features &= ~tcp_udp_sctp;

	/* for vlan corresponding layer type should be in the key */
	if (*features & BIT_ULL(NPC_OUTER_VID))
		if (npc_check_field(rvu, blkaddr, NPC_LB, intf))
			*features &= ~BIT_ULL(NPC_OUTER_VID);
}

/* Scan key extraction profile and record how fields of our interest
 * fill the key structure. Also verify Channel and DMAC exists in
 * key and not overwritten by other header fields.
 */
static int npc_scan_kex(struct rvu *rvu, int blkaddr, u8 intf)
{
	struct npc_mcam *mcam = &rvu->hw->mcam;
	u8 lid, lt, ld, bitnr;
	u8 key_nibble = 0;
	u64 cfg;

	/* Scan and note how parse result is going to be in key.
	 * A bit set in PARSE_NIBBLE_ENA corresponds to a nibble from
	 * parse result in the key. The enabled nibbles from parse result
	 * will be concatenated in key.
	 */
	cfg = rvu_read64(rvu, blkaddr, NPC_AF_INTFX_KEX_CFG(intf));
	cfg &= NPC_PARSE_NIBBLE;
	for_each_set_bit(bitnr, (unsigned long *)&cfg, 31) {
		npc_scan_parse_result(mcam, bitnr, key_nibble, intf);
		key_nibble++;
	}

	/* Scan and note how layer data is going to be in key */
	for (lid = 0; lid < NPC_MAX_LID; lid++) {
		for (lt = 0; lt < NPC_MAX_LT; lt++) {
			for (ld = 0; ld < NPC_MAX_LD; ld++) {
				cfg = rvu_read64(rvu, blkaddr,
						 NPC_AF_INTFX_LIDX_LTX_LDX_CFG
						 (intf, lid, lt, ld));
				if (!FIELD_GET(NPC_LDATA_EN, cfg))
					continue;
				npc_scan_ldata(rvu, blkaddr, lid, lt, cfg,
					       intf);
			}
		}
	}

	return 0;
}

static int npc_scan_verify_kex(struct rvu *rvu, int blkaddr)
{
	int err;

	err = npc_scan_kex(rvu, blkaddr, NIX_INTF_RX);
	if (err)
		return err;

	err = npc_scan_kex(rvu, blkaddr, NIX_INTF_TX);
	if (err)
		return err;

	/* Channel is mandatory */
	if (!npc_is_field_present(rvu, NPC_CHAN, NIX_INTF_RX)) {
		dev_err(rvu->dev, "Channel not present in Key\n");
		return -EINVAL;
	}
	/* check that none of the fields overwrite channel */
	if (npc_check_overlap(rvu, blkaddr, NPC_CHAN, 0, NIX_INTF_RX)) {
		dev_err(rvu->dev, "Channel cannot be overwritten\n");
		return -EINVAL;
	}
	/* DMAC should be present in key for unicast filter to work */
	if (!npc_is_field_present(rvu, NPC_DMAC, NIX_INTF_RX)) {
		dev_err(rvu->dev, "DMAC not present in Key\n");
		return -EINVAL;
	}
	/* check that none of the fields overwrite DMAC */
	if (npc_check_overlap(rvu, blkaddr, NPC_DMAC, 0, NIX_INTF_RX)) {
		dev_err(rvu->dev, "DMAC cannot be overwritten\n");
		return -EINVAL;
	}

	npc_set_features(rvu, blkaddr, NIX_INTF_TX);
	npc_set_features(rvu, blkaddr, NIX_INTF_RX);
	npc_handle_multi_layer_fields(rvu, blkaddr, NIX_INTF_TX);
	npc_handle_multi_layer_fields(rvu, blkaddr, NIX_INTF_RX);

	return 0;
}

int npc_flow_steering_init(struct rvu *rvu, int blkaddr)
{
	return npc_scan_verify_kex(rvu, blkaddr);
}
