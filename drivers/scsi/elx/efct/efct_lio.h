/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#ifndef __EFCT_LIO_H__
#define __EFCT_LIO_H__

#include "efct_scsi.h"
#include <target/target_core_base.h>

#define efct_lio_io_printf(io, fmt, ...)			\
	efc_log_debug(io->efct,					\
		"[%s] [%04x][i:%04x t:%04x h:%04x]" fmt,\
		io->node->display_name, io->instance_index,	\
		io->init_task_tag, io->tgt_task_tag, io->hw_tag,\
		##__VA_ARGS__)

#define efct_lio_tmfio_printf(io, fmt, ...)			\
	efc_log_debug(io->efct,					\
		"[%s] [%04x][i:%04x t:%04x h:%04x][f:%02x]" fmt,\
		io->node->display_name, io->instance_index,	\
		io->init_task_tag, io->tgt_task_tag, io->hw_tag,\
		io->tgt_io.tmf,  ##__VA_ARGS__)

#define efct_set_lio_io_state(io, value) (io->tgt_io.state |= value)

struct efct_lio_wq_data {
	struct efct		*efct;
	void			*ptr;
	struct work_struct	work;
};

/* Target private efct structure */
struct efct_scsi_tgt {
	u32			max_sge;
	u32			max_sgl;

	/*
	 * Variables used to send task set full. We are using a high watermark
	 * method to send task set full. We will reserve a fixed number of IOs
	 * per initiator plus a fudge factor. Once we reach this number,
	 * then the target will start sending task set full/busy responses.
	 */
	atomic_t		initiator_count;
	atomic_t		ios_in_use;
	atomic_t		io_high_watermark;

	atomic_t		watermark_hit;
	int			watermark_min;
	int			watermark_max;

	struct efct_lio_nport	*lio_nport;
	struct efct_lio_tpg	*tpg;

	struct list_head	vport_list;
	/* Protects vport list*/
	spinlock_t		efct_lio_lock;

	u64			wwnn;
};

struct efct_scsi_tgt_nport {
	struct efct_lio_nport	*lio_nport;
};

struct efct_node {
	struct list_head	list_entry;
	struct kref		ref;
	void			(*release)(struct kref *arg);
	struct efct		*efct;
	struct efc_node		*node;
	struct se_session	*session;
	spinlock_t		active_ios_lock;
	struct list_head	active_ios;
	char			display_name[EFC_NAME_LENGTH];
	u32			port_fc_id;
	u32			node_fc_id;
	u32			vpi;
	u32			rpi;
	u32			abort_cnt;
};

#define EFCT_LIO_STATE_SCSI_RECV_CMD		(1 << 0)
#define EFCT_LIO_STATE_TGT_SUBMIT_CMD		(1 << 1)
#define EFCT_LIO_STATE_TFO_QUEUE_DATA_IN	(1 << 2)
#define EFCT_LIO_STATE_TFO_WRITE_PENDING	(1 << 3)
#define EFCT_LIO_STATE_TGT_EXECUTE_CMD		(1 << 4)
#define EFCT_LIO_STATE_SCSI_SEND_RD_DATA	(1 << 5)
#define EFCT_LIO_STATE_TFO_CHK_STOP_FREE	(1 << 6)
#define EFCT_LIO_STATE_SCSI_DATA_DONE		(1 << 7)
#define EFCT_LIO_STATE_TFO_QUEUE_STATUS		(1 << 8)
#define EFCT_LIO_STATE_SCSI_SEND_RSP		(1 << 9)
#define EFCT_LIO_STATE_SCSI_RSP_DONE		(1 << 10)
#define EFCT_LIO_STATE_TGT_GENERIC_FREE		(1 << 11)
#define EFCT_LIO_STATE_SCSI_RECV_TMF		(1 << 12)
#define EFCT_LIO_STATE_TGT_SUBMIT_TMR		(1 << 13)
#define EFCT_LIO_STATE_TFO_WRITE_PEND_STATUS	(1 << 14)
#define EFCT_LIO_STATE_TGT_GENERIC_REQ_FAILURE  (1 << 15)

#define EFCT_LIO_STATE_TFO_ABORTED_TASK		(1 << 29)
#define EFCT_LIO_STATE_TFO_RELEASE_CMD		(1 << 30)
#define EFCT_LIO_STATE_SCSI_CMPL_CMD		(1u << 31)

struct efct_scsi_tgt_io {
	struct se_cmd		cmd;
	unsigned char		sense_buffer[TRANSPORT_SENSE_BUFFER];
	enum dma_data_direction	ddir;
	int			task_attr;
	u64			lun;

	u32			state;
	u8			tmf;
	struct efct_io		*io_to_abort;
	u32			seg_map_cnt;
	u32			seg_cnt;
	u32			cur_seg;
	enum efct_scsi_io_status err;
	bool			aborting;
	bool			rsp_sent;
	u32			transferred_len;
};

/* Handler return codes */
enum {
	SCSI_HANDLER_DATAPHASE_STARTED = 1,
	SCSI_HANDLER_RESP_STARTED,
	SCSI_HANDLER_VALIDATED_DATAPHASE_STARTED,
	SCSI_CMD_NOT_SUPPORTED,
};

#define WWN_NAME_LEN		32
struct efct_lio_vport {
	u64			wwpn;
	u64			npiv_wwpn;
	u64			npiv_wwnn;
	unsigned char		wwpn_str[WWN_NAME_LEN];
	struct se_wwn		vport_wwn;
	struct efct_lio_tpg	*tpg;
	struct efct		*efct;
	struct Scsi_Host	*shost;
	struct fc_vport		*fc_vport;
	atomic_t		enable;
};

struct efct_lio_nport {
	u64			wwpn;
	unsigned char		wwpn_str[WWN_NAME_LEN];
	struct se_wwn		nport_wwn;
	struct efct_lio_tpg	*tpg;
	struct efct		*efct;
	atomic_t		enable;
};

struct efct_lio_tpg_attrib {
	u32			generate_node_acls;
	u32			cache_dynamic_acls;
	u32			demo_mode_write_protect;
	u32			prod_mode_write_protect;
	u32			demo_mode_login_only;
	bool			session_deletion_wait;
};

struct efct_lio_tpg {
	struct se_portal_group	tpg;
	struct efct_lio_nport	*nport;
	struct efct_lio_vport	*vport;
	struct efct_lio_tpg_attrib tpg_attrib;
	unsigned short		tpgt;
	bool			enabled;
};

struct efct_lio_nacl {
	u64			nport_wwnn;
	char			nport_name[WWN_NAME_LEN];
	struct se_session	*session;
	struct se_node_acl	se_node_acl;
};

struct efct_lio_vport_list_t {
	struct list_head	list_entry;
	struct efct_lio_vport	*lio_vport;
};

int efct_scsi_tgt_driver_init(void);
int efct_scsi_tgt_driver_exit(void);

#endif /*__EFCT_LIO_H__ */
