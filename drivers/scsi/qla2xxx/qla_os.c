/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
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
static kmem_cache_t *srb_cachep;

/*
 * Ioctl related information.
 */
static int num_hosts;

int ql2xlogintimeout = 20;
module_param(ql2xlogintimeout, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xlogintimeout,
		"Login timeout value in seconds.");

int qlport_down_retry = 30;
module_param(qlport_down_retry, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(qlport_down_retry,
		"Maximum number of command retries to a port that returns"
		"a PORT-DOWN status.");

int ql2xplogiabsentdevice;
module_param(ql2xplogiabsentdevice, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(ql2xplogiabsentdevice,
		"Option to enable PLOGI to devices that are not present after "
		"a Fabric scan.  This is needed for several broken switches."
		"Default is 0 - no PLOGI. 1 - perfom PLOGI.");

int ql2xloginretrycount = 0;
module_param(ql2xloginretrycount, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xloginretrycount,
		"Specify an alternate value for the NVRAM login retry count.");

#if defined(CONFIG_SCSI_QLA2XXX_EMBEDDED_FIRMWARE)
int ql2xfwloadflash;
module_param(ql2xfwloadflash, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xfwloadflash,
		"Load ISP24xx firmware image from FLASH (onboard memory).");
#endif

static void qla2x00_free_device(scsi_qla_host_t *);

static void qla2x00_config_dma_addressing(scsi_qla_host_t *ha);

int ql2xfdmienable;
module_param(ql2xfdmienable, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xfdmienable,
		"Enables FDMI registratons "
		"Default is 0 - no FDMI. 1 - perfom FDMI.");

int ql2xprocessrscn;
module_param(ql2xprocessrscn, int, S_IRUGO|S_IRUSR);
MODULE_PARM_DESC(ql2xprocessrscn,
		"Option to enable port RSCN handling via a series of less"
		"fabric intrusive ADISCs and PLOGIs.");

/*
 * SCSI host template entry points
 */
static int qla2xxx_slave_configure(struct scsi_device * device);
static int qla2xxx_slave_alloc(struct scsi_device *);
static void qla2xxx_slave_destroy(struct scsi_device *);
static int qla2x00_queuecommand(struct scsi_cmnd *cmd,
		void (*fn)(struct scsi_cmnd *));
static int qla24xx_queuecommand(struct scsi_cmnd *cmd,
		void (*fn)(struct scsi_cmnd *));
static int qla2xxx_eh_abort(struct scsi_cmnd *);
static int qla2xxx_eh_device_reset(struct scsi_cmnd *);
static int qla2xxx_eh_bus_reset(struct scsi_cmnd *);
static int qla2xxx_eh_host_reset(struct scsi_cmnd *);
static int qla2x00_loop_reset(scsi_qla_host_t *ha);
static int qla2x00_device_reset(scsi_qla_host_t *, fc_port_t *);

static int qla2x00_change_queue_depth(struct scsi_device *, int);
static int qla2x00_change_queue_type(struct scsi_device *, int);

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

static struct scsi_host_template qla24xx_driver_template = {
	.module			= THIS_MODULE,
	.name			= "qla2xxx",
	.queuecommand		= qla24xx_queuecommand,

	.eh_abort_handler	= qla2xxx_eh_abort,
	.eh_device_reset_handler = qla2xxx_eh_device_reset,
	.eh_bus_reset_handler	= qla2xxx_eh_bus_reset,
	.eh_host_reset_handler	= qla2xxx_eh_host_reset,

	.slave_configure	= qla2xxx_slave_configure,

	.slave_alloc		= qla2xxx_slave_alloc,
	.slave_destroy		= qla2xxx_slave_destroy,
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
static void qla2x00_sp_free_dma(scsi_qla_host_t *, srb_t *);
void qla2x00_sp_compl(scsi_qla_host_t *ha, srb_t *);

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
			strcat(str, "2.5Gb/s ");
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

char *
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

char *
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

	atomic_set(&sp->ref_count, 1);
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
	scsi_qla_host_t *ha = to_qla_host(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmd->device));
	srb_t *sp;
	int rval;

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		cmd->result = rval;
		goto qc_fail_command;
	}

	/* Close window on fcport/rport state-transitioning. */
	if (!*(fc_port_t **)rport->dd_data) {
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
	scsi_qla_host_t *ha = to_qla_host(cmd->device->host);
	fc_port_t *fcport = (struct fc_port *) cmd->device->hostdata;
	struct fc_rport *rport = starget_to_rport(scsi_target(cmd->device));
	srb_t *sp;
	int rval;

	rval = fc_remote_port_chkready(rport);
	if (rval) {
		cmd->result = rval;
		goto qc24_fail_command;
	}

	/* Close window on fcport/rport state-transitioning. */
	if (!*(fc_port_t **)rport->dd_data) {
		cmd->result = DID_IMM_RETRY << 16;
		goto qc24_fail_command;
	}

	if (atomic_read(&fcport->state) != FCS_ONLINE) {
		if (atomic_read(&fcport->state) == FCS_DEVICE_DEAD ||
		    atomic_read(&ha->loop_state) == LOOP_DEAD) {
			cmd->result = DID_NO_CONNECT << 16;
			goto qc24_fail_command;
		}
		goto qc24_host_busy;
	}

	spin_unlock_irq(ha->host->host_lock);

	sp = qla2x00_get_new_sp(ha, fcport, cmd, done);
	if (!sp)
		goto qc24_host_busy_lock;

	rval = qla24xx_start_scsi(sp);
	if (rval != QLA_SUCCESS)
		goto qc24_host_busy_free_sp;

	spin_lock_irq(ha->host->host_lock);

	return 0;

qc24_host_busy_free_sp:
	qla2x00_sp_free_dma(ha, sp);
	mempool_free(sp, ha->srb_mempool);

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
		if (ha->isp_ops.abort_command(ha, sp)) {
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

	if (qla2x00_wait_for_hba_online(ha) != QLA_SUCCESS)
		goto eh_dev_reset_done;

	if (qla2x00_wait_for_loop_ready(ha) == QLA_SUCCESS) {
		if (qla2x00_device_reset(ha, fcport) == 0)
			ret = SUCCESS;

#if defined(LOGOUT_AFTER_DEVICE_RESET)
		if (ret == SUCCESS) {
			if (fcport->flags & FC_FABRIC_DEVICE) {
				ha->isp_ops.fabric_logout(ha, fcport->loop_id);
				qla2x00_mark_device_lost(ha, fcport, 0, 0);
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

	/* Flush outstanding commands. */
	if (qla2x00_eh_wait_for_pending_target_commands(ha, id))
		ret = FAILED;
	if (ret == FAILED) {
		DEBUG3(printk("%s(%ld): failed while waiting for commands\n",
		    __func__, ha->host_no));
		qla_printk(KERN_INFO, ha,
		    "%s: failed while waiting for commands\n", __func__);
	} else
		qla_printk(KERN_INFO, ha,
		    "scsi(%ld:%d:%d): DEVICE RESET SUCCEEDED.\n", ha->host_no,
		    id, lun);
 eh_dev_reset_done:
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
	if (!qla2x00_eh_wait_for_pending_commands(ha))
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

			status = qla2x00_device_reset(ha, fcport);
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
	return ha->isp_ops.abort_target(reset_fcport);
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
			ha->isp_ops.calc_req_entries = qla2x00_calc_iocbs_64;
			ha->isp_ops.build_iocbs = qla2x00_build_scsi_iocbs_64;
			return;
		}
	}

	dma_set_mask(&ha->pdev->dev, DMA_32BIT_MASK);
	pci_set_consistent_dma_mask(ha->pdev, DMA_32BIT_MASK);
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
	struct scsi_host_template *sht;

	if (pci_enable_device(pdev))
		goto probe_out;

	sht = &qla2x00_driver_template;
	if (pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2422 ||
	    pdev->device == PCI_DEVICE_ID_QLOGIC_ISP2432)
		sht = &qla24xx_driver_template;
	host = scsi_host_alloc(sht, sizeof(scsi_qla_host_t));
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

	ha->dpc_pid = -1;

	/* Configure PCI I/O space */
	ret = qla2x00_iospace_config(ha);
	if (ret)
		goto probe_failed;

	qla_printk(KERN_INFO, ha,
	    "Found an ISP%04X, irq %d, iobase 0x%p\n", pdev->device, pdev->irq,
	    ha->iobase);

	spin_lock_init(&ha->hardware_lock);

	ha->prev_topology = 0;
	ha->ports = MAX_BUSES;
	ha->init_cb_size = sizeof(init_cb_t);
	ha->mgmt_svr_loop_id = MANAGEMENT_SERVER;
	ha->link_data_rate = LDR_UNKNOWN;
	ha->optrom_size = OPTROM_SIZE_2300;

	/* Assign ISP specific operations. */
	ha->isp_ops.pci_config		= qla2100_pci_config;
	ha->isp_ops.reset_chip		= qla2x00_reset_chip;
	ha->isp_ops.chip_diag		= qla2x00_chip_diag;
	ha->isp_ops.config_rings	= qla2x00_config_rings;
	ha->isp_ops.reset_adapter	= qla2x00_reset_adapter;
	ha->isp_ops.nvram_config	= qla2x00_nvram_config;
	ha->isp_ops.update_fw_options	= qla2x00_update_fw_options;
	ha->isp_ops.load_risc		= qla2x00_load_risc;
	ha->isp_ops.pci_info_str	= qla2x00_pci_info_str;
	ha->isp_ops.fw_version_str	= qla2x00_fw_version_str;
	ha->isp_ops.intr_handler	= qla2100_intr_handler;
	ha->isp_ops.enable_intrs	= qla2x00_enable_intrs;
	ha->isp_ops.disable_intrs	= qla2x00_disable_intrs;
	ha->isp_ops.abort_command	= qla2x00_abort_command;
	ha->isp_ops.abort_target	= qla2x00_abort_target;
	ha->isp_ops.fabric_login	= qla2x00_login_fabric;
	ha->isp_ops.fabric_logout	= qla2x00_fabric_logout;
	ha->isp_ops.calc_req_entries	= qla2x00_calc_iocbs_32;
	ha->isp_ops.build_iocbs		= qla2x00_build_scsi_iocbs_32;
	ha->isp_ops.prep_ms_iocb	= qla2x00_prep_ms_iocb;
	ha->isp_ops.prep_ms_fdmi_iocb	= qla2x00_prep_ms_fdmi_iocb;
	ha->isp_ops.read_nvram		= qla2x00_read_nvram_data;
	ha->isp_ops.write_nvram		= qla2x00_write_nvram_data;
	ha->isp_ops.fw_dump		= qla2100_fw_dump;
	ha->isp_ops.ascii_fw_dump	= qla2100_ascii_fw_dump;
	ha->isp_ops.read_optrom		= qla2x00_read_optrom_data;
	ha->isp_ops.write_optrom	= qla2x00_write_optrom_data;
	if (IS_QLA2100(ha)) {
		host->max_id = MAX_TARGETS_2100;
		ha->mbx_count = MAILBOX_REGISTER_COUNT_2100;
		ha->request_q_length = REQUEST_ENTRY_CNT_2100;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2100;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2100;
		host->sg_tablesize = 32;
		ha->gid_list_info_size = 4;
	} else if (IS_QLA2200(ha)) {
		host->max_id = MAX_TARGETS_2200;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		ha->request_q_length = REQUEST_ENTRY_CNT_2200;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2100;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2100;
		ha->gid_list_info_size = 4;
	} else if (IS_QLA23XX(ha)) {
		host->max_id = MAX_TARGETS_2200;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		ha->request_q_length = REQUEST_ENTRY_CNT_2200;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2300;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->isp_ops.pci_config = qla2300_pci_config;
		ha->isp_ops.intr_handler = qla2300_intr_handler;
		ha->isp_ops.fw_dump = qla2300_fw_dump;
		ha->isp_ops.ascii_fw_dump = qla2300_ascii_fw_dump;
		ha->isp_ops.beacon_on = qla2x00_beacon_on;
		ha->isp_ops.beacon_off = qla2x00_beacon_off;
		ha->isp_ops.beacon_blink = qla2x00_beacon_blink;
		ha->gid_list_info_size = 6;
		if (IS_QLA2322(ha) || IS_QLA6322(ha))
			ha->optrom_size = OPTROM_SIZE_2322;
	} else if (IS_QLA24XX(ha) || IS_QLA25XX(ha)) {
		host->max_id = MAX_TARGETS_2200;
		ha->mbx_count = MAILBOX_REGISTER_COUNT;
		ha->request_q_length = REQUEST_ENTRY_CNT_24XX;
		ha->response_q_length = RESPONSE_ENTRY_CNT_2300;
		ha->last_loop_id = SNS_LAST_LOOP_ID_2300;
		ha->init_cb_size = sizeof(struct init_cb_24xx);
		ha->mgmt_svr_loop_id = 10;
		ha->isp_ops.pci_config = qla24xx_pci_config;
		ha->isp_ops.reset_chip = qla24xx_reset_chip;
		ha->isp_ops.chip_diag = qla24xx_chip_diag;
		ha->isp_ops.config_rings = qla24xx_config_rings;
		ha->isp_ops.reset_adapter = qla24xx_reset_adapter;
		ha->isp_ops.nvram_config = qla24xx_nvram_config;
		ha->isp_ops.update_fw_options = qla24xx_update_fw_options;
		ha->isp_ops.load_risc = qla24xx_load_risc;
#if defined(CONFIG_SCSI_QLA2XXX_EMBEDDED_FIRMWARE)
		if (ql2xfwloadflash)
			ha->isp_ops.load_risc = qla24xx_load_risc_flash;
#endif
		ha->isp_ops.pci_info_str = qla24xx_pci_info_str;
		ha->isp_ops.fw_version_str = qla24xx_fw_version_str;
		ha->isp_ops.intr_handler = qla24xx_intr_handler;
		ha->isp_ops.enable_intrs = qla24xx_enable_intrs;
		ha->isp_ops.disable_intrs = qla24xx_disable_intrs;
		ha->isp_ops.abort_command = qla24xx_abort_command;
		ha->isp_ops.abort_target = qla24xx_abort_target;
		ha->isp_ops.fabric_login = qla24xx_login_fabric;
		ha->isp_ops.fabric_logout = qla24xx_fabric_logout;
		ha->isp_ops.prep_ms_iocb = qla24xx_prep_ms_iocb;
		ha->isp_ops.prep_ms_fdmi_iocb = qla24xx_prep_ms_fdmi_iocb;
		ha->isp_ops.read_nvram = qla24xx_read_nvram_data;
		ha->isp_ops.write_nvram = qla24xx_write_nvram_data;
		ha->isp_ops.fw_dump = qla24xx_fw_dump;
		ha->isp_ops.ascii_fw_dump = qla24xx_ascii_fw_dump;
		ha->isp_ops.read_optrom	= qla24xx_read_optrom_data;
		ha->isp_ops.write_optrom = qla24xx_write_optrom_data;
		ha->isp_ops.beacon_on = qla24xx_beacon_on;
		ha->isp_ops.beacon_off = qla24xx_beacon_off;
		ha->isp_ops.beacon_blink = qla24xx_beacon_blink;
		ha->gid_list_info_size = 8;
		ha->optrom_size = OPTROM_SIZE_24XX;
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

	ret = request_irq(pdev->irq, ha->isp_ops.intr_handler,
	    SA_INTERRUPT|SA_SHIRQ, ha->brd_info->drv_name, ha);
	if (ret) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to reserve interrupt %d already in use.\n",
		    pdev->irq);
		goto probe_failed;
	}
	host->irq = pdev->irq;

	/* Initialized the timer */
	qla2x00_start_timer(ha, qla2x00_timer, WATCH_INTERVAL);

	DEBUG2(printk("DEBUG: detect hba %ld at address = %p\n",
	    ha->host_no, ha));

	ha->isp_ops.disable_intrs(ha);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	reg = ha->iobase;
	if (IS_QLA24XX(ha) || IS_QLA25XX(ha)) {
		WRT_REG_DWORD(&reg->isp24.hccr, HCCRX_CLR_HOST_INT);
		WRT_REG_DWORD(&reg->isp24.hccr, HCCRX_CLR_RISC_INT);
	} else {
		WRT_REG_WORD(&reg->isp.semaphore, 0);
		WRT_REG_WORD(&reg->isp.hccr, HCCR_CLR_RISC_INT);
		WRT_REG_WORD(&reg->isp.hccr, HCCR_CLR_HOST_INT);

		/* Enable proper parity */
		if (!IS_QLA2100(ha) && !IS_QLA2200(ha)) {
			if (IS_QLA2300(ha))
				/* SRAM parity */
				WRT_REG_WORD(&reg->isp.hccr,
				    (HCCR_ENABLE_PARITY + 0x1));
			else
				/* SRAM, Instruction RAM and GP RAM parity */
				WRT_REG_WORD(&reg->isp.hccr,
				    (HCCR_ENABLE_PARITY + 0x7));
		}
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	ha->isp_ops.enable_intrs(ha);

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
	    "  ISP%04X: %s @ %s hdma%c, host#=%ld, fw=%s\n",
	    qla2x00_version_str, ha->model_number,
	    ha->model_desc ? ha->model_desc: "", pdev->device,
	    ha->isp_ops.pci_info_str(ha, pci_info), pci_name(pdev),
	    ha->flags.enable_64bit_addressing ? '+': '-', ha->host_no,
	    ha->isp_ops.fw_version_str(ha, fw_str));

	/* Go with fc_rport registration. */
	list_for_each_entry(fcport, &ha->fcports, list)
		qla2x00_reg_remote_port(ha, fcport);

	return 0;

probe_failed:
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

	/* Stop currently executing firmware. */
	qla2x00_stop_firmware(ha);

	/* turn-off interrupts on the card */
	if (ha->interrupts_on)
		ha->isp_ops.disable_intrs(ha);

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

static inline void
qla2x00_schedule_rport_del(struct scsi_qla_host *ha, fc_port_t *fcport,
    int defer)
{
	unsigned long flags;
	struct fc_rport *rport;

	if (!fcport->rport)
		return;

	rport = fcport->rport;
	if (defer) {
		spin_lock_irqsave(&fcport->rport_lock, flags);
		fcport->drport = rport;
		fcport->rport = NULL;
		*(fc_port_t **)rport->dd_data = NULL;
		spin_unlock_irqrestore(&fcport->rport_lock, flags);
		set_bit(FCPORT_UPDATE_NEEDED, &ha->dpc_flags);
	} else {
		spin_lock_irqsave(&fcport->rport_lock, flags);
		fcport->rport = NULL;
		*(fc_port_t **)rport->dd_data = NULL;
		spin_unlock_irqrestore(&fcport->rport_lock, flags);
		fc_remote_port_delete(rport);
	}
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
	if (atomic_read(&fcport->state) == FCS_ONLINE)
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

	list_for_each_entry(fcport, &ha->fcports, list) {
		if (fcport->port_type != FCT_TARGET)
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

	if (defer && ha->dpc_wait && !ha->dpc_active)
		up(ha->dpc_wait);
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
		memset(ha->init_cb, 0, ha->init_cb_size);

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
	unsigned int	wtime;/* max wait time if mbx cmd is busy. */

	if (ha == NULL) {
		/* error */
		DEBUG2(printk("%s(): ERROR invalid ha pointer.\n", __func__));
		return;
	}

	/* Make sure all other threads are stopped. */
	wtime = 60 * 1000;
	while (ha->dpc_wait && wtime)
		wtime = msleep_interruptible(wtime);

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

	vfree(ha->fw_dump24);

	vfree(ha->fw_dump_buffer);

	ha->fw_dump = NULL;
	ha->fw_dump24 = NULL;
	ha->fw_dumped = 0;
	ha->fw_dump_reading = 0;
	ha->fw_dump_buffer = NULL;

	vfree(ha->optrom_buffer);
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

		if (test_and_clear_bit(FCPORT_UPDATE_NEEDED, &ha->dpc_flags))
			qla2x00_update_fcports(ha);

		if (test_and_clear_bit(LOOP_RESET_NEEDED, &ha->dpc_flags)) {
			DEBUG(printk("scsi(%ld): dpc: sched loop_reset()\n",
			    ha->host_no));
			qla2x00_loop_reset(ha);
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
							ha->isp_ops.fabric_logout(
							    ha, fcport->loop_id,
							    fcport->d_id.b.domain,
							    fcport->d_id.b.area,
							    fcport->d_id.b.al_pa);
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
			ha->isp_ops.enable_intrs(ha);

		if (test_and_clear_bit(BEACON_BLINK_NEEDED, &ha->dpc_flags))
			ha->isp_ops.beacon_blink(ha);

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
		DEBUG3(printk("scsi(%ld): Loop Down - seconds remaining %d\n",
		    ha->host_no,
		    atomic_read(&ha->loop_down_timer)));
	}

	/* Check if beacon LED needs to be blinked */
	if (ha->beacon_blink_led == 1) {
		set_bit(BEACON_BLINK_NEEDED, &ha->dpc_flags);
		start_dpc++;
	}

	/* Schedule the DPC routine if needed */
	if ((test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags) ||
	    test_bit(LOOP_RESYNC_NEEDED, &ha->dpc_flags) ||
	    test_bit(LOOP_RESET_NEEDED, &ha->dpc_flags) ||
	    test_bit(FCPORT_UPDATE_NEEDED, &ha->dpc_flags) ||
	    start_dpc ||
	    test_bit(LOGIN_RETRY_NEEDED, &ha->dpc_flags) ||
	    test_bit(RESET_MARKER_NEEDED, &ha->dpc_flags) ||
	    test_bit(BEACON_BLINK_NEEDED, &ha->dpc_flags) ||
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
	const unsigned int step = 100; /* msecs */
	unsigned int iterations = jiffies_to_msecs(timeout)/100;

	do {
		if (!down_trylock(sema))
			return 0;
		if (msleep_interruptible(step))
			break;
	} while (--iterations >= 0);

	return -ETIMEDOUT;
}

#if defined(CONFIG_SCSI_QLA2XXX_EMBEDDED_FIRMWARE)

#define qla2x00_release_firmware()	do { } while (0)
#define qla2x00_pci_module_init()	(0)
#define qla2x00_pci_module_exit()	do { } while (0)

#else	/* !defined(CONFIG_SCSI_QLA2XXX_EMBEDDED_FIRMWARE) */

/* Firmware interface routines. */

#define FW_BLOBS	6
#define FW_ISP21XX	0
#define FW_ISP22XX	1
#define FW_ISP2300	2
#define FW_ISP2322	3
#define FW_ISP63XX	4
#define FW_ISP24XX	5

static DECLARE_MUTEX(qla_fw_lock);

static struct fw_blob qla_fw_blobs[FW_BLOBS] = {
	{ .name = "ql2100_fw.bin", .segs = { 0x1000, 0 }, },
	{ .name = "ql2200_fw.bin", .segs = { 0x1000, 0 }, },
	{ .name = "ql2300_fw.bin", .segs = { 0x800, 0 }, },
	{ .name = "ql2322_fw.bin", .segs = { 0x800, 0x1c000, 0x1e000, 0 }, },
	{ .name = "ql6312_fw.bin", .segs = { 0x800, 0 }, },
	{ .name = "ql2400_fw.bin", },
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
	} else if (IS_QLA2300(ha) || IS_QLA2312(ha)) {
		blob = &qla_fw_blobs[FW_ISP2300];
	} else if (IS_QLA2322(ha)) {
		blob = &qla_fw_blobs[FW_ISP2322];
	} else if (IS_QLA6312(ha) || IS_QLA6322(ha)) {
		blob = &qla_fw_blobs[FW_ISP63XX];
	} else if (IS_QLA24XX(ha)) {
		blob = &qla_fw_blobs[FW_ISP24XX];
	}

	down(&qla_fw_lock);
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
	up(&qla_fw_lock);
	return blob;
}

static void
qla2x00_release_firmware(void)
{
	int idx;

	down(&qla_fw_lock);
	for (idx = 0; idx < FW_BLOBS; idx++)
		if (qla_fw_blobs[idx].fw)
			release_firmware(qla_fw_blobs[idx].fw);
	up(&qla_fw_lock);
}

static struct qla_board_info qla_board_tbl = {
	.drv_name       = "qla2xxx",
};

static struct pci_device_id qla2xxx_pci_tbl[] = {
	{ PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2100,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2200,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2300,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2312,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2322,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP6312,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP6322,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2422,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_QLOGIC, PCI_DEVICE_ID_QLOGIC_ISP2432,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, qla2xxx_pci_tbl);

static int __devinit
qla2xxx_probe_one(struct pci_dev *pdev, const struct pci_device_id *id)
{
	return qla2x00_probe_one(pdev, &qla_board_tbl);
}

static void __devexit
qla2xxx_remove_one(struct pci_dev *pdev)
{
	qla2x00_remove_one(pdev);
}

static struct pci_driver qla2xxx_pci_driver = {
	.name		= "qla2xxx",
	.driver		= {
		.owner		= THIS_MODULE,
	},
	.id_table	= qla2xxx_pci_tbl,
	.probe		= qla2xxx_probe_one,
	.remove		= __devexit_p(qla2xxx_remove_one),
};

static inline int
qla2x00_pci_module_init(void)
{
	return pci_module_init(&qla2xxx_pci_driver);
}

static inline void
qla2x00_pci_module_exit(void)
{
	pci_unregister_driver(&qla2xxx_pci_driver);
}

#endif

/**
 * qla2x00_module_init - Module initialization.
 **/
static int __init
qla2x00_module_init(void)
{
	int ret = 0;

	/* Allocate cache for SRBs. */
	srb_cachep = kmem_cache_create("qla2xxx_srbs", sizeof(srb_t), 0,
	    SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (srb_cachep == NULL) {
		printk(KERN_ERR
		    "qla2xxx: Unable to allocate SRB cache...Failing load!\n");
		return -ENOMEM;
	}

	/* Derive version string. */
	strcpy(qla2x00_version_str, QLA2XXX_VERSION);
#if defined(CONFIG_SCSI_QLA2XXX_EMBEDDED_FIRMWARE)
	strcat(qla2x00_version_str, "-fw");
#endif
#if DEBUG_QLA2100
	strcat(qla2x00_version_str, "-debug");
#endif
	qla2xxx_transport_template =
	    fc_attach_transport(&qla2xxx_transport_functions);
	if (!qla2xxx_transport_template)
		return -ENODEV;

	printk(KERN_INFO "QLogic Fibre Channel HBA Driver\n");
	ret = qla2x00_pci_module_init();
	if (ret) {
		kmem_cache_destroy(srb_cachep);
		fc_release_transport(qla2xxx_transport_template);
	}
	return ret;
}

/**
 * qla2x00_module_exit - Module cleanup.
 **/
static void __exit
qla2x00_module_exit(void)
{
	qla2x00_pci_module_exit();
	qla2x00_release_firmware();
	kmem_cache_destroy(srb_cachep);
	fc_release_transport(qla2xxx_transport_template);
}

module_init(qla2x00_module_init);
module_exit(qla2x00_module_exit);

MODULE_AUTHOR("QLogic Corporation");
MODULE_DESCRIPTION("QLogic Fibre Channel HBA Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(QLA2XXX_VERSION);
