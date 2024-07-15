// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file defines C prototypes for the low-level processor assembly functions
 * and creates a reference for CFI. This needs to be done for every assembly
 * processor ("proc") function that is called from C but does not have a
 * corresponding C implementation.
 *
 * Processors are listed in the order they appear in the Makefile.
 *
 * Functions are listed if and only if they see use on the target CPU, and in
 * the order they are defined in struct processor.
 */
#include <asm/proc-fns.h>

#ifdef CONFIG_CPU_ARM7TDMI
void cpu_arm7tdmi_proc_init(void);
__ADDRESSABLE(cpu_arm7tdmi_proc_init);
void cpu_arm7tdmi_proc_fin(void);
__ADDRESSABLE(cpu_arm7tdmi_proc_fin);
void cpu_arm7tdmi_reset(void);
__ADDRESSABLE(cpu_arm7tdmi_reset);
int cpu_arm7tdmi_do_idle(void);
__ADDRESSABLE(cpu_arm7tdmi_do_idle);
void cpu_arm7tdmi_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm7tdmi_dcache_clean_area);
void cpu_arm7tdmi_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm7tdmi_switch_mm);
#endif

#ifdef CONFIG_CPU_ARM720T
void cpu_arm720_proc_init(void);
__ADDRESSABLE(cpu_arm720_proc_init);
void cpu_arm720_proc_fin(void);
__ADDRESSABLE(cpu_arm720_proc_fin);
void cpu_arm720_reset(void);
__ADDRESSABLE(cpu_arm720_reset);
int cpu_arm720_do_idle(void);
__ADDRESSABLE(cpu_arm720_do_idle);
void cpu_arm720_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm720_dcache_clean_area);
void cpu_arm720_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm720_switch_mm);
void cpu_arm720_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_arm720_set_pte_ext);
#endif

#ifdef CONFIG_CPU_ARM740T
void cpu_arm740_proc_init(void);
__ADDRESSABLE(cpu_arm740_proc_init);
void cpu_arm740_proc_fin(void);
__ADDRESSABLE(cpu_arm740_proc_fin);
void cpu_arm740_reset(void);
__ADDRESSABLE(cpu_arm740_reset);
int cpu_arm740_do_idle(void);
__ADDRESSABLE(cpu_arm740_do_idle);
void cpu_arm740_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm740_dcache_clean_area);
void cpu_arm740_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm740_switch_mm);
#endif

#ifdef CONFIG_CPU_ARM9TDMI
void cpu_arm9tdmi_proc_init(void);
__ADDRESSABLE(cpu_arm9tdmi_proc_init);
void cpu_arm9tdmi_proc_fin(void);
__ADDRESSABLE(cpu_arm9tdmi_proc_fin);
void cpu_arm9tdmi_reset(void);
__ADDRESSABLE(cpu_arm9tdmi_reset);
int cpu_arm9tdmi_do_idle(void);
__ADDRESSABLE(cpu_arm9tdmi_do_idle);
void cpu_arm9tdmi_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm9tdmi_dcache_clean_area);
void cpu_arm9tdmi_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm9tdmi_switch_mm);
#endif

#ifdef CONFIG_CPU_ARM920T
void cpu_arm920_proc_init(void);
__ADDRESSABLE(cpu_arm920_proc_init);
void cpu_arm920_proc_fin(void);
__ADDRESSABLE(cpu_arm920_proc_fin);
void cpu_arm920_reset(void);
__ADDRESSABLE(cpu_arm920_reset);
int cpu_arm920_do_idle(void);
__ADDRESSABLE(cpu_arm920_do_idle);
void cpu_arm920_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm920_dcache_clean_area);
void cpu_arm920_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm920_switch_mm);
void cpu_arm920_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_arm920_set_pte_ext);
#ifdef CONFIG_ARM_CPU_SUSPEND
void cpu_arm920_do_suspend(void *);
__ADDRESSABLE(cpu_arm920_do_suspend);
void cpu_arm920_do_resume(void *);
__ADDRESSABLE(cpu_arm920_do_resume);
#endif /* CONFIG_ARM_CPU_SUSPEND */
#endif /* CONFIG_CPU_ARM920T */

