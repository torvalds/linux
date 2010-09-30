//=====================================================
// CopyRight (C) 2007 Qualcomm Inc. All Rights Reserved.
//
//
// This file is part of Express Card USB Driver
//
// $Id:
//====================================================
// 20090926; aelias; removed all compiler warnings; ubuntu 9.04; 2.6.28-15-generic
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "ft1000_usb.h"

//#include <linux/sched.h>
//#include <linux/ptrace.h>
//#include <linux/slab.h>
//#include <linux/string.h>
//#include <linux/timer.h>
//#include <linux/netdevice.h>
//#include <linux/ioport.h>
//#include <linux/delay.h>
//#include <asm/io.h>
//#include <asm/system.h>
#include <linux/kthread.h>

MODULE_DESCRIPTION("FT1000 EXPRESS CARD DRIVER");
MODULE_LICENSE("Dual MPL/GPL");
MODULE_SUPPORTED_DEVICE("QFT FT1000 Express Cards");


void *pFileStart;
ULONG FileLength;

#define VENDOR_ID 0x1291   /* Qualcomm vendor id */
#define PRODUCT_ID 0x11    /* fake product id */

/* table of devices that work with this driver */
static struct usb_device_id id_table[] = {
    {USB_DEVICE(VENDOR_ID, PRODUCT_ID) },
    { },
};

MODULE_DEVICE_TABLE (usb, id_table);

extern  struct ft1000_device *pdevobj[MAX_NUM_CARDS+2];

char *getfw (char *fn, int *pimgsz);

int ft1000_close(struct net_device *net);
void dsp_reload (struct ft1000_device *ft1000dev);
u16 init_ft1000_netdev(struct ft1000_device *ft1000dev);
u16 reg_ft1000_netdev(struct ft1000_device *ft1000dev, struct usb_interface *intf);
int ft1000_poll(void* dev_id);
void ft1000_DestroyDevice(struct net_device *dev);
u16 ft1000_read_dpram16(struct ft1000_device *ft1000dev, USHORT indx, PUCHAR buffer, u8 highlow);
u16 ft1000_read_register(struct ft1000_device *ft1000dev, short* Data, u16 nRegIndx);
BOOLEAN gPollingfailed = FALSE;

void ft1000InitProc(struct net_device *dev);
void ft1000CleanupProc(FT1000_INFO *info);
int ft1000_poll_thread(void *arg);

int ft1000_poll_thread(void *arg)
{
    int ret = STATUS_SUCCESS;

    while(!kthread_should_stop() )
    {
        msleep(10);
        if ( ! gPollingfailed )
        {
            ret = ft1000_poll(arg);
            if ( ret != STATUS_SUCCESS )
            {
                DEBUG("ft1000_poll_thread: polling failed\n");
                gPollingfailed = TRUE;
            }
        }
    }
    //DEBUG("returned from polling thread\n");
    return STATUS_SUCCESS;
}



