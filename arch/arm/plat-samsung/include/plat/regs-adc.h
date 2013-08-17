/* arch/arm/mach-s3c2410/include/mach/regs-adc.h
 *
 * Copyright (c) 2004 Shannon Holland <holland@loser.net>
 *
 * This program is free software; yosu can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 ADC registers
*/

#ifndef __ASM_ARCH_REGS_ADC_H
#define __ASM_ARCH_REGS_ADC_H "regs-adc.h"

#define S3C2410_ADCREG(x) (x)

#define S3C2410_ADCCON	   S3C2410_ADCREG(0x00)
#define S3C2410_ADCTSC	   S3C2410_ADCREG(0x04)
#define S3C2410_ADCDLY	   S3C2410_ADCREG(0x08)
#define S3C2410_ADCDAT0	   S3C2410_ADCREG(0x0C)
#define S3C2410_ADCDAT1	   S3C2410_ADCREG(0x10)
#define S3C64XX_ADCUPDN		S3C2410_ADCREG(0x14)
#define S3C2443_ADCMUX		S3C2410_ADCREG(0x18)
#define S3C64XX_ADCCLRINT	S3C2410_ADCREG(0x18)
#define S5P_ADCMUX		S3C2410_ADCREG(0x1C)
#define S3C64XX_ADCCLRINTPNDNUP	S3C2410_ADCREG(0x20)


/* ADCCON Register Bits */
#define S3C64XX_ADCCON_RESSEL		(1<<16)
#define S3C2410_ADCCON_ECFLG		(1<<15)
#define S3C2410_ADCCON_PRSCEN		(1<<14)
#define S3C2410_ADCCON_PRSCVL(x)	(((x)&0xFF)<<6)
#define S3C2410_ADCCON_PRSCVLMASK	(0xFF<<6)
#define S3C2410_ADCCON_SELMUX(x)	(((x)&0x7)<<3)
#define S3C2410_ADCCON_MUXMASK		(0x7<<3)
#define S3C2416_ADCCON_RESSEL		(1 << 3)
#define S3C2410_ADCCON_STDBM		(1<<2)
#define S3C2410_ADCCON_READ_START	(1<<1)
#define S3C2410_ADCCON_ENABLE_START	(1<<0)
#define S3C2410_ADCCON_STARTMASK	(0x3<<0)


/* ADCTSC Register Bits */
#define S3C2443_ADCTSC_UD_SEN		(1 << 8)
#define S3C2410_ADCTSC_YM_SEN		(1<<7)
#define S3C2410_ADCTSC_YP_SEN		(1<<6)
#define S3C2410_ADCTSC_XM_SEN		(1<<5)
#define S3C2410_ADCTSC_XP_SEN		(1<<4)
#define S3C2410_ADCTSC_PULL_UP_DISABLE	(1<<3)
#define S3C2410_ADCTSC_AUTO_PST		(1<<2)
#define S3C2410_ADCTSC_XY_PST(x)	(((x)&0x3)<<0)

/* ADCDAT0 Bits */
#define S3C2410_ADCDAT0_UPDOWN		(1<<15)
#define S3C2410_ADCDAT0_AUTO_PST	(1<<14)
#define S3C2410_ADCDAT0_XY_PST		(0x3<<12)
#define S3C2410_ADCDAT0_XPDATA_MASK	(0x03FF)

/* ADCDAT1 Bits */
#define S3C2410_ADCDAT1_UPDOWN		(1<<15)
#define S3C2410_ADCDAT1_AUTO_PST	(1<<14)
#define S3C2410_ADCDAT1_XY_PST		(0x3<<12)
#define S3C2410_ADCDAT1_YPDATA_MASK	(0x03FF)

/* ADCDLY Register Bits */
#define S3C2410_ADCDLY_DELAY(x)		(((x)&0xFFFF)<<0)


/* 2nd ADC HW in EXYNOS series */
#define SAMSUNG_ADC2_CON1	(0x00)
#define SAMSUNG_ADC2_CON2	(0x04)
#define SAMSUNG_ADC2_STATUS	(0x08)
#define SAMSUNG_ADC2_DAT	(0x0c)
#define SAMSUNG_ADC2_INT_EN	(0x10)
#define SAMSUNG_ADC2_INT_STATUS	(0x14)
#define SAMSUNG_ADC2_VERSION	(0x20)

#define SAMSUNG_ADC2_CON1_SOFT_RESET	(0x2<<1)
#define SAMSUNG_ADC2_CON1_STC_EN	(0x1<<0)

#define SAMSUNG_ADC2_CON2_OSEL		(0x1<<10)
#define SAMSUNG_ADC2_CON2_ESEL		(0x1<<9)
#define SAMSUNG_ADC2_CON2_HIGHF		(0x1<<8)
#define SAMSUNG_ADC2_CON2_C_TIME(x)	(((x)&7)<<4)
#define SAMSUNG_ADC2_CON2_ACH_SEL(x)	(((x)&0xf)<<0)
#define SAMSUNG_ADC2_CON2_ACH_MASK	(0xf<<0)

#define SAMSUNG_ADC2_STATUS_FLAG	(1<<2)

#endif /* __ASM_ARCH_REGS_ADC_H */


