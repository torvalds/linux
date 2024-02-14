// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Procedures for creating, accessing and interpreting the device tree.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 * 
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com 
 */

#undef DEBUG

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/kexec.h>
#include <linux/irq.h>
#include <linux/memblock.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/libfdt.h>
#include <linux/cpu.h>
#include <linux/pgtable.h>
#include <linux/seq_buf.h>

#include <asm/rtas.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/kdump.h>
#include <asm/smp.h>
#include <asm/mmu.h>
#include <asm/paca.h>
#include <asm/powernv.h>
#include <asm/iommu.h>
#include <asm/btext.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/pci-bridge.h>
#include <asm/kexec.h>
#include <asm/opal.h>
#include <asm/fadump.h>
#include <asm/epapr_hcalls.h>
#include <asm/firmware.h>
#include <asm/dt_cpu_ftrs.h>
#include <asm/drmem.h>
#include <asm/ultravisor.h>
#include <asm/prom.h>
#include <asm/plpks.h>

#include <mm/mmu_decl.h>

#ifdef DEBUG
#define DBG(fmt...) printk(KERN_ERR fmt)
#else
#define DBG(fmt...)
#endif

int *chip_id_lookup_table;

#ifdef CONFIG_PPC64
int __initdata iommu_is_off;
int __initdata iommu_force_on;
unsigned long tce_alloc_start, tce_alloc_end;
u64 ppc64_rma_size;
unsigned int boot_cpu_node_count __ro_after_init;
#endif
static phys_addr_t first_memblock_size;
static int __initdata boot_cpu_count;

static int __init early_parse_mem(char *p)
{
	if (!p)
		return 1;

	memory_limit = PAGE_ALIGN(memparse(p, &p));
	DBG("memory limit = 0x%llx\n", memory_limit);

	return 0;
}
early_param("mem", early_parse_mem);

/*
 * overlaps_initrd - check for overlap with page aligned extension of
 * initrd.
 */
static inline int overlaps_initrd(unsigned long start, unsigned long size)
{
#ifdef CONFIG_BLK_DEV_INITRD
	if (!initrd_start)
		return 0;

	return	(start + size) > ALIGN_DOWN(initrd_start, PAGE_SIZE) &&
			start <= ALIGN(initrd_end, PAGE_SIZE);
#else
	return 0;
#endif
}

/**
 * move_device_tree - move tree to an unused area, if needed.
 *
 * The device tree may be allocated beyond our memory limit, or inside the
 * crash kernel region for kdump, or within the page aligned range of initrd.
 * If so, move it out of the way.
 */
static void __init move_device_tree(void)
{
	unsigned long start, size;
	void *p;

	DBG("-> move_device_tree\n");

	start = __pa(initial_boot_params);
	size = fdt_totalsize(initial_boot_params);

	if ((memory_limit && (start + size) > PHYSICAL_START + memory_limit) ||
	    !memblock_is_memory(start + size - 1) ||
	    overlaps_crashkernel(start, size) || overlaps_initrd(start, size)) {
		p = memblock_alloc_raw(size, PAGE_SIZE);
		if (!p)
			panic("Failed to allocate %lu bytes to move device tree\n",
			      size);
		memcpy(p, initial_boot_params, size);
		initial_boot_params = p;
		DBG("Moved device tree to 0x%px\n", p);
	}

	DBG("<- move_device_tree\n");
}

/*
 * ibm,pa/pi-features is a per-cpu property that contains a string of
 * attribute descriptors, each of which has a 2 byte header plus up
 * to 254 bytes worth of processor attribute bits.  First header
 * byte specifies the number of bytes following the header.
 * Second header byte is an "attribute-specifier" type, of which
 * zero is the only currently-defined value.
 * Implementation:  Pass in the byte and bit offset for the feature
 * that we are interested in.  The function will return -1 if the
 * pa-features property is missing, or a 1/0 to indicate if the feature
 * is supported/not supported.  Note that the bit numbers are
 * big-endian to match the definition in PAPR.
 */
struct ibm_feature {
	unsigned long	cpu_features;	/* CPU_FTR_xxx bit */
	unsigned long	mmu_features;	/* MMU_FTR_xxx bit */
	unsigned int	cpu_user_ftrs;	/* PPC_FEATURE_xxx bit */
	unsigned int	cpu_user_ftrs2;	/* PPC_FEATURE2_xxx bit */
	unsigned char	pabyte;		/* byte number in ibm,pa/pi-features */
	unsigned char	pabit;		/* bit number (big-endian) */
	unsigned char	invert;		/* if 1, pa bit set => clear feature */
};

