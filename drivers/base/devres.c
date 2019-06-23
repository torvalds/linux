// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/base/devres.c - device resource management
 *
 * Copyright (c) 2006  SUSE Linux Products GmbH
 * Copyright (c) 2006  Tejun Heo <teheo@suse.de>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/percpu.h>

#include <asm/sections.h>

#include "base.h"

struct devres_node {
	struct list_head		entry;
	dr_release_t			release;
#ifdef CONFIG_DEBUG_DEVRES
	const char			*name;
	size_t				size;
#endif
};

struct devres {
	struct devres_node		node;
	/*
	 * Some archs want to perform DMA into kmalloc caches
	 * and need a guaranteed alignment larger than
	 * the alignment of a 64-bit integer.
	 * Thus we use ARCH_KMALLOC_MINALIGN here and get exactly the same
	 * buffer alignment as if it was allocated by plain kmalloc().
	 */
	u8 __aligned(ARCH_KMALLOC_MINALIGN) data[];
};

struct devres_group {
	struct devres_node		node[2];
	void				*id;
	int				color;
	/* -- 8 pointers */
};

#ifdef CONFIG_DEBUG_DEVRES
static int log_devres = 0;
module_param_named(log, log_devres, int, S_IRUGO | S_IWUSR);

static void set_node_dbginfo(struct devres_node *node, const char *name,
			     size_t size)
{
	node->name = name;
	node->size = size;
}

static void devres_log(struct device *dev, struct devres_node *node,
		       const char *op)
{
	if (unlikely(log_devres))
		dev_err(dev, "DEVRES %3s %p %s (%lu bytes)\n",
			op, node, node->name, (unsigned long)node->size);
}
#else /* CONFIG_DEBUG_DEVRES */
#define set_node_dbginfo(node, n, s)	do {} while (0)
#define devres_log(dev, node, op)	do {} while (0)
#endif /* CONFIG_DEBUG_DEVRES */

/*
 * Release functions for devres group.  These callbacks are used only
 * for identification.
 */
static void group_open_release(struct device *dev, void *res)
{
	/* noop */
}

static void group_close_release(struct device *dev, void *res)
{
	/* noop */
}

static struct devres_group * node_to_group(struct devres_node *node)
{
	if (node->release == &group_open_release)
		return container_of(node, struct devres_group, node[0]);
	if (node->release == &group_close_release)
		return container_of(node, struct devres_group, node[1]);
	return NULL;
}

static __always_inline struct devres * alloc_dr(dr_release_t release,
						size_t size, gfp_t gfp, int nid)
{
	size_t tot_size;
	struct devres *dr;

	/* We must catch any near-SIZE_MAX cases that could overflow. */
	if (unlikely(check_add_overflow(sizeof(struct devres), size,
					&tot_size)))
		return NULL;

	dr = kmalloc_node_track_caller(tot_size, gfp, nid);
	if (unlikely(!dr))
		return NULL;

	memset(dr, 0, offsetof(struct devres, data));

	INIT_LIST_HEAD(&dr->node.entry);
	dr->node.release = release;
	return dr;
}

static void add_dr(struct device *dev, struct devres_node *node)
{
	devres_log(dev, node, "ADD");
	BUG_ON(!list_empty(&node->entry));
	list_add_tail(&node->entry, &dev->devres_head);
}

#ifdef CONFIG_DEBUG_DEVRES
void * __devres_alloc_node(dr_release_t release, size_t size, gfp_t gfp, int nid,
		      const char *name)
{
	struct devres *dr;

	dr = alloc_dr(release, size, gfp | __GFP_ZERO, nid);
	if (unlikely(!dr))
		return NULL;
	set_node_dbginfo(&dr->node, name, size);
	return dr->data;
}
EXPORT_SYMBOL_GPL(__devres_alloc_node);
#else
/**
 * devres_alloc - Allocate device resource data
 * @release: Release function devres will be associated with
 * @size: Allocation size
 * @gfp: Allocation flags
 * @nid: NUMA node
 *
 * Allocate devres of @size bytes.  The allocated area is zeroed, then
 * associated with @release.  The returned pointer can be passed to
 * other devres_*() functions.
 *
 * RETURNS:
 * Pointer to allocated devres on success, NULL on failure.
 */
