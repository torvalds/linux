// SPDX-License-Identifier: GPL-2.0
/*
 * AMD L3 cache_disable_{0,1} sysfs handling
 * Documentation/ABI/testing/sysfs-devices-system-cpu
 */

#include <linux/cacheinfo.h>
#include <linux/capability.h>
#include <linux/pci.h>
#include <linux/sysfs.h>

#include <asm/amd/nb.h>

#include "cpu.h"

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
static int amd_get_l3_disable_slot(struct amd_northbridge *nb, unsigned int slot)
{
	unsigned int reg = 0;

	pci_read_config_dword(nb->misc, 0x1BC + slot * 4, &reg);

	/* check whether this slot is activated already */
	if (reg & (3UL << 30))
		return reg & 0xfff;

	return -1;
}

static ssize_t show_cache_disable(struct cacheinfo *ci, char *buf, unsigned int slot)
{
	int index;
	struct amd_northbridge *nb = ci->priv;

	index = amd_get_l3_disable_slot(nb, slot);
	if (index >= 0)
		return sysfs_emit(buf, "%d\n", index);

	return sysfs_emit(buf, "FREE\n");
}

#define SHOW_CACHE_DISABLE(slot)					\
static ssize_t								\
cache_disable_##slot##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	struct cacheinfo *ci = dev_get_drvdata(dev);			\
	return show_cache_disable(ci, buf, slot);			\
}

SHOW_CACHE_DISABLE(0)
SHOW_CACHE_DISABLE(1)

static void amd_l3_disable_index(struct amd_northbridge *nb, int cpu,
				 unsigned int slot, unsigned long idx)
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
				   unsigned int slot, unsigned long index)
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

static ssize_t store_cache_disable(struct cacheinfo *ci, const char *buf,
				   size_t count, unsigned int slot)
{
	struct amd_northbridge *nb = ci->priv;
	unsigned long val = 0;
	int cpu, err = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	cpu = cpumask_first(&ci->shared_cpu_map);

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
	struct cacheinfo *ci = dev_get_drvdata(dev);			\
	return store_cache_disable(ci, buf, count, slot);		\
}

STORE_CACHE_DISABLE(0)
STORE_CACHE_DISABLE(1)

static ssize_t subcaches_show(struct device *dev, struct device_attribute *attr,
			      char *buf)
{
	struct cacheinfo *ci = dev_get_drvdata(dev);
	int cpu = cpumask_first(&ci->shared_cpu_map);

	return sysfs_emit(buf, "%x\n", amd_get_subcaches(cpu));
}

static ssize_t subcaches_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct cacheinfo *ci = dev_get_drvdata(dev);
	int cpu = cpumask_first(&ci->shared_cpu_map);
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

static umode_t cache_private_attrs_is_visible(struct kobject *kobj,
					      struct attribute *attr, int unused)
{
	struct device *dev = kobj_to_dev(kobj);
	struct cacheinfo *ci = dev_get_drvdata(dev);
	umode_t mode = attr->mode;

	if (!ci->priv)
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
	static struct attribute **amd_l3_attrs;
	int n = 1;

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

const struct attribute_group *cache_get_priv_group(struct cacheinfo *ci)
{
	struct amd_northbridge *nb = ci->priv;

	if (ci->level < 3 || !nb)
		return NULL;

	if (nb && nb->l3_cache.indices)
		init_amd_l3_attrs();

	return &cache_private_group;
}

struct amd_northbridge *amd_init_l3_cache(int index)
{
	struct amd_northbridge *nb;
	int node;

	/* only for L3, and not in virtualized environments */
	if (index < 3)
		return NULL;

	node = topology_amd_node_id(smp_processor_id());
	nb = node_to_amd_nb(node);
	if (nb && !nb->l3_cache.indices)
		amd_calc_l3_indices(nb);

	return nb;
}
