// SPDX-License-Identifier: GPL-2.0
/*
 * x86 CPU caches detection and configuration
 *
 * Previous changes
 * - Venkatesh Pallipadi:		Cache identification through CPUID(0x4)
 * - Ashok Raj <ashok.raj@intel.com>:	Work with CPU hotplug infrastructure
 * - Andi Kleen / Andreas Herrmann:	CPUID(0x4) emulation on AMD
 */

#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/cpuhotplug.h>
#include <linux/stop_machine.h>

#include <asm/amd/nb.h>
#include <asm/cacheinfo.h>
#include <asm/cpufeature.h>
#include <asm/cpuid/api.h>
#include <asm/mtrr.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>

#include "cpu.h"

/* Shared last level cache maps */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_llc_shared_map);

/* Shared L2 cache maps */
DEFINE_PER_CPU_READ_MOSTLY(cpumask_var_t, cpu_l2c_shared_map);

static cpumask_var_t cpu_cacheinfo_mask;

/* Kernel controls MTRR and/or PAT MSRs. */
unsigned int memory_caching_control __ro_after_init;

enum _cache_type {
	CTYPE_NULL	= 0,
	CTYPE_DATA	= 1,
	CTYPE_INST	= 2,
	CTYPE_UNIFIED	= 3
};

union _cpuid4_leaf_eax {
	struct {
		enum _cache_type	type			:5;
		unsigned int		level			:3;
		unsigned int		is_self_initializing	:1;
		unsigned int		is_fully_associative	:1;
		unsigned int		reserved		:4;
		unsigned int		num_threads_sharing	:12;
		unsigned int		num_cores_on_die	:6;
	} split;
	u32 full;
};

union _cpuid4_leaf_ebx {
	struct {
		unsigned int		coherency_line_size	:12;
		unsigned int		physical_line_partition	:10;
		unsigned int		ways_of_associativity	:10;
	} split;
	u32 full;
};

union _cpuid4_leaf_ecx {
	struct {
		unsigned int		number_of_sets		:32;
	} split;
	u32 full;
};

struct _cpuid4_info {
	union _cpuid4_leaf_eax eax;
	union _cpuid4_leaf_ebx ebx;
	union _cpuid4_leaf_ecx ecx;
	unsigned int id;
	unsigned long size;
};

/* Map CPUID(0x4) EAX.cache_type to <linux/cacheinfo.h> types */
static const enum cache_type cache_type_map[] = {
	[CTYPE_NULL]	= CACHE_TYPE_NOCACHE,
	[CTYPE_DATA]	= CACHE_TYPE_DATA,
	[CTYPE_INST]	= CACHE_TYPE_INST,
	[CTYPE_UNIFIED] = CACHE_TYPE_UNIFIED,
};

/*
 * Fallback AMD CPUID(0x4) emulation
 * AMD CPUs with TOPOEXT can just use CPUID(0x8000001d)
 *
 * @AMD_L2_L3_INVALID_ASSOC: cache info for the respective L2/L3 cache should
 * be determined from CPUID(0x8000001d) instead of CPUID(0x80000006).
 */

#define AMD_CPUID4_FULLY_ASSOCIATIVE	0xffff
#define AMD_L2_L3_INVALID_ASSOC		0x9

union l1_cache {
	struct {
		unsigned line_size	:8;
		unsigned lines_per_tag	:8;
		unsigned assoc		:8;
		unsigned size_in_kb	:8;
	};
	unsigned int val;
};

union l2_cache {
	struct {
		unsigned line_size	:8;
		unsigned lines_per_tag	:4;
		unsigned assoc		:4;
		unsigned size_in_kb	:16;
	};
	unsigned int val;
};

union l3_cache {
	struct {
		unsigned line_size	:8;
		unsigned lines_per_tag	:4;
		unsigned assoc		:4;
		unsigned res		:2;
		unsigned size_encoded	:14;
	};
	unsigned int val;
};

