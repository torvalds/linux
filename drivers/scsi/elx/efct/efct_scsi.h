/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#if !defined(__EFCT_SCSI_H__)
#define __EFCT_SCSI_H__
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport_fc.h>

/* efct_scsi_rcv_cmd() efct_scsi_rcv_tmf() flags */
#define EFCT_SCSI_CMD_DIR_IN		(1 << 0)
#define EFCT_SCSI_CMD_DIR_OUT		(1 << 1)
#define EFCT_SCSI_CMD_SIMPLE		(1 << 2)
#define EFCT_SCSI_CMD_HEAD_OF_QUEUE	(1 << 3)
#define EFCT_SCSI_CMD_ORDERED		(1 << 4)
#define EFCT_SCSI_CMD_UNTAGGED		(1 << 5)
#define EFCT_SCSI_CMD_ACA		(1 << 6)
#define EFCT_SCSI_FIRST_BURST_ERR	(1 << 7)
#define EFCT_SCSI_FIRST_BURST_ABORTED	(1 << 8)

/* efct_scsi_send_rd_data/recv_wr_data/send_resp flags */
#define EFCT_SCSI_LAST_DATAPHASE	(1 << 0)
#define EFCT_SCSI_NO_AUTO_RESPONSE	(1 << 1)
#define EFCT_SCSI_LOW_LATENCY		(1 << 2)

#define EFCT_SCSI_SNS_BUF_VALID(sense)	((sense) && \
			(0x70 == (((const u8 *)(sense))[0] & 0x70)))

#define EFCT_SCSI_WQ_STEERING_SHIFT	16
#define EFCT_SCSI_WQ_STEERING_MASK	(0xf << EFCT_SCSI_WQ_STEERING_SHIFT)
#define EFCT_SCSI_WQ_STEERING_CLASS	(0 << EFCT_SCSI_WQ_STEERING_SHIFT)
#define EFCT_SCSI_WQ_STEERING_REQUEST	(1 << EFCT_SCSI_WQ_STEERING_SHIFT)
#define EFCT_SCSI_WQ_STEERING_CPU	(2 << EFCT_SCSI_WQ_STEERING_SHIFT)

#define EFCT_SCSI_WQ_CLASS_SHIFT		(20)
#define EFCT_SCSI_WQ_CLASS_MASK		(0xf << EFCT_SCSI_WQ_CLASS_SHIFT)
#define EFCT_SCSI_WQ_CLASS(x)		((x & EFCT_SCSI_WQ_CLASS_MASK) << \
						EFCT_SCSI_WQ_CLASS_SHIFT)

#define EFCT_SCSI_WQ_CLASS_LOW_LATENCY	1

struct efct_scsi_cmd_resp {
	u8 scsi_status;
	u16 scsi_status_qualifier;
	u8 *response_data;
	u32 response_data_length;
	u8 *sense_data;
	u32 sense_data_length;
	int residual;
	u32 response_wire_length;
};

struct efct_vport {
	struct efct		*efct;
	bool			is_vport;
	struct fc_host_statistics fc_host_stats;
	struct Scsi_Host	*shost;
	struct fc_vport		*fc_vport;
	u64			npiv_wwpn;
	u64			npiv_wwnn;
};

/* Status values returned by IO callbacks */
enum efct_scsi_io_status {
	EFCT_SCSI_STATUS_GOOD = 0,
	EFCT_SCSI_STATUS_ABORTED,
	EFCT_SCSI_STATUS_ERROR,
	EFCT_SCSI_STATUS_DIF_GUARD_ERR,
	EFCT_SCSI_STATUS_DIF_REF_TAG_ERROR,
	EFCT_SCSI_STATUS_DIF_APP_TAG_ERROR,
	EFCT_SCSI_STATUS_DIF_UNKNOWN_ERROR,
	EFCT_SCSI_STATUS_PROTOCOL_CRC_ERROR,
	EFCT_SCSI_STATUS_NO_IO,
	EFCT_SCSI_STATUS_ABORT_IN_PROGRESS,
	EFCT_SCSI_STATUS_CHECK_RESPONSE,
	EFCT_SCSI_STATUS_COMMAND_TIMEOUT,
	EFCT_SCSI_STATUS_TIMEDOUT_AND_ABORTED,
	EFCT_SCSI_STATUS_SHUTDOWN,
	EFCT_SCSI_STATUS_NEXUS_LOST,
};

struct efct_node;
struct efct_io;
struct efc_node;
struct efc_nport;

/* Callback used by send_rd_data(), recv_wr_data(), send_resp() */
typedef int (*efct_scsi_io_cb_t)(struct efct_io *io,
				    enum efct_scsi_io_status status,
				    u32 flags, void *arg);

/* Callback used by send_rd_io(), send_wr_io() */
typedef int (*efct_scsi_rsp_io_cb_t)(struct efct_io *io,
			enum efct_scsi_io_status status,
			struct efct_scsi_cmd_resp *rsp,
			u32 flags, void *arg);

