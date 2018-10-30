/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Arm Limited. All rights reserved.
 *
 * Author: Suzuki K Poulose <suzuki.poulose@arm.com>
 */

#ifndef _CORESIGHT_CATU_H
#define _CORESIGHT_CATU_H

#include "coresight-priv.h"

/* Register offset from base */
#define CATU_CONTROL		0x000
#define CATU_MODE		0x004
#define CATU_AXICTRL		0x008
#define CATU_IRQEN		0x00c
#define CATU_SLADDRLO		0x020
#define CATU_SLADDRHI		0x024
#define CATU_INADDRLO		0x028
#define CATU_INADDRHI		0x02c
#define CATU_STATUS		0x100
#define CATU_DEVARCH		0xfbc

#define CATU_CONTROL_ENABLE	0

#define CATU_MODE_PASS_THROUGH	0U
#define CATU_MODE_TRANSLATE	1U

#define CATU_AXICTRL_ARCACHE_SHIFT	4
#define CATU_AXICTRL_ARCACHE_MASK	0xf
#define CATU_AXICTRL_ARPROT_MASK	0x3
#define CATU_AXICTRL_ARCACHE(arcache)		\
	(((arcache) & CATU_AXICTRL_ARCACHE_MASK) << CATU_AXICTRL_ARCACHE_SHIFT)

#define CATU_AXICTRL_VAL(arcache, arprot)	\
	(CATU_AXICTRL_ARCACHE(arcache) | ((arprot) & CATU_AXICTRL_ARPROT_MASK))

#define AXI3_AxCACHE_WB_READ_ALLOC	0x7
/*
 * AXI - ARPROT bits:
 * See AMBA AXI & ACE Protocol specification (ARM IHI 0022E)
 * sectionA4.7 Access Permissions.
 *
 * Bit 0: 0 - Unprivileged access, 1 - Privileged access
 * Bit 1: 0 - Secure access, 1 - Non-secure access.
 * Bit 2: 0 - Data access, 1 - instruction access.
 *
 * CATU AXICTRL:ARPROT[2] is res0 as we always access data.
 */
#define CATU_OS_ARPROT			0x2

#define CATU_OS_AXICTRL		\
	CATU_AXICTRL_VAL(AXI3_AxCACHE_WB_READ_ALLOC, CATU_OS_ARPROT)

#define CATU_STATUS_READY	8
#define CATU_STATUS_ADRERR	0
#define CATU_STATUS_AXIERR	4

#define CATU_IRQEN_ON		0x1
#define CATU_IRQEN_OFF		0x0

struct catu_drvdata {
	struct device *dev;
	void __iomem *base;
	struct coresight_device *csdev;
	int irq;
};

#define CATU_REG32(name, offset)					\
static inline u32							\
catu_read_##name(struct catu_drvdata *drvdata)				\
{									\
	return coresight_read_reg_pair(drvdata->base, offset, -1);	\
}									\
static inline void							\
catu_write_##name(struct catu_drvdata *drvdata, u32 val)		\
{									\
	coresight_write_reg_pair(drvdata->base, val, offset, -1);	\
}

#define CATU_REG_PAIR(name, lo_off, hi_off)				\
static inline u64							\
catu_read_##name(struct catu_drvdata *drvdata)				\
{									\
	return coresight_read_reg_pair(drvdata->base, lo_off, hi_off);	\
}									\
static inline void							\
catu_write_##name(struct catu_drvdata *drvdata, u64 val)		\
{									\
	coresight_write_reg_pair(drvdata->base, val, lo_off, hi_off);	\
}

CATU_REG32(control, CATU_CONTROL);
CATU_REG32(mode, CATU_MODE);
CATU_REG32(irqen, CATU_IRQEN);
CATU_REG32(axictrl, CATU_AXICTRL);
CATU_REG_PAIR(sladdr, CATU_SLADDRLO, CATU_SLADDRHI)
CATU_REG_PAIR(inaddr, CATU_INADDRLO, CATU_INADDRHI)

static inline bool coresight_is_catu_device(struct coresight_device *csdev)
{
	if (!IS_ENABLED(CONFIG_CORESIGHT_CATU))
		return false;
	if (csdev->type != CORESIGHT_DEV_TYPE_HELPER)
		return false;
	if (csdev->subtype.helper_subtype != CORESIGHT_DEV_SUBTYPE_HELPER_CATU)
		return false;
	return true;
}

#ifdef CONFIG_CORESIGHT_CATU
extern const struct etr_buf_operations etr_catu_buf_ops;
#else
/* Dummy declaration for the CATU ops */
static const struct etr_buf_operations etr_catu_buf_ops;
#endif

#endif
