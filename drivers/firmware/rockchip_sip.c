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
#ifdef CONFIG_ARM
#include <asm/psci.h>
#endif
#include <asm/smp_plat.h>
#include <uapi/linux/psci.h>
#include <linux/ptrace.h>

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

struct arm_smccc_res sip_smc_dram(u32 arg0, u32 arg1, u32 arg2)
{
	return __invoke_sip_fn_smc(SIP_DRAM_CONFIG, arg0, arg1, arg2);
}

struct arm_smccc_res sip_smc_get_atf_version(void)
{
	return __invoke_sip_fn_smc(SIP_ATF_VERSION, 0, 0, 0);
}

struct arm_smccc_res sip_smc_get_sip_version(void)
{
	return __invoke_sip_fn_smc(SIP_SIP_VERSION, 0, 0, 0);
}

int sip_smc_set_suspend_mode(u32 ctrl, u32 config1, u32 config2)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_SUSPEND_MODE, ctrl, config1, config2);
	return res.a0;
}

struct arm_smccc_res sip_smc_get_suspend_info(u32 info)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_SUSPEND_MODE, info, 0, 0);
	return res;
}

int sip_smc_virtual_poweroff(void)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(PSCI_FN_NATIVE(1_0, SYSTEM_SUSPEND), 0, 0, 0);
	return res.a0;
}

int sip_smc_remotectl_config(u32 func, u32 data)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_REMOTECTL_CFG, func, data, 0);

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

struct arm_smccc_res sip_smc_request_share_mem(u32 page_num,
					       share_page_type_t page_type)
{
	struct arm_smccc_res res;
	unsigned long share_mem_phy;

	res = __invoke_sip_fn_smc(SIP_SHARE_MEM, page_num, page_type, 0);
	if (IS_SIP_ERROR(res.a0))
		goto error;

	share_mem_phy = res.a1;
	res.a1 = (unsigned long)ioremap(share_mem_phy, SIZE_PAGE(page_num));

error:
	return res;
}

struct arm_smccc_res sip_smc_mcu_el3fiq(u32 arg0, u32 arg1, u32 arg2)
{
	return __invoke_sip_fn_smc(SIP_MCU_EL3FIQ_CFG, arg0, arg1, arg2);
}

struct arm_smccc_res sip_smc_vpu_reset(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(PSCI_SIP_VPU_RESET, arg0, arg1, arg2);
	return res;
}
EXPORT_SYMBOL_GPL(sip_smc_vpu_reset);

struct arm_smccc_res sip_smc_soc_bus_div(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(RK_SIP_SOC_BUS_DIV, arg0, arg1, arg2);
	return res;
}

struct arm_smccc_res sip_smc_lastlog_request(void)
{
	struct arm_smccc_res res;
	void __iomem *addr1, *addr2;

	res = __invoke_sip_fn_smc(SIP_LAST_LOG, local_clock(), 0, 0);
	if (IS_SIP_ERROR(res.a0))
		return res;

	addr1 = ioremap(res.a1, res.a3);
	if (!addr1) {
		pr_err("%s: share memory buffer0 ioremap failed\n", __func__);
		res.a0 = SIP_RET_INVALID_ADDRESS;
		return res;
	}
	addr2 = ioremap(res.a2, res.a3);
	if (!addr2) {
		pr_err("%s: share memory buffer1 ioremap failed\n", __func__);
		res.a0 = SIP_RET_INVALID_ADDRESS;
		return res;
	}

	res.a1 = (unsigned long)addr1;
	res.a2 = (unsigned long)addr2;

	return res;
}

/************************** fiq debugger **************************************/
/*
 * AArch32 is not allowed to call SMC64(ATF framework does not support), so we
 * don't change SIP_UARTDBG_FN to SIP_UARTDBG_CFG64 even when cpu is AArch32
 * mode. Let ATF support SIP_UARTDBG_CFG, and we just initialize SIP_UARTDBG_FN
 * depends on compile option(CONFIG_ARM or CONFIG_ARM64).
 */
#ifdef CONFIG_ARM64
#define SIP_UARTDBG_FN		SIP_UARTDBG_CFG64
#else
#define SIP_UARTDBG_FN		SIP_UARTDBG_CFG
static int firmware_64_32bit;
#endif

static int fiq_sip_enabled;
static int fiq_target_cpu;
static phys_addr_t ft_fiq_mem_phy;
static void __iomem *ft_fiq_mem_base;
static void (*sip_fiq_debugger_uart_irq_tf)(struct pt_regs _pt_regs,
					    unsigned long cpu);
int sip_fiq_debugger_is_enabled(void)
{
	return fiq_sip_enabled;
}

