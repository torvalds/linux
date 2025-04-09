// SPDX-License-Identifier: GPL-2.0
#include <linux/libata.h>
#include <linux/cdrom.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <linux/pm_qos.h>
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
	bool			powered_off; /* ODD is powered off
					      *	during suspend */
};

static int eject_tray(struct ata_device *dev)
{
	struct ata_taskfile tf;
	static const char cdb[ATAPI_CDB_LEN] = {  GPCMD_START_STOP_UNIT,
		0, 0, 0,
		0x02,     /* LoEj */
		0, 0, 0, 0, 0, 0, 0,
	};

	ata_tf_init(dev, &tf);
	tf.flags = ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.command = ATA_CMD_PACKET;
	tf.protocol = ATAPI_PROT_NODATA;

	return ata_exec_internal(dev, &tf, cdb, DMA_NONE, NULL, 0, 0);
}

/* Per the spec, only slot type and drawer type ODD can be supported */
static enum odd_mech_type zpodd_get_mech_type(struct ata_device *dev)
{
	char *buf;
	unsigned int ret;
	struct rm_feature_desc *desc;
	struct ata_taskfile tf;
	static const char cdb[ATAPI_CDB_LEN] = {  GPCMD_GET_CONFIGURATION,
			2,      /* only 1 feature descriptor requested */
			0, 3,   /* 3, removable medium feature */
			0, 0, 0,/* reserved */
			0, 16,
			0, 0, 0,
	};

	buf = kzalloc(16, GFP_KERNEL);
	if (!buf)
		return ODD_MECH_TYPE_UNSUPPORTED;
	desc = (void *)(buf + 8);

	ata_tf_init(dev, &tf);
	tf.flags = ATA_TFLAG_ISADDR | ATA_TFLAG_DEVICE;
	tf.command = ATA_CMD_PACKET;
	tf.protocol = ATAPI_PROT_PIO;
	tf.lbam = 16;

	ret = ata_exec_internal(dev, &tf, cdb, DMA_FROM_DEVICE,
				buf, 16, 0);
	if (ret) {
		kfree(buf);
		return ODD_MECH_TYPE_UNSUPPORTED;
	}

	if (be16_to_cpu(desc->feature_code) != 3) {
		kfree(buf);
		return ODD_MECH_TYPE_UNSUPPORTED;
	}

	if (desc->mech_type == 0 && desc->load == 0 && desc->eject == 1) {
		kfree(buf);
		return ODD_MECH_TYPE_SLOT;
	} else if (desc->mech_type == 1 && desc->load == 0 &&
		   desc->eject == 1) {
		kfree(buf);
		return ODD_MECH_TYPE_DRAWER;
	} else {
		kfree(buf);
		return ODD_MECH_TYPE_UNSUPPORTED;
	}
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

	sense_buf = dev->sector_buf;
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
		zpodd->zp_ready = false;
		return;
	}

	if (!zpodd->zp_sampled) {
		zpodd->zp_sampled = true;
		zpodd->last_ready = jiffies;
		return;
	}

	expires = zpodd->last_ready + secs_to_jiffies(zpodd_poweroff_delay);
	if (time_before(jiffies, expires))
		return;

	zpodd->zp_ready = true;
}

bool zpodd_zpready(struct ata_device *dev)
{
	struct zpodd *zpodd = dev->zpodd;
	return zpodd->zp_ready;
}

/*
 * Enable runtime wake capability through ACPI and set the powered_off flag,
 * this flag will be used during resume to decide what operations are needed
 * to take.
 *
 * Also, media poll needs to be silenced, so that it doesn't bring the ODD
 * back to full power state every few seconds.
 */
void zpodd_enable_run_wake(struct ata_device *dev)
{
	struct zpodd *zpodd = dev->zpodd;

	sdev_disable_disk_events(dev->sdev);

	zpodd->powered_off = true;
	acpi_pm_set_device_wakeup(&dev->tdev, true);
}

/* Disable runtime wake capability if it is enabled */
void zpodd_disable_run_wake(struct ata_device *dev)
{
	struct zpodd *zpodd = dev->zpodd;

	if (zpodd->powered_off)
		acpi_pm_set_device_wakeup(&dev->tdev, false);
}

/*
 * Post power on processing after the ODD has been recovered. If the
 * ODD wasn't powered off during suspend, it doesn't do anything.
 *
 * For drawer type ODD, if it is powered on due to user pressed the
 * eject button, the tray needs to be ejected. This can only be done
 * after the ODD has been recovered, i.e. link is initialized and
 * device is able to process NON_DATA PIO command, as eject needs to
 * send command for the ODD to process.
 *
 * The from_notify flag set in wake notification handler function
 * zpodd_wake_dev represents if power on is due to user's action.
 *
 * For both types of ODD, several fields need to be reset.
 */
void zpodd_post_poweron(struct ata_device *dev)
{
	struct zpodd *zpodd = dev->zpodd;

	if (!zpodd->powered_off)
		return;

	zpodd->powered_off = false;

	if (zpodd->from_notify) {
		zpodd->from_notify = false;
		if (zpodd->mech_type == ODD_MECH_TYPE_DRAWER)
			eject_tray(dev);
	}

	zpodd->zp_sampled = false;
	zpodd->zp_ready = false;

	sdev_enable_disk_events(dev->sdev);
}

static void zpodd_wake_dev(acpi_handle handle, u32 event, void *context)
{
	struct ata_device *ata_dev = context;
	struct zpodd *zpodd = ata_dev->zpodd;
	struct device *dev = &ata_dev->sdev->sdev_gendev;

	if (event == ACPI_NOTIFY_DEVICE_WAKE && pm_runtime_suspended(dev)) {
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
	acpi_handle handle = ata_dev_acpi_handle(dev);
	acpi_remove_notify_handler(handle, ACPI_SYSTEM_NOTIFY, zpodd_wake_dev);
}

void zpodd_init(struct ata_device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(&dev->tdev);
	enum odd_mech_type mech_type;
	struct zpodd *zpodd;

	if (dev->zpodd || !adev || !acpi_device_can_poweroff(adev))
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
	dev_pm_qos_expose_flags(&dev->tdev, 0);
}

void zpodd_exit(struct ata_device *dev)
{
	ata_acpi_remove_pm_notifier(dev);
	kfree(dev->zpodd);
	dev->zpodd = NULL;
}
