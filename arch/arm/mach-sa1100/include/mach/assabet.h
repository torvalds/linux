/*
 * arch/arm/mach-sa1100/include/mach/assabet.h
 *
 * Created 2000/06/05 by Nicolas Pitre <nico@fluxnic.net>
 *
 * This file contains the hardware specific definitions for Assabet
 * Only include this file from SA1100-specific files.
 *
 * 2000/05/23 John Dorsey <john+@cs.cmu.edu>
 *      Definitions for Neponset added.
 */
#ifndef __ASM_ARCH_ASSABET_H
#define __ASM_ARCH_ASSABET_H


/* System Configuration Register flags */

#define ASSABET_SCR_SDRAM_LOW	(1<<2)	/* SDRAM size (low bit) */
#define ASSABET_SCR_SDRAM_HIGH	(1<<3)	/* SDRAM size (high bit) */
#define ASSABET_SCR_FLASH_LOW	(1<<4)	/* Flash size (low bit) */
#define ASSABET_SCR_FLASH_HIGH	(1<<5)	/* Flash size (high bit) */
#define ASSABET_SCR_GFX		(1<<8)	/* Graphics Accelerator (0 = present) */
#define ASSABET_SCR_SA1111	(1<<9)	/* Neponset (0 = present) */

#define ASSABET_SCR_INIT	-1

extern unsigned long SCR_value;

#ifdef CONFIG_ASSABET_NEPONSET
#define machine_has_neponset()  ((SCR_value & ASSABET_SCR_SA1111) == 0)
#else
#define machine_has_neponset()	(0)
#endif

/* Board Control Register */

#define ASSABET_BCR_BASE  0xf1000000
#define ASSABET_BCR (*(volatile unsigned int *)(ASSABET_BCR_BASE))

#define ASSABET_BCR_CF_PWR	(1<<0)	/* Compact Flash Power (1 = 3.3v, 0 = off) */
#define ASSABET_BCR_CF_RST	(1<<1)	/* Compact Flash Reset (1 = power up reset) */
#define ASSABET_BCR_GFX_RST	(1<<1)	/* Graphics Accelerator Reset (0 = hold reset) */
#define ASSABET_BCR_CODEC_RST	(1<<2)	/* 0 = Holds UCB1300, ADI7171, and UDA1341 in reset */
#define ASSABET_BCR_IRDA_FSEL	(1<<3)	/* IRDA Frequency select (0 = SIR, 1 = MIR/ FIR) */
#define ASSABET_BCR_IRDA_MD0	(1<<4)	/* Range/Power select */
#define ASSABET_BCR_IRDA_MD1	(1<<5)	/* Range/Power select */
#define ASSABET_BCR_STEREO_LB	(1<<6)	/* Stereo Loopback */
#define ASSABET_BCR_CF_BUS_OFF	(1<<7)	/* Compact Flash bus (0 = on, 1 = off (float)) */
#define ASSABET_BCR_AUDIO_ON	(1<<8)	/* Audio power on */
#define ASSABET_BCR_LIGHT_ON	(1<<9)	/* Backlight */
#define ASSABET_BCR_LCD_12RGB	(1<<10)	/* 0 = 16RGB, 1 = 12RGB */
#define ASSABET_BCR_LCD_ON	(1<<11)	/* LCD power on */
#define ASSABET_BCR_RS232EN	(1<<12)	/* RS232 transceiver enable */
#define ASSABET_BCR_LED_RED	(1<<13)	/* D9 (0 = on, 1 = off) */
#define ASSABET_BCR_LED_GREEN	(1<<14)	/* D8 (0 = on, 1 = off) */
#define ASSABET_BCR_VIB_ON	(1<<15)	/* Vibration motor (quiet alert) */
#define ASSABET_BCR_COM_DTR	(1<<16)	/* COMport Data Terminal Ready */
#define ASSABET_BCR_COM_RTS	(1<<17)	/* COMport Request To Send */
#define ASSABET_BCR_RAD_WU	(1<<18)	/* Radio wake up interrupt */
#define ASSABET_BCR_SMB_EN	(1<<19)	/* System management bus enable */
#define ASSABET_BCR_TV_IR_DEC	(1<<20)	/* TV IR Decode Enable (not implemented) */
#define ASSABET_BCR_QMUTE	(1<<21)	/* Quick Mute */
#define ASSABET_BCR_RAD_ON	(1<<22)	/* Radio Power On */
#define ASSABET_BCR_SPK_OFF	(1<<23)	/* 1 = Speaker amplifier power off */

#ifdef CONFIG_SA1100_ASSABET
extern void ASSABET_BCR_frob(unsigned int mask, unsigned int set);
#else
#define ASSABET_BCR_frob(x,y)	do { } while (0)
#endif

#define ASSABET_BCR_set(x)	ASSABET_BCR_frob((x), (x))
#define ASSABET_BCR_clear(x)	ASSABET_BCR_frob((x), 0)

#define ASSABET_BSR_BASE	0xf1000000
#define ASSABET_BSR (*(volatile unsigned int*)(ASSABET_BSR_BASE))

#define ASSABET_BSR_RS232_VALID	(1 << 24)
#define ASSABET_BSR_COM_DCD	(1 << 25)
#define ASSABET_BSR_COM_CTS	(1 << 26)
#define ASSABET_BSR_COM_DSR	(1 << 27)
#define ASSABET_BSR_RAD_CTS	(1 << 28)
#define ASSABET_BSR_RAD_DSR	(1 << 29)
#define ASSABET_BSR_RAD_DCD	(1 << 30)
#define ASSABET_BSR_RAD_RI	(1 << 31)


/* GPIOs (bitmasks) for which the generic definition doesn't say much */
#define ASSABET_GPIO_RADIO_IRQ		GPIO_GPIO (14)	/* Radio interrupt request  */
#define ASSABET_GPIO_PS_MODE_SYNC	GPIO_GPIO (16)	/* Power supply mode/sync   */
#define ASSABET_GPIO_STEREO_64FS_CLK	GPIO_GPIO (19)	/* SSP UDA1341 clock input  */
#define ASSABET_GPIO_GFX_IRQ		GPIO_GPIO (24)	/* Graphics IRQ */
#define ASSABET_GPIO_BATT_LOW		GPIO_GPIO (26)	/* Low battery */
#define ASSABET_GPIO_RCLK		GPIO_GPIO (26)	/* CCLK/2  */

/* These are gpiolib GPIO numbers, not bitmasks */
#define ASSABET_GPIO_CF_IRQ		21	/* CF IRQ */
#define ASSABET_GPIO_CF_CD		22	/* CF CD  */
#define ASSABET_GPIO_CF_BVD2		24	/* CF BVD / IOSPKR */
#define ASSABET_GPIO_CF_BVD1		25	/* CF BVD / IOSTSCHG */

#endif
