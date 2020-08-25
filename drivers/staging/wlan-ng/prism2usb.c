// SPDX-License-Identifier: GPL-2.0
#include "hfa384x_usb.c"
#include "prism2mgmt.c"
#include "prism2mib.c"
#include "prism2sta.c"
#include "prism2fw.c"

#define PRISM_DEV(vid, pid, name)		\
	{ USB_DEVICE(vid, pid),			\
	.driver_info = (unsigned long)name }

static const struct usb_device_id usb_prism_tbl[] = {
	PRISM_DEV(0x04bb, 0x0922, "IOData AirPort WN-B11/USBS"),
	PRISM_DEV(0x07aa, 0x0012, "Corega Wireless LAN USB Stick-11"),
	PRISM_DEV(0x09aa, 0x3642, "Prism2.x 11Mbps WLAN USB Adapter"),
	PRISM_DEV(0x1668, 0x0408, "Actiontec Prism2.5 11Mbps WLAN USB Adapter"),
	PRISM_DEV(0x1668, 0x0421, "Actiontec Prism2.5 11Mbps WLAN USB Adapter"),
	PRISM_DEV(0x1915, 0x2236, "Linksys WUSB11v3.0 11Mbps WLAN USB Adapter"),
	PRISM_DEV(0x066b, 0x2212, "Linksys WUSB11v2.5 11Mbps WLAN USB Adapter"),
	PRISM_DEV(0x066b, 0x2213, "Linksys WUSB12v1.1 11Mbps WLAN USB Adapter"),
	PRISM_DEV(0x0411, 0x0016, "Melco WLI-USB-S11 11Mbps WLAN Adapter"),
	PRISM_DEV(0x08de, 0x7a01, "PRISM25 IEEE 802.11 Mini USB Adapter"),
	PRISM_DEV(0x8086, 0x1111, "Intel PRO/Wireless 2011B LAN USB Adapter"),
	PRISM_DEV(0x0d8e, 0x7a01, "PRISM25 IEEE 802.11 Mini USB Adapter"),
	PRISM_DEV(0x045e, 0x006e, "Microsoft MN510 Wireless USB Adapter"),
	PRISM_DEV(0x0967, 0x0204, "Acer Warplink USB Adapter"),
	PRISM_DEV(0x0cde, 0x0002, "Z-Com 725/726 Prism2.5 USB/USB Integrated"),
	PRISM_DEV(0x0cde, 0x0005, "Z-Com Xl735 Wireless 802.11b USB Adapter"),
	PRISM_DEV(0x413c, 0x8100, "Dell TrueMobile 1180 Wireless USB Adapter"),
	PRISM_DEV(0x0b3b, 0x1601, "ALLNET 0193 11Mbps WLAN USB Adapter"),
	PRISM_DEV(0x0b3b, 0x1602, "ZyXEL ZyAIR B200 Wireless USB Adapter"),
	PRISM_DEV(0x0baf, 0x00eb, "USRobotics USR1120 Wireless USB Adapter"),
	PRISM_DEV(0x0411, 0x0027, "Melco WLI-USB-KS11G 11Mbps WLAN Adapter"),
	PRISM_DEV(0x04f1, 0x3009, "JVC MP-XP7250 Builtin USB WLAN Adapter"),
	PRISM_DEV(0x0846, 0x4110, "NetGear MA111"),
	PRISM_DEV(0x03f3, 0x0020, "Adaptec AWN-8020 USB WLAN Adapter"),
	PRISM_DEV(0x2821, 0x3300, "ASUS-WL140 Wireless USB Adapter"),
	PRISM_DEV(0x2001, 0x3700, "DWL-122 Wireless USB Adapter"),
	PRISM_DEV(0x2001, 0x3702, "DWL-120 Rev F Wireless USB Adapter"),
	PRISM_DEV(0x50c2, 0x4013, "Averatec USB WLAN Adapter"),
	PRISM_DEV(0x2c02, 0x14ea, "Planex GW-US11H WLAN USB Adapter"),
	PRISM_DEV(0x124a, 0x168b, "Airvast PRISM3 WLAN USB Adapter"),
	PRISM_DEV(0x083a, 0x3503, "T-Sinus 111 USB WLAN Adapter"),
	PRISM_DEV(0x2821, 0x3300, "Hawking HighDB USB Adapter"),
	PRISM_DEV(0x0411, 0x0044, "Melco WLI-USB-KB11 11Mbps WLAN Adapter"),
	PRISM_DEV(0x1668, 0x6106, "ROPEX FreeLan 802.11b USB Adapter"),
	PRISM_DEV(0x124a, 0x4017, "Pheenet WL-503IA 802.11b USB Adapter"),
	PRISM_DEV(0x0bb2, 0x0302, "Ambit Microsystems Corp."),
	PRISM_DEV(0x9016, 0x182d, "Sitecom WL-022 802.11b USB Adapter"),
	PRISM_DEV(0x0543, 0x0f01,
		  "ViewSonic Airsync USB Adapter 11Mbps (Prism2.5)"),
	PRISM_DEV(0x067c, 0x1022,
		  "Siemens SpeedStream 1022 11Mbps WLAN USB Adapter"),
	PRISM_DEV(0x049f, 0x0033,
		  "Compaq/Intel W100 PRO/Wireless 11Mbps multiport WLAN Adapter"),
	{ } /* terminator */
};
MODULE_DEVICE_TABLE(usb, usb_prism_tbl);