static struct ibm_feature ibm_pa_features[] __initdata = {
	{ .pabyte = 0,  .pabit = 0, .cpu_user_ftrs = PPC_FEATURE_HAS_MMU },
	{ .pabyte = 0,  .pabit = 1, .cpu_user_ftrs = PPC_FEATURE_HAS_FPU },
	{ .pabyte = 0,  .pabit = 3, .cpu_features  = CPU_FTR_CTRL },
	{ .pabyte = 0,  .pabit = 6, .cpu_features  = CPU_FTR_NOEXECUTE },
	{ .pabyte = 1,  .pabit = 2, .mmu_features  = MMU_FTR_CI_LARGE_PAGE },
#ifdef CONFIG_PPC_RADIX_MMU
	{ .pabyte = 40, .pabit = 0, .mmu_features  = MMU_FTR_TYPE_RADIX | MMU_FTR_GTSE },
#endif
	{ .pabyte = 5,  .pabit = 0, .cpu_features  = CPU_FTR_REAL_LE,
				    .cpu_user_ftrs = PPC_FEATURE_TRUE_LE },
	/*
	 * If the kernel doesn't support TM (ie CONFIG_PPC_TRANSACTIONAL_MEM=n),
	 * we don't want to turn on TM here, so we use the *_COMP versions
	 * which are 0 if the kernel doesn't support TM.
	 */
	{ .pabyte = 22, .pabit = 0, .cpu_features = CPU_FTR_TM_COMP,
	  .cpu_user_ftrs2 = PPC_FEATURE2_HTM_COMP | PPC_FEATURE2_HTM_NOSC_COMP },

	{ .pabyte = 64, .pabit = 0, .cpu_features = CPU_FTR_DAWR1 },
	{ .pabyte = 68, .pabit = 5, .cpu_features = CPU_FTR_DEXCR_NPHIE },
};

/*
 * ibm,pi-features property provides the support of processor specific
 * options not described in ibm,pa-features. Right now use byte 0, bit 3
 * which indicates the occurrence of DSI interrupt when the paste operation
 * on the suspended NX window.
 */
static struct ibm_feature ibm_pi_features[] __initdata = {
	{ .pabyte = 0, .pabit = 3, .mmu_features  = MMU_FTR_NX_DSI },
};

static void __init scan_features(unsigned long node, const unsigned char *ftrs,
				 unsigned long tablelen,
				 struct ibm_feature *fp,
				 unsigned long ft_size)
{
	unsigned long i, len, bit;

	/* find descriptor with type == 0 */
	for (;;) {
		if (tablelen < 3)
			return;
		len = 2 + ftrs[0];
		if (tablelen < len)
			return;		/* descriptor 0 not found */
		if (ftrs[1] == 0)
			break;
		tablelen -= len;
		ftrs += len;
	}

	/* loop over bits we know about */
	for (i = 0; i < ft_size; ++i, ++fp) {
		if (fp->pabyte >= ftrs[0])
			continue;
		bit = (ftrs[2 + fp->pabyte] >> (7 - fp->pabit)) & 1;
		if (bit ^ fp->invert) {
			cur_cpu_spec->cpu_features |= fp->cpu_features;
			cur_cpu_spec->cpu_user_features |= fp->cpu_user_ftrs;
			cur_cpu_spec->cpu_user_features2 |= fp->cpu_user_ftrs2;
			cur_cpu_spec->mmu_features |= fp->mmu_features;
		} else {
			cur_cpu_spec->cpu_features &= ~fp->cpu_features;
			cur_cpu_spec->cpu_user_features &= ~fp->cpu_user_ftrs;
			cur_cpu_spec->cpu_user_features2 &= ~fp->cpu_user_ftrs2;
			cur_cpu_spec->mmu_features &= ~fp->mmu_features;
		}
	}
}

static void __init check_cpu_features(unsigned long node, char *name,
				      struct ibm_feature *fp,
				      unsigned long size)
{
	const unsigned char *pa_ftrs;
	int tablelen;

	pa_ftrs = of_get_flat_dt_prop(node, name, &tablelen);
	if (pa_ftrs == NULL)
		return;

	scan_features(node, pa_ftrs, tablelen, fp, size);
}

