/*
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
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

#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/smp_lock.h>
#include <linux/delay.h>

#include <scsi/scsi_tcq.h>
#include <scsi/scsicam.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_fc.h>

/*
 * Driver version
 */
char qla2x00_version_str[40];

/*
 * SRB allocation cache
 */
char srb_cachep_name[16];
kmem_cache_t *srb_cachep;

/*
 * Stats for all adpaters.
 */
struct _qla2x00stats qla2x00_stats;

/*
 * Ioctl related information.
 */
int num_hosts;
int apiHBAInstance;

/*
 * Module parameter information and variables
 */
int ql2xmaxqdepth;
module_param(ql2xmaxqdepth, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xmaxqdepth,
		"Maximum queue depth to report for target devices.");

int ql2xlogintimeout = 20;
module_param(ql2xlogintimeout, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xlogintimeout,
		"Login timeout value in seconds.");

int qlport_down_retry = 30;
module_param(qlport_down_retry, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(qlport_down_retry,
		"Maximum number of command retries to a port that returns"
		"a PORT-DOWN status.");

int ql2xretrycount = 20;
module_param(ql2xretrycount, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xretrycount,
		"Maximum number of mid-layer retries allowed for a command.  "
		"Default value is 20, ");

int ql2xplogiabsentdevice;
module_param(ql2xplogiabsentdevice, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xplogiabsentdevice,
		"Option to enable PLOGI to devices that are not present after "
		"a Fabric scan.  This is needed for several broken switches."
		"Default is 0 - no PLOGI. 1 - perfom PLOGI.");

int ql2xenablezio = 0;
module_param(ql2xenablezio, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xenablezio,
		"Option to enable ZIO:If 1 then enable it otherwise" 
		" use the default set in the NVRAM."
		" Default is 0 : disabled");

int ql2xintrdelaytimer = 10;
module_param(ql2xintrdelaytimer, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xintrdelaytimer,
		"ZIO: Waiting time for Firmware before it generates an "
		"interrupt to the host to notify completion of request.");

int ConfigRequired;
module_param(ConfigRequired, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ConfigRequired,
		"If 1, then only configured devices passed in through the"
		"ql2xopts parameter will be presented to the OS");

int Bind = BIND_BY_PORT_NAME;
module_param(Bind, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(Bind,
		"Target persistent binding method: "
		"0 by Portname (default); 1 by PortID; 2 by Nodename. ");

int ql2xsuspendcount = SUSPEND_COUNT;
module_param(ql2xsuspendcount, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xsuspendcount,
		"Number of 6-second suspend iterations to perform while a "
		"target returns a <NOT READY> status.  Default is 10 "
		"iterations.");

int ql2xloginretrycount = 0;
module_param(ql2xloginretrycount, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xloginretrycount,
		"Specify an alternate value for the NVRAM login retry count.");

static void qla2x00_free_device(scsi_qla_host_t *);

static void qla2x00_config_dma_addressing(scsi_qla_host_t *ha);

/*
 * SCSI host template entry points 
 */
static int qla2xxx_slave_configure(struct scsi_device * device);
static int qla2xxx_slave_alloc(struct scsi_device *);
static void qla2xxx_slave_destroy(struct scsi_device *);
static int qla2x00_queuecommand(struct scsi_cmnd *cmd,
		void (*fn)(struct scsi_cmnd *));
static int qla2xxx_eh_abort(struct scsi_cmnd *);
static int qla2xxx_eh_device_reset(struct scsi_cmnd *);
static int qla2xxx_eh_bus_reset(struct scsi_cmnd *);
static int qla2xxx_eh_host_reset(struct scsi_cmnd *);
static int qla2x00_loop_reset(scsi_qla_host_t *ha);
static int qla2x00_device_reset(scsi_qla_host_t *, fc_port_t *);

static struct scsi_host_template qla2x00_driver_template = {
	.module			= THIS_MODULE,
	.name			= "qla2xxx",
	.queuecommand		= qla2x00_queuecommand,

	.eh_abort_handler	= qla2xxx_eh_abort,
	.eh_device_reset_handler = qla2xxx_eh_device_reset,
	.eh_bus_reset_handler	= qla2xxx_eh_bus_reset,
	.eh_host_reset_handler	= qla2xxx_eh_host_reset,

	.slave_configure	= qla2xxx_slave_configure,

	.slave_alloc		= qla2xxx_slave_alloc,
	.slave_destroy		= qla2xxx_slave_destroy,
	.this_id		= -1,
	.cmd_per_lun		= 3,
	.use_clustering		= ENABLE_CLUSTERING,
	.sg_tablesize		= SG_ALL,

	/*
	 * The RISC allows for each command to transfer (2^32-1) bytes of data,
	 * which equates to 0x800000 sectors.
	 */
	.max_sectors		= 0xFFFF,
};

static struct scsi_transport_template *qla2xxx_transport_template = NULL;

/* TODO Convert to inlines
 *
 * Timer routines
 */
#define	WATCH_INTERVAL		1       /* number of seconds */

static void qla2x00_timer(scsi_qla_host_t *);

static __inline__ void qla2x00_start_timer(scsi_qla_host_t *,
    void *, unsigned long);
static __inline__ void qla2x00_restart_timer(scsi_qla_host_t *, unsigned long);
static __inline__ void qla2x00_stop_timer(scsi_qla_host_t *);

static inline void
qla2x00_start_timer(scsi_qla_host_t *ha, void *func, unsigned long interval)
{
	init_timer(&ha->timer);
	ha->timer.expires = jiffies + interval * HZ;
	ha->timer.data = (unsigned long)ha;
	ha->timer.function = (void (*)(unsigned long))func;
	add_timer(&ha->timer);
	ha->timer_active = 1;
}

static inline void
qla2x00_restart_timer(scsi_qla_host_t *ha, unsigned long interval)
{
	mod_timer(&ha->timer, jiffies + interval * HZ);
}

static __inline__ void
qla2x00_stop_timer(scsi_qla_host_t *ha)
{
	del_timer_sync(&ha->timer);
	ha->timer_active = 0;
}

static int qla2x00_do_dpc(void *data);

static void qla2x00_rst_aen(scsi_qla_host_t *);

static uint8_t qla2x00_mem_alloc(scsi_qla_host_t *);
static void qla2x00_mem_free(scsi_qla_host_t *ha);
static int qla2x00_allocate_sp_pool( scsi_qla_host_t *ha);
static void qla2x00_free_sp_pool(scsi_qla_host_t *ha);
static srb_t *qla2x00_get_new_sp(scsi_qla_host_t *);
static void qla2x00_sp_free_dma(scsi_qla_host_t *, srb_t *);
void qla2x00_sp_compl(scsi_qla_host_t *ha, srb_t *);

/* -------------------------------------------------------------------------- */

static char *
qla2x00_get_pci_info_str(struct scsi_qla_host *ha, char *str)
{
	static char *pci_bus_modes[] = {
		"33", "66", "100", "133",
	};
	uint16_t pci_bus;

	strcpy(str, "PCI");
	pci_bus = (ha->pci_attr & (BIT_9 | BIT_10)) >> 9;
	if (pci_bus) {
		strcat(str, "-X (");
		strcat(str, pci_bus_modes[pci_bus]);
	} else {
		pci_bus = (ha->pci_attr & BIT_8) >> 8;
		strcat(str, " (");
		strcat(str, pci_bus_modes[pci_bus]);
	}
	strcat(str, " MHz)");

	return (str);
}

char *
qla2x00_get_fw_version_str(struct scsi_qla_host *ha, char *str)
{
	char un_str[10];
	
	sprintf(str, "%d.%02d.%02d ", ha->fw_major_version,
	    ha->fw_minor_version,
	    ha->fw_subminor_version);

	if (ha->fw_attributes & BIT_9) {
		strcat(str, "FLX");
		return (str);
	}

	switch (ha->fw_attributes & 0xFF) {
	case 0x7:
		strcat(str, "EF");
		break;
	case 0x17:
		strcat(str, "TP");
		break;
	case 0x37:
		strcat(str, "IP");
		break;
	case 0x77:
		strcat(str, "VI");
		break;
	default:
		sprintf(un_str, "(%x)", ha->fw_attributes);
		strcat(str, un_str);
		break;
	}
	if (ha->fw_attributes & 0x100)
		strcat(str, "X");

	return (str);
}

/**************************************************************************
* qla2x00_queuecommand
*
* Description:
*     Queue a command to the controller.
*
* Input:
*     cmd - pointer to Scsi cmd structure
*     fn - pointer to Scsi done function
*
* Returns:
*   0 - Always
*
* Note:
* The mid-level driver tries to ensures that queuecommand never gets invoked
* concurrently with itself or the interrupt handler (although the
* interrupt handler may call this routine as part of request-completion
* handling).
**************************************************************************/
static int
qla2x00_queuecommand(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	scsi_qla_host_t *ha = to_qla_host(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	srb_t *sp;
	int rval;

	if (!fcport) {
		cmd->result = DID_NO_CONNECT << 16;
		goto qc_fail_command;
	}

	if (atomic_read(&fcport->state) != FCS_ONLINE) {
		if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD ||
		    atomic_read(&ha->loop_state) == LOOP_DEAD) {
			cmd->result = DID_NO_CONNECT << 16;
			goto qc_fail_command;
		}
		goto qc_host_busy;
	}

	spin_unlock_irq(ha->host->host_lock);

	/* Allocate a command packet from the "sp" pool. */
	if ((sp = qla2x00_get_new_sp(ha)) == NULL) {
		goto qc_host_busy_lock;
	}

	sp->ha = ha;
	sp->fcport = fcport;
	sp->cmd = cmd;
	sp->flags = 0;
	sp->err_id = 0;

	CMD_SP(cmd) = (void *)sp;
	cmd->scsi_done = done;

	rval = qla2x00_start_scsi(sp);
	if (rval != QLA_SUCCESS)
		goto qc_host_busy_free_sp;

	/* Manage unprocessed RIO/ZIO commands in response queue. */
	if (ha->flags.online && ha->flags.process_response_queue &&
	    ha->response_ring_ptr->signature != RESPONSE_PROCESSED) {
		unsigned long flags;

		spin_lock_irqsave(&ha->hardware_lock, flags);
		qla2x00_process_response_queue(ha);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

	spin_lock_irq(ha->host->host_lock);

	return 0;

qc_host_busy_free_sp:
	qla2x00_sp_free_dma(ha, sp);
	CMD_SP(cmd) = NULL;
	mempool_free(sp, ha->srb_mempool);

qc_host_busy_lock:
	spin_lock_irq(ha->host->host_lock);

qc_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

qc_fail_command:
	done(cmd);

	return 0;
}

/*
 * qla2x00_eh_wait_on_command
 *    Waits for the command to be returned by the Firmware for some
 *    max time.
 *
 * Input:
 *    ha = actual ha whose done queue will contain the command
 *	      returned by firmware.
 *    cmd = Scsi Command to wait on.
 *    flag = Abort/Reset(Bus or Device Reset)
 *
 * Return:
 *    Not Found : 0
 *    Found : 1
 */
static int
qla2x00_eh_wait_on_command(scsi_qla_host_t *ha, struct scsi_cmnd *cmd)
{
#define ABORT_POLLING_PERIOD	HZ
#define ABORT_WAIT_ITER		((10 * HZ) / (ABORT_POLLING_PERIOD))
	unsigned long wait_iter = ABORT_WAIT_ITER;
	int ret = QLA_SUCCESS;

	while (CMD_SP(cmd)) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(ABORT_POLLING_PERIOD);

		if (--wait_iter)
			break;
	}
	if (CMD_SP(cmd))
		ret = QLA_FUNCTION_FAILED;

	return ret;
}

/*
 * qla2x00_wait_for_hba_online
 *    Wait till the HBA is online after going through 
 *    <= MAX_RETRIES_OF_ISP_ABORT  or
 *    finally HBA is disabled ie marked offline
 *
 * Input:
 *     ha - pointer to host adapter structure
 * 
 * Note:    
 *    Does context switching-Release SPIN_LOCK
 *    (if any) before calling this routine.
 *
 * Return:
 *    Success (Adapter is online) : 0
 *    Failed  (Adapter is offline/disabled) : 1
 */
static int 
qla2x00_wait_for_hba_online(scsi_qla_host_t *ha)
{
	int 	 return_status;
	unsigned long wait_online;

	wait_online = jiffies + (MAX_LOOP_TIMEOUT * HZ); 
	while (((test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags)) ||
	    test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags) ||
	    test_bit(ISP_ABORT_RETRY, &ha->dpc_flags) ||
	    ha->dpc_active) && time_before(jiffies, wait_online)) {

		msleep(1000);
	}
	if (ha->flags.online) 
		return_status = QLA_SUCCESS; 
	else
		return_status = QLA_FUNCTION_FAILED;

	DEBUG2(printk("%s return_status=%d\n",__func__,return_status));

	return (return_status);
}

