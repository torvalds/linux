// SPDX-License-Identifier: GPL-2.0+
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
 *  Reconsolidated from arch/x/kernel/prom.c by Stephen Rothwell and
 *  Grant Likely.
 */

#define pr_fmt(fmt)	"OF: " fmt

#include <linux/cleanup.h>
#include <linux/console.h>
#include <linux/ctype.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_graph.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/proc_fs.h>

#include "of_private.h"

LIST_HEAD(aliases_lookup);

struct device_node *of_root;
EXPORT_SYMBOL(of_root);
struct device_node *of_chosen;
EXPORT_SYMBOL(of_chosen);
struct device_node *of_aliases;
struct device_node *of_stdout;
static const char *of_stdout_options;

struct kset *of_kset;

/*
 * Used to protect the of_aliases, to hold off addition of nodes to sysfs.
 * This mutex must be held whenever modifications are being made to the
 * device tree. The of_{attach,detach}_node() and
 * of_{add,remove,update}_property() helpers make sure this happens.
 */
DEFINE_MUTEX(of_mutex);

/* use when traversing tree through the child, sibling,
 * or parent members of struct device_node.
 */
DEFINE_RAW_SPINLOCK(devtree_lock);

bool of_node_name_eq(const struct device_node *np, const char *name)
{
	const char *node_name;
	size_t len;

	if (!np)
		return false;

	node_name = kbasename(np->full_name);
	len = strchrnul(node_name, '@') - node_name;

	return (strlen(name) == len) && (strncmp(node_name, name, len) == 0);
}
EXPORT_SYMBOL(of_node_name_eq);

bool of_node_name_prefix(const struct device_node *np, const char *prefix)
{
	if (!np)
		return false;

	return strncmp(kbasename(np->full_name), prefix, strlen(prefix)) == 0;
}
EXPORT_SYMBOL(of_node_name_prefix);

static bool __of_node_is_type(const struct device_node *np, const char *type)
{
	const char *match = __of_get_property(np, "device_type", NULL);

	return np && match && type && !strcmp(match, type);
}

#define EXCLUDED_DEFAULT_CELLS_PLATFORMS ( \
	IS_ENABLED(CONFIG_SPARC) \
)

int of_bus_n_addr_cells(struct device_node *np)
{
	u32 cells;

	for (; np; np = np->parent) {
		if (!of_property_read_u32(np, "#address-cells", &cells))
			return cells;
		/*
		 * Default root value and walking parent nodes for "#address-cells"
		 * is deprecated. Any platforms which hit this warning should
		 * be added to the excluded list.
		 */
		WARN_ONCE(!EXCLUDED_DEFAULT_CELLS_PLATFORMS,
			  "Missing '#address-cells' in %pOF\n", np);
	}
	return OF_ROOT_NODE_ADDR_CELLS_DEFAULT;
}

int of_n_addr_cells(struct device_node *np)
{
	if (np->parent)
		np = np->parent;

	return of_bus_n_addr_cells(np);
}
EXPORT_SYMBOL(of_n_addr_cells);

int of_bus_n_size_cells(struct device_node *np)
{
	u32 cells;

	for (; np; np = np->parent) {
		if (!of_property_read_u32(np, "#size-cells", &cells))
			return cells;
		/*
		 * Default root value and walking parent nodes for "#size-cells"
		 * is deprecated. Any platforms which hit this warning should
		 * be added to the excluded list.
		 */
		WARN_ONCE(!EXCLUDED_DEFAULT_CELLS_PLATFORMS,
			  "Missing '#size-cells' in %pOF\n", np);
	}
	return OF_ROOT_NODE_SIZE_CELLS_DEFAULT;
}

int of_n_size_cells(struct device_node *np)
{
	if (np->parent)
		np = np->parent;

	return of_bus_n_size_cells(np);
}
EXPORT_SYMBOL(of_n_size_cells);

#ifdef CONFIG_NUMA
int __weak of_node_to_nid(struct device_node *np)
{
	return NUMA_NO_NODE;
}
#endif

#define OF_PHANDLE_CACHE_BITS	7
#define OF_PHANDLE_CACHE_SZ	BIT(OF_PHANDLE_CACHE_BITS)

static struct device_node *phandle_cache[OF_PHANDLE_CACHE_SZ];

static u32 of_phandle_cache_hash(phandle handle)
{
	return hash_32(handle, OF_PHANDLE_CACHE_BITS);
}

/*
 * Caller must hold devtree_lock.
 */
void __of_phandle_cache_inv_entry(phandle handle)
{
	u32 handle_hash;
	struct device_node *np;

	if (!handle)
		return;

	handle_hash = of_phandle_cache_hash(handle);

	np = phandle_cache[handle_hash];
	if (np && handle == np->phandle)
		phandle_cache[handle_hash] = NULL;
}

void __init of_core_init(void)
{
	struct device_node *np;

	of_platform_register_reconfig_notifier();

	/* Create the kset, and register existing nodes */
	mutex_lock(&of_mutex);
	of_kset = kset_create_and_add("devicetree", NULL, firmware_kobj);
	if (!of_kset) {
		mutex_unlock(&of_mutex);
		pr_err("failed to register existing nodes\n");
		return;
	}
	for_each_of_allnodes(np) {
		__of_attach_node_sysfs(np);
		if (np->phandle && !phandle_cache[of_phandle_cache_hash(np->phandle)])
			phandle_cache[of_phandle_cache_hash(np->phandle)] = np;
	}
	mutex_unlock(&of_mutex);

	/* Symlink in /proc as required by userspace ABI */
	if (of_root)
		proc_symlink("device-tree", NULL, "/sys/firmware/devicetree/base");
}

static struct property *__of_find_property(const struct device_node *np,
					   const char *name, int *lenp)
{
	struct property *pp;

	if (!np)
		return NULL;

	for (pp = np->properties; pp; pp = pp->next) {
		if (of_prop_cmp(pp->name, name) == 0) {
			if (lenp)
				*lenp = pp->length;
			break;
		}
	}

	return pp;
}

struct property *of_find_property(const struct device_node *np,
				  const char *name,
				  int *lenp)
{
	struct property *pp;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	pp = __of_find_property(np, name, lenp);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	return pp;
}
EXPORT_SYMBOL(of_find_property);

struct device_node *__of_find_all_nodes(struct device_node *prev)
{
	struct device_node *np;
	if (!prev) {
		np = of_root;
	} else if (prev->child) {
		np = prev->child;
	} else {
		/* Walk back up looking for a sibling, or the end of the structure */
		np = prev;
		while (np->parent && !np->sibling)
			np = np->parent;
		np = np->sibling; /* Might be null at the end of the tree */
	}
	return np;
}

/**
 * of_find_all_nodes - Get next node in global list
 * @prev:	Previous node or NULL to start iteration
 *		of_node_put() will be called on it
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.
 */
struct device_node *of_find_all_nodes(struct device_node *prev)
{
	struct device_node *np;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	np = __of_find_all_nodes(prev);
	of_node_get(np);
	of_node_put(prev);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return np;
}
EXPORT_SYMBOL(of_find_all_nodes);

