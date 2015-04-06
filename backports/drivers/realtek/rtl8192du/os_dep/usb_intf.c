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
 *
 ******************************************************************************/
#define _HCI_INTF_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <hal_intf.h>
#include <usb_vendor_req.h>
#include <usb_ops.h>
#include <usb_osintf.h>
#include <usb_hal.h>
#include <linux/vmalloc.h>
#include <linux/nl80211.h>

static int rtw_suspend(struct usb_interface *intf, pm_message_t message);
static int rtw_resume(struct usb_interface *intf);
int rtw_resume_process(struct rtw_adapter *padapter);

static int rtw_drv_init(struct usb_interface *pusb_intf,const struct usb_device_id *pdid);
static void rtw_dev_remove(struct usb_interface *pusb_intf);

#define USB_VENDER_ID_REALTEK		0x0BDA

#define RTL8192D_USB_IDS \
	/*=== Realtek demoboard ===*/ \
	/****** 8192DU ********/ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8193)},/* 8192DU-VC */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8194)},/* 8192DU-VS */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8111)},/* Realtek 5G dongle for WiFi Display */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x0193)},/* 8192DE-VAU */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8171)},/* 8192DU-VC */ \
	/*=== Customer ID ===*/ \
	/****** 8192DU-VC ********/ \
	{USB_DEVICE(0x2019, 0xAB2C)},/* PCI - Abocm */ \
	{USB_DEVICE(0x2019, 0x4903)},/* PCI - ETOP */ \
	{USB_DEVICE(0x2019, 0x4904)},/* PCI - ETOP */ \
	{USB_DEVICE(0x07B8, 0x8193)},/* Abocom - Abocom */ \
	/****** 8192DU-VS ********/ \
	{USB_DEVICE(0x20F4, 0x664B)}, /* TRENDnet - Cameo */ \
	{USB_DEVICE(0x04DD, 0x954F)},  /* Sharp */ \
	{USB_DEVICE(0x04DD, 0x96A6)},  /* Sharp */ \
	{USB_DEVICE(0x050D, 0x110A)}, /* Belkin - Edimax */ \
	{USB_DEVICE(0x050D, 0x1105)}, /* Belkin - Edimax */ \
	{USB_DEVICE(0x050D, 0x120A)}, /* Belkin - Edimax */ \
	{USB_DEVICE(0x1668, 0x8102)}, /*  -  */ \
	{USB_DEVICE(0x0BDA, 0xE194)}, /*  - Edimax */ \
	/****** 8192DU-WiFi Display Dongle ********/ \
	{USB_DEVICE(0x2019, 0xAB2D)},/* Planex - Abocom ,5G dongle for WiFi Display */

	#undef RTL8192C_USB_IDS
	#define RTL8192C_USB_IDS

static struct usb_device_id rtw_usb_id_tbl[] ={
	RTL8192D_USB_IDS
	{}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, rtw_usb_id_tbl);

static int const rtw_usb_id_len = sizeof(rtw_usb_id_tbl) / sizeof(struct usb_device_id);

static struct specific_device_id specific_device_id_tbl[] = {
	{.idVendor=USB_VENDER_ID_REALTEK, .idProduct=0x8177, .flags=SPEC_DEV_ID_DISABLE_HT},/* 8188cu 1*1 dongole, (b/g mode only) */
	{.idVendor=USB_VENDER_ID_REALTEK, .idProduct=0x817E, .flags=SPEC_DEV_ID_DISABLE_HT},/* 8188CE-VAU USB minCard (b/g mode only) */
	{.idVendor=0x0b05, .idProduct=0x1791, .flags=SPEC_DEV_ID_DISABLE_HT},
	{.idVendor=0x13D3, .idProduct=0x3311, .flags=SPEC_DEV_ID_DISABLE_HT},
	{.idVendor=0x13D3, .idProduct=0x3359, .flags=SPEC_DEV_ID_DISABLE_HT},/* Russian customer -Azwave (8188CE-VAU  g mode) */
	{}
};

struct rtw_usb_drv {
	struct usb_driver usbdrv;
	int drv_registered;
};

static struct usb_device_id rtl8192d_usb_id_tbl[] ={
	RTL8192D_USB_IDS
	{}	/* Terminating entry */
};

static struct rtw_usb_drv rtl8192d_usb_drv = {
	.usbdrv.name = (char*)"rtl8192du",
	.usbdrv.probe = rtw_drv_init,
	.usbdrv.disconnect = rtw_dev_remove,
	.usbdrv.id_table = rtl8192d_usb_id_tbl,
	.usbdrv.suspend =  rtw_suspend,
	.usbdrv.resume = rtw_resume,
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 22))
	.usbdrv.reset_resume   = rtw_resume,
	#endif
	#ifdef CONFIG_AUTOSUSPEND
	.usbdrv.supports_autosuspend = 1,
	#endif
};
static struct rtw_usb_drv *usb_drv = &rtl8192d_usb_drv;

