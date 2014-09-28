/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _HCI_INTF_C_

#include <drv_types.h>
#include <platform_ops.h>

#ifndef CONFIG_SDIO_HCI
#error "CONFIG_SDIO_HCI shall be on!\n"
#endif

#ifndef dev_to_sdio_func
#define dev_to_sdio_func(d)     container_of(d, struct sdio_func, dev)
#endif

#ifdef CONFIG_WOWLAN
static struct mmc_host *mmc_host = NULL;
#endif

static const struct sdio_device_id sdio_ids[] =
{
#ifdef CONFIG_RTL8723A
	{ SDIO_DEVICE(0x024c, 0x8723),.driver_data = RTL8723A},
#endif //CONFIG_RTL8723A
#ifdef CONFIG_RTL8723B
	{ SDIO_DEVICE(0x024c, 0xB723),.driver_data = RTL8723B},
#endif
#ifdef CONFIG_RTL8188E
	{ SDIO_DEVICE(0x024c, 0x8179),.driver_data = RTL8188E},
#endif //CONFIG_RTL8188E
#ifdef CONFIG_RTL8821A
	{ SDIO_DEVICE(0x024c, 0x8821),.driver_data = RTL8821},
#endif //CONFIG_RTL8188E

#if defined(RTW_ENABLE_WIFI_CONTROL_FUNC) /* temporarily add this to accept all sdio wlan id */
	{ SDIO_DEVICE_CLASS(SDIO_CLASS_WLAN) },
#endif
//	{ /* end: all zeroes */				},
};

static int rtw_drv_init(struct sdio_func *func, const struct sdio_device_id *id);
static void rtw_dev_remove(struct sdio_func *func);
static int rtw_sdio_resume(struct device *dev);
static int rtw_sdio_suspend(struct device *dev);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)) 
static const struct dev_pm_ops rtw_sdio_pm_ops = {
	.suspend	= rtw_sdio_suspend,
	.resume	= rtw_sdio_resume,
};
#endif
	
struct sdio_drv_priv {
	struct sdio_driver r871xs_drv;
	int drv_registered;
};

static struct sdio_drv_priv sdio_drvpriv = {
	.r871xs_drv.probe = rtw_drv_init,
	.r871xs_drv.remove = rtw_dev_remove,
	.r871xs_drv.name = (char*)DRV_NAME,
	.r871xs_drv.id_table = sdio_ids,
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)) 
	.r871xs_drv.drv = {
		.pm = &rtw_sdio_pm_ops,
	}
	#endif
};

static void sd_sync_int_hdl(struct sdio_func *func)
{
	struct dvobj_priv *psdpriv;


	psdpriv = sdio_get_drvdata(func);

	if (!psdpriv->if1) {
		DBG_871X("%s if1 == NULL\n", __func__);
		return;
	}

	rtw_sdio_set_irq_thd(psdpriv, current);
	sd_int_hdl(psdpriv->if1);
	rtw_sdio_set_irq_thd(psdpriv, NULL);
}

int sdio_alloc_irq(struct dvobj_priv *dvobj)
{
	PSDIO_DATA psdio_data;
	struct sdio_func *func;
	int err;

	psdio_data = &dvobj->intf_data;
	func = psdio_data->func;

	sdio_claim_host(func);

	err = sdio_claim_irq(func, &sd_sync_int_hdl);
	if (err)
	{
		dvobj->drv_dbg.dbg_sdio_alloc_irq_error_cnt++;
		printk(KERN_CRIT "%s: sdio_claim_irq FAIL(%d)!\n", __func__, err);
	}
	else
	{
		dvobj->drv_dbg.dbg_sdio_alloc_irq_cnt++;
		dvobj->irq_alloc = 1;
	}

	sdio_release_host(func);

	return err?_FAIL:_SUCCESS;
}

void sdio_free_irq(struct dvobj_priv *dvobj)
{
    PSDIO_DATA psdio_data;
    struct sdio_func *func;
    int err;

    if (dvobj->irq_alloc) {
        psdio_data = &dvobj->intf_data;
        func = psdio_data->func;

        if (func) {
            sdio_claim_host(func);
            err = sdio_release_irq(func);
            if (err)
            {
				dvobj->drv_dbg.dbg_sdio_free_irq_error_cnt++;
				DBG_871X_LEVEL(_drv_err_,"%s: sdio_release_irq FAIL(%d)!\n", __func__, err);
            }
            else
            	dvobj->drv_dbg.dbg_sdio_free_irq_cnt++;
            sdio_release_host(func);
        }
        dvobj->irq_alloc = 0;
    }
}

