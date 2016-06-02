/******************************************************************************
 * os_intfs.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>.
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _OS_INTFS_C_

#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/firmware.h>
#include "osdep_service.h"
#include "drv_types.h"
#include "xmit_osdep.h"
#include "recv_osdep.h"
#include "rtl871x_ioctl.h"
#include "usb_osintf.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("rtl871x wireless lan driver");
MODULE_AUTHOR("Larry Finger");

static char ifname[IFNAMSIZ] = "wlan%d";

/* module param defaults */
static int chip_version = RTL8712_2ndCUT;
static int rfintfs = HWPI;
static int lbkmode = RTL8712_AIR_TRX;
static int hci = RTL8712_USB;
static int ampdu_enable = 1;/*for enable tx_ampdu*/

/* The video_mode variable is for video mode.*/
/* It may be specify when inserting module with video_mode=1 parameter.*/
static int video_mode = 1;   /* enable video mode*/

/*Ndis802_11Infrastructure; infra, ad-hoc, auto*/
static int network_mode = Ndis802_11IBSS;
static int channel = 1;/*ad-hoc support requirement*/
static int wireless_mode = WIRELESS_11BG;
static int vrtl_carrier_sense = AUTO_VCS;
static int vcs_type = RTS_CTS;
static int frag_thresh = 2346;
static int preamble = PREAMBLE_LONG;/*long, short, auto*/
static int scan_mode = 1;/*active, passive*/
static int adhoc_tx_pwr = 1;
static int soft_ap;
static int smart_ps = 1;
static int power_mgnt = PS_MODE_ACTIVE;
static int radio_enable = 1;
static int long_retry_lmt = 7;
static int short_retry_lmt = 7;
static int busy_thresh = 40;
static int ack_policy = NORMAL_ACK;
static int mp_mode;
static int software_encrypt;
static int software_decrypt;

static int wmm_enable;/* default is set to disable the wmm.*/
static int uapsd_enable;
static int uapsd_max_sp = NO_LIMIT;
static int uapsd_acbk_en;
static int uapsd_acbe_en;
static int uapsd_acvi_en;
static int uapsd_acvo_en;

static int ht_enable = 1;
static int cbw40_enable = 1;
static int rf_config = RTL8712_RF_1T2R;  /* 1T2R*/
static int low_power;
/* mac address to use instead of the one stored in Efuse */
char *r8712_initmac;
static char *initmac;
/* if wifi_test = 1, driver will disable the turbo mode and pass it to
 * firmware private.
 */
static int wifi_test;

module_param_string(ifname, ifname, sizeof(ifname), S_IRUGO | S_IWUSR);
module_param(wifi_test, int, 0644);
module_param(initmac, charp, 0644);
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
module_param(ht_enable, int, 0644);
module_param(cbw40_enable, int, 0644);
module_param(ampdu_enable, int, 0644);
module_param(rf_config, int, 0644);
module_param(power_mgnt, int, 0644);
module_param(low_power, int, 0644);

MODULE_PARM_DESC(ifname, " Net interface name, wlan%d=default");
MODULE_PARM_DESC(initmac, "MAC-Address, default: use FUSE");

static int netdev_open(struct net_device *pnetdev);
static int netdev_close(struct net_device *pnetdev);

