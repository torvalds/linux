// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * usb_intf.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _HCI_INTF_C_

#include <linux/usb.h>
#include <linux/module.h>
#include <linux/firmware.h>

#include "osdep_service.h"
#include "drv_types.h"
#include "recv_osdep.h"
#include "xmit_osdep.h"
#include "rtl8712_efuse.h"
#include "usb_ops.h"
#include "usb_osintf.h"

static struct usb_interface *pintf;

static int r871xu_drv_init(struct usb_interface *pusb_intf,
			   const struct usb_device_id *pdid);

static void r871xu_dev_remove(struct usb_interface *pusb_intf);

static const struct usb_device_id rtl871x_usb_id_tbl[] = {
/* RTL8188SU */
	/* Realtek */
	{USB_DEVICE(0x0BDA, 0x8171)},
	{USB_DEVICE(0x0bda, 0x8173)},
	{USB_DEVICE(0x0bda, 0x8712)},
	{USB_DEVICE(0x0bda, 0x8713)},
	{USB_DEVICE(0x0bda, 0xC512)},
	/* Abocom */
	{USB_DEVICE(0x07B8, 0x8188)},
	/* ASUS */
	{USB_DEVICE(0x0B05, 0x1786)},
	{USB_DEVICE(0x0B05, 0x1791)}, /* 11n mode disable */
	/* Belkin */
	{USB_DEVICE(0x050D, 0x945A)},
	/* ISY IWL - Belkin clone */
	{USB_DEVICE(0x050D, 0x11F1)},
	/* Corega */
	{USB_DEVICE(0x07AA, 0x0047)},
	/* D-Link */
	{USB_DEVICE(0x2001, 0x3306)},
	{USB_DEVICE(0x07D1, 0x3306)}, /* 11n mode disable */
	/* Edimax */
	{USB_DEVICE(0x7392, 0x7611)},
	/* EnGenius */
	{USB_DEVICE(0x1740, 0x9603)},
	/* Hawking */
	{USB_DEVICE(0x0E66, 0x0016)},
	/* Hercules */
	{USB_DEVICE(0x06F8, 0xE034)},
	{USB_DEVICE(0x06F8, 0xE032)},
	/* Logitec */
	{USB_DEVICE(0x0789, 0x0167)},
	/* PCI */
	{USB_DEVICE(0x2019, 0xAB28)},
	{USB_DEVICE(0x2019, 0xED16)},
	/* Sitecom */
	{USB_DEVICE(0x0DF6, 0x0057)},
	{USB_DEVICE(0x0DF6, 0x0045)},
	{USB_DEVICE(0x0DF6, 0x0059)}, /* 11n mode disable */
	{USB_DEVICE(0x0DF6, 0x004B)},
	{USB_DEVICE(0x0DF6, 0x005B)},
	{USB_DEVICE(0x0DF6, 0x005D)},
	{USB_DEVICE(0x0DF6, 0x0063)},
	/* Sweex */
	{USB_DEVICE(0x177F, 0x0154)},
	/* Thinkware */
	{USB_DEVICE(0x0BDA, 0x5077)},
	/* Toshiba */
	{USB_DEVICE(0x1690, 0x0752)},
	/* - */
	{USB_DEVICE(0x20F4, 0x646B)},
	{USB_DEVICE(0x083A, 0xC512)},
	{USB_DEVICE(0x25D4, 0x4CA1)},
	{USB_DEVICE(0x25D4, 0x4CAB)},

/* RTL8191SU */
	/* Realtek */
	{USB_DEVICE(0x0BDA, 0x8172)},
	{USB_DEVICE(0x0BDA, 0x8192)},
	/* Amigo */
	{USB_DEVICE(0x0EB0, 0x9061)},
	/* ASUS/EKB */
	{USB_DEVICE(0x13D3, 0x3323)},
	{USB_DEVICE(0x13D3, 0x3311)}, /* 11n mode disable */
	{USB_DEVICE(0x13D3, 0x3342)},
	/* ASUS/EKBLenovo */
	{USB_DEVICE(0x13D3, 0x3333)},
	{USB_DEVICE(0x13D3, 0x3334)},
	{USB_DEVICE(0x13D3, 0x3335)}, /* 11n mode disable */
	{USB_DEVICE(0x13D3, 0x3336)}, /* 11n mode disable */
	/* ASUS/Media BOX */
	{USB_DEVICE(0x13D3, 0x3309)},
	/* Belkin */
	{USB_DEVICE(0x050D, 0x815F)},
	/* D-Link */
	{USB_DEVICE(0x07D1, 0x3302)},
	{USB_DEVICE(0x07D1, 0x3300)},
	{USB_DEVICE(0x07D1, 0x3303)},
	/* Edimax */
	{USB_DEVICE(0x7392, 0x7612)},
	/* EnGenius */
	{USB_DEVICE(0x1740, 0x9605)},
	/* Guillemot */
	{USB_DEVICE(0x06F8, 0xE031)},
	/* Hawking */
	{USB_DEVICE(0x0E66, 0x0015)},
	/* Mediao */
	{USB_DEVICE(0x13D3, 0x3306)},
	/* PCI */
	{USB_DEVICE(0x2019, 0xED18)},
	{USB_DEVICE(0x2019, 0x4901)},
	/* Sitecom */
	{USB_DEVICE(0x0DF6, 0x0058)},
	{USB_DEVICE(0x0DF6, 0x0049)},
	{USB_DEVICE(0x0DF6, 0x004C)},
	{USB_DEVICE(0x0DF6, 0x006C)},
	{USB_DEVICE(0x0DF6, 0x0064)},
	/* Skyworth */
	{USB_DEVICE(0x14b2, 0x3300)},
	{USB_DEVICE(0x14b2, 0x3301)},
	{USB_DEVICE(0x14B2, 0x3302)},
	/* - */
	{USB_DEVICE(0x04F2, 0xAFF2)},
	{USB_DEVICE(0x04F2, 0xAFF5)},
	{USB_DEVICE(0x04F2, 0xAFF6)},
	{USB_DEVICE(0x13D3, 0x3339)},
	{USB_DEVICE(0x13D3, 0x3340)}, /* 11n mode disable */
	{USB_DEVICE(0x13D3, 0x3341)}, /* 11n mode disable */
	{USB_DEVICE(0x13D3, 0x3310)},
	{USB_DEVICE(0x13D3, 0x3325)},

/* RTL8192SU */
	/* Realtek */
	{USB_DEVICE(0x0BDA, 0x8174)},
	/* Belkin */
	{USB_DEVICE(0x050D, 0x845A)},
	/* Corega */
	{USB_DEVICE(0x07AA, 0x0051)},
	/* Edimax */
	{USB_DEVICE(0x7392, 0x7622)},
	/* NEC */
	{USB_DEVICE(0x0409, 0x02B6)},
	{}
};

