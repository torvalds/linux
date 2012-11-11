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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _HCI_INTF_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <rtw_version.h>

#ifndef CONFIG_SDIO_HCI
#error "CONFIG_SDIO_HCI shall be on!\n"
#endif

#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>

#ifdef CONFIG_RTL8723A
#include <rtl8723a_hal.h>
#endif

#ifdef CONFIG_RTL8188E
#include <rtl8188e_hal.h>
#endif

#include <hal_intf.h>
#include <sdio_hal.h>
#include <sdio_ops.h>

#ifdef CONFIG_PLATFORM_ARM_SUN4I
#if defined(CONFIG_MMC_SUNXI_POWER_CONTROL)
#define SDIOID (CONFIG_CHIP_ID==1123 ? 3 : 1)
#define SUNXI_SDIO_WIFI_NUM_RTL8189ES  10
extern void sunximmc_rescan_card(unsigned id, unsigned insert);
extern int mmc_pm_get_mod_type(void);
extern int mmc_pm_gpio_ctrl(char* name, int level);
/*
*	rtl8189es_shdn	= port:PH09<1><default><default><0>
*	rtl8189es_wakeup	= port:PH10<1><default><default><1>
*	rtl8189es_vdd_en  = port:PH11<1><default><default><0>
*	rtl8189es_vcc_en  = port:PH12<1><default><default><0>
*/

int rtl8189es_sdio_powerup(void)
{
	mmc_pm_gpio_ctrl("rtl8189es_vdd_en", 1);   
	udelay(100);   
	mmc_pm_gpio_ctrl("rtl8189es_vcc_en", 1);    
	udelay(50);    
	mmc_pm_gpio_ctrl("rtl8189es_shdn", 1);        
	return 0;
}
int rtl8189es_sdio_poweroff(void)
{    
	mmc_pm_gpio_ctrl("rtl8189es_shdn", 0);    
	mmc_pm_gpio_ctrl("rtl8189es_vcc_en", 0);   
	mmc_pm_gpio_ctrl("rtl8189es_vdd_en", 0);            
	return 0;
}
#endif //defined(CONFIG_MMC_SUNXI_POWER_CONTROL)
#endif //CONFIG_PLATFORM_ARM_SUN4I


#ifndef dev_to_sdio_func
#define dev_to_sdio_func(d)     container_of(d, struct sdio_func, dev)
#endif

static const struct sdio_device_id sdio_ids[] = {
#ifdef CONFIG_RTL8723A
	{ SDIO_DEVICE(0x024c, 0x8723) },
#endif //CONFIG_RTL8723A

#ifdef CONFIG_RTL8188E
	{ SDIO_DEVICE(0x024c, 0x8179) },
#endif //CONFIG_RTL8188E

//	{ SDIO_DEVICE_CLASS(SDIO_CLASS_WLAN)		},
//	{ /* end: all zeroes */				},
};

typedef struct _driver_priv {
	struct sdio_driver r871xs_drv;

	int drv_registered;

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_DUALMAC_CONCURRENT)
	//global variable
	_mutex h2c_fwcmd_mutex;
	_mutex setch_mutex;
	_mutex setbw_mutex;
	_mutex hw_init_mutex;
#endif	
} drv_priv, *pdrv_priv;

static void sd_sync_int_hdl(struct sdio_func *func)
{
	struct dvobj_priv *psdpriv;


	psdpriv = sdio_get_drvdata(func);
	sd_int_hdl(psdpriv->padapter);
}

