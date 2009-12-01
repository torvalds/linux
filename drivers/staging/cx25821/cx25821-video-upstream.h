/*
 *  Driver for the Conexant CX25821 PCIe bridge
 *
 *  Copyright (C) 2009 Conexant Systems Inc.
 *  Authors  <hiep.huynh@conexant.com>, <shu.lin@conexant.com>
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

#include <linux/mutex.h>
#include <linux/workqueue.h>

#define OUTPUT_FRMT_656       0
#define OPEN_FILE_1           0
#define NUM_PROGS             8
#define NUM_FRAMES            2
#define ODD_FIELD             0
#define EVEN_FIELD            1
#define TOP_OFFSET            0
#define FIFO_DISABLE          0
#define FIFO_ENABLE           1
#define TEST_FRAMES           5
#define END_OF_FILE           0
#define IN_PROGRESS           1
#define RESET_STATUS          -1
#define NUM_NO_OPS            5

// PAL and NTSC line sizes and number of lines.
#define WIDTH_D1              720
#define NTSC_LINES_PER_FRAME  480
#define PAL_LINES_PER_FRAME   576
#define PAL_LINE_SZ           1440
#define Y422_LINE_SZ          1440
#define Y411_LINE_SZ          1080
#define NTSC_FIELD_HEIGHT     240
#define NTSC_ODD_FLD_LINES    241
#define PAL_FIELD_HEIGHT      288

#define FRAME_SIZE_NTSC_Y422    (NTSC_LINES_PER_FRAME * Y422_LINE_SZ)
#define FRAME_SIZE_NTSC_Y411    (NTSC_LINES_PER_FRAME * Y411_LINE_SZ)
#define FRAME_SIZE_PAL_Y422     (PAL_LINES_PER_FRAME * Y422_LINE_SZ)
#define FRAME_SIZE_PAL_Y411     (PAL_LINES_PER_FRAME * Y411_LINE_SZ)

#define NTSC_DATA_BUF_SZ        (Y422_LINE_SZ * NTSC_LINES_PER_FRAME)
#define PAL_DATA_BUF_SZ         (Y422_LINE_SZ * PAL_LINES_PER_FRAME)

#define RISC_WRITECR_INSTRUCTION_SIZE   16
#define RISC_SYNC_INSTRUCTION_SIZE      4
#define JUMP_INSTRUCTION_SIZE           12
#define MAXSIZE_NO_OPS                  36
#define DWORD_SIZE                      4

#define USE_RISC_NOOP_VIDEO   1

#ifdef USE_RISC_NOOP_VIDEO
#define PAL_US_VID_PROG_SIZE        ((PAL_FIELD_HEIGHT) * 3 * DWORD_SIZE + RISC_WRITECR_INSTRUCTION_SIZE +   \
				      RISC_SYNC_INSTRUCTION_SIZE + NUM_NO_OPS*DWORD_SIZE)

#define PAL_RISC_BUF_SIZE           (2 * PAL_US_VID_PROG_SIZE)

#define PAL_VID_PROG_SIZE           ((PAL_FIELD_HEIGHT*2) * 3 * DWORD_SIZE + 2*RISC_SYNC_INSTRUCTION_SIZE + \
				      RISC_WRITECR_INSTRUCTION_SIZE + JUMP_INSTRUCTION_SIZE + 2*NUM_NO_OPS*DWORD_SIZE)

#define ODD_FLD_PAL_PROG_SIZE       ((PAL_FIELD_HEIGHT) * 3 * DWORD_SIZE + RISC_SYNC_INSTRUCTION_SIZE + \
				      RISC_WRITECR_INSTRUCTION_SIZE + NUM_NO_OPS*DWORD_SIZE)

#define ODD_FLD_NTSC_PROG_SIZE      ((NTSC_ODD_FLD_LINES) * 3 * DWORD_SIZE + RISC_SYNC_INSTRUCTION_SIZE + \
				      RISC_WRITECR_INSTRUCTION_SIZE + NUM_NO_OPS*DWORD_SIZE)

#define NTSC_US_VID_PROG_SIZE       ((NTSC_ODD_FLD_LINES + 1) * 3 * DWORD_SIZE + RISC_WRITECR_INSTRUCTION_SIZE + \
				      JUMP_INSTRUCTION_SIZE + NUM_NO_OPS*DWORD_SIZE)

#define NTSC_RISC_BUF_SIZE          (2 * (RISC_SYNC_INSTRUCTION_SIZE + NTSC_US_VID_PROG_SIZE))

#define FRAME1_VID_PROG_SIZE        ((NTSC_ODD_FLD_LINES + NTSC_FIELD_HEIGHT) * 3 * DWORD_SIZE + 2*RISC_SYNC_INSTRUCTION_SIZE + \
				      RISC_WRITECR_INSTRUCTION_SIZE + JUMP_INSTRUCTION_SIZE + 2*NUM_NO_OPS*DWORD_SIZE)

#endif

#ifndef USE_RISC_NOOP_VIDEO
#define PAL_US_VID_PROG_SIZE        ((PAL_FIELD_HEIGHT) * 3 * DWORD_SIZE + RISC_WRITECR_INSTRUCTION_SIZE + \
				      RISC_SYNC_INSTRUCTION_SIZE + JUMP_INSTRUCTION_SIZE)

#define PAL_RISC_BUF_SIZE           (2 * PAL_US_VID_PROG_SIZE)

#define PAL_VID_PROG_SIZE           ((PAL_FIELD_HEIGHT*2) * 3 * DWORD_SIZE + 2*RISC_SYNC_INSTRUCTION_SIZE + \
				      RISC_WRITECR_INSTRUCTION_SIZE + JUMP_INSTRUCTION_SIZE )

#define ODD_FLD_PAL_PROG_SIZE       ((PAL_FIELD_HEIGHT) * 3 * DWORD_SIZE + RISC_SYNC_INSTRUCTION_SIZE + RISC_WRITECR_INSTRUCTION_SIZE )
#define ODD_FLD_NTSC_PROG_SIZE      ((NTSC_ODD_FLD_LINES) * 3 * DWORD_SIZE + RISC_SYNC_INSTRUCTION_SIZE + RISC_WRITECR_INSTRUCTION_SIZE )

#define NTSC_US_VID_PROG_SIZE       ((NTSC_ODD_FLD_LINES + 1) * 3 * DWORD_SIZE + RISC_WRITECR_INSTRUCTION_SIZE + JUMP_INSTRUCTION_SIZE)
#define NTSC_RISC_BUF_SIZE          ( 2 * (RISC_SYNC_INSTRUCTION_SIZE + NTSC_US_VID_PROG_SIZE) )
#define FRAME1_VID_PROG_SIZE        ((NTSC_ODD_FLD_LINES + NTSC_FIELD_HEIGHT) * 3 * DWORD_SIZE + 2*RISC_SYNC_INSTRUCTION_SIZE + \
				      RISC_WRITECR_INSTRUCTION_SIZE + JUMP_INSTRUCTION_SIZE )
#endif
