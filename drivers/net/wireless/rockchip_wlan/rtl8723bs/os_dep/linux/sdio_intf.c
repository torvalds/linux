/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#define _HCI_INTF_C_

#include <drv_types.h>
#include <hal_data.h>
#include <platform_ops.h>

#ifndef CONFIG_SDIO_HCI
#error "CONFIG_SDIO_HCI shall be on!\n"
#endif

#ifdef CONFIG_RTL8822B
#include <rtl8822b_hal.h>	/* rtl8822bs_set_hal_ops() */
#endif /* CONFIG_RTL8822B */

#ifdef CONFIG_PLATFORM_INTEL_BYT
#ifdef CONFIG_ACPI
#include <linux/acpi.h>
#include <linux/acpi_gpio.h>
#include "rtw_android.h"
#endif
static int wlan_en_gpio = -1;
#endif /* CONFIG_PLATFORM_INTEL_BYT */

#ifndef dev_to_sdio_func
#define dev_to_sdio_func(d)     container_of(d, struct sdio_func, dev)
#endif

#ifdef CONFIG_WOWLAN
static struct mmc_host *mmc_host = NULL;
#endif

static const struct sdio_device_id sdio_ids[] = {
#ifdef CONFIG_RTL8723B
	{ SDIO_DEVICE(0x024c, 0xB723), .driver_data = RTL8723B},
#endif
#ifdef CONFIG_RTL8188E
	{ SDIO_DEVICE(0x024c, 0x8179), .driver_data = RTL8188E},
#endif /* CONFIG_RTL8188E */

#ifdef CONFIG_RTL8821A
	{ SDIO_DEVICE(0x024c, 0x8821), .driver_data = RTL8821},
#endif /* CONFIG_RTL8821A */

#ifdef CONFIG_RTL8192E
	{ SDIO_DEVICE(0x024c, 0x818B), .driver_data = RTL8192E},
#endif /* CONFIG_RTL8192E */

#ifdef CONFIG_RTL8703B
	{ SDIO_DEVICE(0x024c, 0xB703), .driver_data = RTL8703B},
#endif

#ifdef CONFIG_RTL8188F
	{SDIO_DEVICE(0x024c, 0xF179), .driver_data = RTL8188F},
#endif
#ifdef CONFIG_RTL8822B
	{SDIO_DEVICE(0x024c, 0xB822), .driver_data = RTL8822B},
#endif

#ifdef CONFIG_RTL8723D
	{ SDIO_DEVICE(0x024c, 0xD723), .driver_data = RTL8723D},
	{ SDIO_DEVICE(0x024c, 0xD724), .driver_data = RTL8723D},
#endif

#ifdef CONFIG_RTL8821C
	{SDIO_DEVICE(0x024C, 0xB821), .driver_data = RTL8821C},
	{SDIO_DEVICE(0x024C, 0xC821), .driver_data = RTL8821C},
#endif

#if defined(RTW_ENABLE_WIFI_CONTROL_FUNC) /* temporarily add this to accept all sdio wlan id */
	{ SDIO_DEVICE_CLASS(SDIO_CLASS_WLAN) },
#endif
	{ /* end: all zeroes */				},
};

MODULE_DEVICE_TABLE(sdio, sdio_ids);

static int rtw_drv_init(struct sdio_func *func, const struct sdio_device_id *id);
static void rtw_dev_remove(struct sdio_func *func);
static int rtw_sdio_resume(struct device *dev);
static int rtw_sdio_suspend(struct device *dev);
extern void rtw_dev_unload(PADAPTER padapter);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
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
	.r871xs_drv.name = (char *)DRV_NAME,
	.r871xs_drv.id_table = sdio_ids,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 29))
	.r871xs_drv.drv = {
		.pm = &rtw_sdio_pm_ops,
	}
#endif
};

static struct rtw_if_operations sdio_ops = {
	.read		= rtw_sdio_raw_read,
	.write		= rtw_sdio_raw_write,
};

static void sd_sync_int_hdl(struct sdio_func *func)
{
	struct dvobj_priv *psdpriv;

	psdpriv = sdio_get_drvdata(func);

	if (!dvobj_get_primary_adapter(psdpriv)) {
		RTW_INFO("%s primary adapter == NULL\n", __func__);
		return;
	}

	rtw_sdio_set_irq_thd(psdpriv, current);
	sd_int_hdl(dvobj_get_primary_adapter(psdpriv));
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
	if (err) {
		dvobj->drv_dbg.dbg_sdio_alloc_irq_error_cnt++;
		RTW_PRINT("%s: sdio_claim_irq FAIL(%d)!\n", __func__, err);
	} else {
		dvobj->drv_dbg.dbg_sdio_alloc_irq_cnt++;
		dvobj->irq_alloc = 1;
	}

	sdio_release_host(func);

	return err ? _FAIL : _SUCCESS;
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
			if (err) {
				dvobj->drv_dbg.dbg_sdio_free_irq_error_cnt++;
				RTW_ERR("%s: sdio_release_irq FAIL(%d)!\n", __func__, err);
			} else
				dvobj->drv_dbg.dbg_sdio_free_irq_cnt++;
			sdio_release_host(func);
		}
		dvobj->irq_alloc = 0;
	}
}

