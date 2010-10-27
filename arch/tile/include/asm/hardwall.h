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
 * Provide methods for the HARDWALL_FILE for accessing the UDN.
 */

#ifndef _ASM_TILE_HARDWALL_H
#define _ASM_TILE_HARDWALL_H

#include <linux/ioctl.h>

#define HARDWALL_IOCTL_BASE 0xa2

/*
 * The HARDWALL_CREATE() ioctl is a macro with a "size" argument.
 * The resulting ioctl value is passed to the kernel in conjunction
 * with a pointer to a little-endian bitmask of cpus, which must be
 * physically in a rectangular configuration on the chip.
 * The "size" is the number of bytes of cpu mask data.
 */
#define _HARDWALL_CREATE 1
#define HARDWALL_CREATE(size) \
  _IOC(_IOC_READ, HARDWALL_IOCTL_BASE, _HARDWALL_CREATE, (size))

#define _HARDWALL_ACTIVATE 2
#define HARDWALL_ACTIVATE \
  _IO(HARDWALL_IOCTL_BASE, _HARDWALL_ACTIVATE)

#define _HARDWALL_DEACTIVATE 3
#define HARDWALL_DEACTIVATE \
 _IO(HARDWALL_IOCTL_BASE, _HARDWALL_DEACTIVATE)

#ifndef __KERNEL__

/* This is the canonical name expected by userspace. */
#define HARDWALL_FILE "/dev/hardwall"

#else

/* Hook for /proc/tile/hardwall. */
struct seq_file;
int proc_tile_hardwall_show(struct seq_file *sf, void *v);

#endif

#endif /* _ASM_TILE_HARDWALL_H */