#ifdef CONFIG_GPIO_WAKEUP
extern unsigned int oob_irq;
static irqreturn_t gpio_hostwakeup_irq_thread(int irq, void *data)
{
	PADAPTER padapter = (PADAPTER)data;
	DBG_871X_LEVEL(_drv_always_, "gpio_hostwakeup_irq_thread\n");
	/* Disable interrupt before calling handler */
	//disable_irq_nosync(oob_irq);
	rtw_lock_suspend_timeout(HZ/2);
#ifdef CONFIG_PLATFORM_ARM_SUN6I
	return 0;
#else
	return IRQ_HANDLED;
#endif
}

static u8 gpio_hostwakeup_alloc_irq(PADAPTER padapter)
{
	int err;
	if (oob_irq == 0) {
		DBG_871X("oob_irq ZERO!\n");
		return _FAIL;
	}
	/* dont set it IRQF_TRIGGER_LOW, or wowlan */
	/* power is high after suspend */
	/* and failing can prevent can not sleep issue if */
	/* wifi gpio12 pin is not linked with CPU */
	err = request_threaded_irq(oob_irq, gpio_hostwakeup_irq_thread, NULL,
		//IRQF_TRIGGER_LOW | IRQF_ONESHOT,
		IRQF_TRIGGER_FALLING,
		"rtw_wifi_gpio_wakeup", padapter);
	if (err < 0) {
		DBG_871X("Oops: can't allocate gpio irq %d err:%d\n", oob_irq, err);
		return _FALSE;
	} else {
		DBG_871X("allocate gpio irq %d ok\n", oob_irq);
	}
	
#ifndef CONFIG_PLATFORM_ARM_SUN8I	
	enable_irq_wake(oob_irq);
#endif
	return _SUCCESS;
}

static void gpio_hostwakeup_free_irq(PADAPTER padapter)
{
	if (oob_irq == 0)
		return;
		
#ifndef CONFIG_PLATFORM_ARM_SUN8I
	disable_irq_wake(oob_irq);
#endif
	free_irq(oob_irq, padapter);
}
#endif

static u32 sdio_init(struct dvobj_priv *dvobj)
{
	PSDIO_DATA psdio_data;
	struct sdio_func *func;
	int err;

_func_enter_;

	psdio_data = &dvobj->intf_data;
	func = psdio_data->func;

	//3 1. init SDIO bus
	sdio_claim_host(func);

	err = sdio_enable_func(func);
	if (err) {
		dvobj->drv_dbg.dbg_sdio_init_error_cnt++;
		DBG_8192C(KERN_CRIT "%s: sdio_enable_func FAIL(%d)!\n", __func__, err);
		goto release;
	}

	err = sdio_set_block_size(func, 512);
	if (err) {
		dvobj->drv_dbg.dbg_sdio_init_error_cnt++;
		DBG_8192C(KERN_CRIT "%s: sdio_set_block_size FAIL(%d)!\n", __func__, err);
		goto release;
	}
	psdio_data->block_transfer_len = 512;
	psdio_data->tx_block_mode = 1;
	psdio_data->rx_block_mode = 1;

release:
	sdio_release_host(func);

exit:
_func_exit_;

	if (err) return _FAIL;
	return _SUCCESS;
}

static void sdio_deinit(struct dvobj_priv *dvobj)
{
	struct sdio_func *func;
	int err;


	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+sdio_deinit\n"));

	func = dvobj->intf_data.func;

	if (func) {
		sdio_claim_host(func);
		err = sdio_disable_func(func);
		if (err)
		{
			dvobj->drv_dbg.dbg_sdio_deinit_error_cnt++;
			DBG_8192C(KERN_ERR "%s: sdio_disable_func(%d)\n", __func__, err);
		}

		if (dvobj->irq_alloc) {
			err = sdio_release_irq(func);
			if (err)
			{
				dvobj->drv_dbg.dbg_sdio_free_irq_error_cnt++;
				DBG_8192C(KERN_ERR "%s: sdio_release_irq(%d)\n", __func__, err);
			}
			else
				dvobj->drv_dbg.dbg_sdio_free_irq_cnt++;
		}

		sdio_release_host(func);
	}
}
static struct dvobj_priv *sdio_dvobj_init(struct sdio_func *func)
{
	int status = _FAIL;
	struct dvobj_priv *dvobj = NULL;
	PSDIO_DATA psdio;
_func_enter_;

