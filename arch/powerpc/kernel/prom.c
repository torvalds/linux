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
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/debugfs.h>
#include <linux/irq.h>

#include <asm/prom.h>
#include <asm/rtas.h>
#include <asm/lmb.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/kdump.h>
#include <asm/smp.h>
#include <asm/system.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/pci.h>
#include <asm/iommu.h>
#include <asm/btext.h>
#include <asm/sections.h>
#include <asm/machdep.h>
#include <asm/pSeries_reconfig.h>
#include <asm/pci-bridge.h>
#include <asm/kexec.h>

#ifdef DEBUG
#define DBG(fmt...) printk(KERN_ERR fmt)
#else
#define DBG(fmt...)
#endif


static int __initdata dt_root_addr_cells;
static int __initdata dt_root_size_cells;

#ifdef CONFIG_PPC64
int __initdata iommu_is_off;
int __initdata iommu_force_on;
unsigned long tce_alloc_start, tce_alloc_end;
#endif

typedef u32 cell_t;

#if 0
static struct boot_param_header *initial_boot_params __initdata;
#else
struct boot_param_header *initial_boot_params;
#endif

extern struct device_node *allnodes;	/* temporary while merging */

extern rwlock_t devtree_lock;	/* temporary while merging */

/* export that to outside world */
struct device_node *of_chosen;

static inline char *find_flat_dt_string(u32 offset)
{
	return ((char *)initial_boot_params) +
		initial_boot_params->off_dt_strings + offset;
}

/**
 * This function is used to scan the flattened device-tree, it is
 * used to extract the memory informations at boot before we can
 * unflatten the tree
 */
int __init of_scan_flat_dt(int (*it)(unsigned long node,
				     const char *uname, int depth,
				     void *data),
			   void *data)
{
	unsigned long p = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;
	int rc = 0;
	int depth = -1;

	do {
		u32 tag = *((u32 *)p);
		char *pathp;
		
		p += 4;
		if (tag == OF_DT_END_NODE) {
			depth --;
			continue;
		}
		if (tag == OF_DT_NOP)
			continue;
		if (tag == OF_DT_END)
			break;
		if (tag == OF_DT_PROP) {
			u32 sz = *((u32 *)p);
			p += 8;
			if (initial_boot_params->version < 0x10)
				p = _ALIGN(p, sz >= 8 ? 8 : 4);
			p += sz;
			p = _ALIGN(p, 4);
			continue;
		}
		if (tag != OF_DT_BEGIN_NODE) {
			printk(KERN_WARNING "Invalid tag %x scanning flattened"
			       " device tree !\n", tag);
			return -EINVAL;
		}
		depth++;
		pathp = (char *)p;
		p = _ALIGN(p + strlen(pathp) + 1, 4);
		if ((*pathp) == '/') {
			char *lp, *np;
			for (lp = NULL, np = pathp; *np; np++)
				if ((*np) == '/')
					lp = np+1;
			if (lp != NULL)
				pathp = lp;
		}
		rc = it(p, pathp, depth, data);
		if (rc != 0)
			break;		
	} while(1);

	return rc;
}

unsigned long __init of_get_flat_dt_root(void)
{
	unsigned long p = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;

	while(*((u32 *)p) == OF_DT_NOP)
		p += 4;
	BUG_ON (*((u32 *)p) != OF_DT_BEGIN_NODE);
	p += 4;
	return _ALIGN(p + strlen((char *)p) + 1, 4);
}

/**
 * This  function can be used within scan_flattened_dt callback to get
 * access to properties
 */
void* __init of_get_flat_dt_prop(unsigned long node, const char *name,
				 unsigned long *size)
{
	unsigned long p = node;

	do {
		u32 tag = *((u32 *)p);
		u32 sz, noff;
		const char *nstr;

		p += 4;
		if (tag == OF_DT_NOP)
			continue;
		if (tag != OF_DT_PROP)
			return NULL;

		sz = *((u32 *)p);
		noff = *((u32 *)(p + 4));
		p += 8;
		if (initial_boot_params->version < 0x10)
			p = _ALIGN(p, sz >= 8 ? 8 : 4);

		nstr = find_flat_dt_string(noff);
		if (nstr == NULL) {
			printk(KERN_WARNING "Can't find property index"
			       " name !\n");
			return NULL;
		}
		if (strcmp(name, nstr) == 0) {
			if (size)
				*size = sz;
			return (void *)p;
		}
		p += sz;
		p = _ALIGN(p, 4);
	} while(1);
}

