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
 ******************************************************************************/
#define _HCI_INTF_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <linux/jiffies.h>

#ifndef dev_to_sdio_func
#define dev_to_sdio_func(d)     container_of(d, struct sdio_func, dev)
#endif

static const struct sdio_device_id sdio_ids[] =
{
	{ SDIO_DEVICE(0x024c, 0x0523), },
	{ SDIO_DEVICE(0x024c, 0x0623), },
	{ SDIO_DEVICE(0x024c, 0x0626), },
	{ SDIO_DEVICE(0x024c, 0xb723), },
	{ /* end: all zeroes */				},
};
static const struct acpi_device_id acpi_ids[] = {
	{"OBDA8723", 0x0000},
	{}
};

MODULE_DEVICE_TABLE(sdio, sdio_ids);
MODULE_DEVICE_TABLE(acpi, acpi_ids);

static int rtw_drv_init(struct sdio_func *func, const struct sdio_device_id *id);
static void rtw_dev_remove(struct sdio_func *func);
static int rtw_sdio_resume(struct device *dev);
static int rtw_sdio_suspend(struct device *dev);

static const struct dev_pm_ops rtw_sdio_pm_ops = {
	.suspend	= rtw_sdio_suspend,
	.resume	= rtw_sdio_resume,
};

struct sdio_drv_priv {
	struct sdio_driver r871xs_drv;
	int drv_registered;
};

static struct sdio_drv_priv sdio_drvpriv = {
	.r871xs_drv.probe = rtw_drv_init,
	.r871xs_drv.remove = rtw_dev_remove,
	.r871xs_drv.name = "rtl8723bs",
	.r871xs_drv.id_table = sdio_ids,
	.r871xs_drv.drv = {
		.pm = &rtw_sdio_pm_ops,
	}
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

static int sdio_alloc_irq(struct dvobj_priv *dvobj)
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

static void sdio_free_irq(struct dvobj_priv *dvobj)
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
	struct adapter *padapter = data;
	DBG_871X_LEVEL(_drv_always_, "gpio_hostwakeup_irq_thread\n");
	/* Disable interrupt before calling handler */
	/* disable_irq_nosync(oob_irq); */
	rtw_lock_suspend_timeout(HZ/2);
	return IRQ_HANDLED;
}

static u8 gpio_hostwakeup_alloc_irq(struct adapter *padapter)
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
		/* IRQF_TRIGGER_LOW | IRQF_ONESHOT, */
		IRQF_TRIGGER_FALLING,
		"rtw_wifi_gpio_wakeup", padapter);
	if (err < 0) {
		DBG_871X("Oops: can't allocate gpio irq %d err:%d\n", oob_irq, err);
		return false;
	} else {
		DBG_871X("allocate gpio irq %d ok\n", oob_irq);
	}

	enable_irq_wake(oob_irq);
	return _SUCCESS;
}

static void gpio_hostwakeup_free_irq(struct adapter *padapter)
{
	if (oob_irq == 0)
		return;

	disable_irq_wake(oob_irq);
	free_irq(oob_irq, padapter);
}
#endif

static u32 sdio_init(struct dvobj_priv *dvobj)
{
	PSDIO_DATA psdio_data;
	struct sdio_func *func;
	int err;

	psdio_data = &dvobj->intf_data;
	func = psdio_data->func;

	/* 3 1. init SDIO bus */
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

	if (err)
		return _FAIL;
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

	dvobj = devobj_init();
	if (dvobj == NULL) {
		goto exit;
	}

	sdio_set_drvdata(func, dvobj);

	psdio = &dvobj->intf_data;
	psdio->func = func;

	if (sdio_init(dvobj) != _SUCCESS) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: initialize SDIO Failed!\n", __func__));
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
	return dvobj;
}

static void sdio_dvobj_deinit(struct sdio_func *func)
{
	struct dvobj_priv *dvobj = sdio_get_drvdata(func);

	sdio_set_drvdata(func, NULL);
	if (dvobj) {
		sdio_deinit(dvobj);
		devobj_deinit(dvobj);
	}
	return;
}

void rtw_set_hal_ops(struct adapter *padapter)
{
	/* alloc memory for HAL DATA */
	rtw_hal_data_init(padapter);

	rtl8723bs_set_hal_ops(padapter);
}

static void sd_intf_start(struct adapter *padapter)
{
	if (padapter == NULL) {
		DBG_8192C(KERN_ERR "%s: padapter is NULL!\n", __func__);
		return;
	}

	/*  hal dep */
	rtw_hal_enable_interrupt(padapter);
}

static void sd_intf_stop(struct adapter *padapter)
{
	if (padapter == NULL) {
		DBG_8192C(KERN_ERR "%s: padapter is NULL!\n", __func__);
		return;
	}

	/*  hal dep */
	rtw_hal_disable_interrupt(padapter);
}


