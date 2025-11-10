/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __S390_VDSO_SYMBOLS_H__
#define __S390_VDSO_SYMBOLS_H__

#include <generated/vdso64-offsets.h>

#define VDSO64_SYMBOL(tsk, name) ((tsk)->mm->context.vdso_base + (vdso64_offset_##name))

#endif /* __S390_VDSO_SYMBOLS_H__ */
