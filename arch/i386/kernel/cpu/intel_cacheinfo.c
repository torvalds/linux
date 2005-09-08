/*
 *      Routines to indentify caches on Intel CPU.
 *
 *      Changes:
 *      Venkatesh Pallipadi	: Adding cache identification through cpuid(4)
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/compiler.h>
#include <linux/cpu.h>

#include <asm/processor.h>
#include <asm/smp.h>

#define LVL_1_INST	1
#define LVL_1_DATA	2
#define LVL_2		3
#define LVL_3		4
#define LVL_TRACE	5

struct _cache_table
{
	unsigned char descriptor;
	char cache_type;
	short size;
};

/* all the cache descriptor types we care about (no TLB or trace cache entries) */
static struct _cache_table cache_table[] __devinitdata =
{
	{ 0x06, LVL_1_INST, 8 },	/* 4-way set assoc, 32 byte line size */
	{ 0x08, LVL_1_INST, 16 },	/* 4-way set assoc, 32 byte line size */
	{ 0x0a, LVL_1_DATA, 8 },	/* 2 way set assoc, 32 byte line size */
	{ 0x0c, LVL_1_DATA, 16 },	/* 4-way set assoc, 32 byte line size */
	{ 0x22, LVL_3,      512 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x23, LVL_3,      1024 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x25, LVL_3,      2048 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x29, LVL_3,      4096 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x2c, LVL_1_DATA, 32 },	/* 8-way set assoc, 64 byte line size */
	{ 0x30, LVL_1_INST, 32 },	/* 8-way set assoc, 64 byte line size */
	{ 0x39, LVL_2,      128 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x3b, LVL_2,      128 },	/* 2-way set assoc, sectored cache, 64 byte line size */
	{ 0x3c, LVL_2,      256 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x41, LVL_2,      128 },	/* 4-way set assoc, 32 byte line size */
	{ 0x42, LVL_2,      256 },	/* 4-way set assoc, 32 byte line size */
	{ 0x43, LVL_2,      512 },	/* 4-way set assoc, 32 byte line size */
	{ 0x44, LVL_2,      1024 },	/* 4-way set assoc, 32 byte line size */
	{ 0x45, LVL_2,      2048 },	/* 4-way set assoc, 32 byte line size */
	{ 0x60, LVL_1_DATA, 16 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x66, LVL_1_DATA, 8 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x67, LVL_1_DATA, 16 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x68, LVL_1_DATA, 32 },	/* 4-way set assoc, sectored cache, 64 byte line size */
	{ 0x70, LVL_TRACE,  12 },	/* 8-way set assoc */
	{ 0x71, LVL_TRACE,  16 },	/* 8-way set assoc */
	{ 0x72, LVL_TRACE,  32 },	/* 8-way set assoc */
	{ 0x78, LVL_2,    1024 },	/* 4-way set assoc, 64 byte line size */
	{ 0x79, LVL_2,     128 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x7a, LVL_2,     256 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x7b, LVL_2,     512 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x7c, LVL_2,    1024 },	/* 8-way set assoc, sectored cache, 64 byte line size */
	{ 0x7d, LVL_2,    2048 },	/* 8-way set assoc, 64 byte line size */
	{ 0x7f, LVL_2,     512 },	/* 2-way set assoc, 64 byte line size */
	{ 0x82, LVL_2,     256 },	/* 8-way set assoc, 32 byte line size */
	{ 0x83, LVL_2,     512 },	/* 8-way set assoc, 32 byte line size */
	{ 0x84, LVL_2,    1024 },	/* 8-way set assoc, 32 byte line size */
	{ 0x85, LVL_2,    2048 },	/* 8-way set assoc, 32 byte line size */
	{ 0x86, LVL_2,     512 },	/* 4-way set assoc, 64 byte line size */
	{ 0x87, LVL_2,    1024 },	/* 8-way set assoc, 64 byte line size */
	{ 0x00, 0, 0}
};


