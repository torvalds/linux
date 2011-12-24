/* arch/arm/mach-s3c2410/include/mach/regs-rtc.h
 *
 * Copyright (c) 2003 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 Internal RTC register definition
*/

#ifndef __ASM_ARCH_REGS_RTC_H
#define __ASM_ARCH_REGS_RTC_H __FILE__

#define S3C2410_RTCREG(x) (x)
#define S3C2410_INTP		S3C2410_RTCREG(0x30)
#define S3C2410_INTP_ALM	(1 << 1)
#define S3C2410_INTP_TIC	(1 << 0)

#define S3C2410_RTCCON		S3C2410_RTCREG(0x40)
#define S3C2410_RTCCON_RTCEN	(1 << 0)
#define S3C2410_RTCCON_CNTSEL	(1 << 2)
#define S3C2410_RTCCON_CLKRST	(1 << 3)
#define S3C64XX_RTCCON_TICEN	(1 << 8)

#define S3C2410_TICNT		S3C2410_RTCREG(0x44)
#define S3C2410_TICNT_ENABLE	(1 << 7)

#define S3C2410_RTCALM		S3C2410_RTCREG(0x50)
#define S3C2410_RTCALM_ALMEN	(1 << 6)
#define S3C2410_RTCALM_YEAREN	(1 << 5)
#define S3C2410_RTCALM_MONEN	(1 << 4)
#define S3C2410_RTCALM_DAYEN	(1 << 3)
#define S3C2410_RTCALM_HOUREN	(1 << 2)
#define S3C2410_RTCALM_MINEN	(1 << 1)
#define S3C2410_RTCALM_SECEN	(1 << 0)

#define S3C2410_ALMSEC		S3C2410_RTCREG(0x54)
#define S3C2410_ALMMIN		S3C2410_RTCREG(0x58)
#define S3C2410_ALMHOUR		S3C2410_RTCREG(0x5c)

#define S3C2410_ALMDATE		S3C2410_RTCREG(0x60)
#define S3C2410_ALMMON		S3C2410_RTCREG(0x64)
#define S3C2410_ALMYEAR		S3C2410_RTCREG(0x68)

#define S3C2410_RTCSEC		S3C2410_RTCREG(0x70)
#define S3C2410_RTCMIN		S3C2410_RTCREG(0x74)
#define S3C2410_RTCHOUR		S3C2410_RTCREG(0x78)
#define S3C2410_RTCDATE		S3C2410_RTCREG(0x7c)
#define S3C2410_RTCMON		S3C2410_RTCREG(0x84)
#define S3C2410_RTCYEAR		S3C2410_RTCREG(0x88)

#endif /* __ASM_ARCH_REGS_RTC_H */