#ifdef CONFIG_GPIO_WAKEUP
extern unsigned int oob_irq;
extern unsigned int oob_gpio;
static irqreturn_t gpio_hostwakeup_irq_thread(int irq, void *data)
{
	PADAPTER padapter = (PADAPTER)data;
	RTW_PRINT("gpio_hostwakeup_irq_thread\n");
	/* Disable interrupt before calling handler */
	/* disable_irq_nosync(oob_irq); */
#ifdef CONFIG_PLATFORM_ARM_SUN6I
	return 0;
#else
	return IRQ_HANDLED;
#endif
}

static u8 gpio_hostwakeup_alloc_irq(PADAPTER padapter)
{
	int err;
	u32 status = 0;

	if (oob_irq == 0) {
		RTW_INFO("oob_irq ZERO!\n");
		return _FAIL;
	}

	RTW_INFO("%s : oob_irq = %d\n", __func__, oob_irq);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32))
	status = IRQF_NO_SUSPEND;
#endif

	if (HIGH_ACTIVE)
		status |= IRQF_TRIGGER_RISING;
	else
		status |= IRQF_TRIGGER_FALLING;

	err = request_threaded_irq(oob_irq, gpio_hostwakeup_irq_thread, NULL,
		status, "rtw_wifi_gpio_wakeup", padapter);

	if (err < 0) {
		RTW_INFO("Oops: can't allocate gpio irq %d err:%d\n", oob_irq, err);
		return _FALSE;
	} else
		RTW_INFO("allocate gpio irq %d ok\n", oob_irq);

#ifndef CONFIG_PLATFORM_ARM_SUN8I
	enable_irq_wake(oob_irq);
#endif
	return _SUCCESS;
}

static void gpio_hostwakeup_free_irq(PADAPTER padapter)
{
	wifi_free_gpio(oob_gpio);

	if (oob_irq == 0)
		return;

#ifndef CONFIG_PLATFORM_ARM_SUN8I
	disable_irq_wake(oob_irq);
#endif
	free_irq(oob_irq, padapter);
}
#endif

void dump_sdio_card_info(void *sel, struct dvobj_priv *dvobj)
{
	PSDIO_DATA psdio_data = &dvobj->intf_data;

	RTW_PRINT_SEL(sel, "== SDIO Card Info ==\n");
	RTW_PRINT_SEL(sel, "  clock: %d Hz\n", psdio_data->clock);

	RTW_PRINT_SEL(sel, "  timing spec :");
	switch (psdio_data->timing) {
	case MMC_TIMING_LEGACY:
		_RTW_PRINT_SEL(sel, "legacy");
		break;
	case MMC_TIMING_MMC_HS:
		_RTW_PRINT_SEL(sel, "mmc high-speed");
		break;
	case MMC_TIMING_SD_HS:
		_RTW_PRINT_SEL(sel, "sd high-speed");
		break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
	case MMC_TIMING_UHS_SDR12:
		_RTW_PRINT_SEL(sel, "sd uhs SDR12");
		break;
	case MMC_TIMING_UHS_SDR25:
		_RTW_PRINT_SEL(sel, "sd uhs SDR25");
		break;
	#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0) */

	case MMC_TIMING_UHS_SDR50:
		_RTW_PRINT_SEL(sel, "sd uhs SDR50");
		break;

	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	case MMC_TIMING_MMC_DDR52:
		_RTW_PRINT_SEL(sel, "mmc DDR52");
		break;
	#endif

	case MMC_TIMING_UHS_SDR104:
		_RTW_PRINT_SEL(sel, "sd uhs SDR104");
		break;
	case MMC_TIMING_UHS_DDR50:
		_RTW_PRINT_SEL(sel, "sd uhs DDR50");
		break;

	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	case MMC_TIMING_MMC_HS200:
		_RTW_PRINT_SEL(sel, "mmc HS200");
		break;
	#endif

	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 16, 0)
	case MMC_TIMING_MMC_HS400:
		_RTW_PRINT_SEL(sel, "mmc HS400");
		break;
	#endif
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0) */
	default:
		_RTW_PRINT_SEL(sel, "unknown(%d)", psdio_data->timing);
		break;
	}
	_RTW_PRINT_SEL(sel, "\n");

	RTW_PRINT_SEL(sel, "  sd3_bus_mode: %s\n", (psdio_data->sd3_bus_mode) ? "TRUE" : "FALSE");
	RTW_PRINT_SEL(sel, "================\n");
}

