/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2025 Arm Ltd.

#ifndef MPAM_INTERNAL_H
#define MPAM_INTERNAL_H

#include <linux/arm_mpam.h>
#include <linux/atomic.h>
#include <linux/bitmap.h>
#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/jump_label.h>
#include <linux/llist.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/srcu.h>
#include <linux/types.h>

#define MPAM_MSC_MAX_NUM_RIS	16

struct platform_device;

DECLARE_STATIC_KEY_FALSE(mpam_enabled);

#ifdef CONFIG_MPAM_KUNIT_TEST
#define PACKED_FOR_KUNIT __packed
#else
#define PACKED_FOR_KUNIT
#endif

static inline bool mpam_is_enabled(void)
{
	return static_branch_likely(&mpam_enabled);
}

/*
 * Structures protected by SRCU may not be freed for a surprising amount of
 * time (especially if perf is running). To ensure the MPAM error interrupt can
 * tear down all the structures, build a list of objects that can be garbage
 * collected once synchronize_srcu() has returned.
 * If pdev is non-NULL, use devm_kfree().
 */
struct mpam_garbage {
	/* member of mpam_garbage */
	struct llist_node	llist;

	void			*to_free;
	struct platform_device	*pdev;
};

struct mpam_msc {
	/* member of mpam_all_msc */
	struct list_head	all_msc_list;

	int			id;
	struct platform_device	*pdev;

	/* Not modified after mpam_is_enabled() becomes true */
	enum mpam_msc_iface	iface;
	u32			nrdy_usec;
	cpumask_t		accessibility;
	bool			has_extd_esr;

	int				reenable_error_ppi;
	struct mpam_msc * __percpu	*error_dev_id;

	atomic_t		online_refs;

	/*
	 * probe_lock is only taken during discovery. After discovery these
	 * properties become read-only and the lists are protected by SRCU.
	 */
	struct mutex		probe_lock;
	bool			probed;
	u16			partid_max;
	u8			pmg_max;
	unsigned long		ris_idxs;
	u32			ris_max;

	/*
	 * error_irq_lock is taken when registering/unregistering the error
	 * interrupt and maniupulating the below flags.
	 */
	struct mutex		error_irq_lock;
	bool			error_irq_req;
	bool			error_irq_hw_enabled;

	/* mpam_msc_ris of this component */
	struct list_head	ris;

	/*
	 * part_sel_lock protects access to the MSC hardware registers that are
	 * affected by MPAMCFG_PART_SEL. (including the ID registers that vary
	 * by RIS).
	 * If needed, take msc->probe_lock first.
	 */
	struct mutex		part_sel_lock;

	/*
	 * cfg_lock protects the msc configuration and guards against mbwu_state
	 * save and restore racing.
	 */
	struct mutex		cfg_lock;

	/*
	 * mon_sel_lock protects access to the MSC hardware registers that are
	 * affected by MPAMCFG_MON_SEL, and the mbwu_state.
	 * Access to mon_sel is needed from both process and interrupt contexts,
	 * but is complicated by firmware-backed platforms that can't make any
	 * access unless they can sleep.
	 * Always use the mpam_mon_sel_lock() helpers.
	 * Accesses to mon_sel need to be able to fail if they occur in the wrong
	 * context.
	 * If needed, take msc->probe_lock first.
	 */
	raw_spinlock_t		_mon_sel_lock;
	unsigned long		_mon_sel_flags;

	void __iomem		*mapped_hwpage;
	size_t			mapped_hwpage_sz;

	struct mpam_garbage	garbage;
};

/* Returning false here means accesses to mon_sel must fail and report an error. */
static inline bool __must_check mpam_mon_sel_lock(struct mpam_msc *msc)
{
	/* Locking will require updating to support a firmware backed interface */
	if (WARN_ON_ONCE(msc->iface != MPAM_IFACE_MMIO))
		return false;

	raw_spin_lock_irqsave(&msc->_mon_sel_lock, msc->_mon_sel_flags);
	return true;
}

