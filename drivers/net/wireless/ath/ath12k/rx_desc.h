/* SPDX-License-Identifier: BSD-3-Clause-Clear */
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef ATH12K_RX_DESC_H
#define ATH12K_RX_DESC_H

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

#define RX_MPDU_START_INFO0_REO_DEST_IND		GENMASK(4, 0)
#define RX_MPDU_START_INFO0_LMAC_PEER_ID_MSB		GENMASK(6, 5)
#define RX_MPDU_START_INFO0_FLOW_ID_TOEPLITZ		BIT(7)
#define RX_MPDU_START_INFO0_PKT_SEL_FP_UCAST_DATA	BIT(8)
#define RX_MPDU_START_INFO0_PKT_SEL_FP_MCAST_DATA	BIT(9)
#define RX_MPDU_START_INFO0_PKT_SEL_FP_CTRL_BAR		BIT(10)
#define RX_MPDU_START_INFO0_RXDMA0_SRC_RING_SEL		GENMASK(13, 11)
#define RX_MPDU_START_INFO0_RXDMA0_DST_RING_SEL		GENMASK(16, 14)
#define RX_MPDU_START_INFO0_MCAST_ECHO_DROP_EN		BIT(17)
#define RX_MPDU_START_INFO0_WDS_LEARN_DETECT_EN		BIT(18)
#define RX_MPDU_START_INFO0_INTRA_BSS_CHECK_EN		BIT(19)
#define RX_MPDU_START_INFO0_USE_PPE			BIT(20)
#define RX_MPDU_START_INFO0_PPE_ROUTING_EN		BIT(21)

#define RX_MPDU_START_INFO1_REO_QUEUE_DESC_HI		GENMASK(7, 0)
#define RX_MPDU_START_INFO1_RECV_QUEUE_NUM		GENMASK(23, 8)
#define RX_MPDU_START_INFO1_PRE_DELIM_ERR_WARN		BIT(24)
#define RX_MPDU_START_INFO1_FIRST_DELIM_ERR		BIT(25)

#define RX_MPDU_START_INFO2_EPD_EN			BIT(0)
#define RX_MPDU_START_INFO2_ALL_FRAME_ENCPD		BIT(1)
#define RX_MPDU_START_INFO2_ENC_TYPE			GENMASK(5, 2)
#define RX_MPDU_START_INFO2_VAR_WEP_KEY_WIDTH		GENMASK(7, 6)
#define RX_MPDU_START_INFO2_MESH_STA			GENMASK(9, 8)
#define RX_MPDU_START_INFO2_BSSID_HIT			BIT(10)
#define RX_MPDU_START_INFO2_BSSID_NUM			GENMASK(14, 11)
#define RX_MPDU_START_INFO2_TID				GENMASK(18, 15)

#define RX_MPDU_START_INFO3_RXPCU_MPDU_FLTR		GENMASK(1, 0)
#define RX_MPDU_START_INFO3_SW_FRAME_GRP_ID		GENMASK(8, 2)
#define RX_MPDU_START_INFO3_NDP_FRAME			BIT(9)
#define RX_MPDU_START_INFO3_PHY_ERR			BIT(10)
#define RX_MPDU_START_INFO3_PHY_ERR_MPDU_HDR		BIT(11)
#define RX_MPDU_START_INFO3_PROTO_VER_ERR		BIT(12)
#define RX_MPDU_START_INFO3_AST_LOOKUP_VALID		BIT(13)
#define RX_MPDU_START_INFO3_RANGING			BIT(14)

#define RX_MPDU_START_INFO4_MPDU_FCTRL_VALID		BIT(0)
#define RX_MPDU_START_INFO4_MPDU_DUR_VALID		BIT(1)
#define RX_MPDU_START_INFO4_MAC_ADDR1_VALID		BIT(2)
#define RX_MPDU_START_INFO4_MAC_ADDR2_VALID		BIT(3)
#define RX_MPDU_START_INFO4_MAC_ADDR3_VALID		BIT(4)
#define RX_MPDU_START_INFO4_MAC_ADDR4_VALID		BIT(5)
#define RX_MPDU_START_INFO4_MPDU_SEQ_CTRL_VALID		BIT(6)
#define RX_MPDU_START_INFO4_MPDU_QOS_CTRL_VALID		BIT(7)
#define RX_MPDU_START_INFO4_MPDU_HT_CTRL_VALID		BIT(8)
#define RX_MPDU_START_INFO4_ENCRYPT_INFO_VALID		BIT(9)
#define RX_MPDU_START_INFO4_MPDU_FRAG_NUMBER		GENMASK(13, 10)
#define RX_MPDU_START_INFO4_MORE_FRAG_FLAG		BIT(14)
#define RX_MPDU_START_INFO4_FROM_DS			BIT(16)
#define RX_MPDU_START_INFO4_TO_DS			BIT(17)
#define RX_MPDU_START_INFO4_ENCRYPTED			BIT(18)
#define RX_MPDU_START_INFO4_MPDU_RETRY			BIT(19)
#define RX_MPDU_START_INFO4_MPDU_SEQ_NUM		GENMASK(31, 20)

#define RX_MPDU_START_INFO5_KEY_ID			GENMASK(7, 0)
#define RX_MPDU_START_INFO5_NEW_PEER_ENTRY		BIT(8)
#define RX_MPDU_START_INFO5_DECRYPT_NEEDED		BIT(9)
#define RX_MPDU_START_INFO5_DECAP_TYPE			GENMASK(11, 10)
#define RX_MPDU_START_INFO5_VLAN_TAG_C_PADDING		BIT(12)
#define RX_MPDU_START_INFO5_VLAN_TAG_S_PADDING		BIT(13)
#define RX_MPDU_START_INFO5_STRIP_VLAN_TAG_C		BIT(14)
#define RX_MPDU_START_INFO5_STRIP_VLAN_TAG_S		BIT(15)
#define RX_MPDU_START_INFO5_PRE_DELIM_COUNT		GENMASK(27, 16)
#define RX_MPDU_START_INFO5_AMPDU_FLAG			BIT(28)
#define RX_MPDU_START_INFO5_BAR_FRAME			BIT(29)
#define RX_MPDU_START_INFO5_RAW_MPDU			BIT(30)

