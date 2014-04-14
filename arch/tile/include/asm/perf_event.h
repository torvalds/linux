/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
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
 */

#ifndef _ASM_TILE_PERF_EVENT_H
#define _ASM_TILE_PERF_EVENT_H

#include <linux/percpu.h>
DECLARE_PER_CPU(u64, perf_irqs);

unsigned long handle_syscall_link_address(void);
#endif /* _ASM_TILE_PERF_EVENT_H */