static u32 sdio_init(PADAPTER padapter)
{
	struct dvobj_priv *psddev;
	PSDIO_DATA psdio_data;
	struct sdio_func *func;
	int err;

_func_enter_;

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+sdio_init\n"));
	if (padapter == NULL) {
		printk(KERN_ERR "%s: padapter is NULL!\n", __func__);
		err = -1;
		goto exit;
	}

	psddev = &padapter->dvobjpriv;
	psdio_data = &psddev->intf_data;
	func = psdio_data->func;

	//3 1. init SDIO bus
	sdio_claim_host(func);

	err = sdio_enable_func(func);
	if (err) {
		printk(KERN_CRIT "%s: sdio_enable_func FAIL(%d)!\n", __func__, err);
		goto release;
	}

	err = sdio_set_block_size(func, 512);
	if (err) {
		printk(KERN_CRIT "%s: sdio_set_block_size FAIL(%d)!\n", __func__, err);
		goto release;
	}
	psdio_data->block_transfer_len = 512;
	psdio_data->tx_block_mode = 1;
	psdio_data->rx_block_mode = 1;

	err = sdio_claim_irq(func, &sd_sync_int_hdl);
	if (err) {
		printk(KERN_CRIT "%s: sdio_claim_irq FAIL(%d)!\n", __func__, err);
		goto release;
	}

release:
	sdio_release_host(func);

exit:
_func_exit_;

	if (err) return _FAIL;
	return _SUCCESS;
}

static void sdio_deinit(PADAPTER padapter)
{
	struct dvobj_priv *psddev;
	struct sdio_func *func;
	int err;


	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+sdio_deinit\n"));

	if (padapter == NULL) {
		printk(KERN_ERR "%s: padapter is NULL!\n", __func__);
		return;
	}
	psddev = &padapter->dvobjpriv;
	func = psddev->intf_data.func;

	if (func) {
		sdio_claim_host(func);
		err = sdio_disable_func(func);
		if (err)
			printk(KERN_ERR "%s: sdio_disable_func(%d)\n", __func__, err);
		err = sdio_release_irq(func);
		if (err)
			printk(KERN_ERR "%s: sdio_release_irq(%d)\n", __func__, err);
		sdio_release_host(func);
	}
}

static void decide_chip_type_by_device_id(PADAPTER padapter, u32 id)
{
	padapter->chip_type = NULL_CHIP_TYPE;

	switch (id)
	{
		case 0x8723:
			padapter->chip_type = RTL8188C_8192C;
			padapter->HardwareType = HARDWARE_TYPE_RTL8723AS;
			break;
		case 0x8179:
			padapter->chip_type = RTL8188E;
			padapter->HardwareType = HARDWARE_TYPE_RTL8188ES;
			break;
	}
}

static void sd_intf_start(PADAPTER padapter)
{
	if (padapter == NULL) {
		printk(KERN_ERR "%s: padapter is NULL!\n", __func__);
		return;
	}

	//hal dep
	rtw_hal_enable_interrupt(padapter);
}

static void sd_intf_stop(PADAPTER padapter)
{
	if (padapter == NULL) {
		printk(KERN_ERR "%s: padapter is NULL!\n", __func__);
		return;
	}

	// hal dep
	rtw_hal_disable_interrupt(padapter);	
}

extern char* ifname;
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
	struct net_device *pnetdev;
	PADAPTER padapter;
	struct dvobj_priv *pdvobjpriv;
	PSDIO_DATA psdio;


	RT_TRACE(_module_hci_intfs_c_, _drv_info_,
		 ("+rtw_drv_init: vendor=0x%04x device=0x%04x class=0x%02x\n",
		  func->vendor, func->device, func->class));

	//3 1. init network device data
	pnetdev = rtw_init_netdev(NULL);
	if (!pnetdev) goto error;

	SET_NETDEV_DEV(pnetdev, &func->dev);

	padapter = rtw_netdev_priv(pnetdev);
	pdvobjpriv = &padapter->dvobjpriv;
	pdvobjpriv->padapter = padapter;
	psdio = &pdvobjpriv->intf_data;
	psdio->func = func;

#ifdef CONFIG_IOCTL_CFG80211
	rtw_wdev_alloc(padapter, &func->dev);
