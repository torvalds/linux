/*
 * Core maple bus functionality
 *
 *  Copyright (C) 2007 - 2009 Adrian McMenamin
 *  Copyright (C) 2001 - 2008 Paul Mundt
 *  Copyright (C) 2000 - 2001 YAEGASHI Takeshi
 *  Copyright (C) 2001 M. R. Brown
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/maple.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <asm/cacheflush.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <mach/dma.h>
#include <mach/sysasic.h>

MODULE_AUTHOR("Adrian McMenamin <adrian@mcmen.demon.co.uk>");
MODULE_DESCRIPTION("Maple bus driver for Dreamcast");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{{SEGA, Dreamcast/Maple}}");

static void maple_dma_handler(struct work_struct *work);
static void maple_vblank_handler(struct work_struct *work);

static DECLARE_WORK(maple_dma_process, maple_dma_handler);
static DECLARE_WORK(maple_vblank_process, maple_vblank_handler);

static LIST_HEAD(maple_waitq);
static LIST_HEAD(maple_sentq);

/* mutex to protect queue of waiting packets */
static DEFINE_MUTEX(maple_wlist_lock);

static struct maple_driver maple_unsupported_device;
static struct device maple_bus;
static int subdevice_map[MAPLE_PORTS];
static unsigned long *maple_sendbuf, *maple_sendptr, *maple_lastptr;
static unsigned long maple_pnp_time;
static int started, scanning, fullscan;
static struct kmem_cache *maple_queue_cache;

struct maple_device_specify {
	int port;
	int unit;
};

static bool checked[MAPLE_PORTS];
static bool empty[MAPLE_PORTS];
static struct maple_device *baseunits[MAPLE_PORTS];

/**
 * maple_driver_register - register a maple driver
 * @drv: maple driver to be registered.
 *
 * Registers the passed in @drv, while updating the bus type.
 * Devices with matching function IDs will be automatically probed.
 */
int maple_driver_register(struct maple_driver *drv)
{
	if (!drv)
		return -EINVAL;

	drv->drv.bus = &maple_bus_type;

	return driver_register(&drv->drv);
}
EXPORT_SYMBOL_GPL(maple_driver_register);

/**
 * maple_driver_unregister - unregister a maple driver.
 * @drv: maple driver to unregister.
 *
 * Cleans up after maple_driver_register(). To be invoked in the exit
 * path of any module drivers.
 */
void maple_driver_unregister(struct maple_driver *drv)
{
	driver_unregister(&drv->drv);
}
EXPORT_SYMBOL_GPL(maple_driver_unregister);

/* set hardware registers to enable next round of dma */
static void maple_dma_reset(void)
{
	__raw_writel(MAPLE_MAGIC, MAPLE_RESET);
	/* set trig type to 0 for software trigger, 1 for hardware (VBLANK) */
	__raw_writel(1, MAPLE_TRIGTYPE);
	/*
	* Maple system register
	* bits 31 - 16	timeout in units of 20nsec
	* bit 12	hard trigger - set 0 to keep responding to VBLANK
	* bits 9 - 8	set 00 for 2 Mbps, 01 for 1 Mbps
	* bits 3 - 0	delay (in 1.3ms) between VBLANK and start of DMA
	* max delay is 11
	*/
	__raw_writel(MAPLE_2MBPS | MAPLE_TIMEOUT(0xFFFF), MAPLE_SPEED);
	__raw_writel(virt_to_phys(maple_sendbuf), MAPLE_DMAADDR);
	__raw_writel(1, MAPLE_ENABLE);
}

/**
 * maple_getcond_callback - setup handling MAPLE_COMMAND_GETCOND
 * @dev: device responding
 * @callback: handler callback
 * @interval: interval in jiffies between callbacks
 * @function: the function code for the device
 */
