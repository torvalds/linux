/* SPDX-License-Identifier: GPL-2.0-or-later */
/******************************************************************************
 *
 *	(C)Copyright 1998,1999 SysKonnect,
 *	a business unit of Schneider & Koch & Co. Datensysteme GmbH.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#ifndef	_SKFBI_H_
#define	_SKFBI_H_

/*
 * FDDI-Fx (x := {I(SA), P(CI)})
 *	address calculation & function defines
 */

/*--------------------------------------------------------------------------*/
#ifdef	PCI

/*
 *	(DV)	= only defined for Da Vinci
 *	(ML)	= only defined for Monalisa
 */


/*
 * I2C Address (PCI Config)
 *
 * Note: The temperature and voltage sensors are relocated on a different
 *	 I2C bus.
 */
#define I2C_ADDR_VPD	0xA0	/* I2C address for the VPD EEPROM */ 

/*
 *	Control Register File:
 *	Bank 0
 */
#define	B0_RAP		0x0000	/*  8 bit register address port */
	/* 0x0001 - 0x0003:	reserved */
#define	B0_CTRL		0x0004	/*  8 bit control register */
#define	B0_DAS		0x0005	/*  8 Bit control register (DAS) */
#define	B0_LED		0x0006	/*  8 Bit LED register */
#define	B0_TST_CTRL	0x0007	/*  8 bit test control register */
#define	B0_ISRC		0x0008	/* 32 bit Interrupt source register */
#define	B0_IMSK		0x000c	/* 32 bit Interrupt mask register */

/* 0x0010 - 0x006b:	formac+ (supernet_3) fequently used registers */
#define B0_CMDREG1	0x0010	/* write command reg 1 instruction */
#define B0_CMDREG2	0x0014	/* write command reg 2 instruction */
#define B0_ST1U		0x0010	/* read upper 16-bit of status reg 1 */
#define B0_ST1L		0x0014	/* read lower 16-bit of status reg 1 */
#define B0_ST2U		0x0018	/* read upper 16-bit of status reg 2 */
#define B0_ST2L		0x001c	/* read lower 16-bit of status reg 2 */

#define B0_MARR		0x0020	/* r/w the memory read addr register */
#define B0_MARW		0x0024	/* r/w the memory write addr register*/
#define B0_MDRU		0x0028	/* r/w upper 16-bit of mem. data reg */
#define B0_MDRL		0x002c	/* r/w lower 16-bit of mem. data reg */

#define	B0_MDREG3	0x0030	/* r/w Mode Register 3 */
#define	B0_ST3U		0x0034	/* read upper 16-bit of status reg 3 */
#define	B0_ST3L		0x0038	/* read lower 16-bit of status reg 3 */
#define	B0_IMSK3U	0x003c	/* r/w upper 16-bit of IMSK reg 3 */
#define	B0_IMSK3L	0x0040	/* r/w lower 16-bit of IMSK reg 3 */
#define	B0_IVR		0x0044	/* read Interrupt Vector register */
#define	B0_IMR		0x0048	/* r/w Interrupt mask register */
/* 0x4c	Hidden */

#define B0_CNTRL_A	0x0050	/* control register A (r/w) */
#define B0_CNTRL_B	0x0054	/* control register B (r/w) */
#define B0_INTR_MASK	0x0058	/* interrupt mask (r/w) */
#define B0_XMIT_VECTOR	0x005c	/* transmit vector register (r/w) */

#define B0_STATUS_A	0x0060	/* status register A (read only) */
#define B0_STATUS_B	0x0064	/* status register B (read only) */
#define B0_CNTRL_C	0x0068	/* control register C (r/w) */
#define	B0_MDREG1	0x006c	/* r/w Mode Register 1 */

#define	B0_R1_CSR	0x0070	/* 32 bit BMU control/status reg (rec q 1) */
#define	B0_R2_CSR	0x0074	/* 32 bit BMU control/status reg (rec q 2)(DV)*/
#define	B0_XA_CSR	0x0078	/* 32 bit BMU control/status reg (a xmit q) */
#define	B0_XS_CSR	0x007c	/* 32 bit BMU control/status reg (s xmit q) */

/*
 *	Bank 1
 *	- completely empty (this is the RAP Block window)
 *	Note: if RAP = 1 this page is reserved
 */

/*
 *	Bank 2
 */
#define	B2_MAC_0	0x0100	/*  8 bit MAC address Byte 0 */
#define	B2_MAC_1	0x0101	/*  8 bit MAC address Byte 1 */
#define	B2_MAC_2	0x0102	/*  8 bit MAC address Byte 2 */
#define	B2_MAC_3	0x0103	/*  8 bit MAC address Byte 3 */
#define	B2_MAC_4	0x0104	/*  8 bit MAC address Byte 4 */
#define	B2_MAC_5	0x0105	/*  8 bit MAC address Byte 5 */
#define	B2_MAC_6	0x0106	/*  8 bit MAC address Byte 6 (== 0) (DV) */
#define	B2_MAC_7	0x0107	/*  8 bit MAC address Byte 7 (== 0) (DV) */

#define B2_CONN_TYP	0x0108	/*  8 bit Connector type */
#define B2_PMD_TYP	0x0109	/*  8 bit PMD type */
				/* 0x010a - 0x010b:	reserved */
	/* Eprom registers are currently of no use */
#define B2_E_0		0x010c	/*  8 bit EPROM Byte 0 */
#define B2_E_1		0x010d	/*  8 bit EPROM Byte 1 */
#define B2_E_2		0x010e	/*  8 bit EPROM Byte 2 */
#define B2_E_3		0x010f	/*  8 bit EPROM Byte 3 */
#define B2_FAR		0x0110	/* 32 bit Flash-Prom Address Register/Counter */
#define B2_FDP		0x0114	/*  8 bit Flash-Prom Data Port */
				/* 0x0115 - 0x0117:	reserved */
#define B2_LD_CRTL	0x0118	/*  8 bit loader control */
#define B2_LD_TEST	0x0119	/*  8 bit loader test */
				/* 0x011a - 0x011f:	reserved */
#define B2_TI_INI	0x0120	/* 32 bit Timer init value */
#define B2_TI_VAL	0x0124	/* 32 bit Timer value */
#define B2_TI_CRTL	0x0128	/*  8 bit Timer control */
#define B2_TI_TEST	0x0129	/*  8 Bit Timer Test */
				/* 0x012a - 0x012f:	reserved */
#define B2_WDOG_INI	0x0130	/* 32 bit Watchdog init value */
#define B2_WDOG_VAL	0x0134	/* 32 bit Watchdog value */
#define B2_WDOG_CRTL	0x0138	/*  8 bit Watchdog control */
#define B2_WDOG_TEST	0x0139	/*  8 Bit Watchdog Test */
				/* 0x013a - 0x013f:	reserved */
#define B2_RTM_INI	0x0140	/* 32 bit RTM init value */
#define B2_RTM_VAL	0x0144	/* 32 bit RTM value */
#define B2_RTM_CRTL	0x0148	/*  8 bit RTM control */
#define B2_RTM_TEST	0x0149	/*  8 Bit RTM Test */

#define B2_TOK_COUNT	0x014c	/* (ML)	32 bit	Token Counter */
#define B2_DESC_ADDR_H	0x0150	/* (ML) 32 bit	Desciptor Base Addr Reg High */
#define B2_CTRL_2	0x0154	/* (ML)	 8 bit	Control Register 2 */
#define B2_IFACE_REG	0x0155	/* (ML)	 8 bit	Interface Register */
				/* 0x0156:		reserved */
