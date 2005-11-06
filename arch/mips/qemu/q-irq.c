#include <linux/init.h>
#include <linux/linkage.h>

#include <asm/i8259.h>
#include <asm/mipsregs.h>
#include <asm/qemu.h>
#include <asm/system.h>
#include <asm/time.h>

extern asmlinkage void qemu_handle_int(void);

asmlinkage void do_qemu_int(struct pt_regs *regs)
{
	unsigned int pending = read_c0_status() & read_c0_cause();

	if (pending & 0x8000) {
		ll_timer_interrupt(Q_COUNT_COMPARE_IRQ, regs);
		return;
	}
	if (pending & 0x0400) {
		int irq = i8259_irq();

		if (likely(irq >= 0))
			do_IRQ(irq, regs);

		return;
	}
}

void __init arch_init_irq(void)
{
	set_except_vector(0, qemu_handle_int);
	mips_hpt_frequency = QEMU_C0_COUNTER_CLOCK;		/* 100MHz */

	init_i8259_irqs();
	set_c0_status(0x8400);
}
