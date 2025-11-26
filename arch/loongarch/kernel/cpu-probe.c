// SPDX-License-Identifier: GPL-2.0
/*
 * Processor capabilities determination functions.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/export.h>
#include <linux/printk.h>
#include <linux/uaccess.h>

#include <asm/cpu-features.h>
#include <asm/elf.h>
#include <asm/fpu.h>
#include <asm/loongarch.h>
#include <asm/pgtable-bits.h>
#include <asm/setup.h>

/* Hardware capabilities */
unsigned int elf_hwcap __read_mostly;
EXPORT_SYMBOL_GPL(elf_hwcap);

/*
 * Determine the FCSR mask for FPU hardware.
 */
static inline void cpu_set_fpu_fcsr_mask(struct cpuinfo_loongarch *c)
{
	unsigned long sr, mask, fcsr, fcsr0, fcsr1;

	fcsr = c->fpu_csr0;
	mask = FPU_CSR_ALL_X | FPU_CSR_ALL_E | FPU_CSR_ALL_S | FPU_CSR_RM;

	sr = read_csr_euen();
	enable_fpu();

	fcsr0 = fcsr & mask;
	write_fcsr(LOONGARCH_FCSR0, fcsr0);
	fcsr0 = read_fcsr(LOONGARCH_FCSR0);

	fcsr1 = fcsr | ~mask;
	write_fcsr(LOONGARCH_FCSR0, fcsr1);
	fcsr1 = read_fcsr(LOONGARCH_FCSR0);

	write_fcsr(LOONGARCH_FCSR0, fcsr);

	write_csr_euen(sr);

	c->fpu_mask = ~(fcsr0 ^ fcsr1) & ~mask;
}

/* simd = -1/0/128/256 */
static unsigned int simd = -1U;

static int __init cpu_setup_simd(char *str)
{
	get_option(&str, &simd);
	pr_info("Set SIMD width = %u\n", simd);

	return 0;
}

early_param("simd", cpu_setup_simd);

static int __init cpu_final_simd(void)
{
	struct cpuinfo_loongarch *c = &cpu_data[0];

	if (simd < 128) {
		c->options &= ~LOONGARCH_CPU_LSX;
		elf_hwcap &= ~HWCAP_LOONGARCH_LSX;
	}

	if (simd < 256) {
		c->options &= ~LOONGARCH_CPU_LASX;
		elf_hwcap &= ~HWCAP_LOONGARCH_LASX;
	}

	simd = 0;

	if (c->options & LOONGARCH_CPU_LSX)
		simd = 128;

	if (c->options & LOONGARCH_CPU_LASX)
		simd = 256;

	pr_info("Final SIMD width = %u\n", simd);

	return 0;
}

arch_initcall(cpu_final_simd);

static inline void set_elf_platform(int cpu, const char *plat)
{
	if (cpu == 0)
		__elf_platform = plat;
}

/* MAP BASE */
unsigned long vm_map_base;
EXPORT_SYMBOL(vm_map_base);

static void cpu_probe_addrbits(struct cpuinfo_loongarch *c)
{
#ifdef __NEED_ADDRBITS_PROBE
	c->pabits = (read_cpucfg(LOONGARCH_CPUCFG1) & CPUCFG1_PABITS) >> 4;
	c->vabits = (read_cpucfg(LOONGARCH_CPUCFG1) & CPUCFG1_VABITS) >> 12;
	vm_map_base = 0UL - (1UL << c->vabits);
#endif
}

static void set_isa(struct cpuinfo_loongarch *c, unsigned int isa)
{
	switch (isa) {
	case LOONGARCH_CPU_ISA_LA64:
		c->isa_level |= LOONGARCH_CPU_ISA_LA64;
		fallthrough;
	case LOONGARCH_CPU_ISA_LA32S:
		c->isa_level |= LOONGARCH_CPU_ISA_LA32S;
		fallthrough;
	case LOONGARCH_CPU_ISA_LA32R:
		c->isa_level |= LOONGARCH_CPU_ISA_LA32R;
		break;
	}
}

