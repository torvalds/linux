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

/* Enable bit for DSB subunit trigger timestamp */
#define TPDM_DSB_TIER_XTRIG_TSENAB		BIT(1)

/* DSB programming modes */
/* DSB mode bits mask */
#define TPDM_DSB_MODE_MASK			GENMASK(8, 0)
/* Test mode control bit*/
#define TPDM_DSB_MODE_TEST(val)	(val & GENMASK(1, 0))
/* Performance mode */
#define TPDM_DSB_MODE_PERF		BIT(3)
/* High performance mode */
#define TPDM_DSB_MODE_HPBYTESEL(val)	(val & GENMASK(8, 4))

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

/**
 * struct dsb_dataset - specifics associated to dsb dataset
 * @mode:             DSB programming mode
 * @trig_ts:          Enable/Disable trigger timestamp.
 * @trig_type:        Enable/Disable trigger type.
 */
struct dsb_dataset {
	u32			mode;
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
 */

struct tpdm_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	bool			enable;
	unsigned long		datasets;
	struct dsb_dataset	*dsb;
};

#endif  /* _CORESIGHT_CORESIGHT_TPDM_H */
