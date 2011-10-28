/*
 * bfin_can.h - interface to Blackfin CANs
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BFIN_CAN_H__
#define __ASM_BFIN_CAN_H__

/*
 * transmit and receive channels
 */
#define TRANSMIT_CHL            24
#define RECEIVE_STD_CHL         0
#define RECEIVE_EXT_CHL         4
#define RECEIVE_RTR_CHL         8
#define RECEIVE_EXT_RTR_CHL     12
#define MAX_CHL_NUMBER          32

/*
 * All Blackfin system MMRs are padded to 32bits even if the register
 * itself is only 16bits.  So use a helper macro to streamline this.
 */
#define __BFP(m) u16 m; u16 __pad_##m

/*
 * bfin can registers layout
 */
struct bfin_can_mask_regs {
	__BFP(aml);
	__BFP(amh);
};

struct bfin_can_channel_regs {
	/* data[0,2,4,6] -> data{0,1,2,3} while data[1,3,5,7] is padding */
	u16 data[8];
	__BFP(dlc);
	__BFP(tsv);
	__BFP(id0);
	__BFP(id1);
};

struct bfin_can_regs {
	/*
	 * global control and status registers
	 */
	__BFP(mc1);           /* offset 0x00 */
	__BFP(md1);           /* offset 0x04 */
	__BFP(trs1);          /* offset 0x08 */
	__BFP(trr1);          /* offset 0x0c */
	__BFP(ta1);           /* offset 0x10 */
	__BFP(aa1);           /* offset 0x14 */
	__BFP(rmp1);          /* offset 0x18 */
	__BFP(rml1);          /* offset 0x1c */
	__BFP(mbtif1);        /* offset 0x20 */
	__BFP(mbrif1);        /* offset 0x24 */
	__BFP(mbim1);         /* offset 0x28 */
	__BFP(rfh1);          /* offset 0x2c */
	__BFP(opss1);         /* offset 0x30 */
	u32 __pad1[3];
	__BFP(mc2);           /* offset 0x40 */
	__BFP(md2);           /* offset 0x44 */
	__BFP(trs2);          /* offset 0x48 */
	__BFP(trr2);          /* offset 0x4c */
	__BFP(ta2);           /* offset 0x50 */
	__BFP(aa2);           /* offset 0x54 */
	__BFP(rmp2);          /* offset 0x58 */
	__BFP(rml2);          /* offset 0x5c */
	__BFP(mbtif2);        /* offset 0x60 */
	__BFP(mbrif2);        /* offset 0x64 */
	__BFP(mbim2);         /* offset 0x68 */
	__BFP(rfh2);          /* offset 0x6c */
	__BFP(opss2);         /* offset 0x70 */
	u32 __pad2[3];
	__BFP(clock);         /* offset 0x80 */
	__BFP(timing);        /* offset 0x84 */
	__BFP(debug);         /* offset 0x88 */
	__BFP(status);        /* offset 0x8c */
	__BFP(cec);           /* offset 0x90 */
	__BFP(gis);           /* offset 0x94 */
	__BFP(gim);           /* offset 0x98 */
	__BFP(gif);           /* offset 0x9c */
	__BFP(control);       /* offset 0xa0 */
	__BFP(intr);          /* offset 0xa4 */
	__BFP(version);       /* offset 0xa8 */
	__BFP(mbtd);          /* offset 0xac */
	__BFP(ewr);           /* offset 0xb0 */
	__BFP(esr);           /* offset 0xb4 */
	u32 __pad3[2];
	__BFP(ucreg);         /* offset 0xc0 */
	__BFP(uccnt);         /* offset 0xc4 */
	__BFP(ucrc);          /* offset 0xc8 */
	__BFP(uccnf);         /* offset 0xcc */
	u32 __pad4[1];
	__BFP(version2);      /* offset 0xd4 */
	u32 __pad5[10];

	/*
	 * channel(mailbox) mask and message registers
	 */
	struct bfin_can_mask_regs msk[MAX_CHL_NUMBER];    /* offset 0x100 */
	struct bfin_can_channel_regs chl[MAX_CHL_NUMBER]; /* offset 0x200 */
};

#undef __BFP

/* CAN_CONTROL Masks */
#define SRS			0x0001	/* Software Reset */
#define DNM			0x0002	/* Device Net Mode */
#define ABO			0x0004	/* Auto-Bus On Enable */
#define TXPRIO		0x0008	/* TX Priority (Priority/Mailbox*) */
#define WBA			0x0010	/* Wake-Up On CAN Bus Activity Enable */
#define SMR			0x0020	/* Sleep Mode Request */
#define CSR			0x0040	/* CAN Suspend Mode Request */
#define CCR			0x0080	/* CAN Configuration Mode Request */

/* CAN_STATUS Masks */
#define WT			0x0001	/* TX Warning Flag */
#define WR			0x0002	/* RX Warning Flag */
#define EP			0x0004	/* Error Passive Mode */
#define EBO			0x0008	/* Error Bus Off Mode */
#define SMA			0x0020	/* Sleep Mode Acknowledge */
#define CSA			0x0040	/* Suspend Mode Acknowledge */
#define CCA			0x0080	/* Configuration Mode Acknowledge */
#define MBPTR		0x1F00	/* Mailbox Pointer */
#define TRM			0x4000	/* Transmit Mode */
#define REC			0x8000	/* Receive Mode */

/* CAN_CLOCK Masks */
#define BRP			0x03FF	/* Bit-Rate Pre-Scaler */

/* CAN_TIMING Masks */
#define TSEG1		0x000F	/* Time Segment 1 */
#define TSEG2		0x0070	/* Time Segment 2 */
#define SAM			0x0080	/* Sampling */
#define SJW			0x0300	/* Synchronization Jump Width */

