/* AM33v2 on-board timer module registers
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _ASM_TIMER_REGS_H
#define _ASM_TIMER_REGS_H

#include <asm/cpu-regs.h>
#include <asm/intctl-regs.h>

#ifdef __KERNEL__

/*
 * Timer prescalar control
 */
#define	TMPSCNT			__SYSREG(0xd4003071, u8) /* timer prescaler control */
#define	TMPSCNT_ENABLE		0x80	/* timer prescaler enable */
#define	TMPSCNT_DISABLE		0x00	/* timer prescaler disable */

/*
 * 8-bit timers
 */
#define	TM0MD			__SYSREG(0xd4003000, u8) /* timer 0 mode register */
#define	TM0MD_SRC		0x07	/* timer source */
#define	TM0MD_SRC_IOCLK		0x00	/* - IOCLK */
#define	TM0MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM0MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM0MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM0MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#if	defined(CONFIG_AM33_2)
#define	TM0MD_SRC_TM2IO		0x03	/* - TM2IO pin input */
#define	TM0MD_SRC_TM0IO		0x07	/* - TM0IO pin input */
#endif /* CONFIG_AM33_2 */
#define	TM0MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM0MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM1MD			__SYSREG(0xd4003001, u8) /* timer 1 mode register */
#define	TM1MD_SRC		0x07	/* timer source */
#define	TM1MD_SRC_IOCLK		0x00	/* - IOCLK */
#define	TM1MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM1MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM1MD_SRC_TM0CASCADE	0x03	/* - cascade with timer 0 */
#define	TM1MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM1MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#if defined(CONFIG_AM33_2)
#define	TM1MD_SRC_TM1IO		0x07	/* - TM1IO pin input */
#endif	/* CONFIG_AM33_2 */
#define	TM1MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM1MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM2MD			__SYSREG(0xd4003002, u8) /* timer 2 mode register */
#define	TM2MD_SRC		0x07	/* timer source */
#define	TM2MD_SRC_IOCLK		0x00	/* - IOCLK */
#define	TM2MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM2MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM2MD_SRC_TM1CASCADE	0x03	/* - cascade with timer 1 */
#define	TM2MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM2MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#if defined(CONFIG_AM33_2)
#define	TM2MD_SRC_TM2IO		0x07	/* - TM2IO pin input */
#endif	/* CONFIG_AM33_2 */
#define	TM2MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM2MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM3MD			__SYSREG(0xd4003003, u8) /* timer 3 mode register */
#define	TM3MD_SRC		0x07	/* timer source */
#define	TM3MD_SRC_IOCLK		0x00	/* - IOCLK */
#define	TM3MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM3MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM3MD_SRC_TM2CASCADE	0x03	/* - cascade with timer 2 */
#define	TM3MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM3MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM3MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#if defined(CONFIG_AM33_2)
#define	TM3MD_SRC_TM3IO		0x07	/* - TM3IO pin input */
#endif	/* CONFIG_AM33_2 */
#define	TM3MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM3MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM01MD			__SYSREG(0xd4003000, u16)  /* timer 0:1 mode register */

#define	TM0BR			__SYSREG(0xd4003010, u8)   /* timer 0 base register */
#define	TM1BR			__SYSREG(0xd4003011, u8)   /* timer 1 base register */
#define	TM2BR			__SYSREG(0xd4003012, u8)   /* timer 2 base register */
#define	TM3BR			__SYSREG(0xd4003013, u8)   /* timer 3 base register */
#define	TM01BR			__SYSREG(0xd4003010, u16)  /* timer 0:1 base register */

#define	TM0BC			__SYSREGC(0xd4003020, u8)  /* timer 0 binary counter */
#define	TM1BC			__SYSREGC(0xd4003021, u8)  /* timer 1 binary counter */
#define	TM2BC			__SYSREGC(0xd4003022, u8)  /* timer 2 binary counter */
#define	TM3BC			__SYSREGC(0xd4003023, u8)  /* timer 3 binary counter */
#define	TM01BC			__SYSREGC(0xd4003020, u16) /* timer 0:1 binary counter */