#ifdef CONFIG_PPC_64S_HASH_MMU
static void __init init_mmu_slb_size(unsigned long node)
{
	const __be32 *slb_size_ptr;

	slb_size_ptr = of_get_flat_dt_prop(node, "slb-size", NULL) ? :
			of_get_flat_dt_prop(node, "ibm,slb-size", NULL);

	if (slb_size_ptr)
		mmu_slb_size = be32_to_cpup(slb_size_ptr);
}
#else
#define init_mmu_slb_size(node) do { } while(0)
#endif

static struct feature_property {
	const char *name;
	u32 min_value;
	unsigned long cpu_feature;
	unsigned long cpu_user_ftr;
} feature_properties[] __initdata = {
#ifdef CONFIG_ALTIVEC
	{"altivec", 0, CPU_FTR_ALTIVEC, PPC_FEATURE_HAS_ALTIVEC},
	{"ibm,vmx", 1, CPU_FTR_ALTIVEC, PPC_FEATURE_HAS_ALTIVEC},
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_VSX
	/* Yes, this _really_ is ibm,vmx == 2 to enable VSX */
	{"ibm,vmx", 2, CPU_FTR_VSX, PPC_FEATURE_HAS_VSX},
#endif /* CONFIG_VSX */
#ifdef CONFIG_PPC64
	{"ibm,dfp", 1, 0, PPC_FEATURE_HAS_DFP},
	{"ibm,purr", 1, CPU_FTR_PURR, 0},
	{"ibm,spurr", 1, CPU_FTR_SPURR, 0},
#endif /* CONFIG_PPC64 */
};

#if defined(CONFIG_44x) && defined(CONFIG_PPC_FPU)
static __init void identical_pvr_fixup(unsigned long node)
{
	unsigned int pvr;
	const char *model = of_get_flat_dt_prop(node, "model", NULL);

	/*
	 * Since 440GR(x)/440EP(x) processors have the same pvr,
	 * we check the node path and set bit 28 in the cur_cpu_spec
	 * pvr for EP(x) processor version. This bit is always 0 in
	 * the "real" pvr. Then we call identify_cpu again with
	 * the new logical pvr to enable FPU support.
	 */
	if (model && strstr(model, "440EP")) {
		pvr = cur_cpu_spec->pvr_value | 0x8;
		identify_cpu(0, pvr);
		DBG("Using logical pvr %x for %s\n", pvr, model);
	}
}
#else
#define identical_pvr_fixup(node) do { } while(0)
#endif

static void __init check_cpu_feature_properties(unsigned long node)
{
	int i;
	struct feature_property *fp = feature_properties;
	const __be32 *prop;

	for (i = 0; i < (int)ARRAY_SIZE(feature_properties); ++i, ++fp) {
		prop = of_get_flat_dt_prop(node, fp->name, NULL);
		if (prop && be32_to_cpup(prop) >= fp->min_value) {
			cur_cpu_spec->cpu_features |= fp->cpu_feature;
			cur_cpu_spec->cpu_user_features |= fp->cpu_user_ftr;
		}
	}
}

