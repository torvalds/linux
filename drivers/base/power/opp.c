/*
 * Generic OPP Interface
 *
 * Copyright (C) 2009-2010 Texas Instruments Incorporated.
 *	Nishanth Menon
 *	Romit Dasgupta
 *	Kevin Hilman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/pm_opp.h>
#include <linux/of.h>
#include <linux/export.h>

/*
 * Internal data structure organization with the OPP layer library is as
 * follows:
 * dev_opp_list (root)
 *	|- device 1 (represents voltage domain 1)
 *	|	|- opp 1 (availability, freq, voltage)
 *	|	|- opp 2 ..
 *	...	...
 *	|	`- opp n ..
 *	|- device 2 (represents the next voltage domain)
 *	...
 *	`- device m (represents mth voltage domain)
 * device 1, 2.. are represented by dev_opp structure while each opp
 * is represented by the opp structure.
 */

/**
 * struct dev_pm_opp - Generic OPP description structure
 * @node:	opp list node. The nodes are maintained throughout the lifetime
 *		of boot. It is expected only an optimal set of OPPs are
 *		added to the library by the SoC framework.
 *		RCU usage: opp list is traversed with RCU locks. node
 *		modification is possible realtime, hence the modifications
 *		are protected by the dev_opp_list_lock for integrity.
 *		IMPORTANT: the opp nodes should be maintained in increasing
 *		order.
 * @dynamic:	not-created from static DT entries.
 * @available:	true/false - marks if this OPP as available or not
 * @rate:	Frequency in hertz
 * @u_volt:	Nominal voltage in microvolts corresponding to this OPP
 * @dev_opp:	points back to the device_opp struct this opp belongs to
 * @rcu_head:	RCU callback head used for deferred freeing
 *
 * This structure stores the OPP information for a given device.
 */
struct dev_pm_opp {
	struct list_head node;

	bool available;
	bool dynamic;
	unsigned long rate;
	unsigned long u_volt;

	struct device_opp *dev_opp;
	struct rcu_head rcu_head;
};

/**
 * struct device_opp - Device opp structure
 * @node:	list node - contains the devices with OPPs that
 *		have been registered. Nodes once added are not modified in this
 *		list.
 *		RCU usage: nodes are not modified in the list of device_opp,
 *		however addition is possible and is secured by dev_opp_list_lock
 * @dev:	device pointer
 * @srcu_head:	notifier head to notify the OPP availability changes.
 * @rcu_head:	RCU callback head used for deferred freeing
 * @opp_list:	list of opps
 *
 * This is an internal data structure maintaining the link to opps attached to
 * a device. This structure is not meant to be shared to users as it is
 * meant for book keeping and private to OPP library
 */
struct device_opp {
	struct list_head node;

	struct device *dev;
	struct srcu_notifier_head srcu_head;
	struct rcu_head rcu_head;
	struct list_head opp_list;
};

/*
 * The root of the list of all devices. All device_opp structures branch off
 * from here, with each device_opp containing the list of opp it supports in
 * various states of availability.
 */
static LIST_HEAD(dev_opp_list);
/* Lock to allow exclusive modification to the device and opp lists */
static DEFINE_MUTEX(dev_opp_list_lock);

/**
 * find_device_opp() - find device_opp struct using device pointer
 * @dev:	device pointer used to lookup device OPPs
 *
 * Search list of device OPPs for one containing matching device. Does a RCU
 * reader operation to grab the pointer needed.
 *
 * Returns pointer to 'struct device_opp' if found, otherwise -ENODEV or
 * -EINVAL based on type of error.
 *
 * Locking: This function must be called under rcu_read_lock(). device_opp
 * is a RCU protected pointer. This means that device_opp is valid as long
 * as we are under RCU lock.
 */
static struct device_opp *find_device_opp(struct device *dev)
{
	struct device_opp *tmp_dev_opp, *dev_opp = ERR_PTR(-ENODEV);