/* CAN_DEBUG Masks */
#define DEC			0x0001	/* Disable CAN Error Counters */
#define DRI			0x0002	/* Disable CAN RX Input */
#define DTO			0x0004	/* Disable CAN TX Output */
#define DIL			0x0008	/* Disable CAN Internal Loop */
#define MAA			0x0010	/* Mode Auto-Acknowledge Enable */
#define MRB			0x0020	/* Mode Read Back Enable */
#define CDE			0x8000	/* CAN Debug Enable */

/* CAN_CEC Masks */
#define RXECNT		0x00FF	/* Receive Error Counter */
#define TXECNT		0xFF00	/* Transmit Error Counter */

/* CAN_INTR Masks */
#define MBRIRQ	0x0001	/* Mailbox Receive Interrupt */
#define MBTIRQ	0x0002	/* Mailbox Transmit Interrupt */
#define GIRQ		0x0004	/* Global Interrupt */
#define SMACK		0x0008	/* Sleep Mode Acknowledge */
#define CANTX		0x0040	/* CAN TX Bus Value */
#define CANRX		0x0080	/* CAN RX Bus Value */

/* CAN_MBxx_ID1 and CAN_MBxx_ID0 Masks */
#define DFC			0xFFFF	/* Data Filtering Code (If Enabled) (ID0) */
#define EXTID_LO	0xFFFF	/* Lower 16 Bits of Extended Identifier (ID0) */
#define EXTID_HI	0x0003	/* Upper 2 Bits of Extended Identifier (ID1) */
#define BASEID		0x1FFC	/* Base Identifier */
#define IDE			0x2000	/* Identifier Extension */
#define RTR			0x4000	/* Remote Frame Transmission Request */
#define AME			0x8000	/* Acceptance Mask Enable */

/* CAN_MBxx_TIMESTAMP Masks */
#define TSV			0xFFFF	/* Timestamp */

/* CAN_MBxx_LENGTH Masks */
#define DLC			0x000F	/* Data Length Code */

/* CAN_AMxxH and CAN_AMxxL Masks */
#define DFM			0xFFFF	/* Data Field Mask (If Enabled) (CAN_AMxxL) */
#define EXTID_LO	0xFFFF	/* Lower 16 Bits of Extended Identifier (CAN_AMxxL) */
#define EXTID_HI	0x0003	/* Upper 2 Bits of Extended Identifier (CAN_AMxxH) */
#define BASEID		0x1FFC	/* Base Identifier */
#define AMIDE		0x2000	/* Acceptance Mask ID Extension Enable */
#define FMD			0x4000	/* Full Mask Data Field Enable */
#define FDF			0x8000	/* Filter On Data Field Enable */

/* CAN_MC1 Masks */
#define MC0			0x0001	/* Enable Mailbox 0 */
#define MC1			0x0002	/* Enable Mailbox 1 */
#define MC2			0x0004	/* Enable Mailbox 2 */
#define MC3			0x0008	/* Enable Mailbox 3 */
#define MC4			0x0010	/* Enable Mailbox 4 */
#define MC5			0x0020	/* Enable Mailbox 5 */
#define MC6			0x0040	/* Enable Mailbox 6 */
#define MC7			0x0080	/* Enable Mailbox 7 */
#define MC8			0x0100	/* Enable Mailbox 8 */
#define MC9			0x0200	/* Enable Mailbox 9 */
#define MC10		0x0400	/* Enable Mailbox 10 */
#define MC11		0x0800	/* Enable Mailbox 11 */
#define MC12		0x1000	/* Enable Mailbox 12 */
#define MC13		0x2000	/* Enable Mailbox 13 */
#define MC14		0x4000	/* Enable Mailbox 14 */
#define MC15		0x8000	/* Enable Mailbox 15 */

/* CAN_MC2 Masks */
#define MC16		0x0001	/* Enable Mailbox 16 */
#define MC17		0x0002	/* Enable Mailbox 17 */
#define MC18		0x0004	/* Enable Mailbox 18 */
#define MC19		0x0008	/* Enable Mailbox 19 */
#define MC20		0x0010	/* Enable Mailbox 20 */
#define MC21		0x0020	/* Enable Mailbox 21 */
#define MC22		0x0040	/* Enable Mailbox 22 */
#define MC23		0x0080	/* Enable Mailbox 23 */
#define MC24		0x0100	/* Enable Mailbox 24 */
#define MC25		0x0200	/* Enable Mailbox 25 */
#define MC26		0x0400	/* Enable Mailbox 26 */
#define MC27		0x0800	/* Enable Mailbox 27 */
#define MC28		0x1000	/* Enable Mailbox 28 */
#define MC29		0x2000	/* Enable Mailbox 29 */
#define MC30		0x4000	/* Enable Mailbox 30 */
#define MC31		0x8000	/* Enable Mailbox 31 */

/* CAN_MD1 Masks */
#define MD0			0x0001	/* Enable Mailbox 0 For Receive */
#define MD1			0x0002	/* Enable Mailbox 1 For Receive */
#define MD2			0x0004	/* Enable Mailbox 2 For Receive */
#define MD3			0x0008	/* Enable Mailbox 3 For Receive */
#define MD4			0x0010	/* Enable Mailbox 4 For Receive */
#define MD5			0x0020	/* Enable Mailbox 5 For Receive */
#define MD6			0x0040	/* Enable Mailbox 6 For Receive */
#define MD7			0x0080	/* Enable Mailbox 7 For Receive */
#define MD8			0x0100	/* Enable Mailbox 8 For Receive */
#define MD9			0x0200	/* Enable Mailbox 9 For Receive */
#define MD10		0x0400	/* Enable Mailbox 10 For Receive */
#define MD11		0x0800	/* Enable Mailbox 11 For Receive */
#define MD12		0x1000	/* Enable Mailbox 12 For Receive */
#define MD13		0x2000	/* Enable Mailbox 13 For Receive */
#define MD14		0x4000	/* Enable Mailbox 14 For Receive */
#define MD15		0x8000	/* Enable Mailbox 15 For Receive */