/*
 * Find a property with a given name for a given node
 * and return the value.
 */
const void *__of_get_property(const struct device_node *np,
			      const char *name, int *lenp)
{
	const struct property *pp = __of_find_property(np, name, lenp);

	return pp ? pp->value : NULL;
}

/*
 * Find a property with a given name for a given node
 * and return the value.
 */
const void *of_get_property(const struct device_node *np, const char *name,
			    int *lenp)
{
	const struct property *pp = of_find_property(np, name, lenp);

	return pp ? pp->value : NULL;
}
EXPORT_SYMBOL(of_get_property);

/**
 * __of_device_is_compatible() - Check if the node matches given constraints
 * @device: pointer to node
 * @compat: required compatible string, NULL or "" for any match
 * @type: required device_type value, NULL or "" for any match
 * @name: required node name, NULL or "" for any match
 *
 * Checks if the given @compat, @type and @name strings match the
 * properties of the given @device. A constraints can be skipped by
 * passing NULL or an empty string as the constraint.
 *
 * Returns 0 for no match, and a positive integer on match. The return
 * value is a relative score with larger values indicating better
 * matches. The score is weighted for the most specific compatible value
 * to get the highest score. Matching type is next, followed by matching
 * name. Practically speaking, this results in the following priority
 * order for matches:
 *
 * 1. specific compatible && type && name
 * 2. specific compatible && type
 * 3. specific compatible && name
 * 4. specific compatible
 * 5. general compatible && type && name
 * 6. general compatible && type
 * 7. general compatible && name
 * 8. general compatible
 * 9. type && name
 * 10. type
 * 11. name
 */
static int __of_device_is_compatible(const struct device_node *device,
				     const char *compat, const char *type, const char *name)
{
	const struct property *prop;
	const char *cp;
	int index = 0, score = 0;

	/* Compatible match has highest priority */
	if (compat && compat[0]) {
		prop = __of_find_property(device, "compatible", NULL);
		for (cp = of_prop_next_string(prop, NULL); cp;
		     cp = of_prop_next_string(prop, cp), index++) {
			if (of_compat_cmp(cp, compat, strlen(compat)) == 0) {
				score = INT_MAX/2 - (index << 2);
				break;
			}
		}
		if (!score)
			return 0;
	}

	/* Matching type is better than matching name */
	if (type && type[0]) {
		if (!__of_node_is_type(device, type))
			return 0;
		score += 2;
	}

	/* Matching name is a bit better than not */
	if (name && name[0]) {
		if (!of_node_name_eq(device, name))
			return 0;
		score++;
	}

	return score;
}

/** Checks if the given "compat" string matches one of the strings in
 * the device's "compatible" property
 */
int of_device_is_compatible(const struct device_node *device,
		const char *compat)
{
	unsigned long flags;
	int res;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	res = __of_device_is_compatible(device, compat, NULL, NULL);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return res;
}
EXPORT_SYMBOL(of_device_is_compatible);

/** Checks if the device is compatible with any of the entries in
 *  a NULL terminated array of strings. Returns the best match
 *  score or 0.
 */
int of_device_compatible_match(const struct device_node *device,
			       const char *const *compat)
{
	unsigned int tmp, score = 0;

	if (!compat)
		return 0;

	while (*compat) {
		tmp = of_device_is_compatible(device, *compat);
		if (tmp > score)
			score = tmp;
		compat++;
	}

	return score;
}
EXPORT_SYMBOL_GPL(of_device_compatible_match);

/**
 * of_machine_compatible_match - Test root of device tree against a compatible array
 * @compats: NULL terminated array of compatible strings to look for in root node's compatible property.
 *
 * Returns true if the root node has any of the given compatible values in its
 * compatible property.
 */
bool of_machine_compatible_match(const char *const *compats)
{
	struct device_node *root;
	int rc = 0;

	root = of_find_node_by_path("/");
	if (root) {
		rc = of_device_compatible_match(root, compats);
		of_node_put(root);
	}

	return rc != 0;
}
EXPORT_SYMBOL(of_machine_compatible_match);

static bool __of_device_is_status(const struct device_node *device,
				  const char * const*strings)
{
	const char *status;
	int statlen;

	if (!device)
		return false;

	status = __of_get_property(device, "status", &statlen);
	if (status == NULL)
		return false;

	if (statlen > 0) {
		while (*strings) {
			unsigned int len = strlen(*strings);

			if ((*strings)[len - 1] == '-') {
				if (!strncmp(status, *strings, len))
					return true;
			} else {
				if (!strcmp(status, *strings))
					return true;
			}
			strings++;
		}
	}

	return false;
}

/**
 *  __of_device_is_available - check if a device is available for use
 *
 *  @device: Node to check for availability, with locks already held
 *
 *  Return: True if the status property is absent or set to "okay" or "ok",
 *  false otherwise
 */
static bool __of_device_is_available(const struct device_node *device)
{
	static const char * const ok[] = {"okay", "ok", NULL};

	if (!device)
		return false;

	return !__of_get_property(device, "status", NULL) ||
		__of_device_is_status(device, ok);
}

/**
 *  __of_device_is_reserved - check if a device is reserved
 *
 *  @device: Node to check for availability, with locks already held
 *
 *  Return: True if the status property is set to "reserved", false otherwise
 */
static bool __of_device_is_reserved(const struct device_node *device)
{
	static const char * const reserved[] = {"reserved", NULL};

	return __of_device_is_status(device, reserved);
}

/**
 *  of_device_is_available - check if a device is available for use
 *
 *  @device: Node to check for availability
 *
 *  Return: True if the status property is absent or set to "okay" or "ok",
 *  false otherwise
 */
bool of_device_is_available(const struct device_node *device)
{
	unsigned long flags;
	bool res;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	res = __of_device_is_available(device);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return res;

}
EXPORT_SYMBOL(of_device_is_available);

/**
 *  __of_device_is_fail - check if a device has status "fail" or "fail-..."
 *
 *  @device: Node to check status for, with locks already held
 *
 *  Return: True if the status property is set to "fail" or "fail-..." (for any
 *  error code suffix), false otherwise
 */
static bool __of_device_is_fail(const struct device_node *device)
{
	static const char * const fail[] = {"fail", "fail-", NULL};

	return __of_device_is_status(device, fail);
}

/**
 *  of_device_is_big_endian - check if a device has BE registers
 *
 *  @device: Node to check for endianness
 *
 *  Return: True if the device has a "big-endian" property, or if the kernel
 *  was compiled for BE *and* the device has a "native-endian" property.
 *  Returns false otherwise.
 *
 *  Callers would nominally use ioread32be/iowrite32be if
 *  of_device_is_big_endian() == true, or readl/writel otherwise.
 */
bool of_device_is_big_endian(const struct device_node *device)
{
	if (of_property_read_bool(device, "big-endian"))
		return true;
	if (IS_ENABLED(CONFIG_CPU_BIG_ENDIAN) &&
	    of_property_read_bool(device, "native-endian"))
		return true;
	return false;
}
EXPORT_SYMBOL(of_device_is_big_endian);

