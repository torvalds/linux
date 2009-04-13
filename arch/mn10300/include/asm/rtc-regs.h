/* MN10300 on-chip Real-Time Clock registers
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_RTC_REGS_H
#define _ASM_RTC_REGS_H

#include <asm/intctl-regs.h>

#ifdef __KERNEL__

#define RTSCR			__SYSREG(0xd8600000, u8) /* RTC seconds count reg */
#define RTSAR			__SYSREG(0xd8600001, u8) /* RTC seconds alarm reg */
#define RTMCR			__SYSREG(0xd8600002, u8) /* RTC minutes count reg */
#define RTMAR			__SYSREG(0xd8600003, u8) /* RTC minutes alarm reg */
#define RTHCR			__SYSREG(0xd8600004, u8) /* RTC hours count reg */
#define RTHAR			__SYSREG(0xd8600005, u8) /* RTC hours alarm reg */
#define RTDWCR			__SYSREG(0xd8600006, u8) /* RTC day of the week count reg */
#define RTDMCR			__SYSREG(0xd8600007, u8) /* RTC days count reg */
#define RTMTCR			__SYSREG(0xd8600008, u8) /* RTC months count reg */
#define RTYCR			__SYSREG(0xd8600009, u8) /* RTC years count reg */

#define RTCRA			__SYSREG(0xd860000a, u8)/* RTC control reg A */
#define RTCRA_RS		0x0f	/* periodic timer interrupt cycle setting */
#define RTCRA_RS_NONE		0x00	/* - off */
#define RTCRA_RS_3_90625ms	0x01	/* - 3.90625ms	(1/256s) */
#define RTCRA_RS_7_8125ms	0x02	/* - 7.8125ms	(1/128s) */
#define RTCRA_RS_122_070us	0x03	/* - 122.070us	(1/8192s) */
#define RTCRA_RS_244_141us	0x04	/* - 244.141us	(1/4096s) */
#define RTCRA_RS_488_281us	0x05	/* - 488.281us	(1/2048s) */
#define RTCRA_RS_976_5625us	0x06	/* - 976.5625us	(1/1024s) */
#define RTCRA_RS_1_953125ms	0x07	/* - 1.953125ms	(1/512s) */
#define RTCRA_RS_3_90624ms	0x08	/* - 3.90624ms	(1/256s) */
#define RTCRA_RS_7_8125ms_b	0x09	/* - 7.8125ms	(1/128s) */
#define RTCRA_RS_15_625ms	0x0a	/* - 15.625ms	(1/64s) */
#define RTCRA_RS_31_25ms	0x0b	/* - 31.25ms	(1/32s) */
#define RTCRA_RS_62_5ms		0x0c	/* - 62.5ms	(1/16s) */
#define RTCRA_RS_125ms		0x0d	/* - 125ms	(1/8s) */
#define RTCRA_RS_250ms		0x0e	/* - 250ms	(1/4s) */
#define RTCRA_RS_500ms		0x0f	/* - 500ms	(1/2s) */
#define RTCRA_DVR		0x40	/* divider reset */
#define RTCRA_UIP		0x80	/* clock update flag */

#define RTCRB			__SYSREG(0xd860000b, u8) /* RTC control reg B */
#define RTCRB_DSE		0x01	/* daylight savings time enable */
#define RTCRB_TM		0x02	/* time format */
#define RTCRB_TM_12HR		0x00	/* - 12 hour format */
#define RTCRB_TM_24HR		0x02	/* - 24 hour format */
#define RTCRB_DM		0x04	/* numeric value format */
#define RTCRB_DM_BCD		0x00	/* - BCD */
#define RTCRB_DM_BINARY		0x04	/* - binary */
#define RTCRB_UIE		0x10	/* update interrupt disable */
#define RTCRB_AIE		0x20	/* alarm interrupt disable */
#define RTCRB_PIE		0x40	/* periodic interrupt disable */
#define RTCRB_SET		0x80	/* clock update enable */

#define RTSRC			__SYSREG(0xd860000c, u8) /* RTC status reg C */
#define RTSRC_UF		0x10	/* update end interrupt flag */
#define RTSRC_AF		0x20	/* alarm interrupt flag */
#define RTSRC_PF		0x40	/* periodic interrupt flag */
#define RTSRC_IRQF		0x80	/* interrupt flag */

#define RTIRQ			32
#define RTICR			GxICR(RTIRQ)

/*
 * MC146818 RTC compatibility defs for the MN10300 on-chip RTC
 */
#define RTC_PORT(x)		0xd8600000
#define RTC_ALWAYS_BCD		1	/* RTC operates in binary mode */

#define CMOS_READ(addr)		__SYSREG(0xd8600000 + (addr), u8)
#define CMOS_WRITE(val, addr)	\
	do { __SYSREG(0xd8600000 + (addr), u8) = val; } while (0)

#define RTC_IRQ			RTIRQ

#endif /* __KERNEL__ */

#endif /* _ASM_RTC_REGS_H */
