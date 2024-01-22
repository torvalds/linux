/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CORESIGHT_CORESIGHT_TPDM_H
#define _CORESIGHT_CORESIGHT_TPDM_H

/* The max number of the datasets that TPDM supports */
#define TPDM_DATASETS       7

/* DSB Subunit Registers */
#define TPDM_DSB_CR		(0x780)
#define TPDM_DSB_TIER		(0x784)
#define TPDM_DSB_TPR(n)		(0x788 + (n * 4))
#define TPDM_DSB_TPMR(n)	(0x7A8 + (n * 4))
#define TPDM_DSB_XPR(n)		(0x7C8 + (n * 4))
#define TPDM_DSB_XPMR(n)	(0x7E8 + (n * 4))
#define TPDM_DSB_EDCR(n)	(0x808 + (n * 4))
#define TPDM_DSB_EDCMR(n)	(0x848 + (n * 4))
#define TPDM_DSB_MSR(n)		(0x980 + (n * 4))

/* Enable bit for DSB subunit */
#define TPDM_DSB_CR_ENA		BIT(0)
/* Enable bit for DSB subunit perfmance mode */
#define TPDM_DSB_CR_MODE		BIT(1)
/* Enable bit for DSB subunit trigger type */
#define TPDM_DSB_CR_TRIG_TYPE		BIT(12)
/* Data bits for DSB high performace mode */
#define TPDM_DSB_CR_HPSEL		GENMASK(6, 2)
/* Data bits for DSB test mode */
#define TPDM_DSB_CR_TEST_MODE		GENMASK(10, 9)

/* Enable bit for DSB subunit pattern timestamp */
#define TPDM_DSB_TIER_PATT_TSENAB		BIT(0)
/* Enable bit for DSB subunit trigger timestamp */
#define TPDM_DSB_TIER_XTRIG_TSENAB		BIT(1)
/* Bit for DSB subunit pattern type */
#define TPDM_DSB_TIER_PATT_TYPE			BIT(2)

/* DSB programming modes */
/* DSB mode bits mask */
#define TPDM_DSB_MODE_MASK			GENMASK(8, 0)
/* Test mode control bit*/
#define TPDM_DSB_MODE_TEST(val)	(val & GENMASK(1, 0))
/* Performance mode */
#define TPDM_DSB_MODE_PERF		BIT(3)
/* High performance mode */
#define TPDM_DSB_MODE_HPBYTESEL(val)	(val & GENMASK(8, 4))

#define EDCRS_PER_WORD			16
#define EDCR_TO_WORD_IDX(r)		((r) / EDCRS_PER_WORD)
#define EDCR_TO_WORD_SHIFT(r)		((r % EDCRS_PER_WORD) * 2)
#define EDCR_TO_WORD_VAL(val, r)	(val << EDCR_TO_WORD_SHIFT(r))
#define EDCR_TO_WORD_MASK(r)		EDCR_TO_WORD_VAL(0x3, r)

#define EDCMRS_PER_WORD				32
#define EDCMR_TO_WORD_IDX(r)		((r) / EDCMRS_PER_WORD)
#define EDCMR_TO_WORD_SHIFT(r)		((r) % EDCMRS_PER_WORD)

/* TPDM integration test registers */
#define TPDM_ITATBCNTRL		(0xEF0)
#define TPDM_ITCNTRL		(0xF00)

/* Register value for integration test */
#define ATBCNTRL_VAL_32		0xC00F1409
#define ATBCNTRL_VAL_64		0xC01F1409

/*
 * Number of cycles to write value when
 * integration test.
 */
#define INTEGRATION_TEST_CYCLE	10

/**
 * The bits of PERIPHIDR0 register.
 * The fields [6:0] of PERIPHIDR0 are used to determine what
 * interfaces and subunits are present on a given TPDM.
 *
 * PERIPHIDR0[0] : Fix to 1 if ImplDef subunit present, else 0
 * PERIPHIDR0[1] : Fix to 1 if DSB subunit present, else 0
 */

#define TPDM_PIDR0_DS_IMPDEF	BIT(0)
#define TPDM_PIDR0_DS_DSB	BIT(1)

#define TPDM_DSB_MAX_LINES	256
/* MAX number of EDCR registers */
#define TPDM_DSB_MAX_EDCR	16
/* MAX number of EDCMR registers */
#define TPDM_DSB_MAX_EDCMR	8
/* MAX number of DSB pattern */
#define TPDM_DSB_MAX_PATT	8
/* MAX number of DSB MSR */
#define TPDM_DSB_MAX_MSR 32

