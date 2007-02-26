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

#define SATA_ROOT_PORT(x)	(((x) >> 16) & 0xffff)
#define SATA_PORT_NUMBER(x)	((x) & 0xffff)	/* or NO_PORT_MULT */
#define NO_PORT_MULT		0xffff
#define SATA_ADR_RSVD		0xffffffff

#define REGS_PER_GTF		7
struct taskfile_array {
	u8	tfa[REGS_PER_GTF];	/* regs. 0x1f1 - 0x1f7 */
};


/**
 * sata_get_dev_handle - finds acpi_handle and PCI device.function
 * @dev: device to locate
 * @handle: returned acpi_handle for @dev
 * @pcidevfn: return PCI device.func for @dev
 *
 * This function is somewhat SATA-specific.  Or at least the
 * PATA & SATA versions of this function are different,
 * so it's not entirely generic code.
 *
 * Returns 0 on success, <0 on error.
 */
static int sata_get_dev_handle(struct device *dev, acpi_handle *handle,
					acpi_integer *pcidevfn)
{
	struct pci_dev	*pci_dev;
	acpi_integer	addr;

	pci_dev = to_pci_dev(dev);	/* NOTE: PCI-specific */
	/* Please refer to the ACPI spec for the syntax of _ADR. */
	addr = (PCI_SLOT(pci_dev->devfn) << 16) | PCI_FUNC(pci_dev->devfn);
	*pcidevfn = addr;
	*handle = acpi_get_child(DEVICE_ACPI_HANDLE(dev->parent), addr);
	if (!*handle)
		return -ENODEV;
	return 0;
}

/**
 * pata_get_dev_handle - finds acpi_handle and PCI device.function
 * @dev: device to locate
 * @handle: returned acpi_handle for @dev
 * @pcidevfn: return PCI device.func for @dev
 *
 * The PATA and SATA versions of this function are different.
 *
 * Returns 0 on success, <0 on error.
 */
static int pata_get_dev_handle(struct device *dev, acpi_handle *handle,
				acpi_integer *pcidevfn)
{
	unsigned int bus, devnum, func;
	acpi_integer addr;
	acpi_handle dev_handle, parent_handle;
	struct acpi_buffer buffer = {.length = ACPI_ALLOCATE_BUFFER,
					.pointer = NULL};
	acpi_status status;
	struct acpi_device_info	*dinfo = NULL;
	int ret = -ENODEV;
	struct pci_dev *pdev = to_pci_dev(dev);

	bus = pdev->bus->number;
	devnum = PCI_SLOT(pdev->devfn);
	func = PCI_FUNC(pdev->devfn);

	dev_handle = DEVICE_ACPI_HANDLE(dev);
	parent_handle = DEVICE_ACPI_HANDLE(dev->parent);

	status = acpi_get_object_info(parent_handle, &buffer);
	if (ACPI_FAILURE(status))
		goto err;

	dinfo = buffer.pointer;
	if (dinfo && (dinfo->valid & ACPI_VALID_ADR) &&
	    dinfo->address == bus) {
		/* ACPI spec for _ADR for PCI bus: */
		addr = (acpi_integer)(devnum << 16 | func);
		*pcidevfn = addr;
		*handle = dev_handle;
	} else {
		goto err;
	}

	if (!*handle)
		goto err;
	ret = 0;
err:
	kfree(dinfo);
	return ret;
}

struct walk_info {		/* can be trimmed some */
	struct device	*dev;
	struct acpi_device *adev;
	acpi_handle	handle;
	acpi_integer	pcidevfn;
	unsigned int	drivenum;
	acpi_handle	obj_handle;
	struct ata_port *ataport;
	struct ata_device *atadev;
	u32		sata_adr;
	int		status;
	char		basepath[ACPI_PATHNAME_MAX];
	int		basepath_len;
};