#define SDIO_CARD_INFO_DUMP(dvobj)	dump_sdio_card_info(RTW_DBGDUMP, dvobj)

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
		RTW_PRINT("%s: sdio_enable_func FAIL(%d)!\n", __func__, err);
		goto release;
	}

	err = sdio_set_block_size(func, 512);
	if (err) {
		dvobj->drv_dbg.dbg_sdio_init_error_cnt++;
		RTW_PRINT("%s: sdio_set_block_size FAIL(%d)!\n", __func__, err);
		goto release;
	}
	psdio_data->block_transfer_len = 512;
	psdio_data->tx_block_mode = 1;
	psdio_data->rx_block_mode = 1;

	psdio_data->timing = func->card->host->ios.timing;
	psdio_data->clock = func->card->host->ios.clock;

	psdio_data->sd3_bus_mode = _FALSE;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 0, 0)
	if (psdio_data->timing <= MMC_TIMING_UHS_DDR50
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
		&& psdio_data->timing >= MMC_TIMING_UHS_SDR12
		#else
		&& psdio_data->timing >= MMC_TIMING_UHS_SDR50
		#endif
	)
		psdio_data->sd3_bus_mode = _TRUE;
#endif
	SDIO_CARD_INFO_DUMP(dvobj);


release:
	sdio_release_host(func);

exit:

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
		if (err) {
			dvobj->drv_dbg.dbg_sdio_deinit_error_cnt++;
			RTW_ERR("%s: sdio_disable_func(%d)\n", __func__, err);
		}

		sdio_release_host(func);
	}
}

static void rtw_decide_chip_type_by_device_id(struct dvobj_priv *dvobj, const struct sdio_device_id  *pdid)
{
	dvobj->chip_type = pdid->driver_data;

#if defined(CONFIG_RTL8188E)
	if (dvobj->chip_type == RTL8188E) {
		dvobj->HardwareType = HARDWARE_TYPE_RTL8188ES;
		RTW_INFO("CHIP TYPE: RTL8188E\n");
	}
#endif

#if defined(CONFIG_RTL8723B)
	dvobj->chip_type = RTL8723B;
	dvobj->HardwareType = HARDWARE_TYPE_RTL8723BS;
#endif

#if defined(CONFIG_RTL8821A)
	if (dvobj->chip_type == RTL8821) {
		dvobj->HardwareType = HARDWARE_TYPE_RTL8821S;
		RTW_INFO("CHIP TYPE: RTL8821A\n");
	}
#endif

#if defined(CONFIG_RTL8192E)
	if (dvobj->chip_type == RTL8192E) {
		dvobj->HardwareType = HARDWARE_TYPE_RTL8192ES;
		RTW_INFO("CHIP TYPE: RTL8192E\n");
	}
#endif

#if defined(CONFIG_RTL8703B)
	if (dvobj->chip_type == RTL8703B) {
		dvobj->HardwareType = HARDWARE_TYPE_RTL8703BS;
		RTW_INFO("CHIP TYPE: RTL8703B\n");
	}
#endif

#if defined(CONFIG_RTL8723D)
	if (dvobj->chip_type == RTL8723D) {
		dvobj->HardwareType = HARDWARE_TYPE_RTL8723DS;
		RTW_INFO("CHIP TYPE: RTL8723D\n");
	}
#endif

#if defined(CONFIG_RTL8188F)
	if (dvobj->chip_type == RTL8188F) {
		dvobj->HardwareType = HARDWARE_TYPE_RTL8188FS;
		RTW_INFO("CHIP TYPE: RTL8188F\n");
	}
#endif

#if defined(CONFIG_RTL8822B)
	if (dvobj->chip_type == RTL8822B) {
		dvobj->HardwareType = HARDWARE_TYPE_RTL8822BS;
		RTW_INFO("CHIP TYPE: RTL8822B\n");
	}
#endif

#if defined(CONFIG_RTL8821C)
	if (dvobj->chip_type == RTL8821C) {
		dvobj->HardwareType = HARDWARE_TYPE_RTL8821CS;
		RTW_INFO("CHIP TYPE: RTL8821C\n");
	}
#endif
}

static struct dvobj_priv *sdio_dvobj_init(struct sdio_func *func, const struct sdio_device_id  *pdid)
{
	int status = _FAIL;
	struct dvobj_priv *dvobj = NULL;
	PSDIO_DATA psdio;

	dvobj = devobj_init();
	if (dvobj == NULL)
		goto exit;
	dvobj->intf_ops = &sdio_ops;

	sdio_set_drvdata(func, dvobj);

	psdio = &dvobj->intf_data;
	psdio->func = func;

	if (sdio_init(dvobj) != _SUCCESS) {
		goto free_dvobj;
	}

	dvobj->interface_type = RTW_SDIO;
	rtw_decide_chip_type_by_device_id(dvobj, pdid);

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
		sdio_free_irq(dvobj);
		devobj_deinit(dvobj);
	}

	return;
}

