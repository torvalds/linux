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
#define _HCI_INTF_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>
#include <hal_init.h>
#include <rtl8712_efuse.h>
#include <version.h>

#ifndef CONFIG_USB_HCI

#error "CONFIG_USB_HCI shall be on!\n"

#endif

#include <usb_vendor_req.h>
#include <usb_ops.h>
#include <usb_osintf.h>
#include <usb_hal.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

#error "Shall be Linux or Windows, but not both!\n"

#endif

#ifdef CONFIG_80211N_HT
extern int ht_enable;
extern int cbw40_enable;
extern int ampdu_enable;//for enable tx_ampdu
#endif

static struct usb_interface *pintf;

extern u32 start_drv_threads(_adapter *padapter);
extern void stop_drv_threads (_adapter *padapter);
extern void stop_drv_timers (_adapter *padapter);
extern u8 init_drv_sw(_adapter *padapter);
extern u8 free_drv_sw(_adapter *padapter);
extern struct net_device *init_netdev(void);
extern char* initmac;
extern u8* g_pallocated_recv_buf;

void r871x_dev_unload(_adapter *padapter);

static int r871xu_drv_init(struct usb_interface *pusb_intf,const struct usb_device_id *pdid);
static void r871xu_dev_remove(struct usb_interface *pusb_intf);

static struct usb_device_id rtl871x_usb_id_tbl[] ={
/* RTL8188SU */
	/* Realtek */
	{USB_DEVICE(0x0BDA, 0x8171)},
	{USB_DEVICE(0x0bda, 0x8173)}, // =
	{USB_DEVICE(0x0bda, 0x8712)}, // =
	{USB_DEVICE(0x0bda, 0x8713)}, // =
	{USB_DEVICE(0x0bda, 0xC512)}, // =
	/* Abocom */
	{USB_DEVICE(0x07B8, 0x8188)},
	/* ASUS */
	{USB_DEVICE(0x0B05, 0x1786)},
	{USB_DEVICE(0x0B05, 0x1791)}, // 11n mode disable -
	/* Belkin */
	{USB_DEVICE(0x050D, 0x945A)},
	/* Corega */
	{USB_DEVICE(0x07AA, 0x0047)},
	/* D-Link */
	{USB_DEVICE(0x2001, 0x3306)},
	{USB_DEVICE(0x07D1, 0x3306)}, // 11n mode disable *
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
	/* itecom */
	{USB_DEVICE(0x0DF6, 0x0057)},
	{USB_DEVICE(0x0DF6, 0x0045)},
	{USB_DEVICE(0x0DF6, 0x0059)}, // 11n mode disable *
	{USB_DEVICE(0x0DF6, 0x004B)},
	{USB_DEVICE(0x0DF6, 0x0063)},
	/* Sweex */
	{USB_DEVICE(0x177F, 0x0154)},
	/* Thinkware */
	{USB_DEVICE(0x0BDA, 0x5077)},
	/* Toshiba */
	{USB_DEVICE(0x1690, 0x0752)},
	/* - */
	{USB_DEVICE(0x20F4, 0x646B)},
	{USB_DEVICE(0x083A, 0xC512)}, // =

/* RTL8191SU */
	/* Realtek */
	{USB_DEVICE(0x0BDA, 0x8172)},
	/* Amigo */
	{USB_DEVICE(0x0EB0, 0x9061)},
	/* ASUS/EKB */
	{USB_DEVICE(0x0BDA, 0x8172)},
	{USB_DEVICE(0x13D3, 0x3323)},
	{USB_DEVICE(0x13D3, 0x3311)}, // 11n mode disable -
	{USB_DEVICE(0x13D3, 0x3342)},
	/* ASUS/EKBLenovo */
	{USB_DEVICE(0x13D3, 0x3333)},
	{USB_DEVICE(0x13D3, 0x3334)},
	{USB_DEVICE(0x13D3, 0x3335)}, // 11n mode disable *
	{USB_DEVICE(0x13D3, 0x3336)}, // 11n mode disable *
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
	{USB_DEVICE(0x13D3, 0x3340)}, // 11n mode disable *
	{USB_DEVICE(0x13D3, 0x3341)}, // 11n mode disable *
	{USB_DEVICE(0x13D3, 0x3310)},
	{USB_DEVICE(0x13D3, 0x3325)},

/* RTL8192SU */
	/* Realtek */
	{USB_DEVICE(0x0BDA, 0x8174)},
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


static struct specific_device_id specific_device_id_tbl[] = {
	{.idVendor=0x0b05, .idProduct=0x1791, .flags=SPEC_DEV_ID_DISABLE_HT}, // 0x0B05, 0x1791 -
	{.idVendor=0x13D3, .idProduct=0x3311, .flags=SPEC_DEV_ID_DISABLE_HT}, // 0x13D3, 0x3311 -
		