#define TM0IRQ			2	/* timer 0 IRQ */
#define TM1IRQ			3	/* timer 1 IRQ */
#define TM2IRQ			4	/* timer 2 IRQ */
#define TM3IRQ			5	/* timer 3 IRQ */

#define	TM0ICR			GxICR(TM0IRQ)	/* timer 0 uflow intr ctrl reg */
#define	TM1ICR			GxICR(TM1IRQ)	/* timer 1 uflow intr ctrl reg */
#define	TM2ICR			GxICR(TM2IRQ)	/* timer 2 uflow intr ctrl reg */
#define	TM3ICR			GxICR(TM3IRQ)	/* timer 3 uflow intr ctrl reg */

/*
 * 16-bit timers 4,5 & 7-15
 */
#define	TM4MD			__SYSREG(0xd4003080, u8)   /* timer 4 mode register */
#define	TM4MD_SRC		0x07	/* timer source */
#define	TM4MD_SRC_IOCLK		0x00	/* - IOCLK */
#define	TM4MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM4MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM4MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM4MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM4MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#if defined(CONFIG_AM33_2)
#define	TM4MD_SRC_TM4IO		0x07	/* - TM4IO pin input */
#endif	/* CONFIG_AM33_2 */
#define	TM4MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM4MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM5MD			__SYSREG(0xd4003082, u8)   /* timer 5 mode register */
#define	TM5MD_SRC		0x07	/* timer source */
#define	TM5MD_SRC_IOCLK		0x00	/* - IOCLK */
#define	TM5MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM5MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM5MD_SRC_TM4CASCADE	0x03	/* - cascade with timer 4 */
#define	TM5MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM5MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM5MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#if defined(CONFIG_AM33_2)
#define	TM5MD_SRC_TM5IO		0x07	/* - TM5IO pin input */
#else	/* !CONFIG_AM33_2 */
#define	TM5MD_SRC_TM7UFLOW	0x07	/* - timer 7 underflow */
#endif	/* CONFIG_AM33_2 */
#define	TM5MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM5MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM7MD			__SYSREG(0xd4003086, u8)   /* timer 7 mode register */
#define	TM7MD_SRC		0x07	/* timer source */
#define	TM7MD_SRC_IOCLK		0x00	/* - IOCLK */
#define	TM7MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM7MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM7MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM7MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM7MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#if defined(CONFIG_AM33_2)
#define	TM7MD_SRC_TM7IO		0x07	/* - TM7IO pin input */
#endif	/* CONFIG_AM33_2 */
#define	TM7MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM7MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM8MD			__SYSREG(0xd4003088, u8)   /* timer 8 mode register */
#define	TM8MD_SRC		0x07	/* timer source */
#define	TM8MD_SRC_IOCLK		0x00	/* - IOCLK */
#define	TM8MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM8MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM8MD_SRC_TM7CASCADE	0x03	/* - cascade with timer 7 */
#define	TM8MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM8MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM8MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#if defined(CONFIG_AM33_2)
#define	TM8MD_SRC_TM8IO		0x07	/* - TM8IO pin input */
#else	/* !CONFIG_AM33_2 */
#define	TM8MD_SRC_TM7UFLOW	0x07	/* - timer 7 underflow */
#endif	/* CONFIG_AM33_2 */
#define	TM8MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM8MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM9MD			__SYSREG(0xd400308a, u8)   /* timer 9 mode register */
#define	TM9MD_SRC		0x07	/* timer source */
#define	TM9MD_SRC_IOCLK		0x00	/* - IOCLK */
#define	TM9MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM9MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM9MD_SRC_TM8CASCADE	0x03	/* - cascade with timer 8 */
#define	TM9MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM9MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM9MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#if defined(CONFIG_AM33_2)
#define	TM9MD_SRC_TM9IO		0x07	/* - TM9IO pin input */
#else	/* !CONFIG_AM33_2 */
#define	TM9MD_SRC_TM7UFLOW	0x07	/* - timer 7 underflow */
#endif	/* CONFIG_AM33_2 */
#define	TM9MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM9MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM10MD			__SYSREG(0xd400308c, u8)   /* timer 10 mode register */
#define	TM10MD_SRC		0x07	/* timer source */
#define	TM10MD_SRC_IOCLK	0x00	/* - IOCLK */
#define	TM10MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM10MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM10MD_SRC_TM9CASCADE	0x03	/* - cascade with timer 9 */
#define	TM10MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM10MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM10MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#if defined(CONFIG_AM33_2)
#define	TM10MD_SRC_TM10IO	0x07	/* - TM10IO pin input */
#else	/* !CONFIG_AM33_2 */
#define	TM10MD_SRC_TM7UFLOW	0x07	/* - timer 7 underflow */
#endif	/* CONFIG_AM33_2 */
#define	TM10MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM10MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM11MD			__SYSREG(0xd400308e, u8)   /* timer 11 mode register */
#define	TM11MD_SRC		0x07	/* timer source */
#define	TM11MD_SRC_IOCLK	0x00	/* - IOCLK */
#define	TM11MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM11MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM11MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM11MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM11MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#if defined(CONFIG_AM33_2)
#define	TM11MD_SRC_TM11IO	0x07	/* - TM11IO pin input */
#else	/* !CONFIG_AM33_2 */
#define	TM11MD_SRC_TM7UFLOW	0x07	/* - timer 7 underflow */
#endif	/* CONFIG_AM33_2 */
#define	TM11MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM11MD_COUNT_ENABLE	0x80	/* timer count enable */

