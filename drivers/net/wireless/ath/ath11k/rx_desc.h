/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2019 The Linux Foundation. All rights reserved.
 */
#ifndef ATH11K_RX_DESC_H
#define ATH11K_RX_DESC_H

enum rx_desc_rxpcu_filter {
	RX_DESC_RXPCU_FILTER_PASS,
	RX_DESC_RXPCU_FILTER_MONITOR_CLIENT,
	RX_DESC_RXPCU_FILTER_MONITOR_OTHER,
};

/* rxpcu_filter_pass
 *		This MPDU passed the normal frame filter programming of rxpcu.
 *
 * rxpcu_filter_monitor_client
 *		 This MPDU did not pass the regular frame filter and would
 *		 have been dropped, were it not for the frame fitting into the
 *		 'monitor_client' category.
 *
 * rxpcu_filter_monitor_other
 *		This MPDU did not pass the regular frame filter and also did
 *		not pass the rxpcu_monitor_client filter. It would have been
 *		dropped accept that it did pass the 'monitor_other' category.
 */

#define RX_DESC_INFO0_RXPCU_MPDU_FITLER	GENMASK(1, 0)
#define RX_DESC_INFO0_SW_FRAME_GRP_ID	GENMASK(8, 2)

enum rx_desc_sw_frame_grp_id {
	RX_DESC_SW_FRAME_GRP_ID_NDP_FRAME,
	RX_DESC_SW_FRAME_GRP_ID_MCAST_DATA,
	RX_DESC_SW_FRAME_GRP_ID_UCAST_DATA,
	RX_DESC_SW_FRAME_GRP_ID_NULL_DATA,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_0000,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_0001,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_0010,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_0011,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_0100,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_0101,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_0110,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_0111,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_1000,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_1001,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_1010,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_1011,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_1100,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_1101,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_1110,
	RX_DESC_SW_FRAME_GRP_ID_MGMT_1111,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_0000,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_0001,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_0010,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_0011,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_0100,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_0101,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_0110,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_0111,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_1000,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_1001,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_1010,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_1011,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_1100,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_1101,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_1110,
	RX_DESC_SW_FRAME_GRP_ID_CTRL_1111,
	RX_DESC_SW_FRAME_GRP_ID_UNSUPPORTED,
	RX_DESC_SW_FRAME_GRP_ID_PHY_ERR,
};

enum rx_desc_decap_type {
	RX_DESC_DECAP_TYPE_RAW,
	RX_DESC_DECAP_TYPE_NATIVE_WIFI,
	RX_DESC_DECAP_TYPE_ETHERNET2_DIX,
	RX_DESC_DECAP_TYPE_8023,
};

enum rx_desc_decrypt_status_code {
	RX_DESC_DECRYPT_STATUS_CODE_OK,
	RX_DESC_DECRYPT_STATUS_CODE_UNPROTECTED_FRAME,
	RX_DESC_DECRYPT_STATUS_CODE_DATA_ERR,
	RX_DESC_DECRYPT_STATUS_CODE_KEY_INVALID,
	RX_DESC_DECRYPT_STATUS_CODE_PEER_ENTRY_INVALID,
	RX_DESC_DECRYPT_STATUS_CODE_OTHER,
};

#define RX_ATTENTION_INFO1_FIRST_MPDU		BIT(0)
#define RX_ATTENTION_INFO1_RSVD_1A		BIT(1)
#define RX_ATTENTION_INFO1_MCAST_BCAST		BIT(2)
#define RX_ATTENTION_INFO1_AST_IDX_NOT_FOUND	BIT(3)
#define RX_ATTENTION_INFO1_AST_IDX_TIMEDOUT	BIT(4)
#define RX_ATTENTION_INFO1_POWER_MGMT		BIT(5)
#define RX_ATTENTION_INFO1_NON_QOS		BIT(6)
#define RX_ATTENTION_INFO1_NULL_DATA		BIT(7)
#define RX_ATTENTION_INFO1_MGMT_TYPE		BIT(8)
#define RX_ATTENTION_INFO1_CTRL_TYPE		BIT(9)
#define RX_ATTENTION_INFO1_MORE_DATA		BIT(10)
#define RX_ATTENTION_INFO1_EOSP			BIT(11)
#define RX_ATTENTION_INFO1_A_MSDU_ERROR		BIT(12)
#define RX_ATTENTION_INFO1_FRAGMENT		BIT(13)
#define RX_ATTENTION_INFO1_ORDER		BIT(14)
#define RX_ATTENTION_INFO1_CCE_MATCH		BIT(15)
#define RX_ATTENTION_INFO1_OVERFLOW_ERR		BIT(16)
#define RX_ATTENTION_INFO1_MSDU_LEN_ERR		BIT(17)
#define RX_ATTENTION_INFO1_TCP_UDP_CKSUM_FAIL	BIT(18)
#define RX_ATTENTION_INFO1_IP_CKSUM_FAIL	BIT(19)
#define RX_ATTENTION_INFO1_SA_IDX_INVALID	BIT(20)
#define RX_ATTENTION_INFO1_DA_IDX_INVALID	BIT(21)
#define RX_ATTENTION_INFO1_RSVD_1B		BIT(22)
#define RX_ATTENTION_INFO1_RX_IN_TX_DECRYPT_BYP	BIT(23)
#define RX_ATTENTION_INFO1_ENCRYPT_REQUIRED	BIT(24)
#define RX_ATTENTION_INFO1_DIRECTED		BIT(25)
#define RX_ATTENTION_INFO1_BUFFER_FRAGMENT	BIT(26)
#define RX_ATTENTION_INFO1_MPDU_LEN_ERR		BIT(27)
#define RX_ATTENTION_INFO1_TKIP_MIC_ERR		BIT(28)
#define RX_ATTENTION_INFO1_DECRYPT_ERR		BIT(29)
#define RX_ATTENTION_INFO1_UNDECRYPT_FRAME_ERR	BIT(30)
#define RX_ATTENTION_INFO1_FCS_ERR		BIT(31)

#define RX_ATTENTION_INFO2_FLOW_IDX_TIMEOUT	BIT(0)
#define RX_ATTENTION_INFO2_FLOW_IDX_INVALID	BIT(1)
#define RX_ATTENTION_INFO2_WIFI_PARSER_ERR	BIT(2)
#define RX_ATTENTION_INFO2_AMSDU_PARSER_ERR	BIT(3)
#define RX_ATTENTION_INFO2_SA_IDX_TIMEOUT	BIT(4)
#define RX_ATTENTION_INFO2_DA_IDX_TIMEOUT	BIT(5)
#define RX_ATTENTION_INFO2_MSDU_LIMIT_ERR	BIT(6)
#define RX_ATTENTION_INFO2_DA_IS_VALID		BIT(7)
#define RX_ATTENTION_INFO2_DA_IS_MCBC		BIT(8)
#define RX_ATTENTION_INFO2_SA_IS_VALID		BIT(9)
#define RX_ATTENTION_INFO2_DCRYPT_STATUS_CODE	GENMASK(12, 10)
#define RX_ATTENTION_INFO2_RX_BITMAP_NOT_UPDED	BIT(13)
#define RX_ATTENTION_INFO2_MSDU_DONE		BIT(31)

