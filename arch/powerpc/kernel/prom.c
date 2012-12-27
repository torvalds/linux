/*
 * Procedures for creating, accessing and interpreting the device tree.
 *
 * Paul Mackerras	August 1996.
 * Copyright (C) 1996-2005 Paul Mackerras.
 * 
 *  Adapted for 64bit PowerPC by Dave Engebretsen and Peter Bergner.
 *    {engebret|bergner}@us.ibm.com 
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#undef DEBUG

#include <stdarg.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/stringify.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/bitops.h>
#include <linux/export.h>
#include <linux/kexec.h>
#include <linux/debugfs.h>
#include <linux/irq.h>
#include <linux/memblock.h>
#include <linux/of.h>

#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/kdump.h>
#include <asm/smp.h>
#include <asm/mmu.h>
#include <asm/paca.h>
#include <asm/pgtable.h>
#include <asm/pci.h>
#include <asm/iommu.h>
#include <asm/btext.h>
#include <asm/sections.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/kexec.h>
#include <asm/opal.h>
#include <asm/fadump.h>
#include <asm/debug.h>

#include <mm/mmu_decl.h>

#ifdef DEBUG
#define DBG(fmt...) printk(KERN_ERR fmt)
#else
#define DBG(fmt...)
#endif

#ifdef CONFIG_PPC64
int __initdata iommu_is_off;
int __initdata iommu_force_on;
unsigned long tce_alloc_start, tce_alloc_end;
u64 ppc64_rma_size;
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

	return	(start + size) > _ALIGN_DOWN(initrd_start, PAGE_SIZE) &&
			start <= _ALIGN_UP(initrd_end, PAGE_SIZE);
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
	size = be32_to_cpu(initial_boot_params->totalsize);

	if ((memory_limit && (start + size) > PHYSICAL_START + memory_limit) ||
			overlaps_crashkernel(start, size) ||
			overlaps_initrd(start, size)) {
		p = __va(memblock_alloc(size, PAGE_SIZE));
		memcpy(p, initial_boot_params, size);
		initial_boot_params = (struct boot_param_header *)p;
		DBG("Moved device tree to 0x%p\n", p);
	}

	DBG("<- move_device_tree\n");
}

/*
 * ibm,pa-features is a per-cpu property that contains a string of
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
static struct ibm_pa_feature {
	unsigned long	cpu_features;	/* CPU_FTR_xxx bit */
	unsigned long	mmu_features;	/* MMU_FTR_xxx bit */
	unsigned int	cpu_user_ftrs;	/* PPC_FEATURE_xxx bit */
	unsigned char	pabyte;		/* byte number in ibm,pa-features */
	unsigned char	pabit;		/* bit number (big-endian) */
	unsigned char	invert;		/* if 1, pa bit set => clear feature */
} ibm_pa_features[] __initdata = {
	{0, 0, PPC_FEATURE_HAS_MMU,	0, 0, 0},
	{0, 0, PPC_FEATURE_HAS_FPU,	0, 1, 0},
	{0, MMU_FTR_SLB, 0,		0, 2, 0},
	{CPU_FTR_CTRL, 0, 0,		0, 3, 0},
	{CPU_FTR_NOEXECUTE, 0, 0,	0, 6, 0},
	{CPU_FTR_NODSISRALIGN, 0, 0,	1, 1, 1},
	{0, MMU_FTR_CI_LARGE_PAGE, 0,	1, 2, 0},
	{CPU_FTR_REAL_LE, PPC_FEATURE_TRUE_LE, 5, 0, 0},
};

static void __init scan_features(unsigned long node, unsigned char *ftrs,
				 unsigned long tablelen,
				 struct ibm_pa_feature *fp,
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
			cur_cpu_spec->mmu_features |= fp->mmu_features;
		} else {
			cur_cpu_spec->cpu_features &= ~fp->cpu_features;
			cur_cpu_spec->cpu_user_features &= ~fp->cpu_user_ftrs;
			cur_cpu_spec->mmu_features &= ~fp->mmu_features;
		}
	}
}

static void __init check_cpu_pa_features(unsigned long node)
{
	unsigned char *pa_ftrs;
	unsigned long tablelen;

	pa_ftrs = of_get_flat_dt_prop(node, "ibm,pa-features", &tablelen);
	if (pa_ftrs == NULL)
		return;

	scan_features(node, pa_ftrs, tablelen,
		      ibm_pa_features, ARRAY_SIZE(ibm_pa_features));
}

