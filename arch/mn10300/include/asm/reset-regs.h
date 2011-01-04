/* MN10300 Reset controller and watchdog timer definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_RESET_REGS_H
#define _ASM_RESET_REGS_H

#include <asm/cpu-regs.h>
#include <asm/exceptions.h>

#ifdef __KERNEL__

#ifdef CONFIG_MN10300_WD_TIMER
#define ARCH_HAS_NMI_WATCHDOG		/* See include/linux/nmi.h */
#endif

/*
 * watchdog timer registers
 */
#define WDBC			__SYSREGC(0xc0001000, u8) /* watchdog binary counter reg */

#define WDCTR			__SYSREG(0xc0001002, u8)  /* watchdog timer control reg */
#define WDCTR_WDCK		0x07	/* clock source selection */
#define WDCTR_WDCK_256th	0x00	/* - OSCI/256 */
#define WDCTR_WDCK_1024th	0x01	/* - OSCI/1024 */
#define WDCTR_WDCK_2048th	0x02	/* - OSCI/2048 */
#define WDCTR_WDCK_16384th	0x03	/* - OSCI/16384 */
#define WDCTR_WDCK_65536th	0x04	/* - OSCI/65536 */
#define WDCTR_WDRST		0x40	/* binary counter reset */
#define WDCTR_WDCNE		0x80	/* watchdog timer enable */

#define RSTCTR			__SYSREG(0xc0001004, u8) /* reset control reg */
#define RSTCTR_CHIPRST		0x01	/* chip reset */
#define RSTCTR_DBFRST		0x02	/* double fault reset flag */
#define RSTCTR_WDTRST		0x04	/* watchdog timer reset flag */
#define RSTCTR_WDREN		0x08	/* watchdog timer reset enable */

#ifndef __ASSEMBLY__

static inline void mn10300_proc_hard_reset(void)
{
	RSTCTR &= ~RSTCTR_CHIPRST;
	RSTCTR |= RSTCTR_CHIPRST;
}

extern unsigned int watchdog_alert_counter[];

extern void watchdog_go(void);
extern asmlinkage void watchdog_handler(void);
extern asmlinkage
void watchdog_interrupt(struct pt_regs *, enum exception_code);

#endif

#endif /* __KERNEL__ */

#endif /* _ASM_RESET_REGS_H */
