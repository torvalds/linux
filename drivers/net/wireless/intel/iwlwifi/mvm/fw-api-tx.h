/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2016 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#ifndef __fw_api_tx_h__
#define __fw_api_tx_h__

/**
 * enum iwl_tx_flags - bitmasks for tx_flags in TX command
 * @TX_CMD_FLG_PROT_REQUIRE: use RTS or CTS-to-self to protect the frame
 * @TX_CMD_FLG_WRITE_TX_POWER: update current tx power value in the mgmt frame
 * @TX_CMD_FLG_ACK: expect ACK from receiving station
 * @TX_CMD_FLG_STA_RATE: use RS table with initial index from the TX command.
 *	Otherwise, use rate_n_flags from the TX command
 * @TX_CMD_FLG_BAR: this frame is a BA request, immediate BAR is expected
 *	Must set TX_CMD_FLG_ACK with this flag.
 * @TX_CMD_FLG_VHT_NDPA: mark frame is NDPA for VHT beamformer sequence
 * @TX_CMD_FLG_HT_NDPA: mark frame is NDPA for HT beamformer sequence
 * @TX_CMD_FLG_CSI_FDBK2HOST: mark to send feedback to host (only if good CRC)
 * @TX_CMD_FLG_BT_PRIO_POS: the position of the BT priority (bit 11 is ignored
 *	on old firmwares).
 * @TX_CMD_FLG_BT_DIS: disable BT priority for this frame
 * @TX_CMD_FLG_SEQ_CTL: set if FW should override the sequence control.
 *	Should be set for mgmt, non-QOS data, mcast, bcast and in scan command
 * @TX_CMD_FLG_MORE_FRAG: this frame is non-last MPDU
 * @TX_CMD_FLG_TSF: FW should calculate and insert TSF in the frame
 *	Should be set for beacons and probe responses
 * @TX_CMD_FLG_CALIB: activate PA TX power calibrations
 * @TX_CMD_FLG_KEEP_SEQ_CTL: if seq_ctl is set, don't increase inner seq count
 * @TX_CMD_FLG_MH_PAD: driver inserted 2 byte padding after MAC header.
 *	Should be set for 26/30 length MAC headers
 * @TX_CMD_FLG_RESP_TO_DRV: zero this if the response should go only to FW
 * @TX_CMD_FLG_TKIP_MIC_DONE: FW already performed TKIP MIC calculation
 * @TX_CMD_FLG_DUR: disable duration overwriting used in PS-Poll Assoc-id
 * @TX_CMD_FLG_FW_DROP: FW should mark frame to be dropped
 * @TX_CMD_FLG_EXEC_PAPD: execute PAPD
 * @TX_CMD_FLG_PAPD_TYPE: 0 for reference power, 1 for nominal power
 * @TX_CMD_FLG_HCCA_CHUNK: mark start of TSPEC chunk
 */
enum iwl_tx_flags {
	TX_CMD_FLG_PROT_REQUIRE		= BIT(0),
	TX_CMD_FLG_WRITE_TX_POWER	= BIT(1),
	TX_CMD_FLG_ACK			= BIT(3),
	TX_CMD_FLG_STA_RATE		= BIT(4),
	TX_CMD_FLG_BAR			= BIT(6),
	TX_CMD_FLG_TXOP_PROT		= BIT(7),
	TX_CMD_FLG_VHT_NDPA		= BIT(8),
	TX_CMD_FLG_HT_NDPA		= BIT(9),
	TX_CMD_FLG_CSI_FDBK2HOST	= BIT(10),
	TX_CMD_FLG_BT_PRIO_POS		= 11,
	TX_CMD_FLG_BT_DIS		= BIT(12),
	TX_CMD_FLG_SEQ_CTL		= BIT(13),
	TX_CMD_FLG_MORE_FRAG		= BIT(14),
	TX_CMD_FLG_TSF			= BIT(16),
	TX_CMD_FLG_CALIB		= BIT(17),
	TX_CMD_FLG_KEEP_SEQ_CTL		= BIT(18),
	TX_CMD_FLG_MH_PAD		= BIT(20),
	TX_CMD_FLG_RESP_TO_DRV		= BIT(21),
	TX_CMD_FLG_TKIP_MIC_DONE	= BIT(23),
	TX_CMD_FLG_DUR			= BIT(25),
	TX_CMD_FLG_FW_DROP		= BIT(26),
	TX_CMD_FLG_EXEC_PAPD		= BIT(27),
	TX_CMD_FLG_PAPD_TYPE		= BIT(28),
	TX_CMD_FLG_HCCA_CHUNK		= BIT(31)
}; /* TX_FLAGS_BITS_API_S_VER_1 */

