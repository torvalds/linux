
/*
 * zcache/ramster.h
 *
 * Placeholder to resolve ramster references when !CONFIG_RAMSTER
 * Real ramster.h lives in ramster subdirectory.
 *
 * Copyright (c) 2009-2012, Dan Magenheimer, Oracle Corp.
 */

#ifndef _ZCACHE_RAMSTER_H_
#define _ZCACHE_RAMSTER_H_

#ifdef CONFIG_RAMSTER
#include "ramster/ramster.h"
#else
static inline void ramster_init(bool x, bool y, bool z, bool w)
{
}

static inline void ramster_register_pamops(struct tmem_pamops *p)
{
}

static inline int ramster_remotify_pageframe(bool b)
{
	return 0;
}

static inline void *ramster_pampd_free(void *v, struct tmem_pool *p,
			struct tmem_oid *o, uint32_t u, bool b)
{
	return NULL;
}

static inline int ramster_do_preload_flnode(struct tmem_pool *p)
{
	return -1;
}

static inline bool pampd_is_remote(void *v)
{
	return false;
}

static inline void ramster_count_foreign_pages(bool b, int i)
{
}

static inline void ramster_cpu_up(int cpu)
{
}

static inline void ramster_cpu_down(int cpu)
{
}
#endif

#endif /* _ZCACHE_RAMSTER_H */
