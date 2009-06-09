/* 57xx_iscsi_constants.h: Broadcom NetXtreme II iSCSI HSI
 *
 * Copyright (c) 2006 - 2009 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Anil Veerabhadrappa (anilgv@broadcom.com)
 */
#ifndef __57XX_ISCSI_CONSTANTS_H_
#define __57XX_ISCSI_CONSTANTS_H_

/**
* This file defines HSI constants for the iSCSI flows
*/

/* iSCSI request op codes */
#define ISCSI_OPCODE_CLEANUP_REQUEST    (7)

/* iSCSI response/messages op codes */
#define ISCSI_OPCODE_CLEANUP_RESPONSE 		(0x27)
#define ISCSI_OPCODE_NOPOUT_LOCAL_COMPLETION    (0)

/* iSCSI task types */
#define ISCSI_TASK_TYPE_READ    (0)
#define ISCSI_TASK_TYPE_WRITE   (1)
#define ISCSI_TASK_TYPE_MPATH   (2)

/* initial CQ sequence numbers */
#define ISCSI_INITIAL_SN    (1)

/* KWQ (kernel work queue) layer codes */
#define ISCSI_KWQE_LAYER_CODE   (6)

/* KWQ (kernel work queue) request op codes */
#define ISCSI_KWQE_OPCODE_OFFLOAD_CONN1 (0)
#define ISCSI_KWQE_OPCODE_OFFLOAD_CONN2 (1)
#define ISCSI_KWQE_OPCODE_UPDATE_CONN   (2)
#define ISCSI_KWQE_OPCODE_DESTROY_CONN  (3)
#define ISCSI_KWQE_OPCODE_INIT1         (4)
#define ISCSI_KWQE_OPCODE_INIT2         (5)

/* KCQ (kernel completion queue) response op codes */
#define ISCSI_KCQE_OPCODE_OFFLOAD_CONN  (0x10)
#define ISCSI_KCQE_OPCODE_UPDATE_CONN   (0x12)
#define ISCSI_KCQE_OPCODE_DESTROY_CONN  (0x13)
#define ISCSI_KCQE_OPCODE_INIT          (0x14)
#define ISCSI_KCQE_OPCODE_FW_CLEAN_TASK	(0x15)
#define ISCSI_KCQE_OPCODE_TCP_RESET     (0x16)
#define ISCSI_KCQE_OPCODE_TCP_SYN       (0x17)
#define ISCSI_KCQE_OPCODE_TCP_FIN       (0X18)
#define ISCSI_KCQE_OPCODE_TCP_ERROR     (0x19)
#define ISCSI_KCQE_OPCODE_CQ_EVENT_NOTIFICATION (0x20)
#define ISCSI_KCQE_OPCODE_ISCSI_ERROR   (0x21)

/* KCQ (kernel completion queue) completion status */
#define ISCSI_KCQE_COMPLETION_STATUS_SUCCESS                            (0x0)
#define ISCSI_KCQE_COMPLETION_STATUS_INVALID_OPCODE                     (0x1)
#define ISCSI_KCQE_COMPLETION_STATUS_CTX_ALLOC_FAILURE                  (0x2)
#define ISCSI_KCQE_COMPLETION_STATUS_CTX_FREE_FAILURE                   (0x3)
#define ISCSI_KCQE_COMPLETION_STATUS_NIC_ERROR                          (0x4)

#define ISCSI_KCQE_COMPLETION_STATUS_HDR_DIG_ERR                        (0x5)
#define ISCSI_KCQE_COMPLETION_STATUS_DATA_DIG_ERR                       (0x6)

#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_UNEXPECTED_OPCODE     (0xa)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_OPCODE                (0xb)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_AHS_LEN               (0xc)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_ITT                   (0xd)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_STATSN                (0xe)

/* Response */
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_EXP_DATASN            (0xf)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_PEND_R2T              (0x10)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_DATA_SEG_LEN_IS_ZERO  (0x2c)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_DATA_SEG_LEN_TOO_BIG  (0x2d)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_0                 (0x11)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_1                 (0x12)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_2                 (0x13)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_3                 (0x14)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_4                 (0x15)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_5                 (0x16)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_O_U_6                 (0x17)

