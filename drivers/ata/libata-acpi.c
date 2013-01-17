/*
 * libata-acpi.c
 * Provides ACPI support for PATA/SATA.
 *
 * Copyright (C) 2006 Intel Corp.
 * Copyright (C) 2006 Randy Dunlap
 */

#include <linux/module.h>
#include <linux/ata.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/libata.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <scsi/scsi_device.h>
#include "libata.h"

#include <acpi/acpi_bus.h>

unsigned int ata_acpi_gtf_filter = ATA_ACPI_FILTER_DEFAULT;
module_param_named(acpi_gtf_filter, ata_acpi_gtf_filter, int, 0644);
MODULE_PARM_DESC(acpi_gtf_filter, "filter mask for ACPI _GTF commands, set to filter out (0x1=set xfermode, 0x2=lock/freeze lock, 0x4=DIPM, 0x8=FPDMA non-zero offset, 0x10=FPDMA DMA Setup FIS auto-activate)");

#define NO_PORT_MULT		0xffff
#define SATA_ADR(root, pmp)	(((root) << 16) | (pmp))

#define REGS_PER_GTF		7
struct ata_acpi_gtf {
	u8	tf[REGS_PER_GTF];	/* regs. 0x1f1 - 0x1f7 */
} __packed;

/*
 *	Helper - belongs in the PCI layer somewhere eventually
 */
static int is_pci_dev(struct device *dev)
{
	return (dev->bus == &pci_bus_type);
}

static void ata_acpi_clear_gtf(struct ata_device *dev)
{
	kfree(dev->gtf_cache);
	dev->gtf_cache = NULL;
}

/**
 * ata_ap_acpi_handle - provide the acpi_handle for an ata_port
 * @ap: the acpi_handle returned will correspond to this port
 *
 * Returns the acpi_handle for the ACPI namespace object corresponding to
 * the ata_port passed into the function, or NULL if no such object exists
 */
acpi_handle ata_ap_acpi_handle(struct ata_port *ap)
{
	if (ap->flags & ATA_FLAG_ACPI_SATA)
		return NULL;

	return acpi_get_child(DEVICE_ACPI_HANDLE(ap->host->dev), ap->port_no);
}
EXPORT_SYMBOL(ata_ap_acpi_handle);

/**
 * ata_dev_acpi_handle - provide the acpi_handle for an ata_device
 * @dev: the acpi_device returned will correspond to this port
 *
 * Returns the acpi_handle for the ACPI namespace object corresponding to
 * the ata_device passed into the function, or NULL if no such object exists
 */
acpi_handle ata_dev_acpi_handle(struct ata_device *dev)
{
	acpi_integer adr;
	struct ata_port *ap = dev->link->ap;

	if (dev->flags & ATA_DFLAG_ACPI_DISABLED)
		return NULL;

	if (ap->flags & ATA_FLAG_ACPI_SATA) {
		if (!sata_pmp_attached(ap))
			adr = SATA_ADR(ap->port_no, NO_PORT_MULT);
		else
			adr = SATA_ADR(ap->port_no, dev->link->pmp);
		return acpi_get_child(DEVICE_ACPI_HANDLE(ap->host->dev), adr);
	} else
		return acpi_get_child(ata_ap_acpi_handle(ap), dev->devno);
}
EXPORT_SYMBOL(ata_dev_acpi_handle);

/* @ap and @dev are the same as ata_acpi_handle_hotplug() */
static void ata_acpi_detach_device(struct ata_port *ap, struct ata_device *dev)
{
	if (dev)
		dev->flags |= ATA_DFLAG_DETACH;
	else {
		struct ata_link *tlink;
		struct ata_device *tdev;

		ata_for_each_link(tlink, ap, EDGE)
			ata_for_each_dev(tdev, tlink, ALL)
				tdev->flags |= ATA_DFLAG_DETACH;
	}

	ata_port_schedule_eh(ap);
}

/**
 * ata_acpi_handle_hotplug - ACPI event handler backend
 * @ap: ATA port ACPI event occurred
 * @dev: ATA device ACPI event occurred (can be NULL)
 * @event: ACPI event which occurred
 *
 * All ACPI bay / device realted events end up in this function.  If
 * the event is port-wide @dev is NULL.  If the event is specific to a
 * device, @dev points to it.
 *
 * Hotplug (as opposed to unplug) notification is always handled as
 * port-wide while unplug only kills the target device on device-wide
 * event.
 *
 * LOCKING:
 * ACPI notify handler context.  May sleep.
 */