MODULE_DEVICE_TABLE(usb, rtl871x_usb_id_tbl);

static struct specific_device_id specific_device_id_tbl[] = {
	{.idVendor = 0x0b05, .idProduct = 0x1791,
		 .flags = SPEC_DEV_ID_DISABLE_HT},
	{.idVendor = 0x0df6, .idProduct = 0x0059,
		 .flags = SPEC_DEV_ID_DISABLE_HT},
	{.idVendor = 0x13d3, .idProduct = 0x3306,
		 .flags = SPEC_DEV_ID_DISABLE_HT},
	{.idVendor = 0x13D3, .idProduct = 0x3311,
		 .flags = SPEC_DEV_ID_DISABLE_HT},
	{.idVendor = 0x13d3, .idProduct = 0x3335,
		 .flags = SPEC_DEV_ID_DISABLE_HT},
	{.idVendor = 0x13d3, .idProduct = 0x3336,
		 .flags = SPEC_DEV_ID_DISABLE_HT},
	{.idVendor = 0x13d3, .idProduct = 0x3340,
		 .flags = SPEC_DEV_ID_DISABLE_HT},
	{.idVendor = 0x13d3, .idProduct = 0x3341,
		 .flags = SPEC_DEV_ID_DISABLE_HT},
	{}
};

struct drv_priv {
	struct usb_driver r871xu_drv;
	int drv_registered;
};

