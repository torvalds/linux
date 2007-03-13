/*
 * Copyright (C) 2005-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/init.h>
#include <linux/sysdev.h>
#include <linux/seq_file.h>
#include <linux/cpu.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <linux/param.h>
#include <linux/errno.h>

#include <asm/setup.h>
#include <asm/sysreg.h>

static DEFINE_PER_CPU(struct cpu, cpu_devices);

#ifdef CONFIG_PERFORMANCE_COUNTERS

/*
 * XXX: If/when a SMP-capable implementation of AVR32 will ever be
 * made, we must make sure that the code executes on the correct CPU.
 */
static ssize_t show_pc0event(struct sys_device *dev, char *buf)
{
	unsigned long pccr;

	pccr = sysreg_read(PCCR);
	return sprintf(buf, "0x%lx\n", (pccr >> 12) & 0x3f);
}
static ssize_t store_pc0event(struct sys_device *dev, const char *buf,
			      size_t count)
{
	unsigned long val;
	char *endp;

	val = simple_strtoul(buf, &endp, 0);
	if (endp == buf || val > 0x3f)
		return -EINVAL;
	val = (val << 12) | (sysreg_read(PCCR) & 0xfffc0fff);
	sysreg_write(PCCR, val);
	return count;
}
static ssize_t show_pc0count(struct sys_device *dev, char *buf)
{
	unsigned long pcnt0;

	pcnt0 = sysreg_read(PCNT0);
	return sprintf(buf, "%lu\n", pcnt0);
}
static ssize_t store_pc0count(struct sys_device *dev, const char *buf,
			      size_t count)
{
	unsigned long val;
	char *endp;

	val = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EINVAL;
	sysreg_write(PCNT0, val);

	return count;
}

static ssize_t show_pc1event(struct sys_device *dev, char *buf)
{
	unsigned long pccr;

	pccr = sysreg_read(PCCR);
	return sprintf(buf, "0x%lx\n", (pccr >> 18) & 0x3f);
}
static ssize_t store_pc1event(struct sys_device *dev, const char *buf,
			      size_t count)
{
	unsigned long val;
	char *endp;

	val = simple_strtoul(buf, &endp, 0);
	if (endp == buf || val > 0x3f)
		return -EINVAL;
	val = (val << 18) | (sysreg_read(PCCR) & 0xff03ffff);
	sysreg_write(PCCR, val);
	return count;
}
static ssize_t show_pc1count(struct sys_device *dev, char *buf)
{
	unsigned long pcnt1;

	pcnt1 = sysreg_read(PCNT1);
	return sprintf(buf, "%lu\n", pcnt1);
}
static ssize_t store_pc1count(struct sys_device *dev, const char *buf,
			      size_t count)
{
	unsigned long val;
	char *endp;

	val = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EINVAL;
	sysreg_write(PCNT1, val);

	return count;
}

static ssize_t show_pccycles(struct sys_device *dev, char *buf)
{
	unsigned long pccnt;

	pccnt = sysreg_read(PCCNT);
	return sprintf(buf, "%lu\n", pccnt);
}
static ssize_t store_pccycles(struct sys_device *dev, const char *buf,
			      size_t count)
{
	unsigned long val;
	char *endp;

	val = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EINVAL;
	sysreg_write(PCCNT, val);

	return count;
}

static ssize_t show_pcenable(struct sys_device *dev, char *buf)
{
	unsigned long pccr;

	pccr = sysreg_read(PCCR);
	return sprintf(buf, "%c\n", (pccr & 1)?'1':'0');
}
static ssize_t store_pcenable(struct sys_device *dev, const char *buf,
			      size_t count)
{
	unsigned long pccr, val;
	char *endp;

	val = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EINVAL;
	if (val)
		val = 1;

	pccr = sysreg_read(PCCR);
	pccr = (pccr & ~1UL) | val;
	sysreg_write(PCCR, pccr);

	return count;
}

static SYSDEV_ATTR(pc0event, 0600, show_pc0event, store_pc0event);
static SYSDEV_ATTR(pc0count, 0600, show_pc0count, store_pc0count);
static SYSDEV_ATTR(pc1event, 0600, show_pc1event, store_pc1event);
static SYSDEV_ATTR(pc1count, 0600, show_pc1count, store_pc1count);
static SYSDEV_ATTR(pccycles, 0600, show_pccycles, store_pccycles);
static SYSDEV_ATTR(pcenable, 0600, show_pcenable, store_pcenable);

#endif /* CONFIG_PERFORMANCE_COUNTERS */

static int __init topology_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct cpu *c = &per_cpu(cpu_devices, cpu);

		register_cpu(c, cpu);

#ifdef CONFIG_PERFORMANCE_COUNTERS
		sysdev_create_file(&c->sysdev, &attr_pc0event);
		sysdev_create_file(&c->sysdev, &attr_pc0count);
		sysdev_create_file(&c->sysdev, &attr_pc1event);
		sysdev_create_file(&c->sysdev, &attr_pc1count);
		sysdev_create_file(&c->sysdev, &attr_pccycles);
		sysdev_create_file(&c->sysdev, &attr_pcenable);
#endif
	}

	return 0;
}

subsys_initcall(topology_init);

static const char *cpu_names[] = {
	"Morgan",
	"AP7000",
};
#define NR_CPU_NAMES ARRAY_SIZE(cpu_names)

static const char *arch_names[] = {
	"AVR32A",
	"AVR32B",
};
#define NR_ARCH_NAMES ARRAY_SIZE(arch_names)

