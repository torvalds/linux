/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/platform_device.h>

#include <asm/cpu_type.h>

struct irq_bucket {
        struct irq_bucket *next;
        unsigned int real_irq;
        unsigned int irq;
        unsigned int pil;
};

#define SUN4M_HARD_INT(x)       (0x000000001 << (x))
#define SUN4M_SOFT_INT(x)       (0x000010000 << (x))

#define SUN4D_MAX_BOARD 10
#define SUN4D_MAX_IRQ ((SUN4D_MAX_BOARD + 2) << 5)

/* Map between the irq identifier used in hw to the
 * irq_bucket. The map is sufficient large to hold
 * the sun4d hw identifiers.
 */
extern struct irq_bucket *irq_map[SUN4D_MAX_IRQ];


/* sun4m specific type definitions */

/* This maps direct to CPU specific interrupt registers */
struct sun4m_irq_percpu {
	u32	pending;
	u32	clear;
	u32	set;
};

/* This maps direct to global interrupt registers */
struct sun4m_irq_global {
	u32	pending;
	u32	mask;
	u32	mask_clear;
	u32	mask_set;
	u32	interrupt_target;
};

extern struct sun4m_irq_percpu __iomem *sun4m_irq_percpu[SUN4M_NCPUS];
extern struct sun4m_irq_global __iomem *sun4m_irq_global;

/* The following definitions describe the individual platform features: */
#define FEAT_L10_CLOCKSOURCE (1 << 0) /* L10 timer is used as a clocksource */
#define FEAT_L10_CLOCKEVENT  (1 << 1) /* L10 timer is used as a clockevent */
#define FEAT_L14_ONESHOT     (1 << 2) /* L14 timer clockevent can oneshot */

/*
 * Platform specific configuration
 * The individual platforms assign their platform
 * specifics in their init functions.
 */
struct sparc_config {
	void (*init_timers)(void);
	unsigned int (*build_device_irq)(struct platform_device *op,
	                                 unsigned int real_irq);

	/* generic clockevent features - see FEAT_* above */
	int features;

	/* clock rate used for clock event timer */
	int clock_rate;

	/* one period for clock source timer */
	unsigned int cs_period;

	/* function to obtain offsett for cs period */
	unsigned int (*get_cycles_offset)(void);

	void (*clear_clock_irq)(void);
	void (*load_profile_irq)(int cpu, unsigned int limit);
};
extern struct sparc_config sparc_config;

unsigned int irq_alloc(unsigned int real_irq, unsigned int pil);
void irq_link(unsigned int irq);
void irq_unlink(unsigned int irq);
void handler_irq(unsigned int pil, struct pt_regs *regs);

unsigned long leon_get_irqmask(unsigned int irq);

/* irq_32.c */
void sparc_floppy_irq(int irq, void *dev_id, struct pt_regs *regs);

/* sun4m_irq.c */
void sun4m_nmi(struct pt_regs *regs);

/* sun4d_irq.c */
void sun4d_handler_irq(unsigned int pil, struct pt_regs *regs);

#ifdef CONFIG_SMP

/* All SUN4D IPIs are sent on this IRQ, may be shared with hard IRQs */
#define SUN4D_IPI_IRQ 13

void sun4d_ipi_interrupt(void);

#endif
