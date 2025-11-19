// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2025 Arm Ltd.

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/acpi.h>
#include <linux/atomic.h>
#include <linux/arm_mpam.h>
#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/cacheinfo.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/srcu.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "mpam_internal.h"

/*
 * mpam_list_lock protects the SRCU lists when writing. Once the
 * mpam_enabled key is enabled these lists are read-only,
 * unless the error interrupt disables the driver.
 */
static DEFINE_MUTEX(mpam_list_lock);
static LIST_HEAD(mpam_all_msc);

struct srcu_struct mpam_srcu;

/*
 * Number of MSCs that have been probed. Once all MSCs have been probed MPAM
 * can be enabled.
 */
static atomic_t mpam_num_msc;

static int mpam_cpuhp_state;
static DEFINE_MUTEX(mpam_cpuhp_state_lock);

/*
 * The smallest common values for any CPU or MSC in the system.
 * Generating traffic outside this range will result in screaming interrupts.
 */
u16 mpam_partid_max;
u8 mpam_pmg_max;
static bool partid_max_init, partid_max_published;
static DEFINE_SPINLOCK(partid_max_lock);

/*
 * mpam is enabled once all devices have been probed from CPU online callbacks,
 * scheduled via this work_struct. If access to an MSC depends on a CPU that
 * was not brought online at boot, this can happen surprisingly late.
 */
static DECLARE_WORK(mpam_enable_work, &mpam_enable);

/*
 * All mpam error interrupts indicate a software bug. On receipt, disable the
 * driver.
 */
static DECLARE_WORK(mpam_broken_work, &mpam_disable);

/* When mpam is disabled, the printed reason to aid debugging */
static char *mpam_disable_reason;

/*
 * An MSC is a physical container for controls and monitors, each identified by
 * their RIS index. These share a base-address, interrupts and some MMIO
 * registers. A vMSC is a virtual container for RIS in an MSC that control or
 * monitor the same thing. Members of a vMSC are all RIS in the same MSC, but
 * not all RIS in an MSC share a vMSC.
 *
 * Components are a group of vMSC that control or monitor the same thing but
 * are from different MSC, so have different base-address, interrupts etc.
 * Classes are the set components of the same type.
 *
 * The features of a vMSC is the union of the RIS it contains.
 * The features of a Class and Component are the common subset of the vMSC
 * they contain.
 *
 * e.g. The system cache may have bandwidth controls on multiple interfaces,
 * for regulating traffic from devices independently of traffic from CPUs.
 * If these are two RIS in one MSC, they will be treated as controlling
 * different things, and will not share a vMSC/component/class.
 *
 * e.g. The L2 may have one MSC and two RIS, one for cache-controls another
 * for bandwidth. These two RIS are members of the same vMSC.
 *
 * e.g. The set of RIS that make up the L2 are grouped as a component. These
 * are sometimes termed slices. They should be configured the same, as if there
 * were only one.
 *
 * e.g. The SoC probably has more than one L2, each attached to a distinct set
 * of CPUs. All the L2 components are grouped as a class.
 *
 * When creating an MSC, struct mpam_msc is added to the all mpam_all_msc list,
 * then linked via struct mpam_ris to a vmsc, component and class.
 * The same MSC may exist under different class->component->vmsc paths, but the
 * RIS index will be unique.
 */
LIST_HEAD(mpam_classes);

/* List of all objects that can be free()d after synchronise_srcu() */
static LLIST_HEAD(mpam_garbage);

static inline void init_garbage(struct mpam_garbage *garbage)
{
	init_llist_node(&garbage->llist);
}

#define add_to_garbage(x)				\
do {							\
	__typeof__(x) _x = (x);				\
	_x->garbage.to_free = _x;			\
	llist_add(&_x->garbage.llist, &mpam_garbage);	\
} while (0)

static void mpam_free_garbage(void)
{
	struct mpam_garbage *iter, *tmp;
	struct llist_node *to_free = llist_del_all(&mpam_garbage);

	if (!to_free)
		return;

	synchronize_srcu(&mpam_srcu);

	llist_for_each_entry_safe(iter, tmp, to_free, llist) {
		if (iter->pdev)
			devm_kfree(&iter->pdev->dev, iter->to_free);
		else
			kfree(iter->to_free);
	}
}

static u32 __mpam_read_reg(struct mpam_msc *msc, u16 reg)
{
	WARN_ON_ONCE(!cpumask_test_cpu(smp_processor_id(), &msc->accessibility));

	return readl_relaxed(msc->mapped_hwpage + reg);
}

static inline u32 _mpam_read_partsel_reg(struct mpam_msc *msc, u16 reg)
{
	lockdep_assert_held_once(&msc->part_sel_lock);
	return __mpam_read_reg(msc, reg);
}

#define mpam_read_partsel_reg(msc, reg) _mpam_read_partsel_reg(msc, MPAMF_##reg)

static void __mpam_write_reg(struct mpam_msc *msc, u16 reg, u32 val)
{
	WARN_ON_ONCE(reg + sizeof(u32) > msc->mapped_hwpage_sz);
	WARN_ON_ONCE(!cpumask_test_cpu(smp_processor_id(), &msc->accessibility));

	writel_relaxed(val, msc->mapped_hwpage + reg);
}

static inline void _mpam_write_partsel_reg(struct mpam_msc *msc, u16 reg, u32 val)
{
	lockdep_assert_held_once(&msc->part_sel_lock);
	__mpam_write_reg(msc, reg, val);
}

#define mpam_write_partsel_reg(msc, reg, val)  _mpam_write_partsel_reg(msc, MPAMCFG_##reg, val)

static inline u32 _mpam_read_monsel_reg(struct mpam_msc *msc, u16 reg)
{
	mpam_mon_sel_lock_held(msc);
	return __mpam_read_reg(msc, reg);
}

#define mpam_read_monsel_reg(msc, reg) _mpam_read_monsel_reg(msc, MSMON_##reg)

static inline void _mpam_write_monsel_reg(struct mpam_msc *msc, u16 reg, u32 val)
{
	mpam_mon_sel_lock_held(msc);
	__mpam_write_reg(msc, reg, val);
}

#define mpam_write_monsel_reg(msc, reg, val)   _mpam_write_monsel_reg(msc, MSMON_##reg, val)

