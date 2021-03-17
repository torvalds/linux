// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/nds32.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/l2_cache.h>
#include <nds32_intrinsic.h>

#include <asm/cache_info.h>
extern struct cache_info L1_cache_info[2];

int va_kernel_present(unsigned long addr)
{
	pmd_t *pmd;
	pte_t *ptep, pte;

	pmd = pmd_off_k(addr);
	if (!pmd_none(*pmd)) {
		ptep = pte_offset_map(pmd, addr);
		pte = *ptep;
		if (pte_present(pte))
			return pte;
	}
	return 0;
}

pte_t va_present(struct mm_struct * mm, unsigned long addr)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;

	pgd = pgd_offset(mm, addr);
	if (!pgd_none(*pgd)) {
		p4d = p4d_offset(pgd, addr);
		if (!p4d_none(*p4d)) {
			pud = pud_offset(p4d, addr);
			if (!pud_none(*pud)) {
				pmd = pmd_offset(pud, addr);
				if (!pmd_none(*pmd)) {
					ptep = pte_offset_map(pmd, addr);
					pte = *ptep;
					if (pte_present(pte))
						return pte;
				}
			}
		}
	}
	return 0;

}

int va_readable(struct pt_regs *regs, unsigned long addr)
{
	struct mm_struct *mm = current->mm;
	pte_t pte;
	int ret = 0;

	if (user_mode(regs)) {
		/* user mode */
		pte = va_present(mm, addr);
		if (!pte && pte_read(pte))
			ret = 1;
	} else {
		/* superuser mode is always readable, so we can only
		 * check it is present or not*/
		return (! !va_kernel_present(addr));
	}
	return ret;
}

int va_writable(struct pt_regs *regs, unsigned long addr)
{
	struct mm_struct *mm = current->mm;
	pte_t pte;
	int ret = 0;

	if (user_mode(regs)) {
		/* user mode */
		pte = va_present(mm, addr);
		if (!pte && pte_write(pte))
			ret = 1;
	} else {
		/* superuser mode */
		pte = va_kernel_present(addr);
		if (!pte && pte_kernel_write(pte))
			ret = 1;
	}
	return ret;
}

/*
 * All
 */
void cpu_icache_inval_all(void)
{
	unsigned long end, line_size;

	line_size = L1_cache_info[ICACHE].line_size;
	end =
	    line_size * L1_cache_info[ICACHE].ways * L1_cache_info[ICACHE].sets;

	do {
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1I_IX_INVAL"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1I_IX_INVAL"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1I_IX_INVAL"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1I_IX_INVAL"::"r" (end));
	} while (end > 0);
	__nds32__isb();
}

void cpu_dcache_inval_all(void)
{
	__nds32__cctl_l1d_invalall();
}

#ifdef CONFIG_CACHE_L2
void dcache_wb_all_level(void)
{
	unsigned long flags, cmd;
	local_irq_save(flags);
	__nds32__cctl_l1d_wball_alvl();
	/* Section 1: Ensure the section 2 & 3 program code execution after */
	__nds32__cctlidx_read(NDS32_CCTL_L1D_IX_RWD,0);

	/* Section 2: Confirm the writeback all level is done in CPU and L2C */
	cmd = CCTL_CMD_L2_SYNC;
	L2_CMD_RDY();
	L2C_W_REG(L2_CCTL_CMD_OFF, cmd);
	L2_CMD_RDY();

	/* Section 3: Writeback whole L2 cache */
	cmd = CCTL_ALL_CMD | CCTL_CMD_L2_IX_WB;
	L2_CMD_RDY();
	L2C_W_REG(L2_CCTL_CMD_OFF, cmd);
	L2_CMD_RDY();
	__nds32__msync_all();
	local_irq_restore(flags);
}
EXPORT_SYMBOL(dcache_wb_all_level);
#endif

void cpu_dcache_wb_all(void)
{
	__nds32__cctl_l1d_wball_one_lvl();
	__nds32__cctlidx_read(NDS32_CCTL_L1D_IX_RWD,0);
}

void cpu_dcache_wbinval_all(void)
{
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	unsigned long flags;
	local_irq_save(flags);
#endif
	cpu_dcache_wb_all();
	cpu_dcache_inval_all();
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	local_irq_restore(flags);
#endif
}

/*
 * Page
 */
void cpu_icache_inval_page(unsigned long start)
{
	unsigned long line_size, end;

	line_size = L1_cache_info[ICACHE].line_size;
	end = start + PAGE_SIZE;

	do {
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1I_VA_INVAL"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1I_VA_INVAL"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1I_VA_INVAL"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1I_VA_INVAL"::"r" (end));
	} while (end != start);
	__nds32__isb();
}

void cpu_dcache_inval_page(unsigned long start)
{
	unsigned long line_size, end;

	line_size = L1_cache_info[DCACHE].line_size;
	end = start + PAGE_SIZE;

	do {
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1D_VA_INVAL"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1D_VA_INVAL"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1D_VA_INVAL"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1D_VA_INVAL"::"r" (end));
	} while (end != start);
}

