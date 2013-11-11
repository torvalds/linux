/*
 * Interrupt handling H8/300H depend.
 * Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 */

#include <linux/init.h>
#include <linux/errno.h>

#include <asm/ptrace.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/gpio-internal.h>
#include <asm/regs306x.h>

const int __initconst h8300_saved_vectors[] = {
#if defined(CONFIG_GDB_DEBUG)
	TRAP3_VEC,	/* TRAPA #3 is GDB breakpoint */
#endif
	-1,
};

const h8300_vector __initconst h8300_trap_table[] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	system_call,
	0,
	0,
	trace_break,
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