/* CAN_MD2 Masks */
#define MD16		0x0001	/* Enable Mailbox 16 For Receive */
#define MD17		0x0002	/* Enable Mailbox 17 For Receive */
#define MD18		0x0004	/* Enable Mailbox 18 For Receive */
#define MD19		0x0008	/* Enable Mailbox 19 For Receive */
#define MD20		0x0010	/* Enable Mailbox 20 For Receive */
#define MD21		0x0020	/* Enable Mailbox 21 For Receive */
#define MD22		0x0040	/* Enable Mailbox 22 For Receive */
#define MD23		0x0080	/* Enable Mailbox 23 For Receive */
#define MD24		0x0100	/* Enable Mailbox 24 For Receive */
#define MD25		0x0200	/* Enable Mailbox 25 For Receive */
#define MD26		0x0400	/* Enable Mailbox 26 For Receive */
#define MD27		0x0800	/* Enable Mailbox 27 For Receive */
#define MD28		0x1000	/* Enable Mailbox 28 For Receive */
#define MD29		0x2000	/* Enable Mailbox 29 For Receive */
#define MD30		0x4000	/* Enable Mailbox 30 For Receive */
#define MD31		0x8000	/* Enable Mailbox 31 For Receive */

/* CAN_RMP1 Masks */
#define RMP0		0x0001	/* RX Message Pending In Mailbox 0 */
#define RMP1		0x0002	/* RX Message Pending In Mailbox 1 */
#define RMP2		0x0004	/* RX Message Pending In Mailbox 2 */
#define RMP3		0x0008	/* RX Message Pending In Mailbox 3 */
#define RMP4		0x0010	/* RX Message Pending In Mailbox 4 */
#define RMP5		0x0020	/* RX Message Pending In Mailbox 5 */
#define RMP6		0x0040	/* RX Message Pending In Mailbox 6 */
#define RMP7		0x0080	/* RX Message Pending In Mailbox 7 */
#define RMP8		0x0100	/* RX Message Pending In Mailbox 8 */
#define RMP9		0x0200	/* RX Message Pending In Mailbox 9 */
#define RMP10		0x0400	/* RX Message Pending In Mailbox 10 */
#define RMP11		0x0800	/* RX Message Pending In Mailbox 11 */
#define RMP12		0x1000	/* RX Message Pending In Mailbox 12 */
#define RMP13		0x2000	/* RX Message Pending In Mailbox 13 */
#define RMP14		0x4000	/* RX Message Pending In Mailbox 14 */
#define RMP15		0x8000	/* RX Message Pending In Mailbox 15 */

/* CAN_RMP2 Masks */
#define RMP16		0x0001	/* RX Message Pending In Mailbox 16 */
#define RMP17		0x0002	/* RX Message Pending In Mailbox 17 */
#define RMP18		0x0004	/* RX Message Pending In Mailbox 18 */
#define RMP19		0x0008	/* RX Message Pending In Mailbox 19 */
#define RMP20		0x0010	/* RX Message Pending In Mailbox 20 */
#define RMP21		0x0020	/* RX Message Pending In Mailbox 21 */
#define RMP22		0x0040	/* RX Message Pending In Mailbox 22 */
#define RMP23		0x0080	/* RX Message Pending In Mailbox 23 */
#define RMP24		0x0100	/* RX Message Pending In Mailbox 24 */
#define RMP25		0x0200	/* RX Message Pending In Mailbox 25 */
#define RMP26		0x0400	/* RX Message Pending In Mailbox 26 */
#define RMP27		0x0800	/* RX Message Pending In Mailbox 27 */
#define RMP28		0x1000	/* RX Message Pending In Mailbox 28 */
#define RMP29		0x2000	/* RX Message Pending In Mailbox 29 */
#define RMP30		0x4000	/* RX Message Pending In Mailbox 30 */
#define RMP31		0x8000	/* RX Message Pending In Mailbox 31 */

/* CAN_RML1 Masks */
#define RML0		0x0001	/* RX Message Lost In Mailbox 0 */
#define RML1		0x0002	/* RX Message Lost In Mailbox 1 */
#define RML2		0x0004	/* RX Message Lost In Mailbox 2 */
#define RML3		0x0008	/* RX Message Lost In Mailbox 3 */
#define RML4		0x0010	/* RX Message Lost In Mailbox 4 */
#define RML5		0x0020	/* RX Message Lost In Mailbox 5 */
#define RML6		0x0040	/* RX Message Lost In Mailbox 6 */
#define RML7		0x0080	/* RX Message Lost In Mailbox 7 */
#define RML8		0x0100	/* RX Message Lost In Mailbox 8 */
#define RML9		0x0200	/* RX Message Lost In Mailbox 9 */
#define RML10		0x0400	/* RX Message Lost In Mailbox 10 */
#define RML11		0x0800	/* RX Message Lost In Mailbox 11 */
#define RML12		0x1000	/* RX Message Lost In Mailbox 12 */
#define RML13		0x2000	/* RX Message Lost In Mailbox 13 */
#define RML14		0x4000	/* RX Message Lost In Mailbox 14 */
#define RML15		0x8000	/* RX Message Lost In Mailbox 15 */

/* CAN_RML2 Masks */
#define RML16		0x0001	/* RX Message Lost In Mailbox 16 */
#define RML17		0x0002	/* RX Message Lost In Mailbox 17 */
#define RML18		0x0004	/* RX Message Lost In Mailbox 18 */
#define RML19		0x0008	/* RX Message Lost In Mailbox 19 */
#define RML20		0x0010	/* RX Message Lost In Mailbox 20 */
#define RML21		0x0020	/* RX Message Lost In Mailbox 21 */
#define RML22		0x0040	/* RX Message Lost In Mailbox 22 */
#define RML23		0x0080	/* RX Message Lost In Mailbox 23 */
#define RML24		0x0100	/* RX Message Lost In Mailbox 24 */
#define RML25		0x0200	/* RX Message Lost In Mailbox 25 */
#define RML26		0x0400	/* RX Message Lost In Mailbox 26 */
#define RML27		0x0800	/* RX Message Lost In Mailbox 27 */
#define RML28		0x1000	/* RX Message Lost In Mailbox 28 */
#define RML29		0x2000	/* RX Message Lost In Mailbox 29 */
#define RML30		0x4000	/* RX Message Lost In Mailbox 30 */
#define RML31		0x8000	/* RX Message Lost In Mailbox 31 */

