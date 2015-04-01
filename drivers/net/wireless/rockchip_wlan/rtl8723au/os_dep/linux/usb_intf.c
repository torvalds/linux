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
#define _HCI_INTF_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <hal_intf.h>
#include <rtw_version.h>
#ifndef CONFIG_USB_HCI

#error "CONFIG_USB_HCI shall be on!\n"

#endif

#include <usb_vendor_req.h>
#include <usb_ops.h>
#include <usb_osintf.h>
#include <usb_hal.h>
#ifdef CONFIG_PLATFORM_RTK_DMP
#include <asm/io.h>
#endif

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

#ifdef CONFIG_80211N_HT
extern int rtw_ht_enable;
extern int rtw_cbw40_enable;
extern int rtw_ampdu_enable;//for enable tx_ampdu
#endif

#ifdef CONFIG_GLOBAL_UI_PID
int ui_pid[3] = {0, 0, 0};
#endif


extern int pm_netdev_open(struct net_device *pnetdev,u8 bnormal);
static int rtw_suspend(struct usb_interface *intf, pm_message_t message);
static int rtw_resume(struct usb_interface *intf);
int rtw_resume_process(_adapter *padapter);


static int rtw_drv_init(struct usb_interface *pusb_intf,const struct usb_device_id *pdid);
static void rtw_dev_remove(struct usb_interface *pusb_intf);

static void rtw_dev_shutdown(struct device *dev)
{
	struct usb_interface *usb_intf = container_of(dev, struct usb_interface, dev);
	struct dvobj_priv *dvobj = NULL;
	_adapter *adapter = NULL;
	int i;

	DBG_871X("%s\n", __func__);

	if(usb_intf)
	{
		dvobj = usb_get_intfdata(usb_intf);
		if (dvobj)
		{
			for (i = 0; i<dvobj->iface_nums; i++)
			{
				adapter = dvobj->padapters[i];
				if (adapter)
				{
					adapter->bSurpriseRemoved = _TRUE;
				}
			}

			ATOMIC_SET(&dvobj->continual_io_error, MAX_CONTINUAL_IO_ERR+1);
		}
	}
}

#if (LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,23))
/* Some useful macros to use to create struct usb_device_id */
 #define USB_DEVICE_ID_MATCH_VENDOR 			 0x0001
 #define USB_DEVICE_ID_MATCH_PRODUCT			 0x0002
 #define USB_DEVICE_ID_MATCH_DEV_LO 			 0x0004
 #define USB_DEVICE_ID_MATCH_DEV_HI 			 0x0008
 #define USB_DEVICE_ID_MATCH_DEV_CLASS			 0x0010
 #define USB_DEVICE_ID_MATCH_DEV_SUBCLASS		 0x0020
 #define USB_DEVICE_ID_MATCH_DEV_PROTOCOL		 0x0040
 #define USB_DEVICE_ID_MATCH_INT_CLASS			 0x0080
 #define USB_DEVICE_ID_MATCH_INT_SUBCLASS		 0x0100
 #define USB_DEVICE_ID_MATCH_INT_PROTOCOL		 0x0200
 #define USB_DEVICE_ID_MATCH_INT_NUMBER 		 0x0400


#define USB_DEVICE_ID_MATCH_INT_INFO \
				 (USB_DEVICE_ID_MATCH_INT_CLASS | \
				 USB_DEVICE_ID_MATCH_INT_SUBCLASS | \
				 USB_DEVICE_ID_MATCH_INT_PROTOCOL)
				 

#define USB_DEVICE_AND_INTERFACE_INFO(vend, prod, cl, sc, pr) \
		 .match_flags = USB_DEVICE_ID_MATCH_INT_INFO \
				 | USB_DEVICE_ID_MATCH_DEVICE, \
		 .idVendor = (vend), \
		 .idProduct = (prod), \
		 .bInterfaceClass = (cl), \
		 .bInterfaceSubClass = (sc), \
		 .bInterfaceProtocol = (pr)
 
 /**
  * USB_VENDOR_AND_INTERFACE_INFO - describe a specific usb vendor with a class of usb interfaces
  * @vend: the 16 bit USB Vendor ID
  * @cl: bInterfaceClass value
  * @sc: bInterfaceSubClass value
  * @pr: bInterfaceProtocol value
  *
  * This macro is used to create a struct usb_device_id that matches a
  * specific vendor with a specific class of interfaces.
  *
  * This is especially useful when explicitly matching devices that have
  * vendor specific bDeviceClass values, but standards-compliant interfaces.
  */
#define USB_VENDOR_AND_INTERFACE_INFO(vend, cl, sc, pr) \
		 .match_flags = USB_DEVICE_ID_MATCH_INT_INFO \
				 | USB_DEVICE_ID_MATCH_VENDOR, \
		 .idVendor = (vend), \
		 .bInterfaceClass = (cl), \
		 .bInterfaceSubClass = (sc), \
		 .bInterfaceProtocol = (pr)
 
/* ----------------------------------------------------------------------- */
#endif

#define USB_VENDER_ID_REALTEK		0x0BDA

/* DID_USB_v916_20130116 */
#define RTL8192C_USB_IDS \
	/*=== Realtek demoboard ===*/ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8191)},/* Default ID */ \
	/****** 8188CUS ********/ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8176)},/* 8188cu 1*1 dongole */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8170)},/* 8188CE-VAU USB minCard */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817E)},/* 8188CE-VAU USB minCard */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817A)},/* 8188cu Slim Solo */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817B)},/* 8188cu Slim Combo */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817D)},/* 8188RU High-power USB Dongle */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8754)},/* 8188 Combo for BC4 */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817F)},/* 8188RU */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x818A)},/* RTL8188CUS-VL */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x018A)},/* RTL8188CTV */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x17C0)}, /* RTK demoboard - USB-N10E */ \
	/****** 8192CUS ********/ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8177)},/* 8191cu 1*2 */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8178)},/* 8192cu 2*2 */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x817C)},/* 8192CE-VAU USB minCard */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8191)},/* 8192CU 2*2 */ \
	{USB_DEVICE(0x1058, 0x0631)},/* Alpha, 8192CU */ \
	/*=== Customer ID ===*/	 \
	/****** 8188CUS Dongle ********/ \
	{USB_DEVICE(0x2019, 0xED17)},/* PCI - Edimax */ \
	{USB_DEVICE(0x0DF6, 0x0052)},/* Sitecom - Edimax */ \
	{USB_DEVICE(0x7392, 0x7811)},/* Edimax - Edimax */ \
	{USB_DEVICE(0x07B8, 0x8189)},/* Abocom - Abocom */ \
	{USB_DEVICE(0x0EB0, 0x9071)},/* NO Brand - Etop */ \
	{USB_DEVICE(0x06F8, 0xE033)},/* Hercules - Edimax */ \
	{USB_DEVICE(0x103C, 0x1629)},/* HP - Lite-On ,8188CUS Slim Combo */ \
	{USB_DEVICE(0x2001, 0x3308)},/* D-Link - Alpha */ \
	{USB_DEVICE(0x050D, 0x1102)},/* Belkin - Edimax */ \
	{USB_DEVICE(0x2019, 0xAB2A)},/* Planex - Abocom */ \
	{USB_DEVICE(0x20F4, 0x648B)},/* TRENDnet - Cameo */ \
	{USB_DEVICE(0x4855, 0x0090)},/*  - Feixun */ \
	{USB_DEVICE(0x13D3, 0x3357)},/*  - AzureWave */ \
	{USB_DEVICE(0x0DF6, 0x005C)},/* Sitecom - Edimax */ \
	{USB_DEVICE(0x0BDA, 0x5088)},/* Thinkware - CC&C */ \
	{USB_DEVICE(0x4856, 0x0091)},/* NetweeN - Feixun */ \
	{USB_DEVICE(0x0846, 0x9041)}, /* Netgear - Cameo */ \
	{USB_DEVICE(0x2019, 0x4902)},/* Planex - Etop */ \
	{USB_DEVICE(0x2019, 0xAB2E)},/* SW-WF02-AD15 -Abocom */ \
	{USB_DEVICE(0x2001, 0x330B)}, /* D-LINK - T&W */ \
	{USB_DEVICE(0xCDAB, 0x8010)}, /* - - compare */ \
	{USB_DEVICE(0x0B05, 0x17BA)}, /* ASUS - Edimax */ \
	{USB_DEVICE(0x0BDA, 0x1E1E)}, /* Intel - - */ \
	{USB_DEVICE(0x04BB, 0x094c)}, /* I-O DATA - Edimax */ \
	/****** 8188CTV ********/ \
	{USB_DEVICE(0xCDAB, 0x8011)}, /* - - compare */ \
	{USB_DEVICE(0x0BDA, 0x0A8A)}, /* Sony - Foxconn */ \
	/****** 8188 RU ********/ \
	{USB_DEVICE(0x0BDA, 0x317F)},/* Netcore,Netcore */ \
	/****** 8188CE-VAU ********/ \
	{USB_DEVICE(0x13D3, 0x3359)},/*  - Azwave */ \
	{USB_DEVICE(0x13D3, 0x3358)},/*  - Azwave */ \
	/****** 8188CUS Slim Solo********/ \
	{USB_DEVICE(0x04F2, 0xAFF7)},/* XAVI - XAVI */ \
	{USB_DEVICE(0x04F2, 0xAFF9)},/* XAVI - XAVI */ \
	{USB_DEVICE(0x04F2, 0xAFFA)},/* XAVI - XAVI */ \
	/****** 8188CUS Slim Combo ********/ \
	{USB_DEVICE(0x04F2, 0xAFF8)},/* XAVI - XAVI */ \
	{USB_DEVICE(0x04F2, 0xAFFB)},/* XAVI - XAVI */ \
	{USB_DEVICE(0x04F2, 0xAFFC)},/* XAVI - XAVI */ \
	{USB_DEVICE(0x2019, 0x1201)},/* Planex - Vencer */ \
	/****** 8192CUS Dongle ********/ \
	{USB_DEVICE(0x2001, 0x3307)},/* D-Link - Cameo */ \
	{USB_DEVICE(0x2001, 0x330A)},/* D-Link - Alpha */ \
	{USB_DEVICE(0x2001, 0x3309)},/* D-Link - Alpha */ \
	{USB_DEVICE(0x0586, 0x341F)},/* Zyxel - Abocom */ \
	{USB_DEVICE(0x7392, 0x7822)},/* Edimax - Edimax */ \
	{USB_DEVICE(0x2019, 0xAB2B)},/* Planex - Abocom */ \
	{USB_DEVICE(0x07B8, 0x8178)},/* Abocom - Abocom */ \
	{USB_DEVICE(0x07AA, 0x0056)},/* ATKK - Gemtek */ \
	{USB_DEVICE(0x4855, 0x0091)},/*  - Feixun */ \
	{USB_DEVICE(0x050D, 0x2102)},/* Belkin - Sercomm */ \
	{USB_DEVICE(0x050D, 0x2103)},/* Belkin - Edimax */ \
	{USB_DEVICE(0x20F4, 0x624D)},/* TRENDnet */ \
	{USB_DEVICE(0x0DF6, 0x0061)},/* Sitecom - Edimax */ \
	{USB_DEVICE(0x0B05, 0x17AB)},/* ASUS - Edimax */ \
	{USB_DEVICE(0x0846, 0x9021)},/* Netgear - Sercomm */ \
	{USB_DEVICE(0x0846, 0xF001)}, /* Netgear - Sercomm */ \
	{USB_DEVICE(0x0E66, 0x0019)},/* Hawking,Edimax */ \
	{USB_DEVICE(0x0E66, 0x0020)}, /* Hawking  - Edimax */ \
	{USB_DEVICE(0x050D, 0x1004)}, /* Belkin - Edimax */ \
	{USB_DEVICE(0x0BDA, 0x2E2E)}, /* Intel - - */ \
	{USB_DEVICE(0x2357, 0x0100)}, /* TP-Link - TP-Link */ \
	{USB_DEVICE(0x06F8, 0xE035)}, /* Hercules - Edimax */ \
	{USB_DEVICE(0x04BB, 0x0950)}, /* IO-DATA - Edimax */ \
	{USB_DEVICE(0x0DF6, 0x0070)}, /* Sitecom - Edimax */ \
	{USB_DEVICE(0x0789, 0x016D)}, /* LOGITEC - Edimax */ \
	/****** 8192CE-VAU  ********/ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8186)},/* Intel-Xavi( Azwave) */