/* L2/L3 associativity mapping */
static const unsigned short assocs[] = {
	[1]		= 1,
	[2]		= 2,
	[3]		= 3,
	[4]		= 4,
	[5]		= 6,
	[6]		= 8,
	[8]		= 16,
	[0xa]		= 32,
	[0xb]		= 48,
	[0xc]		= 64,
	[0xd]		= 96,
	[0xe]		= 128,
	[0xf]		= AMD_CPUID4_FULLY_ASSOCIATIVE
};

static const unsigned char levels[] = { 1, 1, 2, 3 };
static const unsigned char types[]  = { 1, 2, 3, 3 };

static void legacy_amd_cpuid4(int index, union _cpuid4_leaf_eax *eax,
			      union _cpuid4_leaf_ebx *ebx, union _cpuid4_leaf_ecx *ecx)
{
	unsigned int dummy, line_size, lines_per_tag, assoc, size_in_kb;
	union l1_cache l1i, l1d, *l1;
	union l2_cache l2;
	union l3_cache l3;

	eax->full = 0;
	ebx->full = 0;
	ecx->full = 0;

	cpuid(0x80000005, &dummy, &dummy, &l1d.val, &l1i.val);
	cpuid(0x80000006, &dummy, &dummy, &l2.val, &l3.val);

	l1 = &l1d;
	switch (index) {
	case 1:
		l1 = &l1i;
		fallthrough;
	case 0:
		if (!l1->val)
			return;

		assoc		= (l1->assoc == 0xff) ? AMD_CPUID4_FULLY_ASSOCIATIVE : l1->assoc;
		line_size	= l1->line_size;
		lines_per_tag	= l1->lines_per_tag;
		size_in_kb	= l1->size_in_kb;
		break;
	case 2:
		if (!l2.assoc || l2.assoc == AMD_L2_L3_INVALID_ASSOC)
			return;

		/* Use x86_cache_size as it might have K7 errata fixes */
		assoc		= assocs[l2.assoc];
		line_size	= l2.line_size;
		lines_per_tag	= l2.lines_per_tag;
		size_in_kb	= __this_cpu_read(cpu_info.x86_cache_size);
		break;
	case 3:
		if (!l3.assoc || l3.assoc == AMD_L2_L3_INVALID_ASSOC)
			return;

		assoc		= assocs[l3.assoc];
		line_size	= l3.line_size;
		lines_per_tag	= l3.lines_per_tag;
		size_in_kb	= l3.size_encoded * 512;
		if (boot_cpu_has(X86_FEATURE_AMD_DCM)) {
			size_in_kb	= size_in_kb >> 1;
			assoc		= assoc >> 1;
		}
		break;
	default:
		return;
	}

	eax->split.is_self_initializing		= 1;
	eax->split.type				= types[index];
	eax->split.level			= levels[index];
	eax->split.num_threads_sharing		= 0;
	eax->split.num_cores_on_die		= topology_num_cores_per_package();

	if (assoc == AMD_CPUID4_FULLY_ASSOCIATIVE)
		eax->split.is_fully_associative = 1;

	ebx->split.coherency_line_size		= line_size - 1;
	ebx->split.ways_of_associativity	= assoc - 1;
	ebx->split.physical_line_partition	= lines_per_tag - 1;
	ecx->split.number_of_sets		= (size_in_kb * 1024) / line_size /
		(ebx->split.ways_of_associativity + 1) - 1;
}

static int cpuid4_info_fill_done(struct _cpuid4_info *id4, union _cpuid4_leaf_eax eax,
				 union _cpuid4_leaf_ebx ebx, union _cpuid4_leaf_ecx ecx)
{
	if (eax.split.type == CTYPE_NULL)
		return -EIO;

	id4->eax = eax;
	id4->ebx = ebx;
	id4->ecx = ecx;
	id4->size = (ecx.split.number_of_sets          + 1) *
		    (ebx.split.coherency_line_size     + 1) *
		    (ebx.split.physical_line_partition + 1) *
		    (ebx.split.ways_of_associativity   + 1);

