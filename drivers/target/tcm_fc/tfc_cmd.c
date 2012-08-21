/*
 * Copyright (c) 2010 Cisco Systems, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* XXX TBD some includes may be extraneous */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <generated/utsrelease.h>
#include <linux/utsname.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/configfs.h>
#include <linux/ctype.h>
#include <linux/hash.h>
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_tcq.h>
#include <scsi/libfc.h>
#include <scsi/fc_encode.h>

#include <target/target_core_base.h>
#include <target/target_core_transport.h>
#include <target/target_core_fabric_ops.h>
#include <target/target_core_device.h>
#include <target/target_core_tpg.h>
#include <target/target_core_configfs.h>
#include <target/target_core_base.h>
#include <target/target_core_tmr.h>
#include <target/configfs_macros.h>

#include "tcm_fc.h"

/*
 * Dump cmd state for debugging.
 */
void ft_dump_cmd(struct ft_cmd *cmd, const char *caller)
{
	struct fc_exch *ep;
	struct fc_seq *sp;
	struct se_cmd *se_cmd;
	struct se_mem *mem;
	struct se_transport_task *task;

	if (!(ft_debug_logging & FT_DEBUG_IO))
		return;

	se_cmd = &cmd->se_cmd;
	printk(KERN_INFO "%s: cmd %p state %d sess %p seq %p se_cmd %p\n",
		caller, cmd, cmd->state, cmd->sess, cmd->seq, se_cmd);
	printk(KERN_INFO "%s: cmd %p cdb %p\n",
		caller, cmd, cmd->cdb);
	printk(KERN_INFO "%s: cmd %p lun %d\n", caller, cmd, cmd->lun);

	task = T_TASK(se_cmd);
	printk(KERN_INFO "%s: cmd %p task %p se_num %u buf %p len %u se_cmd_flags <0x%x>\n",
	       caller, cmd, task, task->t_tasks_se_num,
	       task->t_task_buf, se_cmd->data_length, se_cmd->se_cmd_flags);
	if (task->t_mem_list)
		list_for_each_entry(mem, task->t_mem_list, se_list)
			printk(KERN_INFO "%s: cmd %p mem %p page %p "
			       "len 0x%x off 0x%x\n",
			       caller, cmd, mem,
			       mem->se_page, mem->se_len, mem->se_off);
	sp = cmd->seq;
	if (sp) {
		ep = fc_seq_exch(sp);
		printk(KERN_INFO "%s: cmd %p sid %x did %x "
			"ox_id %x rx_id %x seq_id %x e_stat %x\n",
			caller, cmd, ep->sid, ep->did, ep->oxid, ep->rxid,
			sp->id, ep->esb_stat);
	}
	print_hex_dump(KERN_INFO, "ft_dump_cmd ", DUMP_PREFIX_NONE,
		16, 4, cmd->cdb, MAX_COMMAND_SIZE, 0);
}

static void ft_queue_cmd(struct ft_sess *sess, struct ft_cmd *cmd)
{
	struct se_queue_obj *qobj;
	unsigned long flags;

	qobj = &sess->tport->tpg->qobj;
	spin_lock_irqsave(&qobj->cmd_queue_lock, flags);
	list_add_tail(&cmd->se_req.qr_list, &qobj->qobj_list);
	spin_unlock_irqrestore(&qobj->cmd_queue_lock, flags);
	atomic_inc(&qobj->queue_cnt);
	wake_up_interruptible(&qobj->thread_wq);
}

static struct ft_cmd *ft_dequeue_cmd(struct se_queue_obj *qobj)
{
	unsigned long flags;
	struct se_queue_req *qr;

	spin_lock_irqsave(&qobj->cmd_queue_lock, flags);
	if (list_empty(&qobj->qobj_list)) {
		spin_unlock_irqrestore(&qobj->cmd_queue_lock, flags);
		return NULL;
	}
	qr = list_first_entry(&qobj->qobj_list, struct se_queue_req, qr_list);
	list_del(&qr->qr_list);
	atomic_dec(&qobj->queue_cnt);
	spin_unlock_irqrestore(&qobj->cmd_queue_lock, flags);
	return container_of(qr, struct ft_cmd, se_req);
}