static void loadparam(struct _adapter *padapter, struct  net_device *pnetdev)
{
	struct registry_priv  *registry_par = &padapter->registrypriv;

	registry_par->chip_version = (u8)chip_version;
	registry_par->rfintfs = (u8)rfintfs;
	registry_par->lbkmode = (u8)lbkmode;
	registry_par->hci = (u8)hci;
	registry_par->network_mode  = (u8)network_mode;
	memcpy(registry_par->ssid.Ssid, "ANY", 3);
	registry_par->ssid.SsidLength = 3;
	registry_par->channel = (u8)channel;
	registry_par->wireless_mode = (u8)wireless_mode;
	registry_par->vrtl_carrier_sense = (u8)vrtl_carrier_sense;
	registry_par->vcs_type = (u8)vcs_type;
	registry_par->frag_thresh = (u16)frag_thresh;
	registry_par->preamble = (u8)preamble;
	registry_par->scan_mode = (u8)scan_mode;
	registry_par->adhoc_tx_pwr = (u8)adhoc_tx_pwr;
	registry_par->soft_ap = (u8)soft_ap;
	registry_par->smart_ps = (u8)smart_ps;
	registry_par->power_mgnt = (u8)power_mgnt;
	registry_par->radio_enable = (u8)radio_enable;
	registry_par->long_retry_lmt = (u8)long_retry_lmt;
	registry_par->short_retry_lmt = (u8)short_retry_lmt;
	registry_par->busy_thresh = (u16)busy_thresh;
	registry_par->ack_policy = (u8)ack_policy;
	registry_par->mp_mode = (u8)mp_mode;
	registry_par->software_encrypt = (u8)software_encrypt;
	registry_par->software_decrypt = (u8)software_decrypt;
	/*UAPSD*/
	registry_par->wmm_enable = (u8)wmm_enable;
	registry_par->uapsd_enable = (u8)uapsd_enable;
	registry_par->uapsd_max_sp = (u8)uapsd_max_sp;
	registry_par->uapsd_acbk_en = (u8)uapsd_acbk_en;
	registry_par->uapsd_acbe_en = (u8)uapsd_acbe_en;
	registry_par->uapsd_acvi_en = (u8)uapsd_acvi_en;
	registry_par->uapsd_acvo_en = (u8)uapsd_acvo_en;
	registry_par->ht_enable = (u8)ht_enable;
	registry_par->cbw40_enable = (u8)cbw40_enable;
	registry_par->ampdu_enable = (u8)ampdu_enable;
	registry_par->rf_config = (u8)rf_config;
	registry_par->low_power = (u8)low_power;
	registry_par->wifi_test = (u8) wifi_test;
	r8712_initmac = initmac;
}

static int r871x_net_set_mac_address(struct net_device *pnetdev, void *p)
{
	struct _adapter *padapter = netdev_priv(pnetdev);
	struct sockaddr *addr = p;

	if (!padapter->bup)
		ether_addr_copy(pnetdev->dev_addr, addr->sa_data);
	return 0;
}

static struct net_device_stats *r871x_net_get_stats(struct net_device *pnetdev)
{
	struct _adapter *padapter = netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct recv_priv *precvpriv = &(padapter->recvpriv);

	padapter->stats.tx_packets = pxmitpriv->tx_pkts;
	padapter->stats.rx_packets = precvpriv->rx_pkts;
	padapter->stats.tx_dropped = pxmitpriv->tx_drop;
	padapter->stats.rx_dropped = precvpriv->rx_drop;
	padapter->stats.tx_bytes = pxmitpriv->tx_bytes;
	padapter->stats.rx_bytes = precvpriv->rx_bytes;
	return &padapter->stats;
}

static const struct net_device_ops rtl8712_netdev_ops = {
	.ndo_open = netdev_open,
	.ndo_stop = netdev_close,
	.ndo_start_xmit = r8712_xmit_entry,
	.ndo_set_mac_address = r871x_net_set_mac_address,
	.ndo_get_stats = r871x_net_get_stats,
	.ndo_do_ioctl = r871x_ioctl,
};

struct net_device *r8712_init_netdev(void)
{
	struct _adapter *padapter;
	struct net_device *pnetdev;

	pnetdev = alloc_etherdev(sizeof(struct _adapter));
	if (!pnetdev)
		return NULL;
	if (dev_alloc_name(pnetdev, ifname) < 0) {
		strcpy(ifname, "wlan%d");
		dev_alloc_name(pnetdev, ifname);
	}
	padapter = netdev_priv(pnetdev);
	padapter->pnetdev = pnetdev;
	pr_info("r8712u: register rtl8712_netdev_ops to netdev_ops\n");
	pnetdev->netdev_ops = &rtl8712_netdev_ops;
	pnetdev->watchdog_timeo = HZ; /* 1 second timeout */
	pnetdev->wireless_handlers = (struct iw_handler_def *)
				     &r871x_handlers_def;
	loadparam(padapter, pnetdev);
	netif_carrier_off(pnetdev);
	padapter->pid = 0;  /* Initial the PID value used for HW PBC.*/
	return pnetdev;
}

static u32 start_drv_threads(struct _adapter *padapter)
{
	padapter->cmdThread = kthread_run(r8712_cmd_thread, padapter, "%s",
			      padapter->pnetdev->name);
	if (IS_ERR(padapter->cmdThread))
		return _FAIL;
	return _SUCCESS;
}

