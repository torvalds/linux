/*
 * Copyright (C) 2007 PA Semi, Inc
 *
 * Authors: Egor Martovetsky <egor@pasemi.com>
 *	    Olof Johansson <olof@lixom.net>
 *
 * Maintained by: Olof Johansson <olof@lixom.net>
 *
 * Based on arch/powerpc/platforms/cell/cbe_cpufreq.c:
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/cpufreq.h>
#include <linux/timer.h>

#include <asm/hw_irq.h>
#include <asm/io.h>
#include <asm/prom.h>

#define SDCASR_REG		0x0100
#define SDCASR_REG_STRIDE	0x1000
#define SDCPWR_CFGA0_REG	0x0100
#define SDCPWR_PWST0_REG	0x0000
#define SDCPWR_GIZTIME_REG	0x0440

/* SDCPWR_GIZTIME_REG fields */
#define SDCPWR_GIZTIME_GR	0x80000000
#define SDCPWR_GIZTIME_LONGLOCK	0x000000ff

/* Offset of ASR registers from SDC base */
#define SDCASR_OFFSET		0x120000

static void __iomem *sdcpwr_mapbase;
static void __iomem *sdcasr_mapbase;

static DEFINE_MUTEX(pas_switch_mutex);

/* Current astate, is used when waking up from power savings on
 * one core, in case the other core has switched states during
 * the idle time.
 */
static int current_astate;

/* We support 5(A0-A4) power states excluding turbo(A5-A6) modes */
static struct cpufreq_frequency_table pas_freqs[] = {
	{0,	0},
	{1,	0},
	{2,	0},
	{3,	0},
	{4,	0},
	{0,	CPUFREQ_TABLE_END},
};

static struct freq_attr *pas_cpu_freqs_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

/*
 * hardware specific functions
 */

static int get_astate_freq(int astate)
{
	u32 ret;
	ret = in_le32(sdcpwr_mapbase + SDCPWR_CFGA0_REG + (astate * 0x10));

	return ret & 0x3f;
}

static int get_cur_astate(int cpu)
{
	u32 ret;

	ret = in_le32(sdcpwr_mapbase + SDCPWR_PWST0_REG);
	ret = (ret >> (cpu * 4)) & 0x7;

	return ret;
}

static int get_gizmo_latency(void)
{
	u32 giztime, ret;

	giztime = in_le32(sdcpwr_mapbase + SDCPWR_GIZTIME_REG);

	/* just provide the upper bound */
	if (giztime & SDCPWR_GIZTIME_GR)
		ret = (giztime & SDCPWR_GIZTIME_LONGLOCK) * 128000;
	else
		ret = (giztime & SDCPWR_GIZTIME_LONGLOCK) * 1000;

	return ret;
}

static void set_astate(int cpu, unsigned int astate)
{
	u64 flags;

	/* Return if called before init has run */
	if (unlikely(!sdcasr_mapbase))
		return;

	local_irq_save(flags);

	out_le32(sdcasr_mapbase + SDCASR_REG + SDCASR_REG_STRIDE*cpu, astate);

	local_irq_restore(flags);
}

void restore_astate(int cpu)
{
	set_astate(cpu, current_astate);
}

/*
 * cpufreq functions
 */