/*
 * qla2x00_wait_for_loop_ready
 *    Wait for MAX_LOOP_TIMEOUT(5 min) value for loop
 *    to be in LOOP_READY state.	 
 * Input:
 *     ha - pointer to host adapter structure
 * 
 * Note:    
 *    Does context switching-Release SPIN_LOCK
 *    (if any) before calling this routine.
 *    
 *
 * Return:
 *    Success (LOOP_READY) : 0
 *    Failed  (LOOP_NOT_READY) : 1
 */
static inline int 
qla2x00_wait_for_loop_ready(scsi_qla_host_t *ha)
{
	int 	 return_status = QLA_SUCCESS;
	unsigned long loop_timeout ;

	/* wait for 5 min at the max for loop to be ready */
	loop_timeout = jiffies + (MAX_LOOP_TIMEOUT * HZ); 

	while ((!atomic_read(&ha->loop_down_timer) &&
	    atomic_read(&ha->loop_state) == LOOP_DOWN) ||
	    test_bit(CFG_ACTIVE, &ha->cfg_flags) ||
	    atomic_read(&ha->loop_state) != LOOP_READY) {
		msleep(1000);
		if (time_after_eq(jiffies, loop_timeout)) {
			return_status = QLA_FUNCTION_FAILED;
			break;
		}
	}
	return (return_status);	
}

/**************************************************************************
* qla2xxx_eh_abort
*
* Description:
*    The abort function will abort the specified command.
*
* Input:
*    cmd = Linux SCSI command packet to be aborted.
*
* Returns:
*    Either SUCCESS or FAILED.
*
* Note:
**************************************************************************/
int
qla2xxx_eh_abort(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *ha = to_qla_host(cmd->device->host);
	srb_t *sp;
	int ret, i;
	unsigned int id, lun;
	unsigned long serial;
	unsigned long flags;

	if (!CMD_SP(cmd))
		return FAILED;

	ret = FAILED;

	id = cmd->device->id;
	lun = cmd->device->lun;
	serial = cmd->serial_number;

	/* Check active list for command command. */
	spin_unlock_irq(ha->host->host_lock);
	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (i = 1; i < MAX_OUTSTANDING_COMMANDS; i++) {
		sp = ha->outstanding_cmds[i];

		if (sp == NULL)
			continue;

		if (sp->cmd != cmd)
			continue;

		DEBUG2(printk("%s(%ld): aborting sp %p from RISC. pid=%ld "
		    "sp->state=%x\n", __func__, ha->host_no, sp, serial,
		    sp->state));
		DEBUG3(qla2x00_print_scsi_cmd(cmd);)

		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		if (qla2x00_abort_command(ha, sp)) {
			DEBUG2(printk("%s(%ld): abort_command "
			    "mbx failed.\n", __func__, ha->host_no));
		} else {
			DEBUG3(printk("%s(%ld): abort_command "
			    "mbx success.\n", __func__, ha->host_no));
			ret = SUCCESS;
		}
		spin_lock_irqsave(&ha->hardware_lock, flags);

		break;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Wait for the command to be returned. */
	if (ret == SUCCESS) {
		if (qla2x00_eh_wait_on_command(ha, cmd) != QLA_SUCCESS) {
			qla_printk(KERN_ERR, ha, 
			    "scsi(%ld:%d:%d): Abort handler timed out -- %lx "
			    "%x.\n", ha->host_no, id, lun, serial, ret);
		}
	}
	spin_lock_irq(ha->host->host_lock);

	qla_printk(KERN_INFO, ha, 
	    "scsi(%ld:%d:%d): Abort command issued -- %lx %x.\n", ha->host_no,
	    id, lun, serial, ret);

	return ret;
}

/**************************************************************************
* qla2x00_eh_wait_for_pending_target_commands
*
* Description:
*    Waits for all the commands to come back from the specified target.
*
* Input:
*    ha - pointer to scsi_qla_host structure.
*    t  - target 	
* Returns:
*    Either SUCCESS or FAILED.
*
* Note:
**************************************************************************/
static int
qla2x00_eh_wait_for_pending_target_commands(scsi_qla_host_t *ha, unsigned int t)
{
	int	cnt;
	int	status;
	srb_t		*sp;
	struct scsi_cmnd *cmd;
	unsigned long flags;

	status = 0;

	/*
	 * Waiting for all commands for the designated target in the active
	 * array
	 */
	for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		sp = ha->outstanding_cmds[cnt];
		if (sp) {
			cmd = sp->cmd;
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			if (cmd->device->id == t) {
				if (!qla2x00_eh_wait_on_command(ha, cmd)) {
					status = 1;
					break;
				}
			}
		} else {
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
		}
	}
	return (status);
}


