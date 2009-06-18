/*
 * ibmvfc.c -- driver for IBM Power Virtual Fibre Channel Adapter
 *
 * Written By: Brian King <brking@linux.vnet.ibm.com>, IBM Corporation
 *
 * Copyright (C) IBM Corporation, 2008
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kthread.h>
#include <linux/of.h>
#include <linux/stringify.h>
#include <asm/firmware.h>
#include <asm/irq.h>
#include <asm/vio.h>
#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_transport_fc.h>
#include "ibmvfc.h"

static unsigned int init_timeout = IBMVFC_INIT_TIMEOUT;
static unsigned int default_timeout = IBMVFC_DEFAULT_TIMEOUT;
static unsigned int max_lun = IBMVFC_MAX_LUN;
static unsigned int max_targets = IBMVFC_MAX_TARGETS;
static unsigned int max_requests = IBMVFC_MAX_REQUESTS_DEFAULT;
static unsigned int disc_threads = IBMVFC_MAX_DISC_THREADS;
static unsigned int dev_loss_tmo = IBMVFC_DEV_LOSS_TMO;
static unsigned int ibmvfc_debug = IBMVFC_DEBUG;
static unsigned int log_level = IBMVFC_DEFAULT_LOG_LEVEL;
static LIST_HEAD(ibmvfc_head);
static DEFINE_SPINLOCK(ibmvfc_driver_lock);
static struct scsi_transport_template *ibmvfc_transport_template;

MODULE_DESCRIPTION("IBM Virtual Fibre Channel Driver");
MODULE_AUTHOR("Brian King <brking@linux.vnet.ibm.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(IBMVFC_DRIVER_VERSION);

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
module_param_named(max_lun, max_lun, uint, S_IRUGO);
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
module_param_named(dev_loss_tmo, dev_loss_tmo, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dev_loss_tmo, "Maximum number of seconds that the FC "
		 "transport should insulate the loss of a remote port. Once this "
		 "value is exceeded, the scsi target is removed. "
		 "[Default=" __stringify(IBMVFC_DEV_LOSS_TMO) "]");
module_param_named(log_level, log_level, uint, 0);
MODULE_PARM_DESC(log_level, "Set to 0 - 4 for increasing verbosity of device driver. "
		 "[Default=" __stringify(IBMVFC_DEFAULT_LOG_LEVEL) "]");

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
};

static void ibmvfc_npiv_login(struct ibmvfc_host *);
static void ibmvfc_tgt_send_prli(struct ibmvfc_target *);
static void ibmvfc_tgt_send_plogi(struct ibmvfc_target *);
static void ibmvfc_tgt_query_target(struct ibmvfc_target *);
static void ibmvfc_npiv_logout(struct ibmvfc_host *);

static const char *unknown_error = "unknown error";

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
	struct ibmvfc_trace_entry *entry;

	entry = &vhost->trace[vhost->trace_index++];
	entry->evt = evt;
	entry->time = jiffies;
	entry->fmt = evt->crq.format;
	entry->type = IBMVFC_TRC_START;

	switch (entry->fmt) {
	case IBMVFC_CMD_FORMAT:
		entry->op_code = vfc_cmd->iu.cdb[0];
		entry->scsi_id = vfc_cmd->tgt_scsi_id;
		entry->lun = scsilun_to_int(&vfc_cmd->iu.lun);
		entry->tmf_flags = vfc_cmd->iu.tmf_flags;
		entry->u.start.xfer_len = vfc_cmd->iu.xfer_len;
		break;
	case IBMVFC_MAD_FORMAT:
		entry->op_code = mad->opcode;
		break;
	default:
		break;
	};
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
	struct ibmvfc_trace_entry *entry = &vhost->trace[vhost->trace_index++];

	entry->evt = evt;
	entry->time = jiffies;
	entry->fmt = evt->crq.format;
	entry->type = IBMVFC_TRC_END;

	switch (entry->fmt) {
	case IBMVFC_CMD_FORMAT:
		entry->op_code = vfc_cmd->iu.cdb[0];
		entry->scsi_id = vfc_cmd->tgt_scsi_id;
		entry->lun = scsilun_to_int(&vfc_cmd->iu.lun);
		entry->tmf_flags = vfc_cmd->iu.tmf_flags;
		entry->u.end.status = vfc_cmd->status;
		entry->u.end.error = vfc_cmd->error;
		entry->u.end.fcp_rsp_flags = vfc_cmd->rsp.flags;
		entry->u.end.rsp_code = vfc_cmd->rsp.data.info.rsp_code;
		entry->u.end.scsi_status = vfc_cmd->rsp.scsi_status;
		break;
	case IBMVFC_MAD_FORMAT:
		entry->op_code = mad->opcode;
		entry->u.end.status = mad->status;
		break;
	default:
		break;

	};
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
 * @vfc_cmd:	ibmvfc command struct
 *
 * Return value:
 *	SCSI result value to return for completed command
 **/
static int ibmvfc_get_err_result(struct ibmvfc_cmd *vfc_cmd)
{
	int err;
	struct ibmvfc_fcp_rsp *rsp = &vfc_cmd->rsp;
	int fc_rsp_len = rsp->fcp_rsp_len;

	if ((rsp->flags & FCP_RSP_LEN_VALID) &&
	    ((fc_rsp_len && fc_rsp_len != 4 && fc_rsp_len != 8) ||
	     rsp->data.info.rsp_code))
		return DID_ERROR << 16;

	err = ibmvfc_get_err_index(vfc_cmd->status, vfc_cmd->error);
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
 **/
static void ibmvfc_set_tgt_action(struct ibmvfc_target *tgt,
				  enum ibmvfc_target_action action)
{
	switch (tgt->action) {
	case IBMVFC_TGT_ACTION_DEL_RPORT:
		break;
	default:
		if (action == IBMVFC_TGT_ACTION_DEL_RPORT)
			tgt->add_rport = 0;
		tgt->action = action;
		break;
	}
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
	};

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
		};
		break;
	case IBMVFC_HOST_ACTION_TGT_INIT:
		if (vhost->action == IBMVFC_HOST_ACTION_ALLOC_TGTS)
			vhost->action = action;
		break;
	case IBMVFC_HOST_ACTION_LOGO:
	case IBMVFC_HOST_ACTION_INIT:
	case IBMVFC_HOST_ACTION_TGT_DEL:
	case IBMVFC_HOST_ACTION_QUERY_TGTS:
	case IBMVFC_HOST_ACTION_TGT_DEL_FAILED:
	case IBMVFC_HOST_ACTION_NONE:
	default:
		vhost->action = action;
		break;
	};
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
	if (vhost->action == IBMVFC_HOST_ACTION_NONE) {
		if (!ibmvfc_set_host_state(vhost, IBMVFC_INITIALIZING)) {
			scsi_block_requests(vhost->host);
			ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_QUERY);
		}
	} else
		vhost->reinit = 1;

	wake_up(&vhost->work_wait_q);
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
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
	ibmvfc_set_host_state(vhost, state);
	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_TGT_DEL);
	vhost->events_to_log |= IBMVFC_AE_LINKDOWN;
	wake_up(&vhost->work_wait_q);
	LEAVE;
}

/**
 * ibmvfc_init_host - Start host initialization
 * @vhost:		ibmvfc host struct
 * @relogin:	is this a re-login?
 *
 * Return value:
 *	nothing
 **/
static void ibmvfc_init_host(struct ibmvfc_host *vhost, int relogin)
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
		if (!relogin) {
			memset(vhost->async_crq.msgs, 0, PAGE_SIZE);
			vhost->async_crq.cur = 0;
		}

		list_for_each_entry(tgt, &vhost->targets, queue)
			ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
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
 * ibmvfc_release_crq_queue - Deallocates data and unregisters CRQ
 * @vhost:	ibmvfc host struct
 *
 * Frees irq, deallocates a page for messages, unmaps dma, and unregisters
 * the crq with the hypervisor.
 **/
static void ibmvfc_release_crq_queue(struct ibmvfc_host *vhost)
{
	long rc;
	struct vio_dev *vdev = to_vio_dev(vhost->dev);
	struct ibmvfc_crq_queue *crq = &vhost->crq;

	ibmvfc_dbg(vhost, "Releasing CRQ\n");
	free_irq(vdev->irq, vhost);
	tasklet_kill(&vhost->tasklet);
	do {
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

	vhost->state = IBMVFC_NO_CRQ;
	vhost->logged_in = 0;
	dma_unmap_single(vhost->dev, crq->msg_token, PAGE_SIZE, DMA_BIDIRECTIONAL);
	free_page((unsigned long)crq->msgs);
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
	int rc;
	struct vio_dev *vdev = to_vio_dev(vhost->dev);

	/* Re-enable the CRQ */
	do {
		rc = plpar_hcall_norets(H_ENABLE_CRQ, vdev->unit_address);
	} while (rc == H_IN_PROGRESS || rc == H_BUSY || H_IS_LONG_BUSY(rc));

	if (rc)
		dev_err(vhost->dev, "Error enabling adapter (rc=%d)\n", rc);

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
	int rc;
	struct vio_dev *vdev = to_vio_dev(vhost->dev);
	struct ibmvfc_crq_queue *crq = &vhost->crq;

	/* Close the CRQ */
	do {
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));

	vhost->state = IBMVFC_NO_CRQ;
	vhost->logged_in = 0;
	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_NONE);

	/* Clean out the queue */
	memset(crq->msgs, 0, PAGE_SIZE);
	crq->cur = 0;

	/* And re-open it again */
	rc = plpar_hcall_norets(H_REG_CRQ, vdev->unit_address,
				crq->msg_token, PAGE_SIZE);

	if (rc == H_CLOSED)
		/* Adapter is good, but other end is not ready */
		dev_warn(vhost->dev, "Partner adapter not ready\n");
	else if (rc != 0)
		dev_warn(vhost->dev, "Couldn't register crq (rc=%d)\n", rc);

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
	struct ibmvfc_host *vhost = evt->vhost;
	struct ibmvfc_event_pool *pool = &vhost->pool;

	BUG_ON(!ibmvfc_valid_event(pool, evt));
	BUG_ON(atomic_inc_return(&evt->free) != 1);
	list_add_tail(&evt->queue, &vhost->free);
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

	if (evt->eh_comp)
		complete(evt->eh_comp);

	ibmvfc_free_event(evt);
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
	if (evt->cmnd) {
		evt->cmnd->result = (error_code << 16);
		evt->done = ibmvfc_scsi_eh_done;
	} else
		evt->xfer_iu->mad_common.status = IBMVFC_MAD_DRIVER_FAILED;

	list_del(&evt->queue);
	del_timer(&evt->timer);
	ibmvfc_trc_end(evt);
	evt->done(evt);
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

	ibmvfc_dbg(vhost, "Purging all requests\n");
	list_for_each_entry_safe(evt, pos, &vhost->sent, queue)
		ibmvfc_fail_request(evt, error_code);
}

