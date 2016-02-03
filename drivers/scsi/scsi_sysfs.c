/*
 * scsi_sysfs.c
 *
 * SCSI sysfs interface routines.
 *
 * Created to pull SCSI mid layer sysfs routines into one file.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_dh.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_driver.h>

#include "scsi_priv.h"
#include "scsi_logging.h"

static struct device_type scsi_dev_type;

static const struct {
	enum scsi_device_state	value;
	char			*name;
} sdev_states[] = {
	{ SDEV_CREATED, "created" },
	{ SDEV_RUNNING, "running" },
	{ SDEV_CANCEL, "cancel" },
	{ SDEV_DEL, "deleted" },
	{ SDEV_QUIESCE, "quiesce" },
	{ SDEV_OFFLINE,	"offline" },
	{ SDEV_TRANSPORT_OFFLINE, "transport-offline" },
	{ SDEV_BLOCK,	"blocked" },
	{ SDEV_CREATED_BLOCK, "created-blocked" },
};

const char *scsi_device_state_name(enum scsi_device_state state)
{
	int i;
	char *name = NULL;

	for (i = 0; i < ARRAY_SIZE(sdev_states); i++) {
		if (sdev_states[i].value == state) {
			name = sdev_states[i].name;
			break;
		}
	}
	return name;
}

static const struct {
	enum scsi_host_state	value;
	char			*name;
} shost_states[] = {
	{ SHOST_CREATED, "created" },
	{ SHOST_RUNNING, "running" },
	{ SHOST_CANCEL, "cancel" },
	{ SHOST_DEL, "deleted" },
	{ SHOST_RECOVERY, "recovery" },
	{ SHOST_CANCEL_RECOVERY, "cancel/recovery" },
	{ SHOST_DEL_RECOVERY, "deleted/recovery", },
};
const char *scsi_host_state_name(enum scsi_host_state state)
{
	int i;
	char *name = NULL;

	for (i = 0; i < ARRAY_SIZE(shost_states); i++) {
		if (shost_states[i].value == state) {
			name = shost_states[i].name;
			break;
		}
	}
	return name;
}

static int check_set(unsigned long long *val, char *src)
{
	char *last;

	if (strncmp(src, "-", 20) == 0) {
		*val = SCAN_WILD_CARD;
	} else {
		/*
		 * Doesn't check for int overflow
		 */
		*val = simple_strtoull(src, &last, 0);
		if (*last != '\0')
			return 1;
	}
	return 0;
}

static int scsi_scan(struct Scsi_Host *shost, const char *str)
{
	char s1[15], s2[15], s3[17], junk;
	unsigned long long channel, id, lun;
	int res;

	res = sscanf(str, "%10s %10s %16s %c", s1, s2, s3, &junk);
	if (res != 3)
		return -EINVAL;
	if (check_set(&channel, s1))
		return -EINVAL;
	if (check_set(&id, s2))
		return -EINVAL;
	if (check_set(&lun, s3))
		return -EINVAL;
	if (shost->transportt->user_scan)
		res = shost->transportt->user_scan(shost, channel, id, lun);
	else
		res = scsi_scan_host_selected(shost, channel, id, lun, 1);
	return res;
}

/*
 * shost_show_function: macro to create an attr function that can be used to
 * show a non-bit field.
 */
#define shost_show_function(name, field, format_string)			\
static ssize_t								\
show_##name (struct device *dev, struct device_attribute *attr, 	\
	     char *buf)							\
{									\
	struct Scsi_Host *shost = class_to_shost(dev);			\
	return snprintf (buf, 20, format_string, shost->field);		\
}

/*
 * shost_rd_attr: macro to create a function and attribute variable for a
 * read only field.
 */
#define shost_rd_attr2(name, field, format_string)			\
	shost_show_function(name, field, format_string)			\
static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL);

#define shost_rd_attr(field, format_string) \
shost_rd_attr2(field, field, format_string)

/*
 * Create the actual show/store functions and data structures.
 */

static ssize_t
store_scan(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	int res;

	res = scsi_scan(shost, buf);
	if (res == 0)
		res = count;
	return res;
};
static DEVICE_ATTR(scan, S_IWUSR, NULL, store_scan);

static ssize_t
store_shost_state(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	int i;
	struct Scsi_Host *shost = class_to_shost(dev);
	enum scsi_host_state state = 0;

	for (i = 0; i < ARRAY_SIZE(shost_states); i++) {
		const int len = strlen(shost_states[i].name);
		if (strncmp(shost_states[i].name, buf, len) == 0 &&
		   buf[len] == '\n') {
			state = shost_states[i].value;
			break;
		}
	}
	if (!state)
		return -EINVAL;

	if (scsi_host_set_state(shost, state))
		return -EINVAL;
	return count;
}

static ssize_t
show_shost_state(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	const char *name = scsi_host_state_name(shost->shost_state);

	if (!name)
		return -EINVAL;

	return snprintf(buf, 20, "%s\n", name);
}

/* DEVICE_ATTR(state) clashes with dev_attr_state for sdev */
struct device_attribute dev_attr_hstate =
	__ATTR(state, S_IRUGO | S_IWUSR, show_shost_state, store_shost_state);

