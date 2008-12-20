/*
    camera.h - PXA camera driver header file

    Copyright (C) 2003, Intel Corporation
    Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __ASM_ARCH_CAMERA_H_
#define __ASM_ARCH_CAMERA_H_

#define PXA_CAMERA_MASTER	1
#define PXA_CAMERA_DATAWIDTH_4	2
#define PXA_CAMERA_DATAWIDTH_5	4
#define PXA_CAMERA_DATAWIDTH_8	8
#define PXA_CAMERA_DATAWIDTH_9	0x10
#define PXA_CAMERA_DATAWIDTH_10	0x20
#define PXA_CAMERA_PCLK_EN	0x40
#define PXA_CAMERA_MCLK_EN	0x80
#define PXA_CAMERA_PCP		0x100
#define PXA_CAMERA_HSP		0x200
#define PXA_CAMERA_VSP		0x400

struct pxacamera_platform_data {
	int (*init)(struct device *);

	unsigned long flags;
	unsigned long mclk_10khz;
};

extern void pxa_set_camera_info(struct pxacamera_platform_data *);

#endif /* __ASM_ARCH_CAMERA_H_ */
