/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC Host driver.
 *
 */
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/pci.h>

#include <linux/mic_common.h>
#include "../common/mic_dev.h"
#include "mic_device.h"
#include "mic_smpt.h"
#include "mic_virtio.h"

/**
 * mic_reset - Reset the MIC device.
 * @mdev: pointer to mic_device instance
 */
static void mic_reset(struct mic_device *mdev)
{
	int i;

#define MIC_RESET_TO (45)

	reinit_completion(&mdev->reset_wait);
	mdev->ops->reset_fw_ready(mdev);
	mdev->ops->reset(mdev);

	for (i = 0; i < MIC_RESET_TO; i++) {
		if (mdev->ops->is_fw_ready(mdev))
			goto done;
		/*
		 * Resets typically take 10s of seconds to complete.
		 * Since an MMIO read is required to check if the
		 * firmware is ready or not, a 1 second delay works nicely.
		 */
		msleep(1000);
	}
	mic_set_state(mdev, MIC_RESET_FAILED);
done:
	complete_all(&mdev->reset_wait);
}

/* Initialize the MIC bootparams */
void mic_bootparam_init(struct mic_device *mdev)
{
	struct mic_bootparam *bootparam = mdev->dp;

	bootparam->magic = cpu_to_le32(MIC_MAGIC);
	bootparam->c2h_shutdown_db = mdev->shutdown_db;
	bootparam->h2c_shutdown_db = -1;
	bootparam->h2c_config_db = -1;
	bootparam->shutdown_status = 0;
	bootparam->shutdown_card = 0;
}

/**
 * mic_start - Start the MIC.
 * @mdev: pointer to mic_device instance
 * @buf: buffer containing boot string including firmware/ramdisk path.
 *
 * This function prepares an MIC for boot and initiates boot.
 * RETURNS: An appropriate -ERRNO error value on error, or zero for success.
 */
int mic_start(struct mic_device *mdev, const char *buf)
{
	int rc;
	mutex_lock(&mdev->mic_mutex);
retry:
	if (MIC_OFFLINE != mdev->state) {
		rc = -EINVAL;
		goto unlock_ret;
	}
	if (!mdev->ops->is_fw_ready(mdev)) {
		mic_reset(mdev);
		/*
		 * The state will either be MIC_OFFLINE if the reset succeeded
		 * or MIC_RESET_FAILED if the firmware reset failed.
		 */
		goto retry;
	}
	rc = mdev->ops->load_mic_fw(mdev, buf);
	if (rc)
		goto unlock_ret;
	mic_smpt_restore(mdev);
	mic_intr_restore(mdev);
	mdev->intr_ops->enable_interrupts(mdev);
	mdev->ops->write_spad(mdev, MIC_DPLO_SPAD, mdev->dp_dma_addr);
	mdev->ops->write_spad(mdev, MIC_DPHI_SPAD, mdev->dp_dma_addr >> 32);
	mdev->ops->send_firmware_intr(mdev);
	mic_set_state(mdev, MIC_ONLINE);
unlock_ret:
	mutex_unlock(&mdev->mic_mutex);
	return rc;
}

/**
 * mic_stop - Prepare the MIC for reset and trigger reset.
 * @mdev: pointer to mic_device instance
 * @force: force a MIC to reset even if it is already offline.
 *
 * RETURNS: None.
 */
void mic_stop(struct mic_device *mdev, bool force)
{
	mutex_lock(&mdev->mic_mutex);
	if (MIC_OFFLINE != mdev->state || force) {
		mic_virtio_reset_devices(mdev);
		mic_bootparam_init(mdev);
		mic_reset(mdev);
		if (MIC_RESET_FAILED == mdev->state)
			goto unlock;
		mic_set_shutdown_status(mdev, MIC_NOP);
		if (MIC_SUSPENDED != mdev->state)
			mic_set_state(mdev, MIC_OFFLINE);
	}
unlock:
	mutex_unlock(&mdev->mic_mutex);
}

/**
 * mic_shutdown - Initiate MIC shutdown.
 * @mdev: pointer to mic_device instance
 *
 * RETURNS: None.
 */
void mic_shutdown(struct mic_device *mdev)
{
	struct mic_bootparam *bootparam = mdev->dp;
	s8 db = bootparam->h2c_shutdown_db;

	mutex_lock(&mdev->mic_mutex);
	if (MIC_ONLINE == mdev->state && db != -1) {
		bootparam->shutdown_card = 1;
		mdev->ops->send_intr(mdev, db);
		mic_set_state(mdev, MIC_SHUTTING_DOWN);
	}
	mutex_unlock(&mdev->mic_mutex);
}

/**
 * mic_shutdown_work - Handle shutdown interrupt from MIC.
 * @work: The work structure.
 *
 * This work is scheduled whenever the host has received a shutdown
 * interrupt from the MIC.
 */
