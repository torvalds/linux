// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ibmvfc.c -- driver for IBM Power Virtual Fibre Channel Adapter
 *
 * Written By: Brian King <brking@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) IBM Corporation, 2008
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/stringify.h>
#include <linux/bsg-lib.h>
#include <asm/firmware.h>
#include <asm/irq.h>
#include <asm/rtas.h>
#include <asm/vio.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_bsg_fc.h>
#include "ibmvfc.h"

static unsigned int init_timeout = IBMVFC_INIT_TIMEOUT;
static unsigned int default_timeout = IBMVFC_DEFAULT_TIMEOUT;
static u64 max_lun = IBMVFC_MAX_LUN;
static unsigned int max_targets = IBMVFC_MAX_TARGETS;
static unsigned int max_requests = IBMVFC_MAX_REQUESTS_DEFAULT;
static unsigned int disc_threads = IBMVFC_MAX_DISC_THREADS;
static unsigned int ibmvfc_debug = IBMVFC_DEBUG;
static unsigned int log_level = IBMVFC_DEFAULT_LOG_LEVEL;
static unsigned int cls3_error = IBMVFC_CLS3_ERROR;
static unsigned int mq_enabled = IBMVFC_MQ;
static unsigned int nr_scsi_hw_queues = IBMVFC_SCSI_HW_QUEUES;
static unsigned int nr_scsi_channels = IBMVFC_SCSI_CHANNELS;
static unsigned int mig_channels_only = IBMVFC_MIG_NO_SUB_TO_CRQ;
static unsigned int mig_no_less_channels = IBMVFC_MIG_NO_N_TO_M;

static LIST_HEAD(ibmvfc_head);
static DEFINE_SPINLOCK(ibmvfc_driver_lock);
static struct scsi_transport_template *ibmvfc_transport_template;

MODULE_DESCRIPTION("IBM Virtual Fibre Channel Driver");
MODULE_AUTHOR("Brian King <brking@linux.vnet.ibm.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(IBMVFC_DRIVER_VERSION);

module_param_named(mq, mq_enabled, uint, S_IRUGO);
MODULE_PARM_DESC(mq, "Enable multiqueue support. "
		 "[Default=" __stringify(IBMVFC_MQ) "]");
module_param_named(scsi_host_queues, nr_scsi_hw_queues, uint, S_IRUGO);
MODULE_PARM_DESC(scsi_host_queues, "Number of SCSI Host submission queues. "
		 "[Default=" __stringify(IBMVFC_SCSI_HW_QUEUES) "]");
module_param_named(scsi_hw_channels, nr_scsi_channels, uint, S_IRUGO);
MODULE_PARM_DESC(scsi_hw_channels, "Number of hw scsi channels to request. "
		 "[Default=" __stringify(IBMVFC_SCSI_CHANNELS) "]");
module_param_named(mig_channels_only, mig_channels_only, uint, S_IRUGO);
MODULE_PARM_DESC(mig_channels_only, "Prevent migration to non-channelized system. "
		 "[Default=" __stringify(IBMVFC_MIG_NO_SUB_TO_CRQ) "]");
module_param_named(mig_no_less_channels, mig_no_less_channels, uint, S_IRUGO);
MODULE_PARM_DESC(mig_no_less_channels, "Prevent migration to system with less channels. "
		 "[Default=" __stringify(IBMVFC_MIG_NO_N_TO_M) "]");

module_param_named(init_timeout, init_timeout, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(init_timeout, "Initialization timeout in seconds. "
		 "[Default=" __stringify(IBMVFC_INIT_TIMEOUT) "]");
module_param_named(default_timeout, default_timeout, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(default_timeout,
		 "Default timeout in seconds for initialization and EH commands. "
		 "[Default=" __stringify(IBMVFC_DEFAULT_TIMEOUT) "]");
module_param_named(max_requests, max_requests, uint, S_IRUGO);
MODULE_PARM_DESC(max_requests, "Maximum requests for this adapter. "
		 "[Default=" __stringify(IBMVFC_MAX_REQUESTS_DEFAULT) "]");
module_param_named(max_lun, max_lun, ullong, S_IRUGO);
MODULE_PARM_DESC(max_lun, "Maximum allowed LUN. "
		 "[Default=" __stringify(IBMVFC_MAX_LUN) "]");
module_param_named(max_targets, max_targets, uint, S_IRUGO);
MODULE_PARM_DESC(max_targets, "Maximum allowed targets. "
		 "[Default=" __stringify(IBMVFC_MAX_TARGETS) "]");
module_param_named(disc_threads, disc_threads, uint, S_IRUGO);
MODULE_PARM_DESC(disc_threads, "Number of device discovery threads to use. "
		 "[Default=" __stringify(IBMVFC_MAX_DISC_THREADS) "]");
module_param_named(debug, ibmvfc_debug, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Enable driver debug information. "
		 "[Default=" __stringify(IBMVFC_DEBUG) "]");
module_param_named(log_level, log_level, uint, 0);
MODULE_PARM_DESC(log_level, "Set to 0 - 4 for increasing verbosity of device driver. "
		 "[Default=" __stringify(IBMVFC_DEFAULT_LOG_LEVEL) "]");
module_param_named(cls3_error, cls3_error, uint, 0);
MODULE_PARM_DESC(cls3_error, "Enable FC Class 3 Error Recovery. "
		 "[Default=" __stringify(IBMVFC_CLS3_ERROR) "]");

static const struct {
	u16 status;
	u16 error;
	u8 result;
	u8 retry;
	int log;
	char *name;
} cmd_status [] = {
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_UNABLE_TO_ESTABLISH, DID_ERROR, 1, 1, "unable to establish" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_XPORT_FAULT, DID_OK, 1, 0, "transport fault" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_CMD_TIMEOUT, DID_TIME_OUT, 1, 1, "command timeout" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_ENETDOWN, DID_TRANSPORT_DISRUPTED, 1, 1, "network down" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_HW_FAILURE, DID_ERROR, 1, 1, "hardware failure" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_LINK_DOWN_ERR, DID_REQUEUE, 0, 0, "link down" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_LINK_DEAD_ERR, DID_ERROR, 0, 0, "link dead" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_UNABLE_TO_REGISTER, DID_ERROR, 1, 1, "unable to register" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_XPORT_BUSY, DID_BUS_BUSY, 1, 0, "transport busy" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_XPORT_DEAD, DID_ERROR, 0, 1, "transport dead" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_CONFIG_ERROR, DID_ERROR, 1, 1, "configuration error" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_NAME_SERVER_FAIL, DID_ERROR, 1, 1, "name server failure" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_LINK_HALTED, DID_REQUEUE, 1, 0, "link halted" },
	{ IBMVFC_FABRIC_MAPPED, IBMVFC_XPORT_GENERAL, DID_OK, 1, 0, "general transport error" },

	{ IBMVFC_VIOS_FAILURE, IBMVFC_CRQ_FAILURE, DID_REQUEUE, 1, 1, "CRQ failure" },
	{ IBMVFC_VIOS_FAILURE, IBMVFC_SW_FAILURE, DID_ERROR, 0, 1, "software failure" },
	{ IBMVFC_VIOS_FAILURE, IBMVFC_INVALID_PARAMETER, DID_ERROR, 0, 1, "invalid parameter" },
	{ IBMVFC_VIOS_FAILURE, IBMVFC_MISSING_PARAMETER, DID_ERROR, 0, 1, "missing parameter" },
	{ IBMVFC_VIOS_FAILURE, IBMVFC_HOST_IO_BUS, DID_ERROR, 1, 1, "host I/O bus failure" },
	{ IBMVFC_VIOS_FAILURE, IBMVFC_TRANS_CANCELLED, DID_ERROR, 0, 1, "transaction cancelled" },
	{ IBMVFC_VIOS_FAILURE, IBMVFC_TRANS_CANCELLED_IMPLICIT, DID_ERROR, 0, 1, "transaction cancelled implicit" },
	{ IBMVFC_VIOS_FAILURE, IBMVFC_INSUFFICIENT_RESOURCE, DID_REQUEUE, 1, 1, "insufficient resources" },
	{ IBMVFC_VIOS_FAILURE, IBMVFC_PLOGI_REQUIRED, DID_ERROR, 0, 1, "port login required" },
	{ IBMVFC_VIOS_FAILURE, IBMVFC_COMMAND_FAILED, DID_ERROR, 1, 1, "command failed" },

	{ IBMVFC_FC_FAILURE, IBMVFC_INVALID_ELS_CMD_CODE, DID_ERROR, 0, 1, "invalid ELS command code" },
	{ IBMVFC_FC_FAILURE, IBMVFC_INVALID_VERSION, DID_ERROR, 0, 1, "invalid version level" },
	{ IBMVFC_FC_FAILURE, IBMVFC_LOGICAL_ERROR, DID_ERROR, 1, 1, "logical error" },
	{ IBMVFC_FC_FAILURE, IBMVFC_INVALID_CT_IU_SIZE, DID_ERROR, 0, 1, "invalid CT_IU size" },
	{ IBMVFC_FC_FAILURE, IBMVFC_LOGICAL_BUSY, DID_REQUEUE, 1, 0, "logical busy" },
	{ IBMVFC_FC_FAILURE, IBMVFC_PROTOCOL_ERROR, DID_ERROR, 1, 1, "protocol error" },
	{ IBMVFC_FC_FAILURE, IBMVFC_UNABLE_TO_PERFORM_REQ, DID_ERROR, 1, 1, "unable to perform request" },
	{ IBMVFC_FC_FAILURE, IBMVFC_CMD_NOT_SUPPORTED, DID_ERROR, 0, 0, "command not supported" },
	{ IBMVFC_FC_FAILURE, IBMVFC_SERVER_NOT_AVAIL, DID_ERROR, 0, 1, "server not available" },
	{ IBMVFC_FC_FAILURE, IBMVFC_CMD_IN_PROGRESS, DID_ERROR, 0, 1, "command already in progress" },
	{ IBMVFC_FC_FAILURE, IBMVFC_VENDOR_SPECIFIC, DID_ERROR, 1, 1, "vendor specific" },

	{ IBMVFC_FC_SCSI_ERROR, 0, DID_OK, 1, 0, "SCSI error" },
	{ IBMVFC_FC_SCSI_ERROR, IBMVFC_COMMAND_FAILED, DID_ERROR, 0, 1, "PRLI to device failed." },
};

static void ibmvfc_npiv_login(struct ibmvfc_host *);
static void ibmvfc_tgt_send_prli(struct ibmvfc_target *);
static void ibmvfc_tgt_send_plogi(struct ibmvfc_target *);
static void ibmvfc_tgt_query_target(struct ibmvfc_target *);
static void ibmvfc_npiv_logout(struct ibmvfc_host *);
static void ibmvfc_tgt_implicit_logout_and_del(struct ibmvfc_target *);
static void ibmvfc_tgt_move_login(struct ibmvfc_target *);

static void ibmvfc_release_sub_crqs(struct ibmvfc_host *);
static void ibmvfc_init_sub_crqs(struct ibmvfc_host *);

static const char *unknown_error = "unknown error";

static long h_reg_sub_crq(unsigned long unit_address, unsigned long ioba,
			  unsigned long length, unsigned long *cookie,
			  unsigned long *irq)
{
	unsigned long retbuf[PLPAR_HCALL_BUFSIZE];
	long rc;

	rc = plpar_hcall(H_REG_SUB_CRQ, retbuf, unit_address, ioba, length);
	*cookie = retbuf[0];
	*irq = retbuf[1];

	return rc;
}

static int ibmvfc_check_caps(struct ibmvfc_host *vhost, unsigned long cap_flags)
{
	u64 host_caps = be64_to_cpu(vhost->login_buf->resp.capabilities);

	return (host_caps & cap_flags) ? 1 : 0;
}

static struct ibmvfc_fcp_cmd_iu *ibmvfc_get_fcp_iu(struct ibmvfc_host *vhost,
						   struct ibmvfc_cmd *vfc_cmd)
{
	if (ibmvfc_check_caps(vhost, IBMVFC_HANDLE_VF_WWPN))
		return &vfc_cmd->v2.iu;
	else
		return &vfc_cmd->v1.iu;
}

static struct ibmvfc_fcp_rsp *ibmvfc_get_fcp_rsp(struct ibmvfc_host *vhost,
						 struct ibmvfc_cmd *vfc_cmd)
{
	if (ibmvfc_check_caps(vhost, IBMVFC_HANDLE_VF_WWPN))
		return &vfc_cmd->v2.rsp;
	else
		return &vfc_cmd->v1.rsp;
}

#ifdef CONFIG_SCSI_IBMVFC_TRACE
/**
 * ibmvfc_trc_start - Log a start trace entry
 * @evt:		ibmvfc event struct
 *
 **/
static void ibmvfc_trc_start(struct ibmvfc_event *evt)
{
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_cmd *vfc_cmd = &evt->iu.cmd;
	struct ibmvfc_mad_common *mad = &evt->iu.mad_common;
	struct ibmvfc_fcp_cmd_iu *iu = ibmvfc_get_fcp_iu(vhost, vfc_cmd);
	struct ibmvfc_trace_entry *entry;
	int index = atomic_inc_return(&vhost->trace_index) & IBMVFC_TRACE_INDEX_MASK;

	entry = &vhost->trace[index];
	entry->evt = evt;
	entry->time = jiffies;
	entry->fmt = evt->crq.format;
	entry->type = IBMVFC_TRC_START;

	switch (entry->fmt) {
	case IBMVFC_CMD_FORMAT:
		entry->op_code = iu->cdb[0];
		entry->scsi_id = be64_to_cpu(vfc_cmd->tgt_scsi_id);
		entry->lun = scsilun_to_int(&iu->lun);
		entry->tmf_flags = iu->tmf_flags;
		entry->u.start.xfer_len = be32_to_cpu(iu->xfer_len);
		break;
	case IBMVFC_MAD_FORMAT:
		entry->op_code = be32_to_cpu(mad->opcode);
		break;
	default:
		break;
	}
}

/**
 * ibmvfc_trc_end - Log an end trace entry
 * @evt:		ibmvfc event struct
 *
 **/
static void ibmvfc_trc_end(struct ibmvfc_event *evt)
{
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_cmd *vfc_cmd = &evt->xfer_iu->cmd;
	struct ibmvfc_mad_common *mad = &evt->xfer_iu->mad_common;
	struct ibmvfc_fcp_cmd_iu *iu = ibmvfc_get_fcp_iu(vhost, vfc_cmd);
	struct ibmvfc_fcp_rsp *rsp = ibmvfc_get_fcp_rsp(vhost, vfc_cmd);
	struct ibmvfc_trace_entry *entry;
	int index = atomic_inc_return(&vhost->trace_index) & IBMVFC_TRACE_INDEX_MASK;

	entry = &vhost->trace[index];
	entry->evt = evt;
	entry->time = jiffies;
	entry->fmt = evt->crq.format;
	entry->type = IBMVFC_TRC_END;

	switch (entry->fmt) {
	case IBMVFC_CMD_FORMAT:
		entry->op_code = iu->cdb[0];
		entry->scsi_id = be64_to_cpu(vfc_cmd->tgt_scsi_id);
		entry->lun = scsilun_to_int(&iu->lun);
		entry->tmf_flags = iu->tmf_flags;
		entry->u.end.status = be16_to_cpu(vfc_cmd->status);
		entry->u.end.error = be16_to_cpu(vfc_cmd->error);
		entry->u.end.fcp_rsp_flags = rsp->flags;
		entry->u.end.rsp_code = rsp->data.info.rsp_code;
		entry->u.end.scsi_status = rsp->scsi_status;
		break;
	case IBMVFC_MAD_FORMAT:
		entry->op_code = be32_to_cpu(mad->opcode);
		entry->u.end.status = be16_to_cpu(mad->status);
		break;
	default:
		break;

	}
}

#else
#define ibmvfc_trc_start(evt) do { } while (0)
#define ibmvfc_trc_end(evt) do { } while (0)
#endif

/**
 * ibmvfc_get_err_index - Find the index into cmd_status for the fcp response
 * @status:		status / error class
 * @error:		error
 *
 * Return value:
 *	index into cmd_status / -EINVAL on failure
 **/
static int ibmvfc_get_err_index(u16 status, u16 error)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cmd_status); i++)
		if ((cmd_status[i].status & status) == cmd_status[i].status &&
		    cmd_status[i].error == error)
			return i;

	return -EINVAL;
}

/**
 * ibmvfc_get_cmd_error - Find the error description for the fcp response
 * @status:		status / error class
 * @error:		error
 *
 * Return value:
 *	error description string
 **/
static const char *ibmvfc_get_cmd_error(u16 status, u16 error)
{
	int rc = ibmvfc_get_err_index(status, error);
	if (rc >= 0)
		return cmd_status[rc].name;
	return unknown_error;
}

/**
 * ibmvfc_get_err_result - Find the scsi status to return for the fcp response
 * @vhost:      ibmvfc host struct
 * @vfc_cmd:	ibmvfc command struct
 *
 * Return value:
 *	SCSI result value to return for completed command
 **/
static int ibmvfc_get_err_result(struct ibmvfc_host *vhost, struct ibmvfc_cmd *vfc_cmd)
{
	int err;
	struct ibmvfc_fcp_rsp *rsp = ibmvfc_get_fcp_rsp(vhost, vfc_cmd);
	int fc_rsp_len = be32_to_cpu(rsp->fcp_rsp_len);

	if ((rsp->flags & FCP_RSP_LEN_VALID) &&
	    ((fc_rsp_len && fc_rsp_len != 4 && fc_rsp_len != 8) ||
	     rsp->data.info.rsp_code))
		return DID_ERROR << 16;

	err = ibmvfc_get_err_index(be16_to_cpu(vfc_cmd->status), be16_to_cpu(vfc_cmd->error));
	if (err >= 0)
		return rsp->scsi_status | (cmd_status[err].result << 16);
	return rsp->scsi_status | (DID_ERROR << 16);
}

/**
 * ibmvfc_retry_cmd - Determine if error status is retryable
 * @status:		status / error class
 * @error:		error
 *
 * Return value:
 *	1 if error should be retried / 0 if it should not
 **/
static int ibmvfc_retry_cmd(u16 status, u16 error)
{
	int rc = ibmvfc_get_err_index(status, error);

	if (rc >= 0)
		return cmd_status[rc].retry;
	return 1;
}

static const char *unknown_fc_explain = "unknown fc explain";

static const struct {
	u16 fc_explain;
	char *name;
} ls_explain [] = {
	{ 0x00, "no additional explanation" },
	{ 0x01, "service parameter error - options" },
	{ 0x03, "service parameter error - initiator control" },
	{ 0x05, "service parameter error - recipient control" },
	{ 0x07, "service parameter error - received data field size" },
	{ 0x09, "service parameter error - concurrent seq" },
	{ 0x0B, "service parameter error - credit" },
	{ 0x0D, "invalid N_Port/F_Port_Name" },
	{ 0x0E, "invalid node/Fabric Name" },
	{ 0x0F, "invalid common service parameters" },
	{ 0x11, "invalid association header" },
	{ 0x13, "association header required" },
	{ 0x15, "invalid originator S_ID" },
	{ 0x17, "invalid OX_ID-RX-ID combination" },
	{ 0x19, "command (request) already in progress" },
	{ 0x1E, "N_Port Login requested" },
	{ 0x1F, "Invalid N_Port_ID" },
};

static const struct {
	u16 fc_explain;
	char *name;
} gs_explain [] = {
	{ 0x00, "no additional explanation" },
	{ 0x01, "port identifier not registered" },
	{ 0x02, "port name not registered" },
	{ 0x03, "node name not registered" },
	{ 0x04, "class of service not registered" },
	{ 0x06, "initial process associator not registered" },
	{ 0x07, "FC-4 TYPEs not registered" },
	{ 0x08, "symbolic port name not registered" },
	{ 0x09, "symbolic node name not registered" },
	{ 0x0A, "port type not registered" },
	{ 0xF0, "authorization exception" },
	{ 0xF1, "authentication exception" },
	{ 0xF2, "data base full" },
	{ 0xF3, "data base empty" },
	{ 0xF4, "processing request" },
	{ 0xF5, "unable to verify connection" },
	{ 0xF6, "devices not in a common zone" },
};

/**
 * ibmvfc_get_ls_explain - Return the FC Explain description text
 * @status:	FC Explain status
 *
 * Returns:
 *	error string
 **/
static const char *ibmvfc_get_ls_explain(u16 status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ls_explain); i++)
		if (ls_explain[i].fc_explain == status)
			return ls_explain[i].name;

	return unknown_fc_explain;
}

/**
 * ibmvfc_get_gs_explain - Return the FC Explain description text
 * @status:	FC Explain status
 *
 * Returns:
 *	error string
 **/
static const char *ibmvfc_get_gs_explain(u16 status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(gs_explain); i++)
		if (gs_explain[i].fc_explain == status)
			return gs_explain[i].name;

	return unknown_fc_explain;
}

static const struct {
	enum ibmvfc_fc_type fc_type;
	char *name;
} fc_type [] = {
	{ IBMVFC_FABRIC_REJECT, "fabric reject" },
	{ IBMVFC_PORT_REJECT, "port reject" },
	{ IBMVFC_LS_REJECT, "ELS reject" },
	{ IBMVFC_FABRIC_BUSY, "fabric busy" },
	{ IBMVFC_PORT_BUSY, "port busy" },
	{ IBMVFC_BASIC_REJECT, "basic reject" },
};

static const char *unknown_fc_type = "unknown fc type";

/**
 * ibmvfc_get_fc_type - Return the FC Type description text
 * @status:	FC Type error status
 *
 * Returns:
 *	error string
 **/
static const char *ibmvfc_get_fc_type(u16 status)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fc_type); i++)
		if (fc_type[i].fc_type == status)
			return fc_type[i].name;

	return unknown_fc_type;
}

/**
 * ibmvfc_set_tgt_action - Set the next init action for the target
 * @tgt:		ibmvfc target struct
 * @action:		action to perform
 *
 * Returns:
 *	0 if action changed / non-zero if not changed
 **/
static int ibmvfc_set_tgt_action(struct ibmvfc_target *tgt,
				  enum ibmvfc_target_action action)
{
	int rc = -EINVAL;

	switch (tgt->action) {
	case IBMVFC_TGT_ACTION_LOGOUT_RPORT:
		if (action == IBMVFC_TGT_ACTION_LOGOUT_RPORT_WAIT ||
		    action == IBMVFC_TGT_ACTION_DEL_RPORT) {
			tgt->action = action;
			rc = 0;
		}
		break;
	case IBMVFC_TGT_ACTION_LOGOUT_RPORT_WAIT:
		if (action == IBMVFC_TGT_ACTION_DEL_RPORT ||
		    action == IBMVFC_TGT_ACTION_DEL_AND_LOGOUT_RPORT) {
			tgt->action = action;
			rc = 0;
		}
		break;
	case IBMVFC_TGT_ACTION_LOGOUT_DELETED_RPORT:
		if (action == IBMVFC_TGT_ACTION_LOGOUT_RPORT) {
			tgt->action = action;
			rc = 0;
		}
		break;
	case IBMVFC_TGT_ACTION_DEL_AND_LOGOUT_RPORT:
		if (action == IBMVFC_TGT_ACTION_LOGOUT_DELETED_RPORT) {
			tgt->action = action;
			rc = 0;
		}
		break;
	case IBMVFC_TGT_ACTION_DEL_RPORT:
		if (action == IBMVFC_TGT_ACTION_DELETED_RPORT) {
			tgt->action = action;
			rc = 0;
		}
		break;
	case IBMVFC_TGT_ACTION_DELETED_RPORT:
		break;
	default:
		tgt->action = action;
		rc = 0;
		break;
	}

	if (action >= IBMVFC_TGT_ACTION_LOGOUT_RPORT)
		tgt->add_rport = 0;

	return rc;
}

/**
 * ibmvfc_set_host_state - Set the state for the host
 * @vhost:		ibmvfc host struct
 * @state:		state to set host to
 *
 * Returns:
 *	0 if state changed / non-zero if not changed
 **/
static int ibmvfc_set_host_state(struct ibmvfc_host *vhost,
				  enum ibmvfc_host_state state)
{
	int rc = 0;

	switch (vhost->state) {
	case IBMVFC_HOST_OFFLINE:
		rc = -EINVAL;
		break;
	default:
		vhost->state = state;
		break;
	}

	return rc;
}

/**
 * ibmvfc_set_host_action - Set the next init action for the host
 * @vhost:		ibmvfc host struct
 * @action:		action to perform
 *
 **/
static void ibmvfc_set_host_action(struct ibmvfc_host *vhost,
				   enum ibmvfc_host_action action)
{
	switch (action) {
	case IBMVFC_HOST_ACTION_ALLOC_TGTS:
		if (vhost->action == IBMVFC_HOST_ACTION_INIT_WAIT)
			vhost->action = action;
		break;
	case IBMVFC_HOST_ACTION_LOGO_WAIT:
		if (vhost->action == IBMVFC_HOST_ACTION_LOGO)
			vhost->action = action;
		break;
	case IBMVFC_HOST_ACTION_INIT_WAIT:
		if (vhost->action == IBMVFC_HOST_ACTION_INIT)
			vhost->action = action;
		break;
	case IBMVFC_HOST_ACTION_QUERY:
		switch (vhost->action) {
		case IBMVFC_HOST_ACTION_INIT_WAIT:
		case IBMVFC_HOST_ACTION_NONE:
		case IBMVFC_HOST_ACTION_TGT_DEL_FAILED:
			vhost->action = action;
			break;
		default:
			break;
		}
		break;
	case IBMVFC_HOST_ACTION_TGT_INIT:
		if (vhost->action == IBMVFC_HOST_ACTION_ALLOC_TGTS)
			vhost->action = action;
		break;
	case IBMVFC_HOST_ACTION_REENABLE:
	case IBMVFC_HOST_ACTION_RESET:
		vhost->action = action;
		break;
	case IBMVFC_HOST_ACTION_INIT:
	case IBMVFC_HOST_ACTION_TGT_DEL:
	case IBMVFC_HOST_ACTION_LOGO:
	case IBMVFC_HOST_ACTION_QUERY_TGTS:
	case IBMVFC_HOST_ACTION_TGT_DEL_FAILED:
	case IBMVFC_HOST_ACTION_NONE:
	default:
		switch (vhost->action) {
		case IBMVFC_HOST_ACTION_RESET:
		case IBMVFC_HOST_ACTION_REENABLE:
			break;
		default:
			vhost->action = action;
			break;
		}
		break;
	}
}

/**
 * ibmvfc_reinit_host - Re-start host initialization (no NPIV Login)
 * @vhost:		ibmvfc host struct
 *
 * Return value:
 *	nothing
 **/
static void ibmvfc_reinit_host(struct ibmvfc_host *vhost)
{
	if (vhost->action == IBMVFC_HOST_ACTION_NONE &&
	    vhost->state == IBMVFC_ACTIVE) {
		if (!ibmvfc_set_host_state(vhost, IBMVFC_INITIALIZING)) {
			scsi_block_requests(vhost->host);
			ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_QUERY);
		}
	} else
		vhost->reinit = 1;

	wake_up(&vhost->work_wait_q);
}

/**
 * ibmvfc_del_tgt - Schedule cleanup and removal of the target
 * @tgt:		ibmvfc target struct
 **/
static void ibmvfc_del_tgt(struct ibmvfc_target *tgt)
{
	if (!ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_LOGOUT_RPORT)) {
		tgt->job_step = ibmvfc_tgt_implicit_logout_and_del;
		tgt->init_retries = 0;
	}
	wake_up(&tgt->vhost->work_wait_q);
}

/**
 * ibmvfc_link_down - Handle a link down event from the adapter
 * @vhost:	ibmvfc host struct
 * @state:	ibmvfc host state to enter
 *
 **/
static void ibmvfc_link_down(struct ibmvfc_host *vhost,
			     enum ibmvfc_host_state state)
{
	struct ibmvfc_target *tgt;

	ENTER;
	scsi_block_requests(vhost->host);
	list_for_each_entry(tgt, &vhost->targets, queue)
		ibmvfc_del_tgt(tgt);
	ibmvfc_set_host_state(vhost, state);
	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_TGT_DEL);
	vhost->events_to_log |= IBMVFC_AE_LINKDOWN;
	wake_up(&vhost->work_wait_q);
	LEAVE;
}

/**
 * ibmvfc_init_host - Start host initialization
 * @vhost:		ibmvfc host struct
 *
 * Return value:
 *	nothing
 **/
