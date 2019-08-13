/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2013 Imagination Technologies Ltd.
 *
 * Register definitions for Intel PIIX4 South Bridge Device.
 */
#ifndef __ASM_MIPS_BOARDS_PIIX4_H
#define __ASM_MIPS_BOARDS_PIIX4_H

/* PIRQX Route Control */
#define PIIX4_FUNC0_PIRQRC			0x60
#define   PIIX4_FUNC0_PIRQRC_IRQ_ROUTING_DISABLE	(1 << 7)
#define   PIIX4_FUNC0_PIRQRC_IRQ_ROUTING_MASK		0xf
#define   PIIX4_FUNC0_PIRQRC_IRQ_ROUTING_MAX		16
/* SERIRQ Control */
#define PIIX4_FUNC0_SERIRQC			0x64
#define   PIIX4_FUNC0_SERIRQC_EN			(1 << 7)
#define   PIIX4_FUNC0_SERIRQC_CONT			(1 << 6)
/* Top Of Memory */
#define PIIX4_FUNC0_TOM				0x69
#define   PIIX4_FUNC0_TOM_TOP_OF_MEMORY_MASK		0xf0
/* Deterministic Latency Control */
#define PIIX4_FUNC0_DLC				0x82
#define   PIIX4_FUNC0_DLC_USBPR_EN			(1 << 2)
#define   PIIX4_FUNC0_DLC_PASSIVE_RELEASE_EN		(1 << 1)
#define   PIIX4_FUNC0_DLC_DELAYED_TRANSACTION_EN	(1 << 0)
/* General Configuration */
#define PIIX4_FUNC0_GENCFG			0xb0
#define   PIIX4_FUNC0_GENCFG_SERIRQ			(1 << 16)

/* IDE Timing */
#define PIIX4_FUNC1_IDETIM_PRIMARY_LO		0x40
#define PIIX4_FUNC1_IDETIM_PRIMARY_HI		0x41
#define   PIIX4_FUNC1_IDETIM_PRIMARY_HI_IDE_DECODE_EN	(1 << 7)
#define PIIX4_FUNC1_IDETIM_SECONDARY_LO		0x42
#define PIIX4_FUNC1_IDETIM_SECONDARY_HI		0x43
#define   PIIX4_FUNC1_IDETIM_SECONDARY_HI_IDE_DECODE_EN	(1 << 7)

/* Power Management Configuration Space */
#define PIIX4_FUNC3_PMBA			0x40
#define PIIX4_FUNC3_PMREGMISC			0x80
#define   PIIX4_FUNC3_PMREGMISC_EN			(1 << 0)

/* Power Management IO Space */
#define PIIX4_FUNC3IO_PMSTS			0x00
#define   PIIX4_FUNC3IO_PMSTS_PWRBTN_STS		(1 << 8)
#define PIIX4_FUNC3IO_PMCNTRL			0x04
#define   PIIX4_FUNC3IO_PMCNTRL_SUS_EN			(1 << 13)
#define   PIIX4_FUNC3IO_PMCNTRL_SUS_TYP			(0x7 << 10)
#define   PIIX4_FUNC3IO_PMCNTRL_SUS_TYP_SOFF		(0x0 << 10)
#define   PIIX4_FUNC3IO_PMCNTRL_SUS_TYP_STR		(0x1 << 10)

/* Data for magic special PCI cycle */
#define PIIX4_SUSPEND_MAGIC			0x00120002

#endif /* __ASM_MIPS_BOARDS_PIIX4_H */
