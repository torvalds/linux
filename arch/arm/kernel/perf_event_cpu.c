/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */
#define pr_fmt(fmt) "CPU PMU: " fmt

#include <linux/bitmap.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/cputype.h>
#include <asm/irq_regs.h>
#include <asm/pmu.h>

/* Set at runtime when we know what CPU type we are. */
static struct arm_pmu *cpu_pmu;

static DEFINE_PER_CPU(struct perf_event * [ARMPMU_MAX_HWEVENTS], hw_events);
static DEFINE_PER_CPU(unsigned long [BITS_TO_LONGS(ARMPMU_MAX_HWEVENTS)], used_mask);
static DEFINE_PER_CPU(struct pmu_hw_events, cpu_hw_events);

/*
 * Despite the names, these two functions are CPU-specific and are used
 * by the OProfile/perf code.
 */
const char *perf_pmu_name(void)
{
	if (!cpu_pmu)
		return NULL;

	return cpu_pmu->name;
}
EXPORT_SYMBOL_GPL(perf_pmu_name);

int perf_num_counters(void)
{
	int max_events = 0;

	if (cpu_pmu != NULL)
		max_events = cpu_pmu->num_events;

	return max_events;
}
EXPORT_SYMBOL_GPL(perf_num_counters);

/* Include the PMU-specific implementations. */
#include "perf_event_xscale.c"
#include "perf_event_v6.c"
#include "perf_event_v7.c"

static struct pmu_hw_events *cpu_pmu_get_cpu_events(void)
{
	return &__get_cpu_var(cpu_hw_events);
}

static void cpu_pmu_free_irq(struct arm_pmu *cpu_pmu)
{
	int i, irq, irqs;
	struct platform_device *pmu_device = cpu_pmu->plat_device;

	irqs = min(pmu_device->num_resources, num_possible_cpus());

	for (i = 0; i < irqs; ++i) {
		if (!cpumask_test_and_clear_cpu(i, &cpu_pmu->active_irqs))
			continue;
		irq = platform_get_irq(pmu_device, i);
		if (irq >= 0)
			free_irq(irq, cpu_pmu);
	}
}

static int cpu_pmu_request_irq(struct arm_pmu *cpu_pmu, irq_handler_t handler)
{
	int i, err, irq, irqs;
	struct platform_device *pmu_device = cpu_pmu->plat_device;

	if (!pmu_device)
		return -ENODEV;

	irqs = min(pmu_device->num_resources, num_possible_cpus());
	if (irqs < 1) {
		pr_err("no irqs for PMUs defined\n");
		return -ENODEV;
	}

	for (i = 0; i < irqs; ++i) {
		err = 0;
		irq = platform_get_irq(pmu_device, i);
		if (irq < 0)
			continue;

		/*
		 * If we have a single PMU interrupt that we can't shift,
		 * assume that we're running on a uniprocessor machine and
		 * continue. Otherwise, continue without this interrupt.
		 */
		if (irq_set_affinity(irq, cpumask_of(i)) && irqs > 1) {
			pr_warning("unable to set irq affinity (irq=%d, cpu=%u)\n",
				    irq, i);
			continue;
		}

		err = request_irq(irq, handler, IRQF_NOBALANCING, "arm-pmu",
				  cpu_pmu);
		if (err) {
			pr_err("unable to request IRQ%d for ARM PMU counters\n",
				irq);
			return err;
		}

		cpumask_set_cpu(i, &cpu_pmu->active_irqs);
	}

	return 0;
}

static void cpu_pmu_init(struct arm_pmu *cpu_pmu)
{
	int cpu;
	for_each_possible_cpu(cpu) {
		struct pmu_hw_events *events = &per_cpu(cpu_hw_events, cpu);
		events->events = per_cpu(hw_events, cpu);
		events->used_mask = per_cpu(used_mask, cpu);
		raw_spin_lock_init(&events->pmu_lock);
	}

	cpu_pmu->get_hw_events	= cpu_pmu_get_cpu_events;
	cpu_pmu->request_irq	= cpu_pmu_request_irq;
	cpu_pmu->free_irq	= cpu_pmu_free_irq;

	/* Ensure the PMU has sane values out of reset. */
	if (cpu_pmu->reset)
		on_each_cpu(cpu_pmu->reset, cpu_pmu, 1);
}

/*
 * PMU hardware loses all context when a CPU goes offline.
 * When a CPU is hotplugged back in, since some hardware registers are
 * UNKNOWN at reset, the PMU must be explicitly reset to avoid reading
 * junk values out of them.
 */
static int cpu_pmu_notify(struct notifier_block *b, unsigned long action,
			  void *hcpu)
{
	if ((action & ~CPU_TASKS_FROZEN) != CPU_STARTING)
		return NOTIFY_DONE;

	if (cpu_pmu && cpu_pmu->reset)
		cpu_pmu->reset(cpu_pmu);
	else
		return NOTIFY_DONE;

	return NOTIFY_OK;
}

