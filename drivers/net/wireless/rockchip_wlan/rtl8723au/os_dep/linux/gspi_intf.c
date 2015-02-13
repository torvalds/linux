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

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <rtw_version.h>

#ifndef CONFIG_GSPI_HCI
#error "CONFIG_GSPI_HCI should be on!\n"
#endif

#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
//#include <mach/ldo.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <mach/board.h>
#include <mach/hardware.h>
#include <mach/irqs.h>

#ifdef CONFIG_RTL8723A
#include <rtl8723a_hal.h>
#include <HalPwrSeqCmd.h>
#include <Hal8723PwrSeq.h>
#endif

#ifdef CONFIG_RTL8188E
#include <rtl8188e_hal.h>
#endif

#include <hal_intf.h>
#include <gspi_hal.h>
#include <gspi_ops.h>

#include <custom_gpio.h>


extern char* ifname;

typedef struct _driver_priv {
	int drv_registered;
} drv_priv, *pdrv_priv;

unsigned int oob_irq;
static drv_priv drvpriv = {

};

static void decide_chip_type_by_device_id(PADAPTER padapter)
{
	padapter->chip_type = NULL_CHIP_TYPE;

#if defined(CONFIG_RTL8723A)
	padapter->chip_type = RTL8723A;
			padapter->HardwareType = HARDWARE_TYPE_RTL8723AS;
#elif defined(CONFIG_RTL8188E)
			padapter->chip_type = RTL8188E;
			padapter->HardwareType = HARDWARE_TYPE_RTL8188ES;
#endif
}

static irqreturn_t spi_interrupt_thread(int irq, void *data)
{
	struct dvobj_priv *dvobj;
	PGSPI_DATA pgspi_data;


	dvobj = (struct dvobj_priv*)data;
	pgspi_data = &dvobj->intf_data;

	//spi_int_hdl(padapter);
	if (pgspi_data->priv_wq)
		queue_delayed_work(pgspi_data->priv_wq, &pgspi_data->irq_work, 0);

	return IRQ_HANDLED;
}

static u8 gspi_alloc_irq(struct dvobj_priv *dvobj)
{
	PGSPI_DATA pgspi_data;
	struct spi_device *spi;
	int err;


	pgspi_data = &dvobj->intf_data;
	spi = pgspi_data->func;

	err = request_irq(oob_irq, spi_interrupt_thread,
			IRQF_TRIGGER_FALLING,//IRQF_TRIGGER_HIGH;//|IRQF_ONESHOT,
		       	DRV_NAME, dvobj);
	//err = request_threaded_irq(oob_irq, NULL, spi_interrupt_thread,
	//		IRQF_TRIGGER_FALLING,
	//		DRV_NAME, dvobj);
	if (err < 0) {
		DBG_871X("Oops: can't allocate irq %d err:%d\n", oob_irq, err);
		goto exit;
	}
	enable_irq_wake(oob_irq);
	disable_irq(oob_irq);

exit:
	return err?_FAIL:_SUCCESS;
}

static u8 gspi_init(struct dvobj_priv *dvobj)
{
	PGSPI_DATA pgspi_data;
	int err = 0;

_func_enter_;

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+gspi_init\n"));

	if (NULL == dvobj) {
		DBG_8192C(KERN_ERR "%s: driver object is NULL!\n", __func__);
		err = -1;
		goto exit;
	}

	pgspi_data = &dvobj->intf_data;

	pgspi_data->block_transfer_len = 512;
	pgspi_data->tx_block_mode = 0;
	pgspi_data->rx_block_mode = 0;

exit:
	_func_exit_;

	if (err) return _FAIL;
	return _SUCCESS;
}

static void gspi_deinit(struct dvobj_priv *dvobj)
{
	PGSPI_DATA pgspi_data;
	struct spi_device *spi;
	int err;


	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+gspi_deinit\n"));

	if (NULL == dvobj) {
		DBG_8192C(KERN_ERR "%s: driver object is NULL!\n", __FUNCTION__);
		return;
	}

	pgspi_data = &dvobj->intf_data;
	spi = pgspi_data->func;

	if (spi) {
		free_irq(oob_irq, dvobj);
	}
}

