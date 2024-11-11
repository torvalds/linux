/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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

#ifndef MAC_H
#define MAC_H
#include <net/cfg80211.h>

#define set11nTries(_series, _index) \
	(SM((_series)[_index].Tries, AR_XmitDataTries##_index))

#define set11nRate(_series, _index) \
	(SM((_series)[_index].Rate, AR_XmitRate##_index))

#define set11nPktDurRTSCTS(_series, _index)				\
	(SM((_series)[_index].PktDuration, AR_PacketDur##_index) |	\
	 ((_series)[_index].RateFlags & ATH9K_RATESERIES_RTS_CTS   ?	\
	  AR_RTSCTSQual##_index : 0))

#define set11nRateFlags(_series, _index)				\
	(((_series)[_index].RateFlags & ATH9K_RATESERIES_2040 ?		\
	  AR_2040_##_index : 0)						\
	 |((_series)[_index].RateFlags & ATH9K_RATESERIES_HALFGI ?	\
	   AR_GI##_index : 0)						\
	 |((_series)[_index].RateFlags & ATH9K_RATESERIES_STBC ?	\
	   AR_STBC##_index : 0))

#define set11nChainSel(_series, _index)					\
	(SM((_series)[_index].ChSel, AR_ChainSel##_index))

#define CCK_SIFS_TIME        10
#define CCK_PREAMBLE_BITS   144
#define CCK_PLCP_BITS        48

#define OFDM_SIFS_TIME        16
#define OFDM_PREAMBLE_TIME    20
#define OFDM_PLCP_BITS        22
#define OFDM_SYMBOL_TIME      4

#define OFDM_SIFS_TIME_HALF     32
#define OFDM_PREAMBLE_TIME_HALF 40
#define OFDM_PLCP_BITS_HALF     22
#define OFDM_SYMBOL_TIME_HALF   8

#define OFDM_SIFS_TIME_QUARTER      64
#define OFDM_PREAMBLE_TIME_QUARTER  80
#define OFDM_PLCP_BITS_QUARTER      22
#define OFDM_SYMBOL_TIME_QUARTER    16

#define INIT_AIFS       2
#define INIT_CWMIN      15
#define INIT_CWMIN_11B  31
#define INIT_CWMAX      1023
#define INIT_SH_RETRY   10
#define INIT_LG_RETRY   10
#define INIT_SSH_RETRY  32
#define INIT_SLG_RETRY  32

#define ATH9K_TXERR_XRETRY         0x01
#define ATH9K_TXERR_FILT           0x02
#define ATH9K_TXERR_FIFO           0x04
#define ATH9K_TXERR_XTXOP          0x08
#define ATH9K_TXERR_TIMER_EXPIRED  0x10
#define ATH9K_TX_ACKED		   0x20
#define ATH9K_TX_FLUSH		   0x40
#define ATH9K_TXERR_MASK						\
	(ATH9K_TXERR_XRETRY | ATH9K_TXERR_FILT | ATH9K_TXERR_FIFO |	\
	 ATH9K_TXERR_XTXOP | ATH9K_TXERR_TIMER_EXPIRED | ATH9K_TX_FLUSH)

#define ATH9K_TX_BA                0x01
#define ATH9K_TX_PWRMGMT           0x02
#define ATH9K_TX_DESC_CFG_ERR      0x04
#define ATH9K_TX_DATA_UNDERRUN     0x08
#define ATH9K_TX_DELIM_UNDERRUN    0x10
#define ATH9K_TX_SW_FILTERED       0x80

/* 64 bytes */
#define MIN_TX_FIFO_THRESHOLD   0x1

/*
 * Single stream device AR9285 and AR9271 require 2 KB
 * to work around a hardware issue, all other devices
 * have can use the max 4 KB limit.
 */
#define MAX_TX_FIFO_THRESHOLD   ((4096 / 64) - 1)

struct ath_tx_status {
	u32 ts_tstamp;
	u16 ts_seqnum;
	u8 ts_status;
	u8 ts_rateindex;
	int8_t ts_rssi;
	u8 ts_shortretry;
	u8 ts_longretry;
	u8 ts_virtcol;
	u8 ts_flags;
	int8_t ts_rssi_ctl0;
	int8_t ts_rssi_ctl1;
	int8_t ts_rssi_ctl2;
	int8_t ts_rssi_ext0;
	int8_t ts_rssi_ext1;
	int8_t ts_rssi_ext2;
	u8 qid;
	u16 desc_id;
	u8 tid;
	struct_group(ba,
		u32 ba_low;
		u32 ba_high;
	);
	u32 evm0;
	u32 evm1;
	u32 evm2;
	u32 duration;
};

struct ath_rx_status {
	u32 rs_tstamp;
	u16 rs_datalen;
	u8 rs_status;
	u8 rs_phyerr;
	int8_t rs_rssi;
	u8 rs_keyix;
	u8 rs_rate;
	u8 rs_antenna;
	u8 rs_more;
	int8_t rs_rssi_ctl[3];
	int8_t rs_rssi_ext[3];
	u8 rs_isaggr;
	u8 rs_firstaggr;
	u8 rs_moreaggr;
	u8 rs_num_delims;
	u8 rs_flags;
	bool is_mybeacon;
	u32 evm0;
	u32 evm1;
	u32 evm2;
	u32 evm3;
	u32 evm4;
	u16 enc_flags;
	enum rate_info_bw bw;
};

struct ath_htc_rx_status {
	__be64 rs_tstamp;
	__be16 rs_datalen;
	u8 rs_status;
	u8 rs_phyerr;
	int8_t rs_rssi;
	int8_t rs_rssi_ctl[3];
	int8_t rs_rssi_ext[3];
	u8 rs_keyix;
	u8 rs_rate;
	u8 rs_antenna;
	u8 rs_more;
	u8 rs_isaggr;
	u8 rs_moreaggr;
	u8 rs_num_delims;
	u8 rs_flags;
	u8 rs_dummy;
	/* FIXME: evm* never used? */
	__be32 evm0;
	__be32 evm1;
	__be32 evm2;
};

#define ATH9K_RXERR_CRC           0x01
#define ATH9K_RXERR_PHY           0x02
#define ATH9K_RXERR_FIFO          0x04
#define ATH9K_RXERR_DECRYPT       0x08
#define ATH9K_RXERR_MIC           0x10
#define ATH9K_RXERR_KEYMISS       0x20
#define ATH9K_RXERR_CORRUPT_DESC  0x40

#define ATH9K_RX_MORE             0x01
#define ATH9K_RX_MORE_AGGR        0x02
#define ATH9K_RX_GI               0x04
#define ATH9K_RX_2040             0x08
#define ATH9K_RX_DELIM_CRC_PRE    0x10
#define ATH9K_RX_DELIM_CRC_POST   0x20
#define ATH9K_RX_DECRYPT_BUSY     0x40

#define ATH9K_RXKEYIX_INVALID	((u8)-1)
#define ATH9K_TXKEYIX_INVALID	((u8)-1)

enum ath9k_phyerr {
	ATH9K_PHYERR_UNDERRUN             = 0,  /* Transmit underrun */
	ATH9K_PHYERR_TIMING               = 1,  /* Timing error */
	ATH9K_PHYERR_PARITY               = 2,  /* Illegal parity */
	ATH9K_PHYERR_RATE                 = 3,  /* Illegal rate */
	ATH9K_PHYERR_LENGTH               = 4,  /* Illegal length */
	ATH9K_PHYERR_RADAR                = 5,  /* Radar detect */
	ATH9K_PHYERR_SERVICE              = 6,  /* Illegal service */
	ATH9K_PHYERR_TOR                  = 7,  /* Transmit override receive */

	ATH9K_PHYERR_OFDM_TIMING          = 17,
	ATH9K_PHYERR_OFDM_SIGNAL_PARITY   = 18,
	ATH9K_PHYERR_OFDM_RATE_ILLEGAL    = 19,
	ATH9K_PHYERR_OFDM_LENGTH_ILLEGAL  = 20,
	ATH9K_PHYERR_OFDM_POWER_DROP      = 21,
	ATH9K_PHYERR_OFDM_SERVICE         = 22,
	ATH9K_PHYERR_OFDM_RESTART         = 23,

	ATH9K_PHYERR_CCK_BLOCKER          = 24,
	ATH9K_PHYERR_CCK_TIMING           = 25,
	ATH9K_PHYERR_CCK_HEADER_CRC       = 26,
	ATH9K_PHYERR_CCK_RATE_ILLEGAL     = 27,
	ATH9K_PHYERR_CCK_LENGTH_ILLEGAL   = 28,
	ATH9K_PHYERR_CCK_POWER_DROP       = 29,
	ATH9K_PHYERR_CCK_SERVICE          = 30,
	ATH9K_PHYERR_CCK_RESTART          = 31,

	ATH9K_PHYERR_HT_CRC_ERROR         = 32,
	ATH9K_PHYERR_HT_LENGTH_ILLEGAL    = 33,
	ATH9K_PHYERR_HT_RATE_ILLEGAL      = 34,
	ATH9K_PHYERR_HT_ZLF               = 35,

	ATH9K_PHYERR_FALSE_RADAR_EXT      = 36,
	ATH9K_PHYERR_GREEN_FIELD          = 37,
	ATH9K_PHYERR_SPECTRAL             = 38,

	ATH9K_PHYERR_MAX                  = 39,
};

struct ath_desc {
	u32 ds_link;
	u32 ds_data;
	u32 ds_ctl0;
	u32 ds_ctl1;
	u32 ds_hw[20];
	void *ds_vdata;
} __packed __aligned(4);

#define ATH9K_TXDESC_NOACK		0x0002
#define ATH9K_TXDESC_RTSENA		0x0004
#define ATH9K_TXDESC_CTSENA		0x0008
/* ATH9K_TXDESC_INTREQ forces a tx interrupt to be generated for
 * the descriptor its marked on.  We take a tx interrupt to reap
 * descriptors when the h/w hits an EOL condition or
 * when the descriptor is specifically marked to generate
 * an interrupt with this flag. Descriptors should be
 * marked periodically to insure timely replenishing of the
 * supply needed for sending frames. Deferring interrupts
 * reduces system load and potentially allows more concurrent
 * work to be done but if done to aggressively can cause
 * senders to backup. When the hardware queue is left too
 * large rate control information may also be too out of
 * date. An Alternative for this is TX interrupt mitigation
 * but this needs more testing. */
#define ATH9K_TXDESC_INTREQ		0x0010
#define ATH9K_TXDESC_VEOL		0x0020
#define ATH9K_TXDESC_EXT_ONLY		0x0040
#define ATH9K_TXDESC_EXT_AND_CTL	0x0080
#define ATH9K_TXDESC_VMF		0x0100
#define ATH9K_TXDESC_FRAG_IS_ON 	0x0200
#define ATH9K_TXDESC_LOWRXCHAIN		0x0400
#define ATH9K_TXDESC_LDPC		0x0800
#define ATH9K_TXDESC_CLRDMASK		0x1000

#define ATH9K_TXDESC_PAPRD		0x70000
#define ATH9K_TXDESC_PAPRD_S		16

#define ATH9K_RXDESC_INTREQ		0x0020

struct ar5416_desc {
	u32 ds_link;
	u32 ds_data;
	u32 ds_ctl0;
	u32 ds_ctl1;
	union {
		struct {
			u32 ctl2;
			u32 ctl3;
			u32 ctl4;
			u32 ctl5;
			u32 ctl6;
			u32 ctl7;
			u32 ctl8;
			u32 ctl9;
			u32 ctl10;
			u32 ctl11;
			u32 status0;
			u32 status1;
			u32 status2;
			u32 status3;
			u32 status4;
			u32 status5;
			u32 status6;
			u32 status7;
			u32 status8;
			u32 status9;
		} tx;
		struct {
			u32 status0;
			u32 status1;
			u32 status2;
			u32 status3;
			u32 status4;
			u32 status5;
			u32 status6;
			u32 status7;
			u32 status8;
		} rx;
	} u;
} __packed __aligned(4);

#define AR5416DESC(_ds)         ((struct ar5416_desc *)(_ds))
#define AR5416DESC_CONST(_ds)   ((const struct ar5416_desc *)(_ds))

#define ds_ctl2     u.tx.ctl2
#define ds_ctl3     u.tx.ctl3
#define ds_ctl4     u.tx.ctl4
#define ds_ctl5     u.tx.ctl5
#define ds_ctl6     u.tx.ctl6
#define ds_ctl7     u.tx.ctl7
#define ds_ctl8     u.tx.ctl8
#define ds_ctl9     u.tx.ctl9
#define ds_ctl10    u.tx.ctl10
#define ds_ctl11    u.tx.ctl11

#define ds_txstatus0    u.tx.status0
#define ds_txstatus1    u.tx.status1
#define ds_txstatus2    u.tx.status2
#define ds_txstatus3    u.tx.status3
#define ds_txstatus4    u.tx.status4
#define ds_txstatus5    u.tx.status5
#define ds_txstatus6    u.tx.status6
#define ds_txstatus7    u.tx.status7
#define ds_txstatus8    u.tx.status8
#define ds_txstatus9    u.tx.status9

#define ds_rxstatus0    u.rx.status0
#define ds_rxstatus1    u.rx.status1
#define ds_rxstatus2    u.rx.status2
#define ds_rxstatus3    u.rx.status3
#define ds_rxstatus4    u.rx.status4
#define ds_rxstatus5    u.rx.status5
#define ds_rxstatus6    u.rx.status6
#define ds_rxstatus7    u.rx.status7
#define ds_rxstatus8    u.rx.status8

#define AR_FrameLen         0x00000fff
#define AR_VirtMoreFrag     0x00001000
#define AR_TxCtlRsvd00      0x0000e000
#define AR_XmitPower0       0x003f0000
#define AR_XmitPower0_S     16
#define AR_XmitPower1	    0x3f000000
#define AR_XmitPower1_S     24
#define AR_XmitPower2	    0x3f000000
#define AR_XmitPower2_S     24
#define AR_XmitPower3	    0x3f000000
#define AR_XmitPower3_S     24
#define AR_RTSEnable        0x00400000
#define AR_VEOL             0x00800000
#define AR_ClrDestMask      0x01000000
#define AR_TxCtlRsvd01      0x1e000000
#define AR_TxIntrReq        0x20000000
#define AR_DestIdxValid     0x40000000
#define AR_CTSEnable        0x80000000

#define AR_TxMore           0x00001000
#define AR_DestIdx          0x000fe000
#define AR_DestIdx_S        13
#define AR_FrameType        0x00f00000
#define AR_FrameType_S      20
#define AR_NoAck            0x01000000
#define AR_InsertTS         0x02000000
#define AR_CorruptFCS       0x04000000
#define AR_ExtOnly          0x08000000
#define AR_ExtAndCtl        0x10000000
#define AR_MoreAggr         0x20000000
#define AR_IsAggr           0x40000000

#define AR_BurstDur         0x00007fff
#define AR_BurstDur_S       0
#define AR_DurUpdateEna     0x00008000
#define AR_XmitDataTries0   0x000f0000
#define AR_XmitDataTries0_S 16
#define AR_XmitDataTries1   0x00f00000
#define AR_XmitDataTries1_S 20
#define AR_XmitDataTries2   0x0f000000
#define AR_XmitDataTries2_S 24
#define AR_XmitDataTries3   0xf0000000
#define AR_XmitDataTries3_S 28

#define AR_XmitRate0        0x000000ff
#define AR_XmitRate0_S      0
#define AR_XmitRate1        0x0000ff00
#define AR_XmitRate1_S      8
#define AR_XmitRate2        0x00ff0000
#define AR_XmitRate2_S      16
#define AR_XmitRate3        0xff000000
#define AR_XmitRate3_S      24

#define AR_PacketDur0       0x00007fff
#define AR_PacketDur0_S     0
#define AR_RTSCTSQual0      0x00008000
#define AR_PacketDur1       0x7fff0000
#define AR_PacketDur1_S     16
#define AR_RTSCTSQual1      0x80000000

#define AR_PacketDur2       0x00007fff
#define AR_PacketDur2_S     0
#define AR_RTSCTSQual2      0x00008000
#define AR_PacketDur3       0x7fff0000
#define AR_PacketDur3_S     16
#define AR_RTSCTSQual3      0x80000000

#define AR_AggrLen          0x0000ffff
#define AR_AggrLen_S        0
#define AR_TxCtlRsvd60      0x00030000
#define AR_PadDelim         0x03fc0000
#define AR_PadDelim_S       18
#define AR_EncrType         0x0c000000
#define AR_EncrType_S       26
#define AR_TxCtlRsvd61      0xf0000000
#define AR_LDPC             0x80000000

#define AR_2040_0           0x00000001
#define AR_GI0              0x00000002
#define AR_ChainSel0        0x0000001c
#define AR_ChainSel0_S      2
#define AR_2040_1           0x00000020
#define AR_GI1              0x00000040
#define AR_ChainSel1        0x00000380
#define AR_ChainSel1_S      7
#define AR_2040_2           0x00000400
#define AR_GI2              0x00000800
#define AR_ChainSel2        0x00007000
#define AR_ChainSel2_S      12
#define AR_2040_3           0x00008000
#define AR_GI3              0x00010000
#define AR_ChainSel3        0x000e0000
#define AR_ChainSel3_S      17
#define AR_RTSCTSRate       0x0ff00000
#define AR_RTSCTSRate_S     20
#define AR_STBC0            0x10000000
#define AR_STBC1            0x20000000
#define AR_STBC2            0x40000000
#define AR_STBC3            0x80000000

#define AR_TxRSSIAnt00      0x000000ff
#define AR_TxRSSIAnt00_S    0
#define AR_TxRSSIAnt01      0x0000ff00
#define AR_TxRSSIAnt01_S    8
#define AR_TxRSSIAnt02      0x00ff0000
#define AR_TxRSSIAnt02_S    16
#define AR_TxStatusRsvd00   0x3f000000
#define AR_TxBaStatus       0x40000000
#define AR_TxStatusRsvd01   0x80000000

/*
 * AR_FrmXmitOK - Frame transmission success flag. If set, the frame was
 * transmitted successfully. If clear, no ACK or BA was received to indicate
 * successful transmission when we were expecting an ACK or BA.
 */
#define AR_FrmXmitOK            0x00000001
#define AR_ExcessiveRetries     0x00000002
#define AR_FIFOUnderrun         0x00000004
#define AR_Filtered             0x00000008
#define AR_RTSFailCnt           0x000000f0
#define AR_RTSFailCnt_S         4
#define AR_DataFailCnt          0x00000f00
#define AR_DataFailCnt_S        8
#define AR_VirtRetryCnt         0x0000f000
#define AR_VirtRetryCnt_S       12
#define AR_TxDelimUnderrun      0x00010000
#define AR_TxDataUnderrun       0x00020000
#define AR_DescCfgErr           0x00040000
#define AR_TxTimerExpired       0x00080000
#define AR_TxStatusRsvd10       0xfff00000

#define AR_SendTimestamp    ds_txstatus2
#define AR_BaBitmapLow      ds_txstatus3
#define AR_BaBitmapHigh     ds_txstatus4

#define AR_TxRSSIAnt10      0x000000ff
#define AR_TxRSSIAnt10_S    0
#define AR_TxRSSIAnt11      0x0000ff00
#define AR_TxRSSIAnt11_S    8
#define AR_TxRSSIAnt12      0x00ff0000
#define AR_TxRSSIAnt12_S    16
#define AR_TxRSSICombined   0xff000000
#define AR_TxRSSICombined_S 24

#define AR_TxTid	0xf0000000
#define AR_TxTid_S	28

#define AR_TxEVM0           ds_txstatus5
#define AR_TxEVM1           ds_txstatus6
#define AR_TxEVM2           ds_txstatus7

#define AR_TxDone           0x00000001
#define AR_SeqNum           0x00001ffe
#define AR_SeqNum_S         1
#define AR_TxStatusRsvd80   0x0001e000
#define AR_TxOpExceeded     0x00020000
#define AR_TxStatusRsvd81   0x001c0000
#define AR_FinalTxIdx       0x00600000
#define AR_FinalTxIdx_S     21
#define AR_TxStatusRsvd82   0x01800000
#define AR_PowerMgmt        0x02000000
#define AR_TxStatusRsvd83   0xfc000000

#define AR_RxCTLRsvd00  0xffffffff

#define AR_RxCtlRsvd00  0x00001000
#define AR_RxIntrReq    0x00002000
#define AR_RxCtlRsvd01  0xffffc000

#define AR_RxRSSIAnt00      0x000000ff
#define AR_RxRSSIAnt00_S    0
#define AR_RxRSSIAnt01      0x0000ff00
#define AR_RxRSSIAnt01_S    8
#define AR_RxRSSIAnt02      0x00ff0000
#define AR_RxRSSIAnt02_S    16
#define AR_RxRate           0xff000000
#define AR_RxRate_S         24
#define AR_RxStatusRsvd00   0xff000000

#define AR_DataLen          0x00000fff
#define AR_RxMore           0x00001000
#define AR_NumDelim         0x003fc000
#define AR_NumDelim_S       14
#define AR_RxStatusRsvd10   0xff800000

#define AR_RcvTimestamp     ds_rxstatus2

#define AR_GI               0x00000001
#define AR_2040             0x00000002
#define AR_Parallel40       0x00000004
#define AR_Parallel40_S     2
#define AR_STBC             0x00000008 /* on ar9280 and later */
#define AR_RxStatusRsvd30   0x000000f0
#define AR_RxAntenna	    0xffffff00
#define AR_RxAntenna_S	    8

#define AR_RxRSSIAnt10            0x000000ff
#define AR_RxRSSIAnt10_S          0
#define AR_RxRSSIAnt11            0x0000ff00
#define AR_RxRSSIAnt11_S          8
#define AR_RxRSSIAnt12            0x00ff0000
#define AR_RxRSSIAnt12_S          16
#define AR_RxRSSICombined         0xff000000
#define AR_RxRSSICombined_S       24

#define AR_RxEVM0           ds_rxstatus4
#define AR_RxEVM1           ds_rxstatus5
#define AR_RxEVM2           ds_rxstatus6

#define AR_RxDone           0x00000001
#define AR_RxFrameOK        0x00000002
#define AR_CRCErr           0x00000004
#define AR_DecryptCRCErr    0x00000008
#define AR_PHYErr           0x00000010
#define AR_MichaelErr       0x00000020
#define AR_PreDelimCRCErr   0x00000040
#define AR_RxStatusRsvd70   0x00000080
#define AR_RxKeyIdxValid    0x00000100
#define AR_KeyIdx           0x0000fe00
#define AR_KeyIdx_S         9
#define AR_PHYErrCode       0x0000ff00
#define AR_PHYErrCode_S     8
#define AR_RxMoreAggr       0x00010000
#define AR_RxAggr           0x00020000
#define AR_PostDelimCRCErr  0x00040000
#define AR_RxStatusRsvd71   0x3ff80000
#define AR_RxFirstAggr      0x20000000
#define AR_DecryptBusyErr   0x40000000
#define AR_KeyMiss          0x80000000

enum ath9k_tx_queue {
	ATH9K_TX_QUEUE_INACTIVE = 0,
	ATH9K_TX_QUEUE_DATA,
	ATH9K_TX_QUEUE_BEACON,
	ATH9K_TX_QUEUE_CAB,
	ATH9K_TX_QUEUE_UAPSD,
	ATH9K_TX_QUEUE_PSPOLL
};

#define	ATH9K_NUM_TX_QUEUES 10

/* Used as a queue subtype instead of a WMM AC */
#define ATH9K_WME_UPSD	4

enum ath9k_tx_queue_flags {
	TXQ_FLAG_TXINT_ENABLE = 0x0001,
	TXQ_FLAG_TXDESCINT_ENABLE = 0x0002,
	TXQ_FLAG_TXEOLINT_ENABLE = 0x0004,
	TXQ_FLAG_TXURNINT_ENABLE = 0x0008,
	TXQ_FLAG_BACKOFF_DISABLE = 0x0010,
	TXQ_FLAG_COMPRESSION_ENABLE = 0x0020,
	TXQ_FLAG_RDYTIME_EXP_POLICY_ENABLE = 0x0040,
	TXQ_FLAG_FRAG_BURST_BACKOFF_ENABLE = 0x0080,
};

#define ATH9K_TXQ_USEDEFAULT ((u32) -1)
#define ATH9K_TXQ_USE_LOCKOUT_BKOFF_DIS 0x00000001

#define ATH9K_DECOMP_MASK_SIZE     128

enum ath9k_pkt_type {
	ATH9K_PKT_TYPE_NORMAL = 0,
	ATH9K_PKT_TYPE_ATIM,
	ATH9K_PKT_TYPE_PSPOLL,
	ATH9K_PKT_TYPE_BEACON,
	ATH9K_PKT_TYPE_PROBE_RESP,
	ATH9K_PKT_TYPE_CHIRP,
	ATH9K_PKT_TYPE_GRP_POLL,
};

struct ath9k_tx_queue_info {
	u32 tqi_ver;
	enum ath9k_tx_queue tqi_type;
	int tqi_subtype;
	enum ath9k_tx_queue_flags tqi_qflags;
	u32 tqi_priority;
	u32 tqi_aifs;
	u32 tqi_cwmin;
	u32 tqi_cwmax;
	u16 tqi_shretry;
	u16 tqi_lgretry;
	u32 tqi_cbrPeriod;
	u32 tqi_cbrOverflowLimit;
	u32 tqi_burstTime;
	u32 tqi_readyTime;
	u32 tqi_physCompBuf;
	u32 tqi_intFlags;
};

enum ath9k_rx_filter {
	ATH9K_RX_FILTER_UCAST = 0x00000001,
	ATH9K_RX_FILTER_MCAST = 0x00000002,
	ATH9K_RX_FILTER_BCAST = 0x00000004,
	ATH9K_RX_FILTER_CONTROL = 0x00000008,
	ATH9K_RX_FILTER_BEACON = 0x00000010,
	ATH9K_RX_FILTER_PROM = 0x00000020,
	ATH9K_RX_FILTER_PROBEREQ = 0x00000080,
	ATH9K_RX_FILTER_PHYERR = 0x00000100,
	ATH9K_RX_FILTER_MYBEACON = 0x00000200,
	ATH9K_RX_FILTER_COMP_BAR = 0x00000400,
	ATH9K_RX_FILTER_COMP_BA = 0x00000800,
	ATH9K_RX_FILTER_UNCOMP_BA_BAR = 0x00001000,
	ATH9K_RX_FILTER_PSPOLL = 0x00004000,
	ATH9K_RX_FILTER_PHYRADAR = 0x00002000,
	ATH9K_RX_FILTER_MCAST_BCAST_ALL = 0x00008000,
	ATH9K_RX_FILTER_CONTROL_WRAPPER = 0x00080000,
	ATH9K_RX_FILTER_4ADDRESS = 0x00100000,
};

#define ATH9K_RATESERIES_RTS_CTS  0x0001
#define ATH9K_RATESERIES_2040     0x0002
#define ATH9K_RATESERIES_HALFGI   0x0004
#define ATH9K_RATESERIES_STBC     0x0008

struct ath9k_11n_rate_series {
	u32 Tries;
	u32 Rate;
	u32 PktDuration;
	u32 ChSel;
	u32 RateFlags;
};

enum aggr_type {
	AGGR_BUF_NONE,
	AGGR_BUF_FIRST,
	AGGR_BUF_MIDDLE,
	AGGR_BUF_LAST,
};

enum ath9k_key_type {
	ATH9K_KEY_TYPE_CLEAR,
	ATH9K_KEY_TYPE_WEP,
	ATH9K_KEY_TYPE_AES,
	ATH9K_KEY_TYPE_TKIP,
};

struct ath_tx_info {
	u8 qcu;

	bool is_first;
	bool is_last;

	enum aggr_type aggr;
	u8 ndelim;
	u16 aggr_len;

	dma_addr_t link;
	int pkt_len;
	u32 flags;

	dma_addr_t buf_addr[4];
	int buf_len[4];

	struct ath9k_11n_rate_series rates[4];
	u8 rtscts_rate;
	bool dur_update;

	enum ath9k_pkt_type type;
	enum ath9k_key_type keytype;
	u8 keyix;
	u8 txpower[4];
};

struct ath_hw;
struct ath9k_channel;
enum ath9k_int;

u32 ath9k_hw_gettxbuf(struct ath_hw *ah, u32 q);
void ath9k_hw_puttxbuf(struct ath_hw *ah, u32 q, u32 txdp);
void ath9k_hw_txstart(struct ath_hw *ah, u32 q);
u32 ath9k_hw_numtxpending(struct ath_hw *ah, u32 q);
bool ath9k_hw_updatetxtriglevel(struct ath_hw *ah, bool bIncTrigLevel);
bool ath9k_hw_stop_dma_queue(struct ath_hw *ah, u32 q);
void ath9k_hw_abort_tx_dma(struct ath_hw *ah);
bool ath9k_hw_set_txq_props(struct ath_hw *ah, int q,
			    const struct ath9k_tx_queue_info *qinfo);
bool ath9k_hw_get_txq_props(struct ath_hw *ah, int q,
			    struct ath9k_tx_queue_info *qinfo);
int ath9k_hw_setuptxqueue(struct ath_hw *ah, enum ath9k_tx_queue type,
			  const struct ath9k_tx_queue_info *qinfo);
bool ath9k_hw_releasetxqueue(struct ath_hw *ah, u32 q);
bool ath9k_hw_resettxqueue(struct ath_hw *ah, u32 q);
int ath9k_hw_rxprocdesc(struct ath_hw *ah, struct ath_desc *ds,
			struct ath_rx_status *rs);
void ath9k_hw_setuprxdesc(struct ath_hw *ah, struct ath_desc *ds,
			  u32 size, u32 flags);
bool ath9k_hw_setrxabort(struct ath_hw *ah, bool set);
void ath9k_hw_putrxbuf(struct ath_hw *ah, u32 rxdp);
void ath9k_hw_startpcureceive(struct ath_hw *ah, bool is_scanning);
void ath9k_hw_abortpcurecv(struct ath_hw *ah);
bool ath9k_hw_stopdmarecv(struct ath_hw *ah, bool *reset);
int ath9k_hw_beaconq_setup(struct ath_hw *ah);
void ath9k_hw_set_tx_filter(struct ath_hw *ah, u8 destidx, bool set);

/* Interrupt Handling */
bool ath9k_hw_intrpend(struct ath_hw *ah);
void ath9k_hw_set_interrupts(struct ath_hw *ah);
void ath9k_hw_enable_interrupts(struct ath_hw *ah);
void ath9k_hw_disable_interrupts(struct ath_hw *ah);
void ath9k_hw_kill_interrupts(struct ath_hw *ah);
void ath9k_hw_resume_interrupts(struct ath_hw *ah);

void ar9002_hw_attach_mac_ops(struct ath_hw *ah);

#endif /* MAC_H */
