// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt) "bimc-bwmon: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/spinlock.h>
#include <linux/log2.h>
#include <linux/sizes.h>
#include "governor_bw_hwmon.h"

#define GLB_INT_STATUS(m)	((m)->global_base + 0x100)
#define GLB_INT_CLR(m)		((m)->global_base + 0x108)
#define	GLB_INT_EN(m)		((m)->global_base + 0x10C)
#define MON_INT_STATUS(m)	((m)->base + 0x100)
#define MON_INT_STATUS_MASK	0x03
#define MON2_INT_STATUS_MASK	0xF0
#define MON2_INT_STATUS_SHIFT	4
#define MON_INT_CLR(m)		((m)->base + 0x108)
#define	MON_INT_EN(m)		((m)->base + 0x10C)
#define MON_INT_ENABLE		0x1
#define	MON_EN(m)		((m)->base + 0x280)
#define MON_CLEAR(m)		((m)->base + 0x284)
#define MON_CNT(m)		((m)->base + 0x288)
#define MON_THRES(m)		((m)->base + 0x290)
#define MON_MASK(m)		((m)->base + 0x298)
#define MON_MATCH(m)		((m)->base + 0x29C)

#define MON2_EN(m)		((m)->base + 0x2A0)
#define MON2_CLEAR(m)		((m)->base + 0x2A4)
#define MON2_SW(m)		((m)->base + 0x2A8)
#define MON2_THRES_HI(m)	((m)->base + 0x2AC)
#define MON2_THRES_MED(m)	((m)->base + 0x2B0)
#define MON2_THRES_LO(m)	((m)->base + 0x2B4)
#define MON2_ZONE_ACTIONS(m)	((m)->base + 0x2B8)
#define MON2_ZONE_CNT_THRES(m)	((m)->base + 0x2BC)
#define MON2_BYTE_CNT(m)	((m)->base + 0x2D0)
#define MON2_WIN_TIMER(m)	((m)->base + 0x2D4)
#define MON2_ZONE_CNT(m)	((m)->base + 0x2D8)
#define MON2_ZONE_MAX(m, zone)	((m)->base + 0x2E0 + 0x4 * zone)

#define MON3_INT_STATUS(m)	((m)->base + 0x00)
#define MON3_INT_CLR(m)		((m)->base + 0x08)
#define MON3_INT_EN(m)		((m)->base + 0x0C)
#define MON3_INT_STATUS_MASK	0x0F
#define MON3_EN(m)		((m)->base + 0x10)
#define MON3_CLEAR(m)		((m)->base + 0x14)
#define MON3_MASK(m)		((m)->base + 0x18)
#define MON3_MATCH(m)		((m)->base + 0x1C)
#define MON3_SW(m)		((m)->base + 0x20)
#define MON3_THRES_HI(m)	((m)->base + 0x24)
#define MON3_THRES_MED(m)	((m)->base + 0x28)
#define MON3_THRES_LO(m)	((m)->base + 0x2C)
#define MON3_ZONE_ACTIONS(m)	((m)->base + 0x30)
#define MON3_ZONE_CNT_THRES(m)	((m)->base + 0x34)
#define MON3_BYTE_CNT(m)	((m)->base + 0x38)
#define MON3_WIN_TIMER(m)	((m)->base + 0x3C)
#define MON3_ZONE_CNT(m)	((m)->base + 0x40)
#define MON3_ZONE_MAX(m, zone)	((m)->base + 0x44 + 0x4 * zone)

enum mon_reg_type {
	MON1,
	MON2,
	MON3,
};

struct bwmon_spec {
	bool wrap_on_thres;
	bool overflow;
	bool throt_adj;
	bool hw_sampling;
	bool has_global_base;
	enum mon_reg_type reg_type;
};

struct bwmon {
	void __iomem		*base;
	void __iomem		*global_base;
	unsigned int		mport;
	int			irq;
	const struct bwmon_spec	*spec;
	struct device		*dev;
	struct bw_hwmon		hw;
	u32			hw_timer_hz;
	u32			throttle_adj;
	u32			sample_size_ms;
	u32			intr_status;
	u8			count_shift;
	u32			thres_lim;
	u32			byte_mask;
	u32			byte_match;
};

#define to_bwmon(ptr)		container_of(ptr, struct bwmon, hw)

#define ENABLE_MASK BIT(0)
#define THROTTLE_MASK 0x1F
#define THROTTLE_SHIFT 16

static DEFINE_SPINLOCK(glb_lock);