static struct notifier_block cpu_pmu_hotplug_notifier = {
	.notifier_call = cpu_pmu_notify,
};

/*
 * PMU platform driver and devicetree bindings.
 */
static struct of_device_id cpu_pmu_of_device_ids[] = {
	{.compatible = "arm,cortex-a15-pmu",	.data = armv7_a15_pmu_init},
	{.compatible = "arm,cortex-a9-pmu",	.data = armv7_a9_pmu_init},
	{.compatible = "arm,cortex-a8-pmu",	.data = armv7_a8_pmu_init},
	{.compatible = "arm,cortex-a7-pmu",	.data = armv7_a7_pmu_init},
	{.compatible = "arm,cortex-a5-pmu",	.data = armv7_a5_pmu_init},
	{.compatible = "arm,arm11mpcore-pmu",	.data = armv6mpcore_pmu_init},
	{.compatible = "arm,arm1176-pmu",	.data = armv6pmu_init},
	{.compatible = "arm,arm1136-pmu",	.data = armv6pmu_init},
	{},
};

static struct platform_device_id cpu_pmu_plat_device_ids[] = {
	{.name = "arm-pmu"},
	{},
};

/*
 * CPU PMU identification and probing.
 */
static int probe_current_pmu(struct arm_pmu *pmu)
{
	int cpu = get_cpu();
	unsigned long implementor = read_cpuid_implementor();
	unsigned long part_number = read_cpuid_part_number();
	int ret = -ENODEV;

	pr_info("probing PMU on CPU %d\n", cpu);

	/* ARM Ltd CPUs. */
	if (implementor == ARM_CPU_IMP_ARM) {
		switch (part_number) {
		case ARM_CPU_PART_ARM1136:
		case ARM_CPU_PART_ARM1156:
		case ARM_CPU_PART_ARM1176:
			ret = armv6pmu_init(pmu);
			break;
		case ARM_CPU_PART_ARM11MPCORE:
			ret = armv6mpcore_pmu_init(pmu);
			break;
		case ARM_CPU_PART_CORTEX_A8:
			ret = armv7_a8_pmu_init(pmu);
			break;
		case ARM_CPU_PART_CORTEX_A9:
			ret = armv7_a9_pmu_init(pmu);
			break;
		case ARM_CPU_PART_CORTEX_A5:
			ret = armv7_a5_pmu_init(pmu);
			break;
		case ARM_CPU_PART_CORTEX_A15:
			ret = armv7_a15_pmu_init(pmu);
			break;
		case ARM_CPU_PART_CORTEX_A7:
			ret = armv7_a7_pmu_init(pmu);
			break;
		}
	/* Intel CPUs [xscale]. */
	} else if (implementor == ARM_CPU_IMP_INTEL) {
		switch (xscale_cpu_arch_version()) {
		case ARM_CPU_XSCALE_ARCH_V1:
			ret = xscale1pmu_init(pmu);
			break;
		case ARM_CPU_XSCALE_ARCH_V2:
			ret = xscale2pmu_init(pmu);
			break;
		}
	}

	put_cpu();
	return ret;
}

static int cpu_pmu_device_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	int (*init_fn)(struct arm_pmu *);
	struct device_node *node = pdev->dev.of_node;
	struct arm_pmu *pmu;
	int ret = -ENODEV;

	if (cpu_pmu) {
		pr_info("attempt to register multiple PMU devices!");
		return -ENOSPC;
	}

	pmu = kzalloc(sizeof(struct arm_pmu), GFP_KERNEL);
	if (!pmu) {
		pr_info("failed to allocate PMU device!");
		return -ENOMEM;
	}

	if (node && (of_id = of_match_node(cpu_pmu_of_device_ids, pdev->dev.of_node))) {
		init_fn = of_id->data;
		ret = init_fn(pmu);
	} else {
		ret = probe_current_pmu(pmu);
	}

	if (ret) {
		pr_info("failed to probe PMU!");
		goto out_free;
	}

	cpu_pmu = pmu;
	cpu_pmu->plat_device = pdev;
	cpu_pmu_init(cpu_pmu);
	ret = armpmu_register(cpu_pmu, PERF_TYPE_RAW);

	if (!ret)
		return 0;

out_free:
	pr_info("failed to register PMU devices!");
	kfree(pmu);
	return ret;
}

static struct platform_driver cpu_pmu_driver = {
	.driver		= {
		.name	= "arm-pmu",
		.pm	= &armpmu_dev_pm_ops,
		.of_match_table = cpu_pmu_of_device_ids,
	},
	.probe		= cpu_pmu_device_probe,
	.id_table	= cpu_pmu_plat_device_ids,
};

static int __init register_pmu_driver(void)
{
	int err;

	err = register_cpu_notifier(&cpu_pmu_hotplug_notifier);
	if (err)
		return err;

	err = platform_driver_register(&cpu_pmu_driver);
	if (err)
		unregister_cpu_notifier(&cpu_pmu_hotplug_notifier);

	return err;
}
device_initcall(register_pmu_driver);
