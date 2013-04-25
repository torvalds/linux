/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#define RTMP_MODULE_OS

/*#include "rt_config.h" */
#include "rtmp_comm.h"
#include "rt_os_util.h"
#include "rt_os_net.h"
#include "rt_config.h"
#include "rtmp.h"

/* add by sw start*/
#include <mach/sys_config.h>
extern int sw_usb_disable_hcd(__u32 usbc_no);
extern int sw_usb_enable_hcd(__u32 usbc_no);
static int usb_wifi_host = 2;
/* add by sw end*/

/* Following information will be show when you run 'modinfo' */
/* *** If you have a solution for the bug in current version of driver, please mail to me. */
/* Otherwise post to forum in ralinktech's web site(www.ralinktech.com) and let all users help you. *** */
MODULE_AUTHOR("Paul Lin <paul_lin@ralinktech.com>");
MODULE_DESCRIPTION("RT2870 Wireless Lan Linux Driver");
MODULE_LICENSE("GPL");
#ifdef CONFIG_STA_SUPPORT
#ifdef MODULE_VERSION
MODULE_VERSION(STA_DRIVER_VERSION);
#endif
#endif /* CONFIG_STA_SUPPORT */


extern USB_DEVICE_ID rtusb_dev_id[];
extern INT const rtusb_usb_id_len;
static BOOLEAN	late_resume_flag = FALSE;

static void rt2870_disconnect(
	IN struct usb_device *dev, 
	IN VOID *pAd);

static int rt2870_probe(
	IN struct usb_interface *intf,
	IN struct usb_device *usb_dev,
	IN const USB_DEVICE_ID *dev_id,
	IN VOID **ppAd);

#ifndef PF_NOFREEZE
#define PF_NOFREEZE  0
#endif


/*extern int rt28xx_close(IN struct net_device *net_dev); */
/*extern int rt28xx_open(struct net_device *net_dev); */

static BOOLEAN USBDevConfigInit(
	IN struct usb_device 	*dev,
	IN struct usb_interface *intf, 
	IN VOID					*pAd);
	

VOID RT28XXVendorSpecificCheck(
	IN struct usb_device 	*dev,
	IN VOID 				*pAd)
{


	RT_CMD_USB_MORE_FLAG_CONFIG Config = { dev->descriptor.idVendor,
										dev->descriptor.idProduct };
	RTMP_DRIVER_USB_MORE_FLAG_SET(pAd, &Config);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)

/**************************************************************************/
/**************************************************************************/
/*tested for kernel 2.4 series */
/**************************************************************************/
/**************************************************************************/
static void *rtusb_probe(struct usb_device *dev, UINT interface,
						const USB_DEVICE_ID *id_table);
static void rtusb_disconnect(struct usb_device *dev, void *ptr);

struct usb_driver rtusb_driver = {
		name:RTMP_DRV_NAME,
		probe:rtusb_probe,
		disconnect:rtusb_disconnect,
		id_table:rtusb_dev_id,
	};


static BOOLEAN USBDevConfigInit(
	IN struct usb_device 	*dev,
	IN struct usb_interface *intf, 
	IN VOID					*pAd)
{
	struct usb_interface_descriptor *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	ULONG BulkOutIdx;
	UINT32 i;
	RT_CMD_USB_DEV_CONFIG Config, *pConfig = &Config;


	iface_desc = &intf->altsetting[0];

	/* get # of enpoints */
	pConfig->NumberOfPipes = iface_desc->bNumEndpoints;
	DBGPRINT(RT_DEBUG_TRACE, ("NumEndpoints=%d\n", iface_desc->bNumEndpoints));		 

	/* Configure Pipes */
	endpoint = &iface_desc->endpoint[0];
	BulkOutIdx = 0;

	for(i=0; i<pConfig->NumberOfPipes; i++)
	{
		if ((endpoint[i].bmAttributes == USB_ENDPOINT_XFER_BULK) && 
			((endpoint[i].bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN))
		{
			pConfig->BulkInEpAddr = endpoint[i].bEndpointAddress;
			pConfig->BulkInMaxPacketSize = endpoint[i].wMaxPacketSize;

			DBGPRINT_RAW(RT_DEBUG_TRACE, ("BULK IN MaximumPacketSize = %d\n", pConfig->BulkInMaxPacketSize));
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("EP address = 0x%2x  \n", endpoint[i].bEndpointAddress));
		}
		else if ((endpoint[i].bmAttributes == USB_ENDPOINT_XFER_BULK) && 
				((endpoint[i].bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT))
		{
			/* There are 6 bulk out EP. EP6 highest priority. */
			/* EP1-4 is EDCA.  EP5 is HCCA. */
			pConfig->BulkOutEpAddr[BulkOutIdx++] = endpoint[i].bEndpointAddress;
			pConfig->BulkOutMaxPacketSize = endpoint[i].wMaxPacketSize;

			DBGPRINT_RAW(RT_DEBUG_TRACE, ("BULK OUT MaximumPacketSize = %d\n", pConfig->BulkOutMaxPacketSize));
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("EP address = 0x%2x  \n", endpoint[i].bEndpointAddress));
		}
	}

	if (!(pConfig->BulkInEpAddr && pConfig->BulkOutEpAddr[0])) 
	{
		printk("Could not find both bulk-in and bulk-out endpoints\n");
		return FALSE;
	}

	pConfig->pConfig = dev->config;
	RTMP_DRIVER_USB_CONFIG_INIT(pAd, pConfig);
	RT28XXVendorSpecificCheck(dev, pAd);

	return TRUE;
	
}

