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


#define RK28_CAM_DRV_NAME "rk2818-camera"
#define RK28_CAM_PLATFORM_DEV_ID 33

struct rk28camera_gpio_res {
    unsigned int gpio_reset;
    unsigned int gpio_power;
    unsigned int gpio_flag;
    const char *dev_name;
};

struct rk28camera_platform_data {
    int (*init)(void);
    int (*deinit)(void);
    struct rk28camera_gpio_res gpio_res[2];
};

#endif /* __ASM_ARCH_CAMERA_H_ */

