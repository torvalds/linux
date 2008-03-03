/*
 * Core maple bus functionality
 *
 *  Copyright (C) 2007, 2008 Adrian McMenamin
 *
 * Based on 2.4 code by:
 *
 *  Copyright (C) 2000-2001 YAEGASHI Takeshi
 *  Copyright (C) 2001 M. R. Brown
 *  Copyright (C) 2001 Paul Mundt
 *
 * and others.
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
#include <asm/cacheflush.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/mach/dma.h>
#include <asm/mach/sysasic.h>
#include <asm/mach/maple.h>
#include <linux/delay.h>

MODULE_AUTHOR("Yaegshi Takeshi, Paul Mundt, M.R. Brown, Adrian McMenamin");
MODULE_DESCRIPTION("Maple bus driver for Dreamcast");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{{SEGA, Dreamcast/Maple}}");

static void maple_dma_handler(struct work_struct *work);
static void maple_vblank_handler(struct work_struct *work);

static DECLARE_WORK(maple_dma_process, maple_dma_handler);
static DECLARE_WORK(maple_vblank_process, maple_vblank_handler);

static LIST_HEAD(maple_waitq);
static LIST_HEAD(maple_sentq);

static DEFINE_MUTEX(maple_list_lock);

static struct maple_driver maple_dummy_driver;
static struct device maple_bus;
static int subdevice_map[MAPLE_PORTS];
static unsigned long *maple_sendbuf, *maple_sendptr, *maple_lastptr;
static unsigned long maple_pnp_time;
static int started, scanning, liststatus, fullscan;
static struct kmem_cache *maple_queue_cache;

struct maple_device_specify {
	int port;
	int unit;
};

static bool checked[4];
static struct maple_device *baseunits[4];

/**
 *  maple_driver_register - register a device driver
 *  automatically makes the driver bus a maple bus
 *  @drv: the driver to be registered
 */
int maple_driver_register(struct device_driver *drv)
{
	if (!drv)
		return -EINVAL;
	drv->bus = &maple_bus_type;
	return driver_register(drv);
}
EXPORT_SYMBOL_GPL(maple_driver_register);

/* set hardware registers to enable next round of dma */
static void maplebus_dma_reset(void)
{
	ctrl_outl(MAPLE_MAGIC, MAPLE_RESET);
	/* set trig type to 0 for software trigger, 1 for hardware (VBLANK) */
	ctrl_outl(1, MAPLE_TRIGTYPE);
	ctrl_outl(MAPLE_2MBPS | MAPLE_TIMEOUT(50000), MAPLE_SPEED);
	ctrl_outl(PHYSADDR(maple_sendbuf), MAPLE_DMAADDR);
	ctrl_outl(1, MAPLE_ENABLE);
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
	return (ctrl_inl(MAPLE_STATE) & 1) == 0;
}

static void maple_release_device(struct device *dev)
{
	struct maple_device *mdev;
	struct mapleq *mq;
	if (!dev)
		return;
	mdev = to_maple_dev(dev);
	mq = mdev->mq;
	if (mq) {
		if (mq->recvbufdcsp)
			kmem_cache_free(maple_queue_cache, mq->recvbufdcsp);
		kfree(mq);
		mq = NULL;
	}
	kfree(mdev);
}

/**
 * maple_add_packet - add a single instruction to the queue
 * @mq: instruction to add to waiting queue
 */
void maple_add_packet(struct mapleq *mq)
{
	mutex_lock(&maple_list_lock);
	list_add(&mq->list, &maple_waitq);
	mutex_unlock(&maple_list_lock);
}
EXPORT_SYMBOL_GPL(maple_add_packet);

static struct mapleq *maple_allocq(struct maple_device *mdev)
{
	struct mapleq *mq;

	mq = kmalloc(sizeof(*mq), GFP_KERNEL);
	if (!mq)
		return NULL;

	mq->dev = mdev;
	mq->recvbufdcsp = kmem_cache_zalloc(maple_queue_cache, GFP_KERNEL);
	mq->recvbuf = (void *) P2SEGADDR(mq->recvbufdcsp);
	if (!mq->recvbuf) {
		kfree(mq);
		return NULL;
	}

	return mq;
}

static struct maple_device *maple_alloc_dev(int port, int unit)
{
	struct maple_device *mdev;

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
	mdev->function = 0;
	return mdev;
}

static void maple_free_dev(struct maple_device *mdev)
{
	if (!mdev)
		return;
	if (mdev->mq) {
		if (mdev->mq->recvbufdcsp)
			kmem_cache_free(maple_queue_cache,
				mdev->mq->recvbufdcsp);
		kfree(mdev->mq);
	}
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
	*maple_sendptr++ = PHYSADDR(mq->recvbuf);
	*maple_sendptr++ =
	    mq->command | (to << 8) | (from << 16) | (len << 24);

	while (len-- > 0)
		*maple_sendptr++ = *lsendbuf++;
}

