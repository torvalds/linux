/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __HAB_GHS_OS_H
#define __HAB_GHS_OS_H

#include <ghs_vmm/kgipc.h>

struct ghs_vdev_os {
	struct tasklet_struct task;
};

#endif /* __HAB_GHS_OS_H */
