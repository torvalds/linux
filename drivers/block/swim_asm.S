/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * low-level functions for the SWIM floppy controller
 *
 * needs assembly language because is very timing dependent
 * this controller exists only on macintosh 680x0 based
 *
 * Copyright (C) 2004, 2008 Laurent Vivier <Laurent@lvivier.info>
 *
 * based on Alastair Bridgewater SWIM analysis, 2001
 * based on netBSD IWM driver (c) 1997, 1998 Hauke Fath.
 *
 * 2004-08-21 (lv) - Initial implementation
 * 2008-11-05 (lv) - add get_swim_mode
 */

	.equ	.Lwrite_data,		0x0000
	.equ	.Lwrite_mark,		0x0200
	.equ	.Lwrite_CRC,		0x0400
	.equ	.Lwrite_parameter,	0x0600
	.equ	.Lwrite_phase,		0x0800
	.equ	.Lwrite_setup,		0x0a00
	.equ	.Lwrite_mode0,		0x0c00
	.equ	.Lwrite_mode1,		0x0e00
	.equ	.Lread_data,		0x1000
	.equ	.Lread_mark,		0x1200
	.equ	.Lread_error,		0x1400
	.equ	.Lread_parameter,	0x1600
	.equ	.Lread_phase,		0x1800
	.equ	.Lread_setup,		0x1a00
	.equ	.Lread_status,		0x1c00
	.equ	.Lread_handshake,	0x1e00

	.equ	.Lo_side,		0
	.equ	.Lo_track,		1
	.equ	.Lo_sector,		2
	.equ	.Lo_size,		3
	.equ	.Lo_crc0,		4
	.equ	.Lo_crc1,		5

	.equ	.Lseek_time,		30000
	.equ	.Lmax_retry,		40
	.equ	.Lsector_size,		512
	.equ	.Lmark_sequence_len,	4

	.equ	.Lmr_clear_fifo,	0x01
	.equ	.Lmr_action,		0x08
	.equ	.Lmr_write_action,	0x18

	.equ	.Lhr_crc_error,		0x02
	.equ	.Lhr_fifo_2bytes,	0x40
	.equ	.Lhr_fifo_1byte,	0x80

.Lmfm_mark_check:
	/*
	 * This subroutine reads and validates a mark byte sequence.
	 * On entry, %a1 and %d4 shall hold the location and length (resp.)
	 * of the mark byte array.
	 * %a2 and %a3 shall hold the locations of the handshake and mark
	 * registers.
	 * Returns zero in %d1 for success.
	 */

	moveq	#-1, %d1
	subq	#1, %d4
	movew	#.Lseek_time, %d2

5:	tstb	%a2@
	dbmi	%d2, 5b
	bpl	6f

	moveb	%a3@, %d3
	cmpb	%a1@+, %d3
	dbne	%d4, 5b
	bne	6f

	moveq	#0, %d1
6:	rts

	.global swim_read_sector_header
swim_read_sector_header:
	link	%a6, #0
	moveml	%d1-%d5/%a0-%a5,%sp@-
	movel	%a6@(0x0c), %a4
	moveq	#-1, %d0
	bsr	.Lmfm_read_header
	moveml	%sp@+, %d1-%d5/%a0-%a5
	unlk	%a6
	rts

.Lsector_address_mark:
	.byte	0xa1, 0xa1, 0xa1, 0xfe
.Lsector_data_mark:
	.byte	0xa1, 0xa1, 0xa1, 0xfb

.Lmfm_read_header:
	movel	%a6@(0x08), %a3
	lea	%a3@(.Lread_handshake), %a2
	lea	%a3@(.Lread_data), %a5
	lea	%a3@(.Lread_mark), %a3

	moveb	#.Lmr_write_action, %a3@(.Lwrite_mode0 - .Lread_mark)
	moveb	#.Lmr_clear_fifo, %a3@(.Lwrite_mode1 - .Lread_mark)
	moveb	#.Lmr_clear_fifo, %a3@(.Lwrite_mode0 - .Lread_mark)
	tstb	%a3@(.Lread_error - .Lread_mark)
	moveb	#.Lmr_action, %a3@(.Lwrite_mode1 - .Lread_mark)

	lea	.Lsector_address_mark, %a1
	moveq	#.Lmark_sequence_len, %d4
	bsr	.Lmfm_mark_check
	tstl	%d1
	bne	.Lsignal_nonyb

	/* read header */

	moveq	#.Lmax_retry, %d2
.Lamark0:
	tstb	%a2@
	dbmi	%d2, .Lamark0
	bpl	.Lsignal_nonyb

	moveb	%a5@, %a4@(.Lo_track)

	moveq	#.Lmax_retry, %d2
