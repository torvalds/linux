/*
 *  Amiga Linux/m68k Ariadne Ethernet Driver
 *
 *  © Copyright 1995 by Geert Uytterhoeven (geert@linux-m68k.org)
 *			Peter De Schrijver
 *		       (Peter.DeSchrijver@linux.cc.kuleuven.ac.be)
 *
 *  ----------------------------------------------------------------------------------
 *
 *  This program is based on
 *
 *	lance.c:	An AMD LANCE ethernet driver for linux.
 *			Written 1993-94 by Donald Becker.
 *
 *	Am79C960:	PCnet(tm)-ISA Single-Chip Ethernet Controller
 *			Advanced Micro Devices
 *			Publication #16907, Rev. B, Amendment/0, May 1994
 *
 *	MC68230:	Parallel Interface/Timer (PI/T)
 *			Motorola Semiconductors, December, 1983
 *
 *  ----------------------------------------------------------------------------------
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of the Linux
 *  distribution for more details.
 *
 *  ----------------------------------------------------------------------------------
 *
 *  The Ariadne is a Zorro-II board made by Village Tronic. It contains:
 *
 *	- an Am79C960 PCnet-ISA Single-Chip Ethernet Controller with both
 *	  10BASE-2 (thin coax) and 10BASE-T (UTP) connectors
 *
 *	- an MC68230 Parallel Interface/Timer configured as 2 parallel ports
 */


    /*
     *	Am79C960 PCnet-ISA
     */

struct Am79C960 {
    volatile u_short AddressPROM[8];
				/* IEEE Address PROM (Unused in the Ariadne) */
    volatile u_short RDP;	/* Register Data Port */
    volatile u_short RAP;	/* Register Address Port */
    volatile u_short Reset;	/* Reset Chip on Read Access */
    volatile u_short IDP;	/* ISACSR Data Port */
};


    /*
     *	Am79C960 Control and Status Registers
     *
     *	These values are already swap()ed!!
     *
     *	Only registers marked with a `-' are intended for network software
     *	access
     */