static struct pt_regs sip_fiq_debugger_get_pt_regs(void *reg_base,
						   unsigned long sp_el1)
{
	struct pt_regs fiq_pt_regs;
	__maybe_unused struct sm_nsec_ctx *nsec_ctx = reg_base;
	__maybe_unused struct gp_regs_ctx *gp_regs = reg_base;

#ifdef CONFIG_ARM64
	/*
	 * 64-bit ATF + 64-bit kernel
	 */
	/* copy cpu context: x0 ~ spsr_el3 */
	memcpy(&fiq_pt_regs, reg_base, 8 * 31);

	/* copy pstate: spsr_el3 */
	memcpy(&fiq_pt_regs.pstate, reg_base + 0x110, 8);
	fiq_pt_regs.sp = sp_el1;

	/* copy pc: elr_el3 */
	memcpy(&fiq_pt_regs.pc, reg_base + 0x118, 8);
#else
	if (firmware_64_32bit == FIRMWARE_ATF_64BIT) {
		/*
		 * 64-bit ATF + 32-bit kernel
		 */
		fiq_pt_regs.ARM_r0 = gp_regs->x0;
		fiq_pt_regs.ARM_r1 = gp_regs->x1;
		fiq_pt_regs.ARM_r2 = gp_regs->x2;
		fiq_pt_regs.ARM_r3 = gp_regs->x3;
		fiq_pt_regs.ARM_r4 = gp_regs->x4;
		fiq_pt_regs.ARM_r5 = gp_regs->x5;
		fiq_pt_regs.ARM_r6 = gp_regs->x6;
		fiq_pt_regs.ARM_r7 = gp_regs->x7;
		fiq_pt_regs.ARM_r8 = gp_regs->x8;
		fiq_pt_regs.ARM_r9 = gp_regs->x9;
		fiq_pt_regs.ARM_r10 = gp_regs->x10;
		fiq_pt_regs.ARM_fp = gp_regs->x11;
		fiq_pt_regs.ARM_ip = gp_regs->x12;
		fiq_pt_regs.ARM_sp = gp_regs->x19;	/* aarch32 svc_r13 */
		fiq_pt_regs.ARM_lr = gp_regs->x18;	/* aarch32 svc_r14 */
		fiq_pt_regs.ARM_cpsr = gp_regs->spsr_el3;
		fiq_pt_regs.ARM_pc = gp_regs->elr_el3;
	} else {
		/*
		 * 32-bit tee firmware + 32-bit kernel
		 */
		fiq_pt_regs.ARM_r0 = nsec_ctx->r0;
		fiq_pt_regs.ARM_r1 = nsec_ctx->r1;
		fiq_pt_regs.ARM_r2 = nsec_ctx->r2;
		fiq_pt_regs.ARM_r3 = nsec_ctx->r3;
		fiq_pt_regs.ARM_r4 = nsec_ctx->r4;
		fiq_pt_regs.ARM_r5 = nsec_ctx->r5;
		fiq_pt_regs.ARM_r6 = nsec_ctx->r6;
		fiq_pt_regs.ARM_r7 = nsec_ctx->r7;
		fiq_pt_regs.ARM_r8 = nsec_ctx->r8;
		fiq_pt_regs.ARM_r9 = nsec_ctx->r9;
		fiq_pt_regs.ARM_r10 = nsec_ctx->r10;
		fiq_pt_regs.ARM_fp = nsec_ctx->r11;
		fiq_pt_regs.ARM_ip = nsec_ctx->r12;
		fiq_pt_regs.ARM_sp = nsec_ctx->svc_sp;
		fiq_pt_regs.ARM_lr = nsec_ctx->svc_lr;
		fiq_pt_regs.ARM_cpsr = nsec_ctx->mon_spsr;

		/*
		 * 'nsec_ctx->mon_lr' is not the fiq break point's PC, because it will
		 * be override as 'psci_fiq_debugger_uart_irq_tf_cb' for optee-os to
		 * jump to fiq_debugger handler.
		 *
		 * As 'nsec_ctx->und_lr' is not used for kernel, so optee-os uses it to
		 * deliver fiq break point's PC.
		 *
		 */
		fiq_pt_regs.ARM_pc = nsec_ctx->und_lr;
	}
#endif

	return fiq_pt_regs;
}

static void sip_fiq_debugger_uart_irq_tf_cb(unsigned long sp_el1,
					    unsigned long offset,
					    unsigned long cpu)
{
	struct pt_regs fiq_pt_regs;
	char *cpu_context;

	/* calling fiq handler */
	if (ft_fiq_mem_base) {
		cpu_context = (char *)ft_fiq_mem_base + offset;
		fiq_pt_regs = sip_fiq_debugger_get_pt_regs(cpu_context, sp_el1);
		sip_fiq_debugger_uart_irq_tf(fiq_pt_regs, cpu);
	}

	/* fiq handler done, return to EL3(then EL3 return to EL1 entry) */
	__invoke_sip_fn_smc(SIP_UARTDBG_FN, 0, 0, UARTDBG_CFG_OSHDL_TO_OS);
}