/**************************************************************************
* qla2xxx_eh_device_reset
*
* Description:
*    The device reset function will reset the target and abort any
*    executing commands.
*
*    NOTE: The use of SP is undefined within this context.  Do *NOT*
*          attempt to use this value, even if you determine it is 
*          non-null.
*
* Input:
*    cmd = Linux SCSI command packet of the command that cause the
*          bus device reset.
*
* Returns:
*    SUCCESS/FAILURE (defined as macro in scsi.h).
*
**************************************************************************/
int
qla2xxx_eh_device_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *ha = to_qla_host(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	srb_t *sp;
	int ret;
	unsigned int id, lun;
	unsigned long serial;

	ret = FAILED;

	id = cmd->device->id;
	lun = cmd->device->lun;
	serial = cmd->serial_number;

	sp = (srb_t *) CMD_SP(cmd);
	if (!sp || !fcport)
		return ret;

	qla_printk(KERN_INFO, ha,
	    "scsi(%ld:%d:%d): DEVICE RESET ISSUED.\n", ha->host_no, id, lun);

	spin_unlock_irq(ha->host->host_lock);

	if (qla2x00_wait_for_hba_online(ha) != QLA_SUCCESS) {
		spin_lock_irq(ha->host->host_lock);
		goto eh_dev_reset_done;
	}

	if (qla2x00_wait_for_loop_ready(ha) == QLA_SUCCESS) {
		if (qla2x00_device_reset(ha, fcport) == 0)
			ret = SUCCESS;

#if defined(LOGOUT_AFTER_DEVICE_RESET)
		if (ret == SUCCESS) {
			if (fcport->flags & FC_FABRIC_DEVICE) {
				qla2x00_fabric_logout(ha, fcport->loop_id);
				qla2x00_mark_device_lost(ha, fcport);
			}
		}
#endif
	} else {
		DEBUG2(printk(KERN_INFO
		    "%s failed: loop not ready\n",__func__);)
	}

	if (ret == FAILED) {
		DEBUG3(printk("%s(%ld): device reset failed\n",
		    __func__, ha->host_no));
		qla_printk(KERN_INFO, ha, "%s: device reset failed\n",
		    __func__);

		goto eh_dev_reset_done;
	}

	/*
	 * If we are coming down the EH path, wait for all commands to
	 * complete for the device.
	 */
	if (cmd->device->host->eh_active) {
		if (qla2x00_eh_wait_for_pending_target_commands(ha, id))
			ret = FAILED;

		if (ret == FAILED) {
			DEBUG3(printk("%s(%ld): failed while waiting for "
			    "commands\n", __func__, ha->host_no));
			qla_printk(KERN_INFO, ha,
			    "%s: failed while waiting for commands\n",
			    __func__); 

			goto eh_dev_reset_done;
		}
	}

	qla_printk(KERN_INFO, ha,
	    "scsi(%ld:%d:%d): DEVICE RESET SUCCEEDED.\n", ha->host_no, id, lun);

eh_dev_reset_done:
	spin_lock_irq(ha->host->host_lock);

	return ret;
}

/**************************************************************************
* qla2x00_eh_wait_for_pending_commands
*
* Description:
*    Waits for all the commands to come back from the specified host.
*
* Input:
*    ha - pointer to scsi_qla_host structure.
*
* Returns:
*    1 : SUCCESS
*    0 : FAILED
*
* Note:
**************************************************************************/
static int
qla2x00_eh_wait_for_pending_commands(scsi_qla_host_t *ha)
{
	int	cnt;
	int	status;
	srb_t		*sp;
	struct scsi_cmnd *cmd;
	unsigned long flags;

	status = 1;

	/*
	 * Waiting for all commands for the designated target in the active
	 * array
	 */
	for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		sp = ha->outstanding_cmds[cnt];
		if (sp) {
			cmd = sp->cmd;
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			status = qla2x00_eh_wait_on_command(ha, cmd);
			if (status == 0)
				break;
		}
		else {
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
		}
	}
	return (status);
}


/**************************************************************************
* qla2xxx_eh_bus_reset
*
* Description:
*    The bus reset function will reset the bus and abort any executing
*    commands.
*
* Input:
*    cmd = Linux SCSI command packet of the command that cause the
*          bus reset.
*
* Returns:
*    SUCCESS/FAILURE (defined as macro in scsi.h).
*
**************************************************************************/
int
qla2xxx_eh_bus_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *ha = to_qla_host(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	srb_t *sp;
	int ret;
	unsigned int id, lun;
	unsigned long serial;

	ret = FAILED;

	id = cmd->device->id;
	lun = cmd->device->lun;
	serial = cmd->serial_number;

	sp = (srb_t *) CMD_SP(cmd);
	if (!sp || !fcport)
		return ret;

	qla_printk(KERN_INFO, ha,
	    "scsi(%ld:%d:%d): LOOP RESET ISSUED.\n", ha->host_no, id, lun);

	spin_unlock_irq(ha->host->host_lock);

	if (qla2x00_wait_for_hba_online(ha) != QLA_SUCCESS) {
		DEBUG2(printk("%s failed:board disabled\n",__func__));
		goto eh_bus_reset_done;
	}

	if (qla2x00_wait_for_loop_ready(ha) == QLA_SUCCESS) {
		if (qla2x00_loop_reset(ha) == QLA_SUCCESS)
			ret = SUCCESS;
	}
	if (ret == FAILED)
		goto eh_bus_reset_done;

	/* Waiting for our command in done_queue to be returned to OS.*/
	if (cmd->device->host->eh_active)
		if (!qla2x00_eh_wait_for_pending_commands(ha))
			ret = FAILED;

eh_bus_reset_done:
	qla_printk(KERN_INFO, ha, "%s: reset %s\n", __func__,
	    (ret == FAILED) ? "failed" : "succeded");

	spin_lock_irq(ha->host->host_lock);

	return ret;
}

/**************************************************************************
* qla2xxx_eh_host_reset
*
* Description:
*    The reset function will reset the Adapter.
*
* Input:
*      cmd = Linux SCSI command packet of the command that cause the
*            adapter reset.
*
* Returns:
*      Either SUCCESS or FAILED.
*
* Note:
**************************************************************************/
int
qla2xxx_eh_host_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *ha = to_qla_host(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	srb_t *sp;
	int ret;
	unsigned int id, lun;
	unsigned long serial;

	ret = FAILED;

	id = cmd->device->id;
	lun = cmd->device->lun;
	serial = cmd->serial_number;

	sp = (srb_t *) CMD_SP(cmd);
	if (!sp || !fcport)
		return ret;

	qla_printk(KERN_INFO, ha,
	    "scsi(%ld:%d:%d): ADAPTER RESET ISSUED.\n", ha->host_no, id, lun);

	spin_unlock_irq(ha->host->host_lock);

	if (qla2x00_wait_for_hba_online(ha) != QLA_SUCCESS)
		goto eh_host_reset_lock;

	/*
	 * Fixme-may be dpc thread is active and processing
	 * loop_resync,so wait a while for it to 
	 * be completed and then issue big hammer.Otherwise
	 * it may cause I/O failure as big hammer marks the
	 * devices as lost kicking of the port_down_timer
	 * while dpc is stuck for the mailbox to complete.
	 */
	qla2x00_wait_for_loop_ready(ha);
	set_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
	if (qla2x00_abort_isp(ha)) {
		clear_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
		/* failed. schedule dpc to try */
		set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);

		if (qla2x00_wait_for_hba_online(ha) != QLA_SUCCESS)
			goto eh_host_reset_lock;
	} 
	clear_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);

	/* Waiting for our command in done_queue to be returned to OS.*/
	if (qla2x00_eh_wait_for_pending_commands(ha))
		ret = SUCCESS;

eh_host_reset_lock:
	spin_lock_irq(ha->host->host_lock);

	qla_printk(KERN_INFO, ha, "%s: reset %s\n", __func__,
	    (ret == FAILED) ? "failed" : "succeded");

	return ret;
}

/*
* qla2x00_loop_reset
*      Issue loop reset.
*
* Input:
*      ha = adapter block pointer.
*
* Returns:
*      0 = success
*/
static int
qla2x00_loop_reset(scsi_qla_host_t *ha)
{
	int status = QLA_SUCCESS;
	struct fc_port *fcport;

	if (ha->flags.enable_lip_reset) {
		status = qla2x00_lip_reset(ha);
	}

	if (status == QLA_SUCCESS && ha->flags.enable_target_reset) {
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (fcport->port_type != FCT_TARGET)
				continue;

			status = qla2x00_target_reset(ha, fcport);
			if (status != QLA_SUCCESS)
				break;
		}
	}

	if (status == QLA_SUCCESS &&
		((!ha->flags.enable_target_reset && 
		  !ha->flags.enable_lip_reset) ||
		ha->flags.enable_lip_full_login)) {

		status = qla2x00_full_login_lip(ha);
	}

	/* Issue marker command only when we are going to start the I/O */
	ha->marker_needed = 1;

	if (status) {
		/* Empty */
		DEBUG2_3(printk("%s(%ld): **** FAILED ****\n",
				__func__,
				ha->host_no);)
	} else {
		/* Empty */
		DEBUG3(printk("%s(%ld): exiting normally.\n",
				__func__,
				ha->host_no);)
	}

	return(status);
}