static u64 mpam_msc_read_idr(struct mpam_msc *msc)
{
	u64 idr_high = 0, idr_low;

	lockdep_assert_held(&msc->part_sel_lock);

	idr_low = mpam_read_partsel_reg(msc, IDR);
	if (FIELD_GET(MPAMF_IDR_EXT, idr_low))
		idr_high = mpam_read_partsel_reg(msc, IDR + 4);

	return (idr_high << 32) | idr_low;
}

static void __mpam_part_sel_raw(u32 partsel, struct mpam_msc *msc)
{
	lockdep_assert_held(&msc->part_sel_lock);

	mpam_write_partsel_reg(msc, PART_SEL, partsel);
}

static void __mpam_part_sel(u8 ris_idx, u16 partid, struct mpam_msc *msc)
{
	u32 partsel = FIELD_PREP(MPAMCFG_PART_SEL_RIS, ris_idx) |
		      FIELD_PREP(MPAMCFG_PART_SEL_PARTID_SEL, partid);

	__mpam_part_sel_raw(partsel, msc);
}

int mpam_register_requestor(u16 partid_max, u8 pmg_max)
{
	guard(spinlock)(&partid_max_lock);
	if (!partid_max_init) {
		mpam_partid_max = partid_max;
		mpam_pmg_max = pmg_max;
		partid_max_init = true;
	} else if (!partid_max_published) {
		mpam_partid_max = min(mpam_partid_max, partid_max);
		mpam_pmg_max = min(mpam_pmg_max, pmg_max);
	} else {
		/* New requestors can't lower the values */
		if (partid_max < mpam_partid_max || pmg_max < mpam_pmg_max)
			return -EBUSY;
	}

	return 0;
}
EXPORT_SYMBOL(mpam_register_requestor);

static struct mpam_class *
mpam_class_alloc(u8 level_idx, enum mpam_class_types type)
{
	struct mpam_class *class;

	lockdep_assert_held(&mpam_list_lock);

	class = kzalloc(sizeof(*class), GFP_KERNEL);
	if (!class)
		return ERR_PTR(-ENOMEM);
	init_garbage(&class->garbage);

	INIT_LIST_HEAD_RCU(&class->components);
	/* Affinity is updated when ris are added */
	class->level = level_idx;
	class->type = type;
	INIT_LIST_HEAD_RCU(&class->classes_list);

	list_add_rcu(&class->classes_list, &mpam_classes);

	return class;
}

static void mpam_class_destroy(struct mpam_class *class)
{
	lockdep_assert_held(&mpam_list_lock);

	list_del_rcu(&class->classes_list);
	add_to_garbage(class);
}

static struct mpam_class *
mpam_class_find(u8 level_idx, enum mpam_class_types type)
{
	struct mpam_class *class;

	lockdep_assert_held(&mpam_list_lock);

	list_for_each_entry(class, &mpam_classes, classes_list) {
		if (class->type == type && class->level == level_idx)
			return class;
	}

	return mpam_class_alloc(level_idx, type);
}

static struct mpam_component *
mpam_component_alloc(struct mpam_class *class, int id)
{
	struct mpam_component *comp;

	lockdep_assert_held(&mpam_list_lock);

	comp = kzalloc(sizeof(*comp), GFP_KERNEL);
	if (!comp)
		return ERR_PTR(-ENOMEM);
	init_garbage(&comp->garbage);

	comp->comp_id = id;
	INIT_LIST_HEAD_RCU(&comp->vmsc);
	/* Affinity is updated when RIS are added */
	INIT_LIST_HEAD_RCU(&comp->class_list);
	comp->class = class;

	list_add_rcu(&comp->class_list, &class->components);

	return comp;
}

static void mpam_component_destroy(struct mpam_component *comp)
{
	struct mpam_class *class = comp->class;

	lockdep_assert_held(&mpam_list_lock);

	list_del_rcu(&comp->class_list);
	add_to_garbage(comp);

	if (list_empty(&class->components))
		mpam_class_destroy(class);
}

static struct mpam_component *
mpam_component_find(struct mpam_class *class, int id)
{
	struct mpam_component *comp;

	lockdep_assert_held(&mpam_list_lock);

	list_for_each_entry(comp, &class->components, class_list) {
		if (comp->comp_id == id)
			return comp;
	}

	return mpam_component_alloc(class, id);
}

static struct mpam_vmsc *
mpam_vmsc_alloc(struct mpam_component *comp, struct mpam_msc *msc)
{
	struct mpam_vmsc *vmsc;

	lockdep_assert_held(&mpam_list_lock);

	vmsc = kzalloc(sizeof(*vmsc), GFP_KERNEL);
	if (!vmsc)
		return ERR_PTR(-ENOMEM);
	init_garbage(&vmsc->garbage);

	INIT_LIST_HEAD_RCU(&vmsc->ris);
	INIT_LIST_HEAD_RCU(&vmsc->comp_list);
	vmsc->comp = comp;
	vmsc->msc = msc;

	list_add_rcu(&vmsc->comp_list, &comp->vmsc);

	return vmsc;
}

static void mpam_vmsc_destroy(struct mpam_vmsc *vmsc)
{
	struct mpam_component *comp = vmsc->comp;

	lockdep_assert_held(&mpam_list_lock);

	list_del_rcu(&vmsc->comp_list);
	add_to_garbage(vmsc);

	if (list_empty(&comp->vmsc))
		mpam_component_destroy(comp);
}

static struct mpam_vmsc *
mpam_vmsc_find(struct mpam_component *comp, struct mpam_msc *msc)
{
	struct mpam_vmsc *vmsc;

	lockdep_assert_held(&mpam_list_lock);

	list_for_each_entry(vmsc, &comp->vmsc, comp_list) {
		if (vmsc->msc->id == msc->id)
			return vmsc;
	}

	return mpam_vmsc_alloc(comp, msc);
}

/*
 * The cacheinfo structures are only populated when CPUs are online.
 * This helper walks the acpi tables to include offline CPUs too.
 */
int mpam_get_cpumask_from_cache_id(unsigned long cache_id, u32 cache_level,
				   cpumask_t *affinity)
{
	return acpi_pptt_get_cpumask_from_cache_id(cache_id, affinity);
}

/*
 * cpumask_of_node() only knows about online CPUs. This can't tell us whether
 * a class is represented on all possible CPUs.
 */