static void ibmvfc_init_host(struct ibmvfc_host *vhost)
{
	struct ibmvfc_target *tgt;

	if (vhost->action == IBMVFC_HOST_ACTION_INIT_WAIT) {
		if (++vhost->init_retries > IBMVFC_MAX_HOST_INIT_RETRIES) {
			dev_err(vhost->dev,
				"Host initialization retries exceeded. Taking adapter offline\n");
			ibmvfc_link_down(vhost, IBMVFC_HOST_OFFLINE);
			return;
		}
	}

	if (!ibmvfc_set_host_state(vhost, IBMVFC_INITIALIZING)) {
		memset(vhost->async_crq.msgs.async, 0, PAGE_SIZE);
		vhost->async_crq.cur = 0;

		list_for_each_entry(tgt, &vhost->targets, queue)
			ibmvfc_del_tgt(tgt);
		scsi_block_requests(vhost->host);
		ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_INIT);
		vhost->job_step = ibmvfc_npiv_login;
		wake_up(&vhost->work_wait_q);
	}
}

/**
 * ibmvfc_send_crq - Send a CRQ
 * @vhost:	ibmvfc host struct
 * @word1:	the first 64 bits of the data
 * @word2:	the second 64 bits of the data
 *
 * Return value:
 *	0 on success / other on failure
 **/
static int ibmvfc_send_crq(struct ibmvfc_host *vhost, u64 word1, u64 word2)
{
	struct vio_dev *vdev = to_vio_dev(vhost->dev);
	return plpar_hcall_norets(H_SEND_CRQ, vdev->unit_address, word1, word2);
}

static int ibmvfc_send_sub_crq(struct ibmvfc_host *vhost, u64 cookie, u64 word1,
			       u64 word2, u64 word3, u64 word4)
{
	struct vio_dev *vdev = to_vio_dev(vhost->dev);

	return plpar_hcall_norets(H_SEND_SUB_CRQ, vdev->unit_address, cookie,
				  word1, word2, word3, word4);
}

/**
 * ibmvfc_send_crq_init - Send a CRQ init message
 * @vhost:	ibmvfc host struct
 *
 * Return value:
 *	0 on success / other on failure
 **/
static int ibmvfc_send_crq_init(struct ibmvfc_host *vhost)
{
	ibmvfc_dbg(vhost, "Sending CRQ init\n");
	return ibmvfc_send_crq(vhost, 0xC001000000000000LL, 0);
}

/**
 * ibmvfc_send_crq_init_complete - Send a CRQ init complete message
 * @vhost:	ibmvfc host struct
 *
 * Return value:
 *	0 on success / other on failure
 **/
static int ibmvfc_send_crq_init_complete(struct ibmvfc_host *vhost)
{
	ibmvfc_dbg(vhost, "Sending CRQ init complete\n");
	return ibmvfc_send_crq(vhost, 0xC002000000000000LL, 0);
}

/**
 * ibmvfc_init_event_pool - Allocates and initializes the event pool for a host
 * @vhost:	ibmvfc host who owns the event pool
 * @queue:      ibmvfc queue struct
 * @size:       pool size
 *
 * Returns zero on success.
 **/
static int ibmvfc_init_event_pool(struct ibmvfc_host *vhost,
				  struct ibmvfc_queue *queue,
				  unsigned int size)
{
	int i;
	struct ibmvfc_event_pool *pool = &queue->evt_pool;

	ENTER;
	if (!size)
		return 0;

	pool->size = size;
	pool->events = kcalloc(size, sizeof(*pool->events), GFP_KERNEL);
	if (!pool->events)
		return -ENOMEM;

	pool->iu_storage = dma_alloc_coherent(vhost->dev,
					      size * sizeof(*pool->iu_storage),
					      &pool->iu_token, 0);

	if (!pool->iu_storage) {
		kfree(pool->events);
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&queue->sent);
	INIT_LIST_HEAD(&queue->free);
	spin_lock_init(&queue->l_lock);

	for (i = 0; i < size; ++i) {
		struct ibmvfc_event *evt = &pool->events[i];

		/*
		 * evt->active states
		 *  1 = in flight
		 *  0 = being completed
		 * -1 = free/freed
		 */
		atomic_set(&evt->active, -1);
		atomic_set(&evt->free, 1);
		evt->crq.valid = 0x80;
		evt->crq.ioba = cpu_to_be64(pool->iu_token + (sizeof(*evt->xfer_iu) * i));
		evt->xfer_iu = pool->iu_storage + i;
		evt->vhost = vhost;
		evt->queue = queue;
		evt->ext_list = NULL;
		list_add_tail(&evt->queue_list, &queue->free);
	}

	LEAVE;
	return 0;
}

/**
 * ibmvfc_free_event_pool - Frees memory of the event pool of a host
 * @vhost:	ibmvfc host who owns the event pool
 * @queue:      ibmvfc queue struct
 *
 **/
static void ibmvfc_free_event_pool(struct ibmvfc_host *vhost,
				   struct ibmvfc_queue *queue)
{
	int i;
	struct ibmvfc_event_pool *pool = &queue->evt_pool;

	ENTER;
	for (i = 0; i < pool->size; ++i) {
		list_del(&pool->events[i].queue_list);
		BUG_ON(atomic_read(&pool->events[i].free) != 1);
		if (pool->events[i].ext_list)
			dma_pool_free(vhost->sg_pool,
				      pool->events[i].ext_list,
				      pool->events[i].ext_list_token);
	}

	kfree(pool->events);
	dma_free_coherent(vhost->dev,
			  pool->size * sizeof(*pool->iu_storage),
			  pool->iu_storage, pool->iu_token);
	LEAVE;
}

/**
 * ibmvfc_free_queue - Deallocate queue
 * @vhost:	ibmvfc host struct
 * @queue:	ibmvfc queue struct
 *
 * Unmaps dma and deallocates page for messages
 **/
static void ibmvfc_free_queue(struct ibmvfc_host *vhost,
			      struct ibmvfc_queue *queue)
{
	struct device *dev = vhost->dev;

	dma_unmap_single(dev, queue->msg_token, PAGE_SIZE, DMA_BIDIRECTIONAL);
	free_page((unsigned long)queue->msgs.handle);
	queue->msgs.handle = NULL;

	ibmvfc_free_event_pool(vhost, queue);
}

/**
 * ibmvfc_release_crq_queue - Deallocates data and unregisters CRQ
 * @vhost:	ibmvfc host struct
 *
 * Frees irq, deallocates a page for messages, unmaps dma, and unregisters
 * the crq with the hypervisor.
 **/
static void ibmvfc_release_crq_queue(struct ibmvfc_host *vhost)
{
	long rc = 0;
	struct vio_dev *vdev = to_vio_dev(vhost->dev);
	struct ibmvfc_queue *crq = &vhost->crq;

	ibmvfc_dbg(vhost, "Releasing CRQ\n");
	free_irq(vdev->irq, vhost);
	tasklet_kill(&vhost->tasklet);
	do {
		if (rc)
			msleep(100);
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

	vhost->state = IBMVFC_NO_CRQ;
	vhost->logged_in = 0;

	ibmvfc_free_queue(vhost, crq);
}

/**
 * ibmvfc_reenable_crq_queue - reenables the CRQ
 * @vhost:	ibmvfc host struct
 *
 * Return value:
 *	0 on success / other on failure
 **/
static int ibmvfc_reenable_crq_queue(struct ibmvfc_host *vhost)
{
	int rc = 0;
	struct vio_dev *vdev = to_vio_dev(vhost->dev);
	unsigned long flags;

	ibmvfc_release_sub_crqs(vhost);

	/* Re-enable the CRQ */
	do {
		if (rc)
			msleep(100);
		rc = plpar_hcall_norets(H_ENABLE_CRQ, vdev->unit_address);
	} while (rc == H_IN_PROGRESS || rc == H_BUSY || H_IS_LONG_BUSY(rc));

	if (rc)
		dev_err(vhost->dev, "Error enabling adapter (rc=%d)\n", rc);

	spin_lock_irqsave(vhost->host->host_lock, flags);
	spin_lock(vhost->crq.q_lock);
	vhost->do_enquiry = 1;
	vhost->using_channels = 0;
	spin_unlock(vhost->crq.q_lock);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	ibmvfc_init_sub_crqs(vhost);

	return rc;
}

/**
 * ibmvfc_reset_crq - resets a crq after a failure
 * @vhost:	ibmvfc host struct
 *
 * Return value:
 *	0 on success / other on failure
 **/
static int ibmvfc_reset_crq(struct ibmvfc_host *vhost)
{
	int rc = 0;
	unsigned long flags;
	struct vio_dev *vdev = to_vio_dev(vhost->dev);
	struct ibmvfc_queue *crq = &vhost->crq;

	ibmvfc_release_sub_crqs(vhost);

	/* Close the CRQ */
	do {
		if (rc)
			msleep(100);
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

	spin_lock_irqsave(vhost->host->host_lock, flags);
	spin_lock(vhost->crq.q_lock);
	vhost->state = IBMVFC_NO_CRQ;
	vhost->logged_in = 0;
	vhost->do_enquiry = 1;
	vhost->using_channels = 0;

	/* Clean out the queue */
	memset(crq->msgs.crq, 0, PAGE_SIZE);
	crq->cur = 0;

	/* And re-open it again */
	rc = plpar_hcall_norets(H_REG_CRQ, vdev->unit_address,
				crq->msg_token, PAGE_SIZE);

	if (rc == H_CLOSED)
		/* Adapter is good, but other end is not ready */
		dev_warn(vhost->dev, "Partner adapter not ready\n");
	else if (rc != 0)
		dev_warn(vhost->dev, "Couldn't register crq (rc=%d)\n", rc);

	spin_unlock(vhost->crq.q_lock);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	ibmvfc_init_sub_crqs(vhost);

	return rc;
}

/**
 * ibmvfc_valid_event - Determines if event is valid.
 * @pool:	event_pool that contains the event
 * @evt:	ibmvfc event to be checked for validity
 *
 * Return value:
 *	1 if event is valid / 0 if event is not valid
 **/
static int ibmvfc_valid_event(struct ibmvfc_event_pool *pool,
			      struct ibmvfc_event *evt)
{
	int index = evt - pool->events;
	if (index < 0 || index >= pool->size)	/* outside of bounds */
		return 0;
	if (evt != pool->events + index)	/* unaligned */
		return 0;
	return 1;
}

/**
 * ibmvfc_free_event - Free the specified event
 * @evt:	ibmvfc_event to be freed
 *
 **/
static void ibmvfc_free_event(struct ibmvfc_event *evt)
{
	struct ibmvfc_event_pool *pool = &evt->queue->evt_pool;
	unsigned long flags;

	BUG_ON(!ibmvfc_valid_event(pool, evt));
	BUG_ON(atomic_inc_return(&evt->free) != 1);
	BUG_ON(atomic_dec_and_test(&evt->active));

	spin_lock_irqsave(&evt->queue->l_lock, flags);
	list_add_tail(&evt->queue_list, &evt->queue->free);
	if (evt->eh_comp)
		complete(evt->eh_comp);
	spin_unlock_irqrestore(&evt->queue->l_lock, flags);
}

/**
 * ibmvfc_scsi_eh_done - EH done function for queuecommand commands
 * @evt:	ibmvfc event struct
 *
 * This function does not setup any error status, that must be done
 * before this function gets called.
 **/
static void ibmvfc_scsi_eh_done(struct ibmvfc_event *evt)
{
	struct scsi_cmnd *cmnd = evt->cmnd;

	if (cmnd) {
		scsi_dma_unmap(cmnd);
		cmnd->scsi_done(cmnd);
	}

	ibmvfc_free_event(evt);
}

/**
 * ibmvfc_complete_purge - Complete failed command list
 * @purge_list:		list head of failed commands
 *
 * This function runs completions on commands to fail as a result of a
 * host reset or platform migration.
 **/
static void ibmvfc_complete_purge(struct list_head *purge_list)
{
	struct ibmvfc_event *evt, *pos;

	list_for_each_entry_safe(evt, pos, purge_list, queue_list) {
		list_del(&evt->queue_list);
		ibmvfc_trc_end(evt);
		evt->done(evt);
	}
}

/**
 * ibmvfc_fail_request - Fail request with specified error code
 * @evt:		ibmvfc event struct
 * @error_code:	error code to fail request with
 *
 * Return value:
 *	none
 **/
static void ibmvfc_fail_request(struct ibmvfc_event *evt, int error_code)
{
	/*
	 * Anything we are failing should still be active. Otherwise, it
	 * implies we already got a response for the command and are doing
	 * something bad like double completing it.
	 */
	BUG_ON(!atomic_dec_and_test(&evt->active));
	if (evt->cmnd) {
		evt->cmnd->result = (error_code << 16);
		evt->done = ibmvfc_scsi_eh_done;
	} else
		evt->xfer_iu->mad_common.status = cpu_to_be16(IBMVFC_MAD_DRIVER_FAILED);

	del_timer(&evt->timer);
}

/**
 * ibmvfc_purge_requests - Our virtual adapter just shut down. Purge any sent requests
 * @vhost:		ibmvfc host struct
 * @error_code:	error code to fail requests with
 *
 * Return value:
 *	none
 **/
static void ibmvfc_purge_requests(struct ibmvfc_host *vhost, int error_code)
{
	struct ibmvfc_event *evt, *pos;
	struct ibmvfc_queue *queues = vhost->scsi_scrqs.scrqs;
	unsigned long flags;
	int hwqs = 0;
	int i;

	if (vhost->using_channels)
		hwqs = vhost->scsi_scrqs.active_queues;

	ibmvfc_dbg(vhost, "Purging all requests\n");
	spin_lock_irqsave(&vhost->crq.l_lock, flags);
	list_for_each_entry_safe(evt, pos, &vhost->crq.sent, queue_list)
		ibmvfc_fail_request(evt, error_code);
	list_splice_init(&vhost->crq.sent, &vhost->purge);
	spin_unlock_irqrestore(&vhost->crq.l_lock, flags);

	for (i = 0; i < hwqs; i++) {
		spin_lock_irqsave(queues[i].q_lock, flags);
		spin_lock(&queues[i].l_lock);
		list_for_each_entry_safe(evt, pos, &queues[i].sent, queue_list)
			ibmvfc_fail_request(evt, error_code);
		list_splice_init(&queues[i].sent, &vhost->purge);
		spin_unlock(&queues[i].l_lock);
		spin_unlock_irqrestore(queues[i].q_lock, flags);
	}
}

/**
 * ibmvfc_hard_reset_host - Reset the connection to the server by breaking the CRQ
 * @vhost:	struct ibmvfc host to reset
 **/
static void ibmvfc_hard_reset_host(struct ibmvfc_host *vhost)
{
	ibmvfc_purge_requests(vhost, DID_ERROR);
	ibmvfc_link_down(vhost, IBMVFC_LINK_DOWN);
	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_RESET);
}

/**
 * __ibmvfc_reset_host - Reset the connection to the server (no locking)
 * @vhost:	struct ibmvfc host to reset
 **/
static void __ibmvfc_reset_host(struct ibmvfc_host *vhost)
{
	if (vhost->logged_in && vhost->action != IBMVFC_HOST_ACTION_LOGO_WAIT &&
	    !ibmvfc_set_host_state(vhost, IBMVFC_INITIALIZING)) {
		scsi_block_requests(vhost->host);
		ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_LOGO);
		vhost->job_step = ibmvfc_npiv_logout;
		wake_up(&vhost->work_wait_q);
	} else
		ibmvfc_hard_reset_host(vhost);
}

/**
 * ibmvfc_reset_host - Reset the connection to the server
 * @vhost:	ibmvfc host struct
 **/
static void ibmvfc_reset_host(struct ibmvfc_host *vhost)
{
	unsigned long flags;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	__ibmvfc_reset_host(vhost);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
}

/**
 * ibmvfc_retry_host_init - Retry host initialization if allowed
 * @vhost:	ibmvfc host struct
 *
 * Returns: 1 if init will be retried / 0 if not
 *
 **/
static int ibmvfc_retry_host_init(struct ibmvfc_host *vhost)
{
	int retry = 0;

	if (vhost->action == IBMVFC_HOST_ACTION_INIT_WAIT) {
		vhost->delay_init = 1;
		if (++vhost->init_retries > IBMVFC_MAX_HOST_INIT_RETRIES) {
			dev_err(vhost->dev,
				"Host initialization retries exceeded. Taking adapter offline\n");
			ibmvfc_link_down(vhost, IBMVFC_HOST_OFFLINE);
		} else if (vhost->init_retries == IBMVFC_MAX_HOST_INIT_RETRIES)
			__ibmvfc_reset_host(vhost);
		else {
			ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_INIT);
			retry = 1;
		}
	}

	wake_up(&vhost->work_wait_q);
	return retry;
}

/**
 * __ibmvfc_get_target - Find the specified scsi_target (no locking)
 * @starget:	scsi target struct
 *
 * Return value:
 *	ibmvfc_target struct / NULL if not found
 **/
static struct ibmvfc_target *__ibmvfc_get_target(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ibmvfc_host *vhost = shost_priv(shost);
	struct ibmvfc_target *tgt;

	list_for_each_entry(tgt, &vhost->targets, queue)
		if (tgt->target_id == starget->id) {
			kref_get(&tgt->kref);
			return tgt;
		}
	return NULL;
}

/**
 * ibmvfc_get_target - Find the specified scsi_target
 * @starget:	scsi target struct
 *
 * Return value:
 *	ibmvfc_target struct / NULL if not found
 **/
static struct ibmvfc_target *ibmvfc_get_target(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ibmvfc_target *tgt;
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	tgt = __ibmvfc_get_target(starget);
	spin_unlock_irqrestore(shost->host_lock, flags);
	return tgt;
}

/**
 * ibmvfc_get_host_speed - Get host port speed
 * @shost:		scsi host struct
 *
 * Return value:
 * 	none
 **/