static struct dvobj_priv *gspi_dvobj_init(struct spi_device *spi)
{
	int status = _FAIL;
	struct dvobj_priv *dvobj = NULL;
	PGSPI_DATA pgspi;

_func_enter_;

	dvobj = (struct dvobj_priv*)rtw_zmalloc(sizeof(*dvobj));
	if (NULL == dvobj) {
		goto exit;
	}

	_rtw_mutex_init(&dvobj->hw_init_mutex);
	_rtw_mutex_init(&dvobj->h2c_fwcmd_mutex);
	_rtw_mutex_init(&dvobj->setch_mutex);
	_rtw_mutex_init(&dvobj->setbw_mutex);
	dvobj->processing_dev_remove = _FALSE;
	_rtw_spinlock_init(&dvobj->lock);
	dvobj->macid[1] = _TRUE; //macid=1 for bc/mc stainfo
	_rtw_spinlock_init(&dvobj->cam_ctl.lock);
	//spi init
	/* This is the only SPI value that we need to set here, the rest
	 * comes from the board-peripherals file */
	spi->bits_per_word = 32;
	spi->max_speed_hz = 48 * 1000 * 1000;
	//here mode 0 and 3 all ok,
	//3 can run under 48M clock when SPI_CTL4 bit14 IS_FST set to 1
	//0 can run under 24M clock, but can run under 48M when SPI_CTL4 bit14 IS_FST set to 1 and Ctl0_reg[1:0] set to 3.
	spi->mode = SPI_MODE_3;
	spi_setup(spi);

#if 1
	//DBG_8192C("set spi ==========================%d \n", spi_setup(spi));

	DBG_871X("%s, mode = %d \n", __func__, spi->mode);
	DBG_871X("%s, bit_per_word = %d \n", __func__, spi->bits_per_word);
	DBG_871X("%s, speed = %d \n", __func__, spi->max_speed_hz);
	DBG_871X("%s, chip_select = %d \n", __func__, spi->chip_select);
	DBG_871X("%s, controller_data = %d \n", __func__, *(int *)spi->controller_data);
	DBG_871X("%s, irq= %d \n", __func__, oob_irq);
#endif

	spi_set_drvdata(spi, dvobj);
	pgspi = &dvobj->intf_data;
	pgspi->func = spi;

	if (gspi_init(dvobj) != _SUCCESS) {
		DBG_871X("%s: initialize GSPI Failed!\n", __FUNCTION__);
		goto free_dvobj;
	}
	rtw_reset_continual_io_error(dvobj);
	status = _SUCCESS;

free_dvobj:
	if (status != _SUCCESS && dvobj) {
		spi_set_drvdata(spi, NULL);
		_rtw_spinlock_free(&dvobj->lock);
		_rtw_mutex_free(&dvobj->hw_init_mutex);
		_rtw_mutex_free(&dvobj->h2c_fwcmd_mutex);
		_rtw_mutex_free(&dvobj->setch_mutex);
		_rtw_mutex_free(&dvobj->setbw_mutex);
		_rtw_spinlock_free(&dvobj->cam_ctl.lock);
		rtw_mfree((u8*)dvobj, sizeof(*dvobj));
		dvobj = NULL;
	}

exit:
_func_exit_;

	return dvobj;
}

static void gspi_dvobj_deinit(struct spi_device *spi)
{
	struct dvobj_priv *dvobj = spi_get_drvdata(spi);

_func_enter_;

	spi_set_drvdata(spi, NULL);
	if (dvobj) {
		gspi_deinit(dvobj);
		_rtw_spinlock_free(&dvobj->lock);
		_rtw_mutex_free(&dvobj->hw_init_mutex);
		_rtw_mutex_free(&dvobj->h2c_fwcmd_mutex);
		_rtw_mutex_free(&dvobj->setch_mutex);
		_rtw_mutex_free(&dvobj->setbw_mutex);
		_rtw_spinlock_free(&dvobj->cam_ctl.lock);
		rtw_mfree((u8*)dvobj, sizeof(*dvobj));
	}

_func_exit_;
}