	{.idVendor=0x0DF6, .idProduct=0x0059, .flags=SPEC_DEV_ID_DISABLE_HT}, // 0x0DF6, 0x0059 *
	{.idVendor=0x07D1, .idProduct=0x3306, .flags=SPEC_DEV_ID_DISABLE_HT}, // 0x07D1, 0x3306 *
	{.idVendor=0x13D3, .idProduct=0x3335, .flags=SPEC_DEV_ID_DISABLE_HT}, // 0x13D3, 0x3335 *
	{.idVendor=0x13D3, .idProduct=0x3336, .flags=SPEC_DEV_ID_DISABLE_HT}, // 0x13D3, 0x3336 *
	{.idVendor=0x13D3, .idProduct=0x3340, .flags=SPEC_DEV_ID_DISABLE_HT}, // 0x13D3, 0x3340 *
	{.idVendor=0x13D3, .idProduct=0x3341, .flags=SPEC_DEV_ID_DISABLE_HT}, // 0x13D3, 0x3341 *
	{}
};

typedef struct _driver_priv{
      
	struct usb_driver r871xu_drv;
	int drv_registered;	

}drv_priv, *pdrv_priv;


static drv_priv drvpriv = {	
		
	.r871xu_drv.name="r871x_usb_drv",
	.r871xu_drv.id_table=rtl871x_usb_id_tbl,
	.r871xu_drv.probe=r871xu_drv_init,
	.r871xu_drv.disconnect=r871xu_dev_remove,
	.r871xu_drv.suspend=NULL,
	.r871xu_drv.resume=NULL,
};	

MODULE_DEVICE_TABLE(usb, rtl871x_usb_id_tbl);

uint usb_dvobj_init(_adapter * padapter)
{
	int i;
	u8 val8;
	u32 	blocksz;
	uint	status=_SUCCESS;
	
	struct	usb_device_descriptor 		*pdev_desc;

	struct	usb_host_config			*phost_conf;
	struct	usb_config_descriptor 		*pconf_desc;

	struct	usb_host_interface		*phost_iface;
	struct	usb_interface_descriptor	*piface_desc;
	
	struct	usb_host_endpoint		*phost_endp;
	struct	usb_endpoint_descriptor		*pendp_desc;
	
	
	struct dvobj_priv *pdvobjpriv=&padapter->dvobjpriv;
	struct usb_device *pusbd=pdvobjpriv->pusbdev;

	PURB urb = NULL;

_func_enter_;
	
	pdvobjpriv->padapter=padapter;
	padapter->EepromAddressSize = 6;

	pdev_desc = &pusbd->descriptor;

#if 0
	printk("\n8712_usb_device_descriptor:\n");
	printk("bLength=%x\n", pdev_desc->bLength);
	printk("bDescriptorType=%x\n", pdev_desc->bDescriptorType);
	printk("bcdUSB=%x\n", pdev_desc->bcdUSB);
	printk("bDeviceClass=%x\n", pdev_desc->bDeviceClass);
	printk("bDeviceSubClass=%x\n", pdev_desc->bDeviceSubClass);
	printk("bDeviceProtocol=%x\n", pdev_desc->bDeviceProtocol);
	printk("bMaxPacketSize0=%x\n", pdev_desc->bMaxPacketSize0);
	printk("idVendor=%x\n", pdev_desc->idVendor);
	printk("idProduct=%x\n", pdev_desc->idProduct);
	printk("bcdDevice=%x\n", pdev_desc->bcdDevice);	
	printk("iManufacturer=%x\n", pdev_desc->iManufacturer);
	printk("iProduct=%x\n", pdev_desc->iProduct);
	printk("iSerialNumber=%x\n", pdev_desc->iSerialNumber);
	printk("bNumConfigurations=%x\n", pdev_desc->bNumConfigurations);
#endif	
	
	phost_conf = pusbd->actconfig;
	pconf_desc = &phost_conf->desc;
	
#if 0	
	printk("\n8712_usb_configuration_descriptor:\n");
	printk("bLength=%x\n", pconf_desc->bLength);
	printk("bDescriptorType=%x\n", pconf_desc->bDescriptorType);
	printk("wTotalLength=%x\n", pconf_desc->wTotalLength);
	printk("bNumInterfaces=%x\n", pconf_desc->bNumInterfaces);
	printk("bConfigurationValue=%x\n", pconf_desc->bConfigurationValue);
	printk("iConfiguration=%x\n", pconf_desc->iConfiguration);
	printk("bmAttributes=%x\n", pconf_desc->bmAttributes);
	printk("bMaxPower=%x\n", pconf_desc->bMaxPower);
#endif

	//printk("\n/****** num of altsetting = (%d) ******/\n", pintf->num_altsetting);
		
	phost_iface = &pintf->altsetting[0];
	piface_desc = &phost_iface->desc;

#if 0
	printk("\n8712_usb_interface_descriptor:\n");
	printk("bLength=%x\n", piface_desc->bLength);
	printk("bDescriptorType=%x\n", piface_desc->bDescriptorType);
	printk("bInterfaceNumber=%x\n", piface_desc->bInterfaceNumber);
	printk("bAlternateSetting=%x\n", piface_desc->bAlternateSetting);
	printk("bNumEndpoints=%x\n", piface_desc->bNumEndpoints);
	printk("bInterfaceClass=%x\n", piface_desc->bInterfaceClass);
	printk("bInterfaceSubClass=%x\n", piface_desc->bInterfaceSubClass);
	printk("bInterfaceProtocol=%x\n", piface_desc->bInterfaceProtocol);
	printk("iInterface=%x\n", piface_desc->iInterface);	
#endif
	
	pdvobjpriv->nr_endpoint = piface_desc->bNumEndpoints;
	

	//printk("\ndump 8712_usb_endpoint_descriptor:\n");

	for(i=0; i<pdvobjpriv->nr_endpoint; i++)
	{
		phost_endp = phost_iface->endpoint+i;
		if(phost_endp)
		{
			pendp_desc = &phost_endp->desc;
		
			printk("\n8712_usb_endpoint_descriptor(%d):\n", i);
			printk("bLength=%x\n",pendp_desc->bLength);
			printk("bDescriptorType=%x\n",pendp_desc->bDescriptorType);
			printk("bEndpointAddress=%x\n",pendp_desc->bEndpointAddress);
			//printk("bmAttributes=%x\n",pendp_desc->bmAttributes);
			//printk("wMaxPacketSize=%x\n",pendp_desc->wMaxPacketSize);
			printk("wMaxPacketSize=%x\n",le16_to_cpu(pendp_desc->wMaxPacketSize));
			printk("bInterval=%x\n",pendp_desc->bInterval);
			//printk("bRefresh=%x\n",pendp_desc->bRefresh);
			//printk("bSynchAddress=%x\n",pendp_desc->bSynchAddress);	
		}	

	}

	printk("\n");
	
	if (pusbd->speed==USB_SPEED_HIGH)
	{
                pdvobjpriv->ishighspeed = _TRUE;
                printk("8712u : USB_SPEED_HIGH\n");
	}
	else
	{
		 pdvobjpriv->ishighspeed = _FALSE;
                 printk("8712u: NON USB_SPEED_HIGH\n");
	}
	  	
			
	printk("nr_endpoint=%d\n", pdvobjpriv->nr_endpoint);
	  	
	if ( (alloc_io_queue(padapter)) == _FAIL)
	{
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,(" \n Can't init io_reqs\n"));
		status = _FAIL;			
	}	
	