static void ibmvfc_get_host_speed(struct Scsi_Host *shost)
{
	struct ibmvfc_host *vhost = shost_priv(shost);
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	if (vhost->state == IBMVFC_ACTIVE) {
		switch (be64_to_cpu(vhost->login_buf->resp.link_speed) / 100) {
		case 1:
			fc_host_speed(shost) = FC_PORTSPEED_1GBIT;
			break;
		case 2:
			fc_host_speed(shost) = FC_PORTSPEED_2GBIT;
			break;
		case 4:
			fc_host_speed(shost) = FC_PORTSPEED_4GBIT;
			break;
		case 8:
			fc_host_speed(shost) = FC_PORTSPEED_8GBIT;
			break;
		case 10:
			fc_host_speed(shost) = FC_PORTSPEED_10GBIT;
			break;
		case 16:
			fc_host_speed(shost) = FC_PORTSPEED_16GBIT;
			break;
		default:
			ibmvfc_log(vhost, 3, "Unknown port speed: %lld Gbit\n",
				   be64_to_cpu(vhost->login_buf->resp.link_speed) / 100);
			fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
			break;
		}
	} else
		fc_host_speed(shost) = FC_PORTSPEED_UNKNOWN;
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/**
 * ibmvfc_get_host_port_state - Get host port state
 * @shost:		scsi host struct
 *
 * Return value:
 * 	none
 **/
static void ibmvfc_get_host_port_state(struct Scsi_Host *shost)
{
	struct ibmvfc_host *vhost = shost_priv(shost);
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	switch (vhost->state) {
	case IBMVFC_INITIALIZING:
	case IBMVFC_ACTIVE:
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
		break;
	case IBMVFC_LINK_DOWN:
		fc_host_port_state(shost) = FC_PORTSTATE_LINKDOWN;
		break;
	case IBMVFC_LINK_DEAD:
	case IBMVFC_HOST_OFFLINE:
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
		break;
	case IBMVFC_HALTED:
		fc_host_port_state(shost) = FC_PORTSTATE_BLOCKED;
		break;
	case IBMVFC_NO_CRQ:
		fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
		break;
	default:
		ibmvfc_log(vhost, 3, "Unknown port state: %d\n", vhost->state);
		fc_host_port_state(shost) = FC_PORTSTATE_UNKNOWN;
		break;
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/**
 * ibmvfc_set_rport_dev_loss_tmo - Set rport's device loss timeout
 * @rport:		rport struct
 * @timeout:	timeout value
 *
 * Return value:
 * 	none
 **/
static void ibmvfc_set_rport_dev_loss_tmo(struct fc_rport *rport, u32 timeout)
{
	if (timeout)
		rport->dev_loss_tmo = timeout;
	else
		rport->dev_loss_tmo = 1;
}

/**
 * ibmvfc_release_tgt - Free memory allocated for a target
 * @kref:		kref struct
 *
 **/
static void ibmvfc_release_tgt(struct kref *kref)
{
	struct ibmvfc_target *tgt = container_of(kref, struct ibmvfc_target, kref);
	kfree(tgt);
}

/**
 * ibmvfc_get_starget_node_name - Get SCSI target's node name
 * @starget:	scsi target struct
 *
 * Return value:
 * 	none
 **/
static void ibmvfc_get_starget_node_name(struct scsi_target *starget)
{
	struct ibmvfc_target *tgt = ibmvfc_get_target(starget);
	fc_starget_port_name(starget) = tgt ? tgt->ids.node_name : 0;
	if (tgt)
		kref_put(&tgt->kref, ibmvfc_release_tgt);
}

/**
 * ibmvfc_get_starget_port_name - Get SCSI target's port name
 * @starget:	scsi target struct
 *
 * Return value:
 * 	none
 **/
static void ibmvfc_get_starget_port_name(struct scsi_target *starget)
{
	struct ibmvfc_target *tgt = ibmvfc_get_target(starget);
	fc_starget_port_name(starget) = tgt ? tgt->ids.port_name : 0;
	if (tgt)
		kref_put(&tgt->kref, ibmvfc_release_tgt);
}

/**
 * ibmvfc_get_starget_port_id - Get SCSI target's port ID
 * @starget:	scsi target struct
 *
 * Return value:
 * 	none
 **/
static void ibmvfc_get_starget_port_id(struct scsi_target *starget)
{
	struct ibmvfc_target *tgt = ibmvfc_get_target(starget);
	fc_starget_port_id(starget) = tgt ? tgt->scsi_id : -1;
	if (tgt)
		kref_put(&tgt->kref, ibmvfc_release_tgt);
}

/**
 * ibmvfc_wait_while_resetting - Wait while the host resets
 * @vhost:		ibmvfc host struct
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ibmvfc_wait_while_resetting(struct ibmvfc_host *vhost)
{
	long timeout = wait_event_timeout(vhost->init_wait_q,
					  ((vhost->state == IBMVFC_ACTIVE ||
					    vhost->state == IBMVFC_HOST_OFFLINE ||
					    vhost->state == IBMVFC_LINK_DEAD) &&
					   vhost->action == IBMVFC_HOST_ACTION_NONE),
					  (init_timeout * HZ));

	return timeout ? 0 : -EIO;
}

/**
 * ibmvfc_issue_fc_host_lip - Re-initiate link initialization
 * @shost:		scsi host struct
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int ibmvfc_issue_fc_host_lip(struct Scsi_Host *shost)
{
	struct ibmvfc_host *vhost = shost_priv(shost);

	dev_err(vhost->dev, "Initiating host LIP. Resetting connection\n");
	ibmvfc_reset_host(vhost);
	return ibmvfc_wait_while_resetting(vhost);
}

/**
 * ibmvfc_gather_partition_info - Gather info about the LPAR
 * @vhost:      ibmvfc host struct
 *
 * Return value:
 *	none
 **/
static void ibmvfc_gather_partition_info(struct ibmvfc_host *vhost)
{
	struct device_node *rootdn;
	const char *name;
	const unsigned int *num;

	rootdn = of_find_node_by_path("/");
	if (!rootdn)
		return;

	name = of_get_property(rootdn, "ibm,partition-name", NULL);
	if (name)
		strncpy(vhost->partition_name, name, sizeof(vhost->partition_name));
	num = of_get_property(rootdn, "ibm,partition-no", NULL);
	if (num)
		vhost->partition_number = *num;
	of_node_put(rootdn);
}

/**
 * ibmvfc_set_login_info - Setup info for NPIV login
 * @vhost:	ibmvfc host struct
 *
 * Return value:
 *	none
 **/
static void ibmvfc_set_login_info(struct ibmvfc_host *vhost)
{
	struct ibmvfc_npiv_login *login_info = &vhost->login_info;
	struct ibmvfc_queue *async_crq = &vhost->async_crq;
	struct device_node *of_node = vhost->dev->of_node;
	const char *location;

	memset(login_info, 0, sizeof(*login_info));

	login_info->ostype = cpu_to_be32(IBMVFC_OS_LINUX);
	login_info->max_dma_len = cpu_to_be64(IBMVFC_MAX_SECTORS << 9);
	login_info->max_payload = cpu_to_be32(sizeof(struct ibmvfc_fcp_cmd_iu));
	login_info->max_response = cpu_to_be32(sizeof(struct ibmvfc_fcp_rsp));
	login_info->partition_num = cpu_to_be32(vhost->partition_number);
	login_info->vfc_frame_version = cpu_to_be32(1);
	login_info->fcp_version = cpu_to_be16(3);
	login_info->flags = cpu_to_be16(IBMVFC_FLUSH_ON_HALT);
	if (vhost->client_migrated)
		login_info->flags |= cpu_to_be16(IBMVFC_CLIENT_MIGRATED);

	login_info->max_cmds = cpu_to_be32(max_requests + IBMVFC_NUM_INTERNAL_REQ);
	login_info->capabilities = cpu_to_be64(IBMVFC_CAN_MIGRATE | IBMVFC_CAN_SEND_VF_WWPN);

	if (vhost->mq_enabled || vhost->using_channels)
		login_info->capabilities |= cpu_to_be64(IBMVFC_CAN_USE_CHANNELS);

	login_info->async.va = cpu_to_be64(vhost->async_crq.msg_token);
	login_info->async.len = cpu_to_be32(async_crq->size *
					    sizeof(*async_crq->msgs.async));
	strncpy(login_info->partition_name, vhost->partition_name, IBMVFC_MAX_NAME);
	strncpy(login_info->device_name,
		dev_name(&vhost->host->shost_gendev), IBMVFC_MAX_NAME);

	location = of_get_property(of_node, "ibm,loc-code", NULL);
	location = location ? location : dev_name(vhost->dev);
	strncpy(login_info->drc_name, location, IBMVFC_MAX_NAME);
}

/**
 * ibmvfc_get_event - Gets the next free event in pool
 * @queue:      ibmvfc queue struct
 *
 * Returns a free event from the pool.
 **/
static struct ibmvfc_event *ibmvfc_get_event(struct ibmvfc_queue *queue)
{
	struct ibmvfc_event *evt;
	unsigned long flags;

	spin_lock_irqsave(&queue->l_lock, flags);
	BUG_ON(list_empty(&queue->free));
	evt = list_entry(queue->free.next, struct ibmvfc_event, queue_list);
	atomic_set(&evt->free, 0);
	list_del(&evt->queue_list);
	spin_unlock_irqrestore(&queue->l_lock, flags);
	return evt;
}

/**
 * ibmvfc_locked_done - Calls evt completion with host_lock held
 * @evt:	ibmvfc evt to complete
 *
 * All non-scsi command completion callbacks have the expectation that the
 * host_lock is held. This callback is used by ibmvfc_init_event to wrap a
 * MAD evt with the host_lock.
 **/
static void ibmvfc_locked_done(struct ibmvfc_event *evt)
{
	unsigned long flags;

	spin_lock_irqsave(evt->vhost->host->host_lock, flags);
	evt->_done(evt);
	spin_unlock_irqrestore(evt->vhost->host->host_lock, flags);
}

/**
 * ibmvfc_init_event - Initialize fields in an event struct that are always
 *				required.
 * @evt:	The event
 * @done:	Routine to call when the event is responded to
 * @format:	SRP or MAD format
 **/
static void ibmvfc_init_event(struct ibmvfc_event *evt,
			      void (*done) (struct ibmvfc_event *), u8 format)
{
	evt->cmnd = NULL;
	evt->sync_iu = NULL;
	evt->eh_comp = NULL;
	evt->crq.format = format;
	if (format == IBMVFC_CMD_FORMAT)
		evt->done = done;
	else {
		evt->_done = done;
		evt->done = ibmvfc_locked_done;
	}
	evt->hwq = 0;
}

/**
 * ibmvfc_map_sg_list - Initialize scatterlist
 * @scmd:	scsi command struct
 * @nseg:	number of scatterlist segments
 * @md:	memory descriptor list to initialize
 **/
static void ibmvfc_map_sg_list(struct scsi_cmnd *scmd, int nseg,
			       struct srp_direct_buf *md)
{
	int i;
	struct scatterlist *sg;

	scsi_for_each_sg(scmd, sg, nseg, i) {
		md[i].va = cpu_to_be64(sg_dma_address(sg));
		md[i].len = cpu_to_be32(sg_dma_len(sg));
		md[i].key = 0;
	}
}

/**
 * ibmvfc_map_sg_data - Maps dma for a scatterlist and initializes descriptor fields
 * @scmd:		struct scsi_cmnd with the scatterlist
 * @evt:		ibmvfc event struct
 * @vfc_cmd:	vfc_cmd that contains the memory descriptor
 * @dev:		device for which to map dma memory
 *
 * Returns:
 *	0 on success / non-zero on failure
 **/
static int ibmvfc_map_sg_data(struct scsi_cmnd *scmd,
			      struct ibmvfc_event *evt,
			      struct ibmvfc_cmd *vfc_cmd, struct device *dev)
{

	int sg_mapped;
	struct srp_direct_buf *data = &vfc_cmd->ioba;
	struct ibmvfc_host *vhost = dev_get_drvdata(dev);
	struct ibmvfc_fcp_cmd_iu *iu = ibmvfc_get_fcp_iu(evt->vhost, vfc_cmd);

	if (cls3_error)
		vfc_cmd->flags |= cpu_to_be16(IBMVFC_CLASS_3_ERR);

	sg_mapped = scsi_dma_map(scmd);
	if (!sg_mapped) {
		vfc_cmd->flags |= cpu_to_be16(IBMVFC_NO_MEM_DESC);
		return 0;
	} else if (unlikely(sg_mapped < 0)) {
		if (vhost->log_level > IBMVFC_DEFAULT_LOG_LEVEL)
			scmd_printk(KERN_ERR, scmd, "Failed to map DMA buffer for command\n");
		return sg_mapped;
	}

	if (scmd->sc_data_direction == DMA_TO_DEVICE) {
		vfc_cmd->flags |= cpu_to_be16(IBMVFC_WRITE);
		iu->add_cdb_len |= IBMVFC_WRDATA;
	} else {
		vfc_cmd->flags |= cpu_to_be16(IBMVFC_READ);
		iu->add_cdb_len |= IBMVFC_RDDATA;
	}

	if (sg_mapped == 1) {
		ibmvfc_map_sg_list(scmd, sg_mapped, data);
		return 0;
	}

	vfc_cmd->flags |= cpu_to_be16(IBMVFC_SCATTERLIST);

	if (!evt->ext_list) {
		evt->ext_list = dma_pool_alloc(vhost->sg_pool, GFP_ATOMIC,
					       &evt->ext_list_token);

		if (!evt->ext_list) {
			scsi_dma_unmap(scmd);
			if (vhost->log_level > IBMVFC_DEFAULT_LOG_LEVEL)
				scmd_printk(KERN_ERR, scmd, "Can't allocate memory for scatterlist\n");
			return -ENOMEM;
		}
	}

	ibmvfc_map_sg_list(scmd, sg_mapped, evt->ext_list);

	data->va = cpu_to_be64(evt->ext_list_token);
	data->len = cpu_to_be32(sg_mapped * sizeof(struct srp_direct_buf));
	data->key = 0;
	return 0;
}

/**
 * ibmvfc_timeout - Internal command timeout handler
 * @t:	struct ibmvfc_event that timed out
 *
 * Called when an internally generated command times out
 **/
static void ibmvfc_timeout(struct timer_list *t)
{
	struct ibmvfc_event *evt = from_timer(evt, t, timer);
	struct ibmvfc_host *vhost = evt->vhost;
	dev_err(vhost->dev, "Command timed out (%p). Resetting connection\n", evt);
	ibmvfc_reset_host(vhost);
}

/**
 * ibmvfc_send_event - Transforms event to u64 array and calls send_crq()
 * @evt:		event to be sent
 * @vhost:		ibmvfc host struct
 * @timeout:	timeout in seconds - 0 means do not time command
 *
 * Returns the value returned from ibmvfc_send_crq(). (Zero for success)
 **/
static int ibmvfc_send_event(struct ibmvfc_event *evt,
			     struct ibmvfc_host *vhost, unsigned long timeout)
{
	__be64 *crq_as_u64 = (__be64 *) &evt->crq;
	unsigned long flags;
	int rc;

	/* Copy the IU into the transfer area */
	*evt->xfer_iu = evt->iu;
	if (evt->crq.format == IBMVFC_CMD_FORMAT)
		evt->xfer_iu->cmd.tag = cpu_to_be64((u64)evt);
	else if (evt->crq.format == IBMVFC_MAD_FORMAT)
		evt->xfer_iu->mad_common.tag = cpu_to_be64((u64)evt);
	else
		BUG();

	timer_setup(&evt->timer, ibmvfc_timeout, 0);

	if (timeout) {
		evt->timer.expires = jiffies + (timeout * HZ);
		add_timer(&evt->timer);
	}

	spin_lock_irqsave(&evt->queue->l_lock, flags);
	list_add_tail(&evt->queue_list, &evt->queue->sent);
	atomic_set(&evt->active, 1);

	mb();

	if (evt->queue->fmt == IBMVFC_SUB_CRQ_FMT)
		rc = ibmvfc_send_sub_crq(vhost,
					 evt->queue->vios_cookie,
					 be64_to_cpu(crq_as_u64[0]),
					 be64_to_cpu(crq_as_u64[1]),
					 0, 0);
	else
		rc = ibmvfc_send_crq(vhost, be64_to_cpu(crq_as_u64[0]),
				     be64_to_cpu(crq_as_u64[1]));

	if (rc) {
		atomic_set(&evt->active, 0);
		list_del(&evt->queue_list);
		spin_unlock_irqrestore(&evt->queue->l_lock, flags);
		del_timer(&evt->timer);

		/* If send_crq returns H_CLOSED, return SCSI_MLQUEUE_HOST_BUSY.
		 * Firmware will send a CRQ with a transport event (0xFF) to
		 * tell this client what has happened to the transport. This
		 * will be handled in ibmvfc_handle_crq()
		 */
		if (rc == H_CLOSED) {
			if (printk_ratelimit())
				dev_warn(vhost->dev, "Send warning. Receive queue closed, will retry.\n");
			if (evt->cmnd)
				scsi_dma_unmap(evt->cmnd);
			ibmvfc_free_event(evt);
			return SCSI_MLQUEUE_HOST_BUSY;
		}

		dev_err(vhost->dev, "Send error (rc=%d)\n", rc);
		if (evt->cmnd) {
			evt->cmnd->result = DID_ERROR << 16;
			evt->done = ibmvfc_scsi_eh_done;
		} else
			evt->xfer_iu->mad_common.status = cpu_to_be16(IBMVFC_MAD_CRQ_ERROR);

		evt->done(evt);
	} else {
		spin_unlock_irqrestore(&evt->queue->l_lock, flags);
		ibmvfc_trc_start(evt);
	}

	return 0;
}

/**
 * ibmvfc_log_error - Log an error for the failed command if appropriate
 * @evt:	ibmvfc event to log
 *
 **/
static void ibmvfc_log_error(struct ibmvfc_event *evt)
{
	struct ibmvfc_cmd *vfc_cmd = &evt->xfer_iu->cmd;
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_fcp_rsp *rsp = ibmvfc_get_fcp_rsp(vhost, vfc_cmd);
	struct scsi_cmnd *cmnd = evt->cmnd;
	const char *err = unknown_error;
	int index = ibmvfc_get_err_index(be16_to_cpu(vfc_cmd->status), be16_to_cpu(vfc_cmd->error));
	int logerr = 0;
	int rsp_code = 0;

	if (index >= 0) {
		logerr = cmd_status[index].log;
		err = cmd_status[index].name;
	}

	if (!logerr && (vhost->log_level <= (IBMVFC_DEFAULT_LOG_LEVEL + 1)))
		return;

	if (rsp->flags & FCP_RSP_LEN_VALID)
		rsp_code = rsp->data.info.rsp_code;

	scmd_printk(KERN_ERR, cmnd, "Command (%02X) : %s (%x:%x) "
		    "flags: %x fcp_rsp: %x, resid=%d, scsi_status: %x\n",
		    cmnd->cmnd[0], err, be16_to_cpu(vfc_cmd->status), be16_to_cpu(vfc_cmd->error),
		    rsp->flags, rsp_code, scsi_get_resid(cmnd), rsp->scsi_status);
}

/**
 * ibmvfc_relogin - Log back into the specified device
 * @sdev:	scsi device struct
 *
 **/
static void ibmvfc_relogin(struct scsi_device *sdev)
{
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	struct ibmvfc_target *tgt;
	unsigned long flags;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	list_for_each_entry(tgt, &vhost->targets, queue) {
		if (rport == tgt->rport) {
			ibmvfc_del_tgt(tgt);
			break;
		}
	}

	ibmvfc_reinit_host(vhost);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
}

/**
 * ibmvfc_scsi_done - Handle responses from commands
 * @evt:	ibmvfc event to be handled
 *
 * Used as a callback when sending scsi cmds.
 **/
static void ibmvfc_scsi_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_cmd *vfc_cmd = &evt->xfer_iu->cmd;
	struct ibmvfc_fcp_rsp *rsp = ibmvfc_get_fcp_rsp(evt->vhost, vfc_cmd);
	struct scsi_cmnd *cmnd = evt->cmnd;
	u32 rsp_len = 0;
	u32 sense_len = be32_to_cpu(rsp->fcp_sense_len);

	if (cmnd) {
		if (be16_to_cpu(vfc_cmd->response_flags) & IBMVFC_ADAPTER_RESID_VALID)
			scsi_set_resid(cmnd, be32_to_cpu(vfc_cmd->adapter_resid));
		else if (rsp->flags & FCP_RESID_UNDER)
			scsi_set_resid(cmnd, be32_to_cpu(rsp->fcp_resid));
		else
			scsi_set_resid(cmnd, 0);

		if (vfc_cmd->status) {
			cmnd->result = ibmvfc_get_err_result(evt->vhost, vfc_cmd);

			if (rsp->flags & FCP_RSP_LEN_VALID)
				rsp_len = be32_to_cpu(rsp->fcp_rsp_len);
			if ((sense_len + rsp_len) > SCSI_SENSE_BUFFERSIZE)
				sense_len = SCSI_SENSE_BUFFERSIZE - rsp_len;
			if ((rsp->flags & FCP_SNS_LEN_VALID) && rsp->fcp_sense_len && rsp_len <= 8)
				memcpy(cmnd->sense_buffer, rsp->data.sense + rsp_len, sense_len);
			if ((be16_to_cpu(vfc_cmd->status) & IBMVFC_VIOS_FAILURE) &&
			    (be16_to_cpu(vfc_cmd->error) == IBMVFC_PLOGI_REQUIRED))
				ibmvfc_relogin(cmnd->device);

			if (!cmnd->result && (!scsi_get_resid(cmnd) || (rsp->flags & FCP_RESID_OVER)))
				cmnd->result = (DID_ERROR << 16);

			ibmvfc_log_error(evt);
		}

		if (!cmnd->result &&
		    (scsi_bufflen(cmnd) - scsi_get_resid(cmnd) < cmnd->underflow))
			cmnd->result = (DID_ERROR << 16);

		scsi_dma_unmap(cmnd);
		cmnd->scsi_done(cmnd);
	}

	ibmvfc_free_event(evt);
}

/**
 * ibmvfc_host_chkready - Check if the host can accept commands
 * @vhost:	 struct ibmvfc host
 *
 * Returns:
 *	1 if host can accept command / 0 if not
 **/
static inline int ibmvfc_host_chkready(struct ibmvfc_host *vhost)
{
	int result = 0;

	switch (vhost->state) {
	case IBMVFC_LINK_DEAD:
	case IBMVFC_HOST_OFFLINE:
		result = DID_NO_CONNECT << 16;
		break;
	case IBMVFC_NO_CRQ:
	case IBMVFC_INITIALIZING:
	case IBMVFC_HALTED:
	case IBMVFC_LINK_DOWN:
		result = DID_REQUEUE << 16;
		break;
	case IBMVFC_ACTIVE:
		result = 0;
		break;
	}

	return result;
}

static struct ibmvfc_cmd *ibmvfc_init_vfc_cmd(struct ibmvfc_event *evt, struct scsi_device *sdev)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_cmd *vfc_cmd = &evt->iu.cmd;
	struct ibmvfc_fcp_cmd_iu *iu = ibmvfc_get_fcp_iu(vhost, vfc_cmd);
	struct ibmvfc_fcp_rsp *rsp = ibmvfc_get_fcp_rsp(vhost, vfc_cmd);
	size_t offset;

	memset(vfc_cmd, 0, sizeof(*vfc_cmd));
	if (ibmvfc_check_caps(vhost, IBMVFC_HANDLE_VF_WWPN)) {
		offset = offsetof(struct ibmvfc_cmd, v2.rsp);
		vfc_cmd->target_wwpn = cpu_to_be64(rport->port_name);
	} else
		offset = offsetof(struct ibmvfc_cmd, v1.rsp);
	vfc_cmd->resp.va = cpu_to_be64(be64_to_cpu(evt->crq.ioba) + offset);
	vfc_cmd->resp.len = cpu_to_be32(sizeof(*rsp));
	vfc_cmd->frame_type = cpu_to_be32(IBMVFC_SCSI_FCP_TYPE);
	vfc_cmd->payload_len = cpu_to_be32(sizeof(*iu));
	vfc_cmd->resp_len = cpu_to_be32(sizeof(*rsp));
	vfc_cmd->cancel_key = cpu_to_be32((unsigned long)sdev->hostdata);
	vfc_cmd->tgt_scsi_id = cpu_to_be64(rport->port_id);
	int_to_scsilun(sdev->lun, &iu->lun);

	return vfc_cmd;
}

/**
 * ibmvfc_queuecommand - The queuecommand function of the scsi template
 * @shost:	scsi host struct
 * @cmnd:	struct scsi_cmnd to be executed
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_queuecommand(struct Scsi_Host *shost, struct scsi_cmnd *cmnd)
{
	struct ibmvfc_host *vhost = shost_priv(shost);
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));
	struct ibmvfc_cmd *vfc_cmd;
	struct ibmvfc_fcp_cmd_iu *iu;
	struct ibmvfc_event *evt;
	u32 tag_and_hwq = blk_mq_unique_tag(scsi_cmd_to_rq(cmnd));
	u16 hwq = blk_mq_unique_tag_to_hwq(tag_and_hwq);
	u16 scsi_channel;
	int rc;

	if (unlikely((rc = fc_remote_port_chkready(rport))) ||
	    unlikely((rc = ibmvfc_host_chkready(vhost)))) {
		cmnd->result = rc;
		cmnd->scsi_done(cmnd);
		return 0;
	}

	cmnd->result = (DID_OK << 16);
	if (vhost->using_channels) {
		scsi_channel = hwq % vhost->scsi_scrqs.active_queues;
		evt = ibmvfc_get_event(&vhost->scsi_scrqs.scrqs[scsi_channel]);
		evt->hwq = hwq % vhost->scsi_scrqs.active_queues;
	} else
		evt = ibmvfc_get_event(&vhost->crq);

	ibmvfc_init_event(evt, ibmvfc_scsi_done, IBMVFC_CMD_FORMAT);
	evt->cmnd = cmnd;

	vfc_cmd = ibmvfc_init_vfc_cmd(evt, cmnd->device);
	iu = ibmvfc_get_fcp_iu(vhost, vfc_cmd);

	iu->xfer_len = cpu_to_be32(scsi_bufflen(cmnd));
	memcpy(iu->cdb, cmnd->cmnd, cmnd->cmd_len);

	if (cmnd->flags & SCMD_TAGGED) {
		vfc_cmd->task_tag = cpu_to_be64(scsi_cmd_to_rq(cmnd)->tag);
		iu->pri_task_attr = IBMVFC_SIMPLE_TASK;
	}

	vfc_cmd->correlation = cpu_to_be64((u64)evt);

	if (likely(!(rc = ibmvfc_map_sg_data(cmnd, evt, vfc_cmd, vhost->dev))))
		return ibmvfc_send_event(evt, vhost, 0);

	ibmvfc_free_event(evt);
	if (rc == -ENOMEM)
		return SCSI_MLQUEUE_HOST_BUSY;

	if (vhost->log_level > IBMVFC_DEFAULT_LOG_LEVEL)
		scmd_printk(KERN_ERR, cmnd,
			    "Failed to map DMA buffer for command. rc=%d\n", rc);

	cmnd->result = DID_ERROR << 16;
	cmnd->scsi_done(cmnd);
	return 0;
}

/**
 * ibmvfc_sync_completion - Signal that a synchronous command has completed
 * @evt:	ibmvfc event struct
 *
 **/
static void ibmvfc_sync_completion(struct ibmvfc_event *evt)
{
	/* copy the response back */
	if (evt->sync_iu)
		*evt->sync_iu = *evt->xfer_iu;

	complete(&evt->comp);
}

/**
 * ibmvfc_bsg_timeout_done - Completion handler for cancelling BSG commands
 * @evt:	struct ibmvfc_event
 *
 **/
static void ibmvfc_bsg_timeout_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_host *vhost = evt->vhost;

	ibmvfc_free_event(evt);
	vhost->aborting_passthru = 0;
	dev_info(vhost->dev, "Passthru command cancelled\n");
}

/**
 * ibmvfc_bsg_timeout - Handle a BSG timeout
 * @job:	struct bsg_job that timed out
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_bsg_timeout(struct bsg_job *job)
{
	struct ibmvfc_host *vhost = shost_priv(fc_bsg_to_shost(job));
	unsigned long port_id = (unsigned long)job->dd_data;
	struct ibmvfc_event *evt;
	struct ibmvfc_tmf *tmf;
	unsigned long flags;
	int rc;

	ENTER;
	spin_lock_irqsave(vhost->host->host_lock, flags);
	if (vhost->aborting_passthru || vhost->state != IBMVFC_ACTIVE) {
		__ibmvfc_reset_host(vhost);
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		return 0;
	}

	vhost->aborting_passthru = 1;
	evt = ibmvfc_get_event(&vhost->crq);
	ibmvfc_init_event(evt, ibmvfc_bsg_timeout_done, IBMVFC_MAD_FORMAT);

	tmf = &evt->iu.tmf;
	memset(tmf, 0, sizeof(*tmf));
	tmf->common.version = cpu_to_be32(1);
	tmf->common.opcode = cpu_to_be32(IBMVFC_TMF_MAD);
	tmf->common.length = cpu_to_be16(sizeof(*tmf));
	tmf->scsi_id = cpu_to_be64(port_id);
	tmf->cancel_key = cpu_to_be32(IBMVFC_PASSTHRU_CANCEL_KEY);
	tmf->my_cancel_key = cpu_to_be32(IBMVFC_INTERNAL_CANCEL_KEY);
	rc = ibmvfc_send_event(evt, vhost, default_timeout);

	if (rc != 0) {
		vhost->aborting_passthru = 0;
		dev_err(vhost->dev, "Failed to send cancel event. rc=%d\n", rc);
		rc = -EIO;
	} else
		dev_info(vhost->dev, "Cancelling passthru command to port id 0x%lx\n",
			 port_id);

	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	LEAVE;
	return rc;
}

/**
 * ibmvfc_bsg_plogi - PLOGI into a target to handle a BSG command
 * @vhost:		struct ibmvfc_host to send command
 * @port_id:	port ID to send command
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_bsg_plogi(struct ibmvfc_host *vhost, unsigned int port_id)
{
	struct ibmvfc_port_login *plogi;
	struct ibmvfc_target *tgt;
	struct ibmvfc_event *evt;
	union ibmvfc_iu rsp_iu;
	unsigned long flags;
	int rc = 0, issue_login = 1;

	ENTER;
	spin_lock_irqsave(vhost->host->host_lock, flags);
	list_for_each_entry(tgt, &vhost->targets, queue) {
		if (tgt->scsi_id == port_id) {
			issue_login = 0;
			break;
		}
	}

	if (!issue_login)
		goto unlock_out;
	if (unlikely((rc = ibmvfc_host_chkready(vhost))))
		goto unlock_out;

	evt = ibmvfc_get_event(&vhost->crq);
	ibmvfc_init_event(evt, ibmvfc_sync_completion, IBMVFC_MAD_FORMAT);
	plogi = &evt->iu.plogi;
	memset(plogi, 0, sizeof(*plogi));
	plogi->common.version = cpu_to_be32(1);
	plogi->common.opcode = cpu_to_be32(IBMVFC_PORT_LOGIN);
	plogi->common.length = cpu_to_be16(sizeof(*plogi));
	plogi->scsi_id = cpu_to_be64(port_id);
	evt->sync_iu = &rsp_iu;
	init_completion(&evt->comp);

	rc = ibmvfc_send_event(evt, vhost, default_timeout);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	if (rc)
		return -EIO;

	wait_for_completion(&evt->comp);

	if (rsp_iu.plogi.common.status)
		rc = -EIO;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	ibmvfc_free_event(evt);
unlock_out:
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	LEAVE;
	return rc;
}

/**
 * ibmvfc_bsg_request - Handle a BSG request
 * @job:	struct bsg_job to be executed
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_bsg_request(struct bsg_job *job)
{
	struct ibmvfc_host *vhost = shost_priv(fc_bsg_to_shost(job));
	struct fc_rport *rport = fc_bsg_to_rport(job);
	struct ibmvfc_passthru_mad *mad;
	struct ibmvfc_event *evt;
	union ibmvfc_iu rsp_iu;
	unsigned long flags, port_id = -1;
	struct fc_bsg_request *bsg_request = job->request;
	struct fc_bsg_reply *bsg_reply = job->reply;
	unsigned int code = bsg_request->msgcode;
	int rc = 0, req_seg, rsp_seg, issue_login = 0;
	u32 fc_flags, rsp_len;

	ENTER;
	bsg_reply->reply_payload_rcv_len = 0;
	if (rport)
		port_id = rport->port_id;

	switch (code) {
	case FC_BSG_HST_ELS_NOLOGIN:
		port_id = (bsg_request->rqst_data.h_els.port_id[0] << 16) |
			(bsg_request->rqst_data.h_els.port_id[1] << 8) |
			bsg_request->rqst_data.h_els.port_id[2];
		fallthrough;
	case FC_BSG_RPT_ELS:
		fc_flags = IBMVFC_FC_ELS;
		break;
	case FC_BSG_HST_CT:
		issue_login = 1;
		port_id = (bsg_request->rqst_data.h_ct.port_id[0] << 16) |
			(bsg_request->rqst_data.h_ct.port_id[1] << 8) |
			bsg_request->rqst_data.h_ct.port_id[2];
		fallthrough;
	case FC_BSG_RPT_CT:
		fc_flags = IBMVFC_FC_CT_IU;
		break;
	default:
		return -ENOTSUPP;
	}

	if (port_id == -1)
		return -EINVAL;
	if (!mutex_trylock(&vhost->passthru_mutex))
		return -EBUSY;

	job->dd_data = (void *)port_id;
	req_seg = dma_map_sg(vhost->dev, job->request_payload.sg_list,
			     job->request_payload.sg_cnt, DMA_TO_DEVICE);

	if (!req_seg) {
		mutex_unlock(&vhost->passthru_mutex);
		return -ENOMEM;
	}

	rsp_seg = dma_map_sg(vhost->dev, job->reply_payload.sg_list,
			     job->reply_payload.sg_cnt, DMA_FROM_DEVICE);

	if (!rsp_seg) {
		dma_unmap_sg(vhost->dev, job->request_payload.sg_list,
			     job->request_payload.sg_cnt, DMA_TO_DEVICE);
		mutex_unlock(&vhost->passthru_mutex);
		return -ENOMEM;
	}

	if (req_seg > 1 || rsp_seg > 1) {
		rc = -EINVAL;
		goto out;
	}

	if (issue_login)
		rc = ibmvfc_bsg_plogi(vhost, port_id);

	spin_lock_irqsave(vhost->host->host_lock, flags);

	if (unlikely(rc || (rport && (rc = fc_remote_port_chkready(rport)))) ||
	    unlikely((rc = ibmvfc_host_chkready(vhost)))) {
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		goto out;
	}

	evt = ibmvfc_get_event(&vhost->crq);
	ibmvfc_init_event(evt, ibmvfc_sync_completion, IBMVFC_MAD_FORMAT);
	mad = &evt->iu.passthru;

	memset(mad, 0, sizeof(*mad));
	mad->common.version = cpu_to_be32(1);
	mad->common.opcode = cpu_to_be32(IBMVFC_PASSTHRU);
	mad->common.length = cpu_to_be16(sizeof(*mad) - sizeof(mad->fc_iu) - sizeof(mad->iu));

	mad->cmd_ioba.va = cpu_to_be64(be64_to_cpu(evt->crq.ioba) +
		offsetof(struct ibmvfc_passthru_mad, iu));
	mad->cmd_ioba.len = cpu_to_be32(sizeof(mad->iu));

	mad->iu.cmd_len = cpu_to_be32(job->request_payload.payload_len);
	mad->iu.rsp_len = cpu_to_be32(job->reply_payload.payload_len);
	mad->iu.flags = cpu_to_be32(fc_flags);
	mad->iu.cancel_key = cpu_to_be32(IBMVFC_PASSTHRU_CANCEL_KEY);

	mad->iu.cmd.va = cpu_to_be64(sg_dma_address(job->request_payload.sg_list));
	mad->iu.cmd.len = cpu_to_be32(sg_dma_len(job->request_payload.sg_list));
	mad->iu.rsp.va = cpu_to_be64(sg_dma_address(job->reply_payload.sg_list));
	mad->iu.rsp.len = cpu_to_be32(sg_dma_len(job->reply_payload.sg_list));
	mad->iu.scsi_id = cpu_to_be64(port_id);
	mad->iu.tag = cpu_to_be64((u64)evt);
	rsp_len = be32_to_cpu(mad->iu.rsp.len);

	evt->sync_iu = &rsp_iu;
	init_completion(&evt->comp);
	rc = ibmvfc_send_event(evt, vhost, 0);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	if (rc) {
		rc = -EIO;
		goto out;
	}

	wait_for_completion(&evt->comp);

	if (rsp_iu.passthru.common.status)
		rc = -EIO;
	else
		bsg_reply->reply_payload_rcv_len = rsp_len;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	ibmvfc_free_event(evt);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	bsg_reply->result = rc;
	bsg_job_done(job, bsg_reply->result,
		       bsg_reply->reply_payload_rcv_len);
	rc = 0;
out:
	dma_unmap_sg(vhost->dev, job->request_payload.sg_list,
		     job->request_payload.sg_cnt, DMA_TO_DEVICE);
	dma_unmap_sg(vhost->dev, job->reply_payload.sg_list,
		     job->reply_payload.sg_cnt, DMA_FROM_DEVICE);
	mutex_unlock(&vhost->passthru_mutex);
	LEAVE;
	return rc;
}

/**
 * ibmvfc_reset_device - Reset the device with the specified reset type
 * @sdev:	scsi device to reset
 * @type:	reset type
 * @desc:	reset type description for log messages
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_reset_device(struct scsi_device *sdev, int type, char *desc)
{
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	struct ibmvfc_cmd *tmf;
	struct ibmvfc_event *evt = NULL;
	union ibmvfc_iu rsp_iu;
	struct ibmvfc_fcp_cmd_iu *iu;
	struct ibmvfc_fcp_rsp *fc_rsp = ibmvfc_get_fcp_rsp(vhost, &rsp_iu.cmd);
	int rsp_rc = -EBUSY;
	unsigned long flags;
	int rsp_code = 0;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	if (vhost->state == IBMVFC_ACTIVE) {
		if (vhost->using_channels)
			evt = ibmvfc_get_event(&vhost->scsi_scrqs.scrqs[0]);
		else
			evt = ibmvfc_get_event(&vhost->crq);

		ibmvfc_init_event(evt, ibmvfc_sync_completion, IBMVFC_CMD_FORMAT);
		tmf = ibmvfc_init_vfc_cmd(evt, sdev);
		iu = ibmvfc_get_fcp_iu(vhost, tmf);

		tmf->flags = cpu_to_be16((IBMVFC_NO_MEM_DESC | IBMVFC_TMF));
		if (ibmvfc_check_caps(vhost, IBMVFC_HANDLE_VF_WWPN))
			tmf->target_wwpn = cpu_to_be64(rport->port_name);
		iu->tmf_flags = type;
		evt->sync_iu = &rsp_iu;

		init_completion(&evt->comp);
		rsp_rc = ibmvfc_send_event(evt, vhost, default_timeout);
	}
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	if (rsp_rc != 0) {
		sdev_printk(KERN_ERR, sdev, "Failed to send %s reset event. rc=%d\n",
			    desc, rsp_rc);
		return -EIO;
	}

	sdev_printk(KERN_INFO, sdev, "Resetting %s\n", desc);
	wait_for_completion(&evt->comp);

	if (rsp_iu.cmd.status)
		rsp_code = ibmvfc_get_err_result(vhost, &rsp_iu.cmd);

	if (rsp_code) {
		if (fc_rsp->flags & FCP_RSP_LEN_VALID)
			rsp_code = fc_rsp->data.info.rsp_code;

		sdev_printk(KERN_ERR, sdev, "%s reset failed: %s (%x:%x) "
			    "flags: %x fcp_rsp: %x, scsi_status: %x\n", desc,
			    ibmvfc_get_cmd_error(be16_to_cpu(rsp_iu.cmd.status), be16_to_cpu(rsp_iu.cmd.error)),
			    be16_to_cpu(rsp_iu.cmd.status), be16_to_cpu(rsp_iu.cmd.error), fc_rsp->flags, rsp_code,
			    fc_rsp->scsi_status);
		rsp_rc = -EIO;
	} else
		sdev_printk(KERN_INFO, sdev, "%s reset successful\n", desc);

	spin_lock_irqsave(vhost->host->host_lock, flags);
	ibmvfc_free_event(evt);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	return rsp_rc;
}

/**
 * ibmvfc_match_rport - Match function for specified remote port
 * @evt:	ibmvfc event struct
 * @rport:	device to match
 *
 * Returns:
 *	1 if event matches rport / 0 if event does not match rport
 **/