static void *rtusb_probe(struct usb_device *dev, UINT interface,
						const USB_DEVICE_ID *id)
{
	struct usb_interface *intf;
	VOID *pAd;
	int rv;


	/* get the active interface descriptor */
	intf = &dev->actconfig->interface[interface];

	/* call generic probe procedure. */
	rv = rt2870_probe(intf, dev, id, &pAd);
	if (rv != 0)
		pAd = NULL;
	
	return (void *)pAd;
}

/*Disconnect function is called within exit routine */
static void rtusb_disconnect(struct usb_device *dev, void *ptr)
{
	rt2870_disconnect(dev, ptr);
}


#else	/* else if we are kernel 2.6 series */


/**************************************************************************/
/**************************************************************************/
/*tested for kernel 2.6series */
/**************************************************************************/
/**************************************************************************/

#ifdef CONFIG_PM

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,10)
#define pm_message_t u32
#endif

static int rt2870_suspend(struct usb_interface *intf, pm_message_t state);
static int rt2870_resume(struct usb_interface *intf);
#endif /* CONFIG_PM */


/*--------------------------------------------------------------------- */
/* function declarations                                                                                                */
/*--------------------------------------------------------------------- */

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rt2870_early_suspend(struct early_suspend *early)
{
        DBGPRINT(RT_DEBUG_ERROR, ("%s\n", __func__));
}

static void rt2870_late_resume(struct early_suspend *early)
{
        PRTMP_ADAPTER   pAd = container_of(early, RTMP_ADAPTER, early_suspend);
        DBGPRINT(RT_DEBUG_ERROR, ("%s\n", __func__));

	if (late_resume_flag == TRUE){
		printk("late_resume_flag is TRUE!!!\n");
		if (VIRTUAL_IF_UP((VOID *)pAd) != 0)
                {
			printk("%s, VIRTUAL_IF_UP != 0, WTF!!!!\n", __func__);
                }
		late_resume_flag = FALSE;
	}
}

void RTRegisterEarlySuspend(PRTMP_ADAPTER    pAd)
{
        DBGPRINT(RT_DEBUG_ERROR, ("%s\n", __func__));
        pAd->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN;
        pAd->early_suspend.suspend = rt2870_early_suspend;
        pAd->early_suspend.resume = rt2870_late_resume;
        register_early_suspend(&pAd->early_suspend);
}

void RTUnregisterEarlySuspend(PRTMP_ADAPTER  pAd)
{
        DBGPRINT(RT_DEBUG_ERROR, ("%s\n", __func__));
        late_resume_flag = FALSE;

        if (pAd->early_suspend.suspend)
                unregister_early_suspend(&pAd->early_suspend);

        pAd->early_suspend.suspend = NULL;
        pAd->early_suspend.resume = NULL;
}
#endif

static int rtusb_probe (struct usb_interface *intf,
						const USB_DEVICE_ID *id);
static void rtusb_disconnect(struct usb_interface *intf);

