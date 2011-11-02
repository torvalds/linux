/*
 * Copyright 2011 Calxeda, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _MACH_HIGHBANK__SYSREGS_H_
#define _MACH_HIGHBANK__SYSREGS_H_

#include <linux/io.h>

extern void __iomem *sregs_base;

#define HB_SREG_A9_PWR_REQ		0xf00
#define HB_SREG_A9_BOOT_STAT		0xf04
#define HB_SREG_A9_BOOT_DATA		0xf08

#define HB_PWR_SUSPEND			0
#define HB_PWR_SOFT_RESET		1
#define HB_PWR_HARD_RESET		2
#define HB_PWR_SHUTDOWN			3

static inline void hignbank_set_pwr_suspend(void)
{
	writel(HB_PWR_SUSPEND, sregs_base + HB_SREG_A9_PWR_REQ);
}

static inline void hignbank_set_pwr_shutdown(void)
{
	writel(HB_PWR_SHUTDOWN, sregs_base + HB_SREG_A9_PWR_REQ);
}

static inline void hignbank_set_pwr_soft_reset(void)
{
	writel(HB_PWR_SOFT_RESET, sregs_base + HB_SREG_A9_PWR_REQ);
}

static inline void hignbank_set_pwr_hard_reset(void)
{
	writel(HB_PWR_HARD_RESET, sregs_base + HB_SREG_A9_PWR_REQ);
}

#endif
