/*******************************************************************************
 *
 * This file contains the Linux/SCSI LLD virtual SCSI initiator driver
 * for emulated SAS initiator ports
 *
 * © Copyright 2011-2013 Datera, Inc.
 *
 * Licensed to the Linux Foundation under the General Public License (GPL) version 2.
 *
 * Author: Nicholas A. Bellinger <nab@risingtidesystems.com>
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
 ****************************************************************************/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/configfs.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_cmnd.h>

#include <target/target_core_base.h>
#include <target/target_core_fabric.h>
#include <target/target_core_fabric_configfs.h>

#include "tcm_loop.h"

#define to_tcm_loop_hba(hba)	container_of(hba, struct tcm_loop_hba, dev)

static struct workqueue_struct *tcm_loop_workqueue;
static struct kmem_cache *tcm_loop_cmd_cache;

static int tcm_loop_hba_no_cnt;

static int tcm_loop_queue_status(struct se_cmd *se_cmd);

/*
 * Called from struct target_core_fabric_ops->check_stop_free()
 */
static int tcm_loop_check_stop_free(struct se_cmd *se_cmd)
{
	/*
	 * Do not release struct se_cmd's containing a valid TMR
	 * pointer.  These will be released directly in tcm_loop_device_reset()
	 * with transport_generic_free_cmd().
	 */
	if (se_cmd->se_cmd_flags & SCF_SCSI_TMR_CDB)
		return 0;
	/*
	 * Release the struct se_cmd, which will make a callback to release
	 * struct tcm_loop_cmd * in tcm_loop_deallocate_core_cmd()
	 */
	transport_generic_free_cmd(se_cmd, 0);
	return 1;
}

static void tcm_loop_release_cmd(struct se_cmd *se_cmd)
{
	struct tcm_loop_cmd *tl_cmd = container_of(se_cmd,
				struct tcm_loop_cmd, tl_se_cmd);

	kmem_cache_free(tcm_loop_cmd_cache, tl_cmd);
}

static int tcm_loop_show_info(struct seq_file *m, struct Scsi_Host *host)
{
	seq_printf(m, "tcm_loop_proc_info()\n");
	return 0;
}

static int tcm_loop_driver_probe(struct device *);
static int tcm_loop_driver_remove(struct device *);

static int pseudo_lld_bus_match(struct device *dev,
				struct device_driver *dev_driver)
{
	return 1;
}

static struct bus_type tcm_loop_lld_bus = {
	.name			= "tcm_loop_bus",
	.match			= pseudo_lld_bus_match,
	.probe			= tcm_loop_driver_probe,
	.remove			= tcm_loop_driver_remove,
};

static struct device_driver tcm_loop_driverfs = {
	.name			= "tcm_loop",
	.bus			= &tcm_loop_lld_bus,
};
/*
 * Used with root_device_register() in tcm_loop_alloc_core_bus() below
 */
static struct device *tcm_loop_primary;

static void tcm_loop_submission_work(struct work_struct *work)
{
	struct tcm_loop_cmd *tl_cmd =
		container_of(work, struct tcm_loop_cmd, work);
	struct se_cmd *se_cmd = &tl_cmd->tl_se_cmd;
	struct scsi_cmnd *sc = tl_cmd->sc;
	struct tcm_loop_nexus *tl_nexus;
	struct tcm_loop_hba *tl_hba;
	struct tcm_loop_tpg *tl_tpg;
	struct scatterlist *sgl_bidi = NULL;
	u32 sgl_bidi_count = 0, transfer_length;
	int rc;

	tl_hba = *(struct tcm_loop_hba **)shost_priv(sc->device->host);
	tl_tpg = &tl_hba->tl_hba_tpgs[sc->device->id];

	/*
	 * Ensure that this tl_tpg reference from the incoming sc->device->id
	 * has already been configured via tcm_loop_make_naa_tpg().
	 */
	if (!tl_tpg->tl_hba) {
		set_host_byte(sc, DID_NO_CONNECT);
		goto out_done;
	}
	if (tl_tpg->tl_transport_status == TCM_TRANSPORT_OFFLINE) {
		set_host_byte(sc, DID_TRANSPORT_DISRUPTED);
		goto out_done;
	}
	tl_nexus = tl_tpg->tl_nexus;
	if (!tl_nexus) {
		scmd_printk(KERN_ERR, sc, "TCM_Loop I_T Nexus"
				" does not exist\n");
		set_host_byte(sc, DID_ERROR);
		goto out_done;
	}
	if (scsi_bidi_cmnd(sc)) {
		struct scsi_data_buffer *sdb = scsi_in(sc);

		sgl_bidi = sdb->table.sgl;
		sgl_bidi_count = sdb->table.nents;
		se_cmd->se_cmd_flags |= SCF_BIDI;

	}

	transfer_length = scsi_transfer_length(sc);
	if (!scsi_prot_sg_count(sc) &&
	    scsi_get_prot_op(sc) != SCSI_PROT_NORMAL) {
		se_cmd->prot_pto = true;
		/*
		 * loopback transport doesn't support
		 * WRITE_GENERATE, READ_STRIP protection
		 * information operations, go ahead unprotected.
		 */
		transfer_length = scsi_bufflen(sc);
	}

	se_cmd->tag = tl_cmd->sc_cmd_tag;
	rc = target_submit_cmd_map_sgls(se_cmd, tl_nexus->se_sess, sc->cmnd,
			&tl_cmd->tl_sense_buf[0], tl_cmd->sc->device->lun,
			transfer_length, TCM_SIMPLE_TAG,
			sc->sc_data_direction, 0,
			scsi_sglist(sc), scsi_sg_count(sc),
			sgl_bidi, sgl_bidi_count,
			scsi_prot_sglist(sc), scsi_prot_sg_count(sc));
	if (rc < 0) {
		set_host_byte(sc, DID_NO_CONNECT);
		goto out_done;
	}
	return;

out_done:
	kmem_cache_free(tcm_loop_cmd_cache, tl_cmd);
	sc->scsi_done(sc);
	return;
}