static int ibmvfc_match_rport(struct ibmvfc_event *evt, void *rport)
{
	struct fc_rport *cmd_rport;

	if (evt->cmnd) {
		cmd_rport = starget_to_rport(scsi_target(evt->cmnd->device));
		if (cmd_rport == rport)
			return 1;
	}
	return 0;
}

/**
 * ibmvfc_match_target - Match function for specified target
 * @evt:	ibmvfc event struct
 * @device:	device to match (starget)
 *
 * Returns:
 *	1 if event matches starget / 0 if event does not match starget
 **/
static int ibmvfc_match_target(struct ibmvfc_event *evt, void *device)
{
	if (evt->cmnd && scsi_target(evt->cmnd->device) == device)
		return 1;
	return 0;
}

/**
 * ibmvfc_match_lun - Match function for specified LUN
 * @evt:	ibmvfc event struct
 * @device:	device to match (sdev)
 *
 * Returns:
 *	1 if event matches sdev / 0 if event does not match sdev
 **/
static int ibmvfc_match_lun(struct ibmvfc_event *evt, void *device)
{
	if (evt->cmnd && evt->cmnd->device == device)
		return 1;
	return 0;
}

/**
 * ibmvfc_event_is_free - Check if event is free or not
 * @evt:	ibmvfc event struct
 *
 * Returns:
 *	true / false
 **/
static bool ibmvfc_event_is_free(struct ibmvfc_event *evt)
{
	struct ibmvfc_event *loop_evt;

	list_for_each_entry(loop_evt, &evt->queue->free, queue_list)
		if (loop_evt == evt)
			return true;

	return false;
}

/**
 * ibmvfc_wait_for_ops - Wait for ops to complete
 * @vhost:	ibmvfc host struct
 * @device:	device to match (starget or sdev)
 * @match:	match function
 *
 * Returns:
 *	SUCCESS / FAILED
 **/
static int ibmvfc_wait_for_ops(struct ibmvfc_host *vhost, void *device,
			       int (*match) (struct ibmvfc_event *, void *))
{
	struct ibmvfc_event *evt;
	DECLARE_COMPLETION_ONSTACK(comp);
	int wait, i, q_index, q_size;
	unsigned long flags;
	signed long timeout = IBMVFC_ABORT_WAIT_TIMEOUT * HZ;
	struct ibmvfc_queue *queues;

	ENTER;
	if (vhost->mq_enabled && vhost->using_channels) {
		queues = vhost->scsi_scrqs.scrqs;
		q_size = vhost->scsi_scrqs.active_queues;
	} else {
		queues = &vhost->crq;
		q_size = 1;
	}

	do {
		wait = 0;
		spin_lock_irqsave(vhost->host->host_lock, flags);
		for (q_index = 0; q_index < q_size; q_index++) {
			spin_lock(&queues[q_index].l_lock);
			for (i = 0; i < queues[q_index].evt_pool.size; i++) {
				evt = &queues[q_index].evt_pool.events[i];
				if (!ibmvfc_event_is_free(evt)) {
					if (match(evt, device)) {
						evt->eh_comp = &comp;
						wait++;
					}
				}
			}
			spin_unlock(&queues[q_index].l_lock);
		}
		spin_unlock_irqrestore(vhost->host->host_lock, flags);

		if (wait) {
			timeout = wait_for_completion_timeout(&comp, timeout);

			if (!timeout) {
				wait = 0;
				spin_lock_irqsave(vhost->host->host_lock, flags);
				for (q_index = 0; q_index < q_size; q_index++) {
					spin_lock(&queues[q_index].l_lock);
					for (i = 0; i < queues[q_index].evt_pool.size; i++) {
						evt = &queues[q_index].evt_pool.events[i];
						if (!ibmvfc_event_is_free(evt)) {
							if (match(evt, device)) {
								evt->eh_comp = NULL;
								wait++;
							}
						}
					}
					spin_unlock(&queues[q_index].l_lock);
				}
				spin_unlock_irqrestore(vhost->host->host_lock, flags);
				if (wait)
					dev_err(vhost->dev, "Timed out waiting for aborted commands\n");
				LEAVE;
				return wait ? FAILED : SUCCESS;
			}
		}
	} while (wait);

	LEAVE;
	return SUCCESS;
}

static struct ibmvfc_event *ibmvfc_init_tmf(struct ibmvfc_queue *queue,
					    struct scsi_device *sdev,
					    int type)
{
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	struct scsi_target *starget = scsi_target(sdev);
	struct fc_rport *rport = starget_to_rport(starget);
	struct ibmvfc_event *evt;
	struct ibmvfc_tmf *tmf;

	evt = ibmvfc_get_event(queue);
	ibmvfc_init_event(evt, ibmvfc_sync_completion, IBMVFC_MAD_FORMAT);

	tmf = &evt->iu.tmf;
	memset(tmf, 0, sizeof(*tmf));
	if (ibmvfc_check_caps(vhost, IBMVFC_HANDLE_VF_WWPN)) {
		tmf->common.version = cpu_to_be32(2);
		tmf->target_wwpn = cpu_to_be64(rport->port_name);
	} else {
		tmf->common.version = cpu_to_be32(1);
	}
	tmf->common.opcode = cpu_to_be32(IBMVFC_TMF_MAD);
	tmf->common.length = cpu_to_be16(sizeof(*tmf));
	tmf->scsi_id = cpu_to_be64(rport->port_id);
	int_to_scsilun(sdev->lun, &tmf->lun);
	if (!ibmvfc_check_caps(vhost, IBMVFC_CAN_SUPPRESS_ABTS))
		type &= ~IBMVFC_TMF_SUPPRESS_ABTS;
	if (vhost->state == IBMVFC_ACTIVE)
		tmf->flags = cpu_to_be32((type | IBMVFC_TMF_LUA_VALID));
	else
		tmf->flags = cpu_to_be32(((type & IBMVFC_TMF_SUPPRESS_ABTS) | IBMVFC_TMF_LUA_VALID));
	tmf->cancel_key = cpu_to_be32((unsigned long)sdev->hostdata);
	tmf->my_cancel_key = cpu_to_be32((unsigned long)starget->hostdata);

	init_completion(&evt->comp);

	return evt;
}

static int ibmvfc_cancel_all_mq(struct scsi_device *sdev, int type)
{
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	struct ibmvfc_event *evt, *found_evt, *temp;
	struct ibmvfc_queue *queues = vhost->scsi_scrqs.scrqs;
	unsigned long flags;
	int num_hwq, i;
	int fail = 0;
	LIST_HEAD(cancelq);
	u16 status;

	ENTER;
	spin_lock_irqsave(vhost->host->host_lock, flags);
	num_hwq = vhost->scsi_scrqs.active_queues;
	for (i = 0; i < num_hwq; i++) {
		spin_lock(queues[i].q_lock);
		spin_lock(&queues[i].l_lock);
		found_evt = NULL;
		list_for_each_entry(evt, &queues[i].sent, queue_list) {
			if (evt->cmnd && evt->cmnd->device == sdev) {
				found_evt = evt;
				break;
			}
		}
		spin_unlock(&queues[i].l_lock);

		if (found_evt && vhost->logged_in) {
			evt = ibmvfc_init_tmf(&queues[i], sdev, type);
			evt->sync_iu = &queues[i].cancel_rsp;
			ibmvfc_send_event(evt, vhost, default_timeout);
			list_add_tail(&evt->cancel, &cancelq);
		}

		spin_unlock(queues[i].q_lock);
	}
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	if (list_empty(&cancelq)) {
		if (vhost->log_level > IBMVFC_DEFAULT_LOG_LEVEL)
			sdev_printk(KERN_INFO, sdev, "No events found to cancel\n");
		return 0;
	}

	sdev_printk(KERN_INFO, sdev, "Cancelling outstanding commands.\n");

	list_for_each_entry_safe(evt, temp, &cancelq, cancel) {
		wait_for_completion(&evt->comp);
		status = be16_to_cpu(evt->queue->cancel_rsp.mad_common.status);
		list_del(&evt->cancel);
		ibmvfc_free_event(evt);

		if (status != IBMVFC_MAD_SUCCESS) {
			sdev_printk(KERN_WARNING, sdev, "Cancel failed with rc=%x\n", status);
			switch (status) {
			case IBMVFC_MAD_DRIVER_FAILED:
			case IBMVFC_MAD_CRQ_ERROR:
			/* Host adapter most likely going through reset, return success to
			 * the caller will wait for the command being cancelled to get returned
			 */
				break;
			default:
				fail = 1;
				break;
			}
		}
	}

	if (fail)
		return -EIO;

	sdev_printk(KERN_INFO, sdev, "Successfully cancelled outstanding commands\n");
	LEAVE;
	return 0;
}

static int ibmvfc_cancel_all_sq(struct scsi_device *sdev, int type)
{
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	struct ibmvfc_event *evt, *found_evt;
	union ibmvfc_iu rsp;
	int rsp_rc = -EBUSY;
	unsigned long flags;
	u16 status;

	ENTER;
	found_evt = NULL;
	spin_lock_irqsave(vhost->host->host_lock, flags);
	spin_lock(&vhost->crq.l_lock);
	list_for_each_entry(evt, &vhost->crq.sent, queue_list) {
		if (evt->cmnd && evt->cmnd->device == sdev) {
			found_evt = evt;
			break;
		}
	}
	spin_unlock(&vhost->crq.l_lock);

	if (!found_evt) {
		if (vhost->log_level > IBMVFC_DEFAULT_LOG_LEVEL)
			sdev_printk(KERN_INFO, sdev, "No events found to cancel\n");
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		return 0;
	}

	if (vhost->logged_in) {
		evt = ibmvfc_init_tmf(&vhost->crq, sdev, type);
		evt->sync_iu = &rsp;
		rsp_rc = ibmvfc_send_event(evt, vhost, default_timeout);
	}

	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	if (rsp_rc != 0) {
		sdev_printk(KERN_ERR, sdev, "Failed to send cancel event. rc=%d\n", rsp_rc);
		/* If failure is received, the host adapter is most likely going
		 through reset, return success so the caller will wait for the command
		 being cancelled to get returned */
		return 0;
	}

	sdev_printk(KERN_INFO, sdev, "Cancelling outstanding commands.\n");

	wait_for_completion(&evt->comp);
	status = be16_to_cpu(rsp.mad_common.status);
	spin_lock_irqsave(vhost->host->host_lock, flags);
	ibmvfc_free_event(evt);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	if (status != IBMVFC_MAD_SUCCESS) {
		sdev_printk(KERN_WARNING, sdev, "Cancel failed with rc=%x\n", status);
		switch (status) {
		case IBMVFC_MAD_DRIVER_FAILED:
		case IBMVFC_MAD_CRQ_ERROR:
			/* Host adapter most likely going through reset, return success to
			 the caller will wait for the command being cancelled to get returned */
			return 0;
		default:
			return -EIO;
		};
	}

	sdev_printk(KERN_INFO, sdev, "Successfully cancelled outstanding commands\n");
	return 0;
}

/**
 * ibmvfc_cancel_all - Cancel all outstanding commands to the device
 * @sdev:	scsi device to cancel commands
 * @type:	type of error recovery being performed
 *
 * This sends a cancel to the VIOS for the specified device. This does
 * NOT send any abort to the actual device. That must be done separately.
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_cancel_all(struct scsi_device *sdev, int type)
{
	struct ibmvfc_host *vhost = shost_priv(sdev->host);

	if (vhost->mq_enabled && vhost->using_channels)
		return ibmvfc_cancel_all_mq(sdev, type);
	else
		return ibmvfc_cancel_all_sq(sdev, type);
}

/**
 * ibmvfc_match_key - Match function for specified cancel key
 * @evt:	ibmvfc event struct
 * @key:	cancel key to match
 *
 * Returns:
 *	1 if event matches key / 0 if event does not match key
 **/
static int ibmvfc_match_key(struct ibmvfc_event *evt, void *key)
{
	unsigned long cancel_key = (unsigned long)key;

	if (evt->crq.format == IBMVFC_CMD_FORMAT &&
	    be32_to_cpu(evt->iu.cmd.cancel_key) == cancel_key)
		return 1;
	return 0;
}

/**
 * ibmvfc_match_evt - Match function for specified event
 * @evt:	ibmvfc event struct
 * @match:	event to match
 *
 * Returns:
 *	1 if event matches key / 0 if event does not match key
 **/
static int ibmvfc_match_evt(struct ibmvfc_event *evt, void *match)
{
	if (evt == match)
		return 1;
	return 0;
}

/**
 * ibmvfc_abort_task_set - Abort outstanding commands to the device
 * @sdev:	scsi device to abort commands
 *
 * This sends an Abort Task Set to the VIOS for the specified device. This does
 * NOT send any cancel to the VIOS. That must be done separately.
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_abort_task_set(struct scsi_device *sdev)
{
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	struct ibmvfc_cmd *tmf;
	struct ibmvfc_event *evt, *found_evt;
	union ibmvfc_iu rsp_iu;
	struct ibmvfc_fcp_cmd_iu *iu;
	struct ibmvfc_fcp_rsp *fc_rsp = ibmvfc_get_fcp_rsp(vhost, &rsp_iu.cmd);
	int rc, rsp_rc = -EBUSY;
	unsigned long flags, timeout = IBMVFC_ABORT_TIMEOUT;
	int rsp_code = 0;

	found_evt = NULL;
	spin_lock_irqsave(vhost->host->host_lock, flags);
	spin_lock(&vhost->crq.l_lock);
	list_for_each_entry(evt, &vhost->crq.sent, queue_list) {
		if (evt->cmnd && evt->cmnd->device == sdev) {
			found_evt = evt;
			break;
		}
	}
	spin_unlock(&vhost->crq.l_lock);

	if (!found_evt) {
		if (vhost->log_level > IBMVFC_DEFAULT_LOG_LEVEL)
			sdev_printk(KERN_INFO, sdev, "No events found to abort\n");
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		return 0;
	}

	if (vhost->state == IBMVFC_ACTIVE) {
		evt = ibmvfc_get_event(&vhost->crq);
		ibmvfc_init_event(evt, ibmvfc_sync_completion, IBMVFC_CMD_FORMAT);
		tmf = ibmvfc_init_vfc_cmd(evt, sdev);
		iu = ibmvfc_get_fcp_iu(vhost, tmf);

		if (ibmvfc_check_caps(vhost, IBMVFC_HANDLE_VF_WWPN))
			tmf->target_wwpn = cpu_to_be64(rport->port_name);
		iu->tmf_flags = IBMVFC_ABORT_TASK_SET;
		tmf->flags = cpu_to_be16((IBMVFC_NO_MEM_DESC | IBMVFC_TMF));
		evt->sync_iu = &rsp_iu;

		tmf->correlation = cpu_to_be64((u64)evt);

		init_completion(&evt->comp);
		rsp_rc = ibmvfc_send_event(evt, vhost, default_timeout);
	}

	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	if (rsp_rc != 0) {
		sdev_printk(KERN_ERR, sdev, "Failed to send abort. rc=%d\n", rsp_rc);
		return -EIO;
	}

	sdev_printk(KERN_INFO, sdev, "Aborting outstanding commands\n");
	timeout = wait_for_completion_timeout(&evt->comp, timeout);

	if (!timeout) {
		rc = ibmvfc_cancel_all(sdev, 0);
		if (!rc) {
			rc = ibmvfc_wait_for_ops(vhost, sdev->hostdata, ibmvfc_match_key);
			if (rc == SUCCESS)
				rc = 0;
		}

		if (rc) {
			sdev_printk(KERN_INFO, sdev, "Cancel failed, resetting host\n");
			ibmvfc_reset_host(vhost);
			rsp_rc = -EIO;
			rc = ibmvfc_wait_for_ops(vhost, sdev->hostdata, ibmvfc_match_key);

			if (rc == SUCCESS)
				rsp_rc = 0;

			rc = ibmvfc_wait_for_ops(vhost, evt, ibmvfc_match_evt);
			if (rc != SUCCESS) {
				spin_lock_irqsave(vhost->host->host_lock, flags);
				ibmvfc_hard_reset_host(vhost);
				spin_unlock_irqrestore(vhost->host->host_lock, flags);
				rsp_rc = 0;
			}

			goto out;
		}
	}

	if (rsp_iu.cmd.status)
		rsp_code = ibmvfc_get_err_result(vhost, &rsp_iu.cmd);

	if (rsp_code) {
		if (fc_rsp->flags & FCP_RSP_LEN_VALID)
			rsp_code = fc_rsp->data.info.rsp_code;

		sdev_printk(KERN_ERR, sdev, "Abort failed: %s (%x:%x) "
			    "flags: %x fcp_rsp: %x, scsi_status: %x\n",
			    ibmvfc_get_cmd_error(be16_to_cpu(rsp_iu.cmd.status), be16_to_cpu(rsp_iu.cmd.error)),
			    be16_to_cpu(rsp_iu.cmd.status), be16_to_cpu(rsp_iu.cmd.error), fc_rsp->flags, rsp_code,
			    fc_rsp->scsi_status);
		rsp_rc = -EIO;
	} else
		sdev_printk(KERN_INFO, sdev, "Abort successful\n");

out:
	spin_lock_irqsave(vhost->host->host_lock, flags);
	ibmvfc_free_event(evt);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	return rsp_rc;
}

/**
 * ibmvfc_eh_abort_handler - Abort a command
 * @cmd:	scsi command to abort
 *
 * Returns:
 *	SUCCESS / FAST_IO_FAIL / FAILED
 **/
static int ibmvfc_eh_abort_handler(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	int cancel_rc, block_rc;
	int rc = FAILED;

	ENTER;
	block_rc = fc_block_scsi_eh(cmd);
	ibmvfc_wait_while_resetting(vhost);
	if (block_rc != FAST_IO_FAIL) {
		cancel_rc = ibmvfc_cancel_all(sdev, IBMVFC_TMF_ABORT_TASK_SET);
		ibmvfc_abort_task_set(sdev);
	} else
		cancel_rc = ibmvfc_cancel_all(sdev, IBMVFC_TMF_SUPPRESS_ABTS);

	if (!cancel_rc)
		rc = ibmvfc_wait_for_ops(vhost, sdev, ibmvfc_match_lun);

	if (block_rc == FAST_IO_FAIL && rc != FAILED)
		rc = FAST_IO_FAIL;

	LEAVE;
	return rc;
}

/**
 * ibmvfc_eh_device_reset_handler - Reset a single LUN
 * @cmd:	scsi command struct
 *
 * Returns:
 *	SUCCESS / FAST_IO_FAIL / FAILED
 **/
static int ibmvfc_eh_device_reset_handler(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	int cancel_rc, block_rc, reset_rc = 0;
	int rc = FAILED;

	ENTER;
	block_rc = fc_block_scsi_eh(cmd);
	ibmvfc_wait_while_resetting(vhost);
	if (block_rc != FAST_IO_FAIL) {
		cancel_rc = ibmvfc_cancel_all(sdev, IBMVFC_TMF_LUN_RESET);
		reset_rc = ibmvfc_reset_device(sdev, IBMVFC_LUN_RESET, "LUN");
	} else
		cancel_rc = ibmvfc_cancel_all(sdev, IBMVFC_TMF_SUPPRESS_ABTS);

	if (!cancel_rc && !reset_rc)
		rc = ibmvfc_wait_for_ops(vhost, sdev, ibmvfc_match_lun);

	if (block_rc == FAST_IO_FAIL && rc != FAILED)
		rc = FAST_IO_FAIL;

	LEAVE;
	return rc;
}

/**
 * ibmvfc_dev_cancel_all_noreset - Device iterated cancel all function
 * @sdev:	scsi device struct
 * @data:	return code
 *
 **/
static void ibmvfc_dev_cancel_all_noreset(struct scsi_device *sdev, void *data)
{
	unsigned long *rc = data;
	*rc |= ibmvfc_cancel_all(sdev, IBMVFC_TMF_SUPPRESS_ABTS);
}

/**
 * ibmvfc_dev_cancel_all_reset - Device iterated cancel all function
 * @sdev:	scsi device struct
 * @data:	return code
 *
 **/
static void ibmvfc_dev_cancel_all_reset(struct scsi_device *sdev, void *data)
{
	unsigned long *rc = data;
	*rc |= ibmvfc_cancel_all(sdev, IBMVFC_TMF_TGT_RESET);
}

/**
 * ibmvfc_eh_target_reset_handler - Reset the target
 * @cmd:	scsi command struct
 *
 * Returns:
 *	SUCCESS / FAST_IO_FAIL / FAILED
 **/
static int ibmvfc_eh_target_reset_handler(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	struct scsi_target *starget = scsi_target(sdev);
	int block_rc;
	int reset_rc = 0;
	int rc = FAILED;
	unsigned long cancel_rc = 0;

	ENTER;
	block_rc = fc_block_scsi_eh(cmd);
	ibmvfc_wait_while_resetting(vhost);
	if (block_rc != FAST_IO_FAIL) {
		starget_for_each_device(starget, &cancel_rc, ibmvfc_dev_cancel_all_reset);
		reset_rc = ibmvfc_reset_device(sdev, IBMVFC_TARGET_RESET, "target");
	} else
		starget_for_each_device(starget, &cancel_rc, ibmvfc_dev_cancel_all_noreset);

	if (!cancel_rc && !reset_rc)
		rc = ibmvfc_wait_for_ops(vhost, starget, ibmvfc_match_target);

	if (block_rc == FAST_IO_FAIL && rc != FAILED)
		rc = FAST_IO_FAIL;

	LEAVE;
	return rc;
}

/**
 * ibmvfc_eh_host_reset_handler - Reset the connection to the server
 * @cmd:	struct scsi_cmnd having problems
 *
 **/
static int ibmvfc_eh_host_reset_handler(struct scsi_cmnd *cmd)
{
	int rc;
	struct ibmvfc_host *vhost = shost_priv(cmd->device->host);

	dev_err(vhost->dev, "Resetting connection due to error recovery\n");
	rc = ibmvfc_issue_fc_host_lip(vhost->host);

	return rc ? FAILED : SUCCESS;
}

/**
 * ibmvfc_terminate_rport_io - Terminate all pending I/O to the rport.
 * @rport:		rport struct
 *
 * Return value:
 * 	none
 **/
static void ibmvfc_terminate_rport_io(struct fc_rport *rport)
{
	struct Scsi_Host *shost = rport_to_shost(rport);
	struct ibmvfc_host *vhost = shost_priv(shost);
	struct fc_rport *dev_rport;
	struct scsi_device *sdev;
	struct ibmvfc_target *tgt;
	unsigned long rc, flags;
	unsigned int found;

	ENTER;
	shost_for_each_device(sdev, shost) {
		dev_rport = starget_to_rport(scsi_target(sdev));
		if (dev_rport != rport)
			continue;
		ibmvfc_cancel_all(sdev, IBMVFC_TMF_SUPPRESS_ABTS);
	}

	rc = ibmvfc_wait_for_ops(vhost, rport, ibmvfc_match_rport);

	if (rc == FAILED)
		ibmvfc_issue_fc_host_lip(shost);

	spin_lock_irqsave(shost->host_lock, flags);
	found = 0;
	list_for_each_entry(tgt, &vhost->targets, queue) {
		if (tgt->scsi_id == rport->port_id) {
			found++;
			break;
		}
	}

	if (found && tgt->action == IBMVFC_TGT_ACTION_LOGOUT_DELETED_RPORT) {
		/*
		 * If we get here, that means we previously attempted to send
		 * an implicit logout to the target but it failed, most likely
		 * due to I/O being pending, so we need to send it again
		 */
		ibmvfc_del_tgt(tgt);
		ibmvfc_reinit_host(vhost);
	}

	spin_unlock_irqrestore(shost->host_lock, flags);
	LEAVE;
}

static const struct ibmvfc_async_desc ae_desc [] = {
	{ "PLOGI",	IBMVFC_AE_ELS_PLOGI,	IBMVFC_DEFAULT_LOG_LEVEL + 1 },
	{ "LOGO",	IBMVFC_AE_ELS_LOGO,	IBMVFC_DEFAULT_LOG_LEVEL + 1 },
	{ "PRLO",	IBMVFC_AE_ELS_PRLO,	IBMVFC_DEFAULT_LOG_LEVEL + 1 },
	{ "N-Port SCN",	IBMVFC_AE_SCN_NPORT,	IBMVFC_DEFAULT_LOG_LEVEL + 1 },
	{ "Group SCN",	IBMVFC_AE_SCN_GROUP,	IBMVFC_DEFAULT_LOG_LEVEL + 1 },
	{ "Domain SCN",	IBMVFC_AE_SCN_DOMAIN,	IBMVFC_DEFAULT_LOG_LEVEL },
	{ "Fabric SCN",	IBMVFC_AE_SCN_FABRIC,	IBMVFC_DEFAULT_LOG_LEVEL },
	{ "Link Up",	IBMVFC_AE_LINK_UP,	IBMVFC_DEFAULT_LOG_LEVEL },
	{ "Link Down",	IBMVFC_AE_LINK_DOWN,	IBMVFC_DEFAULT_LOG_LEVEL },
	{ "Link Dead",	IBMVFC_AE_LINK_DEAD,	IBMVFC_DEFAULT_LOG_LEVEL },
	{ "Halt",	IBMVFC_AE_HALT,		IBMVFC_DEFAULT_LOG_LEVEL },
	{ "Resume",	IBMVFC_AE_RESUME,	IBMVFC_DEFAULT_LOG_LEVEL },
	{ "Adapter Failed", IBMVFC_AE_ADAPTER_FAILED, IBMVFC_DEFAULT_LOG_LEVEL },
};

static const struct ibmvfc_async_desc unknown_ae = {
	"Unknown async", 0, IBMVFC_DEFAULT_LOG_LEVEL
};

/**
 * ibmvfc_get_ae_desc - Get text description for async event
 * @ae:	async event
 *
 **/
static const struct ibmvfc_async_desc *ibmvfc_get_ae_desc(u64 ae)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ae_desc); i++)
		if (ae_desc[i].ae == ae)
			return &ae_desc[i];

	return &unknown_ae;
}

static const struct {
	enum ibmvfc_ae_link_state state;
	const char *desc;
} link_desc [] = {
	{ IBMVFC_AE_LS_LINK_UP,		" link up" },
	{ IBMVFC_AE_LS_LINK_BOUNCED,	" link bounced" },
	{ IBMVFC_AE_LS_LINK_DOWN,	" link down" },
	{ IBMVFC_AE_LS_LINK_DEAD,	" link dead" },
};

/**
 * ibmvfc_get_link_state - Get text description for link state
 * @state:	link state
 *
 **/
static const char *ibmvfc_get_link_state(enum ibmvfc_ae_link_state state)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(link_desc); i++)
		if (link_desc[i].state == state)
			return link_desc[i].desc;

	return "";
}

/**
 * ibmvfc_handle_async - Handle an async event from the adapter
 * @crq:	crq to process
 * @vhost:	ibmvfc host struct
 *
 **/
static void ibmvfc_handle_async(struct ibmvfc_async_crq *crq,
				struct ibmvfc_host *vhost)
{
	const struct ibmvfc_async_desc *desc = ibmvfc_get_ae_desc(be64_to_cpu(crq->event));
	struct ibmvfc_target *tgt;

	ibmvfc_log(vhost, desc->log_level, "%s event received. scsi_id: %llx, wwpn: %llx,"
		   " node_name: %llx%s\n", desc->desc, be64_to_cpu(crq->scsi_id),
		   be64_to_cpu(crq->wwpn), be64_to_cpu(crq->node_name),
		   ibmvfc_get_link_state(crq->link_state));

