/*
 * Common hwbus abstraction layer interface for cw1200 wireless driver
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CW1200_HWBUS_H
#define CW1200_HWBUS_H

struct hwbus_priv;

void cw1200_irq_handler(struct cw1200_common *priv);

/* This MUST be wrapped with hwbus_ops->lock/unlock! */
int __cw1200_irq_enable(struct cw1200_common *priv, int enable);

struct hwbus_ops {
	int (*hwbus_memcpy_fromio)(struct hwbus_priv *self, unsigned int addr,
					void *dst, int count);
	int (*hwbus_memcpy_toio)(struct hwbus_priv *self, unsigned int addr,
					const void *src, int count);
	void (*lock)(struct hwbus_priv *self);
	void (*unlock)(struct hwbus_priv *self);
	size_t (*align_size)(struct hwbus_priv *self, size_t size);
	int (*power_mgmt)(struct hwbus_priv *self, bool suspend);
	int (*irq_enable)(struct hwbus_priv *self, int enable);
};

#endif /* CW1200_HWBUS_H */
