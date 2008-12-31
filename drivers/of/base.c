/*
 * Procedures for creating, accessing and interpreting the device tree.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com
 *
 *  Adapted for sparc and sparc64 by David S. Miller davem@davemloft.net
 *
 *  Reconsolidated from arch/x/kernel/prom.c by Stephen Rothwell.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/spinlock.h>

struct device_node *allnodes;

/* use when traversing tree through the allnext, child, sibling,
 * or parent members of struct device_node.
 */
DEFINE_RWLOCK(devtree_lock);

int of_n_addr_cells(struct device_node *np)
{
	const int *ip;

	do {
		if (np->parent)
			np = np->parent;
		ip = of_get_property(np, "#address-cells", NULL);
		if (ip)
			return *ip;
	} while (np->parent);
	/* No #address-cells property for the root node */
	return OF_ROOT_NODE_ADDR_CELLS_DEFAULT;
}
EXPORT_SYMBOL(of_n_addr_cells);

int of_n_size_cells(struct device_node *np)
{
	const int *ip;

	do {
		if (np->parent)
			np = np->parent;
		ip = of_get_property(np, "#size-cells", NULL);
		if (ip)
			return *ip;
	} while (np->parent);
	/* No #size-cells property for the root node */
	return OF_ROOT_NODE_SIZE_CELLS_DEFAULT;
}
EXPORT_SYMBOL(of_n_size_cells);

struct property *of_find_property(const struct device_node *np,
				  const char *name,
				  int *lenp)
{
	struct property *pp;

	if (!np)
		return NULL;

	read_lock(&devtree_lock);
	for (pp = np->properties; pp != 0; pp = pp->next) {
		if (of_prop_cmp(pp->name, name) == 0) {
			if (lenp != 0)
				*lenp = pp->length;
			break;
		}
	}
	read_unlock(&devtree_lock);

	return pp;
}
EXPORT_SYMBOL(of_find_property);

/*
 * Find a property with a given name for a given node
 * and return the value.
 */
const void *of_get_property(const struct device_node *np, const char *name,
			 int *lenp)
{
	struct property *pp = of_find_property(np, name, lenp);

	return pp ? pp->value : NULL;
}
EXPORT_SYMBOL(of_get_property);

/** Checks if the given "compat" string matches one of the strings in
 * the device's "compatible" property
 */
int of_device_is_compatible(const struct device_node *device,
		const char *compat)
{
	const char* cp;
	int cplen, l;

	cp = of_get_property(device, "compatible", &cplen);
	if (cp == NULL)
		return 0;
	while (cplen > 0) {
		if (of_compat_cmp(cp, compat, strlen(compat)) == 0)
			return 1;
		l = strlen(cp) + 1;
		cp += l;
		cplen -= l;
	}

	return 0;
}
EXPORT_SYMBOL(of_device_is_compatible);

/**
 *  of_device_is_available - check if a device is available for use
 *
 *  @device: Node to check for availability
 *
 *  Returns 1 if the status property is absent or set to "okay" or "ok",
 *  0 otherwise
 */
int of_device_is_available(const struct device_node *device)
{
	const char *status;
	int statlen;

	status = of_get_property(device, "status", &statlen);
	if (status == NULL)
		return 1;

	if (statlen > 0) {
		if (!strcmp(status, "okay") || !strcmp(status, "ok"))
			return 1;
	}

	return 0;
}
EXPORT_SYMBOL(of_device_is_available);

