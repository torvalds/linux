/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ORC_LOOKUP_H
#define _ORC_LOOKUP_H

/*
 * This is a lookup table for speeding up access to the .orc_unwind table.
 * Given an input address offset, the corresponding lookup table entry
 * specifies a subset of the .orc_unwind table to search.
 *
 * Each block represents the end of the previous range and the start of the
 * next range.  An extra block is added to give the last range an end.
 *
 * The block size should be a power of 2 to avoid a costly 'div' instruction.
 *
 * A block size of 256 was chosen because it roughly doubles unwinder
 * performance while only adding ~5% to the ORC data footprint.
 */
#define LOOKUP_BLOCK_ORDER	8
#define LOOKUP_BLOCK_SIZE	(1 << LOOKUP_BLOCK_ORDER)

#ifndef LINKER_SCRIPT

extern unsigned int orc_lookup[];
extern unsigned int orc_lookup_end[];

#define LOOKUP_START_IP		(unsigned long)_stext
#define LOOKUP_STOP_IP		(unsigned long)_etext

#endif /* LINKER_SCRIPT */

#endif /* _ORC_LOOKUP_H */