#define RTL8192D_USB_IDS \
	/*=== Realtek demoboard ===*/ \
	/****** 8192DU ********/ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8193)},/* 8192DU-VC */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8194)},/* 8192DU-VS */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8111)},/* Realtek 5G dongle for WiFi Display */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x0193)},/* 8192DE-VAU */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8171)},/* 8192DU-VC */ \
	/*=== Customer ID ===*/ \
	/****** 8192DU-VC ********/ \
	{USB_DEVICE(0x2019, 0xAB2C)},/* PCI - Abocm */ \
	{USB_DEVICE(0x2019, 0x4903)},/* PCI - ETOP */ \
	{USB_DEVICE(0x2019, 0x4904)},/* PCI - ETOP */ \
	{USB_DEVICE(0x07B8, 0x8193)},/* Abocom - Abocom */ \
	/****** 8192DU-VS ********/ \
	{USB_DEVICE(0x20F4, 0x664B)}, /* TRENDnet - Cameo */ \
	{USB_DEVICE(0x04DD, 0x954F)},  /* Sharp */ \
	{USB_DEVICE(0x04DD, 0x96A6)},  /* Sharp */ \
	{USB_DEVICE(0x050D, 0x110A)}, /* Belkin - Edimax */ \
	{USB_DEVICE(0x050D, 0x1105)}, /* Belkin - Edimax */ \
	{USB_DEVICE(0x050D, 0x120A)}, /* Belkin - Edimax */ \
	{USB_DEVICE(0x1668, 0x8102)}, /*  -  */ \
	{USB_DEVICE(0x0BDA, 0xE194)}, /*  - Edimax */ \
	/****** 8192DU-WiFi Display Dongle ********/ \
	{USB_DEVICE(0x2019, 0xAB2D)},/* Planex - Abocom ,5G dongle for WiFi Display */

#define RTL8723A_USB_IDS \
	{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDER_ID_REALTEK, 0x8724,0xff,0xff,0xff)}, /* 8723AU 1*1 */ \
	{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDER_ID_REALTEK, 0x1724,0xff,0xff,0xff)}, /* 8723AU 1*1 */ \
	{USB_DEVICE_AND_INTERFACE_INFO(USB_VENDER_ID_REALTEK, 0x0724,0xff,0xff,0xff)}, /* 8723AU 1*1 */
	
#define RTL8188E_USB_IDS \
	/*=== Realtek demoboard ===*/ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x8179)}, /* 8188EUS */ \
	{USB_DEVICE(USB_VENDER_ID_REALTEK, 0x0179)}, /* 8188ETV */ \
	/*=== Customer ID ===*/ \
	/****** 8188EUS ********/ \
	{USB_DEVICE(0x07B8, 0x8179)}, /* Abocom - Abocom */

#ifndef CONFIG_RTL8192C
	#undef RTL8192C_USB_IDS
	#define RTL8192C_USB_IDS
#endif
#ifndef CONFIG_RTL8192D
	#undef RTL8192D_USB_IDS
	#define RTL8192D_USB_IDS
#endif
#ifndef CONFIG_RTL8723A
	#undef RTL8723A_USB_IDS
	#define RTL8723A_USB_IDS
#endif
#ifndef CONFIG_RTL8188E
	#undef RTL8188E_USB_IDS
	#define RTL8188E_USB_IDS
#endif

static struct usb_device_id rtw_usb_id_tbl[] ={
	RTL8192C_USB_IDS
	RTL8192D_USB_IDS
	RTL8723A_USB_IDS
	RTL8188E_USB_IDS
	{}	/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, rtw_usb_id_tbl);

int const rtw_usb_id_len = sizeof(rtw_usb_id_tbl) / sizeof(struct usb_device_id);

static struct specific_device_id specific_device_id_tbl[] = {
	{.idVendor=USB_VENDER_ID_REALTEK, .idProduct=0x8177, .flags=SPEC_DEV_ID_DISABLE_HT},//8188cu 1*1 dongole, (b/g mode only)
	{.idVendor=USB_VENDER_ID_REALTEK, .idProduct=0x817E, .flags=SPEC_DEV_ID_DISABLE_HT},//8188CE-VAU USB minCard (b/g mode only)
	{.idVendor=0x0b05, .idProduct=0x1791, .flags=SPEC_DEV_ID_DISABLE_HT},
	{.idVendor=0x13D3, .idProduct=0x3311, .flags=SPEC_DEV_ID_DISABLE_HT},
	{.idVendor=0x13D3, .idProduct=0x3359, .flags=SPEC_DEV_ID_DISABLE_HT},//Russian customer -Azwave (8188CE-VAU  g mode)	
#ifdef RTK_DMP_PLATFORM
	{.idVendor=USB_VENDER_ID_REALTEK, .idProduct=0x8111, .flags=SPEC_DEV_ID_ASSIGN_IFNAME}, // Realtek 5G dongle for WiFi Display
	{.idVendor=0x2019, .idProduct=0xAB2D, .flags=SPEC_DEV_ID_ASSIGN_IFNAME}, // PCI-Abocom 5G dongle for WiFi Display
#endif /* RTK_DMP_PLATFORM */
	{}
};

struct rtw_usb_drv {
	struct usb_driver usbdrv;
	int drv_registered;
};

#ifdef CONFIG_RTL8192C
static struct usb_device_id rtl8192c_usb_id_tbl[] ={
	RTL8192C_USB_IDS
	{}	/* Terminating entry */
};

struct rtw_usb_drv rtl8192c_usb_drv = {
	.usbdrv.name = (char*)"rtl8192cu",
	.usbdrv.probe = rtw_drv_init,
	.usbdrv.disconnect = rtw_dev_remove,
	.usbdrv.id_table = rtl8192c_usb_id_tbl,
	.usbdrv.suspend =  rtw_suspend,
	.usbdrv.resume = rtw_resume,
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 22))
  	.usbdrv.reset_resume   = rtw_resume,
	#endif
	#ifdef CONFIG_AUTOSUSPEND	
	.usbdrv.supports_autosuspend = 1,	
	#endif

	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19))
	.usbdrv.drvwrap.driver.shutdown = rtw_dev_shutdown,
	#else
	.usbdrv.driver.shutdown = rtw_dev_shutdown,
	#endif
};

static struct rtw_usb_drv *usb_drv = &rtl8192c_usb_drv;
#endif /* CONFIG_RTL8192C */

#ifdef CONFIG_RTL8192D
static struct usb_device_id rtl8192d_usb_id_tbl[] ={
	RTL8192D_USB_IDS
	{}	/* Terminating entry */
};

struct rtw_usb_drv rtl8192d_usb_drv = {
	.usbdrv.name = (char*)"rtl8192du",
	.usbdrv.probe = rtw_drv_init,
	.usbdrv.disconnect = rtw_dev_remove,
	.usbdrv.id_table = rtl8192d_usb_id_tbl,
	.usbdrv.suspend =  rtw_suspend,
	.usbdrv.resume = rtw_resume,
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 22))
  	.usbdrv.reset_resume   = rtw_resume,
	#endif
	#ifdef CONFIG_AUTOSUSPEND	
	.usbdrv.supports_autosuspend = 1,	
	#endif

	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19))
	.usbdrv.drvwrap.driver.shutdown = rtw_dev_shutdown,
	#else
	.usbdrv.driver.shutdown = rtw_dev_shutdown,
	#endif
};
static struct rtw_usb_drv *usb_drv = &rtl8192d_usb_drv;
#endif /* CONFIG_RTL8192D */

#ifdef CONFIG_RTL8723A
static struct usb_device_id rtl8723a_usb_id_tbl[] ={
	RTL8723A_USB_IDS
	{}	/* Terminating entry */
};

struct rtw_usb_drv rtl8723a_usb_drv = {
	.usbdrv.name = (char*)"rtl8723au",
	.usbdrv.probe = rtw_drv_init,
	.usbdrv.disconnect = rtw_dev_remove,
	.usbdrv.id_table = rtl8723a_usb_id_tbl,
	.usbdrv.suspend =  rtw_suspend,
	.usbdrv.resume = rtw_resume,
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 22))
  	.usbdrv.reset_resume   = rtw_resume,
	#endif
	#ifdef CONFIG_AUTOSUSPEND	
	.usbdrv.supports_autosuspend = 1,	
	#endif

	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19))
	.usbdrv.drvwrap.driver.shutdown = rtw_dev_shutdown,
	#else
	.usbdrv.driver.shutdown = rtw_dev_shutdown,
	#endif
};

static struct rtw_usb_drv *usb_drv = &rtl8723a_usb_drv;
#endif /* CONFIG_RTL8723A */

#ifdef CONFIG_RTL8188E
static struct usb_device_id rtl8188e_usb_id_tbl[] ={
	RTL8188E_USB_IDS
	{}	/* Terminating entry */
};

struct rtw_usb_drv rtl8188e_usb_drv = {
	.usbdrv.name = (char*)"rtl8188eu",
	.usbdrv.probe = rtw_drv_init,
	.usbdrv.disconnect = rtw_dev_remove,
	.usbdrv.id_table = rtl8188e_usb_id_tbl,
	.usbdrv.suspend =  rtw_suspend,
	.usbdrv.resume = rtw_resume,
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 22))
  	.usbdrv.reset_resume   = rtw_resume,
	#endif
	#ifdef CONFIG_AUTOSUSPEND	
	.usbdrv.supports_autosuspend = 1,	
	#endif

	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 19))
	.usbdrv.drvwrap.driver.shutdown = rtw_dev_shutdown,
	#else
	.usbdrv.driver.shutdown = rtw_dev_shutdown,
	#endif
};

