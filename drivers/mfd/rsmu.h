/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Renesas Synchronization Management Unit (SMU) devices.
 *
 * Copyright (C) 2021 Integrated Device Technology, Inc., a Renesas Company.
 */

#ifndef __RSMU_MFD_H
#define __RSMU_MFD_H

#include <linux/mfd/rsmu.h>

#define RSMU_CM_SCSR_BASE		0x20100000

int rsmu_core_init(struct rsmu_ddata *rsmu);
void rsmu_core_exit(struct rsmu_ddata *rsmu);

#endif /* __RSMU_MFD_H */