	return 0;
}

static int amd_fill_cpuid4_info(int index, struct _cpuid4_info *id4)
{
	union _cpuid4_leaf_eax eax;
	union _cpuid4_leaf_ebx ebx;
	union _cpuid4_leaf_ecx ecx;
	u32 ignored;

	if (boot_cpu_has(X86_FEATURE_TOPOEXT) || boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)
		cpuid_count(0x8000001d, index, &eax.full, &ebx.full, &ecx.full, &ignored);
	else
		legacy_amd_cpuid4(index, &eax, &ebx, &ecx);

	return cpuid4_info_fill_done(id4, eax, ebx, ecx);
}

static int intel_fill_cpuid4_info(int index, struct _cpuid4_info *id4)
{
	union _cpuid4_leaf_eax eax;
	union _cpuid4_leaf_ebx ebx;
	union _cpuid4_leaf_ecx ecx;
	u32 ignored;

	cpuid_count(4, index, &eax.full, &ebx.full, &ecx.full, &ignored);

	return cpuid4_info_fill_done(id4, eax, ebx, ecx);
}

static int fill_cpuid4_info(int index, struct _cpuid4_info *id4)
{
	u8 cpu_vendor = boot_cpu_data.x86_vendor;

	return (cpu_vendor == X86_VENDOR_AMD || cpu_vendor == X86_VENDOR_HYGON) ?
		amd_fill_cpuid4_info(index, id4) :
		intel_fill_cpuid4_info(index, id4);
}

static int find_num_cache_leaves(struct cpuinfo_x86 *c)
{
	unsigned int eax, ebx, ecx, edx, op;
	union _cpuid4_leaf_eax cache_eax;
	int i = -1;

	/* Do a CPUID(op) loop to calculate num_cache_leaves */
	op = (c->x86_vendor == X86_VENDOR_AMD || c->x86_vendor == X86_VENDOR_HYGON) ? 0x8000001d : 4;
	do {
		++i;
		cpuid_count(op, i, &eax, &ebx, &ecx, &edx);
		cache_eax.full = eax;
	} while (cache_eax.split.type != CTYPE_NULL);
	return i;
}

/*
 * The max shared threads number comes from CPUID(0x4) EAX[25-14] with input
 * ECX as cache index. Then right shift apicid by the number's order to get
 * cache id for this cache node.
 */
static unsigned int get_cache_id(u32 apicid, const struct _cpuid4_info *id4)
{
	unsigned long num_threads_sharing;
	int index_msb;

	num_threads_sharing = 1 + id4->eax.split.num_threads_sharing;
	index_msb = get_count_order(num_threads_sharing);

	return apicid >> index_msb;
}

/*
 * AMD/Hygon CPUs may have multiple LLCs if L3 caches exist.
 */

void cacheinfo_amd_init_llc_id(struct cpuinfo_x86 *c, u16 die_id)
{
	if (!cpuid_amd_hygon_has_l3_cache())
		return;

	if (c->x86 < 0x17) {
		/* Pre-Zen: LLC is at the node level */
		c->topo.llc_id = die_id;
	} else if (c->x86 == 0x17 && c->x86_model <= 0x1F) {
		/*
		 * Family 17h up to 1F models: LLC is at the core
		 * complex level.  Core complex ID is ApicId[3].
		 */
		c->topo.llc_id = c->topo.apicid >> 3;
	} else {
		/*
		 * Newer families: LLC ID is calculated from the number
		 * of threads sharing the L3 cache.
		 */
		u32 llc_index = find_num_cache_leaves(c) - 1;
		struct _cpuid4_info id4 = {};

		if (!amd_fill_cpuid4_info(llc_index, &id4))
			c->topo.llc_id = get_cache_id(c->topo.apicid, &id4);
	}
}

