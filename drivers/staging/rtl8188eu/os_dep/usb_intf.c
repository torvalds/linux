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

#define pr_fmt(fmt) "R8188EU: " fmt
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <hal_intf.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <mon.h>
#include <osdep_intf.h>

#include <usb_ops_linux.h>
#include <rtw_ioctl.h>

#include "rtl8188e_hal.h"

#define USB_VENDER_ID_REALTEK		0x0bda

/* DID_USB_v916_20130116 */
static struct usb_device_id rtw_usb_id_tbl[] = {
	/*=== Realtek demoboard ===*/
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8179)}, /* 8188EUS */
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x0179)}, /* 8188ETV */
	/*=== Customer ID ===*/
	/****** 8188EUS ********/
	{USB_DEVICE(0x056e, 0x4008)}, /* Elecom WDC-150SU2M */
	{USB_DEVICE(0x07b8, 0x8179)}, /* Abocom - Abocom */
	{USB_DEVICE(0x2001, 0x330F)}, /* DLink DWA-125 REV D1 */
	{USB_DEVICE(0x2001, 0x3310)}, /* Dlink DWA-123 REV D1 */
	{USB_DEVICE(0x2001, 0x3311)}, /* DLink GO-USB-N150 REV B1 */
	{USB_DEVICE(0x2357, 0x010c)}, /* TP-Link TL-WN722N v2 */
	{USB_DEVICE(0x0df6, 0x0076)}, /* Sitecom N150 v2 */
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0xffef)}, /* Rosewill RNX-N150NUB */
	{}	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, rtw_usb_id_tbl);

static struct dvobj_priv *usb_dvobj_init(struct usb_interface *usb_intf)
{
	int	i;
	struct dvobj_priv *pdvobjpriv;
	struct usb_host_config		*phost_conf;
	struct usb_config_descriptor	*pconf_desc;
	struct usb_host_interface	*phost_iface;
	struct usb_interface_descriptor	*piface_desc;
	struct usb_endpoint_descriptor	*pendp_desc;
	struct usb_device	*pusbd;

	pdvobjpriv = kzalloc(sizeof(*pdvobjpriv), GFP_KERNEL);
	if (!pdvobjpriv)
		return NULL;

	pdvobjpriv->pusbintf = usb_intf;
	pusbd = interface_to_usbdev(usb_intf);
	pdvobjpriv->pusbdev = pusbd;
	usb_set_intfdata(usb_intf, pdvobjpriv);

	pdvobjpriv->RtNumInPipes = 0;
	pdvobjpriv->RtNumOutPipes = 0;

	phost_conf = pusbd->actconfig;
	pconf_desc = &phost_conf->desc;

	phost_iface = &usb_intf->altsetting[0];
	piface_desc = &phost_iface->desc;

	pdvobjpriv->NumInterfaces = pconf_desc->bNumInterfaces;
	pdvobjpriv->InterfaceNumber = piface_desc->bInterfaceNumber;

	for (i = 0; i < piface_desc->bNumEndpoints; i++) {
		int ep_num;

		pendp_desc = &phost_iface->endpoint[i].desc;

		ep_num = usb_endpoint_num(pendp_desc);

		if (usb_endpoint_is_bulk_in(pendp_desc)) {
			pdvobjpriv->RtInPipe[pdvobjpriv->RtNumInPipes] = ep_num;
			pdvobjpriv->RtNumInPipes++;
		} else if (usb_endpoint_is_int_in(pendp_desc)) {
			pdvobjpriv->RtInPipe[pdvobjpriv->RtNumInPipes] = ep_num;
			pdvobjpriv->RtNumInPipes++;
		} else if (usb_endpoint_is_bulk_out(pendp_desc)) {
			pdvobjpriv->RtOutPipe[pdvobjpriv->RtNumOutPipes] =
				ep_num;
			pdvobjpriv->RtNumOutPipes++;
		}
	}

	if (pusbd->speed == USB_SPEED_HIGH)
		pdvobjpriv->ishighspeed = true;
	else
		pdvobjpriv->ishighspeed = false;

	mutex_init(&pdvobjpriv->usb_vendor_req_mutex);
	usb_get_dev(pusbd);

	return pdvobjpriv;
}

