/*
 * include/mach/clock.h
 * (C) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * kevin.z
 *
 * core header file for Lichee Linux BSP
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 */

#ifndef __SW_CLOCK_H__
#define __SW_CLOCK_H__

#include <linux/kernel.h>
#include "aw_ccu.h"

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

extern int clk_reset(struct clk *clk, int reset);

#endif  /* #ifndef __SW_CLOCK_H__ */