static int prism2sta_probe_usb(struct usb_interface *interface,
			       const struct usb_device_id *id)
{
	struct usb_device *dev;
	const struct usb_endpoint_descriptor *epd;
	const struct usb_host_interface *iface_desc = interface->cur_altsetting;
	struct wlandevice *wlandev = NULL;
	struct hfa384x *hw = NULL;
	int result = 0;

	if (iface_desc->desc.bNumEndpoints != 2) {
		result = -ENODEV;
		goto failed;
	}

	result = -EINVAL;
	epd = &iface_desc->endpoint[1].desc;
	if (!usb_endpoint_is_bulk_in(epd))
		goto failed;
	epd = &iface_desc->endpoint[2].desc;
	if (!usb_endpoint_is_bulk_out(epd))
		goto failed;

	dev = interface_to_usbdev(interface);
	wlandev = create_wlan();
	if (!wlandev) {
		dev_err(&interface->dev, "Memory allocation failure.\n");
		result = -EIO;
		goto failed;
	}
	hw = wlandev->priv;

	if (wlan_setup(wlandev, &interface->dev) != 0) {
		dev_err(&interface->dev, "wlan_setup() failed.\n");
		result = -EIO;
		goto failed;
	}

	/* Initialize the hw data */
	hfa384x_create(hw, dev);
	hw->wlandev = wlandev;

	/* Register the wlandev, this gets us a name and registers the
	 * linux netdevice.
	 */
	SET_NETDEV_DEV(wlandev->netdev, &interface->dev);

	/* Do a chip-level reset on the MAC */
	if (prism2_doreset) {
		result = hfa384x_corereset(hw,
					   prism2_reset_holdtime,
					   prism2_reset_settletime, 0);
		if (result != 0) {
			result = -EIO;
			dev_err(&interface->dev,
				"hfa384x_corereset() failed.\n");
			goto failed_reset;
		}
	}

	usb_get_dev(dev);

	wlandev->msdstate = WLAN_MSD_HWPRESENT;

	/* Try and load firmware, then enable card before we register */
	prism2_fwtry(dev, wlandev);
	prism2sta_ifstate(wlandev, P80211ENUM_ifstate_enable);

	if (register_wlandev(wlandev) != 0) {
		dev_err(&interface->dev, "register_wlandev() failed.\n");
		result = -EIO;
		goto failed_register;
	}

	goto done;

failed_register:
	usb_put_dev(dev);
failed_reset:
	wlan_unsetup(wlandev);
failed:
	kfree(wlandev);
	kfree(hw);
	wlandev = NULL;

done:
	usb_set_intfdata(interface, wlandev);
	return result;
}

