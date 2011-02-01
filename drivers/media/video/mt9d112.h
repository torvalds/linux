/*
 * Driver for MT9P111 CMOS Image Sensor from Aptina
 *
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MT9D112_H__
#define __MT9D112_H__
struct reginfo
{
    u16 reg;
    u16 val;
};

#define SEQUENCE_INIT        0x00
#define SEQUENCE_NORMAL      0x01
#define SEQUENCE_CAPTURE     0x02
#define SEQUENCE_PREVIEW     0x03

#define SEQUENCE_PROPERTY    0xFFFC
#define SEQUENCE_WAIT_MS     0xFFFD
#define SEQUENCE_WAIT_US     0xFFFE
#define SEQUENCE_END	     0xFFFF
#endif

