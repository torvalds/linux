/*
 * arch/arm/mach-sun7i/clock/clock.c
 * (c) Copyright 2010-2015
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * James Deng <csjamesdeng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <mach/includes.h>

#include "ccm_i.h"

__ccu_clk_t aw_clock[AW_CCU_CLK_CNT];

static struct clk_lookup lookups[AW_CCU_CLK_CNT];

/*
 * clock manage initialize.
 *
 * Returns 0.
 */
int clk_init(void)
{
    int i;
#ifdef CONFIG_AW_ASIC_PLATFORM
    script_item_u val;
    struct clk *clk;
#endif /* CONFIG_AW_ASIC_PLATFORM */

    CCU_INF("aw clock manager init\n");

    /* initialize clock controller unit */
    aw_ccu_init();

    /* clear the data structure */
    memset((void *)aw_clock, 0, sizeof(aw_clock));
    memset((void *)lookups, 0, sizeof(lookups));

    for (i = 0; i < AW_CCU_CLK_CNT; i++) {
        /* initiate clock */
        if (aw_ccu_get_clk(i, &aw_clock[i]) != 0) {
            CCU_ERR("try to get clock %d informaiton failed\n", i);
        }

        /* init clock spin lock */
        CCU_LOCK_INIT(&aw_clock[i].lock);

        /* register clk device */
        lookups[i].con_id = aw_clock[i].aw_clk->name;
        lookups[i].clk    = &aw_clock[i];
        clkdev_add(&lookups[i]);
    }

    /* initiate some clocks */
    lookups[AW_MOD_CLK_SMPTWD].dev_id = "smp_twd";

#ifdef CONFIG_AW_FPGA_PLATFORM
    CCU_INF("skip config pll on fpga\n");
#elif defined CONFIG_AW_ASIC_PLATFORM
    /* config plls */
    if (script_get_item("clock", "pll3", &val) ==
        SCIRPT_ITEM_VALUE_TYPE_INT) {
        CCU_INF("script config pll3 to %dMHz\n", val.val);
        if (val.val >= 27 && val.val <= 381) {
            clk = &aw_clock[AW_SYS_CLK_PLL3];
            clk_enable(clk);
            clk_set_rate(clk, val.val * 1000000);
        } else {
            CCU_ERR("    invalid value, must in 27MHz ~ 381MHz\n");
        }
    }

    if (script_get_item("clock", "pll4", &val) ==
        SCIRPT_ITEM_VALUE_TYPE_INT) {
        CCU_INF("script config pll4 to %dMHz\n", val.val);
        if (val.val >= 240 && val.val <= 2000) {
            clk = &aw_clock[AW_SYS_CLK_PLL4];
            clk_enable(clk);
            clk_set_rate(clk, val.val * 1000000);
        } else {
            CCU_ERR("    invalid value, must in 240MHz ~ 2GHz\n");
        }
    }

    clk = &aw_clock[AW_SYS_CLK_PLL6];
    if (script_get_item("clock", "pll6", &val) ==
        SCIRPT_ITEM_VALUE_TYPE_INT) {
        CCU_INF("script config pll6 to %dMHz\n", val.val);
        if (val.val < 240 || val.val > 2000) {
            CCU_ERR("    invalid value, must in 240MHz ~ 2GHz\n");
            val.val = 600;
            CCU_INF("    change to %dMHz\n", val.val);
        }
    } else {
        val.val = 600;
    }
    clk_enable(clk);
    clk_set_rate(clk, val.val * 1000000);

    if (script_get_item("clock", "pll7", &val) ==
        SCIRPT_ITEM_VALUE_TYPE_INT) {
        CCU_INF("script config pll7 to %dMHz\n", val.val);
        if (val.val >= 27 && val.val <= 381) {
            clk = &aw_clock[AW_SYS_CLK_PLL7];
            clk_enable(clk);
            clk_set_rate(clk, val.val * 1000000);
        } else {
            CCU_ERR("    invalid value, must in 27MHz ~ 381MHz\n");
        }
    }

    if (script_get_item("clock", "pll8", &val) ==
        SCIRPT_ITEM_VALUE_TYPE_INT) {
        CCU_INF("script config pll8 to %dMHz\n", val.val);
        if (val.val >= 240 && val.val <= 2000) {
            clk = &aw_clock[AW_SYS_CLK_PLL8];
            clk_enable(clk);
            clk_set_rate(clk, val.val * 1000000);
        } else {
            CCU_ERR("    invalid value, must in 240MHz ~ 2GHz\n");
        }
    }
#endif /* CONFIG_AW_ASIC_PLATFORM */

    return 0;
}
arch_initcall(clk_init);

int __clk_get(struct clk *hclk)
{
    /*
     * just noitify, do nothing now,
     * if you want record if the clock used count,
     * you can add code here
     */
    return 1;
}

void __clk_put(struct clk *clk)
{
    /*
     * just noitify, do nothing now,
     * if you want record if the clock used count,
     * you can add code here
     */
}

