// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2014 Linaro Ltd.
 * Copyright (c) 2013-2014 Hisilicon Limited.
 */
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/memblock.h>
#include <linux/of_address.h>

#include <asm/cputype.h>
#include <asm/cp15.h>
#include <asm/cacheflush.h>
#include <asm/smp.h>
#include <asm/smp_plat.h>

#include "core.h"

/* bits definition in SC_CPU_RESET_REQ[x]/SC_CPU_RESET_DREQ[x]
 * 1 -- unreset; 0 -- reset
 */
#define CORE_RESET_BIT(x)		(1 << x)
#define NEON_RESET_BIT(x)		(1 << (x + 4))
#define CORE_DEBUG_RESET_BIT(x)		(1 << (x + 9))
#define CLUSTER_L2_RESET_BIT		(1 << 8)
#define CLUSTER_DEBUG_RESET_BIT		(1 << 13)

/*
 * bits definition in SC_CPU_RESET_STATUS[x]
 * 1 -- reset status; 0 -- unreset status
 */
#define CORE_RESET_STATUS(x)		(1 << x)
#define NEON_RESET_STATUS(x)		(1 << (x + 4))
#define CORE_DEBUG_RESET_STATUS(x)	(1 << (x + 9))
#define CLUSTER_L2_RESET_STATUS		(1 << 8)
#define CLUSTER_DEBUG_RESET_STATUS	(1 << 13)
#define CORE_WFI_STATUS(x)		(1 << (x + 16))
#define CORE_WFE_STATUS(x)		(1 << (x + 20))
#define CORE_DEBUG_ACK(x)		(1 << (x + 24))

#define SC_CPU_RESET_REQ(x)		(0x520 + (x << 3))	/* reset */
#define SC_CPU_RESET_DREQ(x)		(0x524 + (x << 3))	/* unreset */
#define SC_CPU_RESET_STATUS(x)		(0x1520 + (x << 3))

#define FAB_SF_MODE			0x0c
#define FAB_SF_INVLD			0x10

/* bits definition in FB_SF_INVLD */
#define FB_SF_INVLD_START		(1 << 8)

#define HIP04_MAX_CLUSTERS		4
#define HIP04_MAX_CPUS_PER_CLUSTER	4

#define POLL_MSEC	10
#define TIMEOUT_MSEC	1000

static void __iomem *sysctrl, *fabric;
static int hip04_cpu_table[HIP04_MAX_CLUSTERS][HIP04_MAX_CPUS_PER_CLUSTER];
static DEFINE_SPINLOCK(boot_lock);
static u32 fabric_phys_addr;
/*
 * [0]: bootwrapper physical address
 * [1]: bootwrapper size
 * [2]: relocation address
 * [3]: relocation size
 */
static u32 hip04_boot_method[4];

static bool hip04_cluster_is_down(unsigned int cluster)
{
	int i;

	for (i = 0; i < HIP04_MAX_CPUS_PER_CLUSTER; i++)
		if (hip04_cpu_table[cluster][i])
			return false;
	return true;
}

static void hip04_set_snoop_filter(unsigned int cluster, unsigned int on)
{
	unsigned long data;

	if (!fabric)
		BUG();
	data = readl_relaxed(fabric + FAB_SF_MODE);
	if (on)
		data |= 1 << cluster;
	else
		data &= ~(1 << cluster);
	writel_relaxed(data, fabric + FAB_SF_MODE);
	do {
		cpu_relax();
	} while (data != readl_relaxed(fabric + FAB_SF_MODE));
}

