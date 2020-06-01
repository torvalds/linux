/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016,2017 IBM Corporation.
 */
#ifndef __XIVE_INTERNAL_H
#define __XIVE_INTERNAL_H

/*
 * A "disabled" interrupt should never fire, to catch problems
 * we set its logical number to this
 */
#define XIVE_BAD_IRQ		0x7fffffff
#define XIVE_MAX_IRQ		(XIVE_BAD_IRQ - 1)

/* Each CPU carry one of these with various per-CPU state */
struct xive_cpu {
#ifdef CONFIG_SMP
	/* HW irq number and data of IPI */
	u32 hw_ipi;
	struct xive_irq_data ipi_data;
#endif /* CONFIG_SMP */

	int chip_id;

	/* Queue datas. Only one is populated */
#define XIVE_MAX_QUEUES	8
	struct xive_q queue[XIVE_MAX_QUEUES];

	/*
	 * Pending mask. Each bit corresponds to a priority that
	 * potentially has pending interrupts.
	 */
	u8 pending_prio;

	/* Cache of HW CPPR */
	u8 cppr;
};

/* Backend ops */
struct xive_ops {
	int	(*populate_irq_data)(u32 hw_irq, struct xive_irq_data *data);
	int 	(*configure_irq)(u32 hw_irq, u32 target, u8 prio, u32 sw_irq);
	int	(*get_irq_config)(u32 hw_irq, u32 *target, u8 *prio,
				  u32 *sw_irq);
	int	(*setup_queue)(unsigned int cpu, struct xive_cpu *xc, u8 prio);
	void	(*cleanup_queue)(unsigned int cpu, struct xive_cpu *xc, u8 prio);
	void	(*setup_cpu)(unsigned int cpu, struct xive_cpu *xc);
	void	(*teardown_cpu)(unsigned int cpu, struct xive_cpu *xc);
	bool	(*match)(struct device_node *np);
	void	(*shutdown)(void);

	void	(*update_pending)(struct xive_cpu *xc);
	void	(*eoi)(u32 hw_irq);
	void	(*sync_source)(u32 hw_irq);
	u64	(*esb_rw)(u32 hw_irq, u32 offset, u64 data, bool write);
#ifdef CONFIG_SMP
	int	(*get_ipi)(unsigned int cpu, struct xive_cpu *xc);
	void	(*put_ipi)(unsigned int cpu, struct xive_cpu *xc);
#endif
	int	(*debug_show)(struct seq_file *m, void *private);
	const char *name;
};

bool xive_core_init(const struct xive_ops *ops, void __iomem *area, u32 offset,
		    u8 max_prio);
__be32 *xive_queue_page_alloc(unsigned int cpu, u32 queue_shift);
int xive_core_debug_init(void);

static inline u32 xive_alloc_order(u32 queue_shift)
{
	return (queue_shift > PAGE_SHIFT) ? (queue_shift - PAGE_SHIFT) : 0;
}

extern bool xive_cmdline_disabled;

#endif /*  __XIVE_INTERNAL_H */
