/*
 * arch/arm/mach-meson6/board-meson6-common.c
 *
 * Copyright (C) 2011-2013 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/amlogic/of_lm.h>
#include <linux/reboot.h>
#include <plat/irq.h>
#include <plat/lm.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <mach/io.h>
#ifdef CONFIG_SMP
#include <mach/smp.h>
#endif
#include <linux/syscore_ops.h>
#include <mach/am_regs.h>

#include <linux/of_fdt.h>
#include <linux/amlogic/vmapi.h>
extern void meson_common_restart(char mode,const char *cmd);
static void meson_map_board_io(void);
extern unsigned long long aml_reserved_start;
extern unsigned long long aml_reserved_end;
extern void __init meson_timer_init(void);

//#ifdef CONFIG_AMLOGIC_VIDEOIN_MANAGER && CONFIG_CMA
#ifdef CONFIG_CMA
static int __init early_dt_scan_vm(unsigned long node, const char *uname,
				   int depth, void *data)
{
    char *p;
    unsigned long l;
    if(strcmp("vm", uname))
        return 0;

    p = of_get_flat_dt_prop(node, "status", &l);

    if(p != 0 && l > 0)
    {
        if(strncmp("ok", p, 2) == 0)
        {
            vm_reserve_cma();
        }
    }
    /* break */
    return 1;
}
#endif

static __init void meson6_reserve(void)
{

#ifdef CONFIG_CMA
    of_scan_flat_dt(early_dt_scan_vm, NULL);
#endif
}

__initdata struct map_desc meson_board_io_desc[1];

static __init void meson_map_board_io(void)
{
	meson_board_io_desc[0].virtual = PAGE_ALIGN(__phys_to_virt(aml_reserved_start)),
	meson_board_io_desc[0].pfn = __phys_to_pfn(aml_reserved_start),
	meson_board_io_desc[0].length     = aml_reserved_end - aml_reserved_start + 1,
	meson_board_io_desc[0].type       = MT_MEMORY_NONCACHED,
	iotable_init(meson_board_io_desc,ARRAY_SIZE(meson_board_io_desc));
}
static void __init meson_map_io(void)
{
	meson_map_default_io();
	meson_map_board_io();
}

static struct of_device_id mxs_of_platform_bus_ids[] = {
		{.compatible = "simple-bus",},  
		{},
};
static struct of_device_id mxs_of_lm_bus_ids[] = {
		{.compatible = "logicmodule-bus",},  
		{},
};
static int mmc_lp_suspend(void)
{
    // Disable MMC_LP_CTRL.
    printk("MMC_LP_CTRL1 before=%#x\n", aml_read_reg32(P_MMC_LP_CTRL1));
    aml_write_reg32(P_MMC_LP_CTRL1, 0x60a80000);
    printk("MMC_LP_CTRL1 after=%#x\n", aml_read_reg32(P_MMC_LP_CTRL1));
    return 0;
}
static void mmc_lp_resume(void)
{
    // Enable MMC_LP_CTRL.
    printk("MMC_LP_CTRL1 before=%#x\n", aml_read_reg32(P_MMC_LP_CTRL1));
    aml_write_reg32(P_MMC_LP_CTRL1, 0x78000030);
    aml_write_reg32(P_MMC_LP_CTRL3, 0x34f00f03); //at bootup its 0x34400f03 ?? and kreboot set it to this
    printk("MMC_LP_CTRL1 after=%#x\n", aml_read_reg32(P_MMC_LP_CTRL1));
}
static struct syscore_ops mmc_lp_syscore_ops = {
    .suspend    = mmc_lp_suspend,
    .resume     = mmc_lp_resume,
};

static __init void mmc_lp_suspend_init(void)
{
    register_syscore_ops(&mmc_lp_syscore_ops);
}

static void power_off(void)
{
	printk("--------power_off\n");
	kernel_restart("charging_reboot");
}

static __init void meson_init_machine_devicetree(void)
{
	struct device *parent;	
	parent = get_device(&platform_bus);
	
	of_platform_populate(NULL,mxs_of_platform_bus_ids,NULL,parent);
	of_lm_populate(NULL,mxs_of_lm_bus_ids,NULL,NULL);
	mmc_lp_suspend_init();
//		of_platform_populate(NULL, of_default_bus_match_table,
//		aml_meson6_auxdata_lookup, NULL);
       pm_power_off = power_off;
}



static __init void meson_init_early(void)
{
	/*
	 * Mali or some USB devices allocate their coherent buffers from atomic
	 * context. Increase size of atomic coherent pool to make sure such
	 * the allocations won't fail.
	 */
	init_dma_coherent_pool_size(SZ_4M);
	meson_cpu_version_init();
}

static void __init meson_init_irq(void)
{
	meson_init_gic_irq();
}
static const char *m6_common_board_compat[] __initdata = {
	"AMLOGIC,8726_MX",
	"AMLOGIC,8726_MXS",
	"AMLOGIC,8726_MXL",
	NULL,
};

DT_MACHINE_START(AML8726_MX, "Amlogic Meson6")
	.reserve	= meson6_reserve,
//.nr_irqs	= 
	.smp		= smp_ops(meson_smp_ops),
	.map_io		= meson_map_io,/// dt - 1
	.init_early	= meson_init_early,/// dt -2
	.init_irq		= meson_init_irq,/// dt - 3
	.init_time		= meson_timer_init, /// dt - 4
//	.handle_irq	= gic_handle_irq,
	.init_machine	= meson_init_machine_devicetree,
	.restart	= meson_common_restart,
	.dt_compat	= m6_common_board_compat,
MACHINE_END
