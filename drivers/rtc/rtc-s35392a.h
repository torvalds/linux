/*drivers/rtc/rtc-s35392a.h - driver for s35392a
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
 #ifndef RTC_S35392A_H_
 #define RTC_S35392A_H_
 
#define S35392A_CMD_STATUS1	0x0
#define S35392A_CMD_STATUS2	0x1
#define S35392A_CMD_TIME1		0x2
#define S35392A_CMD_TIME2		0x3
#define S35392A_CMD_INT1   		0x4
#define S35392A_CMD_INT2  		0x5
#define S35392A_CMD_CHECK   	0x6
#define S35392A_CMD_FREE		0x7

#define S35392A_BYTE_YEAR		0
#define S35392A_BYTE_MONTH	1
#define S35392A_BYTE_DAY		2
#define S35392A_BYTE_WDAY		3
#define S35392A_BYTE_HOURS	4
#define S35392A_BYTE_MINS		5
#define S35392A_BYTE_SECS		6

#define S35392A_ALARM_WDAYS    0
#define S35392A_ALARM_HOURS    1
#define S35392A_ALARM_MINS 	2

#define S35392A_FLAG_POC		0x01
#define S35392A_FLAG_BLD		0x02
#define S35392A_FLAG_INT2   		0x04
#define S35392A_FLAG_INT1   		0x08
#define S35392A_FLAG_24H		0x40

#define S35392A_FLAG_TEST		0x01
#define S35392A_FLAG_INT1AE      0x20
#define S35392A_FLAG_RESET		0x80
#define S35392A_FLAG_INT2AE      0x2

#define S35392A_ALARM_ENABLE   0X80
#define S35392A_ALARM_DISABLE  0X7F
#define S35392A_MASK_INT1         0XE0
#define S35392A_INT1_ENABLE      0X20
#define S35392A_MASK_INT2         0X0E
#define S35392A_INT2_ENABLE      0X02

//#define S35392_STATUS_INT1       (~(0x3<<5))
//#define S35392_STATUS_INT2       (~(0x3<<1))
#endif

