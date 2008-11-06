/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2008 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"
#include "qla_gbl.h"

#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/smp_lock.h>
#include <linux/list.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>
#include <linux/delay.h>

void
qla2x00_vp_stop_timer(scsi_qla_host_t *vha)
{
	if (vha->vp_idx && vha->timer_active) {
		del_timer_sync(&vha->timer);
		vha->timer_active = 0;
	}
}

static uint32_t
qla24xx_allocate_vp_id(scsi_qla_host_t *vha)
{
	uint32_t vp_id;
	struct qla_hw_data *ha = vha->hw;

	/* Find an empty slot and assign an vp_id */
	mutex_lock(&ha->vport_lock);
	vp_id = find_first_zero_bit(ha->vp_idx_map, ha->max_npiv_vports + 1);
	if (vp_id > ha->max_npiv_vports) {
		DEBUG15(printk ("vp_id %d is bigger than max-supported %d.\n",
		    vp_id, ha->max_npiv_vports));
		mutex_unlock(&ha->vport_lock);
		return vp_id;
	}

	set_bit(vp_id, ha->vp_idx_map);
	ha->num_vhosts++;
	ha->cur_vport_count++;
	vha->vp_idx = vp_id;
	list_add_tail(&vha->list, &ha->vp_list);
	mutex_unlock(&ha->vport_lock);
	return vp_id;
}

void
qla24xx_deallocate_vp_id(scsi_qla_host_t *vha)
{
	uint16_t vp_id;
	struct qla_hw_data *ha = vha->hw;

	mutex_lock(&ha->vport_lock);
	vp_id = vha->vp_idx;
	ha->num_vhosts--;
	ha->cur_vport_count--;
	clear_bit(vp_id, ha->vp_idx_map);
	list_del(&vha->list);
	mutex_unlock(&ha->vport_lock);
}

static scsi_qla_host_t *
qla24xx_find_vhost_by_name(struct qla_hw_data *ha, uint8_t *port_name)
{
	scsi_qla_host_t *vha;

	/* Locate matching device in database. */
	list_for_each_entry(vha, &ha->vp_list, list) {
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

	list_for_each_entry(fcport, &vha->vp_fcports, list) {
		DEBUG15(printk("scsi(%ld): Marking port dead, "
		    "loop_id=0x%04x :%x\n",
		    vha->host_no, fcport->loop_id, fcport->vp_idx));

		qla2x00_mark_device_lost(vha, fcport, 0, 0);
		atomic_set(&fcport->state, FCS_UNCONFIGURED);
	}
}

int
qla24xx_disable_vp(scsi_qla_host_t *vha)
{
	int ret;

	ret = qla24xx_control_vp(vha, VCE_COMMAND_DISABLE_VPS_LOGO_ALL);
	atomic_set(&vha->loop_state, LOOP_DOWN);
	atomic_set(&vha->loop_down_timer, LOOP_DOWN_TIME);

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
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);

	/* Check if physical ha port is Up */
	if (atomic_read(&base_vha->loop_state) == LOOP_DOWN  ||
		atomic_read(&base_vha->loop_state) == LOOP_DEAD) {
		vha->vp_err_state =  VP_ERR_PORTDWN;
		fc_vport_set_state(vha->fc_vport, FC_VPORT_LINKDOWN);
		goto enable_failed;
	}

	/* Initialize the new vport unless it is a persistent port */
	mutex_lock(&ha->vport_lock);
	ret = qla24xx_modify_vp_config(vha);
	mutex_unlock(&ha->vport_lock);

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
		DEBUG15(qla_printk(KERN_ERR, vha->hw, "Failed to enable "
		    "receiving of RSCN requests: 0x%x\n", ret));
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
qla2x00_alert_all_vps(struct qla_hw_data *ha, uint16_t *mb)
{
	scsi_qla_host_t *vha;
	int i = 0;

	list_for_each_entry(vha, &ha->vp_list, list) {
		if (vha->vp_idx) {
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
				vha->host_no, __func__, i, *mb, vha));
				qla2x00_async_event(vha, mb);
				break;
			}
		}
		i++;
	}
}

int
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

	/* To exclusively reset vport, we need to log it out first.*/
	if (!test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags))
		qla24xx_control_vp(vha, VCE_COMMAND_DISABLE_VPS_LOGO_ALL);

	DEBUG15(printk("scsi(%ld): Scheduling enable of Vport %d...\n",
	    vha->host_no, vha->vp_idx));
	return qla24xx_enable_vp(vha);
}

