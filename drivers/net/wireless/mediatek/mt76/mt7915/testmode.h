/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7915_TESTMODE_H
#define __MT7915_TESTMODE_H

struct mt7915_tm_trx {
	u8 type;
	u8 enable;
	u8 band;
	u8 rsv;
};

struct mt7915_tm_freq_offset {
	u8 band;
	__le32 freq_offset;
};

struct mt7915_tm_slot_time {
	u8 slot_time;
	u8 sifs;
	u8 rifs;
	u8 _rsv;
	__le16 eifs;
	u8 band;
	u8 _rsv1[5];
};

struct mt7915_tm_clean_txq {
	bool sta_pause;
	u8 wcid;	/* 256 sta */
	u8 band;
	u8 rsv;
};

struct mt7915_tm_cmd {
	u8 testmode_en;
	u8 param_idx;
	u8 _rsv[2];
	union {
		__le32 data;
		struct mt7915_tm_trx trx;
		struct mt7915_tm_freq_offset freq;
		struct mt7915_tm_slot_time slot;
		struct mt7915_tm_clean_txq clean;
		u8 test[72];
	} param;
} __packed;

enum {
	TM_MAC_TX = 1,
	TM_MAC_RX,
	TM_MAC_TXRX,
	TM_MAC_TXRX_RXV,
	TM_MAC_RXV,
	TM_MAC_RX_RXV,
};

struct tm_tx_cont {
	u8 control_ch;
	u8 center_ch;
	u8 bw;
	u8 tx_ant;
	__le16 rateval;
	u8 band;
	u8 txfd_mode;
};

struct mt7915_tm_rf_test {
	u8 action;
	u8 icap_len;
	u8 _rsv[2];
	union {
		__le32 op_mode;
		__le32 freq;

		struct {
			__le32 func_idx;
			union {
				__le32 func_data;
				__le32 cal_dump;

				struct tm_tx_cont tx_cont;

				u8 _pad[80];
			} param;
		} rf;
	} op;
} __packed;

enum {
	RF_OPER_NORMAL,
	RF_OPER_RF_TEST,
	RF_OPER_ICAP,
	RF_OPER_ICAP_OVERLAP,
	RF_OPER_WIFI_SPECTRUM,
};

#endif
