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
 */

#include <linux/mutex.h>
#include <linux/workqueue.h>

#define NUM_AUDIO_PROGS       8
#define NUM_AUDIO_FRAMES      8
#define END_OF_FILE           0
#define IN_PROGRESS           1
#define RESET_STATUS          -1
#define FIFO_DISABLE          0
#define FIFO_ENABLE           1
#define NUM_NO_OPS            4

#define RISC_READ_INSTRUCTION_SIZE      12
#define RISC_JUMP_INSTRUCTION_SIZE      12
#define RISC_WRITECR_INSTRUCTION_SIZE   16
#define RISC_SYNC_INSTRUCTION_SIZE      4
#define DWORD_SIZE                      4
#define AUDIO_SYNC_LINE                 4

#define LINES_PER_AUDIO_BUFFER      15
#define AUDIO_LINE_SIZE             128
#define AUDIO_DATA_BUF_SZ           (AUDIO_LINE_SIZE * LINES_PER_AUDIO_BUFFER)

#define USE_RISC_NOOP_AUDIO   1

#ifdef USE_RISC_NOOP_AUDIO
#define AUDIO_RISC_DMA_BUF_SIZE						\
	(LINES_PER_AUDIO_BUFFER * RISC_READ_INSTRUCTION_SIZE +		\
	 RISC_WRITECR_INSTRUCTION_SIZE + NUM_NO_OPS * DWORD_SIZE +	\
	 RISC_JUMP_INSTRUCTION_SIZE)
#endif

#ifndef USE_RISC_NOOP_AUDIO
#define AUDIO_RISC_DMA_BUF_SIZE						\
	(LINES_PER_AUDIO_BUFFER * RISC_READ_INSTRUCTION_SIZE +		\
	 RISC_WRITECR_INSTRUCTION_SIZE + RISC_JUMP_INSTRUCTION_SIZE)
#endif

static int _line_size;
char *_defaultAudioName = "/root/audioGOOD.wav";