void cacheinfo_hygon_init_llc_id(struct cpuinfo_x86 *c)
{
	if (!cpuid_amd_hygon_has_l3_cache())
		return;

	/*
	 * Hygons are similar to AMD Family 17h up to 1F models: LLC is
	 * at the core complex level.  Core complex ID is ApicId[3].
	 */
	c->topo.llc_id = c->topo.apicid >> 3;
}

void init_amd_cacheinfo(struct cpuinfo_x86 *c)
{
	struct cpu_cacheinfo *ci = get_cpu_cacheinfo(c->cpu_index);

	if (boot_cpu_has(X86_FEATURE_TOPOEXT))
		ci->num_leaves = find_num_cache_leaves(c);
	else if (c->extended_cpuid_level >= 0x80000006)
		ci->num_leaves = (cpuid_edx(0x80000006) & 0xf000) ? 4 : 3;
}

void init_hygon_cacheinfo(struct cpuinfo_x86 *c)
{
	struct cpu_cacheinfo *ci = get_cpu_cacheinfo(c->cpu_index);

	ci->num_leaves = find_num_cache_leaves(c);
}

static void intel_cacheinfo_done(struct cpuinfo_x86 *c, unsigned int l3,
				 unsigned int l2, unsigned int l1i, unsigned int l1d)
{
	/*
	 * If llc_id is still unset, then cpuid_level < 4, which implies
	 * that the only possibility left is SMT.  Since CPUID(0x2) doesn't
	 * specify any shared caches and SMT shares all caches, we can
	 * unconditionally set LLC ID to the package ID so that all
	 * threads share it.
	 */
	if (c->topo.llc_id == BAD_APICID)
		c->topo.llc_id = c->topo.pkg_id;

	c->x86_cache_size = l3 ? l3 : (l2 ? l2 : l1i + l1d);

	if (!l2)
		cpu_detect_cache_sizes(c);
}

/*
 * Legacy Intel CPUID(0x2) path if CPUID(0x4) is not available.
 */
static void intel_cacheinfo_0x2(struct cpuinfo_x86 *c)
{
	unsigned int l1i = 0, l1d = 0, l2 = 0, l3 = 0;
	const struct leaf_0x2_table *desc;
	union leaf_0x2_regs regs;
	u8 *ptr;

	if (c->cpuid_level < 2)
		return;

	cpuid_leaf_0x2(&regs);
	for_each_cpuid_0x2_desc(regs, ptr, desc) {
		switch (desc->c_type) {
		case CACHE_L1_INST:	l1i += desc->c_size; break;
		case CACHE_L1_DATA:	l1d += desc->c_size; break;
		case CACHE_L2:		l2  += desc->c_size; break;
		case CACHE_L3:		l3  += desc->c_size; break;
		}
	}

	intel_cacheinfo_done(c, l3, l2, l1i, l1d);
}

static unsigned int calc_cache_topo_id(struct cpuinfo_x86 *c, const struct _cpuid4_info *id4)
{
	unsigned int num_threads_sharing;
	int index_msb;

	num_threads_sharing = 1 + id4->eax.split.num_threads_sharing;
	index_msb = get_count_order(num_threads_sharing);
	return c->topo.apicid & ~((1 << index_msb) - 1);
}

