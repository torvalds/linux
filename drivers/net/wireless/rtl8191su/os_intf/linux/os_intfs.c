/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#define _OS_INTFS_C_

#include <drv_conf.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif
 

#include <linux/module.h>
#include <linux/init.h>

#include <osdep_service.h>
#include <drv_types.h>
#include <xmit_osdep.h>
#include <recv_osdep.h>
#include <hal_init.h>
#include <rtl871x_ioctl.h>

#ifdef CONFIG_SDIO_HCI
#include <sdio_osintf.h>
#include <linux/mmc/sdio_func.h> 
#include <linux/mmc/sdio_ids.h>
#endif

#ifdef CONFIG_USB_HCI
#include <usb_osintf.h>
#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rtl871x wireless lan driver");
MODULE_AUTHOR("...");

/* module param defaults */
int chip_version =RTL8712_2ndCUT;
int rfintfs = HWPI;
int lbkmode = RTL8712_AIR_TRX;
#ifdef CONFIG_SDIO_HCI
int hci = RTL8712_SDIO;
#endif
#ifdef CONFIG_USB_HCI
int hci = RTL8712_USB;
#endif

// Added by Albert 2010/02/23
// The video_mode variable is for vedio mode.
// It may be specify when inserting module with video_mode=1 parameter.
int video_mode = 1;   // enable video mode

int network_mode = Ndis802_11IBSS;//Ndis802_11Infrastructure;//infra, ad-hoc, auto	  
//NDIS_802_11_SSID	ssid;
int channel = 1;//ad-hoc support requirement 
int wireless_mode = WIRELESS_11BG;
int vrtl_carrier_sense = AUTO_VCS;
int vcs_type = RTS_CTS;//*
int rts_thresh = 2347;//*
int frag_thresh = 2346;//*
int preamble = PREAMBLE_LONG;//long, short, auto
int scan_mode = 1;//active, passive
int adhoc_tx_pwr = 1;
int soft_ap = 0;
int smart_ps = 1;  
int power_mgnt = PS_MODE_ACTIVE;
int radio_enable = 1;
int long_retry_lmt = 7;
int short_retry_lmt = 7;
int busy_thresh = 40;
//int qos_enable = 0; //*
int ack_policy = NORMAL_ACK;
int mp_mode = 0;	
int software_encrypt = 0;
int software_decrypt = 0;	  
 
int wmm_enable = 1;// default is set to enable the wmm.
int uapsd_enable = 0;	  
int uapsd_max_sp = NO_LIMIT;
int uapsd_acbk_en = 0;
int uapsd_acbe_en = 0;
int uapsd_acvi_en = 0;
int uapsd_acvo_en = 0;
	
#ifdef CONFIG_80211N_HT
int ht_enable = 1;
int cbw40_enable = 1;
int ampdu_enable = 1;//for enable tx_ampdu
#endif
int rf_config = RTL8712_RF_1T2R;  // 1T2R	
int low_power = 0;
char* initmac = 0;  // temp mac address if users want to use instead of the mac address in Efuse
int wifi_test = 0;    // if wifi_test = 1, driver had to disable the turbo mode and pass it to firmware private.
u8* g_pallocated_recv_buf = NULL;

module_param(initmac, charp, 0644);
module_param(wifi_test, int, 0644);
module_param(video_mode, int, 0644);
module_param(chip_version, int, 0644);
module_param(rfintfs, int, 0644);
module_param(lbkmode, int, 0644);
module_param(hci, int, 0644);
module_param(network_mode, int, 0644);
module_param(channel, int, 0644);
module_param(mp_mode, int, 0644);
module_param(wmm_enable, int, 0644);
module_param(vrtl_carrier_sense, int, 0644);
module_param(vcs_type, int, 0644);
module_param(busy_thresh, int, 0644);
#ifdef CONFIG_80211N_HT
module_param(ht_enable, int, 0644);
module_param(cbw40_enable, int, 0644);
module_param(ampdu_enable, int, 0644);
#endif
module_param(rf_config, int, 0644);
module_param(power_mgnt, int, 0644);
module_param(low_power, int, 0644);
#ifdef CONFIG_R871X_TEST
int start_pseudo_adhoc(_adapter *padapter);
int stop_pseudo_adhoc(_adapter *padapter);
#endif

extern void r871x_dev_unload(_adapter *padapter);

u32 start_drv_threads(_adapter *padapter);
void stop_drv_threads (_adapter *padapter);
u8 init_drv_sw(_adapter *padapter);
u8 free_drv_sw(_adapter *padapter);

