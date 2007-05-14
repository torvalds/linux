/*
 * libata-acpi.c
 * Provides ACPI support for PATA/SATA.
 *
 * Copyright (C) 2006 Intel Corp.
 * Copyright (C) 2006 Randy Dunlap
 */

#include <linux/ata.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/libata.h>
#include <linux/pci.h>
#include "libata.h"

#include <acpi/acpi_bus.h>
#include <acpi/acnames.h>
#include <acpi/acnamesp.h>
#include <acpi/acparser.h>
#include <acpi/acexcep.h>
#include <acpi/acmacros.h>
#include <acpi/actypes.h>

#define NO_PORT_MULT		0xffff
#define SATA_ADR(root,pmp)	(((root) << 16) | (pmp))

#define REGS_PER_GTF		7
struct taskfile_array {
	u8	tfa[REGS_PER_GTF];	/* regs. 0x1f1 - 0x1f7 */
};

/*
 *	Helper - belongs in the PCI layer somewhere eventually
 */
static int is_pci_dev(struct device *dev)
{
	return (dev->bus == &pci_bus_type);
}

static void ata_acpi_associate_sata_port(struct ata_port *ap)
{
	acpi_integer adr = SATA_ADR(ap->port_no, NO_PORT_MULT);

	ap->device->acpi_handle = acpi_get_child(ap->host->acpi_handle, adr);
}

static void ata_acpi_associate_ide_port(struct ata_port *ap)
{
	int max_devices, i;

	ap->acpi_handle = acpi_get_child(ap->host->acpi_handle, ap->port_no);
	if (!ap->acpi_handle)
		return;

	max_devices = 1;
	if (ap->flags & ATA_FLAG_SLAVE_POSS)
		max_devices++;

	for (i = 0; i < max_devices; i++) {
		struct ata_device *dev = &ap->device[i];

		dev->acpi_handle = acpi_get_child(ap->acpi_handle, i);
	}
}

/**
 * ata_acpi_associate - associate ATA host with ACPI objects
 * @host: target ATA host
 *
 * Look up ACPI objects associated with @host and initialize
 * acpi_handle fields of @host, its ports and devices accordingly.
 *
 * LOCKING:
 * EH context.
 *
 * RETURNS:
 * 0 on success, -errno on failure.
 */
void ata_acpi_associate(struct ata_host *host)
{
	int i;

	if (!is_pci_dev(host->dev) || libata_noacpi)
		return;

	host->acpi_handle = DEVICE_ACPI_HANDLE(host->dev);
	if (!host->acpi_handle)
		return;

	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		if (host->ports[0]->flags & ATA_FLAG_ACPI_SATA)
			ata_acpi_associate_sata_port(ap);
		else
			ata_acpi_associate_ide_port(ap);
	}
}

/**
 * do_drive_get_GTF - get the drive bootup default taskfile settings
 * @dev: target ATA device
 * @gtf_length: number of bytes of _GTF data returned at @gtf_address
 * @gtf_address: buffer containing _GTF taskfile arrays
 *
 * This applies to both PATA and SATA drives.
 *
 * The _GTF method has no input parameters.
 * It returns a variable number of register set values (registers
 * hex 1F1..1F7, taskfiles).
 * The <variable number> is not known in advance, so have ACPI-CA
 * allocate the buffer as needed and return it, then free it later.
 *
 * The returned @gtf_length and @gtf_address are only valid if the
 * function return value is 0.
 */
