
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
#define REMOTE_DOUBLE_CLICK	0xF0
#define REMOTE_BUTTON_LEFT	0x01
#define REMOTE_BUTTON_MIDDLE	0x02
#define REMOTE_BUTTON_RIGHT	0x04

/* size of keysym/keycode translation matricies */
#define XLATE_SIZE 256

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

#define mouse_addr(sp)		(sp->base_address + CONDOR_MOUSE_DATA)
#define display_width(sp)	(mouse_addr(sp) + CONDOR_INPUT_DISPLAY_RESX)
#define display_height(sp)	(mouse_addr(sp) + CONDOR_INPUT_DISPLAY_RESY)
#define display_depth(sp)	(mouse_addr(sp) + CONDOR_INPUT_DISPLAY_BITS)
#define desktop_info(sp)	(mouse_addr(sp) + CONDOR_INPUT_DESKTOP_INFO)
#define vnc_status(sp)		(mouse_addr(sp) + CONDOR_OUTPUT_VNC_STATUS)
#define isr_control(sp)		(mouse_addr(sp) + CONDOR_MOUSE_ISR_CONTROL)

#define mouse_interrupt_pending(sp)	readl(mouse_addr(sp) + CONDOR_MOUSE_ISR_STATUS)
#define clear_mouse_interrupt(sp)	writel(0, mouse_addr(sp) + CONDOR_MOUSE_ISR_STATUS)
#define enable_mouse_interrupts(sp)	writel(1, mouse_addr(sp) + CONDOR_MOUSE_ISR_CONTROL)
#define disable_mouse_interrupts(sp)	writel(0, mouse_addr(sp) + CONDOR_MOUSE_ISR_CONTROL)

/* remote input queue operations */
#define REMOTE_QUEUE_SIZE	60

#define get_queue_writer(sp)	readl(mouse_addr(sp) + CONDOR_MOUSE_Q_WRITER)
#define get_queue_reader(sp)	readl(mouse_addr(sp) + CONDOR_MOUSE_Q_READER)
#define set_queue_reader(sp, reader)	writel(reader, mouse_addr(sp) + CONDOR_MOUSE_Q_READER)

#define queue_begin	(mouse_addr(sp) + CONDOR_MOUSE_Q_BEGIN)

#define get_queue_entry(sp, read_index) \
	((void*)(queue_begin + read_index * sizeof(struct remote_input)))

static inline int advance_queue_reader(struct service_processor *sp, unsigned long reader)
{
	reader++;
	if (reader == REMOTE_QUEUE_SIZE)
		reader = 0;

	set_queue_reader(sp, reader);
	return reader;
}

