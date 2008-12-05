#ifndef __PROM_H
#define __PROM_H

#include <linux/spinlock.h>
#include <asm/prom.h>

extern struct device_node *allnodes;	/* temporary while merging */
extern rwlock_t devtree_lock;	/* temporary while merging */

extern void * prom_early_alloc(unsigned long size);

#ifdef CONFIG_SPARC64
extern void irq_trans_init(struct device_node *dp);
#endif

extern unsigned int prom_unique_id;

static inline int is_root_node(const struct device_node *dp)
{
	if (!dp)
		return 0;

	return (dp->parent == NULL);
}

extern char *build_path_component(struct device_node *dp);

extern struct device_node * __init prom_create_node(phandle node,
						    struct device_node *parent);

extern struct device_node * __init prom_build_tree(struct device_node *parent,
						   phandle node,
						   struct device_node ***nextp);
#endif /* __PROM_H */
