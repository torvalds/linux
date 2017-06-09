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
int xen_smp_intr_init_pv(unsigned int cpu);
void xen_smp_intr_free_pv(unsigned int cpu);

void xen_smp_send_reschedule(int cpu);
void xen_smp_send_call_function_ipi(const struct cpumask *mask);
void xen_smp_send_call_function_single_ipi(int cpu);

struct xen_common_irq {
	int irq;
	char *name;
};
#else /* CONFIG_SMP */

static inline int xen_smp_intr_init(unsigned int cpu)
{
	return 0;
}
static inline void xen_smp_intr_free(unsigned int cpu) {}

static inline int xen_smp_intr_init_pv(unsigned int cpu)
{
	return 0;
}
static inline void xen_smp_intr_free_pv(unsigned int cpu) {}
#endif /* CONFIG_SMP */

#endif
