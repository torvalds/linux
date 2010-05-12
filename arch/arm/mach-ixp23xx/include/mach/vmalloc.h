/*
 * arch/arm/mach-ixp23xx/include/mach/vmalloc.h
 *
 * Copyright (c) 2005 MontaVista Software, Inc.
 *
 * NPU mappings end at 0xf0000000 and we allocate 64MB for board
 * specific static I/O.
 */

#define VMALLOC_END	(0xec000000UL)
