/*
 *	Routines to identify caches on Intel CPU.
 *
 *	Changes:
 *	Venkatesh Pallipadi	: Adding cache identification through cpuid(4)
 *	Ashok Raj <ashok.raj@intel.com>: Work with CPU hotplug infrastructure.
 *	Andi Kleen / Andreas Herrmann	: CPUID4 emulation on AMD.
 */

#include <linux/slab.h>
#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/sysfs.h>
#include <linux/pci.h>

#include <asm/cpufeature.h>
#include <asm/amd_nb.h>
#include <asm/smp.h>

#define LVL_1_INST	1
#define LVL_1_DATA	2
#define LVL_2		3
#define LVL_3		4
#define LVL_TRACE	5

struct _cache_table {
	unsigned char descriptor;
	char cache_type;
	short size;
};

#define MB(x)	((x) * 1024)

/* All the cache descriptor types we care about (no TLB or
   trace cache entries) */

static const struct _cache_table cache_table[] =
{
	{ 0x06, LVL_1_INST, 8 },	/* 4-way set assoc, 32 byte line size */
	{ 0x08, LVL_1_INST, 16 },	/* 4-way set assoc, 32 byte line size */
	{ 0x09, LVL_1_INST, 32 },	/* 4-way set assoc, 64 byte line size */
	{ 0x0a, LVL_1_DATA, 8 },	/* 2 way set assoc, 32 byte line size */
	{ 0x0c, LVL_1_DATA, 16 },	/* 4-way set assoc, 32 byte line size */
	{ 0x0d, LVL_1_DATA, 16 },	/* 4-way set assoc, 64 byte line size */
	{ 0x0e, LVL_1_DATA, 24 },	/* 6-way set assoc, 64 byte line size */
	{ 0x21, LVL_2,      256 },	/* 8-way set assoc, 64 byte line size */
	{ 0x22, LVL_3,      512 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x23, LVL_3,      MB(1) },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x25, LVL_3,      MB(2) },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x29, LVL_3,      MB(4) },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x2c, LVL_1_DATA, 32 },	/* 8-way set assoc, 64 byte line size */
	{ 0x30, LVL_1_INST, 32 },	/* 8-way set assoc, 64 byte line size */
	{ 0x39, LVL_2,      128 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x3a, LVL_2,      192 },	/* 6-way set assoc, sectored cache, 64 byte line size */
	{ 0x3b, LVL_2,      128 },	/* 2-way set assoc, sectored cache, 64 byte line size */
	{ 0x3c, LVL_2,      256 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x3d, LVL_2,      384 },	/* 6-way set assoc, sectored cache, 64 byte line size */
	{ 0x3e, LVL_2,      512 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x3f, LVL_2,      256 },	/* 2-way set assoc, 64 byte line size */
	{ 0x41, LVL_2,      128 },	/* 4-way set assoc, 32 byte line size */
	{ 0x42, LVL_2,      256 },	/* 4-way set assoc, 32 byte line size */
	{ 0x43, LVL_2,      512 },	/* 4-way set assoc, 32 byte line size */
	{ 0x44, LVL_2,      MB(1) },	/* 4-way set assoc, 32 byte line size */
	{ 0x45, LVL_2,      MB(2) },	/* 4-way set assoc, 32 byte line size */
	{ 0x46, LVL_3,      MB(4) },	/* 4-way set assoc, 64 byte line size */
	{ 0x47, LVL_3,      MB(8) },	/* 8-way set assoc, 64 byte line size */
	{ 0x48, LVL_2,      MB(3) },	/* 12-way set assoc, 64 byte line size */
	{ 0x49, LVL_3,      MB(4) },	/* 16-way set assoc, 64 byte line size */
	{ 0x4a, LVL_3,      MB(6) },	/* 12-way set assoc, 64 byte line size */
	{ 0x4b, LVL_3,      MB(8) },	/* 16-way set assoc, 64 byte line size */
	{ 0x4c, LVL_3,      MB(12) },	/* 12-way set assoc, 64 byte line size */
	{ 0x4d, LVL_3,      MB(16) },	/* 16-way set assoc, 64 byte line size */
	{ 0x4e, LVL_2,      MB(6) },	/* 24-way set assoc, 64 byte line size */
	{ 0x60, LVL_1_DATA, 16 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x66, LVL_1_DATA, 8 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x67, LVL_1_DATA, 16 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x68, LVL_1_DATA, 32 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x70, LVL_TRACE,  12 },	/* 8-way set assoc */
	{ 0x71, LVL_TRACE,  16 },	/* 8-way set assoc */
	{ 0x72, LVL_TRACE,  32 },	/* 8-way set assoc */
	{ 0x73, LVL_TRACE,  64 },	/* 8-way set assoc */
	{ 0x78, LVL_2,      MB(1) },	/* 4-way set assoc, 64 byte line size */
	{ 0x79, LVL_2,      128 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x7a, LVL_2,      256 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x7b, LVL_2,      512 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x7c, LVL_2,      MB(1) },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x7d, LVL_2,      MB(2) },	/* 8-way set assoc, 64 byte line size */
	{ 0x7f, LVL_2,      512 },	/* 2-way set assoc, 64 byte line size */
	{ 0x80, LVL_2,      512 },	/* 8-way set assoc, 64 byte line size */
	{ 0x82, LVL_2,      256 },	/* 8-way set assoc, 32 byte line size */
	{ 0x83, LVL_2,      512 },	/* 8-way set assoc, 32 byte line size */
	{ 0x84, LVL_2,      MB(1) },	/* 8-way set assoc, 32 byte line size */
	{ 0x85, LVL_2,      MB(2) },	/* 8-way set assoc, 32 byte line size */
	{ 0x86, LVL_2,      512 },	/* 4-way set assoc, 64 byte line size */
	{ 0x87, LVL_2,      MB(1) },	/* 8-way set assoc, 64 byte line size */
	{ 0xd0, LVL_3,      512 },	/* 4-way set assoc, 64 byte line size */
	{ 0xd1, LVL_3,      MB(1) },	/* 4-way set assoc, 64 byte line size */
	{ 0xd2, LVL_3,      MB(2) },	/* 4-way set assoc, 64 byte line size */
	{ 0xd6, LVL_3,      MB(1) },	/* 8-way set assoc, 64 byte line size */
	{ 0xd7, LVL_3,      MB(2) },	/* 8-way set assoc, 64 byte line size */
	{ 0xd8, LVL_3,      MB(4) },	/* 12-way set assoc, 64 byte line size */
	{ 0xdc, LVL_3,      MB(2) },	/* 12-way set assoc, 64 byte line size */
	{ 0xdd, LVL_3,      MB(4) },	/* 12-way set assoc, 64 byte line size */
	{ 0xde, LVL_3,      MB(8) },	/* 12-way set assoc, 64 byte line size */
	{ 0xe2, LVL_3,      MB(2) },	/* 16-way set assoc, 64 byte line size */
	{ 0xe3, LVL_3,      MB(4) },	/* 16-way set assoc, 64 byte line size */
	{ 0xe4, LVL_3,      MB(8) },	/* 16-way set assoc, 64 byte line size */
	{ 0xea, LVL_3,      MB(12) },	/* 24-way set assoc, 64 byte line size */
	{ 0xeb, LVL_3,      MB(18) },	/* 24-way set assoc, 64 byte line size */
	{ 0xec, LVL_3,      MB(24) },	/* 24-way set assoc, 64 byte line size */
	{ 0x00, 0, 0}
};


