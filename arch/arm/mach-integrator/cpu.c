/*
 *  linux/arch/arm/mach-integrator/cpu.c
 *
 *  Copyright (C) 2001-2002 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * CPU support functions
 */
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/platform.h>
#include <asm/mach-types.h>
#include <asm/hardware/icst.h>

static struct cpufreq_driver integrator_driver;

#define CM_ID  	IO_ADDRESS(INTEGRATOR_HDR_ID)
#define CM_OSC	IO_ADDRESS(INTEGRATOR_HDR_OSC)
#define CM_STAT IO_ADDRESS(INTEGRATOR_HDR_STAT)
#define CM_LOCK IO_ADDRESS(INTEGRATOR_HDR_LOCK)

static const struct icst_params lclk_params = {
	.ref		= 24000000,
	.vco_max	= ICST525_VCO_MAX_5V,
	.vco_min	= ICST525_VCO_MIN,
	.vd_min		= 8,
	.vd_max		= 132,
	.rd_min		= 24,
	.rd_max		= 24,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static const struct icst_params cclk_params = {
	.ref		= 24000000,
	.vco_max	= ICST525_VCO_MAX_5V,
	.vco_min	= ICST525_VCO_MIN,
	.vd_min		= 12,
	.vd_max		= 160,
	.rd_min		= 24,
	.rd_max		= 24,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

/*
 * Validate the speed policy.
 */
static int integrator_verify_policy(struct cpufreq_policy *policy)
{
	struct icst_vco vco;

	cpufreq_verify_within_limits(policy, 
				     policy->cpuinfo.min_freq, 
				     policy->cpuinfo.max_freq);

	vco = icst_hz_to_vco(&cclk_params, policy->max * 1000);
	policy->max = icst_hz(&cclk_params, vco) / 1000;

	vco = icst_hz_to_vco(&cclk_params, policy->min * 1000);
	policy->min = icst_hz(&cclk_params, vco) / 1000;

	cpufreq_verify_within_limits(policy, 
				     policy->cpuinfo.min_freq, 
				     policy->cpuinfo.max_freq);

	return 0;
}


static int integrator_set_target(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation)
{
	cpumask_t cpus_allowed;
	int cpu = policy->cpu;
	struct icst_vco vco;
	struct cpufreq_freqs freqs;
	u_int cm_osc;

	/*
	 * Save this threads cpus_allowed mask.
	 */
	cpus_allowed = current->cpus_allowed;

	/*
	 * Bind to the specified CPU.  When this call returns,
	 * we should be running on the right CPU.
	 */
	set_cpus_allowed(current, cpumask_of_cpu(cpu));
	BUG_ON(cpu != smp_processor_id());

	/* get current setting */
	cm_osc = __raw_readl(CM_OSC);

	if (machine_is_integrator()) {
		vco.s = (cm_osc >> 8) & 7;
	} else if (machine_is_cintegrator()) {
		vco.s = 1;
	}
	vco.v = cm_osc & 255;
	vco.r = 22;
	freqs.old = icst_hz(&cclk_params, vco) / 1000;

	/* icst_hz_to_vco rounds down -- so we need the next
	 * larger freq in case of CPUFREQ_RELATION_L.
	 */
	if (relation == CPUFREQ_RELATION_L)
		target_freq += 999;
	if (target_freq > policy->max)
		target_freq = policy->max;
	vco = icst_hz_to_vco(&cclk_params, target_freq * 1000);
	freqs.new = icst_hz(&cclk_params, vco) / 1000;

	freqs.cpu = policy->cpu;

	if (freqs.old == freqs.new) {
		set_cpus_allowed(current, cpus_allowed);
		return 0;
	}

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	cm_osc = __raw_readl(CM_OSC);

	if (machine_is_integrator()) {
		cm_osc &= 0xfffff800;
		cm_osc |= vco.s << 8;
	} else if (machine_is_cintegrator()) {
		cm_osc &= 0xffffff00;
	}
	cm_osc |= vco.v;

	__raw_writel(0xa05f, CM_LOCK);
	__raw_writel(cm_osc, CM_OSC);
	__raw_writel(0, CM_LOCK);

	/*
	 * Restore the CPUs allowed mask.
	 */
	set_cpus_allowed(current, cpus_allowed);

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

static unsigned int integrator_get(unsigned int cpu)
{
	cpumask_t cpus_allowed;
	unsigned int current_freq;
	u_int cm_osc;
	struct icst_vco vco;

	cpus_allowed = current->cpus_allowed;

	set_cpus_allowed(current, cpumask_of_cpu(cpu));
	BUG_ON(cpu != smp_processor_id());

	/* detect memory etc. */
	cm_osc = __raw_readl(CM_OSC);

	if (machine_is_integrator()) {
		vco.s = (cm_osc >> 8) & 7;
	} else if (machine_is_cintegrator()) {
		vco.s = 1;
	}
	vco.v = cm_osc & 255;
	vco.r = 22;

	current_freq = icst_hz(&cclk_params, vco) / 1000; /* current freq */

	set_cpus_allowed(current, cpus_allowed);

	return current_freq;
}

static int integrator_cpufreq_init(struct cpufreq_policy *policy)
{

	/* set default policy and cpuinfo */
	policy->cpuinfo.max_freq = 160000;
	policy->cpuinfo.min_freq = 12000;
	policy->cpuinfo.transition_latency = 1000000; /* 1 ms, assumed */
	policy->cur = policy->min = policy->max = integrator_get(policy->cpu);

	return 0;
}

static struct cpufreq_driver integrator_driver = {
	.verify		= integrator_verify_policy,
	.target		= integrator_set_target,
	.get		= integrator_get,
	.init		= integrator_cpufreq_init,
	.name		= "integrator",
};

static int __init integrator_cpu_init(void)
{
	return cpufreq_register_driver(&integrator_driver);
}

static void __exit integrator_cpu_exit(void)
{
	cpufreq_unregister_driver(&integrator_driver);
}

MODULE_AUTHOR ("Russell M. King");
MODULE_DESCRIPTION ("cpufreq driver for ARM Integrator CPUs");
MODULE_LICENSE ("GPL");

module_init(integrator_cpu_init);
module_exit(integrator_cpu_exit);