#endif

	//3 2. set interface private data
	sdio_set_drvdata(func, pdvobjpriv);

	//3 3. init driver special setting, interface, OS and hardware relative
	// set interface_type to sdio
	padapter->interface_type = RTW_SDIO;
	decide_chip_type_by_device_id(padapter, (u32)func->device);

	//4 3.1 set hardware operation functions
	padapter->HalData = rtw_zmalloc(sizeof(HAL_DATA_TYPE));
	if (padapter->HalData == NULL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("rtw_drv_init: can't alloc memory for HAL DATA\n"));
		goto error;
	}
	padapter->hal_data_sz = sizeof(HAL_DATA_TYPE);
	set_hal_ops(padapter);

	//3 4. interface init
	if (sdio_init(padapter) != _SUCCESS) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("%s: initialize SDIO Failed!\n", __FUNCTION__));
		goto error;
	}

	padapter->intf_start = &sd_intf_start;
	padapter->intf_stop = &sd_intf_stop;

	//3 5. register I/O operations
	if (rtw_init_io_priv(padapter) == _FAIL)
	{
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			("rtw_drv_init: Can't init io_priv\n"));
		goto deinit;
	}

	//3 6.
	rtw_hal_read_chip_version(padapter);

	//3 7.
	rtw_hal_chip_configure(padapter);

	//3 8. read efuse/eeprom data
	rtw_hal_read_chip_info(padapter);

	//3 9. init driver common data
	if (rtw_init_drv_sw(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("rtw_drv_init: Initialize driver software resource Failed!\n"));
		goto deinit;
	}

	//3 10. get WLan MAC address
	// alloc dev name after read efuse.
	rtw_init_netdev_name(pnetdev, ifname);

	rtw_macaddr_cfg(padapter->eeprompriv.mac_addr);
	_rtw_memcpy(pnetdev->dev_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);


#ifdef CONFIG_PROC_DEBUG
#ifdef RTK_DMP_PLATFORM
	rtw_proc_init_one(pnetdev);
#endif
#endif

#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_init(padapter);
#endif

#ifdef CONFIG_CONCURRENT_MODE
	//set global variable to primary adapter
	padapter->ph2c_fwcmd_mutex = &drvpriv.h2c_fwcmd_mutex;
	padapter->psetch_mutex = &drvpriv.setch_mutex;
	padapter->psetbw_mutex = &drvpriv.setbw_mutex;	
	padapter->hw_init_mutex = &drvpriv.hw_init_mutex;	
#endif

#ifdef CONFIG_PLATFORM_RTD2880B
	DBG_871X("wlan link up\n");
	rtd2885_wlan_netlink_sendMsg("linkup", "8712");
#endif


#ifdef CONFIG_GLOBAL_UI_PID
	if(ui_pid[1]!=0) {
		DBG_871X("ui_pid[1]:%d\n",ui_pid[1]);
		rtw_signal_process(ui_pid[1], SIGUSR2);
	}
#endif

	DBG_871X("bDriverStopped:%d, bSurpriseRemoved:%d, bup:%d, hw_init_completed:%d\n"
		,padapter->bDriverStopped
		,padapter->bSurpriseRemoved
		,padapter->bup
		,padapter->hw_init_completed
	);


	//3 8. Tell the network stack we exist
	if (register_netdev(pnetdev) != 0) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("rtw_drv_init: register_netdev() failed\n"));
		goto deinit;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_drv_if2_init(padapter, NULL)==NULL)
	{
		goto deinit;
	}	
#endif

	RT_TRACE(_module_hci_intfs_c_, _drv_info_,
		 ("-rtw_drv_init: Success. bDriverStopped=%d bSurpriseRemoved=%d\n",
		  padapter->bDriverStopped, padapter->bSurpriseRemoved));

	return 0;

deinit:
	sdio_deinit(padapter);

error:
	if (padapter)
	{
		if (padapter->HalData && padapter->hal_data_sz>0) {			
			rtw_mfree(padapter->HalData, padapter->hal_data_sz);
			padapter->HalData = NULL;
		}
	}

	if (pnetdev)
		rtw_free_netdev(pnetdev);

	RT_TRACE(_module_hci_intfs_c_, _drv_crit_, ("-rtw_drv_init: FAIL!\n"));

	return -1;
}

/*
 * Do deinit job corresponding to netdev_open()
 */
