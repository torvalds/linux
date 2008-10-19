#define WLAN_HOSTIF WLAN_USB
#include "hfa384x_usb.c"
#include "prism2mgmt.c"
#include "prism2mib.c"
#include "prism2sta.c"

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,4,0))
#error "prism2_usb requires at least a 2.4.x kernel!"
#endif

#define PRISM_USB_DEVICE(vid, pid, name) \
           USB_DEVICE(vid, pid),  \
           .driver_info = (unsigned long) name

static struct usb_device_id usb_prism_tbl[] = {
	{PRISM_USB_DEVICE(0x04bb, 0x0922, "IOData AirPort WN-B11/USBS")},
	{PRISM_USB_DEVICE(0x07aa, 0x0012, "Corega Wireless LAN USB Stick-11")},
	{PRISM_USB_DEVICE(0x09aa, 0x3642, "Prism2.x 11Mbps WLAN USB Adapter")},
	{PRISM_USB_DEVICE(0x1668, 0x0408, "Actiontec Prism2.5 11Mbps WLAN USB Adapter")},
	{PRISM_USB_DEVICE(0x1668, 0x0421, "Actiontec Prism2.5 11Mbps WLAN USB Adapter")},
	{PRISM_USB_DEVICE(0x1915, 0x2236, "Linksys WUSB11v3.0 11Mbps WLAN USB Adapter")},
	{PRISM_USB_DEVICE(0x066b, 0x2212, "Linksys WUSB11v2.5 11Mbps WLAN USB Adapter")},
	{PRISM_USB_DEVICE(0x066b, 0x2213, "Linksys WUSB12v1.1 11Mbps WLAN USB Adapter")},
	{PRISM_USB_DEVICE(0x067c, 0x1022, "Siemens SpeedStream 1022 11Mbps WLAN USB Adapter")},
	{PRISM_USB_DEVICE(0x049f, 0x0033, "Compaq/Intel W100 PRO/Wireless 11Mbps multiport WLAN Adapter")},
	{PRISM_USB_DEVICE(0x0411, 0x0016, "Melco WLI-USB-S11 11Mbps WLAN Adapter")},
	{PRISM_USB_DEVICE(0x08de, 0x7a01, "PRISM25 IEEE 802.11 Mini USB Adapter")},
	{PRISM_USB_DEVICE(0x8086, 0x1111, "Intel PRO/Wireless 2011B LAN USB Adapter")},
	{PRISM_USB_DEVICE(0x0d8e, 0x7a01, "PRISM25 IEEE 802.11 Mini USB Adapter")},
	{PRISM_USB_DEVICE(0x045e, 0x006e, "Microsoft MN510 Wireless USB Adapter")},
	{PRISM_USB_DEVICE(0x0967, 0x0204, "Acer Warplink USB Adapter")},
	{PRISM_USB_DEVICE(0x0cde, 0x0002, "Z-Com 725/726 Prism2.5 USB/USB Integrated")},
	{PRISM_USB_DEVICE(0x0cde, 0x0005, "Z-Com Xl735 Wireless 802.11b USB Adapter")},
	{PRISM_USB_DEVICE(0x413c, 0x8100, "Dell TrueMobile 1180 Wireless USB Adapter")},
	{PRISM_USB_DEVICE(0x0b3b, 0x1601, "ALLNET 0193 11Mbps WLAN USB Adapter")},
	{PRISM_USB_DEVICE(0x0b3b, 0x1602, "ZyXEL ZyAIR B200 Wireless USB Adapter")},
	{PRISM_USB_DEVICE(0x0baf, 0x00eb, "USRobotics USR1120 Wireless USB Adapter")},
	{PRISM_USB_DEVICE(0x0411, 0x0027, "Melco WLI-USB-KS11G 11Mbps WLAN Adapter")},
        {PRISM_USB_DEVICE(0x04f1, 0x3009, "JVC MP-XP7250 Builtin USB WLAN Adapter")},
	{PRISM_USB_DEVICE(0x0846, 0x4110, "NetGear MA111")},
        {PRISM_USB_DEVICE(0x03f3, 0x0020, "Adaptec AWN-8020 USB WLAN Adapter")},
//	{PRISM_USB_DEVICE(0x0ace, 0x1201, "ZyDAS ZD1201 Wireless USB Adapter")},
	{PRISM_USB_DEVICE(0x2821, 0x3300, "ASUS-WL140 Wireless USB Adapter")},
	{PRISM_USB_DEVICE(0x2001, 0x3700, "DWL-122 Wireless USB Adapter")},
	{PRISM_USB_DEVICE(0x2001, 0x3702, "DWL-120 Rev F Wireless USB Adapter")},
	{PRISM_USB_DEVICE(0x50c2, 0x4013, "Averatec USB WLAN Adapter")},
	{PRISM_USB_DEVICE(0x2c02, 0x14ea, "Planex GW-US11H WLAN USB Adapter")},
	{PRISM_USB_DEVICE(0x124a, 0x168b, "Airvast PRISM3 WLAN USB Adapter")},
	{PRISM_USB_DEVICE(0x083a, 0x3503, "T-Sinus 111 USB WLAN Adapter")},
	{PRISM_USB_DEVICE(0x2821, 0x3300, "Hawking HighDB USB Adapter")},
	{PRISM_USB_DEVICE(0x0411, 0x0044, "Melco WLI-USB-KB11 11Mbps WLAN Adapter")},
	{PRISM_USB_DEVICE(0x1668, 0x6106, "ROPEX FreeLan 802.11b USB Adapter")},
	{PRISM_USB_DEVICE(0x124a, 0x4017, "Pheenet WL-503IA 802.11b USB Adapter")},
	{PRISM_USB_DEVICE(0x0bb2, 0x0302, "Ambit Microsystems Corp.")},
	{PRISM_USB_DEVICE(0x9016, 0x182d, "Sitecom WL-022 802.11b USB Adapter")},
	{PRISM_USB_DEVICE(0x0543, 0x0f01, "ViewSonic Airsync USB Adapter 11Mbps (Prism2.5)")},
	{ /* terminator */ }
};