static acpi_status get_devices(acpi_handle handle,
				u32 level, void *context, void **return_value)
{
	acpi_status		status;
	struct walk_info	*winfo = context;
	struct acpi_buffer	namebuf = {ACPI_ALLOCATE_BUFFER, NULL};
	char			*pathname;
	struct acpi_buffer	buffer;
	struct acpi_device_info	*dinfo;

	status = acpi_get_name(handle, ACPI_FULL_PATHNAME, &namebuf);
	if (status)
		goto ret;
	pathname = namebuf.pointer;

	buffer.length = ACPI_ALLOCATE_BUFFER;
	buffer.pointer = NULL;
	status = acpi_get_object_info(handle, &buffer);
	if (ACPI_FAILURE(status))
		goto out2;

	dinfo = buffer.pointer;

	/* find full device path name for pcidevfn */
	if (dinfo && (dinfo->valid & ACPI_VALID_ADR) &&
	    dinfo->address == winfo->pcidevfn) {
		if (ata_msg_probe(winfo->ataport))
			ata_dev_printk(winfo->atadev, KERN_DEBUG,
				":%s: matches pcidevfn (0x%llx)\n",
				pathname, winfo->pcidevfn);
		strlcpy(winfo->basepath, pathname,
			sizeof(winfo->basepath));
		winfo->basepath_len = strlen(pathname);
		goto out;
	}

	/* if basepath is not yet known, ignore this object */
	if (!winfo->basepath_len)
		goto out;

	/* if this object is in scope of basepath, maybe use it */
	if (strncmp(pathname, winfo->basepath,
	    winfo->basepath_len) == 0) {
		if (!(dinfo->valid & ACPI_VALID_ADR))
			goto out;
		if (ata_msg_probe(winfo->ataport))
			ata_dev_printk(winfo->atadev, KERN_DEBUG,
				"GOT ONE: (%s) root_port = 0x%llx,"
				" port_num = 0x%llx\n", pathname,
				SATA_ROOT_PORT(dinfo->address),
				SATA_PORT_NUMBER(dinfo->address));
		/* heuristics: */
		if (SATA_PORT_NUMBER(dinfo->address) != NO_PORT_MULT)
			if (ata_msg_probe(winfo->ataport))
				ata_dev_printk(winfo->atadev,
					KERN_DEBUG, "warning: don't"
					" know how to handle SATA port"
					" multiplier\n");
		if (SATA_ROOT_PORT(dinfo->address) ==
			winfo->ataport->port_no &&
		    SATA_PORT_NUMBER(dinfo->address) == NO_PORT_MULT) {
			if (ata_msg_probe(winfo->ataport))
				ata_dev_printk(winfo->atadev,
					KERN_DEBUG,
					"THIS ^^^^^ is the requested"
					" SATA drive (handle = 0x%p)\n",
					handle);
			winfo->sata_adr = dinfo->address;
			winfo->obj_handle = handle;
		}
	}
out:
	kfree(dinfo);
out2:
	kfree(pathname);

ret:
	return status;
}

/* Get the SATA drive _ADR object. */
static int get_sata_adr(struct device *dev, acpi_handle handle,
			acpi_integer pcidevfn, unsigned int drive,
			struct ata_port *ap,
			struct ata_device *atadev, u32 *dev_adr)
{
	acpi_status	status;
	struct walk_info *winfo;
	int		err = -ENOMEM;

	winfo = kzalloc(sizeof(struct walk_info), GFP_KERNEL);
	if (!winfo)
		goto out;

	winfo->dev = dev;
	winfo->atadev = atadev;
	winfo->ataport = ap;
	if (acpi_bus_get_device(handle, &winfo->adev) < 0)
		if (ata_msg_probe(ap))
			ata_dev_printk(winfo->atadev, KERN_DEBUG,
				"acpi_bus_get_device failed\n");
	winfo->handle = handle;
	winfo->pcidevfn = pcidevfn;
	winfo->drivenum = drive;

	status = acpi_get_devices(NULL, get_devices, winfo, NULL);
	if (ACPI_FAILURE(status)) {
		if (ata_msg_probe(ap))
			ata_dev_printk(winfo->atadev, KERN_DEBUG,
				"%s: acpi_get_devices failed\n",
				__FUNCTION__);
		err = -ENODEV;
	} else {
		*dev_adr = winfo->sata_adr;
		atadev->obj_handle = winfo->obj_handle;
		err = 0;
	}
	kfree(winfo);
out:
	return err;
}

