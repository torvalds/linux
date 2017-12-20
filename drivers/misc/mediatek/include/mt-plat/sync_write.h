/*
 * Copyright (C) 2015 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef _MT_SYNC_WRITE_H
#define _MT_SYNC_WRITE_H

#if defined(__KERNEL__)

#include <linux/io.h>
#include <asm/cacheflush.h>

/*
 * Define macros.
 */
#define mt_reg_sync_writel(v, a) \
	do {    \
		__raw_writel((v), (void __force __iomem *)((a)));   \
		mb();  \
	} while (0)

#define mt_reg_sync_writew(v, a) \
	do {    \
		__raw_writew((v), (void __force __iomem *)((a)));   \
		mb();  \
	} while (0)

#define mt_reg_sync_writeb(v, a) \
	do {    \
		__raw_writeb((v), (void __force __iomem *)((a)));   \
		mb();  \
	} while (0)

#ifdef CONFIG_64BIT
#define mt_reg_sync_writeq(v, a) \
	do {    \
		__raw_writeq((v), (void __force __iomem *)((a)));   \
		mb();  \
	} while (0)
#endif

#else				/* __KERNEL__ */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#define mt_reg_sync_writel(v, a)        mt65xx_reg_sync_writel(v, a)
#define mt_reg_sync_writew(v, a)        mt65xx_reg_sync_writew(v, a)
#define mt_reg_sync_writeb(v, a)        mt65xx_reg_sync_writeb(v, a)

#define mb()   \
	{    \
		__asm__ __volatile__ ("dsb" : : : "memory"); \
	}

#define mt65xx_reg_sync_writel(v, a) \
	do {    \
		*(volatile unsigned int *)(a) = (v);    \
		mb(); \
	} while (0)

#define mt65xx_reg_sync_writew(v, a) \
	do {    \
		*(volatile unsigned short *)(a) = (v);    \
		mb(); \
	} while (0)

#define mt65xx_reg_sync_writeb(v, a) \
	do {    \
		*(volatile unsigned char *)(a) = (v);    \
		mb(); \
	} while (0)

#endif				/* __KERNEL__ */

#endif				/* !_MT_SYNC_WRITE_H */
