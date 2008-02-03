/*
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2005 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include "qla_def.h"

#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/smp_lock.h>
#include <linux/list.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>
#include <linux/delay.h>

void qla2x00_vp_stop_timer(scsi_qla_host_t *);

void
qla2x00_vp_stop_timer(scsi_qla_host_t *vha)
{
	if (vha->parent && vha->timer_active) {
		del_timer_sync(&vha->timer);
		vha->timer_active = 0;
	}
}

static uint32_t
qla24xx_allocate_vp_id(scsi_qla_host_t *vha)
{
	uint32_t vp_id;
	scsi_qla_host_t *ha = vha->parent;

	/* Find an empty slot and assign an vp_id */
	down(&ha->vport_sem);
	vp_id = find_first_zero_bit(ha->vp_idx_map, ha->max_npiv_vports + 1);
	if (vp_id > ha->max_npiv_vports) {
		DEBUG15(printk ("vp_id %d is bigger than max-supported %d.\n",
		    vp_id, ha->max_npiv_vports));
		up(&ha->vport_sem);
		return vp_id;
	}

	set_bit(vp_id, ha->vp_idx_map);
	ha->num_vhosts++;
	vha->vp_idx = vp_id;
	list_add_tail(&vha->vp_list, &ha->vp_list);
	up(&ha->vport_sem);
	return vp_id;
}

void
qla24xx_deallocate_vp_id(scsi_qla_host_t *vha)
{
	uint16_t vp_id;
	scsi_qla_host_t *ha = vha->parent;

	down(&ha->vport_sem);
	vp_id = vha->vp_idx;
	ha->num_vhosts--;
	clear_bit(vp_id, ha->vp_idx_map);
	list_del(&vha->vp_list);
	up(&ha->vport_sem);
}

static scsi_qla_host_t *
qla24xx_find_vhost_by_name(scsi_qla_host_t *ha, uint8_t *port_name)
{
	scsi_qla_host_t *vha;

	/* Locate matching device in database. */
	list_for_each_entry(vha, &ha->vp_list, vp_list) {
		if (!memcmp(port_name, vha->port_name, WWN_SIZE))
			return vha;
	}
	return NULL;
}

/*
 * qla2x00_mark_vp_devices_dead
 *	Updates fcport state when device goes offline.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fcport = port structure pointer.
 *
 * Return:
 *	None.
 *
 * Context:
 */
static void
qla2x00_mark_vp_devices_dead(scsi_qla_host_t *vha)
{
	fc_port_t *fcport;
	scsi_qla_host_t *pha = to_qla_parent(vha);

	list_for_each_entry(fcport, &pha->fcports, list) {
		if (fcport->vp_idx != vha->vp_idx)
			continue;

		DEBUG15(printk("scsi(%ld): Marking port dead, "
		    "loop_id=0x%04x :%x\n",
		    vha->host_no, fcport->loop_id, fcport->vp_idx));

		atomic_set(&fcport->state, FCS_DEVICE_DEAD);
		qla2x00_mark_device_lost(vha, fcport, 0, 0);
	}
}

int
qla24xx_disable_vp(scsi_qla_host_t *vha)
{
	int ret;

	ret = qla24xx_control_vp(vha, VCE_COMMAND_DISABLE_VPS_LOGO_ALL);
	atomic_set(&vha->loop_state, LOOP_DOWN);
	atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);

	/* Delete all vp's fcports from parent's list */
	qla2x00_mark_vp_devices_dead(vha);
	atomic_set(&vha->vp_state, VP_FAILED);
	vha->flags.management_server_logged_in = 0;
	if (ret == QLA_SUCCESS) {
		fc_vport_set_state(vha->fc_vport, FC_VPORT_DISABLED);
	} else {
		fc_vport_set_state(vha->fc_vport, FC_VPORT_FAILED);
		return -1;
	}
	return 0;
}

int
qla24xx_enable_vp(scsi_qla_host_t *vha)
{
	int ret;
	scsi_qla_host_t *ha = vha->parent;

	/* Check if physical ha port is Up */
	if (atomic_read(&ha->loop_state) == LOOP_DOWN  ||
		atomic_read(&ha->loop_state) == LOOP_DEAD ) {
		vha->vp_err_state =  VP_ERR_PORTDWN;
		fc_vport_set_state(vha->fc_vport, FC_VPORT_LINKDOWN);
		goto enable_failed;
	}

	/* Initialize the new vport unless it is a persistent port */
	down(&ha->vport_sem);
	ret = qla24xx_modify_vp_config(vha);
	up(&ha->vport_sem);

	if (ret != QLA_SUCCESS) {
		fc_vport_set_state(vha->fc_vport, FC_VPORT_FAILED);
		goto enable_failed;
	}

	DEBUG15(qla_printk(KERN_INFO, ha,
	    "Virtual port with id: %d - Enabled\n", vha->vp_idx));
	return 0;

enable_failed:
	DEBUG15(qla_printk(KERN_INFO, ha,
	    "Virtual port with id: %d - Disabled\n", vha->vp_idx));
	return 1;
}