static void prism2sta_disconnect_usb(struct usb_interface *interface)
{
	struct wlandevice *wlandev;

	wlandev = usb_get_intfdata(interface);
	if (wlandev) {
		LIST_HEAD(cleanlist);
		struct hfa384x_usbctlx *ctlx, *temp;
		unsigned long flags;

		struct hfa384x *hw = wlandev->priv;

		if (!hw)
			goto exit;

		spin_lock_irqsave(&hw->ctlxq.lock, flags);

		p80211netdev_hwremoved(wlandev);
		list_splice_init(&hw->ctlxq.reapable, &cleanlist);
		list_splice_init(&hw->ctlxq.completing, &cleanlist);
		list_splice_init(&hw->ctlxq.pending, &cleanlist);
		list_splice_init(&hw->ctlxq.active, &cleanlist);

		spin_unlock_irqrestore(&hw->ctlxq.lock, flags);

		/* There's no hardware to shutdown, but the driver
		 * might have some tasks or tasklets that must be
		 * stopped before we can tear everything down.
		 */
		prism2sta_ifstate(wlandev, P80211ENUM_ifstate_disable);

		del_singleshot_timer_sync(&hw->throttle);
		del_singleshot_timer_sync(&hw->reqtimer);
		del_singleshot_timer_sync(&hw->resptimer);

		/* Unlink all the URBs. This "removes the wheels"
		 * from the entire CTLX handling mechanism.
		 */
		usb_kill_urb(&hw->rx_urb);
		usb_kill_urb(&hw->tx_urb);
		usb_kill_urb(&hw->ctlx_urb);

		tasklet_kill(&hw->completion_bh);
		tasklet_kill(&hw->reaper_bh);

		cancel_work_sync(&hw->link_bh);
		cancel_work_sync(&hw->commsqual_bh);
		cancel_work_sync(&hw->usb_work);

		/* Now we complete any outstanding commands
		 * and tell everyone who is waiting for their
		 * responses that we have shut down.
		 */
		list_for_each_entry(ctlx, &cleanlist, list)
			complete(&ctlx->done);

		/* Give any outstanding synchronous commands
		 * a chance to complete. All they need to do
		 * is "wake up", so that's easy.
		 * (I'd like a better way to do this, really.)
		 */
		msleep(100);

		/* Now delete the CTLXs, because no-one else can now. */
		list_for_each_entry_safe(ctlx, temp, &cleanlist, list)
			kfree(ctlx);

		/* Unhook the wlandev */
		unregister_wlandev(wlandev);
		wlan_unsetup(wlandev);

		usb_put_dev(hw->usb);

		hfa384x_destroy(hw);
		kfree(hw);

		kfree(wlandev);
	}

exit:
	usb_set_intfdata(interface, NULL);
}

#ifdef CONFIG_PM
static int prism2sta_suspend(struct usb_interface *interface,
			     pm_message_t message)
{
	struct hfa384x *hw = NULL;
	struct wlandevice *wlandev;

	wlandev = usb_get_intfdata(interface);
	if (!wlandev)
		return -ENODEV;

	hw = wlandev->priv;
	if (!hw)
		return -ENODEV;

	prism2sta_ifstate(wlandev, P80211ENUM_ifstate_disable);

	usb_kill_urb(&hw->rx_urb);
	usb_kill_urb(&hw->tx_urb);
	usb_kill_urb(&hw->ctlx_urb);

	return 0;
}

static int prism2sta_resume(struct usb_interface *interface)
{
	int result = 0;
	struct hfa384x *hw = NULL;
	struct wlandevice *wlandev;

	wlandev = usb_get_intfdata(interface);
	if (!wlandev)
		return -ENODEV;

	hw = wlandev->priv;
	if (!hw)
		return -ENODEV;

	/* Do a chip-level reset on the MAC */
	if (prism2_doreset) {
		result = hfa384x_corereset(hw,
					   prism2_reset_holdtime,
					   prism2_reset_settletime, 0);
		if (result != 0) {
			unregister_wlandev(wlandev);
			hfa384x_destroy(hw);
			dev_err(&interface->dev, "hfa384x_corereset() failed.\n");
			kfree(wlandev);
			kfree(hw);
			wlandev = NULL;
			return -ENODEV;
		}
	}

	prism2sta_ifstate(wlandev, P80211ENUM_ifstate_enable);

	return 0;
}
#else
#define prism2sta_suspend NULL
#define prism2sta_resume NULL
#endif /* CONFIG_PM */

static struct usb_driver prism2_usb_driver = {
	.name = "prism2_usb",
	.probe = prism2sta_probe_usb,
	.disconnect = prism2sta_disconnect_usb,
	.id_table = usb_prism_tbl,
	.suspend = prism2sta_suspend,
	.resume = prism2sta_resume,
	.reset_resume = prism2sta_resume,
	/* fops, minor? */
};

module_usb_driver(prism2_usb_driver);
