/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2008 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/moduleparam.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mutex.h>

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
static struct kmem_cache *srb_cachep;

int ql2xlogintimeout = 20;
module_param(ql2xlogintimeout, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xlogintimeout,
		"Login timeout value in seconds.");

int qlport_down_retry;
module_param(qlport_down_retry, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(qlport_down_retry,
		"Maximum number of command retries to a port that returns "
		"a PORT-DOWN status.");

int ql2xplogiabsentdevice;
module_param(ql2xplogiabsentdevice, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xplogiabsentdevice,
		"Option to enable PLOGI to devices that are not present after "
		"a Fabric scan.  This is needed for several broken switches. "
		"Default is 0 - no PLOGI. 1 - perfom PLOGI.");

int ql2xloginretrycount = 0;
module_param(ql2xloginretrycount, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xloginretrycount,
		"Specify an alternate value for the NVRAM login retry count.");

int ql2xallocfwdump = 1;
module_param(ql2xallocfwdump, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xallocfwdump,
		"Option to enable allocation of memory for a firmware dump "
		"during HBA initialization.  Memory allocation requirements "
		"vary by ISP type.  Default is 1 - allocate memory.");

int ql2xextended_error_logging;
module_param(ql2xextended_error_logging, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xextended_error_logging,
		"Option to enable extended error logging, "
		"Default is 0 - no logging. 1 - log errors.");

static void qla2x00_free_device(scsi_qla_host_t *);

static void qla2x00_config_dma_addressing(scsi_qla_host_t *ha);

int ql2xfdmienable=1;
module_param(ql2xfdmienable, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xfdmienable,
		"Enables FDMI registratons "
		"Default is 0 - no FDMI. 1 - perfom FDMI.");

#define MAX_Q_DEPTH    32
static int ql2xmaxqdepth = MAX_Q_DEPTH;
module_param(ql2xmaxqdepth, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xmaxqdepth,
		"Maximum queue depth to report for target devices.");

int ql2xqfullrampup = 120;
module_param(ql2xqfullrampup, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xqfullrampup,
		"Number of seconds to wait to begin to ramp-up the queue "
		"depth for a device after a queue-full condition has been "
		"detected.  Default is 120 seconds.");

int ql2xiidmaenable=1;
module_param(ql2xiidmaenable, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xiidmaenable,
		"Enables iIDMA settings "
		"Default is 1 - perform iIDMA. 0 - no iIDMA.");


/*
 * SCSI host template entry points
 */
static int qla2xxx_slave_configure(struct scsi_device * device);
static int qla2xxx_slave_alloc(struct scsi_device *);
static int qla2xxx_scan_finished(struct Scsi_Host *, unsigned long time);
static void qla2xxx_scan_start(struct Scsi_Host *);
static void qla2xxx_slave_destroy(struct scsi_device *);
static int qla2x00_queuecommand(struct scsi_cmnd *cmd,
		void (*fn)(struct scsi_cmnd *));
static int qla24xx_queuecommand(struct scsi_cmnd *cmd,
		void (*fn)(struct scsi_cmnd *));
static int qla2xxx_eh_abort(struct scsi_cmnd *);
static int qla2xxx_eh_device_reset(struct scsi_cmnd *);
static int qla2xxx_eh_target_reset(struct scsi_cmnd *);
static int qla2xxx_eh_bus_reset(struct scsi_cmnd *);
static int qla2xxx_eh_host_reset(struct scsi_cmnd *);

static int qla2x00_change_queue_depth(struct scsi_device *, int);
static int qla2x00_change_queue_type(struct scsi_device *, int);

static struct scsi_host_template qla2x00_driver_template = {
	.module			= THIS_MODULE,
	.name			= QLA2XXX_DRIVER_NAME,
	.queuecommand		= qla2x00_queuecommand,

	.eh_abort_handler	= qla2xxx_eh_abort,
	.eh_device_reset_handler = qla2xxx_eh_device_reset,
	.eh_target_reset_handler = qla2xxx_eh_target_reset,
	.eh_bus_reset_handler	= qla2xxx_eh_bus_reset,
	.eh_host_reset_handler	= qla2xxx_eh_host_reset,

	.slave_configure	= qla2xxx_slave_configure,

	.slave_alloc		= qla2xxx_slave_alloc,
	.slave_destroy		= qla2xxx_slave_destroy,
	.scan_finished		= qla2xxx_scan_finished,
	.scan_start		= qla2xxx_scan_start,
	.change_queue_depth	= qla2x00_change_queue_depth,
	.change_queue_type	= qla2x00_change_queue_type,
	.this_id		= -1,
	.cmd_per_lun		= 3,
	.use_clustering		= ENABLE_CLUSTERING,
	.sg_tablesize		= SG_ALL,

	/*
	 * The RISC allows for each command to transfer (2^32-1) bytes of data,
	 * which equates to 0x800000 sectors.
	 */
	.max_sectors		= 0xFFFF,
	.shost_attrs		= qla2x00_host_attrs,
};

struct scsi_host_template qla24xx_driver_template = {
	.module			= THIS_MODULE,
	.name			= QLA2XXX_DRIVER_NAME,
	.queuecommand		= qla24xx_queuecommand,

	.eh_abort_handler	= qla2xxx_eh_abort,
	.eh_device_reset_handler = qla2xxx_eh_device_reset,
	.eh_target_reset_handler = qla2xxx_eh_target_reset,
	.eh_bus_reset_handler	= qla2xxx_eh_bus_reset,
	.eh_host_reset_handler	= qla2xxx_eh_host_reset,

	.slave_configure	= qla2xxx_slave_configure,

	.slave_alloc		= qla2xxx_slave_alloc,
	.slave_destroy		= qla2xxx_slave_destroy,
	.scan_finished		= qla2xxx_scan_finished,
	.scan_start		= qla2xxx_scan_start,
	.change_queue_depth	= qla2x00_change_queue_depth,
	.change_queue_type	= qla2x00_change_queue_type,
	.this_id		= -1,
	.cmd_per_lun		= 3,
	.use_clustering		= ENABLE_CLUSTERING,
	.sg_tablesize		= SG_ALL,

	.max_sectors		= 0xFFFF,
	.shost_attrs		= qla2x00_host_attrs,
};

static struct scsi_transport_template *qla2xxx_transport_template = NULL;
struct scsi_transport_template *qla2xxx_transport_vport_template = NULL;

/* TODO Convert to inlines
 *
 * Timer routines
 */

__inline__ void
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

static int qla2x00_mem_alloc(scsi_qla_host_t *);
static void qla2x00_mem_free(scsi_qla_host_t *ha);
static void qla2x00_sp_free_dma(scsi_qla_host_t *, srb_t *);

/* -------------------------------------------------------------------------- */

static char *
qla2x00_pci_info_str(struct scsi_qla_host *ha, char *str)
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

static char *
qla24xx_pci_info_str(struct scsi_qla_host *ha, char *str)
{
	static char *pci_bus_modes[] = { "33", "66", "100", "133", };
	uint32_t pci_bus;
	int pcie_reg;

	pcie_reg = pci_find_capability(ha->pdev, PCI_CAP_ID_EXP);
	if (pcie_reg) {
		char lwstr[6];
		uint16_t pcie_lstat, lspeed, lwidth;

		pcie_reg += 0x12;
		pci_read_config_word(ha->pdev, pcie_reg, &pcie_lstat);
		lspeed = pcie_lstat & (BIT_0 | BIT_1 | BIT_2 | BIT_3);
		lwidth = (pcie_lstat &
		    (BIT_4 | BIT_5 | BIT_6 | BIT_7 | BIT_8 | BIT_9)) >> 4;

		strcpy(str, "PCIe (");
		if (lspeed == 1)
			strcat(str, "2.5GT/s ");
		else if (lspeed == 2)
			strcat(str, "5.0GT/s ");
		else
			strcat(str, "<unknown> ");
		snprintf(lwstr, sizeof(lwstr), "x%d)", lwidth);
		strcat(str, lwstr);

		return str;
	}

	strcpy(str, "PCI");
	pci_bus = (ha->pci_attr & CSRX_PCIX_BUS_MODE_MASK) >> 8;
	if (pci_bus == 0 || pci_bus == 8) {
		strcat(str, " (");
		strcat(str, pci_bus_modes[pci_bus >> 3]);
	} else {
		strcat(str, "-X ");
		if (pci_bus & BIT_2)
			strcat(str, "Mode 2");
		else
			strcat(str, "Mode 1");
		strcat(str, " (");
		strcat(str, pci_bus_modes[pci_bus & ~BIT_2]);
	}
	strcat(str, " MHz)");

	return str;
}

static char *
qla2x00_fw_version_str(struct scsi_qla_host *ha, char *str)
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

static char *
qla24xx_fw_version_str(struct scsi_qla_host *ha, char *str)
{
	sprintf(str, "%d.%02d.%02d ", ha->fw_major_version,
	    ha->fw_minor_version,
	    ha->fw_subminor_version);

	if (ha->fw_attributes & BIT_0)
		strcat(str, "[Class 2] ");
	if (ha->fw_attributes & BIT_1)
		strcat(str, "[IP] ");
	if (ha->fw_attributes & BIT_2)
		strcat(str, "[Multi-ID] ");
	if (ha->fw_attributes & BIT_3)
		strcat(str, "[SB-2] ");
	if (ha->fw_attributes & BIT_4)
		strcat(str, "[T10 CRC] ");
	if (ha->fw_attributes & BIT_5)
		strcat(str, "[VI] ");
	if (ha->fw_attributes & BIT_10)
		strcat(str, "[84XX] ");
	if (ha->fw_attributes & BIT_13)
		strcat(str, "[Experimental]");
	return str;
}

static inline srb_t *
qla2x00_get_new_sp(scsi_qla_host_t *ha, fc_port_t *fcport,
    struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	srb_t *sp;

	sp = mempool_alloc(ha->srb_mempool, GFP_ATOMIC);
	if (!sp)
		return sp;

	sp->ha = ha;
	sp->fcport = fcport;
	sp->cmd = cmd;
	sp->flags = 0;
	CMD_SP(cmd) = (void *)sp;
	cmd->scsi_done = done;

	return sp;
}

static int
qla2x00_queuecommand(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	scsi_qla_host_t *ha = shost_priv(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmd->device));
	srb_t *sp;
	int rval;

	if (unlikely(pci_channel_offline(ha->pdev))) {
		cmd->result = DID_REQUEUE << 16;
		goto qc_fail_command;
	}

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		cmd->result = rval;
		goto qc_fail_command;
	}

	/* Close window on fcport/rport state-transitioning. */
	if (fcport->drport) {
		cmd->result = DID_IMM_RETRY << 16;
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

	sp = qla2x00_get_new_sp(ha, fcport, cmd, done);
	if (!sp)
		goto qc_host_busy_lock;

	rval = qla2x00_start_scsi(sp);
	if (rval != QLA_SUCCESS)
		goto qc_host_busy_free_sp;

	spin_lock_irq(ha->host->host_lock);

	return 0;

qc_host_busy_free_sp:
	qla2x00_sp_free_dma(ha, sp);
	mempool_free(sp, ha->srb_mempool);

qc_host_busy_lock:
	spin_lock_irq(ha->host->host_lock);

qc_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

qc_fail_command:
	done(cmd);

	return 0;
}


