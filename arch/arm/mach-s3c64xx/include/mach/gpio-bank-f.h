/* linux/arch/arm/mach-s3c64xx/include/mach/gpio-bank-f.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 * 	Ben Dooks <ben@simtec.co.uk>
 * 	http://armlinux.simtec.co.uk/
 *
 * GPIO Bank F register and configuration definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#define S3C64XX_GPFCON			(S3C64XX_GPF_BASE + 0x00)
#define S3C64XX_GPFDAT			(S3C64XX_GPF_BASE + 0x04)
#define S3C64XX_GPFPUD			(S3C64XX_GPF_BASE + 0x08)
#define S3C64XX_GPFCONSLP		(S3C64XX_GPF_BASE + 0x0c)
#define S3C64XX_GPFPUDSLP		(S3C64XX_GPF_BASE + 0x10)

#define S3C64XX_GPF_CONMASK(__gpio)	(0x3 << ((__gpio) * 2))
#define S3C64XX_GPF_INPUT(__gpio)	(0x0 << ((__gpio) * 2))
#define S3C64XX_GPF_OUTPUT(__gpio)	(0x1 << ((__gpio) * 2))

#define S3C64XX_GPF0_CAMIF_CLK		(0x02 << 0)
#define S3C64XX_GPF0_EINT_G4_0		(0x03 << 0)

#define S3C64XX_GPF1_CAMIF_HREF		(0x02 << 2)
#define S3C64XX_GPF1_EINT_G4_1		(0x03 << 2)

#define S3C64XX_GPF2_CAMIF_PCLK		(0x02 << 4)
#define S3C64XX_GPF2_EINT_G4_2		(0x03 << 4)

#define S3C64XX_GPF3_CAMIF_nRST		(0x02 << 6)
#define S3C64XX_GPF3_EINT_G4_3		(0x03 << 6)

#define S3C64XX_GPF4_CAMIF_VSYNC	(0x02 << 8)
#define S3C64XX_GPF4_EINT_G4_4		(0x03 << 8)

#define S3C64XX_GPF5_CAMIF_YDATA0	(0x02 << 10)
#define S3C64XX_GPF5_EINT_G4_5		(0x03 << 10)

#define S3C64XX_GPF6_CAMIF_YDATA1	(0x02 << 12)
#define S3C64XX_GPF6_EINT_G4_6		(0x03 << 12)

#define S3C64XX_GPF7_CAMIF_YDATA2	(0x02 << 14)
#define S3C64XX_GPF7_EINT_G4_7		(0x03 << 14)

#define S3C64XX_GPF8_CAMIF_YDATA3	(0x02 << 16)
#define S3C64XX_GPF8_EINT_G4_8		(0x03 << 16)

#define S3C64XX_GPF9_CAMIF_YDATA4	(0x02 << 18)
#define S3C64XX_GPF9_EINT_G4_9		(0x03 << 18)

#define S3C64XX_GPF10_CAMIF_YDATA5	(0x02 << 20)
#define S3C64XX_GPF10_EINT_G4_10	(0x03 << 20)

#define S3C64XX_GPF11_CAMIF_YDATA6	(0x02 << 22)
#define S3C64XX_GPF11_EINT_G4_11	(0x03 << 22)

#define S3C64XX_GPF12_CAMIF_YDATA7	(0x02 << 24)
#define S3C64XX_GPF12_EINT_G4_12	(0x03 << 24)

#define S3C64XX_GPF13_PWM_ECLK		(0x02 << 26)
#define S3C64XX_GPF13_EINT_G4_13	(0x03 << 26)

#define S3C64XX_GPF14_PWM_TOUT0		(0x02 << 28)
#define S3C64XX_GPF14_CLKOUT0		(0x03 << 28)

#define S3C64XX_GPF15_PWM_TOUT1		(0x02 << 30)