	if((dvobj = devobj_init()) == NULL) {
		goto exit;
	}

	sdio_set_drvdata(func, dvobj);

	psdio = &dvobj->intf_data;
	psdio->func = func;

	if (sdio_init(dvobj) != _SUCCESS) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: initialize SDIO Failed!\n", __FUNCTION__));
		goto free_dvobj;
	}
	rtw_reset_continual_io_error(dvobj);
	status = _SUCCESS;

free_dvobj:
	if (status != _SUCCESS && dvobj) {
		sdio_set_drvdata(func, NULL);
		
		devobj_deinit(dvobj);
		
		dvobj = NULL;
	}
exit:
_func_exit_;
	return dvobj;
}

static void sdio_dvobj_deinit(struct sdio_func *func)
{
	struct dvobj_priv *dvobj = sdio_get_drvdata(func);
_func_enter_;

	sdio_set_drvdata(func, NULL);
	if (dvobj) {
		sdio_deinit(dvobj);
		devobj_deinit(dvobj);
	}

_func_exit_;
	return;
}
static void rtw_decide_chip_type_by_device_id(PADAPTER padapter, const struct sdio_device_id  *pdid)
{
	padapter->chip_type = pdid->driver_data;

#if defined(CONFIG_RTL8723A)
	if( padapter->chip_type == RTL8723A){
		padapter->HardwareType = HARDWARE_TYPE_RTL8723AS;
		DBG_871X("CHIP TYPE: RTL8723A\n");
	}
#endif

#if defined(CONFIG_RTL8188E)
	if(padapter->chip_type == RTL8188E){
		padapter->HardwareType = HARDWARE_TYPE_RTL8188ES;
		DBG_871X("CHIP TYPE: RTL8188E\n");
	}
#endif

#if defined(CONFIG_RTL8723B)
	padapter->chip_type = RTL8723B;
	padapter->HardwareType = HARDWARE_TYPE_RTL8723BS;
#endif

#if defined(CONFIG_RTL8821A)
	if (padapter->chip_type == RTL8821) {
		padapter->HardwareType = HARDWARE_TYPE_RTL8821S;
		DBG_871X("CHIP TYPE: RTL8821A\n");
	}
#endif
}

void rtw_set_hal_ops(PADAPTER padapter)
{
	//alloc memory for HAL DATA
	rtw_hal_data_init(padapter);
	
#if defined(CONFIG_RTL8723A)
	if( padapter->chip_type == RTL8723A){
		rtl8723as_set_hal_ops(padapter);
	}
#endif
#if defined(CONFIG_RTL8188E)
	if(padapter->chip_type == RTL8188E){
		rtl8188es_set_hal_ops(padapter);
	}
#endif
#if defined(CONFIG_RTL8723B)
	if(padapter->chip_type == RTL8723B){
		rtl8723bs_set_hal_ops(padapter);
	}
#endif
#if defined(CONFIG_RTL8821A)
	if(padapter->chip_type == RTL8821){
		rtl8821as_set_hal_ops(padapter);
	}
#endif
}

static void sd_intf_start(PADAPTER padapter)
{
	if (padapter == NULL) {
		DBG_8192C(KERN_ERR "%s: padapter is NULL!\n", __func__);
		return;
	}

	// hal dep
	rtw_hal_enable_interrupt(padapter);
}

static void sd_intf_stop(PADAPTER padapter)
{
	if (padapter == NULL) {
		DBG_8192C(KERN_ERR "%s: padapter is NULL!\n", __func__);
		return;
	}

	// hal dep
	rtw_hal_disable_interrupt(padapter);
}


#ifdef RTW_SUPPORT_PLATFORM_SHUTDOWN
PADAPTER g_test_adapter = NULL;
#endif // RTW_SUPPORT_PLATFORM_SHUTDOWN