/**
 * enum iwl_tx_pm_timeouts - pm timeout values in TX command
 * @PM_FRAME_NONE: no need to suspend sleep mode
 * @PM_FRAME_MGMT: fw suspend sleep mode for 100TU
 * @PM_FRAME_ASSOC: fw suspend sleep mode for 10sec
 */
enum iwl_tx_pm_timeouts {
	PM_FRAME_NONE		= 0,
	PM_FRAME_MGMT		= 2,
	PM_FRAME_ASSOC		= 3,
};

#define TX_CMD_SEC_MSK			0x07
#define TX_CMD_SEC_WEP_KEY_IDX_POS	6
#define TX_CMD_SEC_WEP_KEY_IDX_MSK	0xc0

/**
 * enum iwl_tx_cmd_sec_ctrl - bitmasks for security control in TX command
 * @TX_CMD_SEC_WEP: WEP encryption algorithm.
 * @TX_CMD_SEC_CCM: CCM encryption algorithm.
 * @TX_CMD_SEC_TKIP: TKIP encryption algorithm.
 * @TX_CMD_SEC_EXT: extended cipher algorithm.
 * @TX_CMD_SEC_GCMP: GCMP encryption algorithm.
 * @TX_CMD_SEC_KEY128: set for 104 bits WEP key.
 * @TC_CMD_SEC_KEY_FROM_TABLE: for a non-WEP key, set if the key should be taken
 *	from the table instead of from the TX command.
 *	If the key is taken from the key table its index should be given by the
 *	first byte of the TX command key field.
 */
enum iwl_tx_cmd_sec_ctrl {
	TX_CMD_SEC_WEP			= 0x01,
	TX_CMD_SEC_CCM			= 0x02,
	TX_CMD_SEC_TKIP			= 0x03,
	TX_CMD_SEC_EXT			= 0x04,
	TX_CMD_SEC_GCMP			= 0x05,
	TX_CMD_SEC_KEY128		= 0x08,
	TC_CMD_SEC_KEY_FROM_TABLE	= 0x08,
};

/* TODO: how does these values are OK with only 16 bit variable??? */
/*
 * TX command next frame info
 *
 * bits 0:2 - security control (TX_CMD_SEC_*)
 * bit 3 - immediate ACK required
 * bit 4 - rate is taken from STA table
 * bit 5 - frame belongs to BA stream
 * bit 6 - immediate BA response expected
 * bit 7 - unused
 * bits 8:15 - Station ID
 * bits 16:31 - rate
 */
#define TX_CMD_NEXT_FRAME_ACK_MSK		(0x8)
#define TX_CMD_NEXT_FRAME_STA_RATE_MSK		(0x10)
#define TX_CMD_NEXT_FRAME_BA_MSK		(0x20)
#define TX_CMD_NEXT_FRAME_IMM_BA_RSP_MSK	(0x40)
#define TX_CMD_NEXT_FRAME_FLAGS_MSK		(0xf8)
#define TX_CMD_NEXT_FRAME_STA_ID_MSK		(0xff00)
#define TX_CMD_NEXT_FRAME_STA_ID_POS		(8)
#define TX_CMD_NEXT_FRAME_RATE_MSK		(0xffff0000)
#define TX_CMD_NEXT_FRAME_RATE_POS		(16)

/*
 * TX command Frame life time in us - to be written in pm_frame_timeout
 */
#define TX_CMD_LIFE_TIME_INFINITE	0xFFFFFFFF
#define TX_CMD_LIFE_TIME_DEFAULT	2000000 /* 2000 ms*/
#define TX_CMD_LIFE_TIME_PROBE_RESP	40000 /* 40 ms */
#define TX_CMD_LIFE_TIME_EXPIRED_FRAME	0

/*
 * TID for non QoS frames - to be written in tid_tspec
 */
#define IWL_TID_NON_QOS	IWL_MAX_TID_COUNT

/*
 * Limits on the retransmissions - to be written in {data,rts}_retry_limit
 */
#define IWL_DEFAULT_TX_RETRY			15
#define IWL_MGMT_DFAULT_RETRY_LIMIT		3
#define IWL_RTS_DFAULT_RETRY_LIMIT		60
#define IWL_BAR_DFAULT_RETRY_LIMIT		60
#define IWL_LOW_RETRY_LIMIT			7