static BOOLEAN USBDevConfigInit(
	IN struct usb_device 	*dev,
	IN struct usb_interface *intf, 
	IN VOID 				*pAd)
{
	struct usb_host_interface *iface_desc;
	ULONG BulkOutIdx;
	UINT32 i;
	RT_CMD_USB_DEV_CONFIG Config, *pConfig = &Config;


	/* get the active interface descriptor */
	iface_desc = intf->cur_altsetting;

	/* get # of enpoints  */
	pConfig->NumberOfPipes = iface_desc->desc.bNumEndpoints;
	DBGPRINT(RT_DEBUG_TRACE, ("NumEndpoints=%d\n", iface_desc->desc.bNumEndpoints));		  

	/* Configure Pipes */
	BulkOutIdx = 0;

	for(i=0; i<pConfig->NumberOfPipes; i++)
	{ 
		if ((iface_desc->endpoint[i].desc.bmAttributes == 
				USB_ENDPOINT_XFER_BULK) && 
			((iface_desc->endpoint[i].desc.bEndpointAddress &
				USB_ENDPOINT_DIR_MASK) == USB_DIR_IN))
		{
			pConfig->BulkInEpAddr = iface_desc->endpoint[i].desc.bEndpointAddress;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
			pConfig->BulkInMaxPacketSize = le2cpu16(iface_desc->endpoint[i].desc.wMaxPacketSize);
#else
			pConfig->BulkInMaxPacketSize = iface_desc->endpoint[i].desc.wMaxPacketSize;
#endif /* LINUX_VERSION_CODE */

			DBGPRINT_RAW(RT_DEBUG_TRACE, ("BULK IN MaxPacketSize = %d\n", pConfig->BulkInMaxPacketSize));
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("EP address = 0x%2x\n", iface_desc->endpoint[i].desc.bEndpointAddress));
		}
		else if ((iface_desc->endpoint[i].desc.bmAttributes ==
					USB_ENDPOINT_XFER_BULK) && 
				((iface_desc->endpoint[i].desc.bEndpointAddress &
					USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT))
		{
			/* there are 6 bulk out EP. EP6 highest priority. */
			/* EP1-4 is EDCA.  EP5 is HCCA. */
			pConfig->BulkOutEpAddr[BulkOutIdx++] = iface_desc->endpoint[i].desc.bEndpointAddress;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,11)
			pConfig->BulkOutMaxPacketSize = le2cpu16(iface_desc->endpoint[i].desc.wMaxPacketSize);
#else
			pConfig->BulkOutMaxPacketSize = iface_desc->endpoint[i].desc.wMaxPacketSize;
#endif

			DBGPRINT_RAW(RT_DEBUG_TRACE, ("BULK OUT MaxPacketSize = %d\n", pConfig->BulkOutMaxPacketSize));
			DBGPRINT_RAW(RT_DEBUG_TRACE, ("EP address = 0x%2x  \n", iface_desc->endpoint[i].desc.bEndpointAddress));
		}
	}

	if (!(pConfig->BulkInEpAddr && pConfig->BulkOutEpAddr[0])) 
	{
		printk("%s: Could not find both bulk-in and bulk-out endpoints\n", __FUNCTION__);
		return FALSE;
	}

	pConfig->pConfig = &dev->config->desc;
	usb_set_intfdata(intf, pAd);
	RTMP_DRIVER_USB_CONFIG_INIT(pAd, pConfig);
	RT28XXVendorSpecificCheck(dev, pAd);    
	
	return TRUE;
	
}



static int rtusb_probe (struct usb_interface *intf,
						const USB_DEVICE_ID *id)
{	
	VOID *pAd;
	struct usb_device *dev;
	int rv;

	dev = interface_to_usbdev(intf);
	dev = usb_get_dev(dev);
	
	rv = rt2870_probe(intf, dev, id, &pAd);
	if (rv != 0)
	{
		usb_put_dev(dev);
	}
#if 0
	else
 	{
   		if (VIRTUAL_IF_UP(pAd) != 0)
   		{
                     pAd = usb_get_intfdata(intf);
                     usb_set_intfdata(intf, NULL);
                     rt2870_disconnect(dev, pAd);
     			rv = -ENOMEM;
   		}
 	}
#endif
	
	return rv;
}


static void rtusb_disconnect(struct usb_interface *intf)
{
	struct usb_device   *dev = interface_to_usbdev(intf);
	VOID				*pAd;


	pAd = usb_get_intfdata(intf);
	//VIRTUAL_IF_DOWN(pAd);
	usb_set_intfdata(intf, NULL);	

	rt2870_disconnect(dev, pAd);

#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
	    printk("rtusb_disconnect usb_autopm_put_interface \n");

	    usb_autopm_put_interface(intf);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
	 printk(" ^^rt2870_disconnect ====> pm_usage_cnt %d \n", atomic_read(&intf->pm_usage_cnt));
#else
	 printk(" rt2870_disconnect ====> pm_usage_cnt %d \n", intf->pm_usage_cnt);
#endif
	

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */

}


