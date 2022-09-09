/* SPDX-License-Identifier: GPL-2.0 */
#ifndef ISCSI_SEQ_AND_PDU_LIST_H
#define ISCSI_SEQ_AND_PDU_LIST_H

#include <linux/types.h>
#include <linux/cache.h>

/* struct iscsi_pdu->status */
#define DATAOUT_PDU_SENT			1

/* struct iscsi_seq->type */
#define SEQTYPE_IMMEDIATE			1
#define SEQTYPE_UNSOLICITED			2
#define SEQTYPE_NORMAL				3

/* struct iscsi_seq->status */
#define DATAOUT_SEQUENCE_GOT_R2T		1
#define DATAOUT_SEQUENCE_WITHIN_COMMAND_RECOVERY 2
#define DATAOUT_SEQUENCE_COMPLETE		3

/* iscsi_determine_counts_for_list() type */
#define PDULIST_NORMAL				1
#define PDULIST_IMMEDIATE			2
#define PDULIST_UNSOLICITED			3
#define PDULIST_IMMEDIATE_AND_UNSOLICITED	4

/* struct iscsi_pdu->type */
#define PDUTYPE_IMMEDIATE			1
#define PDUTYPE_UNSOLICITED			2
#define PDUTYPE_NORMAL				3

/* struct iscsi_pdu->status */
#define ISCSI_PDU_NOT_RECEIVED			0
#define ISCSI_PDU_RECEIVED_OK			1
#define ISCSI_PDU_CRC_FAILED			2
#define ISCSI_PDU_TIMED_OUT			3

/* struct iscsi_build_list->randomize */
#define RANDOM_DATAIN_PDU_OFFSETS		0x01
#define RANDOM_DATAIN_SEQ_OFFSETS		0x02
#define RANDOM_DATAOUT_PDU_OFFSETS		0x04
#define RANDOM_R2T_OFFSETS			0x08

/* struct iscsi_build_list->data_direction */
#define ISCSI_PDU_READ				0x01
#define ISCSI_PDU_WRITE				0x02

struct iscsi_build_list {
	int		data_direction;
	int		randomize;
	int		type;
	int		immediate_data_length;
};

struct iscsi_pdu {
	int		status;
	int		type;
	u8		flags;
	u32		data_sn;
	u32		length;
	u32		offset;
	u32		pdu_send_order;
	u32		seq_no;
} ____cacheline_aligned;

struct iscsi_seq {
	int		sent;
	int		status;
	int		type;
	u32		data_sn;
	u32		first_datasn;
	u32		last_datasn;
	u32		next_burst_len;
	u32		pdu_start;
	u32		pdu_count;
	u32		offset;
	u32		orig_offset;
	u32		pdu_send_order;
	u32		r2t_sn;
	u32		seq_send_order;
	u32		seq_no;
	u32		xfer_len;
} ____cacheline_aligned;

struct iscsit_cmd;

extern int iscsit_build_pdu_and_seq_lists(struct iscsit_cmd *, u32);
extern struct iscsi_pdu *iscsit_get_pdu_holder(struct iscsit_cmd *, u32, u32);
extern struct iscsi_pdu *iscsit_get_pdu_holder_for_seq(struct iscsit_cmd *, struct iscsi_seq *);
extern struct iscsi_seq *iscsit_get_seq_holder(struct iscsit_cmd *, u32, u32);

#endif /* ISCSI_SEQ_AND_PDU_LIST_H */