static inline void mpam_mon_sel_unlock(struct mpam_msc *msc)
{
	raw_spin_unlock_irqrestore(&msc->_mon_sel_lock, msc->_mon_sel_flags);
}

static inline void mpam_mon_sel_lock_held(struct mpam_msc *msc)
{
	lockdep_assert_held_once(&msc->_mon_sel_lock);
}

static inline void mpam_mon_sel_lock_init(struct mpam_msc *msc)
{
	raw_spin_lock_init(&msc->_mon_sel_lock);
}

/* Bits for mpam features bitmaps */
enum mpam_device_features {
	mpam_feat_cpor_part,
	mpam_feat_cmax_softlim,
	mpam_feat_cmax_cmax,
	mpam_feat_cmax_cmin,
	mpam_feat_cmax_cassoc,
	mpam_feat_mbw_part,
	mpam_feat_mbw_min,
	mpam_feat_mbw_max,
	mpam_feat_mbw_prop,
	mpam_feat_intpri_part,
	mpam_feat_intpri_part_0_low,
	mpam_feat_dspri_part,
	mpam_feat_dspri_part_0_low,
	mpam_feat_msmon,
	mpam_feat_msmon_csu,
	mpam_feat_msmon_csu_capture,
	mpam_feat_msmon_csu_xcl,
	mpam_feat_msmon_csu_hw_nrdy,
	mpam_feat_msmon_mbwu,
	mpam_feat_msmon_mbwu_31counter,
	mpam_feat_msmon_mbwu_44counter,
	mpam_feat_msmon_mbwu_63counter,
	mpam_feat_msmon_mbwu_capture,
	mpam_feat_msmon_mbwu_rwbw,
	mpam_feat_msmon_mbwu_hw_nrdy,
	mpam_feat_partid_nrw,
	MPAM_FEATURE_LAST
};

struct mpam_props {
	DECLARE_BITMAP(features, MPAM_FEATURE_LAST);

	u16			cpbm_wd;
	u16			mbw_pbm_bits;
	u16			bwa_wd;
	u16			cmax_wd;
	u16			cassoc_wd;
	u16			intpri_wd;
	u16			dspri_wd;
	u16			num_csu_mon;
	u16			num_mbwu_mon;

/*
 * Kunit tests use memset() to set up feature combinations that should be
 * removed, and will false-positive if the compiler introduces padding that
 * isn't cleared during sanitisation.
 */
} PACKED_FOR_KUNIT;

#define mpam_has_feature(_feat, x)	test_bit(_feat, (x)->features)
/*
 * The non-atomic get/set operations are used because if struct mpam_props is
 * packed, the alignment requirements for atomics aren't met.
 */
#define mpam_set_feature(_feat, x)	__set_bit(_feat, (x)->features)
#define mpam_clear_feature(_feat, x)	__clear_bit(_feat, (x)->features)

/* The values for MSMON_CFG_MBWU_FLT.RWBW */
enum mon_filter_options {
	COUNT_BOTH	= 0,
	COUNT_WRITE	= 1,
	COUNT_READ	= 2,
};

struct mon_cfg {
	u16			mon;
	u8			pmg;
	bool			match_pmg;
	bool			csu_exclude_clean;
	u32			partid;
	enum mon_filter_options opts;
};

/* Changes to msmon_mbwu_state are protected by the msc's mon_sel_lock. */
struct msmon_mbwu_state {
	bool		enabled;
	bool		reset_on_next_read;
	struct mon_cfg	cfg;

	/*
	 * The value to add to the new reading to account for power management,
	 * and overflow.
	 */
	u64		correction;

	struct mpam_garbage	garbage;
};

struct mpam_class {
	/* mpam_components in this class */
	struct list_head	components;

	cpumask_t		affinity;

	struct mpam_props	props;
	u32			nrdy_usec;
	u8			level;
	enum mpam_class_types	type;

	/* member of mpam_classes */
	struct list_head	classes_list;

	struct ida		ida_csu_mon;
	struct ida		ida_mbwu_mon;

	struct mpam_garbage	garbage;
};

struct mpam_config {
	/* Which configuration values are valid. */
	DECLARE_BITMAP(features, MPAM_FEATURE_LAST);