/**
 * ibmvfc_hard_reset_host - Reset the connection to the server by breaking the CRQ
 * @vhost:	struct ibmvfc host to reset
 **/
static void ibmvfc_hard_reset_host(struct ibmvfc_host *vhost)
{
	int rc;

	scsi_block_requests(vhost->host);
	ibmvfc_purge_requests(vhost, DID_ERROR);
	if ((rc = ibmvfc_reset_crq(vhost)) ||
	    (rc = ibmvfc_send_crq_init(vhost)) ||
	    (rc = vio_enable_interrupts(to_vio_dev(vhost->dev)))) {
		dev_err(vhost->dev, "Error after reset rc=%d\n", rc);
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
	} else
		ibmvfc_link_down(vhost, IBMVFC_LINK_DOWN);
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
		switch (vhost->login_buf->resp.link_speed / 100) {
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
				   vhost->login_buf->resp.link_speed / 100);
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
	struct device_node *of_node = vhost->dev->archdata.of_node;
	const char *location;

	memset(login_info, 0, sizeof(*login_info));

	login_info->ostype = IBMVFC_OS_LINUX;
	login_info->max_dma_len = IBMVFC_MAX_SECTORS << 9;
	login_info->max_payload = sizeof(struct ibmvfc_fcp_cmd_iu);
	login_info->max_response = sizeof(struct ibmvfc_fcp_rsp);
	login_info->partition_num = vhost->partition_number;
	login_info->vfc_frame_version = 1;
	login_info->fcp_version = 3;
	login_info->flags = IBMVFC_FLUSH_ON_HALT;
	if (vhost->client_migrated)
		login_info->flags |= IBMVFC_CLIENT_MIGRATED;

	login_info->max_cmds = max_requests + IBMVFC_NUM_INTERNAL_REQ;
	login_info->capabilities = IBMVFC_CAN_MIGRATE;
	login_info->async.va = vhost->async_crq.msg_token;
	login_info->async.len = vhost->async_crq.size * sizeof(*vhost->async_crq.msgs);
	strncpy(login_info->partition_name, vhost->partition_name, IBMVFC_MAX_NAME);
	strncpy(login_info->device_name,
		dev_name(&vhost->host->shost_gendev), IBMVFC_MAX_NAME);

	location = of_get_property(of_node, "ibm,loc-code", NULL);
	location = location ? location : dev_name(vhost->dev);
	strncpy(login_info->drc_name, location, IBMVFC_MAX_NAME);
}

/**
 * ibmvfc_init_event_pool - Allocates and initializes the event pool for a host
 * @vhost:	ibmvfc host who owns the event pool
 *
 * Returns zero on success.
 **/
static int ibmvfc_init_event_pool(struct ibmvfc_host *vhost)
{
	int i;
	struct ibmvfc_event_pool *pool = &vhost->pool;

	ENTER;
	pool->size = max_requests + IBMVFC_NUM_INTERNAL_REQ;
	pool->events = kcalloc(pool->size, sizeof(*pool->events), GFP_KERNEL);
	if (!pool->events)
		return -ENOMEM;

	pool->iu_storage = dma_alloc_coherent(vhost->dev,
					      pool->size * sizeof(*pool->iu_storage),
					      &pool->iu_token, 0);

	if (!pool->iu_storage) {
		kfree(pool->events);
		return -ENOMEM;
	}

	for (i = 0; i < pool->size; ++i) {
		struct ibmvfc_event *evt = &pool->events[i];
		atomic_set(&evt->free, 1);
		evt->crq.valid = 0x80;
		evt->crq.ioba = pool->iu_token + (sizeof(*evt->xfer_iu) * i);
		evt->xfer_iu = pool->iu_storage + i;
		evt->vhost = vhost;
		evt->ext_list = NULL;
		list_add_tail(&evt->queue, &vhost->free);
	}

	LEAVE;
	return 0;
}

/**
 * ibmvfc_free_event_pool - Frees memory of the event pool of a host
 * @vhost:	ibmvfc host who owns the event pool
 *
 **/
