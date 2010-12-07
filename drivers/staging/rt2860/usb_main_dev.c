/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
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

#include "rt_config.h"

/* Following information will be show when you run 'modinfo' */
/* If you have a solution for the bug in current version of driver, please e-mail me. */
/* Otherwise post to the forum at ralinktech's web site(www.ralinktech.com) and let all users help you. */
MODULE_AUTHOR("Paul Lin <paul_lin@ralinktech.com>");
MODULE_DESCRIPTION("RT2870/RT3070 Wireless Lan Linux Driver");
MODULE_LICENSE("GPL");
#ifdef MODULE_VERSION
MODULE_VERSION(STA_DRIVER_VERSION);
#endif

/* module table */
struct usb_device_id rtusb_usb_id[] = {
#ifdef RT2870
	{USB_DEVICE(0x148F, 0x2770)},	/* Ralink */
	{USB_DEVICE(0x148F, 0x2870)},	/* Ralink */
	{USB_DEVICE(0x07B8, 0x2870)},	/* AboCom */
	{USB_DEVICE(0x07B8, 0x2770)},	/* AboCom */
	{USB_DEVICE(0x0DF6, 0x0039)},	/* Sitecom 2770 */
	{USB_DEVICE(0x0DF6, 0x003F)},	/* Sitecom 2770 */
	{USB_DEVICE(0x083A, 0x7512)},	/* Arcadyan 2770 */
	{USB_DEVICE(0x0789, 0x0162)},	/* Logitec 2870 */
	{USB_DEVICE(0x0789, 0x0163)},	/* Logitec 2870 */
	{USB_DEVICE(0x0789, 0x0164)},	/* Logitec 2870 */
	{USB_DEVICE(0x177f, 0x0302)},	/* lsusb */
	{USB_DEVICE(0x0B05, 0x1731)},	/* Asus */
	{USB_DEVICE(0x0B05, 0x1732)},	/* Asus */
	{USB_DEVICE(0x0B05, 0x1742)},	/* Asus */
	{USB_DEVICE(0x0DF6, 0x0017)},	/* Sitecom */
	{USB_DEVICE(0x0DF6, 0x002B)},	/* Sitecom */
	{USB_DEVICE(0x0DF6, 0x002C)},	/* Sitecom */
	{USB_DEVICE(0x0DF6, 0x002D)},	/* Sitecom */
	{USB_DEVICE(0x14B2, 0x3C06)},	/* Conceptronic */
	{USB_DEVICE(0x14B2, 0x3C28)},	/* Conceptronic */
	{USB_DEVICE(0x2019, 0xED06)},	/* Planex Communications, Inc. */
	{USB_DEVICE(0x07D1, 0x3C09)},	/* D-Link */
	{USB_DEVICE(0x07D1, 0x3C11)},	/* D-Link */
	{USB_DEVICE(0x14B2, 0x3C07)},	/* AL */
	{USB_DEVICE(0x050D, 0x8053)},	/* Belkin */
	{USB_DEVICE(0x050D, 0x825B)},	/* Belkin */
	{USB_DEVICE(0x050D, 0x935A)},	/* Belkin F6D4050 v1 */
	{USB_DEVICE(0x050D, 0x935B)},	/* Belkin F6D4050 v2 */
	{USB_DEVICE(0x14B2, 0x3C23)},	/* Airlink */
	{USB_DEVICE(0x14B2, 0x3C27)},	/* Airlink */
	{USB_DEVICE(0x07AA, 0x002F)},	/* Corega */
	{USB_DEVICE(0x07AA, 0x003C)},	/* Corega */
	{USB_DEVICE(0x07AA, 0x003F)},	/* Corega */
	{USB_DEVICE(0x1044, 0x800B)},	/* Gigabyte */
	{USB_DEVICE(0x15A9, 0x0006)},	/* Sparklan */
	{USB_DEVICE(0x083A, 0xB522)},	/* SMC */
	{USB_DEVICE(0x083A, 0xA618)},	/* SMC */
	{USB_DEVICE(0x083A, 0x8522)},	/* Arcadyan */
	{USB_DEVICE(0x083A, 0x7522)},	/* Arcadyan */
	{USB_DEVICE(0x0CDE, 0x0022)},	/* ZCOM */
	{USB_DEVICE(0x0586, 0x3416)},	/* Zyxel */
	{USB_DEVICE(0x0586, 0x341a)},	/* Zyxel NWD-270N */
	{USB_DEVICE(0x0CDE, 0x0025)},	/* Zyxel */
	{USB_DEVICE(0x1740, 0x9701)},	/* EnGenius */
	{USB_DEVICE(0x1740, 0x9702)},	/* EnGenius */
	{USB_DEVICE(0x0471, 0x200f)},	/* Philips */
	{USB_DEVICE(0x14B2, 0x3C25)},	/* Draytek */
	{USB_DEVICE(0x13D3, 0x3247)},	/* AzureWave */
	{USB_DEVICE(0x083A, 0x6618)},	/* Accton */
	{USB_DEVICE(0x15c5, 0x0008)},	/* Amit */
	{USB_DEVICE(0x0E66, 0x0001)},	/* Hawking */
	{USB_DEVICE(0x0E66, 0x0003)},	/* Hawking */
	{USB_DEVICE(0x129B, 0x1828)},	/* Siemens */
	{USB_DEVICE(0x157E, 0x300E)},	/* U-Media */
	{USB_DEVICE(0x050d, 0x805c)},
	{USB_DEVICE(0x050d, 0x815c)},
	{USB_DEVICE(0x1482, 0x3C09)},	/* Abocom */
	{USB_DEVICE(0x14B2, 0x3C09)},	/* Alpha */
	{USB_DEVICE(0x04E8, 0x2018)},	/* samsung linkstick2 */
	{USB_DEVICE(0x1690, 0x0740)},	/* Askey */
	{USB_DEVICE(0x5A57, 0x0280)},	/* Zinwell */
	{USB_DEVICE(0x5A57, 0x0282)},	/* Zinwell */
	{USB_DEVICE(0x7392, 0x7718)},
	{USB_DEVICE(0x7392, 0x7717)},
	{USB_DEVICE(0x0411, 0x016f)},	/* MelCo.,Inc. WLI-UC-G301N */
	{USB_DEVICE(0x1737, 0x0070)},	/* Linksys WUSB100 */
	{USB_DEVICE(0x1737, 0x0071)},	/* Linksys WUSB600N */
	{USB_DEVICE(0x0411, 0x00e8)},	/* Buffalo WLI-UC-G300N */
	{USB_DEVICE(0x050d, 0x815c)},	/* Belkin F5D8053 */
	{USB_DEVICE(0x100D, 0x9031)},	/* Motorola 2770 */
#endif /* RT2870 // */
#ifdef RT3070
	{USB_DEVICE(0x148F, 0x3070)},	/* Ralink 3070 */
	{USB_DEVICE(0x148F, 0x3071)},	/* Ralink 3071 */
	{USB_DEVICE(0x148F, 0x3072)},	/* Ralink 3072 */
	{USB_DEVICE(0x0DB0, 0x3820)},	/* Ralink 3070 */
	{USB_DEVICE(0x0DB0, 0x871C)},	/* Ralink 3070 */
	{USB_DEVICE(0x0DB0, 0x822C)},	/* Ralink 3070 */
	{USB_DEVICE(0x0DB0, 0x871B)},	/* Ralink 3070 */
	{USB_DEVICE(0x0DB0, 0x822B)},	/* Ralink 3070 */
	{USB_DEVICE(0x0DF6, 0x003E)},	/* Sitecom 3070 */
	{USB_DEVICE(0x0DF6, 0x0042)},	/* Sitecom 3072 */
	{USB_DEVICE(0x0DF6, 0x0048)},	/* Sitecom 3070 */
	{USB_DEVICE(0x0DF6, 0x0047)},	/* Sitecom 3071 */
	{USB_DEVICE(0x14B2, 0x3C12)},	/* AL 3070 */
	{USB_DEVICE(0x18C5, 0x0012)},	/* Corega 3070 */
	{USB_DEVICE(0x083A, 0x7511)},	/* Arcadyan 3070 */
	{USB_DEVICE(0x083A, 0xA701)},	/* SMC 3070 */
	{USB_DEVICE(0x083A, 0xA702)},	/* SMC 3072 */
	{USB_DEVICE(0x1740, 0x9703)},	/* EnGenius 3070 */
	{USB_DEVICE(0x1740, 0x9705)},	/* EnGenius 3071 */
	{USB_DEVICE(0x1740, 0x9706)},	/* EnGenius 3072 */
	{USB_DEVICE(0x1740, 0x9707)},	/* EnGenius 3070 */
	{USB_DEVICE(0x1740, 0x9708)},	/* EnGenius 3071 */
	{USB_DEVICE(0x1740, 0x9709)},	/* EnGenius 3072 */
	{USB_DEVICE(0x13D3, 0x3273)},	/* AzureWave 3070 */
	{USB_DEVICE(0x13D3, 0x3305)},	/* AzureWave 3070*/
	{USB_DEVICE(0x1044, 0x800D)},	/* Gigabyte GN-WB32L 3070 */
	{USB_DEVICE(0x2019, 0xAB25)},	/* Planex Communications, Inc. RT3070 */
	{USB_DEVICE(0x07B8, 0x3070)},	/* AboCom 3070 */
	{USB_DEVICE(0x07B8, 0x3071)},	/* AboCom 3071 */
	{USB_DEVICE(0x07B8, 0x3072)},	/* Abocom 3072 */
	{USB_DEVICE(0x7392, 0x7711)},	/* Edimax 3070 */
	{USB_DEVICE(0x1A32, 0x0304)},	/* Quanta 3070 */
	{USB_DEVICE(0x1EDA, 0x2310)},	/* AirTies 3070 */
	{USB_DEVICE(0x07D1, 0x3C0A)},	/* D-Link 3072 */
	{USB_DEVICE(0x07D1, 0x3C0D)},	/* D-Link 3070 */
	{USB_DEVICE(0x07D1, 0x3C0E)},	/* D-Link 3070 */
	{USB_DEVICE(0x07D1, 0x3C0F)},	/* D-Link 3070 */
	{USB_DEVICE(0x07D1, 0x3C16)},	/* D-Link 3070 */
	{USB_DEVICE(0x07D1, 0x3C17)},	/* D-Link 8070 */
	{USB_DEVICE(0x1D4D, 0x000C)},	/* Pegatron Corporation 3070 */
	{USB_DEVICE(0x1D4D, 0x000E)},	/* Pegatron Corporation 3070 */
	{USB_DEVICE(0x5A57, 0x5257)},	/* Zinwell 3070 */
	{USB_DEVICE(0x5A57, 0x0283)},	/* Zinwell 3072 */
	{USB_DEVICE(0x04BB, 0x0945)},	/* I-O DATA 3072 */
	{USB_DEVICE(0x04BB, 0x0947)},	/* I-O DATA 3070 */
	{USB_DEVICE(0x04BB, 0x0948)},	/* I-O DATA 3072 */
	{USB_DEVICE(0x203D, 0x1480)},	/* Encore 3070 */
	{USB_DEVICE(0x20B8, 0x8888)},	/* PARA INDUSTRIAL 3070 */
	{USB_DEVICE(0x0B05, 0x1784)},	/* Asus 3072 */
	{USB_DEVICE(0x203D, 0x14A9)},	/* Encore 3070*/
	{USB_DEVICE(0x0DB0, 0x899A)},	/* MSI 3070*/
	{USB_DEVICE(0x0DB0, 0x3870)},	/* MSI 3070*/
	{USB_DEVICE(0x0DB0, 0x870A)},	/* MSI 3070*/
	{USB_DEVICE(0x0DB0, 0x6899)},	/* MSI 3070 */
	{USB_DEVICE(0x0DB0, 0x3822)},	/* MSI 3070 */
	{USB_DEVICE(0x0DB0, 0x3871)},	/* MSI 3070 */
	{USB_DEVICE(0x0DB0, 0x871A)},	/* MSI 3070 */
	{USB_DEVICE(0x0DB0, 0x822A)},	/* MSI 3070 */
	{USB_DEVICE(0x0DB0, 0x3821)},	/* Ralink 3070 */
	{USB_DEVICE(0x0DB0, 0x821A)},	/* Ralink 3070 */
	{USB_DEVICE(0x083A, 0xA703)},	/* IO-MAGIC */
	{USB_DEVICE(0x13D3, 0x3307)},	/* Azurewave */
	{USB_DEVICE(0x13D3, 0x3321)},	/* Azurewave */
	{USB_DEVICE(0x07FA, 0x7712)},	/* Edimax */
	{USB_DEVICE(0x0789, 0x0166)},	/* Edimax */
	{USB_DEVICE(0x148F, 0x2070)},	/* Edimax */
#endif /* RT3070 // */
	{USB_DEVICE(0x1737, 0x0077)},	/* Linksys WUSB54GC-EU v3 */
	{USB_DEVICE(0x2001, 0x3C09)},	/* D-Link */
	{USB_DEVICE(0x2001, 0x3C0A)},	/* D-Link 3072 */
	{USB_DEVICE(0x2019, 0xED14)},	/* Planex Communications, Inc. */
	{USB_DEVICE(0x0411, 0x015D)},	/* Buffalo Airstation WLI-UC-GN */
	{}			/* Terminating entry */
};

