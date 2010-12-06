#include "headers.h"

static struct usb_device_id InterfaceUsbtable[] = {
	{ USB_DEVICE(BCM_USB_VENDOR_ID_T3, BCM_USB_PRODUCT_ID_T3) },
	{ USB_DEVICE(BCM_USB_VENDOR_ID_T3, BCM_USB_PRODUCT_ID_T3B) },
	{ USB_DEVICE(BCM_USB_VENDOR_ID_T3, BCM_USB_PRODUCT_ID_T3L) },
	{ USB_DEVICE(BCM_USB_VENDOR_ID_T3, BCM_USB_PRODUCT_ID_SM250) },
	{ USB_DEVICE(BCM_USB_VENDOR_ID_ZTE, BCM_USB_PRODUCT_ID_226) },
	{ USB_DEVICE(BCM_USB_VENDOR_ID_FOXCONN, BCM_USB_PRODUCT_ID_1901) },
	{ USB_DEVICE(BCM_USB_VENDOR_ID_ZTE, BCM_USB_PRODUCT_ID_ZTE_TU25) },
	{ }
};
MODULE_DEVICE_TABLE(usb, InterfaceUsbtable);

static int debug = -1;
module_param(debug, uint, 0600);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

static const u32 default_msg =
	NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK
	| NETIF_MSG_TIMER | NETIF_MSG_TX_ERR | NETIF_MSG_RX_ERR
	| NETIF_MSG_IFUP | NETIF_MSG_IFDOWN;

static int InterfaceAdapterInit(PS_INTERFACE_ADAPTER Adapter);

static void InterfaceAdapterFree(PS_INTERFACE_ADAPTER psIntfAdapter)
{
	int i = 0;

	/* Wake up the wait_queue... */
	if (psIntfAdapter->psAdapter->LEDInfo.led_thread_running & BCM_LED_THREAD_RUNNING_ACTIVELY) {
		psIntfAdapter->psAdapter->DriverState = DRIVER_HALT;
		wake_up(&psIntfAdapter->psAdapter->LEDInfo.notify_led_event);
	}
	reset_card_proc(psIntfAdapter->psAdapter);

	/*
	 * worst case time taken by the RDM/WRM will be 5 sec. will check after every 100 ms
	 * to accertain the device is not being accessed. After this No RDM/WRM should be made.
	 */
	while (psIntfAdapter->psAdapter->DeviceAccess) {
		BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL,
			"Device is being Accessed \n");
		msleep(100);
	}
	/* Free interrupt URB */
	/* psIntfAdapter->psAdapter->device_removed = TRUE; */
	if (psIntfAdapter->psInterruptUrb) {
		usb_free_urb(psIntfAdapter->psInterruptUrb);
	}

	/* Free transmit URBs */
	for (i = 0; i < MAXIMUM_USB_TCB; i++) {
		if (psIntfAdapter->asUsbTcb[i].urb  != NULL) {
			usb_free_urb(psIntfAdapter->asUsbTcb[i].urb);
			psIntfAdapter->asUsbTcb[i].urb = NULL;
		}
	}
	/* Free receive URB and buffers */
	for (i = 0; i < MAXIMUM_USB_RCB; i++) {
		if (psIntfAdapter->asUsbRcb[i].urb != NULL) {
			kfree(psIntfAdapter->asUsbRcb[i].urb->transfer_buffer);
			usb_free_urb(psIntfAdapter->asUsbRcb[i].urb);
			psIntfAdapter->asUsbRcb[i].urb = NULL;
		}
	}
	AdapterFree(psIntfAdapter->psAdapter);
}