static struct adapter *rtw_sdio_if1_init(struct dvobj_priv *dvobj, const struct sdio_device_id  *pdid)
{
	int status = _FAIL;
	struct net_device *pnetdev;
	struct adapter *padapter = NULL;
	PSDIO_DATA psdio = &dvobj->intf_data;

	padapter = vzalloc(sizeof(*padapter));
	if (padapter == NULL) {
		goto exit;
	}

	padapter->dvobj = dvobj;
	dvobj->if1 = padapter;

	padapter->bDriverStopped =true;

	dvobj->padapters = padapter;
	padapter->iface_id = 0;

	/* 3 1. init network device data */
	pnetdev = rtw_init_netdev(padapter);
	if (!pnetdev)
		goto free_adapter;

	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(dvobj));

	padapter = rtw_netdev_priv(pnetdev);

	rtw_wdev_alloc(padapter, dvobj_to_dev(dvobj));

	/* 3 3. init driver special setting, interface, OS and hardware relative */

	/* 4 3.1 set hardware operation functions */
	rtw_set_hal_ops(padapter);


	/* 3 5. initialize Chip version */
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

	rtw_btcoex_Initialize(padapter);

	/* 3 6. read efuse/eeprom data */
	rtw_hal_read_chip_info(padapter);

	/* 3 7. init driver common data */
	if (rtw_init_drv_sw(padapter) == _FAIL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			 ("rtw_drv_init: Initialize driver software resource Failed!\n"));
		goto free_hal_data;
	}

	/* 3 8. get WLan MAC address */
	/*  set mac addr */
	rtw_macaddr_cfg(&psdio->func->dev, padapter->eeprompriv.mac_addr);

	rtw_hal_disable_interrupt(padapter);

	DBG_871X("bDriverStopped:%d, bSurpriseRemoved:%d, bup:%d, hw_init_completed:%d\n"
		, padapter->bDriverStopped
		, padapter->bSurpriseRemoved
		, padapter->bup
		, padapter->hw_init_completed
	);

	status = _SUCCESS;

free_hal_data:
	if (status != _SUCCESS && padapter->HalData)
		kfree(padapter->HalData);

	if (status != _SUCCESS) {
		rtw_wdev_unregister(padapter->rtw_wdev);
		rtw_wdev_free(padapter->rtw_wdev);
	}

free_adapter:
	if (status != _SUCCESS) {
		if (pnetdev)
			rtw_free_netdev(pnetdev);
		else
			vfree((u8 *)padapter);
		padapter = NULL;
	}
exit:
	return padapter;
}

static void rtw_sdio_if1_deinit(struct adapter *if1)
{
	struct net_device *pnetdev = if1->pnetdev;
	struct mlme_priv *pmlmepriv = &if1->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd(if1, 0, false);

	free_mlme_ap_info(if1);

#ifdef CONFIG_GPIO_WAKEUP
	gpio_hostwakeup_free_irq(if1);
#endif

	rtw_cancel_all_timer(if1);

#ifdef CONFIG_WOWLAN
	adapter_to_pwrctl(if1)->wowlan_mode =false;
	DBG_871X_LEVEL(_drv_always_, "%s wowlan_mode:%d\n", __func__, adapter_to_pwrctl(if1)->wowlan_mode);
#endif /* CONFIG_WOWLAN */

	rtw_dev_unload(if1);
	DBG_871X("+r871xu_dev_remove, hw_init_completed =%d\n", if1->hw_init_completed);

	if (if1->rtw_wdev) {
		rtw_wdev_free(if1->rtw_wdev);
	}

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
static int rtw_drv_init(
	struct sdio_func *func,
	const struct sdio_device_id *id)
{
	int status = _FAIL;
	struct adapter *if1 = NULL, *if2 = NULL;
	struct dvobj_priv *dvobj;

	dvobj = sdio_dvobj_init(func);
	if (dvobj == NULL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("initialize device object priv Failed!\n"));
		goto exit;
	}

	if1 = rtw_sdio_if1_init(dvobj, id);
	if (if1 == NULL) {
		DBG_871X("rtw_init_primarystruct adapter Failed!\n");
		goto free_dvobj;
	}

	/* dev_alloc_name && register_netdev */
	status = rtw_drv_register_netdev(if1);
	if (status != _SUCCESS) {
		goto free_if2;
	}

	if (sdio_alloc_irq(dvobj) != _SUCCESS)
		goto free_if2;

#ifdef	CONFIG_GPIO_WAKEUP
	gpio_hostwakeup_alloc_irq(if1);
#endif

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("-871x_drv - drv_init, success!\n"));

	rtw_ndev_notifier_register();
	status = _SUCCESS;

