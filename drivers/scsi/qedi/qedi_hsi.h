/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */
#ifndef __QEDI_HSI__
#define __QEDI_HSI__
/*
 * Add include to common target
 */
#include <linux/qed/common_hsi.h>

/*
 * Add include to common storage target
 */
#include <linux/qed/storage_common.h>

/*
 * Add include to common TCP target
 */
#include <linux/qed/tcp_common.h>

/*
 * Add include to common iSCSI target for both eCore and protocol driver
 */
#include <linux/qed/iscsi_common.h>

/*
 * iSCSI CMDQ element
 */
struct iscsi_cmdqe {
	__le16 conn_id;
	u8 invalid_command;
	u8 cmd_hdr_type;
	__le32 reserved1[2];
	__le32 cmd_payload[13];
};

/*
 * iSCSI CMD header type
 */
enum iscsi_cmd_hdr_type {
	ISCSI_CMD_HDR_TYPE_BHS_ONLY /* iSCSI BHS with no expected AHS */,
	ISCSI_CMD_HDR_TYPE_BHS_W_AHS /* iSCSI BHS with expected AHS */,
	ISCSI_CMD_HDR_TYPE_AHS /* iSCSI AHS */,
	MAX_ISCSI_CMD_HDR_TYPE
};

#endif /* __QEDI_HSI__ */
