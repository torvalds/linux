/*
 * ide-acpi.c
 * Provides ACPI support for IDE drives.
 *
 * Copyright (C) 2005 Intel Corp.
 * Copyright (C) 2005 Randy Dunlap
 * Copyright (C) 2006 SUSE Linux Products GmbH
 * Copyright (C) 2006 Hannes Reinecke
 */

#include <linux/ata.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <acpi/acpi.h>
#include <linux/ide.h>
#include <linux/pci.h>
#include <linux/dmi.h>

#include <acpi/acpi_bus.h>
#include <acpi/acnames.h>
#include <acpi/acnamesp.h>
#include <acpi/acparser.h>
#include <acpi/acexcep.h>
#include <acpi/acmacros.h>
#include <acpi/actypes.h>

#define REGS_PER_GTF		7
struct taskfile_array {
	u8	tfa[REGS_PER_GTF];	/* regs. 0x1f1 - 0x1f7 */
};

struct GTM_buffer {
	u32	PIO_speed0;
	u32	DMA_speed0;
	u32	PIO_speed1;
	u32	DMA_speed1;
	u32	GTM_flags;
};

struct ide_acpi_drive_link {
	ide_drive_t	*drive;
	acpi_handle	 obj_handle;
	u8		 idbuff[512];
};

struct ide_acpi_hwif_link {
	ide_hwif_t			*hwif;
	acpi_handle			 obj_handle;
	struct GTM_buffer		 gtm;
	struct ide_acpi_drive_link	 master;
	struct ide_acpi_drive_link	 slave;
};

#undef DEBUGGING
/* note: adds function name and KERN_DEBUG */
#ifdef DEBUGGING
#define DEBPRINT(fmt, args...)	\
		printk(KERN_DEBUG "%s: " fmt, __FUNCTION__, ## args)
#else
#define DEBPRINT(fmt, args...)	do {} while (0)
#endif	/* DEBUGGING */

extern int ide_noacpi;
extern int ide_noacpitfs;
extern int ide_noacpionboot;

static bool ide_noacpi_psx;
static int no_acpi_psx(const struct dmi_system_id *id)
{
	ide_noacpi_psx = true;
	printk(KERN_NOTICE"%s detected - disable ACPI _PSx.\n", id->ident);
	return 0;
}

static const struct dmi_system_id ide_acpi_dmi_table[] = {
	/* Bug 9673. */
	/* We should check if this is because ACPI NVS isn't save/restored. */
	{
		.callback = no_acpi_psx,
		.ident    = "HP nx9005",
		.matches  = {
			DMI_MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies Ltd."),
			DMI_MATCH(DMI_BIOS_VERSION, "KAM1.60")
		},
	},

	{ }	/* terminate list */
};

static int ide_acpi_blacklist(void)
{
	static int done;
	if (done)
		return 0;
	done = 1;
	dmi_check_system(ide_acpi_dmi_table);
	return 0;
}

/**
 * ide_get_dev_handle - finds acpi_handle and PCI device.function
 * @dev: device to locate
 * @handle: returned acpi_handle for @dev
 * @pcidevfn: return PCI device.func for @dev
 *
 * Returns the ACPI object handle to the corresponding PCI device.
 *
 * Returns 0 on success, <0 on error.
 */
static int ide_get_dev_handle(struct device *dev, acpi_handle *handle,
			       acpi_integer *pcidevfn)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	unsigned int bus, devnum, func;
	acpi_integer addr;
	acpi_handle dev_handle;
	struct acpi_buffer buffer = {.length = ACPI_ALLOCATE_BUFFER,
					.pointer = NULL};
	acpi_status status;
	struct acpi_device_info	*dinfo = NULL;
	int ret = -ENODEV;

	bus = pdev->bus->number;
	devnum = PCI_SLOT(pdev->devfn);
	func = PCI_FUNC(pdev->devfn);
	/* ACPI _ADR encoding for PCI bus: */
	addr = (acpi_integer)(devnum << 16 | func);

	DEBPRINT("ENTER: pci %02x:%02x.%01x\n", bus, devnum, func);

	dev_handle = DEVICE_ACPI_HANDLE(dev);
	if (!dev_handle) {
		DEBPRINT("no acpi handle for device\n");
		goto err;
	}

	status = acpi_get_object_info(dev_handle, &buffer);
	if (ACPI_FAILURE(status)) {
		DEBPRINT("get_object_info for device failed\n");
		goto err;
	}
	dinfo = buffer.pointer;
	if (dinfo && (dinfo->valid & ACPI_VALID_ADR) &&
	    dinfo->address == addr) {
		*pcidevfn = addr;
		*handle = dev_handle;
	} else {
		DEBPRINT("get_object_info for device has wrong "
			" address: %llu, should be %u\n",
			dinfo ? (unsigned long long)dinfo->address : -1ULL,
			(unsigned int)addr);
		goto err;
	}

	DEBPRINT("for dev=0x%x.%x, addr=0x%llx, *handle=0x%p\n",
		 devnum, func, (unsigned long long)addr, *handle);
	ret = 0;
err:
	kfree(dinfo);
	return ret;
}