.Lamark1:
	tstb	%a2@
	dbmi	%d2, .Lamark1
	bpl	.Lsignal_nonyb

	moveb	%a5@, %a4@(.Lo_side)

	moveq	#.Lmax_retry, %d2
.Lamark2:
	tstb	%a2@
	dbmi	%d2, .Lamark2
	bpl	.Lsignal_nonyb

	moveb	%a5@, %a4@(.Lo_sector)

	moveq	#.Lmax_retry, %d2
.Lamark3:
	tstb	%a2@
	dbmi	%d2, .Lamark3
	bpl	.Lsignal_nonyb

	moveb	%a5@, %a4@(.Lo_size)

	moveq	#.Lmax_retry, %d2
.Lcrc0:
	tstb	%a2@
	dbmi	%d2, .Lcrc0
	bpl	.Lsignal_nonyb

	moveb	%a5@, %a4@(.Lo_crc0)

	moveq	#.Lmax_retry, %d2
.Lcrc1:
	tstb	%a2@
	dbmi	%d2, .Lcrc1
	bpl	.Lsignal_nonyb

	moveb	%a5@, %a4@(.Lo_crc1)

	moveb	%a2@, %d5
	andb	#.Lhr_crc_error, %d5
	bne	.Lsignal_nonyb

	moveq	#0, %d0
.Lsignal_nonyb:
	moveb	#.Lmr_write_action, %a3@(.Lwrite_mode0 - .Lread_mark)
	rts

	.global swim_read_sector_data
swim_read_sector_data:
	link	%a6, #0
	moveml	%d1-%d5/%a0-%a5,%sp@-
	movel	%a6@(0x0c), %a4
	moveq	#-1, %d0
	bsr	.Lmfm_read_data
	moveml	%sp@+, %d1-%d5/%a0-%a5
	unlk	%a6
	rts

.Lmfm_read_data:
	movel	%a6@(0x08), %a3
	lea	%a3@(.Lread_handshake), %a2
	lea	%a3@(.Lread_data), %a5
	lea	%a3@(.Lread_mark), %a3

	moveb	#.Lmr_write_action, %a3@(.Lwrite_mode0 - .Lread_mark)
	moveb	#.Lmr_clear_fifo, %a3@(.Lwrite_mode1 - .Lread_mark)
	moveb	#.Lmr_clear_fifo, %a3@(.Lwrite_mode0 - .Lread_mark)
	tstb	%a3@(.Lread_error - .Lread_mark)
	moveb	#.Lmr_action, %a3@(.Lwrite_mode1 - .Lread_mark)

	lea	.Lsector_data_mark, %a1
	moveq	#.Lmark_sequence_len, %d4
	bsr	.Lmfm_mark_check
	tstl	%d1
	bne	.Ldata_exit

	/* read data */

	movel	#.Lsector_size - 1, %d4		/* sector size */
	movew	#.Lmax_retry, %d2
.Lread_data_loop:
	moveb	%a2@, %d5
	andb	#(.Lhr_fifo_1byte + .Lhr_fifo_2bytes), %d5
	dbne	%d2, .Lread_data_loop
	beq	.Ldata_exit

	moveq	#.Lmax_retry, %d2
	moveb	%a5@, %d3
	bsr	.Lmaybe_store
	dbra	%d4, 1f
	bra	.Ldata_crc0
1:	andb	#.Lhr_fifo_2bytes, %d5
	beq	.Lread_data_loop
	moveb	%a5@, %d3
	bsr	.Lmaybe_store
	dbra	%d4, .Lread_data_loop

	/* read CRC */

	movew	#.Lmax_retry, %d2
.Ldata_crc0:
	tstb	%a2@
	dbmi	%d2, .Ldata_crc0
	bpl	.Ldata_exit

	moveb	%a5@, %d5

	moveq	#.Lmax_retry, %d2
.Ldata_crc1:
	tstb	%a2@
	dbmi	%d2, .Ldata_crc1
	bpl	.Ldata_exit

	moveb	%a5@, %d5

	moveb	%a2@, %d5
	andb	#.Lhr_crc_error, %d5
	bne	.Ldata_exit

	/* return number of bytes read */

	movel	#.Lsector_size, %d0
	addw	#1, %d4
	subl	%d4, %d0

.Ldata_exit:
	moveb	#.Lmr_write_action, %a3@(.Lwrite_mode0 - .Lread_mark)
	rts

.Lmaybe_store:
	tstl	%a4
	beq	9f
	moveb	%d3, %a4@+
9:	rts
