/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic iSCSI Offload Driver
 * Copyright (c) 2016 Cavium Inc.
 */

#ifndef _QEDI_ISCSI_H_
#define _QEDI_ISCSI_H_

#include <linux/socket.h>
#include <linux/completion.h>
#include "qedi.h"

#define ISCSI_MAX_SESS_PER_HBA	4096

#define DEF_KA_TIMEOUT		7200000
#define DEF_KA_INTERVAL		10000
#define DEF_KA_MAX_PROBE_COUNT	10
#define DEF_TOS			0
#define DEF_TTL			0xfe
#define DEF_SND_SEQ_SCALE	0
#define DEF_RCV_BUF		0xffff
#define DEF_SND_BUF		0xffff
#define DEF_SEED		0
#define DEF_MAX_RT_TIME		8000
#define DEF_MAX_DA_COUNT        2
#define DEF_SWS_TIMER		1000
#define DEF_MAX_CWND		2
#define DEF_PATH_MTU		1500
#define DEF_MSS			1460
#define DEF_LL2_MTU		1560
#define JUMBO_MTU		9000

#define MIN_MTU         576 /* rfc 793 */
#define IPV4_HDR_LEN    20
#define IPV6_HDR_LEN    40
#define TCP_HDR_LEN     20
#define TCP_OPTION_LEN  12
#define VLAN_LEN         4

enum {
	EP_STATE_IDLE                   = 0x0,
	EP_STATE_ACQRCONN_START         = 0x1,
	EP_STATE_ACQRCONN_COMPL         = 0x2,
	EP_STATE_OFLDCONN_START         = 0x4,
	EP_STATE_OFLDCONN_COMPL         = 0x8,
	EP_STATE_DISCONN_START          = 0x10,
	EP_STATE_DISCONN_COMPL          = 0x20,
	EP_STATE_CLEANUP_START          = 0x40,
	EP_STATE_CLEANUP_CMPL           = 0x80,
	EP_STATE_TCP_FIN_RCVD           = 0x100,
	EP_STATE_TCP_RST_RCVD           = 0x200,
	EP_STATE_LOGOUT_SENT            = 0x400,
	EP_STATE_LOGOUT_RESP_RCVD       = 0x800,
	EP_STATE_CLEANUP_FAILED         = 0x1000,
	EP_STATE_OFLDCONN_FAILED        = 0x2000,
	EP_STATE_CONNECT_FAILED         = 0x4000,
	EP_STATE_DISCONN_TIMEDOUT       = 0x8000,
	EP_STATE_OFLDCONN_NONE          = 0x10000,
};

struct qedi_conn;

struct qedi_endpoint {
	struct qedi_ctx *qedi;
	u32 dst_addr[4];
	u32 src_addr[4];
	u16 src_port;
	u16 dst_port;
	u16 vlan_id;
	u16 pmtu;
	u8 src_mac[ETH_ALEN];
	u8 dst_mac[ETH_ALEN];
	u8 ip_type;
	int state;
	wait_queue_head_t ofld_wait;
	wait_queue_head_t tcp_ofld_wait;
	u32 iscsi_cid;
	/* identifier of the connection from qed */
	u32 handle;
	u32 fw_cid;
	void __iomem *p_doorbell;
	struct iscsi_db_data db_data;

	/* Send queue management */
	struct iscsi_wqe *sq;
	dma_addr_t sq_dma;

	u16 sq_prod_idx;
	u16 fw_sq_prod_idx;
	u16 sq_con_idx;
	u32 sq_mem_size;

	void *sq_pbl;
	dma_addr_t sq_pbl_dma;
	u32 sq_pbl_size;
	struct qedi_conn *conn;
	struct work_struct offload_work;
};

#define QEDI_SQ_WQES_MIN	16

struct qedi_io_bdt {
	struct scsi_sge *sge_tbl;
	dma_addr_t sge_tbl_dma;
	u16 sge_valid;
};