int __init of_flat_dt_is_compatible(unsigned long node, const char *compat)
{
	const char* cp;
	unsigned long cplen, l;

	cp = of_get_flat_dt_prop(node, "compatible", &cplen);
	if (cp == NULL)
		return 0;
	while (cplen > 0) {
		if (strncasecmp(cp, compat, strlen(compat)) == 0)
			return 1;
		l = strlen(cp) + 1;
		cp += l;
		cplen -= l;
	}

	return 0;
}

static void *__init unflatten_dt_alloc(unsigned long *mem, unsigned long size,
				       unsigned long align)
{
	void *res;

	*mem = _ALIGN(*mem, align);
	res = (void *)*mem;
	*mem += size;

	return res;
}

static unsigned long __init unflatten_dt_node(unsigned long mem,
					      unsigned long *p,
					      struct device_node *dad,
					      struct device_node ***allnextpp,
					      unsigned long fpsize)
{
	struct device_node *np;
	struct property *pp, **prev_pp = NULL;
	char *pathp;
	u32 tag;
	unsigned int l, allocl;
	int has_name = 0;
	int new_format = 0;

	tag = *((u32 *)(*p));
	if (tag != OF_DT_BEGIN_NODE) {
		printk("Weird tag at start of node: %x\n", tag);
		return mem;
	}
	*p += 4;
	pathp = (char *)*p;
	l = allocl = strlen(pathp) + 1;
	*p = _ALIGN(*p + l, 4);

	/* version 0x10 has a more compact unit name here instead of the full
	 * path. we accumulate the full path size using "fpsize", we'll rebuild
	 * it later. We detect this because the first character of the name is
	 * not '/'.
	 */
	if ((*pathp) != '/') {
		new_format = 1;
		if (fpsize == 0) {
			/* root node: special case. fpsize accounts for path
			 * plus terminating zero. root node only has '/', so
			 * fpsize should be 2, but we want to avoid the first
			 * level nodes to have two '/' so we use fpsize 1 here
			 */
			fpsize = 1;
			allocl = 2;
		} else {
			/* account for '/' and path size minus terminal 0
			 * already in 'l'
			 */
			fpsize += l;
			allocl = fpsize;
		}
	}


	np = unflatten_dt_alloc(&mem, sizeof(struct device_node) + allocl,
				__alignof__(struct device_node));
	if (allnextpp) {
		memset(np, 0, sizeof(*np));
		np->full_name = ((char*)np) + sizeof(struct device_node);
		if (new_format) {
			char *p = np->full_name;
			/* rebuild full path for new format */
			if (dad && dad->parent) {
				strcpy(p, dad->full_name);
#ifdef DEBUG
				if ((strlen(p) + l + 1) != allocl) {
					DBG("%s: p: %d, l: %d, a: %d\n",
					    pathp, (int)strlen(p), l, allocl);
				}
#endif
				p += strlen(p);
			}
			*(p++) = '/';
			memcpy(p, pathp, l);
		} else
			memcpy(np->full_name, pathp, l);
		prev_pp = &np->properties;
		**allnextpp = np;
		*allnextpp = &np->allnext;
		if (dad != NULL) {
			np->parent = dad;
			/* we temporarily use the next field as `last_child'*/
			if (dad->next == 0)
				dad->child = np;
			else
				dad->next->sibling = np;
			dad->next = np;
		}
		kref_init(&np->kref);
	}
	while(1) {
		u32 sz, noff;
		char *pname;

		tag = *((u32 *)(*p));
		if (tag == OF_DT_NOP) {
			*p += 4;
			continue;
		}
		if (tag != OF_DT_PROP)
			break;
		*p += 4;
		sz = *((u32 *)(*p));
		noff = *((u32 *)((*p) + 4));
		*p += 8;
		if (initial_boot_params->version < 0x10)
			*p = _ALIGN(*p, sz >= 8 ? 8 : 4);

		pname = find_flat_dt_string(noff);
		if (pname == NULL) {
			printk("Can't find property name in list !\n");
			break;
		}
		if (strcmp(pname, "name") == 0)
			has_name = 1;
		l = strlen(pname) + 1;
		pp = unflatten_dt_alloc(&mem, sizeof(struct property),
					__alignof__(struct property));
		if (allnextpp) {
			if (strcmp(pname, "linux,phandle") == 0) {
				np->node = *((u32 *)*p);
				if (np->linux_phandle == 0)
					np->linux_phandle = np->node;
			}
			if (strcmp(pname, "ibm,phandle") == 0)
				np->linux_phandle = *((u32 *)*p);
			pp->name = pname;
			pp->length = sz;
			pp->value = (void *)*p;
			*prev_pp = pp;
			prev_pp = &pp->next;
		}
		*p = _ALIGN((*p) + sz, 4);
	}
	/* with version 0x10 we may not have the name property, recreate
	 * it here from the unit name if absent
	 */
	if (!has_name) {
		char *p = pathp, *ps = pathp, *pa = NULL;
		int sz;

		while (*p) {
			if ((*p) == '@')
				pa = p;
			if ((*p) == '/')
				ps = p + 1;
			p++;
		}
		if (pa < ps)
			pa = p;
		sz = (pa - ps) + 1;
		pp = unflatten_dt_alloc(&mem, sizeof(struct property) + sz,
					__alignof__(struct property));
		if (allnextpp) {
			pp->name = "name";
			pp->length = sz;
			pp->value = pp + 1;
			*prev_pp = pp;
			prev_pp = &pp->next;
			memcpy(pp->value, ps, sz - 1);
			((char *)pp->value)[sz - 1] = 0;
			DBG("fixed up name for %s -> %s\n", pathp,
				(char *)pp->value);
		}
	}
	if (allnextpp) {
		*prev_pp = NULL;
		np->name = of_get_property(np, "name", NULL);
		np->type = of_get_property(np, "device_type", NULL);

		if (!np->name)
			np->name = "<NULL>";
		if (!np->type)
			np->type = "<NULL>";
	}
	while (tag == OF_DT_BEGIN_NODE) {
		mem = unflatten_dt_node(mem, p, np, allnextpp, fpsize);
		tag = *((u32 *)(*p));
	}
	if (tag != OF_DT_END_NODE) {
		printk("Weird tag at end of node: %x\n", tag);
		return mem;
	}
	*p += 4;
	return mem;
}

