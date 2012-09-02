/*
 *  arch/arm/include/asm/glue-cache.h
 *
 *  Copyright (C) 1999-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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

#if defined(CONFIG_CPU_CACHE_V3)
# ifdef _CACHE
#  define MULTI_CACHE 1
# else
#  define _CACHE v3
# endif
#endif

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

#if !defined(_CACHE) && !defined(MULTI_CACHE)
#error Unknown cache maintenance model
#endif

#ifndef MULTI_CACHE
#define __cpuc_flush_icache_all		__glue(_CACHE,_flush_icache_all)
#define __cpuc_flush_kern_all		__glue(_CACHE,_flush_kern_cache_all)
#define __cpuc_flush_user_all		__glue(_CACHE,_flush_user_cache_all)
#define __cpuc_flush_user_range		__glue(_CACHE,_flush_user_cache_range)
#define __cpuc_coherent_kern_range	__glue(_CACHE,_coherent_kern_range)
#define __cpuc_coherent_user_range	__glue(_CACHE,_coherent_user_range)
#define __cpuc_flush_dcache_area	__glue(_CACHE,_flush_kern_dcache_area)

#define dmac_map_area			__glue(_CACHE,_dma_map_area)
#define dmac_unmap_area			__glue(_CACHE,_dma_unmap_area)
#define dmac_flush_range		__glue(_CACHE,_dma_flush_range)
#endif

#endif