#define B2_TST_CTRL_2	0x0157	/* (ML)  8 bit	Test Control Register 2 */
#define B2_I2C_CTRL	0x0158	/* (ML)	32 bit	I2C Control Register */
#define B2_I2C_DATA	0x015c	/* (ML) 32 bit	I2C Data Register */

#define B2_IRQ_MOD_INI	0x0160	/* (ML) 32 bit	IRQ Moderation Timer Init Reg. */
#define B2_IRQ_MOD_VAL	0x0164	/* (ML)	32 bit	IRQ Moderation Timer Value */
#define B2_IRQ_MOD_CTRL	0x0168	/* (ML)  8 bit	IRQ Moderation Timer Control */
#define B2_IRQ_MOD_TEST	0x0169	/* (ML)	 8 bit	IRQ Moderation Timer Test */
				/* 0x016a - 0x017f:	reserved */

/*
 *	Bank 3
 */
/*
 * This is a copy of the Configuration register file (lower half)
 */
#define B3_CFG_SPC	0x180

/*
 *	Bank 4
 */
#define B4_R1_D		0x0200	/* 	4*32 bit current receive Descriptor  */
#define B4_R1_DA	0x0210	/* 	32 bit current rec desc address	     */
#define B4_R1_AC	0x0214	/* 	32 bit current receive Address Count */
#define B4_R1_BC	0x0218	/*	32 bit current receive Byte Counter  */
#define B4_R1_CSR	0x021c	/* 	32 bit BMU Control/Status Register   */
#define B4_R1_F		0x0220	/* 	32 bit flag register		     */
#define B4_R1_T1	0x0224	/* 	32 bit Test Register 1		     */
#define B4_R1_T1_TR	0x0224	/* 	8 bit Test Register 1 TR	     */
#define B4_R1_T1_WR	0x0225	/* 	8 bit Test Register 1 WR	     */
#define B4_R1_T1_RD	0x0226	/* 	8 bit Test Register 1 RD	     */
#define B4_R1_T1_SV	0x0227	/* 	8 bit Test Register 1 SV	     */
#define B4_R1_T2	0x0228	/* 	32 bit Test Register 2		     */
#define B4_R1_T3	0x022c	/* 	32 bit Test Register 3		     */
#define B4_R1_DA_H	0x0230	/* (ML)	32 bit Curr Rx Desc Address High     */
#define B4_R1_AC_H	0x0234	/* (ML)	32 bit Curr Addr Counter High dword  */
				/* 0x0238 - 0x023f:	reserved	  */
				/* Receive queue 2 is removed on Monalisa */
#define B4_R2_D		0x0240	/* 4*32 bit current receive Descriptor	(q2) */
#define B4_R2_DA	0x0250	/* 32 bit current rec desc address	(q2) */
#define B4_R2_AC	0x0254	/* 32 bit current receive Address Count	(q2) */
#define B4_R2_BC	0x0258	/* 32 bit current receive Byte Counter	(q2) */
#define B4_R2_CSR	0x025c	/* 32 bit BMU Control/Status Register	(q2) */
#define B4_R2_F		0x0260	/* 32 bit flag register			(q2) */
#define B4_R2_T1	0x0264	/* 32 bit Test Register 1		(q2) */
#define B4_R2_T1_TR	0x0264	/* 8 bit Test Register 1 TR		(q2) */
#define B4_R2_T1_WR	0x0265	/* 8 bit Test Register 1 WR		(q2) */
#define B4_R2_T1_RD	0x0266	/* 8 bit Test Register 1 RD		(q2) */
#define B4_R2_T1_SV	0x0267	/* 8 bit Test Register 1 SV		(q2) */
#define B4_R2_T2	0x0268	/* 32 bit Test Register 2		(q2) */
#define B4_R2_T3	0x026c	/* 32 bit Test Register 3		(q2) */
				/* 0x0270 - 0x027c:	reserved */

/*
 *	Bank 5
 */
#define B5_XA_D		0x0280	/* 4*32 bit current transmit Descriptor	(xa) */
#define B5_XA_DA	0x0290	/* 32 bit current tx desc address	(xa) */
#define B5_XA_AC	0x0294	/* 32 bit current tx Address Count	(xa) */
#define B5_XA_BC	0x0298	/* 32 bit current tx Byte Counter	(xa) */
#define B5_XA_CSR	0x029c	/* 32 bit BMU Control/Status Register	(xa) */
#define B5_XA_F		0x02a0	/* 32 bit flag register			(xa) */
#define B5_XA_T1	0x02a4	/* 32 bit Test Register 1		(xa) */
#define B5_XA_T1_TR	0x02a4	/* 8 bit Test Register 1 TR		(xa) */
#define B5_XA_T1_WR	0x02a5	/* 8 bit Test Register 1 WR		(xa) */
#define B5_XA_T1_RD	0x02a6	/* 8 bit Test Register 1 RD		(xa) */
#define B5_XA_T1_SV	0x02a7	/* 8 bit Test Register 1 SV		(xa) */
#define B5_XA_T2	0x02a8	/* 32 bit Test Register 2		(xa) */
#define B5_XA_T3	0x02ac	/* 32 bit Test Register 3		(xa) */
#define B5_XA_DA_H	0x02b0	/* (ML)	32 bit Curr Tx Desc Address High     */
#define B5_XA_AC_H	0x02b4	/* (ML)	32 bit Curr Addr Counter High dword  */
				/* 0x02b8 - 0x02bc:	reserved */
#define B5_XS_D		0x02c0	/* 4*32 bit current transmit Descriptor	(xs) */
#define B5_XS_DA	0x02d0	/* 32 bit current tx desc address	(xs) */
#define B5_XS_AC	0x02d4	/* 32 bit current transmit Address Count(xs) */
#define B5_XS_BC	0x02d8	/* 32 bit current transmit Byte Counter	(xs) */
#define B5_XS_CSR	0x02dc	/* 32 bit BMU Control/Status Register	(xs) */
#define B5_XS_F		0x02e0	/* 32 bit flag register			(xs) */
#define B5_XS_T1	0x02e4	/* 32 bit Test Register 1		(xs) */
#define B5_XS_T1_TR	0x02e4	/* 8 bit Test Register 1 TR		(xs) */
#define B5_XS_T1_WR	0x02e5	/* 8 bit Test Register 1 WR		(xs) */
#define B5_XS_T1_RD	0x02e6	/* 8 bit Test Register 1 RD		(xs) */
#define B5_XS_T1_SV	0x02e7	/* 8 bit Test Register 1 SV		(xs) */
#define B5_XS_T2	0x02e8	/* 32 bit Test Register 2		(xs) */
#define B5_XS_T3	0x02ec	/* 32 bit Test Register 3		(xs) */
#define B5_XS_DA_H	0x02f0	/* (ML)	32 bit Curr Tx Desc Address High     */
#define B5_XS_AC_H	0x02f4	/* (ML)	32 bit Curr Addr Counter High dword  */
				/* 0x02f8 - 0x02fc:	reserved */

/*
 *	Bank 6
 */
/* External PLC-S registers (SN2 compatibility for DV) */
/* External registers (ML) */
#define B6_EXT_REG	0x300

