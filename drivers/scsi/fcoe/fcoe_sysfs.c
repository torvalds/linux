/*
 * Copyright(c) 2011 - 2012 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/etherdevice.h>
#include <linux/ctype.h>

#include <scsi/fcoe_sysfs.h>
#include <scsi/libfcoe.h>

/*
 * OK to include local libfcoe.h for debug_logging, but cannot include
 * <scsi/libfcoe.h> otherwise non-netdev based fcoe solutions would have
 * have to include more than fcoe_sysfs.h.
 */
#include "libfcoe.h"

static atomic_t ctlr_num;
static atomic_t fcf_num;

/*
 * fcoe_fcf_dev_loss_tmo: the default number of seconds that fcoe sysfs
 * should insulate the loss of a fcf.
 */
static unsigned int fcoe_fcf_dev_loss_tmo = 1800;  /* seconds */

module_param_named(fcf_dev_loss_tmo, fcoe_fcf_dev_loss_tmo,
		   uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(fcf_dev_loss_tmo,
		 "Maximum number of seconds that libfcoe should"
		 " insulate the loss of a fcf. Once this value is"
		 " exceeded, the fcf is removed.");

/*
 * These are used by the fcoe_*_show_function routines, they
 * are intentionally placed in the .c file as they're not intended
 * for use throughout the code.
 */
#define fcoe_ctlr_id(x)				\
	((x)->id)
#define fcoe_ctlr_work_q_name(x)		\
	((x)->work_q_name)
#define fcoe_ctlr_work_q(x)			\
	((x)->work_q)
#define fcoe_ctlr_devloss_work_q_name(x)	\
	((x)->devloss_work_q_name)
#define fcoe_ctlr_devloss_work_q(x)		\
	((x)->devloss_work_q)
#define fcoe_ctlr_mode(x)			\
	((x)->mode)
#define fcoe_ctlr_fcf_dev_loss_tmo(x)		\
	((x)->fcf_dev_loss_tmo)
#define fcoe_ctlr_link_fail(x)			\
	((x)->lesb.lesb_link_fail)
#define fcoe_ctlr_vlink_fail(x)			\
	((x)->lesb.lesb_vlink_fail)
#define fcoe_ctlr_miss_fka(x)			\
	((x)->lesb.lesb_miss_fka)
#define fcoe_ctlr_symb_err(x)			\
	((x)->lesb.lesb_symb_err)
#define fcoe_ctlr_err_block(x)			\
	((x)->lesb.lesb_err_block)
#define fcoe_ctlr_fcs_error(x)			\
	((x)->lesb.lesb_fcs_error)
#define fcoe_ctlr_enabled(x)			\
	((x)->enabled)
#define fcoe_fcf_state(x)			\
	((x)->state)
#define fcoe_fcf_fabric_name(x)			\
	((x)->fabric_name)
#define fcoe_fcf_switch_name(x)			\
	((x)->switch_name)
#define fcoe_fcf_fc_map(x)			\
	((x)->fc_map)
#define fcoe_fcf_vfid(x)			\
	((x)->vfid)
#define fcoe_fcf_mac(x)				\
	((x)->mac)
#define fcoe_fcf_priority(x)			\
	((x)->priority)
#define fcoe_fcf_fka_period(x)			\
	((x)->fka_period)
#define fcoe_fcf_dev_loss_tmo(x)		\
	((x)->dev_loss_tmo)
#define fcoe_fcf_selected(x)			\
	((x)->selected)
#define fcoe_fcf_vlan_id(x)			\
	((x)->vlan_id)

/*
 * dev_loss_tmo attribute
 */
static int fcoe_str_to_dev_loss(const char *buf, unsigned long *val)
{
	int ret;

	ret = kstrtoul(buf, 0, val);
	if (ret)
		return -EINVAL;
	/*
	 * Check for overflow; dev_loss_tmo is u32
	 */
	if (*val > UINT_MAX)
		return -EINVAL;

	return 0;
}

static int fcoe_fcf_set_dev_loss_tmo(struct fcoe_fcf_device *fcf,
				     unsigned long val)
{
	if ((fcf->state == FCOE_FCF_STATE_UNKNOWN) ||
	    (fcf->state == FCOE_FCF_STATE_DISCONNECTED) ||
	    (fcf->state == FCOE_FCF_STATE_DELETED))
		return -EBUSY;
	/*
	 * Check for overflow; dev_loss_tmo is u32
	 */
	if (val > UINT_MAX)
		return -EINVAL;

	fcoe_fcf_dev_loss_tmo(fcf) = val;
	return 0;
}

#define FCOE_DEVICE_ATTR(_prefix, _name, _mode, _show, _store)	\
struct device_attribute device_attr_fcoe_##_prefix##_##_name =	\
	__ATTR(_name, _mode, _show, _store)

#define fcoe_ctlr_show_function(field, format_string, sz, cast)	\
static ssize_t show_fcoe_ctlr_device_##field(struct device *dev, \
					    struct device_attribute *attr, \
					    char *buf)			\
{									\
	struct fcoe_ctlr_device *ctlr = dev_to_ctlr(dev);		\
	if (ctlr->f->get_fcoe_ctlr_##field)				\
		ctlr->f->get_fcoe_ctlr_##field(ctlr);			\
	return snprintf(buf, sz, format_string,				\
			cast fcoe_ctlr_##field(ctlr));			\
}

#define fcoe_fcf_show_function(field, format_string, sz, cast)	\
static ssize_t show_fcoe_fcf_device_##field(struct device *dev,	\
					   struct device_attribute *attr, \
					   char *buf)			\
{									\
	struct fcoe_fcf_device *fcf = dev_to_fcf(dev);			\
	struct fcoe_ctlr_device *ctlr = fcoe_fcf_dev_to_ctlr_dev(fcf);	\
	if (ctlr->f->get_fcoe_fcf_##field)				\
		ctlr->f->get_fcoe_fcf_##field(fcf);			\
	return snprintf(buf, sz, format_string,				\
			cast fcoe_fcf_##field(fcf));			\
}

#define fcoe_ctlr_private_show_function(field, format_string, sz, cast)	\
static ssize_t show_fcoe_ctlr_device_##field(struct device *dev, \
					    struct device_attribute *attr, \
					    char *buf)			\
{									\
	struct fcoe_ctlr_device *ctlr = dev_to_ctlr(dev);		\
	return snprintf(buf, sz, format_string, cast fcoe_ctlr_##field(ctlr)); \
}

#define fcoe_fcf_private_show_function(field, format_string, sz, cast)	\
static ssize_t show_fcoe_fcf_device_##field(struct device *dev,	\
					   struct device_attribute *attr, \
					   char *buf)			\
{								\
	struct fcoe_fcf_device *fcf = dev_to_fcf(dev);			\
	return snprintf(buf, sz, format_string, cast fcoe_fcf_##field(fcf)); \
}

#define fcoe_ctlr_private_rd_attr(field, format_string, sz)		\
	fcoe_ctlr_private_show_function(field, format_string, sz, )	\
	static FCOE_DEVICE_ATTR(ctlr, field, S_IRUGO,			\
				show_fcoe_ctlr_device_##field, NULL)

#define fcoe_ctlr_rd_attr(field, format_string, sz)			\
	fcoe_ctlr_show_function(field, format_string, sz, )		\
	static FCOE_DEVICE_ATTR(ctlr, field, S_IRUGO,			\
				show_fcoe_ctlr_device_##field, NULL)

#define fcoe_fcf_rd_attr(field, format_string, sz)			\
	fcoe_fcf_show_function(field, format_string, sz, )		\
	static FCOE_DEVICE_ATTR(fcf, field, S_IRUGO,			\
				show_fcoe_fcf_device_##field, NULL)

#define fcoe_fcf_private_rd_attr(field, format_string, sz)		\
	fcoe_fcf_private_show_function(field, format_string, sz, )	\
	static FCOE_DEVICE_ATTR(fcf, field, S_IRUGO,			\
				show_fcoe_fcf_device_##field, NULL)

#define fcoe_ctlr_private_rd_attr_cast(field, format_string, sz, cast)	\
	fcoe_ctlr_private_show_function(field, format_string, sz, (cast)) \
	static FCOE_DEVICE_ATTR(ctlr, field, S_IRUGO,			\
				show_fcoe_ctlr_device_##field, NULL)

#define fcoe_fcf_private_rd_attr_cast(field, format_string, sz, cast)	\
	fcoe_fcf_private_show_function(field, format_string, sz, (cast)) \
	static FCOE_DEVICE_ATTR(fcf, field, S_IRUGO,			\
				show_fcoe_fcf_device_##field, NULL)

#define fcoe_enum_name_search(title, table_type, table)			\
static const char *get_fcoe_##title##_name(enum table_type table_key)	\
{									\
	if (table_key < 0 || table_key >= ARRAY_SIZE(table))		\
		return NULL;						\
	return table[table_key];					\
}

static char *fip_conn_type_names[] = {
	[ FIP_CONN_TYPE_UNKNOWN ] = "Unknown",
	[ FIP_CONN_TYPE_FABRIC ]  = "Fabric",
	[ FIP_CONN_TYPE_VN2VN ]   = "VN2VN",
};
fcoe_enum_name_search(ctlr_mode, fip_conn_type, fip_conn_type_names)

static enum fip_conn_type fcoe_parse_mode(const char *buf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fip_conn_type_names); i++) {
		if (strcasecmp(buf, fip_conn_type_names[i]) == 0)
			return i;
	}

	return FIP_CONN_TYPE_UNKNOWN;
}

static char *fcf_state_names[] = {
	[ FCOE_FCF_STATE_UNKNOWN ]      = "Unknown",
	[ FCOE_FCF_STATE_DISCONNECTED ] = "Disconnected",
	[ FCOE_FCF_STATE_CONNECTED ]    = "Connected",
};
fcoe_enum_name_search(fcf_state, fcf_state, fcf_state_names)
#define FCOE_FCF_STATE_MAX_NAMELEN 50

static ssize_t show_fcf_state(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct fcoe_fcf_device *fcf = dev_to_fcf(dev);
	const char *name;
	name = get_fcoe_fcf_state_name(fcf->state);
	if (!name)
		return -EINVAL;
	return snprintf(buf, FCOE_FCF_STATE_MAX_NAMELEN, "%s\n", name);
}
static FCOE_DEVICE_ATTR(fcf, state, S_IRUGO, show_fcf_state, NULL);

#define FCOE_MAX_MODENAME_LEN 20
static ssize_t show_ctlr_mode(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct fcoe_ctlr_device *ctlr = dev_to_ctlr(dev);
	const char *name;

	name = get_fcoe_ctlr_mode_name(ctlr->mode);
	if (!name)
		return -EINVAL;
	return snprintf(buf, FCOE_MAX_MODENAME_LEN,
			"%s\n", name);
}

static ssize_t store_ctlr_mode(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct fcoe_ctlr_device *ctlr = dev_to_ctlr(dev);
	char mode[FCOE_MAX_MODENAME_LEN + 1];

	if (count > FCOE_MAX_MODENAME_LEN)
		return -EINVAL;

	strncpy(mode, buf, count);

	if (mode[count - 1] == '\n')
		mode[count - 1] = '\0';
	else
		mode[count] = '\0';

	switch (ctlr->enabled) {
	case FCOE_CTLR_ENABLED:
		LIBFCOE_SYSFS_DBG(ctlr, "Cannot change mode when enabled.");
		return -EBUSY;
	case FCOE_CTLR_DISABLED:
		if (!ctlr->f->set_fcoe_ctlr_mode) {
			LIBFCOE_SYSFS_DBG(ctlr,
					  "Mode change not supported by LLD.");
			return -ENOTSUPP;
		}

		ctlr->mode = fcoe_parse_mode(mode);
		if (ctlr->mode == FIP_CONN_TYPE_UNKNOWN) {
			LIBFCOE_SYSFS_DBG(ctlr,
					  "Unknown mode %s provided.", buf);
			return -EINVAL;
		}

		ctlr->f->set_fcoe_ctlr_mode(ctlr);
		LIBFCOE_SYSFS_DBG(ctlr, "Mode changed to %s.", buf);

		return count;
	case FCOE_CTLR_UNUSED:
	default:
		LIBFCOE_SYSFS_DBG(ctlr, "Mode change not supported.");
		return -ENOTSUPP;
	};
}

static FCOE_DEVICE_ATTR(ctlr, mode, S_IRUGO | S_IWUSR,
			show_ctlr_mode, store_ctlr_mode);

static ssize_t store_ctlr_enabled(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct fcoe_ctlr_device *ctlr = dev_to_ctlr(dev);
	int rc;

	switch (ctlr->enabled) {
	case FCOE_CTLR_ENABLED:
		if (*buf == '1')
			return count;
		ctlr->enabled = FCOE_CTLR_DISABLED;
		break;
	case FCOE_CTLR_DISABLED:
		if (*buf == '0')
			return count;
		ctlr->enabled = FCOE_CTLR_ENABLED;
		break;
	case FCOE_CTLR_UNUSED:
		return -ENOTSUPP;
	};

	rc = ctlr->f->set_fcoe_ctlr_enabled(ctlr);
	if (rc)
		return rc;

	return count;
}

static char *ctlr_enabled_state_names[] = {
	[ FCOE_CTLR_ENABLED ]  = "1",
	[ FCOE_CTLR_DISABLED ] = "0",
};
fcoe_enum_name_search(ctlr_enabled_state, ctlr_enabled_state,
		      ctlr_enabled_state_names)
#define FCOE_CTLR_ENABLED_MAX_NAMELEN 50

static ssize_t show_ctlr_enabled_state(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct fcoe_ctlr_device *ctlr = dev_to_ctlr(dev);
	const char *name;

	name = get_fcoe_ctlr_enabled_state_name(ctlr->enabled);
	if (!name)
		return -EINVAL;
	return snprintf(buf, FCOE_CTLR_ENABLED_MAX_NAMELEN,
			"%s\n", name);
}

static FCOE_DEVICE_ATTR(ctlr, enabled, S_IRUGO | S_IWUSR,
			show_ctlr_enabled_state,
			store_ctlr_enabled);

static ssize_t
store_private_fcoe_ctlr_fcf_dev_loss_tmo(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct fcoe_ctlr_device *ctlr = dev_to_ctlr(dev);
	struct fcoe_fcf_device *fcf;
	unsigned long val;
	int rc;

	rc = fcoe_str_to_dev_loss(buf, &val);
	if (rc)
		return rc;

	fcoe_ctlr_fcf_dev_loss_tmo(ctlr) = val;
	mutex_lock(&ctlr->lock);
	list_for_each_entry(fcf, &ctlr->fcfs, peers)
		fcoe_fcf_set_dev_loss_tmo(fcf, val);
	mutex_unlock(&ctlr->lock);
	return count;
}
fcoe_ctlr_private_show_function(fcf_dev_loss_tmo, "%d\n", 20, );
static FCOE_DEVICE_ATTR(ctlr, fcf_dev_loss_tmo, S_IRUGO | S_IWUSR,
			show_fcoe_ctlr_device_fcf_dev_loss_tmo,
			store_private_fcoe_ctlr_fcf_dev_loss_tmo);

/* Link Error Status Block (LESB) */
fcoe_ctlr_rd_attr(link_fail, "%u\n", 20);
fcoe_ctlr_rd_attr(vlink_fail, "%u\n", 20);
fcoe_ctlr_rd_attr(miss_fka, "%u\n", 20);
fcoe_ctlr_rd_attr(symb_err, "%u\n", 20);
fcoe_ctlr_rd_attr(err_block, "%u\n", 20);
fcoe_ctlr_rd_attr(fcs_error, "%u\n", 20);

fcoe_fcf_private_rd_attr_cast(fabric_name, "0x%llx\n", 20, unsigned long long);
fcoe_fcf_private_rd_attr_cast(switch_name, "0x%llx\n", 20, unsigned long long);
fcoe_fcf_private_rd_attr(priority, "%u\n", 20);
fcoe_fcf_private_rd_attr(fc_map, "0x%x\n", 20);
fcoe_fcf_private_rd_attr(vfid, "%u\n", 20);
fcoe_fcf_private_rd_attr(mac, "%pM\n", 20);
fcoe_fcf_private_rd_attr(fka_period, "%u\n", 20);
fcoe_fcf_rd_attr(selected, "%u\n", 20);
fcoe_fcf_rd_attr(vlan_id, "%u\n", 20);

fcoe_fcf_private_show_function(dev_loss_tmo, "%d\n", 20, )
static ssize_t
store_fcoe_fcf_dev_loss_tmo(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct fcoe_fcf_device *fcf = dev_to_fcf(dev);
	unsigned long val;
	int rc;

	rc = fcoe_str_to_dev_loss(buf, &val);
	if (rc)
		return rc;

	rc = fcoe_fcf_set_dev_loss_tmo(fcf, val);
	if (rc)
		return rc;
	return count;
}
static FCOE_DEVICE_ATTR(fcf, dev_loss_tmo, S_IRUGO | S_IWUSR,
			show_fcoe_fcf_device_dev_loss_tmo,
			store_fcoe_fcf_dev_loss_tmo);

static struct attribute *fcoe_ctlr_lesb_attrs[] = {
	&device_attr_fcoe_ctlr_link_fail.attr,
	&device_attr_fcoe_ctlr_vlink_fail.attr,
	&device_attr_fcoe_ctlr_miss_fka.attr,
	&device_attr_fcoe_ctlr_symb_err.attr,
	&device_attr_fcoe_ctlr_err_block.attr,
	&device_attr_fcoe_ctlr_fcs_error.attr,
	NULL,
};

static struct attribute_group fcoe_ctlr_lesb_attr_group = {
	.name = "lesb",
	.attrs = fcoe_ctlr_lesb_attrs,
};

static struct attribute *fcoe_ctlr_attrs[] = {
	&device_attr_fcoe_ctlr_fcf_dev_loss_tmo.attr,
	&device_attr_fcoe_ctlr_enabled.attr,
	&device_attr_fcoe_ctlr_mode.attr,
	NULL,
};

static struct attribute_group fcoe_ctlr_attr_group = {
	.attrs = fcoe_ctlr_attrs,
};

static const struct attribute_group *fcoe_ctlr_attr_groups[] = {
	&fcoe_ctlr_attr_group,
	&fcoe_ctlr_lesb_attr_group,
	NULL,
};

static struct attribute *fcoe_fcf_attrs[] = {
	&device_attr_fcoe_fcf_fabric_name.attr,
	&device_attr_fcoe_fcf_switch_name.attr,
	&device_attr_fcoe_fcf_dev_loss_tmo.attr,
	&device_attr_fcoe_fcf_fc_map.attr,
	&device_attr_fcoe_fcf_vfid.attr,
	&device_attr_fcoe_fcf_mac.attr,
	&device_attr_fcoe_fcf_priority.attr,
	&device_attr_fcoe_fcf_fka_period.attr,
	&device_attr_fcoe_fcf_state.attr,
	&device_attr_fcoe_fcf_selected.attr,
	&device_attr_fcoe_fcf_vlan_id.attr,
	NULL
};

static struct attribute_group fcoe_fcf_attr_group = {
	.attrs = fcoe_fcf_attrs,
};

static const struct attribute_group *fcoe_fcf_attr_groups[] = {
	&fcoe_fcf_attr_group,
	NULL,
};

struct bus_type fcoe_bus_type;

static int fcoe_bus_match(struct device *dev,
			  struct device_driver *drv)
{
	if (dev->bus == &fcoe_bus_type)
		return 1;
	return 0;
}

/**
 * fcoe_ctlr_device_release() - Release the FIP ctlr memory
 * @dev: Pointer to the FIP ctlr's embedded device
 *
 * Called when the last FIP ctlr reference is released.
 */
static void fcoe_ctlr_device_release(struct device *dev)
{
	struct fcoe_ctlr_device *ctlr = dev_to_ctlr(dev);
	kfree(ctlr);
}

/**
 * fcoe_fcf_device_release() - Release the FIP fcf memory
 * @dev: Pointer to the fcf's embedded device
 *
 * Called when the last FIP fcf reference is released.
 */
static void fcoe_fcf_device_release(struct device *dev)
{
	struct fcoe_fcf_device *fcf = dev_to_fcf(dev);
	kfree(fcf);
}

struct device_type fcoe_ctlr_device_type = {
	.name = "fcoe_ctlr",
	.groups = fcoe_ctlr_attr_groups,
	.release = fcoe_ctlr_device_release,
};

struct device_type fcoe_fcf_device_type = {
	.name = "fcoe_fcf",
	.groups = fcoe_fcf_attr_groups,
	.release = fcoe_fcf_device_release,
};

struct bus_attribute fcoe_bus_attr_group[] = {
	__ATTR(ctlr_create, S_IWUSR, NULL, fcoe_ctlr_create_store),
	__ATTR(ctlr_destroy, S_IWUSR, NULL, fcoe_ctlr_destroy_store),
	__ATTR_NULL
};

struct bus_type fcoe_bus_type = {
	.name = "fcoe",
	.match = &fcoe_bus_match,
	.bus_attrs = fcoe_bus_attr_group,
};

/**
 * fcoe_ctlr_device_flush_work() - Flush a FIP ctlr's workqueue
 * @ctlr: Pointer to the FIP ctlr whose workqueue is to be flushed
 */
void fcoe_ctlr_device_flush_work(struct fcoe_ctlr_device *ctlr)
{
	if (!fcoe_ctlr_work_q(ctlr)) {
		printk(KERN_ERR
		       "ERROR: FIP Ctlr '%d' attempted to flush work, "
		       "when no workqueue created.\n", ctlr->id);
		dump_stack();
		return;
	}

	flush_workqueue(fcoe_ctlr_work_q(ctlr));
}

/**
 * fcoe_ctlr_device_queue_work() - Schedule work for a FIP ctlr's workqueue
 * @ctlr: Pointer to the FIP ctlr who owns the devloss workqueue
 * @work:   Work to queue for execution
 *
 * Return value:
 *	1 on success / 0 already queued / < 0 for error
 */
int fcoe_ctlr_device_queue_work(struct fcoe_ctlr_device *ctlr,
			       struct work_struct *work)
{
	if (unlikely(!fcoe_ctlr_work_q(ctlr))) {
		printk(KERN_ERR
		       "ERROR: FIP Ctlr '%d' attempted to queue work, "
		       "when no workqueue created.\n", ctlr->id);
		dump_stack();

		return -EINVAL;
	}

	return queue_work(fcoe_ctlr_work_q(ctlr), work);
}

/**
 * fcoe_ctlr_device_flush_devloss() - Flush a FIP ctlr's devloss workqueue
 * @ctlr: Pointer to FIP ctlr whose workqueue is to be flushed
 */
void fcoe_ctlr_device_flush_devloss(struct fcoe_ctlr_device *ctlr)
{
	if (!fcoe_ctlr_devloss_work_q(ctlr)) {
		printk(KERN_ERR
		       "ERROR: FIP Ctlr '%d' attempted to flush work, "
		       "when no workqueue created.\n", ctlr->id);
		dump_stack();
		return;
	}

	flush_workqueue(fcoe_ctlr_devloss_work_q(ctlr));
}

/**
 * fcoe_ctlr_device_queue_devloss_work() - Schedule work for a FIP ctlr's devloss workqueue
 * @ctlr: Pointer to the FIP ctlr who owns the devloss workqueue
 * @work:   Work to queue for execution
 * @delay:  jiffies to delay the work queuing
 *
 * Return value:
 *	1 on success / 0 already queued / < 0 for error
 */
int fcoe_ctlr_device_queue_devloss_work(struct fcoe_ctlr_device *ctlr,
				       struct delayed_work *work,
				       unsigned long delay)
{
	if (unlikely(!fcoe_ctlr_devloss_work_q(ctlr))) {
		printk(KERN_ERR
		       "ERROR: FIP Ctlr '%d' attempted to queue work, "
		       "when no workqueue created.\n", ctlr->id);
		dump_stack();

		return -EINVAL;
	}

	return queue_delayed_work(fcoe_ctlr_devloss_work_q(ctlr), work, delay);
}

static int fcoe_fcf_device_match(struct fcoe_fcf_device *new,
				 struct fcoe_fcf_device *old)
{
	if (new->switch_name == old->switch_name &&
	    new->fabric_name == old->fabric_name &&
	    new->fc_map == old->fc_map &&
	    compare_ether_addr(new->mac, old->mac) == 0)
		return 1;
	return 0;
}

/**
 * fcoe_ctlr_device_add() - Add a FIP ctlr to sysfs
 * @parent:    The parent device to which the fcoe_ctlr instance
 *             should be attached
 * @f:         The LLD's FCoE sysfs function template pointer
 * @priv_size: Size to be allocated with the fcoe_ctlr_device for the LLD
 *
 * This routine allocates a FIP ctlr object with some additional memory
 * for the LLD. The FIP ctlr is initialized, added to sysfs and then
 * attributes are added to it.
 */
struct fcoe_ctlr_device *fcoe_ctlr_device_add(struct device *parent,
				    struct fcoe_sysfs_function_template *f,
				    int priv_size)
{
	struct fcoe_ctlr_device *ctlr;
	int error = 0;

	ctlr = kzalloc(sizeof(struct fcoe_ctlr_device) + priv_size,
		       GFP_KERNEL);
	if (!ctlr)
		goto out;

	ctlr->id = atomic_inc_return(&ctlr_num) - 1;
	ctlr->f = f;
	ctlr->mode = FIP_CONN_TYPE_FABRIC;
	INIT_LIST_HEAD(&ctlr->fcfs);
	mutex_init(&ctlr->lock);
	ctlr->dev.parent = parent;
	ctlr->dev.bus = &fcoe_bus_type;
	ctlr->dev.type = &fcoe_ctlr_device_type;

	ctlr->fcf_dev_loss_tmo = fcoe_fcf_dev_loss_tmo;

	snprintf(ctlr->work_q_name, sizeof(ctlr->work_q_name),
		 "ctlr_wq_%d", ctlr->id);
	ctlr->work_q = create_singlethread_workqueue(
		ctlr->work_q_name);
	if (!ctlr->work_q)
		goto out_del;

	snprintf(ctlr->devloss_work_q_name,
		 sizeof(ctlr->devloss_work_q_name),
		 "ctlr_dl_wq_%d", ctlr->id);
	ctlr->devloss_work_q = create_singlethread_workqueue(
		ctlr->devloss_work_q_name);
	if (!ctlr->devloss_work_q)
		goto out_del_q;

	dev_set_name(&ctlr->dev, "ctlr_%d", ctlr->id);
	error = device_register(&ctlr->dev);
	if (error)
		goto out_del_q2;

	return ctlr;

out_del_q2:
	destroy_workqueue(ctlr->devloss_work_q);
	ctlr->devloss_work_q = NULL;
out_del_q:
	destroy_workqueue(ctlr->work_q);
	ctlr->work_q = NULL;
out_del:
	kfree(ctlr);
out:
	return NULL;
}
EXPORT_SYMBOL_GPL(fcoe_ctlr_device_add);

/**
 * fcoe_ctlr_device_delete() - Delete a FIP ctlr and its subtree from sysfs
 * @ctlr: A pointer to the ctlr to be deleted
 *
 * Deletes a FIP ctlr and any fcfs attached
 * to it. Deleting fcfs will cause their childen
 * to be deleted as well.
 *
 * The ctlr is detached from sysfs and it's resources
 * are freed (work q), but the memory is not freed
 * until its last reference is released.
 *
 * This routine expects no locks to be held before
 * calling.
 *
 * TODO: Currently there are no callbacks to clean up LLD data
 * for a fcoe_fcf_device. LLDs must keep this in mind as they need
 * to clean up each of their LLD data for all fcoe_fcf_device before
 * calling fcoe_ctlr_device_delete.
 */
void fcoe_ctlr_device_delete(struct fcoe_ctlr_device *ctlr)
{
	struct fcoe_fcf_device *fcf, *next;
	/* Remove any attached fcfs */
	mutex_lock(&ctlr->lock);
	list_for_each_entry_safe(fcf, next,
				 &ctlr->fcfs, peers) {
		list_del(&fcf->peers);
		fcf->state = FCOE_FCF_STATE_DELETED;
		fcoe_ctlr_device_queue_work(ctlr, &fcf->delete_work);
	}
	mutex_unlock(&ctlr->lock);

	fcoe_ctlr_device_flush_work(ctlr);

	destroy_workqueue(ctlr->devloss_work_q);
	ctlr->devloss_work_q = NULL;
	destroy_workqueue(ctlr->work_q);
	ctlr->work_q = NULL;

	device_unregister(&ctlr->dev);
}
EXPORT_SYMBOL_GPL(fcoe_ctlr_device_delete);

/**
 * fcoe_fcf_device_final_delete() - Final delete routine
 * @work: The FIP fcf's embedded work struct
 *
 * It is expected that the fcf has been removed from
 * the FIP ctlr's list before calling this routine.
 */
static void fcoe_fcf_device_final_delete(struct work_struct *work)
{
	struct fcoe_fcf_device *fcf =
		container_of(work, struct fcoe_fcf_device, delete_work);
	struct fcoe_ctlr_device *ctlr = fcoe_fcf_dev_to_ctlr_dev(fcf);

	/*
	 * Cancel any outstanding timers. These should really exist
	 * only when rmmod'ing the LLDD and we're asking for
	 * immediate termination of the rports
	 */
	if (!cancel_delayed_work(&fcf->dev_loss_work))
		fcoe_ctlr_device_flush_devloss(ctlr);

	device_unregister(&fcf->dev);
}

/**
 * fip_timeout_deleted_fcf() - Delete a fcf when the devloss timer fires
 * @work: The FIP fcf's embedded work struct
 *
 * Removes the fcf from the FIP ctlr's list of fcfs and
 * queues the final deletion.
 */
static void fip_timeout_deleted_fcf(struct work_struct *work)
{
	struct fcoe_fcf_device *fcf =
		container_of(work, struct fcoe_fcf_device, dev_loss_work.work);
	struct fcoe_ctlr_device *ctlr = fcoe_fcf_dev_to_ctlr_dev(fcf);

	mutex_lock(&ctlr->lock);

	/*
	 * If the fcf is deleted or reconnected before the timer
	 * fires the devloss queue will be flushed, but the state will
	 * either be CONNECTED or DELETED. If that is the case we
	 * cancel deleting the fcf.
	 */
	if (fcf->state != FCOE_FCF_STATE_DISCONNECTED)
		goto out;

	dev_printk(KERN_ERR, &fcf->dev,
		   "FIP fcf connection time out: removing fcf\n");

	list_del(&fcf->peers);
	fcf->state = FCOE_FCF_STATE_DELETED;
	fcoe_ctlr_device_queue_work(ctlr, &fcf->delete_work);

out:
	mutex_unlock(&ctlr->lock);
}

/**
 * fcoe_fcf_device_delete() - Delete a FIP fcf
 * @fcf: Pointer to the fcf which is to be deleted
 *
 * Queues the FIP fcf on the devloss workqueue
 *
 * Expects the ctlr_attrs mutex to be held for fcf
 * state change.
 */
void fcoe_fcf_device_delete(struct fcoe_fcf_device *fcf)
{
	struct fcoe_ctlr_device *ctlr = fcoe_fcf_dev_to_ctlr_dev(fcf);
	int timeout = fcf->dev_loss_tmo;

	if (fcf->state != FCOE_FCF_STATE_CONNECTED)
		return;

	fcf->state = FCOE_FCF_STATE_DISCONNECTED;

	/*
	 * FCF will only be re-connected by the LLD calling
	 * fcoe_fcf_device_add, and it should be setting up
	 * priv then.
	 */
	fcf->priv = NULL;

	fcoe_ctlr_device_queue_devloss_work(ctlr, &fcf->dev_loss_work,
					   timeout * HZ);
}
EXPORT_SYMBOL_GPL(fcoe_fcf_device_delete);

/**
 * fcoe_fcf_device_add() - Add a FCoE sysfs fcoe_fcf_device to the system
 * @ctlr:    The fcoe_ctlr_device that will be the fcoe_fcf_device parent
 * @new_fcf: A temporary FCF used for lookups on the current list of fcfs
 *
 * Expects to be called with the ctlr->lock held
 */
struct fcoe_fcf_device *fcoe_fcf_device_add(struct fcoe_ctlr_device *ctlr,
					    struct fcoe_fcf_device *new_fcf)
{
	struct fcoe_fcf_device *fcf;
	int error = 0;

	list_for_each_entry(fcf, &ctlr->fcfs, peers) {
		if (fcoe_fcf_device_match(new_fcf, fcf)) {
			if (fcf->state == FCOE_FCF_STATE_CONNECTED)
				return fcf;

			fcf->state = FCOE_FCF_STATE_CONNECTED;

			if (!cancel_delayed_work(&fcf->dev_loss_work))
				fcoe_ctlr_device_flush_devloss(ctlr);

			return fcf;
		}
	}

	fcf = kzalloc(sizeof(struct fcoe_fcf_device), GFP_ATOMIC);
	if (unlikely(!fcf))
		goto out;

	INIT_WORK(&fcf->delete_work, fcoe_fcf_device_final_delete);
	INIT_DELAYED_WORK(&fcf->dev_loss_work, fip_timeout_deleted_fcf);

	fcf->dev.parent = &ctlr->dev;
	fcf->dev.bus = &fcoe_bus_type;
	fcf->dev.type = &fcoe_fcf_device_type;
	fcf->id = atomic_inc_return(&fcf_num) - 1;
	fcf->state = FCOE_FCF_STATE_UNKNOWN;

	fcf->dev_loss_tmo = ctlr->fcf_dev_loss_tmo;

	dev_set_name(&fcf->dev, "fcf_%d", fcf->id);

	fcf->fabric_name = new_fcf->fabric_name;
	fcf->switch_name = new_fcf->switch_name;
	fcf->fc_map = new_fcf->fc_map;
	fcf->vfid = new_fcf->vfid;
	memcpy(fcf->mac, new_fcf->mac, ETH_ALEN);
	fcf->priority = new_fcf->priority;
	fcf->fka_period = new_fcf->fka_period;
	fcf->selected = new_fcf->selected;

	error = device_register(&fcf->dev);
	if (error)
		goto out_del;

	fcf->state = FCOE_FCF_STATE_CONNECTED;
	list_add_tail(&fcf->peers, &ctlr->fcfs);

	return fcf;

out_del:
	kfree(fcf);
out:
	return NULL;
}
EXPORT_SYMBOL_GPL(fcoe_fcf_device_add);

int __init fcoe_sysfs_setup(void)
{
	int error;

	atomic_set(&ctlr_num, 0);
	atomic_set(&fcf_num, 0);

	error = bus_register(&fcoe_bus_type);
	if (error)
		return error;

	return 0;
}

void __exit fcoe_sysfs_teardown(void)
{
	bus_unregister(&fcoe_bus_type);
}