static struct rtw_usb_drv *usb_drv = &rtl8188e_usb_drv;
#endif /* CONFIG_RTL8188E */

static inline int RT_usb_endpoint_dir_in(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN);
}

static inline int RT_usb_endpoint_dir_out(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT);
}

static inline int RT_usb_endpoint_xfer_int(const struct usb_endpoint_descriptor *epd)
{
	return ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT);
}

static inline int RT_usb_endpoint_xfer_bulk(const struct usb_endpoint_descriptor *epd)
{
 	return ((epd->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK);
}

static inline int RT_usb_endpoint_is_bulk_in(const struct usb_endpoint_descriptor *epd)
{
	return (RT_usb_endpoint_xfer_bulk(epd) && RT_usb_endpoint_dir_in(epd));
}

static inline int RT_usb_endpoint_is_bulk_out(const struct usb_endpoint_descriptor *epd)
{
	return (RT_usb_endpoint_xfer_bulk(epd) && RT_usb_endpoint_dir_out(epd));
}

static inline int RT_usb_endpoint_is_int_in(const struct usb_endpoint_descriptor *epd)
{
	return (RT_usb_endpoint_xfer_int(epd) && RT_usb_endpoint_dir_in(epd));
}

static inline int RT_usb_endpoint_num(const struct usb_endpoint_descriptor *epd)
{
	return epd->bEndpointAddress & USB_ENDPOINT_NUMBER_MASK;
}

static u8 rtw_init_intf_priv(struct dvobj_priv *dvobj)
{
	u8 rst = _SUCCESS; 
	
	#ifdef CONFIG_USB_VENDOR_REQ_MUTEX
	_rtw_mutex_init(&dvobj->usb_vendor_req_mutex);
	#endif


	#ifdef CONFIG_USB_VENDOR_REQ_BUFFER_PREALLOC
	dvobj->usb_alloc_vendor_req_buf = rtw_zmalloc(MAX_USB_IO_CTL_SIZE);
	if (dvobj->usb_alloc_vendor_req_buf == NULL) {
		DBG_871X("alloc usb_vendor_req_buf failed... /n");
		rst = _FAIL;
		goto exit;
	}
	dvobj->usb_vendor_req_buf  = 
		(u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(dvobj->usb_alloc_vendor_req_buf ), ALIGNMENT_UNIT);
exit:
	#endif

	return rst;
	
}

static u8 rtw_deinit_intf_priv(struct dvobj_priv *dvobj)
{
	u8 rst = _SUCCESS; 

	#ifdef CONFIG_USB_VENDOR_REQ_BUFFER_PREALLOC
	if(dvobj->usb_vendor_req_buf)
		rtw_mfree(dvobj->usb_alloc_vendor_req_buf, MAX_USB_IO_CTL_SIZE);
	#endif

	#ifdef CONFIG_USB_VENDOR_REQ_MUTEX
	_rtw_mutex_free(&dvobj->usb_vendor_req_mutex);
	#endif
	
	return rst;
}

static struct dvobj_priv *usb_dvobj_init(struct usb_interface *usb_intf)
{
	int	i;
	u8	val8;
	int	status = _FAIL;
	struct dvobj_priv *pdvobjpriv;
	struct usb_device_descriptor 	*pdev_desc;
	struct usb_host_config			*phost_conf;
	struct usb_config_descriptor		*pconf_desc;
	struct usb_host_interface		*phost_iface;
	struct usb_interface_descriptor	*piface_desc;
	struct usb_host_endpoint		*phost_endp;
	struct usb_endpoint_descriptor	*pendp_desc;
	struct usb_device				*pusbd;

_func_enter_;

	if ((pdvobjpriv = (struct dvobj_priv*)rtw_zmalloc(sizeof(*pdvobjpriv))) == NULL) {
		goto exit;
	}

	_rtw_mutex_init(&pdvobjpriv->hw_init_mutex);
	_rtw_mutex_init(&pdvobjpriv->h2c_fwcmd_mutex);
	_rtw_mutex_init(&pdvobjpriv->setch_mutex);
	_rtw_mutex_init(&pdvobjpriv->setbw_mutex);
	pdvobjpriv->processing_dev_remove = _FALSE;

	_rtw_spinlock_init(&pdvobjpriv->lock);
	pdvobjpriv->macid[1] = _TRUE; //macid=1 for bc/mc stainfo
	_rtw_spinlock_init(&pdvobjpriv->cam_ctl.lock);

	pdvobjpriv->pusbintf = usb_intf ;
	pusbd = pdvobjpriv->pusbdev = interface_to_usbdev(usb_intf);
	usb_set_intfdata(usb_intf, pdvobjpriv);

	pdvobjpriv->RtNumInPipes = 0;
	pdvobjpriv->RtNumOutPipes = 0;

	//padapter->EepromAddressSize = 6;
	//pdvobjpriv->nr_endpoint = 6;

	pdev_desc = &pusbd->descriptor;

#if 0
	DBG_871X("\n8712_usb_device_descriptor:\n");
	DBG_871X("bLength=%x\n", pdev_desc->bLength);
	DBG_871X("bDescriptorType=%x\n", pdev_desc->bDescriptorType);
	DBG_871X("bcdUSB=%x\n", pdev_desc->bcdUSB);
	DBG_871X("bDeviceClass=%x\n", pdev_desc->bDeviceClass);
	DBG_871X("bDeviceSubClass=%x\n", pdev_desc->bDeviceSubClass);
	DBG_871X("bDeviceProtocol=%x\n", pdev_desc->bDeviceProtocol);
	DBG_871X("bMaxPacketSize0=%x\n", pdev_desc->bMaxPacketSize0);
	DBG_871X("idVendor=%x\n", pdev_desc->idVendor);
	DBG_871X("idProduct=%x\n", pdev_desc->idProduct);
	DBG_871X("bcdDevice=%x\n", pdev_desc->bcdDevice);
	DBG_871X("iManufacturer=%x\n", pdev_desc->iManufacturer);
	DBG_871X("iProduct=%x\n", pdev_desc->iProduct);
	DBG_871X("iSerialNumber=%x\n", pdev_desc->iSerialNumber);
	DBG_871X("bNumConfigurations=%x\n", pdev_desc->bNumConfigurations);
#endif

	phost_conf = pusbd->actconfig;
	pconf_desc = &phost_conf->desc;

#if 0
	DBG_871X("\n8712_usb_configuration_descriptor:\n");
	DBG_871X("bLength=%x\n", pconf_desc->bLength);
	DBG_871X("bDescriptorType=%x\n", pconf_desc->bDescriptorType);
	DBG_871X("wTotalLength=%x\n", pconf_desc->wTotalLength);
	DBG_871X("bNumInterfaces=%x\n", pconf_desc->bNumInterfaces);
	DBG_871X("bConfigurationValue=%x\n", pconf_desc->bConfigurationValue);
	DBG_871X("iConfiguration=%x\n", pconf_desc->iConfiguration);
	DBG_871X("bmAttributes=%x\n", pconf_desc->bmAttributes);
	DBG_871X("bMaxPower=%x\n", pconf_desc->bMaxPower);
#endif

	//DBG_871X("\n/****** num of altsetting = (%d) ******/\n", pusb_interface->num_altsetting);

	phost_iface = &usb_intf->altsetting[0];
	piface_desc = &phost_iface->desc;

#if 0
	DBG_871X("\n8712_usb_interface_descriptor:\n");
	DBG_871X("bLength=%x\n", piface_desc->bLength);
	DBG_871X("bDescriptorType=%x\n", piface_desc->bDescriptorType);
	DBG_871X("bInterfaceNumber=%x\n", piface_desc->bInterfaceNumber);
	DBG_871X("bAlternateSetting=%x\n", piface_desc->bAlternateSetting);
	DBG_871X("bNumEndpoints=%x\n", piface_desc->bNumEndpoints);
	DBG_871X("bInterfaceClass=%x\n", piface_desc->bInterfaceClass);
	DBG_871X("bInterfaceSubClass=%x\n", piface_desc->bInterfaceSubClass);
	DBG_871X("bInterfaceProtocol=%x\n", piface_desc->bInterfaceProtocol);
	DBG_871X("iInterface=%x\n", piface_desc->iInterface);
#endif

	pdvobjpriv->NumInterfaces = pconf_desc->bNumInterfaces;
	pdvobjpriv->InterfaceNumber = piface_desc->bInterfaceNumber;
	pdvobjpriv->nr_endpoint = piface_desc->bNumEndpoints;

	//DBG_871X("\ndump usb_endpoint_descriptor:\n");

	for (i = 0; i < pdvobjpriv->nr_endpoint; i++)
	{
		phost_endp = phost_iface->endpoint + i;
		if (phost_endp)
		{
			pendp_desc = &phost_endp->desc;

			DBG_871X("\nusb_endpoint_descriptor(%d):\n", i);
			DBG_871X("bLength=%x\n",pendp_desc->bLength);
			DBG_871X("bDescriptorType=%x\n",pendp_desc->bDescriptorType);
			DBG_871X("bEndpointAddress=%x\n",pendp_desc->bEndpointAddress);
			//DBG_871X("bmAttributes=%x\n",pendp_desc->bmAttributes);
			DBG_871X("wMaxPacketSize=%d\n",le16_to_cpu(pendp_desc->wMaxPacketSize));
			DBG_871X("bInterval=%x\n",pendp_desc->bInterval);
			//DBG_871X("bRefresh=%x\n",pendp_desc->bRefresh);
			//DBG_871X("bSynchAddress=%x\n",pendp_desc->bSynchAddress);

			if (RT_usb_endpoint_is_bulk_in(pendp_desc))
			{
				DBG_871X("RT_usb_endpoint_is_bulk_in = %x\n", RT_usb_endpoint_num(pendp_desc));
				pdvobjpriv->RtInPipe[pdvobjpriv->RtNumInPipes] = RT_usb_endpoint_num(pendp_desc);
				pdvobjpriv->RtNumInPipes++;
			}
			else if (RT_usb_endpoint_is_int_in(pendp_desc))
			{
				DBG_871X("RT_usb_endpoint_is_int_in = %x, Interval = %x\n", RT_usb_endpoint_num(pendp_desc),pendp_desc->bInterval);
				pdvobjpriv->RtInPipe[pdvobjpriv->RtNumInPipes] = RT_usb_endpoint_num(pendp_desc);
				pdvobjpriv->RtNumInPipes++;
			}
			else if (RT_usb_endpoint_is_bulk_out(pendp_desc))
			{
				DBG_871X("RT_usb_endpoint_is_bulk_out = %x\n", RT_usb_endpoint_num(pendp_desc));
				pdvobjpriv->RtOutPipe[pdvobjpriv->RtNumOutPipes] = RT_usb_endpoint_num(pendp_desc);
				pdvobjpriv->RtNumOutPipes++;
			}
			pdvobjpriv->ep_num[i] = RT_usb_endpoint_num(pendp_desc);
		}
	}
	
	DBG_871X("nr_endpoint=%d, in_num=%d, out_num=%d\n\n", pdvobjpriv->nr_endpoint, pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);

	if (pusbd->speed == USB_SPEED_HIGH) {
		pdvobjpriv->ishighspeed = _TRUE;
		DBG_871X("USB_SPEED_HIGH\n");
	} else {
		pdvobjpriv->ishighspeed = _FALSE;
		DBG_871X("NON USB_SPEED_HIGH\n");
	}
	
	if (rtw_init_intf_priv(pdvobjpriv) == _FAIL) {
		RT_TRACE(_module_os_intfs_c_,_drv_err_,("\n Can't INIT rtw_init_intf_priv\n"));
		goto free_dvobj;
	}

	//.3 misc
	_rtw_init_sema(&(pdvobjpriv->usb_suspend_sema), 0);	
	rtw_reset_continual_io_error(pdvobjpriv);

	usb_get_dev(pusbd);

	status = _SUCCESS;

free_dvobj:
	if (status != _SUCCESS && pdvobjpriv) {
		usb_set_intfdata(usb_intf, NULL);
		_rtw_spinlock_free(&pdvobjpriv->lock);
		_rtw_mutex_free(&pdvobjpriv->hw_init_mutex);
		_rtw_mutex_free(&pdvobjpriv->h2c_fwcmd_mutex);
		_rtw_mutex_free(&pdvobjpriv->setch_mutex);
		_rtw_mutex_free(&pdvobjpriv->setbw_mutex);
		_rtw_spinlock_free(&pdvobjpriv->cam_ctl.lock);
		rtw_mfree((u8*)pdvobjpriv, sizeof(*pdvobjpriv));
		pdvobjpriv = NULL;
	}
exit:
_func_exit_;
	return pdvobjpriv;
}

static void usb_dvobj_deinit(struct usb_interface *usb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(usb_intf);

_func_enter_;

	usb_set_intfdata(usb_intf, NULL);
	if (dvobj) {
		//Modify condition for 92DU DMDP 2010.11.18, by Thomas
		if ((dvobj->NumInterfaces != 2 && dvobj->NumInterfaces != 3)
			|| (dvobj->InterfaceNumber == 1)) {
			if (interface_to_usbdev(usb_intf)->state != USB_STATE_NOTATTACHED) {
				//If we didn't unplug usb dongle and remove/insert modlue, driver fails on sitesurvey for the first time when device is up . 
				//Reset usb port for sitesurvey fail issue. 2009.8.13, by Thomas
				DBG_871X("usb attached..., try to reset usb device\n");
				usb_reset_device(interface_to_usbdev(usb_intf));
			}
		}
		rtw_deinit_intf_priv(dvobj);
		_rtw_spinlock_free(&dvobj->lock);
		_rtw_mutex_free(&dvobj->hw_init_mutex);
		_rtw_mutex_free(&dvobj->h2c_fwcmd_mutex);
		_rtw_mutex_free(&dvobj->setch_mutex);
		_rtw_mutex_free(&dvobj->setbw_mutex);
		_rtw_spinlock_free(&dvobj->cam_ctl.lock);
		rtw_mfree((u8*)dvobj, sizeof(*dvobj));
	}
	
	//DBG_871X("%s %d\n", __func__, ATOMIC_READ(&usb_intf->dev.kobj.kref.refcount));
	usb_put_dev(interface_to_usbdev(usb_intf));

_func_exit_;
}

static void decide_chip_type_by_usb_device_id(_adapter *padapter, const struct usb_device_id *pdid)
{
	padapter->chip_type = NULL_CHIP_TYPE;
	hal_set_hw_type(padapter);
}

static void usb_intf_start(_adapter *padapter)
{

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+usb_intf_start\n"));	
	
	rtw_hal_inirp_init(padapter);

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-usb_intf_start\n"));

}

static void usb_intf_stop(_adapter *padapter)
{

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+usb_intf_stop\n"));

	//disabel_hw_interrupt
	if(padapter->bSurpriseRemoved == _FALSE)
	{
		//device still exists, so driver can do i/o operation
		//TODO:
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("SurpriseRemoved==_FALSE\n"));
	}

	//cancel in irp
	rtw_hal_inirp_deinit(padapter);	

	//cancel out irp
	rtw_write_port_cancel(padapter);

	//todo:cancel other irps

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-usb_intf_stop\n"));

}