/*
 * qla2x00_device_reset
 *	Issue bus device reset message to the target.
 *
 * Input:
 *	ha = adapter block pointer.
 *	t = SCSI ID.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_device_reset(scsi_qla_host_t *ha, fc_port_t *reset_fcport)
{
	/* Abort Target command will clear Reservation */
	return qla2x00_abort_target(reset_fcport);
}

static int
qla2xxx_slave_alloc(struct scsi_device *sdev)
{
	scsi_qla_host_t *ha = to_qla_host(sdev->host);
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));
	fc_port_t *fcport;
	int found;

	if (!rport)
		return -ENXIO;

	found = 0;
	list_for_each_entry(fcport, &ha->fcports, list) {
		if (rport->port_name ==
		    be64_to_cpu(*(uint64_t *)fcport->port_name)) {
			found++;
			break;
		}
	}
	if (!found)
		return -ENXIO;

	sdev->hostdata = fcport;

	return 0;
}

static int
qla2xxx_slave_configure(struct scsi_device *sdev)
{
	scsi_qla_host_t *ha = to_qla_host(sdev->host);
	struct fc_rport *rport = starget_to_rport(sdev->sdev_target);

	if (sdev->tagged_supported)
		scsi_activate_tcq(sdev, 32);
	else
		scsi_deactivate_tcq(sdev, 32);

	rport->dev_loss_tmo = ha->port_down_retry_count + 5;

	return 0;
}

static void
qla2xxx_slave_destroy(struct scsi_device *sdev)
{
	sdev->hostdata = NULL;
}

/**
 * qla2x00_config_dma_addressing() - Configure OS DMA addressing method.
 * @ha: HA context
 *
 * At exit, the @ha's flags.enable_64bit_addressing set to indicated
 * supported addressing method.
 */
static void
qla2x00_config_dma_addressing(scsi_qla_host_t *ha)
{
	/* Assume 32bit DMA address */
	ha->flags.enable_64bit_addressing = 0;
	ha->calc_request_entries = qla2x00_calc_iocbs_32;
	ha->build_scsi_iocbs = qla2x00_build_scsi_iocbs_32;

	/*
	 * Given the two variants pci_set_dma_mask(), allow the compiler to
	 * assist in setting the proper dma mask.
	 */
	if (sizeof(dma_addr_t) > 4) {
		if (pci_set_dma_mask(ha->pdev, DMA_64BIT_MASK) == 0) {
			ha->flags.enable_64bit_addressing = 1;
			ha->calc_request_entries = qla2x00_calc_iocbs_64;
			ha->build_scsi_iocbs = qla2x00_build_scsi_iocbs_64;

			if (pci_set_consistent_dma_mask(ha->pdev,
			    DMA_64BIT_MASK)) {
				qla_printk(KERN_DEBUG, ha, 
				    "Failed to set 64 bit PCI consistent mask; "
				    "using 32 bit.\n");
				pci_set_consistent_dma_mask(ha->pdev,
				    DMA_32BIT_MASK);
			}
		} else {
			qla_printk(KERN_DEBUG, ha,
			    "Failed to set 64 bit PCI DMA mask, falling back "
			    "to 32 bit MASK.\n");
			pci_set_dma_mask(ha->pdev, DMA_32BIT_MASK);
		}
	} else {
		pci_set_dma_mask(ha->pdev, DMA_32BIT_MASK);
	}
}

static int
qla2x00_iospace_config(scsi_qla_host_t *ha)
{
	unsigned long	pio, pio_len, pio_flags;
	unsigned long	mmio, mmio_len, mmio_flags;

	/* We only need PIO for Flash operations on ISP2312 v2 chips. */
	pio = pci_resource_start(ha->pdev, 0);
	pio_len = pci_resource_len(ha->pdev, 0);
	pio_flags = pci_resource_flags(ha->pdev, 0);
	if (pio_flags & IORESOURCE_IO) {
		if (pio_len < MIN_IOBASE_LEN) {
			qla_printk(KERN_WARNING, ha,
			    "Invalid PCI I/O region size (%s)...\n",
				pci_name(ha->pdev));
			pio = 0;
		}
	} else {
		qla_printk(KERN_WARNING, ha,
		    "region #0 not a PIO resource (%s)...\n",
		    pci_name(ha->pdev));
		pio = 0;
	}

	/* Use MMIO operations for all accesses. */
	mmio = pci_resource_start(ha->pdev, 1);
	mmio_len = pci_resource_len(ha->pdev, 1);
	mmio_flags = pci_resource_flags(ha->pdev, 1);

	if (!(mmio_flags & IORESOURCE_MEM)) {
		qla_printk(KERN_ERR, ha,
		    "region #0 not an MMIO resource (%s), aborting\n",
		    pci_name(ha->pdev));
		goto iospace_error_exit;
	}
	if (mmio_len < MIN_IOBASE_LEN) {
		qla_printk(KERN_ERR, ha,
		    "Invalid PCI mem region size (%s), aborting\n",
			pci_name(ha->pdev));
		goto iospace_error_exit;
	}

	if (pci_request_regions(ha->pdev, ha->brd_info->drv_name)) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to reserve PIO/MMIO regions (%s)\n",
		    pci_name(ha->pdev));

		goto iospace_error_exit;
	}

	ha->pio_address = pio;
	ha->pio_length = pio_len;
	ha->iobase = ioremap(mmio, MIN_IOBASE_LEN);
	if (!ha->iobase) {
		qla_printk(KERN_ERR, ha,
		    "cannot remap MMIO (%s), aborting\n", pci_name(ha->pdev));

		goto iospace_error_exit;
	}

	return (0);

iospace_error_exit:
	return (-ENOMEM);
}

/*
 * PCI driver interface
 */