static void usb_dvobj_deinit(struct usb_interface *usb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(usb_intf);

	usb_set_intfdata(usb_intf, NULL);
	if (dvobj) {
		/* Modify condition for 92DU DMDP 2010.11.18, by Thomas */
		if ((dvobj->NumInterfaces != 2 &&
		    dvobj->NumInterfaces != 3) ||
		    (dvobj->InterfaceNumber == 1)) {
			if (interface_to_usbdev(usb_intf)->state !=
			    USB_STATE_NOTATTACHED) {
				/* If we didn't unplug usb dongle and
				 * remove/insert module, driver fails
				 * on sitesurvey for the first time when
				 * device is up . Reset usb port for sitesurvey
				 * fail issue. */
				pr_debug("usb attached..., try to reset usb device\n");
				usb_reset_device(interface_to_usbdev(usb_intf));
			}
		}

		mutex_destroy(&dvobj->usb_vendor_req_mutex);
		kfree(dvobj);
	}

	usb_put_dev(interface_to_usbdev(usb_intf));

}

void usb_intf_stop(struct adapter *padapter)
{
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+usb_intf_stop\n"));

	/* disable_hw_interrupt */
	if (!padapter->bSurpriseRemoved) {
		/* device still exists, so driver can do i/o operation */
		/* TODO: */
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("SurpriseRemoved == false\n"));
	}

	/* cancel in irp */
	rtw_hal_inirp_deinit(padapter);

	/* cancel out irp */
	usb_write_port_cancel(padapter);

	/* todo:cancel other irps */

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("-usb_intf_stop\n"));
}

static void rtw_dev_unload(struct adapter *padapter)
{
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+rtw_dev_unload\n"));

	if (padapter->bup) {
		pr_debug("===> rtw_dev_unload\n");
		padapter->bDriverStopped = true;
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
		/* s3. */
		usb_intf_stop(padapter);
		/* s4. */
		if (!padapter->pwrctrlpriv.bInternalAutoSuspend)
			rtw_stop_drv_threads(padapter);

		/* s5. */
		if (!padapter->bSurpriseRemoved) {
			rtw_hal_deinit(padapter);
			padapter->bSurpriseRemoved = true;
		}

		padapter->bup = false;
	} else {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("r871x_dev_unload():padapter->bup == false\n"));
	}

	pr_debug("<=== rtw_dev_unload\n");

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("-rtw_dev_unload\n"));
}

static int rtw_suspend(struct usb_interface *pusb_intf, pm_message_t message)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	struct adapter *padapter = dvobj->if1;
	struct net_device *pnetdev = padapter->pnetdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	unsigned long start_time = jiffies;

	pr_debug("==> %s (%s:%d)\n", __func__, current->comm, current->pid);

	if ((!padapter->bup) || (padapter->bDriverStopped) ||
	    (padapter->bSurpriseRemoved)) {
		pr_debug("padapter->bup=%d bDriverStopped=%d bSurpriseRemoved = %d\n",
			padapter->bup, padapter->bDriverStopped,
			padapter->bSurpriseRemoved);
		goto exit;
	}

	pwrpriv->bInSuspend = true;
	rtw_cancel_all_timer(padapter);
	LeaveAllPowerSaveMode(padapter);

	mutex_lock(&pwrpriv->mutex_lock);
	/* s1. */
	if (pnetdev) {
		netif_carrier_off(pnetdev);
		netif_tx_stop_all_queues(pnetdev);
	}

	/* s2. */
	rtw_disassoc_cmd(padapter, 0, false);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) &&
	    check_fwstate(pmlmepriv, _FW_LINKED)) {
		pr_debug("%s:%d %s(%pM), length:%d assoc_ssid.length:%d\n",
			__func__, __LINE__,
			pmlmepriv->cur_network.network.Ssid.Ssid,
			pmlmepriv->cur_network.network.MacAddress,
			pmlmepriv->cur_network.network.Ssid.SsidLength,
			pmlmepriv->assoc_ssid.SsidLength);

		pmlmepriv->to_roaming = 1;
	}
	/* s2-2.  indicate disconnect to os */
	rtw_indicate_disconnect(padapter);
	/* s2-3. */
	rtw_free_assoc_resources(padapter);
	/* s2-4. */
	rtw_free_network_queue(padapter, true);

	rtw_dev_unload(padapter);
	mutex_unlock(&pwrpriv->mutex_lock);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_indicate_scan_done(padapter, 1);

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		rtw_indicate_disconnect(padapter);

