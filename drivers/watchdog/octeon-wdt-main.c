/*
 * Octeon Watchdog driver
 *
 * Copyright (C) 2007, 2008, 2009, 2010 Cavium Networks
 *
 * Converted to use WATCHDOG_CORE by Aaro Koskinen <aaro.koskinen@iki.fi>.
 *
 * Some parts derived from wdt.c
 *
 *	(c) Copyright 1996-1997 Alan Cox <alan@lxorguk.ukuu.org.uk>,
 *						All Rights Reserved.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Alan Cox nor CymruNet Ltd. admit liability nor provide
 *	warranty for any of this software. This material is provided
 *	"AS-IS" and at no charge.
 *
 *	(c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 *
 * The OCTEON watchdog has a maximum timeout of 2^32 * io_clock.
 * For most systems this is less than 10 seconds, so to allow for
 * software to request longer watchdog heartbeats, we maintain software
 * counters to count multiples of the base rate.  If the system locks
 * up in such a manner that we can not run the software counters, the
 * only result is a watchdog reset sooner than was requested.  But
 * that is OK, because in this case userspace would likely not be able
 * to do anything anyhow.
 *
 * The hardware watchdog interval we call the period.  The OCTEON
 * watchdog goes through several stages, after the first period an
 * irq is asserted, then if it is not reset, after the next period NMI
 * is asserted, then after an additional period a chip wide soft reset.
 * So for the software counters, we reset watchdog after each period
 * and decrement the counter.  But for the last two periods we need to
 * let the watchdog progress to the NMI stage so we disable the irq
 * and let it proceed.  Once in the NMI, we print the register state
 * to the serial port and then wait for the reset.
 *
 * A watchdog is maintained for each CPU in the system, that way if
 * one CPU suffers a lockup, we also get a register dump and reset.
 * The userspace ping resets the watchdog on all CPUs.
 *
 * Before userspace opens the watchdog device, we still run the
 * watchdogs to catch any lockups that may be kernel related.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/watchdog.h>
#include <linux/cpumask.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/fs.h>
#include <linux/irq.h>

#include <asm/mipsregs.h>
#include <asm/uasm.h>

#include <asm/octeon/octeon.h>

/* The count needed to achieve timeout_sec. */
static unsigned int timeout_cnt;

/* The maximum period supported. */
static unsigned int max_timeout_sec;

/* The current period.  */
static unsigned int timeout_sec;

/* Set to non-zero when userspace countdown mode active */
static int do_coundown;
static unsigned int countdown_reset;
static unsigned int per_cpu_countdown[NR_CPUS];

static cpumask_t irq_enabled_cpus;

#define WD_TIMO 60			/* Default heartbeat = 60 seconds */

static int heartbeat = WD_TIMO;
module_param(heartbeat, int, S_IRUGO);
MODULE_PARM_DESC(heartbeat,
	"Watchdog heartbeat in seconds. (0 < heartbeat, default="
				__MODULE_STRING(WD_TIMO) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, S_IRUGO);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static u32 nmi_stage1_insns[64] __initdata;
/* We need one branch and therefore one relocation per target label. */
static struct uasm_label labels[5] __initdata;
static struct uasm_reloc relocs[5] __initdata;

enum lable_id {
	label_enter_bootloader = 1
};

/* Some CP0 registers */
#define K0		26
#define C0_CVMMEMCTL 11, 7
#define C0_STATUS 12, 0
#define C0_EBASE 15, 1
#define C0_DESAVE 31, 0

void octeon_wdt_nmi_stage2(void);

