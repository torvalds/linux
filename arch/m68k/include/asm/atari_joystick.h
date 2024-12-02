/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_ATARI_JOYSTICK_H
#define _LINUX_ATARI_JOYSTICK_H

/*
 * linux/include/linux/atari_joystick.h
 * header file for Atari Joystick driver
 * by Robert de Vries (robert@and.nl) on 19Jul93
 */

void atari_joystick_interrupt(char*);
int atari_joystick_init(void);
extern int atari_mouse_buttons;

struct joystick_status {
	char		fire;
	char		dir;
	int		ready;
	int		active;
	wait_queue_head_t wait;
};

#endif
