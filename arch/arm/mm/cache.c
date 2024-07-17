// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file defines C prototypes for the low-level cache assembly functions
 * and populates a vtable for each selected ARM CPU cache type.
 */

#include <linux/types.h>
#include <asm/cacheflush.h>

#ifdef CONFIG_CPU_CACHE_V4
void v4_flush_icache_all(void);
void v4_flush_kern_cache_all(void);
void v4_flush_user_cache_all(void);
void v4_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void v4_coherent_kern_range(unsigned long, unsigned long);
int v4_coherent_user_range(unsigned long, unsigned long);
void v4_flush_kern_dcache_area(void *, size_t);
void v4_dma_map_area(const void *, size_t, int);
void v4_dma_unmap_area(const void *, size_t, int);
void v4_dma_flush_range(const void *, const void *);

struct cpu_cache_fns v4_cache_fns __initconst = {
	.flush_icache_all = v4_flush_icache_all,
	.flush_kern_all = v4_flush_kern_cache_all,
	.flush_kern_louis = v4_flush_kern_cache_all,
	.flush_user_all = v4_flush_user_cache_all,
	.flush_user_range = v4_flush_user_cache_range,
	.coherent_kern_range = v4_coherent_kern_range,
	.coherent_user_range = v4_coherent_user_range,
	.flush_kern_dcache_area = v4_flush_kern_dcache_area,
	.dma_map_area = v4_dma_map_area,
	.dma_unmap_area = v4_dma_unmap_area,
	.dma_flush_range = v4_dma_flush_range,
};
#endif

/* V4 write-back cache "V4WB" */
#ifdef CONFIG_CPU_CACHE_V4WB
void v4wb_flush_icache_all(void);
void v4wb_flush_kern_cache_all(void);
void v4wb_flush_user_cache_all(void);
void v4wb_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void v4wb_coherent_kern_range(unsigned long, unsigned long);
int v4wb_coherent_user_range(unsigned long, unsigned long);
void v4wb_flush_kern_dcache_area(void *, size_t);
void v4wb_dma_map_area(const void *, size_t, int);
void v4wb_dma_unmap_area(const void *, size_t, int);
void v4wb_dma_flush_range(const void *, const void *);

struct cpu_cache_fns v4wb_cache_fns __initconst = {
	.flush_icache_all = v4wb_flush_icache_all,
	.flush_kern_all = v4wb_flush_kern_cache_all,
	.flush_kern_louis = v4wb_flush_kern_cache_all,
	.flush_user_all = v4wb_flush_user_cache_all,
	.flush_user_range = v4wb_flush_user_cache_range,
	.coherent_kern_range = v4wb_coherent_kern_range,
	.coherent_user_range = v4wb_coherent_user_range,
	.flush_kern_dcache_area = v4wb_flush_kern_dcache_area,
	.dma_map_area = v4wb_dma_map_area,
	.dma_unmap_area = v4wb_dma_unmap_area,
	.dma_flush_range = v4wb_dma_flush_range,
};
#endif

/* V4 write-through cache "V4WT" */
#ifdef CONFIG_CPU_CACHE_V4WT
void v4wt_flush_icache_all(void);
void v4wt_flush_kern_cache_all(void);
void v4wt_flush_user_cache_all(void);
void v4wt_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void v4wt_coherent_kern_range(unsigned long, unsigned long);
int v4wt_coherent_user_range(unsigned long, unsigned long);
void v4wt_flush_kern_dcache_area(void *, size_t);
void v4wt_dma_map_area(const void *, size_t, int);
void v4wt_dma_unmap_area(const void *, size_t, int);
void v4wt_dma_flush_range(const void *, const void *);

struct cpu_cache_fns v4wt_cache_fns __initconst = {
	.flush_icache_all = v4wt_flush_icache_all,
	.flush_kern_all = v4wt_flush_kern_cache_all,
	.flush_kern_louis = v4wt_flush_kern_cache_all,
	.flush_user_all = v4wt_flush_user_cache_all,
	.flush_user_range = v4wt_flush_user_cache_range,
	.coherent_kern_range = v4wt_coherent_kern_range,
	.coherent_user_range = v4wt_coherent_user_range,
	.flush_kern_dcache_area = v4wt_flush_kern_dcache_area,
	.dma_map_area = v4wt_dma_map_area,
	.dma_unmap_area = v4wt_dma_unmap_area,
	.dma_flush_range = v4wt_dma_flush_range,
};
#endif

