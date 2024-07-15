// SPDX-License-Identifier: GPL-2.0
/*
 * loongson-specific suspend support
 *
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/acpi.h>
#include <linux/pm.h>
#include <linux/suspend.h>

#include <asm/loongarch.h>
#include <asm/loongson.h>
#include <asm/setup.h>
#include <asm/time.h>
#include <asm/tlbflush.h>

u64 loongarch_suspend_addr;

struct saved_registers {
	u32 ecfg;
	u32 euen;
	u64 pgd;
	u64 kpgd;
	u32 pwctl0;
	u32 pwctl1;
	u64 pcpu_base;
};
static struct saved_registers saved_regs;

void loongarch_common_suspend(void)
{
	save_counter();
	saved_regs.pgd = csr_read64(LOONGARCH_CSR_PGDL);
	saved_regs.kpgd = csr_read64(LOONGARCH_CSR_PGDH);
	saved_regs.pwctl0 = csr_read32(LOONGARCH_CSR_PWCTL0);
	saved_regs.pwctl1 = csr_read32(LOONGARCH_CSR_PWCTL1);
	saved_regs.ecfg = csr_read32(LOONGARCH_CSR_ECFG);
	saved_regs.euen = csr_read32(LOONGARCH_CSR_EUEN);
	saved_regs.pcpu_base = csr_read64(PERCPU_BASE_KS);

	loongarch_suspend_addr = loongson_sysconf.suspend_addr;
}

void loongarch_common_resume(void)
{
	sync_counter();
	local_flush_tlb_all();
	csr_write64(eentry, LOONGARCH_CSR_EENTRY);
	csr_write64(eentry, LOONGARCH_CSR_MERRENTRY);
	csr_write64(tlbrentry, LOONGARCH_CSR_TLBRENTRY);

	csr_write64(saved_regs.pgd, LOONGARCH_CSR_PGDL);
	csr_write64(saved_regs.kpgd, LOONGARCH_CSR_PGDH);
	csr_write32(saved_regs.pwctl0, LOONGARCH_CSR_PWCTL0);
	csr_write32(saved_regs.pwctl1, LOONGARCH_CSR_PWCTL1);
	csr_write32(saved_regs.ecfg, LOONGARCH_CSR_ECFG);
	csr_write32(saved_regs.euen, LOONGARCH_CSR_EUEN);
	csr_write64(saved_regs.pcpu_base, PERCPU_BASE_KS);
}

int loongarch_acpi_suspend(void)
{
	enable_gpe_wakeup();
	enable_pci_wakeup();

	loongarch_common_suspend();

	/* processor specific suspend */
	loongarch_suspend_enter();

	loongarch_common_resume();

	return 0;
}
