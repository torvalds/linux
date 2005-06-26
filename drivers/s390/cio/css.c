/*
 *  drivers/s390/cio/css.c
 *  driver for channel subsystem
 *   $Revision: 1.85 $
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *		 Cornelia Huck (cohuck@de.ibm.com)
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>

#include "css.h"
#include "cio.h"
#include "cio_debug.h"
#include "ioasm.h"
#include "chsc.h"

unsigned int highest_subchannel;
int need_rescan = 0;
int css_init_done = 0;

struct pgid global_pgid;
int css_characteristics_avail = 0;

struct device css_bus_device = {
	.bus_id = "css0",
};

static struct subchannel *
css_alloc_subchannel(int irq)
{
	struct subchannel *sch;
	int ret;

	sch = kmalloc (sizeof (*sch), GFP_KERNEL | GFP_DMA);
	if (sch == NULL)
		return ERR_PTR(-ENOMEM);
	ret = cio_validate_subchannel (sch, irq);
	if (ret < 0) {
		kfree(sch);
		return ERR_PTR(ret);
	}
	if (irq > highest_subchannel)
		highest_subchannel = irq;

	if (sch->st != SUBCHANNEL_TYPE_IO) {
		/* For now we ignore all non-io subchannels. */
		kfree(sch);
		return ERR_PTR(-EINVAL);
	}

	/* 
	 * Set intparm to subchannel address.
	 * This is fine even on 64bit since the subchannel is always located
	 * under 2G.
	 */
	sch->schib.pmcw.intparm = (__u32)(unsigned long)sch;
	ret = cio_modify(sch);
	if (ret) {
		kfree(sch);
		return ERR_PTR(ret);
	}
	return sch;
}

static void
css_free_subchannel(struct subchannel *sch)
{
	if (sch) {
		/* Reset intparm to zeroes. */
		sch->schib.pmcw.intparm = 0;
		cio_modify(sch);
		kfree(sch);
	}
	
}

static void
css_subchannel_release(struct device *dev)
{
	struct subchannel *sch;

	sch = to_subchannel(dev);
	if (!cio_is_console(sch->irq))
		kfree(sch);
}

extern int css_get_ssd_info(struct subchannel *sch);

static int
css_register_subchannel(struct subchannel *sch)
{
	int ret;

	/* Initialize the subchannel structure */
	sch->dev.parent = &css_bus_device;
	sch->dev.bus = &css_bus_type;
	sch->dev.release = &css_subchannel_release;
	
	/* make it known to the system */
	ret = device_register(&sch->dev);
	if (ret)
		printk (KERN_WARNING "%s: could not register %s\n",
			__func__, sch->dev.bus_id);
	else
		css_get_ssd_info(sch);
	return ret;
}

int
css_probe_device(int irq)
{
	int ret;
	struct subchannel *sch;

	sch = css_alloc_subchannel(irq);
	if (IS_ERR(sch))
		return PTR_ERR(sch);
	ret = css_register_subchannel(sch);
	if (ret)
		css_free_subchannel(sch);
	return ret;
}

static int
check_subchannel(struct device * dev, void * data)
{
	struct subchannel *sch;
	int irq = (unsigned long)data;

	sch = to_subchannel(dev);
	return (sch->irq == irq);
}

struct subchannel *
get_subchannel_by_schid(int irq)
{
	struct device *dev;

	dev = bus_find_device(&css_bus_type, NULL,
			      (void *)(unsigned long)irq, check_subchannel);

	return dev ? to_subchannel(dev) : NULL;
}


static inline int
css_get_subchannel_status(struct subchannel *sch, int schid)
{
	struct schib schib;
	int cc;

	cc = stsch(schid, &schib);
	if (cc)
		return CIO_GONE;
	if (!schib.pmcw.dnv)
		return CIO_GONE;
	if (sch && sch->schib.pmcw.dnv &&
	    (schib.pmcw.dev != sch->schib.pmcw.dev))
		return CIO_REVALIDATE;
	if (sch && !sch->lpm)
		return CIO_NO_PATH;
	return CIO_OPER;
}
	
