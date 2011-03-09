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
 */

#ifndef _ASM_TILE_OPCODE_TILE_H
#define _ASM_TILE_OPCODE_TILE_H

#include <arch/chip.h>

#if CHIP_WORD_SIZE() == 64
#include <asm/opcode-tile_64.h>
#else
#include <asm/opcode-tile_32.h>
#endif

/* These definitions are not correct for TILE64, so just avoid them. */
#undef TILE_ELF_MACHINE_CODE
#undef TILE_ELF_NAME

#endif /* _ASM_TILE_OPCODE_TILE_H */
