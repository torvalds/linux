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
#include <linux/module.h>
#include <linux/rockchip/rockchip_sip.h>
#include <asm/cputype.h>
#ifdef CONFIG_ARM
#include <asm/psci.h>
#endif
#include <asm/smp_plat.h>
#include <uapi/linux/psci.h>
#include <linux/ptrace.h>
#include <linux/sched/clock.h>
#include <linux/slab.h>
#include <soc/rockchip/rockchip_sip.h>

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
EXPORT_SYMBOL_GPL(sip_smc_dram);

struct arm_smccc_res sip_smc_get_atf_version(void)
{
	return __invoke_sip_fn_smc(SIP_ATF_VERSION, 0, 0, 0);
}
EXPORT_SYMBOL_GPL(sip_smc_get_atf_version);

struct arm_smccc_res sip_smc_get_sip_version(void)
{
	return __invoke_sip_fn_smc(SIP_SIP_VERSION, 0, 0, 0);
}
EXPORT_SYMBOL_GPL(sip_smc_get_sip_version);

int sip_smc_set_suspend_mode(u32 ctrl, u32 config1, u32 config2)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_SUSPEND_MODE, ctrl, config1, config2);
	return res.a0;
}
EXPORT_SYMBOL_GPL(sip_smc_set_suspend_mode);

struct arm_smccc_res sip_smc_get_suspend_info(u32 info)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_SUSPEND_MODE, info, 0, 0);
	return res;
}
EXPORT_SYMBOL_GPL(sip_smc_get_suspend_info);

int sip_smc_virtual_poweroff(void)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(PSCI_FN_NATIVE(1_0, SYSTEM_SUSPEND), 0, 0, 0);
	return res.a0;
}
EXPORT_SYMBOL_GPL(sip_smc_virtual_poweroff);

int sip_smc_remotectl_config(u32 func, u32 data)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_REMOTECTL_CFG, func, data, 0);

	return res.a0;
}
EXPORT_SYMBOL_GPL(sip_smc_remotectl_config);

u32 sip_smc_secure_reg_read(u32 addr_phy)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_ACCESS_REG, 0, addr_phy, SECURE_REG_RD);
	if (res.a0)
		pr_err("%s error: %d, addr phy: 0x%x\n",
		       __func__, (int)res.a0, addr_phy);

	return res.a1;
}
EXPORT_SYMBOL_GPL(sip_smc_secure_reg_read);

int sip_smc_secure_reg_write(u32 addr_phy, u32 val)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_ACCESS_REG, val, addr_phy, SECURE_REG_WR);
	if (res.a0)
		pr_err("%s error: %d, addr phy: 0x%x\n",
		       __func__, (int)res.a0, addr_phy);

	return res.a0;
}
EXPORT_SYMBOL_GPL(sip_smc_secure_reg_write);

static void *sip_map(phys_addr_t start, size_t size)
{
	struct page **pages;
	phys_addr_t page_start;
	unsigned int page_count;
	pgprot_t prot;
	unsigned int i;
	void *vaddr;

	if (!pfn_valid(__phys_to_pfn(start)))
		return ioremap(start, size);

	page_start = start - offset_in_page(start);
	page_count = DIV_ROUND_UP(size + offset_in_page(start), PAGE_SIZE);

	prot = pgprot_noncached(PAGE_KERNEL);

	pages = kmalloc_array(page_count, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		pr_err("%s: Failed to allocate array for %u pages\n",
		       __func__, page_count);
		return NULL;
	}

	for (i = 0; i < page_count; i++)
		pages[i] = phys_to_page(page_start + i * PAGE_SIZE);

	vaddr = vmap(pages, page_count, VM_MAP, prot);
	kfree(pages);

	/*
	 * Since vmap() uses page granularity, we must add the offset
	 * into the page here, to get the byte granularity address
	 * into the mapping to represent the actual "start" location.
	 */
	return vaddr + offset_in_page(start);
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
	res.a1 = (unsigned long)sip_map(share_mem_phy, SIZE_PAGE(page_num));

error:
	return res;
}
EXPORT_SYMBOL_GPL(sip_smc_request_share_mem);

struct arm_smccc_res sip_smc_mcu_el3fiq(u32 arg0, u32 arg1, u32 arg2)
{
	return __invoke_sip_fn_smc(SIP_MCU_EL3FIQ_CFG, arg0, arg1, arg2);
}
EXPORT_SYMBOL_GPL(sip_smc_mcu_el3fiq);

struct arm_smccc_res sip_smc_vpu_reset(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(PSCI_SIP_VPU_RESET, arg0, arg1, arg2);
	return res;
}
EXPORT_SYMBOL_GPL(sip_smc_vpu_reset);