/**
 * enum iwl_tx_offload_assist_flags_pos -  set %iwl_tx_cmd offload_assist values
 * @TX_CMD_OFFLD_IP_HDR_OFFSET: offset to start of IP header (in words)
 *	from mac header end. For normal case it is 4 words for SNAP.
 *	note: tx_cmd, mac header and pad are not counted in the offset.
 *	This is used to help the offload in case there is tunneling such as
 *	IPv6 in IPv4, in such case the ip header offset should point to the
 *	inner ip header and IPv4 checksum of the external header should be
 *	calculated by driver.
 * @TX_CMD_OFFLD_L4_EN: enable TCP/UDP checksum
 * @TX_CMD_OFFLD_L3_EN: enable IP header checksum
 * @TX_CMD_OFFLD_MH_SIZE: size of the mac header in words. Includes the IV
 *	field. Doesn't include the pad.
 * @TX_CMD_OFFLD_PAD: mark 2-byte pad was inserted after the mac header for
 *	alignment
 * @TX_CMD_OFFLD_AMSDU: mark TX command is A-MSDU
 */
enum iwl_tx_offload_assist_flags_pos {
	TX_CMD_OFFLD_IP_HDR =		0,
	TX_CMD_OFFLD_L4_EN =		6,
	TX_CMD_OFFLD_L3_EN =		7,
	TX_CMD_OFFLD_MH_SIZE =		8,
	TX_CMD_OFFLD_PAD =		13,
	TX_CMD_OFFLD_AMSDU =		14,
};

#define IWL_TX_CMD_OFFLD_MH_MASK	0x1f
#define IWL_TX_CMD_OFFLD_IP_HDR_MASK	0x3f

/* TODO: complete documentation for try_cnt and btkill_cnt */
/**
 * struct iwl_tx_cmd - TX command struct to FW
 * ( TX_CMD = 0x1c )
 * @len: in bytes of the payload, see below for details
 * @offload_assist: TX offload configuration
 * @tx_flags: combination of TX_CMD_FLG_*
 * @rate_n_flags: rate for *all* Tx attempts, if TX_CMD_FLG_STA_RATE_MSK is
 *	cleared. Combination of RATE_MCS_*
 * @sta_id: index of destination station in FW station table
 * @sec_ctl: security control, TX_CMD_SEC_*
 * @initial_rate_index: index into the the rate table for initial TX attempt.
 *	Applied if TX_CMD_FLG_STA_RATE_MSK is set, normally 0 for data frames.
 * @key: security key
 * @next_frame_flags: TX_CMD_SEC_* and TX_CMD_NEXT_FRAME_*
 * @life_time: frame life time (usecs??)
 * @dram_lsb_ptr: Physical address of scratch area in the command (try_cnt +
 *	btkill_cnd + reserved), first 32 bits. "0" disables usage.
 * @dram_msb_ptr: upper bits of the scratch physical address
 * @rts_retry_limit: max attempts for RTS
 * @data_retry_limit: max attempts to send the data packet
 * @tid_spec: TID/tspec
 * @pm_frame_timeout: PM TX frame timeout
 *
 * The byte count (both len and next_frame_len) includes MAC header
 * (24/26/30/32 bytes)
 * + 2 bytes pad if 26/30 header size
 * + 8 byte IV for CCM or TKIP (not used for WEP)
 * + Data payload
 * + 8-byte MIC (not used for CCM/WEP)
 * It does not include post-MAC padding, i.e.,
 * MIC (CCM) 8 bytes, ICV (WEP/TKIP/CKIP) 4 bytes, CRC 4 bytes.
 * Range of len: 14-2342 bytes.
 *
 * After the struct fields the MAC header is placed, plus any padding,
 * and then the actial payload.
 */
struct iwl_tx_cmd {
	__le16 len;
	__le16 offload_assist;
	__le32 tx_flags;
	struct {
		u8 try_cnt;
		u8 btkill_cnt;
		__le16 reserved;
	} scratch; /* DRAM_SCRATCH_API_U_VER_1 */
	__le32 rate_n_flags;
	u8 sta_id;
	u8 sec_ctl;
	u8 initial_rate_index;
	u8 reserved2;
	u8 key[16];
	__le32 reserved3;
	__le32 life_time;
	__le32 dram_lsb_ptr;
	u8 dram_msb_ptr;
	u8 rts_retry_limit;
	u8 data_retry_limit;
	u8 tid_tspec;
	__le16 pm_frame_timeout;
	__le16 reserved4;
	u8 payload[0];
	struct ieee80211_hdr hdr[0];
} __packed; /* TX_CMD_API_S_VER_6 */

/*
 * TX response related data
 */