#if defined(CONFIG_AM34_2)
#define	TM12MD			__SYSREG(0xd4003180, u8)   /* timer 11 mode register */
#define	TM12MD_SRC		0x07	/* timer source */
#define	TM12MD_SRC_IOCLK	0x00	/* - IOCLK */
#define	TM12MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM12MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM12MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM12MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM12MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#define	TM12MD_SRC_TM7UFLOW	0x07	/* - timer 7 underflow */
#define	TM12MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM12MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM13MD			__SYSREG(0xd4003182, u8)   /* timer 11 mode register */
#define	TM13MD_SRC		0x07	/* timer source */
#define	TM13MD_SRC_IOCLK	0x00	/* - IOCLK */
#define	TM13MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM13MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM13MD_SRC_TM12CASCADE	0x03	/* - cascade with timer 12 */
#define	TM13MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM13MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM13MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#define	TM13MD_SRC_TM7UFLOW	0x07	/* - timer 7 underflow */
#define	TM13MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM13MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM14MD			__SYSREG(0xd4003184, u8)   /* timer 11 mode register */
#define	TM14MD_SRC		0x07	/* timer source */
#define	TM14MD_SRC_IOCLK	0x00	/* - IOCLK */
#define	TM14MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM14MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM14MD_SRC_TM13CASCADE	0x03	/* - cascade with timer 13 */
#define	TM14MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM14MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM14MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#define	TM14MD_SRC_TM7UFLOW	0x07	/* - timer 7 underflow */
#define	TM14MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM14MD_COUNT_ENABLE	0x80	/* timer count enable */

#define	TM15MD			__SYSREG(0xd4003186, u8)   /* timer 11 mode register */
#define	TM15MD_SRC		0x07	/* timer source */
#define	TM15MD_SRC_IOCLK	0x00	/* - IOCLK */
#define	TM15MD_SRC_IOCLK_8	0x01	/* - 1/8 IOCLK */
#define	TM15MD_SRC_IOCLK_32	0x02	/* - 1/32 IOCLK */
#define	TM15MD_SRC_TM0UFLOW	0x04	/* - timer 0 underflow */
#define	TM15MD_SRC_TM1UFLOW	0x05	/* - timer 1 underflow */
#define	TM15MD_SRC_TM2UFLOW	0x06	/* - timer 2 underflow */
#define	TM15MD_SRC_TM7UFLOW	0x07	/* - timer 7 underflow */
#define	TM15MD_INIT_COUNTER	0x40	/* initialize TMnBC = TMnBR */
#define	TM15MD_COUNT_ENABLE	0x80	/* timer count enable */
#endif	/* CONFIG_AM34_2 */


