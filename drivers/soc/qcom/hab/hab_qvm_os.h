/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __HAB_QVM_OS_H
#define __HAB_QVM_OS_H

#include <linux/guest_shm.h>
#include <linux/stddef.h>

struct qvm_channel_os {
	struct tasklet_struct task;
};

#endif /*__HAB_QVM_OS_H*/