/**
 * ide_acpi_hwif_get_handle - Get ACPI object handle for a given hwif
 * @hwif: device to locate
 *
 * Retrieves the object handle for a given hwif.
 *
 * Returns handle on success, 0 on error.
 */
static acpi_handle ide_acpi_hwif_get_handle(ide_hwif_t *hwif)
{
	struct device		*dev = hwif->gendev.parent;
	acpi_handle		dev_handle;
	acpi_integer		pcidevfn;
	acpi_handle		chan_handle;
	int			err;

	DEBPRINT("ENTER: device %s\n", hwif->name);

	if (!dev) {
		DEBPRINT("no PCI device for %s\n", hwif->name);
		return NULL;
	}

	err = ide_get_dev_handle(dev, &dev_handle, &pcidevfn);
	if (err < 0) {
		DEBPRINT("ide_get_dev_handle failed (%d)\n", err);
		return NULL;
	}

	/* get child objects of dev_handle == channel objects,
	 * + _their_ children == drive objects */
	/* channel is hwif->channel */
	chan_handle = acpi_get_child(dev_handle, hwif->channel);
	DEBPRINT("chan adr=%d: handle=0x%p\n",
		 hwif->channel, chan_handle);

	return chan_handle;
}

/**
 * ide_acpi_drive_get_handle - Get ACPI object handle for a given drive
 * @drive: device to locate
 *
 * Retrieves the object handle of a given drive. According to the ACPI
 * spec the drive is a child of the hwif.
 *
 * Returns handle on success, 0 on error.
 */
static acpi_handle ide_acpi_drive_get_handle(ide_drive_t *drive)
{
	ide_hwif_t	*hwif = HWIF(drive);
	int		 port;
	acpi_handle	 drive_handle;

	if (!hwif->acpidata)
		return NULL;

	if (!hwif->acpidata->obj_handle)
		return NULL;

	port = hwif->channel ? drive->dn - 2: drive->dn;

	DEBPRINT("ENTER: %s at channel#: %d port#: %d\n",
		 drive->name, hwif->channel, port);


	/* TBD: could also check ACPI object VALID bits */
	drive_handle = acpi_get_child(hwif->acpidata->obj_handle, port);
	DEBPRINT("drive %s handle 0x%p\n", drive->name, drive_handle);

	return drive_handle;
}