/* CAN_OPSS1 Masks */
#define OPSS0		0x0001	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 0 */
#define OPSS1		0x0002	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 1 */
#define OPSS2		0x0004	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 2 */
#define OPSS3		0x0008	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 3 */
#define OPSS4		0x0010	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 4 */
#define OPSS5		0x0020	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 5 */
#define OPSS6		0x0040	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 6 */
#define OPSS7		0x0080	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 7 */
#define OPSS8		0x0100	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 8 */
#define OPSS9		0x0200	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 9 */
#define OPSS10		0x0400	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 10 */
#define OPSS11		0x0800	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 11 */
#define OPSS12		0x1000	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 12 */
#define OPSS13		0x2000	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 13 */
#define OPSS14		0x4000	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 14 */
#define OPSS15		0x8000	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 15 */

/* CAN_OPSS2 Masks */
#define OPSS16		0x0001	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 16 */
#define OPSS17		0x0002	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 17 */
#define OPSS18		0x0004	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 18 */
#define OPSS19		0x0008	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 19 */
#define OPSS20		0x0010	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 20 */
#define OPSS21		0x0020	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 21 */
#define OPSS22		0x0040	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 22 */
#define OPSS23		0x0080	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 23 */
#define OPSS24		0x0100	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 24 */
#define OPSS25		0x0200	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 25 */
#define OPSS26		0x0400	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 26 */
#define OPSS27		0x0800	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 27 */
#define OPSS28		0x1000	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 28 */
#define OPSS29		0x2000	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 29 */
#define OPSS30		0x4000	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 30 */
#define OPSS31		0x8000	/* Enable RX Overwrite Protection or TX Single-Shot For Mailbox 31 */

/* CAN_TRR1 Masks */
#define TRR0		0x0001	/* Deny But Don't Lock Access To Mailbox 0 */
#define TRR1		0x0002	/* Deny But Don't Lock Access To Mailbox 1 */
#define TRR2		0x0004	/* Deny But Don't Lock Access To Mailbox 2 */
#define TRR3		0x0008	/* Deny But Don't Lock Access To Mailbox 3 */
#define TRR4		0x0010	/* Deny But Don't Lock Access To Mailbox 4 */
#define TRR5		0x0020	/* Deny But Don't Lock Access To Mailbox 5 */
#define TRR6		0x0040	/* Deny But Don't Lock Access To Mailbox 6 */
#define TRR7		0x0080	/* Deny But Don't Lock Access To Mailbox 7 */
#define TRR8		0x0100	/* Deny But Don't Lock Access To Mailbox 8 */
#define TRR9		0x0200	/* Deny But Don't Lock Access To Mailbox 9 */
#define TRR10		0x0400	/* Deny But Don't Lock Access To Mailbox 10 */
#define TRR11		0x0800	/* Deny But Don't Lock Access To Mailbox 11 */
#define TRR12		0x1000	/* Deny But Don't Lock Access To Mailbox 12 */
#define TRR13		0x2000	/* Deny But Don't Lock Access To Mailbox 13 */
#define TRR14		0x4000	/* Deny But Don't Lock Access To Mailbox 14 */
#define TRR15		0x8000	/* Deny But Don't Lock Access To Mailbox 15 */

/* CAN_TRR2 Masks */
#define TRR16		0x0001	/* Deny But Don't Lock Access To Mailbox 16 */
#define TRR17		0x0002	/* Deny But Don't Lock Access To Mailbox 17 */
#define TRR18		0x0004	/* Deny But Don't Lock Access To Mailbox 18 */
#define TRR19		0x0008	/* Deny But Don't Lock Access To Mailbox 19 */
#define TRR20		0x0010	/* Deny But Don't Lock Access To Mailbox 20 */
#define TRR21		0x0020	/* Deny But Don't Lock Access To Mailbox 21 */
#define TRR22		0x0040	/* Deny But Don't Lock Access To Mailbox 22 */
#define TRR23		0x0080	/* Deny But Don't Lock Access To Mailbox 23 */
#define TRR24		0x0100	/* Deny But Don't Lock Access To Mailbox 24 */
#define TRR25		0x0200	/* Deny But Don't Lock Access To Mailbox 25 */
#define TRR26		0x0400	/* Deny But Don't Lock Access To Mailbox 26 */
#define TRR27		0x0800	/* Deny But Don't Lock Access To Mailbox 27 */
#define TRR28		0x1000	/* Deny But Don't Lock Access To Mailbox 28 */
#define TRR29		0x2000	/* Deny But Don't Lock Access To Mailbox 29 */
#define TRR30		0x4000	/* Deny But Don't Lock Access To Mailbox 30 */
#define TRR31		0x8000	/* Deny But Don't Lock Access To Mailbox 31 */

/* CAN_TRS1 Masks */
#define TRS0		0x0001	/* Remote Frame Request For Mailbox 0 */
#define TRS1		0x0002	/* Remote Frame Request For Mailbox 1 */
#define TRS2		0x0004	/* Remote Frame Request For Mailbox 2 */
#define TRS3		0x0008	/* Remote Frame Request For Mailbox 3 */
#define TRS4		0x0010	/* Remote Frame Request For Mailbox 4 */
#define TRS5		0x0020	/* Remote Frame Request For Mailbox 5 */
#define TRS6		0x0040	/* Remote Frame Request For Mailbox 6 */
#define TRS7		0x0080	/* Remote Frame Request For Mailbox 7 */
#define TRS8		0x0100	/* Remote Frame Request For Mailbox 8 */
#define TRS9		0x0200	/* Remote Frame Request For Mailbox 9 */
#define TRS10		0x0400	/* Remote Frame Request For Mailbox 10 */
#define TRS11		0x0800	/* Remote Frame Request For Mailbox 11 */
#define TRS12		0x1000	/* Remote Frame Request For Mailbox 12 */
#define TRS13		0x2000	/* Remote Frame Request For Mailbox 13 */
#define TRS14		0x4000	/* Remote Frame Request For Mailbox 14 */
#define TRS15		0x8000	/* Remote Frame Request For Mailbox 15 */

