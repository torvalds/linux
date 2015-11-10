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

#include <linux/cpu.h>
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
 * @turbo:	true if turbo (boost) OPP
 * @rate:	Frequency in hertz
 * @u_volt:	Target voltage in microvolts corresponding to this OPP
 * @u_volt_min:	Minimum voltage in microvolts corresponding to this OPP
 * @u_volt_max:	Maximum voltage in microvolts corresponding to this OPP
 * @u_amp:	Maximum current drawn by the device in microamperes
 * @clock_latency_ns: Latency (in nanoseconds) of switching to this OPP's
 *		frequency from any other OPP's frequency.
 * @dev_opp:	points back to the device_opp struct this opp belongs to
 * @rcu_head:	RCU callback head used for deferred freeing
 * @np:		OPP's device node.
 *
 * This structure stores the OPP information for a given device.
 */
struct dev_pm_opp {
	struct list_head node;

	bool available;
	bool dynamic;
	bool turbo;
	unsigned long rate;

	unsigned long u_volt;
	unsigned long u_volt_min;
	unsigned long u_volt_max;
	unsigned long u_amp;
	unsigned long clock_latency_ns;

	struct device_opp *dev_opp;
	struct rcu_head rcu_head;

	struct device_node *np;
};

/**
 * struct device_list_opp - devices managed by 'struct device_opp'
 * @node:	list node
 * @dev:	device to which the struct object belongs
 * @rcu_head:	RCU callback head used for deferred freeing
 *
 * This is an internal data structure maintaining the list of devices that are
 * managed by 'struct device_opp'.
 */
struct device_list_opp {
	struct list_head node;
	const struct device *dev;
	struct rcu_head rcu_head;
};

/**
 * struct device_opp - Device opp structure
 * @node:	list node - contains the devices with OPPs that
 *		have been registered. Nodes once added are not modified in this
 *		list.
 *		RCU usage: nodes are not modified in the list of device_opp,
 *		however addition is possible and is secured by dev_opp_list_lock
 * @srcu_head:	notifier head to notify the OPP availability changes.
 * @rcu_head:	RCU callback head used for deferred freeing
 * @dev_list:	list of devices that share these OPPs
 * @opp_list:	list of opps
 * @np:		struct device_node pointer for opp's DT node.
 * @shared_opp: OPP is shared between multiple devices.
 *
 * This is an internal data structure maintaining the link to opps attached to
 * a device. This structure is not meant to be shared to users as it is
 * meant for book keeping and private to OPP library.
 *
 * Because the opp structures can be used from both rcu and srcu readers, we
 * need to wait for the grace period of both of them before freeing any
 * resources. And so we have used kfree_rcu() from within call_srcu() handlers.
 */
struct device_opp {
	struct list_head node;

	struct srcu_notifier_head srcu_head;
	struct rcu_head rcu_head;
	struct list_head dev_list;
	struct list_head opp_list;

	struct device_node *np;
	unsigned long clock_latency_ns_max;
	bool shared_opp;
	struct dev_pm_opp *suspend_opp;
};

/*
 * The root of the list of all devices. All device_opp structures branch off
 * from here, with each device_opp containing the list of opp it supports in
 * various states of availability.
 */
static LIST_HEAD(dev_opp_list);
/* Lock to allow exclusive modification to the device and opp lists */
static DEFINE_MUTEX(dev_opp_list_lock);

#define opp_rcu_lockdep_assert()					\
do {									\
	RCU_LOCKDEP_WARN(!rcu_read_lock_held() &&			\
				!lockdep_is_held(&dev_opp_list_lock),	\
			   "Missing rcu_read_lock() or "		\
			   "dev_opp_list_lock protection");		\
} while (0)

static struct device_list_opp *_find_list_dev(const struct device *dev,
					      struct device_opp *dev_opp)
{
	struct device_list_opp *list_dev;

	list_for_each_entry(list_dev, &dev_opp->dev_list, node)
		if (list_dev->dev == dev)
			return list_dev;

	return NULL;
}

static struct device_opp *_managed_opp(const struct device_node *np)
{
	struct device_opp *dev_opp;

	list_for_each_entry_rcu(dev_opp, &dev_opp_list, node) {
		if (dev_opp->np == np) {
			/*
			 * Multiple devices can point to the same OPP table and
			 * so will have same node-pointer, np.
			 *
			 * But the OPPs will be considered as shared only if the
			 * OPP table contains a "opp-shared" property.
			 */
			return dev_opp->shared_opp ? dev_opp : NULL;
		}
	}

	return NULL;
}

/**
 * _find_device_opp() - find device_opp struct using device pointer
 * @dev:	device pointer used to lookup device OPPs
 *
 * Search list of device OPPs for one containing matching device. Does a RCU
 * reader operation to grab the pointer needed.
 *
 * Return: pointer to 'struct device_opp' if found, otherwise -ENODEV or
 * -EINVAL based on type of error.
 *
 * Locking: This function must be called under rcu_read_lock(). device_opp
 * is a RCU protected pointer. This means that device_opp is valid as long
 * as we are under RCU lock.
 */
