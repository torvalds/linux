/*
 * linux/arch/h8300/platform/h8300h/ints_h8300h.c
 * Interrupt handling CPU variants
 *
 * Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/errno.h>

#include <asm/ptrace.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/gpio.h>
#include <asm/regs306x.h>

/* saved vector list */
const int __initdata h8300_saved_vectors[]={
#if defined(CONFIG_GDB_DEBUG)
	TRAP3_VEC,
#endif
	-1
};

/* trap entry table */
const unsigned long __initdata h8300_trap_table[NR_TRAPS]={
	0,0,0,0,0,0,0,0,
	(unsigned long)system_call,  /* TRAPA #0 */
	0,0,
	(unsigned long)trace_break,  /* TRAPA #3 */
};

int h8300_enable_irq_pin(unsigned int irq)
{
	int bitmask;
	if (irq < EXT_IRQ0 || irq > EXT_IRQ5)
		return 0;

	/* initialize IRQ pin */
	bitmask = 1 << (irq - EXT_IRQ0);
	switch(irq) {
	case EXT_IRQ0:
	case EXT_IRQ1:
	case EXT_IRQ2:
	case EXT_IRQ3:
		if (H8300_GPIO_RESERVE(H8300_GPIO_P8, bitmask) == 0)
			return -EBUSY;
		H8300_GPIO_DDR(H8300_GPIO_P8, bitmask, H8300_GPIO_INPUT);
		break;
	case EXT_IRQ4:
	case EXT_IRQ5:
		if (H8300_GPIO_RESERVE(H8300_GPIO_P9, bitmask) == 0)
			return -EBUSY;
		H8300_GPIO_DDR(H8300_GPIO_P9, bitmask, H8300_GPIO_INPUT);
		break;
	}

	return 0;
}

void h8300_disable_irq_pin(unsigned int irq)
{
	int bitmask;
	if (irq < EXT_IRQ0 || irq > EXT_IRQ5)
		return;

	/* disable interrupt & release IRQ pin */
	bitmask = 1 << (irq - EXT_IRQ0);
	switch(irq) {
	case EXT_IRQ0:
	case EXT_IRQ1:
	case EXT_IRQ2:
	case EXT_IRQ3:
		*(volatile unsigned char *)IER &= ~bitmask;
		H8300_GPIO_FREE(H8300_GPIO_P8, bitmask);
		break ;
	case EXT_IRQ4:
	case EXT_IRQ5:
		*(volatile unsigned char *)IER &= ~bitmask;
		H8300_GPIO_FREE(H8300_GPIO_P9, bitmask);
		break;
	}
}
