/* linux/arch/sparc/kernel/time.c
 *
 * Copyright (C) 1995 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 *
 * Chris Davis (cdavis@cois.on.ca) 03/27/1998
 * Added support for the intersil on the sun4/4200
 *
 * Gleb Raiko (rajko@mech.math.msu.su) 08/18/1998
 * Support for MicroSPARC-IIep, PCI CPU.
 *
 * This file handles the Sparc specific time handling details.
 *
 * 1997-09-10	Updated NTP code according to technical memorandum Jan '96
 *		"A Kernel Model for Precision Timekeeping" by Dave Mills
 */
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/rtc/m48t59.h>
#include <linux/timex.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/profile.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <asm/oplib.h>
#include <asm/timex.h>
#include <asm/timer.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/idprom.h>
#include <asm/machines.h>
#include <asm/page.h>
#include <asm/pcic.h>
#include <asm/irq_regs.h>

#include "irq.h"

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

static int set_rtc_mmss(unsigned long);

unsigned long profile_pc(struct pt_regs *regs)
{
	extern char __copy_user_begin[], __copy_user_end[];
	extern char __atomic_begin[], __atomic_end[];
	extern char __bzero_begin[], __bzero_end[];

	unsigned long pc = regs->pc;

	if (in_lock_functions(pc) ||
	    (pc >= (unsigned long) __copy_user_begin &&
	     pc < (unsigned long) __copy_user_end) ||
	    (pc >= (unsigned long) __atomic_begin &&
	     pc < (unsigned long) __atomic_end) ||
	    (pc >= (unsigned long) __bzero_begin &&
	     pc < (unsigned long) __bzero_end))
		pc = regs->u_regs[UREG_RETPC];
	return pc;
}

EXPORT_SYMBOL(profile_pc);

__volatile__ unsigned int *master_l10_counter;

u32 (*do_arch_gettimeoffset)(void);

int update_persistent_clock(struct timespec now)
{
	return set_rtc_mmss(now.tv_sec);
}

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "xtime_update()" routine every clocktick
 */

#define TICK_SIZE (tick_nsec / 1000)

static irqreturn_t timer_interrupt(int dummy, void *dev_id)
{
#ifndef CONFIG_SMP
	profile_tick(CPU_PROFILING);
#endif

	clear_clock_irq();

	xtime_update(1);

#ifndef CONFIG_SMP
	update_process_times(user_mode(get_irq_regs()));
#endif
	return IRQ_HANDLED;
}

static unsigned char mostek_read_byte(struct device *dev, u32 ofs)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct m48t59_plat_data *pdata = pdev->dev.platform_data;

	return readb(pdata->ioaddr + ofs);
}

static void mostek_write_byte(struct device *dev, u32 ofs, u8 val)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct m48t59_plat_data *pdata = pdev->dev.platform_data;

	writeb(val, pdata->ioaddr + ofs);
}

static struct m48t59_plat_data m48t59_data = {
	.read_byte = mostek_read_byte,
	.write_byte = mostek_write_byte,
};

/* resource is set at runtime */
static struct platform_device m48t59_rtc = {
	.name		= "rtc-m48t59",
	.id		= 0,
	.num_resources	= 1,
	.dev	= {
		.platform_data = &m48t59_data,
	},
};

static int __devinit clock_probe(struct platform_device *op)
{
	struct device_node *dp = op->dev.of_node;
	const char *model = of_get_property(dp, "model", NULL);

	if (!model)
		return -ENODEV;

	/* Only the primary RTC has an address property */
	if (!of_find_property(dp, "address", NULL))
		return -ENODEV;

	m48t59_rtc.resource = &op->resource[0];
	if (!strcmp(model, "mk48t02")) {
		/* Map the clock register io area read-only */
		m48t59_data.ioaddr = of_ioremap(&op->resource[0], 0,
						2048, "rtc-m48t59");
		m48t59_data.type = M48T59RTC_TYPE_M48T02;
	} else if (!strcmp(model, "mk48t08")) {
		m48t59_data.ioaddr = of_ioremap(&op->resource[0], 0,
						8192, "rtc-m48t59");
		m48t59_data.type = M48T59RTC_TYPE_M48T08;
	} else
		return -ENODEV;

	if (platform_device_register(&m48t59_rtc) < 0)
		printk(KERN_ERR "Registering RTC device failed\n");

	return 0;
}

static struct of_device_id clock_match[] = {
	{
		.name = "eeprom",
	},
	{},
};

static struct platform_driver clock_driver = {
	.probe		= clock_probe,
	.driver = {
		.name = "rtc",
		.owner = THIS_MODULE,
		.of_match_table = clock_match,
	},
};


/* Probe for the mostek real time clock chip. */
static int __init clock_init(void)
{
	return platform_driver_register(&clock_driver);
}
/* Must be after subsys_initcall() so that busses are probed.  Must
 * be before device_initcall() because things like the RTC driver
 * need to see the clock registers.
 */
fs_initcall(clock_init);


u32 sbus_do_gettimeoffset(void)
{
	unsigned long val = *master_l10_counter;
	unsigned long usec = (val >> 10) & 0x1fffff;

	/* Limit hit?  */
	if (val & 0x80000000)
		usec += 1000000 / HZ;

	return usec * 1000;
}


u32 arch_gettimeoffset(void)
{
	if (unlikely(!do_arch_gettimeoffset))
		return 0;
	return do_arch_gettimeoffset();
}

static void __init sbus_time_init(void)
{
	do_arch_gettimeoffset = sbus_do_gettimeoffset;

	btfixup();

	sparc_irq_config.init_timers(timer_interrupt);
}

void __init time_init(void)
{
#ifdef CONFIG_PCI
	extern void pci_time_init(void);
	if (pcic_present()) {
		pci_time_init();
		return;
	}
#endif
	sbus_time_init();
}


static int set_rtc_mmss(unsigned long secs)
{
	struct rtc_device *rtc = rtc_class_open("rtc0");
	int err = -1;

	if (rtc) {
		err = rtc_set_mmss(rtc, secs);
		rtc_class_close(rtc);
	}

	return err;
}