static int
qla24xx_queuecommand(struct scsi_cmnd *cmd, void (*done)(struct scsi_cmnd *))
{
	scsi_qla_host_t *ha = shost_priv(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmd->device));
	srb_t *sp;
	int rval;
	scsi_qla_host_t *pha = to_qla_parent(ha);

	if (unlikely(pci_channel_offline(pha->pdev))) {
		cmd->result = DID_REQUEUE << 16;
		goto qc24_fail_command;
	}

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		cmd->result = rval;
		goto qc24_fail_command;
	}

	/* Close window on fcport/rport state-transitioning. */
	if (fcport->drport) {
		cmd->result = DID_IMM_RETRY << 16;
		goto qc24_fail_command;
	}

	if (atomic_read(&fcport->state) != FCS_ONLINE) {
		if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD ||
		    atomic_read(&pha->loop_state) == LOOP_DEAD) {
			cmd->result = DID_NO_CONNECT << 16;
			goto qc24_fail_command;
		}
		goto qc24_host_busy;
	}

	spin_unlock_irq(ha->host->host_lock);

	sp = qla2x00_get_new_sp(pha, fcport, cmd, done);
	if (!sp)
		goto qc24_host_busy_lock;

	rval = qla24xx_start_scsi(sp);
	if (rval != QLA_SUCCESS)
		goto qc24_host_busy_free_sp;

	spin_lock_irq(ha->host->host_lock);

	return 0;

qc24_host_busy_free_sp:
	qla2x00_sp_free_dma(pha, sp);
	mempool_free(sp, pha->srb_mempool);

qc24_host_busy_lock:
	spin_lock_irq(ha->host->host_lock);

qc24_host_busy:
	return SCSI_MLQUEUE_HOST_BUSY;

qc24_fail_command:
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
#define ABORT_POLLING_PERIOD	1000
#define ABORT_WAIT_ITER		((10 * 1000) / (ABORT_POLLING_PERIOD))
	unsigned long wait_iter = ABORT_WAIT_ITER;
	int ret = QLA_SUCCESS;

	while (CMD_SP(cmd)) {
		msleep(ABORT_POLLING_PERIOD);

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
int
qla2x00_wait_for_hba_online(scsi_qla_host_t *ha)
{
	int		return_status;
	unsigned long	wait_online;
	scsi_qla_host_t *pha = to_qla_parent(ha);

	wait_online = jiffies + (MAX_LOOP_TIMEOUT * HZ);
	while (((test_bit(ISP_ABORT_NEEDED, &pha->dpc_flags)) ||
	    test_bit(ABORT_ISP_ACTIVE, &pha->dpc_flags) ||
	    test_bit(ISP_ABORT_RETRY, &pha->dpc_flags) ||
	    pha->dpc_active) && time_before(jiffies, wait_online)) {

		msleep(1000);
	}
	if (pha->flags.online)
		return_status = QLA_SUCCESS;
	else
		return_status = QLA_FUNCTION_FAILED;

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
	scsi_qla_host_t *pha = to_qla_parent(ha);

	/* wait for 5 min at the max for loop to be ready */
	loop_timeout = jiffies + (MAX_LOOP_TIMEOUT * HZ);

	while ((!atomic_read(&pha->loop_down_timer) &&
	    atomic_read(&pha->loop_state) == LOOP_DOWN) ||
	    atomic_read(&pha->loop_state) != LOOP_READY) {
		if (atomic_read(&pha->loop_state) == LOOP_DEAD) {
			return_status = QLA_FUNCTION_FAILED;
			break;
		}
		msleep(1000);
		if (time_after_eq(jiffies, loop_timeout)) {
			return_status = QLA_FUNCTION_FAILED;
			break;
		}
	}
	return (return_status);
}

void
qla2x00_abort_fcport_cmds(fc_port_t *fcport)
{
	int cnt;
	unsigned long flags;
	srb_t *sp;
	scsi_qla_host_t *ha = fcport->ha;
	scsi_qla_host_t *pha = to_qla_parent(ha);

	spin_lock_irqsave(&pha->hardware_lock, flags);
	for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
		sp = pha->outstanding_cmds[cnt];
		if (!sp)
			continue;
		if (sp->fcport != fcport)
			continue;

		spin_unlock_irqrestore(&pha->hardware_lock, flags);
		if (ha->isp_ops->abort_command(ha, sp)) {
			DEBUG2(qla_printk(KERN_WARNING, ha,
			    "Abort failed --  %lx\n", sp->cmd->serial_number));
		} else {
			if (qla2x00_eh_wait_on_command(ha, sp->cmd) !=
			    QLA_SUCCESS)
				DEBUG2(qla_printk(KERN_WARNING, ha,
				    "Abort failed while waiting --  %lx\n",
				    sp->cmd->serial_number));

		}
		spin_lock_irqsave(&pha->hardware_lock, flags);
	}
	spin_unlock_irqrestore(&pha->hardware_lock, flags);
}

