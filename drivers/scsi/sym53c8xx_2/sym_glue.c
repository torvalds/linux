/*
 * Device driver for the SYMBIOS/LSILOGIC 53C8XX and 53C1010 family 
 * of PCI-SCSI IO processors.
 *
 * Copyright (C) 1999-2001  Gerard Roudier <groudier@free.fr>
 * Copyright (c) 2003-2005  Matthew Wilcox <matthew@wil.cx>
 *
 * This driver is derived from the Linux sym53c8xx driver.
 * Copyright (C) 1998-2000  Gerard Roudier
 *
 * The sym53c8xx driver is derived from the ncr53c8xx driver that had been 
 * a port of the FreeBSD ncr driver to Linux-1.2.13.
 *
 * The original ncr driver has been written for 386bsd and FreeBSD by
 *         Wolfgang Stanglmeier        <wolf@cologne.de>
 *         Stefan Esser                <se@mi.Uni-Koeln.de>
 * Copyright (C) 1994  Wolfgang Stanglmeier
 *
 * Other major contributions:
 *
 * NVRAM detection and reading.
 * Copyright (C) 1997 Richard Waltham <dormouse@farsrobt.demon.co.uk>
 *
 *-----------------------------------------------------------------------------
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
 */
#include <linux/ctype.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/spinlock.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>

#include "sym_glue.h"
#include "sym_nvram.h"

#define NAME53C		"sym53c"
#define NAME53C8XX	"sym53c8xx"

struct sym_driver_setup sym_driver_setup = SYM_LINUX_DRIVER_SETUP;
unsigned int sym_debug_flags = 0;

static char *excl_string;
static char *safe_string;
module_param_named(cmd_per_lun, sym_driver_setup.max_tag, ushort, 0);
module_param_named(burst, sym_driver_setup.burst_order, byte, 0);
module_param_named(led, sym_driver_setup.scsi_led, byte, 0);
module_param_named(diff, sym_driver_setup.scsi_diff, byte, 0);
module_param_named(irqm, sym_driver_setup.irq_mode, byte, 0);
module_param_named(buschk, sym_driver_setup.scsi_bus_check, byte, 0);
module_param_named(hostid, sym_driver_setup.host_id, byte, 0);
module_param_named(verb, sym_driver_setup.verbose, byte, 0);
module_param_named(debug, sym_debug_flags, uint, 0);
module_param_named(settle, sym_driver_setup.settle_delay, byte, 0);
module_param_named(nvram, sym_driver_setup.use_nvram, byte, 0);
module_param_named(excl, excl_string, charp, 0);
module_param_named(safe, safe_string, charp, 0);

MODULE_PARM_DESC(cmd_per_lun, "The maximum number of tags to use by default");
MODULE_PARM_DESC(burst, "Maximum burst.  0 to disable, 255 to read from registers");
MODULE_PARM_DESC(led, "Set to 1 to enable LED support");
MODULE_PARM_DESC(diff, "0 for no differential mode, 1 for BIOS, 2 for always, 3 for not GPIO3");
MODULE_PARM_DESC(irqm, "0 for open drain, 1 to leave alone, 2 for totem pole");
MODULE_PARM_DESC(buschk, "0 to not check, 1 for detach on error, 2 for warn on error");
MODULE_PARM_DESC(hostid, "The SCSI ID to use for the host adapters");
MODULE_PARM_DESC(verb, "0 for minimal verbosity, 1 for normal, 2 for excessive");
MODULE_PARM_DESC(debug, "Set bits to enable debugging");
MODULE_PARM_DESC(settle, "Settle delay in seconds.  Default 3");
MODULE_PARM_DESC(nvram, "Option currently not used");
MODULE_PARM_DESC(excl, "List ioport addresses here to prevent controllers from being attached");
MODULE_PARM_DESC(safe, "Set other settings to a \"safe mode\"");

MODULE_LICENSE("GPL");
MODULE_VERSION(SYM_VERSION);
MODULE_AUTHOR("Matthew Wilcox <matthew@wil.cx>");
MODULE_DESCRIPTION("NCR, Symbios and LSI 8xx and 1010 PCI SCSI adapters");

static void sym2_setup_params(void)
{
	char *p = excl_string;
	int xi = 0;

	while (p && (xi < 8)) {
		char *next_p;
		int val = (int) simple_strtoul(p, &next_p, 0);
		sym_driver_setup.excludes[xi++] = val;
		p = next_p;
	}

	if (safe_string) {
		if (*safe_string == 'y') {
			sym_driver_setup.max_tag = 0;
			sym_driver_setup.burst_order = 0;
			sym_driver_setup.scsi_led = 0;
			sym_driver_setup.scsi_diff = 1;
			sym_driver_setup.irq_mode = 0;
			sym_driver_setup.scsi_bus_check = 2;
			sym_driver_setup.host_id = 7;
			sym_driver_setup.verbose = 2;
			sym_driver_setup.settle_delay = 10;
			sym_driver_setup.use_nvram = 1;
		} else if (*safe_string != 'n') {
			printk(KERN_WARNING NAME53C8XX "Ignoring parameter %s"
					" passed to safe option", safe_string);
		}
	}
}

static struct scsi_transport_template *sym2_transport_template = NULL;

/*
 *  Driver private area in the SCSI command structure.
 */
struct sym_ucmd {		/* Override the SCSI pointer structure */
	struct completion *eh_done;		/* SCSI error handling */
};

#define SYM_UCMD_PTR(cmd)  ((struct sym_ucmd *)(&(cmd)->SCp))
#define SYM_SOFTC_PTR(cmd) sym_get_hcb(cmd->device->host)

/*
 *  Complete a pending CAM CCB.
 */
void sym_xpt_done(struct sym_hcb *np, struct scsi_cmnd *cmd)
{
	struct sym_ucmd *ucmd = SYM_UCMD_PTR(cmd);
	BUILD_BUG_ON(sizeof(struct scsi_pointer) < sizeof(struct sym_ucmd));

	if (ucmd->eh_done)
		complete(ucmd->eh_done);

	scsi_dma_unmap(cmd);
	cmd->scsi_done(cmd);
}

/*
 *  Tell the SCSI layer about a BUS RESET.
 */
void sym_xpt_async_bus_reset(struct sym_hcb *np)
{
	printf_notice("%s: SCSI BUS has been reset.\n", sym_name(np));
	np->s.settle_time = jiffies + sym_driver_setup.settle_delay * HZ;
	np->s.settle_time_valid = 1;
	if (sym_verbose >= 2)
		printf_info("%s: command processing suspended for %d seconds\n",
			    sym_name(np), sym_driver_setup.settle_delay);
}

/*
 *  Choose the more appropriate CAM status if 
 *  the IO encountered an extended error.
 */
static int sym_xerr_cam_status(int cam_status, int x_status)
{
	if (x_status) {
		if	(x_status & XE_PARITY_ERR)
			cam_status = DID_PARITY;
		else if	(x_status &(XE_EXTRA_DATA|XE_SODL_UNRUN|XE_SWIDE_OVRUN))
			cam_status = DID_ERROR;
		else if	(x_status & XE_BAD_PHASE)
			cam_status = DID_ERROR;
		else
			cam_status = DID_ERROR;
	}
	return cam_status;
}

/*
 *  Build CAM result for a failed or auto-sensed IO.
 */
