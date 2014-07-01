/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#define _HCI_INTF_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <hal_intf.h>
#include <rtw_version.h>
#include <osdep_intf.h>
#include <usb_ops.h>
#include <rtl8723a_hal.h>

static int rtw_suspend(struct usb_interface *intf, pm_message_t message);
static int rtw_resume(struct usb_interface *intf);
static int rtw_drv_init(struct usb_interface *pusb_intf,
			const struct usb_device_id *pdid);
static void rtw_disconnect(struct usb_interface *pusb_intf);

#define USB_VENDER_ID_REALTEK		0x0BDA

#define RTL8723A_USB_IDS \
	{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDER_ID_REALTEK, 0x8724,	\
	 0xff, 0xff, 0xff)}, /* 8723AU 1*1 */ \
	{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDER_ID_REALTEK, 0x1724,	\
	 0xff, 0xff, 0xff)}, /* 8723AU 1*1 */ \
	{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDER_ID_REALTEK, 0x0724,	\
	 0xff, 0xff, 0xff)}, /* 8723AU 1*1 */

static struct usb_device_id rtl8723a_usb_id_tbl[] = {
	RTL8723A_USB_IDS
	{}	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, rtl8723a_usb_id_tbl);

static struct usb_driver rtl8723a_usb_drv = {
	.name = (char *)"rtl8723au",
	.probe = rtw_drv_init,
	.disconnect = rtw_disconnect,
	.id_table = rtl8723a_usb_id_tbl,
	.suspend = rtw_suspend,
	.resume = rtw_resume,
	.reset_resume  = rtw_resume,
};

static struct usb_driver *usb_drv = &rtl8723a_usb_drv;