static inline int RT_usb_endpoint_dir_in(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN);
}

static inline int RT_usb_endpoint_dir_out(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT);
}

static inline int RT_usb_endpoint_xfer_int(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT);
}

static inline int RT_usb_endpoint_xfer_bulk(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK);
}

static inline int RT_usb_endpoint_is_bulk_in(const struct usb_endpoint_descriptor *epd)
{
	return (RT_usb_endpoint_xfer_bulk(epd) && RT_usb_endpoint_dir_in(epd));
}

static inline int RT_usb_endpoint_is_bulk_out(const struct usb_endpoint_descriptor *epd)
{
	return (RT_usb_endpoint_xfer_bulk(epd) && RT_usb_endpoint_dir_out(epd));
}

static inline int RT_usb_endpoint_is_int_in(const struct usb_endpoint_descriptor *epd)
{
	return (RT_usb_endpoint_xfer_int(epd) && RT_usb_endpoint_dir_in(epd));
}

static inline int RT_usb_endpoint_num(const struct usb_endpoint_descriptor *epd)
{
	return epd->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
}

static u8 rtw_init_intf_priv(struct dvobj_priv *dvobj)
{
	u8 rst = _SUCCESS;

	_rtw_mutex_init(&dvobj->usb_vendor_req_mutex);

	dvobj->usb_alloc_vendor_req_buf = kzalloc(MAX_USB_IO_CTL_SIZE, GFP_KERNEL);
	if (dvobj->usb_alloc_vendor_req_buf == NULL) {
		DBG_8192D("alloc usb_vendor_req_buf failed... /n");
		rst = _FAIL;
		goto exit;
	}
	dvobj->usb_vendor_req_buf  =
		(u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(dvobj->usb_alloc_vendor_req_buf), ALIGNMENT_UNIT);
exit:
	return rst;
}

static u8 rtw_deinit_intf_priv(struct dvobj_priv *dvobj)
{
	u8 rst = _SUCCESS;

	kfree(dvobj->usb_alloc_vendor_req_buf);
	_rtw_mutex_free(&dvobj->usb_vendor_req_mutex);
	return rst;
}

static struct dvobj_priv *usb_dvobj_init(struct usb_interface *usb_intf)
{
	int	i;
	u8	val8;
	int	status = _FAIL;
	struct dvobj_priv *pdvobjpriv = NULL;
	struct usb_device				*pusbd;
	struct usb_host_config			*phost_conf;
	struct usb_config_descriptor		*pconf_desc;
	struct usb_host_interface		*phost_iface;
	struct usb_interface_descriptor	*piface_desc;
	struct usb_host_endpoint		*phost_endp;
	struct usb_endpoint_descriptor	*pendp_desc;

	pdvobjpriv = (struct dvobj_priv*)kzalloc(sizeof(*pdvobjpriv), GFP_KERNEL);
	if (!pdvobjpriv)
		goto exit;

	_rtw_mutex_init(&pdvobjpriv->hw_init_mutex);
	_rtw_mutex_init(&pdvobjpriv->h2c_fwcmd_mutex);
	_rtw_mutex_init(&pdvobjpriv->setch_mutex);
	_rtw_mutex_init(&pdvobjpriv->setbw_mutex);

	pdvobjpriv->pusbintf = usb_intf ;
	pusbd = pdvobjpriv->pusbdev = interface_to_usbdev(usb_intf);
	usb_set_intfdata(usb_intf, pdvobjpriv);

	pdvobjpriv->RtNumInPipes = 0;
	pdvobjpriv->RtNumOutPipes = 0;

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

			DBG_8192D("\nusb_endpoint_descriptor(%d):\n", i);
			DBG_8192D("bLength=%x\n",pendp_desc->bLength);
			DBG_8192D("bDescriptorType=%x\n",pendp_desc->bDescriptorType);
			DBG_8192D("bEndpointAddress=%x\n",pendp_desc->bEndpointAddress);
			DBG_8192D("wMaxPacketSize=%x\n",le16_to_cpu(pendp_desc->wMaxPacketSize));
			DBG_8192D("bInterval=%x\n",pendp_desc->bInterval);