/* CAN_TRS2 Masks */
#define TRS16		0x0001	/* Remote Frame Request For Mailbox 16 */
#define TRS17		0x0002	/* Remote Frame Request For Mailbox 17 */
#define TRS18		0x0004	/* Remote Frame Request For Mailbox 18 */
#define TRS19		0x0008	/* Remote Frame Request For Mailbox 19 */
#define TRS20		0x0010	/* Remote Frame Request For Mailbox 20 */
#define TRS21		0x0020	/* Remote Frame Request For Mailbox 21 */
#define TRS22		0x0040	/* Remote Frame Request For Mailbox 22 */
#define TRS23		0x0080	/* Remote Frame Request For Mailbox 23 */
#define TRS24		0x0100	/* Remote Frame Request For Mailbox 24 */
#define TRS25		0x0200	/* Remote Frame Request For Mailbox 25 */
#define TRS26		0x0400	/* Remote Frame Request For Mailbox 26 */
#define TRS27		0x0800	/* Remote Frame Request For Mailbox 27 */
#define TRS28		0x1000	/* Remote Frame Request For Mailbox 28 */
#define TRS29		0x2000	/* Remote Frame Request For Mailbox 29 */
#define TRS30		0x4000	/* Remote Frame Request For Mailbox 30 */
#define TRS31		0x8000	/* Remote Frame Request For Mailbox 31 */

/* CAN_AA1 Masks */
#define AA0			0x0001	/* Aborted Message In Mailbox 0 */
#define AA1			0x0002	/* Aborted Message In Mailbox 1 */
#define AA2			0x0004	/* Aborted Message In Mailbox 2 */
#define AA3			0x0008	/* Aborted Message In Mailbox 3 */
#define AA4			0x0010	/* Aborted Message In Mailbox 4 */
#define AA5			0x0020	/* Aborted Message In Mailbox 5 */
#define AA6			0x0040	/* Aborted Message In Mailbox 6 */
#define AA7			0x0080	/* Aborted Message In Mailbox 7 */
#define AA8			0x0100	/* Aborted Message In Mailbox 8 */
#define AA9			0x0200	/* Aborted Message In Mailbox 9 */
#define AA10		0x0400	/* Aborted Message In Mailbox 10 */
#define AA11		0x0800	/* Aborted Message In Mailbox 11 */
#define AA12		0x1000	/* Aborted Message In Mailbox 12 */
#define AA13		0x2000	/* Aborted Message In Mailbox 13 */
#define AA14		0x4000	/* Aborted Message In Mailbox 14 */
#define AA15		0x8000	/* Aborted Message In Mailbox 15 */

/* CAN_AA2 Masks */
#define AA16		0x0001	/* Aborted Message In Mailbox 16 */
#define AA17		0x0002	/* Aborted Message In Mailbox 17 */
#define AA18		0x0004	/* Aborted Message In Mailbox 18 */
#define AA19		0x0008	/* Aborted Message In Mailbox 19 */
#define AA20		0x0010	/* Aborted Message In Mailbox 20 */
#define AA21		0x0020	/* Aborted Message In Mailbox 21 */
#define AA22		0x0040	/* Aborted Message In Mailbox 22 */
#define AA23		0x0080	/* Aborted Message In Mailbox 23 */
#define AA24		0x0100	/* Aborted Message In Mailbox 24 */
#define AA25		0x0200	/* Aborted Message In Mailbox 25 */
#define AA26		0x0400	/* Aborted Message In Mailbox 26 */
#define AA27		0x0800	/* Aborted Message In Mailbox 27 */
#define AA28		0x1000	/* Aborted Message In Mailbox 28 */
#define AA29		0x2000	/* Aborted Message In Mailbox 29 */
#define AA30		0x4000	/* Aborted Message In Mailbox 30 */
#define AA31		0x8000	/* Aborted Message In Mailbox 31 */

/* CAN_TA1 Masks */
#define TA0			0x0001	/* Transmit Successful From Mailbox 0 */
#define TA1			0x0002	/* Transmit Successful From Mailbox 1 */
#define TA2			0x0004	/* Transmit Successful From Mailbox 2 */
#define TA3			0x0008	/* Transmit Successful From Mailbox 3 */
#define TA4			0x0010	/* Transmit Successful From Mailbox 4 */
#define TA5			0x0020	/* Transmit Successful From Mailbox 5 */
#define TA6			0x0040	/* Transmit Successful From Mailbox 6 */
#define TA7			0x0080	/* Transmit Successful From Mailbox 7 */
#define TA8			0x0100	/* Transmit Successful From Mailbox 8 */
#define TA9			0x0200	/* Transmit Successful From Mailbox 9 */
#define TA10		0x0400	/* Transmit Successful From Mailbox 10 */
#define TA11		0x0800	/* Transmit Successful From Mailbox 11 */
#define TA12		0x1000	/* Transmit Successful From Mailbox 12 */
#define TA13		0x2000	/* Transmit Successful From Mailbox 13 */
#define TA14		0x4000	/* Transmit Successful From Mailbox 14 */
#define TA15		0x8000	/* Transmit Successful From Mailbox 15 */