void * devres_alloc_node(dr_release_t release, size_t size, gfp_t gfp, int nid)
{
	struct devres *dr;

	dr = alloc_dr(release, size, gfp | __GFP_ZERO, nid);
	if (unlikely(!dr))
		return NULL;
	return dr->data;
}
EXPORT_SYMBOL_GPL(devres_alloc_node);
#endif

/**
 * devres_for_each_res - Resource iterator
 * @dev: Device to iterate resource from
 * @release: Look for resources associated with this release function
 * @match: Match function (optional)
 * @match_data: Data for the match function
 * @fn: Function to be called for each matched resource.
 * @data: Data for @fn, the 3rd parameter of @fn
 *
 * Call @fn for each devres of @dev which is associated with @release
 * and for which @match returns 1.
 *
 * RETURNS:
 * 	void
 */
void devres_for_each_res(struct device *dev, dr_release_t release,
			dr_match_t match, void *match_data,
			void (*fn)(struct device *, void *, void *),
			void *data)
{
	struct devres_node *node;
	struct devres_node *tmp;
	unsigned long flags;

	if (!fn)
		return;

	spin_lock_irqsave(&dev->devres_lock, flags);
	list_for_each_entry_safe_reverse(node, tmp,
			&dev->devres_head, entry) {
		struct devres *dr = container_of(node, struct devres, node);

		if (node->release != release)
			continue;
		if (match && !match(dev, dr->data, match_data))
			continue;
		fn(dev, dr->data, data);
	}
	spin_unlock_irqrestore(&dev->devres_lock, flags);
}
EXPORT_SYMBOL_GPL(devres_for_each_res);

/**
 * devres_free - Free device resource data
 * @res: Pointer to devres data to free
 *
 * Free devres created with devres_alloc().
 */
void devres_free(void *res)
{
	if (res) {
		struct devres *dr = container_of(res, struct devres, data);

		BUG_ON(!list_empty(&dr->node.entry));
		kfree(dr);
	}
}
EXPORT_SYMBOL_GPL(devres_free);

/**
 * devres_add - Register device resource
 * @dev: Device to add resource to
 * @res: Resource to register
 *
 * Register devres @res to @dev.  @res should have been allocated
 * using devres_alloc().  On driver detach, the associated release
 * function will be invoked and devres will be freed automatically.
 */
void devres_add(struct device *dev, void *res)
{
	struct devres *dr = container_of(res, struct devres, data);
	unsigned long flags;

	spin_lock_irqsave(&dev->devres_lock, flags);
	add_dr(dev, &dr->node);
	spin_unlock_irqrestore(&dev->devres_lock, flags);
}
EXPORT_SYMBOL_GPL(devres_add);

static struct devres *find_dr(struct device *dev, dr_release_t release,
			      dr_match_t match, void *match_data)
{
	struct devres_node *node;

	list_for_each_entry_reverse(node, &dev->devres_head, entry) {
		struct devres *dr = container_of(node, struct devres, node);

		if (node->release != release)
			continue;
		if (match && !match(dev, dr->data, match_data))
			continue;
		return dr;
	}

	return NULL;
}

/**
 * devres_find - Find device resource
 * @dev: Device to lookup resource from
 * @release: Look for resources associated with this release function
 * @match: Match function (optional)
 * @match_data: Data for the match function
 *
 * Find the latest devres of @dev which is associated with @release
 * and for which @match returns 1.  If @match is NULL, it's considered
 * to match all.
 *
 * RETURNS:
 * Pointer to found devres, NULL if not found.
 */
