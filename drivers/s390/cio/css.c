/*
 *  drivers/s390/cio/css.c
 *  driver for channel subsystem
 *
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH,
 *			 IBM Corporation
 *    Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *		 Cornelia Huck (cornelia.huck@de.ibm.com)
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
#include "device.h"
#include "idset.h"
#include "chp.h"

int css_init_done = 0;
static int need_reprobe = 0;
static int max_ssid = 0;

struct channel_subsystem *css[__MAX_CSSID + 1];

int css_characteristics_avail = 0;

int
for_each_subchannel(int(*fn)(struct subchannel_id, void *), void *data)
{
	struct subchannel_id schid;
	int ret;

	init_subchannel_id(&schid);
	ret = -ENODEV;
	do {
		do {
			ret = fn(schid, data);
			if (ret)
				break;
		} while (schid.sch_no++ < __MAX_SUBCHANNEL);
		schid.sch_no = 0;
	} while (schid.ssid++ < max_ssid);
	return ret;
}

static struct subchannel *
css_alloc_subchannel(struct subchannel_id schid)
{
	struct subchannel *sch;
	int ret;

	sch = kmalloc (sizeof (*sch), GFP_KERNEL | GFP_DMA);
	if (sch == NULL)
		return ERR_PTR(-ENOMEM);
	ret = cio_validate_subchannel (sch, schid);
	if (ret < 0) {
		kfree(sch);
		return ERR_PTR(ret);
	}

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
		kfree(sch->lock);
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
		kfree(sch->lock);
		kfree(sch);
	}
}

static void
css_subchannel_release(struct device *dev)
{
	struct subchannel *sch;

	sch = to_subchannel(dev);
	if (!cio_is_console(sch->schid)) {
		kfree(sch->lock);
		kfree(sch);
	}
}

static int css_sch_device_register(struct subchannel *sch)
{
	int ret;

	mutex_lock(&sch->reg_mutex);
	ret = device_register(&sch->dev);
	mutex_unlock(&sch->reg_mutex);
	return ret;
}

void css_sch_device_unregister(struct subchannel *sch)
{
	mutex_lock(&sch->reg_mutex);
	device_unregister(&sch->dev);
	mutex_unlock(&sch->reg_mutex);
}

static void ssd_from_pmcw(struct chsc_ssd_info *ssd, struct pmcw *pmcw)
{
	int i;
	int mask;

	memset(ssd, 0, sizeof(struct chsc_ssd_info));
	ssd->path_mask = pmcw->pim;
	for (i = 0; i < 8; i++) {
		mask = 0x80 >> i;
		if (pmcw->pim & mask) {
			chp_id_init(&ssd->chpid[i]);
			ssd->chpid[i].id = pmcw->chpid[i];
		}
	}
}

static void ssd_register_chpids(struct chsc_ssd_info *ssd)
{
	int i;
	int mask;

	for (i = 0; i < 8; i++) {
		mask = 0x80 >> i;
		if (ssd->path_mask & mask)
			if (!chp_is_registered(ssd->chpid[i]))
				chp_new(ssd->chpid[i]);
	}
}

void css_update_ssd_info(struct subchannel *sch)
{
	int ret;

	if (cio_is_console(sch->schid)) {
		/* Console is initialized too early for functions requiring
		 * memory allocation. */
		ssd_from_pmcw(&sch->ssd_info, &sch->schib.pmcw);
	} else {
		ret = chsc_get_ssd_info(sch->schid, &sch->ssd_info);
		if (ret)
			ssd_from_pmcw(&sch->ssd_info, &sch->schib.pmcw);
		ssd_register_chpids(&sch->ssd_info);
	}
}

static int css_register_subchannel(struct subchannel *sch)
{
	int ret;

	/* Initialize the subchannel structure */
	sch->dev.parent = &css[0]->device;
	sch->dev.bus = &css_bus_type;
	sch->dev.release = &css_subchannel_release;
	sch->dev.groups = subch_attr_groups;
	css_update_ssd_info(sch);
	/* make it known to the system */
	ret = css_sch_device_register(sch);
	if (ret) {
		CIO_MSG_EVENT(0, "Could not register sch 0.%x.%04x: %d\n",
			      sch->schid.ssid, sch->schid.sch_no, ret);
		return ret;
	}
	return ret;
}

