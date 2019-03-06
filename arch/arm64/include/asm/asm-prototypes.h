/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_PROTOTYPES_H
#define __ASM_PROTOTYPES_H
/*
 * CONFIG_MODVERSIONS requires a C declaration to generate the appropriate CRC
 * for each symbol. Since commit:
 *
 *   4efca4ed05cbdfd1 ("kbuild: modversions for EXPORT_SYMBOL() for asm")
 *
 * ... kbuild will automatically pick these up from <asm/asm-prototypes.h> and
 * feed this to genksyms when building assembly files.
 */
#include <linux/arm-smccc.h>

#include <asm/ftrace.h>
#include <asm/page.h>
#include <asm/string.h>
#include <asm/uaccess.h>

#include <asm-generic/asm-prototypes.h>

long long __ashlti3(long long a, int b);
long long __ashrti3(long long a, int b);
long long __lshrti3(long long a, int b);

#endif /* __ASM_PROTOTYPES_H */