/*
 *	Bank 7
 */
/* DAS PLC-S Registers */

/*
 *	Bank 8 - 15
 */
/* IFCP registers */

/*---------------------------------------------------------------------------*/
/* Definitions of the Bits in the registers */

/*	B0_RAP		16 bit register address port */
#define	RAP_RAP		0x0f	/* Bit 3..0:	0 = block0, .., f = block15 */

/*	B0_CTRL		8 bit control register */
#define CTRL_FDDI_CLR	(1<<7)	/* Bit 7: (ML)	Clear FDDI Reset */
#define CTRL_FDDI_SET	(1<<6)	/* Bit 6: (ML)	Set FDDI Reset */
#define	CTRL_HPI_CLR	(1<<5)	/* Bit 5:	Clear HPI SM reset */
#define	CTRL_HPI_SET	(1<<4)	/* Bit 4:	Set HPI SM reset */
#define	CTRL_MRST_CLR	(1<<3)	/* Bit 3:	Clear Master reset */
#define	CTRL_MRST_SET	(1<<2)	/* Bit 2:	Set Master reset */
#define	CTRL_RST_CLR	(1<<1)	/* Bit 1:	Clear Software reset */
#define	CTRL_RST_SET	(1<<0)	/* Bit 0:	Set Software reset */

/*	B0_DAS		8 Bit control register (DAS) */
#define BUS_CLOCK	(1<<7)	/* Bit 7: (ML)	Bus Clock 0/1 = 33/66MHz */
#define BUS_SLOT_SZ	(1<<6)	/* Bit 6: (ML)	Slot Size 0/1 = 32/64 bit slot*/
				/* Bit 5..4:	reserved */
#define	DAS_AVAIL	(1<<3)	/* Bit 3:	1 = DAS, 0 = SAS */
#define DAS_BYP_ST	(1<<2)	/* Bit 2:	1 = avail,SAS, 0 = not avail */
#define DAS_BYP_INS	(1<<1)	/* Bit 1:	1 = insert Bypass */
#define DAS_BYP_RMV	(1<<0)	/* Bit 0:	1 = remove Bypass */

/*	B0_LED		8 Bit LED register */
				/* Bit 7..6:	reserved */
#define LED_2_ON	(1<<5)	/* Bit 5:	1 = switch LED_2 on (left,gn)*/
#define LED_2_OFF	(1<<4)	/* Bit 4:	1 = switch LED_2 off */
#define LED_1_ON	(1<<3)	/* Bit 3:	1 = switch LED_1 on (mid,yel)*/
#define LED_1_OFF	(1<<2)	/* Bit 2:	1 = switch LED_1 off */
#define LED_0_ON	(1<<1)	/* Bit 1:	1 = switch LED_0 on (rght,gn)*/
#define LED_0_OFF	(1<<0)	/* Bit 0:	1 = switch LED_0 off */
/* This hardware defines are very ugly therefore we define some others */

#define LED_GA_ON	LED_2_ON	/* S port = A port */
#define LED_GA_OFF	LED_2_OFF	/* S port = A port */
#define LED_MY_ON	LED_1_ON
#define LED_MY_OFF	LED_1_OFF
#define LED_GB_ON	LED_0_ON
#define LED_GB_OFF	LED_0_OFF

/*	B0_TST_CTRL	8 bit test control register */
#define	TST_FRC_DPERR_MR	(1<<7)	/* Bit 7:  force DATAPERR on MST RE. */
#define	TST_FRC_DPERR_MW	(1<<6)	/* Bit 6:  force DATAPERR on MST WR. */
#define	TST_FRC_DPERR_TR	(1<<5)	/* Bit 5:  force DATAPERR on TRG RE. */
#define	TST_FRC_DPERR_TW	(1<<4)	/* Bit 4:  force DATAPERR on TRG WR. */
#define	TST_FRC_APERR_M		(1<<3)	/* Bit 3:  force ADDRPERR on MST     */
#define	TST_FRC_APERR_T		(1<<2)	/* Bit 2:  force ADDRPERR on TRG     */
#define	TST_CFG_WRITE_ON	(1<<1)	/* Bit 1:  ena configuration reg. WR */
#define	TST_CFG_WRITE_OFF	(1<<0)	/* Bit 0:  dis configuration reg. WR */

/*	B0_ISRC		32 bit Interrupt source register */
					/* Bit 31..28:	reserved	     */
#define IS_I2C_READY	(1L<<27)	/* Bit 27: (ML)	IRQ on end of I2C tx */
#define IS_IRQ_SW	(1L<<26)	/* Bit 26: (ML)	SW forced IRQ	     */
#define IS_EXT_REG	(1L<<25)	/* Bit 25: (ML) IRQ from external reg*/
#define	IS_IRQ_STAT	(1L<<24)	/* Bit 24:	IRQ status exception */
					/*   PERR, RMABORT, RTABORT DATAPERR */
#define	IS_IRQ_MST_ERR	(1L<<23)	/* Bit 23:	IRQ master error     */
					/*   RMABORT, RTABORT, DATAPERR	     */
#define	IS_TIMINT	(1L<<22)	/* Bit 22:	IRQ_TIMER	*/
#define	IS_TOKEN	(1L<<21)	/* Bit 21:	IRQ_RTM		*/
/*
 * Note: The DAS is our First Port (!=PA)
 */
#define	IS_PLINT1	(1L<<20)	/* Bit 20:	IRQ_PHY_DAS	*/
#define	IS_PLINT2	(1L<<19)	/* Bit 19:	IRQ_IFCP_4	*/
#define	IS_MINTR3	(1L<<18)	/* Bit 18:	IRQ_IFCP_3/IRQ_PHY */
#define	IS_MINTR2	(1L<<17)	/* Bit 17:	IRQ_IFCP_2/IRQ_MAC_2 */
#define	IS_MINTR1	(1L<<16)	/* Bit 16:	IRQ_IFCP_1/IRQ_MAC_1 */
/* Receive Queue 1 */
#define	IS_R1_P		(1L<<15)	/* Bit 15:	Parity Error (q1) */
#define	IS_R1_B		(1L<<14)	/* Bit 14:	End of Buffer (q1) */
#define	IS_R1_F		(1L<<13)	/* Bit 13:	End of Frame (q1) */
#define	IS_R1_C		(1L<<12)	/* Bit 12:	Encoding Error (q1) */
/* Receive Queue 2 */
#define	IS_R2_P		(1L<<11)	/* Bit 11: (DV)	Parity Error (q2) */
#define	IS_R2_B		(1L<<10)	/* Bit 10: (DV)	End of Buffer (q2) */
#define	IS_R2_F		(1L<<9)		/* Bit	9: (DV)	End of Frame (q2) */
#define	IS_R2_C		(1L<<8)		/* Bit	8: (DV)	Encoding Error (q2) */
/* Asynchronous Transmit queue */
					/* Bit  7:	reserved */
#define	IS_XA_B		(1L<<6)		/* Bit	6:	End of Buffer (xa) */
#define	IS_XA_F		(1L<<5)		/* Bit	5:	End of Frame (xa) */
#define	IS_XA_C		(1L<<4)		/* Bit	4:	Encoding Error (xa) */
/* Synchronous Transmit queue */
					/* Bit  3:	reserved */