struct net_device *init_netdev(void);

static uint loadparam( _adapter *padapter,  _nic_hdl	pnetdev);
static int netdev_open (struct net_device *pnetdev);
static int netdev_close (struct net_device *pnetdev);

uint loadparam( _adapter *padapter,  _nic_hdl	pnetdev)
{
       
	uint status = _SUCCESS;
	struct registry_priv  *registry_par = &padapter->registrypriv;

_func_enter_;

	registry_par->chip_version = (u8)chip_version;
	registry_par->rfintfs = (u8)rfintfs;
	registry_par->lbkmode = (u8)lbkmode;	
	registry_par->hci = (u8)hci;
	registry_par->network_mode  = (u8)network_mode;	

     	_memcpy(registry_par->ssid.Ssid, "ANY", 3);
	registry_par->ssid.SsidLength = 3;
	
	registry_par->channel = (u8)channel;
	registry_par->wireless_mode = (u8)wireless_mode;
	registry_par->vrtl_carrier_sense = (u8)vrtl_carrier_sense ;
	registry_par->vcs_type = (u8)vcs_type;
	registry_par->frag_thresh=(u16)frag_thresh;
	registry_par->preamble = (u8)preamble;
	registry_par->scan_mode = (u8)scan_mode;
	registry_par->adhoc_tx_pwr = (u8)adhoc_tx_pwr;
	registry_par->soft_ap=  (u8)soft_ap;
	registry_par->smart_ps =  (u8)smart_ps;  
	registry_par->power_mgnt = (u8)power_mgnt;
	registry_par->radio_enable = (u8)radio_enable;
	registry_par->long_retry_lmt = (u8)long_retry_lmt;
	registry_par->short_retry_lmt = (u8)short_retry_lmt;
  	registry_par->busy_thresh = (u16)busy_thresh;
  	//registry_par->qos_enable = (u8)qos_enable;
    	registry_par->ack_policy = (u8)ack_policy;
	registry_par->mp_mode = (u8)mp_mode;	
	registry_par->software_encrypt = (u8)software_encrypt;
	registry_par->software_decrypt = (u8)software_decrypt;	  

	 //UAPSD
	registry_par->wmm_enable = (u8)wmm_enable;
	registry_par->uapsd_enable = (u8)uapsd_enable;	  
	registry_par->uapsd_max_sp = (u8)uapsd_max_sp;
	registry_par->uapsd_acbk_en = (u8)uapsd_acbk_en;
	registry_par->uapsd_acbe_en = (u8)uapsd_acbe_en;
	registry_par->uapsd_acvi_en = (u8)uapsd_acvi_en;
	registry_par->uapsd_acvo_en = (u8)uapsd_acvo_en;

#ifdef CONFIG_80211N_HT
	registry_par->ht_enable = (u8)ht_enable;
	registry_par->cbw40_enable = (u8)cbw40_enable;
	registry_par->ampdu_enable = (u8)ampdu_enable;
#endif

	registry_par->rf_config = (u8)rf_config;
	registry_par->low_power = (u8)low_power;
	registry_par->wifi_test = (u8) wifi_test;

_func_exit_;

	return status;
}

static int r871x_net_set_mac_address(struct net_device *pnetdev, void *p)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct sockaddr *addr = p;
	
	if(padapter->bup == _FALSE)
	{
		//printk("r8711_net_set_mac_address(), MAC=%x:%x:%x:%x:%x:%x\n", addr->sa_data[0], addr->sa_data[1], addr->sa_data[2], addr->sa_data[3],
		//addr->sa_data[4], addr->sa_data[5]);
		//_memcpy(padapter->eeprompriv.mac_addr, addr->sa_data, ETH_ALEN);
		_memcpy(pnetdev->dev_addr, addr->sa_data, ETH_ALEN);
		//padapter->bset_hwaddr = _TRUE;
	}

	return 0;
}

static struct net_device_stats *r871x_net_get_stats(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct recv_priv *precvpriv = &(padapter->recvpriv);

	padapter->stats.tx_packets = pxmitpriv->tx_pkts;//pxmitpriv->tx_pkts++;
	padapter->stats.rx_packets = precvpriv->rx_pkts;//precvpriv->rx_pkts++; 		
	padapter->stats.tx_dropped = pxmitpriv->tx_drop;
	padapter->stats.rx_dropped = precvpriv->rx_drop;
	padapter->stats.tx_bytes = pxmitpriv->tx_bytes;
	padapter->stats.rx_bytes = precvpriv->rx_bytes;
	
	return &padapter->stats;	
}