free_if2:
	if (status != _SUCCESS && if2) {
	}
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
	struct adapter *padapter = dvobj->if1;

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+rtw_dev_remove\n"));

	dvobj->processing_dev_remove = true;

	rtw_unregister_netdevs(dvobj);

	if (padapter->bSurpriseRemoved == false) {
		int err;

		/* test surprise remove */
		sdio_claim_host(func);
		sdio_readb(func, 0, &err);
		sdio_release_host(func);
		if (err == -ENOMEDIUM) {
			padapter->bSurpriseRemoved = true;
			DBG_871X(KERN_NOTICE "%s: device had been removed!\n", __func__);
		}
	}

	rtw_ps_deny(padapter, PS_DENY_DRV_REMOVE);

	rtw_pm_set_ips(padapter, IPS_NONE);
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	LeaveAllPowerSaveMode(padapter);

	rtw_btcoex_HaltNotify(padapter);

	rtw_sdio_if1_deinit(padapter);

	sdio_dvobj_deinit(func);

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("-rtw_dev_remove\n"));
}

extern int pm_netdev_open(struct net_device *pnetdev, u8 bnormal);
extern int pm_netdev_close(struct net_device *pnetdev, u8 bnormal);

static int rtw_sdio_suspend(struct device *dev)
{
	struct sdio_func *func =dev_to_sdio_func(dev);
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(psdpriv);
	struct adapter *padapter = psdpriv->if1;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	if (padapter->bDriverStopped == true)
	{
		DBG_871X("%s bDriverStopped = %d\n", __func__, padapter->bDriverStopped);
		return 0;
	}

	if (pwrpriv->bInSuspend == true)
	{
		DBG_871X("%s bInSuspend = %d\n", __func__, pwrpriv->bInSuspend);
		pdbgpriv->dbg_suspend_error_cnt++;
		return 0;
	}

	return rtw_suspend_common(padapter);
}

static int rtw_resume_process(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	if (pwrpriv->bInSuspend == false)
	{
		pdbgpriv->dbg_resume_error_cnt++;
		DBG_871X("%s bInSuspend = %d\n", __func__, pwrpriv->bInSuspend);
		return -1;
	}

	return rtw_resume_common(padapter);
}

static int rtw_sdio_resume(struct device *dev)
{
	struct sdio_func *func =dev_to_sdio_func(dev);
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(psdpriv);
	struct adapter *padapter = psdpriv->if1;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	int ret = 0;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	DBG_871X("==> %s (%s:%d)\n", __func__, current->comm, current->pid);

	pdbgpriv->dbg_resume_cnt++;

	if (pwrpriv->bInternalAutoSuspend)
	{
		ret = rtw_resume_process(padapter);
	}
	else
	{
		if (pwrpriv->wowlan_mode || pwrpriv->wowlan_ap_mode)
		{
			ret = rtw_resume_process(padapter);
		}
		else
		{
			ret = rtw_resume_process(padapter);
		}
	}
	pmlmeext->last_scan_time = jiffies;
	DBG_871X("<========  %s return %d\n", __func__, ret);
	return ret;

}

static int __init rtw_drv_entry(void)
{
	int ret = 0;

	DBG_871X_LEVEL(_drv_always_, "module init start\n");
	dump_drv_version(RTW_DBGDUMP);
#ifdef BTCOEXVERSION
	DBG_871X_LEVEL(_drv_always_, "rtl8723bs BT-Coex version = %s\n", BTCOEXVERSION);
#endif /*  BTCOEXVERSION */

	sdio_drvpriv.drv_registered = true;
	rtw_drv_proc_init();

	ret = sdio_register_driver(&sdio_drvpriv.r871xs_drv);
	if (ret != 0)
	{
		sdio_drvpriv.drv_registered = false;
		rtw_drv_proc_deinit();
		rtw_ndev_notifier_unregister();
		DBG_871X("%s: register driver failed!!(%d)\n", __func__, ret);
		goto exit;
	}

	goto exit;

exit:
	DBG_871X_LEVEL(_drv_always_, "module init ret =%d\n", ret);
	return ret;
}

static void __exit rtw_drv_halt(void)
{
	DBG_871X_LEVEL(_drv_always_, "module exit start\n");

	sdio_drvpriv.drv_registered = false;

	sdio_unregister_driver(&sdio_drvpriv.r871xs_drv);

	rtw_drv_proc_deinit();
	rtw_ndev_notifier_unregister();

	DBG_871X_LEVEL(_drv_always_, "module exit success\n");

	rtw_mstat_dump(RTW_DBGDUMP);
}


module_init(rtw_drv_entry);
module_exit(rtw_drv_halt);