static ssize_t
show_shost_mode(unsigned int mode, char *buf)
{
	ssize_t len = 0;

	if (mode & MODE_INITIATOR)
		len = sprintf(buf, "%s", "Initiator");

	if (mode & MODE_TARGET)
		len += sprintf(buf + len, "%s%s", len ? ", " : "", "Target");

	len += sprintf(buf + len, "\n");

	return len;
}

static ssize_t
show_shost_supported_mode(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	unsigned int supported_mode = shost->hostt->supported_mode;

	if (supported_mode == MODE_UNKNOWN)
		/* by default this should be initiator */
		supported_mode = MODE_INITIATOR;

	return show_shost_mode(supported_mode, buf);
}

static DEVICE_ATTR(supported_mode, S_IRUGO | S_IWUSR, show_shost_supported_mode, NULL);

static ssize_t
show_shost_active_mode(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);

	if (shost->active_mode == MODE_UNKNOWN)
		return snprintf(buf, 20, "unknown\n");
	else
		return show_shost_mode(shost->active_mode, buf);
}

static DEVICE_ATTR(active_mode, S_IRUGO | S_IWUSR, show_shost_active_mode, NULL);

static int check_reset_type(const char *str)
{
	if (sysfs_streq(str, "adapter"))
		return SCSI_ADAPTER_RESET;
	else if (sysfs_streq(str, "firmware"))
		return SCSI_FIRMWARE_RESET;
	else
		return 0;
}

static ssize_t
store_host_reset(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	struct scsi_host_template *sht = shost->hostt;
	int ret = -EINVAL;
	int type;

	type = check_reset_type(buf);
	if (!type)
		goto exit_store_host_reset;

	if (sht->host_reset)
		ret = sht->host_reset(shost, type);

exit_store_host_reset:
	if (ret == 0)
		ret = count;
	return ret;
}

static DEVICE_ATTR(host_reset, S_IWUSR, NULL, store_host_reset);

static ssize_t
show_shost_eh_deadline(struct device *dev,
		      struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);

	if (shost->eh_deadline == -1)
		return snprintf(buf, strlen("off") + 2, "off\n");
	return sprintf(buf, "%u\n", shost->eh_deadline / HZ);
}

static ssize_t
store_shost_eh_deadline(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	int ret = -EINVAL;
	unsigned long deadline, flags;

	if (shost->transportt &&
	    (shost->transportt->eh_strategy_handler ||
	     !shost->hostt->eh_host_reset_handler))
		return ret;

	if (!strncmp(buf, "off", strlen("off")))
		deadline = -1;
	else {
		ret = kstrtoul(buf, 10, &deadline);
		if (ret)
			return ret;
		if (deadline * HZ > UINT_MAX)
			return -EINVAL;
	}

	spin_lock_irqsave(shost->host_lock, flags);
	if (scsi_host_in_recovery(shost))
		ret = -EBUSY;
	else {
		if (deadline == -1)
			shost->eh_deadline = -1;
		else
			shost->eh_deadline = deadline * HZ;

		ret = count;
	}
	spin_unlock_irqrestore(shost->host_lock, flags);

	return ret;
}

static DEVICE_ATTR(eh_deadline, S_IRUGO | S_IWUSR, show_shost_eh_deadline, store_shost_eh_deadline);

shost_rd_attr(use_blk_mq, "%d\n");
shost_rd_attr(unique_id, "%u\n");
shost_rd_attr(cmd_per_lun, "%hd\n");
shost_rd_attr(can_queue, "%hd\n");
shost_rd_attr(sg_tablesize, "%hu\n");
shost_rd_attr(sg_prot_tablesize, "%hu\n");
shost_rd_attr(unchecked_isa_dma, "%d\n");
shost_rd_attr(prot_capabilities, "%u\n");
shost_rd_attr(prot_guard_type, "%hd\n");
shost_rd_attr2(proc_name, hostt->proc_name, "%s\n");

static ssize_t
show_host_busy(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct Scsi_Host *shost = class_to_shost(dev);
	return snprintf(buf, 20, "%d\n", atomic_read(&shost->host_busy));
}
static DEVICE_ATTR(host_busy, S_IRUGO, show_host_busy, NULL);

static struct attribute *scsi_sysfs_shost_attrs[] = {
	&dev_attr_use_blk_mq.attr,
	&dev_attr_unique_id.attr,
	&dev_attr_host_busy.attr,
	&dev_attr_cmd_per_lun.attr,
	&dev_attr_can_queue.attr,
	&dev_attr_sg_tablesize.attr,
	&dev_attr_sg_prot_tablesize.attr,
	&dev_attr_unchecked_isa_dma.attr,
	&dev_attr_proc_name.attr,
	&dev_attr_scan.attr,
	&dev_attr_hstate.attr,
	&dev_attr_supported_mode.attr,
	&dev_attr_active_mode.attr,
	&dev_attr_prot_capabilities.attr,
	&dev_attr_prot_guard_type.attr,
	&dev_attr_host_reset.attr,
	&dev_attr_eh_deadline.attr,
	NULL
};

struct attribute_group scsi_shost_attr_group = {
	.attrs =	scsi_sysfs_shost_attrs,
};