#define RX_MPDU_START_INFO6_MPDU_LEN			GENMASK(13, 0)
#define RX_MPDU_START_INFO6_FIRST_MPDU			BIT(14)
#define RX_MPDU_START_INFO6_MCAST_BCAST			BIT(15)
#define RX_MPDU_START_INFO6_AST_IDX_NOT_FOUND		BIT(16)
#define RX_MPDU_START_INFO6_AST_IDX_TIMEOUT		BIT(17)
#define RX_MPDU_START_INFO6_POWER_MGMT			BIT(18)
#define RX_MPDU_START_INFO6_NON_QOS			BIT(19)
#define RX_MPDU_START_INFO6_NULL_DATA			BIT(20)
#define RX_MPDU_START_INFO6_MGMT_TYPE			BIT(21)
#define RX_MPDU_START_INFO6_CTRL_TYPE			BIT(22)
#define RX_MPDU_START_INFO6_MORE_DATA			BIT(23)
#define RX_MPDU_START_INFO6_EOSP			BIT(24)
#define RX_MPDU_START_INFO6_FRAGMENT			BIT(25)
#define RX_MPDU_START_INFO6_ORDER			BIT(26)
#define RX_MPDU_START_INFO6_UAPSD_TRIGGER		BIT(27)
#define RX_MPDU_START_INFO6_ENCRYPT_REQUIRED		BIT(28)
#define RX_MPDU_START_INFO6_DIRECTED			BIT(29)
#define RX_MPDU_START_INFO6_AMSDU_PRESENT		BIT(30)

#define RX_MPDU_START_INFO7_VDEV_ID			GENMASK(7, 0)
#define RX_MPDU_START_INFO7_SERVICE_CODE		GENMASK(16, 8)
#define RX_MPDU_START_INFO7_PRIORITY_VALID		BIT(17)
#define RX_MPDU_START_INFO7_SRC_INFO			GENMASK(29, 18)

#define RX_MPDU_START_INFO8_AUTH_TO_SEND_WDS		BIT(0)

struct rx_mpdu_start_qcn9274 {
	__le32 info0;
	__le32 reo_queue_desc_lo;
	__le32 info1;
	__le32 pn[4];
	__le32 info2;
	__le32 peer_meta_data;
	__le16 info3;
	__le16 phy_ppdu_id;
	__le16 ast_index;
	__le16 sw_peer_id;
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
	__le32 info7;
	u8 multi_link_addr1[ETH_ALEN];
	u8 multi_link_addr2[ETH_ALEN];
	__le32 info8;
	__le32 res0;
	__le32 res1;
} __packed;

#define QCN9274_MPDU_START_SELECT_MPDU_START_TAG			BIT(0)
#define QCN9274_MPDU_START_SELECT_INFO0_REO_QUEUE_DESC_LO		BIT(1)
#define QCN9274_MPDU_START_SELECT_INFO1_PN_31_0				BIT(2)
#define QCN9274_MPDU_START_SELECT_PN_95_32				BIT(3)
#define QCN9274_MPDU_START_SELECT_PN_127_96_INFO2			BIT(4)
#define QCN9274_MPDU_START_SELECT_PEER_MDATA_INFO3_PHY_PPDU_ID		BIT(5)
#define QCN9274_MPDU_START_SELECT_AST_IDX_SW_PEER_ID_INFO4		BIT(6)
#define QCN9274_MPDU_START_SELECT_INFO5_INFO6				BIT(7)
#define QCN9274_MPDU_START_SELECT_FRAME_CTRL_DURATION_ADDR1_31_0	BIT(8)
#define QCN9274_MPDU_START_SELECT_ADDR2_47_0_ADDR1_47_32		BIT(9)
#define QCN9274_MPDU_START_SELECT_ADDR3_47_0_SEQ_CTRL			BIT(10)
#define QCN9274_MPDU_START_SELECT_ADDR4_47_0_QOS_CTRL			BIT(11)
#define QCN9274_MPDU_START_SELECT_HT_CTRL_INFO7				BIT(12)
#define QCN9274_MPDU_START_SELECT_ML_ADDR1_47_0_ML_ADDR2_15_0		BIT(13)
#define QCN9274_MPDU_START_SELECT_ML_ADDR2_47_16_INFO8			BIT(14)
#define QCN9274_MPDU_START_SELECT_RES_0_RES_1				BIT(15)

#define QCN9274_MPDU_START_WMASK (QCN9274_MPDU_START_SELECT_INFO1_PN_31_0 |	\
		QCN9274_MPDU_START_SELECT_PN_95_32 |				\
		QCN9274_MPDU_START_SELECT_PN_127_96_INFO2 |			\
		QCN9274_MPDU_START_SELECT_PEER_MDATA_INFO3_PHY_PPDU_ID |	\
		QCN9274_MPDU_START_SELECT_AST_IDX_SW_PEER_ID_INFO4 |		\
		QCN9274_MPDU_START_SELECT_INFO5_INFO6 |				\
		QCN9274_MPDU_START_SELECT_FRAME_CTRL_DURATION_ADDR1_31_0 |	\
		QCN9274_MPDU_START_SELECT_ADDR2_47_0_ADDR1_47_32 |		\
		QCN9274_MPDU_START_SELECT_ADDR3_47_0_SEQ_CTRL |			\
		QCN9274_MPDU_START_SELECT_ADDR4_47_0_QOS_CTRL)

/* The below rx_mpdu_start_qcn9274_compact structure is tied with the mask
 * value QCN9274_MPDU_START_WMASK. If the mask value changes the structure
 * will also change.
 */

struct rx_mpdu_start_qcn9274_compact {
	__le32 info1;
	__le32 pn[4];
	__le32 info2;
	__le32 peer_meta_data;
	__le16 info3;
	__le16 phy_ppdu_id;
	__le16 ast_index;
	__le16 sw_peer_id;
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
} __packed;

