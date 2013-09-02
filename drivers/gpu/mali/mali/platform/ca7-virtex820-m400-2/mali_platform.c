/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_platform.c
 * Platform specific Mali driver functions for a default platform
 */
#include "mali_kernel_common.h"
#include "mali_osk.h"
#include "mali_platform.h"

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <mach/irqs.h>
#include <mach/clock.h>
#include <mach/sys_config.h>

#error the build would fail if this junk was really needed

int mali_clk_div = 1;
module_param(mali_clk_div, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_clk_div, "Clock divisor for mali");

struct clk *h_ahb_mali, *h_mali_clk, *h_sys_pll8;
int mali_clk_flag=0;
_mali_osk_errcode_t mali_platform_init(void)
{
	unsigned long rate;
	script_item_u   mali_use, clk_drv;

	/* get mali ahb clock */
	h_ahb_mali = clk_get(NULL, CLK_AHB_MALI);
	if(!h_ahb_mali || IS_ERR(h_ahb_mali)) {
		MALI_PRINT(("try to get ahb mali clock failed!\n"));
	} else
		pr_info("%s(%d): get %s handle success!\n", __func__, __LINE__, CLK_AHB_MALI);

	rate = clk_get_rate(h_ahb_mali);
	pr_warning("ahb mali clk=%d\n", rate);

	/* get mali clk */
	h_mali_clk = clk_get(NULL, CLK_MOD_MALI);
	if(!h_mali_clk || IS_ERR(h_ahb_mali)) {
		MALI_PRINT(("try to get mali clock failed!\n"));
	} else {
		pr_info("%s(%d): get %s handle success!\n", __func__, __LINE__, CLK_MOD_MALI);
	}

	rate = clk_get_rate(h_mali_clk);
	pr_warning("mali clk=%d\n", rate);

	h_sys_pll8 = clk_get(NULL, CLK_SYS_PLL8);
	if(!h_sys_pll8 || IS_ERR(h_ahb_mali)) {
		MALI_PRINT(("try to get sys pll8 clock failed!\n"));
	} else
		pr_info("%s(%d): get %s handle success!\n", __func__, __LINE__, CLK_SYS_PLL8);

	/* set mali parent clock */
	if(clk_set_parent(h_mali_clk, h_sys_pll8)) {
		MALI_PRINT(("try to set mali clock source failed!\n"));
	} else
		pr_info("%s(%d): set mali clock source success!\n", __func__, __LINE__);

	/* set mali clock */
	rate = clk_get_rate(h_sys_pll8);
	pr_info("%s(%d): get sys pll8 rate %d!\n", __func__, __LINE__, rate);

	if(SCIRPT_ITEM_VALUE_TYPE_INT == script_get_item("mali_para", "mali_used", &mali_use)) {
		pr_info("%s(%d): get mali_para->mali_used success! mali_use %d\n", __func__, __LINE__, mali_use.val);
		if(mali_use.val == 1) {
			if(SCIRPT_ITEM_VALUE_TYPE_INT == script_get_item("mali_para", "mali_clkdiv", &clk_drv)) {
				pr_info("%s(%d): get mali_para->mali_clkdiv success! clk_drv %d\n", __func__,
					__LINE__, clk_drv.val);
				if(clk_drv.val > 0)
					mali_clk_div = clk_drv.val;
			} else
				pr_info("%s(%d): get mali_para->mali_clkdiv failed!\n", __func__, __LINE__);
		}
	} else
		pr_info("%s(%d): get mali_para->mali_used failed!\n", __func__, __LINE__);

	pr_info("%s(%d): mali_clk_div %d\n", __func__, __LINE__, mali_clk_div);
	rate /= mali_clk_div;
	if(clk_set_rate(h_mali_clk, rate)) {
		MALI_PRINT(("try to set mali clock failed!\n"));
	} else
		pr_info("%s(%d): set mali clock rate success!\n", __func__, __LINE__);

	if(clk_reset(h_mali_clk, AW_CCU_CLK_NRESET)) {
		MALI_PRINT(("try to reset release failed!\n"));
	} else
		pr_info("%s(%d): reset release success!\n", __func__, __LINE__);

	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit(void)
{
	/* close mali axi/apb clock */
	if(mali_clk_flag == 1) {
		/* MALI_PRINT(("disable mali clock\n")); */
		mali_clk_flag = 0;
		clk_disable(h_mali_clk);
		clk_disable(h_ahb_mali);
	}
	MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{	
	if(power_mode == MALI_POWER_MODE_ON) {
		if(mali_clk_flag == 0) {
			/*
			  printk(KERN_WARNING "enable mali clock\n");
			  MALI_PRINT(("enable mali clock\n"));
			*/
			mali_clk_flag = 1;
			if(clk_enable(h_ahb_mali)) {
				MALI_PRINT(("try to enable mali ahb failed!\n"));
			}
			if(clk_enable(h_mali_clk)) {
				MALI_PRINT(("try to enable mali clock failed!\n"));
			}
		}
	}
	else if(power_mode == MALI_POWER_MODE_LIGHT_SLEEP) {
		/* close mali axi/apb clock */
		if(mali_clk_flag == 1) {
			/* MALI_PRINT(("disable mali clock\n")); */
			mali_clk_flag = 0;
			clk_disable(h_mali_clk);
			clk_disable(h_ahb_mali);
		}
	}
	else if(power_mode == MALI_POWER_MODE_DEEP_SLEEP) {
		/* close mali axi/apb clock */
		if(mali_clk_flag == 1) {
			/* MALI_PRINT(("disable mali clock\n")); */
			mali_clk_flag = 0;
			clk_disable(h_mali_clk);
			clk_disable(h_ahb_mali);
		}
	}
	MALI_SUCCESS;
}

void mali_gpu_utilization_handler(u32 utilization)
{
}

void set_mali_parent_power_domain(void* dev)
{
}
