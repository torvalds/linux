/* gpio-regs.h: on-chip general purpose I/O registers
 *
 * Copyright (C) 2003 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_GPIO_REGS
#define _ASM_GPIO_REGS

#define __reg(ADDR) (*(volatile unsigned long *)(ADDR))

#define __get_PDR()	({ __reg(0xfeff0400); })
#define __set_PDR(V)	do { __reg(0xfeff0400) = (V); mb(); } while(0)

#define __get_GPDR()	({ __reg(0xfeff0408); })
#define __set_GPDR(V)	do { __reg(0xfeff0408) = (V); mb(); } while(0)

#define __get_SIR()	({ __reg(0xfeff0410); })
#define __set_SIR(V)	do { __reg(0xfeff0410) = (V); mb(); } while(0)

#define __get_SOR()	({ __reg(0xfeff0418); })
#define __set_SOR(V)	do { __reg(0xfeff0418) = (V); mb(); } while(0)

#define __set_PDSR(V)	do { __reg(0xfeff0420) = (V); mb(); } while(0)

#define __set_PDCR(V)	do { __reg(0xfeff0428) = (V); mb(); } while(0)

#define __get_RSTR()	({ __reg(0xfeff0500); })
#define __set_RSTR(V)	do { __reg(0xfeff0500) = (V); mb(); } while(0)



/* PDR definitions */
#define PDR_GPIO_DATA(X)	(1 << (X))

/* GPDR definitions */
#define GPDR_INPUT		0
#define GPDR_OUTPUT		1
#define GPDR_DREQ0_BIT		0x00001000
#define GPDR_DREQ1_BIT		0x00008000
#define GPDR_DREQ2_BIT		0x00040000
#define GPDR_DREQ3_BIT		0x00080000
#define GPDR_DREQ4_BIT		0x00004000
#define GPDR_DREQ5_BIT		0x00020000
#define GPDR_DREQ6_BIT		0x00100000
#define GPDR_DREQ7_BIT		0x00200000
#define GPDR_DACK0_BIT		0x00002000
#define GPDR_DACK1_BIT		0x00010000
#define GPDR_DACK2_BIT		0x00100000
#define GPDR_DACK3_BIT		0x00200000
#define GPDR_DONE0_BIT		0x00004000
#define GPDR_DONE1_BIT		0x00020000
#define GPDR_GPIO_DIR(X,D)	((D) << (X))

/* SIR definitions */
#define SIR_GPIO_INPUT		0
#define SIR_DREQ7_INPUT		0x00200000
#define SIR_DREQ6_INPUT		0x00100000
#define SIR_DREQ3_INPUT		0x00080000
#define SIR_DREQ2_INPUT		0x00040000
#define SIR_DREQ5_INPUT		0x00020000
#define SIR_DREQ1_INPUT		0x00008000
#define SIR_DREQ4_INPUT		0x00004000
#define SIR_DREQ0_INPUT		0x00001000
#define SIR_RXD1_INPUT		0x00000400
#define SIR_CTS0_INPUT		0x00000100
#define SIR_RXD0_INPUT		0x00000040
#define SIR_GATE1_INPUT		0x00000020
#define SIR_GATE0_INPUT		0x00000010
#define SIR_IRQ3_INPUT		0x00000008
#define SIR_IRQ2_INPUT		0x00000004
#define SIR_IRQ1_INPUT		0x00000002
#define SIR_IRQ0_INPUT		0x00000001
#define SIR_DREQ_BITS		(SIR_DREQ0_INPUT | SIR_DREQ1_INPUT | \
				 SIR_DREQ2_INPUT | SIR_DREQ3_INPUT | \
				 SIR_DREQ4_INPUT | SIR_DREQ5_INPUT | \
				 SIR_DREQ6_INPUT | SIR_DREQ7_INPUT)

/* SOR definitions */
#define SOR_GPIO_OUTPUT		0
#define SOR_DACK3_OUTPUT	0x00200000
#define SOR_DACK2_OUTPUT	0x00100000
#define SOR_DONE1_OUTPUT	0x00020000
#define SOR_DACK1_OUTPUT	0x00010000
#define SOR_DONE0_OUTPUT	0x00004000
#define SOR_DACK0_OUTPUT	0x00002000
#define SOR_TXD1_OUTPUT		0x00000800
#define SOR_RTS0_OUTPUT		0x00000200
#define SOR_TXD0_OUTPUT		0x00000080
#define SOR_TOUT1_OUTPUT	0x00000020
#define SOR_TOUT0_OUTPUT	0x00000010
#define SOR_DONE_BITS		(SOR_DONE0_OUTPUT | SOR_DONE1_OUTPUT)
#define SOR_DACK_BITS		(SOR_DACK0_OUTPUT | SOR_DACK1_OUTPUT | \
				 SOR_DACK2_OUTPUT | SOR_DACK3_OUTPUT)

/* PDSR definitions */
#define PDSR_UNCHANGED		0
#define PDSR_SET_BIT(X)		(1 << (X))

/* PDCR definitions */
#define PDCR_UNCHANGED		0
#define PDCR_CLEAR_BIT(X)	(1 << (X))

/* RSTR definitions */
/* Read Only */
#define RSTR_POWERON		0x00000400
#define RSTR_SOFTRESET_STATUS	0x00000100
/* Write Only */
#define RSTR_SOFTRESET		0x00000001

#endif /* _ASM_GPIO_REGS */