/* rx_mpdu_start
 *
 * reo_destination_indication
 *		The id of the reo exit ring where the msdu frame shall push
 *		after (MPDU level) reordering has finished. Values are defined
 *		in enum %HAL_RX_MSDU_DESC_REO_DEST_IND_.
 *
 * lmac_peer_id_msb
 *
 *		If use_flow_id_toeplitz_clfy is set and lmac_peer_id_'sb
 *		is 2'b00, Rx OLE uses a REO destination indicati'n of {1'b1,
 *		hash[3:0]} using the chosen Toeplitz hash from Common Parser
 *		if flow search fails.
 *		If use_flow_id_toeplitz_clfy is set and lmac_peer_id_msb
 *		's not 2'b00, Rx OLE uses a REO destination indication of
 *		{lmac_peer_id_msb, hash[2:0]} using the chosen Toeplitz
 *		hash from Common Parser if flow search fails.
 *
 * use_flow_id_toeplitz_clfy
 *		Indication to Rx OLE to enable REO destination routing based
 *		on the chosen Toeplitz hash from Common Parser, in case
 *		flow search fails
 *
 * pkt_selection_fp_ucast_data
 *		Filter pass Unicast data frame (matching rxpcu_filter_pass
 *		and sw_frame_group_Unicast_data) routing selection
 *
 * pkt_selection_fp_mcast_data
 *		Filter pass Multicast data frame (matching rxpcu_filter_pass
 *		and sw_frame_group_Multicast_data) routing selection
 *
 * pkt_selection_fp_ctrl_bar
 *		Filter pass BAR frame (matching rxpcu_filter_pass
 *		and sw_frame_group_ctrl_1000) routing selection
 *
 * rxdma0_src_ring_selection
 *		Field only valid when for the received frame type the corresponding
 *		pkt_selection_fp_... bit is set
 *
 * rxdma0_dst_ring_selection
 *		Field only valid when for the received frame type the corresponding
 *		pkt_selection_fp_... bit is set
 *
 * mcast_echo_drop_enable
 *		If set, for multicast packets, multicast echo check (i.e.
 *		SA search with mcast_echo_check = 1) shall be performed
 *		by RXOLE, and any multicast echo packets should be indicated
 *		 to RXDMA for release to WBM
 *
 * wds_learning_detect_en
 *		If set, WDS learning detection based on SA search and notification
 *		to FW (using RXDMA0 status ring) is enabled and the "timestamp"
 *		field in address search failure cache-only entry should
 *		be used to avoid multiple WDS learning notifications.
 *
 * intrabss_check_en
 *		If set, intra-BSS routing detection is enabled
 *
 * use_ppe
 *		Indicates to RXDMA to ignore the REO_destination_indication
 *		and use a programmed value corresponding to the REO2PPE
 *		ring
 *		This override to REO2PPE for packets requiring multiple
 *		buffers shall be disabled based on an RXDMA configuration,
 *		as PPE may not support such packets.
 *
 *		Supported only in full AP chips, not in client/soft
 *		chips
 *
 * ppe_routing_enable
 *		Global enable/disable bit for routing to PPE, used to disable
 *		PPE routing even if RXOLE CCE or flow search indicate 'Use_PPE'
 *		This is set by SW for peers which are being handled by a
 *		host SW/accelerator subsystem that also handles packet
 *		buffer management for WiFi-to-PPE routing.
 *
 *		This is cleared by SW for peers which are being handled
 *		by a different subsystem, completely disabling WiFi-to-PPE
 *		routing for such peers.
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
 * pn
 *		The PN number.
 *
 * epd_en
 *		Field only valid when AST_based_lookup_valid == 1.
 *		In case of ndp or phy_err or AST_based_lookup_valid == 0,
 *		this field will be set to 0
 *		If set to one use EPD instead of LPD
 *		In case of ndp or phy_err, this field will never be set.
 *
 * all_frames_shall_be_encrypted
 *		In case of ndp or phy_err or AST_based_lookup_valid == 0,
 *		this field will be set to 0
 *
 *		When set, all frames (data only ?) shall be encrypted. If
 *		not, RX CRYPTO shall set an error flag.
 *
 *
 * encrypt_type
 *		In case of ndp or phy_err or AST_based_lookup_valid == 0,
 *		this field will be set to 0
 *
 *		Indicates type of decrypt cipher used (as defined in the
 *		peer entry)
 *
 * wep_key_width_for_variable_key
 *
 *		Field only valid when key_type is set to wep_varied_width.
 *
 * mesh_sta
 *
 * bssid_hit
 *		When set, the BSSID of the incoming frame matched one of
 *		 the 8 BSSID register values
 * bssid_number
 *		Field only valid when bssid_hit is set.
 *		This number indicates which one out of the 8 BSSID register
 *		values matched the incoming frame
 *
 * tid
 *		Field only valid when mpdu_qos_control_valid is set
 *		The TID field in the QoS control field
 *
 * peer_meta_data
 *		Meta data that SW has programmed in the Peer table entry
 *		of the transmitting STA.
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
 * ndp_frame
 *		When set, the received frame was an NDP frame, and thus
 *		there will be no MPDU data.
 * phy_err
 *		When set, a PHY error was received before MAC received any
 *		data, and thus there will be no MPDU data.
 *
 * phy_err_during_mpdu_header
 *		When set, a PHY error was received before MAC received the
 *		complete MPDU header which was needed for proper decoding
 *
 * protocol_version_err
 *		Set when RXPCU detected a version error in the Frame control
 *		field
 *
 * ast_based_lookup_valid
 *		When set, AST based lookup for this frame has found a valid
 *		result.
 *
 * ranging
 *		When set, a ranging NDPA or a ranging NDP was received.
 *
 * phy_ppdu_id
 *		A ppdu counter value that PHY increments for every PPDU
 *		received. The counter value wraps around.
 *
 * ast_index
 *
 *		This field indicates the index of the AST entry corresponding
 *		to this MPDU. It is provided by the GSE module instantiated
 *		in RXPCU.
 *		A value of 0xFFFF indicates an invalid AST index, meaning
 *		that No AST entry was found or NO AST search was performed
 *
 * sw_peer_id
 *		In case of ndp or phy_err or AST_based_lookup_valid == 0,
 *		this field will be set to 0
 *		This field indicates a unique peer identifier. It is set
 *		equal to field 'sw_peer_id' from the AST entry
 *
 * frame_control_valid
 *		When set, the field Mpdu_Frame_control_field has valid information
 *
 * frame_duration_valid
 *		When set, the field Mpdu_duration_field has valid information
 *
 * mac_addr_ad1..4_valid
 *		When set, the fields mac_addr_adx_..... have valid information
 *
 * mpdu_seq_ctrl_valid
 *
 *		When set, the fields mpdu_sequence_control_field and mpdu_sequence_number
 *		have valid information as well as field
 *		For MPDUs without a sequence control field, this field will
 *		not be set.
 *
 * mpdu_qos_ctrl_valid, mpdu_ht_ctrl_valid
 *
 *		When set, the field mpdu_qos_control_field, mpdu_ht_control has valid
 *		information, For MPDUs without a QoS,HT control field, this field
 *		will not be set.
 *
 * frame_encryption_info_valid
 *
 *		When set, the encryption related info fields, like IV and
 *		PN are valid
 *		For MPDUs that are not encrypted, this will not be set.
 *
 * mpdu_fragment_number
 *
 *		Field only valid when Mpdu_sequence_control_valid is set
 *		AND Fragment_flag is set. The fragment number from the 802.11 header
 *
 * more_fragment_flag
 *
 *		The More Fragment bit setting from the MPDU header of the
 *		received frame
 *
 * fr_ds
 *
 *		Field only valid when Mpdu_frame_control_valid is set
 *		Set if the from DS bit is set in the frame control.
 *
 * to_ds
 *
 *		Field only valid when Mpdu_frame_control_valid is set
 *		Set if the to DS bit is set in the frame control.
 *
 * encrypted
 *
 *		Field only valid when Mpdu_frame_control_valid is set.
 *		Protected bit from the frame control.
 *
 * mpdu_retry
 *		Field only valid when Mpdu_frame_control_valid is set.
 *		Retry bit from the frame control.  Only valid when first_msdu is set
 *
 * mpdu_sequence_number
 *		Field only valid when Mpdu_sequence_control_valid is set.
 *
 *		The sequence number from the 802.11 header.
 * key_id
 *		The key ID octet from the IV.
 *		Field only valid when Frame_encryption_info_valid is set
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
 * raw_mpdu
 *		Set when no 802.11 to nwifi/ethernet hdr conversion is done
 *
 * mpdu_length
 *		MPDU length before decapsulation.
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
 *		entries within the max search count.
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
 *
 * fragment_flag
 *		Fragment indication
 *
 * order
 *		Set if the order bit in the frame control is set.  Only
 *		set when first_msdu is set.
 *
 * u_apsd_trigger
 *		U-APSD trigger frame
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
 * amsdu_present
 *		AMSDU present
 *
 * mpdu_frame_control_field
 *		Frame control field in header. Only valid when the field is marked valid.
 *
 * mpdu_duration_field
 *		Duration field in header. Only valid when the field is marked valid.
 *
 * mac_addr_adx
 *		MAC addresses in the received frame. Only valid when corresponding
 *		address valid bit is set
 *
 * mpdu_qos_control_field, mpdu_ht_control_field
 *		QoS/HT control fields from header. Valid only when corresponding fields
 *		are marked valid
 *
 * vdev_id
 *		Virtual device associated with this peer
 *		RXOLE uses this to determine intra-BSS routing.
 *
 * service_code
 *		Opaque service code between PPE and Wi-Fi
 *		This field gets passed on by REO to PPE in the EDMA descriptor
 *		('REO_TO_PPE_RING').
 *
 * priority_valid
 *		This field gets passed on by REO to PPE in the EDMA descriptor
 *		('REO_TO_PPE_RING').
 *
 * src_info
 *		Source (virtual) device/interface info. associated with
 *		this peer
 *		This field gets passed on by REO to PPE in the EDMA descriptor
 *		('REO_TO_PPE_RING').
 *
 * multi_link_addr_ad1_ad2_valid
 *		If set, Rx OLE shall convert Address1 and Address2 of received
 *		data frames to multi-link addresses during decapsulation to eth/nwifi
 *
 * multi_link_addr_ad1,ad2
 *		Multi-link receiver address1,2. Only valid when corresponding
 *		valid bit is set
 *
 * authorize_to_send_wds
 *		If not set, RXDMA shall perform error-routing for WDS packets
 *		as the sender is not authorized and might misuse WDS frame
 *		format to inject packets with arbitrary DA/SA.
 *
 */