static void ata_acpi_handle_hotplug(struct ata_port *ap, struct ata_device *dev,
				    u32 event)
{
	struct ata_eh_info *ehi = &ap->link.eh_info;
	int wait = 0;
	unsigned long flags;

	spin_lock_irqsave(ap->lock, flags);
	/*
	 * When dock driver calls into the routine, it will always use
	 * ACPI_NOTIFY_BUS_CHECK/ACPI_NOTIFY_DEVICE_CHECK for add and
	 * ACPI_NOTIFY_EJECT_REQUEST for remove
	 */
	switch (event) {
	case ACPI_NOTIFY_BUS_CHECK:
	case ACPI_NOTIFY_DEVICE_CHECK:
		ata_ehi_push_desc(ehi, "ACPI event");

		ata_ehi_hotplugged(ehi);
		ata_port_freeze(ap);
		break;
	case ACPI_NOTIFY_EJECT_REQUEST:
		ata_ehi_push_desc(ehi, "ACPI event");

		ata_acpi_detach_device(ap, dev);
		wait = 1;
		break;
	}

	spin_unlock_irqrestore(ap->lock, flags);

	if (wait)
		ata_port_wait_eh(ap);
}

static void ata_acpi_dev_notify_dock(acpi_handle handle, u32 event, void *data)
{
	struct ata_device *dev = data;

	ata_acpi_handle_hotplug(dev->link->ap, dev, event);
}

static void ata_acpi_ap_notify_dock(acpi_handle handle, u32 event, void *data)
{
	struct ata_port *ap = data;

	ata_acpi_handle_hotplug(ap, NULL, event);
}

static void ata_acpi_uevent(struct ata_port *ap, struct ata_device *dev,
	u32 event)
{
	struct kobject *kobj = NULL;
	char event_string[20];
	char *envp[] = { event_string, NULL };

	if (dev) {
		if (dev->sdev)
			kobj = &dev->sdev->sdev_gendev.kobj;
	} else
		kobj = &ap->dev->kobj;

	if (kobj) {
		snprintf(event_string, 20, "BAY_EVENT=%d", event);
		kobject_uevent_env(kobj, KOBJ_CHANGE, envp);
	}
}

static void ata_acpi_ap_uevent(acpi_handle handle, u32 event, void *data)
{
	ata_acpi_uevent(data, NULL, event);
}

static void ata_acpi_dev_uevent(acpi_handle handle, u32 event, void *data)
{
	struct ata_device *dev = data;
	ata_acpi_uevent(dev->link->ap, dev, event);
}

static const struct acpi_dock_ops ata_acpi_dev_dock_ops = {
	.handler = ata_acpi_dev_notify_dock,
	.uevent = ata_acpi_dev_uevent,
};

static const struct acpi_dock_ops ata_acpi_ap_dock_ops = {
	.handler = ata_acpi_ap_notify_dock,
	.uevent = ata_acpi_ap_uevent,
};

/**
 * ata_acpi_dissociate - dissociate ATA host from ACPI objects
 * @host: target ATA host
 *
 * This function is called during driver detach after the whole host
 * is shut down.
 *
 * LOCKING:
 * EH context.
 */
void ata_acpi_dissociate(struct ata_host *host)
{
	int i;

	/* Restore initial _GTM values so that driver which attaches
	 * afterward can use them too.
	 */
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];
		const struct ata_acpi_gtm *gtm = ata_acpi_init_gtm(ap);

		if (ata_ap_acpi_handle(ap) && gtm)
			ata_acpi_stm(ap, gtm);
	}
}

/**
 * ata_acpi_gtm - execute _GTM
 * @ap: target ATA port
 * @gtm: out parameter for _GTM result
 *
 * Evaluate _GTM and store the result in @gtm.
 *
 * LOCKING:
 * EH context.
 *
 * RETURNS:
 * 0 on success, -ENOENT if _GTM doesn't exist, -errno on failure.
 */
int ata_acpi_gtm(struct ata_port *ap, struct ata_acpi_gtm *gtm)
{
	struct acpi_buffer output = { .length = ACPI_ALLOCATE_BUFFER };
	union acpi_object *out_obj;
	acpi_status status;
	int rc = 0;

	status = acpi_evaluate_object(ata_ap_acpi_handle(ap), "_GTM", NULL,
				      &output);

	rc = -ENOENT;
	if (status == AE_NOT_FOUND)
		goto out_free;

	rc = -EINVAL;
	if (ACPI_FAILURE(status)) {
		ata_port_err(ap, "ACPI get timing mode failed (AE 0x%x)\n",
			     status);
		goto out_free;
	}

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER) {
		ata_port_warn(ap, "_GTM returned unexpected object type 0x%x\n",
			      out_obj->type);

		goto out_free;
	}

	if (out_obj->buffer.length != sizeof(struct ata_acpi_gtm)) {
		ata_port_err(ap, "_GTM returned invalid length %d\n",
			     out_obj->buffer.length);
		goto out_free;
	}

	memcpy(gtm, out_obj->buffer.pointer, sizeof(struct ata_acpi_gtm));
	rc = 0;
 out_free:
	kfree(output.pointer);
	return rc;
}

EXPORT_SYMBOL_GPL(ata_acpi_gtm);

