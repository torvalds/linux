/*
 * ddbridge-io.h: Digital Devices bridge I/O inline functions
 *
 * Copyright (C) 2010-2017 Digital Devices GmbH
 *                         Ralph Metzler <rjkm@metzlerbros.de>
 *                         Marcus Metzler <mocm@metzlerbros.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __DDBRIDGE_IO_H__
#define __DDBRIDGE_IO_H__

#include <linux/io.h>

#include "ddbridge.h"

/******************************************************************************/

static inline u32 ddblreadl(struct ddb_link *link, u32 adr)
{
	return readl(link->dev->regs + adr);
}

static inline void ddblwritel(struct ddb_link *link, u32 val, u32 adr)
{
	writel(val, link->dev->regs + adr);
}

static inline u32 ddbreadl(struct ddb *dev, u32 adr)
{
	return readl(dev->regs + adr);
}

static inline void ddbwritel(struct ddb *dev, u32 val, u32 adr)
{
	writel(val, dev->regs + adr);
}

static inline void ddbcpyto(struct ddb *dev, u32 adr, void *src, long count)
{
	memcpy_toio(dev->regs + adr, src, count);
}

static inline void ddbcpyfrom(struct ddb *dev, void *dst, u32 adr, long count)
{
	memcpy_fromio(dst, dev->regs + adr, count);
}

static inline u32 safe_ddbreadl(struct ddb *dev, u32 adr)
{
	u32 val = ddbreadl(dev, adr);

	/* (ddb)readl returns (uint)-1 (all bits set) on failure, catch that */
	if (val == ~0) {
		dev_err(&dev->pdev->dev, "ddbreadl failure, adr=%08x\n", adr);
		return 0;
	}

	return val;
}

#endif /* __DDBRIDGE_IO_H__ */