#define NO_KEYCODE 0
#define KEY_SYM_BK_SPC   0xFF08
#define KEY_SYM_TAB      0xFF09
#define KEY_SYM_ENTER    0xFF0D
#define KEY_SYM_SCR_LOCK 0xFF14
#define KEY_SYM_ESCAPE   0xFF1B
#define KEY_SYM_HOME     0xFF50
#define KEY_SYM_LARROW   0xFF51
#define KEY_SYM_UARROW   0xFF52
#define KEY_SYM_RARROW   0xFF53
#define KEY_SYM_DARROW   0xFF54
#define KEY_SYM_PAGEUP   0xFF55
#define KEY_SYM_PAGEDOWN 0xFF56
#define KEY_SYM_END      0xFF57
#define KEY_SYM_INSERT   0xFF63
#define KEY_SYM_NUM_LOCK 0xFF7F
#define KEY_SYM_KPSTAR   0xFFAA
#define KEY_SYM_KPPLUS   0xFFAB
#define KEY_SYM_KPMINUS  0xFFAD
#define KEY_SYM_KPDOT    0xFFAE
#define KEY_SYM_KPSLASH  0xFFAF
#define KEY_SYM_KPRIGHT  0xFF96
#define KEY_SYM_KPUP     0xFF97
#define KEY_SYM_KPLEFT   0xFF98
#define KEY_SYM_KPDOWN   0xFF99
#define KEY_SYM_KP0      0xFFB0
#define KEY_SYM_KP1      0xFFB1
#define KEY_SYM_KP2      0xFFB2
#define KEY_SYM_KP3      0xFFB3
#define KEY_SYM_KP4      0xFFB4
#define KEY_SYM_KP5      0xFFB5
#define KEY_SYM_KP6      0xFFB6
#define KEY_SYM_KP7      0xFFB7
#define KEY_SYM_KP8      0xFFB8
#define KEY_SYM_KP9      0xFFB9
#define KEY_SYM_F1       0xFFBE      // 1B 5B 5B 41
#define KEY_SYM_F2       0xFFBF      // 1B 5B 5B 42
#define KEY_SYM_F3       0xFFC0      // 1B 5B 5B 43
#define KEY_SYM_F4       0xFFC1      // 1B 5B 5B 44
#define KEY_SYM_F5       0xFFC2      // 1B 5B 5B 45
#define KEY_SYM_F6       0xFFC3      // 1B 5B 31 37 7E
#define KEY_SYM_F7       0xFFC4      // 1B 5B 31 38 7E
#define KEY_SYM_F8       0xFFC5      // 1B 5B 31 39 7E
#define KEY_SYM_F9       0xFFC6      // 1B 5B 32 30 7E
#define KEY_SYM_F10      0xFFC7      // 1B 5B 32 31 7E
#define KEY_SYM_F11      0xFFC8      // 1B 5B 32 33 7E
#define KEY_SYM_F12      0xFFC9      // 1B 5B 32 34 7E
#define KEY_SYM_SHIFT    0xFFE1
#define KEY_SYM_CTRL     0xFFE3
#define KEY_SYM_ALT      0xFFE9
#define KEY_SYM_CAP_LOCK 0xFFE5
#define KEY_SYM_DELETE   0xFFFF
#define KEY_SYM_TILDE    0x60
#define KEY_SYM_BKTIC    0x7E
#define KEY_SYM_ONE      0x31
#define KEY_SYM_BANG     0x21
#define KEY_SYM_TWO      0x32
#define KEY_SYM_AT       0x40
#define KEY_SYM_THREE    0x33
#define KEY_SYM_POUND    0x23
#define KEY_SYM_FOUR     0x34
#define KEY_SYM_DOLLAR   0x24
#define KEY_SYM_FIVE     0x35
#define KEY_SYM_PERCENT  0x25
#define KEY_SYM_SIX      0x36
#define KEY_SYM_CARAT    0x5E
#define KEY_SYM_SEVEN    0x37
#define KEY_SYM_AMPER    0x26
#define KEY_SYM_EIGHT    0x38
#define KEY_SYM_STAR     0x2A
#define KEY_SYM_NINE     0x39
#define KEY_SYM_LPAREN   0x28
#define KEY_SYM_ZERO     0x30
#define KEY_SYM_RPAREN   0x29
#define KEY_SYM_MINUS    0x2D
#define KEY_SYM_USCORE   0x5F
#define KEY_SYM_EQUAL    0x2B
#define KEY_SYM_PLUS     0x3D
#define KEY_SYM_LBRKT    0x5B
#define KEY_SYM_LCURLY   0x7B
#define KEY_SYM_RBRKT    0x5D
#define KEY_SYM_RCURLY   0x7D
#define KEY_SYM_SLASH    0x5C
#define KEY_SYM_PIPE     0x7C
#define KEY_SYM_TIC      0x27
#define KEY_SYM_QUOTE    0x22
#define KEY_SYM_SEMIC    0x3B
#define KEY_SYM_COLON    0x3A
#define KEY_SYM_COMMA    0x2C
#define KEY_SYM_LT       0x3C
#define KEY_SYM_PERIOD   0x2E
#define KEY_SYM_GT       0x3E
#define KEY_SYM_BSLASH   0x2F
#define KEY_SYM_QMARK    0x3F
#define KEY_SYM_A        0x41
#define KEY_SYM_B        0x42
#define KEY_SYM_C        0x43
#define KEY_SYM_D        0x44
#define KEY_SYM_E        0x45
#define KEY_SYM_F        0x46
#define KEY_SYM_G        0x47
#define KEY_SYM_H        0x48
#define KEY_SYM_I        0x49
#define KEY_SYM_J        0x4A
#define KEY_SYM_K        0x4B
#define KEY_SYM_L        0x4C
#define KEY_SYM_M        0x4D
#define KEY_SYM_N        0x4E
#define KEY_SYM_O        0x4F
#define KEY_SYM_P        0x50
#define KEY_SYM_Q        0x51
#define KEY_SYM_R        0x52
#define KEY_SYM_S        0x53
#define KEY_SYM_T        0x54
#define KEY_SYM_U        0x55
#define KEY_SYM_V        0x56
#define KEY_SYM_W        0x57
#define KEY_SYM_X        0x58
#define KEY_SYM_Y        0x59
#define KEY_SYM_Z        0x5A
#define KEY_SYM_a        0x61
#define KEY_SYM_b        0x62
#define KEY_SYM_c        0x63
#define KEY_SYM_d        0x64
#define KEY_SYM_e        0x65
#define KEY_SYM_f        0x66
#define KEY_SYM_g        0x67
#define KEY_SYM_h        0x68
#define KEY_SYM_i        0x69
#define KEY_SYM_j        0x6A
#define KEY_SYM_k        0x6B
#define KEY_SYM_l        0x6C
#define KEY_SYM_m        0x6D
#define KEY_SYM_n        0x6E
#define KEY_SYM_o        0x6F
#define KEY_SYM_p        0x70
#define KEY_SYM_q        0x71
#define KEY_SYM_r        0x72
#define KEY_SYM_s        0x73
#define KEY_SYM_t        0x74
#define KEY_SYM_u        0x75
#define KEY_SYM_v        0x76
#define KEY_SYM_w        0x77
#define KEY_SYM_x        0x78
#define KEY_SYM_y        0x79
#define KEY_SYM_z        0x7A
#define KEY_SYM_SPACE    0x20
#endif /* _IBMASM_REMOTE_H_ */
