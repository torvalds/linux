/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/include/asm/glue-cache.h
 *
 *  Copyright (C) 1999-2002 Russell King
 */
#ifndef ASM_GLUE_CACHE_H
#define ASM_GLUE_CACHE_H

#include <asm/glue.h>

/*
 *	Cache Model
 *	===========
 */
#undef _CACHE
#undef MULTI_CACHE

#if defined(CONFIG_CPU_CACHE_V4)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE v4
# endif
#endif

#if defined(CONFIG_CPU_ARM920T) || defined(CONFIG_CPU_ARM922T) || \
    defined(CONFIG_CPU_ARM925T) || defined(CONFIG_CPU_ARM1020) || \
    defined(CONFIG_CPU_ARM1026)
# define MULTI_CACHE 1
#endif

#if defined(CONFIG_CPU_FA526)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE fa
# endif
#endif

#if defined(CONFIG_CPU_ARM926T)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE arm926
# endif
#endif

#if defined(CONFIG_CPU_ARM940T)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE arm940
# endif
#endif

#if defined(CONFIG_CPU_ARM946E)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE arm946
# endif
#endif

#if defined(CONFIG_CPU_CACHE_V4WB)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE v4wb
# endif
#endif

#if defined(CONFIG_CPU_XSCALE)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE xscale
# endif
#endif

#if defined(CONFIG_CPU_XSC3)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE xsc3
# endif
#endif

#if defined(CONFIG_CPU_MOHAWK)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE mohawk
# endif
#endif

#if defined(CONFIG_CPU_FEROCEON)
# define MULTI_CACHE 1
#endif

#if defined(CONFIG_CPU_V6) || defined(CONFIG_CPU_V6K)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE v6
# endif
#endif

#if defined(CONFIG_CPU_V7)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE v7
# endif
#endif

#if defined(CONFIG_CACHE_B15_RAC)
# define MULTI_CACHE 1
#endif

#ifdef CONFIG_CPU_CACHE_NOP
#  define MULTI_CACHE 1
#endif

#if defined(CONFIG_CPU_V7M)
#  define MULTI_CACHE 1
#endif

#if !defined(_CACHE) && !defined(MULTI_CACHE)
#error Unknown cache maintenance model
#endif

#ifndef MULTI_CACHE
#define __cpuc_flush_icache_all		__glue(_CACHE,_flush_icache_all)
#define __cpuc_flush_kern_all		__glue(_CACHE,_flush_kern_cache_all)
/* This function only has a dedicated assembly callback on the v7 cache */
#ifdef CONFIG_CPU_CACHE_V7
#define __cpuc_flush_kern_louis		__glue(_CACHE,_flush_kern_cache_louis)
#else
#define __cpuc_flush_kern_louis		__glue(_CACHE,_flush_kern_cache_all)
#endif
#define __cpuc_flush_user_all		__glue(_CACHE,_flush_user_cache_all)
#define __cpuc_flush_user_range		__glue(_CACHE,_flush_user_cache_range)
#define __cpuc_coherent_kern_range	__glue(_CACHE,_coherent_kern_range)
#define __cpuc_coherent_user_range	__glue(_CACHE,_coherent_user_range)
#define __cpuc_flush_dcache_area	__glue(_CACHE,_flush_kern_dcache_area)

#define dmac_flush_range		__glue(_CACHE,_dma_flush_range)
#endif

#endif
