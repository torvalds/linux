#ifndef __PPC_PLATFORMS_PMAC_PIC_H
#define __PPC_PLATFORMS_PMAC_PIC_H

#include <linux/irq.h>

extern struct hw_interrupt_type pmac_pic;

void pmac_pic_init(void);
int pmac_get_irq(struct pt_regs *regs);

#endif /* __PPC_PLATFORMS_PMAC_PIC_H */