/**
 * struct generic_pdu_resc - login pdu resource structure
 *
 * @req_buf:            driver buffer used to stage payload associated with
 *                      the login request
 * @req_dma_addr:       dma address for iscsi login request payload buffer
 * @req_buf_size:       actual login request payload length
 * @req_wr_ptr:         pointer into login request buffer when next data is
 *                      to be written
 * @resp_hdr:           iscsi header where iscsi login response header is to
 *                      be recreated
 * @resp_buf:           buffer to stage login response payload
 * @resp_dma_addr:      login response payload buffer dma address
 * @resp_buf_size:      login response paylod length
 * @resp_wr_ptr:        pointer into login response buffer when next data is
 *                      to be written
 * @req_bd_tbl:         iscsi login request payload BD table
 * @req_bd_dma:         login request BD table dma address
 * @resp_bd_tbl:        iscsi login response payload BD table
 * @resp_bd_dma:        login request BD table dma address
 *
 * following structure defines buffer info for generic pdus such as iSCSI Login,
 *      Logout and NOP
 */
struct generic_pdu_resc {
	char *req_buf;
	dma_addr_t req_dma_addr;
	u32 req_buf_size;
	char *req_wr_ptr;
	struct iscsi_hdr resp_hdr;
	char *resp_buf;
	dma_addr_t resp_dma_addr;
	u32 resp_buf_size;
	char *resp_wr_ptr;
	char *req_bd_tbl;
	dma_addr_t req_bd_dma;
	char *resp_bd_tbl;
	dma_addr_t resp_bd_dma;
};

struct qedi_conn {
	struct iscsi_cls_conn *cls_conn;
	struct qedi_ctx *qedi;
	struct qedi_endpoint *ep;
	struct iscsi_endpoint *iscsi_ep;
	struct list_head active_cmd_list;
	spinlock_t list_lock;		/* internal conn lock */
	u32 active_cmd_count;
	u32 cmd_cleanup_req;
	u32 cmd_cleanup_cmpl;

	u32 iscsi_conn_id;
	int itt;
	int abrt_conn;
#define QEDI_CID_RESERVED	0x5AFF
	u32 fw_cid;
	/*
	 * Buffer for login negotiation process
	 */
	struct generic_pdu_resc gen_pdu;

	struct list_head tmf_work_list;
	wait_queue_head_t wait_queue;
	spinlock_t tmf_work_lock;	/* tmf work lock */
	bool ep_disconnect_starting;
	int fw_cleanup_works;
};

struct qedi_cmd {
	struct list_head io_cmd;
	bool io_cmd_in_list;
	struct iscsi_hdr hdr;
	struct qedi_conn *conn;
	struct scsi_cmnd *scsi_cmd;
	struct scatterlist *sg;
	struct qedi_io_bdt io_tbl;
	struct e4_iscsi_task_context request;
	unsigned char *sense_buffer;
	dma_addr_t sense_buffer_dma;
	u16 task_id;

	/* field populated for tmf work queue */
	struct iscsi_task *task;
	struct work_struct tmf_work;
	int state;
#define CLEANUP_WAIT	1
#define CLEANUP_RECV	2
#define CLEANUP_WAIT_FAILED	3
#define CLEANUP_NOT_REQUIRED	4
#define LUN_RESET_RESPONSE_RECEIVED	5
#define RESPONSE_RECEIVED	6

	int type;
#define TYPEIO		1
#define TYPERESET	2

	struct qedi_work_map *list_tmf_work;
	/* slowpath management */
	bool use_slowpath;

	struct iscsi_tm_rsp *tmf_resp_buf;
	struct qedi_work cqe_work;
};

struct qedi_work_map {
	struct list_head list;
	struct qedi_cmd *qedi_cmd;
	struct iscsi_task *ctask;
	int rtid;

	int state;
#define QEDI_WORK_QUEUED	1
#define QEDI_WORK_SCHEDULED	2
#define QEDI_WORK_EXIT		3

	struct work_struct *ptr_tmf_work;
};

struct qedi_boot_target {
	char ip_addr[64];
	char iscsi_name[255];
	u32 ipv6_en;
};

#define qedi_set_itt(task_id, itt) ((u32)(((task_id) & 0xffff) | ((itt) << 16)))
#define qedi_get_itt(cqe) (cqe.iscsi_hdr.cmd.itt >> 16)

#define QEDI_OFLD_WAIT_STATE(q) ((q)->state == EP_STATE_OFLDCONN_FAILED || \
				(q)->state == EP_STATE_OFLDCONN_COMPL)

#endif /* _QEDI_ISCSI_H_ */