static void ft_free_cmd(struct ft_cmd *cmd)
{
	struct fc_frame *fp;
	struct fc_lport *lport;

	if (!cmd)
		return;
	fp = cmd->req_frame;
	lport = fr_dev(fp);
	if (fr_seq(fp))
		lport->tt.seq_release(fr_seq(fp));
	fc_frame_free(fp);
	ft_sess_put(cmd->sess);	/* undo get from lookup at recv */
	kfree(cmd);
}

void ft_release_cmd(struct se_cmd *se_cmd)
{
	struct ft_cmd *cmd = container_of(se_cmd, struct ft_cmd, se_cmd);

	ft_free_cmd(cmd);
}

void ft_check_stop_free(struct se_cmd *se_cmd)
{
	transport_generic_free_cmd(se_cmd, 0, 1, 0);
}

/*
 * Send response.
 */
int ft_queue_status(struct se_cmd *se_cmd)
{
	struct ft_cmd *cmd = container_of(se_cmd, struct ft_cmd, se_cmd);
	struct fc_frame *fp;
	struct fcp_resp_with_ext *fcp;
	struct fc_lport *lport;
	struct fc_exch *ep;
	size_t len;

	ft_dump_cmd(cmd, __func__);
	ep = fc_seq_exch(cmd->seq);
	lport = ep->lp;
	len = sizeof(*fcp) + se_cmd->scsi_sense_length;
	fp = fc_frame_alloc(lport, len);
	if (!fp) {
		/* XXX shouldn't just drop it - requeue and retry? */
		return 0;
	}
	fcp = fc_frame_payload_get(fp, len);
	memset(fcp, 0, len);
	fcp->resp.fr_status = se_cmd->scsi_status;

	len = se_cmd->scsi_sense_length;
	if (len) {
		fcp->resp.fr_flags |= FCP_SNS_LEN_VAL;
		fcp->ext.fr_sns_len = htonl(len);
		memcpy((fcp + 1), se_cmd->sense_buffer, len);
	}

	/*
	 * Test underflow and overflow with one mask.  Usually both are off.
	 * Bidirectional commands are not handled yet.
	 */
	if (se_cmd->se_cmd_flags & (SCF_OVERFLOW_BIT | SCF_UNDERFLOW_BIT)) {
		if (se_cmd->se_cmd_flags & SCF_OVERFLOW_BIT)
			fcp->resp.fr_flags |= FCP_RESID_OVER;
		else
			fcp->resp.fr_flags |= FCP_RESID_UNDER;
		fcp->ext.fr_resid = cpu_to_be32(se_cmd->residual_count);
	}

	/*
	 * Send response.
	 */
	cmd->seq = lport->tt.seq_start_next(cmd->seq);
	fc_fill_fc_hdr(fp, FC_RCTL_DD_CMD_STATUS, ep->did, ep->sid, FC_TYPE_FCP,
		       FC_FC_EX_CTX | FC_FC_LAST_SEQ | FC_FC_END_SEQ, 0);

	lport->tt.seq_send(lport, cmd->seq, fp);
	lport->tt.exch_done(cmd->seq);
	return 0;
}

int ft_write_pending_status(struct se_cmd *se_cmd)
{
	struct ft_cmd *cmd = container_of(se_cmd, struct ft_cmd, se_cmd);

	return cmd->write_data_len != se_cmd->data_length;
}

/*
 * Send TX_RDY (transfer ready).
 */
