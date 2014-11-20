/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _RX_DESC_H_
#define _RX_DESC_H_

enum rx_attention_flags {
	RX_ATTENTION_FLAGS_FIRST_MPDU          = 1 << 0,
	RX_ATTENTION_FLAGS_LAST_MPDU           = 1 << 1,
	RX_ATTENTION_FLAGS_MCAST_BCAST         = 1 << 2,
	RX_ATTENTION_FLAGS_PEER_IDX_INVALID    = 1 << 3,
	RX_ATTENTION_FLAGS_PEER_IDX_TIMEOUT    = 1 << 4,
	RX_ATTENTION_FLAGS_POWER_MGMT          = 1 << 5,
	RX_ATTENTION_FLAGS_NON_QOS             = 1 << 6,
	RX_ATTENTION_FLAGS_NULL_DATA           = 1 << 7,
	RX_ATTENTION_FLAGS_MGMT_TYPE           = 1 << 8,
	RX_ATTENTION_FLAGS_CTRL_TYPE           = 1 << 9,
	RX_ATTENTION_FLAGS_MORE_DATA           = 1 << 10,
	RX_ATTENTION_FLAGS_EOSP                = 1 << 11,
	RX_ATTENTION_FLAGS_U_APSD_TRIGGER      = 1 << 12,
	RX_ATTENTION_FLAGS_FRAGMENT            = 1 << 13,
	RX_ATTENTION_FLAGS_ORDER               = 1 << 14,
	RX_ATTENTION_FLAGS_CLASSIFICATION      = 1 << 15,
	RX_ATTENTION_FLAGS_OVERFLOW_ERR        = 1 << 16,
	RX_ATTENTION_FLAGS_MSDU_LENGTH_ERR     = 1 << 17,
	RX_ATTENTION_FLAGS_TCP_UDP_CHKSUM_FAIL = 1 << 18,
	RX_ATTENTION_FLAGS_IP_CHKSUM_FAIL      = 1 << 19,
	RX_ATTENTION_FLAGS_SA_IDX_INVALID      = 1 << 20,
	RX_ATTENTION_FLAGS_DA_IDX_INVALID      = 1 << 21,
	RX_ATTENTION_FLAGS_SA_IDX_TIMEOUT      = 1 << 22,
	RX_ATTENTION_FLAGS_DA_IDX_TIMEOUT      = 1 << 23,
	RX_ATTENTION_FLAGS_ENCRYPT_REQUIRED    = 1 << 24,
	RX_ATTENTION_FLAGS_DIRECTED            = 1 << 25,
	RX_ATTENTION_FLAGS_BUFFER_FRAGMENT     = 1 << 26,
	RX_ATTENTION_FLAGS_MPDU_LENGTH_ERR     = 1 << 27,
	RX_ATTENTION_FLAGS_TKIP_MIC_ERR        = 1 << 28,
	RX_ATTENTION_FLAGS_DECRYPT_ERR         = 1 << 29,
	RX_ATTENTION_FLAGS_FCS_ERR             = 1 << 30,
	RX_ATTENTION_FLAGS_MSDU_DONE           = 1 << 31,
};

struct rx_attention {
	__le32 flags; /* %RX_ATTENTION_FLAGS_ */
} __packed;

/*
 * first_mpdu
 *		Indicates the first MSDU of the PPDU.  If both first_mpdu
 *		and last_mpdu are set in the MSDU then this is a not an
 *		A-MPDU frame but a stand alone MPDU.  Interior MPDU in an
 *		A-MPDU shall have both first_mpdu and last_mpdu bits set to
 *		0.  The PPDU start status will only be valid when this bit
 *		is set.
 *
 * last_mpdu
 *		Indicates the last MSDU of the last MPDU of the PPDU.  The
 *		PPDU end status will only be valid when this bit is set.
 *
 * mcast_bcast
 *		Multicast / broadcast indicator.  Only set when the MAC
 *		address 1 bit 0 is set indicating mcast/bcast and the BSSID
 *		matches one of the 4 BSSID registers. Only set when
 *		first_msdu is set.
 *
 * peer_idx_invalid
 *		Indicates no matching entries within the the max search
 *		count.  Only set when first_msdu is set.
 *
 * peer_idx_timeout
 *		Indicates an unsuccessful search for the peer index due to
 *		timeout.  Only set when first_msdu is set.
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
 * u_apsd_trigger
 *		Set if packet is U-APSD trigger.  Key table will have bits
 *		per TID to indicate U-APSD trigger.
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
 * classification
 *		Indicates that this status has a corresponding MSDU that
 *		requires FW processing.  The OLE will have classification
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
 * sa_idx_timeout
 *		Indicates an unsuccessful search for the source MAC address
 *		due to the expiring of the search timer.
 *
 * da_idx_timeout
 *		Indicates an unsuccessful search for the destination MAC
 *		address due to the expiring of the search timer.
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
 * msdu_done
 *		If set indicates that the RX packet data, RX header data, RX
 *		PPDU start descriptor, RX MPDU start/end descriptor, RX MSDU
 *		start/end descriptors and RX Attention descriptor are all
 *		valid.  This bit must be in the last octet of the
 *		descriptor.
 */