			if (RT_usb_endpoint_is_bulk_in(pendp_desc)) {
				DBG_8192D("RT_usb_endpoint_is_bulk_in = %x\n", RT_usb_endpoint_num(pendp_desc));
				pdvobjpriv->RtNumInPipes++;
			} else if (RT_usb_endpoint_is_int_in(pendp_desc)) {
				DBG_8192D("RT_usb_endpoint_is_int_in = %x, Interval = %x\n", RT_usb_endpoint_num(pendp_desc),pendp_desc->bInterval);
				pdvobjpriv->RtNumInPipes++;
			} else if (RT_usb_endpoint_is_bulk_out(pendp_desc)) {
				DBG_8192D("RT_usb_endpoint_is_bulk_out = %x\n", RT_usb_endpoint_num(pendp_desc));
				pdvobjpriv->RtNumOutPipes++;
			}
			pdvobjpriv->ep_num[i] = RT_usb_endpoint_num(pendp_desc);
		}
	}

	DBG_8192D("nr_endpoint=%d, in_num=%d, out_num=%d\n\n", pdvobjpriv->nr_endpoint, pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);

	if (pusbd->speed == USB_SPEED_HIGH) {
		pdvobjpriv->ishighspeed = true;
		DBG_8192D("USB_SPEED_HIGH\n");
	} else {
		pdvobjpriv->ishighspeed = false;
		DBG_8192D("NON USB_SPEED_HIGH\n");
	}

	if (rtw_init_intf_priv(pdvobjpriv) == _FAIL) {
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("\n Can't INIT rtw_init_intf_priv\n"));
		goto free_dvobj;
	}

	/* 3 misc */
	_rtw_init_sema(&(pdvobjpriv->usb_suspend_sema), 0);

	rtw_reset_continual_urb_error(pdvobjpriv);

	usb_get_dev(pusbd);

	status = _SUCCESS;

free_dvobj:
	if (status != _SUCCESS && pdvobjpriv) {
		usb_set_intfdata(usb_intf, NULL);
		_rtw_mutex_free(&pdvobjpriv->hw_init_mutex);
		_rtw_mutex_free(&pdvobjpriv->h2c_fwcmd_mutex);
		_rtw_mutex_free(&pdvobjpriv->setch_mutex);
		_rtw_mutex_free(&pdvobjpriv->setbw_mutex);
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
		rtw_deinit_intf_priv(dvobj);
		_rtw_mutex_free(&dvobj->hw_init_mutex);
		_rtw_mutex_free(&dvobj->h2c_fwcmd_mutex);
		_rtw_mutex_free(&dvobj->setch_mutex);
		_rtw_mutex_free(&dvobj->setbw_mutex);
		kfree(dvobj);
	}

	usb_put_dev(interface_to_usbdev(usb_intf));

}

static void decide_chip_type_by_usb_device_id(struct rtw_adapter *padapter, const struct usb_device_id *pdid)
{
	padapter->chip_type = NULL_CHIP_TYPE;
	padapter->chip_type = RTL8192D;
	padapter->HardwareType = HARDWARE_TYPE_RTL8192DU;
	DBG_8192D("CHIP TYPE: RTL8192D\n");
}

static void usb_intf_start(struct rtw_adapter *padapter)
{
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+usb_intf_start\n"));
	rtw_hal_inirp_init(padapter);
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-usb_intf_start\n"));
}

static void usb_intf_stop(struct rtw_adapter *padapter)
{

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+usb_intf_stop\n"));

	/* disabel_hw_interrupt */
	if (padapter->bSurpriseRemoved == false)
	{
		/* device still exists, so driver can do i/o operation */
		/* TODO: */
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("SurpriseRemoved==false\n"));
	}

	/* cancel in irp */
	rtw_hal_inirp_deinit(padapter);

	/* cancel out irp */
	rtw_write_port_cancel(padapter);

	/* todo:cancel other irps */

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-usb_intf_stop\n"));
}

static void rtw_dev_unload(struct rtw_adapter *padapter)
{
	struct net_device *pnetdev= (struct net_device*)padapter->pnetdev;
	u8 val8;
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+rtw_dev_unload\n"));

	if (padapter->bup == true)
	{
		DBG_8192D("===> rtw_dev_unload\n");

		padapter->bDriverStopped = true;
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);

		/* s3. */
		if (padapter->intf_stop)
			padapter->intf_stop(padapter);

		/* s4. */
		if (!padapter->pwrctrlpriv.bInternalAutoSuspend)
		rtw_stop_drv_threads(padapter);

		/* s5. */
		if (padapter->bSurpriseRemoved == false) {
#ifdef CONFIG_WOWLAN
			if ((padapter->pwrctrlpriv.bSupportRemoteWakeup==true)&&(padapter->pwrctrlpriv.wowlan_mode==true)) {
				DBG_8192D("%s bSupportWakeOnWlan==true  do not run rtw_hal_deinit()\n",__func__);
			}
			else
#endif /* CONFIG_WOWLAN */
			{
				rtw_hal_deinit(padapter);
			}
			padapter->bSurpriseRemoved = true;
		}

		padapter->bup = false;
#ifdef CONFIG_WOWLAN
		padapter->hw_init_completed=false;
#endif /* CONFIG_WOWLAN */
	}
	else
	{
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("r871x_dev_unload():padapter->bup == false\n"));
	}

	DBG_8192D("<=== rtw_dev_unload\n");

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-rtw_dev_unload\n"));
}