static void get_cpumask_from_node_id(u32 node_id, cpumask_t *affinity)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		if (node_id == cpu_to_node(cpu))
			cpumask_set_cpu(cpu, affinity);
	}
}

static int mpam_ris_get_affinity(struct mpam_msc *msc, cpumask_t *affinity,
				 enum mpam_class_types type,
				 struct mpam_class *class,
				 struct mpam_component *comp)
{
	int err;

	switch (type) {
	case MPAM_CLASS_CACHE:
		err = mpam_get_cpumask_from_cache_id(comp->comp_id, class->level,
						     affinity);
		if (err) {
			dev_warn_once(&msc->pdev->dev,
				      "Failed to determine CPU affinity\n");
			return err;
		}

		if (cpumask_empty(affinity))
			dev_warn_once(&msc->pdev->dev, "no CPUs associated with cache node\n");

		break;
	case MPAM_CLASS_MEMORY:
		get_cpumask_from_node_id(comp->comp_id, affinity);
		/* affinity may be empty for CPU-less memory nodes */
		break;
	case MPAM_CLASS_UNKNOWN:
		return 0;
	}

	cpumask_and(affinity, affinity, &msc->accessibility);

	return 0;
}

static int mpam_ris_create_locked(struct mpam_msc *msc, u8 ris_idx,
				  enum mpam_class_types type, u8 class_id,
				  int component_id)
{
	int err;
	struct mpam_vmsc *vmsc;
	struct mpam_msc_ris *ris;
	struct mpam_class *class;
	struct mpam_component *comp;
	struct platform_device *pdev = msc->pdev;

	lockdep_assert_held(&mpam_list_lock);

	if (ris_idx > MPAM_MSC_MAX_NUM_RIS)
		return -EINVAL;

	if (test_and_set_bit(ris_idx, &msc->ris_idxs))
		return -EBUSY;

	ris = devm_kzalloc(&msc->pdev->dev, sizeof(*ris), GFP_KERNEL);
	if (!ris)
		return -ENOMEM;
	init_garbage(&ris->garbage);
	ris->garbage.pdev = pdev;

	class = mpam_class_find(class_id, type);
	if (IS_ERR(class))
		return PTR_ERR(class);

	comp = mpam_component_find(class, component_id);
	if (IS_ERR(comp)) {
		if (list_empty(&class->components))
			mpam_class_destroy(class);
		return PTR_ERR(comp);
	}

	vmsc = mpam_vmsc_find(comp, msc);
	if (IS_ERR(vmsc)) {
		if (list_empty(&comp->vmsc))
			mpam_component_destroy(comp);
		return PTR_ERR(vmsc);
	}

	err = mpam_ris_get_affinity(msc, &ris->affinity, type, class, comp);
	if (err) {
		if (list_empty(&vmsc->ris))
			mpam_vmsc_destroy(vmsc);
		return err;
	}

	ris->ris_idx = ris_idx;
	INIT_LIST_HEAD_RCU(&ris->msc_list);
	INIT_LIST_HEAD_RCU(&ris->vmsc_list);
	ris->vmsc = vmsc;

	cpumask_or(&comp->affinity, &comp->affinity, &ris->affinity);
	cpumask_or(&class->affinity, &class->affinity, &ris->affinity);
	list_add_rcu(&ris->vmsc_list, &vmsc->ris);
	list_add_rcu(&ris->msc_list, &msc->ris);

	return 0;
}

static void mpam_ris_destroy(struct mpam_msc_ris *ris)
{
	struct mpam_vmsc *vmsc = ris->vmsc;
	struct mpam_msc *msc = vmsc->msc;
	struct mpam_component *comp = vmsc->comp;
	struct mpam_class *class = comp->class;

	lockdep_assert_held(&mpam_list_lock);

	/*
	 * It is assumed affinities don't overlap. If they do the class becomes
	 * unusable immediately.
	 */
	cpumask_andnot(&class->affinity, &class->affinity, &ris->affinity);
	cpumask_andnot(&comp->affinity, &comp->affinity, &ris->affinity);
	clear_bit(ris->ris_idx, &msc->ris_idxs);
	list_del_rcu(&ris->msc_list);
	list_del_rcu(&ris->vmsc_list);
	add_to_garbage(ris);

	if (list_empty(&vmsc->ris))
		mpam_vmsc_destroy(vmsc);
}

int mpam_ris_create(struct mpam_msc *msc, u8 ris_idx,
		    enum mpam_class_types type, u8 class_id, int component_id)
{
	int err;

	mutex_lock(&mpam_list_lock);
	err = mpam_ris_create_locked(msc, ris_idx, type, class_id,
				     component_id);
	mutex_unlock(&mpam_list_lock);
	if (err)
		mpam_free_garbage();

	return err;
}

static struct mpam_msc_ris *mpam_get_or_create_ris(struct mpam_msc *msc,
						   u8 ris_idx)
{
	int err;
	struct mpam_msc_ris *ris;

	lockdep_assert_held(&mpam_list_lock);

	if (!test_bit(ris_idx, &msc->ris_idxs)) {
		err = mpam_ris_create_locked(msc, ris_idx, MPAM_CLASS_UNKNOWN,
					     0, 0);
		if (err)
			return ERR_PTR(err);
	}

	list_for_each_entry(ris, &msc->ris, msc_list) {
		if (ris->ris_idx == ris_idx)
			return ris;
	}

	return ERR_PTR(-ENOENT);
}

/*
 * IHI009A.a has this nugget: "If a monitor does not support automatic behaviour
 * of NRDY, software can use this bit for any purpose" - so hardware might not
 * implement this - but it isn't RES0.
 *
 * Try and see what values stick in this bit. If we can write either value,
 * its probably not implemented by hardware.
 */
static bool _mpam_ris_hw_probe_hw_nrdy(struct mpam_msc_ris *ris, u32 mon_reg)
{
	u32 now;
	u64 mon_sel;
	bool can_set, can_clear;
	struct mpam_msc *msc = ris->vmsc->msc;

	if (WARN_ON_ONCE(!mpam_mon_sel_lock(msc)))
		return false;

	mon_sel = FIELD_PREP(MSMON_CFG_MON_SEL_MON_SEL, 0) |
		  FIELD_PREP(MSMON_CFG_MON_SEL_RIS, ris->ris_idx);
	_mpam_write_monsel_reg(msc, mon_reg, mon_sel);

	_mpam_write_monsel_reg(msc, mon_reg, MSMON___NRDY);
	now = _mpam_read_monsel_reg(msc, mon_reg);
	can_set = now & MSMON___NRDY;

	_mpam_write_monsel_reg(msc, mon_reg, 0);
	now = _mpam_read_monsel_reg(msc, mon_reg);
	can_clear = !(now & MSMON___NRDY);
	mpam_mon_sel_unlock(msc);

	return (!can_set || !can_clear);
}