	_init_sema(&(padapter->dvobjpriv.usb_suspend_sema), 0);


_func_exit_;
	
	return status;


}

void usb_dvobj_deinit(_adapter * padapter){
	
	struct dvobj_priv *pdvobjpriv=&padapter->dvobjpriv;

	_func_enter_;


	_func_exit_;
}

void rtl871x_intf_stop(_adapter *padapter)
{
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+rtl871x_intf_stop\n"));
	
	//disabel_hw_interrupt
	if(padapter->bSurpriseRemoved == _FALSE)
	{
		//device still exists, so driver can do i/o operation
		//TODO:
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("SurpriseRemoved==_FALSE\n"));
	}
	
	//cancel in irp
	if(padapter->dvobjpriv.inirp_deinit !=NULL)
	{	
		padapter->dvobjpriv.inirp_deinit(padapter);	
	}	

	//cancel out irp
	usb_write_port_cancel(padapter);


	//todo:cancel other irps

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-rtl871x_intf_stop\n"));

}

#if 0
void r871x_dev_unload(_adapter *padapter)
{
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+r871x_dev_unload\n"));

	if(padapter->bSurpriseRemoved == _TRUE)
	{		
		padapter->bDriverStopped = _TRUE;
	
		RT_TRACE(_module_os_intfs_c_, _drv_info_, ("padapter->bSurpriseRemoved==_TRUE\n"));
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("unload -> surprise removed\n"));
		
		free_drv_sw(padapter);
		
		return;

	}
					
       padapter->bDriverStopped = _TRUE;
	padapter->bSurpriseRemoved = _TRUE;   

	rtl871x_intf_stop(padapter);

	stop_drv_threads(padapter);

	rtl871x_hal_deinit(padapter);

	
	if(padapter->dvobj_deinit)
	{
		padapter->dvobj_deinit(padapter);
		
	}else
	{
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize hcipriv.hci_priv_init error!!!\n"));
	}			
	
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-r871x_dev_unload\n"));
}
#else
void r871x_dev_unload(_adapter *padapter)
{
	struct net_device *pnetdev= (struct net_device*)padapter->pnetdev;

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+r871x_dev_unload\n"));

	if(padapter->bup == _TRUE)
	{
		printk("+r871x_dev_unload\n");
		//s1.
/*		if(pnetdev)   
     		{
        		netif_carrier_off(pnetdev);
     	  		netif_stop_queue(pnetdev);
     		}
		
		//s2.
		//s2-1.  issue disassoc_cmd to fw
		disassoc_cmd(padapter);	
		//s2-2.  indicate disconnect to os
		indicate_disconnect(padapter);				
		//s2-3. 
	       free_assoc_resources(padapter);	
		//s2-4.
		free_network_queue(padapter);*/

		padapter->bDriverStopped = _TRUE;
	
		//s3.
		rtl871x_intf_stop(padapter);

		//s4.
		stop_drv_threads(padapter);


		//s5.
		if(padapter->bSurpriseRemoved == _FALSE)
		{
			printk("r871x_dev_unload()->rtl871x_hal_deinit()\n");
			rtl871x_hal_deinit(padapter);

			//padapter->bSurpriseRemoved = _TRUE;
		}	

		//s6.	
		if(padapter->dvobj_deinit)
		{
			padapter->dvobj_deinit(padapter);
		
		}
		else
		{
			RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize hcipriv.hci_priv_init error!!!\n"));
		}			
		
		padapter->bup = _FALSE;

	}
	else
	{
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("r871x_dev_unload():padapter->bup == _FALSE\n" ));
	}
				
	printk("-r871x_dev_unload\n");		
	
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-r871x_dev_unload\n"));
	
}
#endif