static bool intel_cacheinfo_0x4(struct cpuinfo_x86 *c)
{
	struct cpu_cacheinfo *ci = get_cpu_cacheinfo(c->cpu_index);
	unsigned int l2_id = BAD_APICID, l3_id = BAD_APICID;
	unsigned int l1d = 0, l1i = 0, l2 = 0, l3 = 0;

	if (c->cpuid_level < 4)
		return false;

	/*
	 * There should be at least one leaf. A non-zero value means
	 * that the number of leaves has been previously initialized.
	 */
	if (!ci->num_leaves)
		ci->num_leaves = find_num_cache_leaves(c);

	if (!ci->num_leaves)
		return false;

	for (int i = 0; i < ci->num_leaves; i++) {
		struct _cpuid4_info id4 = {};
		int ret;

		ret = intel_fill_cpuid4_info(i, &id4);
		if (ret < 0)
			continue;

		switch (id4.eax.split.level) {
		case 1:
			if (id4.eax.split.type == CTYPE_DATA)
				l1d = id4.size / 1024;
			else if (id4.eax.split.type == CTYPE_INST)
				l1i = id4.size / 1024;
			break;
		case 2:
			l2 = id4.size / 1024;
			l2_id = calc_cache_topo_id(c, &id4);
			break;
		case 3:
			l3 = id4.size / 1024;
			l3_id = calc_cache_topo_id(c, &id4);
			break;
		default:
			break;
		}
	}

	c->topo.l2c_id = l2_id;
	c->topo.llc_id = (l3_id == BAD_APICID) ? l2_id : l3_id;
	intel_cacheinfo_done(c, l3, l2, l1i, l1d);
	return true;
}

void init_intel_cacheinfo(struct cpuinfo_x86 *c)
{
	/* Don't use CPUID(0x2) if CPUID(0x4) is supported. */
	if (intel_cacheinfo_0x4(c))
		return;

	intel_cacheinfo_0x2(c);
}

/*
 * <linux/cacheinfo.h> shared_cpu_map setup, AMD/Hygon
 */
static int __cache_amd_cpumap_setup(unsigned int cpu, int index,
				    const struct _cpuid4_info *id4)
{
	struct cpu_cacheinfo *this_cpu_ci;
	struct cacheinfo *ci;
	int i, sibling;

	/*
	 * For L3, always use the pre-calculated cpu_llc_shared_mask
	 * to derive shared_cpu_map.
	 */
	if (index == 3) {
		for_each_cpu(i, cpu_llc_shared_mask(cpu)) {
			this_cpu_ci = get_cpu_cacheinfo(i);
			if (!this_cpu_ci->info_list)
				continue;

			ci = this_cpu_ci->info_list + index;
			for_each_cpu(sibling, cpu_llc_shared_mask(cpu)) {
				if (!cpu_online(sibling))
					continue;
				cpumask_set_cpu(sibling, &ci->shared_cpu_map);
			}
		}
	} else if (boot_cpu_has(X86_FEATURE_TOPOEXT)) {
		unsigned int apicid, nshared, first, last;

		nshared = id4->eax.split.num_threads_sharing + 1;
		apicid = cpu_data(cpu).topo.apicid;
		first = apicid - (apicid % nshared);
		last = first + nshared - 1;

		for_each_online_cpu(i) {
			this_cpu_ci = get_cpu_cacheinfo(i);
			if (!this_cpu_ci->info_list)
				continue;

			apicid = cpu_data(i).topo.apicid;
			if ((apicid < first) || (apicid > last))
				continue;

			ci = this_cpu_ci->info_list + index;

			for_each_online_cpu(sibling) {
				apicid = cpu_data(sibling).topo.apicid;
				if ((apicid < first) || (apicid > last))
					continue;
				cpumask_set_cpu(sibling, &ci->shared_cpu_map);
			}
		}
	} else
		return 0;

	return 1;
}

/*
 * <linux/cacheinfo.h> shared_cpu_map setup, Intel + fallback AMD/Hygon
 */
static void __cache_cpumap_setup(unsigned int cpu, int index,
				 const struct _cpuid4_info *id4)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	struct cacheinfo *ci, *sibling_ci;
	unsigned long num_threads_sharing;
	int index_msb, i;

	if (c->x86_vendor == X86_VENDOR_AMD || c->x86_vendor == X86_VENDOR_HYGON) {
		if (__cache_amd_cpumap_setup(cpu, index, id4))
			return;
	}

	ci = this_cpu_ci->info_list + index;
	num_threads_sharing = 1 + id4->eax.split.num_threads_sharing;

	cpumask_set_cpu(cpu, &ci->shared_cpu_map);
	if (num_threads_sharing == 1)
		return;

	index_msb = get_count_order(num_threads_sharing);

	for_each_online_cpu(i)
		if (cpu_data(i).topo.apicid >> index_msb == c->topo.apicid >> index_msb) {
			struct cpu_cacheinfo *sib_cpu_ci = get_cpu_cacheinfo(i);

			/* Skip if itself or no cacheinfo */
			if (i == cpu || !sib_cpu_ci->info_list)
				continue;

			sibling_ci = sib_cpu_ci->info_list + index;
			cpumask_set_cpu(i, &ci->shared_cpu_map);
			cpumask_set_cpu(cpu, &sibling_ci->shared_cpu_map);
		}
}

