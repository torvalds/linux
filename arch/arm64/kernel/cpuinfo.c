/*
 * Record and handle CPU attributes.
 *
 * Copyright (C) 2014 ARM Ltd.
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <asm/arch_timer.h>
#include <asm/cache.h>
#include <asm/cpu.h>
#include <asm/cputype.h>
#include <asm/cpufeature.h>
#include <asm/fpsimd.h>

#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/compat.h>
#include <linux/elf.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/personality.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/seq_file.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/of_fdt.h>

char* (*arch_read_hardware_id)(void);
EXPORT_SYMBOL(arch_read_hardware_id);

static const char *machine_name;

unsigned int system_serial_low;
EXPORT_SYMBOL(system_serial_low);

unsigned int system_serial_high;
EXPORT_SYMBOL(system_serial_high);

/*
 * In case the boot CPU is hotpluggable, we record its initial state and
 * current state separately. Certain system registers may contain different
 * values depending on configuration at or after reset.
 */
DEFINE_PER_CPU(struct cpuinfo_arm64, cpu_data);
static struct cpuinfo_arm64 boot_cpu_data;

static char *icache_policy_str[] = {
	[0 ... ICACHE_POLICY_PIPT]	= "RESERVED/UNKNOWN",
	[ICACHE_POLICY_VIPT]		= "VIPT",
	[ICACHE_POLICY_PIPT]		= "PIPT",
	[ICACHE_POLICY_VPIPT]		= "VPIPT",
};

unsigned long __icache_flags;

static const char *const hwcap_str[] = {
	"fp",
	"asimd",
	"evtstrm",
	"aes",
	"pmull",
	"sha1",
	"sha2",
	"crc32",
	"atomics",
	"fphp",
	"asimdhp",
	"cpuid",
	"asimdrdm",
	"jscvt",
	"fcma",
	"lrcpc",
	"dcpop",
	"sha3",
	"sm3",
	"sm4",
	"asimddp",
	"sha512",
	"sve",
	"asimdfhm",
	"dit",
	"uscat",
	"ilrcpc",
	"flagm",
	"ssbs",
	NULL
};

#ifdef CONFIG_COMPAT
static const char *const compat_hwcap_str[] = {
	"swp",
	"half",
	"thumb",
	"26bit",
	"fastmult",
	"fpa",
	"vfp",
	"edsp",
	"java",
	"iwmmxt",
	"crunch",
	"thumbee",
	"neon",
	"vfpv3",
	"vfpv3d16",
	"tls",
	"vfpv4",
	"idiva",
	"idivt",
	"vfpd32",
	"lpae",
	"evtstrm",
	NULL
};

static const char *const compat_hwcap2_str[] = {
	"aes",
	"pmull",
	"sha1",
	"sha2",
	"crc32",
	NULL
};
#endif /* CONFIG_COMPAT */

static int c_show(struct seq_file *m, void *v)
{
	int i, j;
	bool compat = personality(current->personality) == PER_LINUX32 ||
		      is_compat_task();

	for_each_online_cpu(i) {
		struct cpuinfo_arm64 *cpuinfo = &per_cpu(cpu_data, i);
		u32 midr = cpuinfo->reg_midr;

		/*
		 * glibc reads /proc/cpuinfo to determine the number of
		 * online processors, looking for lines beginning with
		 * "processor".  Give glibc what it expects.
		 */
		seq_printf(m, "processor\t: %d\n", i);
		if (compat)
			seq_printf(m, "model name\t: ARMv8 Processor rev %d (%s)\n",
				   MIDR_REVISION(midr), COMPAT_ELF_PLATFORM);

		seq_printf(m, "BogoMIPS\t: %lu.%02lu\n",
			   loops_per_jiffy / (500000UL/HZ),
			   loops_per_jiffy / (5000UL/HZ) % 100);

		/*
		 * Dump out the common processor features in a single line.
		 * Userspace should read the hwcaps with getauxval(AT_HWCAP)
		 * rather than attempting to parse this, but there's a body of
		 * software which does already (at least for 32-bit).
		 */
		seq_puts(m, "Features\t:");
		if (compat) {
#ifdef CONFIG_COMPAT
			for (j = 0; compat_hwcap_str[j]; j++)
				if (compat_elf_hwcap & (1 << j))
					seq_printf(m, " %s", compat_hwcap_str[j]);

			for (j = 0; compat_hwcap2_str[j]; j++)
				if (compat_elf_hwcap2 & (1 << j))
					seq_printf(m, " %s", compat_hwcap2_str[j]);
#endif /* CONFIG_COMPAT */
		} else {
			for (j = 0; hwcap_str[j]; j++)
				if (elf_hwcap & (1 << j))
					seq_printf(m, " %s", hwcap_str[j]);
		}
		seq_puts(m, "\n");

		seq_printf(m, "CPU implementer\t: 0x%02x\n",
			   MIDR_IMPLEMENTOR(midr));
		seq_printf(m, "CPU architecture: 8\n");
		seq_printf(m, "CPU variant\t: 0x%x\n", MIDR_VARIANT(midr));
		seq_printf(m, "CPU part\t: 0x%03x\n", MIDR_PARTNUM(midr));
		seq_printf(m, "CPU revision\t: %d\n\n", MIDR_REVISION(midr));
	}

	if (!arch_read_hardware_id)
		seq_printf(m, "Hardware\t: %s\n", machine_name);
	else
		seq_printf(m, "Hardware\t: %s\n", arch_read_hardware_id());

	seq_printf(m, "Serial\t\t: %08x%08x\n",
		   system_serial_high, system_serial_low);

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

const struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show
};


