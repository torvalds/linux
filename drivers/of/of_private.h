/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef _LINUX_OF_PRIVATE_H
#define _LINUX_OF_PRIVATE_H
/*
 * Private symbols used by OF support code
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 */

/**
 * struct alias_prop - Alias property in 'aliases' node
 * @link:	List node to link the structure in aliases_lookup list
 * @alias:	Alias property name
 * @np:		Pointer to device_node that the alias stands for
 * @id:		Index value from end of alias name
 * @stem:	Alias string without the index
 *
 * The structure represents one alias property of 'aliases' node as
 * an entry in aliases_lookup list.
 */
struct alias_prop {
	struct list_head link;
	const char *alias;
	struct device_node *np;
	int id;
	char stem[0];
};

#if defined(CONFIG_SPARC)
#define OF_ROOT_NODE_ADDR_CELLS_DEFAULT 2
#else
#define OF_ROOT_NODE_ADDR_CELLS_DEFAULT 1
#endif

#define OF_ROOT_NODE_SIZE_CELLS_DEFAULT 1

extern struct mutex of_mutex;
extern struct list_head aliases_lookup;
extern struct kset *of_kset;

#if defined(CONFIG_OF_DYNAMIC)
extern int of_property_notify(int action, struct device_node *np,
			      struct property *prop, struct property *old_prop);
extern void of_node_release(struct kobject *kobj);
extern int __of_changeset_apply_entries(struct of_changeset *ocs,
					int *ret_revert);
extern int __of_changeset_apply_notify(struct of_changeset *ocs);
extern int __of_changeset_revert_entries(struct of_changeset *ocs,
					 int *ret_apply);
extern int __of_changeset_revert_notify(struct of_changeset *ocs);
#else /* CONFIG_OF_DYNAMIC */
static inline int of_property_notify(int action, struct device_node *np,
				     struct property *prop, struct property *old_prop)
{
	return 0;
}
#endif /* CONFIG_OF_DYNAMIC */

#if defined(CONFIG_OF_KOBJ)
int of_node_is_attached(struct device_node *node);
int __of_add_property_sysfs(struct device_node *np, struct property *pp);
void __of_remove_property_sysfs(struct device_node *np, struct property *prop);
void __of_update_property_sysfs(struct device_node *np, struct property *newprop,
		struct property *oldprop);
int __of_attach_node_sysfs(struct device_node *np);
void __of_detach_node_sysfs(struct device_node *np);
#else
static inline int __of_add_property_sysfs(struct device_node *np, struct property *pp)
{
	return 0;
}
static inline void __of_remove_property_sysfs(struct device_node *np, struct property *prop) {}
static inline void __of_update_property_sysfs(struct device_node *np,
		struct property *newprop, struct property *oldprop) {}
static inline int __of_attach_node_sysfs(struct device_node *np)
{
	return 0;
}
static inline void __of_detach_node_sysfs(struct device_node *np) {}
#endif

#if defined(CONFIG_OF_RESOLVE)
int of_resolve_phandles(struct device_node *tree);
#endif

#if defined(CONFIG_OF_DYNAMIC)
void __of_free_phandle_cache_entry(phandle handle);
#endif

#if defined(CONFIG_OF_OVERLAY)
void of_overlay_mutex_lock(void);
void of_overlay_mutex_unlock(void);
int of_free_phandle_cache(void);
void of_populate_phandle_cache(void);
#else
static inline void of_overlay_mutex_lock(void) {};
static inline void of_overlay_mutex_unlock(void) {};
#endif

#if defined(CONFIG_OF_UNITTEST) && defined(CONFIG_OF_OVERLAY)
extern void __init unittest_unflatten_overlay_base(void);
#else
static inline void unittest_unflatten_overlay_base(void) {};
#endif

extern void *__unflatten_device_tree(const void *blob,
			      struct device_node *dad,
			      struct device_node **mynodes,
			      void *(*dt_alloc)(u64 size, u64 align),
			      bool detached);

/**
 * General utilities for working with live trees.
 *
 * All functions with two leading underscores operate
 * without taking node references, so you either have to
 * own the devtree lock or work on detached trees only.
 */
struct property *__of_prop_dup(const struct property *prop, gfp_t allocflags);
struct device_node *__of_node_dup(const struct device_node *np,
				  const char *full_name);

struct device_node *__of_find_node_by_path(struct device_node *parent,
						const char *path);
struct device_node *__of_find_node_by_full_path(struct device_node *node,
						const char *path);

extern const void *__of_get_property(const struct device_node *np,
				     const char *name, int *lenp);
extern int __of_add_property(struct device_node *np, struct property *prop);
extern int __of_add_property_sysfs(struct device_node *np,
		struct property *prop);
extern int __of_remove_property(struct device_node *np, struct property *prop);
extern void __of_remove_property_sysfs(struct device_node *np,
		struct property *prop);
extern int __of_update_property(struct device_node *np,
		struct property *newprop, struct property **oldprop);
extern void __of_update_property_sysfs(struct device_node *np,
		struct property *newprop, struct property *oldprop);

extern int __of_attach_node_sysfs(struct device_node *np);
extern void __of_detach_node(struct device_node *np);
extern void __of_detach_node_sysfs(struct device_node *np);

extern void __of_sysfs_remove_bin_file(struct device_node *np,
				       struct property *prop);

/* illegal phandle value (set when unresolved) */
#define OF_PHANDLE_ILLEGAL	0xdeadbeef

/* iterators for transactions, used for overlays */
/* forward iterator */
#define for_each_transaction_entry(_oft, _te) \
	list_for_each_entry(_te, &(_oft)->te_list, node)

/* reverse iterator */
#define for_each_transaction_entry_reverse(_oft, _te) \
	list_for_each_entry_reverse(_te, &(_oft)->te_list, node)

#ifdef CONFIG_OF_ADDRESS
extern int of_dma_get_range(struct device_node *np, u64 *dma_addr,
			    u64 *paddr, u64 *size);
#else
static inline int of_dma_get_range(struct device_node *np, u64 *dma_addr,
				   u64 *paddr, u64 *size)
{
	return -ENODEV;
}
#endif

#endif /* _LINUX_OF_PRIVATE_H */
