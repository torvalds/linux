// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include <linux/usb.h>
#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/recv_osdep.h"
#include "../include/xmit_osdep.h"
#include "../include/hal_intf.h"
#include "../include/osdep_intf.h"
#include "../include/usb_vendor_req.h"
#include "../include/usb_ops.h"
#include "../include/usb_osintf.h"
#include "../include/rtw_ioctl.h"
#include "../include/rtl8188e_hal.h"

int ui_pid[3] = {0, 0, 0};

static int rtw_suspend(struct usb_interface *intf, pm_message_t message);
static int rtw_resume(struct usb_interface *intf);

static int rtw_drv_init(struct usb_interface *pusb_intf, const struct usb_device_id *pdid);
static void rtw_dev_remove(struct usb_interface *pusb_intf);

#define USB_VENDER_ID_REALTEK		0x0bda

/* DID_USB_v916_20130116 */
static struct usb_device_id rtw_usb_id_tbl[] = {
	/*=== Realtek demoboard ===*/
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8179)}, /* 8188EUS */
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x0179)}, /* 8188ETV */
	/*=== Customer ID ===*/
	/****** 8188EUS ********/
	{USB_DEVICE(0x07B8, 0x8179)}, /* Abocom - Abocom */
	{USB_DEVICE(0x0DF6, 0x0076)}, /* Sitecom N150 v2 */
	{USB_DEVICE(0x2001, 0x330F)}, /* DLink DWA-125 REV D1 */
	{USB_DEVICE(0x2001, 0x3310)}, /* Dlink DWA-123 REV D1 */
	{USB_DEVICE(0x2001, 0x3311)}, /* DLink GO-USB-N150 REV B1 */
	{USB_DEVICE(0x2001, 0x331B)}, /* D-Link DWA-121 rev B1 */
	{USB_DEVICE(0x056E, 0x4008)}, /* Elecom WDC-150SU2M */
	{USB_DEVICE(0x2357, 0x010c)}, /* TP-Link TL-WN722N v2 */
	{USB_DEVICE(0x2357, 0x0111)}, /* TP-Link TL-WN727N v5.21 */
	{USB_DEVICE(0x2C4E, 0x0102)}, /* MERCUSYS MW150US v2 */
	{USB_DEVICE(0x0B05, 0x18F0)}, /* ASUS USB-N10 Nano B1 */
	{USB_DEVICE(0x7392, 0xb811)}, /* Edimax EW-7811Un V2 */
	{}	/* Terminating entry */
};

MODULE_DEVICE_TABLE(usb, rtw_usb_id_tbl);

struct rtw_usb_drv {
	struct usb_driver usbdrv;
	int drv_registered;
	struct mutex hw_init_mutex;
};

static struct rtw_usb_drv rtl8188e_usb_drv = {
	.usbdrv.name = "r8188eu",
	.usbdrv.probe = rtw_drv_init,
	.usbdrv.disconnect = rtw_dev_remove,
	.usbdrv.id_table = rtw_usb_id_tbl,
	.usbdrv.suspend =  rtw_suspend,
	.usbdrv.resume = rtw_resume,
	.usbdrv.reset_resume   = rtw_resume,
};

static struct rtw_usb_drv *usb_drv = &rtl8188e_usb_drv;

static struct dvobj_priv *usb_dvobj_init(struct usb_interface *usb_intf)
{
	int	i;
	u8	rt_num_in_pipes = 0;
	struct dvobj_priv *pdvobjpriv;
	struct usb_host_config		*phost_conf;
	struct usb_config_descriptor	*pconf_desc;
	struct usb_host_interface	*phost_iface;
	struct usb_interface_descriptor	*piface_desc;
	struct usb_endpoint_descriptor	*pendp_desc;
	struct usb_device	*pusbd;

	pdvobjpriv = kzalloc(sizeof(*pdvobjpriv), GFP_KERNEL);
	if (!pdvobjpriv)
		goto err;

	pdvobjpriv->pusbintf = usb_intf;
	pusbd = interface_to_usbdev(usb_intf);
	pdvobjpriv->pusbdev = pusbd;
	usb_set_intfdata(usb_intf, pdvobjpriv);

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
			pdvobjpriv->RtInPipe = ep_num;
			rt_num_in_pipes++;
		} else if (usb_endpoint_is_bulk_out(pendp_desc)) {
			pdvobjpriv->RtOutPipe[pdvobjpriv->RtNumOutPipes] =
				ep_num;
			pdvobjpriv->RtNumOutPipes++;
		}
	}

	if (rt_num_in_pipes != 1)
		goto err;

	/* 3 misc */
	rtw_reset_continual_urb_error(pdvobjpriv);

	usb_get_dev(pusbd);
	return pdvobjpriv;

