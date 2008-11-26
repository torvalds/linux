/*
 * Wireless Host Controller (WHC) driver.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/uwb/umc.h>

#include "../../wusbcore/wusbhc.h"

#include "whcd.h"

/*
 * One time initialization.
 *
 * Nothing to do here.
 */
static int whc_reset(struct usb_hcd *usb_hcd)
{
	return 0;
}

/*
 * Start the wireless host controller.
 *
 * Start device notification.
 *
 * Put hc into run state, set DNTS parameters.
 */
static int whc_start(struct usb_hcd *usb_hcd)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct whc *whc = wusbhc_to_whc(wusbhc);
	u8 bcid;
	int ret;

	mutex_lock(&wusbhc->mutex);

	le_writel(WUSBINTR_GEN_CMD_DONE
		  | WUSBINTR_HOST_ERR
		  | WUSBINTR_ASYNC_SCHED_SYNCED
		  | WUSBINTR_DNTS_INT
		  | WUSBINTR_ERR_INT
		  | WUSBINTR_INT,
		  whc->base + WUSBINTR);

	/* set cluster ID */
	bcid = wusb_cluster_id_get();
	ret = whc_set_cluster_id(whc, bcid);
	if (ret < 0)
		goto out;
	wusbhc->cluster_id = bcid;

	/* start HC */
	whc_write_wusbcmd(whc, WUSBCMD_RUN, WUSBCMD_RUN);

	usb_hcd->uses_new_polling = 1;
	usb_hcd->poll_rh = 1;
	usb_hcd->state = HC_STATE_RUNNING;

out:
	mutex_unlock(&wusbhc->mutex);
	return ret;
}


/*
 * Stop the wireless host controller.
 *
 * Stop device notification.
 *
 * Wait for pending transfer to stop? Put hc into stop state?
 */
static void whc_stop(struct usb_hcd *usb_hcd)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct whc *whc = wusbhc_to_whc(wusbhc);

	mutex_lock(&wusbhc->mutex);

	/* stop HC */
	le_writel(0, whc->base + WUSBINTR);
	whc_write_wusbcmd(whc, WUSBCMD_RUN, 0);
	whci_wait_for(&whc->umc->dev, whc->base + WUSBSTS,
		      WUSBSTS_HCHALTED, WUSBSTS_HCHALTED,
		      100, "HC to halt");

	wusb_cluster_id_put(wusbhc->cluster_id);

	mutex_unlock(&wusbhc->mutex);
}

static int whc_get_frame_number(struct usb_hcd *usb_hcd)
{
	/* Frame numbers are not applicable to WUSB. */
	return -ENOSYS;
}


/*
 * Queue an URB to the ASL or PZL
 */
static int whc_urb_enqueue(struct usb_hcd *usb_hcd, struct urb *urb,
			   gfp_t mem_flags)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct whc *whc = wusbhc_to_whc(wusbhc);
	int ret;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_INTERRUPT:
		ret = pzl_urb_enqueue(whc, urb, mem_flags);
		break;
	case PIPE_ISOCHRONOUS:
		dev_err(&whc->umc->dev, "isochronous transfers unsupported\n");
		ret = -ENOTSUPP;
		break;
	case PIPE_CONTROL:
	case PIPE_BULK:
	default:
		ret = asl_urb_enqueue(whc, urb, mem_flags);
		break;
	};

	return ret;
}

/*
 * Remove a queued URB from the ASL or PZL.
 */
static int whc_urb_dequeue(struct usb_hcd *usb_hcd, struct urb *urb, int status)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct whc *whc = wusbhc_to_whc(wusbhc);
	int ret;

	switch (usb_pipetype(urb->pipe)) {
	case PIPE_INTERRUPT:
		ret = pzl_urb_dequeue(whc, urb, status);
		break;
	case PIPE_ISOCHRONOUS:
		ret = -ENOTSUPP;
		break;
	case PIPE_CONTROL:
	case PIPE_BULK:
	default:
		ret = asl_urb_dequeue(whc, urb, status);
		break;
	};

	return ret;
}

/*
 * Wait for all URBs to the endpoint to be completed, then delete the
 * qset.
 */
static void whc_endpoint_disable(struct usb_hcd *usb_hcd,
				 struct usb_host_endpoint *ep)
{
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct whc *whc = wusbhc_to_whc(wusbhc);
	struct whc_qset *qset;

	qset = ep->hcpriv;
	if (qset) {
		ep->hcpriv = NULL;
		if (usb_endpoint_xfer_bulk(&ep->desc)
		    || usb_endpoint_xfer_control(&ep->desc))
			asl_qset_delete(whc, qset);
		else
			pzl_qset_delete(whc, qset);
	}
}

static struct hc_driver whc_hc_driver = {
	.description = "whci-hcd",
	.product_desc = "Wireless host controller",
	.hcd_priv_size = sizeof(struct whc) - sizeof(struct usb_hcd),
	.irq = whc_int_handler,
	.flags = HCD_USB2,

