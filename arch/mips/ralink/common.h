/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (C) 2013 John Crispin <john@phrozen.org>
 */

#ifndef _RALINK_COMMON_H__
#define _RALINK_COMMON_H__

#define RAMIPS_SYS_TYPE_LEN	32

struct ralink_soc_info {
	unsigned char sys_type[RAMIPS_SYS_TYPE_LEN];
	unsigned char *compatible;

	unsigned long mem_base;
	unsigned long mem_size;
	unsigned long mem_size_min;
	unsigned long mem_size_max;
	void (*mem_detect)(void);
};
extern struct ralink_soc_info soc_info;

extern void ralink_of_remap(void);

extern void __init prom_soc_init(struct ralink_soc_info *soc_info);

#endif /* _RALINK_COMMON_H__ */
