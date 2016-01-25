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
 * @file busfreq_lpddr2.c
 *
 * @brief iMX6 LPDDR2 frequency change specific file.
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

#include "common.h"
#include "hardware.h"

static struct device *busfreq_dev;
static int curr_ddr_rate;
static DEFINE_SPINLOCK(freq_lock);

void (*mx6_change_lpddr2_freq)(u32 ddr_freq, int bus_freq_mode) = NULL;

extern unsigned int ddr_normal_rate;
extern void mx6_lpddr2_freq_change(u32 freq, int bus_freq_mode);
extern void imx6_up_lpddr2_freq_change(u32 freq, int bus_freq_mode);
extern unsigned long save_ttbr1(void);
extern void restore_ttbr1(unsigned long ttbr1);
extern void mx6q_lpddr2_freq_change(u32 freq, int bus_freq_mode);
extern unsigned long ddr_freq_change_iram_base;
extern unsigned long imx6_lpddr2_freq_change_start asm("imx6_lpddr2_freq_change_start");
extern unsigned long imx6_lpddr2_freq_change_end asm("imx6_lpddr2_freq_change_end");
extern unsigned long mx6q_lpddr2_freq_change_start asm("mx6q_lpddr2_freq_change_start");
extern unsigned long mx6q_lpddr2_freq_change_end asm("mx6q_lpddr2_freq_change_end");
extern unsigned long iram_tlb_phys_addr;

#ifdef CONFIG_SMP
volatile u32 *wait_for_lpddr2_freq_update;
static unsigned int online_cpus;
static u32 *irqs_used;
void (*wfe_change_lpddr2_freq)(u32 cpuid, u32 *ddr_freq_change_done);
extern void wfe_smp_freq_change(u32 cpuid, u32 *ddr_freq_change_done);
extern unsigned long wfe_smp_freq_change_start asm("wfe_smp_freq_change_start");
extern unsigned long wfe_smp_freq_change_end asm("wfe_smp_freq_change_end");
extern void __iomem *imx_scu_base;
static void __iomem *gic_dist_base;
#endif

#define SMP_WFE_CODE_SIZE 0x400

#ifdef CONFIG_SMP
static irqreturn_t wait_in_wfe_irq(int irq, void *dev_id)
{
	u32 me;

	me = smp_processor_id();
#ifdef CONFIG_LOCAL_TIMERS
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &me);
#endif
	wfe_change_lpddr2_freq(0xff << (me * 8),
			(u32 *)ddr_freq_change_iram_base);
#ifdef CONFIG_LOCAL_TIMERS
	clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &me);
#endif
	return IRQ_HANDLED;
}
#endif

/* change the DDR frequency. */
int update_lpddr2_freq(int ddr_rate)
{
	unsigned long ttbr1, flags;
	int mode = get_bus_freq_mode();

	if (ddr_rate == curr_ddr_rate)
		return 0;

	printk(KERN_DEBUG "\nBus freq set to %d start...\n", ddr_rate);

	spin_lock_irqsave(&freq_lock, flags);
	/*
	 * Flush the TLB, to ensure no TLB maintenance occurs
	 * when DDR is in self-refresh.
	 */
	ttbr1 = save_ttbr1();

	/* Now change DDR frequency. */
	mx6_change_lpddr2_freq(ddr_rate,
		(mode == BUS_FREQ_LOW || mode == BUS_FREQ_ULTRA_LOW) ? 1 : 0);
	restore_ttbr1(ttbr1);

	curr_ddr_rate = ddr_rate;
	spin_unlock_irqrestore(&freq_lock, flags);

	printk(KERN_DEBUG "\nBus freq set to %d done...\n", ddr_rate);

	return 0;
}

int init_mmdc_lpddr2_settings(struct platform_device *busfreq_pdev)
{
	unsigned long ddr_code_size;
	busfreq_dev = &busfreq_pdev->dev;

	ddr_code_size = SZ_4K;

	if (cpu_is_imx6sl())
		mx6_change_lpddr2_freq = (void *)fncpy(
			(void *)ddr_freq_change_iram_base,
			&mx6_lpddr2_freq_change, ddr_code_size);
	if (cpu_is_imx6sx() || cpu_is_imx6ul())
		mx6_change_lpddr2_freq = (void *)fncpy(
			(void *)ddr_freq_change_iram_base,
			&imx6_up_lpddr2_freq_change, ddr_code_size);

	curr_ddr_rate = ddr_normal_rate;

	return 0;
}

