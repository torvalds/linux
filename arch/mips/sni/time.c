#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/time.h>

#include <asm/i8253.h>
#include <asm/sni.h>
#include <asm/time.h>
#include <asm-generic/rtc.h>

#define SNI_CLOCK_TICK_RATE     3686400
#define SNI_COUNTER2_DIV        64
#define SNI_COUNTER0_DIV        ((SNI_CLOCK_TICK_RATE / SNI_COUNTER2_DIV) / HZ)

static void sni_a20r_timer_ack(void)
{
        *(volatile u8 *)A20R_PT_TIM0_ACK = 0x0; wmb();
}

/*
 * a20r platform uses 2 counters to divide the input frequency.
 * Counter 2 output is connected to Counter 0 & 1 input.
 */
static void __init sni_a20r_timer_setup(struct irqaction *irq)
{
        *(volatile u8 *)(A20R_PT_CLOCK_BASE + 12) = 0x34; wmb();
        *(volatile u8 *)(A20R_PT_CLOCK_BASE +  0) = (SNI_COUNTER0_DIV) & 0xff; wmb();
        *(volatile u8 *)(A20R_PT_CLOCK_BASE +  0) = (SNI_COUNTER0_DIV >> 8) & 0xff; wmb();

        *(volatile u8 *)(A20R_PT_CLOCK_BASE + 12) = 0xb4; wmb();
        *(volatile u8 *)(A20R_PT_CLOCK_BASE +  8) = (SNI_COUNTER2_DIV) & 0xff; wmb();
        *(volatile u8 *)(A20R_PT_CLOCK_BASE +  8) = (SNI_COUNTER2_DIV >> 8) & 0xff; wmb();

        setup_irq(SNI_A20R_IRQ_TIMER, irq);
        mips_timer_ack = sni_a20r_timer_ack;
}

#define SNI_8254_TICK_RATE        1193182UL

#define SNI_8254_TCSAMP_COUNTER   ((SNI_8254_TICK_RATE / HZ) + 255)

static __init unsigned long dosample(void)
{
	u32 ct0, ct1;
	volatile u8 msb, lsb;

	/* Start the counter. */
	outb_p(0x34, 0x43);
	outb_p(SNI_8254_TCSAMP_COUNTER & 0xff, 0x40);
	outb(SNI_8254_TCSAMP_COUNTER >> 8, 0x40);

	/* Get initial counter invariant */
	ct0 = read_c0_count();

	/* Latch and spin until top byte of counter0 is zero */
	do {
		outb(0x00, 0x43);
		lsb = inb(0x40);
		msb = inb(0x40);
		ct1 = read_c0_count();
	} while (msb);

	/* Stop the counter. */
	outb(0x38, 0x43);
	/*
	 * Return the difference, this is how far the r4k counter increments
	 * for every 1/HZ seconds. We round off the nearest 1 MHz of master
	 * clock (= 1000000 / HZ / 2).
	 */
	/*return (ct1 - ct0 + (500000/HZ/2)) / (500000/HZ) * (500000/HZ);*/
	return (ct1 - ct0) / (500000/HZ) * (500000/HZ);
}

/*
 * Here we need to calibrate the cycle counter to at least be close.
 */
void __init plat_time_init(void)
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

	setup_pit_timer();
}

/*
 * R4k counter based timer interrupt. Works on RM200-225 and possibly
 * others but not on RM400
 */
static void __init sni_cpu_timer_setup(struct irqaction *irq)
{
        setup_irq(SNI_MIPS_IRQ_CPU_TIMER, irq);
}

void __init plat_timer_setup(struct irqaction *irq)
{
	switch (sni_brd_type) {
	case SNI_BRD_10:
	case SNI_BRD_10NEW:
	case SNI_BRD_TOWER_OASIC:
	case SNI_BRD_MINITOWER:
	        sni_a20r_timer_setup(irq);
	        break;

	case SNI_BRD_PCI_TOWER:
	case SNI_BRD_RM200:
	case SNI_BRD_PCI_MTOWER:
	case SNI_BRD_PCI_DESKTOP:
	case SNI_BRD_PCI_TOWER_CPLUS:
	case SNI_BRD_PCI_MTOWER_CPLUS:
	        sni_cpu_timer_setup(irq);
	        break;
	}
}

unsigned long read_persistent_clock(void)
{
	return -1;
}