#define	IS_XS_B		(1L<<2)		/* Bit	2:	End of Buffer (xs) */
#define	IS_XS_F		(1L<<1)		/* Bit	1:	End of Frame (xs) */
#define	IS_XS_C		(1L<<0)		/* Bit	0:	Encoding Error (xs) */

/*
 * Define all valid interrupt source Bits from GET_ISR ()
 */
#define	ALL_IRSR	0x01ffff77L	/* (DV) */
#define	ALL_IRSR_ML	0x0ffff077L	/* (ML) */


/*	B0_IMSK		32 bit Interrupt mask register */
/*
 * The Bit definnition of this register are the same as of the interrupt
 * source register. These definition are directly derived from the Hardware
 * spec.
 */
					/* Bit 31..28:	reserved	     */
#define IRQ_I2C_READY	(1L<<27)	/* Bit 27: (ML)	IRQ on end of I2C tx */
#define IRQ_SW		(1L<<26)	/* Bit 26: (ML)	SW forced IRQ	     */
#define IRQ_EXT_REG	(1L<<25)	/* Bit 25: (ML) IRQ from external reg*/
#define	IRQ_STAT	(1L<<24)	/* Bit 24:	IRQ status exception */
					/*   PERR, RMABORT, RTABORT DATAPERR */
#define	IRQ_MST_ERR	(1L<<23)	/* Bit 23:	IRQ master error     */
					/*   RMABORT, RTABORT, DATAPERR	     */
#define	IRQ_TIMER	(1L<<22)	/* Bit 22:	IRQ_TIMER	*/
#define	IRQ_RTM		(1L<<21)	/* Bit 21:	IRQ_RTM		*/
#define	IRQ_DAS		(1L<<20)	/* Bit 20:	IRQ_PHY_DAS	*/
#define	IRQ_IFCP_4	(1L<<19)	/* Bit 19:	IRQ_IFCP_4	*/
#define	IRQ_IFCP_3	(1L<<18)	/* Bit 18:	IRQ_IFCP_3/IRQ_PHY */
#define	IRQ_IFCP_2	(1L<<17)	/* Bit 17:	IRQ_IFCP_2/IRQ_MAC_2 */
#define	IRQ_IFCP_1	(1L<<16)	/* Bit 16:	IRQ_IFCP_1/IRQ_MAC_1 */
/* Receive Queue 1 */
#define	IRQ_R1_P	(1L<<15)	/* Bit 15:	Parity Error (q1) */
#define	IRQ_R1_B	(1L<<14)	/* Bit 14:	End of Buffer (q1) */
#define	IRQ_R1_F	(1L<<13)	/* Bit 13:	End of Frame (q1) */
#define	IRQ_R1_C	(1L<<12)	/* Bit 12:	Encoding Error (q1) */
/* Receive Queue 2 */
#define	IRQ_R2_P	(1L<<11)	/* Bit 11: (DV)	Parity Error (q2) */
#define	IRQ_R2_B	(1L<<10)	/* Bit 10: (DV)	End of Buffer (q2) */
#define	IRQ_R2_F	(1L<<9)		/* Bit	9: (DV)	End of Frame (q2) */
#define	IRQ_R2_C	(1L<<8)		/* Bit	8: (DV)	Encoding Error (q2) */
/* Asynchronous Transmit queue */
					/* Bit  7:	reserved */
#define	IRQ_XA_B	(1L<<6)		/* Bit	6:	End of Buffer (xa) */
#define	IRQ_XA_F	(1L<<5)		/* Bit	5:	End of Frame (xa) */
#define	IRQ_XA_C	(1L<<4)		/* Bit	4:	Encoding Error (xa) */
/* Synchronous Transmit queue */
					/* Bit  3:	reserved */
#define	IRQ_XS_B	(1L<<2)		/* Bit	2:	End of Buffer (xs) */
#define	IRQ_XS_F	(1L<<1)		/* Bit	1:	End of Frame (xs) */
#define	IRQ_XS_C	(1L<<0)		/* Bit	0:	Encoding Error (xs) */

/* 0x0010 - 0x006b:	formac+ (supernet_3) fequently used registers */
/*	B0_R1_CSR	32 bit BMU control/status reg (rec q 1 ) */
/*	B0_R2_CSR	32 bit BMU control/status reg (rec q 2 ) */
/*	B0_XA_CSR	32 bit BMU control/status reg (a xmit q ) */
/*	B0_XS_CSR	32 bit BMU control/status reg (s xmit q ) */
/* The registers are the same as B4_R1_CSR, B4_R2_CSR, B5_Xa_CSR, B5_XS_CSR */

/*	B2_MAC_0	8 bit MAC address Byte 0 */
/*	B2_MAC_1	8 bit MAC address Byte 1 */
/*	B2_MAC_2	8 bit MAC address Byte 2 */
/*	B2_MAC_3	8 bit MAC address Byte 3 */
/*	B2_MAC_4	8 bit MAC address Byte 4 */
/*	B2_MAC_5	8 bit MAC address Byte 5 */
/*	B2_MAC_6	8 bit MAC address Byte 6 (== 0) (DV) */
/*	B2_MAC_7	8 bit MAC address Byte 7 (== 0) (DV) */

/*	B2_CONN_TYP	8 bit Connector type */
/*	B2_PMD_TYP	8 bit PMD type */
/*	Values of connector and PMD type comply to SysKonnect internal std */

/*	The EPROM register are currently of no use */
/*	B2_E_0		8 bit EPROM Byte 0 */
/*	B2_E_1		8 bit EPROM Byte 1 */
/*	B2_E_2		8 bit EPROM Byte 2 */
/*	B2_E_3		8 bit EPROM Byte 3 */

/*	B2_FAR		32 bit Flash-Prom Address Register/Counter */
#define	FAR_ADDR	0x1ffffL	/* Bit 16..0:	FPROM Address mask */

/*	B2_FDP		8 bit Flash-Prom Data Port */

/*	B2_LD_CRTL	8 bit loader control */
/*	Bits are currently reserved */

/*	B2_LD_TEST	8 bit loader test */
#define	LD_T_ON		(1<<3)	/* Bit 3:    Loader Testmode on */
#define	LD_T_OFF	(1<<2)	/* Bit 2:    Loader Testmode off */
#define	LD_T_STEP	(1<<1)	/* Bit 1:    Decrement FPROM addr. Counter */
#define	LD_START	(1<<0)	/* Bit 0:    Start loading FPROM */

