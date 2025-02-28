// SPDX-License-Identifier: GPL-2.0-only
/*
 * Record and handle CPU attributes.
 *
 * Copyright (C) 2014 ARM Ltd.
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

/*
 * In case the boot CPU is hotpluggable, we record its initial state and
 * current state separately. Certain system registers may contain different
 * values depending on configuration at or after reset.
 */
DEFINE_PER_CPU(struct cpuinfo_arm64, cpu_data);
static struct cpuinfo_arm64 boot_cpu_data;

static inline const char *icache_policy_str(int l1ip)
{
	switch (l1ip) {
	case CTR_EL0_L1Ip_VIPT:
		return "VIPT";
	case CTR_EL0_L1Ip_PIPT:
		return "PIPT";
	default:
		return "RESERVED/UNKNOWN";
	}
}

unsigned long __icache_flags;

static const char *const hwcap_str[] = {
	[KERNEL_HWCAP_FP]		= "fp",
	[KERNEL_HWCAP_ASIMD]		= "asimd",
	[KERNEL_HWCAP_EVTSTRM]		= "evtstrm",
	[KERNEL_HWCAP_AES]		= "aes",
	[KERNEL_HWCAP_PMULL]		= "pmull",
	[KERNEL_HWCAP_SHA1]		= "sha1",
	[KERNEL_HWCAP_SHA2]		= "sha2",
	[KERNEL_HWCAP_CRC32]		= "crc32",
	[KERNEL_HWCAP_ATOMICS]		= "atomics",
	[KERNEL_HWCAP_FPHP]		= "fphp",
	[KERNEL_HWCAP_ASIMDHP]		= "asimdhp",
	[KERNEL_HWCAP_CPUID]		= "cpuid",
	[KERNEL_HWCAP_ASIMDRDM]		= "asimdrdm",
	[KERNEL_HWCAP_JSCVT]		= "jscvt",
	[KERNEL_HWCAP_FCMA]		= "fcma",
	[KERNEL_HWCAP_LRCPC]		= "lrcpc",
	[KERNEL_HWCAP_DCPOP]		= "dcpop",
	[KERNEL_HWCAP_SHA3]		= "sha3",
	[KERNEL_HWCAP_SM3]		= "sm3",
	[KERNEL_HWCAP_SM4]		= "sm4",
	[KERNEL_HWCAP_ASIMDDP]		= "asimddp",
	[KERNEL_HWCAP_SHA512]		= "sha512",
	[KERNEL_HWCAP_SVE]		= "sve",
	[KERNEL_HWCAP_ASIMDFHM]		= "asimdfhm",
	[KERNEL_HWCAP_DIT]		= "dit",
	[KERNEL_HWCAP_USCAT]		= "uscat",
	[KERNEL_HWCAP_ILRCPC]		= "ilrcpc",
	[KERNEL_HWCAP_FLAGM]		= "flagm",
	[KERNEL_HWCAP_SSBS]		= "ssbs",
	[KERNEL_HWCAP_SB]		= "sb",
	[KERNEL_HWCAP_PACA]		= "paca",
	[KERNEL_HWCAP_PACG]		= "pacg",
	[KERNEL_HWCAP_GCS]		= "gcs",
	[KERNEL_HWCAP_DCPODP]		= "dcpodp",
	[KERNEL_HWCAP_SVE2]		= "sve2",
	[KERNEL_HWCAP_SVEAES]		= "sveaes",
	[KERNEL_HWCAP_SVEPMULL]		= "svepmull",
	[KERNEL_HWCAP_SVEBITPERM]	= "svebitperm",
	[KERNEL_HWCAP_SVESHA3]		= "svesha3",
	[KERNEL_HWCAP_SVESM4]		= "svesm4",
	[KERNEL_HWCAP_FLAGM2]		= "flagm2",
	[KERNEL_HWCAP_FRINT]		= "frint",
	[KERNEL_HWCAP_SVEI8MM]		= "svei8mm",
	[KERNEL_HWCAP_SVEF32MM]		= "svef32mm",
	[KERNEL_HWCAP_SVEF64MM]		= "svef64mm",
	[KERNEL_HWCAP_SVEBF16]		= "svebf16",
	[KERNEL_HWCAP_I8MM]		= "i8mm",
	[KERNEL_HWCAP_BF16]		= "bf16",
	[KERNEL_HWCAP_DGH]		= "dgh",
	[KERNEL_HWCAP_RNG]		= "rng",
	[KERNEL_HWCAP_BTI]		= "bti",
	[KERNEL_HWCAP_MTE]		= "mte",
	[KERNEL_HWCAP_ECV]		= "ecv",
	[KERNEL_HWCAP_AFP]		= "afp",
	[KERNEL_HWCAP_RPRES]		= "rpres",
	[KERNEL_HWCAP_MTE3]		= "mte3",
	[KERNEL_HWCAP_SME]		= "sme",
	[KERNEL_HWCAP_SME_I16I64]	= "smei16i64",
	[KERNEL_HWCAP_SME_F64F64]	= "smef64f64",
	[KERNEL_HWCAP_SME_I8I32]	= "smei8i32",
	[KERNEL_HWCAP_SME_F16F32]	= "smef16f32",
	[KERNEL_HWCAP_SME_B16F32]	= "smeb16f32",
	[KERNEL_HWCAP_SME_F32F32]	= "smef32f32",
	[KERNEL_HWCAP_SME_FA64]		= "smefa64",
	[KERNEL_HWCAP_WFXT]		= "wfxt",
	[KERNEL_HWCAP_EBF16]		= "ebf16",
	[KERNEL_HWCAP_SVE_EBF16]	= "sveebf16",
	[KERNEL_HWCAP_CSSC]		= "cssc",
	[KERNEL_HWCAP_RPRFM]		= "rprfm",
	[KERNEL_HWCAP_SVE2P1]		= "sve2p1",
	[KERNEL_HWCAP_SME2]		= "sme2",
	[KERNEL_HWCAP_SME2P1]		= "sme2p1",
	[KERNEL_HWCAP_SME_I16I32]	= "smei16i32",
	[KERNEL_HWCAP_SME_BI32I32]	= "smebi32i32",
	[KERNEL_HWCAP_SME_B16B16]	= "smeb16b16",
	[KERNEL_HWCAP_SME_F16F16]	= "smef16f16",
	[KERNEL_HWCAP_MOPS]		= "mops",
	[KERNEL_HWCAP_HBC]		= "hbc",
	[KERNEL_HWCAP_SVE_B16B16]	= "sveb16b16",
	[KERNEL_HWCAP_LRCPC3]		= "lrcpc3",
	[KERNEL_HWCAP_LSE128]		= "lse128",
	[KERNEL_HWCAP_FPMR]		= "fpmr",
	[KERNEL_HWCAP_LUT]		= "lut",
	[KERNEL_HWCAP_FAMINMAX]		= "faminmax",
	[KERNEL_HWCAP_F8CVT]		= "f8cvt",
	[KERNEL_HWCAP_F8FMA]		= "f8fma",
	[KERNEL_HWCAP_F8DP4]		= "f8dp4",
	[KERNEL_HWCAP_F8DP2]		= "f8dp2",
	[KERNEL_HWCAP_F8E4M3]		= "f8e4m3",
	[KERNEL_HWCAP_F8E5M2]		= "f8e5m2",
	[KERNEL_HWCAP_SME_LUTV2]	= "smelutv2",
	[KERNEL_HWCAP_SME_F8F16]	= "smef8f16",
	[KERNEL_HWCAP_SME_F8F32]	= "smef8f32",
	[KERNEL_HWCAP_SME_SF8FMA]	= "smesf8fma",
	[KERNEL_HWCAP_SME_SF8DP4]	= "smesf8dp4",
	[KERNEL_HWCAP_SME_SF8DP2]	= "smesf8dp2",
	[KERNEL_HWCAP_POE]		= "poe",
	[KERNEL_HWCAP_CMPBR]		= "cmpbr",
	[KERNEL_HWCAP_FPRCVT]		= "fprcvt",
	[KERNEL_HWCAP_F8MM8]		= "f8mm8",
	[KERNEL_HWCAP_F8MM4]		= "f8mm4",
	[KERNEL_HWCAP_SVE_F16MM]	= "svef16mm",
	[KERNEL_HWCAP_SVE_ELTPERM]	= "sveeltperm",
	[KERNEL_HWCAP_SVE_AES2]		= "sveaes2",
	[KERNEL_HWCAP_SVE_BFSCALE]	= "svebfscale",
	[KERNEL_HWCAP_SVE2P2]		= "sve2p2",
	[KERNEL_HWCAP_SME2P2]		= "sme2p2",
	[KERNEL_HWCAP_SME_SBITPERM]	= "smesbitperm",
	[KERNEL_HWCAP_SME_AES]		= "smeaes",
	[KERNEL_HWCAP_SME_SFEXPA]	= "smesfexpa",
	[KERNEL_HWCAP_SME_STMOP]	= "smestmop",
	[KERNEL_HWCAP_SME_SMOP4]	= "smesmop4",
};