/**
 *	of_get_parent - Get a node's parent if any
 *	@node:	Node to get parent
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_get_parent(const struct device_node *node)
{
	struct device_node *np;

	if (!node)
		return NULL;

	read_lock(&devtree_lock);
	np = of_node_get(node->parent);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_get_parent);

/**
 *	of_get_next_parent - Iterate to a node's parent
 *	@node:	Node to get parent of
 *
 * 	This is like of_get_parent() except that it drops the
 * 	refcount on the passed node, making it suitable for iterating
 * 	through a node's parents.
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_get_next_parent(struct device_node *node)
{
	struct device_node *parent;

	if (!node)
		return NULL;

	read_lock(&devtree_lock);
	parent = of_node_get(node->parent);
	of_node_put(node);
	read_unlock(&devtree_lock);
	return parent;
}

/**
 *	of_get_next_child - Iterate a node childs
 *	@node:	parent node
 *	@prev:	previous child of the parent node, or NULL to get first
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_get_next_child(const struct device_node *node,
	struct device_node *prev)
{
	struct device_node *next;

	read_lock(&devtree_lock);
	next = prev ? prev->sibling : node->child;
	for (; next; next = next->sibling)
		if (of_node_get(next))
			break;
	of_node_put(prev);
	read_unlock(&devtree_lock);
	return next;
}
EXPORT_SYMBOL(of_get_next_child);

/**
 *	of_find_node_by_path - Find a node matching a full OF path
 *	@path:	The full path to match
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_node_by_path(const char *path)
{
	struct device_node *np = allnodes;

	read_lock(&devtree_lock);
	for (; np; np = np->allnext) {
		if (np->full_name && (of_node_cmp(np->full_name, path) == 0)
		    && of_node_get(np))
			break;
	}
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_path);

/**
 *	of_find_node_by_name - Find a node by its "name" property
 *	@from:	The node to start searching from or NULL, the node
 *		you pass will not be searched, only the next one
 *		will; typically, you pass what the previous call
 *		returned. of_node_put() will be called on it
 *	@name:	The name string to match against
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_node_by_name(struct device_node *from,
	const char *name)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	np = from ? from->allnext : allnodes;
	for (; np; np = np->allnext)
		if (np->name && (of_node_cmp(np->name, name) == 0)
		    && of_node_get(np))
			break;
	of_node_put(from);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_name);

/**
 *	of_find_node_by_type - Find a node by its "device_type" property
 *	@from:	The node to start searching from, or NULL to start searching
 *		the entire device tree. The node you pass will not be
 *		searched, only the next one will; typically, you pass
 *		what the previous call returned. of_node_put() will be
 *		called on from for you.
 *	@type:	The type string to match against
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_node_by_type(struct device_node *from,
	const char *type)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	np = from ? from->allnext : allnodes;
	for (; np; np = np->allnext)
		if (np->type && (of_node_cmp(np->type, type) == 0)
		    && of_node_get(np))
			break;
	of_node_put(from);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_type);

/**
 *	of_find_compatible_node - Find a node based on type and one of the
 *                                tokens in its "compatible" property
 *	@from:		The node to start searching from or NULL, the node
 *			you pass will not be searched, only the next one
 *			will; typically, you pass what the previous call
 *			returned. of_node_put() will be called on it
 *	@type:		The type string to match "device_type" or NULL to ignore
 *	@compatible:	The string to match to one of the tokens in the device
 *			"compatible" list.
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_compatible_node(struct device_node *from,
	const char *type, const char *compatible)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	np = from ? from->allnext : allnodes;
	for (; np; np = np->allnext) {
		if (type
		    && !(np->type && (of_node_cmp(np->type, type) == 0)))
			continue;
		if (of_device_is_compatible(np, compatible) && of_node_get(np))
			break;
	}
	of_node_put(from);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_compatible_node);

/**
 *	of_find_node_with_property - Find a node which has a property with
 *                                   the given name.
 *	@from:		The node to start searching from or NULL, the node
 *			you pass will not be searched, only the next one
 *			will; typically, you pass what the previous call
 *			returned. of_node_put() will be called on it
 *	@prop_name:	The name of the property to look for.
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_node_with_property(struct device_node *from,
	const char *prop_name)
{
	struct device_node *np;
	struct property *pp;

	read_lock(&devtree_lock);
	np = from ? from->allnext : allnodes;
	for (; np; np = np->allnext) {
		for (pp = np->properties; pp != 0; pp = pp->next) {
			if (of_prop_cmp(pp->name, prop_name) == 0) {
				of_node_get(np);
				goto out;
			}
		}
	}
out:
	of_node_put(from);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_node_with_property);

/**
 * of_match_node - Tell if an device_node has a matching of_match structure
 *	@matches:	array of of device match structures to search in
 *	@node:		the of device structure to match against
 *
 *	Low level utility function used by device matching.
 */
const struct of_device_id *of_match_node(const struct of_device_id *matches,
					 const struct device_node *node)
{
	while (matches->name[0] || matches->type[0] || matches->compatible[0]) {
		int match = 1;
		if (matches->name[0])
			match &= node->name
				&& !strcmp(matches->name, node->name);
		if (matches->type[0])
			match &= node->type
				&& !strcmp(matches->type, node->type);
		if (matches->compatible[0])
			match &= of_device_is_compatible(node,
						matches->compatible);
		if (match)
			return matches;
		matches++;
	}
	return NULL;
}
EXPORT_SYMBOL(of_match_node);

/**
 *	of_find_matching_node - Find a node based on an of_device_id match
 *				table.
 *	@from:		The node to start searching from or NULL, the node
 *			you pass will not be searched, only the next one
 *			will; typically, you pass what the previous call
 *			returned. of_node_put() will be called on it
 *	@matches:	array of of device match structures to search in
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_matching_node(struct device_node *from,
					  const struct of_device_id *matches)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	np = from ? from->allnext : allnodes;
	for (; np; np = np->allnext) {
		if (of_match_node(matches, np) && of_node_get(np))
			break;
	}
	of_node_put(from);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_matching_node);

/**
 * of_modalias_table: Table of explicit compatible ==> modalias mappings
 *
 * This table allows particulare compatible property values to be mapped
 * to modalias strings.  This is useful for busses which do not directly
 * understand the OF device tree but are populated based on data contained
 * within the device tree.  SPI and I2C are the two current users of this
 * table.
 *
 * In most cases, devices do not need to be listed in this table because
 * the modalias value can be derived directly from the compatible table.
 * However, if for any reason a value cannot be derived, then this table
 * provides a method to override the implicit derivation.
 *
 * At the moment, a single table is used for all bus types because it is
 * assumed that the data size is small and that the compatible values
 * should already be distinct enough to differentiate between SPI, I2C
 * and other devices.
 */