void sym_set_cam_result_error(struct sym_hcb *np, struct sym_ccb *cp, int resid)
{
	struct scsi_cmnd *cmd = cp->cmd;
	u_int cam_status, scsi_status, drv_status;

	drv_status  = 0;
	cam_status  = DID_OK;
	scsi_status = cp->ssss_status;

	if (cp->host_flags & HF_SENSE) {
		scsi_status = cp->sv_scsi_status;
		resid = cp->sv_resid;
		if (sym_verbose && cp->sv_xerr_status)
			sym_print_xerr(cmd, cp->sv_xerr_status);
		if (cp->host_status == HS_COMPLETE &&
		    cp->ssss_status == S_GOOD &&
		    cp->xerr_status == 0) {
			cam_status = sym_xerr_cam_status(DID_OK,
							 cp->sv_xerr_status);
			drv_status = DRIVER_SENSE;
			/*
			 *  Bounce back the sense data to user.
			 */
			memset(cmd->sense_buffer, 0, SCSI_SENSE_BUFFERSIZE);
			memcpy(cmd->sense_buffer, cp->sns_bbuf,
			       min(SCSI_SENSE_BUFFERSIZE, SYM_SNS_BBUF_LEN));
#if 0
			/*
			 *  If the device reports a UNIT ATTENTION condition 
			 *  due to a RESET condition, we should consider all 
			 *  disconnect CCBs for this unit as aborted.
			 */
			if (1) {
				u_char *p;
				p  = (u_char *) cmd->sense_data;
				if (p[0]==0x70 && p[2]==0x6 && p[12]==0x29)
					sym_clear_tasks(np, DID_ABORT,
							cp->target,cp->lun, -1);
			}
#endif
		} else {
			/*
			 * Error return from our internal request sense.  This
			 * is bad: we must clear the contingent allegiance
			 * condition otherwise the device will always return
			 * BUSY.  Use a big stick.
			 */
			sym_reset_scsi_target(np, cmd->device->id);
			cam_status = DID_ERROR;
		}
	} else if (cp->host_status == HS_COMPLETE) 	/* Bad SCSI status */
		cam_status = DID_OK;
	else if (cp->host_status == HS_SEL_TIMEOUT)	/* Selection timeout */
		cam_status = DID_NO_CONNECT;
	else if (cp->host_status == HS_UNEXPECTED)	/* Unexpected BUS FREE*/
		cam_status = DID_ERROR;
	else {						/* Extended error */
		if (sym_verbose) {
			sym_print_addr(cmd, "COMMAND FAILED (%x %x %x).\n",
				cp->host_status, cp->ssss_status,
				cp->xerr_status);
		}
		/*
		 *  Set the most appropriate value for CAM status.
		 */
		cam_status = sym_xerr_cam_status(DID_ERROR, cp->xerr_status);
	}
	scsi_set_resid(cmd, resid);
	cmd->result = (drv_status << 24) + (cam_status << 16) + scsi_status;
}

static int sym_scatter(struct sym_hcb *np, struct sym_ccb *cp, struct scsi_cmnd *cmd)
{
	int segment;
	int use_sg;

	cp->data_len = 0;

	use_sg = scsi_dma_map(cmd);
	if (use_sg > 0) {
		struct scatterlist *sg;
		struct sym_tcb *tp = &np->target[cp->target];
		struct sym_tblmove *data;

		if (use_sg > SYM_CONF_MAX_SG) {
			scsi_dma_unmap(cmd);
			return -1;
		}

		data = &cp->phys.data[SYM_CONF_MAX_SG - use_sg];

		scsi_for_each_sg(cmd, sg, use_sg, segment) {
			dma_addr_t baddr = sg_dma_address(sg);
			unsigned int len = sg_dma_len(sg);

			if ((len & 1) && (tp->head.wval & EWS)) {
				len++;
				cp->odd_byte_adjustment++;
			}

			sym_build_sge(np, &data[segment], baddr, len);
			cp->data_len += len;
		}
	} else {
		segment = -2;
	}

	return segment;
}

/*
 *  Queue a SCSI command.
 */
static int sym_queue_command(struct sym_hcb *np, struct scsi_cmnd *cmd)
{
	struct scsi_device *sdev = cmd->device;
	struct sym_tcb *tp;
	struct sym_lcb *lp;
	struct sym_ccb *cp;
	int	order;

	/*
	 *  Retrieve the target descriptor.
	 */
	tp = &np->target[sdev->id];

	/*
	 *  Select tagged/untagged.
	 */
	lp = sym_lp(tp, sdev->lun);
	order = (lp && lp->s.reqtags) ? M_SIMPLE_TAG : 0;

	/*
	 *  Queue the SCSI IO.
	 */
	cp = sym_get_ccb(np, cmd, order);
	if (!cp)
		return 1;	/* Means resource shortage */
	sym_queue_scsiio(np, cmd, cp);
	return 0;
}

/*
 *  Setup buffers and pointers that address the CDB.
 */
static inline int sym_setup_cdb(struct sym_hcb *np, struct scsi_cmnd *cmd, struct sym_ccb *cp)
{
	memcpy(cp->cdb_buf, cmd->cmnd, cmd->cmd_len);

	cp->phys.cmd.addr = CCB_BA(cp, cdb_buf[0]);
	cp->phys.cmd.size = cpu_to_scr(cmd->cmd_len);

	return 0;
}

/*
 *  Setup pointers that address the data and start the I/O.
 */
int sym_setup_data_and_start(struct sym_hcb *np, struct scsi_cmnd *cmd, struct sym_ccb *cp)
{
	u32 lastp, goalp;
	int dir;

	/*
	 *  Build the CDB.
	 */
	if (sym_setup_cdb(np, cmd, cp))
		goto out_abort;

	/*
	 *  No direction means no data.
	 */
	dir = cmd->sc_data_direction;
	if (dir != DMA_NONE) {
		cp->segments = sym_scatter(np, cp, cmd);
		if (cp->segments < 0) {
			sym_set_cam_status(cmd, DID_ERROR);
			goto out_abort;
		}

		/*
		 *  No segments means no data.
		 */
		if (!cp->segments)
			dir = DMA_NONE;
	} else {
		cp->data_len = 0;
		cp->segments = 0;
	}

	/*
	 *  Set the data pointer.
	 */
	switch (dir) {
	case DMA_BIDIRECTIONAL:
		scmd_printk(KERN_INFO, cmd, "got DMA_BIDIRECTIONAL command");
		sym_set_cam_status(cmd, DID_ERROR);
		goto out_abort;
	case DMA_TO_DEVICE:
		goalp = SCRIPTA_BA(np, data_out2) + 8;
		lastp = goalp - 8 - (cp->segments * (2*4));
		break;
	case DMA_FROM_DEVICE:
		cp->host_flags |= HF_DATA_IN;
		goalp = SCRIPTA_BA(np, data_in2) + 8;
		lastp = goalp - 8 - (cp->segments * (2*4));
		break;
	case DMA_NONE:
	default:
		lastp = goalp = SCRIPTB_BA(np, no_data);
		break;
	}

	/*
	 *  Set all pointers values needed by SCRIPTS.
	 */
	cp->phys.head.lastp = cpu_to_scr(lastp);
	cp->phys.head.savep = cpu_to_scr(lastp);
	cp->startp	    = cp->phys.head.savep;
	cp->goalp	    = cpu_to_scr(goalp);

	/*
	 *  When `#ifed 1', the code below makes the driver 
	 *  panic on the first attempt to write to a SCSI device.
	 *  It is the first test we want to do after a driver 
	 *  change that does not seem obviously safe. :)
	 */
#if 0
	switch (cp->cdb_buf[0]) {
	case 0x0A: case 0x2A: case 0xAA:
		panic("XXXXXXXXXXXXX WRITE NOT YET ALLOWED XXXXXXXXXXXXXX\n");
		break;
	default:
		break;
	}
#endif

	/*
	 *	activate this job.
	 */
	sym_put_start_queue(np, cp);
	return 0;

out_abort:
	sym_free_ccb(np, cp);
	sym_xpt_done(np, cmd);
	return 0;
}


/*
 *  timer daemon.
 *
 *  Misused to keep the driver running when
 *  interrupts are not configured correctly.
 */
static void sym_timer(struct sym_hcb *np)
{
	unsigned long thistime = jiffies;

	/*
	 *  Restart the timer.
	 */
	np->s.timer.expires = thistime + SYM_CONF_TIMER_INTERVAL;
	add_timer(&np->s.timer);

	/*
	 *  If we are resetting the ncr, wait for settle_time before 
	 *  clearing it. Then command processing will be resumed.
	 */
	if (np->s.settle_time_valid) {
		if (time_before_eq(np->s.settle_time, thistime)) {
			if (sym_verbose >= 2 )
				printk("%s: command processing resumed\n",
				       sym_name(np));
			np->s.settle_time_valid = 0;
		}
		return;
	}

	/*
	 *	Nothing to do for now, but that may come.
	 */
	if (np->s.lasttime + 4*HZ < thistime) {
		np->s.lasttime = thistime;
	}

#ifdef SYM_CONF_PCIQ_MAY_MISS_COMPLETIONS
	/*
	 *  Some way-broken PCI bridges may lead to 
	 *  completions being lost when the clearing 
	 *  of the INTFLY flag by the CPU occurs 
	 *  concurrently with the chip raising this flag.
	 *  If this ever happen, lost completions will 
	 * be reaped here.
	 */
	sym_wakeup_done(np);
#endif
}


/*
 *  PCI BUS error handler.
 */
