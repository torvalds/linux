/*******************************************************************************
 * Agere Systems Inc.
 * Wireless device driver for Linux (wlags49).
 *
 * Copyright (c) 1998-2003 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 * Initially developed by TriplePoint, Inc.
 *   http://www.triplepoint.com
 *
 *------------------------------------------------------------------------------
 *
 *   This file contains processing and initialization specific to Card Services
 *   devices (PCMCIA, CF).
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright (c) 2003 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 ******************************************************************************/

/*******************************************************************************
 *  include files
 ******************************************************************************/
#include <wl_version.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>

#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <debug.h>

#include <hcf.h>
#include <dhf.h>
#include <hcfdef.h>

#include <wl_if.h>
#include <wl_internal.h>
#include <wl_util.h>
#include <wl_main.h>
#include <wl_netdev.h>
#include <wl_cs.h>
#include <wl_sysfs.h>


/*******************************************************************************
 *  global definitions
 ******************************************************************************/
#if DBG
extern dbg_info_t *DbgInfo;
#endif  /* DBG */


/*******************************************************************************
 *	wl_adapter_attach()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Creates an instance of the driver, allocating local data structures for
 *  one device. The device is registered with Card Services.
 *
 *  PARAMETERS:
 *
 *      none
 *
 *  RETURNS:
 *
 *      pointer to an allocated dev_link_t structure
 *      NULL on failure
 *
 ******************************************************************************/
static int wl_adapter_attach(struct pcmcia_device *link)
{
	struct net_device   *dev;
	struct wl_private   *lp;
	/*--------------------------------------------------------------------*/

	DBG_FUNC("wl_adapter_attach");
	DBG_ENTER(DbgInfo);

	dev = wl_device_alloc();
	if (dev == NULL) {
		DBG_ERROR(DbgInfo, "wl_device_alloc returned NULL\n");
		return -ENOMEM;
	}

	link->resource[0]->end  = HCF_NUM_IO_PORTS;
	link->resource[0]->flags= IO_DATA_PATH_WIDTH_16;
	link->conf.Attributes   = CONF_ENABLE_IRQ;
	link->conf.IntType      = INT_MEMORY_AND_IO;
	link->conf.ConfigIndex  = 5;
	link->conf.Present      = PRESENT_OPTION;

	link->priv = dev;
	lp = wl_priv(dev);
	lp->link = link;

	wl_adapter_insert(link);

	DBG_LEAVE(DbgInfo);
	return 0;
} /* wl_adapter_attach */
/*============================================================================*/



/*******************************************************************************
 *	wl_adapter_detach()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      This deletes a driver "instance". The device is de-registered with Card
 *  Services. If it has been released, then the net device is unregistered, and
 *  all local data structures are freed. Otherwise, the structures will be
 *  freed when the device is released.
 *
 *  PARAMETERS:
 *
 *      link    - pointer to the dev_link_t structure representing the device to
 *                detach
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
static void wl_adapter_detach(struct pcmcia_device *link)
{
	struct net_device   *dev = link->priv;
	/*--------------------------------------------------------------------*/

	DBG_FUNC("wl_adapter_detach");
	DBG_ENTER(DbgInfo);
	DBG_PARAM(DbgInfo, "link", "0x%p", link);

	wl_adapter_release(link);

	if (dev) {
		unregister_wlags_sysfs(dev);
		unregister_netdev(dev);
	}

	wl_device_dealloc(dev);

	DBG_LEAVE(DbgInfo);
} /* wl_adapter_detach */
/*============================================================================*/


/*******************************************************************************
 *	wl_adapter_release()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      After a card is removed, this routine will release the PCMCIA
 *  configuration. If the device is still open, this will be postponed until it
 *  is closed.
 *
 *  PARAMETERS:
 *
 *      arg - a u_long representing a pointer to a dev_link_t structure for the
 *            device to be released.
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_adapter_release(struct pcmcia_device *link)
{
	DBG_FUNC("wl_adapter_release");
	DBG_ENTER(DbgInfo);
	DBG_PARAM(DbgInfo, "link", "0x%p", link);

	/* Stop hardware */
	wl_remove(link->priv);

	pcmcia_disable_device(link);

	DBG_LEAVE(DbgInfo);
} /* wl_adapter_release */
/*============================================================================*/

