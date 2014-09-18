/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <shu.lin@conexant.com>, <hiep.huynh@conexant.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __CX25821_AUDIO_H__
#define __CX25821_AUDIO_H__

#define USE_RISC_NOOP		1
#define LINES_PER_BUFFER	15
#define AUDIO_LINE_SIZE		128

/* Number of buffer programs to use at once. */
#define NUMBER_OF_PROGRAMS	8

/*
 * Max size of the RISC program for a buffer. - worst case is 2 writes per line
 * Space is also added for the 4 no-op instructions added on the end.
 */
#ifndef USE_RISC_NOOP
#define MAX_BUFFER_PROGRAM_SIZE						\
	(2 * LINES_PER_BUFFER * RISC_WRITE_INSTRUCTION_SIZE +		\
	 RISC_WRITECR_INSTRUCTION_SIZE * 4)
#endif

/* MAE 12 July 2005 Try to use NOOP RISC instruction instead */
#ifdef USE_RISC_NOOP
#define MAX_BUFFER_PROGRAM_SIZE						\
	(2 * LINES_PER_BUFFER * RISC_WRITE_INSTRUCTION_SIZE +		\
	 RISC_NOOP_INSTRUCTION_SIZE * 4)
#endif

/* Sizes of various instructions in bytes.  Used when adding instructions. */
#define RISC_WRITE_INSTRUCTION_SIZE	12
#define RISC_JUMP_INSTRUCTION_SIZE	12
#define RISC_SKIP_INSTRUCTION_SIZE	4
#define RISC_SYNC_INSTRUCTION_SIZE	4
#define RISC_WRITECR_INSTRUCTION_SIZE	16
#define RISC_NOOP_INSTRUCTION_SIZE	4

#define MAX_AUDIO_DMA_BUFFER_SIZE					\
	(MAX_BUFFER_PROGRAM_SIZE * NUMBER_OF_PROGRAMS +			\
	 RISC_SYNC_INSTRUCTION_SIZE)

#endif
