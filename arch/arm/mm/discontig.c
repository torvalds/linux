/*
 * linux/arch/arm/mm/discontig.c
 *
 * Discontiguous memory support.
 *
 * Initial code: Copyright (C) 1999-2000 Nicolas Pitre
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/mmzone.h>
#include <linux/bootmem.h>

#if MAX_NUMNODES != 4 && MAX_NUMNODES != 16
# error Fix Me Please
#endif

/*
 * Our node_data structure for discontiguous memory.
 */

static bootmem_data_t node_bootmem_data[MAX_NUMNODES];

pg_data_t discontig_node_data[MAX_NUMNODES] = {
  { .bdata = &node_bootmem_data[0] },
  { .bdata = &node_bootmem_data[1] },
  { .bdata = &node_bootmem_data[2] },
  { .bdata = &node_bootmem_data[3] },
#if MAX_NUMNODES == 16
  { .bdata = &node_bootmem_data[4] },
  { .bdata = &node_bootmem_data[5] },
  { .bdata = &node_bootmem_data[6] },
  { .bdata = &node_bootmem_data[7] },
  { .bdata = &node_bootmem_data[8] },
  { .bdata = &node_bootmem_data[9] },
  { .bdata = &node_bootmem_data[10] },
  { .bdata = &node_bootmem_data[11] },
  { .bdata = &node_bootmem_data[12] },
  { .bdata = &node_bootmem_data[13] },
  { .bdata = &node_bootmem_data[14] },
  { .bdata = &node_bootmem_data[15] },
#endif
};

EXPORT_SYMBOL(discontig_node_data);
