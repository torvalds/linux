// SPDX-License-Identifier: GPL-2.0
/*
 *    Copyright IBM Corp. 1999, 2010
 *    Author(s): Cornelia Huck (cornelia.huck@de.ibm.com)
 *		 Arnd Bergmann (arndb@de.ibm.com)
 *		 Peter Oberparleiter <peter.oberparleiter@de.ibm.com>
 */

#include <linux/bug.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <asm/chpid.h>
#include <asm/sclp.h>
#include <asm/crw.h>

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
static DEFINE_SPINLOCK(cfg_lock);

/* Map for channel-path status. */
static struct sclp_chp_info chp_info;
static DEFINE_MUTEX(info_lock);

/* Time after which channel-path status may be outdated. */
static unsigned long chp_info_expires;

static struct work_struct cfg_work;

/* Wait queue for configure completion events. */
static wait_queue_head_t cfg_wait_queue;

/* Set vary state for given chpid. */
static void set_chp_logically_online(struct chp_id chpid, int onoff)
{
	chpid_to_chp(chpid)->state = onoff;
}

/* On success return 0 if channel-path is varied offline, 1 if it is varied
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
EXPORT_SYMBOL_GPL(chp_get_sch_opm);

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
	if (!on && !status)
		return 0;

	set_chp_logically_online(chpid, on);
	chsc_chp_vary(chpid, on);
	return 0;
}

/*
 * Channel measurement related functions
 */
static ssize_t chp_measurement_chars_read(struct file *filp,
					  struct kobject *kobj,
					  struct bin_attribute *bin_attr,
					  char *buf, loff_t off, size_t count)
{
	struct channel_path *chp;
	struct device *device;

	device = container_of(kobj, struct device, kobj);
	chp = to_channelpath(device);
	if (chp->cmg == -1)
		return 0;

	return memory_read_from_buffer(buf, count, &off, &chp->cmg_chars,
				       sizeof(chp->cmg_chars));
}

static const struct bin_attribute chp_measurement_chars_attr = {
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

static ssize_t chp_measurement_read(struct file *filp, struct kobject *kobj,
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

static const struct bin_attribute chp_measurement_attr = {
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
	struct channel_path *chp = to_channelpath(dev);
	int status;

	mutex_lock(&chp->lock);
	status = chp->state;
	mutex_unlock(&chp->lock);

	return status ? sprintf(buf, "online\n") : sprintf(buf, "offline\n");
}

static ssize_t chp_status_write(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct channel_path *cp = to_channelpath(dev);
	char cmd[10];
	int num_args;
	int error;

	num_args = sscanf(buf, "%5s", cmd);
	if (!num_args)
		return count;

	if (!strncasecmp(cmd, "on", 2) || !strcmp(cmd, "1")) {
		mutex_lock(&cp->lock);
		error = s390_vary_chpid(cp->chpid, 1);
		mutex_unlock(&cp->lock);
	} else if (!strncasecmp(cmd, "off", 3) || !strcmp(cmd, "0")) {
		mutex_lock(&cp->lock);
		error = s390_vary_chpid(cp->chpid, 0);
		mutex_unlock(&cp->lock);
	} else
		error = -EINVAL;

	return error < 0 ? error : count;
}

static DEVICE_ATTR(status, 0644, chp_status_show, chp_status_write);

static ssize_t chp_configure_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct channel_path *cp;
	int status;

	cp = to_channelpath(dev);
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
	cp = to_channelpath(dev);
	chp_cfg_schedule(cp->chpid, val);
	cfg_wait_idle();

	return count;
}

static DEVICE_ATTR(configure, 0644, chp_configure_show, chp_configure_write);

static ssize_t chp_type_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct channel_path *chp = to_channelpath(dev);
	u8 type;

	mutex_lock(&chp->lock);
	type = chp->desc.desc;
	mutex_unlock(&chp->lock);
	return sprintf(buf, "%x\n", type);
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

static ssize_t chp_chid_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct channel_path *chp = to_channelpath(dev);
	ssize_t rc;

	mutex_lock(&chp->lock);
	if (chp->desc_fmt1.flags & 0x10)
		rc = sprintf(buf, "%04x\n", chp->desc_fmt1.chid);
	else
		rc = 0;
	mutex_unlock(&chp->lock);

	return rc;
}
static DEVICE_ATTR(chid, 0444, chp_chid_show, NULL);

static ssize_t chp_chid_external_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct channel_path *chp = to_channelpath(dev);
	ssize_t rc;

	mutex_lock(&chp->lock);
	if (chp->desc_fmt1.flags & 0x10)
		rc = sprintf(buf, "%x\n", chp->desc_fmt1.flags & 0x8 ? 1 : 0);
	else
		rc = 0;
	mutex_unlock(&chp->lock);

	return rc;
}
static DEVICE_ATTR(chid_external, 0444, chp_chid_external_show, NULL);