#define CSR0		0x0000	/* - PCnet-ISA Controller Status */
#define CSR1		0x0100	/* - IADR[15:0] */
#define CSR2		0x0200	/* - IADR[23:16] */
#define CSR3		0x0300	/* - Interrupt Masks and Deferral Control */
#define CSR4		0x0400	/* - Test and Features Control */
#define CSR6		0x0600	/*   RCV/XMT Descriptor Table Length */
#define CSR8		0x0800	/* - Logical Address Filter, LADRF[15:0] */
#define CSR9		0x0900	/* - Logical Address Filter, LADRF[31:16] */
#define CSR10		0x0a00	/* - Logical Address Filter, LADRF[47:32] */
#define CSR11		0x0b00	/* - Logical Address Filter, LADRF[63:48] */
#define CSR12		0x0c00	/* - Physical Address Register, PADR[15:0] */
#define CSR13		0x0d00	/* - Physical Address Register, PADR[31:16] */
#define CSR14		0x0e00	/* - Physical Address Register, PADR[47:32] */
#define CSR15		0x0f00	/* - Mode Register */
#define CSR16		0x1000	/*   Initialization Block Address Lower */
#define CSR17		0x1100	/*   Initialization Block Address Upper */
#define CSR18		0x1200	/*   Current Receive Buffer Address */
#define CSR19		0x1300	/*   Current Receive Buffer Address */
#define CSR20		0x1400	/*   Current Transmit Buffer Address */
#define CSR21		0x1500	/*   Current Transmit Buffer Address */
#define CSR22		0x1600	/*   Next Receive Buffer Address */
#define CSR23		0x1700	/*   Next Receive Buffer Address */
#define CSR24		0x1800	/* - Base Address of Receive Ring */
#define CSR25		0x1900	/* - Base Address of Receive Ring */
#define CSR26		0x1a00	/*   Next Receive Descriptor Address */
#define CSR27		0x1b00	/*   Next Receive Descriptor Address */
#define CSR28		0x1c00	/*   Current Receive Descriptor Address */
#define CSR29		0x1d00	/*   Current Receive Descriptor Address */
#define CSR30		0x1e00	/* - Base Address of Transmit Ring */
#define CSR31		0x1f00	/* - Base Address of transmit Ring */
#define CSR32		0x2000	/*   Next Transmit Descriptor Address */
#define CSR33		0x2100	/*   Next Transmit Descriptor Address */
#define CSR34		0x2200	/*   Current Transmit Descriptor Address */
#define CSR35		0x2300	/*   Current Transmit Descriptor Address */
#define CSR36		0x2400	/*   Next Next Receive Descriptor Address */
#define CSR37		0x2500	/*   Next Next Receive Descriptor Address */
#define CSR38		0x2600	/*   Next Next Transmit Descriptor Address */
#define CSR39		0x2700	/*   Next Next Transmit Descriptor Address */
#define CSR40		0x2800	/*   Current Receive Status and Byte Count */
#define CSR41		0x2900	/*   Current Receive Status and Byte Count */
#define CSR42		0x2a00	/*   Current Transmit Status and Byte Count */
#define CSR43		0x2b00	/*   Current Transmit Status and Byte Count */
#define CSR44		0x2c00	/*   Next Receive Status and Byte Count */
#define CSR45		0x2d00	/*   Next Receive Status and Byte Count */
#define CSR46		0x2e00	/*   Poll Time Counter */
#define CSR47		0x2f00	/*   Polling Interval */
#define CSR48		0x3000	/*   Temporary Storage */
#define CSR49		0x3100	/*   Temporary Storage */
#define CSR50		0x3200	/*   Temporary Storage */
#define CSR51		0x3300	/*   Temporary Storage */
#define CSR52		0x3400	/*   Temporary Storage */
#define CSR53		0x3500	/*   Temporary Storage */
#define CSR54		0x3600	/*   Temporary Storage */
#define CSR55		0x3700	/*   Temporary Storage */
#define CSR56		0x3800	/*   Temporary Storage */
#define CSR57		0x3900	/*   Temporary Storage */
#define CSR58		0x3a00	/*   Temporary Storage */
#define CSR59		0x3b00	/*   Temporary Storage */
#define CSR60		0x3c00	/*   Previous Transmit Descriptor Address */
#define CSR61		0x3d00	/*   Previous Transmit Descriptor Address */
#define CSR62		0x3e00	/*   Previous Transmit Status and Byte Count */
#define CSR63		0x3f00	/*   Previous Transmit Status and Byte Count */
#define CSR64		0x4000	/*   Next Transmit Buffer Address */
#define CSR65		0x4100	/*   Next Transmit Buffer Address */
#define CSR66		0x4200	/*   Next Transmit Status and Byte Count */
#define CSR67		0x4300	/*   Next Transmit Status and Byte Count */
#define CSR68		0x4400	/*   Transmit Status Temporary Storage */
#define CSR69		0x4500	/*   Transmit Status Temporary Storage */
#define CSR70		0x4600	/*   Temporary Storage */
#define CSR71		0x4700	/*   Temporary Storage */
#define CSR72		0x4800	/*   Receive Ring Counter */
#define CSR74		0x4a00	/*   Transmit Ring Counter */
#define CSR76		0x4c00	/* - Receive Ring Length */
#define CSR78		0x4e00	/* - Transmit Ring Length */
#define CSR80		0x5000	/* - Burst and FIFO Threshold Control */
#define CSR82		0x5200	/* - Bus Activity Timer */
#define CSR84		0x5400	/*   DMA Address */
#define CSR85		0x5500	/*   DMA Address */
#define CSR86		0x5600	/*   Buffer Byte Counter */
#define CSR88		0x5800	/* - Chip ID */
#define CSR89		0x5900	/* - Chip ID */
#define CSR92		0x5c00	/*   Ring Length Conversion */
#define CSR94		0x5e00	/*   Transmit Time Domain Reflectometry Count */
#define CSR96		0x6000	/*   Bus Interface Scratch Register 0 */
#define CSR97		0x6100	/*   Bus Interface Scratch Register 0 */
#define CSR98		0x6200	/*   Bus Interface Scratch Register 1 */
#define CSR99		0x6300	/*   Bus Interface Scratch Register 1 */
#define CSR104		0x6800	/*   SWAP */
#define CSR105		0x6900	/*   SWAP */
#define CSR108		0x6c00	/*   Buffer Management Scratch */
#define CSR109		0x6d00	/*   Buffer Management Scratch */
#define CSR112		0x7000	/* - Missed Frame Count */
#define CSR114		0x7200	/* - Receive Collision Count */
#define CSR124		0x7c00	/* - Buffer Management Unit Test */


    /*
     *	Am79C960 ISA Control and Status Registers
     *
     *	These values are already swap()ed!!
     */

#define ISACSR0		0x0000	/* Master Mode Read Active */
#define ISACSR1		0x0100	/* Master Mode Write Active */
#define ISACSR2		0x0200	/* Miscellaneous Configuration */
#define ISACSR4		0x0400	/* LED0 Status (Link Integrity) */
#define ISACSR5		0x0500	/* LED1 Status */
#define ISACSR6		0x0600	/* LED2 Status */
#define ISACSR7		0x0700	/* LED3 Status */


    /*
     *	Bit definitions for CSR0 (PCnet-ISA Controller Status)
     *
     *	These values are already swap()ed!!
     */