struct rx_attention {
	__le16 info0;
	__le16 phy_ppdu_id;
	__le32 info1;
	__le32 info2;
} __packed;

/* rx_attention
 *
 * rxpcu_mpdu_filter_in_category
 *		Field indicates what the reason was that this mpdu frame
 *		was allowed to come into the receive path by rxpcu. Values
 *		are defined in enum %RX_DESC_RXPCU_FILTER_*.
 *
 * sw_frame_group_id
 *		SW processes frames based on certain classifications. Values
 *		are defined in enum %RX_DESC_SW_FRAME_GRP_ID_*.
 *
 * phy_ppdu_id
 *		A ppdu counter value that PHY increments for every PPDU
 *		received. The counter value wraps around.
 *
 * first_mpdu
 *		Indicates the first MSDU of the PPDU.  If both first_mpdu
 *		and last_mpdu are set in the MSDU then this is a not an
 *		A-MPDU frame but a stand alone MPDU.  Interior MPDU in an
 *		A-MPDU shall have both first_mpdu and last_mpdu bits set to
 *		0.  The PPDU start status will only be valid when this bit
 *		is set.
 *
 * mcast_bcast
 *		Multicast / broadcast indicator.  Only set when the MAC
 *		address 1 bit 0 is set indicating mcast/bcast and the BSSID
 *		matches one of the 4 BSSID registers. Only set when
 *		first_msdu is set.
 *
 * ast_index_not_found
 *		Only valid when first_msdu is set. Indicates no AST matching
 *		entries within the the max search count.
 *
 * ast_index_timeout
 *		Only valid when first_msdu is set. Indicates an unsuccessful
 *		search in the address search table due to timeout.
 *
 * power_mgmt
 *		Power management bit set in the 802.11 header.  Only set
 *		when first_msdu is set.
 *
 * non_qos
 *		Set if packet is not a non-QoS data frame.  Only set when
 *		first_msdu is set.
 *
 * null_data
 *		Set if frame type indicates either null data or QoS null
 *		data format.  Only set when first_msdu is set.
 *
 * mgmt_type
 *		Set if packet is a management packet.  Only set when
 *		first_msdu is set.
 *
 * ctrl_type
 *		Set if packet is a control packet.  Only set when first_msdu
 *		is set.
 *
 * more_data
 *		Set if more bit in frame control is set.  Only set when
 *		first_msdu is set.
 *
 * eosp
 *		Set if the EOSP (end of service period) bit in the QoS
 *		control field is set.  Only set when first_msdu is set.
 *
 * a_msdu_error
 *		Set if number of MSDUs in A-MSDU is above a threshold or if the
 *		size of the MSDU is invalid. This receive buffer will contain
 *		all of the remainder of MSDUs in this MPDU w/o decapsulation.
 *
 * fragment
 *		Indicates that this is an 802.11 fragment frame.  This is
 *		set when either the more_frag bit is set in the frame
 *		control or the fragment number is not zero.  Only set when
 *		first_msdu is set.
 *
 * order
 *		Set if the order bit in the frame control is set.  Only set
 *		when first_msdu is set.
 *
 * cce_match
 *		Indicates that this status has a corresponding MSDU that
 *		requires FW processing. The OLE will have classification
 *		ring mask registers which will indicate the ring(s) for
 *		packets and descriptors which need FW attention.
 *
 * overflow_err
 *		PCU Receive FIFO does not have enough space to store the
 *		full receive packet.  Enough space is reserved in the
 *		receive FIFO for the status is written.  This MPDU remaining
 *		packets in the PPDU will be filtered and no Ack response
 *		will be transmitted.
 *
 * msdu_length_err
 *		Indicates that the MSDU length from the 802.3 encapsulated
 *		length field extends beyond the MPDU boundary.
 *
 * tcp_udp_chksum_fail
 *		Indicates that the computed checksum (tcp_udp_chksum) did
 *		not match the checksum in the TCP/UDP header.
 *
 * ip_chksum_fail
 *		Indicates that the computed checksum did not match the
 *		checksum in the IP header.
 *
 * sa_idx_invalid
 *		Indicates no matching entry was found in the address search
 *		table for the source MAC address.
 *
 * da_idx_invalid
 *		Indicates no matching entry was found in the address search
 *		table for the destination MAC address.
 *
 * rx_in_tx_decrypt_byp
 *		Indicates that RX packet is not decrypted as Crypto is busy
 *		with TX packet processing.
 *
 * encrypt_required
 *		Indicates that this data type frame is not encrypted even if
 *		the policy for this MPDU requires encryption as indicated in
 *		the peer table key type.
 *
 * directed
 *		MPDU is a directed packet which means that the RA matched
 *		our STA addresses.  In proxySTA it means that the TA matched
 *		an entry in our address search table with the corresponding
 *		'no_ack' bit is the address search entry cleared.
 *
 * buffer_fragment
 *		Indicates that at least one of the rx buffers has been
 *		fragmented.  If set the FW should look at the rx_frag_info
 *		descriptor described below.
 *
 * mpdu_length_err
 *		Indicates that the MPDU was pre-maturely terminated
 *		resulting in a truncated MPDU.  Don't trust the MPDU length
 *		field.
 *
 * tkip_mic_err
 *		Indicates that the MPDU Michael integrity check failed
 *
 * decrypt_err
 *		Indicates that the MPDU decrypt integrity check failed
 *
 * fcs_err
 *		Indicates that the MPDU FCS check failed
 *
 * flow_idx_timeout
 *		Indicates an unsuccessful flow search due to the expiring of
 *		the search timer.
 *
 * flow_idx_invalid
 *		flow id is not valid.
 *
 * amsdu_parser_error
 *		A-MSDU could not be properly de-agregated.
 *
 * sa_idx_timeout
 *		Indicates an unsuccessful search for the source MAC address
 *		due to the expiring of the search timer.
 *
 * da_idx_timeout
 *		Indicates an unsuccessful search for the destination MAC
 *		address due to the expiring of the search timer.
 *
 * msdu_limit_error
 *		Indicates that the MSDU threshold was exceeded and thus
 *		all the rest of the MSDUs will not be scattered and will not
 *		be decasulated but will be DMA'ed in RAW format as a single
 *		MSDU buffer.
 *
 * da_is_valid
 *		Indicates that OLE found a valid DA entry.
 *
 * da_is_mcbc
 *		Field Only valid if da_is_valid is set. Indicates the DA address
 *		was a Multicast or Broadcast address.
 *
 * sa_is_valid
 *		Indicates that OLE found a valid SA entry.
 *
 * decrypt_status_code
 *		Field provides insight into the decryption performed. Values are
 *		defined in enum %RX_DESC_DECRYPT_STATUS_CODE*.
 *
 * rx_bitmap_not_updated
 *		Frame is received, but RXPCU could not update the receive bitmap
 *		due to (temporary) fifo constraints.
 *
 * msdu_done
 *		If set indicates that the RX packet data, RX header data, RX
 *		PPDU start descriptor, RX MPDU start/end descriptor, RX MSDU
 *		start/end descriptors and RX Attention descriptor are all
 *		valid.  This bit must be in the last octet of the
 *		descriptor.
 */