static __always_inline void mon_enable(struct bwmon *m, enum mon_reg_type type)
{
	switch (type) {
	case MON1:
		writel_relaxed(ENABLE_MASK | m->throttle_adj, MON_EN(m));
		break;
	case MON2:
		writel_relaxed(ENABLE_MASK | m->throttle_adj, MON2_EN(m));
		break;
	case MON3:
		writel_relaxed(ENABLE_MASK | m->throttle_adj, MON3_EN(m));
		break;
	}
}

static __always_inline void mon_disable(struct bwmon *m, enum mon_reg_type type)
{
	switch (type) {
	case MON1:
		writel_relaxed(m->throttle_adj, MON_EN(m));
		break;
	case MON2:
		writel_relaxed(m->throttle_adj, MON2_EN(m));
		break;
	case MON3:
		writel_relaxed(m->throttle_adj, MON3_EN(m));
		break;
	}
	/*
	 * mon_disable() and mon_irq_clear(),
	 * If latter goes first and count happen to trigger irq, we would
	 * have the irq line high but no one handling it.
	 */
	mb();
}

#define MON_CLEAR_BIT	0x1
#define MON_CLEAR_ALL_BIT	0x2
static __always_inline
void mon_clear(struct bwmon *m, bool clear_all, enum mon_reg_type type)
{
	switch (type) {
	case MON1:
		writel_relaxed(MON_CLEAR_BIT, MON_CLEAR(m));
		break;
	case MON2:
		if (clear_all)
			writel_relaxed(MON_CLEAR_ALL_BIT, MON2_CLEAR(m));
		else
			writel_relaxed(MON_CLEAR_BIT, MON2_CLEAR(m));
		break;
	case MON3:
		if (clear_all)
			writel_relaxed(MON_CLEAR_ALL_BIT, MON3_CLEAR(m));
		else
			writel_relaxed(MON_CLEAR_BIT, MON3_CLEAR(m));
		/*
		 * In some hardware versions since MON3_CLEAR(m) register does
		 * not have self-clearing capability it needs to be cleared
		 * explicitly. But we also need to ensure the writes to it
		 * are successful before clearing it.
		 */
		wmb();
		writel_relaxed(0, MON3_CLEAR(m));
		break;
	}
	/*
	 * The counter clear and IRQ clear bits are not in the same 4KB
	 * region. So, we need to make sure the counter clear is completed
	 * before we try to clear the IRQ or do any other counter operations.
	 */
	mb();
}

#define	SAMPLE_WIN_LIM	0xFFFFFF
static __always_inline
void mon_set_hw_sampling_window(struct bwmon *m, unsigned int sample_ms,
				enum mon_reg_type type)
{
	u32 rate;

	if (unlikely(sample_ms != m->sample_size_ms)) {
		rate = mult_frac(sample_ms, m->hw_timer_hz, MSEC_PER_SEC);
		m->sample_size_ms = sample_ms;
		if (unlikely(rate > SAMPLE_WIN_LIM)) {
			rate = SAMPLE_WIN_LIM;
			pr_warn("Sample window %u larger than hw limit: %u\n",
					rate, SAMPLE_WIN_LIM);
		}
		switch (type) {
		case MON1:
			WARN(1, "Invalid\n");
			return;
		case MON2:
			writel_relaxed(rate, MON2_SW(m));
			break;
		case MON3:
			writel_relaxed(rate, MON3_SW(m));
			break;
		}
	}
}

static void mon_glb_irq_enable(struct bwmon *m)
{
	u32 val;

	val = readl_relaxed(GLB_INT_EN(m));
	val |= 1 << m->mport;
	writel_relaxed(val, GLB_INT_EN(m));
}

static __always_inline
void mon_irq_enable(struct bwmon *m, enum mon_reg_type type)
{
	u32 val;

	spin_lock(&glb_lock);
	switch (type) {
	case MON1:
		mon_glb_irq_enable(m);
		val = readl_relaxed(MON_INT_EN(m));
		val |= MON_INT_ENABLE;
		writel_relaxed(val, MON_INT_EN(m));
		break;
	case MON2:
		mon_glb_irq_enable(m);
		val = readl_relaxed(MON_INT_EN(m));
		val |= MON2_INT_STATUS_MASK;
		writel_relaxed(val, MON_INT_EN(m));
		break;
	case MON3:
		val = readl_relaxed(MON3_INT_EN(m));
		val |= MON3_INT_STATUS_MASK;
		writel_relaxed(val, MON3_INT_EN(m));
		break;
	}
	spin_unlock(&glb_lock);
	/*
	 * make sure irq enable complete for local and global
	 * to avoid race with other monitor calls
	 */
	mb();
}