#ifdef CONFIG_COMPAT
#define COMPAT_KERNEL_HWCAP(x)	const_ilog2(COMPAT_HWCAP_ ## x)
static const char *const compat_hwcap_str[] = {
	[COMPAT_KERNEL_HWCAP(SWP)]	= "swp",
	[COMPAT_KERNEL_HWCAP(HALF)]	= "half",
	[COMPAT_KERNEL_HWCAP(THUMB)]	= "thumb",
	[COMPAT_KERNEL_HWCAP(26BIT)]	= NULL,	/* Not possible on arm64 */
	[COMPAT_KERNEL_HWCAP(FAST_MULT)] = "fastmult",
	[COMPAT_KERNEL_HWCAP(FPA)]	= NULL,	/* Not possible on arm64 */
	[COMPAT_KERNEL_HWCAP(VFP)]	= "vfp",
	[COMPAT_KERNEL_HWCAP(EDSP)]	= "edsp",
	[COMPAT_KERNEL_HWCAP(JAVA)]	= NULL,	/* Not possible on arm64 */
	[COMPAT_KERNEL_HWCAP(IWMMXT)]	= NULL,	/* Not possible on arm64 */
	[COMPAT_KERNEL_HWCAP(CRUNCH)]	= NULL,	/* Not possible on arm64 */
	[COMPAT_KERNEL_HWCAP(THUMBEE)]	= NULL,	/* Not possible on arm64 */
	[COMPAT_KERNEL_HWCAP(NEON)]	= "neon",
	[COMPAT_KERNEL_HWCAP(VFPv3)]	= "vfpv3",
	[COMPAT_KERNEL_HWCAP(VFPV3D16)]	= NULL,	/* Not possible on arm64 */
	[COMPAT_KERNEL_HWCAP(TLS)]	= "tls",
	[COMPAT_KERNEL_HWCAP(VFPv4)]	= "vfpv4",
	[COMPAT_KERNEL_HWCAP(IDIVA)]	= "idiva",
	[COMPAT_KERNEL_HWCAP(IDIVT)]	= "idivt",
	[COMPAT_KERNEL_HWCAP(VFPD32)]	= NULL,	/* Not possible on arm64 */
	[COMPAT_KERNEL_HWCAP(LPAE)]	= "lpae",
	[COMPAT_KERNEL_HWCAP(EVTSTRM)]	= "evtstrm",
	[COMPAT_KERNEL_HWCAP(FPHP)]	= "fphp",
	[COMPAT_KERNEL_HWCAP(ASIMDHP)]	= "asimdhp",
	[COMPAT_KERNEL_HWCAP(ASIMDDP)]	= "asimddp",
	[COMPAT_KERNEL_HWCAP(ASIMDFHM)]	= "asimdfhm",
	[COMPAT_KERNEL_HWCAP(ASIMDBF16)] = "asimdbf16",
	[COMPAT_KERNEL_HWCAP(I8MM)]	= "i8mm",
};