/*
 * ->queuecommand can be and usually is called from interrupt context, so
 * defer the actual submission to a workqueue.
 */
static int tcm_loop_queuecommand(struct Scsi_Host *sh, struct scsi_cmnd *sc)
{
	struct tcm_loop_cmd *tl_cmd;

	pr_debug("tcm_loop_queuecommand() %d:%d:%d:%llu got CDB: 0x%02x"
		" scsi_buf_len: %u\n", sc->device->host->host_no,
		sc->device->id, sc->device->channel, sc->device->lun,
		sc->cmnd[0], scsi_bufflen(sc));

	tl_cmd = kmem_cache_zalloc(tcm_loop_cmd_cache, GFP_ATOMIC);
	if (!tl_cmd) {
		pr_err("Unable to allocate struct tcm_loop_cmd\n");
		set_host_byte(sc, DID_ERROR);
		sc->scsi_done(sc);
		return 0;
	}

	tl_cmd->sc = sc;
	tl_cmd->sc_cmd_tag = sc->request->tag;
	INIT_WORK(&tl_cmd->work, tcm_loop_submission_work);
	queue_work(tcm_loop_workqueue, &tl_cmd->work);
	return 0;
}

/*
 * Called from SCSI EH process context to issue a LUN_RESET TMR
 * to struct scsi_device
 */
static int tcm_loop_issue_tmr(struct tcm_loop_tpg *tl_tpg,
			      u64 lun, int task, enum tcm_tmreq_table tmr)
{
	struct se_cmd *se_cmd = NULL;
	struct se_session *se_sess;
	struct se_portal_group *se_tpg;
	struct tcm_loop_nexus *tl_nexus;
	struct tcm_loop_cmd *tl_cmd = NULL;
	struct tcm_loop_tmr *tl_tmr = NULL;
	int ret = TMR_FUNCTION_FAILED, rc;

	/*
	 * Locate the tl_nexus and se_sess pointers
	 */
	tl_nexus = tl_tpg->tl_nexus;
	if (!tl_nexus) {
		pr_err("Unable to perform device reset without"
				" active I_T Nexus\n");
		return ret;
	}

	tl_cmd = kmem_cache_zalloc(tcm_loop_cmd_cache, GFP_KERNEL);
	if (!tl_cmd) {
		pr_err("Unable to allocate memory for tl_cmd\n");
		return ret;
	}

	tl_tmr = kzalloc(sizeof(struct tcm_loop_tmr), GFP_KERNEL);
	if (!tl_tmr) {
		pr_err("Unable to allocate memory for tl_tmr\n");
		goto release;
	}
	init_waitqueue_head(&tl_tmr->tl_tmr_wait);

	se_cmd = &tl_cmd->tl_se_cmd;
	se_tpg = &tl_tpg->tl_se_tpg;
	se_sess = tl_tpg->tl_nexus->se_sess;
	/*
	 * Initialize struct se_cmd descriptor from target_core_mod infrastructure
	 */
	transport_init_se_cmd(se_cmd, se_tpg->se_tpg_tfo, se_sess, 0,
				DMA_NONE, TCM_SIMPLE_TAG,
				&tl_cmd->tl_sense_buf[0]);

	rc = core_tmr_alloc_req(se_cmd, tl_tmr, tmr, GFP_KERNEL);
	if (rc < 0)
		goto release;

	if (tmr == TMR_ABORT_TASK)
		se_cmd->se_tmr_req->ref_task_tag = task;

	/*
	 * Locate the underlying TCM struct se_lun
	 */
	if (transport_lookup_tmr_lun(se_cmd, lun) < 0) {
		ret = TMR_LUN_DOES_NOT_EXIST;
		goto release;
	}
	/*
	 * Queue the TMR to TCM Core and sleep waiting for
	 * tcm_loop_queue_tm_rsp() to wake us up.
	 */
	transport_generic_handle_tmr(se_cmd);
	wait_event(tl_tmr->tl_tmr_wait, atomic_read(&tl_tmr->tmr_complete));
	/*
	 * The TMR LUN_RESET has completed, check the response status and
	 * then release allocations.
	 */
	ret = se_cmd->se_tmr_req->response;
release:
	if (se_cmd)
		transport_generic_free_cmd(se_cmd, 1);
	else
		kmem_cache_free(tcm_loop_cmd_cache, tl_cmd);
	kfree(tl_tmr);
	return ret;
}

static int tcm_loop_abort_task(struct scsi_cmnd *sc)
{
	struct tcm_loop_hba *tl_hba;
	struct tcm_loop_tpg *tl_tpg;
	int ret = FAILED;

	/*
	 * Locate the tcm_loop_hba_t pointer
	 */
	tl_hba = *(struct tcm_loop_hba **)shost_priv(sc->device->host);
	tl_tpg = &tl_hba->tl_hba_tpgs[sc->device->id];
	ret = tcm_loop_issue_tmr(tl_tpg, sc->device->lun,
				 sc->request->tag, TMR_ABORT_TASK);
	return (ret == TMR_FUNCTION_COMPLETE) ? SUCCESS : FAILED;
}

/*
 * Called from SCSI EH process context to issue a LUN_RESET TMR
 * to struct scsi_device
 */
static int tcm_loop_device_reset(struct scsi_cmnd *sc)
{
	struct tcm_loop_hba *tl_hba;
	struct tcm_loop_tpg *tl_tpg;
	int ret = FAILED;

	/*
	 * Locate the tcm_loop_hba_t pointer
	 */
	tl_hba = *(struct tcm_loop_hba **)shost_priv(sc->device->host);
	tl_tpg = &tl_hba->tl_hba_tpgs[sc->device->id];

	ret = tcm_loop_issue_tmr(tl_tpg, sc->device->lun,
				 0, TMR_LUN_RESET);
	return (ret == TMR_FUNCTION_COMPLETE) ? SUCCESS : FAILED;
}