#ifdef CONFIG_CPU_ARM922T
void cpu_arm922_proc_init(void);
__ADDRESSABLE(cpu_arm922_proc_init);
void cpu_arm922_proc_fin(void);
__ADDRESSABLE(cpu_arm922_proc_fin);
void cpu_arm922_reset(void);
__ADDRESSABLE(cpu_arm922_reset);
int cpu_arm922_do_idle(void);
__ADDRESSABLE(cpu_arm922_do_idle);
void cpu_arm922_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm922_dcache_clean_area);
void cpu_arm922_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm922_switch_mm);
void cpu_arm922_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_arm922_set_pte_ext);
#endif

#ifdef CONFIG_CPU_ARM925T
void cpu_arm925_proc_init(void);
__ADDRESSABLE(cpu_arm925_proc_init);
void cpu_arm925_proc_fin(void);
__ADDRESSABLE(cpu_arm925_proc_fin);
void cpu_arm925_reset(void);
__ADDRESSABLE(cpu_arm925_reset);
int cpu_arm925_do_idle(void);
__ADDRESSABLE(cpu_arm925_do_idle);
void cpu_arm925_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm925_dcache_clean_area);
void cpu_arm925_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm925_switch_mm);
void cpu_arm925_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_arm925_set_pte_ext);
#endif

#ifdef CONFIG_CPU_ARM926T
void cpu_arm926_proc_init(void);
__ADDRESSABLE(cpu_arm926_proc_init);
void cpu_arm926_proc_fin(void);
__ADDRESSABLE(cpu_arm926_proc_fin);
void cpu_arm926_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_arm926_reset);
int cpu_arm926_do_idle(void);
__ADDRESSABLE(cpu_arm926_do_idle);
void cpu_arm926_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm926_dcache_clean_area);
void cpu_arm926_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm926_switch_mm);
void cpu_arm926_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_arm926_set_pte_ext);
#ifdef CONFIG_ARM_CPU_SUSPEND
void cpu_arm926_do_suspend(void *);
__ADDRESSABLE(cpu_arm926_do_suspend);
void cpu_arm926_do_resume(void *);
__ADDRESSABLE(cpu_arm926_do_resume);
#endif /* CONFIG_ARM_CPU_SUSPEND */
#endif /* CONFIG_CPU_ARM926T */

#ifdef CONFIG_CPU_ARM940T
void cpu_arm940_proc_init(void);
__ADDRESSABLE(cpu_arm940_proc_init);
void cpu_arm940_proc_fin(void);
__ADDRESSABLE(cpu_arm940_proc_fin);
void cpu_arm940_reset(void);
__ADDRESSABLE(cpu_arm940_reset);
int cpu_arm940_do_idle(void);
__ADDRESSABLE(cpu_arm940_do_idle);
void cpu_arm940_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm940_dcache_clean_area);
void cpu_arm940_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm940_switch_mm);
#endif

#ifdef CONFIG_CPU_ARM946E
void cpu_arm946_proc_init(void);
__ADDRESSABLE(cpu_arm946_proc_init);
void cpu_arm946_proc_fin(void);
__ADDRESSABLE(cpu_arm946_proc_fin);
void cpu_arm946_reset(void);
__ADDRESSABLE(cpu_arm946_reset);
int cpu_arm946_do_idle(void);
__ADDRESSABLE(cpu_arm946_do_idle);
void cpu_arm946_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm946_dcache_clean_area);
void cpu_arm946_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm946_switch_mm);
#endif

#ifdef CONFIG_CPU_FA526
void cpu_fa526_proc_init(void);
__ADDRESSABLE(cpu_fa526_proc_init);
void cpu_fa526_proc_fin(void);
__ADDRESSABLE(cpu_fa526_proc_fin);
void cpu_fa526_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_fa526_reset);
int cpu_fa526_do_idle(void);
__ADDRESSABLE(cpu_fa526_do_idle);
void cpu_fa526_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_fa526_dcache_clean_area);
void cpu_fa526_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_fa526_switch_mm);
void cpu_fa526_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_fa526_set_pte_ext);
#endif

#ifdef CONFIG_CPU_ARM1020
void cpu_arm1020_proc_init(void);
__ADDRESSABLE(cpu_arm1020_proc_init);
void cpu_arm1020_proc_fin(void);
__ADDRESSABLE(cpu_arm1020_proc_fin);
void cpu_arm1020_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_arm1020_reset);
int cpu_arm1020_do_idle(void);
__ADDRESSABLE(cpu_arm1020_do_idle);
void cpu_arm1020_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm1020_dcache_clean_area);
void cpu_arm1020_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm1020_switch_mm);
void cpu_arm1020_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_arm1020_set_pte_ext);
#endif