#define RX_MPDU_START_INFO0_NDP_FRAME		BIT(9)
#define RX_MPDU_START_INFO0_PHY_ERR		BIT(10)
#define RX_MPDU_START_INFO0_PHY_ERR_MPDU_HDR	BIT(11)
#define RX_MPDU_START_INFO0_PROTO_VER_ERR	BIT(12)
#define RX_MPDU_START_INFO0_AST_LOOKUP_VALID	BIT(13)

#define RX_MPDU_START_INFO1_MPDU_CTRL_VALID	BIT(0)
#define RX_MPDU_START_INFO1_MPDU_DUR_VALID	BIT(1)
#define RX_MPDU_START_INFO1_MAC_ADDR1_VALID	BIT(2)
#define RX_MPDU_START_INFO1_MAC_ADDR2_VALID	BIT(3)
#define RX_MPDU_START_INFO1_MAC_ADDR3_VALID	BIT(4)
#define RX_MPDU_START_INFO1_MAC_ADDR4_VALID	BIT(5)
#define RX_MPDU_START_INFO1_MPDU_SEQ_CTRL_VALID	BIT(6)
#define RX_MPDU_START_INFO1_MPDU_QOS_CTRL_VALID	BIT(7)
#define RX_MPDU_START_INFO1_MPDU_HT_CTRL_VALID	BIT(8)
#define RX_MPDU_START_INFO1_ENCRYPT_INFO_VALID	BIT(9)
#define RX_MPDU_START_INFO1_MPDU_FRAG_NUMBER	GENMASK(13, 10)
#define RX_MPDU_START_INFO1_MORE_FRAG_FLAG	BIT(14)
#define RX_MPDU_START_INFO1_FROM_DS		BIT(16)
#define RX_MPDU_START_INFO1_TO_DS		BIT(17)
#define RX_MPDU_START_INFO1_ENCRYPTED		BIT(18)
#define RX_MPDU_START_INFO1_MPDU_RETRY		BIT(19)
#define RX_MPDU_START_INFO1_MPDU_SEQ_NUM	GENMASK(31, 20)

#define RX_MPDU_START_INFO2_EPD_EN		BIT(0)
#define RX_MPDU_START_INFO2_ALL_FRAME_ENCPD	BIT(1)
#define RX_MPDU_START_INFO2_ENC_TYPE		GENMASK(5, 2)
#define RX_MPDU_START_INFO2_VAR_WEP_KEY_WIDTH	GENMASK(7, 6)
#define RX_MPDU_START_INFO2_MESH_STA		BIT(8)
#define RX_MPDU_START_INFO2_BSSID_HIT		BIT(9)
#define RX_MPDU_START_INFO2_BSSID_NUM		GENMASK(13, 10)
#define RX_MPDU_START_INFO2_TID			GENMASK(17, 14)

#define RX_MPDU_START_INFO3_REO_DEST_IND		GENMASK(4, 0)
#define RX_MPDU_START_INFO3_FLOW_ID_TOEPLITZ		BIT(7)
#define RX_MPDU_START_INFO3_PKT_SEL_FP_UCAST_DATA	BIT(8)
#define RX_MPDU_START_INFO3_PKT_SEL_FP_MCAST_DATA	BIT(9)
#define RX_MPDU_START_INFO3_PKT_SEL_FP_CTRL_BAR		BIT(10)
#define RX_MPDU_START_INFO3_RXDMA0_SRC_RING_SEL		GENMASK(12, 11)
#define RX_MPDU_START_INFO3_RXDMA0_DST_RING_SEL		GENMASK(14, 13)

#define RX_MPDU_START_INFO4_REO_QUEUE_DESC_HI	GENMASK(7, 0)
#define RX_MPDU_START_INFO4_RECV_QUEUE_NUM	GENMASK(23, 8)
#define RX_MPDU_START_INFO4_PRE_DELIM_ERR_WARN	BIT(24)
#define RX_MPDU_START_INFO4_FIRST_DELIM_ERR	BIT(25)

#define RX_MPDU_START_INFO5_KEY_ID		GENMASK(7, 0)
#define RX_MPDU_START_INFO5_NEW_PEER_ENTRY	BIT(8)
#define RX_MPDU_START_INFO5_DECRYPT_NEEDED	BIT(9)
#define RX_MPDU_START_INFO5_DECAP_TYPE		GENMASK(11, 10)
#define RX_MPDU_START_INFO5_VLAN_TAG_C_PADDING	BIT(12)
#define RX_MPDU_START_INFO5_VLAN_TAG_S_PADDING	BIT(13)
#define RX_MPDU_START_INFO5_STRIP_VLAN_TAG_C	BIT(14)
#define RX_MPDU_START_INFO5_STRIP_VLAN_TAG_S	BIT(15)
#define RX_MPDU_START_INFO5_PRE_DELIM_COUNT	GENMASK(27, 16)
#define RX_MPDU_START_INFO5_AMPDU_FLAG		BIT(28)
#define RX_MPDU_START_INFO5_BAR_FRAME		BIT(29)

#define RX_MPDU_START_INFO6_MPDU_LEN		GENMASK(13, 0)
#define RX_MPDU_START_INFO6_FIRST_MPDU		BIT(14)
#define RX_MPDU_START_INFO6_MCAST_BCAST		BIT(15)
#define RX_MPDU_START_INFO6_AST_IDX_NOT_FOUND	BIT(16)
#define RX_MPDU_START_INFO6_AST_IDX_TIMEOUT	BIT(17)
#define RX_MPDU_START_INFO6_POWER_MGMT		BIT(18)
#define RX_MPDU_START_INFO6_NON_QOS		BIT(19)
#define RX_MPDU_START_INFO6_NULL_DATA		BIT(20)
#define RX_MPDU_START_INFO6_MGMT_TYPE		BIT(21)
#define RX_MPDU_START_INFO6_CTRL_TYPE		BIT(22)
#define RX_MPDU_START_INFO6_MORE_DATA		BIT(23)
#define RX_MPDU_START_INFO6_EOSP		BIT(24)
#define RX_MPDU_START_INFO6_FRAGMENT		BIT(25)
#define RX_MPDU_START_INFO6_ORDER		BIT(26)
#define RX_MPDU_START_INFO6_UAPSD_TRIGGER	BIT(27)
#define RX_MPDU_START_INFO6_ENCRYPT_REQUIRED	BIT(28)
#define RX_MPDU_START_INFO6_DIRECTED		BIT(29)