static void mon_glb_irq_disable(struct bwmon *m)
{
	u32 val;

	val = readl_relaxed(GLB_INT_EN(m));
	val &= ~(1 << m->mport);
	writel_relaxed(val, GLB_INT_EN(m));
}

static __always_inline
void mon_irq_disable(struct bwmon *m, enum mon_reg_type type)
{
	u32 val;

	spin_lock(&glb_lock);

	switch (type) {
	case MON1:
		mon_glb_irq_disable(m);
		val = readl_relaxed(MON_INT_EN(m));
		val &= ~MON_INT_ENABLE;
		writel_relaxed(val, MON_INT_EN(m));
		break;
	case MON2:
		mon_glb_irq_disable(m);
		val = readl_relaxed(MON_INT_EN(m));
		val &= ~MON2_INT_STATUS_MASK;
		writel_relaxed(val, MON_INT_EN(m));
		break;
	case MON3:
		val = readl_relaxed(MON3_INT_EN(m));
		val &= ~MON3_INT_STATUS_MASK;
		writel_relaxed(val, MON3_INT_EN(m));
		break;
	}
	spin_unlock(&glb_lock);
	/*
	 * make sure irq disable complete for local and global
	 * to avoid race with other monitor calls
	 */
	mb();
}

static __always_inline
unsigned int mon_irq_status(struct bwmon *m, enum mon_reg_type type)
{
	u32 mval;

	switch (type) {
	case MON1:
		mval = readl_relaxed(MON_INT_STATUS(m));
		dev_dbg(m->dev, "IRQ status p:%x, g:%x\n", mval,
				readl_relaxed(GLB_INT_STATUS(m)));
		mval &= MON_INT_STATUS_MASK;
		break;
	case MON2:
		mval = readl_relaxed(MON_INT_STATUS(m));
		dev_dbg(m->dev, "IRQ status p:%x, g:%x\n", mval,
				readl_relaxed(GLB_INT_STATUS(m)));
		mval &= MON2_INT_STATUS_MASK;
		mval >>= MON2_INT_STATUS_SHIFT;
		break;
	case MON3:
		mval = readl_relaxed(MON3_INT_STATUS(m));
		dev_dbg(m->dev, "IRQ status p:%x\n", mval);
		mval &= MON3_INT_STATUS_MASK;
		break;
	}

	return mval;
}


static void mon_glb_irq_clear(struct bwmon *m)
{
	/*
	 * Synchronize the local interrupt clear in mon_irq_clear()
	 * with the global interrupt clear here. Otherwise, the CPU
	 * may reorder the two writes and clear the global interrupt
	 * before the local interrupt, causing the global interrupt
	 * to be retriggered by the local interrupt still being high.
	 */
	mb();
	writel_relaxed(1 << m->mport, GLB_INT_CLR(m));
	/*
	 * Similarly, because the global registers are in a different
	 * region than the local registers, we need to ensure any register
	 * writes to enable the monitor after this call are ordered with the
	 * clearing here so that local writes don't happen before the
	 * interrupt is cleared.
	 */
	mb();
}

static __always_inline
void mon_irq_clear(struct bwmon *m, enum mon_reg_type type)
{
	switch (type) {
	case MON1:
		writel_relaxed(MON_INT_STATUS_MASK, MON_INT_CLR(m));
		mon_glb_irq_clear(m);
		break;
	case MON2:
		writel_relaxed(MON2_INT_STATUS_MASK, MON_INT_CLR(m));
		mon_glb_irq_clear(m);
		break;
	case MON3:
		writel_relaxed(MON3_INT_STATUS_MASK, MON3_INT_CLR(m));
		/*
		 * In some hardware versions since MON3_INT_CLEAR(m) register
		 * does not have self-clearing capability it needs to be
		 * cleared explicitly. But we also need to ensure the writes
		 * to it are successful before clearing it.
		 */
		wmb();
		writel_relaxed(0, MON3_INT_CLR(m));
		break;
	}
}

static int mon_set_throttle_adj(struct bw_hwmon *hw, uint adj)
{
	struct bwmon *m = to_bwmon(hw);

	if (adj > THROTTLE_MASK)
		return -EINVAL;

	adj = (adj & THROTTLE_MASK) << THROTTLE_SHIFT;
	m->throttle_adj = adj;

	return 0;
}

static u32 mon_get_throttle_adj(struct bw_hwmon *hw)
{
	struct bwmon *m = to_bwmon(hw);

	return m->throttle_adj >> THROTTLE_SHIFT;
}