static int __init early_parse_mem(char *p)
{
	if (!p)
		return 1;

	memory_limit = PAGE_ALIGN(memparse(p, &p));
	DBG("memory limit = 0x%lx\n", memory_limit);

	return 0;
}
early_param("mem", early_parse_mem);

/**
 * move_device_tree - move tree to an unused area, if needed.
 *
 * The device tree may be allocated beyond our memory limit, or inside the
 * crash kernel region for kdump. If so, move it out of the way.
 */
static void move_device_tree(void)
{
	unsigned long start, size;
	void *p;

	DBG("-> move_device_tree\n");

	start = __pa(initial_boot_params);
	size = initial_boot_params->totalsize;

	if ((memory_limit && (start + size) > memory_limit) ||
			overlaps_crashkernel(start, size)) {
		p = __va(lmb_alloc_base(size, PAGE_SIZE, lmb.rmo_size));
		memcpy(p, initial_boot_params, size);
		initial_boot_params = (struct boot_param_header *)p;
		DBG("Moved device tree to 0x%p\n", p);
	}

	DBG("<- move_device_tree\n");
}

/**
 * unflattens the device-tree passed by the firmware, creating the
 * tree of struct device_node. It also fills the "name" and "type"
 * pointers of the nodes so the normal device-tree walking functions
 * can be used (this used to be done by finish_device_tree)
 */