void r8712_stop_drv_threads(struct _adapter *padapter)
{
	/*Below is to terminate r8712_cmd_thread & event_thread...*/
	complete(&padapter->cmdpriv.cmd_queue_comp);
	if (padapter->cmdThread)
		_down_sema(&padapter->cmdpriv.terminate_cmdthread_sema);
	padapter->cmdpriv.cmd_seq = 1;
}

static void start_drv_timers(struct _adapter *padapter)
{
	mod_timer(&padapter->mlmepriv.sitesurveyctrl.sitesurvey_ctrl_timer,
		  jiffies + msecs_to_jiffies(5000));
	mod_timer(&padapter->mlmepriv.wdg_timer,
		  jiffies + msecs_to_jiffies(2000));
}

void r8712_stop_drv_timers(struct _adapter *padapter)
{
	del_timer_sync(&padapter->mlmepriv.assoc_timer);
	del_timer_sync(&padapter->securitypriv.tkip_timer);
	del_timer_sync(&padapter->mlmepriv.scan_to_timer);
	del_timer_sync(&padapter->mlmepriv.dhcp_timer);
	del_timer_sync(&padapter->mlmepriv.wdg_timer);
	del_timer_sync(&padapter->mlmepriv.sitesurveyctrl.sitesurvey_ctrl_timer);
}

static u8 init_default_value(struct _adapter *padapter)
{
	struct registry_priv *pregistrypriv = &padapter->registrypriv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	/*xmit_priv*/
	pxmitpriv->vcs_setting = pregistrypriv->vrtl_carrier_sense;
	pxmitpriv->vcs = pregistrypriv->vcs_type;
	pxmitpriv->vcs_type = pregistrypriv->vcs_type;
	pxmitpriv->rts_thresh = pregistrypriv->rts_thresh;
	pxmitpriv->frag_len = pregistrypriv->frag_thresh;
	/* mlme_priv */
	/* Maybe someday we should rename this variable to "active_mode"(Jeff)*/
	pmlmepriv->passive_mode = 1; /* 1: active, 0: passive. */
	/*ht_priv*/
	{
		int i;
		struct ht_priv	 *phtpriv = &pmlmepriv->htpriv;

		phtpriv->ampdu_enable = false;/*set to disabled*/
		for (i = 0; i < 16; i++)
			phtpriv->baddbareq_issued[i] = false;
	}
	/*security_priv*/
	psecuritypriv->sw_encrypt = pregistrypriv->software_encrypt;
	psecuritypriv->sw_decrypt = pregistrypriv->software_decrypt;
	psecuritypriv->binstallGrpkey = _FAIL;
	/*pwrctrl_priv*/
	/*registry_priv*/
	r8712_init_registrypriv_dev_network(padapter);
	r8712_update_registrypriv_dev_network(padapter);
	/*misc.*/
	return _SUCCESS;
}

u8 r8712_init_drv_sw(struct _adapter *padapter)
{
	if ((r8712_init_cmd_priv(&padapter->cmdpriv)) == _FAIL)
		return _FAIL;
	padapter->cmdpriv.padapter = padapter;
	if ((r8712_init_evt_priv(&padapter->evtpriv)) == _FAIL)
		return _FAIL;
	if (r8712_init_mlme_priv(padapter) == _FAIL)
		return _FAIL;
	_r8712_init_xmit_priv(&padapter->xmitpriv, padapter);
	_r8712_init_recv_priv(&padapter->recvpriv, padapter);
	memset((unsigned char *)&padapter->securitypriv, 0,
	       sizeof(struct security_priv));
	setup_timer(&padapter->securitypriv.tkip_timer,
		    r8712_use_tkipkey_handler, (unsigned long)padapter);
	_r8712_init_sta_priv(&padapter->stapriv);
	padapter->stapriv.padapter = padapter;
	r8712_init_bcmc_stainfo(padapter);
	r8712_init_pwrctrl_priv(padapter);
	mp871xinit(padapter);
	if (init_default_value(padapter) != _SUCCESS)
		return _FAIL;
	r8712_InitSwLeds(padapter);
	return _SUCCESS;
}

u8 r8712_free_drv_sw(struct _adapter *padapter)
{
	struct net_device *pnetdev = padapter->pnetdev;

	r8712_free_cmd_priv(&padapter->cmdpriv);
	r8712_free_evt_priv(&padapter->evtpriv);
	r8712_DeInitSwLeds(padapter);
	r8712_free_mlme_priv(&padapter->mlmepriv);
	r8712_free_io_queue(padapter);
	_free_xmit_priv(&padapter->xmitpriv);
	_r8712_free_sta_priv(&padapter->stapriv);
	_r8712_free_recv_priv(&padapter->recvpriv);
	mp871xdeinit(padapter);
	if (pnetdev)
		free_netdev(pnetdev);
	return _SUCCESS;
}