struct rx_frag_info {
	u8 ring0_more_count;
	u8 ring1_more_count;
	u8 ring2_more_count;
	u8 ring3_more_count;
} __packed;

/*
 * ring0_more_count
 *		Indicates the number of more buffers associated with RX DMA
 *		ring 0.  Field is filled in by the RX_DMA.
 *
 * ring1_more_count
 *		Indicates the number of more buffers associated with RX DMA
 *		ring 1. Field is filled in by the RX_DMA.
 *
 * ring2_more_count
 *		Indicates the number of more buffers associated with RX DMA
 *		ring 2. Field is filled in by the RX_DMA.
 *
 * ring3_more_count
 *		Indicates the number of more buffers associated with RX DMA
 *		ring 3. Field is filled in by the RX_DMA.
 */

enum htt_rx_mpdu_encrypt_type {
	HTT_RX_MPDU_ENCRYPT_WEP40            = 0,
	HTT_RX_MPDU_ENCRYPT_WEP104           = 1,
	HTT_RX_MPDU_ENCRYPT_TKIP_WITHOUT_MIC = 2,
	HTT_RX_MPDU_ENCRYPT_WEP128           = 3,
	HTT_RX_MPDU_ENCRYPT_TKIP_WPA         = 4,
	HTT_RX_MPDU_ENCRYPT_WAPI             = 5,
	HTT_RX_MPDU_ENCRYPT_AES_CCM_WPA2     = 6,
	HTT_RX_MPDU_ENCRYPT_NONE             = 7,
};

#define RX_MPDU_START_INFO0_PEER_IDX_MASK     0x000007ff
#define RX_MPDU_START_INFO0_PEER_IDX_LSB      0
#define RX_MPDU_START_INFO0_SEQ_NUM_MASK      0x0fff0000
#define RX_MPDU_START_INFO0_SEQ_NUM_LSB       16
#define RX_MPDU_START_INFO0_ENCRYPT_TYPE_MASK 0xf0000000
#define RX_MPDU_START_INFO0_ENCRYPT_TYPE_LSB  28
#define RX_MPDU_START_INFO0_FROM_DS           (1 << 11)
#define RX_MPDU_START_INFO0_TO_DS             (1 << 12)
#define RX_MPDU_START_INFO0_ENCRYPTED         (1 << 13)
#define RX_MPDU_START_INFO0_RETRY             (1 << 14)
#define RX_MPDU_START_INFO0_TXBF_H_INFO       (1 << 15)

#define RX_MPDU_START_INFO1_TID_MASK 0xf0000000
#define RX_MPDU_START_INFO1_TID_LSB  28
#define RX_MPDU_START_INFO1_DIRECTED (1 << 16)

struct rx_mpdu_start {
	__le32 info0;
	union {
		struct {
			__le32 pn31_0;
			__le32 info1; /* %RX_MPDU_START_INFO1_ */
		} __packed;
		struct {
			u8 pn[6];
		} __packed;
	} __packed;
} __packed;