/* CAN_TA2 Masks */
#define TA16		0x0001	/* Transmit Successful From Mailbox 16 */
#define TA17		0x0002	/* Transmit Successful From Mailbox 17 */
#define TA18		0x0004	/* Transmit Successful From Mailbox 18 */
#define TA19		0x0008	/* Transmit Successful From Mailbox 19 */
#define TA20		0x0010	/* Transmit Successful From Mailbox 20 */
#define TA21		0x0020	/* Transmit Successful From Mailbox 21 */
#define TA22		0x0040	/* Transmit Successful From Mailbox 22 */
#define TA23		0x0080	/* Transmit Successful From Mailbox 23 */
#define TA24		0x0100	/* Transmit Successful From Mailbox 24 */
#define TA25		0x0200	/* Transmit Successful From Mailbox 25 */
#define TA26		0x0400	/* Transmit Successful From Mailbox 26 */
#define TA27		0x0800	/* Transmit Successful From Mailbox 27 */
#define TA28		0x1000	/* Transmit Successful From Mailbox 28 */
#define TA29		0x2000	/* Transmit Successful From Mailbox 29 */
#define TA30		0x4000	/* Transmit Successful From Mailbox 30 */
#define TA31		0x8000	/* Transmit Successful From Mailbox 31 */

/* CAN_MBTD Masks */
#define TDPTR		0x001F	/* Mailbox To Temporarily Disable */
#define TDA			0x0040	/* Temporary Disable Acknowledge */
#define TDR			0x0080	/* Temporary Disable Request */

/* CAN_RFH1 Masks */
#define RFH0		0x0001	/* Enable Automatic Remote Frame Handling For Mailbox 0 */
#define RFH1		0x0002	/* Enable Automatic Remote Frame Handling For Mailbox 1 */
#define RFH2		0x0004	/* Enable Automatic Remote Frame Handling For Mailbox 2 */
#define RFH3		0x0008	/* Enable Automatic Remote Frame Handling For Mailbox 3 */
#define RFH4		0x0010	/* Enable Automatic Remote Frame Handling For Mailbox 4 */
#define RFH5		0x0020	/* Enable Automatic Remote Frame Handling For Mailbox 5 */
#define RFH6		0x0040	/* Enable Automatic Remote Frame Handling For Mailbox 6 */
#define RFH7		0x0080	/* Enable Automatic Remote Frame Handling For Mailbox 7 */
#define RFH8		0x0100	/* Enable Automatic Remote Frame Handling For Mailbox 8 */
#define RFH9		0x0200	/* Enable Automatic Remote Frame Handling For Mailbox 9 */
#define RFH10		0x0400	/* Enable Automatic Remote Frame Handling For Mailbox 10 */
#define RFH11		0x0800	/* Enable Automatic Remote Frame Handling For Mailbox 11 */
#define RFH12		0x1000	/* Enable Automatic Remote Frame Handling For Mailbox 12 */
#define RFH13		0x2000	/* Enable Automatic Remote Frame Handling For Mailbox 13 */
#define RFH14		0x4000	/* Enable Automatic Remote Frame Handling For Mailbox 14 */
#define RFH15		0x8000	/* Enable Automatic Remote Frame Handling For Mailbox 15 */

/* CAN_RFH2 Masks */
#define RFH16		0x0001	/* Enable Automatic Remote Frame Handling For Mailbox 16 */
#define RFH17		0x0002	/* Enable Automatic Remote Frame Handling For Mailbox 17 */
#define RFH18		0x0004	/* Enable Automatic Remote Frame Handling For Mailbox 18 */
#define RFH19		0x0008	/* Enable Automatic Remote Frame Handling For Mailbox 19 */
#define RFH20		0x0010	/* Enable Automatic Remote Frame Handling For Mailbox 20 */
#define RFH21		0x0020	/* Enable Automatic Remote Frame Handling For Mailbox 21 */
#define RFH22		0x0040	/* Enable Automatic Remote Frame Handling For Mailbox 22 */
#define RFH23		0x0080	/* Enable Automatic Remote Frame Handling For Mailbox 23 */
#define RFH24		0x0100	/* Enable Automatic Remote Frame Handling For Mailbox 24 */
#define RFH25		0x0200	/* Enable Automatic Remote Frame Handling For Mailbox 25 */
#define RFH26		0x0400	/* Enable Automatic Remote Frame Handling For Mailbox 26 */
#define RFH27		0x0800	/* Enable Automatic Remote Frame Handling For Mailbox 27 */
#define RFH28		0x1000	/* Enable Automatic Remote Frame Handling For Mailbox 28 */
#define RFH29		0x2000	/* Enable Automatic Remote Frame Handling For Mailbox 29 */
#define RFH30		0x4000	/* Enable Automatic Remote Frame Handling For Mailbox 30 */
#define RFH31		0x8000	/* Enable Automatic Remote Frame Handling For Mailbox 31 */

/* CAN_MBTIF1 Masks */
#define MBTIF0		0x0001	/* TX Interrupt Active In Mailbox 0 */
#define MBTIF1		0x0002	/* TX Interrupt Active In Mailbox 1 */
#define MBTIF2		0x0004	/* TX Interrupt Active In Mailbox 2 */
#define MBTIF3		0x0008	/* TX Interrupt Active In Mailbox 3 */
#define MBTIF4		0x0010	/* TX Interrupt Active In Mailbox 4 */
#define MBTIF5		0x0020	/* TX Interrupt Active In Mailbox 5 */
#define MBTIF6		0x0040	/* TX Interrupt Active In Mailbox 6 */
#define MBTIF7		0x0080	/* TX Interrupt Active In Mailbox 7 */
#define MBTIF8		0x0100	/* TX Interrupt Active In Mailbox 8 */
#define MBTIF9		0x0200	/* TX Interrupt Active In Mailbox 9 */
#define MBTIF10		0x0400	/* TX Interrupt Active In Mailbox 10 */
#define MBTIF11		0x0800	/* TX Interrupt Active In Mailbox 11 */
#define MBTIF12		0x1000	/* TX Interrupt Active In Mailbox 12 */
#define MBTIF13		0x2000	/* TX Interrupt Active In Mailbox 13 */
#define MBTIF14		0x4000	/* TX Interrupt Active In Mailbox 14 */
#define MBTIF15		0x8000	/* TX Interrupt Active In Mailbox 15 */