static void ConfigureEndPointTypesThroughEEPROM(PMINI_ADAPTER Adapter)
{
	unsigned long ulReg = 0;

	/* Program EP2 MAX_PKT_SIZE */
	ulReg = ntohl(EP2_MPS_REG);
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x128, 4, TRUE);
	ulReg = ntohl(EP2_MPS);
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x12C, 4, TRUE);

	ulReg = ntohl(EP2_CFG_REG);
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x132, 4, TRUE);
	if (((PS_INTERFACE_ADAPTER)(Adapter->pvInterfaceAdapter))->bHighSpeedDevice == TRUE) {
		ulReg = ntohl(EP2_CFG_INT);
		BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x136, 4, TRUE);
	} else {
		/* USE BULK EP as TX in FS mode. */
		ulReg = ntohl(EP2_CFG_BULK);
		BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x136, 4, TRUE);
	}

	/* Program EP4 MAX_PKT_SIZE. */
	ulReg = ntohl(EP4_MPS_REG);
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x13C, 4, TRUE);
	ulReg = ntohl(EP4_MPS);
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x140, 4, TRUE);

	/* Program TX EP as interrupt(Alternate Setting) */
	if (rdmalt(Adapter, 0x0F0110F8, (PUINT)&ulReg, 4)) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "reading of Tx EP is failing");
		return;
	}
	ulReg |= 0x6;

	ulReg = ntohl(ulReg);
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x1CC, 4, TRUE);

	ulReg = ntohl(EP4_CFG_REG);
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x1C8, 4, TRUE);
	/* Program ISOCHRONOUS EP size to zero. */
	ulReg = ntohl(ISO_MPS_REG);
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x1D2, 4, TRUE);
	ulReg = ntohl(ISO_MPS);
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x1D6, 4, TRUE);

	/*
	 * Update EEPROM Version.
	 * Read 4 bytes from 508 and modify 511 and 510.
	 */
	ReadBeceemEEPROM(Adapter, 0x1FC, (PUINT)&ulReg);
	ulReg &= 0x0101FFFF;
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x1FC, 4, TRUE);

	/* Update length field if required. Also make the string NULL terminated. */

	ReadBeceemEEPROM(Adapter, 0xA8, (PUINT)&ulReg);
	if ((ulReg&0x00FF0000)>>16 > 0x30) {
		ulReg = (ulReg&0xFF00FFFF)|(0x30<<16);
		BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0xA8, 4, TRUE);
	}
	ReadBeceemEEPROM(Adapter, 0x148, (PUINT)&ulReg);
	if ((ulReg&0x00FF0000)>>16 > 0x30) {
		ulReg = (ulReg&0xFF00FFFF)|(0x30<<16);
		BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x148, 4, TRUE);
	}
	ulReg = 0;
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x122, 4, TRUE);
	ulReg = 0;
	BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&ulReg, 0x1C2, 4, TRUE);
}

