/*******************************************************************************
 * This file contains main functions related to iSCSI DataSequenceInOrder=No
 * and DataPDUInOrder=No.
 *
 * (c) Copyright 2007-2013 Datera, Inc.
 *
 * Author: Nicholas A. Bellinger <nab@linux-iscsi.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 ******************************************************************************/

#include <linux/slab.h>
#include <linux/random.h>

#include "iscsi_target_core.h"
#include "iscsi_target_util.h"
#include "iscsi_target_tpg.h"
#include "iscsi_target_seq_pdu_list.h"

#define OFFLOAD_BUF_SIZE	32768

#ifdef DEBUG
static void iscsit_dump_seq_list(struct iscsi_cmd *cmd)
{
	int i;
	struct iscsi_seq *seq;

	pr_debug("Dumping Sequence List for ITT: 0x%08x:\n",
			cmd->init_task_tag);

	for (i = 0; i < cmd->seq_count; i++) {
		seq = &cmd->seq_list[i];
		pr_debug("i: %d, pdu_start: %d, pdu_count: %d,"
			" offset: %d, xfer_len: %d, seq_send_order: %d,"
			" seq_no: %d\n", i, seq->pdu_start, seq->pdu_count,
			seq->offset, seq->xfer_len, seq->seq_send_order,
			seq->seq_no);
	}
}

static void iscsit_dump_pdu_list(struct iscsi_cmd *cmd)
{
	int i;
	struct iscsi_pdu *pdu;

	pr_debug("Dumping PDU List for ITT: 0x%08x:\n",
			cmd->init_task_tag);

	for (i = 0; i < cmd->pdu_count; i++) {
		pdu = &cmd->pdu_list[i];
		pr_debug("i: %d, offset: %d, length: %d,"
			" pdu_send_order: %d, seq_no: %d\n", i, pdu->offset,
			pdu->length, pdu->pdu_send_order, pdu->seq_no);
	}
}
#else
static void iscsit_dump_seq_list(struct iscsi_cmd *cmd) {}
static void iscsit_dump_pdu_list(struct iscsi_cmd *cmd) {}
#endif

static void iscsit_ordered_seq_lists(
	struct iscsi_cmd *cmd,
	u8 type)
{
	u32 i, seq_count = 0;

	for (i = 0; i < cmd->seq_count; i++) {
		if (cmd->seq_list[i].type != SEQTYPE_NORMAL)
			continue;
		cmd->seq_list[i].seq_send_order = seq_count++;
	}
}

static void iscsit_ordered_pdu_lists(
	struct iscsi_cmd *cmd,
	u8 type)
{
	u32 i, pdu_send_order = 0, seq_no = 0;

	for (i = 0; i < cmd->pdu_count; i++) {
redo:
		if (cmd->pdu_list[i].seq_no == seq_no) {
			cmd->pdu_list[i].pdu_send_order = pdu_send_order++;
			continue;
		}
		seq_no++;
		pdu_send_order = 0;
		goto redo;
	}
}

/*
 *	Generate count random values into array.
 *	Use 0x80000000 to mark generates valued in array[].
 */
static void iscsit_create_random_array(u32 *array, u32 count)
{
	int i, j, k;

	if (count == 1) {
		array[0] = 0;
		return;
	}

	for (i = 0; i < count; i++) {
redo:
		get_random_bytes(&j, sizeof(u32));
		j = (1 + (int) (9999 + 1) - j) % count;
		for (k = 0; k < i + 1; k++) {
			j |= 0x80000000;
			if ((array[k] & 0x80000000) && (array[k] == j))
				goto redo;
		}
		array[i] = j;
	}

	for (i = 0; i < count; i++)
		array[i] &= ~0x80000000;
}

