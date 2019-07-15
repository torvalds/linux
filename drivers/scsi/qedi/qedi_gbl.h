/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 */

#ifndef _QEDI_GBL_H_
#define _QEDI_GBL_H_

#include "qedi_iscsi.h"

#ifdef CONFIG_DEBUG_FS
extern int qedi_do_not_recover;
#else
#define qedi_do_not_recover (0)
#endif

extern uint qedi_io_tracing;

extern struct scsi_host_template qedi_host_template;
extern struct iscsi_transport qedi_iscsi_transport;
extern const struct qed_iscsi_ops *qedi_ops;
extern const struct qedi_debugfs_ops qedi_debugfs_ops[];
extern const struct file_operations qedi_dbg_fops[];
extern struct device_attribute *qedi_shost_attrs[];

int qedi_alloc_sq(struct qedi_ctx *qedi, struct qedi_endpoint *ep);
void qedi_free_sq(struct qedi_ctx *qedi, struct qedi_endpoint *ep);

int qedi_send_iscsi_login(struct qedi_conn *qedi_conn,
			  struct iscsi_task *task);
int qedi_send_iscsi_logout(struct qedi_conn *qedi_conn,
			   struct iscsi_task *task);
int qedi_iscsi_abort_work(struct qedi_conn *qedi_conn,
			  struct iscsi_task *mtask);
int qedi_send_iscsi_text(struct qedi_conn *qedi_conn,
			 struct iscsi_task *task);
int qedi_send_iscsi_nopout(struct qedi_conn *qedi_conn,
			   struct iscsi_task *task,
			   char *datap, int data_len, int unsol);
int qedi_iscsi_send_ioreq(struct iscsi_task *task);
int qedi_get_task_idx(struct qedi_ctx *qedi);
void qedi_clear_task_idx(struct qedi_ctx *qedi, int idx);
int qedi_iscsi_cleanup_task(struct iscsi_task *task,
			    bool mark_cmd_node_deleted);
void qedi_iscsi_unmap_sg_list(struct qedi_cmd *cmd);
void qedi_update_itt_map(struct qedi_ctx *qedi, u32 tid, u32 proto_itt,
			 struct qedi_cmd *qedi_cmd);
void qedi_get_proto_itt(struct qedi_ctx *qedi, u32 tid, u32 *proto_itt);
void qedi_get_task_tid(struct qedi_ctx *qedi, u32 itt, int16_t *tid);
void qedi_process_iscsi_error(struct qedi_endpoint *ep,
			      struct iscsi_eqe_data *data);
void qedi_start_conn_recovery(struct qedi_ctx *qedi,
			      struct qedi_conn *qedi_conn);
struct qedi_conn *qedi_get_conn_from_id(struct qedi_ctx *qedi, u32 iscsi_cid);
void qedi_process_tcp_error(struct qedi_endpoint *ep,
			    struct iscsi_eqe_data *data);
void qedi_mark_device_missing(struct iscsi_cls_session *cls_session);
void qedi_mark_device_available(struct iscsi_cls_session *cls_session);
void qedi_reset_host_mtu(struct qedi_ctx *qedi, u16 mtu);
int qedi_recover_all_conns(struct qedi_ctx *qedi);
void qedi_fp_process_cqes(struct qedi_work *work);
int qedi_cleanup_all_io(struct qedi_ctx *qedi,
			struct qedi_conn *qedi_conn,
			struct iscsi_task *task, bool in_recovery);
void qedi_trace_io(struct qedi_ctx *qedi, struct iscsi_task *task,
		   u16 tid, int8_t direction);
int qedi_alloc_id(struct qedi_portid_tbl *id_tbl, u16 id);
u16 qedi_alloc_new_id(struct qedi_portid_tbl *id_tbl);
void qedi_free_id(struct qedi_portid_tbl *id_tbl, u16 id);
int qedi_create_sysfs_ctx_attr(struct qedi_ctx *qedi);
void qedi_remove_sysfs_ctx_attr(struct qedi_ctx *qedi);
void qedi_clearsq(struct qedi_ctx *qedi,
		  struct qedi_conn *qedi_conn,
		  struct iscsi_task *task);

#endif
