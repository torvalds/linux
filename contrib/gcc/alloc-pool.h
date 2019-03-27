/* Functions to support a pool of allocatable objects
   Copyright (C) 1997, 1998, 1999, 2000, 2001, 2003, 2004
   Free Software Foundation, Inc.
   Contributed by Daniel Berlin <dan@cgsoftware.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */
#ifndef ALLOC_POOL_H
#define ALLOC_POOL_H

typedef unsigned long ALLOC_POOL_ID_TYPE;

typedef struct alloc_pool_list_def
{
  struct alloc_pool_list_def *next;
}
 *alloc_pool_list;

typedef struct alloc_pool_def
{
  const char *name;
#ifdef ENABLE_CHECKING
  ALLOC_POOL_ID_TYPE id;
#endif
  size_t elts_per_block;
  alloc_pool_list free_list;
  size_t elts_allocated;
  size_t elts_free;
  size_t blocks_allocated;
  alloc_pool_list block_list;
  size_t block_size;
  size_t elt_size;
}
 *alloc_pool;

extern alloc_pool create_alloc_pool (const char *, size_t, size_t);
extern void free_alloc_pool (alloc_pool);
extern void free_alloc_pool_if_empty (alloc_pool *);
extern void *pool_alloc (alloc_pool);
extern void pool_free (alloc_pool, void *);
extern void dump_alloc_pool_statistics (void);
#endif
