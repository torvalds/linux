/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @file busfreq_ddr3.c
 *
 * @brief iMX6 DDR3 frequency change specific file.
 *
 * @ingroup PM
 */
#include <asm/cacheflush.h>
#include <asm/fncpy.h>
#include <asm/io.h>
#include <asm/mach/map.h>
#include <asm/mach-types.h>
#include <asm/tlb.h>
#include <linux/busfreq-imx.h>
#include <linux/clk.h>
#include <linux/clockchips.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/smp.h>

#include "hardware.h"
#include "common.h"

/* DDR settings */
static void __iomem *gic_dist_base;

static int curr_ddr_rate;

#define SMP_WFE_CODE_SIZE	0x400
void (*imx7d_change_ddr_freq)(u32 freq) = NULL;
extern void imx7d_ddr3_freq_change(u32 freq);
extern void imx_lpddr3_freq_change(u32 freq);

extern unsigned int ddr_med_rate;
extern unsigned int ddr_normal_rate;
extern int low_bus_freq_mode;
extern int audio_bus_freq_mode;
extern void mx6_ddr3_freq_change(u32 freq, void *ddr_settings,
	bool dll_mode, void *iomux_offsets);

extern unsigned long save_ttbr1(void);
extern void restore_ttbr1(unsigned long ttbr1);
extern unsigned long ddr_freq_change_iram_base;

extern unsigned long ddr_freq_change_total_size;
extern unsigned long iram_tlb_phys_addr;

#ifdef CONFIG_SMP
volatile u32 *wait_for_ddr_freq_update;
static unsigned int online_cpus;
static u32 *irqs_used;

void (*wfe_change_ddr_freq)(u32 cpuid, u32 *ddr_freq_change_done);
void (*imx7_wfe_change_ddr_freq)(u32 cpuid, u32 ocram_base);
extern void wfe_ddr3_freq_change(u32 cpuid, u32 *ddr_freq_change_done);
extern void imx7_smp_wfe(u32 cpuid, u32 ocram_base);
extern unsigned long wfe_ddr3_freq_change_start
	asm("wfe_ddr3_freq_change_start");
extern unsigned long wfe_ddr3_freq_change_end asm("wfe_ddr3_freq_change_end");
#endif

int can_change_ddr_freq(void)
{
	return 1;
}

#ifdef CONFIG_SMP
/*
 * each active core apart from the one changing
 * the DDR frequency will execute this function.
 * the rest of the cores have to remain in WFE
 * state until the frequency is changed.
 */
static irqreturn_t wait_in_wfe_irq(int irq, void *dev_id)
{
	u32 me;

	me = smp_processor_id();
#ifdef CONFIG_LOCAL_TIMERS
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER,
		&me);
#endif
	if (cpu_is_imx7d())
		imx7_wfe_change_ddr_freq(0x8 * me,
			(u32)ddr_freq_change_iram_base);

#ifdef CONFIG_LOCAL_TIMERS
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT,
		&me);
#endif

	return IRQ_HANDLED;
}
#endif