enum _cache_type {
	CTYPE_NULL = 0,
	CTYPE_DATA = 1,
	CTYPE_INST = 2,
	CTYPE_UNIFIED = 3
};

union _cpuid4_leaf_eax {
	struct {
		enum _cache_type	type:5;
		unsigned int		level:3;
		unsigned int		is_self_initializing:1;
		unsigned int		is_fully_associative:1;
		unsigned int		reserved:4;
		unsigned int		num_threads_sharing:12;
		unsigned int		num_cores_on_die:6;
	} split;
	u32 full;
};

union _cpuid4_leaf_ebx {
	struct {
		unsigned int		coherency_line_size:12;
		unsigned int		physical_line_partition:10;
		unsigned int		ways_of_associativity:10;
	} split;
	u32 full;
};

union _cpuid4_leaf_ecx {
	struct {
		unsigned int		number_of_sets:32;
	} split;
	u32 full;
};

struct _cpuid4_info_regs {
	union _cpuid4_leaf_eax eax;
	union _cpuid4_leaf_ebx ebx;
	union _cpuid4_leaf_ecx ecx;
	unsigned int id;
	unsigned long size;
	struct amd_northbridge *nb;
};

static unsigned short num_cache_leaves;

/* AMD doesn't have CPUID4. Emulate it here to report the same
   information to the user.  This makes some assumptions about the machine:
   L2 not shared, no SMT etc. that is currently true on AMD CPUs.

   In theory the TLBs could be reported as fake type (they are in "dummy").
   Maybe later */