int const rtusb_usb_id_len =
    sizeof(rtusb_usb_id) / sizeof(struct usb_device_id);

MODULE_DEVICE_TABLE(usb, rtusb_usb_id);

static void rt2870_disconnect(struct usb_device *dev, struct rt_rtmp_adapter *pAd);

static int __devinit rt2870_probe(IN struct usb_interface *intf,
				  IN struct usb_device *usb_dev,
				  IN const struct usb_device_id *dev_id,
				  struct rt_rtmp_adapter **ppAd);

#ifndef PF_NOFREEZE
#define PF_NOFREEZE  0
#endif

extern int rt28xx_close(IN struct net_device *net_dev);
extern int rt28xx_open(struct net_device *net_dev);

static BOOLEAN USBDevConfigInit(IN struct usb_device *dev,
				IN struct usb_interface *intf,
				struct rt_rtmp_adapter *pAd);

/*
========================================================================
Routine Description:
    Check the chipset vendor/product ID.

Arguments:
    _dev_p				Point to the PCI or USB device

Return Value:
    TRUE				Check ok
	FALSE				Check fail

Note:
========================================================================
*/
BOOLEAN RT28XXChipsetCheck(IN void *_dev_p)
{
	struct usb_interface *intf = (struct usb_interface *)_dev_p;
	struct usb_device *dev_p = interface_to_usbdev(intf);
	u32 i;

	for (i = 0; i < rtusb_usb_id_len; i++) {
		if (dev_p->descriptor.idVendor == rtusb_usb_id[i].idVendor &&
		    dev_p->descriptor.idProduct == rtusb_usb_id[i].idProduct) {
			printk("rt2870: idVendor = 0x%x, idProduct = 0x%x\n",
			       dev_p->descriptor.idVendor,
			       dev_p->descriptor.idProduct);
			break;
		}
	}

	if (i == rtusb_usb_id_len) {
		printk("rt2870: Error! Device Descriptor not matching!\n");
		return FALSE;
	}

	return TRUE;
}