#define tpdm_simple_dataset_ro(name, mem, idx)			\
	(&((struct tpdm_dataset_attribute[]) {			\
	   {								\
		__ATTR(name, 0444, tpdm_simple_dataset_show, NULL),	\
		mem,							\
		idx,							\
	   }								\
	})[0].attr.attr)

#define tpdm_simple_dataset_rw(name, mem, idx)			\
	(&((struct tpdm_dataset_attribute[]) {			\
	   {								\
		__ATTR(name, 0644, tpdm_simple_dataset_show,		\
		tpdm_simple_dataset_store),		\
		mem,							\
		idx,							\
	   }								\
	})[0].attr.attr)

#define DSB_EDGE_CTRL_ATTR(nr)					\
		tpdm_simple_dataset_ro(edcr##nr,		\
		DSB_EDGE_CTRL, nr)

#define DSB_EDGE_CTRL_MASK_ATTR(nr)				\
		tpdm_simple_dataset_ro(edcmr##nr,		\
		DSB_EDGE_CTRL_MASK, nr)

#define DSB_TRIG_PATT_ATTR(nr)					\
		tpdm_simple_dataset_rw(xpr##nr,			\
		DSB_TRIG_PATT, nr)

#define DSB_TRIG_PATT_MASK_ATTR(nr)				\
		tpdm_simple_dataset_rw(xpmr##nr,		\
		DSB_TRIG_PATT_MASK, nr)

#define DSB_PATT_ATTR(nr)					\
		tpdm_simple_dataset_rw(tpr##nr,			\
		DSB_PATT, nr)

#define DSB_PATT_MASK_ATTR(nr)					\
		tpdm_simple_dataset_rw(tpmr##nr,		\
		DSB_PATT_MASK, nr)

#define DSB_MSR_ATTR(nr)					\
		tpdm_simple_dataset_rw(msr##nr,			\
		DSB_MSR, nr)

/**
 * struct dsb_dataset - specifics associated to dsb dataset
 * @mode:             DSB programming mode
 * @edge_ctrl_idx     Index number of the edge control
 * @edge_ctrl:        Save value for edge control
 * @edge_ctrl_mask:   Save value for edge control mask
 * @patt_val:         Save value for pattern
 * @patt_mask:        Save value for pattern mask
 * @trig_patt:        Save value for trigger pattern
 * @trig_patt_mask:   Save value for trigger pattern mask
 * @msr               Save value for MSR
 * @patt_ts:          Enable/Disable pattern timestamp
 * @patt_type:        Set pattern type
 * @trig_ts:          Enable/Disable trigger timestamp.
 * @trig_type:        Enable/Disable trigger type.
 */
struct dsb_dataset {
	u32			mode;
	u32			edge_ctrl_idx;
	u32			edge_ctrl[TPDM_DSB_MAX_EDCR];
	u32			edge_ctrl_mask[TPDM_DSB_MAX_EDCMR];
	u32			patt_val[TPDM_DSB_MAX_PATT];
	u32			patt_mask[TPDM_DSB_MAX_PATT];
	u32			trig_patt[TPDM_DSB_MAX_PATT];
	u32			trig_patt_mask[TPDM_DSB_MAX_PATT];
	u32			msr[TPDM_DSB_MAX_MSR];
	bool			patt_ts;
	bool			patt_type;
	bool			trig_ts;
	bool			trig_type;
};

/**
 * struct tpdm_drvdata - specifics associated to an TPDM component
 * @base:       memory mapped base address for this component.
 * @dev:        The device entity associated to this component.
 * @csdev:      component vitals needed by the framework.
 * @spinlock:   lock for the drvdata value.
 * @enable:     enable status of the component.
 * @datasets:   The datasets types present of the TPDM.
 * @dsb         Specifics associated to TPDM DSB.
 * @dsb_msr_num Number of MSR supported by DSB TPDM
 */

struct tpdm_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	bool			enable;
	unsigned long		datasets;
	struct dsb_dataset	*dsb;
	u32			dsb_msr_num;
};

/* Enumerate members of various datasets */
enum dataset_mem {
	DSB_EDGE_CTRL,
	DSB_EDGE_CTRL_MASK,
	DSB_TRIG_PATT,
	DSB_TRIG_PATT_MASK,
	DSB_PATT,
	DSB_PATT_MASK,
	DSB_MSR,
};

/**
 * struct tpdm_dataset_attribute - Record the member variables and
 * index number of datasets that need to be operated by sysfs file
 * @attr:       The device attribute
 * @mem:        The member in the dataset data structure
 * @idx:        The index number of the array data
 */
struct tpdm_dataset_attribute {
	struct device_attribute attr;
	enum dataset_mem mem;
	u32 idx;
};

#endif  /* _CORESIGHT_CORESIGHT_TPDM_H */