#define mpam_ris_hw_probe_hw_nrdy(_ris, _mon_reg)			\
	_mpam_ris_hw_probe_hw_nrdy(_ris, MSMON_##_mon_reg)

static void mpam_ris_hw_probe(struct mpam_msc_ris *ris)
{
	int err;
	struct mpam_msc *msc = ris->vmsc->msc;
	struct device *dev = &msc->pdev->dev;
	struct mpam_props *props = &ris->props;

	lockdep_assert_held(&msc->probe_lock);
	lockdep_assert_held(&msc->part_sel_lock);

	/* Cache Portion partitioning */
	if (FIELD_GET(MPAMF_IDR_HAS_CPOR_PART, ris->idr)) {
		u32 cpor_features = mpam_read_partsel_reg(msc, CPOR_IDR);

		props->cpbm_wd = FIELD_GET(MPAMF_CPOR_IDR_CPBM_WD, cpor_features);
		if (props->cpbm_wd)
			mpam_set_feature(mpam_feat_cpor_part, props);
	}

	/* Memory bandwidth partitioning */
	if (FIELD_GET(MPAMF_IDR_HAS_MBW_PART, ris->idr)) {
		u32 mbw_features = mpam_read_partsel_reg(msc, MBW_IDR);

		/* portion bitmap resolution */
		props->mbw_pbm_bits = FIELD_GET(MPAMF_MBW_IDR_BWPBM_WD, mbw_features);
		if (props->mbw_pbm_bits &&
		    FIELD_GET(MPAMF_MBW_IDR_HAS_PBM, mbw_features))
			mpam_set_feature(mpam_feat_mbw_part, props);

		props->bwa_wd = FIELD_GET(MPAMF_MBW_IDR_BWA_WD, mbw_features);
		if (props->bwa_wd && FIELD_GET(MPAMF_MBW_IDR_HAS_MAX, mbw_features))
			mpam_set_feature(mpam_feat_mbw_max, props);
	}

	/* Performance Monitoring */
	if (FIELD_GET(MPAMF_IDR_HAS_MSMON, ris->idr)) {
		u32 msmon_features = mpam_read_partsel_reg(msc, MSMON_IDR);

		/*
		 * If the firmware max-nrdy-us property is missing, the
		 * CSU counters can't be used. Should we wait forever?
		 */
		err = device_property_read_u32(&msc->pdev->dev,
					       "arm,not-ready-us",
					       &msc->nrdy_usec);

		if (FIELD_GET(MPAMF_MSMON_IDR_MSMON_CSU, msmon_features)) {
			u32 csumonidr;

			csumonidr = mpam_read_partsel_reg(msc, CSUMON_IDR);
			props->num_csu_mon = FIELD_GET(MPAMF_CSUMON_IDR_NUM_MON, csumonidr);
			if (props->num_csu_mon) {
				bool hw_managed;

				mpam_set_feature(mpam_feat_msmon_csu, props);

				/* Is NRDY hardware managed? */
				hw_managed = mpam_ris_hw_probe_hw_nrdy(ris, CSU);
				if (hw_managed)
					mpam_set_feature(mpam_feat_msmon_csu_hw_nrdy, props);
			}

			/*
			 * Accept the missing firmware property if NRDY appears
			 * un-implemented.
			 */
			if (err && mpam_has_feature(mpam_feat_msmon_csu_hw_nrdy, props))
				dev_err_once(dev, "Counters are not usable because not-ready timeout was not provided by firmware.");
		}
		if (FIELD_GET(MPAMF_MSMON_IDR_MSMON_MBWU, msmon_features)) {
			bool hw_managed;
			u32 mbwumon_idr = mpam_read_partsel_reg(msc, MBWUMON_IDR);

			props->num_mbwu_mon = FIELD_GET(MPAMF_MBWUMON_IDR_NUM_MON, mbwumon_idr);
			if (props->num_mbwu_mon)
				mpam_set_feature(mpam_feat_msmon_mbwu, props);

			/* Is NRDY hardware managed? */
			hw_managed = mpam_ris_hw_probe_hw_nrdy(ris, MBWU);
			if (hw_managed)
				mpam_set_feature(mpam_feat_msmon_mbwu_hw_nrdy, props);

			/*
			 * Don't warn about any missing firmware property for
			 * MBWU NRDY - it doesn't make any sense!
			 */
		}
	}
}

static int mpam_msc_hw_probe(struct mpam_msc *msc)
{
	u64 idr;
	u16 partid_max;
	u8 ris_idx, pmg_max;
	struct mpam_msc_ris *ris;
	struct device *dev = &msc->pdev->dev;

	lockdep_assert_held(&msc->probe_lock);

	idr = __mpam_read_reg(msc, MPAMF_AIDR);
	if ((idr & MPAMF_AIDR_ARCH_MAJOR_REV) != MPAM_ARCHITECTURE_V1) {
		dev_err_once(dev, "MSC does not match MPAM architecture v1.x\n");
		return -EIO;
	}

	/* Grab an IDR value to find out how many RIS there are */
	mutex_lock(&msc->part_sel_lock);
	idr = mpam_msc_read_idr(msc);
	mutex_unlock(&msc->part_sel_lock);

	msc->ris_max = FIELD_GET(MPAMF_IDR_RIS_MAX, idr);

	/* Use these values so partid/pmg always starts with a valid value */
	msc->partid_max = FIELD_GET(MPAMF_IDR_PARTID_MAX, idr);
	msc->pmg_max = FIELD_GET(MPAMF_IDR_PMG_MAX, idr);

	for (ris_idx = 0; ris_idx <= msc->ris_max; ris_idx++) {
		mutex_lock(&msc->part_sel_lock);
		__mpam_part_sel(ris_idx, 0, msc);
		idr = mpam_msc_read_idr(msc);
		mutex_unlock(&msc->part_sel_lock);

		partid_max = FIELD_GET(MPAMF_IDR_PARTID_MAX, idr);
		pmg_max = FIELD_GET(MPAMF_IDR_PMG_MAX, idr);
		msc->partid_max = min(msc->partid_max, partid_max);
		msc->pmg_max = min(msc->pmg_max, pmg_max);

		mutex_lock(&mpam_list_lock);
		ris = mpam_get_or_create_ris(msc, ris_idx);
		mutex_unlock(&mpam_list_lock);
		if (IS_ERR(ris))
			return PTR_ERR(ris);
		ris->idr = idr;

		mutex_lock(&msc->part_sel_lock);
		__mpam_part_sel(ris_idx, 0, msc);
		mpam_ris_hw_probe(ris);
		mutex_unlock(&msc->part_sel_lock);
	}

	spin_lock(&partid_max_lock);
	mpam_partid_max = min(mpam_partid_max, msc->partid_max);
	mpam_pmg_max = min(mpam_pmg_max, msc->pmg_max);
	spin_unlock(&partid_max_lock);

	msc->probed = true;

	return 0;
}

static void mpam_reset_msc_bitmap(struct mpam_msc *msc, u16 reg, u16 wd)
{
	u32 num_words, msb;
	u32 bm = ~0;
	int i;

	lockdep_assert_held(&msc->part_sel_lock);

	if (wd == 0)
		return;

	/*
	 * Write all ~0 to all but the last 32bit-word, which may
	 * have fewer bits...
	 */
	num_words = DIV_ROUND_UP(wd, 32);
	for (i = 0; i < num_words - 1; i++, reg += sizeof(bm))
		__mpam_write_reg(msc, reg, bm);

	/*
	 * ....and then the last (maybe) partial 32bit word. When wd is a
	 * multiple of 32, msb should be 31 to write a full 32bit word.
	 */
	msb = (wd - 1) % 32;
	bm = GENMASK(msb, 0);
	__mpam_write_reg(msc, reg, bm);
}

static void mpam_reset_ris_partid(struct mpam_msc_ris *ris, u16 partid)
{
	struct mpam_msc *msc = ris->vmsc->msc;
	struct mpam_props *rprops = &ris->props;

	WARN_ON_ONCE(!srcu_read_lock_held((&mpam_srcu)));

	mutex_lock(&msc->part_sel_lock);
	__mpam_part_sel(ris->ris_idx, partid, msc);

	if (mpam_has_feature(mpam_feat_cpor_part, rprops))
		mpam_reset_msc_bitmap(msc, MPAMCFG_CPBM, rprops->cpbm_wd);

	if (mpam_has_feature(mpam_feat_mbw_part, rprops))
		mpam_reset_msc_bitmap(msc, MPAMCFG_MBW_PBM, rprops->mbw_pbm_bits);

	if (mpam_has_feature(mpam_feat_mbw_min, rprops))
		mpam_write_partsel_reg(msc, MBW_MIN, 0);

	if (mpam_has_feature(mpam_feat_mbw_max, rprops))
		mpam_write_partsel_reg(msc, MBW_MAX, MPAMCFG_MBW_MAX_MAX);

	mutex_unlock(&msc->part_sel_lock);
}

/*
 * Called via smp_call_on_cpu() to prevent migration, while still being
 * pre-emptible. Caller must hold mpam_srcu.
 */
static int mpam_reset_ris(void *arg)
{
	u16 partid, partid_max;
	struct mpam_msc_ris *ris = arg;

	if (ris->in_reset_state)
		return 0;

	spin_lock(&partid_max_lock);
	partid_max = mpam_partid_max;
	spin_unlock(&partid_max_lock);
	for (partid = 0; partid <= partid_max; partid++)
		mpam_reset_ris_partid(ris, partid);

	return 0;
}

/*
 * Get the preferred CPU for this MSC. If it is accessible from this CPU,
 * this CPU is preferred. This can be preempted/migrated, it will only result
 * in more work.
 */
static int mpam_get_msc_preferred_cpu(struct mpam_msc *msc)
{
	int cpu = raw_smp_processor_id();

	if (cpumask_test_cpu(cpu, &msc->accessibility))
		return cpu;

	return cpumask_first_and(&msc->accessibility, cpu_online_mask);
}

static int mpam_touch_msc(struct mpam_msc *msc, int (*fn)(void *a), void *arg)
{
	lockdep_assert_irqs_enabled();
	lockdep_assert_cpus_held();
	WARN_ON_ONCE(!srcu_read_lock_held((&mpam_srcu)));

	return smp_call_on_cpu(mpam_get_msc_preferred_cpu(msc), fn, arg, true);
}

static void mpam_reset_msc(struct mpam_msc *msc, bool online)
{
	struct mpam_msc_ris *ris;

	list_for_each_entry_srcu(ris, &msc->ris, msc_list, srcu_read_lock_held(&mpam_srcu)) {
		mpam_touch_msc(msc, &mpam_reset_ris, ris);

		/*
		 * Set in_reset_state when coming online. The reset state
		 * for non-zero partid may be lost while the CPUs are offline.
		 */
		ris->in_reset_state = online;
	}
}

static int mpam_cpu_online(unsigned int cpu)
{
	struct mpam_msc *msc;

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(msc, &mpam_all_msc, all_msc_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		if (!cpumask_test_cpu(cpu, &msc->accessibility))
			continue;

		if (atomic_fetch_inc(&msc->online_refs) == 0)
			mpam_reset_msc(msc, true);
	}

	return 0;
}

/* Before mpam is enabled, try to probe new MSC */
static int mpam_discovery_cpu_online(unsigned int cpu)
{
	int err = 0;
	struct mpam_msc *msc;
	bool new_device_probed = false;

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(msc, &mpam_all_msc, all_msc_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		if (!cpumask_test_cpu(cpu, &msc->accessibility))
			continue;

		mutex_lock(&msc->probe_lock);
		if (!msc->probed)
			err = mpam_msc_hw_probe(msc);
		mutex_unlock(&msc->probe_lock);

		if (err)
			break;
		new_device_probed = true;
	}

	if (new_device_probed && !err)
		schedule_work(&mpam_enable_work);
	if (err) {
		mpam_disable_reason = "error during probing";
		schedule_work(&mpam_broken_work);
	}

	return err;
}

static int mpam_cpu_offline(unsigned int cpu)
{
	struct mpam_msc *msc;

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(msc, &mpam_all_msc, all_msc_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		if (!cpumask_test_cpu(cpu, &msc->accessibility))
			continue;

		if (atomic_dec_and_test(&msc->online_refs))
			mpam_reset_msc(msc, false);
	}

	return 0;
}

static void mpam_register_cpuhp_callbacks(int (*online)(unsigned int online),
					  int (*offline)(unsigned int offline),
					  char *name)
{
	mutex_lock(&mpam_cpuhp_state_lock);
	if (mpam_cpuhp_state) {
		cpuhp_remove_state(mpam_cpuhp_state);
		mpam_cpuhp_state = 0;
	}

	mpam_cpuhp_state = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, name, online,
					     offline);
	if (mpam_cpuhp_state <= 0) {
		pr_err("Failed to register cpuhp callbacks");
		mpam_cpuhp_state = 0;
	}
	mutex_unlock(&mpam_cpuhp_state_lock);
}