union l1_cache {
	struct {
		unsigned line_size:8;
		unsigned lines_per_tag:8;
		unsigned assoc:8;
		unsigned size_in_kb:8;
	};
	unsigned val;
};

union l2_cache {
	struct {
		unsigned line_size:8;
		unsigned lines_per_tag:4;
		unsigned assoc:4;
		unsigned size_in_kb:16;
	};
	unsigned val;
};

union l3_cache {
	struct {
		unsigned line_size:8;
		unsigned lines_per_tag:4;
		unsigned assoc:4;
		unsigned res:2;
		unsigned size_encoded:14;
	};
	unsigned val;
};

static const unsigned short assocs[] = {
	[1] = 1,
	[2] = 2,
	[4] = 4,
	[6] = 8,
	[8] = 16,
	[0xa] = 32,
	[0xb] = 48,
	[0xc] = 64,
	[0xd] = 96,
	[0xe] = 128,
	[0xf] = 0xffff /* fully associative - no way to show this currently */
};

static const unsigned char levels[] = { 1, 1, 2, 3 };
static const unsigned char types[] = { 1, 2, 3, 3 };

static const enum cache_type cache_type_map[] = {
	[CTYPE_NULL] = CACHE_TYPE_NOCACHE,
	[CTYPE_DATA] = CACHE_TYPE_DATA,
	[CTYPE_INST] = CACHE_TYPE_INST,
	[CTYPE_UNIFIED] = CACHE_TYPE_UNIFIED,
};

static void
amd_cpuid4(int leaf, union _cpuid4_leaf_eax *eax,
		     union _cpuid4_leaf_ebx *ebx,
		     union _cpuid4_leaf_ecx *ecx)
{
	unsigned dummy;
	unsigned line_size, lines_per_tag, assoc, size_in_kb;
	union l1_cache l1i, l1d;
	union l2_cache l2;
	union l3_cache l3;
	union l1_cache *l1 = &l1d;

	eax->full = 0;
	ebx->full = 0;
	ecx->full = 0;

	cpuid(0x80000005, &dummy, &dummy, &l1d.val, &l1i.val);
	cpuid(0x80000006, &dummy, &dummy, &l2.val, &l3.val);

	switch (leaf) {
	case 1:
		l1 = &l1i;
	case 0:
		if (!l1->val)
			return;
		assoc = assocs[l1->assoc];
		line_size = l1->line_size;
		lines_per_tag = l1->lines_per_tag;
		size_in_kb = l1->size_in_kb;
		break;
	case 2:
		if (!l2.val)
			return;
		assoc = assocs[l2.assoc];
		line_size = l2.line_size;
		lines_per_tag = l2.lines_per_tag;
		/* cpu_data has errata corrections for K7 applied */
		size_in_kb = __this_cpu_read(cpu_info.x86_cache_size);
		break;
	case 3:
		if (!l3.val)
			return;
		assoc = assocs[l3.assoc];
		line_size = l3.line_size;
		lines_per_tag = l3.lines_per_tag;
		size_in_kb = l3.size_encoded * 512;
		if (boot_cpu_has(X86_FEATURE_AMD_DCM)) {
			size_in_kb = size_in_kb >> 1;
			assoc = assoc >> 1;
		}
		break;
	default:
		return;
	}

	eax->split.is_self_initializing = 1;
	eax->split.type = types[leaf];
	eax->split.level = levels[leaf];
	eax->split.num_threads_sharing = 0;
	eax->split.num_cores_on_die = __this_cpu_read(cpu_info.x86_max_cores) - 1;


	if (assoc == 0xffff)
		eax->split.is_fully_associative = 1;
	ebx->split.coherency_line_size = line_size - 1;
	ebx->split.ways_of_associativity = assoc - 1;
	ebx->split.physical_line_partition = lines_per_tag - 1;
	ecx->split.number_of_sets = (size_in_kb * 1024) / line_size /
		(ebx->split.ways_of_associativity + 1) - 1;
}

