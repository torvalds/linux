/*
 * memory & I/O static mapping definitions for CSR SiRFprimaII
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef __MACH_PRIMA2_MAP_H__
#define __MACH_PRIMA2_MAP_H__

#include <mach/vmalloc.h>

#define SIRFSOC_VA(x)			(VMALLOC_END + ((x) & 0x00FFF000))

#endif