int ft_write_pending(struct se_cmd *se_cmd)
{
	struct ft_cmd *cmd = container_of(se_cmd, struct ft_cmd, se_cmd);
	struct fc_frame *fp;
	struct fcp_txrdy *txrdy;
	struct fc_lport *lport;
	struct fc_exch *ep;
	struct fc_frame_header *fh;
	u32 f_ctl;

	ft_dump_cmd(cmd, __func__);

	ep = fc_seq_exch(cmd->seq);
	lport = ep->lp;
	fp = fc_frame_alloc(lport, sizeof(*txrdy));
	if (!fp)
		return PYX_TRANSPORT_OUT_OF_MEMORY_RESOURCES;

	txrdy = fc_frame_payload_get(fp, sizeof(*txrdy));
	memset(txrdy, 0, sizeof(*txrdy));
	txrdy->ft_burst_len = htonl(se_cmd->data_length);

	cmd->seq = lport->tt.seq_start_next(cmd->seq);
	fc_fill_fc_hdr(fp, FC_RCTL_DD_DATA_DESC, ep->did, ep->sid, FC_TYPE_FCP,
		       FC_FC_EX_CTX | FC_FC_END_SEQ | FC_FC_SEQ_INIT, 0);

	fh = fc_frame_header_get(fp);
	f_ctl = ntoh24(fh->fh_f_ctl);

	/* Only if it is 'Exchange Responder' */
	if (f_ctl & FC_FC_EX_CTX) {
		/* Target is 'exchange responder' and sending XFER_READY
		 * to 'exchange initiator (initiator)'
		 */
		if ((ep->xid <= lport->lro_xid) &&
		    (fh->fh_r_ctl == FC_RCTL_DD_DATA_DESC)) {
			if (se_cmd->se_cmd_flags & SCF_SCSI_DATA_SG_IO_CDB) {
				/*
				 * Map se_mem list to scatterlist, so that
				 * DDP can be setup. DDP setup function require
				 * scatterlist. se_mem_list is internal to
				 * TCM/LIO target
				 */
				transport_do_task_sg_chain(se_cmd);
				cmd->sg = T_TASK(se_cmd)->t_tasks_sg_chained;
				cmd->sg_cnt =
					T_TASK(se_cmd)->t_tasks_sg_chained_no;
			}
			if (cmd->sg && lport->tt.ddp_setup(lport, ep->xid,
						    cmd->sg, cmd->sg_cnt))
				cmd->was_ddp_setup = 1;
		}
	}
	lport->tt.seq_send(lport, cmd->seq, fp);
	return 0;
}

u32 ft_get_task_tag(struct se_cmd *se_cmd)
{
	struct ft_cmd *cmd = container_of(se_cmd, struct ft_cmd, se_cmd);

	return fc_seq_exch(cmd->seq)->rxid;
}

int ft_get_cmd_state(struct se_cmd *se_cmd)
{
	struct ft_cmd *cmd = container_of(se_cmd, struct ft_cmd, se_cmd);

	return cmd->state;
}

int ft_is_state_remove(struct se_cmd *se_cmd)
{
	return 0;	/* XXX TBD */
}

void ft_new_cmd_failure(struct se_cmd *se_cmd)
{
	/* XXX TBD */
	printk(KERN_INFO "%s: se_cmd %p\n", __func__, se_cmd);
}

/*
 * FC sequence response handler for follow-on sequences (data) and aborts.
 */
static void ft_recv_seq(struct fc_seq *sp, struct fc_frame *fp, void *arg)
{
	struct ft_cmd *cmd = arg;
	struct fc_frame_header *fh;

	if (IS_ERR(fp)) {
		/* XXX need to find cmd if queued */
		cmd->se_cmd.t_state = TRANSPORT_REMOVE;
		cmd->seq = NULL;
		transport_generic_free_cmd(&cmd->se_cmd, 0, 1, 0);
		return;
	}

	fh = fc_frame_header_get(fp);

	switch (fh->fh_r_ctl) {
	case FC_RCTL_DD_SOL_DATA:	/* write data */
		ft_recv_write_data(cmd, fp);
		break;
	case FC_RCTL_DD_UNSOL_CTL:	/* command */
	case FC_RCTL_DD_SOL_CTL:	/* transfer ready */
	case FC_RCTL_DD_DATA_DESC:	/* transfer ready */
	default:
		printk(KERN_INFO "%s: unhandled frame r_ctl %x\n",
		       __func__, fh->fh_r_ctl);
		fc_frame_free(fp);
		transport_generic_free_cmd(&cmd->se_cmd, 0, 1, 0);
		break;
	}
}

/*
 * Send a FCP response including SCSI status and optional FCP rsp_code.
 * status is SAM_STAT_GOOD (zero) iff code is valid.
 * This is used in error cases, such as allocation failures.
 */
