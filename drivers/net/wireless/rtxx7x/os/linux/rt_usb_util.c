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
#define RTMP_MODULE_OS_UTIL

#include "rtmp_comm.h"
#include "rtmp_osabl.h"
#include "rt_os_util.h"

#ifdef RTMP_MAC_USB
#ifdef OS_ABL_SUPPORT
MODULE_LICENSE("GPL");
#endif /* OS_ABL_SUPPORT */

#ifdef RESOURCE_BOOT_ALLOC
#include <linux/usb.h>
#include <hcd.h>

#define RTUSB_MAX_BUS_CNT 1

struct rtusb_bulk_mem{
	void *buf;
	dma_addr_t data_dma;
	int len;
	int assigned;
};


struct rtusb_mem_pool{
	struct rtusb_mem_pool *next;
	struct usb_bus *bus;
	struct device *dev;
	struct rtusb_bulk_mem *buf_pool;
	int pool_cnt;
};


enum RTUSB_POOL_STATE{
	MEM_POOL_INVALID = 0,
	MEM_POOL_INITING = 1,
	MEM_POOL_INITED = 2,
	MEM_POOL_STOPING = 3,
	MEM_POOL_MAX,
};


enum RTUSB_POOL_STAT_OP{
	POOL_STAT_CHK = 1,
	POOL_STAT_CHG = 2
};


static DEFINE_SPINLOCK(rtusb_mem_lock);
static enum RTUSB_POOL_STATE mem_pool_stat = MEM_POOL_INVALID;
static struct rtusb_mem_pool *rtusb_buf_pool = NULL;


void dump_mem_pool(void)
{
	struct rtusb_mem_pool *pool;
	struct rtusb_bulk_mem *mem;
	unsigned long irqflags;
	int idx;

	spin_lock_irqsave(&rtusb_mem_lock, irqflags);
	if ((rtusb_buf_pool == NULL) || (mem_pool_stat == MEM_POOL_INVALID)) {
		printk("%s(): Invalid pool(0x%p) status(%d)\n", 
				__FUNCTION__, rtusb_buf_pool, mem_pool_stat);
	} else {

		printk("Dump Pre-allocated mem pool(Hdr:0x%p, flag:%d):\n", 
				rtusb_buf_pool, mem_pool_stat);
		
		pool = rtusb_buf_pool;
		while(pool) {
			printk("\tbus(%s): controller=0x%p, pool=0x%p, pool_cnt=%d\n", pool->bus->bus_name, pool->dev, pool, pool->pool_cnt);
			for (idx = 0; idx < pool->pool_cnt; idx++) {
				mem = (struct rtusb_bulk_mem *)(pool->buf_pool + idx);
				printk("\t\t%d(0x%p):Flag=%d, buf=0x%p, dma=0x%x, len=%d\n", 
						idx, mem, mem->assigned, mem->buf, mem->data_dma, mem->len);
			}
			pool = pool->next;
		}
	}
	spin_unlock_irqrestore(&rtusb_mem_lock, irqflags);
	
}


static int pool_stat_change(enum RTUSB_POOL_STATE old, enum RTUSB_POOL_STATE new)
{
	unsigned long irqflags;
	int status = 0;

	spin_lock_irqsave(&rtusb_mem_lock, irqflags);
	if ((old != MEM_POOL_MAX) && (mem_pool_stat != old)) {
		printk("%s(): invalid mem pool status(exp:%d, act:%d)\n", 
				__FUNCTION__, old, mem_pool_stat);
		status = -1;
	}
	else
		mem_pool_stat = new;
	spin_unlock_irqrestore(&rtusb_mem_lock, irqflags);
	
	return status;
}