#ifdef CONFIG_PPC_STD_MMU_64
static void __init check_cpu_slb_size(unsigned long node)
{
	u32 *slb_size_ptr;

	slb_size_ptr = of_get_flat_dt_prop(node, "slb-size", NULL);
	if (slb_size_ptr != NULL) {
		mmu_slb_size = *slb_size_ptr;
		return;
	}
	slb_size_ptr = of_get_flat_dt_prop(node, "ibm,slb-size", NULL);
	if (slb_size_ptr != NULL) {
		mmu_slb_size = *slb_size_ptr;
	}
}
#else
#define check_cpu_slb_size(node) do { } while(0)
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
static inline void identical_pvr_fixup(unsigned long node)
{
	unsigned int pvr;
	char *model = of_get_flat_dt_prop(node, "model", NULL);

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
	unsigned long i;
	struct feature_property *fp = feature_properties;
	const u32 *prop;

	for (i = 0; i < ARRAY_SIZE(feature_properties); ++i, ++fp) {
		prop = of_get_flat_dt_prop(node, fp->name, NULL);
		if (prop && *prop >= fp->min_value) {
			cur_cpu_spec->cpu_features |= fp->cpu_feature;
			cur_cpu_spec->cpu_user_features |= fp->cpu_user_ftr;
		}
	}
}

static int __init early_init_dt_scan_cpus(unsigned long node,
					  const char *uname, int depth,
					  void *data)
{
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const u32 *prop;
	const u32 *intserv;
	int i, nthreads;
	unsigned long len;
	int found = -1;
	int found_thread = 0;

	/* We are scanning "cpu" nodes only */
	if (type == NULL || strcmp(type, "cpu") != 0)
		return 0;

	/* Get physical cpuid */
	intserv = of_get_flat_dt_prop(node, "ibm,ppc-interrupt-server#s", &len);
	if (intserv) {
		nthreads = len / sizeof(int);
	} else {
		intserv = of_get_flat_dt_prop(node, "reg", NULL);
		nthreads = 1;
	}

	/*
	 * Now see if any of these threads match our boot cpu.
	 * NOTE: This must match the parsing done in smp_setup_cpu_maps.
	 */
	for (i = 0; i < nthreads; i++) {
		/*
		 * version 2 of the kexec param format adds the phys cpuid of
		 * booted proc.
		 */
		if (initial_boot_params->version >= 2) {
			if (intserv[i] == initial_boot_params->boot_cpuid_phys) {
				found = boot_cpu_count;
				found_thread = i;
			}
		} else {
			/*
			 * Check if it's the boot-cpu, set it's hw index now,
			 * unfortunately this format did not support booting
			 * off secondary threads.
			 */
			if (of_get_flat_dt_prop(node,
					"linux,boot-cpu", NULL) != NULL)
				found = boot_cpu_count;
		}
#ifdef CONFIG_SMP
		/* logical cpu id is always 0 on UP kernels */
		boot_cpu_count++;
#endif
	}

	if (found >= 0) {
		DBG("boot cpu: logical %d physical %d\n", found,
			intserv[found_thread]);
		boot_cpuid = found;
		set_hard_smp_processor_id(found, intserv[found_thread]);

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
		 */
		prop = of_get_flat_dt_prop(node, "cpu-version", NULL);
		if (prop && (*prop & 0xff000000) == 0x0f000000)
			identify_cpu(0, *prop);

		identical_pvr_fixup(node);
	}

	check_cpu_feature_properties(node);
	check_cpu_pa_features(node);
	check_cpu_slb_size(node);

#ifdef CONFIG_PPC_PSERIES
	if (nthreads > 1)
		cur_cpu_spec->cpu_features |= CPU_FTR_SMT;
	else
		cur_cpu_spec->cpu_features &= ~CPU_FTR_SMT;
#endif

	return 0;
}

