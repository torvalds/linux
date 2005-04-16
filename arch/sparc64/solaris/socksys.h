/* $Id: socksys.h,v 1.2 1998/03/26 08:46:07 jj Exp $
 * socksys.h: Definitions for STREAMS modules emulation code.
 *
 * Copyright (C) 1998 Patrik Rak (prak3264@ss1000.ms.mff.cuni.cz)
 */

#define MSG_HIPRI	0x01
#define MSG_ANY		0x02
#define MSG_BAND	0x04

#define MORECTL		1
#define MOREDATA	2

#define	TBADADDR		1
#define	TBADOPT			2
#define	TACCES			3
#define TBADF			4
#define TNOADDR			5
#define TOUTSTATE	        6
#define TBADSEQ		        7
#define TSYSERR			8
#define TLOOK		        9
#define TBADDATA	       10
#define TBUFOVFLW	       11
#define TFLOW		       12
#define	TNODATA		       13
#define TNODIS		       14
#define TNOUDERR	       15
#define TBADFLAG	       16
#define TNOREL		       17
#define TNOTSUPPORT	       18
#define TSTATECHNG	       19

#define T_CONN_REQ      0
#define T_CONN_RES      1
#define T_DISCON_REQ    2
#define T_DATA_REQ      3
#define T_EXDATA_REQ    4
#define T_INFO_REQ      5
#define T_BIND_REQ      6
#define T_UNBIND_REQ    7
#define T_UNITDATA_REQ  8
#define T_OPTMGMT_REQ   9
#define T_ORDREL_REQ    10

#define T_CONN_IND      11
#define T_CONN_CON      12
#define T_DISCON_IND    13
#define T_DATA_IND      14
#define T_EXDATA_IND    15
#define T_INFO_ACK      16
#define T_BIND_ACK      17
#define T_ERROR_ACK     18
#define T_OK_ACK        19
#define T_UNITDATA_IND  20
#define T_UDERROR_IND   21
#define T_OPTMGMT_ACK   22
#define T_ORDREL_IND    23

#define T_NEGOTIATE	0x0004
#define T_FAILURE	0x0040

#define TS_UNBND	0	/* unbound */
#define	TS_WACK_BREQ	1	/* waiting for T_BIND_REQ ack  */
#define TS_WACK_UREQ	2	/* waiting for T_UNBIND_REQ ack */
#define TS_IDLE		3	/* idle */
#define TS_WACK_OPTREQ	4	/* waiting for T_OPTMGMT_REQ ack */
#define TS_WACK_CREQ	5	/* waiting for T_CONN_REQ ack */
#define TS_WCON_CREQ	6	/* waiting for T_CONN_REQ confirmation */
#define	TS_WRES_CIND	7	/* waiting for T_CONN_IND */
#define TS_WACK_CRES	8	/* waiting for T_CONN_RES ack */
#define TS_DATA_XFER	9	/* data transfer */
#define TS_WIND_ORDREL	10	/* releasing read but not write */
#define TS_WREQ_ORDREL	11      /* wait to release write but not read */
#define TS_WACK_DREQ6	12	/* waiting for T_DISCON_REQ ack */
#define TS_WACK_DREQ7	13	/* waiting for T_DISCON_REQ ack */
#define TS_WACK_DREQ9	14	/* waiting for T_DISCON_REQ ack */
#define TS_WACK_DREQ10	15	/* waiting for T_DISCON_REQ ack */
#define TS_WACK_DREQ11	16	/* waiting for T_DISCON_REQ ack */
#define TS_NOSTATES	17

struct T_conn_req {
	s32 PRIM_type; 
	s32 DEST_length;
	s32 DEST_offset;
	s32 OPT_length;
	s32 OPT_offset;
};

struct T_bind_req {
	s32 PRIM_type;
	s32 ADDR_length;
	s32 ADDR_offset;
	u32 CONIND_number;
};

struct T_unitdata_req {
	s32 PRIM_type; 
	s32 DEST_length;
	s32 DEST_offset;
	s32 OPT_length;
	s32 OPT_offset;
};

struct T_optmgmt_req {
	s32 PRIM_type; 
	s32 OPT_length;
	s32 OPT_offset;
	s32 MGMT_flags;
};

struct T_bind_ack {
	s32 PRIM_type;
	s32 ADDR_length;
	s32 ADDR_offset;
	u32 CONIND_number;
};

struct T_error_ack {
	s32 PRIM_type;
	s32 ERROR_prim;
	s32 TLI_error;
	s32 UNIX_error;
};

struct T_ok_ack {
	s32 PRIM_type;
	s32 CORRECT_prim;
};

struct T_conn_ind {
	s32 PRIM_type;
	s32 SRC_length;
	s32 SRC_offset;
	s32 OPT_length;
	s32 OPT_offset;
	s32 SEQ_number;
};

struct T_conn_con {
	s32 PRIM_type;
	s32 RES_length;
	s32 RES_offset;
	s32 OPT_length;
	s32 OPT_offset;
};

struct T_discon_ind {
	s32 PRIM_type;
	s32 DISCON_reason;
	s32 SEQ_number;
};

struct T_unitdata_ind {
	s32 PRIM_type;
	s32 SRC_length;
	s32 SRC_offset;
	s32 OPT_length;
	s32 OPT_offset;
};

struct T_optmgmt_ack {
	s32 PRIM_type; 
	s32 OPT_length;
	s32 OPT_offset;
	s32 MGMT_flags;
};

struct opthdr {
	s32 level;
	s32 name;
	s32 len;
	char value[0];	
};

struct T_primsg {
	struct T_primsg *next;
	unsigned char pri;
	unsigned char band;
	int length;
	s32 type;
};

struct strbuf {
	s32 maxlen;
	s32 len;
	u32 buf;
} ;

/* Constants used by STREAMS modules emulation code */

typedef char sol_module;

#define MAX_NR_STREAM_MODULES   16

/* Private data structure assigned to sockets. */

struct sol_socket_struct {
        int magic;
        int modcount;
        sol_module module[MAX_NR_STREAM_MODULES];
        long state;
        int offset;
        struct T_primsg *pfirst, *plast;
};

#define SOLARIS_SOCKET_MAGIC    0xADDED

