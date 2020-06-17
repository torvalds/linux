/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_VERMAGIC_H
#define _ASM_VERMAGIC_H

#ifdef CONFIG_CPU_LITTLE_ENDIAN
# ifdef CONFIG_CPU_SH2
#  define MODULE_PROC_FAMILY "SH2LE "
# elif defined  CONFIG_CPU_SH3
#  define MODULE_PROC_FAMILY "SH3LE "
# elif defined  CONFIG_CPU_SH4
#  define MODULE_PROC_FAMILY "SH4LE "
# else
#  error unknown processor family
# endif
#else
# ifdef CONFIG_CPU_SH2
#  define MODULE_PROC_FAMILY "SH2BE "
# elif defined  CONFIG_CPU_SH3
#  define MODULE_PROC_FAMILY "SH3BE "
# elif defined  CONFIG_CPU_SH4
#  define MODULE_PROC_FAMILY "SH4BE "
# else
#  error unknown processor family
# endif
#endif

#define MODULE_ARCH_VERMAGIC MODULE_PROC_FAMILY

#endif /* _ASM_VERMAGIC_H */