static int css_probe_device(struct subchannel_id schid)
{
	int ret;
	struct subchannel *sch;

	sch = css_alloc_subchannel(schid);
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
	struct subchannel_id *schid = data;

	sch = to_subchannel(dev);
	return schid_equal(&sch->schid, schid);
}

struct subchannel *
get_subchannel_by_schid(struct subchannel_id schid)
{
	struct device *dev;

	dev = bus_find_device(&css_bus_type, NULL,
			      &schid, check_subchannel);

	return dev ? to_subchannel(dev) : NULL;
}

static int css_get_subchannel_status(struct subchannel *sch)
{
	struct schib schib;

	if (stsch(sch->schid, &schib) || !schib.pmcw.dnv)
		return CIO_GONE;
	if (sch->schib.pmcw.dnv && (schib.pmcw.dev != sch->schib.pmcw.dev))
		return CIO_REVALIDATE;
	if (!sch->lpm)
		return CIO_NO_PATH;
	return CIO_OPER;
}

static int css_evaluate_known_subchannel(struct subchannel *sch, int slow)
{
	int event, ret, disc;
	unsigned long flags;
	enum { NONE, UNREGISTER, UNREGISTER_PROBE, REPROBE } action;

	spin_lock_irqsave(sch->lock, flags);
	disc = device_is_disconnected(sch);
	if (disc && slow) {
		/* Disconnected devices are evaluated directly only.*/
		spin_unlock_irqrestore(sch->lock, flags);
		return 0;
	}
	/* No interrupt after machine check - kill pending timers. */
	device_kill_pending_timer(sch);
	if (!disc && !slow) {
		/* Non-disconnected devices are evaluated on the slow path. */
		spin_unlock_irqrestore(sch->lock, flags);
		return -EAGAIN;
	}
	event = css_get_subchannel_status(sch);
	CIO_MSG_EVENT(4, "Evaluating schid 0.%x.%04x, event %d, %s, %s path.\n",
		      sch->schid.ssid, sch->schid.sch_no, event,
		      disc ? "disconnected" : "normal",
		      slow ? "slow" : "fast");
	/* Analyze subchannel status. */
	action = NONE;
	switch (event) {
	case CIO_NO_PATH:
		if (disc) {
			/* Check if paths have become available. */
			action = REPROBE;
			break;
		}
		/* fall through */
	case CIO_GONE:
		/* Prevent unwanted effects when opening lock. */
		cio_disable_subchannel(sch);
		device_set_disconnected(sch);
		/* Ask driver what to do with device. */
		action = UNREGISTER;
		if (sch->driver && sch->driver->notify) {
			spin_unlock_irqrestore(sch->lock, flags);
			ret = sch->driver->notify(&sch->dev, event);
			spin_lock_irqsave(sch->lock, flags);
			if (ret)
				action = NONE;
		}
		break;
	case CIO_REVALIDATE:
		/* Device will be removed, so no notify necessary. */
		if (disc)
			/* Reprobe because immediate unregister might block. */
			action = REPROBE;
		else
			action = UNREGISTER_PROBE;
		break;
	case CIO_OPER:
		if (disc)
			/* Get device operational again. */
			action = REPROBE;
		break;
	}
	/* Perform action. */
	ret = 0;
	switch (action) {
	case UNREGISTER:
	case UNREGISTER_PROBE:
		/* Unregister device (will use subchannel lock). */
		spin_unlock_irqrestore(sch->lock, flags);
		css_sch_device_unregister(sch);
		spin_lock_irqsave(sch->lock, flags);

		/* Reset intparm to zeroes. */
		sch->schib.pmcw.intparm = 0;
		cio_modify(sch);
		break;
	case REPROBE:
		device_trigger_reprobe(sch);
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(sch->lock, flags);
	/* Probe if necessary. */
	if (action == UNREGISTER_PROBE)
		ret = css_probe_device(sch->schid);

	return ret;
}

static int css_evaluate_new_subchannel(struct subchannel_id schid, int slow)
{
	struct schib schib;

	if (!slow) {
		/* Will be done on the slow path. */
		return -EAGAIN;
	}
	if (stsch_err(schid, &schib) || !schib.pmcw.dnv) {
		/* Unusable - ignore. */
		return 0;
	}
	CIO_MSG_EVENT(4, "Evaluating schid 0.%x.%04x, event %d, unknown, "
			 "slow path.\n", schid.ssid, schid.sch_no, CIO_OPER);

	return css_probe_device(schid);
}

static void css_evaluate_subchannel(struct subchannel_id schid, int slow)
{
	struct subchannel *sch;
	int ret;

	sch = get_subchannel_by_schid(schid);
	if (sch) {
		ret = css_evaluate_known_subchannel(sch, slow);
		put_device(&sch->dev);
	} else
		ret = css_evaluate_new_subchannel(schid, slow);
	if (ret == -EAGAIN)
		css_schedule_eval(schid);
}

static struct idset *slow_subchannel_set;
static spinlock_t slow_subchannel_lock;

static int __init slow_subchannel_init(void)
{
	spin_lock_init(&slow_subchannel_lock);
	slow_subchannel_set = idset_sch_new();
	if (!slow_subchannel_set) {
		CIO_MSG_EVENT(0, "could not allocate slow subchannel set\n");
		return -ENOMEM;
	}
	return 0;
}

static void css_slow_path_func(struct work_struct *unused)
{
	struct subchannel_id schid;

	CIO_TRACE_EVENT(4, "slowpath");
	spin_lock_irq(&slow_subchannel_lock);
	init_subchannel_id(&schid);
	while (idset_sch_get_first(slow_subchannel_set, &schid)) {
		idset_sch_del(slow_subchannel_set, schid);
		spin_unlock_irq(&slow_subchannel_lock);
		css_evaluate_subchannel(schid, 1);
		spin_lock_irq(&slow_subchannel_lock);
	}
	spin_unlock_irq(&slow_subchannel_lock);
}

static DECLARE_WORK(slow_path_work, css_slow_path_func);
struct workqueue_struct *slow_path_wq;

void css_schedule_eval(struct subchannel_id schid)
{
	unsigned long flags;

	spin_lock_irqsave(&slow_subchannel_lock, flags);
	idset_sch_add(slow_subchannel_set, schid);
	queue_work(slow_path_wq, &slow_path_work);
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
}

void css_schedule_eval_all(void)
{
	unsigned long flags;

	spin_lock_irqsave(&slow_subchannel_lock, flags);
	idset_fill(slow_subchannel_set);
	queue_work(slow_path_wq, &slow_path_work);
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
}

/* Reprobe subchannel if unregistered. */
static int reprobe_subchannel(struct subchannel_id schid, void *data)
{
	struct subchannel *sch;
	int ret;

	CIO_MSG_EVENT(6, "cio: reprobe 0.%x.%04x\n",
		      schid.ssid, schid.sch_no);
	if (need_reprobe)
		return -EAGAIN;

	sch = get_subchannel_by_schid(schid);
	if (sch) {
		/* Already known. */
		put_device(&sch->dev);
		return 0;
	}

	ret = css_probe_device(schid);
	switch (ret) {
	case 0:
		break;
	case -ENXIO:
	case -ENOMEM:
		/* These should abort looping */
		break;
	default:
		ret = 0;
	}

	return ret;
}

/* Work function used to reprobe all unregistered subchannels. */
static void reprobe_all(struct work_struct *unused)
{
	int ret;

	CIO_MSG_EVENT(2, "reprobe start\n");

	need_reprobe = 0;
	/* Make sure initial subchannel scan is done. */
	wait_event(ccw_device_init_wq,
		   atomic_read(&ccw_device_init_count) == 0);
	ret = for_each_subchannel(reprobe_subchannel, NULL);

	CIO_MSG_EVENT(2, "reprobe done (rc=%d, need_reprobe=%d)\n", ret,
		      need_reprobe);
}

static DECLARE_WORK(css_reprobe_work, reprobe_all);

/* Schedule reprobing of all unregistered subchannels. */
void css_schedule_reprobe(void)
{
	need_reprobe = 1;
	queue_work(ccw_device_work, &css_reprobe_work);
}

EXPORT_SYMBOL_GPL(css_schedule_reprobe);

/*
 * Called from the machine check handler for subchannel report words.
 */
void css_process_crw(int rsid1, int rsid2)
{
	struct subchannel_id mchk_schid;

	CIO_CRW_EVENT(2, "source is subchannel %04X, subsystem id %x\n",
		      rsid1, rsid2);
	init_subchannel_id(&mchk_schid);
	mchk_schid.sch_no = rsid1;
	if (rsid2 != 0)
		mchk_schid.ssid = (rsid2 >> 8) & 3;

	/* 
	 * Since we are always presented with IPI in the CRW, we have to
	 * use stsch() to find out if the subchannel in question has come
	 * or gone.
	 */
	css_evaluate_subchannel(mchk_schid, 0);
}

static int __init
__init_channel_subsystem(struct subchannel_id schid, void *data)
{
	struct subchannel *sch;
	int ret;

	if (cio_is_console(schid))
		sch = cio_get_console_subchannel();
	else {
		sch = css_alloc_subchannel(schid);
		if (IS_ERR(sch))
			ret = PTR_ERR(sch);
		else
			ret = 0;
		switch (ret) {
		case 0:
			break;
		case -ENOMEM:
			panic("Out of memory in init_channel_subsystem\n");
		/* -ENXIO: no more subchannels. */
		case -ENXIO:
			return ret;
		/* -EIO: this subchannel set not supported. */
		case -EIO:
			return ret;
		default:
			return 0;
		}
	}
	/*
	 * We register ALL valid subchannels in ioinfo, even those
	 * that have been present before init_channel_subsystem.
	 * These subchannels can't have been registered yet (kmalloc
	 * not working) so we do it now. This is true e.g. for the
	 * console subchannel.
	 */
	css_register_subchannel(sch);
	return 0;
}

static void __init
css_generate_pgid(struct channel_subsystem *css, u32 tod_high)
{
	if (css_characteristics_avail && css_general_characteristics.mcss) {
		css->global_pgid.pgid_high.ext_cssid.version = 0x80;
		css->global_pgid.pgid_high.ext_cssid.cssid = css->cssid;
	} else {
#ifdef CONFIG_SMP
		css->global_pgid.pgid_high.cpu_addr = hard_smp_processor_id();
#else
		css->global_pgid.pgid_high.cpu_addr = 0;
#endif
	}
	css->global_pgid.cpu_id = ((cpuid_t *) __LC_CPUID)->ident;
	css->global_pgid.cpu_model = ((cpuid_t *) __LC_CPUID)->machine;
	css->global_pgid.tod_high = tod_high;

}

static void
channel_subsystem_release(struct device *dev)
{
	struct channel_subsystem *css;

	css = to_css(dev);
	mutex_destroy(&css->mutex);
	kfree(css);
}

static ssize_t
css_cm_enable_show(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct channel_subsystem *css = to_css(dev);

	if (!css)
		return 0;
	return sprintf(buf, "%x\n", css->cm_enabled);
}

static ssize_t
css_cm_enable_store(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct channel_subsystem *css = to_css(dev);
	int ret;

	switch (buf[0]) {
	case '0':
		ret = css->cm_enabled ? chsc_secm(css, 0) : 0;
		break;
	case '1':
		ret = css->cm_enabled ? 0 : chsc_secm(css, 1);
		break;
	default:
		ret = -EINVAL;
	}
	return ret < 0 ? ret : count;
}

static DEVICE_ATTR(cm_enable, 0644, css_cm_enable_show, css_cm_enable_store);

static int __init setup_css(int nr)
{
	u32 tod_high;
	int ret;

	memset(css[nr], 0, sizeof(struct channel_subsystem));
	css[nr]->pseudo_subchannel =
		kzalloc(sizeof(*css[nr]->pseudo_subchannel), GFP_KERNEL);
	if (!css[nr]->pseudo_subchannel)
		return -ENOMEM;
	css[nr]->pseudo_subchannel->dev.parent = &css[nr]->device;
	css[nr]->pseudo_subchannel->dev.release = css_subchannel_release;
	sprintf(css[nr]->pseudo_subchannel->dev.bus_id, "defunct");
	ret = cio_create_sch_lock(css[nr]->pseudo_subchannel);
	if (ret) {
		kfree(css[nr]->pseudo_subchannel);
		return ret;
	}
	mutex_init(&css[nr]->mutex);
	css[nr]->valid = 1;
	css[nr]->cssid = nr;
	sprintf(css[nr]->device.bus_id, "css%x", nr);
	css[nr]->device.release = channel_subsystem_release;
	tod_high = (u32) (get_clock() >> 32);
	css_generate_pgid(css[nr], tod_high);
	return 0;
}

/*
 * Now that the driver core is running, we can setup our channel subsystem.
 * The struct subchannel's are created during probing (except for the
 * static console subchannel).
 */
static int __init
init_channel_subsystem (void)
{
	int ret, i;

	ret = chsc_determine_css_characteristics();
	if (ret == -ENOMEM)
		goto out; /* No need to continue. */
	if (ret == 0)
		css_characteristics_avail = 1;

	ret = chsc_alloc_sei_area();
	if (ret)
		goto out;

	ret = slow_subchannel_init();
	if (ret)
		goto out;

	if ((ret = bus_register(&css_bus_type)))
		goto out;

	/* Try to enable MSS. */
	ret = chsc_enable_facility(CHSC_SDA_OC_MSS);
	switch (ret) {
	case 0: /* Success. */
		max_ssid = __MAX_SSID;
		break;
	case -ENOMEM:
		goto out_bus;
	default:
		max_ssid = 0;
	}
	/* Setup css structure. */
	for (i = 0; i <= __MAX_CSSID; i++) {
		css[i] = kmalloc(sizeof(struct channel_subsystem), GFP_KERNEL);
		if (!css[i]) {
			ret = -ENOMEM;
			goto out_unregister;
		}
		ret = setup_css(i);
		if (ret)
			goto out_free;
		ret = device_register(&css[i]->device);
		if (ret)
			goto out_free_all;
		if (css_characteristics_avail &&
		    css_chsc_characteristics.secm) {
			ret = device_create_file(&css[i]->device,
						 &dev_attr_cm_enable);
			if (ret)
				goto out_device;
		}
		ret = device_register(&css[i]->pseudo_subchannel->dev);
		if (ret)
			goto out_file;
	}
	css_init_done = 1;

	ctl_set_bit(6, 28);

	for_each_subchannel(__init_channel_subsystem, NULL);
	return 0;
out_file:
	device_remove_file(&css[i]->device, &dev_attr_cm_enable);
out_device:
	device_unregister(&css[i]->device);
out_free_all:
	kfree(css[i]->pseudo_subchannel->lock);
	kfree(css[i]->pseudo_subchannel);
out_free:
	kfree(css[i]);
out_unregister:
	while (i > 0) {
		i--;
		device_unregister(&css[i]->pseudo_subchannel->dev);
		if (css_characteristics_avail && css_chsc_characteristics.secm)
			device_remove_file(&css[i]->device,
					   &dev_attr_cm_enable);
		device_unregister(&css[i]->device);
	}
out_bus:
	bus_unregister(&css_bus_type);
out:
	chsc_free_sei_area();
	kfree(slow_subchannel_set);
	printk(KERN_WARNING"cio: failed to initialize css driver (%d)!\n",
	       ret);
	return ret;
}

int sch_is_pseudo_sch(struct subchannel *sch)
{
	return sch == to_css(sch->dev.parent)->pseudo_subchannel;
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

static int
css_probe (struct device *dev)
{
	struct subchannel *sch;

	sch = to_subchannel(dev);
	sch->driver = container_of (dev->driver, struct css_driver, drv);
	return (sch->driver->probe ? sch->driver->probe(sch) : 0);
}

static int
css_remove (struct device *dev)
{
	struct subchannel *sch;

	sch = to_subchannel(dev);
	return (sch->driver->remove ? sch->driver->remove(sch) : 0);
}

static void
css_shutdown (struct device *dev)
{
	struct subchannel *sch;

	sch = to_subchannel(dev);
	if (sch->driver->shutdown)
		sch->driver->shutdown(sch);
}

struct bus_type css_bus_type = {
	.name     = "css",
	.match    = css_bus_match,
	.probe    = css_probe,
	.remove   = css_remove,
	.shutdown = css_shutdown,
};

subsys_initcall(init_channel_subsystem);

MODULE_LICENSE("GPL");
EXPORT_SYMBOL(css_bus_type);
EXPORT_SYMBOL_GPL(css_characteristics_avail);