/**
 * do_drive_get_GTF - get the drive bootup default taskfile settings
 * @drive: the drive for which the taskfile settings should be retrieved
 * @gtf_length: number of bytes of _GTF data returned at @gtf_address
 * @gtf_address: buffer containing _GTF taskfile arrays
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
static int do_drive_get_GTF(ide_drive_t *drive,
		     unsigned int *gtf_length, unsigned long *gtf_address,
		     unsigned long *obj_loc)
{
	acpi_status			status;
	struct acpi_buffer		output;
	union acpi_object 		*out_obj;
	ide_hwif_t			*hwif = HWIF(drive);
	struct device			*dev = hwif->gendev.parent;
	int				err = -ENODEV;
	int				port;

	*gtf_length = 0;
	*gtf_address = 0UL;
	*obj_loc = 0UL;

	if (ide_noacpi)
		return 0;

	if (!dev) {
		DEBPRINT("no PCI device for %s\n", hwif->name);
		goto out;
	}

	if (!hwif->acpidata) {
		DEBPRINT("no ACPI data for %s\n", hwif->name);
		goto out;
	}

	port = hwif->channel ? drive->dn - 2: drive->dn;

	if (!drive->acpidata) {
		if (port == 0) {
			drive->acpidata = &hwif->acpidata->master;
			hwif->acpidata->master.drive = drive;
		} else {
			drive->acpidata = &hwif->acpidata->slave;
			hwif->acpidata->slave.drive = drive;
		}
	}

	DEBPRINT("ENTER: %s at %s, port#: %d, hard_port#: %d\n",
		 hwif->name, dev->bus_id, port, hwif->channel);

	if (!drive->present) {
		DEBPRINT("%s drive %d:%d not present\n",
			 hwif->name, hwif->channel, port);
		goto out;
	}

	/* Get this drive's _ADR info. if not already known. */
	if (!drive->acpidata->obj_handle) {
		drive->acpidata->obj_handle = ide_acpi_drive_get_handle(drive);
		if (!drive->acpidata->obj_handle) {
			DEBPRINT("No ACPI object found for %s\n",
				 drive->name);
			goto out;
		}
	}

	/* Setting up output buffer */
	output.length = ACPI_ALLOCATE_BUFFER;
	output.pointer = NULL;	/* ACPI-CA sets this; save/free it later */

	/* _GTF has no input parameters */
	err = -EIO;
	status = acpi_evaluate_object(drive->acpidata->obj_handle, "_GTF",
				      NULL, &output);
	if (ACPI_FAILURE(status)) {
		printk(KERN_DEBUG
		       "%s: Run _GTF error: status = 0x%x\n",
		       __FUNCTION__, status);
		goto out;
	}

	if (!output.length || !output.pointer) {
		DEBPRINT("Run _GTF: "
		       "length or ptr is NULL (0x%llx, 0x%p)\n",
		       (unsigned long long)output.length,
		       output.pointer);
		goto out;
	}

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER) {
		DEBPRINT("Run _GTF: error: "
		       "expected object type of ACPI_TYPE_BUFFER, "
		       "got 0x%x\n", out_obj->type);
		err = -ENOENT;
		kfree(output.pointer);
		goto out;
	}

	if (!out_obj->buffer.length || !out_obj->buffer.pointer ||
	    out_obj->buffer.length % REGS_PER_GTF) {
		printk(KERN_ERR
		       "%s: unexpected GTF length (%d) or addr (0x%p)\n",
		       __FUNCTION__, out_obj->buffer.length,
		       out_obj->buffer.pointer);
		err = -ENOENT;
		kfree(output.pointer);
		goto out;
	}

	*gtf_length = out_obj->buffer.length;
	*gtf_address = (unsigned long)out_obj->buffer.pointer;
	*obj_loc = (unsigned long)out_obj;
	DEBPRINT("returning gtf_length=%d, gtf_address=0x%lx, obj_loc=0x%lx\n",
		 *gtf_length, *gtf_address, *obj_loc);
	err = 0;
out:
	return err;
}

/**
 * taskfile_load_raw - send taskfile registers to drive
 * @drive: drive to which output is sent
 * @gtf: raw ATA taskfile register set (0x1f1 - 0x1f7)
 *
 * Outputs IDE taskfile to the drive.
 */
static int taskfile_load_raw(ide_drive_t *drive,
			      const struct taskfile_array *gtf)
{
	ide_task_t args;
	int err = 0;

	DEBPRINT("(0x1f1-1f7): hex: "
	       "%02x %02x %02x %02x %02x %02x %02x\n",
	       gtf->tfa[0], gtf->tfa[1], gtf->tfa[2],
	       gtf->tfa[3], gtf->tfa[4], gtf->tfa[5], gtf->tfa[6]);

	memset(&args, 0, sizeof(ide_task_t));
	args.command_type = IDE_DRIVE_TASK_NO_DATA;
	args.data_phase   = TASKFILE_NO_DATA;
	args.handler      = &task_no_data_intr;

	/* convert gtf to IDE Taskfile */
	args.tfRegister[1] = gtf->tfa[0];	/* 0x1f1 */
	args.tfRegister[2] = gtf->tfa[1];	/* 0x1f2 */
	args.tfRegister[3] = gtf->tfa[2];	/* 0x1f3 */
	args.tfRegister[4] = gtf->tfa[3];	/* 0x1f4 */
	args.tfRegister[5] = gtf->tfa[4];	/* 0x1f5 */
	args.tfRegister[6] = gtf->tfa[5];	/* 0x1f6 */
	args.tfRegister[7] = gtf->tfa[6];	/* 0x1f7 */

	if (ide_noacpitfs) {
		DEBPRINT("_GTF execution disabled\n");
		return err;
	}

	err = ide_raw_taskfile(drive, &args, NULL);
	if (err)
		printk(KERN_ERR "%s: ide_raw_taskfile failed: %u\n",
		       __FUNCTION__, err);

	return err;
}