/* build up command queue */
static void maple_send(void)
{
	int i;
	int maple_packets;
	struct mapleq *mq, *nmq;

	if (!list_empty(&maple_sentq))
		return;
	if (list_empty(&maple_waitq) || !maple_dma_done())
		return;
	maple_packets = 0;
	maple_sendptr = maple_lastptr = maple_sendbuf;
	list_for_each_entry_safe(mq, nmq, &maple_waitq, list) {
		maple_build_block(mq);
		list_move(&mq->list, &maple_sentq);
		if (maple_packets++ > MAPLE_MAXPACKETS)
			break;
	}
	if (maple_packets > 0) {
		for (i = 0; i < (1 << MAPLE_DMA_PAGES); i++)
			dma_cache_sync(0, maple_sendbuf + i * PAGE_SIZE,
				       PAGE_SIZE, DMA_BIDIRECTIONAL);
	}
}

static int attach_matching_maple_driver(struct device_driver *driver,
					void *devptr)
{
	struct maple_driver *maple_drv;
	struct maple_device *mdev;

	mdev = devptr;
	maple_drv = to_maple_driver(driver);
	if (mdev->devinfo.function & be32_to_cpu(maple_drv->function)) {
		if (maple_drv->connect(mdev) == 0) {
			mdev->driver = maple_drv;
			return 1;
		}
	}
	return 0;
}

static void maple_detach_driver(struct maple_device *mdev)
{
	if (!mdev)
		return;
	if (mdev->driver) {
		if (mdev->driver->disconnect)
			mdev->driver->disconnect(mdev);
	}
	mdev->driver = NULL;
	device_unregister(&mdev->dev);
	mdev = NULL;
}