static struct device_opp *_find_device_opp(struct device *dev)
{
	struct device_opp *dev_opp;

	if (IS_ERR_OR_NULL(dev)) {
		pr_err("%s: Invalid parameters\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	list_for_each_entry_rcu(dev_opp, &dev_opp_list, node)
		if (_find_list_dev(dev, dev_opp))
			return dev_opp;

	return ERR_PTR(-ENODEV);
}

/**
 * dev_pm_opp_get_voltage() - Gets the voltage corresponding to an available opp
 * @opp:	opp for which voltage has to be returned for
 *
 * Return: voltage in micro volt corresponding to the opp, else
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

	opp_rcu_lockdep_assert();

	tmp_opp = rcu_dereference(opp);
	if (IS_ERR_OR_NULL(tmp_opp) || !tmp_opp->available)
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
 * Return: frequency in hertz corresponding to the opp, else
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

	opp_rcu_lockdep_assert();

	tmp_opp = rcu_dereference(opp);
	if (IS_ERR_OR_NULL(tmp_opp) || !tmp_opp->available)
		pr_err("%s: Invalid parameters\n", __func__);
	else
		f = tmp_opp->rate;

	return f;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_freq);

/**
 * dev_pm_opp_is_turbo() - Returns if opp is turbo OPP or not
 * @opp: opp for which turbo mode is being verified
 *
 * Turbo OPPs are not for normal use, and can be enabled (under certain
 * conditions) for short duration of times to finish high throughput work
 * quickly. Running on them for longer times may overheat the chip.
 *
 * Return: true if opp is turbo opp, else false.
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. This means that opp which could have been fetched by
 * opp_find_freq_{exact,ceil,floor} functions is valid as long as we are
 * under RCU lock. The pointer returned by the opp_find_freq family must be
 * used in the same section as the usage of this function with the pointer
 * prior to unlocking with rcu_read_unlock() to maintain the integrity of the
 * pointer.
 */
bool dev_pm_opp_is_turbo(struct dev_pm_opp *opp)
{
	struct dev_pm_opp *tmp_opp;

	opp_rcu_lockdep_assert();

	tmp_opp = rcu_dereference(opp);
	if (IS_ERR_OR_NULL(tmp_opp) || !tmp_opp->available) {
		pr_err("%s: Invalid parameters\n", __func__);
		return false;
	}

	return tmp_opp->turbo;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_is_turbo);

/**
 * dev_pm_opp_get_max_clock_latency() - Get max clock latency in nanoseconds
 * @dev:	device for which we do this operation
 *
 * Return: This function returns the max clock latency in nanoseconds.
 *
 * Locking: This function takes rcu_read_lock().
 */
unsigned long dev_pm_opp_get_max_clock_latency(struct device *dev)
{
	struct device_opp *dev_opp;
	unsigned long clock_latency_ns;

	rcu_read_lock();

	dev_opp = _find_device_opp(dev);
	if (IS_ERR(dev_opp))
		clock_latency_ns = 0;
	else
		clock_latency_ns = dev_opp->clock_latency_ns_max;

	rcu_read_unlock();
	return clock_latency_ns;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_max_clock_latency);

/**
 * dev_pm_opp_get_suspend_opp() - Get suspend opp
 * @dev:	device for which we do this operation
 *
 * Return: This function returns pointer to the suspend opp if it is
 * defined and available, otherwise it returns NULL.
 *
 * Locking: This function must be called under rcu_read_lock(). opp is a rcu
 * protected pointer. The reason for the same is that the opp pointer which is
 * returned will remain valid for use with opp_get_{voltage, freq} only while
 * under the locked area. The pointer returned must be used prior to unlocking
 * with rcu_read_unlock() to maintain the integrity of the pointer.
 */
struct dev_pm_opp *dev_pm_opp_get_suspend_opp(struct device *dev)
{
	struct device_opp *dev_opp;

	opp_rcu_lockdep_assert();

	dev_opp = _find_device_opp(dev);
	if (IS_ERR(dev_opp) || !dev_opp->suspend_opp ||
	    !dev_opp->suspend_opp->available)
		return NULL;

	return dev_opp->suspend_opp;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_suspend_opp);

/**
 * dev_pm_opp_get_opp_count() - Get number of opps available in the opp list
 * @dev:	device for which we do this operation
 *
 * Return: This function returns the number of available opps if there are any,
 * else returns 0 if none or the corresponding error value.
 *
 * Locking: This function takes rcu_read_lock().
 */
int dev_pm_opp_get_opp_count(struct device *dev)
{
	struct device_opp *dev_opp;
	struct dev_pm_opp *temp_opp;
	int count = 0;

	rcu_read_lock();

	dev_opp = _find_device_opp(dev);
	if (IS_ERR(dev_opp)) {
		count = PTR_ERR(dev_opp);
		dev_err(dev, "%s: device OPP not found (%d)\n",
			__func__, count);
		goto out_unlock;
	}

	list_for_each_entry_rcu(temp_opp, &dev_opp->opp_list, node) {
		if (temp_opp->available)
			count++;
	}

out_unlock:
	rcu_read_unlock();
	return count;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_opp_count);

/**
 * dev_pm_opp_find_freq_exact() - search for an exact frequency
 * @dev:		device for which we do this operation
 * @freq:		frequency to search for
 * @available:		true/false - match for available opp
 *
 * Return: Searches for exact match in the opp list and returns pointer to the
 * matching opp if found, else returns ERR_PTR in case of error and should
 * be handled using IS_ERR. Error return values can be:
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

	opp_rcu_lockdep_assert();

	dev_opp = _find_device_opp(dev);
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
 * Return: matching *opp and refreshes *freq accordingly, else returns
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

	opp_rcu_lockdep_assert();

	if (!dev || !freq) {
		dev_err(dev, "%s: Invalid argument freq=%p\n", __func__, freq);
		return ERR_PTR(-EINVAL);
	}

	dev_opp = _find_device_opp(dev);
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
 * Return: matching *opp and refreshes *freq accordingly, else returns
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

	opp_rcu_lockdep_assert();

	if (!dev || !freq) {
		dev_err(dev, "%s: Invalid argument freq=%p\n", __func__, freq);
		return ERR_PTR(-EINVAL);
	}

	dev_opp = _find_device_opp(dev);
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

/* List-dev Helpers */
static void _kfree_list_dev_rcu(struct rcu_head *head)
{
	struct device_list_opp *list_dev;

	list_dev = container_of(head, struct device_list_opp, rcu_head);
	kfree_rcu(list_dev, rcu_head);
}

static void _remove_list_dev(struct device_list_opp *list_dev,
			     struct device_opp *dev_opp)
{
	list_del(&list_dev->node);
	call_srcu(&dev_opp->srcu_head.srcu, &list_dev->rcu_head,
		  _kfree_list_dev_rcu);
}

static struct device_list_opp *_add_list_dev(const struct device *dev,
					     struct device_opp *dev_opp)
{
	struct device_list_opp *list_dev;

	list_dev = kzalloc(sizeof(*list_dev), GFP_KERNEL);
	if (!list_dev)
		return NULL;

	/* Initialize list-dev */
	list_dev->dev = dev;
	list_add_rcu(&list_dev->node, &dev_opp->dev_list);

	return list_dev;
}

/**
 * _add_device_opp() - Find device OPP table or allocate a new one
 * @dev:	device for which we do this operation
 *
 * It tries to find an existing table first, if it couldn't find one, it
 * allocates a new OPP table and returns that.
 *
 * Return: valid device_opp pointer if success, else NULL.
 */
static struct device_opp *_add_device_opp(struct device *dev)
{
	struct device_opp *dev_opp;
	struct device_list_opp *list_dev;

	/* Check for existing list for 'dev' first */
	dev_opp = _find_device_opp(dev);
	if (!IS_ERR(dev_opp))
		return dev_opp;

	/*
	 * Allocate a new device OPP table. In the infrequent case where a new
	 * device is needed to be added, we pay this penalty.
	 */
	dev_opp = kzalloc(sizeof(*dev_opp), GFP_KERNEL);
	if (!dev_opp)
		return NULL;

	INIT_LIST_HEAD(&dev_opp->dev_list);

	list_dev = _add_list_dev(dev, dev_opp);
	if (!list_dev) {
		kfree(dev_opp);
		return NULL;
	}

	srcu_init_notifier_head(&dev_opp->srcu_head);
	INIT_LIST_HEAD(&dev_opp->opp_list);

	/* Secure the device list modification */
	list_add_rcu(&dev_opp->node, &dev_opp_list);
	return dev_opp;
}

/**
 * _kfree_device_rcu() - Free device_opp RCU handler
 * @head:	RCU head
 */
static void _kfree_device_rcu(struct rcu_head *head)
{
	struct device_opp *device_opp = container_of(head, struct device_opp, rcu_head);

	kfree_rcu(device_opp, rcu_head);
}

/**
 * _remove_device_opp() - Removes a device OPP table
 * @dev_opp: device OPP table to be removed.
 *
 * Removes/frees device OPP table it it doesn't contain any OPPs.
 */
static void _remove_device_opp(struct device_opp *dev_opp)
{
	struct device_list_opp *list_dev;

	if (!list_empty(&dev_opp->opp_list))
		return;

	list_dev = list_first_entry(&dev_opp->dev_list, struct device_list_opp,
				    node);

	_remove_list_dev(list_dev, dev_opp);

	/* dev_list must be empty now */
	WARN_ON(!list_empty(&dev_opp->dev_list));

	list_del_rcu(&dev_opp->node);
	call_srcu(&dev_opp->srcu_head.srcu, &dev_opp->rcu_head,
		  _kfree_device_rcu);
}

/**
 * _kfree_opp_rcu() - Free OPP RCU handler
 * @head:	RCU head
 */
static void _kfree_opp_rcu(struct rcu_head *head)
{
	struct dev_pm_opp *opp = container_of(head, struct dev_pm_opp, rcu_head);

	kfree_rcu(opp, rcu_head);
}

/**
 * _opp_remove()  - Remove an OPP from a table definition
 * @dev_opp:	points back to the device_opp struct this opp belongs to
 * @opp:	pointer to the OPP to remove
 * @notify:	OPP_EVENT_REMOVE notification should be sent or not
 *
 * This function removes an opp definition from the opp list.
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * It is assumed that the caller holds required mutex for an RCU updater
 * strategy.
 */
static void _opp_remove(struct device_opp *dev_opp,
			struct dev_pm_opp *opp, bool notify)
{
	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	if (notify)
		srcu_notifier_call_chain(&dev_opp->srcu_head, OPP_EVENT_REMOVE, opp);
	list_del_rcu(&opp->node);
	call_srcu(&dev_opp->srcu_head.srcu, &opp->rcu_head, _kfree_opp_rcu);

	_remove_device_opp(dev_opp);
}

/**
 * dev_pm_opp_remove()  - Remove an OPP from OPP list
 * @dev:	device for which we do this operation
 * @freq:	OPP to remove with matching 'freq'
 *
 * This function removes an opp from the opp list.
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
void dev_pm_opp_remove(struct device *dev, unsigned long freq)
{
	struct dev_pm_opp *opp;
	struct device_opp *dev_opp;
	bool found = false;

	/* Hold our list modification lock here */
	mutex_lock(&dev_opp_list_lock);

	dev_opp = _find_device_opp(dev);
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

	_opp_remove(dev_opp, opp, true);
unlock:
	mutex_unlock(&dev_opp_list_lock);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_remove);

static struct dev_pm_opp *_allocate_opp(struct device *dev,
					struct device_opp **dev_opp)
{
	struct dev_pm_opp *opp;