/**************************************************************************/
/**************************************************************************/
/*tested for kernel 2.6series */
/**************************************************************************/
/**************************************************************************/

#ifdef CONFIG_PM
static int rt2870_suspend(struct usb_interface *intf, pm_message_t state);
static int rt2870_resume(struct usb_interface *intf);
#endif /* CONFIG_PM // */

static BOOLEAN USBDevConfigInit(IN struct usb_device *dev,
				IN struct usb_interface *intf,
				struct rt_rtmp_adapter *pAd)
{
	struct usb_host_interface *iface_desc;
	unsigned long BulkOutIdx;
	u32 i;

	/* get the active interface descriptor */
	iface_desc = intf->cur_altsetting;

	/* get # of enpoints  */
	pAd->NumberOfPipes = iface_desc->desc.bNumEndpoints;
	DBGPRINT(RT_DEBUG_TRACE,
		 ("NumEndpoints=%d\n", iface_desc->desc.bNumEndpoints));

	/* Configure Pipes */
	BulkOutIdx = 0;

	for (i = 0; i < pAd->NumberOfPipes; i++) {
		if ((iface_desc->endpoint[i].desc.bmAttributes ==
		     USB_ENDPOINT_XFER_BULK) &&
		    ((iface_desc->endpoint[i].desc.bEndpointAddress &
		      USB_ENDPOINT_DIR_MASK) == USB_DIR_IN)) {
			pAd->BulkInEpAddr =
			    iface_desc->endpoint[i].desc.bEndpointAddress;
			pAd->BulkInMaxPacketSize =
			    le2cpu16(iface_desc->endpoint[i].desc.
				     wMaxPacketSize);

			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     ("BULK IN MaxPacketSize = %d\n",
				      pAd->BulkInMaxPacketSize));
			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     ("EP address = 0x%2x\n",
				      iface_desc->endpoint[i].desc.
				      bEndpointAddress));
		} else
		    if ((iface_desc->endpoint[i].desc.bmAttributes ==
			 USB_ENDPOINT_XFER_BULK)
			&&
			((iface_desc->endpoint[i].desc.
			  bEndpointAddress & USB_ENDPOINT_DIR_MASK) ==
			 USB_DIR_OUT)) {
			/* there are 6 bulk out EP. EP6 highest priority. */
			/* EP1-4 is EDCA.  EP5 is HCCA. */
			pAd->BulkOutEpAddr[BulkOutIdx++] =
			    iface_desc->endpoint[i].desc.bEndpointAddress;
			pAd->BulkOutMaxPacketSize =
			    le2cpu16(iface_desc->endpoint[i].desc.
				     wMaxPacketSize);

			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     ("BULK OUT MaxPacketSize = %d\n",
				      pAd->BulkOutMaxPacketSize));
			DBGPRINT_RAW(RT_DEBUG_TRACE,
				     ("EP address = 0x%2x  \n",
				      iface_desc->endpoint[i].desc.
				      bEndpointAddress));
		}
	}

	if (!(pAd->BulkInEpAddr && pAd->BulkOutEpAddr[0])) {
		printk
		    ("%s: Could not find both bulk-in and bulk-out endpoints\n",
		     __FUNCTION__);
		return FALSE;
	}

	pAd->config = &dev->config->desc;
	usb_set_intfdata(intf, pAd);

	return TRUE;

}

