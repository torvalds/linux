// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Advanced Micro Devices, Inc. */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/vfio.h>

#include <linux/pds/pds_common.h>
#include <linux/pds/pds_core_if.h>
#include <linux/pds/pds_adminq.h>

#include "vfio_dev.h"
#include "pci_drv.h"
#include "cmds.h"

#define PDS_VFIO_DRV_DESCRIPTION	"AMD/Pensando VFIO Device Driver"
#define PCI_VENDOR_ID_PENSANDO		0x1dd8

static void pds_vfio_recovery(struct pds_vfio_pci_device *pds_vfio)
{
	bool deferred_reset_needed = false;

	/*
	 * Documentation states that the kernel migration driver must not
	 * generate asynchronous device state transitions outside of
	 * manipulation by the user or the VFIO_DEVICE_RESET ioctl.
	 *
	 * Since recovery is an asynchronous event received from the device,
	 * initiate a deferred reset. Issue a deferred reset in the following
	 * situations:
	 *   1. Migration is in progress, which will cause the next step of
	 *	the migration to fail.
	 *   2. If the device is in a state that will be set to
	 *	VFIO_DEVICE_STATE_RUNNING on the next action (i.e. VM is
	 *	shutdown and device is in VFIO_DEVICE_STATE_STOP).
	 */
	mutex_lock(&pds_vfio->state_mutex);
	if ((pds_vfio->state != VFIO_DEVICE_STATE_RUNNING &&
	     pds_vfio->state != VFIO_DEVICE_STATE_ERROR) ||
	    (pds_vfio->state == VFIO_DEVICE_STATE_RUNNING &&
	     pds_vfio_dirty_is_enabled(pds_vfio)))
		deferred_reset_needed = true;
	mutex_unlock(&pds_vfio->state_mutex);

	/*
	 * On the next user initiated state transition, the device will
	 * transition to the VFIO_DEVICE_STATE_ERROR. At this point it's the user's
	 * responsibility to reset the device.
	 *
	 * If a VFIO_DEVICE_RESET is requested post recovery and before the next
	 * state transition, then the deferred reset state will be set to
	 * VFIO_DEVICE_STATE_RUNNING.
	 */
	if (deferred_reset_needed) {
		mutex_lock(&pds_vfio->reset_mutex);
		pds_vfio->deferred_reset = true;
		pds_vfio->deferred_reset_state = VFIO_DEVICE_STATE_ERROR;
		mutex_unlock(&pds_vfio->reset_mutex);
	}
}

static int pds_vfio_pci_notify_handler(struct notifier_block *nb,
				       unsigned long ecode, void *data)
{
	struct pds_vfio_pci_device *pds_vfio =
		container_of(nb, struct pds_vfio_pci_device, nb);
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	union pds_core_notifyq_comp *event = data;

	dev_dbg(dev, "%s: event code %lu\n", __func__, ecode);

	/*
	 * We don't need to do anything for RESET state==0 as there is no notify
	 * or feedback mechanism available, and it is possible that we won't
	 * even see a state==0 event since the pds_core recovery is pending.
	 *
	 * Any requests from VFIO while state==0 will fail, which will return
	 * error and may cause migration to fail.
	 */
	if (ecode == PDS_EVENT_RESET) {
		dev_info(dev, "%s: PDS_EVENT_RESET event received, state==%d\n",
			 __func__, event->reset.state);
		/*
		 * pds_core device finished recovery and sent us the
		 * notification (state == 1) to allow us to recover
		 */
		if (event->reset.state == 1)
			pds_vfio_recovery(pds_vfio);
	}

	return 0;
}

static int
pds_vfio_pci_register_event_handler(struct pds_vfio_pci_device *pds_vfio)
{
	struct device *dev = pds_vfio_to_dev(pds_vfio);
	struct notifier_block *nb = &pds_vfio->nb;
	int err;

	if (!nb->notifier_call) {
		nb->notifier_call = pds_vfio_pci_notify_handler;
		err = pdsc_register_notify(nb);
		if (err) {
			nb->notifier_call = NULL;
			dev_err(dev,
				"failed to register pds event handler: %pe\n",
				ERR_PTR(err));
			return -EINVAL;
		}
		dev_dbg(dev, "pds event handler registered\n");
	}

	return 0;
}

static void
pds_vfio_pci_unregister_event_handler(struct pds_vfio_pci_device *pds_vfio)
{
	if (pds_vfio->nb.notifier_call) {
		pdsc_unregister_notify(&pds_vfio->nb);
		pds_vfio->nb.notifier_call = NULL;
	}
}

static int pds_vfio_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	struct pds_vfio_pci_device *pds_vfio;
	int err;

	pds_vfio = vfio_alloc_device(pds_vfio_pci_device, vfio_coredev.vdev,
				     &pdev->dev, pds_vfio_ops_info());
	if (IS_ERR(pds_vfio))
		return PTR_ERR(pds_vfio);

	dev_set_drvdata(&pdev->dev, &pds_vfio->vfio_coredev);

	err = vfio_pci_core_register_device(&pds_vfio->vfio_coredev);
	if (err)
		goto out_put_vdev;

	err = pds_vfio_register_client_cmd(pds_vfio);
	if (err) {
		dev_err(&pdev->dev, "failed to register as client: %pe\n",
			ERR_PTR(err));
		goto out_unregister_coredev;
	}

	err = pds_vfio_pci_register_event_handler(pds_vfio);
	if (err)
		goto out_unregister_client;

	return 0;

out_unregister_client:
	pds_vfio_unregister_client_cmd(pds_vfio);
out_unregister_coredev:
	vfio_pci_core_unregister_device(&pds_vfio->vfio_coredev);
out_put_vdev:
	vfio_put_device(&pds_vfio->vfio_coredev.vdev);
	return err;
}

static void pds_vfio_pci_remove(struct pci_dev *pdev)
{
	struct pds_vfio_pci_device *pds_vfio = pds_vfio_pci_drvdata(pdev);

	pds_vfio_pci_unregister_event_handler(pds_vfio);
	pds_vfio_unregister_client_cmd(pds_vfio);
	vfio_pci_core_unregister_device(&pds_vfio->vfio_coredev);
	vfio_put_device(&pds_vfio->vfio_coredev.vdev);
}

static const struct pci_device_id pds_vfio_pci_table[] = {
	{ PCI_DRIVER_OVERRIDE_DEVICE_VFIO(PCI_VENDOR_ID_PENSANDO, 0x1003) }, /* Ethernet VF */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pds_vfio_pci_table);

static void pds_vfio_pci_aer_reset_done(struct pci_dev *pdev)
{
	struct pds_vfio_pci_device *pds_vfio = pds_vfio_pci_drvdata(pdev);

	pds_vfio_reset(pds_vfio);
}

static const struct pci_error_handlers pds_vfio_pci_err_handlers = {
	.reset_done = pds_vfio_pci_aer_reset_done,
	.error_detected = vfio_pci_core_aer_err_detected,
};

static struct pci_driver pds_vfio_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = pds_vfio_pci_table,
	.probe = pds_vfio_pci_probe,
	.remove = pds_vfio_pci_remove,
	.err_handler = &pds_vfio_pci_err_handlers,
	.driver_managed_dma = true,
};

module_pci_driver(pds_vfio_pci_driver);

MODULE_DESCRIPTION(PDS_VFIO_DRV_DESCRIPTION);
MODULE_AUTHOR("Brett Creeley <brett.creeley@amd.com>");
MODULE_LICENSE("GPL");
