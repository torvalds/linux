/*
 *  Machine specific NMI handling for generic.
 *  Split out from traps.c by Osamu Tomita <tomita@cinet.co.jp>
 */
#ifndef _ASM_X86_MACH_DEFAULT_MACH_TRAPS_H
#define _ASM_X86_MACH_DEFAULT_MACH_TRAPS_H

#include <asm/mc146818rtc.h>

static inline unsigned char get_nmi_reason(void)
{
	return inb(0x61);
}

static inline void reassert_nmi(void)
{
	int old_reg = -1;

	if (do_i_have_lock_cmos())
		old_reg = current_lock_cmos_reg();
	else
		lock_cmos(0); /* register doesn't matter here */
	outb(0x8f, 0x70);
	inb(0x71);		/* dummy */
	outb(0x0f, 0x70);
	inb(0x71);		/* dummy */
	if (old_reg >= 0)
		outb(old_reg, 0x70);
	else
		unlock_cmos();
}

#endif /* _ASM_X86_MACH_DEFAULT_MACH_TRAPS_H */
