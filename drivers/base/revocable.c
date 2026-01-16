// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2026 Google LLC
 *
 * Revocable resource management
 */

#include <linux/device.h>
#include <linux/kref.h>
#include <linux/revocable.h>
#include <linux/slab.h>
#include <linux/srcu.h>

/**
 * DOC: Overview
 *
 * The "revocable" mechanism is a synchronization primitive designed to manage
 * safe access to resources that can be asynchronously removed or invalidated.
 * Its primary purpose is to prevent Use-After-Free (UAF) errors when
 * interacting with resources whose lifetimes are not guaranteed to outlast
 * their consumers.
 *
 * This is particularly useful in systems where resources can disappear
 * unexpectedly, such as those provided by hot-pluggable devices like USB.
 * When a consumer holds a reference to such a resource, the underlying device
 * might be removed, causing the resource's memory to be freed.  Subsequent
 * access attempts by the consumer would then lead to UAF errors.
 *
 * Revocable addresses this by providing a form of "weak reference" and a
 * controlled access method.  It allows a resource consumer to safely attempt to
 * access the resource.  The mechanism guarantees that any access granted is
 * valid for the duration of its use.  If the resource has already been
 * revoked (i.e., freed), the access attempt will fail safely, typically by
 * returning NULL, instead of causing a crash.
 *
 * The implementation uses a provider/consumer model built on Sleepable
 * RCU (SRCU) to guarantee safe memory access:
 *
 * - A resource provider, such as a driver for a hot-pluggable device,
 *   allocates a struct revocable_provider and initializes it with a pointer
 *   to the resource.
 *
 * - A resource consumer that wants to access the resource allocates a
 *   struct revocable which acts as a handle containing a reference to the
 *   provider.
 *
 * - To access the resource, the consumer uses revocable_try_access().
 *   This function enters an SRCU read-side critical section and returns
 *   the pointer to the resource.  If the provider has already freed the
 *   resource, it returns NULL.  After use, the consumer calls
 *   revocable_withdraw_access() to exit the SRCU critical section.  The
 *   REVOCABLE_TRY_ACCESS_WITH() and REVOCABLE_TRY_ACCESS_SCOPED() are
 *   convenient helpers for doing that.
 *
 * - When the provider needs to remove the resource, it calls
 *   revocable_provider_revoke().  This function sets the internal resource
 *   pointer to NULL and then calls synchronize_srcu() to wait for all
 *   current readers to finish before the resource can be completely torn
 *   down.
 */

/**
 * struct revocable_provider - A handle for resource provider.
 * @srcu: The SRCU to protect the resource.
 * @res:  The pointer of resource.  It can point to anything.
 * @kref: The refcount for this handle.
 */
struct revocable_provider {
	struct srcu_struct srcu;
	void __rcu *res;
	struct kref kref;
};

/**
 * struct revocable - A handle for resource consumer.
 * @rp: The pointer of resource provider.
 * @idx: The index for the RCU critical section.
 */
struct revocable {
	struct revocable_provider *rp;
	int idx;
};

/**
 * revocable_provider_alloc() - Allocate struct revocable_provider.
 * @res: The pointer of resource.
 *
 * This holds an initial refcount to the struct.
 *
 * Return: The pointer of struct revocable_provider.  NULL on errors.
 */
struct revocable_provider *revocable_provider_alloc(void *res)
{
	struct revocable_provider *rp;

	rp = kzalloc(sizeof(*rp), GFP_KERNEL);
	if (!rp)
		return NULL;

	init_srcu_struct(&rp->srcu);
	rcu_assign_pointer(rp->res, res);
	synchronize_srcu(&rp->srcu);
	kref_init(&rp->kref);

	return rp;
}
EXPORT_SYMBOL_GPL(revocable_provider_alloc);

static void revocable_provider_release(struct kref *kref)
{
	struct revocable_provider *rp = container_of(kref,
			struct revocable_provider, kref);

	cleanup_srcu_struct(&rp->srcu);
	kfree(rp);
}