/**
 * ata_acpi_stm - execute _STM
 * @ap: target ATA port
 * @stm: timing parameter to _STM
 *
 * Evaluate _STM with timing parameter @stm.
 *
 * LOCKING:
 * EH context.
 *
 * RETURNS:
 * 0 on success, -ENOENT if _STM doesn't exist, -errno on failure.
 */
int ata_acpi_stm(struct ata_port *ap, const struct ata_acpi_gtm *stm)
{
	acpi_status status;
	struct ata_acpi_gtm		stm_buf = *stm;
	struct acpi_object_list         input;
	union acpi_object               in_params[3];

	in_params[0].type = ACPI_TYPE_BUFFER;
	in_params[0].buffer.length = sizeof(struct ata_acpi_gtm);
	in_params[0].buffer.pointer = (u8 *)&stm_buf;
	/* Buffers for id may need byteswapping ? */
	in_params[1].type = ACPI_TYPE_BUFFER;
	in_params[1].buffer.length = 512;
	in_params[1].buffer.pointer = (u8 *)ap->link.device[0].id;
	in_params[2].type = ACPI_TYPE_BUFFER;
	in_params[2].buffer.length = 512;
	in_params[2].buffer.pointer = (u8 *)ap->link.device[1].id;

	input.count = 3;
	input.pointer = in_params;

	status = acpi_evaluate_object(ata_ap_acpi_handle(ap), "_STM", &input,
				      NULL);

	if (status == AE_NOT_FOUND)
		return -ENOENT;
	if (ACPI_FAILURE(status)) {
		ata_port_err(ap, "ACPI set timing mode failed (status=0x%x)\n",
			     status);
		return -EINVAL;
	}
	return 0;
}

EXPORT_SYMBOL_GPL(ata_acpi_stm);

/**
 * ata_dev_get_GTF - get the drive bootup default taskfile settings
 * @dev: target ATA device
 * @gtf: output parameter for buffer containing _GTF taskfile arrays
 *
 * This applies to both PATA and SATA drives.
 *
 * The _GTF method has no input parameters.
 * It returns a variable number of register set values (registers
 * hex 1F1..1F7, taskfiles).
 * The <variable number> is not known in advance, so have ACPI-CA
 * allocate the buffer as needed and return it, then free it later.
 *
 * LOCKING:
 * EH context.
 *
 * RETURNS:
 * Number of taskfiles on success, 0 if _GTF doesn't exist.  -EINVAL
 * if _GTF is invalid.
 */
static int ata_dev_get_GTF(struct ata_device *dev, struct ata_acpi_gtf **gtf)
{
	struct ata_port *ap = dev->link->ap;
	acpi_status status;
	struct acpi_buffer output;
	union acpi_object *out_obj;
	int rc = 0;

	/* if _GTF is cached, use the cached value */
	if (dev->gtf_cache) {
		out_obj = dev->gtf_cache;
		goto done;
	}

	/* set up output buffer */
	output.length = ACPI_ALLOCATE_BUFFER;
	output.pointer = NULL;	/* ACPI-CA sets this; save/free it later */

	if (ata_msg_probe(ap))
		ata_dev_dbg(dev, "%s: ENTER: port#: %d\n",
			    __func__, ap->port_no);

	/* _GTF has no input parameters */
	status = acpi_evaluate_object(ata_dev_acpi_handle(dev), "_GTF", NULL,
				      &output);
	out_obj = dev->gtf_cache = output.pointer;

	if (ACPI_FAILURE(status)) {
		if (status != AE_NOT_FOUND) {
			ata_dev_warn(dev, "_GTF evaluation failed (AE 0x%x)\n",
				     status);
			rc = -EINVAL;
		}
		goto out_free;
	}

	if (!output.length || !output.pointer) {
		if (ata_msg_probe(ap))
			ata_dev_dbg(dev, "%s: Run _GTF: length or ptr is NULL (0x%llx, 0x%p)\n",
				    __func__,
				    (unsigned long long)output.length,
				    output.pointer);
		rc = -EINVAL;
		goto out_free;
	}

	if (out_obj->type != ACPI_TYPE_BUFFER) {
		ata_dev_warn(dev, "_GTF unexpected object type 0x%x\n",
			     out_obj->type);
		rc = -EINVAL;
		goto out_free;
	}

	if (out_obj->buffer.length % REGS_PER_GTF) {
		ata_dev_warn(dev, "unexpected _GTF length (%d)\n",
			     out_obj->buffer.length);
		rc = -EINVAL;
		goto out_free;
	}

 done:
	rc = out_obj->buffer.length / REGS_PER_GTF;
	if (gtf) {
		*gtf = (void *)out_obj->buffer.pointer;
		if (ata_msg_probe(ap))
			ata_dev_dbg(dev, "%s: returning gtf=%p, gtf_count=%d\n",
				    __func__, *gtf, rc);
	}
	return rc;

 out_free:
	ata_acpi_clear_gtf(dev);
	return rc;
}