#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))
static const struct net_device_ops rtl8712_netdev_ops = {
	.ndo_open = netdev_open,
        .ndo_stop = netdev_close,
        .ndo_start_xmit = xmit_entry,
        .ndo_set_mac_address = r871x_net_set_mac_address,
        .ndo_get_stats = r871x_net_get_stats,
#ifdef CONFIG_IOCTL_CFG80211
	.ndo_do_ioctl = rtw_cfg80211_do_ioctl,
#else //CONFIG_IOCTL_CFG80211
        .ndo_do_ioctl = r871x_ioctl,
#endif //CONFIG_IOCTL_CFG80211
};
#endif

struct net_device *init_netdev(void)	
{
	_adapter *padapter;
	struct net_device *pnetdev;

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+init_net_dev\n"));

	//pnetdev = alloc_netdev(sizeof(_adapter), "wlan%d", ether_setup);
	pnetdev = rtw_alloc_etherdev(sizeof(_adapter));	
	if (!pnetdev)
	   return NULL;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
	SET_MODULE_OWNER(pnetdev);
#endif

	//ether_setup(pnetdev); already called in alloc_etherdev() -> alloc_netdev().
	
	padapter = rtw_netdev_priv(pnetdev);
	padapter->pnetdev = pnetdev;	
	
	//pnetdev->init = NULL;
#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,29))

	printk("register rtl8712_netdev_ops to netdev_ops\n");
	pnetdev->netdev_ops = &rtl8712_netdev_ops;

#else
	pnetdev->open = netdev_open;
	pnetdev->stop = netdev_close;	
	
	pnetdev->hard_start_xmit = xmit_entry;

	pnetdev->set_mac_address = r871x_net_set_mac_address;
	pnetdev->get_stats = r871x_net_get_stats;

#ifdef CONFIG_IOCTL_CFG80211
	pnetdev->do_ioctl = rtw_cfg80211_do_ioctl;
#else  //CONFIG_IOCTL_CFG80211
	pnetdev->do_ioctl = r871x_ioctl;
#endif //CONFIG_IOCTL_CFG80211

#endif


#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_TX
	pnetdev->features |= NETIF_F_IP_CSUM;
#endif	
	//pnetdev->tx_timeout = NULL;
	pnetdev->watchdog_timeo = HZ; /* 1 second timeout */	
	
	pnetdev->wireless_handlers = (struct iw_handler_def *)&r871x_handlers_def;  
	
#ifdef WIRELESS_SPY
	//priv->wireless_data.spy_data = &priv->spy_data;
	//pnetdev->wireless_data = &priv->wireless_data;
#endif

#ifdef CONFIG_PLATFORM_MT53XX
	if(dev_alloc_name(pnetdev,"rea%d") < 0)
#else
	if(dev_alloc_name(pnetdev,"wlan%d") < 0)
#endif
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("dev_alloc_name, fail! \n"));
	}

	//step 2.
   	loadparam(padapter, pnetdev);	   

	netif_carrier_off(pnetdev);
	//netif_stop_queue(pnetdev);

	padapter->pid = 0;  // Initial the PID value used for HW PBC.
	
	return pnetdev;

}

u32 start_drv_threads(_adapter *padapter)
{
    u32 _status = _SUCCESS;

    RT_TRACE(_module_os_intfs_c_,_drv_info_,("+start_drv_threads\n"));

#ifdef CONFIG_SDIO_HCI
    padapter->xmitThread = kernel_thread(xmit_thread, padapter, CLONE_FS|CLONE_FILES);
    if(padapter->xmitThread < 0)
		_status = _FAIL;
#endif

#ifdef CONFIG_RECV_THREAD_MODE
    padapter->recvThread = kernel_thread(recv_thread, padapter, CLONE_FS|CLONE_FILES);
    if(padapter->recvThread < 0)
		_status = _FAIL;	
#endif

    padapter->cmdThread = kernel_thread(cmd_thread, padapter, CLONE_FS|CLONE_FILES);
    if(padapter->cmdThread < 0)
		_status = _FAIL;		

#ifdef CONFIG_EVENT_THREAD_MODE
    padapter->evtThread = kernel_thread(event_thread, padapter, CLONE_FS|CLONE_FILES);
    if(padapter->evtThread < 0)
		_status = _FAIL;		
#endif
  
    return _status;
}