static void ft_send_resp_status(struct fc_lport *lport,
				const struct fc_frame *rx_fp,
				u32 status, enum fcp_resp_rsp_codes code)
{
	struct fc_frame *fp;
	struct fc_seq *sp;
	const struct fc_frame_header *fh;
	size_t len;
	struct fcp_resp_with_ext *fcp;
	struct fcp_resp_rsp_info *info;

	fh = fc_frame_header_get(rx_fp);
	FT_IO_DBG("FCP error response: did %x oxid %x status %x code %x\n",
		  ntoh24(fh->fh_s_id), ntohs(fh->fh_ox_id), status, code);
	len = sizeof(*fcp);
	if (status == SAM_STAT_GOOD)
		len += sizeof(*info);
	fp = fc_frame_alloc(lport, len);
	if (!fp)
		return;
	fcp = fc_frame_payload_get(fp, len);
	memset(fcp, 0, len);
	fcp->resp.fr_status = status;
	if (status == SAM_STAT_GOOD) {
		fcp->ext.fr_rsp_len = htonl(sizeof(*info));
		fcp->resp.fr_flags |= FCP_RSP_LEN_VAL;
		info = (struct fcp_resp_rsp_info *)(fcp + 1);
		info->rsp_code = code;
	}

	fc_fill_reply_hdr(fp, rx_fp, FC_RCTL_DD_CMD_STATUS, 0);
	sp = fr_seq(fp);
	if (sp) {
		lport->tt.seq_send(lport, sp, fp);
		lport->tt.exch_done(sp);
	} else {
		lport->tt.frame_send(lport, fp);
	}
}

/*
 * Send error or task management response.
 * Always frees the cmd and associated state.
 */
static void ft_send_resp_code(struct ft_cmd *cmd, enum fcp_resp_rsp_codes code)
{
	ft_send_resp_status(cmd->sess->tport->lport,
			    cmd->req_frame, SAM_STAT_GOOD, code);
	ft_free_cmd(cmd);
}

/*
 * Handle Task Management Request.
 */
static void ft_send_tm(struct ft_cmd *cmd)
{
	struct se_tmr_req *tmr;
	struct fcp_cmnd *fcp;
	struct ft_sess *sess;
	u8 tm_func;

	fcp = fc_frame_payload_get(cmd->req_frame, sizeof(*fcp));

	switch (fcp->fc_tm_flags) {
	case FCP_TMF_LUN_RESET:
		tm_func = TMR_LUN_RESET;
		break;
	case FCP_TMF_TGT_RESET:
		tm_func = TMR_TARGET_WARM_RESET;
		break;
	case FCP_TMF_CLR_TASK_SET:
		tm_func = TMR_CLEAR_TASK_SET;
		break;
	case FCP_TMF_ABT_TASK_SET:
		tm_func = TMR_ABORT_TASK_SET;
		break;
	case FCP_TMF_CLR_ACA:
		tm_func = TMR_CLEAR_ACA;
		break;
	default:
		/*
		 * FCP4r01 indicates having a combination of
		 * tm_flags set is invalid.
		 */
		FT_TM_DBG("invalid FCP tm_flags %x\n", fcp->fc_tm_flags);
		ft_send_resp_code(cmd, FCP_CMND_FIELDS_INVALID);
		return;
	}

	FT_TM_DBG("alloc tm cmd fn %d\n", tm_func);
	tmr = core_tmr_alloc_req(&cmd->se_cmd, cmd, tm_func);
	if (!tmr) {
		FT_TM_DBG("alloc failed\n");
		ft_send_resp_code(cmd, FCP_TMF_FAILED);
		return;
	}
	cmd->se_cmd.se_tmr_req = tmr;

	switch (fcp->fc_tm_flags) {
	case FCP_TMF_LUN_RESET:
		cmd->lun = scsilun_to_int((struct scsi_lun *)fcp->fc_lun);
		if (transport_get_lun_for_tmr(&cmd->se_cmd, cmd->lun) < 0) {
			/*
			 * Make sure to clean up newly allocated TMR request
			 * since "unable to  handle TMR request because failed
			 * to get to LUN"
			 */
			FT_TM_DBG("Failed to get LUN for TMR func %d, "
				  "se_cmd %p, unpacked_lun %d\n",
				  tm_func, &cmd->se_cmd, cmd->lun);
			ft_dump_cmd(cmd, __func__);
			sess = cmd->sess;
			transport_send_check_condition_and_sense(&cmd->se_cmd,
				cmd->se_cmd.scsi_sense_reason, 0);
			transport_generic_free_cmd(&cmd->se_cmd, 0, 1, 0);
			ft_sess_put(sess);
			return;
		}
		break;
	case FCP_TMF_TGT_RESET:
	case FCP_TMF_CLR_TASK_SET:
	case FCP_TMF_ABT_TASK_SET:
	case FCP_TMF_CLR_ACA:
		break;
	default:
		return;
	}
	transport_generic_handle_tmr(&cmd->se_cmd);
}