/**
 * of_get_parent - Get a node's parent if any
 * @node:	Node to get parent
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.
 */
struct device_node *of_get_parent(const struct device_node *node)
{
	struct device_node *np;
	unsigned long flags;

	if (!node)
		return NULL;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	np = of_node_get(node->parent);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return np;
}
EXPORT_SYMBOL(of_get_parent);

/**
 * of_get_next_parent - Iterate to a node's parent
 * @node:	Node to get parent of
 *
 * This is like of_get_parent() except that it drops the
 * refcount on the passed node, making it suitable for iterating
 * through a node's parents.
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.
 */
struct device_node *of_get_next_parent(struct device_node *node)
{
	struct device_node *parent;
	unsigned long flags;

	if (!node)
		return NULL;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	parent = of_node_get(node->parent);
	of_node_put(node);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return parent;
}
EXPORT_SYMBOL(of_get_next_parent);

static struct device_node *__of_get_next_child(const struct device_node *node,
						struct device_node *prev)
{
	struct device_node *next;

	if (!node)
		return NULL;

	next = prev ? prev->sibling : node->child;
	of_node_get(next);
	of_node_put(prev);
	return next;
}
#define __for_each_child_of_node(parent, child) \
	for (child = __of_get_next_child(parent, NULL); child != NULL; \
	     child = __of_get_next_child(parent, child))

/**
 * of_get_next_child - Iterate a node childs
 * @node:	parent node
 * @prev:	previous child of the parent node, or NULL to get first
 *
 * Return: A node pointer with refcount incremented, use of_node_put() on
 * it when done. Returns NULL when prev is the last child. Decrements the
 * refcount of prev.
 */
struct device_node *of_get_next_child(const struct device_node *node,
	struct device_node *prev)
{
	struct device_node *next;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	next = __of_get_next_child(node, prev);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return next;
}
EXPORT_SYMBOL(of_get_next_child);

static struct device_node *of_get_next_status_child(const struct device_node *node,
						    struct device_node *prev,
						    bool (*checker)(const struct device_node *))
{
	struct device_node *next;
	unsigned long flags;

	if (!node)
		return NULL;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	next = prev ? prev->sibling : node->child;
	for (; next; next = next->sibling) {
		if (!checker(next))
			continue;
		if (of_node_get(next))
			break;
	}
	of_node_put(prev);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return next;
}

/**
 * of_get_next_available_child - Find the next available child node
 * @node:	parent node
 * @prev:	previous child of the parent node, or NULL to get first
 *
 * This function is like of_get_next_child(), except that it
 * automatically skips any disabled nodes (i.e. status = "disabled").
 */
struct device_node *of_get_next_available_child(const struct device_node *node,
	struct device_node *prev)
{
	return of_get_next_status_child(node, prev, __of_device_is_available);
}
EXPORT_SYMBOL(of_get_next_available_child);

/**
 * of_get_next_reserved_child - Find the next reserved child node
 * @node:	parent node
 * @prev:	previous child of the parent node, or NULL to get first
 *
 * This function is like of_get_next_child(), except that it
 * automatically skips any disabled nodes (i.e. status = "disabled").
 */
struct device_node *of_get_next_reserved_child(const struct device_node *node,
						struct device_node *prev)
{
	return of_get_next_status_child(node, prev, __of_device_is_reserved);
}
EXPORT_SYMBOL(of_get_next_reserved_child);

/**
 * of_get_next_cpu_node - Iterate on cpu nodes
 * @prev:	previous child of the /cpus node, or NULL to get first
 *
 * Unusable CPUs (those with the status property set to "fail" or "fail-...")
 * will be skipped.
 *
 * Return: A cpu node pointer with refcount incremented, use of_node_put()
 * on it when done. Returns NULL when prev is the last child. Decrements
 * the refcount of prev.
 */
struct device_node *of_get_next_cpu_node(struct device_node *prev)
{
	struct device_node *next = NULL;
	unsigned long flags;
	struct device_node *node;

	if (!prev)
		node = of_find_node_by_path("/cpus");

	raw_spin_lock_irqsave(&devtree_lock, flags);
	if (prev)
		next = prev->sibling;
	else if (node) {
		next = node->child;
		of_node_put(node);
	}
	for (; next; next = next->sibling) {
		if (__of_device_is_fail(next))
			continue;
		if (!(of_node_name_eq(next, "cpu") ||
		      __of_node_is_type(next, "cpu")))
			continue;
		if (of_node_get(next))
			break;
	}
	of_node_put(prev);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return next;
}
EXPORT_SYMBOL(of_get_next_cpu_node);

/**
 * of_get_compatible_child - Find compatible child node
 * @parent:	parent node
 * @compatible:	compatible string
 *
 * Lookup child node whose compatible property contains the given compatible
 * string.
 *
 * Return: a node pointer with refcount incremented, use of_node_put() on it
 * when done; or NULL if not found.
 */
struct device_node *of_get_compatible_child(const struct device_node *parent,
				const char *compatible)
{
	struct device_node *child;

	for_each_child_of_node(parent, child) {
		if (of_device_is_compatible(child, compatible))
			break;
	}

	return child;
}
EXPORT_SYMBOL(of_get_compatible_child);

/**
 * of_get_child_by_name - Find the child node by name for a given parent
 * @node:	parent node
 * @name:	child name to look for.
 *
 * This function looks for child node for given matching name
 *
 * Return: A node pointer if found, with refcount incremented, use
 * of_node_put() on it when done.
 * Returns NULL if node is not found.
 */
struct device_node *of_get_child_by_name(const struct device_node *node,
				const char *name)
{
	struct device_node *child;

	for_each_child_of_node(node, child)
		if (of_node_name_eq(child, name))
			break;
	return child;
}
EXPORT_SYMBOL(of_get_child_by_name);

struct device_node *__of_find_node_by_path(const struct device_node *parent,
						const char *path)
{
	struct device_node *child;
	int len;

	len = strcspn(path, "/:");
	if (!len)
		return NULL;

	__for_each_child_of_node(parent, child) {
		const char *name = kbasename(child->full_name);
		if (strncmp(path, name, len) == 0 && (strlen(name) == len))
			return child;
	}
	return NULL;
}

struct device_node *__of_find_node_by_full_path(struct device_node *node,
						const char *path)
{
	const char *separator = strchr(path, ':');

	while (node && *path == '/') {
		struct device_node *tmp = node;

		path++; /* Increment past '/' delimiter */
		node = __of_find_node_by_path(node, path);
		of_node_put(tmp);
		path = strchrnul(path, '/');
		if (separator && separator < path)
			break;
	}
	return node;
}

