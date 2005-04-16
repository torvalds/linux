
/*
 * IBM ASM Service Processor Device Driver
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2004
 *
 * Author: Max Asböck <amax@us.ibm.com> 
 *
 * Orignally written by Pete Reynolds
 */

#ifndef _IBMASM_REMOTE_H_
#define _IBMASM_REMOTE_H_

#include <asm/io.h>

/* pci offsets */
#define CONDOR_MOUSE_DATA		0x000AC000
#define CONDOR_MOUSE_ISR_CONTROL	0x00
#define CONDOR_MOUSE_ISR_STATUS		0x04
#define CONDOR_MOUSE_Q_READER		0x08
#define CONDOR_MOUSE_Q_WRITER		0x0C
#define CONDOR_MOUSE_Q_BEGIN		0x10
#define CONDOR_MOUSE_MAX_X		0x14
#define CONDOR_MOUSE_MAX_Y		0x18

#define CONDOR_INPUT_DESKTOP_INFO	0x1F0
#define CONDOR_INPUT_DISPLAY_RESX	0x1F4
#define CONDOR_INPUT_DISPLAY_RESY	0x1F8
#define CONDOR_INPUT_DISPLAY_BITS	0x1FC
#define CONDOR_OUTPUT_VNC_STATUS	0x200

#define CONDOR_MOUSE_INTR_STATUS_MASK	0x00000001

#define INPUT_TYPE_MOUSE	0x1
#define INPUT_TYPE_KEYBOARD	0x2


/* mouse button states received from SP */
#define REMOTE_MOUSE_DOUBLE_CLICK	0xF0
#define REMOTE_MOUSE_BUTTON_LEFT	0x01
#define REMOTE_MOUSE_BUTTON_MIDDLE	0x02
#define REMOTE_MOUSE_BUTTON_RIGHT	0x04


struct mouse_input {
	unsigned short	y;
	unsigned short	x;
};


struct keyboard_input {
	unsigned short	key_code;
	unsigned char	key_flag;
	unsigned char	key_down;
};



struct remote_input { 
	union {
		struct mouse_input	mouse;
		struct keyboard_input	keyboard;
	} data;

	unsigned char	type;
	unsigned char	pad1;
	unsigned char	mouse_buttons;
	unsigned char	pad3;
};

#define mouse_addr(sp) 		sp->base_address + CONDOR_MOUSE_DATA
#define display_width(sp)	mouse_addr(sp) + CONDOR_INPUT_DISPLAY_RESX
#define display_height(sp)	mouse_addr(sp) + CONDOR_INPUT_DISPLAY_RESY
#define display_depth(sp)	mouse_addr(sp) + CONDOR_INPUT_DISPLAY_BITS
#define vnc_status(sp)		mouse_addr(sp) + CONDOR_OUTPUT_VNC_STATUS

#define mouse_interrupt_pending(sp) 	readl(mouse_addr(sp) + CONDOR_MOUSE_ISR_STATUS)
#define clear_mouse_interrupt(sp)	writel(0, mouse_addr(sp) + CONDOR_MOUSE_ISR_STATUS)
#define enable_mouse_interrupts(sp)	writel(1, mouse_addr(sp) + CONDOR_MOUSE_ISR_CONTROL)
#define disable_mouse_interrupts(sp)	writel(0, mouse_addr(sp) + CONDOR_MOUSE_ISR_CONTROL)

/* remote input queue operations */
#define REMOTE_QUEUE_SIZE	60

#define get_queue_writer(sp)	readl(mouse_addr(sp) + CONDOR_MOUSE_Q_WRITER)
#define get_queue_reader(sp)	readl(mouse_addr(sp) + CONDOR_MOUSE_Q_READER)
#define set_queue_reader(sp, reader)	writel(reader, mouse_addr(sp) + CONDOR_MOUSE_Q_READER)

#define queue_begin	mouse_addr(sp) + CONDOR_MOUSE_Q_BEGIN

#define get_queue_entry(sp, read_index) \
	queue_begin + read_index * sizeof(struct remote_input)

static inline int advance_queue_reader(struct service_processor *sp, unsigned long reader)
{
	reader++;
	if (reader == REMOTE_QUEUE_SIZE)
		reader = 0;

	set_queue_reader(sp, reader);
	return reader;
}

#endif /* _IBMASM_REMOTE_H_ */