	.reset = whc_reset,
	.start = whc_start,
	.stop = whc_stop,
	.get_frame_number = whc_get_frame_number,
	.urb_enqueue = whc_urb_enqueue,
	.urb_dequeue = whc_urb_dequeue,
	.endpoint_disable = whc_endpoint_disable,

	.hub_status_data = wusbhc_rh_status_data,
	.hub_control = wusbhc_rh_control,
	.bus_suspend = wusbhc_rh_suspend,
	.bus_resume = wusbhc_rh_resume,
	.start_port_reset = wusbhc_rh_start_port_reset,
};

static int whc_probe(struct umc_dev *umc)
{
	int ret = -ENOMEM;
	struct usb_hcd *usb_hcd;
	struct wusbhc *wusbhc = NULL;
	struct whc *whc = NULL;
	struct device *dev = &umc->dev;

	usb_hcd = usb_create_hcd(&whc_hc_driver, dev, "whci");
	if (usb_hcd == NULL) {
		dev_err(dev, "unable to create hcd\n");
		goto error;
	}

	usb_hcd->wireless = 1;

	wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	whc = wusbhc_to_whc(wusbhc);
	whc->umc = umc;

	ret = whc_init(whc);
	if (ret)
		goto error;

	wusbhc->dev = dev;
	wusbhc->uwb_rc = uwb_rc_get_by_grandpa(umc->dev.parent);
	if (!wusbhc->uwb_rc) {
		ret = -ENODEV;
		dev_err(dev, "cannot get radio controller\n");
		goto error;
	}

	if (whc->n_devices > USB_MAXCHILDREN) {
		dev_warn(dev, "USB_MAXCHILDREN too low for WUSB adapter (%u ports)\n",
			 whc->n_devices);
		wusbhc->ports_max = USB_MAXCHILDREN;
	} else
		wusbhc->ports_max = whc->n_devices;
	wusbhc->mmcies_max      = whc->n_mmc_ies;
	wusbhc->start           = whc_wusbhc_start;
	wusbhc->stop            = whc_wusbhc_stop;
	wusbhc->mmcie_add       = whc_mmcie_add;
	wusbhc->mmcie_rm        = whc_mmcie_rm;
	wusbhc->dev_info_set    = whc_dev_info_set;
	wusbhc->bwa_set         = whc_bwa_set;
	wusbhc->set_num_dnts    = whc_set_num_dnts;
	wusbhc->set_ptk         = whc_set_ptk;
	wusbhc->set_gtk         = whc_set_gtk;

	ret = wusbhc_create(wusbhc);
	if (ret)
		goto error_wusbhc_create;

	ret = usb_add_hcd(usb_hcd, whc->umc->irq, IRQF_SHARED);
	if (ret) {
		dev_err(dev, "cannot add HCD: %d\n", ret);
		goto error_usb_add_hcd;
	}

	ret = wusbhc_b_create(wusbhc);
	if (ret) {
		dev_err(dev, "WUSBHC phase B setup failed: %d\n", ret);
		goto error_wusbhc_b_create;
	}

	whc_dbg_init(whc);

	return 0;

error_wusbhc_b_create:
	usb_remove_hcd(usb_hcd);
error_usb_add_hcd:
	wusbhc_destroy(wusbhc);
error_wusbhc_create:
	uwb_rc_put(wusbhc->uwb_rc);
error:
	whc_clean_up(whc);
	if (usb_hcd)
		usb_put_hcd(usb_hcd);
	return ret;
}


static void whc_remove(struct umc_dev *umc)
{
	struct usb_hcd *usb_hcd = dev_get_drvdata(&umc->dev);
	struct wusbhc *wusbhc = usb_hcd_to_wusbhc(usb_hcd);
	struct whc *whc = wusbhc_to_whc(wusbhc);

	if (usb_hcd) {
		whc_dbg_clean_up(whc);
		wusbhc_b_destroy(wusbhc);
		usb_remove_hcd(usb_hcd);
		wusbhc_destroy(wusbhc);
		uwb_rc_put(wusbhc->uwb_rc);
		whc_clean_up(whc);
		usb_put_hcd(usb_hcd);
	}
}

static struct umc_driver whci_hc_driver = {
	.name =		"whci-hcd",
	.cap_id =       UMC_CAP_ID_WHCI_WUSB_HC,
	.probe =	whc_probe,
	.remove =	whc_remove,
};

static int __init whci_hc_driver_init(void)
{
	return umc_driver_register(&whci_hc_driver);
}
module_init(whci_hc_driver_init);

static void __exit whci_hc_driver_exit(void)
{
	umc_driver_unregister(&whci_hc_driver);
}
module_exit(whci_hc_driver_exit);

/* PCI device ID's that we handle (so it gets loaded) */
static struct pci_device_id whci_hcd_id_table[] = {
	{ PCI_DEVICE_CLASS(PCI_CLASS_WIRELESS_WHCI, ~0) },
	{ /* empty last entry */ }
};
MODULE_DEVICE_TABLE(pci, whci_hcd_id_table);

MODULE_DESCRIPTION("WHCI Wireless USB host controller driver");
MODULE_AUTHOR("Cambridge Silicon Radio Ltd.");
MODULE_LICENSE("GPL");