static inline int RT_usb_endpoint_dir_in(const struct usb_endpoint_descriptor *epd)
{
	return (epd->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN;
}

static inline int RT_usb_endpoint_dir_out(const struct usb_endpoint_descriptor *epd)
{
	return (epd->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT;
}

static inline int RT_usb_endpoint_xfer_int(const struct usb_endpoint_descriptor *epd)
{
	return (epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT;
}

static inline int RT_usb_endpoint_xfer_bulk(const struct usb_endpoint_descriptor *epd)
{
	return (epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK;
}

static inline int RT_usb_endpoint_is_bulk_in(const struct usb_endpoint_descriptor *epd)
{
	return RT_usb_endpoint_xfer_bulk(epd) && RT_usb_endpoint_dir_in(epd);
}

static inline int RT_usb_endpoint_is_bulk_out(const struct usb_endpoint_descriptor *epd)
{
	return RT_usb_endpoint_xfer_bulk(epd) && RT_usb_endpoint_dir_out(epd);
}

static inline int RT_usb_endpoint_is_int_in(const struct usb_endpoint_descriptor *epd)
{
	return RT_usb_endpoint_xfer_int(epd) && RT_usb_endpoint_dir_in(epd);
}

static inline int RT_usb_endpoint_num(const struct usb_endpoint_descriptor *epd)
{
	return epd->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
}

static int rtw_init_intf_priv(struct dvobj_priv *dvobj)
{
	int rst = _SUCCESS;

	mutex_init(&dvobj->usb_vendor_req_mutex);
	dvobj->usb_alloc_vendor_req_buf = kzalloc(MAX_USB_IO_CTL_SIZE,
						  GFP_KERNEL);
	if (dvobj->usb_alloc_vendor_req_buf == NULL) {
		DBG_8723A("alloc usb_vendor_req_buf failed...\n");
		rst = _FAIL;
		goto exit;
	}
	dvobj->usb_vendor_req_buf =
		PTR_ALIGN(dvobj->usb_alloc_vendor_req_buf, ALIGNMENT_UNIT);
exit:
	return rst;
}

static int rtw_deinit_intf_priv(struct dvobj_priv *dvobj)
{
	int rst = _SUCCESS;

	kfree(dvobj->usb_alloc_vendor_req_buf);

	mutex_destroy(&dvobj->usb_vendor_req_mutex);

	return rst;
}

static struct dvobj_priv *usb_dvobj_init(struct usb_interface *usb_intf)
{
	struct dvobj_priv *pdvobjpriv;
	struct usb_device_descriptor *pdev_desc;
	struct usb_host_config	 *phost_conf;
	struct usb_config_descriptor *pconf_desc;
	struct usb_host_interface *phost_iface;
	struct usb_interface_descriptor *piface_desc;
	struct usb_host_endpoint *phost_endp;
	struct usb_endpoint_descriptor *pendp_desc;
	struct usb_device		 *pusbd;
	int	i;
	int	status = _FAIL;

	pdvobjpriv = kzalloc(sizeof(*pdvobjpriv), GFP_KERNEL);
	if (!pdvobjpriv)
		goto exit;

	mutex_init(&pdvobjpriv->hw_init_mutex);
	mutex_init(&pdvobjpriv->h2c_fwcmd_mutex);
	mutex_init(&pdvobjpriv->setch_mutex);
	mutex_init(&pdvobjpriv->setbw_mutex);

	pdvobjpriv->pusbintf = usb_intf;
	pusbd = interface_to_usbdev(usb_intf);
	pdvobjpriv->pusbdev = pusbd;
	usb_set_intfdata(usb_intf, pdvobjpriv);

	pdvobjpriv->RtNumInPipes = 0;
	pdvobjpriv->RtNumOutPipes = 0;

	pdev_desc = &pusbd->descriptor;

	phost_conf = pusbd->actconfig;
	pconf_desc = &phost_conf->desc;

	phost_iface = &usb_intf->altsetting[0];
	piface_desc = &phost_iface->desc;

	pdvobjpriv->NumInterfaces = pconf_desc->bNumInterfaces;
	pdvobjpriv->InterfaceNumber = piface_desc->bInterfaceNumber;
	pdvobjpriv->nr_endpoint = piface_desc->bNumEndpoints;

	for (i = 0; i < pdvobjpriv->nr_endpoint; i++) {
		phost_endp = phost_iface->endpoint + i;
		if (phost_endp) {
			pendp_desc = &phost_endp->desc;

			DBG_8723A("\nusb_endpoint_descriptor(%d):\n", i);
			DBG_8723A("bLength =%x\n", pendp_desc->bLength);
			DBG_8723A("bDescriptorType =%x\n",
				  pendp_desc->bDescriptorType);
			DBG_8723A("bEndpointAddress =%x\n",
				  pendp_desc->bEndpointAddress);
			DBG_8723A("wMaxPacketSize =%d\n",
				  le16_to_cpu(pendp_desc->wMaxPacketSize));
			DBG_8723A("bInterval =%x\n", pendp_desc->bInterval);

			if (RT_usb_endpoint_is_bulk_in(pendp_desc)) {
				DBG_8723A("RT_usb_endpoint_is_bulk_in = %x\n",
					  RT_usb_endpoint_num(pendp_desc));
				pdvobjpriv->RtInPipe[pdvobjpriv->RtNumInPipes] =
					RT_usb_endpoint_num(pendp_desc);
				pdvobjpriv->RtNumInPipes++;
			} else if (RT_usb_endpoint_is_int_in(pendp_desc)) {
				DBG_8723A("RT_usb_endpoint_is_int_in = %x, Interval = %x\n",
					  RT_usb_endpoint_num(pendp_desc),
					  pendp_desc->bInterval);
				pdvobjpriv->RtInPipe[pdvobjpriv->RtNumInPipes] =
					RT_usb_endpoint_num(pendp_desc);
				pdvobjpriv->RtNumInPipes++;
			} else if (RT_usb_endpoint_is_bulk_out(pendp_desc)) {
				DBG_8723A("RT_usb_endpoint_is_bulk_out = %x\n",
					  RT_usb_endpoint_num(pendp_desc));
				pdvobjpriv->RtOutPipe[pdvobjpriv->RtNumOutPipes] =
					RT_usb_endpoint_num(pendp_desc);
				pdvobjpriv->RtNumOutPipes++;
			}
			pdvobjpriv->ep_num[i] = RT_usb_endpoint_num(pendp_desc);
		}
	}
	DBG_8723A("nr_endpoint =%d, in_num =%d, out_num =%d\n\n",
		  pdvobjpriv->nr_endpoint, pdvobjpriv->RtNumInPipes,
		  pdvobjpriv->RtNumOutPipes);

	if (pusbd->speed == USB_SPEED_HIGH) {
		pdvobjpriv->ishighspeed = true;
		DBG_8723A("USB_SPEED_HIGH\n");
	} else {
		pdvobjpriv->ishighspeed = false;
		DBG_8723A("NON USB_SPEED_HIGH\n");
	}

	if (rtw_init_intf_priv(pdvobjpriv) == _FAIL) {
		RT_TRACE(_module_os_intfs_c_, _drv_err_,
			 ("\n Can't INIT rtw_init_intf_priv\n"));
		goto free_dvobj;
	}
	/* 3 misc */
	rtw_reset_continual_urb_error(pdvobjpriv);
	usb_get_dev(pusbd);
	status = _SUCCESS;
free_dvobj:
	if (status != _SUCCESS && pdvobjpriv) {
		usb_set_intfdata(usb_intf, NULL);
		mutex_destroy(&pdvobjpriv->hw_init_mutex);
		mutex_destroy(&pdvobjpriv->h2c_fwcmd_mutex);
		mutex_destroy(&pdvobjpriv->setch_mutex);
		mutex_destroy(&pdvobjpriv->setbw_mutex);
		kfree(pdvobjpriv);
		pdvobjpriv = NULL;
	}
exit:
	return pdvobjpriv;
}

static void usb_dvobj_deinit(struct usb_interface *usb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(usb_intf);

	usb_set_intfdata(usb_intf, NULL);
	if (dvobj) {
		/* Modify condition for 92DU DMDP 2010.11.18, by Thomas */
		if ((dvobj->NumInterfaces != 2 && dvobj->NumInterfaces != 3) ||
		    (dvobj->InterfaceNumber == 1)) {
			if (interface_to_usbdev(usb_intf)->state !=
			    USB_STATE_NOTATTACHED) {
				/* If we didn't unplug usb dongle and
				 * remove/insert module, driver fails on
				 * sitesurvey for the first time when
				 * device is up .
				 * Reset usb port for sitesurvey fail issue.
				 */
				DBG_8723A("usb attached..., try to reset usb device\n");
				usb_reset_device(interface_to_usbdev(usb_intf));
			}
		}
		rtw_deinit_intf_priv(dvobj);
		mutex_destroy(&dvobj->hw_init_mutex);
		mutex_destroy(&dvobj->h2c_fwcmd_mutex);
		mutex_destroy(&dvobj->setch_mutex);
		mutex_destroy(&dvobj->setbw_mutex);
		kfree(dvobj);
	}
	usb_put_dev(interface_to_usbdev(usb_intf));
}

void rtl8723a_usb_intf_stop(struct rtw_adapter *padapter)
{
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+usb_intf_stop\n"));

	/* disable_hw_interrupt */
	if (!padapter->bSurpriseRemoved) {
		/* device still exists, so driver can do i/o operation
		 * TODO:
		 */
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("SurpriseRemoved == false\n"));
	}

	/* cancel in irp */
	rtl8723au_inirp_deinit(padapter);

	/* cancel out irp */
	rtl8723au_write_port_cancel(padapter);

	/* todo:cancel other irps */
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("-usb_intf_stop\n"));
}

static void rtw_dev_unload(struct rtw_adapter *padapter)
{
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+rtw_dev_unload\n"));

	if (padapter->bup) {
		DBG_8723A("===> rtw_dev_unload\n");

		padapter->bDriverStopped = true;
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done23a(&padapter->xmitpriv,
					RTW_SCTX_DONE_DRV_STOP);

		/* s3. */
		rtl8723a_usb_intf_stop(padapter);

		/* s4. */
		flush_workqueue(padapter->cmdpriv.wq);

		/* s5. */
		if (!padapter->bSurpriseRemoved) {
			rtw_hal_deinit23a(padapter);
			padapter->bSurpriseRemoved = true;
		}
		padapter->bup = false;
	} else {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("r871x_dev_unload():padapter->bup == false\n"));
	}
	DBG_8723A("<=== rtw_dev_unload\n");
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("-rtw_dev_unload\n"));
}