void sym_log_bus_error(struct Scsi_Host *shost)
{
	struct sym_data *sym_data = shost_priv(shost);
	struct pci_dev *pdev = sym_data->pdev;
	unsigned short pci_sts;
	pci_read_config_word(pdev, PCI_STATUS, &pci_sts);
	if (pci_sts & 0xf900) {
		pci_write_config_word(pdev, PCI_STATUS, pci_sts);
		shost_printk(KERN_WARNING, shost,
			"PCI bus error: status = 0x%04x\n", pci_sts & 0xf900);
	}
}

/*
 * queuecommand method.  Entered with the host adapter lock held and
 * interrupts disabled.
 */
static int sym53c8xx_queue_command_lck(struct scsi_cmnd *cmd,
					void (*done)(struct scsi_cmnd *))
{
	struct sym_hcb *np = SYM_SOFTC_PTR(cmd);
	struct sym_ucmd *ucp = SYM_UCMD_PTR(cmd);
	int sts = 0;

	cmd->scsi_done = done;
	memset(ucp, 0, sizeof(*ucp));

	/*
	 *  Shorten our settle_time if needed for 
	 *  this command not to time out.
	 */
	if (np->s.settle_time_valid && cmd->request->timeout) {
		unsigned long tlimit = jiffies + cmd->request->timeout;
		tlimit -= SYM_CONF_TIMER_INTERVAL*2;
		if (time_after(np->s.settle_time, tlimit)) {
			np->s.settle_time = tlimit;
		}
	}

	if (np->s.settle_time_valid)
		return SCSI_MLQUEUE_HOST_BUSY;

	sts = sym_queue_command(np, cmd);
	if (sts)
		return SCSI_MLQUEUE_HOST_BUSY;
	return 0;
}

static DEF_SCSI_QCMD(sym53c8xx_queue_command)

/*
 *  Linux entry point of the interrupt handler.
 */
static irqreturn_t sym53c8xx_intr(int irq, void *dev_id)
{
	struct Scsi_Host *shost = dev_id;
	struct sym_data *sym_data = shost_priv(shost);
	irqreturn_t result;

	/* Avoid spinloop trying to handle interrupts on frozen device */
	if (pci_channel_offline(sym_data->pdev))
		return IRQ_NONE;

	if (DEBUG_FLAGS & DEBUG_TINY) printf_debug ("[");

	spin_lock(shost->host_lock);
	result = sym_interrupt(shost);
	spin_unlock(shost->host_lock);

	if (DEBUG_FLAGS & DEBUG_TINY) printf_debug ("]\n");

	return result;
}

/*
 *  Linux entry point of the timer handler
 */
static void sym53c8xx_timer(unsigned long npref)
{
	struct sym_hcb *np = (struct sym_hcb *)npref;
	unsigned long flags;

	spin_lock_irqsave(np->s.host->host_lock, flags);
	sym_timer(np);
	spin_unlock_irqrestore(np->s.host->host_lock, flags);
}


/*
 *  What the eh thread wants us to perform.
 */
#define SYM_EH_ABORT		0
#define SYM_EH_DEVICE_RESET	1
#define SYM_EH_BUS_RESET	2
#define SYM_EH_HOST_RESET	3

/*
 *  Generic method for our eh processing.
 *  The 'op' argument tells what we have to do.
 */
static int sym_eh_handler(int op, char *opname, struct scsi_cmnd *cmd)
{
	struct sym_ucmd *ucmd = SYM_UCMD_PTR(cmd);
	struct Scsi_Host *shost = cmd->device->host;
	struct sym_data *sym_data = shost_priv(shost);
	struct pci_dev *pdev = sym_data->pdev;
	struct sym_hcb *np = sym_data->ncb;
	SYM_QUEHEAD *qp;
	int cmd_queued = 0;
	int sts = -1;
	struct completion eh_done;

	scmd_printk(KERN_WARNING, cmd, "%s operation started\n", opname);

	/* We may be in an error condition because the PCI bus
	 * went down. In this case, we need to wait until the
	 * PCI bus is reset, the card is reset, and only then
	 * proceed with the scsi error recovery.  There's no
	 * point in hurrying; take a leisurely wait.
	 */
#define WAIT_FOR_PCI_RECOVERY	35
	if (pci_channel_offline(pdev)) {
		int finished_reset = 0;
		init_completion(&eh_done);
		spin_lock_irq(shost->host_lock);
		/* Make sure we didn't race */
		if (pci_channel_offline(pdev)) {
			BUG_ON(sym_data->io_reset);
			sym_data->io_reset = &eh_done;
		} else {
			finished_reset = 1;
		}
		spin_unlock_irq(shost->host_lock);
		if (!finished_reset)
			finished_reset = wait_for_completion_timeout
						(sym_data->io_reset,
						WAIT_FOR_PCI_RECOVERY*HZ);
		spin_lock_irq(shost->host_lock);
		sym_data->io_reset = NULL;
		spin_unlock_irq(shost->host_lock);
		if (!finished_reset)
			return SCSI_FAILED;
	}

	spin_lock_irq(shost->host_lock);
	/* This one is queued in some place -> to wait for completion */
	FOR_EACH_QUEUED_ELEMENT(&np->busy_ccbq, qp) {
		struct sym_ccb *cp = sym_que_entry(qp, struct sym_ccb, link_ccbq);
		if (cp->cmd == cmd) {
			cmd_queued = 1;
			break;
		}
	}

	/* Try to proceed the operation we have been asked for */
	sts = -1;
	switch(op) {
	case SYM_EH_ABORT:
		sts = sym_abort_scsiio(np, cmd, 1);
		break;
	case SYM_EH_DEVICE_RESET:
		sts = sym_reset_scsi_target(np, cmd->device->id);
		break;
	case SYM_EH_BUS_RESET:
		sym_reset_scsi_bus(np, 1);
		sts = 0;
		break;
	case SYM_EH_HOST_RESET:
		sym_reset_scsi_bus(np, 0);
		sym_start_up(shost, 1);
		sts = 0;
		break;
	default:
		break;
	}

	/* On error, restore everything and cross fingers :) */
	if (sts)
		cmd_queued = 0;

	if (cmd_queued) {
		init_completion(&eh_done);
		ucmd->eh_done = &eh_done;
		spin_unlock_irq(shost->host_lock);
		if (!wait_for_completion_timeout(&eh_done, 5*HZ)) {
			ucmd->eh_done = NULL;
			sts = -2;
		}
	} else {
		spin_unlock_irq(shost->host_lock);
	}

	dev_warn(&cmd->device->sdev_gendev, "%s operation %s.\n", opname,
			sts==0 ? "complete" :sts==-2 ? "timed-out" : "failed");
	return sts ? SCSI_FAILED : SCSI_SUCCESS;
}


/*
 * Error handlers called from the eh thread (one thread per HBA).
 */
static int sym53c8xx_eh_abort_handler(struct scsi_cmnd *cmd)
{
	return sym_eh_handler(SYM_EH_ABORT, "ABORT", cmd);
}

static int sym53c8xx_eh_device_reset_handler(struct scsi_cmnd *cmd)
{
	return sym_eh_handler(SYM_EH_DEVICE_RESET, "DEVICE RESET", cmd);
}

static int sym53c8xx_eh_bus_reset_handler(struct scsi_cmnd *cmd)
{
	return sym_eh_handler(SYM_EH_BUS_RESET, "BUS RESET", cmd);
}

static int sym53c8xx_eh_host_reset_handler(struct scsi_cmnd *cmd)
{
	return sym_eh_handler(SYM_EH_HOST_RESET, "HOST RESET", cmd);
}

/*
 *  Tune device queuing depth, according to various limits.
 */
static void sym_tune_dev_queuing(struct sym_tcb *tp, int lun, u_short reqtags)
{
	struct sym_lcb *lp = sym_lp(tp, lun);
	u_short	oldtags;

	if (!lp)
		return;

	oldtags = lp->s.reqtags;

	if (reqtags > lp->s.scdev_depth)
		reqtags = lp->s.scdev_depth;

	lp->s.reqtags     = reqtags;

	if (reqtags != oldtags) {
		dev_info(&tp->starget->dev,
		         "tagged command queuing %s, command queue depth %d.\n",
		          lp->s.reqtags ? "enabled" : "disabled", reqtags);
	}
}