static int __devinit rtusb_probe(struct usb_interface *intf,
		       const struct usb_device_id *id)
{
	struct rt_rtmp_adapter *pAd;
	struct usb_device *dev;
	int rv;

	dev = interface_to_usbdev(intf);
	dev = usb_get_dev(dev);

	rv = rt2870_probe(intf, dev, id, &pAd);
	if (rv != 0)
		usb_put_dev(dev);

	return rv;
}

static void rtusb_disconnect(struct usb_interface *intf)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	struct rt_rtmp_adapter *pAd;

	pAd = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);

	rt2870_disconnect(dev, pAd);
}

struct usb_driver rtusb_driver = {
	.name = "rt2870",
	.probe = rtusb_probe,
	.disconnect = rtusb_disconnect,
	.id_table = rtusb_usb_id,

#ifdef CONFIG_PM
suspend:rt2870_suspend,
resume:rt2870_resume,
#endif
};

#ifdef CONFIG_PM

void RT2870RejectPendingPackets(struct rt_rtmp_adapter *pAd)
{
	/* clear PS packets */
	/* clear TxSw packets */
}

static int rt2870_suspend(struct usb_interface *intf, pm_message_t state)
{
	struct net_device *net_dev;
	struct rt_rtmp_adapter *pAd = usb_get_intfdata(intf);

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt2870_suspend()\n"));
	net_dev = pAd->net_dev;
	netif_device_detach(net_dev);

	pAd->PM_FlgSuspend = 1;
	if (netif_running(net_dev)) {
		RTUSBCancelPendingBulkInIRP(pAd);
		RTUSBCancelPendingBulkOutIRP(pAd);
	}
	DBGPRINT(RT_DEBUG_TRACE, ("<=== rt2870_suspend()\n"));
	return 0;
}