/**
 * do_drive_set_taskfiles - write the drive taskfile settings from _GTF
 * @drive: the drive to which the taskfile command should be sent
 * @gtf_length: total number of bytes of _GTF taskfiles
 * @gtf_address: location of _GTF taskfile arrays
 *
 * Write {gtf_address, length gtf_length} in groups of
 * REGS_PER_GTF bytes.
 */
static int do_drive_set_taskfiles(ide_drive_t *drive,
				  unsigned int gtf_length,
				  unsigned long gtf_address)
{
	int			rc = -ENODEV, err;
	int			gtf_count = gtf_length / REGS_PER_GTF;
	int			ix;
	struct taskfile_array	*gtf;

	if (ide_noacpi)
		return 0;

	DEBPRINT("ENTER: %s, hard_port#: %d\n", drive->name, drive->dn);

	if (!drive->present)
		goto out;
	if (!gtf_count)		/* shouldn't be here */
		goto out;

	DEBPRINT("total GTF bytes=%u (0x%x), gtf_count=%d, addr=0x%lx\n",
		 gtf_length, gtf_length, gtf_count, gtf_address);

	if (gtf_length % REGS_PER_GTF) {
		printk(KERN_ERR "%s: unexpected GTF length (%d)\n",
		       __FUNCTION__, gtf_length);
		goto out;
	}

	rc = 0;
	for (ix = 0; ix < gtf_count; ix++) {
		gtf = (struct taskfile_array *)
			(gtf_address + ix * REGS_PER_GTF);

		/* send all TaskFile registers (0x1f1-0x1f7) *in*that*order* */
		err = taskfile_load_raw(drive, gtf);
		if (err)
			rc = err;
	}

out:
	return rc;
}

/**
 * ide_acpi_exec_tfs - get then write drive taskfile settings
 * @drive: the drive for which the taskfile settings should be
 *         written.
 *
 * According to the ACPI spec this should be called after _STM
 * has been evaluated for the interface. Some ACPI vendors interpret
 * that as a hard requirement and modify the taskfile according
 * to the Identify Drive information passed down with _STM.
 * So one should really make sure to call this only after _STM has
 * been executed.
 */
