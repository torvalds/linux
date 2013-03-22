/*
 * arch/arm/mach-sun4i/clock/clock.c
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/cpufreq.h>
#include <linux/debugfs.h>
#include <linux/io.h>

#include <mach/clock.h>
#include <mach/system.h>

/* we predefine the count here, but it's ugly, maybe malloc is better */
#define MAX_SYSTEM_CLK_CNT  (32)
#define MAX_MODULE_CLK_CNT  (200)

#undef CCU_DBG
#undef CCU_ERR
#if (0)
    #define CCU_DBG     printk
    #define CCU_ERR     printk
#else
    #define CCU_DBG(...)
    #define CCU_ERR(...)
#endif


// alloc memory for store clock informatioin, maybe malloc is better
static __s32 SysClkCnt;
static __s32 ModClkCnt;
static struct clk ccu_sys_clk[MAX_SYSTEM_CLK_CNT];
static struct clk ccu_mod_clk[MAX_MODULE_CLK_CNT];

// lock for operation protect
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);


/*
*********************************************************************************************************
*                           clk_init
*
*Description: clock management initialise.
*
*Arguments  : none
*
*Return     : result
*               0,  initialise successed;
*              -1,  initialise failed;
*
*Notes      :
*
*********************************************************************************************************
*/
int clk_init(void)
{
    __s32           i;
    struct clk      *tmpSclk;

    CCU_DBG("aw clock manager init!\n");

    //initialise clock controller unit
    if(AW_CCU_ERR_NONE != aw_ccu_init())
    {
        CCU_ERR("csp ccmu initial failed!\n");
        return -1;
    }

    //clear the data structure
    SysClkCnt = 0;
    memset((void *)&ccu_sys_clk, 0, sizeof(ccu_sys_clk));
    ModClkCnt = 0;
    memset((void *)&ccu_mod_clk, 0, sizeof(ccu_mod_clk));

    //get system clock information
    SysClkCnt = aw_ccu_get_sys_clk_cnt();
    if(SysClkCnt > MAX_SYSTEM_CLK_CNT)
    {
        CCU_ERR("system clock count define (%d) is invalid! actule is:%d\n", MAX_SYSTEM_CLK_CNT, SysClkCnt);
        return -1;
    }
    for(i=0; i<SysClkCnt; i++)
    {
        ccu_sys_clk[i].clk = aw_ccu_get_sys_clk((__aw_ccu_sys_clk_e)i);
        if(ccu_sys_clk[i].clk)
        {
            tmpSclk = &ccu_sys_clk[ccu_sys_clk[i].clk->parent];
            ccu_sys_clk[i].parent = tmpSclk;
            ccu_sys_clk[i].set_clk = &aw_ccu_set_sys_clk;
            ccu_sys_clk[i].get_clk = (__aw_ccu_clk_t * (*)(__s32))&aw_ccu_get_sys_clk;
            ccu_sys_clk[i].hash = ccu_clk_calc_hash(ccu_sys_clk[i].clk->name);
            if(ccu_sys_clk[i].clk->onoff == AW_CCU_CLK_ON)
            {
                tmpSclk->usr_cnt++;
            }
        }
    }

    //get module clock information
    ModClkCnt = aw_ccu_get_mod_clk_cnt();
    if(ModClkCnt > MAX_MODULE_CLK_CNT)
    {
        CCU_ERR("module clock count define (%d) is invalid! actule is:%d\n", MAX_MODULE_CLK_CNT, ModClkCnt);
        return -1;
    }
    for(i=0; i<ModClkCnt; i++)
    {
        ccu_mod_clk[i].clk = aw_ccu_get_mod_clk((__aw_ccu_sys_clk_e)i);
        if(ccu_mod_clk[i].clk)
        {
            tmpSclk = &ccu_sys_clk[ccu_mod_clk[i].clk->parent];
            ccu_mod_clk[i].parent = tmpSclk;
            ccu_mod_clk[i].set_clk = &aw_ccu_set_mod_clk;
            ccu_mod_clk[i].get_clk = (__aw_ccu_clk_t * (*)(__s32))&aw_ccu_get_mod_clk;
            ccu_mod_clk[i].hash = ccu_clk_calc_hash(ccu_mod_clk[i].clk->name);
            if(ccu_mod_clk[i].clk->onoff == AW_CCU_CLK_ON)
            {
                tmpSclk->usr_cnt++;
            }
            if(tmpSclk->child)
            {
                ccu_mod_clk[i].right = tmpSclk->child;
                tmpSclk->child->left = &ccu_mod_clk[i];
            }
            tmpSclk->child = &ccu_mod_clk[i];
        }
    }

    /* enable pll for use, it need be modified to dynamic */
    tmpSclk = &ccu_sys_clk[AW_SYS_CLK_PLL2];
    tmpSclk->clk->onoff = AW_CCU_CLK_ON;
    tmpSclk->set_clk(tmpSclk->clk);

    tmpSclk = &ccu_sys_clk[AW_SYS_CLK_PLL3];
    tmpSclk->clk->onoff = AW_CCU_CLK_ON;
    tmpSclk->set_clk(tmpSclk->clk);

	/* initiate PLL4 */
#if(USE_PLL6M_REPLACE_PLL4)
	if (SUNXI_VER_A10C == sw_get_ic_ver()) {
        tmpSclk = &ccu_sys_clk[AW_SYS_CLK_PLL4];
        tmpSclk->clk->onoff = AW_CCU_CLK_OFF;
        tmpSclk->set_clk(tmpSclk->clk);
	} else
#endif
	{
        tmpSclk = &ccu_sys_clk[AW_SYS_CLK_PLL4];
        tmpSclk->clk->rate  = 960000000;
        tmpSclk->set_clk(tmpSclk->clk);
        tmpSclk->clk->onoff = AW_CCU_CLK_ON;
        tmpSclk->set_clk(tmpSclk->clk);
	}

	/* initiate PLL6 */
	tmpSclk = &ccu_sys_clk[AW_SYS_CLK_PLL6];
	tmpSclk->clk->rate  = 1200000000;
	tmpSclk->set_clk(tmpSclk->clk);
	tmpSclk->clk->onoff = AW_CCU_CLK_ON;
	tmpSclk->set_clk(tmpSclk->clk);

	tmpSclk = &ccu_sys_clk[AW_SYS_CLK_PLL6M];
	tmpSclk->clk->rate  = 100000000;
	tmpSclk->set_clk(tmpSclk->clk);
	tmpSclk->clk->onoff = AW_CCU_CLK_ON;
	tmpSclk->set_clk(tmpSclk->clk);

	tmpSclk = &ccu_sys_clk[AW_SYS_CLK_PLL62];
	tmpSclk->clk->rate  = 600000000;
	tmpSclk->set_clk(tmpSclk->clk);
	tmpSclk->clk->onoff = AW_CCU_CLK_ON;
	tmpSclk->set_clk(tmpSclk->clk);

	tmpSclk = clk_get(NULL, "apb1");
	if (tmpSclk) {
		struct clk *tmpClk = &ccu_sys_clk[AW_SYS_CLK_HOSC];
		clk_set_parent(tmpSclk, tmpClk);
	} else {
		CCU_ERR("try to get apb1 clock handle failed!\n");
	}

    tmpSclk = &ccu_sys_clk[AW_SYS_CLK_PLL7];
    tmpSclk->clk->onoff = AW_CCU_CLK_ON;
    tmpSclk->set_clk(tmpSclk->clk);

    return 0;
}
arch_initcall(clk_init);