int rtw_hw_suspend23a(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct net_device *pnetdev = padapter->pnetdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if ((!padapter->bup) || (padapter->bDriverStopped) ||
	    (padapter->bSurpriseRemoved)) {
		DBG_8723A("padapter->bup =%d bDriverStopped =%d bSurpriseRemoved = %d\n",
			  padapter->bup, padapter->bDriverStopped,
			  padapter->bSurpriseRemoved);
		goto error_exit;
	}

	if (padapter) { /* system suspend */
		LeaveAllPowerSaveMode23a(padapter);

		DBG_8723A("==> rtw_hw_suspend23a\n");
		down(&pwrpriv->lock);
		pwrpriv->bips_processing = true;
		/* padapter->net_closed = true; */
		/* s1. */
		if (pnetdev) {
			netif_carrier_off(pnetdev);
			netif_tx_stop_all_queues(pnetdev);
		}

		/* s2. */
		rtw_disassoc_cmd23a(padapter, 500, false);

		/* s2-2.  indicate disconnect to os */
		/* rtw_indicate_disconnect23a(padapter); */
		if (check_fwstate(pmlmepriv, _FW_LINKED)) {
			_clr_fwstate_(pmlmepriv, _FW_LINKED);

			rtw_led_control(padapter, LED_CTL_NO_LINK);

			rtw_os_indicate_disconnect23a(padapter);

			/* donnot enqueue cmd */
			rtw_lps_ctrl_wk_cmd23a(padapter,
					       LPS_CTRL_DISCONNECT, 0);
		}
		/* s2-3. */
		rtw_free_assoc_resources23a(padapter, 1);

		/* s2-4. */
		rtw_free_network_queue23a(padapter);
		rtw_ips_dev_unload23a(padapter);
		pwrpriv->rf_pwrstate = rf_off;
		pwrpriv->bips_processing = false;
		up(&pwrpriv->lock);
	} else {
		goto error_exit;
	}
	return 0;
error_exit:
	DBG_8723A("%s, failed\n", __func__);
	return -1;
}

