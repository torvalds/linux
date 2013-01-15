#include <linux/libata.h>
#include <linux/cdrom.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <scsi/scsi_device.h>

#include "libata.h"

static int zpodd_poweroff_delay = 30; /* 30 seconds for power off delay */
module_param(zpodd_poweroff_delay, int, 0644);
MODULE_PARM_DESC(zpodd_poweroff_delay, "Poweroff delay for ZPODD in seconds");

enum odd_mech_type {
	ODD_MECH_TYPE_SLOT,
	ODD_MECH_TYPE_DRAWER,
	ODD_MECH_TYPE_UNSUPPORTED,
};

struct zpodd {
	enum odd_mech_type	mech_type; /* init during probe, RO afterwards */
	struct ata_device	*dev;

	/* The following fields are synchronized by PM core. */
	bool			from_notify; /* resumed as a result of
					      * acpi wake notification */
	bool			zp_ready; /* ZP ready state */
	unsigned long		last_ready; /* last ZP ready timestamp */
	bool			zp_sampled; /* ZP ready state sampled */
};

/* Per the spec, only slot type and drawer type ODD can be supported */
static enum odd_mech_type zpodd_get_mech_type(struct ata_device *dev)
{
	char buf[16];
	unsigned int ret;
	struct rm_feature_desc *desc = (void *)(buf + 8);
	struct ata_taskfile tf = {};

	char cdb[] = {  GPCMD_GET_CONFIGURATION,
			2,      /* only 1 feature descriptor requested */
			0, 3,   /* 3, removable medium feature */
			0, 0, 0,/* reserved */
			0, sizeof(buf),
			0, 0, 0,
	};

	tf.flags = ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.command = ATA_CMD_PACKET;
	tf.protocol = ATAPI_PROT_PIO;
	tf.lbam = sizeof(buf);

	ret = ata_exec_internal(dev, &tf, cdb, DMA_FROM_DEVICE,
				buf, sizeof(buf), 0);
	if (ret)
		return ODD_MECH_TYPE_UNSUPPORTED;

	if (be16_to_cpu(desc->feature_code) != 3)
		return ODD_MECH_TYPE_UNSUPPORTED;

	if (desc->mech_type == 0 && desc->load == 0 && desc->eject == 1)
		return ODD_MECH_TYPE_SLOT;
	else if (desc->mech_type == 1 && desc->load == 0 && desc->eject == 1)
		return ODD_MECH_TYPE_DRAWER;
	else
		return ODD_MECH_TYPE_UNSUPPORTED;
}

static bool odd_can_poweroff(struct ata_device *ata_dev)
{
	acpi_handle handle;
	acpi_status status;
	struct acpi_device *acpi_dev;

	handle = ata_dev_acpi_handle(ata_dev);
	if (!handle)
		return false;

	status = acpi_bus_get_device(handle, &acpi_dev);
	if (ACPI_FAILURE(status))
		return false;

	return acpi_device_can_poweroff(acpi_dev);
}

/* Test if ODD is zero power ready by sense code */
static bool zpready(struct ata_device *dev)
{
	u8 sense_key, *sense_buf;
	unsigned int ret, asc, ascq, add_len;
	struct zpodd *zpodd = dev->zpodd;

	ret = atapi_eh_tur(dev, &sense_key);

	if (!ret || sense_key != NOT_READY)
		return false;

	sense_buf = dev->link->ap->sector_buf;
	ret = atapi_eh_request_sense(dev, sense_buf, sense_key);
	if (ret)
		return false;

	/* sense valid */
	if ((sense_buf[0] & 0x7f) != 0x70)
		return false;

	add_len = sense_buf[7];
	/* has asc and ascq */
	if (add_len < 6)
		return false;

	asc = sense_buf[12];
	ascq = sense_buf[13];

	if (zpodd->mech_type == ODD_MECH_TYPE_SLOT)
		/* no media inside */
		return asc == 0x3a;
	else
		/* no media inside and door closed */
		return asc == 0x3a && ascq == 0x01;
}

/*
 * Update the zpodd->zp_ready field. This field will only be set
 * if the ODD has stayed in ZP ready state for zpodd_poweroff_delay
 * time, and will be used to decide if power off is allowed. If it
 * is set, it will be cleared during resume from powered off state.
 */
void zpodd_on_suspend(struct ata_device *dev)
{
	struct zpodd *zpodd = dev->zpodd;
	unsigned long expires;

	if (!zpready(dev)) {
		zpodd->zp_sampled = false;
		return;
	}

	if (!zpodd->zp_sampled) {
		zpodd->zp_sampled = true;
		zpodd->last_ready = jiffies;
		return;
	}

	expires = zpodd->last_ready +
		  msecs_to_jiffies(zpodd_poweroff_delay * 1000);
	if (time_before(jiffies, expires))
		return;

	zpodd->zp_ready = true;
}

static void zpodd_wake_dev(acpi_handle handle, u32 event, void *context)
{
	struct ata_device *ata_dev = context;
	struct zpodd *zpodd = ata_dev->zpodd;
	struct device *dev = &ata_dev->sdev->sdev_gendev;

	if (event == ACPI_NOTIFY_DEVICE_WAKE && ata_dev &&
			pm_runtime_suspended(dev)) {
		zpodd->from_notify = true;
		pm_runtime_resume(dev);
	}
}

static void ata_acpi_add_pm_notifier(struct ata_device *dev)
{
	acpi_handle handle = ata_dev_acpi_handle(dev);
	acpi_install_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
				    zpodd_wake_dev, dev);
}

static void ata_acpi_remove_pm_notifier(struct ata_device *dev)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(&dev->sdev->sdev_gendev);
	acpi_remove_notify_handler(handle, ACPI_SYSTEM_NOTIFY, zpodd_wake_dev);
}

void zpodd_init(struct ata_device *dev)
{
	enum odd_mech_type mech_type;
	struct zpodd *zpodd;

	if (dev->zpodd)
		return;

	if (!odd_can_poweroff(dev))
		return;

	mech_type = zpodd_get_mech_type(dev);
	if (mech_type == ODD_MECH_TYPE_UNSUPPORTED)
		return;

	zpodd = kzalloc(sizeof(struct zpodd), GFP_KERNEL);
	if (!zpodd)
		return;

	zpodd->mech_type = mech_type;

	ata_acpi_add_pm_notifier(dev);
	zpodd->dev = dev;
	dev->zpodd = zpodd;
}

void zpodd_exit(struct ata_device *dev)
{
	ata_acpi_remove_pm_notifier(dev);
	kfree(dev->zpodd);
	dev->zpodd = NULL;
}