static int __init early_init_dt_scan_cpus(unsigned long node,
					  const char *uname, int depth,
					  void *data)
{
	const char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const __be32 *prop;
	const __be32 *intserv;
	int i, nthreads;
	int len;
	int found = -1;
	int found_thread = 0;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	if (IS_ENABLED(CONFIG_PPC64))
		boot_cpu_node_count++;

	/* Get physical cpuid */
	intserv = of_get_flat_dt_prop(node, "ibm,ppc-interrupt-server#s", &len);
	if (!intserv)
		intserv = of_get_flat_dt_prop(node, "reg", &len);

	nthreads = len / sizeof(int);

	/*
	 * Now see if any of these threads match our boot cpu.
	 * NOTE: This must match the parsing done in smp_setup_cpu_maps.
	 */
	for (i = 0; i < nthreads; i++) {
		if (be32_to_cpu(intserv[i]) ==
			fdt_boot_cpuid_phys(initial_boot_params)) {
			found = boot_cpu_count;
			found_thread = i;
		}
#ifdef CONFIG_SMP
		/* logical cpu id is always 0 on UP kernels */
		boot_cpu_count++;
#endif
	}

	/* Not the boot CPU */
	if (found < 0)
		return 0;

	DBG("boot cpu: logical %d physical %d\n", found,
	    be32_to_cpu(intserv[found_thread]));
	boot_cpuid = found;

	if (IS_ENABLED(CONFIG_PPC64))
		boot_cpu_hwid = be32_to_cpu(intserv[found_thread]);

	if (nr_cpu_ids % nthreads != 0) {
		set_nr_cpu_ids(ALIGN(nr_cpu_ids, nthreads));
		pr_warn("nr_cpu_ids was not a multiple of threads_per_core, adjusted to %d\n",
			nr_cpu_ids);
	}

	if (boot_cpuid >= nr_cpu_ids) {
		set_nr_cpu_ids(min(CONFIG_NR_CPUS, ALIGN(boot_cpuid + 1, nthreads)));
		pr_warn("Boot CPU %d >= nr_cpu_ids, adjusted nr_cpu_ids to %d\n",
			boot_cpuid, nr_cpu_ids);
	}

	/*
	 * PAPR defines "logical" PVR values for cpus that
	 * meet various levels of the architecture:
	 * 0x0f000001	Architecture version 2.04
	 * 0x0f000002	Architecture version 2.05
	 * If the cpu-version property in the cpu node contains
	 * such a value, we call identify_cpu again with the
	 * logical PVR value in order to use the cpu feature
	 * bits appropriate for the architecture level.
	 *
	 * A POWER6 partition in "POWER6 architected" mode
	 * uses the 0x0f000002 PVR value; in POWER5+ mode
	 * it uses 0x0f000001.
	 *
	 * If we're using device tree CPU feature discovery then we don't
	 * support the cpu-version property, and it's the responsibility of the
	 * firmware/hypervisor to provide the correct feature set for the
	 * architecture level via the ibm,powerpc-cpu-features binding.
	 */
	if (!dt_cpu_ftrs_in_use()) {
		prop = of_get_flat_dt_prop(node, "cpu-version", NULL);
		if (prop && (be32_to_cpup(prop) & 0xff000000) == 0x0f000000) {
			identify_cpu(0, be32_to_cpup(prop));
			seq_buf_printf(&ppc_hw_desc, "0x%04x ", be32_to_cpup(prop));
		}

		check_cpu_feature_properties(node);
		check_cpu_features(node, "ibm,pa-features", ibm_pa_features,
				   ARRAY_SIZE(ibm_pa_features));
		check_cpu_features(node, "ibm,pi-features", ibm_pi_features,
				   ARRAY_SIZE(ibm_pi_features));
	}

	identical_pvr_fixup(node);
	init_mmu_slb_size(node);

#ifdef CONFIG_PPC64
	if (nthreads == 1)
		cur_cpu_spec->cpu_features &= ~CPU_FTR_SMT;
	else if (!dt_cpu_ftrs_in_use())
		cur_cpu_spec->cpu_features |= CPU_FTR_SMT;
#endif

	return 0;
}

static int __init early_init_dt_scan_chosen_ppc(unsigned long node,
						const char *uname,
						int depth, void *data)
{
	const unsigned long *lprop; /* All these set by kernel, so no need to convert endian */

	/* Use common scan routine to determine if this is the chosen node */
	if (early_init_dt_scan_chosen(data) < 0)
		return 0;

#ifdef CONFIG_PPC64
	/* check if iommu is forced on or off */
	if (of_get_flat_dt_prop(node, "linux,iommu-off", NULL) != NULL)
		iommu_is_off = 1;
	if (of_get_flat_dt_prop(node, "linux,iommu-force-on", NULL) != NULL)
		iommu_force_on = 1;
#endif

	/* mem=x on the command line is the preferred mechanism */
	lprop = of_get_flat_dt_prop(node, "linux,memory-limit", NULL);
	if (lprop)
		memory_limit = *lprop;

#ifdef CONFIG_PPC64
	lprop = of_get_flat_dt_prop(node, "linux,tce-alloc-start", NULL);
	if (lprop)
		tce_alloc_start = *lprop;
	lprop = of_get_flat_dt_prop(node, "linux,tce-alloc-end", NULL);
	if (lprop)
		tce_alloc_end = *lprop;
#endif

#ifdef CONFIG_KEXEC_CORE
	lprop = of_get_flat_dt_prop(node, "linux,crashkernel-base", NULL);
	if (lprop)
		crashk_res.start = *lprop;

	lprop = of_get_flat_dt_prop(node, "linux,crashkernel-size", NULL);
	if (lprop)
		crashk_res.end = crashk_res.start + *lprop - 1;
#endif

	/* break now */
	return 1;
}

