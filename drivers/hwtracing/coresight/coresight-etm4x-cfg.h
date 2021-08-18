/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2014-2020, The Linux Foundation. All rights reserved.
 */

#ifndef _CORESIGHT_ETM4X_CFG_H
#define _CORESIGHT_ETM4X_CFG_H

#include "coresight-config.h"
#include "coresight-etm4x.h"

/* ETMv4 specific config functions */
int etm4_cscfg_register(struct coresight_device *csdev);

#endif /* CORESIGHT_ETM4X_CFG_H */