/* CAN_MBTIF2 Masks */
#define MBTIF16		0x0001	/* TX Interrupt Active In Mailbox 16 */
#define MBTIF17		0x0002	/* TX Interrupt Active In Mailbox 17 */
#define MBTIF18		0x0004	/* TX Interrupt Active In Mailbox 18 */
#define MBTIF19		0x0008	/* TX Interrupt Active In Mailbox 19 */
#define MBTIF20		0x0010	/* TX Interrupt Active In Mailbox 20 */
#define MBTIF21		0x0020	/* TX Interrupt Active In Mailbox 21 */
#define MBTIF22		0x0040	/* TX Interrupt Active In Mailbox 22 */
#define MBTIF23		0x0080	/* TX Interrupt Active In Mailbox 23 */
#define MBTIF24		0x0100	/* TX Interrupt Active In Mailbox 24 */
#define MBTIF25		0x0200	/* TX Interrupt Active In Mailbox 25 */
#define MBTIF26		0x0400	/* TX Interrupt Active In Mailbox 26 */
#define MBTIF27		0x0800	/* TX Interrupt Active In Mailbox 27 */
#define MBTIF28		0x1000	/* TX Interrupt Active In Mailbox 28 */
#define MBTIF29		0x2000	/* TX Interrupt Active In Mailbox 29 */
#define MBTIF30		0x4000	/* TX Interrupt Active In Mailbox 30 */
#define MBTIF31		0x8000	/* TX Interrupt Active In Mailbox 31 */

/* CAN_MBRIF1 Masks */
#define MBRIF0		0x0001	/* RX Interrupt Active In Mailbox 0 */
#define MBRIF1		0x0002	/* RX Interrupt Active In Mailbox 1 */
#define MBRIF2		0x0004	/* RX Interrupt Active In Mailbox 2 */
#define MBRIF3		0x0008	/* RX Interrupt Active In Mailbox 3 */
#define MBRIF4		0x0010	/* RX Interrupt Active In Mailbox 4 */
#define MBRIF5		0x0020	/* RX Interrupt Active In Mailbox 5 */
#define MBRIF6		0x0040	/* RX Interrupt Active In Mailbox 6 */
#define MBRIF7		0x0080	/* RX Interrupt Active In Mailbox 7 */
#define MBRIF8		0x0100	/* RX Interrupt Active In Mailbox 8 */
#define MBRIF9		0x0200	/* RX Interrupt Active In Mailbox 9 */
#define MBRIF10		0x0400	/* RX Interrupt Active In Mailbox 10 */
#define MBRIF11		0x0800	/* RX Interrupt Active In Mailbox 11 */
#define MBRIF12		0x1000	/* RX Interrupt Active In Mailbox 12 */
#define MBRIF13		0x2000	/* RX Interrupt Active In Mailbox 13 */
#define MBRIF14		0x4000	/* RX Interrupt Active In Mailbox 14 */
#define MBRIF15		0x8000	/* RX Interrupt Active In Mailbox 15 */

/* CAN_MBRIF2 Masks */
#define MBRIF16		0x0001	/* RX Interrupt Active In Mailbox 16 */
#define MBRIF17		0x0002	/* RX Interrupt Active In Mailbox 17 */
#define MBRIF18		0x0004	/* RX Interrupt Active In Mailbox 18 */
#define MBRIF19		0x0008	/* RX Interrupt Active In Mailbox 19 */
#define MBRIF20		0x0010	/* RX Interrupt Active In Mailbox 20 */
#define MBRIF21		0x0020	/* RX Interrupt Active In Mailbox 21 */
#define MBRIF22		0x0040	/* RX Interrupt Active In Mailbox 22 */
#define MBRIF23		0x0080	/* RX Interrupt Active In Mailbox 23 */
#define MBRIF24		0x0100	/* RX Interrupt Active In Mailbox 24 */
#define MBRIF25		0x0200	/* RX Interrupt Active In Mailbox 25 */
#define MBRIF26		0x0400	/* RX Interrupt Active In Mailbox 26 */
#define MBRIF27		0x0800	/* RX Interrupt Active In Mailbox 27 */
#define MBRIF28		0x1000	/* RX Interrupt Active In Mailbox 28 */
#define MBRIF29		0x2000	/* RX Interrupt Active In Mailbox 29 */
#define MBRIF30		0x4000	/* RX Interrupt Active In Mailbox 30 */
#define MBRIF31		0x8000	/* RX Interrupt Active In Mailbox 31 */

/* CAN_MBIM1 Masks */
#define MBIM0		0x0001	/* Enable Interrupt For Mailbox 0 */
#define MBIM1		0x0002	/* Enable Interrupt For Mailbox 1 */
#define MBIM2		0x0004	/* Enable Interrupt For Mailbox 2 */
#define MBIM3		0x0008	/* Enable Interrupt For Mailbox 3 */
#define MBIM4		0x0010	/* Enable Interrupt For Mailbox 4 */
#define MBIM5		0x0020	/* Enable Interrupt For Mailbox 5 */
#define MBIM6		0x0040	/* Enable Interrupt For Mailbox 6 */
#define MBIM7		0x0080	/* Enable Interrupt For Mailbox 7 */
#define MBIM8		0x0100	/* Enable Interrupt For Mailbox 8 */
#define MBIM9		0x0200	/* Enable Interrupt For Mailbox 9 */
#define MBIM10		0x0400	/* Enable Interrupt For Mailbox 10 */
#define MBIM11		0x0800	/* Enable Interrupt For Mailbox 11 */
#define MBIM12		0x1000	/* Enable Interrupt For Mailbox 12 */
#define MBIM13		0x2000	/* Enable Interrupt For Mailbox 13 */
#define MBIM14		0x4000	/* Enable Interrupt For Mailbox 14 */
#define MBIM15		0x8000	/* Enable Interrupt For Mailbox 15 */