static int iscsit_randomize_pdu_lists(
	struct iscsi_cmd *cmd,
	u8 type)
{
	int i = 0;
	u32 *array, pdu_count, seq_count = 0, seq_no = 0, seq_offset = 0;

	for (pdu_count = 0; pdu_count < cmd->pdu_count; pdu_count++) {
redo:
		if (cmd->pdu_list[pdu_count].seq_no == seq_no) {
			seq_count++;
			continue;
		}
		array = kcalloc(seq_count, sizeof(u32), GFP_KERNEL);
		if (!array) {
			pr_err("Unable to allocate memory"
				" for random array.\n");
			return -ENOMEM;
		}
		iscsit_create_random_array(array, seq_count);

		for (i = 0; i < seq_count; i++)
			cmd->pdu_list[seq_offset+i].pdu_send_order = array[i];

		kfree(array);

		seq_offset += seq_count;
		seq_count = 0;
		seq_no++;
		goto redo;
	}

	if (seq_count) {
		array = kcalloc(seq_count, sizeof(u32), GFP_KERNEL);
		if (!array) {
			pr_err("Unable to allocate memory for"
				" random array.\n");
			return -ENOMEM;
		}
		iscsit_create_random_array(array, seq_count);

		for (i = 0; i < seq_count; i++)
			cmd->pdu_list[seq_offset+i].pdu_send_order = array[i];

		kfree(array);
	}

	return 0;
}

static int iscsit_randomize_seq_lists(
	struct iscsi_cmd *cmd,
	u8 type)
{
	int i, j = 0;
	u32 *array, seq_count = cmd->seq_count;

	if ((type == PDULIST_IMMEDIATE) || (type == PDULIST_UNSOLICITED))
		seq_count--;
	else if (type == PDULIST_IMMEDIATE_AND_UNSOLICITED)
		seq_count -= 2;

	if (!seq_count)
		return 0;

	array = kcalloc(seq_count, sizeof(u32), GFP_KERNEL);
	if (!array) {
		pr_err("Unable to allocate memory for random array.\n");
		return -ENOMEM;
	}
	iscsit_create_random_array(array, seq_count);

	for (i = 0; i < cmd->seq_count; i++) {
		if (cmd->seq_list[i].type != SEQTYPE_NORMAL)
			continue;
		cmd->seq_list[i].seq_send_order = array[j++];
	}

	kfree(array);
	return 0;
}

static void iscsit_determine_counts_for_list(
	struct iscsi_cmd *cmd,
	struct iscsi_build_list *bl,
	u32 *seq_count,
	u32 *pdu_count)
{
	int check_immediate = 0;
	u32 burstlength = 0, offset = 0;
	u32 unsolicited_data_length = 0;
	u32 mdsl;
	struct iscsi_conn *conn = cmd->conn;

	if (cmd->se_cmd.data_direction == DMA_TO_DEVICE)
		mdsl = cmd->conn->conn_ops->MaxXmitDataSegmentLength;
	else
		mdsl = cmd->conn->conn_ops->MaxRecvDataSegmentLength;

	if ((bl->type == PDULIST_IMMEDIATE) ||
	    (bl->type == PDULIST_IMMEDIATE_AND_UNSOLICITED))
		check_immediate = 1;

	if ((bl->type == PDULIST_UNSOLICITED) ||
	    (bl->type == PDULIST_IMMEDIATE_AND_UNSOLICITED))
		unsolicited_data_length = min(cmd->se_cmd.data_length,
			conn->sess->sess_ops->FirstBurstLength);

	while (offset < cmd->se_cmd.data_length) {
		*pdu_count += 1;

		if (check_immediate) {
			check_immediate = 0;
			offset += bl->immediate_data_length;
			*seq_count += 1;
			if (unsolicited_data_length)
				unsolicited_data_length -=
					bl->immediate_data_length;
			continue;
		}
		if (unsolicited_data_length > 0) {
			if ((offset + mdsl) >= cmd->se_cmd.data_length) {
				unsolicited_data_length -=
					(cmd->se_cmd.data_length - offset);
				offset += (cmd->se_cmd.data_length - offset);
				continue;
			}
			if ((offset + mdsl)
					>= conn->sess->sess_ops->FirstBurstLength) {
				unsolicited_data_length -=
					(conn->sess->sess_ops->FirstBurstLength -
					offset);
				offset += (conn->sess->sess_ops->FirstBurstLength -
					offset);
				burstlength = 0;
				*seq_count += 1;
				continue;
			}

			offset += mdsl;
			unsolicited_data_length -= mdsl;
			continue;
		}
		if ((offset + mdsl) >= cmd->se_cmd.data_length) {
			offset += (cmd->se_cmd.data_length - offset);
			continue;
		}
		if ((burstlength + mdsl) >=
		     conn->sess->sess_ops->MaxBurstLength) {
			offset += (conn->sess->sess_ops->MaxBurstLength -
					burstlength);
			burstlength = 0;
			*seq_count += 1;
			continue;
		}

		burstlength += mdsl;
		offset += mdsl;
	}
}


