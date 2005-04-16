/* fc.h: Definitions for Fibre Channel Physical and Signaling Interface.
 *
 * Copyright (C) 1996-1997,1999 Jakub Jelinek (jj@ultra.linux.cz)
 *
 * Sources:
 *	Fibre Channel Physical & Signaling Interface (FC-PH), dpANS, 1994
 *	dpANS Fibre Channel Protocol for SCSI (X3.269-199X), Rev. 012, 1995
 */

#ifndef __FC_H
#define __FC_H

/* World Wide Name */
#define NAAID_IEEE		1
#define NAAID_IEEE_EXT		2
#define NAAID_LOCAL		3
#define NAAID_IP		4
#define NAAID_IEEE_REG		5
#define NAAID_IEEE_REG_EXT	6
#define NAAID_CCITT		12
#define NAAID_CCITT_GRP		14

/* This is NAAID_IEEE_EXT scheme */
typedef struct {
	u32	naaid:4;
	u32	nportid:12;
	u32	hi:16;
	u32	lo;
} fc_wwn;

/* Frame header for FC-PH frames */

/* r_ctl field */
#define R_CTL_DEVICE_DATA	0x00	/* FC4 Device_Data frame */
#define R_CTL_EXTENDED_SVC	0x20	/* Extended Link_Data frame */
#define R_CTL_FC4_SVC		0x30	/* FC4 Link_Data frame */
#define R_CTL_VIDEO		0x40	/* Video_Data frame */
#define R_CTL_BASIC_SVC		0x80	/* Basic Link_Data frame */
#define R_CTL_LINK_CTL		0xc0	/* Link_Control frame */
/* FC4 Device_Data frames */
#define R_CTL_UNCATEGORIZED	0x00
#define R_CTL_SOLICITED_DATA	0x01
#define R_CTL_UNSOL_CONTROL	0x02
#define R_CTL_SOLICITED_CONTROL	0x03
#define R_CTL_UNSOL_DATA	0x04
#define R_CTL_XFER_RDY		0x05
#define R_CTL_COMMAND		0x06
#define R_CTL_STATUS		0x07
/* Basic Link_Data frames */
#define R_CTL_LS_NOP		0x80
#define R_CTL_LS_ABTS		0x81
#define R_CTL_LS_RMC		0x82
#define R_CTL_LS_BA_ACC		0x84
#define R_CTL_LS_BA_RJT		0x85
/* Extended Link_Data frames */
#define R_CTL_ELS_REQ		0x22
#define R_CTL_ELS_RSP		0x23
/* Link_Control frames */
#define R_CTL_ACK_1		0xc0
#define R_CTL_ACK_N		0xc1
#define R_CTL_P_RJT		0xc2
#define R_CTL_F_RJT		0xc3
#define R_CTL_P_BSY		0xc4
#define R_CTL_F_BSY_DF		0xc5
#define R_CTL_F_BSY_LC		0xc6
#define R_CTL_LCR		0xc7

/* type field */
#define TYPE_BASIC_LS		0x00
#define TYPE_EXTENDED_LS	0x01
#define TYPE_IS8802		0x04
#define TYPE_IS8802_SNAP	0x05
#define TYPE_SCSI_FCP		0x08
#define TYPE_SCSI_GPP		0x09
#define TYPE_HIPP_FP		0x0a
#define TYPE_IPI3_MASTER	0x11
#define TYPE_IPI3_SLAVE		0x12
#define TYPE_IPI3_PEER		0x13

/* f_ctl field */
#define F_CTL_FILL_BYTES	0x000003
#define F_CTL_XCHG_REASSEMBLE	0x000004
#define F_CTL_RO_PRESENT	0x000008
#define F_CTL_ABORT_SEQ		0x000030
#define F_CTL_CONTINUE_SEQ	0x0000c0
#define F_CTL_INVALIDATE_XID	0x004000
#define F_CTL_XID_REASSIGNED	0x008000
#define F_CTL_SEQ_INITIATIVE	0x010000
#define F_CTL_CHAINED_SEQ	0x020000
#define F_CTL_END_CONNECT	0x040000
#define F_CTL_END_SEQ		0x080000
#define F_CTL_LAST_SEQ		0x100000
#define F_CTL_FIRST_SEQ		0x200000
#define F_CTL_SEQ_CONTEXT	0x400000
#define F_CTL_XCHG_CONTEXT	0x800000

typedef struct {
	u32	r_ctl:8,	did:24;
	u32	xxx1:8,		sid:24;
	u32	type:8,		f_ctl:24;
	u32	seq_id:8,	df_ctl:8,	seq_cnt:16;
	u16	ox_id,		rx_id;
	u32	param;
} fc_hdr;
/* The following are ugly macros to make setup of this structure faster */
#define FILL_FCHDR_RCTL_DID(fch, r_ctl, did) *(u32 *)(fch) = ((r_ctl) << 24) | (did);
#define FILL_FCHDR_SID(fch, sid) *((u32 *)(fch)+1) = (sid);
#define FILL_FCHDR_TYPE_FCTL(fch, type, f_ctl) *((u32 *)(fch)+2) = ((type) << 24) | (f_ctl);
#define FILL_FCHDR_SEQ_DF_SEQ(fch, seq_id, df_ctl, seq_cnt) *((u32 *)(fch)+3) = ((seq_id) << 24) | ((df_ctl) << 16) | (seq_cnt);
#define FILL_FCHDR_OXRX(fch, ox_id, rx_id) *((u32 *)(fch)+4) = ((ox_id) << 16) | (rx_id);