static int do_drive_get_GTF(struct ata_device *dev, unsigned int *gtf_length,
			    unsigned long *gtf_address, unsigned long *obj_loc)
{
	struct ata_port *ap = dev->ap;
	acpi_status status;
	struct acpi_buffer output;
	union acpi_object *out_obj;
	int err = -ENODEV;

	*gtf_length = 0;
	*gtf_address = 0UL;
	*obj_loc = 0UL;

	if (!dev->acpi_handle)
		return 0;

	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG, "%s: ENTER: port#: %d\n",
			       __FUNCTION__, ap->port_no);

	if (!ata_dev_enabled(dev) || (ap->flags & ATA_FLAG_DISABLED)) {
		if (ata_msg_probe(ap))
			ata_dev_printk(dev, KERN_DEBUG, "%s: ERR: "
				"ata_dev_present: %d, PORT_DISABLED: %lu\n",
				__FUNCTION__, ata_dev_enabled(dev),
				ap->flags & ATA_FLAG_DISABLED);
		goto out;
	}

	/* Setting up output buffer */
	output.length = ACPI_ALLOCATE_BUFFER;
	output.pointer = NULL;	/* ACPI-CA sets this; save/free it later */

	/* _GTF has no input parameters */
	err = -EIO;
	status = acpi_evaluate_object(dev->acpi_handle, "_GTF",
				      NULL, &output);
	if (ACPI_FAILURE(status)) {
		if (ata_msg_probe(ap))
			ata_dev_printk(dev, KERN_DEBUG,
				"%s: Run _GTF error: status = 0x%x\n",
				__FUNCTION__, status);
		goto out;
	}

	if (!output.length || !output.pointer) {
		if (ata_msg_probe(ap))
			ata_dev_printk(dev, KERN_DEBUG, "%s: Run _GTF: "
				"length or ptr is NULL (0x%llx, 0x%p)\n",
				__FUNCTION__,
				(unsigned long long)output.length,
				output.pointer);
		kfree(output.pointer);
		goto out;
	}

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER) {
		kfree(output.pointer);
		if (ata_msg_probe(ap))
			ata_dev_printk(dev, KERN_DEBUG, "%s: Run _GTF: "
				"error: expected object type of "
				" ACPI_TYPE_BUFFER, got 0x%x\n",
				__FUNCTION__, out_obj->type);
		err = -ENOENT;
		goto out;
	}

	if (!out_obj->buffer.length || !out_obj->buffer.pointer ||
	    out_obj->buffer.length % REGS_PER_GTF) {
		if (ata_msg_drv(ap))
			ata_dev_printk(dev, KERN_ERR,
				"%s: unexpected GTF length (%d) or addr (0x%p)\n",
				__FUNCTION__, out_obj->buffer.length,
				out_obj->buffer.pointer);
		err = -ENOENT;
		goto out;
	}

	*gtf_length = out_obj->buffer.length;
	*gtf_address = (unsigned long)out_obj->buffer.pointer;
	*obj_loc = (unsigned long)out_obj;
	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG, "%s: returning "
			"gtf_length=%d, gtf_address=0x%lx, obj_loc=0x%lx\n",
			__FUNCTION__, *gtf_length, *gtf_address, *obj_loc);
	err = 0;
out:
	return err;
}

/**
 * taskfile_load_raw - send taskfile registers to host controller
 * @dev: target ATA device
 * @gtf: raw ATA taskfile register set (0x1f1 - 0x1f7)
 *
 * Outputs ATA taskfile to standard ATA host controller using MMIO
 * or PIO as indicated by the ATA_FLAG_MMIO flag.
 * Writes the control, feature, nsect, lbal, lbam, and lbah registers.
 * Optionally (ATA_TFLAG_LBA48) writes hob_feature, hob_nsect,
 * hob_lbal, hob_lbam, and hob_lbah.
 *
 * This function waits for idle (!BUSY and !DRQ) after writing
 * registers.  If the control register has a new value, this
 * function also waits for idle after writing control and before
 * writing the remaining registers.
 *
 * LOCKING: TBD:
 * Inherited from caller.
 */
static void taskfile_load_raw(struct ata_device *dev,
			      const struct taskfile_array *gtf)
{
	struct ata_port *ap = dev->ap;
	struct ata_taskfile tf;
	unsigned int err;

	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG, "%s: (0x1f1-1f7): hex: "
			"%02x %02x %02x %02x %02x %02x %02x\n",
			__FUNCTION__,
			gtf->tfa[0], gtf->tfa[1], gtf->tfa[2],
			gtf->tfa[3], gtf->tfa[4], gtf->tfa[5], gtf->tfa[6]);

	if ((gtf->tfa[0] == 0) && (gtf->tfa[1] == 0) && (gtf->tfa[2] == 0)
	    && (gtf->tfa[3] == 0) && (gtf->tfa[4] == 0) && (gtf->tfa[5] == 0)
	    && (gtf->tfa[6] == 0))
		return;

	ata_tf_init(dev, &tf);

	/* convert gtf to tf */
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE; /* TBD */
	tf.protocol = ATA_PROT_NODATA;
	tf.feature = gtf->tfa[0];	/* 0x1f1 */
	tf.nsect   = gtf->tfa[1];	/* 0x1f2 */
	tf.lbal    = gtf->tfa[2];	/* 0x1f3 */
	tf.lbam    = gtf->tfa[3];	/* 0x1f4 */
	tf.lbah    = gtf->tfa[4];	/* 0x1f5 */
	tf.device  = gtf->tfa[5];	/* 0x1f6 */
	tf.command = gtf->tfa[6];	/* 0x1f7 */

	err = ata_exec_internal(dev, &tf, NULL, DMA_NONE, NULL, 0);
	if (err && ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_ERR,
			"%s: ata_exec_internal failed: %u\n",
			__FUNCTION__, err);
}