const struct attribute_group *scsi_sysfs_shost_attr_groups[] = {
	&scsi_shost_attr_group,
	NULL
};

static void scsi_device_cls_release(struct device *class_dev)
{
	struct scsi_device *sdev;

	sdev = class_to_sdev(class_dev);
	put_device(&sdev->sdev_gendev);
}

static void scsi_device_dev_release_usercontext(struct work_struct *work)
{
	struct scsi_device *sdev;
	struct device *parent;
	struct list_head *this, *tmp;
	unsigned long flags;

	sdev = container_of(work, struct scsi_device, ew.work);

	scsi_dh_release_device(sdev);

	parent = sdev->sdev_gendev.parent;

	spin_lock_irqsave(sdev->host->host_lock, flags);
	list_del(&sdev->siblings);
	list_del(&sdev->same_target_siblings);
	list_del(&sdev->starved_entry);
	spin_unlock_irqrestore(sdev->host->host_lock, flags);

	cancel_work_sync(&sdev->event_work);

	list_for_each_safe(this, tmp, &sdev->event_list) {
		struct scsi_event *evt;

		evt = list_entry(this, struct scsi_event, node);
		list_del(&evt->node);
		kfree(evt);
	}

	blk_put_queue(sdev->request_queue);
	/* NULL queue means the device can't be used */
	sdev->request_queue = NULL;

	kfree(sdev->vpd_pg83);
	kfree(sdev->vpd_pg80);
	kfree(sdev->inquiry);
	kfree(sdev);

	if (parent)
		put_device(parent);
}

static void scsi_device_dev_release(struct device *dev)
{
	struct scsi_device *sdp = to_scsi_device(dev);
	execute_in_process_context(scsi_device_dev_release_usercontext,
				   &sdp->ew);
}

static struct class sdev_class = {
	.name		= "scsi_device",
	.dev_release	= scsi_device_cls_release,
};

/* all probing is done in the individual ->probe routines */
static int scsi_bus_match(struct device *dev, struct device_driver *gendrv)
{
	struct scsi_device *sdp;

	if (dev->type != &scsi_dev_type)
		return 0;

	sdp = to_scsi_device(dev);
	if (sdp->no_uld_attach)
		return 0;
	return (sdp->inq_periph_qual == SCSI_INQ_PQ_CON)? 1: 0;
}

static int scsi_bus_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct scsi_device *sdev;

	if (dev->type != &scsi_dev_type)
		return 0;

	sdev = to_scsi_device(dev);

	add_uevent_var(env, "MODALIAS=" SCSI_DEVICE_MODALIAS_FMT, sdev->type);
	return 0;
}

struct bus_type scsi_bus_type = {
        .name		= "scsi",
        .match		= scsi_bus_match,
	.uevent		= scsi_bus_uevent,
#ifdef CONFIG_PM
	.pm		= &scsi_bus_pm_ops,
#endif
};
EXPORT_SYMBOL_GPL(scsi_bus_type);

int scsi_sysfs_register(void)
{
	int error;

	error = bus_register(&scsi_bus_type);
	if (!error) {
		error = class_register(&sdev_class);
		if (error)
			bus_unregister(&scsi_bus_type);
	}

	return error;
}

void scsi_sysfs_unregister(void)
{
	class_unregister(&sdev_class);
	bus_unregister(&scsi_bus_type);
}

/*
 * sdev_show_function: macro to create an attr function that can be used to
 * show a non-bit field.
 */
#define sdev_show_function(field, format_string)				\
static ssize_t								\
sdev_show_##field (struct device *dev, struct device_attribute *attr,	\
		   char *buf)						\
{									\
	struct scsi_device *sdev;					\
	sdev = to_scsi_device(dev);					\
	return snprintf (buf, 20, format_string, sdev->field);		\
}									\

/*
 * sdev_rd_attr: macro to create a function and attribute variable for a
 * read only field.
 */
#define sdev_rd_attr(field, format_string)				\
	sdev_show_function(field, format_string)			\
static DEVICE_ATTR(field, S_IRUGO, sdev_show_##field, NULL);


/*
 * sdev_rw_attr: create a function and attribute variable for a
 * read/write field.
 */
#define sdev_rw_attr(field, format_string)				\
	sdev_show_function(field, format_string)				\
									\
static ssize_t								\
sdev_store_##field (struct device *dev, struct device_attribute *attr,	\
		    const char *buf, size_t count)			\
{									\
	struct scsi_device *sdev;					\
	sdev = to_scsi_device(dev);					\
	sscanf (buf, format_string, &sdev->field);			\
	return count;							\
}									\
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, sdev_show_##field, sdev_store_##field);

/* Currently we don't export bit fields, but we might in future,
 * so leave this code in */
#if 0
/*
 * sdev_rd_attr: create a function and attribute variable for a
 * read/write bit field.
 */
#define sdev_rw_attr_bit(field)						\
	sdev_show_function(field, "%d\n")					\
									\
