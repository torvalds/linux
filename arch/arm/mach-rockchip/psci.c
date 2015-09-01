/*
 * arch/arm/mach-rockchip/psci.c
 *
 * PSCI call interface for rockchip
 *
 * Copyright (C) 2015 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/of.h>
#include <linux/types.h>
#include <linux/rockchip/psci.h>
#include <asm/compiler.h>
#include <asm/smp_plat.h>

static u32 reg_rd_fn_smc(u64 function_id, u64 arg0, u64 arg1, u64 arg2,
			 u64 *val)
{
	asm volatile(
			__asmeq("%0", "x0")
			__asmeq("%1", "x1")
			__asmeq("%2", "x2")
			__asmeq("%3", "x3")
			"smc	#0\n"
		: "+r" (function_id), "+r" (arg0)
		: "r" (arg1), "r" (arg2));

		if (val)
			*val = arg0;

	return function_id;
}

static u32 reg_wr_fn_smc(u64 function_id, u64 arg0, u64 arg1, u64 arg2)
{
	asm volatile(
			__asmeq("%0", "x0")
			__asmeq("%1", "x1")
			__asmeq("%2", "x2")
			__asmeq("%3", "x3")
			"smc	#0\n"
		: "+r" (function_id), "+r" (arg0)
		: "r" (arg1), "r" (arg2));

	return function_id;
}

static u32 (*reg_wr_fn)(u64, u64, u64, u64) = reg_wr_fn_smc;
static u32 (*reg_rd_fn)(u64, u64, u64, u64, u64 *) = reg_rd_fn_smc;

/*
 * u64 *val: another return value
 */
u32 rockchip_psci_smc_read(u64 function_id, u64 arg0, u64 arg1, u64 arg2,
			   u64 *val)
{
	return reg_rd_fn(function_id, arg0, arg1, arg2, val);
}

u32 rockchip_psci_smc_write(u64 function_id, u64 arg0, u64 arg1, u64 arg2)
{
	return reg_wr_fn(function_id, arg0, arg1, arg2);
}

u32 rockchip_secure_reg_read32(u64 addr_phy)
{
	u64 val = 0;

	reg_rd_fn(PSCI_SIP_ACCESS_REG, 0, addr_phy, SEC_REG_RD_32, &val);

	return val;
}

u32 rockchip_secure_reg_write32(u64 addr_phy, u32 val)
{
	u64 val_64 = val;

	return reg_wr_fn(PSCI_SIP_ACCESS_REG, val_64, addr_phy, SEC_REG_WR_32);
}

/*
 * get trust firmware verison
 */
u32 rockchip_psci_smc_get_tf_ver(void)
{
	return reg_rd_fn(PSCI_SIP_RKTF_VER, 0, 0, 0, NULL);
}

/*************************** fiq debug *****************************/
static u64 ft_fiq_mem_phy;
static void __iomem *ft_fiq_mem_base;
static void (*psci_fiq_debugger_uart_irq_tf)(void *reg_base, u64 sp_el1);

void psci_fiq_debugger_uart_irq_tf_cb(u64 sp_el1, u64 offset)
{
	psci_fiq_debugger_uart_irq_tf((char *)ft_fiq_mem_base + offset, sp_el1);

	reg_wr_fn(PSCI_SIP_UARTDBG_CFG, 0, 0, UARTDBG_CFG_OSHDL_TO_OS);
}

void psci_fiq_debugger_uart_irq_tf_init(u32 irq_id, void *callback)
{
	psci_fiq_debugger_uart_irq_tf = callback;

	ft_fiq_mem_phy = reg_wr_fn(PSCI_SIP_UARTDBG_CFG, irq_id,
				   (u64)psci_fiq_debugger_uart_irq_tf_cb,
				   UARTDBG_CFG_INIT);
	ft_fiq_mem_base = ioremap(ft_fiq_mem_phy, 8 * 1024);
}

u32 psci_fiq_debugger_switch_cpu(u32 cpu)
{
	return reg_wr_fn(PSCI_SIP_UARTDBG_CFG, cpu_logical_map(cpu),
			 0, UARTDBG_CFG_OSHDL_CPUSW);
}

void psci_fiq_debugger_enable_debug(bool val)
{
	if (val)
		reg_wr_fn(PSCI_SIP_UARTDBG_CFG, 0,
			  0, UARTDBG_CFG_OSHDL_DEBUG_ENABLE);
	else
		reg_wr_fn(PSCI_SIP_UARTDBG_CFG, 0,
			  0, UARTDBG_CFG_OSHDL_DEBUG_DISABLE);
}
