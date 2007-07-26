/* Copyright 2002,2003 Andi Kleen, SuSE Labs */

/* vsyscall handling for 32bit processes. Map a stub page into it 
   on demand because 32bit cannot reach the kernel's fixmaps */

#include <linux/mm.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/stringify.h>
#include <linux/security.h>
#include <asm/proto.h>
#include <asm/tlbflush.h>
#include <asm/ia32_unistd.h>
#include <asm/vsyscall32.h>

extern unsigned char syscall32_syscall[], syscall32_syscall_end[];
extern unsigned char syscall32_sysenter[], syscall32_sysenter_end[];
extern int sysctl_vsyscall32;

static struct page *syscall32_pages[1];
static int use_sysenter = -1;

struct linux_binprm;

/* Setup a VMA at program startup for the vsyscall page */
int syscall32_setup_pages(struct linux_binprm *bprm, int exstack)
{
	struct mm_struct *mm = current->mm;
	int ret;

	down_write(&mm->mmap_sem);
	/*
	 * MAYWRITE to allow gdb to COW and set breakpoints
	 *
	 * Make sure the vDSO gets into every core dump.
	 * Dumping its contents makes post-mortem fully interpretable later
	 * without matching up the same kernel and hardware config to see
	 * what PC values meant.
	 */
	/* Could randomize here */
	ret = install_special_mapping(mm, VSYSCALL32_BASE, PAGE_SIZE,
				      VM_READ|VM_EXEC|
				      VM_MAYREAD|VM_MAYWRITE|VM_MAYEXEC|
				      VM_ALWAYSDUMP,
				      syscall32_pages);
	up_write(&mm->mmap_sem);
	return ret;
}

static int __init init_syscall32(void)
{ 
	char *syscall32_page = (void *)get_zeroed_page(GFP_KERNEL);
	if (!syscall32_page) 
		panic("Cannot allocate syscall32 page"); 
	syscall32_pages[0] = virt_to_page(syscall32_page);
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
