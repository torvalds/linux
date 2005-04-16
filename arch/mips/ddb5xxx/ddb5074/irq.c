/*
 *  arch/mips/ddb5074/irq.c -- NEC DDB Vrc-5074 interrupt routines
 *
 *  Copyright (C) 2000 Geert Uytterhoeven <geert@sonycom.com>
 *                     Sony Software Development Center Europe (SDCE), Brussels
 */
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>

#include <asm/i8259.h>
#include <asm/io.h>
#include <asm/irq_cpu.h>
#include <asm/ptrace.h>
#include <asm/nile4.h>
#include <asm/ddb5xxx/ddb5xxx.h>
#include <asm/ddb5xxx/ddb5074.h>


extern asmlinkage void ddbIRQ(void);

static struct irqaction irq_cascade = { no_action, 0, CPU_MASK_NONE, "cascade", NULL, NULL };

#define M1543_PNP_CONFIG	0x03f0	/* PnP Config Port */
#define M1543_PNP_INDEX		0x03f0	/* PnP Index Port */
#define M1543_PNP_DATA		0x03f1	/* PnP Data Port */

#define M1543_PNP_ALT_CONFIG	0x0370	/* Alternative PnP Config Port */
#define M1543_PNP_ALT_INDEX	0x0370	/* Alternative PnP Index Port */
#define M1543_PNP_ALT_DATA	0x0371	/* Alternative PnP Data Port */

#define M1543_INT1_MASTER_CTRL	0x0020	/* INT_1 (master) Control Register */
#define M1543_INT1_MASTER_MASK	0x0021	/* INT_1 (master) Mask Register */

#define M1543_INT1_SLAVE_CTRL	0x00a0	/* INT_1 (slave) Control Register */
#define M1543_INT1_SLAVE_MASK	0x00a1	/* INT_1 (slave) Mask Register */

#define M1543_INT1_MASTER_ELCR	0x04d0	/* INT_1 (master) Edge/Level Control */
#define M1543_INT1_SLAVE_ELCR	0x04d1	/* INT_1 (slave) Edge/Level Control */


static void m1543_irq_setup(void)
{
	/*
	 *  The ALI M1543 has 13 interrupt inputs, IRQ1..IRQ13.  Not all
	 *  the possible IO sources in the M1543 are in use by us.  We will
	 *  use the following mapping:
	 *
	 *      IRQ1  - keyboard (default set by M1543)
	 *      IRQ3  - reserved for UART B (default set by M1543) (note that
	 *              the schematics for the DDB Vrc-5074 board seem to
	 *              indicate that IRQ3 is connected to the DS1386
	 *              watchdog timer interrupt output so we might have
	 *              a conflict)
	 *      IRQ4  - reserved for UART A (default set by M1543)
	 *      IRQ5  - parallel (default set by M1543)
	 *      IRQ8  - DS1386 time of day (RTC) interrupt
	 *      IRQ12 - mouse
	 */

	/*
	 *  Assing mouse interrupt to IRQ12
	 */

	/* Enter configuration mode */
	outb(0x51, M1543_PNP_CONFIG);
	outb(0x23, M1543_PNP_CONFIG);

	/* Select logical device 7 (Keyboard) */
	outb(0x07, M1543_PNP_INDEX);
	outb(0x07, M1543_PNP_DATA);

	/* Select IRQ12 */
	outb(0x72, M1543_PNP_INDEX);
	outb(0x0c, M1543_PNP_DATA);

	outb(0x30, M1543_PNP_INDEX);
	printk("device 7, 0x30: %02x\n",inb(M1543_PNP_DATA));

	outb(0x70, M1543_PNP_INDEX);
	printk("device 7, 0x70: %02x\n",inb(M1543_PNP_DATA));

	/* Leave configration mode */
	outb(0xbb, M1543_PNP_CONFIG);


}

void ddb_local0_irqdispatch(struct pt_regs *regs)
{
	u32 mask;
	int nile4_irq;

	mask = nile4_get_irq_stat(0);

	/* Handle the timer interrupt first */
#if 0
	if (mask & (1 << NILE4_INT_GPT)) {
		do_IRQ(nile4_to_irq(NILE4_INT_GPT), regs);
		mask &= ~(1 << NILE4_INT_GPT);
	}
#endif
	for (nile4_irq = 0; mask; nile4_irq++, mask >>= 1)
		if (mask & 1) {
			if (nile4_irq == NILE4_INT_INTE) {
				int i8259_irq;

				nile4_clear_irq(NILE4_INT_INTE);
				i8259_irq = nile4_i8259_iack();
				do_IRQ(i8259_irq, regs);
			} else
				do_IRQ(nile4_to_irq(nile4_irq), regs);

		}
}

void ddb_local1_irqdispatch(void)
{
	printk("ddb_local1_irqdispatch called\n");
}

void ddb_buserror_irq(void)
{
	printk("ddb_buserror_irq called\n");
}

void ddb_8254timer_irq(void)
{
	printk("ddb_8254timer_irq called\n");
}

void __init arch_init_irq(void)
{
	/* setup cascade interrupts */
	setup_irq(NILE4_IRQ_BASE  + NILE4_INT_INTE, &irq_cascade);
	setup_irq(CPU_IRQ_BASE + CPU_NILE4_CASCADE, &irq_cascade);

	set_except_vector(0, ddbIRQ);

	nile4_irq_setup(NILE4_IRQ_BASE);
	m1543_irq_setup();
	init_i8259_irqs();


	printk("CPU_IRQ_BASE: %d\n",CPU_IRQ_BASE);

	mips_cpu_irq_init(CPU_IRQ_BASE);

	printk("enabling 8259 cascade\n");

	ddb5074_led_hex(0);

	/* Enable the interrupt cascade */
	nile4_enable_irq(NILE4_IRQ_BASE+IRQ_I8259_CASCADE);
}
