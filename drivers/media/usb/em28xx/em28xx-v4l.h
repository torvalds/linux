/* SPDX-License-Identifier: GPL-2.0 */
/*
 * em28xx-video.c - driver for Empia EM2800/EM2820/2840 USB
 *		    video capture devices
 *
 * Copyright (C) 2013-2014 Mauro Carvalho Chehab <mchehab+samsung@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

int em28xx_start_analog_streaming(struct vb2_queue *vq, unsigned int count);
void em28xx_stop_vbi_streaming(struct vb2_queue *vq);
extern const struct vb2_ops em28xx_vbi_qops;