static void
qla24xx_configure_vp(scsi_qla_host_t *vha)
{
	struct fc_vport *fc_vport;
	int ret;

	fc_vport = vha->fc_vport;

	DEBUG15(printk("scsi(%ld): %s: change request #3 for this host.\n",
	    vha->host_no, __func__));
	ret = qla2x00_send_change_request(vha, 0x3, vha->vp_idx);
	if (ret != QLA_SUCCESS) {
		DEBUG15(qla_printk(KERN_ERR, vha, "Failed to enable receiving"
		    " of RSCN requests: 0x%x\n", ret));
		return;
	} else {
		/* Corresponds to SCR enabled */
		clear_bit(VP_SCR_NEEDED, &vha->vp_flags);
	}

	vha->flags.online = 1;
	if (qla24xx_configure_vhba(vha))
		return;

	atomic_set(&vha->vp_state, VP_ACTIVE);
	fc_vport_set_state(fc_vport, FC_VPORT_ACTIVE);
}

void
qla2x00_alert_all_vps(scsi_qla_host_t *ha, uint16_t *mb)
{
	int i, vp_idx_matched;
	scsi_qla_host_t *vha;

	if (ha->parent)
		return;

	for_each_mapped_vp_idx(ha, i) {
		vp_idx_matched = 0;

		list_for_each_entry(vha, &ha->vp_list, vp_list) {
			if (i == vha->vp_idx) {
				vp_idx_matched = 1;
				break;
			}
		}

		if (vp_idx_matched) {
			switch (mb[0]) {
			case MBA_LIP_OCCURRED:
			case MBA_LOOP_UP:
			case MBA_LOOP_DOWN:
			case MBA_LIP_RESET:
			case MBA_POINT_TO_POINT:
			case MBA_CHG_IN_CONNECTION:
			case MBA_PORT_UPDATE:
			case MBA_RSCN_UPDATE:
				DEBUG15(printk("scsi(%ld)%s: Async_event for"
				    " VP[%d], mb = 0x%x, vha=%p\n",
				    vha->host_no, __func__,i, *mb, vha));
				qla2x00_async_event(vha, mb);
				break;
			}
		}
	}
}

void
qla2x00_vp_abort_isp(scsi_qla_host_t *vha)
{
	/*
	 * Physical port will do most of the abort and recovery work. We can
	 * just treat it as a loop down
	 */
	if (atomic_read(&vha->loop_state) != LOOP_DOWN) {
		atomic_set(&vha->loop_state, LOOP_DOWN);
		qla2x00_mark_all_devices_lost(vha, 0);
	} else {
		if (!atomic_read(&vha->loop_down_timer))
			atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);
	}

	DEBUG15(printk("scsi(%ld): Scheduling enable of Vport %d...\n",
	    vha->host_no, vha->vp_idx));
	qla24xx_enable_vp(vha);
}

static int
qla2x00_do_dpc_vp(scsi_qla_host_t *vha)
{
	if (test_and_clear_bit(VP_IDX_ACQUIRED, &vha->vp_flags)) {
		/* VP acquired. complete port configuration */
		qla24xx_configure_vp(vha);
		return 0;
	}

	if (test_and_clear_bit(ISP_ABORT_NEEDED, &vha->dpc_flags))
		qla2x00_vp_abort_isp(vha);

	if (test_and_clear_bit(RESET_MARKER_NEEDED, &vha->dpc_flags) &&
	    (!(test_and_set_bit(RESET_ACTIVE, &vha->dpc_flags)))) {
		clear_bit(RESET_ACTIVE, &vha->dpc_flags);
	}

	if (test_and_clear_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags)) {
		if (!(test_and_set_bit(LOOP_RESYNC_ACTIVE, &vha->dpc_flags))) {
			qla2x00_loop_resync(vha);
			clear_bit(LOOP_RESYNC_ACTIVE, &vha->dpc_flags);
		}
	}

	return 0;
}

void
qla2x00_do_dpc_all_vps(scsi_qla_host_t *ha)
{
	int ret;
	int i, vp_idx_matched;
	scsi_qla_host_t *vha;

	if (ha->parent)
		return;
	if (list_empty(&ha->vp_list))
		return;

	clear_bit(VP_DPC_NEEDED, &ha->dpc_flags);

	for_each_mapped_vp_idx(ha, i) {
		vp_idx_matched = 0;

		list_for_each_entry(vha, &ha->vp_list, vp_list) {
			if (i == vha->vp_idx) {
				vp_idx_matched = 1;
				break;
			}
		}

		if (vp_idx_matched)
			ret = qla2x00_do_dpc_vp(vha);
	}
}

