/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2014 Texas Instruments, Inc.
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 */
#ifndef __MEMORY_H
#define __MEMORY_H

#define MAX_PHYSMEM_BITS	36
#define SECTION_SIZE_BITS	34

#define KEYSTONE_LOW_PHYS_START		0x80000000ULL
#define KEYSTONE_LOW_PHYS_SIZE		0x80000000ULL /* 2G */
#define KEYSTONE_LOW_PHYS_END		(KEYSTONE_LOW_PHYS_START + \
					 KEYSTONE_LOW_PHYS_SIZE - 1)

#define KEYSTONE_HIGH_PHYS_START	0x800000000ULL
#define KEYSTONE_HIGH_PHYS_SIZE		0x400000000ULL	/* 16G */
#define KEYSTONE_HIGH_PHYS_END		(KEYSTONE_HIGH_PHYS_START + \
					 KEYSTONE_HIGH_PHYS_SIZE - 1)
#endif /* __MEMORY_H */