static void process_spec_devid(const struct usb_device_id *pdid)
{
	u16 vid, pid;
	u32 flags;
	int i;
	int num = sizeof(specific_device_id_tbl)/sizeof(struct specific_device_id);

	for (i=0; i<num; i++)
	{
		vid = specific_device_id_tbl[i].idVendor;
		pid = specific_device_id_tbl[i].idProduct;
		flags = specific_device_id_tbl[i].flags;

#ifdef CONFIG_80211N_HT
		if ((pdid->idVendor==vid) && (pdid->idProduct==pid) && (flags&SPEC_DEV_ID_DISABLE_HT))
		{
			 rtw_ht_enable = 0;
			 rtw_cbw40_enable = 0;
			 rtw_ampdu_enable = 0;
		}
#endif

	}
}

static int rtw_suspend(struct usb_interface *pusb_intf, pm_message_t message)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	struct rtw_adapter *padapter = dvobj->if1;
	struct net_device *pnetdev = padapter->pnetdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct usb_device *usb_dev = interface_to_usbdev(pusb_intf);
#ifdef CONFIG_WOWLAN
	struct wowlan_ioctl_param poidparam;
#endif /*  CONFIG_WOWLAN */
	int ret = 0;
	u32 start_time = rtw_get_current_time();

	DBG_8192D("==> %s (%s:%d)\n",__func__, current->comm, current->pid);

	if ((!padapter->bup) || (padapter->bDriverStopped)||(padapter->bSurpriseRemoved))
	{
		DBG_8192D("padapter->bup=%d bDriverStopped=%d bSurpriseRemoved = %d\n",
			padapter->bup, padapter->bDriverStopped,padapter->bSurpriseRemoved);
		goto exit;
	}

	pwrpriv->bInSuspend = true;
	rtw_cancel_all_timer(padapter);
	LeaveAllPowerSaveMode(padapter);

	_enter_pwrlock(&pwrpriv->lock);
	/* s1. */
	if (pnetdev)
	{
		netif_carrier_off(pnetdev);
		rtw_netif_stop_queue(pnetdev);
	}
#ifdef CONFIG_WOWLAN
	if (padapter->pwrctrlpriv.bSupportRemoteWakeup==true&&padapter->pwrctrlpriv.wowlan_mode==true) {
		u8 ps_mode=PS_MODE_MIN;
		/* set H2C command */
		poidparam.subcode=WOWLAN_ENABLE;
		rtw_hal_set_hwreg(padapter,HW_VAR_WOWLAN,(u8 *)&poidparam);
	}
	else
#endif /* CONFIG_WOWLAN */
	{
		/* s2. */
		rtw_disassoc_cmd(padapter, 0, false);
	}

#ifdef CONFIG_LAYER2_ROAMING_RESUME
	if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED))
	{
		DBG_8192D("%s:%d %s(%pM), length:%d assoc_ssid.length:%d\n",__func__, __LINE__,
				pmlmepriv->cur_network.network.Ssid.Ssid,
				pmlmepriv->cur_network.network.MacAddress,
				pmlmepriv->cur_network.network.Ssid.SsidLength,
				pmlmepriv->assoc_ssid.SsidLength);
		rtw_set_roaming(padapter, 1);
		}
#endif
	/* s2-2.  indicate disconnect to os */
	rtw_indicate_disconnect(padapter);
	/* s2-3. */
	rtw_free_assoc_resources(padapter, 1);
#ifdef CONFIG_AUTOSUSPEND
	if (!pwrpriv->bInternalAutoSuspend)
#endif
	/* s2-4. */
	rtw_free_network_queue(padapter, true);

	rtw_dev_unload(padapter);
#ifdef CONFIG_AUTOSUSPEND
	pwrpriv->rf_pwrstate = rf_off;
	pwrpriv->bips_processing = false;
#endif
	_exit_pwrlock(&pwrpriv->lock);

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_indicate_scan_done(padapter, 1);

	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		rtw_indicate_disconnect(padapter);

exit:
	DBG_8192D("<===  %s return %d.............. in %dms\n", __func__
		, ret, rtw_get_passing_time_ms(start_time));

	return ret;
}