void __init unflatten_device_tree(void)
{
	unsigned long start, mem, size;
	struct device_node **allnextp = &allnodes;

	DBG(" -> unflatten_device_tree()\n");

	/* First pass, scan for size */
	start = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;
	size = unflatten_dt_node(0, &start, NULL, NULL, 0);
	size = (size | 3) + 1;

	DBG("  size is %lx, allocating...\n", size);

	/* Allocate memory for the expanded device tree */
	mem = lmb_alloc(size + 4, __alignof__(struct device_node));
	mem = (unsigned long) __va(mem);

	((u32 *)mem)[size / 4] = 0xdeadbeef;

	DBG("  unflattening %lx...\n", mem);

	/* Second pass, do actual unflattening */
	start = ((unsigned long)initial_boot_params) +
		initial_boot_params->off_dt_struct;
	unflatten_dt_node(mem, &start, NULL, &allnextp, 0);
	if (*((u32 *)start) != OF_DT_END)
		printk(KERN_WARNING "Weird tag at end of tree: %08x\n", *((u32 *)start));
	if (((u32 *)mem)[size / 4] != 0xdeadbeef)
		printk(KERN_WARNING "End of tree marker overwritten: %08x\n",
		       ((u32 *)mem)[size / 4] );
	*allnextp = NULL;

	/* Get pointer to OF "/chosen" node for use everywhere */
	of_chosen = of_find_node_by_path("/chosen");
	if (of_chosen == NULL)
		of_chosen = of_find_node_by_path("/chosen@0");

	DBG(" <- unflatten_device_tree()\n");
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
	unsigned int	cpu_user_ftrs;	/* PPC_FEATURE_xxx bit */
	unsigned char	pabyte;		/* byte number in ibm,pa-features */
	unsigned char	pabit;		/* bit number (big-endian) */
	unsigned char	invert;		/* if 1, pa bit set => clear feature */
} ibm_pa_features[] __initdata = {
	{0, PPC_FEATURE_HAS_MMU,	0, 0, 0},
	{0, PPC_FEATURE_HAS_FPU,	0, 1, 0},
	{CPU_FTR_SLB, 0,		0, 2, 0},
	{CPU_FTR_CTRL, 0,		0, 3, 0},
	{CPU_FTR_NOEXECUTE, 0,		0, 6, 0},
	{CPU_FTR_NODSISRALIGN, 0,	1, 1, 1},
	{CPU_FTR_CI_LARGE_PAGE, 0,	1, 2, 0},
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
		} else {
			cur_cpu_spec->cpu_features &= ~fp->cpu_features;
			cur_cpu_spec->cpu_user_features &= ~fp->cpu_user_ftrs;
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
#ifdef CONFIG_PPC64
	{"ibm,dfp", 1, 0, PPC_FEATURE_HAS_DFP},
	{"ibm,purr", 1, CPU_FTR_PURR, 0},
	{"ibm,spurr", 1, CPU_FTR_SPURR, 0},
#endif /* CONFIG_PPC64 */
};

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
	static int logical_cpuid = 0;
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	const u32 *prop;
	const u32 *intserv;
	int i, nthreads;
	unsigned long len;
	int found = 0;

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
		if (initial_boot_params && initial_boot_params->version >= 2) {
			if (intserv[i] ==
					initial_boot_params->boot_cpuid_phys) {
				found = 1;
				break;
			}
		} else {
			/*
			 * Check if it's the boot-cpu, set it's hw index now,
			 * unfortunately this format did not support booting
			 * off secondary threads.
			 */
			if (of_get_flat_dt_prop(node,
					"linux,boot-cpu", NULL) != NULL) {
				found = 1;
				break;
			}
		}

#ifdef CONFIG_SMP
		/* logical cpu id is always 0 on UP kernels */
		logical_cpuid++;
#endif
	}

	if (found) {
		DBG("boot cpu: logical %d physical %d\n", logical_cpuid,
			intserv[i]);
		boot_cpuid = logical_cpuid;
		set_hard_smp_processor_id(boot_cpuid, intserv[i]);

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
#if defined(CONFIG_44x) && defined(CONFIG_PPC_FPU)
		/*
		 * Since 440GR(x)/440EP(x) processors have the same pvr,
		 * we check the node path and set bit 28 in the cur_cpu_spec
		 * pvr for EP(x) processor version. This bit is always 0 in
		 * the "real" pvr. Then we call identify_cpu again with
		 * the new logical pvr to enable FPU support.
		 */
		if (strstr(uname, "440EP")) {
			identify_cpu(0, cur_cpu_spec->pvr_value | 0x8);
		}
#endif
	}

	check_cpu_feature_properties(node);
	check_cpu_pa_features(node);

#ifdef CONFIG_PPC_PSERIES
	if (nthreads > 1)
		cur_cpu_spec->cpu_features |= CPU_FTR_SMT;
	else
		cur_cpu_spec->cpu_features &= ~CPU_FTR_SMT;
#endif

	return 0;
}