static void rtw_dev_unload(PADAPTER padapter)
{
	struct net_device *pnetdev = (struct net_device*)padapter->pnetdev;


	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+rtw_dev_unload\n"));

	padapter->bDriverStopped = _TRUE;

	if (padapter->bup == _TRUE)
	{
		// stop TX
//		val8 = 0xFF;
//		rtw_hal_set_hwreg(padapter, HW_VAR_TXPAUSE,&val8);

#if 0
		if (padapter->intf_stop)
			padapter->intf_stop(padapter);
#else
		sd_intf_stop(padapter);
#endif
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("@ rtw_dev_unload: stop intf complete!\n"));

		if (!padapter->pwrctrlpriv.bInternalAutoSuspend)
			rtw_stop_drv_threads(padapter);
		
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("@ rtw_dev_unload: stop thread complete!\n"));

		if (padapter->bSurpriseRemoved == _FALSE)
		{
#ifdef CONFIG_WOWLAN
			if (padapter->pwrctrlpriv.bSupportWakeOnWlan == _TRUE) {
				DBG_871X("%s bSupportWakeOnWlan==_TRUE  do not run rtw_hal_deinit()\n",__FUNCTION__);
			}
			else
#endif
			{
				rtw_hal_deinit(padapter);
			}
			padapter->bSurpriseRemoved = _TRUE;
		}
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("@ rtw_dev_unload: deinit hal complelt!\n"));

		padapter->bup = _FALSE;
	}
	else {
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("rtw_dev_unload: bup==_FALSE\n"));
	}

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("-rtw_dev_unload\n"));
}

static void rtw_dev_remove(struct sdio_func *func)
{
	PADAPTER padapter;
	struct net_device *pnetdev;
#ifdef CONFIG_IOCTL_CFG80211
	struct wireless_dev *wdev;
#endif

_func_enter_;

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+rtw_dev_remove\n"));

	padapter = ((struct dvobj_priv*)sdio_get_drvdata(func))->padapter;
#ifdef CONFIG_IOCTL_CFG80211
	wdev = padapter->rtw_wdev;
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND ) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(&padapter->pwrctrlpriv);
#endif

	if (padapter->bSurpriseRemoved == _FALSE)
	{
		// test surprise remove
		int err;

		sdio_claim_host(func);
		sdio_readb(func, 0, &err);
		sdio_release_host(func);
		if (err == -ENOMEDIUM) {
			padapter->bSurpriseRemoved = _TRUE;
			DBG_871X(KERN_NOTICE "%s: device had been removed!\n", __func__);
		}
	}

#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_unload(padapter);
#endif
	LeaveAllPowerSaveMode(padapter);

	pnetdev = (struct net_device*)padapter->pnetdev;
	if (pnetdev) {
		unregister_netdev(pnetdev); //will call netdev_close()
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("rtw_dev_remove: unregister netdev\n"));
#ifdef CONFIG_PROC_DEBUG
		rtw_proc_remove_one(pnetdev);
#endif
	} else {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("rtw_dev_remove: NO padapter->pnetdev!\n"));
	}

	rtw_cancel_all_timer(padapter);

	rtw_dev_unload(padapter);

	// interface deinit
	sdio_deinit(padapter);
	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("rtw_dev_remove: deinit SDIO complete!\n"));

	rtw_free_drv_sw(padapter);

#ifdef CONFIG_IOCTL_CFG80211
	rtw_wdev_free(wdev);
#endif

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("-rtw_dev_remove\n"));

_func_exit_;
}

static int rtw_sdio_suspend(struct device *dev)
{
	struct sdio_func *func =dev_to_sdio_func(dev);
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	_adapter *padapter = psdpriv->padapter;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct net_device *pnetdev = padapter->pnetdev;
	int ret = 0;
	u32 start_time = rtw_get_current_time();
	
	_func_enter_;

	DBG_871X("==> %s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);

	if((!padapter->bup) || (padapter->bDriverStopped)||(padapter->bSurpriseRemoved))
	{
		DBG_871X("%s bup=%d bDriverStopped=%d bSurpriseRemoved = %d\n", __FUNCTION__
			,padapter->bup, padapter->bDriverStopped,padapter->bSurpriseRemoved);
		goto exit;
	}
	
	pwrpriv->bInSuspend = _TRUE;		
	rtw_cancel_all_timer(padapter);		
	LeaveAllPowerSaveMode(padapter);
	
	//padapter->net_closed = _TRUE;
	//s1.
	if(pnetdev)
	{
		netif_carrier_off(pnetdev);
		rtw_netif_stop_queue(pnetdev);
	}
#ifdef CONFIG_WOWLAN
	padapter->pwrctrlpriv.bSupportWakeOnWlan=_TRUE;
#else		
	//s2.
	//s2-1.  issue rtw_disassoc_cmd to fw
	disconnect_hdl(padapter, NULL);
	//rtw_disassoc_cmd(padapter);
#endif

#ifdef CONFIG_LAYER2_ROAMING_RESUME
	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED) )
	{
		DBG_871X("%s %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n",__FUNCTION__,
				pmlmepriv->cur_network.network.Ssid.Ssid,
				MAC_ARG(pmlmepriv->cur_network.network.MacAddress),
				pmlmepriv->cur_network.network.Ssid.SsidLength,
				pmlmepriv->assoc_ssid.SsidLength);
		
		pmlmepriv->to_roaming = 1;
	}