static struct attribute *chp_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_configure.attr,
	&dev_attr_type.attr,
	&dev_attr_cmg.attr,
	&dev_attr_shared.attr,
	&dev_attr_chid.attr,
	&dev_attr_chid_external.attr,
	NULL,
};
static struct attribute_group chp_attr_group = {
	.attrs = chp_attrs,
};
static const struct attribute_group *chp_attr_groups[] = {
	&chp_attr_group,
	NULL,
};

static void chp_release(struct device *dev)
{
	struct channel_path *cp;

	cp = to_channelpath(dev);
	kfree(cp);
}

/**
 * chp_update_desc - update channel-path description
 * @chp: channel-path
 *
 * Update the channel-path description of the specified channel-path
 * including channel measurement related information.
 * Return zero on success, non-zero otherwise.
 */
int chp_update_desc(struct channel_path *chp)
{
	int rc;

	rc = chsc_determine_fmt0_channel_path_desc(chp->chpid, &chp->desc);
	if (rc)
		return rc;

	/*
	 * Fetching the following data is optional. Not all machines or
	 * hypervisors implement the required chsc commands.
	 */
	chsc_determine_fmt1_channel_path_desc(chp->chpid, &chp->desc_fmt1);
	chsc_get_channel_measurement_chars(chp);

	return 0;
}

/**
 * chp_new - register a new channel-path
 * @chpid: channel-path ID
 *
 * Create and register data structure representing new channel-path. Return
 * zero on success, non-zero otherwise.
 */
int chp_new(struct chp_id chpid)
{
	struct channel_subsystem *css = css_by_id(chpid.cssid);
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
	chp->dev.parent = &css->device;
	chp->dev.groups = chp_attr_groups;
	chp->dev.release = chp_release;
	mutex_init(&chp->lock);

	/* Obtain channel path description and fill it in. */
	ret = chp_update_desc(chp);
	if (ret)
		goto out_free;
	if ((chp->desc.flags & 0x80) == 0) {
		ret = -ENODEV;
		goto out_free;
	}
	dev_set_name(&chp->dev, "chp%x.%02x", chpid.cssid, chpid.id);

	/* make it known to the system */
	ret = device_register(&chp->dev);
	if (ret) {
		CIO_MSG_EVENT(0, "Could not register chp%x.%02x: %d\n",
			      chpid.cssid, chpid.id, ret);
		put_device(&chp->dev);
		goto out;
	}
	mutex_lock(&css->mutex);
	if (css->cm_enabled) {
		ret = chp_add_cmg_attr(chp);
		if (ret) {
			device_unregister(&chp->dev);
			mutex_unlock(&css->mutex);
			goto out;
		}
	}
	css->chps[chpid.id] = chp;
	mutex_unlock(&css->mutex);
	goto out;
out_free:
	kfree(chp);
out:
	return ret;
}

/**
 * chp_get_chp_desc - return newly allocated channel-path description
 * @chpid: channel-path ID
 *
 * On success return a newly allocated copy of the channel-path description
 * data associated with the given channel-path ID. Return %NULL on error.
 */
struct channel_path_desc_fmt0 *chp_get_chp_desc(struct chp_id chpid)
{
	struct channel_path *chp;
	struct channel_path_desc_fmt0 *desc;