static int
usbbcm_device_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct usb_device *udev = interface_to_usbdev(intf);
	int retval;
	PMINI_ADAPTER psAdapter;
	PS_INTERFACE_ADAPTER psIntfAdapter;
	struct net_device *ndev;

	/* Reserve one extra queue for the bit-bucket */
	ndev = alloc_etherdev_mq(sizeof(MINI_ADAPTER), NO_OF_QUEUES+1);
	if (ndev == NULL) {
		dev_err(&udev->dev, DRV_NAME ": no memory for device\n");
		return -ENOMEM;
	}

	SET_NETDEV_DEV(ndev, &intf->dev);

	psAdapter = netdev_priv(ndev);
	psAdapter->dev = ndev;
	psAdapter->msg_enable = netif_msg_init(debug, default_msg);

	/* Init default driver debug state */

	psAdapter->stDebugState.debug_level = DBG_LVL_CURR;
	psAdapter->stDebugState.type = DBG_TYPE_INITEXIT;

	/*
	 * Technically, one can start using BCM_DEBUG_PRINT after this point.
	 * However, realize that by default the Type/Subtype bitmaps are all zero now;
	 * so no prints will actually appear until the TestApp turns on debug paths via
	 * the ioctl(); so practically speaking, in early init, no logging happens.
	 *
	 * A solution (used below): we explicitly set the bitmaps to 1 for Type=DBG_TYPE_INITEXIT
	 * and ALL subtype's of the same. Now all bcm debug statements get logged, enabling debug
	 * during early init.
	 * Further, we turn this OFF once init_module() completes.
	 */

	psAdapter->stDebugState.subtype[DBG_TYPE_INITEXIT] = 0xff;
	BCM_SHOW_DEBUG_BITMAP(psAdapter);

	retval = InitAdapter(psAdapter);
	if (retval) {
		dev_err(&udev->dev, DRV_NAME ": InitAdapter Failed\n");
		AdapterFree(psAdapter);
		return retval;
	}

	/* Allocate interface adapter structure */
	psIntfAdapter = kzalloc(sizeof(S_INTERFACE_ADAPTER), GFP_KERNEL);
	if (psIntfAdapter == NULL) {
		dev_err(&udev->dev, DRV_NAME ": no memory for Interface adapter\n");
		AdapterFree(psAdapter);
		return -ENOMEM;
	}

	psAdapter->pvInterfaceAdapter = psIntfAdapter;
	psIntfAdapter->psAdapter = psAdapter;

	/* Store usb interface in Interface Adapter */
	psIntfAdapter->interface = intf;
	usb_set_intfdata(intf, psIntfAdapter);

	BCM_DEBUG_PRINT(psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "psIntfAdapter 0x%p", psIntfAdapter);
	retval = InterfaceAdapterInit(psIntfAdapter);
	if (retval) {
		/* If the Firmware/Cfg File is not present
		 * then return success, let the application
		 * download the files.
		 */
		if (-ENOENT == retval) {
			BCM_DEBUG_PRINT(psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "File Not Found, Use App to Download\n");
			return STATUS_SUCCESS;
		}
		BCM_DEBUG_PRINT(psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "InterfaceAdapterInit Failed \n");
		usb_set_intfdata(intf, NULL);
		udev = interface_to_usbdev(intf);
		usb_put_dev(udev);
		InterfaceAdapterFree(psIntfAdapter);
		return retval;
	}
	if (psAdapter->chip_id > T3) {
		uint32_t uiNackZeroLengthInt = 4;

		if (wrmalt(psAdapter, DISABLE_USB_ZERO_LEN_INT, &uiNackZeroLengthInt, sizeof(uiNackZeroLengthInt))) {
			return -EIO;
		}
	}

	/* Check whether the USB-Device Supports remote Wake-Up */
	if (USB_CONFIG_ATT_WAKEUP & udev->actconfig->desc.bmAttributes) {
		/* If Suspend then only support dynamic suspend */
		if (psAdapter->bDoSuspend) {
#ifdef CONFIG_PM
			udev->autosuspend_delay = 0;
			intf->needs_remote_wakeup = 1;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
			udev->autosuspend_disabled = 0;
#else
			usb_enable_autosuspend(udev);
#endif
			device_init_wakeup(&intf->dev, 1);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
			usb_autopm_disable(intf);
#endif
			INIT_WORK(&psIntfAdapter->usbSuspendWork, putUsbSuspend);
			BCM_DEBUG_PRINT(psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Enabling USB Auto-Suspend\n");
#endif
		} else {
			intf->needs_remote_wakeup = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
			udev->autosuspend_disabled = 1;
#else
			usb_disable_autosuspend(udev);
#endif
		}
	}

	psAdapter->stDebugState.subtype[DBG_TYPE_INITEXIT] = 0x0;
	return retval;
}

static void usbbcm_disconnect(struct usb_interface *intf)
{
	PS_INTERFACE_ADAPTER psIntfAdapter = usb_get_intfdata(intf);
	PMINI_ADAPTER psAdapter;
	struct usb_device  *udev = interface_to_usbdev(intf);

	if (psIntfAdapter == NULL)
		return;

	psAdapter = psIntfAdapter->psAdapter;
	netif_device_detach(psAdapter->dev);

	if (psAdapter->bDoSuspend)
		intf->needs_remote_wakeup = 0;

	psAdapter->device_removed = TRUE ;
	usb_set_intfdata(intf, NULL);
	InterfaceAdapterFree(psIntfAdapter);
	usb_put_dev(udev);
}