#endif
		
	//s2-2.  indicate disconnect to os
	rtw_indicate_disconnect(padapter);
	//s2-3.
	rtw_free_assoc_resources(padapter, 1);

	//s2-4.
	rtw_free_network_queue(padapter, _TRUE);

	rtw_led_control(padapter, LED_CTL_POWER_OFF);

	rtw_dev_unload(padapter);

	if(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_indicate_scan_done(padapter, 1);

	if(check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		rtw_indicate_disconnect(padapter);

	// interface deinit
	sdio_deinit(padapter);
	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("%s: deinit SDIO complete!\n", __FUNCTION__));

exit:
	DBG_871X("<===  %s return %d.............. in %dms\n", __FUNCTION__
		, ret, rtw_get_passing_time_ms(start_time));

	_func_exit_;
	return ret;
}

extern int pm_netdev_open(struct net_device *pnetdev,u8 bnormal);
int rtw_resume_process(_adapter *padapter)
{
	struct net_device *pnetdev;
	struct pwrctrl_priv *pwrpriv;
	u8 is_pwrlock_hold_by_caller;
	u8 is_directly_called_by_auto_resume;
	int ret = 0;
	u32 start_time = rtw_get_current_time();
	
	_func_enter_;

	DBG_871X("==> %s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);

	if (padapter) {
		pnetdev = padapter->pnetdev;
		pwrpriv = &padapter->pwrctrlpriv;
	} else {
		ret = -1;
		goto exit;
	}

	// interface init
	if (sdio_init(padapter) != _SUCCESS)
	{
		ret = -1;
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: initialize SDIO Failed!!\n", __FUNCTION__));
		goto exit;
	}

	rtw_reset_drv_sw(padapter);
	pwrpriv->bkeepfwalive = _FALSE;

	DBG_871X("bkeepfwalive(%x)\n",pwrpriv->bkeepfwalive);
	if(pm_netdev_open(pnetdev,_TRUE) != 0) {
		ret = -1;
		goto exit;
	}

	netif_device_attach(pnetdev);	
	netif_carrier_on(pnetdev);

	if( padapter->pid[1]!=0) {
		DBG_871X("pid[1]:%d\n",padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}	

	#ifdef CONFIG_LAYER2_ROAMING_RESUME
	rtw_roaming(padapter, NULL);
	#endif

	#ifdef CONFIG_RESUME_IN_WORKQUEUE
	rtw_unlock_suspend();
	#endif //CONFIG_RESUME_IN_WORKQUEUE

exit:
	DBG_871X("<===  %s return %d.............. in %dms\n", __FUNCTION__
		, ret, rtw_get_passing_time_ms(start_time));

	_func_exit_;
	
	return ret;
}

static int rtw_sdio_resume(struct device *dev)
{
	struct sdio_func *func =dev_to_sdio_func(dev);
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	_adapter *padapter = psdpriv->padapter;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	 int ret = 0;
 
	if(pwrpriv->bInternalAutoSuspend ){
 		ret = rtw_resume_process(padapter);
	} else {
#ifdef CONFIG_RESUME_IN_WORKQUEUE
		rtw_resume_in_workqueue(pwrpriv);
#elif defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
		if(rtw_is_earlysuspend_registered(pwrpriv)) {
			//jeff: bypass resume here, do in late_resume
			pwrpriv->do_late_resume = _TRUE;
		} else {
			ret = rtw_resume_process(padapter);
		}
#else // Normal resume process
		ret = rtw_resume_process(padapter);
#endif //CONFIG_RESUME_IN_WORKQUEUE
	}
	
	return ret;

}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)) 
static const struct dev_pm_ops rtw_sdio_pm_ops = {
	.suspend	= rtw_sdio_suspend,
	.resume	= rtw_sdio_resume,
};
#endif