/**
 * of_find_node_opts_by_path - Find a node matching a full OF path
 * @path: Either the full path to match, or if the path does not
 *       start with '/', the name of a property of the /aliases
 *       node (an alias).  In the case of an alias, the node
 *       matching the alias' value will be returned.
 * @opts: Address of a pointer into which to store the start of
 *       an options string appended to the end of the path with
 *       a ':' separator.
 *
 * Valid paths:
 *  * /foo/bar	Full path
 *  * foo	Valid alias
 *  * foo/bar	Valid alias + relative path
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.
 */
struct device_node *of_find_node_opts_by_path(const char *path, const char **opts)
{
	struct device_node *np = NULL;
	const struct property *pp;
	unsigned long flags;
	const char *separator = strchr(path, ':');

	if (opts)
		*opts = separator ? separator + 1 : NULL;

	if (strcmp(path, "/") == 0)
		return of_node_get(of_root);

	/* The path could begin with an alias */
	if (*path != '/') {
		int len;
		const char *p = separator;

		if (!p)
			p = strchrnul(path, '/');
		len = p - path;

		/* of_aliases must not be NULL */
		if (!of_aliases)
			return NULL;

		for_each_property_of_node(of_aliases, pp) {
			if (strlen(pp->name) == len && !strncmp(pp->name, path, len)) {
				np = of_find_node_by_path(pp->value);
				break;
			}
		}
		if (!np)
			return NULL;
		path = p;
	}

	/* Step down the tree matching path components */
	raw_spin_lock_irqsave(&devtree_lock, flags);
	if (!np)
		np = of_node_get(of_root);
	np = __of_find_node_by_full_path(np, path);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return np;
}
EXPORT_SYMBOL(of_find_node_opts_by_path);

/**
 * of_find_node_by_name - Find a node by its "name" property
 * @from:	The node to start searching from or NULL; the node
 *		you pass will not be searched, only the next one
 *		will. Typically, you pass what the previous call
 *		returned. of_node_put() will be called on @from.
 * @name:	The name string to match against
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.
 */
struct device_node *of_find_node_by_name(struct device_node *from,
	const char *name)
{
	struct device_node *np;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	for_each_of_allnodes_from(from, np)
		if (of_node_name_eq(np, name) && of_node_get(np))
			break;
	of_node_put(from);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_name);

/**
 * of_find_node_by_type - Find a node by its "device_type" property
 * @from:	The node to start searching from, or NULL to start searching
 *		the entire device tree. The node you pass will not be
 *		searched, only the next one will; typically, you pass
 *		what the previous call returned. of_node_put() will be
 *		called on from for you.
 * @type:	The type string to match against
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.
 */
struct device_node *of_find_node_by_type(struct device_node *from,
	const char *type)
{
	struct device_node *np;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	for_each_of_allnodes_from(from, np)
		if (__of_node_is_type(np, type) && of_node_get(np))
			break;
	of_node_put(from);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_type);

/**
 * of_find_compatible_node - Find a node based on type and one of the
 *                                tokens in its "compatible" property
 * @from:	The node to start searching from or NULL, the node
 *		you pass will not be searched, only the next one
 *		will; typically, you pass what the previous call
 *		returned. of_node_put() will be called on it
 * @type:	The type string to match "device_type" or NULL to ignore
 * @compatible:	The string to match to one of the tokens in the device
 *		"compatible" list.
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.
 */
struct device_node *of_find_compatible_node(struct device_node *from,
	const char *type, const char *compatible)
{
	struct device_node *np;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	for_each_of_allnodes_from(from, np)
		if (__of_device_is_compatible(np, compatible, type, NULL) &&
		    of_node_get(np))
			break;
	of_node_put(from);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return np;
}
EXPORT_SYMBOL(of_find_compatible_node);

/**
 * of_find_node_with_property - Find a node which has a property with
 *                              the given name.
 * @from:	The node to start searching from or NULL, the node
 *		you pass will not be searched, only the next one
 *		will; typically, you pass what the previous call
 *		returned. of_node_put() will be called on it
 * @prop_name:	The name of the property to look for.
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.
 */
struct device_node *of_find_node_with_property(struct device_node *from,
	const char *prop_name)
{
	struct device_node *np;
	const struct property *pp;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	for_each_of_allnodes_from(from, np) {
		for (pp = np->properties; pp; pp = pp->next) {
			if (of_prop_cmp(pp->name, prop_name) == 0) {
				of_node_get(np);
				goto out;
			}
		}
	}
out:
	of_node_put(from);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return np;
}
EXPORT_SYMBOL(of_find_node_with_property);

static
const struct of_device_id *__of_match_node(const struct of_device_id *matches,
					   const struct device_node *node)
{
	const struct of_device_id *best_match = NULL;
	int score, best_score = 0;

	if (!matches)
		return NULL;

	for (; matches->name[0] || matches->type[0] || matches->compatible[0]; matches++) {
		score = __of_device_is_compatible(node, matches->compatible,
						  matches->type, matches->name);
		if (score > best_score) {
			best_match = matches;
			best_score = score;
		}
	}

	return best_match;
}

/**
 * of_match_node - Tell if a device_node has a matching of_match structure
 * @matches:	array of of device match structures to search in
 * @node:	the of device structure to match against
 *
 * Low level utility function used by device matching.
 */
const struct of_device_id *of_match_node(const struct of_device_id *matches,
					 const struct device_node *node)
{
	const struct of_device_id *match;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	match = __of_match_node(matches, node);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return match;
}
EXPORT_SYMBOL(of_match_node);

/**
 * of_find_matching_node_and_match - Find a node based on an of_device_id
 *				     match table.
 * @from:	The node to start searching from or NULL, the node
 *		you pass will not be searched, only the next one
 *		will; typically, you pass what the previous call
 *		returned. of_node_put() will be called on it
 * @matches:	array of of device match structures to search in
 * @match:	Updated to point at the matches entry which matched
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.
 */
struct device_node *of_find_matching_node_and_match(struct device_node *from,
					const struct of_device_id *matches,
					const struct of_device_id **match)
{
	struct device_node *np;
	const struct of_device_id *m;
	unsigned long flags;

	if (match)
		*match = NULL;

	raw_spin_lock_irqsave(&devtree_lock, flags);
	for_each_of_allnodes_from(from, np) {
		m = __of_match_node(matches, np);
		if (m && of_node_get(np)) {
			if (match)
				*match = m;
			break;
		}
	}
	of_node_put(from);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return np;
}
EXPORT_SYMBOL(of_find_matching_node_and_match);

/**
 * of_alias_from_compatible - Lookup appropriate alias for a device node
 *			      depending on compatible
 * @node:	pointer to a device tree node
 * @alias:	Pointer to buffer that alias value will be copied into
 * @len:	Length of alias value
 *
 * Based on the value of the compatible property, this routine will attempt
 * to choose an appropriate alias value for a particular device tree node.
 * It does this by stripping the manufacturer prefix (as delimited by a ',')
 * from the first entry in the compatible list property.
 *
 * Note: The matching on just the "product" side of the compatible is a relic
 * from I2C and SPI. Please do not add any new user.
 *
 * Return: This routine returns 0 on success, <0 on failure.
 */