#define COMPAT_KERNEL_HWCAP2(x)	const_ilog2(COMPAT_HWCAP2_ ## x)
static const char *const compat_hwcap2_str[] = {
	[COMPAT_KERNEL_HWCAP2(AES)]	= "aes",
	[COMPAT_KERNEL_HWCAP2(PMULL)]	= "pmull",
	[COMPAT_KERNEL_HWCAP2(SHA1)]	= "sha1",
	[COMPAT_KERNEL_HWCAP2(SHA2)]	= "sha2",
	[COMPAT_KERNEL_HWCAP2(CRC32)]	= "crc32",
	[COMPAT_KERNEL_HWCAP2(SB)]	= "sb",
	[COMPAT_KERNEL_HWCAP2(SSBS)]	= "ssbs",
};
#endif /* CONFIG_COMPAT */

static int c_show(struct seq_file *m, void *v)
{
	int i, j;
	bool compat = personality(current->personality) == PER_LINUX32;

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
			for (j = 0; j < ARRAY_SIZE(compat_hwcap_str); j++) {
				if (compat_elf_hwcap & (1 << j)) {
					/*
					 * Warn once if any feature should not
					 * have been present on arm64 platform.
					 */
					if (WARN_ON_ONCE(!compat_hwcap_str[j]))
						continue;

					seq_printf(m, " %s", compat_hwcap_str[j]);
				}
			}

			for (j = 0; j < ARRAY_SIZE(compat_hwcap2_str); j++)
				if (compat_elf_hwcap2 & (1 << j))
					seq_printf(m, " %s", compat_hwcap2_str[j]);
#endif /* CONFIG_COMPAT */
		} else {
			for (j = 0; j < ARRAY_SIZE(hwcap_str); j++)
				if (cpu_have_feature(j))
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


static const struct kobj_type cpuregs_kobj_type = {
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
			return sprintf(buf, "0x%016llx\n", info->reg_##_field);	\
		else								\
			return 0;						\
	}									\
	static struct kobj_attribute cpuregs_attr_##_name = __ATTR_RO(_name)

CPUREGS_ATTR_RO(midr_el1, midr);
CPUREGS_ATTR_RO(revidr_el1, revidr);
CPUREGS_ATTR_RO(smidr_el1, smidr);

static struct attribute *cpuregs_id_attrs[] = {
	&cpuregs_attr_midr_el1.attr,
	&cpuregs_attr_revidr_el1.attr,
	NULL
};

static const struct attribute_group cpuregs_attr_group = {
	.attrs = cpuregs_id_attrs,
	.name = "identification"
};

static struct attribute *sme_cpuregs_id_attrs[] = {
	&cpuregs_attr_smidr_el1.attr,
	NULL
};

static const struct attribute_group sme_cpuregs_attr_group = {
	.attrs = sme_cpuregs_id_attrs,
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
	if (system_supports_sme())
		rc = sysfs_merge_group(&info->kobj, &sme_cpuregs_attr_group);
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
device_initcall(cpuinfo_regs_init);

static void cpuinfo_detect_icache_policy(struct cpuinfo_arm64 *info)
{
	unsigned int cpu = smp_processor_id();
	u32 l1ip = CTR_L1IP(info->reg_ctr);

	switch (l1ip) {
	case CTR_EL0_L1Ip_PIPT:
		break;
	case CTR_EL0_L1Ip_VIPT:
	default:
		/* Assume aliasing */
		set_bit(ICACHEF_ALIASING, &__icache_flags);
		break;
	}

	pr_info("Detected %s I-cache on CPU%d\n", icache_policy_str(l1ip), cpu);
}

static void __cpuinfo_store_cpu_32bit(struct cpuinfo_32bit *info)
{
	info->reg_id_dfr0 = read_cpuid(ID_DFR0_EL1);
	info->reg_id_dfr1 = read_cpuid(ID_DFR1_EL1);
	info->reg_id_isar0 = read_cpuid(ID_ISAR0_EL1);
	info->reg_id_isar1 = read_cpuid(ID_ISAR1_EL1);
	info->reg_id_isar2 = read_cpuid(ID_ISAR2_EL1);
	info->reg_id_isar3 = read_cpuid(ID_ISAR3_EL1);
	info->reg_id_isar4 = read_cpuid(ID_ISAR4_EL1);
	info->reg_id_isar5 = read_cpuid(ID_ISAR5_EL1);
	info->reg_id_isar6 = read_cpuid(ID_ISAR6_EL1);
	info->reg_id_mmfr0 = read_cpuid(ID_MMFR0_EL1);
	info->reg_id_mmfr1 = read_cpuid(ID_MMFR1_EL1);
	info->reg_id_mmfr2 = read_cpuid(ID_MMFR2_EL1);
	info->reg_id_mmfr3 = read_cpuid(ID_MMFR3_EL1);
	info->reg_id_mmfr4 = read_cpuid(ID_MMFR4_EL1);
	info->reg_id_mmfr5 = read_cpuid(ID_MMFR5_EL1);
	info->reg_id_pfr0 = read_cpuid(ID_PFR0_EL1);
	info->reg_id_pfr1 = read_cpuid(ID_PFR1_EL1);
	info->reg_id_pfr2 = read_cpuid(ID_PFR2_EL1);

	info->reg_mvfr0 = read_cpuid(MVFR0_EL1);
	info->reg_mvfr1 = read_cpuid(MVFR1_EL1);
	info->reg_mvfr2 = read_cpuid(MVFR2_EL1);
}

static void __cpuinfo_store_cpu(struct cpuinfo_arm64 *info)
{
	info->reg_cntfrq = arch_timer_get_cntfrq();
	/*
	 * Use the effective value of the CTR_EL0 than the raw value
	 * exposed by the CPU. CTR_EL0.IDC field value must be interpreted
	 * with the CLIDR_EL1 fields to avoid triggering false warnings
	 * when there is a mismatch across the CPUs. Keep track of the
	 * effective value of the CTR_EL0 in our internal records for
	 * accurate sanity check and feature enablement.
	 */
	info->reg_ctr = read_cpuid_effective_cachetype();
	info->reg_dczid = read_cpuid(DCZID_EL0);
	info->reg_midr = read_cpuid_id();
	info->reg_revidr = read_cpuid(REVIDR_EL1);

	info->reg_id_aa64dfr0 = read_cpuid(ID_AA64DFR0_EL1);
	info->reg_id_aa64dfr1 = read_cpuid(ID_AA64DFR1_EL1);
	info->reg_id_aa64isar0 = read_cpuid(ID_AA64ISAR0_EL1);
	info->reg_id_aa64isar1 = read_cpuid(ID_AA64ISAR1_EL1);
	info->reg_id_aa64isar2 = read_cpuid(ID_AA64ISAR2_EL1);
	info->reg_id_aa64isar3 = read_cpuid(ID_AA64ISAR3_EL1);
	info->reg_id_aa64mmfr0 = read_cpuid(ID_AA64MMFR0_EL1);
	info->reg_id_aa64mmfr1 = read_cpuid(ID_AA64MMFR1_EL1);
	info->reg_id_aa64mmfr2 = read_cpuid(ID_AA64MMFR2_EL1);
	info->reg_id_aa64mmfr3 = read_cpuid(ID_AA64MMFR3_EL1);
	info->reg_id_aa64mmfr4 = read_cpuid(ID_AA64MMFR4_EL1);
	info->reg_id_aa64pfr0 = read_cpuid(ID_AA64PFR0_EL1);
	info->reg_id_aa64pfr1 = read_cpuid(ID_AA64PFR1_EL1);
	info->reg_id_aa64pfr2 = read_cpuid(ID_AA64PFR2_EL1);
	info->reg_id_aa64zfr0 = read_cpuid(ID_AA64ZFR0_EL1);
	info->reg_id_aa64smfr0 = read_cpuid(ID_AA64SMFR0_EL1);
	info->reg_id_aa64fpfr0 = read_cpuid(ID_AA64FPFR0_EL1);

	if (id_aa64pfr1_mte(info->reg_id_aa64pfr1))
		info->reg_gmid = read_cpuid(GMID_EL1);

	if (id_aa64pfr0_32bit_el0(info->reg_id_aa64pfr0))
		__cpuinfo_store_cpu_32bit(&info->aarch32);

	if (id_aa64pfr0_mpam(info->reg_id_aa64pfr0))
		info->reg_mpamidr = read_cpuid(MPAMIDR_EL1);

	if (IS_ENABLED(CONFIG_ARM64_SME) &&
	    id_aa64pfr1_sme(info->reg_id_aa64pfr1)) {
		/*
		 * We mask out SMPS since even if the hardware
		 * supports priorities the kernel does not at present
		 * and we block access to them.
		 */
		info->reg_smidr = read_cpuid(SMIDR_EL1) & ~SMIDR_EL1_SMPS;
	}

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
}