/**
 * do_drive_get_GTF - get the drive bootup default taskfile settings
 * @ap: the ata_port for the drive
 * @ix: target ata_device (drive) index
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
static int do_drive_get_GTF(struct ata_port *ap, int ix,
			unsigned int *gtf_length, unsigned long *gtf_address,
			unsigned long *obj_loc)
{
	acpi_status			status;
	acpi_handle			dev_handle = NULL;
	acpi_handle			chan_handle, drive_handle;
	acpi_integer			pcidevfn = 0;
	u32				dev_adr;
	struct acpi_buffer		output;
	union acpi_object 		*out_obj;
	struct device			*dev = ap->host->dev;
	struct ata_device		*atadev = &ap->device[ix];
	int				err = -ENODEV;

	*gtf_length = 0;
	*gtf_address = 0UL;
	*obj_loc = 0UL;

	if (noacpi)
		return 0;

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG, "%s: ENTER: port#: %d\n",
			       __FUNCTION__, ap->port_no);

	if (!ata_dev_enabled(atadev) || (ap->flags & ATA_FLAG_DISABLED)) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG, "%s: ERR: "
				"ata_dev_present: %d, PORT_DISABLED: %lu\n",
				__FUNCTION__, ata_dev_enabled(atadev),
				ap->flags & ATA_FLAG_DISABLED);
		goto out;
	}

	/* Don't continue if device has no _ADR method.
	 * _GTF is intended for known motherboard devices. */
	if (!(ap->cbl == ATA_CBL_SATA)) {
		err = pata_get_dev_handle(dev, &dev_handle, &pcidevfn);
		if (err < 0) {
			if (ata_msg_probe(ap))
				ata_dev_printk(atadev, KERN_DEBUG,
					"%s: pata_get_dev_handle failed (%d)\n",
					__FUNCTION__, err);
			goto out;
		}
	} else {
		err = sata_get_dev_handle(dev, &dev_handle, &pcidevfn);
		if (err < 0) {
			if (ata_msg_probe(ap))
				ata_dev_printk(atadev, KERN_DEBUG,
					"%s: sata_get_dev_handle failed (%d\n",
					__FUNCTION__, err);
			goto out;
		}
	}

	/* Get this drive's _ADR info. if not already known. */
	if (!atadev->obj_handle) {
		if (!(ap->cbl == ATA_CBL_SATA)) {
			/* get child objects of dev_handle == channel objects,
	 		 * + _their_ children == drive objects */
			/* channel is ap->port_no */
			chan_handle = acpi_get_child(dev_handle,
						ap->port_no);
			if (ata_msg_probe(ap))
				ata_dev_printk(atadev, KERN_DEBUG,
					"%s: chan adr=%d: chan_handle=0x%p\n",
					__FUNCTION__, ap->port_no,
					chan_handle);
			if (!chan_handle) {
				err = -ENODEV;
				goto out;
			}
			/* TBD: could also check ACPI object VALID bits */
			drive_handle = acpi_get_child(chan_handle, ix);
			if (!drive_handle) {
				err = -ENODEV;
				goto out;
			}
			dev_adr = ix;
			atadev->obj_handle = drive_handle;
		} else {	/* for SATA mode */
			dev_adr = SATA_ADR_RSVD;
			err = get_sata_adr(dev, dev_handle, pcidevfn, 0,
					ap, atadev, &dev_adr);
		}
		if (err < 0 || dev_adr == SATA_ADR_RSVD ||
		    !atadev->obj_handle) {
			if (ata_msg_probe(ap))
				ata_dev_printk(atadev, KERN_DEBUG,
					"%s: get_sata/pata_adr failed: "
					"err=%d, dev_adr=%u, obj_handle=0x%p\n",
					__FUNCTION__, err, dev_adr,
					atadev->obj_handle);
			goto out;
		}
	}

	/* Setting up output buffer */
	output.length = ACPI_ALLOCATE_BUFFER;
	output.pointer = NULL;	/* ACPI-CA sets this; save/free it later */

	/* _GTF has no input parameters */
	err = -EIO;
	status = acpi_evaluate_object(atadev->obj_handle, "_GTF",
					NULL, &output);
	if (ACPI_FAILURE(status)) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				"%s: Run _GTF error: status = 0x%x\n",
				__FUNCTION__, status);
		goto out;
	}

	if (!output.length || !output.pointer) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG, "%s: Run _GTF: "
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
			ata_dev_printk(atadev, KERN_DEBUG, "%s: Run _GTF: "
				"error: expected object type of "
				" ACPI_TYPE_BUFFER, got 0x%x\n",
				__FUNCTION__, out_obj->type);
		err = -ENOENT;
		goto out;
	}

	if (!out_obj->buffer.length || !out_obj->buffer.pointer ||
	    out_obj->buffer.length % REGS_PER_GTF) {
		if (ata_msg_drv(ap))
			ata_dev_printk(atadev, KERN_ERR,
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
		ata_dev_printk(atadev, KERN_DEBUG, "%s: returning "
			"gtf_length=%d, gtf_address=0x%lx, obj_loc=0x%lx\n",
			__FUNCTION__, *gtf_length, *gtf_address, *obj_loc);
	err = 0;
out:
	return err;
}

/**
 * taskfile_load_raw - send taskfile registers to host controller
 * @ap: Port to which output is sent
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
static void taskfile_load_raw(struct ata_port *ap,
				struct ata_device *atadev,
				const struct taskfile_array *gtf)
{
	struct ata_taskfile tf;
	unsigned int err;

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG, "%s: (0x1f1-1f7): hex: "
			"%02x %02x %02x %02x %02x %02x %02x\n",
			__FUNCTION__,
			gtf->tfa[0], gtf->tfa[1], gtf->tfa[2],
			gtf->tfa[3], gtf->tfa[4], gtf->tfa[5], gtf->tfa[6]);

	if ((gtf->tfa[0] == 0) && (gtf->tfa[1] == 0) && (gtf->tfa[2] == 0)
	    && (gtf->tfa[3] == 0) && (gtf->tfa[4] == 0) && (gtf->tfa[5] == 0)
	    && (gtf->tfa[6] == 0))
		return;

	ata_tf_init(atadev, &tf);

	/* convert gtf to tf */
	tf.flags |= ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE; /* TBD */
	tf.protocol = atadev->class == ATA_DEV_ATAPI ?
		ATA_PROT_ATAPI_NODATA : ATA_PROT_NODATA;
	tf.feature = gtf->tfa[0];	/* 0x1f1 */
	tf.nsect   = gtf->tfa[1];	/* 0x1f2 */
	tf.lbal    = gtf->tfa[2];	/* 0x1f3 */
	tf.lbam    = gtf->tfa[3];	/* 0x1f4 */
	tf.lbah    = gtf->tfa[4];	/* 0x1f5 */
	tf.device  = gtf->tfa[5];	/* 0x1f6 */
	tf.command = gtf->tfa[6];	/* 0x1f7 */

	err = ata_exec_internal(atadev, &tf, NULL, DMA_NONE, NULL, 0);
	if (err && ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_ERR,
			"%s: ata_exec_internal failed: %u\n",
			__FUNCTION__, err);
}