#if defined(CONFIG_AMD_NB) && defined(CONFIG_SYSFS)

/*
 * L3 cache descriptors
 */
static void amd_calc_l3_indices(struct amd_northbridge *nb)
{
	struct amd_l3_cache *l3 = &nb->l3_cache;
	unsigned int sc0, sc1, sc2, sc3;
	u32 val = 0;

	pci_read_config_dword(nb->misc, 0x1C4, &val);

	/* calculate subcache sizes */
	l3->subcaches[0] = sc0 = !(val & BIT(0));
	l3->subcaches[1] = sc1 = !(val & BIT(4));

	if (boot_cpu_data.x86 == 0x15) {
		l3->subcaches[0] = sc0 += !(val & BIT(1));
		l3->subcaches[1] = sc1 += !(val & BIT(5));
	}

	l3->subcaches[2] = sc2 = !(val & BIT(8))  + !(val & BIT(9));
	l3->subcaches[3] = sc3 = !(val & BIT(12)) + !(val & BIT(13));

	l3->indices = (max(max3(sc0, sc1, sc2), sc3) << 10) - 1;
}

/*
 * check whether a slot used for disabling an L3 index is occupied.
 * @l3: L3 cache descriptor
 * @slot: slot number (0..1)
 *
 * @returns: the disabled index if used or negative value if slot free.
 */
static int amd_get_l3_disable_slot(struct amd_northbridge *nb, unsigned slot)
{
	unsigned int reg = 0;

	pci_read_config_dword(nb->misc, 0x1BC + slot * 4, &reg);

	/* check whether this slot is activated already */
	if (reg & (3UL << 30))
		return reg & 0xfff;

	return -1;
}

static ssize_t show_cache_disable(struct cacheinfo *this_leaf, char *buf,
				  unsigned int slot)
{
	int index;
	struct amd_northbridge *nb = this_leaf->priv;

	index = amd_get_l3_disable_slot(nb, slot);
	if (index >= 0)
		return sprintf(buf, "%d\n", index);

	return sprintf(buf, "FREE\n");
}

#define SHOW_CACHE_DISABLE(slot)					\
static ssize_t								\
cache_disable_##slot##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);		\
	return show_cache_disable(this_leaf, buf, slot);		\
}
SHOW_CACHE_DISABLE(0)
SHOW_CACHE_DISABLE(1)

static void amd_l3_disable_index(struct amd_northbridge *nb, int cpu,
				 unsigned slot, unsigned long idx)
{
	int i;

	idx |= BIT(30);

	/*
	 *  disable index in all 4 subcaches
	 */
	for (i = 0; i < 4; i++) {
		u32 reg = idx | (i << 20);

		if (!nb->l3_cache.subcaches[i])
			continue;

		pci_write_config_dword(nb->misc, 0x1BC + slot * 4, reg);

		/*
		 * We need to WBINVD on a core on the node containing the L3
		 * cache which indices we disable therefore a simple wbinvd()
		 * is not sufficient.
		 */
		wbinvd_on_cpu(cpu);

		reg |= BIT(31);
		pci_write_config_dword(nb->misc, 0x1BC + slot * 4, reg);
	}
}

/*
 * disable a L3 cache index by using a disable-slot
 *
 * @l3:    L3 cache descriptor
 * @cpu:   A CPU on the node containing the L3 cache
 * @slot:  slot number (0..1)
 * @index: index to disable
 *
 * @return: 0 on success, error status on failure
 */
static int amd_set_l3_disable_slot(struct amd_northbridge *nb, int cpu,
			    unsigned slot, unsigned long index)
{
	int ret = 0;

	/*  check if @slot is already used or the index is already disabled */
	ret = amd_get_l3_disable_slot(nb, slot);
	if (ret >= 0)
		return -EEXIST;

	if (index > nb->l3_cache.indices)
		return -EINVAL;

	/* check whether the other slot has disabled the same index already */
	if (index == amd_get_l3_disable_slot(nb, !slot))
		return -EEXIST;

	amd_l3_disable_index(nb, cpu, slot, index);

	return 0;
}