/**
 * do_drive_set_taskfiles - write the drive taskfile settings from _GTF
 * @dev: target ATA device
 * @gtf_length: total number of bytes of _GTF taskfiles
 * @gtf_address: location of _GTF taskfile arrays
 *
 * This applies to both PATA and SATA drives.
 *
 * Write {gtf_address, length gtf_length} in groups of
 * REGS_PER_GTF bytes.
 */
static int do_drive_set_taskfiles(struct ata_device *dev,
				  unsigned int gtf_length,
				  unsigned long gtf_address)
{
	struct ata_port *ap = dev->ap;
	int err = -ENODEV;
	int gtf_count = gtf_length / REGS_PER_GTF;
	int ix;
	struct taskfile_array	*gtf;

	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG, "%s: ENTER: port#: %d\n",
			       __FUNCTION__, ap->port_no);

	if (!(ap->flags & ATA_FLAG_ACPI_SATA))
		return 0;

	if (!ata_dev_enabled(dev) || (ap->flags & ATA_FLAG_DISABLED))
		goto out;
	if (!gtf_count)		/* shouldn't be here */
		goto out;

	if (gtf_length % REGS_PER_GTF) {
		if (ata_msg_drv(ap))
			ata_dev_printk(dev, KERN_ERR,
				"%s: unexpected GTF length (%d)\n",
				__FUNCTION__, gtf_length);
		goto out;
	}

	for (ix = 0; ix < gtf_count; ix++) {
		gtf = (struct taskfile_array *)
			(gtf_address + ix * REGS_PER_GTF);

		/* send all TaskFile registers (0x1f1-0x1f7) *in*that*order* */
		taskfile_load_raw(dev, gtf);
	}

	err = 0;
out:
	return err;
}

/**
 * ata_acpi_exec_tfs - get then write drive taskfile settings
 * @ap: the ata_port for the drive
 *
 * This applies to both PATA and SATA drives.
 */
int ata_acpi_exec_tfs(struct ata_port *ap)
{
	int ix;
	int ret = 0;
	unsigned int gtf_length;
	unsigned long gtf_address;
	unsigned long obj_loc;

	/*
	 * TBD - implement PATA support.  For now,
	 * we should not run GTF on PATA devices since some
	 * PATA require execution of GTM/STM before GTF.
	 */
	if (!(ap->flags & ATA_FLAG_ACPI_SATA))
		return 0;

	for (ix = 0; ix < ATA_MAX_DEVICES; ix++) {
		struct ata_device *dev = &ap->device[ix];

		if (!ata_dev_enabled(dev))
			continue;

		ret = do_drive_get_GTF(dev, &gtf_length, &gtf_address,
				       &obj_loc);
		if (ret < 0) {
			if (ata_msg_probe(ap))
				ata_port_printk(ap, KERN_DEBUG,
					"%s: get_GTF error (%d)\n",
					__FUNCTION__, ret);
			break;
		}

		ret = do_drive_set_taskfiles(dev, gtf_length, gtf_address);
		kfree((void *)obj_loc);
		if (ret < 0) {
			if (ata_msg_probe(ap))
				ata_port_printk(ap, KERN_DEBUG,
					"%s: set_taskfiles error (%d)\n",
					__FUNCTION__, ret);
			break;
		}
	}

	return ret;
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
 */
int ata_acpi_push_id(struct ata_device *dev)
{
	struct ata_port *ap = dev->ap;
	int err;
	acpi_status status;
	struct acpi_object_list input;
	union acpi_object in_params[1];

	if (!dev->acpi_handle)
		return 0;

	if (ata_msg_probe(ap))
		ata_dev_printk(dev, KERN_DEBUG, "%s: ix = %d, port#: %d\n",
			       __FUNCTION__, dev->devno, ap->port_no);

	/* Don't continue if not a SATA device. */
	if (!(ap->flags & ATA_FLAG_ACPI_SATA)) {
		if (ata_msg_probe(ap))
			ata_dev_printk(dev, KERN_DEBUG,
				"%s: Not a SATA device\n", __FUNCTION__);
		goto out;
	}

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
	status = acpi_evaluate_object(dev->acpi_handle, "_SDD", &input, NULL);
	swap_buf_le16(dev->id, ATA_ID_WORDS);

	err = ACPI_FAILURE(status) ? -EIO : 0;
	if (err < 0) {
		if (ata_msg_probe(ap))
			ata_dev_printk(dev, KERN_DEBUG,
				       "%s _SDD error: status = 0x%x\n",
				       __FUNCTION__, status);
	}

	/* always return success */
out:
	return 0;
}


