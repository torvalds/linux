#ifndef _PPC_KERNEL_MPC8xx_H
#define _PPC_KERNEL_MPC8xx_H

#include <linux/irq.h>
#include <linux/interrupt.h>

extern struct hw_interrupt_type mpc8xx_pic;

int mpc8xx_pic_init(void);
unsigned int mpc8xx_get_irq(void);

#endif /* _PPC_KERNEL_PPC8xx_H */