static void
qla2x00_block_error_handler(struct scsi_cmnd *cmnd)
{
	struct Scsi_Host *shost = cmnd->device->host;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmnd->device));
	unsigned long flags;

	spin_lock_irqsave(shost->host_lock, flags);
	while (rport->port_state == FC_PORTSTATE_BLOCKED) {
		spin_unlock_irqrestore(shost->host_lock, flags);
		msleep(1000);
		spin_lock_irqsave(shost->host_lock, flags);
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
	return;
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
*    Only return FAILED if command not returned by firmware.
**************************************************************************/
static int
qla2xxx_eh_abort(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *ha = shost_priv(cmd->device->host);
	srb_t *sp;
	int ret, i;
	unsigned int id, lun;
	unsigned long serial;
	unsigned long flags;
	int wait = 0;
	scsi_qla_host_t *pha = to_qla_parent(ha);

	qla2x00_block_error_handler(cmd);

	if (!CMD_SP(cmd))
		return SUCCESS;

	ret = SUCCESS;

	id = cmd->device->id;
	lun = cmd->device->lun;
	serial = cmd->serial_number;

	/* Check active list for command command. */
	spin_lock_irqsave(&pha->hardware_lock, flags);
	for (i = 1; i < MAX_OUTSTANDING_COMMANDS; i++) {
		sp = pha->outstanding_cmds[i];

		if (sp == NULL)
			continue;

		if (sp->cmd != cmd)
			continue;

		DEBUG2(printk("%s(%ld): aborting sp %p from RISC. pid=%ld.\n",
		    __func__, ha->host_no, sp, serial));

		spin_unlock_irqrestore(&pha->hardware_lock, flags);
		if (ha->isp_ops->abort_command(ha, sp)) {
			DEBUG2(printk("%s(%ld): abort_command "
			    "mbx failed.\n", __func__, ha->host_no));
		} else {
			DEBUG3(printk("%s(%ld): abort_command "
			    "mbx success.\n", __func__, ha->host_no));
			wait = 1;
		}
		spin_lock_irqsave(&pha->hardware_lock, flags);

		break;
	}
	spin_unlock_irqrestore(&pha->hardware_lock, flags);

	/* Wait for the command to be returned. */
	if (wait) {
		if (qla2x00_eh_wait_on_command(ha, cmd) != QLA_SUCCESS) {
			qla_printk(KERN_ERR, ha,
			    "scsi(%ld:%d:%d): Abort handler timed out -- %lx "
			    "%x.\n", ha->host_no, id, lun, serial, ret);
			ret = FAILED;
		}
	}

	qla_printk(KERN_INFO, ha,
	    "scsi(%ld:%d:%d): Abort command issued -- %d %lx %x.\n",
	    ha->host_no, id, lun, wait, serial, ret);

	return ret;
}

enum nexus_wait_type {
	WAIT_HOST = 0,
	WAIT_TARGET,
	WAIT_LUN,
};

static int
qla2x00_eh_wait_for_pending_commands(scsi_qla_host_t *ha, unsigned int t,
    unsigned int l, enum nexus_wait_type type)
{
	int cnt, match, status;
	srb_t *sp;
	unsigned long flags;
	scsi_qla_host_t *pha = to_qla_parent(ha);

	status = QLA_SUCCESS;
	spin_lock_irqsave(&pha->hardware_lock, flags);
	for (cnt = 1; status == QLA_SUCCESS && cnt < MAX_OUTSTANDING_COMMANDS;
	    cnt++) {
		sp = pha->outstanding_cmds[cnt];
		if (!sp)
			continue;

		if (ha->vp_idx != sp->fcport->ha->vp_idx)
			continue;
		match = 0;
		switch (type) {
		case WAIT_HOST:
			match = 1;
			break;
		case WAIT_TARGET:
			match = sp->cmd->device->id == t;
			break;
		case WAIT_LUN:
			match = (sp->cmd->device->id == t &&
			    sp->cmd->device->lun == l);
			break;
		}
		if (!match)
			continue;

		spin_unlock_irqrestore(&pha->hardware_lock, flags);
		status = qla2x00_eh_wait_on_command(ha, sp->cmd);
		spin_lock_irqsave(&pha->hardware_lock, flags);
	}
	spin_unlock_irqrestore(&pha->hardware_lock, flags);

	return status;
}

static char *reset_errors[] = {
	"HBA not online",
	"HBA not ready",
	"Task management failed",
	"Waiting for command completions",
};

static int
__qla2xxx_eh_generic_reset(char *name, enum nexus_wait_type type,
    struct scsi_cmnd *cmd, int (*do_reset)(struct fc_port *, unsigned int))
{
	scsi_qla_host_t *ha = shost_priv(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	int err;

	qla2x00_block_error_handler(cmd);

	if (!fcport)
		return FAILED;

	qla_printk(KERN_INFO, ha, "scsi(%ld:%d:%d): %s RESET ISSUED.\n",
	    ha->host_no, cmd->device->id, cmd->device->lun, name);

	err = 0;
	if (qla2x00_wait_for_hba_online(ha) != QLA_SUCCESS)
		goto eh_reset_failed;
	err = 1;
	if (qla2x00_wait_for_loop_ready(ha) != QLA_SUCCESS)
		goto eh_reset_failed;
	err = 2;
	if (do_reset(fcport, cmd->device->lun) != QLA_SUCCESS)
		goto eh_reset_failed;
	err = 3;
	if (qla2x00_eh_wait_for_pending_commands(ha, cmd->device->id,
	    cmd->device->lun, type) != QLA_SUCCESS)
		goto eh_reset_failed;

	qla_printk(KERN_INFO, ha, "scsi(%ld:%d:%d): %s RESET SUCCEEDED.\n",
	    ha->host_no, cmd->device->id, cmd->device->lun, name);

	return SUCCESS;

 eh_reset_failed:
	qla_printk(KERN_INFO, ha, "scsi(%ld:%d:%d): %s RESET FAILED: %s.\n",
	    ha->host_no, cmd->device->id, cmd->device->lun, name,
	    reset_errors[err]);
	return FAILED;
}

static int
qla2xxx_eh_device_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *ha = shost_priv(cmd->device->host);

	return __qla2xxx_eh_generic_reset("DEVICE", WAIT_LUN, cmd,
	    ha->isp_ops->lun_reset);
}

static int
qla2xxx_eh_target_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *ha = shost_priv(cmd->device->host);

	return __qla2xxx_eh_generic_reset("TARGET", WAIT_TARGET, cmd,
	    ha->isp_ops->target_reset);
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
static int
qla2xxx_eh_bus_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *ha = shost_priv(cmd->device->host);
	scsi_qla_host_t *pha = to_qla_parent(ha);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	int ret = FAILED;
	unsigned int id, lun;
	unsigned long serial;

	qla2x00_block_error_handler(cmd);

	id = cmd->device->id;
	lun = cmd->device->lun;
	serial = cmd->serial_number;

	if (!fcport)
		return ret;

	qla_printk(KERN_INFO, ha,
	    "scsi(%ld:%d:%d): LOOP RESET ISSUED.\n", ha->host_no, id, lun);

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

	/* Flush outstanding commands. */
	if (qla2x00_eh_wait_for_pending_commands(pha, 0, 0, WAIT_HOST) !=
	    QLA_SUCCESS)
		ret = FAILED;

eh_bus_reset_done:
	qla_printk(KERN_INFO, ha, "%s: reset %s\n", __func__,
	    (ret == FAILED) ? "failed" : "succeded");

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
static int
qla2xxx_eh_host_reset(struct scsi_cmnd *cmd)
{
	scsi_qla_host_t *ha = shost_priv(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	int ret = FAILED;
	unsigned int id, lun;
	unsigned long serial;
	scsi_qla_host_t *pha = to_qla_parent(ha);

	qla2x00_block_error_handler(cmd);

	id = cmd->device->id;
	lun = cmd->device->lun;
	serial = cmd->serial_number;

	if (!fcport)
		return ret;

	qla_printk(KERN_INFO, ha,
	    "scsi(%ld:%d:%d): ADAPTER RESET ISSUED.\n", ha->host_no, id, lun);

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
	set_bit(ABORT_ISP_ACTIVE, &pha->dpc_flags);
	if (qla2x00_abort_isp(pha)) {
		clear_bit(ABORT_ISP_ACTIVE, &pha->dpc_flags);
		/* failed. schedule dpc to try */
		set_bit(ISP_ABORT_NEEDED, &pha->dpc_flags);

		if (qla2x00_wait_for_hba_online(ha) != QLA_SUCCESS)
			goto eh_host_reset_lock;
	}
	clear_bit(ABORT_ISP_ACTIVE, &pha->dpc_flags);

	/* Waiting for our command in done_queue to be returned to OS.*/
	if (qla2x00_eh_wait_for_pending_commands(pha, 0, 0, WAIT_HOST) ==
	    QLA_SUCCESS)
		ret = SUCCESS;

	if (ha->parent)
		qla2x00_vp_abort_isp(ha);

eh_host_reset_lock:
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
int
qla2x00_loop_reset(scsi_qla_host_t *ha)
{
	int ret;
	struct fc_port *fcport;

	if (ha->flags.enable_lip_full_login) {
		ret = qla2x00_full_login_lip(ha);
		if (ret != QLA_SUCCESS) {
			DEBUG2_3(printk("%s(%ld): bus_reset failed: "
			    "full_login_lip=%d.\n", __func__, ha->host_no,
			    ret));
		}
		atomic_set(&ha->loop_state, LOOP_DOWN);
		atomic_set(&ha->loop_down_timer, LOOP_DOWN_TIME);
		qla2x00_mark_all_devices_lost(ha, 0);
		qla2x00_wait_for_loop_ready(ha);
	}

	if (ha->flags.enable_lip_reset) {
		ret = qla2x00_lip_reset(ha);
		if (ret != QLA_SUCCESS) {
			DEBUG2_3(printk("%s(%ld): bus_reset failed: "
			    "lip_reset=%d.\n", __func__, ha->host_no, ret));
		}
		qla2x00_wait_for_loop_ready(ha);
	}

	if (ha->flags.enable_target_reset) {
		list_for_each_entry(fcport, &ha->fcports, list) {
			if (fcport->port_type != FCT_TARGET)
				continue;

			ret = ha->isp_ops->target_reset(fcport, 0);
			if (ret != QLA_SUCCESS) {
				DEBUG2_3(printk("%s(%ld): bus_reset failed: "
				    "target_reset=%d d_id=%x.\n", __func__,
				    ha->host_no, ret, fcport->d_id.b24));
			}
		}
	}

	/* Issue marker command only when we are going to start the I/O */
	ha->marker_needed = 1;

	return QLA_SUCCESS;
}

void
qla2x00_abort_all_cmds(scsi_qla_host_t *ha, int res)
{
	int cnt;
	unsigned long flags;
	srb_t *sp;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
		sp = ha->outstanding_cmds[cnt];
		if (sp) {
			ha->outstanding_cmds[cnt] = NULL;
			sp->cmd->result = res;
			qla2x00_sp_compl(ha, sp);
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static int
qla2xxx_slave_alloc(struct scsi_device *sdev)
{
	struct fc_rport *rport = starget_to_rport(scsi_target(sdev));

	if (!rport || fc_remote_port_chkready(rport))
		return -ENXIO;

	sdev->hostdata = *(fc_port_t **)rport->dd_data;

	return 0;
}

static int
qla2xxx_slave_configure(struct scsi_device *sdev)
{
	scsi_qla_host_t *ha = shost_priv(sdev->host);
	struct fc_rport *rport = starget_to_rport(sdev->sdev_target);

	if (sdev->tagged_supported)
		scsi_activate_tcq(sdev, ha->max_q_depth);
	else
		scsi_deactivate_tcq(sdev, ha->max_q_depth);

	rport->dev_loss_tmo = ha->port_down_retry_count;

	return 0;
}

static void
qla2xxx_slave_destroy(struct scsi_device *sdev)
{
	sdev->hostdata = NULL;
}

static int
qla2x00_change_queue_depth(struct scsi_device *sdev, int qdepth)
{
	scsi_adjust_queue_depth(sdev, scsi_get_tag_type(sdev), qdepth);
	return sdev->queue_depth;
}

static int
qla2x00_change_queue_type(struct scsi_device *sdev, int tag_type)
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
	/* Assume a 32bit DMA mask. */
	ha->flags.enable_64bit_addressing = 0;

	if (!dma_set_mask(&ha->pdev->dev, DMA_64BIT_MASK)) {
		/* Any upper-dword bits set? */
		if (MSD(dma_get_required_mask(&ha->pdev->dev)) &&
		    !pci_set_consistent_dma_mask(ha->pdev, DMA_64BIT_MASK)) {
			/* Ok, a 64bit DMA mask is applicable. */
			ha->flags.enable_64bit_addressing = 1;
			ha->isp_ops->calc_req_entries = qla2x00_calc_iocbs_64;
			ha->isp_ops->build_iocbs = qla2x00_build_scsi_iocbs_64;
			return;
		}
	}

	dma_set_mask(&ha->pdev->dev, DMA_32BIT_MASK);
	pci_set_consistent_dma_mask(ha->pdev, DMA_32BIT_MASK);
}

static void
qla2x00_enable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 1;
	/* enable risc and host interrupts */
	WRT_REG_WORD(&reg->ictrl, ICR_EN_INT | ICR_EN_RISC);
	RD_REG_WORD(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

}

static void
qla2x00_disable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 0;
	/* disable risc and host interrupts */
	WRT_REG_WORD(&reg->ictrl, 0);
	RD_REG_WORD(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void
qla24xx_enable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 1;
	WRT_REG_DWORD(&reg->ictrl, ICRX_EN_RISC_INT);
	RD_REG_DWORD(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void
qla24xx_disable_intrs(scsi_qla_host_t *ha)
{
	unsigned long flags = 0;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	ha->interrupts_on = 0;
	WRT_REG_DWORD(&reg->ictrl, 0);
	RD_REG_DWORD(&reg->ictrl);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static struct isp_operations qla2100_isp_ops = {
	.pci_config		= qla2100_pci_config,
	.reset_chip		= qla2x00_reset_chip,
	.chip_diag		= qla2x00_chip_diag,
	.config_rings		= qla2x00_config_rings,
	.reset_adapter		= qla2x00_reset_adapter,
	.nvram_config		= qla2x00_nvram_config,
	.update_fw_options	= qla2x00_update_fw_options,
	.load_risc		= qla2x00_load_risc,
	.pci_info_str		= qla2x00_pci_info_str,
	.fw_version_str		= qla2x00_fw_version_str,
	.intr_handler		= qla2100_intr_handler,
	.enable_intrs		= qla2x00_enable_intrs,
	.disable_intrs		= qla2x00_disable_intrs,
	.abort_command		= qla2x00_abort_command,
	.target_reset		= qla2x00_abort_target,
	.lun_reset		= qla2x00_lun_reset,
	.fabric_login		= qla2x00_login_fabric,
	.fabric_logout		= qla2x00_fabric_logout,
	.calc_req_entries	= qla2x00_calc_iocbs_32,
	.build_iocbs		= qla2x00_build_scsi_iocbs_32,
	.prep_ms_iocb		= qla2x00_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla2x00_prep_ms_fdmi_iocb,
	.read_nvram		= qla2x00_read_nvram_data,
	.write_nvram		= qla2x00_write_nvram_data,
	.fw_dump		= qla2100_fw_dump,
	.beacon_on		= NULL,
	.beacon_off		= NULL,
	.beacon_blink		= NULL,
	.read_optrom		= qla2x00_read_optrom_data,
	.write_optrom		= qla2x00_write_optrom_data,
	.get_flash_version	= qla2x00_get_flash_version,
};

static struct isp_operations qla2300_isp_ops = {
	.pci_config		= qla2300_pci_config,
	.reset_chip		= qla2x00_reset_chip,
	.chip_diag		= qla2x00_chip_diag,
	.config_rings		= qla2x00_config_rings,
	.reset_adapter		= qla2x00_reset_adapter,
	.nvram_config		= qla2x00_nvram_config,
	.update_fw_options	= qla2x00_update_fw_options,
	.load_risc		= qla2x00_load_risc,
	.pci_info_str		= qla2x00_pci_info_str,
	.fw_version_str		= qla2x00_fw_version_str,
	.intr_handler		= qla2300_intr_handler,
	.enable_intrs		= qla2x00_enable_intrs,
	.disable_intrs		= qla2x00_disable_intrs,
	.abort_command		= qla2x00_abort_command,
	.target_reset		= qla2x00_abort_target,
	.lun_reset		= qla2x00_lun_reset,
	.fabric_login		= qla2x00_login_fabric,
	.fabric_logout		= qla2x00_fabric_logout,
	.calc_req_entries	= qla2x00_calc_iocbs_32,
	.build_iocbs		= qla2x00_build_scsi_iocbs_32,
	.prep_ms_iocb		= qla2x00_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla2x00_prep_ms_fdmi_iocb,
	.read_nvram		= qla2x00_read_nvram_data,
	.write_nvram		= qla2x00_write_nvram_data,
	.fw_dump		= qla2300_fw_dump,
	.beacon_on		= qla2x00_beacon_on,
	.beacon_off		= qla2x00_beacon_off,
	.beacon_blink		= qla2x00_beacon_blink,
	.read_optrom		= qla2x00_read_optrom_data,
	.write_optrom		= qla2x00_write_optrom_data,
	.get_flash_version	= qla2x00_get_flash_version,
};

static struct isp_operations qla24xx_isp_ops = {
	.pci_config		= qla24xx_pci_config,
	.reset_chip		= qla24xx_reset_chip,
	.chip_diag		= qla24xx_chip_diag,
	.config_rings		= qla24xx_config_rings,
	.reset_adapter		= qla24xx_reset_adapter,
	.nvram_config		= qla24xx_nvram_config,
	.update_fw_options	= qla24xx_update_fw_options,
	.load_risc		= qla24xx_load_risc,
	.pci_info_str		= qla24xx_pci_info_str,
	.fw_version_str		= qla24xx_fw_version_str,
	.intr_handler		= qla24xx_intr_handler,
	.enable_intrs		= qla24xx_enable_intrs,
	.disable_intrs		= qla24xx_disable_intrs,
	.abort_command		= qla24xx_abort_command,
	.target_reset		= qla24xx_abort_target,
	.lun_reset		= qla24xx_lun_reset,
	.fabric_login		= qla24xx_login_fabric,
	.fabric_logout		= qla24xx_fabric_logout,
	.calc_req_entries	= NULL,
	.build_iocbs		= NULL,
	.prep_ms_iocb		= qla24xx_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla24xx_prep_ms_fdmi_iocb,
	.read_nvram		= qla24xx_read_nvram_data,
	.write_nvram		= qla24xx_write_nvram_data,
	.fw_dump		= qla24xx_fw_dump,
	.beacon_on		= qla24xx_beacon_on,
	.beacon_off		= qla24xx_beacon_off,
	.beacon_blink		= qla24xx_beacon_blink,
	.read_optrom		= qla24xx_read_optrom_data,
	.write_optrom		= qla24xx_write_optrom_data,
	.get_flash_version	= qla24xx_get_flash_version,
};

static struct isp_operations qla25xx_isp_ops = {
	.pci_config		= qla25xx_pci_config,
	.reset_chip		= qla24xx_reset_chip,
	.chip_diag		= qla24xx_chip_diag,
	.config_rings		= qla24xx_config_rings,
	.reset_adapter		= qla24xx_reset_adapter,
	.nvram_config		= qla24xx_nvram_config,
	.update_fw_options	= qla24xx_update_fw_options,
	.load_risc		= qla24xx_load_risc,
	.pci_info_str		= qla24xx_pci_info_str,
	.fw_version_str		= qla24xx_fw_version_str,
	.intr_handler		= qla24xx_intr_handler,
	.enable_intrs		= qla24xx_enable_intrs,
	.disable_intrs		= qla24xx_disable_intrs,
	.abort_command		= qla24xx_abort_command,
	.target_reset		= qla24xx_abort_target,
	.lun_reset		= qla24xx_lun_reset,
	.fabric_login		= qla24xx_login_fabric,
	.fabric_logout		= qla24xx_fabric_logout,
	.calc_req_entries	= NULL,
	.build_iocbs		= NULL,
	.prep_ms_iocb		= qla24xx_prep_ms_iocb,
	.prep_ms_fdmi_iocb	= qla24xx_prep_ms_fdmi_iocb,
	.read_nvram		= qla25xx_read_nvram_data,
	.write_nvram		= qla25xx_write_nvram_data,
	.fw_dump		= qla25xx_fw_dump,
	.beacon_on		= qla24xx_beacon_on,
	.beacon_off		= qla24xx_beacon_off,
	.beacon_blink		= qla24xx_beacon_blink,
	.read_optrom		= qla25xx_read_optrom_data,
	.write_optrom		= qla24xx_write_optrom_data,
	.get_flash_version	= qla24xx_get_flash_version,
};

static inline void
qla2x00_set_isp_flags(scsi_qla_host_t *ha)
{
	ha->device_type = DT_EXTENDED_IDS;
	switch (ha->pdev->device) {
	case PCI_DEVICE_ID_QLOGIC_ISP2100:
		ha->device_type |= DT_ISP2100;
		ha->device_type &= ~DT_EXTENDED_IDS;
		ha->fw_srisc_address = RISC_START_ADDRESS_2100;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2200:
		ha->device_type |= DT_ISP2200;
		ha->device_type &= ~DT_EXTENDED_IDS;
		ha->fw_srisc_address = RISC_START_ADDRESS_2100;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2300:
		ha->device_type |= DT_ISP2300;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->fw_srisc_address = RISC_START_ADDRESS_2300;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2312:
		ha->device_type |= DT_ISP2312;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->fw_srisc_address = RISC_START_ADDRESS_2300;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2322:
		ha->device_type |= DT_ISP2322;
		ha->device_type |= DT_ZIO_SUPPORTED;
		if (ha->pdev->subsystem_vendor == 0x1028 &&
		    ha->pdev->subsystem_device == 0x0170)
			ha->device_type |= DT_OEM_001;
		ha->fw_srisc_address = RISC_START_ADDRESS_2300;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP6312:
		ha->device_type |= DT_ISP6312;
		ha->fw_srisc_address = RISC_START_ADDRESS_2300;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP6322:
		ha->device_type |= DT_ISP6322;
		ha->fw_srisc_address = RISC_START_ADDRESS_2300;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2422:
		ha->device_type |= DT_ISP2422;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2432:
		ha->device_type |= DT_ISP2432;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP8432:
		ha->device_type |= DT_ISP8432;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP5422:
		ha->device_type |= DT_ISP5422;
		ha->device_type |= DT_FWI2;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP5432:
		ha->device_type |= DT_ISP5432;
		ha->device_type |= DT_FWI2;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	case PCI_DEVICE_ID_QLOGIC_ISP2532:
		ha->device_type |= DT_ISP2532;
		ha->device_type |= DT_ZIO_SUPPORTED;
		ha->device_type |= DT_FWI2;
		ha->device_type |= DT_IIDMA;
		ha->fw_srisc_address = RISC_START_ADDRESS_2400;
		break;
	}
}

static int
qla2x00_iospace_config(scsi_qla_host_t *ha)
{
	resource_size_t pio;

	if (pci_request_selected_regions(ha->pdev, ha->bars,
	    QLA2XXX_DRIVER_NAME)) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to reserve PIO/MMIO regions (%s)\n",
		    pci_name(ha->pdev));

		goto iospace_error_exit;
	}
	if (!(ha->bars & 1))
		goto skip_pio;

	/* We only need PIO for Flash operations on ISP2312 v2 chips. */
	pio = pci_resource_start(ha->pdev, 0);
	if (pci_resource_flags(ha->pdev, 0) & IORESOURCE_IO) {
		if (pci_resource_len(ha->pdev, 0) < MIN_IOBASE_LEN) {
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
	ha->pio_address = pio;

skip_pio:
	/* Use MMIO operations for all accesses. */
	if (!(pci_resource_flags(ha->pdev, 1) & IORESOURCE_MEM)) {
		qla_printk(KERN_ERR, ha,
		    "region #1 not an MMIO resource (%s), aborting\n",
		    pci_name(ha->pdev));
		goto iospace_error_exit;
	}
	if (pci_resource_len(ha->pdev, 1) < MIN_IOBASE_LEN) {
		qla_printk(KERN_ERR, ha,
		    "Invalid PCI mem region size (%s), aborting\n",
			pci_name(ha->pdev));
		goto iospace_error_exit;
	}

	ha->iobase = ioremap(pci_resource_start(ha->pdev, 1), MIN_IOBASE_LEN);
	if (!ha->iobase) {
		qla_printk(KERN_ERR, ha,
		    "cannot remap MMIO (%s), aborting\n", pci_name(ha->pdev));

		goto iospace_error_exit;
	}

	return (0);

iospace_error_exit:
	return (-ENOMEM);
}

static void
qla2xxx_scan_start(struct Scsi_Host *shost)
{
	scsi_qla_host_t *ha = shost_priv(shost);

	set_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags);
	set_bit(LOCAL_LOOP_UPDATE, &ha->dpc_flags);
	set_bit(RSCN_UPDATE, &ha->dpc_flags);
	set_bit(NPIV_CONFIG_NEEDED, &ha->dpc_flags);
}

static int
qla2xxx_scan_finished(struct Scsi_Host *shost, unsigned long time)
{
	scsi_qla_host_t *ha = shost_priv(shost);

	if (!ha->host)
		return 1;
	if (time > ha->loop_reset_delay * HZ)
		return 1;

	return atomic_read(&ha->loop_state) == LOOP_READY;
}

/*
 * PCI driver interface
 */
static int __devinit
qla2x00_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	int	ret = -ENODEV;
	struct Scsi_Host *host;
	scsi_qla_host_t *ha;
	char pci_info[30];
	char fw_str[30];
	struct scsi_host_template *sht;
	int bars, mem_only = 0;

	bars = pci_select_bars(pdev, IORESOURCE_MEM | IORESOURCE_IO);
	sht = &qla2x00_driver_template;
	if (pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2422 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2432 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP8432 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP5422 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP5432 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2532) {
		bars = pci_select_bars(pdev, IORESOURCE_MEM);
		sht = &qla24xx_driver_template;
		mem_only = 1;
	}

	if (mem_only) {
		if (pci_enable_device_mem(pdev))
			goto probe_out;
	} else {
		if (pci_enable_device(pdev))
			goto probe_out;
	}

	if (pci_find_aer_capability(pdev))
		if (pci_enable_pcie_error_reporting(pdev))
			goto probe_out;

	host = scsi_host_alloc(sht, sizeof(scsi_qla_host_t));
	if (host == NULL) {
		printk(KERN_WARNING
		    "qla2xxx: Couldn't allocate host from scsi layer!\n");
		goto probe_disable_device;
	}

	/* Clear our data area */
	ha = shost_priv(host);
	memset(ha, 0, sizeof(scsi_qla_host_t));

	ha->pdev = pdev;
	ha->host = host;
	ha->host_no = host->host_no;
	sprintf(ha->host_str, "%s_%ld", QLA2XXX_DRIVER_NAME, ha->host_no);
	ha->parent = NULL;
	ha->bars = bars;
	ha->mem_only = mem_only;
	spin_lock_init(&ha->hardware_lock);

	/* Set ISP-type information. */
	qla2x00_set_isp_flags(ha);

	/* Configure PCI I/O space */
	ret = qla2x00_iospace_config(ha);
	if (ret)
		goto probe_failed;

	qla_printk(KERN_INFO, ha,
	    "Found an ISP%04X, irq %d, iobase 0x%p\n", pdev->device, pdev->irq,
	    ha->iobase);

	ha->prev_topology = 0;
	ha->init_cb_size = sizeof(init_cb_t);
	ha->mgmt_svr_loop_id = MANAGEMENT_SERVER + ha->vp_idx;
	ha->link_data_rate = PORT_SPEED_UNKNOWN;
	ha->optrom_size = OPTROM_SIZE_2300;

	ha->max_q_depth = MAX_Q_DEPTH;
	if (ql2xmaxqdepth != 0 && ql2xmaxqdepth <= 0xffffU)
		ha->max_q_depth = ql2xmaxqdepth;

	/* Assign ISP specific operations. */
	if (IS_QLA2100(ha)) {
		host->max_id = MAX_TARGETS_2100;
		ha->mbx_count = MAILBOX_REGISTER_COUNT_2100;
		ha->request_q_length = REQUEST_ENTRY_CNT_2100;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2100;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2100;
		host->sg_tablesize = 32;
		ha->gid_list_info_size = 4;
		ha->isp_ops = &qla2100_isp_ops;
	} else if (IS_QLA2200(ha)) {
		host->max_id = MAX_TARGETS_2200;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		ha->request_q_length = REQUEST_ENTRY_CNT_2200;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2100;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2100;
		ha->gid_list_info_size = 4;
		ha->isp_ops = &qla2100_isp_ops;
	} else if (IS_QLA23XX(ha)) {
		host->max_id = MAX_TARGETS_2200;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		ha->request_q_length = REQUEST_ENTRY_CNT_2200;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2300;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->gid_list_info_size = 6;
		if (IS_QLA2322(ha) || IS_QLA6322(ha))
			ha->optrom_size = OPTROM_SIZE_2322;
		ha->isp_ops = &qla2300_isp_ops;
	} else if (IS_QLA24XX_TYPE(ha)) {
		host->max_id = MAX_TARGETS_2200;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		ha->request_q_length = REQUEST_ENTRY_CNT_24XX;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2300;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct mid_init_cb_24xx);
		ha->mgmt_svr_loop_id = 10 + ha->vp_idx;
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_24XX;
		ha->isp_ops = &qla24xx_isp_ops;
	} else if (IS_QLA25XX(ha)) {
		host->max_id = MAX_TARGETS_2200;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		ha->request_q_length = REQUEST_ENTRY_CNT_24XX;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2300;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct mid_init_cb_24xx);
		ha->mgmt_svr_loop_id = 10 + ha->vp_idx;
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_25XX;
		ha->isp_ops = &qla25xx_isp_ops;
	}
	host->can_queue = ha->request_q_length + 128;

	mutex_init(&ha->vport_lock);
	init_completion(&ha->mbx_cmd_comp);
	complete(&ha->mbx_cmd_comp);
	init_completion(&ha->mbx_intr_comp);

	INIT_LIST_HEAD(&ha->list);
	INIT_LIST_HEAD(&ha->fcports);
	INIT_LIST_HEAD(&ha->vp_list);
	INIT_LIST_HEAD(&ha->work_list);

	set_bit(0, (unsigned long *) ha->vp_idx_map);

	qla2x00_config_dma_addressing(ha);
	if (qla2x00_mem_alloc(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "[ERROR] Failed to allocate memory for adapter\n");

		ret = -ENOMEM;
		goto probe_failed;
	}

	if (qla2x00_initialize_adapter(ha)) {
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
	ha->dpc_thread = kthread_create(qla2x00_do_dpc, ha,
			"%s_dpc", ha->host_str);
	if (IS_ERR(ha->dpc_thread)) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to start DPC thread!\n");
		ret = PTR_ERR(ha->dpc_thread);
		goto probe_failed;
	}

	host->this_id = 255;
	host->cmd_per_lun = 3;
	host->unique_id = host->host_no;
	host->max_cmd_len = MAX_CMDSZ;
	host->max_channel = MAX_BUSES - 1;
	host->max_lun = MAX_LUNS;
	host->transportt = qla2xxx_transport_template;

	ret = qla2x00_request_irqs(ha);
	if (ret)
		goto probe_failed;

	/* Initialized the timer */
	qla2x00_start_timer(ha, qla2x00_timer, WATCH_INTERVAL);

	DEBUG2(printk("DEBUG: detect hba %ld at address = %p\n",
	    ha->host_no, ha));

	pci_set_drvdata(pdev, ha);

	ha->flags.init_done = 1;
	ha->flags.online = 1;

	ret = scsi_add_host(host, &pdev->dev);
	if (ret)
		goto probe_failed;

	ha->isp_ops->enable_intrs(ha);

	scsi_scan_host(host);

	qla2x00_alloc_sysfs_attr(ha);

	qla2x00_init_host_attr(ha);

	qla2x00_dfs_setup(ha);

	qla_printk(KERN_INFO, ha, "\n"
	    " QLogic Fibre Channel HBA Driver: %s\n"
	    "  QLogic %s - %s\n"
	    "  ISP%04X: %s @ %s hdma%c, host#=%ld, fw=%s\n",
	    qla2x00_version_str, ha->model_number,
	    ha->model_desc ? ha->model_desc: "", pdev->device,
	    ha->isp_ops->pci_info_str(ha, pci_info), pci_name(pdev),
	    ha->flags.enable_64bit_addressing ? '+': '-', ha->host_no,
	    ha->isp_ops->fw_version_str(ha, fw_str));

	return 0;

probe_failed:
	qla2x00_free_device(ha);

	scsi_host_put(host);

probe_disable_device:
	pci_disable_device(pdev);

probe_out:
	return ret;
}

static void
qla2x00_remove_one(struct pci_dev *pdev)
{
	scsi_qla_host_t *ha, *vha, *temp;

	ha = pci_get_drvdata(pdev);

	list_for_each_entry_safe(vha, temp, &ha->vp_list, vp_list)
		fc_vport_terminate(vha->fc_vport);

	set_bit(UNLOADING, &ha->dpc_flags);

	qla2x00_dfs_remove(ha);

	qla84xx_put_chip(ha);

	qla2x00_free_sysfs_attr(ha);

	fc_remove_host(ha->host);

	scsi_remove_host(ha->host);

	qla2x00_free_device(ha);

	scsi_host_put(ha->host);

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static void
qla2x00_free_device(scsi_qla_host_t *ha)
{
	qla2x00_abort_all_cmds(ha, DID_NO_CONNECT << 16);

	/* Disable timer */
	if (ha->timer_active)
		qla2x00_stop_timer(ha);

	ha->flags.online = 0;

	/* Kill the kernel thread for this host */
	if (ha->dpc_thread) {
		struct task_struct *t = ha->dpc_thread;

		/*
		 * qla2xxx_wake_dpc checks for ->dpc_thread
		 * so we need to zero it out.
		 */
		ha->dpc_thread = NULL;
		kthread_stop(t);
	}

	if (ha->flags.fce_enabled)
		qla2x00_disable_fce_trace(ha, NULL, NULL);

	if (ha->eft)
		qla2x00_disable_eft_trace(ha);

	/* Stop currently executing firmware. */
	qla2x00_try_to_stop_firmware(ha);

	/* turn-off interrupts on the card */
	if (ha->interrupts_on)
		ha->isp_ops->disable_intrs(ha);

	qla2x00_mem_free(ha);

	qla2x00_free_irqs(ha);

	/* release io space registers  */
	if (ha->iobase)
		iounmap(ha->iobase);
	pci_release_selected_regions(ha->pdev, ha->bars);
}

static inline void
qla2x00_schedule_rport_del(struct scsi_qla_host *ha, fc_port_t *fcport,
    int defer)
{
	struct fc_rport *rport;
	scsi_qla_host_t *pha = to_qla_parent(ha);

	if (!fcport->rport)
		return;

	rport = fcport->rport;
	if (defer) {
		spin_lock_irq(ha->host->host_lock);
		fcport->drport = rport;
		spin_unlock_irq(ha->host->host_lock);
		set_bit(FCPORT_UPDATE_NEEDED, &pha->dpc_flags);
		qla2xxx_wake_dpc(pha);
	} else
		fc_remote_port_delete(rport);
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
    int do_login, int defer)
{
	if (atomic_read(&fcport->state) == FCS_ONLINE &&
	    ha->vp_idx == fcport->vp_idx)
		qla2x00_schedule_rport_del(ha, fcport, defer);

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
qla2x00_mark_all_devices_lost(scsi_qla_host_t *ha, int defer)
{
	fc_port_t *fcport;
	scsi_qla_host_t *pha = to_qla_parent(ha);

	list_for_each_entry(fcport, &pha->fcports, list) {
		if (ha->vp_idx != fcport->vp_idx)
			continue;
		/*
		 * No point in marking the device as lost, if the device is
		 * already DEAD.
		 */
		if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD)
			continue;
		if (atomic_read(&fcport->state) == FCS_ONLINE)
			qla2x00_schedule_rport_del(ha, fcport, defer);
		atomic_set(&fcport->state, FCS_DEVICE_LOST);
	}
}

/*
* qla2x00_mem_alloc
*      Allocates adapter memory.
*
* Returns:
*      0  = success.
*      !0  = failure.
*/
static int
qla2x00_mem_alloc(scsi_qla_host_t *ha)
{
	char	name[16];

	ha->request_ring = dma_alloc_coherent(&ha->pdev->dev,
	    (ha->request_q_length + 1) * sizeof(request_t), &ha->request_dma,
	    GFP_KERNEL);
	if (!ha->request_ring)
		goto fail;

	ha->response_ring = dma_alloc_coherent(&ha->pdev->dev,
	    (ha->response_q_length + 1) * sizeof(response_t),
	    &ha->response_dma, GFP_KERNEL);
	if (!ha->response_ring)
		goto fail_free_request_ring;

	ha->gid_list = dma_alloc_coherent(&ha->pdev->dev, GID_LIST_SIZE,
	    &ha->gid_list_dma, GFP_KERNEL);
	if (!ha->gid_list)
		goto fail_free_response_ring;

	ha->init_cb = dma_alloc_coherent(&ha->pdev->dev, ha->init_cb_size,
	    &ha->init_cb_dma, GFP_KERNEL);
	if (!ha->init_cb)
		goto fail_free_gid_list;

	snprintf(name, sizeof(name), "%s_%ld", QLA2XXX_DRIVER_NAME,
	    ha->host_no);
	ha->s_dma_pool = dma_pool_create(name, &ha->pdev->dev,
	    DMA_POOL_SIZE, 8, 0);
	if (!ha->s_dma_pool)
		goto fail_free_init_cb;

	ha->srb_mempool = mempool_create_slab_pool(SRB_MIN_REQ, srb_cachep);
	if (!ha->srb_mempool)
		goto fail_free_s_dma_pool;

	/* Get memory for cached NVRAM */
	ha->nvram = kzalloc(MAX_NVRAM_SIZE, GFP_KERNEL);
	if (!ha->nvram)
		goto fail_free_srb_mempool;

	/* Allocate memory for SNS commands */
	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		/* Get consistent memory allocated for SNS commands */
		ha->sns_cmd = dma_alloc_coherent(&ha->pdev->dev,
		    sizeof(struct sns_cmd_pkt), &ha->sns_cmd_dma, GFP_KERNEL);
		if (!ha->sns_cmd)
			goto fail_free_nvram;
	} else {
		/* Get consistent memory allocated for MS IOCB */
		ha->ms_iocb = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL,
		    &ha->ms_iocb_dma);
		if (!ha->ms_iocb)
			goto fail_free_nvram;

		/* Get consistent memory allocated for CT SNS commands */
		ha->ct_sns = dma_alloc_coherent(&ha->pdev->dev,
		    sizeof(struct ct_sns_pkt), &ha->ct_sns_dma, GFP_KERNEL);
		if (!ha->ct_sns)
			goto fail_free_ms_iocb;
	}

	return 0;

fail_free_ms_iocb:
	dma_pool_free(ha->s_dma_pool, ha->ms_iocb, ha->ms_iocb_dma);
	ha->ms_iocb = NULL;
	ha->ms_iocb_dma = 0;
fail_free_nvram:
	kfree(ha->nvram);
	ha->nvram = NULL;
fail_free_srb_mempool:
	mempool_destroy(ha->srb_mempool);
	ha->srb_mempool = NULL;
fail_free_s_dma_pool:
	dma_pool_destroy(ha->s_dma_pool);
	ha->s_dma_pool = NULL;
fail_free_init_cb:
	dma_free_coherent(&ha->pdev->dev, ha->init_cb_size, ha->init_cb,
	    ha->init_cb_dma);
	ha->init_cb = NULL;
	ha->init_cb_dma = 0;
fail_free_gid_list:
	dma_free_coherent(&ha->pdev->dev, GID_LIST_SIZE, ha->gid_list,
	    ha->gid_list_dma);
	ha->gid_list = NULL;
	ha->gid_list_dma = 0;
fail_free_response_ring:
	dma_free_coherent(&ha->pdev->dev, (ha->response_q_length + 1) *
	    sizeof(response_t), ha->response_ring, ha->response_dma);
	ha->response_ring = NULL;
	ha->response_dma = 0;
fail_free_request_ring:
	dma_free_coherent(&ha->pdev->dev, (ha->request_q_length + 1) *
	    sizeof(request_t), ha->request_ring, ha->request_dma);
	ha->request_ring = NULL;
	ha->request_dma = 0;
fail:
	return -ENOMEM;
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

	if (ha->srb_mempool)
		mempool_destroy(ha->srb_mempool);

	if (ha->fce)
		dma_free_coherent(&ha->pdev->dev, FCE_SIZE, ha->fce,
		    ha->fce_dma);

	if (ha->fw_dump) {
		if (ha->eft)
			dma_free_coherent(&ha->pdev->dev,
			    ntohl(ha->fw_dump->eft_size), ha->eft, ha->eft_dma);
		vfree(ha->fw_dump);
	}

	if (ha->sns_cmd)
		dma_free_coherent(&ha->pdev->dev, sizeof(struct sns_cmd_pkt),
		    ha->sns_cmd, ha->sns_cmd_dma);

	if (ha->ct_sns)
		dma_free_coherent(&ha->pdev->dev, sizeof(struct ct_sns_pkt),
		    ha->ct_sns, ha->ct_sns_dma);

	if (ha->sfp_data)
		dma_pool_free(ha->s_dma_pool, ha->sfp_data, ha->sfp_data_dma);

	if (ha->ms_iocb)
		dma_pool_free(ha->s_dma_pool, ha->ms_iocb, ha->ms_iocb_dma);

	if (ha->s_dma_pool)
		dma_pool_destroy(ha->s_dma_pool);

	if (ha->init_cb)
		dma_free_coherent(&ha->pdev->dev, ha->init_cb_size,
		    ha->init_cb, ha->init_cb_dma);

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

	ha->srb_mempool = NULL;
	ha->eft = NULL;
	ha->eft_dma = 0;
	ha->sns_cmd = NULL;
	ha->sns_cmd_dma = 0;
	ha->ct_sns = NULL;
	ha->ct_sns_dma = 0;
	ha->ms_iocb = NULL;
	ha->ms_iocb_dma = 0;
	ha->init_cb = NULL;
	ha->init_cb_dma = 0;

	ha->s_dma_pool = NULL;

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

	ha->fw_dump = NULL;
	ha->fw_dumped = 0;
	ha->fw_dump_reading = 0;

	vfree(ha->optrom_buffer);
	kfree(ha->nvram);
}

static struct qla_work_evt *
qla2x00_alloc_work(struct scsi_qla_host *ha, enum qla_work_type type,
    int locked)
{
	struct qla_work_evt *e;

	e = kzalloc(sizeof(struct qla_work_evt), locked ? GFP_ATOMIC:
	    GFP_KERNEL);
	if (!e)
		return NULL;

	INIT_LIST_HEAD(&e->list);
	e->type = type;
	e->flags = QLA_EVT_FLAG_FREE;
	return e;
}

static int
qla2x00_post_work(struct scsi_qla_host *ha, struct qla_work_evt *e, int locked)
{
	unsigned long uninitialized_var(flags);
	scsi_qla_host_t *pha = to_qla_parent(ha);

	if (!locked)
		spin_lock_irqsave(&pha->hardware_lock, flags);
	list_add_tail(&e->list, &ha->work_list);
	qla2xxx_wake_dpc(ha);
	if (!locked)
		spin_unlock_irqrestore(&pha->hardware_lock, flags);
	return QLA_SUCCESS;
}

int
qla2x00_post_aen_work(struct scsi_qla_host *ha, enum fc_host_event_code code,
    u32 data)
{
	struct qla_work_evt *e;

	e = qla2x00_alloc_work(ha, QLA_EVT_AEN, 1);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.aen.code = code;
	e->u.aen.data = data;
	return qla2x00_post_work(ha, e, 1);
}

int
qla2x00_post_hwe_work(struct scsi_qla_host *ha, uint16_t code, uint16_t d1,
    uint16_t d2, uint16_t d3)
{
	struct qla_work_evt *e;

	e = qla2x00_alloc_work(ha, QLA_EVT_HWE_LOG, 1);
	if (!e)
		return QLA_FUNCTION_FAILED;

	e->u.hwe.code = code;
	e->u.hwe.d1 = d1;
	e->u.hwe.d2 = d2;
	e->u.hwe.d3 = d3;
	return qla2x00_post_work(ha, e, 1);
}

static void
qla2x00_do_work(struct scsi_qla_host *ha)
{
	struct qla_work_evt *e;
	scsi_qla_host_t *pha = to_qla_parent(ha);

	spin_lock_irq(&pha->hardware_lock);
	while (!list_empty(&ha->work_list)) {
		e = list_entry(ha->work_list.next, struct qla_work_evt, list);
		list_del_init(&e->list);
		spin_unlock_irq(&pha->hardware_lock);

		switch (e->type) {
		case QLA_EVT_AEN:
			fc_host_post_event(ha->host, fc_get_event_number(),
			    e->u.aen.code, e->u.aen.data);
			break;
		case QLA_EVT_HWE_LOG:
			qla2xxx_hw_event_log(ha, e->u.hwe.code, e->u.hwe.d1,
			    e->u.hwe.d2, e->u.hwe.d3);
			break;
		}
		if (e->flags & QLA_EVT_FLAG_FREE)
			kfree(e);
		spin_lock_irq(&pha->hardware_lock);
	}
	spin_unlock_irq(&pha->hardware_lock);
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
	int		rval;
	scsi_qla_host_t *ha;
	fc_port_t	*fcport;
	uint8_t		status;
	uint16_t	next_loopid;
	struct scsi_qla_host *vha;
	int             i;


	ha = (scsi_qla_host_t *)data;

	set_user_nice(current, -20);

	while (!kthread_should_stop()) {
		DEBUG3(printk("qla2x00: DPC handler sleeping\n"));

		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		__set_current_state(TASK_RUNNING);

		DEBUG3(printk("qla2x00: DPC handler waking up\n"));

		/* Initialization not yet finished. Don't do anything yet. */
		if (!ha->flags.init_done)
			continue;

		DEBUG3(printk("scsi(%ld): DPC handler\n", ha->host_no));

		ha->dpc_active = 1;

		if (ha->flags.mbox_busy) {
			ha->dpc_active = 0;
			continue;
		}

		qla2x00_do_work(ha);

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

			for_each_mapped_vp_idx(ha, i) {
				list_for_each_entry(vha, &ha->vp_list,
				    vp_list) {
					if (i == vha->vp_idx) {
						set_bit(ISP_ABORT_NEEDED,
						    &vha->dpc_flags);
						break;
					}
				}
			}

			DEBUG(printk("scsi(%ld): dpc: qla2x00_abort_isp end\n",
			    ha->host_no));
		}

		if (test_bit(FCPORT_UPDATE_NEEDED, &ha->dpc_flags)) {
			qla2x00_update_fcports(ha);
			clear_bit(FCPORT_UPDATE_NEEDED, &ha->dpc_flags);
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
				/*
				 * If the port is not ONLINE then try to login
				 * to it if we haven't run out of retries.
				 */
				if (atomic_read(&fcport->state) != FCS_ONLINE &&
				    fcport->login_retry) {

					if (fcport->flags & FCF_FABRIC_DEVICE) {
						if (fcport->flags &
						    FCF_TAPE_PRESENT)
							ha->isp_ops->fabric_logout(
							    ha, fcport->loop_id,
							    fcport->d_id.b.domain,
							    fcport->d_id.b.area,
							    fcport->d_id.b.al_pa);
						status = qla2x00_fabric_login(
						    ha, fcport, &next_loopid);
					} else
						status =
						    qla2x00_local_device_login(
							ha, fcport);

					fcport->login_retry--;
					if (status == QLA_SUCCESS) {
						fcport->old_loop_id = fcport->loop_id;

						DEBUG(printk("scsi(%ld): port login OK: logged in ID 0x%x\n",
						    ha->host_no, fcport->loop_id));

						qla2x00_update_fcport(ha,
						    fcport);
					} else if (status == 1) {
						set_bit(RELOGIN_NEEDED, &ha->dpc_flags);
						/* retry the login again */
						DEBUG(printk("scsi(%ld): Retrying %d login again loop_id 0x%x\n",
						    ha->host_no,
						    fcport->login_retry, fcport->loop_id));
					} else {
						fcport->login_retry = 0;
					}
					if (fcport->login_retry == 0 && status != QLA_SUCCESS)
						fcport->loop_id = FC_NO_LOOP_ID;
				}
				if (test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags))
					break;
			}
			DEBUG(printk("scsi(%ld): qla2x00_port_login - end\n",
			    ha->host_no));
		}

		if (test_and_clear_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags)) {

			DEBUG(printk("scsi(%ld): qla2x00_loop_resync()\n",
			    ha->host_no));

			if (!(test_and_set_bit(LOOP_RESYNC_ACTIVE,
			    &ha->dpc_flags))) {

				rval = qla2x00_loop_resync(ha);

				clear_bit(LOOP_RESYNC_ACTIVE, &ha->dpc_flags);
			}

			DEBUG(printk("scsi(%ld): qla2x00_loop_resync - end\n",
			    ha->host_no));
		}

		if (test_bit(NPIV_CONFIG_NEEDED, &ha->dpc_flags) &&
		    atomic_read(&ha->loop_state) == LOOP_READY) {
			clear_bit(NPIV_CONFIG_NEEDED, &ha->dpc_flags);
			qla2xxx_flash_npiv_conf(ha);
		}

		if (!ha->interrupts_on)
			ha->isp_ops->enable_intrs(ha);

		if (test_and_clear_bit(BEACON_BLINK_NEEDED, &ha->dpc_flags))
			ha->isp_ops->beacon_blink(ha);

		qla2x00_do_dpc_all_vps(ha);

		ha->dpc_active = 0;
	} /* End of while(1) */

	DEBUG(printk("scsi(%ld): DPC handler exiting\n", ha->host_no));

	/*
	 * Make sure that nobody tries to wake us up again.
	 */
	ha->dpc_active = 0;

	return 0;
}