static void disable_ht_for_spec_devid(const struct usb_device_id *pdid)
{
#ifdef CONFIG_80211N_HT
	u16 vid, pid;
	u32 flags;
	int i;	
	int num = sizeof(specific_device_id_tbl)/sizeof(struct specific_device_id);

	for(i=0; i<num; i++)
	{
		vid = specific_device_id_tbl[i].idVendor;
		pid = specific_device_id_tbl[i].idProduct;
		flags = specific_device_id_tbl[i].flags;

		if((pdid->idVendor==vid) && (pdid->idProduct==pid) && (flags&SPEC_DEV_ID_DISABLE_HT))
		{
			 ht_enable = 0;            
			 cbw40_enable = 0;            
			 ampdu_enable = 0;			
		}
	}
#endif
}

u8 key_char2num(u8 ch)
{
    if((ch>='0')&&(ch<='9'))
        return ch - '0';
    else if ((ch>='a')&&(ch<='f'))
        return ch - 'a' + 10;
    else if ((ch>='A')&&(ch<='F'))
        return ch - 'A' + 10;
    else
	 return 0xff;
}

u8 key_2char2num(u8 hch, u8 lch)
{
    return ((key_char2num(hch) << 4) | key_char2num(lch));
}


/*
 * drv_init() - a device potentially for us
 *
 * notes: drv_init() is called when the bus driver has located a card for us to support.
 *        We accept the new device by returning 0.
*/
static int r871xu_drv_init(struct usb_interface *pusb_intf,const struct usb_device_id *pdid)
{  	
  	uint status;	 
	_adapter *padapter = NULL;
  	struct dvobj_priv *pdvobjpriv;
	struct net_device *pnetdev;
	struct usb_device	*udev;

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+871x - drv_init\n"));

	printk( "==DriverVersion: %s==\n", DRVER );

	//2009.8.13, by Thomas
	// In this probe function, O.S. will provide the usb interface pointer to driver.
	// We have to increase the reference count of the usb device structure by using the usb_get_dev function.
	udev = interface_to_usbdev(pusb_intf);
	usb_get_dev(udev);

	pintf = pusb_intf;

#ifdef CONFIG_80211N_HT
	//step 0.	
	disable_ht_for_spec_devid(pdid);
#endif

	//step 1.
	pnetdev = init_netdev();
	if (!pnetdev)
		goto error;	
	
	padapter = rtw_netdev_priv(pnetdev);
	pdvobjpriv = &padapter->dvobjpriv;	
	pdvobjpriv->padapter = padapter;

	padapter->dvobjpriv.pusbdev = udev;

#ifdef CONFIG_IOCTL_CFG80211
	rtw_wdev_alloc(padapter, &pusb_intf->dev);
#endif //CONFIG_IOCTL_CFG80211

	usb_set_intfdata(pusb_intf, pnetdev);	
	SET_NETDEV_DEV(pnetdev, &pusb_intf->dev);

	//step 2.
	padapter->dvobj_init=&usb_dvobj_init;
	padapter->dvobj_deinit=&usb_dvobj_deinit;
	padapter->halpriv.hal_bus_init=&usb_hal_bus_init;
	padapter->halpriv.hal_bus_deinit=&usb_hal_bus_deinit;	
	padapter->dvobjpriv.inirp_init=&usb_inirp_init;
	padapter->dvobjpriv.inirp_deinit=&usb_inirp_deinit;

	//step 3.
	//initialize the dvobj_priv 		
	if(padapter->dvobj_init ==NULL){
			RT_TRACE(_module_hci_intfs_c_,_drv_err_,("\n Initialize dvobjpriv.dvobj_init error!!!\n"));
			goto error;
	}else{
	
		status=padapter->dvobj_init(padapter);
		if (status != _SUCCESS) {
			RT_TRACE(_module_hci_intfs_c_,_drv_err_,("\n initialize device object priv Failed!\n"));			
			goto error;
		} 
	}

	//step 4.
	status = init_drv_sw(padapter);	
	if(status ==_FAIL){
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Initialize driver software resource Failed!\n"));	
		goto error;					
	}	

#if 1
	//step 5. read efuse/eeprom data and get mac_addr
	{
		int i, offset;
		u8 mac[6];
		u8	tmpU1b, AutoloadFail, eeprom_CustomerID;
		u8 *pdata = padapter->eeprompriv.efuse_eeprom_data;

		tmpU1b = read8(padapter, EE_9346CR);//CR9346	

		// To check system boot selection.
		if (tmpU1b & _9356SEL)
		{
			printk("Boot from EEPROM\n");
		}
		else 
		{
			printk("Boot from EFUSE\n");
		}	

		// To check autoload success or not.
		if (tmpU1b & _EEPROM_EN)
		{
			printk("Autoload OK!!\n");
			AutoloadFail = _TRUE;

			//tmpU1b = read8(padapter, EFUSE_TEST+3);
			//write8(padapter, EFUSE_TEST+3, tmpU1b|0x80);
			//mdelay_os(1);
			//write8(padapter, EFUSE_TEST+3, (tmpU1b&(~ BIT(7))));

			// Retrieve Chip version.
			// 20090915 Joseph: Recognize IC version by Reg0x4 BIT15.
			tmpU1b = (u8)((read32(padapter, PMC_FSM)>>15)&0x1F);

			if(tmpU1b==0x3)
				padapter->registrypriv.chip_version = RTL8712_3rdCUT;
			else
				padapter->registrypriv.chip_version = (tmpU1b>>1)+1;

			switch(padapter->registrypriv.chip_version)
			{
				case RTL8712_1stCUT:
					RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Chip Version ID: RTL8712_1stCUT.\n"));
					break;
				case RTL8712_2ndCUT:
					RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Chip Version ID: RTL8712_2ndCUT.\n"));
					break;
				case RTL8712_3rdCUT:
					RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Chip Version ID: RTL8712_3rdCUT.\n"));
					break;
				default:
					RT_TRACE(_module_hci_intfs_c_,_drv_err_,("Unknown Chip Version!!\n"));
					padapter->registrypriv.chip_version = RTL8712_2ndCUT;
					break;
			}
			
#if 1

			for(i=0, offset=0 ; i<128; i+=8, offset++)
			{
				efuse_pg_packet_read(padapter, offset, &pdata[i]);			
			}

			if ( initmac )
			{	//	Users specify the mac address
				int jj,kk;

				for( jj = 0, kk = 0; jj < ETH_ALEN; jj++, kk += 3 )
				{
	            			mac[jj] = key_2char2num(initmac[kk], initmac[kk+ 1]);
				}
			}
			else
			{	//	Use the mac address stored in the Efuse
			 	_memcpy(mac, &pdata[0x12], ETH_ALEN);//offset = 0x12 for usb in efuse 
			}


			eeprom_CustomerID = pdata[0x52];
			switch(eeprom_CustomerID)
			{
				case EEPROM_CID_ALPHA:
					padapter->eeprompriv.CustomerID = RT_CID_819x_ALPHA;
					break;
					
				case EEPROM_CID_CAMEO:
					padapter->eeprompriv.CustomerID = RT_CID_819x_CAMEO;
					break;			
					
				case EEPROM_CID_SITECOM:
					padapter->eeprompriv.CustomerID = RT_CID_819x_Sitecom;
					break;	
					
				case EEPROM_CID_COREGA:
					padapter->eeprompriv.CustomerID = RT_CID_COREGA;						
					break;			
			
				case EEPROM_CID_Senao:
					padapter->eeprompriv.CustomerID = RT_CID_819x_Senao;
					break;
		
				case EEPROM_CID_EDIMAX_BELKIN:
					padapter->eeprompriv.CustomerID = RT_CID_819x_Edimax_Belkin;
					break;
		
				case EEPROM_CID_SERCOMM_BELKIN:
					padapter->eeprompriv.CustomerID = RT_CID_819x_Sercomm_Belkin;
					break;
					
				case EEPROM_CID_WNC_COREGA:
					padapter->eeprompriv.CustomerID = RT_CID_819x_WNC_COREGA;
					break;
		
				case EEPROM_CID_WHQL:
					break;
					
				case EEPROM_CID_NetCore:
					padapter->eeprompriv.CustomerID = RT_CID_819x_Netcore;
					break;
		
				case EEPROM_CID_CAMEO1:
					padapter->eeprompriv.CustomerID = RT_CID_819x_CAMEO1;
					break;
					
				case EEPROM_CID_CLEVO:
					padapter->eeprompriv.CustomerID = RT_CID_819x_CLEVO;
					break;
		
				default:
					padapter->eeprompriv.CustomerID = RT_CID_DEFAULT;
					break;
					
			}
			printk("CustomerID = 0x%4x\n", padapter->eeprompriv.CustomerID);

			//
			// Led mode
			// 
			switch(padapter->eeprompriv.CustomerID)
			{
				case RT_CID_DEFAULT:
				case RT_CID_819x_ALPHA:
				case RT_CID_819x_CAMEO:
					padapter->ledpriv.LedStrategy = SW_LED_MODE1;
					padapter->ledpriv.bRegUseLed = _TRUE;
					break;	

				case RT_CID_819x_Sitecom:
					padapter->ledpriv.LedStrategy = SW_LED_MODE2;
					padapter->ledpriv.bRegUseLed = _TRUE;
					break;	

				case RT_CID_COREGA:
				case RT_CID_819x_Senao:
					padapter->ledpriv.LedStrategy = SW_LED_MODE3;
					padapter->ledpriv.bRegUseLed = _TRUE;
					break;			

				case RT_CID_819x_Edimax_Belkin:
					padapter->ledpriv.LedStrategy = SW_LED_MODE4;
					padapter->ledpriv.bRegUseLed = _TRUE;
					break;					

				case RT_CID_819x_Sercomm_Belkin:
					padapter->ledpriv.LedStrategy = SW_LED_MODE5;
					padapter->ledpriv.bRegUseLed = _TRUE;
					break;

				case RT_CID_819x_WNC_COREGA:
					padapter->ledpriv.LedStrategy = SW_LED_MODE6;
					padapter->ledpriv.bRegUseLed = _TRUE;
					break;

				default:
					padapter->ledpriv.LedStrategy = SW_LED_MODE0;
					padapter->ledpriv.bRegUseLed = _FALSE;
					break;			
			}
#endif
		}
		else {
			printk("AutoLoad Fail reported from CR9346!!\n");
			AutoloadFail = _FALSE;
		}

		if(	((mac[0]==0xff) &&(mac[1]==0xff) && (mac[2]==0xff)  && (mac[3]==0xff) &&
			(mac[4]==0xff) &&(mac[5]==0xff) ) || 
			((mac[0]==0x0) &&(mac[1]==0x0) && (mac[2]==0x0)  && (mac[3]==0x0) &&
			(mac[4]==0x0) &&(mac[5]==0x0)) || (AutoloadFail == _FALSE))
		{
		mac[0]=0x00;
		mac[1]=0xe0;
		mac[2]=0x4c;
		mac[3]=0x87;
		mac[4]=0x00;
		mac[5]=0x00;
		}
	
		_memcpy(pnetdev->dev_addr, mac/*padapter->eeprompriv.mac_addr*/, ETH_ALEN);

		 printk("MAC Address from efuse= %x-%x-%x-%x-%x-%x\n", 
			mac[0],mac[1],mac[2],mac[3], mac[4], mac[5]);		
	}	
#endif

	//step 6.
	/* Tell the network stack we exist */
	if (register_netdev(pnetdev) != 0) {
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("register_netdev() failed\n"));
		goto error;
	}
	
	_spinlock_init( &padapter->lockRxFF0Filter );
	
  	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-drv_init - Adapter->bDriverStopped=%d, Adapter->bSurpriseRemoved=%d\n",padapter->bDriverStopped, padapter->bSurpriseRemoved));
  	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-871x_drv - drv_init, success!\n"));
	//printk("-871x_drv - drv_init, success!\n");