static int AllocUsbCb(PS_INTERFACE_ADAPTER psIntfAdapter)
{
	int i = 0;

	for (i = 0; i < MAXIMUM_USB_TCB; i++) {
		if ((psIntfAdapter->asUsbTcb[i].urb =
				usb_alloc_urb(0, GFP_KERNEL)) == NULL) {
			BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_PRINTK, 0, 0, "Cant allocate Tx urb for index %d", i);
			return -ENOMEM;
		}
	}

	for (i = 0; i < MAXIMUM_USB_RCB; i++) {
		if ((psIntfAdapter->asUsbRcb[i].urb =
				usb_alloc_urb(0, GFP_KERNEL)) == NULL) {
			BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_PRINTK, 0, 0, "Cant allocate Rx urb for index %d", i);
			return -ENOMEM;
		}
		if ((psIntfAdapter->asUsbRcb[i].urb->transfer_buffer =
			kmalloc(MAX_DATA_BUFFER_SIZE, GFP_KERNEL)) == NULL) {
			BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_PRINTK, 0, 0, "Cant allocate Rx buffer for index %d", i);
			return -ENOMEM;
		}
		psIntfAdapter->asUsbRcb[i].urb->transfer_buffer_length = MAX_DATA_BUFFER_SIZE;
	}
	return 0;
}

static int device_run(PS_INTERFACE_ADAPTER psIntfAdapter)
{
	int value = 0;
	UINT status = STATUS_SUCCESS;

	status = InitCardAndDownloadFirmware(psIntfAdapter->psAdapter);
	if (status != STATUS_SUCCESS) {
		pr_err(DRV_NAME "InitCardAndDownloadFirmware failed.\n");
		return status;
	}
	if (TRUE == psIntfAdapter->psAdapter->fw_download_done) {
		if (StartInterruptUrb(psIntfAdapter)) {
			BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Cannot send interrupt in URB");
		}

		/*
		 * now register the cntrl interface.
		 * after downloading the f/w waiting for 5 sec to get the mailbox interrupt.
		 */
		psIntfAdapter->psAdapter->waiting_to_fw_download_done = FALSE;
		value = wait_event_timeout(psIntfAdapter->psAdapter->ioctl_fw_dnld_wait_queue,
					psIntfAdapter->psAdapter->waiting_to_fw_download_done, 5*HZ);

		if (value == 0)
			pr_err(DRV_NAME ": Mailbox Interrupt has not reached to Driver..\n");

		if (register_control_device_interface(psIntfAdapter->psAdapter) < 0) {
			pr_err(DRV_NAME ": Register Control Device failed...\n");
			return -EIO;
		}
	}
	return 0;
}


static inline int bcm_usb_endpoint_num(const struct usb_endpoint_descriptor *epd)
{
	return epd->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
}

static inline int bcm_usb_endpoint_type(const struct usb_endpoint_descriptor *epd)
{
	return epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK;
}

static inline int bcm_usb_endpoint_dir_in(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN);
}

static inline int bcm_usb_endpoint_dir_out(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT);
}

static inline int bcm_usb_endpoint_xfer_bulk(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		USB_ENDPOINT_XFER_BULK);
}

static inline int bcm_usb_endpoint_xfer_control(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		USB_ENDPOINT_XFER_CONTROL);
}

static inline int bcm_usb_endpoint_xfer_int(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		USB_ENDPOINT_XFER_INT);
}

static inline int bcm_usb_endpoint_xfer_isoc(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) ==
		USB_ENDPOINT_XFER_ISOC);
}

static inline int bcm_usb_endpoint_is_bulk_in(const struct usb_endpoint_descriptor *epd)
{
	return (bcm_usb_endpoint_xfer_bulk(epd) && bcm_usb_endpoint_dir_in(epd));
}

static inline int bcm_usb_endpoint_is_bulk_out(const struct usb_endpoint_descriptor *epd)
{
	return (bcm_usb_endpoint_xfer_bulk(epd) && bcm_usb_endpoint_dir_out(epd));
}

static inline int bcm_usb_endpoint_is_int_in(const struct usb_endpoint_descriptor *epd)
{
	return (bcm_usb_endpoint_xfer_int(epd) && bcm_usb_endpoint_dir_in(epd));
}

static inline int bcm_usb_endpoint_is_int_out(const struct usb_endpoint_descriptor *epd)
{
	return (bcm_usb_endpoint_xfer_int(epd) && bcm_usb_endpoint_dir_out(epd));
}

static inline int bcm_usb_endpoint_is_isoc_in(const struct usb_endpoint_descriptor *epd)
{
	return (bcm_usb_endpoint_xfer_isoc(epd) && bcm_usb_endpoint_dir_in(epd));
}

