/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TSTYPE_H_
#define _TSTYPE_H_
#include "rtl819x_Qos.h"

#define TS_ADDBA_DELAY		60

#define TOTAL_TS_NUM		16
#define TCLAS_NUM		4

/*  This define the Tx/Rx directions */
enum tr_select {
	TX_DIR = 0,
	RX_DIR = 1,
};

typedef union _QOS_TCLAS {

	struct _TYPE_GENERAL {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
	} TYPE_GENERAL;

	struct _TYPE0_ETH {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u8		SrcAddr[6];
		u8		DstAddr[6];
		u16		Type;
	} TYPE0_ETH;

	struct _TYPE1_IPV4 {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u8		Version;
		u8		SrcIP[4];
		u8		DstIP[4];
		u16		SrcPort;
		u16		DstPort;
		u8		DSCP;
		u8		Protocol;
		u8		Reserved;
	} TYPE1_IPV4;

	struct _TYPE1_IPV6 {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u8		Version;
		u8		SrcIP[16];
		u8		DstIP[16];
		u16		SrcPort;
		u16		DstPort;
		u8		FlowLabel[3];
	} TYPE1_IPV6;

	struct _TYPE2_8021Q {
		u8		Priority;
		u8		ClassifierType;
		u8		Mask;
		u16		TagType;
	} TYPE2_8021Q;
} QOS_TCLAS, *PQOS_TCLAS;

struct ts_common_info {
	struct list_head		list;
	struct timer_list		setup_timer;
	struct timer_list		inact_timer;
	u8				addr[6];
	struct tspec_body		t_spec;
	QOS_TCLAS			t_class[TCLAS_NUM];
	u8				t_clas_proc;
	u8				t_clas_num;
};

struct tx_ts_record {
	struct ts_common_info		ts_common_info;
	u16				tx_cur_seq;
	BA_RECORD			tx_pending_ba_record;	/*  For BA Originator */
	BA_RECORD			tx_admitted_ba_record;	/*  For BA Originator */
	u8				add_ba_req_in_progress;
	u8				add_ba_req_delayed;
	u8				using_ba;
	struct timer_list		ts_add_ba_timer;
	u8				num;
};

struct rx_ts_record {
	struct ts_common_info		ts_common_info;
	u16				rx_indicate_seq;
	u16				rx_timeout_indicate_seq;
	struct list_head		rx_pending_pkt_list;
	struct timer_list		rx_pkt_pending_timer;
	BA_RECORD			rx_admitted_ba_record;	 /*  For BA Recipient */
	u16				rx_last_seq_num;
	u8				rx_last_frag_num;
	u8				num;
};

#endif