int update_lpddr2_freq_smp(int ddr_rate)
{
	unsigned long ttbr1;
	int mode = get_bus_freq_mode();
#ifdef CONFIG_SMP
	int cpu = 0;
	int me = 0;
	u32 reg;
#endif

	if (ddr_rate == curr_ddr_rate)
		return 0;

	printk(KERN_DEBUG "Bus freq set to %d start...\n", ddr_rate);

	/* ensure that all Cores are in WFE. */
	local_irq_disable();

#ifdef CONFIG_SMP
	me = smp_processor_id();

	/* Make sure all the online cores are active */
	while (1) {
		bool not_exited_busfreq = false;
		for_each_online_cpu(cpu) {
			reg = __raw_readl(imx_scu_base + 0x08);
			if (reg & (0x02 << (cpu * 8)))
				not_exited_busfreq = true;
		}
		if (!not_exited_busfreq)
			break;
	}

	wmb();
	*wait_for_lpddr2_freq_update = 1;
	dsb();
	online_cpus = readl_relaxed(imx_scu_base + 0x08);
	for_each_online_cpu(cpu) {
		*((char *)(&online_cpus) + (u8)cpu) = 0x02;
		if (cpu != me) {
			reg = 1 << (irqs_used[cpu] % 32);
			writel_relaxed(reg, gic_dist_base + GIC_DIST_PENDING_SET
					+ (irqs_used[cpu] / 32) * 4);
		}
	}

	/* Wait for the other active CPUs to idle */
	while (1) {
		reg = readl_relaxed(imx_scu_base + 0x08);
		reg |= (0x02 << (me * 8));
		if (reg == online_cpus)
			break;
	}
#endif

	/* Ensure iram_tlb_phys_addr is flushed to DDR. */
	__cpuc_flush_dcache_area(&iram_tlb_phys_addr,
			sizeof(iram_tlb_phys_addr));
	outer_clean_range(__pa(&iram_tlb_phys_addr),
			__pa(&iram_tlb_phys_addr + 1));
	/*
	 * Flush the TLB, to ensure no TLB maintenance occurs
	 * when DDR is in self-refresh.
	 */
	ttbr1 = save_ttbr1();

	/* Now change DDR frequency. */
	mx6_change_lpddr2_freq(ddr_rate,
		(mode == BUS_FREQ_LOW || mode == BUS_FREQ_ULTRA_LOW) ? 1 : 0);

	restore_ttbr1(ttbr1);

	curr_ddr_rate = ddr_rate;

#ifdef CONFIG_SMP
	wmb();
	/* DDR frequency change is done . */
	*wait_for_lpddr2_freq_update = 0;
	dsb();
	/* wake up all the cores. */
	sev();
#endif

	local_irq_enable();
	printk(KERN_DEBUG "Bus freq set to %d done! cpu=%d\n", ddr_rate, me);

	return 0;
}

int init_mmdc_lpddr2_settings_mx6q(struct platform_device *busfreq_pdev)
{
	unsigned long ddr_code_size;
#ifdef CONFIG_SMP
	struct device *dev = &busfreq_pdev->dev;
	struct device_node *node;
	struct irq_data *d;
	u32 cpu;
	int err;

	node = of_find_compatible_node(NULL, NULL, "arm,cortex-a9-gic");
	if (!node) {
		printk(KERN_ERR "failed to find imx6q-a9-gic device tree data!\n");
		return -EINVAL;
	}

	gic_dist_base = of_iomap(node, 0);
	WARN(!gic_dist_base, "unable to map gic dist registers\n");

	irqs_used = devm_kzalloc(dev, sizeof(u32) * num_present_cpus(),
			GFP_KERNEL);

	for_each_online_cpu(cpu) {
		int irq = platform_get_irq(busfreq_pdev, cpu);
		err = request_irq(irq, wait_in_wfe_irq, IRQF_PERCPU,
				"mmdc_1", NULL);
		if (err) {
			dev_err(dev,
				"Busfreq:request_irq failed %d, err = %d\n",
				irq, err);
			return err;
		}
		err = irq_set_affinity(irq, cpumask_of(cpu));
		if (err) {
			dev_err(dev,
				"Busfreq: Cannot set irq affinity irq=%d,\n",
				irq);
			return err;
		}
		d = irq_get_irq_data(irq);
		irqs_used[cpu] = d->hwirq + 32;
	}

	/* Stoange_iram_basee the variable used to communicate between cores in
	 * a non-cacheable IRAM area */
	wait_for_lpddr2_freq_update = (u32 *)ddr_freq_change_iram_base;
	wfe_change_lpddr2_freq = (void *)fncpy((void *)ddr_freq_change_iram_base + 0x8,
			&wfe_smp_freq_change, SMP_WFE_CODE_SIZE - 0x8);
#endif
	ddr_code_size = (&mx6q_lpddr2_freq_change_end -&mx6q_lpddr2_freq_change_start) *4;

	mx6_change_lpddr2_freq = (void *)fncpy(
			(void *)ddr_freq_change_iram_base + SMP_WFE_CODE_SIZE,
			&mx6q_lpddr2_freq_change, ddr_code_size);

	curr_ddr_rate = ddr_normal_rate;

	return 0;
}