static int sym53c8xx_slave_alloc(struct scsi_device *sdev)
{
	struct sym_hcb *np = sym_get_hcb(sdev->host);
	struct sym_tcb *tp = &np->target[sdev->id];
	struct sym_lcb *lp;
	unsigned long flags;
	int error;

	if (sdev->id >= SYM_CONF_MAX_TARGET || sdev->lun >= SYM_CONF_MAX_LUN)
		return -ENXIO;

	spin_lock_irqsave(np->s.host->host_lock, flags);

	/*
	 * Fail the device init if the device is flagged NOSCAN at BOOT in
	 * the NVRAM.  This may speed up boot and maintain coherency with
	 * BIOS device numbering.  Clearing the flag allows the user to
	 * rescan skipped devices later.  We also return an error for
	 * devices not flagged for SCAN LUNS in the NVRAM since some single
	 * lun devices behave badly when asked for a non zero LUN.
	 */

	if (tp->usrflags & SYM_SCAN_BOOT_DISABLED) {
		tp->usrflags &= ~SYM_SCAN_BOOT_DISABLED;
		starget_printk(KERN_INFO, sdev->sdev_target,
				"Scan at boot disabled in NVRAM\n");
		error = -ENXIO;
		goto out;
	}

	if (tp->usrflags & SYM_SCAN_LUNS_DISABLED) {
		if (sdev->lun != 0) {
			error = -ENXIO;
			goto out;
		}
		starget_printk(KERN_INFO, sdev->sdev_target,
				"Multiple LUNs disabled in NVRAM\n");
	}

	lp = sym_alloc_lcb(np, sdev->id, sdev->lun);
	if (!lp) {
		error = -ENOMEM;
		goto out;
	}
	if (tp->nlcb == 1)
		tp->starget = sdev->sdev_target;

	spi_min_period(tp->starget) = tp->usr_period;
	spi_max_width(tp->starget) = tp->usr_width;

	error = 0;
out:
	spin_unlock_irqrestore(np->s.host->host_lock, flags);

	return error;
}

/*
 * Linux entry point for device queue sizing.
 */
static int sym53c8xx_slave_configure(struct scsi_device *sdev)
{
	struct sym_hcb *np = sym_get_hcb(sdev->host);
	struct sym_tcb *tp = &np->target[sdev->id];
	struct sym_lcb *lp = sym_lp(tp, sdev->lun);
	int reqtags, depth_to_use;

	/*
	 *  Get user flags.
	 */
	lp->curr_flags = lp->user_flags;

	/*
	 *  Select queue depth from driver setup.
	 *  Do not use more than configured by user.
	 *  Use at least 1.
	 *  Do not use more than our maximum.
	 */
	reqtags = sym_driver_setup.max_tag;
	if (reqtags > tp->usrtags)
		reqtags = tp->usrtags;
	if (!sdev->tagged_supported)
		reqtags = 0;
	if (reqtags > SYM_CONF_MAX_TAG)
		reqtags = SYM_CONF_MAX_TAG;
	depth_to_use = reqtags ? reqtags : 1;
	scsi_change_queue_depth(sdev, depth_to_use);
	lp->s.scdev_depth = depth_to_use;
	sym_tune_dev_queuing(tp, sdev->lun, reqtags);

	if (!spi_initial_dv(sdev->sdev_target))
		spi_dv_device(sdev);

	return 0;
}

static void sym53c8xx_slave_destroy(struct scsi_device *sdev)
{
	struct sym_hcb *np = sym_get_hcb(sdev->host);
	struct sym_tcb *tp = &np->target[sdev->id];
	struct sym_lcb *lp = sym_lp(tp, sdev->lun);
	unsigned long flags;

	/* if slave_alloc returned before allocating a sym_lcb, return */
	if (!lp)
		return;

	spin_lock_irqsave(np->s.host->host_lock, flags);

	if (lp->busy_itlq || lp->busy_itl) {
		/*
		 * This really shouldn't happen, but we can't return an error
		 * so let's try to stop all on-going I/O.
		 */
		starget_printk(KERN_WARNING, tp->starget,
			       "Removing busy LCB (%d)\n", (u8)sdev->lun);
		sym_reset_scsi_bus(np, 1);
	}

	if (sym_free_lcb(np, sdev->id, sdev->lun) == 0) {
		/*
		 * It was the last unit for this target.
		 */
		tp->head.sval        = 0;
		tp->head.wval        = np->rv_scntl3;
		tp->head.uval        = 0;
		tp->tgoal.check_nego = 1;
		tp->starget	     = NULL;
	}

	spin_unlock_irqrestore(np->s.host->host_lock, flags);
}

/*
 *  Linux entry point for info() function
 */
static const char *sym53c8xx_info (struct Scsi_Host *host)
{
	return SYM_DRIVER_NAME;
}


#ifdef SYM_LINUX_PROC_INFO_SUPPORT
/*
 *  Proc file system stuff
 *
 *  A read operation returns adapter information.
 *  A write operation is a control command.
 *  The string is parsed in the driver code and the command is passed 
 *  to the sym_usercmd() function.
 */

#ifdef SYM_LINUX_USER_COMMAND_SUPPORT

struct	sym_usrcmd {
	u_long	target;
	u_long	lun;
	u_long	data;
	u_long	cmd;
};

#define UC_SETSYNC      10
#define UC_SETTAGS	11
#define UC_SETDEBUG	12
#define UC_SETWIDE	14
#define UC_SETFLAG	15
#define UC_SETVERBOSE	17
#define UC_RESETDEV	18
#define UC_CLEARDEV	19

static void sym_exec_user_command (struct sym_hcb *np, struct sym_usrcmd *uc)
{
	struct sym_tcb *tp;
	int t, l;

	switch (uc->cmd) {
	case 0: return;

#ifdef SYM_LINUX_DEBUG_CONTROL_SUPPORT
	case UC_SETDEBUG:
		sym_debug_flags = uc->data;
		break;
#endif
	case UC_SETVERBOSE:
		np->verbose = uc->data;
		break;
	default:
		/*
		 * We assume that other commands apply to targets.
		 * This should always be the case and avoid the below 
		 * 4 lines to be repeated 6 times.
		 */
		for (t = 0; t < SYM_CONF_MAX_TARGET; t++) {
			if (!((uc->target >> t) & 1))
				continue;
			tp = &np->target[t];
			if (!tp->nlcb)
				continue;

			switch (uc->cmd) {

			case UC_SETSYNC:
				if (!uc->data || uc->data >= 255) {
					tp->tgoal.iu = tp->tgoal.dt =
						tp->tgoal.qas = 0;
					tp->tgoal.offset = 0;
				} else if (uc->data <= 9 && np->minsync_dt) {
					if (uc->data < np->minsync_dt)
						uc->data = np->minsync_dt;
					tp->tgoal.iu = tp->tgoal.dt =
						tp->tgoal.qas = 1;
					tp->tgoal.width = 1;
					tp->tgoal.period = uc->data;
					tp->tgoal.offset = np->maxoffs_dt;
				} else {
					if (uc->data < np->minsync)
						uc->data = np->minsync;
					tp->tgoal.iu = tp->tgoal.dt =
						tp->tgoal.qas = 0;
					tp->tgoal.period = uc->data;
					tp->tgoal.offset = np->maxoffs;
				}
				tp->tgoal.check_nego = 1;
				break;
			case UC_SETWIDE:
				tp->tgoal.width = uc->data ? 1 : 0;
				tp->tgoal.check_nego = 1;
				break;
			case UC_SETTAGS:
				for (l = 0; l < SYM_CONF_MAX_LUN; l++)
					sym_tune_dev_queuing(tp, l, uc->data);
				break;
			case UC_RESETDEV:
				tp->to_reset = 1;
				np->istat_sem = SEM;
				OUTB(np, nc_istat, SIGP|SEM);
				break;
			case UC_CLEARDEV:
				for (l = 0; l < SYM_CONF_MAX_LUN; l++) {
					struct sym_lcb *lp = sym_lp(tp, l);
					if (lp) lp->to_clear = 1;
				}
				np->istat_sem = SEM;
				OUTB(np, nc_istat, SIGP|SEM);
				break;
			case UC_SETFLAG:
				tp->usrflags = uc->data;
				break;
			}
		}
		break;
	}
}

static int sym_skip_spaces(char *ptr, int len)
{
	int cnt, c;

	for (cnt = len; cnt > 0 && (c = *ptr++) && isspace(c); cnt--);

	return (len - cnt);
}

static int get_int_arg(char *ptr, int len, u_long *pv)
{
	char *end;

	*pv = simple_strtoul(ptr, &end, 10);
	return (end - ptr);
}

static int is_keyword(char *ptr, int len, char *verb)
{
	int verb_len = strlen(verb);

	if (len >= verb_len && !memcmp(verb, ptr, verb_len))
		return verb_len;
	else
		return 0;
}

#define SKIP_SPACES(ptr, len)						\
	if ((arg_len = sym_skip_spaces(ptr, len)) < 1)			\
		return -EINVAL;						\
	ptr += arg_len; len -= arg_len;