static int tcm_loop_target_reset(struct scsi_cmnd *sc)
{
	struct tcm_loop_hba *tl_hba;
	struct tcm_loop_tpg *tl_tpg;

	/*
	 * Locate the tcm_loop_hba_t pointer
	 */
	tl_hba = *(struct tcm_loop_hba **)shost_priv(sc->device->host);
	if (!tl_hba) {
		pr_err("Unable to perform device reset without"
				" active I_T Nexus\n");
		return FAILED;
	}
	/*
	 * Locate the tl_tpg pointer from TargetID in sc->device->id
	 */
	tl_tpg = &tl_hba->tl_hba_tpgs[sc->device->id];
	if (tl_tpg) {
		tl_tpg->tl_transport_status = TCM_TRANSPORT_ONLINE;
		return SUCCESS;
	}
	return FAILED;
}

static int tcm_loop_slave_alloc(struct scsi_device *sd)
{
	set_bit(QUEUE_FLAG_BIDI, &sd->request_queue->queue_flags);
	return 0;
}

static struct scsi_host_template tcm_loop_driver_template = {
	.show_info		= tcm_loop_show_info,
	.proc_name		= "tcm_loopback",
	.name			= "TCM_Loopback",
	.queuecommand		= tcm_loop_queuecommand,
	.change_queue_depth	= scsi_change_queue_depth,
	.eh_abort_handler = tcm_loop_abort_task,
	.eh_device_reset_handler = tcm_loop_device_reset,
	.eh_target_reset_handler = tcm_loop_target_reset,
	.can_queue		= 1024,
	.this_id		= -1,
	.sg_tablesize		= 256,
	.cmd_per_lun		= 1024,
	.max_sectors		= 0xFFFF,
	.use_clustering		= DISABLE_CLUSTERING,
	.slave_alloc		= tcm_loop_slave_alloc,
	.module			= THIS_MODULE,
	.use_blk_tags		= 1,
	.track_queue_depth	= 1,
};

static int tcm_loop_driver_probe(struct device *dev)
{
	struct tcm_loop_hba *tl_hba;
	struct Scsi_Host *sh;
	int error, host_prot;

	tl_hba = to_tcm_loop_hba(dev);

	sh = scsi_host_alloc(&tcm_loop_driver_template,
			sizeof(struct tcm_loop_hba));
	if (!sh) {
		pr_err("Unable to allocate struct scsi_host\n");
		return -ENODEV;
	}
	tl_hba->sh = sh;

	/*
	 * Assign the struct tcm_loop_hba pointer to struct Scsi_Host->hostdata
	 */
	*((struct tcm_loop_hba **)sh->hostdata) = tl_hba;
	/*
	 * Setup single ID, Channel and LUN for now..
	 */
	sh->max_id = 2;
	sh->max_lun = 0;
	sh->max_channel = 0;
	sh->max_cmd_len = SCSI_MAX_VARLEN_CDB_SIZE;

	host_prot = SHOST_DIF_TYPE1_PROTECTION | SHOST_DIF_TYPE2_PROTECTION |
		    SHOST_DIF_TYPE3_PROTECTION | SHOST_DIX_TYPE1_PROTECTION |
		    SHOST_DIX_TYPE2_PROTECTION | SHOST_DIX_TYPE3_PROTECTION;

	scsi_host_set_prot(sh, host_prot);
	scsi_host_set_guard(sh, SHOST_DIX_GUARD_CRC);

	error = scsi_add_host(sh, &tl_hba->dev);
	if (error) {
		pr_err("%s: scsi_add_host failed\n", __func__);
		scsi_host_put(sh);
		return -ENODEV;
	}
	return 0;
}

static int tcm_loop_driver_remove(struct device *dev)
{
	struct tcm_loop_hba *tl_hba;
	struct Scsi_Host *sh;

	tl_hba = to_tcm_loop_hba(dev);
	sh = tl_hba->sh;

	scsi_remove_host(sh);
	scsi_host_put(sh);
	return 0;
}

static void tcm_loop_release_adapter(struct device *dev)
{
	struct tcm_loop_hba *tl_hba = to_tcm_loop_hba(dev);

	kfree(tl_hba);
}

/*
 * Called from tcm_loop_make_scsi_hba() in tcm_loop_configfs.c
 */
static int tcm_loop_setup_hba_bus(struct tcm_loop_hba *tl_hba, int tcm_loop_host_id)
{
	int ret;

	tl_hba->dev.bus = &tcm_loop_lld_bus;
	tl_hba->dev.parent = tcm_loop_primary;
	tl_hba->dev.release = &tcm_loop_release_adapter;
	dev_set_name(&tl_hba->dev, "tcm_loop_adapter_%d", tcm_loop_host_id);

	ret = device_register(&tl_hba->dev);
	if (ret) {
		pr_err("device_register() failed for"
				" tl_hba->dev: %d\n", ret);
		return -ENODEV;
	}

	return 0;
}

/*
 * Called from tcm_loop_fabric_init() in tcl_loop_fabric.c to load the emulated
 * tcm_loop SCSI bus.
 */
static int tcm_loop_alloc_core_bus(void)
{
	int ret;

	tcm_loop_primary = root_device_register("tcm_loop_0");
	if (IS_ERR(tcm_loop_primary)) {
		pr_err("Unable to allocate tcm_loop_primary\n");
		return PTR_ERR(tcm_loop_primary);
	}

	ret = bus_register(&tcm_loop_lld_bus);
	if (ret) {
		pr_err("bus_register() failed for tcm_loop_lld_bus\n");
		goto dev_unreg;
	}

	ret = driver_register(&tcm_loop_driverfs);
	if (ret) {
		pr_err("driver_register() failed for"
				"tcm_loop_driverfs\n");
		goto bus_unreg;
	}

	pr_debug("Initialized TCM Loop Core Bus\n");
	return ret;

bus_unreg:
	bus_unregister(&tcm_loop_lld_bus);
dev_unreg:
	root_device_unregister(tcm_loop_primary);
	return ret;
}

static void tcm_loop_release_core_bus(void)
{
	driver_unregister(&tcm_loop_driverfs);
	bus_unregister(&tcm_loop_lld_bus);
	root_device_unregister(tcm_loop_primary);

	pr_debug("Releasing TCM Loop Core BUS\n");
}

