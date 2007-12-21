#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/linkage.h>

#include <asm/i8259.h>
#include <asm/irq_cpu.h>
#include <asm/mipsregs.h>
#include <asm/qemu.h>
#include <asm/system.h>
#include <asm/time.h>

asmlinkage void plat_irq_dispatch(void)
{
	unsigned int pending = read_c0_status() & read_c0_cause();

	if (pending & 0x8000) {
		do_IRQ(Q_COUNT_COMPARE_IRQ);
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

	mips_cpu_irq_init();
	init_i8259_irqs();
	set_c0_status(0x400);
}
