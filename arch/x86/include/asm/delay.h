/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_DELAY_H
#define _ASM_X86_DELAY_H

#include <asm-generic/delay.h>
#include <linux/init.h>

void __init use_tsc_delay(void);
void __init use_tpause_delay(void);
void use_mwaitx_delay(void);

#endif /* _ASM_X86_DELAY_H */