	if (unlikely(IS_ERR_OR_NULL(dev))) {
		pr_err("%s: Invalid parameters\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	list_for_each_entry_rcu(tmp_dev_opp, &dev_opp_list, node) {
		if (tmp_dev_opp->dev == dev) {
			dev_opp = tmp_dev_opp;
			break;
		}
	}

	return dev_opp;
}

/**
 * dev_pm_opp_get_voltage() - Gets the voltage corresponding to an available opp
 * @opp:	opp for which voltage has to be returned for
 *
 * Return voltage in micro volt corresponding to the opp, else
 * return 0
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. This means that opp which could have been fetched by
 * opp_find_freq_{exact,ceil,floor} functions is valid as long as we are
 * under RCU lock. The pointer returned by the opp_find_freq family must be
 * used in the same section as the usage of this function with the pointer
 * prior to unlocking with rcu_read_unlock() to maintain the integrity of the
 * pointer.
 */
unsigned long dev_pm_opp_get_voltage(struct dev_pm_opp *opp)
{
	struct dev_pm_opp *tmp_opp;
	unsigned long v = 0;

	tmp_opp = rcu_dereference(opp);
	if (unlikely(IS_ERR_OR_NULL(tmp_opp)) || !tmp_opp->available)
		pr_err("%s: Invalid parameters\n", __func__);
	else
		v = tmp_opp->u_volt;

	return v;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_voltage);

/**
 * dev_pm_opp_get_freq() - Gets the frequency corresponding to an available opp
 * @opp:	opp for which frequency has to be returned for
 *
 * Return frequency in hertz corresponding to the opp, else
 * return 0
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. This means that opp which could have been fetched by
 * opp_find_freq_{exact,ceil,floor} functions is valid as long as we are
 * under RCU lock. The pointer returned by the opp_find_freq family must be
 * used in the same section as the usage of this function with the pointer
 * prior to unlocking with rcu_read_unlock() to maintain the integrity of the
 * pointer.
 */
unsigned long dev_pm_opp_get_freq(struct dev_pm_opp *opp)
{
	struct dev_pm_opp *tmp_opp;
	unsigned long f = 0;

	tmp_opp = rcu_dereference(opp);
	if (unlikely(IS_ERR_OR_NULL(tmp_opp)) || !tmp_opp->available)
		pr_err("%s: Invalid parameters\n", __func__);
	else
		f = tmp_opp->rate;

	return f;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_freq);

/**
 * dev_pm_opp_get_opp_count() - Get number of opps available in the opp list
 * @dev:	device for which we do this operation
 *
 * This function returns the number of available opps if there are any,
 * else returns 0 if none or the corresponding error value.
 *
 * Locking: This function must be called under rcu_read_lock(). This function
 * internally references two RCU protected structures: device_opp and opp which
 * are safe as long as we are under a common RCU locked section.
 */
int dev_pm_opp_get_opp_count(struct device *dev)
{
	struct device_opp *dev_opp;
	struct dev_pm_opp *temp_opp;
	int count = 0;

	dev_opp = find_device_opp(dev);
	if (IS_ERR(dev_opp)) {
		int r = PTR_ERR(dev_opp);
		dev_err(dev, "%s: device OPP not found (%d)\n", __func__, r);
		return r;
	}

	list_for_each_entry_rcu(temp_opp, &dev_opp->opp_list, node) {
		if (temp_opp->available)
			count++;
	}

	return count;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_opp_count);

/**
 * dev_pm_opp_find_freq_exact() - search for an exact frequency
 * @dev:		device for which we do this operation
 * @freq:		frequency to search for
 * @available:		true/false - match for available opp
 *
 * Searches for exact match in the opp list and returns pointer to the matching
 * opp if found, else returns ERR_PTR in case of error and should be handled
 * using IS_ERR. Error return values can be:
 * EINVAL:	for bad pointer
 * ERANGE:	no match found for search
 * ENODEV:	if device not found in list of registered devices
 *
 * Note: available is a modifier for the search. if available=true, then the
 * match is for exact matching frequency and is available in the stored OPP
 * table. if false, the match is for exact frequency which is not available.
 *
 * This provides a mechanism to enable an opp which is not available currently
 * or the opposite as well.
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. The reason for the same is that the opp pointer which is
 * returned will remain valid for use with opp_get_{voltage, freq} only while
 * under the locked area. The pointer returned must be used prior to unlocking
 * with rcu_read_unlock() to maintain the integrity of the pointer.
 */
struct dev_pm_opp *dev_pm_opp_find_freq_exact(struct device *dev,
					      unsigned long freq,
					      bool available)
{
	struct device_opp *dev_opp;
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	dev_opp = find_device_opp(dev);
	if (IS_ERR(dev_opp)) {
		int r = PTR_ERR(dev_opp);
		dev_err(dev, "%s: device OPP not found (%d)\n", __func__, r);
		return ERR_PTR(r);
	}

	list_for_each_entry_rcu(temp_opp, &dev_opp->opp_list, node) {
		if (temp_opp->available == available &&
				temp_opp->rate == freq) {
			opp = temp_opp;
			break;
		}
	}

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_freq_exact);

/**
 * dev_pm_opp_find_freq_ceil() - Search for an rounded ceil freq
 * @dev:	device for which we do this operation
 * @freq:	Start frequency
 *
 * Search for the matching ceil *available* OPP from a starting freq
 * for a device.
 *
 * Returns matching *opp and refreshes *freq accordingly, else returns
 * ERR_PTR in case of error and should be handled using IS_ERR. Error return
 * values can be:
 * EINVAL:	for bad pointer
 * ERANGE:	no match found for search
 * ENODEV:	if device not found in list of registered devices
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. The reason for the same is that the opp pointer which is
 * returned will remain valid for use with opp_get_{voltage, freq} only while
 * under the locked area. The pointer returned must be used prior to unlocking
 * with rcu_read_unlock() to maintain the integrity of the pointer.
 */
struct dev_pm_opp *dev_pm_opp_find_freq_ceil(struct device *dev,
					     unsigned long *freq)
{
	struct device_opp *dev_opp;
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	if (!dev || !freq) {
		dev_err(dev, "%s: Invalid argument freq=%p\n", __func__, freq);
		return ERR_PTR(-EINVAL);
	}

	dev_opp = find_device_opp(dev);
	if (IS_ERR(dev_opp))
		return ERR_CAST(dev_opp);

	list_for_each_entry_rcu(temp_opp, &dev_opp->opp_list, node) {
		if (temp_opp->available && temp_opp->rate >= *freq) {
			opp = temp_opp;
			*freq = opp->rate;
			break;
		}
	}

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_freq_ceil);

/**
 * dev_pm_opp_find_freq_floor() - Search for a rounded floor freq
 * @dev:	device for which we do this operation
 * @freq:	Start frequency
 *
 * Search for the matching floor *available* OPP from a starting freq
 * for a device.
 *
 * Returns matching *opp and refreshes *freq accordingly, else returns
 * ERR_PTR in case of error and should be handled using IS_ERR. Error return
 * values can be:
 * EINVAL:	for bad pointer
 * ERANGE:	no match found for search
 * ENODEV:	if device not found in list of registered devices
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. The reason for the same is that the opp pointer which is
 * returned will remain valid for use with opp_get_{voltage, freq} only while
 * under the locked area. The pointer returned must be used prior to unlocking
 * with rcu_read_unlock() to maintain the integrity of the pointer.
 */
struct dev_pm_opp *dev_pm_opp_find_freq_floor(struct device *dev,
					      unsigned long *freq)
{
	struct device_opp *dev_opp;
	struct dev_pm_opp *temp_opp, *opp = ERR_PTR(-ERANGE);

	if (!dev || !freq) {
		dev_err(dev, "%s: Invalid argument freq=%p\n", __func__, freq);
		return ERR_PTR(-EINVAL);
	}

	dev_opp = find_device_opp(dev);
	if (IS_ERR(dev_opp))
		return ERR_CAST(dev_opp);

	list_for_each_entry_rcu(temp_opp, &dev_opp->opp_list, node) {
		if (temp_opp->available) {
			/* go to the next node, before choosing prev */
			if (temp_opp->rate > *freq)
				break;
			else
				opp = temp_opp;
		}
	}
	if (!IS_ERR(opp))
		*freq = opp->rate;

	return opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_find_freq_floor);

static int dev_pm_opp_add_dynamic(struct device *dev, unsigned long freq,
				  unsigned long u_volt, bool dynamic)
{
	struct device_opp *dev_opp = NULL;
	struct dev_pm_opp *opp, *new_opp;
	struct list_head *head;

	/* allocate new OPP node */
	new_opp = kzalloc(sizeof(*new_opp), GFP_KERNEL);
	if (!new_opp) {
		dev_warn(dev, "%s: Unable to create new OPP node\n", __func__);
		return -ENOMEM;
	}

	/* Hold our list modification lock here */
	mutex_lock(&dev_opp_list_lock);

	/* populate the opp table */
	new_opp->rate = freq;
	new_opp->u_volt = u_volt;
	new_opp->available = true;
	new_opp->dynamic = dynamic;

	/* Check for existing list for 'dev' */
	dev_opp = find_device_opp(dev);
	if (IS_ERR(dev_opp)) {
		/*
		 * Allocate a new device OPP table. In the infrequent case
		 * where a new device is needed to be added, we pay this
		 * penalty.
		 */
		dev_opp = kzalloc(sizeof(struct device_opp), GFP_KERNEL);
		if (!dev_opp) {
			mutex_unlock(&dev_opp_list_lock);
			kfree(new_opp);
			dev_warn(dev,
				"%s: Unable to create device OPP structure\n",
				__func__);
			return -ENOMEM;
		}

		dev_opp->dev = dev;
		srcu_init_notifier_head(&dev_opp->srcu_head);
		INIT_LIST_HEAD(&dev_opp->opp_list);

		/* Secure the device list modification */
		list_add_rcu(&dev_opp->node, &dev_opp_list);
		head = &dev_opp->opp_list;
		goto list_add;
	}

	/*
	 * Insert new OPP in order of increasing frequency
	 * and discard if already present
	 */
	head = &dev_opp->opp_list;
	list_for_each_entry_rcu(opp, &dev_opp->opp_list, node) {
		if (new_opp->rate <= opp->rate)
			break;
		else
			head = &opp->node;
	}

	/* Duplicate OPPs ? */
	if (new_opp->rate == opp->rate) {
		int ret = opp->available && new_opp->u_volt == opp->u_volt ?
			0 : -EEXIST;

		dev_warn(dev, "%s: duplicate OPPs detected. Existing: freq: %lu, volt: %lu, enabled: %d. New: freq: %lu, volt: %lu, enabled: %d\n",
			 __func__, opp->rate, opp->u_volt, opp->available,
			 new_opp->rate, new_opp->u_volt, new_opp->available);
		mutex_unlock(&dev_opp_list_lock);
		kfree(new_opp);
		return ret;
	}

list_add:
	new_opp->dev_opp = dev_opp;
	list_add_rcu(&new_opp->node, head);
	mutex_unlock(&dev_opp_list_lock);

	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	srcu_notifier_call_chain(&dev_opp->srcu_head, OPP_EVENT_ADD, new_opp);
	return 0;
}

/**
 * dev_pm_opp_add()  - Add an OPP table from a table definitions
 * @dev:	device for which we do this operation
 * @freq:	Frequency in Hz for this OPP
 * @u_volt:	Voltage in uVolts for this OPP
 *
 * This function adds an opp definition to the opp list and returns status.
 * The opp is made available by default and it can be controlled using
 * dev_pm_opp_enable/disable functions.
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 *
 * Return:
 * 0:		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST:	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM:	Memory allocation failure
 */
int dev_pm_opp_add(struct device *dev, unsigned long freq, unsigned long u_volt)
{
	return dev_pm_opp_add_dynamic(dev, freq, u_volt, true);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_add);

static void kfree_opp_rcu(struct rcu_head *head)
{
	struct dev_pm_opp *opp = container_of(head, struct dev_pm_opp, rcu_head);

	kfree_rcu(opp, rcu_head);
}

static void kfree_device_rcu(struct rcu_head *head)
{
	struct device_opp *device_opp = container_of(head, struct device_opp, rcu_head);

	kfree(device_opp);
}

void __dev_pm_opp_remove(struct device_opp *dev_opp, struct dev_pm_opp *opp)
{
	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	srcu_notifier_call_chain(&dev_opp->srcu_head, OPP_EVENT_REMOVE, opp);
	list_del_rcu(&opp->node);
	call_srcu(&dev_opp->srcu_head.srcu, &opp->rcu_head, kfree_opp_rcu);

	if (list_empty(&dev_opp->opp_list)) {
		list_del_rcu(&dev_opp->node);
		call_srcu(&dev_opp->srcu_head.srcu, &dev_opp->rcu_head,
			  kfree_device_rcu);
	}
}

/**
 * dev_pm_opp_remove()  - Remove an OPP from OPP list
 * @dev:	device for which we do this operation
 * @freq:	OPP to remove with matching 'freq'
 *
 * This function removes an opp from the opp list.
 */
void dev_pm_opp_remove(struct device *dev, unsigned long freq)
{
	struct dev_pm_opp *opp;
	struct device_opp *dev_opp;
	bool found = false;

	/* Hold our list modification lock here */
	mutex_lock(&dev_opp_list_lock);

	dev_opp = find_device_opp(dev);
	if (IS_ERR(dev_opp))
		goto unlock;

	list_for_each_entry(opp, &dev_opp->opp_list, node) {
		if (opp->rate == freq) {
			found = true;
			break;
		}
	}

	if (!found) {
		dev_warn(dev, "%s: Couldn't find OPP with freq: %lu\n",
			 __func__, freq);
		goto unlock;
	}

	__dev_pm_opp_remove(dev_opp, opp);
unlock:
	mutex_unlock(&dev_opp_list_lock);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_remove);

/**
 * opp_set_availability() - helper to set the availability of an opp
 * @dev:		device for which we do this operation
 * @freq:		OPP frequency to modify availability
 * @availability_req:	availability status requested for this opp
 *
 * Set the availability of an OPP with an RCU operation, opp_{enable,disable}
 * share a common logic which is isolated here.
 *
 * Returns -EINVAL for bad pointers, -ENOMEM if no memory available for the
 * copy operation, returns 0 if no modifcation was done OR modification was
 * successful.
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks to
 * keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex locking or synchronize_rcu() blocking calls cannot be used.
 */
static int opp_set_availability(struct device *dev, unsigned long freq,
		bool availability_req)
{
	struct device_opp *tmp_dev_opp, *dev_opp = ERR_PTR(-ENODEV);
	struct dev_pm_opp *new_opp, *tmp_opp, *opp = ERR_PTR(-ENODEV);
	int r = 0;

	/* keep the node allocated */
	new_opp = kmalloc(sizeof(*new_opp), GFP_KERNEL);
	if (!new_opp) {
		dev_warn(dev, "%s: Unable to create OPP\n", __func__);
		return -ENOMEM;
	}

	mutex_lock(&dev_opp_list_lock);

	/* Find the device_opp */
	list_for_each_entry(tmp_dev_opp, &dev_opp_list, node) {
		if (dev == tmp_dev_opp->dev) {
			dev_opp = tmp_dev_opp;
			break;
		}
	}
	if (IS_ERR(dev_opp)) {
		r = PTR_ERR(dev_opp);
		dev_warn(dev, "%s: Device OPP not found (%d)\n", __func__, r);
		goto unlock;
	}

	/* Do we have the frequency? */
	list_for_each_entry(tmp_opp, &dev_opp->opp_list, node) {
		if (tmp_opp->rate == freq) {
			opp = tmp_opp;
			break;
		}
	}
	if (IS_ERR(opp)) {
		r = PTR_ERR(opp);
		goto unlock;
	}

	/* Is update really needed? */
	if (opp->available == availability_req)
		goto unlock;
	/* copy the old data over */
	*new_opp = *opp;

	/* plug in new node */
	new_opp->available = availability_req;

	list_replace_rcu(&opp->node, &new_opp->node);
	mutex_unlock(&dev_opp_list_lock);
	call_srcu(&dev_opp->srcu_head.srcu, &opp->rcu_head, kfree_opp_rcu);

	/* Notify the change of the OPP availability */
	if (availability_req)
		srcu_notifier_call_chain(&dev_opp->srcu_head, OPP_EVENT_ENABLE,
					 new_opp);
	else
		srcu_notifier_call_chain(&dev_opp->srcu_head, OPP_EVENT_DISABLE,
					 new_opp);

	return 0;

unlock:
	mutex_unlock(&dev_opp_list_lock);
	kfree(new_opp);
	return r;
}

/**
 * dev_pm_opp_enable() - Enable a specific OPP
 * @dev:	device for which we do this operation
 * @freq:	OPP frequency to enable
 *
 * Enables a provided opp. If the operation is valid, this returns 0, else the
 * corresponding error value. It is meant to be used for users an OPP available
 * after being temporarily made unavailable with dev_pm_opp_disable.
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Hence this function indirectly uses RCU and mutex locks to keep the
 * integrity of the internal data structures. Callers should ensure that
 * this function is *NOT* called under RCU protection or in contexts where
 * mutex locking or synchronize_rcu() blocking calls cannot be used.
 */
int dev_pm_opp_enable(struct device *dev, unsigned long freq)
{
	return opp_set_availability(dev, freq, true);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_enable);

/**
 * dev_pm_opp_disable() - Disable a specific OPP
 * @dev:	device for which we do this operation
 * @freq:	OPP frequency to disable
 *
 * Disables a provided opp. If the operation is valid, this returns
 * 0, else the corresponding error value. It is meant to be a temporary
 * control by users to make this OPP not available until the circumstances are
 * right to make it available again (with a call to dev_pm_opp_enable).
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Hence this function indirectly uses RCU and mutex locks to keep the
 * integrity of the internal data structures. Callers should ensure that
 * this function is *NOT* called under RCU protection or in contexts where
 * mutex locking or synchronize_rcu() blocking calls cannot be used.
 */
int dev_pm_opp_disable(struct device *dev, unsigned long freq)
{
	return opp_set_availability(dev, freq, false);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_disable);

/**
 * dev_pm_opp_get_notifier() - find notifier_head of the device with opp
 * @dev:	device pointer used to lookup device OPPs.
 */
struct srcu_notifier_head *dev_pm_opp_get_notifier(struct device *dev)
{
	struct device_opp *dev_opp = find_device_opp(dev);

