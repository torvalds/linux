/*
 *  Copyright (C) 1995  Linus Torvalds
 *  Adapted from 'alpha' version by Gary Thomas
 *  Modified by Cort Dougan (cort@cs.nmt.edu)
 *  Modified for MBX using prep/chrp/pmac functions by Dan (dmalek@jlc.net)
 *  Further modified for generic 8xx by Dan.
 */

/*
 * bootup setup stuff..
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/fsl_devices.h>

#include <asm/io.h>
#include <asm/mpc8xx.h>
#include <asm/8xx_immap.h>
#include <asm/prom.h>
#include <asm/fs_pd.h>
#include <mm/mmu_decl.h>

#include <sysdev/mpc8xx_pic.h>

#include "mpc8xx.h"

struct mpc8xx_pcmcia_ops m8xx_pcmcia_ops;

extern int cpm_pic_init(void);
extern int cpm_get_irq(void);

/* A place holder for time base interrupts, if they are ever enabled. */
static irqreturn_t timebase_interrupt(int irq, void *dev)
{
	printk ("timebase_interrupt()\n");

	return IRQ_HANDLED;
}

static struct irqaction tbint_irqaction = {
	.handler = timebase_interrupt,
	.mask = CPU_MASK_NONE,
	.name = "tbint",
};

/* per-board overridable init_internal_rtc() function. */
void __init __attribute__ ((weak))
init_internal_rtc(void)
{
	sit8xx_t __iomem *sys_tmr = immr_map(im_sit);

	/* Disable the RTC one second and alarm interrupts. */
	clrbits16(&sys_tmr->sit_rtcsc, (RTCSC_SIE | RTCSC_ALE));

	/* Enable the RTC */
	setbits16(&sys_tmr->sit_rtcsc, (RTCSC_RTF | RTCSC_RTE));
	immr_unmap(sys_tmr);
}

static int __init get_freq(char *name, unsigned long *val)
{
	struct device_node *cpu;
	const unsigned int *fp;
	int found = 0;

	/* The cpu node should have timebase and clock frequency properties */
	cpu = of_find_node_by_type(NULL, "cpu");

	if (cpu) {
		fp = of_get_property(cpu, name, NULL);
		if (fp) {
			found = 1;
			*val = *fp;
		}

		of_node_put(cpu);
	}

	return found;
}

/* The decrementer counts at the system (internal) clock frequency divided by
 * sixteen, or external oscillator divided by four.  We force the processor
 * to use system clock divided by sixteen.
 */
void __init mpc8xx_calibrate_decr(void)
{
	struct device_node *cpu;
	cark8xx_t __iomem *clk_r1;
	car8xx_t __iomem *clk_r2;
	sitk8xx_t __iomem *sys_tmr1;
	sit8xx_t __iomem *sys_tmr2;
	int irq, virq;

	clk_r1 = immr_map(im_clkrstk);

	/* Unlock the SCCR. */
	out_be32(&clk_r1->cark_sccrk, ~KAPWR_KEY);
	out_be32(&clk_r1->cark_sccrk, KAPWR_KEY);
	immr_unmap(clk_r1);

	/* Force all 8xx processors to use divide by 16 processor clock. */
	clk_r2 = immr_map(im_clkrst);
	setbits32(&clk_r2->car_sccr, 0x02000000);
	immr_unmap(clk_r2);

	/* Processor frequency is MHz.
	 */
	ppc_proc_freq = 50000000;
	if (!get_freq("clock-frequency", &ppc_proc_freq))
		printk(KERN_ERR "WARNING: Estimating processor frequency "
		                "(not found)\n");

	ppc_tb_freq = ppc_proc_freq / 16;
	printk("Decrementer Frequency = 0x%lx\n", ppc_tb_freq);

	/* Perform some more timer/timebase initialization.  This used
	 * to be done elsewhere, but other changes caused it to get
	 * called more than once....that is a bad thing.
	 *
	 * First, unlock all of the registers we are going to modify.
	 * To protect them from corruption during power down, registers
	 * that are maintained by keep alive power are "locked".  To
	 * modify these registers we have to write the key value to
	 * the key location associated with the register.
	 * Some boards power up with these unlocked, while others
	 * are locked.  Writing anything (including the unlock code?)
	 * to the unlocked registers will lock them again.  So, here
	 * we guarantee the registers are locked, then we unlock them
	 * for our use.
	 */
	sys_tmr1 = immr_map(im_sitk);
	out_be32(&sys_tmr1->sitk_tbscrk, ~KAPWR_KEY);
	out_be32(&sys_tmr1->sitk_rtcsck, ~KAPWR_KEY);
	out_be32(&sys_tmr1->sitk_tbk, ~KAPWR_KEY);
	out_be32(&sys_tmr1->sitk_tbscrk, KAPWR_KEY);
	out_be32(&sys_tmr1->sitk_rtcsck, KAPWR_KEY);
	out_be32(&sys_tmr1->sitk_tbk, KAPWR_KEY);
	immr_unmap(sys_tmr1);

	init_internal_rtc();

	/* Enabling the decrementer also enables the timebase interrupts
	 * (or from the other point of view, to get decrementer interrupts
	 * we have to enable the timebase).  The decrementer interrupt
	 * is wired into the vector table, nothing to do here for that.
	 */
	cpu = of_find_node_by_type(NULL, "cpu");
	virq= irq_of_parse_and_map(cpu, 0);
	irq = irq_map[virq].hwirq;

	sys_tmr2 = immr_map(im_sit);
	out_be16(&sys_tmr2->sit_tbscr, ((1 << (7 - (irq/2))) << 8) |
					(TBSCR_TBF | TBSCR_TBE));
	immr_unmap(sys_tmr2);

	if (setup_irq(virq, &tbint_irqaction))
		panic("Could not allocate timer IRQ!");
}

