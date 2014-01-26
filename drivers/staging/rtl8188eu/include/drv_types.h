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
/*-----------------------------------------------------------------------------

	For type defines and data structure defines

------------------------------------------------------------------------------*/


#ifndef __DRV_TYPES_H__
#define __DRV_TYPES_H__

#define DRV_NAME "r8188eu"

#include <osdep_service.h>
#include <wlan_bssdef.h>
#include <drv_types_linux.h>
#include <rtw_ht.h>
#include <rtw_cmd.h>
#include <rtw_xmit.h>
#include <rtw_recv.h>
#include <hal_intf.h>
#include <hal_com.h>
#include <rtw_qos.h>
#include <rtw_security.h>
#include <rtw_pwrctrl.h>
#include <rtw_io.h>
#include <rtw_eeprom.h>
#include <sta_info.h>
#include <rtw_mlme.h>
#include <rtw_debug.h>
#include <rtw_rf.h>
#include <rtw_event.h>
#include <rtw_led.h>
#include <rtw_mlme_ext.h>
#include <rtw_p2p.h>
#include <rtw_ap.h>
#include <rtw_mp.h>
#include <rtw_br_ext.h>

enum _NIC_VERSION {
	RTL8711_NIC,
	RTL8712_NIC,
	RTL8713_NIC,
	RTL8716_NIC
};

#define SPEC_DEV_ID_NONE		BIT(0)
#define SPEC_DEV_ID_DISABLE_HT		BIT(1)
#define SPEC_DEV_ID_ENABLE_PS		BIT(2)
#define SPEC_DEV_ID_RF_CONFIG_1T1R	BIT(3)
#define SPEC_DEV_ID_RF_CONFIG_2T2R	BIT(4)
#define SPEC_DEV_ID_ASSIGN_IFNAME	BIT(5)

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
	u8	mp_mode;
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

	struct wlan_bssid_ex    dev_network;

	u8	ht_enable;
	u8	cbw40_enable;
	u8	ampdu_enable;/* for tx */
	u8	rx_stbc;
	u8	ampdu_amsdu;/* A-MPDU Supports A-MSDU is permitted */
	u8	lowrate_two_xmit;

	u8	rf_config;
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

/* For registry parameters */
#define RGTRY_OFT(field) ((u32)FIELD_OFFSET(struct registry_priv, field))
#define RGTRY_SZ(field)   sizeof(((struct registry_priv *)0)->field)
#define BSSID_OFT(field) ((u32)FIELD_OFFSET(struct wlan_bssid_ex, field))
#define BSSID_SZ(field)   sizeof(((struct wlan_bssid_ex *)0)->field)

#define MAX_CONTINUAL_URB_ERR		4

struct dvobj_priv {
	struct adapter *if1;
	struct adapter *if2;

	/* For 92D, DMDP have 2 interface. */
	u8	InterfaceNumber;
	u8	NumInterfaces;

	/* In /Out Pipe information */
	int	RtInPipe[2];
	int	RtOutPipe[3];
	u8	Queue2Pipe[HW_QUEUE_ENTRY];/* for out pipe mapping */

	u8	irq_alloc;

/*-------- below is for USB INTERFACE --------*/

	u8	nr_endpoint;
	u8	ishighspeed;
	u8	RtNumInPipes;
	u8	RtNumOutPipes;
	int	ep_num[5]; /* endpoint number */
	int	RegUsbSS;
	struct semaphore usb_suspend_sema;
	struct mutex  usb_vendor_req_mutex;

	u8 *usb_alloc_vendor_req_buf;
	u8 *usb_vendor_req_buf;

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

enum _IFACE_TYPE {
	IFACE_PORT0, /* mapping to port0 for C/D series chips */
	IFACE_PORT1, /* mapping to port1 for C/D series chip */
	MAX_IFACE_PORT,
};

enum _ADAPTER_TYPE {
	PRIMARY_ADAPTER,
	SECONDARY_ADAPTER,
	MAX_ADAPTER,
};

enum driver_state {
	DRIVER_NORMAL = 0,
	DRIVER_DISAPPEAR = 1,
	DRIVER_REPLACE_DONGLE = 2,
};

struct adapter {
	int	DriverState;/* for disable driver using module, use dongle toi
			     * replace module. */
	int	pid[3];/* process id from UI, 0:wps, 1:hostapd, 2:dhcpcd */
	int	bDongle;/* build-in module or external dongle */
	u16	chip_type;

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
	struct	mp_priv	mppriv;

#ifdef CONFIG_88EU_AP_MODE
	struct	hostapd_priv	*phostapdpriv;
#endif

	struct wifidirect_info	wdinfo;

	void *HalData;
	u32 hal_data_sz;
	struct hal_ops	HalFunc;

	s32	bDriverStopped;
	s32	bSurpriseRemoved;
	s32	bCardDisableWOHSM;

	u32	IsrContent;
	u32	ImrContent;

	u8	EepromAddressSize;
	u8	hw_init_completed;
	u8	bDriverIsGoingToUnload;
	u8	init_adpt_in_progress;
	u8	bHaltInProgress;

	void *cmdThread;
	void *evtThread;
	void *xmitThread;
	void *recvThread;
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
	u8 bBTFWReady;
	u8 bReadPortCancel;
	u8 bWritePortCancel;
	u8 bRxRSSIDisplay;
	/* The driver will show up the desired channel number
	 * when this flag is 1. */
	u8 bNotifyChannelChange;
#ifdef CONFIG_88EU_P2P
	/* The driver will show the current P2P status when the
	 * upper application reads it. */
	u8 bShowGetP2PState;
#endif
	struct adapter *pbuddy_adapter;

	struct mutex *hw_init_mutex;

	spinlock_t br_ext_lock;
	struct nat25_network_db_entry	*nethash[NAT25_HASH_SIZE];
	int				pppoe_connection_in_progress;
	unsigned char			pppoe_addr[MACADDRLEN];
	unsigned char			scdb_mac[MACADDRLEN];
	unsigned char			scdb_ip[4];
	struct nat25_network_db_entry	*scdb_entry;
	unsigned char			br_mac[MACADDRLEN];
	unsigned char			br_ip[4];
	struct br_ext_info		ethBrExtInfo;

	u8	fix_rate;

	unsigned char     in_cta_test;
};

#define adapter_to_dvobj(adapter) (adapter->dvobj)

int rtw_handle_dualmac(struct adapter *adapter, bool init);

static inline u8 *myid(struct eeprom_priv *peepriv)
{
	return peepriv->mac_addr;
}

#endif /* __DRV_TYPES_H__ */