/*
 * enum iwl_tx_status - status that is returned by the fw after attempts to Tx
 * @TX_STATUS_SUCCESS:
 * @TX_STATUS_DIRECT_DONE:
 * @TX_STATUS_POSTPONE_DELAY:
 * @TX_STATUS_POSTPONE_FEW_BYTES:
 * @TX_STATUS_POSTPONE_BT_PRIO:
 * @TX_STATUS_POSTPONE_QUIET_PERIOD:
 * @TX_STATUS_POSTPONE_CALC_TTAK:
 * @TX_STATUS_FAIL_INTERNAL_CROSSED_RETRY:
 * @TX_STATUS_FAIL_SHORT_LIMIT:
 * @TX_STATUS_FAIL_LONG_LIMIT:
 * @TX_STATUS_FAIL_UNDERRUN:
 * @TX_STATUS_FAIL_DRAIN_FLOW:
 * @TX_STATUS_FAIL_RFKILL_FLUSH:
 * @TX_STATUS_FAIL_LIFE_EXPIRE:
 * @TX_STATUS_FAIL_DEST_PS:
 * @TX_STATUS_FAIL_HOST_ABORTED:
 * @TX_STATUS_FAIL_BT_RETRY:
 * @TX_STATUS_FAIL_STA_INVALID:
 * @TX_TATUS_FAIL_FRAG_DROPPED:
 * @TX_STATUS_FAIL_TID_DISABLE:
 * @TX_STATUS_FAIL_FIFO_FLUSHED:
 * @TX_STATUS_FAIL_SMALL_CF_POLL:
 * @TX_STATUS_FAIL_FW_DROP:
 * @TX_STATUS_FAIL_STA_COLOR_MISMATCH: mismatch between color of Tx cmd and
 *	STA table
 * @TX_FRAME_STATUS_INTERNAL_ABORT:
 * @TX_MODE_MSK:
 * @TX_MODE_NO_BURST:
 * @TX_MODE_IN_BURST_SEQ:
 * @TX_MODE_FIRST_IN_BURST:
 * @TX_QUEUE_NUM_MSK:
 *
 * Valid only if frame_count =1
 * TODO: complete documentation
 */
enum iwl_tx_status {
	TX_STATUS_MSK = 0x000000ff,
	TX_STATUS_SUCCESS = 0x01,
	TX_STATUS_DIRECT_DONE = 0x02,
	/* postpone TX */
	TX_STATUS_POSTPONE_DELAY = 0x40,
	TX_STATUS_POSTPONE_FEW_BYTES = 0x41,
	TX_STATUS_POSTPONE_BT_PRIO = 0x42,
	TX_STATUS_POSTPONE_QUIET_PERIOD = 0x43,
	TX_STATUS_POSTPONE_CALC_TTAK = 0x44,
	/* abort TX */
	TX_STATUS_FAIL_INTERNAL_CROSSED_RETRY = 0x81,
	TX_STATUS_FAIL_SHORT_LIMIT = 0x82,
	TX_STATUS_FAIL_LONG_LIMIT = 0x83,
	TX_STATUS_FAIL_UNDERRUN = 0x84,
	TX_STATUS_FAIL_DRAIN_FLOW = 0x85,
	TX_STATUS_FAIL_RFKILL_FLUSH = 0x86,
	TX_STATUS_FAIL_LIFE_EXPIRE = 0x87,
	TX_STATUS_FAIL_DEST_PS = 0x88,
	TX_STATUS_FAIL_HOST_ABORTED = 0x89,
	TX_STATUS_FAIL_BT_RETRY = 0x8a,
	TX_STATUS_FAIL_STA_INVALID = 0x8b,
	TX_STATUS_FAIL_FRAG_DROPPED = 0x8c,
	TX_STATUS_FAIL_TID_DISABLE = 0x8d,
	TX_STATUS_FAIL_FIFO_FLUSHED = 0x8e,
	TX_STATUS_FAIL_SMALL_CF_POLL = 0x8f,
	TX_STATUS_FAIL_FW_DROP = 0x90,
	TX_STATUS_FAIL_STA_COLOR_MISMATCH = 0x91,
	TX_STATUS_INTERNAL_ABORT = 0x92,
	TX_MODE_MSK = 0x00000f00,
	TX_MODE_NO_BURST = 0x00000000,
	TX_MODE_IN_BURST_SEQ = 0x00000100,
	TX_MODE_FIRST_IN_BURST = 0x00000200,
	TX_QUEUE_NUM_MSK = 0x0001f000,
	TX_NARROW_BW_MSK = 0x00060000,
	TX_NARROW_BW_1DIV2 = 0x00020000,
	TX_NARROW_BW_1DIV4 = 0x00040000,
	TX_NARROW_BW_1DIV8 = 0x00060000,
};

