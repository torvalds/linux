/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __HGSL_GMUGOS_H
#define __HGSL_GMUGOS_H

#include <linux/device.h>
#include <linux/irqreturn.h>
#include <linux/platform_device.h>

#define HGSL_GMUGOS_NODE_NAME    "hgsl_gmugos"
#define HGSL_GMUGOS_IRQ_NUM      (8)
#define HGSL_GMUGOS_NAME_LEN     (64)

#define GMUGOS_IRQ_MASK TCSR_DEST_IRQ_MASK_0

struct hgsl_gmugos_irq {
	struct regmap *regmap;
	u32 id;
	s32 num;
};

struct hgsl_gmugos {
	struct hgsl_gmugos_irq irq[HGSL_GMUGOS_IRQ_NUM];
	u32 dev_hnd;
};

int hgsl_init_gmugos(struct platform_device *pdev,
				struct hgsl_context *ctxt, u32 irq_idx);
void hgsl_gmugos_irq_trigger(struct hgsl_gmugos *gmugos,
				u32 id);
void hgsl_gmugos_irq_enable(struct hgsl_gmugos_irq *gmugos_irq,
				u32 mask_bits);
void hgsl_gmugos_irq_disable(struct hgsl_gmugos_irq *gmugos_irq,
				u32 mask_bits);
void hgsl_gmugos_irq_free(struct hgsl_gmugos_irq *irq);

#endif  /* __HGSL_GMUGOS_H */

