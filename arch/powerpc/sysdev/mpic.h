/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _POWERPC_SYSDEV_MPIC_H
#define _POWERPC_SYSDEV_MPIC_H

/*
 * Copyright 2006-2007, Michael Ellerman, IBM Corporation.
 */

#ifdef CONFIG_PCI_MSI
extern void mpic_msi_reserve_hwirq(struct mpic *mpic, irq_hw_number_t hwirq);
extern int mpic_msi_init_allocator(struct mpic *mpic);
extern int mpic_u3msi_init(struct mpic *mpic);
#else
static inline void mpic_msi_reserve_hwirq(struct mpic *mpic,
					  irq_hw_number_t hwirq)
{
	return;
}

static inline int mpic_u3msi_init(struct mpic *mpic)
{
	return -1;
}
#endif

#if defined(CONFIG_PCI_MSI) && defined(CONFIG_PPC_PASEMI)
int mpic_pasemi_msi_init(struct mpic *mpic);
#else
static inline int mpic_pasemi_msi_init(struct mpic *mpic) { return -1; }
#endif

extern int mpic_set_irq_type(struct irq_data *d, unsigned int flow_type);
extern void mpic_set_vector(unsigned int virq, unsigned int vector);
extern int mpic_set_affinity(struct irq_data *d,
			     const struct cpumask *cpumask, bool force);
extern void mpic_reset_core(int cpu);

#ifdef CONFIG_FSL_SOC
extern int mpic_map_error_int(struct mpic *mpic, unsigned int virq, irq_hw_number_t  hw);
extern void mpic_err_int_init(struct mpic *mpic, irq_hw_number_t irqnum);
extern int mpic_setup_error_int(struct mpic *mpic, int intvec);
#else
static inline int mpic_map_error_int(struct mpic *mpic, unsigned int virq, irq_hw_number_t  hw)
{
	return 0;
}


static inline void mpic_err_int_init(struct mpic *mpic, irq_hw_number_t irqnum)
{
	return;
}

static inline int mpic_setup_error_int(struct mpic *mpic, int intvec)
{
	return -1;
}
#endif

#endif /* _POWERPC_SYSDEV_MPIC_H */
