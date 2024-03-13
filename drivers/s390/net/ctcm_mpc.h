/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 2007
 * Authors:	Peter Tiedemann (ptiedem@de.ibm.com)
 *
 * 	MPC additions:
 *		Belinda Thompson (belindat@us.ibm.com)
 *		Andy Richter (richtera@us.ibm.com)
 */

#ifndef _CTC_MPC_H_
#define _CTC_MPC_H_

#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include "fsm.h"

/*
 * MPC external interface
 * Note that ctc_mpc_xyz are called with a lock on ................
 */

/*  port_number is the mpc device 0, 1, 2 etc mpc2 is port_number 2 */

/*  passive open  Just wait for XID2 exchange */
extern int ctc_mpc_alloc_channel(int port,
		void (*callback)(int port_num, int max_write_size));
/* active open  Alloc then send XID2 */
extern void ctc_mpc_establish_connectivity(int port,
		void (*callback)(int port_num, int rc, int max_write_size));

extern void ctc_mpc_dealloc_ch(int port);
extern void ctc_mpc_flow_control(int port, int flowc);

/*
 * other MPC Group prototypes and structures
 */

#define ETH_P_SNA_DIX	0x80D5

/*
 * Declaration of an XID2
 *
 */
#define ALLZEROS 0x0000000000000000

#define XID_FM2		0x20
#define XID2_0		0x00
#define XID2_7		0x07
#define XID2_WRITE_SIDE 0x04
#define XID2_READ_SIDE	0x05

struct xid2 {
	__u8	xid2_type_id;
	__u8	xid2_len;
	__u32	xid2_adj_id;
	__u8	xid2_rlen;
	__u8	xid2_resv1;
	__u8	xid2_flag1;
	__u8	xid2_fmtt;
	__u8	xid2_flag4;
	__u16	xid2_resv2;
	__u8	xid2_tgnum;
	__u32	xid2_sender_id;
	__u8	xid2_flag2;
	__u8	xid2_option;
	char  xid2_resv3[8];
	__u16	xid2_resv4;
	__u8	xid2_dlc_type;
	__u16	xid2_resv5;
	__u8	xid2_mpc_flag;
	__u8	xid2_resv6;
	__u16	xid2_buf_len;
	char xid2_buffer[255 - (13 * sizeof(__u8) +
				2 * sizeof(__u32) +
				4 * sizeof(__u16) +
				8 * sizeof(char))];
} __attribute__ ((packed));

#define XID2_LENGTH  (sizeof(struct xid2))

struct th_header {
	__u8	th_seg;
	__u8	th_ch_flag;
#define TH_HAS_PDU	0xf0
#define TH_IS_XID	0x01
#define TH_SWEEP_REQ	0xfe
#define TH_SWEEP_RESP	0xff
	__u8	th_blk_flag;
#define TH_DATA_IS_XID	0x80
#define TH_RETRY	0x40
#define TH_DISCONTACT	0xc0
#define TH_SEG_BLK	0x20
#define TH_LAST_SEG	0x10
#define TH_PDU_PART	0x08
	__u8	th_is_xid;	/* is 0x01 if this is XID  */
	__u32	th_seq_num;
} __attribute__ ((packed));

struct th_addon {
	__u32	th_last_seq;
	__u32	th_resvd;
} __attribute__ ((packed));

struct th_sweep {
	struct th_header th;
	struct th_addon sw;
} __attribute__ ((packed));

#define TH_HEADER_LENGTH (sizeof(struct th_header))
#define TH_SWEEP_LENGTH (sizeof(struct th_sweep))

#define PDU_LAST	0x80
#define PDU_CNTL	0x40
#define PDU_FIRST	0x20

struct pdu {
	__u32	pdu_offset;
	__u8	pdu_flag;
	__u8	pdu_proto;   /*  0x01 is APPN SNA  */
	__u16	pdu_seq;
} __attribute__ ((packed));

#define PDU_HEADER_LENGTH  (sizeof(struct pdu))

struct qllc {
	__u8	qllc_address;
#define QLLC_REQ	0xFF
#define QLLC_RESP	0x00
	__u8	qllc_commands;
#define QLLC_DISCONNECT 0x53
#define QLLC_UNSEQACK	0x73
#define QLLC_SETMODE	0x93
#define QLLC_EXCHID	0xBF
} __attribute__ ((packed));


/*
 * Definition of one MPC group
 */

#define MAX_MPCGCHAN		10
#define MPC_XID_TIMEOUT_VALUE	10000
#define MPC_CHANNEL_ADD		0
#define MPC_CHANNEL_REMOVE	1
#define MPC_CHANNEL_ATTN	2
#define XSIDE	1
#define YSIDE	0

struct mpcg_info {
	struct sk_buff	*skb;
	struct channel	*ch;
	struct xid2	*xid;
	struct th_sweep	*sweep;
	struct th_header *th;
};

struct mpc_group {
	struct tasklet_struct mpc_tasklet;
	struct tasklet_struct mpc_tasklet2;
	int	changed_side;
	int	saved_state;
	int	channels_terminating;
	int	out_of_sequence;
	int	flow_off_called;
	int	port_num;
	int	port_persist;
	int	alloc_called;
	__u32	xid2_adj_id;
	__u8	xid2_tgnum;
	__u32	xid2_sender_id;
	int	num_channel_paths;
	int	active_channels[2];
	__u16	group_max_buflen;
	int	outstanding_xid2;
	int	outstanding_xid7;
	int	outstanding_xid7_p2;
	int	sweep_req_pend_num;
	int	sweep_rsp_pend_num;
	struct sk_buff	*xid_skb;
	char		*xid_skb_data;
	struct th_header *xid_th;
	struct xid2	*xid;
	char		*xid_id;
	struct th_header *rcvd_xid_th;
	struct sk_buff	*rcvd_xid_skb;
	char		*rcvd_xid_data;
	__u8		in_sweep;
	__u8		roll;
	struct xid2	*saved_xid2;
	void 		(*allochanfunc)(int, int);
	int		allocchan_callback_retries;
	void 		(*estconnfunc)(int, int, int);
	int		estconn_callback_retries;
	int		estconn_called;
	int		xidnogood;
	int		send_qllc_disc;
	fsm_timer	timer;
	fsm_instance	*fsm; /* group xid fsm */
};

#ifdef DEBUGDATA
void ctcmpc_dumpit(char *buf, int len);
#else
static inline void ctcmpc_dumpit(char *buf, int len)
{
}
#endif

#ifdef DEBUGDATA
/*
 * Dump header and first 16 bytes of an sk_buff for debugging purposes.
 *
 * skb	 The struct sk_buff to dump.
 * offset Offset relative to skb-data, where to start the dump.
 */
void ctcmpc_dump_skb(struct sk_buff *skb, int offset);
#else
static inline void ctcmpc_dump_skb(struct sk_buff *skb, int offset)
{}
#endif

static inline void ctcmpc_dump32(char *buf, int len)
{
	if (len < 32)
		ctcmpc_dumpit(buf, len);
	else
		ctcmpc_dumpit(buf, 32);
}

void ctcm_ccw_check_rc(struct channel *, int, char *);
void mpc_group_ready(unsigned long adev);
void mpc_channel_action(struct channel *ch, int direction, int action);
void mpc_action_send_discontact(unsigned long thischan);
void mpc_action_discontact(fsm_instance *fi, int event, void *arg);
void ctcmpc_bh(unsigned long thischan);
#endif
/* --- This is the END my friend --- */