/*
 *	Builds PDU and/or Sequence list, called while DataSequenceInOrder=No
 *	or DataPDUInOrder=No.
 */
static int iscsit_do_build_pdu_and_seq_lists(
	struct iscsi_cmd *cmd,
	struct iscsi_build_list *bl)
{
	int check_immediate = 0, datapduinorder, datasequenceinorder;
	u32 burstlength = 0, offset = 0, i = 0, mdsl;
	u32 pdu_count = 0, seq_no = 0, unsolicited_data_length = 0;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_pdu *pdu = cmd->pdu_list;
	struct iscsi_seq *seq = cmd->seq_list;

	if (cmd->se_cmd.data_direction == DMA_TO_DEVICE)
		mdsl = cmd->conn->conn_ops->MaxXmitDataSegmentLength;
	else
		mdsl = cmd->conn->conn_ops->MaxRecvDataSegmentLength;

	datapduinorder = conn->sess->sess_ops->DataPDUInOrder;
	datasequenceinorder = conn->sess->sess_ops->DataSequenceInOrder;

	if ((bl->type == PDULIST_IMMEDIATE) ||
	    (bl->type == PDULIST_IMMEDIATE_AND_UNSOLICITED))
		check_immediate = 1;

	if ((bl->type == PDULIST_UNSOLICITED) ||
	    (bl->type == PDULIST_IMMEDIATE_AND_UNSOLICITED))
		unsolicited_data_length = min(cmd->se_cmd.data_length,
			conn->sess->sess_ops->FirstBurstLength);

