/*
 * Suspend support specific for s390.
 *
 * Copyright IBM Corp. 2009
 *
 * Author(s): Hans-Joachim Picht <hans@linux.vnet.ibm.com>
 */

#include <linux/mm.h>
#include <linux/suspend.h>
#include <linux/reboot.h>
#include <linux/pfn.h>
#include <asm/sections.h>
#include <asm/ipl.h>

/*
 * References to section boundaries
 */
extern const void __nosave_begin, __nosave_end;

/*
 *  check if given pfn is in the 'nosave' or in the read only NSS section
 */
int pfn_is_nosave(unsigned long pfn)
{
	unsigned long nosave_begin_pfn = __pa(&__nosave_begin) >> PAGE_SHIFT;
	unsigned long nosave_end_pfn = PAGE_ALIGN(__pa(&__nosave_end))
					>> PAGE_SHIFT;
	unsigned long eshared_pfn = PFN_DOWN(__pa(&_eshared)) - 1;
	unsigned long stext_pfn = PFN_DOWN(__pa(&_stext));

	if (pfn >= nosave_begin_pfn && pfn < nosave_end_pfn)
		return 1;
	if (pfn >= stext_pfn && pfn <= eshared_pfn) {
		if (ipl_info.type == IPL_TYPE_NSS)
			return 1;
	} else if ((tprot(pfn * PAGE_SIZE) && pfn > 0))
		return 1;
	return 0;
}