struct usb_driver rtusb_driver = {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,15)
	.owner = THIS_MODULE,
#endif	
	.name=RTMP_DRV_NAME,
	.probe=rtusb_probe,
	.disconnect=rtusb_disconnect,
	.id_table=rtusb_dev_id,

#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
	.supports_autosuspend = 1,
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
	suspend:	rt2870_suspend,
	resume:		rt2870_resume,
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)
	reset_resume:	rt2870_resume,
#endif

#endif /* CONFIG_PM */
	};

#ifdef CONFIG_PM

VOID RT2870RejectPendingPackets(
	IN	VOID	*pAd)
{
	/* clear PS packets */
	/* clear TxSw packets */
}

static int rt2870_suspend(
	struct usb_interface *intf,
	pm_message_t state)
{
	struct net_device *net_dev;
	VOID *pAd = usb_get_intfdata(intf);

	late_resume_flag = TRUE;

#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
	UCHAR Flag;
#endif	
#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	if ((RT_IS_EARLYSUSPEND_REGISTERED((PRTMP_ADAPTER)pAd)) && (late_resume_flag == TRUE)){
		VIRTUAL_IF_DOWN((VOID *)pAd);
		DBGPRINT(RT_DEBUG_OFF, ("%s, We has already register earlysuspend, make VIRTUAL_IF_DOWN\n", __func__));
		return 0;
	}
#endif
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
//	UCHAR Flag;
	DBGPRINT(RT_DEBUG_ERROR, ("autosuspend===> rt2870_suspend()\n"));
/*	RtmpOSWrielessEventSend(pAd->net_dev, RT_WLAN_EVENT_CGIWAP, -1, NULL, NULL, 0);*/
	RTMP_DRIVER_ADAPTER_END_DISSASSOCIATE(pAd);

/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_IDLE_RADIO_OFF)) */
	RTMP_DRIVER_ADAPTER_IDLE_RADIO_OFF_TEST(pAd, &Flag);
	if(!Flag)
	{
		/*RT28xxUsbAsicRadioOff(pAd); */
		RTMP_DRIVER_ADAPTER_RT28XX_USB_ASICRADIO_OFF(pAd);

		/*RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND); */
		RTMP_DRIVER_ADAPTER_CPU_SUSPEND_SET(pAd);
	}
		/*RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_SUSPEND); */
		RTMP_DRIVER_ADAPTER_SUSPEND_SET(pAd);
	DBGPRINT(RT_DEBUG_ERROR, ("autosuspend <=== rt2870_suspend()\n"));
	return 0;
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */


	DBGPRINT(RT_DEBUG_TRACE, ("===> rt2870_suspend()\n"));
//	RtmpOSWrielessEventSend(pAd->net_dev, RT_WLAN_EVENT_CGIWAP, -1, NULL, NULL, 0);
//	RTMP_DRIVER_ADAPTER_END_DISSASSOCIATE(pAd);

//	RTMP_DRIVER_ADAPTER_RT28XX_USB_ASICRADIO_OFF(pAd);
/*	net_dev = pAd->net_dev; */
	RTMP_DRIVER_NET_DEV_GET(pAd, &net_dev);
	netif_device_detach(net_dev);

	RTMP_DRIVER_USB_SUSPEND(pAd, netif_running(net_dev));
	DBGPRINT(RT_DEBUG_TRACE, ("<=== rt2870_suspend()\n"));
	return 0;
}

static int rt2870_resume(
	struct usb_interface *intf)
{
	struct net_device *net_dev;
	VOID *pAd = usb_get_intfdata(intf);

#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
	struct usb_device		*pUsb_Dev;
	UCHAR Flag;
	INT 		pm_usage_cnt;
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	if ((RT_IS_EARLYSUSPEND_REGISTERED((PRTMP_ADAPTER)pAd)) && (late_resume_flag == TRUE)){
		DBGPRINT(RT_DEBUG_OFF, ("%s, We has already register earlysuspend, call VIRTUAL_IF_UP\n", __func__));
		return 0;
	}
#endif

#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
//	struct usb_device		*pUsb_Dev;
//	UCHAR Flag;
//	INT 		pm_usage_cnt;