struct clk * clk_get(struct device *dev, const char *id)
{
    __s32   i = 0;
    __s32   tmpHash = ccu_clk_calc_hash((char *)id);

    CCU_DBG("%s:%d:%s:Get clock %s !\n", __FILE__, __LINE__, __FUNCTION__, id);
    if(!id)
    {
        return NULL;
    }

    mutex_lock(&clocks_mutex);

    /* search system clock table */
    for(i=0; i<SysClkCnt; i++)
    {
        if(tmpHash == ccu_sys_clk[i].hash)
        {
            if(!strcmp(id, ccu_sys_clk[i].clk->name))
            {
                ccu_sys_clk[i].usr_cnt++;
                mutex_unlock(&clocks_mutex);
                return &ccu_sys_clk[i];
            }
        }
    }

    /* search module clock table */
    for(i=0; i<ModClkCnt; i++)
    {
        if(tmpHash == ccu_mod_clk[i].hash)
        {
            if(!strcmp(id, ccu_mod_clk[i].clk->name))
            {
                ccu_mod_clk[i].usr_cnt++;
                mutex_unlock(&clocks_mutex);
                return &ccu_mod_clk[i];
            }
        }
    }
    CCU_ERR("%s:%d:%s: (%s) failed!\n", __FILE__, __LINE__, __FUNCTION__, id);

    mutex_unlock(&clocks_mutex);

    return NULL;
}
EXPORT_SYMBOL(clk_get);


int clk_enable(struct clk *clk)
{
    unsigned long flags;
    int ret = 0;

    if((clk == NULL) || IS_ERR(clk))
        return -EINVAL;

    spin_lock_irqsave(&clockfw_lock, flags);
    if(clk->clk->onoff == AW_CCU_CLK_OFF)
    {
        /* update clock parameter */
        clk->clk = clk->get_clk(clk->clk->id);

        /* try to enable clock */
        clk->clk->onoff = AW_CCU_CLK_ON;
        ret = clk->set_clk(clk->clk);
    }
    if(!ret) {
        clk->enable++;
    }
    spin_unlock_irqrestore(&clockfw_lock, flags);

    return ret;
}
EXPORT_SYMBOL(clk_enable);