//---------------------------------------------------------------------------
// Function:    ft1000_probe
//
// Parameters:  struct usb_interface *interface  - passed by USB core
//              struct usb_device_id *id         - passed by USB core
// Returns:     0 - success
//
// Description: This function is invoked when the express card is plugged in
//
// Notes:
//
//---------------------------------------------------------------------------
static int ft1000_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    struct usb_device *dev;
    unsigned numaltsetting;
    int i;

    struct ft1000_device *ft1000dev;
    FT1000_INFO *pft1000info;

    if(!(ft1000dev = kmalloc(sizeof(struct ft1000_device), GFP_KERNEL)))
    {
        printk("out of memory allocating device structure\n");
	return 0;
    }

    memset(ft1000dev, 0, sizeof(*ft1000dev));

	//get usb device
    dev = interface_to_usbdev(interface);
    DEBUG("ft1000_probe: usb device descriptor info:\n");
    DEBUG("ft1000_probe: number of configuration is %d\n", dev->descriptor.bNumConfigurations);

	ft1000dev->dev = dev;
	ft1000dev->status = 0;
	ft1000dev->net = NULL;
	//ft1000dev->device_lock = SPIN_LOCK_UNLOCKED;
	spin_lock_init(&ft1000dev->device_lock);
	ft1000dev->tx_urb = usb_alloc_urb(0, GFP_ATOMIC);
	ft1000dev->rx_urb = usb_alloc_urb(0, GFP_ATOMIC);


    DEBUG("ft1000_probe is called\n");
    numaltsetting = interface->num_altsetting;
    DEBUG("ft1000_probe: number of alt settings is :%d\n",numaltsetting);
    iface_desc = interface->cur_altsetting;
    DEBUG("ft1000_probe: number of endpoints is %d\n", iface_desc->desc.bNumEndpoints);
    DEBUG("ft1000_probe: descriptor type is %d\n", iface_desc->desc.bDescriptorType);
    DEBUG("ft1000_probe: interface number is %d\n", iface_desc->desc.bInterfaceNumber);
    DEBUG("ft1000_probe: alternatesetting is %d\n", iface_desc->desc.bAlternateSetting);
    DEBUG("ft1000_probe: interface class is %d\n", iface_desc->desc.bInterfaceClass);
    DEBUG("ft1000_probe: control endpoint info:\n");
    DEBUG("ft1000_probe: descriptor0 type -- %d\n", iface_desc->endpoint[0].desc.bmAttributes);
    DEBUG("ft1000_probe: descriptor1 type -- %d\n", iface_desc->endpoint[1].desc.bmAttributes);
    DEBUG("ft1000_probe: descriptor2 type -- %d\n", iface_desc->endpoint[2].desc.bmAttributes);

    for (i=0; i< iface_desc->desc.bNumEndpoints;i++ )
    {
		endpoint = (struct usb_endpoint_descriptor *)&iface_desc->endpoint[i].desc;
                DEBUG("endpoint %d\n", i);
                DEBUG("bEndpointAddress=%x, bmAttributes=%x\n", endpoint->bEndpointAddress, endpoint->bmAttributes);
		if ( (endpoint->bEndpointAddress & USB_DIR_IN) && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK))
		{
			ft1000dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			DEBUG("ft1000_probe: in: %d\n", endpoint->bEndpointAddress);
		}

		if (!(endpoint->bEndpointAddress & USB_DIR_IN) && ((endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK))
		{
			ft1000dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
			DEBUG("ft1000_probe: out: %d\n", endpoint->bEndpointAddress);
		}
    }

    DEBUG("bulk_in=%d, bulk_out=%d\n", ft1000dev->bulk_in_endpointAddr, ft1000dev->bulk_out_endpointAddr);

    //read DSP image
    pFileStart = (void*)getfw("/etc/flarion/ft3000.img", &FileLength);

    if (pFileStart == NULL )
    {
        DEBUG ("ft1000_probe: Read DSP image failed\n");
        return 0;
    }

    //for ( i=0; i< MAX_NUM_CARDS+2; i++)
    //    pdevobj[i] = NULL;

    //download dsp image
    DEBUG("ft1000_probe: start downloading dsp image...\n");
    init_ft1000_netdev(ft1000dev);
    pft1000info = (FT1000_INFO *) netdev_priv (ft1000dev->net);

//    DEBUG("In probe: pft1000info=%x\n", pft1000info);				// aelias [-] reason: warning: format ???%x??? expects type ???unsigned int???, but argument 2 has type ???struct FT1000_INFO *???
    DEBUG("In probe: pft1000info=%p\n", pft1000info);		// aelias [+] reason: up

    dsp_reload(ft1000dev);
    gPollingfailed = FALSE;  //mbelian
    pft1000info->pPollThread = kthread_run(ft1000_poll_thread, ft1000dev, "ft1000_poll");
	msleep(500); //mbelian


    if ( pft1000info->DSP_loading )
    {
        DEBUG("ERROR!!!! RETURN FROM ft1000_probe **********************\n");
        return 0;
    }

    while (!pft1000info->CardReady)
    {
        if ( gPollingfailed )
        {
            if ( pft1000info->pPollThread )
            {
                kthread_stop(pft1000info->pPollThread );
            }
            return 0;
        }
        msleep(100);
        DEBUG("ft1000_probe::Waiting for Card Ready\n");
    }


    //initialize network device
    DEBUG("ft1000_probe::Card Ready!!!! Registering network device\n");

    reg_ft1000_netdev(ft1000dev, interface);

    pft1000info->NetDevRegDone = 1;

		ft1000InitProc(ft1000dev->net);// +mbelian

       return 0;
}