void rtw_dev_unload(_adapter *padapter)
{
	struct net_device *pnetdev= (struct net_device*)padapter->pnetdev;
	u8 val8;
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+rtw_dev_unload\n"));

	if(padapter->bup == _TRUE)
	{
		DBG_871X("===> rtw_dev_unload\n");

		padapter->bDriverStopped = _TRUE;
		#ifdef CONFIG_XMIT_ACK
		if (padapter->xmitpriv.ack_tx)
			rtw_ack_tx_done(&padapter->xmitpriv, RTW_SCTX_DONE_DRV_STOP);
		#endif

		//s3.
		if(padapter->intf_stop)
		{
			padapter->intf_stop(padapter);
		}

		//s4.
		if(!adapter_to_pwrctl(padapter)->bInternalAutoSuspend )			
		rtw_stop_drv_threads(padapter);


		//s5.
		if(padapter->bSurpriseRemoved == _FALSE)
		{
			//DBG_871X("r871x_dev_unload()->rtl871x_hal_deinit()\n");
#ifdef CONFIG_WOWLAN
			if((adapter_to_pwrctl(padapter)->bSupportRemoteWakeup==_TRUE)&&(adapter_to_pwrctl(padapter)->wowlan_mode==_TRUE)){
				DBG_871X("%s bSupportWakeOnWlan==_TRUE  do not run rtw_hal_deinit()\n",__FUNCTION__);
			}
			else
#endif //CONFIG_WOWLAN
			{
				rtw_hal_deinit(padapter);
			}
			padapter->bSurpriseRemoved = _TRUE;
		}

		padapter->bup = _FALSE;
#ifdef CONFIG_WOWLAN
		padapter->hw_init_completed=_FALSE;
#endif //CONFIG_WOWLAN
	}
	else
	{
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("r871x_dev_unload():padapter->bup == _FALSE\n" ));
	}

	DBG_871X("<=== rtw_dev_unload\n");

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-rtw_dev_unload\n"));

}

static void process_spec_devid(const struct usb_device_id *pdid)
{
	u16 vid, pid;
	u32 flags;
	int i;
	int num = sizeof(specific_device_id_tbl)/sizeof(struct specific_device_id);

	for(i=0; i<num; i++)
	{
		vid = specific_device_id_tbl[i].idVendor;
		pid = specific_device_id_tbl[i].idProduct;
		flags = specific_device_id_tbl[i].flags;

#ifdef CONFIG_80211N_HT
		if((pdid->idVendor==vid) && (pdid->idProduct==pid) && (flags&SPEC_DEV_ID_DISABLE_HT))
		{
			 rtw_ht_enable = 0;
			 rtw_cbw40_enable = 0;
			 rtw_ampdu_enable = 0;
		}
#endif

#ifdef RTK_DMP_PLATFORM
		// Change the ifname to wlan10 when PC side WFD dongle plugin on DMP platform.
		// It is used to distinguish between normal and PC-side wifi dongle/module.
		if((pdid->idVendor==vid) && (pdid->idProduct==pid) && (flags&SPEC_DEV_ID_ASSIGN_IFNAME))
		{
			extern char* ifname;
			strncpy(ifname, "wlan10", 6); 
			//DBG_871X("%s()-%d: ifname=%s, vid=%04X, pid=%04X\n", __FUNCTION__, __LINE__, ifname, vid, pid);
		}
#endif /* RTK_DMP_PLATFORM */

	}
}

#ifdef SUPPORT_HW_RFOFF_DETECTED
int rtw_hw_suspend(_adapter *padapter )
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct usb_interface *pusb_intf = adapter_to_dvobj(padapter)->pusbintf;	
	struct net_device *pnetdev = padapter->pnetdev;
	
	_func_enter_;

	if((!padapter->bup) || (padapter->bDriverStopped)||(padapter->bSurpriseRemoved))
	{
		DBG_871X("padapter->bup=%d bDriverStopped=%d bSurpriseRemoved = %d\n",
			padapter->bup, padapter->bDriverStopped,padapter->bSurpriseRemoved);		
		goto error_exit;
	}
	
	if(padapter)//system suspend
	{		
		LeaveAllPowerSaveMode(padapter);
		
		DBG_871X("==> rtw_hw_suspend\n");	
		_enter_pwrlock(&pwrpriv->lock);
		pwrpriv->bips_processing = _TRUE;
		//padapter->net_closed = _TRUE;
		//s1.
		if(pnetdev)
		{
			netif_carrier_off(pnetdev);
			rtw_netif_stop_queue(pnetdev);
		}

		//s2.
		rtw_disassoc_cmd(padapter, 500, _FALSE);
		
		//s2-2.  indicate disconnect to os
		//rtw_indicate_disconnect(padapter);
		{
			struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;		

			if(check_fwstate(pmlmepriv, _FW_LINKED))
			{
				_clr_fwstate_(pmlmepriv, _FW_LINKED);

				rtw_led_control(padapter, LED_CTL_NO_LINK);

				rtw_os_indicate_disconnect(padapter);
				
				#ifdef CONFIG_LPS
				//donnot enqueue cmd
				rtw_lps_ctrl_wk_cmd(padapter, LPS_CTRL_DISCONNECT, 0);
				#endif
			}

		}
		//s2-3.
		rtw_free_assoc_resources(padapter, 1);

		//s2-4.
		rtw_free_network_queue(padapter,_TRUE);
		#ifdef CONFIG_IPS
		rtw_ips_dev_unload(padapter);			
		#endif 
		pwrpriv->rf_pwrstate = rf_off;
		pwrpriv->bips_processing = _FALSE;		

		_exit_pwrlock(&pwrpriv->lock);
	}
	else
		goto error_exit;
	
	_func_exit_;
	return 0;
	
error_exit:
	DBG_871X("%s, failed \n",__FUNCTION__);
	return (-1);

}

int rtw_hw_resume(_adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct usb_interface *pusb_intf = adapter_to_dvobj(padapter)->pusbintf;
	struct net_device *pnetdev = padapter->pnetdev;

	_func_enter_;

	if(padapter)//system resume
	{	
		DBG_871X("==> rtw_hw_resume\n");
		_enter_pwrlock(&pwrpriv->lock);
		pwrpriv->bips_processing = _TRUE;
		rtw_reset_drv_sw(padapter);
	
		if(pm_netdev_open(pnetdev,_FALSE) != 0)
		{
			_exit_pwrlock(&pwrpriv->lock);
			goto error_exit;
		}

		netif_device_attach(pnetdev);	
		netif_carrier_on(pnetdev);

		if(!rtw_netif_queue_stopped(pnetdev))
      			rtw_netif_start_queue(pnetdev);
		else
			rtw_netif_wake_queue(pnetdev);
		
		pwrpriv->bkeepfwalive = _FALSE;
		pwrpriv->brfoffbyhw = _FALSE;
		
		pwrpriv->rf_pwrstate = rf_on;
		pwrpriv->bips_processing = _FALSE;	
	
		_exit_pwrlock(&pwrpriv->lock);
	}
	else
	{
		goto error_exit;	
	}

	_func_exit_;
	
	return 0;
error_exit:
	DBG_871X("%s, Open net dev failed \n",__FUNCTION__);
	return (-1);
}
#endif

