#include <linux/init.h>
#include <linux/linkage.h>

#include <asm/i8259.h>
#include <asm/mipsregs.h>
#include <asm/qemu.h>
#include <asm/system.h>
#include <asm/time.h>

extern asmlinkage void qemu_handle_int(void);

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause();

	if (pending & 0x8000) {
		ll_timer_interrupt(Q_COUNT_COMPARE_IRQ);
		return;
	}
	if (pending & 0x0400) {
		int irq = i8259_irq();

		if (likely(irq >= 0))
			do_IRQ(irq);

		return;
	}
}

void __init arch_init_irq(void)
{
	mips_hpt_frequency = QEMU_C0_COUNTER_CLOCK;		/* 100MHz */

	init_i8259_irqs();
	set_c0_status(0x8400);
}