/*
 * peer_idx
 *		The index of the address search table which associated with
 *		the peer table entry corresponding to this MPDU.  Only valid
 *		when first_msdu is set.
 *
 * fr_ds
 *		Set if the from DS bit is set in the frame control.  Only
 *		valid when first_msdu is set.
 *
 * to_ds
 *		Set if the to DS bit is set in the frame control.  Only
 *		valid when first_msdu is set.
 *
 * encrypted
 *		Protected bit from the frame control.  Only valid when
 *		first_msdu is set.
 *
 * retry
 *		Retry bit from the frame control.  Only valid when
 *		first_msdu is set.
 *
 * txbf_h_info
 *		The MPDU data will contain H information.  Primarily used
 *		for debug.
 *
 * seq_num
 *		The sequence number from the 802.11 header.  Only valid when
 *		first_msdu is set.
 *
 * encrypt_type
 *		Indicates type of decrypt cipher used (as defined in the
 *		peer table)
 *		0: WEP40
 *		1: WEP104
 *		2: TKIP without MIC
 *		3: WEP128
 *		4: TKIP (WPA)
 *		5: WAPI
 *		6: AES-CCM (WPA2)
 *		7: No cipher
 *		Only valid when first_msdu_is set
 *
 * pn_31_0
 *		Bits [31:0] of the PN number extracted from the IV field
 *		WEP: IV = {key_id_octet, pn2, pn1, pn0}.  Only pn[23:0] is
 *		valid.
 *		TKIP: IV = {pn5, pn4, pn3, pn2, key_id_octet, pn0,
 *		WEPSeed[1], pn1}.  Only pn[47:0] is valid.
 *		AES-CCM: IV = {pn5, pn4, pn3, pn2, key_id_octet, 0x0, pn1,
 *		pn0}.  Only pn[47:0] is valid.
 *		WAPI: IV = {key_id_octet, 0x0, pn15, pn14, pn13, pn12, pn11,
 *		pn10, pn9, pn8, pn7, pn6, pn5, pn4, pn3, pn2, pn1, pn0}.
 *		The ext_wapi_pn[127:48] in the rx_msdu_misc descriptor and
 *		pn[47:0] are valid.
 *		Only valid when first_msdu is set.
 *
 * pn_47_32
 *		Bits [47:32] of the PN number.   See description for
 *		pn_31_0.  The remaining PN fields are in the rx_msdu_end
 *		descriptor
 *
 * pn
 *		Use this field to access the pn without worrying about
 *		byte-order and bitmasking/bitshifting.
 *
 * directed
 *		See definition in RX attention descriptor
 *
 * reserved_2
 *		Reserved: HW should fill with zero.  FW should ignore.
 *
 * tid
 *		The TID field in the QoS control field
 */

#define RX_MPDU_END_INFO0_RESERVED_0_MASK     0x00001fff
#define RX_MPDU_END_INFO0_RESERVED_0_LSB      0
#define RX_MPDU_END_INFO0_POST_DELIM_CNT_MASK 0x0fff0000
#define RX_MPDU_END_INFO0_POST_DELIM_CNT_LSB  16
#define RX_MPDU_END_INFO0_OVERFLOW_ERR        (1 << 13)
#define RX_MPDU_END_INFO0_LAST_MPDU           (1 << 14)
#define RX_MPDU_END_INFO0_POST_DELIM_ERR      (1 << 15)
#define RX_MPDU_END_INFO0_MPDU_LENGTH_ERR     (1 << 28)
#define RX_MPDU_END_INFO0_TKIP_MIC_ERR        (1 << 29)
#define RX_MPDU_END_INFO0_DECRYPT_ERR         (1 << 30)
#define RX_MPDU_END_INFO0_FCS_ERR             (1 << 31)

struct rx_mpdu_end {
	__le32 info0;
} __packed;

/*
 * reserved_0
 *		Reserved
 *
 * overflow_err
 *		PCU Receive FIFO does not have enough space to store the
 *		full receive packet.  Enough space is reserved in the
 *		receive FIFO for the status is written.  This MPDU remaining
 *		packets in the PPDU will be filtered and no Ack response
 *		will be transmitted.
 *
 * last_mpdu
 *		Indicates that this is the last MPDU of a PPDU.
 *
 * post_delim_err
 *		Indicates that a delimiter FCS error occurred after this
 *		MPDU before the next MPDU.  Only valid when last_msdu is
 *		set.
 *
 * post_delim_cnt
 *		Count of the delimiters after this MPDU.  This requires the
 *		last MPDU to be held until all the EOF descriptors have been
 *		received.  This may be inefficient in the future when
 *		ML-MIMO is used.  Only valid when last_mpdu is set.
 *
 * mpdu_length_err
 *		See definition in RX attention descriptor
 *
 * tkip_mic_err
 *		See definition in RX attention descriptor
 *
 * decrypt_err
 *		See definition in RX attention descriptor
 *
 * fcs_err
 *		See definition in RX attention descriptor
 */

#define RX_MSDU_START_INFO0_MSDU_LENGTH_MASK    0x00003fff
#define RX_MSDU_START_INFO0_MSDU_LENGTH_LSB     0
#define RX_MSDU_START_INFO0_IP_OFFSET_MASK      0x000fc000
#define RX_MSDU_START_INFO0_IP_OFFSET_LSB       14
#define RX_MSDU_START_INFO0_RING_MASK_MASK      0x00f00000
#define RX_MSDU_START_INFO0_RING_MASK_LSB       20
#define RX_MSDU_START_INFO0_TCP_UDP_OFFSET_MASK 0x7f000000
#define RX_MSDU_START_INFO0_TCP_UDP_OFFSET_LSB  24