int rtusb_resource_recycle(struct usb_device *udev, void *buf, dma_addr_t dma)
{
	struct rtusb_mem_pool *pool;
	struct usb_bus *dev_bus = udev->bus;
	struct rtusb_bulk_mem *mem;
	unsigned long irqflags;
	int pool_idx;

	printk("-->%s()\n", __FUNCTION__);
	if (!dev_bus) {
		printk("Error:Invalid dev_bus!\n");
		return -1;
	}

	printk("Recycle mem(0x%x, 0x%x) for usb_dev(%s) which attached to bus(0x%p, %s), controller=0x%p\n", 
			buf, dma, udev->product, dev_bus, dev_bus->bus_name, dev_bus->controller);
	
	spin_lock_irqsave(&rtusb_mem_lock, irqflags);
	if ((rtusb_buf_pool == NULL) || (mem_pool_stat != MEM_POOL_INITED)) {
		printk("%s(): Invalid pool(0x%p) status(%d)\n", 
				__FUNCTION__, rtusb_buf_pool, mem_pool_stat);
		spin_unlock_irqrestore(&rtusb_mem_lock, irqflags);
		return -1;
	}

	pool = rtusb_buf_pool;
	while(pool != NULL)
	{
		if (dev_bus->controller == pool->dev)
		{
			printk("%s():Find attached controller(0x%p, %s)!\n", 
					__FUNCTION__, pool->dev, (pool->bus ? pool->bus->bus_name : "Invalid"));
			for (pool_idx = 0; pool_idx < pool->pool_cnt; pool_idx++)
			{
				mem = (struct rtusb_bulk_mem *)(pool->buf_pool + pool_idx);
				if (mem->assigned && (mem->buf == buf) && (mem->data_dma == dma))
				{
					mem->assigned = 0;
					printk("\tRecycle done\n");
					break;
				}
			}
			break;
		}
		pool = pool->next;
	}
	spin_unlock_irqrestore(&rtusb_mem_lock, irqflags);

	if (pool == NULL) {
		printk("%s():Cannot found buf(0x%p, 0x%x) assigned to usb_dev(%s) in mem pool\n", 
				__FUNCTION__, buf, dma, udev->product);
		dump_mem_pool();
	}

	return 0;
	
}

void  *rtusb_resource_alloc(struct usb_device *udev, int len, dma_addr_t *dma)
{
	struct usb_bus *dev_bus = udev->bus;
	struct rtusb_mem_pool *pool;
	struct rtusb_bulk_mem *mem;
	unsigned long irqflags;
	int pool_idx;

	printk("--->%s():\n", __FUNCTION__);
	if (!dev_bus) {
		printk("Error, invalid bus!\n");
		return NULL;
	}
	
	printk("Request mem(len:%d) for usb_dev(%s) which attached to bus(0x%p, %s), controller=0x%p\n", 
			len, udev->product, dev_bus, dev_bus->bus_name, dev_bus->controller);
	
	spin_lock_irqsave(&rtusb_mem_lock, irqflags);
	if ((rtusb_buf_pool == NULL) || (mem_pool_stat != MEM_POOL_INITED)) {
		printk("%s(): Invalid pool(0x%p) status(%d)\n", 
				__FUNCTION__, rtusb_buf_pool, mem_pool_stat);
		spin_unlock_irqrestore(&rtusb_mem_lock, irqflags);
		return NULL;
	}

	pool = rtusb_buf_pool;
	while(pool != NULL)
	{
		if (dev_bus->controller == pool->dev)
		{
			printk("%s():Find attached controller(0x%p) at pool(0x%p)!\n", 
					__FUNCTION__, dev_bus->controller, pool);
			if (pool->bus != dev_bus) {
				printk("Adjust the pool->bus as current one!\n");
				pool->bus = dev_bus; /* write it back in case the bus is changed */
			}

			for (pool_idx = 0; pool_idx < pool->pool_cnt; pool_idx++)
			{
				mem = (struct rtusb_bulk_mem *)(pool->buf_pool + pool_idx);
				if ((mem->assigned == 0) && (mem->len == len))
				{
					mem->assigned = 1;
					*dma = mem->data_dma;
					memset(mem->buf, 0, mem->len);
					spin_unlock_irqrestore(&rtusb_mem_lock, irqflags);
					
					printk("%s():Assign the buf(0x%p, 0x%x, len=%d) to usb_dev(%s)\n", 
							__FUNCTION__, mem->buf, mem->data_dma, mem->len, udev->product);

					dump_mem_pool();
					return mem->buf;
				}
			}
		}
		pool = pool->next;
	}
	spin_unlock_irqrestore(&rtusb_mem_lock, irqflags);

	printk("%s():Cannot found buf assign to usb_dev(%s)!\n", __FUNCTION__, udev->product);
	dump_mem_pool();

	return NULL;
	
}