/*
 * enum iwl_tx_agg_status - TX aggregation status
 * @AGG_TX_STATE_STATUS_MSK:
 * @AGG_TX_STATE_TRANSMITTED:
 * @AGG_TX_STATE_UNDERRUN:
 * @AGG_TX_STATE_BT_PRIO:
 * @AGG_TX_STATE_FEW_BYTES:
 * @AGG_TX_STATE_ABORT:
 * @AGG_TX_STATE_LAST_SENT_TTL:
 * @AGG_TX_STATE_LAST_SENT_TRY_CNT:
 * @AGG_TX_STATE_LAST_SENT_BT_KILL:
 * @AGG_TX_STATE_SCD_QUERY:
 * @AGG_TX_STATE_TEST_BAD_CRC32:
 * @AGG_TX_STATE_RESPONSE:
 * @AGG_TX_STATE_DUMP_TX:
 * @AGG_TX_STATE_DELAY_TX:
 * @AGG_TX_STATE_TRY_CNT_MSK: Retry count for 1st frame in aggregation (retries
 *	occur if tx failed for this frame when it was a member of a previous
 *	aggregation block). If rate scaling is used, retry count indicates the
 *	rate table entry used for all frames in the new agg.
 *@ AGG_TX_STATE_SEQ_NUM_MSK: Command ID and sequence number of Tx command for
 *	this frame
 *
 * TODO: complete documentation
 */
enum iwl_tx_agg_status {
	AGG_TX_STATE_STATUS_MSK = 0x00fff,
	AGG_TX_STATE_TRANSMITTED = 0x000,
	AGG_TX_STATE_UNDERRUN = 0x001,
	AGG_TX_STATE_BT_PRIO = 0x002,
	AGG_TX_STATE_FEW_BYTES = 0x004,
	AGG_TX_STATE_ABORT = 0x008,
	AGG_TX_STATE_LAST_SENT_TTL = 0x010,
	AGG_TX_STATE_LAST_SENT_TRY_CNT = 0x020,
	AGG_TX_STATE_LAST_SENT_BT_KILL = 0x040,
	AGG_TX_STATE_SCD_QUERY = 0x080,
	AGG_TX_STATE_TEST_BAD_CRC32 = 0x0100,
	AGG_TX_STATE_RESPONSE = 0x1ff,
	AGG_TX_STATE_DUMP_TX = 0x200,
	AGG_TX_STATE_DELAY_TX = 0x400,
	AGG_TX_STATE_TRY_CNT_POS = 12,
	AGG_TX_STATE_TRY_CNT_MSK = 0xf << AGG_TX_STATE_TRY_CNT_POS,
};

#define AGG_TX_STATE_LAST_SENT_MSK  (AGG_TX_STATE_LAST_SENT_TTL| \
				     AGG_TX_STATE_LAST_SENT_TRY_CNT| \
				     AGG_TX_STATE_LAST_SENT_BT_KILL)

/*
 * The mask below describes a status where we are absolutely sure that the MPDU
 * wasn't sent. For BA/Underrun we cannot be that sure. All we know that we've
 * written the bytes to the TXE, but we know nothing about what the DSP did.
 */
#define AGG_TX_STAT_FRAME_NOT_SENT (AGG_TX_STATE_FEW_BYTES | \
				    AGG_TX_STATE_ABORT | \
				    AGG_TX_STATE_SCD_QUERY)

/*
 * REPLY_TX = 0x1c (response)
 *
 * This response may be in one of two slightly different formats, indicated
 * by the frame_count field:
 *
 * 1)	No aggregation (frame_count == 1).  This reports Tx results for a single
 *	frame. Multiple attempts, at various bit rates, may have been made for
 *	this frame.
 *
 * 2)	Aggregation (frame_count > 1).  This reports Tx results for two or more
 *	frames that used block-acknowledge.  All frames were transmitted at
 *	same rate. Rate scaling may have been used if first frame in this new
 *	agg block failed in previous agg block(s).
 *
 *	Note that, for aggregation, ACK (block-ack) status is not delivered
 *	here; block-ack has not been received by the time the device records
 *	this status.
 *	This status relates to reasons the tx might have been blocked or aborted
 *	within the device, rather than whether it was received successfully by
 *	the destination station.
 */

/**
 * struct agg_tx_status - per packet TX aggregation status
 * @status: enum iwl_tx_agg_status
 * @sequence: Sequence # for this frame's Tx cmd (not SSN!)
 */
struct agg_tx_status {
	__le16 status;
	__le16 sequence;
} __packed;