int rtw_hw_resume23a(struct rtw_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct net_device *pnetdev = padapter->pnetdev;

	if (padapter) { /* system resume */
		DBG_8723A("==> rtw_hw_resume23a\n");
		down(&pwrpriv->lock);
		pwrpriv->bips_processing = true;
		rtw_reset_drv_sw23a(padapter);

		if (pm_netdev_open23a(pnetdev, false)) {
			up(&pwrpriv->lock);
			goto error_exit;
		}

		netif_device_attach(pnetdev);
		netif_carrier_on(pnetdev);

		if (!rtw_netif_queue_stopped(pnetdev))
			netif_tx_start_all_queues(pnetdev);
		else
			netif_tx_wake_all_queues(pnetdev);

		pwrpriv->bkeepfwalive = false;

		pwrpriv->rf_pwrstate = rf_on;
		pwrpriv->bips_processing = false;

		up(&pwrpriv->lock);
	} else {
		goto error_exit;
	}
	return 0;
error_exit:
	DBG_8723A("%s, Open net dev failed\n", __func__);
	return -1;
}

static int rtw_suspend(struct usb_interface *pusb_intf, pm_message_t message)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	struct rtw_adapter *padapter = dvobj->if1;
	struct net_device *pnetdev = padapter->pnetdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	int ret = 0;
	unsigned long start_time = jiffies;

	DBG_8723A("==> %s (%s:%d)\n", __func__, current->comm, current->pid);

	if ((!padapter->bup) || (padapter->bDriverStopped) ||
	    (padapter->bSurpriseRemoved)) {
		DBG_8723A("padapter->bup =%d bDriverStopped =%d bSurpriseRemoved = %d\n",
			  padapter->bup, padapter->bDriverStopped,
			  padapter->bSurpriseRemoved);
		goto exit;
	}
	pwrpriv->bInSuspend = true;
	rtw_cancel_all_timer23a(padapter);
	LeaveAllPowerSaveMode23a(padapter);

	down(&pwrpriv->lock);
	/* padapter->net_closed = true; */
	/* s1. */
	if (pnetdev) {
		netif_carrier_off(pnetdev);
		netif_tx_stop_all_queues(pnetdev);
	}

	/* s2. */
	rtw_disassoc_cmd23a(padapter, 0, false);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) &&
	    check_fwstate(pmlmepriv, _FW_LINKED)) {
		DBG_8723A("%s:%d %s(%pM), length:%d assoc_ssid.length:%d\n",
			  __func__, __LINE__,
			  pmlmepriv->cur_network.network.Ssid.ssid,
			  pmlmepriv->cur_network.network.MacAddress,
			  pmlmepriv->cur_network.network.Ssid.ssid_len,
			  pmlmepriv->assoc_ssid.ssid_len);

		rtw_set_roaming(padapter, 1);
	}
	/* s2-2.  indicate disconnect to os */
	rtw_indicate_disconnect23a(padapter);
	/* s2-3. */
	rtw_free_assoc_resources23a(padapter, 1);
	/* s2-4. */
	rtw_free_network_queue23a(padapter);

	rtw_dev_unload(padapter);
	up(&pwrpriv->lock);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_cfg80211_indicate_scan_done(
			wdev_to_priv(padapter->rtw_wdev), true);

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		rtw_indicate_disconnect23a(padapter);

