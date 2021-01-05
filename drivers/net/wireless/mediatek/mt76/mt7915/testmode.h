// SPDX-License-Identifier: ISC
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

struct mt7915_tm_cmd {
	u8 testmode_en;
	u8 param_idx;
	u8 _rsv[2];
	union {
		__le32 data;
		struct mt7915_tm_trx trx;
		struct mt7915_tm_freq_offset freq;
		struct mt7915_tm_slot_time slot;
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

#endif