void * devres_find(struct device *dev, dr_release_t release,
		   dr_match_t match, void *match_data)
{
	struct devres *dr;
	unsigned long flags;

	spin_lock_irqsave(&dev->devres_lock, flags);
	dr = find_dr(dev, release, match, match_data);
	spin_unlock_irqrestore(&dev->devres_lock, flags);

	if (dr)
		return dr->data;
	return NULL;
}
EXPORT_SYMBOL_GPL(devres_find);

/**
 * devres_get - Find devres, if non-existent, add one atomically
 * @dev: Device to lookup or add devres for
 * @new_res: Pointer to new initialized devres to add if not found
 * @match: Match function (optional)
 * @match_data: Data for the match function
 *
 * Find the latest devres of @dev which has the same release function
 * as @new_res and for which @match return 1.  If found, @new_res is
 * freed; otherwise, @new_res is added atomically.
 *
 * RETURNS:
 * Pointer to found or added devres.
 */
void * devres_get(struct device *dev, void *new_res,
		  dr_match_t match, void *match_data)
{
	struct devres *new_dr = container_of(new_res, struct devres, data);
	struct devres *dr;
	unsigned long flags;

	spin_lock_irqsave(&dev->devres_lock, flags);
	dr = find_dr(dev, new_dr->node.release, match, match_data);
	if (!dr) {
		add_dr(dev, &new_dr->node);
		dr = new_dr;
		new_res = NULL;
	}
	spin_unlock_irqrestore(&dev->devres_lock, flags);
	devres_free(new_res);

	return dr->data;
}
EXPORT_SYMBOL_GPL(devres_get);

/**
 * devres_remove - Find a device resource and remove it
 * @dev: Device to find resource from
 * @release: Look for resources associated with this release function
 * @match: Match function (optional)
 * @match_data: Data for the match function
 *
 * Find the latest devres of @dev associated with @release and for
 * which @match returns 1.  If @match is NULL, it's considered to
 * match all.  If found, the resource is removed atomically and
 * returned.
 *
 * RETURNS:
 * Pointer to removed devres on success, NULL if not found.
 */
void * devres_remove(struct device *dev, dr_release_t release,
		     dr_match_t match, void *match_data)
{
	struct devres *dr;
	unsigned long flags;

	spin_lock_irqsave(&dev->devres_lock, flags);
	dr = find_dr(dev, release, match, match_data);
	if (dr) {
		list_del_init(&dr->node.entry);
		devres_log(dev, &dr->node, "REM");
	}
	spin_unlock_irqrestore(&dev->devres_lock, flags);

	if (dr)
		return dr->data;
	return NULL;
}
EXPORT_SYMBOL_GPL(devres_remove);

/**
 * devres_destroy - Find a device resource and destroy it
 * @dev: Device to find resource from
 * @release: Look for resources associated with this release function
 * @match: Match function (optional)
 * @match_data: Data for the match function
 *
 * Find the latest devres of @dev associated with @release and for
 * which @match returns 1.  If @match is NULL, it's considered to
 * match all.  If found, the resource is removed atomically and freed.
 *
 * Note that the release function for the resource will not be called,
 * only the devres-allocated data will be freed.  The caller becomes
 * responsible for freeing any other data.
 *
 * RETURNS:
 * 0 if devres is found and freed, -ENOENT if not found.
 */
int devres_destroy(struct device *dev, dr_release_t release,
		   dr_match_t match, void *match_data)
{
	void *res;

	res = devres_remove(dev, release, match, match_data);
	if (unlikely(!res))
		return -ENOENT;

	devres_free(res);
	return 0;
}
EXPORT_SYMBOL_GPL(devres_destroy);


/**
 * devres_release - Find a device resource and destroy it, calling release
 * @dev: Device to find resource from
 * @release: Look for resources associated with this release function
 * @match: Match function (optional)
 * @match_data: Data for the match function
 *
 * Find the latest devres of @dev associated with @release and for
 * which @match returns 1.  If @match is NULL, it's considered to
 * match all.  If found, the resource is removed atomically, the
 * release function called and the resource freed.
 *
 * RETURNS:
 * 0 if devres is found and freed, -ENOENT if not found.
 */