static struct kobj_type cpuregs_kobj_type = {
	.sysfs_ops = &kobj_sysfs_ops,
};

/*
 * The ARM ARM uses the phrase "32-bit register" to describe a register
 * whose upper 32 bits are RES0 (per C5.1.1, ARM DDI 0487A.i), however
 * no statement is made as to whether the upper 32 bits will or will not
 * be made use of in future, and between ARM DDI 0487A.c and ARM DDI
 * 0487A.d CLIDR_EL1 was expanded from 32-bit to 64-bit.
 *
 * Thus, while both MIDR_EL1 and REVIDR_EL1 are described as 32-bit
 * registers, we expose them both as 64 bit values to cater for possible
 * future expansion without an ABI break.
 */
#define kobj_to_cpuinfo(kobj)	container_of(kobj, struct cpuinfo_arm64, kobj)
#define CPUREGS_ATTR_RO(_name, _field)						\
	static ssize_t _name##_show(struct kobject *kobj,			\
			struct kobj_attribute *attr, char *buf)			\
	{									\
		struct cpuinfo_arm64 *info = kobj_to_cpuinfo(kobj);		\
										\
		if (info->reg_midr)						\
			return sprintf(buf, "0x%016x\n", info->reg_##_field);	\
		else								\
			return 0;						\
	}									\
	static struct kobj_attribute cpuregs_attr_##_name = __ATTR_RO(_name)

CPUREGS_ATTR_RO(midr_el1, midr);
CPUREGS_ATTR_RO(revidr_el1, revidr);

static struct attribute *cpuregs_id_attrs[] = {
	&cpuregs_attr_midr_el1.attr,
	&cpuregs_attr_revidr_el1.attr,
	NULL
};

static const struct attribute_group cpuregs_attr_group = {
	.attrs = cpuregs_id_attrs,
	.name = "identification"
};

static int cpuid_cpu_online(unsigned int cpu)
{
	int rc;
	struct device *dev;
	struct cpuinfo_arm64 *info = &per_cpu(cpu_data, cpu);

	dev = get_cpu_device(cpu);
	if (!dev) {
		rc = -ENODEV;
		goto out;
	}
	rc = kobject_add(&info->kobj, &dev->kobj, "regs");
	if (rc)
		goto out;
	rc = sysfs_create_group(&info->kobj, &cpuregs_attr_group);
	if (rc)
		kobject_del(&info->kobj);
out:
	return rc;
}

static int cpuid_cpu_offline(unsigned int cpu)
{
	struct device *dev;
	struct cpuinfo_arm64 *info = &per_cpu(cpu_data, cpu);

	dev = get_cpu_device(cpu);
	if (!dev)
		return -ENODEV;
	if (info->kobj.parent) {
		sysfs_remove_group(&info->kobj, &cpuregs_attr_group);
		kobject_del(&info->kobj);
	}

	return 0;
}

static int __init cpuinfo_regs_init(void)
{
	int cpu, ret;

	for_each_possible_cpu(cpu) {
		struct cpuinfo_arm64 *info = &per_cpu(cpu_data, cpu);

		kobject_init(&info->kobj, &cpuregs_kobj_type);
	}

	ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "arm64/cpuinfo:online",
				cpuid_cpu_online, cpuid_cpu_offline);
	if (ret < 0) {
		pr_err("cpuinfo: failed to register hotplug callbacks.\n");
		return ret;
	}
	return 0;
}
static void cpuinfo_detect_icache_policy(struct cpuinfo_arm64 *info)
{
	unsigned int cpu = smp_processor_id();
	u32 l1ip = CTR_L1IP(info->reg_ctr);

	switch (l1ip) {
	case ICACHE_POLICY_PIPT:
		break;
	case ICACHE_POLICY_VPIPT:
		set_bit(ICACHEF_VPIPT, &__icache_flags);
		break;
	default:
		/* Fallthrough */
	case ICACHE_POLICY_VIPT:
		/* Assume aliasing */
		set_bit(ICACHEF_ALIASING, &__icache_flags);
	}

	pr_info("Detected %s I-cache on CPU%d\n", icache_policy_str[l1ip], cpu);
}

