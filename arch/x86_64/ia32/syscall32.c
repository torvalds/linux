/* Copyright 2002,2003 Andi Kleen, SuSE Labs */

/* vsyscall handling for 32bit processes. Map a stub page into it 
   on demand because 32bit cannot reach the kernel's fixmaps */

#include <linux/mm.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/stringify.h>
#include <asm/proto.h>
#include <asm/tlbflush.h>
#include <asm/ia32_unistd.h>

/* 32bit VDSOs mapped into user space. */ 
asm(".section \".init.data\",\"aw\"\n"
    "syscall32_syscall:\n"
    ".incbin \"arch/x86_64/ia32/vsyscall-syscall.so\"\n"
    "syscall32_syscall_end:\n"
    "syscall32_sysenter:\n"
    ".incbin \"arch/x86_64/ia32/vsyscall-sysenter.so\"\n"
    "syscall32_sysenter_end:\n"
    ".previous");

extern unsigned char syscall32_syscall[], syscall32_syscall_end[];
extern unsigned char syscall32_sysenter[], syscall32_sysenter_end[];
extern int sysctl_vsyscall32;

char *syscall32_page; 
static int use_sysenter = -1;

/*
 * Map the 32bit vsyscall page on demand.
 *
 * RED-PEN: This knows too much about high level VM.
 *
 * Alternative would be to generate a vma with appropriate backing options
 * and let it be handled by generic VM.
 */
int __map_syscall32(struct mm_struct *mm, unsigned long address)
{ 
	pgd_t *pgd;
	pud_t *pud;
	pte_t *pte;
	pmd_t *pmd;
	int err = -ENOMEM;

	spin_lock(&mm->page_table_lock); 
 	pgd = pgd_offset(mm, address);
 	pud = pud_alloc(mm, pgd, address);
 	if (pud) {
 		pmd = pmd_alloc(mm, pud, address);
 		if (pmd && (pte = pte_alloc_map(mm, pmd, address)) != NULL) {
 			if (pte_none(*pte)) {
 				set_pte(pte,
 					mk_pte(virt_to_page(syscall32_page),
 					       PAGE_KERNEL_VSYSCALL32));
 			}
 			/* Flush only the local CPU. Other CPUs taking a fault
 			   will just end up here again
			   This probably not needed and just paranoia. */
 			__flush_tlb_one(address);
 			err = 0;
		}
	}
	spin_unlock(&mm->page_table_lock);
	return err;
}

int map_syscall32(struct mm_struct *mm, unsigned long address)
{
	int err;
	down_read(&mm->mmap_sem);
	err = __map_syscall32(mm, address);
	up_read(&mm->mmap_sem);
	return err;
}

static int __init init_syscall32(void)
{ 
	syscall32_page = (void *)get_zeroed_page(GFP_KERNEL); 
	if (!syscall32_page) 
		panic("Cannot allocate syscall32 page"); 
	SetPageReserved(virt_to_page(syscall32_page));
 	if (use_sysenter > 0) {
 		memcpy(syscall32_page, syscall32_sysenter,
 		       syscall32_sysenter_end - syscall32_sysenter);
 	} else {
  		memcpy(syscall32_page, syscall32_syscall,
  		       syscall32_syscall_end - syscall32_syscall);
  	}	
	return 0;
} 
	
__initcall(init_syscall32); 

/* May not be __init: called during resume */
void syscall32_cpu_init(void)
{
	if (use_sysenter < 0)
 		use_sysenter = (boot_cpu_data.x86_vendor == X86_VENDOR_INTEL);

	/* Load these always in case some future AMD CPU supports
	   SYSENTER from compat mode too. */
	checking_wrmsrl(MSR_IA32_SYSENTER_CS, (u64)__KERNEL_CS);
	checking_wrmsrl(MSR_IA32_SYSENTER_ESP, 0ULL);
	checking_wrmsrl(MSR_IA32_SYSENTER_EIP, (u64)ia32_sysenter_target);

	wrmsrl(MSR_CSTAR, ia32_cstar_target);
}