int devres_release(struct device *dev, dr_release_t release,
		   dr_match_t match, void *match_data)
{
	void *res;

	res = devres_remove(dev, release, match, match_data);
	if (unlikely(!res))
		return -ENOENT;

	(*release)(dev, res);
	devres_free(res);
	return 0;
}
EXPORT_SYMBOL_GPL(devres_release);

static int remove_nodes(struct device *dev,
			struct list_head *first, struct list_head *end,
			struct list_head *todo)
{
	int cnt = 0, nr_groups = 0;
	struct list_head *cur;

	/* First pass - move normal devres entries to @todo and clear
	 * devres_group colors.
	 */
	cur = first;
	while (cur != end) {
		struct devres_node *node;
		struct devres_group *grp;

		node = list_entry(cur, struct devres_node, entry);
		cur = cur->next;

		grp = node_to_group(node);
		if (grp) {
			/* clear color of group markers in the first pass */
			grp->color = 0;
			nr_groups++;
		} else {
			/* regular devres entry */
			if (&node->entry == first)
				first = first->next;
			list_move_tail(&node->entry, todo);
			cnt++;
		}
	}

	if (!nr_groups)
		return cnt;

	/* Second pass - Scan groups and color them.  A group gets
	 * color value of two iff the group is wholly contained in
	 * [cur, end).  That is, for a closed group, both opening and
	 * closing markers should be in the range, while just the
	 * opening marker is enough for an open group.
	 */
	cur = first;
	while (cur != end) {
		struct devres_node *node;
		struct devres_group *grp;

		node = list_entry(cur, struct devres_node, entry);
		cur = cur->next;

		grp = node_to_group(node);
		BUG_ON(!grp || list_empty(&grp->node[0].entry));

		grp->color++;
		if (list_empty(&grp->node[1].entry))
			grp->color++;

		BUG_ON(grp->color <= 0 || grp->color > 2);
		if (grp->color == 2) {
			/* No need to update cur or end.  The removed
			 * nodes are always before both.
			 */
			list_move_tail(&grp->node[0].entry, todo);
			list_del_init(&grp->node[1].entry);
		}
	}

	return cnt;
}

static int release_nodes(struct device *dev, struct list_head *first,
			 struct list_head *end, unsigned long flags)
	__releases(&dev->devres_lock)
{
	LIST_HEAD(todo);
	int cnt;
	struct devres *dr, *tmp;

	cnt = remove_nodes(dev, first, end, &todo);

	spin_unlock_irqrestore(&dev->devres_lock, flags);

	/* Release.  Note that both devres and devres_group are
	 * handled as devres in the following loop.  This is safe.
	 */
	list_for_each_entry_safe_reverse(dr, tmp, &todo, node.entry) {
		devres_log(dev, &dr->node, "REL");
		dr->node.release(dev, dr->data);
		kfree(dr);
	}

	return cnt;
}

/**
 * devres_release_all - Release all managed resources
 * @dev: Device to release resources for
 *
 * Release all resources associated with @dev.  This function is
 * called on driver detach.
 */
int devres_release_all(struct device *dev)
{
	unsigned long flags;

	/* Looks like an uninitialized device structure */
	if (WARN_ON(dev->devres_head.next == NULL))
		return -ENODEV;
	spin_lock_irqsave(&dev->devres_lock, flags);
	return release_nodes(dev, dev->devres_head.next, &dev->devres_head,
			     flags);
}

/**
 * devres_open_group - Open a new devres group
 * @dev: Device to open devres group for
 * @id: Separator ID
 * @gfp: Allocation flags
 *
 * Open a new devres group for @dev with @id.  For @id, using a
 * pointer to an object which won't be used for another group is
 * recommended.  If @id is NULL, address-wise unique ID is created.
 *
 * RETURNS:
 * ID of the new group, NULL on failure.
 */
