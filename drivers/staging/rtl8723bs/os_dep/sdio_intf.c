// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#include <drv_types.h>
#include <hal_btcoex.h>
#include <linux/jiffies.h>

#ifndef dev_to_sdio_func
#define dev_to_sdio_func(d)     container_of(d, struct sdio_func, dev)
#endif

static const struct sdio_device_id sdio_ids[] = {
	{ SDIO_DEVICE(0x024c, 0x0523), },
	{ SDIO_DEVICE(0x024c, 0x0525), },
	{ SDIO_DEVICE(0x024c, 0x0623), },
	{ SDIO_DEVICE(0x024c, 0x0626), },
	{ SDIO_DEVICE(0x024c, 0x0627), },
	{ SDIO_DEVICE(0x024c, 0xb723), },
	{ /* end: all zeroes */				},
};
MODULE_DEVICE_TABLE(sdio, sdio_ids);

static int rtw_drv_init(struct sdio_func *func, const struct sdio_device_id *id);
static void rtw_dev_remove(struct sdio_func *func);
static int rtw_sdio_resume(struct device *dev);
static int rtw_sdio_suspend(struct device *dev);

static const struct dev_pm_ops rtw_sdio_pm_ops = {
	.suspend	= rtw_sdio_suspend,
	.resume	= rtw_sdio_resume,
};

static struct sdio_driver rtl8723bs_sdio_driver = {
	.probe = rtw_drv_init,
	.remove = rtw_dev_remove,
	.name = "rtl8723bs",
	.id_table = sdio_ids,
	.drv = {
		.pm = &rtw_sdio_pm_ops,
	}
};

static void sd_sync_int_hdl(struct sdio_func *func)
{
	struct dvobj_priv *psdpriv;


	psdpriv = sdio_get_drvdata(func);

	if (!psdpriv->if1)
		return;

	rtw_sdio_set_irq_thd(psdpriv, current);
	sd_int_hdl(psdpriv->if1);
	rtw_sdio_set_irq_thd(psdpriv, NULL);
}

static int sdio_alloc_irq(struct dvobj_priv *dvobj)
{
	struct sdio_data *psdio_data;
	struct sdio_func *func;
	int err;

	psdio_data = &dvobj->intf_data;
	func = psdio_data->func;

	sdio_claim_host(func);

	err = sdio_claim_irq(func, &sd_sync_int_hdl);
	if (err) {
		dvobj->drv_dbg.dbg_sdio_alloc_irq_error_cnt++;
		printk(KERN_CRIT "%s: sdio_claim_irq FAIL(%d)!\n", __func__, err);
	} else {
		dvobj->drv_dbg.dbg_sdio_alloc_irq_cnt++;
		dvobj->irq_alloc = 1;
	}

	sdio_release_host(func);

	return err?_FAIL:_SUCCESS;
}

static void sdio_free_irq(struct dvobj_priv *dvobj)
{
	struct sdio_data *psdio_data;
	struct sdio_func *func;
	int err;

	if (dvobj->irq_alloc) {
		psdio_data = &dvobj->intf_data;
		func = psdio_data->func;

		if (func) {
			sdio_claim_host(func);
			err = sdio_release_irq(func);
			if (err) {
				dvobj->drv_dbg.dbg_sdio_free_irq_error_cnt++;
				netdev_err(dvobj->if1->pnetdev,
					   "%s: sdio_release_irq FAIL(%d)!\n",
					   __func__, err);
			} else
				dvobj->drv_dbg.dbg_sdio_free_irq_cnt++;
			sdio_release_host(func);
		}
		dvobj->irq_alloc = 0;
	}
}