/*
 * definitions for initial rate index field
 * bits [3:0] initial rate index
 * bits [6:4] rate table color, used for the initial rate
 * bit-7 invalid rate indication
 */
#define TX_RES_INIT_RATE_INDEX_MSK 0x0f
#define TX_RES_RATE_TABLE_COLOR_MSK 0x70
#define TX_RES_INV_RATE_INDEX_MSK 0x80

#define IWL_MVM_TX_RES_GET_TID(_ra_tid) ((_ra_tid) & 0x0f)
#define IWL_MVM_TX_RES_GET_RA(_ra_tid) ((_ra_tid) >> 4)

/**
 * struct iwl_mvm_tx_resp - notifies that fw is TXing a packet
 * ( REPLY_TX = 0x1c )
 * @frame_count: 1 no aggregation, >1 aggregation
 * @bt_kill_count: num of times blocked by bluetooth (unused for agg)
 * @failure_rts: num of failures due to unsuccessful RTS
 * @failure_frame: num failures due to no ACK (unused for agg)
 * @initial_rate: for non-agg: rate of the successful Tx. For agg: rate of the
 *	Tx of all the batch. RATE_MCS_*
 * @wireless_media_time: for non-agg: RTS + CTS + frame tx attempts time + ACK.
 *	for agg: RTS + CTS + aggregation tx time + block-ack time.
 *	in usec.
 * @pa_status: tx power info
 * @pa_integ_res_a: tx power info
 * @pa_integ_res_b: tx power info
 * @pa_integ_res_c: tx power info
 * @measurement_req_id: tx power info
 * @tfd_info: TFD information set by the FH
 * @seq_ctl: sequence control from the Tx cmd
 * @byte_cnt: byte count from the Tx cmd
 * @tlc_info: TLC rate info
 * @ra_tid: bits [3:0] = ra, bits [7:4] = tid
 * @frame_ctrl: frame control
 * @status: for non-agg:  frame status TX_STATUS_*
 *	for agg: status of 1st frame, AGG_TX_STATE_*; other frame status fields
 *	follow this one, up to frame_count.
 *
 * After the array of statuses comes the SSN of the SCD. Look at
 * %iwl_mvm_get_scd_ssn for more details.
 */
struct iwl_mvm_tx_resp {
	u8 frame_count;
	u8 bt_kill_count;
	u8 failure_rts;
	u8 failure_frame;
	__le32 initial_rate;
	__le16 wireless_media_time;

	u8 pa_status;
	u8 pa_integ_res_a[3];
	u8 pa_integ_res_b[3];
	u8 pa_integ_res_c[3];
	__le16 measurement_req_id;
	u8 reduced_tpc;
	u8 reserved;

	__le32 tfd_info;
	__le16 seq_ctl;
	__le16 byte_cnt;
	u8 tlc_info;
	u8 ra_tid;
	__le16 frame_ctrl;

	struct agg_tx_status status;
} __packed; /* TX_RSP_API_S_VER_3 */

/**
 * struct iwl_mvm_ba_notif - notifies about reception of BA
 * ( BA_NOTIF = 0xc5 )
 * @sta_addr_lo32: lower 32 bits of the MAC address
 * @sta_addr_hi16: upper 16 bits of the MAC address
 * @sta_id: Index of recipient (BA-sending) station in fw's station table
 * @tid: tid of the session
 * @seq_ctl:
 * @bitmap: the bitmap of the BA notification as seen in the air
 * @scd_flow: the tx queue this BA relates to
 * @scd_ssn: the index of the last contiguously sent packet
 * @txed: number of Txed frames in this batch
 * @txed_2_done: number of Acked frames in this batch
 * @reduced_txp: power reduced according to TPC. This is the actual value and
 *	not a copy from the LQ command. Thus, if not the first rate was used
 *	for Tx-ing then this value will be set to 0 by FW.
 */
struct iwl_mvm_ba_notif {
	__le32 sta_addr_lo32;
	__le16 sta_addr_hi16;
	__le16 reserved;

	u8 sta_id;
	u8 tid;
	__le16 seq_ctl;
	__le64 bitmap;
	__le16 scd_flow;
	__le16 scd_ssn;
	u8 txed;
	u8 txed_2_done;
	u8 reduced_txp;
	u8 reserved1;
} __packed;

/**
 * struct iwl_mvm_compressed_ba_tfd - progress of a TFD queue
 * @q_num: TFD queue number
 * @tfd_index: Index of first un-acked frame in the  TFD queue
 */