static void ibmvfc_free_event_pool(struct ibmvfc_host *vhost)
{
	int i;
	struct ibmvfc_event_pool *pool = &vhost->pool;

	ENTER;
	for (i = 0; i < pool->size; ++i) {
		list_del(&pool->events[i].queue);
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
 * ibmvfc_get_event - Gets the next free event in pool
 * @vhost:	ibmvfc host struct
 *
 * Returns a free event from the pool.
 **/
static struct ibmvfc_event *ibmvfc_get_event(struct ibmvfc_host *vhost)
{
	struct ibmvfc_event *evt;

	BUG_ON(list_empty(&vhost->free));
	evt = list_entry(vhost->free.next, struct ibmvfc_event, queue);
	atomic_set(&evt->free, 0);
	list_del(&evt->queue);
	return evt;
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
	evt->crq.format = format;
	evt->done = done;
	evt->eh_comp = NULL;
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
		md[i].va = sg_dma_address(sg);
		md[i].len = sg_dma_len(sg);
		md[i].key = 0;
	}
}

/**
 * ibmvfc_map_sg_data - Maps dma for a scatterlist and initializes decriptor fields
 * @scmd:		Scsi_Cmnd with the scatterlist
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

	sg_mapped = scsi_dma_map(scmd);
	if (!sg_mapped) {
		vfc_cmd->flags |= IBMVFC_NO_MEM_DESC;
		return 0;
	} else if (unlikely(sg_mapped < 0)) {
		if (vhost->log_level > IBMVFC_DEFAULT_LOG_LEVEL)
			scmd_printk(KERN_ERR, scmd, "Failed to map DMA buffer for command\n");
		return sg_mapped;
	}

	if (scmd->sc_data_direction == DMA_TO_DEVICE) {
		vfc_cmd->flags |= IBMVFC_WRITE;
		vfc_cmd->iu.add_cdb_len |= IBMVFC_WRDATA;
	} else {
		vfc_cmd->flags |= IBMVFC_READ;
		vfc_cmd->iu.add_cdb_len |= IBMVFC_RDDATA;
	}

	if (sg_mapped == 1) {
		ibmvfc_map_sg_list(scmd, sg_mapped, data);
		return 0;
	}

	vfc_cmd->flags |= IBMVFC_SCATTERLIST;

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

	data->va = evt->ext_list_token;
	data->len = sg_mapped * sizeof(struct srp_direct_buf);
	data->key = 0;
	return 0;
}

/**
 * ibmvfc_timeout - Internal command timeout handler
 * @evt:	struct ibmvfc_event that timed out
 *
 * Called when an internally generated command times out
 **/
static void ibmvfc_timeout(struct ibmvfc_event *evt)
{
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
	u64 *crq_as_u64 = (u64 *) &evt->crq;
	int rc;

	/* Copy the IU into the transfer area */
	*evt->xfer_iu = evt->iu;
	if (evt->crq.format == IBMVFC_CMD_FORMAT)
		evt->xfer_iu->cmd.tag = (u64)evt;
	else if (evt->crq.format == IBMVFC_MAD_FORMAT)
		evt->xfer_iu->mad_common.tag = (u64)evt;
	else
		BUG();

	list_add_tail(&evt->queue, &vhost->sent);
	init_timer(&evt->timer);

	if (timeout) {
		evt->timer.data = (unsigned long) evt;
		evt->timer.expires = jiffies + (timeout * HZ);
		evt->timer.function = (void (*)(unsigned long))ibmvfc_timeout;
		add_timer(&evt->timer);
	}

	mb();

	if ((rc = ibmvfc_send_crq(vhost, crq_as_u64[0], crq_as_u64[1]))) {
		list_del(&evt->queue);
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
			evt->xfer_iu->mad_common.status = IBMVFC_MAD_CRQ_ERROR;

		evt->done(evt);
	} else
		ibmvfc_trc_start(evt);

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
	struct ibmvfc_fcp_rsp *rsp = &vfc_cmd->rsp;
	struct scsi_cmnd *cmnd = evt->cmnd;
	const char *err = unknown_error;
	int index = ibmvfc_get_err_index(vfc_cmd->status, vfc_cmd->error);
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

	scmd_printk(KERN_ERR, cmnd, "Command (%02X) failed: %s (%x:%x) "
		    "flags: %x fcp_rsp: %x, resid=%d, scsi_status: %x\n",
		    cmnd->cmnd[0], err, vfc_cmd->status, vfc_cmd->error,
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

	list_for_each_entry(tgt, &vhost->targets, queue) {
		if (rport == tgt->rport) {
			ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
			break;
		}
	}

	ibmvfc_reinit_host(vhost);
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
	struct ibmvfc_fcp_rsp *rsp = &vfc_cmd->rsp;
	struct scsi_cmnd *cmnd = evt->cmnd;
	u32 rsp_len = 0;
	u32 sense_len = rsp->fcp_sense_len;

	if (cmnd) {
		if (vfc_cmd->response_flags & IBMVFC_ADAPTER_RESID_VALID)
			scsi_set_resid(cmnd, vfc_cmd->adapter_resid);
		else if (rsp->flags & FCP_RESID_UNDER)
			scsi_set_resid(cmnd, rsp->fcp_resid);
		else
			scsi_set_resid(cmnd, 0);

		if (vfc_cmd->status) {
			cmnd->result = ibmvfc_get_err_result(vfc_cmd);

			if (rsp->flags & FCP_RSP_LEN_VALID)
				rsp_len = rsp->fcp_rsp_len;
			if ((sense_len + rsp_len) > SCSI_SENSE_BUFFERSIZE)
				sense_len = SCSI_SENSE_BUFFERSIZE - rsp_len;
			if ((rsp->flags & FCP_SNS_LEN_VALID) && rsp->fcp_sense_len && rsp_len <= 8)
				memcpy(cmnd->sense_buffer, rsp->data.sense + rsp_len, sense_len);
			if ((vfc_cmd->status & IBMVFC_VIOS_FAILURE) && (vfc_cmd->error == IBMVFC_PLOGI_REQUIRED))
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

	if (evt->eh_comp)
		complete(evt->eh_comp);

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
	};

	return result;
}

/**
 * ibmvfc_queuecommand - The queuecommand function of the scsi template
 * @cmnd:	struct scsi_cmnd to be executed
 * @done:	Callback function to be called when cmnd is completed
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_queuecommand(struct scsi_cmnd *cmnd,
			       void (*done) (struct scsi_cmnd *))
{
	struct ibmvfc_host *vhost = shost_priv(cmnd->device->host);
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));
	struct ibmvfc_cmd *vfc_cmd;
	struct ibmvfc_event *evt;
	u8 tag[2];
	int rc;

	if (unlikely((rc = fc_remote_port_chkready(rport))) ||
	    unlikely((rc = ibmvfc_host_chkready(vhost)))) {
		cmnd->result = rc;
		done(cmnd);
		return 0;
	}

	cmnd->result = (DID_OK << 16);
	evt = ibmvfc_get_event(vhost);
	ibmvfc_init_event(evt, ibmvfc_scsi_done, IBMVFC_CMD_FORMAT);
	evt->cmnd = cmnd;
	cmnd->scsi_done = done;
	vfc_cmd = &evt->iu.cmd;
	memset(vfc_cmd, 0, sizeof(*vfc_cmd));
	vfc_cmd->resp.va = (u64)evt->crq.ioba + offsetof(struct ibmvfc_cmd, rsp);
	vfc_cmd->resp.len = sizeof(vfc_cmd->rsp);
	vfc_cmd->frame_type = IBMVFC_SCSI_FCP_TYPE;
	vfc_cmd->payload_len = sizeof(vfc_cmd->iu);
	vfc_cmd->resp_len = sizeof(vfc_cmd->rsp);
	vfc_cmd->cancel_key = (unsigned long)cmnd->device->hostdata;
	vfc_cmd->tgt_scsi_id = rport->port_id;
	vfc_cmd->iu.xfer_len = scsi_bufflen(cmnd);
	int_to_scsilun(cmnd->device->lun, &vfc_cmd->iu.lun);
	memcpy(vfc_cmd->iu.cdb, cmnd->cmnd, cmnd->cmd_len);

	if (scsi_populate_tag_msg(cmnd, tag)) {
		vfc_cmd->task_tag = tag[1];
		switch (tag[0]) {
		case MSG_SIMPLE_TAG:
			vfc_cmd->iu.pri_task_attr = IBMVFC_SIMPLE_TASK;
			break;
		case MSG_HEAD_TAG:
			vfc_cmd->iu.pri_task_attr = IBMVFC_HEAD_OF_QUEUE;
			break;
		case MSG_ORDERED_TAG:
			vfc_cmd->iu.pri_task_attr = IBMVFC_ORDERED_TASK;
			break;
		};
	}

	if (likely(!(rc = ibmvfc_map_sg_data(cmnd, evt, vfc_cmd, vhost->dev))))
		return ibmvfc_send_event(evt, vhost, 0);

	ibmvfc_free_event(evt);
	if (rc == -ENOMEM)
		return SCSI_MLQUEUE_HOST_BUSY;

	if (vhost->log_level > IBMVFC_DEFAULT_LOG_LEVEL)
		scmd_printk(KERN_ERR, cmnd,
			    "Failed to map DMA buffer for command. rc=%d\n", rc);

	cmnd->result = DID_ERROR << 16;
	done(cmnd);
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
	struct ibmvfc_fcp_rsp *fc_rsp = &rsp_iu.cmd.rsp;
	int rsp_rc = -EBUSY;
	unsigned long flags;
	int rsp_code = 0;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	if (vhost->state == IBMVFC_ACTIVE) {
		evt = ibmvfc_get_event(vhost);
		ibmvfc_init_event(evt, ibmvfc_sync_completion, IBMVFC_CMD_FORMAT);

		tmf = &evt->iu.cmd;
		memset(tmf, 0, sizeof(*tmf));
		tmf->resp.va = (u64)evt->crq.ioba + offsetof(struct ibmvfc_cmd, rsp);
		tmf->resp.len = sizeof(tmf->rsp);
		tmf->frame_type = IBMVFC_SCSI_FCP_TYPE;
		tmf->payload_len = sizeof(tmf->iu);
		tmf->resp_len = sizeof(tmf->rsp);
		tmf->cancel_key = (unsigned long)sdev->hostdata;
		tmf->tgt_scsi_id = rport->port_id;
		int_to_scsilun(sdev->lun, &tmf->iu.lun);
		tmf->flags = (IBMVFC_NO_MEM_DESC | IBMVFC_TMF);
		tmf->iu.tmf_flags = type;
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

	if (rsp_iu.cmd.status) {
		if (fc_rsp->flags & FCP_RSP_LEN_VALID)
			rsp_code = fc_rsp->data.info.rsp_code;

		sdev_printk(KERN_ERR, sdev, "%s reset failed: %s (%x:%x) "
			    "flags: %x fcp_rsp: %x, scsi_status: %x\n",
			    desc, ibmvfc_get_cmd_error(rsp_iu.cmd.status, rsp_iu.cmd.error),
			    rsp_iu.cmd.status, rsp_iu.cmd.error, fc_rsp->flags, rsp_code,
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
	struct ibmvfc_fcp_rsp *fc_rsp = &rsp_iu.cmd.rsp;
	int rsp_rc = -EBUSY;
	unsigned long flags;
	int rsp_code = 0;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	found_evt = NULL;
	list_for_each_entry(evt, &vhost->sent, queue) {
		if (evt->cmnd && evt->cmnd->device == sdev) {
			found_evt = evt;
			break;
		}
	}

	if (!found_evt) {
		if (vhost->log_level > IBMVFC_DEFAULT_LOG_LEVEL)
			sdev_printk(KERN_INFO, sdev, "No events found to abort\n");
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		return 0;
	}

	if (vhost->state == IBMVFC_ACTIVE) {
		evt = ibmvfc_get_event(vhost);
		ibmvfc_init_event(evt, ibmvfc_sync_completion, IBMVFC_CMD_FORMAT);

		tmf = &evt->iu.cmd;
		memset(tmf, 0, sizeof(*tmf));
		tmf->resp.va = (u64)evt->crq.ioba + offsetof(struct ibmvfc_cmd, rsp);
		tmf->resp.len = sizeof(tmf->rsp);
		tmf->frame_type = IBMVFC_SCSI_FCP_TYPE;
		tmf->payload_len = sizeof(tmf->iu);
		tmf->resp_len = sizeof(tmf->rsp);
		tmf->cancel_key = (unsigned long)sdev->hostdata;
		tmf->tgt_scsi_id = rport->port_id;
		int_to_scsilun(sdev->lun, &tmf->iu.lun);
		tmf->flags = (IBMVFC_NO_MEM_DESC | IBMVFC_TMF);
		tmf->iu.tmf_flags = IBMVFC_ABORT_TASK_SET;
		evt->sync_iu = &rsp_iu;

		init_completion(&evt->comp);
		rsp_rc = ibmvfc_send_event(evt, vhost, default_timeout);
	}

	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	if (rsp_rc != 0) {
		sdev_printk(KERN_ERR, sdev, "Failed to send abort. rc=%d\n", rsp_rc);
		return -EIO;
	}

	sdev_printk(KERN_INFO, sdev, "Aborting outstanding commands\n");
	wait_for_completion(&evt->comp);

	if (rsp_iu.cmd.status) {
		if (fc_rsp->flags & FCP_RSP_LEN_VALID)
			rsp_code = fc_rsp->data.info.rsp_code;

		sdev_printk(KERN_ERR, sdev, "Abort failed: %s (%x:%x) "
			    "flags: %x fcp_rsp: %x, scsi_status: %x\n",
			    ibmvfc_get_cmd_error(rsp_iu.cmd.status, rsp_iu.cmd.error),
			    rsp_iu.cmd.status, rsp_iu.cmd.error, fc_rsp->flags, rsp_code,
			    fc_rsp->scsi_status);
		rsp_rc = -EIO;
	} else
		sdev_printk(KERN_INFO, sdev, "Abort successful\n");

	spin_lock_irqsave(vhost->host->host_lock, flags);
	ibmvfc_free_event(evt);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);
	return rsp_rc;
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
	struct scsi_target *starget = scsi_target(sdev);
	struct fc_rport *rport = starget_to_rport(starget);
	struct ibmvfc_tmf *tmf;
	struct ibmvfc_event *evt, *found_evt;
	union ibmvfc_iu rsp;
	int rsp_rc = -EBUSY;
	unsigned long flags;
	u16 status;

	ENTER;
	spin_lock_irqsave(vhost->host->host_lock, flags);
	found_evt = NULL;
	list_for_each_entry(evt, &vhost->sent, queue) {
		if (evt->cmnd && evt->cmnd->device == sdev) {
			found_evt = evt;
			break;
		}
	}

	if (!found_evt) {
		if (vhost->log_level > IBMVFC_DEFAULT_LOG_LEVEL)
			sdev_printk(KERN_INFO, sdev, "No events found to cancel\n");
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		return 0;
	}

	if (vhost->state == IBMVFC_ACTIVE) {
		evt = ibmvfc_get_event(vhost);
		ibmvfc_init_event(evt, ibmvfc_sync_completion, IBMVFC_MAD_FORMAT);

		tmf = &evt->iu.tmf;
		memset(tmf, 0, sizeof(*tmf));
		tmf->common.version = 1;
		tmf->common.opcode = IBMVFC_TMF_MAD;
		tmf->common.length = sizeof(*tmf);
		tmf->scsi_id = rport->port_id;
		int_to_scsilun(sdev->lun, &tmf->lun);
		tmf->flags = (type | IBMVFC_TMF_LUA_VALID);
		tmf->cancel_key = (unsigned long)sdev->hostdata;
		tmf->my_cancel_key = (unsigned long)starget->hostdata;

		evt->sync_iu = &rsp;
		init_completion(&evt->comp);
		rsp_rc = ibmvfc_send_event(evt, vhost, default_timeout);
	}

	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	if (rsp_rc != 0) {
		sdev_printk(KERN_ERR, sdev, "Failed to send cancel event. rc=%d\n", rsp_rc);
		return -EIO;
	}

	sdev_printk(KERN_INFO, sdev, "Cancelling outstanding commands.\n");

	wait_for_completion(&evt->comp);
	status = rsp.mad_common.status;
	spin_lock_irqsave(vhost->host->host_lock, flags);
	ibmvfc_free_event(evt);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	if (status != IBMVFC_MAD_SUCCESS) {
		sdev_printk(KERN_WARNING, sdev, "Cancel failed with rc=%x\n", status);
		return -EIO;
	}

	sdev_printk(KERN_INFO, sdev, "Successfully cancelled outstanding commands\n");
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
	int wait;
	unsigned long flags;
	signed long timeout = init_timeout * HZ;

	ENTER;
	do {
		wait = 0;
		spin_lock_irqsave(vhost->host->host_lock, flags);
		list_for_each_entry(evt, &vhost->sent, queue) {
			if (match(evt, device)) {
				evt->eh_comp = &comp;
				wait++;
			}
		}
		spin_unlock_irqrestore(vhost->host->host_lock, flags);

		if (wait) {
			timeout = wait_for_completion_timeout(&comp, timeout);

			if (!timeout) {
				wait = 0;
				spin_lock_irqsave(vhost->host->host_lock, flags);
				list_for_each_entry(evt, &vhost->sent, queue) {
					if (match(evt, device)) {
						evt->eh_comp = NULL;
						wait++;
					}
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

/**
 * ibmvfc_eh_abort_handler - Abort a command
 * @cmd:	scsi command to abort
 *
 * Returns:
 *	SUCCESS / FAILED
 **/
static int ibmvfc_eh_abort_handler(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	int cancel_rc, abort_rc;
	int rc = FAILED;

	ENTER;
	ibmvfc_wait_while_resetting(vhost);
	cancel_rc = ibmvfc_cancel_all(sdev, IBMVFC_TMF_ABORT_TASK_SET);
	abort_rc = ibmvfc_abort_task_set(sdev);

	if (!cancel_rc && !abort_rc)
		rc = ibmvfc_wait_for_ops(vhost, sdev, ibmvfc_match_lun);

	LEAVE;
	return rc;
}

/**
 * ibmvfc_eh_device_reset_handler - Reset a single LUN
 * @cmd:	scsi command struct
 *
 * Returns:
 *	SUCCESS / FAILED
 **/
static int ibmvfc_eh_device_reset_handler(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	int cancel_rc, reset_rc;
	int rc = FAILED;

	ENTER;
	ibmvfc_wait_while_resetting(vhost);
	cancel_rc = ibmvfc_cancel_all(sdev, IBMVFC_TMF_LUN_RESET);
	reset_rc = ibmvfc_reset_device(sdev, IBMVFC_LUN_RESET, "LUN");

	if (!cancel_rc && !reset_rc)
		rc = ibmvfc_wait_for_ops(vhost, sdev, ibmvfc_match_lun);

	LEAVE;
	return rc;
}

/**
 * ibmvfc_dev_cancel_all - Device iterated cancel all function
 * @sdev:	scsi device struct
 * @data:	return code
 *
 **/
static void ibmvfc_dev_cancel_all(struct scsi_device *sdev, void *data)
{
	unsigned long *rc = data;
	*rc |= ibmvfc_cancel_all(sdev, IBMVFC_TMF_TGT_RESET);
}

/**
 * ibmvfc_dev_abort_all - Device iterated abort task set function
 * @sdev:	scsi device struct
 * @data:	return code
 *
 **/
static void ibmvfc_dev_abort_all(struct scsi_device *sdev, void *data)
{
	unsigned long *rc = data;
	*rc |= ibmvfc_abort_task_set(sdev);
}

/**
 * ibmvfc_eh_target_reset_handler - Reset the target
 * @cmd:	scsi command struct
 *
 * Returns:
 *	SUCCESS / FAILED
 **/
static int ibmvfc_eh_target_reset_handler(struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct ibmvfc_host *vhost = shost_priv(sdev->host);
	struct scsi_target *starget = scsi_target(sdev);
	int reset_rc;
	int rc = FAILED;
	unsigned long cancel_rc = 0;

	ENTER;
	ibmvfc_wait_while_resetting(vhost);
	starget_for_each_device(starget, &cancel_rc, ibmvfc_dev_cancel_all);
	reset_rc = ibmvfc_reset_device(sdev, IBMVFC_TARGET_RESET, "target");

	if (!cancel_rc && !reset_rc)
		rc = ibmvfc_wait_for_ops(vhost, starget, ibmvfc_match_target);

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
	struct scsi_target *starget = to_scsi_target(&rport->dev);
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct ibmvfc_host *vhost = shost_priv(shost);
	unsigned long cancel_rc = 0;
	unsigned long abort_rc = 0;
	int rc = FAILED;

	ENTER;
	starget_for_each_device(starget, &cancel_rc, ibmvfc_dev_cancel_all);
	starget_for_each_device(starget, &abort_rc, ibmvfc_dev_abort_all);

	if (!cancel_rc && !abort_rc)
		rc = ibmvfc_wait_for_ops(vhost, starget, ibmvfc_match_target);

	if (rc == FAILED)
		ibmvfc_issue_fc_host_lip(shost);
	LEAVE;
}

static const struct {
	enum ibmvfc_async_event ae;
	const char *desc;
} ae_desc [] = {
	{ IBMVFC_AE_ELS_PLOGI,		"PLOGI" },
	{ IBMVFC_AE_ELS_LOGO,		"LOGO" },
	{ IBMVFC_AE_ELS_PRLO,		"PRLO" },
	{ IBMVFC_AE_SCN_NPORT,		"N-Port SCN" },
	{ IBMVFC_AE_SCN_GROUP,		"Group SCN" },
	{ IBMVFC_AE_SCN_DOMAIN,		"Domain SCN" },
	{ IBMVFC_AE_SCN_FABRIC,		"Fabric SCN" },
	{ IBMVFC_AE_LINK_UP,		"Link Up" },
	{ IBMVFC_AE_LINK_DOWN,		"Link Down" },
	{ IBMVFC_AE_LINK_DEAD,		"Link Dead" },
	{ IBMVFC_AE_HALT,			"Halt" },
	{ IBMVFC_AE_RESUME,		"Resume" },
	{ IBMVFC_AE_ADAPTER_FAILED,	"Adapter Failed" },
};

static const char *unknown_ae = "Unknown async";

/**
 * ibmvfc_get_ae_desc - Get text description for async event
 * @ae:	async event
 *
 **/
static const char *ibmvfc_get_ae_desc(u64 ae)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ae_desc); i++)
		if (ae_desc[i].ae == ae)
			return ae_desc[i].desc;

	return unknown_ae;
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
	const char *desc = ibmvfc_get_ae_desc(crq->event);
	struct ibmvfc_target *tgt;

	ibmvfc_log(vhost, 3, "%s event received. scsi_id: %llx, wwpn: %llx,"
		   " node_name: %llx\n", desc, crq->scsi_id, crq->wwpn, crq->node_name);

	switch (crq->event) {
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
		};

		break;
	case IBMVFC_AE_LINK_UP:
		vhost->events_to_log |= IBMVFC_AE_LINKUP;
		vhost->delay_init = 1;
		__ibmvfc_reset_host(vhost);
		break;
	case IBMVFC_AE_SCN_FABRIC:
	case IBMVFC_AE_SCN_DOMAIN:
		vhost->events_to_log |= IBMVFC_AE_RSCN;
		vhost->delay_init = 1;
		__ibmvfc_reset_host(vhost);
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
			if (crq->scsi_id && tgt->scsi_id != crq->scsi_id)
				continue;
			if (crq->wwpn && tgt->ids.port_name != crq->wwpn)
				continue;
			if (crq->node_name && tgt->ids.node_name != crq->node_name)
				continue;
			if (tgt->need_login && crq->event == IBMVFC_AE_ELS_LOGO)
				tgt->logo_rcvd = 1;
			if (!tgt->need_login || crq->event == IBMVFC_AE_ELS_PLOGI) {
				ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
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
	};
}