#define ZONE1_SHIFT	8
#define ZONE2_SHIFT	16
#define ZONE3_SHIFT	24
#define ZONE0_ACTION	0x01	/* Increment zone 0 count */
#define ZONE1_ACTION	0x09	/* Increment zone 1 & clear lower zones */
#define ZONE2_ACTION	0x25	/* Increment zone 2 & clear lower zones */
#define ZONE3_ACTION	0x95	/* Increment zone 3 & clear lower zones */
static u32 calc_zone_actions(void)
{
	u32 zone_actions;

	zone_actions = ZONE0_ACTION;
	zone_actions |= ZONE1_ACTION << ZONE1_SHIFT;
	zone_actions |= ZONE2_ACTION << ZONE2_SHIFT;
	zone_actions |= ZONE3_ACTION << ZONE3_SHIFT;

	return zone_actions;
}

#define ZONE_CNT_LIM	0xFFU
#define UP_CNT_1	1
static u32 calc_zone_counts(struct bw_hwmon *hw)
{
	u32 zone_counts;

	zone_counts = ZONE_CNT_LIM;
	zone_counts |= min(hw->down_cnt, ZONE_CNT_LIM) << ZONE1_SHIFT;
	zone_counts |= ZONE_CNT_LIM << ZONE2_SHIFT;
	zone_counts |= UP_CNT_1 << ZONE3_SHIFT;

	return zone_counts;
}

#define MB_SHIFT	20

static u32 mbps_to_count(unsigned long mbps, unsigned int ms, u8 shift)
{
	mbps *= ms;

	if (shift > MB_SHIFT)
		mbps >>= shift - MB_SHIFT;
	else
		mbps <<= MB_SHIFT - shift;

	return DIV_ROUND_UP(mbps, MSEC_PER_SEC);
}

/*
 * Define the 4 zones using HI, MED & LO thresholds:
 * Zone 0: byte count < THRES_LO
 * Zone 1: THRES_LO < byte count < THRES_MED
 * Zone 2: THRES_MED < byte count < THRES_HI
 * Zone 3: THRES_LIM > byte count > THRES_HI
 */
#define	THRES_LIM(shift)	(0xFFFFFFFF >> shift)

static __always_inline
void set_zone_thres(struct bwmon *m, unsigned int sample_ms,
		    enum mon_reg_type type)
{
	struct bw_hwmon *hw = &m->hw;
	u32 hi, med, lo;
	u32 zone_cnt_thres = calc_zone_counts(hw);

	hi = mbps_to_count(hw->up_wake_mbps, sample_ms, m->count_shift);
	med = mbps_to_count(hw->down_wake_mbps, sample_ms, m->count_shift);
	lo = 0;

	if (unlikely((hi > m->thres_lim) || (med > hi) || (lo > med))) {
		pr_warn("Zone thres larger than hw limit: hi:%u med:%u lo:%u\n",
				hi, med, lo);
		hi = min(hi, m->thres_lim);
		med = min(med, hi - 1);
		lo = min(lo, med-1);
	}

	switch (type) {
	case MON1:
		WARN(1, "Invalid\n");
		return;
	case MON2:
		writel_relaxed(hi, MON2_THRES_HI(m));
		writel_relaxed(med, MON2_THRES_MED(m));
		writel_relaxed(lo, MON2_THRES_LO(m));
		/* Set the zone count thresholds for interrupts */
		writel_relaxed(zone_cnt_thres, MON2_ZONE_CNT_THRES(m));
		break;
	case MON3:
		writel_relaxed(hi, MON3_THRES_HI(m));
		writel_relaxed(med, MON3_THRES_MED(m));
		writel_relaxed(lo, MON3_THRES_LO(m));
		/* Set the zone count thresholds for interrupts */
		writel_relaxed(zone_cnt_thres, MON3_ZONE_CNT_THRES(m));
		break;
	}

	dev_dbg(m->dev, "Thres: hi:%u med:%u lo:%u\n", hi, med, lo);
	dev_dbg(m->dev, "Zone Count Thres: %0x\n", zone_cnt_thres);
}

static __always_inline
void mon_set_zones(struct bwmon *m, unsigned int sample_ms,
		   enum mon_reg_type type)
{
	mon_set_hw_sampling_window(m, sample_ms, type);
	set_zone_thres(m, sample_ms, type);
}

static void mon_set_limit(struct bwmon *m, u32 count)
{
	writel_relaxed(count, MON_THRES(m));
	dev_dbg(m->dev, "Thres: %08x\n", count);
}