#ifdef CONFIG_CPU_ARM1020E
void cpu_arm1020e_proc_init(void);
__ADDRESSABLE(cpu_arm1020e_proc_init);
void cpu_arm1020e_proc_fin(void);
__ADDRESSABLE(cpu_arm1020e_proc_fin);
void cpu_arm1020e_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_arm1020e_reset);
int cpu_arm1020e_do_idle(void);
__ADDRESSABLE(cpu_arm1020e_do_idle);
void cpu_arm1020e_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm1020e_dcache_clean_area);
void cpu_arm1020e_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm1020e_switch_mm);
void cpu_arm1020e_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_arm1020e_set_pte_ext);
#endif

#ifdef CONFIG_CPU_ARM1022
void cpu_arm1022_proc_init(void);
__ADDRESSABLE(cpu_arm1022_proc_init);
void cpu_arm1022_proc_fin(void);
__ADDRESSABLE(cpu_arm1022_proc_fin);
void cpu_arm1022_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_arm1022_reset);
int cpu_arm1022_do_idle(void);
__ADDRESSABLE(cpu_arm1022_do_idle);
void cpu_arm1022_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm1022_dcache_clean_area);
void cpu_arm1022_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm1022_switch_mm);
void cpu_arm1022_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_arm1022_set_pte_ext);
#endif

#ifdef CONFIG_CPU_ARM1026
void cpu_arm1026_proc_init(void);
__ADDRESSABLE(cpu_arm1026_proc_init);
void cpu_arm1026_proc_fin(void);
__ADDRESSABLE(cpu_arm1026_proc_fin);
void cpu_arm1026_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_arm1026_reset);
int cpu_arm1026_do_idle(void);
__ADDRESSABLE(cpu_arm1026_do_idle);
void cpu_arm1026_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_arm1026_dcache_clean_area);
void cpu_arm1026_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_arm1026_switch_mm);
void cpu_arm1026_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_arm1026_set_pte_ext);
#endif

#ifdef CONFIG_CPU_SA110
void cpu_sa110_proc_init(void);
__ADDRESSABLE(cpu_sa110_proc_init);
void cpu_sa110_proc_fin(void);
__ADDRESSABLE(cpu_sa110_proc_fin);
void cpu_sa110_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_sa110_reset);
int cpu_sa110_do_idle(void);
__ADDRESSABLE(cpu_sa110_do_idle);
void cpu_sa110_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_sa110_dcache_clean_area);
void cpu_sa110_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_sa110_switch_mm);
void cpu_sa110_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_sa110_set_pte_ext);
#endif

#ifdef CONFIG_CPU_SA1100
void cpu_sa1100_proc_init(void);
__ADDRESSABLE(cpu_sa1100_proc_init);
void cpu_sa1100_proc_fin(void);
__ADDRESSABLE(cpu_sa1100_proc_fin);
void cpu_sa1100_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_sa1100_reset);
int cpu_sa1100_do_idle(void);
__ADDRESSABLE(cpu_sa1100_do_idle);
void cpu_sa1100_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_sa1100_dcache_clean_area);
void cpu_sa1100_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_sa1100_switch_mm);
void cpu_sa1100_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_sa1100_set_pte_ext);
#ifdef CONFIG_ARM_CPU_SUSPEND
void cpu_sa1100_do_suspend(void *);
__ADDRESSABLE(cpu_sa1100_do_suspend);
void cpu_sa1100_do_resume(void *);
__ADDRESSABLE(cpu_sa1100_do_resume);
#endif /* CONFIG_ARM_CPU_SUSPEND */
#endif /* CONFIG_CPU_SA1100 */

