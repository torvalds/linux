/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _CORESIGHT_ETM4X_CFG_H
#define _CORESIGHT_ETM4X_CFG_H

#include "coresight-config.h"
#include "coresight-etm4x.h"

/* ETMv4 specific config defines */

/* resource IDs */

#define ETM4_CFG_RES_CTR	0x001
#define ETM4_CFG_RES_CMP	0x002
#define ETM4_CFG_RES_CMP_PAIR0	0x003
#define ETM4_CFG_RES_CMP_PAIR1	0x004
#define ETM4_CFG_RES_SEL	0x005
#define ETM4_CFG_RES_SEL_PAIR0	0x006
#define ETM4_CFG_RES_SEL_PAIR1	0x007
#define ETM4_CFG_RES_SEQ	0x008
#define ETM4_CFG_RES_TS		0x009
#define ETM4_CFG_RES_MASK	0x00F

/* ETMv4 specific config functions */
int etm4_cscfg_register(struct coresight_device *csdev);

#endif /* CORESIGHT_ETM4X_CFG_H */
