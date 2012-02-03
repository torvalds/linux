/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
/*-------------------------------------------------------------------------------
	
	For type defines and data structure defines

--------------------------------------------------------------------------------*/


#ifndef __DRV_TYPES_H__
#define __DRV_TYPES_H__

#include <drv_conf.h>
#include <osdep_service.h>
#include <wlan_bssdef.h>


#ifdef PLATFORM_OS_XP
#include <drv_types_xp.h>
#endif

#ifdef PLATFORM_OS_CE
#include <drv_types_ce.h>
#endif

#ifdef PLATFORM_LINUX
#include <drv_types_linux.h>
#endif

enum _NIC_VERSION {
	
	RTL8711_NIC,
	RTL8712_NIC,
	RTL8713_NIC,
	RTL8716_NIC
		
};

enum{
	UP_LINK,
	DOWN_LINK,	
};
typedef struct _ADAPTER _adapter, ADAPTER,*PADAPTER;

#ifdef CONFIG_80211N_HT
#include <rtw_ht.h>
#endif

#include <rtw_cmd.h>
#include <wlan_bssdef.h>
#include <rtw_xmit.h>
#include <rtw_recv.h>
#include <hal_init.h>
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

#ifdef CONFIG_DRVEXT_MODULE
#include <drvext_api.h>
#endif

#ifdef CONFIG_MP_INCLUDED
#include <rtw_mp.h>
#endif

#ifdef CONFIG_BR_EXT
#include <rtw_br_ext.h>
#endif	// CONFIG_BR_EXT

#define SPEC_DEV_ID_NONE BIT(0)
#define SPEC_DEV_ID_DISABLE_HT BIT(1)
#define SPEC_DEV_ID_ENABLE_PS BIT(2)
#define SPEC_DEV_ID_RF_CONFIG_1T1R BIT(3)
#define SPEC_DEV_ID_RF_CONFIG_2T2R BIT(4)
#define SPEC_DEV_ID_ASSIGN_IFNAME BIT(5)

struct specific_device_id{
	
	u32		flags;
	
	u16		idVendor;
	u16		idProduct;

};

struct registry_priv
{    
	u8	chip_version;
	u8	rfintfs;
	u8	lbkmode;
	u8	hci;
	NDIS_802_11_SSID	ssid;
	u8	network_mode;	//infra, ad-hoc, auto	  
	u8	channel;//ad-hoc support requirement 
	u8	wireless_mode;//A, B, G, auto
	u8 	scan_mode;//active, passive
	u8	radio_enable;
	u8	preamble;//long, short, auto
	u8	vrtl_carrier_sense;//Enable, Disable, Auto
	u8	vcs_type;//RTS/CTS, CTS-to-self
	u16	rts_thresh;
	u16  frag_thresh;	
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

	  //UAPSD
	u8	wmm_enable;
	u8	uapsd_enable;	  
	u8	uapsd_max_sp;
	u8	uapsd_acbk_en;
	u8	uapsd_acbe_en;
	u8	uapsd_acvi_en;
	u8	uapsd_acvo_en;	  

	WLAN_BSSID_EX    dev_network;

#ifdef CONFIG_80211N_HT
	u8	ht_enable;
	u8	cbw40_enable;
	u8	ampdu_enable;//for tx
	u8 	rx_stbc;
	u8	ampdu_amsdu;//A-MPDU Supports A-MSDU is permitted
#endif
	u8	lowrate_two_xmit;
	
	u8	rf_config ;
	u8	low_power ;

	u8	wifi_spec;// !turbo_mode	  
	  
	u8	channel_plan;
#ifdef CONFIG_BT_COEXIST
	u8	bt_iso;
	u8	bt_sco;
	u8	bt_ampdu;
#endif
	BOOLEAN	bAcceptAddbaReq;	

	u8	antdiv_cfg;

	u8	usbss_enable;//0:disable,1:enable
	u8	hwpdn_mode;//0:disable,1:enable,2:decide by EFUSE config
	u8	hwpwrp_detect;//0:disable,1:enable

	u8	hw_wps_pbc;//0:disable,1:enable

#ifdef CONFIG_ADAPTOR_INFO_CACHING_FILE
	char	adaptor_info_caching_file_path[PATH_LENGTH_MAX];
#endif

#ifdef CONFIG_LAYER2_ROAMING
	u8	max_roaming_times; // the max number driver will try to roaming
#endif
};


//For registry parameters
#define RGTRY_OFT(field) ((ULONG)FIELD_OFFSET(struct registry_priv,field))
#define RGTRY_SZ(field)   sizeof(((struct registry_priv*) 0)->field)
#define BSSID_OFT(field) ((ULONG)FIELD_OFFSET(WLAN_BSSID_EX,field))
#define BSSID_SZ(field)   sizeof(((PWLAN_BSSID_EX) 0)->field)

#define MAX_CONTINUAL_URB_ERR 4

struct dvobj_priv {

	_adapter * padapter;