static inline int bcm_usb_endpoint_is_isoc_out(const struct usb_endpoint_descriptor *epd)
{
	return (bcm_usb_endpoint_xfer_isoc(epd) && bcm_usb_endpoint_dir_out(epd));
}

static int InterfaceAdapterInit(PS_INTERFACE_ADAPTER psIntfAdapter)
{
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	unsigned long value;
	int retval = 0;
	int usedIntOutForBulkTransfer = 0 ;
	BOOLEAN bBcm16 = FALSE;
	UINT uiData = 0;

	/* Store the usb dev into interface adapter */
	psIntfAdapter->udev = usb_get_dev(interface_to_usbdev(psIntfAdapter->interface));

	psIntfAdapter->bHighSpeedDevice = (psIntfAdapter->udev->speed == USB_SPEED_HIGH);
	psIntfAdapter->psAdapter->interface_rdm = BcmRDM;
	psIntfAdapter->psAdapter->interface_wrm = BcmWRM;

	if (rdmalt(psIntfAdapter->psAdapter, CHIP_ID_REG, (PUINT)&(psIntfAdapter->psAdapter->chip_id), sizeof(UINT)) < 0) {
		BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_PRINTK, 0, 0, "CHIP ID Read Failed\n");
		return STATUS_FAILURE;
	}

	if (0xbece3200 == (psIntfAdapter->psAdapter->chip_id & ~(0xF0)))
		psIntfAdapter->psAdapter->chip_id &= ~0xF0;

	dev_info(&psIntfAdapter->udev->dev, "RDM Chip ID 0x%lx\n",
		 psIntfAdapter->psAdapter->chip_id);

	iface_desc = psIntfAdapter->interface->cur_altsetting;

	if (psIntfAdapter->psAdapter->chip_id == T3B) {
		/* T3B device will have EEPROM, check if EEPROM is proper and BCM16 can be done or not. */
		BeceemEEPROMBulkRead(psIntfAdapter->psAdapter, &uiData, 0x0, 4);
		if (uiData == BECM)
			bBcm16 = TRUE;

		dev_info(&psIntfAdapter->udev->dev, "number of alternate setting %d\n",
			 psIntfAdapter->interface->num_altsetting);

		if (bBcm16 == TRUE) {
			/* selecting alternate setting one as a default setting for High Speed  modem. */
			if (psIntfAdapter->bHighSpeedDevice)
				retval= usb_set_interface(psIntfAdapter->udev, DEFAULT_SETTING_0, ALTERNATE_SETTING_1);
			BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "BCM16 is Applicable on this dongle");
			if (retval || (psIntfAdapter->bHighSpeedDevice == FALSE)) {
				usedIntOutForBulkTransfer = EP2 ;
				endpoint = &iface_desc->endpoint[EP2].desc;
				BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Interface altsetting  got failed or Moemd is configured to FS.hence will work on default setting 0 \n");
				/*
				 * If Modem is high speed device EP2 should be INT OUT End point
				 * If Mode is FS then EP2 should be bulk end point
				 */
				if (((psIntfAdapter->bHighSpeedDevice == TRUE) && (bcm_usb_endpoint_is_int_out(endpoint) == FALSE))
					|| ((psIntfAdapter->bHighSpeedDevice == FALSE) && (bcm_usb_endpoint_is_bulk_out(endpoint) == FALSE))) {
					BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Configuring the EEPROM ");
					/* change the EP2, EP4 to INT OUT end point */
					ConfigureEndPointTypesThroughEEPROM(psIntfAdapter->psAdapter);

					/*
					 * It resets the device and if any thing gets changed
					 *  in USB descriptor it will show fail and re-enumerate
					 * the device
					 */
					retval = usb_reset_device(psIntfAdapter->udev);
					if (retval) {
						BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "reset got failed. hence Re-enumerating the device \n");
						return retval ;
					}

				}
				if ((psIntfAdapter->bHighSpeedDevice == FALSE) && bcm_usb_endpoint_is_bulk_out(endpoint)) {
					/* Once BULK is selected in FS mode. Revert it back to INT. Else USB_IF will fail. */
					UINT _uiData = ntohl(EP2_CFG_INT);
					BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Reverting Bulk to INT as it is FS MODE");
					BeceemEEPROMBulkWrite(psIntfAdapter->psAdapter, (PUCHAR)&_uiData, 0x136, 4, TRUE);
				}
			} else {
				usedIntOutForBulkTransfer = EP4 ;
				endpoint = &iface_desc->endpoint[EP4].desc;
				BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Choosing AltSetting as a default setting");
				if (bcm_usb_endpoint_is_int_out(endpoint) == FALSE) {
					BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, " Dongle does not have BCM16 Fix");
					/* change the EP2, EP4 to INT OUT end point and use EP4 in altsetting */
					ConfigureEndPointTypesThroughEEPROM(psIntfAdapter->psAdapter);

					/*
					 * It resets the device and if any thing gets changed in
					 *  USB descriptor it will show fail and re-enumerate the
					 * device
					 */
					retval = usb_reset_device(psIntfAdapter->udev);
					if (retval) {
						BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "reset got failed. hence Re-enumerating the device \n");
						return retval;
					}

				}
			}
		}
	}

	iface_desc = psIntfAdapter->interface->cur_altsetting;

	for (value = 0; value < iface_desc->desc.bNumEndpoints; ++value) {
		endpoint = &iface_desc->endpoint[value].desc;

		if (!psIntfAdapter->sBulkIn.bulk_in_endpointAddr && bcm_usb_endpoint_is_bulk_in(endpoint)) {
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			psIntfAdapter->sBulkIn.bulk_in_size = buffer_size;
			psIntfAdapter->sBulkIn.bulk_in_endpointAddr = endpoint->bEndpointAddress;
			psIntfAdapter->sBulkIn.bulk_in_pipe =
					usb_rcvbulkpipe(psIntfAdapter->udev,
								psIntfAdapter->sBulkIn.bulk_in_endpointAddr);
		}

		if (!psIntfAdapter->sBulkOut.bulk_out_endpointAddr && bcm_usb_endpoint_is_bulk_out(endpoint)) {
			psIntfAdapter->sBulkOut.bulk_out_endpointAddr = endpoint->bEndpointAddress;
			psIntfAdapter->sBulkOut.bulk_out_pipe =
				usb_sndbulkpipe(psIntfAdapter->udev,
					psIntfAdapter->sBulkOut.bulk_out_endpointAddr);
		}

		if (!psIntfAdapter->sIntrIn.int_in_endpointAddr && bcm_usb_endpoint_is_int_in(endpoint)) {
			buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
			psIntfAdapter->sIntrIn.int_in_size = buffer_size;
			psIntfAdapter->sIntrIn.int_in_endpointAddr = endpoint->bEndpointAddress;
			psIntfAdapter->sIntrIn.int_in_interval = endpoint->bInterval;
			psIntfAdapter->sIntrIn.int_in_buffer =
						kmalloc(buffer_size, GFP_KERNEL);
			if (!psIntfAdapter->sIntrIn.int_in_buffer) {
				dev_err(&psIntfAdapter->udev->dev,
					"could not allocate interrupt_in_buffer\n");
				return -EINVAL;
			}
		}

		if (!psIntfAdapter->sIntrOut.int_out_endpointAddr && bcm_usb_endpoint_is_int_out(endpoint)) {
			if (!psIntfAdapter->sBulkOut.bulk_out_endpointAddr &&
				(psIntfAdapter->psAdapter->chip_id == T3B) && (value == usedIntOutForBulkTransfer)) {
				/* use first intout end point as a bulk out end point */
				buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
				psIntfAdapter->sBulkOut.bulk_out_size = buffer_size;
				psIntfAdapter->sBulkOut.bulk_out_endpointAddr = endpoint->bEndpointAddress;
				psIntfAdapter->sBulkOut.bulk_out_pipe = usb_sndintpipe(psIntfAdapter->udev,
									psIntfAdapter->sBulkOut.bulk_out_endpointAddr);
				psIntfAdapter->sBulkOut.int_out_interval = endpoint->bInterval;
			} else if (value == EP6) {
				buffer_size = le16_to_cpu(endpoint->wMaxPacketSize);
				psIntfAdapter->sIntrOut.int_out_size = buffer_size;
				psIntfAdapter->sIntrOut.int_out_endpointAddr = endpoint->bEndpointAddress;
				psIntfAdapter->sIntrOut.int_out_interval = endpoint->bInterval;
				psIntfAdapter->sIntrOut.int_out_buffer= kmalloc(buffer_size, GFP_KERNEL);
				if (!psIntfAdapter->sIntrOut.int_out_buffer) {
					dev_err(&psIntfAdapter->udev->dev,
						"could not allocate interrupt_out_buffer\n");
					return -EINVAL;
				}
			}
		}
	}

	usb_set_intfdata(psIntfAdapter->interface, psIntfAdapter);

	psIntfAdapter->psAdapter->bcm_file_download = InterfaceFileDownload;
	psIntfAdapter->psAdapter->bcm_file_readback_from_chip =
				InterfaceFileReadbackFromChip;
	psIntfAdapter->psAdapter->interface_transmit = InterfaceTransmitPacket;

	retval = CreateInterruptUrb(psIntfAdapter);

	if (retval) {
		BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_PRINTK, 0, 0, "Cannot create interrupt urb");
		return retval;
	}

	retval = AllocUsbCb(psIntfAdapter);
	if (retval) {
		return retval;
	}

	return device_run(psIntfAdapter);
}

