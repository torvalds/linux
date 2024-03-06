/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _CORESIGHT_CORESIGHT_TPDA_H
#define _CORESIGHT_CORESIGHT_TPDA_H

#define TPDA_CR			(0x000)
#define TPDA_Pn_CR(n)		(0x004 + (n * 4))
/* Aggregator port enable bit */
#define TPDA_Pn_CR_ENA		BIT(0)
/* Aggregator port DSB data set element size bit */
#define TPDA_Pn_CR_DSBSIZE		BIT(8)

#define TPDA_MAX_INPORTS	32

/* Bits 6 ~ 12 is for atid value */
#define TPDA_CR_ATID		GENMASK(12, 6)

/**
 * struct tpda_drvdata - specifics associated to an TPDA component
 * @base:       memory mapped base address for this component.
 * @dev:        The device entity associated to this component.
 * @csdev:      component vitals needed by the framework.
 * @spinlock:   lock for the drvdata value.
 * @enable:     enable status of the component.
 */
struct tpda_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	spinlock_t		spinlock;
	u8			atid;
};

#endif  /* _CORESIGHT_CORESIGHT_TPDA_H */