static void spi_irq_work(void *data)
{
	struct delayed_work *dwork;
	PGSPI_DATA pgspi;
	struct dvobj_priv *dvobj;


	dwork = container_of(data, struct delayed_work, work);
	pgspi = container_of(dwork, GSPI_DATA, irq_work);

	dvobj = spi_get_drvdata(pgspi->func);
	if (!dvobj->if1) {
		DBG_871X("%s if1 == NULL !!\n", __FUNCTION__);
		return;
	}
	spi_int_hdl(dvobj->if1);
}

static void gspi_intf_start(PADAPTER padapter)
{
	PGSPI_DATA pgspi;


	if (padapter == NULL) {
		DBG_871X(KERN_ERR "%s: padapter is NULL!\n", __FUNCTION__);
		return;
	}

	pgspi = &adapter_to_dvobj(padapter)->intf_data;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
	pgspi->priv_wq = alloc_workqueue("spi_wq", 0, 0);
#else
	pgspi->priv_wq = create_workqueue("spi_wq");
#endif
	INIT_DELAYED_WORK(&pgspi->irq_work, (void*)spi_irq_work);

	enable_irq(oob_irq);
	//hal dep
	rtw_hal_enable_interrupt(padapter);
}

static void gspi_intf_stop(PADAPTER padapter)
{
	PGSPI_DATA pgspi;


	if (padapter == NULL) {
		DBG_871X(KERN_ERR "%s: padapter is NULL!\n", __FUNCTION__);
		return;
	}

	pgspi = &adapter_to_dvobj(padapter)->intf_data;

	if (pgspi->priv_wq) {
		cancel_delayed_work_sync(&pgspi->irq_work);
		flush_workqueue(pgspi->priv_wq);
		destroy_workqueue(pgspi->priv_wq);
		pgspi->priv_wq = NULL;
	}

	//hal dep
	rtw_hal_disable_interrupt(padapter);
	disable_irq(oob_irq);
}

/*
 * Do deinit job corresponding to netdev_open()
 */
