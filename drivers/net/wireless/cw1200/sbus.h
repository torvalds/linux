/*
 * Common sbus abstraction layer interface for cw1200 wireless driver
 *
 * Copyright (c) 2010, ST-Ericsson
 * Author: Dmitry Tarnyagin <dmitry.tarnyagin@lockless.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef CW1200_SBUS_H
#define CW1200_SBUS_H

/*
 * sbus priv forward definition.
 * Implemented and instantiated in particular modules.
 */
struct sbus_priv;

void cw1200_irq_handler(struct cw1200_common *priv);

/* This MUST be wrapped with sbus_ops->lock/unlock! */
int __cw1200_irq_enable(struct cw1200_common *priv, int enable);

struct sbus_ops {
	int (*sbus_memcpy_fromio)(struct sbus_priv *self, unsigned int addr,
					void *dst, int count);
	int (*sbus_memcpy_toio)(struct sbus_priv *self, unsigned int addr,
					const void *src, int count);
	void (*lock)(struct sbus_priv *self);
	void (*unlock)(struct sbus_priv *self);
	size_t (*align_size)(struct sbus_priv *self, size_t size);
	int (*power_mgmt)(struct sbus_priv *self, bool suspend);
};

#endif /* CW1200_SBUS_H */