void stop_drv_threads (_adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+stop_drv_threads\n"));	

	//Below is to termindate cmd_thread & event_thread...
	_up_sema(&padapter->cmdpriv.cmd_queue_sema);
	//_up_sema(&padapter->cmdpriv.cmd_done_sema);
	if(padapter->cmdThread){
	_down_sema(&padapter->cmdpriv.terminate_cmdthread_sema);
	}
	padapter->cmdpriv.cmd_seq = 1;

#ifdef CONFIG_EVENT_THREAD_MODE
        _up_sema(&padapter->evtpriv.evt_notify);
	if(padapter->evtThread){
	_down_sema(&padapter->evtpriv.terminate_evtthread_sema);
	}
#endif

#ifdef CONFIG_SDIO_HCI
	// Below is to termindate tx_thread...
	_up_sema(&padapter->xmitpriv.xmit_sema);	
	_down_sema(&padapter->xmitpriv.terminate_xmitthread_sema);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("\n drv_halt: xmit_thread can be terminated ! \n"));
#endif
	 
#ifdef CONFIG_RECV_THREAD_MODE	
	// Below is to termindate rx_thread...
	_up_sema(&padapter->recvpriv.recv_sema);
	_down_sema(&padapter->recvpriv.terminate_recvthread_sema);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("\n drv_halt:recv_thread can be terminated! \n"));
#endif
}

void start_drv_timers (_adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+start_drv_timers\n"));

	_set_timer(&padapter->mlmepriv.sitesurveyctrl.sitesurvey_ctrl_timer, 5000);

	_set_timer(&padapter->mlmepriv.wdg_timer, 2000);
}

void stop_drv_timers (_adapter *padapter)
{
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+stop_drv_timers\n"));

	_cancel_timer_ex(&padapter->mlmepriv.assoc_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("stop_drv_timers:cancel association timer complete! \n"));

	_cancel_timer_ex(&padapter->mlmepriv.sitesurveyctrl.sitesurvey_ctrl_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("stop_drv_timers:cancel sitesurvey_ctrl_timer! \n"));

	_cancel_timer_ex(&padapter->securitypriv.tkip_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("stop_drv_timers:cancel tkip_timer! \n"));

	_cancel_timer_ex(&padapter->mlmepriv.scan_to_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("stop_drv_timers:cancel scan_to_timer! \n"));
	
#ifdef CONFIG_PWRCTRL
	_cancel_timer_ex(&padapter->mlmepriv.dhcp_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_err_,("stop_drv_timers:cancel dhcp_timer! \n"));
#endif

	_cancel_timer_ex(&padapter->mlmepriv.survey_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("stop_drv_timers: cancel survey_timer!\n"));
	
	_cancel_timer_ex(&padapter->mlmepriv.wdg_timer);
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("stop_drv_timers:cancel wdg_timer! \n"));
}

u8 init_default_value(_adapter *padapter)
{
	u8 ret  = _SUCCESS;
	struct registry_priv* pregistrypriv = &padapter->registrypriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct recv_priv	*precvpriv = &padapter->recvpriv;
	struct mlme_priv *pmlmepriv= &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;


	//xmit_priv
	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	pxmitpriv->rts_thresh = pregistrypriv->rts_thresh;
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;
	
		

	//recv_priv
	

	//mlme_priv
	pmlmepriv->passive_mode=1; // 1: active, 0: pasive. Maybe someday we should rename this varable to "active_mode" (Jeff)
	
	//qos_priv
	//pmlmepriv->qospriv.qos_option = pregistrypriv->wmm_enable;
	
	//ht_priv
#ifdef CONFIG_80211N_HT		
	{
		int i;
		struct ht_priv	 *phtpriv = &pmlmepriv->htpriv;
		
	//padapter->registrypriv.ht_enable = 1;//gtest
		
		phtpriv->ampdu_enable = _FALSE;//set to disabled

		for(i=0; i<16; i++)
		{
			phtpriv->baddbareq_issued[i] = _FALSE;
		}
		
	}	
#endif	

	//security_priv
	//get_encrypt_decrypt_from_registrypriv(padapter);
	psecuritypriv->sw_encrypt=pregistrypriv->software_encrypt;
	psecuritypriv->sw_decrypt=pregistrypriv->software_decrypt;
	psecuritypriv->binstallGrpkey=_FAIL;
	
	

	//pwrctrl_priv


	//registry_priv
	init_registrypriv_dev_network(padapter);		
	update_registrypriv_dev_network(padapter);


	//misc.
	
		
	return ret;

}