void * devres_open_group(struct device *dev, void *id, gfp_t gfp)
{
	struct devres_group *grp;
	unsigned long flags;

	grp = kmalloc(sizeof(*grp), gfp);
	if (unlikely(!grp))
		return NULL;

	grp->node[0].release = &group_open_release;
	grp->node[1].release = &group_close_release;
	INIT_LIST_HEAD(&grp->node[0].entry);
	INIT_LIST_HEAD(&grp->node[1].entry);
	set_node_dbginfo(&grp->node[0], "grp<", 0);
	set_node_dbginfo(&grp->node[1], "grp>", 0);
	grp->id = grp;
	if (id)
		grp->id = id;

	spin_lock_irqsave(&dev->devres_lock, flags);
	add_dr(dev, &grp->node[0]);
	spin_unlock_irqrestore(&dev->devres_lock, flags);
	return grp->id;
}
EXPORT_SYMBOL_GPL(devres_open_group);

/* Find devres group with ID @id.  If @id is NULL, look for the latest. */
static struct devres_group * find_group(struct device *dev, void *id)
{
	struct devres_node *node;

	list_for_each_entry_reverse(node, &dev->devres_head, entry) {
		struct devres_group *grp;

		if (node->release != &group_open_release)
			continue;

		grp = container_of(node, struct devres_group, node[0]);

		if (id) {
			if (grp->id == id)
				return grp;
		} else if (list_empty(&grp->node[1].entry))
			return grp;
	}

	return NULL;
}

/**
 * devres_close_group - Close a devres group
 * @dev: Device to close devres group for
 * @id: ID of target group, can be NULL
 *
 * Close the group identified by @id.  If @id is NULL, the latest open
 * group is selected.
 */
void devres_close_group(struct device *dev, void *id)
{
	struct devres_group *grp;
	unsigned long flags;

	spin_lock_irqsave(&dev->devres_lock, flags);

	grp = find_group(dev, id);
	if (grp)
		add_dr(dev, &grp->node[1]);
	else
		WARN_ON(1);

	spin_unlock_irqrestore(&dev->devres_lock, flags);
}
EXPORT_SYMBOL_GPL(devres_close_group);

/**
 * devres_remove_group - Remove a devres group
 * @dev: Device to remove group for
 * @id: ID of target group, can be NULL
 *
 * Remove the group identified by @id.  If @id is NULL, the latest
 * open group is selected.  Note that removing a group doesn't affect
 * any other resources.
 */
void devres_remove_group(struct device *dev, void *id)
{
	struct devres_group *grp;
	unsigned long flags;

	spin_lock_irqsave(&dev->devres_lock, flags);

	grp = find_group(dev, id);
	if (grp) {
		list_del_init(&grp->node[0].entry);
		list_del_init(&grp->node[1].entry);
		devres_log(dev, &grp->node[0], "REM");
	} else
		WARN_ON(1);

	spin_unlock_irqrestore(&dev->devres_lock, flags);

	kfree(grp);
}
EXPORT_SYMBOL_GPL(devres_remove_group);

/**
 * devres_release_group - Release resources in a devres group
 * @dev: Device to release group for
 * @id: ID of target group, can be NULL
 *
 * Release all resources in the group identified by @id.  If @id is
 * NULL, the latest open group is selected.  The selected group and
 * groups properly nested inside the selected group are removed.
 *
 * RETURNS:
 * The number of released non-group resources.
 */
int devres_release_group(struct device *dev, void *id)
{
	struct devres_group *grp;
	unsigned long flags;
	int cnt = 0;

	spin_lock_irqsave(&dev->devres_lock, flags);

	grp = find_group(dev, id);
	if (grp) {
		struct list_head *first = &grp->node[0].entry;
		struct list_head *end = &dev->devres_head;

		if (!list_empty(&grp->node[1].entry))
			end = grp->node[1].entry.next;

		cnt = release_nodes(dev, first, end, flags);
	} else {
		WARN_ON(1);
		spin_unlock_irqrestore(&dev->devres_lock, flags);
	}

	return cnt;
}
EXPORT_SYMBOL_GPL(devres_release_group);

/*
 * Custom devres actions allow inserting a simple function call
 * into the teadown sequence.
 */