#define GET_INT_ARG(ptr, len, v)					\
	if (!(arg_len = get_int_arg(ptr, len, &(v))))			\
		return -EINVAL;						\
	ptr += arg_len; len -= arg_len;


/*
 * Parse a control command
 */

static int sym_user_command(struct Scsi_Host *shost, char *buffer, int length)
{
	struct sym_hcb *np = sym_get_hcb(shost);
	char *ptr	= buffer;
	int len		= length;
	struct sym_usrcmd cmd, *uc = &cmd;
	int		arg_len;
	u_long 		target;

	memset(uc, 0, sizeof(*uc));

	if (len > 0 && ptr[len-1] == '\n')
		--len;

	if	((arg_len = is_keyword(ptr, len, "setsync")) != 0)
		uc->cmd = UC_SETSYNC;
	else if	((arg_len = is_keyword(ptr, len, "settags")) != 0)
		uc->cmd = UC_SETTAGS;
	else if	((arg_len = is_keyword(ptr, len, "setverbose")) != 0)
		uc->cmd = UC_SETVERBOSE;
	else if	((arg_len = is_keyword(ptr, len, "setwide")) != 0)
		uc->cmd = UC_SETWIDE;
#ifdef SYM_LINUX_DEBUG_CONTROL_SUPPORT
	else if	((arg_len = is_keyword(ptr, len, "setdebug")) != 0)
		uc->cmd = UC_SETDEBUG;
#endif
	else if	((arg_len = is_keyword(ptr, len, "setflag")) != 0)
		uc->cmd = UC_SETFLAG;
	else if	((arg_len = is_keyword(ptr, len, "resetdev")) != 0)
		uc->cmd = UC_RESETDEV;
	else if	((arg_len = is_keyword(ptr, len, "cleardev")) != 0)
		uc->cmd = UC_CLEARDEV;
	else
		arg_len = 0;

#ifdef DEBUG_PROC_INFO
printk("sym_user_command: arg_len=%d, cmd=%ld\n", arg_len, uc->cmd);
#endif

	if (!arg_len)
		return -EINVAL;
	ptr += arg_len; len -= arg_len;

	switch(uc->cmd) {
	case UC_SETSYNC:
	case UC_SETTAGS:
	case UC_SETWIDE:
	case UC_SETFLAG:
	case UC_RESETDEV:
	case UC_CLEARDEV:
		SKIP_SPACES(ptr, len);
		if ((arg_len = is_keyword(ptr, len, "all")) != 0) {
			ptr += arg_len; len -= arg_len;
			uc->target = ~0;
		} else {
			GET_INT_ARG(ptr, len, target);
			uc->target = (1<<target);
#ifdef DEBUG_PROC_INFO
printk("sym_user_command: target=%ld\n", target);
#endif
		}
		break;
	}

	switch(uc->cmd) {
	case UC_SETVERBOSE:
	case UC_SETSYNC:
	case UC_SETTAGS:
	case UC_SETWIDE:
		SKIP_SPACES(ptr, len);
		GET_INT_ARG(ptr, len, uc->data);
#ifdef DEBUG_PROC_INFO
printk("sym_user_command: data=%ld\n", uc->data);
#endif
		break;
#ifdef SYM_LINUX_DEBUG_CONTROL_SUPPORT
	case UC_SETDEBUG:
		while (len > 0) {
			SKIP_SPACES(ptr, len);
			if	((arg_len = is_keyword(ptr, len, "alloc")))
				uc->data |= DEBUG_ALLOC;
			else if	((arg_len = is_keyword(ptr, len, "phase")))
				uc->data |= DEBUG_PHASE;
			else if	((arg_len = is_keyword(ptr, len, "queue")))
				uc->data |= DEBUG_QUEUE;
			else if	((arg_len = is_keyword(ptr, len, "result")))
				uc->data |= DEBUG_RESULT;
			else if	((arg_len = is_keyword(ptr, len, "scatter")))
				uc->data |= DEBUG_SCATTER;
			else if	((arg_len = is_keyword(ptr, len, "script")))
				uc->data |= DEBUG_SCRIPT;
			else if	((arg_len = is_keyword(ptr, len, "tiny")))
				uc->data |= DEBUG_TINY;
			else if	((arg_len = is_keyword(ptr, len, "timing")))
				uc->data |= DEBUG_TIMING;
			else if	((arg_len = is_keyword(ptr, len, "nego")))
				uc->data |= DEBUG_NEGO;
			else if	((arg_len = is_keyword(ptr, len, "tags")))
				uc->data |= DEBUG_TAGS;
			else if	((arg_len = is_keyword(ptr, len, "pointer")))
				uc->data |= DEBUG_POINTER;
			else
				return -EINVAL;
			ptr += arg_len; len -= arg_len;
		}
#ifdef DEBUG_PROC_INFO
printk("sym_user_command: data=%ld\n", uc->data);
#endif
		break;
#endif /* SYM_LINUX_DEBUG_CONTROL_SUPPORT */
	case UC_SETFLAG:
		while (len > 0) {
			SKIP_SPACES(ptr, len);
			if	((arg_len = is_keyword(ptr, len, "no_disc")))
				uc->data &= ~SYM_DISC_ENABLED;
			else
				return -EINVAL;
			ptr += arg_len; len -= arg_len;
		}
		break;
	default:
		break;
	}

	if (len)
		return -EINVAL;
	else {
		unsigned long flags;

		spin_lock_irqsave(shost->host_lock, flags);
		sym_exec_user_command(np, uc);
		spin_unlock_irqrestore(shost->host_lock, flags);
	}
	return length;
}

#endif	/* SYM_LINUX_USER_COMMAND_SUPPORT */


/*
 *  Copy formatted information into the input buffer.
 */
static int sym_show_info(struct seq_file *m, struct Scsi_Host *shost)
{
#ifdef SYM_LINUX_USER_INFO_SUPPORT
	struct sym_data *sym_data = shost_priv(shost);
	struct pci_dev *pdev = sym_data->pdev;
	struct sym_hcb *np = sym_data->ncb;

	seq_printf(m, "Chip " NAME53C "%s, device id 0x%x, "
		 "revision id 0x%x\n", np->s.chip_name,
		 pdev->device, pdev->revision);
	seq_printf(m, "At PCI address %s, IRQ %u\n",
			 pci_name(pdev), pdev->irq);
	seq_printf(m, "Min. period factor %d, %s SCSI BUS%s\n",
		 (int) (np->minsync_dt ? np->minsync_dt : np->minsync),
		 np->maxwide ? "Wide" : "Narrow",
		 np->minsync_dt ? ", DT capable" : "");

	seq_printf(m, "Max. started commands %d, "
		 "max. commands per LUN %d\n",
		 SYM_CONF_MAX_START, SYM_CONF_MAX_TAG);

	return 0;
#else
	return -EINVAL;
#endif /* SYM_LINUX_USER_INFO_SUPPORT */
}

#endif /* SYM_LINUX_PROC_INFO_SUPPORT */

/*
 * Free resources claimed by sym_iomap_device().  Note that
 * sym_free_resources() should be used instead of this function after calling
 * sym_attach().
 */
static void sym_iounmap_device(struct sym_device *device)
{
	if (device->s.ioaddr)
		pci_iounmap(device->pdev, device->s.ioaddr);
	if (device->s.ramaddr)
		pci_iounmap(device->pdev, device->s.ramaddr);
}

/*
 *	Free controller resources.
 */
static void sym_free_resources(struct sym_hcb *np, struct pci_dev *pdev,
		int do_free_irq)
{
	/*
	 *  Free O/S specific resources.
	 */
	if (do_free_irq)
		free_irq(pdev->irq, np->s.host);
	if (np->s.ioaddr)
		pci_iounmap(pdev, np->s.ioaddr);
	if (np->s.ramaddr)
		pci_iounmap(pdev, np->s.ramaddr);
	/*
	 *  Free O/S independent resources.
	 */
	sym_hcb_free(np);

	sym_mfree_dma(np, sizeof(*np), "HCB");
}

/*
 *  Host attach and initialisations.
 *
 *  Allocate host data and ncb structure.
 *  Remap MMIO region.
 *  Do chip initialization.
 *  If all is OK, install interrupt handling and
 *  start the timer daemon.
 */
