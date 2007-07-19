/*
 * Copyright (C) 2005 Paolo 'Blaisorblade' Giarrusso <blaisorblade@yahoo.it>
 * Licensed under the GPL
 */

#ifndef __UM_MALLOC_H__
#define __UM_MALLOC_H__

#include "kern_constants.h"

extern void *__kmalloc(int size, int flags);
static inline void *kmalloc(int size, int flags)
{
	return __kmalloc(size, flags);
}

extern void kfree(const void *ptr);

extern void *vmalloc(unsigned long size);
extern void vfree(void *ptr);

#endif /* __UM_MALLOC_H__ */