static char *tcm_loop_get_fabric_name(void)
{
	return "loopback";
}

static inline struct tcm_loop_tpg *tl_tpg(struct se_portal_group *se_tpg)
{
	return container_of(se_tpg, struct tcm_loop_tpg, tl_se_tpg);
}

static char *tcm_loop_get_endpoint_wwn(struct se_portal_group *se_tpg)
{
	/*
	 * Return the passed NAA identifier for the Target Port
	 */
	return &tl_tpg(se_tpg)->tl_hba->tl_wwn_address[0];
}

static u16 tcm_loop_get_tag(struct se_portal_group *se_tpg)
{
	/*
	 * This Tag is used when forming SCSI Name identifier in EVPD=1 0x83
	 * to represent the SCSI Target Port.
	 */
	return tl_tpg(se_tpg)->tl_tpgt;
}

/*
 * Returning (1) here allows for target_core_mod struct se_node_acl to be generated
 * based upon the incoming fabric dependent SCSI Initiator Port
 */
static int tcm_loop_check_demo_mode(struct se_portal_group *se_tpg)
{
	return 1;
}

static int tcm_loop_check_demo_mode_cache(struct se_portal_group *se_tpg)
{
	return 0;
}

/*
 * Allow I_T Nexus full READ-WRITE access without explict Initiator Node ACLs for
 * local virtual Linux/SCSI LLD passthrough into VM hypervisor guest
 */
static int tcm_loop_check_demo_mode_write_protect(struct se_portal_group *se_tpg)
{
	return 0;
}

/*
 * Because TCM_Loop does not use explict ACLs and MappedLUNs, this will
 * never be called for TCM_Loop by target_core_fabric_configfs.c code.
 * It has been added here as a nop for target_fabric_tf_ops_check()
 */
static int tcm_loop_check_prod_mode_write_protect(struct se_portal_group *se_tpg)
{
	return 0;
}

static int tcm_loop_check_prot_fabric_only(struct se_portal_group *se_tpg)
{
	struct tcm_loop_tpg *tl_tpg = container_of(se_tpg, struct tcm_loop_tpg,
						   tl_se_tpg);
	return tl_tpg->tl_fabric_prot_type;
}

static u32 tcm_loop_get_inst_index(struct se_portal_group *se_tpg)
{
	return 1;
}

static u32 tcm_loop_sess_get_index(struct se_session *se_sess)
{
	return 1;
}

static void tcm_loop_set_default_node_attributes(struct se_node_acl *se_acl)
{
	return;
}

static int tcm_loop_get_cmd_state(struct se_cmd *se_cmd)
{
	struct tcm_loop_cmd *tl_cmd = container_of(se_cmd,
			struct tcm_loop_cmd, tl_se_cmd);

	return tl_cmd->sc_cmd_state;
}

static int tcm_loop_shutdown_session(struct se_session *se_sess)
{
	return 0;
}

static void tcm_loop_close_session(struct se_session *se_sess)
{
	return;
};

static int tcm_loop_write_pending(struct se_cmd *se_cmd)
{
	/*
	 * Since Linux/SCSI has already sent down a struct scsi_cmnd
	 * sc->sc_data_direction of DMA_TO_DEVICE with struct scatterlist array
	 * memory, and memory has already been mapped to struct se_cmd->t_mem_list
	 * format with transport_generic_map_mem_to_cmd().
	 *
	 * We now tell TCM to add this WRITE CDB directly into the TCM storage
	 * object execution queue.
	 */
	target_execute_cmd(se_cmd);
	return 0;
}

static int tcm_loop_write_pending_status(struct se_cmd *se_cmd)
{
	return 0;
}

static int tcm_loop_queue_data_in(struct se_cmd *se_cmd)
{
	struct tcm_loop_cmd *tl_cmd = container_of(se_cmd,
				struct tcm_loop_cmd, tl_se_cmd);
	struct scsi_cmnd *sc = tl_cmd->sc;

	pr_debug("tcm_loop_queue_data_in() called for scsi_cmnd: %p"
		     " cdb: 0x%02x\n", sc, sc->cmnd[0]);

	sc->result = SAM_STAT_GOOD;
	set_host_byte(sc, DID_OK);
	if ((se_cmd->se_cmd_flags & SCF_OVERFLOW_BIT) ||
	    (se_cmd->se_cmd_flags & SCF_UNDERFLOW_BIT))
		scsi_set_resid(sc, se_cmd->residual_count);
	sc->scsi_done(sc);
	return 0;
}

static int tcm_loop_queue_status(struct se_cmd *se_cmd)
{
	struct tcm_loop_cmd *tl_cmd = container_of(se_cmd,
				struct tcm_loop_cmd, tl_se_cmd);
	struct scsi_cmnd *sc = tl_cmd->sc;

	pr_debug("tcm_loop_queue_status() called for scsi_cmnd: %p"
			" cdb: 0x%02x\n", sc, sc->cmnd[0]);

	if (se_cmd->sense_buffer &&
	   ((se_cmd->se_cmd_flags & SCF_TRANSPORT_TASK_SENSE) ||
	    (se_cmd->se_cmd_flags & SCF_EMULATED_TASK_SENSE))) {

		memcpy(sc->sense_buffer, se_cmd->sense_buffer,
				SCSI_SENSE_BUFFERSIZE);
		sc->result = SAM_STAT_CHECK_CONDITION;
		set_driver_byte(sc, DRIVER_SENSE);
	} else
		sc->result = se_cmd->scsi_status;

	set_host_byte(sc, DID_OK);
	if ((se_cmd->se_cmd_flags & SCF_OVERFLOW_BIT) ||
	    (se_cmd->se_cmd_flags & SCF_UNDERFLOW_BIT))
		scsi_set_resid(sc, se_cmd->residual_count);
	sc->scsi_done(sc);
	return 0;
}