exit:
	pr_debug("<===  %s .............. in %dms\n", __func__,
		 jiffies_to_msecs(jiffies - start_time));

	return 0;
}

static int rtw_resume_process(struct adapter *padapter)
{
	struct net_device *pnetdev;
	struct pwrctrl_priv *pwrpriv = NULL;
	int ret = -1;
	unsigned long start_time = jiffies;

	pr_debug("==> %s (%s:%d)\n", __func__, current->comm, current->pid);

	if (padapter) {
		pnetdev = padapter->pnetdev;
		pwrpriv = &padapter->pwrctrlpriv;
	} else {
		goto exit;
	}

	mutex_lock(&pwrpriv->mutex_lock);
	rtw_reset_drv_sw(padapter);
	pwrpriv->bkeepfwalive = false;

	pr_debug("bkeepfwalive(%x)\n", pwrpriv->bkeepfwalive);
	if (netdev_open(pnetdev) != 0) {
		mutex_unlock(&pwrpriv->mutex_lock);
		goto exit;
	}

	netif_device_attach(pnetdev);
	netif_carrier_on(pnetdev);

	mutex_unlock(&pwrpriv->mutex_lock);

	rtw_roaming(padapter, NULL);

	ret = 0;
exit:
	if (pwrpriv)
		pwrpriv->bInSuspend = false;
	pr_debug("<===  %s return %d.............. in %dms\n", __func__,
		ret, jiffies_to_msecs(jiffies - start_time));

	return ret;
}

static int rtw_resume(struct usb_interface *pusb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	struct adapter *padapter = dvobj->if1;

	return rtw_resume_process(padapter);
}

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located
 * a card for us to support.
 *        We accept the new device by returning 0.
 */

static struct adapter *rtw_usb_if1_init(struct dvobj_priv *dvobj,
	struct usb_interface *pusb_intf, const struct usb_device_id *pdid)
{
	struct adapter *padapter = NULL;
	struct net_device *pnetdev = NULL;
	struct net_device *pmondev;
	int status = _FAIL;

	padapter = (struct adapter *)vzalloc(sizeof(*padapter));
	if (padapter == NULL)
		goto exit;
	padapter->dvobj = dvobj;
	dvobj->if1 = padapter;

	padapter->bDriverStopped = true;
	mutex_init(&padapter->hw_init_mutex);

	pnetdev = rtw_init_netdev(padapter);
	if (pnetdev == NULL)
		goto free_adapter;
	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(dvobj));
	padapter = rtw_netdev_priv(pnetdev);

	if (padapter->registrypriv.monitor_enable) {
		pmondev = rtl88eu_mon_init();
		if (pmondev == NULL)
			netdev_warn(pnetdev, "Failed to initialize monitor interface");
		padapter->pmondev = pmondev;
	}

	padapter->HalData = kzalloc(sizeof(struct hal_data_8188e), GFP_KERNEL);
	if (!padapter->HalData)
		DBG_88E("cant not alloc memory for HAL DATA\n");

	/* step read_chip_version */
	rtw_hal_read_chip_version(padapter);

	/* step usb endpoint mapping */
	rtw_hal_chip_configure(padapter);

	/* step read efuse/eeprom data and get mac_addr */
	rtw_hal_read_chip_info(padapter);

	/* step 5. */
	if (rtw_init_drv_sw(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("Initialize driver software resource Failed!\n"));
		goto free_hal_data;
	}

#ifdef CONFIG_PM
	if (padapter->pwrctrlpriv.bSupportRemoteWakeup) {
		dvobj->pusbdev->do_remote_wakeup = 1;
		pusb_intf->needs_remote_wakeup = 1;
		device_init_wakeup(&pusb_intf->dev, 1);
		pr_debug("\n  padapter->pwrctrlpriv.bSupportRemoteWakeup~~~~~~\n");
		pr_debug("\n  padapter->pwrctrlpriv.bSupportRemoteWakeup~~~[%d]~~~\n",
			device_may_wakeup(&pusb_intf->dev));
	}
