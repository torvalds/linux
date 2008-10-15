#ifndef _M68KNOMMU_IRQ_H_
#define _M68KNOMMU_IRQ_H_

#ifdef CONFIG_COLDFIRE
/*
 * On the ColdFire we keep track of all vectors. That way drivers
 * can register whatever vector number they wish, and we can deal
 * with it.
 */
#define	SYS_IRQS	256
#define	NR_IRQS		SYS_IRQS

#else

/*
 * # of m68k interrupts
 */
#define SYS_IRQS	8
#define NR_IRQS		(24 + SYS_IRQS)

#endif /* CONFIG_COLDFIRE */


#define irq_canonicalize(irq)	(irq)

#endif /* _M68KNOMMU_IRQ_H_ */