void clk_disable(struct clk *clk)
{
    unsigned long flags;

    if(clk == NULL || IS_ERR(clk) || !clk->enable)
        return;

    CCU_DBG("%s:%d:%s: %s !\n", __FILE__, __LINE__, __FUNCTION__, clk->clk->name);

    spin_lock_irqsave(&clockfw_lock, flags);
    clk->enable--;
    if(clk->enable){
        spin_unlock_irqrestore(&clockfw_lock, flags);
        return;
    }
    if(clk->clk->onoff == AW_CCU_CLK_ON)
    {
        /* update clock parameter */
        clk->clk = clk->get_clk(clk->clk->id);

        /* try to disalbe clock */
        clk->clk->onoff = AW_CCU_CLK_OFF;
        clk->set_clk(clk->clk);
    }
    spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_disable);


unsigned long clk_get_rate(struct clk *clk)
{
    unsigned long   flags;
    unsigned long   ret = 0;

    if((clk == NULL) || IS_ERR(clk))
    {
        return 0;
    }

    CCU_DBG("%s:%d:%s: %s !\n", __FILE__, __LINE__, __FUNCTION__, clk->clk->name);

    spin_lock_irqsave(&clockfw_lock, flags);

    clk->clk = clk->get_clk(clk->clk->id);
    ret = (unsigned long)clk->clk->rate;

    spin_unlock_irqrestore(&clockfw_lock, flags);

    return ret;
}
EXPORT_SYMBOL(clk_get_rate);


unsigned long clk_get_rate_nolock(struct clk *clk)
{
    unsigned long   ret = 0;

    if((clk == NULL) || IS_ERR(clk))
    {
        return 0;
    }

    CCU_DBG("%s:%d:%s: %s !\n", __FILE__, __LINE__, __FUNCTION__, clk->clk->name);

    clk->clk = clk->get_clk(clk->clk->id);
    ret = (unsigned long)clk->clk->rate;

    return ret;
}


int clk_set_rate(struct clk *clk, unsigned long rate)
{
    unsigned long   flags;
    int ret = 0;

    if(clk == NULL || IS_ERR(clk))
        return -1;

    CCU_DBG("%s:%d:%s: %s !\n", __FILE__, __LINE__, __FUNCTION__, clk->clk->name);

    spin_lock_irqsave(&clockfw_lock, flags);
    /* update clock parameter */
    clk->clk = clk->get_clk(clk->clk->id);
    clk->clk->rate = rate;
    ret = clk->set_clk(clk->clk);

    spin_unlock_irqrestore(&clockfw_lock, flags);

    return ret;
}
EXPORT_SYMBOL(clk_set_rate);


struct clk *clk_get_parent(struct clk *clk)
{
    struct clk *parent = NULL;

    if((clk == NULL) || IS_ERR(clk))
    {
        return NULL;
    }

    CCU_DBG("%s:%d:%s: %s !\n", __FILE__, __LINE__, __FUNCTION__, clk->clk->name);
    parent = clk->parent;

    return parent;
}
EXPORT_SYMBOL(clk_get_parent);


int clk_set_parent(struct clk *clk, struct clk *parent)
{
    unsigned long   flags;
    int ret = -1;
    struct clk *old_parent;

    if((clk == NULL) || IS_ERR(parent))
    {
        return ret;
    }

    if (parent == NULL || IS_ERR(parent))
    {
        return ret;
    }

    CCU_DBG("%s:%d:%s: %s !\n", __FILE__, __LINE__, __FUNCTION__, clk->clk->name);

    spin_lock_irqsave(&clockfw_lock, flags);

    /* update clock parameter */
    clk->clk = clk->get_clk(clk->clk->id);
    old_parent = clk->parent;
    clk->clk->rate = clk_get_rate_nolock(parent) / (clk_get_rate_nolock(old_parent) / clk_get_rate_nolock(clk));
    clk->clk->parent = parent->clk->id;
    ret = clk->set_clk(clk->clk);
    if(ret){
        clk->clk->parent = old_parent->clk->id;
    }
    else{
        clk->parent = parent;
    }
    spin_unlock_irqrestore(&clockfw_lock, flags);

    return ret;
}
EXPORT_SYMBOL(clk_set_parent);


void clk_put(struct clk *clk)
{
    unsigned long   flags;

    if((clk == NULL) || IS_ERR(clk))
    {
        return;
    }

    if(!clk->usr_cnt)
    {
        return;
    }

    CCU_DBG("%s:%d:%s: %s !\n", __FILE__, __LINE__, __FUNCTION__, clk->clk->name);

    spin_lock_irqsave(&clockfw_lock, flags);
    clk->usr_cnt--;
    spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_put);


int clk_reset(struct clk *clk, int reset)
{
    unsigned long   flags;
    int ret = 0;

    if((clk == NULL) || IS_ERR(clk))
    {
        return -EINVAL;
    }

    CCU_DBG("%s:%d:%s: %s !\n", __FILE__, __LINE__, __FUNCTION__, clk->clk->name);

    spin_lock_irqsave(&clockfw_lock, flags);
    /* update clock parameter */
    clk->clk = clk->get_clk(clk->clk->id);
    reset? (clk->clk->reset = AW_CCU_CLK_RESET) : (clk->clk->reset = AW_CCU_CLK_NRESET);
    ret = clk->set_clk(clk->clk);
    spin_unlock_irqrestore(&clockfw_lock, flags);

    return ret;
}
EXPORT_SYMBOL(clk_reset);