static struct Scsi_Host *sym_attach(struct scsi_host_template *tpnt, int unit,
				    struct sym_device *dev)
{
	struct sym_data *sym_data;
	struct sym_hcb *np = NULL;
	struct Scsi_Host *shost = NULL;
	struct pci_dev *pdev = dev->pdev;
	unsigned long flags;
	struct sym_fw *fw;
	int do_free_irq = 0;

	printk(KERN_INFO "sym%d: <%s> rev 0x%x at pci %s irq %u\n",
		unit, dev->chip.name, pdev->revision, pci_name(pdev),
		pdev->irq);

	/*
	 *  Get the firmware for this chip.
	 */
	fw = sym_find_firmware(&dev->chip);
	if (!fw)
		goto attach_failed;

	shost = scsi_host_alloc(tpnt, sizeof(*sym_data));
	if (!shost)
		goto attach_failed;
	sym_data = shost_priv(shost);

	/*
	 *  Allocate immediately the host control block, 
	 *  since we are only expecting to succeed. :)
	 *  We keep track in the HCB of all the resources that 
	 *  are to be released on error.
	 */
	np = __sym_calloc_dma(&pdev->dev, sizeof(*np), "HCB");
	if (!np)
		goto attach_failed;
	np->bus_dmat = &pdev->dev; /* Result in 1 DMA pool per HBA */
	sym_data->ncb = np;
	sym_data->pdev = pdev;
	np->s.host = shost;

	pci_set_drvdata(pdev, shost);

	/*
	 *  Copy some useful infos to the HCB.
	 */
	np->hcb_ba	= vtobus(np);
	np->verbose	= sym_driver_setup.verbose;
	np->s.unit	= unit;
	np->features	= dev->chip.features;
	np->clock_divn	= dev->chip.nr_divisor;
	np->maxoffs	= dev->chip.offset_max;
	np->maxburst	= dev->chip.burst_max;
	np->myaddr	= dev->host_id;
	np->mmio_ba	= (u32)dev->mmio_base;
	np->ram_ba	= (u32)dev->ram_base;
	np->s.ioaddr	= dev->s.ioaddr;
	np->s.ramaddr	= dev->s.ramaddr;

	/*
	 *  Edit its name.
	 */
	strlcpy(np->s.chip_name, dev->chip.name, sizeof(np->s.chip_name));
	sprintf(np->s.inst_name, "sym%d", np->s.unit);

	if ((SYM_CONF_DMA_ADDRESSING_MODE > 0) && (np->features & FE_DAC) &&
			!pci_set_dma_mask(pdev, DMA_DAC_MASK)) {
		set_dac(np);
	} else if (pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		printf_warning("%s: No suitable DMA available\n", sym_name(np));
		goto attach_failed;
	}

	if (sym_hcb_attach(shost, fw, dev->nvram))
		goto attach_failed;

	/*
	 *  Install the interrupt handler.
	 *  If we synchonize the C code with SCRIPTS on interrupt, 
	 *  we do not want to share the INTR line at all.
	 */
	if (request_irq(pdev->irq, sym53c8xx_intr, IRQF_SHARED, NAME53C8XX,
			shost)) {
		printf_err("%s: request irq %u failure\n",
			sym_name(np), pdev->irq);
		goto attach_failed;
	}
	do_free_irq = 1;

	/*
	 *  After SCSI devices have been opened, we cannot
	 *  reset the bus safely, so we do it here.
	 */
	spin_lock_irqsave(shost->host_lock, flags);
	if (sym_reset_scsi_bus(np, 0))
		goto reset_failed;

	/*
	 *  Start the SCRIPTS.
	 */
	sym_start_up(shost, 1);

	/*
	 *  Start the timer daemon
	 */
	init_timer(&np->s.timer);
	np->s.timer.data     = (unsigned long) np;
	np->s.timer.function = sym53c8xx_timer;
	np->s.lasttime=0;
	sym_timer (np);

	/*
	 *  Fill Linux host instance structure
	 *  and return success.
	 */
	shost->max_channel	= 0;
	shost->this_id		= np->myaddr;
	shost->max_id		= np->maxwide ? 16 : 8;
	shost->max_lun		= SYM_CONF_MAX_LUN;
	shost->unique_id	= pci_resource_start(pdev, 0);
	shost->cmd_per_lun	= SYM_CONF_MAX_TAG;
	shost->can_queue	= (SYM_CONF_MAX_START-2);
	shost->sg_tablesize	= SYM_CONF_MAX_SG;
	shost->max_cmd_len	= 16;
	BUG_ON(sym2_transport_template == NULL);
	shost->transportt	= sym2_transport_template;

	/* 53c896 rev 1 errata: DMA may not cross 16MB boundary */
	if (pdev->device == PCI_DEVICE_ID_NCR_53C896 && pdev->revision < 2)
		shost->dma_boundary = 0xFFFFFF;

	spin_unlock_irqrestore(shost->host_lock, flags);

	return shost;

 reset_failed:
	printf_err("%s: FATAL ERROR: CHECK SCSI BUS - CABLES, "
		   "TERMINATION, DEVICE POWER etc.!\n", sym_name(np));
	spin_unlock_irqrestore(shost->host_lock, flags);
 attach_failed:
	printf_info("sym%d: giving up ...\n", unit);
	if (np)
		sym_free_resources(np, pdev, do_free_irq);
	else
		sym_iounmap_device(dev);
	if (shost)
		scsi_host_put(shost);

	return NULL;
 }


/*
 *    Detect and try to read SYMBIOS and TEKRAM NVRAM.
 */
#if SYM_CONF_NVRAM_SUPPORT
static void sym_get_nvram(struct sym_device *devp, struct sym_nvram *nvp)
{
	devp->nvram = nvp;
	nvp->type = 0;

	sym_read_nvram(devp, nvp);
}
#else
static inline void sym_get_nvram(struct sym_device *devp, struct sym_nvram *nvp)
{
}
#endif	/* SYM_CONF_NVRAM_SUPPORT */

static int sym_check_supported(struct sym_device *device)
{
	struct sym_chip *chip;
	struct pci_dev *pdev = device->pdev;
	unsigned long io_port = pci_resource_start(pdev, 0);
	int i;

	/*
	 *  If user excluded this chip, do not initialize it.
	 *  I hate this code so much.  Must kill it.
	 */
	if (io_port) {
		for (i = 0 ; i < 8 ; i++) {
			if (sym_driver_setup.excludes[i] == io_port)
				return -ENODEV;
		}
	}

	/*
	 * Check if the chip is supported.  Then copy the chip description
	 * to our device structure so we can make it match the actual device
	 * and options.
	 */
	chip = sym_lookup_chip_table(pdev->device, pdev->revision);
	if (!chip) {
		dev_info(&pdev->dev, "device not supported\n");
		return -ENODEV;
	}
	memcpy(&device->chip, chip, sizeof(device->chip));

	return 0;
}

/*
 * Ignore Symbios chips controlled by various RAID controllers.
 * These controllers set value 0x52414944 at RAM end - 16.
 */
static int sym_check_raid(struct sym_device *device)
{
	unsigned int ram_size, ram_val;

	if (!device->s.ramaddr)
		return 0;

	if (device->chip.features & FE_RAM8K)
		ram_size = 8192;
	else
		ram_size = 4096;

	ram_val = readl(device->s.ramaddr + ram_size - 16);
	if (ram_val != 0x52414944)
		return 0;

	dev_info(&device->pdev->dev,
			"not initializing, driven by RAID controller.\n");
	return -ENODEV;
}

static int sym_set_workarounds(struct sym_device *device)
{
	struct sym_chip *chip = &device->chip;
	struct pci_dev *pdev = device->pdev;
	u_short status_reg;

	/*
	 *  (ITEM 12 of a DEL about the 896 I haven't yet).
	 *  We must ensure the chip will use WRITE AND INVALIDATE.
	 *  The revision number limit is for now arbitrary.
	 */
	if (pdev->device == PCI_DEVICE_ID_NCR_53C896 && pdev->revision < 0x4) {
		chip->features	|= (FE_WRIE | FE_CLSE);
	}

	/* If the chip can do Memory Write Invalidate, enable it */
	if (chip->features & FE_WRIE) {
		if (pci_set_mwi(pdev))
			return -ENODEV;
	}

	/*
	 *  Work around for errant bit in 895A. The 66Mhz
	 *  capable bit is set erroneously. Clear this bit.
	 *  (Item 1 DEL 533)
	 *
	 *  Make sure Config space and Features agree.
	 *
	 *  Recall: writes are not normal to status register -
	 *  write a 1 to clear and a 0 to leave unchanged.
	 *  Can only reset bits.
	 */
	pci_read_config_word(pdev, PCI_STATUS, &status_reg);
	if (chip->features & FE_66MHZ) {
		if (!(status_reg & PCI_STATUS_66MHZ))
			chip->features &= ~FE_66MHZ;
	} else {
		if (status_reg & PCI_STATUS_66MHZ) {
			status_reg = PCI_STATUS_66MHZ;
			pci_write_config_word(pdev, PCI_STATUS, status_reg);
			pci_read_config_word(pdev, PCI_STATUS, &status_reg);
		}
	}

	return 0;
}