#define RX_MSDU_START_INFO1_MSDU_NUMBER_MASK    0x000000ff
#define RX_MSDU_START_INFO1_MSDU_NUMBER_LSB     0
#define RX_MSDU_START_INFO1_DECAP_FORMAT_MASK   0x00000300
#define RX_MSDU_START_INFO1_DECAP_FORMAT_LSB    8
#define RX_MSDU_START_INFO1_SA_IDX_MASK         0x07ff0000
#define RX_MSDU_START_INFO1_SA_IDX_LSB          16
#define RX_MSDU_START_INFO1_IPV4_PROTO          (1 << 10)
#define RX_MSDU_START_INFO1_IPV6_PROTO          (1 << 11)
#define RX_MSDU_START_INFO1_TCP_PROTO           (1 << 12)
#define RX_MSDU_START_INFO1_UDP_PROTO           (1 << 13)
#define RX_MSDU_START_INFO1_IP_FRAG             (1 << 14)
#define RX_MSDU_START_INFO1_TCP_ONLY_ACK        (1 << 15)

/* The decapped header (rx_hdr_status) contains the following:
 *  a) 802.11 header
 *  [padding to 4 bytes]
 *  b) HW crypto parameter
 *     - 0 bytes for no security
 *     - 4 bytes for WEP
 *     - 8 bytes for TKIP, AES
 *  [padding to 4 bytes]
 *  c) A-MSDU subframe header (14 bytes) if appliable
 *  d) LLC/SNAP (RFC1042, 8 bytes)
 *
 * In case of A-MSDU only first frame in sequence contains (a) and (b). */
enum rx_msdu_decap_format {
	RX_MSDU_DECAP_RAW = 0,

	/* Note: QoS frames are reported as non-QoS. The rx_hdr_status in
	 * htt_rx_desc contains the original decapped 802.11 header. */
	RX_MSDU_DECAP_NATIVE_WIFI = 1,

	/* Payload contains an ethernet header (struct ethhdr). */
	RX_MSDU_DECAP_ETHERNET2_DIX = 2,

	/* Payload contains two 48-bit addresses and 2-byte length (14 bytes
	 * total), followed by an RFC1042 header (8 bytes). */
	RX_MSDU_DECAP_8023_SNAP_LLC = 3
};

struct rx_msdu_start {
	__le32 info0; /* %RX_MSDU_START_INFO0_ */
	__le32 flow_id_crc;
	__le32 info1; /* %RX_MSDU_START_INFO1_ */
} __packed;

/*
 * msdu_length
 *		MSDU length in bytes after decapsulation.  This field is
 *		still valid for MPDU frames without A-MSDU.  It still
 *		represents MSDU length after decapsulation
 *
 * ip_offset
 *		Indicates the IP offset in bytes from the start of the
 *		packet after decapsulation.  Only valid if ipv4_proto or
 *		ipv6_proto is set.
 *
 * ring_mask
 *		Indicates the destination RX rings for this MSDU.
 *
 * tcp_udp_offset
 *		Indicates the offset in bytes to the start of TCP or UDP
 *		header from the start of the IP header after decapsulation.
 *		Only valid if tcp_prot or udp_prot is set.  The value 0
 *		indicates that the offset is longer than 127 bytes.
 *
 * reserved_0c
 *		Reserved: HW should fill with zero.  FW should ignore.
 *
 * flow_id_crc
 *		The flow_id_crc runs CRC32 on the following information:
 *		IPv4 option: dest_addr[31:0], src_addr [31:0], {24'b0,
 *		protocol[7:0]}.
 *		IPv6 option: dest_addr[127:0], src_addr [127:0], {24'b0,
 *		next_header[7:0]}
 *		UDP case: sort_port[15:0], dest_port[15:0]
 *		TCP case: sort_port[15:0], dest_port[15:0],
 *		{header_length[3:0], 6'b0, flags[5:0], window_size[15:0]},
 *		{16'b0, urgent_ptr[15:0]}, all options except 32-bit
 *		timestamp.
 *
 * msdu_number
 *		Indicates the MSDU number within a MPDU.  This value is
 *		reset to zero at the start of each MPDU.  If the number of
 *		MSDU exceeds 255 this number will wrap using modulo 256.
 *
 * decap_format
 *		Indicates the format after decapsulation:
 *		0: RAW: No decapsulation
 *		1: Native WiFi
 *		2: Ethernet 2 (DIX)
 *		3: 802.3 (SNAP/LLC)
 *
 * ipv4_proto
 *		Set if L2 layer indicates IPv4 protocol.
 *
 * ipv6_proto
 *		Set if L2 layer indicates IPv6 protocol.
 *
 * tcp_proto
 *		Set if the ipv4_proto or ipv6_proto are set and the IP
 *		protocol indicates TCP.
 *
 * udp_proto
 *		Set if the ipv4_proto or ipv6_proto are set and the IP
 *			protocol indicates UDP.
 *
 * ip_frag
 *		Indicates that either the IP More frag bit is set or IP frag
 *		number is non-zero.  If set indicates that this is a
 *		fragmented IP packet.
 *
 * tcp_only_ack
 *		Set if only the TCP Ack bit is set in the TCP flags and if
 *		the TCP payload is 0.
 *
 * sa_idx
 *		The offset in the address table which matches the MAC source
 *		address.
 *
 * reserved_2b
 *		Reserved: HW should fill with zero.  FW should ignore.
 */

