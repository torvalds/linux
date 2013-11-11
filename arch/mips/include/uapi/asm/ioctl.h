/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 99, 2001 Ralf Baechle <ralf@linux-mips.org>
 * Copyright (C) 2009 Wind River Systems
 * Written by Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef __ASM_IOCTL_H
#define __ASM_IOCTL_H

#define _IOC_SIZEBITS	13
#define _IOC_DIRBITS	3

/*
 * Direction bits _IOC_NONE could be 0, but OSF/1 gives it a bit.
 * And this turns out useful to catch old ioctl numbers in header
 * files for us.
 */
#define _IOC_NONE	1U
#define _IOC_READ	2U
#define _IOC_WRITE	4U

#include <asm-generic/ioctl.h>

#endif /* __ASM_IOCTL_H */