/**
 * revocable_provider_revoke() - Revoke the managed resource.
 * @rp: The pointer of resource provider.
 *
 * This sets the resource `(struct revocable_provider *)->res` to NULL to
 * indicate the resource has gone.
 *
 * This drops the refcount to the resource provider.  If it is the final
 * reference, revocable_provider_release() will be called to free the struct.
 */
void revocable_provider_revoke(struct revocable_provider *rp)
{
	rcu_assign_pointer(rp->res, NULL);
	synchronize_srcu(&rp->srcu);
	kref_put(&rp->kref, revocable_provider_release);
}
EXPORT_SYMBOL_GPL(revocable_provider_revoke);

static void devm_revocable_provider_revoke(void *data)
{
	struct revocable_provider *rp = data;

	revocable_provider_revoke(rp);
}

/**
 * devm_revocable_provider_alloc() - Dev-managed revocable_provider_alloc().
 * @dev: The device.
 * @res: The pointer of resource.
 *
 * It is convenient to allocate providers via this function if the @res is
 * also tied to the lifetime of the @dev.  revocable_provider_revoke() will
 * be called automatically when the device is unbound.
 *
 * This holds an initial refcount to the struct.
 *
 * Return: The pointer of struct revocable_provider.  NULL on errors.
 */
struct revocable_provider *devm_revocable_provider_alloc(struct device *dev,
							 void *res)
{
	struct revocable_provider *rp;

	rp = revocable_provider_alloc(res);
	if (!rp)
		return NULL;

	if (devm_add_action_or_reset(dev, devm_revocable_provider_revoke, rp))
		return NULL;

	return rp;
}
EXPORT_SYMBOL_GPL(devm_revocable_provider_alloc);

/**
 * revocable_alloc() - Allocate struct revocable.
 * @rp: The pointer of resource provider.
 *
 * This holds a refcount to the resource provider.
 *
 * Return: The pointer of struct revocable.  NULL on errors.
 */
struct revocable *revocable_alloc(struct revocable_provider *rp)
{
	struct revocable *rev;

	rev = kzalloc(sizeof(*rev), GFP_KERNEL);
	if (!rev)
		return NULL;

	rev->rp = rp;
	kref_get(&rp->kref);

	return rev;
}
EXPORT_SYMBOL_GPL(revocable_alloc);

/**
 * revocable_free() - Free struct revocable.
 * @rev: The pointer of struct revocable.
 *
 * This drops a refcount to the resource provider.  If it is the final
 * reference, revocable_provider_release() will be called to free the struct.
 */
void revocable_free(struct revocable *rev)
{
	struct revocable_provider *rp = rev->rp;

	kref_put(&rp->kref, revocable_provider_release);
	kfree(rev);
}
EXPORT_SYMBOL_GPL(revocable_free);

/**
 * revocable_try_access() - Try to access the resource.
 * @rev: The pointer of struct revocable.
 *
 * This tries to de-reference to the resource and enters a RCU critical
 * section.
 *
 * Return: The pointer to the resource.  NULL if the resource has gone.
 */
void *revocable_try_access(struct revocable *rev) __acquires(&rev->rp->srcu)
{
	struct revocable_provider *rp = rev->rp;

	rev->idx = srcu_read_lock(&rp->srcu);
	return srcu_dereference(rp->res, &rp->srcu);
}
EXPORT_SYMBOL_GPL(revocable_try_access);

/**
 * revocable_withdraw_access() - Stop accessing to the resource.
 * @rev: The pointer of struct revocable.
 *
 * Call this function to indicate the resource is no longer used.  It exits
 * the RCU critical section.
 */
void revocable_withdraw_access(struct revocable *rev) __releases(&rev->rp->srcu)
{
	struct revocable_provider *rp = rev->rp;

	srcu_read_unlock(&rp->srcu, rev->idx);
}
EXPORT_SYMBOL_GPL(revocable_withdraw_access);
