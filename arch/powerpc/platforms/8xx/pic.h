#ifndef _PPC_KERNEL_MPC8xx_H
#define _PPC_KERNEL_MPC8xx_H

#include <linux/irq.h>
#include <linux/interrupt.h>

void mpc8xx_pic_init(void);
unsigned int mpc8xx_get_irq(void);

/*
 * Some internal interrupt registers use an 8-bit mask for the interrupt
 * level instead of a number.
 */
static inline uint mk_int_int_mask(uint mask)
{
	return (1 << (7 - (mask/2)));
}

#endif /* _PPC_KERNEL_PPC8xx_H */
