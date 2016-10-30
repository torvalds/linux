#ifndef _XEN_SMP_H

#ifdef CONFIG_SMP
extern void xen_send_IPI_mask(const struct cpumask *mask,
			      int vector);
extern void xen_send_IPI_mask_allbutself(const struct cpumask *mask,
				int vector);
extern void xen_send_IPI_allbutself(int vector);
extern void xen_send_IPI_all(int vector);
extern void xen_send_IPI_self(int vector);

extern int xen_smp_intr_init(unsigned int cpu);
extern void xen_smp_intr_free(unsigned int cpu);

#else /* CONFIG_SMP */

static inline int xen_smp_intr_init(unsigned int cpu)
{
	return 0;
}
static inline void xen_smp_intr_free(unsigned int cpu) {}
#endif /* CONFIG_SMP */

#ifdef CONFIG_XEN_PVH
extern void xen_pvh_early_cpu_init(int cpu, bool entry);
#else
static inline void xen_pvh_early_cpu_init(int cpu, bool entry)
{
}
#endif

#endif