/*
 * Map HBA registers and on-chip SRAM (if present).
 */
static int sym_iomap_device(struct sym_device *device)
{
	struct pci_dev *pdev = device->pdev;
	struct pci_bus_region bus_addr;
	int i = 2;

	pcibios_resource_to_bus(pdev->bus, &bus_addr, &pdev->resource[1]);
	device->mmio_base = bus_addr.start;

	if (device->chip.features & FE_RAM) {
		/*
		 * If the BAR is 64-bit, resource 2 will be occupied by the
		 * upper 32 bits
		 */
		if (!pdev->resource[i].flags)
			i++;
		pcibios_resource_to_bus(pdev->bus, &bus_addr,
					&pdev->resource[i]);
		device->ram_base = bus_addr.start;
	}

#ifdef CONFIG_SCSI_SYM53C8XX_MMIO
	if (device->mmio_base)
		device->s.ioaddr = pci_iomap(pdev, 1,
						pci_resource_len(pdev, 1));
#endif
	if (!device->s.ioaddr)
		device->s.ioaddr = pci_iomap(pdev, 0,
						pci_resource_len(pdev, 0));
	if (!device->s.ioaddr) {
		dev_err(&pdev->dev, "could not map registers; giving up.\n");
		return -EIO;
	}
	if (device->ram_base) {
		device->s.ramaddr = pci_iomap(pdev, i,
						pci_resource_len(pdev, i));
		if (!device->s.ramaddr) {
			dev_warn(&pdev->dev,
				"could not map SRAM; continuing anyway.\n");
			device->ram_base = 0;
		}
	}

	return 0;
}

/*
 * The NCR PQS and PDS cards are constructed as a DEC bridge
 * behind which sits a proprietary NCR memory controller and
 * either four or two 53c875s as separate devices.  We can tell
 * if an 875 is part of a PQS/PDS or not since if it is, it will
 * be on the same bus as the memory controller.  In its usual
 * mode of operation, the 875s are slaved to the memory
 * controller for all transfers.  To operate with the Linux
 * driver, the memory controller is disabled and the 875s
 * freed to function independently.  The only wrinkle is that
 * the preset SCSI ID (which may be zero) must be read in from
 * a special configuration space register of the 875.
 */
static void sym_config_pqs(struct pci_dev *pdev, struct sym_device *sym_dev)
{
	int slot;
	u8 tmp;

	for (slot = 0; slot < 256; slot++) {
		struct pci_dev *memc = pci_get_slot(pdev->bus, slot);

		if (!memc || memc->vendor != 0x101a || memc->device == 0x0009) {
			pci_dev_put(memc);
			continue;
		}

		/* bit 1: allow individual 875 configuration */
		pci_read_config_byte(memc, 0x44, &tmp);
		if ((tmp & 0x2) == 0) {
			tmp |= 0x2;
			pci_write_config_byte(memc, 0x44, tmp);
		}

		/* bit 2: drive individual 875 interrupts to the bus */
		pci_read_config_byte(memc, 0x45, &tmp);
		if ((tmp & 0x4) == 0) {
			tmp |= 0x4;
			pci_write_config_byte(memc, 0x45, tmp);
		}

		pci_dev_put(memc);
		break;
	}

	pci_read_config_byte(pdev, 0x84, &tmp);
	sym_dev->host_id = tmp;
}

/*
 *  Called before unloading the module.
 *  Detach the host.
 *  We have to free resources and halt the NCR chip.
 */
static int sym_detach(struct Scsi_Host *shost, struct pci_dev *pdev)
{
	struct sym_hcb *np = sym_get_hcb(shost);
	printk("%s: detaching ...\n", sym_name(np));

	del_timer_sync(&np->s.timer);

	/*
	 * Reset NCR chip.
	 * We should use sym_soft_reset(), but we don't want to do 
	 * so, since we may not be safe if interrupts occur.
	 */
	printk("%s: resetting chip\n", sym_name(np));
	OUTB(np, nc_istat, SRST);
	INB(np, nc_mbox1);
	udelay(10);
	OUTB(np, nc_istat, 0);

	sym_free_resources(np, pdev, 1);
	scsi_host_put(shost);

	return 1;
}

/*
 * Driver host template.
 */
static struct scsi_host_template sym2_template = {
	.module			= THIS_MODULE,
	.name			= "sym53c8xx",
	.info			= sym53c8xx_info, 
	.queuecommand		= sym53c8xx_queue_command,
	.slave_alloc		= sym53c8xx_slave_alloc,
	.slave_configure	= sym53c8xx_slave_configure,
	.slave_destroy		= sym53c8xx_slave_destroy,
	.eh_abort_handler	= sym53c8xx_eh_abort_handler,
	.eh_device_reset_handler = sym53c8xx_eh_device_reset_handler,
	.eh_bus_reset_handler	= sym53c8xx_eh_bus_reset_handler,
	.eh_host_reset_handler	= sym53c8xx_eh_host_reset_handler,
	.this_id		= 7,
	.use_clustering		= ENABLE_CLUSTERING,
	.max_sectors		= 0xFFFF,
#ifdef SYM_LINUX_PROC_INFO_SUPPORT
	.show_info		= sym_show_info,
#ifdef	SYM_LINUX_USER_COMMAND_SUPPORT
	.write_info		= sym_user_command,
#endif
	.proc_name		= NAME53C8XX,
#endif
};

static int attach_count;

static int sym2_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct sym_device sym_dev;
	struct sym_nvram nvram;
	struct Scsi_Host *shost;
	int do_iounmap = 0;
	int do_disable_device = 1;

	memset(&sym_dev, 0, sizeof(sym_dev));
	memset(&nvram, 0, sizeof(nvram));
	sym_dev.pdev = pdev;
	sym_dev.host_id = SYM_SETUP_HOST_ID;

	if (pci_enable_device(pdev))
		goto leave;

	pci_set_master(pdev);

	if (pci_request_regions(pdev, NAME53C8XX))
		goto disable;

	if (sym_check_supported(&sym_dev))
		goto free;

	if (sym_iomap_device(&sym_dev))
		goto free;
	do_iounmap = 1;

	if (sym_check_raid(&sym_dev)) {
		do_disable_device = 0;	/* Don't disable the device */
		goto free;
	}

	if (sym_set_workarounds(&sym_dev))
		goto free;

	sym_config_pqs(pdev, &sym_dev);

	sym_get_nvram(&sym_dev, &nvram);

	do_iounmap = 0; /* Don't sym_iounmap_device() after sym_attach(). */
	shost = sym_attach(&sym2_template, attach_count, &sym_dev);
	if (!shost)
		goto free;

	if (scsi_add_host(shost, &pdev->dev))
		goto detach;
	scsi_scan_host(shost);

	attach_count++;

	return 0;

 detach:
	sym_detach(pci_get_drvdata(pdev), pdev);
 free:
	if (do_iounmap)
		sym_iounmap_device(&sym_dev);
	pci_release_regions(pdev);
 disable:
	if (do_disable_device)
		pci_disable_device(pdev);
 leave:
	return -ENODEV;
}

static void sym2_remove(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);

	scsi_remove_host(shost);
	sym_detach(shost, pdev);
	pci_release_regions(pdev);
	pci_disable_device(pdev);

	attach_count--;
}

/**
 * sym2_io_error_detected() - called when PCI error is detected
 * @pdev: pointer to PCI device
 * @state: current state of the PCI slot
 */
static pci_ers_result_t sym2_io_error_detected(struct pci_dev *pdev,
                                         enum pci_channel_state state)
{
	/* If slot is permanently frozen, turn everything off */
	if (state == pci_channel_io_perm_failure) {
		sym2_remove(pdev);
		return PCI_ERS_RESULT_DISCONNECT;
	}

	disable_irq(pdev->irq);
	pci_disable_device(pdev);

	/* Request that MMIO be enabled, so register dump can be taken. */
	return PCI_ERS_RESULT_CAN_RECOVER;
}

/**
 * sym2_io_slot_dump - Enable MMIO and dump debug registers
 * @pdev: pointer to PCI device
 */