	while (offset < cmd->se_cmd.data_length) {
		pdu_count++;
		if (!datapduinorder) {
			pdu[i].offset = offset;
			pdu[i].seq_no = seq_no;
		}
		if (!datasequenceinorder && (pdu_count == 1)) {
			seq[seq_no].pdu_start = i;
			seq[seq_no].seq_no = seq_no;
			seq[seq_no].offset = offset;
			seq[seq_no].orig_offset = offset;
		}

		if (check_immediate) {
			check_immediate = 0;
			if (!datapduinorder) {
				pdu[i].type = PDUTYPE_IMMEDIATE;
				pdu[i++].length = bl->immediate_data_length;
			}
			if (!datasequenceinorder) {
				seq[seq_no].type = SEQTYPE_IMMEDIATE;
				seq[seq_no].pdu_count = 1;
				seq[seq_no].xfer_len =
					bl->immediate_data_length;
			}
			offset += bl->immediate_data_length;
			pdu_count = 0;
			seq_no++;
			if (unsolicited_data_length)
				unsolicited_data_length -=
					bl->immediate_data_length;
			continue;
		}
		if (unsolicited_data_length > 0) {
			if ((offset + mdsl) >= cmd->se_cmd.data_length) {
				if (!datapduinorder) {
					pdu[i].type = PDUTYPE_UNSOLICITED;
					pdu[i].length =
						(cmd->se_cmd.data_length - offset);
				}
				if (!datasequenceinorder) {
					seq[seq_no].type = SEQTYPE_UNSOLICITED;
					seq[seq_no].pdu_count = pdu_count;
					seq[seq_no].xfer_len = (burstlength +
						(cmd->se_cmd.data_length - offset));
				}
				unsolicited_data_length -=
						(cmd->se_cmd.data_length - offset);
				offset += (cmd->se_cmd.data_length - offset);
				continue;
			}
			if ((offset + mdsl) >=
					conn->sess->sess_ops->FirstBurstLength) {
				if (!datapduinorder) {
					pdu[i].type = PDUTYPE_UNSOLICITED;
					pdu[i++].length =
					   (conn->sess->sess_ops->FirstBurstLength -
						offset);
				}
				if (!datasequenceinorder) {
					seq[seq_no].type = SEQTYPE_UNSOLICITED;
					seq[seq_no].pdu_count = pdu_count;
					seq[seq_no].xfer_len = (burstlength +
					   (conn->sess->sess_ops->FirstBurstLength -
						offset));
				}
				unsolicited_data_length -=
					(conn->sess->sess_ops->FirstBurstLength -
						offset);
				offset += (conn->sess->sess_ops->FirstBurstLength -
						offset);
				burstlength = 0;
				pdu_count = 0;
				seq_no++;
				continue;
			}

			if (!datapduinorder) {
				pdu[i].type = PDUTYPE_UNSOLICITED;
				pdu[i++].length = mdsl;
			}
			burstlength += mdsl;
			offset += mdsl;
			unsolicited_data_length -= mdsl;
			continue;
		}
		if ((offset + mdsl) >= cmd->se_cmd.data_length) {
			if (!datapduinorder) {
				pdu[i].type = PDUTYPE_NORMAL;
				pdu[i].length = (cmd->se_cmd.data_length - offset);
			}
			if (!datasequenceinorder) {
				seq[seq_no].type = SEQTYPE_NORMAL;
				seq[seq_no].pdu_count = pdu_count;
				seq[seq_no].xfer_len = (burstlength +
					(cmd->se_cmd.data_length - offset));
			}
			offset += (cmd->se_cmd.data_length - offset);
			continue;
		}
		if ((burstlength + mdsl) >=
		     conn->sess->sess_ops->MaxBurstLength) {
			if (!datapduinorder) {
				pdu[i].type = PDUTYPE_NORMAL;
				pdu[i++].length =
					(conn->sess->sess_ops->MaxBurstLength -
						burstlength);
			}
			if (!datasequenceinorder) {
				seq[seq_no].type = SEQTYPE_NORMAL;
				seq[seq_no].pdu_count = pdu_count;
				seq[seq_no].xfer_len = (burstlength +
					(conn->sess->sess_ops->MaxBurstLength -
					burstlength));
			}
			offset += (conn->sess->sess_ops->MaxBurstLength -
					burstlength);
			burstlength = 0;
			pdu_count = 0;
			seq_no++;
			continue;
		}

		if (!datapduinorder) {
			pdu[i].type = PDUTYPE_NORMAL;
			pdu[i++].length = mdsl;
		}
		burstlength += mdsl;
		offset += mdsl;
	}

	if (!datasequenceinorder) {
		if (bl->data_direction & ISCSI_PDU_WRITE) {
			if (bl->randomize & RANDOM_R2T_OFFSETS) {
				if (iscsit_randomize_seq_lists(cmd, bl->type)
						< 0)
					return -1;
			} else
				iscsit_ordered_seq_lists(cmd, bl->type);
		} else if (bl->data_direction & ISCSI_PDU_READ) {
			if (bl->randomize & RANDOM_DATAIN_SEQ_OFFSETS) {
				if (iscsit_randomize_seq_lists(cmd, bl->type)
						< 0)
					return -1;
			} else
				iscsit_ordered_seq_lists(cmd, bl->type);
		}

		iscsit_dump_seq_list(cmd);
	}
	if (!datapduinorder) {
		if (bl->data_direction & ISCSI_PDU_WRITE) {
			if (bl->randomize & RANDOM_DATAOUT_PDU_OFFSETS) {
				if (iscsit_randomize_pdu_lists(cmd, bl->type)
						< 0)
					return -1;
			} else
				iscsit_ordered_pdu_lists(cmd, bl->type);
		} else if (bl->data_direction & ISCSI_PDU_READ) {
			if (bl->randomize & RANDOM_DATAIN_PDU_OFFSETS) {
				if (iscsit_randomize_pdu_lists(cmd, bl->type)
						< 0)
					return -1;
			} else
				iscsit_ordered_pdu_lists(cmd, bl->type);
		}

		iscsit_dump_pdu_list(cmd);
	}

