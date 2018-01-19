/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/ftrace.h>
#include <linux/uaccess.h>
#include <asm/string.h>
#include <asm/page.h>
#include <asm/checksum.h>

#include <asm-generic/asm-prototypes.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/special_insns.h>
#include <asm/preempt.h>

#ifndef CONFIG_X86_CMPXCHG64
extern void cmpxchg8b_emu(void);
#endif