	switch (be64_to_cpu(crq->event)) {
	case IBMVFC_AE_RESUME:
		switch (crq->link_state) {
		case IBMVFC_AE_LS_LINK_DOWN:
			ibmvfc_link_down(vhost, IBMVFC_LINK_DOWN);
			break;
		case IBMVFC_AE_LS_LINK_DEAD:
			ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
			break;
		case IBMVFC_AE_LS_LINK_UP:
		case IBMVFC_AE_LS_LINK_BOUNCED:
		default:
			vhost->events_to_log |= IBMVFC_AE_LINKUP;
			vhost->delay_init = 1;
			__ibmvfc_reset_host(vhost);
			break;
		}

		break;
	case IBMVFC_AE_LINK_UP:
		vhost->events_to_log |= IBMVFC_AE_LINKUP;
		vhost->delay_init = 1;
		__ibmvfc_reset_host(vhost);
		break;
	case IBMVFC_AE_SCN_FABRIC:
	case IBMVFC_AE_SCN_DOMAIN:
		vhost->events_to_log |= IBMVFC_AE_RSCN;
		if (vhost->state < IBMVFC_HALTED) {
			vhost->delay_init = 1;
			__ibmvfc_reset_host(vhost);
		}
		break;
	case IBMVFC_AE_SCN_NPORT:
	case IBMVFC_AE_SCN_GROUP:
		vhost->events_to_log |= IBMVFC_AE_RSCN;
		ibmvfc_reinit_host(vhost);
		break;
	case IBMVFC_AE_ELS_LOGO:
	case IBMVFC_AE_ELS_PRLO:
	case IBMVFC_AE_ELS_PLOGI:
		list_for_each_entry(tgt, &vhost->targets, queue) {
			if (!crq->scsi_id && !crq->wwpn && !crq->node_name)
				break;
			if (crq->scsi_id && cpu_to_be64(tgt->scsi_id) != crq->scsi_id)
				continue;
			if (crq->wwpn && cpu_to_be64(tgt->ids.port_name) != crq->wwpn)
				continue;
			if (crq->node_name && cpu_to_be64(tgt->ids.node_name) != crq->node_name)
				continue;
			if (tgt->need_login && be64_to_cpu(crq->event) == IBMVFC_AE_ELS_LOGO)
				tgt->logo_rcvd = 1;
			if (!tgt->need_login || be64_to_cpu(crq->event) == IBMVFC_AE_ELS_PLOGI) {
				ibmvfc_del_tgt(tgt);
				ibmvfc_reinit_host(vhost);
			}
		}
		break;
	case IBMVFC_AE_LINK_DOWN:
	case IBMVFC_AE_ADAPTER_FAILED:
		ibmvfc_link_down(vhost, IBMVFC_LINK_DOWN);
		break;
	case IBMVFC_AE_LINK_DEAD:
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		break;
	case IBMVFC_AE_HALT:
		ibmvfc_link_down(vhost, IBMVFC_HALTED);
		break;
	default:
		dev_err(vhost->dev, "Unknown async event received: %lld\n", crq->event);
		break;
	}
}

/**
 * ibmvfc_handle_crq - Handles and frees received events in the CRQ
 * @crq:	Command/Response queue
 * @vhost:	ibmvfc host struct
 * @evt_doneq:	Event done queue
 *
**/
static void ibmvfc_handle_crq(struct ibmvfc_crq *crq, struct ibmvfc_host *vhost,
			      struct list_head *evt_doneq)
{
	long rc;
	struct ibmvfc_event *evt = (struct ibmvfc_event *)be64_to_cpu(crq->ioba);

	switch (crq->valid) {
	case IBMVFC_CRQ_INIT_RSP:
		switch (crq->format) {
		case IBMVFC_CRQ_INIT:
			dev_info(vhost->dev, "Partner initialized\n");
			/* Send back a response */
			rc = ibmvfc_send_crq_init_complete(vhost);
			if (rc == 0)
				ibmvfc_init_host(vhost);
			else
				dev_err(vhost->dev, "Unable to send init rsp. rc=%ld\n", rc);
			break;
		case IBMVFC_CRQ_INIT_COMPLETE:
			dev_info(vhost->dev, "Partner initialization complete\n");
			ibmvfc_init_host(vhost);
			break;
		default:
			dev_err(vhost->dev, "Unknown crq message type: %d\n", crq->format);
		}
		return;
	case IBMVFC_CRQ_XPORT_EVENT:
		vhost->state = IBMVFC_NO_CRQ;
		vhost->logged_in = 0;
		ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_NONE);
		if (crq->format == IBMVFC_PARTITION_MIGRATED) {
			/* We need to re-setup the interpartition connection */
			dev_info(vhost->dev, "Partition migrated, Re-enabling adapter\n");
			vhost->client_migrated = 1;
			ibmvfc_purge_requests(vhost, DID_REQUEUE);
			ibmvfc_link_down(vhost, IBMVFC_LINK_DOWN);
			ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_REENABLE);
		} else if (crq->format == IBMVFC_PARTNER_FAILED || crq->format == IBMVFC_PARTNER_DEREGISTER) {
			dev_err(vhost->dev, "Host partner adapter deregistered or failed (rc=%d)\n", crq->format);
			ibmvfc_purge_requests(vhost, DID_ERROR);
			ibmvfc_link_down(vhost, IBMVFC_LINK_DOWN);
			ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_RESET);
		} else {
			dev_err(vhost->dev, "Received unknown transport event from partner (rc=%d)\n", crq->format);
		}
		return;
	case IBMVFC_CRQ_CMD_RSP:
		break;
	default:
		dev_err(vhost->dev, "Got an invalid message type 0x%02x\n", crq->valid);
		return;
	}

	if (crq->format == IBMVFC_ASYNC_EVENT)
		return;

	/* The only kind of payload CRQs we should get are responses to
	 * things we send. Make sure this response is to something we
	 * actually sent
	 */
	if (unlikely(!ibmvfc_valid_event(&vhost->crq.evt_pool, evt))) {
		dev_err(vhost->dev, "Returned correlation_token 0x%08llx is invalid!\n",
			crq->ioba);
		return;
	}

	if (unlikely(atomic_dec_if_positive(&evt->active))) {
		dev_err(vhost->dev, "Received duplicate correlation_token 0x%08llx!\n",
			crq->ioba);
		return;
	}

	spin_lock(&evt->queue->l_lock);
	list_move_tail(&evt->queue_list, evt_doneq);
	spin_unlock(&evt->queue->l_lock);
}

/**
 * ibmvfc_scan_finished - Check if the device scan is done.
 * @shost:	scsi host struct
 * @time:	current elapsed time
 *
 * Returns:
 *	0 if scan is not done / 1 if scan is done
 **/
static int ibmvfc_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	unsigned long flags;
	struct ibmvfc_host *vhost = shost_priv(shost);
	int done = 0;

	spin_lock_irqsave(shost->host_lock, flags);
	if (!vhost->scan_timeout)
		done = 1;
	else if (time >= (vhost->scan_timeout * HZ)) {
		dev_info(vhost->dev, "Scan taking longer than %d seconds, "
			 "continuing initialization\n", vhost->scan_timeout);
		done = 1;
	}

	if (vhost->scan_complete) {
		vhost->scan_timeout = init_timeout;
		done = 1;
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
	return done;
}

/**
 * ibmvfc_slave_alloc - Setup the device's task set value
 * @sdev:	struct scsi_device device to configure
 *
 * Set the device's task set value so that error handling works as
 * expected.
 *
 * Returns:
 *	0 on success / -ENXIO if device does not exist
 **/
static int ibmvfc_slave_alloc(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	struct ibmvfc_host *vhost = shost_priv(shost);
	unsigned long flags = 0;

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	spin_lock_irqsave(shost->host_lock, flags);
	sdev->hostdata = (void *)(unsigned long)vhost->task_set++;
	spin_unlock_irqrestore(shost->host_lock, flags);
	return 0;
}

/**
 * ibmvfc_target_alloc - Setup the target's task set value
 * @starget:	struct scsi_target
 *
 * Set the target's task set value so that error handling works as
 * expected.
 *
 * Returns:
 *	0 on success / -ENXIO if device does not exist
 **/
static int ibmvfc_target_alloc(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ibmvfc_host *vhost = shost_priv(shost);
	unsigned long flags = 0;

	spin_lock_irqsave(shost->host_lock, flags);
	starget->hostdata = (void *)(unsigned long)vhost->task_set++;
	spin_unlock_irqrestore(shost->host_lock, flags);
	return 0;
}

/**
 * ibmvfc_slave_configure - Configure the device
 * @sdev:	struct scsi_device device to configure
 *
 * Enable allow_restart for a device if it is a disk. Adjust the
 * queue_depth here also.
 *
 * Returns:
 *	0
 **/
static int ibmvfc_slave_configure(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;
	unsigned long flags = 0;

	spin_lock_irqsave(shost->host_lock, flags);
	if (sdev->type == TYPE_DISK) {
		sdev->allow_restart = 1;
		blk_queue_rq_timeout(sdev->request_queue, 120 * HZ);
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
	return 0;
}

/**
 * ibmvfc_change_queue_depth - Change the device's queue depth
 * @sdev:	scsi device struct
 * @qdepth:	depth to set
 *
 * Return value:
 * 	actual depth set
 **/
static int ibmvfc_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	if (qdepth > IBMVFC_MAX_CMDS_PER_LUN)
		qdepth = IBMVFC_MAX_CMDS_PER_LUN;

	return scsi_change_queue_depth(sdev, qdepth);
}

static ssize_t ibmvfc_show_host_partition_name(struct device *dev,
						 struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			vhost->login_buf->resp.partition_name);
}

static ssize_t ibmvfc_show_host_device_name(struct device *dev,
					    struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			vhost->login_buf->resp.device_name);
}

static ssize_t ibmvfc_show_host_loc_code(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			vhost->login_buf->resp.port_loc_code);
}

static ssize_t ibmvfc_show_host_drc_name(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);

	return snprintf(buf, PAGE_SIZE, "%s\n",
			vhost->login_buf->resp.drc_name);
}

static ssize_t ibmvfc_show_host_npiv_version(struct device *dev,
					     struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);
	return snprintf(buf, PAGE_SIZE, "%d\n", be32_to_cpu(vhost->login_buf->resp.version));
}

static ssize_t ibmvfc_show_host_capabilities(struct device *dev,
					     struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);
	return snprintf(buf, PAGE_SIZE, "%llx\n", be64_to_cpu(vhost->login_buf->resp.capabilities));
}

/**
 * ibmvfc_show_log_level - Show the adapter's error logging level
 * @dev:	class device struct
 * @attr:	unused
 * @buf:	buffer
 *
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ibmvfc_show_log_level(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);
	unsigned long flags = 0;
	int len;

	spin_lock_irqsave(shost->host_lock, flags);
	len = snprintf(buf, PAGE_SIZE, "%d\n", vhost->log_level);
	spin_unlock_irqrestore(shost->host_lock, flags);
	return len;
}

/**
 * ibmvfc_store_log_level - Change the adapter's error logging level
 * @dev:	class device struct
 * @attr:	unused
 * @buf:	buffer
 * @count:      buffer size
 *
 * Return value:
 * 	number of bytes printed to buffer
 **/
static ssize_t ibmvfc_store_log_level(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);
	unsigned long flags = 0;

	spin_lock_irqsave(shost->host_lock, flags);
	vhost->log_level = simple_strtoul(buf, NULL, 10);
	spin_unlock_irqrestore(shost->host_lock, flags);
	return strlen(buf);
}

static ssize_t ibmvfc_show_scsi_channels(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);
	unsigned long flags = 0;
	int len;

	spin_lock_irqsave(shost->host_lock, flags);
	len = snprintf(buf, PAGE_SIZE, "%d\n", vhost->client_scsi_channels);
	spin_unlock_irqrestore(shost->host_lock, flags);
	return len;
}

static ssize_t ibmvfc_store_scsi_channels(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);
	unsigned long flags = 0;
	unsigned int channels;

	spin_lock_irqsave(shost->host_lock, flags);
	channels = simple_strtoul(buf, NULL, 10);
	vhost->client_scsi_channels = min(channels, nr_scsi_hw_queues);
	ibmvfc_hard_reset_host(vhost);
	spin_unlock_irqrestore(shost->host_lock, flags);
	return strlen(buf);
}

static DEVICE_ATTR(partition_name, S_IRUGO, ibmvfc_show_host_partition_name, NULL);
static DEVICE_ATTR(device_name, S_IRUGO, ibmvfc_show_host_device_name, NULL);
static DEVICE_ATTR(port_loc_code, S_IRUGO, ibmvfc_show_host_loc_code, NULL);
static DEVICE_ATTR(drc_name, S_IRUGO, ibmvfc_show_host_drc_name, NULL);
static DEVICE_ATTR(npiv_version, S_IRUGO, ibmvfc_show_host_npiv_version, NULL);
static DEVICE_ATTR(capabilities, S_IRUGO, ibmvfc_show_host_capabilities, NULL);
static DEVICE_ATTR(log_level, S_IRUGO | S_IWUSR,
		   ibmvfc_show_log_level, ibmvfc_store_log_level);
static DEVICE_ATTR(nr_scsi_channels, S_IRUGO | S_IWUSR,
		   ibmvfc_show_scsi_channels, ibmvfc_store_scsi_channels);

#ifdef CONFIG_SCSI_IBMVFC_TRACE
/**
 * ibmvfc_read_trace - Dump the adapter trace
 * @filp:		open sysfs file
 * @kobj:		kobject struct
 * @bin_attr:	bin_attribute struct
 * @buf:		buffer
 * @off:		offset
 * @count:		buffer size
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ibmvfc_read_trace(struct file *filp, struct kobject *kobj,
				 struct bin_attribute *bin_attr,
				 char *buf, loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);
	unsigned long flags = 0;
	int size = IBMVFC_TRACE_SIZE;
	char *src = (char *)vhost->trace;

	if (off > size)
		return 0;
	if (off + count > size) {
		size -= off;
		count = size;
	}

	spin_lock_irqsave(shost->host_lock, flags);
	memcpy(buf, &src[off], count);
	spin_unlock_irqrestore(shost->host_lock, flags);
	return count;
}

static struct bin_attribute ibmvfc_trace_attr = {
	.attr =	{
		.name = "trace",
		.mode = S_IRUGO,
	},
	.size = 0,
	.read = ibmvfc_read_trace,
};
#endif

static struct device_attribute *ibmvfc_attrs[] = {
	&dev_attr_partition_name,
	&dev_attr_device_name,
	&dev_attr_port_loc_code,
	&dev_attr_drc_name,
	&dev_attr_npiv_version,
	&dev_attr_capabilities,
	&dev_attr_log_level,
	&dev_attr_nr_scsi_channels,
	NULL
};

static struct scsi_host_template driver_template = {
	.module = THIS_MODULE,
	.name = "IBM POWER Virtual FC Adapter",
	.proc_name = IBMVFC_NAME,
	.queuecommand = ibmvfc_queuecommand,
	.eh_timed_out = fc_eh_timed_out,
	.eh_abort_handler = ibmvfc_eh_abort_handler,
	.eh_device_reset_handler = ibmvfc_eh_device_reset_handler,
	.eh_target_reset_handler = ibmvfc_eh_target_reset_handler,
	.eh_host_reset_handler = ibmvfc_eh_host_reset_handler,
	.slave_alloc = ibmvfc_slave_alloc,
	.slave_configure = ibmvfc_slave_configure,
	.target_alloc = ibmvfc_target_alloc,
	.scan_finished = ibmvfc_scan_finished,
	.change_queue_depth = ibmvfc_change_queue_depth,
	.cmd_per_lun = 16,
	.can_queue = IBMVFC_MAX_REQUESTS_DEFAULT,
	.this_id = -1,
	.sg_tablesize = SG_ALL,
	.max_sectors = IBMVFC_MAX_SECTORS,
	.shost_attrs = ibmvfc_attrs,
	.track_queue_depth = 1,
	.host_tagset = 1,
};

/**
 * ibmvfc_next_async_crq - Returns the next entry in async queue
 * @vhost:	ibmvfc host struct
 *
 * Returns:
 *	Pointer to next entry in queue / NULL if empty
 **/
static struct ibmvfc_async_crq *ibmvfc_next_async_crq(struct ibmvfc_host *vhost)
{
	struct ibmvfc_queue *async_crq = &vhost->async_crq;
	struct ibmvfc_async_crq *crq;

	crq = &async_crq->msgs.async[async_crq->cur];
	if (crq->valid & 0x80) {
		if (++async_crq->cur == async_crq->size)
			async_crq->cur = 0;
		rmb();
	} else
		crq = NULL;

	return crq;
}

/**
 * ibmvfc_next_crq - Returns the next entry in message queue
 * @vhost:	ibmvfc host struct
 *
 * Returns:
 *	Pointer to next entry in queue / NULL if empty
 **/
static struct ibmvfc_crq *ibmvfc_next_crq(struct ibmvfc_host *vhost)
{
	struct ibmvfc_queue *queue = &vhost->crq;
	struct ibmvfc_crq *crq;

	crq = &queue->msgs.crq[queue->cur];
	if (crq->valid & 0x80) {
		if (++queue->cur == queue->size)
			queue->cur = 0;
		rmb();
	} else
		crq = NULL;

	return crq;
}

/**
 * ibmvfc_interrupt - Interrupt handler
 * @irq:		number of irq to handle, not used
 * @dev_instance: ibmvfc_host that received interrupt
 *
 * Returns:
 *	IRQ_HANDLED
 **/
static irqreturn_t ibmvfc_interrupt(int irq, void *dev_instance)
{
	struct ibmvfc_host *vhost = (struct ibmvfc_host *)dev_instance;
	unsigned long flags;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	vio_disable_interrupts(to_vio_dev(vhost->dev));
	tasklet_schedule(&vhost->tasklet);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	return IRQ_HANDLED;
}

/**
 * ibmvfc_tasklet - Interrupt handler tasklet
 * @data:		ibmvfc host struct
 *
 * Returns:
 *	Nothing
 **/
static void ibmvfc_tasklet(void *data)
{
	struct ibmvfc_host *vhost = data;
	struct vio_dev *vdev = to_vio_dev(vhost->dev);
	struct ibmvfc_crq *crq;
	struct ibmvfc_async_crq *async;
	struct ibmvfc_event *evt, *temp;
	unsigned long flags;
	int done = 0;
	LIST_HEAD(evt_doneq);

	spin_lock_irqsave(vhost->host->host_lock, flags);
	spin_lock(vhost->crq.q_lock);
	while (!done) {
		/* Pull all the valid messages off the async CRQ */
		while ((async = ibmvfc_next_async_crq(vhost)) != NULL) {
			ibmvfc_handle_async(async, vhost);
			async->valid = 0;
			wmb();
		}

		/* Pull all the valid messages off the CRQ */
		while ((crq = ibmvfc_next_crq(vhost)) != NULL) {
			ibmvfc_handle_crq(crq, vhost, &evt_doneq);
			crq->valid = 0;
			wmb();
		}

		vio_enable_interrupts(vdev);
		if ((async = ibmvfc_next_async_crq(vhost)) != NULL) {
			vio_disable_interrupts(vdev);
			ibmvfc_handle_async(async, vhost);
			async->valid = 0;
			wmb();
		} else if ((crq = ibmvfc_next_crq(vhost)) != NULL) {
			vio_disable_interrupts(vdev);
			ibmvfc_handle_crq(crq, vhost, &evt_doneq);
			crq->valid = 0;
			wmb();
		} else
			done = 1;
	}

	spin_unlock(vhost->crq.q_lock);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	list_for_each_entry_safe(evt, temp, &evt_doneq, queue_list) {
		del_timer(&evt->timer);
		list_del(&evt->queue_list);
		ibmvfc_trc_end(evt);
		evt->done(evt);
	}
}

static int ibmvfc_toggle_scrq_irq(struct ibmvfc_queue *scrq, int enable)
{
	struct device *dev = scrq->vhost->dev;
	struct vio_dev *vdev = to_vio_dev(dev);
	unsigned long rc;
	int irq_action = H_ENABLE_VIO_INTERRUPT;

	if (!enable)
		irq_action = H_DISABLE_VIO_INTERRUPT;

	rc = plpar_hcall_norets(H_VIOCTL, vdev->unit_address, irq_action,
				scrq->hw_irq, 0, 0);

	if (rc)
		dev_err(dev, "Couldn't %s sub-crq[%lu] irq. rc=%ld\n",
			enable ? "enable" : "disable", scrq->hwq_id, rc);

	return rc;
}

static void ibmvfc_handle_scrq(struct ibmvfc_crq *crq, struct ibmvfc_host *vhost,
			       struct list_head *evt_doneq)
{
	struct ibmvfc_event *evt = (struct ibmvfc_event *)be64_to_cpu(crq->ioba);

	switch (crq->valid) {
	case IBMVFC_CRQ_CMD_RSP:
		break;
	case IBMVFC_CRQ_XPORT_EVENT:
		return;
	default:
		dev_err(vhost->dev, "Got and invalid message type 0x%02x\n", crq->valid);
		return;
	}

	/* The only kind of payload CRQs we should get are responses to
	 * things we send. Make sure this response is to something we
	 * actually sent
	 */
	if (unlikely(!ibmvfc_valid_event(&evt->queue->evt_pool, evt))) {
		dev_err(vhost->dev, "Returned correlation_token 0x%08llx is invalid!\n",
			crq->ioba);
		return;
	}

	if (unlikely(atomic_dec_if_positive(&evt->active))) {
		dev_err(vhost->dev, "Received duplicate correlation_token 0x%08llx!\n",
			crq->ioba);
		return;
	}

	spin_lock(&evt->queue->l_lock);
	list_move_tail(&evt->queue_list, evt_doneq);
	spin_unlock(&evt->queue->l_lock);
}

static struct ibmvfc_crq *ibmvfc_next_scrq(struct ibmvfc_queue *scrq)
{
	struct ibmvfc_crq *crq;

	crq = &scrq->msgs.scrq[scrq->cur].crq;
	if (crq->valid & 0x80) {
		if (++scrq->cur == scrq->size)
			scrq->cur = 0;
		rmb();
	} else
		crq = NULL;

	return crq;
}

static void ibmvfc_drain_sub_crq(struct ibmvfc_queue *scrq)
{
	struct ibmvfc_crq *crq;
	struct ibmvfc_event *evt, *temp;
	unsigned long flags;
	int done = 0;
	LIST_HEAD(evt_doneq);

	spin_lock_irqsave(scrq->q_lock, flags);
	while (!done) {
		while ((crq = ibmvfc_next_scrq(scrq)) != NULL) {
			ibmvfc_handle_scrq(crq, scrq->vhost, &evt_doneq);
			crq->valid = 0;
			wmb();
		}

		ibmvfc_toggle_scrq_irq(scrq, 1);
		if ((crq = ibmvfc_next_scrq(scrq)) != NULL) {
			ibmvfc_toggle_scrq_irq(scrq, 0);
			ibmvfc_handle_scrq(crq, scrq->vhost, &evt_doneq);
			crq->valid = 0;
			wmb();
		} else
			done = 1;
	}
	spin_unlock_irqrestore(scrq->q_lock, flags);

	list_for_each_entry_safe(evt, temp, &evt_doneq, queue_list) {
		del_timer(&evt->timer);
		list_del(&evt->queue_list);
		ibmvfc_trc_end(evt);
		evt->done(evt);
	}
}

static irqreturn_t ibmvfc_interrupt_scsi(int irq, void *scrq_instance)
{
	struct ibmvfc_queue *scrq = (struct ibmvfc_queue *)scrq_instance;

	ibmvfc_toggle_scrq_irq(scrq, 0);
	ibmvfc_drain_sub_crq(scrq);

	return IRQ_HANDLED;
}

/**
 * ibmvfc_init_tgt - Set the next init job step for the target
 * @tgt:		ibmvfc target struct
 * @job_step:	job step to perform
 *
 **/
static void ibmvfc_init_tgt(struct ibmvfc_target *tgt,
			    void (*job_step) (struct ibmvfc_target *))
{
	if (!ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_INIT))
		tgt->job_step = job_step;
	wake_up(&tgt->vhost->work_wait_q);
}

/**
 * ibmvfc_retry_tgt_init - Attempt to retry a step in target initialization
 * @tgt:		ibmvfc target struct
 * @job_step:	initialization job step
 *
 * Returns: 1 if step will be retried / 0 if not
 *
 **/
static int ibmvfc_retry_tgt_init(struct ibmvfc_target *tgt,
				  void (*job_step) (struct ibmvfc_target *))
{
	if (++tgt->init_retries > IBMVFC_MAX_TGT_INIT_RETRIES) {
		ibmvfc_del_tgt(tgt);
		wake_up(&tgt->vhost->work_wait_q);
		return 0;
	} else
		ibmvfc_init_tgt(tgt, job_step);
	return 1;
}

/* Defined in FC-LS */
static const struct {
	int code;
	int retry;
	int logged_in;
} prli_rsp [] = {
	{ 0, 1, 0 },
	{ 1, 0, 1 },
	{ 2, 1, 0 },
	{ 3, 1, 0 },
	{ 4, 0, 0 },
	{ 5, 0, 0 },
	{ 6, 0, 1 },
	{ 7, 0, 0 },
	{ 8, 1, 0 },
};

/**
 * ibmvfc_get_prli_rsp - Find PRLI response index
 * @flags:	PRLI response flags
 *
 **/
static int ibmvfc_get_prli_rsp(u16 flags)
{
	int i;
	int code = (flags & 0x0f00) >> 8;

	for (i = 0; i < ARRAY_SIZE(prli_rsp); i++)
		if (prli_rsp[i].code == code)
			return i;

	return 0;
}

/**
 * ibmvfc_tgt_prli_done - Completion handler for Process Login
 * @evt:	ibmvfc event struct
 *
 **/
static void ibmvfc_tgt_prli_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_target *tgt = evt->tgt;
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_process_login *rsp = &evt->xfer_iu->prli;
	struct ibmvfc_prli_svc_parms *parms = &rsp->parms;
	u32 status = be16_to_cpu(rsp->common.status);
	int index, level = IBMVFC_DEFAULT_LOG_LEVEL;

	vhost->discovery_threads--;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
	switch (status) {
	case IBMVFC_MAD_SUCCESS:
		tgt_dbg(tgt, "Process Login succeeded: %X %02X %04X\n",
			parms->type, parms->flags, parms->service_parms);

		if (parms->type == IBMVFC_SCSI_FCP_TYPE) {
			index = ibmvfc_get_prli_rsp(be16_to_cpu(parms->flags));
			if (prli_rsp[index].logged_in) {
				if (be16_to_cpu(parms->flags) & IBMVFC_PRLI_EST_IMG_PAIR) {
					tgt->need_login = 0;
					tgt->ids.roles = 0;
					if (be32_to_cpu(parms->service_parms) & IBMVFC_PRLI_TARGET_FUNC)
						tgt->ids.roles |= FC_PORT_ROLE_FCP_TARGET;
					if (be32_to_cpu(parms->service_parms) & IBMVFC_PRLI_INITIATOR_FUNC)
						tgt->ids.roles |= FC_PORT_ROLE_FCP_INITIATOR;
					tgt->add_rport = 1;
				} else
					ibmvfc_del_tgt(tgt);
			} else if (prli_rsp[index].retry)
				ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_prli);
			else
				ibmvfc_del_tgt(tgt);
		} else
			ibmvfc_del_tgt(tgt);
		break;
	case IBMVFC_MAD_DRIVER_FAILED:
		break;
	case IBMVFC_MAD_CRQ_ERROR:
		ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_prli);
		break;
	case IBMVFC_MAD_FAILED:
	default:
		if ((be16_to_cpu(rsp->status) & IBMVFC_VIOS_FAILURE) &&
		     be16_to_cpu(rsp->error) == IBMVFC_PLOGI_REQUIRED)
			level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_plogi);
		else if (tgt->logo_rcvd)
			level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_plogi);
		else if (ibmvfc_retry_cmd(be16_to_cpu(rsp->status), be16_to_cpu(rsp->error)))
			level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_prli);
		else
			ibmvfc_del_tgt(tgt);

		tgt_log(tgt, level, "Process Login failed: %s (%x:%x) rc=0x%02X\n",
			ibmvfc_get_cmd_error(be16_to_cpu(rsp->status), be16_to_cpu(rsp->error)),
			be16_to_cpu(rsp->status), be16_to_cpu(rsp->error), status);
		break;
	}

	kref_put(&tgt->kref, ibmvfc_release_tgt);
	ibmvfc_free_event(evt);
	wake_up(&vhost->work_wait_q);
}

/**
 * ibmvfc_tgt_send_prli - Send a process login
 * @tgt:	ibmvfc target struct
 *
 **/
static void ibmvfc_tgt_send_prli(struct ibmvfc_target *tgt)
{
	struct ibmvfc_process_login *prli;
	struct ibmvfc_host *vhost = tgt->vhost;
	struct ibmvfc_event *evt;

	if (vhost->discovery_threads >= disc_threads)
		return;

	kref_get(&tgt->kref);
	evt = ibmvfc_get_event(&vhost->crq);
	vhost->discovery_threads++;
	ibmvfc_init_event(evt, ibmvfc_tgt_prli_done, IBMVFC_MAD_FORMAT);
	evt->tgt = tgt;
	prli = &evt->iu.prli;
	memset(prli, 0, sizeof(*prli));
	if (ibmvfc_check_caps(vhost, IBMVFC_HANDLE_VF_WWPN)) {
		prli->common.version = cpu_to_be32(2);
		prli->target_wwpn = cpu_to_be64(tgt->wwpn);
	} else {
		prli->common.version = cpu_to_be32(1);
	}
	prli->common.opcode = cpu_to_be32(IBMVFC_PROCESS_LOGIN);
	prli->common.length = cpu_to_be16(sizeof(*prli));
	prli->scsi_id = cpu_to_be64(tgt->scsi_id);

	prli->parms.type = IBMVFC_SCSI_FCP_TYPE;
	prli->parms.flags = cpu_to_be16(IBMVFC_PRLI_EST_IMG_PAIR);
	prli->parms.service_parms = cpu_to_be32(IBMVFC_PRLI_INITIATOR_FUNC);
	prli->parms.service_parms |= cpu_to_be32(IBMVFC_PRLI_READ_FCP_XFER_RDY_DISABLED);

	if (cls3_error)
		prli->parms.service_parms |= cpu_to_be32(IBMVFC_PRLI_RETRY);

	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_INIT_WAIT);
	if (ibmvfc_send_event(evt, vhost, default_timeout)) {
		vhost->discovery_threads--;
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
		kref_put(&tgt->kref, ibmvfc_release_tgt);
	} else
		tgt_dbg(tgt, "Sent process login\n");
}