#define	TM4BR			__SYSREG(0xd4003090, u16)  /* timer 4 base register */
#define	TM5BR			__SYSREG(0xd4003092, u16)  /* timer 5 base register */
#define	TM45BR			__SYSREG(0xd4003090, u32)  /* timer 4:5 base register */
#define	TM7BR			__SYSREG(0xd4003096, u16)  /* timer 7 base register */
#define	TM8BR			__SYSREG(0xd4003098, u16)  /* timer 8 base register */
#define	TM9BR			__SYSREG(0xd400309a, u16)  /* timer 9 base register */
#define	TM89BR			__SYSREG(0xd4003098, u32)  /* timer 8:9 base register */
#define	TM10BR			__SYSREG(0xd400309c, u16)  /* timer 10 base register */
#define	TM11BR			__SYSREG(0xd400309e, u16)  /* timer 11 base register */
#if defined(CONFIG_AM34_2)
#define	TM12BR			__SYSREG(0xd4003190, u16)  /* timer 12 base register */
#define	TM13BR			__SYSREG(0xd4003192, u16)  /* timer 13 base register */
#define	TM14BR			__SYSREG(0xd4003194, u16)  /* timer 14 base register */
#define	TM15BR			__SYSREG(0xd4003196, u16)  /* timer 15 base register */
#endif	/* CONFIG_AM34_2 */

#define	TM4BC			__SYSREG(0xd40030a0, u16)  /* timer 4 binary counter */
#define	TM5BC			__SYSREG(0xd40030a2, u16)  /* timer 5 binary counter */
#define	TM45BC			__SYSREG(0xd40030a0, u32)  /* timer 4:5 binary counter */
#define	TM7BC			__SYSREG(0xd40030a6, u16)  /* timer 7 binary counter */
#define	TM8BC			__SYSREG(0xd40030a8, u16)  /* timer 8 binary counter */
#define	TM9BC			__SYSREG(0xd40030aa, u16)  /* timer 9 binary counter */
#define	TM89BC			__SYSREG(0xd40030a8, u32)  /* timer 8:9 binary counter */
#define	TM10BC			__SYSREG(0xd40030ac, u16)  /* timer 10 binary counter */
#define	TM11BC			__SYSREG(0xd40030ae, u16)  /* timer 11 binary counter */
#if defined(CONFIG_AM34_2)
#define	TM12BC			__SYSREG(0xd40031a0, u16)  /* timer 12 binary counter */
#define	TM13BC			__SYSREG(0xd40031a2, u16)  /* timer 13 binary counter */
#define	TM14BC			__SYSREG(0xd40031a4, u16)  /* timer 14 binary counter */
#define	TM15BC			__SYSREG(0xd40031a6, u16)  /* timer 15 binary counter */
#endif	/* CONFIG_AM34_2 */

#define TM4IRQ			6	/* timer 4 IRQ */
#define TM5IRQ			7	/* timer 5 IRQ */
#define TM7IRQ			11	/* timer 7 IRQ */
#define TM8IRQ			12	/* timer 8 IRQ */
#define TM9IRQ			13	/* timer 9 IRQ */
#define TM10IRQ			14	/* timer 10 IRQ */
#define TM11IRQ			15	/* timer 11 IRQ */
#if defined(CONFIG_AM34_2)
#define TM12IRQ			64	/* timer 12 IRQ */
#define TM13IRQ			65	/* timer 13 IRQ */
#define TM14IRQ			66	/* timer 14 IRQ */
#define TM15IRQ			67	/* timer 15 IRQ */
#endif	/* CONFIG_AM34_2 */

#define	TM4ICR			GxICR(TM4IRQ)	/* timer 4 uflow intr ctrl reg */
#define	TM5ICR			GxICR(TM5IRQ)	/* timer 5 uflow intr ctrl reg */
#define	TM7ICR			GxICR(TM7IRQ)	/* timer 7 uflow intr ctrl reg */
#define	TM8ICR			GxICR(TM8IRQ)	/* timer 8 uflow intr ctrl reg */
#define	TM9ICR			GxICR(TM9IRQ)	/* timer 9 uflow intr ctrl reg */
#define	TM10ICR			GxICR(TM10IRQ)	/* timer 10 uflow intr ctrl reg */
#define	TM11ICR			GxICR(TM11IRQ)	/* timer 11 uflow intr ctrl reg */
#if defined(CONFIG_AM34_2)
#define	TM12ICR			GxICR(TM12IRQ)	/* timer 12 uflow intr ctrl reg */
#define	TM13ICR			GxICR(TM13IRQ)	/* timer 13 uflow intr ctrl reg */
#define	TM14ICR			GxICR(TM14IRQ)	/* timer 14 uflow intr ctrl reg */
#define	TM15ICR			GxICR(TM15IRQ)	/* timer 15 uflow intr ctrl reg */
#endif	/* CONFIG_AM34_2 */