static void tcm_loop_queue_tm_rsp(struct se_cmd *se_cmd)
{
	struct se_tmr_req *se_tmr = se_cmd->se_tmr_req;
	struct tcm_loop_tmr *tl_tmr = se_tmr->fabric_tmr_ptr;
	/*
	 * The SCSI EH thread will be sleeping on se_tmr->tl_tmr_wait, go ahead
	 * and wake up the wait_queue_head_t in tcm_loop_device_reset()
	 */
	atomic_set(&tl_tmr->tmr_complete, 1);
	wake_up(&tl_tmr->tl_tmr_wait);
}

static void tcm_loop_aborted_task(struct se_cmd *se_cmd)
{
	return;
}

static char *tcm_loop_dump_proto_id(struct tcm_loop_hba *tl_hba)
{
	switch (tl_hba->tl_proto_id) {
	case SCSI_PROTOCOL_SAS:
		return "SAS";
	case SCSI_PROTOCOL_FCP:
		return "FCP";
	case SCSI_PROTOCOL_ISCSI:
		return "iSCSI";
	default:
		break;
	}

	return "Unknown";
}

/* Start items for tcm_loop_port_cit */

static int tcm_loop_port_link(
	struct se_portal_group *se_tpg,
	struct se_lun *lun)
{
	struct tcm_loop_tpg *tl_tpg = container_of(se_tpg,
				struct tcm_loop_tpg, tl_se_tpg);
	struct tcm_loop_hba *tl_hba = tl_tpg->tl_hba;

	atomic_inc_mb(&tl_tpg->tl_tpg_port_count);
	/*
	 * Add Linux/SCSI struct scsi_device by HCTL
	 */
	scsi_add_device(tl_hba->sh, 0, tl_tpg->tl_tpgt, lun->unpacked_lun);

	pr_debug("TCM_Loop_ConfigFS: Port Link Successful\n");
	return 0;
}

static void tcm_loop_port_unlink(
	struct se_portal_group *se_tpg,
	struct se_lun *se_lun)
{
	struct scsi_device *sd;
	struct tcm_loop_hba *tl_hba;
	struct tcm_loop_tpg *tl_tpg;

	tl_tpg = container_of(se_tpg, struct tcm_loop_tpg, tl_se_tpg);
	tl_hba = tl_tpg->tl_hba;

	sd = scsi_device_lookup(tl_hba->sh, 0, tl_tpg->tl_tpgt,
				se_lun->unpacked_lun);
	if (!sd) {
		pr_err("Unable to locate struct scsi_device for %d:%d:"
			"%llu\n", 0, tl_tpg->tl_tpgt, se_lun->unpacked_lun);
		return;
	}
	/*
	 * Remove Linux/SCSI struct scsi_device by HCTL
	 */
	scsi_remove_device(sd);
	scsi_device_put(sd);

	atomic_dec_mb(&tl_tpg->tl_tpg_port_count);

	pr_debug("TCM_Loop_ConfigFS: Port Unlink Successful\n");
}

/* End items for tcm_loop_port_cit */

static ssize_t tcm_loop_tpg_attrib_show_fabric_prot_type(
	struct se_portal_group *se_tpg,
	char *page)
{
	struct tcm_loop_tpg *tl_tpg = container_of(se_tpg, struct tcm_loop_tpg,
						   tl_se_tpg);

	return sprintf(page, "%d\n", tl_tpg->tl_fabric_prot_type);
}

static ssize_t tcm_loop_tpg_attrib_store_fabric_prot_type(
	struct se_portal_group *se_tpg,
	const char *page,
	size_t count)
{
	struct tcm_loop_tpg *tl_tpg = container_of(se_tpg, struct tcm_loop_tpg,
						   tl_se_tpg);
	unsigned long val;
	int ret = kstrtoul(page, 0, &val);

	if (ret) {
		pr_err("kstrtoul() returned %d for fabric_prot_type\n", ret);
		return ret;
	}
	if (val != 0 && val != 1 && val != 3) {
		pr_err("Invalid qla2xxx fabric_prot_type: %lu\n", val);
		return -EINVAL;
	}
	tl_tpg->tl_fabric_prot_type = val;

	return count;
}

TF_TPG_ATTRIB_ATTR(tcm_loop, fabric_prot_type, S_IRUGO | S_IWUSR);

static struct configfs_attribute *tcm_loop_tpg_attrib_attrs[] = {
	&tcm_loop_tpg_attrib_fabric_prot_type.attr,
	NULL,
};

/* Start items for tcm_loop_nexus_cit */

static int tcm_loop_make_nexus(
	struct tcm_loop_tpg *tl_tpg,
	const char *name)
{
	struct se_portal_group *se_tpg;
	struct tcm_loop_hba *tl_hba = tl_tpg->tl_hba;
	struct tcm_loop_nexus *tl_nexus;
	int ret = -ENOMEM;

	if (tl_tpg->tl_nexus) {
		pr_debug("tl_tpg->tl_nexus already exists\n");
		return -EEXIST;
	}
	se_tpg = &tl_tpg->tl_se_tpg;

	tl_nexus = kzalloc(sizeof(struct tcm_loop_nexus), GFP_KERNEL);
	if (!tl_nexus) {
		pr_err("Unable to allocate struct tcm_loop_nexus\n");
		return -ENOMEM;
	}
	/*
	 * Initialize the struct se_session pointer
	 */
	tl_nexus->se_sess = transport_init_session(
				TARGET_PROT_DIN_PASS | TARGET_PROT_DOUT_PASS);
	if (IS_ERR(tl_nexus->se_sess)) {
		ret = PTR_ERR(tl_nexus->se_sess);
		goto out;
	}
	/*
	 * Since we are running in 'demo mode' this call with generate a
	 * struct se_node_acl for the tcm_loop struct se_portal_group with the SCSI
	 * Initiator port name of the passed configfs group 'name'.
	 */
	tl_nexus->se_sess->se_node_acl = core_tpg_check_initiator_node_acl(
				se_tpg, (unsigned char *)name);
	if (!tl_nexus->se_sess->se_node_acl) {
		transport_free_session(tl_nexus->se_sess);
		goto out;
	}
	/* Now, register the I_T Nexus as active. */
	transport_register_session(se_tpg, tl_nexus->se_sess->se_node_acl,
			tl_nexus->se_sess, tl_nexus);
	tl_tpg->tl_nexus = tl_nexus;
	pr_debug("TCM_Loop_ConfigFS: Established I_T Nexus to emulated"
		" %s Initiator Port: %s\n", tcm_loop_dump_proto_id(tl_hba),
		name);
	return 0;

out:
	kfree(tl_nexus);
	return ret;
}