static u32 mon_get_limit(struct bwmon *m)
{
	return readl_relaxed(MON_THRES(m));
}

#define THRES_HIT(status)	(status & BIT(0))
#define OVERFLOW(status)	(status & BIT(1))
static unsigned long mon_get_count1(struct bwmon *m)
{
	unsigned long count, status;

	count = readl_relaxed(MON_CNT(m));
	status = mon_irq_status(m, MON1);

	dev_dbg(m->dev, "Counter: %08lx\n", count);

	if (OVERFLOW(status) && m->spec->overflow)
		count += 0xFFFFFFFF;
	if (THRES_HIT(status) && m->spec->wrap_on_thres)
		count += mon_get_limit(m);

	dev_dbg(m->dev, "Actual Count: %08lx\n", count);

	return count;
}

static __always_inline
unsigned int get_zone(struct bwmon *m, enum mon_reg_type type)
{
	u32 zone_counts;
	u32 zone;

	zone = get_bitmask_order(m->intr_status);
	if (zone) {
		zone--;
	} else {
		switch (type) {
		case MON1:
			WARN(1, "Invalid\n");
			return 0;
		case MON2:
			zone_counts = readl_relaxed(MON2_ZONE_CNT(m));
			break;
		case MON3:
			zone_counts = readl_relaxed(MON3_ZONE_CNT(m));
			break;
		}

		if (zone_counts) {
			zone = get_bitmask_order(zone_counts) - 1;
			zone /= 8;
		}
	}

	m->intr_status = 0;
	return zone;
}

static __always_inline
unsigned long get_zone_count(struct bwmon *m, unsigned int zone,
			     enum mon_reg_type type)
{
	unsigned long count;

	switch (type) {
	case MON1:
		WARN(1, "Invalid\n");
		return 0;
	case MON2:
		count = readl_relaxed(MON2_ZONE_MAX(m, zone));
		break;
	case MON3:
		count = readl_relaxed(MON3_ZONE_MAX(m, zone));
		break;
	}

	if (count)
		count++;

	return count;
}

static __always_inline
unsigned long mon_get_zone_stats(struct bwmon *m, enum mon_reg_type type)
{
	unsigned int zone;
	unsigned long count = 0;

	zone = get_zone(m, type);
	count = get_zone_count(m, zone, type);
	count <<= m->count_shift;

	dev_dbg(m->dev, "Zone%d Max byte count: %08lx\n", zone, count);

	return count;
}

static __always_inline
unsigned long mon_get_count(struct bwmon *m, enum mon_reg_type type)
{
	unsigned long count;

	switch (type) {
	case MON1:
		count = mon_get_count1(m);
		break;
	case MON2:
	case MON3:
		count = mon_get_zone_stats(m, type);
		break;
	}

	return count;
}

/* ********** CPUBW specific code  ********** */

/* Returns MBps of read/writes for the sampling window. */
static unsigned int mbps_to_bytes(unsigned long mbps, unsigned int ms,
				  unsigned int tolerance_percent)
{
	mbps *= (100 + tolerance_percent) * ms;
	mbps /= 100;
	mbps = DIV_ROUND_UP(mbps, MSEC_PER_SEC);
	mbps *= SZ_1M;
	return mbps;
}

static __always_inline
unsigned long __get_bytes_and_clear(struct bw_hwmon *hw, enum mon_reg_type type)
{
	struct bwmon *m = to_bwmon(hw);
	unsigned long count;

	mon_disable(m, type);
	count = mon_get_count(m, type);
	mon_clear(m, false, type);
	mon_irq_clear(m, type);
	mon_enable(m, type);

	return count;
}

static unsigned long get_bytes_and_clear(struct bw_hwmon *hw)
{
	return __get_bytes_and_clear(hw, MON1);
}

static unsigned long get_bytes_and_clear2(struct bw_hwmon *hw)
{
	return __get_bytes_and_clear(hw, MON2);
}

static unsigned long get_bytes_and_clear3(struct bw_hwmon *hw)
{
	return __get_bytes_and_clear(hw, MON3);
}

static unsigned long set_thres(struct bw_hwmon *hw, unsigned long bytes)
{
	unsigned long count;
	u32 limit;
	struct bwmon *m = to_bwmon(hw);

	mon_disable(m, MON1);
	count = mon_get_count1(m);
	mon_clear(m, false, MON1);
	mon_irq_clear(m, MON1);

	if (likely(!m->spec->wrap_on_thres))
		limit = bytes;
	else
		limit = max(bytes, 500000UL);

	mon_set_limit(m, limit);
	mon_enable(m, MON1);

	return count;
}