/*	B2_TI_INI	32 bit Timer init value */
/*	B2_TI_VAL	32 bit Timer value */
/*	B2_TI_CRTL	8 bit Timer control */
/*	B2_TI_TEST	8 Bit Timer Test */
/*	B2_WDOG_INI	32 bit Watchdog init value */
/*	B2_WDOG_VAL	32 bit Watchdog value */
/*	B2_WDOG_CRTL	8 bit Watchdog control */
/*	B2_WDOG_TEST	8 Bit Watchdog Test */
/*	B2_RTM_INI	32 bit RTM init value */
/*	B2_RTM_VAL	32 bit RTM value */
/*	B2_RTM_CRTL	8 bit RTM control */
/*	B2_RTM_TEST	8 Bit RTM Test */
/*	B2_<TIM>_CRTL	8 bit <TIM> control */
/*	B2_IRQ_MOD_INI	32 bit IRQ Moderation Timer Init Reg.	(ML) */
/*	B2_IRQ_MOD_VAL	32 bit IRQ Moderation Timer Value	(ML) */
/*	B2_IRQ_MOD_CTRL	8 bit IRQ Moderation Timer Control	(ML) */
/*	B2_IRQ_MOD_TEST	8 bit IRQ Moderation Timer Test		(ML) */
#define GET_TOK_CT	(1<<4)	/* Bit 4: Get the Token Counter (RTM) */
#define TIM_RES_TOK	(1<<3)	/* Bit 3: RTM Status: 1 == restricted */
#define TIM_ALARM	(1<<3)	/* Bit 3: Timer Alarm (WDOG) */
#define TIM_START	(1<<2)	/* Bit 2: Start Timer (TI,WDOG,RTM,IRQ_MOD)*/
#define TIM_STOP	(1<<1)	/* Bit 1: Stop Timer (TI,WDOG,RTM,IRQ_MOD) */
#define TIM_CL_IRQ	(1<<0)	/* Bit 0: Clear Timer IRQ (TI,WDOG,RTM) */
/*	B2_<TIM>_TEST	8 Bit <TIM> Test */
#define	TIM_T_ON	(1<<2)	/* Bit 2: Test mode on (TI,WDOG,RTM,IRQ_MOD) */
#define	TIM_T_OFF	(1<<1)	/* Bit 1: Test mode off (TI,WDOG,RTM,IRQ_MOD) */
#define	TIM_T_STEP	(1<<0)	/* Bit 0: Test step (TI,WDOG,RTM,IRQ_MOD) */

/*	B2_TOK_COUNT	0x014c	(ML)	32 bit	Token Counter */
/*	B2_DESC_ADDR_H	0x0150	(ML)	32 bit	Desciptor Base Addr Reg High */
/*	B2_CTRL_2	0x0154	(ML)	 8 bit	Control Register 2 */
				/* Bit 7..5:	reserved		*/
#define CTRL_CL_I2C_IRQ (1<<4)	/* Bit 4:	Clear I2C IRQ		*/
#define CTRL_ST_SW_IRQ	(1<<3)	/* Bit 3:	Set IRQ SW Request	*/
#define CTRL_CL_SW_IRQ	(1<<2)	/* Bit 2:	Clear IRQ SW Request	*/
#define CTRL_STOP_DONE	(1<<1)	/* Bit 1:	Stop Master is finished */
#define	CTRL_STOP_MAST	(1<<0)	/* Bit 0:	Command Bit to stop the master*/

/*	B2_IFACE_REG	0x0155	(ML)	 8 bit	Interface Register */
				/* Bit 7..3:	reserved		*/
#define	IF_I2C_DATA_DIR	(1<<2)	/* Bit 2:	direction of IF_I2C_DATA*/
#define IF_I2C_DATA	(1<<1)	/* Bit 1:	I2C Data Port		*/
#define	IF_I2C_CLK	(1<<0)	/* Bit 0:	I2C Clock Port		*/

				/* 0x0156:		reserved */
/*	B2_TST_CTRL_2	0x0157	(ML)	 8 bit	Test Control Register 2 */
					/* Bit 7..4:	reserved */
					/* force the following error on */
					/* the next master read/write	*/
#define TST_FRC_DPERR_MR64	(1<<3)	/* Bit 3:	DataPERR RD 64	*/
#define TST_FRC_DPERR_MW64	(1<<2)	/* Bit 2:	DataPERR WR 64	*/
#define TST_FRC_APERR_1M64	(1<<1)	/* Bit 1:	AddrPERR on 1. phase */
#define TST_FRC_APERR_2M64	(1<<0)	/* Bit 0:	AddrPERR on 2. phase */

/*	B2_I2C_CTRL	0x0158	(ML)	32 bit	I2C Control Register	       */
#define	I2C_FLAG	(1L<<31)	/* Bit 31:	Start read/write if WR */
#define I2C_ADDR	(0x7fffL<<16)	/* Bit 30..16:	Addr to be read/written*/
#define	I2C_DEV_SEL	(0x7fL<<9)	/* Bit  9..15:	I2C Device Select      */
					/* Bit  5.. 8:	reserved	       */
#define I2C_BURST_LEN	(1L<<4)		/* Bit  4	Burst Len, 1/4 bytes   */
#define I2C_DEV_SIZE	(7L<<1)		/* Bit	1.. 3:	I2C Device Size	       */
#define I2C_025K_DEV	(0L<<1)		/*		0: 256 Bytes or smaller*/
#define I2C_05K_DEV	(1L<<1)		/* 		1: 512	Bytes	       */
#define	I2C_1K_DEV	(2L<<1)		/*		2: 1024 Bytes	       */
#define I2C_2K_DEV	(3L<<1)		/*		3: 2048	Bytes	       */
#define	I2C_4K_DEV	(4L<<1)		/*		4: 4096 Bytes	       */
#define	I2C_8K_DEV	(5L<<1)		/*		5: 8192 Bytes	       */
#define	I2C_16K_DEV	(6L<<1)		/*		6: 16384 Bytes	       */
#define	I2C_32K_DEV	(7L<<1)		/*		7: 32768 Bytes	       */
#define I2C_STOP_BIT	(1<<0)		/* Bit  0:	Interrupt I2C transfer */

/*
 * I2C Addresses
 *
 * The temperature sensor and the voltage sensor are on the same I2C bus.
 * Note: The voltage sensor (Micorwire) will be selected by PCI_EXT_PATCH_1
 *	 in PCI_OUR_REG 1.
 */
#define	I2C_ADDR_TEMP	0x90	/* I2C Address Temperature Sensor */

/*	B2_I2C_DATA	0x015c	(ML)	32 bit	I2C Data Register */

