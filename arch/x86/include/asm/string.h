/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_STRING_H
#define _ASM_X86_STRING_H

#ifdef CONFIG_X86_32
# include <asm/string_32.h>
#else
# include <asm/string_64.h>
#endif

#include <asm/shared/string.h>

#endif /* _ASM_X86_STRING_H */