static ssize_t store_cache_disable(struct cacheinfo *this_leaf,
				   const char *buf, size_t count,
				   unsigned int slot)
{
	unsigned long val = 0;
	int cpu, err = 0;
	struct amd_northbridge *nb = this_leaf->priv;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	cpu = cpumask_first(&this_leaf->shared_cpu_map);

	if (kstrtoul(buf, 10, &val) < 0)
		return -EINVAL;

	err = amd_set_l3_disable_slot(nb, cpu, slot, val);
	if (err) {
		if (err == -EEXIST)
			pr_warn("L3 slot %d in use/index already disabled!\n",
				   slot);
		return err;
	}
	return count;
}

#define STORE_CACHE_DISABLE(slot)					\
static ssize_t								\
cache_disable_##slot##_store(struct device *dev,			\
			     struct device_attribute *attr,		\
			     const char *buf, size_t count)		\
{									\
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);		\
	return store_cache_disable(this_leaf, buf, count, slot);	\
}
STORE_CACHE_DISABLE(0)
STORE_CACHE_DISABLE(1)

static ssize_t subcaches_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	int cpu = cpumask_first(&this_leaf->shared_cpu_map);

	return sprintf(buf, "%x\n", amd_get_subcaches(cpu));
}

static ssize_t subcaches_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	int cpu = cpumask_first(&this_leaf->shared_cpu_map);
	unsigned long val;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (kstrtoul(buf, 16, &val) < 0)
		return -EINVAL;

	if (amd_set_subcaches(cpu, val))
		return -EINVAL;

	return count;
}

static DEVICE_ATTR_RW(cache_disable_0);
static DEVICE_ATTR_RW(cache_disable_1);
static DEVICE_ATTR_RW(subcaches);

static umode_t
cache_private_attrs_is_visible(struct kobject *kobj,
			       struct attribute *attr, int unused)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cacheinfo *this_leaf = dev_get_drvdata(dev);
	umode_t mode = attr->mode;

	if (!this_leaf->priv)
		return 0;

	if ((attr == &dev_attr_subcaches.attr) &&
	    amd_nb_has_feature(AMD_NB_L3_PARTITIONING))
		return mode;

	if ((attr == &dev_attr_cache_disable_0.attr ||
	     attr == &dev_attr_cache_disable_1.attr) &&
	    amd_nb_has_feature(AMD_NB_L3_INDEX_DISABLE))
		return mode;

	return 0;
}

static struct attribute_group cache_private_group = {
	.is_visible = cache_private_attrs_is_visible,
};

static void init_amd_l3_attrs(void)
{
	int n = 1;
	static struct attribute **amd_l3_attrs;

	if (amd_l3_attrs) /* already initialized */
		return;

	if (amd_nb_has_feature(AMD_NB_L3_INDEX_DISABLE))
		n += 2;
	if (amd_nb_has_feature(AMD_NB_L3_PARTITIONING))
		n += 1;

	amd_l3_attrs = kcalloc(n, sizeof(*amd_l3_attrs), GFP_KERNEL);
	if (!amd_l3_attrs)
		return;

	n = 0;
	if (amd_nb_has_feature(AMD_NB_L3_INDEX_DISABLE)) {
		amd_l3_attrs[n++] = &dev_attr_cache_disable_0.attr;
		amd_l3_attrs[n++] = &dev_attr_cache_disable_1.attr;
	}
	if (amd_nb_has_feature(AMD_NB_L3_PARTITIONING))
		amd_l3_attrs[n++] = &dev_attr_subcaches.attr;

	cache_private_group.attrs = amd_l3_attrs;
}

const struct attribute_group *
cache_get_priv_group(struct cacheinfo *this_leaf)
{
	struct amd_northbridge *nb = this_leaf->priv;

	if (this_leaf->level < 3 || !nb)
		return NULL;

	if (nb && nb->l3_cache.indices)
		init_amd_l3_attrs();

	return &cache_private_group;
}

static void amd_init_l3_cache(struct _cpuid4_info_regs *this_leaf, int index)
{
	int node;

	/* only for L3, and not in virtualized environments */
	if (index < 3)
		return;

	node = amd_get_nb_id(smp_processor_id());
	this_leaf->nb = node_to_amd_nb(node);
	if (this_leaf->nb && !this_leaf->nb->l3_cache.indices)
		amd_calc_l3_indices(this_leaf->nb);
}
#else
#define amd_init_l3_cache(x, y)
#endif  /* CONFIG_AMD_NB && CONFIG_SYSFS */

