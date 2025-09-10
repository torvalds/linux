/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_VDSO_VSYSCALL_H
#define __ASM_VDSO_VSYSCALL_H

#ifndef __ASSEMBLER__

#include <linux/hrtimer.h>
#include <vdso/datapage.h>
#include <asm/vdso.h>

/* The asm-generic header needs to be included after the definitions above */
#include <asm-generic/vdso/vsyscall.h>

#endif /* !__ASSEMBLER__ */

#endif /* __ASM_VDSO_VSYSCALL_H */