/**
 * ata_acpi_gtm_xfermode - determine xfermode from GTM parameter
 * @dev: target device
 * @gtm: GTM parameter to use
 *
 * Determine xfermask for @dev from @gtm.
 *
 * LOCKING:
 * None.
 *
 * RETURNS:
 * Determined xfermask.
 */
unsigned long ata_acpi_gtm_xfermask(struct ata_device *dev,
				    const struct ata_acpi_gtm *gtm)
{
	unsigned long xfer_mask = 0;
	unsigned int type;
	int unit;
	u8 mode;

	/* we always use the 0 slot for crap hardware */
	unit = dev->devno;
	if (!(gtm->flags & 0x10))
		unit = 0;

	/* PIO */
	mode = ata_timing_cycle2mode(ATA_SHIFT_PIO, gtm->drive[unit].pio);
	xfer_mask |= ata_xfer_mode2mask(mode);

	/* See if we have MWDMA or UDMA data. We don't bother with
	 * MWDMA if UDMA is available as this means the BIOS set UDMA
	 * and our error changedown if it works is UDMA to PIO anyway.
	 */
	if (!(gtm->flags & (1 << (2 * unit))))
		type = ATA_SHIFT_MWDMA;
	else
		type = ATA_SHIFT_UDMA;

	mode = ata_timing_cycle2mode(type, gtm->drive[unit].dma);
	xfer_mask |= ata_xfer_mode2mask(mode);

	return xfer_mask;
}
EXPORT_SYMBOL_GPL(ata_acpi_gtm_xfermask);

/**
 * ata_acpi_cbl_80wire		-	Check for 80 wire cable
 * @ap: Port to check
 * @gtm: GTM data to use
 *
 * Return 1 if the @gtm indicates the BIOS selected an 80wire mode.
 */
