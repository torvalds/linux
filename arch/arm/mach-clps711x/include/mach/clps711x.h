/*
 *  This file contains the hardware definitions of the Cirrus Logic
 *  ARM7 CLPS711X internal registers.
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
#ifndef __MACH_CLPS711X_H
#define __MACH_CLPS711X_H

#define CLPS711X_PHYS_BASE	(0x80000000)

#define PADR		(0x0000)
#define PBDR		(0x0001)
#define PCDR		(0x0002)
#define PDDR		(0x0003)
#define PADDR		(0x0040)
#define PBDDR		(0x0041)
#define PCDDR		(0x0042)
#define PDDDR		(0x0043)
#define PEDR		(0x0083)
#define PEDDR		(0x00c3)
#define SYSCON1		(0x0100)
#define SYSFLG1		(0x0140)
#define MEMCFG1		(0x0180)
#define MEMCFG2		(0x01c0)
#define DRFPR		(0x0200)
#define INTSR1		(0x0240)
#define INTMR1		(0x0280)
#define LCDCON		(0x02c0)
#define TC1D		(0x0300)
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

#define DAIR		(0x2000)
#define DAIDR0		(0x2040)
#define DAIDR1		(0x2080)
#define DAIDR2		(0x20c0)
#define DAISR		(0x2100)
#define SYSCON3		(0x2200)
#define INTSR3		(0x2240)
#define INTMR3		(0x2280)
#define LEDFLSH		(0x22c0)
#define SDCONF		(0x2300)
#define SDRFPR		(0x2340)
#define UNIQID		(0x2440)
#define DAI64FS		(0x2600)
#define PLLW		(0x2610)
#define PLLR		(0xa5a8)
#define RANDID0		(0x2700)
#define RANDID1		(0x2704)
#define RANDID2		(0x2708)
#define RANDID3		(0x270c)

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
#define SYSFLG1_VERID(x)	(((x) >> 30) & 3)
#define SYSFLG1_VERID_MASK	(3 << 30)

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

#define SYNCIO_FRMLEN(x)	(((x) & 0x1f) << 8)
#define SYNCIO_SMCKEN		(1 << 13)
#define SYNCIO_TXFRMEN		(1 << 14)

#define DAIR_RESERVED		(0x0404)
#define DAIR_DAIEN		(1 << 16)
#define DAIR_ECS		(1 << 17)
#define DAIR_LCTM		(1 << 19)
#define DAIR_LCRM		(1 << 20)
#define DAIR_RCTM		(1 << 21)
#define DAIR_RCRM		(1 << 22)
#define DAIR_LBM		(1 << 23)

#define DAIDR2_FIFOEN		(1 << 15)
#define DAIDR2_FIFOLEFT		(0x0d << 16)
#define DAIDR2_FIFORIGHT	(0x11 << 16)

#define DAISR_RCTS		(1 << 0)
#define DAISR_RCRS		(1 << 1)
#define DAISR_LCTS		(1 << 2)
#define DAISR_LCRS		(1 << 3)
#define DAISR_RCTU		(1 << 4)
#define DAISR_RCRO		(1 << 5)
#define DAISR_LCTU		(1 << 6)
#define DAISR_LCRO		(1 << 7)
#define DAISR_RCNF		(1 << 8)
#define DAISR_RCNE		(1 << 9)
#define DAISR_LCNF		(1 << 10)
#define DAISR_LCNE		(1 << 11)
#define DAISR_FIFO		(1 << 12)

#define DAI64FS_I2SF64		(1 << 0)
#define DAI64FS_AUDIOCLKEN	(1 << 1)
#define DAI64FS_AUDIOCLKSRC	(1 << 2)
#define DAI64FS_MCLK256EN	(1 << 3)
#define DAI64FS_LOOPBACK	(1 << 5)

#define SYSCON3_ADCCON		(1 << 0)
#define SYSCON3_CLKCTL0		(1 << 1)
#define SYSCON3_CLKCTL1		(1 << 2)
#define SYSCON3_DAISEL		(1 << 3)
#define SYSCON3_ADCCKNSEN	(1 << 4)
#define SYSCON3_VERSN(x)	(((x) >> 5) & 7)
#define SYSCON3_VERSN_MASK	(7 << 5)
#define SYSCON3_FASTWAKE	(1 << 8)
#define SYSCON3_DAIEN		(1 << 9)
#define SYSCON3_128FS		SYSCON3_DAIEN
#define SYSCON3_ENPD67		(1 << 10)

#define SDCONF_ACTIVE		(1 << 10)
#define SDCONF_CLKCTL		(1 << 9)
#define SDCONF_WIDTH_4		(0 << 7)
#define SDCONF_WIDTH_8		(1 << 7)
#define SDCONF_WIDTH_16		(2 << 7)
#define SDCONF_WIDTH_32		(3 << 7)
#define SDCONF_SIZE_16		(0 << 5)
#define SDCONF_SIZE_64		(1 << 5)
#define SDCONF_SIZE_128		(2 << 5)
#define SDCONF_SIZE_256		(3 << 5)
#define SDCONF_CASLAT_2		(2)
#define SDCONF_CASLAT_3		(3)

#define MEMCFG_BUS_WIDTH_32	(1)
#define MEMCFG_BUS_WIDTH_16	(0)
#define MEMCFG_BUS_WIDTH_8	(3)

#define MEMCFG_WAITSTATE_8_3	(0 << 2)
#define MEMCFG_WAITSTATE_7_3	(1 << 2)
#define MEMCFG_WAITSTATE_6_3	(2 << 2)
#define MEMCFG_WAITSTATE_5_3	(3 << 2)
#define MEMCFG_WAITSTATE_4_2	(4 << 2)
#define MEMCFG_WAITSTATE_3_2	(5 << 2)
#define MEMCFG_WAITSTATE_2_2	(6 << 2)
#define MEMCFG_WAITSTATE_1_2	(7 << 2)
#define MEMCFG_WAITSTATE_8_1	(8 << 2)
#define MEMCFG_WAITSTATE_7_1	(9 << 2)
#define MEMCFG_WAITSTATE_6_1	(10 << 2)
#define MEMCFG_WAITSTATE_5_1	(11 << 2)
#define MEMCFG_WAITSTATE_4_0	(12 << 2)
#define MEMCFG_WAITSTATE_3_0	(13 << 2)
#define MEMCFG_WAITSTATE_2_0	(14 << 2)
#define MEMCFG_WAITSTATE_1_0	(15 << 2)

#endif /* __MACH_CLPS711X_H */