int ide_acpi_exec_tfs(ide_drive_t *drive)
{
	int		ret;
	unsigned int	gtf_length;
	unsigned long	gtf_address;
	unsigned long	obj_loc;

	if (ide_noacpi)
		return 0;

	DEBPRINT("call get_GTF, drive=%s port=%d\n", drive->name, drive->dn);

	ret = do_drive_get_GTF(drive, &gtf_length, &gtf_address, &obj_loc);
	if (ret < 0) {
		DEBPRINT("get_GTF error (%d)\n", ret);
		return ret;
	}

	DEBPRINT("call set_taskfiles, drive=%s\n", drive->name);

	ret = do_drive_set_taskfiles(drive, gtf_length, gtf_address);
	kfree((void *)obj_loc);
	if (ret < 0) {
		DEBPRINT("set_taskfiles error (%d)\n", ret);
	}

	DEBPRINT("ret=%d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(ide_acpi_exec_tfs);

/**
 * ide_acpi_get_timing - get the channel (controller) timings
 * @hwif: target IDE interface (channel)
 *
 * This function executes the _GTM ACPI method for the target channel.
 *
 */
void ide_acpi_get_timing(ide_hwif_t *hwif)
{
	acpi_status		status;
	struct acpi_buffer	output;
	union acpi_object 	*out_obj;

	if (ide_noacpi)
		return;

	DEBPRINT("ENTER:\n");

	if (!hwif->acpidata) {
		DEBPRINT("no ACPI data for %s\n", hwif->name);
		return;
	}

	/* Setting up output buffer for _GTM */
	output.length = ACPI_ALLOCATE_BUFFER;
	output.pointer = NULL;	/* ACPI-CA sets this; save/free it later */

	/* _GTM has no input parameters */
	status = acpi_evaluate_object(hwif->acpidata->obj_handle, "_GTM",
				      NULL, &output);

	DEBPRINT("_GTM status: %d, outptr: 0x%p, outlen: 0x%llx\n",
		 status, output.pointer,
		 (unsigned long long)output.length);

	if (ACPI_FAILURE(status)) {
		DEBPRINT("Run _GTM error: status = 0x%x\n", status);
		return;
	}

	if (!output.length || !output.pointer) {
		DEBPRINT("Run _GTM: length or ptr is NULL (0x%llx, 0x%p)\n",
		       (unsigned long long)output.length,
		       output.pointer);
		kfree(output.pointer);
		return;
	}

	out_obj = output.pointer;
	if (out_obj->type != ACPI_TYPE_BUFFER) {
		kfree(output.pointer);
		DEBPRINT("Run _GTM: error: "
		       "expected object type of ACPI_TYPE_BUFFER, "
		       "got 0x%x\n", out_obj->type);
		return;
	}

	if (!out_obj->buffer.length || !out_obj->buffer.pointer ||
	    out_obj->buffer.length != sizeof(struct GTM_buffer)) {
		kfree(output.pointer);
		printk(KERN_ERR
			"%s: unexpected _GTM length (0x%x)[should be 0x%zx] or "
			"addr (0x%p)\n",
			__FUNCTION__, out_obj->buffer.length,
			sizeof(struct GTM_buffer), out_obj->buffer.pointer);
		return;
	}

	memcpy(&hwif->acpidata->gtm, out_obj->buffer.pointer,
	       sizeof(struct GTM_buffer));

	DEBPRINT("_GTM info: ptr: 0x%p, len: 0x%x, exp.len: 0x%Zx\n",
		 out_obj->buffer.pointer, out_obj->buffer.length,
		 sizeof(struct GTM_buffer));

	DEBPRINT("_GTM fields: 0x%x, 0x%x, 0x%x, 0x%x, 0x%x\n",
		 hwif->acpidata->gtm.PIO_speed0,
		 hwif->acpidata->gtm.DMA_speed0,
		 hwif->acpidata->gtm.PIO_speed1,
		 hwif->acpidata->gtm.DMA_speed1,
		 hwif->acpidata->gtm.GTM_flags);

	kfree(output.pointer);
}
EXPORT_SYMBOL_GPL(ide_acpi_get_timing);

/**
 * ide_acpi_push_timing - set the channel (controller) timings
 * @hwif: target IDE interface (channel)
 *
 * This function executes the _STM ACPI method for the target channel.
 *
 * _STM requires Identify Drive data, which has to passed as an argument.
 * Unfortunately hd_driveid is a mangled version which we can't readily
 * use; hence we'll get the information afresh.
 */
void ide_acpi_push_timing(ide_hwif_t *hwif)
{
	acpi_status		status;
	struct acpi_object_list	input;
	union acpi_object 	in_params[3];
	struct ide_acpi_drive_link	*master = &hwif->acpidata->master;
	struct ide_acpi_drive_link	*slave = &hwif->acpidata->slave;

	if (ide_noacpi)
		return;

	DEBPRINT("ENTER:\n");

	if (!hwif->acpidata) {
		DEBPRINT("no ACPI data for %s\n", hwif->name);
		return;
	}

	/* Give the GTM buffer + drive Identify data to the channel via the
	 * _STM method: */
	/* setup input parameters buffer for _STM */
	input.count = 3;
	input.pointer = in_params;
	in_params[0].type = ACPI_TYPE_BUFFER;
	in_params[0].buffer.length = sizeof(struct GTM_buffer);
	in_params[0].buffer.pointer = (u8 *)&hwif->acpidata->gtm;
	in_params[1].type = ACPI_TYPE_BUFFER;
	in_params[1].buffer.length = sizeof(struct hd_driveid);
	in_params[1].buffer.pointer = (u8 *)&master->idbuff;
	in_params[2].type = ACPI_TYPE_BUFFER;
	in_params[2].buffer.length = sizeof(struct hd_driveid);
	in_params[2].buffer.pointer = (u8 *)&slave->idbuff;
	/* Output buffer: _STM has no output */

	status = acpi_evaluate_object(hwif->acpidata->obj_handle, "_STM",
				      &input, NULL);

	if (ACPI_FAILURE(status)) {
		DEBPRINT("Run _STM error: status = 0x%x\n", status);
	}
	DEBPRINT("_STM status: %d\n", status);
}
EXPORT_SYMBOL_GPL(ide_acpi_push_timing);

/**
 * ide_acpi_set_state - set the channel power state
 * @hwif: target IDE interface
 * @on: state, on/off
 *
 * This function executes the _PS0/_PS3 ACPI method to set the power state.
 * ACPI spec requires _PS0 when IDE power on and _PS3 when power off
 */
void ide_acpi_set_state(ide_hwif_t *hwif, int on)
{
	int unit;

	if (ide_noacpi || ide_noacpi_psx)
		return;

	DEBPRINT("ENTER:\n");

	if (!hwif->acpidata) {
		DEBPRINT("no ACPI data for %s\n", hwif->name);
		return;
	}
	/* channel first and then drives for power on and verse versa for power off */
	if (on)
		acpi_bus_set_power(hwif->acpidata->obj_handle, ACPI_STATE_D0);
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		if (!drive->acpidata->obj_handle)
			drive->acpidata->obj_handle = ide_acpi_drive_get_handle(drive);

		if (drive->acpidata->obj_handle && drive->present) {
			acpi_bus_set_power(drive->acpidata->obj_handle,
				on? ACPI_STATE_D0: ACPI_STATE_D3);
		}
	}
	if (!on)
		acpi_bus_set_power(hwif->acpidata->obj_handle, ACPI_STATE_D3);
}

/**
 * ide_acpi_init - initialize the ACPI link for an IDE interface
 * @hwif: target IDE interface (channel)
 *
 * The ACPI spec is not quite clear when the drive identify buffer
 * should be obtained. Calling IDENTIFY DEVICE during shutdown
 * is not the best of ideas as the drive might already being put to
 * sleep. And obviously we can't call it during resume.
 * So we get the information during startup; but this means that
 * any changes during run-time will be lost after resume.
 */
void ide_acpi_init(ide_hwif_t *hwif)
{
	int unit;
	int			err;
	struct ide_acpi_drive_link	*master;
	struct ide_acpi_drive_link	*slave;

	ide_acpi_blacklist();

	hwif->acpidata = kzalloc(sizeof(struct ide_acpi_hwif_link), GFP_KERNEL);
	if (!hwif->acpidata)
		return;

	hwif->acpidata->obj_handle = ide_acpi_hwif_get_handle(hwif);
	if (!hwif->acpidata->obj_handle) {
		DEBPRINT("no ACPI object for %s found\n", hwif->name);
		kfree(hwif->acpidata);
		hwif->acpidata = NULL;
		return;
	}

	/*
	 * The ACPI spec mandates that we send information
	 * for both drives, regardless whether they are connected
	 * or not.
	 */
	hwif->acpidata->master.drive = &hwif->drives[0];
	hwif->drives[0].acpidata = &hwif->acpidata->master;
	master = &hwif->acpidata->master;

	hwif->acpidata->slave.drive = &hwif->drives[1];
	hwif->drives[1].acpidata = &hwif->acpidata->slave;
	slave = &hwif->acpidata->slave;


	/*
	 * Send IDENTIFY for each drive
	 */
	if (master->drive->present) {
		err = taskfile_lib_get_identify(master->drive, master->idbuff);
		if (err) {
			DEBPRINT("identify device %s failed (%d)\n",
				 master->drive->name, err);
		}
	}

	if (slave->drive->present) {
		err = taskfile_lib_get_identify(slave->drive, slave->idbuff);
		if (err) {
			DEBPRINT("identify device %s failed (%d)\n",
				 slave->drive->name, err);
		}
	}

	if (ide_noacpionboot) {
		DEBPRINT("ACPI methods disabled on boot\n");
		return;
	}

	/* ACPI _PS0 before _STM */
	ide_acpi_set_state(hwif, 1);
	/*
	 * ACPI requires us to call _STM on startup
	 */
	ide_acpi_get_timing(hwif);
	ide_acpi_push_timing(hwif);

	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];

		if (drive->present) {
			/* Execute ACPI startup code */
			ide_acpi_exec_tfs(drive);
		}
	}
}
EXPORT_SYMBOL_GPL(ide_acpi_init);