	if (IS_ERR(dev_opp))
		return ERR_CAST(dev_opp); /* matching type */

	return &dev_opp->srcu_head;
}

#ifdef CONFIG_OF
/**
 * of_init_opp_table() - Initialize opp table from device tree
 * @dev:	device pointer used to lookup device OPPs.
 *
 * Register the initial OPP table with the OPP library for given device.
 */
int of_init_opp_table(struct device *dev)
{
	const struct property *prop;
	const __be32 *val;
	int nr;

	prop = of_find_property(dev->of_node, "operating-points", NULL);
	if (!prop)
		return -ENODEV;
	if (!prop->value)
		return -ENODATA;

	/*
	 * Each OPP is a set of tuples consisting of frequency and
	 * voltage like <freq-kHz vol-uV>.
	 */
	nr = prop->length / sizeof(u32);
	if (nr % 2) {
		dev_err(dev, "%s: Invalid OPP list\n", __func__);
		return -EINVAL;
	}

	val = prop->value;
	while (nr) {
		unsigned long freq = be32_to_cpup(val++) * 1000;
		unsigned long volt = be32_to_cpup(val++);

		if (dev_pm_opp_add_dynamic(dev, freq, volt, false))
			dev_warn(dev, "%s: Failed to add OPP %ld\n",
				 __func__, freq);
		nr -= 2;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(of_init_opp_table);

/**
 * of_free_opp_table() - Free OPP table entries created from static DT entries
 * @dev:	device pointer used to lookup device OPPs.
 *
 * Free OPPs created using static entries present in DT.
 */
void of_free_opp_table(struct device *dev)
{
	struct device_opp *dev_opp;
	struct dev_pm_opp *opp, *tmp;

	/* Check for existing list for 'dev' */
	dev_opp = find_device_opp(dev);
	if (WARN(IS_ERR(dev_opp), "%s: dev_opp: %ld\n", dev_name(dev),
		 PTR_ERR(dev_opp)))
		return;

	/* Hold our list modification lock here */
	mutex_lock(&dev_opp_list_lock);

	/* Free static OPPs */
	list_for_each_entry_safe(opp, tmp, &dev_opp->opp_list, node) {
		if (!opp->dynamic)
			__dev_pm_opp_remove(dev_opp, opp);
	}

	mutex_unlock(&dev_opp_list_lock);
}
EXPORT_SYMBOL_GPL(of_free_opp_table);
#endif