u8 rtw_set_hal_ops(PADAPTER padapter)
{
	/* alloc memory for HAL DATA */
	if (rtw_hal_data_init(padapter) == _FAIL)
		return _FAIL;

#if defined(CONFIG_RTL8188E)
	if (rtw_get_chip_type(padapter) == RTL8188E)
		rtl8188es_set_hal_ops(padapter);
#endif

#if defined(CONFIG_RTL8723B)
	if (rtw_get_chip_type(padapter) == RTL8723B)
		rtl8723bs_set_hal_ops(padapter);
#endif

#if defined(CONFIG_RTL8821A)
	if (rtw_get_chip_type(padapter) == RTL8821)
		rtl8821as_set_hal_ops(padapter);
#endif

#if defined(CONFIG_RTL8192E)
	if (rtw_get_chip_type(padapter) == RTL8192E)
		rtl8192es_set_hal_ops(padapter);
#endif

#if defined(CONFIG_RTL8703B)
	if (rtw_get_chip_type(padapter) == RTL8703B)
		rtl8703bs_set_hal_ops(padapter);
#endif

#if defined(CONFIG_RTL8723D)
	if (rtw_get_chip_type(padapter) == RTL8723D)
		rtl8723ds_set_hal_ops(padapter);
#endif

#if defined(CONFIG_RTL8188F)
	if (rtw_get_chip_type(padapter) == RTL8188F)
		rtl8188fs_set_hal_ops(padapter);
#endif

#if defined(CONFIG_RTL8822B)
	if (rtw_get_chip_type(padapter) == RTL8822B)
		rtl8822bs_set_hal_ops(padapter);
#endif

#if defined(CONFIG_RTL8821C)
	if (rtw_get_chip_type(padapter) == RTL8821C) {
		if (rtl8821cs_set_hal_ops(padapter) == _FAIL)
			return _FAIL;
	}
#endif

	if (rtw_hal_ops_check(padapter) == _FAIL)
		return _FAIL;

	if (hal_spec_init(padapter) == _FAIL)
		return _FAIL;

	return _SUCCESS;
}

static void sd_intf_start(PADAPTER padapter)
{
	if (padapter == NULL) {
		RTW_ERR("%s: padapter is NULL!\n", __func__);
		return;
	}

	/* hal dep */
	rtw_hal_enable_interrupt(padapter);
}

static void sd_intf_stop(PADAPTER padapter)
{
	if (padapter == NULL) {
		RTW_ERR("%s: padapter is NULL!\n", __func__);
		return;
	}

	/* hal dep */
	rtw_hal_disable_interrupt(padapter);
}


#ifdef RTW_SUPPORT_PLATFORM_SHUTDOWN
PADAPTER g_test_adapter = NULL;
#endif /* RTW_SUPPORT_PLATFORM_SHUTDOWN */

_adapter *rtw_sdio_primary_adapter_init(struct dvobj_priv *dvobj)
{
	int status = _FAIL;
	PADAPTER padapter = NULL;

	padapter = (_adapter *)rtw_zvmalloc(sizeof(*padapter));
	if (padapter == NULL)
		goto exit;

	if (loadparam(padapter) != _SUCCESS)
		goto free_adapter;

#ifdef RTW_SUPPORT_PLATFORM_SHUTDOWN
	g_test_adapter = padapter;
#endif /* RTW_SUPPORT_PLATFORM_SHUTDOWN */
	padapter->dvobj = dvobj;

	rtw_set_drv_stopped(padapter);/*init*/

	dvobj->padapters[dvobj->iface_nums++] = padapter;
	padapter->iface_id = IFACE_ID0;

	/* set adapter_type/iface type for primary padapter */
	padapter->isprimary = _TRUE;
	padapter->adapter_type = PRIMARY_ADAPTER;
#ifdef CONFIG_MI_WITH_MBSSID_CAM
	padapter->hw_port = HW_PORT0;
#else
	padapter->hw_port = HW_PORT0;
#endif

	/* 3 3. init driver special setting, interface, OS and hardware relative */

	/* 4 3.1 set hardware operation functions */
	if (rtw_set_hal_ops(padapter) == _FAIL)
		goto free_hal_data;

	/* 3 5. initialize Chip version */
	padapter->intf_start = &sd_intf_start;
	padapter->intf_stop = &sd_intf_stop;

	padapter->intf_init = &sdio_init;
	padapter->intf_deinit = &sdio_deinit;
	padapter->intf_alloc_irq = &sdio_alloc_irq;
	padapter->intf_free_irq = &sdio_free_irq;

	if (rtw_init_io_priv(padapter, sdio_set_intf_ops) == _FAIL) {
		goto free_hal_data;
	}

	rtw_hal_read_chip_version(padapter);

	rtw_hal_chip_configure(padapter);

	/* 3 6. read efuse/eeprom data */
	if (rtw_hal_read_chip_info(padapter) == _FAIL)
		goto free_hal_data;

	/* 3 7. init driver common data */
	if (rtw_init_drv_sw(padapter) == _FAIL) {
		goto free_hal_data;
	}

#ifdef CONFIG_BT_COEXIST
	if (GET_HAL_DATA(padapter)->EEPROMBluetoothCoexist)
		rtw_btcoex_Initialize(padapter);
	else
		rtw_btcoex_wifionly_initialize(padapter);
#else /* !CONFIG_BT_COEXIST */
	rtw_btcoex_wifionly_initialize(padapter);
#endif /* CONFIG_BT_COEXIST */

	/* 3 8. get WLan MAC address */
	/* set mac addr */
	rtw_macaddr_cfg(adapter_mac_addr(padapter),  get_hal_mac_addr(padapter));

#ifdef CONFIG_MI_WITH_MBSSID_CAM
	rtw_mbid_camid_alloc(padapter, adapter_mac_addr(padapter));
#endif
#ifdef CONFIG_P2P
	rtw_init_wifidirect_addrs(padapter, adapter_mac_addr(padapter), adapter_mac_addr(padapter));
#endif /* CONFIG_P2P */

	rtw_hal_disable_interrupt(padapter);

	RTW_INFO("bDriverStopped:%s, bSurpriseRemoved:%s, bup:%d, hw_init_completed:%d\n"
		, rtw_is_drv_stopped(padapter) ? "True" : "False"
		, rtw_is_surprise_removed(padapter) ? "True" : "False"
		, padapter->bup
		, rtw_get_hw_init_completed(padapter)
	);

	status = _SUCCESS;

free_hal_data:
	if (status != _SUCCESS && padapter->HalData)
		rtw_hal_free_data(padapter);

free_adapter:
	if (status != _SUCCESS && padapter) {
		rtw_vmfree((u8 *)padapter, sizeof(*padapter));
		padapter = NULL;
	}
exit:
	return padapter;
}