int qla2x00_probe_one(struct pci_dev *pdev, struct qla_board_info *brd_info)
{
	int	ret = -ENODEV;
	device_reg_t __iomem *reg;
	struct Scsi_Host *host;
	scsi_qla_host_t *ha;
	unsigned long	flags = 0;
	unsigned long	wait_switch = 0;
	char pci_info[20];
	char fw_str[30];
	fc_port_t *fcport;

	if (pci_enable_device(pdev))
		goto probe_out;

	host = scsi_host_alloc(&qla2x00_driver_template,
	    sizeof(scsi_qla_host_t));
	if (host == NULL) {
		printk(KERN_WARNING
		    "qla2xxx: Couldn't allocate host from scsi layer!\n");
		goto probe_disable_device;
	}

	/* Clear our data area */
	ha = (scsi_qla_host_t *)host->hostdata;
	memset(ha, 0, sizeof(scsi_qla_host_t));

	ha->pdev = pdev;
	ha->host = host;
	ha->host_no = host->host_no;
	ha->brd_info = brd_info;
	sprintf(ha->host_str, "%s_%ld", ha->brd_info->drv_name, ha->host_no);

	/* Configure PCI I/O space */
	ret = qla2x00_iospace_config(ha);
	if (ret)
		goto probe_failed;

	/* Sanitize the information from PCI BIOS. */
	host->irq = pdev->irq;

	qla_printk(KERN_INFO, ha,
	    "Found an %s, irq %d, iobase 0x%p\n", ha->brd_info->isp_name,
	    host->irq, ha->iobase);

	spin_lock_init(&ha->hardware_lock);

	/* 4.23 Initialize /proc/scsi/qla2x00 counters */
	ha->actthreads = 0;
	ha->qthreads   = 0;
	ha->total_isr_cnt = 0;
	ha->total_isp_aborts = 0;
	ha->total_lip_cnt = 0;
	ha->total_dev_errs = 0;
	ha->total_ios = 0;
	ha->total_bytes = 0;

	ha->prev_topology = 0;
	ha->ports = MAX_BUSES;

	if (IS_QLA2100(ha)) {
		ha->max_targets = MAX_TARGETS_2100;
		ha->mbx_count = MAILBOX_REGISTER_COUNT_2100;
		ha->request_q_length = REQUEST_ENTRY_CNT_2100;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2100;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2100;
		host->sg_tablesize = 32;
	} else if (IS_QLA2200(ha)) {
		ha->max_targets = MAX_TARGETS_2200;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		ha->request_q_length = REQUEST_ENTRY_CNT_2200;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2100;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2100;
	} else /*if (IS_QLA2300(ha))*/ {
		ha->max_targets = MAX_TARGETS_2200;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		ha->request_q_length = REQUEST_ENTRY_CNT_2200;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2300;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2300;
	}
	host->can_queue = ha->request_q_length + 128;

	/* load the F/W, read paramaters, and init the H/W */
	ha->instance = num_hosts;

	init_MUTEX(&ha->mbx_cmd_sem);
	init_MUTEX_LOCKED(&ha->mbx_intr_sem);

	INIT_LIST_HEAD(&ha->list);
	INIT_LIST_HEAD(&ha->fcports);
	INIT_LIST_HEAD(&ha->rscn_fcports);

	/*
	 * These locks are used to prevent more than one CPU
	 * from modifying the queue at the same time. The
	 * higher level "host_lock" will reduce most
	 * contention for these locks.
	 */
	spin_lock_init(&ha->mbx_reg_lock);

	ha->dpc_pid = -1;
	init_completion(&ha->dpc_inited);
	init_completion(&ha->dpc_exited);

	qla2x00_config_dma_addressing(ha);
	if (qla2x00_mem_alloc(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "[ERROR] Failed to allocate memory for adapter\n");

		ret = -ENOMEM;
		goto probe_failed;
	}

	if (qla2x00_initialize_adapter(ha) &&
	    !(ha->device_flags & DFLG_NO_CABLE)) {

		qla_printk(KERN_WARNING, ha,
		    "Failed to initialize adapter\n");

		DEBUG2(printk("scsi(%ld): Failed to initialize adapter - "
		    "Adapter flags %x.\n",
		    ha->host_no, ha->device_flags));

		ret = -ENODEV;
		goto probe_failed;
	}

	/*
	 * Startup the kernel thread for this host adapter
	 */
	ha->dpc_should_die = 0;
	ha->dpc_pid = kernel_thread(qla2x00_do_dpc, ha, 0);
	if (ha->dpc_pid < 0) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to start DPC thread!\n");

		ret = -ENODEV;
		goto probe_failed;
	}
	wait_for_completion(&ha->dpc_inited);

	host->this_id = 255;
	host->cmd_per_lun = 3;
	host->unique_id = ha->instance;
	host->max_cmd_len = MAX_CMDSZ;
	host->max_channel = ha->ports - 1;
	host->max_lun = MAX_LUNS;
	host->transportt = qla2xxx_transport_template;

	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		ret = request_irq(host->irq, qla2100_intr_handler,
		    SA_INTERRUPT|SA_SHIRQ, ha->brd_info->drv_name, ha);
	else
		ret = request_irq(host->irq, qla2300_intr_handler,
		    SA_INTERRUPT|SA_SHIRQ, ha->brd_info->drv_name, ha);
	if (ret) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to reserve interrupt %d already in use.\n",
		    host->irq);
		goto probe_failed;
	}

	/* Initialized the timer */
	qla2x00_start_timer(ha, qla2x00_timer, WATCH_INTERVAL);

	DEBUG2(printk("DEBUG: detect hba %ld at address = %p\n",
	    ha->host_no, ha));

	reg = ha->iobase;

	/* Disable ISP interrupts. */
	qla2x00_disable_intrs(ha);

	/* Ensure mailbox registers are free. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	WRT_REG_WORD(&reg->semaphore, 0);
	WRT_REG_WORD(&reg->hccr, HCCR_CLR_RISC_INT);
	WRT_REG_WORD(&reg->hccr, HCCR_CLR_HOST_INT);

	/* Enable proper parity */
	if (!IS_QLA2100(ha) && !IS_QLA2200(ha)) {
		if (IS_QLA2300(ha))
			/* SRAM parity */
			WRT_REG_WORD(&reg->hccr, (HCCR_ENABLE_PARITY + 0x1));
		else
			/* SRAM, Instruction RAM and GP RAM parity */
			WRT_REG_WORD(&reg->hccr, (HCCR_ENABLE_PARITY + 0x7));
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Enable chip interrupts. */
	qla2x00_enable_intrs(ha);

	/* v2.19.5b6 */
	/*
	 * Wait around max loop_reset_delay secs for the devices to come
	 * on-line. We don't want Linux scanning before we are ready.
	 *
	 */
	for (wait_switch = jiffies + (ha->loop_reset_delay * HZ);
	    time_before(jiffies,wait_switch) &&
	     !(ha->device_flags & (DFLG_NO_CABLE | DFLG_FABRIC_DEVICES))
	     && (ha->device_flags & SWITCH_FOUND) ;) {

		qla2x00_check_fabric_devices(ha);

		msleep(10);
	}

	pci_set_drvdata(pdev, ha);
	ha->flags.init_done = 1;
	num_hosts++;

	ret = scsi_add_host(host, &pdev->dev);
	if (ret)
		goto probe_failed;

	qla2x00_alloc_sysfs_attr(ha);

	qla2x00_init_host_attr(ha);

	qla_printk(KERN_INFO, ha, "\n"
	    " QLogic Fibre Channel HBA Driver: %s\n"
	    "  QLogic %s - %s\n"
	    "  %s: %s @ %s hdma%c, host#=%ld, fw=%s\n", qla2x00_version_str,
	    ha->model_number, ha->model_desc ? ha->model_desc: "",
	    ha->brd_info->isp_name, qla2x00_get_pci_info_str(ha, pci_info),
	    pci_name(ha->pdev), ha->flags.enable_64bit_addressing ? '+': '-',
	    ha->host_no, qla2x00_get_fw_version_str(ha, fw_str));

	/* Go with fc_rport registration. */
	list_for_each_entry(fcport, &ha->fcports, list)
		qla2x00_reg_remote_port(ha, fcport);

	return 0;

probe_failed:
	fc_remove_host(ha->host);

	qla2x00_free_device(ha);

	scsi_host_put(host);

probe_disable_device:
	pci_disable_device(pdev);

probe_out:
	return ret;
}
EXPORT_SYMBOL_GPL(qla2x00_probe_one);

void qla2x00_remove_one(struct pci_dev *pdev)
{
	scsi_qla_host_t *ha;

	ha = pci_get_drvdata(pdev);

	qla2x00_free_sysfs_attr(ha);

	fc_remove_host(ha->host);

	scsi_remove_host(ha->host);

	qla2x00_free_device(ha);

	scsi_host_put(ha->host);

	pci_set_drvdata(pdev, NULL);
}
EXPORT_SYMBOL_GPL(qla2x00_remove_one);

static void
qla2x00_free_device(scsi_qla_host_t *ha)
{
	int ret;

	/* Abort any outstanding IO descriptors. */
	if (!IS_QLA2100(ha) && !IS_QLA2200(ha))
		qla2x00_cancel_io_descriptors(ha);

	/* turn-off interrupts on the card */
	if (ha->interrupts_on)
		qla2x00_disable_intrs(ha);

	/* Disable timer */
	if (ha->timer_active)
		qla2x00_stop_timer(ha);

	/* Kill the kernel thread for this host */
	if (ha->dpc_pid >= 0) {
		ha->dpc_should_die = 1;
		wmb();
		ret = kill_proc(ha->dpc_pid, SIGHUP, 1);
		if (ret) {
			qla_printk(KERN_ERR, ha,
			    "Unable to signal DPC thread -- (%d)\n", ret);

			/* TODO: SOMETHING MORE??? */
		} else {
			wait_for_completion(&ha->dpc_exited);
		}
	}

	qla2x00_mem_free(ha);


	ha->flags.online = 0;

	/* Detach interrupts */
	if (ha->pdev->irq)
		free_irq(ha->pdev->irq, ha);

	/* release io space registers  */
	if (ha->iobase)
		iounmap(ha->iobase);
	pci_release_regions(ha->pdev);

	pci_disable_device(ha->pdev);
}

/*
 * qla2x00_mark_device_lost Updates fcport state when device goes offline.
 *
 * Input: ha = adapter block pointer.  fcport = port structure pointer.
 *
 * Return: None.
 *
 * Context:
 */
void qla2x00_mark_device_lost(scsi_qla_host_t *ha, fc_port_t *fcport,
    int do_login)
{
	if (atomic_read(&fcport->state) == FCS_ONLINE && fcport->rport)
		fc_remote_port_block(fcport->rport);
	/* 
	 * We may need to retry the login, so don't change the state of the
	 * port but do the retries.
	 */
	if (atomic_read(&fcport->state) != FCS_DEVICE_DEAD)
		atomic_set(&fcport->state, FCS_DEVICE_LOST);

	if (!do_login)
		return;

	if (fcport->login_retry == 0) {
		fcport->login_retry = ha->login_retry_count;
		set_bit(RELOGIN_NEEDED, &ha->dpc_flags);

		DEBUG(printk("scsi(%ld): Port login retry: "
		    "%02x%02x%02x%02x%02x%02x%02x%02x, "
		    "id = 0x%04x retry cnt=%d\n",
		    ha->host_no,
		    fcport->port_name[0],
		    fcport->port_name[1],
		    fcport->port_name[2],
		    fcport->port_name[3],
		    fcport->port_name[4],
		    fcport->port_name[5],
		    fcport->port_name[6],
		    fcport->port_name[7],
		    fcport->loop_id,
		    fcport->login_retry));
	}
}