static unsigned long
__set_hw_events(struct bw_hwmon *hw, unsigned int sample_ms,
		enum mon_reg_type type)
{
	struct bwmon *m = to_bwmon(hw);

	mon_disable(m, type);
	mon_clear(m, false, type);
	mon_irq_clear(m, type);

	mon_set_zones(m, sample_ms, type);
	mon_enable(m, type);

	return 0;
}

static unsigned long set_hw_events(struct bw_hwmon *hw, unsigned int sample_ms)
{
	return __set_hw_events(hw, sample_ms, MON2);
}

static unsigned long
set_hw_events3(struct bw_hwmon *hw, unsigned int sample_ms)
{
	return __set_hw_events(hw, sample_ms, MON3);
}

static irqreturn_t
__bwmon_intr_handler(int irq, void *dev, enum mon_reg_type type)
{
	struct bwmon *m = dev;

	m->intr_status = mon_irq_status(m, type);
	if (!m->intr_status)
		return IRQ_NONE;

	if (bw_hwmon_sample_end(&m->hw) > 0)
		return IRQ_WAKE_THREAD;

	return IRQ_HANDLED;
}

static irqreturn_t bwmon_intr_handler(int irq, void *dev)
{
	return __bwmon_intr_handler(irq, dev, MON1);
}

static irqreturn_t bwmon_intr_handler2(int irq, void *dev)
{
	return __bwmon_intr_handler(irq, dev, MON2);
}

static irqreturn_t bwmon_intr_handler3(int irq, void *dev)
{
	return __bwmon_intr_handler(irq, dev, MON3);
}

static irqreturn_t bwmon_intr_thread(int irq, void *dev)
{
	struct bwmon *m = dev;

	update_bw_hwmon(&m->hw);
	return IRQ_HANDLED;
}

static __always_inline
void mon_set_byte_count_filter(struct bwmon *m, enum mon_reg_type type)
{
	if (!m->byte_mask)
		return;

	switch (type) {
	case MON1:
	case MON2:
		writel_relaxed(m->byte_mask, MON_MASK(m));
		writel_relaxed(m->byte_match, MON_MATCH(m));
		break;
	case MON3:
		writel_relaxed(m->byte_mask, MON3_MASK(m));
		writel_relaxed(m->byte_match, MON3_MATCH(m));
		break;
	}
}

static __always_inline int __start_bw_hwmon(struct bw_hwmon *hw,
		unsigned long mbps, enum mon_reg_type type)
{
	struct bwmon *m = to_bwmon(hw);
	u32 limit, zone_actions;
	int ret;
	irq_handler_t handler;

	switch (type) {
	case MON1:
		handler = bwmon_intr_handler;
		limit = mbps_to_bytes(mbps, hw->df->profile->polling_ms, 0);
		break;
	case MON2:
		zone_actions = calc_zone_actions();
		handler = bwmon_intr_handler2;
		break;
	case MON3:
		zone_actions = calc_zone_actions();
		handler = bwmon_intr_handler3;
		break;
	}

	ret = request_threaded_irq(m->irq, handler, bwmon_intr_thread,
				  IRQF_ONESHOT | IRQF_SHARED,
				  dev_name(m->dev), m);
	if (ret < 0) {
		dev_err(m->dev, "Unable to register interrupt handler! (%d)\n",
			ret);
		return ret;
	}

	mon_disable(m, type);

	mon_clear(m, false, type);

	switch (type) {
	case MON1:
		mon_set_limit(m, limit);
		break;
	case MON2:
		mon_set_zones(m, hw->df->profile->polling_ms, type);
		/* Set the zone actions to increment appropriate counters */
		writel_relaxed(zone_actions, MON2_ZONE_ACTIONS(m));
		break;
	case MON3:
		mon_set_zones(m, hw->df->profile->polling_ms, type);
		/* Set the zone actions to increment appropriate counters */
		writel_relaxed(zone_actions, MON3_ZONE_ACTIONS(m));
	}

	mon_set_byte_count_filter(m, type);
	mon_irq_clear(m, type);
	mon_irq_enable(m, type);
	mon_enable(m, type);

	return 0;
}

static int start_bw_hwmon(struct bw_hwmon *hw, unsigned long mbps)
{
	return __start_bw_hwmon(hw, mbps, MON1);
}

static int start_bw_hwmon2(struct bw_hwmon *hw, unsigned long mbps)
{
	return __start_bw_hwmon(hw, mbps, MON2);
}

