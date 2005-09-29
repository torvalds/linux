/*
 *  Unmaintained SGI Visual Workstation support.
 *  Split out from setup.c by davej@suse.de
 */

#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/fixmap.h>
#include <asm/arch_hooks.h>
#include <asm/io.h>
#include "cobalt.h"
#include "piix4.h"

int no_broadcast;

char visws_board_type = -1;
char visws_board_rev = -1;

void __init visws_get_board_type_and_rev(void)
{
	int raw;

	visws_board_type = (char)(inb_p(PIIX_GPI_BD_REG) & PIIX_GPI_BD_REG)
							 >> PIIX_GPI_BD_SHIFT;
	/*
	 * Get Board rev.
	 * First, we have to initialize the 307 part to allow us access
	 * to the GPIO registers.  Let's map them at 0x0fc0 which is right
	 * after the PIIX4 PM section.
	 */
	outb_p(SIO_DEV_SEL, SIO_INDEX);
	outb_p(SIO_GP_DEV, SIO_DATA);	/* Talk to GPIO regs. */

	outb_p(SIO_DEV_MSB, SIO_INDEX);
	outb_p(SIO_GP_MSB, SIO_DATA);	/* MSB of GPIO base address */

	outb_p(SIO_DEV_LSB, SIO_INDEX);
	outb_p(SIO_GP_LSB, SIO_DATA);	/* LSB of GPIO base address */

	outb_p(SIO_DEV_ENB, SIO_INDEX);
	outb_p(1, SIO_DATA);		/* Enable GPIO registers. */

	/*
	 * Now, we have to map the power management section to write
	 * a bit which enables access to the GPIO registers.
	 * What lunatic came up with this shit?
	 */
	outb_p(SIO_DEV_SEL, SIO_INDEX);
	outb_p(SIO_PM_DEV, SIO_DATA);	/* Talk to GPIO regs. */

	outb_p(SIO_DEV_MSB, SIO_INDEX);
	outb_p(SIO_PM_MSB, SIO_DATA);	/* MSB of PM base address */

	outb_p(SIO_DEV_LSB, SIO_INDEX);
	outb_p(SIO_PM_LSB, SIO_DATA);	/* LSB of PM base address */

	outb_p(SIO_DEV_ENB, SIO_INDEX);
	outb_p(1, SIO_DATA);		/* Enable PM registers. */

	/*
	 * Now, write the PM register which enables the GPIO registers.
	 */
	outb_p(SIO_PM_FER2, SIO_PM_INDEX);
	outb_p(SIO_PM_GP_EN, SIO_PM_DATA);

	/*
	 * Now, initialize the GPIO registers.
	 * We want them all to be inputs which is the
	 * power on default, so let's leave them alone.
	 * So, let's just read the board rev!
	 */
	raw = inb_p(SIO_GP_DATA1);
	raw &= 0x7f;	/* 7 bits of valid board revision ID. */

	if (visws_board_type == VISWS_320) {
		if (raw < 0x6) {
			visws_board_rev = 4;
		} else if (raw < 0xc) {
			visws_board_rev = 5;
		} else {
			visws_board_rev = 6;
		}
	} else if (visws_board_type == VISWS_540) {
			visws_board_rev = 2;
		} else {
			visws_board_rev = raw;
		}

	printk(KERN_INFO "Silicon Graphics Visual Workstation %s (rev %d) detected\n",
	       (visws_board_type == VISWS_320 ? "320" :
	       (visws_board_type == VISWS_540 ? "540" :
		"unknown")), visws_board_rev);
}

void __init pre_intr_init_hook(void)
{
	init_VISWS_APIC_irqs();
}

void __init intr_init_hook(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	apic_intr_init();
#endif
}

void __init pre_setup_arch_hook()
{
	visws_get_board_type_and_rev();
}

static struct irqaction irq0 = {
	.handler =	timer_interrupt,
	.flags =	SA_INTERRUPT,
	.name =		"timer",
};

void __init time_init_hook(void)
{
	printk(KERN_INFO "Starting Cobalt Timer system clock\n");

	/* Set the countdown value */
	co_cpu_write(CO_CPU_TIMEVAL, CO_TIME_HZ/HZ);

	/* Start the timer */
	co_cpu_write(CO_CPU_CTRL, co_cpu_read(CO_CPU_CTRL) | CO_CTRL_TIMERUN);

	/* Enable (unmask) the timer interrupt */
	co_cpu_write(CO_CPU_CTRL, co_cpu_read(CO_CPU_CTRL) & ~CO_CTRL_TIMEMASK);

	/* Wire cpu IDT entry to s/w handler (and Cobalt APIC to IDT) */
	setup_irq(0, &irq0);
}