u8 init_drv_sw(_adapter *padapter)
{
	u8	ret8=_SUCCESS;

_func_enter_;

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+init_drv_sw\n"));

	if ((init_cmd_priv(&padapter->cmdpriv)) == _FAIL)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("\n Can't init cmd_priv\n"));
		ret8=_FAIL;
		goto exit;
	}
	padapter->cmdpriv.padapter=padapter;
	
	if ((init_evt_priv(&padapter->evtpriv)) == _FAIL)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("\n Can't init evt_priv\n"));
		ret8=_FAIL;
		goto exit;
	}
	
#ifdef CONFIG_RECV_BH
	tasklet_init(&padapter->evtpriv.event_tasklet, 
				(void(*)(unsigned long))recv_event_bh,
	     			(unsigned long)padapter);
#endif
	
	if (init_mlme_priv(padapter) == _FAIL)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("\n Can't init mlme_priv\n"));
		ret8=_FAIL;
		goto exit;
	}
		
	_init_xmit_priv(&padapter->xmitpriv, padapter);
		
	_init_recv_priv(&padapter->recvpriv, padapter);

	_memset((unsigned char *)&padapter->securitypriv, 0, sizeof (struct security_priv));	
	_init_timer(&(padapter->securitypriv.tkip_timer), padapter->pnetdev, use_tkipkey_handler, padapter);

	_init_sta_priv(&padapter->stapriv);
	padapter->stapriv.padapter = padapter;	

	init_bcmc_stainfo(padapter);

	init_pwrctrl_priv(padapter);	

	//_memset((u8 *)&padapter->qospriv, 0, sizeof (struct qos_priv));//move to mlme_priv

	_init_sema(&(padapter->pwrctrlpriv.pnp_pwr_mgnt_sema), 0);
		
#ifdef CONFIG_MP_INCLUDED
        mp871xinit(padapter); 
#endif

/*
#ifdef CONFIG_MLME_EXT
	if (init_mlme_ext_priv(padapter) == _FAIL)
	{
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("can't init mlme_ext_priv\n"));
		ret8=_FAIL;
		goto exit;
	}
#endif
*/
	ret8 = init_default_value(padapter);		

	InitSwLeds(padapter);

exit:
	
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("-init_drv_sw\n"));

	_func_exit_;	
	
	return ret8;
	
}

u8 free_drv_sw(_adapter *padapter)
{
	u8 bool;


	struct net_device *pnetdev = (struct net_device*)padapter->pnetdev;

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("==>free_drv_sw"));
	
	free_cmd_priv(&padapter->cmdpriv);	
	free_evt_priv(&padapter->evtpriv);

	DeInitSwLeds(padapter);
	
	free_mlme_priv(&padapter->mlmepriv);
	
	free_io_queue(padapter);
	
	_free_xmit_priv(&padapter->xmitpriv);
	
	_free_sta_priv(&padapter->stapriv); //will free bcmc_stainfo here
	
	_free_recv_priv(&padapter->recvpriv);

	_spinlock_free( &padapter->lockRxFF0Filter);

	//_mfree((void *)padapter, sizeof (padapter));

#ifdef CONFIG_MP_INCLUDED
        mp871xdeinit(padapter);
#endif

#ifdef CONFIG_DRVEXT_MODULE
	free_drvext(&padapter->drvextpriv);
#endif	

#ifdef CONFIG_MLME_EXT
	free_mlme_ext_priv(&padapter->mlmeextpriv);
#endif	

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("<==free_drv_sw\n"));

	if(pnetdev)
	{
		rtw_free_netdev(pnetdev);
	}

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("-free_drv_sw\n"));

	return _SUCCESS;
}


void enable_video_mode( _adapter* padapter, int cbw40_value)
{
        //   bit 8:
        //   1 -> enable video mode to 96B AP
        //   0 -> disable video mode to 96B AP
        //   bit 9:
        //   1 -> enable 40MHz mode
        //   0 -> disable 40MHz mode
        //   bit 10:
        //   1 -> enable STBC
        //   0 -> disable STBC
        u32  intcmd = 0xf4000500;   // enable bit8, bit10

        if ( cbw40_value )
        {
        //  if the driver supports the 40M bandwidth, we can enable the bit 9.
            intcmd |= 0x200;
        }
        fw_cmd( padapter, intcmd);
}