static int hip04_boot_secondary(unsigned int l_cpu, struct task_struct *idle)
{
	unsigned int mpidr, cpu, cluster;
	unsigned long data;
	void __iomem *sys_dreq, *sys_status;

	mpidr = cpu_logical_map(l_cpu);
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	if (!sysctrl)
		return -ENODEV;
	if (cluster >= HIP04_MAX_CLUSTERS || cpu >= HIP04_MAX_CPUS_PER_CLUSTER)
		return -EINVAL;

	spin_lock_irq(&boot_lock);

	if (hip04_cpu_table[cluster][cpu])
		goto out;

	sys_dreq = sysctrl + SC_CPU_RESET_DREQ(cluster);
	sys_status = sysctrl + SC_CPU_RESET_STATUS(cluster);
	if (hip04_cluster_is_down(cluster)) {
		data = CLUSTER_DEBUG_RESET_BIT;
		writel_relaxed(data, sys_dreq);
		do {
			cpu_relax();
			data = readl_relaxed(sys_status);
		} while (data & CLUSTER_DEBUG_RESET_STATUS);
		hip04_set_snoop_filter(cluster, 1);
	}

	data = CORE_RESET_BIT(cpu) | NEON_RESET_BIT(cpu) | \
	       CORE_DEBUG_RESET_BIT(cpu);
	writel_relaxed(data, sys_dreq);
	do {
		cpu_relax();
	} while (data == readl_relaxed(sys_status));

	/*
	 * We may fail to power up core again without this delay.
	 * It's not mentioned in document. It's found by test.
	 */
	udelay(20);

	arch_send_wakeup_ipi_mask(cpumask_of(l_cpu));

out:
	hip04_cpu_table[cluster][cpu]++;
	spin_unlock_irq(&boot_lock);

	return 0;
}

#ifdef CONFIG_HOTPLUG_CPU
static void hip04_cpu_die(unsigned int l_cpu)
{
	unsigned int mpidr, cpu, cluster;
	bool last_man;

	mpidr = cpu_logical_map(l_cpu);
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	spin_lock(&boot_lock);
	hip04_cpu_table[cluster][cpu]--;
	if (hip04_cpu_table[cluster][cpu] == 1) {
		/* A power_up request went ahead of us. */
		spin_unlock(&boot_lock);
		return;
	} else if (hip04_cpu_table[cluster][cpu] > 1) {
		pr_err("Cluster %d CPU%d boots multiple times\n", cluster, cpu);
		BUG();
	}

	last_man = hip04_cluster_is_down(cluster);
	spin_unlock(&boot_lock);
	if (last_man) {
		/* Since it's Cortex A15, disable L2 prefetching. */
		asm volatile(
		"mcr	p15, 1, %0, c15, c0, 3 \n\t"
		"isb	\n\t"
		"dsb	"
		: : "r" (0x400) );
		v7_exit_coherency_flush(all);
	} else {
		v7_exit_coherency_flush(louis);
	}

	for (;;)
		wfi();
}

static int hip04_cpu_kill(unsigned int l_cpu)
{
	unsigned int mpidr, cpu, cluster;
	unsigned int data, tries, count;

	mpidr = cpu_logical_map(l_cpu);
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);
	BUG_ON(cluster >= HIP04_MAX_CLUSTERS ||
	       cpu >= HIP04_MAX_CPUS_PER_CLUSTER);

	count = TIMEOUT_MSEC / POLL_MSEC;
	spin_lock_irq(&boot_lock);
	for (tries = 0; tries < count; tries++) {
		if (hip04_cpu_table[cluster][cpu])
			goto err;
		cpu_relax();
		data = readl_relaxed(sysctrl + SC_CPU_RESET_STATUS(cluster));
		if (data & CORE_WFI_STATUS(cpu))
			break;
		spin_unlock_irq(&boot_lock);
		/* Wait for clean L2 when the whole cluster is down. */
		msleep(POLL_MSEC);
		spin_lock_irq(&boot_lock);
	}
	if (tries >= count)
		goto err;
	data = CORE_RESET_BIT(cpu) | NEON_RESET_BIT(cpu) | \
	       CORE_DEBUG_RESET_BIT(cpu);
	writel_relaxed(data, sysctrl + SC_CPU_RESET_REQ(cluster));
	for (tries = 0; tries < count; tries++) {
		cpu_relax();
		data = readl_relaxed(sysctrl + SC_CPU_RESET_STATUS(cluster));
		if (data & CORE_RESET_STATUS(cpu))
			break;
	}
	if (tries >= count)
		goto err;
	if (hip04_cluster_is_down(cluster))
		hip04_set_snoop_filter(cluster, 0);
	spin_unlock_irq(&boot_lock);
	return 1;