int rtusb_resource_exit(void)
{
	struct usb_bus *bus;
	struct rtusb_mem_pool *pool;
	struct rtusb_bulk_mem *mem;
	unsigned long irqflags;
	int status, idx;

	printk("--->%s()\n", __FUNCTION__);
	dump_mem_pool();
	
	status = pool_stat_change(MEM_POOL_INITED, MEM_POOL_STOPING);
	if (status != 0)
		return -1;

	//spin_lock_irqsave(&rtusb_mem_lock, irqflags);
	while(rtusb_buf_pool != NULL) {
		pool = rtusb_buf_pool;
		printk("%s():Free Pre-allocated mem for bus(%s)!\n", __FUNCTION__, pool->bus->bus_name);
		for (idx = 0; idx < pool->pool_cnt; idx++) {
			mem = (struct rtusb_bulk_mem *)(pool->buf_pool + idx);
			bus = pool->bus;
			if (mem->assigned == 1)
				printk("Warning, mem still occupied by someone?\n");
			if (mem->buf) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
				usb_free_coherent(bus->root_hub, mem->len, mem->buf, mem->data_dma);
#else
				usb_buffer_free(bus->root_hub, mem->len, mem->buf, mem->data_dma);
#endif
#else
				kfree(mem->buf);
#endif
				printk("%s():%d:Free the buf(0x%p) with len(%d)\n", 
						__FUNCTION__, idx, mem, mem->len);
			}
		}
		rtusb_buf_pool = rtusb_buf_pool->next;
		kfree(pool);
	}
	//spin_unlock_irqrestore(&rtusb_mem_lock, irqflags);

	printk("%s():After free pools, dump it\n", __FUNCTION__);
	dump_mem_pool();
	status = pool_stat_change(MEM_POOL_STOPING, MEM_POOL_INVALID);
	
	return status;
}


int rtusb_resource_init(int txlen, int rxlen, int tx_cnt, int rx_cnt)
{
	struct usb_bus *bus;
	struct rtusb_mem_pool *pool;
	struct rtusb_bulk_mem *mem;
	int status, idx, buf_len, pool_cnt, bus_cnt;

	pool_cnt = tx_cnt + rx_cnt;
	printk("%s()--->txlen=%d,rxlen=%d, pool_cnt=%d(t:%d,r:%d)!\n", 
		__FUNCTION__, txlen, rxlen, pool_cnt, tx_cnt,rx_cnt);
	
	status = pool_stat_change(MEM_POOL_INVALID, MEM_POOL_INITING);
	if ((status!=0) || (txlen == 0) || (rxlen == 0) || (tx_cnt == 0) || (rx_cnt == 0))
		return -1;

	/* 
		for each bus, we need to allocate resource for it, because we cannot 
		expect which bus will be used for our dongle.
	*/
	bus_cnt = 0;
	mutex_lock(&usb_bus_list_lock);
	list_for_each_entry(bus, &usb_bus_list, bus_list) {
		if (bus->root_hub) {
			/*  Currently we only alloc memory for high speed bus */
			if (bus->root_hub->speed != USB_SPEED_HIGH)
				continue;
			buf_len = sizeof(struct rtusb_mem_pool) + sizeof(struct rtusb_mem_pool) * pool_cnt;
			pool = kmalloc(buf_len, GFP_ATOMIC);
			if (!pool) {
				printk("%s():Allocate pool structure for bus(%s) failed\n", 
						__FUNCTION__, bus->bus_name);
				continue;
			}

			memset(pool, 0, buf_len);
			pool->pool_cnt = pool_cnt;
			pool->bus = bus;
			pool->dev = bus->controller;
			pool->buf_pool = (struct rtusb_bulk_mem *)(pool + 1);
			for (idx = 0; idx < pool_cnt; idx++) {
				mem = (struct rtusb_bulk_mem *)(pool->buf_pool + idx);
				buf_len = (idx >= tx_cnt) ? rxlen : txlen;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
				mem->buf = usb_alloc_coherent(bus->root_hub, buf_len, GFP_ATOMIC, &mem->data_dma);
#else
				mem->buf = usb_buffer_alloc(bus->root_hub, buf_len, GFP_ATOMIC, &mem->data_dma);
#endif
#else
				mem->buf = kmalloc(buf_len, GFP_ATOMIC);
#endif
				if (mem->buf)
					mem->len = buf_len;
				else
					printk("%s():Alloc membuf(idx:%d) for bus(%s) failed!\n", 
							__FUNCTION__, idx, bus->bus_name);
			}

			if (rtusb_buf_pool)
				pool->next = rtusb_buf_pool;
			rtusb_buf_pool = pool;
			
			bus_cnt++;
		}
	}
	mutex_unlock(&usb_bus_list_lock);

	status = pool_stat_change(MEM_POOL_INITING, MEM_POOL_INITED);
	dump_mem_pool();
	
	printk("<---%s(%d)\n", __FUNCTION__, status);
	
	return status;
}

