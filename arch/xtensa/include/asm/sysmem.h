/*
 * sysmem-related prototypes.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2014 Cadence Design Systems Inc.
 */

#ifndef _XTENSA_SYSMEM_H
#define _XTENSA_SYSMEM_H

#include <linux/memblock.h>

void bootmem_init(void);
void zones_init(void);

#endif /* _XTENSA_SYSMEM_H */