	u32	cpbm;
	u32	mbw_pbm;
	u16	mbw_max;

	bool	reset_cpbm;
	bool	reset_mbw_pbm;
	bool	reset_mbw_max;

	struct mpam_garbage	garbage;
};

struct mpam_component {
	u32			comp_id;

	/* mpam_vmsc in this component */
	struct list_head	vmsc;

	cpumask_t		affinity;

	/*
	 * Array of configuration values, indexed by partid.
	 * Read from cpuhp callbacks, hold the cpuhp lock when writing.
	 */
	struct mpam_config	*cfg;

	/* member of mpam_class:components */
	struct list_head	class_list;

	/* parent: */
	struct mpam_class	*class;

	struct mpam_garbage	garbage;
};

struct mpam_vmsc {
	/* member of mpam_component:vmsc_list */
	struct list_head	comp_list;

	/* mpam_msc_ris in this vmsc */
	struct list_head	ris;

	struct mpam_props	props;

	/* All RIS in this vMSC are members of this MSC */
	struct mpam_msc		*msc;

	/* parent: */
	struct mpam_component	*comp;

	struct mpam_garbage	garbage;
};

struct mpam_msc_ris {
	u8			ris_idx;
	u64			idr;
	struct mpam_props	props;
	bool			in_reset_state;

	cpumask_t		affinity;

	/* member of mpam_vmsc:ris */
	struct list_head	vmsc_list;

	/* member of mpam_msc:ris */
	struct list_head	msc_list;

	/* parent: */
	struct mpam_vmsc	*vmsc;

	/* msmon mbwu configuration is preserved over reset */
	struct msmon_mbwu_state	*mbwu_state;

	struct mpam_garbage	garbage;
};

static inline int mpam_alloc_csu_mon(struct mpam_class *class)
{
	struct mpam_props *cprops = &class->props;

	if (!mpam_has_feature(mpam_feat_msmon_csu, cprops))
		return -EOPNOTSUPP;

	return ida_alloc_max(&class->ida_csu_mon, cprops->num_csu_mon - 1,
			     GFP_KERNEL);
}

static inline void mpam_free_csu_mon(struct mpam_class *class, int csu_mon)
{
	ida_free(&class->ida_csu_mon, csu_mon);
}

static inline int mpam_alloc_mbwu_mon(struct mpam_class *class)
{
	struct mpam_props *cprops = &class->props;

	if (!mpam_has_feature(mpam_feat_msmon_mbwu, cprops))
		return -EOPNOTSUPP;

	return ida_alloc_max(&class->ida_mbwu_mon, cprops->num_mbwu_mon - 1,
			     GFP_KERNEL);
}

static inline void mpam_free_mbwu_mon(struct mpam_class *class, int mbwu_mon)
{
	ida_free(&class->ida_mbwu_mon, mbwu_mon);
}

/* List of all classes - protected by srcu*/
extern struct srcu_struct mpam_srcu;
extern struct list_head mpam_classes;

/* System wide partid/pmg values */
extern u16 mpam_partid_max;
extern u8 mpam_pmg_max;

/* Scheduled work callback to enable mpam once all MSC have been probed */
void mpam_enable(struct work_struct *work);
void mpam_disable(struct work_struct *work);

int mpam_apply_config(struct mpam_component *comp, u16 partid,
		      struct mpam_config *cfg);

int mpam_msmon_read(struct mpam_component *comp, struct mon_cfg *ctx,
		    enum mpam_device_features, u64 *val);
void mpam_msmon_reset_mbwu(struct mpam_component *comp, struct mon_cfg *ctx);

int mpam_get_cpumask_from_cache_id(unsigned long cache_id, u32 cache_level,
				   cpumask_t *affinity);

/*
 * MPAM MSCs have the following register layout. See:
 * Arm Memory System Resource Partitioning and Monitoring (MPAM) System
 * Component Specification.
 * https://developer.arm.com/documentation/ihi0099/aa/
 */
#define MPAM_ARCHITECTURE_V1    0x10