/**
 * ibmvfc_handle_crq - Handles and frees received events in the CRQ
 * @crq:	Command/Response queue
 * @vhost:	ibmvfc host struct
 *
 **/
static void ibmvfc_handle_crq(struct ibmvfc_crq *crq, struct ibmvfc_host *vhost)
{
	long rc;
	struct ibmvfc_event *evt = (struct ibmvfc_event *)crq->ioba;

	switch (crq->valid) {
	case IBMVFC_CRQ_INIT_RSP:
		switch (crq->format) {
		case IBMVFC_CRQ_INIT:
			dev_info(vhost->dev, "Partner initialized\n");
			/* Send back a response */
			rc = ibmvfc_send_crq_init_complete(vhost);
			if (rc == 0)
				ibmvfc_init_host(vhost, 0);
			else
				dev_err(vhost->dev, "Unable to send init rsp. rc=%ld\n", rc);
			break;
		case IBMVFC_CRQ_INIT_COMPLETE:
			dev_info(vhost->dev, "Partner initialization complete\n");
			ibmvfc_init_host(vhost, 0);
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
			dev_info(vhost->dev, "Re-enabling adapter\n");
			vhost->client_migrated = 1;
			ibmvfc_purge_requests(vhost, DID_REQUEUE);
			if ((rc = ibmvfc_reenable_crq_queue(vhost)) ||
			    (rc = ibmvfc_send_crq_init(vhost))) {
				ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
				dev_err(vhost->dev, "Error after enable (rc=%ld)\n", rc);
			} else
				ibmvfc_link_down(vhost, IBMVFC_LINK_DOWN);
		} else {
			dev_err(vhost->dev, "Virtual adapter failed (rc=%d)\n", crq->format);

			ibmvfc_purge_requests(vhost, DID_ERROR);
			if ((rc = ibmvfc_reset_crq(vhost)) ||
			    (rc = ibmvfc_send_crq_init(vhost))) {
				ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
				dev_err(vhost->dev, "Error after reset (rc=%ld)\n", rc);
			} else
				ibmvfc_link_down(vhost, IBMVFC_LINK_DOWN);
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
	if (unlikely(!ibmvfc_valid_event(&vhost->pool, evt))) {
		dev_err(vhost->dev, "Returned correlation_token 0x%08llx is invalid!\n",
			crq->ioba);
		return;
	}

	if (unlikely(atomic_read(&evt->free))) {
		dev_err(vhost->dev, "Received duplicate correlation_token 0x%08llx!\n",
			crq->ioba);
		return;
	}

	del_timer(&evt->timer);
	list_del(&evt->queue);
	ibmvfc_trc_end(evt);
	evt->done(evt);
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
	if (time >= (init_timeout * HZ)) {
		dev_info(vhost->dev, "Scan taking longer than %d seconds, "
			 "continuing initialization\n", init_timeout);
		done = 1;
	}

	if (vhost->scan_complete)
		done = 1;
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
	struct fc_rport *rport = starget_to_rport(sdev->sdev_target);
	unsigned long flags = 0;

	spin_lock_irqsave(shost->host_lock, flags);
	if (sdev->type == TYPE_DISK)
		sdev->allow_restart = 1;

	if (sdev->tagged_supported) {
		scsi_set_tag_type(sdev, MSG_SIMPLE_TAG);
		scsi_activate_tcq(sdev, sdev->queue_depth);
	} else
		scsi_deactivate_tcq(sdev, sdev->queue_depth);

	rport->dev_loss_tmo = dev_loss_tmo;
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

	scsi_adjust_queue_depth(sdev, 0, qdepth);
	return sdev->queue_depth;
}

/**
 * ibmvfc_change_queue_type - Change the device's queue type
 * @sdev:		scsi device struct
 * @tag_type:	type of tags to use
 *
 * Return value:
 * 	actual queue type set
 **/
static int ibmvfc_change_queue_type(struct scsi_device *sdev, int tag_type)
{
	if (sdev->tagged_supported) {
		scsi_set_tag_type(sdev, tag_type);

		if (tag_type)
			scsi_activate_tcq(sdev, sdev->queue_depth);
		else
			scsi_deactivate_tcq(sdev, sdev->queue_depth);
	} else
		tag_type = 0;

	return tag_type;
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
	return snprintf(buf, PAGE_SIZE, "%d\n", vhost->login_buf->resp.version);
}

static ssize_t ibmvfc_show_host_capabilities(struct device *dev,
					     struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct ibmvfc_host *vhost = shost_priv(shost);
	return snprintf(buf, PAGE_SIZE, "%llx\n", vhost->login_buf->resp.capabilities);
}

/**
 * ibmvfc_show_log_level - Show the adapter's error logging level
 * @dev:	class device struct
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
 * @buf:	buffer
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

static DEVICE_ATTR(partition_name, S_IRUGO, ibmvfc_show_host_partition_name, NULL);
static DEVICE_ATTR(device_name, S_IRUGO, ibmvfc_show_host_device_name, NULL);
static DEVICE_ATTR(port_loc_code, S_IRUGO, ibmvfc_show_host_loc_code, NULL);
static DEVICE_ATTR(drc_name, S_IRUGO, ibmvfc_show_host_drc_name, NULL);
static DEVICE_ATTR(npiv_version, S_IRUGO, ibmvfc_show_host_npiv_version, NULL);
static DEVICE_ATTR(capabilities, S_IRUGO, ibmvfc_show_host_capabilities, NULL);
static DEVICE_ATTR(log_level, S_IRUGO | S_IWUSR,
		   ibmvfc_show_log_level, ibmvfc_store_log_level);

#ifdef CONFIG_SCSI_IBMVFC_TRACE
/**
 * ibmvfc_read_trace - Dump the adapter trace
 * @kobj:		kobject struct
 * @bin_attr:	bin_attribute struct
 * @buf:		buffer
 * @off:		offset
 * @count:		buffer size
 *
 * Return value:
 *	number of bytes printed to buffer
 **/
static ssize_t ibmvfc_read_trace(struct kobject *kobj,
				 struct bin_attribute *bin_attr,
				 char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
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
	NULL
};

static struct scsi_host_template driver_template = {
	.module = THIS_MODULE,
	.name = "IBM POWER Virtual FC Adapter",
	.proc_name = IBMVFC_NAME,
	.queuecommand = ibmvfc_queuecommand,
	.eh_abort_handler = ibmvfc_eh_abort_handler,
	.eh_device_reset_handler = ibmvfc_eh_device_reset_handler,
	.eh_target_reset_handler = ibmvfc_eh_target_reset_handler,
	.eh_host_reset_handler = ibmvfc_eh_host_reset_handler,
	.slave_alloc = ibmvfc_slave_alloc,
	.slave_configure = ibmvfc_slave_configure,
	.target_alloc = ibmvfc_target_alloc,
	.scan_finished = ibmvfc_scan_finished,
	.change_queue_depth = ibmvfc_change_queue_depth,
	.change_queue_type = ibmvfc_change_queue_type,
	.cmd_per_lun = 16,
	.can_queue = IBMVFC_MAX_REQUESTS_DEFAULT,
	.this_id = -1,
	.sg_tablesize = SG_ALL,
	.max_sectors = IBMVFC_MAX_SECTORS,
	.use_clustering = ENABLE_CLUSTERING,
	.shost_attrs = ibmvfc_attrs,
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
	struct ibmvfc_async_crq_queue *async_crq = &vhost->async_crq;
	struct ibmvfc_async_crq *crq;

	crq = &async_crq->msgs[async_crq->cur];
	if (crq->valid & 0x80) {
		if (++async_crq->cur == async_crq->size)
			async_crq->cur = 0;
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
	struct ibmvfc_crq_queue *queue = &vhost->crq;
	struct ibmvfc_crq *crq;

	crq = &queue->msgs[queue->cur];
	if (crq->valid & 0x80) {
		if (++queue->cur == queue->size)
			queue->cur = 0;
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
	unsigned long flags;
	int done = 0;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	while (!done) {
		/* Pull all the valid messages off the async CRQ */
		while ((async = ibmvfc_next_async_crq(vhost)) != NULL) {
			ibmvfc_handle_async(async, vhost);
			async->valid = 0;
		}

		/* Pull all the valid messages off the CRQ */
		while ((crq = ibmvfc_next_crq(vhost)) != NULL) {
			ibmvfc_handle_crq(crq, vhost);
			crq->valid = 0;
		}

		vio_enable_interrupts(vdev);
		if ((async = ibmvfc_next_async_crq(vhost)) != NULL) {
			vio_disable_interrupts(vdev);
			ibmvfc_handle_async(async, vhost);
			async->valid = 0;
		} else if ((crq = ibmvfc_next_crq(vhost)) != NULL) {
			vio_disable_interrupts(vdev);
			ibmvfc_handle_crq(crq, vhost);
			crq->valid = 0;
		} else
			done = 1;
	}

	spin_unlock_irqrestore(vhost->host->host_lock, flags);
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
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_INIT);
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
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
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
	u32 status = rsp->common.status;
	int index, level = IBMVFC_DEFAULT_LOG_LEVEL;

	vhost->discovery_threads--;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
	switch (status) {
	case IBMVFC_MAD_SUCCESS:
		tgt_dbg(tgt, "Process Login succeeded: %X %02X %04X\n",
			parms->type, parms->flags, parms->service_parms);

		if (parms->type == IBMVFC_SCSI_FCP_TYPE) {
			index = ibmvfc_get_prli_rsp(parms->flags);
			if (prli_rsp[index].logged_in) {
				if (parms->flags & IBMVFC_PRLI_EST_IMG_PAIR) {
					tgt->need_login = 0;
					tgt->ids.roles = 0;
					if (parms->service_parms & IBMVFC_PRLI_TARGET_FUNC)
						tgt->ids.roles |= FC_PORT_ROLE_FCP_TARGET;
					if (parms->service_parms & IBMVFC_PRLI_INITIATOR_FUNC)
						tgt->ids.roles |= FC_PORT_ROLE_FCP_INITIATOR;
					tgt->add_rport = 1;
				} else
					ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
			} else if (prli_rsp[index].retry)
				ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_prli);
			else
				ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
		} else
			ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
		break;
	case IBMVFC_MAD_DRIVER_FAILED:
		break;
	case IBMVFC_MAD_CRQ_ERROR:
		ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_prli);
		break;
	case IBMVFC_MAD_FAILED:
	default:
		if ((rsp->status & IBMVFC_VIOS_FAILURE) && rsp->error == IBMVFC_PLOGI_REQUIRED)
			level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_plogi);
		else if (tgt->logo_rcvd)
			level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_plogi);
		else if (ibmvfc_retry_cmd(rsp->status, rsp->error))
			level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_prli);
		else
			ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);

		tgt_log(tgt, level, "Process Login failed: %s (%x:%x) rc=0x%02X\n",
			ibmvfc_get_cmd_error(rsp->status, rsp->error),
			rsp->status, rsp->error, status);
		break;
	};

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
	evt = ibmvfc_get_event(vhost);
	vhost->discovery_threads++;
	ibmvfc_init_event(evt, ibmvfc_tgt_prli_done, IBMVFC_MAD_FORMAT);
	evt->tgt = tgt;
	prli = &evt->iu.prli;
	memset(prli, 0, sizeof(*prli));
	prli->common.version = 1;
	prli->common.opcode = IBMVFC_PROCESS_LOGIN;
	prli->common.length = sizeof(*prli);
	prli->scsi_id = tgt->scsi_id;

	prli->parms.type = IBMVFC_SCSI_FCP_TYPE;
	prli->parms.flags = IBMVFC_PRLI_EST_IMG_PAIR;
	prli->parms.service_parms = IBMVFC_PRLI_INITIATOR_FUNC;

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
	u32 status = rsp->common.status;
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
		if (ibmvfc_retry_cmd(rsp->status, rsp->error))
			level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_send_plogi);
		else
			ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);

		tgt_log(tgt, level, "Port Login failed: %s (%x:%x) %s (%x) %s (%x) rc=0x%02X\n",
			ibmvfc_get_cmd_error(rsp->status, rsp->error), rsp->status, rsp->error,
			ibmvfc_get_fc_type(rsp->fc_type), rsp->fc_type,
			ibmvfc_get_ls_explain(rsp->fc_explain), rsp->fc_explain, status);
		break;
	};

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
	evt = ibmvfc_get_event(vhost);
	vhost->discovery_threads++;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_INIT_WAIT);
	ibmvfc_init_event(evt, ibmvfc_tgt_plogi_done, IBMVFC_MAD_FORMAT);
	evt->tgt = tgt;
	plogi = &evt->iu.plogi;
	memset(plogi, 0, sizeof(*plogi));
	plogi->common.version = 1;
	plogi->common.opcode = IBMVFC_PORT_LOGIN;
	plogi->common.length = sizeof(*plogi);
	plogi->scsi_id = tgt->scsi_id;

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
	u32 status = rsp->common.status;

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
	};

	if (vhost->action == IBMVFC_HOST_ACTION_TGT_INIT)
		ibmvfc_init_tgt(tgt, ibmvfc_tgt_send_plogi);
	else if (vhost->action == IBMVFC_HOST_ACTION_QUERY_TGTS &&
		 tgt->scsi_id != tgt->new_scsi_id)
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
	kref_put(&tgt->kref, ibmvfc_release_tgt);
	wake_up(&vhost->work_wait_q);
}