#ifdef OS_ABL_SUPPORT
EXPORT_SYMBOL(rtusb_resource_exit);
EXPORT_SYMBOL(rtusb_resource_init);
#endif /* OS_ABL_SUPPORT */
#endif /* RESOURCE_BOOT_ALLOC */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
/*
========================================================================
Routine Description:
	Dump URB information.

Arguments:
	purb_org		- the URB

Return Value:
	None

Note:
========================================================================
*/
void dump_urb(VOID *purb_org)
{
	struct urb *purb = (struct urb *)purb_org;

	printk("urb                  :0x%08lx\n", (unsigned long)purb);
	printk("\tdev                   :0x%08lx\n", (unsigned long)purb->dev);
	printk("\t\tdev->state          :0x%d\n", purb->dev->state);
	printk("\tpipe                  :0x%08x\n", purb->pipe);
	printk("\tstatus                :%d\n", purb->status);
	printk("\ttransfer_flags        :0x%08x\n", purb->transfer_flags);
	printk("\ttransfer_buffer       :0x%08lx\n", (unsigned long)purb->transfer_buffer);
	printk("\ttransfer_buffer_length:%d\n", purb->transfer_buffer_length);
	printk("\tactual_length         :%d\n", purb->actual_length);
	printk("\tsetup_packet          :0x%08lx\n", (unsigned long)purb->setup_packet);
	printk("\tstart_frame           :%d\n", purb->start_frame);
	printk("\tnumber_of_packets     :%d\n", purb->number_of_packets);
	printk("\tinterval              :%d\n", purb->interval);
	printk("\terror_count           :%d\n", purb->error_count);
	printk("\tcontext               :0x%08lx\n", (unsigned long)purb->context);
	printk("\tcomplete              :0x%08lx\n\n", (unsigned long)purb->complete);
}
#else
void dump_urb(VOID *purb_org)
{
	return;
}
#endif /* LINUX_VERSION_CODE */



#ifdef CONFIG_STA_SUPPORT
#ifdef CONFIG_PM
#ifdef USB_SUPPORT_SELECTIVE_SUSPEND


void rausb_autopm_put_interface( void *intf)
{
	usb_autopm_put_interface((struct usb_interface *)intf);
}

int  rausb_autopm_get_interface( void *intf)
{
	return usb_autopm_get_interface((struct usb_interface *)intf);
}



/*
========================================================================
Routine Description:
	RTMP_Usb_AutoPM_Put_Interface

Arguments:
	

Return Value:
	

Note:
========================================================================
*/

