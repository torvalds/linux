#ifndef _PPC64_PSERIES_RECONFIG_H
#define _PPC64_PSERIES_RECONFIG_H
#ifdef __KERNEL__

#ifdef CONFIG_PPC_PSERIES
/* Not the best place to put this, will be fixed when we move some
 * of the rtas suspend-me stuff to pseries */
extern void pSeries_coalesce_init(void);
#else /* !CONFIG_PPC_PSERIES */
static inline void pSeries_coalesce_init(void) { }
#endif /* CONFIG_PPC_PSERIES */


#endif /* __KERNEL__ */
#endif /* _PPC64_PSERIES_RECONFIG_H */