/**
 * ibmvfc_tgt_plogi_done - Completion handler for Port Login
 * @evt:	ibmvfc event struct
 *
 **/
static void ibmvfc_tgt_plogi_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_target *tgt = evt->tgt;
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_port_login *rsp = &evt->xfer_iu->plogi;
	u32 status = be16_to_cpu(rsp->common.status);
	int level = IBMVFC_DEFAULT_LOG_LEVEL;

	vhost->discovery_threads--;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
	switch (status) {
	case IBMVFC_MAD_SUCCESS:
		tgt_dbg(tgt, "Port Login succeeded\n");
		if (tgt->ids.port_name &&
		    tgt->ids.port_name != wwn_to_u64(rsp->service_parms.port_name)) {
			vhost->reinit = 1;
			tgt_dbg(tgt, "Port re-init required\n");
			break;
		}
		tgt->ids.node_name = wwn_to_u64(rsp->service_parms.node_name);
		tgt->ids.port_name = wwn_to_u64(rsp->service_parms.port_name);
		tgt->ids.port_id = tgt->scsi_id;
		memcpy(&tgt->service_parms, &rsp->service_parms,
		       sizeof(tgt->service_parms));
		memcpy(&tgt->service_parms_change, &rsp->service_parms_change,
		       sizeof(tgt->service_parms_change));
		ibmvfc_init_tgt(tgt, ibmvfc_tgt_send_prli);
		break;
	case IBMVFC_MAD_DRIVER_FAILED:
		break;
	case IBMVFC_MAD_CRQ_ERROR:
		ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_plogi);
		break;
	case IBMVFC_MAD_FAILED:
	default:
		if (ibmvfc_retry_cmd(be16_to_cpu(rsp->status), be16_to_cpu(rsp->error)))
			level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_plogi);
		else
			ibmvfc_del_tgt(tgt);

		tgt_log(tgt, level, "Port Login failed: %s (%x:%x) %s (%x) %s (%x) rc=0x%02X\n",
			ibmvfc_get_cmd_error(be16_to_cpu(rsp->status), be16_to_cpu(rsp->error)),
					     be16_to_cpu(rsp->status), be16_to_cpu(rsp->error),
			ibmvfc_get_fc_type(be16_to_cpu(rsp->fc_type)), be16_to_cpu(rsp->fc_type),
			ibmvfc_get_ls_explain(be16_to_cpu(rsp->fc_explain)), be16_to_cpu(rsp->fc_explain), status);
		break;
	}

	kref_put(&tgt->kref, ibmvfc_release_tgt);
	ibmvfc_free_event(evt);
	wake_up(&vhost->work_wait_q);
}

/**
 * ibmvfc_tgt_send_plogi - Send PLOGI to the specified target
 * @tgt:	ibmvfc target struct
 *
 **/
static void ibmvfc_tgt_send_plogi(struct ibmvfc_target *tgt)
{
	struct ibmvfc_port_login *plogi;
	struct ibmvfc_host *vhost = tgt->vhost;
	struct ibmvfc_event *evt;

	if (vhost->discovery_threads >= disc_threads)
		return;

	kref_get(&tgt->kref);
	tgt->logo_rcvd = 0;
	evt = ibmvfc_get_event(&vhost->crq);
	vhost->discovery_threads++;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_INIT_WAIT);
	ibmvfc_init_event(evt, ibmvfc_tgt_plogi_done, IBMVFC_MAD_FORMAT);
	evt->tgt = tgt;
	plogi = &evt->iu.plogi;
	memset(plogi, 0, sizeof(*plogi));
	if (ibmvfc_check_caps(vhost, IBMVFC_HANDLE_VF_WWPN)) {
		plogi->common.version = cpu_to_be32(2);
		plogi->target_wwpn = cpu_to_be64(tgt->wwpn);
	} else {
		plogi->common.version = cpu_to_be32(1);
	}
	plogi->common.opcode = cpu_to_be32(IBMVFC_PORT_LOGIN);
	plogi->common.length = cpu_to_be16(sizeof(*plogi));
	plogi->scsi_id = cpu_to_be64(tgt->scsi_id);

	if (ibmvfc_send_event(evt, vhost, default_timeout)) {
		vhost->discovery_threads--;
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
		kref_put(&tgt->kref, ibmvfc_release_tgt);
	} else
		tgt_dbg(tgt, "Sent port login\n");
}

/**
 * ibmvfc_tgt_implicit_logout_done - Completion handler for Implicit Logout MAD
 * @evt:	ibmvfc event struct
 *
 **/
static void ibmvfc_tgt_implicit_logout_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_target *tgt = evt->tgt;
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_implicit_logout *rsp = &evt->xfer_iu->implicit_logout;
	u32 status = be16_to_cpu(rsp->common.status);

	vhost->discovery_threads--;
	ibmvfc_free_event(evt);
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);

	switch (status) {
	case IBMVFC_MAD_SUCCESS:
		tgt_dbg(tgt, "Implicit Logout succeeded\n");
		break;
	case IBMVFC_MAD_DRIVER_FAILED:
		kref_put(&tgt->kref, ibmvfc_release_tgt);
		wake_up(&vhost->work_wait_q);
		return;
	case IBMVFC_MAD_FAILED:
	default:
		tgt_err(tgt, "Implicit Logout failed: rc=0x%02X\n", status);
		break;
	}

	ibmvfc_init_tgt(tgt, ibmvfc_tgt_send_plogi);
	kref_put(&tgt->kref, ibmvfc_release_tgt);
	wake_up(&vhost->work_wait_q);
}

/**
 * __ibmvfc_tgt_get_implicit_logout_evt - Allocate and init an event for implicit logout
 * @tgt:		ibmvfc target struct
 * @done:		Routine to call when the event is responded to
 *
 * Returns:
 *	Allocated and initialized ibmvfc_event struct
 **/
static struct ibmvfc_event *__ibmvfc_tgt_get_implicit_logout_evt(struct ibmvfc_target *tgt,
								 void (*done) (struct ibmvfc_event *))
{
	struct ibmvfc_implicit_logout *mad;
	struct ibmvfc_host *vhost = tgt->vhost;
	struct ibmvfc_event *evt;

	kref_get(&tgt->kref);
	evt = ibmvfc_get_event(&vhost->crq);
	ibmvfc_init_event(evt, done, IBMVFC_MAD_FORMAT);
	evt->tgt = tgt;
	mad = &evt->iu.implicit_logout;
	memset(mad, 0, sizeof(*mad));
	mad->common.version = cpu_to_be32(1);
	mad->common.opcode = cpu_to_be32(IBMVFC_IMPLICIT_LOGOUT);
	mad->common.length = cpu_to_be16(sizeof(*mad));
	mad->old_scsi_id = cpu_to_be64(tgt->scsi_id);
	return evt;
}

/**
 * ibmvfc_tgt_implicit_logout - Initiate an Implicit Logout for specified target
 * @tgt:		ibmvfc target struct
 *
 **/
static void ibmvfc_tgt_implicit_logout(struct ibmvfc_target *tgt)
{
	struct ibmvfc_host *vhost = tgt->vhost;
	struct ibmvfc_event *evt;

	if (vhost->discovery_threads >= disc_threads)
		return;

	vhost->discovery_threads++;
	evt = __ibmvfc_tgt_get_implicit_logout_evt(tgt,
						   ibmvfc_tgt_implicit_logout_done);

	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_INIT_WAIT);
	if (ibmvfc_send_event(evt, vhost, default_timeout)) {
		vhost->discovery_threads--;
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
		kref_put(&tgt->kref, ibmvfc_release_tgt);
	} else
		tgt_dbg(tgt, "Sent Implicit Logout\n");
}

/**
 * ibmvfc_tgt_implicit_logout_and_del_done - Completion handler for Implicit Logout MAD
 * @evt:	ibmvfc event struct
 *
 **/
static void ibmvfc_tgt_implicit_logout_and_del_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_target *tgt = evt->tgt;
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_passthru_mad *mad = &evt->xfer_iu->passthru;
	u32 status = be16_to_cpu(mad->common.status);

	vhost->discovery_threads--;
	ibmvfc_free_event(evt);

	/*
	 * If our state is IBMVFC_HOST_OFFLINE, we could be unloading the
	 * driver in which case we need to free up all the targets. If we are
	 * not unloading, we will still go through a hard reset to get out of
	 * offline state, so there is no need to track the old targets in that
	 * case.
	 */
	if (status == IBMVFC_MAD_SUCCESS || vhost->state == IBMVFC_HOST_OFFLINE)
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
	else
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_AND_LOGOUT_RPORT);

	tgt_dbg(tgt, "Implicit Logout %s\n", (status == IBMVFC_MAD_SUCCESS) ? "succeeded" : "failed");
	kref_put(&tgt->kref, ibmvfc_release_tgt);
	wake_up(&vhost->work_wait_q);
}

/**
 * ibmvfc_tgt_implicit_logout_and_del - Initiate an Implicit Logout for specified target
 * @tgt:		ibmvfc target struct
 *
 **/
static void ibmvfc_tgt_implicit_logout_and_del(struct ibmvfc_target *tgt)
{
	struct ibmvfc_host *vhost = tgt->vhost;
	struct ibmvfc_event *evt;

	if (!vhost->logged_in) {
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
		return;
	}

	if (vhost->discovery_threads >= disc_threads)
		return;

	vhost->discovery_threads++;
	evt = __ibmvfc_tgt_get_implicit_logout_evt(tgt,
						   ibmvfc_tgt_implicit_logout_and_del_done);

	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_LOGOUT_RPORT_WAIT);
	if (ibmvfc_send_event(evt, vhost, default_timeout)) {
		vhost->discovery_threads--;
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
		kref_put(&tgt->kref, ibmvfc_release_tgt);
	} else
		tgt_dbg(tgt, "Sent Implicit Logout\n");
}

/**
 * ibmvfc_tgt_move_login_done - Completion handler for Move Login
 * @evt:	ibmvfc event struct
 *
 **/
static void ibmvfc_tgt_move_login_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_target *tgt = evt->tgt;
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_move_login *rsp = &evt->xfer_iu->move_login;
	u32 status = be16_to_cpu(rsp->common.status);
	int level = IBMVFC_DEFAULT_LOG_LEVEL;

	vhost->discovery_threads--;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
	switch (status) {
	case IBMVFC_MAD_SUCCESS:
		tgt_dbg(tgt, "Move Login succeeded for new scsi_id: %llX\n", tgt->new_scsi_id);
		tgt->ids.node_name = wwn_to_u64(rsp->service_parms.node_name);
		tgt->ids.port_name = wwn_to_u64(rsp->service_parms.port_name);
		tgt->scsi_id = tgt->new_scsi_id;
		tgt->ids.port_id = tgt->scsi_id;
		memcpy(&tgt->service_parms, &rsp->service_parms,
		       sizeof(tgt->service_parms));
		memcpy(&tgt->service_parms_change, &rsp->service_parms_change,
		       sizeof(tgt->service_parms_change));
		ibmvfc_init_tgt(tgt, ibmvfc_tgt_send_prli);
		break;
	case IBMVFC_MAD_DRIVER_FAILED:
		break;
	case IBMVFC_MAD_CRQ_ERROR:
		ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_move_login);
		break;
	case IBMVFC_MAD_FAILED:
	default:
		level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_move_login);

		tgt_log(tgt, level,
			"Move Login failed: new scsi_id: %llX, flags:%x, vios_flags:%x, rc=0x%02X\n",
			tgt->new_scsi_id, be32_to_cpu(rsp->flags), be16_to_cpu(rsp->vios_flags),
			status);
		break;
	}

	kref_put(&tgt->kref, ibmvfc_release_tgt);
	ibmvfc_free_event(evt);
	wake_up(&vhost->work_wait_q);
}


/**
 * ibmvfc_tgt_move_login - Initiate a move login for specified target
 * @tgt:		ibmvfc target struct
 *
 **/
static void ibmvfc_tgt_move_login(struct ibmvfc_target *tgt)
{
	struct ibmvfc_host *vhost = tgt->vhost;
	struct ibmvfc_move_login *move;
	struct ibmvfc_event *evt;

	if (vhost->discovery_threads >= disc_threads)
		return;

	kref_get(&tgt->kref);
	evt = ibmvfc_get_event(&vhost->crq);
	vhost->discovery_threads++;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_INIT_WAIT);
	ibmvfc_init_event(evt, ibmvfc_tgt_move_login_done, IBMVFC_MAD_FORMAT);
	evt->tgt = tgt;
	move = &evt->iu.move_login;
	memset(move, 0, sizeof(*move));
	move->common.version = cpu_to_be32(1);
	move->common.opcode = cpu_to_be32(IBMVFC_MOVE_LOGIN);
	move->common.length = cpu_to_be16(sizeof(*move));

	move->old_scsi_id = cpu_to_be64(tgt->scsi_id);
	move->new_scsi_id = cpu_to_be64(tgt->new_scsi_id);
	move->wwpn = cpu_to_be64(tgt->wwpn);
	move->node_name = cpu_to_be64(tgt->ids.node_name);

	if (ibmvfc_send_event(evt, vhost, default_timeout)) {
		vhost->discovery_threads--;
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
		kref_put(&tgt->kref, ibmvfc_release_tgt);
	} else
		tgt_dbg(tgt, "Sent Move Login for new scsi_id: %llX\n", tgt->new_scsi_id);
}

/**
 * ibmvfc_adisc_needs_plogi - Does device need PLOGI?
 * @mad:	ibmvfc passthru mad struct
 * @tgt:	ibmvfc target struct
 *
 * Returns:
 *	1 if PLOGI needed / 0 if PLOGI not needed
 **/
static int ibmvfc_adisc_needs_plogi(struct ibmvfc_passthru_mad *mad,
				    struct ibmvfc_target *tgt)
{
	if (wwn_to_u64((u8 *)&mad->fc_iu.response[2]) != tgt->ids.port_name)
		return 1;
	if (wwn_to_u64((u8 *)&mad->fc_iu.response[4]) != tgt->ids.node_name)
		return 1;
	if (be32_to_cpu(mad->fc_iu.response[6]) != tgt->scsi_id)
		return 1;
	return 0;
}

/**
 * ibmvfc_tgt_adisc_done - Completion handler for ADISC
 * @evt:	ibmvfc event struct
 *
 **/
static void ibmvfc_tgt_adisc_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_target *tgt = evt->tgt;
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_passthru_mad *mad = &evt->xfer_iu->passthru;
	u32 status = be16_to_cpu(mad->common.status);
	u8 fc_reason, fc_explain;

	vhost->discovery_threads--;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
	del_timer(&tgt->timer);

	switch (status) {
	case IBMVFC_MAD_SUCCESS:
		tgt_dbg(tgt, "ADISC succeeded\n");
		if (ibmvfc_adisc_needs_plogi(mad, tgt))
			ibmvfc_del_tgt(tgt);
		break;
	case IBMVFC_MAD_DRIVER_FAILED:
		break;
	case IBMVFC_MAD_FAILED:
	default:
		ibmvfc_del_tgt(tgt);
		fc_reason = (be32_to_cpu(mad->fc_iu.response[1]) & 0x00ff0000) >> 16;
		fc_explain = (be32_to_cpu(mad->fc_iu.response[1]) & 0x0000ff00) >> 8;
		tgt_info(tgt, "ADISC failed: %s (%x:%x) %s (%x) %s (%x) rc=0x%02X\n",
			 ibmvfc_get_cmd_error(be16_to_cpu(mad->iu.status), be16_to_cpu(mad->iu.error)),
			 be16_to_cpu(mad->iu.status), be16_to_cpu(mad->iu.error),
			 ibmvfc_get_fc_type(fc_reason), fc_reason,
			 ibmvfc_get_ls_explain(fc_explain), fc_explain, status);
		break;
	}

	kref_put(&tgt->kref, ibmvfc_release_tgt);
	ibmvfc_free_event(evt);
	wake_up(&vhost->work_wait_q);
}

/**
 * ibmvfc_init_passthru - Initialize an event struct for FC passthru
 * @evt:		ibmvfc event struct
 *
 **/
static void ibmvfc_init_passthru(struct ibmvfc_event *evt)
{
	struct ibmvfc_passthru_mad *mad = &evt->iu.passthru;

	memset(mad, 0, sizeof(*mad));
	mad->common.version = cpu_to_be32(1);
	mad->common.opcode = cpu_to_be32(IBMVFC_PASSTHRU);
	mad->common.length = cpu_to_be16(sizeof(*mad) - sizeof(mad->fc_iu) - sizeof(mad->iu));
	mad->cmd_ioba.va = cpu_to_be64((u64)be64_to_cpu(evt->crq.ioba) +
		offsetof(struct ibmvfc_passthru_mad, iu));
	mad->cmd_ioba.len = cpu_to_be32(sizeof(mad->iu));
	mad->iu.cmd_len = cpu_to_be32(sizeof(mad->fc_iu.payload));
	mad->iu.rsp_len = cpu_to_be32(sizeof(mad->fc_iu.response));
	mad->iu.cmd.va = cpu_to_be64((u64)be64_to_cpu(evt->crq.ioba) +
		offsetof(struct ibmvfc_passthru_mad, fc_iu) +
		offsetof(struct ibmvfc_passthru_fc_iu, payload));
	mad->iu.cmd.len = cpu_to_be32(sizeof(mad->fc_iu.payload));
	mad->iu.rsp.va = cpu_to_be64((u64)be64_to_cpu(evt->crq.ioba) +
		offsetof(struct ibmvfc_passthru_mad, fc_iu) +
		offsetof(struct ibmvfc_passthru_fc_iu, response));
	mad->iu.rsp.len = cpu_to_be32(sizeof(mad->fc_iu.response));
}

/**
 * ibmvfc_tgt_adisc_cancel_done - Completion handler when cancelling an ADISC
 * @evt:		ibmvfc event struct
 *
 * Just cleanup this event struct. Everything else is handled by
 * the ADISC completion handler. If the ADISC never actually comes
 * back, we still have the timer running on the ADISC event struct
 * which will fire and cause the CRQ to get reset.
 *
 **/
static void ibmvfc_tgt_adisc_cancel_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_target *tgt = evt->tgt;

	tgt_dbg(tgt, "ADISC cancel complete\n");
	vhost->abort_threads--;
	ibmvfc_free_event(evt);
	kref_put(&tgt->kref, ibmvfc_release_tgt);
	wake_up(&vhost->work_wait_q);
}

/**
 * ibmvfc_adisc_timeout - Handle an ADISC timeout
 * @t:		ibmvfc target struct
 *
 * If an ADISC times out, send a cancel. If the cancel times
 * out, reset the CRQ. When the ADISC comes back as cancelled,
 * log back into the target.
 **/
static void ibmvfc_adisc_timeout(struct timer_list *t)
{
	struct ibmvfc_target *tgt = from_timer(tgt, t, timer);
	struct ibmvfc_host *vhost = tgt->vhost;
	struct ibmvfc_event *evt;
	struct ibmvfc_tmf *tmf;
	unsigned long flags;
	int rc;

	tgt_dbg(tgt, "ADISC timeout\n");
	spin_lock_irqsave(vhost->host->host_lock, flags);
	if (vhost->abort_threads >= disc_threads ||
	    tgt->action != IBMVFC_TGT_ACTION_INIT_WAIT ||
	    vhost->state != IBMVFC_INITIALIZING ||
	    vhost->action != IBMVFC_HOST_ACTION_QUERY_TGTS) {
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		return;
	}

	vhost->abort_threads++;
	kref_get(&tgt->kref);
	evt = ibmvfc_get_event(&vhost->crq);
	ibmvfc_init_event(evt, ibmvfc_tgt_adisc_cancel_done, IBMVFC_MAD_FORMAT);

	evt->tgt = tgt;
	tmf = &evt->iu.tmf;
	memset(tmf, 0, sizeof(*tmf));
	if (ibmvfc_check_caps(vhost, IBMVFC_HANDLE_VF_WWPN)) {
		tmf->common.version = cpu_to_be32(2);
		tmf->target_wwpn = cpu_to_be64(tgt->wwpn);
	} else {
		tmf->common.version = cpu_to_be32(1);
	}
	tmf->common.opcode = cpu_to_be32(IBMVFC_TMF_MAD);
	tmf->common.length = cpu_to_be16(sizeof(*tmf));
	tmf->scsi_id = cpu_to_be64(tgt->scsi_id);
	tmf->cancel_key = cpu_to_be32(tgt->cancel_key);

	rc = ibmvfc_send_event(evt, vhost, default_timeout);

	if (rc) {
		tgt_err(tgt, "Failed to send cancel event for ADISC. rc=%d\n", rc);
		vhost->abort_threads--;
		kref_put(&tgt->kref, ibmvfc_release_tgt);
		__ibmvfc_reset_host(vhost);
	} else
		tgt_dbg(tgt, "Attempting to cancel ADISC\n");
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
}

/**
 * ibmvfc_tgt_adisc - Initiate an ADISC for specified target
 * @tgt:		ibmvfc target struct
 *
 * When sending an ADISC we end up with two timers running. The
 * first timer is the timer in the ibmvfc target struct. If this
 * fires, we send a cancel to the target. The second timer is the
 * timer on the ibmvfc event for the ADISC, which is longer. If that
 * fires, it means the ADISC timed out and our attempt to cancel it
 * also failed, so we need to reset the CRQ.
 **/
static void ibmvfc_tgt_adisc(struct ibmvfc_target *tgt)
{
	struct ibmvfc_passthru_mad *mad;
	struct ibmvfc_host *vhost = tgt->vhost;
	struct ibmvfc_event *evt;

	if (vhost->discovery_threads >= disc_threads)
		return;

	kref_get(&tgt->kref);
	evt = ibmvfc_get_event(&vhost->crq);
	vhost->discovery_threads++;
	ibmvfc_init_event(evt, ibmvfc_tgt_adisc_done, IBMVFC_MAD_FORMAT);
	evt->tgt = tgt;

	ibmvfc_init_passthru(evt);
	mad = &evt->iu.passthru;
	mad->iu.flags = cpu_to_be32(IBMVFC_FC_ELS);
	mad->iu.scsi_id = cpu_to_be64(tgt->scsi_id);
	mad->iu.cancel_key = cpu_to_be32(tgt->cancel_key);

	mad->fc_iu.payload[0] = cpu_to_be32(IBMVFC_ADISC);
	memcpy(&mad->fc_iu.payload[2], &vhost->login_buf->resp.port_name,
	       sizeof(vhost->login_buf->resp.port_name));
	memcpy(&mad->fc_iu.payload[4], &vhost->login_buf->resp.node_name,
	       sizeof(vhost->login_buf->resp.node_name));
	mad->fc_iu.payload[6] = cpu_to_be32(be64_to_cpu(vhost->login_buf->resp.scsi_id) & 0x00ffffff);

	if (timer_pending(&tgt->timer))
		mod_timer(&tgt->timer, jiffies + (IBMVFC_ADISC_TIMEOUT * HZ));
	else {
		tgt->timer.expires = jiffies + (IBMVFC_ADISC_TIMEOUT * HZ);
		add_timer(&tgt->timer);
	}

	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_INIT_WAIT);
	if (ibmvfc_send_event(evt, vhost, IBMVFC_ADISC_PLUS_CANCEL_TIMEOUT)) {
		vhost->discovery_threads--;
		del_timer(&tgt->timer);
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
		kref_put(&tgt->kref, ibmvfc_release_tgt);
	} else
		tgt_dbg(tgt, "Sent ADISC\n");
}

/**
 * ibmvfc_tgt_query_target_done - Completion handler for Query Target MAD
 * @evt:	ibmvfc event struct
 *
 **/
static void ibmvfc_tgt_query_target_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_target *tgt = evt->tgt;
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_query_tgt *rsp = &evt->xfer_iu->query_tgt;
	u32 status = be16_to_cpu(rsp->common.status);
	int level = IBMVFC_DEFAULT_LOG_LEVEL;

	vhost->discovery_threads--;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
	switch (status) {
	case IBMVFC_MAD_SUCCESS:
		tgt_dbg(tgt, "Query Target succeeded\n");
		if (be64_to_cpu(rsp->scsi_id) != tgt->scsi_id)
			ibmvfc_del_tgt(tgt);
		else
			ibmvfc_init_tgt(tgt, ibmvfc_tgt_adisc);
		break;
	case IBMVFC_MAD_DRIVER_FAILED:
		break;
	case IBMVFC_MAD_CRQ_ERROR:
		ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_query_target);
		break;
	case IBMVFC_MAD_FAILED:
	default:
		if ((be16_to_cpu(rsp->status) & IBMVFC_FABRIC_MAPPED) == IBMVFC_FABRIC_MAPPED &&
		    be16_to_cpu(rsp->error) == IBMVFC_UNABLE_TO_PERFORM_REQ &&
		    be16_to_cpu(rsp->fc_explain) == IBMVFC_PORT_NAME_NOT_REG)
			ibmvfc_del_tgt(tgt);
		else if (ibmvfc_retry_cmd(be16_to_cpu(rsp->status), be16_to_cpu(rsp->error)))
			level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_query_target);
		else
			ibmvfc_del_tgt(tgt);

		tgt_log(tgt, level, "Query Target failed: %s (%x:%x) %s (%x) %s (%x) rc=0x%02X\n",
			ibmvfc_get_cmd_error(be16_to_cpu(rsp->status), be16_to_cpu(rsp->error)),
			be16_to_cpu(rsp->status), be16_to_cpu(rsp->error),
			ibmvfc_get_fc_type(be16_to_cpu(rsp->fc_type)), be16_to_cpu(rsp->fc_type),
			ibmvfc_get_gs_explain(be16_to_cpu(rsp->fc_explain)), be16_to_cpu(rsp->fc_explain),
			status);
		break;
	}

	kref_put(&tgt->kref, ibmvfc_release_tgt);
	ibmvfc_free_event(evt);
	wake_up(&vhost->work_wait_q);
}

/**
 * ibmvfc_tgt_query_target - Initiate a Query Target for specified target
 * @tgt:	ibmvfc target struct
 *
 **/
static void ibmvfc_tgt_query_target(struct ibmvfc_target *tgt)
{
	struct ibmvfc_query_tgt *query_tgt;
	struct ibmvfc_host *vhost = tgt->vhost;
	struct ibmvfc_event *evt;

	if (vhost->discovery_threads >= disc_threads)
		return;

	kref_get(&tgt->kref);
	evt = ibmvfc_get_event(&vhost->crq);
	vhost->discovery_threads++;
	evt->tgt = tgt;
	ibmvfc_init_event(evt, ibmvfc_tgt_query_target_done, IBMVFC_MAD_FORMAT);
	query_tgt = &evt->iu.query_tgt;
	memset(query_tgt, 0, sizeof(*query_tgt));
	query_tgt->common.version = cpu_to_be32(1);
	query_tgt->common.opcode = cpu_to_be32(IBMVFC_QUERY_TARGET);
	query_tgt->common.length = cpu_to_be16(sizeof(*query_tgt));
	query_tgt->wwpn = cpu_to_be64(tgt->ids.port_name);

	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_INIT_WAIT);
	if (ibmvfc_send_event(evt, vhost, default_timeout)) {
		vhost->discovery_threads--;
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
		kref_put(&tgt->kref, ibmvfc_release_tgt);
	} else
		tgt_dbg(tgt, "Sent Query Target\n");
}