/* Faraday FA526 cache */
#ifdef CONFIG_CPU_CACHE_FA
void fa_flush_icache_all(void);
void fa_flush_kern_cache_all(void);
void fa_flush_user_cache_all(void);
void fa_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void fa_coherent_kern_range(unsigned long, unsigned long);
int fa_coherent_user_range(unsigned long, unsigned long);
void fa_flush_kern_dcache_area(void *, size_t);
void fa_dma_map_area(const void *, size_t, int);
void fa_dma_unmap_area(const void *, size_t, int);
void fa_dma_flush_range(const void *, const void *);

struct cpu_cache_fns fa_cache_fns __initconst = {
	.flush_icache_all = fa_flush_icache_all,
	.flush_kern_all = fa_flush_kern_cache_all,
	.flush_kern_louis = fa_flush_kern_cache_all,
	.flush_user_all = fa_flush_user_cache_all,
	.flush_user_range = fa_flush_user_cache_range,
	.coherent_kern_range = fa_coherent_kern_range,
	.coherent_user_range = fa_coherent_user_range,
	.flush_kern_dcache_area = fa_flush_kern_dcache_area,
	.dma_map_area = fa_dma_map_area,
	.dma_unmap_area = fa_dma_unmap_area,
	.dma_flush_range = fa_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_CACHE_V6
void v6_flush_icache_all(void);
void v6_flush_kern_cache_all(void);
void v6_flush_user_cache_all(void);
void v6_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void v6_coherent_kern_range(unsigned long, unsigned long);
int v6_coherent_user_range(unsigned long, unsigned long);
void v6_flush_kern_dcache_area(void *, size_t);
void v6_dma_map_area(const void *, size_t, int);
void v6_dma_unmap_area(const void *, size_t, int);
void v6_dma_flush_range(const void *, const void *);

struct cpu_cache_fns v6_cache_fns __initconst = {
	.flush_icache_all = v6_flush_icache_all,
	.flush_kern_all = v6_flush_kern_cache_all,
	.flush_kern_louis = v6_flush_kern_cache_all,
	.flush_user_all = v6_flush_user_cache_all,
	.flush_user_range = v6_flush_user_cache_range,
	.coherent_kern_range = v6_coherent_kern_range,
	.coherent_user_range = v6_coherent_user_range,
	.flush_kern_dcache_area = v6_flush_kern_dcache_area,
	.dma_map_area = v6_dma_map_area,
	.dma_unmap_area = v6_dma_unmap_area,
	.dma_flush_range = v6_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_CACHE_V7
void v7_flush_icache_all(void);
void v7_flush_kern_cache_all(void);
void v7_flush_kern_cache_louis(void);
void v7_flush_user_cache_all(void);
void v7_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void v7_coherent_kern_range(unsigned long, unsigned long);
int v7_coherent_user_range(unsigned long, unsigned long);
void v7_flush_kern_dcache_area(void *, size_t);
void v7_dma_map_area(const void *, size_t, int);
void v7_dma_unmap_area(const void *, size_t, int);
void v7_dma_flush_range(const void *, const void *);

struct cpu_cache_fns v7_cache_fns __initconst = {
	.flush_icache_all = v7_flush_icache_all,
	.flush_kern_all = v7_flush_kern_cache_all,
	.flush_kern_louis = v7_flush_kern_cache_louis,
	.flush_user_all = v7_flush_user_cache_all,
	.flush_user_range = v7_flush_user_cache_range,
	.coherent_kern_range = v7_coherent_kern_range,
	.coherent_user_range = v7_coherent_user_range,
	.flush_kern_dcache_area = v7_flush_kern_dcache_area,
	.dma_map_area = v7_dma_map_area,
	.dma_unmap_area = v7_dma_unmap_area,
	.dma_flush_range = v7_dma_flush_range,
};

/* Special quirky cache flush function for Broadcom B15 v7 caches */
void b15_flush_kern_cache_all(void);

struct cpu_cache_fns b15_cache_fns __initconst = {
	.flush_icache_all = v7_flush_icache_all,
#ifdef CONFIG_CACHE_B15_RAC
	.flush_kern_all = b15_flush_kern_cache_all,
#else
	.flush_kern_all = v7_flush_kern_cache_all,
#endif
	.flush_kern_louis = v7_flush_kern_cache_louis,
	.flush_user_all = v7_flush_user_cache_all,
	.flush_user_range = v7_flush_user_cache_range,
	.coherent_kern_range = v7_coherent_kern_range,
	.coherent_user_range = v7_coherent_user_range,
	.flush_kern_dcache_area = v7_flush_kern_dcache_area,
	.dma_map_area = v7_dma_map_area,
	.dma_unmap_area = v7_dma_unmap_area,
	.dma_flush_range = v7_dma_flush_range,
};
#endif

/* The NOP cache is just a set of dummy stubs that by definition does nothing */
#ifdef CONFIG_CPU_CACHE_NOP
void nop_flush_icache_all(void);
void nop_flush_kern_cache_all(void);
void nop_flush_user_cache_all(void);
void nop_flush_user_cache_range(unsigned long start, unsigned long end, unsigned int flags);
void nop_coherent_kern_range(unsigned long start, unsigned long end);
int nop_coherent_user_range(unsigned long, unsigned long);
void nop_flush_kern_dcache_area(void *kaddr, size_t size);
void nop_dma_map_area(const void *start, size_t size, int flags);
void nop_dma_unmap_area(const void *start, size_t size, int flags);
void nop_dma_flush_range(const void *start, const void *end);

struct cpu_cache_fns nop_cache_fns __initconst = {
	.flush_icache_all = nop_flush_icache_all,
	.flush_kern_all = nop_flush_kern_cache_all,
	.flush_kern_louis = nop_flush_kern_cache_all,
	.flush_user_all = nop_flush_user_cache_all,
	.flush_user_range = nop_flush_user_cache_range,
	.coherent_kern_range = nop_coherent_kern_range,
	.coherent_user_range = nop_coherent_user_range,
	.flush_kern_dcache_area = nop_flush_kern_dcache_area,
	.dma_map_area = nop_dma_map_area,
	.dma_unmap_area = nop_dma_unmap_area,
	.dma_flush_range = nop_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_CACHE_V7M
void v7m_flush_icache_all(void);
void v7m_flush_kern_cache_all(void);
void v7m_flush_user_cache_all(void);
void v7m_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void v7m_coherent_kern_range(unsigned long, unsigned long);
int v7m_coherent_user_range(unsigned long, unsigned long);
void v7m_flush_kern_dcache_area(void *, size_t);
void v7m_dma_map_area(const void *, size_t, int);
void v7m_dma_unmap_area(const void *, size_t, int);
void v7m_dma_flush_range(const void *, const void *);

struct cpu_cache_fns v7m_cache_fns __initconst = {
	.flush_icache_all = v7m_flush_icache_all,
	.flush_kern_all = v7m_flush_kern_cache_all,
	.flush_kern_louis = v7m_flush_kern_cache_all,
	.flush_user_all = v7m_flush_user_cache_all,
	.flush_user_range = v7m_flush_user_cache_range,
	.coherent_kern_range = v7m_coherent_kern_range,
	.coherent_user_range = v7m_coherent_user_range,
	.flush_kern_dcache_area = v7m_flush_kern_dcache_area,
	.dma_map_area = v7m_dma_map_area,
	.dma_unmap_area = v7m_dma_unmap_area,
	.dma_flush_range = v7m_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_ARM1020
void arm1020_flush_icache_all(void);
void arm1020_flush_kern_cache_all(void);
void arm1020_flush_user_cache_all(void);
void arm1020_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void arm1020_coherent_kern_range(unsigned long, unsigned long);
int arm1020_coherent_user_range(unsigned long, unsigned long);
void arm1020_flush_kern_dcache_area(void *, size_t);
void arm1020_dma_map_area(const void *, size_t, int);
void arm1020_dma_unmap_area(const void *, size_t, int);
void arm1020_dma_flush_range(const void *, const void *);

struct cpu_cache_fns arm1020_cache_fns __initconst = {
	.flush_icache_all = arm1020_flush_icache_all,
	.flush_kern_all = arm1020_flush_kern_cache_all,
	.flush_kern_louis = arm1020_flush_kern_cache_all,
	.flush_user_all = arm1020_flush_user_cache_all,
	.flush_user_range = arm1020_flush_user_cache_range,
	.coherent_kern_range = arm1020_coherent_kern_range,
	.coherent_user_range = arm1020_coherent_user_range,
	.flush_kern_dcache_area = arm1020_flush_kern_dcache_area,
	.dma_map_area = arm1020_dma_map_area,
	.dma_unmap_area = arm1020_dma_unmap_area,
	.dma_flush_range = arm1020_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_ARM1020E
void arm1020e_flush_icache_all(void);
void arm1020e_flush_kern_cache_all(void);
void arm1020e_flush_user_cache_all(void);
void arm1020e_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void arm1020e_coherent_kern_range(unsigned long, unsigned long);
int arm1020e_coherent_user_range(unsigned long, unsigned long);
void arm1020e_flush_kern_dcache_area(void *, size_t);
void arm1020e_dma_map_area(const void *, size_t, int);
void arm1020e_dma_unmap_area(const void *, size_t, int);
void arm1020e_dma_flush_range(const void *, const void *);

struct cpu_cache_fns arm1020e_cache_fns __initconst = {
	.flush_icache_all = arm1020e_flush_icache_all,
	.flush_kern_all = arm1020e_flush_kern_cache_all,
	.flush_kern_louis = arm1020e_flush_kern_cache_all,
	.flush_user_all = arm1020e_flush_user_cache_all,
	.flush_user_range = arm1020e_flush_user_cache_range,
	.coherent_kern_range = arm1020e_coherent_kern_range,
	.coherent_user_range = arm1020e_coherent_user_range,
	.flush_kern_dcache_area = arm1020e_flush_kern_dcache_area,
	.dma_map_area = arm1020e_dma_map_area,
	.dma_unmap_area = arm1020e_dma_unmap_area,
	.dma_flush_range = arm1020e_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_ARM1022
void arm1022_flush_icache_all(void);
void arm1022_flush_kern_cache_all(void);
void arm1022_flush_user_cache_all(void);
void arm1022_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void arm1022_coherent_kern_range(unsigned long, unsigned long);
int arm1022_coherent_user_range(unsigned long, unsigned long);
void arm1022_flush_kern_dcache_area(void *, size_t);
void arm1022_dma_map_area(const void *, size_t, int);
void arm1022_dma_unmap_area(const void *, size_t, int);
void arm1022_dma_flush_range(const void *, const void *);

struct cpu_cache_fns arm1022_cache_fns __initconst = {
	.flush_icache_all = arm1022_flush_icache_all,
	.flush_kern_all = arm1022_flush_kern_cache_all,
	.flush_kern_louis = arm1022_flush_kern_cache_all,
	.flush_user_all = arm1022_flush_user_cache_all,
	.flush_user_range = arm1022_flush_user_cache_range,
	.coherent_kern_range = arm1022_coherent_kern_range,
	.coherent_user_range = arm1022_coherent_user_range,
	.flush_kern_dcache_area = arm1022_flush_kern_dcache_area,
	.dma_map_area = arm1022_dma_map_area,
	.dma_unmap_area = arm1022_dma_unmap_area,
	.dma_flush_range = arm1022_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_ARM1026
void arm1026_flush_icache_all(void);
void arm1026_flush_kern_cache_all(void);
void arm1026_flush_user_cache_all(void);
void arm1026_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void arm1026_coherent_kern_range(unsigned long, unsigned long);
int arm1026_coherent_user_range(unsigned long, unsigned long);
void arm1026_flush_kern_dcache_area(void *, size_t);
void arm1026_dma_map_area(const void *, size_t, int);
void arm1026_dma_unmap_area(const void *, size_t, int);
void arm1026_dma_flush_range(const void *, const void *);

struct cpu_cache_fns arm1026_cache_fns __initconst = {
	.flush_icache_all = arm1026_flush_icache_all,
	.flush_kern_all = arm1026_flush_kern_cache_all,
	.flush_kern_louis = arm1026_flush_kern_cache_all,
	.flush_user_all = arm1026_flush_user_cache_all,
	.flush_user_range = arm1026_flush_user_cache_range,
	.coherent_kern_range = arm1026_coherent_kern_range,
	.coherent_user_range = arm1026_coherent_user_range,
	.flush_kern_dcache_area = arm1026_flush_kern_dcache_area,
	.dma_map_area = arm1026_dma_map_area,
	.dma_unmap_area = arm1026_dma_unmap_area,
	.dma_flush_range = arm1026_dma_flush_range,
};
#endif

#if defined(CONFIG_CPU_ARM920T) && !defined(CONFIG_CPU_DCACHE_WRITETHROUGH)
void arm920_flush_icache_all(void);
void arm920_flush_kern_cache_all(void);
void arm920_flush_user_cache_all(void);
void arm920_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void arm920_coherent_kern_range(unsigned long, unsigned long);
int arm920_coherent_user_range(unsigned long, unsigned long);
void arm920_flush_kern_dcache_area(void *, size_t);
void arm920_dma_map_area(const void *, size_t, int);
void arm920_dma_unmap_area(const void *, size_t, int);
void arm920_dma_flush_range(const void *, const void *);

struct cpu_cache_fns arm920_cache_fns __initconst = {
	.flush_icache_all = arm920_flush_icache_all,
	.flush_kern_all = arm920_flush_kern_cache_all,
	.flush_kern_louis = arm920_flush_kern_cache_all,
	.flush_user_all = arm920_flush_user_cache_all,
	.flush_user_range = arm920_flush_user_cache_range,
	.coherent_kern_range = arm920_coherent_kern_range,
	.coherent_user_range = arm920_coherent_user_range,
	.flush_kern_dcache_area = arm920_flush_kern_dcache_area,
	.dma_map_area = arm920_dma_map_area,
	.dma_unmap_area = arm920_dma_unmap_area,
	.dma_flush_range = arm920_dma_flush_range,
};
#endif

#if defined(CONFIG_CPU_ARM922T) && !defined(CONFIG_CPU_DCACHE_WRITETHROUGH)
void arm922_flush_icache_all(void);
void arm922_flush_kern_cache_all(void);
void arm922_flush_user_cache_all(void);
void arm922_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void arm922_coherent_kern_range(unsigned long, unsigned long);
int arm922_coherent_user_range(unsigned long, unsigned long);
void arm922_flush_kern_dcache_area(void *, size_t);
void arm922_dma_map_area(const void *, size_t, int);
void arm922_dma_unmap_area(const void *, size_t, int);
void arm922_dma_flush_range(const void *, const void *);

struct cpu_cache_fns arm922_cache_fns __initconst = {
	.flush_icache_all = arm922_flush_icache_all,
	.flush_kern_all = arm922_flush_kern_cache_all,
	.flush_kern_louis = arm922_flush_kern_cache_all,
	.flush_user_all = arm922_flush_user_cache_all,
	.flush_user_range = arm922_flush_user_cache_range,
	.coherent_kern_range = arm922_coherent_kern_range,
	.coherent_user_range = arm922_coherent_user_range,
	.flush_kern_dcache_area = arm922_flush_kern_dcache_area,
	.dma_map_area = arm922_dma_map_area,
	.dma_unmap_area = arm922_dma_unmap_area,
	.dma_flush_range = arm922_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_ARM925T
void arm925_flush_icache_all(void);
void arm925_flush_kern_cache_all(void);
void arm925_flush_user_cache_all(void);
void arm925_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void arm925_coherent_kern_range(unsigned long, unsigned long);
int arm925_coherent_user_range(unsigned long, unsigned long);
void arm925_flush_kern_dcache_area(void *, size_t);
void arm925_dma_map_area(const void *, size_t, int);
void arm925_dma_unmap_area(const void *, size_t, int);
void arm925_dma_flush_range(const void *, const void *);

struct cpu_cache_fns arm925_cache_fns __initconst = {
	.flush_icache_all = arm925_flush_icache_all,
	.flush_kern_all = arm925_flush_kern_cache_all,
	.flush_kern_louis = arm925_flush_kern_cache_all,
	.flush_user_all = arm925_flush_user_cache_all,
	.flush_user_range = arm925_flush_user_cache_range,
	.coherent_kern_range = arm925_coherent_kern_range,
	.coherent_user_range = arm925_coherent_user_range,
	.flush_kern_dcache_area = arm925_flush_kern_dcache_area,
	.dma_map_area = arm925_dma_map_area,
	.dma_unmap_area = arm925_dma_unmap_area,
	.dma_flush_range = arm925_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_ARM926T
void arm926_flush_icache_all(void);
void arm926_flush_kern_cache_all(void);
void arm926_flush_user_cache_all(void);
void arm926_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void arm926_coherent_kern_range(unsigned long, unsigned long);
int arm926_coherent_user_range(unsigned long, unsigned long);
void arm926_flush_kern_dcache_area(void *, size_t);
void arm926_dma_map_area(const void *, size_t, int);
void arm926_dma_unmap_area(const void *, size_t, int);
void arm926_dma_flush_range(const void *, const void *);

struct cpu_cache_fns arm926_cache_fns __initconst = {
	.flush_icache_all = arm926_flush_icache_all,
	.flush_kern_all = arm926_flush_kern_cache_all,
	.flush_kern_louis = arm926_flush_kern_cache_all,
	.flush_user_all = arm926_flush_user_cache_all,
	.flush_user_range = arm926_flush_user_cache_range,
	.coherent_kern_range = arm926_coherent_kern_range,
	.coherent_user_range = arm926_coherent_user_range,
	.flush_kern_dcache_area = arm926_flush_kern_dcache_area,
	.dma_map_area = arm926_dma_map_area,
	.dma_unmap_area = arm926_dma_unmap_area,
	.dma_flush_range = arm926_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_ARM940T
void arm940_flush_icache_all(void);
void arm940_flush_kern_cache_all(void);
void arm940_flush_user_cache_all(void);
void arm940_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void arm940_coherent_kern_range(unsigned long, unsigned long);
int arm940_coherent_user_range(unsigned long, unsigned long);
void arm940_flush_kern_dcache_area(void *, size_t);
void arm940_dma_map_area(const void *, size_t, int);
void arm940_dma_unmap_area(const void *, size_t, int);
void arm940_dma_flush_range(const void *, const void *);

struct cpu_cache_fns arm940_cache_fns __initconst = {
	.flush_icache_all = arm940_flush_icache_all,
	.flush_kern_all = arm940_flush_kern_cache_all,
	.flush_kern_louis = arm940_flush_kern_cache_all,
	.flush_user_all = arm940_flush_user_cache_all,
	.flush_user_range = arm940_flush_user_cache_range,
	.coherent_kern_range = arm940_coherent_kern_range,
	.coherent_user_range = arm940_coherent_user_range,
	.flush_kern_dcache_area = arm940_flush_kern_dcache_area,
	.dma_map_area = arm940_dma_map_area,
	.dma_unmap_area = arm940_dma_unmap_area,
	.dma_flush_range = arm940_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_ARM946E
void arm946_flush_icache_all(void);
void arm946_flush_kern_cache_all(void);
void arm946_flush_user_cache_all(void);
void arm946_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void arm946_coherent_kern_range(unsigned long, unsigned long);
int arm946_coherent_user_range(unsigned long, unsigned long);
void arm946_flush_kern_dcache_area(void *, size_t);
void arm946_dma_map_area(const void *, size_t, int);
void arm946_dma_unmap_area(const void *, size_t, int);
void arm946_dma_flush_range(const void *, const void *);

struct cpu_cache_fns arm946_cache_fns __initconst = {
	.flush_icache_all = arm946_flush_icache_all,
	.flush_kern_all = arm946_flush_kern_cache_all,
	.flush_kern_louis = arm946_flush_kern_cache_all,
	.flush_user_all = arm946_flush_user_cache_all,
	.flush_user_range = arm946_flush_user_cache_range,
	.coherent_kern_range = arm946_coherent_kern_range,
	.coherent_user_range = arm946_coherent_user_range,
	.flush_kern_dcache_area = arm946_flush_kern_dcache_area,
	.dma_map_area = arm946_dma_map_area,
	.dma_unmap_area = arm946_dma_unmap_area,
	.dma_flush_range = arm946_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_XSCALE
void xscale_flush_icache_all(void);
void xscale_flush_kern_cache_all(void);
void xscale_flush_user_cache_all(void);
void xscale_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void xscale_coherent_kern_range(unsigned long, unsigned long);
int xscale_coherent_user_range(unsigned long, unsigned long);
void xscale_flush_kern_dcache_area(void *, size_t);
void xscale_dma_map_area(const void *, size_t, int);
void xscale_dma_unmap_area(const void *, size_t, int);
void xscale_dma_flush_range(const void *, const void *);

struct cpu_cache_fns xscale_cache_fns __initconst = {
	.flush_icache_all = xscale_flush_icache_all,
	.flush_kern_all = xscale_flush_kern_cache_all,
	.flush_kern_louis = xscale_flush_kern_cache_all,
	.flush_user_all = xscale_flush_user_cache_all,
	.flush_user_range = xscale_flush_user_cache_range,
	.coherent_kern_range = xscale_coherent_kern_range,
	.coherent_user_range = xscale_coherent_user_range,
	.flush_kern_dcache_area = xscale_flush_kern_dcache_area,
	.dma_map_area = xscale_dma_map_area,
	.dma_unmap_area = xscale_dma_unmap_area,
	.dma_flush_range = xscale_dma_flush_range,
};

/* The 80200 A0 and A1 need a special quirk for dma_map_area() */
void xscale_80200_A0_A1_dma_map_area(const void *, size_t, int);

struct cpu_cache_fns xscale_80200_A0_A1_cache_fns __initconst = {
	.flush_icache_all = xscale_flush_icache_all,
	.flush_kern_all = xscale_flush_kern_cache_all,
	.flush_kern_louis = xscale_flush_kern_cache_all,
	.flush_user_all = xscale_flush_user_cache_all,
	.flush_user_range = xscale_flush_user_cache_range,
	.coherent_kern_range = xscale_coherent_kern_range,
	.coherent_user_range = xscale_coherent_user_range,
	.flush_kern_dcache_area = xscale_flush_kern_dcache_area,
	.dma_map_area = xscale_80200_A0_A1_dma_map_area,
	.dma_unmap_area = xscale_dma_unmap_area,
	.dma_flush_range = xscale_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_XSC3
void xsc3_flush_icache_all(void);
void xsc3_flush_kern_cache_all(void);
void xsc3_flush_user_cache_all(void);
void xsc3_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void xsc3_coherent_kern_range(unsigned long, unsigned long);
int xsc3_coherent_user_range(unsigned long, unsigned long);
void xsc3_flush_kern_dcache_area(void *, size_t);
void xsc3_dma_map_area(const void *, size_t, int);
void xsc3_dma_unmap_area(const void *, size_t, int);
void xsc3_dma_flush_range(const void *, const void *);

struct cpu_cache_fns xsc3_cache_fns __initconst = {
	.flush_icache_all = xsc3_flush_icache_all,
	.flush_kern_all = xsc3_flush_kern_cache_all,
	.flush_kern_louis = xsc3_flush_kern_cache_all,
	.flush_user_all = xsc3_flush_user_cache_all,
	.flush_user_range = xsc3_flush_user_cache_range,
	.coherent_kern_range = xsc3_coherent_kern_range,
	.coherent_user_range = xsc3_coherent_user_range,
	.flush_kern_dcache_area = xsc3_flush_kern_dcache_area,
	.dma_map_area = xsc3_dma_map_area,
	.dma_unmap_area = xsc3_dma_unmap_area,
	.dma_flush_range = xsc3_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_MOHAWK
void mohawk_flush_icache_all(void);
void mohawk_flush_kern_cache_all(void);
void mohawk_flush_user_cache_all(void);
void mohawk_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void mohawk_coherent_kern_range(unsigned long, unsigned long);
int mohawk_coherent_user_range(unsigned long, unsigned long);
void mohawk_flush_kern_dcache_area(void *, size_t);
void mohawk_dma_map_area(const void *, size_t, int);
void mohawk_dma_unmap_area(const void *, size_t, int);
void mohawk_dma_flush_range(const void *, const void *);

struct cpu_cache_fns mohawk_cache_fns __initconst = {
	.flush_icache_all = mohawk_flush_icache_all,
	.flush_kern_all = mohawk_flush_kern_cache_all,
	.flush_kern_louis = mohawk_flush_kern_cache_all,
	.flush_user_all = mohawk_flush_user_cache_all,
	.flush_user_range = mohawk_flush_user_cache_range,
	.coherent_kern_range = mohawk_coherent_kern_range,
	.coherent_user_range = mohawk_coherent_user_range,
	.flush_kern_dcache_area = mohawk_flush_kern_dcache_area,
	.dma_map_area = mohawk_dma_map_area,
	.dma_unmap_area = mohawk_dma_unmap_area,
	.dma_flush_range = mohawk_dma_flush_range,
};
#endif

#ifdef CONFIG_CPU_FEROCEON
void feroceon_flush_icache_all(void);
void feroceon_flush_kern_cache_all(void);
void feroceon_flush_user_cache_all(void);
void feroceon_flush_user_cache_range(unsigned long, unsigned long, unsigned int);
void feroceon_coherent_kern_range(unsigned long, unsigned long);
int feroceon_coherent_user_range(unsigned long, unsigned long);
void feroceon_flush_kern_dcache_area(void *, size_t);
void feroceon_dma_map_area(const void *, size_t, int);
void feroceon_dma_unmap_area(const void *, size_t, int);
void feroceon_dma_flush_range(const void *, const void *);

struct cpu_cache_fns feroceon_cache_fns __initconst = {
	.flush_icache_all = feroceon_flush_icache_all,
	.flush_kern_all = feroceon_flush_kern_cache_all,
	.flush_kern_louis = feroceon_flush_kern_cache_all,
	.flush_user_all = feroceon_flush_user_cache_all,
	.flush_user_range = feroceon_flush_user_cache_range,
	.coherent_kern_range = feroceon_coherent_kern_range,
	.coherent_user_range = feroceon_coherent_user_range,
	.flush_kern_dcache_area = feroceon_flush_kern_dcache_area,
	.dma_map_area = feroceon_dma_map_area,
	.dma_unmap_area = feroceon_dma_unmap_area,
	.dma_flush_range = feroceon_dma_flush_range,
};

void feroceon_range_flush_kern_dcache_area(void *, size_t);
void feroceon_range_dma_map_area(const void *, size_t, int);
void feroceon_range_dma_flush_range(const void *, const void *);

struct cpu_cache_fns feroceon_range_cache_fns __initconst = {
	.flush_icache_all = feroceon_flush_icache_all,
	.flush_kern_all = feroceon_flush_kern_cache_all,
	.flush_kern_louis = feroceon_flush_kern_cache_all,
	.flush_user_all = feroceon_flush_user_cache_all,
	.flush_user_range = feroceon_flush_user_cache_range,
	.coherent_kern_range = feroceon_coherent_kern_range,
	.coherent_user_range = feroceon_coherent_user_range,
	.flush_kern_dcache_area = feroceon_range_flush_kern_dcache_area,
	.dma_map_area = feroceon_range_dma_map_area,
	.dma_unmap_area = feroceon_dma_unmap_area,
	.dma_flush_range = feroceon_range_dma_flush_range,
};
#endif