static void __init octeon_wdt_build_stage1(void)
{
	int i;
	int len;
	u32 *p = nmi_stage1_insns;
#ifdef CONFIG_HOTPLUG_CPU
	struct uasm_label *l = labels;
	struct uasm_reloc *r = relocs;
#endif

	/*
	 * For the next few instructions running the debugger may
	 * cause corruption of k0 in the saved registers. Since we're
	 * about to crash, nobody probably cares.
	 *
	 * Save K0 into the debug scratch register
	 */
	uasm_i_dmtc0(&p, K0, C0_DESAVE);

	uasm_i_mfc0(&p, K0, C0_STATUS);
#ifdef CONFIG_HOTPLUG_CPU
	if (octeon_bootloader_entry_addr)
		uasm_il_bbit0(&p, &r, K0, ilog2(ST0_NMI),
			      label_enter_bootloader);
#endif
	/* Force 64-bit addressing enabled */
	uasm_i_ori(&p, K0, K0, ST0_UX | ST0_SX | ST0_KX);
	uasm_i_mtc0(&p, K0, C0_STATUS);

#ifdef CONFIG_HOTPLUG_CPU
	if (octeon_bootloader_entry_addr) {
		uasm_i_mfc0(&p, K0, C0_EBASE);
		/* Coreid number in K0 */
		uasm_i_andi(&p, K0, K0, 0xf);
		/* 8 * coreid in bits 16-31 */
		uasm_i_dsll_safe(&p, K0, K0, 3 + 16);
		uasm_i_ori(&p, K0, K0, 0x8001);
		uasm_i_dsll_safe(&p, K0, K0, 16);
		uasm_i_ori(&p, K0, K0, 0x0700);
		uasm_i_drotr_safe(&p, K0, K0, 32);
		/*
		 * Should result in: 0x8001,0700,0000,8*coreid which is
		 * CVMX_CIU_WDOGX(coreid) - 0x0500
		 *
		 * Now ld K0, CVMX_CIU_WDOGX(coreid)
		 */
		uasm_i_ld(&p, K0, 0x500, K0);
		/*
		 * If bit one set handle the NMI as a watchdog event.
		 * otherwise transfer control to bootloader.
		 */
		uasm_il_bbit0(&p, &r, K0, 1, label_enter_bootloader);
		uasm_i_nop(&p);
	}
#endif

	/* Clear Dcache so cvmseg works right. */
	uasm_i_cache(&p, 1, 0, 0);

	/* Use K0 to do a read/modify/write of CVMMEMCTL */
	uasm_i_dmfc0(&p, K0, C0_CVMMEMCTL);
	/* Clear out the size of CVMSEG	*/
	uasm_i_dins(&p, K0, 0, 0, 6);
	/* Set CVMSEG to its largest value */
	uasm_i_ori(&p, K0, K0, 0x1c0 | 54);
	/* Store the CVMMEMCTL value */
	uasm_i_dmtc0(&p, K0, C0_CVMMEMCTL);

	/* Load the address of the second stage handler */
	UASM_i_LA(&p, K0, (long)octeon_wdt_nmi_stage2);
	uasm_i_jr(&p, K0);
	uasm_i_dmfc0(&p, K0, C0_DESAVE);

#ifdef CONFIG_HOTPLUG_CPU
	if (octeon_bootloader_entry_addr) {
		uasm_build_label(&l, p, label_enter_bootloader);
		/* Jump to the bootloader and restore K0 */
		UASM_i_LA(&p, K0, (long)octeon_bootloader_entry_addr);
		uasm_i_jr(&p, K0);
		uasm_i_dmfc0(&p, K0, C0_DESAVE);
	}
#endif
	uasm_resolve_relocs(relocs, labels);

	len = (int)(p - nmi_stage1_insns);
	pr_debug("Synthesized NMI stage 1 handler (%d instructions)\n", len);

	pr_debug("\t.set push\n");
	pr_debug("\t.set noreorder\n");
	for (i = 0; i < len; i++)
		pr_debug("\t.word 0x%08x\n", nmi_stage1_insns[i]);
	pr_debug("\t.set pop\n");

	if (len > 32)
		panic("NMI stage 1 handler exceeds 32 instructions, was %d\n",
		      len);
}

static int cpu2core(int cpu)
{
#ifdef CONFIG_SMP
	return cpu_logical_map(cpu);
#else
	return cvmx_get_core_num();
#endif
}

static int core2cpu(int coreid)
{
#ifdef CONFIG_SMP
	return cpu_number_map(coreid);
#else
	return 0;
#endif
}

/**
 * Poke the watchdog when an interrupt is received
 *
 * @cpl:
 * @dev_id:
 *
 * Returns
 */