void maple_getcond_callback(struct maple_device *dev,
			void (*callback) (struct mapleq *mq),
			unsigned long interval, unsigned long function)
{
	dev->callback = callback;
	dev->interval = interval;
	dev->function = cpu_to_be32(function);
	dev->when = jiffies;
}
EXPORT_SYMBOL_GPL(maple_getcond_callback);

static int maple_dma_done(void)
{
	return (__raw_readl(MAPLE_STATE) & 1) == 0;
}

static void maple_release_device(struct device *dev)
{
	struct maple_device *mdev;
	struct mapleq *mq;

	mdev = to_maple_dev(dev);
	mq = mdev->mq;
	kmem_cache_free(maple_queue_cache, mq->recvbuf);
	kfree(mq);
	kfree(mdev);
}

/**
 * maple_add_packet - add a single instruction to the maple bus queue
 * @mdev: maple device
 * @function: function on device being queried
 * @command: maple command to add
 * @length: length of command string (in 32 bit words)
 * @data: remainder of command string
 */
int maple_add_packet(struct maple_device *mdev, u32 function, u32 command,
	size_t length, void *data)
{
	int ret = 0;
	void *sendbuf = NULL;

	if (length) {
		sendbuf = kcalloc(length, 4, GFP_KERNEL);
		if (!sendbuf) {
			ret = -ENOMEM;
			goto out;
		}
		((__be32 *)sendbuf)[0] = cpu_to_be32(function);
	}

	mdev->mq->command = command;
	mdev->mq->length = length;
	if (length > 1)
		memcpy(sendbuf + 4, data, (length - 1) * 4);
	mdev->mq->sendbuf = sendbuf;

	mutex_lock(&maple_wlist_lock);
	list_add_tail(&mdev->mq->list, &maple_waitq);
	mutex_unlock(&maple_wlist_lock);
out:
	return ret;
}
EXPORT_SYMBOL_GPL(maple_add_packet);

static struct mapleq *maple_allocq(struct maple_device *mdev)
{
	struct mapleq *mq;

	mq = kzalloc(sizeof(*mq), GFP_KERNEL);
	if (!mq)
		goto failed_nomem;

	INIT_LIST_HEAD(&mq->list);
	mq->dev = mdev;
	mq->recvbuf = kmem_cache_zalloc(maple_queue_cache, GFP_KERNEL);
	if (!mq->recvbuf)
		goto failed_p2;
	mq->recvbuf->buf = &((mq->recvbuf->bufx)[0]);

	return mq;

failed_p2:
	kfree(mq);
failed_nomem:
	dev_err(&mdev->dev, "could not allocate memory for device (%d, %d)\n",
		mdev->port, mdev->unit);
	return NULL;
}

static struct maple_device *maple_alloc_dev(int port, int unit)
{
	struct maple_device *mdev;

	/* zero this out to avoid kobj subsystem
	* thinking it has already been registered */

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return NULL;

	mdev->port = port;
	mdev->unit = unit;

	mdev->mq = maple_allocq(mdev);

	if (!mdev->mq) {
		kfree(mdev);
		return NULL;
	}
	mdev->dev.bus = &maple_bus_type;
	mdev->dev.parent = &maple_bus;
	init_waitqueue_head(&mdev->maple_wait);
	return mdev;
}

static void maple_free_dev(struct maple_device *mdev)
{
	kmem_cache_free(maple_queue_cache, mdev->mq->recvbuf);
	kfree(mdev->mq);
	kfree(mdev);
}

/* process the command queue into a maple command block
 * terminating command has bit 32 of first long set to 0
 */
static void maple_build_block(struct mapleq *mq)
{
	int port, unit, from, to, len;
	unsigned long *lsendbuf = mq->sendbuf;

	port = mq->dev->port & 3;
	unit = mq->dev->unit;
	len = mq->length;
	from = port << 6;
	to = (port << 6) | (unit > 0 ? (1 << (unit - 1)) & 0x1f : 0x20);

	*maple_lastptr &= 0x7fffffff;
	maple_lastptr = maple_sendptr;

	*maple_sendptr++ = (port << 16) | len | 0x80000000;
	*maple_sendptr++ = virt_to_phys(mq->recvbuf->buf);
	*maple_sendptr++ =
	    mq->command | (to << 8) | (from << 16) | (len << 24);
	while (len-- > 0)
		*maple_sendptr++ = *lsendbuf++;
}