/**
 * ibmvfc_tgt_implicit_logout - Initiate an Implicit Logout for specified target
 * @tgt:		ibmvfc target struct
 *
 **/
static void ibmvfc_tgt_implicit_logout(struct ibmvfc_target *tgt)
{
	struct ibmvfc_implicit_logout *mad;
	struct ibmvfc_host *vhost = tgt->vhost;
	struct ibmvfc_event *evt;

	if (vhost->discovery_threads >= disc_threads)
		return;

	kref_get(&tgt->kref);
	evt = ibmvfc_get_event(vhost);
	vhost->discovery_threads++;
	ibmvfc_init_event(evt, ibmvfc_tgt_implicit_logout_done, IBMVFC_MAD_FORMAT);
	evt->tgt = tgt;
	mad = &evt->iu.implicit_logout;
	memset(mad, 0, sizeof(*mad));
	mad->common.version = 1;
	mad->common.opcode = IBMVFC_IMPLICIT_LOGOUT;
	mad->common.length = sizeof(*mad);
	mad->old_scsi_id = tgt->scsi_id;

	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_INIT_WAIT);
	if (ibmvfc_send_event(evt, vhost, default_timeout)) {
		vhost->discovery_threads--;
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
		kref_put(&tgt->kref, ibmvfc_release_tgt);
	} else
		tgt_dbg(tgt, "Sent Implicit Logout\n");
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
	if (memcmp(&mad->fc_iu.response[2], &tgt->ids.port_name,
		   sizeof(tgt->ids.port_name)))
		return 1;
	if (memcmp(&mad->fc_iu.response[4], &tgt->ids.node_name,
		   sizeof(tgt->ids.node_name)))
		return 1;
	if (mad->fc_iu.response[6] != tgt->scsi_id)
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
	u32 status = mad->common.status;
	u8 fc_reason, fc_explain;

	vhost->discovery_threads--;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
	del_timer(&tgt->timer);

	switch (status) {
	case IBMVFC_MAD_SUCCESS:
		tgt_dbg(tgt, "ADISC succeeded\n");
		if (ibmvfc_adisc_needs_plogi(mad, tgt))
			ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
		break;
	case IBMVFC_MAD_DRIVER_FAILED:
		break;
	case IBMVFC_MAD_FAILED:
	default:
		ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
		fc_reason = (mad->fc_iu.response[1] & 0x00ff0000) >> 16;
		fc_explain = (mad->fc_iu.response[1] & 0x0000ff00) >> 8;
		tgt_info(tgt, "ADISC failed: %s (%x:%x) %s (%x) %s (%x) rc=0x%02X\n",
			 ibmvfc_get_cmd_error(mad->iu.status, mad->iu.error),
			 mad->iu.status, mad->iu.error,
			 ibmvfc_get_fc_type(fc_reason), fc_reason,
			 ibmvfc_get_ls_explain(fc_explain), fc_explain, status);
		break;
	};

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
	mad->common.version = 1;
	mad->common.opcode = IBMVFC_PASSTHRU;
	mad->common.length = sizeof(*mad) - sizeof(mad->fc_iu) - sizeof(mad->iu);
	mad->cmd_ioba.va = (u64)evt->crq.ioba +
		offsetof(struct ibmvfc_passthru_mad, iu);
	mad->cmd_ioba.len = sizeof(mad->iu);
	mad->iu.cmd_len = sizeof(mad->fc_iu.payload);
	mad->iu.rsp_len = sizeof(mad->fc_iu.response);
	mad->iu.cmd.va = (u64)evt->crq.ioba +
		offsetof(struct ibmvfc_passthru_mad, fc_iu) +
		offsetof(struct ibmvfc_passthru_fc_iu, payload);
	mad->iu.cmd.len = sizeof(mad->fc_iu.payload);
	mad->iu.rsp.va = (u64)evt->crq.ioba +
		offsetof(struct ibmvfc_passthru_mad, fc_iu) +
		offsetof(struct ibmvfc_passthru_fc_iu, response);
	mad->iu.rsp.len = sizeof(mad->fc_iu.response);
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
 * @tgt:		ibmvfc target struct
 *
 * If an ADISC times out, send a cancel. If the cancel times
 * out, reset the CRQ. When the ADISC comes back as cancelled,
 * log back into the target.
 **/
