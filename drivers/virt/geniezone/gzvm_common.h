/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#ifndef __GZ_COMMON_H__
#define __GZ_COMMON_H__

int gzvm_irqchip_inject_irq(struct gzvm *gzvm, unsigned int vcpu_idx,
			    u32 irq, bool level);

#endif /* __GZVM_COMMON_H__ */