struct action_devres {
	void *data;
	void (*action)(void *);
};

static int devm_action_match(struct device *dev, void *res, void *p)
{
	struct action_devres *devres = res;
	struct action_devres *target = p;

	return devres->action == target->action &&
	       devres->data == target->data;
}

static void devm_action_release(struct device *dev, void *res)
{
	struct action_devres *devres = res;

	devres->action(devres->data);
}

/**
 * devm_add_action() - add a custom action to list of managed resources
 * @dev: Device that owns the action
 * @action: Function that should be called
 * @data: Pointer to data passed to @action implementation
 *
 * This adds a custom action to the list of managed resources so that
 * it gets executed as part of standard resource unwinding.
 */
int devm_add_action(struct device *dev, void (*action)(void *), void *data)
{
	struct action_devres *devres;

	devres = devres_alloc(devm_action_release,
			      sizeof(struct action_devres), GFP_KERNEL);
	if (!devres)
		return -ENOMEM;

	devres->data = data;
	devres->action = action;

	devres_add(dev, devres);
	return 0;
}
EXPORT_SYMBOL_GPL(devm_add_action);

/**
 * devm_remove_action() - removes previously added custom action
 * @dev: Device that owns the action
 * @action: Function implementing the action
 * @data: Pointer to data passed to @action implementation
 *
 * Removes instance of @action previously added by devm_add_action().
 * Both action and data should match one of the existing entries.
 */
void devm_remove_action(struct device *dev, void (*action)(void *), void *data)
{
	struct action_devres devres = {
		.data = data,
		.action = action,
	};

	WARN_ON(devres_destroy(dev, devm_action_release, devm_action_match,
			       &devres));
}
EXPORT_SYMBOL_GPL(devm_remove_action);

/**
 * devm_release_action() - release previously added custom action
 * @dev: Device that owns the action
 * @action: Function implementing the action
 * @data: Pointer to data passed to @action implementation
 *
 * Releases and removes instance of @action previously added by
 * devm_add_action().  Both action and data should match one of the
 * existing entries.
 */
void devm_release_action(struct device *dev, void (*action)(void *), void *data)
{
	struct action_devres devres = {
		.data = data,
		.action = action,
	};

	WARN_ON(devres_release(dev, devm_action_release, devm_action_match,
			       &devres));

}
EXPORT_SYMBOL_GPL(devm_release_action);

/*
 * Managed kmalloc/kfree
 */
static void devm_kmalloc_release(struct device *dev, void *res)
{
	/* noop */
}

static int devm_kmalloc_match(struct device *dev, void *res, void *data)
{
	return res == data;
}

/**
 * devm_kmalloc - Resource-managed kmalloc
 * @dev: Device to allocate memory for
 * @size: Allocation size
 * @gfp: Allocation gfp flags
 *
 * Managed kmalloc.  Memory allocated with this function is
 * automatically freed on driver detach.  Like all other devres
 * resources, guaranteed alignment is unsigned long long.
 *
 * RETURNS:
 * Pointer to allocated memory on success, NULL on failure.
 */
void * devm_kmalloc(struct device *dev, size_t size, gfp_t gfp)
{
	struct devres *dr;

	/* use raw alloc_dr for kmalloc caller tracing */
	dr = alloc_dr(devm_kmalloc_release, size, gfp, dev_to_node(dev));
	if (unlikely(!dr))
		return NULL;

	/*
	 * This is named devm_kzalloc_release for historical reasons
	 * The initial implementation did not support kmalloc, only kzalloc
	 */
	set_node_dbginfo(&dr->node, "devm_kzalloc_release", size);
	devres_add(dev, dr->data);
	return dr->data;
}
EXPORT_SYMBOL_GPL(devm_kmalloc);

/**
 * devm_kstrdup - Allocate resource managed space and
 *                copy an existing string into that.
 * @dev: Device to allocate memory for
 * @s: the string to duplicate
 * @gfp: the GFP mask used in the devm_kmalloc() call when
 *       allocating memory
 * RETURNS:
 * Pointer to allocated string on success, NULL on failure.
 */