static void ibmvfc_adisc_timeout(struct ibmvfc_target *tgt)
{
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
	evt = ibmvfc_get_event(vhost);
	ibmvfc_init_event(evt, ibmvfc_tgt_adisc_cancel_done, IBMVFC_MAD_FORMAT);

	evt->tgt = tgt;
	tmf = &evt->iu.tmf;
	memset(tmf, 0, sizeof(*tmf));
	tmf->common.version = 1;
	tmf->common.opcode = IBMVFC_TMF_MAD;
	tmf->common.length = sizeof(*tmf);
	tmf->scsi_id = tgt->scsi_id;
	tmf->cancel_key = tgt->cancel_key;

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
	evt = ibmvfc_get_event(vhost);
	vhost->discovery_threads++;
	ibmvfc_init_event(evt, ibmvfc_tgt_adisc_done, IBMVFC_MAD_FORMAT);
	evt->tgt = tgt;

	ibmvfc_init_passthru(evt);
	mad = &evt->iu.passthru;
	mad->iu.flags = IBMVFC_FC_ELS;
	mad->iu.scsi_id = tgt->scsi_id;
	mad->iu.cancel_key = tgt->cancel_key;

	mad->fc_iu.payload[0] = IBMVFC_ADISC;
	memcpy(&mad->fc_iu.payload[2], &vhost->login_buf->resp.port_name,
	       sizeof(vhost->login_buf->resp.port_name));
	memcpy(&mad->fc_iu.payload[4], &vhost->login_buf->resp.node_name,
	       sizeof(vhost->login_buf->resp.node_name));
	mad->fc_iu.payload[6] = vhost->login_buf->resp.scsi_id & 0x00ffffff;

	if (timer_pending(&tgt->timer))
		mod_timer(&tgt->timer, jiffies + (IBMVFC_ADISC_TIMEOUT * HZ));
	else {
		tgt->timer.data = (unsigned long) tgt;
		tgt->timer.expires = jiffies + (IBMVFC_ADISC_TIMEOUT * HZ);
		tgt->timer.function = (void (*)(unsigned long))ibmvfc_adisc_timeout;
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
	u32 status = rsp->common.status;
	int level = IBMVFC_DEFAULT_LOG_LEVEL;

	vhost->discovery_threads--;
	ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_NONE);
	switch (status) {
	case IBMVFC_MAD_SUCCESS:
		tgt_dbg(tgt, "Query Target succeeded\n");
		tgt->new_scsi_id = rsp->scsi_id;
		if (rsp->scsi_id != tgt->scsi_id)
			ibmvfc_init_tgt(tgt, ibmvfc_tgt_implicit_logout);
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
		if ((rsp->status & IBMVFC_FABRIC_MAPPED) == IBMVFC_FABRIC_MAPPED &&
		    rsp->error == IBMVFC_UNABLE_TO_PERFORM_REQ &&
		    rsp->fc_explain == IBMVFC_PORT_NAME_NOT_REG)
			ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);
		else if (ibmvfc_retry_cmd(rsp->status, rsp->error))
			level += ibmvfc_retry_tgt_init(tgt, ibmvfc_tgt_query_target);
		else
			ibmvfc_set_tgt_action(tgt, IBMVFC_TGT_ACTION_DEL_RPORT);

		tgt_log(tgt, level, "Query Target failed: %s (%x:%x) %s (%x) %s (%x) rc=0x%02X\n",
			ibmvfc_get_cmd_error(rsp->status, rsp->error), rsp->status, rsp->error,
			ibmvfc_get_fc_type(rsp->fc_type), rsp->fc_type,
			ibmvfc_get_gs_explain(rsp->fc_explain), rsp->fc_explain, status);
		break;
	};

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
	evt = ibmvfc_get_event(vhost);
	vhost->discovery_threads++;
	evt->tgt = tgt;
	ibmvfc_init_event(evt, ibmvfc_tgt_query_target_done, IBMVFC_MAD_FORMAT);
	query_tgt = &evt->iu.query_tgt;
	memset(query_tgt, 0, sizeof(*query_tgt));
	query_tgt->common.version = 1;
	query_tgt->common.opcode = IBMVFC_QUERY_TARGET;
	query_tgt->common.length = sizeof(*query_tgt);
	query_tgt->wwpn = tgt->ids.port_name;

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
 * @scsi_id:	SCSI ID to allocate target for
 *
 * Returns:
 *	0 on success / other on failure
 **/