/*
 * 16-bit timer 6
 */
#define	TM6MD			__SYSREG(0xd4003084, u16)  /* timer6 mode register */
#define	TM6MD_SRC		0x0007	/* timer source */
#define	TM6MD_SRC_IOCLK		0x0000	/* - IOCLK */
#define	TM6MD_SRC_IOCLK_8	0x0001	/* - 1/8 IOCLK */
#define	TM6MD_SRC_IOCLK_32	0x0002	/* - 1/32 IOCLK */
#define	TM6MD_SRC_TM0UFLOW	0x0004	/* - timer 0 underflow */
#define	TM6MD_SRC_TM1UFLOW	0x0005	/* - timer 1 underflow */
#define	TM6MD_SRC_TM2UFLOW	0x0006	/* - timer 2 underflow */
#if defined(CONFIG_AM33_2)
/* #define	TM6MD_SRC_TM6IOB_BOTH	0x0006 */	/* - TM6IOB pin input (both edges) */
#define	TM6MD_SRC_TM6IOB_SINGLE	0x0007	/* - TM6IOB pin input (single edge) */
#endif	/* CONFIG_AM33_2 */
#define	TM6MD_ONESHOT_ENABLE	0x0040	/* oneshot count */
#define	TM6MD_CLR_ENABLE	0x0010	/* clear count enable */
#if	defined(CONFIG_AM33_2)
#define	TM6MD_TRIG_ENABLE	0x0080	/* TM6IOB pin trigger enable */
#define TM6MD_PWM		0x3800	/* PWM output mode */
#define TM6MD_PWM_DIS		0x0000	/* - disabled */
#define	TM6MD_PWM_10BIT		0x1000	/* - 10 bits mode */
#define	TM6MD_PWM_11BIT		0x1800	/* - 11 bits mode */
#define	TM6MD_PWM_12BIT		0x3000	/* - 12 bits mode */
#define	TM6MD_PWM_14BIT		0x3800	/* - 14 bits mode */
#endif	/* CONFIG_AM33_2 */

#define	TM6MD_INIT_COUNTER	0x4000	/* initialize TMnBC to zero */
#define	TM6MD_COUNT_ENABLE	0x8000	/* timer count enable */

#define	TM6MDA			__SYSREG(0xd40030b4, u8)   /* timer6 cmp/cap A mode reg */
#define	TM6MDA_MODE_CMP_SINGLE	0x00	/* - compare, single buffer mode */
#define	TM6MDA_MODE_CMP_DOUBLE	0x40	/* - compare, double buffer mode */
#if	defined(CONFIG_AM33_2)
#define TM6MDA_OUT		0x07	/* output select */
#define	TM6MDA_OUT_SETA_RESETB	0x00	/* - set at match A, reset at match B */
#define	TM6MDA_OUT_SETA_RESETOV	0x01	/* - set at match A, reset at overflow */
#define	TM6MDA_OUT_SETA		0x02	/* - set at match A */
#define	TM6MDA_OUT_RESETA	0x03	/* - reset at match A */
#define	TM6MDA_OUT_TOGGLE	0x04	/* - toggle on match A */
#define TM6MDA_MODE		0xc0	/* compare A register mode */
#define	TM6MDA_MODE_CAP_S_EDGE	0x80	/* - capture, single edge mode */
#define	TM6MDA_MODE_CAP_D_EDGE	0xc0	/* - capture, double edge mode */
#define TM6MDA_EDGE		0x20	/* compare A edge select */
#define	TM6MDA_EDGE_FALLING	0x00	/* capture on falling edge */
#define	TM6MDA_EDGE_RISING	0x20	/* capture on rising edge */
#define	TM6MDA_CAPTURE_ENABLE	0x10	/* capture enable */
#else	/* !CONFIG_AM33_2 */
#define	TM6MDA_MODE		0x40	/* compare A register mode */
#endif	/* CONFIG_AM33_2 */

