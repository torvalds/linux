/*
 * mrst.c: Intel Moorestown platform specific setup code
 *
 * (C) Copyright 2008 Intel Corporation
 * Author: Jacob Pan (jacob.jun.pan@intel.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sfi.h>
#include <linux/irq.h>
#include <linux/module.h>

#include <asm/setup.h>
#include <asm/mpspec_def.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/mrst.h>
#include <asm/io.h>
#include <asm/i8259.h>
#include <asm/apb_timer.h>

static u32 sfi_mtimer_usage[SFI_MTMR_MAX_NUM];
static struct sfi_timer_table_entry sfi_mtimer_array[SFI_MTMR_MAX_NUM];
int sfi_mtimer_num;

struct sfi_rtc_table_entry sfi_mrtc_array[SFI_MRTC_MAX];
EXPORT_SYMBOL_GPL(sfi_mrtc_array);
int sfi_mrtc_num;

static inline void assign_to_mp_irq(struct mpc_intsrc *m,
				    struct mpc_intsrc *mp_irq)
{
	memcpy(mp_irq, m, sizeof(struct mpc_intsrc));
}

static inline int mp_irq_cmp(struct mpc_intsrc *mp_irq,
				struct mpc_intsrc *m)
{
	return memcmp(mp_irq, m, sizeof(struct mpc_intsrc));
}

static void save_mp_irq(struct mpc_intsrc *m)
{
	int i;

	for (i = 0; i < mp_irq_entries; i++) {
		if (!mp_irq_cmp(&mp_irqs[i], m))
			return;
	}

	assign_to_mp_irq(m, &mp_irqs[mp_irq_entries]);
	if (++mp_irq_entries == MAX_IRQ_SOURCES)
		panic("Max # of irq sources exceeded!!\n");
}

/* parse all the mtimer info to a static mtimer array */
static int __init sfi_parse_mtmr(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_timer_table_entry *pentry;
	struct mpc_intsrc mp_irq;
	int totallen;

	sb = (struct sfi_table_simple *)table;
	if (!sfi_mtimer_num) {
		sfi_mtimer_num = SFI_GET_NUM_ENTRIES(sb,
					struct sfi_timer_table_entry);
		pentry = (struct sfi_timer_table_entry *) sb->pentry;
		totallen = sfi_mtimer_num * sizeof(*pentry);
		memcpy(sfi_mtimer_array, pentry, totallen);
	}

	printk(KERN_INFO "SFI: MTIMER info (num = %d):\n", sfi_mtimer_num);
	pentry = sfi_mtimer_array;
	for (totallen = 0; totallen < sfi_mtimer_num; totallen++, pentry++) {
		printk(KERN_INFO "timer[%d]: paddr = 0x%08x, freq = %dHz,"
			" irq = %d\n", totallen, (u32)pentry->phys_addr,
			pentry->freq_hz, pentry->irq);
			if (!pentry->irq)
				continue;
			mp_irq.type = MP_IOAPIC;
			mp_irq.irqtype = mp_INT;
/* triggering mode edge bit 2-3, active high polarity bit 0-1 */
			mp_irq.irqflag = 5;
			mp_irq.srcbus = 0;
			mp_irq.srcbusirq = pentry->irq;	/* IRQ */
			mp_irq.dstapic = MP_APIC_ALL;
			mp_irq.dstirq = pentry->irq;
			save_mp_irq(&mp_irq);
	}

	return 0;
}

struct sfi_timer_table_entry *sfi_get_mtmr(int hint)
{
	int i;
	if (hint < sfi_mtimer_num) {
		if (!sfi_mtimer_usage[hint]) {
			pr_debug("hint taken for timer %d irq %d\n",\
				hint, sfi_mtimer_array[hint].irq);
			sfi_mtimer_usage[hint] = 1;
			return &sfi_mtimer_array[hint];
		}
	}
	/* take the first timer available */
	for (i = 0; i < sfi_mtimer_num;) {
		if (!sfi_mtimer_usage[i]) {
			sfi_mtimer_usage[i] = 1;
			return &sfi_mtimer_array[i];
		}
		i++;
	}
	return NULL;
}