#ifdef CONFIG_HOSTAPD_MODE
	hostapd_mode_init(padapter);
#endif	

#ifdef CONFIG_PLATFORM_RTD2880B
	printk("wlan link up\n");
	rtd2885_wlan_netlink_sendMsg("linkup", "8712");	
#endif	

  	return 0;

error:	      

	usb_put_dev(udev);//decrease the reference count of the usb device structure if driver fail on initialzation

	usb_set_intfdata(pusb_intf, NULL);

   	if(padapter->dvobj_deinit==NULL){
		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("\n Initialize dvobjpriv.dvobj_deinit error!!!\n"));
	}else{
		padapter->dvobj_deinit(padapter);
	} 	  

	if(pnetdev)
	{
		//unregister_netdev(pnetdev);
		free_netdev(pnetdev);
	}

	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-871x_sdio - drv_init, fail!\n"));
	//printk("-871x_sdio - drv_init, fail!\n");

	return -ENODEV;

}

/*
 * dev_remove() - our device is being removed
*/
#if 0
static void r871xu_dev_remove(struct usb_interface *pusb_intf)
{	
	_irqL irqL;
	struct net_device *pnetdev=usb_get_intfdata(pusb_intf);	
     _adapter *padapter = (_adapter*)rtw_netdev_priv(pnetdev);
	 struct	mlme_priv *pmlmepriv = &padapter->mlmepriv;

_func_exit_;

	if(padapter)	
	{
       		RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+dev_remove()\n"));
		
		pnetdev= (struct net_device*)padapter->pnetdev;	
      		
		padapter->bSurpriseRemoved = _TRUE;	 
     
    		if(pnetdev)   
     		{
        		netif_carrier_off(pnetdev);
     	  		netif_stop_queue(pnetdev);
     		}

		rtl871x_intf_stop(padapter);			
		stop_drv_threads(padapter);

		// indicate-disconnect if necssary (free all assoc-resources)
		// dis-assoc from assoc_sta (optional)			
		_enter_critical(&pmlmepriv->lock, &irqL);
		if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE) 
		{
			indicate_disconnect(padapter); //will clr Linked_state; before this function, we must have chked whether  issue dis-assoc_cmd or not		
		}
		_exit_critical(&pmlmepriv->lock, &irqL);
			
		//todo:wait until fw has process dis-assoc cmd		


		r871x_dev_unload(padapter);		
			
	}
	
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-dev_remove()\n"));