static int ibmvfc_alloc_target(struct ibmvfc_host *vhost, u64 scsi_id)
{
	struct ibmvfc_target *tgt;
	unsigned long flags;

	spin_lock_irqsave(vhost->host->host_lock, flags);
	list_for_each_entry(tgt, &vhost->targets, queue) {
		if (tgt->scsi_id == scsi_id) {
			if (tgt->need_login)
				ibmvfc_init_tgt(tgt, ibmvfc_tgt_implicit_logout);
			goto unlock_out;
		}
	}
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	tgt = mempool_alloc(vhost->tgt_pool, GFP_NOIO);
	if (!tgt) {
		dev_err(vhost->dev, "Target allocation failure for scsi id %08llx\n",
			scsi_id);
		return -ENOMEM;
	}

	memset(tgt, 0, sizeof(*tgt));
	tgt->scsi_id = scsi_id;
	tgt->new_scsi_id = scsi_id;
	tgt->vhost = vhost;
	tgt->need_login = 1;
	tgt->cancel_key = vhost->task_set++;
	init_timer(&tgt->timer);
	kref_init(&tgt->kref);
	ibmvfc_init_tgt(tgt, ibmvfc_tgt_implicit_logout);
	spin_lock_irqsave(vhost->host->host_lock, flags);
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
		rc = ibmvfc_alloc_target(vhost,
					 vhost->disc_buf->scsi_id[i] & IBMVFC_DISC_TGT_SCSI_ID_MASK);

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
	u32 mad_status = rsp->common.status;
	int level = IBMVFC_DEFAULT_LOG_LEVEL;

	switch (mad_status) {
	case IBMVFC_MAD_SUCCESS:
		ibmvfc_dbg(vhost, "Discover Targets succeeded\n");
		vhost->num_targets = rsp->num_written;
		ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_ALLOC_TGTS);
		break;
	case IBMVFC_MAD_FAILED:
		level += ibmvfc_retry_host_init(vhost);
		ibmvfc_log(vhost, level, "Discover Targets failed: %s (%x:%x)\n",
			   ibmvfc_get_cmd_error(rsp->status, rsp->error), rsp->status, rsp->error);
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
	struct ibmvfc_event *evt = ibmvfc_get_event(vhost);

	ibmvfc_init_event(evt, ibmvfc_discover_targets_done, IBMVFC_MAD_FORMAT);
	mad = &evt->iu.discover_targets;
	memset(mad, 0, sizeof(*mad));
	mad->common.version = 1;
	mad->common.opcode = IBMVFC_DISC_TARGETS;
	mad->common.length = sizeof(*mad);
	mad->bufflen = vhost->disc_buf_sz;
	mad->buffer.va = vhost->disc_buf_dma;
	mad->buffer.len = vhost->disc_buf_sz;
	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_INIT_WAIT);

	if (!ibmvfc_send_event(evt, vhost, default_timeout))
		ibmvfc_dbg(vhost, "Sent discover targets\n");
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
	u32 mad_status = evt->xfer_iu->npiv_login.common.status;
	struct ibmvfc_npiv_login_resp *rsp = &vhost->login_buf->resp;
	unsigned int npiv_max_sectors;
	int level = IBMVFC_DEFAULT_LOG_LEVEL;

	switch (mad_status) {
	case IBMVFC_MAD_SUCCESS:
		ibmvfc_free_event(evt);
		break;
	case IBMVFC_MAD_FAILED:
		if (ibmvfc_retry_cmd(rsp->status, rsp->error))
			level += ibmvfc_retry_host_init(vhost);
		else
			ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		ibmvfc_log(vhost, level, "NPIV Login failed: %s (%x:%x)\n",
			   ibmvfc_get_cmd_error(rsp->status, rsp->error), rsp->status, rsp->error);
		ibmvfc_free_event(evt);
		return;
	case IBMVFC_MAD_CRQ_ERROR:
		ibmvfc_retry_host_init(vhost);
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

	if (!(rsp->flags & IBMVFC_NATIVE_FC)) {
		dev_err(vhost->dev, "Virtual adapter does not support FC. %x\n",
			rsp->flags);
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		wake_up(&vhost->work_wait_q);
		return;
	}

	if (rsp->max_cmds <= IBMVFC_NUM_INTERNAL_REQ) {
		dev_err(vhost->dev, "Virtual adapter supported queue depth too small: %d\n",
			rsp->max_cmds);
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
		wake_up(&vhost->work_wait_q);
		return;
	}

	vhost->logged_in = 1;
	npiv_max_sectors = min((uint)(rsp->max_dma_len >> 9), IBMVFC_MAX_SECTORS);
	dev_info(vhost->dev, "Host partition: %s, device: %s %s %s max sectors %u\n",
		 rsp->partition_name, rsp->device_name, rsp->port_loc_code,
		 rsp->drc_name, npiv_max_sectors);

	fc_host_fabric_name(vhost->host) = rsp->node_name;
	fc_host_node_name(vhost->host) = rsp->node_name;
	fc_host_port_name(vhost->host) = rsp->port_name;
	fc_host_port_id(vhost->host) = rsp->scsi_id;
	fc_host_port_type(vhost->host) = FC_PORTTYPE_NPIV;
	fc_host_supported_classes(vhost->host) = 0;
	if (rsp->service_parms.class1_parms[0] & 0x80000000)
		fc_host_supported_classes(vhost->host) |= FC_COS_CLASS1;
	if (rsp->service_parms.class2_parms[0] & 0x80000000)
		fc_host_supported_classes(vhost->host) |= FC_COS_CLASS2;
	if (rsp->service_parms.class3_parms[0] & 0x80000000)
		fc_host_supported_classes(vhost->host) |= FC_COS_CLASS3;
	fc_host_maxframe_size(vhost->host) =
		rsp->service_parms.common.bb_rcv_sz & 0x0fff;

	vhost->host->can_queue = rsp->max_cmds - IBMVFC_NUM_INTERNAL_REQ;
	vhost->host->max_sectors = npiv_max_sectors;
	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_QUERY);
	wake_up(&vhost->work_wait_q);
}

/**
 * ibmvfc_npiv_login - Sends NPIV login
 * @vhost:	ibmvfc host struct
 *
 **/
static void ibmvfc_npiv_login(struct ibmvfc_host *vhost)
{
	struct ibmvfc_npiv_login_mad *mad;
	struct ibmvfc_event *evt = ibmvfc_get_event(vhost);

	ibmvfc_gather_partition_info(vhost);
	ibmvfc_set_login_info(vhost);
	ibmvfc_init_event(evt, ibmvfc_npiv_login_done, IBMVFC_MAD_FORMAT);

	memcpy(vhost->login_buf, &vhost->login_info, sizeof(vhost->login_info));
	mad = &evt->iu.npiv_login;
	memset(mad, 0, sizeof(struct ibmvfc_npiv_login_mad));
	mad->common.version = 1;
	mad->common.opcode = IBMVFC_NPIV_LOGIN;
	mad->common.length = sizeof(struct ibmvfc_npiv_login_mad);
	mad->buffer.va = vhost->login_buf_dma;
	mad->buffer.len = sizeof(*vhost->login_buf);

	ibmvfc_set_host_action(vhost, IBMVFC_HOST_ACTION_INIT_WAIT);

	if (!ibmvfc_send_event(evt, vhost, default_timeout))
		ibmvfc_dbg(vhost, "Sent NPIV login\n");
	else
		ibmvfc_link_down(vhost, IBMVFC_LINK_DEAD);
};

/**
 * ibmvfc_npiv_logout_done - Completion handler for NPIV Logout
 * @vhost:		ibmvfc host struct
 *
 **/