/* build up command queue */
static void maple_send(void)
{
	int i, maple_packets = 0;
	struct mapleq *mq, *nmq;

	if (!maple_dma_done())
		return;

	/* disable DMA */
	__raw_writel(0, MAPLE_ENABLE);

	if (!list_empty(&maple_sentq))
		goto finish;

	mutex_lock(&maple_wlist_lock);
	if (list_empty(&maple_waitq)) {
		mutex_unlock(&maple_wlist_lock);
		goto finish;
	}

	maple_lastptr = maple_sendbuf;
	maple_sendptr = maple_sendbuf;

	list_for_each_entry_safe(mq, nmq, &maple_waitq, list) {
		maple_build_block(mq);
		list_del_init(&mq->list);
		list_add_tail(&mq->list, &maple_sentq);
		if (maple_packets++ > MAPLE_MAXPACKETS)
			break;
	}
	mutex_unlock(&maple_wlist_lock);
	if (maple_packets > 0) {
		for (i = 0; i < (1 << MAPLE_DMA_PAGES); i++)
			__flush_purge_region(maple_sendbuf + i * PAGE_SIZE,
					PAGE_SIZE);
	}

finish:
	maple_dma_reset();
}

/* check if there is a driver registered likely to match this device */
static int maple_check_matching_driver(struct device_driver *driver,
					void *devptr)
{
	struct maple_driver *maple_drv;
	struct maple_device *mdev;

	mdev = devptr;
	maple_drv = to_maple_driver(driver);
	if (mdev->devinfo.function & cpu_to_be32(maple_drv->function))
		return 1;
	return 0;
}

static void maple_detach_driver(struct maple_device *mdev)
{
	device_unregister(&mdev->dev);
}

/* process initial MAPLE_COMMAND_DEVINFO for each device or port */
static void maple_attach_driver(struct maple_device *mdev)
{
	char *p, *recvbuf;
	unsigned long function;
	int matched, error;

	recvbuf = mdev->mq->recvbuf->buf;
	/* copy the data as individual elements in
	* case of memory optimisation */
	memcpy(&mdev->devinfo.function, recvbuf + 4, 4);
	memcpy(&mdev->devinfo.function_data[0], recvbuf + 8, 12);
	memcpy(&mdev->devinfo.area_code, recvbuf + 20, 1);
	memcpy(&mdev->devinfo.connector_direction, recvbuf + 21, 1);
	memcpy(&mdev->devinfo.product_name[0], recvbuf + 22, 30);
	memcpy(&mdev->devinfo.standby_power, recvbuf + 112, 2);
	memcpy(&mdev->devinfo.max_power, recvbuf + 114, 2);
	memcpy(mdev->product_name, mdev->devinfo.product_name, 30);
	mdev->product_name[30] = '\0';
	memcpy(mdev->product_licence, mdev->devinfo.product_licence, 60);
	mdev->product_licence[60] = '\0';

	for (p = mdev->product_name + 29; mdev->product_name <= p; p--)
		if (*p == ' ')
			*p = '\0';
		else
			break;
	for (p = mdev->product_licence + 59; mdev->product_licence <= p; p--)
		if (*p == ' ')
			*p = '\0';
		else
			break;

	function = be32_to_cpu(mdev->devinfo.function);

	dev_info(&mdev->dev, "detected %s: function 0x%lX: at (%d, %d)\n",
		mdev->product_name, function, mdev->port, mdev->unit);

	if (function > 0x200) {
		/* Do this silently - as not a real device */
		function = 0;
		mdev->driver = &maple_unsupported_device;
		dev_set_name(&mdev->dev, "%d:0.port", mdev->port);
	} else {
		matched =
			bus_for_each_drv(&maple_bus_type, NULL, mdev,
				maple_check_matching_driver);

		if (matched == 0) {
			/* Driver does not exist yet */
			dev_info(&mdev->dev, "no driver found\n");
			mdev->driver = &maple_unsupported_device;
		}
		dev_set_name(&mdev->dev, "%d:0%d.%lX", mdev->port,
			     mdev->unit, function);
	}

	mdev->function = function;
	mdev->dev.release = &maple_release_device;

	atomic_set(&mdev->busy, 0);
	error = device_register(&mdev->dev);
	if (error) {
		dev_warn(&mdev->dev, "could not register device at"
			" (%d, %d), with error 0x%X\n", mdev->unit,
			mdev->port, error);
		maple_free_dev(mdev);
		mdev = NULL;
		return;
	}
}