static int rtw_resume(struct usb_interface *pusb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	struct rtw_adapter *padapter = dvobj->if1;
	struct net_device *pnetdev = padapter->pnetdev;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	 int ret = 0;

	if (pwrpriv->bInternalAutoSuspend) {
		ret = rtw_resume_process(padapter);
	} else {
#ifdef CONFIG_RESUME_IN_WORKQUEUE
		rtw_resume_in_workqueue(pwrpriv);
#else
		if (rtw_is_earlysuspend_registered(pwrpriv)
			#ifdef CONFIG_WOWLAN
			&& !padapter->pwrctrlpriv.wowlan_mode
			#endif /* CONFIG_WOWLAN */
		) {
			/* jeff: bypass resume here, do in late_resume */
			rtw_set_do_late_resume(pwrpriv, true);
		} else {
			ret = rtw_resume_process(padapter);
		}
#endif /* CONFIG_RESUME_IN_WORKQUEUE */
	}

	return ret;
}

int rtw_resume_process(struct rtw_adapter *padapter)
{
	struct net_device *pnetdev;
	struct pwrctrl_priv *pwrpriv = NULL;
	int ret = -1;
	u32 start_time = rtw_get_current_time();

	DBG_8192D("==> %s (%s:%d)\n",__func__, current->comm, current->pid);

	if (padapter) {
		pnetdev= padapter->pnetdev;
		pwrpriv = &padapter->pwrctrlpriv;
	} else {
		goto exit;
	}

	_enter_pwrlock(&pwrpriv->lock);
	rtw_reset_drv_sw(padapter);
	pwrpriv->bkeepfwalive = false;

	DBG_8192D("bkeepfwalive(%x)\n",pwrpriv->bkeepfwalive);
	if (pm_netdev_open(pnetdev,true) != 0)
		goto exit;

	netif_device_attach(pnetdev);
	netif_carrier_on(pnetdev);

#ifdef CONFIG_AUTOSUSPEND
	if (pwrpriv->bInternalAutoSuspend) {
		pwrpriv->bInternalAutoSuspend = false;
		pwrpriv->brfoffbyhw = false;
		DBG_8192D("enc_algorithm(%x),wepkeymask(%x)\n",
			padapter->securitypriv.dot11PrivacyAlgrthm,pwrpriv->wepkeymask);
		if (	(_WEP40_ == padapter->securitypriv.dot11PrivacyAlgrthm) ||
			(_WEP104_ == padapter->securitypriv.dot11PrivacyAlgrthm))
		{
			int keyid;

			for (keyid=0;keyid<4;keyid++) {
				if (pwrpriv->wepkeymask & BIT(keyid)) {
					if (keyid == padapter->securitypriv.dot11PrivacyKeyIndex)
						rtw_set_key(padapter,&padapter->securitypriv, keyid, 1);
					else
						rtw_set_key(padapter,&padapter->securitypriv, keyid, 0);
				}
			}
		}
	}
#endif
	_exit_pwrlock(&pwrpriv->lock);

	if (padapter->pid[1]!=0) {
		DBG_8192D("pid[1]:%d\n",padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}

	#ifdef CONFIG_LAYER2_ROAMING_RESUME
	rtw_roaming(padapter, NULL);
	#endif

	ret = 0;
exit:
	#ifdef CONFIG_RESUME_IN_WORKQUEUE
	rtw_unlock_suspend();
	#endif /* CONFIG_RESUME_IN_WORKQUEUE */

	if (pwrpriv)
		pwrpriv->bInSuspend = false;
	DBG_8192D("<===  %s return %d.............. in %dms\n", __func__
		, ret, rtw_get_passing_time_ms(start_time));

	return ret;
}

#ifdef CONFIG_AUTOSUSPEND
void autosuspend_enter(struct rtw_adapter* padapter)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	pwrpriv->bInternalAutoSuspend = true;
	pwrpriv->bips_processing = true;

	DBG_8192D("==>autosuspend_enter...........\n");

	if (rf_off == pwrpriv->change_rfpwrstate)
	{
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
		usb_enable_autosuspend(dvobj->pusbdev);
		#else
		dvobj->pusbdev->autosuspend_disabled = 0;/* autosuspend disabled by the user */
		#endif

		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
			usb_autopm_put_interface(dvobj->pusbintf);
		#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,20))
			usb_autopm_enable(dvobj->pusbintf);
		#else
			usb_autosuspend_device(dvobj->pusbdev, 1);
		#endif
	}
	#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,32))
	DBG_8192D("...pm_usage_cnt(%d).....\n", atomic_read(&(dvobj->pusbintf->pm_usage_cnt)));
	#else
	DBG_8192D("...pm_usage_cnt(%d).....\n", dvobj->pusbintf->pm_usage_cnt);
	#endif
}