static int rt2870_resume(struct usb_interface *intf)
{
	struct net_device *net_dev;
	struct rt_rtmp_adapter *pAd = usb_get_intfdata(intf);

	DBGPRINT(RT_DEBUG_TRACE, ("===> rt2870_resume()\n"));

	pAd->PM_FlgSuspend = 0;
	net_dev = pAd->net_dev;
	netif_device_attach(net_dev);
	netif_start_queue(net_dev);
	netif_carrier_on(net_dev);
	netif_wake_queue(net_dev);

	DBGPRINT(RT_DEBUG_TRACE, ("<=== rt2870_resume()\n"));
	return 0;
}
#endif /* CONFIG_PM // */

/* Init driver module */
int __init rtusb_init(void)
{
	printk("rtusb init --->\n");
	return usb_register(&rtusb_driver);
}

/* Deinit driver module */
void __exit rtusb_exit(void)
{
	usb_deregister(&rtusb_driver);
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
    MLME kernel thread.

Arguments:
	*Context			the pAd, driver control block pointer

Return Value:
    0					close the thread

Note:
========================================================================
*/
int MlmeThread(IN void *Context)
{
	struct rt_rtmp_adapter *pAd;
	struct rt_rtmp_os_task *pTask;
	int status;
	status = 0;

	pTask = Context;
	pAd = pTask->priv;

	RtmpOSTaskCustomize(pTask);

	while (!pTask->task_killed) {
#ifdef KTHREAD_SUPPORT
		RTMP_WAIT_EVENT_INTERRUPTIBLE(pAd, pTask);
#else
		RTMP_SEM_EVENT_WAIT(&(pTask->taskSema), status);

		/* unlock the device pointers */
		if (status != 0) {
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);
			break;
		}
#endif

		/* lock the device pointers , need to check if required */
		/*down(&(pAd->usbdev_semaphore)); */

		if (!pAd->PM_FlgSuspend)
			MlmeHandler(pAd);
	}

	/* notify the exit routine that we're actually exiting now
	 *
	 * complete()/wait_for_completion() is similar to up()/down(),
	 * except that complete() is safe in the case where the structure
	 * is getting deleted in a parallel mode of execution (i.e. just
	 * after the down() -- that's necessary for the thread-shutdown
	 * case.
	 *
	 * complete_and_exit() goes even further than this -- it is safe in
	 * the case that the thread of the caller is going away (not just
	 * the structure) -- this is necessary for the module-remove case.
	 * This is important in preemption kernels, which transfer the flow
	 * of execution immediately upon a complete().
	 */
	DBGPRINT(RT_DEBUG_TRACE, ("<---%s\n", __FUNCTION__));
#ifndef KTHREAD_SUPPORT
	pTask->taskPID = THREAD_PID_INIT_VALUE;
	complete_and_exit(&pTask->taskComplete, 0);
#endif
	return 0;

}