static void rtw_sdio_primary_adapter_deinit(_adapter *padapter)
{
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd(padapter, 0, RTW_CMDF_DIRECTLY);

#ifdef CONFIG_AP_MODE
	if (MLME_IS_AP(padapter) || MLME_IS_MESH(padapter)) {
		free_mlme_ap_info(padapter);
		#ifdef CONFIG_HOSTAPD_MLME
		hostapd_mode_unload(padapter);
		#endif
	}
#endif

#ifdef CONFIG_GPIO_WAKEUP
#ifdef CONFIG_PLATFORM_ARM_SUN6I
	sw_gpio_eint_set_enable(gpio_eint_wlan, 0);
	sw_gpio_irq_free(eint_wlan_handle);
#else
	gpio_hostwakeup_free_irq(padapter);
#endif
#endif

	/*rtw_cancel_all_timer(if1);*/

#ifdef CONFIG_WOWLAN
	adapter_to_pwrctl(padapter)->wowlan_mode = _FALSE;
	RTW_PRINT("%s wowlan_mode:%d\n", __func__, adapter_to_pwrctl(padapter)->wowlan_mode);
#endif /* CONFIG_WOWLAN */

	rtw_dev_unload(padapter);
	RTW_INFO("+r871xu_dev_remove, hw_init_completed=%d\n", rtw_get_hw_init_completed(padapter));

	rtw_free_drv_sw(padapter);

	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
	rtw_os_ndev_free(padapter);

#ifdef RTW_HALMAC
	rtw_halmac_deinit_adapter(adapter_to_dvobj(padapter));
#endif /* RTW_HALMAC */

	rtw_vmfree((u8 *)padapter, sizeof(_adapter));

#ifdef CONFIG_PLATFORM_RTD2880B
	RTW_INFO("wlan link down\n");
	rtd2885_wlan_netlink_sendMsg("linkdown", "8712");
#endif

#ifdef RTW_SUPPORT_PLATFORM_SHUTDOWN
	g_test_adapter = NULL;
#endif /* RTW_SUPPORT_PLATFORM_SHUTDOWN */
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
#ifdef CONFIG_CONCURRENT_MODE
	int i;
#endif
	struct net_device *pnetdev;
	PADAPTER padapter = NULL;
	struct dvobj_priv *dvobj;

#ifdef CONFIG_PLATFORM_INTEL_BYT

#ifdef CONFIG_ACPI
	acpi_handle handle;
	struct acpi_device *adev;
#endif

#if defined(CONFIG_ACPI) && defined(CONFIG_GPIO_WAKEUP)
	handle = ACPI_HANDLE(&func->dev);

	if (handle) {
		/* Dont try to do acpi pm for the wifi module */
		if (!handle || acpi_bus_get_device(handle, &adev))
			RTW_INFO("Could not get acpi pointer!\n");
		else {
			adev->flags.power_manageable = 0;
			RTW_INFO("Disabling ACPI power management support!\n");
		}
		oob_gpio = acpi_get_gpio_by_index(&func->dev, 0, NULL);
		RTW_INFO("rtw_drv_init: ACPI_HANDLE found oob_gpio %d!\n", oob_gpio);
		wifi_configure_gpio();
	} else
		RTW_INFO("rtw_drv_init: ACPI_HANDLE NOT found!\n");
#endif

#if defined(CONFIG_ACPI)
	if (&func->dev && ACPI_HANDLE(&func->dev)) {
		wlan_en_gpio = acpi_get_gpio_by_index(&func->dev, 1, NULL);
		RTW_INFO("rtw_drv_init: ACPI_HANDLE found wlan_en %d!\n", wlan_en_gpio);
	} else
		RTW_INFO("rtw_drv_init: ACPI_HANDLE NOT found!\n");
#endif
#endif /* CONFIG_PLATFORM_INTEL_BYT */



	dvobj = sdio_dvobj_init(func, id);
	if (dvobj == NULL) {
		goto exit;
	}

	padapter = rtw_sdio_primary_adapter_init(dvobj);
	if (padapter == NULL) {
		RTW_INFO("rtw_init_primary_adapter Failed!\n");
		goto free_dvobj;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->registrypriv.virtual_iface_num > (CONFIG_IFACE_NUMBER - 1))
		padapter->registrypriv.virtual_iface_num = (CONFIG_IFACE_NUMBER - 1);

	for (i = 0; i < padapter->registrypriv.virtual_iface_num; i++) {
		if (rtw_drv_add_vir_if(padapter, sdio_set_intf_ops) == NULL) {
			RTW_INFO("rtw_drv_add_iface failed! (%d)\n", i);
			goto free_if_vir;
		}
	}
#endif

	/* dev_alloc_name && register_netdev */
	if (rtw_os_ndevs_init(dvobj) != _SUCCESS)
		goto free_if_vir;

#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_init(padapter);
#endif

#ifdef CONFIG_PLATFORM_RTD2880B
	RTW_INFO("wlan link up\n");
	rtd2885_wlan_netlink_sendMsg("linkup", "8712");
#endif

	if (sdio_alloc_irq(dvobj) != _SUCCESS)
		goto os_ndevs_deinit;

#ifdef CONFIG_GPIO_WAKEUP
#ifdef CONFIG_PLATFORM_ARM_SUN6I
	eint_wlan_handle = sw_gpio_irq_request(gpio_eint_wlan, TRIG_EDGE_NEGATIVE, (peint_handle)gpio_hostwakeup_irq_thread, NULL);
	if (!eint_wlan_handle) {
		RTW_INFO("%s: request irq failed\n", __func__);
		return -1;
	}
#else
	gpio_hostwakeup_alloc_irq(padapter);
#endif
#endif

#ifdef CONFIG_GLOBAL_UI_PID
	if (ui_pid[1] != 0) {
		RTW_INFO("ui_pid[1]:%d\n", ui_pid[1]);
		rtw_signal_process(ui_pid[1], SIGUSR2);
	}
#endif


	status = _SUCCESS;

os_ndevs_deinit:
	if (status != _SUCCESS)
		rtw_os_ndevs_deinit(dvobj);
free_if_vir:
	if (status != _SUCCESS) {
		#ifdef CONFIG_CONCURRENT_MODE
		rtw_drv_stop_vir_ifaces(dvobj);
		rtw_drv_free_vir_ifaces(dvobj);
		#endif
	}

	if (status != _SUCCESS && padapter)
		rtw_sdio_primary_adapter_deinit(padapter);

free_dvobj:
	if (status != _SUCCESS)
		sdio_dvobj_deinit(func);
exit:
	return status == _SUCCESS ? 0 : -ENODEV;
}