#define RX_MPDU_START_RAW_MPDU			BIT(0)

struct rx_mpdu_start {
	__le16 info0;
	__le16 phy_ppdu_id;
	__le16 ast_index;
	__le16 sw_peer_id;
	__le32 info1;
	__le32 info2;
	__le32 pn[4];
	__le32 peer_meta_data;
	__le32 info3;
	__le32 reo_queue_desc_lo;
	__le32 info4;
	__le32 info5;
	__le32 info6;
	__le16 frame_ctrl;
	__le16 duration;
	u8 addr1[ETH_ALEN];
	u8 addr2[ETH_ALEN];
	u8 addr3[ETH_ALEN];
	__le16 seq_ctrl;
	u8 addr4[ETH_ALEN];
	__le16 qos_ctrl;
	__le32 ht_ctrl;
	__le32 raw;
} __packed;

/* rx_mpdu_start
 *
 * rxpcu_mpdu_filter_in_category
 *		Field indicates what the reason was that this mpdu frame
 *		was allowed to come into the receive path by rxpcu. Values
 *		are defined in enum %RX_DESC_RXPCU_FILTER_*.
 *		Note: for ndp frame, if it was expected because the preceding
 *		NDPA was filter_pass, the setting rxpcu_filter_pass will be
 *		used. This setting will also be used for every ndp frame in
 *		case Promiscuous mode is enabled.
 *
 * sw_frame_group_id
 *		SW processes frames based on certain classifications. Values
 *		are defined in enum %RX_DESC_SW_FRAME_GRP_ID_*.
 *
 * ndp_frame
 *		Indicates that the received frame was an NDP frame.
 *
 * phy_err
 *		Indicates that PHY error was received before MAC received data.
 *
 * phy_err_during_mpdu_header
 *		PHY error was received before MAC received the complete MPDU
 *		header which was needed for proper decoding.
 *
 * protocol_version_err
 *		RXPCU detected a version error in the frame control field.
 *
 * ast_based_lookup_valid
 *		AST based lookup for this frame has found a valid result.
 *
 * phy_ppdu_id
 *		A ppdu counter value that PHY increments for every PPDU
 *		received. The counter value wraps around.
 *
 * ast_index
 *		This field indicates the index of the AST entry corresponding
 *		to this MPDU. It is provided by the GSE module instantiated in
 *		RXPCU. A value of 0xFFFF indicates an invalid AST index.
 *
 * sw_peer_id
 *		This field indicates a unique peer identifier. It is set equal
 *		to field 'sw_peer_id' from the AST entry.
 *
 * mpdu_frame_control_valid, mpdu_duration_valid, mpdu_qos_control_valid,
 * mpdu_ht_control_valid, frame_encryption_info_valid
 *		Indicates that each fields have valid entries.
 *
 * mac_addr_adx_valid
 *		Corresponding mac_addr_adx_{lo/hi} has valid entries.
 *
 * from_ds, to_ds
 *		Valid only when mpdu_frame_control_valid is set. Indicates that
 *		frame is received from DS and sent to DS.
 *
 * encrypted
 *		Protected bit from the frame control.
 *
 * mpdu_retry
 *		Retry bit from frame control. Only valid when first_msdu is set.
 *
 * mpdu_sequence_number
 *		The sequence number from the 802.11 header.
 *
 * epd_en
 *		If set, use EPD instead of LPD.
 *
 * all_frames_shall_be_encrypted
 *		If set, all frames (data only?) shall be encrypted. If not,
 *		RX CRYPTO shall set an error flag.
 *
 * encrypt_type
 *		Values are defined in enum %HAL_ENCRYPT_TYPE_.
 *
 * mesh_sta
 *		Indicates a Mesh (11s) STA.
 *
 * bssid_hit
 *		 BSSID of the incoming frame matched one of the 8 BSSID
 *		 register values.
 *
 * bssid_number
 *		This number indicates which one out of the 8 BSSID register
 *		values matched the incoming frame.
 *
 * tid
 *		TID field in the QoS control field
 *
 * pn
 *		The PN number.
 *
 * peer_meta_data
 *		Meta data that SW has programmed in the Peer table entry
 *		of the transmitting STA.
 *
 * rx_reo_queue_desc_addr_lo
 *		Address (lower 32 bits) of the REO queue descriptor.
 *
 * rx_reo_queue_desc_addr_hi
 *		Address (upper 8 bits) of the REO queue descriptor.
 *
 * receive_queue_number
 *		Indicates the MPDU queue ID to which this MPDU link
 *		descriptor belongs.
 *
 * pre_delim_err_warning
 *		Indicates that a delimiter FCS error was found in between the
 *		previous MPDU and this MPDU. Note that this is just a warning,
 *		and does not mean that this MPDU is corrupted in any way. If
 *		it is, there will be other errors indicated such as FCS or
 *		decrypt errors.
 *
 * first_delim_err
 *		Indicates that the first delimiter had a FCS failure.
 *
 * key_id
 *		The key ID octet from the IV.
 *
 * new_peer_entry
 *		Set if new RX_PEER_ENTRY TLV follows. If clear, RX_PEER_ENTRY
 *		doesn't follow so RX DECRYPTION module either uses old peer
 *		entry or not decrypt.
 *
 * decrypt_needed
 *		When RXPCU sets bit 'ast_index_not_found or ast_index_timeout',
 *		RXPCU will also ensure that this bit is NOT set. CRYPTO for that
 *		reason only needs to evaluate this bit and non of the other ones
 *
 * decap_type
 *		Used by the OLE during decapsulation. Values are defined in
 *		enum %MPDU_START_DECAP_TYPE_*.
 *
 * rx_insert_vlan_c_tag_padding
 * rx_insert_vlan_s_tag_padding
 *		Insert 4 byte of all zeros as VLAN tag or double VLAN tag if
 *		the rx payload does not have VLAN.
 *
 * strip_vlan_c_tag_decap
 * strip_vlan_s_tag_decap
 *		Strip VLAN or double VLAN during decapsulation.
 *
 * pre_delim_count
 *		The number of delimiters before this MPDU. Note that this
 *		number is cleared at PPDU start. If this MPDU is the first
 *		received MPDU in the PPDU and this MPDU gets filtered-in,
 *		this field will indicate the number of delimiters located
 *		after the last MPDU in the previous PPDU.
 *
 *		If this MPDU is located after the first received MPDU in
 *		an PPDU, this field will indicate the number of delimiters
 *		located between the previous MPDU and this MPDU.
 *
 * ampdu_flag
 *		Received frame was part of an A-MPDU.
 *
 * bar_frame
 *		Received frame is a BAR frame
 *
 * mpdu_length
 *		MPDU length before decapsulation.
 *
 * first_mpdu..directed
 *		See definition in RX attention descriptor
 *
 */