enum rx_msdu_start_pkt_type {
	RX_MSDU_START_PKT_TYPE_11A,
	RX_MSDU_START_PKT_TYPE_11B,
	RX_MSDU_START_PKT_TYPE_11N,
	RX_MSDU_START_PKT_TYPE_11AC,
	RX_MSDU_START_PKT_TYPE_11AX,
	RX_MSDU_START_PKT_TYPE_11BA,
	RX_MSDU_START_PKT_TYPE_11BE,
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

#define RX_MSDU_END_64_TLV_SRC_LINK_ID		GENMASK(24, 22)

#define RX_MSDU_END_INFO0_RXPCU_MPDU_FITLER	GENMASK(1, 0)
#define RX_MSDU_END_INFO0_SW_FRAME_GRP_ID	GENMASK(8, 2)

#define RX_MSDU_END_INFO1_REPORTED_MPDU_LENGTH	GENMASK(13, 0)

#define RX_MSDU_END_INFO2_CCE_SUPER_RULE	GENMASK(13, 8)
#define RX_MSDU_END_INFO2_CCND_TRUNCATE		BIT(14)
#define RX_MSDU_END_INFO2_CCND_CCE_DIS		BIT(15)

#define RX_MSDU_END_INFO3_DA_OFFSET		GENMASK(5, 0)
#define RX_MSDU_END_INFO3_SA_OFFSET		GENMASK(11, 6)
#define RX_MSDU_END_INFO3_DA_OFFSET_VALID	BIT(12)
#define RX_MSDU_END_INFO3_SA_OFFSET_VALID	BIT(13)

#define RX_MSDU_END_INFO4_TCP_FLAG		GENMASK(8, 0)
#define RX_MSDU_END_INFO4_LRO_ELIGIBLE		BIT(9)

#define RX_MSDU_END_INFO5_SA_IDX_TIMEOUT	BIT(0)
#define RX_MSDU_END_INFO5_DA_IDX_TIMEOUT	BIT(1)
#define RX_MSDU_END_INFO5_TO_DS			BIT(2)
#define RX_MSDU_END_INFO5_TID			GENMASK(6, 3)
#define RX_MSDU_END_INFO5_SA_IS_VALID		BIT(7)
#define RX_MSDU_END_INFO5_DA_IS_VALID		BIT(8)
#define RX_MSDU_END_INFO5_DA_IS_MCBC		BIT(9)
#define RX_MSDU_END_INFO5_L3_HDR_PADDING	GENMASK(11, 10)
#define RX_MSDU_END_INFO5_FIRST_MSDU		BIT(12)
#define RX_MSDU_END_INFO5_LAST_MSDU		BIT(13)
#define RX_MSDU_END_INFO5_FROM_DS		BIT(14)
#define RX_MSDU_END_INFO5_IP_CHKSUM_FAIL_COPY	BIT(15)

#define RX_MSDU_END_INFO6_MSDU_DROP		BIT(0)
#define RX_MSDU_END_INFO6_REO_DEST_IND		GENMASK(5, 1)
#define RX_MSDU_END_INFO6_FLOW_IDX		GENMASK(25, 6)
#define RX_MSDU_END_INFO6_USE_PPE		BIT(26)
#define RX_MSDU_END_INFO6_MESH_STA		GENMASK(28, 27)
#define RX_MSDU_END_INFO6_VLAN_CTAG_STRIPPED	BIT(29)
#define RX_MSDU_END_INFO6_VLAN_STAG_STRIPPED	BIT(30)
#define RX_MSDU_END_INFO6_FRAGMENT_FLAG		BIT(31)

#define RX_MSDU_END_INFO7_AGGR_COUNT		GENMASK(7, 0)
#define RX_MSDU_END_INFO7_FLOW_AGGR_CONTN	BIT(8)
#define RX_MSDU_END_INFO7_FISA_TIMEOUT		BIT(9)

#define RX_MSDU_END_INFO7_TCPUDP_CSUM_FAIL_CPY	BIT(10)
#define RX_MSDU_END_INFO7_MSDU_LIMIT_ERROR	BIT(11)
#define RX_MSDU_END_INFO7_FLOW_IDX_TIMEOUT	BIT(12)
#define RX_MSDU_END_INFO7_FLOW_IDX_INVALID	BIT(13)
#define RX_MSDU_END_INFO7_CCE_MATCH		BIT(14)
#define RX_MSDU_END_INFO7_AMSDU_PARSER_ERR	BIT(15)

#define RX_MSDU_END_INFO8_KEY_ID		GENMASK(7, 0)

#define RX_MSDU_END_INFO9_SERVICE_CODE		GENMASK(14, 6)
#define RX_MSDU_END_INFO9_PRIORITY_VALID	BIT(15)
#define RX_MSDU_END_INFO9_INRA_BSS		BIT(16)
#define RX_MSDU_END_INFO9_DEST_CHIP_ID		GENMASK(18, 17)
#define RX_MSDU_END_INFO9_MCAST_ECHO		BIT(19)
#define RX_MSDU_END_INFO9_WDS_LEARN_EVENT	BIT(20)
#define RX_MSDU_END_INFO9_WDS_ROAM_EVENT	BIT(21)
#define RX_MSDU_END_INFO9_WDS_KEEP_ALIVE_EVENT	BIT(22)

#define RX_MSDU_END_INFO10_MSDU_LENGTH		GENMASK(13, 0)
#define RX_MSDU_END_INFO10_STBC			BIT(14)
#define RX_MSDU_END_INFO10_IPSEC_ESP		BIT(15)
#define RX_MSDU_END_INFO10_L3_OFFSET		GENMASK(22, 16)
#define RX_MSDU_END_INFO10_IPSEC_AH		BIT(23)
#define RX_MSDU_END_INFO10_L4_OFFSET		GENMASK(31, 24)

#define RX_MSDU_END_INFO11_MSDU_NUMBER		GENMASK(7, 0)
#define RX_MSDU_END_INFO11_DECAP_FORMAT		GENMASK(9, 8)
#define RX_MSDU_END_INFO11_IPV4			BIT(10)
#define RX_MSDU_END_INFO11_IPV6			BIT(11)
#define RX_MSDU_END_INFO11_TCP			BIT(12)
#define RX_MSDU_END_INFO11_UDP			BIT(13)
#define RX_MSDU_END_INFO11_IP_FRAG		BIT(14)
#define RX_MSDU_END_INFO11_TCP_ONLY_ACK		BIT(15)
#define RX_MSDU_END_INFO11_DA_IS_BCAST_MCAST	BIT(16)
#define RX_MSDU_END_INFO11_SEL_TOEPLITZ_HASH	GENMASK(18, 17)
#define RX_MSDU_END_INFO11_IP_FIXED_HDR_VALID	BIT(19)
#define RX_MSDU_END_INFO11_IP_EXTN_HDR_VALID	BIT(20)
#define RX_MSDU_END_INFO11_IP_TCP_UDP_HDR_VALID	BIT(21)
#define RX_MSDU_END_INFO11_MESH_CTRL_PRESENT	BIT(22)
#define RX_MSDU_END_INFO11_LDPC			BIT(23)
#define RX_MSDU_END_INFO11_IP4_IP6_NXT_HDR	GENMASK(31, 24)

#define RX_MSDU_END_INFO12_USER_RSSI		GENMASK(7, 0)
#define RX_MSDU_END_INFO12_PKT_TYPE		GENMASK(11, 8)
#define RX_MSDU_END_INFO12_SGI			GENMASK(13, 12)
#define RX_MSDU_END_INFO12_RATE_MCS		GENMASK(17, 14)
#define RX_MSDU_END_INFO12_RECV_BW		GENMASK(20, 18)
#define RX_MSDU_END_INFO12_RECEPTION_TYPE	GENMASK(23, 21)

#define RX_MSDU_END_INFO12_MIMO_SS_BITMAP	GENMASK(30, 24)
#define RX_MSDU_END_INFO12_MIMO_DONE_COPY	BIT(31)

#define RX_MSDU_END_INFO13_FIRST_MPDU		BIT(0)
#define RX_MSDU_END_INFO13_MCAST_BCAST		BIT(2)
#define RX_MSDU_END_INFO13_AST_IDX_NOT_FOUND	BIT(3)
#define RX_MSDU_END_INFO13_AST_IDX_TIMEDOUT	BIT(4)
#define RX_MSDU_END_INFO13_POWER_MGMT		BIT(5)
#define RX_MSDU_END_INFO13_NON_QOS		BIT(6)
#define RX_MSDU_END_INFO13_NULL_DATA		BIT(7)
#define RX_MSDU_END_INFO13_MGMT_TYPE		BIT(8)
#define RX_MSDU_END_INFO13_CTRL_TYPE		BIT(9)
#define RX_MSDU_END_INFO13_MORE_DATA		BIT(10)
#define RX_MSDU_END_INFO13_EOSP			BIT(11)
#define RX_MSDU_END_INFO13_A_MSDU_ERROR		BIT(12)
#define RX_MSDU_END_INFO13_ORDER		BIT(14)
#define RX_MSDU_END_INFO13_OVERFLOW_ERR		BIT(16)
#define RX_MSDU_END_INFO13_MSDU_LEN_ERR		BIT(17)
#define RX_MSDU_END_INFO13_TCP_UDP_CKSUM_FAIL	BIT(18)
#define RX_MSDU_END_INFO13_IP_CKSUM_FAIL	BIT(19)
#define RX_MSDU_END_INFO13_SA_IDX_INVALID	BIT(20)
#define RX_MSDU_END_INFO13_DA_IDX_INVALID	BIT(21)
#define RX_MSDU_END_INFO13_AMSDU_ADDR_MISMATCH	BIT(22)
#define RX_MSDU_END_INFO13_RX_IN_TX_DECRYPT_BYP	BIT(23)
#define RX_MSDU_END_INFO13_ENCRYPT_REQUIRED	BIT(24)
#define RX_MSDU_END_INFO13_DIRECTED		BIT(25)
#define RX_MSDU_END_INFO13_BUFFER_FRAGMENT	BIT(26)
#define RX_MSDU_END_INFO13_MPDU_LEN_ERR		BIT(27)
#define RX_MSDU_END_INFO13_TKIP_MIC_ERR		BIT(28)
#define RX_MSDU_END_INFO13_DECRYPT_ERR		BIT(29)
#define RX_MSDU_END_INFO13_UNDECRYPT_FRAME_ERR	BIT(30)
#define RX_MSDU_END_INFO13_FCS_ERR		BIT(31)

#define RX_MSDU_END_INFO13_WIFI_PARSER_ERR      BIT(15)

#define RX_MSDU_END_INFO14_DECRYPT_STATUS_CODE	GENMASK(12, 10)
#define RX_MSDU_END_INFO14_RX_BITMAP_NOT_UPDED	BIT(13)
#define RX_MSDU_END_INFO14_MSDU_DONE		BIT(31)

struct rx_msdu_end_qcn9274 {
	__le16 info0;
	__le16 phy_ppdu_id;
	__le16 ip_hdr_cksum;
	__le16 info1;
	__le16 info2;
	__le16 cumulative_l3_checksum;
	__le32 rule_indication0;
	__le32 ipv6_options_crc;
	__le16 info3;
	__le16 l3_type;
	__le32 rule_indication1;
	__le32 tcp_seq_num;
	__le32 tcp_ack_num;
	__le16 info4;
	__le16 window_size;
	__le16 sa_sw_peer_id;
	__le16 info5;
	__le16 sa_idx;
	__le16 da_idx_or_sw_peer_id;
	__le32 info6;
	__le32 fse_metadata;
	__le16 cce_metadata;
	__le16 tcp_udp_cksum;
	__le16 info7;
	__le16 cumulative_ip_length;
	__le32 info8;
	__le32 info9;
	__le32 info10;
	__le32 info11;
	__le16 vlan_ctag_ci;
	__le16 vlan_stag_ci;
	__le32 peer_meta_data;
	__le32 info12;
	__le32 flow_id_toeplitz;
	__le32 ppdu_start_timestamp_63_32;
	__le32 phy_meta_data;
	__le32 ppdu_start_timestamp_31_0;
	__le32 toeplitz_hash_2_or_4;
	__le16 res0;
	__le16 sa_15_0;
	__le32 sa_47_16;
	__le32 info13;
	__le32 info14;
} __packed;

#define QCN9274_MSDU_END_SELECT_MSDU_END_TAG				BIT(0)
#define QCN9274_MSDU_END_SELECT_INFO0_PHY_PPDUID_IP_HDR_CSUM_INFO1	BIT(1)
#define QCN9274_MSDU_END_SELECT_INFO2_CUMULATIVE_CSUM_RULE_IND_0	BIT(2)
#define QCN9274_MSDU_END_SELECT_IPV6_OP_CRC_INFO3_TYPE13		BIT(3)
#define QCN9274_MSDU_END_SELECT_RULE_IND_1_TCP_SEQ_NUM			BIT(4)
#define QCN9274_MSDU_END_SELECT_TCP_ACK_NUM_INFO4_WINDOW_SIZE		BIT(5)
#define QCN9274_MSDU_END_SELECT_SA_SW_PER_ID_INFO5_SA_DA_ID		BIT(6)
#define QCN9274_MSDU_END_SELECT_INFO6_FSE_METADATA			BIT(7)
#define QCN9274_MSDU_END_SELECT_CCE_MDATA_TCP_UDP_CSUM_INFO7_IP_LEN	BIT(8)
#define QCN9274_MSDU_END_SELECT_INFO8_INFO9				BIT(9)
#define QCN9274_MSDU_END_SELECT_INFO10_INFO11				BIT(10)
#define QCN9274_MSDU_END_SELECT_VLAN_CTAG_STAG_CI_PEER_MDATA		BIT(11)
#define QCN9274_MSDU_END_SELECT_INFO12_AND_FLOW_ID_TOEPLITZ		BIT(12)
#define QCN9274_MSDU_END_SELECT_PPDU_START_TS_63_32_PHY_MDATA		BIT(13)
#define QCN9274_MSDU_END_SELECT_PPDU_START_TS_31_0_TOEPLITZ_HASH_2_4	BIT(14)
#define QCN9274_MSDU_END_SELECT_RES0_SA_47_0				BIT(15)
#define QCN9274_MSDU_END_SELECT_INFO13_INFO14				BIT(16)

#define QCN9274_MSDU_END_WMASK (QCN9274_MSDU_END_SELECT_MSDU_END_TAG |	\
		QCN9274_MSDU_END_SELECT_SA_SW_PER_ID_INFO5_SA_DA_ID |	\
		QCN9274_MSDU_END_SELECT_INFO10_INFO11 |			\
		QCN9274_MSDU_END_SELECT_INFO12_AND_FLOW_ID_TOEPLITZ |	\
		QCN9274_MSDU_END_SELECT_PPDU_START_TS_63_32_PHY_MDATA |	\
		QCN9274_MSDU_END_SELECT_INFO13_INFO14)