int __init early_init_dt_scan_chosen_ppc(unsigned long node, const char *uname,
					 int depth, void *data)
{
	unsigned long *lprop;

	/* Use common scan routine to determine if this is the chosen node */
	if (early_init_dt_scan_chosen(node, uname, depth, data) == 0)
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

#ifdef CONFIG_KEXEC
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

#ifdef CONFIG_PPC_PSERIES
/*
 * Interpret the ibm,dynamic-memory property in the
 * /ibm,dynamic-reconfiguration-memory node.
 * This contains a list of memory blocks along with NUMA affinity
 * information.
 */
static int __init early_init_dt_scan_drconf_memory(unsigned long node)
{
	__be32 *dm, *ls, *usm;
	unsigned long l, n, flags;
	u64 base, size, memblock_size;
	unsigned int is_kexec_kdump = 0, rngs;

	ls = of_get_flat_dt_prop(node, "ibm,lmb-size", &l);
	if (ls == NULL || l < dt_root_size_cells * sizeof(__be32))
		return 0;
	memblock_size = dt_mem_next_cell(dt_root_size_cells, &ls);

	dm = of_get_flat_dt_prop(node, "ibm,dynamic-memory", &l);
	if (dm == NULL || l < sizeof(__be32))
		return 0;

	n = *dm++;	/* number of entries */
	if (l < (n * (dt_root_addr_cells + 4) + 1) * sizeof(__be32))
		return 0;

	/* check if this is a kexec/kdump kernel. */
	usm = of_get_flat_dt_prop(node, "linux,drconf-usable-memory",
						 &l);
	if (usm != NULL)
		is_kexec_kdump = 1;

	for (; n != 0; --n) {
		base = dt_mem_next_cell(dt_root_addr_cells, &dm);
		flags = dm[3];
		/* skip DRC index, pad, assoc. list index, flags */
		dm += 4;
		/* skip this block if the reserved bit is set in flags (0x80)
		   or if the block is not assigned to this partition (0x8) */
		if ((flags & 0x80) || !(flags & 0x8))
			continue;
		size = memblock_size;
		rngs = 1;
		if (is_kexec_kdump) {
			/*
			 * For each memblock in ibm,dynamic-memory, a corresponding
			 * entry in linux,drconf-usable-memory property contains
			 * a counter 'p' followed by 'p' (base, size) duple.
			 * Now read the counter from
			 * linux,drconf-usable-memory property
			 */
			rngs = dt_mem_next_cell(dt_root_size_cells, &usm);
			if (!rngs) /* there are no (base, size) duple */
				continue;
		}
		do {
			if (is_kexec_kdump) {
				base = dt_mem_next_cell(dt_root_addr_cells,
							 &usm);
				size = dt_mem_next_cell(dt_root_size_cells,
							 &usm);
			}
			if (iommu_is_off) {
				if (base >= 0x80000000ul)
					continue;
				if ((base + size) > 0x80000000ul)
					size = 0x80000000ul - base;
			}
			memblock_add(base, size);
		} while (--rngs);
	}
	memblock_dump_all();
	return 0;
}
#else
#define early_init_dt_scan_drconf_memory(node)	0
#endif /* CONFIG_PPC_PSERIES */

static int __init early_init_dt_scan_memory_ppc(unsigned long node,
						const char *uname,
						int depth, void *data)
{
	if (depth == 1 &&
	    strcmp(uname, "ibm,dynamic-reconfiguration-memory") == 0)
		return early_init_dt_scan_drconf_memory(node);
	
	return early_init_dt_scan_memory(node, uname, depth, data);
}

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
	memblock_add(base, size);
}