/* Memory mapped control pages */
/* ID Register offsets in the memory mapped page */
#define MPAMF_IDR		0x0000  /* features id register */
#define MPAMF_IIDR		0x0018  /* implementer id register */
#define MPAMF_AIDR		0x0020  /* architectural id register */
#define MPAMF_IMPL_IDR		0x0028  /* imp-def partitioning */
#define MPAMF_CPOR_IDR		0x0030  /* cache-portion partitioning */
#define MPAMF_CCAP_IDR		0x0038  /* cache-capacity partitioning */
#define MPAMF_MBW_IDR		0x0040  /* mem-bw partitioning */
#define MPAMF_PRI_IDR		0x0048  /* priority partitioning */
#define MPAMF_MSMON_IDR		0x0080  /* performance monitoring features */
#define MPAMF_CSUMON_IDR	0x0088  /* cache-usage monitor */
#define MPAMF_MBWUMON_IDR	0x0090  /* mem-bw usage monitor */
#define MPAMF_PARTID_NRW_IDR	0x0050  /* partid-narrowing */

/* Configuration and Status Register offsets in the memory mapped page */
#define MPAMCFG_PART_SEL	0x0100  /* partid to configure */
#define MPAMCFG_CPBM		0x1000  /* cache-portion config */
#define MPAMCFG_CMAX		0x0108  /* cache-capacity config */
#define MPAMCFG_CMIN		0x0110  /* cache-capacity config */
#define MPAMCFG_CASSOC		0x0118  /* cache-associativity config */
#define MPAMCFG_MBW_MIN		0x0200  /* min mem-bw config */
#define MPAMCFG_MBW_MAX		0x0208  /* max mem-bw config */
#define MPAMCFG_MBW_WINWD	0x0220  /* mem-bw accounting window config */
#define MPAMCFG_MBW_PBM		0x2000  /* mem-bw portion bitmap config */
#define MPAMCFG_PRI		0x0400  /* priority partitioning config */
#define MPAMCFG_MBW_PROP	0x0500  /* mem-bw stride config */
#define MPAMCFG_INTPARTID	0x0600  /* partid-narrowing config */

#define MSMON_CFG_MON_SEL	0x0800  /* monitor selector */
#define MSMON_CFG_CSU_FLT	0x0810  /* cache-usage monitor filter */
#define MSMON_CFG_CSU_CTL	0x0818  /* cache-usage monitor config */
#define MSMON_CFG_MBWU_FLT	0x0820  /* mem-bw monitor filter */
#define MSMON_CFG_MBWU_CTL	0x0828  /* mem-bw monitor config */
#define MSMON_CSU		0x0840  /* current cache-usage */
#define MSMON_CSU_CAPTURE	0x0848  /* last cache-usage value captured */
#define MSMON_MBWU		0x0860  /* current mem-bw usage value */
#define MSMON_MBWU_CAPTURE	0x0868  /* last mem-bw value captured */
#define MSMON_MBWU_L		0x0880  /* current long mem-bw usage value */
#define MSMON_MBWU_L_CAPTURE	0x0890  /* last long mem-bw value captured */
#define MSMON_CAPT_EVNT		0x0808  /* signal a capture event */
#define MPAMF_ESR		0x00F8  /* error status register */
#define MPAMF_ECR		0x00F0  /* error control register */

/* MPAMF_IDR - MPAM features ID register */
#define MPAMF_IDR_PARTID_MAX		GENMASK(15, 0)
#define MPAMF_IDR_PMG_MAX		GENMASK(23, 16)
#define MPAMF_IDR_HAS_CCAP_PART		BIT(24)
#define MPAMF_IDR_HAS_CPOR_PART		BIT(25)
#define MPAMF_IDR_HAS_MBW_PART		BIT(26)
#define MPAMF_IDR_HAS_PRI_PART		BIT(27)
#define MPAMF_IDR_EXT			BIT(28)
#define MPAMF_IDR_HAS_IMPL_IDR		BIT(29)
#define MPAMF_IDR_HAS_MSMON		BIT(30)
#define MPAMF_IDR_HAS_PARTID_NRW	BIT(31)
#define MPAMF_IDR_HAS_RIS		BIT(32)
#define MPAMF_IDR_HAS_EXTD_ESR		BIT(38)
#define MPAMF_IDR_HAS_ESR		BIT(39)
#define MPAMF_IDR_RIS_MAX		GENMASK(59, 56)

