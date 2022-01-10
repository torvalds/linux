/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2017 QLogic Corporation
 */
#ifndef __QLA_NVME_H
#define __QLA_NVME_H

#include <uapi/scsi/fc/fc_fs.h>
#include <uapi/scsi/fc/fc_els.h>
#include <linux/nvme-fc-driver.h>

#include "qla_def.h"
#include "qla_dsd.h"

#define MIN_NVME_HW_QUEUES 1
#define MAX_NVME_HW_QUEUES 128
#define DEF_NVME_HW_QUEUES 8

#define NVME_ATIO_CMD_OFF 32
#define NVME_FIRST_PACKET_CMDLEN (64 - NVME_ATIO_CMD_OFF)
#define Q2T_NVME_NUM_TAGS 2048
#define QLA_MAX_FC_SEGMENTS 64

struct scsi_qla_host;
struct qla_hw_data;
struct req_que;
struct srb;

struct nvme_private {
	struct srb	*sp;
	struct nvmefc_ls_req *fd;
	struct work_struct ls_work;
	struct work_struct abort_work;
	int comp_status;
	spinlock_t cmd_lock;
};

struct qla_nvme_rport {
	struct fc_port *fcport;
};

#define COMMAND_NVME    0x88            /* Command Type FC-NVMe IOCB */
struct cmd_nvme {
	uint8_t entry_type;             /* Entry type. */
	uint8_t entry_count;            /* Entry count. */
	uint8_t sys_define;             /* System defined. */
	uint8_t entry_status;           /* Entry Status. */

	uint32_t handle;                /* System handle. */
	__le16	nport_handle;		/* N_PORT handle. */
	__le16	timeout;		/* Command timeout. */

	__le16	dseg_count;		/* Data segment count. */
	__le16	nvme_rsp_dsd_len;	/* NVMe RSP DSD length */

	uint64_t rsvd;

	__le16	control_flags;		/* Control Flags */
#define CF_ADMIN_ASYNC_EVENT		BIT_13
#define CF_NVME_FIRST_BURST_ENABLE	BIT_11
#define CF_DIF_SEG_DESCR_ENABLE         BIT_3
#define CF_DATA_SEG_DESCR_ENABLE        BIT_2
#define CF_READ_DATA                    BIT_1
#define CF_WRITE_DATA                   BIT_0

	__le16	nvme_cmnd_dseg_len;             /* Data segment length. */
	__le64	 nvme_cmnd_dseg_address __packed;/* Data segment address. */
	__le64	 nvme_rsp_dseg_address __packed; /* Data segment address. */

	__le32	byte_count;		/* Total byte count. */

	uint8_t port_id[3];             /* PortID of destination port. */
	uint8_t vp_index;

	struct dsd64 nvme_dsd;
};

#define PT_LS4_REQUEST 0x89	/* Link Service pass-through IOCB (request) */
struct pt_ls4_request {
	uint8_t entry_type;
	uint8_t entry_count;
	uint8_t sys_define;
	uint8_t entry_status;
	uint32_t handle;
	__le16	status;
	__le16	nport_handle;
	__le16	tx_dseg_count;
	uint8_t  vp_index;
	uint8_t  rsvd;
	__le16	timeout;
	__le16	control_flags;
#define CF_LS4_SHIFT		13
#define CF_LS4_ORIGINATOR	0
#define CF_LS4_RESPONDER	1
#define CF_LS4_RESPONDER_TERM	2

	__le16	rx_dseg_count;
	__le16	rsvd2;
	__le32	exchange_address;
	__le32	rsvd3;
	__le32	rx_byte_count;
	__le32	tx_byte_count;
	struct dsd64 dsd[2];
};

#define PT_LS4_UNSOL 0x56	/* pass-up unsolicited rec FC-NVMe request */
struct pt_ls4_rx_unsol {
	uint8_t entry_type;
	uint8_t entry_count;
	__le16	rsvd0;
	__le16	rsvd1;
	uint8_t vp_index;
	uint8_t rsvd2;
	__le16	rsvd3;
	__le16	nport_handle;
	__le16	frame_size;
	__le16	rsvd4;
	__le32	exchange_address;
	uint8_t d_id[3];
	uint8_t r_ctl;
	be_id_t s_id;
	uint8_t cs_ctl;
	uint8_t f_ctl[3];
	uint8_t type;
	__le16	seq_cnt;
	uint8_t df_ctl;
	uint8_t seq_id;
	__le16	rx_id;
	__le16	ox_id;
	__le32	param;
	__le32	desc0;
#define PT_LS4_PAYLOAD_OFFSET 0x2c
#define PT_LS4_FIRST_PACKET_LEN 20
	__le32	desc_len;
	__le32	payload[3];
};

/*
 * Global functions prototype in qla_nvme.c source file.
 */
int qla_nvme_register_hba(struct scsi_qla_host *);
int  qla_nvme_register_remote(struct scsi_qla_host *, struct fc_port *);
void qla_nvme_delete(struct scsi_qla_host *);
void qla24xx_nvme_ls4_iocb(struct scsi_qla_host *, struct pt_ls4_request *,
    struct req_que *);
void qla24xx_async_gffid_sp_done(struct srb *sp, int);
#endif