//---------------------------------------------------------------------------
// Function:    ft1000_disconnect
//
// Parameters:  struct usb_interface *interface  - passed by USB core
//
// Returns:     0 - success
//
// Description: This function is invoked when the express card is plugged out
//
// Notes:
//
//---------------------------------------------------------------------------
static void ft1000_disconnect(struct usb_interface *interface)
{
    FT1000_INFO *pft1000info;

    DEBUG("ft1000_disconnect is called\n");

    pft1000info = (PFT1000_INFO)usb_get_intfdata(interface);
//    DEBUG("In disconnect pft1000info=%x\n", pft1000info);	// aelias [-] reason: warning: format ???%x??? expects type ???unsigned int???, but argument 2 has type ???struct FT1000_INFO *???
    DEBUG("In disconnect pft1000info=%p\n", pft1000info);	// aelias [+] reason: up



    if (pft1000info)
    {
		ft1000CleanupProc(pft1000info);	//+mbelian
        if ( pft1000info->pPollThread )
        {
            kthread_stop(pft1000info->pPollThread );
        }

        DEBUG("ft1000_disconnect: threads are terminated\n");

        if (pft1000info->pFt1000Dev->net)
        {
            DEBUG("ft1000_disconnect: destroy char driver\n");
            ft1000_DestroyDevice(pft1000info->pFt1000Dev->net);
            //DEBUG("ft1000_disconnect: calling ft1000_close\n");
            //ft1000_close(pft1000info->pFt1000Dev->net);
            //DEBUG("ft1000_disconnect: ft1000_close is called\n");
            unregister_netdev(pft1000info->pFt1000Dev->net);
            DEBUG("ft1000_disconnect: network device unregisterd\n");
            free_netdev(pft1000info->pFt1000Dev->net);

        }

        usb_free_urb(pft1000info->pFt1000Dev->rx_urb);
        usb_free_urb(pft1000info->pFt1000Dev->tx_urb);

        DEBUG("ft1000_disconnect: urb freed\n");

		kfree(pft1000info->pFt1000Dev); //+mbelian
    }

    //terminate other kernel threads
    //in multiple instances case, first find the device
    //in the link list
    /**if (pPollThread)
    {
        kthread_stop(pPollThread);
        DEBUG("Polling thread is killed \n");
    }**/

    return;
}

static struct usb_driver ft1000_usb_driver = {
    //.owner =    THIS_MODULE,
    .name  =    "ft1000usb",
    .probe =    ft1000_probe,
    .disconnect = ft1000_disconnect,
    .id_table = id_table,
};

//---------------------------------------------------------------------------
// Function:    usb_ft1000_init
//
// Parameters:  none
//
// Returns:     0 - success
//
// Description: The entry point of the module, register the usb driver
//
// Notes:
//
//---------------------------------------------------------------------------
static int __init usb_ft1000_init(void)
{
    int ret = 0;

    DEBUG("Initialize and register the driver\n");

    ret = usb_register(&ft1000_usb_driver);
    if (ret)
        err("usb_register failed. Error number %d", ret);

    return ret;
}

//---------------------------------------------------------------------------
// Function:    usb_ft1000_exit
//
// Parameters:
//
// Returns:
//
// Description: Moudle unload function, deregister usb driver
//
// Notes:
//
//---------------------------------------------------------------------------
static void __exit usb_ft1000_exit(void)
{
    DEBUG("Deregister the driver\n");
    usb_deregister(&ft1000_usb_driver);
}

module_init (usb_ft1000_init);
module_exit (usb_ft1000_exit);