#endif

	/* 2012-07-11 Move here to prevent the 8723AS-VAU BT auto
	 * suspend influence */
	if (usb_autopm_get_interface(pusb_intf) < 0)
			pr_debug("can't get autopm:\n");

	/*  alloc dev name after read efuse. */
	rtw_init_netdev_name(pnetdev, padapter->registrypriv.ifname);
	rtw_macaddr_cfg(padapter->eeprompriv.mac_addr);
	memcpy(pnetdev->dev_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);
	pr_debug("MAC Address from pnetdev->dev_addr =  %pM\n",
		pnetdev->dev_addr);

	/* step 6. Tell the network stack we exist */
	if (register_netdev(pnetdev) != 0) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("register_netdev() failed\n"));
		goto free_hal_data;
	}

	pr_debug("bDriverStopped:%d, bSurpriseRemoved:%d, bup:%d, hw_init_completed:%d\n"
		, padapter->bDriverStopped
		, padapter->bSurpriseRemoved
		, padapter->bup
		, padapter->hw_init_completed
	);

	status = _SUCCESS;

free_hal_data:
	if (status != _SUCCESS)
		kfree(padapter->HalData);
free_adapter:
	if (status != _SUCCESS) {
		if (pnetdev)
			rtw_free_netdev(pnetdev);
		else
			vfree(padapter);
		padapter = NULL;
	}
exit:
	return padapter;
}

static void rtw_usb_if1_deinit(struct adapter *if1)
{
	struct net_device *pnetdev = if1->pnetdev;
	struct mlme_priv *pmlmepriv = &if1->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd(if1, 0, false);

#ifdef CONFIG_88EU_AP_MODE
	free_mlme_ap_info(if1);
#endif

	if (pnetdev)
		unregister_netdev(pnetdev); /* will call netdev_close() */

	rtl88eu_mon_deinit(if1->pmondev);
	rtw_cancel_all_timer(if1);

	rtw_dev_unload(if1);
	pr_debug("+r871xu_dev_remove, hw_init_completed=%d\n",
		if1->hw_init_completed);
	rtw_free_drv_sw(if1);
	rtw_free_netdev(pnetdev);
}

static int rtw_drv_init(struct usb_interface *pusb_intf, const struct usb_device_id *pdid)
{
	struct adapter *if1 = NULL;
	struct dvobj_priv *dvobj;

	/* Initialize dvobj_priv */
	dvobj = usb_dvobj_init(pusb_intf);
	if (!dvobj) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("initialize device object priv Failed!\n"));
		goto exit;
	}

	if1 = rtw_usb_if1_init(dvobj, pusb_intf, pdid);
	if (!if1) {
		pr_debug("rtw_init_primarystruct adapter Failed!\n");
		goto free_dvobj;
	}

	return 0;

free_dvobj:
	usb_dvobj_deinit(pusb_intf);
exit:
	return -ENODEV;
}

/*
 * dev_remove() - our device is being removed
*/
/* rmmod module & unplug(SurpriseRemoved) will call r871xu_dev_remove() => how to recognize both */
static void rtw_dev_remove(struct usb_interface *pusb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	struct adapter *padapter = dvobj->if1;

	pr_debug("+rtw_dev_remove\n");
	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+dev_remove()\n"));

	if (!pusb_intf->unregistering)
		padapter->bSurpriseRemoved = true;

	rtw_pm_set_ips(padapter, IPS_NONE);
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	LeaveAllPowerSaveMode(padapter);

	rtw_usb_if1_deinit(padapter);

	usb_dvobj_deinit(pusb_intf);

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("-dev_remove()\n"));
	pr_debug("-r871xu_dev_remove, done\n");
}

static struct usb_driver rtl8188e_usb_drv = {
	.name = "r8188eu",
	.probe = rtw_drv_init,
	.disconnect = rtw_dev_remove,
	.id_table = rtw_usb_id_tbl,
	.suspend =  rtw_suspend,
	.resume = rtw_resume,
	.reset_resume = rtw_resume,
};

module_usb_driver(rtl8188e_usb_drv)