MODULE_DEVICE_TABLE(usb, usb_prism_tbl);

/*----------------------------------------------------------------
* prism2sta_probe_usb
*
* Probe routine called by the USB subsystem.
*
* Arguments:
*	dev		ptr to the usb_device struct
*	ifnum		interface number being offered
*
* Returns:
*	NULL		- we're not claiming the device+interface
*	non-NULL	- we are claiming the device+interface and
*			  this is a ptr to the data we want back
*			  when disconnect is called.
*
* Side effects:
*
* Call context:
*	I'm not sure, assume it's interrupt.
*
----------------------------------------------------------------*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
static void __devinit *prism2sta_probe_usb(
	struct usb_device *dev,
	unsigned int ifnum,
	const struct usb_device_id *id)
#else
static int prism2sta_probe_usb(
	struct usb_interface *interface,
	const struct usb_device_id *id)
#endif
{

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
	struct usb_interface *interface;
#else
	struct usb_device *dev;
#endif

	wlandevice_t	*wlandev = NULL;
	hfa384x_t	*hw = NULL;
	int              result = 0;

	DBFENTER;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
	interface = &dev->actconfig->interface[ifnum];
#else
	dev = interface_to_usbdev(interface);
#endif


	if ((wlandev = create_wlan()) == NULL) {
		WLAN_LOG_ERROR("%s: Memory allocation failure.\n", dev_info);
		result = -EIO;
		goto failed;
	}
	hw = wlandev->priv;

	if ( wlan_setup(wlandev) != 0 ) {
		WLAN_LOG_ERROR("%s: wlan_setup() failed.\n", dev_info);
		result = -EIO;
		goto failed;
	}

	/* Initialize the hw data */
	hfa384x_create(hw, dev);
	hw->wlandev = wlandev;

	/* Register the wlandev, this gets us a name and registers the
	 * linux netdevice.
	 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
	SET_NETDEV_DEV(wlandev->netdev, &(interface->dev));
#endif
        if ( register_wlandev(wlandev) != 0 ) {
		WLAN_LOG_ERROR("%s: register_wlandev() failed.\n", dev_info);
		result = -EIO;
		goto failed;
        }

	/* Do a chip-level reset on the MAC */
	if (prism2_doreset) {
		result = hfa384x_corereset(hw,
				prism2_reset_holdtime,
				prism2_reset_settletime, 0);
		if (result != 0) {
			unregister_wlandev(wlandev);
			hfa384x_destroy(hw);
			result = -EIO;
			WLAN_LOG_ERROR(
				"%s: hfa384x_corereset() failed.\n",
				dev_info);
			goto failed;
		}
	}