static int
cpuid4_cache_lookup_regs(int index, struct _cpuid4_info_regs *this_leaf)
{
	union _cpuid4_leaf_eax	eax;
	union _cpuid4_leaf_ebx	ebx;
	union _cpuid4_leaf_ecx	ecx;
	unsigned		edx;

	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD) {
		if (boot_cpu_has(X86_FEATURE_TOPOEXT))
			cpuid_count(0x8000001d, index, &eax.full,
				    &ebx.full, &ecx.full, &edx);
		else
			amd_cpuid4(index, &eax, &ebx, &ecx);
		amd_init_l3_cache(this_leaf, index);
	} else {
		cpuid_count(4, index, &eax.full, &ebx.full, &ecx.full, &edx);
	}

	if (eax.split.type == CTYPE_NULL)
		return -EIO; /* better error ? */

	this_leaf->eax = eax;
	this_leaf->ebx = ebx;
	this_leaf->ecx = ecx;
	this_leaf->size = (ecx.split.number_of_sets          + 1) *
			  (ebx.split.coherency_line_size     + 1) *
			  (ebx.split.physical_line_partition + 1) *
			  (ebx.split.ways_of_associativity   + 1);
	return 0;
}

static int find_num_cache_leaves(struct cpuinfo_x86 *c)
{
	unsigned int		eax, ebx, ecx, edx, op;
	union _cpuid4_leaf_eax	cache_eax;
	int 			i = -1;

	if (c->x86_vendor == X86_VENDOR_AMD)
		op = 0x8000001d;
	else
		op = 4;

	do {
		++i;
		/* Do cpuid(op) loop to find out num_cache_leaves */
		cpuid_count(op, i, &eax, &ebx, &ecx, &edx);
		cache_eax.full = eax;
	} while (cache_eax.split.type != CTYPE_NULL);
	return i;
}

void init_amd_cacheinfo(struct cpuinfo_x86 *c)
{

	if (boot_cpu_has(X86_FEATURE_TOPOEXT)) {
		num_cache_leaves = find_num_cache_leaves(c);
	} else if (c->extended_cpuid_level >= 0x80000006) {
		if (cpuid_edx(0x80000006) & 0xf000)
			num_cache_leaves = 4;
		else
			num_cache_leaves = 3;
	}
}

