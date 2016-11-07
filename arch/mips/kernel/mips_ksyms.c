/*
 * Export MIPS-specific functions needed for loadable modules.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 97, 98, 99, 2000, 01, 03, 04, 05, 12 by Ralf Baechle
 * Copyright (C) 1999, 2000, 01 Silicon Graphics, Inc.
 */
#include <linux/interrupt.h>
#include <linux/export.h>
#include <asm/checksum.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <asm/ftrace.h>
#include <asm/fpu.h>
#include <asm/msa.h>

/*
 * Functions that operate on entire pages.  Mostly used by memory management.
 */
EXPORT_SYMBOL(clear_page);
EXPORT_SYMBOL(copy_page);
