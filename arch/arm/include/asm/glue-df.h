/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/glue-df.h
 *
 *  Copyright (C) 1997-1999 Russell King
 *  Copyright (C) 2000-2002 Deep Blue Solutions Ltd.
 */
#ifndef ASM_GLUE_DF_H
#define ASM_GLUE_DF_H

#include <asm/glue.h>

/*
 *	Data Abort Model
 *	================
 *
 *	We have the following to choose from:
 *	  arm7		- ARM7 style
 *	  v4_early	- ARMv4 without Thumb early abort handler
 *	  v4t_late	- ARMv4 with Thumb late abort handler
 *	  v4t_early	- ARMv4 with Thumb early abort handler
 *	  v5t_early	- ARMv5 with Thumb early abort handler
 *	  v5tj_early	- ARMv5 with Thumb and Java early abort handler
 *	  xscale	- ARMv5 with Thumb with Xscale extensions
 *	  v6_early	- ARMv6 generic early abort handler
 *	  v7_early	- ARMv7 generic early abort handler
 */
#undef CPU_DABORT_HANDLER
#undef MULTI_DABORT

#ifdef CONFIG_CPU_ABRT_EV4
# ifdef CPU_DABORT_HANDLER
#  define MULTI_DABORT 1
# else
#  define CPU_DABORT_HANDLER v4_early_abort
# endif
#endif

#ifdef CONFIG_CPU_ABRT_LV4T
# ifdef CPU_DABORT_HANDLER
#  define MULTI_DABORT 1
# else
#  define CPU_DABORT_HANDLER v4t_late_abort
# endif
#endif

#ifdef CONFIG_CPU_ABRT_EV4T
# ifdef CPU_DABORT_HANDLER
#  define MULTI_DABORT 1
# else
#  define CPU_DABORT_HANDLER v4t_early_abort
# endif
#endif

#ifdef CONFIG_CPU_ABRT_EV5T
# ifdef CPU_DABORT_HANDLER
#  define MULTI_DABORT 1
# else
#  define CPU_DABORT_HANDLER v5t_early_abort
# endif
#endif

#ifdef CONFIG_CPU_ABRT_EV5TJ
# ifdef CPU_DABORT_HANDLER
#  define MULTI_DABORT 1
# else
#  define CPU_DABORT_HANDLER v5tj_early_abort
# endif
#endif

#ifdef CONFIG_CPU_ABRT_EV6
# ifdef CPU_DABORT_HANDLER
#  define MULTI_DABORT 1
# else
#  define CPU_DABORT_HANDLER v6_early_abort
# endif
#endif

#ifdef CONFIG_CPU_ABRT_EV7
# ifdef CPU_DABORT_HANDLER
#  define MULTI_DABORT 1
# else
#  define CPU_DABORT_HANDLER v7_early_abort
# endif
#endif

#ifdef CONFIG_CPU_ABRT_NOMMU
# ifdef CPU_DABORT_HANDLER
#  define MULTI_DABORT 1
# else
#  define CPU_DABORT_HANDLER nommu_early_abort
# endif
#endif

#ifndef CPU_DABORT_HANDLER
#error Unknown data abort handler type
#endif

#endif