	RTMP_DRIVER_USB_DEV_GET(pAd, &pUsb_Dev);
	RTMP_DRIVER_USB_INTF_GET(pAd, &intf);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
	pm_usage_cnt = atomic_read(&intf->pm_usage_cnt);	
#else
	pm_usage_cnt = intf->pm_usage_cnt;
#endif
#if 0
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
	if(pUsb_Dev->autosuspend_disabled == 0)
#else
	if(pUsb_Dev->auto_pm == 1)
#endif
#endif
	{
		if(pm_usage_cnt  <= 0)
			usb_autopm_get_interface(intf);

	}
	DBGPRINT(RT_DEBUG_ERROR, ("autosuspend===> rt2870_resume()\n"));
	/*RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_SUSPEND); */
	RTMP_DRIVER_ADAPTER_SUSPEND_CLEAR(pAd);

	RTMP_DRIVER_ADAPTER_CPU_SUSPEND_TEST(pAd, &Flag);
	if(Flag)
	/*if(RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND)) */
	{
		/*RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND); */
		RTMP_DRIVER_ADAPTER_CPU_SUSPEND_CLEAR(pAd);
	}

	/*RT28xxUsbAsicRadioOn(pAd); */
	RTMP_DRIVER_ADAPTER_RT28XX_USB_ASICRADIO_ON(pAd);
	DBGPRINT(RT_DEBUG_ERROR, ("autosuspend<===  rt2870_resume()\n"));
	return 0;
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */


	DBGPRINT(RT_DEBUG_TRACE, ("===> rt2870_resume()\n"));
/*	pAd->PM_FlgSuspend = 0; */
	//RTMP_DRIVER_ADAPTER_RT28XX_USB_ASICRADIO_ON(pAd);
	RTMP_DRIVER_USB_RESUME(pAd);

/*	net_dev = pAd->net_dev; */
	RTMP_DRIVER_NET_DEV_GET(pAd, &net_dev);
	netif_device_attach(net_dev);
	netif_start_queue(net_dev);
	netif_carrier_on(net_dev);
	netif_wake_queue(net_dev);

	DBGPRINT(RT_DEBUG_TRACE, ("<=== rt2870_resume()\n"));
	return 0;
}
#endif /* CONFIG_PM */
#endif /* LINUX_VERSION_CODE */


/* Init driver module */
INT __init rtusb_init(void)
{
	int ret = 0;
	
	printk("rtusb init %s --->\n", RTMP_DRV_NAME);

	ret = script_parser_fetch("usb_wifi_para", "usb_wifi_usbc_num", (int *)&usb_wifi_host, 64);
	if(ret != 0){
		printk("ERR: script_parser_fetch usb_wifi_usbc_num failed\n");
		ret = -ENOMEM;
		return ret;
	}

	printk("sw_usb_enable_hcd: usbc_num = %d\n", usb_wifi_host);

	sw_usb_enable_hcd(usb_wifi_host);
	
#ifdef RESOURCE_BOOT_ALLOC
{
	int status;
	status = rtusb_resource_init(rtusb_tx_buf_len, rtusb_rx_buf_len, rtusb_tx_buf_cnt, rtusb_rx_buf_cnt);
	if (status)
	{
		printk("resource allocate failed, don't register driver!\n");
		return -1;
	}
}
#endif /* RESOURCE_BOOT_ALLOC */

	return usb_register(&rtusb_driver);
}

/* Deinit driver module */
VOID __exit rtusb_exit(void)
{
	usb_deregister(&rtusb_driver);	
	
#ifdef RESOURCE_BOOT_ALLOC
	rtusb_resource_exit();
#endif /* RESOURCE_BOOT_ALLOC */	

	printk("sw_usb_disable_hcd: usbc_num = %d\n", usb_wifi_host);
	sw_usb_disable_hcd(usb_wifi_host);

	printk("<--- rtusb exit\n");
}

module_init(rtusb_init);
module_exit(rtusb_exit);




/*---------------------------------------------------------------------	*/
/* function declarations												*/
/*---------------------------------------------------------------------	*/



