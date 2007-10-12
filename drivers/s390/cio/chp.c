/*
 *  drivers/s390/cio/chp.c
 *
 *    Copyright IBM Corp. 1999,2007
 *    Author(s): Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/bug.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <asm/chpid.h>
#include <asm/sclp.h>

#include "cio.h"
#include "css.h"
#include "ioasm.h"
#include "cio_debug.h"
#include "chp.h"

#define to_channelpath(device) container_of(device, struct channel_path, dev)
#define CHP_INFO_UPDATE_INTERVAL	1*HZ

enum cfg_task_t {
	cfg_none,
	cfg_configure,
	cfg_deconfigure
};

/* Map for pending configure tasks. */
static enum cfg_task_t chp_cfg_task[__MAX_CSSID + 1][__MAX_CHPID + 1];
static DEFINE_MUTEX(cfg_lock);
static int cfg_busy;

/* Map for channel-path status. */
static struct sclp_chp_info chp_info;
static DEFINE_MUTEX(info_lock);

/* Time after which channel-path status may be outdated. */
static unsigned long chp_info_expires;

/* Workqueue to perform pending configure tasks. */
static struct workqueue_struct *chp_wq;
static struct work_struct cfg_work;

/* Wait queue for configure completion events. */
static wait_queue_head_t cfg_wait_queue;

/* Return channel_path struct for given chpid. */
static inline struct channel_path *chpid_to_chp(struct chp_id chpid)
{
	return channel_subsystems[chpid.cssid]->chps[chpid.id];
}

/* Set vary state for given chpid. */
static void set_chp_logically_online(struct chp_id chpid, int onoff)
{
	chpid_to_chp(chpid)->state = onoff;
}

/* On succes return 0 if channel-path is varied offline, 1 if it is varied
 * online. Return -ENODEV if channel-path is not registered. */
int chp_get_status(struct chp_id chpid)
{
	return (chpid_to_chp(chpid) ? chpid_to_chp(chpid)->state : -ENODEV);
}

/**
 * chp_get_sch_opm - return opm for subchannel
 * @sch: subchannel
 *
 * Calculate and return the operational path mask (opm) based on the chpids
 * used by the subchannel and the status of the associated channel-paths.
 */
u8 chp_get_sch_opm(struct subchannel *sch)
{
	struct chp_id chpid;
	int opm;
	int i;

	opm = 0;
	chp_id_init(&chpid);
	for (i = 0; i < 8; i++) {
		opm <<= 1;
		chpid.id = sch->schib.pmcw.chpid[i];
		if (chp_get_status(chpid) != 0)
			opm |= 1;
	}
	return opm;
}

/**
 * chp_is_registered - check if a channel-path is registered
 * @chpid: channel-path ID
 *
 * Return non-zero if a channel-path with the given chpid is registered,
 * zero otherwise.
 */
int chp_is_registered(struct chp_id chpid)
{
	return chpid_to_chp(chpid) != NULL;
}

/*
 * Function: s390_vary_chpid
 * Varies the specified chpid online or offline
 */
static int s390_vary_chpid(struct chp_id chpid, int on)
{
	char dbf_text[15];
	int status;

	sprintf(dbf_text, on?"varyon%x.%02x":"varyoff%x.%02x", chpid.cssid,
		chpid.id);
	CIO_TRACE_EVENT(2, dbf_text);

	status = chp_get_status(chpid);
	if (!on && !status) {
		printk(KERN_ERR "cio: chpid %x.%02x is already offline\n",
		       chpid.cssid, chpid.id);
		return -EINVAL;
	}

	set_chp_logically_online(chpid, on);
	chsc_chp_vary(chpid, on);
	return 0;
}

/*
 * Channel measurement related functions
 */
static ssize_t chp_measurement_chars_read(struct kobject *kobj,
					  struct bin_attribute *bin_attr,
					  char *buf, loff_t off, size_t count)
{
	struct channel_path *chp;
	struct device *device;
	unsigned int size;

	device = container_of(kobj, struct device, kobj);
	chp = to_channelpath(device);
	if (!chp->cmg_chars)
		return 0;

	size = sizeof(struct cmg_chars);

	if (off > size)
		return 0;
	if (off + count > size)
		count = size - off;
	memcpy(buf, chp->cmg_chars + off, count);
	return count;
}

static struct bin_attribute chp_measurement_chars_attr = {
	.attr = {
		.name = "measurement_chars",
		.mode = S_IRUSR,
	},
	.size = sizeof(struct cmg_chars),
	.read = chp_measurement_chars_read,
};

static void chp_measurement_copy_block(struct cmg_entry *buf,
				       struct channel_subsystem *css,
				       struct chp_id chpid)
{
	void *area;
	struct cmg_entry *entry, reference_buf;
	int idx;