static irqreturn_t octeon_wdt_poke_irq(int cpl, void *dev_id)
{
	unsigned int core = cvmx_get_core_num();
	int cpu = core2cpu(core);

	if (do_coundown) {
		if (per_cpu_countdown[cpu] > 0) {
			/* We're alive, poke the watchdog */
			cvmx_write_csr(CVMX_CIU_PP_POKEX(core), 1);
			per_cpu_countdown[cpu]--;
		} else {
			/* Bad news, you are about to reboot. */
			disable_irq_nosync(cpl);
			cpumask_clear_cpu(cpu, &irq_enabled_cpus);
		}
	} else {
		/* Not open, just ping away... */
		cvmx_write_csr(CVMX_CIU_PP_POKEX(core), 1);
	}
	return IRQ_HANDLED;
}

/* From setup.c */
extern int prom_putchar(char c);

/**
 * Write a string to the uart
 *
 * @str:        String to write
 */
static void octeon_wdt_write_string(const char *str)
{
	/* Just loop writing one byte at a time */
	while (*str)
		prom_putchar(*str++);
}

/**
 * Write a hex number out of the uart
 *
 * @value:      Number to display
 * @digits:     Number of digits to print (1 to 16)
 */
static void octeon_wdt_write_hex(u64 value, int digits)
{
	int d;
	int v;

	for (d = 0; d < digits; d++) {
		v = (value >> ((digits - d - 1) * 4)) & 0xf;
		if (v >= 10)
			prom_putchar('a' + v - 10);
		else
			prom_putchar('0' + v);
	}
}

static const char reg_name[][3] = {
	"$0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
	"a4", "a5", "a6", "a7", "t0", "t1", "t2", "t3",
	"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
	"t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra"
};

/**
 * NMI stage 3 handler. NMIs are handled in the following manner:
 * 1) The first NMI handler enables CVMSEG and transfers from
 * the bootbus region into normal memory. It is careful to not
 * destroy any registers.
 * 2) The second stage handler uses CVMSEG to save the registers
 * and create a stack for C code. It then calls the third level
 * handler with one argument, a pointer to the register values.
 * 3) The third, and final, level handler is the following C
 * function that prints out some useful infomration.
 *
 * @reg:    Pointer to register state before the NMI
 */
void octeon_wdt_nmi_stage3(u64 reg[32])
{
	u64 i;

	unsigned int coreid = cvmx_get_core_num();
	/*
	 * Save status and cause early to get them before any changes
	 * might happen.
	 */
	u64 cp0_cause = read_c0_cause();
	u64 cp0_status = read_c0_status();
	u64 cp0_error_epc = read_c0_errorepc();
	u64 cp0_epc = read_c0_epc();

	/* Delay so output from all cores output is not jumbled together. */
	__delay(100000000ull * coreid);

	octeon_wdt_write_string("\r\n*** NMI Watchdog interrupt on Core 0x");
	octeon_wdt_write_hex(coreid, 1);
	octeon_wdt_write_string(" ***\r\n");
	for (i = 0; i < 32; i++) {
		octeon_wdt_write_string("\t");
		octeon_wdt_write_string(reg_name[i]);
		octeon_wdt_write_string("\t0x");
		octeon_wdt_write_hex(reg[i], 16);
		if (i & 1)
			octeon_wdt_write_string("\r\n");
	}
	octeon_wdt_write_string("\terr_epc\t0x");
	octeon_wdt_write_hex(cp0_error_epc, 16);

	octeon_wdt_write_string("\tepc\t0x");
	octeon_wdt_write_hex(cp0_epc, 16);
	octeon_wdt_write_string("\r\n");

	octeon_wdt_write_string("\tstatus\t0x");
	octeon_wdt_write_hex(cp0_status, 16);
	octeon_wdt_write_string("\tcause\t0x");
	octeon_wdt_write_hex(cp0_cause, 16);
	octeon_wdt_write_string("\r\n");

	octeon_wdt_write_string("\tsum0\t0x");
	octeon_wdt_write_hex(cvmx_read_csr(CVMX_CIU_INTX_SUM0(coreid * 2)), 16);
	octeon_wdt_write_string("\ten0\t0x");
	octeon_wdt_write_hex(cvmx_read_csr(CVMX_CIU_INTX_EN0(coreid * 2)), 16);
	octeon_wdt_write_string("\r\n");

	octeon_wdt_write_string("*** Chip soft reset soon ***\r\n");
}

