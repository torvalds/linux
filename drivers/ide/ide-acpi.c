/*
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

#define REGS_PER_GTF		7

struct GTM_buffer {
	u32	PIO_speed0;
	u32	DMA_speed0;
	u32	PIO_speed1;
	u32	DMA_speed1;
	u32	GTM_flags;
};

struct ide_acpi_drive_link {
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
		printk(KERN_DEBUG "%s: " fmt, __func__, ## args)
#else
#define DEBPRINT(fmt, args...)	do {} while (0)
#endif	/* DEBUGGING */

static int ide_noacpi;
module_param_named(noacpi, ide_noacpi, bool, 0);
MODULE_PARM_DESC(noacpi, "disable IDE ACPI support");

static int ide_acpigtf;
module_param_named(acpigtf, ide_acpigtf, bool, 0);
MODULE_PARM_DESC(acpigtf, "enable IDE ACPI _GTF support");

static int ide_acpionboot;
module_param_named(acpionboot, ide_acpionboot, bool, 0);
MODULE_PARM_DESC(acpionboot, "call IDE ACPI methods on boot");

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

int ide_acpi_init(void)
{
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
	acpi_handle		uninitialized_var(dev_handle);
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
	int				err = -ENODEV;

	*gtf_length = 0;
	*gtf_address = 0UL;
	*obj_loc = 0UL;

	if (!drive->acpidata->obj_handle) {
		DEBPRINT("No ACPI object found for %s\n", drive->name);
		goto out;
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
		       __func__, status);
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
		       __func__, out_obj->buffer.length,
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
	int			rc = 0, err;
	int			gtf_count = gtf_length / REGS_PER_GTF;
	int			ix;

	DEBPRINT("total GTF bytes=%u (0x%x), gtf_count=%d, addr=0x%lx\n",
		 gtf_length, gtf_length, gtf_count, gtf_address);

	/* send all taskfile registers (0x1f1-0x1f7) *in*that*order* */
	for (ix = 0; ix < gtf_count; ix++) {
		u8 *gtf = (u8 *)(gtf_address + ix * REGS_PER_GTF);
		struct ide_cmd cmd;

		DEBPRINT("(0x1f1-1f7): "
			 "hex: %02x %02x %02x %02x %02x %02x %02x\n",
			 gtf[0], gtf[1], gtf[2],
			 gtf[3], gtf[4], gtf[5], gtf[6]);

		if (!ide_acpigtf) {
			DEBPRINT("_GTF execution disabled\n");
			continue;
		}

		/* convert GTF to taskfile */
		memset(&cmd, 0, sizeof(cmd));
		memcpy(&cmd.tf.feature, gtf, REGS_PER_GTF);
		cmd.valid.out.tf = IDE_VALID_OUT_TF | IDE_VALID_DEVICE;
		cmd.valid.in.tf  = IDE_VALID_IN_TF  | IDE_VALID_DEVICE;

		err = ide_no_data_taskfile(drive, &cmd);
		if (err) {
			printk(KERN_ERR "%s: ide_no_data_taskfile failed: %u\n",
					__func__, err);
			rc = err;
		}
	}

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
			__func__, out_obj->buffer.length,
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

/**
 * ide_acpi_push_timing - set the channel (controller) timings
 * @hwif: target IDE interface (channel)
 *
 * This function executes the _STM ACPI method for the target channel.
 *
 * _STM requires Identify Drive data, which has to passed as an argument.
 * Unfortunately drive->id is a mangled version which we can't readily
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
	in_params[1].buffer.length = ATA_ID_WORDS * 2;
	in_params[1].buffer.pointer = (u8 *)&master->idbuff;
	in_params[2].type = ACPI_TYPE_BUFFER;
	in_params[2].buffer.length = ATA_ID_WORDS * 2;
	in_params[2].buffer.pointer = (u8 *)&slave->idbuff;
	/* Output buffer: _STM has no output */

	status = acpi_evaluate_object(hwif->acpidata->obj_handle, "_STM",
				      &input, NULL);

	if (ACPI_FAILURE(status)) {
		DEBPRINT("Run _STM error: status = 0x%x\n", status);
	}
	DEBPRINT("_STM status: %d\n", status);
}

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
	ide_drive_t *drive;
	int i;

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

	ide_port_for_each_present_dev(i, drive, hwif) {
		if (drive->acpidata->obj_handle)
			acpi_bus_set_power(drive->acpidata->obj_handle,
					   on ? ACPI_STATE_D0 : ACPI_STATE_D3);
	}

	if (!on)
		acpi_bus_set_power(hwif->acpidata->obj_handle, ACPI_STATE_D3);
}

/**
 * ide_acpi_init_port - initialize the ACPI link for an IDE interface
 * @hwif: target IDE interface (channel)
 *
 * The ACPI spec is not quite clear when the drive identify buffer
 * should be obtained. Calling IDENTIFY DEVICE during shutdown
 * is not the best of ideas as the drive might already being put to
 * sleep. And obviously we can't call it during resume.
 * So we get the information during startup; but this means that
 * any changes during run-time will be lost after resume.
 */
void ide_acpi_init_port(ide_hwif_t *hwif)
{
	hwif->acpidata = kzalloc(sizeof(struct ide_acpi_hwif_link), GFP_KERNEL);
	if (!hwif->acpidata)
		return;

	hwif->acpidata->obj_handle = ide_acpi_hwif_get_handle(hwif);
	if (!hwif->acpidata->obj_handle) {
		DEBPRINT("no ACPI object for %s found\n", hwif->name);
		kfree(hwif->acpidata);
		hwif->acpidata = NULL;
	}
}

void ide_acpi_port_init_devices(ide_hwif_t *hwif)
{
	ide_drive_t *drive;
	int i, err;

	if (hwif->acpidata == NULL)
		return;

	/*
	 * The ACPI spec mandates that we send information
	 * for both drives, regardless whether they are connected
	 * or not.
	 */
	hwif->devices[0]->acpidata = &hwif->acpidata->master;
	hwif->devices[1]->acpidata = &hwif->acpidata->slave;

	/* get _ADR info for each device */
	ide_port_for_each_present_dev(i, drive, hwif) {
		acpi_handle dev_handle;

		DEBPRINT("ENTER: %s at channel#: %d port#: %d\n",
			 drive->name, hwif->channel, drive->dn & 1);

		/* TBD: could also check ACPI object VALID bits */
		dev_handle = acpi_get_child(hwif->acpidata->obj_handle,
					    drive->dn & 1);

		DEBPRINT("drive %s handle 0x%p\n", drive->name, dev_handle);

		drive->acpidata->obj_handle = dev_handle;
	}

	/* send IDENTIFY for each device */
	ide_port_for_each_present_dev(i, drive, hwif) {
		err = taskfile_lib_get_identify(drive, drive->acpidata->idbuff);
		if (err)
			DEBPRINT("identify device %s failed (%d)\n",
				 drive->name, err);
	}

	if (!ide_acpionboot) {
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

	ide_port_for_each_present_dev(i, drive, hwif) {
		ide_acpi_exec_tfs(drive);
	}
}