static int netdev_open(struct net_device *pnetdev)
{
	uint status;	
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+871x_drv - dev_open\n"));
	//printk("+871x_drv - drv_open, bup=%d\n", padapter->bup);

       if(padapter->bup == _FALSE)
    	{    
		padapter->bDriverStopped = _FALSE;
	 	padapter->bSurpriseRemoved = _FALSE;	 
        	padapter->bup = _TRUE;
	
		status = rtl871x_hal_init(padapter);		
		if (status ==_FAIL)
		{			
			RT_TRACE(_module_os_intfs_c_,_drv_err_,("rtl871x_hal_init(): Can't init h/w!\n"));
			goto netdev_open_error;
		}
		
		if ( initmac == NULL )	//	Use the mac address stored in the Efuse
		{
			_memcpy(pnetdev->dev_addr, padapter->eeprompriv.mac_addr, ETH_ALEN);
		}
		else
		{	//	Use the user specifiy mac address. So, we have to inform f/w to use it.
			msleep_os( 200 );
			setMacAddr_cmd( padapter, (u8*) pnetdev->dev_addr );
			//	The "myid" function will get the wifi mac address from eeprompriv structure instead of netdev structure.			
			//	So, we have to overwrite the mac_addr stored in the eeprompriv structure.
			//	In this case, the real mac address won't be used anymore.
			//	So that, the eeprompriv.mac_addr should store the mac which users specify.
			_memcpy( padapter->eeprompriv.mac_addr, pnetdev->dev_addr, ETH_ALEN );
		}

		printk("MAC Address= %x-%x-%x-%x-%x-%x\n", 
				 pnetdev->dev_addr[0],	pnetdev->dev_addr[1],  pnetdev->dev_addr[2],	pnetdev->dev_addr[3], pnetdev->dev_addr[4], pnetdev->dev_addr[5]);		

		
#ifdef CONFIG_MLME_EXT
		if (init_mlme_ext_priv(padapter) == _FAIL)
		{
			RT_TRACE(_module_os_intfs_c_,_drv_err_,("can't init mlme_ext_priv\n"));
			goto netdev_open_error;
		}
#endif

		
#ifdef CONFIG_DRVEXT_MODULE
		init_drvext(padapter);
#endif	   
		
		status=start_drv_threads(padapter);
		if(status ==_FAIL)
		{			
			RT_TRACE(_module_os_intfs_c_,_drv_err_,("Initialize driver software resource Failed!\n"));			
			goto netdev_open_error;			
		}
		

#ifdef CONFIG_USB_HCI	
		if(padapter->dvobjpriv.inirp_init == NULL)
		{
			RT_TRACE(_module_os_intfs_c_,_drv_err_,("Initialize dvobjpriv.inirp_init error!!!\n"));
			goto netdev_open_error;	
		}
		else
		{	
			padapter->dvobjpriv.inirp_init(padapter);
		}			
#endif

#ifdef CONFIG_IOCTL_CFG80211
		rtw_cfg80211_init_wiphy(padapter);
#endif
	
#ifdef CONFIG_PWRCTRL
		RT_TRACE(_module_os_intfs_c_,_drv_info_,("Initialize Power Mode. \n"));
		set_ps_mode(padapter, padapter->registrypriv.power_mgnt, padapter->registrypriv.smart_ps);
#endif	
	

#ifdef CONFIG_R871X_TEST
		//start_pseudo_adhoc(padapter);
#endif

       	//padapter->bDriverStopped = _FALSE;
	 	//padapter->bSurpriseRemoved = _FALSE;	 
        	//padapter->bup = _TRUE;
	}		
		
	//netif_carrier_on(pnetdev);//call this func when joinbss_event_callback return success       
 	if(!netif_queue_stopped(pnetdev))
      		netif_start_queue(pnetdev);
	else
		netif_wake_queue(pnetdev);
		
	 if ( video_mode )
	 {
              enable_video_mode( padapter, cbw40_enable );
	 }
	 
	//start driver mlme relation timer
	start_drv_timers(padapter);
	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_NO_LINK);	

        RT_TRACE(_module_os_intfs_c_,_drv_info_,("-871x_drv - dev_open\n"));
	//printk("-871x_drv - drv_open, bup=%d\n", padapter->bup);
		
	 return 0;
	
netdev_open_error:

	padapter->bup = _FALSE;
	
	netif_carrier_off(pnetdev);	
	netif_stop_queue(pnetdev);
	
	RT_TRACE(_module_os_intfs_c_,_drv_err_,("-871x_drv - dev_open, fail!\n"));
	//printk("-871x_drv - drv_open fail, bup=%d\n", padapter->bup);
	
	return (-1);
}