/*
 * qla2x00_mark_all_devices_lost
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
void
qla2x00_mark_all_devices_lost(scsi_qla_host_t *ha) 
{
	fc_port_t *fcport;

	list_for_each_entry(fcport, &ha->fcports, list) {
		if (fcport->port_type != FCT_TARGET)
			continue;

		/*
		 * No point in marking the device as lost, if the device is
		 * already DEAD.
		 */
		if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD)
			continue;
		if (atomic_read(&fcport->state) == FCS_ONLINE && fcport->rport)
			fc_remote_port_block(fcport->rport);
		atomic_set(&fcport->state, FCS_DEVICE_LOST);
	}
}

/*
* qla2x00_mem_alloc
*      Allocates adapter memory.
*
* Returns:
*      0  = success.
*      1  = failure.
*/
static uint8_t
qla2x00_mem_alloc(scsi_qla_host_t *ha)
{
	char	name[16];
	uint8_t   status = 1;
	int	retry= 10;

	do {
		/*
		 * This will loop only once if everything goes well, else some
		 * number of retries will be performed to get around a kernel
		 * bug where available mem is not allocated until after a
		 * little delay and a retry.
		 */
		ha->request_ring = dma_alloc_coherent(&ha->pdev->dev,
		    (ha->request_q_length + 1) * sizeof(request_t),
		    &ha->request_dma, GFP_KERNEL);
		if (ha->request_ring == NULL) {
			qla_printk(KERN_WARNING, ha,
			    "Memory Allocation failed - request_ring\n");

			qla2x00_mem_free(ha);
			msleep(100);

			continue;
		}

		ha->response_ring = dma_alloc_coherent(&ha->pdev->dev,
		    (ha->response_q_length + 1) * sizeof(response_t),
		    &ha->response_dma, GFP_KERNEL);
		if (ha->response_ring == NULL) {
			qla_printk(KERN_WARNING, ha,
			    "Memory Allocation failed - response_ring\n");

			qla2x00_mem_free(ha);
			msleep(100);

			continue;
		}

		ha->gid_list = dma_alloc_coherent(&ha->pdev->dev, GID_LIST_SIZE,
		    &ha->gid_list_dma, GFP_KERNEL);
		if (ha->gid_list == NULL) {
			qla_printk(KERN_WARNING, ha,
			    "Memory Allocation failed - gid_list\n");

			qla2x00_mem_free(ha);
			msleep(100);

			continue;
		}

		ha->rlc_rsp = dma_alloc_coherent(&ha->pdev->dev,
		    sizeof(rpt_lun_cmd_rsp_t), &ha->rlc_rsp_dma, GFP_KERNEL);
		if (ha->rlc_rsp == NULL) {
			qla_printk(KERN_WARNING, ha,
				"Memory Allocation failed - rlc");

			qla2x00_mem_free(ha);
			msleep(100);

			continue;
		}

		snprintf(name, sizeof(name), "qla2xxx_%ld", ha->host_no);
		ha->s_dma_pool = dma_pool_create(name, &ha->pdev->dev,
		    DMA_POOL_SIZE, 8, 0);
		if (ha->s_dma_pool == NULL) {
			qla_printk(KERN_WARNING, ha,
			    "Memory Allocation failed - s_dma_pool\n");

			qla2x00_mem_free(ha);
			msleep(100);

			continue;
		}

		/* get consistent memory allocated for init control block */
		ha->init_cb = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
		    &ha->init_cb_dma);
		if (ha->init_cb == NULL) {
			qla_printk(KERN_WARNING, ha,
			    "Memory Allocation failed - init_cb\n");

			qla2x00_mem_free(ha);
			msleep(100);

			continue;
		}
		memset(ha->init_cb, 0, sizeof(init_cb_t));

		/* Get consistent memory allocated for Get Port Database cmd */
		ha->iodesc_pd = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
		    &ha->iodesc_pd_dma);
		if (ha->iodesc_pd == NULL) {
			/* error */
			qla_printk(KERN_WARNING, ha,
			    "Memory Allocation failed - iodesc_pd\n");

			qla2x00_mem_free(ha);
			msleep(100);

			continue;
		}
		memset(ha->iodesc_pd, 0, PORT_DATABASE_SIZE);

		/* Allocate ioctl related memory. */
		if (qla2x00_alloc_ioctl_mem(ha)) {
			qla_printk(KERN_WARNING, ha,
			    "Memory Allocation failed - ioctl_mem\n");

			qla2x00_mem_free(ha);
			msleep(100);

			continue;
		}

		if (qla2x00_allocate_sp_pool(ha)) {
			qla_printk(KERN_WARNING, ha,
			    "Memory Allocation failed - "
			    "qla2x00_allocate_sp_pool()\n");

			qla2x00_mem_free(ha);
			msleep(100);

			continue;
		}

		/* Allocate memory for SNS commands */
		if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
			/* Get consistent memory allocated for SNS commands */
			ha->sns_cmd = dma_alloc_coherent(&ha->pdev->dev,
			    sizeof(struct sns_cmd_pkt), &ha->sns_cmd_dma,
			    GFP_KERNEL);
			if (ha->sns_cmd == NULL) {
				/* error */
				qla_printk(KERN_WARNING, ha,
				    "Memory Allocation failed - sns_cmd\n");

				qla2x00_mem_free(ha);
				msleep(100);

				continue;
			}
			memset(ha->sns_cmd, 0, sizeof(struct sns_cmd_pkt));
		} else {
			/* Get consistent memory allocated for MS IOCB */
			ha->ms_iocb = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
			    &ha->ms_iocb_dma);
			if (ha->ms_iocb == NULL) {
				/* error */
				qla_printk(KERN_WARNING, ha,
				    "Memory Allocation failed - ms_iocb\n");

				qla2x00_mem_free(ha);
				msleep(100);

				continue;
			}
			memset(ha->ms_iocb, 0, sizeof(ms_iocb_entry_t));

			/*
			 * Get consistent memory allocated for CT SNS
			 * commands
			 */
			ha->ct_sns = dma_alloc_coherent(&ha->pdev->dev,
			    sizeof(struct ct_sns_pkt), &ha->ct_sns_dma,
			    GFP_KERNEL);
			if (ha->ct_sns == NULL) {
				/* error */
				qla_printk(KERN_WARNING, ha,
				    "Memory Allocation failed - ct_sns\n");

				qla2x00_mem_free(ha);
				msleep(100);

				continue;
			}
			memset(ha->ct_sns, 0, sizeof(struct ct_sns_pkt));
		}

		/* Done all allocations without any error. */
		status = 0;

	} while (retry-- && status != 0);

	if (status) {
		printk(KERN_WARNING
			"%s(): **** FAILED ****\n", __func__);
	}

	return(status);
}

/*
* qla2x00_mem_free
*      Frees all adapter allocated memory.
*
* Input:
*      ha = adapter block pointer.
*/
static void
qla2x00_mem_free(scsi_qla_host_t *ha)
{
	struct list_head	*fcpl, *fcptemp;
	fc_port_t	*fcport;
	unsigned long	wtime;/* max wait time if mbx cmd is busy. */

	if (ha == NULL) {
		/* error */
		DEBUG2(printk("%s(): ERROR invalid ha pointer.\n", __func__));
		return;
	}

	/* Make sure all other threads are stopped. */
	wtime = 60 * HZ;
	while (ha->dpc_wait && wtime) {
		set_current_state(TASK_INTERRUPTIBLE);
		wtime = schedule_timeout(wtime);
	}

	/* free ioctl memory */
	qla2x00_free_ioctl_mem(ha);

	/* free sp pool */
	qla2x00_free_sp_pool(ha);

	if (ha->sns_cmd)
		dma_free_coherent(&ha->pdev->dev, sizeof(struct sns_cmd_pkt),
		    ha->sns_cmd, ha->sns_cmd_dma);

	if (ha->ct_sns)
		dma_free_coherent(&ha->pdev->dev, sizeof(struct ct_sns_pkt),
		    ha->ct_sns, ha->ct_sns_dma);

	if (ha->ms_iocb)
		dma_pool_free(ha->s_dma_pool, ha->ms_iocb, ha->ms_iocb_dma);

	if (ha->iodesc_pd)
		dma_pool_free(ha->s_dma_pool, ha->iodesc_pd, ha->iodesc_pd_dma);

	if (ha->init_cb)
		dma_pool_free(ha->s_dma_pool, ha->init_cb, ha->init_cb_dma);

	if (ha->s_dma_pool)
		dma_pool_destroy(ha->s_dma_pool);

	if (ha->rlc_rsp)
		dma_free_coherent(&ha->pdev->dev,
		    sizeof(rpt_lun_cmd_rsp_t), ha->rlc_rsp,
		    ha->rlc_rsp_dma);

	if (ha->gid_list)
		dma_free_coherent(&ha->pdev->dev, GID_LIST_SIZE, ha->gid_list,
		    ha->gid_list_dma);

	if (ha->response_ring)
		dma_free_coherent(&ha->pdev->dev,
		    (ha->response_q_length + 1) * sizeof(response_t),
		    ha->response_ring, ha->response_dma);

	if (ha->request_ring)
		dma_free_coherent(&ha->pdev->dev,
		    (ha->request_q_length + 1) * sizeof(request_t),
		    ha->request_ring, ha->request_dma);

	ha->sns_cmd = NULL;
	ha->sns_cmd_dma = 0;
	ha->ct_sns = NULL;
	ha->ct_sns_dma = 0;
	ha->ms_iocb = NULL;
	ha->ms_iocb_dma = 0;
	ha->iodesc_pd = NULL;
	ha->iodesc_pd_dma = 0;
	ha->init_cb = NULL;
	ha->init_cb_dma = 0;

	ha->s_dma_pool = NULL;

	ha->rlc_rsp = NULL;
	ha->rlc_rsp_dma = 0;
	ha->gid_list = NULL;
	ha->gid_list_dma = 0;

	ha->response_ring = NULL;
	ha->response_dma = 0;
	ha->request_ring = NULL;
	ha->request_dma = 0;

	list_for_each_safe(fcpl, fcptemp, &ha->fcports) {
		fcport = list_entry(fcpl, fc_port_t, list);

		/* fc ports */
		list_del_init(&fcport->list);
		kfree(fcport);
	}
	INIT_LIST_HEAD(&ha->fcports);

	if (ha->fw_dump)
		free_pages((unsigned long)ha->fw_dump, ha->fw_dump_order);

	if (ha->fw_dump_buffer)
		vfree(ha->fw_dump_buffer);

	ha->fw_dump = NULL;
	ha->fw_dump_reading = 0;
	ha->fw_dump_buffer = NULL;
}

