/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_PROTOTYPES_H
#define __ASM_PROTOTYPES_H

#include <asm/cacheflush.h>
#include <asm/checksum.h>
#include <asm/ftrace.h>
#include <asm/page.h>
#include <asm/string.h>
#include <asm/uaccess.h>

#include <asm-generic/asm-prototypes.h>

/*
 * gcc internal math functions
 */
long long __ashrdi3(long long, int);
long long __ashldi3(long long, int);
long long __bswapdi2(long long);
int __bswapsi2(int);
long long __lshrdi3(long long, int);
int __divsi3(int, int);
int __modsi3(int, int);
int __mulsi3(int, int);
unsigned int __udivsi3(unsigned int, unsigned int);
unsigned int __umodsi3(unsigned int, unsigned int);
unsigned long long __umulsidi3(unsigned int, unsigned int);

#endif /* __ASM_PROTOTYPES_H */
