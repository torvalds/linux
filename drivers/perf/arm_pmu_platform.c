// SPDX-License-Identifier: GPL-2.0
/*
 * platform_device probing code for ARM performance counters.
 *
 * Copyright (C) 2009 picoChip Designs, Ltd., Jamie Iles
 * Copyright (C) 2010 ARM Ltd., Will Deacon <will.deacon@arm.com>
 */
#define pr_fmt(fmt) "hw perfevents: " fmt
#define dev_fmt pr_fmt

#include <linux/bug.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/irqdesc.h>
#include <linux/kconfig.h>
#include <linux/of.h>
#include <linux/percpu.h>
#include <linux/perf/arm_pmu.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/smp.h>

static int probe_current_pmu(struct arm_pmu *pmu,
			     const struct pmu_probe_info *info)
{
	int cpu = get_cpu();
	unsigned int cpuid = read_cpuid_id();
	int ret = -ENODEV;

	pr_info("probing PMU on CPU %d\n", cpu);

	for (; info->init != NULL; info++) {
		if ((cpuid & info->mask) != info->cpuid)
			continue;
		ret = info->init(pmu);
		break;
	}

	put_cpu();
	return ret;
}

static int pmu_parse_percpu_irq(struct arm_pmu *pmu, int irq)
{
	int cpu, ret;
	struct pmu_hw_events __percpu *hw_events = pmu->hw_events;

	ret = irq_get_percpu_devid_partition(irq, &pmu->supported_cpus);
	if (ret)
		return ret;

	for_each_cpu(cpu, &pmu->supported_cpus)
		per_cpu(hw_events->irq, cpu) = irq;

	return 0;
}

static bool pmu_has_irq_affinity(struct device_node *node)
{
	return !!of_find_property(node, "interrupt-affinity", NULL);
}

static int pmu_parse_irq_affinity(struct device *dev, int i)
{
	struct device_node *dn;
	int cpu;

	/*
	 * If we don't have an interrupt-affinity property, we guess irq
	 * affinity matches our logical CPU order, as we used to assume.
	 * This is fragile, so we'll warn in pmu_parse_irqs().
	 */
	if (!pmu_has_irq_affinity(dev->of_node))
		return i;

	dn = of_parse_phandle(dev->of_node, "interrupt-affinity", i);
	if (!dn) {
		dev_warn(dev, "failed to parse interrupt-affinity[%d]\n", i);
		return -EINVAL;
	}

	cpu = of_cpu_node_to_id(dn);
	if (cpu < 0) {
		dev_warn(dev, "failed to find logical CPU for %pOFn\n", dn);
		cpu = nr_cpu_ids;
	}

	of_node_put(dn);

	return cpu;
}

static int pmu_parse_irqs(struct arm_pmu *pmu)
{
	int i = 0, num_irqs;
	struct platform_device *pdev = pmu->plat_device;
	struct pmu_hw_events __percpu *hw_events = pmu->hw_events;
	struct device *dev = &pdev->dev;

	num_irqs = platform_irq_count(pdev);
	if (num_irqs < 0)
		return dev_err_probe(dev, num_irqs, "unable to count PMU IRQs\n");

	/*
	 * In this case we have no idea which CPUs are covered by the PMU.
	 * To match our prior behaviour, we assume all CPUs in this case.
	 */
	if (num_irqs == 0) {
		dev_warn(dev, "no irqs for PMU, sampling events not supported\n");
		pmu->pmu.capabilities |= PERF_PMU_CAP_NO_INTERRUPT;
		cpumask_setall(&pmu->supported_cpus);
		return 0;
	}

	if (num_irqs == 1) {
		int irq = platform_get_irq(pdev, 0);
		if ((irq > 0) && irq_is_percpu_devid(irq))
			return pmu_parse_percpu_irq(pmu, irq);
	}

	if (nr_cpu_ids != 1 && !pmu_has_irq_affinity(dev->of_node))
		dev_warn(dev, "no interrupt-affinity property, guessing.\n");

	for (i = 0; i < num_irqs; i++) {
		int cpu, irq;

		irq = platform_get_irq(pdev, i);
		if (WARN_ON(irq <= 0))
			continue;

		if (irq_is_percpu_devid(irq)) {
			dev_warn(dev, "multiple PPIs or mismatched SPI/PPI detected\n");
			return -EINVAL;
		}

		cpu = pmu_parse_irq_affinity(dev, i);
		if (cpu < 0)
			return cpu;
		if (cpu >= nr_cpu_ids)
			continue;

		if (per_cpu(hw_events->irq, cpu)) {
			dev_warn(dev, "multiple PMU IRQs for the same CPU detected\n");
			return -EINVAL;
		}

		per_cpu(hw_events->irq, cpu) = irq;
		cpumask_set_cpu(cpu, &pmu->supported_cpus);
	}

	return 0;
}

static int armpmu_request_irqs(struct arm_pmu *armpmu)
{
	struct pmu_hw_events __percpu *hw_events = armpmu->hw_events;
	int cpu, err = 0;

	for_each_cpu(cpu, &armpmu->supported_cpus) {
		int irq = per_cpu(hw_events->irq, cpu);
		if (!irq)
			continue;

		err = armpmu_request_irq(irq, cpu);
		if (err)
			break;
	}

	return err;
}

static void armpmu_free_irqs(struct arm_pmu *armpmu)
{
	int cpu;
	struct pmu_hw_events __percpu *hw_events = armpmu->hw_events;

	for_each_cpu(cpu, &armpmu->supported_cpus) {
		int irq = per_cpu(hw_events->irq, cpu);

		armpmu_free_irq(irq, cpu);
	}
}

int arm_pmu_device_probe(struct platform_device *pdev,
			 const struct of_device_id *of_table,
			 const struct pmu_probe_info *probe_table)
{
	armpmu_init_fn init_fn;
	struct device *dev = &pdev->dev;
	struct arm_pmu *pmu;
	int ret = -ENODEV;

	pmu = armpmu_alloc();
	if (!pmu)
		return -ENOMEM;

	pmu->pmu.parent = &pdev->dev;
	pmu->plat_device = pdev;

	ret = pmu_parse_irqs(pmu);
	if (ret)
		goto out_free;

	init_fn = of_device_get_match_data(dev);
	if (init_fn) {
		pmu->secure_access = of_property_read_bool(dev->of_node,
							   "secure-reg-access");

		/* arm64 systems boot only as non-secure */
		if (IS_ENABLED(CONFIG_ARM64) && pmu->secure_access) {
			dev_warn(dev, "ignoring \"secure-reg-access\" property for arm64\n");
			pmu->secure_access = false;
		}

		ret = init_fn(pmu);
	} else if (probe_table) {
		cpumask_setall(&pmu->supported_cpus);
		ret = probe_current_pmu(pmu, probe_table);
	}

	if (ret) {
		dev_err(dev, "failed to probe PMU!\n");
		goto out_free;
	}

	ret = armpmu_request_irqs(pmu);
	if (ret)
		goto out_free_irqs;

	ret = armpmu_register(pmu);
	if (ret) {
		dev_err(dev, "failed to register PMU devices!\n");
		goto out_free_irqs;
	}

	return 0;

out_free_irqs:
	armpmu_free_irqs(pmu);
out_free:
	armpmu_free(pmu);
	return ret;
}
