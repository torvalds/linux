/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2010 John Crispin <john@phrozen.org>
 */

#ifndef _LTQ_FALCON_H__
#define _LTQ_FALCON_H__

#ifdef CONFIG_SOC_FALCON

#include <linux/pinctrl/pinctrl.h>
#include <lantiq.h>

/* Chip IDs */
#define SOC_ID_FALCON		0x01B8

/* SoC Types */
#define SOC_TYPE_FALCON		0x01

/*
 * during early_printk no ioremap possible at this early stage
 * let's use KSEG1 instead
 */
#define LTQ_ASC0_BASE_ADDR	0x1E100C00
#define LTQ_EARLY_ASC		KSEG1ADDR(LTQ_ASC0_BASE_ADDR)

/* WDT */
#define LTQ_RST_CAUSE_WDTRST	0x0002

/* CHIP ID */
#define LTQ_STATUS_BASE_ADDR	0x1E802000

#define FALCON_CHIPID		((u32 *)(KSEG1 + LTQ_STATUS_BASE_ADDR + 0x0c))
#define FALCON_CHIPTYPE		((u32 *)(KSEG1 + LTQ_STATUS_BASE_ADDR + 0x38))
#define FALCON_CHIPCONF		((u32 *)(KSEG1 + LTQ_STATUS_BASE_ADDR + 0x40))

/* SYSCTL - start/stop/restart/configure/... different parts of the Soc */
#define SYSCTL_SYS1		0
#define SYSCTL_SYSETH		1
#define SYSCTL_SYSGPE		2

/* BOOT_SEL - find what boot media we have */
#define BS_FLASH		0x1
#define BS_SPI			0x4

/* global register ranges */
extern __iomem void *ltq_ebu_membase;
extern __iomem void *ltq_sys1_membase;
#define ltq_ebu_w32(x, y)	ltq_w32((x), ltq_ebu_membase + (y))
#define ltq_ebu_r32(x)		ltq_r32(ltq_ebu_membase + (x))

#define ltq_sys1_w32(x, y)	ltq_w32((x), ltq_sys1_membase + (y))
#define ltq_sys1_r32(x)		ltq_r32(ltq_sys1_membase + (x))
#define ltq_sys1_w32_mask(clear, set, reg)   \
	ltq_sys1_w32((ltq_sys1_r32(reg) & ~(clear)) | (set), reg)

/* allow the gpio and pinctrl drivers to talk to each other */
extern int pinctrl_falcon_get_range_size(int id);
extern void pinctrl_falcon_add_gpio_range(struct pinctrl_gpio_range *range);

/*
 * to keep the irq code generic we need to define this to 0 as falcon
 * has no EIU/EBU
 */
#define LTQ_EBU_PCC_ISTAT	0

#endif /* CONFIG_SOC_FALCON */
#endif /* _LTQ_XWAY_H__ */