/*
 * qla2x00_allocate_sp_pool
 * 	 This routine is called during initialization to allocate
 *  	 memory for local srb_t.
 *
 * Input:
 *	 ha   = adapter block pointer.
 *
 * Context:
 *      Kernel context.
 * 
 * Note: Sets the ref_count for non Null sp to one.
 */
static int
qla2x00_allocate_sp_pool(scsi_qla_host_t *ha) 
{
	int      rval;

	rval = QLA_SUCCESS;
	ha->srb_mempool = mempool_create(SRB_MIN_REQ, mempool_alloc_slab,
	    mempool_free_slab, srb_cachep);
	if (ha->srb_mempool == NULL) {
		qla_printk(KERN_INFO, ha, "Unable to allocate SRB mempool.\n");
		rval = QLA_FUNCTION_FAILED;
	}
	return (rval);
}

/*
 *  This routine frees all adapter allocated memory.
 *  
 */
static void
qla2x00_free_sp_pool( scsi_qla_host_t *ha) 
{
	if (ha->srb_mempool) {
		mempool_destroy(ha->srb_mempool);
		ha->srb_mempool = NULL;
	}
}

/**************************************************************************
* qla2x00_do_dpc
*   This kernel thread is a task that is schedule by the interrupt handler
*   to perform the background processing for interrupts.
*
* Notes:
* This task always run in the context of a kernel thread.  It
* is kick-off by the driver's detect code and starts up
* up one per adapter. It immediately goes to sleep and waits for
* some fibre event.  When either the interrupt handler or
* the timer routine detects a event it will one of the task
* bits then wake us up.
**************************************************************************/
static int
qla2x00_do_dpc(void *data)
{
	DECLARE_MUTEX_LOCKED(sem);
	scsi_qla_host_t *ha;
	fc_port_t	*fcport;
	uint8_t		status;
	uint16_t	next_loopid;

	ha = (scsi_qla_host_t *)data;

	lock_kernel();

	daemonize("%s_dpc", ha->host_str);
	allow_signal(SIGHUP);

	ha->dpc_wait = &sem;

	set_user_nice(current, -20);

	unlock_kernel();

	complete(&ha->dpc_inited);

	while (1) {
		DEBUG3(printk("qla2x00: DPC handler sleeping\n"));

		if (down_interruptible(&sem))
			break;

		if (ha->dpc_should_die)
			break;

		DEBUG3(printk("qla2x00: DPC handler waking up\n"));

		/* Initialization not yet finished. Don't do anything yet. */
		if (!ha->flags.init_done || ha->dpc_active)
			continue;

		DEBUG3(printk("scsi(%ld): DPC handler\n", ha->host_no));

		ha->dpc_active = 1;

		if (ha->flags.mbox_busy) {
			ha->dpc_active = 0;
			continue;
		}

		if (test_and_clear_bit(ISP_ABORT_NEEDED, &ha->dpc_flags)) {

			DEBUG(printk("scsi(%ld): dpc: sched "
			    "qla2x00_abort_isp ha = %p\n",
			    ha->host_no, ha));
			if (!(test_and_set_bit(ABORT_ISP_ACTIVE,
			    &ha->dpc_flags))) {

				if (qla2x00_abort_isp(ha)) {
					/* failed. retry later */
					set_bit(ISP_ABORT_NEEDED,
					    &ha->dpc_flags);
				}
				clear_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
			}
			DEBUG(printk("scsi(%ld): dpc: qla2x00_abort_isp end\n",
			    ha->host_no));
		}

		if (test_and_clear_bit(RESET_MARKER_NEEDED, &ha->dpc_flags) &&
		    (!(test_and_set_bit(RESET_ACTIVE, &ha->dpc_flags)))) {

			DEBUG(printk("scsi(%ld): qla2x00_reset_marker()\n",
			    ha->host_no));

			qla2x00_rst_aen(ha);
			clear_bit(RESET_ACTIVE, &ha->dpc_flags);
		}

		/* Retry each device up to login retry count */
		if ((test_and_clear_bit(RELOGIN_NEEDED, &ha->dpc_flags)) &&
		    !test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) &&
		    atomic_read(&ha->loop_state) != LOOP_DOWN) {

			DEBUG(printk("scsi(%ld): qla2x00_port_login()\n",
			    ha->host_no));

			next_loopid = 0;
			list_for_each_entry(fcport, &ha->fcports, list) {
				if (fcport->port_type != FCT_TARGET)
					continue;

				/*
				 * If the port is not ONLINE then try to login
				 * to it if we haven't run out of retries.
				 */
				if (atomic_read(&fcport->state) != FCS_ONLINE &&
				    fcport->login_retry) {

					fcport->login_retry--;
					if (fcport->flags & FCF_FABRIC_DEVICE) {
						if (fcport->flags &
						    FCF_TAPE_PRESENT)
							qla2x00_fabric_logout(
							    ha,
							    fcport->loop_id);
						status = qla2x00_fabric_login(
						    ha, fcport, &next_loopid);
					} else
						status =
						    qla2x00_local_device_login(
							ha, fcport->loop_id);

					if (status == QLA_SUCCESS) {
						fcport->old_loop_id = fcport->loop_id;

						DEBUG(printk("scsi(%ld): port login OK: logged in ID 0x%x\n",
						    ha->host_no, fcport->loop_id));
						
						fcport->port_login_retry_count =
						    ha->port_down_retry_count * PORT_RETRY_TIME;
						atomic_set(&fcport->state, FCS_ONLINE);
						atomic_set(&fcport->port_down_timer,
						    ha->port_down_retry_count * PORT_RETRY_TIME);

						fcport->login_retry = 0;
					} else if (status == 1) {
						set_bit(RELOGIN_NEEDED, &ha->dpc_flags);
						/* retry the login again */
						DEBUG(printk("scsi(%ld): Retrying %d login again loop_id 0x%x\n",
						    ha->host_no,
						    fcport->login_retry, fcport->loop_id));
					} else {
						fcport->login_retry = 0;
					}
				}
				if (test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags))
					break;
			}
			DEBUG(printk("scsi(%ld): qla2x00_port_login - end\n",
			    ha->host_no));
		}

		if ((test_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags)) &&
		    atomic_read(&ha->loop_state) != LOOP_DOWN) {

			clear_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags);
			DEBUG(printk("scsi(%ld): qla2x00_login_retry()\n",
			    ha->host_no));
				
			set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);

			DEBUG(printk("scsi(%ld): qla2x00_login_retry - end\n",
			    ha->host_no));
		}

		if (test_and_clear_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags)) {

			DEBUG(printk("scsi(%ld): qla2x00_loop_resync()\n",
			    ha->host_no));

			if (!(test_and_set_bit(LOOP_RESYNC_ACTIVE,
			    &ha->dpc_flags))) {

				qla2x00_loop_resync(ha);

				clear_bit(LOOP_RESYNC_ACTIVE, &ha->dpc_flags);
			}

			DEBUG(printk("scsi(%ld): qla2x00_loop_resync - end\n",
			    ha->host_no));
		}

		if (test_and_clear_bit(FCPORT_RESCAN_NEEDED, &ha->dpc_flags)) {

			DEBUG(printk("scsi(%ld): Rescan flagged fcports...\n",
			    ha->host_no));

			qla2x00_rescan_fcports(ha);

			DEBUG(printk("scsi(%ld): Rescan flagged fcports..."
			    "end.\n",
			    ha->host_no));
		}

		if (!ha->interrupts_on)
			qla2x00_enable_intrs(ha);

		ha->dpc_active = 0;
	} /* End of while(1) */

	DEBUG(printk("scsi(%ld): DPC handler exiting\n", ha->host_no));

	/*
	 * Make sure that nobody tries to wake us up again.
	 */
	ha->dpc_wait = NULL;
	ha->dpc_active = 0;

	complete_and_exit(&ha->dpc_exited, 0);
}