static int tcm_loop_drop_nexus(
	struct tcm_loop_tpg *tpg)
{
	struct se_session *se_sess;
	struct tcm_loop_nexus *tl_nexus;

	tl_nexus = tpg->tl_nexus;
	if (!tl_nexus)
		return -ENODEV;

	se_sess = tl_nexus->se_sess;
	if (!se_sess)
		return -ENODEV;

	if (atomic_read(&tpg->tl_tpg_port_count)) {
		pr_err("Unable to remove TCM_Loop I_T Nexus with"
			" active TPG port count: %d\n",
			atomic_read(&tpg->tl_tpg_port_count));
		return -EPERM;
	}

	pr_debug("TCM_Loop_ConfigFS: Removing I_T Nexus to emulated"
		" %s Initiator Port: %s\n", tcm_loop_dump_proto_id(tpg->tl_hba),
		tl_nexus->se_sess->se_node_acl->initiatorname);
	/*
	 * Release the SCSI I_T Nexus to the emulated Target Port
	 */
	transport_deregister_session(tl_nexus->se_sess);
	tpg->tl_nexus = NULL;
	kfree(tl_nexus);
	return 0;
}

/* End items for tcm_loop_nexus_cit */

static ssize_t tcm_loop_tpg_show_nexus(
	struct se_portal_group *se_tpg,
	char *page)
{
	struct tcm_loop_tpg *tl_tpg = container_of(se_tpg,
			struct tcm_loop_tpg, tl_se_tpg);
	struct tcm_loop_nexus *tl_nexus;
	ssize_t ret;

	tl_nexus = tl_tpg->tl_nexus;
	if (!tl_nexus)
		return -ENODEV;

	ret = snprintf(page, PAGE_SIZE, "%s\n",
		tl_nexus->se_sess->se_node_acl->initiatorname);

	return ret;
}

static ssize_t tcm_loop_tpg_store_nexus(
	struct se_portal_group *se_tpg,
	const char *page,
	size_t count)
{
	struct tcm_loop_tpg *tl_tpg = container_of(se_tpg,
			struct tcm_loop_tpg, tl_se_tpg);
	struct tcm_loop_hba *tl_hba = tl_tpg->tl_hba;
	unsigned char i_port[TL_WWN_ADDR_LEN], *ptr, *port_ptr;
	int ret;
	/*
	 * Shutdown the active I_T nexus if 'NULL' is passed..
	 */
	if (!strncmp(page, "NULL", 4)) {
		ret = tcm_loop_drop_nexus(tl_tpg);
		return (!ret) ? count : ret;
	}
	/*
	 * Otherwise make sure the passed virtual Initiator port WWN matches
	 * the fabric protocol_id set in tcm_loop_make_scsi_hba(), and call
	 * tcm_loop_make_nexus()
	 */
	if (strlen(page) >= TL_WWN_ADDR_LEN) {
		pr_err("Emulated NAA Sas Address: %s, exceeds"
				" max: %d\n", page, TL_WWN_ADDR_LEN);
		return -EINVAL;
	}
	snprintf(&i_port[0], TL_WWN_ADDR_LEN, "%s", page);

	ptr = strstr(i_port, "naa.");
	if (ptr) {
		if (tl_hba->tl_proto_id != SCSI_PROTOCOL_SAS) {
			pr_err("Passed SAS Initiator Port %s does not"
				" match target port protoid: %s\n", i_port,
				tcm_loop_dump_proto_id(tl_hba));
			return -EINVAL;
		}
		port_ptr = &i_port[0];
		goto check_newline;
	}
	ptr = strstr(i_port, "fc.");
	if (ptr) {
		if (tl_hba->tl_proto_id != SCSI_PROTOCOL_FCP) {
			pr_err("Passed FCP Initiator Port %s does not"
				" match target port protoid: %s\n", i_port,
				tcm_loop_dump_proto_id(tl_hba));
			return -EINVAL;
		}
		port_ptr = &i_port[3]; /* Skip over "fc." */
		goto check_newline;
	}
	ptr = strstr(i_port, "iqn.");
	if (ptr) {
		if (tl_hba->tl_proto_id != SCSI_PROTOCOL_ISCSI) {
			pr_err("Passed iSCSI Initiator Port %s does not"
				" match target port protoid: %s\n", i_port,
				tcm_loop_dump_proto_id(tl_hba));
			return -EINVAL;
		}
		port_ptr = &i_port[0];
		goto check_newline;
	}
	pr_err("Unable to locate prefix for emulated Initiator Port:"
			" %s\n", i_port);
	return -EINVAL;
	/*
	 * Clear any trailing newline for the NAA WWN
	 */
check_newline:
	if (i_port[strlen(i_port)-1] == '\n')
		i_port[strlen(i_port)-1] = '\0';

	ret = tcm_loop_make_nexus(tl_tpg, port_ptr);
	if (ret < 0)
		return ret;

	return count;
}

TF_TPG_BASE_ATTR(tcm_loop, nexus, S_IRUGO | S_IWUSR);

static ssize_t tcm_loop_tpg_show_transport_status(
	struct se_portal_group *se_tpg,
	char *page)
{
	struct tcm_loop_tpg *tl_tpg = container_of(se_tpg,
			struct tcm_loop_tpg, tl_se_tpg);
	const char *status = NULL;
	ssize_t ret = -EINVAL;

	switch (tl_tpg->tl_transport_status) {
	case TCM_TRANSPORT_ONLINE:
		status = "online";
		break;
	case TCM_TRANSPORT_OFFLINE:
		status = "offline";
		break;
	default:
		break;
	}

	if (status)
		ret = snprintf(page, PAGE_SIZE, "%s\n", status);

	return ret;
}