#ifdef CONFIG_BLK_DEV_INITRD
static void __init early_init_dt_check_for_initrd(unsigned long node)
{
	unsigned long l;
	u32 *prop;

	DBG("Looking for initrd properties... ");

	prop = of_get_flat_dt_prop(node, "linux,initrd-start", &l);
	if (prop) {
		initrd_start = (unsigned long)__va(of_read_ulong(prop, l/4));

		prop = of_get_flat_dt_prop(node, "linux,initrd-end", &l);
		if (prop) {
			initrd_end = (unsigned long)
					__va(of_read_ulong(prop, l/4));
			initrd_below_start_ok = 1;
		} else {
			initrd_start = 0;
		}
	}

	DBG("initrd_start=0x%lx  initrd_end=0x%lx\n", initrd_start, initrd_end);
}
#else
static inline void early_init_dt_check_for_initrd(unsigned long node)
{
}
#endif /* CONFIG_BLK_DEV_INITRD */

static int __init early_init_dt_scan_chosen(unsigned long node,
					    const char *uname, int depth, void *data)
{
	unsigned long *lprop;
	unsigned long l;
	char *p;

	DBG("search \"chosen\", depth: %d, uname: %s\n", depth, uname);

	if (depth != 1 ||
	    (strcmp(uname, "chosen") != 0 && strcmp(uname, "chosen@0") != 0))
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
	lprop = (u64*)of_get_flat_dt_prop(node, "linux,crashkernel-base", NULL);
	if (lprop)
		crashk_res.start = *lprop;

	lprop = (u64*)of_get_flat_dt_prop(node, "linux,crashkernel-size", NULL);
	if (lprop)
		crashk_res.end = crashk_res.start + *lprop - 1;
#endif

	early_init_dt_check_for_initrd(node);

	/* Retreive command line */
 	p = of_get_flat_dt_prop(node, "bootargs", &l);
	if (p != NULL && l > 0)
		strlcpy(cmd_line, p, min((int)l, COMMAND_LINE_SIZE));

#ifdef CONFIG_CMDLINE
	if (p == NULL || l == 0 || (l == 1 && (*p) == 0))
		strlcpy(cmd_line, CONFIG_CMDLINE, COMMAND_LINE_SIZE);
#endif /* CONFIG_CMDLINE */

	DBG("Command line is: %s\n", cmd_line);

	/* break now */
	return 1;
}

static int __init early_init_dt_scan_root(unsigned long node,
					  const char *uname, int depth, void *data)
{
	u32 *prop;

	if (depth != 0)
		return 0;

	prop = of_get_flat_dt_prop(node, "#size-cells", NULL);
	dt_root_size_cells = (prop == NULL) ? 1 : *prop;
	DBG("dt_root_size_cells = %x\n", dt_root_size_cells);

	prop = of_get_flat_dt_prop(node, "#address-cells", NULL);
	dt_root_addr_cells = (prop == NULL) ? 2 : *prop;
	DBG("dt_root_addr_cells = %x\n", dt_root_addr_cells);
	
	/* break now */
	return 1;
}

static unsigned long __init dt_mem_next_cell(int s, cell_t **cellp)
{
	cell_t *p = *cellp;

	*cellp = p + s;
	return of_read_ulong(p, s);
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
	cell_t *dm, *ls;
	unsigned long l, n;
	unsigned long base, size, lmb_size, flags;

	ls = (cell_t *)of_get_flat_dt_prop(node, "ibm,lmb-size", &l);
	if (ls == NULL || l < dt_root_size_cells * sizeof(cell_t))
		return 0;
	lmb_size = dt_mem_next_cell(dt_root_size_cells, &ls);

	dm = (cell_t *)of_get_flat_dt_prop(node, "ibm,dynamic-memory", &l);
	if (dm == NULL || l < sizeof(cell_t))
		return 0;

	n = *dm++;	/* number of entries */
	if (l < (n * (dt_root_addr_cells + 4) + 1) * sizeof(cell_t))
		return 0;

	for (; n != 0; --n) {
		base = dt_mem_next_cell(dt_root_addr_cells, &dm);
		flags = dm[3];
		/* skip DRC index, pad, assoc. list index, flags */
		dm += 4;
		/* skip this block if the reserved bit is set in flags (0x80)
		   or if the block is not assigned to this partition (0x8) */
		if ((flags & 0x80) || !(flags & 0x8))
			continue;
		size = lmb_size;
		if (iommu_is_off) {
			if (base >= 0x80000000ul)
				continue;
			if ((base + size) > 0x80000000ul)
				size = 0x80000000ul - base;
		}
		lmb_add(base, size);
	}
	lmb_dump_all();
	return 0;
}
#else
#define early_init_dt_scan_drconf_memory(node)	0
#endif /* CONFIG_PPC_PSERIES */

