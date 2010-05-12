#ifndef __PROM_H
#define __PROM_H

#include <linux/spinlock.h>
#include <asm/prom.h>

extern void * prom_early_alloc(unsigned long size);
extern void irq_trans_init(struct device_node *dp);

extern unsigned int prom_unique_id;

static inline int is_root_node(const struct device_node *dp)
{
	if (!dp)
		return 0;

	return (dp->parent == NULL);
}

extern char *build_path_component(struct device_node *dp);
extern void of_console_init(void);

extern unsigned int prom_early_allocated;

#endif /* __PROM_H */
