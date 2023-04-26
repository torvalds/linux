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
/* Enable bit for DSB subunit */
#define TPDM_DSB_CR_ENA		BIT(0)

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
 * struct tpdm_drvdata - specifics associated to an TPDM component
 * @base:       memory mapped base address for this component.
 * @dev:        The device entity associated to this component.
 * @csdev:      component vitals needed by the framework.
 * @spinlock:   lock for the drvdata value.
 * @enable:     enable status of the component.
 * @datasets:   The datasets types present of the TPDM.
 */

struct tpdm_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	bool			enable;
	unsigned long		datasets;
};

#endif  /* _CORESIGHT_CORESIGHT_TPDM_H */
