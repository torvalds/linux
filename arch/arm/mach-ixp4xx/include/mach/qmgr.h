/*
 * Copyright (C) 2007 Krzysztof Halasa <khc@pm.waw.pl>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
 */

#ifndef IXP4XX_QMGR_H
#define IXP4XX_QMGR_H

#include <linux/io.h>
#include <linux/kernel.h>

#define HALF_QUEUES	32
#define QUEUES		64	/* only 32 lower queues currently supported */
#define MAX_QUEUE_LENGTH 4	/* in dwords */

#define QUEUE_STAT1_EMPTY		1 /* queue status bits */
#define QUEUE_STAT1_NEARLY_EMPTY	2
#define QUEUE_STAT1_NEARLY_FULL		4
#define QUEUE_STAT1_FULL		8
#define QUEUE_STAT2_UNDERFLOW		1
#define QUEUE_STAT2_OVERFLOW		2

#define QUEUE_WATERMARK_0_ENTRIES	0
#define QUEUE_WATERMARK_1_ENTRY		1
#define QUEUE_WATERMARK_2_ENTRIES	2
#define QUEUE_WATERMARK_4_ENTRIES	3
#define QUEUE_WATERMARK_8_ENTRIES	4
#define QUEUE_WATERMARK_16_ENTRIES	5
#define QUEUE_WATERMARK_32_ENTRIES	6
#define QUEUE_WATERMARK_64_ENTRIES	7

/* queue interrupt request conditions */
#define QUEUE_IRQ_SRC_EMPTY		0
#define QUEUE_IRQ_SRC_NEARLY_EMPTY	1
#define QUEUE_IRQ_SRC_NEARLY_FULL	2
#define QUEUE_IRQ_SRC_FULL		3
#define QUEUE_IRQ_SRC_NOT_EMPTY		4
#define QUEUE_IRQ_SRC_NOT_NEARLY_EMPTY	5
#define QUEUE_IRQ_SRC_NOT_NEARLY_FULL	6
#define QUEUE_IRQ_SRC_NOT_FULL		7

struct qmgr_regs {
	u32 acc[QUEUES][MAX_QUEUE_LENGTH]; /* 0x000 - 0x3FF */
	u32 stat1[4];		/* 0x400 - 0x40F */
	u32 stat2[2];		/* 0x410 - 0x417 */
	u32 statne_h;		/* 0x418 - queue nearly empty */
	u32 statf_h;		/* 0x41C - queue full */
	u32 irqsrc[4];		/* 0x420 - 0x42F IRC source */
	u32 irqen[2];		/* 0x430 - 0x437 IRQ enabled */
	u32 irqstat[2];		/* 0x438 - 0x43F - IRQ access only */
	u32 reserved[1776];
	u32 sram[2048];		/* 0x2000 - 0x3FFF - config and buffer */
};

void qmgr_set_irq(unsigned int queue, int src,
		  void (*handler)(void *pdev), void *pdev);
void qmgr_enable_irq(unsigned int queue);
void qmgr_disable_irq(unsigned int queue);

/* request_ and release_queue() must be called from non-IRQ context */
int qmgr_request_queue(unsigned int queue, unsigned int len /* dwords */,
		       unsigned int nearly_empty_watermark,
		       unsigned int nearly_full_watermark);
void qmgr_release_queue(unsigned int queue);


static inline void qmgr_put_entry(unsigned int queue, u32 val)
{
	extern struct qmgr_regs __iomem *qmgr_regs;
	__raw_writel(val, &qmgr_regs->acc[queue][0]);
}

static inline u32 qmgr_get_entry(unsigned int queue)
{
	extern struct qmgr_regs __iomem *qmgr_regs;
	return __raw_readl(&qmgr_regs->acc[queue][0]);
}

static inline int qmgr_get_stat1(unsigned int queue)
{
	extern struct qmgr_regs __iomem *qmgr_regs;
	return (__raw_readl(&qmgr_regs->stat1[queue >> 3])
		>> ((queue & 7) << 2)) & 0xF;
}

static inline int qmgr_get_stat2(unsigned int queue)
{
	extern struct qmgr_regs __iomem *qmgr_regs;
	return (__raw_readl(&qmgr_regs->stat2[queue >> 4])
		>> ((queue & 0xF) << 1)) & 0x3;
}

static inline int qmgr_stat_empty(unsigned int queue)
{
	return !!(qmgr_get_stat1(queue) & QUEUE_STAT1_EMPTY);
}

static inline int qmgr_stat_nearly_empty(unsigned int queue)
{
	return !!(qmgr_get_stat1(queue) & QUEUE_STAT1_NEARLY_EMPTY);
}

static inline int qmgr_stat_nearly_full(unsigned int queue)
{
	return !!(qmgr_get_stat1(queue) & QUEUE_STAT1_NEARLY_FULL);
}

static inline int qmgr_stat_full(unsigned int queue)
{
	return !!(qmgr_get_stat1(queue) & QUEUE_STAT1_FULL);
}

static inline int qmgr_stat_underflow(unsigned int queue)
{
	return !!(qmgr_get_stat2(queue) & QUEUE_STAT2_UNDERFLOW);
}

static inline int qmgr_stat_overflow(unsigned int queue)
{
	return !!(qmgr_get_stat2(queue) & QUEUE_STAT2_OVERFLOW);
}

#endif