void
qla2xxx_wake_dpc(scsi_qla_host_t *ha)
{
	struct task_struct *t = ha->dpc_thread;

	if (!test_bit(UNLOADING, &ha->dpc_flags) && t)
		wake_up_process(t);
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

static void
qla2x00_sp_free_dma(scsi_qla_host_t *ha, srb_t *sp)
{
	struct scsi_cmnd *cmd = sp->cmd;

	if (sp->flags & SRB_DMA_VALID) {
		scsi_dma_unmap(cmd);
		sp->flags &= ~SRB_DMA_VALID;
	}
	CMD_SP(cmd) = NULL;
}

void
qla2x00_sp_compl(scsi_qla_host_t *ha, srb_t *sp)
{
	struct scsi_cmnd *cmd = sp->cmd;

	qla2x00_sp_free_dma(ha, sp);

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
void
qla2x00_timer(scsi_qla_host_t *ha)
{
	unsigned long	cpu_flags = 0;
	fc_port_t	*fcport;
	int		start_dpc = 0;
	int		index;
	srb_t		*sp;
	int		t;
	scsi_qla_host_t *pha = to_qla_parent(ha);

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
			    "%d remaining\n",
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
			/* NPIV - scan physical port only */
			if (!ha->parent) {
				spin_lock_irqsave(&ha->hardware_lock,
				    cpu_flags);
				for (index = 1;
				    index < MAX_OUTSTANDING_COMMANDS;
				    index++) {
					fc_port_t *sfcp;

					sp = ha->outstanding_cmds[index];
					if (!sp)
						continue;
					sfcp = sp->fcport;
					if (!(sfcp->flags & FCF_TAPE_PRESENT))
						continue;

					set_bit(ISP_ABORT_NEEDED,
					    &ha->dpc_flags);
					break;
				}
				spin_unlock_irqrestore(&ha->hardware_lock,
				    cpu_flags);
			}
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

			if (!(ha->device_flags & DFLG_NO_CABLE) &&
			    !ha->parent) {
				DEBUG(printk("scsi(%ld): Loop down - "
				    "aborting ISP.\n",
				    ha->host_no));
				qla_printk(KERN_WARNING, ha,
				    "Loop down - aborting ISP.\n");

				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
			}
		}
		DEBUG3(printk("scsi(%ld): Loop Down - seconds remaining %d\n",
		    ha->host_no,
		    atomic_read(&ha->loop_down_timer)));
	}

	/* Check if beacon LED needs to be blinked */
	if (ha->beacon_blink_led == 1) {
		set_bit(BEACON_BLINK_NEEDED, &ha->dpc_flags);
		start_dpc++;
	}

	/* Process any deferred work. */
	if (!list_empty(&ha->work_list))
		start_dpc++;

	/* Schedule the DPC routine if needed */
	if ((test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
	    test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ||
	    test_bit(FCPORT_UPDATE_NEEDED, &ha->dpc_flags) ||
	    start_dpc ||
	    test_bit(RESET_MARKER_NEEDED, &ha->dpc_flags) ||
	    test_bit(BEACON_BLINK_NEEDED, &ha->dpc_flags) ||
	    test_bit(VP_DPC_NEEDED, &ha->dpc_flags) ||
	    test_bit(RELOGIN_NEEDED, &ha->dpc_flags)))
		qla2xxx_wake_dpc(pha);

	qla2x00_restart_timer(ha, WATCH_INTERVAL);
}