int RTMP_Usb_AutoPM_Put_Interface (
	IN	VOID			*pUsb_Devsrc,
	IN	VOID			*intfsrc)
{

	INT	 pm_usage_cnt;

struct usb_device		*pUsb_Dev =(struct usb_device *)pUsb_Devsrc;	
struct usb_interface	*intf =(struct usb_interface *)intfsrc;


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
		pm_usage_cnt = atomic_read(&intf->pm_usage_cnt);	
#else
		pm_usage_cnt = intf->pm_usage_cnt;
#endif

		if(pm_usage_cnt == 1)
		{
#if 0
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
			if(pUsb_Dev->autosuspend_disabled  ==0)
#else
			if(pUsb_Dev->auto_pm ==1)
#endif
#endif
			{
					rausb_autopm_put_interface(intf);
			}
	
#if 0
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
			else
			{
				DBGPRINT(RT_DEBUG_TRACE, ("STAMlmePeriodicExec: AsicRadioOff  fRTMP_ADAPTER_SUSPEND\n"));
/*				RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_SUSPEND); */
/*				RTMP_DRIVER_ADAPTER_SUSPEND_SET(pAd); */
				return (-1);
			}
#endif
#endif
                }

			return 0;
}

EXPORT_SYMBOL(RTMP_Usb_AutoPM_Put_Interface);

/*
========================================================================
Routine Description:
	RTMP_Usb_AutoPM_Get_Interface

Arguments:
	

Return Value: (-1)  error (resume fail )    1 success ( resume success)  2  (do  nothing)
	

Note:
========================================================================
*/

int RTMP_Usb_AutoPM_Get_Interface (
	IN	VOID			*pUsb_Devsrc,
	IN	VOID			*intfsrc)
{

	INT	 pm_usage_cnt;
	struct usb_device		*pUsb_Dev =(struct usb_device *)pUsb_Devsrc;	
	struct usb_interface	*intf =(struct usb_interface *)intfsrc;


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
	pm_usage_cnt = (INT)atomic_read(&intf->pm_usage_cnt);	
#else
	pm_usage_cnt = intf->pm_usage_cnt;
#endif

/*	if(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_CPU_SUSPEND)) */
	{
		if(pm_usage_cnt == 0)
		{
			int res=1;
#if 0
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,33)
		if(pUsb_Dev->autosuspend_disabled  ==0)
#else
		if(pUsb_Dev->auto_pm ==1)
#endif
#endif
			{
				res = rausb_autopm_get_interface(intf);

/*
when system  power level from auto to on, auto_pm is 0 and the function radioon will set fRTMP_ADAPTER_SUSPEND
so we must clear fkag here;

*/				
/*				RTMP_CLEAR_FLAG(pAd, fRTMP_ADAPTER_SUSPEND); */

				if (res)
				{
/*					DBGPRINT(RT_DEBUG_ERROR, ("AsicSwitchChannel autopm_resume fail ------\n")); */
/*					RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_SUSPEND); */
					return (-1);
				}			
			}
#if 0
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
			else
			{
/*				DBGPRINT(RT_DEBUG_TRACE, ("AsicSwitchChannel: fRTMP_ADAPTER_SUSPEND\n")); */
/*				RTMP_SET_FLAG(pAd, fRTMP_ADAPTER_SUSPEND); */
				return (-1);
			}
#endif
#endif
			return 1;
		}
			return 2;
}
/*
	else
	{
				DBGPRINT(RT_DEBUG_TRACE, ("AsicSwitchChannel: fRTMP_ADAPTER_CPU_SUSPEND\n"));
				return;
	}
*/
}

EXPORT_SYMBOL(RTMP_Usb_AutoPM_Get_Interface);

#endif /* USB_SUPPORT_SELECTIVE_SUSPEND */
#endif /* CONFIG_PM */
#endif /* CONFIG_STA_SUPPORT */



#ifdef OS_ABL_SUPPORT
/*
========================================================================
Routine Description:
	Register a USB driver.

Arguments:
	new_driver		- the driver

Return Value:
	0				- successfully
	Otherwise		- fail

Note:
========================================================================
*/
int rausb_register(VOID * new_driver)
{
	return usb_register((struct usb_driver *)new_driver);
}
EXPORT_SYMBOL(rausb_register);