enum rx_msdu_start_pkt_type {
	RX_MSDU_START_PKT_TYPE_11A,
	RX_MSDU_START_PKT_TYPE_11B,
	RX_MSDU_START_PKT_TYPE_11N,
	RX_MSDU_START_PKT_TYPE_11AC,
	RX_MSDU_START_PKT_TYPE_11AX,
};

enum rx_msdu_start_sgi {
	RX_MSDU_START_SGI_0_8_US,
	RX_MSDU_START_SGI_0_4_US,
	RX_MSDU_START_SGI_1_6_US,
	RX_MSDU_START_SGI_3_2_US,
};

enum rx_msdu_start_recv_bw {
	RX_MSDU_START_RECV_BW_20MHZ,
	RX_MSDU_START_RECV_BW_40MHZ,
	RX_MSDU_START_RECV_BW_80MHZ,
	RX_MSDU_START_RECV_BW_160MHZ,
};

enum rx_msdu_start_reception_type {
	RX_MSDU_START_RECEPTION_TYPE_SU,
	RX_MSDU_START_RECEPTION_TYPE_DL_MU_MIMO,
	RX_MSDU_START_RECEPTION_TYPE_DL_MU_OFDMA,
	RX_MSDU_START_RECEPTION_TYPE_DL_MU_OFDMA_MIMO,
	RX_MSDU_START_RECEPTION_TYPE_UL_MU_MIMO,
	RX_MSDU_START_RECEPTION_TYPE_UL_MU_OFDMA,
	RX_MSDU_START_RECEPTION_TYPE_UL_MU_OFDMA_MIMO,
};

#define RX_MSDU_START_INFO1_MSDU_LENGTH		GENMASK(13, 0)
#define RX_MSDU_START_INFO1_RSVD_1A		BIT(14)
#define RX_MSDU_START_INFO1_IPSEC_ESP		BIT(15)
#define RX_MSDU_START_INFO1_L3_OFFSET		GENMASK(22, 16)
#define RX_MSDU_START_INFO1_IPSEC_AH		BIT(23)
#define RX_MSDU_START_INFO1_L4_OFFSET		GENMASK(31, 24)

#define RX_MSDU_START_INFO2_MSDU_NUMBER		GENMASK(7, 0)
#define RX_MSDU_START_INFO2_DECAP_TYPE		GENMASK(9, 8)
#define RX_MSDU_START_INFO2_IPV4		BIT(10)
#define RX_MSDU_START_INFO2_IPV6		BIT(11)
#define RX_MSDU_START_INFO2_TCP			BIT(12)
#define RX_MSDU_START_INFO2_UDP			BIT(13)
#define RX_MSDU_START_INFO2_IP_FRAG		BIT(14)
#define RX_MSDU_START_INFO2_TCP_ONLY_ACK	BIT(15)
#define RX_MSDU_START_INFO2_DA_IS_BCAST_MCAST	BIT(16)
#define RX_MSDU_START_INFO2_SELECTED_TOEPLITZ_HASH	GENMASK(18, 17)
#define RX_MSDU_START_INFO2_IP_FIXED_HDR_VALID		BIT(19)
#define RX_MSDU_START_INFO2_IP_EXTN_HDR_VALID		BIT(20)
#define RX_MSDU_START_INFO2_IP_TCP_UDP_HDR_VALID	BIT(21)
#define RX_MSDU_START_INFO2_MESH_CTRL_PRESENT		BIT(22)
#define RX_MSDU_START_INFO2_LDPC			BIT(23)
#define RX_MSDU_START_INFO2_IP4_IP6_NXT_HDR		GENMASK(31, 24)
#define RX_MSDU_START_INFO2_DECAP_FORMAT		GENMASK(9, 8)

#define RX_MSDU_START_INFO3_USER_RSSI		GENMASK(7, 0)
#define RX_MSDU_START_INFO3_PKT_TYPE		GENMASK(11, 8)
#define RX_MSDU_START_INFO3_STBC		BIT(12)
#define RX_MSDU_START_INFO3_SGI			GENMASK(14, 13)
#define RX_MSDU_START_INFO3_RATE_MCS		GENMASK(18, 15)
#define RX_MSDU_START_INFO3_RECV_BW		GENMASK(20, 19)
#define RX_MSDU_START_INFO3_RECEPTION_TYPE	GENMASK(23, 21)
#define RX_MSDU_START_INFO3_MIMO_SS_BITMAP	GENMASK(31, 24)

struct rx_msdu_start {
	__le16 info0;
	__le16 phy_ppdu_id;
	__le32 info1;
	__le32 info2;
	__le32 toeplitz_hash;
	__le32 flow_id_toeplitz;
	__le32 info3;
	__le32 ppdu_start_timestamp;
	__le32 phy_meta_data;
} __packed;