/* Firmware interface routines. */

#define FW_BLOBS	6
#define FW_ISP21XX	0
#define FW_ISP22XX	1
#define FW_ISP2300	2
#define FW_ISP2322	3
#define FW_ISP24XX	4
#define FW_ISP25XX	5

#define FW_FILE_ISP21XX	"ql2100_fw.bin"
#define FW_FILE_ISP22XX	"ql2200_fw.bin"
#define FW_FILE_ISP2300	"ql2300_fw.bin"
#define FW_FILE_ISP2322	"ql2322_fw.bin"
#define FW_FILE_ISP24XX	"ql2400_fw.bin"
#define FW_FILE_ISP25XX	"ql2500_fw.bin"

static DEFINE_MUTEX(qla_fw_lock);

static struct fw_blob qla_fw_blobs[FW_BLOBS] = {
	{ .name = FW_FILE_ISP21XX, .segs = { 0x1000, 0 }, },
	{ .name = FW_FILE_ISP22XX, .segs = { 0x1000, 0 }, },
	{ .name = FW_FILE_ISP2300, .segs = { 0x800, 0 }, },
	{ .name = FW_FILE_ISP2322, .segs = { 0x800, 0x1c000, 0x1e000, 0 }, },
	{ .name = FW_FILE_ISP24XX, },
	{ .name = FW_FILE_ISP25XX, },
};

