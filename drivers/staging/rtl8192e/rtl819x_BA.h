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
	u16 short_data;
	struct {
		u16	frag_num:4;
		u16	seq_num:12;
	} field;
};

union ba_param_set {
	u8 char_data[2];
	u16 short_data;
	struct {
		u16 amsdu_support:1;
		u16 ba_policy:1;
		u16 tid:4;
		u16 buffer_size:10;
	} field;
};

union delba_param_set {
	u8 char_data[2];
	u16 short_data;
	struct {
		u16 reserved:11;
		u16 initiator:1;
		u16 tid:4;
	} field;
};

struct ba_record {
	struct timer_list timer;
	u8 b_valid;
	u8 dialog_token;
	union ba_param_set ba_param_set;
	u16 ba_timeout_value;
	union sequence_control ba_start_seq_ctrl;
};

#endif
