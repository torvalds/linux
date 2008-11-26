/*
 *  Copyright (C) 2004 by Basler Vision Technologies AG
 *  Author: Thomas Koeller <thomas.koeller@baslerweb.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined(_ASM_RM9K_OCD_H)
#define _ASM_RM9K_OCD_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/io.h>

extern volatile void __iomem * const ocd_base;
extern volatile void __iomem * const titan_base;

#define ocd_addr(__x__)		(ocd_base + (__x__))
#define titan_addr(__x__)	(titan_base + (__x__))
#define scram_addr(__x__)	(scram_base + (__x__))

/* OCD register access */
#define ocd_readl(__offs__) __raw_readl(ocd_addr(__offs__))
#define ocd_readw(__offs__) __raw_readw(ocd_addr(__offs__))
#define ocd_readb(__offs__) __raw_readb(ocd_addr(__offs__))
#define ocd_writel(__val__, __offs__) \
	__raw_writel((__val__), ocd_addr(__offs__))
#define ocd_writew(__val__, __offs__) \
	__raw_writew((__val__), ocd_addr(__offs__))
#define ocd_writeb(__val__, __offs__) \
	__raw_writeb((__val__), ocd_addr(__offs__))

/* TITAN register access - 32 bit-wide only */
#define titan_readl(__offs__) __raw_readl(titan_addr(__offs__))
#define titan_writel(__val__, __offs__) \
	__raw_writel((__val__), titan_addr(__offs__))

/* Protect access to shared TITAN registers */
extern spinlock_t titan_lock;
extern int titan_irqflags;
#define lock_titan_regs() spin_lock_irqsave(&titan_lock, titan_irqflags)
#define unlock_titan_regs() spin_unlock_irqrestore(&titan_lock, titan_irqflags)

#endif	/* !defined(_ASM_RM9K_OCD_H) */
