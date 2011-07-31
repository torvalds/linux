/*
 * ts27010_mux.h
 *
 * Copyright (C) 2002, 2004, 2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
* This header file should be included by both MUX and other applications
* which access MUX device files. It gives the additional macro definitions
* shared between MUX and applications.
*/

#define NR_MUXS 16

#define NUM_MUX_CMD_FILES 16
#define NUM_MUX_DATA_FILES 0
#define NUM_MUX_FILES (NUM_MUX_CMD_FILES  +  NUM_MUX_DATA_FILES)

#define LDISC_BUFFER_SIZE (4096 - sizeof(struct ts27010_ringbuf))

/* TODO: should use the IOCTLNUM macros */
/* Special ioctl() upon a MUX device file for hanging up a call */
#define TS0710MUX_IO_MSC_HANGUP 0x54F0

/* Special ioctl() upon a MUX device file for MUX loopback test */
#define TS0710MUX_IO_TEST_CMD 0x54F1

/* TODO: get rid of these */
/* Special Error code might be return from write() to a MUX device file */
#define EDISCONNECTED 900	/* link is disconnected */

/* Special Error code might be return from open() to a MUX device file  */
#define EREJECTED 901		/* link connection request is rejected */

/* TODO: goes away with clean tty interface */
extern struct tty_struct *ts27010mux_tty;

struct ts27010_ringbuf;

int ts27010_mux_active(void);
int ts27010_mux_line_open(int line);
void ts27010_mux_line_close(int line);
int ts27010_mux_line_write(int line, const unsigned char *buf, int count);
int ts27010_mux_line_chars_in_buffer(int line);
int ts27010_mux_line_write_room(int line);
void ts27010_mux_recv(struct ts27010_ringbuf *rbuf);

int ts27010_ldisc_init(void);
void ts27010_ldisc_remove(void);
int ts27010_ldisc_send(struct tty_struct *tty, u8 *data, int len);


int ts27010_tty_init(void);
void ts27010_tty_remove(void);
int ts27010_tty_send(int line, u8 *data, int len);
int ts27010_tty_send_rbuf(int line, struct ts27010_ringbuf *rbuf,
			  int data_idx, int len);