struct arm_smccc_res sip_smc_bus_config(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_BUS_CFG, arg0, arg1, arg2);
	return res;
}
EXPORT_SYMBOL_GPL(sip_smc_bus_config);

struct dram_addrmap_info *sip_smc_get_dram_map(void)
{
	struct arm_smccc_res res;
	static struct dram_addrmap_info *map;

	if (map)
		return map;

	/* Request share memory size 4KB */
	res = sip_smc_request_share_mem(1, SHARE_PAGE_TYPE_DDR_ADDRMAP);
	if (res.a0 != 0) {
		pr_err("no ATF memory for init\n");
		return NULL;
	}

	map = (struct dram_addrmap_info *)res.a1;

	res = sip_smc_dram(SHARE_PAGE_TYPE_DDR_ADDRMAP, 0,
			   ROCKCHIP_SIP_CONFIG_DRAM_ADDRMAP_GET);
	if (res.a0) {
		pr_err("rockchip_sip_config_dram_init error:%lx\n", res.a0);
		map = NULL;
		return NULL;
	}

	return map;
}
EXPORT_SYMBOL_GPL(sip_smc_get_dram_map);

struct arm_smccc_res sip_smc_lastlog_request(void)
{
	struct arm_smccc_res res;
	void __iomem *addr1, *addr2;

	res = __invoke_sip_fn_smc(SIP_LAST_LOG, local_clock(), 0, 0);
	if (IS_SIP_ERROR(res.a0))
		return res;

	addr1 = sip_map(res.a1, res.a3);
	if (!addr1) {
		pr_err("%s: share memory buffer0 ioremap failed\n", __func__);
		res.a0 = SIP_RET_INVALID_ADDRESS;
		return res;
	}
	addr2 = sip_map(res.a2, res.a3);
	if (!addr2) {
		pr_err("%s: share memory buffer1 ioremap failed\n", __func__);
		res.a0 = SIP_RET_INVALID_ADDRESS;
		return res;
	}

	res.a1 = (unsigned long)addr1;
	res.a2 = (unsigned long)addr2;

	return res;
}
EXPORT_SYMBOL_GPL(sip_smc_lastlog_request);

int sip_smc_amp_config(u32 sub_func_id, u32 arg1, u32 arg2, u32 arg3)
{
	struct arm_smccc_res res;

	arm_smccc_smc(RK_SIP_AMP_CFG, sub_func_id, arg1, arg2, arg3,
		      0, 0, 0, &res);
	return res.a0;
}
EXPORT_SYMBOL_GPL(sip_smc_amp_config);

struct arm_smccc_res sip_smc_get_amp_info(u32 sub_func_id, u32 arg1)
{
	struct arm_smccc_res res;

	arm_smccc_smc(RK_SIP_AMP_CFG, sub_func_id, arg1, 0, 0, 0, 0, 0, &res);
	return res;
}
EXPORT_SYMBOL_GPL(sip_smc_get_amp_info);

void __iomem *sip_hdcp_request_share_memory(int id)
{
	static void __iomem *base;
	struct arm_smccc_res res;

	if (id < 0 || id >= MAX_DEVICE) {
		pr_err("%s: invalid device id\n", __func__);
		return NULL;
	}

	if (!base) {
		/* request page share memory */
		res = sip_smc_request_share_mem(2, SHARE_PAGE_TYPE_HDCP);
		if (IS_SIP_ERROR(res.a0))
			return NULL;
		base = (void __iomem *)res.a1;
	}

	return base + id * 1024;
}

struct arm_smccc_res sip_hdcp_config(u32 arg0, u32 arg1, u32 arg2)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_HDCP_CONFIG, arg0, arg1, arg2);
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
static void (*sip_fiq_debugger_uart_irq_tf)(struct pt_regs *_pt_regs,
					    unsigned long cpu);
static struct pt_regs fiq_pt_regs;

int sip_fiq_debugger_is_enabled(void)
{
	return fiq_sip_enabled;
}
EXPORT_SYMBOL_GPL(sip_fiq_debugger_is_enabled);

static void sip_fiq_debugger_get_pt_regs(void *reg_base,
					 unsigned long sp_el1)
{
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
}

static void sip_fiq_debugger_uart_irq_tf_cb(unsigned long sp_el1,
					    unsigned long offset,
					    unsigned long cpu)
{
	char *cpu_context;

	/* calling fiq handler */
	if (ft_fiq_mem_base) {
		cpu_context = (char *)ft_fiq_mem_base + offset;
		sip_fiq_debugger_get_pt_regs(cpu_context, sp_el1);
		sip_fiq_debugger_uart_irq_tf(&fiq_pt_regs, cpu);
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
		ft_fiq_mem_base = sip_map(ft_fiq_mem_phy,
					  FIQ_UARTDBG_SHARE_MEM_SIZE);
		if (!ft_fiq_mem_base) {
			pr_err("%s: share memory ioremap failed\n", __func__);
			return -ENOMEM;
		}
	}

	fiq_sip_enabled = 1;

	return SIP_RET_SUCCESS;
}
EXPORT_SYMBOL_GPL(sip_fiq_debugger_uart_irq_tf_init);