/* MPAMF_MSMON_IDR - MPAM performance monitoring ID register */
#define MPAMF_MSMON_IDR_MSMON_CSU		BIT(16)
#define MPAMF_MSMON_IDR_MSMON_MBWU		BIT(17)
#define MPAMF_MSMON_IDR_HAS_LOCAL_CAPT_EVNT	BIT(31)

/* MPAMF_CPOR_IDR - MPAM features cache portion partitioning ID register */
#define MPAMF_CPOR_IDR_CPBM_WD			GENMASK(15, 0)

/* MPAMF_CCAP_IDR - MPAM features cache capacity partitioning ID register */
#define MPAMF_CCAP_IDR_CMAX_WD			GENMASK(5, 0)
#define MPAMF_CCAP_IDR_CASSOC_WD		GENMASK(12, 8)
#define MPAMF_CCAP_IDR_HAS_CASSOC		BIT(28)
#define MPAMF_CCAP_IDR_HAS_CMIN			BIT(29)
#define MPAMF_CCAP_IDR_NO_CMAX			BIT(30)
#define MPAMF_CCAP_IDR_HAS_CMAX_SOFTLIM		BIT(31)

/* MPAMF_MBW_IDR - MPAM features memory bandwidth partitioning ID register */
#define MPAMF_MBW_IDR_BWA_WD		GENMASK(5, 0)
#define MPAMF_MBW_IDR_HAS_MIN		BIT(10)
#define MPAMF_MBW_IDR_HAS_MAX		BIT(11)
#define MPAMF_MBW_IDR_HAS_PBM		BIT(12)
#define MPAMF_MBW_IDR_HAS_PROP		BIT(13)
#define MPAMF_MBW_IDR_WINDWR		BIT(14)
#define MPAMF_MBW_IDR_BWPBM_WD		GENMASK(28, 16)

/* MPAMF_PRI_IDR - MPAM features priority partitioning ID register */
#define MPAMF_PRI_IDR_HAS_INTPRI	BIT(0)
#define MPAMF_PRI_IDR_INTPRI_0_IS_LOW	BIT(1)
#define MPAMF_PRI_IDR_INTPRI_WD		GENMASK(9, 4)
#define MPAMF_PRI_IDR_HAS_DSPRI		BIT(16)
#define MPAMF_PRI_IDR_DSPRI_0_IS_LOW	BIT(17)
#define MPAMF_PRI_IDR_DSPRI_WD		GENMASK(25, 20)

/* MPAMF_CSUMON_IDR - MPAM cache storage usage monitor ID register */
#define MPAMF_CSUMON_IDR_NUM_MON	GENMASK(15, 0)
#define MPAMF_CSUMON_IDR_HAS_OFLOW_CAPT	BIT(24)
#define MPAMF_CSUMON_IDR_HAS_CEVNT_OFLW	BIT(25)
#define MPAMF_CSUMON_IDR_HAS_OFSR	BIT(26)
#define MPAMF_CSUMON_IDR_HAS_OFLOW_LNKG	BIT(27)
#define MPAMF_CSUMON_IDR_HAS_XCL	BIT(29)
#define MPAMF_CSUMON_IDR_CSU_RO		BIT(30)
#define MPAMF_CSUMON_IDR_HAS_CAPTURE	BIT(31)

/* MPAMF_MBWUMON_IDR - MPAM memory bandwidth usage monitor ID register */
#define MPAMF_MBWUMON_IDR_NUM_MON	GENMASK(15, 0)
#define MPAMF_MBWUMON_IDR_HAS_RWBW	BIT(28)
#define MPAMF_MBWUMON_IDR_LWD		BIT(29)
#define MPAMF_MBWUMON_IDR_HAS_LONG	BIT(30)
#define MPAMF_MBWUMON_IDR_HAS_CAPTURE	BIT(31)

