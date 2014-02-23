#ifndef _ASM_X86_MSHYPER_H
#define _ASM_X86_MSHYPER_H

#include <linux/types.h>
#include <linux/interrupt.h>
#include <asm/hyperv.h>

struct ms_hyperv_info {
	u32 features;
	u32 hints;
};

extern struct ms_hyperv_info ms_hyperv;

void hyperv_callback_vector(void);
#ifdef CONFIG_TRACING
#define trace_hyperv_callback_vector hyperv_callback_vector
#endif
void hyperv_vector_handler(struct pt_regs *regs);
int hv_setup_vmbus_irq(int irq, irq_handler_t handler, void *dev_id);
void hv_remove_vmbus_irq(int irq, void *dev_id);

#endif
