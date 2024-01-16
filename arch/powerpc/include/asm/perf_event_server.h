/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Performance event support - PowerPC classic/server specific definitions.
 *
 * Copyright 2008-2009 Paul Mackerras, IBM Corporation.
 */

#include <linux/types.h>
#include <asm/hw_irq.h>
#include <linux/device.h>
#include <uapi/asm/perf_event.h>

/* Update perf_event_print_debug() if this changes */
#define MAX_HWEVENTS		8
#define MAX_EVENT_ALTERNATIVES	8
#define MAX_LIMITED_HWCOUNTERS	2

struct perf_event;

struct mmcr_regs {
	unsigned long mmcr0;
	unsigned long mmcr1;
	unsigned long mmcr2;
	unsigned long mmcra;
	unsigned long mmcr3;
};
/*
 * This struct provides the constants and functions needed to
 * describe the PMU on a particular POWER-family CPU.
 */
struct power_pmu {
	const char	*name;
	int		n_counter;
	int		max_alternatives;
	unsigned long	add_fields;
	unsigned long	test_adder;
	int		(*compute_mmcr)(u64 events[], int n_ev,
				unsigned int hwc[], struct mmcr_regs *mmcr,
				struct perf_event *pevents[], u32 flags);
	int		(*get_constraint)(u64 event_id, unsigned long *mskp,
				unsigned long *valp, u64 event_config1);
	int		(*get_alternatives)(u64 event_id, unsigned int flags,
				u64 alt[]);
	void		(*get_mem_data_src)(union perf_mem_data_src *dsrc,
				u32 flags, struct pt_regs *regs);
	void		(*get_mem_weight)(u64 *weight, u64 type);
	unsigned long	group_constraint_mask;
	unsigned long	group_constraint_val;
	u64             (*bhrb_filter_map)(u64 branch_sample_type);
	void            (*config_bhrb)(u64 pmu_bhrb_filter);
	void		(*disable_pmc)(unsigned int pmc, struct mmcr_regs *mmcr);
	int		(*limited_pmc_event)(u64 event_id);
	u32		flags;
	const struct attribute_group	**attr_groups;
	int		n_generic;
	int		*generic_events;
	u64		(*cache_events)[PERF_COUNT_HW_CACHE_MAX]
			       [PERF_COUNT_HW_CACHE_OP_MAX]
			       [PERF_COUNT_HW_CACHE_RESULT_MAX];

	int		n_blacklist_ev;
	int 		*blacklist_ev;
	/* BHRB entries in the PMU */
	int		bhrb_nr;
	/*
	 * set this flag with `PERF_PMU_CAP_EXTENDED_REGS` if
	 * the pmu supports extended perf regs capability
	 */
	int		capabilities;
	/*
	 * Function to check event code for values which are
	 * reserved. Function takes struct perf_event as input,
	 * since event code could be spread in attr.config*
	 */
	int		(*check_attr_config)(struct perf_event *ev);
};

/*
 * Values for power_pmu.flags
 */
#define PPMU_LIMITED_PMC5_6	0x00000001 /* PMC5/6 have limited function */
#define PPMU_ALT_SIPR		0x00000002 /* uses alternate posn for SIPR/HV */
#define PPMU_NO_SIPR		0x00000004 /* no SIPR/HV in MMCRA at all */
#define PPMU_NO_CONT_SAMPLING	0x00000008 /* no continuous sampling */
#define PPMU_SIAR_VALID		0x00000010 /* Processor has SIAR Valid bit */
#define PPMU_HAS_SSLOT		0x00000020 /* Has sampled slot in MMCRA */
#define PPMU_HAS_SIER		0x00000040 /* Has SIER */
#define PPMU_ARCH_207S		0x00000080 /* PMC is architecture v2.07S */
#define PPMU_NO_SIAR		0x00000100 /* Do not use SIAR */
#define PPMU_ARCH_31		0x00000200 /* Has MMCR3, SIER2 and SIER3 */
#define PPMU_P10_DD1		0x00000400 /* Is power10 DD1 processor version */
#define PPMU_HAS_ATTR_CONFIG1	0x00000800 /* Using config1 attribute */

/*
 * Values for flags to get_alternatives()
 */
