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

#ifdef CONFIG_PPC_PSERIES
extern int pSeries_reconfig_notifier_register(struct notifier_block *);
extern void pSeries_reconfig_notifier_unregister(struct notifier_block *);
extern struct blocking_notifier_head pSeries_reconfig_chain;
#else /* !CONFIG_PPC_PSERIES */
static inline int pSeries_reconfig_notifier_register(struct notifier_block *nb)
{
	return 0;
}
static inline void pSeries_reconfig_notifier_unregister(struct notifier_block *nb) { }
#endif /* CONFIG_PPC_PSERIES */

#endif /* __KERNEL__ */
#endif /* _PPC64_PSERIES_RECONFIG_H */