unsigned int init_intel_cacheinfo(struct cpuinfo_x86 *c)
{
	/* Cache sizes */
	unsigned int trace = 0, l1i = 0, l1d = 0, l2 = 0, l3 = 0;
	unsigned int new_l1d = 0, new_l1i = 0; /* Cache sizes from cpuid(4) */
	unsigned int new_l2 = 0, new_l3 = 0, i; /* Cache sizes from cpuid(4) */
	unsigned int l2_id = 0, l3_id = 0, num_threads_sharing, index_msb;
#ifdef CONFIG_SMP
	unsigned int cpu = c->cpu_index;
#endif

	if (c->cpuid_level > 3) {
		static int is_initialized;

		if (is_initialized == 0) {
			/* Init num_cache_leaves from boot CPU */
			num_cache_leaves = find_num_cache_leaves(c);
			is_initialized++;
		}

		/*
		 * Whenever possible use cpuid(4), deterministic cache
		 * parameters cpuid leaf to find the cache details
		 */
		for (i = 0; i < num_cache_leaves; i++) {
			struct _cpuid4_info_regs this_leaf = {};
			int retval;

			retval = cpuid4_cache_lookup_regs(i, &this_leaf);
			if (retval < 0)
				continue;

			switch (this_leaf.eax.split.level) {
			case 1:
				if (this_leaf.eax.split.type == CTYPE_DATA)
					new_l1d = this_leaf.size/1024;
				else if (this_leaf.eax.split.type == CTYPE_INST)
					new_l1i = this_leaf.size/1024;
				break;
			case 2:
				new_l2 = this_leaf.size/1024;
				num_threads_sharing = 1 + this_leaf.eax.split.num_threads_sharing;
				index_msb = get_count_order(num_threads_sharing);
				l2_id = c->apicid & ~((1 << index_msb) - 1);
				break;
			case 3:
				new_l3 = this_leaf.size/1024;
				num_threads_sharing = 1 + this_leaf.eax.split.num_threads_sharing;
				index_msb = get_count_order(num_threads_sharing);
				l3_id = c->apicid & ~((1 << index_msb) - 1);
				break;
			default:
				break;
			}
		}
	}
	/*
	 * Don't use cpuid2 if cpuid4 is supported. For P4, we use cpuid2 for
	 * trace cache
	 */
	if ((num_cache_leaves == 0 || c->x86 == 15) && c->cpuid_level > 1) {
		/* supports eax=2  call */
		int j, n;
		unsigned int regs[4];
		unsigned char *dp = (unsigned char *)regs;
		int only_trace = 0;

		if (num_cache_leaves != 0 && c->x86 == 15)
			only_trace = 1;

		/* Number of times to iterate */
		n = cpuid_eax(2) & 0xFF;

		for (i = 0 ; i < n ; i++) {
			cpuid(2, &regs[0], &regs[1], &regs[2], &regs[3]);

			/* If bit 31 is set, this is an unknown format */
			for (j = 0 ; j < 3 ; j++)
				if (regs[j] & (1 << 31))
					regs[j] = 0;

			/* Byte 0 is level count, not a descriptor */
			for (j = 1 ; j < 16 ; j++) {
				unsigned char des = dp[j];
				unsigned char k = 0;

				/* look up this descriptor in the table */
				while (cache_table[k].descriptor != 0) {
					if (cache_table[k].descriptor == des) {
						if (only_trace && cache_table[k].cache_type != LVL_TRACE)
							break;
						switch (cache_table[k].cache_type) {
						case LVL_1_INST:
							l1i += cache_table[k].size;
							break;
						case LVL_1_DATA:
							l1d += cache_table[k].size;
							break;
						case LVL_2:
							l2 += cache_table[k].size;
							break;
						case LVL_3:
							l3 += cache_table[k].size;
							break;
						case LVL_TRACE:
							trace += cache_table[k].size;
							break;
						}

						break;
					}

					k++;
				}
			}
		}
	}

	if (new_l1d)
		l1d = new_l1d;

	if (new_l1i)
		l1i = new_l1i;

	if (new_l2) {
		l2 = new_l2;
#ifdef CONFIG_SMP
		per_cpu(cpu_llc_id, cpu) = l2_id;
#endif
	}

	if (new_l3) {
		l3 = new_l3;
#ifdef CONFIG_SMP
		per_cpu(cpu_llc_id, cpu) = l3_id;
#endif
	}

#ifdef CONFIG_SMP
	/*
	 * If cpu_llc_id is not yet set, this means cpuid_level < 4 which in
	 * turns means that the only possibility is SMT (as indicated in
	 * cpuid1). Since cpuid2 doesn't specify shared caches, and we know
	 * that SMT shares all caches, we can unconditionally set cpu_llc_id to
	 * c->phys_proc_id.
	 */
	if (per_cpu(cpu_llc_id, cpu) == BAD_APICID)
		per_cpu(cpu_llc_id, cpu) = c->phys_proc_id;
#endif

	c->x86_cache_size = l3 ? l3 : (l2 ? l2 : (l1i+l1d));

	return l2;
}

static int __cache_amd_cpumap_setup(unsigned int cpu, int index,
				    struct _cpuid4_info_regs *base)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *this_leaf;
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
			this_leaf = this_cpu_ci->info_list + index;
			for_each_cpu(sibling, cpu_llc_shared_mask(cpu)) {
				if (!cpu_online(sibling))
					continue;
				cpumask_set_cpu(sibling,
						&this_leaf->shared_cpu_map);
			}
		}
	} else if (boot_cpu_has(X86_FEATURE_TOPOEXT)) {
		unsigned int apicid, nshared, first, last;

		nshared = base->eax.split.num_threads_sharing + 1;
		apicid = cpu_data(cpu).apicid;
		first = apicid - (apicid % nshared);
		last = first + nshared - 1;

		for_each_online_cpu(i) {
			this_cpu_ci = get_cpu_cacheinfo(i);
			if (!this_cpu_ci->info_list)
				continue;

			apicid = cpu_data(i).apicid;
			if ((apicid < first) || (apicid > last))
				continue;

			this_leaf = this_cpu_ci->info_list + index;

			for_each_online_cpu(sibling) {
				apicid = cpu_data(sibling).apicid;
				if ((apicid < first) || (apicid > last))
					continue;
				cpumask_set_cpu(sibling,
						&this_leaf->shared_cpu_map);
			}
		}
	} else
		return 0;

	return 1;
}

