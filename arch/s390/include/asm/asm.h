/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_ASM_H
#define _ASM_S390_ASM_H

/* GCC versions before 14.2.0 may die with an ICE in some configurations. */
#if defined(__GCC_ASM_FLAG_OUTPUTS__) && !(IS_ENABLED(CONFIG_CC_IS_GCC) && (GCC_VERSION < 140200))

#define __HAVE_ASM_FLAG_OUTPUTS__

#endif

#endif /* _ASM_S390_ASM_H */