/*
*  qla2x00_rst_aen
*      Processes asynchronous reset.
*
* Input:
*      ha  = adapter block pointer.
*/
static void
qla2x00_rst_aen(scsi_qla_host_t *ha) 
{
	if (ha->flags.online && !ha->flags.reset_active &&
	    !atomic_read(&ha->loop_down_timer) &&
	    !(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags))) {
		do {
			clear_bit(RESET_MARKER_NEEDED, &ha->dpc_flags);

			/*
			 * Issue marker command only when we are going to start
			 * the I/O.
			 */
			ha->marker_needed = 1;
		} while (!atomic_read(&ha->loop_down_timer) &&
		    (test_bit(RESET_MARKER_NEEDED, &ha->dpc_flags)));
	}
}


/*
 * This routine will allocate SP from the free queue
 * input:
 *        scsi_qla_host_t *
 * output:
 *        srb_t * or NULL
 */
static srb_t *
qla2x00_get_new_sp(scsi_qla_host_t *ha)
{
	srb_t *sp;

	sp = mempool_alloc(ha->srb_mempool, GFP_ATOMIC);
	if (sp)
		atomic_set(&sp->ref_count, 1);
	return (sp);
}

static void
qla2x00_sp_free_dma(scsi_qla_host_t *ha, srb_t *sp)
{
	struct scsi_cmnd *cmd = sp->cmd;

	if (sp->flags & SRB_DMA_VALID) {
		if (cmd->use_sg) {
			dma_unmap_sg(&ha->pdev->dev, cmd->request_buffer,
			    cmd->use_sg, cmd->sc_data_direction);
		} else if (cmd->request_bufflen) {
			dma_unmap_single(&ha->pdev->dev, sp->dma_handle,
			    cmd->request_bufflen, cmd->sc_data_direction);
		}
		sp->flags &= ~SRB_DMA_VALID;
	}
}

void
qla2x00_sp_compl(scsi_qla_host_t *ha, srb_t *sp)
{
	struct scsi_cmnd *cmd = sp->cmd;

	qla2x00_sp_free_dma(ha, sp);

	CMD_SP(cmd) = NULL;
	mempool_free(sp, ha->srb_mempool);

	cmd->scsi_done(cmd);
}

/**************************************************************************
*   qla2x00_timer
*
* Description:
*   One second timer
*
* Context: Interrupt
***************************************************************************/
static void
qla2x00_timer(scsi_qla_host_t *ha)
{
	unsigned long	cpu_flags = 0;
	fc_port_t	*fcport;
	int		start_dpc = 0;
	int		index;
	srb_t		*sp;
	int		t;

	/*
	 * Ports - Port down timer.
	 *
	 * Whenever, a port is in the LOST state we start decrementing its port
	 * down timer every second until it reaches zero. Once  it reaches zero
	 * the port it marked DEAD. 
	 */
	t = 0;
	list_for_each_entry(fcport, &ha->fcports, list) {
		if (fcport->port_type != FCT_TARGET)
			continue;

		if (atomic_read(&fcport->state) == FCS_DEVICE_LOST) {

			if (atomic_read(&fcport->port_down_timer) == 0)
				continue;

			if (atomic_dec_and_test(&fcport->port_down_timer) != 0) 
				atomic_set(&fcport->state, FCS_DEVICE_DEAD);
			
			DEBUG(printk("scsi(%ld): fcport-%d - port retry count: "
			    "%d remainning\n",
			    ha->host_no,
			    t, atomic_read(&fcport->port_down_timer)));
		}
		t++;
	} /* End of for fcport  */


	/* Loop down handler. */
	if (atomic_read(&ha->loop_down_timer) > 0 &&
	    !(test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags)) && ha->flags.online) {

		if (atomic_read(&ha->loop_down_timer) ==
		    ha->loop_down_abort_time) {

			DEBUG(printk("scsi(%ld): Loop Down - aborting the "
			    "queues before time expire\n",
			    ha->host_no));

			if (!IS_QLA2100(ha) && ha->link_down_timeout)
				atomic_set(&ha->loop_state, LOOP_DEAD); 

			/* Schedule an ISP abort to return any tape commands. */
			spin_lock_irqsave(&ha->hardware_lock, cpu_flags);
			for (index = 1; index < MAX_OUTSTANDING_COMMANDS;
			    index++) {
				fc_port_t *sfcp;

				sp = ha->outstanding_cmds[index];
				if (!sp)
					continue;
				sfcp = sp->fcport;
				if (!(sfcp->flags & FCF_TAPE_PRESENT))
					continue;

				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				break;
			}
			spin_unlock_irqrestore(&ha->hardware_lock, cpu_flags);

			set_bit(ABORT_QUEUES_NEEDED, &ha->dpc_flags);
			start_dpc++;
		}

		/* if the loop has been down for 4 minutes, reinit adapter */
		if (atomic_dec_and_test(&ha->loop_down_timer) != 0) {
			DEBUG(printk("scsi(%ld): Loop down exceed 4 mins - "
			    "restarting queues.\n",
			    ha->host_no));

			set_bit(RESTART_QUEUES_NEEDED, &ha->dpc_flags);
			start_dpc++;

			if (!(ha->device_flags & DFLG_NO_CABLE)) {
				DEBUG(printk("scsi(%ld): Loop down - "
				    "aborting ISP.\n",
				    ha->host_no));
				qla_printk(KERN_WARNING, ha,
				    "Loop down - aborting ISP.\n");

				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
			}
		}
		DEBUG3(printk("scsi(%ld): Loop Down - seconds remainning %d\n",
		    ha->host_no,
		    atomic_read(&ha->loop_down_timer)));
	}

	/* Schedule the DPC routine if needed */
	if ((test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
	    test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ||
	    start_dpc ||
	    test_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags) ||
	    test_bit(RESET_MARKER_NEEDED, &ha->dpc_flags) ||
	    test_bit(RELOGIN_NEEDED, &ha->dpc_flags)) &&
	    ha->dpc_wait && !ha->dpc_active) {

		up(ha->dpc_wait);
	}

	qla2x00_restart_timer(ha, WATCH_INTERVAL);
}

/* XXX(hch): crude hack to emulate a down_timeout() */
int
qla2x00_down_timeout(struct semaphore *sema, unsigned long timeout)
{
	const unsigned int step = HZ/10;

	do {
		if (!down_trylock(sema))
			return 0;
		set_current_state(TASK_INTERRUPTIBLE);
		if (schedule_timeout(step))
			break;
	} while ((timeout -= step) > 0);

	return -ETIMEDOUT;
}

/**
 * qla2x00_module_init - Module initialization.
 **/
static int __init
qla2x00_module_init(void)
{
	/* Allocate cache for SRBs. */
	sprintf(srb_cachep_name, "qla2xxx_srbs");
	srb_cachep = kmem_cache_create(srb_cachep_name, sizeof(srb_t), 0,
	    SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (srb_cachep == NULL) {
		printk(KERN_ERR
		    "qla2xxx: Unable to allocate SRB cache...Failing load!\n");
		return -ENOMEM;
	}

	/* Derive version string. */
	strcpy(qla2x00_version_str, QLA2XXX_VERSION);
#if DEBUG_QLA2100
	strcat(qla2x00_version_str, "-debug");
#endif
	qla2xxx_transport_template =
	    fc_attach_transport(&qla2xxx_transport_functions);
	if (!qla2xxx_transport_template)
		return -ENODEV;

	printk(KERN_INFO "QLogic Fibre Channel HBA Driver\n");
	return 0;
}

/**
 * qla2x00_module_exit - Module cleanup.
 **/
static void __exit
qla2x00_module_exit(void)
{
	/* Free SRBs cache. */
	if (srb_cachep != NULL) {
		if (kmem_cache_destroy(srb_cachep) != 0) {
			printk(KERN_ERR
			    "qla2xxx: Unable to free SRB cache...Memory pools "
			    "still active?\n");
		}
		srb_cachep = NULL;
	}

	fc_release_transport(qla2xxx_transport_template);
}

module_init(qla2x00_module_init);
module_exit(qla2x00_module_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic Fibre Channel HBA Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLA2XXX_VERSION);