#ifndef NEW_MODULE_CODE
	usb_inc_dev_use(dev);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))
	usb_get_dev(dev);
#endif

	wlandev->msdstate = WLAN_MSD_HWPRESENT;

	goto done;

 failed:
	if (wlandev)	kfree(wlandev);
	if (hw)		kfree(hw);
	wlandev = NULL;

 done:
	DBFEXIT;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
	return wlandev;
#else
	usb_set_intfdata(interface, wlandev);
	return result;
#endif
}


/*----------------------------------------------------------------
* prism2sta_disconnect_usb
*
* Called when a device previously claimed by probe is removed
* from the USB.
*
* Arguments:
*	dev		ptr to the usb_device struct
*	ptr		ptr returned by probe() when the device
*                       was claimed.
*
* Returns:
*	Nothing
*
* Side effects:
*
* Call context:
*	process
----------------------------------------------------------------*/
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0))
static void __devexit
prism2sta_disconnect_usb(struct usb_device *dev, void *ptr)
#else
static void
prism2sta_disconnect_usb(struct usb_interface *interface)
#endif
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0))
	wlandevice_t		*wlandev;
#else
	wlandevice_t		*wlandev = (wlandevice_t*)ptr;
#endif

        DBFENTER;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0))
	wlandev = (wlandevice_t *) usb_get_intfdata(interface);
#endif

	if ( wlandev != NULL ) {
		LIST_HEAD(cleanlist);
		struct list_head	*entry;
		struct list_head	*temp;
		unsigned long		flags;

		hfa384x_t		*hw = wlandev->priv;

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

		flush_scheduled_work();

		/* Now we complete any outstanding commands
		 * and tell everyone who is waiting for their
		 * responses that we have shut down.
		 */
		list_for_each(entry, &cleanlist) {
			hfa384x_usbctlx_t	*ctlx;

			ctlx = list_entry(entry, hfa384x_usbctlx_t, list);
			complete(&ctlx->done);
		}

		/* Give any outstanding synchronous commands
		 * a chance to complete. All they need to do
		 * is "wake up", so that's easy.
		 * (I'd like a better way to do this, really.)
		 */
		msleep(100);

		/* Now delete the CTLXs, because no-one else can now. */
		list_for_each_safe(entry, temp, &cleanlist) {
			hfa384x_usbctlx_t *ctlx;

			ctlx = list_entry(entry, hfa384x_usbctlx_t, list);
			kfree(ctlx);
		}

		/* Unhook the wlandev */
		unregister_wlandev(wlandev);
		wlan_unsetup(wlandev);

#ifndef NEW_MODULE_CODE
		usb_dec_dev_use(hw->usb);
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,15))
		usb_put_dev(hw->usb);
#endif

		hfa384x_destroy(hw);
		kfree(hw);

		kfree(wlandev);
	}

 exit:

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0))
	usb_set_intfdata(interface, NULL);
#endif
	DBFEXIT;
}


static struct usb_driver prism2_usb_driver = {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,4,19)) && (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16))
	.owner = THIS_MODULE,
#endif
	.name = "prism2_usb",
	.probe = prism2sta_probe_usb,
	.disconnect = prism2sta_disconnect_usb,
	.id_table = usb_prism_tbl,
	/* fops, minor? */
};

#ifdef MODULE

static int __init prism2usb_init(void)
{
        DBFENTER;

        WLAN_LOG_NOTICE("%s Loaded\n", version);
        WLAN_LOG_NOTICE("dev_info is: %s\n", dev_info);

	/* This call will result in calls to prism2sta_probe_usb. */
	return usb_register(&prism2_usb_driver);

	DBFEXIT;
};

static void __exit prism2usb_cleanup(void)
{
        DBFENTER;

	usb_deregister(&prism2_usb_driver);

        printk(KERN_NOTICE "%s Unloaded\n", version);

	DBFEXIT;
};

module_init(prism2usb_init);
module_exit(prism2usb_cleanup);

#endif // module