#ifdef CONFIG_CPU_XSCALE
void cpu_xscale_proc_init(void);
__ADDRESSABLE(cpu_xscale_proc_init);
void cpu_xscale_proc_fin(void);
__ADDRESSABLE(cpu_xscale_proc_fin);
void cpu_xscale_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_xscale_reset);
int cpu_xscale_do_idle(void);
__ADDRESSABLE(cpu_xscale_do_idle);
void cpu_xscale_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_xscale_dcache_clean_area);
void cpu_xscale_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_xscale_switch_mm);
void cpu_xscale_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_xscale_set_pte_ext);
#ifdef CONFIG_ARM_CPU_SUSPEND
void cpu_xscale_do_suspend(void *);
__ADDRESSABLE(cpu_xscale_do_suspend);
void cpu_xscale_do_resume(void *);
__ADDRESSABLE(cpu_xscale_do_resume);
#endif /* CONFIG_ARM_CPU_SUSPEND */
#endif /* CONFIG_CPU_XSCALE */

#ifdef CONFIG_CPU_XSC3
void cpu_xsc3_proc_init(void);
__ADDRESSABLE(cpu_xsc3_proc_init);
void cpu_xsc3_proc_fin(void);
__ADDRESSABLE(cpu_xsc3_proc_fin);
void cpu_xsc3_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_xsc3_reset);
int cpu_xsc3_do_idle(void);
__ADDRESSABLE(cpu_xsc3_do_idle);
void cpu_xsc3_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_xsc3_dcache_clean_area);
void cpu_xsc3_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_xsc3_switch_mm);
void cpu_xsc3_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_xsc3_set_pte_ext);
#ifdef CONFIG_ARM_CPU_SUSPEND
void cpu_xsc3_do_suspend(void *);
__ADDRESSABLE(cpu_xsc3_do_suspend);
void cpu_xsc3_do_resume(void *);
__ADDRESSABLE(cpu_xsc3_do_resume);
#endif /* CONFIG_ARM_CPU_SUSPEND */
#endif /* CONFIG_CPU_XSC3 */

#ifdef CONFIG_CPU_MOHAWK
void cpu_mohawk_proc_init(void);
__ADDRESSABLE(cpu_mohawk_proc_init);
void cpu_mohawk_proc_fin(void);
__ADDRESSABLE(cpu_mohawk_proc_fin);
void cpu_mohawk_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_mohawk_reset);
int cpu_mohawk_do_idle(void);
__ADDRESSABLE(cpu_mohawk_do_idle);
void cpu_mohawk_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_mohawk_dcache_clean_area);
void cpu_mohawk_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_mohawk_switch_mm);
void cpu_mohawk_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_mohawk_set_pte_ext);
#ifdef CONFIG_ARM_CPU_SUSPEND
void cpu_mohawk_do_suspend(void *);
__ADDRESSABLE(cpu_mohawk_do_suspend);
void cpu_mohawk_do_resume(void *);
__ADDRESSABLE(cpu_mohawk_do_resume);
#endif /* CONFIG_ARM_CPU_SUSPEND */
#endif /* CONFIG_CPU_MOHAWK */

#ifdef CONFIG_CPU_FEROCEON
void cpu_feroceon_proc_init(void);
__ADDRESSABLE(cpu_feroceon_proc_init);
void cpu_feroceon_proc_fin(void);
__ADDRESSABLE(cpu_feroceon_proc_fin);
void cpu_feroceon_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_feroceon_reset);
int cpu_feroceon_do_idle(void);
__ADDRESSABLE(cpu_feroceon_do_idle);
void cpu_feroceon_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_feroceon_dcache_clean_area);
void cpu_feroceon_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_feroceon_switch_mm);
void cpu_feroceon_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_feroceon_set_pte_ext);
#ifdef CONFIG_ARM_CPU_SUSPEND
void cpu_feroceon_do_suspend(void *);
__ADDRESSABLE(cpu_feroceon_do_suspend);
void cpu_feroceon_do_resume(void *);
__ADDRESSABLE(cpu_feroceon_do_resume);
#endif /* CONFIG_ARM_CPU_SUSPEND */
#endif /* CONFIG_CPU_FEROCEON */

#if defined(CONFIG_CPU_V6) || defined(CONFIG_CPU_V6K)
void cpu_v6_proc_init(void);
__ADDRESSABLE(cpu_v6_proc_init);
void cpu_v6_proc_fin(void);
__ADDRESSABLE(cpu_v6_proc_fin);
void cpu_v6_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_v6_reset);
int cpu_v6_do_idle(void);
__ADDRESSABLE(cpu_v6_do_idle);
void cpu_v6_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_v6_dcache_clean_area);
void cpu_v6_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_v6_switch_mm);
void cpu_v6_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_v6_set_pte_ext);
#ifdef CONFIG_ARM_CPU_SUSPEND
void cpu_v6_do_suspend(void *);
__ADDRESSABLE(cpu_v6_do_suspend);
void cpu_v6_do_resume(void *);
__ADDRESSABLE(cpu_v6_do_resume);
#endif /* CONFIG_ARM_CPU_SUSPEND */
#endif /* CPU_V6 */

