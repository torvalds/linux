#ifndef _FDC_IO_H
#define _FDC_IO_H

/*
 *    Copyright (C) 1993-1996 Bas Laarhoven,
 *              (C) 1996-1997 Claus-Justus Heine.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2, or (at your option)
 any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; see the file COPYING.  If not, write to
 the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 *
 * $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/fdc-io.h,v $
 * $Revision: 1.3 $
 * $Date: 1997/10/05 19:18:06 $
 *
 *      This file contains the declarations for the low level
 *      functions that communicate with the floppy disk controller,
 *      for the QIC-40/80/3010/3020 floppy-tape driver "ftape" for
 *      Linux.
 */

#include <linux/fdreg.h>

#include "../lowlevel/ftape-bsm.h"

#define FDC_SK_BIT      (0x20)
#define FDC_MT_BIT      (0x80)

#define FDC_READ        (FD_READ & ~(FDC_SK_BIT | FDC_MT_BIT))
#define FDC_WRITE       (FD_WRITE & ~FDC_MT_BIT)
#define FDC_READ_DELETED  (0x4c)
#define FDC_WRITE_DELETED (0x49)
#define FDC_VERIFY        (0x56)
#define FDC_READID      (0x4a)
#define FDC_SENSED      (0x04)
#define FDC_SENSEI      (FD_SENSEI)
#define FDC_FORMAT      (FD_FORMAT)
#define FDC_RECAL       (FD_RECALIBRATE)
#define FDC_SEEK        (FD_SEEK)
#define FDC_SPECIFY     (FD_SPECIFY)
#define FDC_RECALIBR    (FD_RECALIBRATE)
#define FDC_VERSION     (FD_VERSION)
#define FDC_PERPEND     (FD_PERPENDICULAR)
#define FDC_DUMPREGS    (FD_DUMPREGS)
#define FDC_LOCK        (FD_LOCK)
#define FDC_UNLOCK      (FD_UNLOCK)
#define FDC_CONFIGURE   (FD_CONFIGURE)
#define FDC_DRIVE_SPEC  (0x8e)	/* i82078 has this (any others?) */
#define FDC_PARTID      (0x18)	/* i82078 has this */
#define FDC_SAVE        (0x2e)	/* i82078 has this (any others?) */
#define FDC_RESTORE     (0x4e)	/* i82078 has this (any others?) */

#define FDC_STATUS_MASK (STATUS_BUSY | STATUS_DMA | STATUS_DIR | STATUS_READY)
#define FDC_DATA_READY  (STATUS_READY)
#define FDC_DATA_OUTPUT (STATUS_DIR)
#define FDC_DATA_READY_MASK (STATUS_READY | STATUS_DIR)
#define FDC_DATA_OUT_READY  (STATUS_READY | STATUS_DIR)
#define FDC_DATA_IN_READY   (STATUS_READY)
#define FDC_BUSY        (STATUS_BUSY)
#define FDC_CLK48_BIT   (0x80)
#define FDC_SEL3V_BIT   (0x40)

#define ST0_INT_MASK    (ST0_INTR)
#define FDC_INT_NORMAL  (ST0_INTR & 0x00)
#define FDC_INT_ABNORMAL (ST0_INTR & 0x40)
#define FDC_INT_INVALID (ST0_INTR & 0x80)
#define FDC_INT_READYCH (ST0_INTR & 0xC0)
#define ST0_SEEK_END    (ST0_SE)
#define ST3_TRACK_0     (ST3_TZ)

#define FDC_RESET_NOT   (0x04)
#define FDC_DMA_MODE    (0x08)
#define FDC_MOTOR_0     (0x10)
#define FDC_MOTOR_1     (0x20)

typedef struct {
	void (**hook) (void);	/* our wedge into the isr */
	enum {
		no_fdc, i8272, i82077, i82077AA, fc10,
		i82078, i82078_1
	} type;			/* FDC type */
	unsigned int irq; /* FDC irq nr */
	unsigned int dma; /* FDC dma channel nr */
	__u16 sra;	  /* Status register A (PS/2 only) */
	__u16 srb;	  /* Status register B (PS/2 only) */
	__u16 dor;	  /* Digital output register */
	__u16 tdr;	  /* Tape Drive Register (82077SL-1 &
			     82078 only) */
	__u16 msr;	  /* Main Status Register */
	__u16 dsr;	  /* Datarate Select Register (8207x only) */
	__u16 fifo;	  /* Data register / Fifo on 8207x */
	__u16 dir;	  /* Digital Input Register */
	__u16 ccr;	  /* Configuration Control Register */
	__u16 dor2;	  /* Alternate dor on MACH-2 controller,
			     also used with FC-10, meaning unknown */
} fdc_config_info;

