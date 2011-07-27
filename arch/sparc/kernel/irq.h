#include <linux/platform_device.h>

#include <asm/btfixup.h>

struct irq_bucket {
        struct irq_bucket *next;
        unsigned int real_irq;
        unsigned int irq;
        unsigned int pil;
};

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

/*
 * Platform specific irq configuration
 * The individual platforms assign their platform
 * specifics in their init functions.
 */
struct sparc_irq_config {
	void (*init_timers)(irq_handler_t);
	unsigned int (*build_device_irq)(struct platform_device *op,
	                                 unsigned int real_irq);
};
extern struct sparc_irq_config sparc_irq_config;

unsigned int irq_alloc(unsigned int real_irq, unsigned int pil);
void irq_link(unsigned int irq);
void irq_unlink(unsigned int irq);
void handler_irq(unsigned int pil, struct pt_regs *regs);

/* Dave Redman (djhr@tadpole.co.uk)
 * changed these to function pointers.. it saves cycles and will allow
 * the irq dependencies to be split into different files at a later date
 * sun4c_irq.c, sun4m_irq.c etc so we could reduce the kernel size.
 * Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Changed these to btfixup entities... It saves cycles :)
 */

BTFIXUPDEF_CALL(void, clear_clock_irq, void)
BTFIXUPDEF_CALL(void, load_profile_irq, int, unsigned int)

static inline void clear_clock_irq(void)
{
	BTFIXUP_CALL(clear_clock_irq)();
}

static inline void load_profile_irq(int cpu, int limit)
{
	BTFIXUP_CALL(load_profile_irq)(cpu, limit);
}

#ifdef CONFIG_SMP
BTFIXUPDEF_CALL(void, set_cpu_int, int, int)
BTFIXUPDEF_CALL(void, clear_cpu_int, int, int)
BTFIXUPDEF_CALL(void, set_irq_udt, int)

#define set_cpu_int(cpu,level) BTFIXUP_CALL(set_cpu_int)(cpu,level)
#define clear_cpu_int(cpu,level) BTFIXUP_CALL(clear_cpu_int)(cpu,level)
#define set_irq_udt(cpu) BTFIXUP_CALL(set_irq_udt)(cpu)

/* All SUN4D IPIs are sent on this IRQ, may be shared with hard IRQs */
#define SUN4D_IPI_IRQ 14

extern void sun4d_ipi_interrupt(void);

#endif