err:
	spin_unlock_irq(&boot_lock);
	return 0;
}
#endif

static const struct smp_operations hip04_smp_ops __initconst = {
	.smp_boot_secondary	= hip04_boot_secondary,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_die		= hip04_cpu_die,
	.cpu_kill		= hip04_cpu_kill,
#endif
};

static bool __init hip04_cpu_table_init(void)
{
	unsigned int mpidr, cpu, cluster;

	mpidr = read_cpuid_mpidr();
	cpu = MPIDR_AFFINITY_LEVEL(mpidr, 0);
	cluster = MPIDR_AFFINITY_LEVEL(mpidr, 1);

	if (cluster >= HIP04_MAX_CLUSTERS ||
	    cpu >= HIP04_MAX_CPUS_PER_CLUSTER) {
		pr_err("%s: boot CPU is out of bound!\n", __func__);
		return false;
	}
	hip04_set_snoop_filter(cluster, 1);
	hip04_cpu_table[cluster][cpu] = 1;
	return true;
}

static int __init hip04_smp_init(void)
{
	struct device_node *np, *np_sctl, *np_fab;
	struct resource fab_res;
	void __iomem *relocation;
	int ret = -ENODEV;

	np = of_find_compatible_node(NULL, NULL, "hisilicon,hip04-bootwrapper");
	if (!np)
		goto err;
	ret = of_property_read_u32_array(np, "boot-method",
					 &hip04_boot_method[0], 4);
	if (ret)
		goto err;

	ret = -ENODEV;
	np_sctl = of_find_compatible_node(NULL, NULL, "hisilicon,sysctrl");
	if (!np_sctl)
		goto err;
	np_fab = of_find_compatible_node(NULL, NULL, "hisilicon,hip04-fabric");
	if (!np_fab)
		goto err;

	ret = memblock_reserve(hip04_boot_method[0], hip04_boot_method[1]);
	if (ret)
		goto err;

	relocation = ioremap(hip04_boot_method[2], hip04_boot_method[3]);
	if (!relocation) {
		pr_err("failed to map relocation space\n");
		ret = -ENOMEM;
		goto err_reloc;
	}
	sysctrl = of_iomap(np_sctl, 0);
	if (!sysctrl) {
		pr_err("failed to get sysctrl base\n");
		ret = -ENOMEM;
		goto err_sysctrl;
	}
	ret = of_address_to_resource(np_fab, 0, &fab_res);
	if (ret) {
		pr_err("failed to get fabric base phys\n");
		goto err_fabric;
	}
	fabric_phys_addr = fab_res.start;
	sync_cache_w(&fabric_phys_addr);
	fabric = of_iomap(np_fab, 0);
	if (!fabric) {
		pr_err("failed to get fabric base\n");
		ret = -ENOMEM;
		goto err_fabric;
	}

	if (!hip04_cpu_table_init()) {
		ret = -EINVAL;
		goto err_table;
	}

	/*
	 * Fill the instruction address that is used after secondary core
	 * out of reset.
	 */
	writel_relaxed(hip04_boot_method[0], relocation);
	writel_relaxed(0xa5a5a5a5, relocation + 4);	/* magic number */
	writel_relaxed(__pa_symbol(secondary_startup), relocation + 8);
	writel_relaxed(0, relocation + 12);
	iounmap(relocation);

	smp_set_ops(&hip04_smp_ops);
	return ret;
err_table:
	iounmap(fabric);
err_fabric:
	iounmap(sysctrl);
err_sysctrl:
	iounmap(relocation);
err_reloc:
	memblock_free(hip04_boot_method[0], hip04_boot_method[1]);
err:
	return ret;
}
early_initcall(hip04_smp_init);