static const char *mmu_types[] = {
	"No MMU",
	"ITLB and DTLB",
	"Shared TLB",
	"MPU"
};

void __init setup_processor(void)
{
	unsigned long config0, config1;
	unsigned long features;
	unsigned cpu_id, cpu_rev, arch_id, arch_rev, mmu_type;
	unsigned tmp;

	config0 = sysreg_read(CONFIG0);
	config1 = sysreg_read(CONFIG1);
	cpu_id = SYSREG_BFEXT(PROCESSORID, config0);
	cpu_rev = SYSREG_BFEXT(PROCESSORREVISION, config0);
	arch_id = SYSREG_BFEXT(AT, config0);
	arch_rev = SYSREG_BFEXT(AR, config0);
	mmu_type = SYSREG_BFEXT(MMUT, config0);

	boot_cpu_data.arch_type = arch_id;
	boot_cpu_data.cpu_type = cpu_id;
	boot_cpu_data.arch_revision = arch_rev;
	boot_cpu_data.cpu_revision = cpu_rev;
	boot_cpu_data.tlb_config = mmu_type;

	tmp = SYSREG_BFEXT(ILSZ, config1);
	if (tmp) {
		boot_cpu_data.icache.ways = 1 << SYSREG_BFEXT(IASS, config1);
		boot_cpu_data.icache.sets = 1 << SYSREG_BFEXT(ISET, config1);
		boot_cpu_data.icache.linesz = 1 << (tmp + 1);
	}
	tmp = SYSREG_BFEXT(DLSZ, config1);
	if (tmp) {
		boot_cpu_data.dcache.ways = 1 << SYSREG_BFEXT(DASS, config1);
		boot_cpu_data.dcache.sets = 1 << SYSREG_BFEXT(DSET, config1);
		boot_cpu_data.dcache.linesz = 1 << (tmp + 1);
	}

	if ((cpu_id >= NR_CPU_NAMES) || (arch_id >= NR_ARCH_NAMES)) {
		printk ("Unknown CPU configuration (ID %02x, arch %02x), "
			"continuing anyway...\n",
			cpu_id, arch_id);
		return;
	}

	printk ("CPU: %s [%02x] revision %d (%s revision %d)\n",
		cpu_names[cpu_id], cpu_id, cpu_rev,
		arch_names[arch_id], arch_rev);
	printk ("CPU: MMU configuration: %s\n", mmu_types[mmu_type]);

	printk ("CPU: features:");
	features = 0;
	if (config0 & SYSREG_BIT(CONFIG0_R)) {
		features |= AVR32_FEATURE_RMW;
		printk(" rmw");
	}
	if (config0 & SYSREG_BIT(CONFIG0_D)) {
		features |= AVR32_FEATURE_DSP;
		printk(" dsp");
	}
	if (config0 & SYSREG_BIT(CONFIG0_S)) {
		features |= AVR32_FEATURE_SIMD;
		printk(" simd");
	}
	if (config0 & SYSREG_BIT(CONFIG0_O)) {
		features |= AVR32_FEATURE_OCD;
		printk(" ocd");
	}
	if (config0 & SYSREG_BIT(CONFIG0_P)) {
		features |= AVR32_FEATURE_PCTR;
		printk(" perfctr");
	}
	if (config0 & SYSREG_BIT(CONFIG0_J)) {
		features |= AVR32_FEATURE_JAVA;
		printk(" java");
	}
	if (config0 & SYSREG_BIT(CONFIG0_F)) {
		features |= AVR32_FEATURE_FPU;
		printk(" fpu");
	}
	printk("\n");
	boot_cpu_data.features = features;
}

#ifdef CONFIG_PROC_FS
static int c_show(struct seq_file *m, void *v)
{
	unsigned int icache_size, dcache_size;
	unsigned int cpu = smp_processor_id();

	icache_size = boot_cpu_data.icache.ways *
		boot_cpu_data.icache.sets *
		boot_cpu_data.icache.linesz;
	dcache_size = boot_cpu_data.dcache.ways *
		boot_cpu_data.dcache.sets *
		boot_cpu_data.dcache.linesz;

	seq_printf(m, "processor\t: %d\n", cpu);

	if (boot_cpu_data.arch_type < NR_ARCH_NAMES)
		seq_printf(m, "cpu family\t: %s revision %d\n",
			   arch_names[boot_cpu_data.arch_type],
			   boot_cpu_data.arch_revision);
	if (boot_cpu_data.cpu_type < NR_CPU_NAMES)
		seq_printf(m, "cpu type\t: %s revision %d\n",
			   cpu_names[boot_cpu_data.cpu_type],
			   boot_cpu_data.cpu_revision);

	seq_printf(m, "i-cache\t\t: %dK (%u ways x %u sets x %u)\n",
		   icache_size >> 10,
		   boot_cpu_data.icache.ways,
		   boot_cpu_data.icache.sets,
		   boot_cpu_data.icache.linesz);
	seq_printf(m, "d-cache\t\t: %dK (%u ways x %u sets x %u)\n",
		   dcache_size >> 10,
		   boot_cpu_data.dcache.ways,
		   boot_cpu_data.dcache.sets,
		   boot_cpu_data.dcache.linesz);
	seq_printf(m, "bogomips\t: %lu.%02lu\n",
		   boot_cpu_data.loops_per_jiffy / (500000/HZ),
		   (boot_cpu_data.loops_per_jiffy / (5000/HZ)) % 100);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void c_stop(struct seq_file *m, void *v)
{

}

struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show
};
#endif /* CONFIG_PROC_FS */