_adapter *rtw_sdio_if1_init(struct dvobj_priv *dvobj, const struct sdio_device_id  *pdid)
{
	int status = _FAIL;
	struct net_device *pnetdev;
	PADAPTER padapter = NULL;
	
	if ((padapter = (_adapter *)rtw_zvmalloc(sizeof(*padapter))) == NULL) {
		goto exit;
	}

#ifdef RTW_SUPPORT_PLATFORM_SHUTDOWN
	g_test_adapter = padapter;
#endif // RTW_SUPPORT_PLATFORM_SHUTDOWN
	padapter->dvobj = dvobj;
	dvobj->if1 = padapter;

	padapter->bDriverStopped=_TRUE;

	dvobj->padapters[dvobj->iface_nums++] = padapter;
	padapter->iface_id = IFACE_ID0;

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_DUALMAC_CONCURRENT)
	//set adapter_type/iface type for primary padapter
	padapter->isprimary = _TRUE;
	padapter->adapter_type = PRIMARY_ADAPTER;	
	#ifndef CONFIG_HWPORT_SWAP
	padapter->iface_type = IFACE_PORT0;
	#else
	padapter->iface_type = IFACE_PORT1;
	#endif
#endif

	padapter->interface_type = RTW_SDIO;
	rtw_decide_chip_type_by_device_id(padapter, pdid);
	
	//3 1. init network device data
	pnetdev = rtw_init_netdev(padapter);
	if (!pnetdev)
		goto free_adapter;
	
	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(dvobj));

	padapter = rtw_netdev_priv(pnetdev);

#ifdef CONFIG_IOCTL_CFG80211
	rtw_wdev_alloc(padapter, dvobj_to_dev(dvobj));
#endif

	//3 3. init driver special setting, interface, OS and hardware relative

	//4 3.1 set hardware operation functions
	rtw_set_hal_ops(padapter);


	//3 5. initialize Chip version
	padapter->intf_start = &sd_intf_start;
	padapter->intf_stop = &sd_intf_stop;

	padapter->intf_init = &sdio_init;
	padapter->intf_deinit = &sdio_deinit;
	padapter->intf_alloc_irq = &sdio_alloc_irq;
	padapter->intf_free_irq = &sdio_free_irq;

	if (rtw_init_io_priv(padapter, sdio_set_intf_ops) == _FAIL)
	{
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			("rtw_drv_init: Can't init io_priv\n"));
		goto free_hal_data;
	}

	rtw_hal_read_chip_version(padapter);

	rtw_hal_chip_configure(padapter);

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_Initialize(padapter);
#endif // CONFIG_BT_COEXIST

	//3 6. read efuse/eeprom data
	rtw_hal_read_chip_info(padapter);

	//3 7. init driver common data
	if (rtw_init_drv_sw(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("rtw_drv_init: Initialize driver software resource Failed!\n"));
		goto free_hal_data;
	}

	//3 8. get WLan MAC address
	// set mac addr
	rtw_macaddr_cfg(padapter->eeprompriv.mac_addr);
	rtw_init_wifidirect_addrs(padapter, padapter->eeprompriv.mac_addr, padapter->eeprompriv.mac_addr);

	rtw_hal_disable_interrupt(padapter);

	DBG_871X("bDriverStopped:%d, bSurpriseRemoved:%d, bup:%d, hw_init_completed:%d\n"
		,padapter->bDriverStopped
		,padapter->bSurpriseRemoved
		,padapter->bup
		,padapter->hw_init_completed
	);
	
	status = _SUCCESS;
	
free_hal_data:
	if(status != _SUCCESS && padapter->HalData)
		rtw_mfree(padapter->HalData, sizeof(*(padapter->HalData)));

free_wdev:
	if(status != _SUCCESS) {
		#ifdef CONFIG_IOCTL_CFG80211
		rtw_wdev_unregister(padapter->rtw_wdev);
		rtw_wdev_free(padapter->rtw_wdev);
		#endif
	}

free_adapter:
	if (status != _SUCCESS) {
		if (pnetdev)
			rtw_free_netdev(pnetdev);
		else
			rtw_vmfree((u8*)padapter, sizeof(*padapter));
		padapter = NULL;
	}
exit:
	return padapter;
}