	/* allocate new OPP node */
	opp = kzalloc(sizeof(*opp), GFP_KERNEL);
	if (!opp)
		return NULL;

	INIT_LIST_HEAD(&opp->node);

	*dev_opp = _add_device_opp(dev);
	if (!*dev_opp) {
		kfree(opp);
		return NULL;
	}

	return opp;
}

static int _opp_add(struct device *dev, struct dev_pm_opp *new_opp,
		    struct device_opp *dev_opp)
{
	struct dev_pm_opp *opp;
	struct list_head *head = &dev_opp->opp_list;

	/*
	 * Insert new OPP in order of increasing frequency and discard if
	 * already present.
	 *
	 * Need to use &dev_opp->opp_list in the condition part of the 'for'
	 * loop, don't replace it with head otherwise it will become an infinite
	 * loop.
	 */
	list_for_each_entry_rcu(opp, &dev_opp->opp_list, node) {
		if (new_opp->rate > opp->rate) {
			head = &opp->node;
			continue;
		}

		if (new_opp->rate < opp->rate)
			break;

		/* Duplicate OPPs */
		dev_warn(dev, "%s: duplicate OPPs detected. Existing: freq: %lu, volt: %lu, enabled: %d. New: freq: %lu, volt: %lu, enabled: %d\n",
			 __func__, opp->rate, opp->u_volt, opp->available,
			 new_opp->rate, new_opp->u_volt, new_opp->available);

		return opp->available && new_opp->u_volt == opp->u_volt ?
			0 : -EEXIST;
	}