struct iwl_mvm_compressed_ba_tfd {
	u8 q_num;
	u8 reserved;
	__le16 tfd_index;
} __packed; /* COMPRESSED_BA_TFD_API_S_VER_1 */

/**
 * struct iwl_mvm_compressed_ba_ratid - progress of a RA TID queue
 * @q_num: RA TID queue number
 * @tid: TID of the queue
 * @ssn: BA window current SSN
 */
struct iwl_mvm_compressed_ba_ratid {
	u8 q_num;
	u8 tid;
	__le16 ssn;
} __packed; /* COMPRESSED_BA_RATID_API_S_VER_1 */

/*
 * enum iwl_mvm_ba_resp_flags - TX aggregation status
 * @IWL_MVM_BA_RESP_TX_AGG: generated due to BA
 * @IWL_MVM_BA_RESP_TX_BAR: generated due to BA after BAR
 * @IWL_MVM_BA_RESP_TX_AGG_FAIL: aggregation didn't receive BA
 * @IWL_MVM_BA_RESP_TX_UNDERRUN: aggregation got underrun
 * @IWL_MVM_BA_RESP_TX_BT_KILL: aggregation got BT-kill
 * @IWL_MVM_BA_RESP_TX_DSP_TIMEOUT: aggregation didn't finish within the
 *	expected time
 */
enum iwl_mvm_ba_resp_flags {
	IWL_MVM_BA_RESP_TX_AGG,
	IWL_MVM_BA_RESP_TX_BAR,
	IWL_MVM_BA_RESP_TX_AGG_FAIL,
	IWL_MVM_BA_RESP_TX_UNDERRUN,
	IWL_MVM_BA_RESP_TX_BT_KILL,
	IWL_MVM_BA_RESP_TX_DSP_TIMEOUT
};

/**
 * struct iwl_mvm_compressed_ba_notif - notifies about reception of BA
 * ( BA_NOTIF = 0xc5 )
 * @flags: status flag, see the &iwl_mvm_ba_resp_flags
 * @sta_id: Index of recipient (BA-sending) station in fw's station table
 * @reduced_txp: power reduced according to TPC. This is the actual value and
 *	not a copy from the LQ command. Thus, if not the first rate was used
 *	for Tx-ing then this value will be set to 0 by FW.
 * @initial_rate: TLC rate info, initial rate index, TLC table color
 * @retry_cnt: retry count
 * @query_byte_cnt: SCD query byte count
 * @query_frame_cnt: SCD query frame count
 * @txed: number of frames sent in the aggregation (all-TIDs)
 * @done: number of frames that were Acked by the BA (all-TIDs)
 * @wireless_time: Wireless-media time
 * @tx_rate: the rate the aggregation was sent at
 * @tfd_cnt: number of TFD-Q elements
 * @ra_tid_cnt: number of RATID-Q elements
 */
struct iwl_mvm_compressed_ba_notif {
	__le32 flags;
	u8 sta_id;
	u8 reduced_txp;
	u8 initial_rate;
	u8 retry_cnt;
	__le32 query_byte_cnt;
	__le16 query_frame_cnt;
	__le16 txed;
	__le16 done;
	__le32 wireless_time;
	__le32 tx_rate;
	__le16 tfd_cnt;
	__le16 ra_tid_cnt;
	struct iwl_mvm_compressed_ba_tfd tfd[1];
	struct iwl_mvm_compressed_ba_ratid ra_tid[0];
} __packed; /* COMPRESSED_BA_RES_API_S_VER_4 */

/**
 * struct iwl_mac_beacon_cmd_v6 - beacon template command
 * @tx: the tx commands associated with the beacon frame
 * @template_id: currently equal to the mac context id of the coresponding
 *  mac.
 * @tim_idx: the offset of the tim IE in the beacon
 * @tim_size: the length of the tim IE
 * @frame: the template of the beacon frame
 */
struct iwl_mac_beacon_cmd_v6 {
	struct iwl_tx_cmd tx;
	__le32 template_id;
	__le32 tim_idx;
	__le32 tim_size;
	struct ieee80211_hdr frame[0];
} __packed; /* BEACON_TEMPLATE_CMD_API_S_VER_6 */

/**
 * struct iwl_mac_beacon_cmd - beacon template command with offloaded CSA
 * @tx: the tx commands associated with the beacon frame
 * @template_id: currently equal to the mac context id of the coresponding
 *  mac.
 * @tim_idx: the offset of the tim IE in the beacon
 * @tim_size: the length of the tim IE
 * @ecsa_offset: offset to the ECSA IE if present
 * @csa_offset: offset to the CSA IE if present
 * @frame: the template of the beacon frame
 */