int ata_acpi_cbl_80wire(struct ata_port *ap, const struct ata_acpi_gtm *gtm)
{
	struct ata_device *dev;

	ata_for_each_dev(dev, &ap->link, ENABLED) {
		unsigned long xfer_mask, udma_mask;

		xfer_mask = ata_acpi_gtm_xfermask(dev, gtm);
		ata_unpack_xfermask(xfer_mask, NULL, NULL, &udma_mask);

		if (udma_mask & ~ATA_UDMA_MASK_40C)
			return 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(ata_acpi_cbl_80wire);

static void ata_acpi_gtf_to_tf(struct ata_device *dev,
			       const struct ata_acpi_gtf *gtf,
			       struct ata_taskfile *tf)
{
	ata_tf_init(dev, tf);

	tf->flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf->protocol = ATA_PROT_NODATA;
	tf->feature = gtf->tf[0];	/* 0x1f1 */
	tf->nsect   = gtf->tf[1];	/* 0x1f2 */
	tf->lbal    = gtf->tf[2];	/* 0x1f3 */
	tf->lbam    = gtf->tf[3];	/* 0x1f4 */
	tf->lbah    = gtf->tf[4];	/* 0x1f5 */
	tf->device  = gtf->tf[5];	/* 0x1f6 */
	tf->command = gtf->tf[6];	/* 0x1f7 */
}

static int ata_acpi_filter_tf(struct ata_device *dev,
			      const struct ata_taskfile *tf,
			      const struct ata_taskfile *ptf)
{
	if (dev->gtf_filter & ATA_ACPI_FILTER_SETXFER) {
		/* libata doesn't use ACPI to configure transfer mode.
		 * It will only confuse device configuration.  Skip.
		 */
		if (tf->command == ATA_CMD_SET_FEATURES &&
		    tf->feature == SETFEATURES_XFER)
			return 1;
	}

	if (dev->gtf_filter & ATA_ACPI_FILTER_LOCK) {
		/* BIOS writers, sorry but we don't wanna lock
		 * features unless the user explicitly said so.
		 */

		/* DEVICE CONFIGURATION FREEZE LOCK */
		if (tf->command == ATA_CMD_CONF_OVERLAY &&
		    tf->feature == ATA_DCO_FREEZE_LOCK)
			return 1;

		/* SECURITY FREEZE LOCK */
		if (tf->command == ATA_CMD_SEC_FREEZE_LOCK)
			return 1;

		/* SET MAX LOCK and SET MAX FREEZE LOCK */
		if ((!ptf || ptf->command != ATA_CMD_READ_NATIVE_MAX) &&
		    tf->command == ATA_CMD_SET_MAX &&
		    (tf->feature == ATA_SET_MAX_LOCK ||
		     tf->feature == ATA_SET_MAX_FREEZE_LOCK))
			return 1;
	}

	if (tf->command == ATA_CMD_SET_FEATURES &&
	    tf->feature == SETFEATURES_SATA_ENABLE) {
		/* inhibit enabling DIPM */
		if (dev->gtf_filter & ATA_ACPI_FILTER_DIPM &&
		    tf->nsect == SATA_DIPM)
			return 1;

		/* inhibit FPDMA non-zero offset */
		if (dev->gtf_filter & ATA_ACPI_FILTER_FPDMA_OFFSET &&
		    (tf->nsect == SATA_FPDMA_OFFSET ||
		     tf->nsect == SATA_FPDMA_IN_ORDER))
			return 1;

		/* inhibit FPDMA auto activation */
		if (dev->gtf_filter & ATA_ACPI_FILTER_FPDMA_AA &&
		    tf->nsect == SATA_FPDMA_AA)
			return 1;
	}

	return 0;
}

/**
 * ata_acpi_run_tf - send taskfile registers to host controller
 * @dev: target ATA device
 * @gtf: raw ATA taskfile register set (0x1f1 - 0x1f7)
 *
 * Outputs ATA taskfile to standard ATA host controller.
 * Writes the control, feature, nsect, lbal, lbam, and lbah registers.
 * Optionally (ATA_TFLAG_LBA48) writes hob_feature, hob_nsect,
 * hob_lbal, hob_lbam, and hob_lbah.
 *
 * This function waits for idle (!BUSY and !DRQ) after writing
 * registers.  If the control register has a new value, this
 * function also waits for idle after writing control and before
 * writing the remaining registers.
 *
 * LOCKING:
 * EH context.
 *
 * RETURNS:
 * 1 if command is executed successfully.  0 if ignored, rejected or
 * filtered out, -errno on other errors.
 */
static int ata_acpi_run_tf(struct ata_device *dev,
			   const struct ata_acpi_gtf *gtf,
			   const struct ata_acpi_gtf *prev_gtf)
{
	struct ata_taskfile *pptf = NULL;
	struct ata_taskfile tf, ptf, rtf;
	unsigned int err_mask;
	const char *level;
	const char *descr;
	char msg[60];
	int rc;

	if ((gtf->tf[0] == 0) && (gtf->tf[1] == 0) && (gtf->tf[2] == 0)
	    && (gtf->tf[3] == 0) && (gtf->tf[4] == 0) && (gtf->tf[5] == 0)
	    && (gtf->tf[6] == 0))
		return 0;

	ata_acpi_gtf_to_tf(dev, gtf, &tf);
	if (prev_gtf) {
		ata_acpi_gtf_to_tf(dev, prev_gtf, &ptf);
		pptf = &ptf;
	}

	if (!ata_acpi_filter_tf(dev, &tf, pptf)) {
		rtf = tf;
		err_mask = ata_exec_internal(dev, &rtf, NULL,
					     DMA_NONE, NULL, 0, 0);

		switch (err_mask) {
		case 0:
			level = KERN_DEBUG;
			snprintf(msg, sizeof(msg), "succeeded");
			rc = 1;
			break;

		case AC_ERR_DEV:
			level = KERN_INFO;
			snprintf(msg, sizeof(msg),
				 "rejected by device (Stat=0x%02x Err=0x%02x)",
				 rtf.command, rtf.feature);
			rc = 0;
			break;

		default:
			level = KERN_ERR;
			snprintf(msg, sizeof(msg),
				 "failed (Emask=0x%x Stat=0x%02x Err=0x%02x)",
				 err_mask, rtf.command, rtf.feature);
			rc = -EIO;
			break;
		}
	} else {
		level = KERN_INFO;
		snprintf(msg, sizeof(msg), "filtered out");
		rc = 0;
	}
	descr = ata_get_cmd_descript(tf.command);

	ata_dev_printk(dev, level,
		       "ACPI cmd %02x/%02x:%02x:%02x:%02x:%02x:%02x (%s) %s\n",
		       tf.command, tf.feature, tf.nsect, tf.lbal,
		       tf.lbam, tf.lbah, tf.device,
		       (descr ? descr : "unknown"), msg);

	return rc;
}

/**
 * ata_acpi_exec_tfs - get then write drive taskfile settings
 * @dev: target ATA device
 * @nr_executed: out parameter for the number of executed commands
 *
 * Evaluate _GTF and execute returned taskfiles.
 *
 * LOCKING:
 * EH context.
 *
 * RETURNS:
 * Number of executed taskfiles on success, 0 if _GTF doesn't exist.
 * -errno on other errors.
 */
static int ata_acpi_exec_tfs(struct ata_device *dev, int *nr_executed)
{
	struct ata_acpi_gtf *gtf = NULL, *pgtf = NULL;
	int gtf_count, i, rc;

	/* get taskfiles */
	rc = ata_dev_get_GTF(dev, &gtf);
	if (rc < 0)
		return rc;
	gtf_count = rc;

	/* execute them */
	for (i = 0; i < gtf_count; i++, gtf++) {
		rc = ata_acpi_run_tf(dev, gtf, pgtf);
		if (rc < 0)
			break;
		if (rc) {
			(*nr_executed)++;
			pgtf = gtf;
		}
	}

	ata_acpi_clear_gtf(dev);

	if (rc < 0)
		return rc;
	return 0;
}

/**
 * ata_acpi_push_id - send Identify data to drive
 * @dev: target ATA device
 *
 * _SDD ACPI object: for SATA mode only
 * Must be after Identify (Packet) Device -- uses its data
 * ATM this function never returns a failure.  It is an optional
 * method and if it fails for whatever reason, we should still
 * just keep going.
 *
 * LOCKING:
 * EH context.
 *
 * RETURNS:
 * 0 on success, -ENOENT if _SDD doesn't exist, -errno on failure.
 */
static int ata_acpi_push_id(struct ata_device *dev)
{
	struct ata_port *ap = dev->link->ap;
	acpi_status status;
	struct acpi_object_list input;
	union acpi_object in_params[1];

	if (ata_msg_probe(ap))
		ata_dev_dbg(dev, "%s: ix = %d, port#: %d\n",
			    __func__, dev->devno, ap->port_no);

	/* Give the drive Identify data to the drive via the _SDD method */
	/* _SDD: set up input parameters */
	input.count = 1;
	input.pointer = in_params;
	in_params[0].type = ACPI_TYPE_BUFFER;
	in_params[0].buffer.length = sizeof(dev->id[0]) * ATA_ID_WORDS;
	in_params[0].buffer.pointer = (u8 *)dev->id;
	/* Output buffer: _SDD has no output */

	/* It's OK for _SDD to be missing too. */
	swap_buf_le16(dev->id, ATA_ID_WORDS);
	status = acpi_evaluate_object(ata_dev_acpi_handle(dev), "_SDD", &input,
				      NULL);
	swap_buf_le16(dev->id, ATA_ID_WORDS);

	if (status == AE_NOT_FOUND)
		return -ENOENT;

	if (ACPI_FAILURE(status)) {
		ata_dev_warn(dev, "ACPI _SDD failed (AE 0x%x)\n", status);
		return -EIO;
	}

	return 0;
}

/**
 * ata_acpi_on_suspend - ATA ACPI hook called on suspend
 * @ap: target ATA port
 *
 * This function is called when @ap is about to be suspended.  All
 * devices are already put to sleep but the port_suspend() callback
 * hasn't been executed yet.  Error return from this function aborts
 * suspend.
 *
 * LOCKING:
 * EH context.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
int ata_acpi_on_suspend(struct ata_port *ap)
{
	/* nada */
	return 0;
}

/**
 * ata_acpi_on_resume - ATA ACPI hook called on resume
 * @ap: target ATA port
 *
 * This function is called when @ap is resumed - right after port
 * itself is resumed but before any EH action is taken.
 *
 * LOCKING:
 * EH context.
 */
void ata_acpi_on_resume(struct ata_port *ap)
{
	const struct ata_acpi_gtm *gtm = ata_acpi_init_gtm(ap);
	struct ata_device *dev;

	if (ata_ap_acpi_handle(ap) && gtm) {
		/* _GTM valid */

		/* restore timing parameters */
		ata_acpi_stm(ap, gtm);

		/* _GTF should immediately follow _STM so that it can
		 * use values set by _STM.  Cache _GTF result and
		 * schedule _GTF.
		 */
		ata_for_each_dev(dev, &ap->link, ALL) {
			ata_acpi_clear_gtf(dev);
			if (ata_dev_enabled(dev) &&
			    ata_dev_get_GTF(dev, NULL) >= 0)
				dev->flags |= ATA_DFLAG_ACPI_PENDING;
		}
	} else {
		/* SATA _GTF needs to be evaulated after _SDD and
		 * there's no reason to evaluate IDE _GTF early
		 * without _STM.  Clear cache and schedule _GTF.
		 */
		ata_for_each_dev(dev, &ap->link, ALL) {
			ata_acpi_clear_gtf(dev);
			if (ata_dev_enabled(dev))
				dev->flags |= ATA_DFLAG_ACPI_PENDING;
		}
	}
}

/**
 * ata_acpi_set_state - set the port power state
 * @ap: target ATA port
 * @state: state, on/off
 *
 * This function executes the _PS0/_PS3 ACPI method to set the power state.
 * ACPI spec requires _PS0 when IDE power on and _PS3 when power off
 */
void ata_acpi_set_state(struct ata_port *ap, pm_message_t state)
{
	struct ata_device *dev;
	acpi_handle handle;
	int acpi_state;

	/* channel first and then drives for power on and vica versa
	   for power off */
	handle = ata_ap_acpi_handle(ap);
	if (handle && state.event == PM_EVENT_ON)
		acpi_bus_set_power(handle, ACPI_STATE_D0);

	ata_for_each_dev(dev, &ap->link, ENABLED) {
		handle = ata_dev_acpi_handle(dev);
		if (!handle)
			continue;

		if (state.event != PM_EVENT_ON) {
			acpi_state = acpi_pm_device_sleep_state(
				&dev->sdev->sdev_gendev, NULL, ACPI_STATE_D3);
			if (acpi_state > 0)
				acpi_bus_set_power(handle, acpi_state);
			/* TBD: need to check if it's runtime pm request */
			acpi_pm_device_run_wake(
				&dev->sdev->sdev_gendev, true);
		} else {
			/* Ditto */
			acpi_pm_device_run_wake(
				&dev->sdev->sdev_gendev, false);
			acpi_bus_set_power(handle, ACPI_STATE_D0);
		}
	}

	handle = ata_ap_acpi_handle(ap);
	if (handle && state.event != PM_EVENT_ON)
		acpi_bus_set_power(handle, ACPI_STATE_D3);
}

/**
 * ata_acpi_on_devcfg - ATA ACPI hook called on device donfiguration
 * @dev: target ATA device
 *
 * This function is called when @dev is about to be configured.
 * IDENTIFY data might have been modified after this hook is run.
 *
 * LOCKING:
 * EH context.
 *
 * RETURNS:
 * Positive number if IDENTIFY data needs to be refreshed, 0 if not,
 * -errno on failure.
 */
int ata_acpi_on_devcfg(struct ata_device *dev)
{
	struct ata_port *ap = dev->link->ap;
	struct ata_eh_context *ehc = &ap->link.eh_context;
	int acpi_sata = ap->flags & ATA_FLAG_ACPI_SATA;
	int nr_executed = 0;
	int rc;

	if (!ata_dev_acpi_handle(dev))
		return 0;

	/* do we need to do _GTF? */
	if (!(dev->flags & ATA_DFLAG_ACPI_PENDING) &&
	    !(acpi_sata && (ehc->i.flags & ATA_EHI_DID_HARDRESET)))
		return 0;

	/* do _SDD if SATA */
	if (acpi_sata) {
		rc = ata_acpi_push_id(dev);
		if (rc && rc != -ENOENT)
			goto acpi_err;
	}

	/* do _GTF */
	rc = ata_acpi_exec_tfs(dev, &nr_executed);
	if (rc)
		goto acpi_err;

	dev->flags &= ~ATA_DFLAG_ACPI_PENDING;

	/* refresh IDENTIFY page if any _GTF command has been executed */
	if (nr_executed) {
		rc = ata_dev_reread_id(dev, 0);
		if (rc < 0) {
			ata_dev_err(dev,
				    "failed to IDENTIFY after ACPI commands\n");
			return rc;
		}
	}

	return 0;

 acpi_err:
	/* ignore evaluation failure if we can continue safely */
	if (rc == -EINVAL && !nr_executed && !(ap->pflags & ATA_PFLAG_FROZEN))
		return 0;

	/* fail and let EH retry once more for unknown IO errors */
	if (!(dev->flags & ATA_DFLAG_ACPI_FAILED)) {
		dev->flags |= ATA_DFLAG_ACPI_FAILED;
		return rc;
	}

	dev->flags |= ATA_DFLAG_ACPI_DISABLED;
	ata_dev_warn(dev, "ACPI: failed the second time, disabled\n");

	/* We can safely continue if no _GTF command has been executed
	 * and port is not frozen.
	 */
	if (!nr_executed && !(ap->pflags & ATA_PFLAG_FROZEN))
		return 0;

	return rc;
}

/**
 * ata_acpi_on_disable - ATA ACPI hook called when a device is disabled
 * @dev: target ATA device
 *
 * This function is called when @dev is about to be disabled.
 *
 * LOCKING:
 * EH context.
 */
void ata_acpi_on_disable(struct ata_device *dev)
{
	ata_acpi_clear_gtf(dev);
}

static void ata_acpi_wake_dev(acpi_handle handle, u32 event, void *context)
{
	struct ata_device *ata_dev = context;

	if (event == ACPI_NOTIFY_DEVICE_WAKE && ata_dev &&
			pm_runtime_suspended(&ata_dev->sdev->sdev_gendev))
		scsi_autopm_get_device(ata_dev->sdev);
}

static void ata_acpi_add_pm_notifier(struct ata_device *dev)
{
	struct acpi_device *acpi_dev;
	acpi_handle handle;
	acpi_status status;

	handle = ata_dev_acpi_handle(dev);
	if (!handle)
		return;

	status = acpi_bus_get_device(handle, &acpi_dev);
	if (ACPI_FAILURE(status))
		return;

	if (dev->sdev->can_power_off) {
		acpi_install_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
			ata_acpi_wake_dev, dev);
		device_set_run_wake(&dev->sdev->sdev_gendev, true);
	}
}

