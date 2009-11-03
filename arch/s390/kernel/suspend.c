/*
 * Suspend support specific for s390.
 *
 * Copyright IBM Corp. 2009
 *
 * Author(s): Hans-Joachim Picht <hans@linux.vnet.ibm.com>
 */

#include <linux/pfn.h>
#include <asm/system.h>

/*
 * References to section boundaries
 */
extern const void __nosave_begin, __nosave_end;

int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = PFN_DOWN(__pa(&__nosave_begin));
	unsigned long nosave_end_pfn = PFN_DOWN(__pa(&__nosave_end));

	/* Always save lowcore pages (LC protection might be enabled). */
	if (pfn <= LC_PAGES)
		return 0;
	if (pfn >= nosave_begin_pfn && pfn < nosave_end_pfn)
		return 1;
	/* Skip memory holes and read-only pages (NSS, DCSS, ...). */
	if (tprot(PFN_PHYS(pfn)))
		return 1;
	return 0;
}

void save_processor_state(void)
{
	/* swsusp_arch_suspend() actually saves all cpu register contents.
	 * Machine checks must be disabled since swsusp_arch_suspend() stores
	 * register contents to their lowcore save areas. That's the same
	 * place where register contents on machine checks would be saved.
	 * To avoid register corruption disable machine checks.
	 * We must also disable machine checks in the new psw mask for
	 * program checks, since swsusp_arch_suspend() may generate program
	 * checks. Disabling machine checks for all other new psw masks is
	 * just paranoia.
	 */
	local_mcck_disable();
	/* Disable lowcore protection */
	__ctl_clear_bit(0,28);
	S390_lowcore.external_new_psw.mask &= ~PSW_MASK_MCHECK;
	S390_lowcore.svc_new_psw.mask &= ~PSW_MASK_MCHECK;
	S390_lowcore.io_new_psw.mask &= ~PSW_MASK_MCHECK;
	S390_lowcore.program_new_psw.mask &= ~PSW_MASK_MCHECK;
}

void restore_processor_state(void)
{
	S390_lowcore.external_new_psw.mask |= PSW_MASK_MCHECK;
	S390_lowcore.svc_new_psw.mask |= PSW_MASK_MCHECK;
	S390_lowcore.io_new_psw.mask |= PSW_MASK_MCHECK;
	S390_lowcore.program_new_psw.mask |= PSW_MASK_MCHECK;
	/* Enable lowcore protection */
	__ctl_set_bit(0,28);
	local_mcck_enable();
}