#ifdef CONFIG_CPU_V7
void cpu_v7_proc_init(void);
__ADDRESSABLE(cpu_v7_proc_init);
void cpu_v7_proc_fin(void);
__ADDRESSABLE(cpu_v7_proc_fin);
void cpu_v7_reset(void);
__ADDRESSABLE(cpu_v7_reset);
int cpu_v7_do_idle(void);
__ADDRESSABLE(cpu_v7_do_idle);
#ifdef CONFIG_PJ4B_ERRATA_4742
int cpu_pj4b_do_idle(void);
__ADDRESSABLE(cpu_pj4b_do_idle);
#endif
void cpu_v7_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_v7_dcache_clean_area);
void cpu_v7_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
/* Special switch_mm() callbacks to work around bugs in v7 */
__ADDRESSABLE(cpu_v7_switch_mm);
void cpu_v7_iciallu_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_v7_iciallu_switch_mm);
void cpu_v7_bpiall_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_v7_bpiall_switch_mm);
#ifdef CONFIG_ARM_LPAE
void cpu_v7_set_pte_ext(pte_t *ptep, pte_t pte);
#else
void cpu_v7_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
#endif
__ADDRESSABLE(cpu_v7_set_pte_ext);
#ifdef CONFIG_ARM_CPU_SUSPEND
void cpu_v7_do_suspend(void *);
__ADDRESSABLE(cpu_v7_do_suspend);
void cpu_v7_do_resume(void *);
__ADDRESSABLE(cpu_v7_do_resume);
/* Special versions of suspend and resume for the CA9MP cores */
void cpu_ca9mp_do_suspend(void *);
__ADDRESSABLE(cpu_ca9mp_do_suspend);
void cpu_ca9mp_do_resume(void *);
__ADDRESSABLE(cpu_ca9mp_do_resume);
/* Special versions of suspend and resume for the Marvell PJ4B cores */
#ifdef CONFIG_CPU_PJ4B
void cpu_pj4b_do_suspend(void *);
__ADDRESSABLE(cpu_pj4b_do_suspend);
void cpu_pj4b_do_resume(void *);
__ADDRESSABLE(cpu_pj4b_do_resume);
#endif /* CONFIG_CPU_PJ4B */
#endif /* CONFIG_ARM_CPU_SUSPEND */
#endif /* CONFIG_CPU_V7 */

#ifdef CONFIG_CPU_V7M
void cpu_v7m_proc_init(void);
__ADDRESSABLE(cpu_v7m_proc_init);
void cpu_v7m_proc_fin(void);
__ADDRESSABLE(cpu_v7m_proc_fin);
void cpu_v7m_reset(unsigned long addr, bool hvc);
__ADDRESSABLE(cpu_v7m_reset);
int cpu_v7m_do_idle(void);
__ADDRESSABLE(cpu_v7m_do_idle);
void cpu_v7m_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_v7m_dcache_clean_area);
void cpu_v7m_switch_mm(phys_addr_t pgd_phys, struct mm_struct *mm);
__ADDRESSABLE(cpu_v7m_switch_mm);
void cpu_v7m_set_pte_ext(pte_t *ptep, pte_t pte, unsigned int ext);
__ADDRESSABLE(cpu_v7m_set_pte_ext);
#ifdef CONFIG_ARM_CPU_SUSPEND
void cpu_v7m_do_suspend(void *);
__ADDRESSABLE(cpu_v7m_do_suspend);
void cpu_v7m_do_resume(void *);
__ADDRESSABLE(cpu_v7m_do_resume);
#endif /* CONFIG_ARM_CPU_SUSPEND */
void cpu_cm7_proc_fin(void);
__ADDRESSABLE(cpu_cm7_proc_fin);
void cpu_cm7_dcache_clean_area(void *addr, int size);
__ADDRESSABLE(cpu_cm7_dcache_clean_area);
#endif /* CONFIG_CPU_V7M */
