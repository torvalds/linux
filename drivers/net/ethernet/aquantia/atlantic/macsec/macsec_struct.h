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

/*! Represents the bitfields of a single row in the Ingress Pre-MACSec
 *  CTL Filter table.
 */
struct aq_mss_ingress_prectlf_record {
	/*! This is used to store the 48 bit value used to compare SA, DA
	 *  or halfDA+half SA value.
	 */
	u32 sa_da[2];
	/*! This is used to store the 16 bit ethertype value used for
	 *  comparison.
	 */
	u32 eth_type;
	/*! The match mask is per-nibble. 0 means don't care, i.e. every
	 *  value will match successfully. The total data is 64 bit, i.e.
	 *  16 nibbles masks.
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

/*! Represents the bitfields of a single row in the Ingress Pre-MACSec
 *  Packet Classifier table.
 */
struct aq_mss_ingress_preclass_record {
	/*! The 64 bit SCI field used to compare with extracted value.
	 *  Should have SCI value in case TCI[SCI_SEND] == 0. This will be
	 *  used for ICV calculation.
	 */
	u32 sci[2];
	/*! The 8 bit TCI field used to compare with extracted value. */
	u32 tci;
	/*! 8 bit encryption offset. */
	u32 encr_offset;
	/*! The 16 bit Ethertype (in the clear) field used to compare with
	 *  extracted value.
	 */
	u32 eth_type;
	/*! This is to specify the 40bit SNAP header if the SNAP header's
	 *  mask is enabled.
	 */
	u32 snap[2];
	/*! This is to specify the 24bit LLC header if the LLC header's
	 *  mask is enabled.
	 */
	u32 llc;
	/*! The 48 bit MAC_SA field used to compare with extracted value. */
	u32 mac_sa[2];
	/*! The 48 bit MAC_DA field used to compare with extracted value. */
	u32 mac_da[2];
	/*! 0: this is to compare with non-LPBK packet
	 *  1: this is to compare with LPBK packet.
	 *  This value is used to compare with a controlled-tag which goes
	 *  with the packet when looped back from Egress port.
	 */
	u32 lpbk_packet;
	/*! The value of this bit mask will affects how the SC index and SA
	 *  index created.
	 *  2'b00: 1 SC has 4 SA.
	 *    SC index is equivalent to {SC_Index[4:2], 1'b0}.
	 *    SA index is equivalent to {SC_Index[4:2], SECTAG's AN[1:0]}
	 *    Here AN bits are not compared.
	 *  2'b10: 1 SC has 2 SA.
	 *    SC index is equivalent to SC_Index[4:1]
	 *    SA index is equivalent to {SC_Index[4:1], SECTAG's AN[0]}
	 *    Compare AN[1] field only
	 *  2'b11: 1 SC has 1 SA. No SC entry exists for the specific SA.
	 *    SA index is equivalent to SC_Index[4:0]
	 *    AN[1:0] bits are compared.
	 *    NOTE: This design is to supports different usage of AN. User
	 *    can either ping-pong buffer 2 SA by using only the AN[0] bit.
	 *    Or use 4 SA per SC by use AN[1:0] bits. Or even treat each SA
	 *    as independent. i.e. AN[1:0] is just another matching pointer
	 *    to select SA.
	 */
	u32 an_mask;
	/*! This is bit mask to enable comparison the upper 6 bits TCI
	 *  field, which does not include the AN field.
	 *  0: don't compare
	 *  1: enable comparison of the bits.
	 */
	u32 tci_mask;
	/*! 0: don't care
	 *  1: enable comparison of SCI.
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
	/*! Mask is per-byte.
	 *  0: don't care and no LLC header exist.
	 *  1: compare the LLC header.
	 *  If this bit is set to 1, the extracted filed will assume the
	 *  LLC header exist as encapsulated in 802.3 (RFC 1042). I.E. the
	 *  next three bytes after the 802.3MAC header is LLC header.
	 */
	u32 llc_mask;
	/*! Reserved. This bit should be always 0. */
	u32 _802_2_encapsulate;
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
	/*! 0: don't care
	 *  1: enable checking if this is loopback packet or not.
	 */
	u32 lpbk_mask;
	/*! If packet matches and tagged as controlled-packet. This SC/SA
	 *  index is used for later SC and SA table lookup.
	 */
	u32 sc_idx;
	/*! 0: the packets will be sent to MAC FIFO
	 *  1: The packets will be sent to Debug/Loopback FIFO.
	 *  If the above's action is drop. This bit has no meaning.
	 */
	u32 proc_dest;
	/*! 0: Process: Forward to next two modules for 802.1AE decryption.
	 *  1: Process but keep SECTAG: Forward to next two modules for
	 *     802.1AE decryption but keep the MACSEC header with added error
	 *     code information. ICV will be stripped for all control packets.
	 *  2: Bypass: Bypass the next two decryption modules but processed
	 *     by post-classification.
	 *  3: Drop: drop this packet and update counts accordingly.
	 */
	u32 action;
	/*! 0: This is a controlled-port packet if matched.
	 *  1: This is an uncontrolled-port packet if matched.
	 */
	u32 ctrl_unctrl;
	/*! Use the SCI value from the Table if 'SC' bit of the input
	 *  packet is not present.
	 */
	u32 sci_from_table;
	/*! Reserved. */
	u32 reserved;
	/*! 0: Not valid entry. This entry is not used
	 *  1: valid entry.
	 */
	u32 valid;
};

/*! Represents the bitfields of a single row in the Ingress SC Lookup table. */
struct aq_mss_ingress_sc_record {
	/*! This is to specify when the SC was first used. Set by HW. */
	u32 stop_time;
	/*! This is to specify when the SC was first used. Set by HW. */
	u32 start_time;
	/*! 0: Strict
	 *  1: Check
	 *  2: Disabled.
	 */
	u32 validate_frames;
	/*! 1: Replay control enabled.
	 *  0: replay control disabled.
	 */
	u32 replay_protect;
	/*! This is to specify the window range for anti-replay. Default is 0.
	 *  0: is strict order enforcement.
	 */
	u32 anti_replay_window;
	/*! 0: when none of the SA related to SC has inUse set.
	 *  1: when either of the SA related to the SC has inUse set.
	 *  This bit is set by HW.
	 */
	u32 receiving;
	/*! 0: when hardware processed the SC for the first time, it clears
	 *     this bit
	 *  1: This bit is set by SW, when it sets up the SC.
	 */
	u32 fresh;
	/*! 0: The AN number will not automatically roll over if Next_PN is
	 *     saturated.
	 *  1: The AN number will automatically roll over if Next_PN is
	 *     saturated.
	 *  Rollover is valid only after expiry. Normal roll over between
	 *  SA's should be normal process.
	 */
	u32 an_rol;
	/*! Reserved. */
	u32 reserved;
	/*! 0: Invalid SC
	 *  1: Valid SC.
	 */
	u32 valid;
};

/*! Represents the bitfields of a single row in the Ingress SA Lookup table. */
struct aq_mss_ingress_sa_record {
	/*! This is to specify when the SC was first used. Set by HW. */
	u32 stop_time;
	/*! This is to specify when the SC was first used. Set by HW. */
	u32 start_time;
	/*! This is updated by HW to store the expected NextPN number for
	 *  anti-replay.
	 */
	u32 next_pn;
	/*! The Next_PN number is going to wrapped around from 0XFFFF_FFFF
	 *  to 0. set by HW.
	 */
	u32 sat_nextpn;
	/*! 0: This SA is not yet used.
	 *  1: This SA is inUse.
	 */
	u32 in_use;
	/*! 0: when hardware processed the SC for the first time, it clears
	 *     this timer
	 *  1: This bit is set by SW, when it sets up the SC.
	 */
	u32 fresh;
	/*! Reserved. */
	u32 reserved;
	/*! 0: Invalid SA.
	 *  1: Valid SA.
	 */
	u32 valid;
};

/*! Represents the bitfields of a single row in the Ingress SA Key
 *  Lookup table.
 */
struct aq_mss_ingress_sakey_record {
	/*! Key for AES-GCM processing. */
	u32 key[8];
	/*! AES key size
	 *  00 - 128bits
	 *  01 - 192bits
	 *  10 - 256bits
	 *  11 - reserved.
	 */
	u32 key_len;
};

/*! Represents the bitfields of a single row in the Ingress Post-
 *  MACSec Packet Classifier table.
 */
struct aq_mss_ingress_postclass_record {
	/*! The 8 bit value used to compare with extracted value for byte 0. */
	u32 byte0;
	/*! The 8 bit value used to compare with extracted value for byte 1. */
	u32 byte1;
	/*! The 8 bit value used to compare with extracted value for byte 2. */
	u32 byte2;
	/*! The 8 bit value used to compare with extracted value for byte 3. */
	u32 byte3;
	/*! Ethertype in the packet. */
	u32 eth_type;
	/*! Ether Type value > 1500 (0x5dc). */
	u32 eth_type_valid;
	/*! VLAN ID after parsing. */
	u32 vlan_id;
	/*! VLAN priority after parsing. */
	u32 vlan_up;
	/*! Valid VLAN coding. */
	u32 vlan_valid;
	/*! SA index. */
	u32 sai;
	/*! SAI hit, i.e. controlled packet. */
	u32 sai_hit;
	/*! Mask for payload ethertype field. */
	u32 eth_type_mask;
	/*! 0~63: byte location used extracted by packets comparator, which
	 *  can be anything from the first 64 bytes of the MAC packets.
	 *  This byte location counted from MAC' DA address. i.e. set to 0
	 *  will point to byte 0 of DA address.
	 */
	u32 byte3_location;
	/*! Mask for Byte Offset 3. */
	u32 byte3_mask;
	/*! 0~63: byte location used extracted by packets comparator, which
	 *  can be anything from the first 64 bytes of the MAC packets.
	 *  This byte location counted from MAC' DA address. i.e. set to 0
	 *  will point to byte 0 of DA address.
	 */
	u32 byte2_location;
	/*! Mask for Byte Offset 2. */
	u32 byte2_mask;
	/*! 0~63: byte location used extracted by packets comparator, which
	 *  can be anything from the first 64 bytes of the MAC packets.
	 *  This byte location counted from MAC' DA address. i.e. set to 0
	 *  will point to byte 0 of DA address.
	 */
	u32 byte1_location;
	/*! Mask for Byte Offset 1. */
	u32 byte1_mask;
	/*! 0~63: byte location used extracted by packets comparator, which
	 *  can be anything from the first 64 bytes of the MAC packets.
	 *  This byte location counted from MAC' DA address. i.e. set to 0
	 *  will point to byte 0 of DA address.
	 */
	u32 byte0_location;
	/*! Mask for Byte Offset 0. */
	u32 byte0_mask;
	/*! Mask for Ethertype valid field. Indicates 802.3 vs. Other. */
	u32 eth_type_valid_mask;
	/*! Mask for VLAN ID field. */
	u32 vlan_id_mask;
	/*! Mask for VLAN UP field. */
	u32 vlan_up_mask;
	/*! Mask for VLAN valid field. */
	u32 vlan_valid_mask;
	/*! Mask for SAI. */
	u32 sai_mask;
	/*! Mask for SAI_HIT. */
	u32 sai_hit_mask;
	/*! Action if only first level matches and second level does not.
	 *  0: pass
	 *  1: drop (fail).
	 */
	u32 firstlevel_actions;
	/*! Action if both first and second level matched.
	 *  0: pass
	 *  1: drop (fail).
	 */
	u32 secondlevel_actions;
	/*! Reserved. */
	u32 reserved;
	/*! 0: Not valid entry. This entry is not used
	 *  1: valid entry.
	 */
	u32 valid;
};

/*! Represents the bitfields of a single row in the Ingress Post-
 *  MACSec CTL Filter table.
 */
struct aq_mss_ingress_postctlf_record {
	/*! This is used to store the 48 bit value used to compare SA, DA
	 *  or halfDA+half SA value.
	 */
	u32 sa_da[2];
	/*! This is used to store the 16 bit ethertype value used for
	 *  comparison.
	 */
	u32 eth_type;
	/*! The match mask is per-nibble. 0 means don't care, i.e. every
	 *  value will match successfully. The total data is 64 bit, i.e.
	 *  16 nibbles masks.
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

/*! Represents the Egress MIB counters for a single SC. Counters are
 *  64 bits, lower 32 bits in field[0].
 */
struct aq_mss_egress_sc_counters {
	/*! The number of integrity protected but not encrypted packets
	 *  for this transmitting SC.
	 */
	u32 sc_protected_pkts[2];
	/*! The number of integrity protected and encrypted packets for
	 *  this transmitting SC.
	 */
	u32 sc_encrypted_pkts[2];
	/*! The number of plain text octets that are integrity protected
	 *  but not encrypted on the transmitting SC.
	 */
	u32 sc_protected_octets[2];
	/*! The number of plain text octets that are integrity protected
	 *  and encrypted on the transmitting SC.
	 */
	u32 sc_encrypted_octets[2];
};

/*! Represents the Egress MIB counters for a single SA. Counters are
 *  64 bits, lower 32 bits in field[0].
 */
struct aq_mss_egress_sa_counters {
	/*! The number of dropped packets for this transmitting SA. */
	u32 sa_hit_drop_redirect[2];
	/*! TODO */
	u32 sa_protected2_pkts[2];
	/*! The number of integrity protected but not encrypted packets
	 *  for this transmitting SA.
	 */
	u32 sa_protected_pkts[2];
	/*! The number of integrity protected and encrypted packets for
	 *  this transmitting SA.
	 */
	u32 sa_encrypted_pkts[2];
};

/*! Represents the common Egress MIB counters; the counter not
 *  associated with a particular SC/SA. Counters are 64 bits, lower 32
 *  bits in field[0].
 */
struct aq_mss_egress_common_counters {
	/*! The number of transmitted packets classified as MAC_CTL packets. */
	u32 ctl_pkt[2];
	/*! The number of transmitted packets that did not match any rows
	 *  in the Egress Packet Classifier table.
	 */
	u32 unknown_sa_pkts[2];
	/*! The number of transmitted packets where the SC table entry has
	 *  protect=0 (so packets are forwarded unchanged).
	 */
	u32 untagged_pkts[2];
	/*! The number of transmitted packets discarded because the packet
	 *  length is greater than the ifMtu of the Common Port interface.
	 */
	u32 too_long[2];
	/*! The number of transmitted packets for which table memory was
	 *  affected by an ECC error during processing.
	 */
	u32 ecc_error_pkts[2];
	/*! The number of transmitted packets for where the matched row in
	 *  the Egress Packet Classifier table has action=drop.
	 */
	u32 unctrl_hit_drop_redir[2];
};

/*! Represents the Ingress MIB counters for a single SA. Counters are
 *  64 bits, lower 32 bits in field[0].
 */
struct aq_mss_ingress_sa_counters {
	/*! For this SA, the number of received packets without a SecTAG. */
	u32 untagged_hit_pkts[2];
	/*! For this SA, the number of received packets that were dropped. */
	u32 ctrl_hit_drop_redir_pkts[2];
	/*! For this SA which is not currently in use, the number of
	 *  received packets that have been discarded, and have either the
	 *  packets encrypted or the matched row in the Ingress SC Lookup
	 *  table has validate_frames=Strict.
	 */
	u32 not_using_sa[2];
	/*! For this SA which is not currently in use, the number of
	 *  received, unencrypted, packets with the matched row in the
	 *  Ingress SC Lookup table has validate_frames!=Strict.
	 */
	u32 unused_sa[2];
	/*! For this SA, the number discarded packets with the condition
	 *  that the packets are not valid and one of the following
	 *  conditions are true: either the matched row in the Ingress SC
	 *  Lookup table has validate_frames=Strict or the packets
	 *  encrypted.
	 */
	u32 not_valid_pkts[2];
	/*! For this SA, the number of packets with the condition that the
	 *  packets are not valid and the matched row in the Ingress SC
	 *  Lookup table has validate_frames=Check.
	 */
	u32 invalid_pkts[2];
	/*! For this SA, the number of validated packets. */
	u32 ok_pkts[2];
	/*! For this SC, the number of received packets that have been
	 *  discarded with the condition: the matched row in the Ingress
	 *  SC Lookup table has replay_protect=1 and the PN of the packet
	 *  is lower than the lower bound replay check PN.
	 */
	u32 late_pkts[2];
	/*! For this SA, the number of packets with the condition that the
	 *  PN of the packets is lower than the lower bound replay
	 *  protection PN.
	 */
	u32 delayed_pkts[2];
	/*! For this SC, the number of packets with the following condition:
	 *  - the matched row in the Ingress SC Lookup table has
	 *    replay_protect=0 or
	 *  - the matched row in the Ingress SC Lookup table has
	 *    replay_protect=1 and the packet is not encrypted and the
	 *    integrity check has failed or
	 *  - the matched row in the Ingress SC Lookup table has
	 *    replay_protect=1 and the packet is encrypted and integrity
	 *    check has failed.
	 */
	u32 unchecked_pkts[2];
	/*! The number of octets of plaintext recovered from received
	 *  packets that were integrity protected but not encrypted.
	 */
	u32 validated_octets[2];
	/*! The number of octets of plaintext recovered from received
	 *  packets that were integrity protected and encrypted.
	 */
	u32 decrypted_octets[2];
};

/*! Represents the common Ingress MIB counters; the counter not
 *  associated with a particular SA. Counters are 64 bits, lower 32
 *  bits in field[0].
 */
struct aq_mss_ingress_common_counters {
	/*! The number of received packets classified as MAC_CTL packets. */
	u32 ctl_pkts[2];
	/*! The number of received packets with the MAC security tag
	 *  (SecTAG), not matching any rows in the Ingress Pre-MACSec
	 *  Packet Classifier table.
	 */
	u32 tagged_miss_pkts[2];
	/*! The number of received packets without the MAC security tag
	 *  (SecTAG), not matching any rows in the Ingress Pre-MACSec
	 *  Packet Classifier table.
	 */
	u32 untagged_miss_pkts[2];
	/*! The number of received packets discarded without the MAC
	 *  security tag (SecTAG) and with the matched row in the Ingress
	 *  SC Lookup table having validate_frames=Strict.
	 */
	u32 notag_pkts[2];
	/*! The number of received packets without the MAC security tag
	 *  (SecTAG) and with the matched row in the Ingress SC Lookup
	 *  table having validate_frames!=Strict.
	 */
	u32 untagged_pkts[2];
	/*! The number of received packets discarded with an invalid
	 *  SecTAG or a zero value PN or an invalid ICV.
	 */
	u32 bad_tag_pkts[2];
	/*! The number of received packets discarded with unknown SCI
	 *  information with the condition:
	 *  the matched row in the Ingress SC Lookup table has
	 *  validate_frames=Strict or the C bit in the SecTAG is set.
	 */
	u32 no_sci_pkts[2];
	/*! The number of received packets with unknown SCI with the condition:
	 *  The matched row in the Ingress SC Lookup table has
	 *  validate_frames!=Strict and the C bit in the SecTAG is not set.
	 */
	u32 unknown_sci_pkts[2];
	/*! The number of received packets by the controlled port service
	 *  that passed the Ingress Post-MACSec Packet Classifier table
	 *  check.
	 */
	u32 ctrl_prt_pass_pkts[2];
	/*! The number of received packets by the uncontrolled port
	 *  service that passed the Ingress Post-MACSec Packet Classifier
	 *  table check.
	 */
	u32 unctrl_prt_pass_pkts[2];
	/*! The number of received packets by the controlled port service
	 *  that failed the Ingress Post-MACSec Packet Classifier table
	 *  check.
	 */
	u32 ctrl_prt_fail_pkts[2];
	/*! The number of received packets by the uncontrolled port
	 *  service that failed the Ingress Post-MACSec Packet Classifier
	 *  table check.
	 */
	u32 unctrl_prt_fail_pkts[2];
	/*! The number of received packets discarded because the packet
	 *  length is greater than the ifMtu of the Common Port interface.
	 */
	u32 too_long_pkts[2];
	/*! The number of received packets classified as MAC_CTL by the
	 *  Ingress Post-MACSec CTL Filter table.
	 */
	u32 igpoc_ctl_pkts[2];
	/*! The number of received packets for which table memory was
	 *  affected by an ECC error during processing.
	 */
	u32 ecc_error_pkts[2];
	/*! The number of received packets by the uncontrolled port
	 *  service that were dropped.
	 */
	u32 unctrl_hit_drop_redir[2];
};

#endif