void cpu_dcache_wb_page(unsigned long start)
{
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	unsigned long line_size, end;

	line_size = L1_cache_info[DCACHE].line_size;
	end = start + PAGE_SIZE;

	do {
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (end));
		end -= line_size;
		__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (end));
	} while (end != start);
	__nds32__cctlidx_read(NDS32_CCTL_L1D_IX_RWD,0);
#endif
}

void cpu_dcache_wbinval_page(unsigned long start)
{
	unsigned long line_size, end;

	line_size = L1_cache_info[DCACHE].line_size;
	end = start + PAGE_SIZE;

	do {
		end -= line_size;
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
		__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (end));
#endif
		__asm__ volatile ("\n\tcctl %0, L1D_VA_INVAL"::"r" (end));
		end -= line_size;
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
		__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (end));
#endif
		__asm__ volatile ("\n\tcctl %0, L1D_VA_INVAL"::"r" (end));
		end -= line_size;
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
		__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (end));
#endif
		__asm__ volatile ("\n\tcctl %0, L1D_VA_INVAL"::"r" (end));
		end -= line_size;
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
		__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (end));
#endif
		__asm__ volatile ("\n\tcctl %0, L1D_VA_INVAL"::"r" (end));
	} while (end != start);
	__nds32__cctlidx_read(NDS32_CCTL_L1D_IX_RWD,0);
}

void cpu_cache_wbinval_page(unsigned long page, int flushi)
{
	cpu_dcache_wbinval_page(page);
	if (flushi)
		cpu_icache_inval_page(page);
}

/*
 * Range
 */
void cpu_icache_inval_range(unsigned long start, unsigned long end)
{
	unsigned long line_size;

	line_size = L1_cache_info[ICACHE].line_size;

	while (end > start) {
		__asm__ volatile ("\n\tcctl %0, L1I_VA_INVAL"::"r" (start));
		start += line_size;
	}
	__nds32__isb();
}

void cpu_dcache_inval_range(unsigned long start, unsigned long end)
{
	unsigned long line_size;

	line_size = L1_cache_info[DCACHE].line_size;

	while (end > start) {
		__asm__ volatile ("\n\tcctl %0, L1D_VA_INVAL"::"r" (start));
		start += line_size;
	}
}

void cpu_dcache_wb_range(unsigned long start, unsigned long end)
{
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
	unsigned long line_size;

	line_size = L1_cache_info[DCACHE].line_size;

	while (end > start) {
		__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (start));
		start += line_size;
	}
	__nds32__cctlidx_read(NDS32_CCTL_L1D_IX_RWD,0);
#endif
}

void cpu_dcache_wbinval_range(unsigned long start, unsigned long end)
{
	unsigned long line_size;

	line_size = L1_cache_info[DCACHE].line_size;

	while (end > start) {
#ifndef CONFIG_CPU_DCACHE_WRITETHROUGH
		__asm__ volatile ("\n\tcctl %0, L1D_VA_WB"::"r" (start));
#endif
		__asm__ volatile ("\n\tcctl %0, L1D_VA_INVAL"::"r" (start));
		start += line_size;
	}
	__nds32__cctlidx_read(NDS32_CCTL_L1D_IX_RWD,0);
}

void cpu_cache_wbinval_range(unsigned long start, unsigned long end, int flushi)
{
	unsigned long line_size, align_start, align_end;

	line_size = L1_cache_info[DCACHE].line_size;
	align_start = start & ~(line_size - 1);
	align_end = (end + line_size - 1) & ~(line_size - 1);
	cpu_dcache_wbinval_range(align_start, align_end);

	if (flushi) {
		line_size = L1_cache_info[ICACHE].line_size;
		align_start = start & ~(line_size - 1);
		align_end = (end + line_size - 1) & ~(line_size - 1);
		cpu_icache_inval_range(align_start, align_end);
	}
}

void cpu_cache_wbinval_range_check(struct vm_area_struct *vma,
				   unsigned long start, unsigned long end,
				   bool flushi, bool wbd)
{
	unsigned long line_size, t_start, t_end;

	if (!flushi && !wbd)
		return;
	line_size = L1_cache_info[DCACHE].line_size;
	start = start & ~(line_size - 1);
	end = (end + line_size - 1) & ~(line_size - 1);

	if ((end - start) > (8 * PAGE_SIZE)) {
		if (wbd)
			cpu_dcache_wbinval_all();
		if (flushi)
			cpu_icache_inval_all();
		return;
	}

	t_start = (start + PAGE_SIZE) & PAGE_MASK;
	t_end = ((end - 1) & PAGE_MASK);

	if ((start & PAGE_MASK) == t_end) {
		if (va_present(vma->vm_mm, start)) {
			if (wbd)
				cpu_dcache_wbinval_range(start, end);
			if (flushi)
				cpu_icache_inval_range(start, end);
		}
		return;
	}

	if (va_present(vma->vm_mm, start)) {
		if (wbd)
			cpu_dcache_wbinval_range(start, t_start);
		if (flushi)
			cpu_icache_inval_range(start, t_start);
	}

	if (va_present(vma->vm_mm, end - 1)) {
		if (wbd)
			cpu_dcache_wbinval_range(t_end, end);
		if (flushi)
			cpu_icache_inval_range(t_end, end);
	}

	while (t_start < t_end) {
		if (va_present(vma->vm_mm, t_start)) {
			if (wbd)
				cpu_dcache_wbinval_page(t_start);
			if (flushi)
				cpu_icache_inval_page(t_start);
		}
		t_start += PAGE_SIZE;
	}
}

