// SPDX-License-Identifier: GPL-2.0
/*
 *  dpll_core.c - DPLL subsystem kernel-space interface implementation.
 *
 *  Copyright (c) 2023 Meta Platforms, Inc. and affiliates
 *  Copyright (c) 2023 Intel Corporation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "dpll_core.h"
#include "dpll_netlink.h"

/* Mutex lock to protect DPLL subsystem devices and pins */
DEFINE_MUTEX(dpll_lock);

DEFINE_XARRAY_FLAGS(dpll_device_xa, XA_FLAGS_ALLOC);
DEFINE_XARRAY_FLAGS(dpll_pin_xa, XA_FLAGS_ALLOC);

static u32 dpll_device_xa_id;
static u32 dpll_pin_xa_id;

#define ASSERT_DPLL_REGISTERED(d)	\
	WARN_ON_ONCE(!xa_get_mark(&dpll_device_xa, (d)->id, DPLL_REGISTERED))
#define ASSERT_DPLL_NOT_REGISTERED(d)	\
	WARN_ON_ONCE(xa_get_mark(&dpll_device_xa, (d)->id, DPLL_REGISTERED))
#define ASSERT_DPLL_PIN_REGISTERED(p) \
	WARN_ON_ONCE(!xa_get_mark(&dpll_pin_xa, (p)->id, DPLL_REGISTERED))

struct dpll_device_registration {
	struct list_head list;
	const struct dpll_device_ops *ops;
	void *priv;
};

struct dpll_pin_registration {
	struct list_head list;
	const struct dpll_pin_ops *ops;
	void *priv;
	void *cookie;
};

struct dpll_device *dpll_device_get_by_id(int id)
{
	if (xa_get_mark(&dpll_device_xa, id, DPLL_REGISTERED))
		return xa_load(&dpll_device_xa, id);

	return NULL;
}

static struct dpll_pin_registration *
dpll_pin_registration_find(struct dpll_pin_ref *ref,
			   const struct dpll_pin_ops *ops, void *priv,
			   void *cookie)
{
	struct dpll_pin_registration *reg;

	list_for_each_entry(reg, &ref->registration_list, list) {
		if (reg->ops == ops && reg->priv == priv &&
		    reg->cookie == cookie)
			return reg;
	}
	return NULL;
}

static int
dpll_xa_ref_pin_add(struct xarray *xa_pins, struct dpll_pin *pin,
		    const struct dpll_pin_ops *ops, void *priv,
		    void *cookie)
{
	struct dpll_pin_registration *reg;
	struct dpll_pin_ref *ref;
	bool ref_exists = false;
	unsigned long i;
	int ret;

	xa_for_each(xa_pins, i, ref) {
		if (ref->pin != pin)
			continue;
		reg = dpll_pin_registration_find(ref, ops, priv, cookie);
		if (reg) {
			refcount_inc(&ref->refcount);
			return 0;
		}
		ref_exists = true;
		break;
	}

	if (!ref_exists) {
		ref = kzalloc(sizeof(*ref), GFP_KERNEL);
		if (!ref)
			return -ENOMEM;
		ref->pin = pin;
		INIT_LIST_HEAD(&ref->registration_list);
		ret = xa_insert(xa_pins, pin->pin_idx, ref, GFP_KERNEL);
		if (ret) {
			kfree(ref);
			return ret;
		}
		refcount_set(&ref->refcount, 1);
	}

	reg = kzalloc(sizeof(*reg), GFP_KERNEL);
	if (!reg) {
		if (!ref_exists) {
			xa_erase(xa_pins, pin->pin_idx);
			kfree(ref);
		}
		return -ENOMEM;
	}
	reg->ops = ops;
	reg->priv = priv;
	reg->cookie = cookie;
	if (ref_exists)
		refcount_inc(&ref->refcount);
	list_add_tail(&reg->list, &ref->registration_list);

	return 0;
}