_func_exit_;	

	return;
	
}
#else
//rmmod module & unplug(SurpriseRemoved) will call r871xu_dev_remove() => how to recognize both
static void r871xu_dev_remove(struct usb_interface *pusb_intf)
{		
	struct net_device *pnetdev=usb_get_intfdata(pusb_intf);	
	struct usb_device	*udev = interface_to_usbdev(pusb_intf);
       _adapter *padapter = (_adapter*)rtw_netdev_priv(pnetdev);
   
_func_exit_;

	usb_set_intfdata(pusb_intf, NULL);

	if(padapter)	
	{

#ifdef CONFIG_IOCTL_CFG80211
		struct wireless_dev *wdev = padapter->rtw_wdev;
#endif //CONFIG_IOCTL_CFG80211

		printk("+r871xu_dev_remove\n");
       	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+dev_remove()\n"));		
      		
#ifdef CONFIG_HOSTAPD_MODE
		hostapd_mode_unload(padapter);
#endif	
			
		if(drvpriv.drv_registered == _TRUE)
		{
			//printk("r871xu_dev_remove():padapter->bSurpriseRemoved == _TRUE\n");
		        padapter->bSurpriseRemoved = _TRUE;	 
		}
		/*else
		{
			//printk("r871xu_dev_remove():module removed\n");
			padapter->hw_init_completed = _FALSE;
		}*/

		if(pnetdev != NULL) {
			unregister_netdev(pnetdev); //will call netdev_close()
		}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	/* kernel 2.4 series */
#else
		flush_scheduled_work();
#endif
		udelay_os(1);

		//Stop driver mlme relation timer
		stop_drv_timers(padapter);

		r871x_dev_unload(padapter);

		free_drv_sw(padapter);

#ifdef CONFIG_IOCTL_CFG80211
		rtw_wdev_free(wdev);
#endif //CONFIG_IOCTL_CFG80211		
	}

	usb_put_dev(udev);//decrease the reference count of the usb device structure when disconnect
		
	//If we didn't unplug usb dongle and remove/insert modlue, driver fails on sitesurvey for the first time when device is up . 
	//Reset usb port for sitesurvey fail issue. 2009.8.13, by Thomas
	if(udev->state != USB_STATE_NOTATTACHED)
		usb_reset_device(udev);
	
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("-dev_remove()\n"));
	//printk("-r871xu_dev_remove, hw_init_completed=%d\n", padapter->hw_init_completed);