/*	B4_R1_D		4*32 bit current receive Descriptor	(q1) */
/*	B4_R1_DA	32 bit current rec desc address		(q1) */
/*	B4_R1_AC	32 bit current receive Address Count	(q1) */
/*	B4_R1_BC	32 bit current receive Byte Counter	(q1) */
/*	B4_R1_CSR	32 bit BMU Control/Status Register	(q1) */
/*	B4_R1_F		32 bit flag register			(q1) */
/*	B4_R1_T1	32 bit Test Register 1		 	(q1) */
/*	B4_R1_T2	32 bit Test Register 2		 	(q1) */
/*	B4_R1_T3	32 bit Test Register 3		 	(q1) */
/*	B4_R2_D		4*32 bit current receive Descriptor	(q2) */
/*	B4_R2_DA	32 bit current rec desc address		(q2) */
/*	B4_R2_AC	32 bit current receive Address Count	(q2) */
/*	B4_R2_BC	32 bit current receive Byte Counter	(q2) */
/*	B4_R2_CSR	32 bit BMU Control/Status Register	(q2) */
/*	B4_R2_F		32 bit flag register			(q2) */
/*	B4_R2_T1	32 bit Test Register 1			(q2) */
/*	B4_R2_T2	32 bit Test Register 2			(q2) */
/*	B4_R2_T3	32 bit Test Register 3			(q2) */
/*	B5_XA_D		4*32 bit current receive Descriptor	(xa) */
/*	B5_XA_DA	32 bit current rec desc address		(xa) */
/*	B5_XA_AC	32 bit current receive Address Count	(xa) */
/*	B5_XA_BC	32 bit current receive Byte Counter	(xa) */
/*	B5_XA_CSR	32 bit BMU Control/Status Register	(xa) */
/*	B5_XA_F		32 bit flag register			(xa) */
/*	B5_XA_T1	32 bit Test Register 1			(xa) */
/*	B5_XA_T2	32 bit Test Register 2			(xa) */
/*	B5_XA_T3	32 bit Test Register 3			(xa) */
/*	B5_XS_D		4*32 bit current receive Descriptor	(xs) */
/*	B5_XS_DA	32 bit current rec desc address		(xs) */
/*	B5_XS_AC	32 bit current receive Address Count	(xs) */
/*	B5_XS_BC	32 bit current receive Byte Counter	(xs) */
/*	B5_XS_CSR	32 bit BMU Control/Status Register	(xs) */
/*	B5_XS_F		32 bit flag register			(xs) */
/*	B5_XS_T1	32 bit Test Register 1			(xs) */
/*	B5_XS_T2	32 bit Test Register 2			(xs) */
/*	B5_XS_T3	32 bit Test Register 3			(xs) */
/*	B5_<xx>_CSR	32 bit BMU Control/Status Register	(xx) */
#define	CSR_DESC_CLEAR	(1L<<21)    /* Bit 21:	Clear Reset for Descr */
#define	CSR_DESC_SET	(1L<<20)    /* Bit 20:	Set Reset for Descr */
#define	CSR_FIFO_CLEAR	(1L<<19)    /* Bit 19:	Clear Reset for FIFO */
#define	CSR_FIFO_SET	(1L<<18)    /* Bit 18:	Set Reset for FIFO */
#define	CSR_HPI_RUN	(1L<<17)    /* Bit 17:	Release HPI SM */
#define	CSR_HPI_RST	(1L<<16)    /* Bit 16:	Reset HPI SM to Idle */
#define	CSR_SV_RUN	(1L<<15)    /* Bit 15:	Release Supervisor SM */
#define	CSR_SV_RST	(1L<<14)    /* Bit 14:	Reset Supervisor SM */
#define	CSR_DREAD_RUN	(1L<<13)    /* Bit 13:	Release Descr Read SM */
#define	CSR_DREAD_RST	(1L<<12)    /* Bit 12:	Reset Descr Read SM */
#define	CSR_DWRITE_RUN	(1L<<11)    /* Bit 11:	Rel. Descr Write SM */
#define	CSR_DWRITE_RST	(1L<<10)    /* Bit 10:	Reset Descr Write SM */
#define	CSR_TRANS_RUN	(1L<<9)     /* Bit 9:	Release Transfer SM */
#define	CSR_TRANS_RST	(1L<<8)     /* Bit 8:	Reset Transfer SM */
				    /* Bit 7..5: reserved */
#define	CSR_START	(1L<<4)     /* Bit 4:	Start Rec/Xmit Queue */
#define	CSR_IRQ_CL_P	(1L<<3)     /* Bit 3:	Clear Parity IRQ, Rcv */
#define	CSR_IRQ_CL_B	(1L<<2)     /* Bit 2:	Clear EOB IRQ */
#define	CSR_IRQ_CL_F	(1L<<1)     /* Bit 1:	Clear EOF IRQ */
#define	CSR_IRQ_CL_C	(1L<<0)     /* Bit 0:	Clear ERR IRQ */

#define CSR_SET_RESET	(CSR_DESC_SET|CSR_FIFO_SET|CSR_HPI_RST|CSR_SV_RST|\
			CSR_DREAD_RST|CSR_DWRITE_RST|CSR_TRANS_RST)
#define CSR_CLR_RESET	(CSR_DESC_CLEAR|CSR_FIFO_CLEAR|CSR_HPI_RUN|CSR_SV_RUN|\
			CSR_DREAD_RUN|CSR_DWRITE_RUN|CSR_TRANS_RUN)


/*	B5_<xx>_F	32 bit flag register		 (xx) */
					/* Bit 28..31:	reserved	      */
#define F_ALM_FULL	(1L<<27)	/* Bit 27: (ML)	FIFO almost full      */
#define F_FIFO_EOF	(1L<<26)	/* Bit 26: (ML)	Fag bit in FIFO       */
#define F_WM_REACHED	(1L<<25)	/* Bit 25: (ML)	Watermark reached     */
#define F_UP_DW_USED	(1L<<24)	/* Bit 24: (ML) Upper Dword used (bug)*/
					/* Bit 23: 	reserved	      */
#define F_FIFO_LEVEL	(0x1fL<<16)	/* Bit 16..22:(ML) # of Qwords in FIFO*/
					/* Bit  8..15: 	reserved	      */
#define F_ML_WATER_M	0x0000ffL	/* Bit  0.. 7:(ML) Watermark	      */
#define	FLAG_WATER	0x00001fL	/* Bit 4..0:(DV) Level of req data tr.*/

/*	B5_<xx>_T1	32 bit Test Register 1		 (xx) */
/*		Holds four State Machine control Bytes */
#define	SM_CRTL_SV	(0xffL<<24) /* Bit 31..24:  Control Supervisor SM */
#define	SM_CRTL_RD	(0xffL<<16) /* Bit 23..16:  Control Read Desc SM */
#define	SM_CRTL_WR	(0xffL<<8)  /* Bit 15..8:   Control Write Desc SM */
#define	SM_CRTL_TR	(0xffL<<0)  /* Bit 7..0:    Control Transfer SM */

/*	B4_<xx>_T1_TR	8 bit Test Register 1 TR		(xx) */
/*	B4_<xx>_T1_WR	8 bit Test Register 1 WR		(xx) */
/*	B4_<xx>_T1_RD	8 bit Test Register 1 RD		(xx) */
/*	B4_<xx>_T1_SV	8 bit Test Register 1 SV		(xx) */
/* The control status byte of each machine looks like ... */
#define	SM_STATE	0xf0	/* Bit 7..4:	State which shall be loaded */
#define	SM_LOAD		0x08	/* Bit 3:	Load the SM with SM_STATE */
#define	SM_TEST_ON	0x04	/* Bit 2:	Switch on SM Test Mode */
#define	SM_TEST_OFF	0x02	/* Bit 1:	Go off the Test Mode */
#define	SM_STEP		0x01	/* Bit 0:	Step the State Machine */

/* The coding of the states */
#define	SM_SV_IDLE	0x0	/* Supervisor	Idle		Tr/Re	     */
#define	SM_SV_RES_START	0x1	/* Supervisor	Res_Start	Tr/Re	     */
#define	SM_SV_GET_DESC	0x3	/* Supervisor	Get_Desc	Tr/Re	     */
#define	SM_SV_CHECK	0x2	/* Supervisor	Check		Tr/Re	     */
#define	SM_SV_MOV_DATA	0x6	/* Supervisor	Move_Data	Tr/Re	     */
#define	SM_SV_PUT_DESC	0x7	/* Supervisor	Put_Desc	Tr/Re	     */
#define	SM_SV_SET_IRQ	0x5	/* Supervisor	Set_Irq		Tr/Re	     */

#define	SM_RD_IDLE	0x0	/* Read Desc.	Idle		Tr/Re	     */
#define	SM_RD_LOAD	0x1	/* Read Desc.	Load		Tr/Re	     */
#define	SM_RD_WAIT_TC	0x3	/* Read Desc.	Wait_TC		Tr/Re	     */
#define	SM_RD_RST_EOF	0x6	/* Read Desc.	Reset_EOF	   Re	     */
#define	SM_RD_WDONE_R	0x2	/* Read Desc.	Wait_Done	   Re	     */
#define	SM_RD_WDONE_T	0x4	/* Read Desc.	Wait_Done	Tr   	     */