static void rtw_dev_remove(struct sdio_func *func)
{
	struct dvobj_priv *dvobj = sdio_get_drvdata(func);
	struct pwrctrl_priv *pwrctl = dvobj_to_pwrctl(dvobj);
	PADAPTER padapter = dvobj_get_primary_adapter(dvobj);



	dvobj->processing_dev_remove = _TRUE;

	/* TODO: use rtw_os_ndevs_deinit instead at the first stage of driver's dev deinit function */
	rtw_os_ndevs_unregister(dvobj);

	if (!rtw_is_surprise_removed(padapter)) {
		int err;

		/* test surprise remove */
		sdio_claim_host(func);
		sdio_readb(func, 0, &err);
		sdio_release_host(func);
		if (err == -ENOMEDIUM) {
			rtw_set_surprise_removed(padapter);
			RTW_INFO("%s: device had been removed!\n", __func__);
		}
	}

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(pwrctl);
#endif

	if (GET_HAL_DATA(padapter)->bFWReady == _TRUE) {
		rtw_ps_deny(padapter, PS_DENY_DRV_REMOVE);
		rtw_pm_set_ips(padapter, IPS_NONE);
		rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);
		LeaveAllPowerSaveMode(padapter);
	}
	rtw_set_drv_stopped(padapter);	/*for stop thread*/
	rtw_stop_cmd_thread(padapter);