int
qla24xx_vport_create_req_sanity_check(struct fc_vport *fc_vport)
{
	scsi_qla_host_t *ha = shost_priv(fc_vport->shost);
	scsi_qla_host_t *vha;
	uint8_t port_name[WWN_SIZE];

	if (fc_vport->roles != FC_PORT_ROLE_FCP_INITIATOR)
		return VPCERR_UNSUPPORTED;

	/* Check up the F/W and H/W support NPIV */
	if (!ha->flags.npiv_supported)
		return VPCERR_UNSUPPORTED;

	/* Check up whether npiv supported switch presented */
	if (!(ha->switch_cap & FLOGI_MID_SUPPORT))
		return VPCERR_NO_FABRIC_SUPP;

	/* Check up unique WWPN */
	u64_to_wwn(fc_vport->port_name, port_name);
	if (!memcmp(port_name, ha->port_name, WWN_SIZE))
		return VPCERR_BAD_WWN;
	vha = qla24xx_find_vhost_by_name(ha, port_name);
	if (vha)
		return VPCERR_BAD_WWN;

	/* Check up max-npiv-supports */
	if (ha->num_vhosts > ha->max_npiv_vports) {
		DEBUG15(printk("scsi(%ld): num_vhosts %ud is bigger than "
		    "max_npv_vports %ud.\n", ha->host_no,
		    ha->num_vhosts, ha->max_npiv_vports));
		return VPCERR_UNSUPPORTED;
	}
	return 0;
}

scsi_qla_host_t *
qla24xx_create_vhost(struct fc_vport *fc_vport)
{
	scsi_qla_host_t *ha = shost_priv(fc_vport->shost);
	scsi_qla_host_t *vha;
	struct Scsi_Host *host;

	host = scsi_host_alloc(&qla24xx_driver_template,
	    sizeof(scsi_qla_host_t));
	if (!host) {
		printk(KERN_WARNING
		    "qla2xxx: scsi_host_alloc() failed for vport\n");
		return(NULL);
	}

	vha = shost_priv(host);

	/* clone the parent hba */
	memcpy(vha, ha, sizeof (scsi_qla_host_t));

	fc_vport->dd_data = vha;

	vha->node_name = kmalloc(WWN_SIZE * sizeof(char), GFP_KERNEL);
	if (!vha->node_name)
		goto create_vhost_failed_1;

	vha->port_name = kmalloc(WWN_SIZE * sizeof(char), GFP_KERNEL);
	if (!vha->port_name)
		goto create_vhost_failed_2;

	/* New host info */
	u64_to_wwn(fc_vport->node_name, vha->node_name);
	u64_to_wwn(fc_vport->port_name, vha->port_name);

	vha->host = host;
	vha->host_no = host->host_no;
	vha->parent = ha;
	vha->fc_vport = fc_vport;
	vha->device_flags = 0;
	vha->instance = num_hosts;
	vha->vp_idx = qla24xx_allocate_vp_id(vha);
	if (vha->vp_idx > ha->max_npiv_vports) {
		DEBUG15(printk("scsi(%ld): Couldn't allocate vp_id.\n",
			vha->host_no));
		goto create_vhost_failed_3;
	}
	vha->mgmt_svr_loop_id = 10 + vha->vp_idx;

	init_completion(&vha->mbx_cmd_comp);
	complete(&vha->mbx_cmd_comp);
	init_completion(&vha->mbx_intr_comp);

	INIT_LIST_HEAD(&vha->list);
	INIT_LIST_HEAD(&vha->fcports);
	INIT_LIST_HEAD(&vha->vp_fcports);

	vha->dpc_flags = 0L;
	set_bit(REGISTER_FDMI_NEEDED, &vha->dpc_flags);
	set_bit(REGISTER_FC4_NEEDED, &vha->dpc_flags);

	/*
	 * To fix the issue of processing a parent's RSCN for the vport before
	 * its SCR is complete.
	 */
	set_bit(VP_SCR_NEEDED, &vha->vp_flags);
	atomic_set(&vha->loop_state, LOOP_DOWN);
	atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);

	qla2x00_start_timer(vha, qla2x00_timer, WATCH_INTERVAL);

	host->can_queue = vha->request_q_length + 128;
	host->this_id = 255;
	host->cmd_per_lun = 3;
	host->max_cmd_len = MAX_CMDSZ;
	host->max_channel = MAX_BUSES - 1;
	host->max_lun = MAX_LUNS;
	host->unique_id = vha->instance;
	host->max_id = MAX_TARGETS_2200;
	host->transportt = qla2xxx_transport_vport_template;

	DEBUG15(printk("DEBUG: detect vport hba %ld at address = %p\n",
	    vha->host_no, vha));

	vha->flags.init_done = 1;
	num_hosts++;

	down(&ha->vport_sem);
	set_bit(vha->vp_idx, ha->vp_idx_map);
	ha->cur_vport_count++;
	up(&ha->vport_sem);

	return vha;

create_vhost_failed_3:
	kfree(vha->port_name);

create_vhost_failed_2:
	kfree(vha->node_name);

create_vhost_failed_1:
	return NULL;
}