void rtw_dev_unload(PADAPTER padapter)
{
	struct net_device *pnetdev = (struct net_device*)padapter->pnetdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+rtw_dev_unload\n"));

	padapter->bDriverStopped = _TRUE;
	#ifdef CONFIG_XMIT_ACK
	if (padapter->xmitpriv.ack_tx)
		rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
	#endif

	if (padapter->bup == _TRUE)
	{
#if 0
		if (padapter->intf_stop)
			padapter->intf_stop(padapter);
#else
		gspi_intf_stop(padapter);
#endif
		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("@ rtw_dev_unload: stop intf complete!\n"));

		if (!adapter_to_pwrctl(padapter)->bInternalAutoSuspend)
			rtw_stop_drv_threads(padapter);

		RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("@ rtw_dev_unload: stop thread complete!\n"));

		if (padapter->bSurpriseRemoved == _FALSE)
		{
#ifdef CONFIG_WOWLAN
			if (adapter_to_pwrctl(padapter)->bSupportRemoteWakeup == _TRUE) {
				DBG_871X("%s bSupportRemoteWakeup==_TRUE  do not run rtw_hal_deinit()\n",__FUNCTION__);
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

static PADAPTER rtw_gspi_if1_init(struct dvobj_priv *dvobj)
{
	int status = _FAIL;
	struct net_device *pnetdev;
	PADAPTER padapter = NULL;


	padapter = (PADAPTER)rtw_zvmalloc(sizeof(*padapter));
	if (NULL == padapter) {
		goto exit;
	}

	padapter->dvobj = dvobj;
	dvobj->if1 = padapter;

	padapter->bDriverStopped = _TRUE;

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

	padapter->interface_type = RTW_GSPI;
	decide_chip_type_by_device_id(padapter);

	//3 1. init network device data
	pnetdev = rtw_init_netdev(padapter);
	if (!pnetdev)
		goto free_adapter;

	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(dvobj));

#ifdef CONFIG_IOCTL_CFG80211
	rtw_wdev_alloc(padapter, dvobj_to_dev(dvobj));
#endif


	//3 3. init driver special setting, interface, OS and hardware relative
	//4 3.1 set hardware operation functions
	hal_set_hal_ops(padapter);


	//3 5. initialize Chip version
	padapter->intf_start = &gspi_intf_start;
	padapter->intf_stop = &gspi_intf_stop;

	if (rtw_init_io_priv(padapter, spi_set_intf_ops) == _FAIL)
	{
		RT_TRACE(_module_hci_intfs_c_, _drv_err_,
			("rtw_drv_init: Can't init io_priv\n"));
		goto free_hal_data;
	}

	{
		u32 ret = 0;
		DBG_8192C("read start:\n");
		//spi_write8_endian(padapter, SPI_LOCAL_OFFSET | 0xF0, 0x01, 1);
		rtw_write8(padapter, SPI_LOCAL_OFFSET | 0xF0, 0x03);
		ret = rtw_read32(padapter, SPI_LOCAL_OFFSET | 0xF0);
		DBG_8192C("read end 0xF0 read32:%x:\n", ret);
		DBG_8192C("read end 0xF0 read8:%x:\n", rtw_read8(padapter, SPI_LOCAL_OFFSET | 0xF0));

	}

	rtw_hal_read_chip_version(padapter);

	rtw_hal_chip_configure(padapter);


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
	if (status != _SUCCESS && padapter->HalData)
		rtw_mfree(padapter->HalData, sizeof(*(padapter->HalData)));

free_wdev:
	if (status != _SUCCESS) {
		#ifdef CONFIG_IOCTL_CFG80211
		rtw_wdev_unregister(padapter->rtw_wdev);
		rtw_wdev_free(padapter->rtw_wdev);
		#endif
	}

free_adapter:
	if (status != _SUCCESS) {
	if (pnetdev)
		rtw_free_netdev(pnetdev);
		else if (padapter)
			rtw_vmfree((u8*)padapter, sizeof(*padapter));
		padapter = NULL;
	}

exit:
	return padapter;
}

static void rtw_gspi_if1_deinit(PADAPTER if1)
{
	struct net_device *pnetdev = if1->pnetdev;
	struct mlme_priv *pmlmepriv = &if1->mlmepriv;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd(if1, 0, _FALSE);

#ifdef CONFIG_AP_MODE
	free_mlme_ap_info(if1);
	#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_unload(if1);
#endif
#endif

	rtw_cancel_all_timer(if1);

	rtw_dev_unload(if1);
	DBG_871X("+r871xu_dev_remove, hw_init_completed=%d\n", if1->hw_init_completed);

	rtw_handle_dualmac(if1, 0);

#ifdef CONFIG_IOCTL_CFG80211
	if (if1->rtw_wdev)
	{
		//rtw_wdev_unregister(if1->rtw_wdev);
		rtw_wdev_free(if1->rtw_wdev);
	}
#endif

	rtw_free_drv_sw(if1);

	if(pnetdev)
		rtw_free_netdev(pnetdev);
}

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located a card for us to support.
 *        We accept the new device by returning 0.
 */
static int /*__devinit*/  rtw_drv_probe(struct spi_device *spi)
{
	int status = _FAIL;
	struct dvobj_priv *dvobj;
	struct net_device *pnetdev;
	PADAPTER if1 = NULL, if2 = NULL;


	DBG_8192C("RTW: %s line:%d", __FUNCTION__, __LINE__);

	if ((dvobj = gspi_dvobj_init(spi)) == NULL) {
		DBG_871X("%s: Initialize device object priv Failed!\n", __FUNCTION__);
		goto exit;
	}

	if ((if1 = rtw_gspi_if1_init(dvobj)) == NULL) {
		DBG_871X("rtw_init_primary_adapter Failed!\n");
		goto free_dvobj;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if ((if2 = rtw_drv_if2_init(if1, NULL, spi_set_intf_ops)) == NULL) {
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

	if (gspi_alloc_irq(dvobj) != _SUCCESS)
		goto free_if2;

#ifdef CONFIG_GLOBAL_UI_PID
	if(ui_pid[1]!=0) {
		DBG_871X("ui_pid[1]:%d\n",ui_pid[1]);
		rtw_signal_process(ui_pid[1], SIGUSR2);
		}
#endif

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-871x_drv - drv_init, success!\n"));

	status = _SUCCESS;

free_if2:
	if (status != _SUCCESS && if2) {
		#ifdef CONFIG_CONCURRENT_MODE
		rtw_drv_if2_stop(if2);
		rtw_drv_if2_free(if2);
		#endif
	}

free_if1:
	if (status != _SUCCESS && if1) {
		rtw_gspi_if1_deinit(if1);
	}

free_dvobj:
	if (status != _SUCCESS)
		gspi_dvobj_deinit(spi);

exit:
	return status == _SUCCESS?0:-ENODEV;
}
extern void rtw_unregister_netdevs(struct dvobj_priv *dvobj);
static int /*__devexit*/  rtw_dev_remove(struct spi_device *spi)
{
	struct dvobj_priv *dvobj = spi_get_drvdata(spi);
	PADAPTER padapter = dvobj->if1;

_func_enter_;

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("+rtw_dev_remove\n"));

	dvobj->processing_dev_remove = _TRUE;
	rtw_unregister_netdevs(dvobj);	

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(dvobj_to_pwrctl(dvobj));
#endif

	rtw_pm_set_ips(padapter, IPS_NONE);
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	LeaveAllPowerSaveMode(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_if2_stop(dvobj->if2);
#endif

	rtw_gspi_if1_deinit(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_if2_free(dvobj->if2);
#endif

	gspi_dvobj_deinit(spi);

	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("-rtw_dev_remove\n"));

_func_exit_;

	return 0;
}

static int rtw_gspi_suspend(struct spi_device *spi, pm_message_t mesg)
{
	struct dvobj_priv *dvobj = spi_get_drvdata(spi);
	PADAPTER padapter = dvobj->if1;
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct net_device *pnetdev = padapter->pnetdev;
	int ret = 0;

	u32 start_time = rtw_get_current_time();

	_func_enter_;

	DBG_871X("==> %s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);

	pwrpriv->bInSuspend = _TRUE;

	while (pwrpriv->bips_processing == _TRUE)
		rtw_msleep_os(1);

	if((!padapter->bup) || (padapter->bDriverStopped)||(padapter->bSurpriseRemoved))
	{
		DBG_871X("%s bup=%d bDriverStopped=%d bSurpriseRemoved = %d\n", __FUNCTION__
			,padapter->bup, padapter->bDriverStopped,padapter->bSurpriseRemoved);
		goto exit;
	}

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
	pwrpriv->bSupportRemoteWakeup=_TRUE;
#else
	//s2.
	rtw_disassoc_cmd(padapter, 0, _FALSE);
#endif

#ifdef CONFIG_LAYER2_ROAMING_RESUME
	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED) )
	{
		DBG_871X("%s %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n",__FUNCTION__,
				pmlmepriv->cur_network.network.Ssid.Ssid,
				MAC_ARG(pmlmepriv->cur_network.network.MacAddress),
				pmlmepriv->cur_network.network.Ssid.SsidLength,
				pmlmepriv->assoc_ssid.SsidLength);

		rtw_set_roaming(padapter, 1);
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
	gspi_deinit(dvobj);
	RT_TRACE(_module_hci_intfs_c_, _drv_notice_, ("%s: deinit GSPI complete!\n", __FUNCTION__));

	rtw_wifi_gpio_wlan_ctrl(WLAN_PWDN_OFF);
	rtw_mdelay_os(1);
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

	rtw_wifi_gpio_wlan_ctrl(WLAN_PWDN_ON);
	rtw_mdelay_os(1);

	{
		u32 ret = 0;
		DBG_8192C("read start:\n");
		//spi_write8_endian(padapter, SPI_LOCAL_OFFSET | 0xF0, 0x01, 1);
		rtw_write8(padapter, SPI_LOCAL_OFFSET | 0xF0, 0x03);
		ret = rtw_read32(padapter, SPI_LOCAL_OFFSET | 0xF0);
		DBG_8192C("read end 0xF0 read32:%x:\n", ret);
		DBG_8192C("read end 0xF0 read8:%x:\n", rtw_read8(padapter, SPI_LOCAL_OFFSET | 0xF0));

	}

	if (padapter) {
		pnetdev = padapter->pnetdev;
		pwrpriv = adapter_to_pwrctl(padapter);
	} else {
		ret = -1;
		goto exit;
	}

	// interface init
	if (gspi_init(adapter_to_dvobj(padapter)) != _SUCCESS)
	{
		ret = -1;
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: initialize SDIO Failed!!\n", __FUNCTION__));
		goto exit;
	}
	rtw_hal_disable_interrupt(padapter);
	if (gspi_alloc_irq(adapter_to_dvobj(padapter)) != _SUCCESS)
	{
		ret = -1;
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("%s: gspi_alloc_irq Failed!!\n", __FUNCTION__));
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
	pwrpriv->bInSuspend = _FALSE;
	DBG_871X("<===  %s return %d.............. in %dms\n", __FUNCTION__
		, ret, rtw_get_passing_time_ms(start_time));

	_func_exit_;

	return ret;
}

static int rtw_gspi_resume(struct spi_device *spi)
{
	struct dvobj_priv *dvobj = spi_get_drvdata(spi);
	PADAPTER padapter = dvobj->if1;
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
	 int ret = 0;


	DBG_871X("==> %s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);

	if(pwrpriv->bInternalAutoSuspend ){
 		ret = rtw_resume_process(padapter);
	} else {
#ifdef CONFIG_RESUME_IN_WORKQUEUE
		rtw_resume_in_workqueue(pwrpriv);
#else
		if(rtw_is_earlysuspend_registered(pwrpriv)) {
			/* jeff: bypass resume here, do in late_resume */
			rtw_set_do_late_resume(pwrpriv, _TRUE);
		} else {
			ret = rtw_resume_process(padapter);
		}
#endif /* CONFIG_RESUME_IN_WORKQUEUE */
	}

	DBG_871X("<========  %s return %d\n", __FUNCTION__, ret);
	return ret;

}


static struct spi_driver rtw_spi_drv = {
	.probe = rtw_drv_probe,
	.remove = rtw_dev_remove,
	.suspend = rtw_gspi_suspend,
	.resume = rtw_gspi_resume,
	.driver = {
		.name = "wlan_spi",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
	}

};

static int __init rtw_drv_entry(void)
{
	int ret = 0;

	DBG_871X_LEVEL(_drv_always_, "module init start\n");
	dump_drv_version(RTW_DBGDUMP);
#ifdef BTCOEXVERSION
	DBG_871X_LEVEL(_drv_always_, DRV_NAME" BT-Coex version = %s\n", BTCOEXVERSION);
#endif // BTCOEXVERSION

	drvpriv.drv_registered = _TRUE;
	rtw_suspend_lock_init();
	rtw_drv_proc_init();
	rtw_ndev_notifier_register();

	rtw_wifi_gpio_init();
	rtw_wifi_gpio_wlan_ctrl(WLAN_PWDN_ON);

	ret = spi_register_driver(&rtw_spi_drv);
	if (ret != 0) {
		drvpriv.drv_registered = _FALSE;
		rtw_suspend_lock_uninit();
		rtw_drv_proc_deinit();
		rtw_ndev_notifier_unregister();

		rtw_wifi_gpio_wlan_ctrl(WLAN_PWDN_OFF);
		rtw_wifi_gpio_deinit();

		goto exit;
	}

exit:
	DBG_871X_LEVEL(_drv_always_, "module init ret=%d\n", ret);
	return ret;
}

static void __exit rtw_drv_halt(void)
{
	DBG_871X_LEVEL(_drv_always_, "module exit start\n");

	drvpriv.drv_registered = _FALSE;

	spi_unregister_driver(&rtw_spi_drv);

	rtw_wifi_gpio_wlan_ctrl(WLAN_PWDN_OFF);
	rtw_wifi_gpio_deinit();

	rtw_suspend_lock_uninit();
	rtw_drv_proc_deinit();
	rtw_ndev_notifier_unregister();

	DBG_871X_LEVEL(_drv_always_, "module exit success\n");

	rtw_mstat_dump(RTW_DBGDUMP);
}
module_init(rtw_drv_entry);
module_exit(rtw_drv_halt);