static int start_bw_hwmon3(struct bw_hwmon *hw, unsigned long mbps)
{
	return __start_bw_hwmon(hw, mbps, MON3);
}

static __always_inline
void __stop_bw_hwmon(struct bw_hwmon *hw, enum mon_reg_type type)
{
	struct bwmon *m = to_bwmon(hw);

	mon_irq_disable(m, type);
	free_irq(m->irq, m);
	mon_disable(m, type);
	mon_clear(m, true, type);
	mon_irq_clear(m, type);
}

static void stop_bw_hwmon(struct bw_hwmon *hw)
{
	return __stop_bw_hwmon(hw, MON1);
}

static void stop_bw_hwmon2(struct bw_hwmon *hw)
{
	return __stop_bw_hwmon(hw, MON2);
}

static void stop_bw_hwmon3(struct bw_hwmon *hw)
{
	return __stop_bw_hwmon(hw, MON3);
}

static __always_inline
int __suspend_bw_hwmon(struct bw_hwmon *hw, enum mon_reg_type type)
{
	struct bwmon *m = to_bwmon(hw);

	mon_irq_disable(m, type);
	free_irq(m->irq, m);
	mon_disable(m, type);
	mon_irq_clear(m, type);

	return 0;
}

static int suspend_bw_hwmon(struct bw_hwmon *hw)
{
	return __suspend_bw_hwmon(hw, MON1);
}

static int suspend_bw_hwmon2(struct bw_hwmon *hw)
{
	return __suspend_bw_hwmon(hw, MON2);
}

static int suspend_bw_hwmon3(struct bw_hwmon *hw)
{
	return __suspend_bw_hwmon(hw, MON3);
}

static __always_inline
int __resume_bw_hwmon(struct bw_hwmon *hw, enum mon_reg_type type)
{
	struct bwmon *m = to_bwmon(hw);
	int ret;
	irq_handler_t handler;

	switch (type) {
	case MON1:
		handler = bwmon_intr_handler;
		break;
	case MON2:
		handler = bwmon_intr_handler2;
		break;
	case MON3:
		handler = bwmon_intr_handler3;
		break;
	}

	mon_clear(m, false, type);
	ret = request_threaded_irq(m->irq, handler, bwmon_intr_thread,
				  IRQF_ONESHOT | IRQF_SHARED,
				  dev_name(m->dev), m);
	if (ret < 0) {
		dev_err(m->dev, "Unable to register interrupt handler! (%d)\n",
			ret);
		return ret;
	}

	mon_irq_enable(m, type);
	mon_enable(m, type);

	return 0;
}

static int resume_bw_hwmon(struct bw_hwmon *hw)
{
	return __resume_bw_hwmon(hw, MON1);
}

static int resume_bw_hwmon2(struct bw_hwmon *hw)
{
	return __resume_bw_hwmon(hw, MON2);
}

static int resume_bw_hwmon3(struct bw_hwmon *hw)
{
	return __resume_bw_hwmon(hw, MON3);
}

/*************************************************************************/

static const struct bwmon_spec spec[] = {
	[0] = {
		.wrap_on_thres = true,
		.overflow = false,
		.throt_adj = false,
		.hw_sampling = false,
		.has_global_base = true,
		.reg_type = MON1,
	},
	[1] = {
		.wrap_on_thres = false,
		.overflow = true,
		.throt_adj = false,
		.hw_sampling = false,
		.has_global_base = true,
		.reg_type = MON1,
	},
	[2] = {
		.wrap_on_thres = false,
		.overflow = true,
		.throt_adj = true,
		.hw_sampling = false,
		.has_global_base = true,
		.reg_type = MON1,
	},
	[3] = {
		.wrap_on_thres = false,
		.overflow = true,
		.throt_adj = true,
		.hw_sampling = true,
		.has_global_base = true,
		.reg_type = MON2,
	},
	[4] = {
		.wrap_on_thres = false,
		.overflow = true,
		.throt_adj = false,
		.hw_sampling = true,
		.reg_type = MON3,
	},
};

static const struct of_device_id bimc_bwmon_match_table[] = {
	{ .compatible = "qcom,bimc-bwmon", .data = &spec[0] },
	{ .compatible = "qcom,bimc-bwmon2", .data = &spec[1] },
	{ .compatible = "qcom,bimc-bwmon3", .data = &spec[2] },
	{ .compatible = "qcom,bimc-bwmon4", .data = &spec[3] },
	{ .compatible = "qcom,bimc-bwmon5", .data = &spec[4] },
	{}
};