int of_alias_from_compatible(const struct device_node *node, char *alias, int len)
{
	const char *compatible, *p;
	int cplen;

	compatible = of_get_property(node, "compatible", &cplen);
	if (!compatible || strlen(compatible) > cplen)
		return -ENODEV;
	p = strchr(compatible, ',');
	strscpy(alias, p ? p + 1 : compatible, len);
	return 0;
}
EXPORT_SYMBOL_GPL(of_alias_from_compatible);

/**
 * of_find_node_by_phandle - Find a node given a phandle
 * @handle:	phandle of the node to find
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.
 */
struct device_node *of_find_node_by_phandle(phandle handle)
{
	struct device_node *np = NULL;
	unsigned long flags;
	u32 handle_hash;

	if (!handle)
		return NULL;

	handle_hash = of_phandle_cache_hash(handle);

	raw_spin_lock_irqsave(&devtree_lock, flags);

	if (phandle_cache[handle_hash] &&
	    handle == phandle_cache[handle_hash]->phandle)
		np = phandle_cache[handle_hash];

	if (!np) {
		for_each_of_allnodes(np)
			if (np->phandle == handle &&
			    !of_node_check_flag(np, OF_DETACHED)) {
				phandle_cache[handle_hash] = np;
				break;
			}
	}

	of_node_get(np);
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_phandle);

void of_print_phandle_args(const char *msg, const struct of_phandle_args *args)
{
	int i;
	printk("%s %pOF", msg, args->np);
	for (i = 0; i < args->args_count; i++) {
		const char delim = i ? ',' : ':';

		pr_cont("%c%08x", delim, args->args[i]);
	}
	pr_cont("\n");
}

int of_phandle_iterator_init(struct of_phandle_iterator *it,
		const struct device_node *np,
		const char *list_name,
		const char *cells_name,
		int cell_count)
{
	const __be32 *list;
	int size;

	memset(it, 0, sizeof(*it));

	/*
	 * one of cell_count or cells_name must be provided to determine the
	 * argument length.
	 */
	if (cell_count < 0 && !cells_name)
		return -EINVAL;

	list = of_get_property(np, list_name, &size);
	if (!list)
		return -ENOENT;

	it->cells_name = cells_name;
	it->cell_count = cell_count;
	it->parent = np;
	it->list_end = list + size / sizeof(*list);
	it->phandle_end = list;
	it->cur = list;

	return 0;
}
EXPORT_SYMBOL_GPL(of_phandle_iterator_init);