/*
========================================================================
Routine Description:
    Release allocated resources.

Arguments:
    *dev				Point to the PCI or USB device
	pAd					driver control block pointer

Return Value:
    None

Note:
========================================================================
*/
static void rt2870_disconnect(struct usb_device *dev, VOID *pAd)
{
	struct net_device *net_dev;


	DBGPRINT(RT_DEBUG_ERROR, ("rtusb_disconnect: unregister usbnet usb-%s-%s\n",
				dev->bus->bus_name, dev->devpath));
	if (!pAd)
	{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	/* kernel 2.4 series */
		while(MOD_IN_USE > 0)
		{
			MOD_DEC_USE_COUNT;
		}
#else
		usb_put_dev(dev);
#endif /* LINUX_VERSION_CODE */

		printk("rtusb_disconnect: pAd == NULL!\n");
		return;
	}
/*	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST); */
	RTMP_DRIVER_NIC_NOT_EXIST_SET(pAd);

	/* for debug, wait to show some messages to /proc system */
	udelay(1);


	RTMP_DRIVER_NET_DEV_GET(pAd, &net_dev);
	RtmpPhyNetDevExit(pAd, net_dev);

	/* FIXME: Shall we need following delay and flush the schedule?? */
	udelay(1);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	/* kernel 2.4 series */
#else
	flush_scheduled_work();
#endif /* LINUX_VERSION_CODE */
	udelay(1);

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	RTUnregisterEarlySuspend((PRTMP_ADAPTER)pAd);
#endif

	/* free the root net_device */
	RtmpOSNetDevFree(net_dev);

#ifdef RT_CFG80211_SUPPORT
	RTMP_DRIVER_80211_UNREGISTER(pAd, net_dev);
#endif /* RT_CFG80211_SUPPORT */

	RtmpRaDevCtrlExit(pAd);

	/* release a use of the usb device structure */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,5,0)	/* kernel 2.4 series */
	while(MOD_IN_USE > 0)
	{
		MOD_DEC_USE_COUNT;
	}
#else
	usb_put_dev(dev);
#endif /* LINUX_VERSION_CODE */
	udelay(1);
	
	DBGPRINT(RT_DEBUG_ERROR, (" RTUSB disconnect successfully\n"));
}