/*
========================================================================
Routine Description:
    USB command kernel thread.

Arguments:
	*Context			the pAd, driver control block pointer

Return Value:
    0					close the thread

Note:
========================================================================
*/
int RTUSBCmdThread(IN void *Context)
{
	struct rt_rtmp_adapter *pAd;
	struct rt_rtmp_os_task *pTask;
	int status;
	status = 0;

	pTask = Context;
	pAd = pTask->priv;

	RtmpOSTaskCustomize(pTask);

	NdisAcquireSpinLock(&pAd->CmdQLock);
	pAd->CmdQ.CmdQState = RTMP_TASK_STAT_RUNNING;
	NdisReleaseSpinLock(&pAd->CmdQLock);

	while (pAd && pAd->CmdQ.CmdQState == RTMP_TASK_STAT_RUNNING) {
#ifdef KTHREAD_SUPPORT
		RTMP_WAIT_EVENT_INTERRUPTIBLE(pAd, pTask);
#else
		/* lock the device pointers */
		RTMP_SEM_EVENT_WAIT(&(pTask->taskSema), status);

		if (status != 0) {
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS);
			break;
		}
#endif

		if (pAd->CmdQ.CmdQState == RTMP_TASK_STAT_STOPED)
			break;

		if (!pAd->PM_FlgSuspend)
			CMDHandler(pAd);
	}

	if (pAd && !pAd->PM_FlgSuspend) {	/* Clear the CmdQElements. */
		struct rt_cmdqelmt *pCmdQElmt = NULL;

		NdisAcquireSpinLock(&pAd->CmdQLock);
		pAd->CmdQ.CmdQState = RTMP_TASK_STAT_STOPED;
		while (pAd->CmdQ.size) {
			RTUSBDequeueCmd(&pAd->CmdQ, &pCmdQElmt);
			if (pCmdQElmt) {
				if (pCmdQElmt->CmdFromNdis == TRUE) {
					if (pCmdQElmt->buffer != NULL)
						os_free_mem(pAd,
							    pCmdQElmt->buffer);
					os_free_mem(pAd, (u8 *)pCmdQElmt);
				} else {
					if ((pCmdQElmt->buffer != NULL)
					    && (pCmdQElmt->bufferlength != 0))
						os_free_mem(pAd,
							    pCmdQElmt->buffer);
					os_free_mem(pAd, (u8 *)pCmdQElmt);
				}
			}
		}

		NdisReleaseSpinLock(&pAd->CmdQLock);
	}
	/* notify the exit routine that we're actually exiting now
	 *
	 * complete()/wait_for_completion() is similar to up()/down(),
	 * except that complete() is safe in the case where the structure
	 * is getting deleted in a parallel mode of execution (i.e. just
	 * after the down() -- that's necessary for the thread-shutdown
	 * case.
	 *
	 * complete_and_exit() goes even further than this -- it is safe in
	 * the case that the thread of the caller is going away (not just
	 * the structure) -- this is necessary for the module-remove case.
	 * This is important in preemption kernels, which transfer the flow
	 * of execution immediately upon a complete().
	 */
	DBGPRINT(RT_DEBUG_TRACE, ("<---RTUSBCmdThread\n"));

#ifndef KTHREAD_SUPPORT
	pTask->taskPID = THREAD_PID_INIT_VALUE;
	complete_and_exit(&pTask->taskComplete, 0);
#endif
	return 0;

}

