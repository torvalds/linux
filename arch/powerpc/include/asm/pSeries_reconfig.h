#ifndef _PPC64_PSERIES_RECONFIG_H
#define _PPC64_PSERIES_RECONFIG_H
#ifdef __KERNEL__

#include <linux/notifier.h>

/*
 * Use this API if your code needs to know about OF device nodes being
 * added or removed on pSeries systems.
 */

#define PSERIES_RECONFIG_ADD		0x0001
#define PSERIES_RECONFIG_REMOVE		0x0002
#define PSERIES_DRCONF_MEM_ADD		0x0003
#define PSERIES_DRCONF_MEM_REMOVE	0x0004
#define PSERIES_UPDATE_PROPERTY		0x0005

/**
 * pSeries_reconfig_notify - Notifier value structure for OFDT property updates
 *
 * @node: Device tree node which owns the property being updated
 * @property: Updated property
 */
struct pSeries_reconfig_prop_update {
	struct device_node *node;
	struct property *property;
};

#ifdef CONFIG_PPC_PSERIES
extern int pSeries_reconfig_notifier_register(struct notifier_block *);
extern void pSeries_reconfig_notifier_unregister(struct notifier_block *);
extern int pSeries_reconfig_notify(unsigned long action, void *p);
/* Not the best place to put this, will be fixed when we move some
 * of the rtas suspend-me stuff to pseries */
extern void pSeries_coalesce_init(void);
#else /* !CONFIG_PPC_PSERIES */
static inline int pSeries_reconfig_notifier_register(struct notifier_block *nb)
{
	return 0;
}
static inline void pSeries_reconfig_notifier_unregister(struct notifier_block *nb) { }
static inline void pSeries_coalesce_init(void) { }
#endif /* CONFIG_PPC_PSERIES */


#endif /* __KERNEL__ */
#endif /* _PPC64_PSERIES_RECONFIG_H */
