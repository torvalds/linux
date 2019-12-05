// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <asm/prom.h>

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


/* Helper Routines to convert between drc_index to cpu numbers */

int of_read_drc_info_cell(struct property **prop, const __be32 **curval,
			struct of_drc_info *data)
{
	const char *p = (char *)(*curval);
	const __be32 *p2;

	if (!data)
		return -EINVAL;

	/* Get drc-type:encode-string */
	data->drc_type = (char *)p;
	p = of_prop_next_string(*prop, p);
	if (!p)
		return -EINVAL;

	/* Get drc-name-prefix:encode-string */
	data->drc_name_prefix = (char *)p;
	p = of_prop_next_string(*prop, p);
	if (!p)
		return -EINVAL;

	/* Get drc-index-start:encode-int */
	p2 = (const __be32 *)p;
	data->drc_index_start = be32_to_cpu(*p2);

	/* Get drc-name-suffix-start:encode-int */
	p2 = of_prop_next_u32(*prop, p2, &data->drc_name_suffix_start);
	if (!p2)
		return -EINVAL;

	/* Get number-sequential-elements:encode-int */
	p2 = of_prop_next_u32(*prop, p2, &data->num_sequential_elems);
	if (!p2)
		return -EINVAL;

	/* Get sequential-increment:encode-int */
	p2 = of_prop_next_u32(*prop, p2, &data->sequential_inc);
	if (!p2)
		return -EINVAL;

	/* Get drc-power-domain:encode-int */
	p2 = of_prop_next_u32(*prop, p2, &data->drc_power_domain);
	if (!p2)
		return -EINVAL;

	/* Should now know end of current entry */
	(*curval) = (void *)p2;
	data->last_drc_index = data->drc_index_start +
		((data->num_sequential_elems - 1) * data->sequential_inc);

	return 0;
}
EXPORT_SYMBOL(of_read_drc_info_cell);
