/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Time operations for IP22 machines. Original code may come from
 * Ralf Baechle or David S. Miller (sorry guys, i'm really not sure)
 *
 * Copyright (C) 2001 by Ladislav Michl
 * Copyright (C) 2003, 06 Ralf Baechle (ralf@linux-mips.org)
 */
#include <linux/bcd.h>
#include <linux/i8253.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/time.h>
#include <linux/ftrace.h>

#include <asm/cpu.h>
#include <asm/mipsregs.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/time.h>
#include <asm/sgialib.h>
#include <asm/sgi/ioc.h>
#include <asm/sgi/hpc3.h>
#include <asm/sgi/ip22.h>

static unsigned long dosample(void)
{
	u32 ct0, ct1;
	u8 msb;

	/* Start the counter. */
	sgint->tcword = (SGINT_TCWORD_CNT2 | SGINT_TCWORD_CALL |
			 SGINT_TCWORD_MRGEN);
	sgint->tcnt2 = SGINT_TCSAMP_COUNTER & 0xff;
	sgint->tcnt2 = SGINT_TCSAMP_COUNTER >> 8;

	/* Get initial counter invariant */
	ct0 = read_c0_count();

	/* Latch and spin until top byte of counter2 is zero */
	do {
		writeb(SGINT_TCWORD_CNT2 | SGINT_TCWORD_CLAT, &sgint->tcword);
		(void) readb(&sgint->tcnt2);
		msb = readb(&sgint->tcnt2);
		ct1 = read_c0_count();
	} while (msb);

	/* Stop the counter. */
	writeb(SGINT_TCWORD_CNT2 | SGINT_TCWORD_CALL | SGINT_TCWORD_MSWST,
	       &sgint->tcword);
	/*
	 * Return the difference, this is how far the r4k counter increments
	 * for every 1/HZ seconds. We round off the nearest 1 MHz of master
	 * clock (= 1000000 / HZ / 2).
	 */

	return (ct1 - ct0) / (500000/HZ) * (500000/HZ);
}

/*
 * Here we need to calibrate the cycle counter to at least be close.
 */
__init void plat_time_init(void)
{
	unsigned long r4k_ticks[3];
	unsigned long r4k_tick;

	/*
	 * Figure out the r4k offset, the algorithm is very simple and works in
	 * _all_ cases as long as the 8254 counter register itself works ok (as
	 * an interrupt driving timer it does not because of bug, this is why
	 * we are using the onchip r4k counter/compare register to serve this
	 * purpose, but for r4k_offset calculation it will work ok for us).
	 * There are other very complicated ways of performing this calculation
	 * but this one works just fine so I am not going to futz around. ;-)
	 */
	printk(KERN_INFO "Calibrating system timer... ");
	dosample();	/* Prime cache. */
	dosample();	/* Prime cache. */
	/* Zero is NOT an option. */
	do {
		r4k_ticks[0] = dosample();
	} while (!r4k_ticks[0]);
	do {
		r4k_ticks[1] = dosample();
	} while (!r4k_ticks[1]);

	if (r4k_ticks[0] != r4k_ticks[1]) {
		printk("warning: timer counts differ, retrying... ");
		r4k_ticks[2] = dosample();
		if (r4k_ticks[2] == r4k_ticks[0]
		    || r4k_ticks[2] == r4k_ticks[1])
			r4k_tick = r4k_ticks[2];
		else {
			printk("disagreement, using average... ");
			r4k_tick = (r4k_ticks[0] + r4k_ticks[1]
				   + r4k_ticks[2]) / 3;
		}
	} else
		r4k_tick = r4k_ticks[0];

	printk("%d [%d.%04d MHz CPU]\n", (int) r4k_tick,
		(int) (r4k_tick / (500000 / HZ)),
		(int) (r4k_tick % (500000 / HZ)));

	mips_hpt_frequency = r4k_tick * HZ;

	if (ip22_is_fullhouse())
		setup_pit_timer();
}

/* Generic SGI handler for (spurious) 8254 interrupts */
void __irq_entry indy_8254timer_irq(void)
{
	int irq = SGI_8254_0_IRQ;
	ULONG cnt;
	char c;

	irq_enter();
	kstat_incr_irqs_this_cpu(irq, irq_to_desc(irq));
	printk(KERN_ALERT "Oops, got 8254 interrupt.\n");
	ArcRead(0, &c, 1, &cnt);
	ArcEnterInteractiveMode();
	irq_exit();
}
