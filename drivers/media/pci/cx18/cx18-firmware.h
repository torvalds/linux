/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  cx18 firmware functions
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 */

int cx18_firmware_init(struct cx18 *cx);
void cx18_halt_firmware(struct cx18 *cx);
void cx18_init_memory(struct cx18 *cx);
void cx18_init_power(struct cx18 *cx, int lowpwr);