#define	TM6MDB			__SYSREG(0xd40030b5, u8)   /* timer6 cmp/cap B mode reg */
#define	TM6MDB_MODE_CMP_SINGLE	0x00	/* - compare, single buffer mode */
#define	TM6MDB_MODE_CMP_DOUBLE	0x40	/* - compare, double buffer mode */
#if defined(CONFIG_AM33_2)
#define TM6MDB_OUT		0x07	/* output select */
#define	TM6MDB_OUT_SETB_RESETA	0x00	/* - set at match B, reset at match A */
#define	TM6MDB_OUT_SETB_RESETOV	0x01	/* - set at match B */
#define	TM6MDB_OUT_RESETB	0x03	/* - reset at match B */
#define	TM6MDB_OUT_TOGGLE	0x04	/* - toggle on match B */
#define TM6MDB_MODE		0xc0	/* compare B register mode */
#define	TM6MDB_MODE_CAP_S_EDGE	0x80	/* - capture, single edge mode */
#define	TM6MDB_MODE_CAP_D_EDGE	0xc0	/* - capture, double edge mode */
#define TM6MDB_EDGE		0x20	/* compare B edge select */
#define	TM6MDB_EDGE_FALLING	0x00	/* capture on falling edge */
#define	TM6MDB_EDGE_RISING	0x20	/* capture on rising edge */
#define	TM6MDB_CAPTURE_ENABLE	0x10	/* capture enable */
#else	/* !CONFIG_AM33_2 */
#define	TM6MDB_MODE		0x40	/* compare B register mode */
#endif	/* CONFIG_AM33_2 */

#define	TM6CA			__SYSREG(0xd40030c4, u16)   /* timer6 cmp/capture reg A */
#define	TM6CB			__SYSREG(0xd40030d4, u16)   /* timer6 cmp/capture reg B */
#define	TM6BC			__SYSREG(0xd40030a4, u16)   /* timer6 binary counter */

#define TM6IRQ			6	/* timer 6 IRQ */
#define TM6AIRQ			9	/* timer 6A IRQ */
#define TM6BIRQ			10	/* timer 6B IRQ */

#define	TM6ICR			GxICR(TM6IRQ)	/* timer 6 uflow intr ctrl reg */
#define	TM6AICR			GxICR(TM6AIRQ)	/* timer 6A intr control reg */
#define	TM6BICR			GxICR(TM6BIRQ)	/* timer 6B intr control reg */

#if defined(CONFIG_AM34_2)
/*
 * MTM: OS Tick-Timer
 */
#define	TMTMD			__SYSREG(0xd4004100, u8)	/* Tick Timer mode register */
#define	TMTMD_TMTLDE		0x40	/* initialize TMTBC = TMTBR */
#define	TMTMD_TMTCNE		0x80	/* timer count enable       */

#define	TMTBR			__SYSREG(0xd4004110, u32)	/* Tick Timer mode reg */
#define	TMTBC			__SYSREG(0xd4004120, u32)	/* Tick Timer mode reg */

/*
 * MTM: OS Timestamp-Timer
 */
#define	TMSMD			__SYSREG(0xd4004140, u8)	/* Tick Timer mode register */
#define	TMSMD_TMSLDE		0x40		/* initialize TMSBC = TMSBR */
#define	TMSMD_TMSCNE		0x80		/* timer count enable       */

#define	TMSBR			__SYSREG(0xd4004150, u32)	/* Tick Timer mode register */
#define	TMSBC			__SYSREG(0xd4004160, u32)	/* Tick Timer mode register */

#define TMTIRQ			119		/* OS Tick timer   IRQ */
#define TMSIRQ			120		/* Timestamp timer IRQ */

#define	TMTICR			GxICR(TMTIRQ)	/* OS Tick timer   uflow intr ctrl reg */
#define	TMSICR			GxICR(TMSIRQ)	/* Timestamp timer uflow intr ctrl reg */
#endif	/* CONFIG_AM34_2 */

#endif /* __KERNEL__ */

#endif /* _ASM_TIMER_REGS_H */