	//For 92D, DMDP have 2 interface.
	u8	InterfaceNumber;
	u8	NumInterfaces;

/*-------- below is for SDIO INTERFACE --------*/

#ifdef CONFIG_SDIO_HCI

#ifdef PLATFORM_OS_XP
	PDEVICE_OBJECT	pphysdevobj;//pPhysDevObj;
	PDEVICE_OBJECT	pfuncdevobj;//pFuncDevObj;
	PDEVICE_OBJECT	pnextdevobj;//pNextDevObj;
	SDBUS_INTERFACE_STANDARD	sdbusinft;//SdBusInterface;
	u8	nextdevstacksz;//unsigned char			 NextDeviceStackSize;
#endif//PLATFORM_OS_XP

#ifdef PLATFORM_OS_CE
	SD_DEVICE_HANDLE hDevice;     
	SD_CARD_RCA                 sd_rca;
	SD_CARD_INTERFACE           card_intf;
	BOOLEAN                     enableIsarWithStatus;
	WCHAR	active_path[MAX_ACTIVE_REG_PATH];
	SD_HOST_BLOCK_CAPABILITY    sd_host_blk_cap;
#endif//PLATFORM_OS_CE

#ifdef PLATFORM_LINUX
	struct sdio_func	*func;	
#endif//PLATFORM_LINUX

	u8	func_number;//unsigned char			FunctionNumber;
	u32	block_transfer_len;//unsigned long			BLOCK_TRANSFER_LEN;
	u32	blk_shiftbits;
	u16	driver_version;
	u16	rxblknum;
	u16	rxblknum_rd;
	u16	c2hblknum; 
	u8  tx_block_mode;
	u8  rx_block_mode;
	u8 cmdfifo_cnt;
	u8 rxfifo_cnt;
	u16	sdio_hisr;
	u16	sdio_himr;	
#endif//	CONFIG_SDIO_HCI

/*-------- below is for USB INTERFACE --------*/
 
#ifdef CONFIG_USB_HCI

	u8	nr_endpoint;
	u8	ishighspeed;
	u8	RtNumInPipes;
	u8	RtNumOutPipes;
	int	ep_num[5]; //endpoint number

	int	RegUsbSS;
	
	_sema	usb_suspend_sema;

#ifdef CONFIG_USB_VENDOR_REQ_MUTEX
	_mutex  usb_vendor_req_mutex;
#endif
	
#ifdef CONFIG_USB_VENDOR_REQ_BUFFER_PREALLOC
	u8 * usb_alloc_vendor_req_buf;
	u8 * usb_vendor_req_buf;
#endif	

#ifdef PLATFORM_WINDOWS
	//related device objects
	PDEVICE_OBJECT	pphysdevobj;//pPhysDevObj;
	PDEVICE_OBJECT	pfuncdevobj;//pFuncDevObj;
	PDEVICE_OBJECT	pnextdevobj;//pNextDevObj;

	u8	nextdevstacksz;//unsigned char NextDeviceStackSize;	//= (CHAR)CEdevice->pUsbDevObj->StackSize + 1; 

	//urb for control diescriptor request

#ifdef PLATFORM_OS_XP
	struct _URB_CONTROL_DESCRIPTOR_REQUEST descriptor_urb;
	PUSB_CONFIGURATION_DESCRIPTOR	pconfig_descriptor;//UsbConfigurationDescriptor;
#endif

#ifdef PLATFORM_OS_CE
	WCHAR			active_path[MAX_ACTIVE_REG_PATH];	// adapter regpath
	USB_EXTENSION	usb_extension;

	_nic_hdl		pipehdls_r8192c[0x10];
#endif

	u32	config_descriptor_len;//ULONG UsbConfigurationDescriptorLength;	
#endif//PLATFORM_WINDOWS

#ifdef PLATFORM_LINUX
	struct usb_interface *pusbintf;
	struct usb_device *pusbdev;
#endif//PLATFORM_LINUX

	ATOMIC_T continual_urb_error;
#endif//CONFIG_USB_HCI

/*-------- below is for PCIE INTERFACE --------*/
 
#ifdef CONFIG_PCI_HCI

#ifdef PLATFORM_LINUX
	struct pci_dev *ppcidev;

	//PCI MEM map
	unsigned long	pci_mem_end;	/* shared mem end	*/
	unsigned long	pci_mem_start;	/* shared mem start	*/
	
	//PCI IO map
	unsigned long	pci_base_addr;	/* device I/O address	*/

	//PciBridge
	struct pci_priv	pcipriv;

	u16	irqline;
	u8	irq_enabled;
	u8	irq_alloc;
	RT_ISR_CONTENT	isr_content;
	_lock	irq_th_lock;

	//ASPM
	u8	const_pci_aspm;
	u8	const_amdpci_aspm;
	u8	const_hwsw_rfoff_d3;
	u8	const_support_pciaspm;
	// pci-e bridge */
	u8 	const_hostpci_aspm_setting;
	// pci-e device */
	u8 	const_devicepci_aspm_setting;
	u8 	b_support_aspm; // If it supports ASPM, Offset[560h] = 0x40, otherwise Offset[560h] = 0x00. 
	u8	b_support_backdoor;
#endif//PLATFORM_LINUX

#endif//CONFIG_PCI_HCI
};