#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_stop_vir_ifaces(dvobj);
#endif

#ifdef CONFIG_BT_COEXIST
#ifdef CONFIG_BT_COEXIST_SOCKET_TRX
	if (GET_HAL_DATA(padapter)->EEPROMBluetoothCoexist)
		rtw_btcoex_close_socket(padapter);
#endif
	rtw_btcoex_HaltNotify(padapter);
#endif

	rtw_sdio_primary_adapter_deinit(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_free_vir_ifaces(dvobj);
#endif

	sdio_dvobj_deinit(func);


}
extern int pm_netdev_open(struct net_device *pnetdev, u8 bnormal);
extern int pm_netdev_close(struct net_device *pnetdev, u8 bnormal);

static int rtw_sdio_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	struct pwrctrl_priv *pwrpriv = NULL;
	_adapter *padapter = NULL;
	struct debug_priv *pdbgpriv = NULL;
	int ret = 0;
	u8 ch, bw, offset;

	if (psdpriv == NULL)
		goto exit;

	pwrpriv = dvobj_to_pwrctl(psdpriv);
	padapter = dvobj_get_primary_adapter(psdpriv);
	pdbgpriv = &psdpriv->drv_dbg;
	if (rtw_is_drv_stopped(padapter)) {
		RTW_INFO("%s bDriverStopped == _TRUE\n", __func__);
		goto exit;
	}

	if (pwrpriv->bInSuspend == _TRUE) {
		RTW_INFO("%s bInSuspend = %d\n", __func__, pwrpriv->bInSuspend);
		pdbgpriv->dbg_suspend_error_cnt++;
		goto exit;
	}

	ret = rtw_suspend_common(padapter);