static int netdev_close(struct net_device *pnetdev)
{
	_adapter *padapter = (_adapter *)rtw_netdev_priv(pnetdev);
		
	RT_TRACE(_module_os_intfs_c_,_drv_info_,("+871x_drv - drv_close\n"));	

	// Close LED
        padapter->ledpriv.LedControlHandler(padapter, LED_CTL_POWER_OFF);
        msleep( 200 );

/*	if(!padapter->hw_init_completed)
	{
		printk("(1)871x_drv - drv_close, bup=%d, hw_init_completed=%d\n", padapter->bup, padapter->hw_init_completed);

	padapter->bDriverStopped = _TRUE;   

	r871x_dev_unload(padapter);	
	}
	else*/
	{
		printk("(2)871x_drv - drv_close, bup=%d, hw_init_completed=%d\n", padapter->bup, padapter->hw_init_completed);

		//s1.
		if(pnetdev)   
     		{
			if (!netif_queue_stopped(pnetdev))
				netif_stop_queue(pnetdev);
     		}
		
		#ifndef CONFIG_ANDROID
			
		//s2.	
		//s2-1.  issue disassoc_cmd to fw
		disassoc_cmd(padapter);
		//s2-2.  indicate disconnect to os
		indicate_disconnect(padapter);
		//s2-3. 
		free_assoc_resources(padapter);	
		//s2-4.
		free_network_queue(padapter);

		#endif
			

	}

#ifdef CONFIG_IOCTL_CFG80211
	printk("call rtw_indicate_scan_done when drv_close\n");
	rtw_indicate_scan_done(padapter, _TRUE);
#endif //CONFIG_IOCTL_CFG80211	

	//r871x_dev_unload(padapter);

	RT_TRACE(_module_os_intfs_c_,_drv_info_,("-871x_drv - drv_close\n"));
	printk("-871x_drv - drv_close, bup=%d\n", padapter->bup);
	   
	return 0;
}


#ifdef CONFIG_R871X_TEST

#include <mlme_osdep.h>

int start_pseudo_adhoc(_adapter *padapter)
{
     	_irqL irqL;
	int	res;
	struct sta_info *psta, *psta_old;
	unsigned char ibssid[6];
	unsigned char adhoc_sta_addr[6];
	unsigned long length;
		
	struct wlan_network *pnetwork=NULL;	
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);	
	struct wlan_network *tgt_network = &(pmlmepriv->cur_network);	


	ibssid[0] = 0x02;  //in ad-hoc mode bit1 must set to 1
	ibssid[1] = 0x87;
	ibssid[2] = 0x11;
	ibssid[3] = 0x12;
	ibssid[4] = 0x66;
	ibssid[5] = 0x55;

	//87-4c-e0-0-1-55
	adhoc_sta_addr[0] = 0x00; 
	adhoc_sta_addr[1] = 0xE0;
	adhoc_sta_addr[2] = 0x4C;
	adhoc_sta_addr[3] = 0x87;
	adhoc_sta_addr[4] = 0x12;

#ifdef CONFIG_R8712_TEST_ASTA
	adhoc_sta_addr[5] = 0x22;//A-22, B-11
#endif
#ifdef CONFIG_R8712_TEST_BSTA
	adhoc_sta_addr[5] = 0x11;//A-22, B-11
#endif

	pmlmepriv->fw_state = WIFI_ADHOC_STATE;
	set_fwstate(pmlmepriv, _FW_UNDER_LINKING);
	
	//clear psta in the cur_network, if any
	psta_old = get_stainfo(&padapter->stapriv, tgt_network->network.MacAddress);
	if (psta_old)
	   	free_stainfo(padapter,  psta_old);

	//create new  a wlan_network for mp driver and replace the cur_network;
	pnetwork= (struct wlan_network *)_malloc(sizeof(struct wlan_network));       
	if(pnetwork == NULL){
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("Can't alloc wlan_network for pseudo_adhoc\n"));
		return _FAIL;
	}
	_memset((unsigned char *)pnetwork, 0, sizeof (struct wlan_network));
	pnetwork->join_res = 1;//
	_memcpy(&(pnetwork->network.MacAddress), ibssid, ETH_ALEN);
		   
	pnetwork->network.InfrastructureMode = Ndis802_11IBSS;
	pnetwork->network.NetworkTypeInUse = Ndis802_11OFDM24;

	pnetwork->network.IELength = 0;
	   
	pnetwork->network.Ssid.SsidLength = 21;
	_memcpy(pnetwork->network.Ssid.Ssid , (unsigned char*)"rtl_pseudo_adhoc_8712", pnetwork->network.Ssid.SsidLength);

	length = get_NDIS_WLAN_BSSID_EX_sz(&pnetwork->network);
	pnetwork->network.Length = ((length>>2) + ((length%4 != 0) ? 1 : 0))*4;//round up to multiple of 4 bytes.

       //create a new psta for mp driver in the new created wlan_network
	//psta = alloc_stainfo(&padapter->stapriv, pnetwork->network.MacAddress);
	psta = alloc_stainfo(&padapter->stapriv, adhoc_sta_addr);
	if (psta == NULL) {
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("Can't alloc sta_info for pseudo_adhoc\n"));
		return _FAIL;
	}