static int dpll_xa_ref_pin_del(struct xarray *xa_pins, struct dpll_pin *pin,
			       const struct dpll_pin_ops *ops, void *priv,
			       void *cookie)
{
	struct dpll_pin_registration *reg;
	struct dpll_pin_ref *ref;
	unsigned long i;

	xa_for_each(xa_pins, i, ref) {
		if (ref->pin != pin)
			continue;
		reg = dpll_pin_registration_find(ref, ops, priv, cookie);
		if (WARN_ON(!reg))
			return -EINVAL;
		list_del(&reg->list);
		kfree(reg);
		if (refcount_dec_and_test(&ref->refcount)) {
			xa_erase(xa_pins, i);
			WARN_ON(!list_empty(&ref->registration_list));
			kfree(ref);
		}
		return 0;
	}

	return -EINVAL;
}

static int
dpll_xa_ref_dpll_add(struct xarray *xa_dplls, struct dpll_device *dpll,
		     const struct dpll_pin_ops *ops, void *priv, void *cookie)
{
	struct dpll_pin_registration *reg;
	struct dpll_pin_ref *ref;
	bool ref_exists = false;
	unsigned long i;
	int ret;

	xa_for_each(xa_dplls, i, ref) {
		if (ref->dpll != dpll)
			continue;
		reg = dpll_pin_registration_find(ref, ops, priv, cookie);
		if (reg) {
			refcount_inc(&ref->refcount);
			return 0;
		}
		ref_exists = true;
		break;
	}

	if (!ref_exists) {
		ref = kzalloc(sizeof(*ref), GFP_KERNEL);
		if (!ref)
			return -ENOMEM;
		ref->dpll = dpll;
		INIT_LIST_HEAD(&ref->registration_list);
		ret = xa_insert(xa_dplls, dpll->id, ref, GFP_KERNEL);
		if (ret) {
			kfree(ref);
			return ret;
		}
		refcount_set(&ref->refcount, 1);
	}

	reg = kzalloc(sizeof(*reg), GFP_KERNEL);
	if (!reg) {
		if (!ref_exists) {
			xa_erase(xa_dplls, dpll->id);
			kfree(ref);
		}
		return -ENOMEM;
	}
	reg->ops = ops;
	reg->priv = priv;
	reg->cookie = cookie;
	if (ref_exists)
		refcount_inc(&ref->refcount);
	list_add_tail(&reg->list, &ref->registration_list);

	return 0;
}

static void
dpll_xa_ref_dpll_del(struct xarray *xa_dplls, struct dpll_device *dpll,
		     const struct dpll_pin_ops *ops, void *priv, void *cookie)
{
	struct dpll_pin_registration *reg;
	struct dpll_pin_ref *ref;
	unsigned long i;

	xa_for_each(xa_dplls, i, ref) {
		if (ref->dpll != dpll)
			continue;
		reg = dpll_pin_registration_find(ref, ops, priv, cookie);
		if (WARN_ON(!reg))
			return;
		list_del(&reg->list);
		kfree(reg);
		if (refcount_dec_and_test(&ref->refcount)) {
			xa_erase(xa_dplls, i);
			WARN_ON(!list_empty(&ref->registration_list));
			kfree(ref);
		}
		return;
	}
}

struct dpll_pin_ref *dpll_xa_ref_dpll_first(struct xarray *xa_refs)
{
	struct dpll_pin_ref *ref;
	unsigned long i = 0;

	ref = xa_find(xa_refs, &i, ULONG_MAX, XA_PRESENT);
	WARN_ON(!ref);
	return ref;
}

static struct dpll_device *
dpll_device_alloc(const u64 clock_id, u32 device_idx, struct module *module)
{
	struct dpll_device *dpll;
	int ret;

	dpll = kzalloc(sizeof(*dpll), GFP_KERNEL);
	if (!dpll)
		return ERR_PTR(-ENOMEM);
	refcount_set(&dpll->refcount, 1);
	INIT_LIST_HEAD(&dpll->registration_list);
	dpll->device_idx = device_idx;
	dpll->clock_id = clock_id;
	dpll->module = module;
	ret = xa_alloc_cyclic(&dpll_device_xa, &dpll->id, dpll, xa_limit_32b,
			      &dpll_device_xa_id, GFP_KERNEL);
	if (ret < 0) {
		kfree(dpll);
		return ERR_PTR(ret);
	}
	xa_init_flags(&dpll->pin_refs, XA_FLAGS_ALLOC);

	return dpll;
}

