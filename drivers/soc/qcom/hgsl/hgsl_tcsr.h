/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __HGSL_TCSR_H
#define __HGSL_TCSR_H

#include <linux/device.h>
#include <linux/irqreturn.h>
#include <linux/platform_device.h>

/*
 * Bit 0-5 are used for doorbell interrupt for each RB
 * Bit 8 is used for retire TS interrupt to GVM.
 */
#define TCSR_KMD_TRIGGER_IRQ_ID_0		0
#define TCSR_GMU_TRIGGER_IRQ_ID_0		8

/* Define Source and Destination IRQ for KMD */
#define TCSR_SRC_IRQ_ID_0		TCSR_KMD_TRIGGER_IRQ_ID_0

#define TCSR_DEST_IRQ_ID_0		TCSR_GMU_TRIGGER_IRQ_ID_0

#define TCSR_DEST_IRQ_MASK_0	(1 << TCSR_DEST_IRQ_ID_0)

enum hgsl_tcsr_role {
	HGSL_TCSR_ROLE_SENDER = 0,
	HGSL_TCSR_ROLE_RECEIVER = 1,
	HGSL_TCSR_ROLE_MAX,
};

struct hgsl_tcsr;

extern struct platform_driver hgsl_tcsr_driver;
#if IS_ENABLED(CONFIG_QCOM_HGSL_TCSR_SIGNAL)
struct hgsl_tcsr *hgsl_tcsr_request(struct platform_device *pdev,
				enum hgsl_tcsr_role role,
				struct device *client,
				irqreturn_t (*isr)(struct device *, u32));
void hgsl_tcsr_free(struct hgsl_tcsr *tcsr);
int hgsl_tcsr_enable(struct hgsl_tcsr *tcsr);
void hgsl_tcsr_disable(struct hgsl_tcsr *tcsr);
bool hgsl_tcsr_is_enabled(struct hgsl_tcsr *tcsr);
void hgsl_tcsr_irq_trigger(struct hgsl_tcsr *tcsr, int irq_id);
void hgsl_tcsr_irq_enable(struct hgsl_tcsr *tcsr, u32 mask, bool enable);
#else
static inline struct hgsl_tcsr *hgsl_tcsr_request(struct platform_device *pdev,
				enum hgsl_tcsr_role role,
				struct device *client,
				irqreturn_t (*isr)(struct device *, u32))
{
	return NULL;
}

static inline void hgsl_tcsr_free(struct hgsl_tcsr *tcsr)
{
}

static inline int hgsl_tcsr_enable(struct hgsl_tcsr *tcsr)
{
	return -ENODEV;
}

static inline void hgsl_tcsr_disable(struct hgsl_tcsr *tcsr)
{
}

static inline bool hgsl_tcsr_is_enabled(struct hgsl_tcsr *tcsr)
{
	return false;
}

static inline void hgsl_tcsr_irq_trigger(struct hgsl_tcsr *tcsr, int irq_id)
{
}

static inline void hgsl_tcsr_irq_enable(struct hgsl_tcsr *tcsr, u32 mask,
					bool enable)
{
}
#endif

#endif  /* __HGSL_TCSR_H */