/*
========================================================================
Routine Description:
	De-Register a USB driver.

Arguments:
	new_driver		- the driver

Return Value:
	None

Note:
========================================================================
*/
void rausb_deregister(VOID * driver)
{
	usb_deregister((struct usb_driver *)driver);
}
EXPORT_SYMBOL(rausb_deregister);


/*
========================================================================
Routine Description:
	Create a new urb for a USB driver to use.

Arguments:
	iso_packets		- number of iso packets for this urb

Return Value:
	the URB

Note:
========================================================================
*/
struct urb *rausb_alloc_urb(int iso_packets)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	return usb_alloc_urb(iso_packets, GFP_ATOMIC);
#else
	return usb_alloc_urb(iso_packets);
#endif /* LINUX_VERSION_CODE */
}
EXPORT_SYMBOL(rausb_alloc_urb);


/*
========================================================================
Routine Description:
	Free the memory used by a urb.

Arguments:
	urb				- the URB

Return Value:
	None

Note:
========================================================================
*/
void rausb_free_urb(VOID *urb)
{
	usb_free_urb((struct urb *)urb);
}
EXPORT_SYMBOL(rausb_free_urb);


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
/*
========================================================================
Routine Description:
	Release a use of the usb device structure.

Arguments:
	dev				- the USB device

Return Value:
	None

Note:
========================================================================
*/
void rausb_put_dev(VOID *dev)
{
	usb_put_dev((struct usb_device *)dev);
}
EXPORT_SYMBOL(rausb_put_dev);


/*
========================================================================
Routine Description:
	Increments the reference count of the usb device structure.

Arguments:
	dev				- the USB device

Return Value:
	the device with the incremented reference counter

Note:
========================================================================
*/
struct usb_device *rausb_get_dev(VOID *dev)
{
	return usb_get_dev((struct usb_device *)dev);
}
EXPORT_SYMBOL(rausb_get_dev);
#endif /* LINUX_VERSION_CODE */


/*
========================================================================
Routine Description:
	Issue an asynchronous transfer request for an endpoint.

Arguments:
	urb				- the URB

Return Value:
	0				- successfully
	Otherwise		- fail

Note:
========================================================================
*/
int rausb_submit_urb(VOID *urb)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	return usb_submit_urb((struct urb *)urb, GFP_ATOMIC);
#else
	return usb_submit_urb((struct urb *)urb);
#endif /* LINUX_VERSION_CODE */
}
EXPORT_SYMBOL(rausb_submit_urb);

/*
========================================================================
Routine Description:
	Allocate dma-consistent buffer.

Arguments:
	dev				- the USB device
	size			- buffer size
	dma				- used to return DMA address of buffer

Return Value:
	a buffer that may be used to perform DMA to the specified device

Note:
========================================================================
*/
void *rausb_buffer_alloc(VOID *dev,
							size_t size,
							ra_dma_addr_t *dma)
{
#ifdef RESOURCE_BOOT_ALLOC
	void *buf;
	if (size > 4095) {
		buf = rtusb_resource_alloc(dev, size, dma);
		printk("%s():alloc usb buffer(p:0x%p, dma:0x%x, len:%d) %s!\n", 
					__FUNCTION__, buf, *dma, size, (buf ? "done" : "fail"));
		return buf;
	}
#endif /* RESOURCE_BOOT_ALLOC */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
	dma_addr_t DmaAddr = (dma_addr_t)(*dma);
	void *buf;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
	buf = usb_alloc_coherent(dev, size, GFP_ATOMIC, &DmaAddr);
#else
	buf = usb_buffer_alloc(dev, size, GFP_ATOMIC, &DmaAddr);
#endif
	*dma = (ra_dma_addr_t)DmaAddr;
	return buf;

#else
	return kmalloc(size, GFP_ATOMIC);
#endif
}
EXPORT_SYMBOL(rausb_buffer_alloc);