/* Data-In */
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_REMAIN_RCV_LEN        (0x18)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_MAX_RCV_PDU_LEN       (0x19)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_F_BIT_ZERO            (0x1a)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_TTT_NOT_RSRV          (0x1b)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_DATASN                (0x1c)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_REMAIN_BURST_LEN      (0x1d)

/* R2T */
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_BUFFER_OFF            (0x1f)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_LUN                   (0x20)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_R2TSN                 (0x21)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_DESIRED_DATA_TRNS_LEN_0 (0x22)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_DESIRED_DATA_TRNS_LEN_1 (0x23)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_PEND_R2T_EXCEED       (0x24)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_TTT_IS_RSRV           (0x25)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_MAX_BURST_LEN         (0x26)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_DATA_SEG_LEN_NOT_ZERO (0x27)

/* TMF */
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_REJECT_PDU_LEN        (0x28)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_ASYNC_PDU_LEN         (0x29)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_NOPIN_PDU_LEN         (0x2a)
#define ISCSI_KCQE_COMPLETION_STATUS_PROTOCOL_ERR_PEND_R2T_IN_CLEANUP   (0x2b)

/* IP/TCP processing errors: */
#define ISCI_KCQE_COMPLETION_STATUS_TCP_ERROR_IP_FRAGMENT               (0x40)
#define ISCI_KCQE_COMPLETION_STATUS_TCP_ERROR_IP_OPTIONS                (0x41)
#define ISCI_KCQE_COMPLETION_STATUS_TCP_ERROR_URGENT_FLAG               (0x42)
#define ISCI_KCQE_COMPLETION_STATUS_TCP_ERROR_MAX_RTRANS                (0x43)

/* iSCSI licensing errors */
/* general iSCSI license not installed */
#define ISCSI_KCQE_COMPLETION_STATUS_ISCSI_NOT_SUPPORTED                (0x50)
/* additional LOM specific iSCSI license not installed */
#define ISCSI_KCQE_COMPLETION_STATUS_LOM_ISCSI_NOT_ENABLED              (0x51)

/* SQ/RQ/CQ DB structure sizes */
#define ISCSI_SQ_DB_SIZE    (16)
#define ISCSI_RQ_DB_SIZE    (16)
#define ISCSI_CQ_DB_SIZE    (80)

#define ISCSI_SQN_TO_NOTIFY_NOT_VALID                                   0xFFFF

/* Page size codes (for flags field in connection offload request) */
#define ISCSI_PAGE_SIZE_256     (0)
#define ISCSI_PAGE_SIZE_512     (1)
#define ISCSI_PAGE_SIZE_1K      (2)
#define ISCSI_PAGE_SIZE_2K      (3)
#define ISCSI_PAGE_SIZE_4K      (4)
#define ISCSI_PAGE_SIZE_8K      (5)
#define ISCSI_PAGE_SIZE_16K     (6)
#define ISCSI_PAGE_SIZE_32K     (7)
#define ISCSI_PAGE_SIZE_64K     (8)
#define ISCSI_PAGE_SIZE_128K    (9)
#define ISCSI_PAGE_SIZE_256K    (10)
#define ISCSI_PAGE_SIZE_512K    (11)
#define ISCSI_PAGE_SIZE_1M      (12)
#define ISCSI_PAGE_SIZE_2M      (13)
#define ISCSI_PAGE_SIZE_4M      (14)
#define ISCSI_PAGE_SIZE_8M      (15)

/* Iscsi PDU related defines */
#define ISCSI_HEADER_SIZE   (48)
#define ISCSI_DIGEST_SHIFT  (2)
#define ISCSI_DIGEST_SIZE   (4)

#define B577XX_ISCSI_CONNECTION_TYPE    3

#endif /*__57XX_ISCSI_CONSTANTS_H_ */