typedef enum {
	fdc_data_rate_250  = 2,
	fdc_data_rate_300  = 1,	/* any fdc in default configuration */
	fdc_data_rate_500  = 0,
	fdc_data_rate_1000 = 3,
	fdc_data_rate_2000 = 1,	/* i82078-1: when using Data Rate Table #2 */
} fdc_data_rate_type;

typedef enum {
	fdc_idle          = 0,
	fdc_reading_data  = FDC_READ,
	fdc_seeking       = FDC_SEEK,
	fdc_writing_data  = FDC_WRITE,
	fdc_deleting      = FDC_WRITE_DELETED,
	fdc_reading_id    = FDC_READID,
	fdc_recalibrating = FDC_RECAL,
	fdc_formatting    = FDC_FORMAT,
	fdc_verifying     = FDC_VERIFY
} fdc_mode_enum;

typedef enum {
	waiting = 0,
	reading,
	writing,
	formatting,
	verifying,
	deleting,
	done,
	error,
	mmapped,
} buffer_state_enum;

typedef struct {
	__u8 *address;
	volatile buffer_state_enum status;
	volatile __u8 *ptr;
	volatile unsigned int bytes;
	volatile unsigned int segment_id;

	/* bitmap for remainder of segment not yet handled.
	 * one bit set for each bad sector that must be skipped.
	 */
	volatile SectorMap bad_sector_map;

	/* bitmap with bad data blocks in data buffer.
	 * the errors in this map may be retried.
	 */
	volatile SectorMap soft_error_map;

	/* bitmap with bad data blocks in data buffer
	 * the errors in this map may not be retried.
	 */
	volatile SectorMap hard_error_map;

	/* retry counter for soft errors.
	 */
	volatile int retry;

	/* sectors to skip on retry ???
	 */
	volatile unsigned int skip;

	/* nr of data blocks in data buffer
	 */
	volatile unsigned int data_offset;

	/* offset in segment for first sector to be handled.
	 */
	volatile unsigned int sector_offset;

	/* size of cluster of good sectors to be handled.
	 */
	volatile unsigned int sector_count;

	/* size of remaining part of segment to be handled.
	 */
	volatile unsigned int remaining;

	/* points to next segment (contiguous) to be handled,
	 * or is zero if no read-ahead is allowed.
	 */
	volatile unsigned int next_segment;

	/* flag being set if deleted data was read.
	 */
	volatile int deleted;

	/* floppy coordinates of first sector in segment */
	volatile __u8 head;
	volatile __u8 cyl;
	volatile __u8 sect;

	/* gap to use when formatting */
	__u8 gap3;
	/* flag set when buffer is mmaped */
	int mmapped;
} buffer_struct;

/*
 *      fdc-io.c defined public variables
 */
extern volatile fdc_mode_enum fdc_mode;
extern int fdc_setup_error;	/* outdated ??? */
extern wait_queue_head_t ftape_wait_intr;
extern volatile int ftape_current_cylinder; /* track nr FDC thinks we're on */
extern volatile __u8 fdc_head;	/* FDC head */
extern volatile __u8 fdc_cyl;	/* FDC track */
extern volatile __u8 fdc_sect;	/* FDC sector */
extern fdc_config_info fdc;	/* FDC hardware configuration */

extern unsigned int ft_fdc_base;
extern unsigned int ft_fdc_irq;
extern unsigned int ft_fdc_dma;
extern unsigned int ft_fdc_threshold;
extern unsigned int ft_fdc_rate_limit;
extern int ft_probe_fc10;
extern int ft_mach2;
/*
 *      fdc-io.c defined public functions
 */
extern void fdc_catch_stray_interrupts(int count);
extern int fdc_ready_wait(unsigned int timeout);
extern int fdc_command(const __u8 * cmd_data, int cmd_len);
extern int fdc_result(__u8 * res_data, int res_len);
extern int fdc_interrupt_wait(unsigned int time);
extern int fdc_seek(int track);
extern int fdc_sense_drive_status(int *st3);
extern void fdc_motor(int motor);
extern void fdc_reset(void);
extern void fdc_disable(void);
extern int fdc_fifo_threshold(__u8 threshold,
			      int *fifo_state, int *lock_state, int *fifo_thr);
extern void fdc_wait_calibrate(void);
extern int fdc_sense_interrupt_status(int *st0, int *current_cylinder);
extern void fdc_save_drive_specs(void);
extern void fdc_restore_drive_specs(void);
extern int fdc_set_data_rate(int rate);
extern void fdc_set_write_precomp(int precomp);
extern int fdc_release_irq_and_dma(void);
extern void fdc_release_regions(void);
extern int fdc_init(void);
extern int fdc_setup_read_write(buffer_struct * buff, __u8 operation);
extern int fdc_setup_formatting(buffer_struct * buff);
#endif