/*
 * Compare the range against max mem limit and update
 * size if it cross the limit.
 */

#ifdef CONFIG_SPARSEMEM
static bool __init validate_mem_limit(u64 base, u64 *size)
{
	u64 max_mem = 1UL << (MAX_PHYSMEM_BITS);

	if (base >= max_mem)
		return false;
	if ((base + *size) > max_mem)
		*size = max_mem - base;
	return true;
}
#else
static bool __init validate_mem_limit(u64 base, u64 *size)
{
	return true;
}
#endif

#ifdef CONFIG_PPC_PSERIES
/*
 * Interpret the ibm dynamic reconfiguration memory LMBs.
 * This contains a list of memory blocks along with NUMA affinity
 * information.
 */
static int  __init early_init_drmem_lmb(struct drmem_lmb *lmb,
					const __be32 **usm,
					void *data)
{
	u64 base, size;
	int is_kexec_kdump = 0, rngs;

	base = lmb->base_addr;
	size = drmem_lmb_size();
	rngs = 1;

	/*
	 * Skip this block if the reserved bit is set in flags
	 * or if the block is not assigned to this partition.
	 */
	if ((lmb->flags & DRCONF_MEM_RESERVED) ||
	    !(lmb->flags & DRCONF_MEM_ASSIGNED))
		return 0;

	if (*usm)
		is_kexec_kdump = 1;

	if (is_kexec_kdump) {
		/*
		 * For each memblock in ibm,dynamic-memory, a
		 * corresponding entry in linux,drconf-usable-memory
		 * property contains a counter 'p' followed by 'p'
		 * (base, size) duple. Now read the counter from
		 * linux,drconf-usable-memory property
		 */
		rngs = dt_mem_next_cell(dt_root_size_cells, usm);
		if (!rngs) /* there are no (base, size) duple */
			return 0;
	}

	do {
		if (is_kexec_kdump) {
			base = dt_mem_next_cell(dt_root_addr_cells, usm);
			size = dt_mem_next_cell(dt_root_size_cells, usm);
		}

		if (iommu_is_off) {
			if (base >= 0x80000000ul)
				continue;
			if ((base + size) > 0x80000000ul)
				size = 0x80000000ul - base;
		}

		if (!validate_mem_limit(base, &size))
			continue;

		DBG("Adding: %llx -> %llx\n", base, size);
		memblock_add(base, size);

		if (lmb->flags & DRCONF_MEM_HOTREMOVABLE)
			memblock_mark_hotplug(base, size);
	} while (--rngs);

	return 0;
}
#endif /* CONFIG_PPC_PSERIES */

static int __init early_init_dt_scan_memory_ppc(void)
{
#ifdef CONFIG_PPC_PSERIES
	const void *fdt = initial_boot_params;
	int node = fdt_path_offset(fdt, "/ibm,dynamic-reconfiguration-memory");

	if (node > 0)
		walk_drmem_lmbs_early(node, NULL, early_init_drmem_lmb);

#endif

	return early_init_dt_scan_memory();
}

/*
 * For a relocatable kernel, we need to get the memstart_addr first,
 * then use it to calculate the virtual kernel start address. This has
 * to happen at a very early stage (before machine_init). In this case,
 * we just want to get the memstart_address and would not like to mess the
 * memblock at this stage. So introduce a variable to skip the memblock_add()
 * for this reason.
 */
#ifdef CONFIG_RELOCATABLE
static int add_mem_to_memblock = 1;
#else
#define add_mem_to_memblock 1
#endif

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
#ifdef CONFIG_PPC64
	if (iommu_is_off) {
		if (base >= 0x80000000ul)
			return;
		if ((base + size) > 0x80000000ul)
			size = 0x80000000ul - base;
	}
