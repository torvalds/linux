/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __S390_VDSO_SYMBOLS_H__
#define __S390_VDSO_SYMBOLS_H__

#include <generated/vdso-offsets.h>

#define VDSO_SYMBOL(tsk, name) ((tsk)->mm->context.vdso_base + (vdso_offset_##name))

#endif /* __S390_VDSO_SYMBOLS_H__ */
