/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#include <rtw_ht.h>
#include <rtw_cmd.h>
#include <rtw_xmit.h>
#include <rtw_recv.h>
#include <hal_intf.h>
#include <hal_com.h>
#include <rtw_security.h>
#include <rtw_pwrctrl.h>
#include <rtw_eeprom.h>
#include <sta_info.h>

struct qos_priv {
	/* bit mask option: u-apsd, s-apsd, ts, block ack... */
	unsigned int qos_option;
};

#include <rtw_mlme.h>
#include <rtw_debug.h>
#include <rtw_rf.h>
#include <rtw_event.h>
#include <rtw_led.h>
#include <rtw_mlme_ext.h>
#include <rtw_ap.h>

#define SPEC_DEV_ID_NONE		BIT(0)
#define SPEC_DEV_ID_DISABLE_HT		BIT(1)
#define SPEC_DEV_ID_ENABLE_PS		BIT(2)
#define SPEC_DEV_ID_RF_CONFIG_1T1R	BIT(3)
#define SPEC_DEV_ID_RF_CONFIG_2T2R	BIT(4)
#define SPEC_DEV_ID_ASSIGN_IFNAME	BIT(5)

struct registry_priv {
	struct ndis_802_11_ssid	ssid;
	u8	channel;/* ad-hoc support requirement */
	u8	wireless_mode;/* A, B, G, auto */
	u8	preamble;/* long, short, auto */
	u8	vrtl_carrier_sense;/* Enable, Disable, Auto */
	u8	vcs_type;/* RTS/CTS, CTS-to-self */
	u16	rts_thresh;
	u16	frag_thresh;
	u8	power_mgnt;
	u8	ips_mode;
	u8	smart_ps;
	u8	mp_mode;
	u8	acm_method;
	  /* UAPSD */
	u8	wmm_enable;
	u8	uapsd_enable;

	struct wlan_bssid_ex    dev_network;

	u8	ht_enable;
	u8	cbw40_enable;
	u8	ampdu_enable;/* for tx */
	u8	rx_stbc;
	u8	ampdu_amsdu;/* A-MPDU Supports A-MSDU is permitted */

	u8	wifi_spec;/*  !turbo_mode */

	u8	channel_plan;
	bool	accept_addba_req; /* true = accept AP's Add BA req */

	u8	antdiv_cfg;
	u8	antdiv_type;

	u8	usbss_enable;/* 0:disable,1:enable */
	u8	hwpdn_mode;/* 0:disable,1:enable,2:decide by EFUSE config */

	u8	max_roaming_times; /*  the max number driver will try */

	u8	fw_iol; /* enable iol without other concern */

	u8	enable80211d;

	u8	ifname[16];
	u8	if2name[16];

	u8	notch_filter;
	bool	monitor_enable;
};

#define MAX_CONTINUAL_URB_ERR		4

struct dvobj_priv {
	struct adapter *if1;
	/* For 92D, DMDP have 2 interface. */
	u8	InterfaceNumber;
	u8	NumInterfaces;

	/* In /Out Pipe information */
	int	RtInPipe[2];
	int	RtOutPipe[3];
	u8	Queue2Pipe[HW_QUEUE_ENTRY];/* for out pipe mapping */

/*-------- below is for USB INTERFACE --------*/
	u8	ishighspeed;
	u8	RtNumInPipes;
	u8	RtNumOutPipes;
	struct mutex  usb_vendor_req_mutex;

	struct usb_interface *pusbintf;
	struct usb_device *pusbdev;
};

static inline struct device *dvobj_to_dev(struct dvobj_priv *dvobj)
{
	/* todo: get interface type from dvobj and the return
	 * the dev accordingly
	 */
	return &dvobj->pusbintf->dev;
};

struct adapter {
	struct dvobj_priv *dvobj;
	struct	mlme_priv mlmepriv;
	struct	mlme_ext_priv mlmeextpriv;
	struct	cmd_priv	cmdpriv;
	struct	xmit_priv	xmitpriv;
	struct	recv_priv	recvpriv;
	struct	sta_priv	stapriv;
	struct	security_priv	securitypriv;
	struct	registry_priv	registrypriv;
	struct	pwrctrl_priv	pwrctrlpriv;
	struct	eeprom_priv eeprompriv;
	struct	led_priv	ledpriv;

	struct hal_data_8188e *HalData;

	s32	bDriverStopped;
	s32	bSurpriseRemoved;

	u8	hw_init_completed;

	void *cmdThread;
	struct  net_device *pnetdev;
	struct  net_device *pmondev;

	int bup;
	struct net_device_stats stats;
	struct iw_statistics iwstats;
	struct proc_dir_entry *dir_dev;/*  for proc directory */

	int net_closed;
	u8 bFWReady;
	u8 bReadPortCancel;
	u8 bWritePortCancel;

	struct mutex hw_init_mutex;
};

#define adapter_to_dvobj(adapter) (adapter->dvobj)

static inline u8 *myid(struct eeprom_priv *peepriv)
{
	return peepriv->mac_addr;
}

#endif /* __DRV_TYPES_H__ */
