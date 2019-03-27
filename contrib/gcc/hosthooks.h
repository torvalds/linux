/* The host_hooks data structure.
   Copyright 2003, 2004 Free Software Foundation, Inc.

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

#ifndef GCC_HOST_HOOKS_H
#define GCC_HOST_HOOKS_H

struct host_hooks
{
  void (*extra_signals) (void);

  /* Identify an address that's likely to be free in a subsequent invocation
     of the compiler.  The area should be able to hold SIZE bytes.  FD is an
     open file descriptor if the host would like to probe with mmap.  */
  void * (*gt_pch_get_address) (size_t size, int fd);

  /* ADDR is an address returned by gt_pch_get_address.  Attempt to allocate
     SIZE bytes at the same address and load it with the data from FD at 
     OFFSET.  Return -1 if we couldn't allocate memory at ADDR, return 0
     if the memory is allocated but the data not loaded, return 1 if done.  */
  int (*gt_pch_use_address) (void *addr, size_t size, int fd, size_t offset);

  /*  Return the alignment required for allocating virtual memory. Usually
      this is the same as pagesize.  */
  size_t (*gt_pch_alloc_granularity) (void);

  /* Whenever you add entries here, make sure you adjust hosthooks-def.h.  */
};

/* Each host provides its own.  */
extern const struct host_hooks host_hooks;

#endif /* GCC_LANG_HOOKS_H */