char *devm_kstrdup(struct device *dev, const char *s, gfp_t gfp)
{
	size_t size;
	char *buf;

	if (!s)
		return NULL;

	size = strlen(s) + 1;
	buf = devm_kmalloc(dev, size, gfp);
	if (buf)
		memcpy(buf, s, size);
	return buf;
}
EXPORT_SYMBOL_GPL(devm_kstrdup);

/**
 * devm_kstrdup_const - resource managed conditional string duplication
 * @dev: device for which to duplicate the string
 * @s: the string to duplicate
 * @gfp: the GFP mask used in the kmalloc() call when allocating memory
 *
 * Strings allocated by devm_kstrdup_const will be automatically freed when
 * the associated device is detached.
 *
 * RETURNS:
 * Source string if it is in .rodata section otherwise it falls back to
 * devm_kstrdup.
 */
const char *devm_kstrdup_const(struct device *dev, const char *s, gfp_t gfp)
{
	if (is_kernel_rodata((unsigned long)s))
		return s;

	return devm_kstrdup(dev, s, gfp);
}
EXPORT_SYMBOL_GPL(devm_kstrdup_const);

/**
 * devm_kvasprintf - Allocate resource managed space and format a string
 *		     into that.
 * @dev: Device to allocate memory for
 * @gfp: the GFP mask used in the devm_kmalloc() call when
 *       allocating memory
 * @fmt: The printf()-style format string
 * @ap: Arguments for the format string
 * RETURNS:
 * Pointer to allocated string on success, NULL on failure.
 */
char *devm_kvasprintf(struct device *dev, gfp_t gfp, const char *fmt,
		      va_list ap)
{
	unsigned int len;
	char *p;
	va_list aq;

	va_copy(aq, ap);
	len = vsnprintf(NULL, 0, fmt, aq);
	va_end(aq);

	p = devm_kmalloc(dev, len+1, gfp);
	if (!p)
		return NULL;

	vsnprintf(p, len+1, fmt, ap);

	return p;
}
EXPORT_SYMBOL(devm_kvasprintf);

/**
 * devm_kasprintf - Allocate resource managed space and format a string
 *		    into that.
 * @dev: Device to allocate memory for
 * @gfp: the GFP mask used in the devm_kmalloc() call when
 *       allocating memory
 * @fmt: The printf()-style format string
 * @...: Arguments for the format string
 * RETURNS:
 * Pointer to allocated string on success, NULL on failure.
 */
char *devm_kasprintf(struct device *dev, gfp_t gfp, const char *fmt, ...)
{
	va_list ap;
	char *p;

	va_start(ap, fmt);
	p = devm_kvasprintf(dev, gfp, fmt, ap);
	va_end(ap);

	return p;
}
EXPORT_SYMBOL_GPL(devm_kasprintf);

/**
 * devm_kfree - Resource-managed kfree
 * @dev: Device this memory belongs to
 * @p: Memory to free
 *
 * Free memory allocated with devm_kmalloc().
 */
void devm_kfree(struct device *dev, const void *p)
{
	int rc;

	/*
	 * Special case: pointer to a string in .rodata returned by
	 * devm_kstrdup_const().
	 */
	if (unlikely(is_kernel_rodata((unsigned long)p)))
		return;

	rc = devres_destroy(dev, devm_kmalloc_release,
			    devm_kmalloc_match, (void *)p);
	WARN_ON(rc);
}
EXPORT_SYMBOL_GPL(devm_kfree);

/**
 * devm_kmemdup - Resource-managed kmemdup
 * @dev: Device this memory belongs to
 * @src: Memory region to duplicate
 * @len: Memory region length
 * @gfp: GFP mask to use
 *
 * Duplicate region of a memory using resource managed kmalloc
 */
void *devm_kmemdup(struct device *dev, const void *src, size_t len, gfp_t gfp)
{
	void *p;

	p = devm_kmalloc(dev, len, gfp);
	if (p)
		memcpy(p, src, len);

	return p;
}
EXPORT_SYMBOL_GPL(devm_kmemdup);

