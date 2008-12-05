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

extern struct property * __init build_prop_list(phandle node);

#endif /* __PROM_H */