#ifdef CONFIG_CACHE_L2
static inline void cpu_l2cache_op(unsigned long start, unsigned long end, unsigned long op)
{
	if (atl2c_base) {
		unsigned long p_start = __pa(start);
		unsigned long p_end = __pa(end);
		unsigned long cmd;
		unsigned long line_size;
		/* TODO Can Use PAGE Mode to optimize if range large than PAGE_SIZE */
		line_size = L2_CACHE_LINE_SIZE();
		p_start = p_start & (~(line_size - 1));
		p_end = (p_end + line_size - 1) & (~(line_size - 1));
		cmd =
		    (p_start & ~(line_size - 1)) | op |
		    CCTL_SINGLE_CMD;
		do {
			L2_CMD_RDY();
			L2C_W_REG(L2_CCTL_CMD_OFF, cmd);
			cmd += line_size;
			p_start += line_size;
		} while (p_end > p_start);
		cmd = CCTL_CMD_L2_SYNC;
		L2_CMD_RDY();
		L2C_W_REG(L2_CCTL_CMD_OFF, cmd);
		L2_CMD_RDY();
	}
}
#else
#define cpu_l2cache_op(start,end,op) do { } while (0)
#endif
/*
 * DMA
 */
void cpu_dma_wb_range(unsigned long start, unsigned long end)
{
	unsigned long line_size;
	unsigned long flags;
	line_size = L1_cache_info[DCACHE].line_size;
	start = start & (~(line_size - 1));
	end = (end + line_size - 1) & (~(line_size - 1));
	if (unlikely(start == end))
		return;

	local_irq_save(flags);
	cpu_dcache_wb_range(start, end);
	cpu_l2cache_op(start, end, CCTL_CMD_L2_PA_WB);
	__nds32__msync_all();
	local_irq_restore(flags);
}

void cpu_dma_inval_range(unsigned long start, unsigned long end)
{
	unsigned long line_size;
	unsigned long old_start = start;
	unsigned long old_end = end;
	unsigned long flags;
	line_size = L1_cache_info[DCACHE].line_size;
	start = start & (~(line_size - 1));
	end = (end + line_size - 1) & (~(line_size - 1));
	if (unlikely(start == end))
		return;
	local_irq_save(flags);
	if (start != old_start) {
		cpu_dcache_wbinval_range(start, start + line_size);
		cpu_l2cache_op(start, start + line_size, CCTL_CMD_L2_PA_WBINVAL);
	}
	if (end != old_end) {
		cpu_dcache_wbinval_range(end - line_size, end);
		cpu_l2cache_op(end - line_size, end, CCTL_CMD_L2_PA_WBINVAL);
	}
	cpu_dcache_inval_range(start, end);
	cpu_l2cache_op(start, end, CCTL_CMD_L2_PA_INVAL);
	__nds32__msync_all();
	local_irq_restore(flags);

}

void cpu_dma_wbinval_range(unsigned long start, unsigned long end)
{
	unsigned long line_size;
	unsigned long flags;
	line_size = L1_cache_info[DCACHE].line_size;
	start = start & (~(line_size - 1));
	end = (end + line_size - 1) & (~(line_size - 1));
	if (unlikely(start == end))
		return;

	local_irq_save(flags);
	cpu_dcache_wbinval_range(start, end);
	cpu_l2cache_op(start, end, CCTL_CMD_L2_PA_WBINVAL);
	__nds32__msync_all();
	local_irq_restore(flags);
}

void cpu_proc_init(void)
{
}

void cpu_proc_fin(void)
{
}

void cpu_do_idle(void)
{
	__nds32__standby_no_wake_grant();
}

void cpu_reset(unsigned long reset)
{
	u32 tmp;
	GIE_DISABLE();
	tmp = __nds32__mfsr(NDS32_SR_CACHE_CTL);
	tmp &= ~(CACHE_CTL_mskIC_EN | CACHE_CTL_mskDC_EN);
	__nds32__mtsr_isb(tmp, NDS32_SR_CACHE_CTL);
	cpu_dcache_wbinval_all();
	cpu_icache_inval_all();

	__asm__ __volatile__("jr.toff %0\n\t"::"r"(reset));
}

void cpu_switch_mm(struct mm_struct *mm)
{
	unsigned long cid;
	cid = __nds32__mfsr(NDS32_SR_TLB_MISC);
	cid = (cid & ~TLB_MISC_mskCID) | mm->context.id;
	__nds32__mtsr_dsb(cid, NDS32_SR_TLB_MISC);
	__nds32__mtsr_isb(__pa(mm->pgd), NDS32_SR_L1_PPTB);
}