exit:
	DBG_8723A("<===  %s return %d.............. in %dms\n", __func__,
		  ret, jiffies_to_msecs(jiffies - start_time));

	return ret;
}

static int rtw_resume(struct usb_interface *pusb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	struct rtw_adapter *padapter = dvobj->if1;
	int ret;

	ret = rtw_resume_process23a(padapter);

	return ret;
}

int rtw_resume_process23a(struct rtw_adapter *padapter)
{
	struct net_device *pnetdev;
	struct pwrctrl_priv *pwrpriv = NULL;
	int ret = -1;
	unsigned long start_time = jiffies;

	DBG_8723A("==> %s (%s:%d)\n", __func__, current->comm, current->pid);

	if (!padapter)
		goto exit;
	pnetdev = padapter->pnetdev;
	pwrpriv = &padapter->pwrctrlpriv;

	down(&pwrpriv->lock);
	rtw_reset_drv_sw23a(padapter);
	pwrpriv->bkeepfwalive = false;

	DBG_8723A("bkeepfwalive(%x)\n", pwrpriv->bkeepfwalive);
	if (pm_netdev_open23a(pnetdev, true) != 0)
		goto exit;

	netif_device_attach(pnetdev);
	netif_carrier_on(pnetdev);

	up(&pwrpriv->lock);

	if (padapter->pid[1] != 0) {
		DBG_8723A("pid[1]:%d\n", padapter->pid[1]);
		kill_pid(find_vpid(padapter->pid[1]), SIGUSR2, 1);
	}

	rtw23a_roaming(padapter, NULL);

	ret = 0;
exit:
	if (pwrpriv)
		pwrpriv->bInSuspend = false;
	DBG_8723A("<===  %s return %d.............. in %dms\n", __func__,
		  ret, jiffies_to_msecs(jiffies - start_time));

	return ret;
}

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located a card
 * for us to support.
 *        We accept the new device by returning 0.
 */
