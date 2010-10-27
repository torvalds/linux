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
 * The hypervisor's memory controller profiling infrastructure allows
 * the programmer to find out what fraction of the available memory
 * bandwidth is being consumed at each memory controller.  The
 * profiler provides start, stop, and clear operations to allows
 * profiling over a specific time window, as well as an interface for
 * reading the most recent profile values.
 *
 * This header declares IOCTL codes necessary to control memprof.
 */
#ifndef _ASM_TILE_MEMPROF_H
#define _ASM_TILE_MEMPROF_H

#include <linux/ioctl.h>

#define MEMPROF_IOCTL_TYPE 0xB4
#define MEMPROF_IOCTL_START _IO(MEMPROF_IOCTL_TYPE, 0)
#define MEMPROF_IOCTL_STOP _IO(MEMPROF_IOCTL_TYPE, 1)
#define MEMPROF_IOCTL_CLEAR _IO(MEMPROF_IOCTL_TYPE, 2)

#endif /* _ASM_TILE_MEMPROF_H */
