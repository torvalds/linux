// SPDX-License-Identifier: GPL-2.0
/*
 * driver for channel subsystem
 *
 * Copyright IBM Corp. 2002, 2010
 *
 * Author(s): Arnd Bergmann (arndb@de.ibm.com)
 *	      Cornelia Huck (cornelia.huck@de.ibm.com)
 */

#define KMSG_COMPONENT "cio"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/export.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/proc_fs.h>
#include <linux/genalloc.h>
#include <linux/dma-mapping.h>
#include <asm/isc.h>
#include <asm/crw.h>

#include "css.h"
#include "cio.h"
#include "blacklist.h"
#include "cio_debug.h"
#include "ioasm.h"
#include "chsc.h"
#include "device.h"
#include "idset.h"
#include "chp.h"

int css_init_done = 0;
int max_ssid;

#define MAX_CSS_IDX 0
struct channel_subsystem *channel_subsystems[MAX_CSS_IDX + 1];
static struct bus_type css_bus_type;

int
for_each_subchannel(int(*fn)(struct subchannel_id, void *), void *data)
{
	struct subchannel_id schid;
	int ret;

	init_subchannel_id(&schid);
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

struct cb_data {
	void *data;
	struct idset *set;
	int (*fn_known_sch)(struct subchannel *, void *);
	int (*fn_unknown_sch)(struct subchannel_id, void *);
};

static int call_fn_known_sch(struct device *dev, void *data)
{
	struct subchannel *sch = to_subchannel(dev);
	struct cb_data *cb = data;
	int rc = 0;

	if (cb->set)
		idset_sch_del(cb->set, sch->schid);
	if (cb->fn_known_sch)
		rc = cb->fn_known_sch(sch, cb->data);
	return rc;
}

static int call_fn_unknown_sch(struct subchannel_id schid, void *data)
{
	struct cb_data *cb = data;
	int rc = 0;

	if (idset_sch_contains(cb->set, schid))
		rc = cb->fn_unknown_sch(schid, cb->data);
	return rc;
}

static int call_fn_all_sch(struct subchannel_id schid, void *data)
{
	struct cb_data *cb = data;
	struct subchannel *sch;
	int rc = 0;

	sch = get_subchannel_by_schid(schid);
	if (sch) {
		if (cb->fn_known_sch)
			rc = cb->fn_known_sch(sch, cb->data);
		put_device(&sch->dev);
	} else {
		if (cb->fn_unknown_sch)
			rc = cb->fn_unknown_sch(schid, cb->data);
	}

	return rc;
}

int for_each_subchannel_staged(int (*fn_known)(struct subchannel *, void *),
			       int (*fn_unknown)(struct subchannel_id,
			       void *), void *data)
{
	struct cb_data cb;
	int rc;

	cb.data = data;
	cb.fn_known_sch = fn_known;
	cb.fn_unknown_sch = fn_unknown;

	if (fn_known && !fn_unknown) {
		/* Skip idset allocation in case of known-only loop. */
		cb.set = NULL;
		return bus_for_each_dev(&css_bus_type, NULL, &cb,
					call_fn_known_sch);
	}

	cb.set = idset_sch_new();
	if (!cb.set)
		/* fall back to brute force scanning in case of oom */
		return for_each_subchannel(call_fn_all_sch, &cb);

	idset_fill(cb.set);

	/* Process registered subchannels. */
	rc = bus_for_each_dev(&css_bus_type, NULL, &cb, call_fn_known_sch);
	if (rc)
		goto out;
	/* Process unregistered subchannels. */
	if (fn_unknown)
		rc = for_each_subchannel(call_fn_unknown_sch, &cb);
out:
	idset_free(cb.set);

	return rc;
}

static void css_sch_todo(struct work_struct *work);

static int css_sch_create_locks(struct subchannel *sch)
{
	sch->lock = kmalloc(sizeof(*sch->lock), GFP_KERNEL);
	if (!sch->lock)
		return -ENOMEM;

	spin_lock_init(sch->lock);
	mutex_init(&sch->reg_mutex);

	return 0;
}

static void css_subchannel_release(struct device *dev)
{
	struct subchannel *sch = to_subchannel(dev);

	sch->config.intparm = 0;
	cio_commit_config(sch);
	kfree(sch->driver_override);
	kfree(sch->lock);
	kfree(sch);
}

static int css_validate_subchannel(struct subchannel_id schid,
				   struct schib *schib)
{
	int err;

	switch (schib->pmcw.st) {
	case SUBCHANNEL_TYPE_IO:
	case SUBCHANNEL_TYPE_MSG:
		if (!css_sch_is_valid(schib))
			err = -ENODEV;
		else if (is_blacklisted(schid.ssid, schib->pmcw.dev)) {
			CIO_MSG_EVENT(6, "Blacklisted device detected "
				      "at devno %04X, subchannel set %x\n",
				      schib->pmcw.dev, schid.ssid);
			err = -ENODEV;
		} else
			err = 0;
		break;
	default:
		err = 0;
	}
	if (err)
		goto out;

	CIO_MSG_EVENT(4, "Subchannel 0.%x.%04x reports subchannel type %04X\n",
		      schid.ssid, schid.sch_no, schib->pmcw.st);
out:
	return err;
}

struct subchannel *css_alloc_subchannel(struct subchannel_id schid,
					struct schib *schib)
{
	struct subchannel *sch;
	int ret;

	ret = css_validate_subchannel(schid, schib);
	if (ret < 0)
		return ERR_PTR(ret);

	sch = kzalloc(sizeof(*sch), GFP_KERNEL | GFP_DMA);
	if (!sch)
		return ERR_PTR(-ENOMEM);

	sch->schid = schid;
	sch->schib = *schib;
	sch->st = schib->pmcw.st;

	ret = css_sch_create_locks(sch);
	if (ret)
		goto err;

	INIT_WORK(&sch->todo_work, css_sch_todo);
	sch->dev.release = &css_subchannel_release;
	device_initialize(&sch->dev);
	/*
	 * The physical addresses of some the dma structures that can
	 * belong to a subchannel need to fit 31 bit width (e.g. ccw).
	 */
	sch->dev.coherent_dma_mask = DMA_BIT_MASK(31);
	/*
	 * But we don't have such restrictions imposed on the stuff that
	 * is handled by the streaming API.
	 */
	sch->dma_mask = DMA_BIT_MASK(64);
	sch->dev.dma_mask = &sch->dma_mask;
	return sch;

err:
	kfree(sch);
	return ERR_PTR(ret);
}

static int css_sch_device_register(struct subchannel *sch)
{
	int ret;

	mutex_lock(&sch->reg_mutex);
	dev_set_name(&sch->dev, "0.%x.%04x", sch->schid.ssid,
		     sch->schid.sch_no);
	ret = device_add(&sch->dev);
	mutex_unlock(&sch->reg_mutex);
	return ret;
}

/**
 * css_sch_device_unregister - unregister a subchannel
 * @sch: subchannel to be unregistered
 */
void css_sch_device_unregister(struct subchannel *sch)
{
	mutex_lock(&sch->reg_mutex);
	if (device_is_registered(&sch->dev))
		device_unregister(&sch->dev);
	mutex_unlock(&sch->reg_mutex);
}
EXPORT_SYMBOL_GPL(css_sch_device_unregister);

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
			chp_new(ssd->chpid[i]);
	}
}