struct iwl_mac_beacon_cmd {
	struct iwl_tx_cmd tx;
	__le32 template_id;
	__le32 tim_idx;
	__le32 tim_size;
	__le32 ecsa_offset;
	__le32 csa_offset;
	struct ieee80211_hdr frame[0];
} __packed; /* BEACON_TEMPLATE_CMD_API_S_VER_7 */

struct iwl_beacon_notif {
	struct iwl_mvm_tx_resp beacon_notify_hdr;
	__le64 tsf;
	__le32 ibss_mgr_status;
} __packed;

/**
 * struct iwl_extended_beacon_notif - notifies about beacon transmission
 * @beacon_notify_hdr: tx response command associated with the beacon
 * @tsf: last beacon tsf
 * @ibss_mgr_status: whether IBSS is manager
 * @gp2: last beacon time in gp2
 */
struct iwl_extended_beacon_notif {
	struct iwl_mvm_tx_resp beacon_notify_hdr;
	__le64 tsf;
	__le32 ibss_mgr_status;
	__le32 gp2;
} __packed; /* BEACON_NTFY_API_S_VER_5 */

/**
 * enum iwl_dump_control - dump (flush) control flags
 * @DUMP_TX_FIFO_FLUSH: Dump MSDUs until the the FIFO is empty
 *	and the TFD queues are empty.
 */
enum iwl_dump_control {
	DUMP_TX_FIFO_FLUSH	= BIT(1),
};

/**
 * struct iwl_tx_path_flush_cmd -- queue/FIFO flush command
 * @queues_ctl: bitmap of queues to flush
 * @flush_ctl: control flags
 * @reserved: reserved
 */
struct iwl_tx_path_flush_cmd {
	__le32 queues_ctl;
	__le16 flush_ctl;
	__le16 reserved;
} __packed; /* TX_PATH_FLUSH_CMD_API_S_VER_1 */

/**
 * iwl_mvm_get_scd_ssn - returns the SSN of the SCD
 * @tx_resp: the Tx response from the fw (agg or non-agg)
 *
 * When the fw sends an AMPDU, it fetches the MPDUs one after the other. Since
 * it can't know that everything will go well until the end of the AMPDU, it
 * can't know in advance the number of MPDUs that will be sent in the current
 * batch. This is why it writes the agg Tx response while it fetches the MPDUs.
 * Hence, it can't know in advance what the SSN of the SCD will be at the end
 * of the batch. This is why the SSN of the SCD is written at the end of the
 * whole struct at a variable offset. This function knows how to cope with the
 * variable offset and returns the SSN of the SCD.
 */
static inline u32 iwl_mvm_get_scd_ssn(struct iwl_mvm_tx_resp *tx_resp)
{
	return le32_to_cpup((__le32 *)&tx_resp->status +
			    tx_resp->frame_count) & 0xfff;
}

/* Available options for the SCD_QUEUE_CFG HCMD */
enum iwl_scd_cfg_actions {
	SCD_CFG_DISABLE_QUEUE		= 0x0,
	SCD_CFG_ENABLE_QUEUE		= 0x1,
	SCD_CFG_UPDATE_QUEUE_TID	= 0x2,
};

/**
 * struct iwl_scd_txq_cfg_cmd - New txq hw scheduler config command
 * @token:
 * @sta_id: station id
 * @tid:
 * @scd_queue: scheduler queue to confiug
 * @action: 1 queue enable, 0 queue disable, 2 change txq's tid owner
 *	Value is one of %iwl_scd_cfg_actions options
 * @aggregate: 1 aggregated queue, 0 otherwise
 * @tx_fifo: %enum iwl_mvm_tx_fifo
 * @window: BA window size
 * @ssn: SSN for the BA agreement
 */
struct iwl_scd_txq_cfg_cmd {
	u8 token;
	u8 sta_id;
	u8 tid;
	u8 scd_queue;
	u8 action;
	u8 aggregate;
	u8 tx_fifo;
	u8 window;
	__le16 ssn;
	__le16 reserved;
} __packed; /* SCD_QUEUE_CFG_CMD_API_S_VER_1 */

/**
 * struct iwl_scd_txq_cfg_rsp
 * @token: taken from the command
 * @sta_id: station id from the command
 * @tid: tid from the command
 * @scd_queue: scd_queue from the command
 */
struct iwl_scd_txq_cfg_rsp {
	u8 token;
	u8 sta_id;
	u8 tid;
	u8 scd_queue;
} __packed; /* SCD_QUEUE_CFG_RSP_API_S_VER_1 */

#endif /* __fw_api_tx_h__ */