static int
qla2x00_do_dpc_vp(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);

	if (test_and_clear_bit(VP_IDX_ACQUIRED, &vha->vp_flags)) {
		/* VP acquired. complete port configuration */
		if (atomic_read(&base_vha->loop_state) == LOOP_READY) {
			qla24xx_configure_vp(vha);
		} else {
			set_bit(VP_IDX_ACQUIRED, &vha->vp_flags);
			set_bit(VP_DPC_NEEDED, &base_vha->dpc_flags);
		}

		return 0;
	}

	if (test_bit(FCPORT_UPDATE_NEEDED, &vha->dpc_flags)) {
		qla2x00_update_fcports(vha);
		clear_bit(FCPORT_UPDATE_NEEDED, &vha->dpc_flags);
	}

	if ((test_and_clear_bit(RELOGIN_NEEDED, &vha->dpc_flags)) &&
		!test_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags) &&
		atomic_read(&vha->loop_state) != LOOP_DOWN) {

		DEBUG(printk("scsi(%ld): qla2x00_port_login()\n",
						vha->host_no));
		qla2x00_relogin(vha);

		DEBUG(printk("scsi(%ld): qla2x00_port_login - end\n",
							vha->host_no));
	}

	if (test_and_clear_bit(RESET_MARKER_NEEDED, &vha->dpc_flags) &&
	    (!(test_and_set_bit(RESET_ACTIVE, &vha->dpc_flags)))) {
		clear_bit(RESET_ACTIVE, &vha->dpc_flags);
	}

	if (atomic_read(&vha->vp_state) == VP_ACTIVE &&
	    test_and_clear_bit(LOOP_RESYNC_NEEDED, &vha->dpc_flags)) {
		if (!(test_and_set_bit(LOOP_RESYNC_ACTIVE, &vha->dpc_flags))) {
			qla2x00_loop_resync(vha);
			clear_bit(LOOP_RESYNC_ACTIVE, &vha->dpc_flags);
		}
	}

	return 0;
}

void
qla2x00_do_dpc_all_vps(scsi_qla_host_t *vha)
{
	int ret;
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *vp;

	if (vha->vp_idx)
		return;
	if (list_empty(&ha->vp_list))
		return;

	clear_bit(VP_DPC_NEEDED, &vha->dpc_flags);

	list_for_each_entry(vp, &ha->vp_list, list) {
		if (vp->vp_idx)
			ret = qla2x00_do_dpc_vp(vp);
	}
}

int
qla24xx_vport_create_req_sanity_check(struct fc_vport *fc_vport)
{
	scsi_qla_host_t *base_vha = shost_priv(fc_vport->shost);
	struct qla_hw_data *ha = base_vha->hw;
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
	if (!memcmp(port_name, base_vha->port_name, WWN_SIZE))
		return VPCERR_BAD_WWN;
	vha = qla24xx_find_vhost_by_name(ha, port_name);
	if (vha)
		return VPCERR_BAD_WWN;

	/* Check up max-npiv-supports */
	if (ha->num_vhosts > ha->max_npiv_vports) {
		DEBUG15(printk("scsi(%ld): num_vhosts %ud is bigger than "
		    "max_npv_vports %ud.\n", base_vha->host_no,
		    ha->num_vhosts, ha->max_npiv_vports));
		return VPCERR_UNSUPPORTED;
	}
	return 0;
}

scsi_qla_host_t *
qla24xx_create_vhost(struct fc_vport *fc_vport)
{
	scsi_qla_host_t *base_vha = shost_priv(fc_vport->shost);
	struct qla_hw_data *ha = base_vha->hw;
	scsi_qla_host_t *vha;
	struct scsi_host_template *sht = &qla24xx_driver_template;
	struct Scsi_Host *host;

	vha = qla2x00_create_host(sht, ha);
	if (!vha) {
		DEBUG(printk("qla2xxx: scsi_host_alloc() failed for vport\n"));
		return(NULL);
	}

	host = vha->host;
	fc_vport->dd_data = vha;

	/* New host info */
	u64_to_wwn(fc_vport->node_name, vha->node_name);
	u64_to_wwn(fc_vport->port_name, vha->port_name);

	vha->fc_vport = fc_vport;
	vha->device_flags = 0;
	vha->vp_idx = qla24xx_allocate_vp_id(vha);
	if (vha->vp_idx > ha->max_npiv_vports) {
		DEBUG15(printk("scsi(%ld): Couldn't allocate vp_id.\n",
			vha->host_no));
		goto create_vhost_failed;
	}
	vha->mgmt_svr_loop_id = 10 + vha->vp_idx;

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

	host->can_queue = ha->req->length + 128;
	host->this_id = 255;
	host->cmd_per_lun = 3;
	host->max_cmd_len = MAX_CMDSZ;
	host->max_channel = MAX_BUSES - 1;
	host->max_lun = MAX_LUNS;
	host->unique_id = host->host_no;
	host->max_id = MAX_TARGETS_2200;
	host->transportt = qla2xxx_transport_vport_template;

	DEBUG15(printk("DEBUG: detect vport hba %ld at address = %p\n",
	    vha->host_no, vha));

	vha->flags.init_done = 1;

	return vha;

create_vhost_failed:
	return NULL;
}
