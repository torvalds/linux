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

#define DEBUG_QMGR	0

#define HALF_QUEUES	32
#define QUEUES		64
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

#if DEBUG_QMGR
extern char qmgr_queue_descs[QUEUES][32];

int qmgr_request_queue(unsigned int queue, unsigned int len /* dwords */,
		       unsigned int nearly_empty_watermark,
		       unsigned int nearly_full_watermark,
		       const char *desc_format, const char* name);
#else
int __qmgr_request_queue(unsigned int queue, unsigned int len /* dwords */,
			 unsigned int nearly_empty_watermark,
			 unsigned int nearly_full_watermark);
#define qmgr_request_queue(queue, len, nearly_empty_watermark,		\
			   nearly_full_watermark, desc_format, name)	\
	__qmgr_request_queue(queue, len, nearly_empty_watermark,	\
			     nearly_full_watermark)
#endif

void qmgr_release_queue(unsigned int queue);


static inline void qmgr_put_entry(unsigned int queue, u32 val)
{
	const struct qmgr_regs __iomem *qmgr_regs = (void __iomem *)IXP4XX_QMGR_BASE_VIRT;
#if DEBUG_QMGR
	BUG_ON(!qmgr_queue_descs[queue]); /* not yet requested */

	printk(KERN_DEBUG "Queue %s(%i) put %X\n",
	       qmgr_queue_descs[queue], queue, val);
#endif
	__raw_writel(val, &qmgr_regs->acc[queue][0]);
}

static inline u32 qmgr_get_entry(unsigned int queue)
{
	u32 val;
	const struct qmgr_regs __iomem *qmgr_regs = (void __iomem *)IXP4XX_QMGR_BASE_VIRT;
	val = __raw_readl(&qmgr_regs->acc[queue][0]);
#if DEBUG_QMGR
	BUG_ON(!qmgr_queue_descs[queue]); /* not yet requested */

	printk(KERN_DEBUG "Queue %s(%i) get %X\n",
	       qmgr_queue_descs[queue], queue, val);
#endif
	return val;
}

static inline int __qmgr_get_stat1(unsigned int queue)
{
	const struct qmgr_regs __iomem *qmgr_regs = (void __iomem *)IXP4XX_QMGR_BASE_VIRT;
	return (__raw_readl(&qmgr_regs->stat1[queue >> 3])
		>> ((queue & 7) << 2)) & 0xF;
}

static inline int __qmgr_get_stat2(unsigned int queue)
{
	const struct qmgr_regs __iomem *qmgr_regs = (void __iomem *)IXP4XX_QMGR_BASE_VIRT;
	BUG_ON(queue >= HALF_QUEUES);
	return (__raw_readl(&qmgr_regs->stat2[queue >> 4])
		>> ((queue & 0xF) << 1)) & 0x3;
}

/**
 * qmgr_stat_empty() - checks if a hardware queue is empty
 * @queue:	queue number
 *
 * Returns non-zero value if the queue is empty.
 */
static inline int qmgr_stat_empty(unsigned int queue)
{
	BUG_ON(queue >= HALF_QUEUES);
	return __qmgr_get_stat1(queue) & QUEUE_STAT1_EMPTY;
}

/**
 * qmgr_stat_below_low_watermark() - checks if a queue is below low watermark
 * @queue:	queue number
 *
 * Returns non-zero value if the queue is below low watermark.
 */
static inline int qmgr_stat_below_low_watermark(unsigned int queue)
{
	const struct qmgr_regs __iomem *qmgr_regs = (void __iomem *)IXP4XX_QMGR_BASE_VIRT;
	if (queue >= HALF_QUEUES)
		return (__raw_readl(&qmgr_regs->statne_h) >>
			(queue - HALF_QUEUES)) & 0x01;
	return __qmgr_get_stat1(queue) & QUEUE_STAT1_NEARLY_EMPTY;
}

/**
 * qmgr_stat_above_high_watermark() - checks if a queue is above high watermark
 * @queue:	queue number
 *
 * Returns non-zero value if the queue is above high watermark
 */
static inline int qmgr_stat_above_high_watermark(unsigned int queue)
{
	BUG_ON(queue >= HALF_QUEUES);
	return __qmgr_get_stat1(queue) & QUEUE_STAT1_NEARLY_FULL;
}

/**
 * qmgr_stat_full() - checks if a hardware queue is full
 * @queue:	queue number
 *
 * Returns non-zero value if the queue is full.
 */
static inline int qmgr_stat_full(unsigned int queue)
{
	const struct qmgr_regs __iomem *qmgr_regs = (void __iomem *)IXP4XX_QMGR_BASE_VIRT;
	if (queue >= HALF_QUEUES)
		return (__raw_readl(&qmgr_regs->statf_h) >>
			(queue - HALF_QUEUES)) & 0x01;
	return __qmgr_get_stat1(queue) & QUEUE_STAT1_FULL;
}

/**
 * qmgr_stat_underflow() - checks if a hardware queue experienced underflow
 * @queue:	queue number
 *
 * Returns non-zero value if the queue experienced underflow.
 */
static inline int qmgr_stat_underflow(unsigned int queue)
{
	return __qmgr_get_stat2(queue) & QUEUE_STAT2_UNDERFLOW;
}

/**
 * qmgr_stat_overflow() - checks if a hardware queue experienced overflow
 * @queue:	queue number
 *
 * Returns non-zero value if the queue experienced overflow.
 */
static inline int qmgr_stat_overflow(unsigned int queue)
{
	return __qmgr_get_stat2(queue) & QUEUE_STAT2_OVERFLOW;
}

#endif