static struct rtw_adapter *rtw_usb_if1_init(struct dvobj_priv *dvobj,
					    struct usb_interface *pusb_intf,
					    const struct usb_device_id *pdid)
{
	struct rtw_adapter *padapter = NULL;
	struct net_device *pnetdev = NULL;
	int status = _FAIL;

	pnetdev = rtw_init_netdev23a(padapter);
	if (!pnetdev)
		goto free_adapter;
	padapter = netdev_priv(pnetdev);

	padapter->dvobj = dvobj;
	padapter->bDriverStopped = true;
	dvobj->if1 = padapter;
	dvobj->padapters[dvobj->iface_nums++] = padapter;
	padapter->iface_id = IFACE_ID0;

	rtl8723au_set_hw_type(padapter);

	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(dvobj));

	if (rtw_wdev_alloc(padapter, dvobj_to_dev(dvobj)))
		goto free_adapter;

	/* step 2. allocate HalData */
	padapter->HalData = kzalloc(sizeof(struct hal_data_8723a), GFP_KERNEL);
	if (!padapter->HalData)
		goto free_wdev;

	/* step read_chip_version */
	rtl8723a_read_chip_version(padapter);

	/* step usb endpoint mapping */
	rtl8723au_chip_configure(padapter);

	/* step read efuse/eeprom data and get mac_addr */
	rtl8723a_read_adapter_info(padapter);

	/* step 5. */
	if (rtw_init_drv_sw23a(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("Initialize driver software resource Failed!\n"));
		goto free_hal_data;
	}

#ifdef CONFIG_PM
	if (padapter->pwrctrlpriv.bSupportRemoteWakeup) {
		dvobj->pusbdev->do_remote_wakeup = 1;
		pusb_intf->needs_remote_wakeup = 1;
		device_init_wakeup(&pusb_intf->dev, 1);
		DBG_8723A("\n  padapter->pwrctrlpriv.bSupportRemoteWakeup~~~~~~\n");
		DBG_8723A("\n  padapter->pwrctrlpriv.bSupportRemoteWakeup~~~[%d]~~~\n",
			  device_may_wakeup(&pusb_intf->dev));
	}
#endif
	/* 2012-07-11 Move here to prevent the 8723AS-VAU BT
	 * auto suspend influence
	 */
	if (usb_autopm_get_interface(pusb_intf) < 0)
		DBG_8723A("can't get autopm:\n");
#ifdef	CONFIG_8723AU_BT_COEXIST
	padapter->pwrctrlpriv.autopm_cnt = 1;
#endif

	/* If the eeprom mac address is corrupted, assign a random address */
	if (is_broadcast_ether_addr(padapter->eeprompriv.mac_addr) ||
	    is_zero_ether_addr(padapter->eeprompriv.mac_addr))
		eth_random_addr(padapter->eeprompriv.mac_addr);

	DBG_8723A("bDriverStopped:%d, bSurpriseRemoved:%d, bup:%d, hw_init_completed:%d\n",
		  padapter->bDriverStopped, padapter->bSurpriseRemoved,
		  padapter->bup, padapter->hw_init_completed
	);
	status = _SUCCESS;

free_hal_data:
	if (status != _SUCCESS)
		kfree(padapter->HalData);
free_wdev:
	if (status != _SUCCESS) {
		rtw_wdev_unregister(padapter->rtw_wdev);
		rtw_wdev_free(padapter->rtw_wdev);
	}
free_adapter:
	if (status != _SUCCESS) {
		if (pnetdev)
			free_netdev(pnetdev);
		padapter = NULL;
	}
	return padapter;
}