static int __init early_init_dt_scan_memory(unsigned long node,
					    const char *uname, int depth, void *data)
{
	char *type = of_get_flat_dt_prop(node, "device_type", NULL);
	cell_t *reg, *endp;
	unsigned long l;

	/* Look for the ibm,dynamic-reconfiguration-memory node */
	if (depth == 1 &&
	    strcmp(uname, "ibm,dynamic-reconfiguration-memory") == 0)
		return early_init_dt_scan_drconf_memory(node);

	/* We are scanning "memory" nodes only */
	if (type == NULL) {
		/*
		 * The longtrail doesn't have a device_type on the
		 * /memory node, so look for the node called /memory@0.
		 */
		if (depth != 1 || strcmp(uname, "memory@0") != 0)
			return 0;
	} else if (strcmp(type, "memory") != 0)
		return 0;

	reg = (cell_t *)of_get_flat_dt_prop(node, "linux,usable-memory", &l);
	if (reg == NULL)
		reg = (cell_t *)of_get_flat_dt_prop(node, "reg", &l);
	if (reg == NULL)
		return 0;

	endp = reg + (l / sizeof(cell_t));

	DBG("memory scan node %s, reg size %ld, data: %x %x %x %x,\n",
	    uname, l, reg[0], reg[1], reg[2], reg[3]);

	while ((endp - reg) >= (dt_root_addr_cells + dt_root_size_cells)) {
		unsigned long base, size;

		base = dt_mem_next_cell(dt_root_addr_cells, &reg);
		size = dt_mem_next_cell(dt_root_size_cells, &reg);

		if (size == 0)
			continue;
		DBG(" - %lx ,  %lx\n", base, size);
#ifdef CONFIG_PPC64
		if (iommu_is_off) {
			if (base >= 0x80000000ul)
				continue;
			if ((base + size) > 0x80000000ul)
				size = 0x80000000ul - base;
		}
#endif
		lmb_add(base, size);
	}
	return 0;
}

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
	lmb_reserve(self_base, self_size);

#ifdef CONFIG_BLK_DEV_INITRD
	/* then reserve the initrd, if any */
	if (initrd_start && (initrd_end > initrd_start))
		lmb_reserve(__pa(initrd_start), initrd_end - initrd_start);
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
			lmb_reserve(base_32, size_32);
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
		lmb_reserve(base, size);
	}

#if 0
	DBG("memory reserved, lmbs :\n");
      	lmb_dump_all();
#endif
}

void __init early_init_devtree(void *params)
{
	DBG(" -> early_init_devtree(%p)\n", params);

	/* Setup flat device-tree pointer */
	initial_boot_params = params;

#ifdef CONFIG_PPC_RTAS
	/* Some machines might need RTAS info for debugging, grab it now. */
	of_scan_flat_dt(early_init_dt_scan_rtas, NULL);
#endif

	/* Retrieve various informations from the /chosen node of the
	 * device-tree, including the platform type, initrd location and
	 * size, TCE reserve, and more ...
	 */
	of_scan_flat_dt(early_init_dt_scan_chosen, NULL);

	/* Scan memory nodes and rebuild LMBs */
	lmb_init();
	of_scan_flat_dt(early_init_dt_scan_root, NULL);
	of_scan_flat_dt(early_init_dt_scan_memory, NULL);

	/* Save command line for /proc/cmdline and then parse parameters */
	strlcpy(boot_command_line, cmd_line, COMMAND_LINE_SIZE);
	parse_early_param();

	/* Reserve LMB regions used by kernel, initrd, dt, etc... */
	lmb_reserve(PHYSICAL_START, __pa(klimit) - PHYSICAL_START);
	reserve_kdump_trampoline();
	reserve_crashkernel();
	early_reserve_mem();

	lmb_enforce_memory_limit(memory_limit);
	lmb_analyze();

	DBG("Phys. mem: %lx\n", lmb_phys_mem_size());

	/* We may need to relocate the flat tree, do it now.
	 * FIXME .. and the initrd too? */
	move_device_tree();

	DBG("Scanning CPUs ...\n");

	/* Retreive CPU related informations from the flat tree
	 * (altivec support, boot CPU ID, ...)
	 */
	of_scan_flat_dt(early_init_dt_scan_cpus, NULL);

	DBG(" <- early_init_devtree()\n");
}