struct pages_devres {
	unsigned long addr;
	unsigned int order;
};

static int devm_pages_match(struct device *dev, void *res, void *p)
{
	struct pages_devres *devres = res;
	struct pages_devres *target = p;

	return devres->addr == target->addr;
}

static void devm_pages_release(struct device *dev, void *res)
{
	struct pages_devres *devres = res;

	free_pages(devres->addr, devres->order);
}

/**
 * devm_get_free_pages - Resource-managed __get_free_pages
 * @dev: Device to allocate memory for
 * @gfp_mask: Allocation gfp flags
 * @order: Allocation size is (1 << order) pages
 *
 * Managed get_free_pages.  Memory allocated with this function is
 * automatically freed on driver detach.
 *
 * RETURNS:
 * Address of allocated memory on success, 0 on failure.
 */

unsigned long devm_get_free_pages(struct device *dev,
				  gfp_t gfp_mask, unsigned int order)
{
	struct pages_devres *devres;
	unsigned long addr;

	addr = __get_free_pages(gfp_mask, order);

	if (unlikely(!addr))
		return 0;

	devres = devres_alloc(devm_pages_release,
			      sizeof(struct pages_devres), GFP_KERNEL);
	if (unlikely(!devres)) {
		free_pages(addr, order);
		return 0;
	}

	devres->addr = addr;
	devres->order = order;

	devres_add(dev, devres);
	return addr;
}
EXPORT_SYMBOL_GPL(devm_get_free_pages);

/**
 * devm_free_pages - Resource-managed free_pages
 * @dev: Device this memory belongs to
 * @addr: Memory to free
 *
 * Free memory allocated with devm_get_free_pages(). Unlike free_pages,
 * there is no need to supply the @order.
 */
void devm_free_pages(struct device *dev, unsigned long addr)
{
	struct pages_devres devres = { .addr = addr };

	WARN_ON(devres_release(dev, devm_pages_release, devm_pages_match,
			       &devres));
}
EXPORT_SYMBOL_GPL(devm_free_pages);

static void devm_percpu_release(struct device *dev, void *pdata)
{
	void __percpu *p;

	p = *(void __percpu **)pdata;
	free_percpu(p);
}

static int devm_percpu_match(struct device *dev, void *data, void *p)
{
	struct devres *devr = container_of(data, struct devres, data);

	return *(void **)devr->data == p;
}

/**
 * __devm_alloc_percpu - Resource-managed alloc_percpu
 * @dev: Device to allocate per-cpu memory for
 * @size: Size of per-cpu memory to allocate
 * @align: Alignment of per-cpu memory to allocate
 *
 * Managed alloc_percpu. Per-cpu memory allocated with this function is
 * automatically freed on driver detach.
 *
 * RETURNS:
 * Pointer to allocated memory on success, NULL on failure.
 */
void __percpu *__devm_alloc_percpu(struct device *dev, size_t size,
		size_t align)
{
	void *p;
	void __percpu *pcpu;

	pcpu = __alloc_percpu(size, align);
	if (!pcpu)
		return NULL;

	p = devres_alloc(devm_percpu_release, sizeof(void *), GFP_KERNEL);
	if (!p) {
		free_percpu(pcpu);
		return NULL;
	}

	*(void __percpu **)p = pcpu;

	devres_add(dev, p);

	return pcpu;
}
EXPORT_SYMBOL_GPL(__devm_alloc_percpu);

/**
 * devm_free_percpu - Resource-managed free_percpu
 * @dev: Device this memory belongs to
 * @pdata: Per-cpu memory to free
 *
 * Free memory allocated with devm_alloc_percpu().
 */
void devm_free_percpu(struct device *dev, void __percpu *pdata)
{
	WARN_ON(devres_destroy(dev, devm_percpu_release, devm_percpu_match,
			       (void *)pdata));
}
EXPORT_SYMBOL_GPL(devm_free_percpu);