static void rtw_sdio_if1_deinit(_adapter *if1)
{
	struct net_device *pnetdev = if1->pnetdev;
	struct mlme_priv *pmlmepriv= &if1->mlmepriv;

	if(check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd(if1, 0, _FALSE);

#ifdef CONFIG_AP_MODE
	free_mlme_ap_info(if1);
	#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_unload(if1);
	#endif
#endif

#ifdef CONFIG_GPIO_WAKEUP
#ifdef CONFIG_PLATFORM_ARM_SUN6I 
        sw_gpio_eint_set_enable(gpio_eint_wlan, 0);
        sw_gpio_irq_free(eint_wlan_handle);
#else  
	gpio_hostwakeup_free_irq(if1);
#endif
#endif

	rtw_cancel_all_timer(if1);

#ifdef CONFIG_WOWLAN
	adapter_to_pwrctl(if1)->wowlan_mode=_FALSE;
	DBG_871X_LEVEL(_drv_always_, "%s wowlan_mode:%d\n", __func__, adapter_to_pwrctl(if1)->wowlan_mode);
#endif //CONFIG_WOWLAN

	rtw_dev_unload(if1);
	DBG_871X("+r871xu_dev_remove, hw_init_completed=%d\n", if1->hw_init_completed);
	
	rtw_handle_dualmac(if1, 0);

#ifdef CONFIG_IOCTL_CFG80211
	if (if1->rtw_wdev) {
		rtw_wdev_free(if1->rtw_wdev);
	}
#endif

	rtw_free_drv_sw(if1);

	if(pnetdev)
		rtw_free_netdev(pnetdev);

#ifdef CONFIG_PLATFORM_RTD2880B
	DBG_871X("wlan link down\n");
	rtd2885_wlan_netlink_sendMsg("linkdown", "8712");
#endif

#ifdef RTW_SUPPORT_PLATFORM_SHUTDOWN
	g_test_adapter = NULL;
#endif // RTW_SUPPORT_PLATFORM_SHUTDOWN
}

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located a card for us to support.
 *        We accept the new device by returning 0.
 */
static int rtw_drv_init(
	struct sdio_func *func,
	const struct sdio_device_id *id)
{
	int status = _FAIL;
	struct net_device *pnetdev;
	PADAPTER if1 = NULL, if2 = NULL;
	struct dvobj_priv *dvobj;

	RT_TRACE(_module_hci_intfs_c_, _drv_info_,
		("+rtw_drv_init: vendor=0x%04x device=0x%04x class=0x%02x\n",
		func->vendor, func->device, func->class));

	if ((dvobj = sdio_dvobj_init(func)) == NULL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("initialize device object priv Failed!\n"));
		goto exit;
	}

	if ((if1 = rtw_sdio_if1_init(dvobj, id)) == NULL) {
		DBG_871X("rtw_init_primary_adapter Failed!\n");
		goto free_dvobj;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if ((if2 = rtw_drv_if2_init(if1, sdio_set_intf_ops)) == NULL) {
		goto free_if1;
	}
#endif

	//dev_alloc_name && register_netdev
	if((status = rtw_drv_register_netdev(if1)) != _SUCCESS) {
		goto free_if2;
	}

#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_init(if1);
#endif

#ifdef CONFIG_PLATFORM_RTD2880B
	DBG_871X("wlan link up\n");
	rtd2885_wlan_netlink_sendMsg("linkup", "8712");
#endif

	if (sdio_alloc_irq(dvobj) != _SUCCESS)
		goto free_if2;

#ifdef	CONFIG_GPIO_WAKEUP
#ifdef CONFIG_PLATFORM_ARM_SUN6I
        eint_wlan_handle = sw_gpio_irq_request(gpio_eint_wlan, TRIG_EDGE_NEGATIVE,(peint_handle)gpio_hostwakeup_irq_thread, NULL);
        if (!eint_wlan_handle) {
               DBG_871X( "%s: request irq failed\n",__func__);
               return -1;
  }
#else
	gpio_hostwakeup_alloc_irq(if1);
#endif
#endif

#ifdef CONFIG_GLOBAL_UI_PID
	if(ui_pid[1]!=0) {
		DBG_871X("ui_pid[1]:%d\n",ui_pid[1]);
		rtw_signal_process(ui_pid[1], SIGUSR2);
	}
#endif

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-871x_drv - drv_init, success!\n"));

	status = _SUCCESS;

free_if2:
	if(status != _SUCCESS && if2) {
		#ifdef CONFIG_CONCURRENT_MODE
		rtw_drv_if2_stop(if2);
		rtw_drv_if2_free(if2);
		#endif
	}
free_if1:
	if (status != _SUCCESS && if1) {
		rtw_sdio_if1_deinit(if1);
	}
free_dvobj:
	if (status != _SUCCESS)
		sdio_dvobj_deinit(func);
exit:
	return status == _SUCCESS?0:-ENODEV;
}

static void rtw_dev_remove(struct sdio_func *func)
{
	struct dvobj_priv *dvobj = sdio_get_drvdata(func);
	struct pwrctrl_priv *pwrctl = dvobj_to_pwrctl(dvobj);
	PADAPTER padapter = dvobj->if1;

_func_enter_;

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+rtw_dev_remove\n"));

	dvobj->processing_dev_remove = _TRUE;

	rtw_unregister_netdevs(dvobj);

	if (padapter->bSurpriseRemoved == _FALSE) {
		int err;

		/* test surprise remove */
		sdio_claim_host(func);
		sdio_readb(func, 0, &err);
		sdio_release_host(func);
		if (err == -ENOMEDIUM) {
			padapter->bSurpriseRemoved = _TRUE;
			DBG_871X(KERN_NOTICE "%s: device had been removed!\n", __func__);
		}
	}

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(pwrctl);
#endif

	rtw_ps_deny(padapter, PS_DENY_DRV_REMOVE);

	rtw_pm_set_ips(padapter, IPS_NONE);
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	LeaveAllPowerSaveMode(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_if2_stop(dvobj->if2);
#endif

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_HaltNotify(padapter);
#endif

	rtw_sdio_if1_deinit(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_if2_free(dvobj->if2);
#endif

	sdio_dvobj_deinit(func);

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("-rtw_dev_remove\n"));

_func_exit_;
}
extern int pm_netdev_open(struct net_device *pnetdev,u8 bnormal);
extern int pm_netdev_close(struct net_device *pnetdev,u8 bnormal);

static int rtw_sdio_suspend(struct device *dev)
{
	struct sdio_func *func =dev_to_sdio_func(dev);
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(psdpriv);
	_adapter *padapter = psdpriv->if1;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	int ret = 0;
	u8 ch, bw, offset;

	if(padapter->bDriverStopped == _TRUE)
	{
		DBG_871X("%s bDriverStopped = %d\n", __FUNCTION__, padapter->bDriverStopped);
		goto exit;
	}

	if (pwrpriv->bInSuspend == _TRUE)
	{
		DBG_871X("%s bInSuspend = %d\n", __FUNCTION__, pwrpriv->bInSuspend);
		pdbgpriv->dbg_suspend_error_cnt++;
		goto exit;
	}

	ret = rtw_suspend_common(padapter);		

exit:
#ifdef CONFIG_RTW_SDIO_PM_KEEP_POWER 
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,34))
	//Android 4.0 don't support WIFI close power
	//or power down or clock will close after wifi resume,
	//this is sprd's bug in Android 4.0, but sprd don't
	//want to fix it.
	//we have test power under 8723as, power consumption is ok
	if (func) {
		mmc_pm_flag_t pm_flag = 0;
		pm_flag = sdio_get_host_pm_caps(func);
		DBG_871X("cmd: %s: suspend: PM flag = 0x%x\n", sdio_func_id(func), pm_flag);
		if (!(pm_flag & MMC_PM_KEEP_POWER)) {
			DBG_871X("%s: cannot remain alive while host is suspended\n", sdio_func_id(func));
			pdbgpriv->dbg_suspend_error_cnt++;
			return -ENOSYS;
		} else {
			DBG_871X("cmd: suspend with MMC_PM_KEEP_POWER\n");
			sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
		}
	}
#endif	
#endif
	return ret;
}
int rtw_resume_process(_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
		
	if (pwrpriv->bInSuspend == _FALSE)
	{
		pdbgpriv->dbg_resume_error_cnt++;
		DBG_871X("%s bInSuspend = %d\n", __FUNCTION__, pwrpriv->bInSuspend);
		return -1;
	}
	
	return rtw_resume_common(padapter);
}