/*
 * An MSC can control traffic from a set of CPUs, but may only be accessible
 * from a (hopefully wider) set of CPUs. The common reason for this is power
 * management. If all the CPUs in a cluster are in PSCI:CPU_SUSPEND, the
 * corresponding cache may also be powered off. By making accesses from
 * one of those CPUs, we ensure we don't access a cache that's powered off.
 */
static void update_msc_accessibility(struct mpam_msc *msc)
{
	u32 affinity_id;
	int err;

	err = device_property_read_u32(&msc->pdev->dev, "cpu_affinity",
				       &affinity_id);
	if (err)
		cpumask_copy(&msc->accessibility, cpu_possible_mask);
	else
		acpi_pptt_get_cpus_from_container(affinity_id, &msc->accessibility);
}

/*
 * There are two ways of reaching a struct mpam_msc_ris. Via the
 * class->component->vmsc->ris, or via the msc.
 * When destroying the msc, the other side needs unlinking and cleaning up too.
 */
static void mpam_msc_destroy(struct mpam_msc *msc)
{
	struct platform_device *pdev = msc->pdev;
	struct mpam_msc_ris *ris, *tmp;

	lockdep_assert_held(&mpam_list_lock);

	list_for_each_entry_safe(ris, tmp, &msc->ris, msc_list)
		mpam_ris_destroy(ris);

	list_del_rcu(&msc->all_msc_list);
	platform_set_drvdata(pdev, NULL);

	add_to_garbage(msc);
}

