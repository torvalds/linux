/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2006, 2007 Florian Fainelli <florian@openwrt.org>
 */

#ifndef __PROM_H__
#define __PROM_H__

extern char *prom_getenv(const char *name);
extern void prom_meminit(void);

#endif /* __PROM_H__ */