#define	SM_TR_IDLE	0x0	/* Trans. Data	Idle		Tr/Re	     */
#define	SM_TR_LOAD	0x3	/* Trans. Data	Load		Tr/Re	     */
#define	SM_TR_LOAD_R_ML	0x1	/* Trans. Data	Load		  /Re	(ML) */
#define	SM_TR_WAIT_TC	0x2	/* Trans. Data	Wait_TC		Tr/Re	     */
#define	SM_TR_WDONE	0x4	/* Trans. Data	Wait_Done	Tr/Re	     */

#define	SM_WR_IDLE	0x0	/* Write Desc.	Idle		Tr/Re	     */
#define	SM_WR_ABLEN	0x1	/* Write Desc.	Act_Buf_Length	Tr/Re	     */
#define	SM_WR_LD_A4	0x2	/* Write Desc.	Load_A4		   Re	     */
#define	SM_WR_RES_OWN	0x2	/* Write Desc.	Res_OWN		Tr   	     */
#define	SM_WR_WAIT_EOF	0x3	/* Write Desc.	Wait_EOF	   Re	     */
#define	SM_WR_LD_N2C_R	0x4	/* Write Desc.	Load_N2C	   Re	     */
#define	SM_WR_WAIT_TC_R	0x5	/* Write Desc.	Wait_TC		   Re	     */
#define	SM_WR_WAIT_TC4	0x6	/* Write Desc.	Wait_TC4	   Re	     */
#define	SM_WR_LD_A_T	0x6	/* Write Desc.	Load_A		Tr   	     */
#define	SM_WR_LD_A_R	0x7	/* Write Desc.	Load_A		   Re	     */
#define	SM_WR_WAIT_TC_T	0x7	/* Write Desc.	Wait_TC		Tr   	     */
#define	SM_WR_LD_N2C_T	0xc	/* Write Desc.	Load_N2C	Tr   	     */
#define	SM_WR_WDONE_T	0x9	/* Write Desc.	Wait_Done	Tr   	     */
#define	SM_WR_WDONE_R	0xc	/* Write Desc.	Wait_Done	   Re	     */
#define SM_WR_LD_D_AD	0xe	/* Write Desc.  Load_Dumr_A	   Re	(ML) */
#define SM_WR_WAIT_D_TC	0xf	/* Write Desc.	Wait_Dumr_TC	   Re	(ML) */

/*	B5_<xx>_T2	32 bit Test Register 2		 (xx) */
/* Note: This register is only defined for the transmit queues */
				/* Bit 31..8:	reserved */
#define	AC_TEST_ON	(1<<7)	/* Bit 7:	Address Counter Test Mode on */
#define	AC_TEST_OFF	(1<<6)	/* Bit 6:	Address Counter Test Mode off*/
#define	BC_TEST_ON	(1<<5)	/* Bit 5:	Byte Counter Test Mode on */
#define	BC_TEST_OFF	(1<<4)	/* Bit 4:	Byte Counter Test Mode off */
#define	TEST_STEP04	(1<<3)	/* Bit 3:	Inc AC/Dec BC by 4 */
#define	TEST_STEP03	(1<<2)	/* Bit 2:	Inc AC/Dec BC by 3 */
#define	TEST_STEP02	(1<<1)	/* Bit 1:	Inc AC/Dec BC by 2 */
#define	TEST_STEP01	(1<<0)	/* Bit 0:	Inc AC/Dec BC by 1 */

/*	B5_<xx>_T3	32 bit Test Register 3		 (xx) */
/* Note: This register is only defined for the transmit queues */
				/* Bit 31..8:	reserved */
#define T3_MUX_2	(1<<7)	/* Bit 7: (ML)	Mux position MSB */
#define T3_VRAM_2	(1<<6)	/* Bit 6: (ML)	Virtual RAM buffer addr MSB */
#define	T3_LOOP		(1<<5)	/* Bit 5: 	Set Loopback (Xmit) */
#define	T3_UNLOOP	(1<<4)	/* Bit 4: 	Unset Loopback (Xmit) */
#define	T3_MUX		(3<<2)	/* Bit 3..2:	Mux position */
#define	T3_VRAM		(3<<0)	/* Bit 1..0:	Virtual RAM buffer Address */


/*
 * address transmission from logical to physical offset address on board
 */
#define	FMA(a)	(0x0400|((a)<<2))	/* FORMAC+ (r/w) (SN3) */
#define	P1(a)	(0x0380|((a)<<2))	/* PLC1 (r/w) (DAS) */
#define	P2(a)	(0x0600|((a)<<2))	/* PLC2 (r/w) (covered by the SN3) */
#define PRA(a)	(B2_MAC_0 + (a))	/* configuration PROM (MAC address) */

/*
 * FlashProm specification
 */
#define	MAX_PAGES	0x20000L	/* Every byte has a single page */
#define	MAX_FADDR	1		/* 1 byte per page */

/*
 * Receive / Transmit Buffer Control word
 */
#define	BMU_OWN		(1UL<<31)	/* OWN bit: 0 == host, 1 == adapter */
#define	BMU_STF		(1L<<30)	/* Start of Frame ?		*/
#define	BMU_EOF		(1L<<29)	/* End of Frame ?		*/
#define	BMU_EN_IRQ_EOB	(1L<<28)	/* Enable "End of Buffer" IRQ	*/
#define	BMU_EN_IRQ_EOF	(1L<<27)	/* Enable "End of Frame" IRQ	*/
#define	BMU_DEV_0	(1L<<26)	/* RX: don't transfer to system mem */
#define BMU_SMT_TX	(1L<<25)	/* TX: if set, buffer type SMT_MBuf */
#define BMU_ST_BUF	(1L<<25)	/* RX: copy of start of frame */
#define BMU_UNUSED	(1L<<24)	/* Set if the Descr is curr unused */
#define BMU_SW		(3L<<24)	/* 2 Bits reserved for SW usage */
#define	BMU_CHECK	0x00550000L	/* To identify the control word */
#define	BMU_BBC		0x0000FFFFL	/* R/T Buffer Byte Count        */

/*
 * physical address offset + IO-Port base address
 */
#ifdef MEM_MAPPED_IO
#define	ADDR(a)		(char far *) smc->hw.iop+(a)
#define	ADDRS(smc,a)	(char far *) (smc)->hw.iop+(a)
#else
#define	ADDR(a)	(((a)>>7) ? (outp(smc->hw.iop+B0_RAP,(a)>>7), \
	(smc->hw.iop+(((a)&0x7F)|((a)>>7 ? 0x80:0)))) : \
	(smc->hw.iop+(((a)&0x7F)|((a)>>7 ? 0x80:0))))
#define	ADDRS(smc,a) (((a)>>7) ? (outp((smc)->hw.iop+B0_RAP,(a)>>7), \
	((smc)->hw.iop+(((a)&0x7F)|((a)>>7 ? 0x80:0)))) : \
	((smc)->hw.iop+(((a)&0x7F)|((a)>>7 ? 0x80:0))))
#endif

/*
 * Define a macro to access the configuration space
 */
#define PCI_C(a)	ADDR(B3_CFG_SPC + (a))	/* PCI Config Space */

#define EXT_R(a)	ADDR(B6_EXT_REG + (a))	/* External Registers */

