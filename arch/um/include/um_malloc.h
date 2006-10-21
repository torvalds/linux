/*
 * Copyright (C) 2005 Paolo 'Blaisorblade' Giarrusso <blaisorblade@yahoo.it>
 * Licensed under the GPL
 */

#ifndef __UM_MALLOC_H__
#define __UM_MALLOC_H__

extern void *um_kmalloc(int size);
extern void *um_kmalloc_atomic(int size);
extern void kfree(const void *ptr);

extern void *um_vmalloc(int size);
extern void *um_vmalloc_atomic(int size);
extern void vfree(void *ptr);

#endif /* __UM_MALLOC_H__ */