/*
 * if device has been registered for the given
 * port and unit then return 1 - allows identification
 * of which devices need to be attached or detached
 */
static int check_maple_device(struct device *device, void *portptr)
{
	struct maple_device_specify *ds;
	struct maple_device *mdev;

	ds = portptr;
	mdev = to_maple_dev(device);
	if (mdev->port == ds->port && mdev->unit == ds->unit)
		return 1;
	return 0;
}

static int setup_maple_commands(struct device *device, void *ignored)
{
	int add;
	struct maple_device *mdev = to_maple_dev(device);
	if (mdev->interval > 0 && atomic_read(&mdev->busy) == 0 &&
		time_after(jiffies, mdev->when)) {
		/* bounce if we cannot add */
		add = maple_add_packet(mdev,
			be32_to_cpu(mdev->devinfo.function),
			MAPLE_COMMAND_GETCOND, 1, NULL);
		if (!add)
			mdev->when = jiffies + mdev->interval;
	} else {
		if (time_after(jiffies, maple_pnp_time))
			/* Ensure we don't have block reads and devinfo
			* calls interfering with one another - so flag the
			* device as busy */
			if (atomic_read(&mdev->busy) == 0) {
				atomic_set(&mdev->busy, 1);
				maple_add_packet(mdev, 0,
					MAPLE_COMMAND_DEVINFO, 0, NULL);
			}
	}
	return 0;
}

/* VBLANK bottom half - implemented via workqueue */
static void maple_vblank_handler(struct work_struct *work)
{
	int x, locking;
	struct maple_device *mdev;

	if (!maple_dma_done())
		return;

	__raw_writel(0, MAPLE_ENABLE);

	if (!list_empty(&maple_sentq))
		goto finish;

	/*
	* Set up essential commands - to fetch data and
	* check devices are still present
	*/
	bus_for_each_dev(&maple_bus_type, NULL, NULL,
		setup_maple_commands);

	if (time_after(jiffies, maple_pnp_time)) {
		/*
		* Scan the empty ports - bus is flakey and may have
		* mis-reported emptyness
		*/
		for (x = 0; x < MAPLE_PORTS; x++) {
			if (checked[x] && empty[x]) {
				mdev = baseunits[x];
				if (!mdev)
					break;
				atomic_set(&mdev->busy, 1);
				locking = maple_add_packet(mdev, 0,
					MAPLE_COMMAND_DEVINFO, 0, NULL);
				if (!locking)
					break;
				}
			}

		maple_pnp_time = jiffies + MAPLE_PNP_INTERVAL;
	}

finish:
	maple_send();
}