/**
 * Indicates whether the root node has a given value in its
 * compatible property.
 */
int machine_is_compatible(const char *compat)
{
	struct device_node *root;
	int rc = 0;

	root = of_find_node_by_path("/");
	if (root) {
		rc = of_device_is_compatible(root, compat);
		of_node_put(root);
	}
	return rc;
}
EXPORT_SYMBOL(machine_is_compatible);

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
 *	of_find_node_by_phandle - Find a node given a phandle
 *	@handle:	phandle of the node to find
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_node_by_phandle(phandle handle)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	for (np = allnodes; np != 0; np = np->allnext)
		if (np->linux_phandle == handle)
			break;
	of_node_get(np);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_node_by_phandle);

/**
 *	of_find_all_nodes - Get next node in global list
 *	@prev:	Previous node or NULL to start iteration
 *		of_node_put() will be called on it
 *
 *	Returns a node pointer with refcount incremented, use
 *	of_node_put() on it when done.
 */
struct device_node *of_find_all_nodes(struct device_node *prev)
{
	struct device_node *np;

	read_lock(&devtree_lock);
	np = prev ? prev->allnext : allnodes;
	for (; np != 0; np = np->allnext)
		if (of_node_get(np))
			break;
	of_node_put(prev);
	read_unlock(&devtree_lock);
	return np;
}
EXPORT_SYMBOL(of_find_all_nodes);

/**
 *	of_node_get - Increment refcount of a node
 *	@node:	Node to inc refcount, NULL is supported to
 *		simplify writing of callers
 *
 *	Returns node.
 */
struct device_node *of_node_get(struct device_node *node)
{
	if (node)
		kref_get(&node->kref);
	return node;
}
EXPORT_SYMBOL(of_node_get);

static inline struct device_node * kref_to_device_node(struct kref *kref)
{
	return container_of(kref, struct device_node, kref);
}

/**
 *	of_node_release - release a dynamically allocated node
 *	@kref:  kref element of the node to be released
 *
 *	In of_node_put() this function is passed to kref_put()
 *	as the destructor.
 */
static void of_node_release(struct kref *kref)
{
	struct device_node *node = kref_to_device_node(kref);
	struct property *prop = node->properties;

	/* We should never be releasing nodes that haven't been detached. */
	if (!of_node_check_flag(node, OF_DETACHED)) {
		printk("WARNING: Bad of_node_put() on %s\n", node->full_name);
		dump_stack();
		kref_init(&node->kref);
		return;
	}

	if (!of_node_check_flag(node, OF_DYNAMIC))
		return;

	while (prop) {
		struct property *next = prop->next;
		kfree(prop->name);
		kfree(prop->value);
		kfree(prop);
		prop = next;

		if (!prop) {
			prop = node->deadprops;
			node->deadprops = NULL;
		}
	}
	kfree(node->full_name);
	kfree(node->data);
	kfree(node);
}

/**
 *	of_node_put - Decrement refcount of a node
 *	@node:	Node to dec refcount, NULL is supported to
 *		simplify writing of callers
 *
 */
void of_node_put(struct device_node *node)
{
	if (node)
		kref_put(&node->kref, of_node_release);
}
EXPORT_SYMBOL(of_node_put);

/*
 * Plug a device node into the tree and global list.
 */
void of_attach_node(struct device_node *np)
{
	write_lock(&devtree_lock);
	np->sibling = np->parent->child;
	np->allnext = allnodes;
	np->parent->child = np;
	allnodes = np;
	write_unlock(&devtree_lock);
}

/*
 * "Unplug" a node from the device tree.  The caller must hold
 * a reference to the node.  The memory associated with the node
 * is not freed until its refcount goes to zero.
 */