#endif
	/* Keep track of the beginning of memory -and- the size of
	 * the very first block in the device-tree as it represents
	 * the RMA on ppc64 server
	 */
	if (base < memstart_addr) {
		memstart_addr = base;
		first_memblock_size = size;
	}

	/* Add the chunk to the MEMBLOCK list */
	if (add_mem_to_memblock) {
		if (validate_mem_limit(base, &size))
			memblock_add(base, size);
	}
}

static void __init early_reserve_mem_dt(void)
{
	unsigned long i, dt_root;
	int len;
	const __be32 *prop;

	early_init_fdt_reserve_self();
	early_init_fdt_scan_reserved_mem();

	dt_root = of_get_flat_dt_root();

	prop = of_get_flat_dt_prop(dt_root, "reserved-ranges", &len);

	if (!prop)
		return;

	DBG("Found new-style reserved-ranges\n");

	/* Each reserved range is an (address,size) pair, 2 cells each,
	 * totalling 4 cells per range. */
	for (i = 0; i < len / (sizeof(*prop) * 4); i++) {
		u64 base, size;

		base = of_read_number(prop + (i * 4) + 0, 2);
		size = of_read_number(prop + (i * 4) + 2, 2);

		if (size) {
			DBG("reserving: %llx -> %llx\n", base, size);
			memblock_reserve(base, size);
		}
	}
}