static void ata_acpi_remove_pm_notifier(struct ata_device *dev)
{
	struct acpi_device *acpi_dev;
	acpi_handle handle;
	acpi_status status;

	handle = ata_dev_acpi_handle(dev);
	if (!handle)
		return;

	status = acpi_bus_get_device(handle, &acpi_dev);
	if (ACPI_FAILURE(status))
		return;

	if (dev->sdev->can_power_off) {
		device_set_run_wake(&dev->sdev->sdev_gendev, false);
		acpi_remove_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
			ata_acpi_wake_dev);
	}
}

static void ata_acpi_register_power_resource(struct ata_device *dev)
{
	struct scsi_device *sdev = dev->sdev;
	acpi_handle handle;

	handle = ata_dev_acpi_handle(dev);
	if (handle)
		acpi_dev_pm_remove_dependent(handle, &sdev->sdev_gendev);
}

static void ata_acpi_unregister_power_resource(struct ata_device *dev)
{
	struct scsi_device *sdev = dev->sdev;
	acpi_handle handle;

	handle = ata_dev_acpi_handle(dev);
	if (handle)
		acpi_dev_pm_remove_dependent(handle, &sdev->sdev_gendev);
}

void ata_acpi_bind(struct ata_device *dev)
{
	ata_acpi_add_pm_notifier(dev);
	ata_acpi_register_power_resource(dev);
}