/**
 * do_drive_set_taskfiles - write the drive taskfile settings from _GTF
 * @ap: the ata_port for the drive
 * @atadev: target ata_device
 * @gtf_length: total number of bytes of _GTF taskfiles
 * @gtf_address: location of _GTF taskfile arrays
 *
 * This applies to both PATA and SATA drives.
 *
 * Write {gtf_address, length gtf_length} in groups of
 * REGS_PER_GTF bytes.
 */
static int do_drive_set_taskfiles(struct ata_port *ap,
		struct ata_device *atadev, unsigned int gtf_length,
		unsigned long gtf_address)
{
	int			err = -ENODEV;
	int			gtf_count = gtf_length / REGS_PER_GTF;
	int			ix;
	struct taskfile_array	*gtf;

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG, "%s: ENTER: port#: %d\n",
			       __FUNCTION__, ap->port_no);

	if (noacpi || !(ap->cbl == ATA_CBL_SATA))
		return 0;

	if (!ata_dev_enabled(atadev) || (ap->flags & ATA_FLAG_DISABLED))
		goto out;
	if (!gtf_count)		/* shouldn't be here */
		goto out;

	if (gtf_length % REGS_PER_GTF) {
		if (ata_msg_drv(ap))
			ata_dev_printk(atadev, KERN_ERR,
				"%s: unexpected GTF length (%d)\n",
				__FUNCTION__, gtf_length);
		goto out;
	}

	for (ix = 0; ix < gtf_count; ix++) {
		gtf = (struct taskfile_array *)
			(gtf_address + ix * REGS_PER_GTF);

		/* send all TaskFile registers (0x1f1-0x1f7) *in*that*order* */
		taskfile_load_raw(ap, atadev, gtf);
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
	int		ix;
	int		ret =0;
	unsigned int	gtf_length;
	unsigned long	gtf_address;
	unsigned long	obj_loc;

	if (noacpi)
		return 0;

	for (ix = 0; ix < ATA_MAX_DEVICES; ix++) {
		if (!ata_dev_enabled(&ap->device[ix]))
			continue;

		ret = do_drive_get_GTF(ap, ix,
				&gtf_length, &gtf_address, &obj_loc);
		if (ret < 0) {
			if (ata_msg_probe(ap))
				ata_port_printk(ap, KERN_DEBUG,
					"%s: get_GTF error (%d)\n",
					__FUNCTION__, ret);
			break;
		}

		ret = do_drive_set_taskfiles(ap, &ap->device[ix],
				gtf_length, gtf_address);
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
 * @ap: the ata_port for the drive
 * @ix: drive index
 *
 * _SDD ACPI object: for SATA mode only
 * Must be after Identify (Packet) Device -- uses its data
 * ATM this function never returns a failure.  It is an optional
 * method and if it fails for whatever reason, we should still
 * just keep going.
 */
int ata_acpi_push_id(struct ata_port *ap, unsigned int ix)
{
	acpi_handle                     handle;
	acpi_integer                    pcidevfn;
	int                             err;
	struct device                   *dev = ap->host->dev;
	struct ata_device               *atadev = &ap->device[ix];
	u32                             dev_adr;
	acpi_status                     status;
	struct acpi_object_list         input;
	union acpi_object               in_params[1];

	if (noacpi)
		return 0;

	if (ata_msg_probe(ap))
		ata_dev_printk(atadev, KERN_DEBUG, "%s: ix = %d, port#: %d\n",
			       __FUNCTION__, ix, ap->port_no);

	/* Don't continue if not a SATA device. */
	if (!(ap->cbl == ATA_CBL_SATA)) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				"%s: Not a SATA device\n", __FUNCTION__);
		goto out;
	}

	/* Don't continue if device has no _ADR method.
	 * _SDD is intended for known motherboard devices. */
	err = sata_get_dev_handle(dev, &handle, &pcidevfn);
	if (err < 0) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				"%s: sata_get_dev_handle failed (%d\n",
				__FUNCTION__, err);
		goto out;
	}

	/* Get this drive's _ADR info, if not already known */
	if (!atadev->obj_handle) {
		dev_adr = SATA_ADR_RSVD;
		err = get_sata_adr(dev, handle, pcidevfn, ix, ap, atadev,
					&dev_adr);
		if (err < 0 || dev_adr == SATA_ADR_RSVD ||
			!atadev->obj_handle) {
			if (ata_msg_probe(ap))
				ata_dev_printk(atadev, KERN_DEBUG,
					"%s: get_sata_adr failed: "
					"err=%d, dev_adr=%u, obj_handle=0x%p\n",
					__FUNCTION__, err, dev_adr,
					atadev->obj_handle);
			goto out;
		}
	}

	/* Give the drive Identify data to the drive via the _SDD method */
	/* _SDD: set up input parameters */
	input.count = 1;
	input.pointer = in_params;
	in_params[0].type = ACPI_TYPE_BUFFER;
	in_params[0].buffer.length = sizeof(atadev->id[0]) * ATA_ID_WORDS;
	in_params[0].buffer.pointer = (u8 *)atadev->id;
	/* Output buffer: _SDD has no output */

	/* It's OK for _SDD to be missing too. */
	swap_buf_le16(atadev->id, ATA_ID_WORDS);
	status = acpi_evaluate_object(atadev->obj_handle, "_SDD", &input, NULL);
	swap_buf_le16(atadev->id, ATA_ID_WORDS);

	err = ACPI_FAILURE(status) ? -EIO : 0;
	if (err < 0) {
		if (ata_msg_probe(ap))
			ata_dev_printk(atadev, KERN_DEBUG,
				       "%s _SDD error: status = 0x%x\n",
				       __FUNCTION__, status);
	}

	/* always return success */
out:
	return 0;
}