static void mpam_msc_drv_remove(struct platform_device *pdev)
{
	struct mpam_msc *msc = platform_get_drvdata(pdev);

	mutex_lock(&mpam_list_lock);
	mpam_msc_destroy(msc);
	mutex_unlock(&mpam_list_lock);

	mpam_free_garbage();
}

static struct mpam_msc *do_mpam_msc_drv_probe(struct platform_device *pdev)
{
	int err;
	u32 tmp;
	struct mpam_msc *msc;
	struct resource *msc_res;
	struct device *dev = &pdev->dev;

	lockdep_assert_held(&mpam_list_lock);

	msc = devm_kzalloc(&pdev->dev, sizeof(*msc), GFP_KERNEL);
	if (!msc)
		return ERR_PTR(-ENOMEM);
	init_garbage(&msc->garbage);
	msc->garbage.pdev = pdev;

	err = devm_mutex_init(dev, &msc->probe_lock);
	if (err)
		return ERR_PTR(err);

	err = devm_mutex_init(dev, &msc->part_sel_lock);
	if (err)
		return ERR_PTR(err);

	mpam_mon_sel_lock_init(msc);
	msc->id = pdev->id;
	msc->pdev = pdev;
	INIT_LIST_HEAD_RCU(&msc->all_msc_list);
	INIT_LIST_HEAD_RCU(&msc->ris);

	update_msc_accessibility(msc);
	if (cpumask_empty(&msc->accessibility)) {
		dev_err_once(dev, "MSC is not accessible from any CPU!");
		return ERR_PTR(-EINVAL);
	}

	if (device_property_read_u32(&pdev->dev, "pcc-channel", &tmp))
		msc->iface = MPAM_IFACE_MMIO;
	else
		msc->iface = MPAM_IFACE_PCC;

	if (msc->iface == MPAM_IFACE_MMIO) {
		void __iomem *io;

		io = devm_platform_get_and_ioremap_resource(pdev, 0,
							    &msc_res);
		if (IS_ERR(io)) {
			dev_err_once(dev, "Failed to map MSC base address\n");
			return ERR_CAST(io);
		}
		msc->mapped_hwpage_sz = msc_res->end - msc_res->start;
		msc->mapped_hwpage = io;
	} else {
		return ERR_PTR(-EINVAL);
	}

	list_add_rcu(&msc->all_msc_list, &mpam_all_msc);
	platform_set_drvdata(pdev, msc);

	return msc;
}

static int fw_num_msc;

static int mpam_msc_drv_probe(struct platform_device *pdev)
{
	int err;
	struct mpam_msc *msc = NULL;
	void *plat_data = pdev->dev.platform_data;

	mutex_lock(&mpam_list_lock);
	msc = do_mpam_msc_drv_probe(pdev);
	mutex_unlock(&mpam_list_lock);

	if (IS_ERR(msc))
		return PTR_ERR(msc);

	/* Create RIS entries described by firmware */
	err = acpi_mpam_parse_resources(msc, plat_data);
	if (err) {
		mpam_msc_drv_remove(pdev);
		return err;
	}

	if (atomic_add_return(1, &mpam_num_msc) == fw_num_msc)
		mpam_register_cpuhp_callbacks(mpam_discovery_cpu_online, NULL,
					      "mpam:drv_probe");

	return 0;
}

static struct platform_driver mpam_msc_driver = {
	.driver = {
		.name = "mpam_msc",
	},
	.probe = mpam_msc_drv_probe,
	.remove = mpam_msc_drv_remove,
};

/* Any of these features mean the BWA_WD field is valid. */
static bool mpam_has_bwa_wd_feature(struct mpam_props *props)
{
	if (mpam_has_feature(mpam_feat_mbw_min, props))
		return true;
	if (mpam_has_feature(mpam_feat_mbw_max, props))
		return true;
	return false;
}