#if 1
#ifdef CONFIG_WOWLAN
static void rtw_suspend_wow(_adapter *padapter)
{
	struct net_device *pnetdev = padapter->pnetdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct wifidirect_info*	pwdinfo = &padapter->wdinfo;
	struct wowlan_ioctl_param poidparam;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		pwrpriv->wowlan_mode = _TRUE;
	else
		pwrpriv->wowlan_mode = _FALSE;
	
	rtw_cancel_all_timer(padapter);		
	
	//padapter->net_closed = _TRUE;
	//s1.
	if(pnetdev)
	{
		netif_carrier_off(pnetdev);
		rtw_netif_stop_queue(pnetdev);
	}

	if(pwrpriv->bSupportRemoteWakeup==_TRUE && pwrpriv->wowlan_mode==_TRUE){
		//set H2C command
		poidparam.subcode=WOWLAN_ENABLE;
		padapter->HalFunc.SetHwRegHandler(padapter,HW_VAR_WOWLAN,(u8 *)&poidparam);
	}
	else
	{
		//s2.
		rtw_disassoc_cmd(padapter, 0, _FALSE);
	}


#ifdef CONFIG_LAYER2_ROAMING_RESUME
	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED)&& rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
	{
		//DBG_871X("%s:%d assoc_ssid:%s\n", __FUNCTION__, __LINE__, pmlmepriv->assoc_ssid.Ssid);
		DBG_871X("%s:%d %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n",__FUNCTION__, __LINE__,
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
#ifdef CONFIG_AUTOSUSPEND
	if(!pwrpriv->bInternalAutoSuspend )
#endif
	//s2-4.
	rtw_free_network_queue(padapter, _TRUE);

	rtw_dev_unload(padapter);
	
	if(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_indicate_scan_done(padapter, 1);

	//if(check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
	//	rtw_indicate_disconnect(padapter);

}
#endif
static int rtw_suspend(struct usb_interface *pusb_intf, pm_message_t message)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	_adapter *padapter = dvobj->if1;
	struct net_device *pnetdev = padapter->pnetdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
	struct usb_device *usb_dev = interface_to_usbdev(pusb_intf);


	int ret = 0;
	u32 start_time = rtw_get_current_time();
	
	_func_enter_;

	DBG_871X("==> %s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);


#ifdef CONFIG_IOL_READ_EFUSE_MAP
	if(!padapter->bup){
		u8 bMacPwrCtrlOn = _FALSE;
		rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
		if(bMacPwrCtrlOn)
			rtw_hal_power_off(padapter);
	}
#endif

	if((!padapter->bup) || (padapter->bDriverStopped)||(padapter->bSurpriseRemoved))
	{
		DBG_871X("padapter->bup=%d bDriverStopped=%d bSurpriseRemoved = %d\n",
			padapter->bup, padapter->bDriverStopped,padapter->bSurpriseRemoved);
		goto exit;
	}
	
	if(pwrpriv->bInternalAutoSuspend )
	{
	#ifdef CONFIG_AUTOSUSPEND	
	#ifdef SUPPORT_HW_RFOFF_DETECTED
		// The FW command register update must after MAC and FW init ready.
		if((padapter->bFWReady) && (pwrpriv->bHWPwrPindetect ) && (padapter->registrypriv.usbss_enable ))
		{
			u8 bOpen = _TRUE;
			rtw_interface_ps_func(padapter,HAL_USB_SELECT_SUSPEND,&bOpen);
			//rtl8192c_set_FwSelectSuspend_cmd(padapter,_TRUE ,500);//note fw to support hw power down ping detect
		}
	#endif
	#endif
	}

	pwrpriv->bInSuspend = _TRUE;		

	LeaveAllPowerSaveMode(padapter);

	rtw_stop_cmd_thread(padapter);

	_enter_pwrlock(&pwrpriv->lock);
#ifdef CONFIG_WOWLAN
	rtw_suspend_wow(padapter);
#else
	rtw_suspend_common(padapter);	
#endif
	
#ifdef CONFIG_AUTOSUSPEND
	pwrpriv->rf_pwrstate = rf_off;
	pwrpriv->bips_processing = _FALSE;
#endif
	_exit_pwrlock(&pwrpriv->lock);

	
exit:
	DBG_871X("<===  %s return %d.............. in %dms\n", __FUNCTION__
		, ret, rtw_get_passing_time_ms(start_time));

	_func_exit_;
	return ret;
}
#else
static int rtw_suspend(struct usb_interface *pusb_intf, pm_message_t message)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	_adapter *padapter = dvobj->if1;
	struct net_device *pnetdev = padapter->pnetdev;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
	struct usb_device *usb_dev = interface_to_usbdev(pusb_intf);
#ifdef CONFIG_WOWLAN
	struct wowlan_ioctl_param poidparam;
#endif // CONFIG_WOWLAN

	int ret = 0;
	u32 start_time = rtw_get_current_time();
	
	_func_enter_;

	DBG_871X("==> %s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);

#ifdef CONFIG_WOWLAN
	if (check_fwstate(pmlmepriv, _FW_LINKED))
		pwrpriv->wowlan_mode = _TRUE;
	else
		pwrpriv->wowlan_mode = _FALSE;
#endif

	if((!padapter->bup) || (padapter->bDriverStopped)||(padapter->bSurpriseRemoved))
	{
		DBG_871X("padapter->bup=%d bDriverStopped=%d bSurpriseRemoved = %d\n",
			padapter->bup, padapter->bDriverStopped,padapter->bSurpriseRemoved);
		goto exit;
	}
	
	if(pwrpriv->bInternalAutoSuspend )
	{
	#ifdef CONFIG_AUTOSUSPEND	
	#ifdef SUPPORT_HW_RFOFF_DETECTED
		// The FW command register update must after MAC and FW init ready.
		if((padapter->bFWReady) && (pwrpriv->bHWPwrPindetect ) && (padapter->registrypriv.usbss_enable ))
		{
			u8 bOpen = _TRUE;
			rtw_interface_ps_func(padapter,HAL_USB_SELECT_SUSPEND,&bOpen);
			//rtl8192c_set_FwSelectSuspend_cmd(padapter,_TRUE ,500);//note fw to support hw power down ping detect
		}
	#endif
	#endif
	}
	pwrpriv->bInSuspend = _TRUE;		
	rtw_cancel_all_timer(padapter);		
	LeaveAllPowerSaveMode(padapter);

	rtw_stop_cmd_thread(padapter);

	_enter_pwrlock(&pwrpriv->lock);
	//padapter->net_closed = _TRUE;
	//s1.
	if(pnetdev)
	{
		netif_carrier_off(pnetdev);
		rtw_netif_stop_queue(pnetdev);
	}

#ifdef CONFIG_WOWLAN
	if(pwrpriv->bSupportRemoteWakeup==_TRUE && pwrpriv->wowlan_mode==_TRUE){
		//set H2C command
		poidparam.subcode=WOWLAN_ENABLE;
		padapter->HalFunc.SetHwRegHandler(padapter,HW_VAR_WOWLAN,(u8 *)&poidparam);
	}
	else
#else
	{
	//s2.
	rtw_disassoc_cmd(padapter, 0, _FALSE);
	}
#endif //CONFIG_WOWLAN

#ifdef CONFIG_LAYER2_ROAMING_RESUME
	if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) && check_fwstate(pmlmepriv, _FW_LINKED) )
	{
		//DBG_871X("%s:%d assoc_ssid:%s\n", __FUNCTION__, __LINE__, pmlmepriv->assoc_ssid.Ssid);
		DBG_871X("%s:%d %s(" MAC_FMT "), length:%d assoc_ssid.length:%d\n",__FUNCTION__, __LINE__,
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
#ifdef CONFIG_AUTOSUSPEND
	if(!pwrpriv->bInternalAutoSuspend )
#endif
	//s2-4.
	rtw_free_network_queue(padapter, _TRUE);

	rtw_dev_unload(padapter);
#ifdef CONFIG_AUTOSUSPEND
	pwrpriv->rf_pwrstate = rf_off;
	pwrpriv->bips_processing = _FALSE;
#endif
	_exit_pwrlock(&pwrpriv->lock);

	if(check_fwstate(pmlmepriv, _FW_UNDER_SURVEY))
		rtw_indicate_scan_done(padapter, 1);

	if(check_fwstate(pmlmepriv, _FW_UNDER_LINKING))
		rtw_indicate_disconnect(padapter);
	
exit:
	DBG_871X("<===  %s return %d.............. in %dms\n", __FUNCTION__
		, ret, rtw_get_passing_time_ms(start_time));

	_func_exit_;
	return ret;
}

#endif
static int rtw_resume(struct usb_interface *pusb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	_adapter *padapter = dvobj->if1;
	struct net_device *pnetdev = padapter->pnetdev;
	struct pwrctrl_priv *pwrpriv = dvobj_to_pwrctl(dvobj);
	 int ret = 0;
 
	if(pwrpriv->bInternalAutoSuspend ){
 		ret = rtw_resume_process(padapter);
	} else {
#ifdef CONFIG_RESUME_IN_WORKQUEUE
		rtw_resume_in_workqueue(pwrpriv);
#else
		if (rtw_is_earlysuspend_registered(pwrpriv)
			#ifdef CONFIG_WOWLAN
			&& !pwrpriv->wowlan_mode
			#endif /* CONFIG_WOWLAN */
		) {
			/* jeff: bypass resume here, do in late_resume */
			rtw_set_do_late_resume(pwrpriv, _TRUE);
		} else {
			ret = rtw_resume_process(padapter);
		}
#endif /* CONFIG_RESUME_IN_WORKQUEUE */
	}
	
	return ret;

}

int rtw_resume_process(_adapter *padapter)
{
	struct net_device *pnetdev;
	struct pwrctrl_priv *pwrpriv;
	int ret = -1;
	u32 start_time = rtw_get_current_time();
#ifdef CONFIG_BT_COEXIST
	u8 pm_cnt;
#endif	//#ifdef CONFIG_BT_COEXIST
	_func_enter_;

	DBG_871X("==> %s (%s:%d)\n",__FUNCTION__, current->comm, current->pid);

	if(padapter) {
		pnetdev= padapter->pnetdev;
		pwrpriv = adapter_to_pwrctl(padapter);
	} else {
		goto exit;
	}

	_enter_pwrlock(&pwrpriv->lock);
#ifdef CONFIG_BT_COEXIST
#ifdef CONFIG_AUTOSUSPEND
	#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,32))
	DBG_871X("%s...pm_usage_cnt(%d)  pwrpriv->bAutoResume=%x.  ....\n",__func__,atomic_read(&(adapter_to_dvobj(padapter)->pusbintf->pm_usage_cnt)),pwrpriv->bAutoResume);
	pm_cnt=atomic_read(&(adapter_to_dvobj(padapter)->pusbintf->pm_usage_cnt));
	#else
	DBG_871X("...pm_usage_cnt(%d).....\n", adapter_to_dvobj(padapter)->pusbintf->pm_usage_cnt);
	pm_cnt = adapter_to_dvobj(padapter)->pusbintf->pm_usage_cnt;
	#endif	

	DBG_871X("pwrpriv->bAutoResume (%x)\n",pwrpriv->bAutoResume );
	if( _TRUE == pwrpriv->bAutoResume ){
		pwrpriv->bInternalAutoSuspend = _FALSE;
		pwrpriv->bAutoResume=_FALSE;
		DBG_871X("pwrpriv->bAutoResume (%x)  pwrpriv->bInternalAutoSuspend(%x)\n",pwrpriv->bAutoResume,pwrpriv->bInternalAutoSuspend );

	}
#endif //#ifdef CONFIG_AUTOSUSPEND
#endif //#ifdef CONFIG_BT_COEXIST


	if(rtw_resume_common(padapter)!= 0) {
		DBG_871X("%s rtw_resume_common failed\n",__FUNCTION__);
		_exit_pwrlock(&pwrpriv->lock);
		goto exit;
	}

#ifdef CONFIG_AUTOSUSPEND
	if(pwrpriv->bInternalAutoSuspend )
	{
		#ifdef CONFIG_AUTOSUSPEND
		#ifdef SUPPORT_HW_RFOFF_DETECTED
			// The FW command register update must after MAC and FW init ready.
		if((padapter->bFWReady) && (pwrpriv->bHWPwrPindetect) && (padapter->registrypriv.usbss_enable ))
		{
			//rtl8192c_set_FwSelectSuspend_cmd(padapter,_FALSE ,500);//note fw to support hw power down ping detect
			u8 bOpen = _FALSE;
			rtw_interface_ps_func(padapter,HAL_USB_SELECT_SUSPEND,&bOpen);
		}
		#endif
		#endif
#ifdef CONFIG_BT_COEXIST
		DBG_871X("pwrpriv->bAutoResume (%x)\n",pwrpriv->bAutoResume );
		if( _TRUE == pwrpriv->bAutoResume ){
			pwrpriv->bInternalAutoSuspend = _FALSE;
			pwrpriv->bAutoResume=_FALSE;
			DBG_871X("pwrpriv->bAutoResume (%x)  pwrpriv->bInternalAutoSuspend(%x)\n",pwrpriv->bAutoResume,pwrpriv->bInternalAutoSuspend );
		}

#else	//#ifdef CONFIG_BT_COEXIST
		pwrpriv->bInternalAutoSuspend = _FALSE;
#endif	//#ifdef CONFIG_BT_COEXIST
		pwrpriv->brfoffbyhw = _FALSE;
	}
#endif
	_exit_pwrlock(&pwrpriv->lock);

	if( padapter->pid[1]!=0) {
		DBG_871X("pid[1]:%d\n",padapter->pid[1]);
		rtw_signal_process(padapter->pid[1], SIGUSR2);
	}		

	ret = 0;
exit:
	#ifdef CONFIG_RESUME_IN_WORKQUEUE
	rtw_unlock_suspend();
	#endif //CONFIG_RESUME_IN_WORKQUEUE

	pwrpriv->bInSuspend = _FALSE;
	DBG_871X("<===  %s return %d.............. in %dms\n", __FUNCTION__
		, ret, rtw_get_passing_time_ms(start_time));
	
	_func_exit_;
	
	return ret;
}

#ifdef CONFIG_AUTOSUSPEND
void autosuspend_enter(_adapter* padapter)	
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	
	DBG_871X("==>autosuspend_enter...........\n");

	pwrpriv->bInternalAutoSuspend = _TRUE;
	pwrpriv->bips_processing = _TRUE;
	
	if(rf_off == pwrpriv->change_rfpwrstate )
	{	
#ifndef	CONFIG_BT_COEXIST
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
		usb_enable_autosuspend(dvobj->pusbdev);
		#else
		dvobj->pusbdev->autosuspend_disabled = 0;//autosuspend disabled by the user	
		#endif
	
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
			usb_autopm_put_interface(dvobj->pusbintf);	
		#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,20))		
			usb_autopm_enable(dvobj->pusbintf);
		#else
			usb_autosuspend_device(dvobj->pusbdev, 1);
		#endif
#else	//#ifndef	CONFIG_BT_COEXIST
		if(1==pwrpriv->autopm_cnt){
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
		usb_enable_autosuspend(dvobj->pusbdev);
		#else
		dvobj->pusbdev->autosuspend_disabled = 0;//autosuspend disabled by the user	
		#endif
	
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
			usb_autopm_put_interface(dvobj->pusbintf);	
		#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,20))		
			usb_autopm_enable(dvobj->pusbintf);
		#else
			usb_autosuspend_device(dvobj->pusbdev, 1);
		#endif
			pwrpriv->autopm_cnt --;
		}
		else
		DBG_871X("0!=pwrpriv->autopm_cnt[%d]   didn't usb_autopm_put_interface\n", pwrpriv->autopm_cnt);

#endif	//#ifndef	CONFIG_BT_COEXIST
	}
	#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,32))
	DBG_871X("...pm_usage_cnt(%d).....\n", atomic_read(&(dvobj->pusbintf->pm_usage_cnt)));
	#else
	DBG_871X("...pm_usage_cnt(%d).....\n", dvobj->pusbintf->pm_usage_cnt);
	#endif
	
}
int autoresume_enter(_adapter* padapter)
{
	int result = _SUCCESS;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct security_priv* psecuritypriv=&(padapter->securitypriv);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
	
	DBG_871X("====> autoresume_enter \n");
	
	if(rf_off == pwrpriv->rf_pwrstate )
	{
		pwrpriv->ps_flag = _FALSE;
#ifndef	CONFIG_BT_COEXIST
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))				
			if (usb_autopm_get_interface(dvobj->pusbintf) < 0) 
			{
				DBG_871X( "can't get autopm: %d\n", result);
				result = _FAIL;
				goto error_exit;
			}			
		#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,20))				
			usb_autopm_disable(dvobj->pusbintf);
		#else
			usb_autoresume_device(dvobj->pusbdev, 1);
		#endif

		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,32))
		DBG_871X("...pm_usage_cnt(%d).....\n", atomic_read(&(dvobj->pusbintf->pm_usage_cnt)));
		#else
		DBG_871X("...pm_usage_cnt(%d).....\n", dvobj->pusbintf->pm_usage_cnt);
		#endif	