static void ibmvfc_npiv_logout_done(struct ibmvfc_event *evt)
{
	struct ibmvfc_host *vhost = evt->vhost;
	u32 mad_status = evt->xfer_iu->npiv_logout.common.status;

	ibmvfc_free_event(evt);

	switch (mad_status) {
	case IBMVFC_MAD_SUCCESS:
		if (list_empty(&vhost->sent) &&
		    vhost->action == IBMVFC_HOST_ACTION_LOGO_WAIT) {
			ibmvfc_init_host(vhost, 0);
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

	evt = ibmvfc_get_event(vhost);
	ibmvfc_init_event(evt, ibmvfc_npiv_logout_done, IBMVFC_MAD_FORMAT);

	mad = &evt->iu.npiv_logout;
	memset(mad, 0, sizeof(*mad));
	mad->common.version = 1;
	mad->common.opcode = IBMVFC_NPIV_LOGOUT;
	mad->common.length = sizeof(struct ibmvfc_npiv_logout_mad);

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
	case IBMVFC_HOST_ACTION_LOGO:
	case IBMVFC_HOST_ACTION_INIT:
	case IBMVFC_HOST_ACTION_ALLOC_TGTS:
	case IBMVFC_HOST_ACTION_TGT_DEL:
	case IBMVFC_HOST_ACTION_TGT_DEL_FAILED:
	case IBMVFC_HOST_ACTION_QUERY:
	default:
		break;
	};

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
		spin_unlock_irqrestore(vhost->host->host_lock, flags);
		fc_remote_port_delete(rport);
		del_timer_sync(&tgt->timer);
		kref_put(&tgt->kref, ibmvfc_release_tgt);
		return;
	}

	if (rport) {
		tgt_dbg(tgt, "rport add succeeded\n");
		tgt->rport = rport;
		rport->maxframe_size = tgt->service_parms.common.bb_rcv_sz & 0x0fff;
		rport->supported_classes = 0;
		tgt->target_id = rport->scsi_target_id;
		if (tgt->service_parms.class1_parms[0] & 0x80000000)
			rport->supported_classes |= FC_COS_CLASS1;
		if (tgt->service_parms.class2_parms[0] & 0x80000000)
			rport->supported_classes |= FC_COS_CLASS2;
		if (tgt->service_parms.class3_parms[0] & 0x80000000)
			rport->supported_classes |= FC_COS_CLASS3;
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

	ibmvfc_log_ae(vhost, vhost->events_to_log);
	spin_lock_irqsave(vhost->host->host_lock, flags);
	vhost->events_to_log = 0;
	switch (vhost->action) {
	case IBMVFC_HOST_ACTION_NONE:
	case IBMVFC_HOST_ACTION_LOGO_WAIT:
	case IBMVFC_HOST_ACTION_INIT_WAIT:
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
			if (tgt->action == IBMVFC_TGT_ACTION_DEL_RPORT) {
				tgt_dbg(tgt, "Deleting rport\n");
				rport = tgt->rport;
				tgt->rport = NULL;
				list_del(&tgt->queue);
				spin_unlock_irqrestore(vhost->host->host_lock, flags);
				if (rport)
					fc_remote_port_delete(rport);
				del_timer_sync(&tgt->timer);
				kref_put(&tgt->kref, ibmvfc_release_tgt);
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
	};

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

	set_user_nice(current, -20);

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
	struct ibmvfc_crq_queue *crq = &vhost->crq;

	ENTER;
	crq->msgs = (struct ibmvfc_crq *)get_zeroed_page(GFP_KERNEL);

	if (!crq->msgs)
		return -ENOMEM;

	crq->size = PAGE_SIZE / sizeof(*crq->msgs);
	crq->msg_token = dma_map_single(dev, crq->msgs,
					PAGE_SIZE, DMA_BIDIRECTIONAL);

	if (dma_mapping_error(dev, crq->msg_token))
		goto map_failed;

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

	crq->cur = 0;
	LEAVE;
	return retrc;

req_irq_failed:
	tasklet_kill(&vhost->tasklet);
	do {
		rc = plpar_hcall_norets(H_FREE_CRQ, vdev->unit_address);
	} while (rc == H_BUSY || H_IS_LONG_BUSY(rc));
reg_crq_failed:
	dma_unmap_single(dev, crq->msg_token, PAGE_SIZE, DMA_BIDIRECTIONAL);
map_failed:
	free_page((unsigned long)crq->msgs);
	return retrc;
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
	struct ibmvfc_async_crq_queue *async_q = &vhost->async_crq;

	ENTER;
	mempool_destroy(vhost->tgt_pool);
	kfree(vhost->trace);
	dma_free_coherent(vhost->dev, vhost->disc_buf_sz, vhost->disc_buf,
			  vhost->disc_buf_dma);
	dma_free_coherent(vhost->dev, sizeof(*vhost->login_buf),
			  vhost->login_buf, vhost->login_buf_dma);
	dma_pool_destroy(vhost->sg_pool);
	dma_unmap_single(vhost->dev, async_q->msg_token,
			 async_q->size * sizeof(*async_q->msgs), DMA_BIDIRECTIONAL);
	free_page((unsigned long)async_q->msgs);
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
	struct ibmvfc_async_crq_queue *async_q = &vhost->async_crq;
	struct device *dev = vhost->dev;

	ENTER;
	async_q->msgs = (struct ibmvfc_async_crq *)get_zeroed_page(GFP_KERNEL);
	if (!async_q->msgs) {
		dev_err(dev, "Couldn't allocate async queue.\n");
		goto nomem;
	}

	async_q->size = PAGE_SIZE / sizeof(struct ibmvfc_async_crq);
	async_q->msg_token = dma_map_single(dev, async_q->msgs,
					    async_q->size * sizeof(*async_q->msgs),
					    DMA_BIDIRECTIONAL);

	if (dma_mapping_error(dev, async_q->msg_token)) {
		dev_err(dev, "Failed to map async queue\n");
		goto free_async_crq;
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

	vhost->disc_buf_sz = sizeof(vhost->disc_buf->scsi_id[0]) * max_targets;
	vhost->disc_buf = dma_alloc_coherent(dev, vhost->disc_buf_sz,
					     &vhost->disc_buf_dma, GFP_KERNEL);

	if (!vhost->disc_buf) {
		dev_err(dev, "Couldn't allocate Discover Targets buffer\n");
		goto free_login_buffer;
	}

	vhost->trace = kcalloc(IBMVFC_NUM_TRACE_ENTRIES,
			       sizeof(struct ibmvfc_trace_entry), GFP_KERNEL);

	if (!vhost->trace)
		goto free_disc_buffer;

	vhost->tgt_pool = mempool_create_kzalloc_pool(IBMVFC_TGT_MEMPOOL_SZ,
						      sizeof(struct ibmvfc_target));

	if (!vhost->tgt_pool) {
		dev_err(dev, "Couldn't allocate target memory pool\n");
		goto free_trace;
	}

	LEAVE;
	return 0;

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
	dma_unmap_single(dev, async_q->msg_token,
			 async_q->size * sizeof(*async_q->msgs), DMA_BIDIRECTIONAL);
free_async_crq:
	free_page((unsigned long)async_q->msgs);
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

	vhost = shost_priv(shost);
	INIT_LIST_HEAD(&vhost->sent);
	INIT_LIST_HEAD(&vhost->free);
	INIT_LIST_HEAD(&vhost->targets);
	sprintf(vhost->name, IBMVFC_NAME);
	vhost->host = shost;
	vhost->dev = dev;
	vhost->partition_number = -1;
	vhost->log_level = log_level;
	vhost->task_set = 1;
	strcpy(vhost->partition_name, "UNKNOWN");
	init_waitqueue_head(&vhost->work_wait_q);
	init_waitqueue_head(&vhost->init_wait_q);
	INIT_WORK(&vhost->rport_add_work_q, ibmvfc_rport_add_thread);

	if ((rc = ibmvfc_alloc_mem(vhost)))
		goto free_scsi_host;

	vhost->work_thread = kthread_run(ibmvfc_work, vhost, "%s_%d", IBMVFC_NAME,
					 shost->host_no);

	if (IS_ERR(vhost->work_thread)) {
		dev_err(dev, "Couldn't create kernel thread: %ld\n",
			PTR_ERR(vhost->work_thread));
		goto free_host_mem;
	}

	if ((rc = ibmvfc_init_crq(vhost))) {
		dev_err(dev, "Couldn't initialize crq. rc=%d\n", rc);
		goto kill_kthread;
	}

	if ((rc = ibmvfc_init_event_pool(vhost))) {
		dev_err(dev, "Couldn't initialize event pool. rc=%d\n", rc);
		goto release_crq;
	}

	if ((rc = scsi_add_host(shost, dev)))
		goto release_event_pool;

	if ((rc = ibmvfc_create_trace_file(&shost->shost_dev.kobj,
					   &ibmvfc_trace_attr))) {
		dev_err(dev, "Failed to create trace file. rc=%d\n", rc);
		goto remove_shost;
	}

	dev_set_drvdata(dev, vhost);
	spin_lock(&ibmvfc_driver_lock);
	list_add_tail(&vhost->queue, &ibmvfc_head);
	spin_unlock(&ibmvfc_driver_lock);

	ibmvfc_send_crq_init(vhost);
	scsi_scan_host(shost);
	return 0;

remove_shost:
	scsi_remove_host(shost);
release_event_pool:
	ibmvfc_free_event_pool(vhost);
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
static int ibmvfc_remove(struct vio_dev *vdev)
{
	struct ibmvfc_host *vhost = dev_get_drvdata(&vdev->dev);
	unsigned long flags;

	ENTER;
	ibmvfc_remove_trace_file(&vhost->host->shost_dev.kobj, &ibmvfc_trace_attr);
	ibmvfc_link_down(vhost, IBMVFC_HOST_OFFLINE);
	ibmvfc_wait_while_resetting(vhost);
	ibmvfc_release_crq_queue(vhost);
	kthread_stop(vhost->work_thread);
	fc_remove_host(vhost->host);
	scsi_remove_host(vhost->host);

	spin_lock_irqsave(vhost->host->host_lock, flags);
	ibmvfc_purge_requests(vhost, DID_ERROR);
	ibmvfc_free_event_pool(vhost);
	spin_unlock_irqrestore(vhost->host->host_lock, flags);

	ibmvfc_free_mem(vhost);
	spin_lock(&ibmvfc_driver_lock);
	list_del(&vhost->queue);
	spin_unlock(&ibmvfc_driver_lock);
	scsi_host_put(vhost->host);
	LEAVE;
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

static struct vio_device_id ibmvfc_device_table[] __devinitdata = {
	{"fcp", "IBM,vfc-client"},
	{ "", "" }
};
MODULE_DEVICE_TABLE(vio, ibmvfc_device_table);

static struct vio_driver ibmvfc_driver = {
	.id_table = ibmvfc_device_table,
	.probe = ibmvfc_probe,
	.remove = ibmvfc_remove,
	.get_desired_dma = ibmvfc_get_desired_dma,
	.driver = {
		.name = IBMVFC_NAME,
		.owner = THIS_MODULE,
	}
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