void css_update_ssd_info(struct subchannel *sch)
{
	int ret;

	ret = chsc_get_ssd_info(sch->schid, &sch->ssd_info);
	if (ret)
		ssd_from_pmcw(&sch->ssd_info, &sch->schib.pmcw);

	ssd_register_chpids(&sch->ssd_info);
}

static ssize_t type_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct subchannel *sch = to_subchannel(dev);

	return sprintf(buf, "%01x\n", sch->st);
}

static DEVICE_ATTR_RO(type);

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct subchannel *sch = to_subchannel(dev);

	return sprintf(buf, "css:t%01X\n", sch->st);
}

static DEVICE_ATTR_RO(modalias);

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct subchannel *sch = to_subchannel(dev);
	char *driver_override, *old, *cp;

	/* We need to keep extra room for a newline */
	if (count >= (PAGE_SIZE - 1))
		return -EINVAL;

	driver_override = kstrndup(buf, count, GFP_KERNEL);
	if (!driver_override)
		return -ENOMEM;

	cp = strchr(driver_override, '\n');
	if (cp)
		*cp = '\0';

	device_lock(dev);
	old = sch->driver_override;
	if (strlen(driver_override)) {
		sch->driver_override = driver_override;
	} else {
		kfree(driver_override);
		sch->driver_override = NULL;
	}
	device_unlock(dev);

	kfree(old);

	return count;
}

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct subchannel *sch = to_subchannel(dev);
	ssize_t len;

	device_lock(dev);
	len = snprintf(buf, PAGE_SIZE, "%s\n", sch->driver_override);
	device_unlock(dev);
	return len;
}
static DEVICE_ATTR_RW(driver_override);

static struct attribute *subch_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_modalias.attr,
	&dev_attr_driver_override.attr,
	NULL,
};

static struct attribute_group subch_attr_group = {
	.attrs = subch_attrs,
};

static const struct attribute_group *default_subch_attr_groups[] = {
	&subch_attr_group,
	NULL,
};

static ssize_t chpids_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct subchannel *sch = to_subchannel(dev);
	struct chsc_ssd_info *ssd = &sch->ssd_info;
	ssize_t ret = 0;
	int mask;
	int chp;

	for (chp = 0; chp < 8; chp++) {
		mask = 0x80 >> chp;
		if (ssd->path_mask & mask)
			ret += sprintf(buf + ret, "%02x ", ssd->chpid[chp].id);
		else
			ret += sprintf(buf + ret, "00 ");
	}
	ret += sprintf(buf + ret, "\n");
	return ret;
}
static DEVICE_ATTR_RO(chpids);

static ssize_t pimpampom_show(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct subchannel *sch = to_subchannel(dev);
	struct pmcw *pmcw = &sch->schib.pmcw;

	return sprintf(buf, "%02x %02x %02x\n",
		       pmcw->pim, pmcw->pam, pmcw->pom);
}
static DEVICE_ATTR_RO(pimpampom);

static struct attribute *io_subchannel_type_attrs[] = {
	&dev_attr_chpids.attr,
	&dev_attr_pimpampom.attr,
	NULL,
};
ATTRIBUTE_GROUPS(io_subchannel_type);

static const struct device_type io_subchannel_type = {
	.groups = io_subchannel_type_groups,
};