/* rx_msdu_start
 *
 * rxpcu_mpdu_filter_in_category
 *		Field indicates what the reason was that this mpdu frame
 *		was allowed to come into the receive path by rxpcu. Values
 *		are defined in enum %RX_DESC_RXPCU_FILTER_*.
 *
 * sw_frame_group_id
 *		SW processes frames based on certain classifications. Values
 *		are defined in enum %RX_DESC_SW_FRAME_GRP_ID_*.
 *
 * phy_ppdu_id
 *		A ppdu counter value that PHY increments for every PPDU
 *		received. The counter value wraps around.
 *
 * msdu_length
 *		MSDU length in bytes after decapsulation.
 *
 * ipsec_esp
 *		Set if IPv4/v6 packet is using IPsec ESP.
 *
 * l3_offset
 *		Depending upon mode bit, this field either indicates the
 *		L3 offset in bytes from the start of the RX_HEADER or the IP
 *		offset in bytes from the start of the packet after
 *		decapsulation. The latter is only valid if ipv4_proto or
 *		ipv6_proto is set.
 *
 * ipsec_ah
 *		Set if IPv4/v6 packet is using IPsec AH
 *
 * l4_offset
 *		Depending upon mode bit, this field either indicates the
 *		L4 offset nin bytes from the start of RX_HEADER (only valid
 *		if either ipv4_proto or ipv6_proto is set to 1) or indicates
 *		the offset in bytes to the start of TCP or UDP header from
 *		the start of the IP header after decapsulation (Only valid if
 *		tcp_proto or udp_proto is set). The value 0 indicates that
 *		the offset is longer than 127 bytes.
 *
 * msdu_number
 *		Indicates the MSDU number within a MPDU.  This value is
 *		reset to zero at the start of each MPDU.  If the number of
 *		MSDU exceeds 255 this number will wrap using modulo 256.
 *
 * decap_type
 *		Indicates the format after decapsulation. Values are defined in
 *		enum %MPDU_START_DECAP_TYPE_*.
 *
 * ipv4_proto
 *		Set if L2 layer indicates IPv4 protocol.
 *
 * ipv6_proto
 *		Set if L2 layer indicates IPv6 protocol.
 *
 * tcp_proto
 *		Set if the ipv4_proto or ipv6_proto are set and the IP protocol
 *		indicates TCP.
 *
 * udp_proto
 *		Set if the ipv4_proto or ipv6_proto are set and the IP protocol
 *		indicates UDP.
 *
 * ip_frag
 *		Indicates that either the IP More frag bit is set or IP frag
 *		number is non-zero.  If set indicates that this is a fragmented
 *		IP packet.
 *
 * tcp_only_ack
 *		Set if only the TCP Ack bit is set in the TCP flags and if
 *		the TCP payload is 0.
 *
 * da_is_bcast_mcast
 *		The destination address is broadcast or multicast.
 *
 * toeplitz_hash
 *		Actual chosen Hash.
 *		0 - Toeplitz hash of 2-tuple (IP source address, IP
 *		    destination address)
 *		1 - Toeplitz hash of 4-tuple (IP source	address,
 *		    IP destination address, L4 (TCP/UDP) source port,
 *		    L4 (TCP/UDP) destination port)
 *		2 - Toeplitz of flow_id
 *		3 - Zero is used
 *
 * ip_fixed_header_valid
 *		Fixed 20-byte IPv4 header or 40-byte IPv6 header parsed
 *		fully within first 256 bytes of the packet
 *
 * ip_extn_header_valid
 *		IPv6/IPv6 header, including IPv4 options and
 *		recognizable extension headers parsed fully within first 256
 *		bytes of the packet
 *
 * tcp_udp_header_valid
 *		Fixed 20-byte TCP (excluding TCP options) or 8-byte UDP
 *		header parsed fully within first 256 bytes of the packet
 *
 * mesh_control_present
 *		When set, this MSDU includes the 'Mesh Control' field
 *
 * ldpc
 *
 * ip4_protocol_ip6_next_header
 *		For IPv4, this is the 8 bit protocol field set). For IPv6 this
 *		is the 8 bit next_header field.
 *
 * toeplitz_hash_2_or_4
 *		Controlled by RxOLE register - If register bit set to 0,
 *		Toeplitz hash is computed over 2-tuple IPv4 or IPv6 src/dest
 *		addresses; otherwise, toeplitz hash is computed over 4-tuple
 *		IPv4 or IPv6 src/dest addresses and src/dest ports.
 *
 * flow_id_toeplitz
 *		Toeplitz hash of 5-tuple
 *		{IP source address, IP destination address, IP source port, IP
 *		destination port, L4 protocol}  in case of non-IPSec.
 *
 *		In case of IPSec - Toeplitz hash of 4-tuple
 *		{IP source address, IP destination address, SPI, L4 protocol}
 *
 *		The relevant Toeplitz key registers are provided in RxOLE's
 *		instance of common parser module. These registers are separate
 *		from the Toeplitz keys used by ASE/FSE modules inside RxOLE.
 *		The actual value will be passed on from common parser module
 *		to RxOLE in one of the WHO_* TLVs.
 *
 * user_rssi
 *		RSSI for this user
 *
 * pkt_type
 *		Values are defined in enum %RX_MSDU_START_PKT_TYPE_*.
 *
 * stbc
 *		When set, use STBC transmission rates.
 *
 * sgi
 *		Field only valid when pkt type is HT, VHT or HE. Values are
 *		defined in enum %RX_MSDU_START_SGI_*.
 *
 * rate_mcs
 *		MCS Rate used.
 *
 * receive_bandwidth
 *		Full receive Bandwidth. Values are defined in enum
 *		%RX_MSDU_START_RECV_*.
 *
 * reception_type
 *		Indicates what type of reception this is and defined in enum
 *		%RX_MSDU_START_RECEPTION_TYPE_*.
 *
 * mimo_ss_bitmap
 *		Field only valid when
 *		Reception_type is RX_MSDU_START_RECEPTION_TYPE_DL_MU_MIMO or
 *		RX_MSDU_START_RECEPTION_TYPE_DL_MU_OFDMA_MIMO.
 *
 *		Bitmap, with each bit indicating if the related spatial
 *		stream is used for this STA
 *
 *		LSB related to SS 0
 *
 *		0 - spatial stream not used for this reception
 *		1 - spatial stream used for this reception
 *
 * ppdu_start_timestamp
 *		Timestamp that indicates when the PPDU that contained this MPDU
 *		started on the medium.
 *
 * phy_meta_data
 *		SW programmed Meta data provided by the PHY. Can be used for SW
 *		to indicate the channel the device is on.
 */

#define RX_MSDU_END_INFO0_RXPCU_MPDU_FITLER	GENMASK(1, 0)
#define RX_MSDU_END_INFO0_SW_FRAME_GRP_ID	GENMASK(8, 2)

#define RX_MSDU_END_INFO1_KEY_ID		GENMASK(7, 0)
#define RX_MSDU_END_INFO1_CCE_SUPER_RULE	GENMASK(13, 8)
#define RX_MSDU_END_INFO1_CCND_TRUNCATE		BIT(14)
#define RX_MSDU_END_INFO1_CCND_CCE_DIS		BIT(15)
#define RX_MSDU_END_INFO1_EXT_WAPI_PN		GENMASK(31, 16)

#define RX_MSDU_END_INFO2_REPORTED_MPDU_LEN	GENMASK(13, 0)
#define RX_MSDU_END_INFO2_FIRST_MSDU		BIT(14)
#define RX_MSDU_END_INFO2_LAST_MSDU		BIT(15)
#define RX_MSDU_END_INFO2_SA_IDX_TIMEOUT	BIT(16)
#define RX_MSDU_END_INFO2_DA_IDX_TIMEOUT	BIT(17)
#define RX_MSDU_END_INFO2_MSDU_LIMIT_ERR	BIT(18)
#define RX_MSDU_END_INFO2_FLOW_IDX_TIMEOUT	BIT(19)
#define RX_MSDU_END_INFO2_FLOW_IDX_INVALID	BIT(20)
#define RX_MSDU_END_INFO2_WIFI_PARSER_ERR	BIT(21)
#define RX_MSDU_END_INFO2_AMSDU_PARSET_ERR	BIT(22)
#define RX_MSDU_END_INFO2_SA_IS_VALID		BIT(23)
#define RX_MSDU_END_INFO2_DA_IS_VALID		BIT(24)
#define RX_MSDU_END_INFO2_DA_IS_MCBC		BIT(25)
#define RX_MSDU_END_INFO2_L3_HDR_PADDING	GENMASK(27, 26)