err:
	kfree(pdvobjpriv);
	return NULL;
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
				usb_reset_device(interface_to_usbdev(usb_intf));
			}
		}
		kfree(dvobj);
	}

	usb_put_dev(interface_to_usbdev(usb_intf));

}

static void usb_intf_start(struct adapter *padapter)
{
	rtl8188eu_inirp_init(padapter);
}

static void usb_intf_stop(struct adapter *padapter)
{
	/* cancel in irp */
	rtw_read_port_cancel(padapter);

	/* cancel out irp */
	rtw_write_port_cancel(padapter);

	/* todo:cancel other irps */
}

static void rtw_dev_unload(struct adapter *padapter)
{
	if (padapter->bup) {
		padapter->bDriverStopped = true;
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
		/* s3. */
		if (padapter->intf_stop)
			padapter->intf_stop(padapter);
		/* s4. */
		rtw_stop_drv_threads(padapter);

		/* s5. */
		if (!padapter->bSurpriseRemoved) {
			rtw_hal_deinit(padapter);
			padapter->bSurpriseRemoved = true;
		}

		padapter->bup = false;
	}
}

static int rtw_suspend(struct usb_interface *pusb_intf, pm_message_t message)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	struct adapter *padapter = dvobj->if1;
	struct net_device *pnetdev = padapter->pnetdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;

	int ret = 0;

	if ((!padapter->bup) || (padapter->bDriverStopped) ||
	    (padapter->bSurpriseRemoved)) {
		goto exit;
	}

	pwrpriv->bInSuspend = true;
	rtw_cancel_all_timer(padapter);
	LeaveAllPowerSaveMode(padapter);

	mutex_lock(&pwrpriv->lock);
	/* s1. */
	if (pnetdev) {
		netif_carrier_off(pnetdev);
		netif_tx_stop_all_queues(pnetdev);
	}

	/* s2. */
	rtw_disassoc_cmd(padapter, 0, false);

	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) &&
	    check_fwstate(pmlmepriv, _FW_LINKED)) {
		pmlmepriv->to_roaming = 1;
	}
	/* s2-2.  indicate disconnect to os */
	rtw_indicate_disconnect(padapter);
	/* s2-3. */
	rtw_free_assoc_resources(padapter, 1);
	/* s2-4. */
	rtw_free_network_queue(padapter, true);

	rtw_dev_unload(padapter);
	mutex_unlock(&pwrpriv->lock);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_indicate_scan_done(padapter, 1);

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		rtw_indicate_disconnect(padapter);

exit:
		return ret;
}

static int rtw_resume(struct usb_interface *pusb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	struct adapter *padapter = dvobj->if1;
	struct net_device *pnetdev;
	struct pwrctrl_priv *pwrpriv = NULL;
	int ret = -1;

	pnetdev = padapter->pnetdev;
	pwrpriv = &padapter->pwrctrlpriv;

	mutex_lock(&pwrpriv->lock);
	rtw_reset_drv_sw(padapter);
	if (pwrpriv)
		pwrpriv->bkeepfwalive = false;

	if (netdev_open(pnetdev) != 0) {
		mutex_unlock(&pwrpriv->lock);
		goto exit;
	}

	netif_device_attach(pnetdev);
	netif_carrier_on(pnetdev);

	mutex_unlock(&pwrpriv->lock);

	if (padapter->pid[1] != 0) {
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}

	rtw_roaming(padapter, NULL);

	ret = 0;
exit:
	if (pwrpriv)
		pwrpriv->bInSuspend = false;

	return ret;
}

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located
 * a card for us to support.
 *        We accept the new device by returning 0.
 */

static struct adapter *rtw_usb_if1_init(struct dvobj_priv *dvobj,
	struct usb_interface *pusb_intf)
{
	struct adapter *padapter = NULL;
	struct net_device *pnetdev = NULL;
	struct io_priv *piopriv;
	struct intf_hdl *pintf;

	padapter = vzalloc(sizeof(*padapter));
	if (!padapter)
		return NULL;

	padapter->dvobj = dvobj;
	dvobj->if1 = padapter;

	padapter->bDriverStopped = true;

	padapter->hw_init_mutex = &usb_drv->hw_init_mutex;

	if (rtw_handle_dualmac(padapter, 1) != _SUCCESS)
		goto free_adapter;