void * __init early_init_dt_alloc_memory_arch(u64 size, u64 align)
{
	return __va(memblock_alloc(size, align));
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init early_init_dt_setup_initrd_arch(unsigned long start,
		unsigned long end)
{
	initrd_start = (unsigned long)__va(start);
	initrd_end = (unsigned long)__va(end);
	initrd_below_start_ok = 1;
}
#endif

static void __init early_reserve_mem(void)
{
	u64 base, size;
	u64 *reserve_map;
	unsigned long self_base;
	unsigned long self_size;

	reserve_map = (u64 *)(((unsigned long)initial_boot_params) +
					initial_boot_params->off_mem_rsvmap);

	/* before we do anything, lets reserve the dt blob */
	self_base = __pa((unsigned long)initial_boot_params);
	self_size = initial_boot_params->totalsize;
	memblock_reserve(self_base, self_size);

#ifdef CONFIG_BLK_DEV_INITRD
	/* then reserve the initrd, if any */
	if (initrd_start && (initrd_end > initrd_start))
		memblock_reserve(_ALIGN_DOWN(__pa(initrd_start), PAGE_SIZE),
			_ALIGN_UP(initrd_end, PAGE_SIZE) -
			_ALIGN_DOWN(initrd_start, PAGE_SIZE));
#endif /* CONFIG_BLK_DEV_INITRD */

#ifdef CONFIG_PPC32
	/* 
	 * Handle the case where we might be booting from an old kexec
	 * image that setup the mem_rsvmap as pairs of 32-bit values
	 */
	if (*reserve_map > 0xffffffffull) {
		u32 base_32, size_32;
		u32 *reserve_map_32 = (u32 *)reserve_map;

		while (1) {
			base_32 = *(reserve_map_32++);
			size_32 = *(reserve_map_32++);
			if (size_32 == 0)
				break;
			/* skip if the reservation is for the blob */
			if (base_32 == self_base && size_32 == self_size)
				continue;
			DBG("reserving: %x -> %x\n", base_32, size_32);
			memblock_reserve(base_32, size_32);
		}
		return;
	}
#endif
	while (1) {
		base = *(reserve_map++);
		size = *(reserve_map++);
		if (size == 0)
			break;
		DBG("reserving: %llx -> %llx\n", base, size);
		memblock_reserve(base, size);
	}
}

void __init early_init_devtree(void *params)
{
	phys_addr_t limit;

	DBG(" -> early_init_devtree(%p)\n", params);

	/* Setup flat device-tree pointer */
	initial_boot_params = params;

#ifdef CONFIG_PPC_RTAS
	/* Some machines might need RTAS info for debugging, grab it now. */
	of_scan_flat_dt(early_init_dt_scan_rtas, NULL);
#endif

#ifdef CONFIG_PPC_POWERNV
	/* Some machines might need OPAL info for debugging, grab it now. */
	of_scan_flat_dt(early_init_dt_scan_opal, NULL);
#endif

#ifdef CONFIG_FA_DUMP
	/* scan tree to see if dump is active during last boot */
	of_scan_flat_dt(early_init_dt_scan_fw_dump, NULL);
#endif

	/* Pre-initialize the cmd_line with the content of boot_commmand_line,
	 * which will be empty except when the content of the variable has
	 * been overriden by a bootloading mechanism. This happens typically
	 * with HAL takeover
	 */
	strlcpy(cmd_line, boot_command_line, COMMAND_LINE_SIZE);

	/* Retrieve various informations from the /chosen node of the
	 * device-tree, including the platform type, initrd location and
	 * size, TCE reserve, and more ...
	 */
	of_scan_flat_dt(early_init_dt_scan_chosen_ppc, cmd_line);

	/* Scan memory nodes and rebuild MEMBLOCKs */
	of_scan_flat_dt(early_init_dt_scan_root, NULL);
	of_scan_flat_dt(early_init_dt_scan_memory_ppc, NULL);

	/* Save command line for /proc/cmdline and then parse parameters */
	strlcpy(boot_command_line, cmd_line, COMMAND_LINE_SIZE);
	parse_early_param();

	/* make sure we've parsed cmdline for mem= before this */
	if (memory_limit)
		first_memblock_size = min_t(u64, first_memblock_size, memory_limit);
	setup_initial_memory_limit(memstart_addr, first_memblock_size);
	/* Reserve MEMBLOCK regions used by kernel, initrd, dt, etc... */
	memblock_reserve(PHYSICAL_START, __pa(klimit) - PHYSICAL_START);
	/* If relocatable, reserve first 32k for interrupt vectors etc. */
	if (PHYSICAL_START > MEMORY_START)
		memblock_reserve(MEMORY_START, 0x8000);
	reserve_kdump_trampoline();
#ifdef CONFIG_FA_DUMP
	/*
	 * If we fail to reserve memory for firmware-assisted dump then
	 * fallback to kexec based kdump.
	 */
	if (fadump_reserve_mem() == 0)
#endif
		reserve_crashkernel();
	early_reserve_mem();

	/*
	 * Ensure that total memory size is page-aligned, because otherwise
	 * mark_bootmem() gets upset.
	 */
	limit = ALIGN(memory_limit ?: memblock_phys_mem_size(), PAGE_SIZE);
	memblock_enforce_memory_limit(limit);

	memblock_allow_resize();
	memblock_dump_all();

	DBG("Phys. mem: %llx\n", memblock_phys_mem_size());

	/* We may need to relocate the flat tree, do it now.
	 * FIXME .. and the initrd too? */
	move_device_tree();

	allocate_pacas();

	DBG("Scanning CPUs ...\n");

	/* Retrieve CPU related informations from the flat tree
	 * (altivec support, boot CPU ID, ...)
	 */
	of_scan_flat_dt(early_init_dt_scan_cpus, NULL);

#if defined(CONFIG_SMP) && defined(CONFIG_PPC64)
	/* We'll later wait for secondaries to check in; there are
	 * NCPUS-1 non-boot CPUs  :-)
	 */
	spinning_secondaries = boot_cpu_count - 1;
#endif

	DBG(" <- early_init_devtree()\n");
}

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
 *	of_find_next_cache_node - Find a node's subsidiary cache
 *	@np:	node of type "cpu" or "cache"
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.  Caller should hold a reference
 *	to np.
 */
struct device_node *of_find_next_cache_node(struct device_node *np)
{
	struct device_node *child;
	const phandle *handle;

	handle = of_get_property(np, "l2-cache", NULL);
	if (!handle)
		handle = of_get_property(np, "next-level-cache", NULL);

	if (handle)
		return of_find_node_by_phandle(*handle);

	/* OF on pmac has nodes instead of properties named "l2-cache"
	 * beneath CPU nodes.
	 */
	if (!strcmp(np->type, "cpu"))
		for_each_child_of_node(np, child)
			if (!strcmp(child->type, "cache"))
				return child;

	return NULL;
}

#ifdef CONFIG_PPC_PSERIES
/*
 * Fix up the uninitialized fields in a new device node:
 * name, type and pci-specific fields
 */

static int of_finish_dynamic_node(struct device_node *node)
{
	struct device_node *parent = of_get_parent(node);
	int err = 0;
	const phandle *ibm_phandle;

	node->name = of_get_property(node, "name", NULL);
	node->type = of_get_property(node, "device_type", NULL);

	if (!node->name)
		node->name = "<NULL>";
	if (!node->type)
		node->type = "<NULL>";

	if (!parent) {
		err = -ENODEV;
		goto out;
	}

	/* We don't support that function on PowerMac, at least
	 * not yet
	 */
	if (machine_is(powermac))
		return -ENODEV;

	/* fix up new node's phandle field */
	if ((ibm_phandle = of_get_property(node, "ibm,phandle", NULL)))
		node->phandle = *ibm_phandle;

out:
	of_node_put(parent);
	return err;
}

static int prom_reconfig_notifier(struct notifier_block *nb,
				  unsigned long action, void *node)
{
	int err;

	switch (action) {
	case OF_RECONFIG_ATTACH_NODE:
		err = of_finish_dynamic_node(node);
		if (err < 0)
			printk(KERN_ERR "finish_node returned %d\n", err);
		break;
	default:
		err = 0;
		break;
	}
	return notifier_from_errno(err);
}

static struct notifier_block prom_reconfig_nb = {
	.notifier_call = prom_reconfig_notifier,
	.priority = 10, /* This one needs to run first */
};

static int __init prom_reconfig_setup(void)
{
	return of_reconfig_notifier_register(&prom_reconfig_nb);
}
__initcall(prom_reconfig_setup);
#endif

/* Find the device node for a given logical cpu number, also returns the cpu
 * local thread number (index in ibm,interrupt-server#s) if relevant and
 * asked for (non NULL)
 */
struct device_node *of_get_cpu_node(int cpu, unsigned int *thread)
{
	int hardid;
	struct device_node *np;

	hardid = get_hard_smp_processor_id(cpu);

	for_each_node_by_type(np, "cpu") {
		const u32 *intserv;
		unsigned int plen, t;

		/* Check for ibm,ppc-interrupt-server#s. If it doesn't exist
		 * fallback to "reg" property and assume no threads
		 */
		intserv = of_get_property(np, "ibm,ppc-interrupt-server#s",
				&plen);
		if (intserv == NULL) {
			const u32 *reg = of_get_property(np, "reg", NULL);
			if (reg == NULL)
				continue;
			if (*reg == hardid) {
				if (thread)
					*thread = 0;
				return np;
			}
		} else {
			plen /= sizeof(u32);
			for (t = 0; t < plen; t++) {
				if (hardid == intserv[t]) {
					if (thread)
						*thread = t;
					return np;
				}
			}
		}
	}
	return NULL;
}
EXPORT_SYMBOL(of_get_cpu_node);

#if defined(CONFIG_DEBUG_FS) && defined(DEBUG)
static struct debugfs_blob_wrapper flat_dt_blob;

static int __init export_flat_device_tree(void)
{
	struct dentry *d;

	flat_dt_blob.data = initial_boot_params;
	flat_dt_blob.size = initial_boot_params->totalsize;

	d = debugfs_create_blob("flat-device-tree", S_IFREG | S_IRUSR,
				powerpc_debugfs_root, &flat_dt_blob);
	if (!d)
		return 1;

	return 0;
}
__initcall(export_flat_device_tree);
#endif
