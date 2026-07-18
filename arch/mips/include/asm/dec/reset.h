/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *	arch/mips/include/asm/dec/reset.h
 *
 *	DECstation/DECsystem halt/reset support.
 *
 *	Copyright (C) 2026  Maciej W. Rozycki
 */
#ifndef __ASM_DEC_RESET_H
#define __ASM_DEC_RESET_H

#include <linux/compiler_attributes.h>

void __noreturn dec_machine_restart(char *command);
void __noreturn dec_machine_halt(void);
void __noreturn dec_machine_power_off(void);
irqreturn_t dec_intr_halt(int irq, void *dev_id);

#endif /* __ASM_DEC_RESET_H */
