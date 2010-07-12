/*
 * header.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2005-2006 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/string.h>
#define DL_STRCMP  strcmp

/* maximum parenthesis nesting in relocation stack expressions */
#define STATIC_EXPR_STK_SIZE 10

#include <linux/types.h>

#include "doff.h"
#include <dspbridge/dynamic_loader.h>
#include "params.h"
#include "dload_internal.h"
#include "reloc_table.h"

/*
 * Plausibility limits
 *
 * These limits are imposed upon the input DOFF file as a check for validity.
 * They are hard limits, in that the load will fail if they are exceeded.
 * The numbers selected are arbitrary, in that the loader implementation does
 * not require these limits.
 */

/* maximum number of bytes in string table */
#define MAX_REASONABLE_STRINGTAB (0x100000)
/* maximum number of code,data,etc. sections */
#define MAX_REASONABLE_SECTIONS (200)
/* maximum number of linker symbols */
#define MAX_REASONABLE_SYMBOLS (100000)

/* shift count to align F_BIG with DLOAD_LITTLE */
#define ALIGN_COFF_ENDIANNESS 7
#define ENDIANNESS_MASK (DF_BYTE_ORDER >> ALIGN_COFF_ENDIANNESS)
