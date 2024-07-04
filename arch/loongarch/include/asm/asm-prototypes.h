/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/uaccess.h>
#include <asm/fpu.h>
#include <asm/lbt.h>
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/ftrace.h>
#include <asm-generic/asm-prototypes.h>

#ifdef CONFIG_ARCH_SUPPORTS_INT128
__int128_t __ashlti3(__int128_t a, int b);
__int128_t __ashrti3(__int128_t a, int b);
__int128_t __lshrti3(__int128_t a, int b);
#endif