int autoresume_enter(struct rtw_adapter* padapter)
{
	int result = _SUCCESS;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct security_priv* psecuritypriv=&(padapter->securitypriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);

	DBG_8192D("====> autoresume_enter\n");

	if (rf_off == pwrpriv->rf_pwrstate)
	{
		pwrpriv->ps_flag = false;
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
			if (usb_autopm_get_interface(dvobj->pusbintf) < 0)
			{
				DBG_8192D("can't get autopm: %d\n", result);
				result = _FAIL;
				goto error_exit;
			}
		#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,20))
			usb_autopm_disable(dvobj->pusbintf);
		#else
			usb_autoresume_device(dvobj->pusbdev, 1);
		#endif

		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,32))
		DBG_8192D("...pm_usage_cnt(%d).....\n", atomic_read(&(dvobj->pusbintf->pm_usage_cnt)));
		#else
		DBG_8192D("...pm_usage_cnt(%d).....\n", dvobj->pusbintf->pm_usage_cnt);
		#endif
	}
	DBG_8192D("<==== autoresume_enter\n");
error_exit:

	return result;
}
#endif

static struct rtw_adapter *rtw_usb_if1_init(struct dvobj_priv *dvobj,
					    struct usb_interface *pusb_intf,
					    const struct usb_device_id *pdid)
{
	struct rtw_adapter *padapter = NULL;
	struct net_device *pnetdev = NULL;
	int status = _FAIL;

	padapter = (struct rtw_adapter *)vzalloc(sizeof(*padapter));
	if (!padapter)
		goto exit;
	padapter->dvobj = dvobj;
	dvobj->if1 = padapter;

	padapter->bDriverStopped=true;

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_DUALMAC_CONCURRENT)
	/* set adapter_type/iface type for primary padapter */
	padapter->isprimary = true;
	padapter->adapter_type = PRIMARY_ADAPTER;
	padapter->iface_id = IFACE_ID0;
	#ifndef CONFIG_HWPORT_SWAP
	padapter->iface_type = IFACE_PORT0;
	#else
	padapter->iface_type = IFACE_PORT1;
	#endif
	dvobj->padapters[dvobj->iface_nums++] = padapter;
#endif

	#ifndef RTW_DVOBJ_CHIP_HW_TYPE
	/* step 1-1., decide the chip_type via vid/pid */
	padapter->interface_type = RTW_USB;
	decide_chip_type_by_usb_device_id(padapter, pdid);
	#endif

	if ((pnetdev = rtw_init_netdev(padapter)) == NULL) {
		goto free_adapter;
	}
	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(dvobj));
	padapter = rtw_netdev_priv(pnetdev);

	if (rtw_handle_dualmac(padapter, 1) != _SUCCESS)
		goto free_adapter;

	if (rtw_wdev_alloc(padapter, dvobj_to_dev(dvobj)) != 0)
		goto handle_dualmac;

	/* step 2. hook HalFunc, allocate HalData */
	if (padapter->chip_type == RTL8192D) {
		rtl8192du_set_hal_ops(padapter);
	} else {
		DBG_8192D("Detect NULL_CHIP_TYPE\n");
		goto free_wdev;
	}

	/* step 3. */
	padapter->intf_start=&usb_intf_start;
	padapter->intf_stop=&usb_intf_stop;

	/* 2 */
	if ((rtw_init_io_priv(padapter, usb_set_intf_ops)) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("\n Can't init io_reqs\n"));
		goto free_hal_data;
	}

	rtw_hal_read_chip_version(padapter);

	/* 4 usb endpoint mapping */
	rtw_hal_chip_configure(padapter);

	/* step 4. read efuse/eeprom data and get mac_addr */
	rtw_hal_read_chip_info(padapter);

	/* step 5. */
	if (rtw_init_drv_sw(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize driver software resource Failed!\n"));
		goto free_hal_data;
	}

#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
	if (padapter->pwrctrlpriv.bSupportRemoteWakeup)
	{
		dvobj->pusbdev->do_remote_wakeup=1;
		pusb_intf->needs_remote_wakeup = 1;
		device_init_wakeup(&pusb_intf->dev, 1);
		DBG_8192D("\n  padapter->pwrctrlpriv.bSupportRemoteWakeup~~~~~~\n");
		DBG_8192D("\n  padapter->pwrctrlpriv.bSupportRemoteWakeup~~~[%d]~~~\n",device_may_wakeup(&pusb_intf->dev));
	}
#endif
#endif

#ifdef CONFIG_AUTOSUSPEND
	if (padapter->registrypriv.power_mgnt != PS_MODE_ACTIVE)
	{
		if (padapter->registrypriv.usbss_enable) {	/* autosuspend (2s delay) */
			#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,38))
			dvobj->pusbdev->dev.power.autosuspend_delay = 0 * HZ;/* 15 * HZ; idle-delay time */
			#else
			dvobj->pusbdev->autosuspend_delay = 0 * HZ;/* 15 * HZ; idle-delay time */
			#endif

			#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
			usb_enable_autosuspend(dvobj->pusbdev);
			#elif  (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,22) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,34))
			padapter->bDisableAutosuspend = dvobj->pusbdev->autosuspend_disabled ;
			dvobj->pusbdev->autosuspend_disabled = 0;/* autosuspend disabled by the user */
			#endif

			usb_autopm_get_interface(dvobj->pusbintf);/* init pm_usage_cnt ,let it start from 1 */

			#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,32))
			DBG_8192D("%s...pm_usage_cnt(%d).....\n",__func__, atomic_read(&(dvobj->pusbintf ->pm_usage_cnt)));
			#else
			DBG_8192D("%s...pm_usage_cnt(%d).....\n",__func__, dvobj->pusbintf ->pm_usage_cnt);
			#endif
		}
	}
#endif

	/*  alloc dev name after read efuse. */
	rtw_init_netdev_name(pnetdev, padapter->registrypriv.ifname);
	rtw_macaddr_cfg(padapter->eeprompriv.mac_addr);
	rtw_init_wifidirect_addrs(padapter, padapter->eeprompriv.mac_addr, padapter->eeprompriv.mac_addr);
	memcpy(pnetdev->dev_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);
	DBG_8192D("MAC Address from pnetdev->dev_addr= %pM\n", pnetdev->dev_addr);

#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_init(padapter);
#endif

	/* step 6. Tell the network stack we exist */
	if (register_netdev(pnetdev) != 0) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("register_netdev() failed\n"));
		goto free_hal_data;
	}

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-drv_init - adapter->bDriverStopped=%d, adapter->bSurpriseRemoved=%d\n",padapter->bDriverStopped, padapter->bSurpriseRemoved));
	DBG_8192D("bDriverStopped:%d, bSurpriseRemoved:%d, bup:%d, hw_init_completed:%d\n"
		,padapter->bDriverStopped
		,padapter->bSurpriseRemoved
		,padapter->bup
		,padapter->hw_init_completed
	);

	status = _SUCCESS;

