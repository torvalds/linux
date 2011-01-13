/*
 * This file contains the hardware definitions of the Cavium Networks boards.
 *
 * Copyright 2003 ARM Limited.
 * Copyright 2008 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_HARDWARE_H
#define __MACH_HARDWARE_H

#include <asm/sizes.h>

/* macro to get at IO space when running virtually */
#define PCIBIOS_MIN_IO		0x00000000
#define PCIBIOS_MIN_MEM		0x00000000
#define pcibios_assign_all_busses()	1

#endif
