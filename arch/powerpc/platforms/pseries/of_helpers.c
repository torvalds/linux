// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>

#include "of_helpers.h"

/**
 * pseries_of_derive_parent - basically like dirname(1)
 * @path:  the full_name of a node to be added to the tree
 *
 * Returns the node which should be the parent of the node
 * described by path.  E.g., for path = "/foo/bar", returns
 * the node with full_name = "/foo".
 */
struct device_node *pseries_of_derive_parent(const char *path)
{
	struct device_node *parent;
	char *parent_path = "/";
	const char *tail;

	/* We do not want the trailing '/' character */
	tail = kbasename(path) - 1;

	/* reject if path is "/" */
	if (!strcmp(path, "/"))
		return ERR_PTR(-EINVAL);

	if (tail > path) {
		parent_path = kstrndup(path, tail - path, GFP_KERNEL);
		if (!parent_path)
			return ERR_PTR(-ENOMEM);
	}
	parent = of_find_node_by_path(parent_path);
	if (strcmp(parent_path, "/"))
		kfree(parent_path);
	return parent ? parent : ERR_PTR(-EINVAL);
}