/* handle devices added via hotplugs - placing them on queue for DEVINFO */
static void maple_map_subunits(struct maple_device *mdev, int submask)
{
	int retval, k, devcheck;
	struct maple_device *mdev_add;
	struct maple_device_specify ds;

	ds.port = mdev->port;
	for (k = 0; k < 5; k++) {
		ds.unit = k + 1;
		retval =
		    bus_for_each_dev(&maple_bus_type, NULL, &ds,
				     check_maple_device);
		if (retval) {
			submask = submask >> 1;
			continue;
		}
		devcheck = submask & 0x01;
		if (devcheck) {
			mdev_add = maple_alloc_dev(mdev->port, k + 1);
			if (!mdev_add)
				return;
			atomic_set(&mdev_add->busy, 1);
			maple_add_packet(mdev_add, 0, MAPLE_COMMAND_DEVINFO,
				0, NULL);
			/* mark that we are checking sub devices */
			scanning = 1;
		}
		submask = submask >> 1;
	}
}

/* mark a device as removed */
static void maple_clean_submap(struct maple_device *mdev)
{
	int killbit;

	killbit = (mdev->unit > 0 ? (1 << (mdev->unit - 1)) & 0x1f : 0x20);
	killbit = ~killbit;
	killbit &= 0xFF;
	subdevice_map[mdev->port] = subdevice_map[mdev->port] & killbit;
}

/* handle empty port or hotplug removal */
static void maple_response_none(struct maple_device *mdev)
{
	maple_clean_submap(mdev);

	if (likely(mdev->unit != 0)) {
		/*
		* Block devices play up
		* and give the impression they have
		* been removed even when still in place or
		* trip the mtd layer when they have
		* really gone - this code traps that eventuality
		* and ensures we aren't overloaded with useless
		* error messages
		*/
		if (mdev->can_unload) {
			if (!mdev->can_unload(mdev)) {
				atomic_set(&mdev->busy, 2);
				wake_up(&mdev->maple_wait);
				return;
			}
		}

		dev_info(&mdev->dev, "detaching device at (%d, %d)\n",
			mdev->port, mdev->unit);
		maple_detach_driver(mdev);
		return;
	} else {
		if (!started || !fullscan) {
			if (checked[mdev->port] == false) {
				checked[mdev->port] = true;
				empty[mdev->port] = true;
				dev_info(&mdev->dev, "no devices"
					" to port %d\n", mdev->port);
			}
			return;
		}
	}
	/* Some hardware devices generate false detach messages on unit 0 */
	atomic_set(&mdev->busy, 0);
}

/* preprocess hotplugs or scans */
static void maple_response_devinfo(struct maple_device *mdev,
				   char *recvbuf)
{
	char submask;
	if (!started || (scanning == 2) || !fullscan) {
		if ((mdev->unit == 0) && (checked[mdev->port] == false)) {
			checked[mdev->port] = true;
			maple_attach_driver(mdev);
		} else {
			if (mdev->unit != 0)
				maple_attach_driver(mdev);
			if (mdev->unit == 0) {
				empty[mdev->port] = false;
				maple_attach_driver(mdev);
			}
		}
	}
	if (mdev->unit == 0) {
		submask = recvbuf[2] & 0x1F;
		if (submask ^ subdevice_map[mdev->port]) {
			maple_map_subunits(mdev, submask);
			subdevice_map[mdev->port] = submask;
		}
	}
}

static void maple_response_fileerr(struct maple_device *mdev, void *recvbuf)
{
	if (mdev->fileerr_handler) {
		mdev->fileerr_handler(mdev, recvbuf);
		return;
	} else
		dev_warn(&mdev->dev, "device at (%d, %d) reports"
			"file error 0x%X\n", mdev->port, mdev->unit,
			((int *)recvbuf)[1]);
}

static void maple_port_rescan(void)
{
	int i;
	struct maple_device *mdev;

	fullscan = 1;
	for (i = 0; i < MAPLE_PORTS; i++) {
		if (checked[i] == false) {
			fullscan = 0;
			mdev = baseunits[i];
			maple_add_packet(mdev, 0, MAPLE_COMMAND_DEVINFO,
				0, NULL);
		}
	}
}