/*
 * Define some values needed for the MAC address (PROM)
 */
#define	SA_MAC		(0)	/* start addr. MAC_AD within the PROM */
#define	PRA_OFF		(0)	/* offset correction when 4th byte reading */

#define	SKFDDI_PSZ	8	/* address PROM size */

#define	FM_A(a)	ADDR(FMA(a))	/* FORMAC Plus physical addr */
#define	P1_A(a)	ADDR(P1(a))	/* PLC1 (r/w) */
#define	P2_A(a)	ADDR(P2(a))	/* PLC2 (r/w) (DAS) */
#define PR_A(a)	ADDR(PRA(a))	/* config. PROM (MAC address) */

/*
 * Macro to read the PROM
 */
#define	READ_PROM(a)	((u_char)inp(a))

#define	GET_PAGE(bank)	outpd(ADDR(B2_FAR),bank)
#define	VPP_ON()
#define	VPP_OFF()

/*
 * Note: Values of the Interrupt Source Register are defined above
 */
#define ISR_A		ADDR(B0_ISRC)
#define	GET_ISR()		inpd(ISR_A)
#define GET_ISR_SMP(iop)	inpd((iop)+B0_ISRC)
#define	CHECK_ISR()		(inpd(ISR_A) & inpd(ADDR(B0_IMSK)))
#define CHECK_ISR_SMP(iop)	(inpd((iop)+B0_ISRC) & inpd((iop)+B0_IMSK))

#define	BUS_CHECK()

/*
 * CLI_FBI:	Disable Board Interrupts
 * STI_FBI:	Enable Board Interrupts
 */
#ifndef UNIX
#define	CLI_FBI()	outpd(ADDR(B0_IMSK),0)
#else
#define	CLI_FBI(smc)	outpd(ADDRS((smc),B0_IMSK),0)
#endif

#ifndef UNIX
#define	STI_FBI()	outpd(ADDR(B0_IMSK),smc->hw.is_imask)
#else
#define	STI_FBI(smc)	outpd(ADDRS((smc),B0_IMSK),(smc)->hw.is_imask)
#endif

#define CLI_FBI_SMP(iop)	outpd((iop)+B0_IMSK,0)
#define	STI_FBI_SMP(smc,iop)	outpd((iop)+B0_IMSK,(smc)->hw.is_imask)

#endif	/* PCI */
/*--------------------------------------------------------------------------*/

/*
 * 12 bit transfer (dword) counter:
 *	(ISA:	2*trc = number of byte)
 *	(EISA:	4*trc = number of byte)
 *	(MCA:	4*trc = number of byte)
 */
#define	MAX_TRANS	(0x0fff)

/*
 * PC PIC
 */
#define	MST_8259 (0x20)
#define	SLV_8259 (0xA0)

#define TPS		(18)		/* ticks per second */

/*
 * error timer defs
 */
#define	TN		(4)	/* number of supported timer = TN+1 */
#define	SNPPND_TIME	(5)	/* buffer memory access over mem. data reg. */

#define	MAC_AD	0x405a0000

#define MODR1	FM_A(FM_MDREG1)	/* mode register 1 */
#define MODR2	FM_A(FM_MDREG2)	/* mode register 2 */

#define CMDR1	FM_A(FM_CMDREG1)	/* command register 1 */
#define CMDR2	FM_A(FM_CMDREG2)	/* command register 2 */


/*
 * function defines
 */
#define	CLEAR(io,mask)		outpw((io),inpw(io)&(~(mask)))
#define	SET(io,mask)		outpw((io),inpw(io)|(mask))
#define	GET(io,mask)		(inpw(io)&(mask))
#define	SETMASK(io,val,mask)	outpw((io),(inpw(io) & ~(mask)) | (val))

/*
 * PHY Port A (PA) = PLC 1
 * With SuperNet 3 PHY-A and PHY S are identical.
 */
#define	PLC(np,reg)	(((np) == PA) ? P2_A(reg) : P1_A(reg))

/*
 * set memory address register for write and read
 */
#define	MARW(ma)	outpw(FM_A(FM_MARW),(unsigned int)(ma))
#define	MARR(ma)	outpw(FM_A(FM_MARR),(unsigned int)(ma))

/*
 * read/write from/to memory data register
 */
/* write double word */
#define	MDRW(dd)	outpw(FM_A(FM_MDRU),(unsigned int)((dd)>>16)) ;\
			outpw(FM_A(FM_MDRL),(unsigned int)(dd))

#ifndef WINNT
/* read double word */
#define	MDRR()		(((long)inpw(FM_A(FM_MDRU))<<16) + inpw(FM_A(FM_MDRL)))

/* read FORMAC+ 32-bit status register */
#define	GET_ST1()	(((long)inpw(FM_A(FM_ST1U))<<16) + inpw(FM_A(FM_ST1L)))
#define	GET_ST2()	(((long)inpw(FM_A(FM_ST2U))<<16) + inpw(FM_A(FM_ST2L)))
#ifdef	SUPERNET_3
#define	GET_ST3()	(((long)inpw(FM_A(FM_ST3U))<<16) + inpw(FM_A(FM_ST3L)))
#endif
#else
/* read double word */
#define MDRR()		inp2w((FM_A(FM_MDRU)),(FM_A(FM_MDRL)))

/* read FORMAC+ 32-bit status register */
#define GET_ST1()	inp2w((FM_A(FM_ST1U)),(FM_A(FM_ST1L)))
#define GET_ST2()	inp2w((FM_A(FM_ST2U)),(FM_A(FM_ST2L)))
#ifdef	SUPERNET_3
#define GET_ST3()	inp2w((FM_A(FM_ST3U)),(FM_A(FM_ST3L)))
#endif
#endif

/* Special timer macro for 82c54 */
				/* timer access over data bus bit 8..15 */
#define	OUT_82c54_TIMER(port,val)	outpw(TI_A(port),(val)<<8)
#define	IN_82c54_TIMER(port)		((inpw(TI_A(port))>>8) & 0xff)


#ifdef	DEBUG
#define	DB_MAC(mac,st) {if (debug_mac & 0x1)\
				printf("M") ;\
			if (debug_mac & 0x2)\
				printf("\tMAC %d status 0x%08lx\n",mac,st) ;\
			if (debug_mac & 0x4)\
				dp_mac(mac,st) ;\
}

#define	DB_PLC(p,iev) {	if (debug_plc & 0x1)\
				printf("P") ;\
			if (debug_plc & 0x2)\
				printf("\tPLC %s Int 0x%04x\n", \
					(p == PA) ? "A" : "B", iev) ;\
			if (debug_plc & 0x4)\
				dp_plc(p,iev) ;\
}

#define	DB_TIMER() {	if (debug_timer & 0x1)\
				printf("T") ;\
			if (debug_timer & 0x2)\
				printf("\tTimer ISR\n") ;\
}

#else	/* no DEBUG */

#define	DB_MAC(mac,st)
#define	DB_PLC(p,iev)
#define	DB_TIMER()

#endif	/* no DEBUG */

#define	INC_PTR(sp,cp,ep)	if (++cp == ep) cp = sp
/*
 * timer defs
 */
#define	COUNT(t)	((t)<<6)	/* counter */
#define	RW_OP(o)	((o)<<4)	/* read/write operation */
#define	TMODE(m)	((m)<<1)	/* timer mode */

#endif
