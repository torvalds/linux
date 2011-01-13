/* linux/arch/arm/plat-s3c64xx/include/mach/regs-gpio-memport.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64XX - GPIO memory port register definitions
 */

#ifndef __ASM_PLAT_S3C64XX_REGS_GPIO_MEMPORT_H
#define __ASM_PLAT_S3C64XX_REGS_GPIO_MEMPORT_H __FILE__

#define S3C64XX_MEM0CONSTOP	S3C64XX_GPIOREG(0x1B0)
#define S3C64XX_MEM1CONSTOP	S3C64XX_GPIOREG(0x1B4)

#define S3C64XX_MEM0CONSLP0	S3C64XX_GPIOREG(0x1C0)
#define S3C64XX_MEM0CONSLP1	S3C64XX_GPIOREG(0x1C4)
#define S3C64XX_MEM1CONSLP	S3C64XX_GPIOREG(0x1C8)

#define S3C64XX_MEM0DRVCON	S3C64XX_GPIOREG(0x1D0)
#define S3C64XX_MEM1DRVCON	S3C64XX_GPIOREG(0x1D4)

#endif /* __ASM_PLAT_S3C64XX_REGS_GPIO_MEMPORT_H */