static int
css_evaluate_subchannel(int irq, int slow)
{
	int event, ret, disc;
	struct subchannel *sch;
	unsigned long flags;

	sch = get_subchannel_by_schid(irq);
	disc = sch ? device_is_disconnected(sch) : 0;
	if (disc && slow) {
		if (sch)
			put_device(&sch->dev);
		return 0; /* Already processed. */
	}
	/*
	 * We've got a machine check, so running I/O won't get an interrupt.
	 * Kill any pending timers.
	 */
	if (sch)
		device_kill_pending_timer(sch);
	if (!disc && !slow) {
		if (sch)
			put_device(&sch->dev);
		return -EAGAIN; /* Will be done on the slow path. */
	}
	event = css_get_subchannel_status(sch, irq);
	CIO_MSG_EVENT(4, "Evaluating schid %04x, event %d, %s, %s path.\n",
		      irq, event, sch?(disc?"disconnected":"normal"):"unknown",
		      slow?"slow":"fast");
	switch (event) {
	case CIO_NO_PATH:
	case CIO_GONE:
		if (!sch) {
			/* Never used this subchannel. Ignore. */
			ret = 0;
			break;
		}
		if (disc && (event == CIO_NO_PATH)) {
			/*
			 * Uargh, hack again. Because we don't get a machine
			 * check on configure on, our path bookkeeping can
			 * be out of date here (it's fine while we only do
			 * logical varying or get chsc machine checks). We
			 * need to force reprobing or we might miss devices
			 * coming operational again. It won't do harm in real
			 * no path situations.
			 */
			spin_lock_irqsave(&sch->lock, flags);
			device_trigger_reprobe(sch);
			spin_unlock_irqrestore(&sch->lock, flags);
			ret = 0;
			break;
		}
		if (sch->driver && sch->driver->notify &&
		    sch->driver->notify(&sch->dev, event)) {
			cio_disable_subchannel(sch);
			device_set_disconnected(sch);
			ret = 0;
			break;
		}
		/*
		 * Unregister subchannel.
		 * The device will be killed automatically.
		 */
		cio_disable_subchannel(sch);
		device_unregister(&sch->dev);
		/* Reset intparm to zeroes. */
		sch->schib.pmcw.intparm = 0;
		cio_modify(sch);
		put_device(&sch->dev);
		ret = 0;
		break;
	case CIO_REVALIDATE:
		/* 
		 * Revalidation machine check. Sick.
		 * We don't notify the driver since we have to throw the device
		 * away in any case.
		 */
		if (!disc) {
			device_unregister(&sch->dev);
			/* Reset intparm to zeroes. */
			sch->schib.pmcw.intparm = 0;
			cio_modify(sch);
			put_device(&sch->dev);
			ret = css_probe_device(irq);
		} else {
			/*
			 * We can't immediately deregister the disconnected
			 * device since it might block.
			 */
			spin_lock_irqsave(&sch->lock, flags);
			device_trigger_reprobe(sch);
			spin_unlock_irqrestore(&sch->lock, flags);
			ret = 0;
		}
		break;
	case CIO_OPER:
		if (disc) {
			spin_lock_irqsave(&sch->lock, flags);
			/* Get device operational again. */
			device_trigger_reprobe(sch);
			spin_unlock_irqrestore(&sch->lock, flags);
		}
		ret = sch ? 0 : css_probe_device(irq);
		break;
	default:
		BUG();
		ret = 0;
	}
	return ret;
}

static void
css_rescan_devices(void)
{
	int irq, ret;

	for (irq = 0; irq < __MAX_SUBCHANNELS; irq++) {
		ret = css_evaluate_subchannel(irq, 1);
		/* No more memory. It doesn't make sense to continue. No
		 * panic because this can happen in midflight and just
		 * because we can't use a new device is no reason to crash
		 * the system. */
		if (ret == -ENOMEM)
			break;
		/* -ENXIO indicates that there are no more subchannels. */
		if (ret == -ENXIO)
			break;
	}
}

struct slow_subchannel {
	struct list_head slow_list;
	unsigned long schid;
};

static LIST_HEAD(slow_subchannels_head);
static DEFINE_SPINLOCK(slow_subchannel_lock);

static void
css_trigger_slow_path(void)
{
	CIO_TRACE_EVENT(4, "slowpath");

	if (need_rescan) {
		need_rescan = 0;
		css_rescan_devices();
		return;
	}

	spin_lock_irq(&slow_subchannel_lock);
	while (!list_empty(&slow_subchannels_head)) {
		struct slow_subchannel *slow_sch =
			list_entry(slow_subchannels_head.next,
				   struct slow_subchannel, slow_list);

		list_del_init(slow_subchannels_head.next);
		spin_unlock_irq(&slow_subchannel_lock);
		css_evaluate_subchannel(slow_sch->schid, 1);
		spin_lock_irq(&slow_subchannel_lock);
		kfree(slow_sch);
	}
	spin_unlock_irq(&slow_subchannel_lock);
}

typedef void (*workfunc)(void *);
DECLARE_WORK(slow_path_work, (workfunc)css_trigger_slow_path, NULL);
struct workqueue_struct *slow_path_wq;

/*
 * Rescan for new devices. FIXME: This is slow.
 * This function is called when we have lost CRWs due to overflows and we have
 * to do subchannel housekeeping.
 */
void
css_reiterate_subchannels(void)
{
	css_clear_subchannel_slow_list();
	need_rescan = 1;
}

/*
 * Called from the machine check handler for subchannel report words.
 */
int
css_process_crw(int irq)
{
	int ret;

	CIO_CRW_EVENT(2, "source is subchannel %04X\n", irq);

	if (need_rescan)
		/* We need to iterate all subchannels anyway. */
		return -EAGAIN;
	/* 
	 * Since we are always presented with IPI in the CRW, we have to
	 * use stsch() to find out if the subchannel in question has come
	 * or gone.
	 */
	ret = css_evaluate_subchannel(irq, 0);
	if (ret == -EAGAIN) {
		if (css_enqueue_subchannel_slow(irq)) {
			css_clear_subchannel_slow_list();
			need_rescan = 1;
		}
	}
	return ret;
}