static void ci_info_init(struct cacheinfo *ci, const struct _cpuid4_info *id4,
			 struct amd_northbridge *nb)
{
	ci->id				= id4->id;
	ci->attributes			= CACHE_ID;
	ci->level			= id4->eax.split.level;
	ci->type			= cache_type_map[id4->eax.split.type];
	ci->coherency_line_size		= id4->ebx.split.coherency_line_size + 1;
	ci->ways_of_associativity	= id4->ebx.split.ways_of_associativity + 1;
	ci->size			= id4->size;
	ci->number_of_sets		= id4->ecx.split.number_of_sets + 1;
	ci->physical_line_partition	= id4->ebx.split.physical_line_partition + 1;
	ci->priv			= nb;
}

int init_cache_level(unsigned int cpu)
{
	struct cpu_cacheinfo *ci = get_cpu_cacheinfo(cpu);

	/* There should be at least one leaf. */
	if (!ci->num_leaves)
		return -ENOENT;

	return 0;
}

int populate_cache_leaves(unsigned int cpu)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *ci = this_cpu_ci->info_list;
	u8 cpu_vendor = boot_cpu_data.x86_vendor;
	u32 apicid = cpu_data(cpu).topo.apicid;
	struct amd_northbridge *nb = NULL;
	struct _cpuid4_info id4 = {};
	int idx, ret;

	for (idx = 0; idx < this_cpu_ci->num_leaves; idx++) {
		ret = fill_cpuid4_info(idx, &id4);
		if (ret)
			return ret;

		id4.id = get_cache_id(apicid, &id4);

		if (cpu_vendor == X86_VENDOR_AMD || cpu_vendor == X86_VENDOR_HYGON)
			nb = amd_init_l3_cache(idx);

		ci_info_init(ci++, &id4, nb);
		__cache_cpumap_setup(cpu, idx, &id4);
	}

	this_cpu_ci->cpu_map_populated = true;
	return 0;
}

/*
 * Disable and enable caches. Needed for changing MTRRs and the PAT MSR.
 *
 * Since we are disabling the cache don't allow any interrupts,
 * they would run extremely slow and would only increase the pain.
 *
 * The caller must ensure that local interrupts are disabled and
 * are reenabled after cache_enable() has been called.
 */
static unsigned long saved_cr4;
static DEFINE_RAW_SPINLOCK(cache_disable_lock);

/*
 * Cache flushing is the most time-consuming step when programming the
 * MTRRs.  On many Intel CPUs without known erratas, it can be skipped
 * if the CPU declares cache self-snooping support.
 */
static void maybe_flush_caches(void)
{
	if (!static_cpu_has(X86_FEATURE_SELFSNOOP))
		wbinvd();
}

void cache_disable(void) __acquires(cache_disable_lock)
{
	unsigned long cr0;

	/*
	 * This is not ideal since the cache is only flushed/disabled
	 * for this CPU while the MTRRs are changed, but changing this
	 * requires more invasive changes to the way the kernel boots.
	 */
	raw_spin_lock(&cache_disable_lock);

	/* Enter the no-fill (CD=1, NW=0) cache mode and flush caches. */
	cr0 = read_cr0() | X86_CR0_CD;
	write_cr0(cr0);

	maybe_flush_caches();

	/* Save value of CR4 and clear Page Global Enable (bit 7) */
	if (cpu_feature_enabled(X86_FEATURE_PGE)) {
		saved_cr4 = __read_cr4();
		__write_cr4(saved_cr4 & ~X86_CR4_PGE);
	}

	/* Flush all TLBs via a mov %cr3, %reg; mov %reg, %cr3 */
	count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
	flush_tlb_local();

	if (cpu_feature_enabled(X86_FEATURE_MTRR))
		mtrr_disable();

	maybe_flush_caches();
}

