/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_BUS_H_
#define _MM81X_BUS_H_

#include <linux/skbuff.h>
#include "core.h"

enum mm81x_bus_type {
	MM81X_BUS_TYPE_USB,
	MM81X_BUS_TYPE_SDIO,
};

struct mm81x_bus_ops {
	int (*dm_read)(struct mm81x *mors, u32 addr, u8 *data, int len);
	int (*dm_write)(struct mm81x *mors, u32 addr, const u8 *data, int len);
	int (*reg32_read)(struct mm81x *mors, u32 addr, u32 *data);
	int (*reg32_write)(struct mm81x *mors, u32 addr, u32 data);
	int (*digital_reset)(struct mm81x *mors);
	void (*set_bus_enable)(struct mm81x *mors, bool enable);
	void (*config_burst_mode)(struct mm81x *mors, bool enable_burst);
	void (*claim)(struct mm81x *mors);
	void (*set_irq)(struct mm81x *mors, bool enable);
	void (*release)(struct mm81x *mors);
	unsigned int bulk_alignment;
};

/*
 * Default TX alignment for buses which don't care. mac80211 will give us
 * SKBs aligned to the 2 byte boundary, so 2 is effectively a noop.
 */
#define MM81X_BUS_DEFAULT_BULK_ALIGNMENT (2)

/* mm81x_dm_read - len must be rounded up to the nearest 4-byte boundary */
static inline int mm81x_dm_read(struct mm81x *mors, u32 addr, u8 *data, int len)
{
	return mors->bus_ops->dm_read(mors, addr, data, len);
}

static inline int mm81x_dm_write(struct mm81x *mors, u32 addr, const u8 *data,
				 int len)
{
	return mors->bus_ops->dm_write(mors, addr, data, len);
}

static inline int mm81x_reg32_read(struct mm81x *mors, u32 addr, u32 *data)
{
	return mors->bus_ops->reg32_read(mors, addr, data);
}

static inline int mm81x_reg32_write(struct mm81x *mors, u32 addr, u32 data)
{
	return mors->bus_ops->reg32_write(mors, addr, data);
}

static inline int mm81x_bus_digital_reset(struct mm81x *mors)
{
	if (mors->bus_ops->digital_reset)
		return mors->bus_ops->digital_reset(mors);

	return 0;
}

static inline void mm81x_set_bus_enable(struct mm81x *mors, bool enable)
{
	mors->bus_ops->set_bus_enable(mors, enable);
}

static inline void mm81x_bus_config_burst_mode(struct mm81x *mors,
					       bool enable_burst)
{
	if (mors->bus_ops->config_burst_mode)
		mors->bus_ops->config_burst_mode(mors, enable_burst);
}

static inline void mm81x_claim_bus(struct mm81x *mors)
{
	mors->bus_ops->claim(mors);
}

static inline void mm81x_bus_set_irq(struct mm81x *mors, bool enable)
{
	mors->bus_ops->set_irq(mors, enable);
}

static inline void mm81x_release_bus(struct mm81x *mors)
{
	mors->bus_ops->release(mors);
}

static inline unsigned int mm81x_bus_get_alignment(struct mm81x *mors)
{
	return mors->bus_ops->bulk_alignment;
}

#endif /* !_MM81X_BUS_H_ */