#ifdef CONFIG_PM
static int r871x_suspend(struct usb_interface *pusb_intf, pm_message_t state)
{
	struct net_device *pnetdev = usb_get_intfdata(pusb_intf);
	struct _adapter *padapter = netdev_priv(pnetdev);

	netdev_info(pnetdev, "Suspending...\n");
	padapter->suspended = true;
	rtl871x_intf_stop(padapter);
	if (pnetdev->netdev_ops->ndo_stop)
		pnetdev->netdev_ops->ndo_stop(pnetdev);
	mdelay(10);
	netif_device_detach(pnetdev);
	return 0;
}

static void rtl871x_intf_resume(struct _adapter *padapter)
{
	if (padapter->dvobjpriv.inirp_init)
		padapter->dvobjpriv.inirp_init(padapter);
}

static int r871x_resume(struct usb_interface *pusb_intf)
{
	struct net_device *pnetdev = usb_get_intfdata(pusb_intf);
	struct _adapter *padapter = netdev_priv(pnetdev);

	netdev_info(pnetdev,  "Resuming...\n");
	netif_device_attach(pnetdev);
	if (pnetdev->netdev_ops->ndo_open)
		pnetdev->netdev_ops->ndo_open(pnetdev);
	padapter->suspended = false;
	rtl871x_intf_resume(padapter);
	return 0;
}
#endif

static struct drv_priv drvpriv = {
	.r871xu_drv.name = "r8712u",
	.r871xu_drv.id_table = rtl871x_usb_id_tbl,
	.r871xu_drv.probe = r871xu_drv_init,
	.r871xu_drv.disconnect = r871xu_dev_remove,
#ifdef CONFIG_PM
	.r871xu_drv.suspend = r871x_suspend,
	.r871xu_drv.resume = r871x_resume,
#endif
};

static uint r8712_usb_dvobj_init(struct _adapter *padapter)
{
	uint	status = _SUCCESS;
	struct	usb_host_interface		*phost_iface;
	struct	usb_interface_descriptor	*piface_desc;
	struct dvobj_priv *pdvobjpriv = &padapter->dvobjpriv;
	struct usb_device *pusbd = pdvobjpriv->pusbdev;

	pdvobjpriv->padapter = padapter;
	padapter->eeprom_address_size = 6;
	phost_iface = pintf->cur_altsetting;
	piface_desc = &phost_iface->desc;
	pdvobjpriv->nr_endpoint = piface_desc->bNumEndpoints;
	if (pusbd->speed == USB_SPEED_HIGH) {
		pdvobjpriv->ishighspeed = true;
		dev_info(&pusbd->dev, "r8712u: USB_SPEED_HIGH with %d endpoints\n",
			 pdvobjpriv->nr_endpoint);
	} else {
		pdvobjpriv->ishighspeed = false;
		dev_info(&pusbd->dev, "r8712u: USB_SPEED_LOW with %d endpoints\n",
			 pdvobjpriv->nr_endpoint);
	}
	if ((r8712_alloc_io_queue(padapter)) == _FAIL)
		status = _FAIL;
	return status;
}

static void r8712_usb_dvobj_deinit(struct _adapter *padapter)
{
}

void rtl871x_intf_stop(struct _adapter *padapter)
{
	/*disable_hw_interrupt*/
	if (!padapter->surprise_removed) {
		/*device still exists, so driver can do i/o operation
		 * TODO:
		 */
	}

	/* cancel in irp */
	if (padapter->dvobjpriv.inirp_deinit)
		padapter->dvobjpriv.inirp_deinit(padapter);
	/* cancel out irp */
	r8712_usb_write_port_cancel(padapter);
	/* TODO:cancel other irps */
}

void r871x_dev_unload(struct _adapter *padapter)
{
	if (padapter->bup) {
		/*s1.*/
		padapter->driver_stopped = true;

		/*s3.*/
		rtl871x_intf_stop(padapter);

		/*s4.*/
		r8712_stop_drv_threads(padapter);

		/*s5.*/
		if (!padapter->surprise_removed) {
			padapter->hw_init_completed = false;
			rtl8712_hal_deinit(padapter);
		}

		/*s6.*/
		if (padapter->dvobj_deinit)
			padapter->dvobj_deinit(padapter);
		padapter->bup = false;
	}
}

