// SPDX-License-Identifier: GPL-2.0
#include <asm/fpu.h>
#include <asm/loongson.h>
#include <asm/sections.h>
#include <asm/time.h>
#include <asm/tlbflush.h>
#include <linux/suspend.h>

static u32 saved_crmd;
static u32 saved_prmd;
static u32 saved_euen;
static u32 saved_ecfg;
static u64 saved_pcpu_base;
struct pt_regs saved_regs;

void save_processor_state(void)
{
	save_counter();
	saved_crmd = csr_read32(LOONGARCH_CSR_CRMD);
	saved_prmd = csr_read32(LOONGARCH_CSR_PRMD);
	saved_euen = csr_read32(LOONGARCH_CSR_EUEN);
	saved_ecfg = csr_read32(LOONGARCH_CSR_ECFG);
	saved_pcpu_base = csr_read64(PERCPU_BASE_KS);

	if (is_fpu_owner())
		save_fp(current);
}

void restore_processor_state(void)
{
	sync_counter();
	csr_write32(saved_crmd, LOONGARCH_CSR_CRMD);
	csr_write32(saved_prmd, LOONGARCH_CSR_PRMD);
	csr_write32(saved_euen, LOONGARCH_CSR_EUEN);
	csr_write32(saved_ecfg, LOONGARCH_CSR_ECFG);
	csr_write64(saved_pcpu_base, PERCPU_BASE_KS);

	if (is_fpu_owner())
		restore_fp(current);
}

int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = PFN_DOWN(__pa(&__nosave_begin));
	unsigned long nosave_end_pfn = PFN_UP(__pa(&__nosave_end));

	return	(pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn);
}

extern int swsusp_asm_suspend(void);

int swsusp_arch_suspend(void)
{
	enable_pci_wakeup();
	return swsusp_asm_suspend();
}

extern int swsusp_asm_resume(void);

int swsusp_arch_resume(void)
{
	/* Avoid TLB mismatch during and after kernel resume */
	local_flush_tlb_all();
	return swsusp_asm_resume();
}
