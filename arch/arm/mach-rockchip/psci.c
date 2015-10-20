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
#ifdef CONFIG_ARM
#include <asm/opcodes-sec.h>
#endif

/*
 * SMC32 id call made from arch32 or arch64
 */
static u32 reg_rd_fn_smc(u32 function_id, u32 arg0, u32 arg1,
			 u32 arg2, u32 *val)
{
	asm volatile(
#ifdef CONFIG_ARM
			__asmeq("%0", "r0")
			__asmeq("%1", "r1")
			__asmeq("%2", "r2")
			__asmeq("%3", "r3")
			__SMC(0)
#else
			__asmeq("%w0", "w0")
			__asmeq("%w1", "w1")
			__asmeq("%w2", "w2")
			__asmeq("%w3", "w3")
			"smc	#0\n"
#endif
		: "+r" (function_id), "+r" (arg0)
		: "r" (arg1), "r" (arg2));


		if (val)
			*val = arg0;

	return function_id;
}

/*
 * SMC32 id call made from arch32 or arch64
 */
static u32 reg_wr_fn_smc(u32 function_id, u32 arg0,
			 u32 arg1, u32 arg2)
{
	asm volatile(
#ifdef CONFIG_ARM
			__asmeq("%0", "r0")
			__asmeq("%1", "r1")
			__asmeq("%2", "r2")
			__asmeq("%3", "r3")
			__SMC(0)
#else
			__asmeq("%w0", "w0")
			__asmeq("%w1", "w1")
			__asmeq("%w2", "w2")
			__asmeq("%w3", "w3")
			"smc	#0\n"
#endif
		: "+r" (function_id), "+r" (arg0)
		: "r" (arg1), "r" (arg2));

	return function_id;
}

static u32 (*reg_wr_fn)(u32, u32, u32, u32) = reg_wr_fn_smc;
static u32 (*reg_rd_fn)(u32, u32, u32, u32, u32 *) = reg_rd_fn_smc;


#ifdef CONFIG_ARM64

/*
 * SMC64 id call only made from arch64
 */
static u32 reg_rd_fn_smc64(u64 function_id, u64 arg0, u64 arg1, u64 arg2,
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

/*
 * SMC64 id call only made from Arch64
 */
static u32 reg_wr_fn_smc64(u64 function_id, u64 arg0, u64 arg1, u64 arg2)
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

static u32 (*reg_wr_fn64)(u64, u64, u64, u64) = reg_wr_fn_smc64;
static u32 (*reg_rd_fn64)(u64, u64, u64, u64, u64 *) = reg_rd_fn_smc64;

u32 rockchip_psci_smc_read64(u64 function_id, u64 arg0, u64 arg1, u64 arg2,
			     u64 *val)
{
	return reg_rd_fn64(function_id, arg0, arg1, arg2, val);
}

u32 rockchip_psci_smc_write64(u64 function_id, u64 arg0, u64 arg1, u64 arg2)
{
	return reg_wr_fn64(function_id, arg0, arg1, arg2);
}

u64 rockchip_secure_reg_read64(u64 addr_phy)
{
	u64 val;

	reg_rd_fn64(PSCI_SIP_ACCESS_REG64, 0, addr_phy, SEC_REG_RD, &val);

	return val;
}

u32 rockchip_secure_reg_write64(u64 addr_phy, u64 val)
{
	return reg_wr_fn64(PSCI_SIP_ACCESS_REG64, val, addr_phy, SEC_REG_WR);
}

#endif /*CONFIG_ARM64*/

u32 rockchip_psci_smc_read(u32 function_id, u32 arg0, u32 arg1, u32 arg2,
			   u32 *val)
{
	return reg_rd_fn(function_id, arg0, arg1, arg2, val);
}

u32 rockchip_psci_smc_write(u32 function_id, u32 arg0, u32 arg1, u32 arg2)
{
	return reg_wr_fn(function_id, arg0, arg1, arg2);
}

u32 rockchip_secure_reg_read(u32 addr_phy)
{
	u32 val = 0;

	reg_rd_fn(PSCI_SIP_ACCESS_REG, 0, addr_phy, SEC_REG_RD, &val);

	return val;
}

u32 rockchip_secure_reg_write(u32 addr_phy, u32 val)
{
	return reg_wr_fn(PSCI_SIP_ACCESS_REG, val, addr_phy, SEC_REG_WR);
}

/*
 * get trust firmware verison
 */
u32 rockchip_psci_smc_get_tf_ver(void)
{
	return reg_rd_fn(PSCI_SIP_RKTF_VER, 0, 0, 0, NULL);
}

u32 psci_set_memory_secure(bool val)
{
	return reg_wr_fn(PSCI_SIP_SMEM_CONFIG, val, 0, 0);
}

/*************************** fiq debug *****************************/
#ifdef CONFIG_ARM64
static u64 ft_fiq_mem_phy;
static void __iomem *ft_fiq_mem_base;
static void (*psci_fiq_debugger_uart_irq_tf)(void *reg_base, u64 sp_el1);

void psci_fiq_debugger_uart_irq_tf_cb(u64 sp_el1, u64 offset)
{
	psci_fiq_debugger_uart_irq_tf((char *)ft_fiq_mem_base + offset, sp_el1);
	reg_wr_fn64(PSCI_SIP_UARTDBG_CFG64, 0, 0, UARTDBG_CFG_OSHDL_TO_OS);
}

void psci_fiq_debugger_uart_irq_tf_init(u32 irq_id, void *callback)
{
	psci_fiq_debugger_uart_irq_tf = callback;
	ft_fiq_mem_phy = reg_wr_fn64(PSCI_SIP_UARTDBG_CFG64, irq_id,
				     (u64)psci_fiq_debugger_uart_irq_tf_cb,
				     UARTDBG_CFG_INIT);
	ft_fiq_mem_base = ioremap(ft_fiq_mem_phy, 8 * 1024);
}

u32 psci_fiq_debugger_switch_cpu(u32 cpu)
{
	return reg_wr_fn64(PSCI_SIP_UARTDBG_CFG64, cpu_logical_map(cpu),
			   0, UARTDBG_CFG_OSHDL_CPUSW);
}

void psci_fiq_debugger_enable_debug(bool val)
{
	if (val)
		reg_wr_fn64(PSCI_SIP_UARTDBG_CFG64, 0,
			    0, UARTDBG_CFG_OSHDL_DEBUG_ENABLE);
	else
		reg_wr_fn64(PSCI_SIP_UARTDBG_CFG64, 0,
			    0, UARTDBG_CFG_OSHDL_DEBUG_DISABLE);
}
#endif