#define MISMATCHED_HELPER(parent, child, helper, field, alias)		\
	helper(parent) &&						\
	((helper(child) && (parent)->field != (child)->field) ||	\
	 (!helper(child) && !(alias)))

#define MISMATCHED_FEAT(parent, child, feat, field, alias)		     \
	mpam_has_feature((feat), (parent)) &&				     \
	((mpam_has_feature((feat), (child)) && (parent)->field != (child)->field) || \
	 (!mpam_has_feature((feat), (child)) && !(alias)))

#define CAN_MERGE_FEAT(parent, child, feat, alias)			\
	(alias) && !mpam_has_feature((feat), (parent)) &&		\
	mpam_has_feature((feat), (child))

/*
 * Combine two props fields.
 * If this is for controls that alias the same resource, it is safe to just
 * copy the values over. If two aliasing controls implement the same scheme
 * a safe value must be picked.
 * For non-aliasing controls, these control different resources, and the
 * resulting safe value must be compatible with both. When merging values in
 * the tree, all the aliasing resources must be handled first.
 * On mismatch, parent is modified.
 */
static void __props_mismatch(struct mpam_props *parent,
			     struct mpam_props *child, bool alias)
{
	if (CAN_MERGE_FEAT(parent, child, mpam_feat_cpor_part, alias)) {
		parent->cpbm_wd = child->cpbm_wd;
	} else if (MISMATCHED_FEAT(parent, child, mpam_feat_cpor_part,
				   cpbm_wd, alias)) {
		pr_debug("cleared cpor_part\n");
		mpam_clear_feature(mpam_feat_cpor_part, parent);
		parent->cpbm_wd = 0;
	}

	if (CAN_MERGE_FEAT(parent, child, mpam_feat_mbw_part, alias)) {
		parent->mbw_pbm_bits = child->mbw_pbm_bits;
	} else if (MISMATCHED_FEAT(parent, child, mpam_feat_mbw_part,
				   mbw_pbm_bits, alias)) {
		pr_debug("cleared mbw_part\n");
		mpam_clear_feature(mpam_feat_mbw_part, parent);
		parent->mbw_pbm_bits = 0;
	}

	/* bwa_wd is a count of bits, fewer bits means less precision */
	if (alias && !mpam_has_bwa_wd_feature(parent) &&
	    mpam_has_bwa_wd_feature(child)) {
		parent->bwa_wd = child->bwa_wd;
	} else if (MISMATCHED_HELPER(parent, child, mpam_has_bwa_wd_feature,
				     bwa_wd, alias)) {
		pr_debug("took the min bwa_wd\n");
		parent->bwa_wd = min(parent->bwa_wd, child->bwa_wd);
	}

	/* For num properties, take the minimum */
	if (CAN_MERGE_FEAT(parent, child, mpam_feat_msmon_csu, alias)) {
		parent->num_csu_mon = child->num_csu_mon;
	} else if (MISMATCHED_FEAT(parent, child, mpam_feat_msmon_csu,
				   num_csu_mon, alias)) {
		pr_debug("took the min num_csu_mon\n");
		parent->num_csu_mon = min(parent->num_csu_mon,
					  child->num_csu_mon);
	}

	if (CAN_MERGE_FEAT(parent, child, mpam_feat_msmon_mbwu, alias)) {
		parent->num_mbwu_mon = child->num_mbwu_mon;
	} else if (MISMATCHED_FEAT(parent, child, mpam_feat_msmon_mbwu,
				   num_mbwu_mon, alias)) {
		pr_debug("took the min num_mbwu_mon\n");
		parent->num_mbwu_mon = min(parent->num_mbwu_mon,
					   child->num_mbwu_mon);
	}

	if (alias) {
		/* Merge features for aliased resources */
		bitmap_or(parent->features, parent->features, child->features, MPAM_FEATURE_LAST);
	} else {
		/* Clear missing features for non aliasing */
		bitmap_and(parent->features, parent->features, child->features, MPAM_FEATURE_LAST);
	}
}

/*
 * If a vmsc doesn't match class feature/configuration, do the right thing(tm).
 * For 'num' properties we can just take the minimum.
 * For properties where the mismatched unused bits would make a difference, we
 * nobble the class feature, as we can't configure all the resources.
 * e.g. The L3 cache is composed of two resources with 13 and 17 portion
 * bitmaps respectively.
 */
static void
__class_props_mismatch(struct mpam_class *class, struct mpam_vmsc *vmsc)
{
	struct mpam_props *cprops = &class->props;
	struct mpam_props *vprops = &vmsc->props;
	struct device *dev = &vmsc->msc->pdev->dev;

	lockdep_assert_held(&mpam_list_lock); /* we modify class */

	dev_dbg(dev, "Merging features for class:0x%lx &= vmsc:0x%lx\n",
		(long)cprops->features, (long)vprops->features);

	/* Take the safe value for any common features */
	__props_mismatch(cprops, vprops, false);
}

static void
__vmsc_props_mismatch(struct mpam_vmsc *vmsc, struct mpam_msc_ris *ris)
{
	struct mpam_props *rprops = &ris->props;
	struct mpam_props *vprops = &vmsc->props;
	struct device *dev = &vmsc->msc->pdev->dev;

	lockdep_assert_held(&mpam_list_lock); /* we modify vmsc */

	dev_dbg(dev, "Merging features for vmsc:0x%lx |= ris:0x%lx\n",
		(long)vprops->features, (long)rprops->features);

	/*
	 * Merge mismatched features - Copy any features that aren't common,
	 * but take the safe value for any common features.
	 */
	__props_mismatch(vprops, rprops, true);
}

/*
 * Copy the first component's first vMSC's properties and features to the
 * class. __class_props_mismatch() will remove conflicts.
 * It is not possible to have a class with no components, or a component with
 * no resources. The vMSC properties have already been built.
 */
static void mpam_enable_init_class_features(struct mpam_class *class)
{
	struct mpam_vmsc *vmsc;
	struct mpam_component *comp;

	comp = list_first_entry(&class->components,
				struct mpam_component, class_list);
	vmsc = list_first_entry(&comp->vmsc,
				struct mpam_vmsc, comp_list);

	class->props = vmsc->props;
}