void ata_acpi_unbind(struct ata_device *dev)
{
	ata_acpi_remove_pm_notifier(dev);
	ata_acpi_unregister_power_resource(dev);
}

static int compat_pci_ata(struct ata_port *ap)
{
	struct device *dev = ap->tdev.parent;
	struct pci_dev *pdev;

	if (!is_pci_dev(dev))
		return 0;

	pdev = to_pci_dev(dev);

	if ((pdev->class >> 8) != PCI_CLASS_STORAGE_SATA &&
	    (pdev->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return 0;

	return 1;
}

static int ata_acpi_bind_host(struct ata_port *ap, acpi_handle *handle)
{
	if (ap->flags & ATA_FLAG_ACPI_SATA)
		return -ENODEV;

	*handle = acpi_get_child(DEVICE_ACPI_HANDLE(ap->tdev.parent),
			ap->port_no);

	if (!*handle)
		return -ENODEV;

	if (ata_acpi_gtm(ap, &ap->__acpi_init_gtm) == 0)
		ap->pflags |= ATA_PFLAG_INIT_GTM_VALID;

	return 0;
}

static int ata_acpi_bind_device(struct ata_port *ap, struct scsi_device *sdev,
				acpi_handle *handle)
{
	struct ata_device *ata_dev;
	acpi_status status;
	struct acpi_device *acpi_dev;
	struct acpi_device_power_state *states;

	if (ap->flags & ATA_FLAG_ACPI_SATA) {
		if (!sata_pmp_attached(ap))
			ata_dev = &ap->link.device[sdev->id];
		else
			ata_dev = &ap->pmp_link[sdev->channel].device[sdev->id];
	}
	else {
		ata_dev = &ap->link.device[sdev->id];
	}

	*handle = ata_dev_acpi_handle(ata_dev);

	if (!*handle)
		return -ENODEV;

	status = acpi_bus_get_device(*handle, &acpi_dev);
	if (ACPI_FAILURE(status))
		return 0;

	/*
	 * If firmware has _PS3 or _PR3 for this device,
	 * and this ata ODD device support device attention,
	 * it means this device can be powered off
	 */
	states = acpi_dev->power.states;
	if ((states[ACPI_STATE_D3_HOT].flags.valid ||
			states[ACPI_STATE_D3_COLD].flags.explicit_set) &&
			ata_dev->flags & ATA_DFLAG_DA)
		sdev->can_power_off = 1;

	return 0;
}

static int is_ata_port(const struct device *dev)
{
	return dev->type == &ata_port_type;
}

static struct ata_port *dev_to_ata_port(struct device *dev)
{
	while (!is_ata_port(dev)) {
		if (!dev->parent)
			return NULL;
		dev = dev->parent;
	}
	return to_ata_port(dev);
}

static int ata_acpi_find_device(struct device *dev, acpi_handle *handle)
{
	struct ata_port *ap = dev_to_ata_port(dev);

	if (!ap)
		return -ENODEV;

	if (!compat_pci_ata(ap))
		return -ENODEV;

	if (scsi_is_host_device(dev))
		return ata_acpi_bind_host(ap, handle);
	else if (scsi_is_sdev_device(dev)) {
		struct scsi_device *sdev = to_scsi_device(dev);

		return ata_acpi_bind_device(ap, sdev, handle);
	} else
		return -ENODEV;
}

static int ata_acpi_find_dummy(struct device *dev, acpi_handle *handle)
{
	return -ENODEV;
}

static struct acpi_bus_type ata_acpi_bus = {
	.find_bridge = ata_acpi_find_dummy,
	.find_device = ata_acpi_find_device,
};

int ata_acpi_register(void)
{
	return scsi_register_acpi_bus_type(&ata_acpi_bus);
}

void ata_acpi_unregister(void)
{
	scsi_unregister_acpi_bus_type(&ata_acpi_bus);
}