	new_opp->dev_opp = dev_opp;
	list_add_rcu(&new_opp->node, head);

	return 0;
}

/**
 * _opp_add_dynamic() - Allocate a dynamic OPP.
 * @dev:	device for which we do this operation
 * @freq:	Frequency in Hz for this OPP
 * @u_volt:	Voltage in uVolts for this OPP
 * @dynamic:	Dynamically added OPPs.
 *
 * This function adds an opp definition to the opp list and returns status.
 * The opp is made available by default and it can be controlled using
 * dev_pm_opp_enable/disable functions and may be removed by dev_pm_opp_remove.
 *
 * NOTE: "dynamic" parameter impacts OPPs added by the of_init_opp_table and
 * freed by of_free_opp_table.
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 *
 * Return:
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 */
static int _opp_add_dynamic(struct device *dev, unsigned long freq,
			    long u_volt, bool dynamic)
{
	struct device_opp *dev_opp;
	struct dev_pm_opp *new_opp;
	int ret;

	/* Hold our list modification lock here */
	mutex_lock(&dev_opp_list_lock);

	new_opp = _allocate_opp(dev, &dev_opp);
	if (!new_opp) {
		ret = -ENOMEM;
		goto unlock;
	}

	/* populate the opp table */
	new_opp->rate = freq;
	new_opp->u_volt = u_volt;
	new_opp->available = true;
	new_opp->dynamic = dynamic;

	ret = _opp_add(dev, new_opp, dev_opp);
	if (ret)
		goto free_opp;

	mutex_unlock(&dev_opp_list_lock);

	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	srcu_notifier_call_chain(&dev_opp->srcu_head, OPP_EVENT_ADD, new_opp);
	return 0;

free_opp:
	_opp_remove(dev_opp, new_opp, false);
unlock:
	mutex_unlock(&dev_opp_list_lock);
	return ret;
}

/* TODO: Support multiple regulators */
static int opp_get_microvolt(struct dev_pm_opp *opp, struct device *dev)
{
	u32 microvolt[3] = {0};
	int count, ret;

	/* Missing property isn't a problem, but an invalid entry is */
	if (!of_find_property(opp->np, "opp-microvolt", NULL))
		return 0;

	count = of_property_count_u32_elems(opp->np, "opp-microvolt");
	if (count < 0) {
		dev_err(dev, "%s: Invalid opp-microvolt property (%d)\n",
			__func__, count);
		return count;
	}

	/* There can be one or three elements here */
	if (count != 1 && count != 3) {
		dev_err(dev, "%s: Invalid number of elements in opp-microvolt property (%d)\n",
			__func__, count);
		return -EINVAL;
	}

	ret = of_property_read_u32_array(opp->np, "opp-microvolt", microvolt,
					 count);
	if (ret) {
		dev_err(dev, "%s: error parsing opp-microvolt: %d\n", __func__,
			ret);
		return -EINVAL;
	}

	opp->u_volt = microvolt[0];
	opp->u_volt_min = microvolt[1];
	opp->u_volt_max = microvolt[2];

	return 0;
}

/**
 * _opp_add_static_v2() - Allocate static OPPs (As per 'v2' DT bindings)
 * @dev:	device for which we do this operation
 * @np:		device node
 *
 * This function adds an opp definition to the opp list and returns status. The
 * opp can be controlled using dev_pm_opp_enable/disable functions and may be
 * removed by dev_pm_opp_remove.
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 *
 * Return:
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 * -EINVAL	Failed parsing the OPP node
 */
static int _opp_add_static_v2(struct device *dev, struct device_node *np)
{
	struct device_opp *dev_opp;
	struct dev_pm_opp *new_opp;
	u64 rate;
	u32 val;
	int ret;

	/* Hold our list modification lock here */
	mutex_lock(&dev_opp_list_lock);

	new_opp = _allocate_opp(dev, &dev_opp);
	if (!new_opp) {
		ret = -ENOMEM;
		goto unlock;
	}

	ret = of_property_read_u64(np, "opp-hz", &rate);
	if (ret < 0) {
		dev_err(dev, "%s: opp-hz not found\n", __func__);
		goto free_opp;
	}

	/*
	 * Rate is defined as an unsigned long in clk API, and so casting
	 * explicitly to its type. Must be fixed once rate is 64 bit
	 * guaranteed in clk API.
	 */
	new_opp->rate = (unsigned long)rate;
	new_opp->turbo = of_property_read_bool(np, "turbo-mode");

	new_opp->np = np;
	new_opp->dynamic = false;
	new_opp->available = true;

	if (!of_property_read_u32(np, "clock-latency-ns", &val))
		new_opp->clock_latency_ns = val;

	ret = opp_get_microvolt(new_opp, dev);
	if (ret)
		goto free_opp;

	if (!of_property_read_u32(new_opp->np, "opp-microamp", &val))
		new_opp->u_amp = val;

	ret = _opp_add(dev, new_opp, dev_opp);
	if (ret)
		goto free_opp;

	/* OPP to select on device suspend */
	if (of_property_read_bool(np, "opp-suspend")) {
		if (dev_opp->suspend_opp)
			dev_warn(dev, "%s: Multiple suspend OPPs found (%lu %lu)\n",
				 __func__, dev_opp->suspend_opp->rate,
				 new_opp->rate);
		else
			dev_opp->suspend_opp = new_opp;
	}

	if (new_opp->clock_latency_ns > dev_opp->clock_latency_ns_max)
		dev_opp->clock_latency_ns_max = new_opp->clock_latency_ns;

	mutex_unlock(&dev_opp_list_lock);

	pr_debug("%s: turbo:%d rate:%lu uv:%lu uvmin:%lu uvmax:%lu latency:%lu\n",
		 __func__, new_opp->turbo, new_opp->rate, new_opp->u_volt,
		 new_opp->u_volt_min, new_opp->u_volt_max,
		 new_opp->clock_latency_ns);

	/*
	 * Notify the changes in the availability of the operable
	 * frequency/voltage list.
	 */
	srcu_notifier_call_chain(&dev_opp->srcu_head, OPP_EVENT_ADD, new_opp);
	return 0;

free_opp:
	_opp_remove(dev_opp, new_opp, false);
unlock:
	mutex_unlock(&dev_opp_list_lock);
	return ret;
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
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 */
int dev_pm_opp_add(struct device *dev, unsigned long freq, unsigned long u_volt)
{
	return _opp_add_dynamic(dev, freq, u_volt, true);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_add);

/**
 * _opp_set_availability() - helper to set the availability of an opp
 * @dev:		device for which we do this operation
 * @freq:		OPP frequency to modify availability
 * @availability_req:	availability status requested for this opp
 *
 * Set the availability of an OPP with an RCU operation, opp_{enable,disable}
 * share a common logic which is isolated here.
 *
 * Return: -EINVAL for bad pointers, -ENOMEM if no memory available for the
 * copy operation, returns 0 if no modification was done OR modification was
 * successful.
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Hence this function internally uses RCU updater strategy with mutex locks to
 * keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex locking or synchronize_rcu() blocking calls cannot be used.
 */
static int _opp_set_availability(struct device *dev, unsigned long freq,
				 bool availability_req)
{
	struct device_opp *dev_opp;
	struct dev_pm_opp *new_opp, *tmp_opp, *opp = ERR_PTR(-ENODEV);
	int r = 0;

	/* keep the node allocated */
	new_opp = kmalloc(sizeof(*new_opp), GFP_KERNEL);
	if (!new_opp)
		return -ENOMEM;

	mutex_lock(&dev_opp_list_lock);

	/* Find the device_opp */
	dev_opp = _find_device_opp(dev);
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
	call_srcu(&dev_opp->srcu_head.srcu, &opp->rcu_head, _kfree_opp_rcu);

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
 *
 * Return: -EINVAL for bad pointers, -ENOMEM if no memory available for the
 * copy operation, returns 0 if no modification was done OR modification was
 * successful.
 */
int dev_pm_opp_enable(struct device *dev, unsigned long freq)
{
	return _opp_set_availability(dev, freq, true);
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
 *
 * Return: -EINVAL for bad pointers, -ENOMEM if no memory available for the
 * copy operation, returns 0 if no modification was done OR modification was
 * successful.
 */
int dev_pm_opp_disable(struct device *dev, unsigned long freq)
{
	return _opp_set_availability(dev, freq, false);
}
EXPORT_SYMBOL_GPL(dev_pm_opp_disable);

/**
 * dev_pm_opp_get_notifier() - find notifier_head of the device with opp
 * @dev:	device pointer used to lookup device OPPs.
 *
 * Return: pointer to  notifier head if found, otherwise -ENODEV or
 * -EINVAL based on type of error casted as pointer. value must be checked
 *  with IS_ERR to determine valid pointer or error result.
 *
 * Locking: This function must be called under rcu_read_lock(). dev_opp is a RCU
 * protected pointer. The reason for the same is that the opp pointer which is
 * returned will remain valid for use with opp_get_{voltage, freq} only while
 * under the locked area. The pointer returned must be used prior to unlocking
 * with rcu_read_unlock() to maintain the integrity of the pointer.
 */
struct srcu_notifier_head *dev_pm_opp_get_notifier(struct device *dev)
{
	struct device_opp *dev_opp = _find_device_opp(dev);

	if (IS_ERR(dev_opp))
		return ERR_CAST(dev_opp); /* matching type */

	return &dev_opp->srcu_head;
}
EXPORT_SYMBOL_GPL(dev_pm_opp_get_notifier);

#ifdef CONFIG_OF
/**
 * of_free_opp_table() - Free OPP table entries created from static DT entries
 * @dev:	device pointer used to lookup device OPPs.
 *
 * Free OPPs created using static entries present in DT.
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Hence this function indirectly uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 */
void of_free_opp_table(struct device *dev)
{
	struct device_opp *dev_opp;
	struct dev_pm_opp *opp, *tmp;

	/* Hold our list modification lock here */
	mutex_lock(&dev_opp_list_lock);

	/* Check for existing list for 'dev' */
	dev_opp = _find_device_opp(dev);
	if (IS_ERR(dev_opp)) {
		int error = PTR_ERR(dev_opp);

		if (error != -ENODEV)
			WARN(1, "%s: dev_opp: %d\n",
			     IS_ERR_OR_NULL(dev) ?
					"Invalid device" : dev_name(dev),
			     error);
		goto unlock;
	}

	/* Find if dev_opp manages a single device */
	if (list_is_singular(&dev_opp->dev_list)) {
		/* Free static OPPs */
		list_for_each_entry_safe(opp, tmp, &dev_opp->opp_list, node) {
			if (!opp->dynamic)
				_opp_remove(dev_opp, opp, true);
		}
	} else {
		_remove_list_dev(_find_list_dev(dev, dev_opp), dev_opp);
	}

unlock:
	mutex_unlock(&dev_opp_list_lock);
}
EXPORT_SYMBOL_GPL(of_free_opp_table);

void of_cpumask_free_opp_table(cpumask_var_t cpumask)
{
	struct device *cpu_dev;
	int cpu;

	WARN_ON(cpumask_empty(cpumask));

	for_each_cpu(cpu, cpumask) {
		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_err("%s: failed to get cpu%d device\n", __func__,
			       cpu);
			continue;
		}

		of_free_opp_table(cpu_dev);
	}
}
EXPORT_SYMBOL_GPL(of_cpumask_free_opp_table);

/* Returns opp descriptor node from its phandle. Caller must do of_node_put() */
static struct device_node *
_of_get_opp_desc_node_from_prop(struct device *dev, const struct property *prop)
{
	struct device_node *opp_np;

	opp_np = of_find_node_by_phandle(be32_to_cpup(prop->value));
	if (!opp_np) {
		dev_err(dev, "%s: Prop: %s contains invalid opp desc phandle\n",
			__func__, prop->name);
		return ERR_PTR(-EINVAL);
	}

	return opp_np;
}

/* Returns opp descriptor node for a device. Caller must do of_node_put() */
static struct device_node *_of_get_opp_desc_node(struct device *dev)
{
	const struct property *prop;

	prop = of_find_property(dev->of_node, "operating-points-v2", NULL);
	if (!prop)
		return ERR_PTR(-ENODEV);
	if (!prop->value)
		return ERR_PTR(-ENODATA);

	/*
	 * TODO: Support for multiple OPP tables.
	 *
	 * There should be only ONE phandle present in "operating-points-v2"
	 * property.
	 */
	if (prop->length != sizeof(__be32)) {
		dev_err(dev, "%s: Invalid opp desc phandle\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	return _of_get_opp_desc_node_from_prop(dev, prop);
}

/* Initializes OPP tables based on new bindings */
static int _of_init_opp_table_v2(struct device *dev,
				 const struct property *prop)
{
	struct device_node *opp_np, *np;
	struct device_opp *dev_opp;
	int ret = 0, count = 0;

	if (!prop->value)
		return -ENODATA;

	/* Get opp node */
	opp_np = _of_get_opp_desc_node_from_prop(dev, prop);
	if (IS_ERR(opp_np))
		return PTR_ERR(opp_np);

	dev_opp = _managed_opp(opp_np);
	if (dev_opp) {
		/* OPPs are already managed */
		if (!_add_list_dev(dev, dev_opp))
			ret = -ENOMEM;
		goto put_opp_np;
	}

	/* We have opp-list node now, iterate over it and add OPPs */
	for_each_available_child_of_node(opp_np, np) {
		count++;

		ret = _opp_add_static_v2(dev, np);
		if (ret) {
			dev_err(dev, "%s: Failed to add OPP, %d\n", __func__,
				ret);
			goto free_table;
		}
	}

	/* There should be one of more OPP defined */
	if (WARN_ON(!count)) {
		ret = -ENOENT;
		goto put_opp_np;
	}

	dev_opp = _find_device_opp(dev);
	if (WARN_ON(IS_ERR(dev_opp))) {
		ret = PTR_ERR(dev_opp);
		goto free_table;
	}

	dev_opp->np = opp_np;
	dev_opp->shared_opp = of_property_read_bool(opp_np, "opp-shared");

	of_node_put(opp_np);
	return 0;

free_table:
	of_free_opp_table(dev);
put_opp_np:
	of_node_put(opp_np);

	return ret;
}

/* Initializes OPP tables based on old-deprecated bindings */
static int _of_init_opp_table_v1(struct device *dev)
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

		if (_opp_add_dynamic(dev, freq, volt, false))
			dev_warn(dev, "%s: Failed to add OPP %ld\n",
				 __func__, freq);
		nr -= 2;
	}

	return 0;
}

/**
 * of_init_opp_table() - Initialize opp table from device tree
 * @dev:	device pointer used to lookup device OPPs.
 *
 * Register the initial OPP table with the OPP library for given device.
 *
 * Locking: The internal device_opp and opp structures are RCU protected.
 * Hence this function indirectly uses RCU updater strategy with mutex locks
 * to keep the integrity of the internal data structures. Callers should ensure
 * that this function is *NOT* called under RCU protection or in contexts where
 * mutex cannot be locked.
 *
 * Return:
 * 0		On success OR
 *		Duplicate OPPs (both freq and volt are same) and opp->available
 * -EEXIST	Freq are same and volt are different OR
 *		Duplicate OPPs (both freq and volt are same) and !opp->available
 * -ENOMEM	Memory allocation failure
 * -ENODEV	when 'operating-points' property is not found or is invalid data
 *		in device node.
 * -ENODATA	when empty 'operating-points' property is found
 * -EINVAL	when invalid entries are found in opp-v2 table
 */
int of_init_opp_table(struct device *dev)
{
	const struct property *prop;

	/*
	 * OPPs have two version of bindings now. The older one is deprecated,
	 * try for the new binding first.
	 */
	prop = of_find_property(dev->of_node, "operating-points-v2", NULL);
	if (!prop) {
		/*
		 * Try old-deprecated bindings for backward compatibility with
		 * older dtbs.
		 */
		return _of_init_opp_table_v1(dev);
	}

	return _of_init_opp_table_v2(dev, prop);
}
EXPORT_SYMBOL_GPL(of_init_opp_table);

int of_cpumask_init_opp_table(cpumask_var_t cpumask)
{
	struct device *cpu_dev;
	int cpu, ret = 0;

	WARN_ON(cpumask_empty(cpumask));

	for_each_cpu(cpu, cpumask) {
		cpu_dev = get_cpu_device(cpu);
		if (!cpu_dev) {
			pr_err("%s: failed to get cpu%d device\n", __func__,
			       cpu);
			continue;
		}

		ret = of_init_opp_table(cpu_dev);
		if (ret) {
			pr_err("%s: couldn't find opp table for cpu:%d, %d\n",
			       __func__, cpu, ret);

			/* Free all other OPPs */
			of_cpumask_free_opp_table(cpumask);
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(of_cpumask_init_opp_table);

/* Required only for V1 bindings, as v2 can manage it from DT itself */
int set_cpus_sharing_opps(struct device *cpu_dev, cpumask_var_t cpumask)
{
	struct device_list_opp *list_dev;
	struct device_opp *dev_opp;
	struct device *dev;
	int cpu, ret = 0;

	rcu_read_lock();

	dev_opp = _find_device_opp(cpu_dev);
	if (IS_ERR(dev_opp)) {
		ret = -EINVAL;
		goto out_rcu_read_unlock;
	}

	for_each_cpu(cpu, cpumask) {
		if (cpu == cpu_dev->id)
			continue;

		dev = get_cpu_device(cpu);
		if (!dev) {
			dev_err(cpu_dev, "%s: failed to get cpu%d device\n",
				__func__, cpu);
			continue;
		}

		list_dev = _add_list_dev(dev, dev_opp);
		if (!list_dev) {
			dev_err(dev, "%s: failed to add list-dev for cpu%d device\n",
				__func__, cpu);
			continue;
		}
	}
out_rcu_read_unlock:
	rcu_read_unlock();

	return 0;
}
EXPORT_SYMBOL_GPL(set_cpus_sharing_opps);

/*
 * Works only for OPP v2 bindings.
 *
 * cpumask should be already set to mask of cpu_dev->id.
 * Returns -ENOENT if operating-points-v2 bindings aren't supported.
 */
int of_get_cpus_sharing_opps(struct device *cpu_dev, cpumask_var_t cpumask)
{
	struct device_node *np, *tmp_np;
	struct device *tcpu_dev;
	int cpu, ret = 0;

	/* Get OPP descriptor node */
	np = _of_get_opp_desc_node(cpu_dev);
	if (IS_ERR(np)) {
		dev_dbg(cpu_dev, "%s: Couldn't find opp node: %ld\n", __func__,
			PTR_ERR(np));
		return -ENOENT;
	}

	/* OPPs are shared ? */
	if (!of_property_read_bool(np, "opp-shared"))
		goto put_cpu_node;

	for_each_possible_cpu(cpu) {
		if (cpu == cpu_dev->id)
			continue;

		tcpu_dev = get_cpu_device(cpu);
		if (!tcpu_dev) {
			dev_err(cpu_dev, "%s: failed to get cpu%d device\n",
				__func__, cpu);
			ret = -ENODEV;
			goto put_cpu_node;
		}

		/* Get OPP descriptor node */
		tmp_np = _of_get_opp_desc_node(tcpu_dev);
		if (IS_ERR(tmp_np)) {
			dev_err(tcpu_dev, "%s: Couldn't find opp node: %ld\n",
				__func__, PTR_ERR(tmp_np));
			ret = PTR_ERR(tmp_np);
			goto put_cpu_node;
		}

		/* CPUs are sharing opp node */
		if (np == tmp_np)
			cpumask_set_cpu(cpu, cpumask);

		of_node_put(tmp_np);
	}

put_cpu_node:
	of_node_put(np);
	return ret;
}
EXPORT_SYMBOL_GPL(of_get_cpus_sharing_opps);
#endif