/* process initial MAPLE_COMMAND_DEVINFO for each device or port */
static void maple_attach_driver(struct maple_device *mdev)
{
	char *p, *recvbuf;
	unsigned long function;
	int matched, retval;

	recvbuf = mdev->mq->recvbuf;
	/* copy the data as individual elements in
	* case of memory optimisation */
	memcpy(&mdev->devinfo.function, recvbuf + 4, 4);
	memcpy(&mdev->devinfo.function_data[0], recvbuf + 8, 12);
	memcpy(&mdev->devinfo.area_code, recvbuf + 20, 1);
	memcpy(&mdev->devinfo.connector_direction, recvbuf + 21, 1);
	memcpy(&mdev->devinfo.product_name[0], recvbuf + 22, 30);
	memcpy(&mdev->devinfo.product_licence[0], recvbuf + 52, 60);
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

	printk(KERN_INFO "Maple device detected: %s\n",
		mdev->product_name);
	printk(KERN_INFO "Maple device: %s\n", mdev->product_licence);

	function = be32_to_cpu(mdev->devinfo.function);

	if (function > 0x200) {
		/* Do this silently - as not a real device */
		function = 0;
		mdev->driver = &maple_dummy_driver;
		sprintf(mdev->dev.bus_id, "%d:0.port", mdev->port);
	} else {
		printk(KERN_INFO
			"Maple bus at (%d, %d): Function 0x%lX\n",
			mdev->port, mdev->unit, function);

		matched =
		    bus_for_each_drv(&maple_bus_type, NULL, mdev,
				     attach_matching_maple_driver);

		if (matched == 0) {
			/* Driver does not exist yet */
			printk(KERN_INFO
				"No maple driver found.\n");
			mdev->driver = &maple_dummy_driver;
		}
		sprintf(mdev->dev.bus_id, "%d:0%d.%lX", mdev->port,
			mdev->unit, function);
	}
	mdev->function = function;
	mdev->dev.release = &maple_release_device;
	retval = device_register(&mdev->dev);
	if (retval) {
		printk(KERN_INFO
		"Maple bus: Attempt to register device"
		" (%x, %x) failed.\n",
		mdev->port, mdev->unit);
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
static int detach_maple_device(struct device *device, void *portptr)
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
	struct maple_device *maple_dev = to_maple_dev(device);

	if ((maple_dev->interval > 0)
	    && time_after(jiffies, maple_dev->when)) {
		maple_dev->when = jiffies + maple_dev->interval;
		maple_dev->mq->command = MAPLE_COMMAND_GETCOND;
		maple_dev->mq->sendbuf = &maple_dev->function;
		maple_dev->mq->length = 1;
		maple_add_packet(maple_dev->mq);
		liststatus++;
	} else {
		if (time_after(jiffies, maple_pnp_time)) {
			maple_dev->mq->command = MAPLE_COMMAND_DEVINFO;
			maple_dev->mq->length = 0;
			maple_add_packet(maple_dev->mq);
			liststatus++;
		}
	}

	return 0;
}

/* VBLANK bottom half - implemented via workqueue */
static void maple_vblank_handler(struct work_struct *work)
{
	if (!maple_dma_done())
		return;
	if (!list_empty(&maple_sentq))
		return;
	ctrl_outl(0, MAPLE_ENABLE);
	liststatus = 0;
	bus_for_each_dev(&maple_bus_type, NULL, NULL,
			 setup_maple_commands);
	if (time_after(jiffies, maple_pnp_time))
		maple_pnp_time = jiffies + MAPLE_PNP_INTERVAL;
	if (liststatus && list_empty(&maple_sentq)) {
		INIT_LIST_HEAD(&maple_sentq);
		maple_send();
	}
	maplebus_dma_reset();
}

/* handle devices added via hotplugs - placing them on queue for DEVINFO*/
static void maple_map_subunits(struct maple_device *mdev, int submask)
{
	int retval, k, devcheck;
	struct maple_device *mdev_add;
	struct maple_device_specify ds;

	for (k = 0; k < 5; k++) {
		ds.port = mdev->port;
		ds.unit = k + 1;
		retval =
		    bus_for_each_dev(&maple_bus_type, NULL, &ds,
				     detach_maple_device);
		if (retval) {
			submask = submask >> 1;
			continue;
		}
		devcheck = submask & 0x01;
		if (devcheck) {
			mdev_add = maple_alloc_dev(mdev->port, k + 1);
			if (!mdev_add)
				return;
			mdev_add->mq->command = MAPLE_COMMAND_DEVINFO;
			mdev_add->mq->length = 0;
			maple_add_packet(mdev_add->mq);
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
static void maple_response_none(struct maple_device *mdev,
				struct mapleq *mq)
{
	if (mdev->unit != 0) {
		list_del(&mq->list);
		maple_clean_submap(mdev);
		printk(KERN_INFO
		       "Maple bus device detaching at (%d, %d)\n",
		       mdev->port, mdev->unit);
		maple_detach_driver(mdev);
		return;
	}
	if (!started || !fullscan) {
		if (checked[mdev->port] == false) {
			checked[mdev->port] = true;
			printk(KERN_INFO "No maple devices attached"
				" to port %d\n", mdev->port);
		}
		return;
	}
	maple_clean_submap(mdev);
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
		}
		return;
	}
	if (mdev->unit == 0) {
		submask = recvbuf[2] & 0x1F;
		if (submask ^ subdevice_map[mdev->port]) {
			maple_map_subunits(mdev, submask);
			subdevice_map[mdev->port] = submask;
		}
	}
}

/* maple dma end bottom half - implemented via workqueue */
static void maple_dma_handler(struct work_struct *work)
{
	struct mapleq *mq, *nmq;
	struct maple_device *dev;
	char *recvbuf;
	enum maple_code code;
	int i;

	if (!maple_dma_done())
		return;
	ctrl_outl(0, MAPLE_ENABLE);
	if (!list_empty(&maple_sentq)) {
		list_for_each_entry_safe(mq, nmq, &maple_sentq, list) {
			recvbuf = mq->recvbuf;
			code = recvbuf[0];
			dev = mq->dev;
			switch (code) {
			case MAPLE_RESPONSE_NONE:
				maple_response_none(dev, mq);
				break;

			case MAPLE_RESPONSE_DEVINFO:
				maple_response_devinfo(dev, recvbuf);
				break;

			case MAPLE_RESPONSE_DATATRF:
				if (dev->callback)
					dev->callback(mq);
				break;

			case MAPLE_RESPONSE_FILEERR:
			case MAPLE_RESPONSE_AGAIN:
			case MAPLE_RESPONSE_BADCMD:
			case MAPLE_RESPONSE_BADFUNC:
				printk(KERN_DEBUG
				       "Maple non-fatal error 0x%X\n",
				       code);
				break;

			case MAPLE_RESPONSE_ALLINFO:
				printk(KERN_DEBUG
				       "Maple - extended device information"
					" not supported\n");
				break;

			case MAPLE_RESPONSE_OK:
				break;

			default:
				break;
			}
		}
		INIT_LIST_HEAD(&maple_sentq);
		if (scanning == 1) {
			maple_send();
			scanning = 2;
		} else
			scanning = 0;

		if (!fullscan) {
			fullscan = 1;
			for (i = 0; i < MAPLE_PORTS; i++) {
				if (checked[i] == false) {
					fullscan = 0;
					dev = baseunits[i];
					dev->mq->command =
						MAPLE_COMMAND_DEVINFO;
					dev->mq->length = 0;
					maple_add_packet(dev->mq);
				}
			}
		}
		if (started == 0)
			started = 1;
	}
	maplebus_dma_reset();
}

static irqreturn_t maplebus_dma_interrupt(int irq, void *dev_id)
{
	/* Load everything into the bottom half */
	schedule_work(&maple_dma_process);
	return IRQ_HANDLED;
}

static irqreturn_t maplebus_vblank_interrupt(int irq, void *dev_id)
{
	schedule_work(&maple_vblank_process);
	return IRQ_HANDLED;
}

static int maple_set_dma_interrupt_handler(void)
{
	return request_irq(HW_EVENT_MAPLE_DMA, maplebus_dma_interrupt,
		IRQF_SHARED, "maple bus DMA", &maple_dummy_driver);
}

static int maple_set_vblank_interrupt_handler(void)
{
	return request_irq(HW_EVENT_VSYNC, maplebus_vblank_interrupt,
		IRQF_SHARED, "maple bus VBLANK", &maple_dummy_driver);
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

static int match_maple_bus_driver(struct device *devptr,
				  struct device_driver *drvptr)
{
	struct maple_driver *maple_drv;
	struct maple_device *maple_dev;

	maple_drv = container_of(drvptr, struct maple_driver, drv);
	maple_dev = container_of(devptr, struct maple_device, dev);
	/* Trap empty port case */
	if (maple_dev->devinfo.function == 0xFFFFFFFF)
		return 0;
	else if (maple_dev->devinfo.function &
		 be32_to_cpu(maple_drv->function))
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

static struct maple_driver maple_dummy_driver = {
	.drv = {
		.name = "maple_dummy_driver",
		.bus = &maple_bus_type,
	},
};

struct bus_type maple_bus_type = {
	.name = "maple",
	.match = match_maple_bus_driver,
	.uevent = maple_bus_uevent,
};
EXPORT_SYMBOL_GPL(maple_bus_type);

static struct device maple_bus = {
	.bus_id = "maple",
	.release = maple_bus_release,
};

static int __init maple_bus_init(void)
{
	int retval, i;
	struct maple_device *mdev[MAPLE_PORTS];
	ctrl_outl(0, MAPLE_STATE);

	retval = device_register(&maple_bus);
	if (retval)
		goto cleanup;

	retval = bus_register(&maple_bus_type);
	if (retval)
		goto cleanup_device;

	retval = driver_register(&maple_dummy_driver.drv);
	if (retval)
		goto cleanup_bus;

	/* allocate memory for maple bus dma */
	retval = maple_get_dma_buffer();
	if (retval) {
		printk(KERN_INFO
		       "Maple bus: Failed to allocate Maple DMA buffers\n");
		goto cleanup_basic;
	}

	/* set up DMA interrupt handler */
	retval = maple_set_dma_interrupt_handler();
	if (retval) {
		printk(KERN_INFO
		       "Maple bus: Failed to grab maple DMA IRQ\n");
		goto cleanup_dma;
	}

	/* set up VBLANK interrupt handler */
	retval = maple_set_vblank_interrupt_handler();
	if (retval) {
		printk(KERN_INFO "Maple bus: Failed to grab VBLANK IRQ\n");
		goto cleanup_irq;
	}

	maple_queue_cache =
	    kmem_cache_create("maple_queue_cache", 0x400, 0,
			      SLAB_POISON|SLAB_HWCACHE_ALIGN, NULL);

	if (!maple_queue_cache)
		goto cleanup_bothirqs;

	/* setup maple ports */
	for (i = 0; i < MAPLE_PORTS; i++) {
		checked[i] = false;
		mdev[i] = maple_alloc_dev(i, 0);
		baseunits[i] = mdev[i];
		if (!mdev[i]) {
			while (i-- > 0)
				maple_free_dev(mdev[i]);
			goto cleanup_cache;
		}
		mdev[i]->mq->command = MAPLE_COMMAND_DEVINFO;
		mdev[i]->mq->length = 0;
		maple_add_packet(mdev[i]->mq);
		subdevice_map[i] = 0;
	}

	/* setup maplebus hardware */
	maplebus_dma_reset();
	/* initial detection */
	maple_send();
	maple_pnp_time = jiffies;
	printk(KERN_INFO "Maple bus core now registered.\n");

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
	driver_unregister(&maple_dummy_driver.drv);

cleanup_bus:
	bus_unregister(&maple_bus_type);

cleanup_device:
	device_unregister(&maple_bus);

cleanup:
	printk(KERN_INFO "Maple bus registration failed\n");
	return retval;
}
/* Push init to later to ensure hardware gets detected */
fs_initcall(maple_bus_init);
