#ifndef __IA64_INTR_REMAPPING_H
#define __IA64_INTR_REMAPPING_H
#define irq_remapping_enabled 0
#define dmar_alloc_hwirq	create_irq
#define dmar_free_hwirq		destroy_irq
#endif