static void rtw_usb_if1_deinit(struct rtw_adapter *if1)
{
	struct net_device *pnetdev = if1->pnetdev;
	struct mlme_priv *pmlmepriv = &if1->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd23a(if1, 0, false);

#ifdef CONFIG_8723AU_AP_MODE
	free_mlme_ap_info23a(if1);
#endif

	if (pnetdev)
		unregister_netdev(pnetdev); /* will call netdev_close() */

	rtw_cancel_all_timer23a(if1);

	rtw_dev_unload(if1);

	DBG_8723A("+r871xu_dev_remove, hw_init_completed =%d\n",
		  if1->hw_init_completed);

	if (if1->rtw_wdev) {
		rtw_wdev_unregister(if1->rtw_wdev);
		rtw_wdev_free(if1->rtw_wdev);
	}

#ifdef CONFIG_8723AU_BT_COEXIST
	if (1 == if1->pwrctrlpriv.autopm_cnt) {
		usb_autopm_put_interface(adapter_to_dvobj(if1)->pusbintf);
		if1->pwrctrlpriv.autopm_cnt--;
	}
#endif

	rtw_free_drv_sw23a(if1);

	if (pnetdev)
		free_netdev(pnetdev);
}

static int rtw_drv_init(struct usb_interface *pusb_intf,
			const struct usb_device_id *pdid)
{
	struct rtw_adapter *if1 = NULL;
	struct dvobj_priv *dvobj;
	int status = _FAIL;

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+rtw_drv_init\n"));

	/* Initialize dvobj_priv */
	dvobj = usb_dvobj_init(pusb_intf);
	if (!dvobj) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("initialize device object priv Failed!\n"));
		goto exit;
	}

	if1 = rtw_usb_if1_init(dvobj, pusb_intf, pdid);
	if (!if1) {
		DBG_8723A("rtw_init_primary_adapter Failed!\n");
		goto free_dvobj;
	}

	/* dev_alloc_name && register_netdev */
	status = rtw_drv_register_netdev(if1);
	if (status != _SUCCESS)
		goto free_if1;
	RT_TRACE(_module_hci_intfs_c_, _drv_err_,
		 ("-871x_drv - drv_init, success!\n"));

	status = _SUCCESS;

free_if1:
	if (status != _SUCCESS && if1)
		rtw_usb_if1_deinit(if1);
free_dvobj:
	if (status != _SUCCESS)
		usb_dvobj_deinit(pusb_intf);
exit:
	return status == _SUCCESS ? 0 : -ENODEV;
}

/* dev_remove() - our device is being removed */
static void rtw_disconnect(struct usb_interface *pusb_intf)
{
	struct dvobj_priv *dvobj;
	struct rtw_adapter *padapter;
	struct net_device *pnetdev;
	struct mlme_priv *pmlmepriv;

	dvobj = usb_get_intfdata(pusb_intf);
	if (!dvobj)
		return;

	padapter = dvobj->if1;
	pnetdev = padapter->pnetdev;
	pmlmepriv = &padapter->mlmepriv;

	usb_set_intfdata(pusb_intf, NULL);

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+dev_remove()\n"));

	rtw_pm_set_ips23a(padapter, IPS_NONE);
	rtw_pm_set_lps23a(padapter, PS_MODE_ACTIVE);

	LeaveAllPowerSaveMode23a(padapter);

	rtw_usb_if1_deinit(padapter);

	usb_dvobj_deinit(pusb_intf);

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("-dev_remove()\n"));
	DBG_8723A("-r871xu_dev_remove, done\n");

	return;
}

static int __init rtw_drv_entry(void)
{
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+rtw_drv_entry\n"));
	return usb_register(usb_drv);
}

static void __exit rtw_drv_halt(void)
{
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+rtw_drv_halt\n"));
	DBG_8723A("+rtw_drv_halt\n");

	usb_deregister(usb_drv);

	DBG_8723A("-rtw_drv_halt\n");
}

module_init(rtw_drv_entry);
module_exit(rtw_drv_halt);