/*
{
	//87-4c-e0-0-1-55
	adhoc_sta_addr[0] = 0x00; 
	adhoc_sta_addr[1] = 0xE0;
	adhoc_sta_addr[2] = 0x4C;
	adhoc_sta_addr[3] = 0x87;
	adhoc_sta_addr[4] = 0x12;
	adhoc_sta_addr[5] = 0x11;
	
	psta = alloc_stainfo(&padapter->stapriv, adhoc_sta_addr);
	if (psta == NULL) {
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("Can't alloc sta_info for pseudo_adhoc\n"));
		return _FAIL;
	}

	
}
*/
	_enter_critical(&pmlmepriv->lock, &irqL);

	tgt_network->join_res = pnetwork->join_res;
	
	if (pnetwork->join_res > 0) {

		if ((pmlmepriv->fw_state) & _FW_UNDER_LINKING) {

			psta_old = get_stainfo(&padapter->stapriv, tgt_network->network.MacAddress);

			if (psta_old)
				free_stainfo(padapter, psta_old);
			
			 _memcpy(&tgt_network->network, &pnetwork->network,
				(get_NDIS_WLAN_BSSID_EX_sz(&pnetwork->network)));

			tgt_network->aid = psta->aid  = pnetwork->join_res;

			(pmlmepriv->fw_state) ^= _FW_UNDER_LINKING;
		}
		else {

			//RT_TRACE(_module_os_intfs_c_,_drv_err_,"err: fw_state:%x",pmlmepriv->fw_state);
			free_stainfo(padapter, psta);
			
			res = _FAIL;
			goto end_of_mp_start_test;
		}

		  
              //Set to LINKED STATE for pseudo_adhoc
		pmlmepriv->fw_state |= _FW_LINKED;	

		os_indicate_connect(padapter);
			  
/*
              //NDIS_802_11_NETWORK_INFRASTRUCTURE networktype;
              if(padapter->eeprompriv.bautoload_fail_flag==_FALSE)
              		res = setopmode_cmd(padapter, 5);//?
*/
		res=_SUCCESS;

	}
	else {

		free_stainfo(padapter, psta);
              res = _FAIL;

	}	
				

	//
	pmlmepriv->qospriv.qos_option=1;
				
end_of_mp_start_test:				

	_exit_critical(&pmlmepriv->lock, &irqL);

	_mfree((unsigned char*)pnetwork, sizeof(struct wlan_network));

	return res;

}

int stop_pseudo_adhoc(_adapter *padapter)
{
  	struct sta_info *psta;	
	unsigned char adhoc_sta_addr[6];
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);	
	struct wlan_network *tgt_network = &(pmlmepriv->cur_network);	

	//87-4c-e0-0-1-55
	adhoc_sta_addr[0] = 0x00; 
	adhoc_sta_addr[1] = 0xE0;
	adhoc_sta_addr[2] = 0x4C;
	adhoc_sta_addr[3] = 0x87;
	adhoc_sta_addr[4] = 0x12;
	adhoc_sta_addr[5] = 0x22;

	 //return to normal state (default:null mode)
       pmlmepriv->fw_state = WIFI_NULL_STATE;

	os_indicate_disconnect(padapter);
	
	//clear psta used in mp test mode.
       psta = get_stainfo(&padapter->stapriv, adhoc_sta_addr);
       if(psta)	   	
	   	free_stainfo(padapter, psta);

/*
{
	//87-4c-e0-0-1-55
	adhoc_sta_addr[0] = 0x00; 
	adhoc_sta_addr[1] = 0xE0;
	adhoc_sta_addr[2] = 0x4C;
	adhoc_sta_addr[3] = 0x87;
	adhoc_sta_addr[4] = 0x12;
	adhoc_sta_addr[5] = 0x11;

	psta = get_stainfo(&padapter->stapriv, adhoc_sta_addr);
       if(psta)	   	
	   	free_stainfo(padapter, psta);

	   
}
*/
	//flush the cur_network   
	_memset((unsigned char *)tgt_network, 0, sizeof (struct wlan_network));	

	
	
	return _SUCCESS;

}

#endif
