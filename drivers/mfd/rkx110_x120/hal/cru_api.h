/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Joseph Chen <chenjh@rock-chips.com>
 */

#ifndef _CRU_API_H_
#define _CRU_API_H_

#include "hal_def.h"
#include "hal_os_def.h"
#include "cru_core.h"
#include "cru_rkx110.h"
#include "cru_rkx120.h"
#include "cru_rkx111.h"
#include "cru_rkx121.h"

static HAL_DEFINE_MUTEX(top_lock);

static inline bool hwclk_is_enabled(struct hwclk *hw, uint32_t clk)
{
    bool ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkIsEnabled(hw, clk);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline int hwclk_enable(struct hwclk *hw, uint32_t clk)
{
    int ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkEnable(hw, clk);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline int hwclk_disable(struct hwclk *hw, uint32_t clk)
{
    int ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkDisable(hw, clk);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline bool hwclk_is_reset(struct hwclk *hw, uint32_t clk)
{
    bool ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkIsReset(hw, clk);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline int hwclk_reset(struct hwclk *hw, uint32_t clk)
{
    int ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkResetAssert(hw, clk);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline int hwclk_reset_deassert(struct hwclk *hw, uint32_t clk)
{
    int ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkResetDeassert(hw, clk);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline int hwclk_set_div(struct hwclk *hw, uint32_t clk, uint32_t div)
{
    int ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkSetDiv(hw, clk, div);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline uint32_t hwclk_get_div(struct hwclk *hw, uint32_t clk)
{
    uint32_t ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkGetDiv(hw, clk);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline int hwclk_set_mux(struct hwclk *hw, uint32_t clk, uint32_t mux)
{
    int ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkSetMux(hw, clk, mux);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline uint32_t hwclk_get_mux(struct hwclk *hw, uint32_t clk)
{
    uint32_t ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkGetMux(hw, clk);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline uint32_t hwclk_get_rate(struct hwclk *hw, uint32_t composite_clk)
{
    uint32_t ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkGetFreq(hw, composite_clk);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline int hwclk_set_rate(struct hwclk *hw, uint32_t composite_clk, uint32_t rate)
{
    int ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkSetFreq(hw, composite_clk, rate);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline int hwclk_set_testout(struct hwclk *hw, uint32_t composite_clk,
                                    uint32_t mux, uint32_t div)
{
    int ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_ClkSetTestout(hw, composite_clk, mux, div);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline void hwclk_dump_tree(HAL_ClockType type)
{
    HAL_MutexLock(&top_lock);
    HAL_CRU_ClkDumpTree(type);
    HAL_MutexUnlock(&top_lock);
}

static inline int hwclk_set_glbsrst(struct hwclk *hw, uint32_t type)
{
    int ret;

    HAL_MutexLock(&hw->lock);
    ret = HAL_CRU_SetGlbSrst(hw, type);
    HAL_MutexUnlock(&hw->lock);

    return ret;
}

static inline struct hwclk *hwclk_register(struct xferclk xfer)
{
    struct hwclk *ret;

    HAL_MutexLock(&top_lock);
    ret = HAL_CRU_Register(xfer);
    HAL_MutexUnlock(&top_lock);

    return ret;
}

static inline struct hwclk *hwclk_get(void *client)
{
    struct hwclk *ret;

    HAL_MutexLock(&top_lock);
    ret = HAL_CRU_ClkGet(client);
    HAL_MutexUnlock(&top_lock);

    return ret;
}

static inline int hwclk_init(void)
{
    return HAL_CRU_Init();
}

#endif
