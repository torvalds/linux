/*
 * WUSB Wire Adapter: WLP interface
 * Sysfs interfaces
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 *
 * FIXME: docs
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/uwb/debug.h>
#include <linux/device.h>
#include "i1480u-wlp.h"


/**
 *
 * @dev: Class device from the net_device; assumed refcnted.
 *
 * Yes, I don't lock--we assume it is refcounted and I am getting a
 * single byte value that is kind of atomic to read.
 */
ssize_t uwb_phy_rate_show(const struct wlp_options *options, char *buf)
{
	return sprintf(buf, "%u\n",
		       wlp_tx_hdr_phy_rate(&options->def_tx_hdr));
}
EXPORT_SYMBOL_GPL(uwb_phy_rate_show);


ssize_t uwb_phy_rate_store(struct wlp_options *options,
			   const char *buf, size_t size)
{
	ssize_t result;
	unsigned rate;

	result = sscanf(buf, "%u\n", &rate);
	if (result != 1) {
		result = -EINVAL;
		goto out;
	}
	result = -EINVAL;
	if (rate >= UWB_PHY_RATE_INVALID)
		goto out;
	wlp_tx_hdr_set_phy_rate(&options->def_tx_hdr, rate);
	result = 0;
out:
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(uwb_phy_rate_store);


ssize_t uwb_rts_cts_show(const struct wlp_options *options, char *buf)
{
	return sprintf(buf, "%u\n",
		       wlp_tx_hdr_rts_cts(&options->def_tx_hdr));
}
EXPORT_SYMBOL_GPL(uwb_rts_cts_show);


ssize_t uwb_rts_cts_store(struct wlp_options *options,
			  const char *buf, size_t size)
{
	ssize_t result;
	unsigned value;

	result = sscanf(buf, "%u\n", &value);
	if (result != 1) {
		result = -EINVAL;
		goto out;
	}
	result = -EINVAL;
	wlp_tx_hdr_set_rts_cts(&options->def_tx_hdr, !!value);
	result = 0;
out:
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(uwb_rts_cts_store);


ssize_t uwb_ack_policy_show(const struct wlp_options *options, char *buf)
{
	return sprintf(buf, "%u\n",
		       wlp_tx_hdr_ack_policy(&options->def_tx_hdr));
}
EXPORT_SYMBOL_GPL(uwb_ack_policy_show);


ssize_t uwb_ack_policy_store(struct wlp_options *options,
			     const char *buf, size_t size)
{
	ssize_t result;
	unsigned value;

	result = sscanf(buf, "%u\n", &value);
	if (result != 1 || value > UWB_ACK_B_REQ) {
		result = -EINVAL;
		goto out;
	}
	wlp_tx_hdr_set_ack_policy(&options->def_tx_hdr, value);
	result = 0;
out:
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(uwb_ack_policy_store);


/**
 * Show the PCA base priority.
 *
 * We can access without locking, as the value is (for now) orthogonal
 * to other values.
 */
ssize_t uwb_pca_base_priority_show(const struct wlp_options *options,
				   char *buf)
{
	return sprintf(buf, "%u\n",
		       options->pca_base_priority);
}
EXPORT_SYMBOL_GPL(uwb_pca_base_priority_show);


/**
 * Set the PCA base priority.
 *
 * We can access without locking, as the value is (for now) orthogonal
 * to other values.
 */
ssize_t uwb_pca_base_priority_store(struct wlp_options *options,
				    const char *buf, size_t size)
{
	ssize_t result = -EINVAL;
	u8 pca_base_priority;

	result = sscanf(buf, "%hhu\n", &pca_base_priority);
	if (result != 1) {
		result = -EINVAL;
		goto out;
	}
	result = -EINVAL;
	if (pca_base_priority >= 8)
		goto out;
	options->pca_base_priority = pca_base_priority;
	/* Update TX header if we are currently using PCA. */
	if (result >= 0 && (wlp_tx_hdr_delivery_id_type(&options->def_tx_hdr) & WLP_DRP) == 0)
		wlp_tx_hdr_set_delivery_id_type(&options->def_tx_hdr, options->pca_base_priority);
	result = 0;
out:
	return result < 0 ? result : size;
}
EXPORT_SYMBOL_GPL(uwb_pca_base_priority_store);

/**
 * Show current inflight values
 *
 * Will print the current MAX and THRESHOLD values for the basic flow
 * control. In addition it will report how many times the TX queue needed
 * to be restarted since the last time this query was made.
 */
static ssize_t wlp_tx_inflight_show(struct i1480u_tx_inflight *inflight,
				    char *buf)
{
	ssize_t result;
	unsigned long sec_elapsed = (jiffies - inflight->restart_ts)/HZ;
	unsigned long restart_count = atomic_read(&inflight->restart_count);

	result = scnprintf(buf, PAGE_SIZE, "%lu %lu %d %lu %lu %lu\n"
			   "#read: threshold max inflight_count restarts "
			   "seconds restarts/sec\n"
			   "#write: threshold max\n",
			   inflight->threshold, inflight->max,
			   atomic_read(&inflight->count),
			   restart_count, sec_elapsed,
			   sec_elapsed == 0 ? 0 : restart_count/sec_elapsed);
	inflight->restart_ts = jiffies;
	atomic_set(&inflight->restart_count, 0);
	return result;
}

static
ssize_t wlp_tx_inflight_store(struct i1480u_tx_inflight *inflight,
				const char *buf, size_t size)
{
	unsigned long in_threshold, in_max;
	ssize_t result;
	result = sscanf(buf, "%lu %lu", &in_threshold, &in_max);
	if (result != 2)
		return -EINVAL;
	if (in_max <= in_threshold)
		return -EINVAL;
	inflight->max = in_max;
	inflight->threshold = in_threshold;
	return size;
}
/*
 * Glue (or function adaptors) for accesing info on sysfs
 *
 * [we need this indirection because the PCI driver does almost the
 * same]
 *
 * Linux 2.6.21 changed how 'struct netdevice' does attributes (from
 * having a 'struct class_dev' to having a 'struct device'). That is
 * quite of a pain.
 *
 * So we try to abstract that here. i1480u_SHOW() and i1480u_STORE()
 * create adaptors for extracting the 'struct i1480u' from a 'struct
 * dev' and calling a function for doing a sysfs operation (as we have
 * them factorized already). i1480u_ATTR creates the attribute file
 * (CLASS_DEVICE_ATTR or DEVICE_ATTR) and i1480u_ATTR_NAME produces a
 * class_device_attr_NAME or device_attr_NAME (for group registration).
 */
#include <linux/version.h>

#define i1480u_SHOW(name, fn, param)				\
static ssize_t i1480u_show_##name(struct device *dev,		\
				  struct device_attribute *attr,\
				  char *buf)			\
{								\
	struct i1480u *i1480u = netdev_priv(to_net_dev(dev));	\
	return fn(&i1480u->param, buf);				\
}

#define i1480u_STORE(name, fn, param)				\
static ssize_t i1480u_store_##name(struct device *dev,		\
				   struct device_attribute *attr,\
				   const char *buf, size_t size)\
{								\
	struct i1480u *i1480u = netdev_priv(to_net_dev(dev));	\
	return fn(&i1480u->param, buf, size);			\
}

#define i1480u_ATTR(name, perm) static DEVICE_ATTR(name, perm,  \
					     i1480u_show_##name,\
					     i1480u_store_##name)

#define i1480u_ATTR_SHOW(name) static DEVICE_ATTR(name,		\
					S_IRUGO,		\
					i1480u_show_##name, NULL)

#define i1480u_ATTR_NAME(a) (dev_attr_##a)


/*
 * Sysfs adaptors
 */
i1480u_SHOW(uwb_phy_rate, uwb_phy_rate_show, options);
i1480u_STORE(uwb_phy_rate, uwb_phy_rate_store, options);
i1480u_ATTR(uwb_phy_rate, S_IRUGO | S_IWUSR);

i1480u_SHOW(uwb_rts_cts, uwb_rts_cts_show, options);
i1480u_STORE(uwb_rts_cts, uwb_rts_cts_store, options);
i1480u_ATTR(uwb_rts_cts, S_IRUGO | S_IWUSR);

i1480u_SHOW(uwb_ack_policy, uwb_ack_policy_show, options);
i1480u_STORE(uwb_ack_policy, uwb_ack_policy_store, options);
i1480u_ATTR(uwb_ack_policy, S_IRUGO | S_IWUSR);

i1480u_SHOW(uwb_pca_base_priority, uwb_pca_base_priority_show, options);
i1480u_STORE(uwb_pca_base_priority, uwb_pca_base_priority_store, options);
i1480u_ATTR(uwb_pca_base_priority, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_eda, wlp_eda_show, wlp);
i1480u_STORE(wlp_eda, wlp_eda_store, wlp);
i1480u_ATTR(wlp_eda, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_uuid, wlp_uuid_show, wlp);
i1480u_STORE(wlp_uuid, wlp_uuid_store, wlp);
i1480u_ATTR(wlp_uuid, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_dev_name, wlp_dev_name_show, wlp);
i1480u_STORE(wlp_dev_name, wlp_dev_name_store, wlp);
i1480u_ATTR(wlp_dev_name, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_dev_manufacturer, wlp_dev_manufacturer_show, wlp);
i1480u_STORE(wlp_dev_manufacturer, wlp_dev_manufacturer_store, wlp);
i1480u_ATTR(wlp_dev_manufacturer, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_dev_model_name, wlp_dev_model_name_show, wlp);
i1480u_STORE(wlp_dev_model_name, wlp_dev_model_name_store, wlp);
i1480u_ATTR(wlp_dev_model_name, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_dev_model_nr, wlp_dev_model_nr_show, wlp);
i1480u_STORE(wlp_dev_model_nr, wlp_dev_model_nr_store, wlp);
i1480u_ATTR(wlp_dev_model_nr, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_dev_serial, wlp_dev_serial_show, wlp);
i1480u_STORE(wlp_dev_serial, wlp_dev_serial_store, wlp);
i1480u_ATTR(wlp_dev_serial, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_dev_prim_category, wlp_dev_prim_category_show, wlp);
i1480u_STORE(wlp_dev_prim_category, wlp_dev_prim_category_store, wlp);
i1480u_ATTR(wlp_dev_prim_category, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_dev_prim_OUI, wlp_dev_prim_OUI_show, wlp);
i1480u_STORE(wlp_dev_prim_OUI, wlp_dev_prim_OUI_store, wlp);
i1480u_ATTR(wlp_dev_prim_OUI, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_dev_prim_OUI_sub, wlp_dev_prim_OUI_sub_show, wlp);
i1480u_STORE(wlp_dev_prim_OUI_sub, wlp_dev_prim_OUI_sub_store, wlp);
i1480u_ATTR(wlp_dev_prim_OUI_sub, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_dev_prim_subcat, wlp_dev_prim_subcat_show, wlp);
i1480u_STORE(wlp_dev_prim_subcat, wlp_dev_prim_subcat_store, wlp);
i1480u_ATTR(wlp_dev_prim_subcat, S_IRUGO | S_IWUSR);

i1480u_SHOW(wlp_neighborhood, wlp_neighborhood_show, wlp);
i1480u_ATTR_SHOW(wlp_neighborhood);

i1480u_SHOW(wss_activate, wlp_wss_activate_show, wlp.wss);
i1480u_STORE(wss_activate, wlp_wss_activate_store, wlp.wss);
i1480u_ATTR(wss_activate, S_IRUGO | S_IWUSR);

/*
 * Show the (min, max, avg) Line Quality Estimate (LQE, in dB) as over
 * the last 256 received WLP frames (ECMA-368 13.3).
 *
 * [the -7dB that have to be substracted from the LQI to make the LQE
 * are already taken into account].
 */
i1480u_SHOW(wlp_lqe, stats_show, lqe_stats);
i1480u_STORE(wlp_lqe, stats_store, lqe_stats);
i1480u_ATTR(wlp_lqe, S_IRUGO | S_IWUSR);

/*
 * Show the Receive Signal Strength Indicator averaged over all the
 * received WLP frames (ECMA-368 13.3). Still is not clear what
 * this value is, but is kind of a percentage of the signal strength
 * at the antenna.
 */
i1480u_SHOW(wlp_rssi, stats_show, rssi_stats);
i1480u_STORE(wlp_rssi, stats_store, rssi_stats);
i1480u_ATTR(wlp_rssi, S_IRUGO | S_IWUSR);

/**
 * We maintain a basic flow control counter. "count" how many TX URBs are
 * outstanding. Only allow "max"
 * TX URBs to be outstanding. If this value is reached the queue will be
 * stopped. The queue will be restarted when there are
 * "threshold" URBs outstanding.
 */
i1480u_SHOW(wlp_tx_inflight, wlp_tx_inflight_show, tx_inflight);
i1480u_STORE(wlp_tx_inflight, wlp_tx_inflight_store, tx_inflight);
i1480u_ATTR(wlp_tx_inflight, S_IRUGO | S_IWUSR);

static struct attribute *i1480u_attrs[] = {
	&i1480u_ATTR_NAME(uwb_phy_rate).attr,
	&i1480u_ATTR_NAME(uwb_rts_cts).attr,
	&i1480u_ATTR_NAME(uwb_ack_policy).attr,
	&i1480u_ATTR_NAME(uwb_pca_base_priority).attr,
	&i1480u_ATTR_NAME(wlp_lqe).attr,
	&i1480u_ATTR_NAME(wlp_rssi).attr,
	&i1480u_ATTR_NAME(wlp_eda).attr,
	&i1480u_ATTR_NAME(wlp_uuid).attr,
	&i1480u_ATTR_NAME(wlp_dev_name).attr,
	&i1480u_ATTR_NAME(wlp_dev_manufacturer).attr,
	&i1480u_ATTR_NAME(wlp_dev_model_name).attr,
	&i1480u_ATTR_NAME(wlp_dev_model_nr).attr,
	&i1480u_ATTR_NAME(wlp_dev_serial).attr,
	&i1480u_ATTR_NAME(wlp_dev_prim_category).attr,
	&i1480u_ATTR_NAME(wlp_dev_prim_OUI).attr,
	&i1480u_ATTR_NAME(wlp_dev_prim_OUI_sub).attr,
	&i1480u_ATTR_NAME(wlp_dev_prim_subcat).attr,
	&i1480u_ATTR_NAME(wlp_neighborhood).attr,
	&i1480u_ATTR_NAME(wss_activate).attr,
	&i1480u_ATTR_NAME(wlp_tx_inflight).attr,
	NULL,
};

static struct attribute_group i1480u_attr_group = {
	.name = NULL,	/* we want them in the same directory */
	.attrs = i1480u_attrs,
};

int i1480u_sysfs_setup(struct i1480u *i1480u)
{
	int result;
	struct device *dev = &i1480u->usb_iface->dev;
	result = sysfs_create_group(&i1480u->net_dev->dev.kobj,
				    &i1480u_attr_group);
	if (result < 0)
		dev_err(dev, "cannot initialize sysfs attributes: %d\n",
			result);
	return result;
}


void i1480u_sysfs_release(struct i1480u *i1480u)
{
	sysfs_remove_group(&i1480u->net_dev->dev.kobj,
			   &i1480u_attr_group);
}