#define RX_MSDU_END_INFO0_REPORTED_MPDU_LENGTH_MASK 0x00003fff
#define RX_MSDU_END_INFO0_REPORTED_MPDU_LENGTH_LSB  0
#define RX_MSDU_END_INFO0_FIRST_MSDU                (1 << 14)
#define RX_MSDU_END_INFO0_LAST_MSDU                 (1 << 15)
#define RX_MSDU_END_INFO0_PRE_DELIM_ERR             (1 << 30)
#define RX_MSDU_END_INFO0_RESERVED_3B               (1 << 31)

struct rx_msdu_end {
	__le16 ip_hdr_cksum;
	__le16 tcp_hdr_cksum;
	u8 key_id_octet;
	u8 classification_filter;
	u8 wapi_pn[10];
	__le32 info0;
} __packed;

/*
 *ip_hdr_chksum
 *		This can include the IP header checksum or the pseudo header
 *		checksum used by TCP/UDP checksum.
 *
 *tcp_udp_chksum
 *		The value of the computed TCP/UDP checksum.  A mode bit
 *		selects whether this checksum is the full checksum or the
 *		partial checksum which does not include the pseudo header.
 *
 *key_id_octet
 *		The key ID octet from the IV.  Only valid when first_msdu is
 *		set.
 *
 *classification_filter
 *		Indicates the number classification filter rule
 *
 *ext_wapi_pn_63_48
 *		Extension PN (packet number) which is only used by WAPI.
 *		This corresponds to WAPI PN bits [63:48] (pn6 and pn7).  The
 *		WAPI PN bits [63:0] are in the pn field of the rx_mpdu_start
 *		descriptor.
 *
 *ext_wapi_pn_95_64
 *		Extension PN (packet number) which is only used by WAPI.
 *		This corresponds to WAPI PN bits [95:64] (pn8, pn9, pn10 and
 *		pn11).
 *
 *ext_wapi_pn_127_96
 *		Extension PN (packet number) which is only used by WAPI.
 *		This corresponds to WAPI PN bits [127:96] (pn12, pn13, pn14,
 *		pn15).
 *
 *reported_mpdu_length
 *		MPDU length before decapsulation.  Only valid when
 *		first_msdu is set.  This field is taken directly from the
 *		length field of the A-MPDU delimiter or the preamble length
 *		field for non-A-MPDU frames.
 *
 *first_msdu
 *		Indicates the first MSDU of A-MSDU.  If both first_msdu and
 *		last_msdu are set in the MSDU then this is a non-aggregated
 *		MSDU frame: normal MPDU.  Interior MSDU in an A-MSDU shall
 *		have both first_mpdu and last_mpdu bits set to 0.
 *
 *last_msdu
 *		Indicates the last MSDU of the A-MSDU.  MPDU end status is
 *		only valid when last_msdu is set.
 *
 *reserved_3a
 *		Reserved: HW should fill with zero.  FW should ignore.
 *
 *pre_delim_err
 *		Indicates that the first delimiter had a FCS failure.  Only
 *		valid when first_mpdu and first_msdu are set.
 *
 *reserved_3b
 *		Reserved: HW should fill with zero.  FW should ignore.
 */

#define RX_PPDU_START_SIG_RATE_SELECT_OFDM 0
#define RX_PPDU_START_SIG_RATE_SELECT_CCK  1

#define RX_PPDU_START_SIG_RATE_OFDM_48 0
#define RX_PPDU_START_SIG_RATE_OFDM_24 1
#define RX_PPDU_START_SIG_RATE_OFDM_12 2
#define RX_PPDU_START_SIG_RATE_OFDM_6  3
#define RX_PPDU_START_SIG_RATE_OFDM_54 4
#define RX_PPDU_START_SIG_RATE_OFDM_36 5
#define RX_PPDU_START_SIG_RATE_OFDM_18 6
#define RX_PPDU_START_SIG_RATE_OFDM_9  7