static int rtw_sdio_resume(struct device *dev)
{
	struct sdio_func *func =dev_to_sdio_func(dev);
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(psdpriv);
	_adapter *padapter = psdpriv->if1;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	int ret = 0;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	DBG_871X("==> %s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);

	pdbgpriv->dbg_resume_cnt++;

	if(pwrpriv->bInternalAutoSuspend)
	{
 		ret = rtw_resume_process(padapter);
	}
	else
	{
		if(pwrpriv->wowlan_mode || pwrpriv->wowlan_ap_mode)
		{
			rtw_resume_lock_suspend();			
			ret = rtw_resume_process(padapter);
			rtw_resume_unlock_suspend();
		}
		else
		{
#ifdef CONFIG_RESUME_IN_WORKQUEUE
			rtw_resume_in_workqueue(pwrpriv);
#else			
			if (rtw_is_earlysuspend_registered(pwrpriv))
			{
				/* jeff: bypass resume here, do in late_resume */
				rtw_set_do_late_resume(pwrpriv, _TRUE);
			}	
			else
			{
				rtw_resume_lock_suspend();			
				ret = rtw_resume_process(padapter);
				rtw_resume_unlock_suspend();
			}
#endif
		}
	}
	pmlmeext->last_scan_time = rtw_get_current_time();
	DBG_871X("<========  %s return %d\n", __FUNCTION__, ret);
	return ret;

}

static int rtw_drv_entry(void)
{
	int ret = 0;

	DBG_871X_LEVEL(_drv_always_, "module init start\n");
	dump_drv_version(RTW_DBGDUMP);
#ifdef BTCOEXVERSION
	DBG_871X_LEVEL(_drv_always_, DRV_NAME" BT-Coex version = %s\n", BTCOEXVERSION);
#endif // BTCOEXVERSION

	ret = platform_wifi_power_on();
	if (ret)
	{
		DBG_871X("%s: power on failed!!(%d)\n", __FUNCTION__, ret);
		ret = -1;
		goto exit;
	}

	sdio_drvpriv.drv_registered = _TRUE;
	rtw_suspend_lock_init();
	rtw_drv_proc_init();
	rtw_ndev_notifier_register();

	ret = sdio_register_driver(&sdio_drvpriv.r871xs_drv);
	if (ret != 0)
	{
		sdio_drvpriv.drv_registered = _FALSE;
		rtw_suspend_lock_uninit();
		rtw_drv_proc_deinit();
		rtw_ndev_notifier_unregister();
		DBG_871X("%s: register driver failed!!(%d)\n", __FUNCTION__, ret);
		goto poweroff;
	}

	rtw_android_wifictrl_func_add();

	goto exit;

poweroff:
	platform_wifi_power_off();

exit:
	DBG_871X_LEVEL(_drv_always_, "module init ret=%d\n", ret);
	return ret;
}

static void rtw_drv_halt(void)
{
	DBG_871X_LEVEL(_drv_always_, "module exit start\n");

	sdio_drvpriv.drv_registered = _FALSE;

	sdio_unregister_driver(&sdio_drvpriv.r871xs_drv);

	rtw_android_wifictrl_func_del();

	platform_wifi_power_off();

	rtw_suspend_lock_uninit();
	rtw_drv_proc_deinit();
	rtw_ndev_notifier_unregister();

	DBG_871X_LEVEL(_drv_always_, "module exit success\n");

	rtw_mstat_dump(RTW_DBGDUMP);
}

#include "wifi_version.h"

extern int rockchip_wifi_power(int on);
extern int rockchip_wifi_set_carddetect(int val);

int rockchip_wifi_init_module_rtkwifi(void)
{
    printk("\n");
    printk("=======================================================\n");
    printk("==== Launching Wi-Fi driver! (Powered by Rockchip) ====\n");
    printk("=======================================================\n");
    printk("Realtek 8723BS SDIO WiFi driver (Powered by Rockchip,Ver %s) init.\n", RTL8723BS_DRV_VERSION);

    rockchip_wifi_power(1);
    rockchip_wifi_set_carddetect(1);    
    return rtw_drv_entry();
}

void rockchip_wifi_exit_module_rtkwifi(void)
{
    printk("\n");
    printk("=======================================================\n");
    printk("==== Dislaunching Wi-Fi driver! (Powered by Rockchip) ====\n");
    printk("=======================================================\n");
    printk("Realtek 8723BS SDIO WiFi driver (Powered by Rockchip,Ver %s) init.\n", RTL8723BS_DRV_VERSION);

    rtw_drv_halt();

    rockchip_wifi_set_carddetect(0);
    rockchip_wifi_power(0);
}

EXPORT_SYMBOL(rockchip_wifi_init_module_rtkwifi);
EXPORT_SYMBOL(rockchip_wifi_exit_module_rtkwifi);
//module_init(rtw_drv_entry);
//module_exit(rtw_drv_halt);