void sfi_free_mtmr(struct sfi_timer_table_entry *mtmr)
{
	int i;
	for (i = 0; i < sfi_mtimer_num;) {
		if (mtmr->irq == sfi_mtimer_array[i].irq) {
			sfi_mtimer_usage[i] = 0;
			return;
		}
		i++;
	}
}

/* parse all the mrtc info to a global mrtc array */
int __init sfi_parse_mrtc(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_rtc_table_entry *pentry;
	struct mpc_intsrc mp_irq;

	int totallen;

	sb = (struct sfi_table_simple *)table;
	if (!sfi_mrtc_num) {
		sfi_mrtc_num = SFI_GET_NUM_ENTRIES(sb,
						struct sfi_rtc_table_entry);
		pentry = (struct sfi_rtc_table_entry *)sb->pentry;
		totallen = sfi_mrtc_num * sizeof(*pentry);
		memcpy(sfi_mrtc_array, pentry, totallen);
	}

	printk(KERN_INFO "SFI: RTC info (num = %d):\n", sfi_mrtc_num);
	pentry = sfi_mrtc_array;
	for (totallen = 0; totallen < sfi_mrtc_num; totallen++, pentry++) {
		printk(KERN_INFO "RTC[%d]: paddr = 0x%08x, irq = %d\n",
			totallen, (u32)pentry->phys_addr, pentry->irq);
		mp_irq.type = MP_IOAPIC;
		mp_irq.irqtype = mp_INT;
		mp_irq.irqflag = 0;
		mp_irq.srcbus = 0;
		mp_irq.srcbusirq = pentry->irq;	/* IRQ */
		mp_irq.dstapic = MP_APIC_ALL;
		mp_irq.dstirq = pentry->irq;
		save_mp_irq(&mp_irq);
	}
	return 0;
}

/*
 * the secondary clock in Moorestown can be APBT or LAPIC clock, default to
 * APBT but cmdline option can also override it.
 */
static void __cpuinit mrst_setup_secondary_clock(void)
{
	/* restore default lapic clock if disabled by cmdline */
	if (disable_apbt_percpu)
		return setup_secondary_APIC_clock();
	apbt_setup_secondary_clock();
}

static unsigned long __init mrst_calibrate_tsc(void)
{
	unsigned long flags, fast_calibrate;

	local_irq_save(flags);
	fast_calibrate = apbt_quick_calibrate();
	local_irq_restore(flags);

	if (fast_calibrate)
		return fast_calibrate;

	return 0;
}

void __init mrst_time_init(void)
{
	sfi_table_parse(SFI_SIG_MTMR, NULL, NULL, sfi_parse_mtmr);
	pre_init_apic_IRQ0();
	apbt_time_init();
}

void __init mrst_rtc_init(void)
{
	sfi_table_parse(SFI_SIG_MRTC, NULL, NULL, sfi_parse_mrtc);
}

/*
 * if we use per cpu apb timer, the bootclock already setup. if we use lapic
 * timer and one apbt timer for broadcast, we need to set up lapic boot clock.
 */
static void __init mrst_setup_boot_clock(void)
{
	pr_info("%s: per cpu apbt flag %d \n", __func__, disable_apbt_percpu);
	if (disable_apbt_percpu)
		setup_boot_APIC_clock();
};

/*
 * Moorestown specific x86_init function overrides and early setup
 * calls.
 */
void __init x86_mrst_early_setup(void)
{
	x86_init.resources.probe_roms = x86_init_noop;
	x86_init.resources.reserve_resources = x86_init_noop;

	x86_init.timers.timer_init = mrst_time_init;
	x86_init.timers.setup_percpu_clockev = mrst_setup_boot_clock;

	x86_init.irqs.pre_vector_init = x86_init_noop;

	x86_cpuinit.setup_percpu_clockev = mrst_setup_secondary_clock;

	x86_platform.calibrate_tsc = mrst_calibrate_tsc;
	x86_init.pci.init = pci_mrst_init;
	x86_init.pci.fixup_irqs = x86_init_noop;

	legacy_pic = &null_legacy_pic;
}