struct of_modalias_table {
	char *of_device;
	char *modalias;
};
static struct of_modalias_table of_modalias_table[] = {
	{ "fsl,mcu-mpc8349emitx", "mcu-mpc8349emitx" },
};

/**
 * of_modalias_node - Lookup appropriate modalias for a device node
 * @node:	pointer to a device tree node
 * @modalias:	Pointer to buffer that modalias value will be copied into
 * @len:	Length of modalias value
 *
 * Based on the value of the compatible property, this routine will determine
 * an appropriate modalias value for a particular device tree node.  Two
 * separate methods are attempted to derive a modalias value.
 *
 * First method is to lookup the compatible value in of_modalias_table.
 * Second is to strip off the manufacturer prefix from the first
 * compatible entry and use the remainder as modalias
 *
 * This routine returns 0 on success
 */
int of_modalias_node(struct device_node *node, char *modalias, int len)
{
	int i, cplen;
	const char *compatible;
	const char *p;

	/* 1. search for exception list entry */
	for (i = 0; i < ARRAY_SIZE(of_modalias_table); i++) {
		compatible = of_modalias_table[i].of_device;
		if (!of_device_is_compatible(node, compatible))
			continue;
		strlcpy(modalias, of_modalias_table[i].modalias, len);
		return 0;
	}

	compatible = of_get_property(node, "compatible", &cplen);
	if (!compatible)
		return -ENODEV;

	/* 2. take first compatible entry and strip manufacturer */
	p = strchr(compatible, ',');
	if (!p)
		return -ENODEV;
	p++;
	strlcpy(modalias, p, len);
	return 0;
}
EXPORT_SYMBOL_GPL(of_modalias_node);

/**
 * of_parse_phandles_with_args - Find a node pointed by phandle in a list
 * @np:		pointer to a device tree node containing a list
 * @list_name:	property name that contains a list
 * @cells_name:	property name that specifies phandles' arguments count
 * @index:	index of a phandle to parse out
 * @out_node:	optional pointer to device_node struct pointer (will be filled)
 * @out_args:	optional pointer to arguments pointer (will be filled)
 *
 * This function is useful to parse lists of phandles and their arguments.
 * Returns 0 on success and fills out_node and out_args, on error returns
 * appropriate errno value.
 *
 * Example:
 *
 * phandle1: node1 {
 * 	#list-cells = <2>;
 * }
 *
 * phandle2: node2 {
 * 	#list-cells = <1>;
 * }
 *
 * node3 {
 * 	list = <&phandle1 1 2 &phandle2 3>;
 * }
 *
 * To get a device_node of the `node2' node you may call this:
 * of_parse_phandles_with_args(node3, "list", "#list-cells", 2, &node2, &args);
 */
int of_parse_phandles_with_args(struct device_node *np, const char *list_name,
				const char *cells_name, int index,
				struct device_node **out_node,
				const void **out_args)
{
	int ret = -EINVAL;
	const u32 *list;
	const u32 *list_end;
	int size;
	int cur_index = 0;
	struct device_node *node = NULL;
	const void *args = NULL;

	list = of_get_property(np, list_name, &size);
	if (!list) {
		ret = -ENOENT;
		goto err0;
	}
	list_end = list + size / sizeof(*list);

	while (list < list_end) {
		const u32 *cells;
		const phandle *phandle;

		phandle = list++;
		args = list;

		/* one cell hole in the list = <>; */
		if (!*phandle)
			goto next;

		node = of_find_node_by_phandle(*phandle);
		if (!node) {
			pr_debug("%s: could not find phandle\n",
				 np->full_name);
			goto err0;
		}

		cells = of_get_property(node, cells_name, &size);
		if (!cells || size != sizeof(*cells)) {
			pr_debug("%s: could not get %s for %s\n",
				 np->full_name, cells_name, node->full_name);
			goto err1;
		}

		list += *cells;
		if (list > list_end) {
			pr_debug("%s: insufficient arguments length\n",
				 np->full_name);
			goto err1;
		}
next:
		if (cur_index == index)
			break;

		of_node_put(node);
		node = NULL;
		args = NULL;
		cur_index++;
	}

	if (!node) {
		/*
		 * args w/o node indicates that the loop above has stopped at
		 * the 'hole' cell. Report this differently.
		 */
		if (args)
			ret = -EEXIST;
		else
			ret = -ENOENT;
		goto err0;
	}

	if (out_node)
		*out_node = node;
	if (out_args)
		*out_args = args;

	return 0;
err1:
	of_node_put(node);
err0:
	pr_debug("%s failed with status %d\n", __func__, ret);
	return ret;
}
EXPORT_SYMBOL(of_parse_phandles_with_args);