struct fw_blob *
qla2x00_request_firmware(scsi_qla_host_t *ha)
{
	struct fw_blob *blob;

	blob = NULL;
	if (IS_QLA2100(ha)) {
		blob = &qla_fw_blobs[FW_ISP21XX];
	} else if (IS_QLA2200(ha)) {
		blob = &qla_fw_blobs[FW_ISP22XX];
	} else if (IS_QLA2300(ha) || IS_QLA2312(ha) || IS_QLA6312(ha)) {
		blob = &qla_fw_blobs[FW_ISP2300];
	} else if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
		blob = &qla_fw_blobs[FW_ISP2322];
	} else if (IS_QLA24XX_TYPE(ha)) {
		blob = &qla_fw_blobs[FW_ISP24XX];
	} else if (IS_QLA25XX(ha)) {
		blob = &qla_fw_blobs[FW_ISP25XX];
	}

	mutex_lock(&qla_fw_lock);
	if (blob->fw)
		goto out;

	if (request_firmware(&blob->fw, blob->name, &ha->pdev->dev)) {
		DEBUG2(printk("scsi(%ld): Failed to load firmware image "
		    "(%s).\n", ha->host_no, blob->name));
		blob->fw = NULL;
		blob = NULL;
		goto out;
	}

out:
	mutex_unlock(&qla_fw_lock);
	return blob;
}

