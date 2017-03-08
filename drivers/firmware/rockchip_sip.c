/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2016, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/arm-smccc.h>
#include <linux/io.h>
#include <linux/rockchip/rockchip_sip.h>
#include <asm/cputype.h>
#include <asm/smp_plat.h>
#include <uapi/linux/psci.h>

#ifdef CONFIG_64BIT
#define PSCI_FN_NATIVE(version, name)	PSCI_##version##_FN64_##name
#else
#define PSCI_FN_NATIVE(version, name)	PSCI_##version##_FN_##name
#endif

#define SIZE_PAGE(n)	((n) << 12)

static struct arm_smccc_res __invoke_sip_fn_smc(unsigned long function_id,
						unsigned long arg0,
						unsigned long arg1,
						unsigned long arg2)
{
	struct arm_smccc_res res;

	arm_smccc_smc(function_id, arg0, arg1, arg2, 0, 0, 0, 0, &res);
	return res;
}

struct arm_smccc_res sip_smc_ddr_cfg(u32 arg0, u32 arg1,
				     u32 arg2)
{
	return __invoke_sip_fn_smc(SIP_DDR_CFG32, arg0, arg1, arg2);
}

struct arm_smccc_res sip_smc_get_atf_version(void)
{
	return __invoke_sip_fn_smc(SIP_ATF_VERSION32, 0, 0, 0);
}

struct arm_smccc_res sip_smc_get_sip_version(void)
{
	return __invoke_sip_fn_smc(SIP_SIP_VERSION32, 0, 0, 0);
}

int sip_smc_set_suspend_mode(u32 ctrl,
			     u32 config1,
			     u32 config2)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_SUSPEND_MODE32, ctrl,
				  config1, config2);

	return res.a0;
}

int rk_psci_virtual_poweroff(void)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(PSCI_FN_NATIVE(1_0, SYSTEM_SUSPEND),
				  0, 0, 0);
	return res.a0;
}

u32 sip_smc_secure_reg_read(u32 addr_phy)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_ACCESS_REG, 0, addr_phy, SECURE_REG_RD);
	if (res.a0)
		pr_err("%s error: %d, addr phy: 0x%x\n",
		       __func__, (int)res.a0, addr_phy);

	return res.a1;
}

int sip_smc_secure_reg_write(u32 addr_phy, u32 val)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_ACCESS_REG, val, addr_phy, SECURE_REG_WR);
	if (res.a0)
		pr_err("%s error: %d, addr phy: 0x%x\n",
		       __func__, (int)res.a0, addr_phy);

	return res.a0;
}

/************************** fiq debugger **************************************/
static u64 ft_fiq_mem_phy;
static void __iomem *ft_fiq_mem_base;
static void (*psci_fiq_debugger_uart_irq_tf)(void *reg_base, u64 sp_el1);
static u32 fig_init_flag;

u32 rockchip_psci_smc_get_tf_ver(void)
{
	struct arm_smccc_res res;

	arm_smccc_smc(PSCI_0_2_FN_PSCI_VERSION, 0, 0, 0, 0, 0, 0, 0, &res);
	return 0x00010005;
}

void psci_fiq_debugger_uart_irq_tf_cb(u64 sp_el1, u64 offset)
{
	psci_fiq_debugger_uart_irq_tf((char *)ft_fiq_mem_base + offset, sp_el1);
	__invoke_sip_fn_smc(PSCI_SIP_UARTDBG_CFG64, 0, 0,
			    UARTDBG_CFG_OSHDL_TO_OS);
}

void psci_fiq_debugger_uart_irq_tf_init(u32 irq_id, void *callback)
{
	struct arm_smccc_res sip_smmc;

	psci_fiq_debugger_uart_irq_tf = callback;
	sip_smmc = __invoke_sip_fn_smc(PSCI_SIP_UARTDBG_CFG64, irq_id,
				       (u64)psci_fiq_debugger_uart_irq_tf_cb,
				       UARTDBG_CFG_INIT);
	ft_fiq_mem_phy = sip_smmc.a0;
	ft_fiq_mem_base = ioremap(ft_fiq_mem_phy, 8 * 1024);
	fig_init_flag = 1;
}

void psci_enable_fiq(void)
{
	int irq_flag;
	int cpu_id;

	if (fig_init_flag != 1)
		return;
	irq_flag = *((char *)(ft_fiq_mem_base) + 8 * 1024 - 0x04);

	cpu_id = read_cpuid_mpidr() & MPIDR_HWID_BITMASK;
	if ((irq_flag == 0xAA) && (cpu_id == 0))
		__invoke_sip_fn_smc(RK_SIP_ENABLE_FIQ, 0, 0, 0);
}

u32 psci_fiq_debugger_switch_cpu(u32 cpu)
{
	struct arm_smccc_res sip_smmc;

		sip_smmc = __invoke_sip_fn_smc(PSCI_SIP_UARTDBG_CFG64,
					       cpu_logical_map(cpu),
					       0, UARTDBG_CFG_OSHDL_CPUSW);
	return sip_smmc.a0;
}

void psci_fiq_debugger_enable_debug(bool val)
{
	if (val)
		__invoke_sip_fn_smc(PSCI_SIP_UARTDBG_CFG64, 0,
				    0, UARTDBG_CFG_OSHDL_DEBUG_ENABLE);
	else
		__invoke_sip_fn_smc(PSCI_SIP_UARTDBG_CFG64, 0,
				    0, UARTDBG_CFG_OSHDL_DEBUG_DISABLE);
}

int psci_fiq_debugger_set_print_port(u32 port, u32 baudrate)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(PSCI_SIP_UARTDBG_CFG64, port, baudrate,
				  UARTDBG_CFG_PRINT_PORT);
	return res.a0;
}

struct arm_smccc_res sip_smc_get_share_mem_page(u32 page_num,
						share_page_type_t page_type)
{
	struct arm_smccc_res res;
	unsigned long share_mem_phy;

	res = __invoke_sip_fn_smc(SIP_SHARE_MEM32, page_num, page_type, 0);
	if (res.a0)
		goto error;

	share_mem_phy = res.a1;
	res.a1 = (unsigned long)ioremap(share_mem_phy, SIZE_PAGE(page_num));

error:
	return res;
}

struct arm_smccc_res sip_smc_get_call_count(void)
{
	return __invoke_sip_fn_smc(SIP_SVC_CALL_COUNT, 0, 0, 0);
}