static int wl_adapter_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	/* if (link->open) { */
	netif_device_detach(dev);
	wl_suspend(dev);
	/* CHECK! pcmcia_release_configuration(link->handle); */
	/* } */

	return 0;
} /* wl_adapter_suspend */

static int wl_adapter_resume(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	wl_resume(dev);

	netif_device_attach(dev);

	return 0;
} /* wl_adapter_resume */

/*******************************************************************************
 *	wl_adapter_insert()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      wl_adapter_insert() is scheduled to run after a CARD_INSERTION event is
 *  received, to configure the PCMCIA socket, and to make the ethernet device
 *  available to the system.
 *
 *  PARAMETERS:
 *
 *      link    - pointer to the dev_link_t structure representing the device to
 *                insert
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_adapter_insert(struct pcmcia_device *link)
{
	struct net_device *dev;
	int i;
	int ret;
	/*--------------------------------------------------------------------*/

	DBG_FUNC("wl_adapter_insert");
	DBG_ENTER(DbgInfo);
	DBG_PARAM(DbgInfo, "link", "0x%p", link);

	dev     = link->priv;

	/* Do we need to allocate an interrupt? */
	link->conf.Attributes |= CONF_ENABLE_IRQ;
	link->io_lines = 6;

	ret = pcmcia_request_io(link);
	if (ret != 0)
		goto failed;

	ret = pcmcia_request_irq(link, (void *) wl_isr);
	if (ret != 0)
		goto failed;

	ret = pcmcia_request_configuration(link, &link->conf);
	if (ret != 0)
		goto failed;

	dev->irq        = link->irq;
	dev->base_addr  = link->resource[0]->start;

	SET_NETDEV_DEV(dev, &link->dev);
	if (register_netdev(dev) != 0) {
		printk("%s: register_netdev() failed\n", MODULE_NAME);
		goto failed;
	}

	register_wlags_sysfs(dev);

	printk(KERN_INFO "%s: Wireless, io_addr %#03lx, irq %d, ""mac_address ",
		dev->name, dev->base_addr, dev->irq);
	for (i = 0; i < ETH_ALEN; i++)
		printk("%02X%c", dev->dev_addr[i], ((i < (ETH_ALEN-1)) ? ':' : '\n'));

	DBG_LEAVE(DbgInfo);
	return;

failed:
	wl_adapter_release(link);

	DBG_LEAVE(DbgInfo);
	return;
} /* wl_adapter_insert */
/*============================================================================*/


/*******************************************************************************
 *	wl_adapter_open()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Open the device.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to a net_device structure representing the network
 *            device to open.
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_adapter_open(struct net_device *dev)
{
	struct wl_private *lp = wl_priv(dev);
	struct pcmcia_device *link = lp->link;
	int result = 0;
	int hcf_status = HCF_SUCCESS;
	/*--------------------------------------------------------------------*/

	DBG_FUNC("wl_adapter_open");
	DBG_ENTER(DbgInfo);
	DBG_PRINT("%s\n", VERSION_INFO);
	DBG_PARAM(DbgInfo, "dev", "%s (0x%p)", dev->name, dev);

	if (!pcmcia_dev_present(link)) {
		DBG_LEAVE(DbgInfo);
		return -ENODEV;
	}

	link->open++;

	hcf_status = wl_open(dev);

	if (hcf_status != HCF_SUCCESS) {
		link->open--;
		result = -ENODEV;
	}

	DBG_LEAVE(DbgInfo);
	return result;
} /* wl_adapter_open */
/*============================================================================*/


/*******************************************************************************
 *	wl_adapter_close()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Close the device.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to a net_device structure representing the network
 *            device to close.
 *
 *  RETURNS:
 *
 *      0 on success
 *      errno value otherwise
 *
 ******************************************************************************/
