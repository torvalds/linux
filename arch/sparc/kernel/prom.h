#ifndef __PROM_H
#define __PROM_H

#include <linux/spinlock.h>
#include <asm/prom.h>

extern struct device_node *allnodes;	/* temporary while merging */
extern rwlock_t devtree_lock;	/* temporary while merging */

#endif /* __PROM_H */