#else	//#ifndef	CONFIG_BT_COEXIST
		pwrpriv->bAutoResume=_TRUE;	
		if(0==pwrpriv->autopm_cnt){
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))				
			if (usb_autopm_get_interface(dvobj->pusbintf) < 0) 
			{
				DBG_871X( "can't get autopm: %d\n", result);
				result = _FAIL;
				goto error_exit;
			}			
		#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,20))				
			usb_autopm_disable(dvobj->pusbintf);
		#else
			usb_autoresume_device(dvobj->pusbdev, 1);
		#endif
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,32))
			DBG_871X("...pm_usage_cnt(%d).....\n", atomic_read(&(dvobj->pusbintf->pm_usage_cnt)));
		#else
			DBG_871X("...pm_usage_cnt(%d).....\n", dvobj->pusbintf->pm_usage_cnt);
		#endif	
			pwrpriv->autopm_cnt++;
		}
		else
			DBG_871X("0!=pwrpriv->autopm_cnt[%d]   didn't usb_autopm_get_interface\n",pwrpriv->autopm_cnt);
#endif //#ifndef	CONFIG_BT_COEXIST
	}	
	DBG_871X("<==== autoresume_enter \n");
error_exit:	

	return result;
}
#endif

#ifdef CONFIG_PLATFORM_RTD2880B
extern void rtd2885_wlan_netlink_sendMsg(char *action_string, char *name);
#endif

#ifdef CONFIG_PLATFORM_ARM_SUNxI
#include <mach/sys_config.h>
extern int sw_usb_disable_hcd(__u32 usbc_no);
extern int sw_usb_enable_hcd(__u32 usbc_no);
static int usb_wifi_host = 2;
#endif

#ifdef CONFIG_PLATFORM_ARM_SUN6I
#include <mach/sys_config.h>
extern int sw_usb_disable_hcd(__u32 usbc_no);
extern int sw_usb_enable_hcd(__u32 usbc_no);
extern void wifi_pm_power(int on);
static script_item_u item;
#endif

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located a card for us to support.
 *        We accept the new device by returning 0.
*/

_adapter  *rtw_sw_export = NULL;

_adapter *rtw_usb_if1_init(struct dvobj_priv *dvobj,
	struct usb_interface *pusb_intf, const struct usb_device_id *pdid)
{
	_adapter *padapter = NULL;
	struct net_device *pnetdev = NULL;
	int status = _FAIL;

	if ((padapter = (_adapter *)rtw_zvmalloc(sizeof(*padapter))) == NULL) {
		goto exit;
	}
	padapter->dvobj = dvobj;
	dvobj->if1 = padapter;
	
	padapter->bDriverStopped=_TRUE;

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

	#ifndef RTW_DVOBJ_CHIP_HW_TYPE
	//step 1-1., decide the chip_type via vid/pid
	padapter->interface_type = RTW_USB;
	decide_chip_type_by_usb_device_id(padapter, pdid);
	#endif

	if (rtw_handle_dualmac(padapter, 1) != _SUCCESS)
		goto free_adapter;

	if((pnetdev = rtw_init_netdev(padapter)) == NULL) {
		goto handle_dualmac;
	}
	SET_NETDEV_DEV(pnetdev, dvobj_to_dev(dvobj));
	padapter = rtw_netdev_priv(pnetdev);

#ifdef CONFIG_IOCTL_CFG80211
	if(rtw_wdev_alloc(padapter, dvobj_to_dev(dvobj)) != 0) {
		goto handle_dualmac;
	}
#endif


	//step 2. hook HalFunc, allocate HalData
	hal_set_hal_ops(padapter);

	padapter->intf_start=&usb_intf_start;
	padapter->intf_stop=&usb_intf_stop;

	//step init_io_priv
	rtw_init_io_priv(padapter, usb_set_intf_ops);

	//step read_chip_version
	rtw_hal_read_chip_version(padapter);

	//step usb endpoint mapping
	rtw_hal_chip_configure(padapter);

	//step read efuse/eeprom data and get mac_addr
	rtw_hal_read_chip_info(padapter);	

	//step 5.
	if(rtw_init_drv_sw(padapter) ==_FAIL) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize driver software resource Failed!\n"));
		goto free_hal_data;
	}