/* The below rx_msdu_end_qcn9274_compact structure is tied with the mask value
 * QCN9274_MSDU_END_WMASK. If the mask value changes the structure will also
 * change.
 */

struct rx_msdu_end_qcn9274_compact {
	__le64 msdu_end_tag;
	__le16 sa_sw_peer_id;
	__le16 info5;
	__le16 sa_idx;
	__le16 da_idx_or_sw_peer_id;
	__le32 info10;
	__le32 info11;
	__le32 info12;
	__le32 flow_id_toeplitz;
	__le32 ppdu_start_timestamp_63_32;
	__le32 phy_meta_data;
	__le32 info13;
	__le32 info14;
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
 * reported_mpdu_length
 *		MPDU length before decapsulation. Only valid when first_msdu is
 *		set. This field is taken directly from the length field of the
 *		A-MPDU delimiter or the preamble length field for non-A-MPDU
 *		frames.
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
 * cumulative_l3_checksum
 *		FISA: IP header checksum including the total MSDU length
 *		that is part of this flow aggregated so far, reported if
 *		'RXOLE_R0_FISA_CTRL. CHKSUM_CUM_IP_LEN_EN' is set
 *
 * rule_indication
 *		Bitmap indicating which of rules have matched.
 *
 * ipv6_options_crc
 *		32 bit CRC computed out of  IP v6 extension headers.
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
 * sa_sw_peer_id
 *		sw_peer_id from the address search entry corresponding to the
 *		source address of the MSDU.
 *
 * sa_idx_timeout
 *		Indicates an unsuccessful MAC source address search due to the
 *		expiring of the search timer.
 *
 * da_idx_timeout
 *		Indicates an unsuccessful MAC destination address search due to
 *		the expiring of the search timer.
 *
 * to_ds
 *		Set if the to DS bit is set in the frame control.
 *
 * tid
 *		TID field in the QoS control field
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
 * fr_ds
 *		Set if the from DS bit is set in the frame control.
 *
 * ip_chksum_fail_copy
 *		Indicates that the computed checksum did not match the
 *		checksum in the IP header.
 *
 * sa_idx
 *		The offset in the address table which matches the MAC source
 *		address.
 *
 * da_idx_or_sw_peer_id
 *		Based on a register configuration in RXOLE, this field will
 *		contain:
 *		The offset in the address table which matches the MAC destination
 *		address
 *		OR:
 *		sw_peer_id from the address search entry corresponding to
 *		the destination address of the MSDU
 *
 * msdu_drop
 *		REO shall drop this MSDU and not forward it to any other ring.
 *
 *		The id of the reo exit ring where the msdu frame shall push
 *		after (MPDU level) reordering has finished. Values are defined
 *		in enum %HAL_RX_MSDU_DESC_REO_DEST_IND_.
 *
 * flow_idx
 *		Flow table index.
 *
 * use_ppe
 *		Indicates to RXDMA to ignore the REO_destination_indication
 *		and use a programmed value corresponding to the REO2PPE
 *		ring
 *
 * mesh_sta
 *		When set, this is a Mesh (11s) STA.
 *
 * vlan_ctag_stripped
 *		Set by RXOLE if it stripped 4-bytes of C-VLAN Tag from the
 *		packet
 *
 * vlan_stag_stripped
 *		Set by RXOLE if it stripped 4-bytes of S-VLAN Tag from the
 *		packet
 *
 * fragment_flag
 *		Indicates that this is an 802.11 fragment frame.  This is
 *		set when either the more_frag bit is set in the frame control
 *		or the fragment number is not zero.  Only set when first_msdu
 *		is set.
 *
 * fse_metadata
 *		FSE related meta data.
 *
 * cce_metadata
 *		CCE related meta data.
 *
 * tcp_udp_chksum
 *		The value of the computed TCP/UDP checksum.  A mode bit
 *		selects whether this checksum is the full checksum or the
 *		partial checksum which does not include the pseudo header.
 *
 * aggregation_count
 *		Number of MSDU's aggregated so far
 *
 * flow_aggregation_continuation
 *		To indicate that this MSDU can be aggregated with
 *		the previous packet with the same flow id
 *
 * fisa_timeout
 *		To indicate that the aggregation has restarted for
 *		this flow due to timeout
 *
 * tcp_udp_chksum_fail
 *		Indicates that the computed checksum (tcp_udp_chksum) did
 *		not match the checksum in the TCP/UDP header.
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
 * cce_match
 *		Indicates that this status has a corresponding MSDU that
 *		requires FW processing. The OLE will have classification
 *		ring mask registers which will indicate the ring(s) for
 *		packets and descriptors which need FW attention.
 *
 * amsdu_parser_error
 *		A-MSDU could not be properly de-agregated.
 *
 * cumulative_ip_length
 *		Total MSDU length that is part of this flow aggregated
 *		so far
 *
 * key_id
 *		The key ID octet from the IV. Only valid when first_msdu is set.
 *
 * service_code
 *		Opaque service code between PPE and Wi-Fi
 *
 * priority_valid
 *		This field gets passed on by REO to PPE in the EDMA descriptor
 *
 * intra_bss
 *		This packet needs intra-BSS routing by SW as the 'vdev_id'
 *		for the destination is the same as 'vdev_id' (from 'RX_MPDU_PCU_START')
 *		that this MSDU was got in.
 *
 * dest_chip_id
 *		If intra_bss is set, copied by RXOLE from 'ADDR_SEARCH_ENTRY'
 *		to support intra-BSS routing with multi-chip multi-link
 *		operation. This indicates into which chip's TCL the packet should be
 *		queueued
 *
 * multicast_echo
 *		If set, this packet is a multicast echo, i.e. the DA is
 *		multicast and Rx OLE SA search with mcast_echo_check = 1
 *		passed. RXDMA should release such packets to WBM.
 *
 * wds_learning_event
 *		If set, this packet has an SA search failure with WDS learning
 *		enabled for the peer. RXOLE should route this TLV to the
 *		RXDMA0 status ring to notify FW.
 *
 * wds_roaming_event
 *		If set, this packet's SA 'Sw_peer_id' mismatches the 'Sw_peer_id'
 *		of the peer through which the packet was got, indicating
 *		the SA node has roamed. RXOLE should route this TLV to
 *		the RXDMA0 status ring to notify FW.
 *
 * wds_keep_alive_event
 *		If set, the AST timestamp for this packet's SA is older
 *		than the current timestamp by more than a threshold programmed
 *		in RXOLE. RXOLE should route this TLV to the RXDMA0 status
 *		ring to notify FW to keep the AST entry for the SA alive.
 *
 * msdu_length
 *		MSDU length in bytes after decapsulation.
 *		This field is still valid for MPDU frames without A-MSDU.
 *		It still represents MSDU length after decapsulation
 *
 * stbc
 *		When set, use STBC transmission rates.
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
 *		L4 offset in bytes from the start of RX_HEADER (only valid
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
 *
 * vlan_ctag_ci
 *		2 bytes of C-VLAN Tag Control Information from WHO_L2_LLC
 *
 * vlan_stag_ci
 *		2 bytes of S-VLAN Tag Control Information from WHO_L2_LLC
 *		in case of double VLAN
 *
 * peer_meta_data
 *		Meta data that SW has programmed in the Peer table entry
 *		of the transmitting STA.
 *
 * user_rssi
 *		RSSI for this user
 *
 * pkt_type
 *		Values are defined in enum %RX_MSDU_START_PKT_TYPE_*.
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
 * msdu_done_copy
 *		If set indicates that the RX packet data, RX header data,
 *		RX PPDU start descriptor, RX MPDU start/end descriptor,
 *		RX MSDU start/end descriptors and RX Attention descriptor
 *		are all valid.  This bit is in the last 64-bit of the descriptor
 *		expected to be subscribed in future hardware.
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
 * ppdu_start_timestamp
 *		Timestamp that indicates when the PPDU that contained this MPDU
 *		started on the medium.
 *
 * phy_meta_data
 *		SW programmed Meta data provided by the PHY. Can be used for SW
 *		to indicate the channel the device is on.
 *
 * toeplitz_hash_2_or_4
 *		Controlled by multiple RxOLE registers for TCP/UDP over
 *		IPv4/IPv6 - Either, Toeplitz hash computed over 2-tuple
 *		IPv4 or IPv6 src/dest addresses is reported; or, Toeplitz
 *		hash computed over 4-tuple IPv4 or IPv6 src/dest addresses
 *		and src/dest ports is reported. The Flow_id_toeplitz hash
 *		can also be reported here. Usually the hash reported here
 *		is the one used for hash-based REO routing (see use_flow_id_toeplitz_clfy
 *		in 'RXPT_CLASSIFY_INFO').
 *
 * sa
 *		Source MAC address
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
 *		entries within the max search count.
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
 * order
 *		Set if the order bit in the frame control is set.  Only
 *		set when first_msdu is set.
 *
 * wifi_parser_error
 *		Indicates that the WiFi frame has one of the following errors
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
 * amsdu_addr_mismatch
 *		Indicates that an A-MSDU with 'from DS = 0' had an SA mismatching
 *		TA or an A-MDU with 'to DS = 0' had a DA mismatching RA
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
 * decrypt_status_code
 *		Field provides insight into the decryption performed. Values
 *		are defined in enum %RX_DESC_DECRYPT_STATUS_CODE_*.
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
 *
 */

struct hal_rx_desc_qcn9274 {
	struct rx_msdu_end_qcn9274 msdu_end;
	struct rx_mpdu_start_qcn9274 mpdu_start;
	u8 msdu_payload[];
} __packed;

struct hal_rx_desc_qcn9274_compact {
	struct rx_msdu_end_qcn9274_compact msdu_end;
	struct rx_mpdu_start_qcn9274_compact mpdu_start;
	u8 msdu_payload[];
} __packed;

#define RX_BE_PADDING0_BYTES 8
#define RX_BE_PADDING1_BYTES 8

#define HAL_RX_BE_PKT_HDR_TLV_LEN		112

struct rx_pkt_hdr_tlv {
	__le64 tag;
	__le64 phy_ppdu_id;
	u8 rx_pkt_hdr[HAL_RX_BE_PKT_HDR_TLV_LEN];
};

struct hal_rx_desc_wcn7850 {
	__le64 msdu_end_tag;
	struct rx_msdu_end_qcn9274 msdu_end;
	u8 rx_padding0[RX_BE_PADDING0_BYTES];
	__le64 mpdu_start_tag;
	struct rx_mpdu_start_qcn9274 mpdu_start;
	struct rx_pkt_hdr_tlv	 pkt_hdr_tlv;
	u8 msdu_payload[];
};

struct hal_rx_desc {
	union {
		struct hal_rx_desc_qcn9274 qcn9274;
		struct hal_rx_desc_qcn9274_compact qcn9274_compact;
		struct hal_rx_desc_wcn7850 wcn7850;
	} u;
} __packed;

#define MAX_USER_POS 8
#define MAX_MU_GROUP_ID 64
#define MAX_MU_GROUP_SHOW 16
#define MAX_MU_GROUP_LENGTH (6 * MAX_MU_GROUP_SHOW)

#endif /* ATH12K_RX_DESC_H */