/* MPAMF_PARTID_NRW_IDR - MPAM PARTID narrowing ID register */
#define MPAMF_PARTID_NRW_IDR_INTPARTID_MAX	GENMASK(15, 0)

/* MPAMF_IIDR - MPAM implementation ID register */
#define MPAMF_IIDR_IMPLEMENTER	GENMASK(11, 0)
#define MPAMF_IIDR_REVISION	GENMASK(15, 12)
#define MPAMF_IIDR_VARIANT	GENMASK(19, 16)
#define MPAMF_IIDR_PRODUCTID	GENMASK(31, 20)

/* MPAMF_AIDR - MPAM architecture ID register */
#define MPAMF_AIDR_ARCH_MINOR_REV	GENMASK(3, 0)
#define MPAMF_AIDR_ARCH_MAJOR_REV	GENMASK(7, 4)

/* MPAMCFG_PART_SEL - MPAM partition configuration selection register */
#define MPAMCFG_PART_SEL_PARTID_SEL	GENMASK(15, 0)
#define MPAMCFG_PART_SEL_INTERNAL	BIT(16)
#define MPAMCFG_PART_SEL_RIS		GENMASK(27, 24)

/* MPAMCFG_CASSOC - MPAM cache maximum associativity partition configuration register */
#define MPAMCFG_CASSOC_CASSOC		GENMASK(15, 0)

/* MPAMCFG_CMAX - MPAM cache capacity configuration register */
#define MPAMCFG_CMAX_SOFTLIM		BIT(31)
#define MPAMCFG_CMAX_CMAX		GENMASK(15, 0)

/* MPAMCFG_CMIN - MPAM cache capacity configuration register */
#define MPAMCFG_CMIN_CMIN		GENMASK(15, 0)

/*
 * MPAMCFG_MBW_MIN - MPAM memory minimum bandwidth partitioning configuration
 *                   register
 */
#define MPAMCFG_MBW_MIN_MIN		GENMASK(15, 0)

/*
 * MPAMCFG_MBW_MAX - MPAM memory maximum bandwidth partitioning configuration
 *                   register
 */
#define MPAMCFG_MBW_MAX_MAX		GENMASK(15, 0)
#define MPAMCFG_MBW_MAX_HARDLIM		BIT(31)

/*
 * MPAMCFG_MBW_WINWD - MPAM memory bandwidth partitioning window width
 *                     register
 */
#define MPAMCFG_MBW_WINWD_US_FRAC	GENMASK(7, 0)
#define MPAMCFG_MBW_WINWD_US_INT	GENMASK(23, 8)

/* MPAMCFG_PRI - MPAM priority partitioning configuration register */
#define MPAMCFG_PRI_INTPRI		GENMASK(15, 0)
#define MPAMCFG_PRI_DSPRI		GENMASK(31, 16)

/*
 * MPAMCFG_MBW_PROP - Memory bandwidth proportional stride partitioning
 *                    configuration register
 */
#define MPAMCFG_MBW_PROP_STRIDEM1	GENMASK(15, 0)
#define MPAMCFG_MBW_PROP_EN		BIT(31)

/*
 * MPAMCFG_INTPARTID - MPAM internal partition narrowing configuration register
 */
#define MPAMCFG_INTPARTID_INTPARTID	GENMASK(15, 0)
#define MPAMCFG_INTPARTID_INTERNAL	BIT(16)

/* MSMON_CFG_MON_SEL - Memory system performance monitor selection register */
#define MSMON_CFG_MON_SEL_MON_SEL	GENMASK(15, 0)
#define MSMON_CFG_MON_SEL_RIS		GENMASK(27, 24)

/* MPAMF_ESR - MPAM Error Status Register */
#define MPAMF_ESR_PARTID_MON	GENMASK(15, 0)
#define MPAMF_ESR_PMG		GENMASK(23, 16)
#define MPAMF_ESR_ERRCODE	GENMASK(27, 24)
#define MPAMF_ESR_OVRWR		BIT(31)
#define MPAMF_ESR_RIS		GENMASK(35, 32)