/**
 * dpll_device_get - find existing or create new dpll device
 * @clock_id: clock_id of creator
 * @device_idx: idx given by device driver
 * @module: reference to registering module
 *
 * Get existing object of a dpll device, unique for given arguments.
 * Create new if doesn't exist yet.
 *
 * Context: Acquires a lock (dpll_lock)
 * Return:
 * * valid dpll_device struct pointer if succeeded
 * * ERR_PTR(X) - error
 */
struct dpll_device *
dpll_device_get(u64 clock_id, u32 device_idx, struct module *module)
{
	struct dpll_device *dpll, *ret = NULL;
	unsigned long index;

	mutex_lock(&dpll_lock);
	xa_for_each(&dpll_device_xa, index, dpll) {
		if (dpll->clock_id == clock_id &&
		    dpll->device_idx == device_idx &&
		    dpll->module == module) {
			ret = dpll;
			refcount_inc(&ret->refcount);
			break;
		}
	}
	if (!ret)
		ret = dpll_device_alloc(clock_id, device_idx, module);
	mutex_unlock(&dpll_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dpll_device_get);

/**
 * dpll_device_put - decrease the refcount and free memory if possible
 * @dpll: dpll_device struct pointer
 *
 * Context: Acquires a lock (dpll_lock)
 * Drop reference for a dpll device, if all references are gone, delete
 * dpll device object.
 */
void dpll_device_put(struct dpll_device *dpll)
{
	mutex_lock(&dpll_lock);
	if (refcount_dec_and_test(&dpll->refcount)) {
		ASSERT_DPLL_NOT_REGISTERED(dpll);
		WARN_ON_ONCE(!xa_empty(&dpll->pin_refs));
		xa_destroy(&dpll->pin_refs);
		xa_erase(&dpll_device_xa, dpll->id);
		WARN_ON(!list_empty(&dpll->registration_list));
		kfree(dpll);
	}
	mutex_unlock(&dpll_lock);
}
EXPORT_SYMBOL_GPL(dpll_device_put);

static struct dpll_device_registration *
dpll_device_registration_find(struct dpll_device *dpll,
			      const struct dpll_device_ops *ops, void *priv)
{
	struct dpll_device_registration *reg;

	list_for_each_entry(reg, &dpll->registration_list, list) {
		if (reg->ops == ops && reg->priv == priv)
			return reg;
	}
	return NULL;
}

/**
 * dpll_device_register - register the dpll device in the subsystem
 * @dpll: pointer to a dpll
 * @type: type of a dpll
 * @ops: ops for a dpll device
 * @priv: pointer to private information of owner
 *
 * Make dpll device available for user space.
 *
 * Context: Acquires a lock (dpll_lock)
 * Return:
 * * 0 on success
 * * negative - error value
 */
int dpll_device_register(struct dpll_device *dpll, enum dpll_type type,
			 const struct dpll_device_ops *ops, void *priv)
{
	struct dpll_device_registration *reg;
	bool first_registration = false;

	if (WARN_ON(!ops))
		return -EINVAL;
	if (WARN_ON(!ops->mode_get))
		return -EINVAL;
	if (WARN_ON(!ops->lock_status_get))
		return -EINVAL;
	if (WARN_ON(type < DPLL_TYPE_PPS || type > DPLL_TYPE_MAX))
		return -EINVAL;

	mutex_lock(&dpll_lock);
	reg = dpll_device_registration_find(dpll, ops, priv);
	if (reg) {
		mutex_unlock(&dpll_lock);
		return -EEXIST;
	}

	reg = kzalloc(sizeof(*reg), GFP_KERNEL);
	if (!reg) {
		mutex_unlock(&dpll_lock);
		return -ENOMEM;
	}
	reg->ops = ops;
	reg->priv = priv;
	dpll->type = type;
	first_registration = list_empty(&dpll->registration_list);
	list_add_tail(&reg->list, &dpll->registration_list);
	if (!first_registration) {
		mutex_unlock(&dpll_lock);
		return 0;
	}

	xa_set_mark(&dpll_device_xa, dpll->id, DPLL_REGISTERED);
	dpll_device_create_ntf(dpll);
	mutex_unlock(&dpll_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(dpll_device_register);

/**
 * dpll_device_unregister - unregister dpll device
 * @dpll: registered dpll pointer
 * @ops: ops for a dpll device
 * @priv: pointer to private information of owner
 *
 * Unregister device, make it unavailable for userspace.
 * Note: It does not free the memory
 * Context: Acquires a lock (dpll_lock)
 */
void dpll_device_unregister(struct dpll_device *dpll,
			    const struct dpll_device_ops *ops, void *priv)
{
	struct dpll_device_registration *reg;

	mutex_lock(&dpll_lock);
	ASSERT_DPLL_REGISTERED(dpll);
	dpll_device_delete_ntf(dpll);
	reg = dpll_device_registration_find(dpll, ops, priv);
	if (WARN_ON(!reg)) {
		mutex_unlock(&dpll_lock);
		return;
	}
	list_del(&reg->list);
	kfree(reg);

	if (!list_empty(&dpll->registration_list)) {
		mutex_unlock(&dpll_lock);
		return;
	}
	xa_clear_mark(&dpll_device_xa, dpll->id, DPLL_REGISTERED);
	mutex_unlock(&dpll_lock);
}
EXPORT_SYMBOL_GPL(dpll_device_unregister);

static void dpll_pin_prop_free(struct dpll_pin_properties *prop)
{
	kfree(prop->package_label);
	kfree(prop->panel_label);
	kfree(prop->board_label);
	kfree(prop->freq_supported);
}

static int dpll_pin_prop_dup(const struct dpll_pin_properties *src,
			     struct dpll_pin_properties *dst)
{
	memcpy(dst, src, sizeof(*dst));
	if (src->freq_supported && src->freq_supported_num) {
		size_t freq_size = src->freq_supported_num *
				   sizeof(*src->freq_supported);
		dst->freq_supported = kmemdup(src->freq_supported,
					      freq_size, GFP_KERNEL);
		if (!dst->freq_supported)
			return -ENOMEM;
	}
	if (src->board_label) {
		dst->board_label = kstrdup(src->board_label, GFP_KERNEL);
		if (!dst->board_label)
			goto err_board_label;
	}
	if (src->panel_label) {
		dst->panel_label = kstrdup(src->panel_label, GFP_KERNEL);
		if (!dst->panel_label)
			goto err_panel_label;
	}
	if (src->package_label) {
		dst->package_label = kstrdup(src->package_label, GFP_KERNEL);
		if (!dst->package_label)
			goto err_package_label;
	}

	return 0;

err_package_label:
	kfree(dst->panel_label);
err_panel_label:
	kfree(dst->board_label);
err_board_label:
	kfree(dst->freq_supported);
	return -ENOMEM;
}

static struct dpll_pin *
dpll_pin_alloc(u64 clock_id, u32 pin_idx, struct module *module,
	       const struct dpll_pin_properties *prop)
{
	struct dpll_pin *pin;
	int ret;

	pin = kzalloc(sizeof(*pin), GFP_KERNEL);
	if (!pin)
		return ERR_PTR(-ENOMEM);
	pin->pin_idx = pin_idx;
	pin->clock_id = clock_id;
	pin->module = module;
	if (WARN_ON(prop->type < DPLL_PIN_TYPE_MUX ||
		    prop->type > DPLL_PIN_TYPE_MAX)) {
		ret = -EINVAL;
		goto err_pin_prop;
	}
	ret = dpll_pin_prop_dup(prop, &pin->prop);
	if (ret)
		goto err_pin_prop;
	refcount_set(&pin->refcount, 1);
	xa_init_flags(&pin->dpll_refs, XA_FLAGS_ALLOC);
	xa_init_flags(&pin->parent_refs, XA_FLAGS_ALLOC);
	ret = xa_alloc_cyclic(&dpll_pin_xa, &pin->id, pin, xa_limit_32b,
			      &dpll_pin_xa_id, GFP_KERNEL);
	if (ret)
		goto err_xa_alloc;
	return pin;
err_xa_alloc:
	xa_destroy(&pin->dpll_refs);
	xa_destroy(&pin->parent_refs);
	dpll_pin_prop_free(&pin->prop);
err_pin_prop:
	kfree(pin);
	return ERR_PTR(ret);
}

static void dpll_netdev_pin_assign(struct net_device *dev, struct dpll_pin *dpll_pin)
{
	rtnl_lock();
	rcu_assign_pointer(dev->dpll_pin, dpll_pin);
	rtnl_unlock();
}

void dpll_netdev_pin_set(struct net_device *dev, struct dpll_pin *dpll_pin)
{
	WARN_ON(!dpll_pin);
	dpll_netdev_pin_assign(dev, dpll_pin);
}
EXPORT_SYMBOL(dpll_netdev_pin_set);

void dpll_netdev_pin_clear(struct net_device *dev)
{
	dpll_netdev_pin_assign(dev, NULL);
}
EXPORT_SYMBOL(dpll_netdev_pin_clear);

/**
 * dpll_pin_get - find existing or create new dpll pin
 * @clock_id: clock_id of creator
 * @pin_idx: idx given by dev driver
 * @module: reference to registering module
 * @prop: dpll pin properties
 *
 * Get existing object of a pin (unique for given arguments) or create new
 * if doesn't exist yet.
 *
 * Context: Acquires a lock (dpll_lock)
 * Return:
 * * valid allocated dpll_pin struct pointer if succeeded
 * * ERR_PTR(X) - error
 */
struct dpll_pin *
dpll_pin_get(u64 clock_id, u32 pin_idx, struct module *module,
	     const struct dpll_pin_properties *prop)
{
	struct dpll_pin *pos, *ret = NULL;
	unsigned long i;

	mutex_lock(&dpll_lock);
	xa_for_each(&dpll_pin_xa, i, pos) {
		if (pos->clock_id == clock_id &&
		    pos->pin_idx == pin_idx &&
		    pos->module == module) {
			ret = pos;
			refcount_inc(&ret->refcount);
			break;
		}
	}
	if (!ret)
		ret = dpll_pin_alloc(clock_id, pin_idx, module, prop);
	mutex_unlock(&dpll_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dpll_pin_get);

/**
 * dpll_pin_put - decrease the refcount and free memory if possible
 * @pin: pointer to a pin to be put
 *
 * Drop reference for a pin, if all references are gone, delete pin object.
 *
 * Context: Acquires a lock (dpll_lock)
 */
void dpll_pin_put(struct dpll_pin *pin)
{
	mutex_lock(&dpll_lock);
	if (refcount_dec_and_test(&pin->refcount)) {
		xa_erase(&dpll_pin_xa, pin->id);
		xa_destroy(&pin->dpll_refs);
		xa_destroy(&pin->parent_refs);
		dpll_pin_prop_free(&pin->prop);
		kfree_rcu(pin, rcu);
	}
	mutex_unlock(&dpll_lock);
}
EXPORT_SYMBOL_GPL(dpll_pin_put);

static int
__dpll_pin_register(struct dpll_device *dpll, struct dpll_pin *pin,
		    const struct dpll_pin_ops *ops, void *priv, void *cookie)
{
	int ret;

	ret = dpll_xa_ref_pin_add(&dpll->pin_refs, pin, ops, priv, cookie);
	if (ret)
		return ret;
	ret = dpll_xa_ref_dpll_add(&pin->dpll_refs, dpll, ops, priv, cookie);
	if (ret)
		goto ref_pin_del;
	xa_set_mark(&dpll_pin_xa, pin->id, DPLL_REGISTERED);
	dpll_pin_create_ntf(pin);

	return ret;

ref_pin_del:
	dpll_xa_ref_pin_del(&dpll->pin_refs, pin, ops, priv, cookie);
	return ret;
}

/**
 * dpll_pin_register - register the dpll pin in the subsystem
 * @dpll: pointer to a dpll
 * @pin: pointer to a dpll pin
 * @ops: ops for a dpll pin ops
 * @priv: pointer to private information of owner
 *
 * Context: Acquires a lock (dpll_lock)
 * Return:
 * * 0 on success
 * * negative - error value
 */
int
dpll_pin_register(struct dpll_device *dpll, struct dpll_pin *pin,
		  const struct dpll_pin_ops *ops, void *priv)
{
	int ret;

	if (WARN_ON(!ops) ||
	    WARN_ON(!ops->state_on_dpll_get) ||
	    WARN_ON(!ops->direction_get))
		return -EINVAL;

	mutex_lock(&dpll_lock);
	if (WARN_ON(!(dpll->module == pin->module &&
		      dpll->clock_id == pin->clock_id)))
		ret = -EINVAL;
	else
		ret = __dpll_pin_register(dpll, pin, ops, priv, NULL);
	mutex_unlock(&dpll_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(dpll_pin_register);

static void
__dpll_pin_unregister(struct dpll_device *dpll, struct dpll_pin *pin,
		      const struct dpll_pin_ops *ops, void *priv, void *cookie)
{
	ASSERT_DPLL_PIN_REGISTERED(pin);
	dpll_xa_ref_pin_del(&dpll->pin_refs, pin, ops, priv, cookie);
	dpll_xa_ref_dpll_del(&pin->dpll_refs, dpll, ops, priv, cookie);
	if (xa_empty(&pin->dpll_refs))
		xa_clear_mark(&dpll_pin_xa, pin->id, DPLL_REGISTERED);
}

/**
 * dpll_pin_unregister - unregister dpll pin from dpll device
 * @dpll: registered dpll pointer
 * @pin: pointer to a pin
 * @ops: ops for a dpll pin
 * @priv: pointer to private information of owner
 *
 * Note: It does not free the memory
 * Context: Acquires a lock (dpll_lock)
 */
void dpll_pin_unregister(struct dpll_device *dpll, struct dpll_pin *pin,
			 const struct dpll_pin_ops *ops, void *priv)
{
	if (WARN_ON(xa_empty(&dpll->pin_refs)))
		return;
	if (WARN_ON(!xa_empty(&pin->parent_refs)))
		return;

	mutex_lock(&dpll_lock);
	dpll_pin_delete_ntf(pin);
	__dpll_pin_unregister(dpll, pin, ops, priv, NULL);
	mutex_unlock(&dpll_lock);
}
EXPORT_SYMBOL_GPL(dpll_pin_unregister);

/**
 * dpll_pin_on_pin_register - register a pin with a parent pin
 * @parent: pointer to a parent pin
 * @pin: pointer to a pin
 * @ops: ops for a dpll pin
 * @priv: pointer to private information of owner
 *
 * Register a pin with a parent pin, create references between them and
 * between newly registered pin and dplls connected with a parent pin.
 *
 * Context: Acquires a lock (dpll_lock)
 * Return:
 * * 0 on success
 * * negative - error value
 */
int dpll_pin_on_pin_register(struct dpll_pin *parent, struct dpll_pin *pin,
			     const struct dpll_pin_ops *ops, void *priv)
{
	struct dpll_pin_ref *ref;
	unsigned long i, stop;
	int ret;

	if (WARN_ON(parent->prop.type != DPLL_PIN_TYPE_MUX))
		return -EINVAL;

	if (WARN_ON(!ops) ||
	    WARN_ON(!ops->state_on_pin_get) ||
	    WARN_ON(!ops->direction_get))
		return -EINVAL;

	mutex_lock(&dpll_lock);
	ret = dpll_xa_ref_pin_add(&pin->parent_refs, parent, ops, priv, pin);
	if (ret)
		goto unlock;
	refcount_inc(&pin->refcount);
	xa_for_each(&parent->dpll_refs, i, ref) {
		ret = __dpll_pin_register(ref->dpll, pin, ops, priv, parent);
		if (ret) {
			stop = i;
			goto dpll_unregister;
		}
		dpll_pin_create_ntf(pin);
	}
	mutex_unlock(&dpll_lock);

	return ret;

dpll_unregister:
	xa_for_each(&parent->dpll_refs, i, ref)
		if (i < stop) {
			__dpll_pin_unregister(ref->dpll, pin, ops, priv,
					      parent);
			dpll_pin_delete_ntf(pin);
		}
	refcount_dec(&pin->refcount);
	dpll_xa_ref_pin_del(&pin->parent_refs, parent, ops, priv, pin);
unlock:
	mutex_unlock(&dpll_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(dpll_pin_on_pin_register);

/**
 * dpll_pin_on_pin_unregister - unregister dpll pin from a parent pin
 * @parent: pointer to a parent pin
 * @pin: pointer to a pin
 * @ops: ops for a dpll pin
 * @priv: pointer to private information of owner
 *
 * Context: Acquires a lock (dpll_lock)
 * Note: It does not free the memory
 */
void dpll_pin_on_pin_unregister(struct dpll_pin *parent, struct dpll_pin *pin,
				const struct dpll_pin_ops *ops, void *priv)
{
	struct dpll_pin_ref *ref;
	unsigned long i;

	mutex_lock(&dpll_lock);
	dpll_pin_delete_ntf(pin);
	dpll_xa_ref_pin_del(&pin->parent_refs, parent, ops, priv, pin);
	refcount_dec(&pin->refcount);
	xa_for_each(&pin->dpll_refs, i, ref)
		__dpll_pin_unregister(ref->dpll, pin, ops, priv, parent);
	mutex_unlock(&dpll_lock);
}
EXPORT_SYMBOL_GPL(dpll_pin_on_pin_unregister);

static struct dpll_device_registration *
dpll_device_registration_first(struct dpll_device *dpll)
{
	struct dpll_device_registration *reg;

	reg = list_first_entry_or_null((struct list_head *)&dpll->registration_list,
				       struct dpll_device_registration, list);
	WARN_ON(!reg);
	return reg;
}

void *dpll_priv(struct dpll_device *dpll)
{
	struct dpll_device_registration *reg;

	reg = dpll_device_registration_first(dpll);
	return reg->priv;
}

const struct dpll_device_ops *dpll_device_ops(struct dpll_device *dpll)
{
	struct dpll_device_registration *reg;

	reg = dpll_device_registration_first(dpll);
	return reg->ops;
}

static struct dpll_pin_registration *
dpll_pin_registration_first(struct dpll_pin_ref *ref)
{
	struct dpll_pin_registration *reg;

	reg = list_first_entry_or_null(&ref->registration_list,
				       struct dpll_pin_registration, list);
	WARN_ON(!reg);
	return reg;
}

void *dpll_pin_on_dpll_priv(struct dpll_device *dpll,
			    struct dpll_pin *pin)
{
	struct dpll_pin_registration *reg;
	struct dpll_pin_ref *ref;

	ref = xa_load(&dpll->pin_refs, pin->pin_idx);
	if (!ref)
		return NULL;
	reg = dpll_pin_registration_first(ref);
	return reg->priv;
}

void *dpll_pin_on_pin_priv(struct dpll_pin *parent,
			   struct dpll_pin *pin)
{
	struct dpll_pin_registration *reg;
	struct dpll_pin_ref *ref;

	ref = xa_load(&pin->parent_refs, parent->pin_idx);
	if (!ref)
		return NULL;
	reg = dpll_pin_registration_first(ref);
	return reg->priv;
}

const struct dpll_pin_ops *dpll_pin_ops(struct dpll_pin_ref *ref)
{
	struct dpll_pin_registration *reg;

	reg = dpll_pin_registration_first(ref);
	return reg->ops;
}

static int __init dpll_init(void)
{
	int ret;

	ret = genl_register_family(&dpll_nl_family);
	if (ret)
		goto error;

	return 0;

error:
	mutex_destroy(&dpll_lock);
	return ret;
}

static void __exit dpll_exit(void)
{
	genl_unregister_family(&dpll_nl_family);
	mutex_destroy(&dpll_lock);
}

subsys_initcall(dpll_init);
module_exit(dpll_exit);