void RTUSBWatchDog(struct rt_rtmp_adapter *pAd)
{
	struct rt_ht_tx_context *pHTTXContext;
	int idx;
	unsigned long irqFlags;
	PURB pUrb;
	BOOLEAN needDumpSeq = FALSE;
	u32 MACValue;
	u32 TxRxQ_Pcnt;

	idx = 0;
	RTMP_IO_READ32(pAd, TXRXQ_PCNT, &MACValue);
	if ((MACValue & 0xff) != 0) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("TX QUEUE 0 Not EMPTY(Value=0x%0x)!\n",
			  MACValue));
		RTMP_IO_WRITE32(pAd, PBF_CFG, 0xf40012);
		while ((MACValue & 0xff) != 0 && (idx++ < 10)) {
			RTMP_IO_READ32(pAd, TXRXQ_PCNT, &MACValue);
			RTMPusecDelay(1);
		}
		RTMP_IO_WRITE32(pAd, PBF_CFG, 0xf40006);
	}

	if (pAd->watchDogRxOverFlowCnt >= 2) {
		DBGPRINT(RT_DEBUG_TRACE,
			 ("Maybe the Rx Bulk-In hanged! Cancel the pending Rx bulks request!\n"));
		if ((!RTMP_TEST_FLAG
		     (pAd,
		      (fRTMP_ADAPTER_RESET_IN_PROGRESS |
		       fRTMP_ADAPTER_BULKIN_RESET |
		       fRTMP_ADAPTER_HALT_IN_PROGRESS |
		       fRTMP_ADAPTER_NIC_NOT_EXIST)))) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("Call CMDTHREAD_RESET_BULK_IN to cancel the pending Rx Bulk!\n"));
			RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_BULKIN_RESET);
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_RESET_BULK_IN,
						NULL, 0);
			needDumpSeq = TRUE;
		}
		pAd->watchDogRxOverFlowCnt = 0;
	}

	RTUSBReadMACRegister(pAd, 0x438, &TxRxQ_Pcnt);

	for (idx = 0; idx < NUM_OF_TX_RING; idx++) {
		pUrb = NULL;

		RTMP_IRQ_LOCK(&pAd->BulkOutLock[idx], irqFlags);
		if ((pAd->BulkOutPending[idx] == TRUE)
		    && pAd->watchDogTxPendingCnt) {
			int actual_length = 0, transfer_buffer_length = 0;
			BOOLEAN isDataPacket = FALSE;
			pAd->watchDogTxPendingCnt[idx]++;

			if ((pAd->watchDogTxPendingCnt[idx] > 2) &&
			    (!RTMP_TEST_FLAG
			     (pAd,
			      (fRTMP_ADAPTER_RESET_IN_PROGRESS |
			       fRTMP_ADAPTER_HALT_IN_PROGRESS |
			       fRTMP_ADAPTER_NIC_NOT_EXIST |
			       fRTMP_ADAPTER_BULKOUT_RESET)))
			    ) {
				/* FIXME: Following code just support single bulk out. If you wanna support multiple bulk out. Modify it! */
				pHTTXContext =
				    (struct rt_ht_tx_context *)(&pAd->TxContext[idx]);
				if (pHTTXContext->IRPPending) {	/* Check TxContext. */
					pUrb = pHTTXContext->pUrb;

					actual_length = pUrb->actual_length;
					transfer_buffer_length =
					    pUrb->transfer_buffer_length;
					isDataPacket = TRUE;
				} else if (idx == MGMTPIPEIDX) {
					struct rt_tx_context *pMLMEContext, *pNULLContext,
					    *pPsPollContext;

					/*Check MgmtContext. */
					pMLMEContext =
					    (struct rt_tx_context *)(pAd->MgmtRing.
							   Cell[pAd->MgmtRing.
								TxDmaIdx].
							   AllocVa);
					pPsPollContext =
					    (struct rt_tx_context *)(&pAd->PsPollContext);
					pNULLContext =
					    (struct rt_tx_context *)(&pAd->NullContext);

					if (pMLMEContext->IRPPending) {
						ASSERT(pMLMEContext->
						       IRPPending);
						pUrb = pMLMEContext->pUrb;
					} else if (pNULLContext->IRPPending) {
						ASSERT(pNULLContext->
						       IRPPending);
						pUrb = pNULLContext->pUrb;
					} else if (pPsPollContext->IRPPending) {
						ASSERT(pPsPollContext->
						       IRPPending);
						pUrb = pPsPollContext->pUrb;
					}
				}

				RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[idx],
						irqFlags);

				printk(KERN_INFO "%d:%lu LTL=%d , TL=%d L:%d\n",
				       idx, pAd->watchDogTxPendingCnt[idx],
				       pAd->TransferedLength[idx],
				       actual_length, transfer_buffer_length);

				if (pUrb) {
					if ((isDataPacket
					     && pAd->TransferedLength[idx] ==
					     actual_length
					     && pAd->TransferedLength[idx] <
					     transfer_buffer_length
					     && actual_length != 0
/*                                      && TxRxQ_Pcnt==0 */
					     && pAd->watchDogTxPendingCnt[idx] >
					     3)
					    || isDataPacket == FALSE
					    || pAd->watchDogTxPendingCnt[idx] >
					    6) {
						DBGPRINT(RT_DEBUG_TRACE,
							 ("Maybe the Tx Bulk-Out hanged! Cancel the pending Tx bulks request of idx(%d)!\n",
							  idx));
						DBGPRINT(RT_DEBUG_TRACE,
							 ("Unlink the pending URB!\n"));
						/* unlink it now */
						RTUSB_UNLINK_URB(pUrb);
						/* Sleep 200 microseconds to give cancellation time to work */
						/*RTMPusecDelay(200); */
						needDumpSeq = TRUE;
					}
				} else {
					DBGPRINT(RT_DEBUG_ERROR,
						 ("Unknown bulkOut URB maybe hanged!\n"));
				}
			} else {
				RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[idx],
						irqFlags);
			}

			if (isDataPacket == TRUE)
				pAd->TransferedLength[idx] = actual_length;
		} else {
			RTMP_IRQ_UNLOCK(&pAd->BulkOutLock[idx], irqFlags);
		}
	}

	/* For Sigma debug, dump the ba_reordering sequence. */
	if ((needDumpSeq == TRUE) && (pAd->CommonCfg.bDisableReordering == 0)) {
		u16 Idx;
		struct rt_ba_rec_entry *pBAEntry = NULL;
		u8 count = 0;
		struct reordering_mpdu *mpdu_blk;

		Idx = pAd->MacTab.Content[BSSID_WCID].BARecWcidArray[0];

		pBAEntry = &pAd->BATable.BARecEntry[Idx];
		if ((pBAEntry->list.qlen > 0) && (pBAEntry->list.next != NULL)) {
			DBGPRINT(RT_DEBUG_TRACE,
				 ("NICUpdateRawCounters():The Queueing pkt in reordering buffer:\n"));
			NdisAcquireSpinLock(&pBAEntry->RxReRingLock);
			mpdu_blk = pBAEntry->list.next;
			while (mpdu_blk) {
				DBGPRINT(RT_DEBUG_TRACE,
					 ("\t%d:Seq-%d, bAMSDU-%d!\n", count,
					  mpdu_blk->Sequence,
					  mpdu_blk->bAMSDU));
				mpdu_blk = mpdu_blk->next;
				count++;
			}

			DBGPRINT(RT_DEBUG_TRACE,
				 ("\npBAEntry->LastIndSeq=%d!\n",
				  pBAEntry->LastIndSeq));
			NdisReleaseSpinLock(&pBAEntry->RxReRingLock);
		}
	}
}

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
static void rt2870_disconnect(struct usb_device *dev, struct rt_rtmp_adapter *pAd)
{
	DBGPRINT(RT_DEBUG_ERROR,
		 ("rtusb_disconnect: unregister usbnet usb-%s-%s\n",
		  dev->bus->bus_name, dev->devpath));
	if (!pAd) {
		usb_put_dev(dev);
		printk("rtusb_disconnect: pAd == NULL!\n");
		return;
	}
	RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST);

	/* for debug, wait to show some messages to /proc system */
	udelay(1);

	RtmpPhyNetDevExit(pAd, pAd->net_dev);

	/* FIXME: Shall we need following delay and flush the schedule?? */
	udelay(1);
	flush_scheduled_work();
	udelay(1);

	/* free the root net_device */
	RtmpOSNetDevFree(pAd->net_dev);

	RtmpRaDevCtrlExit(pAd);

	/* release a use of the usb device structure */
	usb_put_dev(dev);
	udelay(1);

	DBGPRINT(RT_DEBUG_ERROR, (" RTUSB disconnect successfully\n"));
}