/* The RTC on the MPC8xx is an internal register.
 * We want to protect this during power down, so we need to unlock,
 * modify, and re-lock.
 */

int mpc8xx_set_rtc_time(struct rtc_time *tm)
{
	sitk8xx_t __iomem *sys_tmr1;
	sit8xx_t __iomem *sys_tmr2;
	int time;

	sys_tmr1 = immr_map(im_sitk);
	sys_tmr2 = immr_map(im_sit);
	time = mktime(tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
	              tm->tm_hour, tm->tm_min, tm->tm_sec);

	out_be32(&sys_tmr1->sitk_rtck, KAPWR_KEY);
	out_be32(&sys_tmr2->sit_rtc, time);
	out_be32(&sys_tmr1->sitk_rtck, ~KAPWR_KEY);

	immr_unmap(sys_tmr2);
	immr_unmap(sys_tmr1);
	return 0;
}

void mpc8xx_get_rtc_time(struct rtc_time *tm)
{
	unsigned long data;
	sit8xx_t __iomem *sys_tmr = immr_map(im_sit);

	/* Get time from the RTC. */
	data = in_be32(&sys_tmr->sit_rtc);
	to_tm(data, tm);
	tm->tm_year -= 1900;
	tm->tm_mon -= 1;
	immr_unmap(sys_tmr);
	return;
}

void mpc8xx_restart(char *cmd)
{
	car8xx_t __iomem *clk_r = immr_map(im_clkrst);


	local_irq_disable();

	setbits32(&clk_r->car_plprcr, 0x00000080);
	/* Clear the ME bit in MSR to cause checkstop on machine check
	*/
	mtmsr(mfmsr() & ~0x1000);

	in_8(&clk_r->res[0]);
	panic("Restart failed\n");
}

static void cpm_cascade(unsigned int irq, struct irq_desc *desc)
{
	int cascade_irq;

	if ((cascade_irq = cpm_get_irq()) >= 0) {
		struct irq_desc *cdesc = irq_desc + cascade_irq;

		generic_handle_irq(cascade_irq);
		cdesc->chip->eoi(cascade_irq);
	}
	desc->chip->eoi(irq);
}

/* Initialize the internal interrupt controllers.  The number of
 * interrupts supported can vary with the processor type, and the
 * 82xx family can have up to 64.
 * External interrupts can be either edge or level triggered, and
 * need to be initialized by the appropriate driver.
 */
void __init mpc8xx_pics_init(void)
{
	int irq;

	if (mpc8xx_pic_init()) {
		printk(KERN_ERR "Failed interrupt 8xx controller  initialization\n");
		return;
	}

	irq = cpm_pic_init();
	if (irq != NO_IRQ)
		set_irq_chained_handler(irq, cpm_cascade);
}