static ssize_t tcm_loop_tpg_store_transport_status(
	struct se_portal_group *se_tpg,
	const char *page,
	size_t count)
{
	struct tcm_loop_tpg *tl_tpg = container_of(se_tpg,
			struct tcm_loop_tpg, tl_se_tpg);

	if (!strncmp(page, "online", 6)) {
		tl_tpg->tl_transport_status = TCM_TRANSPORT_ONLINE;
		return count;
	}
	if (!strncmp(page, "offline", 7)) {
		tl_tpg->tl_transport_status = TCM_TRANSPORT_OFFLINE;
		if (tl_tpg->tl_nexus) {
			struct se_session *tl_sess = tl_tpg->tl_nexus->se_sess;

			core_allocate_nexus_loss_ua(tl_sess->se_node_acl);
		}
		return count;
	}
	return -EINVAL;
}

TF_TPG_BASE_ATTR(tcm_loop, transport_status, S_IRUGO | S_IWUSR);

static struct configfs_attribute *tcm_loop_tpg_attrs[] = {
	&tcm_loop_tpg_nexus.attr,
	&tcm_loop_tpg_transport_status.attr,
	NULL,
};

/* Start items for tcm_loop_naa_cit */

static struct se_portal_group *tcm_loop_make_naa_tpg(
	struct se_wwn *wwn,
	struct config_group *group,
	const char *name)
{
	struct tcm_loop_hba *tl_hba = container_of(wwn,
			struct tcm_loop_hba, tl_hba_wwn);
	struct tcm_loop_tpg *tl_tpg;
	int ret;
	unsigned long tpgt;

	if (strstr(name, "tpgt_") != name) {
		pr_err("Unable to locate \"tpgt_#\" directory"
				" group\n");
		return ERR_PTR(-EINVAL);
	}
	if (kstrtoul(name+5, 10, &tpgt))
		return ERR_PTR(-EINVAL);

	if (tpgt >= TL_TPGS_PER_HBA) {
		pr_err("Passed tpgt: %lu exceeds TL_TPGS_PER_HBA:"
				" %u\n", tpgt, TL_TPGS_PER_HBA);
		return ERR_PTR(-EINVAL);
	}
	tl_tpg = &tl_hba->tl_hba_tpgs[tpgt];
	tl_tpg->tl_hba = tl_hba;
	tl_tpg->tl_tpgt = tpgt;
	/*
	 * Register the tl_tpg as a emulated TCM Target Endpoint
	 */
	ret = core_tpg_register(wwn, &tl_tpg->tl_se_tpg, tl_hba->tl_proto_id);
	if (ret < 0)
		return ERR_PTR(-ENOMEM);

	pr_debug("TCM_Loop_ConfigFS: Allocated Emulated %s"
		" Target Port %s,t,0x%04lx\n", tcm_loop_dump_proto_id(tl_hba),
		config_item_name(&wwn->wwn_group.cg_item), tpgt);

	return &tl_tpg->tl_se_tpg;
}

static void tcm_loop_drop_naa_tpg(
	struct se_portal_group *se_tpg)
{
	struct se_wwn *wwn = se_tpg->se_tpg_wwn;
	struct tcm_loop_tpg *tl_tpg = container_of(se_tpg,
				struct tcm_loop_tpg, tl_se_tpg);
	struct tcm_loop_hba *tl_hba;
	unsigned short tpgt;

	tl_hba = tl_tpg->tl_hba;
	tpgt = tl_tpg->tl_tpgt;
	/*
	 * Release the I_T Nexus for the Virtual target link if present
	 */
	tcm_loop_drop_nexus(tl_tpg);
	/*
	 * Deregister the tl_tpg as a emulated TCM Target Endpoint
	 */
	core_tpg_deregister(se_tpg);

	tl_tpg->tl_hba = NULL;
	tl_tpg->tl_tpgt = 0;

	pr_debug("TCM_Loop_ConfigFS: Deallocated Emulated %s"
		" Target Port %s,t,0x%04x\n", tcm_loop_dump_proto_id(tl_hba),
		config_item_name(&wwn->wwn_group.cg_item), tpgt);
}

/* End items for tcm_loop_naa_cit */

/* Start items for tcm_loop_cit */

static struct se_wwn *tcm_loop_make_scsi_hba(
	struct target_fabric_configfs *tf,
	struct config_group *group,
	const char *name)
{
	struct tcm_loop_hba *tl_hba;
	struct Scsi_Host *sh;
	char *ptr;
	int ret, off = 0;

	tl_hba = kzalloc(sizeof(struct tcm_loop_hba), GFP_KERNEL);
	if (!tl_hba) {
		pr_err("Unable to allocate struct tcm_loop_hba\n");
		return ERR_PTR(-ENOMEM);
	}
	/*
	 * Determine the emulated Protocol Identifier and Target Port Name
	 * based on the incoming configfs directory name.
	 */
	ptr = strstr(name, "naa.");
	if (ptr) {
		tl_hba->tl_proto_id = SCSI_PROTOCOL_SAS;
		goto check_len;
	}
	ptr = strstr(name, "fc.");
	if (ptr) {
		tl_hba->tl_proto_id = SCSI_PROTOCOL_FCP;
		off = 3; /* Skip over "fc." */
		goto check_len;
	}
	ptr = strstr(name, "iqn.");
	if (!ptr) {
		pr_err("Unable to locate prefix for emulated Target "
				"Port: %s\n", name);
		ret = -EINVAL;
		goto out;
	}
	tl_hba->tl_proto_id = SCSI_PROTOCOL_ISCSI;

check_len:
	if (strlen(name) >= TL_WWN_ADDR_LEN) {
		pr_err("Emulated NAA %s Address: %s, exceeds"
			" max: %d\n", name, tcm_loop_dump_proto_id(tl_hba),
			TL_WWN_ADDR_LEN);
		ret = -EINVAL;
		goto out;
	}
	snprintf(&tl_hba->tl_wwn_address[0], TL_WWN_ADDR_LEN, "%s", &name[off]);

	/*
	 * Call device_register(tl_hba->dev) to register the emulated
	 * Linux/SCSI LLD of type struct Scsi_Host at tl_hba->sh after
	 * device_register() callbacks in tcm_loop_driver_probe()
	 */
	ret = tcm_loop_setup_hba_bus(tl_hba, tcm_loop_hba_no_cnt);
	if (ret)
		goto out;

	sh = tl_hba->sh;
	tcm_loop_hba_no_cnt++;
	pr_debug("TCM_Loop_ConfigFS: Allocated emulated Target"
		" %s Address: %s at Linux/SCSI Host ID: %d\n",
		tcm_loop_dump_proto_id(tl_hba), name, sh->host_no);

	return &tl_hba->tl_hba_wwn;
out:
	kfree(tl_hba);
	return ERR_PTR(ret);
}