static int InterfaceSuspend(struct usb_interface *intf, pm_message_t message)
{
	PS_INTERFACE_ADAPTER  psIntfAdapter = usb_get_intfdata(intf);

	psIntfAdapter->bSuspended = TRUE;

	if (TRUE == psIntfAdapter->bPreparingForBusSuspend) {
		psIntfAdapter->bPreparingForBusSuspend = FALSE;

		if (psIntfAdapter->psAdapter->LinkStatus == LINKUP_DONE) {
			psIntfAdapter->psAdapter->IdleMode = TRUE ;
			BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Host Entered in PMU Idle Mode..");
		} else {
			psIntfAdapter->psAdapter->bShutStatus = TRUE;
			BCM_DEBUG_PRINT(psIntfAdapter->psAdapter, DBG_TYPE_INITEXIT, DRV_ENTRY, DBG_LVL_ALL, "Host Entered in PMU Shutdown Mode..");
		}
	}
	psIntfAdapter->psAdapter->bPreparingForLowPowerMode = FALSE;

	/* Signaling the control pkt path */
	wake_up(&psIntfAdapter->psAdapter->lowpower_mode_wait_queue);

	return 0;
}

static int InterfaceResume(struct usb_interface *intf)
{
	PS_INTERFACE_ADAPTER  psIntfAdapter = usb_get_intfdata(intf);
	printk("=================================\n");
	mdelay(100);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 32)
	intf->pm_usage_cnt =1 ;