int of_phandle_iterator_next(struct of_phandle_iterator *it)
{
	uint32_t count = 0;

	if (it->node) {
		of_node_put(it->node);
		it->node = NULL;
	}

	if (!it->cur || it->phandle_end >= it->list_end)
		return -ENOENT;

	it->cur = it->phandle_end;

	/* If phandle is 0, then it is an empty entry with no arguments. */
	it->phandle = be32_to_cpup(it->cur++);

	if (it->phandle) {

		/*
		 * Find the provider node and parse the #*-cells property to
		 * determine the argument length.
		 */
		it->node = of_find_node_by_phandle(it->phandle);

		if (it->cells_name) {
			if (!it->node) {
				pr_err("%pOF: could not find phandle %d\n",
				       it->parent, it->phandle);
				goto err;
			}

			if (of_property_read_u32(it->node, it->cells_name,
						 &count)) {
				/*
				 * If both cell_count and cells_name is given,
				 * fall back to cell_count in absence
				 * of the cells_name property
				 */
				if (it->cell_count >= 0) {
					count = it->cell_count;
				} else {
					pr_err("%pOF: could not get %s for %pOF\n",
					       it->parent,
					       it->cells_name,
					       it->node);
					goto err;
				}
			}
		} else {
			count = it->cell_count;
		}

		/*
		 * Make sure that the arguments actually fit in the remaining
		 * property data length
		 */
		if (it->cur + count > it->list_end) {
			if (it->cells_name)
				pr_err("%pOF: %s = %d found %td\n",
					it->parent, it->cells_name,
					count, it->list_end - it->cur);
			else
				pr_err("%pOF: phandle %s needs %d, found %td\n",
					it->parent, of_node_full_name(it->node),
					count, it->list_end - it->cur);
			goto err;
		}
	}

	it->phandle_end = it->cur + count;
	it->cur_count = count;

	return 0;

err:
	if (it->node) {
		of_node_put(it->node);
		it->node = NULL;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(of_phandle_iterator_next);

int of_phandle_iterator_args(struct of_phandle_iterator *it,
			     uint32_t *args,
			     int size)
{
	int i, count;

	count = it->cur_count;

	if (WARN_ON(size < count))
		count = size;

	for (i = 0; i < count; i++)
		args[i] = be32_to_cpup(it->cur++);

	return count;
}

int __of_parse_phandle_with_args(const struct device_node *np,
				 const char *list_name,
				 const char *cells_name,
				 int cell_count, int index,
				 struct of_phandle_args *out_args)
{
	struct of_phandle_iterator it;
	int rc, cur_index = 0;

	if (index < 0)
		return -EINVAL;

	/* Loop over the phandles until all the requested entry is found */
	of_for_each_phandle(&it, rc, np, list_name, cells_name, cell_count) {
		/*
		 * All of the error cases bail out of the loop, so at
		 * this point, the parsing is successful. If the requested
		 * index matches, then fill the out_args structure and return,
		 * or return -ENOENT for an empty entry.
		 */
		rc = -ENOENT;
		if (cur_index == index) {
			if (!it.phandle)
				goto err;

			if (out_args) {
				int c;

				c = of_phandle_iterator_args(&it,
							     out_args->args,
							     MAX_PHANDLE_ARGS);
				out_args->np = it.node;
				out_args->args_count = c;
			} else {
				of_node_put(it.node);
			}

			/* Found it! return success */
			return 0;
		}

		cur_index++;
	}

	/*
	 * Unlock node before returning result; will be one of:
	 * -ENOENT : index is for empty phandle
	 * -EINVAL : parsing error on data
	 */

 err:
	of_node_put(it.node);
	return rc;
}
EXPORT_SYMBOL(__of_parse_phandle_with_args);

/**
 * of_parse_phandle_with_args_map() - Find a node pointed by phandle in a list and remap it
 * @np:		pointer to a device tree node containing a list
 * @list_name:	property name that contains a list
 * @stem_name:	stem of property names that specify phandles' arguments count
 * @index:	index of a phandle to parse out
 * @out_args:	optional pointer to output arguments structure (will be filled)
 *
 * This function is useful to parse lists of phandles and their arguments.
 * Returns 0 on success and fills out_args, on error returns appropriate errno
 * value. The difference between this function and of_parse_phandle_with_args()
 * is that this API remaps a phandle if the node the phandle points to has
 * a <@stem_name>-map property.
 *
 * Caller is responsible to call of_node_put() on the returned out_args->np
 * pointer.
 *
 * Example::
 *
 *  phandle1: node1 {
 *  	#list-cells = <2>;
 *  };
 *
 *  phandle2: node2 {
 *  	#list-cells = <1>;
 *  };
 *
 *  phandle3: node3 {
 *  	#list-cells = <1>;
 *  	list-map = <0 &phandle2 3>,
 *  		   <1 &phandle2 2>,
 *  		   <2 &phandle1 5 1>;
 *  	list-map-mask = <0x3>;
 *  };
 *
 *  node4 {
 *  	list = <&phandle1 1 2 &phandle3 0>;
 *  };
 *
 * To get a device_node of the ``node2`` node you may call this:
 * of_parse_phandle_with_args(node4, "list", "list", 1, &args);
 */
int of_parse_phandle_with_args_map(const struct device_node *np,
				   const char *list_name,
				   const char *stem_name,
				   int index, struct of_phandle_args *out_args)
{
	char *cells_name __free(kfree) = kasprintf(GFP_KERNEL, "#%s-cells", stem_name);
	char *map_name __free(kfree) = kasprintf(GFP_KERNEL, "%s-map", stem_name);
	char *mask_name __free(kfree) = kasprintf(GFP_KERNEL, "%s-map-mask", stem_name);
	char *pass_name __free(kfree) = kasprintf(GFP_KERNEL, "%s-map-pass-thru", stem_name);
	struct device_node *cur, *new = NULL;
	const __be32 *map, *mask, *pass;
	static const __be32 dummy_mask[] = { [0 ... MAX_PHANDLE_ARGS] = cpu_to_be32(~0) };
	static const __be32 dummy_pass[] = { [0 ... MAX_PHANDLE_ARGS] = cpu_to_be32(0) };
	__be32 initial_match_array[MAX_PHANDLE_ARGS];
	const __be32 *match_array = initial_match_array;
	int i, ret, map_len, match;
	u32 list_size, new_size;

	if (index < 0)
		return -EINVAL;

	if (!cells_name || !map_name || !mask_name || !pass_name)
		return -ENOMEM;

	ret = __of_parse_phandle_with_args(np, list_name, cells_name, -1, index,
					   out_args);
	if (ret)
		return ret;

	/* Get the #<list>-cells property */
	cur = out_args->np;
	ret = of_property_read_u32(cur, cells_name, &list_size);
	if (ret < 0)
		goto put;

	/* Precalculate the match array - this simplifies match loop */
	for (i = 0; i < list_size; i++)
		initial_match_array[i] = cpu_to_be32(out_args->args[i]);

	ret = -EINVAL;
	while (cur) {
		/* Get the <list>-map property */
		map = of_get_property(cur, map_name, &map_len);
		if (!map) {
			return 0;
		}
		map_len /= sizeof(u32);

		/* Get the <list>-map-mask property (optional) */
		mask = of_get_property(cur, mask_name, NULL);
		if (!mask)
			mask = dummy_mask;
		/* Iterate through <list>-map property */
		match = 0;
		while (map_len > (list_size + 1) && !match) {
			/* Compare specifiers */
			match = 1;
			for (i = 0; i < list_size; i++, map_len--)
				match &= !((match_array[i] ^ *map++) & mask[i]);

			of_node_put(new);
			new = of_find_node_by_phandle(be32_to_cpup(map));
			map++;
			map_len--;

			/* Check if not found */
			if (!new)
				goto put;

			if (!of_device_is_available(new))
				match = 0;

			ret = of_property_read_u32(new, cells_name, &new_size);
			if (ret)
				goto put;

			/* Check for malformed properties */
			if (WARN_ON(new_size > MAX_PHANDLE_ARGS))
				goto put;
			if (map_len < new_size)
				goto put;

			/* Move forward by new node's #<list>-cells amount */
			map += new_size;
			map_len -= new_size;
		}
		if (!match)
			goto put;

		/* Get the <list>-map-pass-thru property (optional) */
		pass = of_get_property(cur, pass_name, NULL);
		if (!pass)
			pass = dummy_pass;

		/*
		 * Successfully parsed a <list>-map translation; copy new
		 * specifier into the out_args structure, keeping the
		 * bits specified in <list>-map-pass-thru.
		 */
		match_array = map - new_size;
		for (i = 0; i < new_size; i++) {
			__be32 val = *(map - new_size + i);

			if (i < list_size) {
				val &= ~pass[i];
				val |= cpu_to_be32(out_args->args[i]) & pass[i];
			}

			out_args->args[i] = be32_to_cpu(val);
		}
		out_args->args_count = list_size = new_size;
		/* Iterate again with new provider */
		out_args->np = new;
		of_node_put(cur);
		cur = new;
		new = NULL;
	}
put:
	of_node_put(cur);
	of_node_put(new);
	return ret;
}
EXPORT_SYMBOL(of_parse_phandle_with_args_map);

/**
 * of_count_phandle_with_args() - Find the number of phandles references in a property
 * @np:		pointer to a device tree node containing a list
 * @list_name:	property name that contains a list
 * @cells_name:	property name that specifies phandles' arguments count
 *
 * Return: The number of phandle + argument tuples within a property. It
 * is a typical pattern to encode a list of phandle and variable
 * arguments into a single property. The number of arguments is encoded
 * by a property in the phandle-target node. For example, a gpios
 * property would contain a list of GPIO specifies consisting of a
 * phandle and 1 or more arguments. The number of arguments are
 * determined by the #gpio-cells property in the node pointed to by the
 * phandle.
 */
int of_count_phandle_with_args(const struct device_node *np, const char *list_name,
				const char *cells_name)
{
	struct of_phandle_iterator it;
	int rc, cur_index = 0;

	/*
	 * If cells_name is NULL we assume a cell count of 0. This makes
	 * counting the phandles trivial as each 32bit word in the list is a
	 * phandle and no arguments are to consider. So we don't iterate through
	 * the list but just use the length to determine the phandle count.
	 */
	if (!cells_name) {
		const __be32 *list;
		int size;

		list = of_get_property(np, list_name, &size);
		if (!list)
			return -ENOENT;

		return size / sizeof(*list);
	}

	rc = of_phandle_iterator_init(&it, np, list_name, cells_name, -1);
	if (rc)
		return rc;

	while ((rc = of_phandle_iterator_next(&it)) == 0)
		cur_index += 1;

	if (rc != -ENOENT)
		return rc;

	return cur_index;
}
EXPORT_SYMBOL(of_count_phandle_with_args);

static struct property *__of_remove_property_from_list(struct property **list, struct property *prop)
{
	struct property **next;

	for (next = list; *next; next = &(*next)->next) {
		if (*next == prop) {
			*next = prop->next;
			prop->next = NULL;
			return prop;
		}
	}
	return NULL;
}

/**
 * __of_add_property - Add a property to a node without lock operations
 * @np:		Caller's Device Node
 * @prop:	Property to add
 */
int __of_add_property(struct device_node *np, struct property *prop)
{
	int rc = 0;
	unsigned long flags;
	struct property **next;

	raw_spin_lock_irqsave(&devtree_lock, flags);

	__of_remove_property_from_list(&np->deadprops, prop);

	prop->next = NULL;
	next = &np->properties;
	while (*next) {
		if (strcmp(prop->name, (*next)->name) == 0) {
			/* duplicate ! don't insert it */
			rc = -EEXIST;
			goto out_unlock;
		}
		next = &(*next)->next;
	}
	*next = prop;

out_unlock:
	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	if (rc)
		return rc;

	__of_add_property_sysfs(np, prop);
	return 0;
}

/**
 * of_add_property - Add a property to a node
 * @np:		Caller's Device Node
 * @prop:	Property to add
 */
int of_add_property(struct device_node *np, struct property *prop)
{
	int rc;

	mutex_lock(&of_mutex);
	rc = __of_add_property(np, prop);
	mutex_unlock(&of_mutex);

	if (!rc)
		of_property_notify(OF_RECONFIG_ADD_PROPERTY, np, prop, NULL);

	return rc;
}
EXPORT_SYMBOL_GPL(of_add_property);

int __of_remove_property(struct device_node *np, struct property *prop)
{
	unsigned long flags;
	int rc = -ENODEV;

	raw_spin_lock_irqsave(&devtree_lock, flags);

	if (__of_remove_property_from_list(&np->properties, prop)) {
		/* Found the property, add it to deadprops list */
		prop->next = np->deadprops;
		np->deadprops = prop;
		rc = 0;
	}

	raw_spin_unlock_irqrestore(&devtree_lock, flags);
	if (rc)
		return rc;

	__of_remove_property_sysfs(np, prop);
	return 0;
}

/**
 * of_remove_property - Remove a property from a node.
 * @np:		Caller's Device Node
 * @prop:	Property to remove
 *
 * Note that we don't actually remove it, since we have given out
 * who-knows-how-many pointers to the data using get-property.
 * Instead we just move the property to the "dead properties"
 * list, so it won't be found any more.
 */
int of_remove_property(struct device_node *np, struct property *prop)
{
	int rc;

	if (!prop)
		return -ENODEV;

	mutex_lock(&of_mutex);
	rc = __of_remove_property(np, prop);
	mutex_unlock(&of_mutex);

	if (!rc)
		of_property_notify(OF_RECONFIG_REMOVE_PROPERTY, np, prop, NULL);

	return rc;
}
EXPORT_SYMBOL_GPL(of_remove_property);

int __of_update_property(struct device_node *np, struct property *newprop,
		struct property **oldpropp)
{
	struct property **next, *oldprop;
	unsigned long flags;

	raw_spin_lock_irqsave(&devtree_lock, flags);

	__of_remove_property_from_list(&np->deadprops, newprop);

	for (next = &np->properties; *next; next = &(*next)->next) {
		if (of_prop_cmp((*next)->name, newprop->name) == 0)
			break;
	}
	*oldpropp = oldprop = *next;

	if (oldprop) {
		/* replace the node */
		newprop->next = oldprop->next;
		*next = newprop;
		oldprop->next = np->deadprops;
		np->deadprops = oldprop;
	} else {
		/* new node */
		newprop->next = NULL;
		*next = newprop;
	}

	raw_spin_unlock_irqrestore(&devtree_lock, flags);

	__of_update_property_sysfs(np, newprop, oldprop);

	return 0;
}

/*
 * of_update_property - Update a property in a node, if the property does
 * not exist, add it.
 *
 * Note that we don't actually remove it, since we have given out
 * who-knows-how-many pointers to the data using get-property.
 * Instead we just move the property to the "dead properties" list,
 * and add the new property to the property list
 */
int of_update_property(struct device_node *np, struct property *newprop)
{
	struct property *oldprop;
	int rc;

	if (!newprop->name)
		return -EINVAL;

	mutex_lock(&of_mutex);
	rc = __of_update_property(np, newprop, &oldprop);
	mutex_unlock(&of_mutex);

	if (!rc)
		of_property_notify(OF_RECONFIG_UPDATE_PROPERTY, np, newprop, oldprop);

	return rc;
}

static void of_alias_add(struct alias_prop *ap, struct device_node *np,
			 int id, const char *stem, int stem_len)
{
	ap->np = np;
	ap->id = id;
	strscpy(ap->stem, stem, stem_len + 1);
	list_add_tail(&ap->link, &aliases_lookup);
	pr_debug("adding DT alias:%s: stem=%s id=%i node=%pOF\n",
		 ap->alias, ap->stem, ap->id, np);
}

/**
 * of_alias_scan - Scan all properties of the 'aliases' node
 * @dt_alloc:	An allocator that provides a virtual address to memory
 *		for storing the resulting tree
 *
 * The function scans all the properties of the 'aliases' node and populates
 * the global lookup table with the properties.  It returns the
 * number of alias properties found, or an error code in case of failure.
 */
void of_alias_scan(void * (*dt_alloc)(u64 size, u64 align))
{
	const struct property *pp;

	of_aliases = of_find_node_by_path("/aliases");
	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen == NULL)
		of_chosen = of_find_node_by_path("/chosen@0");

	if (of_chosen) {
		/* linux,stdout-path and /aliases/stdout are for legacy compatibility */
		const char *name = NULL;

		if (of_property_read_string(of_chosen, "stdout-path", &name))
			of_property_read_string(of_chosen, "linux,stdout-path",
						&name);
		if (IS_ENABLED(CONFIG_PPC) && !name)
			of_property_read_string(of_aliases, "stdout", &name);
		if (name)
			of_stdout = of_find_node_opts_by_path(name, &of_stdout_options);
		if (of_stdout)
			of_stdout->fwnode.flags |= FWNODE_FLAG_BEST_EFFORT;
	}

	if (!of_aliases)
		return;

	for_each_property_of_node(of_aliases, pp) {
		const char *start = pp->name;
		const char *end = start + strlen(start);
		struct device_node *np;
		struct alias_prop *ap;
		int id, len;

		/* Skip those we do not want to proceed */
		if (!strcmp(pp->name, "name") ||
		    !strcmp(pp->name, "phandle") ||
		    !strcmp(pp->name, "linux,phandle"))
			continue;

		np = of_find_node_by_path(pp->value);
		if (!np)
			continue;

		/* walk the alias backwards to extract the id and work out
		 * the 'stem' string */
		while (isdigit(*(end-1)) && end > start)
			end--;
		len = end - start;

		if (kstrtoint(end, 10, &id) < 0)
			continue;

		/* Allocate an alias_prop with enough space for the stem */
		ap = dt_alloc(sizeof(*ap) + len + 1, __alignof__(*ap));
		if (!ap)
			continue;
		memset(ap, 0, sizeof(*ap) + len + 1);
		ap->alias = start;
		of_alias_add(ap, np, id, start, len);
	}
}

/**
 * of_alias_get_id - Get alias id for the given device_node
 * @np:		Pointer to the given device_node
 * @stem:	Alias stem of the given device_node
 *
 * The function travels the lookup table to get the alias id for the given
 * device_node and alias stem.
 *
 * Return: The alias id if found.
 */
int of_alias_get_id(const struct device_node *np, const char *stem)
{
	struct alias_prop *app;
	int id = -ENODEV;

	mutex_lock(&of_mutex);
	list_for_each_entry(app, &aliases_lookup, link) {
		if (strcmp(app->stem, stem) != 0)
			continue;

		if (np == app->np) {
			id = app->id;
			break;
		}
	}
	mutex_unlock(&of_mutex);

	return id;
}
EXPORT_SYMBOL_GPL(of_alias_get_id);

/**
 * of_alias_get_highest_id - Get highest alias id for the given stem
 * @stem:	Alias stem to be examined
 *
 * The function travels the lookup table to get the highest alias id for the
 * given alias stem.  It returns the alias id if found.
 */
int of_alias_get_highest_id(const char *stem)
{
	struct alias_prop *app;
	int id = -ENODEV;

	mutex_lock(&of_mutex);
	list_for_each_entry(app, &aliases_lookup, link) {
		if (strcmp(app->stem, stem) != 0)
			continue;

		if (app->id > id)
			id = app->id;
	}
	mutex_unlock(&of_mutex);

	return id;
}
EXPORT_SYMBOL_GPL(of_alias_get_highest_id);

/**
 * of_console_check() - Test and setup console for DT setup
 * @dn: Pointer to device node
 * @name: Name to use for preferred console without index. ex. "ttyS"
 * @index: Index to use for preferred console.
 *
 * Check if the given device node matches the stdout-path property in the
 * /chosen node. If it does then register it as the preferred console.
 *
 * Return: TRUE if console successfully setup. Otherwise return FALSE.
 */
bool of_console_check(const struct device_node *dn, char *name, int index)
{
	if (!dn || dn != of_stdout || console_set_on_cmdline)
		return false;

	/*
	 * XXX: cast `options' to char pointer to suppress complication
	 * warnings: printk, UART and console drivers expect char pointer.
	 */
	return !add_preferred_console(name, index, (char *)of_stdout_options);
}
EXPORT_SYMBOL_GPL(of_console_check);

/**
 * of_find_next_cache_node - Find a node's subsidiary cache
 * @np:	node of type "cpu" or "cache"
 *
 * Return: A node pointer with refcount incremented, use
 * of_node_put() on it when done.  Caller should hold a reference
 * to np.
 */
struct device_node *of_find_next_cache_node(const struct device_node *np)
{
	struct device_node *child, *cache_node;

	cache_node = of_parse_phandle(np, "l2-cache", 0);
	if (!cache_node)
		cache_node = of_parse_phandle(np, "next-level-cache", 0);

	if (cache_node)
		return cache_node;

	/* OF on pmac has nodes instead of properties named "l2-cache"
	 * beneath CPU nodes.
	 */
	if (IS_ENABLED(CONFIG_PPC_PMAC) && of_node_is_type(np, "cpu"))
		for_each_child_of_node(np, child)
			if (of_node_is_type(child, "cache"))
				return child;

	return NULL;
}

/**
 * of_find_last_cache_level - Find the level at which the last cache is
 * 		present for the given logical cpu
 *
 * @cpu: cpu number(logical index) for which the last cache level is needed
 *
 * Return: The level at which the last cache is present. It is exactly
 * same as  the total number of cache levels for the given logical cpu.
 */
int of_find_last_cache_level(unsigned int cpu)
{
	u32 cache_level = 0;
	struct device_node *prev = NULL, *np = of_cpu_device_node_get(cpu);

	while (np) {
		of_node_put(prev);
		prev = np;
		np = of_find_next_cache_node(np);
	}

	of_property_read_u32(prev, "cache-level", &cache_level);
	of_node_put(prev);

	return cache_level;
}

/**
 * of_map_id - Translate an ID through a downstream mapping.
 * @np: root complex device node.
 * @id: device ID to map.
 * @map_name: property name of the map to use.
 * @map_mask_name: optional property name of the mask to use.
 * @target: optional pointer to a target device node.
 * @id_out: optional pointer to receive the translated ID.
 *
 * Given a device ID, look up the appropriate implementation-defined
 * platform ID and/or the target device which receives transactions on that
 * ID, as per the "iommu-map" and "msi-map" bindings. Either of @target or
 * @id_out may be NULL if only the other is required. If @target points to
 * a non-NULL device node pointer, only entries targeting that node will be
 * matched; if it points to a NULL value, it will receive the device node of
 * the first matching target phandle, with a reference held.
 *
 * Return: 0 on success or a standard error code on failure.
 */
int of_map_id(const struct device_node *np, u32 id,
	       const char *map_name, const char *map_mask_name,
	       struct device_node **target, u32 *id_out)
{
	u32 map_mask, masked_id;
	int map_len;
	const __be32 *map = NULL;

	if (!np || !map_name || (!target && !id_out))
		return -EINVAL;

	map = of_get_property(np, map_name, &map_len);
	if (!map) {
		if (target)
			return -ENODEV;
		/* Otherwise, no map implies no translation */
		*id_out = id;
		return 0;
	}

	if (!map_len || map_len % (4 * sizeof(*map))) {
		pr_err("%pOF: Error: Bad %s length: %d\n", np,
			map_name, map_len);
		return -EINVAL;
	}

	/* The default is to select all bits. */
	map_mask = 0xffffffff;

	/*
	 * Can be overridden by "{iommu,msi}-map-mask" property.
	 * If of_property_read_u32() fails, the default is used.
	 */
	if (map_mask_name)
		of_property_read_u32(np, map_mask_name, &map_mask);

	masked_id = map_mask & id;
	for ( ; map_len > 0; map_len -= 4 * sizeof(*map), map += 4) {
		struct device_node *phandle_node;
		u32 id_base = be32_to_cpup(map + 0);
		u32 phandle = be32_to_cpup(map + 1);
		u32 out_base = be32_to_cpup(map + 2);
		u32 id_len = be32_to_cpup(map + 3);

		if (id_base & ~map_mask) {
			pr_err("%pOF: Invalid %s translation - %s-mask (0x%x) ignores id-base (0x%x)\n",
				np, map_name, map_name,
				map_mask, id_base);
			return -EFAULT;
		}

		if (masked_id < id_base || masked_id >= id_base + id_len)
			continue;

		phandle_node = of_find_node_by_phandle(phandle);
		if (!phandle_node)
			return -ENODEV;

		if (target) {
			if (*target)
				of_node_put(phandle_node);
			else
				*target = phandle_node;

			if (*target != phandle_node)
				continue;
		}

		if (id_out)
			*id_out = masked_id - id_base + out_base;

		pr_debug("%pOF: %s, using mask %08x, id-base: %08x, out-base: %08x, length: %08x, id: %08x -> %08x\n",
			np, map_name, map_mask, id_base, out_base,
			id_len, id, masked_id - id_base + out_base);
		return 0;
	}

	pr_info("%pOF: no %s translation for id 0x%x on %pOF\n", np, map_name,
		id, target && *target ? *target : NULL);

	/* Bypasses translation */
	if (id_out)
		*id_out = id;
	return 0;
}
EXPORT_SYMBOL_GPL(of_map_id);
