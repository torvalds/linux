/*
 * low-level functions for the SWIM floppy controller
 *
 * needs assembly language because is very timing dependent
 * this controller exists only on macintosh 680x0 based
 *
 * Copyright (C) 2004,2008 Laurent Vivier <Laurent@lvivier.info>
 *
 * based on Alastair Bridgewater SWIM analysis, 2001
 * based on netBSD IWM driver (c) 1997, 1998 Hauke Fath.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * 2004-08-21 (lv) - Initial implementation
 * 2008-11-05 (lv) - add get_swim_mode
 */

	.equ	write_data,	0x0000
	.equ	write_mark,	0x0200
	.equ	write_CRC,	0x0400
	.equ	write_parameter,0x0600
	.equ	write_phase,	0x0800
	.equ	write_setup,	0x0a00
	.equ	write_mode0,	0x0c00
	.equ	write_mode1,	0x0e00
	.equ	read_data,	0x1000
	.equ	read_mark,	0x1200
	.equ	read_error,	0x1400
	.equ	read_parameter,	0x1600
	.equ	read_phase,	0x1800
	.equ	read_setup,	0x1a00
	.equ	read_status,	0x1c00
	.equ	read_handshake,	0x1e00

	.equ	o_side, 0
	.equ	o_track, 1
	.equ	o_sector, 2
	.equ	o_size, 3
	.equ	o_crc0, 4
	.equ	o_crc1, 5

	.equ	seek_time, 30000
	.equ	max_retry, 40
	.equ	sector_size, 512

	.global swim_read_sector_header
swim_read_sector_header:
	link	%a6, #0
	moveml	%d1-%d5/%a0-%a4,%sp@-
	movel	%a6@(0x0c), %a4
	bsr	mfm_read_addrmark
	moveml	%sp@+, %d1-%d5/%a0-%a4
	unlk	%a6
	rts

sector_address_mark:
	.byte	0xa1, 0xa1, 0xa1, 0xfe
sector_data_mark:
	.byte	0xa1, 0xa1, 0xa1, 0xfb

mfm_read_addrmark:
	movel	%a6@(0x08), %a3
	lea	%a3@(read_handshake), %a2
	lea	%a3@(read_mark), %a3
	moveq	#-1, %d0
	movew	#seek_time, %d2

wait_header_init:
	tstb	%a3@(read_error - read_mark)
	moveb	#0x18, %a3@(write_mode0 - read_mark)
	moveb	#0x01, %a3@(write_mode1 - read_mark)
	moveb	#0x01, %a3@(write_mode0 - read_mark)
	tstb	%a3@(read_error - read_mark)
	moveb	#0x08, %a3@(write_mode1 - read_mark)

	lea	sector_address_mark, %a0
	moveq	#3, %d1

wait_addr_mark_byte:

	tstb	%a2@
	dbmi	%d2, wait_addr_mark_byte
	bpl	header_exit

	moveb	%a3@, %d3
	cmpb	%a0@+, %d3
	dbne	%d1, wait_addr_mark_byte
	bne	wait_header_init

	moveq	#max_retry, %d2

amark0:	tstb	%a2@
	dbmi	%d2, amark0
	bpl	signal_nonyb

	moveb	%a3@, %a4@(o_track)

	moveq	#max_retry, %d2

amark1:	tstb	%a2@
	dbmi	%d2, amark1
	bpl	signal_nonyb

	moveb	%a3@, %a4@(o_side)

	moveq	#max_retry, %d2

amark2:	tstb	%a2@
	dbmi	%d2, amark2
	bpl	signal_nonyb

	moveb	%a3@, %a4@(o_sector)

	moveq	#max_retry, %d2

amark3:	tstb	%a2@
	dbmi	%d2, amark3
	bpl	signal_nonyb

	moveb	%a3@, %a4@(o_size)

	moveq	#max_retry, %d2

crc0:	tstb	%a2@
	dbmi	%d2, crc0
	bpl	signal_nonyb

	moveb	%a3@, %a4@(o_crc0)

	moveq	#max_retry, %d2

crc1:	tstb	%a2@
	dbmi	%d2, crc1
	bpl	signal_nonyb

	moveb	%a3@, %a4@(o_crc1)

	tstb	%a3@(read_error - read_mark)

header_exit:
	moveq	#0, %d0
	moveb	#0x18, %a3@(write_mode0 - read_mark)
	rts
signal_nonyb:
	moveq	#-1, %d0
	moveb	#0x18, %a3@(write_mode0 - read_mark)
	rts

	.global swim_read_sector_data
swim_read_sector_data:
	link	%a6, #0
	moveml	%d1-%d5/%a0-%a5,%sp@-
	movel	%a6@(0x0c), %a4
	bsr	mfm_read_data
	moveml	%sp@+, %d1-%d5/%a0-%a5
	unlk	%a6
	rts

mfm_read_data:
	movel	%a6@(0x08), %a3
	lea	%a3@(read_handshake), %a2
	lea	%a3@(read_data), %a5
	lea	%a3@(read_mark), %a3
	movew	#seek_time, %d2

wait_data_init:
	tstb	%a3@(read_error - read_mark)
	moveb	#0x18, %a3@(write_mode0 - read_mark)
	moveb	#0x01, %a3@(write_mode1 - read_mark)
	moveb	#0x01, %a3@(write_mode0 - read_mark)
	tstb	%a3@(read_error - read_mark)
	moveb	#0x08, %a3@(write_mode1 - read_mark)

	lea	sector_data_mark, %a0
	moveq	#3, %d1

	/* wait data address mark */

wait_data_mark_byte:

	tstb	%a2@
	dbmi	%d2, wait_data_mark_byte
	bpl	data_exit

	moveb	%a3@, %d3
	cmpb	%a0@+, %d3
	dbne	%d1, wait_data_mark_byte
	bne	wait_data_init

	/* read data */

	tstb	%a3@(read_error - read_mark)

	movel	#sector_size-1, %d4		/* sector size */
read_new_data:
	movew	#max_retry, %d2
read_data_loop:
	moveb	%a2@, %d5
	andb	#0xc0, %d5
	dbne	%d2, read_data_loop
	beq	data_exit
	moveb	%a5@, %a4@+
	andb	#0x40, %d5
	dbne	%d4, read_new_data
	beq	exit_loop
	moveb	%a5@, %a4@+
	dbra	%d4, read_new_data
exit_loop:

	/* read CRC */

	movew	#max_retry, %d2
data_crc0:

	tstb	%a2@
	dbmi	%d2, data_crc0
	bpl	data_exit

	moveb	%a3@, %d5

	moveq	#max_retry, %d2

data_crc1:

	tstb	%a2@
	dbmi	%d2, data_crc1
	bpl	data_exit

	moveb	%a3@, %d5

	tstb	%a3@(read_error - read_mark)

	moveb	#0x18, %a3@(write_mode0 - read_mark)

	/* return number of bytes read */

	movel	#sector_size, %d0
	addw	#1, %d4
	subl	%d4, %d0
	rts
data_exit:
	moveb	#0x18, %a3@(write_mode0 - read_mark)
	moveq	#-1, %d0
	rts