static u32 sdio_init(struct dvobj_priv *dvobj)
{
	struct sdio_data *psdio_data;
	struct sdio_func *func;
	int err;

	psdio_data = &dvobj->intf_data;
	func = psdio_data->func;

	/* 3 1. init SDIO bus */
	sdio_claim_host(func);

	err = sdio_enable_func(func);
	if (err) {
		dvobj->drv_dbg.dbg_sdio_init_error_cnt++;
		goto release;
	}

	err = sdio_set_block_size(func, 512);
	if (err) {
		dvobj->drv_dbg.dbg_sdio_init_error_cnt++;
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

	func = dvobj->intf_data.func;

	if (func) {
		sdio_claim_host(func);
		err = sdio_disable_func(func);
		if (err)
			dvobj->drv_dbg.dbg_sdio_deinit_error_cnt++;

		if (dvobj->irq_alloc) {
			err = sdio_release_irq(func);
			if (err)
				dvobj->drv_dbg.dbg_sdio_free_irq_error_cnt++;
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
	struct sdio_data *psdio;

	dvobj = devobj_init();
	if (!dvobj)
		goto exit;

	sdio_set_drvdata(func, dvobj);

	psdio = &dvobj->intf_data;
	psdio->func = func;

	if (sdio_init(dvobj) != _SUCCESS)
		goto free_dvobj;

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
}

void rtw_set_hal_ops(struct adapter *padapter)
{
	/* alloc memory for HAL DATA */
	rtw_hal_data_init(padapter);

	rtl8723bs_set_hal_ops(padapter);
}

static void sd_intf_start(struct adapter *padapter)
{
	if (!padapter)
		return;

	/*  hal dep */
	rtw_hal_enable_interrupt(padapter);
}

static void sd_intf_stop(struct adapter *padapter)
{
	if (!padapter)
		return;

	/*  hal dep */
	rtw_hal_disable_interrupt(padapter);
}


static struct adapter *rtw_sdio_if1_init(struct dvobj_priv *dvobj, const struct sdio_device_id  *pdid)
{
	int status = _FAIL;
	struct net_device *pnetdev;
	struct adapter *padapter = NULL;
	struct sdio_data *psdio = &dvobj->intf_data;

	padapter = vzalloc(sizeof(*padapter));
	if (!padapter)
		goto exit;

	padapter->dvobj = dvobj;
	dvobj->if1 = padapter;

	padapter->bDriverStopped = true;

	dvobj->padapters = padapter;
	padapter->iface_id = 0;

	/* 3 1. init network device data */
	pnetdev = rtw_init_netdev(padapter);
	if (!pnetdev)
		goto free_adapter;

	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(dvobj));

	padapter = rtw_netdev_priv(pnetdev);

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
		goto free_hal_data;

	rtw_hal_read_chip_version(padapter);

	rtw_hal_chip_configure(padapter);

	hal_btcoex_Initialize((void *) padapter);

	/* 3 6. read efuse/eeprom data */
	rtw_hal_read_chip_info(padapter);

	/* 3 7. init driver common data */
	if (rtw_init_drv_sw(padapter) == _FAIL)
		goto free_hal_data;

	rtw_wdev_alloc(padapter, dvobj_to_dev(dvobj));

	/* 3 8. get WLan MAC address */
	/*  set mac addr */
	rtw_macaddr_cfg(&psdio->func->dev, padapter->eeprompriv.mac_addr);

	rtw_hal_disable_interrupt(padapter);

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

	rtw_cancel_all_timer(if1);

	rtw_dev_unload(if1);

	if (if1->rtw_wdev)
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
static int rtw_drv_init(
	struct sdio_func *func,
	const struct sdio_device_id *id)
{
	int status = _FAIL;
	struct adapter *if1 = NULL;
	struct dvobj_priv *dvobj;

	dvobj = sdio_dvobj_init(func);
	if (!dvobj)
		goto exit;

	if1 = rtw_sdio_if1_init(dvobj, id);
	if (!if1)
		goto free_dvobj;

	/* dev_alloc_name && register_netdev */
	status = rtw_drv_register_netdev(if1);
	if (status != _SUCCESS)
		goto free_if1;

	if (sdio_alloc_irq(dvobj) != _SUCCESS)
		goto free_if1;

	rtw_ndev_notifier_register();
	status = _SUCCESS;

free_if1:
	if (status != _SUCCESS && if1)
		rtw_sdio_if1_deinit(if1);

free_dvobj:
	if (status != _SUCCESS)
		sdio_dvobj_deinit(func);
exit:
	return status == _SUCCESS ? 0 : -ENODEV;
}

static void rtw_dev_remove(struct sdio_func *func)
{
	struct dvobj_priv *dvobj = sdio_get_drvdata(func);
	struct adapter *padapter = dvobj->if1;

	dvobj->processing_dev_remove = true;

	rtw_unregister_netdevs(dvobj);

	if (!padapter->bSurpriseRemoved) {
		int err;

		/* test surprise remove */
		sdio_claim_host(func);
		sdio_readb(func, 0, &err);
		sdio_release_host(func);
		if (err == -ENOMEDIUM)
			padapter->bSurpriseRemoved = true;
	}

	rtw_ps_deny(padapter, PS_DENY_DRV_REMOVE);

	rtw_pm_set_ips(padapter, IPS_NONE);
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	LeaveAllPowerSaveMode(padapter);

	rtw_btcoex_HaltNotify(padapter);

	rtw_sdio_if1_deinit(padapter);

	sdio_dvobj_deinit(func);
}

static int rtw_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(psdpriv);
	struct adapter *padapter = psdpriv->if1;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	if (padapter->bDriverStopped)
		return 0;

	if (pwrpriv->bInSuspend) {
		pdbgpriv->dbg_suspend_error_cnt++;
		return 0;
	}

	rtw_suspend_common(padapter);

	return 0;
}

static int rtw_resume_process(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	if (!pwrpriv->bInSuspend) {
		pdbgpriv->dbg_resume_error_cnt++;
		return -1;
	}

	return rtw_resume_common(padapter);
}

static int rtw_sdio_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	struct adapter *padapter = psdpriv->if1;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	int ret = 0;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	pdbgpriv->dbg_resume_cnt++;

	ret = rtw_resume_process(padapter);

	pmlmeext->last_scan_time = jiffies;
	return ret;
}

static int __init rtw_drv_entry(void)
{
	int ret;

	ret = sdio_register_driver(&rtl8723bs_sdio_driver);
	if (ret != 0)
		rtw_ndev_notifier_unregister();

	return ret;
}

static void __exit rtw_drv_halt(void)
{
	sdio_unregister_driver(&rtl8723bs_sdio_driver);

	rtw_ndev_notifier_unregister();
}


module_init(rtw_drv_entry);
module_exit(rtw_drv_halt);