	chp = chpid_to_chp(chpid);
	if (!chp)
		return NULL;
	desc = kmalloc(sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return NULL;

	mutex_lock(&chp->lock);
	memcpy(desc, &chp->desc, sizeof(*desc));
	mutex_unlock(&chp->lock);
	return desc;
}

/**
 * chp_process_crw - process channel-path status change
 * @crw0: channel report-word to handler
 * @crw1: second channel-report word (always NULL)
 * @overflow: crw overflow indication
 *
 * Handle channel-report-words indicating that the status of a channel-path
 * has changed.
 */
static void chp_process_crw(struct crw *crw0, struct crw *crw1,
			    int overflow)
{
	struct chp_id chpid;

	if (overflow) {
		css_schedule_eval_all();
		return;
	}
	CIO_CRW_EVENT(2, "CRW reports slct=%d, oflw=%d, "
		      "chn=%d, rsc=%X, anc=%d, erc=%X, rsid=%X\n",
		      crw0->slct, crw0->oflw, crw0->chn, crw0->rsc, crw0->anc,
		      crw0->erc, crw0->rsid);
	/*
	 * Check for solicited machine checks. These are
	 * created by reset channel path and need not be
	 * handled here.
	 */
	if (crw0->slct) {
		CIO_CRW_EVENT(2, "solicited machine check for "
			      "channel path %02X\n", crw0->rsid);
		return;
	}
	chp_id_init(&chpid);
	chpid.id = crw0->rsid;
	switch (crw0->erc) {
	case CRW_ERC_IPARM: /* Path has come. */
	case CRW_ERC_INIT:
		if (!chp_is_registered(chpid))
			chp_new(chpid);
		chsc_chp_online(chpid);
		break;
	case CRW_ERC_PERRI: /* Path has gone. */
	case CRW_ERC_PERRN:
		chsc_chp_offline(chpid);
		break;
	default:
		CIO_CRW_EVENT(2, "Don't know how to handle erc=%x\n",
			      crw0->erc);
	}
}

int chp_ssd_get_mask(struct chsc_ssd_info *ssd, struct chp_link *link)
{
	int i;
	int mask;

	for (i = 0; i < 8; i++) {
		mask = 0x80 >> i;
		if (!(ssd->path_mask & mask))
			continue;
		if (!chp_id_is_equal(&ssd->chpid[i], &link->chpid))
			continue;
		if ((ssd->fla_valid_mask & mask) &&
		    ((ssd->fla[i] & link->fla_mask) != link->fla))
			continue;
		return mask;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(chp_ssd_get_mask);

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

/* Fetch the first configure task. Set chpid accordingly. */
static enum cfg_task_t chp_cfg_fetch_task(struct chp_id *chpid)
{
	enum cfg_task_t t = cfg_none;

	chp_id_for_each(chpid) {
		t = cfg_get_task(*chpid);
		if (t != cfg_none)
			break;
	}

	return t;
}

/* Perform one configure/deconfigure request. Reschedule work function until
 * last request. */
static void cfg_func(struct work_struct *work)
{
	struct chp_id chpid;
	enum cfg_task_t t;
	int rc;

	spin_lock(&cfg_lock);
	t = chp_cfg_fetch_task(&chpid);
	spin_unlock(&cfg_lock);

	switch (t) {
	case cfg_configure:
		rc = sclp_chp_configure(chpid);
		if (rc)
			CIO_MSG_EVENT(2, "chp: sclp_chp_configure(%x.%02x)="
				      "%d\n", chpid.cssid, chpid.id, rc);
		else {
			info_expire();
			chsc_chp_online(chpid);
		}
		break;
	case cfg_deconfigure:
		rc = sclp_chp_deconfigure(chpid);
		if (rc)
			CIO_MSG_EVENT(2, "chp: sclp_chp_deconfigure(%x.%02x)="
				      "%d\n", chpid.cssid, chpid.id, rc);
		else {
			info_expire();
			chsc_chp_offline(chpid);
		}
		break;
	case cfg_none:
		/* Get updated information after last change. */
		info_update();
		wake_up_interruptible(&cfg_wait_queue);
		return;
	}
	spin_lock(&cfg_lock);
	if (t == cfg_get_task(chpid))
		cfg_set_task(chpid, cfg_none);
	spin_unlock(&cfg_lock);
	schedule_work(&cfg_work);
}

/**
 * chp_cfg_schedule - schedule chpid configuration request
 * @chpid: channel-path ID
 * @configure: Non-zero for configure, zero for deconfigure
 *
 * Schedule a channel-path configuration/deconfiguration request.
 */
void chp_cfg_schedule(struct chp_id chpid, int configure)
{
	CIO_MSG_EVENT(2, "chp_cfg_sched%x.%02x=%d\n", chpid.cssid, chpid.id,
		      configure);
	spin_lock(&cfg_lock);
	cfg_set_task(chpid, configure ? cfg_configure : cfg_deconfigure);
	spin_unlock(&cfg_lock);
	schedule_work(&cfg_work);
}

/**
 * chp_cfg_cancel_deconfigure - cancel chpid deconfiguration request
 * @chpid: channel-path ID
 *
 * Cancel an active channel-path deconfiguration request if it has not yet
 * been performed.
 */
void chp_cfg_cancel_deconfigure(struct chp_id chpid)
{
	CIO_MSG_EVENT(2, "chp_cfg_cancel:%x.%02x\n", chpid.cssid, chpid.id);
	spin_lock(&cfg_lock);
	if (cfg_get_task(chpid) == cfg_deconfigure)
		cfg_set_task(chpid, cfg_none);
	spin_unlock(&cfg_lock);
}

static bool cfg_idle(void)
{
	struct chp_id chpid;
	enum cfg_task_t t;

	spin_lock(&cfg_lock);
	t = chp_cfg_fetch_task(&chpid);
	spin_unlock(&cfg_lock);

	return t == cfg_none;
}

static int cfg_wait_idle(void)
{
	if (wait_event_interruptible(cfg_wait_queue, cfg_idle()))
		return -ERESTARTSYS;
	return 0;
}

static int __init chp_init(void)
{
	struct chp_id chpid;
	int state, ret;

	ret = crw_register_handler(CRW_RSC_CPATH, chp_process_crw);
	if (ret)
		return ret;
	INIT_WORK(&cfg_work, cfg_func);
	init_waitqueue_head(&cfg_wait_queue);
	if (info_update())
		return 0;
	/* Register available channel-paths. */
	chp_id_for_each(&chpid) {
		state = chp_info_get_status(chpid);
		if (state == CHP_STATUS_CONFIGURED ||
		    state == CHP_STATUS_STANDBY)
			chp_new(chpid);
	}

	return 0;
}

subsys_initcall(chp_init);
