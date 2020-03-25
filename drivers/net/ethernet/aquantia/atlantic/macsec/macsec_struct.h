/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef _MACSEC_STRUCT_H_
#define _MACSEC_STRUCT_H_

/*! Represents the bitfields of a single row in the Egress CTL Filter
 *  table.
 */
struct aq_mss_egress_ctlf_record {
	/*! This is used to store the 48 bit value used to compare SA, DA or
	 *  halfDA+half SA value.
	 */
	u32 sa_da[2];
	/*! This is used to store the 16 bit ethertype value used for
	 *  comparison.
	 */
	u32 eth_type;
	/*! The match mask is per-nibble. 0 means don't care, i.e. every value
	 *  will match successfully. The total data is 64 bit, i.e. 16 nibbles
	 *  masks.
	 */
	u32 match_mask;
	/*! 0: No compare, i.e. This entry is not used
	 *  1: compare DA only
	 *  2: compare SA only
	 *  3: compare half DA + half SA
	 *  4: compare ether type only
	 *  5: compare DA + ethertype
	 *  6: compare SA + ethertype
	 *  7: compare DA+ range.
	 */
	u32 match_type;
	/*! 0: Bypass the remaining modules if matched.
	 *  1: Forward to next module for more classifications.
	 */
	u32 action;
};

/*! Represents the bitfields of a single row in the Egress Packet
 *  Classifier table.
 */