/**
 * ibmvfc_alloc_target - Allocate and initialize an ibmvfc target
 * @vhost:		ibmvfc host struct
 * @target:		Holds SCSI ID to allocate target forand the WWPN
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_alloc_target(struct ibmvfc_host *vhost,
			       struct ibmvfc_discover_targets_entry *target)
{
	struct ibmvfc_target *stgt = NULL;
	struct ibmvfc_target *wtgt = NULL;
	struct ibmvfc_target *tgt;
	unsigned long flags;
	u64 scsi_id = be32_to_cpu(target->scsi_id) & IBMVFC_DISC_TGT_SCSI_ID_MASK;
	u64 wwpn = be64_to_cpu(target->wwpn);

	/* Look to see if we already have a target allocated for this SCSI ID or WWPN */
	spin_lock_irqsave(vhost->host->host_lock, flags);
	list_for_each_entry(tgt, &vhost->targets, queue) {
		if (tgt->wwpn == wwpn) {
			wtgt = tgt;
			break;
		}
	}

	list_for_each_entry(tgt, &vhost->targets, queue) {
		if (tgt->scsi_id == scsi_id) {
			stgt = tgt;
			break;
		}
	}

	if (wtgt && !stgt) {
		/*
		 * A WWPN target has moved and we still are tracking the old
		 * SCSI ID.  The only way we should be able to get here is if
		 * we attempted to send an implicit logout for the old SCSI ID
		 * and it failed for some reason, such as there being I/O
		 * pending to the target. In this case, we will have already
		 * deleted the rport from the FC transport so we do a move
		 * login, which works even with I/O pending, however, if
		 * there is still I/O pending, it will stay outstanding, so
		 * we only do this if fast fail is disabled for the rport,
		 * otherwise we let terminate_rport_io clean up the port
		 * before we login at the new location.
		 */
		if (wtgt->action == IBMVFC_TGT_ACTION_LOGOUT_DELETED_RPORT) {
			if (wtgt->move_login) {
				/*
				 * Do a move login here. The old target is no longer
				 * known to the transport layer We don't use the
				 * normal ibmvfc_set_tgt_action to set this, as we
				 * don't normally want to allow this state change.
				 */
				wtgt->new_scsi_id = scsi_id;
				wtgt->action = IBMVFC_TGT_ACTION_INIT;
				wtgt->init_retries = 0;
				ibmvfc_init_tgt(wtgt, ibmvfc_tgt_move_login);
			}
			goto unlock_out;
		} else {
			tgt_err(wtgt, "Unexpected target state: %d, %p\n",
				wtgt->action, wtgt->rport);
		}
	} else if (stgt) {
		if (tgt->need_login)
			ibmvfc_init_tgt(tgt, ibmvfc_tgt_implicit_logout);
		goto unlock_out;
	}
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	tgt = mempool_alloc(vhost->tgt_pool, GFP_NOIO);
	memset(tgt, 0, sizeof(*tgt));
	tgt->scsi_id = scsi_id;
	tgt->wwpn = wwpn;
	tgt->vhost = vhost;
	tgt->need_login = 1;
	timer_setup(&tgt->timer, ibmvfc_adisc_timeout, 0);
	kref_init(&tgt->kref);
	ibmvfc_init_tgt(tgt, ibmvfc_tgt_implicit_logout);
	spin_lock_irqsave(vhost->host->host_lock, flags);
	tgt->cancel_key = vhost->task_set++;
	list_add_tail(&tgt->queue, &vhost->targets);

unlock_out:
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	return 0;
}

/**
 * ibmvfc_alloc_targets - Allocate and initialize ibmvfc targets
 * @vhost:		ibmvfc host struct
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_alloc_targets(struct ibmvfc_host *vhost)
{
	int i, rc;

	for (i = 0, rc = 0; !rc && i < vhost->num_targets; i++)
		rc = ibmvfc_alloc_target(vhost, &vhost->disc_buf[i]);

	return rc;
}

/**
 * ibmvfc_discover_targets_done - Completion handler for discover targets MAD
 * @evt:	ibmvfc event struct
 *
 **/
static void ibmvfc_discover_targets_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_discover_targets *rsp = &evt->xfer_iu->discover_targets;
	u32 mad_status = be16_to_cpu(rsp->common.status);
	int level = IBMVFC_DEFAULT_LOG_LEVEL;

	switch (mad_status) {
	case IBMVFC_MAD_SUCCESS:
		ibmvfc_dbg(vhost, "Discover Targets succeeded\n");
		vhost->num_targets = be32_to_cpu(rsp->num_written);
		ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_ALLOC_TGTS);
		break;
	case IBMVFC_MAD_FAILED:
		level += ibmvfc_retry_host_init(vhost);
		ibmvfc_log(vhost, level, "Discover Targets failed: %s (%x:%x)\n",
			   ibmvfc_get_cmd_error(be16_to_cpu(rsp->status), be16_to_cpu(rsp->error)),
			   be16_to_cpu(rsp->status), be16_to_cpu(rsp->error));
		break;
	case IBMVFC_MAD_DRIVER_FAILED:
		break;
	default:
		dev_err(vhost->dev, "Invalid Discover Targets response: 0x%x\n", mad_status);
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		break;
	}

	ibmvfc_free_event(evt);
	wake_up(&vhost->work_wait_q);
}

/**
 * ibmvfc_discover_targets - Send Discover Targets MAD
 * @vhost:	ibmvfc host struct
 *
 **/
static void ibmvfc_discover_targets(struct ibmvfc_host *vhost)
{
	struct ibmvfc_discover_targets *mad;
	struct ibmvfc_event *evt = ibmvfc_get_event(&vhost->crq);

	ibmvfc_init_event(evt, ibmvfc_discover_targets_done, IBMVFC_MAD_FORMAT);
	mad = &evt->iu.discover_targets;
	memset(mad, 0, sizeof(*mad));
	mad->common.version = cpu_to_be32(1);
	mad->common.opcode = cpu_to_be32(IBMVFC_DISC_TARGETS);
	mad->common.length = cpu_to_be16(sizeof(*mad));
	mad->bufflen = cpu_to_be32(vhost->disc_buf_sz);
	mad->buffer.va = cpu_to_be64(vhost->disc_buf_dma);
	mad->buffer.len = cpu_to_be32(vhost->disc_buf_sz);
	mad->flags = cpu_to_be32(IBMVFC_DISC_TGT_PORT_ID_WWPN_LIST);
	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_INIT_WAIT);

	if (!ibmvfc_send_event(evt, vhost, default_timeout))
		ibmvfc_dbg(vhost, "Sent discover targets\n");
	else
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
}

static void ibmvfc_channel_setup_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_channel_setup *setup = vhost->channel_setup_buf;
	struct ibmvfc_scsi_channels *scrqs = &vhost->scsi_scrqs;
	u32 mad_status = be16_to_cpu(evt->xfer_iu->channel_setup.common.status);
	int level = IBMVFC_DEFAULT_LOG_LEVEL;
	int flags, active_queues, i;

	ibmvfc_free_event(evt);

	switch (mad_status) {
	case IBMVFC_MAD_SUCCESS:
		ibmvfc_dbg(vhost, "Channel Setup succeeded\n");
		flags = be32_to_cpu(setup->flags);
		vhost->do_enquiry = 0;
		active_queues = be32_to_cpu(setup->num_scsi_subq_channels);
		scrqs->active_queues = active_queues;

		if (flags & IBMVFC_CHANNELS_CANCELED) {
			ibmvfc_dbg(vhost, "Channels Canceled\n");
			vhost->using_channels = 0;
		} else {
			if (active_queues)
				vhost->using_channels = 1;
			for (i = 0; i < active_queues; i++)
				scrqs->scrqs[i].vios_cookie =
					be64_to_cpu(setup->channel_handles[i]);

			ibmvfc_dbg(vhost, "Using %u channels\n",
				   vhost->scsi_scrqs.active_queues);
		}
		break;
	case IBMVFC_MAD_FAILED:
		level += ibmvfc_retry_host_init(vhost);
		ibmvfc_log(vhost, level, "Channel Setup failed\n");
		fallthrough;
	case IBMVFC_MAD_DRIVER_FAILED:
		return;
	default:
		dev_err(vhost->dev, "Invalid Channel Setup response: 0x%x\n",
			mad_status);
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		return;
	}

	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_QUERY);
	wake_up(&vhost->work_wait_q);
}

static void ibmvfc_channel_setup(struct ibmvfc_host *vhost)
{
	struct ibmvfc_channel_setup_mad *mad;
	struct ibmvfc_channel_setup *setup_buf = vhost->channel_setup_buf;
	struct ibmvfc_event *evt = ibmvfc_get_event(&vhost->crq);
	struct ibmvfc_scsi_channels *scrqs = &vhost->scsi_scrqs;
	unsigned int num_channels =
		min(vhost->client_scsi_channels, vhost->max_vios_scsi_channels);
	int i;

	memset(setup_buf, 0, sizeof(*setup_buf));
	if (num_channels == 0)
		setup_buf->flags = cpu_to_be32(IBMVFC_CANCEL_CHANNELS);
	else {
		setup_buf->num_scsi_subq_channels = cpu_to_be32(num_channels);
		for (i = 0; i < num_channels; i++)
			setup_buf->channel_handles[i] = cpu_to_be64(scrqs->scrqs[i].cookie);
	}

	ibmvfc_init_event(evt, ibmvfc_channel_setup_done, IBMVFC_MAD_FORMAT);
	mad = &evt->iu.channel_setup;
	memset(mad, 0, sizeof(*mad));
	mad->common.version = cpu_to_be32(1);
	mad->common.opcode = cpu_to_be32(IBMVFC_CHANNEL_SETUP);
	mad->common.length = cpu_to_be16(sizeof(*mad));
	mad->buffer.va = cpu_to_be64(vhost->channel_setup_dma);
	mad->buffer.len = cpu_to_be32(sizeof(*vhost->channel_setup_buf));

	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_INIT_WAIT);

	if (!ibmvfc_send_event(evt, vhost, default_timeout))
		ibmvfc_dbg(vhost, "Sent channel setup\n");
	else
		ibmvfc_link_down(vhost, IBMVFC_LINK_DOWN);
}

static void ibmvfc_channel_enquiry_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_channel_enquiry *rsp = &evt->xfer_iu->channel_enquiry;
	u32 mad_status = be16_to_cpu(rsp->common.status);
	int level = IBMVFC_DEFAULT_LOG_LEVEL;

	switch (mad_status) {
	case IBMVFC_MAD_SUCCESS:
		ibmvfc_dbg(vhost, "Channel Enquiry succeeded\n");
		vhost->max_vios_scsi_channels = be32_to_cpu(rsp->num_scsi_subq_channels);
		ibmvfc_free_event(evt);
		break;
	case IBMVFC_MAD_FAILED:
		level += ibmvfc_retry_host_init(vhost);
		ibmvfc_log(vhost, level, "Channel Enquiry failed\n");
		fallthrough;
	case IBMVFC_MAD_DRIVER_FAILED:
		ibmvfc_free_event(evt);
		return;
	default:
		dev_err(vhost->dev, "Invalid Channel Enquiry response: 0x%x\n",
			mad_status);
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		ibmvfc_free_event(evt);
		return;
	}

	ibmvfc_channel_setup(vhost);
}

static void ibmvfc_channel_enquiry(struct ibmvfc_host *vhost)
{
	struct ibmvfc_channel_enquiry *mad;
	struct ibmvfc_event *evt = ibmvfc_get_event(&vhost->crq);

	ibmvfc_init_event(evt, ibmvfc_channel_enquiry_done, IBMVFC_MAD_FORMAT);
	mad = &evt->iu.channel_enquiry;
	memset(mad, 0, sizeof(*mad));
	mad->common.version = cpu_to_be32(1);
	mad->common.opcode = cpu_to_be32(IBMVFC_CHANNEL_ENQUIRY);
	mad->common.length = cpu_to_be16(sizeof(*mad));

	if (mig_channels_only)
		mad->flags |= cpu_to_be32(IBMVFC_NO_CHANNELS_TO_CRQ_SUPPORT);
	if (mig_no_less_channels)
		mad->flags |= cpu_to_be32(IBMVFC_NO_N_TO_M_CHANNELS_SUPPORT);

	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_INIT_WAIT);

	if (!ibmvfc_send_event(evt, vhost, default_timeout))
		ibmvfc_dbg(vhost, "Send channel enquiry\n");
	else
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
}

/**
 * ibmvfc_npiv_login_done - Completion handler for NPIV Login
 * @evt:	ibmvfc event struct
 *
 **/
static void ibmvfc_npiv_login_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_host *vhost = evt->vhost;
	u32 mad_status = be16_to_cpu(evt->xfer_iu->npiv_login.common.status);
	struct ibmvfc_npiv_login_resp *rsp = &vhost->login_buf->resp;
	unsigned int npiv_max_sectors;
	int level = IBMVFC_DEFAULT_LOG_LEVEL;

	switch (mad_status) {
	case IBMVFC_MAD_SUCCESS:
		ibmvfc_free_event(evt);
		break;
	case IBMVFC_MAD_FAILED:
		if (ibmvfc_retry_cmd(be16_to_cpu(rsp->status), be16_to_cpu(rsp->error)))
			level += ibmvfc_retry_host_init(vhost);
		else
			ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		ibmvfc_log(vhost, level, "NPIV Login failed: %s (%x:%x)\n",
			   ibmvfc_get_cmd_error(be16_to_cpu(rsp->status), be16_to_cpu(rsp->error)),
						be16_to_cpu(rsp->status), be16_to_cpu(rsp->error));
		ibmvfc_free_event(evt);
		return;
	case IBMVFC_MAD_CRQ_ERROR:
		ibmvfc_retry_host_init(vhost);
		fallthrough;
	case IBMVFC_MAD_DRIVER_FAILED:
		ibmvfc_free_event(evt);
		return;
	default:
		dev_err(vhost->dev, "Invalid NPIV Login response: 0x%x\n", mad_status);
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		ibmvfc_free_event(evt);
		return;
	}

	vhost->client_migrated = 0;

	if (!(be32_to_cpu(rsp->flags) & IBMVFC_NATIVE_FC)) {
		dev_err(vhost->dev, "Virtual adapter does not support FC. %x\n",
			rsp->flags);
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		wake_up(&vhost->work_wait_q);
		return;
	}

	if (be32_to_cpu(rsp->max_cmds) <= IBMVFC_NUM_INTERNAL_REQ) {
		dev_err(vhost->dev, "Virtual adapter supported queue depth too small: %d\n",
			rsp->max_cmds);
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		wake_up(&vhost->work_wait_q);
		return;
	}

	vhost->logged_in = 1;
	npiv_max_sectors = min((uint)(be64_to_cpu(rsp->max_dma_len) >> 9), IBMVFC_MAX_SECTORS);
	dev_info(vhost->dev, "Host partition: %s, device: %s %s %s max sectors %u\n",
		 rsp->partition_name, rsp->device_name, rsp->port_loc_code,
		 rsp->drc_name, npiv_max_sectors);

	fc_host_fabric_name(vhost->host) = be64_to_cpu(rsp->node_name);
	fc_host_node_name(vhost->host) = be64_to_cpu(rsp->node_name);
	fc_host_port_name(vhost->host) = be64_to_cpu(rsp->port_name);
	fc_host_port_id(vhost->host) = be64_to_cpu(rsp->scsi_id);
	fc_host_port_type(vhost->host) = FC_PORTTYPE_NPIV;
	fc_host_supported_classes(vhost->host) = 0;
	if (be32_to_cpu(rsp->service_parms.class1_parms[0]) & 0x80000000)
		fc_host_supported_classes(vhost->host) |= FC_COS_CLASS1;
	if (be32_to_cpu(rsp->service_parms.class2_parms[0]) & 0x80000000)
		fc_host_supported_classes(vhost->host) |= FC_COS_CLASS2;
	if (be32_to_cpu(rsp->service_parms.class3_parms[0]) & 0x80000000)
		fc_host_supported_classes(vhost->host) |= FC_COS_CLASS3;
	fc_host_maxframe_size(vhost->host) =
		be16_to_cpu(rsp->service_parms.common.bb_rcv_sz) & 0x0fff;

	vhost->host->can_queue = be32_to_cpu(rsp->max_cmds) - IBMVFC_NUM_INTERNAL_REQ;
	vhost->host->max_sectors = npiv_max_sectors;

	if (ibmvfc_check_caps(vhost, IBMVFC_CAN_SUPPORT_CHANNELS) && vhost->do_enquiry) {
		ibmvfc_channel_enquiry(vhost);
	} else {
		vhost->do_enquiry = 0;
		ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_QUERY);
		wake_up(&vhost->work_wait_q);
	}
}

/**
 * ibmvfc_npiv_login - Sends NPIV login
 * @vhost:	ibmvfc host struct
 *
 **/
static void ibmvfc_npiv_login(struct ibmvfc_host *vhost)
{
	struct ibmvfc_npiv_login_mad *mad;
	struct ibmvfc_event *evt = ibmvfc_get_event(&vhost->crq);

	ibmvfc_gather_partition_info(vhost);
	ibmvfc_set_login_info(vhost);
	ibmvfc_init_event(evt, ibmvfc_npiv_login_done, IBMVFC_MAD_FORMAT);

	memcpy(vhost->login_buf, &vhost->login_info, sizeof(vhost->login_info));
	mad = &evt->iu.npiv_login;
	memset(mad, 0, sizeof(struct ibmvfc_npiv_login_mad));
	mad->common.version = cpu_to_be32(1);
	mad->common.opcode = cpu_to_be32(IBMVFC_NPIV_LOGIN);
	mad->common.length = cpu_to_be16(sizeof(struct ibmvfc_npiv_login_mad));
	mad->buffer.va = cpu_to_be64(vhost->login_buf_dma);
	mad->buffer.len = cpu_to_be32(sizeof(*vhost->login_buf));

	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_INIT_WAIT);

	if (!ibmvfc_send_event(evt, vhost, default_timeout))
		ibmvfc_dbg(vhost, "Sent NPIV login\n");
	else
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
}

/**
 * ibmvfc_npiv_logout_done - Completion handler for NPIV Logout
 * @evt:		ibmvfc event struct
 *
 **/
static void ibmvfc_npiv_logout_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_host *vhost = evt->vhost;
	u32 mad_status = be16_to_cpu(evt->xfer_iu->npiv_logout.common.status);

	ibmvfc_free_event(evt);

	switch (mad_status) {
	case IBMVFC_MAD_SUCCESS:
		if (list_empty(&vhost->crq.sent) &&
		    vhost->action == IBMVFC_HOST_ACTION_LOGO_WAIT) {
			ibmvfc_init_host(vhost);
			return;
		}
		break;
	case IBMVFC_MAD_FAILED:
	case IBMVFC_MAD_NOT_SUPPORTED:
	case IBMVFC_MAD_CRQ_ERROR:
	case IBMVFC_MAD_DRIVER_FAILED:
	default:
		ibmvfc_dbg(vhost, "NPIV Logout failed. 0x%X\n", mad_status);
		break;
	}

	ibmvfc_hard_reset_host(vhost);
}

/**
 * ibmvfc_npiv_logout - Issue an NPIV Logout
 * @vhost:		ibmvfc host struct
 *
 **/
static void ibmvfc_npiv_logout(struct ibmvfc_host *vhost)
{
	struct ibmvfc_npiv_logout_mad *mad;
	struct ibmvfc_event *evt;

	evt = ibmvfc_get_event(&vhost->crq);
	ibmvfc_init_event(evt, ibmvfc_npiv_logout_done, IBMVFC_MAD_FORMAT);

	mad = &evt->iu.npiv_logout;
	memset(mad, 0, sizeof(*mad));
	mad->common.version = cpu_to_be32(1);
	mad->common.opcode = cpu_to_be32(IBMVFC_NPIV_LOGOUT);
	mad->common.length = cpu_to_be16(sizeof(struct ibmvfc_npiv_logout_mad));

	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_LOGO_WAIT);

	if (!ibmvfc_send_event(evt, vhost, default_timeout))
		ibmvfc_dbg(vhost, "Sent NPIV logout\n");
	else
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
}

/**
 * ibmvfc_dev_init_to_do - Is there target initialization work to do?
 * @vhost:		ibmvfc host struct
 *
 * Returns:
 *	1 if work to do / 0 if not
 **/
static int ibmvfc_dev_init_to_do(struct ibmvfc_host *vhost)
{
	struct ibmvfc_target *tgt;

	list_for_each_entry(tgt, &vhost->targets, queue) {
		if (tgt->action == IBMVFC_TGT_ACTION_INIT ||
		    tgt->action == IBMVFC_TGT_ACTION_INIT_WAIT)
			return 1;
	}

	return 0;
}

/**
 * ibmvfc_dev_logo_to_do - Is there target logout work to do?
 * @vhost:		ibmvfc host struct
 *
 * Returns:
 *	1 if work to do / 0 if not
 **/
static int ibmvfc_dev_logo_to_do(struct ibmvfc_host *vhost)
{
	struct ibmvfc_target *tgt;

	list_for_each_entry(tgt, &vhost->targets, queue) {
		if (tgt->action == IBMVFC_TGT_ACTION_LOGOUT_RPORT ||
		    tgt->action == IBMVFC_TGT_ACTION_LOGOUT_RPORT_WAIT)
			return 1;
	}
	return 0;
}

/**
 * __ibmvfc_work_to_do - Is there task level work to do? (no locking)
 * @vhost:		ibmvfc host struct
 *
 * Returns:
 *	1 if work to do / 0 if not
 **/
static int __ibmvfc_work_to_do(struct ibmvfc_host *vhost)
{
	struct ibmvfc_target *tgt;

	if (kthread_should_stop())
		return 1;
	switch (vhost->action) {
	case IBMVFC_HOST_ACTION_NONE:
	case IBMVFC_HOST_ACTION_INIT_WAIT:
	case IBMVFC_HOST_ACTION_LOGO_WAIT:
		return 0;
	case IBMVFC_HOST_ACTION_TGT_INIT:
	case IBMVFC_HOST_ACTION_QUERY_TGTS:
		if (vhost->discovery_threads == disc_threads)
			return 0;
		list_for_each_entry(tgt, &vhost->targets, queue)
			if (tgt->action == IBMVFC_TGT_ACTION_INIT)
				return 1;
		list_for_each_entry(tgt, &vhost->targets, queue)
			if (tgt->action == IBMVFC_TGT_ACTION_INIT_WAIT)
				return 0;
		return 1;
	case IBMVFC_HOST_ACTION_TGT_DEL:
	case IBMVFC_HOST_ACTION_TGT_DEL_FAILED:
		if (vhost->discovery_threads == disc_threads)
			return 0;
		list_for_each_entry(tgt, &vhost->targets, queue)
			if (tgt->action == IBMVFC_TGT_ACTION_LOGOUT_RPORT)
				return 1;
		list_for_each_entry(tgt, &vhost->targets, queue)
			if (tgt->action == IBMVFC_TGT_ACTION_LOGOUT_RPORT_WAIT)
				return 0;
		return 1;
	case IBMVFC_HOST_ACTION_LOGO:
	case IBMVFC_HOST_ACTION_INIT:
	case IBMVFC_HOST_ACTION_ALLOC_TGTS:
	case IBMVFC_HOST_ACTION_QUERY:
	case IBMVFC_HOST_ACTION_RESET:
	case IBMVFC_HOST_ACTION_REENABLE:
	default:
		break;
	}

	return 1;
}

/**
 * ibmvfc_work_to_do - Is there task level work to do?
 * @vhost:		ibmvfc host struct
 *
 * Returns:
 *	1 if work to do / 0 if not
 **/
static int ibmvfc_work_to_do(struct ibmvfc_host *vhost)
{
	unsigned long flags;
	int rc;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	rc = __ibmvfc_work_to_do(vhost);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	return rc;
}

/**
 * ibmvfc_log_ae - Log async events if necessary
 * @vhost:		ibmvfc host struct
 * @events:		events to log
 *
 **/
static void ibmvfc_log_ae(struct ibmvfc_host *vhost, int events)
{
	if (events & IBMVFC_AE_RSCN)
		fc_host_post_event(vhost->host, fc_get_event_number(), FCH_EVT_RSCN, 0);
	if ((events & IBMVFC_AE_LINKDOWN) &&
	    vhost->state >= IBMVFC_HALTED)
		fc_host_post_event(vhost->host, fc_get_event_number(), FCH_EVT_LINKDOWN, 0);
	if ((events & IBMVFC_AE_LINKUP) &&
	    vhost->state == IBMVFC_INITIALIZING)
		fc_host_post_event(vhost->host, fc_get_event_number(), FCH_EVT_LINKUP, 0);
}

/**
 * ibmvfc_tgt_add_rport - Tell the FC transport about a new remote port
 * @tgt:		ibmvfc target struct
 *
 **/
static void ibmvfc_tgt_add_rport(struct ibmvfc_target *tgt)
{
	struct ibmvfc_host *vhost = tgt->vhost;
	struct fc_rport *rport;
	unsigned long flags;

	tgt_dbg(tgt, "Adding rport\n");
	rport = fc_remote_port_add(vhost->host, 0, &tgt->ids);
	spin_lock_irqsave(vhost->host->host_lock, flags);

	if (rport && tgt->action == IBMVFC_TGT_ACTION_DEL_RPORT) {
		tgt_dbg(tgt, "Deleting rport\n");
		list_del(&tgt->queue);
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DELETED_RPORT);
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		fc_remote_port_delete(rport);
		del_timer_sync(&tgt->timer);
		kref_put(&tgt->kref, ibmvfc_release_tgt);
		return;
	} else if (rport && tgt->action == IBMVFC_TGT_ACTION_DEL_AND_LOGOUT_RPORT) {
		tgt_dbg(tgt, "Deleting rport with outstanding I/O\n");
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_LOGOUT_DELETED_RPORT);
		tgt->rport = NULL;
		tgt->init_retries = 0;
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		fc_remote_port_delete(rport);
		return;
	} else if (rport && tgt->action == IBMVFC_TGT_ACTION_DELETED_RPORT) {
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		return;
	}

	if (rport) {
		tgt_dbg(tgt, "rport add succeeded\n");
		tgt->rport = rport;
		rport->maxframe_size = be16_to_cpu(tgt->service_parms.common.bb_rcv_sz) & 0x0fff;
		rport->supported_classes = 0;
		tgt->target_id = rport->scsi_target_id;
		if (be32_to_cpu(tgt->service_parms.class1_parms[0]) & 0x80000000)
			rport->supported_classes |= FC_COS_CLASS1;
		if (be32_to_cpu(tgt->service_parms.class2_parms[0]) & 0x80000000)
			rport->supported_classes |= FC_COS_CLASS2;
		if (be32_to_cpu(tgt->service_parms.class3_parms[0]) & 0x80000000)
			rport->supported_classes |= FC_COS_CLASS3;
		if (rport->rqst_q)
			blk_queue_max_segments(rport->rqst_q, 1);
	} else
		tgt_dbg(tgt, "rport add failed\n");
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
}

/**
 * ibmvfc_do_work - Do task level work
 * @vhost:		ibmvfc host struct
 *
 **/