/*
========================================================================
Routine Description:
	Free memory allocated with usb_buffer_alloc.

Arguments:
	dev				- the USB device
	size			- buffer size
	addr			- CPU address of buffer
	dma				- used to return DMA address of buffer

Return Value:
	None

Note:
========================================================================
*/
void rausb_buffer_free(VOID *dev,
							size_t size,
							void *addr,
							ra_dma_addr_t dma)
{
#ifdef RESOURCE_BOOT_ALLOC
	if (size > 4095)
		rtusb_resource_recycle(dev, addr, dma);
	else
#endif /* RESOURCE_BOOT_ALLOC */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,5,0)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35)
	usb_free_coherent(dev, size, addr, dma);
#else
	usb_buffer_free(dev, size, addr, dma);
#endif
#else
	kfree(addr);
#endif
}
EXPORT_SYMBOL(rausb_buffer_free);

/*
========================================================================
Routine Description:
	Send a control message to a device.

Arguments:
	dev				- the USB device

Return Value:
	0				- successfully
	Otherwise		- fail

Note:
========================================================================
*/
int rausb_control_msg(VOID *dev,
						unsigned int pipe,
						__u8 request,
						__u8 requesttype,
						__u16 value,
						__u16 index,
						void *data,
						__u16 size,
						int timeout)
{
	int ret;

	ret = usb_control_msg((struct usb_device *)dev, pipe, request, requesttype, value, index,
							data, size, timeout);
	if (ret == -ENODEV)
		return RTMP_USB_CONTROL_MSG_ENODEV;
	if (ret < 0)
		return RTMP_USB_CONTROL_MSG_FAIL;
	return ret;
}
EXPORT_SYMBOL(rausb_control_msg);

unsigned int rausb_sndctrlpipe(VOID *dev, ULONG address)
{
	return usb_sndctrlpipe(dev, address);
}
EXPORT_SYMBOL(rausb_sndctrlpipe);

unsigned int rausb_rcvctrlpipe(VOID *dev, ULONG address)
{
	return usb_rcvctrlpipe(dev, address);
}
EXPORT_SYMBOL(rausb_rcvctrlpipe);


/*
========================================================================
Routine Description:
	Cancel a transfer request and wait for it to finish.

Arguments:
	urb				- the URB

Return Value:
	None

Note:
========================================================================
*/
void rausb_kill_urb(VOID *urb)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,7)
	usb_kill_urb((struct urb *)urb);
#else
	usb_unlink_urb((struct urb *)urb);
#endif /* LINUX_VERSION_CODE */
}
EXPORT_SYMBOL(rausb_kill_urb);

#endif /* OS_ABL_SUPPORT */


VOID RtmpOsUsbEmptyUrbCheck(
	IN	VOID				**ppWait,
	IN	NDIS_SPIN_LOCK		*pBulkInLock,
	IN	UCHAR				PendingRx)
{
	UINT32 i = 0;
	DECLARE_WAIT_QUEUE_HEAD(unlink_wakeup); 
	DECLARE_WAITQUEUE(wait, current);


	/* ensure there are no more active urbs. */
	add_wait_queue (&unlink_wakeup, &wait);
	*ppWait = &unlink_wakeup;

	/* maybe wait for deletions to finish. */
	i = 0;
	/*while((i < 25) && atomic_read(&pAd->PendingRx) > 0) */
	while(i < 25)
	{
/*		unsigned long IrqFlags; */

		RTMP_SEM_LOCK(pBulkInLock);
		if (PendingRx == 0)
		{
			RTMP_SEM_UNLOCK(pBulkInLock);
			break;
		}
		RTMP_SEM_UNLOCK(pBulkInLock);
		
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,9)
		msleep(UNLINK_TIMEOUT_MS);	/*Time in millisecond */
#else
		RTMPusecDelay(UNLINK_TIMEOUT_MS*1000);	/*Time in microsecond */
#endif
		i++;
	}
	*ppWait = NULL;
	remove_wait_queue (&unlink_wakeup, &wait); 
}