	if (chpid.id < 128) {
		area = css->cub_addr1;
		idx = chpid.id;
	} else {
		area = css->cub_addr2;
		idx = chpid.id - 128;
	}
	entry = area + (idx * sizeof(struct cmg_entry));
	do {
		memcpy(buf, entry, sizeof(*entry));
		memcpy(&reference_buf, entry, sizeof(*entry));
	} while (reference_buf.values[0] != buf->values[0]);
}

static ssize_t chp_measurement_read(struct kobject *kobj,
				    struct bin_attribute *bin_attr,
				    char *buf, loff_t off, size_t count)
{
	struct channel_path *chp;
	struct channel_subsystem *css;
	struct device *device;
	unsigned int size;

	device = container_of(kobj, struct device, kobj);
	chp = to_channelpath(device);
	css = to_css(chp->dev.parent);

	size = sizeof(struct cmg_entry);

	/* Only allow single reads. */
	if (off || count < size)
		return 0;
	chp_measurement_copy_block((struct cmg_entry *)buf, css, chp->chpid);
	count = size;
	return count;
}

static struct bin_attribute chp_measurement_attr = {
	.attr = {
		.name = "measurement",
		.mode = S_IRUSR,
	},
	.size = sizeof(struct cmg_entry),
	.read = chp_measurement_read,
};

void chp_remove_cmg_attr(struct channel_path *chp)
{
	device_remove_bin_file(&chp->dev, &chp_measurement_chars_attr);
	device_remove_bin_file(&chp->dev, &chp_measurement_attr);
}

int chp_add_cmg_attr(struct channel_path *chp)
{
	int ret;

	ret = device_create_bin_file(&chp->dev, &chp_measurement_chars_attr);
	if (ret)
		return ret;
	ret = device_create_bin_file(&chp->dev, &chp_measurement_attr);
	if (ret)
		device_remove_bin_file(&chp->dev, &chp_measurement_chars_attr);
	return ret;
}

/*
 * Files for the channel path entries.
 */
static ssize_t chp_status_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct channel_path *chp = container_of(dev, struct channel_path, dev);

	if (!chp)
		return 0;
	return (chp_get_status(chp->chpid) ? sprintf(buf, "online\n") :
		sprintf(buf, "offline\n"));
}

static ssize_t chp_status_write(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct channel_path *cp = container_of(dev, struct channel_path, dev);
	char cmd[10];
	int num_args;
	int error;

	num_args = sscanf(buf, "%5s", cmd);
	if (!num_args)
		return count;

	if (!strnicmp(cmd, "on", 2) || !strcmp(cmd, "1"))
		error = s390_vary_chpid(cp->chpid, 1);
	else if (!strnicmp(cmd, "off", 3) || !strcmp(cmd, "0"))
		error = s390_vary_chpid(cp->chpid, 0);
	else
		error = -EINVAL;

	return error < 0 ? error : count;

}

static DEVICE_ATTR(status, 0644, chp_status_show, chp_status_write);

static ssize_t chp_configure_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct channel_path *cp;
	int status;

	cp = container_of(dev, struct channel_path, dev);
	status = chp_info_get_status(cp->chpid);
	if (status < 0)
		return status;

	return snprintf(buf, PAGE_SIZE, "%d\n", status);
}

static int cfg_wait_idle(void);

static ssize_t chp_configure_write(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct channel_path *cp;
	int val;
	char delim;

	if (sscanf(buf, "%d %c", &val, &delim) != 1)
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;
	cp = container_of(dev, struct channel_path, dev);
	chp_cfg_schedule(cp->chpid, val);
	cfg_wait_idle();

	return count;
}

static DEVICE_ATTR(configure, 0644, chp_configure_show, chp_configure_write);

static ssize_t chp_type_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct channel_path *chp = container_of(dev, struct channel_path, dev);

	if (!chp)
		return 0;
	return sprintf(buf, "%x\n", chp->desc.desc);
}

static DEVICE_ATTR(type, 0444, chp_type_show, NULL);

static ssize_t chp_cmg_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct channel_path *chp = to_channelpath(dev);

	if (!chp)
		return 0;
	if (chp->cmg == -1) /* channel measurements not available */
		return sprintf(buf, "unknown\n");
	return sprintf(buf, "%x\n", chp->cmg);
}

static DEVICE_ATTR(cmg, 0444, chp_cmg_show, NULL);

static ssize_t chp_shared_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct channel_path *chp = to_channelpath(dev);

	if (!chp)
		return 0;
	if (chp->shared == -1) /* channel measurements not available */
		return sprintf(buf, "unknown\n");
	return sprintf(buf, "%x\n", chp->shared);
}

static DEVICE_ATTR(shared, 0444, chp_shared_show, NULL);

