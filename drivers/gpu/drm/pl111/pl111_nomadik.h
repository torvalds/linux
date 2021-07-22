// SPDX-License-Identifier: GPL-2.0+

#ifndef PL111_NOMADIK_H
#define PL111_NOMADIK_H
#endif

struct device;

#ifdef CONFIG_ARCH_NOMADIK

void pl111_nomadik_init(struct device *dev);

#else

static inline void pl111_nomadik_init(struct device *dev)
{
}

#endif