VOID	RtmpOsUsbInitHTTxDesc(
	IN	VOID			*pUrbSrc,
	IN	VOID			*pUsb_Dev,
	IN	UINT			BulkOutEpAddr,
	IN	PUCHAR			pSrc,
	IN	ULONG			BulkOutSize,
	IN	USB_COMPLETE_HANDLER	Func,
	IN	VOID			*pTxContext,
	IN	ra_dma_addr_t		TransferDma)
{
	PURB pUrb = (PURB)pUrbSrc;


	ASSERT(pUrb);

	/*Initialize a tx bulk urb */
	RTUSB_FILL_HTTX_BULK_URB(pUrb,
						pUsb_Dev,
						BulkOutEpAddr,
						pSrc,
						BulkOutSize,
						(usb_complete_t)Func,
						pTxContext,
						TransferDma);
}


VOID	RtmpOsUsbInitRxDesc(
	IN	VOID			*pUrbSrc,
	IN	VOID			*pUsb_Dev,
	IN	UINT			BulkInEpAddr,
	IN	UCHAR			*pTransferBuffer,
	IN	UINT32			BufSize,
	IN	USB_COMPLETE_HANDLER	Func,
	IN	VOID			*pRxContext,
	IN	ra_dma_addr_t		TransferDma)
{
	PURB pUrb = (PURB)pUrbSrc;


	ASSERT(pUrb);

	/*Initialize a rx bulk urb */
	RTUSB_FILL_RX_BULK_URB(pUrb,
						pUsb_Dev,
						BulkInEpAddr,
						pTransferBuffer,
						BufSize,
						(usb_complete_t)Func,
						pRxContext,
						TransferDma);
}


VOID *RtmpOsUsbContextGet(
	IN	VOID			*pUrb)
{
	return ((purbb_t)pUrb)->rtusb_urb_context;
}


NTSTATUS RtmpOsUsbStatusGet(
	IN	VOID			*pUrb)
{
	return ((purbb_t)pUrb)->rtusb_urb_status;
}


VOID RtmpOsUsbDmaMapping(
	IN	VOID			*pUrb)
{
	RTUSB_URB_DMA_MAPPING(((purbb_t)pUrb));
}


/*
========================================================================
Routine Description:
	Get the data pointer from the URB.

Arguments:
	pUrb			- USB URB

Return Value:
	the data pointer

Note:
========================================================================
*/
VOID *RtmpOsUsbUrbDataGet(
	IN	VOID					*pUrb)
{
	return RTMP_USB_URB_DATA_GET(pUrb);
}


/*
========================================================================
Routine Description:
	Get the status from the URB.

Arguments:
	pUrb			- USB URB

Return Value:
	the status

Note:
========================================================================
*/
NTSTATUS RtmpOsUsbUrbStatusGet(
	IN	VOID					*pUrb)
{
	return RTMP_USB_URB_STATUS_GET(pUrb);
}


/*
========================================================================
Routine Description:
	Get the data length from the URB.

Arguments:
	pUrb			- USB URB

Return Value:
	the data length

Note:
========================================================================
*/
ULONG RtmpOsUsbUrbLenGet(
	IN	VOID					*pUrb)
{
	return RTMP_USB_URB_LEN_GET(pUrb);
}

/*
========================================================================
Routine Description:
	Get USB Vendor ID.

Arguments:
	pUsbDev			- the usb device

Return Value:
	the name

Note:
========================================================================
*/
UINT32 RtmpOsGetUsbDevVendorID(IN VOID *pUsbDev) {
	return ((struct usb_device *) pUsbDev)->descriptor.idVendor;
}

/*
========================================================================
Routine Description:
	Get USB Product ID.

Arguments:
	pUsbDev			- the usb device

Return Value:
	the name

Note:
========================================================================
*/
UINT32 RtmpOsGetUsbDevProductID(IN VOID *pUsbDev) {
	return ((struct usb_device *) pUsbDev)->descriptor.idProduct;
}

#endif /* RTMP_MAC_USB */

/* End of rt_usb_util.c */
