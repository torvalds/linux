/*

Copyright 1996,2002,2005 Gregory D. Hager, Alfred A. Rizzi, Noah J. Cowan,
			 Jason Lapenta, Scott Smedley

This file is part of the DT3155 Device Driver.

The DT3155 Device Driver is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The DT3155 Device Driver is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty
of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the DT3155 Device Driver; if not, write to the Free
Software Foundation, Inc., 59 Temple Place, Suite 330, Boston,
MA 02111-1307 USA

-- Changes --

  Date     Programmer  Description of changes made
  -------------------------------------------------------------------
  03-Jul-2000 JML     n/a
  10-Oct-2001 SS      port to 2.4 kernel.
  24-Jul-2002 SS      remove unused code & added GPL licence.
  05-Aug-2005 SS      port to 2.6 kernel; make CCIR mode default.

*/

#ifndef _DT3155_INC
#define _DT3155_INC

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/time.h>		/* struct timeval */
#else
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/time.h>
#include <unistd.h>
#endif


#define TRUE  1
#define FALSE 0

/* Uncomment this for 50Hz CCIR */
#define CCIR 1

/* Can be 1 or 2 */
#define MAXBOARDS 1

#define BOARD_MAX_BUFFS	3
#define MAXBUFFERS	(BOARD_MAX_BUFFS*MAXBOARDS)

#define PCI_PAGE_SIZE	(1 << 12)

#ifdef CCIR
#define DT3155_MAX_ROWS	576
#define DT3155_MAX_COLS	768
#define FORMAT50HZ	TRUE
#else
#define DT3155_MAX_ROWS	480
#define DT3155_MAX_COLS	640
#define FORMAT50HZ	FALSE
#endif

/* Configuration structure */
struct dt3155_config_s {
	u32 acq_mode;
	u32 cols, rows;
	u32 continuous;
};


/* hold data for each frame */
typedef struct {
	u32 addr;		/* address of the buffer with the frame */
	u32 tag;		/* unique number for the frame */
	struct timeval time;	/* time that capture took place */
} frame_info_t;

/*
 * Structure for interrupt and buffer handling.
 * This is the setup for 1 card
 */
struct dt3155_fbuffer_s {
	int    nbuffers;

	frame_info_t frame_info[BOARD_MAX_BUFFS];

	int empty_buffers[BOARD_MAX_BUFFS];	/* indexes empty frames */
	int empty_len;				/* Number of empty buffers */
						/* Zero means empty */

	int active_buf;			/* Where data is currently dma'ing */
	int locked_buf;			/* Buffers used by user */

	int ready_que[BOARD_MAX_BUFFS];
	u32 ready_head;	/* The most recent buffer located here */
	u32 ready_len;	/* The number of ready buffers */

	int even_happened;
	int even_stopped;

	int stop_acquire;	/* Flag to stop interrupts */
	u32 frame_count;	/* Counter for frames acquired by this card */
};



#define DT3155_MODE_FRAME	1
#define DT3155_MODE_FIELD	2

#define DT3155_SNAP		1
#define DT3155_ACQ		2

/* There is one status structure for each card. */
typedef struct dt3155_status_s {
	int fixed_mode;		/* if 1, we are in fixed frame mode */
	u32 reg_addr;	/* Register address for a single card */
	u32 mem_addr;	/* Buffer start addr for this card */
	u32 mem_size;	/* This is the amount of mem available  */
	u32 irq;		/* this card's irq */
	struct dt3155_config_s config;		/* configuration struct */
	struct dt3155_fbuffer_s fbuffer;	/* frame buffer state struct */
	u32 state;		/* this card's state */
	u32 device_installed;	/* Flag if installed. 1=installed */
} dt3155_status_t;

/* Reference to global status structure */
extern struct dt3155_status_s dt3155_status[MAXBOARDS];

#define DT3155_STATE_IDLE	0x00
#define DT3155_STATE_FRAME	0x01
#define DT3155_STATE_FLD	0x02
#define DT3155_STATE_STOP	0x100
#define DT3155_STATE_ERROR	0x200
#define DT3155_STATE_MODE	0x0ff

#define DT3155_IOC_MAGIC	'!'

#define DT3155_SET_CONFIG	_IOW(DT3155_IOC_MAGIC, 1, struct dt3155_config_s)
#define DT3155_GET_CONFIG	_IOR(DT3155_IOC_MAGIC, 2, struct dt3155_status_s)
#define DT3155_STOP		_IO(DT3155_IOC_MAGIC, 3)
#define DT3155_START		_IO(DT3155_IOC_MAGIC, 4)
#define DT3155_FLUSH		_IO(DT3155_IOC_MAGIC, 5)
#define DT3155_IOC_MAXNR	5

/* Error codes */

#define DT_ERR_NO_BUFFERS	0x10000	/* not used but it might be one day */
#define DT_ERR_CORRUPT		0x20000
#define DT_ERR_OVERRUN		0x30000
#define DT_ERR_I2C_TIMEOUT	0x40000
#define DT_ERR_MASK		0xff0000/* not used but it might be one day */

/* User code will probably want to declare one of these for each card */
typedef struct dt3155_read_s {
	u32 offset;
	u32 frame_seq;
	u32 state;

	frame_info_t frame_info;
} dt3155_read_t;

#endif /* _DT3155_inc */