int clk_enable(struct clk *clk)
{
    DEFINE_FLAGS(flags);

    if ((clk == NULL) || IS_ERR(clk)) {
        CCU_ERR("%s: invalid handle\n", __func__);
        return -EINVAL;
    }

    CCU_LOCK(&clk->lock, flags);
    if (0 == clk->enable) {
        clk->ops->set_status(clk->aw_clk->id, AW_CCU_CLK_ON);
    }
    clk->enable++;
    CCU_UNLOCK(&clk->lock, flags);

    CCU_DBG("%s: %s is enabled, count #%d\n", __func__, clk->aw_clk->name, clk->enable);

    return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
    DEFINE_FLAGS(flags);

    if (clk == NULL || IS_ERR(clk)) {
        CCU_ERR("%s: invalid handle\n", __func__);
        return;
    }

    CCU_LOCK(&clk->lock, flags);
    if (clk->enable)
        clk->enable--;
    if (clk->enable) {
        CCU_UNLOCK(&clk->lock, flags);
        CCU_DBG("%s: %s is disabled, count #%d\n", __func__, clk->aw_clk->name, clk->enable);
        return;
    }
    clk->ops->set_status(clk->aw_clk->id, AW_CCU_CLK_OFF);
    CCU_UNLOCK(&clk->lock, flags);

    CCU_DBG("%s: %s is disabled, count #%d\n", __func__, clk->aw_clk->name, clk->enable);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
    unsigned long ret = 0;
    DEFINE_FLAGS(flags);

    if ((clk == NULL) || IS_ERR(clk)) {
        CCU_ERR("%s: invalid handle\n", __func__);
        return 0;
    }

    CCU_LOCK(&clk->lock, flags);
    clk->aw_clk->rate = clk->ops->get_rate(clk->aw_clk->id);
    ret = (unsigned long)clk->aw_clk->rate;
    CCU_UNLOCK(&clk->lock, flags);

    CCU_DBG("%s: %s current rate is %llu\n", __func__, clk->aw_clk->name, clk->aw_clk->rate);

    return ret;
}
EXPORT_SYMBOL(clk_get_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
    DEFINE_FLAGS(flags);

    if (clk == NULL || IS_ERR(clk)) {
        CCU_ERR("%s: invalid handle\n", __func__);
        return -EINVAL;
    }

    CCU_LOCK(&clk->lock, flags);
    if (clk->ops->set_rate(clk->aw_clk->id, rate) == 0) {
        clk->aw_clk->rate = clk->ops->get_rate(clk->aw_clk->id);
        CCU_UNLOCK(&clk->lock, flags);
        CCU_DBG("%s: set %s rate to %lu, actual rate is %llu\n", __func__,
                clk->aw_clk->name, rate, clk->aw_clk->rate);
        return 0;
    }
    CCU_UNLOCK(&clk->lock, flags);

    CCU_ERR("%s: set %s rate to %lu failed\n", __func__,
            clk->aw_clk->name, rate);

    return -1;
}
EXPORT_SYMBOL(clk_set_rate);

struct clk *clk_get_parent(struct clk *clk)
{
    struct clk *clk_ret = NULL;
    DEFINE_FLAGS(flags);

    if ((clk == NULL) || IS_ERR(clk)) {
        CCU_ERR("%s: invalid handle\n", __func__);
        return NULL;
    }

    CCU_LOCK(&clk->lock, flags);
    clk_ret = &aw_clock[clk->aw_clk->parent];
    CCU_UNLOCK(&clk->lock, flags);

    CCU_DBG("%s: %s's parent is %s\n", __func__, clk->aw_clk->name,
            clk_ret->aw_clk->name);

    return clk_ret;
}
EXPORT_SYMBOL(clk_get_parent);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
    DEFINE_FLAGS(flags);

    if ((clk == NULL) || IS_ERR(clk) ||
        (parent == NULL) || IS_ERR(parent)) {
        CCU_ERR("%s: invalid handle\n", __func__);
        return -EINVAL;
    }

    CCU_LOCK(&clk->lock, flags);
    if (clk->ops->set_parent(clk->aw_clk->id, parent->aw_clk->id) == 0) {
        clk->aw_clk->parent = clk->ops->get_parent(clk->aw_clk->id);
        clk->aw_clk->rate   = clk->ops->get_rate(clk->aw_clk->id);
        CCU_UNLOCK(&clk->lock, flags);
        CCU_DBG("%s: set %s parent to %s, actual parent is %s, current rate is %llu\n",
                __func__, clk->aw_clk->name, parent->aw_clk->name,
                aw_clock[clk->aw_clk->parent].aw_clk->name, clk->aw_clk->rate);
        return 0;
    }
    CCU_UNLOCK(&clk->lock, flags);

    CCU_ERR("%s: set %s parent to %s failed\n", __func__,
            clk->aw_clk->name, parent->aw_clk->name);

    return -1;
}
EXPORT_SYMBOL(clk_set_parent);

int clk_reset(struct clk *clk, __aw_ccu_clk_reset_e reset)
{
    DEFINE_FLAGS(flags);

    if ((clk == NULL) || IS_ERR(clk)) {
        CCU_ERR("%s: invalid handle\n", __func__);
        return -EINVAL;
    }

    CCU_LOCK(&clk->lock, flags);
    clk->ops->set_reset(clk->aw_clk->id, reset);
    clk->aw_clk->reset = reset;
    CCU_UNLOCK(&clk->lock, flags);

    CCU_DBG("%s: %s reset done\n", __func__, clk->aw_clk->name);

    return 0;
}
EXPORT_SYMBOL(clk_reset);