free_hal_data:
	if (status != _SUCCESS && padapter->HalData)
		kfree(padapter->HalData);
free_wdev:
	if (status != _SUCCESS) {
		rtw_wdev_unregister(padapter->rtw_wdev);
		rtw_wdev_free(padapter->rtw_wdev);
	}
handle_dualmac:
	if (status != _SUCCESS)
		rtw_handle_dualmac(padapter, 0);
free_adapter:
	if (status != _SUCCESS) {
		if (pnetdev)
			rtw_free_netdev(pnetdev);
		else if (padapter)
			vfree(padapter);
		padapter = NULL;
	}
exit:
	return padapter;
}

static void rtw_usb_if1_deinit(struct rtw_adapter *if1)
{
	struct net_device *pnetdev = if1->pnetdev;
	struct mlme_priv *pmlmepriv= &if1->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd(if1, 0, false);

#ifdef CONFIG_92D_AP_MODE
	free_mlme_ap_info(if1);
	#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_unload(if1);
	#endif
#endif

	if (if1->DriverState != DRIVER_DISAPPEAR)
	{
		if (pnetdev) {
			unregister_netdev(pnetdev); /* will call netdev_close() */
			rtw_proc_remove_one(pnetdev);
		}
	}

	rtw_cancel_all_timer(if1);
#ifdef CONFIG_WOWLAN
	if1->pwrctrlpriv.wowlan_mode=false;
#endif /* CONFIG_WOWLAN */
	rtw_dev_unload(if1);

	DBG_8192D("%s, hw_init_completed=%d\n", __func__, if1->hw_init_completed);

	/* s6. */
	rtw_handle_dualmac(if1, 0);

	rtw_wdev_unregister(if1->rtw_wdev);
	rtw_wdev_free(if1->rtw_wdev);

	rtw_free_drv_sw(if1);

	if (pnetdev)
		rtw_free_netdev(pnetdev);
}

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located a card for us to support.
 *        We accept the new device by returning 0.
*/

static struct rtw_adapter  *rtw_sw_export = NULL;