void mic_shutdown_work(struct work_struct *work)
{
	struct mic_device *mdev = container_of(work, struct mic_device,
			shutdown_work);
	struct mic_bootparam *bootparam = mdev->dp;

	mutex_lock(&mdev->mic_mutex);
	mic_set_shutdown_status(mdev, bootparam->shutdown_status);
	bootparam->shutdown_status = 0;

	/*
	 * if state is MIC_SUSPENDED, OSPM suspend is in progress. We do not
	 * change the state here so as to prevent users from booting the card
	 * during and after the suspend operation.
	 */
	if (MIC_SHUTTING_DOWN != mdev->state &&
	    MIC_SUSPENDED != mdev->state)
		mic_set_state(mdev, MIC_SHUTTING_DOWN);
	mutex_unlock(&mdev->mic_mutex);
}

/**
 * mic_reset_trigger_work - Trigger MIC reset.
 * @work: The work structure.
 *
 * This work is scheduled whenever the host wants to reset the MIC.
 */
void mic_reset_trigger_work(struct work_struct *work)
{
	struct mic_device *mdev = container_of(work, struct mic_device,
			reset_trigger_work);

	mic_stop(mdev, false);
}

/**
 * mic_complete_resume - Complete MIC Resume after an OSPM suspend/hibernate
 * event.
 * @mdev: pointer to mic_device instance
 *
 * RETURNS: None.
 */
void mic_complete_resume(struct mic_device *mdev)
{
	if (mdev->state != MIC_SUSPENDED) {
		dev_warn(mdev->sdev->parent, "state %d should be %d\n",
			 mdev->state, MIC_SUSPENDED);
		return;
	}

	/* Make sure firmware is ready */
	if (!mdev->ops->is_fw_ready(mdev))
		mic_stop(mdev, true);

	mutex_lock(&mdev->mic_mutex);
	mic_set_state(mdev, MIC_OFFLINE);
	mutex_unlock(&mdev->mic_mutex);
}

/**
 * mic_prepare_suspend - Handle suspend notification for the MIC device.
 * @mdev: pointer to mic_device instance
 *
 * RETURNS: None.
 */
void mic_prepare_suspend(struct mic_device *mdev)
{
	int rc;

#define MIC_SUSPEND_TIMEOUT (60 * HZ)

	mutex_lock(&mdev->mic_mutex);
	switch (mdev->state) {
	case MIC_OFFLINE:
		/*
		 * Card is already offline. Set state to MIC_SUSPENDED
		 * to prevent users from booting the card.
		 */
		mic_set_state(mdev, MIC_SUSPENDED);
		mutex_unlock(&mdev->mic_mutex);
		break;
	case MIC_ONLINE:
		/*
		 * Card is online. Set state to MIC_SUSPENDING and notify
		 * MIC user space daemon which will issue card
		 * shutdown and reset.
		 */
		mic_set_state(mdev, MIC_SUSPENDING);
		mutex_unlock(&mdev->mic_mutex);
		rc = wait_for_completion_timeout(&mdev->reset_wait,
						MIC_SUSPEND_TIMEOUT);
		/* Force reset the card if the shutdown completion timed out */
		if (!rc) {
			mutex_lock(&mdev->mic_mutex);
			mic_set_state(mdev, MIC_SUSPENDED);
			mutex_unlock(&mdev->mic_mutex);
			mic_stop(mdev, true);
		}
		break;
	case MIC_SHUTTING_DOWN:
		/*
		 * Card is shutting down. Set state to MIC_SUSPENDED
		 * to prevent further boot of the card.
		 */
		mic_set_state(mdev, MIC_SUSPENDED);
		mutex_unlock(&mdev->mic_mutex);
		rc = wait_for_completion_timeout(&mdev->reset_wait,
						MIC_SUSPEND_TIMEOUT);
		/* Force reset the card if the shutdown completion timed out */
		if (!rc)
			mic_stop(mdev, true);
		break;
	default:
		mutex_unlock(&mdev->mic_mutex);
		break;
	}
}

/**
 * mic_suspend - Initiate MIC suspend. Suspend merely issues card shutdown.
 * @mdev: pointer to mic_device instance
 *
 * RETURNS: None.
 */
void mic_suspend(struct mic_device *mdev)
{
	struct mic_bootparam *bootparam = mdev->dp;
	s8 db = bootparam->h2c_shutdown_db;

	mutex_lock(&mdev->mic_mutex);
	if (MIC_SUSPENDING == mdev->state && db != -1) {
		bootparam->shutdown_card = 1;
		mdev->ops->send_intr(mdev, db);
		mic_set_state(mdev, MIC_SUSPENDED);
	}
	mutex_unlock(&mdev->mic_mutex);
}