#define RX_PPDU_START_SIG_RATE_CCK_LP_11  0
#define RX_PPDU_START_SIG_RATE_CCK_LP_5_5 1
#define RX_PPDU_START_SIG_RATE_CCK_LP_2   2
#define RX_PPDU_START_SIG_RATE_CCK_LP_1   3
#define RX_PPDU_START_SIG_RATE_CCK_SP_11  4
#define RX_PPDU_START_SIG_RATE_CCK_SP_5_5 5
#define RX_PPDU_START_SIG_RATE_CCK_SP_2   6

#define HTT_RX_PPDU_START_PREAMBLE_LEGACY        0x04
#define HTT_RX_PPDU_START_PREAMBLE_HT            0x08
#define HTT_RX_PPDU_START_PREAMBLE_HT_WITH_TXBF  0x09
#define HTT_RX_PPDU_START_PREAMBLE_VHT           0x0C
#define HTT_RX_PPDU_START_PREAMBLE_VHT_WITH_TXBF 0x0D

#define RX_PPDU_START_INFO0_IS_GREENFIELD (1 << 0)

#define RX_PPDU_START_INFO1_L_SIG_RATE_MASK    0x0000000f
#define RX_PPDU_START_INFO1_L_SIG_RATE_LSB     0
#define RX_PPDU_START_INFO1_L_SIG_LENGTH_MASK  0x0001ffe0
#define RX_PPDU_START_INFO1_L_SIG_LENGTH_LSB   5
#define RX_PPDU_START_INFO1_L_SIG_TAIL_MASK    0x00fc0000
#define RX_PPDU_START_INFO1_L_SIG_TAIL_LSB     18
#define RX_PPDU_START_INFO1_PREAMBLE_TYPE_MASK 0xff000000
#define RX_PPDU_START_INFO1_PREAMBLE_TYPE_LSB  24
#define RX_PPDU_START_INFO1_L_SIG_RATE_SELECT  (1 << 4)
#define RX_PPDU_START_INFO1_L_SIG_PARITY       (1 << 17)

#define RX_PPDU_START_INFO2_HT_SIG_VHT_SIG_A_1_MASK 0x00ffffff
#define RX_PPDU_START_INFO2_HT_SIG_VHT_SIG_A_1_LSB  0

#define RX_PPDU_START_INFO3_HT_SIG_VHT_SIG_A_2_MASK 0x00ffffff
#define RX_PPDU_START_INFO3_HT_SIG_VHT_SIG_A_2_LSB  0
#define RX_PPDU_START_INFO3_TXBF_H_INFO             (1 << 24)

#define RX_PPDU_START_INFO4_VHT_SIG_B_MASK 0x1fffffff
#define RX_PPDU_START_INFO4_VHT_SIG_B_LSB  0

#define RX_PPDU_START_INFO5_SERVICE_MASK 0x0000ffff
#define RX_PPDU_START_INFO5_SERVICE_LSB  0

struct rx_ppdu_start {
	struct {
		u8 pri20_mhz;
		u8 ext20_mhz;
		u8 ext40_mhz;
		u8 ext80_mhz;
	} rssi_chains[4];
	u8 rssi_comb;
	__le16 rsvd0;
	u8 info0; /* %RX_PPDU_START_INFO0_ */
	__le32 info1; /* %RX_PPDU_START_INFO1_ */
	__le32 info2; /* %RX_PPDU_START_INFO2_ */
	__le32 info3; /* %RX_PPDU_START_INFO3_ */
	__le32 info4; /* %RX_PPDU_START_INFO4_ */
	__le32 info5; /* %RX_PPDU_START_INFO5_ */
} __packed;