struct aq_mss_egress_class_record {
	/*! VLAN ID field. */
	u32 vlan_id;
	/*! VLAN UP field. */
	u32 vlan_up;
	/*! VLAN Present in the Packet. */
	u32 vlan_valid;
	/*! The 8 bit value used to compare with extracted value for byte 3. */
	u32 byte3;
	/*! The 8 bit value used to compare with extracted value for byte 2. */
	u32 byte2;
	/*! The 8 bit value used to compare with extracted value for byte 1. */
	u32 byte1;
	/*! The 8 bit value used to compare with extracted value for byte 0. */
	u32 byte0;
	/*! The 8 bit TCI field used to compare with extracted value. */
	u32 tci;
	/*! The 64 bit SCI field in the SecTAG. */
	u32 sci[2];
	/*! The 16 bit Ethertype (in the clear) field used to compare with
	 *  extracted value.
	 */
	u32 eth_type;
	/*! This is to specify the 40bit SNAP header if the SNAP header's mask
	 *  is enabled.
	 */
	u32 snap[2];
	/*! This is to specify the 24bit LLC header if the LLC header's mask is
	 *  enabled.
	 */
	u32 llc;
	/*! The 48 bit MAC_SA field used to compare with extracted value. */
	u32 mac_sa[2];
	/*! The 48 bit MAC_DA field used to compare with extracted value. */
	u32 mac_da[2];
	/*! The 32 bit Packet number used to compare with extracted value. */
	u32 pn;
	/*! 0~63: byte location used extracted by packets comparator, which
	 *  can be anything from the first 64 bytes of the MAC packets.
	 *  This byte location counted from MAC' DA address. i.e. set to 0
	 *  will point to byte 0 of DA address.
	 */
	u32 byte3_location;
	/*! 0: don't care
	 *  1: enable comparison of extracted byte pointed by byte 3 location.
	 */
	u32 byte3_mask;
	/*! 0~63: byte location used extracted by packets comparator, which
	 *  can be anything from the first 64 bytes of the MAC packets.
	 *  This byte location counted from MAC' DA address. i.e. set to 0
	 *  will point to byte 0 of DA address.
	 */
	u32 byte2_location;
	/*! 0: don't care
	 *  1: enable comparison of extracted byte pointed by byte 2 location.
	 */
	u32 byte2_mask;
	/*! 0~63: byte location used extracted by packets comparator, which
	 *  can be anything from the first 64 bytes of the MAC packets.
	 *  This byte location counted from MAC' DA address. i.e. set to 0
	 *  will point to byte 0 of DA address.
	 */
	u32 byte1_location;
	/*! 0: don't care
	 *  1: enable comparison of extracted byte pointed by byte 1 location.
	 */
	u32 byte1_mask;
	/*! 0~63: byte location used extracted by packets comparator, which
	 *  can be anything from the first 64 bytes of the MAC packets.
	 *  This byte location counted from MAC' DA address. i.e. set to 0
	 *  will point to byte 0 of DA address.
	 */
	u32 byte0_location;
	/*! 0: don't care
	 *  1: enable comparison of extracted byte pointed by byte 0 location.
	 */
	u32 byte0_mask;
	/*! Mask is per-byte.
	 *  0: don't care
	 *  1: enable comparison of extracted VLAN ID field.
	 */
	u32 vlan_id_mask;
	/*! 0: don't care
	 *  1: enable comparison of extracted VLAN UP field.
	 */
	u32 vlan_up_mask;
	/*! 0: don't care
	 *  1: enable comparison of extracted VLAN Valid field.
	 */
	u32 vlan_valid_mask;
	/*! This is bit mask to enable comparison the 8 bit TCI field,
	 *  including the AN field.
	 *  For explicit SECTAG, AN is hardware controlled. For sending
	 *  packet w/ explicit SECTAG, rest of the TCI fields are directly
	 *  from the SECTAG.
	 */
	u32 tci_mask;
	/*! Mask is per-byte.
	 *  0: don't care
	 *  1: enable comparison of SCI
	 *  Note: If this field is not 0, this means the input packet's
	 *  SECTAG is explicitly tagged and MACSEC module will only update
	 *  the MSDU.
	 *  PN number is hardware controlled.
	 */
	u32 sci_mask;
	/*! Mask is per-byte.
	 *  0: don't care
	 *  1: enable comparison of Ethertype.
	 */
	u32 eth_type_mask;
	/*! Mask is per-byte.
	 *  0: don't care and no SNAP header exist.
	 *  1: compare the SNAP header.
	 *  If this bit is set to 1, the extracted filed will assume the
	 *  SNAP header exist as encapsulated in 802.3 (RFC 1042). I.E. the
	 *  next 5 bytes after the the LLC header is SNAP header.
	 */
	u32 snap_mask;
	/*! 0: don't care and no LLC header exist.
	 *  1: compare the LLC header.
	 *  If this bit is set to 1, the extracted filed will assume the
	 *  LLC header exist as encapsulated in 802.3 (RFC 1042). I.E. the
	 *  next three bytes after the 802.3MAC header is LLC header.
	 */
	u32 llc_mask;
	/*! Mask is per-byte.
	 *  0: don't care
	 *  1: enable comparison of MAC_SA.
	 */
	u32 sa_mask;
	/*! Mask is per-byte.
	 *  0: don't care
	 *  1: enable comparison of MAC_DA.
	 */
	u32 da_mask;
	/*! Mask is per-byte. */
	u32 pn_mask;
	/*! Reserved. This bit should be always 0. */
	u32 eight02dot2;
	/*! 1: For explicit sectag case use TCI_SC from table
	 *  0: use TCI_SC from explicit sectag.
	 */
	u32 tci_sc;
	/*! 1: For explicit sectag case,use TCI_V,ES,SCB,E,C from table
	 *  0: use TCI_V,ES,SCB,E,C from explicit sectag.
	 */
	u32 tci_87543;
	/*! 1: indicates that incoming packet has explicit sectag. */
	u32 exp_sectag_en;
	/*! If packet matches and tagged as controlled-packet, this SC/SA
	 *  index is used for later SC and SA table lookup.
	 */
	u32 sc_idx;
	/*! This field is used to specify how many SA entries are
	 *  associated with 1 SC entry.
	 *  2'b00: 1 SC has 4 SA.
	 *  SC index is equivalent to {SC_Index[4:2], 1'b0}.
	 *  SA index is equivalent to {SC_Index[4:2], SC entry's current AN[1:0]
	 *  2'b10: 1 SC has 2 SA.
	 *  SC index is equivalent to SC_Index[4:1]
	 *  SA index is equivalent to {SC_Index[4:1], SC entry's current AN[0]}
	 *  2'b11: 1 SC has 1 SA. No SC entry exists for the specific SA.
	 *  SA index is equivalent to SC_Index[4:0]
	 *  Note: if specified as 2'b11, hardware AN roll over is not
	 *  supported.
	 */
	u32 sc_sa;
	/*! 0: the packets will be sent to MAC FIFO
	 *  1: The packets will be sent to Debug/Loopback FIFO.
	 *  If the above's action is drop, this bit has no meaning.
	 */
	u32 debug;
	/*! 0: forward to remaining modules
	 *  1: bypass the next encryption modules. This packet is considered
	 *     un-control packet.
	 *  2: drop
	 *  3: Reserved.
	 */
	u32 action;
	/*! 0: Not valid entry. This entry is not used
	 *  1: valid entry.
	 */
	u32 valid;
};

