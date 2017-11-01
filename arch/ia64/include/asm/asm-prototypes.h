/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_ASM_PROTOTYPES_H
#define _ASM_IA64_ASM_PROTOTYPES_H

#include <asm/cacheflush.h>
#include <asm/checksum.h>
#include <asm/esi.h>
#include <asm/ftrace.h>
#include <asm/page.h>
#include <asm/pal.h>
#include <asm/string.h>
#include <asm/uaccess.h>
#include <asm/unwind.h>
#include <asm/xor.h>

extern const char ia64_ivt[];

signed int __divsi3(signed int, unsigned int);
signed int __modsi3(signed int, unsigned int);

signed long long __divdi3(signed long long, unsigned long long);
signed long long __moddi3(signed long long, unsigned long long);

unsigned int __udivsi3(unsigned int, unsigned int);
unsigned int __umodsi3(unsigned int, unsigned int);

unsigned long long __udivdi3(unsigned long long, unsigned long long);
unsigned long long __umoddi3(unsigned long long, unsigned long long);

#endif /* _ASM_IA64_ASM_PROTOTYPES_H */
