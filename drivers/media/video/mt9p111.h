/*
 * Driver for MT9P111 CMOS Image Sensor from Aptina
 *
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MT9P111_H__
#define __MT9P111_H__
struct reginfo
{
    u16 reg;
    u16 val;
	u16 reg_len;
	u16 rev;
};

#define WORD_LEN             0x04
#define BYTE_LEN             0x02

#define SEQUENCE_INIT        0x00
#define SEQUENCE_NORMAL      0x01
#define SEQUENCE_CAPTURE     0x02
#define SEQUENCE_PREVIEW     0x03

#define SEQUENCE_PROPERTY    0xFFFC
#define SEQUENCE_WAIT_MS     0xFFFD
#define SEQUENCE_WAIT_US     0xFFFE
#define SEQUENCE_END	     0xFFFF

/*configure register for flipe and mirror during initial*/
#define CONFIG_SENSOR_FLIPE     0
#define CONFIG_SENSOR_MIRROR    0
#define CONFIG_SENSOR_MIRROR_AND_FLIPE  1
#define CONFIG_SENSOR_NONE_FLIP_MIRROR  0
/**adjust part parameter to solve bug******/
#define ADJUST_FOR_720P_FALG      1
#define ADJUST_FOR_VGA_FALG       1
#define ADJUST_FOR_CAPTURE_FALG   1
#define ADJUST_PCLK_FRE_FALG      1
/**optimize code to shoten open time******/
#define ADJUST_OPTIMIZE_TIME_FALG      1


#endif
