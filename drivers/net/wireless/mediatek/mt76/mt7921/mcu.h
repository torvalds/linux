/* SPDX-License-Identifier: ISC */
/* Copyright (C) 2020 MediaTek Inc. */

#ifndef __MT7921_MCU_H
#define __MT7921_MCU_H

#include "../mt76_connac_mcu.h"

struct mt7921_mcu_tx_done_event {
	u8 pid;
	u8 status;
	__le16 seq;

	u8 wlan_idx;
	u8 tx_cnt;
	__le16 tx_rate;

	u8 flag;
	u8 tid;
	u8 rsp_rate;
	u8 mcs;

	u8 bw;
	u8 tx_pwr;
	u8 reason;
	u8 rsv0[1];

	__le32 delay;
	__le32 timestamp;
	__le32 applied_flag;
	u8 txs[28];

	u8 rsv1[32];
} __packed;

/* ext event table */
enum {
	MCU_EXT_EVENT_RATE_REPORT = 0x87,
};

struct mt7921_mcu_eeprom_info {
	__le32 addr;
	__le32 valid;
	u8 data[MT7921_EEPROM_BLOCK_SIZE];
} __packed;

#define MT_RA_RATE_NSS			GENMASK(8, 6)
#define MT_RA_RATE_MCS			GENMASK(3, 0)
#define MT_RA_RATE_TX_MODE		GENMASK(12, 9)
#define MT_RA_RATE_DCM_EN		BIT(4)
#define MT_RA_RATE_BW			GENMASK(14, 13)

enum {
	MT_EBF = BIT(0),	/* explicit beamforming */
	MT_IBF = BIT(1)		/* implicit beamforming */
};

struct mt7921_mcu_ant_id_config {
	u8 ant_id[4];
} __packed;

struct mt7921_txpwr_req {
	u8 ver;
	u8 action;
	__le16 len;
	u8 dbdc_idx;
	u8 rsv[3];
} __packed;

struct mt7921_txpwr_event {
	u8 ver;
	u8 action;
	__le16 len;
	struct mt7921_txpwr txpwr;
} __packed;

struct mt7921_wf_rf_pin_ctrl_event {
	u8 result;
	u8 value;
} __packed;

enum {
	TM_SWITCH_MODE,
	TM_SET_AT_CMD,
	TM_QUERY_AT_CMD,
};

enum {
	MT7921_TM_NORMAL,
	MT7921_TM_TESTMODE,
	MT7921_TM_ICAP,
	MT7921_TM_ICAP_OVERLAP,
	MT7921_TM_WIFISPECTRUM,
};

struct mt7921_rftest_cmd {
	u8 action;
	u8 rsv[3];
	__le32 param0;
	__le32 param1;
} __packed;

struct mt7921_rftest_evt {
	__le32 param0;
	__le32 param1;
} __packed;

struct mt7921_clc_info_tlv {
	__le16 tag;
	__le16 len;

	u8 chan_conf; /* BIT(0) : Enable UNII-4
		       * BIT(1) : Enable UNII-5
		       * BIT(2) : Enable UNII-6
		       * BIT(3) : Enable UNII-7
		       * BIT(4) : Enable UNII-8
		       */
	u8 rsv[63];
} __packed;
#endif