static void tcm_loop_drop_scsi_hba(
	struct se_wwn *wwn)
{
	struct tcm_loop_hba *tl_hba = container_of(wwn,
				struct tcm_loop_hba, tl_hba_wwn);

	pr_debug("TCM_Loop_ConfigFS: Deallocating emulated Target"
		" %s Address: %s at Linux/SCSI Host ID: %d\n",
		tcm_loop_dump_proto_id(tl_hba), tl_hba->tl_wwn_address,
		tl_hba->sh->host_no);
	/*
	 * Call device_unregister() on the original tl_hba->dev.
	 * tcm_loop_fabric_scsi.c:tcm_loop_release_adapter() will
	 * release *tl_hba;
	 */
	device_unregister(&tl_hba->dev);
}

/* Start items for tcm_loop_cit */
static ssize_t tcm_loop_wwn_show_attr_version(
	struct target_fabric_configfs *tf,
	char *page)
{
	return sprintf(page, "TCM Loopback Fabric module %s\n", TCM_LOOP_VERSION);
}

TF_WWN_ATTR_RO(tcm_loop, version);

static struct configfs_attribute *tcm_loop_wwn_attrs[] = {
	&tcm_loop_wwn_version.attr,
	NULL,
};

/* End items for tcm_loop_cit */

static const struct target_core_fabric_ops loop_ops = {
	.module				= THIS_MODULE,
	.name				= "loopback",
	.get_fabric_name		= tcm_loop_get_fabric_name,
	.tpg_get_wwn			= tcm_loop_get_endpoint_wwn,
	.tpg_get_tag			= tcm_loop_get_tag,
	.tpg_check_demo_mode		= tcm_loop_check_demo_mode,
	.tpg_check_demo_mode_cache	= tcm_loop_check_demo_mode_cache,
	.tpg_check_demo_mode_write_protect =
				tcm_loop_check_demo_mode_write_protect,
	.tpg_check_prod_mode_write_protect =
				tcm_loop_check_prod_mode_write_protect,
	.tpg_check_prot_fabric_only	= tcm_loop_check_prot_fabric_only,
	.tpg_get_inst_index		= tcm_loop_get_inst_index,
	.check_stop_free		= tcm_loop_check_stop_free,
	.release_cmd			= tcm_loop_release_cmd,
	.shutdown_session		= tcm_loop_shutdown_session,
	.close_session			= tcm_loop_close_session,
	.sess_get_index			= tcm_loop_sess_get_index,
	.write_pending			= tcm_loop_write_pending,
	.write_pending_status		= tcm_loop_write_pending_status,
	.set_default_node_attributes	= tcm_loop_set_default_node_attributes,
	.get_cmd_state			= tcm_loop_get_cmd_state,
	.queue_data_in			= tcm_loop_queue_data_in,
	.queue_status			= tcm_loop_queue_status,
	.queue_tm_rsp			= tcm_loop_queue_tm_rsp,
	.aborted_task			= tcm_loop_aborted_task,
	.fabric_make_wwn		= tcm_loop_make_scsi_hba,
	.fabric_drop_wwn		= tcm_loop_drop_scsi_hba,
	.fabric_make_tpg		= tcm_loop_make_naa_tpg,
	.fabric_drop_tpg		= tcm_loop_drop_naa_tpg,
	.fabric_post_link		= tcm_loop_port_link,
	.fabric_pre_unlink		= tcm_loop_port_unlink,
	.tfc_wwn_attrs			= tcm_loop_wwn_attrs,
	.tfc_tpg_base_attrs		= tcm_loop_tpg_attrs,
	.tfc_tpg_attrib_attrs		= tcm_loop_tpg_attrib_attrs,
};

static int __init tcm_loop_fabric_init(void)
{
	int ret = -ENOMEM;

	tcm_loop_workqueue = alloc_workqueue("tcm_loop", 0, 0);
	if (!tcm_loop_workqueue)
		goto out;

	tcm_loop_cmd_cache = kmem_cache_create("tcm_loop_cmd_cache",
				sizeof(struct tcm_loop_cmd),
				__alignof__(struct tcm_loop_cmd),
				0, NULL);
	if (!tcm_loop_cmd_cache) {
		pr_debug("kmem_cache_create() for"
			" tcm_loop_cmd_cache failed\n");
		goto out_destroy_workqueue;
	}

	ret = tcm_loop_alloc_core_bus();
	if (ret)
		goto out_destroy_cache;

	ret = target_register_template(&loop_ops);
	if (ret)
		goto out_release_core_bus;

	return 0;

out_release_core_bus:
	tcm_loop_release_core_bus();
out_destroy_cache:
	kmem_cache_destroy(tcm_loop_cmd_cache);
out_destroy_workqueue:
	destroy_workqueue(tcm_loop_workqueue);
out:
	return ret;
}

static void __exit tcm_loop_fabric_exit(void)
{
	target_unregister_template(&loop_ops);
	tcm_loop_release_core_bus();
	kmem_cache_destroy(tcm_loop_cmd_cache);
	destroy_workqueue(tcm_loop_workqueue);
}

MODULE_DESCRIPTION("TCM loopback virtual Linux/SCSI fabric module");
MODULE_AUTHOR("Nicholas A. Bellinger <nab@risingtidesystems.com>");
MODULE_LICENSE("GPL");
module_init(tcm_loop_fabric_init);
module_exit(tcm_loop_fabric_exit);
