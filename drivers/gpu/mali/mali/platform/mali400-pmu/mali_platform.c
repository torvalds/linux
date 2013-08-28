/*
 * Copyright (C) 2010-2011 ARM Limited. All rights reserved.
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
#include "mali_mem_validation.h"

#include <linux/module.h>
#include <linux/clk.h>
#include <mach/irqs.h>
#include <mach/clock.h>
#include <plat/sys_config.h>
#include <plat/memory.h>

#ifdef CONFIG_FB_SUNXI_RESERVED_MEM
extern unsigned long fb_start;
extern unsigned long fb_size;
static int fb_validation_range_added;
#endif


int mali_clk_div = 3;
module_param(mali_clk_div, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(mali_clk_div, "Clock divisor for mali");

struct clk *h_ahb_mali, *h_mali_clk, *h_ve_pll;
int mali_clk_flag=0;


_mali_osk_errcode_t mali_platform_init(void)
{
	unsigned long rate;
	int clk_div;
	int mali_used = 0;

#ifdef CONFIG_FB_SUNXI_RESERVED_MEM
	/* mali_platform_init() may be called multiple times,
	   but we only need to set the validation range once */
	if (!fb_validation_range_added) {
		_mali_osk_resource_t fb_resource = {
			.type = MEM_VALIDATION,
			.description = "Framebuffer",
			.cpu_usage_adjust = 0x40000000,
			.base = __phys_to_bus(fb_start),
			.size = fb_size,
			.flags = 0
		};
		mali_mem_validation_add_range(&fb_resource);
		MALI_PRINT(("permit MALI_IOC_MEM_MAP_EXT ioctl for framebuffer"
			    " (paddr=0x%08X, size=%d)\n", fb_start, fb_size));
		fb_validation_range_added = 1;
	}
#endif

	//get mali ahb clock
	h_ahb_mali = clk_get(NULL, "ahb_mali");
	if(!h_ahb_mali){
		MALI_PRINT(("try to get ahb mali clock failed!\n"));
	}
	//get mali clk
	h_mali_clk = clk_get(NULL, "mali");
	if(!h_mali_clk){
		MALI_PRINT(("try to get mali clock failed!\n"));
	}

	h_ve_pll = clk_get(NULL, "ve_pll");
	if(!h_ve_pll){
		MALI_PRINT(("try to get ve pll clock failed!\n"));
	}

	//set mali parent clock
	if(clk_set_parent(h_mali_clk, h_ve_pll)){
		MALI_PRINT(("try to set mali clock source failed!\n"));
	}

	//set mali clock
	rate = clk_get_rate(h_ve_pll);

	if(!script_parser_fetch("mali_para", "mali_used", &mali_used, 1)) {
		if (mali_used == 1) {
			if (!script_parser_fetch("mali_para", "mali_clkdiv", &clk_div, 1)) {
				if (clk_div > 0) {
					pr_info("mali: use config clk_div %d\n", clk_div);
					mali_clk_div = clk_div;
				}
			}
		}
	}

	pr_info("mali: clk_div %d\n", mali_clk_div);
	rate /= mali_clk_div;

	if(clk_set_rate(h_mali_clk, rate)){
		MALI_PRINT(("try to set mali clock failed!\n"));
	}

	if(clk_reset(h_mali_clk,0)){
		MALI_PRINT(("try to reset release failed!\n"));
	}

	MALI_PRINT(("mali clock set completed, clock is  %d Hz\n", rate));


	/*enable mali axi/apb clock*/
	if(mali_clk_flag == 0)
	{
		//printk(KERN_WARNING "enable mali clock\n");
		//MALI_PRINT(("enable mali clock\n"));
		mali_clk_flag = 1;
	       if(clk_enable(h_ahb_mali))
	       {
		     MALI_PRINT(("try to enable mali ahb failed!\n"));
	       }
	       if(clk_enable(h_mali_clk))
	       {
		       MALI_PRINT(("try to enable mali clock failed!\n"));
	        }
	}


    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_deinit(void)
{
	/*close mali axi/apb clock*/
	if(mali_clk_flag == 1)
	{
		//MALI_PRINT(("disable mali clock\n"));
		mali_clk_flag = 0;
	       clk_disable(h_mali_clk);
	       clk_disable(h_ahb_mali);
	}

    MALI_SUCCESS;
}

_mali_osk_errcode_t mali_platform_power_mode_change(mali_power_mode power_mode)
{
    MALI_SUCCESS;
}

void mali_gpu_utilization_handler(u32 utilization)
{
}

void set_mali_parent_power_domain(void* dev)
{
}