enum _cache_type
{
	CACHE_TYPE_NULL	= 0,
	CACHE_TYPE_DATA = 1,
	CACHE_TYPE_INST = 2,
	CACHE_TYPE_UNIFIED = 3
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

struct _cpuid4_info {
	union _cpuid4_leaf_eax eax;
	union _cpuid4_leaf_ebx ebx;
	union _cpuid4_leaf_ecx ecx;
	unsigned long size;
	cpumask_t shared_cpu_map;
};

#define MAX_CACHE_LEAVES		4
static unsigned short			num_cache_leaves;

static int __devinit cpuid4_cache_lookup(int index, struct _cpuid4_info *this_leaf)
{
	unsigned int		eax, ebx, ecx, edx;
	union _cpuid4_leaf_eax	cache_eax;

	cpuid_count(4, index, &eax, &ebx, &ecx, &edx);
	cache_eax.full = eax;
	if (cache_eax.split.type == CACHE_TYPE_NULL)
		return -EIO; /* better error ? */

	this_leaf->eax.full = eax;
	this_leaf->ebx.full = ebx;
	this_leaf->ecx.full = ecx;
	this_leaf->size = (this_leaf->ecx.split.number_of_sets + 1) *
		(this_leaf->ebx.split.coherency_line_size + 1) *
		(this_leaf->ebx.split.physical_line_partition + 1) *
		(this_leaf->ebx.split.ways_of_associativity + 1);
	return 0;
}

static int __init find_num_cache_leaves(void)
{
	unsigned int		eax, ebx, ecx, edx;
	union _cpuid4_leaf_eax	cache_eax;
	int 			i;
	int 			retval;

	retval = MAX_CACHE_LEAVES;
	/* Do cpuid(4) loop to find out num_cache_leaves */
	for (i = 0; i < MAX_CACHE_LEAVES; i++) {
		cpuid_count(4, i, &eax, &ebx, &ecx, &edx);
		cache_eax.full = eax;
		if (cache_eax.split.type == CACHE_TYPE_NULL) {
			retval = i;
			break;
		}
	}
	return retval;
}

unsigned int __devinit init_intel_cacheinfo(struct cpuinfo_x86 *c)
{
	unsigned int trace = 0, l1i = 0, l1d = 0, l2 = 0, l3 = 0; /* Cache sizes */
	unsigned int new_l1d = 0, new_l1i = 0; /* Cache sizes from cpuid(4) */
	unsigned int new_l2 = 0, new_l3 = 0, i; /* Cache sizes from cpuid(4) */

	if (c->cpuid_level > 4) {
		static int is_initialized;

		if (is_initialized == 0) {
			/* Init num_cache_leaves from boot CPU */
			num_cache_leaves = find_num_cache_leaves();
			is_initialized++;
		}

		/*
		 * Whenever possible use cpuid(4), deterministic cache
		 * parameters cpuid leaf to find the cache details
		 */
		for (i = 0; i < num_cache_leaves; i++) {
			struct _cpuid4_info this_leaf;

			int retval;

			retval = cpuid4_cache_lookup(i, &this_leaf);
			if (retval >= 0) {
				switch(this_leaf.eax.split.level) {
				    case 1:
					if (this_leaf.eax.split.type ==
							CACHE_TYPE_DATA)
						new_l1d = this_leaf.size/1024;
					else if (this_leaf.eax.split.type ==
							CACHE_TYPE_INST)
						new_l1i = this_leaf.size/1024;
					break;
				    case 2:
					new_l2 = this_leaf.size/1024;
					break;
				    case 3:
					new_l3 = this_leaf.size/1024;
					break;
				    default:
					break;
				}
			}
		}
	}
	if (c->cpuid_level > 1) {
		/* supports eax=2  call */
		int i, j, n;
		int regs[4];
		unsigned char *dp = (unsigned char *)regs;

		/* Number of times to iterate */
		n = cpuid_eax(2) & 0xFF;

		for ( i = 0 ; i < n ; i++ ) {
			cpuid(2, &regs[0], &regs[1], &regs[2], &regs[3]);

			/* If bit 31 is set, this is an unknown format */
			for ( j = 0 ; j < 3 ; j++ ) {
				if ( regs[j] < 0 ) regs[j] = 0;
			}

			/* Byte 0 is level count, not a descriptor */
			for ( j = 1 ; j < 16 ; j++ ) {
				unsigned char des = dp[j];
				unsigned char k = 0;

				/* look up this descriptor in the table */
				while (cache_table[k].descriptor != 0)
				{
					if (cache_table[k].descriptor == des) {
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

		if (new_l1d)
			l1d = new_l1d;

		if (new_l1i)
			l1i = new_l1i;

		if (new_l2)
			l2 = new_l2;

		if (new_l3)
			l3 = new_l3;

		if ( trace )
			printk (KERN_INFO "CPU: Trace cache: %dK uops", trace);
		else if ( l1i )
			printk (KERN_INFO "CPU: L1 I cache: %dK", l1i);
		if ( l1d )
			printk(", L1 D cache: %dK\n", l1d);
		else
			printk("\n");
		if ( l2 )
			printk(KERN_INFO "CPU: L2 cache: %dK\n", l2);
		if ( l3 )
			printk(KERN_INFO "CPU: L3 cache: %dK\n", l3);

		/*
		 * This assumes the L3 cache is shared; it typically lives in
		 * the northbridge.  The L1 caches are included by the L2
		 * cache, and so should not be included for the purpose of
		 * SMP switching weights.
		 */
		c->x86_cache_size = l2 ? l2 : (l1i+l1d);
	}

	return l2;
}

/* pointer to _cpuid4_info array (for each cache leaf) */
static struct _cpuid4_info *cpuid4_info[NR_CPUS];
#define CPUID4_INFO_IDX(x,y)    (&((cpuid4_info[x])[y]))

#ifdef CONFIG_SMP
static void __devinit cache_shared_cpu_map_setup(unsigned int cpu, int index)
{
	struct _cpuid4_info	*this_leaf;
	unsigned long num_threads_sharing;
#ifdef CONFIG_X86_HT
	struct cpuinfo_x86 *c = cpu_data + cpu;
#endif

	this_leaf = CPUID4_INFO_IDX(cpu, index);
	num_threads_sharing = 1 + this_leaf->eax.split.num_threads_sharing;

	if (num_threads_sharing == 1)
		cpu_set(cpu, this_leaf->shared_cpu_map);
#ifdef CONFIG_X86_HT
	else if (num_threads_sharing == smp_num_siblings)
		this_leaf->shared_cpu_map = cpu_sibling_map[cpu];
	else if (num_threads_sharing == (c->x86_num_cores * smp_num_siblings))
		this_leaf->shared_cpu_map = cpu_core_map[cpu];
	else
		printk(KERN_DEBUG "Number of CPUs sharing cache didn't match "
				"any known set of CPUs\n");
#endif
}
#else
static void __init cache_shared_cpu_map_setup(unsigned int cpu, int index) {}
#endif

static void free_cache_attributes(unsigned int cpu)
{
	kfree(cpuid4_info[cpu]);
	cpuid4_info[cpu] = NULL;
}

static int __devinit detect_cache_attributes(unsigned int cpu)
{
	struct _cpuid4_info	*this_leaf;
	unsigned long 		j;
	int 			retval;
	cpumask_t		oldmask;

	if (num_cache_leaves == 0)
		return -ENOENT;

	cpuid4_info[cpu] = kmalloc(
	    sizeof(struct _cpuid4_info) * num_cache_leaves, GFP_KERNEL);
	if (unlikely(cpuid4_info[cpu] == NULL))
		return -ENOMEM;
	memset(cpuid4_info[cpu], 0,
	    sizeof(struct _cpuid4_info) * num_cache_leaves);

	oldmask = current->cpus_allowed;
	retval = set_cpus_allowed(current, cpumask_of_cpu(cpu));
	if (retval)
		goto out;

	/* Do cpuid and store the results */
	retval = 0;
	for (j = 0; j < num_cache_leaves; j++) {
		this_leaf = CPUID4_INFO_IDX(cpu, j);
		retval = cpuid4_cache_lookup(j, this_leaf);
		if (unlikely(retval < 0))
			break;
		cache_shared_cpu_map_setup(cpu, j);
	}
	set_cpus_allowed(current, oldmask);

out:
	if (retval)
		free_cache_attributes(cpu);
	return retval;
}

#ifdef CONFIG_SYSFS

#include <linux/kobject.h>
#include <linux/sysfs.h>

extern struct sysdev_class cpu_sysdev_class; /* from drivers/base/cpu.c */

/* pointer to kobject for cpuX/cache */
static struct kobject * cache_kobject[NR_CPUS];

struct _index_kobject {
	struct kobject kobj;
	unsigned int cpu;
	unsigned short index;
};

/* pointer to array of kobjects for cpuX/cache/indexY */
static struct _index_kobject *index_kobject[NR_CPUS];
#define INDEX_KOBJECT_PTR(x,y)    (&((index_kobject[x])[y]))

#define show_one_plus(file_name, object, val)				\
static ssize_t show_##file_name						\
			(struct _cpuid4_info *this_leaf, char *buf)	\
{									\
	return sprintf (buf, "%lu\n", (unsigned long)this_leaf->object + val); \
}

show_one_plus(level, eax.split.level, 0);
show_one_plus(coherency_line_size, ebx.split.coherency_line_size, 1);
show_one_plus(physical_line_partition, ebx.split.physical_line_partition, 1);
show_one_plus(ways_of_associativity, ebx.split.ways_of_associativity, 1);
show_one_plus(number_of_sets, ecx.split.number_of_sets, 1);

static ssize_t show_size(struct _cpuid4_info *this_leaf, char *buf)
{
	return sprintf (buf, "%luK\n", this_leaf->size / 1024);
}

static ssize_t show_shared_cpu_map(struct _cpuid4_info *this_leaf, char *buf)
{
	char mask_str[NR_CPUS];
	cpumask_scnprintf(mask_str, NR_CPUS, this_leaf->shared_cpu_map);
	return sprintf(buf, "%s\n", mask_str);
}

static ssize_t show_type(struct _cpuid4_info *this_leaf, char *buf) {
	switch(this_leaf->eax.split.type) {
	    case CACHE_TYPE_DATA:
		return sprintf(buf, "Data\n");
		break;
	    case CACHE_TYPE_INST:
		return sprintf(buf, "Instruction\n");
		break;
	    case CACHE_TYPE_UNIFIED:
		return sprintf(buf, "Unified\n");
		break;
	    default:
		return sprintf(buf, "Unknown\n");
		break;
	}
}

struct _cache_attr {
	struct attribute attr;
	ssize_t (*show)(struct _cpuid4_info *, char *);
	ssize_t (*store)(struct _cpuid4_info *, const char *, size_t count);
};

#define define_one_ro(_name) \
static struct _cache_attr _name = \
	__ATTR(_name, 0444, show_##_name, NULL)

define_one_ro(level);
define_one_ro(type);
define_one_ro(coherency_line_size);
define_one_ro(physical_line_partition);
define_one_ro(ways_of_associativity);
define_one_ro(number_of_sets);
define_one_ro(size);
define_one_ro(shared_cpu_map);

static struct attribute * default_attrs[] = {
	&type.attr,
	&level.attr,
	&coherency_line_size.attr,
	&physical_line_partition.attr,
	&ways_of_associativity.attr,
	&number_of_sets.attr,
	&size.attr,
	&shared_cpu_map.attr,
	NULL
};

#define to_object(k) container_of(k, struct _index_kobject, kobj)
#define to_attr(a) container_of(a, struct _cache_attr, attr)

static ssize_t show(struct kobject * kobj, struct attribute * attr, char * buf)
{
	struct _cache_attr *fattr = to_attr(attr);
	struct _index_kobject *this_leaf = to_object(kobj);
	ssize_t ret;

	ret = fattr->show ?
		fattr->show(CPUID4_INFO_IDX(this_leaf->cpu, this_leaf->index),
			buf) :
	       	0;
	return ret;
}

static ssize_t store(struct kobject * kobj, struct attribute * attr,
		     const char * buf, size_t count)
{
	return 0;
}

static struct sysfs_ops sysfs_ops = {
	.show   = show,
	.store  = store,
};

static struct kobj_type ktype_cache = {
	.sysfs_ops	= &sysfs_ops,
	.default_attrs	= default_attrs,
};

static struct kobj_type ktype_percpu_entry = {
	.sysfs_ops	= &sysfs_ops,
};

static void cpuid4_cache_sysfs_exit(unsigned int cpu)
{
	kfree(cache_kobject[cpu]);
	kfree(index_kobject[cpu]);
	cache_kobject[cpu] = NULL;
	index_kobject[cpu] = NULL;
	free_cache_attributes(cpu);
}

static int __devinit cpuid4_cache_sysfs_init(unsigned int cpu)
{

	if (num_cache_leaves == 0)
		return -ENOENT;

	detect_cache_attributes(cpu);
	if (cpuid4_info[cpu] == NULL)
		return -ENOENT;

	/* Allocate all required memory */
	cache_kobject[cpu] = kmalloc(sizeof(struct kobject), GFP_KERNEL);
	if (unlikely(cache_kobject[cpu] == NULL))
		goto err_out;
	memset(cache_kobject[cpu], 0, sizeof(struct kobject));

	index_kobject[cpu] = kmalloc(
	    sizeof(struct _index_kobject ) * num_cache_leaves, GFP_KERNEL);
	if (unlikely(index_kobject[cpu] == NULL))
		goto err_out;
	memset(index_kobject[cpu], 0,
	    sizeof(struct _index_kobject) * num_cache_leaves);

	return 0;

err_out:
	cpuid4_cache_sysfs_exit(cpu);
	return -ENOMEM;
}

/* Add/Remove cache interface for CPU device */
static int __devinit cache_add_dev(struct sys_device * sys_dev)
{
	unsigned int cpu = sys_dev->id;
	unsigned long i, j;
	struct _index_kobject *this_object;
	int retval = 0;

	retval = cpuid4_cache_sysfs_init(cpu);
	if (unlikely(retval < 0))
		return retval;

	cache_kobject[cpu]->parent = &sys_dev->kobj;
	kobject_set_name(cache_kobject[cpu], "%s", "cache");
	cache_kobject[cpu]->ktype = &ktype_percpu_entry;
	retval = kobject_register(cache_kobject[cpu]);

	for (i = 0; i < num_cache_leaves; i++) {
		this_object = INDEX_KOBJECT_PTR(cpu,i);
		this_object->cpu = cpu;
		this_object->index = i;
		this_object->kobj.parent = cache_kobject[cpu];
		kobject_set_name(&(this_object->kobj), "index%1lu", i);
		this_object->kobj.ktype = &ktype_cache;
		retval = kobject_register(&(this_object->kobj));
		if (unlikely(retval)) {
			for (j = 0; j < i; j++) {
				kobject_unregister(
					&(INDEX_KOBJECT_PTR(cpu,j)->kobj));
			}
			kobject_unregister(cache_kobject[cpu]);
			cpuid4_cache_sysfs_exit(cpu);
			break;
		}
	}
	return retval;
}

static int __devexit cache_remove_dev(struct sys_device * sys_dev)
{
	unsigned int cpu = sys_dev->id;
	unsigned long i;

	for (i = 0; i < num_cache_leaves; i++)
		kobject_unregister(&(INDEX_KOBJECT_PTR(cpu,i)->kobj));
	kobject_unregister(cache_kobject[cpu]);
	cpuid4_cache_sysfs_exit(cpu);
	return 0;
}

static struct sysdev_driver cache_sysdev_driver = {
	.add = cache_add_dev,
	.remove = __devexit_p(cache_remove_dev),
};

/* Register/Unregister the cpu_cache driver */
static int __devinit cache_register_driver(void)
{
	if (num_cache_leaves == 0)
		return 0;

	return sysdev_driver_register(&cpu_sysdev_class,&cache_sysdev_driver);
}

device_initcall(cache_register_driver);

#endif

