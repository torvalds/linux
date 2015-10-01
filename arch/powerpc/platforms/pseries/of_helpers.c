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
	struct device_node *parent = NULL;
	char *parent_path = "/";
	size_t parent_path_len = strrchr(path, '/') - path + 1;

	/* reject if path is "/" */
	if (!strcmp(path, "/"))
		return ERR_PTR(-EINVAL);

	if (strrchr(path, '/') != path) {
		parent_path = kmalloc(parent_path_len, GFP_KERNEL);
		if (!parent_path)
			return ERR_PTR(-ENOMEM);
		strlcpy(parent_path, path, parent_path_len);
	}
	parent = of_find_node_by_path(parent_path);
	if (!parent)
		return ERR_PTR(-EINVAL);
	if (strcmp(parent_path, "/"))
		kfree(parent_path);
	return parent;
}