	pnetdev = rtw_init_netdev(padapter);
	if (!pnetdev)
		goto handle_dualmac;
	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(dvobj));
	padapter = rtw_netdev_priv(pnetdev);

	padapter->intf_start = &usb_intf_start;
	padapter->intf_stop = &usb_intf_stop;

	/* step init_io_priv */
	piopriv = &padapter->iopriv;
	pintf = &piopriv->intf;
	piopriv->padapter = padapter;
	pintf->padapter = padapter;
	pintf->pintf_dev = adapter_to_dvobj(padapter);

	/* step read_chip_version */
	rtl8188e_read_chip_version(padapter);

	/* step usb endpoint mapping */
	rtl8188eu_interface_configure(padapter);

	/* step read efuse/eeprom data and get mac_addr */
	ReadAdapterInfo8188EU(padapter);

	/* step 5. */
	if (rtw_init_drv_sw(padapter) == _FAIL)
		goto handle_dualmac;

#ifdef CONFIG_PM
	if (padapter->pwrctrlpriv.bSupportRemoteWakeup) {
		dvobj->pusbdev->do_remote_wakeup = 1;
		pusb_intf->needs_remote_wakeup = 1;
		device_init_wakeup(&pusb_intf->dev, 1);
	}
#endif

	/* 2012-07-11 Move here to prevent the 8723AS-VAU BT auto
	 * suspend influence */
	if (usb_autopm_get_interface(pusb_intf) < 0)
		;

	/*  alloc dev name after read efuse. */
	if (rtw_init_netdev_name(pnetdev, padapter->registrypriv.ifname) < 0) {
		goto free_drv_sw;
	}
	rtw_macaddr_cfg(padapter->eeprompriv.mac_addr);
	rtw_init_wifidirect_addrs(padapter, padapter->eeprompriv.mac_addr,
				  padapter->eeprompriv.mac_addr);
	eth_hw_addr_set(pnetdev, padapter->eeprompriv.mac_addr);

	/* step 6. Tell the network stack we exist */
	if (register_netdev(pnetdev) != 0)
		goto free_drv_sw;

	return padapter;

free_drv_sw:
	rtw_cancel_all_timer(padapter);
	rtw_free_drv_sw(padapter);
handle_dualmac:
	rtw_handle_dualmac(padapter, 0);
free_adapter:
	if (pnetdev)
		rtw_free_netdev(pnetdev);
	else if (padapter)
		vfree(padapter);

	return NULL;
}

static void rtw_usb_if1_deinit(struct adapter *if1)
{
	struct net_device *pnetdev = if1->pnetdev;
	struct mlme_priv *pmlmepriv = &if1->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd(if1, 0, false);

	free_mlme_ap_info(if1);

	if (pnetdev) {
		/* will call netdev_close() */
		unregister_netdev(pnetdev);
	}
	rtw_cancel_all_timer(if1);

	rtw_dev_unload(if1);
	rtw_handle_dualmac(if1, 0);
	rtw_free_drv_sw(if1);
	if (pnetdev)
		rtw_free_netdev(pnetdev);
}

static int rtw_drv_init(struct usb_interface *pusb_intf, const struct usb_device_id *pdid)
{
	struct adapter *if1 = NULL;
	struct dvobj_priv *dvobj;

	/* Initialize dvobj_priv */
	dvobj = usb_dvobj_init(pusb_intf);
	if (!dvobj)
		goto err;

	if1 = rtw_usb_if1_init(dvobj, pusb_intf);
	if (!if1) {
		goto free_dvobj;
	}

	if (ui_pid[1] != 0) {
		rtw_signal_process(ui_pid[1], SIGUSR2);
	}

	return 0;

free_dvobj:
	usb_dvobj_deinit(pusb_intf);
err:
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

	if (usb_drv->drv_registered)
		padapter->bSurpriseRemoved = true;

	rtw_pm_set_ips(padapter, IPS_NONE);
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	LeaveAllPowerSaveMode(padapter);

	rtw_usb_if1_deinit(padapter);

	usb_dvobj_deinit(pusb_intf);
}

static int __init rtw_drv_entry(void)
{
	mutex_init(&usb_drv->hw_init_mutex);

	usb_drv->drv_registered = true;
	return usb_register(&usb_drv->usbdrv);
}

static void __exit rtw_drv_halt(void)
{
	usb_drv->drv_registered = false;
	usb_deregister(&usb_drv->usbdrv);

	mutex_destroy(&usb_drv->hw_init_mutex);
}

module_init(rtw_drv_entry);
module_exit(rtw_drv_halt);
