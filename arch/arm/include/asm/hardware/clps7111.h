/*
 *  arch/arm/include/asm/hardware/clps7111.h
 *
 *  This file contains the hardware definitions of the CLPS7111 internal
 *  registers.
 *
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef __ASM_HARDWARE_CLPS7111_H
#define __ASM_HARDWARE_CLPS7111_H

#define CLPS711X_PHYS_BASE	(0x80000000)

#define PADR		(0x0000)
#define PBDR		(0x0001)
#define PDDR		(0x0003)
#define PADDR		(0x0040)
#define PBDDR		(0x0041)
#define PDDDR		(0x0043)
#define PEDR		(0x0080)
#define PEDDR		(0x00c0)
#define SYSCON1		(0x0100)
#define SYSFLG1		(0x0140)
#define MEMCFG1		(0x0180)
#define MEMCFG2		(0x01c0)
#define DRFPR		(0x0200)
#define INTSR1		(0x0240)
#define INTMR1		(0x0280)
#define LCDCON		(0x02c0)
#define TC1D            (0x0300)
#define TC2D		(0x0340)
#define RTCDR		(0x0380)
#define RTCMR		(0x03c0)
#define PMPCON		(0x0400)
#define CODR		(0x0440)
#define UARTDR1		(0x0480)
#define UBRLCR1		(0x04c0)
#define SYNCIO		(0x0500)
#define PALLSW		(0x0540)
#define PALMSW		(0x0580)
#define STFCLR		(0x05c0)
#define BLEOI		(0x0600)
#define MCEOI		(0x0640)
#define TEOI		(0x0680)
#define TC1EOI		(0x06c0)
#define TC2EOI		(0x0700)
#define RTCEOI		(0x0740)
#define UMSEOI		(0x0780)
#define COEOI		(0x07c0)
#define HALT		(0x0800)
#define STDBY		(0x0840)

#define FBADDR		(0x1000)
#define SYSCON2		(0x1100)
#define SYSFLG2		(0x1140)
#define INTSR2		(0x1240)
#define INTMR2		(0x1280)
#define UARTDR2		(0x1480)
#define UBRLCR2		(0x14c0)
#define SS2DR		(0x1500)
#define SRXEOF		(0x1600)
#define SS2POP		(0x16c0)
#define KBDEOI		(0x1700)

/* common bits: SYSCON1 / SYSCON2 */
#define SYSCON_UARTEN		(1 << 8)

#define SYSCON1_KBDSCAN(x)	((x) & 15)
#define SYSCON1_KBDSCANMASK	(15)
#define SYSCON1_TC1M		(1 << 4)
#define SYSCON1_TC1S		(1 << 5)
#define SYSCON1_TC2M		(1 << 6)
#define SYSCON1_TC2S		(1 << 7)
#define SYSCON1_UART1EN		SYSCON_UARTEN
#define SYSCON1_BZTOG		(1 << 9)
#define SYSCON1_BZMOD		(1 << 10)
#define SYSCON1_DBGEN		(1 << 11)
#define SYSCON1_LCDEN		(1 << 12)
#define SYSCON1_CDENTX		(1 << 13)
#define SYSCON1_CDENRX		(1 << 14)
#define SYSCON1_SIREN		(1 << 15)
#define SYSCON1_ADCKSEL(x)	(((x) & 3) << 16)
#define SYSCON1_ADCKSEL_MASK	(3 << 16)
#define SYSCON1_EXCKEN		(1 << 18)
#define SYSCON1_WAKEDIS		(1 << 19)
#define SYSCON1_IRTXM		(1 << 20)

/* common bits: SYSFLG1 / SYSFLG2 */
#define SYSFLG_UBUSY		(1 << 11)
#define SYSFLG_URXFE		(1 << 22)
#define SYSFLG_UTXFF		(1 << 23)

#define SYSFLG1_MCDR		(1 << 0)
#define SYSFLG1_DCDET		(1 << 1)
#define SYSFLG1_WUDR		(1 << 2)
#define SYSFLG1_WUON		(1 << 3)
#define SYSFLG1_CTS		(1 << 8)
#define SYSFLG1_DSR		(1 << 9)
#define SYSFLG1_DCD		(1 << 10)
#define SYSFLG1_UBUSY		SYSFLG_UBUSY
#define SYSFLG1_NBFLG		(1 << 12)
#define SYSFLG1_RSTFLG		(1 << 13)
#define SYSFLG1_PFFLG		(1 << 14)
#define SYSFLG1_CLDFLG		(1 << 15)
#define SYSFLG1_URXFE		SYSFLG_URXFE
#define SYSFLG1_UTXFF		SYSFLG_UTXFF
#define SYSFLG1_CRXFE		(1 << 24)
#define SYSFLG1_CTXFF		(1 << 25)
#define SYSFLG1_SSIBUSY		(1 << 26)
#define SYSFLG1_ID		(1 << 29)

#define SYSFLG2_SSRXOF		(1 << 0)
#define SYSFLG2_RESVAL		(1 << 1)
#define SYSFLG2_RESFRM		(1 << 2)
#define SYSFLG2_SS2RXFE		(1 << 3)
#define SYSFLG2_SS2TXFF		(1 << 4)
#define SYSFLG2_SS2TXUF		(1 << 5)
#define SYSFLG2_CKMODE		(1 << 6)
#define SYSFLG2_UBUSY		SYSFLG_UBUSY
#define SYSFLG2_URXFE		SYSFLG_URXFE
#define SYSFLG2_UTXFF		SYSFLG_UTXFF

#define LCDCON_GSEN		(1 << 30)
#define LCDCON_GSMD		(1 << 31)

#define SYSCON2_SERSEL		(1 << 0)
#define SYSCON2_KBD6		(1 << 1)
#define SYSCON2_DRAMZ		(1 << 2)
#define SYSCON2_KBWEN		(1 << 3)
#define SYSCON2_SS2TXEN		(1 << 4)
#define SYSCON2_PCCARD1		(1 << 5)
#define SYSCON2_PCCARD2		(1 << 6)
#define SYSCON2_SS2RXEN		(1 << 7)
#define SYSCON2_UART2EN		SYSCON_UARTEN
#define SYSCON2_SS2MAEN		(1 << 9)
#define SYSCON2_OSTB		(1 << 12)
#define SYSCON2_CLKENSL		(1 << 13)
#define SYSCON2_BUZFREQ		(1 << 14)

/* common bits: UARTDR1 / UARTDR2 */
#define UARTDR_FRMERR		(1 << 8)
#define UARTDR_PARERR		(1 << 9)
#define UARTDR_OVERR		(1 << 10)

/* common bits: UBRLCR1 / UBRLCR2 */
#define UBRLCR_BAUD_MASK	((1 << 12) - 1)
#define UBRLCR_BREAK		(1 << 12)
#define UBRLCR_PRTEN		(1 << 13)
#define UBRLCR_EVENPRT		(1 << 14)
#define UBRLCR_XSTOP		(1 << 15)
#define UBRLCR_FIFOEN		(1 << 16)
#define UBRLCR_WRDLEN5		(0 << 17)
#define UBRLCR_WRDLEN6		(1 << 17)
#define UBRLCR_WRDLEN7		(2 << 17)
#define UBRLCR_WRDLEN8		(3 << 17)
#define UBRLCR_WRDLEN_MASK	(3 << 17)

#define SYNCIO_SMCKEN		(1 << 13)
#define SYNCIO_TXFRMEN		(1 << 14)

#endif /* __ASM_HARDWARE_CLPS7111_H */