exit:
#ifdef CONFIG_RTW_SDIO_PM_KEEP_POWER
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 34))
	/* Android 4.0 don't support WIFI close power */
	/* or power down or clock will close after wifi resume, */
	/* this is sprd's bug in Android 4.0, but sprd don't */
	/* want to fix it. */
	/* we have test power under 8723as, power consumption is ok */
	if (func) {
		mmc_pm_flag_t pm_flag = 0;
		pm_flag = sdio_get_host_pm_caps(func);
		RTW_INFO("cmd: %s: suspend: PM flag = 0x%x\n", sdio_func_id(func), pm_flag);
		if (!(pm_flag & MMC_PM_KEEP_POWER)) {
			RTW_INFO("%s: cannot remain alive while host is suspended\n", sdio_func_id(func));
			if (pdbgpriv)
				pdbgpriv->dbg_suspend_error_cnt++;
			return -ENOSYS;
		} else {
			RTW_INFO("cmd: suspend with MMC_PM_KEEP_POWER\n");
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

	if (pwrpriv->bInSuspend == _FALSE) {
		pdbgpriv->dbg_resume_error_cnt++;
		RTW_INFO("%s bInSuspend = %d\n", __FUNCTION__, pwrpriv->bInSuspend);
		return -1;
	}

	return rtw_resume_common(padapter);
}

static int rtw_sdio_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct dvobj_priv *psdpriv = sdio_get_drvdata(func);
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(psdpriv);
	_adapter *padapter = dvobj_get_primary_adapter(psdpriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	int ret = 0;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;

	RTW_INFO("==> %s (%s:%d)\n", __FUNCTION__, current->comm, current->pid);

	pdbgpriv->dbg_resume_cnt++;

	if (pwrpriv->bInternalAutoSuspend)
		ret = rtw_resume_process(padapter);
	else {
#ifdef CONFIG_PLATFORM_INTEL_BYT
		if (0)
#else
		if (pwrpriv->wowlan_mode || pwrpriv->wowlan_ap_mode)
#endif
		{
			rtw_resume_lock_suspend();
			ret = rtw_resume_process(padapter);
			rtw_resume_unlock_suspend();
		} else {
#ifdef CONFIG_RESUME_IN_WORKQUEUE
			rtw_resume_in_workqueue(pwrpriv);
#else
			if (rtw_is_earlysuspend_registered(pwrpriv)) {
				/* jeff: bypass resume here, do in late_resume */
				rtw_set_do_late_resume(pwrpriv, _TRUE);
			} else {
				rtw_resume_lock_suspend();
				ret = rtw_resume_process(padapter);
				rtw_resume_unlock_suspend();
			}
#endif
		}
	}
	pmlmeext->last_scan_time = rtw_get_current_time();
	RTW_INFO("<========  %s return %d\n", __FUNCTION__, ret);
	return ret;

}

static int rtw_drv_entry(void)
{
	int ret = 0;

	RTW_PRINT("module init start\n");
	dump_drv_version(RTW_DBGDUMP);
#ifdef BTCOEXVERSION
	RTW_PRINT(DRV_NAME" BT-Coex version = %s\n", BTCOEXVERSION);
#endif /* BTCOEXVERSION */

#ifndef CONFIG_PLATFORM_INTEL_BYT
	rtw_android_wifictrl_func_add();
#endif /* !CONFIG_PLATFORM_INTEL_BYT */

	ret = platform_wifi_power_on();
	if (ret) {
		RTW_INFO("%s: power on failed!!(%d)\n", __FUNCTION__, ret);
		ret = -1;
		goto exit;
	}

	sdio_drvpriv.drv_registered = _TRUE;
	rtw_suspend_lock_init();
	rtw_drv_proc_init();
	rtw_ndev_notifier_register();
	rtw_inetaddr_notifier_register();

	ret = sdio_register_driver(&sdio_drvpriv.r871xs_drv);
	if (ret != 0) {
		sdio_drvpriv.drv_registered = _FALSE;
		rtw_suspend_lock_uninit();
		rtw_drv_proc_deinit();
		rtw_ndev_notifier_unregister();
		rtw_inetaddr_notifier_unregister();
		RTW_INFO("%s: register driver failed!!(%d)\n", __FUNCTION__, ret);
		goto poweroff;
	}

	goto exit;

poweroff:
	platform_wifi_power_off();

exit:
	RTW_PRINT("module init ret=%d\n", ret);
	return ret;
}

static void rtw_drv_halt(void)
{
	RTW_PRINT("module exit start\n");

	sdio_drvpriv.drv_registered = _FALSE;

	sdio_unregister_driver(&sdio_drvpriv.r871xs_drv);

	rtw_android_wifictrl_func_del();

	platform_wifi_power_off();

	rtw_suspend_lock_uninit();
	rtw_drv_proc_deinit();
	rtw_ndev_notifier_unregister();
	rtw_inetaddr_notifier_unregister();

	RTW_PRINT("module exit success\n");

	rtw_mstat_dump(RTW_DBGDUMP);
}

#ifdef CONFIG_PLATFORM_INTEL_BYT
int rtw_sdio_set_power(int on)
{

	if (wlan_en_gpio >= 0) {
		if (on)
			gpio_set_value(wlan_en_gpio, 1);
		else
			gpio_set_value(wlan_en_gpio, 0);
	}

	return 0;
}
#endif /* CONFIG_PLATFORM_INTEL_BYT */

#include <linux/rfkill-wlan.h>
extern int get_wifi_chip_type(void);
extern int rockchip_wifi_power(int on);
extern int rockchip_wifi_set_carddetect(int val);

int rockchip_wifi_init_module_rtkwifi(void)
{
#ifdef CONFIG_WIFI_LOAD_DRIVER_WHEN_KERNEL_BOOTUP
    int type = get_wifi_chip_type();
    if (type < WIFI_AP6XXX_SERIES || type == WIFI_ESP8089) return 0;
#endif
    printk("\n");
    printk("=======================================================\n");
    printk("==== Launching Wi-Fi driver! (Powered by Rockchip) ====\n");
    printk("=======================================================\n");
    printk("Realtek 8723BS SDIO WiFi driver (Powered by Rockchip,Ver %s) init.\n", DRIVERVERSION);

    rockchip_wifi_power(1);
    rockchip_wifi_set_carddetect(1);

    return rtw_drv_entry();
}

void rockchip_wifi_exit_module_rtkwifi(void)
{
#ifdef CONFIG_WIFI_LOAD_DRIVER_WHEN_KERNEL_BOOTUP
    int type = get_wifi_chip_type();
    if (type < WIFI_AP6XXX_SERIES || type == WIFI_ESP8089) return;
#endif
    printk("\n");
    printk("=======================================================\n");
    printk("==== Dislaunching Wi-Fi driver! (Powered by Rockchip) ====\n");
    printk("=======================================================\n");
    printk("Realtek 8723BS SDIO WiFi driver (Powered by Rockchip,Ver %s) init.\n", DRIVERVERSION);

    rtw_drv_halt();

    rockchip_wifi_set_carddetect(0);
    rockchip_wifi_power(0);
}
#ifdef CONFIG_WIFI_BUILD_MODULE
module_init(rockchip_wifi_init_module_rtkwifi);
module_exit(rockchip_wifi_exit_module_rtkwifi);
#else
#ifdef CONFIG_WIFI_LOAD_DRIVER_WHEN_KERNEL_BOOTUP
late_initcall(rockchip_wifi_init_module_rtkwifi);
module_exit(rockchip_wifi_exit_module_rtkwifi);
#else
EXPORT_SYMBOL(rockchip_wifi_init_module_rtkwifi);
EXPORT_SYMBOL(rockchip_wifi_exit_module_rtkwifi);
#endif
#endif
//module_init(rtw_drv_entry);
//module_exit(rtw_drv_halt);