static void cpu_probe_common(struct cpuinfo_loongarch *c)
{
	unsigned int config;
	unsigned long asid_mask;

	c->options = LOONGARCH_CPU_CPUCFG | LOONGARCH_CPU_CSR | LOONGARCH_CPU_VINT;

	elf_hwcap = HWCAP_LOONGARCH_CPUCFG;

	config = read_cpucfg(LOONGARCH_CPUCFG1);

	switch (config & CPUCFG1_ISA) {
	case 0:
		set_isa(c, LOONGARCH_CPU_ISA_LA32R);
		break;
	case 1:
		set_isa(c, LOONGARCH_CPU_ISA_LA32S);
		break;
	case 2:
		set_isa(c, LOONGARCH_CPU_ISA_LA64);
		break;
	default:
		pr_warn("Warning: unknown ISA level\n");
	}

	if (config & CPUCFG1_PAGING)
		c->options |= LOONGARCH_CPU_TLB;
	if (config & CPUCFG1_IOCSR)
		c->options |= LOONGARCH_CPU_IOCSR;
	if (config & CPUCFG1_MSGINT)
		c->options |= LOONGARCH_CPU_MSGINT;
	if (config & CPUCFG1_UAL) {
		c->options |= LOONGARCH_CPU_UAL;
		elf_hwcap |= HWCAP_LOONGARCH_UAL;
	}
	if (config & CPUCFG1_CRC32) {
		c->options |= LOONGARCH_CPU_CRC32;
		elf_hwcap |= HWCAP_LOONGARCH_CRC32;
	}

	config = read_cpucfg(LOONGARCH_CPUCFG2);
	if (config & CPUCFG2_LAM) {
		c->options |= LOONGARCH_CPU_LAM;
		elf_hwcap |= HWCAP_LOONGARCH_LAM;
	}
	if (config & CPUCFG2_FP) {
		c->options |= LOONGARCH_CPU_FPU;
		elf_hwcap |= HWCAP_LOONGARCH_FPU;
	}
#ifdef CONFIG_CPU_HAS_LSX
	if ((config & CPUCFG2_LSX) && (simd >= 128)) {
		c->options |= LOONGARCH_CPU_LSX;
		elf_hwcap |= HWCAP_LOONGARCH_LSX;
	}
#endif
#ifdef CONFIG_CPU_HAS_LASX
	if ((config & CPUCFG2_LASX) && (simd >= 256)) {
		c->options |= LOONGARCH_CPU_LASX;
		elf_hwcap |= HWCAP_LOONGARCH_LASX;
	}
#endif
	if (config & CPUCFG2_COMPLEX) {
		c->options |= LOONGARCH_CPU_COMPLEX;
		elf_hwcap |= HWCAP_LOONGARCH_COMPLEX;
	}
	if (config & CPUCFG2_CRYPTO) {
		c->options |= LOONGARCH_CPU_CRYPTO;
		elf_hwcap |= HWCAP_LOONGARCH_CRYPTO;
	}
	if (config & CPUCFG2_PTW) {
		c->options |= LOONGARCH_CPU_PTW;
		elf_hwcap |= HWCAP_LOONGARCH_PTW;
	}
	if (config & CPUCFG2_LSPW) {
		c->options |= LOONGARCH_CPU_LSPW;
		elf_hwcap |= HWCAP_LOONGARCH_LSPW;
	}
	if (config & CPUCFG2_LVZP) {
		c->options |= LOONGARCH_CPU_LVZ;
		elf_hwcap |= HWCAP_LOONGARCH_LVZ;
	}
#ifdef CONFIG_CPU_HAS_LBT
	if (config & CPUCFG2_X86BT) {
		c->options |= LOONGARCH_CPU_LBT_X86;
		elf_hwcap |= HWCAP_LOONGARCH_LBT_X86;
	}
	if (config & CPUCFG2_ARMBT) {
		c->options |= LOONGARCH_CPU_LBT_ARM;
		elf_hwcap |= HWCAP_LOONGARCH_LBT_ARM;
	}
	if (config & CPUCFG2_MIPSBT) {
		c->options |= LOONGARCH_CPU_LBT_MIPS;
		elf_hwcap |= HWCAP_LOONGARCH_LBT_MIPS;
	}
#endif

	config = read_cpucfg(LOONGARCH_CPUCFG6);
	if (config & CPUCFG6_PMP)
		c->options |= LOONGARCH_CPU_PMP;

	config = csr_read32(LOONGARCH_CSR_ASID);
	config = (config & CSR_ASID_BIT) >> CSR_ASID_BIT_SHIFT;
	asid_mask = GENMASK(config - 1, 0);
	set_cpu_asid_mask(c, asid_mask);

	config = read_csr_prcfg1();
	c->timerbits = (config & CSR_CONF1_TMRBITS) >> CSR_CONF1_TMRBITS_SHIFT;
	c->ksave_mask = GENMASK((config & CSR_CONF1_KSNUM) - 1, 0);
	c->ksave_mask &= ~(EXC_KSAVE_MASK | PERCPU_KSAVE_MASK | KVM_KSAVE_MASK);

	config = read_csr_prcfg3();
	switch (config & CSR_CONF3_TLBTYPE) {
	case 0:
		c->tlbsizemtlb = 0;
		c->tlbsizestlbsets = 0;
		c->tlbsizestlbways = 0;
		c->tlbsize = 0;
		break;
	case 1:
		c->tlbsizemtlb = ((config & CSR_CONF3_MTLBSIZE) >> CSR_CONF3_MTLBSIZE_SHIFT) + 1;
		c->tlbsizestlbsets = 0;
		c->tlbsizestlbways = 0;
		c->tlbsize = c->tlbsizemtlb + c->tlbsizestlbsets * c->tlbsizestlbways;
		break;
	case 2:
		c->tlbsizemtlb = ((config & CSR_CONF3_MTLBSIZE) >> CSR_CONF3_MTLBSIZE_SHIFT) + 1;
		c->tlbsizestlbsets = 1 << ((config & CSR_CONF3_STLBIDX) >> CSR_CONF3_STLBIDX_SHIFT);
		c->tlbsizestlbways = ((config & CSR_CONF3_STLBWAYS) >> CSR_CONF3_STLBWAYS_SHIFT) + 1;
		c->tlbsize = c->tlbsizemtlb + c->tlbsizestlbsets * c->tlbsizestlbways;
		break;
	default:
		pr_warn("Warning: unknown TLB type\n");
	}

	if (get_num_brps() + get_num_wrps())
		c->options |= LOONGARCH_CPU_WATCH;
}