/*
 * Send status from completed task management request.
 */
int ft_queue_tm_resp(struct se_cmd *se_cmd)
{
	struct ft_cmd *cmd = container_of(se_cmd, struct ft_cmd, se_cmd);
	struct se_tmr_req *tmr = se_cmd->se_tmr_req;
	enum fcp_resp_rsp_codes code;

	switch (tmr->response) {
	case TMR_FUNCTION_COMPLETE:
		code = FCP_TMF_CMPL;
		break;
	case TMR_LUN_DOES_NOT_EXIST:
		code = FCP_TMF_INVALID_LUN;
		break;
	case TMR_FUNCTION_REJECTED:
		code = FCP_TMF_REJECTED;
		break;
	case TMR_TASK_DOES_NOT_EXIST:
	case TMR_TASK_STILL_ALLEGIANT:
	case TMR_TASK_FAILOVER_NOT_SUPPORTED:
	case TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED:
	case TMR_FUNCTION_AUTHORIZATION_FAILED:
	default:
		code = FCP_TMF_FAILED;
		break;
	}
	FT_TM_DBG("tmr fn %d resp %d fcp code %d\n",
		  tmr->function, tmr->response, code);
	ft_send_resp_code(cmd, code);
	return 0;
}

/*
 * Handle incoming FCP command.
 */
static void ft_recv_cmd(struct ft_sess *sess, struct fc_frame *fp)
{
	struct ft_cmd *cmd;
	struct fc_lport *lport = sess->tport->lport;

	cmd = kzalloc(sizeof(*cmd), GFP_ATOMIC);
	if (!cmd)
		goto busy;
	cmd->sess = sess;
	cmd->seq = lport->tt.seq_assign(lport, fp);
	if (!cmd->seq) {
		kfree(cmd);
		goto busy;
	}
	cmd->req_frame = fp;		/* hold frame during cmd */
	ft_queue_cmd(sess, cmd);
	return;

busy:
	FT_IO_DBG("cmd or seq allocation failure - sending BUSY\n");
	ft_send_resp_status(lport, fp, SAM_STAT_BUSY, 0);
	fc_frame_free(fp);
	ft_sess_put(sess);		/* undo get from lookup */
}


/*
 * Handle incoming FCP frame.
 * Caller has verified that the frame is type FCP.
 */
void ft_recv_req(struct ft_sess *sess, struct fc_frame *fp)
{
	struct fc_frame_header *fh = fc_frame_header_get(fp);

	switch (fh->fh_r_ctl) {
	case FC_RCTL_DD_UNSOL_CMD:	/* command */
		ft_recv_cmd(sess, fp);
		break;
	case FC_RCTL_DD_SOL_DATA:	/* write data */
	case FC_RCTL_DD_UNSOL_CTL:
	case FC_RCTL_DD_SOL_CTL:
	case FC_RCTL_DD_DATA_DESC:	/* transfer ready */
	case FC_RCTL_ELS4_REQ:		/* SRR, perhaps */
	default:
		printk(KERN_INFO "%s: unhandled frame r_ctl %x\n",
		       __func__, fh->fh_r_ctl);
		fc_frame_free(fp);
		ft_sess_put(sess);	/* undo get from lookup */
		break;
	}
}

/*
 * Send new command to target.
 */