/* efct_scsi_cb_t flags */
#define EFCT_SCSI_IO_CMPL		(1 << 0)
/* IO completed, response sent */
#define EFCT_SCSI_IO_CMPL_RSP_SENT	(1 << 1)
#define EFCT_SCSI_IO_ABORTED		(1 << 2)

/* efct_scsi_recv_tmf() request values */
enum efct_scsi_tmf_cmd {
	EFCT_SCSI_TMF_ABORT_TASK = 1,
	EFCT_SCSI_TMF_QUERY_TASK_SET,
	EFCT_SCSI_TMF_ABORT_TASK_SET,
	EFCT_SCSI_TMF_CLEAR_TASK_SET,
	EFCT_SCSI_TMF_QUERY_ASYNCHRONOUS_EVENT,
	EFCT_SCSI_TMF_LOGICAL_UNIT_RESET,
	EFCT_SCSI_TMF_CLEAR_ACA,
	EFCT_SCSI_TMF_TARGET_RESET,
};

/* efct_scsi_send_tmf_resp() response values */
enum efct_scsi_tmf_resp {
	EFCT_SCSI_TMF_FUNCTION_COMPLETE = 1,
	EFCT_SCSI_TMF_FUNCTION_SUCCEEDED,
	EFCT_SCSI_TMF_FUNCTION_IO_NOT_FOUND,
	EFCT_SCSI_TMF_FUNCTION_REJECTED,
	EFCT_SCSI_TMF_INCORRECT_LOGICAL_UNIT_NUMBER,
	EFCT_SCSI_TMF_SERVICE_DELIVERY,
};

struct efct_scsi_sgl {
	uintptr_t	addr;
	uintptr_t	dif_addr;
	size_t		len;
};

enum efct_scsi_io_role {
	EFCT_SCSI_IO_ROLE_ORIGINATOR,
	EFCT_SCSI_IO_ROLE_RESPONDER,
};

struct efct_io *
efct_scsi_io_alloc(struct efct_node *node);
void efct_scsi_io_free(struct efct_io *io);
struct efct_io *efct_io_get_instance(struct efct *efct, u32 index);

int efct_scsi_tgt_driver_init(void);
int efct_scsi_tgt_driver_exit(void);
int efct_scsi_tgt_new_device(struct efct *efct);
int efct_scsi_tgt_del_device(struct efct *efct);
int
efct_scsi_tgt_new_nport(struct efc *efc, struct efc_nport *nport);
void
efct_scsi_tgt_del_nport(struct efc *efc, struct efc_nport *nport);

int
efct_scsi_new_initiator(struct efc *efc, struct efc_node *node);

enum efct_scsi_del_initiator_reason {
	EFCT_SCSI_INITIATOR_DELETED,
	EFCT_SCSI_INITIATOR_MISSING,
};

int
efct_scsi_del_initiator(struct efc *efc, struct efc_node *node,	int reason);
void
efct_scsi_recv_cmd(struct efct_io *io, uint64_t lun, u8 *cdb, u32 cdb_len,
		   u32 flags);
int
efct_scsi_recv_tmf(struct efct_io *tmfio, u32 lun, enum efct_scsi_tmf_cmd cmd,
		   struct efct_io *abortio, u32 flags);
int
efct_scsi_send_rd_data(struct efct_io *io, u32 flags, struct efct_scsi_sgl *sgl,
		u32 sgl_count, u64 wire_len, efct_scsi_io_cb_t cb, void *arg);
int
efct_scsi_recv_wr_data(struct efct_io *io, u32 flags, struct efct_scsi_sgl *sgl,
		u32 sgl_count, u64 wire_len, efct_scsi_io_cb_t cb, void *arg);
int
efct_scsi_send_resp(struct efct_io *io, u32 flags,
		struct efct_scsi_cmd_resp *rsp, efct_scsi_io_cb_t cb, void *arg);
int
efct_scsi_send_tmf_resp(struct efct_io *io, enum efct_scsi_tmf_resp rspcode,
			u8 addl_rsp_info[3], efct_scsi_io_cb_t cb, void *arg);
int
efct_scsi_tgt_abort_io(struct efct_io *io, efct_scsi_io_cb_t cb, void *arg);

void efct_scsi_io_complete(struct efct_io *io);

int efct_scsi_reg_fc_transport(void);
void efct_scsi_release_fc_transport(void);
int efct_scsi_new_device(struct efct *efct);
void efct_scsi_del_device(struct efct *efct);
void _efct_scsi_io_free(struct kref *arg);

int
efct_scsi_del_vport(struct efct *efct, struct Scsi_Host *shost);
struct efct_vport *
efct_scsi_new_vport(struct efct *efct, struct device *dev);

int efct_scsi_io_dispatch(struct efct_io *io, void *cb);
int efct_scsi_io_dispatch_abort(struct efct_io *io, void *cb);
void efct_scsi_check_pending(struct efct *efct);
struct efct_io *
efct_bls_send_rjt(struct efct_io *io, struct fc_frame_header *hdr);

#endif /* __EFCT_SCSI_H__ */
