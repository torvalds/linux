#ifndef _XEN_SMP_H

extern void xen_send_IPI_mask(const struct cpumask *mask,
			      int vector);
extern void xen_send_IPI_mask_allbutself(const struct cpumask *mask,
				int vector);
extern void xen_send_IPI_allbutself(int vector);
extern void xen_send_IPI_all(int vector);
extern void xen_send_IPI_self(int vector);

#ifdef CONFIG_XEN_PVH
extern void xen_pvh_early_cpu_init(int cpu, bool entry);
#else
static inline void xen_pvh_early_cpu_init(int cpu, bool entry)
{
}
#endif

#endif