int wl_adapter_close(struct net_device *dev)
{
	struct wl_private *lp = wl_priv(dev);
	struct pcmcia_device *link = lp->link;
	/*--------------------------------------------------------------------*/

	DBG_FUNC("wl_adapter_close");
	DBG_ENTER(DbgInfo);
	DBG_PARAM(DbgInfo, "dev", "%s (0x%p)", dev->name, dev);

	if (link == NULL) {
		DBG_LEAVE(DbgInfo);
		return -ENODEV;
	}

	DBG_TRACE(DbgInfo, "%s: Shutting down adapter.\n", dev->name);
	wl_close(dev);

	link->open--;

	DBG_LEAVE(DbgInfo);
	return 0;
} /* wl_adapter_close */
/*============================================================================*/

static struct pcmcia_device_id wl_adapter_ids[] = {
#if !((HCF_TYPE) & HCF_TYPE_HII5)
	PCMCIA_DEVICE_MANF_CARD(0x0156, 0x0003),
	PCMCIA_DEVICE_PROD_ID12("Agere Systems", "Wireless PC Card Model 0110",
				0x33103a9b, 0xe175b0dd),
#else
	PCMCIA_DEVICE_MANF_CARD(0x0156, 0x0004),
	PCMCIA_DEVICE_PROD_ID12("Linksys", "WCF54G_Wireless-G_CompactFlash_Card",
				0x0733cc81, 0x98a599e1),
#endif  /* (HCF_TYPE) & HCF_TYPE_HII5 */
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, wl_adapter_ids);

static struct pcmcia_driver wlags49_driver = {
	.owner	    = THIS_MODULE,
	.drv	    = {
		.name = DRIVER_NAME,
	},
	.probe	    = wl_adapter_attach,
	.remove	    = wl_adapter_detach,
	.id_table   = wl_adapter_ids,
	.suspend    = wl_adapter_suspend,
	.resume	    = wl_adapter_resume,
};



/*******************************************************************************
 *	wl_adapter_init_module()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Called by init_module() to perform PCMCIA driver initialization.
 *
 *  PARAMETERS:
 *
 *      N/A
 *
 *  RETURNS:
 *
 *      0 on success
 *      -1 on error
 *
 ******************************************************************************/
int wl_adapter_init_module(void)
{
	int ret;
	/*--------------------------------------------------------------------*/

	DBG_FUNC("wl_adapter_init_module");
	DBG_ENTER(DbgInfo);
	DBG_TRACE(DbgInfo, "wl_adapter_init_module() -- PCMCIA\n");

	ret = pcmcia_register_driver(&wlags49_driver);

	DBG_LEAVE(DbgInfo);
	return ret;
} /* wl_adapter_init_module */
/*============================================================================*/


/*******************************************************************************
 *	wl_adapter_cleanup_module()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Called by cleanup_module() to perform driver uninitialization.
 *
 *  PARAMETERS:
 *
 *      N/A
 *
 *  RETURNS:
 *
 *      N/A
 *
 ******************************************************************************/
void wl_adapter_cleanup_module(void)
{
	DBG_FUNC("wl_adapter_cleanup_module");
	DBG_ENTER(DbgInfo);
	DBG_TRACE(DbgInfo, "wl_adapter_cleanup_module() -- PCMCIA\n");


	pcmcia_unregister_driver(&wlags49_driver);

	DBG_LEAVE(DbgInfo);
	return;
} /* wl_adapter_cleanup_module */
/*============================================================================*/


/*******************************************************************************
 *	wl_adapter_is_open()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Check with Card Services to determine if this device is open.
 *
 *  PARAMETERS:
 *
 *      dev - a pointer to the net_device structure whose open status will be
 *            checked
 *
 *  RETURNS:
 *
 *      nonzero if device is open
 *      0 otherwise
 *
 ******************************************************************************/