#define PPMU_LIMITED_PMC_OK	1	/* can put this on a limited PMC */
#define PPMU_LIMITED_PMC_REQD	2	/* have to put this on a limited PMC */
#define PPMU_ONLY_COUNT_RUN	4	/* only counting in run state */

int __init register_power_pmu(struct power_pmu *pmu);

struct pt_regs;
extern unsigned long perf_misc_flags(struct pt_regs *regs);
extern unsigned long perf_instruction_pointer(struct pt_regs *regs);
extern unsigned long int read_bhrb(int n);

/*
 * Only override the default definitions in include/linux/perf_event.h
 * if we have hardware PMU support.
 */
#ifdef CONFIG_PPC_PERF_CTRS
#define perf_misc_flags(regs)	perf_misc_flags(regs)
#endif

/*
 * The power_pmu.get_constraint function returns a 32/64-bit value and
 * a 32/64-bit mask that express the constraints between this event_id and
 * other events.
 *
 * The value and mask are divided up into (non-overlapping) bitfields
 * of three different types:
 *
 * Select field: this expresses the constraint that some set of bits
 * in MMCR* needs to be set to a specific value for this event_id.  For a
 * select field, the mask contains 1s in every bit of the field, and
 * the value contains a unique value for each possible setting of the
 * MMCR* bits.  The constraint checking code will ensure that two events
 * that set the same field in their masks have the same value in their
 * value dwords.
 *
 * Add field: this expresses the constraint that there can be at most
 * N events in a particular class.  A field of k bits can be used for
 * N <= 2^(k-1) - 1.  The mask has the most significant bit of the field
 * set (and the other bits 0), and the value has only the least significant
 * bit of the field set.  In addition, the 'add_fields' and 'test_adder'
 * in the struct power_pmu for this processor come into play.  The
 * add_fields value contains 1 in the LSB of the field, and the
 * test_adder contains 2^(k-1) - 1 - N in the field.
 *
 * NAND field: this expresses the constraint that you may not have events
 * in all of a set of classes.  (For example, on PPC970, you can't select
 * events from the FPU, ISU and IDU simultaneously, although any two are
 * possible.)  For N classes, the field is N+1 bits wide, and each class
 * is assigned one bit from the least-significant N bits.  The mask has
 * only the most-significant bit set, and the value has only the bit
 * for the event_id's class set.  The test_adder has the least significant
 * bit set in the field.
 *
 * If an event_id is not subject to the constraint expressed by a particular
 * field, then it will have 0 in both the mask and value for that field.
 */

extern ssize_t power_events_sysfs_show(struct device *dev,
				struct device_attribute *attr, char *page);

/*
 * EVENT_VAR() is same as PMU_EVENT_VAR with a suffix.
 *
 * Having a suffix allows us to have aliases in sysfs - eg: the generic
 * event 'cpu-cycles' can have two entries in sysfs: 'cpu-cycles' and
 * 'PM_CYC' where the latter is the name by which the event is known in
 * POWER CPU specification.
 *
 * Similarly, some hardware and cache events use the same event code. Eg.
 * on POWER8, both "cache-references" and "L1-dcache-loads" events refer
 * to the same event, PM_LD_REF_L1.  The suffix, allows us to have two
 * sysfs objects for the same event and thus two entries/aliases in sysfs.
 */
#define	EVENT_VAR(_id, _suffix)		event_attr_##_id##_suffix
#define	EVENT_PTR(_id, _suffix)		&EVENT_VAR(_id, _suffix).attr.attr

#define	EVENT_ATTR(_name, _id, _suffix)					\
	PMU_EVENT_ATTR(_name, EVENT_VAR(_id, _suffix), _id,		\
			power_events_sysfs_show)

#define	GENERIC_EVENT_ATTR(_name, _id)	EVENT_ATTR(_name, _id, _g)
#define	GENERIC_EVENT_PTR(_id)		EVENT_PTR(_id, _g)

#define	CACHE_EVENT_ATTR(_name, _id)	EVENT_ATTR(_name, _id, _c)
#define	CACHE_EVENT_PTR(_id)		EVENT_PTR(_id, _c)

#define	POWER_EVENT_ATTR(_name, _id)	EVENT_ATTR(_name, _id, _p)
#define	POWER_EVENT_PTR(_id)		EVENT_PTR(_id, _p)