#ifdef CONFIG_PM
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,18))
	if(adapter_to_pwrctl(padapter)->bSupportRemoteWakeup)
	{
		dvobj->pusbdev->do_remote_wakeup=1;
		pusb_intf->needs_remote_wakeup = 1;		
		device_init_wakeup(&pusb_intf->dev, 1);
		DBG_871X("\n  pwrctrlpriv.bSupportRemoteWakeup~~~~~~\n");
		DBG_871X("\n  pwrctrlpriv.bSupportRemoteWakeup~~~[%d]~~~\n",device_may_wakeup(&pusb_intf->dev));
	}
#endif
#endif

#ifdef CONFIG_AUTOSUSPEND
	if( padapter->registrypriv.power_mgnt != PS_MODE_ACTIVE )
	{
		if(padapter->registrypriv.usbss_enable ){ 	/* autosuspend (2s delay) */
			#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,38))
			dvobj->pusbdev->dev.power.autosuspend_delay = 0 * HZ;//15 * HZ; idle-delay time		
			#else
			dvobj->pusbdev->autosuspend_delay = 0 * HZ;//15 * HZ; idle-delay time		 	
			#endif

			#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,35))
			usb_enable_autosuspend(dvobj->pusbdev);
			#elif  (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,22) && LINUX_VERSION_CODE<=KERNEL_VERSION(2,6,34))
			padapter->bDisableAutosuspend = dvobj->pusbdev->autosuspend_disabled ;
			dvobj->pusbdev->autosuspend_disabled = 0;//autosuspend disabled by the user
			#endif

			//usb_autopm_get_interface(adapter_to_dvobj(padapter)->pusbintf );//init pm_usage_cnt ,let it start from 1

			#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,32))
			DBG_871X("%s...pm_usage_cnt(%d).....\n",__FUNCTION__,atomic_read(&(dvobj->pusbintf ->pm_usage_cnt)));
			#else
			DBG_871X("%s...pm_usage_cnt(%d).....\n",__FUNCTION__,dvobj->pusbintf ->pm_usage_cnt);
			#endif							
		}
	}	
#endif
	//2012-07-11 Move here to prevent the 8723AS-VAU BT auto suspend influence	
	#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))				
			if (usb_autopm_get_interface(pusb_intf) < 0) 
				{
					DBG_871X( "can't get autopm: \n");
				}
	#endif
#ifdef	CONFIG_BT_COEXIST
	adapter_to_pwrctl(padapter)->autopm_cnt=1;
#endif

	// set mac addr
	rtw_macaddr_cfg(padapter->eeprompriv.mac_addr);
	rtw_init_wifidirect_addrs(padapter, padapter->eeprompriv.mac_addr, padapter->eeprompriv.mac_addr);

	DBG_871X("bDriverStopped:%d, bSurpriseRemoved:%d, bup:%d, hw_init_completed:%d\n"
		, padapter->bDriverStopped
		, padapter->bSurpriseRemoved
		, padapter->bup
		, padapter->hw_init_completed
	);

	status = _SUCCESS;

free_hal_data:
	if(status != _SUCCESS && padapter->HalData)
		rtw_mfree(padapter->HalData, sizeof(*(padapter->HalData)));
free_wdev:
	if(status != _SUCCESS) {
		#ifdef CONFIG_IOCTL_CFG80211
		rtw_wdev_unregister(padapter->rtw_wdev);
		rtw_wdev_free(padapter->rtw_wdev);
		#endif
	}
handle_dualmac:
	if (status != _SUCCESS)
		rtw_handle_dualmac(padapter, 0);
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

static void rtw_usb_if1_deinit(_adapter *if1)
{
	struct net_device *pnetdev = if1->pnetdev;
	struct mlme_priv *pmlmepriv= &if1->mlmepriv;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(if1);

	if(check_fwstate(pmlmepriv, _FW_LINKED))
		rtw_disassoc_cmd(if1, 0, _FALSE);


#ifdef CONFIG_AP_MODE
	free_mlme_ap_info(if1);
	#ifdef CONFIG_HOSTAPD_MLME
	hostapd_mode_unload(if1);
	#endif
#endif

	rtw_cancel_all_timer(if1);

#ifdef CONFIG_WOWLAN
	pwrctl->wowlan_mode=_FALSE;
#endif //CONFIG_WOWLAN

	rtw_dev_unload(if1);

	DBG_871X("+r871xu_dev_remove, hw_init_completed=%d\n", if1->hw_init_completed);

	rtw_handle_dualmac(if1, 0);

#ifdef CONFIG_IOCTL_CFG80211
	if(if1->rtw_wdev)
	{
		//rtw_wdev_unregister(if1->rtw_wdev);
		rtw_wdev_free(if1->rtw_wdev);
	}
#endif

#ifdef CONFIG_BT_COEXIST
	if(1 == pwrctl->autopm_cnt){
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,33))
			usb_autopm_put_interface(adapter_to_dvobj(if1)->pusbintf);	
		#elif (LINUX_VERSION_CODE>=KERNEL_VERSION(2,6,20))		
			usb_autopm_enable(adapter_to_dvobj(if1)->pusbintf);
		#else
			usb_autosuspend_device(adapter_to_dvobj(if1)->pusbdev, 1);
		#endif
		pwrctl->autopm_cnt --;
	}
#endif

	rtw_free_drv_sw(if1);

	if(pnetdev)
		rtw_free_netdev(pnetdev);

#ifdef CONFIG_PLATFORM_RTD2880B
	DBG_871X("wlan link down\n");
	rtd2885_wlan_netlink_sendMsg("linkdown", "8712");
#endif

}

static void dump_usb_interface(struct usb_interface *usb_intf)
{
	int	i;
	u8	val8;

	struct usb_device				*udev = interface_to_usbdev(usb_intf);
	struct usb_device_descriptor 	*dev_desc = &udev->descriptor;
	
	struct usb_host_config			*act_conf = udev->actconfig;
	struct usb_config_descriptor	*act_conf_desc = &act_conf->desc;
	
	struct usb_host_interface		*host_iface;
	struct usb_interface_descriptor	*iface_desc;
	struct usb_host_endpoint		*host_endp;
	struct usb_endpoint_descriptor	*endp_desc;

#if 1 /* The usb device this usb interface belongs to */
	DBG_871X("usb_interface:%p, usb_device:%p(num:%d, path:%s), usb_device_descriptor:%p\n", usb_intf, udev, udev->devnum, udev->devpath, dev_desc);
	DBG_871X("bLength:%u\n", dev_desc->bLength);
	DBG_871X("bDescriptorType:0x%02x\n", dev_desc->bDescriptorType);
	DBG_871X("bcdUSB:0x%04x\n", le16_to_cpu(dev_desc->bcdUSB));
	DBG_871X("bDeviceClass:0x%02x\n", dev_desc->bDeviceClass);
	DBG_871X("bDeviceSubClass:0x%02x\n", dev_desc->bDeviceSubClass);
	DBG_871X("bDeviceProtocol:0x%02x\n", dev_desc->bDeviceProtocol);
	DBG_871X("bMaxPacketSize0:%u\n", dev_desc->bMaxPacketSize0);
	DBG_871X("idVendor:0x%04x\n", le16_to_cpu(dev_desc->idVendor));
	DBG_871X("idProduct:0x%04x\n", le16_to_cpu(dev_desc->idProduct));
	DBG_871X("bcdDevice:0x%04x\n", le16_to_cpu(dev_desc->bcdDevice));
	DBG_871X("iManufacturer:0x02%x\n", dev_desc->iManufacturer);
	DBG_871X("iProduct:0x%02x\n", dev_desc->iProduct);
	DBG_871X("iSerialNumber:0x%02x\n", dev_desc->iSerialNumber);
	DBG_871X("bNumConfigurations:%u\n", dev_desc->bNumConfigurations);
#endif
	

#if 1 /* The acting usb_config_descriptor */
	DBG_871X("\nact_conf_desc:%p\n", act_conf_desc);
	DBG_871X("bLength:%u\n", act_conf_desc->bLength);
	DBG_871X("bDescriptorType:0x%02x\n", act_conf_desc->bDescriptorType);
	DBG_871X("wTotalLength:%u\n", le16_to_cpu(act_conf_desc->wTotalLength));
	DBG_871X("bNumInterfaces:%u\n", act_conf_desc->bNumInterfaces);
	DBG_871X("bConfigurationValue:0x%02x\n", act_conf_desc->bConfigurationValue);
	DBG_871X("iConfiguration:0x%02x\n", act_conf_desc->iConfiguration);
	DBG_871X("bmAttributes:0x%02x\n", act_conf_desc->bmAttributes);
	DBG_871X("bMaxPower=%u\n", act_conf_desc->bMaxPower);
#endif


	DBG_871X("****** num of altsetting = (%d) ******/\n", usb_intf->num_altsetting);
	/* Get he host side alternate setting (the current alternate setting) for this interface*/
	host_iface = usb_intf->cur_altsetting;
	iface_desc = &host_iface->desc;

#if 1 /* The current alternate setting*/
	DBG_871X("\nusb_interface_descriptor:%p:\n", iface_desc);
	DBG_871X("bLength:%u\n", iface_desc->bLength);
	DBG_871X("bDescriptorType:0x%02x\n", iface_desc->bDescriptorType);
	DBG_871X("bInterfaceNumber:0x%02x\n", iface_desc->bInterfaceNumber);
	DBG_871X("bAlternateSetting=%x\n", iface_desc->bAlternateSetting);
	DBG_871X("bNumEndpoints=%x\n", iface_desc->bNumEndpoints);
	DBG_871X("bInterfaceClass=%x\n", iface_desc->bInterfaceClass);
	DBG_871X("bInterfaceSubClass=%x\n", iface_desc->bInterfaceSubClass);
	DBG_871X("bInterfaceProtocol=%x\n", iface_desc->bInterfaceProtocol);
	DBG_871X("iInterface=%x\n", iface_desc->iInterface);
#endif


#if 1
	//DBG_871X("\ndump usb_endpoint_descriptor:\n");

	for (i = 0; i < iface_desc->bNumEndpoints; i++)
	{
		host_endp = host_iface->endpoint + i;
		if (host_endp)
		{
			endp_desc = &host_endp->desc;

			DBG_871X("\nusb_endpoint_descriptor(%d):\n", i);
			DBG_871X("bLength=%x\n",endp_desc->bLength);
			DBG_871X("bDescriptorType=%x\n",endp_desc->bDescriptorType);
			DBG_871X("bEndpointAddress=%x\n",endp_desc->bEndpointAddress);
			DBG_871X("bmAttributes=%x\n",endp_desc->bmAttributes);
			DBG_871X("wMaxPacketSize=%x\n",endp_desc->wMaxPacketSize);
			DBG_871X("wMaxPacketSize=%x\n",le16_to_cpu(endp_desc->wMaxPacketSize));
			DBG_871X("bInterval=%x\n",endp_desc->bInterval);
			//DBG_871X("bRefresh=%x\n",pendp_desc->bRefresh);
			//DBG_871X("bSynchAddress=%x\n",pendp_desc->bSynchAddress);

			if (RT_usb_endpoint_is_bulk_in(endp_desc))
			{
				DBG_871X("RT_usb_endpoint_is_bulk_in = %x\n", RT_usb_endpoint_num(endp_desc));
				//pdvobjpriv->RtNumInPipes++;
			}
			else if (RT_usb_endpoint_is_int_in(endp_desc))
			{
				DBG_871X("RT_usb_endpoint_is_int_in = %x, Interval = %x\n", RT_usb_endpoint_num(endp_desc),endp_desc->bInterval);
				//pdvobjpriv->RtNumInPipes++;
			}
			else if (RT_usb_endpoint_is_bulk_out(endp_desc))
			{
				DBG_871X("RT_usb_endpoint_is_bulk_out = %x\n", RT_usb_endpoint_num(endp_desc));
				//pdvobjpriv->RtNumOutPipes++;
			}
			//pdvobjpriv->ep_num[i] = RT_usb_endpoint_num(pendp_desc);
		}
	}
	
	//DBG_871X("nr_endpoint=%d, in_num=%d, out_num=%d\n\n", pdvobjpriv->nr_endpoint, pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);
#endif

	if (udev->speed == USB_SPEED_HIGH)
		DBG_871X("USB_SPEED_HIGH\n");
	else
		DBG_871X("NON USB_SPEED_HIGH\n");

}


