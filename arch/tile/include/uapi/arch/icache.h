/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 */

/**
 * @file
 *
 * Support for invalidating bytes in the instruction cache.
 */

#ifndef __ARCH_ICACHE_H__
#define __ARCH_ICACHE_H__

#include <arch/chip.h>


/**
 * Invalidate the instruction cache for the given range of memory.
 *
 * @param addr The start of memory to be invalidated.
 * @param size The number of bytes to be invalidated.
 * @param page_size The system's page size, e.g. getpagesize() in userspace.
 * This value must be a power of two no larger than the page containing
 * the code to be invalidated. If the value is smaller than the actual page
 * size, this function will still work, but may run slower than necessary.
 */
static __inline void
invalidate_icache(const void* addr, unsigned long size,
                  unsigned long page_size)
{
  const unsigned long cache_way_size =
    CHIP_L1I_CACHE_SIZE() / CHIP_L1I_ASSOC();
  unsigned long max_useful_size;
  const char* start, *end;
  long num_passes;

  if (__builtin_expect(size == 0, 0))
    return;

#ifdef __tilegx__
  /* Limit the number of bytes visited to avoid redundant iterations. */
  max_useful_size = (page_size < cache_way_size) ? page_size : cache_way_size;

  /* No PA aliasing is possible, so one pass always suffices. */
  num_passes = 1;
#else
  /* Limit the number of bytes visited to avoid redundant iterations. */
  max_useful_size = cache_way_size;

  /*
   * Compute how many passes we need (we'll treat 0 as if it were 1).
   * This works because we know the page size is a power of two.
   */
  num_passes = cache_way_size >> __builtin_ctzl(page_size);
#endif

  if (__builtin_expect(size > max_useful_size, 0))
    size = max_useful_size;

  /* Locate the first and last bytes to be invalidated. */
  start = (const char *)((unsigned long)addr & -CHIP_L1I_LINE_SIZE());
  end = (const char*)addr + size - 1;

  __insn_mf();

  do
  {
    const char* p;

    for (p = start; p <= end; p += CHIP_L1I_LINE_SIZE())
      __insn_icoh(p);

    start += page_size;
    end += page_size;
  }
  while (--num_passes > 0);

  __insn_drain();
}


#endif /* __ARCH_ICACHE_H__ */