static void enable_video_mode(struct _adapter *padapter, int cbw40_value)
{
	/*   bit 8:
	 *   1 -> enable video mode to 96B AP
	 *   0 -> disable video mode to 96B AP
	 *   bit 9:
	 *   1 -> enable 40MHz mode
	 *   0 -> disable 40MHz mode
	 *   bit 10:
	 *   1 -> enable STBC
	 *   0 -> disable STBC
	 */
	u32  intcmd = 0xf4000500;   /* enable bit8, bit10*/

	if (cbw40_value) {
		/* if the driver supports the 40M bandwidth,
		 * we can enable the bit 9.
		 */
		intcmd |= 0x200;
	}
	r8712_fw_cmd(padapter, intcmd);
}

/**
 *
 * This function intends to handle the activation of an interface
 * i.e. when it is brought Up/Active from a Down state.
 *
 */
static int netdev_open(struct net_device *pnetdev)
{
	struct _adapter *padapter = netdev_priv(pnetdev);

	mutex_lock(&padapter->mutex_start);
	if (!padapter->bup) {
		padapter->bDriverStopped = false;
		padapter->bSurpriseRemoved = false;
		padapter->bup = true;
		if (rtl871x_hal_init(padapter) != _SUCCESS)
			goto netdev_open_error;
		if (!r8712_initmac)
			/* Use the mac address stored in the Efuse */
			memcpy(pnetdev->dev_addr,
				padapter->eeprompriv.mac_addr, ETH_ALEN);
		else {
			/* We have to inform f/w to use user-supplied MAC
			 * address.
			 */
			msleep(200);
			r8712_setMacAddr_cmd(padapter, (u8 *)pnetdev->dev_addr);
			/*
			 * The "myid" function will get the wifi mac address
			 * from eeprompriv structure instead of netdev
			 * structure. So, we have to overwrite the mac_addr
			 * stored in the eeprompriv structure. In this case,
			 * the real mac address won't be used anymore. So that,
			 * the eeprompriv.mac_addr should store the mac which
			 * users specify.
			 */
			memcpy(padapter->eeprompriv.mac_addr,
				pnetdev->dev_addr, ETH_ALEN);
		}
		if (start_drv_threads(padapter) != _SUCCESS)
			goto netdev_open_error;
		if (!padapter->dvobjpriv.inirp_init)
			goto netdev_open_error;
		else
			padapter->dvobjpriv.inirp_init(padapter);
		r8712_set_ps_mode(padapter, padapter->registrypriv.power_mgnt,
				  padapter->registrypriv.smart_ps);
	}
	if (!netif_queue_stopped(pnetdev))
		netif_start_queue(pnetdev);
	else
		netif_wake_queue(pnetdev);

	 if (video_mode)
		enable_video_mode(padapter, cbw40_enable);
	/* start driver mlme relation timer */
	start_drv_timers(padapter);
	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_NO_LINK);
	mutex_unlock(&padapter->mutex_start);
	return 0;
netdev_open_error:
	padapter->bup = false;
	netif_carrier_off(pnetdev);
	netif_stop_queue(pnetdev);
	mutex_unlock(&padapter->mutex_start);
	return -1;
}

/**
 *
 * This function intends to handle the shutdown of an interface
 * i.e. when it is brought Down from an Up/Active state.
 *
 */
static int netdev_close(struct net_device *pnetdev)
{
	struct _adapter *padapter = netdev_priv(pnetdev);

	/* Close LED*/
	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_POWER_OFF);
	msleep(200);

	/*s1.*/
	if (pnetdev) {
		if (!netif_queue_stopped(pnetdev))
			netif_stop_queue(pnetdev);
	}
	/*s2.*/
	/*s2-1.  issue disassoc_cmd to fw*/
	r8712_disassoc_cmd(padapter);
	/*s2-2.  indicate disconnect to os*/
	r8712_ind_disconnect(padapter);
	/*s2-3.*/
	r8712_free_assoc_resources(padapter);
	/*s2-4.*/
	r8712_free_network_queue(padapter);
	return 0;
}

#include "mlme_osdep.h"