/*! Represents the bitfields of a single row in the Egress SC Lookup table. */
struct aq_mss_egress_sc_record {
	/*! This is to specify when the SC was first used. Set by HW. */
	u32 start_time;
	/*! This is to specify when the SC was last used. Set by HW. */
	u32 stop_time;
	/*! This is to specify which of the SA entries are used by current HW.
	 *  Note: This value need to be set by SW after reset.  It will be
	 *  automatically updated by HW, if AN roll over is enabled.
	 */
	u32 curr_an;
	/*! 0: Clear the SA Valid Bit after PN expiry.
	 *  1: Do not Clear the SA Valid bit after PN expiry of the current SA.
	 *  When the Enable AN roll over is set, S/W does not need to
	 *  program the new SA's and the H/W will automatically roll over
	 *  between the SA's without session expiry.
	 *  For normal operation, Enable AN Roll over will be set to '0'
	 *  and in which case, the SW needs to program the new SA values
	 *  after the current PN expires.
	 */
	u32 an_roll;
	/*! This is the TCI field used if packet is not explicitly tagged. */
	u32 tci;
	/*! This value indicates the offset where the decryption will start.
	 *  [[Values of 0, 4, 8-50].
	 */
	u32 enc_off;
	/*! 0: Do not protect frames, all the packets will be forwarded
	 *     unchanged. MIB counter (OutPktsUntagged) will be updated.
	 *  1: Protect.
	 */
	u32 protect;
	/*! 0: when none of the SA related to SC has inUse set.
	 *  1: when either of the SA related to the SC has inUse set.
	 *  This bit is set by HW.
	 */
	u32 recv;
	/*! 0: H/W Clears this bit on the first use.
	 *  1: SW updates this entry, when programming the SC Table.
	 */
	u32 fresh;
	/*! AES Key size
	 *  00 - 128bits
	 *  01 - 192bits
	 *  10 - 256bits
	 *  11 - Reserved.
	 */
	u32 sak_len;
	/*! 0: Invalid SC
	 *  1: Valid SC.
	 */
	u32 valid;
};

/*! Represents the bitfields of a single row in the Egress SA Lookup table. */
struct aq_mss_egress_sa_record {
	/*! This is to specify when the SC was first used. Set by HW. */
	u32 start_time;
	/*! This is to specify when the SC was last used. Set by HW. */
	u32 stop_time;
	/*! This is set by SW and updated by HW to store the Next PN number
	 *  used for encryption.
	 */
	u32 next_pn;
	/*! The Next_PN number is going to wrapped around from 0xFFFF_FFFF
	 *  to 0. set by HW.
	 */
	u32 sat_pn;
	/*! 0: This SA is in use.
	 *  1: This SA is Fresh and set by SW.
	 */
	u32 fresh;
	/*! 0: Invalid SA
	 *  1: Valid SA.
	 */
	u32 valid;
};

/*! Represents the bitfields of a single row in the Egress SA Key
 *  Lookup table.
 */
struct aq_mss_egress_sakey_record {
	/*! Key for AES-GCM processing. */
	u32 key[8];
};

#endif