static void
qla2x00_release_firmware(void)
{
	int idx;

	mutex_lock(&qla_fw_lock);
	for (idx = 0; idx < FW_BLOBS; idx++)
		if (qla_fw_blobs[idx].fw)
			release_firmware(qla_fw_blobs[idx].fw);
	mutex_unlock(&qla_fw_lock);
}

static pci_ers_result_t
qla2xxx_pci_error_detected(struct pci_dev *pdev, pci_channel_state_t state)
{
	switch (state) {
	case pci_channel_io_normal:
		return PCI_ERS_RESULT_CAN_RECOVER;
	case pci_channel_io_frozen:
		pci_disable_device(pdev);
		return PCI_ERS_RESULT_NEED_RESET;
	case pci_channel_io_perm_failure:
		qla2x00_remove_one(pdev);
		return PCI_ERS_RESULT_DISCONNECT;
	}
	return PCI_ERS_RESULT_NEED_RESET;
}

static pci_ers_result_t
qla2xxx_pci_mmio_enabled(struct pci_dev *pdev)
{
	int risc_paused = 0;
	uint32_t stat;
	unsigned long flags;
	scsi_qla_host_t *ha = pci_get_drvdata(pdev);
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	struct device_reg_24xx __iomem *reg24 = &ha->iobase->isp24;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (IS_QLA2100(ha) || IS_QLA2200(ha)){
		stat = RD_REG_DWORD(&reg->hccr);
		if (stat & HCCR_RISC_PAUSE)
			risc_paused = 1;
	} else if (IS_QLA23XX(ha)) {
		stat = RD_REG_DWORD(&reg->u.isp2300.host_status);
		if (stat & HSR_RISC_PAUSED)
			risc_paused = 1;
	} else if (IS_FWI2_CAPABLE(ha)) {
		stat = RD_REG_DWORD(&reg24->host_status);
		if (stat & HSRX_RISC_PAUSED)
			risc_paused = 1;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (risc_paused) {
		qla_printk(KERN_INFO, ha, "RISC paused -- mmio_enabled, "
		    "Dumping firmware!\n");
		ha->isp_ops->fw_dump(ha, 0);

		return PCI_ERS_RESULT_NEED_RESET;
	} else
		return PCI_ERS_RESULT_RECOVERED;
}

static pci_ers_result_t
qla2xxx_pci_slot_reset(struct pci_dev *pdev)
{
	pci_ers_result_t ret = PCI_ERS_RESULT_DISCONNECT;
	scsi_qla_host_t *ha = pci_get_drvdata(pdev);
	int rc;

	if (ha->mem_only)
		rc = pci_enable_device_mem(pdev);
	else
		rc = pci_enable_device(pdev);

	if (rc) {
		qla_printk(KERN_WARNING, ha,
		    "Can't re-enable PCI device after reset.\n");

		return ret;
	}
	pci_set_master(pdev);

	if (ha->isp_ops->pci_config(ha))
		return ret;

	set_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
	if (qla2x00_abort_isp(ha)== QLA_SUCCESS)
		ret =  PCI_ERS_RESULT_RECOVERED;
	clear_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);

	return ret;
}