static int pas_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	const u32 *max_freqp;
	u32 max_freq;
	int i, cur_astate;
	struct resource res;
	struct device_node *cpu, *dn;
	int err = -ENODEV;

	cpu = of_get_cpu_node(policy->cpu, NULL);

	if (!cpu)
		goto out;

	dn = of_find_compatible_node(NULL, "sdc", "1682m-sdc");
	if (!dn)
		goto out;
	err = of_address_to_resource(dn, 0, &res);
	of_node_put(dn);
	if (err)
		goto out;
	sdcasr_mapbase = ioremap(res.start + SDCASR_OFFSET, 0x2000);
	if (!sdcasr_mapbase) {
		err = -EINVAL;
		goto out;
	}

	dn = of_find_compatible_node(NULL, "gizmo", "1682m-gizmo");
	if (!dn) {
		err = -ENODEV;
		goto out_unmap_sdcasr;
	}
	err = of_address_to_resource(dn, 0, &res);
	of_node_put(dn);
	if (err)
		goto out_unmap_sdcasr;
	sdcpwr_mapbase = ioremap(res.start, 0x1000);
	if (!sdcpwr_mapbase) {
		err = -EINVAL;
		goto out_unmap_sdcasr;
	}

	pr_debug("init cpufreq on CPU %d\n", policy->cpu);

	max_freqp = of_get_property(cpu, "clock-frequency", NULL);
	if (!max_freqp) {
		err = -EINVAL;
		goto out_unmap_sdcpwr;
	}

	/* we need the freq in kHz */
	max_freq = *max_freqp / 1000;

	pr_debug("max clock-frequency is at %u kHz\n", max_freq);
	pr_debug("initializing frequency table\n");

	/* initialize frequency table */
	for (i=0; pas_freqs[i].frequency!=CPUFREQ_TABLE_END; i++) {
		pas_freqs[i].frequency = get_astate_freq(pas_freqs[i].index) * 100000;
		pr_debug("%d: %d\n", i, pas_freqs[i].frequency);
	}

	policy->governor = CPUFREQ_DEFAULT_GOVERNOR;

	policy->cpuinfo.transition_latency = get_gizmo_latency();

	cur_astate = get_cur_astate(policy->cpu);
	pr_debug("current astate is at %d\n",cur_astate);

	policy->cur = pas_freqs[cur_astate].frequency;
	policy->cpus = cpu_online_map;

	cpufreq_frequency_table_get_attr(pas_freqs, policy->cpu);

	/* this ensures that policy->cpuinfo_min and policy->cpuinfo_max
	 * are set correctly
	 */
	return cpufreq_frequency_table_cpuinfo(policy, pas_freqs);

out_unmap_sdcpwr:
	iounmap(sdcpwr_mapbase);

out_unmap_sdcasr:
	iounmap(sdcasr_mapbase);
out:
	return err;
}

static int pas_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	if (sdcasr_mapbase)
		iounmap(sdcasr_mapbase);
	if (sdcpwr_mapbase)
		iounmap(sdcpwr_mapbase);

	cpufreq_frequency_table_put_attr(policy->cpu);
	return 0;
}

static int pas_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, pas_freqs);
}

static int pas_cpufreq_target(struct cpufreq_policy *policy,
			      unsigned int target_freq,
			      unsigned int relation)
{
	struct cpufreq_freqs freqs;
	int pas_astate_new;
	int i;

	cpufreq_frequency_table_target(policy,
				       pas_freqs,
				       target_freq,
				       relation,
				       &pas_astate_new);

	freqs.old = policy->cur;
	freqs.new = pas_freqs[pas_astate_new].frequency;
	freqs.cpu = policy->cpu;

	mutex_lock(&pas_switch_mutex);
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	pr_debug("setting frequency for cpu %d to %d kHz, 1/%d of max frequency\n",
		 policy->cpu,
		 pas_freqs[pas_astate_new].frequency,
		 pas_freqs[pas_astate_new].index);

	current_astate = pas_astate_new;

	for_each_online_cpu(i)
		set_astate(i, pas_astate_new);

	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
	mutex_unlock(&pas_switch_mutex);

	return 0;
}

static struct cpufreq_driver pas_cpufreq_driver = {
	.name		= "pas-cpufreq",
	.owner		= THIS_MODULE,
	.flags		= CPUFREQ_CONST_LOOPS,
	.init		= pas_cpufreq_cpu_init,
	.exit		= pas_cpufreq_cpu_exit,
	.verify		= pas_cpufreq_verify,
	.target		= pas_cpufreq_target,
	.attr		= pas_cpu_freqs_attr,
};

/*
 * module init and destoy
 */

static int __init pas_cpufreq_init(void)
{
	if (!machine_is_compatible("PA6T-1682M"))
		return -ENODEV;

	return cpufreq_register_driver(&pas_cpufreq_driver);
}

static void __exit pas_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&pas_cpufreq_driver);
}

module_init(pas_cpufreq_init);
module_exit(pas_cpufreq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Egor Martovetsky <egor@pasemi.com>, Olof Johansson <olof@lixom.net>");
