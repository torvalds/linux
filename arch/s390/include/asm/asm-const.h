/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_ASM_CONST_H
#define _ASM_S390_ASM_CONST_H

#ifdef __ASSEMBLER__
#  define stringify_in_c(...)	__VA_ARGS__
#else
/* This version of stringify will deal with commas... */
#  define __stringify_in_c(...)	#__VA_ARGS__
#  define stringify_in_c(...)	__stringify_in_c(__VA_ARGS__) " "
#endif
#endif /* _ASM_S390_ASM_CONST_H */