	return 0;
}

int iscsit_build_pdu_and_seq_lists(
	struct iscsi_cmd *cmd,
	u32 immediate_data_length)
{
	struct iscsi_build_list bl;
	u32 pdu_count = 0, seq_count = 1;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_pdu *pdu = NULL;
	struct iscsi_seq *seq = NULL;

	struct iscsi_session *sess = conn->sess;
	struct iscsi_node_attrib *na;

	/*
	 * Do nothing if no OOO shenanigans
	 */
	if (sess->sess_ops->DataSequenceInOrder &&
	    sess->sess_ops->DataPDUInOrder)
		return 0;

	if (cmd->data_direction == DMA_NONE)
		return 0;

	na = iscsit_tpg_get_node_attrib(sess);
	memset(&bl, 0, sizeof(struct iscsi_build_list));

	if (cmd->data_direction == DMA_FROM_DEVICE) {
		bl.data_direction = ISCSI_PDU_READ;
		bl.type = PDULIST_NORMAL;
		if (na->random_datain_pdu_offsets)
			bl.randomize |= RANDOM_DATAIN_PDU_OFFSETS;
		if (na->random_datain_seq_offsets)
			bl.randomize |= RANDOM_DATAIN_SEQ_OFFSETS;
	} else {
		bl.data_direction = ISCSI_PDU_WRITE;
		bl.immediate_data_length = immediate_data_length;
		if (na->random_r2t_offsets)
			bl.randomize |= RANDOM_R2T_OFFSETS;

		if (!cmd->immediate_data && !cmd->unsolicited_data)
			bl.type = PDULIST_NORMAL;
		else if (cmd->immediate_data && !cmd->unsolicited_data)
			bl.type = PDULIST_IMMEDIATE;
		else if (!cmd->immediate_data && cmd->unsolicited_data)
			bl.type = PDULIST_UNSOLICITED;
		else if (cmd->immediate_data && cmd->unsolicited_data)
			bl.type = PDULIST_IMMEDIATE_AND_UNSOLICITED;
	}

	iscsit_determine_counts_for_list(cmd, &bl, &seq_count, &pdu_count);

	if (!conn->sess->sess_ops->DataSequenceInOrder) {
		seq = kcalloc(seq_count, sizeof(struct iscsi_seq), GFP_ATOMIC);
		if (!seq) {
			pr_err("Unable to allocate struct iscsi_seq list\n");
			return -ENOMEM;
		}
		cmd->seq_list = seq;
		cmd->seq_count = seq_count;
	}

	if (!conn->sess->sess_ops->DataPDUInOrder) {
		pdu = kcalloc(pdu_count, sizeof(struct iscsi_pdu), GFP_ATOMIC);
		if (!pdu) {
			pr_err("Unable to allocate struct iscsi_pdu list.\n");
			kfree(seq);
			return -ENOMEM;
		}
		cmd->pdu_list = pdu;
		cmd->pdu_count = pdu_count;
	}

	return iscsit_do_build_pdu_and_seq_lists(cmd, &bl);
}

struct iscsi_pdu *iscsit_get_pdu_holder(
	struct iscsi_cmd *cmd,
	u32 offset,
	u32 length)
{
	u32 i;
	struct iscsi_pdu *pdu = NULL;

	if (!cmd->pdu_list) {
		pr_err("struct iscsi_cmd->pdu_list is NULL!\n");
		return NULL;
	}

	pdu = &cmd->pdu_list[0];

	for (i = 0; i < cmd->pdu_count; i++)
		if ((pdu[i].offset == offset) && (pdu[i].length == length))
			return &pdu[i];

	pr_err("Unable to locate PDU holder for ITT: 0x%08x, Offset:"
		" %u, Length: %u\n", cmd->init_task_tag, offset, length);
	return NULL;
}

