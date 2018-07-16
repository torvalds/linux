/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TSTYPE_H_
#define _TSTYPE_H_
#include "rtl819x_Qos.h"
#define TS_SETUP_TIMEOUT	60  /*  In millisecond */
#define TS_INACT_TIMEOUT	60
#define TS_ADDBA_DELAY		60

#define TOTAL_TS_NUM		16
#define TCLAS_NUM		4

/*  This define the Tx/Rx directions */
enum tr_select {
	TX_DIR = 0,
	RX_DIR = 1,
};

typedef struct _TS_COMMON_INFO {
	struct list_head		List;
	struct timer_list		SetupTimer;
	struct timer_list		InactTimer;
	u8				Addr[6];
	TSPEC_BODY			TSpec;
	QOS_TCLAS			TClass[TCLAS_NUM];
	u8				TClasProc;
	u8				TClasNum;
} TS_COMMON_INFO, *PTS_COMMON_INFO;

typedef struct _TX_TS_RECORD {
	TS_COMMON_INFO		TsCommonInfo;
	u16				TxCurSeq;
	BA_RECORD			TxPendingBARecord;	/*  For BA Originator */
	BA_RECORD			TxAdmittedBARecord;	/*  For BA Originator */
/* 	QOS_DL_RECORD		DLRecord; */
	u8				bAddBaReqInProgress;
	u8				bAddBaReqDelayed;
	u8				bUsingBa;
	struct timer_list		TsAddBaTimer;
	u8				num;
} TX_TS_RECORD, *PTX_TS_RECORD;

typedef struct _RX_TS_RECORD {
	TS_COMMON_INFO		TsCommonInfo;
	u16				RxIndicateSeq;
	u16				RxTimeoutIndicateSeq;
	struct list_head		RxPendingPktList;
	struct timer_list		RxPktPendingTimer;
	BA_RECORD			RxAdmittedBARecord;	 /*  For BA Recipient */
	u16				RxLastSeqNum;
	u8				RxLastFragNum;
	u8				num;
/* 	QOS_DL_RECORD		DLRecord; */
} RX_TS_RECORD, *PRX_TS_RECORD;


#endif