/* maple dma end bottom half - implemented via workqueue */
static void maple_dma_handler(struct work_struct *work)
{
	struct mapleq *mq, *nmq;
	struct maple_device *mdev;
	char *recvbuf;
	enum maple_code code;

	if (!maple_dma_done())
		return;
	__raw_writel(0, MAPLE_ENABLE);
	if (!list_empty(&maple_sentq)) {
		list_for_each_entry_safe(mq, nmq, &maple_sentq, list) {
			mdev = mq->dev;
			recvbuf = mq->recvbuf->buf;
			__flush_invalidate_region(sh_cacheop_vaddr(recvbuf),
					0x400);
			code = recvbuf[0];
			kfree(mq->sendbuf);
			list_del_init(&mq->list);
			switch (code) {
			case MAPLE_RESPONSE_NONE:
				maple_response_none(mdev);
				break;

			case MAPLE_RESPONSE_DEVINFO:
				maple_response_devinfo(mdev, recvbuf);
				atomic_set(&mdev->busy, 0);
				break;

			case MAPLE_RESPONSE_DATATRF:
				if (mdev->callback)
					mdev->callback(mq);
				atomic_set(&mdev->busy, 0);
				wake_up(&mdev->maple_wait);
				break;

			case MAPLE_RESPONSE_FILEERR:
				maple_response_fileerr(mdev, recvbuf);
				atomic_set(&mdev->busy, 0);
				wake_up(&mdev->maple_wait);
				break;

			case MAPLE_RESPONSE_AGAIN:
			case MAPLE_RESPONSE_BADCMD:
			case MAPLE_RESPONSE_BADFUNC:
				dev_warn(&mdev->dev, "non-fatal error"
					" 0x%X at (%d, %d)\n", code,
					mdev->port, mdev->unit);
				atomic_set(&mdev->busy, 0);
				break;

			case MAPLE_RESPONSE_ALLINFO:
				dev_notice(&mdev->dev, "extended"
				" device information request for (%d, %d)"
				" but call is not supported\n", mdev->port,
				mdev->unit);
				atomic_set(&mdev->busy, 0);
				break;

			case MAPLE_RESPONSE_OK:
				atomic_set(&mdev->busy, 0);
				wake_up(&mdev->maple_wait);
				break;

			default:
				break;
			}
		}
		/* if scanning is 1 then we have subdevices to check */
		if (scanning == 1) {
			maple_send();
			scanning = 2;
		} else
			scanning = 0;
		/*check if we have actually tested all ports yet */
		if (!fullscan)
			maple_port_rescan();
		/* mark that we have been through the first scan */
		started = 1;
	}
	maple_send();
}

static irqreturn_t maple_dma_interrupt(int irq, void *dev_id)
{
	/* Load everything into the bottom half */
	schedule_work(&maple_dma_process);
	return IRQ_HANDLED;
}

static irqreturn_t maple_vblank_interrupt(int irq, void *dev_id)
{
	schedule_work(&maple_vblank_process);
	return IRQ_HANDLED;
}

static int maple_set_dma_interrupt_handler(void)
{
	return request_irq(HW_EVENT_MAPLE_DMA, maple_dma_interrupt,
		IRQF_SHARED, "maple bus DMA", &maple_unsupported_device);
}

static int maple_set_vblank_interrupt_handler(void)
{
	return request_irq(HW_EVENT_VSYNC, maple_vblank_interrupt,
		IRQF_SHARED, "maple bus VBLANK", &maple_unsupported_device);
}

static int maple_get_dma_buffer(void)
{
	maple_sendbuf =
	    (void *) __get_free_pages(GFP_KERNEL | __GFP_ZERO,
				      MAPLE_DMA_PAGES);
	if (!maple_sendbuf)
		return -ENOMEM;
	return 0;
}

static int maple_match_bus_driver(struct device *devptr,
				  struct device_driver *drvptr)
{
	struct maple_driver *maple_drv = to_maple_driver(drvptr);
	struct maple_device *maple_dev = to_maple_dev(devptr);