/*
 * rssi_chain0_pri20
 *		RSSI of RX PPDU on chain 0 of primary 20 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain0_sec20
 *		RSSI of RX PPDU on chain 0 of secondary 20 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain0_sec40
 *		RSSI of RX PPDU on chain 0 of secondary 40 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain0_sec80
 *		RSSI of RX PPDU on chain 0 of secondary 80 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain1_pri20
 *		RSSI of RX PPDU on chain 1 of primary 20 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain1_sec20
 *		RSSI of RX PPDU on chain 1 of secondary 20 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain1_sec40
 *		RSSI of RX PPDU on chain 1 of secondary 40 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain1_sec80
 *		RSSI of RX PPDU on chain 1 of secondary 80 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain2_pri20
 *		RSSI of RX PPDU on chain 2 of primary 20 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain2_sec20
 *		RSSI of RX PPDU on chain 2 of secondary 20 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain2_sec40
 *		RSSI of RX PPDU on chain 2 of secondary 40 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain2_sec80
 *		RSSI of RX PPDU on chain 2 of secondary 80 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain3_pri20
 *		RSSI of RX PPDU on chain 3 of primary 20 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain3_sec20
 *		RSSI of RX PPDU on chain 3 of secondary 20 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain3_sec40
 *		RSSI of RX PPDU on chain 3 of secondary 40 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_chain3_sec80
 *		RSSI of RX PPDU on chain 3 of secondary 80 MHz bandwidth.
 *		Value of 0x80 indicates invalid.
 *
 * rssi_comb
 *		The combined RSSI of RX PPDU of all active chains and
 *		bandwidths.  Value of 0x80 indicates invalid.
 *
 * reserved_4a
 *		Reserved: HW should fill with 0, FW should ignore.
 *
 * is_greenfield
 *		Do we really support this?
 *
 * reserved_4b
 *		Reserved: HW should fill with 0, FW should ignore.
 *
 * l_sig_rate
 *		If l_sig_rate_select is 0:
 *		0x8: OFDM 48 Mbps
 *		0x9: OFDM 24 Mbps
 *		0xA: OFDM 12 Mbps
 *		0xB: OFDM 6 Mbps
 *		0xC: OFDM 54 Mbps
 *		0xD: OFDM 36 Mbps
 *		0xE: OFDM 18 Mbps
 *		0xF: OFDM 9 Mbps
 *		If l_sig_rate_select is 1:
 *		0x8: CCK 11 Mbps long preamble
 *		0x9: CCK 5.5 Mbps long preamble
 *		0xA: CCK 2 Mbps long preamble
 *		0xB: CCK 1 Mbps long preamble
 *		0xC: CCK 11 Mbps short preamble
 *		0xD: CCK 5.5 Mbps short preamble
 *		0xE: CCK 2 Mbps short preamble
 *
 * l_sig_rate_select
 *		Legacy signal rate select.  If set then l_sig_rate indicates
 *		CCK rates.  If clear then l_sig_rate indicates OFDM rates.
 *
 * l_sig_length
 *		Length of legacy frame in octets.
 *
 * l_sig_parity
 *		Odd parity over l_sig_rate and l_sig_length
 *
 * l_sig_tail
 *		Tail bits for Viterbi decoder
 *
 * preamble_type
 *		Indicates the type of preamble ahead:
 *		0x4: Legacy (OFDM/CCK)
 *		0x8: HT
 *		0x9: HT with TxBF
 *		0xC: VHT
 *		0xD: VHT with TxBF
 *		0x80 - 0xFF: Reserved for special baseband data types such
 *		as radar and spectral scan.
 *
 * ht_sig_vht_sig_a_1
 *		If preamble_type == 0x8 or 0x9
 *		HT-SIG (first 24 bits)
 *		If preamble_type == 0xC or 0xD
 *		VHT-SIG A (first 24 bits)
 *		Else
 *		Reserved
 *
 * reserved_6
 *		Reserved: HW should fill with 0, FW should ignore.
 *
 * ht_sig_vht_sig_a_2
 *		If preamble_type == 0x8 or 0x9
 *		HT-SIG (last 24 bits)
 *		If preamble_type == 0xC or 0xD
 *		VHT-SIG A (last 24 bits)
 *		Else
 *		Reserved
 *
 * txbf_h_info
 *		Indicates that the packet data carries H information which
 *		is used for TxBF debug.
 *
 * reserved_7
 *		Reserved: HW should fill with 0, FW should ignore.
 *
 * vht_sig_b
 *		WiFi 1.0 and WiFi 2.0 will likely have this field to be all
 *		0s since the BB does not plan on decoding VHT SIG-B.
 *
 * reserved_8
 *		Reserved: HW should fill with 0, FW should ignore.
 *
 * service
 *		Service field from BB for OFDM, HT and VHT packets.  CCK
 *		packets will have service field of 0.
 *
 * reserved_9
 *		Reserved: HW should fill with 0, FW should ignore.
*/

#define RX_PPDU_END_FLAGS_PHY_ERR             (1 << 0)
#define RX_PPDU_END_FLAGS_RX_LOCATION         (1 << 1)
#define RX_PPDU_END_FLAGS_TXBF_H_INFO         (1 << 2)

#define RX_PPDU_END_INFO0_RX_ANTENNA_MASK     0x00ffffff
#define RX_PPDU_END_INFO0_RX_ANTENNA_LSB      0
#define RX_PPDU_END_INFO0_FLAGS_TX_HT_VHT_ACK (1 << 24)
#define RX_PPDU_END_INFO0_BB_CAPTURED_CHANNEL (1 << 25)

#define RX_PPDU_END_INFO1_PPDU_DONE (1 << 15)

