/*
 * Copyright (C) 2017 Josh Poimboeuf <jpoimboe@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
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