static void disable_ht_for_spec_devid(const struct usb_device_id *pdid,
				      struct _adapter *padapter)
{
	u16 vid, pid;
	u32 flags;
	int i;
	int num = ARRAY_SIZE(specific_device_id_tbl);

	for (i = 0; i < num; i++) {
		vid = specific_device_id_tbl[i].idVendor;
		pid = specific_device_id_tbl[i].idProduct;
		flags = specific_device_id_tbl[i].flags;

		if ((pdid->idVendor == vid) && (pdid->idProduct == pid) &&
		    (flags & SPEC_DEV_ID_DISABLE_HT)) {
			padapter->registrypriv.ht_enable = 0;
			padapter->registrypriv.cbw40_enable = 0;
			padapter->registrypriv.ampdu_enable = 0;
		}
	}
}

static const struct device_type wlan_type = {
	.name = "wlan",
};

/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located a card for us
 * to support. We accept the new device by returning 0.
 */
static int r871xu_drv_init(struct usb_interface *pusb_intf,
			   const struct usb_device_id *pdid)
{
	uint status;
	struct _adapter *padapter = NULL;
	struct dvobj_priv *pdvobjpriv;
	struct net_device *pnetdev;
	struct usb_device *udev;

	/* In this probe function, O.S. will provide the usb interface pointer
	 * to driver. We have to increase the reference count of the usb device
	 * structure by using the usb_get_dev function.
	 */
	udev = interface_to_usbdev(pusb_intf);
	usb_get_dev(udev);
	pintf = pusb_intf;
	/* step 1. */
	pnetdev = r8712_init_netdev();
	if (!pnetdev)
		goto put_dev;
	padapter = netdev_priv(pnetdev);
	disable_ht_for_spec_devid(pdid, padapter);
	pdvobjpriv = &padapter->dvobjpriv;
	pdvobjpriv->padapter = padapter;
	padapter->dvobjpriv.pusbdev = udev;
	padapter->pusb_intf = pusb_intf;
	usb_set_intfdata(pusb_intf, pnetdev);
	SET_NETDEV_DEV(pnetdev, &pusb_intf->dev);
	pnetdev->dev.type = &wlan_type;
	/* step 2. */
	padapter->dvobj_init = r8712_usb_dvobj_init;
	padapter->dvobj_deinit = r8712_usb_dvobj_deinit;
	padapter->halpriv.hal_bus_init = r8712_usb_hal_bus_init;
	padapter->dvobjpriv.inirp_init = r8712_usb_inirp_init;
	padapter->dvobjpriv.inirp_deinit = r8712_usb_inirp_deinit;
	/* step 3.
	 * initialize the dvobj_priv
	 */

	status = padapter->dvobj_init(padapter);
	if (status != _SUCCESS)
		goto free_netdev;

	/* step 4. */
	status = r8712_init_drv_sw(padapter);
	if (status)
		goto dvobj_deinit;
	/* step 5. read efuse/eeprom data and get mac_addr */
	{
		int i, offset;
		u8 mac[6];
		u8 tmpU1b, AutoloadFail, eeprom_CustomerID;
		u8 *pdata = padapter->eeprompriv.efuse_eeprom_data;

		tmpU1b = r8712_read8(padapter, EE_9346CR);/*CR9346*/

		/* To check system boot selection.*/
		dev_info(&udev->dev, "r8712u: Boot from %s: Autoload %s\n",
			 (tmpU1b & _9356SEL) ? "EEPROM" : "EFUSE",
			 (tmpU1b & _EEPROM_EN) ? "OK" : "Failed");

		/* To check autoload success or not.*/
		if (tmpU1b & _EEPROM_EN) {
			AutoloadFail = true;
			/* The following operations prevent Efuse leakage by
			 * turning on 2.5V.
			 */
			tmpU1b = r8712_read8(padapter, EFUSE_TEST + 3);
			r8712_write8(padapter, EFUSE_TEST + 3, tmpU1b | 0x80);
			msleep(20);
			r8712_write8(padapter, EFUSE_TEST + 3,
				     (tmpU1b & (~BIT(7))));

			/* Retrieve Chip version.
			 * Recognize IC version by Reg0x4 BIT15.
			 */
			tmpU1b = (u8)((r8712_read32(padapter, PMC_FSM) >> 15) &
						    0x1F);
			if (tmpU1b == 0x3)
				padapter->registrypriv.chip_version =
				     RTL8712_3rdCUT;
			else
				padapter->registrypriv.chip_version =
				     (tmpU1b >> 1) + 1;
			switch (padapter->registrypriv.chip_version) {
			case RTL8712_1stCUT:
			case RTL8712_2ndCUT:
			case RTL8712_3rdCUT:
				break;
			default:
				padapter->registrypriv.chip_version =
				     RTL8712_2ndCUT;
				break;
			}

			for (i = 0, offset = 0; i < 128; i += 8, offset++)
				r8712_efuse_pg_packet_read(padapter, offset,
						     &pdata[i]);

			if (!r8712_initmac || !mac_pton(r8712_initmac, mac)) {
				/* Use the mac address stored in the Efuse
				 * offset = 0x12 for usb in efuse
				 */
				ether_addr_copy(mac, &pdata[0x12]);
			}
			eeprom_CustomerID = pdata[0x52];
			switch (eeprom_CustomerID) {
			case EEPROM_CID_ALPHA:
				padapter->eeprompriv.CustomerID =
						 RT_CID_819x_ALPHA;
				break;
			case EEPROM_CID_CAMEO:
				padapter->eeprompriv.CustomerID =
						 RT_CID_819x_CAMEO;
				break;
			case EEPROM_CID_SITECOM:
				padapter->eeprompriv.CustomerID =
						 RT_CID_819x_Sitecom;
				break;
			case EEPROM_CID_COREGA:
				padapter->eeprompriv.CustomerID =
						 RT_CID_COREGA;
				break;
			case EEPROM_CID_Senao:
				padapter->eeprompriv.CustomerID =
						 RT_CID_819x_Senao;
				break;
			case EEPROM_CID_EDIMAX_BELKIN:
				padapter->eeprompriv.CustomerID =
						 RT_CID_819x_Edimax_Belkin;
				break;
			case EEPROM_CID_SERCOMM_BELKIN:
				padapter->eeprompriv.CustomerID =
						 RT_CID_819x_Sercomm_Belkin;
				break;
			case EEPROM_CID_WNC_COREGA:
				padapter->eeprompriv.CustomerID =
						 RT_CID_819x_WNC_COREGA;
				break;
			case EEPROM_CID_WHQL:
				break;
			case EEPROM_CID_NetCore:
				padapter->eeprompriv.CustomerID =
						 RT_CID_819x_Netcore;
				break;
			case EEPROM_CID_CAMEO1:
				padapter->eeprompriv.CustomerID =
						 RT_CID_819x_CAMEO1;
				break;
			case EEPROM_CID_CLEVO:
				padapter->eeprompriv.CustomerID =
						 RT_CID_819x_CLEVO;
				break;
			default:
				padapter->eeprompriv.CustomerID =
						 RT_CID_DEFAULT;
				break;
			}
			dev_info(&udev->dev, "r8712u: CustomerID = 0x%.4x\n",
				 padapter->eeprompriv.CustomerID);
			/* Led mode */
			switch (padapter->eeprompriv.CustomerID) {
			case RT_CID_DEFAULT:
			case RT_CID_819x_ALPHA:
			case RT_CID_819x_CAMEO:
				padapter->ledpriv.LedStrategy = SW_LED_MODE1;
				padapter->ledpriv.bRegUseLed = true;
				break;
			case RT_CID_819x_Sitecom:
				padapter->ledpriv.LedStrategy = SW_LED_MODE2;
				padapter->ledpriv.bRegUseLed = true;
				break;
			case RT_CID_COREGA:
			case RT_CID_819x_Senao:
				padapter->ledpriv.LedStrategy = SW_LED_MODE3;
				padapter->ledpriv.bRegUseLed = true;
				break;
			case RT_CID_819x_Edimax_Belkin:
				padapter->ledpriv.LedStrategy = SW_LED_MODE4;
				padapter->ledpriv.bRegUseLed = true;
				break;
			case RT_CID_819x_Sercomm_Belkin:
				padapter->ledpriv.LedStrategy = SW_LED_MODE5;
				padapter->ledpriv.bRegUseLed = true;
				break;
			case RT_CID_819x_WNC_COREGA:
				padapter->ledpriv.LedStrategy = SW_LED_MODE6;
				padapter->ledpriv.bRegUseLed = true;
				break;
			default:
				padapter->ledpriv.LedStrategy = SW_LED_MODE0;
				padapter->ledpriv.bRegUseLed = false;
				break;
			}
		} else {
			AutoloadFail = false;
		}
		if (((mac[0] == 0xff) && (mac[1] == 0xff) &&
		     (mac[2] == 0xff) && (mac[3] == 0xff) &&
		     (mac[4] == 0xff) && (mac[5] == 0xff)) ||
		    ((mac[0] == 0x00) && (mac[1] == 0x00) &&
		     (mac[2] == 0x00) && (mac[3] == 0x00) &&
		     (mac[4] == 0x00) && (mac[5] == 0x00)) ||
		     (!AutoloadFail)) {
			mac[0] = 0x00;
			mac[1] = 0xe0;
			mac[2] = 0x4c;
			mac[3] = 0x87;
			mac[4] = 0x00;
			mac[5] = 0x00;
		}
		if (r8712_initmac) {
			/* Make sure the user did not select a multicast
			 * address by setting bit 1 of first octet.
			 */
			mac[0] &= 0xFE;
			dev_info(&udev->dev,
				"r8712u: MAC Address from user = %pM\n", mac);
		} else {
			dev_info(&udev->dev,
				"r8712u: MAC Address from efuse = %pM\n", mac);
		}
		eth_hw_addr_set(pnetdev, mac);
	}
	/* step 6. Load the firmware asynchronously */
	if (rtl871x_load_fw(padapter))
		goto deinit_drv_sw;
	spin_lock_init(&padapter->lock_rx_ff0_filter);
	mutex_init(&padapter->mutex_start);
	return 0;

deinit_drv_sw:
	r8712_free_drv_sw(padapter);
dvobj_deinit:
	padapter->dvobj_deinit(padapter);
free_netdev:
	free_netdev(pnetdev);
put_dev:
	usb_put_dev(udev);
	usb_set_intfdata(pusb_intf, NULL);
	return -ENODEV;
}

