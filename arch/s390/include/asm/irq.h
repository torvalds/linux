#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#define EXT_INTERRUPT	1
#define IO_INTERRUPT	2
#define THIN_INTERRUPT	3

#define NR_IRQS_BASE	4

#ifdef CONFIG_PCI_NR_MSI
# define NR_IRQS	(NR_IRQS_BASE + CONFIG_PCI_NR_MSI)
#else
# define NR_IRQS	NR_IRQS_BASE
#endif

/* This number is used when no interrupt has been assigned */
#define NO_IRQ		0

/* External interruption codes */
#define EXT_IRQ_INTERRUPT_KEY	0x0040
#define EXT_IRQ_CLK_COMP	0x1004
#define EXT_IRQ_CPU_TIMER	0x1005
#define EXT_IRQ_WARNING_TRACK	0x1007
#define EXT_IRQ_MALFUNC_ALERT	0x1200
#define EXT_IRQ_EMERGENCY_SIG	0x1201
#define EXT_IRQ_EXTERNAL_CALL	0x1202
#define EXT_IRQ_TIMING_ALERT	0x1406
#define EXT_IRQ_MEASURE_ALERT	0x1407
#define EXT_IRQ_SERVICE_SIG	0x2401
#define EXT_IRQ_CP_SERVICE	0x2603
#define EXT_IRQ_IUCV		0x4000

#ifndef __ASSEMBLY__

#include <linux/hardirq.h>
#include <linux/percpu.h>
#include <linux/cache.h>
#include <linux/types.h>

enum interruption_class {
	IRQEXT_CLK,
	IRQEXT_EXC,
	IRQEXT_EMS,
	IRQEXT_TMR,
	IRQEXT_TLA,
	IRQEXT_PFL,
	IRQEXT_DSD,
	IRQEXT_VRT,
	IRQEXT_SCP,
	IRQEXT_IUC,
	IRQEXT_CMS,
	IRQEXT_CMC,
	IRQEXT_CMR,
	IRQEXT_FTP,
	IRQIO_CIO,
	IRQIO_QAI,
	IRQIO_DAS,
	IRQIO_C15,
	IRQIO_C70,
	IRQIO_TAP,
	IRQIO_VMR,
	IRQIO_LCS,
	IRQIO_CLW,
	IRQIO_CTC,
	IRQIO_APB,
	IRQIO_ADM,
	IRQIO_CSC,
	IRQIO_PCI,
	IRQIO_MSI,
	IRQIO_VIR,
	IRQIO_VAI,
	NMI_NMI,
	CPU_RST,
	NR_ARCH_IRQS
};

struct irq_stat {
	unsigned int irqs[NR_ARCH_IRQS];
};

DECLARE_PER_CPU_SHARED_ALIGNED(struct irq_stat, irq_stat);

static __always_inline void inc_irq_stat(enum interruption_class irq)
{
	__get_cpu_var(irq_stat).irqs[irq]++;
}

struct ext_code {
	unsigned short subcode;
	unsigned short code;
};

typedef void (*ext_int_handler_t)(struct ext_code, unsigned int, unsigned long);

int register_external_irq(u16 code, ext_int_handler_t handler);
int unregister_external_irq(u16 code, ext_int_handler_t handler);

enum irq_subclass {
	IRQ_SUBCLASS_MEASUREMENT_ALERT = 5,
	IRQ_SUBCLASS_SERVICE_SIGNAL = 9,
};

void irq_subclass_register(enum irq_subclass subclass);
void irq_subclass_unregister(enum irq_subclass subclass);

#define irq_canonicalize(irq)  (irq)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_IRQ_H */