#define RX_MSDU_END_INFO3_TCP_FLAG		GENMASK(8, 0)
#define RX_MSDU_END_INFO3_LRO_ELIGIBLE		BIT(9)

#define RX_MSDU_END_INFO4_DA_OFFSET		GENMASK(5, 0)
#define RX_MSDU_END_INFO4_SA_OFFSET		GENMASK(11, 6)
#define RX_MSDU_END_INFO4_DA_OFFSET_VALID	BIT(12)
#define RX_MSDU_END_INFO4_SA_OFFSET_VALID	BIT(13)
#define RX_MSDU_END_INFO4_L3_TYPE		GENMASK(31, 16)

#define RX_MSDU_END_INFO5_MSDU_DROP		BIT(0)
#define RX_MSDU_END_INFO5_REO_DEST_IND		GENMASK(5, 1)
#define RX_MSDU_END_INFO5_FLOW_IDX		GENMASK(25, 6)

struct rx_msdu_end {
	__le16 info0;
	__le16 phy_ppdu_id;
	__le16 ip_hdr_cksum;
	__le16 tcp_udp_cksum;
	__le32 info1;
	__le32 ext_wapi_pn[2];
	__le32 info2;
	__le32 ipv6_options_crc;
	__le32 tcp_seq_num;
	__le32 tcp_ack_num;
	__le16 info3;
	__le16 window_size;
	__le32 info4;
	__le32 rule_indication[2];
	__le16 sa_idx;
	__le16 da_idx;
	__le32 info5;
	__le32 fse_metadata;
	__le16 cce_metadata;
	__le16 sa_sw_peer_id;
} __packed;

/* rx_msdu_end
 *
 * rxpcu_mpdu_filter_in_category
 *		Field indicates what the reason was that this mpdu frame
 *		was allowed to come into the receive path by rxpcu. Values
 *		are defined in enum %RX_DESC_RXPCU_FILTER_*.
 *
 * sw_frame_group_id
 *		SW processes frames based on certain classifications. Values
 *		are defined in enum %RX_DESC_SW_FRAME_GRP_ID_*.
 *
 * phy_ppdu_id
 *		A ppdu counter value that PHY increments for every PPDU
 *		received. The counter value wraps around.
 *
 * ip_hdr_cksum
 *		This can include the IP header checksum or the pseudo
 *		header checksum used by TCP/UDP checksum.
 *
 * tcp_udp_chksum
 *		The value of the computed TCP/UDP checksum.  A mode bit
 *		selects whether this checksum is the full checksum or the
 *		partial checksum which does not include the pseudo header.
 *
 * key_id
 *		The key ID octet from the IV. Only valid when first_msdu is set.
 *
 * cce_super_rule
 *		Indicates the super filter rule.
 *
 * cce_classify_not_done_truncate
 *		Classification failed due to truncated frame.
 *
 * cce_classify_not_done_cce_dis
 *		Classification failed due to CCE global disable
 *
 * ext_wapi_pn*
 *		Extension PN (packet number) which is only used by WAPI.
 *
 * reported_mpdu_length
 *		MPDU length before decapsulation. Only valid when first_msdu is
 *		set. This field is taken directly from the length field of the
 *		A-MPDU delimiter or the preamble length field for non-A-MPDU
 *		frames.
 *
 * first_msdu
 *		Indicates the first MSDU of A-MSDU. If both first_msdu and
 *		last_msdu are set in the MSDU then this is a non-aggregated MSDU
 *		frame: normal MPDU. Interior MSDU in an A-MSDU shall have both
 *		first_mpdu and last_mpdu bits set to 0.
 *
 * last_msdu
 *		Indicates the last MSDU of the A-MSDU. MPDU end status is only
 *		valid when last_msdu is set.
 *
 * sa_idx_timeout
 *		Indicates an unsuccessful MAC source address search due to the
 *		expiring of the search timer.
 *
 * da_idx_timeout
 *		Indicates an unsuccessful MAC destination address search due to
 *		the expiring of the search timer.
 *
 * msdu_limit_error
 *		Indicates that the MSDU threshold was exceeded and thus all the
 *		rest of the MSDUs will not be scattered and will not be
 *		decapsulated but will be DMA'ed in RAW format as a single MSDU.
 *
 * flow_idx_timeout
 *		Indicates an unsuccessful flow search due to the expiring of
 *		the search timer.
 *
 * flow_idx_invalid
 *		flow id is not valid.
 *
 * amsdu_parser_error
 *		A-MSDU could not be properly de-agregated.
 *
 * sa_is_valid
 *		Indicates that OLE found a valid SA entry.
 *
 * da_is_valid
 *		Indicates that OLE found a valid DA entry.
 *
 * da_is_mcbc
 *		Field Only valid if da_is_valid is set. Indicates the DA address
 *		was a Multicast of Broadcast address.
 *
 * l3_header_padding
 *		Number of bytes padded  to make sure that the L3 header will
 *		always start of a Dword boundary.
 *
 * ipv6_options_crc
 *		32 bit CRC computed out of  IP v6 extension headers.
 *
 * tcp_seq_number
 *		TCP sequence number.
 *
 * tcp_ack_number
 *		TCP acknowledge number.
 *
 * tcp_flag
 *		TCP flags {NS, CWR, ECE, URG, ACK, PSH, RST, SYN, FIN}.
 *
 * lro_eligible
 *		Computed out of TCP and IP fields to indicate that this
 *		MSDU is eligible for LRO.
 *
 * window_size
 *		TCP receive window size.
 *
 * da_offset
 *		Offset into MSDU buffer for DA.
 *
 * sa_offset
 *		Offset into MSDU buffer for SA.
 *
 * da_offset_valid
 *		da_offset field is valid. This will be set to 0 in case
 *		of a dynamic A-MSDU when DA is compressed.
 *
 * sa_offset_valid
 *		sa_offset field is valid. This will be set to 0 in case
 *		of a dynamic A-MSDU when SA is compressed.
 *
 * l3_type
 *		The 16-bit type value indicating the type of L3 later
 *		extracted from LLC/SNAP, set to zero if SNAP is not
 *		available.
 *
 * rule_indication
 *		Bitmap indicating which of rules have matched.
 *
 * sa_idx
 *		The offset in the address table which matches MAC source address
 *
 * da_idx
 *		The offset in the address table which matches MAC destination
 *		address.
 *
 * msdu_drop
 *		REO shall drop this MSDU and not forward it to any other ring.
 *
 * reo_destination_indication
 *		The id of the reo exit ring where the msdu frame shall push
 *		after (MPDU level) reordering has finished. Values are defined
 *		in enum %HAL_RX_MSDU_DESC_REO_DEST_IND_.
 *
 * flow_idx
 *		Flow table index.
 *
 * fse_metadata
 *		FSE related meta data.
 *
 * cce_metadata
 *		CCE related meta data.
 *
 * sa_sw_peer_id
 *		sw_peer_id from the address search entry corresponding to the
 *		source address of the MSDU.
 */