static int rtw_drv_init(struct usb_interface *pusb_intf, const struct usb_device_id *did)
{
	uint status = _FAIL;
	struct rtw_adapter *if1 = NULL, *if2 = NULL;
	struct dvobj_priv *dvobj = NULL;
#ifdef CONFIG_MULTI_VIR_IFACES
	int i;
#endif /* CONFIG_MULTI_VIR_IFACES */

	/* step 0. */
	process_spec_devid(did);

	/* Initialize dvobj_priv */
	if ((dvobj = usb_dvobj_init(pusb_intf)) == NULL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("initialize device object priv Failed!\n"));
		goto exit;
	}

	/* Initialize if1 */
	if ((if1 = rtw_usb_if1_init(dvobj, pusb_intf, did)) == NULL) {
		DBG_8192D("rtw_usb_if1_init Failed!\n");
		goto free_dvobj;
	}

	/* Initialize if2 */
#ifdef CONFIG_CONCURRENT_MODE
	if ((if2 = rtw_drv_if2_init(if1, NULL, usb_set_intf_ops)) == NULL) {
		goto free_if1;
	}
#ifdef CONFIG_MULTI_VIR_IFACES
	for (i=0; i<if1->registrypriv.ext_iface_num;i++) {
		if (rtw_drv_add_vir_if (if1, "wlan%d", usb_set_intf_ops) == NULL) {
			DBG_8192D("rtw_drv_add_iface failed! (%d)\n", i);
			break;
		}
	}
#endif /* CONFIG_MULTI_VIR_IFACES */
#endif

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

/*
 * dev_remove() - our device is being removed
*/
/* rmmod module & unplug(SurpriseRemoved) will call r871xu_dev_remove() => how to recognize both */
static void rtw_dev_remove(struct usb_interface *pusb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	struct rtw_adapter *padapter = dvobj->if1;
	struct wireless_dev *pwdev = padapter->rtw_wdev;

	DBG_8192D("+rtw_dev_remove\n");

	if (usb_drv->drv_registered )
		padapter->bSurpriseRemoved = true;

	/* to avoid WARN_ON in __cfg80211_disconnected() */
	pwdev->iftype = NL80211_IFTYPE_STATION;

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(&padapter->pwrctrlpriv);
#endif

	rtw_pm_set_ips(padapter, IPS_NONE);
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	LeaveAllPowerSaveMode(padapter);

#ifdef CONFIG_CONCURRENT_MODE
#ifdef CONFIG_MULTI_VIR_IFACES
	rtw_drv_stop_vir_ifaces(dvobj);
#endif /* CONFIG_MULTI_VIR_IFACES */
	rtw_drv_if2_stop(dvobj->if2);
#endif	/* CONFIG_CONCURRENT_MODE */

	rtw_usb_if1_deinit(padapter);

#ifdef CONFIG_CONCURRENT_MODE
#ifdef CONFIG_MULTI_VIR_IFACES
	rtw_drv_free_vir_ifaces(dvobj);
#endif /* CONFIG_MULTI_VIR_IFACES */
	rtw_drv_if2_free(dvobj->if2);
#endif /* CONFIG_CONCURRENT_MODE */

	usb_dvobj_deinit(pusb_intf);

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-dev_remove()\n"));
	DBG_8192D("-r871xu_dev_remove, done\n");

	return;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
extern int console_suspend_enabled;
#endif

static int __init rtw_drv_entry(void)
{
	rtw_suspend_lock_init();
	usb_drv->drv_registered = true;
	return usb_register(&usb_drv->usbdrv);
}

static void __exit rtw_drv_halt(void)
{
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+rtw_drv_halt\n"));
	DBG_8192D("+rtw_drv_halt\n");

	rtw_suspend_lock_uninit();

	usb_drv->drv_registered = false;
	usb_deregister(&usb_drv->usbdrv);

	DBG_8192D("-rtw_drv_halt\n");
}

module_init(rtw_drv_entry);
module_exit(rtw_drv_halt);

#ifdef CONFIG_WOWLAN
#ifdef CONFIG_WOWLAN_MANUAL

int rtw_resume_toshiba(struct rtw_adapter * adapter)
{
	struct dvobj_priv *pdvobjpriv;
	pdvobjpriv = adapter_to_dvobj(adapter);

	rtw_resume(pdvobjpriv->pusbintf);
	return 0;
}

int rtw_suspend_toshiba(struct rtw_adapter * adapter)
{
	pm_message_t msg;
	struct dvobj_priv *pdvobjpriv;
	pdvobjpriv = adapter_to_dvobj(adapter);
	msg.event=0;
	/* for Toshiba only, they should call rtw_suspend before suspend */
	rtw_suspend(pdvobjpriv->pusbintf, msg);
	return 0;
}
EXPORT_SYMBOL(rtw_suspend_toshiba);
EXPORT_SYMBOL(rtw_resume_toshiba);
#endif /* CONFIG_WOWLAN_MANUAL */
#endif /* CONFIG_WOWLAN */