int css_register_subchannel(struct subchannel *sch)
{
	int ret;

	/* Initialize the subchannel structure */
	sch->dev.parent = &channel_subsystems[0]->device;
	sch->dev.bus = &css_bus_type;
	sch->dev.groups = default_subch_attr_groups;

	if (sch->st == SUBCHANNEL_TYPE_IO)
		sch->dev.type = &io_subchannel_type;

	/*
	 * We don't want to generate uevents for I/O subchannels that don't
	 * have a working ccw device behind them since they will be
	 * unregistered before they can be used anyway, so we delay the add
	 * uevent until after device recognition was successful.
	 * Note that we suppress the uevent for all subchannel types;
	 * the subchannel driver can decide itself when it wants to inform
	 * userspace of its existence.
	 */
	dev_set_uevent_suppress(&sch->dev, 1);
	css_update_ssd_info(sch);
	/* make it known to the system */
	ret = css_sch_device_register(sch);
	if (ret) {
		CIO_MSG_EVENT(0, "Could not register sch 0.%x.%04x: %d\n",
			      sch->schid.ssid, sch->schid.sch_no, ret);
		return ret;
	}
	if (!sch->driver) {
		/*
		 * No driver matched. Generate the uevent now so that
		 * a fitting driver module may be loaded based on the
		 * modalias.
		 */
		dev_set_uevent_suppress(&sch->dev, 0);
		kobject_uevent(&sch->dev.kobj, KOBJ_ADD);
	}
	return ret;
}

static int css_probe_device(struct subchannel_id schid, struct schib *schib)
{
	struct subchannel *sch;
	int ret;

	sch = css_alloc_subchannel(schid, schib);
	if (IS_ERR(sch))
		return PTR_ERR(sch);

	ret = css_register_subchannel(sch);
	if (ret)
		put_device(&sch->dev);

	return ret;
}