typedef enum _DRIVER_STATE{
	DRIVER_NORMAL = 0,
	DRIVER_DISAPPEAR = 1,
	DRIVER_REPLACE_DONGLE = 2,
}DRIVER_STATE;

struct _ADAPTER{	
	int	DriverState;// for disable driver using module, use dongle to replace module.
	int	pid[3];//process id from UI, 0:wps, 1:hostapd, 2:dhcpcd
	int	bDongle;//build-in module or external dongle
	u16 	chip_type;
	u16	HardwareType;
	u16	interface_type;//USB,SDIO,PCI
 
	struct 	dvobj_priv dvobjpriv;
	struct	mlme_priv mlmepriv;
	struct	mlme_ext_priv mlmeextpriv;
	struct	cmd_priv	cmdpriv;
	struct	evt_priv	evtpriv;
	//struct	io_queue	*pio_queue;
	struct 	io_priv	iopriv;
	struct	xmit_priv	xmitpriv;
	struct	recv_priv	recvpriv;
	struct	sta_priv	stapriv;
	struct	security_priv	securitypriv;	
	struct	registry_priv	registrypriv;
	struct	wlan_acl_pool	acl_list;
	struct	pwrctrl_priv	pwrctrlpriv;
	struct 	eeprom_priv eeprompriv;
	struct	led_priv	ledpriv;
	
#ifdef CONFIG_MP_INCLUDED
       struct	mp_priv	mppriv;
#endif

#ifdef CONFIG_DRVEXT_MODULE
	struct	drvext_priv	drvextpriv;
#endif
	
#ifdef CONFIG_AP_MODE
	struct	hostapd_priv	*phostapdpriv;		
#endif

#ifdef CONFIG_P2P
	struct wifidirect_info	wdinfo;
#endif //CONFIG_P2P

	PVOID			HalData;
	u32 hal_data_sz;
	struct hal_ops	HalFunc;

#ifdef CONFIG_BT_COEXIST
	//struct	btcoexist_priv	bt_coexist;
#endif
	s32	bDriverStopped; 
	s32	bSurpriseRemoved;
	s32  bCardDisableWOHSM;

	u32	IsrContent;
	u32	ImrContent;	

	u8	EepromAddressSize;
	u8	hw_init_completed;
	u8	init_adpt_in_progress;
	u8	bfirst_init;
	u8	bHaltInProgress;
	
	_thread_hdl_	cmdThread;
	_thread_hdl_	evtThread;
	_thread_hdl_	xmitThread;
	_thread_hdl_	recvThread;


	NDIS_STATUS (*dvobj_init)(_adapter * adapter);
	void (*dvobj_deinit)(_adapter * adapter);

	void (*intf_start)(_adapter * adapter);
	void (*intf_stop)(_adapter * adapter);

#ifdef PLATFORM_WINDOWS
	_nic_hdl		hndis_adapter;//hNdisAdapter(NDISMiniportAdapterHandle);
	_nic_hdl		hndis_config;//hNdisConfiguration;
	NDIS_STRING fw_img;

	u32	NdisPacketFilter;	
	u8	MCList[MAX_MCAST_LIST_NUM][6];
	u32	MCAddrCount;	
#endif //end of PLATFORM_WINDOWS


#ifdef PLATFORM_LINUX	
	_nic_hdl pnetdev;

	// used by rtw_rereg_nd_name related function
	struct rereg_nd_name_data {
		_nic_hdl old_pnetdev;
		char old_ifname[IFNAMSIZ];
		u8 old_ips_mode;
		u8 old_bRegUseLed;
	} rereg_nd_name_priv;
	
	int bup;
	struct net_device_stats stats;
	struct iw_statistics iwstats;
	struct proc_dir_entry *dir_dev;// for proc directory
#endif //end of PLATFORM_LINUX

	int net_closed;

	u8 bFWReady;
	u8 bReadPortCancel;
	u8 bWritePortCancel;
	u8 bRxRSSIDisplay;
#ifdef CONFIG_AUTOSUSPEND
	u8	bDisableAutosuspend;
#endif
#ifdef CONFIG_BR_EXT
	_lock					br_ext_lock;
	//unsigned int			macclone_completed;
	struct nat25_network_db_entry	*nethash[NAT25_HASH_SIZE];
	int				pppoe_connection_in_progress;
	unsigned char			pppoe_addr[MACADDRLEN];
	unsigned char			scdb_mac[MACADDRLEN];
	unsigned char			scdb_ip[4];
	struct nat25_network_db_entry	*scdb_entry;
	unsigned char			br_mac[MACADDRLEN];
	unsigned char			br_ip[4];
	
	struct br_ext_info		ethBrExtInfo;
#endif	// CONFIG_BR_EXT

};	
  
__inline static u8 *myid(struct eeprom_priv *peepriv)
{
	return (peepriv->mac_addr);
}


#endif //__DRV_TYPES_H__