static int __devinit rt2870_probe(IN struct usb_interface *intf,
				  IN struct usb_device *usb_dev,
				  IN const struct usb_device_id *dev_id,
				  struct rt_rtmp_adapter **ppAd)
{
	struct net_device *net_dev = NULL;
	struct rt_rtmp_adapter *pAd = (struct rt_rtmp_adapter *)NULL;
	int status, rv;
	void *handle;
	struct rt_rtmp_os_netdev_op_hook netDevHook;

	DBGPRINT(RT_DEBUG_TRACE, ("===>rt2870_probe()!\n"));

	/* Check chipset vendor/product ID */
	/*if (RT28XXChipsetCheck(_dev_p) == FALSE) */
	/*      goto err_out; */

/*RtmpDevInit============================================= */
	/* Allocate struct rt_rtmp_adapter adapter structure */
	handle = kmalloc(sizeof(struct os_cookie), GFP_KERNEL);
	if (handle == NULL) {
		printk
		    ("rt2870_probe(): Allocate memory for os handle failed!\n");
		return -ENOMEM;
	}
	((struct os_cookie *)handle)->pUsb_Dev = usb_dev;

	rv = RTMPAllocAdapterBlock(handle, &pAd);
	if (rv != NDIS_STATUS_SUCCESS) {
		kfree(handle);
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

	/* Here are the net_device structure with usb specific parameters. 
	 * for supporting Network Manager.
	 * Set the sysfs physical device reference for the network logical device if set prior to registration will
	 * cause a symlink during initialization.
	 */
	SET_NETDEV_DEV(net_dev, &(usb_dev->dev));

	pAd->StaCfg.OriDevType = net_dev->type;

/*All done, it's time to register the net device to linux kernel. */
	/* Register this device */
	status = RtmpOSNetDevAttach(net_dev, &netDevHook);
	if (status != 0)
		goto err_out_free_netdev;

#ifdef KTHREAD_SUPPORT
	init_waitqueue_head(&pAd->mlmeTask.kthread_q);
	init_waitqueue_head(&pAd->timerTask.kthread_q);
	init_waitqueue_head(&pAd->cmdQTask.kthread_q);
#endif

	*ppAd = pAd;

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