struct iscsi_pdu *iscsit_get_pdu_holder_for_seq(
	struct iscsi_cmd *cmd,
	struct iscsi_seq *seq)
{
	u32 i;
	struct iscsi_conn *conn = cmd->conn;
	struct iscsi_pdu *pdu = NULL;

	if (!cmd->pdu_list) {
		pr_err("struct iscsi_cmd->pdu_list is NULL!\n");
		return NULL;
	}

	if (conn->sess->sess_ops->DataSequenceInOrder) {
redo:
		pdu = &cmd->pdu_list[cmd->pdu_start];

		for (i = 0; pdu[i].seq_no != cmd->seq_no; i++) {
			pr_debug("pdu[i].seq_no: %d, pdu[i].pdu"
				"_send_order: %d, pdu[i].offset: %d,"
				" pdu[i].length: %d\n", pdu[i].seq_no,
				pdu[i].pdu_send_order, pdu[i].offset,
				pdu[i].length);

			if (pdu[i].pdu_send_order == cmd->pdu_send_order) {
				cmd->pdu_send_order++;
				return &pdu[i];
			}
		}

		cmd->pdu_start += cmd->pdu_send_order;
		cmd->pdu_send_order = 0;
		cmd->seq_no++;

		if (cmd->pdu_start < cmd->pdu_count)
			goto redo;

		pr_err("Command ITT: 0x%08x unable to locate"
			" struct iscsi_pdu for cmd->pdu_send_order: %u.\n",
			cmd->init_task_tag, cmd->pdu_send_order);
		return NULL;
	} else {
		if (!seq) {
			pr_err("struct iscsi_seq is NULL!\n");
			return NULL;
		}

		pr_debug("seq->pdu_start: %d, seq->pdu_count: %d,"
			" seq->seq_no: %d\n", seq->pdu_start, seq->pdu_count,
			seq->seq_no);

		pdu = &cmd->pdu_list[seq->pdu_start];

		if (seq->pdu_send_order == seq->pdu_count) {
			pr_err("Command ITT: 0x%08x seq->pdu_send"
				"_order: %u equals seq->pdu_count: %u\n",
				cmd->init_task_tag, seq->pdu_send_order,
				seq->pdu_count);
			return NULL;
		}

		for (i = 0; i < seq->pdu_count; i++) {
			if (pdu[i].pdu_send_order == seq->pdu_send_order) {
				seq->pdu_send_order++;
				return &pdu[i];
			}
		}

		pr_err("Command ITT: 0x%08x unable to locate iscsi"
			"_pdu_t for seq->pdu_send_order: %u.\n",
			cmd->init_task_tag, seq->pdu_send_order);
		return NULL;
	}

	return NULL;
}

struct iscsi_seq *iscsit_get_seq_holder(
	struct iscsi_cmd *cmd,
	u32 offset,
	u32 length)
{
	u32 i;

	if (!cmd->seq_list) {
		pr_err("struct iscsi_cmd->seq_list is NULL!\n");
		return NULL;
	}

	for (i = 0; i < cmd->seq_count; i++) {
		pr_debug("seq_list[i].orig_offset: %d, seq_list[i]."
			"xfer_len: %d, seq_list[i].seq_no %u\n",
			cmd->seq_list[i].orig_offset, cmd->seq_list[i].xfer_len,
			cmd->seq_list[i].seq_no);

		if ((cmd->seq_list[i].orig_offset +
				cmd->seq_list[i].xfer_len) >=
				(offset + length))
			return &cmd->seq_list[i];
	}

	pr_err("Unable to locate Sequence holder for ITT: 0x%08x,"
		" Offset: %u, Length: %u\n", cmd->init_task_tag, offset,
		length);
	return NULL;
}