static int
check_subchannel(struct device *dev, const void *data)
{
	struct subchannel *sch;
	struct subchannel_id *schid = (void *)data;

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

/**
 * css_sch_is_valid() - check if a subchannel is valid
 * @schib: subchannel information block for the subchannel
 */
int css_sch_is_valid(struct schib *schib)
{
	if ((schib->pmcw.st == SUBCHANNEL_TYPE_IO) && !schib->pmcw.dnv)
		return 0;
	if ((schib->pmcw.st == SUBCHANNEL_TYPE_MSG) && !schib->pmcw.w)
		return 0;
	return 1;
}
EXPORT_SYMBOL_GPL(css_sch_is_valid);

static int css_evaluate_new_subchannel(struct subchannel_id schid, int slow)
{
	struct schib schib;
	int ccode;

	if (!slow) {
		/* Will be done on the slow path. */
		return -EAGAIN;
	}
	/*
	 * The first subchannel that is not-operational (ccode==3)
	 * indicates that there aren't any more devices available.
	 * If stsch gets an exception, it means the current subchannel set
	 * is not valid.
	 */
	ccode = stsch(schid, &schib);
	if (ccode)
		return (ccode == 3) ? -ENXIO : ccode;

	return css_probe_device(schid, &schib);
}

static int css_evaluate_known_subchannel(struct subchannel *sch, int slow)
{
	int ret = 0;

	if (sch->driver) {
		if (sch->driver->sch_event)
			ret = sch->driver->sch_event(sch, slow);
		else
			dev_dbg(&sch->dev,
				"Got subchannel machine check but "
				"no sch_event handler provided.\n");
	}
	if (ret != 0 && ret != -EAGAIN) {
		CIO_MSG_EVENT(2, "eval: sch 0.%x.%04x, rc=%d\n",
			      sch->schid.ssid, sch->schid.sch_no, ret);
	}
	return ret;
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

/**
 * css_sched_sch_todo - schedule a subchannel operation
 * @sch: subchannel
 * @todo: todo
 *
 * Schedule the operation identified by @todo to be performed on the slow path
 * workqueue. Do nothing if another operation with higher priority is already
 * scheduled. Needs to be called with subchannel lock held.
 */
void css_sched_sch_todo(struct subchannel *sch, enum sch_todo todo)
{
	CIO_MSG_EVENT(4, "sch_todo: sched sch=0.%x.%04x todo=%d\n",
		      sch->schid.ssid, sch->schid.sch_no, todo);
	if (sch->todo >= todo)
		return;
	/* Get workqueue ref. */
	if (!get_device(&sch->dev))
		return;
	sch->todo = todo;
	if (!queue_work(cio_work_q, &sch->todo_work)) {
		/* Already queued, release workqueue ref. */
		put_device(&sch->dev);
	}
}
EXPORT_SYMBOL_GPL(css_sched_sch_todo);

static void css_sch_todo(struct work_struct *work)
{
	struct subchannel *sch;
	enum sch_todo todo;
	int ret;

	sch = container_of(work, struct subchannel, todo_work);
	/* Find out todo. */
	spin_lock_irq(sch->lock);
	todo = sch->todo;
	CIO_MSG_EVENT(4, "sch_todo: sch=0.%x.%04x, todo=%d\n", sch->schid.ssid,
		      sch->schid.sch_no, todo);
	sch->todo = SCH_TODO_NOTHING;
	spin_unlock_irq(sch->lock);
	/* Perform todo. */
	switch (todo) {
	case SCH_TODO_NOTHING:
		break;
	case SCH_TODO_EVAL:
		ret = css_evaluate_known_subchannel(sch, 1);
		if (ret == -EAGAIN) {
			spin_lock_irq(sch->lock);
			css_sched_sch_todo(sch, todo);
			spin_unlock_irq(sch->lock);
		}
		break;
	case SCH_TODO_UNREG:
		css_sch_device_unregister(sch);
		break;
	}
	/* Release workqueue ref. */
	put_device(&sch->dev);
}

static struct idset *slow_subchannel_set;
static spinlock_t slow_subchannel_lock;
static wait_queue_head_t css_eval_wq;
static atomic_t css_eval_scheduled;

static int __init slow_subchannel_init(void)
{
	spin_lock_init(&slow_subchannel_lock);
	atomic_set(&css_eval_scheduled, 0);
	init_waitqueue_head(&css_eval_wq);
	slow_subchannel_set = idset_sch_new();
	if (!slow_subchannel_set) {
		CIO_MSG_EVENT(0, "could not allocate slow subchannel set\n");
		return -ENOMEM;
	}
	return 0;
}

static int slow_eval_known_fn(struct subchannel *sch, void *data)
{
	int eval;
	int rc;

	spin_lock_irq(&slow_subchannel_lock);
	eval = idset_sch_contains(slow_subchannel_set, sch->schid);
	idset_sch_del(slow_subchannel_set, sch->schid);
	spin_unlock_irq(&slow_subchannel_lock);
	if (eval) {
		rc = css_evaluate_known_subchannel(sch, 1);
		if (rc == -EAGAIN)
			css_schedule_eval(sch->schid);
		/*
		 * The loop might take long time for platforms with lots of
		 * known devices. Allow scheduling here.
		 */
		cond_resched();
	}
	return 0;
}

static int slow_eval_unknown_fn(struct subchannel_id schid, void *data)
{
	int eval;
	int rc = 0;

	spin_lock_irq(&slow_subchannel_lock);
	eval = idset_sch_contains(slow_subchannel_set, schid);
	idset_sch_del(slow_subchannel_set, schid);
	spin_unlock_irq(&slow_subchannel_lock);
	if (eval) {
		rc = css_evaluate_new_subchannel(schid, 1);
		switch (rc) {
		case -EAGAIN:
			css_schedule_eval(schid);
			rc = 0;
			break;
		case -ENXIO:
		case -ENOMEM:
		case -EIO:
			/* These should abort looping */
			spin_lock_irq(&slow_subchannel_lock);
			idset_sch_del_subseq(slow_subchannel_set, schid);
			spin_unlock_irq(&slow_subchannel_lock);
			break;
		default:
			rc = 0;
		}
		/* Allow scheduling here since the containing loop might
		 * take a while.  */
		cond_resched();
	}
	return rc;
}

static void css_slow_path_func(struct work_struct *unused)
{
	unsigned long flags;

	CIO_TRACE_EVENT(4, "slowpath");
	for_each_subchannel_staged(slow_eval_known_fn, slow_eval_unknown_fn,
				   NULL);
	spin_lock_irqsave(&slow_subchannel_lock, flags);
	if (idset_is_empty(slow_subchannel_set)) {
		atomic_set(&css_eval_scheduled, 0);
		wake_up(&css_eval_wq);
	}
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
}

static DECLARE_DELAYED_WORK(slow_path_work, css_slow_path_func);
struct workqueue_struct *cio_work_q;

void css_schedule_eval(struct subchannel_id schid)
{
	unsigned long flags;

	spin_lock_irqsave(&slow_subchannel_lock, flags);
	idset_sch_add(slow_subchannel_set, schid);
	atomic_set(&css_eval_scheduled, 1);
	queue_delayed_work(cio_work_q, &slow_path_work, 0);
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
}

void css_schedule_eval_all(void)
{
	unsigned long flags;

	spin_lock_irqsave(&slow_subchannel_lock, flags);
	idset_fill(slow_subchannel_set);
	atomic_set(&css_eval_scheduled, 1);
	queue_delayed_work(cio_work_q, &slow_path_work, 0);
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
}

static int __unset_registered(struct device *dev, void *data)
{
	struct idset *set = data;
	struct subchannel *sch = to_subchannel(dev);

	idset_sch_del(set, sch->schid);
	return 0;
}

void css_schedule_eval_all_unreg(unsigned long delay)
{
	unsigned long flags;
	struct idset *unreg_set;

	/* Find unregistered subchannels. */
	unreg_set = idset_sch_new();
	if (!unreg_set) {
		/* Fallback. */
		css_schedule_eval_all();
		return;
	}
	idset_fill(unreg_set);
	bus_for_each_dev(&css_bus_type, NULL, unreg_set, __unset_registered);
	/* Apply to slow_subchannel_set. */
	spin_lock_irqsave(&slow_subchannel_lock, flags);
	idset_add_set(slow_subchannel_set, unreg_set);
	atomic_set(&css_eval_scheduled, 1);
	queue_delayed_work(cio_work_q, &slow_path_work, delay);
	spin_unlock_irqrestore(&slow_subchannel_lock, flags);
	idset_free(unreg_set);
}

void css_wait_for_slow_path(void)
{
	flush_workqueue(cio_work_q);
}

/* Schedule reprobing of all unregistered subchannels. */
void css_schedule_reprobe(void)
{
	/* Schedule with a delay to allow merging of subsequent calls. */
	css_schedule_eval_all_unreg(1 * HZ);
}
EXPORT_SYMBOL_GPL(css_schedule_reprobe);

/*
 * Called from the machine check handler for subchannel report words.
 */
static void css_process_crw(struct crw *crw0, struct crw *crw1, int overflow)
{
	struct subchannel_id mchk_schid;
	struct subchannel *sch;

	if (overflow) {
		css_schedule_eval_all();
		return;
	}
	CIO_CRW_EVENT(2, "CRW0 reports slct=%d, oflw=%d, "
		      "chn=%d, rsc=%X, anc=%d, erc=%X, rsid=%X\n",
		      crw0->slct, crw0->oflw, crw0->chn, crw0->rsc, crw0->anc,
		      crw0->erc, crw0->rsid);
	if (crw1)
		CIO_CRW_EVENT(2, "CRW1 reports slct=%d, oflw=%d, "
			      "chn=%d, rsc=%X, anc=%d, erc=%X, rsid=%X\n",
			      crw1->slct, crw1->oflw, crw1->chn, crw1->rsc,
			      crw1->anc, crw1->erc, crw1->rsid);
	init_subchannel_id(&mchk_schid);
	mchk_schid.sch_no = crw0->rsid;
	if (crw1)
		mchk_schid.ssid = (crw1->rsid >> 4) & 3;

	if (crw0->erc == CRW_ERC_PMOD) {
		sch = get_subchannel_by_schid(mchk_schid);
		if (sch) {
			css_update_ssd_info(sch);
			put_device(&sch->dev);
		}
	}
	/*
	 * Since we are always presented with IPI in the CRW, we have to
	 * use stsch() to find out if the subchannel in question has come
	 * or gone.
	 */
	css_evaluate_subchannel(mchk_schid, 0);
}

static void __init
css_generate_pgid(struct channel_subsystem *css, u32 tod_high)
{
	struct cpuid cpu_id;

	if (css_general_characteristics.mcss) {
		css->global_pgid.pgid_high.ext_cssid.version = 0x80;
		css->global_pgid.pgid_high.ext_cssid.cssid =
			css->id_valid ? css->cssid : 0;
	} else {
		css->global_pgid.pgid_high.cpu_addr = stap();
	}
	get_cpu_id(&cpu_id);
	css->global_pgid.cpu_id = cpu_id.ident;
	css->global_pgid.cpu_model = cpu_id.machine;
	css->global_pgid.tod_high = tod_high;
}

static void channel_subsystem_release(struct device *dev)
{
	struct channel_subsystem *css = to_css(dev);

	mutex_destroy(&css->mutex);
	kfree(css);
}

static ssize_t real_cssid_show(struct device *dev, struct device_attribute *a,
			       char *buf)
{
	struct channel_subsystem *css = to_css(dev);

	if (!css->id_valid)
		return -EINVAL;

	return sprintf(buf, "%x\n", css->cssid);
}
static DEVICE_ATTR_RO(real_cssid);

static ssize_t cm_enable_show(struct device *dev, struct device_attribute *a,
			      char *buf)
{
	struct channel_subsystem *css = to_css(dev);
	int ret;

	mutex_lock(&css->mutex);
	ret = sprintf(buf, "%x\n", css->cm_enabled);
	mutex_unlock(&css->mutex);
	return ret;
}

static ssize_t cm_enable_store(struct device *dev, struct device_attribute *a,
			       const char *buf, size_t count)
{
	struct channel_subsystem *css = to_css(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;
	mutex_lock(&css->mutex);
	switch (val) {
	case 0:
		ret = css->cm_enabled ? chsc_secm(css, 0) : 0;
		break;
	case 1:
		ret = css->cm_enabled ? 0 : chsc_secm(css, 1);
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&css->mutex);
	return ret < 0 ? ret : count;
}
static DEVICE_ATTR_RW(cm_enable);

static umode_t cm_enable_mode(struct kobject *kobj, struct attribute *attr,
			      int index)
{
	return css_chsc_characteristics.secm ? attr->mode : 0;
}

static struct attribute *cssdev_attrs[] = {
	&dev_attr_real_cssid.attr,
	NULL,
};

static struct attribute_group cssdev_attr_group = {
	.attrs = cssdev_attrs,
};

static struct attribute *cssdev_cm_attrs[] = {
	&dev_attr_cm_enable.attr,
	NULL,
};

static struct attribute_group cssdev_cm_attr_group = {
	.attrs = cssdev_cm_attrs,
	.is_visible = cm_enable_mode,
};

static const struct attribute_group *cssdev_attr_groups[] = {
	&cssdev_attr_group,
	&cssdev_cm_attr_group,
	NULL,
};

static int __init setup_css(int nr)
{
	struct channel_subsystem *css;
	int ret;

	css = kzalloc(sizeof(*css), GFP_KERNEL);
	if (!css)
		return -ENOMEM;

	channel_subsystems[nr] = css;
	dev_set_name(&css->device, "css%x", nr);
	css->device.groups = cssdev_attr_groups;
	css->device.release = channel_subsystem_release;
	/*
	 * We currently allocate notifier bits with this (using
	 * css->device as the device argument with the DMA API)
	 * and are fine with 64 bit addresses.
	 */
	css->device.coherent_dma_mask = DMA_BIT_MASK(64);
	css->device.dma_mask = &css->device.coherent_dma_mask;

	mutex_init(&css->mutex);
	ret = chsc_get_cssid_iid(nr, &css->cssid, &css->iid);
	if (!ret) {
		css->id_valid = true;
		pr_info("Partition identifier %01x.%01x\n", css->cssid,
			css->iid);
	}
	css_generate_pgid(css, (u32) (get_tod_clock() >> 32));

	ret = device_register(&css->device);
	if (ret) {
		put_device(&css->device);
		goto out_err;
	}

	css->pseudo_subchannel = kzalloc(sizeof(*css->pseudo_subchannel),
					 GFP_KERNEL);
	if (!css->pseudo_subchannel) {
		device_unregister(&css->device);
		ret = -ENOMEM;
		goto out_err;
	}

	css->pseudo_subchannel->dev.parent = &css->device;
	css->pseudo_subchannel->dev.release = css_subchannel_release;
	mutex_init(&css->pseudo_subchannel->reg_mutex);
	ret = css_sch_create_locks(css->pseudo_subchannel);
	if (ret) {
		kfree(css->pseudo_subchannel);
		device_unregister(&css->device);
		goto out_err;
	}

	dev_set_name(&css->pseudo_subchannel->dev, "defunct");
	ret = device_register(&css->pseudo_subchannel->dev);
	if (ret) {
		put_device(&css->pseudo_subchannel->dev);
		device_unregister(&css->device);
		goto out_err;
	}

	return ret;
out_err:
	channel_subsystems[nr] = NULL;
	return ret;
}

static int css_reboot_event(struct notifier_block *this,
			    unsigned long event,
			    void *ptr)
{
	struct channel_subsystem *css;
	int ret;

	ret = NOTIFY_DONE;
	for_each_css(css) {
		mutex_lock(&css->mutex);
		if (css->cm_enabled)
			if (chsc_secm(css, 0))
				ret = NOTIFY_BAD;
		mutex_unlock(&css->mutex);
	}

	return ret;
}

static struct notifier_block css_reboot_notifier = {
	.notifier_call = css_reboot_event,
};

/*
 * Since the css devices are neither on a bus nor have a class
 * nor have a special device type, we cannot stop/restart channel
 * path measurements via the normal suspend/resume callbacks, but have
 * to use notifiers.
 */
static int css_power_event(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct channel_subsystem *css;
	int ret;

	switch (event) {
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		ret = NOTIFY_DONE;
		for_each_css(css) {
			mutex_lock(&css->mutex);
			if (!css->cm_enabled) {
				mutex_unlock(&css->mutex);
				continue;
			}
			ret = __chsc_do_secm(css, 0);
			ret = notifier_from_errno(ret);
			mutex_unlock(&css->mutex);
		}
		break;
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		ret = NOTIFY_DONE;
		for_each_css(css) {
			mutex_lock(&css->mutex);
			if (!css->cm_enabled) {
				mutex_unlock(&css->mutex);
				continue;
			}
			ret = __chsc_do_secm(css, 1);
			ret = notifier_from_errno(ret);
			mutex_unlock(&css->mutex);
		}
		/* search for subchannels, which appeared during hibernation */
		css_schedule_reprobe();
		break;
	default:
		ret = NOTIFY_DONE;
	}
	return ret;

}
static struct notifier_block css_power_notifier = {
	.notifier_call = css_power_event,
};

#define  CIO_DMA_GFP (GFP_KERNEL | __GFP_ZERO)
static struct gen_pool *cio_dma_pool;

/* Currently cio supports only a single css */
struct device *cio_get_dma_css_dev(void)
{
	return &channel_subsystems[0]->device;
}

struct gen_pool *cio_gp_dma_create(struct device *dma_dev, int nr_pages)
{
	struct gen_pool *gp_dma;
	void *cpu_addr;
	dma_addr_t dma_addr;
	int i;

	gp_dma = gen_pool_create(3, -1);
	if (!gp_dma)
		return NULL;
	for (i = 0; i < nr_pages; ++i) {
		cpu_addr = dma_alloc_coherent(dma_dev, PAGE_SIZE, &dma_addr,
					      CIO_DMA_GFP);
		if (!cpu_addr)
			return gp_dma;
		gen_pool_add_virt(gp_dma, (unsigned long) cpu_addr,
				  dma_addr, PAGE_SIZE, -1);
	}
	return gp_dma;
}

static void __gp_dma_free_dma(struct gen_pool *pool,
			      struct gen_pool_chunk *chunk, void *data)
{
	size_t chunk_size = chunk->end_addr - chunk->start_addr + 1;

	dma_free_coherent((struct device *) data, chunk_size,
			 (void *) chunk->start_addr,
			 (dma_addr_t) chunk->phys_addr);
}

void cio_gp_dma_destroy(struct gen_pool *gp_dma, struct device *dma_dev)
{
	if (!gp_dma)
		return;
	/* this is quite ugly but no better idea */
	gen_pool_for_each_chunk(gp_dma, __gp_dma_free_dma, dma_dev);
	gen_pool_destroy(gp_dma);
}

static int cio_dma_pool_init(void)
{
	/* No need to free up the resources: compiled in */
	cio_dma_pool = cio_gp_dma_create(cio_get_dma_css_dev(), 1);
	if (!cio_dma_pool)
		return -ENOMEM;
	return 0;
}

void *cio_gp_dma_zalloc(struct gen_pool *gp_dma, struct device *dma_dev,
			size_t size)
{
	dma_addr_t dma_addr;
	unsigned long addr;
	size_t chunk_size;

	if (!gp_dma)
		return NULL;
	addr = gen_pool_alloc(gp_dma, size);
	while (!addr) {
		chunk_size = round_up(size, PAGE_SIZE);
		addr = (unsigned long) dma_alloc_coherent(dma_dev,
					 chunk_size, &dma_addr, CIO_DMA_GFP);
		if (!addr)
			return NULL;
		gen_pool_add_virt(gp_dma, addr, dma_addr, chunk_size, -1);
		addr = gen_pool_alloc(gp_dma, size);
	}
	return (void *) addr;
}

void cio_gp_dma_free(struct gen_pool *gp_dma, void *cpu_addr, size_t size)
{
	if (!cpu_addr)
		return;
	memset(cpu_addr, 0, size);
	gen_pool_free(gp_dma, (unsigned long) cpu_addr, size);
}

/*
 * Allocate dma memory from the css global pool. Intended for memory not
 * specific to any single device within the css. The allocated memory
 * is not guaranteed to be 31-bit addressable.
 *
 * Caution: Not suitable for early stuff like console.
 */
void *cio_dma_zalloc(size_t size)
{
	return cio_gp_dma_zalloc(cio_dma_pool, cio_get_dma_css_dev(), size);
}

void cio_dma_free(void *cpu_addr, size_t size)
{
	cio_gp_dma_free(cio_dma_pool, cpu_addr, size);
}

/*
 * Now that the driver core is running, we can setup our channel subsystem.
 * The struct subchannel's are created during probing.
 */
static int __init css_bus_init(void)
{
	int ret, i;

	ret = chsc_init();
	if (ret)
		return ret;

	chsc_determine_css_characteristics();
	/* Try to enable MSS. */
	ret = chsc_enable_facility(CHSC_SDA_OC_MSS);
	if (ret)
		max_ssid = 0;
	else /* Success. */
		max_ssid = __MAX_SSID;

	ret = slow_subchannel_init();
	if (ret)
		goto out;

	ret = crw_register_handler(CRW_RSC_SCH, css_process_crw);
	if (ret)
		goto out;

	if ((ret = bus_register(&css_bus_type)))
		goto out;

	/* Setup css structure. */
	for (i = 0; i <= MAX_CSS_IDX; i++) {
		ret = setup_css(i);
		if (ret)
			goto out_unregister;
	}
	ret = register_reboot_notifier(&css_reboot_notifier);
	if (ret)
		goto out_unregister;
	ret = register_pm_notifier(&css_power_notifier);
	if (ret)
		goto out_unregister_rn;
	ret = cio_dma_pool_init();
	if (ret)
		goto out_unregister_pmn;
	airq_init();
	css_init_done = 1;

	/* Enable default isc for I/O subchannels. */
	isc_register(IO_SCH_ISC);

	return 0;
out_unregister_pmn:
	unregister_pm_notifier(&css_power_notifier);
out_unregister_rn:
	unregister_reboot_notifier(&css_reboot_notifier);
out_unregister:
	while (i-- > 0) {
		struct channel_subsystem *css = channel_subsystems[i];
		device_unregister(&css->pseudo_subchannel->dev);
		device_unregister(&css->device);
	}
	bus_unregister(&css_bus_type);
out:
	crw_unregister_handler(CRW_RSC_SCH);
	idset_free(slow_subchannel_set);
	chsc_init_cleanup();
	pr_alert("The CSS device driver initialization failed with "
		 "errno=%d\n", ret);
	return ret;
}

static void __init css_bus_cleanup(void)
{
	struct channel_subsystem *css;

	for_each_css(css) {
		device_unregister(&css->pseudo_subchannel->dev);
		device_unregister(&css->device);
	}
	bus_unregister(&css_bus_type);
	crw_unregister_handler(CRW_RSC_SCH);
	idset_free(slow_subchannel_set);
	chsc_init_cleanup();
	isc_unregister(IO_SCH_ISC);
}

static int __init channel_subsystem_init(void)
{
	int ret;

	ret = css_bus_init();
	if (ret)
		return ret;
	cio_work_q = create_singlethread_workqueue("cio");
	if (!cio_work_q) {
		ret = -ENOMEM;
		goto out_bus;
	}
	ret = io_subchannel_init();
	if (ret)
		goto out_wq;

	/* Register subchannels which are already in use. */
	cio_register_early_subchannels();
	/* Start initial subchannel evaluation. */
	css_schedule_eval_all();

	return ret;
out_wq:
	destroy_workqueue(cio_work_q);
out_bus:
	css_bus_cleanup();
	return ret;
}
subsys_initcall(channel_subsystem_init);

static int css_settle(struct device_driver *drv, void *unused)
{
	struct css_driver *cssdrv = to_cssdriver(drv);

	if (cssdrv->settle)
		return cssdrv->settle();
	return 0;
}

int css_complete_work(void)
{
	int ret;

	/* Wait for the evaluation of subchannels to finish. */
	ret = wait_event_interruptible(css_eval_wq,
				       atomic_read(&css_eval_scheduled) == 0);
	if (ret)
		return -EINTR;
	flush_workqueue(cio_work_q);
	/* Wait for the subchannel type specific initialization to finish */
	return bus_for_each_drv(&css_bus_type, NULL, NULL, css_settle);
}


/*
 * Wait for the initialization of devices to finish, to make sure we are
 * done with our setup if the search for the root device starts.
 */
static int __init channel_subsystem_init_sync(void)
{
	css_complete_work();
	return 0;
}
subsys_initcall_sync(channel_subsystem_init_sync);

void channel_subsystem_reinit(void)
{
	struct channel_path *chp;
	struct chp_id chpid;

	chsc_enable_facility(CHSC_SDA_OC_MSS);
	chp_id_for_each(&chpid) {
		chp = chpid_to_chp(chpid);
		if (chp)
			chp_update_desc(chp);
	}
	cmf_reactivate();
}

#ifdef CONFIG_PROC_FS
static ssize_t cio_settle_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	int ret;

	/* Handle pending CRW's. */
	crw_wait_for_channel_report();
	ret = css_complete_work();

	return ret ? ret : count;
}

static const struct proc_ops cio_settle_proc_ops = {
	.proc_open	= nonseekable_open,
	.proc_write	= cio_settle_write,
	.proc_lseek	= no_llseek,
};

static int __init cio_settle_init(void)
{
	struct proc_dir_entry *entry;

	entry = proc_create("cio_settle", S_IWUSR, NULL, &cio_settle_proc_ops);
	if (!entry)
		return -ENOMEM;
	return 0;
}
device_initcall(cio_settle_init);
#endif /*CONFIG_PROC_FS*/

int sch_is_pseudo_sch(struct subchannel *sch)
{
	if (!sch->dev.parent)
		return 0;
	return sch == to_css(sch->dev.parent)->pseudo_subchannel;
}

static int css_bus_match(struct device *dev, struct device_driver *drv)
{
	struct subchannel *sch = to_subchannel(dev);
	struct css_driver *driver = to_cssdriver(drv);
	struct css_device_id *id;

	/* When driver_override is set, only bind to the matching driver */
	if (sch->driver_override && strcmp(sch->driver_override, drv->name))
		return 0;

	for (id = driver->subchannel_type; id->match_flags; id++) {
		if (sch->st == id->type)
			return 1;
	}

	return 0;
}

static int css_probe(struct device *dev)
{
	struct subchannel *sch;
	int ret;

	sch = to_subchannel(dev);
	sch->driver = to_cssdriver(dev->driver);
	ret = sch->driver->probe ? sch->driver->probe(sch) : 0;
	if (ret)
		sch->driver = NULL;
	return ret;
}

static int css_remove(struct device *dev)
{
	struct subchannel *sch;
	int ret;

	sch = to_subchannel(dev);
	ret = sch->driver->remove ? sch->driver->remove(sch) : 0;
	sch->driver = NULL;
	return ret;
}

static void css_shutdown(struct device *dev)
{
	struct subchannel *sch;

	sch = to_subchannel(dev);
	if (sch->driver && sch->driver->shutdown)
		sch->driver->shutdown(sch);
}

static int css_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct subchannel *sch = to_subchannel(dev);
	int ret;

	ret = add_uevent_var(env, "ST=%01X", sch->st);
	if (ret)
		return ret;
	ret = add_uevent_var(env, "MODALIAS=css:t%01X", sch->st);
	return ret;
}

static int css_pm_prepare(struct device *dev)
{
	struct subchannel *sch = to_subchannel(dev);
	struct css_driver *drv;

	if (mutex_is_locked(&sch->reg_mutex))
		return -EAGAIN;
	if (!sch->dev.driver)
		return 0;
	drv = to_cssdriver(sch->dev.driver);
	/* Notify drivers that they may not register children. */
	return drv->prepare ? drv->prepare(sch) : 0;
}

static void css_pm_complete(struct device *dev)
{
	struct subchannel *sch = to_subchannel(dev);
	struct css_driver *drv;

	if (!sch->dev.driver)
		return;
	drv = to_cssdriver(sch->dev.driver);
	if (drv->complete)
		drv->complete(sch);
}

static int css_pm_freeze(struct device *dev)
{
	struct subchannel *sch = to_subchannel(dev);
	struct css_driver *drv;

	if (!sch->dev.driver)
		return 0;
	drv = to_cssdriver(sch->dev.driver);
	return drv->freeze ? drv->freeze(sch) : 0;
}

static int css_pm_thaw(struct device *dev)
{
	struct subchannel *sch = to_subchannel(dev);
	struct css_driver *drv;

	if (!sch->dev.driver)
		return 0;
	drv = to_cssdriver(sch->dev.driver);
	return drv->thaw ? drv->thaw(sch) : 0;
}

static int css_pm_restore(struct device *dev)
{
	struct subchannel *sch = to_subchannel(dev);
	struct css_driver *drv;

	css_update_ssd_info(sch);
	if (!sch->dev.driver)
		return 0;
	drv = to_cssdriver(sch->dev.driver);
	return drv->restore ? drv->restore(sch) : 0;
}

static const struct dev_pm_ops css_pm_ops = {
	.prepare = css_pm_prepare,
	.complete = css_pm_complete,
	.freeze = css_pm_freeze,
	.thaw = css_pm_thaw,
	.restore = css_pm_restore,
};

static struct bus_type css_bus_type = {
	.name     = "css",
	.match    = css_bus_match,
	.probe    = css_probe,
	.remove   = css_remove,
	.shutdown = css_shutdown,
	.uevent   = css_uevent,
	.pm = &css_pm_ops,
};

/**
 * css_driver_register - register a css driver
 * @cdrv: css driver to register
 *
 * This is mainly a wrapper around driver_register that sets name
 * and bus_type in the embedded struct device_driver correctly.
 */
int css_driver_register(struct css_driver *cdrv)
{
	cdrv->drv.bus = &css_bus_type;
	return driver_register(&cdrv->drv);
}
EXPORT_SYMBOL_GPL(css_driver_register);

/**
 * css_driver_unregister - unregister a css driver
 * @cdrv: css driver to unregister
 *
 * This is a wrapper around driver_unregister.
 */
void css_driver_unregister(struct css_driver *cdrv)
{
	driver_unregister(&cdrv->drv);
}
EXPORT_SYMBOL_GPL(css_driver_unregister);
