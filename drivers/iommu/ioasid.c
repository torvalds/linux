// SPDX-License-Identifier: GPL-2.0
/*
 * I/O Address Space ID allocator. There is one global IOASID space, split into
 * subsets. Users create a subset with DECLARE_IOASID_SET, then allocate and
 * free IOASIDs with ioasid_alloc and ioasid_free.
 */
#include <linux/ioasid.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/xarray.h>

struct ioasid_data {
	ioasid_t id;
	struct ioasid_set *set;
	void *private;
	struct rcu_head rcu;
};

static DEFINE_XARRAY_ALLOC(ioasid_xa);

/**
 * ioasid_set_data - Set private data for an allocated ioasid
 * @ioasid: the ID to set data
 * @data:   the private data
 *
 * For IOASID that is already allocated, private data can be set
 * via this API. Future lookup can be done via ioasid_find.
 */
int ioasid_set_data(ioasid_t ioasid, void *data)
{
	struct ioasid_data *ioasid_data;
	int ret = 0;

	xa_lock(&ioasid_xa);
	ioasid_data = xa_load(&ioasid_xa, ioasid);
	if (ioasid_data)
		rcu_assign_pointer(ioasid_data->private, data);
	else
		ret = -ENOENT;
	xa_unlock(&ioasid_xa);

	/*
	 * Wait for readers to stop accessing the old private data, so the
	 * caller can free it.
	 */
	if (!ret)
		synchronize_rcu();

	return ret;
}
EXPORT_SYMBOL_GPL(ioasid_set_data);

/**
 * ioasid_alloc - Allocate an IOASID
 * @set: the IOASID set
 * @min: the minimum ID (inclusive)
 * @max: the maximum ID (inclusive)
 * @private: data private to the caller
 *
 * Allocate an ID between @min and @max. The @private pointer is stored
 * internally and can be retrieved with ioasid_find().
 *
 * Return: the allocated ID on success, or %INVALID_IOASID on failure.
 */
ioasid_t ioasid_alloc(struct ioasid_set *set, ioasid_t min, ioasid_t max,
		      void *private)
{
	struct ioasid_data *data;
	ioasid_t id;

	data = kzalloc(sizeof(*data), GFP_ATOMIC);
	if (!data)
		return INVALID_IOASID;

	data->set = set;
	data->private = private;

	if (xa_alloc(&ioasid_xa, &id, data, XA_LIMIT(min, max), GFP_KERNEL)) {
		pr_err("Failed to alloc ioasid from %d to %d\n", min, max);
		goto exit_free;
	}
	data->id = id;

	return id;
exit_free:
	kfree(data);
	return INVALID_IOASID;
}
EXPORT_SYMBOL_GPL(ioasid_alloc);

/**
 * ioasid_free - Free an IOASID
 * @ioasid: the ID to remove
 */
void ioasid_free(ioasid_t ioasid)
{
	struct ioasid_data *ioasid_data;

	ioasid_data = xa_erase(&ioasid_xa, ioasid);

	kfree_rcu(ioasid_data, rcu);
}
EXPORT_SYMBOL_GPL(ioasid_free);

/**
 * ioasid_find - Find IOASID data
 * @set: the IOASID set
 * @ioasid: the IOASID to find
 * @getter: function to call on the found object
 *
 * The optional getter function allows to take a reference to the found object
 * under the rcu lock. The function can also check if the object is still valid:
 * if @getter returns false, then the object is invalid and NULL is returned.
 *
 * If the IOASID exists, return the private pointer passed to ioasid_alloc.
 * Private data can be NULL if not set. Return an error if the IOASID is not
 * found, or if @set is not NULL and the IOASID does not belong to the set.
 */
void *ioasid_find(struct ioasid_set *set, ioasid_t ioasid,
		  bool (*getter)(void *))
{
	void *priv;
	struct ioasid_data *ioasid_data;

	rcu_read_lock();
	ioasid_data = xa_load(&ioasid_xa, ioasid);
	if (!ioasid_data) {
		priv = ERR_PTR(-ENOENT);
		goto unlock;
	}
	if (set && ioasid_data->set != set) {
		/* data found but does not belong to the set */
		priv = ERR_PTR(-EACCES);
		goto unlock;
	}
	/* Now IOASID and its set is verified, we can return the private data */
	priv = rcu_dereference(ioasid_data->private);
	if (getter && !getter(priv))
		priv = NULL;
unlock:
	rcu_read_unlock();

	return priv;
}
EXPORT_SYMBOL_GPL(ioasid_find);

MODULE_AUTHOR("Jean-Philippe Brucker <jean-philippe.brucker@arm.com>");
MODULE_AUTHOR("Jacob Pan <jacob.jun.pan@linux.intel.com>");
MODULE_DESCRIPTION("IO Address Space ID (IOASID) allocator");
MODULE_LICENSE("GPL");
