/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

/*-----------------------------------------------------------------------------

	For type defines and data structure defines

------------------------------------------------------------------------------*/

#ifndef __DRV_TYPES_H__
#define __DRV_TYPES_H__

#define DRV_NAME "r8188eu"

#include "osdep_service.h"
#include "wlan_bssdef.h"
#include "rtw_ht.h"
#include "rtw_cmd.h"
#include "rtw_xmit.h"
#include "rtw_recv.h"
#include "hal_intf.h"
#include "hal_com.h"
#include "rtw_security.h"
#include "rtw_pwrctrl.h"
#include "rtw_io.h"
#include "rtw_eeprom.h"
#include "sta_info.h"
#include "rtw_mlme.h"
#include "rtw_rf.h"
#include "rtw_event.h"
#include "rtw_led.h"
#include "rtw_mlme_ext.h"
#include "rtw_p2p.h"
#include "rtw_ap.h"
#include "rtw_br_ext.h"
#include "rtl8188e_hal.h"
#include "rtw_fw.h"

#define DRIVERVERSION	"v4.1.4_6773.20130222"

struct registry_priv {
	u8	chip_version;
	u8	rfintfs;
	u8	lbkmode;
	u8	hci;
	struct ndis_802_11_ssid	ssid;
	u8	network_mode;	/* infra, ad-hoc, auto */
	u8	channel;/* ad-hoc support requirement */
	u8	wireless_mode;/* A, B, G, auto */
	u8	scan_mode;/* active, passive */
	u8	radio_enable;
	u8	preamble;/* long, short, auto */
	u8	vrtl_carrier_sense;/* Enable, Disable, Auto */
	u8	vcs_type;/* RTS/CTS, CTS-to-self */
	u16	rts_thresh;
	u16	frag_thresh;
	u8	adhoc_tx_pwr;
	u8	soft_ap;
	u8	power_mgnt;
	u8	ips_mode;
	u8	smart_ps;
	u8	long_retry_lmt;
	u8	short_retry_lmt;
	u16	busy_thresh;
	u8	ack_policy;
	u8	software_encrypt;
	u8	software_decrypt;
	u8	acm_method;
	  /* UAPSD */
	u8	wmm_enable;
	u8	uapsd_enable;
	u8	uapsd_max_sp;
	u8	uapsd_acbk_en;
	u8	uapsd_acbe_en;
	u8	uapsd_acvi_en;
	u8	uapsd_acvo_en;

	u8	led_enable;

	struct wlan_bssid_ex    dev_network;

	u8	ht_enable;
	u8	cbw40_enable;
	u8	ampdu_enable;/* for tx */
	u8	rx_stbc;
	u8	ampdu_amsdu;/* A-MPDU Supports A-MSDU is permitted */
	u8	lowrate_two_xmit;

	u8	low_power;

	u8	wifi_spec;/*  !turbo_mode */

	u8	channel_plan;
	bool	bAcceptAddbaReq;

	u8	antdiv_cfg;
	u8	antdiv_type;

	u8	usbss_enable;/* 0:disable,1:enable */
	u8	hwpdn_mode;/* 0:disable,1:enable,2:decide by EFUSE config */
	u8	hwpwrp_detect;/* 0:disable,1:enable */

	u8	hw_wps_pbc;/* 0:disable,1:enable */

	u8	max_roaming_times; /*  the max number driver will try */

	u8	fw_iol; /* enable iol without other concern */

	u8	enable80211d;

	u8	ifname[16];
	u8	if2name[16];

	u8	notch_filter;
};

#define MAX_CONTINUAL_URB_ERR		4

struct dvobj_priv {
	struct adapter *if1;

	/* For 92D, DMDP have 2 interface. */
	u8	InterfaceNumber;
	u8	NumInterfaces;

	/* In /Out Pipe information */
	int	RtInPipe;
	int	RtOutPipe[3];
	u8	Queue2Pipe[HW_QUEUE_ENTRY];/* for out pipe mapping */

	struct rt_firmware firmware;

/*-------- below is for USB INTERFACE --------*/

	u8	RtNumOutPipes;

	struct usb_interface *pusbintf;
	struct usb_device *pusbdev;

	atomic_t continual_urb_error;
};

static inline struct device *dvobj_to_dev(struct dvobj_priv *dvobj)
{
	/* todo: get interface type from dvobj and the return
	 * the dev accordingly */
	return &dvobj->pusbintf->dev;
};

struct adapter {
	int	pid[3];/* process id from UI, 0:wps, 1:hostapd, 2:dhcpcd */

	struct dvobj_priv *dvobj;
	struct	mlme_priv mlmepriv;
	struct	mlme_ext_priv mlmeextpriv;
	struct	cmd_priv	cmdpriv;
	struct	evt_priv	evtpriv;
	struct	io_priv	iopriv;
	struct	xmit_priv	xmitpriv;
	struct	recv_priv	recvpriv;
	struct	sta_priv	stapriv;
	struct	security_priv	securitypriv;
	struct	registry_priv	registrypriv;
	struct	pwrctrl_priv	pwrctrlpriv;
	struct	eeprom_priv eeprompriv;
	struct	led_priv	ledpriv;
	struct wifidirect_info	wdinfo;

	struct hal_data_8188e haldata;

	s32	bDriverStopped;
	s32	bSurpriseRemoved;
	s32	bCardDisableWOHSM;

	u8	hw_init_completed;
	s8	signal_strength;

	void *cmdThread;
	void (*intf_start)(struct adapter *adapter);
	void (*intf_stop)(struct adapter *adapter);
	struct  net_device *pnetdev;

	/*  used by rtw_rereg_nd_name related function */
	struct rereg_nd_name_data {
		struct  net_device *old_pnetdev;
		char old_ifname[IFNAMSIZ];
		u8 old_ips_mode;
		u8 old_bRegUseLed;
	} rereg_nd_name_priv;

	int bup;
	struct net_device_stats stats;
	struct iw_statistics iwstats;
	struct proc_dir_entry *dir_dev;/*  for proc directory */

	int net_closed;
	u8 bFWReady;
	u8 bReadPortCancel;
	u8 bWritePortCancel;
	u8 bRxRSSIDisplay;
	/* The driver will show up the desired channel number
	 * when this flag is 1. */
	u8 bNotifyChannelChange;
	/* The driver will show the current P2P status when the
	 * upper application reads it. */
	u8 bShowGetP2PState;
	struct adapter *pbuddy_adapter;

	struct mutex *hw_init_mutex;

	spinlock_t br_ext_lock;
	struct nat25_network_db_entry	*nethash[NAT25_HASH_SIZE];
	int				pppoe_connection_in_progress;
	unsigned char			pppoe_addr[ETH_ALEN];
	unsigned char			scdb_mac[ETH_ALEN];
	unsigned char			scdb_ip[4];
	struct nat25_network_db_entry	*scdb_entry;
	unsigned char			br_mac[ETH_ALEN];
	unsigned char			br_ip[4];
	struct br_ext_info		ethBrExtInfo;
};

#define adapter_to_dvobj(adapter) (adapter->dvobj)

int rtw_handle_dualmac(struct adapter *adapter, bool init);

static inline u8 *myid(struct eeprom_priv *peepriv)
{
	return peepriv->mac_addr;
}

#endif /* __DRV_TYPES_H__ */