/* Well known addresses */
#define FS_GENERAL_MULTICAST	0xfffff7
#define FS_WELL_KNOWN_MULTICAST	0xfffff8
#define FS_HUNT_GROUP		0xfffff9
#define FS_MANAGEMENT_SERVER	0xfffffa
#define FS_TIME_SERVER		0xfffffb
#define FS_NAME_SERVER		0xfffffc
#define FS_FABRIC_CONTROLLER	0xfffffd
#define FS_FABRIC_F_PORT	0xfffffe
#define FS_BROADCAST		0xffffff

/* Reject frames */
/* The param field should be cast to this structure */
typedef struct {
	u8	action;
	u8	reason;
	u8	xxx;
	u8	vendor_unique;
} rjt_param;

/* Reject action codes */
#define RJT_RETRY			0x01
#define RJT_NONRETRY			0x02

/* Reject reason codes */
#define RJT_INVALID_DID			0x01
#define RJT_INVALID_SID			0x02
#define RJT_NPORT_NOT_AVAIL_TEMP	0x03
#define RJT_NPORT_NOT_AVAIL_PERM	0x04
#define RJT_CLASS_NOT_SUPPORTED		0x05
#define RJT_DELIMITER_ERROR		0x06
#define RJT_TYPE_NOT_SUPPORTED		0x07
#define RJT_INVALID_LINK_CONTROL	0x08
#define RJT_INVALID_R_CTL		0x09
#define RJT_INVALID_F_CTL		0x0a
#define RJT_INVALID_OX_ID		0x0b
#define RJT_INVALID_RX_ID		0x0c
#define RJT_INVALID_SEQ_ID		0x0d
#define RJT_INVALID_DF_CTL		0x0e
#define RJT_INVALID_SEQ_CNT		0x0f
#define RJT_INVALID_PARAMETER		0x10
#define RJT_EXCHANGE_ERROR		0x11
#define RJT_PROTOCOL_ERROR		0x12
#define RJT_INCORRECT_LENGTH		0x13
#define RJT_UNEXPECTED_ACK		0x14
#define RJT_UNEXPECTED_LINK_RESP	0x15
#define RJT_LOGIN_REQUIRED		0x16
#define RJT_EXCESSIVE_SEQUENCES		0x17
#define RJT_CANT_ESTABLISH_EXCHANGE	0x18
#define RJT_SECURITY_NOT_SUPPORTED	0x19
#define RJT_FABRIC_NA			0x1a
#define RJT_VENDOR_UNIQUE		0xff


#define SP_F_PORT_LOGIN			0x10

/* Extended SVC commands */
#define LS_RJT			0x01000000
#define LS_ACC			0x02000000
#define LS_PRLI_ACC		0x02100014
#define LS_PLOGI		0x03000000
#define LS_FLOGI		0x04000000
#define LS_LOGO			0x05000000
#define LS_ABTX			0x06000000
#define LS_RCS			0x07000000
#define LS_RES			0x08000000
#define LS_RSS			0x09000000
#define LS_RSI			0x0a000000
#define LS_ESTS			0x0b000000
#define LS_ESTC			0x0c000000
#define LS_ADVC			0x0d000000
#define LS_RTV			0x0e000000
#define LS_RLS			0x0f000000
#define LS_ECHO			0x10000000
#define LS_TEST			0x11000000
#define LS_RRQ			0x12000000
#define LS_IDENT		0x20000000
#define LS_PRLI			0x20100014
#define LS_DISPLAY		0x21000000
#define LS_PRLO			0x21100014
#define LS_PDISC		0x50000000
#define LS_ADISC		0x52000000

typedef struct {
	u8	fcph_hi, fcph_lo;
	u16	buf2buf_credit;
	u8	common_features;
	u8	xxx1;
	u16	buf2buf_size;
	u8	xxx2;
	u8	total_concurrent;
	u16	off_by_info;
	u32	e_d_tov;
} common_svc_parm;

typedef struct {
	u16	serv_opts;
	u16	initiator_ctl;
	u16	rcpt_ctl;
	u16	recv_size;
	u8	xxx1;
	u8	concurrent_seqs;
	u16	end2end_credit;
	u16	open_seqs_per_xchg;
	u16	xxx2;
} svc_parm;

/* Login */
typedef struct {
	u32		code;
	common_svc_parm	common;
	fc_wwn		nport_wwn;
	fc_wwn		node_wwn;
	svc_parm	class1;
	svc_parm	class2;
	svc_parm	class3;
} logi;

#endif /* !(__FC_H) */
