/*
 * arch/arm/plat-sunxi/include/plat/clock.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __SW_CLOCK_H__
#define __SW_CLOCK_H__

#include <linux/kernel.h>
#include <linux/clocksource.h>
#include <mach/aw_ccu.h>

/* define clock type            */
typedef enum __CCU_CLK_TYPE
{
    CCU_CLK_TYPE_SYS,
    CCU_CLK_TYPE_MOD,

} __ccu_clk_type_e;

typedef enum __CCU_CLK_CHANGE
{
    CCU_CLK_CHANGE_NONE,
    CCU_CLK_CHANGE_PREPARE,
    CCU_CLK_CHANGE_DONE,

} __ccu_clk_change_e;


typedef struct clk
{
    __aw_ccu_clk_t  *clk;       /* clock handle from ccu csp                            */
    __s32           usr_cnt;    /* user count                                           */
    __s32           enable;     /* enable count, when it down to 0, it will be disalbe  */
    __s32           hash;       /* hash value, for fast search without string compare   */

    __aw_ccu_clk_t  *(*get_clk)(__s32 id);
                                /* set clock                                            */
    __aw_ccu_err_e  (*set_clk)(__aw_ccu_clk_t *clk);
                                /* get clock                                            */
    struct clk      *parent;    /* parent clock node pointer                            */
    struct clk      *child;     /* child clock node pinter                              */
    struct clk      *left;      /* left brother node pointer                            */
    struct clk      *right;     /* right bother node pointer                            */

} __ccu_clk_t;

static inline const char *clk_name(struct clk *clk)
{
	return clk->clk->name;
}

extern int clk_reset(struct clk *clk, int reset);
cycle_t aw_clksrc_read(struct clocksource *cs);

#endif  /* #ifndef __SW_CLOCK_H__ */