#define MAX_NAME_LEN	32
#define VENDOR_OFFSET	0
#define CPUNAME_OFFSET	9

static char cpu_full_name[MAX_NAME_LEN] = "        -        ";

static inline void cpu_probe_loongson(struct cpuinfo_loongarch *c, unsigned int cpu)
{
	uint32_t config;
	uint64_t *vendor = (void *)(&cpu_full_name[VENDOR_OFFSET]);
	uint64_t *cpuname = (void *)(&cpu_full_name[CPUNAME_OFFSET]);
	const char *core_name = id_to_core_name(c->processor_id);

	switch (BIT(fls(c->isa_level) - 1)) {
	case LOONGARCH_CPU_ISA_LA32R:
	case LOONGARCH_CPU_ISA_LA32S:
		c->cputype = CPU_LOONGSON32;
		__cpu_family[cpu] = "Loongson-32bit";
		break;
	case LOONGARCH_CPU_ISA_LA64:
		c->cputype = CPU_LOONGSON64;
		__cpu_family[cpu] = "Loongson-64bit";
		break;
	}

	pr_info("%s Processor probed (%s Core)\n", __cpu_family[cpu], core_name);

	if (!cpu_has_iocsr) {
		__cpu_full_name[cpu] = "Unknown";
		return;
	}

	*vendor = iocsr_read64(LOONGARCH_IOCSR_VENDOR);
	*cpuname = iocsr_read64(LOONGARCH_IOCSR_CPUNAME);

	if (!__cpu_full_name[cpu]) {
		if (((char *)vendor)[0] == 0)
			__cpu_full_name[cpu] = "Unknown";
		else
			__cpu_full_name[cpu] = cpu_full_name;
	}

	config = iocsr_read32(LOONGARCH_IOCSR_FEATURES);
	if (config & IOCSRF_CSRIPI)
		c->options |= LOONGARCH_CPU_CSRIPI;
	if (config & IOCSRF_EXTIOI)
		c->options |= LOONGARCH_CPU_EXTIOI;
	if (config & IOCSRF_FREQSCALE)
		c->options |= LOONGARCH_CPU_SCALEFREQ;
	if (config & IOCSRF_FLATMODE)
		c->options |= LOONGARCH_CPU_FLATMODE;
	if (config & IOCSRF_EIODECODE)
		c->options |= LOONGARCH_CPU_EIODECODE;
	if (config & IOCSRF_AVEC)
		c->options |= LOONGARCH_CPU_AVECINT;
	if (config & IOCSRF_REDIRECT)
		c->options |= LOONGARCH_CPU_REDIRECTINT;
	if (config & IOCSRF_VM)
		c->options |= LOONGARCH_CPU_HYPERVISOR;
}

#ifdef CONFIG_64BIT
/* For use by uaccess.h */
u64 __ua_limit;
EXPORT_SYMBOL(__ua_limit);
#endif

const char *__cpu_family[NR_CPUS];
const char *__cpu_full_name[NR_CPUS];
const char *__elf_platform;

static void cpu_report(void)
{
	struct cpuinfo_loongarch *c = &current_cpu_data;

	pr_info("CPU%d revision is: %08x (%s)\n",
		smp_processor_id(), c->processor_id, cpu_family_string());
	if (c->options & LOONGARCH_CPU_FPU)
		pr_info("FPU%d revision is: %08x\n", smp_processor_id(), c->fpu_vers);
}

void cpu_probe(void)
{
	unsigned int cpu = smp_processor_id();
	struct cpuinfo_loongarch *c = &current_cpu_data;

	/*
	 * Set a default ELF platform, cpu probe may later
	 * overwrite it with a more precise value
	 */
	set_elf_platform(cpu, "loongarch");

	c->cputype	= CPU_UNKNOWN;
	c->processor_id = read_cpucfg(LOONGARCH_CPUCFG0);
	c->fpu_vers     = (read_cpucfg(LOONGARCH_CPUCFG2) & CPUCFG2_FPVERS) >> 3;

	c->fpu_csr0	= FPU_CSR_RN;
	c->fpu_mask	= FPU_CSR_RSVD;

	cpu_probe_common(c);

	per_cpu_trap_init(cpu);

	switch (c->processor_id & PRID_COMP_MASK) {
	case PRID_COMP_LOONGSON:
		cpu_probe_loongson(c, cpu);
		break;
	}

	BUG_ON(!__cpu_family[cpu]);
	BUG_ON(c->cputype == CPU_UNKNOWN);

	cpu_probe_addrbits(c);

#ifdef CONFIG_64BIT
	if (cpu == 0)
		__ua_limit = ~((1ull << cpu_vabits) - 1);
#endif

	cpu_report();
}
