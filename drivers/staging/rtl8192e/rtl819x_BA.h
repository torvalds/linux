/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef _BATYPE_H_
#define _BATYPE_H_

#define	BA_SETUP_TIMEOUT	200

#define	BA_POLICY_DELAYED		0
#define	BA_POLICY_IMMEDIATE	1

#define	ADDBA_STATUS_SUCCESS			0
#define	ADDBA_STATUS_REFUSED		37
#define	ADDBA_STATUS_INVALID_PARAM	38

#define	DELBA_REASON_END_BA			37
#define	DELBA_REASON_UNKNOWN_BA	38
#define	DELBA_REASON_TIMEOUT			39
union sequence_control {
	u16 ShortData;
	struct {
		u16	FragNum:4;
		u16	SeqNum:12;
	} field;
};

union ba_param_set {
	u8 charData[2];
	u16 shortData;
	struct {
		u16 AMSDU_Support:1;
		u16 BAPolicy:1;
		u16 TID:4;
		u16 BufferSize:10;
	} field;
};

union delba_param_set {
	u8 charData[2];
	u16 shortData;
	struct {
		u16 Reserved:11;
		u16 Initiator:1;
		u16 TID:4;
	} field;
};

struct ba_record {
	struct timer_list		Timer;
	u8				bValid;
	u8				DialogToken;
	union ba_param_set BaParamSet;
	u16				BaTimeoutValue;
	union sequence_control BaStartSeqCtrl;
};

#endif