struct rx_ppdu_end {
	__le32 evm_p0;
	__le32 evm_p1;
	__le32 evm_p2;
	__le32 evm_p3;
	__le32 evm_p4;
	__le32 evm_p5;
	__le32 evm_p6;
	__le32 evm_p7;
	__le32 evm_p8;
	__le32 evm_p9;
	__le32 evm_p10;
	__le32 evm_p11;
	__le32 evm_p12;
	__le32 evm_p13;
	__le32 evm_p14;
	__le32 evm_p15;
	__le32 tsf_timestamp;
	__le32 wb_timestamp;
	u8 locationing_timestamp;
	u8 phy_err_code;
	__le16 flags; /* %RX_PPDU_END_FLAGS_ */
	__le32 info0; /* %RX_PPDU_END_INFO0_ */
	__le16 bb_length;
	__le16 info1; /* %RX_PPDU_END_INFO1_ */
} __packed;

/*
 * evm_p0
 *		EVM for pilot 0.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p1
 *		EVM for pilot 1.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p2
 *		EVM for pilot 2.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p3
 *		EVM for pilot 3.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p4
 *		EVM for pilot 4.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p5
 *		EVM for pilot 5.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p6
 *		EVM for pilot 6.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p7
 *		EVM for pilot 7.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p8
 *		EVM for pilot 8.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p9
 *		EVM for pilot 9.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p10
 *		EVM for pilot 10.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p11
 *		EVM for pilot 11.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p12
 *		EVM for pilot 12.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p13
 *		EVM for pilot 13.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p14
 *		EVM for pilot 14.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * evm_p15
 *		EVM for pilot 15.  Contain EVM for streams: 0, 1, 2 and 3.
 *
 * tsf_timestamp
 *		Receive TSF timestamp sampled on the rising edge of
 *		rx_clear.  For PHY errors this may be the current TSF when
 *		phy_error is asserted if the rx_clear does not assert before
 *		the end of the PHY error.
 *
 * wb_timestamp
 *		WLAN/BT timestamp is a 1 usec resolution timestamp which
 *		does not get updated based on receive beacon like TSF.  The
 *		same rules for capturing tsf_timestamp are used to capture
 *		the wb_timestamp.
 *
 * locationing_timestamp
 *		Timestamp used for locationing.  This timestamp is used to
 *		indicate fractions of usec.  For example if the MAC clock is
 *		running at 80 MHz, the timestamp will increment every 12.5
 *		nsec.  The value starts at 0 and increments to 79 and
 *		returns to 0 and repeats.  This information is valid for
 *		every PPDU.  This information can be used in conjunction
 *		with wb_timestamp to capture large delta times.
 *
 * phy_err_code
 *		See the 1.10.8.1.2 for the list of the PHY error codes.
 *
 * phy_err
 *		Indicates a PHY error was detected for this PPDU.
 *
 * rx_location
 *		Indicates that location information was requested.
 *
 * txbf_h_info
 *		Indicates that the packet data carries H information which
 *		is used for TxBF debug.
 *
 * reserved_18
 *		Reserved: HW should fill with 0, FW should ignore.
 *
 * rx_antenna
 *		Receive antenna value
 *
 * tx_ht_vht_ack
 *		Indicates that a HT or VHT Ack/BA frame was transmitted in
 *		response to this receive packet.
 *
 * bb_captured_channel
 *		Indicates that the BB has captured a channel dump.  FW can
 *		then read the channel dump memory.  This may indicate that
 *		the channel was captured either based on PCU setting the
 *		capture_channel bit  BB descriptor or FW setting the
 *		capture_channel mode bit.
 *
 * reserved_19
 *		Reserved: HW should fill with 0, FW should ignore.
 *
 * bb_length
 *		Indicates the number of bytes of baseband information for
 *		PPDUs where the BB descriptor preamble type is 0x80 to 0xFF
 *		which indicates that this is not a normal PPDU but rather
 *		contains baseband debug information.
 *
 * reserved_20
 *		Reserved: HW should fill with 0, FW should ignore.
 *
 * ppdu_done
 *		PPDU end status is only valid when ppdu_done bit is set.
 *		Every time HW sets this bit in memory FW/SW must clear this
 *		bit in memory.  FW will initialize all the ppdu_done dword
 *		to 0.
*/

#define FW_RX_DESC_INFO0_DISCARD  (1 << 0)
#define FW_RX_DESC_INFO0_FORWARD  (1 << 1)
#define FW_RX_DESC_INFO0_INSPECT  (1 << 5)
#define FW_RX_DESC_INFO0_EXT_MASK 0xC0
#define FW_RX_DESC_INFO0_EXT_LSB  6

struct fw_rx_desc_base {
	u8 info0;
} __packed;

#endif /* _RX_DESC_H_ */