static void __init early_reserve_mem(void)
{
	__be64 *reserve_map;

	reserve_map = (__be64 *)(((unsigned long)initial_boot_params) +
			fdt_off_mem_rsvmap(initial_boot_params));

	/* Look for the new "reserved-regions" property in the DT */
	early_reserve_mem_dt();

#ifdef CONFIG_BLK_DEV_INITRD
	/* Then reserve the initrd, if any */
	if (initrd_start && (initrd_end > initrd_start)) {
		memblock_reserve(ALIGN_DOWN(__pa(initrd_start), PAGE_SIZE),
			ALIGN(initrd_end, PAGE_SIZE) -
			ALIGN_DOWN(initrd_start, PAGE_SIZE));
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	if (!IS_ENABLED(CONFIG_PPC32))
		return;

	/* 
	 * Handle the case where we might be booting from an old kexec
	 * image that setup the mem_rsvmap as pairs of 32-bit values
	 */
	if (be64_to_cpup(reserve_map) > 0xffffffffull) {
		u32 base_32, size_32;
		__be32 *reserve_map_32 = (__be32 *)reserve_map;

		DBG("Found old 32-bit reserve map\n");

		while (1) {
			base_32 = be32_to_cpup(reserve_map_32++);
			size_32 = be32_to_cpup(reserve_map_32++);
			if (size_32 == 0)
				break;
			DBG("reserving: %x -> %x\n", base_32, size_32);
			memblock_reserve(base_32, size_32);
		}
		return;
	}
}

#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
static bool tm_disabled __initdata;

static int __init parse_ppc_tm(char *str)
{
	bool res;

	if (kstrtobool(str, &res))
		return -EINVAL;

	tm_disabled = !res;

	return 0;
}
early_param("ppc_tm", parse_ppc_tm);

static void __init tm_init(void)
{
	if (tm_disabled) {
		pr_info("Disabling hardware transactional memory (HTM)\n");
		cur_cpu_spec->cpu_user_features2 &=
			~(PPC_FEATURE2_HTM_NOSC | PPC_FEATURE2_HTM);
		cur_cpu_spec->cpu_features &= ~CPU_FTR_TM;
		return;
	}

	pnv_tm_init();
}
#else
static void tm_init(void) { }
#endif /* CONFIG_PPC_TRANSACTIONAL_MEM */

static int __init
early_init_dt_scan_model(unsigned long node, const char *uname,
			 int depth, void *data)
{
	const char *prop;

	if (depth != 0)
		return 0;

	prop = of_get_flat_dt_prop(node, "model", NULL);
	if (prop)
		seq_buf_printf(&ppc_hw_desc, "%s ", prop);

	/* break now */
	return 1;
}

#ifdef CONFIG_PPC64
static void __init save_fscr_to_task(void)
{
	/*
	 * Ensure the init_task (pid 0, aka swapper) uses the value of FSCR we
	 * have configured via the device tree features or via __init_FSCR().
	 * That value will then be propagated to pid 1 (init) and all future
	 * processes.
	 */
	if (early_cpu_has_feature(CPU_FTR_ARCH_207S))
		init_task.thread.fscr = mfspr(SPRN_FSCR);
}
#else
static inline void save_fscr_to_task(void) {}
#endif


void __init early_init_devtree(void *params)
{
	phys_addr_t limit;

	DBG(" -> early_init_devtree(%px)\n", params);

	/* Too early to BUG_ON(), do it by hand */
	if (!early_init_dt_verify(params))
		panic("BUG: Failed verifying flat device tree, bad version?");

	of_scan_flat_dt(early_init_dt_scan_model, NULL);

#ifdef CONFIG_PPC_RTAS
	/* Some machines might need RTAS info for debugging, grab it now. */
	of_scan_flat_dt(early_init_dt_scan_rtas, NULL);
#endif

#ifdef CONFIG_PPC_POWERNV
	/* Some machines might need OPAL info for debugging, grab it now. */
	of_scan_flat_dt(early_init_dt_scan_opal, NULL);

	/* Scan tree for ultravisor feature */
	of_scan_flat_dt(early_init_dt_scan_ultravisor, NULL);
#endif

#if defined(CONFIG_FA_DUMP) || defined(CONFIG_PRESERVE_FA_DUMP)
	/* scan tree to see if dump is active during last boot */
	of_scan_flat_dt(early_init_dt_scan_fw_dump, NULL);
#endif

	/* Retrieve various informations from the /chosen node of the
	 * device-tree, including the platform type, initrd location and
	 * size, TCE reserve, and more ...
	 */
	of_scan_flat_dt(early_init_dt_scan_chosen_ppc, boot_command_line);

	/* Scan memory nodes and rebuild MEMBLOCKs */
	early_init_dt_scan_root();
	early_init_dt_scan_memory_ppc();

	/*
	 * As generic code authors expect to be able to use static keys
	 * in early_param() handlers, we initialize the static keys just
	 * before parsing early params (it's fine to call jump_label_init()
	 * more than once).
	 */
	jump_label_init();
	parse_early_param();

	/* make sure we've parsed cmdline for mem= before this */
	if (memory_limit)
		first_memblock_size = min_t(u64, first_memblock_size, memory_limit);
	setup_initial_memory_limit(memstart_addr, first_memblock_size);
	/* Reserve MEMBLOCK regions used by kernel, initrd, dt, etc... */
	memblock_reserve(PHYSICAL_START, __pa(_end) - PHYSICAL_START);
	/* If relocatable, reserve first 32k for interrupt vectors etc. */
	if (PHYSICAL_START > MEMORY_START)
		memblock_reserve(MEMORY_START, 0x8000);
	reserve_kdump_trampoline();
#if defined(CONFIG_FA_DUMP) || defined(CONFIG_PRESERVE_FA_DUMP)
	/*
	 * If we fail to reserve memory for firmware-assisted dump then
	 * fallback to kexec based kdump.
	 */
	if (fadump_reserve_mem() == 0)
#endif
		reserve_crashkernel();
	early_reserve_mem();

	/* Ensure that total memory size is page-aligned. */
	limit = ALIGN(memory_limit ?: memblock_phys_mem_size(), PAGE_SIZE);
	memblock_enforce_memory_limit(limit);

#if defined(CONFIG_PPC_BOOK3S_64) && defined(CONFIG_PPC_4K_PAGES)
	if (!early_radix_enabled())
		memblock_cap_memory_range(0, 1UL << (H_MAX_PHYSMEM_BITS));
#endif

	memblock_allow_resize();
	memblock_dump_all();

	DBG("Phys. mem: %llx\n", (unsigned long long)memblock_phys_mem_size());

	/* We may need to relocate the flat tree, do it now.
	 * FIXME .. and the initrd too? */
	move_device_tree();

	DBG("Scanning CPUs ...\n");

	dt_cpu_ftrs_scan();

	// We can now add the CPU name & PVR to the hardware description
	seq_buf_printf(&ppc_hw_desc, "%s 0x%04lx ", cur_cpu_spec->cpu_name, mfspr(SPRN_PVR));

	/* Retrieve CPU related informations from the flat tree
	 * (altivec support, boot CPU ID, ...)
	 */
	of_scan_flat_dt(early_init_dt_scan_cpus, NULL);
	if (boot_cpuid < 0) {
		printk("Failed to identify boot CPU !\n");
		BUG();
	}

	save_fscr_to_task();

#if defined(CONFIG_SMP) && defined(CONFIG_PPC64)
	/* We'll later wait for secondaries to check in; there are
	 * NCPUS-1 non-boot CPUs  :-)
	 */
	spinning_secondaries = boot_cpu_count - 1;
#endif

	mmu_early_init_devtree();

#ifdef CONFIG_PPC_POWERNV
	/* Scan and build the list of machine check recoverable ranges */
	of_scan_flat_dt(early_init_dt_scan_recoverable_ranges, NULL);
#endif
	epapr_paravirt_early_init();

	/* Now try to figure out if we are running on LPAR and so on */
	pseries_probe_fw_features();

	/*
	 * Initialize pkey features and default AMR/IAMR values
	 */
	pkey_early_init_devtree();

#ifdef CONFIG_PPC_PS3
	/* Identify PS3 firmware */
	if (of_flat_dt_is_compatible(of_get_flat_dt_root(), "sony,ps3"))
		powerpc_firmware_features |= FW_FEATURE_PS3_POSSIBLE;
#endif

	/* If kexec left a PLPKS password in the DT, get it and clear it */
	plpks_early_init_devtree();

	tm_init();

	DBG(" <- early_init_devtree()\n");
}

#ifdef CONFIG_RELOCATABLE
/*
 * This function run before early_init_devtree, so we have to init
 * initial_boot_params.
 */
void __init early_get_first_memblock_info(void *params, phys_addr_t *size)
{
	/* Setup flat device-tree pointer */
	initial_boot_params = params;

	/*
	 * Scan the memory nodes and set add_mem_to_memblock to 0 to avoid
	 * mess the memblock.
	 */
	add_mem_to_memblock = 0;
	early_init_dt_scan_root();
	early_init_dt_scan_memory_ppc();
	add_mem_to_memblock = 1;

	if (size)
		*size = first_memblock_size;
}
#endif

/*******
 *
 * New implementation of the OF "find" APIs, return a refcounted
 * object, call of_node_put() when done.  The device tree and list
 * are protected by a rw_lock.
 *
 * Note that property management will need some locking as well,
 * this isn't dealt with yet.
 *
 *******/

/**
 * of_get_ibm_chip_id - Returns the IBM "chip-id" of a device
 * @np: device node of the device
 *
 * This looks for a property "ibm,chip-id" in the node or any
 * of its parents and returns its content, or -1 if it cannot
 * be found.
 */
int of_get_ibm_chip_id(struct device_node *np)
{
	of_node_get(np);
	while (np) {
		u32 chip_id;

		/*
		 * Skiboot may produce memory nodes that contain more than one
		 * cell in chip-id, we only read the first one here.
		 */
		if (!of_property_read_u32(np, "ibm,chip-id", &chip_id)) {
			of_node_put(np);
			return chip_id;
		}

		np = of_get_next_parent(np);
	}
	return -1;
}
EXPORT_SYMBOL(of_get_ibm_chip_id);

/**
 * cpu_to_chip_id - Return the cpus chip-id
 * @cpu: The logical cpu number.
 *
 * Return the value of the ibm,chip-id property corresponding to the given
 * logical cpu number. If the chip-id can not be found, returns -1.
 */
int cpu_to_chip_id(int cpu)
{
	struct device_node *np;
	int ret = -1, idx;

	idx = cpu / threads_per_core;
	if (chip_id_lookup_table && chip_id_lookup_table[idx] != -1)
		return chip_id_lookup_table[idx];

	np = of_get_cpu_node(cpu, NULL);
	if (np) {
		ret = of_get_ibm_chip_id(np);
		of_node_put(np);

		if (chip_id_lookup_table)
			chip_id_lookup_table[idx] = ret;
	}

	return ret;
}
EXPORT_SYMBOL(cpu_to_chip_id);

bool arch_match_cpu_phys_id(int cpu, u64 phys_id)
{
#ifdef CONFIG_SMP
	/*
	 * Early firmware scanning must use this rather than
	 * get_hard_smp_processor_id because we don't have pacas allocated
	 * until memory topology is discovered.
	 */
	if (cpu_to_phys_id != NULL)
		return (int)phys_id == cpu_to_phys_id[cpu];
#endif

	return (int)phys_id == get_hard_smp_processor_id(cpu);
}
