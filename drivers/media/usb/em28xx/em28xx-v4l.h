/* SPDX-License-Identifier: GPL-2.0 */
/*
 * em28xx-video.c - driver for Empia EM2800/EM2820/2840 USB
 *		    video capture devices
 *
 * Copyright (C) 2013-2014 Mauro Carvalho Chehab <mchehab+samsung@kernel.org>
 */

int em28xx_start_analog_streaming(struct vb2_queue *vq, unsigned int count);
void em28xx_stop_vbi_streaming(struct vb2_queue *vq);
extern const struct vb2_ops em28xx_vbi_qops;