static void ibmvfc_do_work(struct ibmvfc_host *vhost)
{
	struct ibmvfc_target *tgt;
	unsigned long flags;
	struct fc_rport *rport;
	LIST_HEAD(purge);
	int rc;

	ibmvfc_log_ae(vhost, vhost->events_to_log);
	spin_lock_irqsave(vhost->host->host_lock, flags);
	vhost->events_to_log = 0;
	switch (vhost->action) {
	case IBMVFC_HOST_ACTION_NONE:
	case IBMVFC_HOST_ACTION_LOGO_WAIT:
	case IBMVFC_HOST_ACTION_INIT_WAIT:
		break;
	case IBMVFC_HOST_ACTION_RESET:
		list_splice_init(&vhost->purge, &purge);
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		ibmvfc_complete_purge(&purge);
		rc = ibmvfc_reset_crq(vhost);

		spin_lock_irqsave(vhost->host->host_lock, flags);
		if (!rc || rc == H_CLOSED)
			vio_enable_interrupts(to_vio_dev(vhost->dev));
		if (vhost->action == IBMVFC_HOST_ACTION_RESET) {
			/*
			 * The only action we could have changed to would have
			 * been reenable, in which case, we skip the rest of
			 * this path and wait until we've done the re-enable
			 * before sending the crq init.
			 */
			vhost->action = IBMVFC_HOST_ACTION_TGT_DEL;

			if (rc || (rc = ibmvfc_send_crq_init(vhost)) ||
			    (rc = vio_enable_interrupts(to_vio_dev(vhost->dev)))) {
				ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
				dev_err(vhost->dev, "Error after reset (rc=%d)\n", rc);
			}
		}
		break;
	case IBMVFC_HOST_ACTION_REENABLE:
		list_splice_init(&vhost->purge, &purge);
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		ibmvfc_complete_purge(&purge);
		rc = ibmvfc_reenable_crq_queue(vhost);

		spin_lock_irqsave(vhost->host->host_lock, flags);
		if (vhost->action == IBMVFC_HOST_ACTION_REENABLE) {
			/*
			 * The only action we could have changed to would have
			 * been reset, in which case, we skip the rest of this
			 * path and wait until we've done the reset before
			 * sending the crq init.
			 */
			vhost->action = IBMVFC_HOST_ACTION_TGT_DEL;
			if (rc || (rc = ibmvfc_send_crq_init(vhost))) {
				ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
				dev_err(vhost->dev, "Error after enable (rc=%d)\n", rc);
			}
		}
		break;
	case IBMVFC_HOST_ACTION_LOGO:
		vhost->job_step(vhost);
		break;
	case IBMVFC_HOST_ACTION_INIT:
		BUG_ON(vhost->state != IBMVFC_INITIALIZING);
		if (vhost->delay_init) {
			vhost->delay_init = 0;
			spin_unlock_irqrestore(vhost->host->host_lock, flags);
			ssleep(15);
			return;
		} else
			vhost->job_step(vhost);
		break;
	case IBMVFC_HOST_ACTION_QUERY:
		list_for_each_entry(tgt, &vhost->targets, queue)
			ibmvfc_init_tgt(tgt, ibmvfc_tgt_query_target);
		ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_QUERY_TGTS);
		break;
	case IBMVFC_HOST_ACTION_QUERY_TGTS:
		list_for_each_entry(tgt, &vhost->targets, queue) {
			if (tgt->action == IBMVFC_TGT_ACTION_INIT) {
				tgt->job_step(tgt);
				break;
			}
		}

		if (!ibmvfc_dev_init_to_do(vhost))
			ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_TGT_DEL);
		break;
	case IBMVFC_HOST_ACTION_TGT_DEL:
	case IBMVFC_HOST_ACTION_TGT_DEL_FAILED:
		list_for_each_entry(tgt, &vhost->targets, queue) {
			if (tgt->action == IBMVFC_TGT_ACTION_LOGOUT_RPORT) {
				tgt->job_step(tgt);
				break;
			}
		}

		if (ibmvfc_dev_logo_to_do(vhost)) {
			spin_unlock_irqrestore(vhost->host->host_lock, flags);
			return;
		}

		list_for_each_entry(tgt, &vhost->targets, queue) {
			if (tgt->action == IBMVFC_TGT_ACTION_DEL_RPORT) {
				tgt_dbg(tgt, "Deleting rport\n");
				rport = tgt->rport;
				tgt->rport = NULL;
				list_del(&tgt->queue);
				ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DELETED_RPORT);
				spin_unlock_irqrestore(vhost->host->host_lock, flags);
				if (rport)
					fc_remote_port_delete(rport);
				del_timer_sync(&tgt->timer);
				kref_put(&tgt->kref, ibmvfc_release_tgt);
				return;
			} else if (tgt->action == IBMVFC_TGT_ACTION_DEL_AND_LOGOUT_RPORT) {
				tgt_dbg(tgt, "Deleting rport with I/O outstanding\n");
				rport = tgt->rport;
				tgt->rport = NULL;
				tgt->init_retries = 0;
				ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_LOGOUT_DELETED_RPORT);

				/*
				 * If fast fail is enabled, we wait for it to fire and then clean up
				 * the old port, since we expect the fast fail timer to clean up the
				 * outstanding I/O faster than waiting for normal command timeouts.
				 * However, if fast fail is disabled, any I/O outstanding to the
				 * rport LUNs will stay outstanding indefinitely, since the EH handlers
				 * won't get invoked for I/O's timing out. If this is a NPIV failover
				 * scenario, the better alternative is to use the move login.
				 */
				if (rport && rport->fast_io_fail_tmo == -1)
					tgt->move_login = 1;
				spin_unlock_irqrestore(vhost->host->host_lock, flags);
				if (rport)
					fc_remote_port_delete(rport);
				return;
			}
		}

		if (vhost->state == IBMVFC_INITIALIZING) {
			if (vhost->action == IBMVFC_HOST_ACTION_TGT_DEL_FAILED) {
				if (vhost->reinit) {
					vhost->reinit = 0;
					scsi_block_requests(vhost->host);
					ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_QUERY);
					spin_unlock_irqrestore(vhost->host->host_lock, flags);
				} else {
					ibmvfc_set_host_state(vhost, IBMVFC_ACTIVE);
					ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_NONE);
					wake_up(&vhost->init_wait_q);
					schedule_work(&vhost->rport_add_work_q);
					vhost->init_retries = 0;
					spin_unlock_irqrestore(vhost->host->host_lock, flags);
					scsi_unblock_requests(vhost->host);
				}

				return;
			} else {
				ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_INIT);
				vhost->job_step = ibmvfc_discover_targets;
			}
		} else {
			ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_NONE);
			spin_unlock_irqrestore(vhost->host->host_lock, flags);
			scsi_unblock_requests(vhost->host);
			wake_up(&vhost->init_wait_q);
			return;
		}
		break;
	case IBMVFC_HOST_ACTION_ALLOC_TGTS:
		ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_TGT_INIT);
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		ibmvfc_alloc_targets(vhost);
		spin_lock_irqsave(vhost->host->host_lock, flags);
		break;
	case IBMVFC_HOST_ACTION_TGT_INIT:
		list_for_each_entry(tgt, &vhost->targets, queue) {
			if (tgt->action == IBMVFC_TGT_ACTION_INIT) {
				tgt->job_step(tgt);
				break;
			}
		}

		if (!ibmvfc_dev_init_to_do(vhost))
			ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_TGT_DEL_FAILED);
		break;
	default:
		break;
	}

	spin_unlock_irqrestore(vhost->host->host_lock, flags);
}

/**
 * ibmvfc_work - Do task level work
 * @data:		ibmvfc host struct
 *
 * Returns:
 *	zero
 **/
static int ibmvfc_work(void *data)
{
	struct ibmvfc_host *vhost = data;
	int rc;

	set_user_nice(current, MIN_NICE);

	while (1) {
		rc = wait_event_interruptible(vhost->work_wait_q,
					      ibmvfc_work_to_do(vhost));

		BUG_ON(rc);

		if (kthread_should_stop())
			break;

		ibmvfc_do_work(vhost);
	}

	ibmvfc_dbg(vhost, "ibmvfc kthread exiting...\n");
	return 0;
}

/**
 * ibmvfc_alloc_queue - Allocate queue
 * @vhost:	ibmvfc host struct
 * @queue:	ibmvfc queue to allocate
 * @fmt:	queue format to allocate
 *
 * Returns:
 *	0 on success / non-zero on failure
 **/
static int ibmvfc_alloc_queue(struct ibmvfc_host *vhost,
			      struct ibmvfc_queue *queue,
			      enum ibmvfc_msg_fmt fmt)
{
	struct device *dev = vhost->dev;
	size_t fmt_size;
	unsigned int pool_size = 0;

	ENTER;
	spin_lock_init(&queue->_lock);
	queue->q_lock = &queue->_lock;

	switch (fmt) {
	case IBMVFC_CRQ_FMT:
		fmt_size = sizeof(*queue->msgs.crq);
		pool_size = max_requests + IBMVFC_NUM_INTERNAL_REQ;
		break;
	case IBMVFC_ASYNC_FMT:
		fmt_size = sizeof(*queue->msgs.async);
		break;
	case IBMVFC_SUB_CRQ_FMT:
		fmt_size = sizeof(*queue->msgs.scrq);
		/* We need one extra event for Cancel Commands */
		pool_size = max_requests + 1;
		break;
	default:
		dev_warn(dev, "Unknown command/response queue message format: %d\n", fmt);
		return -EINVAL;
	}

	if (ibmvfc_init_event_pool(vhost, queue, pool_size)) {
		dev_err(dev, "Couldn't initialize event pool.\n");
		return -ENOMEM;
	}

	queue->msgs.handle = (void *)get_zeroed_page(GFP_KERNEL);
	if (!queue->msgs.handle)
		return -ENOMEM;

	queue->msg_token = dma_map_single(dev, queue->msgs.handle, PAGE_SIZE,
					  DMA_BIDIRECTIONAL);

	if (dma_mapping_error(dev, queue->msg_token)) {
		free_page((unsigned long)queue->msgs.handle);
		queue->msgs.handle = NULL;
		return -ENOMEM;
	}

	queue->cur = 0;
	queue->fmt = fmt;
	queue->size = PAGE_SIZE / fmt_size;

	queue->vhost = vhost;
	return 0;
}

/**
 * ibmvfc_init_crq - Initializes and registers CRQ with hypervisor
 * @vhost:	ibmvfc host struct
 *
 * Allocates a page for messages, maps it for dma, and registers
 * the crq with the hypervisor.
 *
 * Return value:
 *	zero on success / other on failure
 **/
static int ibmvfc_init_crq(struct ibmvfc_host *vhost)
{
	int rc, retrc = -ENOMEM;
	struct device *dev = vhost->dev;
	struct vio_dev *vdev = to_vio_dev(dev);
	struct ibmvfc_queue *crq = &vhost->crq;

	ENTER;
	if (ibmvfc_alloc_queue(vhost, crq, IBMVFC_CRQ_FMT))
		return -ENOMEM;

	retrc = rc = plpar_hcall_norets(H_REG_CRQ, vdev->unit_address,
					crq->msg_token, PAGE_SIZE);

	if (rc == H_RESOURCE)
		/* maybe kexecing and resource is busy. try a reset */
		retrc = rc = ibmvfc_reset_crq(vhost);

	if (rc == H_CLOSED)
		dev_warn(dev, "Partner adapter not ready\n");
	else if (rc) {
		dev_warn(dev, "Error %d opening adapter\n", rc);
		goto reg_crq_failed;
	}

	retrc = 0;

	tasklet_init(&vhost->tasklet, (void *)ibmvfc_tasklet, (unsigned long)vhost);

	if ((rc = request_irq(vdev->irq, ibmvfc_interrupt, 0, IBMVFC_NAME, vhost))) {
		dev_err(dev, "Couldn't register irq 0x%x. rc=%d\n", vdev->irq, rc);
		goto req_irq_failed;
	}

	if ((rc = vio_enable_interrupts(vdev))) {
		dev_err(dev, "Error %d enabling interrupts\n", rc);
		goto req_irq_failed;
	}

	LEAVE;
	return retrc;

req_irq_failed:
	tasklet_kill(&vhost->tasklet);
	do {
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));
reg_crq_failed:
	ibmvfc_free_queue(vhost, crq);
	return retrc;
}

static int ibmvfc_register_scsi_channel(struct ibmvfc_host *vhost,
				  int index)
{
	struct device *dev = vhost->dev;
	struct vio_dev *vdev = to_vio_dev(dev);
	struct ibmvfc_queue *scrq = &vhost->scsi_scrqs.scrqs[index];
	int rc = -ENOMEM;

	ENTER;

	if (ibmvfc_alloc_queue(vhost, scrq, IBMVFC_SUB_CRQ_FMT))
		return -ENOMEM;

	rc = h_reg_sub_crq(vdev->unit_address, scrq->msg_token, PAGE_SIZE,
			   &scrq->cookie, &scrq->hw_irq);

	/* H_CLOSED indicates successful register, but no CRQ partner */
	if (rc && rc != H_CLOSED) {
		dev_warn(dev, "Error registering sub-crq: %d\n", rc);
		if (rc == H_PARAMETER)
			dev_warn_once(dev, "Firmware may not support MQ\n");
		goto reg_failed;
	}

	scrq->irq = irq_create_mapping(NULL, scrq->hw_irq);

	if (!scrq->irq) {
		rc = -EINVAL;
		dev_err(dev, "Error mapping sub-crq[%d] irq\n", index);
		goto irq_failed;
	}

	snprintf(scrq->name, sizeof(scrq->name), "ibmvfc-%x-scsi%d",
		 vdev->unit_address, index);
	rc = request_irq(scrq->irq, ibmvfc_interrupt_scsi, 0, scrq->name, scrq);

	if (rc) {
		dev_err(dev, "Couldn't register sub-crq[%d] irq\n", index);
		irq_dispose_mapping(scrq->irq);
		goto irq_failed;
	}

	scrq->hwq_id = index;

	LEAVE;
	return 0;

irq_failed:
	do {
		rc = plpar_hcall_norets(H_FREE_SUB_CRQ, vdev->unit_address, scrq->cookie);
	} while (rtas_busy_delay(rc));
reg_failed:
	ibmvfc_free_queue(vhost, scrq);
	LEAVE;
	return rc;
}

static void ibmvfc_deregister_scsi_channel(struct ibmvfc_host *vhost, int index)
{
	struct device *dev = vhost->dev;
	struct vio_dev *vdev = to_vio_dev(dev);
	struct ibmvfc_queue *scrq = &vhost->scsi_scrqs.scrqs[index];
	long rc;

	ENTER;

	free_irq(scrq->irq, scrq);
	irq_dispose_mapping(scrq->irq);
	scrq->irq = 0;

	do {
		rc = plpar_hcall_norets(H_FREE_SUB_CRQ, vdev->unit_address,
					scrq->cookie);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

	if (rc)
		dev_err(dev, "Failed to free sub-crq[%d]: rc=%ld\n", index, rc);

	ibmvfc_free_queue(vhost, scrq);
	LEAVE;
}

static void ibmvfc_init_sub_crqs(struct ibmvfc_host *vhost)
{
	int i, j;

	ENTER;
	if (!vhost->mq_enabled)
		return;

	vhost->scsi_scrqs.scrqs = kcalloc(nr_scsi_hw_queues,
					  sizeof(*vhost->scsi_scrqs.scrqs),
					  GFP_KERNEL);
	if (!vhost->scsi_scrqs.scrqs) {
		vhost->do_enquiry = 0;
		return;
	}

	for (i = 0; i < nr_scsi_hw_queues; i++) {
		if (ibmvfc_register_scsi_channel(vhost, i)) {
			for (j = i; j > 0; j--)
				ibmvfc_deregister_scsi_channel(vhost, j - 1);
			kfree(vhost->scsi_scrqs.scrqs);
			vhost->scsi_scrqs.scrqs = NULL;
			vhost->scsi_scrqs.active_queues = 0;
			vhost->do_enquiry = 0;
			break;
		}
	}

	LEAVE;
}

static void ibmvfc_release_sub_crqs(struct ibmvfc_host *vhost)
{
	int i;

	ENTER;
	if (!vhost->scsi_scrqs.scrqs)
		return;

	for (i = 0; i < nr_scsi_hw_queues; i++)
		ibmvfc_deregister_scsi_channel(vhost, i);

	kfree(vhost->scsi_scrqs.scrqs);
	vhost->scsi_scrqs.scrqs = NULL;
	vhost->scsi_scrqs.active_queues = 0;
	LEAVE;
}

/**
 * ibmvfc_free_mem - Free memory for vhost
 * @vhost:	ibmvfc host struct
 *
 * Return value:
 * 	none
 **/
static void ibmvfc_free_mem(struct ibmvfc_host *vhost)
{
	struct ibmvfc_queue *async_q = &vhost->async_crq;

	ENTER;
	mempool_destroy(vhost->tgt_pool);
	kfree(vhost->trace);
	dma_free_coherent(vhost->dev, vhost->disc_buf_sz, vhost->disc_buf,
			  vhost->disc_buf_dma);
	dma_free_coherent(vhost->dev, sizeof(*vhost->login_buf),
			  vhost->login_buf, vhost->login_buf_dma);
	dma_free_coherent(vhost->dev, sizeof(*vhost->channel_setup_buf),
			  vhost->channel_setup_buf, vhost->channel_setup_dma);
	dma_pool_destroy(vhost->sg_pool);
	ibmvfc_free_queue(vhost, async_q);
	LEAVE;
}

/**
 * ibmvfc_alloc_mem - Allocate memory for vhost
 * @vhost:	ibmvfc host struct
 *
 * Return value:
 * 	0 on success / non-zero on failure
 **/
static int ibmvfc_alloc_mem(struct ibmvfc_host *vhost)
{
	struct ibmvfc_queue *async_q = &vhost->async_crq;
	struct device *dev = vhost->dev;

	ENTER;
	if (ibmvfc_alloc_queue(vhost, async_q, IBMVFC_ASYNC_FMT)) {
		dev_err(dev, "Couldn't allocate/map async queue.\n");
		goto nomem;
	}

	vhost->sg_pool = dma_pool_create(IBMVFC_NAME, dev,
					 SG_ALL * sizeof(struct srp_direct_buf),
					 sizeof(struct srp_direct_buf), 0);

	if (!vhost->sg_pool) {
		dev_err(dev, "Failed to allocate sg pool\n");
		goto unmap_async_crq;
	}

	vhost->login_buf = dma_alloc_coherent(dev, sizeof(*vhost->login_buf),
					      &vhost->login_buf_dma, GFP_KERNEL);

	if (!vhost->login_buf) {
		dev_err(dev, "Couldn't allocate NPIV login buffer\n");
		goto free_sg_pool;
	}

	vhost->disc_buf_sz = sizeof(*vhost->disc_buf) * max_targets;
	vhost->disc_buf = dma_alloc_coherent(dev, vhost->disc_buf_sz,
					     &vhost->disc_buf_dma, GFP_KERNEL);

	if (!vhost->disc_buf) {
		dev_err(dev, "Couldn't allocate Discover Targets buffer\n");
		goto free_login_buffer;
	}

	vhost->trace = kcalloc(IBMVFC_NUM_TRACE_ENTRIES,
			       sizeof(struct ibmvfc_trace_entry), GFP_KERNEL);
	atomic_set(&vhost->trace_index, -1);

	if (!vhost->trace)
		goto free_disc_buffer;

	vhost->tgt_pool = mempool_create_kmalloc_pool(IBMVFC_TGT_MEMPOOL_SZ,
						      sizeof(struct ibmvfc_target));

	if (!vhost->tgt_pool) {
		dev_err(dev, "Couldn't allocate target memory pool\n");
		goto free_trace;
	}

	vhost->channel_setup_buf = dma_alloc_coherent(dev, sizeof(*vhost->channel_setup_buf),
						      &vhost->channel_setup_dma,
						      GFP_KERNEL);

	if (!vhost->channel_setup_buf) {
		dev_err(dev, "Couldn't allocate Channel Setup buffer\n");
		goto free_tgt_pool;
	}

	LEAVE;
	return 0;

free_tgt_pool:
	mempool_destroy(vhost->tgt_pool);
free_trace:
	kfree(vhost->trace);
free_disc_buffer:
	dma_free_coherent(dev, vhost->disc_buf_sz, vhost->disc_buf,
			  vhost->disc_buf_dma);
free_login_buffer:
	dma_free_coherent(dev, sizeof(*vhost->login_buf),
			  vhost->login_buf, vhost->login_buf_dma);
free_sg_pool:
	dma_pool_destroy(vhost->sg_pool);
unmap_async_crq:
	ibmvfc_free_queue(vhost, async_q);
nomem:
	LEAVE;
	return -ENOMEM;
}

/**
 * ibmvfc_rport_add_thread - Worker thread for rport adds
 * @work:	work struct
 *
 **/
static void ibmvfc_rport_add_thread(struct work_struct *work)
{
	struct ibmvfc_host *vhost = container_of(work, struct ibmvfc_host,
						 rport_add_work_q);
	struct ibmvfc_target *tgt;
	struct fc_rport *rport;
	unsigned long flags;
	int did_work;

	ENTER;
	spin_lock_irqsave(vhost->host->host_lock, flags);
	do {
		did_work = 0;
		if (vhost->state != IBMVFC_ACTIVE)
			break;

		list_for_each_entry(tgt, &vhost->targets, queue) {
			if (tgt->add_rport) {
				did_work = 1;
				tgt->add_rport = 0;
				kref_get(&tgt->kref);
				rport = tgt->rport;
				if (!rport) {
					spin_unlock_irqrestore(vhost->host->host_lock, flags);
					ibmvfc_tgt_add_rport(tgt);
				} else if (get_device(&rport->dev)) {
					spin_unlock_irqrestore(vhost->host->host_lock, flags);
					tgt_dbg(tgt, "Setting rport roles\n");
					fc_remote_port_rolechg(rport, tgt->ids.roles);
					put_device(&rport->dev);
				} else {
					spin_unlock_irqrestore(vhost->host->host_lock, flags);
				}

				kref_put(&tgt->kref, ibmvfc_release_tgt);
				spin_lock_irqsave(vhost->host->host_lock, flags);
				break;
			}
		}
	} while(did_work);

	if (vhost->state == IBMVFC_ACTIVE)
		vhost->scan_complete = 1;
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	LEAVE;
}

/**
 * ibmvfc_probe - Adapter hot plug add entry point
 * @vdev:	vio device struct
 * @id:	vio device id struct
 *
 * Return value:
 * 	0 on success / non-zero on failure
 **/
static int ibmvfc_probe(struct vio_dev *vdev, const struct vio_device_id *id)
{
	struct ibmvfc_host *vhost;
	struct Scsi_Host *shost;
	struct device *dev = &vdev->dev;
	int rc = -ENOMEM;
	unsigned int max_scsi_queues = IBMVFC_MAX_SCSI_QUEUES;

	ENTER;
	shost = scsi_host_alloc(&driver_template, sizeof(*vhost));
	if (!shost) {
		dev_err(dev, "Couldn't allocate host data\n");
		goto out;
	}

	shost->transportt = ibmvfc_transport_template;
	shost->can_queue = max_requests;
	shost->max_lun = max_lun;
	shost->max_id = max_targets;
	shost->max_sectors = IBMVFC_MAX_SECTORS;
	shost->max_cmd_len = IBMVFC_MAX_CDB_LEN;
	shost->unique_id = shost->host_no;
	shost->nr_hw_queues = mq_enabled ? min(max_scsi_queues, nr_scsi_hw_queues) : 1;

	vhost = shost_priv(shost);
	INIT_LIST_HEAD(&vhost->targets);
	INIT_LIST_HEAD(&vhost->purge);
	sprintf(vhost->name, IBMVFC_NAME);
	vhost->host = shost;
	vhost->dev = dev;
	vhost->partition_number = -1;
	vhost->log_level = log_level;
	vhost->task_set = 1;

	vhost->mq_enabled = mq_enabled;
	vhost->client_scsi_channels = min(shost->nr_hw_queues, nr_scsi_channels);
	vhost->using_channels = 0;
	vhost->do_enquiry = 1;
	vhost->scan_timeout = 0;

	strcpy(vhost->partition_name, "UNKNOWN");
	init_waitqueue_head(&vhost->work_wait_q);
	init_waitqueue_head(&vhost->init_wait_q);
	INIT_WORK(&vhost->rport_add_work_q, ibmvfc_rport_add_thread);
	mutex_init(&vhost->passthru_mutex);

	if ((rc = ibmvfc_alloc_mem(vhost)))
		goto free_scsi_host;

	vhost->work_thread = kthread_run(ibmvfc_work, vhost, "%s_%d", IBMVFC_NAME,
					 shost->host_no);

	if (IS_ERR(vhost->work_thread)) {
		dev_err(dev, "Couldn't create kernel thread: %ld\n",
			PTR_ERR(vhost->work_thread));
		rc = PTR_ERR(vhost->work_thread);
		goto free_host_mem;
	}

	if ((rc = ibmvfc_init_crq(vhost))) {
		dev_err(dev, "Couldn't initialize crq. rc=%d\n", rc);
		goto kill_kthread;
	}

	if ((rc = scsi_add_host(shost, dev)))
		goto release_crq;

	fc_host_dev_loss_tmo(shost) = IBMVFC_DEV_LOSS_TMO;

	if ((rc = ibmvfc_create_trace_file(&shost->shost_dev.kobj,
					   &ibmvfc_trace_attr))) {
		dev_err(dev, "Failed to create trace file. rc=%d\n", rc);
		goto remove_shost;
	}

	ibmvfc_init_sub_crqs(vhost);

	if (shost_to_fc_host(shost)->rqst_q)
		blk_queue_max_segments(shost_to_fc_host(shost)->rqst_q, 1);
	dev_set_drvdata(dev, vhost);
	spin_lock(&ibmvfc_driver_lock);
	list_add_tail(&vhost->queue, &ibmvfc_head);
	spin_unlock(&ibmvfc_driver_lock);

	ibmvfc_send_crq_init(vhost);
	scsi_scan_host(shost);
	return 0;

remove_shost:
	scsi_remove_host(shost);
release_crq:
	ibmvfc_release_crq_queue(vhost);
kill_kthread:
	kthread_stop(vhost->work_thread);
free_host_mem:
	ibmvfc_free_mem(vhost);
free_scsi_host:
	scsi_host_put(shost);
out:
	LEAVE;
	return rc;
}

/**
 * ibmvfc_remove - Adapter hot plug remove entry point
 * @vdev:	vio device struct
 *
 * Return value:
 * 	0
 **/
static void ibmvfc_remove(struct vio_dev *vdev)
{
	struct ibmvfc_host *vhost = dev_get_drvdata(&vdev->dev);
	LIST_HEAD(purge);
	unsigned long flags;

	ENTER;
	ibmvfc_remove_trace_file(&vhost->host->shost_dev.kobj, &ibmvfc_trace_attr);

	spin_lock_irqsave(vhost->host->host_lock, flags);
	ibmvfc_link_down(vhost, IBMVFC_HOST_OFFLINE);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	ibmvfc_wait_while_resetting(vhost);
	kthread_stop(vhost->work_thread);
	fc_remove_host(vhost->host);
	scsi_remove_host(vhost->host);

	spin_lock_irqsave(vhost->host->host_lock, flags);
	ibmvfc_purge_requests(vhost, DID_ERROR);
	list_splice_init(&vhost->purge, &purge);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	ibmvfc_complete_purge(&purge);
	ibmvfc_release_sub_crqs(vhost);
	ibmvfc_release_crq_queue(vhost);

	ibmvfc_free_mem(vhost);
	spin_lock(&ibmvfc_driver_lock);
	list_del(&vhost->queue);
	spin_unlock(&ibmvfc_driver_lock);
	scsi_host_put(vhost->host);
	LEAVE;
}

/**
 * ibmvfc_resume - Resume from suspend
 * @dev:	device struct
 *
 * We may have lost an interrupt across suspend/resume, so kick the
 * interrupt handler
 *
 */
static int ibmvfc_resume(struct device *dev)
{
	unsigned long flags;
	struct ibmvfc_host *vhost = dev_get_drvdata(dev);
	struct vio_dev *vdev = to_vio_dev(dev);

	spin_lock_irqsave(vhost->host->host_lock, flags);
	vio_disable_interrupts(vdev);
	tasklet_schedule(&vhost->tasklet);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	return 0;
}

/**
 * ibmvfc_get_desired_dma - Calculate DMA resources needed by the driver
 * @vdev:	vio device struct
 *
 * Return value:
 *	Number of bytes the driver will need to DMA map at the same time in
 *	order to perform well.
 */
static unsigned long ibmvfc_get_desired_dma(struct vio_dev *vdev)
{
	unsigned long pool_dma = max_requests * sizeof(union ibmvfc_iu);
	return pool_dma + ((512 * 1024) * driver_template.cmd_per_lun);
}

static const struct vio_device_id ibmvfc_device_table[] = {
	{"fcp", "IBM,vfc-client"},
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, ibmvfc_device_table);

static const struct dev_pm_ops ibmvfc_pm_ops = {
	.resume = ibmvfc_resume
};

static struct vio_driver ibmvfc_driver = {
	.id_table = ibmvfc_device_table,
	.probe = ibmvfc_probe,
	.remove = ibmvfc_remove,
	.get_desired_dma = ibmvfc_get_desired_dma,
	.name = IBMVFC_NAME,
	.pm = &ibmvfc_pm_ops,
};

static struct fc_function_template ibmvfc_transport_functions = {
	.show_host_fabric_name = 1,
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_port_type = 1,
	.show_host_port_id = 1,
	.show_host_maxframe_size = 1,

	.get_host_port_state = ibmvfc_get_host_port_state,
	.show_host_port_state = 1,

	.get_host_speed = ibmvfc_get_host_speed,
	.show_host_speed = 1,

	.issue_fc_host_lip = ibmvfc_issue_fc_host_lip,
	.terminate_rport_io = ibmvfc_terminate_rport_io,

	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,

	.set_rport_dev_loss_tmo = ibmvfc_set_rport_dev_loss_tmo,
	.show_rport_dev_loss_tmo = 1,

	.get_starget_node_name = ibmvfc_get_starget_node_name,
	.show_starget_node_name = 1,

	.get_starget_port_name = ibmvfc_get_starget_port_name,
	.show_starget_port_name = 1,

	.get_starget_port_id = ibmvfc_get_starget_port_id,
	.show_starget_port_id = 1,

	.bsg_request = ibmvfc_bsg_request,
	.bsg_timeout = ibmvfc_bsg_timeout,
};

/**
 * ibmvfc_module_init - Initialize the ibmvfc module
 *
 * Return value:
 * 	0 on success / other on failure
 **/
static int __init ibmvfc_module_init(void)
{
	int rc;

	if (!firmware_has_feature(FW_FEATURE_VIO))
		return -ENODEV;

	printk(KERN_INFO IBMVFC_NAME": IBM Virtual Fibre Channel Driver version: %s %s\n",
	       IBMVFC_DRIVER_VERSION, IBMVFC_DRIVER_DATE);

	ibmvfc_transport_template = fc_attach_transport(&ibmvfc_transport_functions);
	if (!ibmvfc_transport_template)
		return -ENOMEM;

	rc = vio_register_driver(&ibmvfc_driver);
	if (rc)
		fc_release_transport(ibmvfc_transport_template);
	return rc;
}

/**
 * ibmvfc_module_exit - Teardown the ibmvfc module
 *
 * Return value:
 * 	nothing
 **/
static void __exit ibmvfc_module_exit(void)
{
	vio_unregister_driver(&ibmvfc_driver);
	fc_release_transport(ibmvfc_transport_template);
}

module_init(ibmvfc_module_init);
module_exit(ibmvfc_module_exit);