/* rmmod module & unplug(SurpriseRemoved) will call r871xu_dev_remove()
 * => how to recognize both
 */
static void r871xu_dev_remove(struct usb_interface *pusb_intf)
{
	struct net_device *pnetdev = usb_get_intfdata(pusb_intf);
	struct usb_device *udev = interface_to_usbdev(pusb_intf);
	struct _adapter *padapter = netdev_priv(pnetdev);

	/* never exit with a firmware callback pending */
	wait_for_completion(&padapter->rtl8712_fw_ready);
	if (pnetdev->reg_state != NETREG_UNINITIALIZED)
		unregister_netdev(pnetdev); /* will call netdev_close() */
	usb_set_intfdata(pusb_intf, NULL);
	release_firmware(padapter->fw);
	if (drvpriv.drv_registered)
		padapter->surprise_removed = true;
	r8712_flush_rwctrl_works(padapter);
	r8712_flush_led_works(padapter);
	udelay(1);
	/* Stop driver mlme relation timer */
	r8712_stop_drv_timers(padapter);
	r871x_dev_unload(padapter);
	r8712_free_drv_sw(padapter);
	free_netdev(pnetdev);

	/* decrease the reference count of the usb device structure
	 * when disconnect
	 */
	usb_put_dev(udev);

	/* If we didn't unplug usb dongle and remove/insert module, driver
	 * fails on sitesurvey for the first time when device is up.
	 * Reset usb port for sitesurvey fail issue.
	 */
	if (udev->state != USB_STATE_NOTATTACHED)
		usb_reset_device(udev);
}

static int __init r8712u_drv_entry(void)
{
	drvpriv.drv_registered = true;
	return usb_register(&drvpriv.r871xu_drv);
}

static void __exit r8712u_drv_halt(void)
{
	drvpriv.drv_registered = false;
	usb_deregister(&drvpriv.r871xu_drv);
}

module_init(r8712u_drv_entry);
module_exit(r8712u_drv_halt);