static void octeon_wdt_disable_interrupt(int cpu)
{
	unsigned int core;
	unsigned int irq;
	union cvmx_ciu_wdogx ciu_wdog;

	core = cpu2core(cpu);

	irq = OCTEON_IRQ_WDOG0 + core;

	/* Poke the watchdog to clear out its state */
	cvmx_write_csr(CVMX_CIU_PP_POKEX(core), 1);

	/* Disable the hardware. */
	ciu_wdog.u64 = 0;
	cvmx_write_csr(CVMX_CIU_WDOGX(core), ciu_wdog.u64);

	free_irq(irq, octeon_wdt_poke_irq);
}

static void octeon_wdt_setup_interrupt(int cpu)
{
	unsigned int core;
	unsigned int irq;
	union cvmx_ciu_wdogx ciu_wdog;

	core = cpu2core(cpu);

	/* Disable it before doing anything with the interrupts. */
	ciu_wdog.u64 = 0;
	cvmx_write_csr(CVMX_CIU_WDOGX(core), ciu_wdog.u64);

	per_cpu_countdown[cpu] = countdown_reset;

	irq = OCTEON_IRQ_WDOG0 + core;

	if (request_irq(irq, octeon_wdt_poke_irq,
			IRQF_NO_THREAD, "octeon_wdt", octeon_wdt_poke_irq))
		panic("octeon_wdt: Couldn't obtain irq %d", irq);

	cpumask_set_cpu(cpu, &irq_enabled_cpus);

	/* Poke the watchdog to clear out its state */
	cvmx_write_csr(CVMX_CIU_PP_POKEX(core), 1);

	/* Finally enable the watchdog now that all handlers are installed */
	ciu_wdog.u64 = 0;
	ciu_wdog.s.len = timeout_cnt;
	ciu_wdog.s.mode = 3;	/* 3 = Interrupt + NMI + Soft-Reset */
	cvmx_write_csr(CVMX_CIU_WDOGX(core), ciu_wdog.u64);
}

static int octeon_wdt_cpu_callback(struct notifier_block *nfb,
					   unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_DOWN_PREPARE:
		octeon_wdt_disable_interrupt(cpu);
		break;
	case CPU_ONLINE:
	case CPU_DOWN_FAILED:
		octeon_wdt_setup_interrupt(cpu);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static int octeon_wdt_ping(struct watchdog_device __always_unused *wdog)
{
	int cpu;
	int coreid;

	for_each_online_cpu(cpu) {
		coreid = cpu2core(cpu);
		cvmx_write_csr(CVMX_CIU_PP_POKEX(coreid), 1);
		per_cpu_countdown[cpu] = countdown_reset;
		if ((countdown_reset || !do_coundown) &&
		    !cpumask_test_cpu(cpu, &irq_enabled_cpus)) {
			/* We have to enable the irq */
			int irq = OCTEON_IRQ_WDOG0 + coreid;

			enable_irq(irq);
			cpumask_set_cpu(cpu, &irq_enabled_cpus);
		}
	}
	return 0;
}

static void octeon_wdt_calc_parameters(int t)
{
	unsigned int periods;

	timeout_sec = max_timeout_sec;


	/*
	 * Find the largest interrupt period, that can evenly divide
	 * the requested heartbeat time.
	 */
	while ((t % timeout_sec) != 0)
		timeout_sec--;

	periods = t / timeout_sec;

	/*
	 * The last two periods are after the irq is disabled, and
	 * then to the nmi, so we subtract them off.
	 */

	countdown_reset = periods > 2 ? periods - 2 : 0;
	heartbeat = t;
	timeout_cnt = ((octeon_get_io_clock_rate() >> 8) * timeout_sec) >> 8;
}

static int octeon_wdt_set_timeout(struct watchdog_device *wdog,
				  unsigned int t)
{
	int cpu;
	int coreid;
	union cvmx_ciu_wdogx ciu_wdog;

	if (t <= 0)
		return -1;

	octeon_wdt_calc_parameters(t);

	for_each_online_cpu(cpu) {
		coreid = cpu2core(cpu);
		cvmx_write_csr(CVMX_CIU_PP_POKEX(coreid), 1);
		ciu_wdog.u64 = 0;
		ciu_wdog.s.len = timeout_cnt;
		ciu_wdog.s.mode = 3;	/* 3 = Interrupt + NMI + Soft-Reset */
		cvmx_write_csr(CVMX_CIU_WDOGX(coreid), ciu_wdog.u64);
		cvmx_write_csr(CVMX_CIU_PP_POKEX(coreid), 1);
	}
	octeon_wdt_ping(wdog); /* Get the irqs back on. */
	return 0;
}

static int octeon_wdt_start(struct watchdog_device *wdog)
{
	octeon_wdt_ping(wdog);
	do_coundown = 1;
	return 0;
}

static int octeon_wdt_stop(struct watchdog_device *wdog)
{
	do_coundown = 0;
	octeon_wdt_ping(wdog);
	return 0;
}

static struct notifier_block octeon_wdt_cpu_notifier = {
	.notifier_call = octeon_wdt_cpu_callback,
};

static const struct watchdog_info octeon_wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE | WDIOF_KEEPALIVEPING,
	.identity = "OCTEON",
};

