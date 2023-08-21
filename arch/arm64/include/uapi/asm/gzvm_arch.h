/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef __GZVM_ARCH_H__
#define __GZVM_ARCH_H__

#include <linux/types.h>

#define GZVM_VGIC_NR_SGIS		16
#define GZVM_VGIC_NR_PPIS		16
#define GZVM_VGIC_NR_PRIVATE_IRQS	(GZVM_VGIC_NR_SGIS + GZVM_VGIC_NR_PPIS)

#endif /* __GZVM_ARCH_H__ */