void cache_enable(void) __releases(cache_disable_lock)
{
	/* Flush TLBs (no need to flush caches - they are disabled) */
	count_vm_tlb_event(NR_TLB_LOCAL_FLUSH_ALL);
	flush_tlb_local();

	if (cpu_feature_enabled(X86_FEATURE_MTRR))
		mtrr_enable();

	/* Enable caches */
	write_cr0(read_cr0() & ~X86_CR0_CD);

	/* Restore value of CR4 */
	if (cpu_feature_enabled(X86_FEATURE_PGE))
		__write_cr4(saved_cr4);

	raw_spin_unlock(&cache_disable_lock);
}

static void cache_cpu_init(void)
{
	unsigned long flags;

	local_irq_save(flags);

	if (memory_caching_control & CACHE_MTRR) {
		cache_disable();
		mtrr_generic_set_state();
		cache_enable();
	}

	if (memory_caching_control & CACHE_PAT)
		pat_cpu_init();

	local_irq_restore(flags);
}

static bool cache_aps_delayed_init = true;

void set_cache_aps_delayed_init(bool val)
{
	cache_aps_delayed_init = val;
}

bool get_cache_aps_delayed_init(void)
{
	return cache_aps_delayed_init;
}

static int cache_rendezvous_handler(void *unused)
{
	if (get_cache_aps_delayed_init() || !cpu_online(smp_processor_id()))
		cache_cpu_init();

	return 0;
}

void __init cache_bp_init(void)
{
	mtrr_bp_init();
	pat_bp_init();

	if (memory_caching_control)
		cache_cpu_init();
}

void cache_bp_restore(void)
{
	if (memory_caching_control)
		cache_cpu_init();
}

static int cache_ap_online(unsigned int cpu)
{
	cpumask_set_cpu(cpu, cpu_cacheinfo_mask);

	if (!memory_caching_control || get_cache_aps_delayed_init())
		return 0;

	/*
	 * Ideally we should hold mtrr_mutex here to avoid MTRR entries
	 * changed, but this routine will be called in CPU boot time,
	 * holding the lock breaks it.
	 *
	 * This routine is called in two cases:
	 *
	 *   1. very early time of software resume, when there absolutely
	 *      isn't MTRR entry changes;
	 *
	 *   2. CPU hotadd time. We let mtrr_add/del_page hold cpuhotplug
	 *      lock to prevent MTRR entry changes
	 */
	stop_machine_from_inactive_cpu(cache_rendezvous_handler, NULL,
				       cpu_cacheinfo_mask);

	return 0;
}

static int cache_ap_offline(unsigned int cpu)
{
	cpumask_clear_cpu(cpu, cpu_cacheinfo_mask);
	return 0;
}

/*
 * Delayed cache initialization for all AP's
 */
void cache_aps_init(void)
{
	if (!memory_caching_control || !get_cache_aps_delayed_init())
		return;

	stop_machine(cache_rendezvous_handler, NULL, cpu_online_mask);
	set_cache_aps_delayed_init(false);
}

static int __init cache_ap_register(void)
{
	zalloc_cpumask_var(&cpu_cacheinfo_mask, GFP_KERNEL);
	cpumask_set_cpu(smp_processor_id(), cpu_cacheinfo_mask);

	cpuhp_setup_state_nocalls(CPUHP_AP_CACHECTRL_STARTING,
				  "x86/cachectrl:starting",
				  cache_ap_online, cache_ap_offline);
	return 0;
}
early_initcall(cache_ap_register);
