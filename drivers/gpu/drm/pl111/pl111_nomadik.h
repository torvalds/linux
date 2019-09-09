// SPDX-License-Identifier: GPL-2.0+
#include <linux/device.h>

#ifndef PL111_NOMADIK_H
#define PL111_NOMADIK_H
#endif

#ifdef CONFIG_ARCH_NOMADIK

void pl111_nomadik_init(struct device *dev);

#else

static inline void pl111_nomadik_init(struct device *dev)
{
}

#endif