	/* Trap empty port case */
	if (maple_dev->devinfo.function == 0xFFFFFFFF)
		return 0;
	else if (maple_dev->devinfo.function &
		 cpu_to_be32(maple_drv->function))
		return 1;
	return 0;
}

static int maple_bus_uevent(struct device *dev,
			    struct kobj_uevent_env *env)
{
	return 0;
}

static void maple_bus_release(struct device *dev)
{
}

static struct maple_driver maple_unsupported_device = {
	.drv = {
		.name = "maple_unsupported_device",
		.bus = &maple_bus_type,
	},
};
/*
 * maple_bus_type - core maple bus structure
 */
struct bus_type maple_bus_type = {
	.name = "maple",
	.match = maple_match_bus_driver,
	.uevent = maple_bus_uevent,
};
EXPORT_SYMBOL_GPL(maple_bus_type);

static struct device maple_bus = {
	.init_name = "maple",
	.release = maple_bus_release,
};

static int __init maple_bus_init(void)
{
	int retval, i;
	struct maple_device *mdev[MAPLE_PORTS];

	__raw_writel(0, MAPLE_ENABLE);

	retval = device_register(&maple_bus);
	if (retval)
		goto cleanup;

	retval = bus_register(&maple_bus_type);
	if (retval)
		goto cleanup_device;

	retval = driver_register(&maple_unsupported_device.drv);
	if (retval)
		goto cleanup_bus;

	/* allocate memory for maple bus dma */
	retval = maple_get_dma_buffer();
	if (retval) {
		dev_err(&maple_bus, "failed to allocate DMA buffers\n");
		goto cleanup_basic;
	}

	/* set up DMA interrupt handler */
	retval = maple_set_dma_interrupt_handler();
	if (retval) {
		dev_err(&maple_bus, "bus failed to grab maple "
			"DMA IRQ\n");
		goto cleanup_dma;
	}

	/* set up VBLANK interrupt handler */
	retval = maple_set_vblank_interrupt_handler();
	if (retval) {
		dev_err(&maple_bus, "bus failed to grab VBLANK IRQ\n");
		goto cleanup_irq;
	}

	maple_queue_cache = KMEM_CACHE(maple_buffer, SLAB_HWCACHE_ALIGN);

	if (!maple_queue_cache)
		goto cleanup_bothirqs;

	INIT_LIST_HEAD(&maple_waitq);
	INIT_LIST_HEAD(&maple_sentq);

	/* setup maple ports */
	for (i = 0; i < MAPLE_PORTS; i++) {
		checked[i] = false;
		empty[i] = false;
		mdev[i] = maple_alloc_dev(i, 0);
		if (!mdev[i]) {
			while (i-- > 0)
				maple_free_dev(mdev[i]);
			goto cleanup_cache;
		}
		baseunits[i] = mdev[i];
		atomic_set(&mdev[i]->busy, 1);
		maple_add_packet(mdev[i], 0, MAPLE_COMMAND_DEVINFO, 0, NULL);
		subdevice_map[i] = 0;
	}

	maple_pnp_time = jiffies + HZ;
	/* prepare initial queue */
	maple_send();
	dev_info(&maple_bus, "bus core now registered\n");

	return 0;

cleanup_cache:
	kmem_cache_destroy(maple_queue_cache);

cleanup_bothirqs:
	free_irq(HW_EVENT_VSYNC, 0);

cleanup_irq:
	free_irq(HW_EVENT_MAPLE_DMA, 0);

cleanup_dma:
	free_pages((unsigned long) maple_sendbuf, MAPLE_DMA_PAGES);

cleanup_basic:
	driver_unregister(&maple_unsupported_device.drv);

cleanup_bus:
	bus_unregister(&maple_bus_type);

cleanup_device:
	device_unregister(&maple_bus);

cleanup:
	printk(KERN_ERR "Maple bus registration failed\n");
	return retval;
}
/* Push init to later to ensure hardware gets detected */
fs_initcall(maple_bus_init);
