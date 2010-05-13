/*drivers/rtc/rtc-HYM8563.h - driver for HYM8563
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
 
#ifndef _DRIVERS_HYM8563_H
#define _DRIVERS_HYM8563_H
 
#define   RTC_CTL1		0x00
#define   RTC_CTL2		0x01
#define   RTC_SEC		0x02
#define   RTC_MIN		0x03
#define   RTC_HOUR		0x04
#define   RTC_DAY		0x05
#define   RTC_WEEK		0x06
#define   RTC_MON		0x07
#define   RTC_YEAR		0x08
#define   RTC_A_MIN 	0x09
#define   RTC_A_HOUR	0x0A
#define   RTC_A_DAY 	0x0B
#define   RTC_A_WEEK	0x0C
#define   RTC_CLKOUT	0x0D
#define   RTC_T_CTL 	0x0E
#define   RTC_T_COUNT	0x0F
#define   CENTURY	0x80
#define   TI		0x10
#define   AF		0x08
#define   TF		0x04
#define   AIE		0x02
#define   TIE		0x01
#define   FE		0x80
#define   TE		0x80
#define   FD1		0x02
#define   FD0		0x01
#define   TD1		0x02
#define   TD0		0x01
#define   VL		0x80

#define HYM8563_REG_LEN 	0x10
#define HYM8563_RTC_SECTION_LEN	0x07

struct hym8563_platform_data {
    unsigned int speed;
    unsigned int mode;
    unsigned int reg_byte_cnt;
};

#endif  /*_DRIVERS_HYM8563_H*/