static void mpam_enable_merge_vmsc_features(struct mpam_component *comp)
{
	struct mpam_vmsc *vmsc;
	struct mpam_msc_ris *ris;
	struct mpam_class *class = comp->class;

	list_for_each_entry(vmsc, &comp->vmsc, comp_list) {
		list_for_each_entry(ris, &vmsc->ris, vmsc_list) {
			__vmsc_props_mismatch(vmsc, ris);
			class->nrdy_usec = max(class->nrdy_usec,
					       vmsc->msc->nrdy_usec);
		}
	}
}

static void mpam_enable_merge_class_features(struct mpam_component *comp)
{
	struct mpam_vmsc *vmsc;
	struct mpam_class *class = comp->class;

	list_for_each_entry(vmsc, &comp->vmsc, comp_list)
		__class_props_mismatch(class, vmsc);
}

/*
 * Merge all the common resource features into class.
 * vmsc features are bitwise-or'd together by mpam_enable_merge_vmsc_features()
 * as the first step so that mpam_enable_init_class_features() can initialise
 * the class with a representative set of features.
 * Next the mpam_enable_merge_class_features() bitwise-and's all the vmsc
 * features to form the class features.
 * Other features are the min/max as appropriate.
 *
 * To avoid walking the whole tree twice, the class->nrdy_usec property is
 * updated when working with the vmsc as it is a max(), and doesn't need
 * initialising first.
 */
static void mpam_enable_merge_features(struct list_head *all_classes_list)
{
	struct mpam_class *class;
	struct mpam_component *comp;

	lockdep_assert_held(&mpam_list_lock);

	list_for_each_entry(class, all_classes_list, classes_list) {
		list_for_each_entry(comp, &class->components, class_list)
			mpam_enable_merge_vmsc_features(comp);

		mpam_enable_init_class_features(class);

		list_for_each_entry(comp, &class->components, class_list)
			mpam_enable_merge_class_features(comp);
	}
}

static void mpam_enable_once(void)
{
	/*
	 * Once the cpuhp callbacks have been changed, mpam_partid_max can no
	 * longer change.
	 */
	spin_lock(&partid_max_lock);
	partid_max_published = true;
	spin_unlock(&partid_max_lock);

	mutex_lock(&mpam_list_lock);
	mpam_enable_merge_features(&mpam_classes);
	mutex_unlock(&mpam_list_lock);

	mpam_register_cpuhp_callbacks(mpam_cpu_online, mpam_cpu_offline,
				      "mpam:online");

	/* Use printk() to avoid the pr_fmt adding the function name. */
	printk(KERN_INFO "MPAM enabled with %u PARTIDs and %u PMGs\n",
	       mpam_partid_max + 1, mpam_pmg_max + 1);
}

static void mpam_reset_component_locked(struct mpam_component *comp)
{
	struct mpam_vmsc *vmsc;

	lockdep_assert_cpus_held();

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(vmsc, &comp->vmsc, comp_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		struct mpam_msc *msc = vmsc->msc;
		struct mpam_msc_ris *ris;

		list_for_each_entry_srcu(ris, &vmsc->ris, vmsc_list,
					 srcu_read_lock_held(&mpam_srcu)) {
			if (!ris->in_reset_state)
				mpam_touch_msc(msc, mpam_reset_ris, ris);
			ris->in_reset_state = true;
		}
	}
}

static void mpam_reset_class_locked(struct mpam_class *class)
{
	struct mpam_component *comp;

	lockdep_assert_cpus_held();

	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(comp, &class->components, class_list,
				 srcu_read_lock_held(&mpam_srcu))
		mpam_reset_component_locked(comp);
}

static void mpam_reset_class(struct mpam_class *class)
{
	cpus_read_lock();
	mpam_reset_class_locked(class);
	cpus_read_unlock();
}

/*
 * Called in response to an error IRQ.
 * All of MPAMs errors indicate a software bug, restore any modified
 * controls to their reset values.
 */
void mpam_disable(struct work_struct *ignored)
{
	int idx;
	struct mpam_class *class;
	struct mpam_msc *msc, *tmp;

	mutex_lock(&mpam_cpuhp_state_lock);
	if (mpam_cpuhp_state) {
		cpuhp_remove_state(mpam_cpuhp_state);
		mpam_cpuhp_state = 0;
	}
	mutex_unlock(&mpam_cpuhp_state_lock);

	idx = srcu_read_lock(&mpam_srcu);
	list_for_each_entry_srcu(class, &mpam_classes, classes_list,
				 srcu_read_lock_held(&mpam_srcu))
		mpam_reset_class(class);
	srcu_read_unlock(&mpam_srcu, idx);

	mutex_lock(&mpam_list_lock);
	list_for_each_entry_safe(msc, tmp, &mpam_all_msc, all_msc_list)
		mpam_msc_destroy(msc);
	mutex_unlock(&mpam_list_lock);
	mpam_free_garbage();

	pr_err_once("MPAM disabled due to %s\n", mpam_disable_reason);
}

/*
 * Enable mpam once all devices have been probed.
 * Scheduled by mpam_discovery_cpu_online() once all devices have been created.
 * Also scheduled when new devices are probed when new CPUs come online.
 */
void mpam_enable(struct work_struct *work)
{
	static atomic_t once;
	struct mpam_msc *msc;
	bool all_devices_probed = true;

	/* Have we probed all the hw devices? */
	guard(srcu)(&mpam_srcu);
	list_for_each_entry_srcu(msc, &mpam_all_msc, all_msc_list,
				 srcu_read_lock_held(&mpam_srcu)) {
		mutex_lock(&msc->probe_lock);
		if (!msc->probed)
			all_devices_probed = false;
		mutex_unlock(&msc->probe_lock);

		if (!all_devices_probed)
			break;
	}

	if (all_devices_probed && !atomic_fetch_inc(&once))
		mpam_enable_once();
}

static int __init mpam_msc_driver_init(void)
{
	if (!system_supports_mpam())
		return -EOPNOTSUPP;

	init_srcu_struct(&mpam_srcu);

	fw_num_msc = acpi_mpam_count_msc();
	if (fw_num_msc <= 0) {
		pr_err("No MSC devices found in firmware\n");
		return -EINVAL;
	}

	return platform_driver_register(&mpam_msc_driver);
}

/* Must occur after arm64_mpam_register_cpus() from arch_initcall() */
subsys_initcall(mpam_msc_driver_init);