static void ft_send_cmd(struct ft_cmd *cmd)
{
	struct fc_frame_header *fh = fc_frame_header_get(cmd->req_frame);
	struct se_cmd *se_cmd;
	struct fcp_cmnd *fcp;
	int data_dir;
	u32 data_len;
	int task_attr;
	int ret;

	fcp = fc_frame_payload_get(cmd->req_frame, sizeof(*fcp));
	if (!fcp)
		goto err;

	if (fcp->fc_flags & FCP_CFL_LEN_MASK)
		goto err;		/* not handling longer CDBs yet */

	if (fcp->fc_tm_flags) {
		task_attr = FCP_PTA_SIMPLE;
		data_dir = DMA_NONE;
		data_len = 0;
	} else {
		switch (fcp->fc_flags & (FCP_CFL_RDDATA | FCP_CFL_WRDATA)) {
		case 0:
			data_dir = DMA_NONE;
			break;
		case FCP_CFL_RDDATA:
			data_dir = DMA_FROM_DEVICE;
			break;
		case FCP_CFL_WRDATA:
			data_dir = DMA_TO_DEVICE;
			break;
		case FCP_CFL_WRDATA | FCP_CFL_RDDATA:
			goto err;	/* TBD not supported by tcm_fc yet */
		}
		/*
		 * Locate the SAM Task Attr from fc_pri_ta
		 */
		switch (fcp->fc_pri_ta & FCP_PTA_MASK) {
		case FCP_PTA_HEADQ:
			task_attr = MSG_HEAD_TAG;
			break;
		case FCP_PTA_ORDERED:
			task_attr = MSG_ORDERED_TAG;
			break;
		case FCP_PTA_ACA:
			task_attr = MSG_ACA_TAG;
			break;
		case FCP_PTA_SIMPLE: /* Fallthrough */
		default:
			task_attr = MSG_SIMPLE_TAG;
		}


		task_attr = fcp->fc_pri_ta & FCP_PTA_MASK;
		data_len = ntohl(fcp->fc_dl);
		cmd->cdb = fcp->fc_cdb;
	}

	se_cmd = &cmd->se_cmd;
	/*
	 * Initialize struct se_cmd descriptor from target_core_mod
	 * infrastructure
	 */
	transport_init_se_cmd(se_cmd, &ft_configfs->tf_ops, cmd->sess->se_sess,
			      data_len, data_dir, task_attr,
			      &cmd->ft_sense_buffer[0]);
	/*
	 * Check for FCP task management flags
	 */
	if (fcp->fc_tm_flags) {
		ft_send_tm(cmd);
		return;
	}

	fc_seq_exch(cmd->seq)->lp->tt.seq_set_resp(cmd->seq, ft_recv_seq, cmd);

	cmd->lun = scsilun_to_int((struct scsi_lun *)fcp->fc_lun);
	ret = transport_get_lun_for_cmd(&cmd->se_cmd, NULL, cmd->lun);
	if (ret < 0) {
		ft_dump_cmd(cmd, __func__);
		transport_send_check_condition_and_sense(&cmd->se_cmd,
			cmd->se_cmd.scsi_sense_reason, 0);
		return;
	}

	ret = transport_generic_allocate_tasks(se_cmd, cmd->cdb);

	FT_IO_DBG("r_ctl %x alloc task ret %d\n", fh->fh_r_ctl, ret);
	ft_dump_cmd(cmd, __func__);

	if (ret == -1) {
		transport_send_check_condition_and_sense(se_cmd,
				TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE, 0);
		transport_generic_free_cmd(se_cmd, 0, 1, 0);
		return;
	}
	if (ret == -2) {
		if (se_cmd->se_cmd_flags & SCF_SCSI_RESERVATION_CONFLICT)
			ft_queue_status(se_cmd);
		else
			transport_send_check_condition_and_sense(se_cmd,
					se_cmd->scsi_sense_reason, 0);
		transport_generic_free_cmd(se_cmd, 0, 1, 0);
		return;
	}
	transport_generic_handle_cdb(se_cmd);
	return;

err:
	ft_send_resp_code(cmd, FCP_CMND_FIELDS_INVALID);
	return;
}

/*
 * Handle request in the command thread.
 */
static void ft_exec_req(struct ft_cmd *cmd)
{
	FT_IO_DBG("cmd state %x\n", cmd->state);
	switch (cmd->state) {
	case FC_CMD_ST_NEW:
		ft_send_cmd(cmd);
		break;
	default:
		break;
	}
}

/*
 * Processing thread.
 * Currently one thread per tpg.
 */
int ft_thread(void *arg)
{
	struct ft_tpg *tpg = arg;
	struct se_queue_obj *qobj = &tpg->qobj;
	struct ft_cmd *cmd;
	int ret;

	set_user_nice(current, -20);

	while (!kthread_should_stop()) {
		ret = wait_event_interruptible(qobj->thread_wq,
			atomic_read(&qobj->queue_cnt) || kthread_should_stop());
		if (ret < 0 || kthread_should_stop())
			goto out;
		cmd = ft_dequeue_cmd(qobj);
		if (cmd)
			ft_exec_req(cmd);
	}

out:
	return 0;
}