static int bimc_bwmon_driver_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct bwmon *m;
	int ret;
	u32 data, count_unit;

	m = devm_kzalloc(dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->dev = dev;

	m->spec = of_device_get_match_data(dev);
	if (!m->spec) {
		dev_err(dev, "Unknown device type!\n");
		return -ENODEV;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "base");
	if (!res) {
		dev_err(dev, "base not found!\n");
		return -EINVAL;
	}
	m->base = devm_ioremap(dev, res->start, resource_size(res));
	if (!m->base) {
		dev_err(dev, "Unable map base!\n");
		return -ENOMEM;
	}

	if (m->spec->has_global_base) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						   "global_base");
		if (!res) {
			dev_err(dev, "global_base not found!\n");
			return -EINVAL;
		}
		m->global_base = devm_ioremap(dev, res->start,
					      resource_size(res));
		if (!m->global_base) {
			dev_err(dev, "Unable map global_base!\n");
			return -ENOMEM;
		}

		ret = of_property_read_u32(dev->of_node, "qcom,mport", &data);
		if (ret < 0) {
			dev_err(dev, "mport not found! (%d)\n", ret);
			return ret;
		}
		m->mport = data;
	}

	m->irq = platform_get_irq(pdev, 0);
	if (m->irq < 0) {
		dev_err(dev, "Unable to get IRQ number\n");
		return m->irq;
	}

	m->hw.of_node = of_parse_phandle(dev->of_node, "qcom,target-dev", 0);
	if (!m->hw.of_node)
		return -EINVAL;

	if (m->spec->hw_sampling) {
		ret = of_property_read_u32(dev->of_node, "qcom,hw-timer-hz",
					   &m->hw_timer_hz);
		if (ret < 0) {
			dev_err(dev, "HW sampling rate not specified!\n");
			return ret;
		}
	}

	if (of_property_read_u32(dev->of_node, "qcom,count-unit", &count_unit))
		count_unit = SZ_1M;
	m->count_shift = order_base_2(count_unit);
	m->thres_lim = THRES_LIM(m->count_shift);

	switch (m->spec->reg_type) {
	case MON3:
		m->hw.start_hwmon = start_bw_hwmon3;
		m->hw.stop_hwmon = stop_bw_hwmon3;
		m->hw.suspend_hwmon = suspend_bw_hwmon3;
		m->hw.resume_hwmon = resume_bw_hwmon3;
		m->hw.get_bytes_and_clear = get_bytes_and_clear3;
		m->hw.set_hw_events = set_hw_events3;
		break;
	case MON2:
		m->hw.start_hwmon = start_bw_hwmon2;
		m->hw.stop_hwmon = stop_bw_hwmon2;
		m->hw.suspend_hwmon = suspend_bw_hwmon2;
		m->hw.resume_hwmon = resume_bw_hwmon2;
		m->hw.get_bytes_and_clear = get_bytes_and_clear2;
		m->hw.set_hw_events = set_hw_events;
		break;
	case MON1:
		m->hw.start_hwmon = start_bw_hwmon;
		m->hw.stop_hwmon = stop_bw_hwmon;
		m->hw.suspend_hwmon = suspend_bw_hwmon;
		m->hw.resume_hwmon = resume_bw_hwmon;
		m->hw.get_bytes_and_clear = get_bytes_and_clear;
		m->hw.set_thres = set_thres;
		break;
	}

	of_property_read_u32(dev->of_node, "qcom,byte-mid-match",
			     &m->byte_match);
	of_property_read_u32(dev->of_node, "qcom,byte-mid-mask",
			     &m->byte_mask);

	if (m->spec->throt_adj) {
		m->hw.set_throttle_adj = mon_set_throttle_adj;
		m->hw.get_throttle_adj = mon_get_throttle_adj;
	}

	ret = register_bw_hwmon(dev, &m->hw);
	if (ret < 0) {
		dev_err(dev, "Dev BW hwmon registration failed: %d\n", ret);
		return ret;
	}
	dev_err(dev, "Dev BW hwmon registration success: %d\n", ret);

	return 0;
}

static struct platform_driver bimc_bwmon_driver = {
	.probe = bimc_bwmon_driver_probe,
	.driver = {
		.name = "bimc-bwmon",
		.of_match_table = bimc_bwmon_match_table,
		.suppress_bind_attrs = true,
	},
};

module_platform_driver(bimc_bwmon_driver);
MODULE_DESCRIPTION("BIMC bandwidth monitor driver");
MODULE_LICENSE("GPL");