int sip_fiq_debugger_uart_irq_tf_init(u32 irq_id, void *callback_fn)
{
	struct arm_smccc_res res;

	fiq_target_cpu = 0;

	/* init fiq debugger callback */
	sip_fiq_debugger_uart_irq_tf = callback_fn;
	res = __invoke_sip_fn_smc(SIP_UARTDBG_FN, irq_id,
				  (unsigned long)sip_fiq_debugger_uart_irq_tf_cb,
				  UARTDBG_CFG_INIT);
	if (IS_SIP_ERROR(res.a0)) {
		pr_err("%s error: %d\n", __func__, (int)res.a0);
		return res.a0;
	}

	/* share memory ioremap */
	if (!ft_fiq_mem_base) {
		ft_fiq_mem_phy = res.a1;
		ft_fiq_mem_base = ioremap(ft_fiq_mem_phy,
					  FIQ_UARTDBG_SHARE_MEM_SIZE);
		if (!ft_fiq_mem_base) {
			pr_err("%s: share memory ioremap failed\n", __func__);
			return -ENOMEM;
		}
	}

	fiq_sip_enabled = 1;

	return SIP_RET_SUCCESS;
}

int sip_fiq_debugger_switch_cpu(u32 cpu)
{
	struct arm_smccc_res res;

	fiq_target_cpu = cpu;
	res = __invoke_sip_fn_smc(SIP_UARTDBG_FN, cpu_logical_map(cpu),
				  0, UARTDBG_CFG_OSHDL_CPUSW);
	return res.a0;
}

void sip_fiq_debugger_enable_debug(bool enable)
{
	unsigned long val;

	val = enable ? UARTDBG_CFG_OSHDL_DEBUG_ENABLE :
		       UARTDBG_CFG_OSHDL_DEBUG_DISABLE;

	__invoke_sip_fn_smc(SIP_UARTDBG_FN, 0, 0, val);
}

int sip_fiq_debugger_set_print_port(u32 port_phyaddr, u32 baudrate)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_UARTDBG_FN, port_phyaddr, baudrate,
				  UARTDBG_CFG_PRINT_PORT);
	return res.a0;
}

int sip_fiq_debugger_request_share_memory(void)
{
	struct arm_smccc_res res;

	/* request page share memory */
	res = sip_smc_request_share_mem(FIQ_UARTDBG_PAGE_NUMS,
					SHARE_PAGE_TYPE_UARTDBG);
	if (IS_SIP_ERROR(res.a0))
		return res.a0;

	return SIP_RET_SUCCESS;
}

int sip_fiq_debugger_get_target_cpu(void)
{
	return fiq_target_cpu;
}

void sip_fiq_debugger_enable_fiq(bool enable, uint32_t tgt_cpu)
{
	u32 en;

	fiq_target_cpu = tgt_cpu;
	en = enable ? UARTDBG_CFG_FIQ_ENABEL : UARTDBG_CFG_FIQ_DISABEL;
	__invoke_sip_fn_smc(SIP_UARTDBG_FN, tgt_cpu, 0, en);
}

/******************************************************************************/
#ifdef CONFIG_ARM
static __init int sip_firmware_init(void)
{
	struct arm_smccc_res res;

	if (!psci_smp_available())
		return 0;

	/*
	 * OP-TEE works on kernel 3.10 and 4.4 and we have different sip
	 * implement. We should tell OP-TEE the current rockchip sip version.
	 */
	res = __invoke_sip_fn_smc(SIP_SIP_VERSION, SIP_IMPLEMENT_V2,
				  SECURE_REG_WR, 0);
	if (IS_SIP_ERROR(res.a0))
		pr_err("%s: set rockchip sip version v2 failed\n", __func__);

	/*
	 * Currently, we support:
	 *
	 *	1. 64-bit ATF + 64-bit kernel;
	 *	2. 64-bit ATF + 32-bit kernel;
	 *	3. 32-bit TEE + 32-bit kernel;
	 *
	 * We need to detect which case of above and record in firmware_64_32bit
	 * We get info from cpuid and compare with all supported ARMv7 cpu.
	 */
	switch (read_cpuid_part()) {
	case ARM_CPU_PART_CORTEX_A7:
	case ARM_CPU_PART_CORTEX_A8:
	case ARM_CPU_PART_CORTEX_A9:
	case ARM_CPU_PART_CORTEX_A12:
	case ARM_CPU_PART_CORTEX_A15:
	case ARM_CPU_PART_CORTEX_A17:
		firmware_64_32bit = FIRMWARE_TEE_32BIT;
		break;
	default:
		firmware_64_32bit = FIRMWARE_ATF_64BIT;
		break;
	}

	return 0;
}
arch_initcall(sip_firmware_init);
#endif