#define ERR		0x0080	/* Error */
#define BABL		0x0040	/* Babble: Transmitted too many bits */
#define CERR		0x0020	/* No Heartbeat (10BASE-T) */
#define MISS		0x0010	/* Missed Frame */
#define MERR		0x0008	/* Memory Error */
#define RINT		0x0004	/* Receive Interrupt */
#define TINT		0x0002	/* Transmit Interrupt */
#define IDON		0x0001	/* Initialization Done */
#define INTR		0x8000	/* Interrupt Flag */
#define INEA		0x4000	/* Interrupt Enable */
#define RXON		0x2000	/* Receive On */
#define TXON		0x1000	/* Transmit On */
#define TDMD		0x0800	/* Transmit Demand */
#define STOP		0x0400	/* Stop */
#define STRT		0x0200	/* Start */
#define INIT		0x0100	/* Initialize */


    /*
     *	Bit definitions for CSR3 (Interrupt Masks and Deferral Control)
     *
     *	These values are already swap()ed!!
     */

#define BABLM		0x0040	/* Babble Mask */
#define MISSM		0x0010	/* Missed Frame Mask */
#define MERRM		0x0008	/* Memory Error Mask */
#define RINTM		0x0004	/* Receive Interrupt Mask */
#define TINTM		0x0002	/* Transmit Interrupt Mask */
#define IDONM		0x0001	/* Initialization Done Mask */
#define DXMT2PD		0x1000	/* Disable Transmit Two Part Deferral */
#define EMBA		0x0800	/* Enable Modified Back-off Algorithm */


    /*
     *	Bit definitions for CSR4 (Test and Features Control)
     *
     *	These values are already swap()ed!!
     */

#define ENTST		0x0080	/* Enable Test Mode */
#define DMAPLUS		0x0040	/* Disable Burst Transaction Counter */
#define TIMER		0x0020	/* Timer Enable Register */
#define DPOLL		0x0010	/* Disable Transmit Polling */
#define APAD_XMT	0x0008	/* Auto Pad Transmit */
#define ASTRP_RCV	0x0004	/* Auto Pad Stripping */
#define MFCO		0x0002	/* Missed Frame Counter Overflow Interrupt */
#define MFCOM		0x0001	/* Missed Frame Counter Overflow Mask */
#define RCVCCO		0x2000	/* Receive Collision Counter Overflow Interrupt */
#define RCVCCOM		0x1000	/* Receive Collision Counter Overflow Mask */
#define TXSTRT		0x0800	/* Transmit Start Status */
#define TXSTRTM		0x0400	/* Transmit Start Mask */
#define JAB		0x0200	/* Jabber Error */
#define JABM		0x0100	/* Jabber Error Mask */


    /*
     *	Bit definitions for CSR15 (Mode Register)
     *
     *	These values are already swap()ed!!
     */

#define PROM		0x0080	/* Promiscuous Mode */
#define DRCVBC		0x0040	/* Disable Receive Broadcast */
#define DRCVPA		0x0020	/* Disable Receive Physical Address */
#define DLNKTST		0x0010	/* Disable Link Status */
#define DAPC		0x0008	/* Disable Automatic Polarity Correction */
#define MENDECL		0x0004	/* MENDEC Loopback Mode */
#define LRTTSEL		0x0002	/* Low Receive Treshold/Transmit Mode Select */
#define PORTSEL1	0x0001	/* Port Select Bits */
#define PORTSEL2	0x8000	/* Port Select Bits */
#define INTL		0x4000	/* Internal Loopback */
#define DRTY		0x2000	/* Disable Retry */
#define FCOLL		0x1000	/* Force Collision */
#define DXMTFCS		0x0800	/* Disable Transmit CRC */
#define LOOP		0x0400	/* Loopback Enable */
#define DTX		0x0200	/* Disable Transmitter */
#define DRX		0x0100	/* Disable Receiver */


    /*
     *	Bit definitions for ISACSR2 (Miscellaneous Configuration)
     *
     *	These values are already swap()ed!!
     */

#define ASEL		0x0200	/* Media Interface Port Auto Select */


    /*
     *	Bit definitions for ISACSR5-7 (LED1-3 Status)
     *
     *	These values are already swap()ed!!
     */

#define LEDOUT		0x0080	/* Current LED Status */
#define PSE		0x8000	/* Pulse Stretcher Enable */
#define XMTE		0x1000	/* Enable Transmit Status Signal */
#define RVPOLE		0x0800	/* Enable Receive Polarity Signal */
#define RCVE		0x0400	/* Enable Receive Status Signal */
#define JABE		0x0200	/* Enable Jabber Signal */
#define COLE		0x0100	/* Enable Collision Signal */


    /*
     *	Receive Descriptor Ring Entry
     */