int wl_adapter_is_open(struct net_device *dev)
{
	struct wl_private *lp = wl_priv(dev);
	struct pcmcia_device *link = lp->link;

	if (!pcmcia_dev_present(link))
		return 0;

	return link->open;
} /* wl_adapter_is_open */
/*============================================================================*/


#if DBG

/*******************************************************************************
 *	DbgEvent()
 *******************************************************************************
 *
 *  DESCRIPTION:
 *
 *      Converts the card serivces events to text for debugging.
 *
 *  PARAMETERS:
 *
 *      mask    - a integer representing the error(s) being reported by Card
 *                Services.
 *
 *  RETURNS:
 *
 *      a pointer to a string describing the error(s)
 *
 ******************************************************************************/
const char *DbgEvent(int mask)
{
	static char DbgBuffer[256];
	char *pBuf;
	/*--------------------------------------------------------------------*/

	pBuf    = DbgBuffer;
	*pBuf   = '\0';


	if (mask & CS_EVENT_WRITE_PROTECT)
		strcat(pBuf, "WRITE_PROTECT ");

	if (mask & CS_EVENT_CARD_LOCK)
		strcat(pBuf, "CARD_LOCK ");

	if (mask & CS_EVENT_CARD_INSERTION)
		strcat(pBuf, "CARD_INSERTION ");

	if (mask & CS_EVENT_CARD_REMOVAL)
		strcat(pBuf, "CARD_REMOVAL ");

	if (mask & CS_EVENT_BATTERY_DEAD)
		strcat(pBuf, "BATTERY_DEAD ");

	if (mask & CS_EVENT_BATTERY_LOW)
		strcat(pBuf, "BATTERY_LOW ");

	if (mask & CS_EVENT_READY_CHANGE)
		strcat(pBuf, "READY_CHANGE ");

	if (mask & CS_EVENT_CARD_DETECT)
		strcat(pBuf, "CARD_DETECT ");

	if (mask & CS_EVENT_RESET_REQUEST)
		strcat(pBuf, "RESET_REQUEST ");

	if (mask & CS_EVENT_RESET_PHYSICAL)
		strcat(pBuf, "RESET_PHYSICAL ");

	if (mask & CS_EVENT_CARD_RESET)
		strcat(pBuf, "CARD_RESET ");

	if (mask & CS_EVENT_REGISTRATION_COMPLETE)
		strcat(pBuf, "REGISTRATION_COMPLETE ");

	/* if (mask & CS_EVENT_RESET_COMPLETE)
		strcat(pBuf, "RESET_COMPLETE "); */

	if (mask & CS_EVENT_PM_SUSPEND)
		strcat(pBuf, "PM_SUSPEND ");

	if (mask & CS_EVENT_PM_RESUME)
		strcat(pBuf, "PM_RESUME ");

	if (mask & CS_EVENT_INSERTION_REQUEST)
		strcat(pBuf, "INSERTION_REQUEST ");

	if (mask & CS_EVENT_EJECTION_REQUEST)
		strcat(pBuf, "EJECTION_REQUEST ");

	if (mask & CS_EVENT_MTD_REQUEST)
		strcat(pBuf, "MTD_REQUEST ");

	if (mask & CS_EVENT_ERASE_COMPLETE)
		strcat(pBuf, "ERASE_COMPLETE ");

	if (mask & CS_EVENT_REQUEST_ATTENTION)
		strcat(pBuf, "REQUEST_ATTENTION ");

	if (mask & CS_EVENT_CB_DETECT)
		strcat(pBuf, "CB_DETECT ");

	if (mask & CS_EVENT_3VCARD)
		strcat(pBuf, "3VCARD ");

	if (mask & CS_EVENT_XVCARD)
		strcat(pBuf, "XVCARD ");


	if (*pBuf) {
		pBuf[strlen(pBuf) - 1] = '\0';
	} else {
		if (mask != 0x0)
			sprintf(pBuf, "<<0x%08x>>", mask);
	}

	return pBuf;
} /* DbgEvent */
/*============================================================================*/

#endif  /* DBG */