static int rt2870_probe(
	IN struct usb_interface *intf,
	IN struct usb_device *usb_dev,
	IN const USB_DEVICE_ID *dev_id,
	IN VOID **ppAd)
{
	struct  net_device		*net_dev = NULL;
	VOID       				*pAd = (VOID *) NULL;
	INT                 	status, rv;
	PVOID					handle;
	RTMP_OS_NETDEV_OP_HOOK	netDevHook;
	ULONG					OpMode;
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
/*	INT 		pm_usage_cnt; */
	INT		 res =1 ; 
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */	
/*
  rt_intf = intf;
  rt_usb_dev = usb_dev;
  rt_dev_id = dev_id;
 *rt_ppAd = ppAd;	
*/
	DBGPRINT(RT_DEBUG_TRACE, ("===>rt2870_probe()!\n"));
	
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND

        res = usb_autopm_get_interface(intf);
	if (res)
	{
			DBGPRINT(RT_DEBUG_ERROR, ("rt2870_probe autopm_resume fail ------\n"));
		     return -EIO;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
	atomic_set(&intf->pm_usage_cnt, 1);
	 printk(" rt2870_probe ====> pm_usage_cnt %d \n", atomic_read(&intf->pm_usage_cnt));
#else
         intf->pm_usage_cnt = 1;
	 printk(" rt2870_probe ====> pm_usage_cnt %d \n", intf->pm_usage_cnt);
#endif
	

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */



/*RtmpDevInit============================================= */
	/* Allocate RTMP_ADAPTER adapter structure */
/*	handle = kmalloc(sizeof(struct os_cookie), GFP_KERNEL); */
	os_alloc_mem(NULL, (UCHAR **)&handle, sizeof(struct os_cookie));
	if (handle == NULL)
	{
		printk("rt2870_probe(): Allocate memory for os handle failed!\n");
		return -ENOMEM;
	}
	memset(handle, 0, sizeof(struct os_cookie));

	((POS_COOKIE)handle)->pUsb_Dev = usb_dev;

#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND
	((POS_COOKIE)handle)->intf = intf;
#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */



	/* set/get operators to/from DRIVER module */
#ifdef OS_ABL_FUNC_SUPPORT
	/* get DRIVER operations */
	RtmpNetOpsInit(pRtmpDrvNetOps);
	RTMP_DRV_OPS_FUNCTION(pRtmpDrvOps, pRtmpDrvNetOps, NULL, NULL);
	RtmpNetOpsSet(pRtmpDrvNetOps);
#endif /* OS_ABL_FUNC_SUPPORT */

	rv = RTMPAllocAdapterBlock(handle, &pAd);
	if (rv != NDIS_STATUS_SUCCESS) 
	{
/*		kfree(handle); */
		os_free_mem(NULL, handle);
		goto err_out;
	}

/*USBDevInit============================================== */
	if (USBDevConfigInit(usb_dev, intf, pAd) == FALSE)
		goto err_out_free_radev;

	RtmpRaDevCtrlInit(pAd, RTMP_DEV_INF_USB);
	
/*NetDevInit============================================== */
	net_dev = RtmpPhyNetDevInit(pAd, &netDevHook);
	if (net_dev == NULL)
		goto err_out_free_radev;
	
	/* Here are the net_device structure with usb specific parameters. */
#ifdef NATIVE_WPA_SUPPLICANT_SUPPORT
	/* for supporting Network Manager.
	  * Set the sysfs physical device reference for the network logical device if set prior to registration will 
	  * cause a symlink during initialization.
	 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0))
	SET_NETDEV_DEV(net_dev, &(usb_dev->dev));
#endif
#endif /* NATIVE_WPA_SUPPLICANT_SUPPORT */

#ifdef CONFIG_STA_SUPPORT
/*    pAd->StaCfg.OriDevType = net_dev->type; */
	RTMP_DRIVER_STA_DEV_TYPE_SET(pAd, net_dev->type);
#endif /* CONFIG_STA_SUPPORT */

/*All done, it's time to register the net device to linux kernel. */
	/* Register this device */
#ifdef RT_CFG80211_SUPPORT
{
/*	pAd->pCfgDev = &(usb_dev->dev); */
/*	pAd->CFG80211_Register = CFG80211_Register; */
/*	RTMP_DRIVER_CFG80211_INIT(pAd, usb_dev); */

	/*
		In 2.6.32, cfg80211 register must be before register_netdevice();
		We can not put the register in rt28xx_open();
		Or you will suffer NULL pointer in list_add of
		cfg80211_netdev_notifier_call().
	*/
	CFG80211_Register(pAd, &(usb_dev->dev), net_dev);
}
#endif /* RT_CFG80211_SUPPORT */

	RTMP_DRIVER_OP_MODE_GET(pAd, &OpMode);
	status = RtmpOSNetDevAttach(OpMode, net_dev, &netDevHook);
	if (status != 0)
		goto err_out_free_netdev;

/*#ifdef KTHREAD_SUPPORT */

	*ppAd = pAd;




#ifdef INF_PPA_SUPPORT
/*	pAd->pDirectpathCb = (PPA_DIRECTPATH_CB *) kmalloc (sizeof(PPA_DIRECTPATH_CB), GFP_ATOMIC); */
/*	os_alloc_mem(NULL, (UCHAR **)&(pAd->pDirectpathCb), sizeof(PPA_DIRECTPATH_CB)); */
	RTMP_DRIVER_INF_PPA_INIT(pAd);
#endif /* INF_PPA_SUPPORT */

#ifdef PRE_ASSIGN_MAC_ADDR
	UCHAR PermanentAddress[MAC_ADDR_LEN];
	RTMP_DRIVER_MAC_ADDR_GET(pAd, &PermanentAddress[0]);
	DBGPRINT(RT_DEBUG_TRACE, ("@%s MAC address: %02x:%02x:%02x:%02x:%02x:%02x\n", __FUNCTION__, PermanentAddress[0], PermanentAddress[1],PermanentAddress[2],PermanentAddress[3],PermanentAddress[4],PermanentAddress[5]));
	/* Set up the Mac address */
	RtmpOSNetDevAddrSet(OpMode, net_dev, &PermanentAddress[0], NULL);
#endif /* PRE_ASSIGN_MAC_ADDR */

#if defined(CONFIG_HAS_EARLYSUSPEND) || defined(CONFIG_ANDROID_POWER)
	((PRTMP_ADAPTER)pAd)->early_suspend.suspend = NULL;
	RTRegisterEarlySuspend(pAd);
#endif

	DBGPRINT(RT_DEBUG_TRACE, ("<===rt2870_probe()!\n"));

	return 0;

	/* --------------------------- ERROR HANDLE --------------------------- */	
err_out_free_netdev:
	RtmpOSNetDevFree(net_dev);
	
err_out_free_radev:
	RTMPFreeAdapter(pAd);
	
err_out:
	*ppAd = NULL;

	return -1;
	
}



#ifdef OS_ABL_SUPPORT
/* USB complete handlers in LINUX */
RTMP_DRV_USB_COMPLETE_HANDLER RtmpDrvUsbBulkOutDataPacketComplete = NULL;
RTMP_DRV_USB_COMPLETE_HANDLER RtmpDrvUsbBulkOutMLMEPacketComplete = NULL;
RTMP_DRV_USB_COMPLETE_HANDLER RtmpDrvUsbBulkOutNullFrameComplete = NULL;
RTMP_DRV_USB_COMPLETE_HANDLER RtmpDrvUsbBulkOutRTSFrameComplete = NULL;
RTMP_DRV_USB_COMPLETE_HANDLER RtmpDrvUsbBulkOutPsPollComplete = NULL;
RTMP_DRV_USB_COMPLETE_HANDLER RtmpDrvUsbBulkRxComplete = NULL;

USBHST_STATUS RTUSBBulkOutDataPacketComplete(URBCompleteStatus Status, purbb_t pURB, pregs *pt_regs)
{
	RtmpDrvUsbBulkOutDataPacketComplete((VOID *)pURB);
}

USBHST_STATUS RTUSBBulkOutMLMEPacketComplete(URBCompleteStatus Status, purbb_t pURB, pregs *pt_regs)
{
	RtmpDrvUsbBulkOutMLMEPacketComplete((VOID *)pURB);
}

USBHST_STATUS RTUSBBulkOutNullFrameComplete(URBCompleteStatus Status, purbb_t pURB, pregs *pt_regs)
{
	RtmpDrvUsbBulkOutNullFrameComplete((VOID *)pURB);
}

USBHST_STATUS RTUSBBulkOutRTSFrameComplete(URBCompleteStatus Status, purbb_t pURB, pregs *pt_regs)
{
	RtmpDrvUsbBulkOutRTSFrameComplete((VOID *)pURB);
}

USBHST_STATUS RTUSBBulkOutPsPollComplete(URBCompleteStatus Status, purbb_t pURB, pregs *pt_regs)
{
	RtmpDrvUsbBulkOutPsPollComplete((VOID *)pURB);
}

USBHST_STATUS RTUSBBulkRxComplete(URBCompleteStatus Status, purbb_t pURB, pregs *pt_regs)
{
	RtmpDrvUsbBulkRxComplete((VOID *)pURB);
}

VOID RtmpNetOpsInit(
	IN VOID			*pDrvNetOpsSrc)
{
	RTMP_NET_ABL_OPS *pDrvNetOps = (RTMP_NET_ABL_OPS *)pDrvNetOpsSrc;


	pDrvNetOps->RtmpNetUsbBulkOutDataPacketComplete = (RTMP_DRV_USB_COMPLETE_HANDLER)RTUSBBulkOutDataPacketComplete;
	pDrvNetOps->RtmpNetUsbBulkOutMLMEPacketComplete = (RTMP_DRV_USB_COMPLETE_HANDLER)RTUSBBulkOutMLMEPacketComplete;
	pDrvNetOps->RtmpNetUsbBulkOutNullFrameComplete = (RTMP_DRV_USB_COMPLETE_HANDLER)RTUSBBulkOutNullFrameComplete;
	pDrvNetOps->RtmpNetUsbBulkOutRTSFrameComplete = (RTMP_DRV_USB_COMPLETE_HANDLER)RTUSBBulkOutRTSFrameComplete;
	pDrvNetOps->RtmpNetUsbBulkOutPsPollComplete = (RTMP_DRV_USB_COMPLETE_HANDLER)RTUSBBulkOutPsPollComplete;
	pDrvNetOps->RtmpNetUsbBulkRxComplete = (RTMP_DRV_USB_COMPLETE_HANDLER)RTUSBBulkRxComplete;
}


VOID RtmpNetOpsSet(
	IN VOID			*pDrvNetOpsSrc)
{
	RTMP_NET_ABL_OPS *pDrvNetOps = (RTMP_NET_ABL_OPS *)pDrvNetOpsSrc;


	RtmpDrvUsbBulkOutDataPacketComplete = pDrvNetOps->RtmpDrvUsbBulkOutDataPacketComplete;
	RtmpDrvUsbBulkOutMLMEPacketComplete = pDrvNetOps->RtmpDrvUsbBulkOutMLMEPacketComplete;
	RtmpDrvUsbBulkOutNullFrameComplete = pDrvNetOps->RtmpDrvUsbBulkOutNullFrameComplete;
	RtmpDrvUsbBulkOutRTSFrameComplete = pDrvNetOps->RtmpDrvUsbBulkOutRTSFrameComplete;
	RtmpDrvUsbBulkOutPsPollComplete = pDrvNetOps->RtmpDrvUsbBulkOutPsPollComplete;
	RtmpDrvUsbBulkRxComplete = pDrvNetOps->RtmpDrvUsbBulkRxComplete;
}
#endif /* OS_ABL_SUPPORT */