static struct attribute *chp_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_configure.attr,
	&dev_attr_type.attr,
	&dev_attr_cmg.attr,
	&dev_attr_shared.attr,
	NULL,
};

static struct attribute_group chp_attr_group = {
	.attrs = chp_attrs,
};

static void chp_release(struct device *dev)
{
	struct channel_path *cp;

	cp = container_of(dev, struct channel_path, dev);
	kfree(cp);
}

/**
 * chp_new - register a new channel-path
 * @chpid - channel-path ID
 *
 * Create and register data structure representing new channel-path. Return
 * zero on success, non-zero otherwise.
 */
int chp_new(struct chp_id chpid)
{
	struct channel_path *chp;
	int ret;

	if (chp_is_registered(chpid))
		return 0;
	chp = kzalloc(sizeof(struct channel_path), GFP_KERNEL);
	if (!chp)
		return -ENOMEM;

	/* fill in status, etc. */
	chp->chpid = chpid;
	chp->state = 1;
	chp->dev.parent = &channel_subsystems[chpid.cssid]->device;
	chp->dev.release = chp_release;
	snprintf(chp->dev.bus_id, BUS_ID_SIZE, "chp%x.%02x", chpid.cssid,
		 chpid.id);

	/* Obtain channel path description and fill it in. */
	ret = chsc_determine_channel_path_description(chpid, &chp->desc);
	if (ret)
		goto out_free;
	if ((chp->desc.flags & 0x80) == 0) {
		ret = -ENODEV;
		goto out_free;
	}
	/* Get channel-measurement characteristics. */
	if (css_characteristics_avail && css_chsc_characteristics.scmc
	    && css_chsc_characteristics.secm) {
		ret = chsc_get_channel_measurement_chars(chp);
		if (ret)
			goto out_free;
	} else {
		chp->cmg = -1;
	}

	/* make it known to the system */
	ret = device_register(&chp->dev);
	if (ret) {
		CIO_MSG_EVENT(0, "Could not register chp%x.%02x: %d\n",
			      chpid.cssid, chpid.id, ret);
		goto out_free;
	}
	ret = sysfs_create_group(&chp->dev.kobj, &chp_attr_group);
	if (ret) {
		device_unregister(&chp->dev);
		goto out_free;
	}
	mutex_lock(&channel_subsystems[chpid.cssid]->mutex);
	if (channel_subsystems[chpid.cssid]->cm_enabled) {
		ret = chp_add_cmg_attr(chp);
		if (ret) {
			sysfs_remove_group(&chp->dev.kobj, &chp_attr_group);
			device_unregister(&chp->dev);
			mutex_unlock(&channel_subsystems[chpid.cssid]->mutex);
			goto out_free;
		}
	}
	channel_subsystems[chpid.cssid]->chps[chpid.id] = chp;
	mutex_unlock(&channel_subsystems[chpid.cssid]->mutex);
	return ret;
out_free:
	kfree(chp);
	return ret;
}

/**
 * chp_get_chp_desc - return newly allocated channel-path description
 * @chpid: channel-path ID
 *
 * On success return a newly allocated copy of the channel-path description
 * data associated with the given channel-path ID. Return %NULL on error.
 */
void *chp_get_chp_desc(struct chp_id chpid)
{
	struct channel_path *chp;
	struct channel_path_desc *desc;

	chp = chpid_to_chp(chpid);
	if (!chp)
		return NULL;
	desc = kmalloc(sizeof(struct channel_path_desc), GFP_KERNEL);
	if (!desc)
		return NULL;
	memcpy(desc, &chp->desc, sizeof(struct channel_path_desc));
	return desc;
}

/**
 * chp_process_crw - process channel-path status change
 * @id: channel-path ID number
 * @status: non-zero if channel-path has become available, zero otherwise
 *
 * Handle channel-report-words indicating that the status of a channel-path
 * has changed.
 */
void chp_process_crw(int id, int status)
{
	struct chp_id chpid;

	chp_id_init(&chpid);
	chpid.id = id;
	if (status) {
		if (!chp_is_registered(chpid))
			chp_new(chpid);
		chsc_chp_online(chpid);
	} else
		chsc_chp_offline(chpid);
}

static inline int info_bit_num(struct chp_id id)
{
	return id.id + id.cssid * (__MAX_CHPID + 1);
}

/* Force chp_info refresh on next call to info_validate(). */
static void info_expire(void)
{
	mutex_lock(&info_lock);
	chp_info_expires = jiffies - 1;
	mutex_unlock(&info_lock);
}

/* Ensure that chp_info is up-to-date. */
static int info_update(void)
{
	int rc;

	mutex_lock(&info_lock);
	rc = 0;
	if (time_after(jiffies, chp_info_expires)) {
		/* Data is too old, update. */
		rc = sclp_chp_read_info(&chp_info);
		chp_info_expires = jiffies + CHP_INFO_UPDATE_INTERVAL ;
	}
	mutex_unlock(&info_lock);

	return rc;
}