static pci_ers_result_t sym2_io_slot_dump(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);

	sym_dump_registers(shost);

	/* Request a slot reset. */
	return PCI_ERS_RESULT_NEED_RESET;
}

/**
 * sym2_reset_workarounds - hardware-specific work-arounds
 *
 * This routine is similar to sym_set_workarounds(), except
 * that, at this point, we already know that the device was
 * successfully initialized at least once before, and so most
 * of the steps taken there are un-needed here.
 */
static void sym2_reset_workarounds(struct pci_dev *pdev)
{
	u_short status_reg;
	struct sym_chip *chip;

	chip = sym_lookup_chip_table(pdev->device, pdev->revision);

	/* Work around for errant bit in 895A, in a fashion
	 * similar to what is done in sym_set_workarounds().
	 */
	pci_read_config_word(pdev, PCI_STATUS, &status_reg);
	if (!(chip->features & FE_66MHZ) && (status_reg & PCI_STATUS_66MHZ)) {
		status_reg = PCI_STATUS_66MHZ;
		pci_write_config_word(pdev, PCI_STATUS, status_reg);
		pci_read_config_word(pdev, PCI_STATUS, &status_reg);
	}
}

/**
 * sym2_io_slot_reset() - called when the pci bus has been reset.
 * @pdev: pointer to PCI device
 *
 * Restart the card from scratch.
 */
static pci_ers_result_t sym2_io_slot_reset(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct sym_hcb *np = sym_get_hcb(shost);

	printk(KERN_INFO "%s: recovering from a PCI slot reset\n",
	          sym_name(np));

	if (pci_enable_device(pdev)) {
		printk(KERN_ERR "%s: Unable to enable after PCI reset\n",
		        sym_name(np));
		return PCI_ERS_RESULT_DISCONNECT;
	}

	pci_set_master(pdev);
	enable_irq(pdev->irq);

	/* If the chip can do Memory Write Invalidate, enable it */
	if (np->features & FE_WRIE) {
		if (pci_set_mwi(pdev))
			return PCI_ERS_RESULT_DISCONNECT;
	}

	/* Perform work-arounds, analogous to sym_set_workarounds() */
	sym2_reset_workarounds(pdev);

	/* Perform host reset only on one instance of the card */
	if (PCI_FUNC(pdev->devfn) == 0) {
		if (sym_reset_scsi_bus(np, 0)) {
			printk(KERN_ERR "%s: Unable to reset scsi host\n",
			        sym_name(np));
			return PCI_ERS_RESULT_DISCONNECT;
		}
		sym_start_up(shost, 1);
	}

	return PCI_ERS_RESULT_RECOVERED;
}

/**
 * sym2_io_resume() - resume normal ops after PCI reset
 * @pdev: pointer to PCI device
 *
 * Called when the error recovery driver tells us that its
 * OK to resume normal operation. Use completion to allow
 * halted scsi ops to resume.
 */
static void sym2_io_resume(struct pci_dev *pdev)
{
	struct Scsi_Host *shost = pci_get_drvdata(pdev);
	struct sym_data *sym_data = shost_priv(shost);

	spin_lock_irq(shost->host_lock);
	if (sym_data->io_reset)
		complete(sym_data->io_reset);
	spin_unlock_irq(shost->host_lock);
}

static void sym2_get_signalling(struct Scsi_Host *shost)
{
	struct sym_hcb *np = sym_get_hcb(shost);
	enum spi_signal_type type;

	switch (np->scsi_mode) {
	case SMODE_SE:
		type = SPI_SIGNAL_SE;
		break;
	case SMODE_LVD:
		type = SPI_SIGNAL_LVD;
		break;
	case SMODE_HVD:
		type = SPI_SIGNAL_HVD;
		break;
	default:
		type = SPI_SIGNAL_UNKNOWN;
		break;
	}
	spi_signalling(shost) = type;
}

static void sym2_set_offset(struct scsi_target *starget, int offset)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct sym_hcb *np = sym_get_hcb(shost);
	struct sym_tcb *tp = &np->target[starget->id];

	tp->tgoal.offset = offset;
	tp->tgoal.check_nego = 1;
}

static void sym2_set_period(struct scsi_target *starget, int period)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct sym_hcb *np = sym_get_hcb(shost);
	struct sym_tcb *tp = &np->target[starget->id];

	/* have to have DT for these transfers, but DT will also
	 * set width, so check that this is allowed */
	if (period <= np->minsync && spi_width(starget))
		tp->tgoal.dt = 1;

	tp->tgoal.period = period;
	tp->tgoal.check_nego = 1;
}

static void sym2_set_width(struct scsi_target *starget, int width)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct sym_hcb *np = sym_get_hcb(shost);
	struct sym_tcb *tp = &np->target[starget->id];

	/* It is illegal to have DT set on narrow transfers.  If DT is
	 * clear, we must also clear IU and QAS.  */
	if (width == 0)
		tp->tgoal.iu = tp->tgoal.dt = tp->tgoal.qas = 0;

	tp->tgoal.width = width;
	tp->tgoal.check_nego = 1;
}

static void sym2_set_dt(struct scsi_target *starget, int dt)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct sym_hcb *np = sym_get_hcb(shost);
	struct sym_tcb *tp = &np->target[starget->id];

	/* We must clear QAS and IU if DT is clear */
	if (dt)
		tp->tgoal.dt = 1;
	else
		tp->tgoal.iu = tp->tgoal.dt = tp->tgoal.qas = 0;
	tp->tgoal.check_nego = 1;
}

#if 0
static void sym2_set_iu(struct scsi_target *starget, int iu)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct sym_hcb *np = sym_get_hcb(shost);
	struct sym_tcb *tp = &np->target[starget->id];

	if (iu)
		tp->tgoal.iu = tp->tgoal.dt = 1;
	else
		tp->tgoal.iu = 0;
	tp->tgoal.check_nego = 1;
}

static void sym2_set_qas(struct scsi_target *starget, int qas)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct sym_hcb *np = sym_get_hcb(shost);
	struct sym_tcb *tp = &np->target[starget->id];

	if (qas)
		tp->tgoal.dt = tp->tgoal.qas = 1;
	else
		tp->tgoal.qas = 0;
	tp->tgoal.check_nego = 1;
}
#endif

static struct spi_function_template sym2_transport_functions = {
	.set_offset	= sym2_set_offset,
	.show_offset	= 1,
	.set_period	= sym2_set_period,
	.show_period	= 1,
	.set_width	= sym2_set_width,
	.show_width	= 1,
	.set_dt		= sym2_set_dt,
	.show_dt	= 1,
#if 0
	.set_iu		= sym2_set_iu,
	.show_iu	= 1,
	.set_qas	= sym2_set_qas,
	.show_qas	= 1,
#endif
	.get_signalling	= sym2_get_signalling,
};

static struct pci_device_id sym2_id_table[] = {
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C810,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C820,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL }, /* new */
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C825,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C815,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C810AP,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL }, /* new */
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C860,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C1510,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_CLASS_STORAGE_SCSI<<8,  0xffff00, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C896,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C895,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C885,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C875,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C1510,
	  PCI_ANY_ID, PCI_ANY_ID,  PCI_CLASS_STORAGE_SCSI<<8,  0xffff00, 0UL }, /* new */
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C895A,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C875A,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C1010_33,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_LSI_53C1010_66,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ PCI_VENDOR_ID_LSI_LOGIC, PCI_DEVICE_ID_NCR_53C875J,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0UL },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, sym2_id_table);

static const struct pci_error_handlers sym2_err_handler = {
	.error_detected	= sym2_io_error_detected,
	.mmio_enabled	= sym2_io_slot_dump,
	.slot_reset	= sym2_io_slot_reset,
	.resume		= sym2_io_resume,
};

static struct pci_driver sym2_driver = {
	.name		= NAME53C8XX,
	.id_table	= sym2_id_table,
	.probe		= sym2_probe,
	.remove		= sym2_remove,
	.err_handler 	= &sym2_err_handler,
};

static int __init sym2_init(void)
{
	int error;

	sym2_setup_params();
	sym2_transport_template = spi_attach_transport(&sym2_transport_functions);
	if (!sym2_transport_template)
		return -ENODEV;

	error = pci_register_driver(&sym2_driver);
	if (error)
		spi_release_transport(sym2_transport_template);
	return error;
}

static void __exit sym2_exit(void)
{
	pci_unregister_driver(&sym2_driver);
	spi_release_transport(sym2_transport_template);
}

module_init(sym2_init);
module_exit(sym2_exit);
