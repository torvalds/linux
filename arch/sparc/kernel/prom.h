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

#endif /* __PROM_H */