/**
 * chp_info_get_status - retrieve configure status of a channel-path
 * @chpid: channel-path ID
 *
 * On success, return 0 for standby, 1 for configured, 2 for reserved,
 * 3 for not recognized. Return negative error code on error.
 */
int chp_info_get_status(struct chp_id chpid)
{
	int rc;
	int bit;

	rc = info_update();
	if (rc)
		return rc;

	bit = info_bit_num(chpid);
	mutex_lock(&info_lock);
	if (!chp_test_bit(chp_info.recognized, bit))
		rc = CHP_STATUS_NOT_RECOGNIZED;
	else if (chp_test_bit(chp_info.configured, bit))
		rc = CHP_STATUS_CONFIGURED;
	else if (chp_test_bit(chp_info.standby, bit))
		rc = CHP_STATUS_STANDBY;
	else
		rc = CHP_STATUS_RESERVED;
	mutex_unlock(&info_lock);

	return rc;
}

/* Return configure task for chpid. */
static enum cfg_task_t cfg_get_task(struct chp_id chpid)
{
	return chp_cfg_task[chpid.cssid][chpid.id];
}

/* Set configure task for chpid. */
static void cfg_set_task(struct chp_id chpid, enum cfg_task_t cfg)
{
	chp_cfg_task[chpid.cssid][chpid.id] = cfg;
}

/* Perform one configure/deconfigure request. Reschedule work function until
 * last request. */
static void cfg_func(struct work_struct *work)
{
	struct chp_id chpid;
	enum cfg_task_t t;

	mutex_lock(&cfg_lock);
	t = cfg_none;
	chp_id_for_each(&chpid) {
		t = cfg_get_task(chpid);
		if (t != cfg_none) {
			cfg_set_task(chpid, cfg_none);
			break;
		}
	}
	mutex_unlock(&cfg_lock);

	switch (t) {
	case cfg_configure:
		sclp_chp_configure(chpid);
		info_expire();
		chsc_chp_online(chpid);
		break;
	case cfg_deconfigure:
		sclp_chp_deconfigure(chpid);
		info_expire();
		chsc_chp_offline(chpid);
		break;
	case cfg_none:
		/* Get updated information after last change. */
		info_update();
		mutex_lock(&cfg_lock);
		cfg_busy = 0;
		mutex_unlock(&cfg_lock);
		wake_up_interruptible(&cfg_wait_queue);
		return;
	}
	queue_work(chp_wq, &cfg_work);
}

/**
 * chp_cfg_schedule - schedule chpid configuration request
 * @chpid - channel-path ID
 * @configure - Non-zero for configure, zero for deconfigure
 *
 * Schedule a channel-path configuration/deconfiguration request.
 */
void chp_cfg_schedule(struct chp_id chpid, int configure)
{
	CIO_MSG_EVENT(2, "chp_cfg_sched%x.%02x=%d\n", chpid.cssid, chpid.id,
		      configure);
	mutex_lock(&cfg_lock);
	cfg_set_task(chpid, configure ? cfg_configure : cfg_deconfigure);
	cfg_busy = 1;
	mutex_unlock(&cfg_lock);
	queue_work(chp_wq, &cfg_work);
}

/**
 * chp_cfg_cancel_deconfigure - cancel chpid deconfiguration request
 * @chpid - channel-path ID
 *
 * Cancel an active channel-path deconfiguration request if it has not yet
 * been performed.
 */
void chp_cfg_cancel_deconfigure(struct chp_id chpid)
{
	CIO_MSG_EVENT(2, "chp_cfg_cancel:%x.%02x\n", chpid.cssid, chpid.id);
	mutex_lock(&cfg_lock);
	if (cfg_get_task(chpid) == cfg_deconfigure)
		cfg_set_task(chpid, cfg_none);
	mutex_unlock(&cfg_lock);
}

static int cfg_wait_idle(void)
{
	if (wait_event_interruptible(cfg_wait_queue, !cfg_busy))
		return -ERESTARTSYS;
	return 0;
}

static int __init chp_init(void)
{
	struct chp_id chpid;

	chp_wq = create_singlethread_workqueue("cio_chp");
	if (!chp_wq)
		return -ENOMEM;
	INIT_WORK(&cfg_work, cfg_func);
	init_waitqueue_head(&cfg_wait_queue);
	if (info_update())
		return 0;
	/* Register available channel-paths. */
	chp_id_for_each(&chpid) {
		if (chp_info_get_status(chpid) != CHP_STATUS_NOT_RECOGNIZED)
			chp_new(chpid);
	}

	return 0;
}

subsys_initcall(chp_init);