#ifdef CONFIG_PLATFORM_RTD2880B
	printk("wlan link down\n");
	rtd2885_wlan_netlink_sendMsg("linkdown", "8712");	
#endif	
	

_func_exit_;	

	return;
	
}
#endif

static int __init r8712u_drv_entry(void)
{
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+r8712u_drv_entry\n"));
	g_pallocated_recv_buf = _malloc(NR_RECVBUFF *sizeof(struct recv_buf) + 4);
	drvpriv.drv_registered = _TRUE;
	return usb_register(&drvpriv.r871xu_drv);	
}

static void __exit r8712u_drv_halt(void)
{
	RT_TRACE(_module_hci_intfs_c_,_drv_err_,("+r8712u_drv_halt\n"));
	printk("+r8712u_drv_halt\n");
	drvpriv.drv_registered = _FALSE;
	
	usb_deregister(&drvpriv.r871xu_drv);

	if( g_pallocated_recv_buf )
		_mfree(g_pallocated_recv_buf, NR_RECVBUFF *sizeof(struct recv_buf) + 4);
	
	printk("-r8712u_drv_halt\n");
}


module_init(r8712u_drv_entry);
module_exit(r8712u_drv_halt);


/*
init (driver module)-> r8712u_drv_entry
probe (sd device)-> r871xu_drv_init(dev_init)
open (net_device) ->netdev_open
close (net_device) ->netdev_close
remove (sd device) ->r871xu_dev_remove
exit (driver module)-> r8712u_drv_halt
*/


/*
r8711s_drv_entry()
r8711u_drv_entry()
r8712s_drv_entry()
r8712u_drv_entry()
*/