/* MPAMF_ECR - MPAM Error Control Register */
#define MPAMF_ECR_INTEN		BIT(0)

/* Error conditions in accessing memory mapped registers */
#define MPAM_ERRCODE_NONE			0
#define MPAM_ERRCODE_PARTID_SEL_RANGE		1
#define MPAM_ERRCODE_REQ_PARTID_RANGE		2
#define MPAM_ERRCODE_MSMONCFG_ID_RANGE		3
#define MPAM_ERRCODE_REQ_PMG_RANGE		4
#define MPAM_ERRCODE_MONITOR_RANGE		5
#define MPAM_ERRCODE_INTPARTID_RANGE		6
#define MPAM_ERRCODE_UNEXPECTED_INTERNAL	7
#define MPAM_ERRCODE_UNDEFINED_RIS_PART_SEL	8
#define MPAM_ERRCODE_RIS_NO_CONTROL		9
#define MPAM_ERRCODE_UNDEFINED_RIS_MON_SEL	10
#define MPAM_ERRCODE_RIS_NO_MONITOR		11

/*
 * MSMON_CFG_CSU_CTL - Memory system performance monitor configure cache storage
 *                    usage monitor control register
 * MSMON_CFG_MBWU_CTL - Memory system performance monitor configure memory
 *                     bandwidth usage monitor control register
 */
#define MSMON_CFG_x_CTL_TYPE			GENMASK(7, 0)
#define MSMON_CFG_MBWU_CTL_OFLOW_STATUS_L	BIT(15)
#define MSMON_CFG_x_CTL_MATCH_PARTID		BIT(16)
#define MSMON_CFG_x_CTL_MATCH_PMG		BIT(17)
#define MSMON_CFG_MBWU_CTL_SCLEN		BIT(19)
#define MSMON_CFG_x_CTL_SUBTYPE			GENMASK(22, 20)
#define MSMON_CFG_x_CTL_OFLOW_FRZ		BIT(24)
#define MSMON_CFG_x_CTL_OFLOW_INTR		BIT(25)
#define MSMON_CFG_x_CTL_OFLOW_STATUS		BIT(26)
#define MSMON_CFG_x_CTL_CAPT_RESET		BIT(27)
#define MSMON_CFG_x_CTL_CAPT_EVNT		GENMASK(30, 28)
#define MSMON_CFG_x_CTL_EN			BIT(31)

#define MSMON_CFG_MBWU_CTL_TYPE_MBWU		0x42
#define MSMON_CFG_CSU_CTL_TYPE_CSU		0x43

/*
 * MSMON_CFG_CSU_FLT -  Memory system performance monitor configure cache storage
 *                      usage monitor filter register
 * MSMON_CFG_MBWU_FLT - Memory system performance monitor configure memory
 *                      bandwidth usage monitor filter register
 */
#define MSMON_CFG_x_FLT_PARTID			GENMASK(15, 0)
#define MSMON_CFG_x_FLT_PMG			GENMASK(23, 16)

#define MSMON_CFG_MBWU_FLT_RWBW			GENMASK(31, 30)
#define MSMON_CFG_CSU_FLT_XCL			BIT(31)

/*
 * MSMON_CSU - Memory system performance monitor cache storage usage monitor
 *            register
 * MSMON_CSU_CAPTURE -  Memory system performance monitor cache storage usage
 *                     capture register
 * MSMON_MBWU  - Memory system performance monitor memory bandwidth usage
 *               monitor register
 * MSMON_MBWU_CAPTURE - Memory system performance monitor memory bandwidth usage
 *                     capture register
 */
#define MSMON___VALUE		GENMASK(30, 0)
#define MSMON___NRDY		BIT(31)
#define MSMON___L_NRDY		BIT(63)
#define MSMON___L_VALUE		GENMASK(43, 0)
#define MSMON___LWD_VALUE	GENMASK(62, 0)

/*
 * MSMON_CAPT_EVNT - Memory system performance monitoring capture event
 *                  generation register
 */
#define MSMON_CAPT_EVNT_NOW	BIT(0)

#endif /* MPAM_INTERNAL_H */