static void __init
css_generate_pgid(void)
{
	/* Let's build our path group ID here. */
	if (css_characteristics_avail && css_general_characteristics.mcss)
		global_pgid.cpu_addr = 0x8000;
	else {
#ifdef CONFIG_SMP
		global_pgid.cpu_addr = hard_smp_processor_id();
#else
		global_pgid.cpu_addr = 0;
#endif
	}
	global_pgid.cpu_id = ((cpuid_t *) __LC_CPUID)->ident;
	global_pgid.cpu_model = ((cpuid_t *) __LC_CPUID)->machine;
	global_pgid.tod_high = (__u32) (get_clock() >> 32);
}

/*
 * Now that the driver core is running, we can setup our channel subsystem.
 * The struct subchannel's are created during probing (except for the
 * static console subchannel).
 */
static int __init
init_channel_subsystem (void)
{
	int ret, irq;

	if (chsc_determine_css_characteristics() == 0)
		css_characteristics_avail = 1;

	css_generate_pgid();

	if ((ret = bus_register(&css_bus_type)))
		goto out;
	if ((ret = device_register (&css_bus_device)))
		goto out_bus;

	css_init_done = 1;

	ctl_set_bit(6, 28);

	for (irq = 0; irq < __MAX_SUBCHANNELS; irq++) {
		struct subchannel *sch;

		if (cio_is_console(irq))
			sch = cio_get_console_subchannel();
		else {
			sch = css_alloc_subchannel(irq);
			if (IS_ERR(sch))
				ret = PTR_ERR(sch);
			else
				ret = 0;
			if (ret == -ENOMEM)
				panic("Out of memory in "
				      "init_channel_subsystem\n");
			/* -ENXIO: no more subchannels. */
			if (ret == -ENXIO)
				break;
			if (ret)
				continue;
		}
		/*
		 * We register ALL valid subchannels in ioinfo, even those
		 * that have been present before init_channel_subsystem.
		 * These subchannels can't have been registered yet (kmalloc
		 * not working) so we do it now. This is true e.g. for the
		 * console subchannel.
		 */
		css_register_subchannel(sch);
	}
	return 0;

out_bus:
	bus_unregister(&css_bus_type);
out:
	return ret;
}

/*
 * find a driver for a subchannel. They identify by the subchannel
 * type with the exception that the console subchannel driver has its own
 * subchannel type although the device is an i/o subchannel
 */
static int
css_bus_match (struct device *dev, struct device_driver *drv)
{
	struct subchannel *sch = container_of (dev, struct subchannel, dev);
	struct css_driver *driver = container_of (drv, struct css_driver, drv);

	if (sch->st == driver->subchannel_type)
		return 1;

	return 0;
}

struct bus_type css_bus_type = {
	.name  = "css",
	.match = &css_bus_match,
};

subsys_initcall(init_channel_subsystem);

/*
 * Register root devices for some drivers. The release function must not be
 * in the device drivers, so we do it here.
 */
static void
s390_root_dev_release(struct device *dev)
{
	kfree(dev);
}

struct device *
s390_root_dev_register(const char *name)
{
	struct device *dev;
	int ret;

	if (!strlen(name))
		return ERR_PTR(-EINVAL);
	dev = kmalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);
	memset(dev, 0, sizeof(struct device));
	strncpy(dev->bus_id, name, min(strlen(name), (size_t)BUS_ID_SIZE));
	dev->release = s390_root_dev_release;
	ret = device_register(dev);
	if (ret) {
		kfree(dev);
		return ERR_PTR(ret);
	}
	return dev;
}

void
s390_root_dev_unregister(struct device *dev)
{
	if (dev)
		device_unregister(dev);
}

int
css_enqueue_subchannel_slow(unsigned long schid)
{
	struct slow_subchannel *new_slow_sch;
	unsigned long flags;

	new_slow_sch = kmalloc(sizeof(struct slow_subchannel), GFP_ATOMIC);
	if (!new_slow_sch)
		return -ENOMEM;
	memset(new_slow_sch, 0, sizeof(struct slow_subchannel));
	new_slow_sch->schid = schid;
	spin_lock_irqsave(&slow_subchannel_lock, flags);
	list_add_tail(&new_slow_sch->slow_list, &slow_subchannels_head);
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
	return 0;
}

void
css_clear_subchannel_slow_list(void)
{
	unsigned long flags;

	spin_lock_irqsave(&slow_subchannel_lock, flags);
	while (!list_empty(&slow_subchannels_head)) {
		struct slow_subchannel *slow_sch =
			list_entry(slow_subchannels_head.next,
				   struct slow_subchannel, slow_list);

		list_del_init(slow_subchannels_head.next);
		kfree(slow_sch);
	}
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
}



int
css_slow_subchannels_exist(void)
{
	return (!list_empty(&slow_subchannels_head));
}

MODULE_LICENSE("GPL");
EXPORT_SYMBOL(css_bus_type);
EXPORT_SYMBOL(s390_root_dev_register);
EXPORT_SYMBOL(s390_root_dev_unregister);
EXPORT_SYMBOL_GPL(css_characteristics_avail);