static void __cache_cpumap_setup(unsigned int cpu, int index,
				 struct _cpuid4_info_regs *base)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *this_leaf, *sibling_leaf;
	unsigned long num_threads_sharing;
	int index_msb, i;
	struct cpuinfo_x86 *c = &cpu_data(cpu);

	if (c->x86_vendor == X86_VENDOR_AMD) {
		if (__cache_amd_cpumap_setup(cpu, index, base))
			return;
	}

	this_leaf = this_cpu_ci->info_list + index;
	num_threads_sharing = 1 + base->eax.split.num_threads_sharing;

	cpumask_set_cpu(cpu, &this_leaf->shared_cpu_map);
	if (num_threads_sharing == 1)
		return;

	index_msb = get_count_order(num_threads_sharing);

	for_each_online_cpu(i)
		if (cpu_data(i).apicid >> index_msb == c->apicid >> index_msb) {
			struct cpu_cacheinfo *sib_cpu_ci = get_cpu_cacheinfo(i);

			if (i == cpu || !sib_cpu_ci->info_list)
				continue;/* skip if itself or no cacheinfo */
			sibling_leaf = sib_cpu_ci->info_list + index;
			cpumask_set_cpu(i, &this_leaf->shared_cpu_map);
			cpumask_set_cpu(cpu, &sibling_leaf->shared_cpu_map);
		}
}

static void ci_leaf_init(struct cacheinfo *this_leaf,
			 struct _cpuid4_info_regs *base)
{
	this_leaf->id = base->id;
	this_leaf->attributes = CACHE_ID;
	this_leaf->level = base->eax.split.level;
	this_leaf->type = cache_type_map[base->eax.split.type];
	this_leaf->coherency_line_size =
				base->ebx.split.coherency_line_size + 1;
	this_leaf->ways_of_associativity =
				base->ebx.split.ways_of_associativity + 1;
	this_leaf->size = base->size;
	this_leaf->number_of_sets = base->ecx.split.number_of_sets + 1;
	this_leaf->physical_line_partition =
				base->ebx.split.physical_line_partition + 1;
	this_leaf->priv = base->nb;
}

static int __init_cache_level(unsigned int cpu)
{
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);

	if (!num_cache_leaves)
		return -ENOENT;
	if (!this_cpu_ci)
		return -EINVAL;
	this_cpu_ci->num_levels = 3;
	this_cpu_ci->num_leaves = num_cache_leaves;
	return 0;
}

/*
 * The max shared threads number comes from CPUID.4:EAX[25-14] with input
 * ECX as cache index. Then right shift apicid by the number's order to get
 * cache id for this cache node.
 */
static void get_cache_id(int cpu, struct _cpuid4_info_regs *id4_regs)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	unsigned long num_threads_sharing;
	int index_msb;

	num_threads_sharing = 1 + id4_regs->eax.split.num_threads_sharing;
	index_msb = get_count_order(num_threads_sharing);
	id4_regs->id = c->apicid >> index_msb;
}

static int __populate_cache_leaves(unsigned int cpu)
{
	unsigned int idx, ret;
	struct cpu_cacheinfo *this_cpu_ci = get_cpu_cacheinfo(cpu);
	struct cacheinfo *this_leaf = this_cpu_ci->info_list;
	struct _cpuid4_info_regs id4_regs = {};

	for (idx = 0; idx < this_cpu_ci->num_leaves; idx++) {
		ret = cpuid4_cache_lookup_regs(idx, &id4_regs);
		if (ret)
			return ret;
		get_cache_id(cpu, &id4_regs);
		ci_leaf_init(this_leaf++, &id4_regs);
		__cache_cpumap_setup(cpu, idx, &id4_regs);
	}
	this_cpu_ci->cpu_map_populated = true;

	return 0;
}

DEFINE_SMP_CALL_CACHE_FUNCTION(init_cache_level)
DEFINE_SMP_CALL_CACHE_FUNCTION(populate_cache_leaves)