void of_detach_node(struct device_node *np)
{
	struct device_node *parent;

	write_lock(&devtree_lock);

	parent = np->parent;
	if (!parent)
		goto out_unlock;

	if (allnodes == np)
		allnodes = np->allnext;
	else {
		struct device_node *prev;
		for (prev = allnodes;
		     prev->allnext != np;
		     prev = prev->allnext)
			;
		prev->allnext = np->allnext;
	}

	if (parent->child == np)
		parent->child = np->sibling;
	else {
		struct device_node *prevsib;
		for (prevsib = np->parent->child;
		     prevsib->sibling != np;
		     prevsib = prevsib->sibling)
			;
		prevsib->sibling = np->sibling;
	}

	of_node_set_flag(np, OF_DETACHED);

out_unlock:
	write_unlock(&devtree_lock);
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

	/* fix up new node's linux_phandle field */
	if ((ibm_phandle = of_get_property(node, "ibm,phandle", NULL)))
		node->linux_phandle = *ibm_phandle;

out:
	of_node_put(parent);
	return err;
}

static int prom_reconfig_notifier(struct notifier_block *nb,
				  unsigned long action, void *node)
{
	int err;

	switch (action) {
	case PSERIES_RECONFIG_ADD:
		err = of_finish_dynamic_node(node);
		if (err < 0) {
			printk(KERN_ERR "finish_node returned %d\n", err);
			err = NOTIFY_BAD;
		}
		break;
	default:
		err = NOTIFY_DONE;
		break;
	}
	return err;
}

static struct notifier_block prom_reconfig_nb = {
	.notifier_call = prom_reconfig_notifier,
	.priority = 10, /* This one needs to run first */
};

static int __init prom_reconfig_setup(void)
{
	return pSeries_reconfig_notifier_register(&prom_reconfig_nb);
}
__initcall(prom_reconfig_setup);
#endif

/*
 * Add a property to a node
 */
int prom_add_property(struct device_node* np, struct property* prop)
{
	struct property **next;

	prop->next = NULL;	
	write_lock(&devtree_lock);
	next = &np->properties;
	while (*next) {
		if (strcmp(prop->name, (*next)->name) == 0) {
			/* duplicate ! don't insert it */
			write_unlock(&devtree_lock);
			return -1;
		}
		next = &(*next)->next;
	}
	*next = prop;
	write_unlock(&devtree_lock);

#ifdef CONFIG_PROC_DEVICETREE
	/* try to add to proc as well if it was initialized */
	if (np->pde)
		proc_device_tree_add_prop(np->pde, prop);
#endif /* CONFIG_PROC_DEVICETREE */

	return 0;
}

/*
 * Remove a property from a node.  Note that we don't actually
 * remove it, since we have given out who-knows-how-many pointers
 * to the data using get-property.  Instead we just move the property
 * to the "dead properties" list, so it won't be found any more.
 */
int prom_remove_property(struct device_node *np, struct property *prop)
{
	struct property **next;
	int found = 0;

	write_lock(&devtree_lock);
	next = &np->properties;
	while (*next) {
		if (*next == prop) {
			/* found the node */
			*next = prop->next;
			prop->next = np->deadprops;
			np->deadprops = prop;
			found = 1;
			break;
		}
		next = &(*next)->next;
	}
	write_unlock(&devtree_lock);

	if (!found)
		return -ENODEV;

#ifdef CONFIG_PROC_DEVICETREE
	/* try to remove the proc node as well */
	if (np->pde)
		proc_device_tree_remove_prop(np->pde, prop);
#endif /* CONFIG_PROC_DEVICETREE */

	return 0;
}

/*
 * Update a property in a node.  Note that we don't actually
 * remove it, since we have given out who-knows-how-many pointers
 * to the data using get-property.  Instead we just move the property
 * to the "dead properties" list, and add the new property to the
 * property list
 */
int prom_update_property(struct device_node *np,
			 struct property *newprop,
			 struct property *oldprop)
{
	struct property **next;
	int found = 0;

	write_lock(&devtree_lock);
	next = &np->properties;
	while (*next) {
		if (*next == oldprop) {
			/* found the node */
			newprop->next = oldprop->next;
			*next = newprop;
			oldprop->next = np->deadprops;
			np->deadprops = oldprop;
			found = 1;
			break;
		}
		next = &(*next)->next;
	}
	write_unlock(&devtree_lock);

	if (!found)
		return -ENODEV;

#ifdef CONFIG_PROC_DEVICETREE
	/* try to add to proc as well if it was initialized */
	if (np->pde)
		proc_device_tree_update_prop(np->pde, newprop, oldprop);
#endif /* CONFIG_PROC_DEVICETREE */

	return 0;
}


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