static ssize_t								\
sdev_store_##field (struct device *dev, struct device_attribute *attr,	\
		    const char *buf, size_t count)			\
{									\
	int ret;							\
	struct scsi_device *sdev;					\
	ret = scsi_sdev_check_buf_bit(buf);				\
	if (ret >= 0)	{						\
		sdev = to_scsi_device(dev);				\
		sdev->field = ret;					\
		ret = count;						\
	}								\
	return ret;							\
}									\
static DEVICE_ATTR(field, S_IRUGO | S_IWUSR, sdev_show_##field, sdev_store_##field);

/*
 * scsi_sdev_check_buf_bit: return 0 if buf is "0", return 1 if buf is "1",
 * else return -EINVAL.
 */
static int scsi_sdev_check_buf_bit(const char *buf)
{
	if ((buf[1] == '\0') || ((buf[1] == '\n') && (buf[2] == '\0'))) {
		if (buf[0] == '1')
			return 1;
		else if (buf[0] == '0')
			return 0;
		else 
			return -EINVAL;
	} else
		return -EINVAL;
}
#endif
/*
 * Create the actual show/store functions and data structures.
 */
sdev_rd_attr (type, "%d\n");
sdev_rd_attr (scsi_level, "%d\n");
sdev_rd_attr (vendor, "%.8s\n");
sdev_rd_attr (model, "%.16s\n");
sdev_rd_attr (rev, "%.4s\n");

static ssize_t
sdev_show_device_busy(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	return snprintf(buf, 20, "%d\n", atomic_read(&sdev->device_busy));
}
static DEVICE_ATTR(device_busy, S_IRUGO, sdev_show_device_busy, NULL);

static ssize_t
sdev_show_device_blocked(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	return snprintf(buf, 20, "%d\n", atomic_read(&sdev->device_blocked));
}
static DEVICE_ATTR(device_blocked, S_IRUGO, sdev_show_device_blocked, NULL);

/*
 * TODO: can we make these symlinks to the block layer ones?
 */
static ssize_t
sdev_show_timeout (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev;
	sdev = to_scsi_device(dev);
	return snprintf(buf, 20, "%d\n", sdev->request_queue->rq_timeout / HZ);
}

static ssize_t
sdev_store_timeout (struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct scsi_device *sdev;
	int timeout;
	sdev = to_scsi_device(dev);
	sscanf (buf, "%d\n", &timeout);
	blk_queue_rq_timeout(sdev->request_queue, timeout * HZ);
	return count;
}
static DEVICE_ATTR(timeout, S_IRUGO | S_IWUSR, sdev_show_timeout, sdev_store_timeout);

static ssize_t
sdev_show_eh_timeout(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev;
	sdev = to_scsi_device(dev);
	return snprintf(buf, 20, "%u\n", sdev->eh_timeout / HZ);
}

static ssize_t
sdev_store_eh_timeout(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct scsi_device *sdev;
	unsigned int eh_timeout;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	sdev = to_scsi_device(dev);
	err = kstrtouint(buf, 10, &eh_timeout);
	if (err)
		return err;
	sdev->eh_timeout = eh_timeout * HZ;

	return count;
}
static DEVICE_ATTR(eh_timeout, S_IRUGO | S_IWUSR, sdev_show_eh_timeout, sdev_store_eh_timeout);

static ssize_t
store_rescan_field (struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	scsi_rescan_device(dev);
	return count;
}
static DEVICE_ATTR(rescan, S_IWUSR, NULL, store_rescan_field);

static ssize_t
sdev_store_delete(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	if (device_remove_file_self(dev, attr))
		scsi_remove_device(to_scsi_device(dev));
	return count;
};
static DEVICE_ATTR(delete, S_IWUSR, NULL, sdev_store_delete);

static ssize_t
store_state_field(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	int i;
	struct scsi_device *sdev = to_scsi_device(dev);
	enum scsi_device_state state = 0;

	for (i = 0; i < ARRAY_SIZE(sdev_states); i++) {
		const int len = strlen(sdev_states[i].name);
		if (strncmp(sdev_states[i].name, buf, len) == 0 &&
		   buf[len] == '\n') {
			state = sdev_states[i].value;
			break;
		}
	}
	if (!state)
		return -EINVAL;

	if (scsi_device_set_state(sdev, state))
		return -EINVAL;
	return count;
}

static ssize_t
show_state_field(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	const char *name = scsi_device_state_name(sdev->sdev_state);

	if (!name)
		return -EINVAL;

	return snprintf(buf, 20, "%s\n", name);
}

static DEVICE_ATTR(state, S_IRUGO | S_IWUSR, show_state_field, store_state_field);

static ssize_t
show_queue_type_field(struct device *dev, struct device_attribute *attr,
		      char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	const char *name = "none";

	if (sdev->simple_tags)
		name = "simple";

	return snprintf(buf, 20, "%s\n", name);
}

static ssize_t
store_queue_type_field(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	if (!sdev->tagged_supported)
		return -EINVAL;
		
	sdev_printk(KERN_INFO, sdev,
		    "ignoring write to deprecated queue_type attribute");
	return count;
}

static DEVICE_ATTR(queue_type, S_IRUGO | S_IWUSR, show_queue_type_field,
		   store_queue_type_field);

#define sdev_vpd_pg_attr(_page)						\
static ssize_t							\
show_vpd_##_page(struct file *filp, struct kobject *kobj,	\
		 struct bin_attribute *bin_attr,			\
		 char *buf, loff_t off, size_t count)			\
{									\
	struct device *dev = container_of(kobj, struct device, kobj);	\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	int ret;							\
	if (!sdev->vpd_##_page)						\
		return -EINVAL;						\
	rcu_read_lock();						\
	ret = memory_read_from_buffer(buf, count, &off,			\
				      rcu_dereference(sdev->vpd_##_page), \
				       sdev->vpd_##_page##_len);	\
	rcu_read_unlock();						\
	return ret;						\
}									\
static struct bin_attribute dev_attr_vpd_##_page = {		\
	.attr =	{.name = __stringify(vpd_##_page), .mode = S_IRUGO },	\
	.size = 0,							\
	.read = show_vpd_##_page,					\
};

sdev_vpd_pg_attr(pg83);
sdev_vpd_pg_attr(pg80);

static ssize_t show_inquiry(struct file *filep, struct kobject *kobj,
			    struct bin_attribute *bin_attr,
			    char *buf, loff_t off, size_t count)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct scsi_device *sdev = to_scsi_device(dev);

	if (!sdev->inquiry)
		return -EINVAL;

	return memory_read_from_buffer(buf, count, &off, sdev->inquiry,
				       sdev->inquiry_len);
}

static struct bin_attribute dev_attr_inquiry = {
	.attr = {
		.name = "inquiry",
		.mode = S_IRUGO,
	},
	.size = 0,
	.read = show_inquiry,
};

static ssize_t
show_iostat_counterbits(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, 20, "%d\n", (int)sizeof(atomic_t) * 8);
}

static DEVICE_ATTR(iocounterbits, S_IRUGO, show_iostat_counterbits, NULL);

#define show_sdev_iostat(field)						\
static ssize_t								\
show_iostat_##field(struct device *dev, struct device_attribute *attr,	\
		    char *buf)						\
{									\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	unsigned long long count = atomic_read(&sdev->field);		\
	return snprintf(buf, 20, "0x%llx\n", count);			\
}									\
static DEVICE_ATTR(field, S_IRUGO, show_iostat_##field, NULL)

show_sdev_iostat(iorequest_cnt);
show_sdev_iostat(iodone_cnt);
show_sdev_iostat(ioerr_cnt);

static ssize_t
sdev_show_modalias(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct scsi_device *sdev;
	sdev = to_scsi_device(dev);
	return snprintf (buf, 20, SCSI_DEVICE_MODALIAS_FMT "\n", sdev->type);
}
static DEVICE_ATTR(modalias, S_IRUGO, sdev_show_modalias, NULL);

#define DECLARE_EVT_SHOW(name, Cap_name)				\
static ssize_t								\
sdev_show_evt_##name(struct device *dev, struct device_attribute *attr,	\
		     char *buf)						\
{									\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	int val = test_bit(SDEV_EVT_##Cap_name, sdev->supported_events);\
	return snprintf(buf, 20, "%d\n", val);				\
}

#define DECLARE_EVT_STORE(name, Cap_name)				\
static ssize_t								\
sdev_store_evt_##name(struct device *dev, struct device_attribute *attr,\
		      const char *buf, size_t count)			\
{									\
	struct scsi_device *sdev = to_scsi_device(dev);			\
	int val = simple_strtoul(buf, NULL, 0);				\
	if (val == 0)							\
		clear_bit(SDEV_EVT_##Cap_name, sdev->supported_events);	\
	else if (val == 1)						\
		set_bit(SDEV_EVT_##Cap_name, sdev->supported_events);	\
	else								\
		return -EINVAL;						\
	return count;							\
}

#define DECLARE_EVT(name, Cap_name)					\
	DECLARE_EVT_SHOW(name, Cap_name)				\
	DECLARE_EVT_STORE(name, Cap_name)				\
	static DEVICE_ATTR(evt_##name, S_IRUGO, sdev_show_evt_##name,	\
			   sdev_store_evt_##name);
#define REF_EVT(name) &dev_attr_evt_##name.attr

DECLARE_EVT(media_change, MEDIA_CHANGE)
DECLARE_EVT(inquiry_change_reported, INQUIRY_CHANGE_REPORTED)
DECLARE_EVT(capacity_change_reported, CAPACITY_CHANGE_REPORTED)
DECLARE_EVT(soft_threshold_reached, SOFT_THRESHOLD_REACHED_REPORTED)
DECLARE_EVT(mode_parameter_change_reported, MODE_PARAMETER_CHANGE_REPORTED)
DECLARE_EVT(lun_change_reported, LUN_CHANGE_REPORTED)

static ssize_t
sdev_store_queue_depth(struct device *dev, struct device_attribute *attr,
		       const char *buf, size_t count)
{
	int depth, retval;
	struct scsi_device *sdev = to_scsi_device(dev);
	struct scsi_host_template *sht = sdev->host->hostt;

	if (!sht->change_queue_depth)
		return -EINVAL;

	depth = simple_strtoul(buf, NULL, 0);

	if (depth < 1 || depth > sdev->host->can_queue)
		return -EINVAL;

	retval = sht->change_queue_depth(sdev, depth);
	if (retval < 0)
		return retval;

	sdev->max_queue_depth = sdev->queue_depth;

	return count;
}
sdev_show_function(queue_depth, "%d\n");

static DEVICE_ATTR(queue_depth, S_IRUGO | S_IWUSR, sdev_show_queue_depth,
		   sdev_store_queue_depth);

static ssize_t
sdev_show_wwid(struct device *dev, struct device_attribute *attr,
		    char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	ssize_t count;

	count = scsi_vpd_lun_id(sdev, buf, PAGE_SIZE);
	if (count > 0) {
		buf[count] = '\n';
		count++;
	}
	return count;
}
static DEVICE_ATTR(wwid, S_IRUGO, sdev_show_wwid, NULL);

#ifdef CONFIG_SCSI_DH
static ssize_t
sdev_show_dh_state(struct device *dev, struct device_attribute *attr,
		   char *buf)
{
	struct scsi_device *sdev = to_scsi_device(dev);

	if (!sdev->handler)
		return snprintf(buf, 20, "detached\n");

	return snprintf(buf, 20, "%s\n", sdev->handler->name);
}

static ssize_t
sdev_store_dh_state(struct device *dev, struct device_attribute *attr,
		    const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	int err = -EINVAL;

	if (sdev->sdev_state == SDEV_CANCEL ||
	    sdev->sdev_state == SDEV_DEL)
		return -ENODEV;

	if (!sdev->handler) {
		/*
		 * Attach to a device handler
		 */
		err = scsi_dh_attach(sdev->request_queue, buf);
	} else if (!strncmp(buf, "activate", 8)) {
		/*
		 * Activate a device handler
		 */
		if (sdev->handler->activate)
			err = sdev->handler->activate(sdev, NULL, NULL);
		else
			err = 0;
	} else if (!strncmp(buf, "detach", 6)) {
		/*
		 * Detach from a device handler
		 */
		sdev_printk(KERN_WARNING, sdev,
			    "can't detach handler %s.\n",
			    sdev->handler->name);
		err = -EINVAL;
	}

	return err < 0 ? err : count;
}

static DEVICE_ATTR(dh_state, S_IRUGO | S_IWUSR, sdev_show_dh_state,
		   sdev_store_dh_state);
#endif

static ssize_t
sdev_show_queue_ramp_up_period(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct scsi_device *sdev;
	sdev = to_scsi_device(dev);
	return snprintf(buf, 20, "%u\n",
			jiffies_to_msecs(sdev->queue_ramp_up_period));
}

static ssize_t
sdev_store_queue_ramp_up_period(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	unsigned int period;

	if (kstrtouint(buf, 10, &period))
		return -EINVAL;

	sdev->queue_ramp_up_period = msecs_to_jiffies(period);
	return count;
}

static DEVICE_ATTR(queue_ramp_up_period, S_IRUGO | S_IWUSR,
		   sdev_show_queue_ramp_up_period,
		   sdev_store_queue_ramp_up_period);

static umode_t scsi_sdev_attr_is_visible(struct kobject *kobj,
					 struct attribute *attr, int i)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct scsi_device *sdev = to_scsi_device(dev);


	if (attr == &dev_attr_queue_depth.attr &&
	    !sdev->host->hostt->change_queue_depth)
		return S_IRUGO;

	if (attr == &dev_attr_queue_ramp_up_period.attr &&
	    !sdev->host->hostt->change_queue_depth)
		return 0;

	return attr->mode;
}

/* Default template for device attributes.  May NOT be modified */
static struct attribute *scsi_sdev_attrs[] = {
	&dev_attr_device_blocked.attr,
	&dev_attr_type.attr,
	&dev_attr_scsi_level.attr,
	&dev_attr_device_busy.attr,
	&dev_attr_vendor.attr,
	&dev_attr_model.attr,
	&dev_attr_rev.attr,
	&dev_attr_rescan.attr,
	&dev_attr_delete.attr,
	&dev_attr_state.attr,
	&dev_attr_timeout.attr,
	&dev_attr_eh_timeout.attr,
	&dev_attr_iocounterbits.attr,
	&dev_attr_iorequest_cnt.attr,
	&dev_attr_iodone_cnt.attr,
	&dev_attr_ioerr_cnt.attr,
	&dev_attr_modalias.attr,
	&dev_attr_queue_depth.attr,
	&dev_attr_queue_type.attr,
	&dev_attr_wwid.attr,
#ifdef CONFIG_SCSI_DH
	&dev_attr_dh_state.attr,
#endif
	&dev_attr_queue_ramp_up_period.attr,
	REF_EVT(media_change),
	REF_EVT(inquiry_change_reported),
	REF_EVT(capacity_change_reported),
	REF_EVT(soft_threshold_reached),
	REF_EVT(mode_parameter_change_reported),
	REF_EVT(lun_change_reported),
	NULL
};

static struct bin_attribute *scsi_sdev_bin_attrs[] = {
	&dev_attr_vpd_pg83,
	&dev_attr_vpd_pg80,
	&dev_attr_inquiry,
	NULL
};
static struct attribute_group scsi_sdev_attr_group = {
	.attrs =	scsi_sdev_attrs,
	.bin_attrs =	scsi_sdev_bin_attrs,
	.is_visible =	scsi_sdev_attr_is_visible,
};

static const struct attribute_group *scsi_sdev_attr_groups[] = {
	&scsi_sdev_attr_group,
	NULL
};

static int scsi_target_add(struct scsi_target *starget)
{
	int error;

	if (starget->state != STARGET_CREATED)
		return 0;

	error = device_add(&starget->dev);
	if (error) {
		dev_err(&starget->dev, "target device_add failed, error %d\n", error);
		return error;
	}
	transport_add_device(&starget->dev);
	starget->state = STARGET_RUNNING;

	pm_runtime_set_active(&starget->dev);
	pm_runtime_enable(&starget->dev);
	device_enable_async_suspend(&starget->dev);

	return 0;
}

/**
 * scsi_sysfs_add_sdev - add scsi device to sysfs
 * @sdev:	scsi_device to add
 *
 * Return value:
 * 	0 on Success / non-zero on Failure
 **/
int scsi_sysfs_add_sdev(struct scsi_device *sdev)
{
	int error, i;
	struct request_queue *rq = sdev->request_queue;
	struct scsi_target *starget = sdev->sdev_target;

	error = scsi_device_set_state(sdev, SDEV_RUNNING);
	if (error)
		return error;

	error = scsi_target_add(starget);
	if (error)
		return error;

	transport_configure_device(&starget->dev);

	device_enable_async_suspend(&sdev->sdev_gendev);
	scsi_autopm_get_target(starget);
	pm_runtime_set_active(&sdev->sdev_gendev);
	pm_runtime_forbid(&sdev->sdev_gendev);
	pm_runtime_enable(&sdev->sdev_gendev);
	scsi_autopm_put_target(starget);

	scsi_autopm_get_device(sdev);

	error = device_add(&sdev->sdev_gendev);
	if (error) {
		sdev_printk(KERN_INFO, sdev,
				"failed to add device: %d\n", error);
		return error;
	}

	error = scsi_dh_add_device(sdev);
	if (error)
		/*
		 * device_handler is optional, so any error can be ignored
		 */
		sdev_printk(KERN_INFO, sdev,
				"failed to add device handler: %d\n", error);

	device_enable_async_suspend(&sdev->sdev_dev);
	error = device_add(&sdev->sdev_dev);
	if (error) {
		sdev_printk(KERN_INFO, sdev,
				"failed to add class device: %d\n", error);
		scsi_dh_remove_device(sdev);
		device_del(&sdev->sdev_gendev);
		return error;
	}
	transport_add_device(&sdev->sdev_gendev);
	sdev->is_visible = 1;

	error = bsg_register_queue(rq, &sdev->sdev_gendev, NULL, NULL);

	if (error)
		/* we're treating error on bsg register as non-fatal,
		 * so pretend nothing went wrong */
		sdev_printk(KERN_INFO, sdev,
			    "Failed to register bsg queue, errno=%d\n", error);

	/* add additional host specific attributes */
	if (sdev->host->hostt->sdev_attrs) {
		for (i = 0; sdev->host->hostt->sdev_attrs[i]; i++) {
			error = device_create_file(&sdev->sdev_gendev,
					sdev->host->hostt->sdev_attrs[i]);
			if (error)
				return error;
		}
	}

	scsi_autopm_put_device(sdev);
	return error;
}

void __scsi_remove_device(struct scsi_device *sdev)
{
	struct device *dev = &sdev->sdev_gendev;

	/*
	 * This cleanup path is not reentrant and while it is impossible
	 * to get a new reference with scsi_device_get() someone can still
	 * hold a previously acquired one.
	 */
	if (sdev->sdev_state == SDEV_DEL)
		return;

	if (sdev->is_visible) {
		if (scsi_device_set_state(sdev, SDEV_CANCEL) != 0)
			return;

		bsg_unregister_queue(sdev->request_queue);
		device_unregister(&sdev->sdev_dev);
		transport_remove_device(dev);
		scsi_dh_remove_device(sdev);
		device_del(dev);
	} else
		put_device(&sdev->sdev_dev);

	/*
	 * Stop accepting new requests and wait until all queuecommand() and
	 * scsi_run_queue() invocations have finished before tearing down the
	 * device.
	 */
	scsi_device_set_state(sdev, SDEV_DEL);
	blk_cleanup_queue(sdev->request_queue);
	cancel_work_sync(&sdev->requeue_work);

	if (sdev->host->hostt->slave_destroy)
		sdev->host->hostt->slave_destroy(sdev);
	transport_destroy_device(dev);

	/*
	 * Paired with the kref_get() in scsi_sysfs_initialize().  We have
	 * remoed sysfs visibility from the device, so make the target
	 * invisible if this was the last device underneath it.
	 */
	scsi_target_reap(scsi_target(sdev));

	put_device(dev);
}

/**
 * scsi_remove_device - unregister a device from the scsi bus
 * @sdev:	scsi_device to unregister
 **/
void scsi_remove_device(struct scsi_device *sdev)
{
	struct Scsi_Host *shost = sdev->host;

	mutex_lock(&shost->scan_mutex);
	__scsi_remove_device(sdev);
	mutex_unlock(&shost->scan_mutex);
}
EXPORT_SYMBOL(scsi_remove_device);

static void __scsi_remove_target(struct scsi_target *starget)
{
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	unsigned long flags;
	struct scsi_device *sdev;

	spin_lock_irqsave(shost->host_lock, flags);
 restart:
	list_for_each_entry(sdev, &shost->__devices, siblings) {
		if (sdev->channel != starget->channel ||
		    sdev->id != starget->id ||
		    scsi_device_get(sdev))
			continue;
		spin_unlock_irqrestore(shost->host_lock, flags);
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
		spin_lock_irqsave(shost->host_lock, flags);
		goto restart;
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}

/**
 * scsi_remove_target - try to remove a target and all its devices
 * @dev: generic starget or parent of generic stargets to be removed
 *
 * Note: This is slightly racy.  It is possible that if the user
 * requests the addition of another device then the target won't be
 * removed.
 */
void scsi_remove_target(struct device *dev)
{
	struct Scsi_Host *shost = dev_to_shost(dev->parent);
	struct scsi_target *starget;
	unsigned long flags;

restart:
	spin_lock_irqsave(shost->host_lock, flags);
	list_for_each_entry(starget, &shost->__targets, siblings) {
		if (starget->state == STARGET_DEL)
			continue;
		if (starget->dev.parent == dev || &starget->dev == dev) {
			kref_get(&starget->reap_ref);
			spin_unlock_irqrestore(shost->host_lock, flags);
			__scsi_remove_target(starget);
			scsi_target_reap(starget);
			goto restart;
		}
	}
	spin_unlock_irqrestore(shost->host_lock, flags);
}
EXPORT_SYMBOL(scsi_remove_target);

int scsi_register_driver(struct device_driver *drv)
{
	drv->bus = &scsi_bus_type;

	return driver_register(drv);
}
EXPORT_SYMBOL(scsi_register_driver);

int scsi_register_interface(struct class_interface *intf)
{
	intf->class = &sdev_class;

	return class_interface_register(intf);
}
EXPORT_SYMBOL(scsi_register_interface);

/**
 * scsi_sysfs_add_host - add scsi host to subsystem
 * @shost:     scsi host struct to add to subsystem
 **/
int scsi_sysfs_add_host(struct Scsi_Host *shost)
{
	int error, i;

	/* add host specific attributes */
	if (shost->hostt->shost_attrs) {
		for (i = 0; shost->hostt->shost_attrs[i]; i++) {
			error = device_create_file(&shost->shost_dev,
					shost->hostt->shost_attrs[i]);
			if (error)
				return error;
		}
	}

	transport_register_device(&shost->shost_gendev);
	transport_configure_device(&shost->shost_gendev);
	return 0;
}

static struct device_type scsi_dev_type = {
	.name =		"scsi_device",
	.release =	scsi_device_dev_release,
	.groups =	scsi_sdev_attr_groups,
};

void scsi_sysfs_device_initialize(struct scsi_device *sdev)
{
	unsigned long flags;
	struct Scsi_Host *shost = sdev->host;
	struct scsi_target  *starget = sdev->sdev_target;

	device_initialize(&sdev->sdev_gendev);
	sdev->sdev_gendev.bus = &scsi_bus_type;
	sdev->sdev_gendev.type = &scsi_dev_type;
	dev_set_name(&sdev->sdev_gendev, "%d:%d:%d:%llu",
		     sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);

	device_initialize(&sdev->sdev_dev);
	sdev->sdev_dev.parent = get_device(&sdev->sdev_gendev);
	sdev->sdev_dev.class = &sdev_class;
	dev_set_name(&sdev->sdev_dev, "%d:%d:%d:%llu",
		     sdev->host->host_no, sdev->channel, sdev->id, sdev->lun);
	/*
	 * Get a default scsi_level from the target (derived from sibling
	 * devices).  This is the best we can do for guessing how to set
	 * sdev->lun_in_cdb for the initial INQUIRY command.  For LUN 0 the
	 * setting doesn't matter, because all the bits are zero anyway.
	 * But it does matter for higher LUNs.
	 */
	sdev->scsi_level = starget->scsi_level;
	if (sdev->scsi_level <= SCSI_2 &&
			sdev->scsi_level != SCSI_UNKNOWN &&
			!shost->no_scsi2_lun_in_cdb)
		sdev->lun_in_cdb = 1;

	transport_setup_device(&sdev->sdev_gendev);
	spin_lock_irqsave(shost->host_lock, flags);
	list_add_tail(&sdev->same_target_siblings, &starget->devices);
	list_add_tail(&sdev->siblings, &shost->__devices);
	spin_unlock_irqrestore(shost->host_lock, flags);
	/*
	 * device can now only be removed via __scsi_remove_device() so hold
	 * the target.  Target will be held in CREATED state until something
	 * beneath it becomes visible (in which case it moves to RUNNING)
	 */
	kref_get(&starget->reap_ref);
}

int scsi_is_sdev_device(const struct device *dev)
{
	return dev->type == &scsi_dev_type;
}
EXPORT_SYMBOL(scsi_is_sdev_device);

/* A blank transport template that is used in drivers that don't
 * yet implement Transport Attributes */
struct scsi_transport_template blank_transport_template = { { { {NULL, }, }, }, };