static void __cpuinfo_store_cpu(struct cpuinfo_arm64 *info)
{
	info->reg_cntfrq = arch_timer_get_cntfrq();
	info->reg_ctr = read_cpuid_cachetype();
	info->reg_dczid = read_cpuid(DCZID_EL0);
	info->reg_midr = read_cpuid_id();
	info->reg_revidr = read_cpuid(REVIDR_EL1);

	info->reg_id_aa64dfr0 = read_cpuid(ID_AA64DFR0_EL1);
	info->reg_id_aa64dfr1 = read_cpuid(ID_AA64DFR1_EL1);
	info->reg_id_aa64isar0 = read_cpuid(ID_AA64ISAR0_EL1);
	info->reg_id_aa64isar1 = read_cpuid(ID_AA64ISAR1_EL1);
	info->reg_id_aa64mmfr0 = read_cpuid(ID_AA64MMFR0_EL1);
	info->reg_id_aa64mmfr1 = read_cpuid(ID_AA64MMFR1_EL1);
	info->reg_id_aa64mmfr2 = read_cpuid(ID_AA64MMFR2_EL1);
	info->reg_id_aa64pfr0 = read_cpuid(ID_AA64PFR0_EL1);
	info->reg_id_aa64pfr1 = read_cpuid(ID_AA64PFR1_EL1);
	info->reg_id_aa64zfr0 = read_cpuid(ID_AA64ZFR0_EL1);

	/* Update the 32bit ID registers only if AArch32 is implemented */
	if (id_aa64pfr0_32bit_el0(info->reg_id_aa64pfr0)) {
		info->reg_id_dfr0 = read_cpuid(ID_DFR0_EL1);
		info->reg_id_isar0 = read_cpuid(ID_ISAR0_EL1);
		info->reg_id_isar1 = read_cpuid(ID_ISAR1_EL1);
		info->reg_id_isar2 = read_cpuid(ID_ISAR2_EL1);
		info->reg_id_isar3 = read_cpuid(ID_ISAR3_EL1);
		info->reg_id_isar4 = read_cpuid(ID_ISAR4_EL1);
		info->reg_id_isar5 = read_cpuid(ID_ISAR5_EL1);
		info->reg_id_mmfr0 = read_cpuid(ID_MMFR0_EL1);
		info->reg_id_mmfr1 = read_cpuid(ID_MMFR1_EL1);
		info->reg_id_mmfr2 = read_cpuid(ID_MMFR2_EL1);
		info->reg_id_mmfr3 = read_cpuid(ID_MMFR3_EL1);
		info->reg_id_pfr0 = read_cpuid(ID_PFR0_EL1);
		info->reg_id_pfr1 = read_cpuid(ID_PFR1_EL1);

		info->reg_mvfr0 = read_cpuid(MVFR0_EL1);
		info->reg_mvfr1 = read_cpuid(MVFR1_EL1);
		info->reg_mvfr2 = read_cpuid(MVFR2_EL1);
	}

	if (IS_ENABLED(CONFIG_ARM64_SVE) &&
	    id_aa64pfr0_sve(info->reg_id_aa64pfr0))
		info->reg_zcr = read_zcr_features();

	cpuinfo_detect_icache_policy(info);
}

void cpuinfo_store_cpu(void)
{
	struct cpuinfo_arm64 *info = this_cpu_ptr(&cpu_data);
	__cpuinfo_store_cpu(info);
	update_cpu_features(smp_processor_id(), info, &boot_cpu_data);
}

void __init cpuinfo_store_boot_cpu(void)
{
	struct cpuinfo_arm64 *info = &per_cpu(cpu_data, 0);
	__cpuinfo_store_cpu(info);

	boot_cpu_data = *info;
	init_cpu_features(&boot_cpu_data);
	machine_name = of_flat_dt_get_machine_name();
}

device_initcall(cpuinfo_regs_init);
