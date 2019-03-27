/* Cygwin host-specific hook definitions.
 Copyright (C) 2004 Free Software Foundation, Inc.

 This file is part of GCC.

 GCC is free software; you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published
 by the Free Software Foundation; either version 2, or (at your
 option) any later version.

 GCC is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
 License for more details.

 You should have received a copy of the GNU General Public License
 along with GCC; see the file COPYING. If not, write to the
 Free Software Foundation, 51 Franklin Street, Fifth Floor, Boston,
 MA 02110-1301, USA. */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include <sys/mman.h>
#include "hosthooks.h"
#include "hosthooks-def.h"
#include "toplev.h"
#include "diagnostic.h"

static void * cygwin_gt_pch_get_address (size_t, int fd);
static size_t cygwin_gt_pch_alloc_granularity (void);

#undef HOST_HOOKS_GT_PCH_GET_ADDRESS
#define HOST_HOOKS_GT_PCH_GET_ADDRESS cygwin_gt_pch_get_address
#undef HOST_HOOKS_GT_PCH_ALLOC_GRANULARITY
#define HOST_HOOKS_GT_PCH_ALLOC_GRANULARITY cygwin_gt_pch_alloc_granularity

/* Granularity for reserving address space.  */
static const size_t va_granularity = 0x10000;

/*  Return the alignment required for allocating virtual memory. */
static size_t
cygwin_gt_pch_alloc_granularity (void)
{
  return va_granularity;
}

/* Identify an address that's likely to be free in a subsequent invocation
   of the compiler.  The area should be able to hold SIZE bytes.  FD is an
   open file descriptor if the host would like to probe with mmap.  */
static void *
cygwin_gt_pch_get_address (size_t sz, int fd)
{
  void *base;
  off_t p = lseek(fd, 0, SEEK_CUR);

  if (p == (off_t) -1)
    fatal_error ("can't get position in PCH file: %m");

   /* Cygwin requires that the underlying file be at least
      as large as the requested mapping.  */
  if ((size_t) p < sz)
  { 
    if ( ftruncate (fd, sz) == -1 )
      fatal_error ("can't extend PCH file: %m");
  }

  base = mmap (NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);

  if (base == MAP_FAILED)
    base = NULL;
  else
    munmap (base, sz);

  if (lseek (fd, p, SEEK_SET) == (off_t) -1 )
    fatal_error ("can't set position in PCH file: %m");

  return base;
}

const struct host_hooks host_hooks = HOST_HOOKS_INITIALIZER;