static drv_priv drvpriv = {
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

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)) 
extern int console_suspend_enabled;
#endif

static int __init rtw_drv_entry(void)
{
	int ret=0;
	
#ifdef CONFIG_PLATFORM_ARM_SUN4I
/*depends on sunxi power control */
#if defined CONFIG_MMC_SUNXI_POWER_CONTROL    
	unsigned int mod_sel = mmc_pm_get_mod_type();	
	if(mod_sel == SUNXI_SDIO_WIFI_NUM_RTL8189ES) 
	{		
		rtl8189es_sdio_powerup();  		
		sunximmc_rescan_card(SDIOID, 1);		
		printk("[rtl8189es] %s: power up, rescan card.\n", __FUNCTION__);  			
	} 
	else 
	{		
		ret = -1;		
		printk("[rtl8189es] %s: mod_sel = %d is incorrect.\n", __FUNCTION__, mod_sel);	
	}
#endif	// defined CONFIG_MMC_SUNXI_POWER_CONTROL
	if(ret != 0)		
		goto exit;
	
#endif //CONFIG_PLATFORM_ARM_SUN4I

//	DBG_871X(KERN_INFO "+%s", __func__);
	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+rtw_drv_entry\n"));

	DBG_871X(DRV_NAME " driver version=%s\n", DRIVERVERSION);
	DBG_871X("build time: %s %s\n", __DATE__, __TIME__);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)) 
	//console_suspend_enabled=0;
#endif
	
	rtw_suspend_lock_init();

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_DUALMAC_CONCURRENT)
	//init global variable
	_rtw_mutex_init(&drvpriv.h2c_fwcmd_mutex);
	_rtw_mutex_init(&drvpriv.setch_mutex);
	_rtw_mutex_init(&drvpriv.setbw_mutex);
	_rtw_mutex_init(&drvpriv.hw_init_mutex);
#endif
	
	drvpriv.drv_registered = _TRUE;

	ret = sdio_register_driver(&drvpriv.r871xs_drv);

exit:	
//	DBG_871X(KERN_INFO "-%s: ret=%d", __func__, ret);
	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("-rtw_drv_entry: ret=%d\n", ret));

	return ret;
}

static void __exit rtw_drv_halt(void)
{
//	DBG_871X(KERN_INFO "+%s", __func__);
	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+rtw_drv_halt\n"));

	rtw_suspend_lock_uninit();
	drvpriv.drv_registered = _FALSE;

	sdio_unregister_driver(&drvpriv.r871xs_drv);

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_DUALMAC_CONCURRENT)
	_rtw_mutex_free(&drvpriv.h2c_fwcmd_mutex);
	_rtw_mutex_free(&drvpriv.setch_mutex);
	_rtw_mutex_free(&drvpriv.setbw_mutex);
	_rtw_mutex_free(&drvpriv.hw_init_mutex);
#endif

#ifdef CONFIG_PLATFORM_ARM_SUN4I
#if defined(CONFIG_MMC_SUNXI_POWER_CONTROL)	
	sunximmc_rescan_card(SDIOID, 0);
#ifdef CONFIG_RTL8188E	
	rtl8189es_sdio_poweroff();	
	printk("[rtl8189es] %s: remove card, power off.\n", __FUNCTION__);
#endif //CONFIG_RTL8188E
#endif //defined(CONFIG_MMC_SUNXI_POWER_CONTROL)
#endif //CONFIG_PLATFORM_ARM_SUN4I

//	DBG_871X(KERN_INFO "-%s", __func__);
	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("-rtw_drv_halt\n"));
}


module_init(rtw_drv_entry);
module_exit(rtw_drv_halt);