struct RDRE {
    volatile u_short RMD0;	/* LADR[15:0] */
    volatile u_short RMD1;	/* HADR[23:16] | Receive Flags */
    volatile u_short RMD2;	/* Buffer Byte Count (two's complement) */
    volatile u_short RMD3;	/* Message Byte Count */
};


    /*
     *	Transmit Descriptor Ring Entry
     */

struct TDRE {
    volatile u_short TMD0;	/* LADR[15:0] */
    volatile u_short TMD1;	/* HADR[23:16] | Transmit Flags */
    volatile u_short TMD2;	/* Buffer Byte Count (two's complement) */
    volatile u_short TMD3;	/* Error Flags */
};


    /*
     *	Receive Flags
     */

#define RF_OWN		0x0080	/* PCnet-ISA controller owns the descriptor */
#define RF_ERR		0x0040	/* Error */
#define RF_FRAM		0x0020	/* Framing Error */
#define RF_OFLO		0x0010	/* Overflow Error */
#define RF_CRC		0x0008	/* CRC Error */
#define RF_BUFF		0x0004	/* Buffer Error */
#define RF_STP		0x0002	/* Start of Packet */
#define RF_ENP		0x0001	/* End of Packet */


    /*
     *	Transmit Flags
     */

#define TF_OWN		0x0080	/* PCnet-ISA controller owns the descriptor */
#define TF_ERR		0x0040	/* Error */
#define TF_ADD_FCS	0x0020	/* Controls FCS Generation */
#define TF_MORE		0x0010	/* More than one retry needed */
#define TF_ONE		0x0008	/* One retry needed */
#define TF_DEF		0x0004	/* Deferred */
#define TF_STP		0x0002	/* Start of Packet */
#define TF_ENP		0x0001	/* End of Packet */


    /*
     *	Error Flags
     */

#define EF_BUFF		0x0080	/* Buffer Error */
#define EF_UFLO		0x0040	/* Underflow Error */
#define EF_LCOL		0x0010	/* Late Collision */
#define EF_LCAR		0x0008	/* Loss of Carrier */
#define EF_RTRY		0x0004	/* Retry Error */
#define EF_TDR		0xff03	/* Time Domain Reflectometry */



    /*
     *	MC68230 Parallel Interface/Timer
     */

struct MC68230 {
    volatile u_char PGCR;	/* Port General Control Register */
    u_char Pad1[1];
    volatile u_char PSRR;	/* Port Service Request Register */
    u_char Pad2[1];
    volatile u_char PADDR;	/* Port A Data Direction Register */
    u_char Pad3[1];
    volatile u_char PBDDR;	/* Port B Data Direction Register */
    u_char Pad4[1];
    volatile u_char PCDDR;	/* Port C Data Direction Register */
    u_char Pad5[1];
    volatile u_char PIVR;	/* Port Interrupt Vector Register */
    u_char Pad6[1];
    volatile u_char PACR;	/* Port A Control Register */
    u_char Pad7[1];
    volatile u_char PBCR;	/* Port B Control Register */
    u_char Pad8[1];
    volatile u_char PADR;	/* Port A Data Register */
    u_char Pad9[1];
    volatile u_char PBDR;	/* Port B Data Register */
    u_char Pad10[1];
    volatile u_char PAAR;	/* Port A Alternate Register */
    u_char Pad11[1];
    volatile u_char PBAR;	/* Port B Alternate Register */
    u_char Pad12[1];
    volatile u_char PCDR;	/* Port C Data Register */
    u_char Pad13[1];
    volatile u_char PSR;	/* Port Status Register */
    u_char Pad14[5];
    volatile u_char TCR;	/* Timer Control Register */
    u_char Pad15[1];
    volatile u_char TIVR;	/* Timer Interrupt Vector Register */
    u_char Pad16[3];
    volatile u_char CPRH;	/* Counter Preload Register (High) */
    u_char Pad17[1];
    volatile u_char CPRM;	/* Counter Preload Register (Mid) */
    u_char Pad18[1];
    volatile u_char CPRL;	/* Counter Preload Register (Low) */
    u_char Pad19[3];
    volatile u_char CNTRH;	/* Count Register (High) */
    u_char Pad20[1];
    volatile u_char CNTRM;	/* Count Register (Mid) */
    u_char Pad21[1];
    volatile u_char CNTRL;	/* Count Register (Low) */
    u_char Pad22[1];
    volatile u_char TSR;	/* Timer Status Register */
    u_char Pad23[11];
};


    /*
     *	Ariadne Expansion Board Structure
     */

#define ARIADNE_LANCE		0x360

#define ARIADNE_PIT		0x1000

#define ARIADNE_BOOTPROM	0x4000	/* I guess it's here :-) */
#define ARIADNE_BOOTPROM_SIZE	0x4000

#define ARIADNE_RAM		0x8000	/* Always access WORDs!! */
#define ARIADNE_RAM_SIZE	0x8000