static const struct watchdog_ops octeon_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= octeon_wdt_start,
	.stop		= octeon_wdt_stop,
	.ping		= octeon_wdt_ping,
	.set_timeout	= octeon_wdt_set_timeout,
};

static struct watchdog_device octeon_wdt = {
	.info	= &octeon_wdt_info,
	.ops	= &octeon_wdt_ops,
};

/**
 * Module/ driver initialization.
 *
 * Returns Zero on success
 */
static int __init octeon_wdt_init(void)
{
	int i;
	int ret;
	int cpu;
	u64 *ptr;

	/*
	 * Watchdog time expiration length = The 16 bits of LEN
	 * represent the most significant bits of a 24 bit decrementer
	 * that decrements every 256 cycles.
	 *
	 * Try for a timeout of 5 sec, if that fails a smaller number
	 * of even seconds,
	 */
	max_timeout_sec = 6;
	do {
		max_timeout_sec--;
		timeout_cnt = ((octeon_get_io_clock_rate() >> 8) *
			      max_timeout_sec) >> 8;
	} while (timeout_cnt > 65535);

	BUG_ON(timeout_cnt == 0);

	octeon_wdt_calc_parameters(heartbeat);

	pr_info("Initial granularity %d Sec\n", timeout_sec);

	octeon_wdt.timeout	= timeout_sec;
	octeon_wdt.max_timeout	= UINT_MAX;

	watchdog_set_nowayout(&octeon_wdt, nowayout);

	ret = watchdog_register_device(&octeon_wdt);
	if (ret) {
		pr_err("watchdog_register_device() failed: %d\n", ret);
		return ret;
	}

	/* Build the NMI handler ... */
	octeon_wdt_build_stage1();

	/* ... and install it. */
	ptr = (u64 *) nmi_stage1_insns;
	for (i = 0; i < 16; i++) {
		cvmx_write_csr(CVMX_MIO_BOOT_LOC_ADR, i * 8);
		cvmx_write_csr(CVMX_MIO_BOOT_LOC_DAT, ptr[i]);
	}
	cvmx_write_csr(CVMX_MIO_BOOT_LOC_CFGX(0), 0x81fc0000);

	cpumask_clear(&irq_enabled_cpus);

	cpu_notifier_register_begin();
	for_each_online_cpu(cpu)
		octeon_wdt_setup_interrupt(cpu);

	__register_hotcpu_notifier(&octeon_wdt_cpu_notifier);
	cpu_notifier_register_done();

	return 0;
}

/**
 * Module / driver shutdown
 */
static void __exit octeon_wdt_cleanup(void)
{
	int cpu;

	watchdog_unregister_device(&octeon_wdt);

	cpu_notifier_register_begin();
	__unregister_hotcpu_notifier(&octeon_wdt_cpu_notifier);

	for_each_online_cpu(cpu) {
		int core = cpu2core(cpu);
		/* Disable the watchdog */
		cvmx_write_csr(CVMX_CIU_WDOGX(core), 0);
		/* Free the interrupt handler */
		free_irq(OCTEON_IRQ_WDOG0 + core, octeon_wdt_poke_irq);
	}

	cpu_notifier_register_done();

	/*
	 * Disable the boot-bus memory, the code it points to is soon
	 * to go missing.
	 */
	cvmx_write_csr(CVMX_MIO_BOOT_LOC_CFGX(0), 0);
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cavium Networks <support@caviumnetworks.com>");
MODULE_DESCRIPTION("Cavium Networks Octeon Watchdog driver.");
module_init(octeon_wdt_init);
module_exit(octeon_wdt_cleanup);
