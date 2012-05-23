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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
/*---------------------------------------------------------------------

	For type defines and data structure defines

-----------------------------------------------------------------------*/
#ifndef __DRV_TYPES_H__
#define __DRV_TYPES_H__

struct _adapter;

#include "osdep_service.h"
#include "wlan_bssdef.h"
#include "rtl8712_spec.h"
#include "rtl8712_hal.h"
#include <linux/mutex.h>
#include <linux/completion.h>

enum _NIC_VERSION {
	RTL8711_NIC,
	RTL8712_NIC,
	RTL8713_NIC,
	RTL8716_NIC
};

struct _adapter;

struct	qos_priv	{
	/* bit mask option: u-apsd, s-apsd, ts, block ack... */
	unsigned int qos_option;
};

#include "rtl871x_ht.h"
#include "rtl871x_cmd.h"
#include "rtl871x_xmit.h"
#include "rtl871x_recv.h"
#include "rtl871x_security.h"
#include "rtl871x_pwrctrl.h"
#include "rtl871x_io.h"
#include "rtl871x_eeprom.h"
#include "sta_info.h"
#include "rtl871x_mlme.h"
#include "rtl871x_mp.h"
#include "rtl871x_debug.h"
#include "rtl871x_rf.h"
#include "rtl871x_event.h"
#include "rtl871x_led.h"

#define SPEC_DEV_ID_NONE BIT(0)
#define SPEC_DEV_ID_DISABLE_HT BIT(1)
#define SPEC_DEV_ID_ENABLE_PS BIT(2)

struct specific_device_id {
	u32		flags;
	u16		idVendor;
	u16		idProduct;

};

struct registry_priv {
	u8	chip_version;
	u8	rfintfs;
	u8	lbkmode;
	u8	hci;
	u8	network_mode;	/*infra, ad-hoc, auto*/
	struct ndis_802_11_ssid	ssid;
	u8	channel;/* ad-hoc support requirement */
	u8	wireless_mode;/* A, B, G, auto */
	u8	vrtl_carrier_sense; /*Enable, Disable, Auto*/
	u8	vcs_type;/*RTS/CTS, CTS-to-self*/
	u16	rts_thresh;
	u16  frag_thresh;
	u8	preamble;/*long, short, auto*/
	u8  scan_mode;/*active, passive*/
	u8  adhoc_tx_pwr;
	u8  soft_ap;
	u8  smart_ps;
	u8 power_mgnt;
	u8 radio_enable;
	u8 long_retry_lmt;
	u8 short_retry_lmt;
	u16 busy_thresh;
	u8 ack_policy;
	u8 mp_mode;
	u8 software_encrypt;
	u8 software_decrypt;
	/* UAPSD */
	u8 wmm_enable;
	u8 uapsd_enable;
	u8 uapsd_max_sp;
	u8 uapsd_acbk_en;
	u8 uapsd_acbe_en;
	u8 uapsd_acvi_en;
	u8 uapsd_acvo_en;

	struct wlan_bssid_ex dev_network;

	u8 ht_enable;
	u8 cbw40_enable;
	u8 ampdu_enable;/*for tx*/
	u8 rf_config;
	u8 low_power;
	u8 wifi_test;
};

/* For registry parameters */
#define RGTRY_OFT(field) ((addr_t)FIELD_OFFSET(struct registry_priv, field))
#define RGTRY_SZ(field)   sizeof(((struct registry_priv *)0)->field)
#define BSSID_OFT(field) ((addr_t)FIELD_OFFSET(struct ndis_wlan_bssid_ex, \
			 field))
#define BSSID_SZ(field)   sizeof(((struct ndis_wlan_bssid_ex *)0)->field)

struct dvobj_priv {
	struct _adapter *padapter;
	u32 nr_endpoint;
	u8   ishighspeed;
	uint(*inirp_init)(struct _adapter *adapter);
	uint(*inirp_deinit)(struct _adapter *adapter);
	struct usb_device *pusbdev;
};

/**
 * struct _adapter - the main adapter structure for this device.
 *
 * bup: True indicates that the interface is up.
 */
struct _adapter {
	struct	dvobj_priv dvobjpriv;
	struct	mlme_priv mlmepriv;
	struct	cmd_priv	cmdpriv;
	struct	evt_priv	evtpriv;
	struct	io_queue	*pio_queue;
	struct	xmit_priv	xmitpriv;
	struct	recv_priv	recvpriv;
	struct	sta_priv	stapriv;
	struct	security_priv	securitypriv;
	struct	registry_priv	registrypriv;
	struct	wlan_acl_pool	acl_list;
	struct	pwrctrl_priv	pwrctrlpriv;
	struct	eeprom_priv eeprompriv;
	struct	hal_priv	halpriv;
	struct	led_priv	ledpriv;
	struct mp_priv  mppriv;
	s32	bDriverStopped;
	s32	bSurpriseRemoved;
	u32	IsrContent;
	u32	ImrContent;
	bool	fw_found;
	u8	EepromAddressSize;
	u8	hw_init_completed;
	struct task_struct *cmdThread;
	 pid_t evtThread;
	struct task_struct *xmitThread;
	pid_t recvThread;
	uint(*dvobj_init)(struct _adapter *adapter);
	void  (*dvobj_deinit)(struct _adapter *adapter);
	struct net_device *pnetdev;
	int bup;
	struct net_device_stats stats;
	struct iw_statistics iwstats;
	int pid; /*process id from UI*/
	_workitem wkFilterRxFF0;
	u8 blnEnableRxFF0Filter;
	spinlock_t lockRxFF0Filter;
	const struct firmware *fw;
	struct usb_interface *pusb_intf;
	struct mutex mutex_start;
	struct completion rtl8712_fw_ready;
};

static inline u8 *myid(struct eeprom_priv *peepriv)
{
	return peepriv->mac_addr;
}

u8 r8712_usb_hal_bus_init(struct _adapter *adapter);

#endif /*__DRV_TYPES_H__*/

