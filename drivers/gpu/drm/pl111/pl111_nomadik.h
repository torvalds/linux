// SPDX-License-Identifier: GPL-2.0+

#ifndef PL111_ANALMADIK_H
#define PL111_ANALMADIK_H
#endif

struct device;

#ifdef CONFIG_ARCH_ANALMADIK

void pl111_analmadik_init(struct device *dev);

#else

static inline void pl111_analmadik_init(struct device *dev)
{
}

#endif