/* change the DDR frequency. */
int update_ddr_freq_imx_smp(int ddr_rate)
{
	int me = 0;
	unsigned long ttbr1;
#ifdef CONFIG_SMP
	unsigned int reg = 0;
	int cpu = 0;
#endif

	if (!can_change_ddr_freq())
		return -1;

	if (ddr_rate == curr_ddr_rate)
		return 0;

	printk(KERN_DEBUG "\nBus freq set to %d start...\n", ddr_rate);

	/* ensure that all Cores are in WFE. */
	local_irq_disable();

#ifdef CONFIG_SMP
	me = smp_processor_id();

	/* Make sure all the online cores are active */
	while (1) {
		bool not_exited_busfreq = false;
		u32 reg = 0;

		for_each_online_cpu(cpu) {
			if (cpu_is_imx7d())
				reg = *(wait_for_ddr_freq_update + 1);

			if (reg & (0x02 << (cpu * 8)))
				not_exited_busfreq = true;
		}
		if (!not_exited_busfreq)
			break;
	}

	wmb();
	*wait_for_ddr_freq_update = 1;
	dsb();
	if (cpu_is_imx7d())
		online_cpus = *(wait_for_ddr_freq_update + 1);

	for_each_online_cpu(cpu) {
		*((char *)(&online_cpus) + (u8)cpu) = 0x02;
		if (cpu != me) {
			/* set the interrupt to be pending in the GIC. */
			reg = 1 << (irqs_used[cpu] % 32);
			writel_relaxed(reg, gic_dist_base + GIC_DIST_PENDING_SET
				+ (irqs_used[cpu] / 32) * 4);
		}
	}
	/* Wait for the other active CPUs to idle */
	while (1) {
		u32 reg = 0;

		if (cpu_is_imx7d())
			reg = *(wait_for_ddr_freq_update + 1);
		reg |= (0x02 << (me * 8));
		if (reg == online_cpus)
			break;
	}
#endif

	/* Ensure iram_tlb_phys_addr is flushed to DDR. */
	__cpuc_flush_dcache_area(&iram_tlb_phys_addr,
		sizeof(iram_tlb_phys_addr));

	ttbr1 = save_ttbr1();
	/* Now we can change the DDR frequency. */
	if (cpu_is_imx7d())
		imx7d_change_ddr_freq(ddr_rate);

	restore_ttbr1(ttbr1);
	curr_ddr_rate = ddr_rate;

#ifdef CONFIG_SMP
	wmb();
	/* DDR frequency change is done . */
	*wait_for_ddr_freq_update = 0;
	dsb();

	/* wake up all the cores. */
	sev();
#endif

	local_irq_enable();

	printk(KERN_DEBUG "Bus freq set to %d done! cpu=%d\n", ddr_rate, me);

	return 0;
}

int init_ddrc_ddr_settings(struct platform_device *busfreq_pdev)
{
	int ddr_type = imx_ddrc_get_ddr_type();
#ifdef CONFIG_SMP
	struct device_node *node;
	u32 cpu;
	struct device *dev = &busfreq_pdev->dev;
	int err;
	struct irq_data *d;

	node = of_find_compatible_node(NULL, NULL, "arm,cortex-a7-gic");
	if (!node) {
		printk(KERN_ERR "failed to find imx7d-a7-gic device tree data!\n");
		return -EINVAL;
	}
	gic_dist_base = of_iomap(node, 0);
	WARN(!gic_dist_base, "unable to map gic dist registers\n");

	irqs_used = devm_kzalloc(dev, sizeof(u32) * num_present_cpus(),
					GFP_KERNEL);
	for_each_online_cpu(cpu) {
		int irq;
		/*
		 * set up a reserved interrupt to get all
		 * the active cores into a WFE state
		 * before changing the DDR frequency.
		 */
		irq = platform_get_irq(busfreq_pdev, cpu);
		err = request_irq(irq, wait_in_wfe_irq,
			IRQF_PERCPU, "ddrc", NULL);
		if (err) {
			dev_err(dev,
				"Busfreq:request_irq failed %d, err = %d\n",
				irq, err);
			return err;
		}
		err = irq_set_affinity(irq, cpumask_of(cpu));
		if (err) {
			dev_err(dev,
				"Busfreq: Cannot set irq affinity irq=%d\n",
				irq);
			return err;
		}
		d = irq_get_irq_data(irq);
		irqs_used[cpu] = d->hwirq + 32;
	}

	/* Store the variable used to communicate between cores */
	wait_for_ddr_freq_update = (u32 *)ddr_freq_change_iram_base;
	imx7_wfe_change_ddr_freq = (void *)fncpy(
		(void *)ddr_freq_change_iram_base + 0x8,
		&imx7_smp_wfe, SMP_WFE_CODE_SIZE - 0x8);
#endif
	if (ddr_type == IMX_DDR_TYPE_DDR3)
		imx7d_change_ddr_freq = (void *)fncpy(
			(void *)ddr_freq_change_iram_base + SMP_WFE_CODE_SIZE,
			&imx7d_ddr3_freq_change,
			MX7_BUSFREQ_OCRAM_SIZE - SMP_WFE_CODE_SIZE);
	else if (ddr_type == IMX_DDR_TYPE_LPDDR3)
		imx7d_change_ddr_freq = (void *)fncpy(
			(void *)ddr_freq_change_iram_base +
			SMP_WFE_CODE_SIZE,
			&imx_lpddr3_freq_change,
			MX7_BUSFREQ_OCRAM_SIZE - SMP_WFE_CODE_SIZE);

	curr_ddr_rate = ddr_normal_rate;

	return 0;
}
