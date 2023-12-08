// SPDX-License-Identifier: GPL-2.0
#include <asm/tlbflush.h>

extern int restore_image(void);

int swsusp_arch_resume(void)
{
	/* Avoid TLB mismatch during and after kernel resume */
	local_flush_tlb_all();
	return restore_image();
}
