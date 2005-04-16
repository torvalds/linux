/* $Id: message.h,v 1.1.10.1 2001/09/23 22:24:59 kai Exp $
 *
 * Copyright (C) 1996  SpellCaster Telecommunications Inc.
 *
 * structures, macros and defines useful for sending
 * messages to the adapter
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For more information, please contact gpl-info@spellcast.com or write:
 *
 *     SpellCaster Telecommunications Inc.
 *     5621 Finch Avenue East, Unit #3
 *     Scarborough, Ontario  Canada
 *     M1B 2T9
 *     +1 (416) 297-8565
 *     +1 (416) 297-6433 Facsimile
 */

/*
 * Board message macros, defines and structures
 */
 
#ifndef MESSAGE_H
#define MESSAGE_H

#define MAX_MESSAGES		32	/* Maximum messages that can be
					   queued */
#define MSG_DATA_LEN		48	/* Maximum size of message payload */
#define MSG_LEN			64	/* Size of a message */
#define CMPID			0	/* Loader message process ID */
#define CEPID			64	/* Firmware message process ID */

/*
 * Macro to determine if a message is a loader message
 */
#define IS_CM_MESSAGE(mesg, tx, cx, dx)		\
		((mesg.type == cmRspType##tx)		\
		&&(mesg.class == cmRspClass##cx)	\
		&&(mesg.code == cmRsp##dx))

/*
 * Macro to determine if a message is a firmware message
 */
#define IS_CE_MESSAGE(mesg, tx, cx, dx)		\
		((mesg.type == ceRspType##tx)		\
		&&(mesg.class == ceRspClass##cx)	\
		&&(mesg.code == ceRsp##tx##dx))

/* 
 * Loader Request and Response Messages
 */

/* message types */
#define cmReqType1			1
#define cmReqType2			2
#define cmRspType0			0
#define cmRspType1			1
#define cmRspType2			2
#define cmRspType5			5

/* message classes */
#define cmReqClass0			0
#define cmRspClass0			0

/* message codes */
#define cmReqHWConfig		1			/* 1,0,1 */
#define cmReqMsgLpbk		2			/* 1,0,2 */
#define cmReqVersion		3			/* 1,0,3 */
#define cmReqLoadProc		1			/* 2,0,1 */
#define cmReqStartProc		2			/* 2,0,2 */
#define cmReqReadMem		6			/* 2,0,6 */
#define cmRspHWConfig		cmReqHWConfig
#define	cmRspMsgLpbk		cmReqMsgLpbk
#define cmRspVersion		cmReqVersion
#define cmRspLoadProc		cmReqLoadProc
#define cmRspStartProc		cmReqStartProc
#define	cmRspReadMem		cmReqReadMem
#define cmRspMiscEngineUp	1			/* 5,0,1 */
#define cmRspInvalid		0			/* 0,0,0 */


/*
 * Firmware Request and Response Messages
 */

/* message types */
#define ceReqTypePhy		1
#define ceReqTypeLnk		2
#define ceReqTypeCall		3
#define ceReqTypeStat		1
#define ceRspTypeErr		0
#define	ceRspTypePhy		ceReqTypePhy
#define ceRspTypeLnk		ceReqTypeLnk
#define ceRspTypeCall		ceReqTypeCall
#define ceRspTypeStat		ceReqTypeStat

/* message classes */
#define ceReqClass0		0
#define ceReqClass1		1
#define ceReqClass2		2
#define ceReqClass3		3
#define ceRspClass0		ceReqClass0
#define ceRspClass1		ceReqClass1
#define ceRspClass2		ceReqClass2
#define ceRspClass3		ceReqClass3

/* message codes  (B) = BRI only, (P) = PRI only, (V) = POTS only */
#define ceReqPhyProcInfo	1			/* 1,0,1 */
#define ceReqPhyConnect		1			/* 1,1,1 */
#define ceReqPhyDisconnect	2			/* 1,1,2 */
#define ceReqPhySetParams	3			/* 1,1,3 (P) */
#define ceReqPhyGetParams	4			/* 1,1,4 (P) */
#define ceReqPhyStatus		1			/* 1,2,1 */
#define ceReqPhyAcfaStatus	2			/* 1,2,2 (P) */
#define ceReqPhyChCallState	3			/* 1,2,3 (P) */
#define ceReqPhyChServState	4			/* 1,2,4 (P) */
#define ceReqPhyRLoopBack	1			/* 1,3,1 */
#define ceRspPhyProcInfo	ceReqPhyProcInfo
#define	ceRspPhyConnect		ceReqPhyConnect
#define ceRspPhyDisconnect	ceReqPhyDisconnect
#define ceRspPhySetParams	ceReqPhySetParams
#define ceRspPhyGetParams	ceReqPhyGetParams
#define ceRspPhyStatus		ceReqPhyStatus
#define ceRspPhyAcfaStatus	ceReqPhyAcfaStatus
#define ceRspPhyChCallState	ceReqPhyChCallState
#define ceRspPhyChServState	ceReqPhyChServState
#define ceRspPhyRLoopBack	ceReqphyRLoopBack
#define ceReqLnkSetParam	1			/* 2,0,1 */
#define ceReqLnkGetParam	2			/* 2,0,2 */
#define ceReqLnkGetStats	3			/* 2,0,3 */
#define ceReqLnkWrite		1			/* 2,1,1 */
#define ceReqLnkRead		2			/* 2,1,2 */
#define ceReqLnkFlush		3			/* 2,1,3 */
#define ceReqLnkWrBufTrc	4			/* 2,1,4 */
#define ceReqLnkRdBufTrc	5			/* 2,1,5 */
#define ceRspLnkSetParam	ceReqLnkSetParam
#define ceRspLnkGetParam	ceReqLnkGetParam
#define ceRspLnkGetStats	ceReqLnkGetStats
#define ceRspLnkWrite		ceReqLnkWrite
#define ceRspLnkRead		ceReqLnkRead
#define ceRspLnkFlush		ceReqLnkFlush
#define ceRspLnkWrBufTrc	ceReqLnkWrBufTrc
#define ceRspLnkRdBufTrc	ceReqLnkRdBufTrc
#define ceReqCallSetSwitchType	1			/* 3,0,1 */
#define ceReqCallGetSwitchType	2			/* 3,0,2 */
#define ceReqCallSetFrameFormat	3			/* 3,0,3 */
#define ceReqCallGetFrameFormat	4			/* 3,0,4 */
#define ceReqCallSetCallType	5			/* 3,0,5 */
#define ceReqCallGetCallType	6			/* 3,0,6 */
#define ceReqCallSetSPID	7			/* 3,0,7 (!P) */
#define ceReqCallGetSPID	8			/* 3,0,8 (!P) */
#define ceReqCallSetMyNumber	9			/* 3,0,9 (!P) */
#define ceReqCallGetMyNumber	10			/* 3,0,10 (!P) */
#define	ceRspCallSetSwitchType	ceReqCallSetSwitchType
#define ceRspCallGetSwitchType	ceReqCallSetSwitchType
#define ceRspCallSetFrameFormat	ceReqCallSetFrameFormat
#define ceRspCallGetFrameFormat	ceReqCallGetFrameFormat
#define ceRspCallSetCallType	ceReqCallSetCallType
#define ceRspCallGetCallType	ceReqCallGetCallType
#define ceRspCallSetSPID	ceReqCallSetSPID
#define ceRspCallGetSPID	ceReqCallGetSPID
#define ceRspCallSetMyNumber	ceReqCallSetMyNumber
#define ceRspCallGetMyNumber	ceReqCallGetMyNumber
#define ceRspStatAcfaStatus	2
#define ceRspStat
#define ceRspErrError		0			/* 0,0,0 */

/*
 * Call Types
 */
#define CALLTYPE_64K		0
#define CALLTYPE_56K		1
#define CALLTYPE_SPEECH		2
#define CALLTYPE_31KHZ		3

/*
 * Link Level data contains a pointer to and the length of
 * a buffer in shared RAM. Used by LnkRead and LnkWrite message
 * types. Part of RspMsgStruct and ReqMsgStruct.
 */
typedef struct {
	unsigned long buff_offset;
	unsigned short msg_len;
} LLData;


/* 
 * Message payload template for an HWConfig message
 */
typedef struct {
	char st_u_sense;
	char powr_sense;
	char sply_sense;
	unsigned char asic_id;
	long ram_size;
	char serial_no[13];
	char part_no[13];
	char rev_no[2];
} HWConfig_pl;

/*
 * A Message
 */
struct message {
	unsigned char sequence_no;
	unsigned char process_id;
	unsigned char time_stamp;
	unsigned char cmd_sequence_no;	/* Rsp messages only */
	unsigned char reserved1[3];
	unsigned char msg_byte_cnt;
	unsigned char type;
	unsigned char class;
	unsigned char code;
	unsigned char phy_link_no;
	unsigned char rsp_status;	/* Rsp messages only */
	unsigned char reseved2[3];
	union {
		unsigned char byte_array[MSG_DATA_LEN];
		LLData response;
		HWConfig_pl HWCresponse;
	} msg_data;
};

typedef struct message ReqMessage;	/* Request message */
typedef struct message RspMessage;	/* Response message */

/*
 * The first 5010 bytes of shared memory contain the message queues,
 * indexes and other data. This structure is its template
 */
typedef struct {
	volatile ReqMessage req_queue[MAX_MESSAGES];
	volatile RspMessage rsp_queue[MAX_MESSAGES];
	volatile unsigned char req_head;
	volatile unsigned char req_tail;
	volatile unsigned char rsp_head;
	volatile unsigned char rsp_tail;
	volatile unsigned long signature;
	volatile unsigned long trace_enable;
	volatile unsigned char reserved[4];
} DualPortMemory;

#endif