/* CAN_MBIM2 Masks */
#define MBIM16		0x0001	/* Enable Interrupt For Mailbox 16 */
#define MBIM17		0x0002	/* Enable Interrupt For Mailbox 17 */
#define MBIM18		0x0004	/* Enable Interrupt For Mailbox 18 */
#define MBIM19		0x0008	/* Enable Interrupt For Mailbox 19 */
#define MBIM20		0x0010	/* Enable Interrupt For Mailbox 20 */
#define MBIM21		0x0020	/* Enable Interrupt For Mailbox 21 */
#define MBIM22		0x0040	/* Enable Interrupt For Mailbox 22 */
#define MBIM23		0x0080	/* Enable Interrupt For Mailbox 23 */
#define MBIM24		0x0100	/* Enable Interrupt For Mailbox 24 */
#define MBIM25		0x0200	/* Enable Interrupt For Mailbox 25 */
#define MBIM26		0x0400	/* Enable Interrupt For Mailbox 26 */
#define MBIM27		0x0800	/* Enable Interrupt For Mailbox 27 */
#define MBIM28		0x1000	/* Enable Interrupt For Mailbox 28 */
#define MBIM29		0x2000	/* Enable Interrupt For Mailbox 29 */
#define MBIM30		0x4000	/* Enable Interrupt For Mailbox 30 */
#define MBIM31		0x8000	/* Enable Interrupt For Mailbox 31 */

/* CAN_GIM Masks */
#define EWTIM		0x0001	/* Enable TX Error Count Interrupt */
#define EWRIM		0x0002	/* Enable RX Error Count Interrupt */
#define EPIM		0x0004	/* Enable Error-Passive Mode Interrupt */
#define BOIM		0x0008	/* Enable Bus Off Interrupt */
#define WUIM		0x0010	/* Enable Wake-Up Interrupt */
#define UIAIM		0x0020	/* Enable Access To Unimplemented Address Interrupt */
#define AAIM		0x0040	/* Enable Abort Acknowledge Interrupt */
#define RMLIM		0x0080	/* Enable RX Message Lost Interrupt */
#define UCEIM		0x0100	/* Enable Universal Counter Overflow Interrupt */
#define EXTIM		0x0200	/* Enable External Trigger Output Interrupt */
#define ADIM		0x0400	/* Enable Access Denied Interrupt */

/* CAN_GIS Masks */
#define EWTIS		0x0001	/* TX Error Count IRQ Status */
#define EWRIS		0x0002	/* RX Error Count IRQ Status */
#define EPIS		0x0004	/* Error-Passive Mode IRQ Status */
#define BOIS		0x0008	/* Bus Off IRQ Status */
#define WUIS		0x0010	/* Wake-Up IRQ Status */
#define UIAIS		0x0020	/* Access To Unimplemented Address IRQ Status */
#define AAIS		0x0040	/* Abort Acknowledge IRQ Status */
#define RMLIS		0x0080	/* RX Message Lost IRQ Status */
#define UCEIS		0x0100	/* Universal Counter Overflow IRQ Status */
#define EXTIS		0x0200	/* External Trigger Output IRQ Status */
#define ADIS		0x0400	/* Access Denied IRQ Status */

/* CAN_GIF Masks */
#define EWTIF		0x0001	/* TX Error Count IRQ Flag */
#define EWRIF		0x0002	/* RX Error Count IRQ Flag */
#define EPIF		0x0004	/* Error-Passive Mode IRQ Flag */
#define BOIF		0x0008	/* Bus Off IRQ Flag */
#define WUIF		0x0010	/* Wake-Up IRQ Flag */
#define UIAIF		0x0020	/* Access To Unimplemented Address IRQ Flag */
#define AAIF		0x0040	/* Abort Acknowledge IRQ Flag */
#define RMLIF		0x0080	/* RX Message Lost IRQ Flag */
#define UCEIF		0x0100	/* Universal Counter Overflow IRQ Flag */
#define EXTIF		0x0200	/* External Trigger Output IRQ Flag */
#define ADIF		0x0400	/* Access Denied IRQ Flag */

/* CAN_UCCNF Masks */
#define UCCNF		0x000F	/* Universal Counter Mode */
#define UC_STAMP	0x0001	/*  Timestamp Mode */
#define UC_WDOG		0x0002	/*  Watchdog Mode */
#define UC_AUTOTX	0x0003	/*  Auto-Transmit Mode */
#define UC_ERROR	0x0006	/*  CAN Error Frame Count */
#define UC_OVER		0x0007	/*  CAN Overload Frame Count */
#define UC_LOST		0x0008	/*  Arbitration Lost During TX Count */
#define UC_AA		0x0009	/*  TX Abort Count */
#define UC_TA		0x000A	/*  TX Successful Count */
#define UC_REJECT	0x000B	/*  RX Message Rejected Count */
#define UC_RML		0x000C	/*  RX Message Lost Count */
#define UC_RX		0x000D	/*  Total Successful RX Messages Count */
#define UC_RMP		0x000E	/*  Successful RX W/Matching ID Count */
#define UC_ALL		0x000F	/*  Correct Message On CAN Bus Line Count */
#define UCRC		0x0020	/* Universal Counter Reload/Clear */
#define UCCT		0x0040	/* Universal Counter CAN Trigger */
#define UCE			0x0080	/* Universal Counter Enable */

/* CAN_ESR Masks */
#define ACKE		0x0004	/* Acknowledge Error */
#define SER			0x0008	/* Stuff Error */
#define CRCE		0x0010	/* CRC Error */
#define SA0			0x0020	/* Stuck At Dominant Error */
#define BEF			0x0040	/* Bit Error Flag */
#define FER			0x0080	/* Form Error Flag */

/* CAN_EWR Masks */
#define EWLREC		0x00FF	/* RX Error Count Limit (For EWRIS) */
#define EWLTEC		0xFF00	/* TX Error Count Limit (For EWTIS) */

#endif
