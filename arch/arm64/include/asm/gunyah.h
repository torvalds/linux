/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __ASM_GUNYAH_H_
#define __ASM_GUNYAH_H_

#include <linux/irq.h>
#include <dt-bindings/interrupt-controller/arm-gic.h>

static inline int arch_gh_fill_irq_fwspec_params(u32 virq, struct irq_fwspec *fwspec)
{
	if (virq < 32 || virq > 1019)
		return -EINVAL;

	fwspec->param_count = 3;
	fwspec->param[0] = GIC_SPI;
	fwspec->param[1] = virq - 32;
	fwspec->param[2] = IRQ_TYPE_EDGE_RISING;
	return 0;
}

#endif
