/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __QCOM_QTEE_SHM_BRIDGE_INT_H_
#define __QCOM_QTEE_SHM_BRIDGE_INT_H_

int __init qtee_shmbridge_driver_init(void);
void __exit qtee_shmbridge_driver_exit(void);

#define SCM_SVC_RTIC                                0x19
#define TZ_HLOS_NOTIFY_CORE_KERNEL_BOOTUP           0x7
int scm_mem_protection_init_do(void);
#endif