enum rx_mpdu_end_rxdma_dest_ring {
	RX_MPDU_END_RXDMA_DEST_RING_RELEASE,
	RX_MPDU_END_RXDMA_DEST_RING_FW,
	RX_MPDU_END_RXDMA_DEST_RING_SW,
	RX_MPDU_END_RXDMA_DEST_RING_REO,
};

#define RX_MPDU_END_INFO1_UNSUP_KTYPE_SHORT_FRAME	BIT(11)
#define RX_MPDU_END_INFO1_RX_IN_TX_DECRYPT_BYT		BIT(12)
#define RX_MPDU_END_INFO1_OVERFLOW_ERR			BIT(13)
#define RX_MPDU_END_INFO1_MPDU_LEN_ERR			BIT(14)
#define RX_MPDU_END_INFO1_TKIP_MIC_ERR			BIT(15)
#define RX_MPDU_END_INFO1_DECRYPT_ERR			BIT(16)
#define RX_MPDU_END_INFO1_UNENCRYPTED_FRAME_ERR		BIT(17)
#define RX_MPDU_END_INFO1_PN_FIELDS_VALID		BIT(18)
#define RX_MPDU_END_INFO1_FCS_ERR			BIT(19)
#define RX_MPDU_END_INFO1_MSDU_LEN_ERR			BIT(20)
#define RX_MPDU_END_INFO1_RXDMA0_DEST_RING		GENMASK(22, 21)
#define RX_MPDU_END_INFO1_RXDMA1_DEST_RING		GENMASK(24, 23)
#define RX_MPDU_END_INFO1_DECRYPT_STATUS_CODE		GENMASK(27, 25)
#define RX_MPDU_END_INFO1_RX_BITMAP_NOT_UPD		BIT(28)

struct rx_mpdu_end {
	__le16 info0;
	__le16 phy_ppdu_id;
	__le32 info1;
} __packed;

/* rx_mpdu_end
 *
 * rxpcu_mpdu_filter_in_category
 *		Field indicates what the reason was that this mpdu frame
 *		was allowed to come into the receive path by rxpcu. Values
 *		are defined in enum %RX_DESC_RXPCU_FILTER_*.
 *
 * sw_frame_group_id
 *		SW processes frames based on certain classifications. Values
 *		are defined in enum %RX_DESC_SW_FRAME_GRP_ID_*.
 *
 * phy_ppdu_id
 *		A ppdu counter value that PHY increments for every PPDU
 *		received. The counter value wraps around.
 *
 * unsup_ktype_short_frame
 *		This bit will be '1' when WEP or TKIP or WAPI key type is
 *		received for 11ah short frame. Crypto will bypass the received
 *		packet without decryption to RxOLE after setting this bit.
 *
 * rx_in_tx_decrypt_byp
 *		Indicates that RX packet is not decrypted as Crypto is
 *		busy with TX packet processing.
 *
 * overflow_err
 *		RXPCU Receive FIFO ran out of space to receive the full MPDU.
 *		Therefore this MPDU is terminated early and is thus corrupted.
 *
 *		This MPDU will not be ACKed.
 *
 *		RXPCU might still be able to correctly receive the following
 *		MPDUs in the PPDU if enough fifo space became available in time.
 *
 * mpdu_length_err
 *		Set by RXPCU if the expected MPDU length does not correspond
 *		with the actually received number of bytes in the MPDU.
 *
 * tkip_mic_err
 *		Set by Rx crypto when crypto detected a TKIP MIC error for
 *		this MPDU.
 *
 * decrypt_err
 *		Set by RX CRYPTO when CRYPTO detected a decrypt error for this
 *		MPDU or CRYPTO received an encrypted frame, but did not get a
 *		valid corresponding key id in the peer entry.
 *
 * unencrypted_frame_err
 *		Set by RX CRYPTO when CRYPTO detected an unencrypted frame while
 *		in the peer entry field 'All_frames_shall_be_encrypted' is set.
 *
 * pn_fields_contain_valid_info
 *		Set by RX CRYPTO to indicate that there is a valid PN field
 *		present in this MPDU.
 *
 * fcs_err
 *		Set by RXPCU when there is an FCS error detected for this MPDU.
 *
 * msdu_length_err
 *		Set by RXOLE when there is an msdu length error detected
 *		in at least 1 of the MSDUs embedded within the MPDU.
 *
 * rxdma0_destination_ring
 * rxdma1_destination_ring
 *		The ring to which RXDMA0/1 shall push the frame, assuming
 *		no MPDU level errors are detected. In case of MPDU level
 *		errors, RXDMA0/1 might change the RXDMA0/1 destination. Values
 *		are defined in %enum RX_MPDU_END_RXDMA_DEST_RING_*.
 *
 * decrypt_status_code
 *		Field provides insight into the decryption performed. Values
 *		are defined in enum %RX_DESC_DECRYPT_STATUS_CODE_*.
 *
 * rx_bitmap_not_updated
 *		Frame is received, but RXPCU could not update the receive bitmap
 *		due to (temporary) fifo constraints.
 */

/* Padding bytes to avoid TLV's spanning across 128 byte boundary */
#define HAL_RX_DESC_PADDING0_BYTES	4
#define HAL_RX_DESC_PADDING1_BYTES	16

#define HAL_RX_DESC_HDR_STATUS_LEN	120

struct hal_rx_desc {
	__le32 msdu_end_tag;
	struct rx_msdu_end msdu_end;
	__le32 rx_attn_tag;
	struct rx_attention attention;
	__le32 msdu_start_tag;
	struct rx_msdu_start msdu_start;
	u8 rx_padding0[HAL_RX_DESC_PADDING0_BYTES];
	__le32 mpdu_start_tag;
	struct rx_mpdu_start mpdu_start;
	__le32 mpdu_end_tag;
	struct rx_mpdu_end mpdu_end;
	u8 rx_padding1[HAL_RX_DESC_PADDING1_BYTES];
	__le32 hdr_status_tag;
	__le32 phy_ppdu_id;
	u8 hdr_status[HAL_RX_DESC_HDR_STATUS_LEN];
	u8 msdu_payload[0];
} __packed;

#endif /* ATH11K_RX_DESC_H */