static void
qla2xxx_pci_resume(struct pci_dev *pdev)
{
	scsi_qla_host_t *ha = pci_get_drvdata(pdev);
	int ret;

	ret = qla2x00_wait_for_hba_online(ha);
	if (ret != QLA_SUCCESS) {
		qla_printk(KERN_ERR, ha,
		    "the device failed to resume I/O "
		    "from slot/link_reset");
	}
	pci_cleanup_aer_uncorrect_error_status(pdev);
}

static struct pci_error_handlers qla2xxx_err_handler = {
	.error_detected = qla2xxx_pci_error_detected,
	.mmio_enabled = qla2xxx_pci_mmio_enabled,
	.slot_reset = qla2xxx_pci_slot_reset,
	.resume = qla2xxx_pci_resume,
};

static struct pci_device_id qla2xxx_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2100) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2200) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2300) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2312) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2322) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP6312) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP6322) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2422) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2432) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP8432) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP5422) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP5432) },
	{ PCI_DEVICE(PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2532) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, qla2xxx_pci_tbl);

static struct pci_driver qla2xxx_pci_driver = {
	.name		= QLA2XXX_DRIVER_NAME,
	.driver		= {
		.owner		= THIS_MODULE,
	},
	.id_table	= qla2xxx_pci_tbl,
	.probe		= qla2x00_probe_one,
	.remove		= qla2x00_remove_one,
	.err_handler	= &qla2xxx_err_handler,
};

/**
 * qla2x00_module_init - Module initialization.
 **/
static int __init
qla2x00_module_init(void)
{
	int ret = 0;

	/* Allocate cache for SRBs. */
	srb_cachep = kmem_cache_create("qla2xxx_srbs", sizeof(srb_t), 0,
	    SLAB_HWCACHE_ALIGN, NULL);
	if (srb_cachep == NULL) {
		printk(KERN_ERR
		    "qla2xxx: Unable to allocate SRB cache...Failing load!\n");
		return -ENOMEM;
	}

	/* Derive version string. */
	strcpy(qla2x00_version_str, QLA2XXX_VERSION);
	if (ql2xextended_error_logging)
		strcat(qla2x00_version_str, "-debug");

	qla2xxx_transport_template =
	    fc_attach_transport(&qla2xxx_transport_functions);
	if (!qla2xxx_transport_template) {
		kmem_cache_destroy(srb_cachep);
		return -ENODEV;
	}
	qla2xxx_transport_vport_template =
	    fc_attach_transport(&qla2xxx_transport_vport_functions);
	if (!qla2xxx_transport_vport_template) {
		kmem_cache_destroy(srb_cachep);
		fc_release_transport(qla2xxx_transport_template);
		return -ENODEV;
	}

	printk(KERN_INFO "QLogic Fibre Channel HBA Driver: %s\n",
	    qla2x00_version_str);
	ret = pci_register_driver(&qla2xxx_pci_driver);
	if (ret) {
		kmem_cache_destroy(srb_cachep);
		fc_release_transport(qla2xxx_transport_template);
		fc_release_transport(qla2xxx_transport_vport_template);
	}
	return ret;
}

/**
 * qla2x00_module_exit - Module cleanup.
 **/
static void __exit
qla2x00_module_exit(void)
{
	pci_unregister_driver(&qla2xxx_pci_driver);
	qla2x00_release_firmware();
	kmem_cache_destroy(srb_cachep);
	fc_release_transport(qla2xxx_transport_template);
	fc_release_transport(qla2xxx_transport_vport_template);
}

module_init(qla2x00_module_init);
module_exit(qla2x00_module_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic Fibre Channel HBA Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLA2XXX_VERSION);
MODULE_FIRMWARE(FW_FILE_ISP21XX);
MODULE_FIRMWARE(FW_FILE_ISP22XX);
MODULE_FIRMWARE(FW_FILE_ISP2300);
MODULE_FIRMWARE(FW_FILE_ISP2322);
MODULE_FIRMWARE(FW_FILE_ISP24XX);
MODULE_FIRMWARE(FW_FILE_ISP25XX);