#endif
	psIntfAdapter->bSuspended = FALSE;

	StartInterruptUrb(psIntfAdapter);
	InterfaceRx(psIntfAdapter);
	return 0;
}

static struct usb_driver usbbcm_driver = {
	.name = "usbbcm",
	.probe = usbbcm_device_probe,
	.disconnect = usbbcm_disconnect,
	.suspend = InterfaceSuspend,
	.resume = InterfaceResume,
	.id_table = InterfaceUsbtable,
	.supports_autosuspend = 1,
};

struct class *bcm_class;

static __init int bcm_init(void)
{
	printk(KERN_INFO "%s: %s, %s\n", DRV_NAME, DRV_DESCRIPTION, DRV_VERSION);
	printk(KERN_INFO "%s\n", DRV_COPYRIGHT);

	bcm_class = class_create(THIS_MODULE, DRV_NAME);
	if (IS_ERR(bcm_class)) {
		printk(KERN_ERR DRV_NAME ": could not create class\n");
		return PTR_ERR(bcm_class);
	}

	return usb_register(&usbbcm_driver);
}

static __exit void bcm_exit(void)
{
	class_destroy(bcm_class);

	usb_deregister(&usbbcm_driver);
}

module_init(bcm_init);
module_exit(bcm_exit);

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