static ulong cpu_logical_map_mpidr(u32 cpu)
{
#ifdef MODULE
	/* Empirically, local "cpu_logical_map()" for rockchip platforms */
	ulong mpidr = 0x00;

	if (cpu < 4)
		/* 0x00, 0x01, 0x02, 0x03 */
		mpidr = cpu;
	else if (cpu < 8)
		/* 0x100, 0x101, 0x102, 0x103 */
		mpidr = 0x100 | (cpu - 4);
	else
		pr_err("Unsupported map cpu: %d\n", cpu);

	return mpidr;
#else
	return cpu_logical_map(cpu);
#endif
}

int sip_fiq_debugger_switch_cpu(u32 cpu)
{
	struct arm_smccc_res res;

	fiq_target_cpu = cpu;
	res = __invoke_sip_fn_smc(SIP_UARTDBG_FN, cpu_logical_map_mpidr(cpu),
				  0, UARTDBG_CFG_OSHDL_CPUSW);
	return res.a0;
}

int sip_fiq_debugger_sdei_switch_cpu(u32 cur_cpu, u32 target_cpu, u32 flag)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_SDEI_FIQ_DBG_SWITCH_CPU,
				  cur_cpu, target_cpu, flag);
	return res.a0;
}

int sip_fiq_debugger_sdei_get_event_id(u32 *fiq, u32 *sw_cpu, u32 *flag)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_SDEI_FIQ_DBG_GET_EVENT_ID,
				  0, 0, 0);
	*fiq = res.a1;
	*sw_cpu = res.a2;
	if (flag)
		*flag = res.a3;

	return res.a0;
}

EXPORT_SYMBOL_GPL(sip_fiq_debugger_switch_cpu);

void sip_fiq_debugger_enable_debug(bool enable)
{
	unsigned long val;

	val = enable ? UARTDBG_CFG_OSHDL_DEBUG_ENABLE :
		       UARTDBG_CFG_OSHDL_DEBUG_DISABLE;

	__invoke_sip_fn_smc(SIP_UARTDBG_FN, 0, 0, val);
}
EXPORT_SYMBOL_GPL(sip_fiq_debugger_enable_debug);

int sip_fiq_debugger_set_print_port(u32 port_phyaddr, u32 baudrate)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(SIP_UARTDBG_FN, port_phyaddr, baudrate,
				  UARTDBG_CFG_PRINT_PORT);
	return res.a0;
}
EXPORT_SYMBOL_GPL(sip_fiq_debugger_set_print_port);

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
EXPORT_SYMBOL_GPL(sip_fiq_debugger_request_share_memory);

int sip_fiq_debugger_get_target_cpu(void)
{
	return fiq_target_cpu;
}
EXPORT_SYMBOL_GPL(sip_fiq_debugger_get_target_cpu);

void sip_fiq_debugger_enable_fiq(bool enable, uint32_t tgt_cpu)
{
	u32 en;

	fiq_target_cpu = tgt_cpu;
	en = enable ? UARTDBG_CFG_FIQ_ENABEL : UARTDBG_CFG_FIQ_DISABEL;
	__invoke_sip_fn_smc(SIP_UARTDBG_FN, tgt_cpu, 0, en);
}
EXPORT_SYMBOL_GPL(sip_fiq_debugger_enable_fiq);

int sip_fiq_control(u32 sub_func, u32 irq, unsigned long data)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(RK_SIP_FIQ_CTRL,
				  sub_func, irq, data);
	return res.a0;
}
EXPORT_SYMBOL_GPL(sip_fiq_control);

int sip_wdt_config(u32 sub_func, u32 arg1, u32 arg2, u32 arg3)
{
	struct arm_smccc_res res;

	arm_smccc_smc(SIP_WDT_CFG, sub_func, arg1, arg2, arg3,
		      0, 0, 0, &res);

	return res.a0;
}
EXPORT_SYMBOL_GPL(sip_wdt_config);

int sip_hdcpkey_init(u32 hdcp_id)
{
	struct arm_smccc_res res;

	res = __invoke_sip_fn_smc(TRUSTED_OS_HDCPKEY_INIT, hdcp_id, 0, 0);

	return res.a0;
}
EXPORT_SYMBOL_GPL(sip_hdcpkey_init);

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

MODULE_DESCRIPTION("Rockchip SIP Call");
MODULE_LICENSE("GPL");