static int rtw_drv_init(struct usb_interface *pusb_intf, const struct usb_device_id *pdid)
{
	int i;
	_adapter *if1 = NULL, *if2 = NULL;
	int status;
	struct dvobj_priv *dvobj;

	RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("+rtw_drv_init\n"));
	//DBG_871X("+rtw_drv_init\n");

	//step 0.
	process_spec_devid(pdid);

	/* Initialize dvobj_priv */
	if ((dvobj = usb_dvobj_init(pusb_intf)) == NULL) {
		RT_TRACE(_module_hci_intfs_c_, _drv_err_, ("initialize device object priv Failed!\n"));
		goto exit;
	}

	#ifdef RTW_DVOBJ_CHIP_HW_TYPE
	decide_chip_type_by_usb_device_id(dvobj, pdid);
	#endif

	if ((if1 = rtw_usb_if1_init(dvobj, pusb_intf, pdid)) == NULL) {
		DBG_871X("rtw_init_primary_adapter Failed!\n");
		goto free_dvobj;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if((if2 = rtw_drv_if2_init(if1, usb_set_intf_ops)) == NULL) {
		goto free_if1;
	}	
#endif

#ifdef CONFIG_INTEL_PROXIM	
	rtw_sw_export=if1;
#endif

#ifdef CONFIG_GLOBAL_UI_PID
	if(ui_pid[1]!=0) {
		DBG_871X("ui_pid[1]:%d\n",ui_pid[1]);
		rtw_signal_process(ui_pid[1], SIGUSR2);
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

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-871x_drv - drv_init, success!\n"));

	status = _SUCCESS;

free_if2:
	if(status != _SUCCESS && if2) {
		#ifdef CONFIG_CONCURRENT_MODE
		rtw_drv_if2_stop(if2);
		rtw_drv_if2_free(if2);
		#endif
	}
free_if1:
	if (status != _SUCCESS && if1) {
		rtw_usb_if1_deinit(if1);
	}
free_dvobj:
	if (status != _SUCCESS)
		usb_dvobj_deinit(pusb_intf);
exit:
	return status == _SUCCESS?0:-ENODEV;
}
extern void rtw_unregister_netdevs(struct dvobj_priv *dvobj);
/*
 * dev_remove() - our device is being removed
*/
//rmmod module & unplug(SurpriseRemoved) will call r871xu_dev_remove() => how to recognize both
static void rtw_dev_remove(struct usb_interface *pusb_intf)
{
	struct dvobj_priv *dvobj = usb_get_intfdata(pusb_intf);
	_adapter *padapter = dvobj->if1;
	struct net_device *pnetdev = padapter->pnetdev;
	struct mlme_priv *pmlmepriv= &padapter->mlmepriv;

_func_enter_;

	DBG_871X("+rtw_dev_remove\n");
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+dev_remove()\n"));
	dvobj->processing_dev_remove = _TRUE;
	rtw_unregister_netdevs(dvobj);	
	
	if(usb_drv->drv_registered == _TRUE)
	{
		//DBG_871X("r871xu_dev_remove():padapter->bSurpriseRemoved == _TRUE\n");
		padapter->bSurpriseRemoved = _TRUE;
	}
	/*else
	{
		//DBG_871X("r871xu_dev_remove():module removed\n");
		padapter->hw_init_completed = _FALSE;
	}*/
#ifdef CONFIG_MP_INCLUDED
	if (padapter->registrypriv.mp_mode == 1)
		MPT_DeInitAdapter(padapter);
#endif
#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	rtw_unregister_early_suspend(dvobj_to_pwrctl(dvobj));
#endif

	rtw_pm_set_ips(padapter, IPS_NONE);
	rtw_pm_set_lps(padapter, PS_MODE_ACTIVE);

	LeaveAllPowerSaveMode(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_if2_stop(dvobj->if2);
#endif

	rtw_usb_if1_deinit(padapter);

#ifdef CONFIG_CONCURRENT_MODE
	rtw_drv_if2_free(dvobj->if2);
#endif

	usb_dvobj_deinit(pusb_intf);

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-dev_remove()\n"));
	DBG_871X("-r871xu_dev_remove, done\n");


#ifdef CONFIG_INTEL_PROXIM	
	rtw_sw_export=NULL;
#endif

_func_exit_;

	return;

}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)) 
extern int console_suspend_enabled;
#endif

static int /*__init*/ rtw_drv_entry(void)
{
	int ret = 0;

	DBG_871X_LEVEL(_drv_always_, "module init start\n");
	dump_drv_version(RTW_DBGDUMP);
#ifdef BTCOEXVERSION
	DBG_871X_LEVEL(_drv_always_, DRV_NAME" BT-Coex version = %s\n", BTCOEXVERSION);
#endif // BTCOEXVERSION

#ifdef CONFIG_PLATFORM_RTK_DMP
	u32 tmp;
	tmp=readl((volatile unsigned int*)0xb801a608);
	tmp &= 0xffffff00;
	tmp |= 0x55;
	writel(tmp,(volatile unsigned int*)0xb801a608);//write dummy register for 1055
#endif
#ifdef CONFIG_PLATFORM_ARM_SUNxI
#ifndef CONFIG_RTL8723A
	/* ----------get usb_wifi_usbc_num------------- */	
	ret = script_parser_fetch("usb_wifi_para", "usb_wifi_usbc_num", (int *)&usb_wifi_host, 64);	
	if(ret != 0){		
		DBG_8192C("ERR: script_parser_fetch usb_wifi_usbc_num failed\n");		
		ret = -ENOMEM;		
		goto exit;	
	}	
	DBG_8192C("sw_usb_enable_hcd: usbc_num = %d\n", usb_wifi_host);	
	sw_usb_enable_hcd(usb_wifi_host);
#endif //CONFIG_RTL8723A	
#endif //CONFIG_PLATFORM_ARM_SUNxI

#if defined  CONFIG_PLATFORM_ARM_SUN6I   
	script_item_value_type_e type;

	type = script_get_item("wifi_para", "wifi_usbc_id", &item);
	if(SCIRPT_ITEM_VALUE_TYPE_INT != type){
		printk("ERR: script_get_item wifi_usbc_id failed\n");
		ret = -ENOMEM;
		goto exit;
	}

	printk("sw_usb_enable_hcd: usbc_num = %d\n", item.val);
	wifi_pm_power(1);
	mdelay(10);
	
	#ifndef CONFIG_RTL8723A
	sw_usb_enable_hcd(item.val);
	#endif
#endif // defined  CONFIG_PLATFORM_ARM_SUN6I  && !(defined CONFIG_RTL8723A)
	
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)) 
	//console_suspend_enabled=0;
#endif

	usb_drv->drv_registered = _TRUE;
	rtw_suspend_lock_init();
	rtw_drv_proc_init();
	rtw_ndev_notifier_register();

	ret = usb_register(&usb_drv->usbdrv);

	if (ret != 0) {
		usb_drv->drv_registered = _FALSE;
		rtw_suspend_lock_uninit();
		rtw_drv_proc_deinit();
		rtw_ndev_notifier_unregister();
		goto exit;
	}

exit:
	DBG_871X_LEVEL(_drv_always_, "module init ret=%d\n", ret);
	return ret;
}

static void /*exit*/ rtw_drv_halt(void)
{
	DBG_871X_LEVEL(_drv_always_, "module exit start\n");

	usb_drv->drv_registered = _FALSE;
	usb_deregister(&usb_drv->usbdrv);

#ifdef CONFIG_PLATFORM_ARM_SUNxI
#ifndef CONFIG_RTL8723A
	DBG_8192C("sw_usb_disable_hcd: usbc_num = %d\n", usb_wifi_host);
	sw_usb_disable_hcd(usb_wifi_host);
#endif //ifndef CONFIG_RTL8723A	
#endif	//CONFIG_PLATFORM_ARM_SUNxI

#if defined  CONFIG_PLATFORM_ARM_SUN6I
	#ifndef CONFIG_RTL8723A
	sw_usb_disable_hcd(item.val);
	#endif
	wifi_pm_power(0);
#endif	//  defined  CONFIG_PLATFORM_ARM_SUN6I  && !(defined CONFIG_RTL8723A)

	rtw_suspend_lock_uninit();
	rtw_drv_proc_deinit();
	rtw_ndev_notifier_unregister();

	DBG_871X_LEVEL(_drv_always_, "module exit success\n");

	rtw_mstat_dump(RTW_DBGDUMP);
}


#include "wifi_version.h"
#include <linux/rfkill-wlan.h>
extern int get_wifi_chip_type(void);

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
    printk("Realtek 8723AU USB WiFi driver (Powered by Rockchip,Ver %s) init.\n", RTL8192_DRV_VERSION);
    rockchip_wifi_power(1);

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
    printk("Realtek 8723AU USB WiFi driver (Powered by Rockchip,Ver %s) init.\n", RTL8192_DRV_VERSION);
    rtw_drv_halt();
    rockchip_wifi_power(0);
}

#ifdef CONFIG_WIFI_LOAD_DRIVER_WHEN_KERNEL_BOOTUP
late_initcall(rockchip_wifi_init_module_rtkwifi);
module_exit(rockchip_wifi_exit_module_rtkwifi);
#else
EXPORT_SYMBOL(rockchip_wifi_init_module_rtkwifi);
EXPORT_SYMBOL(rockchip_wifi_exit_module_rtkwifi);
#endif
//module_init(rtw_drv_entry);
//module_exit(rtw_drv_halt);

#ifdef CONFIG_INTEL_PROXIM	
_adapter  *rtw_usb_get_sw_pointer(void)
{
	return rtw_sw_export;
}
EXPORT_SYMBOL(rtw_usb_get_sw_pointer);
#endif	//CONFIG_INTEL_PROXIM

