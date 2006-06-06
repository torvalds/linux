/*
 * Definitions for the new Marvell Yukon / SysKonenct driver.
 */
#ifndef _SKGE_H
#define _SKGE_H

/* PCI config registers */
#define PCI_DEV_REG1	0x40
#define  PCI_PHY_COMA	0x8000000
#define  PCI_VIO	0x2000000
#define PCI_DEV_REG2	0x44
#define  PCI_REV_DESC	 0x4

#define PCI_STATUS_ERROR_BITS (PCI_STATUS_DETECTED_PARITY | \
			       PCI_STATUS_SIG_SYSTEM_ERROR | \
			       PCI_STATUS_REC_MASTER_ABORT | \
			       PCI_STATUS_REC_TARGET_ABORT | \
			       PCI_STATUS_PARITY)

enum csr_regs {
	B0_RAP	= 0x0000,
	B0_CTST	= 0x0004,
	B0_LED	= 0x0006,
	B0_POWER_CTRL	= 0x0007,
	B0_ISRC	= 0x0008,
	B0_IMSK	= 0x000c,
	B0_HWE_ISRC	= 0x0010,
	B0_HWE_IMSK	= 0x0014,
	B0_SP_ISRC	= 0x0018,
	B0_XM1_IMSK	= 0x0020,
	B0_XM1_ISRC	= 0x0028,
	B0_XM1_PHY_ADDR	= 0x0030,
	B0_XM1_PHY_DATA	= 0x0034,
	B0_XM2_IMSK	= 0x0040,
	B0_XM2_ISRC	= 0x0048,
	B0_XM2_PHY_ADDR	= 0x0050,
	B0_XM2_PHY_DATA	= 0x0054,
	B0_R1_CSR	= 0x0060,
	B0_R2_CSR	= 0x0064,
	B0_XS1_CSR	= 0x0068,
	B0_XA1_CSR	= 0x006c,
	B0_XS2_CSR	= 0x0070,
	B0_XA2_CSR	= 0x0074,

	B2_MAC_1	= 0x0100,
	B2_MAC_2	= 0x0108,
	B2_MAC_3	= 0x0110,
	B2_CONN_TYP	= 0x0118,
	B2_PMD_TYP	= 0x0119,
	B2_MAC_CFG	= 0x011a,
	B2_CHIP_ID	= 0x011b,
	B2_E_0		= 0x011c,
	B2_E_1		= 0x011d,
	B2_E_2		= 0x011e,
	B2_E_3		= 0x011f,
	B2_FAR		= 0x0120,
	B2_FDP		= 0x0124,
	B2_LD_CTRL	= 0x0128,
	B2_LD_TEST	= 0x0129,
	B2_TI_INI	= 0x0130,
	B2_TI_VAL	= 0x0134,
	B2_TI_CTRL	= 0x0138,
	B2_TI_TEST	= 0x0139,
	B2_IRQM_INI	= 0x0140,
	B2_IRQM_VAL	= 0x0144,
	B2_IRQM_CTRL	= 0x0148,
	B2_IRQM_TEST	= 0x0149,
	B2_IRQM_MSK	= 0x014c,
	B2_IRQM_HWE_MSK	= 0x0150,
	B2_TST_CTRL1	= 0x0158,
	B2_TST_CTRL2	= 0x0159,
	B2_GP_IO	= 0x015c,
	B2_I2C_CTRL	= 0x0160,
	B2_I2C_DATA	= 0x0164,
	B2_I2C_IRQ	= 0x0168,
	B2_I2C_SW	= 0x016c,
	B2_BSC_INI	= 0x0170,
	B2_BSC_VAL	= 0x0174,
	B2_BSC_CTRL	= 0x0178,
	B2_BSC_STAT	= 0x0179,
	B2_BSC_TST	= 0x017a,

	B3_RAM_ADDR	= 0x0180,
	B3_RAM_DATA_LO	= 0x0184,
	B3_RAM_DATA_HI	= 0x0188,
	B3_RI_WTO_R1	= 0x0190,
	B3_RI_WTO_XA1	= 0x0191,
	B3_RI_WTO_XS1	= 0x0192,
	B3_RI_RTO_R1	= 0x0193,
	B3_RI_RTO_XA1	= 0x0194,
	B3_RI_RTO_XS1	= 0x0195,
	B3_RI_WTO_R2	= 0x0196,
	B3_RI_WTO_XA2	= 0x0197,
	B3_RI_WTO_XS2	= 0x0198,
	B3_RI_RTO_R2	= 0x0199,
	B3_RI_RTO_XA2	= 0x019a,
	B3_RI_RTO_XS2	= 0x019b,
	B3_RI_TO_VAL	= 0x019c,
	B3_RI_CTRL	= 0x01a0,
	B3_RI_TEST	= 0x01a2,
	B3_MA_TOINI_RX1	= 0x01b0,
	B3_MA_TOINI_RX2	= 0x01b1,
	B3_MA_TOINI_TX1	= 0x01b2,
	B3_MA_TOINI_TX2	= 0x01b3,
	B3_MA_TOVAL_RX1	= 0x01b4,
	B3_MA_TOVAL_RX2	= 0x01b5,
	B3_MA_TOVAL_TX1	= 0x01b6,
	B3_MA_TOVAL_TX2	= 0x01b7,
	B3_MA_TO_CTRL	= 0x01b8,
	B3_MA_TO_TEST	= 0x01ba,
	B3_MA_RCINI_RX1	= 0x01c0,
	B3_MA_RCINI_RX2	= 0x01c1,
	B3_MA_RCINI_TX1	= 0x01c2,
	B3_MA_RCINI_TX2	= 0x01c3,
	B3_MA_RCVAL_RX1	= 0x01c4,
	B3_MA_RCVAL_RX2	= 0x01c5,
	B3_MA_RCVAL_TX1	= 0x01c6,
	B3_MA_RCVAL_TX2	= 0x01c7,
	B3_MA_RC_CTRL	= 0x01c8,
	B3_MA_RC_TEST	= 0x01ca,
	B3_PA_TOINI_RX1	= 0x01d0,
	B3_PA_TOINI_RX2	= 0x01d4,
	B3_PA_TOINI_TX1	= 0x01d8,
	B3_PA_TOINI_TX2	= 0x01dc,
	B3_PA_TOVAL_RX1	= 0x01e0,
	B3_PA_TOVAL_RX2	= 0x01e4,
	B3_PA_TOVAL_TX1	= 0x01e8,
	B3_PA_TOVAL_TX2	= 0x01ec,
	B3_PA_CTRL	= 0x01f0,
	B3_PA_TEST	= 0x01f2,
};

/*	B0_CTST			16 bit	Control/Status register */
enum {
	CS_CLK_RUN_HOT	= 1<<13,/* CLK_RUN hot m. (YUKON-Lite only) */
	CS_CLK_RUN_RST	= 1<<12,/* CLK_RUN reset  (YUKON-Lite only) */
	CS_CLK_RUN_ENA	= 1<<11,/* CLK_RUN enable (YUKON-Lite only) */
	CS_VAUX_AVAIL	= 1<<10,/* VAUX available (YUKON only) */
	CS_BUS_CLOCK	= 1<<9,	/* Bus Clock 0/1 = 33/66 MHz */
	CS_BUS_SLOT_SZ	= 1<<8,	/* Slot Size 0/1 = 32/64 bit slot */
	CS_ST_SW_IRQ	= 1<<7,	/* Set IRQ SW Request */
	CS_CL_SW_IRQ	= 1<<6,	/* Clear IRQ SW Request */
	CS_STOP_DONE	= 1<<5,	/* Stop Master is finished */
	CS_STOP_MAST	= 1<<4,	/* Command Bit to stop the master */
	CS_MRST_CLR	= 1<<3,	/* Clear Master reset	*/
	CS_MRST_SET	= 1<<2,	/* Set Master reset	*/
	CS_RST_CLR	= 1<<1,	/* Clear Software reset	*/
	CS_RST_SET	= 1,	/* Set   Software reset	*/

/*	B0_LED			 8 Bit	LED register */
/* Bit  7.. 2:	reserved */
	LED_STAT_ON	= 1<<1,	/* Status LED on	*/
	LED_STAT_OFF	= 1,		/* Status LED off	*/

/*	B0_POWER_CTRL	 8 Bit	Power Control reg (YUKON only) */
	PC_VAUX_ENA	= 1<<7,	/* Switch VAUX Enable  */
	PC_VAUX_DIS	= 1<<6,	/* Switch VAUX Disable */
	PC_VCC_ENA	= 1<<5,	/* Switch VCC Enable  */
	PC_VCC_DIS	= 1<<4,	/* Switch VCC Disable */
	PC_VAUX_ON	= 1<<3,	/* Switch VAUX On  */
	PC_VAUX_OFF	= 1<<2,	/* Switch VAUX Off */
	PC_VCC_ON	= 1<<1,	/* Switch VCC On  */
	PC_VCC_OFF	= 1<<0,	/* Switch VCC Off */
};

/*	B2_IRQM_MSK 	32 bit	IRQ Moderation Mask */
enum {
	IS_ALL_MSK	= 0xbffffffful,	/* All Interrupt bits */
	IS_HW_ERR	= 1<<31,	/* Interrupt HW Error */
					/* Bit 30:	reserved */
	IS_PA_TO_RX1	= 1<<29,	/* Packet Arb Timeout Rx1 */
	IS_PA_TO_RX2	= 1<<28,	/* Packet Arb Timeout Rx2 */
	IS_PA_TO_TX1	= 1<<27,	/* Packet Arb Timeout Tx1 */
	IS_PA_TO_TX2	= 1<<26,	/* Packet Arb Timeout Tx2 */
	IS_I2C_READY	= 1<<25,	/* IRQ on end of I2C Tx */
	IS_IRQ_SW	= 1<<24,	/* SW forced IRQ	*/
	IS_EXT_REG	= 1<<23,	/* IRQ from LM80 or PHY (GENESIS only) */
					/* IRQ from PHY (YUKON only) */
	IS_TIMINT	= 1<<22,	/* IRQ from Timer	*/
	IS_MAC1		= 1<<21,	/* IRQ from MAC 1	*/
	IS_LNK_SYNC_M1	= 1<<20,	/* Link Sync Cnt wrap MAC 1 */
	IS_MAC2		= 1<<19,	/* IRQ from MAC 2	*/
	IS_LNK_SYNC_M2	= 1<<18,	/* Link Sync Cnt wrap MAC 2 */
/* Receive Queue 1 */
	IS_R1_B		= 1<<17,	/* Q_R1 End of Buffer */
	IS_R1_F		= 1<<16,	/* Q_R1 End of Frame */
	IS_R1_C		= 1<<15,	/* Q_R1 Encoding Error */
/* Receive Queue 2 */
	IS_R2_B		= 1<<14,	/* Q_R2 End of Buffer */
	IS_R2_F		= 1<<13,	/* Q_R2 End of Frame */
	IS_R2_C		= 1<<12,	/* Q_R2 Encoding Error */
/* Synchronous Transmit Queue 1 */
	IS_XS1_B	= 1<<11,	/* Q_XS1 End of Buffer */
	IS_XS1_F	= 1<<10,	/* Q_XS1 End of Frame */
	IS_XS1_C	= 1<<9,		/* Q_XS1 Encoding Error */
/* Asynchronous Transmit Queue 1 */
	IS_XA1_B	= 1<<8,		/* Q_XA1 End of Buffer */
	IS_XA1_F	= 1<<7,		/* Q_XA1 End of Frame */
	IS_XA1_C	= 1<<6,		/* Q_XA1 Encoding Error */
/* Synchronous Transmit Queue 2 */
	IS_XS2_B	= 1<<5,		/* Q_XS2 End of Buffer */
	IS_XS2_F	= 1<<4,		/* Q_XS2 End of Frame */
	IS_XS2_C	= 1<<3,		/* Q_XS2 Encoding Error */
/* Asynchronous Transmit Queue 2 */
	IS_XA2_B	= 1<<2,		/* Q_XA2 End of Buffer */
	IS_XA2_F	= 1<<1,		/* Q_XA2 End of Frame */
	IS_XA2_C	= 1<<0,		/* Q_XA2 Encoding Error */

	IS_TO_PORT1	= IS_PA_TO_RX1 | IS_PA_TO_TX1,
	IS_TO_PORT2	= IS_PA_TO_RX2 | IS_PA_TO_TX2,

	IS_PORT_1	= IS_XA1_F| IS_R1_F | IS_TO_PORT1 | IS_MAC1,
	IS_PORT_2	= IS_XA2_F| IS_R2_F | IS_TO_PORT2 | IS_MAC2,
};


/*	B2_IRQM_HWE_MSK	32 bit	IRQ Moderation HW Error Mask */
enum {
	IS_IRQ_TIST_OV	= 1<<13, /* Time Stamp Timer Overflow (YUKON only) */
	IS_IRQ_SENSOR	= 1<<12, /* IRQ from Sensor (YUKON only) */
	IS_IRQ_MST_ERR	= 1<<11, /* IRQ master error detected */
	IS_IRQ_STAT	= 1<<10, /* IRQ status exception */
	IS_NO_STAT_M1	= 1<<9,	/* No Rx Status from MAC 1 */
	IS_NO_STAT_M2	= 1<<8,	/* No Rx Status from MAC 2 */
	IS_NO_TIST_M1	= 1<<7,	/* No Time Stamp from MAC 1 */
	IS_NO_TIST_M2	= 1<<6,	/* No Time Stamp from MAC 2 */
	IS_RAM_RD_PAR	= 1<<5,	/* RAM Read  Parity Error */
	IS_RAM_WR_PAR	= 1<<4,	/* RAM Write Parity Error */
	IS_M1_PAR_ERR	= 1<<3,	/* MAC 1 Parity Error */
	IS_M2_PAR_ERR	= 1<<2,	/* MAC 2 Parity Error */
	IS_R1_PAR_ERR	= 1<<1,	/* Queue R1 Parity Error */
	IS_R2_PAR_ERR	= 1<<0,	/* Queue R2 Parity Error */

	IS_ERR_MSK	= IS_IRQ_MST_ERR | IS_IRQ_STAT
			| IS_NO_STAT_M1 | IS_NO_STAT_M2
			| IS_RAM_RD_PAR | IS_RAM_WR_PAR
			| IS_M1_PAR_ERR | IS_M2_PAR_ERR
			| IS_R1_PAR_ERR | IS_R2_PAR_ERR,
};

/*	B2_TST_CTRL1	 8 bit	Test Control Register 1 */
enum {
	TST_FRC_DPERR_MR = 1<<7, /* force DATAPERR on MST RD */
	TST_FRC_DPERR_MW = 1<<6, /* force DATAPERR on MST WR */
	TST_FRC_DPERR_TR = 1<<5, /* force DATAPERR on TRG RD */
	TST_FRC_DPERR_TW = 1<<4, /* force DATAPERR on TRG WR */
	TST_FRC_APERR_M	 = 1<<3, /* force ADDRPERR on MST */
	TST_FRC_APERR_T	 = 1<<2, /* force ADDRPERR on TRG */
	TST_CFG_WRITE_ON = 1<<1, /* Enable  Config Reg WR */
	TST_CFG_WRITE_OFF= 1<<0, /* Disable Config Reg WR */
};

/*	B2_MAC_CFG		 8 bit	MAC Configuration / Chip Revision */
enum {
	CFG_CHIP_R_MSK	  = 0xf<<4,	/* Bit 7.. 4: Chip Revision */
					/* Bit 3.. 2:	reserved */
	CFG_DIS_M2_CLK	  = 1<<1,	/* Disable Clock for 2nd MAC */
	CFG_SNG_MAC	  = 1<<0,	/* MAC Config: 0=2 MACs / 1=1 MAC*/
};

/*	B2_CHIP_ID		 8 bit 	Chip Identification Number */
enum {
	CHIP_ID_GENESIS	   = 0x0a, /* Chip ID for GENESIS */
	CHIP_ID_YUKON	   = 0xb0, /* Chip ID for YUKON */
	CHIP_ID_YUKON_LITE = 0xb1, /* Chip ID for YUKON-Lite (Rev. A1-A3) */
	CHIP_ID_YUKON_LP   = 0xb2, /* Chip ID for YUKON-LP */
	CHIP_ID_YUKON_XL   = 0xb3, /* Chip ID for YUKON-2 XL */
	CHIP_ID_YUKON_EC   = 0xb6, /* Chip ID for YUKON-2 EC */
 	CHIP_ID_YUKON_FE   = 0xb7, /* Chip ID for YUKON-2 FE */

	CHIP_REV_YU_LITE_A1  = 3,	/* Chip Rev. for YUKON-Lite A1,A2 */
	CHIP_REV_YU_LITE_A3  = 7,	/* Chip Rev. for YUKON-Lite A3 */
};

/*	B2_TI_CTRL		 8 bit	Timer control */
/*	B2_IRQM_CTRL	 8 bit	IRQ Moderation Timer Control */
enum {
	TIM_START	= 1<<2,	/* Start Timer */
	TIM_STOP	= 1<<1,	/* Stop  Timer */
	TIM_CLR_IRQ	= 1<<0,	/* Clear Timer IRQ (!IRQM) */
};

/*	B2_TI_TEST		 8 Bit	Timer Test */
/*	B2_IRQM_TEST	 8 bit	IRQ Moderation Timer Test */
/*	B28_DPT_TST		 8 bit	Descriptor Poll Timer Test Reg */
enum {
	TIM_T_ON	= 1<<2,	/* Test mode on */
	TIM_T_OFF	= 1<<1,	/* Test mode off */
	TIM_T_STEP	= 1<<0,	/* Test step */
};

/*	B2_GP_IO		32 bit	General Purpose I/O Register */
enum {
	GP_DIR_9 = 1<<25, /* IO_9 direct, 0=In/1=Out */
	GP_DIR_8 = 1<<24, /* IO_8 direct, 0=In/1=Out */
	GP_DIR_7 = 1<<23, /* IO_7 direct, 0=In/1=Out */
	GP_DIR_6 = 1<<22, /* IO_6 direct, 0=In/1=Out */
	GP_DIR_5 = 1<<21, /* IO_5 direct, 0=In/1=Out */
	GP_DIR_4 = 1<<20, /* IO_4 direct, 0=In/1=Out */
	GP_DIR_3 = 1<<19, /* IO_3 direct, 0=In/1=Out */
	GP_DIR_2 = 1<<18, /* IO_2 direct, 0=In/1=Out */
	GP_DIR_1 = 1<<17, /* IO_1 direct, 0=In/1=Out */
	GP_DIR_0 = 1<<16, /* IO_0 direct, 0=In/1=Out */

	GP_IO_9	= 1<<9,	/* IO_9 pin */
	GP_IO_8	= 1<<8,	/* IO_8 pin */
	GP_IO_7	= 1<<7,	/* IO_7 pin */
	GP_IO_6	= 1<<6,	/* IO_6 pin */
	GP_IO_5	= 1<<5,	/* IO_5 pin */
	GP_IO_4	= 1<<4,	/* IO_4 pin */
	GP_IO_3	= 1<<3,	/* IO_3 pin */
	GP_IO_2	= 1<<2,	/* IO_2 pin */
	GP_IO_1	= 1<<1,	/* IO_1 pin */
	GP_IO_0	= 1<<0,	/* IO_0 pin */
};

/* Descriptor Bit Definition */
/*	TxCtrl		Transmit Buffer Control Field */
/*	RxCtrl		Receive  Buffer Control Field */
enum {
	BMU_OWN		= 1<<31, /* OWN bit: 0=host/1=BMU */
	BMU_STF		= 1<<30, /* Start of Frame */
	BMU_EOF		= 1<<29, /* End of Frame */
	BMU_IRQ_EOB	= 1<<28, /* Req "End of Buffer" IRQ */
	BMU_IRQ_EOF	= 1<<27, /* Req "End of Frame" IRQ */
				/* TxCtrl specific bits */
	BMU_STFWD	= 1<<26, /* (Tx)	Store & Forward Frame */
	BMU_NO_FCS	= 1<<25, /* (Tx) Disable MAC FCS (CRC) generation */
	BMU_SW	= 1<<24, /* (Tx)	1 bit res. for SW use */
				/* RxCtrl specific bits */
	BMU_DEV_0	= 1<<26, /* (Rx)	Transfer data to Dev0 */
	BMU_STAT_VAL	= 1<<25, /* (Rx)	Rx Status Valid */
	BMU_TIST_VAL	= 1<<24, /* (Rx)	Rx TimeStamp Valid */
			/* Bit 23..16:	BMU Check Opcodes */
	BMU_CHECK	= 0x55<<16, /* Default BMU check */
	BMU_TCP_CHECK	= 0x56<<16, /* Descr with TCP ext */
	BMU_UDP_CHECK	= 0x57<<16, /* Descr with UDP ext (YUKON only) */
	BMU_BBC		= 0xffffL, /* Bit 15.. 0:	Buffer Byte Counter */
};

/*	B2_BSC_CTRL		 8 bit	Blink Source Counter Control */
enum {
	 BSC_START	= 1<<1,	/* Start Blink Source Counter */
	 BSC_STOP	= 1<<0,	/* Stop  Blink Source Counter */
};

/*	B2_BSC_STAT		 8 bit	Blink Source Counter Status */
enum {
	BSC_SRC		= 1<<0,	/* Blink Source, 0=Off / 1=On */
};

/*	B2_BSC_TST		16 bit	Blink Source Counter Test Reg */
enum {
	BSC_T_ON	= 1<<2,	/* Test mode on */
	BSC_T_OFF	= 1<<1,	/* Test mode off */
	BSC_T_STEP	= 1<<0,	/* Test step */
};

/*	B3_RAM_ADDR		32 bit	RAM Address, to read or write */
					/* Bit 31..19:	reserved */
#define RAM_ADR_RAN	0x0007ffffL	/* Bit 18.. 0:	RAM Address Range */
/* RAM Interface Registers */

/*	B3_RI_CTRL		16 bit	RAM Iface Control Register */
enum {
	RI_CLR_RD_PERR	= 1<<9,	/* Clear IRQ RAM Read Parity Err */
	RI_CLR_WR_PERR	= 1<<8,	/* Clear IRQ RAM Write Parity Err*/

	RI_RST_CLR	= 1<<1,	/* Clear RAM Interface Reset */
	RI_RST_SET	= 1<<0,	/* Set   RAM Interface Reset */
};

/* MAC Arbiter Registers */
/*	B3_MA_TO_CTRL	16 bit	MAC Arbiter Timeout Ctrl Reg */
enum {
	MA_FOE_ON	= 1<<3,	/* XMAC Fast Output Enable ON */
	MA_FOE_OFF	= 1<<2,	/* XMAC Fast Output Enable OFF */
	MA_RST_CLR	= 1<<1,	/* Clear MAC Arbiter Reset */
	MA_RST_SET	= 1<<0,	/* Set   MAC Arbiter Reset */

};

/* Timeout values */
#define SK_MAC_TO_53	72		/* MAC arbiter timeout */
#define SK_PKT_TO_53	0x2000		/* Packet arbiter timeout */
#define SK_PKT_TO_MAX	0xffff		/* Maximum value */
#define SK_RI_TO_53	36		/* RAM interface timeout */

/* Packet Arbiter Registers */
/*	B3_PA_CTRL		16 bit	Packet Arbiter Ctrl Register */
enum {
	PA_CLR_TO_TX2	= 1<<13,	/* Clear IRQ Packet Timeout TX2 */
	PA_CLR_TO_TX1	= 1<<12,	/* Clear IRQ Packet Timeout TX1 */
	PA_CLR_TO_RX2	= 1<<11,	/* Clear IRQ Packet Timeout RX2 */
	PA_CLR_TO_RX1	= 1<<10,	/* Clear IRQ Packet Timeout RX1 */
	PA_ENA_TO_TX2	= 1<<9,	/* Enable  Timeout Timer TX2 */
	PA_DIS_TO_TX2	= 1<<8,	/* Disable Timeout Timer TX2 */
	PA_ENA_TO_TX1	= 1<<7,	/* Enable  Timeout Timer TX1 */
	PA_DIS_TO_TX1	= 1<<6,	/* Disable Timeout Timer TX1 */
	PA_ENA_TO_RX2	= 1<<5,	/* Enable  Timeout Timer RX2 */
	PA_DIS_TO_RX2	= 1<<4,	/* Disable Timeout Timer RX2 */
	PA_ENA_TO_RX1	= 1<<3,	/* Enable  Timeout Timer RX1 */
	PA_DIS_TO_RX1	= 1<<2,	/* Disable Timeout Timer RX1 */
	PA_RST_CLR	= 1<<1,	/* Clear MAC Arbiter Reset */
	PA_RST_SET	= 1<<0,	/* Set   MAC Arbiter Reset */
};

#define PA_ENA_TO_ALL	(PA_ENA_TO_RX1 | PA_ENA_TO_RX2 |\
						PA_ENA_TO_TX1 | PA_ENA_TO_TX2)


/* Transmit Arbiter Registers MAC 1 and 2, use SK_REG() to access */
/*	TXA_ITI_INI		32 bit	Tx Arb Interval Timer Init Val */
/*	TXA_ITI_VAL		32 bit	Tx Arb Interval Timer Value */
/*	TXA_LIM_INI		32 bit	Tx Arb Limit Counter Init Val */
/*	TXA_LIM_VAL		32 bit	Tx Arb Limit Counter Value */

#define TXA_MAX_VAL	0x00ffffffUL	/* Bit 23.. 0:	Max TXA Timer/Cnt Val */

/*	TXA_CTRL		 8 bit	Tx Arbiter Control Register */
enum {
	TXA_ENA_FSYNC	= 1<<7,	/* Enable  force of sync Tx queue */
	TXA_DIS_FSYNC	= 1<<6,	/* Disable force of sync Tx queue */
	TXA_ENA_ALLOC	= 1<<5,	/* Enable  alloc of free bandwidth */
	TXA_DIS_ALLOC	= 1<<4,	/* Disable alloc of free bandwidth */
	TXA_START_RC	= 1<<3,	/* Start sync Rate Control */
	TXA_STOP_RC	= 1<<2,	/* Stop  sync Rate Control */
	TXA_ENA_ARB	= 1<<1,	/* Enable  Tx Arbiter */
	TXA_DIS_ARB	= 1<<0,	/* Disable Tx Arbiter */
};

/*
 *	Bank 4 - 5
 */
/* Transmit Arbiter Registers MAC 1 and 2, use SK_REG() to access */
enum {
	TXA_ITI_INI	= 0x0200,/* 32 bit	Tx Arb Interval Timer Init Val*/
	TXA_ITI_VAL	= 0x0204,/* 32 bit	Tx Arb Interval Timer Value */
	TXA_LIM_INI	= 0x0208,/* 32 bit	Tx Arb Limit Counter Init Val */
	TXA_LIM_VAL	= 0x020c,/* 32 bit	Tx Arb Limit Counter Value */
	TXA_CTRL	= 0x0210,/*  8 bit	Tx Arbiter Control Register */
	TXA_TEST	= 0x0211,/*  8 bit	Tx Arbiter Test Register */
	TXA_STAT	= 0x0212,/*  8 bit	Tx Arbiter Status Register */
};


enum {
	B6_EXT_REG	= 0x0300,/* External registers (GENESIS only) */
	B7_CFG_SPC	= 0x0380,/* copy of the Configuration register */
	B8_RQ1_REGS	= 0x0400,/* Receive Queue 1 */
	B8_RQ2_REGS	= 0x0480,/* Receive Queue 2 */
	B8_TS1_REGS	= 0x0600,/* Transmit sync queue 1 */
	B8_TA1_REGS	= 0x0680,/* Transmit async queue 1 */
	B8_TS2_REGS	= 0x0700,/* Transmit sync queue 2 */
	B8_TA2_REGS	= 0x0780,/* Transmit sync queue 2 */
	B16_RAM_REGS	= 0x0800,/* RAM Buffer Registers */
};

/* Queue Register Offsets, use Q_ADDR() to access */
enum {
	B8_Q_REGS = 0x0400, /* base of Queue registers */
	Q_D	= 0x00,	/* 8*32	bit	Current Descriptor */
	Q_DA_L	= 0x20,	/* 32 bit	Current Descriptor Address Low dWord */
	Q_DA_H	= 0x24,	/* 32 bit	Current Descriptor Address High dWord */
	Q_AC_L	= 0x28,	/* 32 bit	Current Address Counter Low dWord */
	Q_AC_H	= 0x2c,	/* 32 bit	Current Address Counter High dWord */
	Q_BC	= 0x30,	/* 32 bit	Current Byte Counter */
	Q_CSR	= 0x34,	/* 32 bit	BMU Control/Status Register */
	Q_F	= 0x38,	/* 32 bit	Flag Register */
	Q_T1	= 0x3c,	/* 32 bit	Test Register 1 */
	Q_T1_TR	= 0x3c,	/*  8 bit	Test Register 1 Transfer SM */
	Q_T1_WR	= 0x3d,	/*  8 bit	Test Register 1 Write Descriptor SM */
	Q_T1_RD	= 0x3e,	/*  8 bit	Test Register 1 Read Descriptor SM */
	Q_T1_SV	= 0x3f,	/*  8 bit	Test Register 1 Supervisor SM */
	Q_T2	= 0x40,	/* 32 bit	Test Register 2	*/
	Q_T3	= 0x44,	/* 32 bit	Test Register 3	*/

};
#define Q_ADDR(reg, offs) (B8_Q_REGS + (reg) + (offs))

/* RAM Buffer Register Offsets */
enum {

	RB_START	= 0x00,/* 32 bit	RAM Buffer Start Address */
	RB_END	= 0x04,/* 32 bit	RAM Buffer End Address */
	RB_WP	= 0x08,/* 32 bit	RAM Buffer Write Pointer */
	RB_RP	= 0x0c,/* 32 bit	RAM Buffer Read Pointer */
	RB_RX_UTPP	= 0x10,/* 32 bit	Rx Upper Threshold, Pause Packet */
	RB_RX_LTPP	= 0x14,/* 32 bit	Rx Lower Threshold, Pause Packet */
	RB_RX_UTHP	= 0x18,/* 32 bit	Rx Upper Threshold, High Prio */
	RB_RX_LTHP	= 0x1c,/* 32 bit	Rx Lower Threshold, High Prio */
	/* 0x10 - 0x1f:	reserved at Tx RAM Buffer Registers */
	RB_PC	= 0x20,/* 32 bit	RAM Buffer Packet Counter */
	RB_LEV	= 0x24,/* 32 bit	RAM Buffer Level Register */
	RB_CTRL	= 0x28,/* 32 bit	RAM Buffer Control Register */
	RB_TST1	= 0x29,/*  8 bit	RAM Buffer Test Register 1 */
	RB_TST2	= 0x2a,/*  8 bit	RAM Buffer Test Register 2 */
};

/* Receive and Transmit Queues */
enum {
	Q_R1	= 0x0000,	/* Receive Queue 1 */
	Q_R2	= 0x0080,	/* Receive Queue 2 */
	Q_XS1	= 0x0200,	/* Synchronous Transmit Queue 1 */
	Q_XA1	= 0x0280,	/* Asynchronous Transmit Queue 1 */
	Q_XS2	= 0x0300,	/* Synchronous Transmit Queue 2 */
	Q_XA2	= 0x0380,	/* Asynchronous Transmit Queue 2 */
};

/* Different MAC Types */
enum {
	SK_MAC_XMAC =	0,	/* Xaqti XMAC II */
	SK_MAC_GMAC =	1,	/* Marvell GMAC */
};

/* Different PHY Types */
enum {
	SK_PHY_XMAC	= 0,/* integrated in XMAC II */
	SK_PHY_BCOM	= 1,/* Broadcom BCM5400 */
	SK_PHY_LONE	= 2,/* Level One LXT1000  [not supported]*/
	SK_PHY_NAT	= 3,/* National DP83891  [not supported] */
	SK_PHY_MARV_COPPER= 4,/* Marvell 88E1011S */
	SK_PHY_MARV_FIBER = 5,/* Marvell 88E1011S working on fiber */
};

/* PHY addresses (bits 12..8 of PHY address reg) */
enum {
	PHY_ADDR_XMAC	= 0<<8,
	PHY_ADDR_BCOM	= 1<<8,

/* GPHY address (bits 15..11 of SMI control reg) */
	PHY_ADDR_MARV	= 0,
};

#define RB_ADDR(offs, queue) (B16_RAM_REGS + (queue) + (offs))

/* Receive MAC FIFO, Receive LED, and Link_Sync regs (GENESIS only) */
enum {
	RX_MFF_EA	= 0x0c00,/* 32 bit	Receive MAC FIFO End Address */
	RX_MFF_WP	= 0x0c04,/* 32 bit	Receive MAC FIFO Write Pointer */

	RX_MFF_RP	= 0x0c0c,/* 32 bit	Receive MAC FIFO Read Pointer */
	RX_MFF_PC	= 0x0c10,/* 32 bit	Receive MAC FIFO Packet Cnt */
	RX_MFF_LEV	= 0x0c14,/* 32 bit	Receive MAC FIFO Level */
	RX_MFF_CTRL1	= 0x0c18,/* 16 bit	Receive MAC FIFO Control Reg 1*/
	RX_MFF_STAT_TO	= 0x0c1a,/*  8 bit	Receive MAC Status Timeout */
	RX_MFF_TIST_TO	= 0x0c1b,/*  8 bit	Receive MAC Time Stamp Timeout */
	RX_MFF_CTRL2	= 0x0c1c,/*  8 bit	Receive MAC FIFO Control Reg 2*/
	RX_MFF_TST1	= 0x0c1d,/*  8 bit	Receive MAC FIFO Test Reg 1 */
	RX_MFF_TST2	= 0x0c1e,/*  8 bit	Receive MAC FIFO Test Reg 2 */

	RX_LED_INI	= 0x0c20,/* 32 bit	Receive LED Cnt Init Value */
	RX_LED_VAL	= 0x0c24,/* 32 bit	Receive LED Cnt Current Value */
	RX_LED_CTRL	= 0x0c28,/*  8 bit	Receive LED Cnt Control Reg */
	RX_LED_TST	= 0x0c29,/*  8 bit	Receive LED Cnt Test Register */

	LNK_SYNC_INI	= 0x0c30,/* 32 bit	Link Sync Cnt Init Value */
	LNK_SYNC_VAL	= 0x0c34,/* 32 bit	Link Sync Cnt Current Value */
	LNK_SYNC_CTRL	= 0x0c38,/*  8 bit	Link Sync Cnt Control Register */
	LNK_SYNC_TST	= 0x0c39,/*  8 bit	Link Sync Cnt Test Register */
	LNK_LED_REG	= 0x0c3c,/*  8 bit	Link LED Register */
};

/* Receive and Transmit MAC FIFO Registers (GENESIS only) */
/*	RX_MFF_CTRL1	16 bit	Receive MAC FIFO Control Reg 1 */
enum {
	MFF_ENA_RDY_PAT	= 1<<13,	/* Enable  Ready Patch */
	MFF_DIS_RDY_PAT	= 1<<12,	/* Disable Ready Patch */
	MFF_ENA_TIM_PAT	= 1<<11,	/* Enable  Timing Patch */
	MFF_DIS_TIM_PAT	= 1<<10,	/* Disable Timing Patch */
	MFF_ENA_ALM_FUL	= 1<<9,	/* Enable  AlmostFull Sign */
	MFF_DIS_ALM_FUL	= 1<<8,	/* Disable AlmostFull Sign */
	MFF_ENA_PAUSE	= 1<<7,	/* Enable  Pause Signaling */
	MFF_DIS_PAUSE	= 1<<6,	/* Disable Pause Signaling */
	MFF_ENA_FLUSH	= 1<<5,	/* Enable  Frame Flushing */
	MFF_DIS_FLUSH	= 1<<4,	/* Disable Frame Flushing */
	MFF_ENA_TIST	= 1<<3,	/* Enable  Time Stamp Gener */
	MFF_DIS_TIST	= 1<<2,	/* Disable Time Stamp Gener */
	MFF_CLR_INTIST	= 1<<1,	/* Clear IRQ No Time Stamp */
	MFF_CLR_INSTAT	= 1<<0,	/* Clear IRQ No Status */
#define MFF_RX_CTRL_DEF MFF_ENA_TIM_PAT
};

/*	TX_MFF_CTRL1	16 bit	Transmit MAC FIFO Control Reg 1 */
enum {
	MFF_CLR_PERR	= 1<<15,	/* Clear Parity Error IRQ */
								/* Bit 14:	reserved */
	MFF_ENA_PKT_REC	= 1<<13,	/* Enable  Packet Recovery */
	MFF_DIS_PKT_REC	= 1<<12,	/* Disable Packet Recovery */

	MFF_ENA_W4E	= 1<<7,	/* Enable  Wait for Empty */
	MFF_DIS_W4E	= 1<<6,	/* Disable Wait for Empty */

	MFF_ENA_LOOPB	= 1<<3,	/* Enable  Loopback */
	MFF_DIS_LOOPB	= 1<<2,	/* Disable Loopback */
	MFF_CLR_MAC_RST	= 1<<1,	/* Clear XMAC Reset */
	MFF_SET_MAC_RST	= 1<<0,	/* Set   XMAC Reset */
};

#define MFF_TX_CTRL_DEF	(MFF_ENA_PKT_REC | MFF_ENA_TIM_PAT | MFF_ENA_FLUSH)

/*	RX_MFF_TST2	 	 8 bit	Receive MAC FIFO Test Register 2 */
/*	TX_MFF_TST2	 	 8 bit	Transmit MAC FIFO Test Register 2 */
enum {
	MFF_WSP_T_ON	= 1<<6,	/* Tx: Write Shadow Ptr TestOn */
	MFF_WSP_T_OFF	= 1<<5,	/* Tx: Write Shadow Ptr TstOff */
	MFF_WSP_INC	= 1<<4,	/* Tx: Write Shadow Ptr Increment */
	MFF_PC_DEC	= 1<<3,	/* Packet Counter Decrement */
	MFF_PC_T_ON	= 1<<2,	/* Packet Counter Test On */
	MFF_PC_T_OFF	= 1<<1,	/* Packet Counter Test Off */
	MFF_PC_INC	= 1<<0,	/* Packet Counter Increment */
};

/*	RX_MFF_TST1	 	 8 bit	Receive MAC FIFO Test Register 1 */
/*	TX_MFF_TST1	 	 8 bit	Transmit MAC FIFO Test Register 1 */
enum {
	MFF_WP_T_ON	= 1<<6,	/* Write Pointer Test On */
	MFF_WP_T_OFF	= 1<<5,	/* Write Pointer Test Off */
	MFF_WP_INC	= 1<<4,	/* Write Pointer Increm */

	MFF_RP_T_ON	= 1<<2,	/* Read Pointer Test On */
	MFF_RP_T_OFF	= 1<<1,	/* Read Pointer Test Off */
	MFF_RP_DEC	= 1<<0,	/* Read Pointer Decrement */
};

/*	RX_MFF_CTRL2	 8 bit	Receive MAC FIFO Control Reg 2 */
/*	TX_MFF_CTRL2	 8 bit	Transmit MAC FIFO Control Reg 2 */
enum {
	MFF_ENA_OP_MD	= 1<<3,	/* Enable  Operation Mode */
	MFF_DIS_OP_MD	= 1<<2,	/* Disable Operation Mode */
	MFF_RST_CLR	= 1<<1,	/* Clear MAC FIFO Reset */
	MFF_RST_SET	= 1<<0,	/* Set   MAC FIFO Reset */
};


/*	Link LED Counter Registers (GENESIS only) */

/*	RX_LED_CTRL		 8 bit	Receive LED Cnt Control Reg */
/*	TX_LED_CTRL		 8 bit	Transmit LED Cnt Control Reg */
/*	LNK_SYNC_CTRL	 8 bit	Link Sync Cnt Control Register */
enum {
	LED_START	= 1<<2,	/* Start Timer */
	LED_STOP	= 1<<1,	/* Stop Timer */
	LED_STATE	= 1<<0,	/* Rx/Tx: LED State, 1=LED on */
};

/*	RX_LED_TST		 8 bit	Receive LED Cnt Test Register */
/*	TX_LED_TST		 8 bit	Transmit LED Cnt Test Register */
/*	LNK_SYNC_TST	 8 bit	Link Sync Cnt Test Register */
enum {
	LED_T_ON	= 1<<2,	/* LED Counter Test mode On */
	LED_T_OFF	= 1<<1,	/* LED Counter Test mode Off */
	LED_T_STEP	= 1<<0,	/* LED Counter Step */
};

/*	LNK_LED_REG	 	 8 bit	Link LED Register */
enum {
	LED_BLK_ON	= 1<<5,	/* Link LED Blinking On */
	LED_BLK_OFF	= 1<<4,	/* Link LED Blinking Off */
	LED_SYNC_ON	= 1<<3,	/* Use Sync Wire to switch LED */
	LED_SYNC_OFF	= 1<<2,	/* Disable Sync Wire Input */
	LED_ON	= 1<<1,	/* switch LED on */
	LED_OFF	= 1<<0,	/* switch LED off */
};

/* Receive GMAC FIFO (YUKON) */
enum {
	RX_GMF_EA	= 0x0c40,/* 32 bit	Rx GMAC FIFO End Address */
	RX_GMF_AF_THR	= 0x0c44,/* 32 bit	Rx GMAC FIFO Almost Full Thresh. */
	RX_GMF_CTRL_T	= 0x0c48,/* 32 bit	Rx GMAC FIFO Control/Test */
	RX_GMF_FL_MSK	= 0x0c4c,/* 32 bit	Rx GMAC FIFO Flush Mask */
	RX_GMF_FL_THR	= 0x0c50,/* 32 bit	Rx GMAC FIFO Flush Threshold */
	RX_GMF_WP	= 0x0c60,/* 32 bit	Rx GMAC FIFO Write Pointer */
	RX_GMF_WLEV	= 0x0c68,/* 32 bit	Rx GMAC FIFO Write Level */
	RX_GMF_RP	= 0x0c70,/* 32 bit	Rx GMAC FIFO Read Pointer */
	RX_GMF_RLEV	= 0x0c78,/* 32 bit	Rx GMAC FIFO Read Level */
};


/*	TXA_TEST		 8 bit	Tx Arbiter Test Register */
enum {
	TXA_INT_T_ON	= 1<<5,	/* Tx Arb Interval Timer Test On */
	TXA_INT_T_OFF	= 1<<4,	/* Tx Arb Interval Timer Test Off */
	TXA_INT_T_STEP	= 1<<3,	/* Tx Arb Interval Timer Step */
	TXA_LIM_T_ON	= 1<<2,	/* Tx Arb Limit Timer Test On */
	TXA_LIM_T_OFF	= 1<<1,	/* Tx Arb Limit Timer Test Off */
	TXA_LIM_T_STEP	= 1<<0,	/* Tx Arb Limit Timer Step */
};

/*	TXA_STAT		 8 bit	Tx Arbiter Status Register */
enum {
	TXA_PRIO_XS	= 1<<0,	/* sync queue has prio to send */
};


/*	Q_BC			32 bit	Current Byte Counter */

/* BMU Control Status Registers */
/*	B0_R1_CSR		32 bit	BMU Ctrl/Stat Rx Queue 1 */
/*	B0_R2_CSR		32 bit	BMU Ctrl/Stat Rx Queue 2 */
/*	B0_XA1_CSR		32 bit	BMU Ctrl/Stat Sync Tx Queue 1 */
/*	B0_XS1_CSR		32 bit	BMU Ctrl/Stat Async Tx Queue 1 */
/*	B0_XA2_CSR		32 bit	BMU Ctrl/Stat Sync Tx Queue 2 */
/*	B0_XS2_CSR		32 bit	BMU Ctrl/Stat Async Tx Queue 2 */
/*	Q_CSR			32 bit	BMU Control/Status Register */

enum {
	CSR_SV_IDLE	= 1<<24,	/* BMU SM Idle */

	CSR_DESC_CLR	= 1<<21,	/* Clear Reset for Descr */
	CSR_DESC_SET	= 1<<20,	/* Set   Reset for Descr */
	CSR_FIFO_CLR	= 1<<19,	/* Clear Reset for FIFO */
	CSR_FIFO_SET	= 1<<18,	/* Set   Reset for FIFO */
	CSR_HPI_RUN	= 1<<17,	/* Release HPI SM */
	CSR_HPI_RST	= 1<<16,	/* Reset   HPI SM to Idle */
	CSR_SV_RUN	= 1<<15,	/* Release Supervisor SM */
	CSR_SV_RST	= 1<<14,	/* Reset   Supervisor SM */
	CSR_DREAD_RUN	= 1<<13,	/* Release Descr Read SM */
	CSR_DREAD_RST	= 1<<12,	/* Reset   Descr Read SM */
	CSR_DWRITE_RUN	= 1<<11,	/* Release Descr Write SM */
	CSR_DWRITE_RST	= 1<<10,	/* Reset   Descr Write SM */
	CSR_TRANS_RUN	= 1<<9,		/* Release Transfer SM */
	CSR_TRANS_RST	= 1<<8,		/* Reset   Transfer SM */
	CSR_ENA_POL	= 1<<7,		/* Enable  Descr Polling */
	CSR_DIS_POL	= 1<<6,		/* Disable Descr Polling */
	CSR_STOP	= 1<<5,		/* Stop  Rx/Tx Queue */
	CSR_START	= 1<<4,		/* Start Rx/Tx Queue */
	CSR_IRQ_CL_P	= 1<<3,		/* (Rx)	Clear Parity IRQ */
	CSR_IRQ_CL_B	= 1<<2,		/* Clear EOB IRQ */
	CSR_IRQ_CL_F	= 1<<1,		/* Clear EOF IRQ */
	CSR_IRQ_CL_C	= 1<<0,		/* Clear ERR IRQ */
};

#define CSR_SET_RESET	(CSR_DESC_SET | CSR_FIFO_SET | CSR_HPI_RST |\
			CSR_SV_RST | CSR_DREAD_RST | CSR_DWRITE_RST |\
			CSR_TRANS_RST)
#define CSR_CLR_RESET	(CSR_DESC_CLR | CSR_FIFO_CLR | CSR_HPI_RUN |\
			CSR_SV_RUN | CSR_DREAD_RUN | CSR_DWRITE_RUN |\
			CSR_TRANS_RUN)

/*	Q_F				32 bit	Flag Register */
enum {
	F_ALM_FULL	= 1<<27,	/* Rx FIFO: almost full */
	F_EMPTY		= 1<<27,	/* Tx FIFO: empty flag */
	F_FIFO_EOF	= 1<<26,	/* Tag (EOF Flag) bit in FIFO */
	F_WM_REACHED	= 1<<25,	/* Watermark reached */

	F_FIFO_LEVEL	= 0x1fL<<16,	/* Bit 23..16:	# of Qwords in FIFO */
	F_WATER_MARK	= 0x0007ffL,	/* Bit 10.. 0:	Watermark */
};

/* RAM Buffer Register Offsets, use RB_ADDR(Queue, Offs) to access */
/*	RB_START		32 bit	RAM Buffer Start Address */
/*	RB_END			32 bit	RAM Buffer End Address */
/*	RB_WP			32 bit	RAM Buffer Write Pointer */
/*	RB_RP			32 bit	RAM Buffer Read Pointer */
/*	RB_RX_UTPP		32 bit	Rx Upper Threshold, Pause Pack */
/*	RB_RX_LTPP		32 bit	Rx Lower Threshold, Pause Pack */
/*	RB_RX_UTHP		32 bit	Rx Upper Threshold, High Prio */
/*	RB_RX_LTHP		32 bit	Rx Lower Threshold, High Prio */
/*	RB_PC			32 bit	RAM Buffer Packet Counter */
/*	RB_LEV			32 bit	RAM Buffer Level Register */

#define RB_MSK	0x0007ffff	/* Bit 18.. 0:	RAM Buffer Pointer Bits */
/*	RB_TST2			 8 bit	RAM Buffer Test Register 2 */
/*	RB_TST1			 8 bit	RAM Buffer Test Register 1 */

/*	RB_CTRL			 8 bit	RAM Buffer Control Register */
enum {
	RB_ENA_STFWD	= 1<<5,	/* Enable  Store & Forward */
	RB_DIS_STFWD	= 1<<4,	/* Disable Store & Forward */
	RB_ENA_OP_MD	= 1<<3,	/* Enable  Operation Mode */
	RB_DIS_OP_MD	= 1<<2,	/* Disable Operation Mode */
	RB_RST_CLR	= 1<<1,	/* Clear RAM Buf STM Reset */
	RB_RST_SET	= 1<<0,	/* Set   RAM Buf STM Reset */
};

/* Transmit MAC FIFO and Transmit LED Registers (GENESIS only), */
enum {
	TX_MFF_EA	= 0x0d00,/* 32 bit	Transmit MAC FIFO End Address */
	TX_MFF_WP	= 0x0d04,/* 32 bit	Transmit MAC FIFO WR Pointer */
	TX_MFF_WSP	= 0x0d08,/* 32 bit	Transmit MAC FIFO WR Shadow Ptr */
	TX_MFF_RP	= 0x0d0c,/* 32 bit	Transmit MAC FIFO RD Pointer */
	TX_MFF_PC	= 0x0d10,/* 32 bit	Transmit MAC FIFO Packet Cnt */
	TX_MFF_LEV	= 0x0d14,/* 32 bit	Transmit MAC FIFO Level */
	TX_MFF_CTRL1	= 0x0d18,/* 16 bit	Transmit MAC FIFO Ctrl Reg 1 */
	TX_MFF_WAF	= 0x0d1a,/*  8 bit	Transmit MAC Wait after flush */

	TX_MFF_CTRL2	= 0x0d1c,/*  8 bit	Transmit MAC FIFO Ctrl Reg 2 */
	TX_MFF_TST1	= 0x0d1d,/*  8 bit	Transmit MAC FIFO Test Reg 1 */
	TX_MFF_TST2	= 0x0d1e,/*  8 bit	Transmit MAC FIFO Test Reg 2 */

	TX_LED_INI	= 0x0d20,/* 32 bit	Transmit LED Cnt Init Value */
	TX_LED_VAL	= 0x0d24,/* 32 bit	Transmit LED Cnt Current Val */
	TX_LED_CTRL	= 0x0d28,/*  8 bit	Transmit LED Cnt Control Reg */
	TX_LED_TST	= 0x0d29,/*  8 bit	Transmit LED Cnt Test Reg */
};

/* Counter and Timer constants, for a host clock of 62.5 MHz */
#define SK_XMIT_DUR		0x002faf08UL	/*  50 ms */
#define SK_BLK_DUR		0x01dcd650UL	/* 500 ms */

#define SK_DPOLL_DEF	0x00ee6b28UL	/* 250 ms at 62.5 MHz */

#define SK_DPOLL_MAX	0x00ffffffUL	/* 268 ms at 62.5 MHz */
					/* 215 ms at 78.12 MHz */

#define SK_FACT_62		100	/* is given in percent */
#define SK_FACT_53		 85     /* on GENESIS:	53.12 MHz */
#define SK_FACT_78		125	/* on YUKON:	78.12 MHz */


/* Transmit GMAC FIFO (YUKON only) */
enum {
	TX_GMF_EA	= 0x0d40,/* 32 bit	Tx GMAC FIFO End Address */
	TX_GMF_AE_THR	= 0x0d44,/* 32 bit	Tx GMAC FIFO Almost Empty Thresh.*/
	TX_GMF_CTRL_T	= 0x0d48,/* 32 bit	Tx GMAC FIFO Control/Test */

	TX_GMF_WP	= 0x0d60,/* 32 bit 	Tx GMAC FIFO Write Pointer */
	TX_GMF_WSP	= 0x0d64,/* 32 bit 	Tx GMAC FIFO Write Shadow Ptr. */
	TX_GMF_WLEV	= 0x0d68,/* 32 bit 	Tx GMAC FIFO Write Level */

	TX_GMF_RP	= 0x0d70,/* 32 bit 	Tx GMAC FIFO Read Pointer */
	TX_GMF_RSTP	= 0x0d74,/* 32 bit 	Tx GMAC FIFO Restart Pointer */
	TX_GMF_RLEV	= 0x0d78,/* 32 bit 	Tx GMAC FIFO Read Level */

	/* Descriptor Poll Timer Registers */
	B28_DPT_INI	= 0x0e00,/* 24 bit	Descriptor Poll Timer Init Val */
	B28_DPT_VAL	= 0x0e04,/* 24 bit	Descriptor Poll Timer Curr Val */
	B28_DPT_CTRL	= 0x0e08,/*  8 bit	Descriptor Poll Timer Ctrl Reg */

	B28_DPT_TST	= 0x0e0a,/*  8 bit	Descriptor Poll Timer Test Reg */

	/* Time Stamp Timer Registers (YUKON only) */
	GMAC_TI_ST_VAL	= 0x0e14,/* 32 bit	Time Stamp Timer Curr Val */
	GMAC_TI_ST_CTRL	= 0x0e18,/*  8 bit	Time Stamp Timer Ctrl Reg */
	GMAC_TI_ST_TST	= 0x0e1a,/*  8 bit	Time Stamp Timer Test Reg */
};


enum {
	LINKLED_OFF 	     = 0x01,
	LINKLED_ON  	     = 0x02,
	LINKLED_LINKSYNC_OFF = 0x04,
	LINKLED_LINKSYNC_ON  = 0x08,
	LINKLED_BLINK_OFF    = 0x10,
	LINKLED_BLINK_ON     = 0x20,
};

/* GMAC and GPHY Control Registers (YUKON only) */
enum {
	GMAC_CTRL	= 0x0f00,/* 32 bit	GMAC Control Reg */
	GPHY_CTRL	= 0x0f04,/* 32 bit	GPHY Control Reg */
	GMAC_IRQ_SRC	= 0x0f08,/*  8 bit	GMAC Interrupt Source Reg */
	GMAC_IRQ_MSK	= 0x0f0c,/*  8 bit	GMAC Interrupt Mask Reg */
	GMAC_LINK_CTRL	= 0x0f10,/* 16 bit	Link Control Reg */

/* Wake-up Frame Pattern Match Control Registers (YUKON only) */

	WOL_REG_OFFS	= 0x20,/* HW-Bug: Address is + 0x20 against spec. */

	WOL_CTRL_STAT	= 0x0f20,/* 16 bit	WOL Control/Status Reg */
	WOL_MATCH_CTL	= 0x0f22,/*  8 bit	WOL Match Control Reg */
	WOL_MATCH_RES	= 0x0f23,/*  8 bit	WOL Match Result Reg */
	WOL_MAC_ADDR	= 0x0f24,/* 32 bit	WOL MAC Address */
	WOL_PATT_RPTR	= 0x0f2c,/*  8 bit	WOL Pattern Read Pointer */

/* WOL Pattern Length Registers (YUKON only) */

	WOL_PATT_LEN_LO	= 0x0f30,/* 32 bit	WOL Pattern Length 3..0 */
	WOL_PATT_LEN_HI	= 0x0f34,/* 24 bit	WOL Pattern Length 6..4 */

/* WOL Pattern Counter Registers (YUKON only) */

	WOL_PATT_CNT_0	= 0x0f38,/* 32 bit	WOL Pattern Counter 3..0 */
	WOL_PATT_CNT_4	= 0x0f3c,/* 24 bit	WOL Pattern Counter 6..4 */
};

enum {
	WOL_PATT_RAM_1	= 0x1000,/*  WOL Pattern RAM Link 1 */
	WOL_PATT_RAM_2	= 0x1400,/*  WOL Pattern RAM Link 2 */
};

enum {
	BASE_XMAC_1	= 0x2000,/* XMAC 1 registers */
	BASE_GMAC_1	= 0x2800,/* GMAC 1 registers */
	BASE_XMAC_2	= 0x3000,/* XMAC 2 registers */
	BASE_GMAC_2	= 0x3800,/* GMAC 2 registers */
};

/*
 * Receive Frame Status Encoding
 */
enum {
	XMR_FS_LEN	= 0x3fff<<18,	/* Bit 31..18:	Rx Frame Length */
	XMR_FS_LEN_SHIFT = 18,
	XMR_FS_2L_VLAN	= 1<<17, /* Bit 17:	tagged wh 2Lev VLAN ID*/
	XMR_FS_1_VLAN	= 1<<16, /* Bit 16:	tagged wh 1ev VLAN ID*/
	XMR_FS_BC	= 1<<15, /* Bit 15:	Broadcast Frame */
	XMR_FS_MC	= 1<<14, /* Bit 14:	Multicast Frame */
	XMR_FS_UC	= 1<<13, /* Bit 13:	Unicast Frame */

	XMR_FS_BURST	= 1<<11, /* Bit 11:	Burst Mode */
	XMR_FS_CEX_ERR	= 1<<10, /* Bit 10:	Carrier Ext. Error */
	XMR_FS_802_3	= 1<<9, /* Bit  9:	802.3 Frame */
	XMR_FS_COL_ERR	= 1<<8, /* Bit  8:	Collision Error */
	XMR_FS_CAR_ERR	= 1<<7, /* Bit  7:	Carrier Event Error */
	XMR_FS_LEN_ERR	= 1<<6, /* Bit  6:	In-Range Length Error */
	XMR_FS_FRA_ERR	= 1<<5, /* Bit  5:	Framing Error */
	XMR_FS_RUNT	= 1<<4, /* Bit  4:	Runt Frame */
	XMR_FS_LNG_ERR	= 1<<3, /* Bit  3:	Giant (Jumbo) Frame */
	XMR_FS_FCS_ERR	= 1<<2, /* Bit  2:	Frame Check Sequ Err */
	XMR_FS_ERR	= 1<<1, /* Bit  1:	Frame Error */
	XMR_FS_MCTRL	= 1<<0, /* Bit  0:	MAC Control Packet */

/*
 * XMR_FS_ERR will be set if
 *	XMR_FS_FCS_ERR, XMR_FS_LNG_ERR, XMR_FS_RUNT,
 *	XMR_FS_FRA_ERR, XMR_FS_LEN_ERR, or XMR_FS_CEX_ERR
 * is set. XMR_FS_LNG_ERR and XMR_FS_LEN_ERR will issue
 * XMR_FS_ERR unless the corresponding bit in the Receive Command
 * Register is set.
 */
};

/*
,* XMAC-PHY Registers, indirect addressed over the XMAC
 */
enum {
	PHY_XMAC_CTRL		= 0x00,/* 16 bit r/w	PHY Control Register */
	PHY_XMAC_STAT		= 0x01,/* 16 bit r/w	PHY Status Register */
	PHY_XMAC_ID0		= 0x02,/* 16 bit r/o	PHY ID0 Register */
	PHY_XMAC_ID1		= 0x03,/* 16 bit r/o	PHY ID1 Register */
	PHY_XMAC_AUNE_ADV	= 0x04,/* 16 bit r/w	Auto-Neg. Advertisement */
	PHY_XMAC_AUNE_LP	= 0x05,/* 16 bit r/o	Link Partner Abi Reg */
	PHY_XMAC_AUNE_EXP	= 0x06,/* 16 bit r/o	Auto-Neg. Expansion Reg */
	PHY_XMAC_NEPG	= 0x07,/* 16 bit r/w	Next Page Register */
	PHY_XMAC_NEPG_LP	= 0x08,/* 16 bit r/o	Next Page Link Partner */

	PHY_XMAC_EXT_STAT	= 0x0f,/* 16 bit r/o	Ext Status Register */
	PHY_XMAC_RES_ABI	= 0x10,/* 16 bit r/o	PHY Resolved Ability */
};
/*
 * Broadcom-PHY Registers, indirect addressed over XMAC
 */
enum {
	PHY_BCOM_CTRL		= 0x00,/* 16 bit r/w	PHY Control Register */
	PHY_BCOM_STAT		= 0x01,/* 16 bit r/o	PHY Status Register */
	PHY_BCOM_ID0		= 0x02,/* 16 bit r/o	PHY ID0 Register */
	PHY_BCOM_ID1		= 0x03,/* 16 bit r/o	PHY ID1 Register */
	PHY_BCOM_AUNE_ADV	= 0x04,/* 16 bit r/w	Auto-Neg. Advertisement */
	PHY_BCOM_AUNE_LP	= 0x05,/* 16 bit r/o	Link Part Ability Reg */
	PHY_BCOM_AUNE_EXP	= 0x06,/* 16 bit r/o	Auto-Neg. Expansion Reg */
	PHY_BCOM_NEPG		= 0x07,/* 16 bit r/w	Next Page Register */
	PHY_BCOM_NEPG_LP	= 0x08,/* 16 bit r/o	Next Page Link Partner */
	/* Broadcom-specific registers */
	PHY_BCOM_1000T_CTRL	= 0x09,/* 16 bit r/w	1000Base-T Control Reg */
	PHY_BCOM_1000T_STAT	= 0x0a,/* 16 bit r/o	1000Base-T Status Reg */
	PHY_BCOM_EXT_STAT	= 0x0f,/* 16 bit r/o	Extended Status Reg */
	PHY_BCOM_P_EXT_CTRL	= 0x10,/* 16 bit r/w	PHY Extended Ctrl Reg */
	PHY_BCOM_P_EXT_STAT	= 0x11,/* 16 bit r/o	PHY Extended Stat Reg */
	PHY_BCOM_RE_CTR		= 0x12,/* 16 bit r/w	Receive Error Counter */
	PHY_BCOM_FC_CTR		= 0x13,/* 16 bit r/w	False Carrier Sense Cnt */
	PHY_BCOM_RNO_CTR	= 0x14,/* 16 bit r/w	Receiver NOT_OK Cnt */

	PHY_BCOM_AUX_CTRL	= 0x18,/* 16 bit r/w	Auxiliary Control Reg */
	PHY_BCOM_AUX_STAT	= 0x19,/* 16 bit r/o	Auxiliary Stat Summary */
	PHY_BCOM_INT_STAT	= 0x1a,/* 16 bit r/o	Interrupt Status Reg */
	PHY_BCOM_INT_MASK	= 0x1b,/* 16 bit r/w	Interrupt Mask Reg */
};

/*
 * Marvel-PHY Registers, indirect addressed over GMAC
 */
enum {
	PHY_MARV_CTRL		= 0x00,/* 16 bit r/w	PHY Control Register */
	PHY_MARV_STAT		= 0x01,/* 16 bit r/o	PHY Status Register */
	PHY_MARV_ID0		= 0x02,/* 16 bit r/o	PHY ID0 Register */
	PHY_MARV_ID1		= 0x03,/* 16 bit r/o	PHY ID1 Register */
	PHY_MARV_AUNE_ADV	= 0x04,/* 16 bit r/w	Auto-Neg. Advertisement */
	PHY_MARV_AUNE_LP	= 0x05,/* 16 bit r/o	Link Part Ability Reg */
	PHY_MARV_AUNE_EXP	= 0x06,/* 16 bit r/o	Auto-Neg. Expansion Reg */
	PHY_MARV_NEPG		= 0x07,/* 16 bit r/w	Next Page Register */
	PHY_MARV_NEPG_LP	= 0x08,/* 16 bit r/o	Next Page Link Partner */
	/* Marvel-specific registers */
	PHY_MARV_1000T_CTRL	= 0x09,/* 16 bit r/w	1000Base-T Control Reg */
	PHY_MARV_1000T_STAT	= 0x0a,/* 16 bit r/o	1000Base-T Status Reg */
	PHY_MARV_EXT_STAT	= 0x0f,/* 16 bit r/o	Extended Status Reg */
	PHY_MARV_PHY_CTRL	= 0x10,/* 16 bit r/w	PHY Specific Ctrl Reg */
	PHY_MARV_PHY_STAT	= 0x11,/* 16 bit r/o	PHY Specific Stat Reg */
	PHY_MARV_INT_MASK	= 0x12,/* 16 bit r/w	Interrupt Mask Reg */
	PHY_MARV_INT_STAT	= 0x13,/* 16 bit r/o	Interrupt Status Reg */
	PHY_MARV_EXT_CTRL	= 0x14,/* 16 bit r/w	Ext. PHY Specific Ctrl */
	PHY_MARV_RXE_CNT	= 0x15,/* 16 bit r/w	Receive Error Counter */
	PHY_MARV_EXT_ADR	= 0x16,/* 16 bit r/w	Ext. Ad. for Cable Diag. */
	PHY_MARV_PORT_IRQ	= 0x17,/* 16 bit r/o	Port 0 IRQ (88E1111 only) */
	PHY_MARV_LED_CTRL	= 0x18,/* 16 bit r/w	LED Control Reg */
	PHY_MARV_LED_OVER	= 0x19,/* 16 bit r/w	Manual LED Override Reg */
	PHY_MARV_EXT_CTRL_2	= 0x1a,/* 16 bit r/w	Ext. PHY Specific Ctrl 2 */
	PHY_MARV_EXT_P_STAT	= 0x1b,/* 16 bit r/w	Ext. PHY Spec. Stat Reg */
	PHY_MARV_CABLE_DIAG	= 0x1c,/* 16 bit r/o	Cable Diagnostic Reg */
	PHY_MARV_PAGE_ADDR	= 0x1d,/* 16 bit r/w	Extended Page Address Reg */
	PHY_MARV_PAGE_DATA	= 0x1e,/* 16 bit r/w	Extended Page Data Reg */

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
	PHY_MARV_FE_LED_PAR	= 0x16,/* 16 bit r/w	LED Parallel Select Reg. */
	PHY_MARV_FE_LED_SER	= 0x17,/* 16 bit r/w	LED Stream Select S. LED */
	PHY_MARV_FE_VCT_TX	= 0x1a,/* 16 bit r/w	VCT Reg. for TXP/N Pins */
	PHY_MARV_FE_VCT_RX	= 0x1b,/* 16 bit r/o	VCT Reg. for RXP/N Pins */
	PHY_MARV_FE_SPEC_2	= 0x1c,/* 16 bit r/w	Specific Control Reg. 2 */
};

enum {
	PHY_CT_RESET	= 1<<15, /* Bit 15: (sc)	clear all PHY related regs */
	PHY_CT_LOOP	= 1<<14, /* Bit 14:	enable Loopback over PHY */
	PHY_CT_SPS_LSB	= 1<<13, /* Bit 13:	Speed select, lower bit */
	PHY_CT_ANE	= 1<<12, /* Bit 12:	Auto-Negotiation Enabled */
	PHY_CT_PDOWN	= 1<<11, /* Bit 11:	Power Down Mode */
	PHY_CT_ISOL	= 1<<10, /* Bit 10:	Isolate Mode */
	PHY_CT_RE_CFG	= 1<<9, /* Bit  9:	(sc) Restart Auto-Negotiation */
	PHY_CT_DUP_MD	= 1<<8, /* Bit  8:	Duplex Mode */
	PHY_CT_COL_TST	= 1<<7, /* Bit  7:	Collision Test enabled */
	PHY_CT_SPS_MSB	= 1<<6, /* Bit  6:	Speed select, upper bit */
};

enum {
	PHY_CT_SP1000	= PHY_CT_SPS_MSB, /* enable speed of 1000 Mbps */
	PHY_CT_SP100	= PHY_CT_SPS_LSB, /* enable speed of  100 Mbps */
	PHY_CT_SP10	= 0,		  /* enable speed of   10 Mbps */
};

enum {
	PHY_ST_EXT_ST	= 1<<8, /* Bit  8:	Extended Status Present */

	PHY_ST_PRE_SUP	= 1<<6, /* Bit  6:	Preamble Suppression */
	PHY_ST_AN_OVER	= 1<<5, /* Bit  5:	Auto-Negotiation Over */
	PHY_ST_REM_FLT	= 1<<4, /* Bit  4:	Remote Fault Condition Occured */
	PHY_ST_AN_CAP	= 1<<3, /* Bit  3:	Auto-Negotiation Capability */
	PHY_ST_LSYNC	= 1<<2, /* Bit  2:	Link Synchronized */
	PHY_ST_JAB_DET	= 1<<1, /* Bit  1:	Jabber Detected */
	PHY_ST_EXT_REG	= 1<<0, /* Bit  0:	Extended Register available */
};

enum {
	PHY_I1_OUI_MSK	= 0x3f<<10, /* Bit 15..10:	Organization Unique ID */
	PHY_I1_MOD_NUM	= 0x3f<<4, /* Bit  9.. 4:	Model Number */
	PHY_I1_REV_MSK	= 0xf, /* Bit  3.. 0:	Revision Number */
};

/* different Broadcom PHY Ids */
enum {
	PHY_BCOM_ID1_A1	= 0x6041,
	PHY_BCOM_ID1_B2 = 0x6043,
	PHY_BCOM_ID1_C0	= 0x6044,
	PHY_BCOM_ID1_C5	= 0x6047,
};

/* different Marvell PHY Ids */
enum {
	PHY_MARV_ID0_VAL= 0x0141, /* Marvell Unique Identifier */
	PHY_MARV_ID1_B0	= 0x0C23, /* Yukon (PHY 88E1011) */
	PHY_MARV_ID1_B2	= 0x0C25, /* Yukon-Plus (PHY 88E1011) */
	PHY_MARV_ID1_C2	= 0x0CC2, /* Yukon-EC (PHY 88E1111) */
	PHY_MARV_ID1_Y2	= 0x0C91, /* Yukon-2 (PHY 88E1112) */
};

/* Advertisement register bits */
enum {
	PHY_AN_NXT_PG	= 1<<15, /* Bit 15:	Request Next Page */
	PHY_AN_ACK	= 1<<14, /* Bit 14:	(ro) Acknowledge Received */
	PHY_AN_RF	= 1<<13, /* Bit 13:	Remote Fault Bits */

	PHY_AN_PAUSE_ASYM = 1<<11,/* Bit 11:	Try for asymmetric */
	PHY_AN_PAUSE_CAP = 1<<10, /* Bit 10:	Try for pause */
	PHY_AN_100BASE4	= 1<<9, /* Bit 9:	Try for 100mbps 4k packets */
	PHY_AN_100FULL	= 1<<8, /* Bit 8:	Try for 100mbps full-duplex */
	PHY_AN_100HALF	= 1<<7, /* Bit 7:	Try for 100mbps half-duplex */
	PHY_AN_10FULL	= 1<<6, /* Bit 6:	Try for 10mbps full-duplex */
	PHY_AN_10HALF	= 1<<5, /* Bit 5:	Try for 10mbps half-duplex */
	PHY_AN_CSMA	= 1<<0, /* Bit 0:	Only selector supported */
	PHY_AN_SEL	= 0x1f, /* Bit 4..0:	Selector Field, 00001=Ethernet*/
	PHY_AN_FULL	= PHY_AN_100FULL | PHY_AN_10FULL | PHY_AN_CSMA,
	PHY_AN_ALL	= PHY_AN_10HALF | PHY_AN_10FULL |
		  	  PHY_AN_100HALF | PHY_AN_100FULL,
};

/* Xmac Specific */
enum {
	PHY_X_AN_NXT_PG	= 1<<15, /* Bit 15:	Request Next Page */
	PHY_X_AN_ACK	= 1<<14, /* Bit 14:	(ro) Acknowledge Received */
	PHY_X_AN_RFB	= 3<<12,/* Bit 13..12:	Remote Fault Bits */

	PHY_X_AN_PAUSE	= 3<<7,/* Bit  8.. 7:	Pause Bits */
	PHY_X_AN_HD	= 1<<6, /* Bit  6:	Half Duplex */
	PHY_X_AN_FD	= 1<<5, /* Bit  5:	Full Duplex */
};

/* Pause Bits (PHY_X_AN_PAUSE and PHY_X_RS_PAUSE) encoding */
enum {
	PHY_X_P_NO_PAUSE	= 0<<7,/* Bit  8..7:	no Pause Mode */
	PHY_X_P_SYM_MD	= 1<<7, /* Bit  8..7:	symmetric Pause Mode */
	PHY_X_P_ASYM_MD	= 2<<7,/* Bit  8..7:	asymmetric Pause Mode */
	PHY_X_P_BOTH_MD	= 3<<7,/* Bit  8..7:	both Pause Mode */
};


/* Broadcom-Specific */
/*****  PHY_BCOM_1000T_CTRL	16 bit r/w	1000Base-T Control Reg *****/
enum {
	PHY_B_1000C_TEST	= 7<<13,/* Bit 15..13:	Test Modes */
	PHY_B_1000C_MSE	= 1<<12, /* Bit 12:	Master/Slave Enable */
	PHY_B_1000C_MSC	= 1<<11, /* Bit 11:	M/S Configuration */
	PHY_B_1000C_RD	= 1<<10, /* Bit 10:	Repeater/DTE */
	PHY_B_1000C_AFD	= 1<<9, /* Bit  9:	Advertise Full Duplex */
	PHY_B_1000C_AHD	= 1<<8, /* Bit  8:	Advertise Half Duplex */
};

/*****  PHY_BCOM_1000T_STAT	16 bit r/o	1000Base-T Status Reg *****/
/*****  PHY_MARV_1000T_STAT	16 bit r/o	1000Base-T Status Reg *****/
enum {
	PHY_B_1000S_MSF	= 1<<15, /* Bit 15:	Master/Slave Fault */
	PHY_B_1000S_MSR	= 1<<14, /* Bit 14:	Master/Slave Result */
	PHY_B_1000S_LRS	= 1<<13, /* Bit 13:	Local Receiver Status */
	PHY_B_1000S_RRS	= 1<<12, /* Bit 12:	Remote Receiver Status */
	PHY_B_1000S_LP_FD	= 1<<11, /* Bit 11:	Link Partner can FD */
	PHY_B_1000S_LP_HD	= 1<<10, /* Bit 10:	Link Partner can HD */
									/* Bit  9..8:	reserved */
	PHY_B_1000S_IEC	= 0xff, /* Bit  7..0:	Idle Error Count */
};

/*****  PHY_BCOM_EXT_STAT	16 bit r/o	Extended Status Register *****/
enum {
	PHY_B_ES_X_FD_CAP	= 1<<15, /* Bit 15:	1000Base-X FD capable */
	PHY_B_ES_X_HD_CAP	= 1<<14, /* Bit 14:	1000Base-X HD capable */
	PHY_B_ES_T_FD_CAP	= 1<<13, /* Bit 13:	1000Base-T FD capable */
	PHY_B_ES_T_HD_CAP	= 1<<12, /* Bit 12:	1000Base-T HD capable */
};

/*****  PHY_BCOM_P_EXT_CTRL	16 bit r/w	PHY Extended Control Reg *****/
enum {
	PHY_B_PEC_MAC_PHY	= 1<<15, /* Bit 15:	10BIT/GMI-Interface */
	PHY_B_PEC_DIS_CROSS	= 1<<14, /* Bit 14:	Disable MDI Crossover */
	PHY_B_PEC_TX_DIS	= 1<<13, /* Bit 13:	Tx output Disabled */
	PHY_B_PEC_INT_DIS	= 1<<12, /* Bit 12:	Interrupts Disabled */
	PHY_B_PEC_F_INT	= 1<<11, /* Bit 11:	Force Interrupt */
	PHY_B_PEC_BY_45	= 1<<10, /* Bit 10:	Bypass 4B5B-Decoder */
	PHY_B_PEC_BY_SCR	= 1<<9, /* Bit  9:	Bypass Scrambler */
	PHY_B_PEC_BY_MLT3	= 1<<8, /* Bit  8:	Bypass MLT3 Encoder */
	PHY_B_PEC_BY_RXA	= 1<<7, /* Bit  7:	Bypass Rx Alignm. */
	PHY_B_PEC_RES_SCR	= 1<<6, /* Bit  6:	Reset Scrambler */
	PHY_B_PEC_EN_LTR	= 1<<5, /* Bit  5:	Ena LED Traffic Mode */
	PHY_B_PEC_LED_ON	= 1<<4, /* Bit  4:	Force LED's on */
	PHY_B_PEC_LED_OFF	= 1<<3, /* Bit  3:	Force LED's off */
	PHY_B_PEC_EX_IPG	= 1<<2, /* Bit  2:	Extend Tx IPG Mode */
	PHY_B_PEC_3_LED	= 1<<1, /* Bit  1:	Three Link LED mode */
	PHY_B_PEC_HIGH_LA	= 1<<0, /* Bit  0:	GMII FIFO Elasticy */
};

/*****  PHY_BCOM_P_EXT_STAT	16 bit r/o	PHY Extended Status Reg *****/
enum {
	PHY_B_PES_CROSS_STAT	= 1<<13, /* Bit 13:	MDI Crossover Status */
	PHY_B_PES_INT_STAT	= 1<<12, /* Bit 12:	Interrupt Status */
	PHY_B_PES_RRS	= 1<<11, /* Bit 11:	Remote Receiver Stat. */
	PHY_B_PES_LRS	= 1<<10, /* Bit 10:	Local Receiver Stat. */
	PHY_B_PES_LOCKED	= 1<<9, /* Bit  9:	Locked */
	PHY_B_PES_LS	= 1<<8, /* Bit  8:	Link Status */
	PHY_B_PES_RF	= 1<<7, /* Bit  7:	Remote Fault */
	PHY_B_PES_CE_ER	= 1<<6, /* Bit  6:	Carrier Ext Error */
	PHY_B_PES_BAD_SSD	= 1<<5, /* Bit  5:	Bad SSD */
	PHY_B_PES_BAD_ESD	= 1<<4, /* Bit  4:	Bad ESD */
	PHY_B_PES_RX_ER	= 1<<3, /* Bit  3:	Receive Error */
	PHY_B_PES_TX_ER	= 1<<2, /* Bit  2:	Transmit Error */
	PHY_B_PES_LOCK_ER	= 1<<1, /* Bit  1:	Lock Error */
	PHY_B_PES_MLT3_ER	= 1<<0, /* Bit  0:	MLT3 code Error */
};

/*  PHY_BCOM_AUNE_ADV	16 bit r/w	Auto-Negotiation Advertisement *****/
/*  PHY_BCOM_AUNE_LP	16 bit r/o	Link Partner Ability Reg *****/
enum {
	PHY_B_AN_RF	= 1<<13, /* Bit 13:	Remote Fault */

	PHY_B_AN_ASP	= 1<<11, /* Bit 11:	Asymmetric Pause */
	PHY_B_AN_PC	= 1<<10, /* Bit 10:	Pause Capable */
};


/*****  PHY_BCOM_FC_CTR		16 bit r/w	False Carrier Counter *****/
enum {
	PHY_B_FC_CTR	= 0xff, /* Bit  7..0:	False Carrier Counter */

/*****  PHY_BCOM_RNO_CTR	16 bit r/w	Receive NOT_OK Counter *****/
	PHY_B_RC_LOC_MSK	= 0xff00, /* Bit 15..8:	Local Rx NOT_OK cnt */
	PHY_B_RC_REM_MSK	= 0x00ff, /* Bit  7..0:	Remote Rx NOT_OK cnt */

/*****  PHY_BCOM_AUX_CTRL	16 bit r/w	Auxiliary Control Reg *****/
	PHY_B_AC_L_SQE		= 1<<15, /* Bit 15:	Low Squelch */
	PHY_B_AC_LONG_PACK	= 1<<14, /* Bit 14:	Rx Long Packets */
	PHY_B_AC_ER_CTRL	= 3<<12,/* Bit 13..12:	Edgerate Control */
									/* Bit 11:	reserved */
	PHY_B_AC_TX_TST	= 1<<10, /* Bit 10:	Tx test bit, always 1 */
									/* Bit  9.. 8:	reserved */
	PHY_B_AC_DIS_PRF	= 1<<7, /* Bit  7:	dis part resp filter */
									/* Bit  6:	reserved */
	PHY_B_AC_DIS_PM	= 1<<5, /* Bit  5:	dis power management */
									/* Bit  4:	reserved */
	PHY_B_AC_DIAG	= 1<<3, /* Bit  3:	Diagnostic Mode */
};

/*****  PHY_BCOM_AUX_STAT	16 bit r/o	Auxiliary Status Reg *****/
enum {
	PHY_B_AS_AN_C	= 1<<15, /* Bit 15:	AutoNeg complete */
	PHY_B_AS_AN_CA	= 1<<14, /* Bit 14:	AN Complete Ack */
	PHY_B_AS_ANACK_D	= 1<<13, /* Bit 13:	AN Ack Detect */
	PHY_B_AS_ANAB_D	= 1<<12, /* Bit 12:	AN Ability Detect */
	PHY_B_AS_NPW	= 1<<11, /* Bit 11:	AN Next Page Wait */
	PHY_B_AS_AN_RES_MSK	= 7<<8,/* Bit 10..8:	AN HDC */
	PHY_B_AS_PDF	= 1<<7, /* Bit  7:	Parallel Detect. Fault */
	PHY_B_AS_RF	= 1<<6, /* Bit  6:	Remote Fault */
	PHY_B_AS_ANP_R	= 1<<5, /* Bit  5:	AN Page Received */
	PHY_B_AS_LP_ANAB	= 1<<4, /* Bit  4:	LP AN Ability */
	PHY_B_AS_LP_NPAB	= 1<<3, /* Bit  3:	LP Next Page Ability */
	PHY_B_AS_LS	= 1<<2, /* Bit  2:	Link Status */
	PHY_B_AS_PRR	= 1<<1, /* Bit  1:	Pause Resolution-Rx */
	PHY_B_AS_PRT	= 1<<0, /* Bit  0:	Pause Resolution-Tx */
};
#define PHY_B_AS_PAUSE_MSK	(PHY_B_AS_PRR | PHY_B_AS_PRT)

/*****  PHY_BCOM_INT_STAT	16 bit r/o	Interrupt Status Reg *****/
/*****  PHY_BCOM_INT_MASK	16 bit r/w	Interrupt Mask Reg *****/
enum {
	PHY_B_IS_PSE	= 1<<14, /* Bit 14:	Pair Swap Error */
	PHY_B_IS_MDXI_SC	= 1<<13, /* Bit 13:	MDIX Status Change */
	PHY_B_IS_HCT	= 1<<12, /* Bit 12:	counter above 32k */
	PHY_B_IS_LCT	= 1<<11, /* Bit 11:	counter above 128 */
	PHY_B_IS_AN_PR	= 1<<10, /* Bit 10:	Page Received */
	PHY_B_IS_NO_HDCL	= 1<<9, /* Bit  9:	No HCD Link */
	PHY_B_IS_NO_HDC	= 1<<8, /* Bit  8:	No HCD */
	PHY_B_IS_NEG_USHDC	= 1<<7, /* Bit  7:	Negotiated Unsup. HCD */
	PHY_B_IS_SCR_S_ER	= 1<<6, /* Bit  6:	Scrambler Sync Error */
	PHY_B_IS_RRS_CHANGE	= 1<<5, /* Bit  5:	Remote Rx Stat Change */
	PHY_B_IS_LRS_CHANGE	= 1<<4, /* Bit  4:	Local Rx Stat Change */
	PHY_B_IS_DUP_CHANGE	= 1<<3, /* Bit  3:	Duplex Mode Change */
	PHY_B_IS_LSP_CHANGE	= 1<<2, /* Bit  2:	Link Speed Change */
	PHY_B_IS_LST_CHANGE	= 1<<1, /* Bit  1:	Link Status Changed */
	PHY_B_IS_CRC_ER	= 1<<0, /* Bit  0:	CRC Error */
};
#define PHY_B_DEF_MSK	\
	(~(PHY_B_IS_PSE | PHY_B_IS_AN_PR | PHY_B_IS_DUP_CHANGE | \
	    PHY_B_IS_LSP_CHANGE | PHY_B_IS_LST_CHANGE))

/* Pause Bits (PHY_B_AN_ASP and PHY_B_AN_PC) encoding */
enum {
	PHY_B_P_NO_PAUSE	= 0<<10,/* Bit 11..10:	no Pause Mode */
	PHY_B_P_SYM_MD	= 1<<10, /* Bit 11..10:	symmetric Pause Mode */
	PHY_B_P_ASYM_MD	= 2<<10,/* Bit 11..10:	asymmetric Pause Mode */
	PHY_B_P_BOTH_MD	= 3<<10,/* Bit 11..10:	both Pause Mode */
};
/*
 * Resolved Duplex mode and Capabilities (Aux Status Summary Reg)
 */
enum {
	PHY_B_RES_1000FD	= 7<<8,/* Bit 10..8:	1000Base-T Full Dup. */
	PHY_B_RES_1000HD	= 6<<8,/* Bit 10..8:	1000Base-T Half Dup. */
};

/** Marvell-Specific */
enum {
	PHY_M_AN_NXT_PG	= 1<<15, /* Request Next Page */
	PHY_M_AN_ACK	= 1<<14, /* (ro)	Acknowledge Received */
	PHY_M_AN_RF	= 1<<13, /* Remote Fault */

	PHY_M_AN_ASP	= 1<<11, /* Asymmetric Pause */
	PHY_M_AN_PC	= 1<<10, /* MAC Pause implemented */
	PHY_M_AN_100_T4	= 1<<9, /* Not cap. 100Base-T4 (always 0) */
	PHY_M_AN_100_FD	= 1<<8, /* Advertise 100Base-TX Full Duplex */
	PHY_M_AN_100_HD	= 1<<7, /* Advertise 100Base-TX Half Duplex */
	PHY_M_AN_10_FD	= 1<<6, /* Advertise 10Base-TX Full Duplex */
	PHY_M_AN_10_HD	= 1<<5, /* Advertise 10Base-TX Half Duplex */
	PHY_M_AN_SEL_MSK =0x1f<<4,	/* Bit  4.. 0: Selector Field Mask */
};

/* special defines for FIBER (88E1011S only) */
enum {
	PHY_M_AN_ASP_X	= 1<<8, /* Asymmetric Pause */
	PHY_M_AN_PC_X	= 1<<7, /* MAC Pause implemented */
	PHY_M_AN_1000X_AHD	= 1<<6, /* Advertise 10000Base-X Half Duplex */
	PHY_M_AN_1000X_AFD	= 1<<5, /* Advertise 10000Base-X Full Duplex */
};

/* Pause Bits (PHY_M_AN_ASP_X and PHY_M_AN_PC_X) encoding */
enum {
	PHY_M_P_NO_PAUSE_X	= 0<<7,/* Bit  8.. 7:	no Pause Mode */
	PHY_M_P_SYM_MD_X	= 1<<7, /* Bit  8.. 7:	symmetric Pause Mode */
	PHY_M_P_ASYM_MD_X	= 2<<7,/* Bit  8.. 7:	asymmetric Pause Mode */
	PHY_M_P_BOTH_MD_X	= 3<<7,/* Bit  8.. 7:	both Pause Mode */
};

/*****  PHY_MARV_1000T_CTRL	16 bit r/w	1000Base-T Control Reg *****/
enum {
	PHY_M_1000C_TEST	= 7<<13,/* Bit 15..13:	Test Modes */
	PHY_M_1000C_MSE	= 1<<12, /* Manual Master/Slave Enable */
	PHY_M_1000C_MSC	= 1<<11, /* M/S Configuration (1=Master) */
	PHY_M_1000C_MPD	= 1<<10, /* Multi-Port Device */
	PHY_M_1000C_AFD	= 1<<9, /* Advertise Full Duplex */
	PHY_M_1000C_AHD	= 1<<8, /* Advertise Half Duplex */
};

/*****  PHY_MARV_PHY_CTRL	16 bit r/w	PHY Specific Ctrl Reg *****/
enum {
	PHY_M_PC_TX_FFD_MSK	= 3<<14,/* Bit 15..14: Tx FIFO Depth Mask */
	PHY_M_PC_RX_FFD_MSK	= 3<<12,/* Bit 13..12: Rx FIFO Depth Mask */
	PHY_M_PC_ASS_CRS_TX	= 1<<11, /* Assert CRS on Transmit */
	PHY_M_PC_FL_GOOD	= 1<<10, /* Force Link Good */
	PHY_M_PC_EN_DET_MSK	= 3<<8,/* Bit  9.. 8: Energy Detect Mask */
	PHY_M_PC_ENA_EXT_D	= 1<<7, /* Enable Ext. Distance (10BT) */
	PHY_M_PC_MDIX_MSK	= 3<<5,/* Bit  6.. 5: MDI/MDIX Config. Mask */
	PHY_M_PC_DIS_125CLK	= 1<<4, /* Disable 125 CLK */
	PHY_M_PC_MAC_POW_UP	= 1<<3, /* MAC Power up */
	PHY_M_PC_SQE_T_ENA	= 1<<2, /* SQE Test Enabled */
	PHY_M_PC_POL_R_DIS	= 1<<1, /* Polarity Reversal Disabled */
	PHY_M_PC_DIS_JABBER	= 1<<0, /* Disable Jabber */
};

enum {
	PHY_M_PC_EN_DET		= 2<<8,	/* Energy Detect (Mode 1) */
	PHY_M_PC_EN_DET_PLUS	= 3<<8, /* Energy Detect Plus (Mode 2) */
};

#define PHY_M_PC_MDI_XMODE(x)	(((x)<<5) & PHY_M_PC_MDIX_MSK)

enum {
	PHY_M_PC_MAN_MDI	= 0, /* 00 = Manual MDI configuration */
	PHY_M_PC_MAN_MDIX	= 1, /* 01 = Manual MDIX configuration */
	PHY_M_PC_ENA_AUTO	= 3, /* 11 = Enable Automatic Crossover */
};

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
enum {
	PHY_M_PC_ENA_DTE_DT	= 1<<15, /* Enable Data Terminal Equ. (DTE) Detect */
	PHY_M_PC_ENA_ENE_DT	= 1<<14, /* Enable Energy Detect (sense & pulse) */
	PHY_M_PC_DIS_NLP_CK	= 1<<13, /* Disable Normal Link Puls (NLP) Check */
	PHY_M_PC_ENA_LIP_NP	= 1<<12, /* Enable Link Partner Next Page Reg. */
	PHY_M_PC_DIS_NLP_GN	= 1<<11, /* Disable Normal Link Puls Generation */

	PHY_M_PC_DIS_SCRAMB	= 1<<9, /* Disable Scrambler */
	PHY_M_PC_DIS_FEFI	= 1<<8, /* Disable Far End Fault Indic. (FEFI) */

	PHY_M_PC_SH_TP_SEL	= 1<<6, /* Shielded Twisted Pair Select */
	PHY_M_PC_RX_FD_MSK	= 3<<2,/* Bit  3.. 2: Rx FIFO Depth Mask */
};

/*****  PHY_MARV_PHY_STAT	16 bit r/o	PHY Specific Status Reg *****/
enum {
	PHY_M_PS_SPEED_MSK	= 3<<14, /* Bit 15..14: Speed Mask */
	PHY_M_PS_SPEED_1000	= 1<<15, /*		10 = 1000 Mbps */
	PHY_M_PS_SPEED_100	= 1<<14, /*		01 =  100 Mbps */
	PHY_M_PS_SPEED_10	= 0,	 /*		00 =   10 Mbps */
	PHY_M_PS_FULL_DUP	= 1<<13, /* Full Duplex */
	PHY_M_PS_PAGE_REC	= 1<<12, /* Page Received */
	PHY_M_PS_SPDUP_RES	= 1<<11, /* Speed & Duplex Resolved */
	PHY_M_PS_LINK_UP	= 1<<10, /* Link Up */
	PHY_M_PS_CABLE_MSK	= 7<<7,  /* Bit  9.. 7: Cable Length Mask */
	PHY_M_PS_MDI_X_STAT	= 1<<6,  /* MDI Crossover Stat (1=MDIX) */
	PHY_M_PS_DOWNS_STAT	= 1<<5,  /* Downshift Status (1=downsh.) */
	PHY_M_PS_ENDET_STAT	= 1<<4,  /* Energy Detect Status (1=act) */
	PHY_M_PS_TX_P_EN	= 1<<3,  /* Tx Pause Enabled */
	PHY_M_PS_RX_P_EN	= 1<<2,  /* Rx Pause Enabled */
	PHY_M_PS_POL_REV	= 1<<1,  /* Polarity Reversed */
	PHY_M_PS_JABBER		= 1<<0,  /* Jabber */
};

#define PHY_M_PS_PAUSE_MSK	(PHY_M_PS_TX_P_EN | PHY_M_PS_RX_P_EN)

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
enum {
	PHY_M_PS_DTE_DETECT	= 1<<15, /* Data Terminal Equipment (DTE) Detected */
	PHY_M_PS_RES_SPEED	= 1<<14, /* Resolved Speed (1=100 Mbps, 0=10 Mbps */
};

enum {
	PHY_M_IS_AN_ERROR	= 1<<15, /* Auto-Negotiation Error */
	PHY_M_IS_LSP_CHANGE	= 1<<14, /* Link Speed Changed */
	PHY_M_IS_DUP_CHANGE	= 1<<13, /* Duplex Mode Changed */
	PHY_M_IS_AN_PR		= 1<<12, /* Page Received */
	PHY_M_IS_AN_COMPL	= 1<<11, /* Auto-Negotiation Completed */
	PHY_M_IS_LST_CHANGE	= 1<<10, /* Link Status Changed */
	PHY_M_IS_SYMB_ERROR	= 1<<9, /* Symbol Error */
	PHY_M_IS_FALSE_CARR	= 1<<8, /* False Carrier */
	PHY_M_IS_FIFO_ERROR	= 1<<7, /* FIFO Overflow/Underrun Error */
	PHY_M_IS_MDI_CHANGE	= 1<<6, /* MDI Crossover Changed */
	PHY_M_IS_DOWNSH_DET	= 1<<5, /* Downshift Detected */
	PHY_M_IS_END_CHANGE	= 1<<4, /* Energy Detect Changed */

	PHY_M_IS_DTE_CHANGE	= 1<<2, /* DTE Power Det. Status Changed */
	PHY_M_IS_POL_CHANGE	= 1<<1, /* Polarity Changed */
	PHY_M_IS_JABBER		= 1<<0, /* Jabber */

	PHY_M_IS_DEF_MSK	= PHY_M_IS_AN_ERROR | PHY_M_IS_LSP_CHANGE |
				  PHY_M_IS_LST_CHANGE | PHY_M_IS_FIFO_ERROR,

	PHY_M_IS_AN_MSK		= PHY_M_IS_AN_ERROR | PHY_M_IS_AN_COMPL,
};

/*****  PHY_MARV_EXT_CTRL	16 bit r/w	Ext. PHY Specific Ctrl *****/
enum {
	PHY_M_EC_ENA_BC_EXT = 1<<15, /* Enable Block Carr. Ext. (88E1111 only) */
	PHY_M_EC_ENA_LIN_LB = 1<<14, /* Enable Line Loopback (88E1111 only) */

	PHY_M_EC_DIS_LINK_P = 1<<12, /* Disable Link Pulses (88E1111 only) */
	PHY_M_EC_M_DSC_MSK  = 3<<10, /* Bit 11..10:	Master Downshift Counter */
					/* (88E1011 only) */
	PHY_M_EC_S_DSC_MSK	= 3<<8,/* Bit  9.. 8:	Slave  Downshift Counter */
				       /* (88E1011 only) */
	PHY_M_EC_M_DSC_MSK2	= 7<<9,/* Bit 11.. 9:	Master Downshift Counter */
					/* (88E1111 only) */
	PHY_M_EC_DOWN_S_ENA	= 1<<8, /* Downshift Enable (88E1111 only) */
					/* !!! Errata in spec. (1 = disable) */
	PHY_M_EC_RX_TIM_CT	= 1<<7, /* RGMII Rx Timing Control*/
	PHY_M_EC_MAC_S_MSK	= 7<<4,/* Bit  6.. 4:	Def. MAC interface speed */
	PHY_M_EC_FIB_AN_ENA	= 1<<3, /* Fiber Auto-Neg. Enable (88E1011S only) */
	PHY_M_EC_DTE_D_ENA	= 1<<2, /* DTE Detect Enable (88E1111 only) */
	PHY_M_EC_TX_TIM_CT	= 1<<1, /* RGMII Tx Timing Control */
	PHY_M_EC_TRANS_DIS	= 1<<0, /* Transmitter Disable (88E1111 only) */};

#define PHY_M_EC_M_DSC(x)	((x)<<10) /* 00=1x; 01=2x; 10=3x; 11=4x */
#define PHY_M_EC_S_DSC(x)	((x)<<8) /* 00=dis; 01=1x; 10=2x; 11=3x */
#define PHY_M_EC_MAC_S(x)	((x)<<4) /* 01X=0; 110=2.5; 111=25 (MHz) */

#define PHY_M_EC_M_DSC_2(x)	((x)<<9) /* 000=1x; 001=2x; 010=3x; 011=4x */
											/* 100=5x; 101=6x; 110=7x; 111=8x */
enum {
	MAC_TX_CLK_0_MHZ	= 2,
	MAC_TX_CLK_2_5_MHZ	= 6,
	MAC_TX_CLK_25_MHZ 	= 7,
};

/*****  PHY_MARV_LED_CTRL	16 bit r/w	LED Control Reg *****/
enum {
	PHY_M_LEDC_DIS_LED	= 1<<15, /* Disable LED */
	PHY_M_LEDC_PULS_MSK	= 7<<12,/* Bit 14..12: Pulse Stretch Mask */
	PHY_M_LEDC_F_INT	= 1<<11, /* Force Interrupt */
	PHY_M_LEDC_BL_R_MSK	= 7<<8,/* Bit 10.. 8: Blink Rate Mask */
	PHY_M_LEDC_DP_C_LSB	= 1<<7, /* Duplex Control (LSB, 88E1111 only) */
	PHY_M_LEDC_TX_C_LSB	= 1<<6, /* Tx Control (LSB, 88E1111 only) */
	PHY_M_LEDC_LK_C_MSK	= 7<<3,/* Bit  5.. 3: Link Control Mask */
					/* (88E1111 only) */
};

enum {
	PHY_M_LEDC_LINK_MSK	= 3<<3,/* Bit  4.. 3: Link Control Mask */
									/* (88E1011 only) */
	PHY_M_LEDC_DP_CTRL	= 1<<2, /* Duplex Control */
	PHY_M_LEDC_DP_C_MSB	= 1<<2, /* Duplex Control (MSB, 88E1111 only) */
	PHY_M_LEDC_RX_CTRL	= 1<<1, /* Rx Activity / Link */
	PHY_M_LEDC_TX_CTRL	= 1<<0, /* Tx Activity / Link */
	PHY_M_LEDC_TX_C_MSB	= 1<<0, /* Tx Control (MSB, 88E1111 only) */
};

#define PHY_M_LED_PULS_DUR(x)	(((x)<<12) & PHY_M_LEDC_PULS_MSK)

enum {
	PULS_NO_STR	= 0,/* no pulse stretching */
	PULS_21MS	= 1,/* 21 ms to 42 ms */
	PULS_42MS	= 2,/* 42 ms to 84 ms */
	PULS_84MS	= 3,/* 84 ms to 170 ms */
	PULS_170MS	= 4,/* 170 ms to 340 ms */
	PULS_340MS	= 5,/* 340 ms to 670 ms */
	PULS_670MS	= 6,/* 670 ms to 1.3 s */
	PULS_1300MS	= 7,/* 1.3 s to 2.7 s */
};

#define PHY_M_LED_BLINK_RT(x)	(((x)<<8) & PHY_M_LEDC_BL_R_MSK)

enum {
	BLINK_42MS	= 0,/* 42 ms */
	BLINK_84MS	= 1,/* 84 ms */
	BLINK_170MS	= 2,/* 170 ms */
	BLINK_340MS	= 3,/* 340 ms */
	BLINK_670MS	= 4,/* 670 ms */
};

/*****  PHY_MARV_LED_OVER	16 bit r/w	Manual LED Override Reg *****/
#define PHY_M_LED_MO_SGMII(x)	((x)<<14) /* Bit 15..14:  SGMII AN Timer */
										/* Bit 13..12:	reserved */
#define PHY_M_LED_MO_DUP(x)	((x)<<10) /* Bit 11..10:  Duplex */
#define PHY_M_LED_MO_10(x)	((x)<<8) /* Bit  9.. 8:  Link 10 */
#define PHY_M_LED_MO_100(x)	((x)<<6) /* Bit  7.. 6:  Link 100 */
#define PHY_M_LED_MO_1000(x)	((x)<<4) /* Bit  5.. 4:  Link 1000 */
#define PHY_M_LED_MO_RX(x)	((x)<<2) /* Bit  3.. 2:  Rx */
#define PHY_M_LED_MO_TX(x)	((x)<<0) /* Bit  1.. 0:  Tx */

enum {
	MO_LED_NORM	= 0,
	MO_LED_BLINK	= 1,
	MO_LED_OFF	= 2,
	MO_LED_ON	= 3,
};

/*****  PHY_MARV_EXT_CTRL_2	16 bit r/w	Ext. PHY Specific Ctrl 2 *****/
enum {
	PHY_M_EC2_FI_IMPED	= 1<<6, /* Fiber Input  Impedance */
	PHY_M_EC2_FO_IMPED	= 1<<5, /* Fiber Output Impedance */
	PHY_M_EC2_FO_M_CLK	= 1<<4, /* Fiber Mode Clock Enable */
	PHY_M_EC2_FO_BOOST	= 1<<3, /* Fiber Output Boost */
	PHY_M_EC2_FO_AM_MSK	= 7,/* Bit  2.. 0:	Fiber Output Amplitude */
};

/*****  PHY_MARV_EXT_P_STAT 16 bit r/w	Ext. PHY Specific Status *****/
enum {
	PHY_M_FC_AUTO_SEL	= 1<<15, /* Fiber/Copper Auto Sel. Dis. */
	PHY_M_FC_AN_REG_ACC	= 1<<14, /* Fiber/Copper AN Reg. Access */
	PHY_M_FC_RESOLUTION	= 1<<13, /* Fiber/Copper Resolution */
	PHY_M_SER_IF_AN_BP	= 1<<12, /* Ser. IF AN Bypass Enable */
	PHY_M_SER_IF_BP_ST	= 1<<11, /* Ser. IF AN Bypass Status */
	PHY_M_IRQ_POLARITY	= 1<<10, /* IRQ polarity */
	PHY_M_DIS_AUT_MED	= 1<<9, /* Disable Aut. Medium Reg. Selection */
									/* (88E1111 only) */
								/* Bit  9.. 4: reserved (88E1011 only) */
	PHY_M_UNDOC1	= 1<<7, /* undocumented bit !! */
	PHY_M_DTE_POW_STAT	= 1<<4, /* DTE Power Status (88E1111 only) */
	PHY_M_MODE_MASK	= 0xf, /* Bit  3.. 0: copy of HWCFG MODE[3:0] */
};

/*****  PHY_MARV_CABLE_DIAG	16 bit r/o	Cable Diagnostic Reg *****/
enum {
	PHY_M_CABD_ENA_TEST	= 1<<15, /* Enable Test (Page 0) */
	PHY_M_CABD_DIS_WAIT	= 1<<15, /* Disable Waiting Period (Page 1) */
					/* (88E1111 only) */
	PHY_M_CABD_STAT_MSK	= 3<<13, /* Bit 14..13: Status Mask */
	PHY_M_CABD_AMPL_MSK	= 0x1f<<8,/* Bit 12.. 8: Amplitude Mask */
					/* (88E1111 only) */
	PHY_M_CABD_DIST_MSK	= 0xff, /* Bit  7.. 0: Distance Mask */
};

/* values for Cable Diagnostic Status (11=fail; 00=OK; 10=open; 01=short) */
enum {
	CABD_STAT_NORMAL= 0,
	CABD_STAT_SHORT	= 1,
	CABD_STAT_OPEN	= 2,
	CABD_STAT_FAIL	= 3,
};

/* for 10/100 Fast Ethernet PHY (88E3082 only) */
/*****  PHY_MARV_FE_LED_PAR		16 bit r/w	LED Parallel Select Reg. *****/
									/* Bit 15..12: reserved (used internally) */
enum {
	PHY_M_FELP_LED2_MSK = 0xf<<8,	/* Bit 11.. 8: LED2 Mask (LINK) */
	PHY_M_FELP_LED1_MSK = 0xf<<4,	/* Bit  7.. 4: LED1 Mask (ACT) */
	PHY_M_FELP_LED0_MSK = 0xf, /* Bit  3.. 0: LED0 Mask (SPEED) */
};

#define PHY_M_FELP_LED2_CTRL(x)	(((x)<<8) & PHY_M_FELP_LED2_MSK)
#define PHY_M_FELP_LED1_CTRL(x)	(((x)<<4) & PHY_M_FELP_LED1_MSK)
#define PHY_M_FELP_LED0_CTRL(x)	(((x)<<0) & PHY_M_FELP_LED0_MSK)

enum {
	LED_PAR_CTRL_COLX	= 0x00,
	LED_PAR_CTRL_ERROR	= 0x01,
	LED_PAR_CTRL_DUPLEX	= 0x02,
	LED_PAR_CTRL_DP_COL	= 0x03,
	LED_PAR_CTRL_SPEED	= 0x04,
	LED_PAR_CTRL_LINK	= 0x05,
	LED_PAR_CTRL_TX		= 0x06,
	LED_PAR_CTRL_RX		= 0x07,
	LED_PAR_CTRL_ACT	= 0x08,
	LED_PAR_CTRL_LNK_RX	= 0x09,
	LED_PAR_CTRL_LNK_AC	= 0x0a,
	LED_PAR_CTRL_ACT_BL	= 0x0b,
	LED_PAR_CTRL_TX_BL	= 0x0c,
	LED_PAR_CTRL_RX_BL	= 0x0d,
	LED_PAR_CTRL_COL_BL	= 0x0e,
	LED_PAR_CTRL_INACT	= 0x0f
};

/*****,PHY_MARV_FE_SPEC_2		16 bit r/w	Specific Control Reg. 2 *****/
enum {
	PHY_M_FESC_DIS_WAIT	= 1<<2, /* Disable TDR Waiting Period */
	PHY_M_FESC_ENA_MCLK	= 1<<1, /* Enable MAC Rx Clock in sleep mode */
	PHY_M_FESC_SEL_CL_A	= 1<<0, /* Select Class A driver (100B-TX) */
};


/*****  PHY_MARV_PHY_CTRL (page 3)		16 bit r/w	LED Control Reg. *****/
enum {
	PHY_M_LEDC_LOS_MSK	= 0xf<<12,/* Bit 15..12: LOS LED Ctrl. Mask */
	PHY_M_LEDC_INIT_MSK	= 0xf<<8, /* Bit 11.. 8: INIT LED Ctrl. Mask */
	PHY_M_LEDC_STA1_MSK	= 0xf<<4,/* Bit  7.. 4: STAT1 LED Ctrl. Mask */
	PHY_M_LEDC_STA0_MSK	= 0xf, /* Bit  3.. 0: STAT0 LED Ctrl. Mask */
};

#define PHY_M_LEDC_LOS_CTRL(x)	(((x)<<12) & PHY_M_LEDC_LOS_MSK)
#define PHY_M_LEDC_INIT_CTRL(x)	(((x)<<8) & PHY_M_LEDC_INIT_MSK)
#define PHY_M_LEDC_STA1_CTRL(x)	(((x)<<4) & PHY_M_LEDC_STA1_MSK)
#define PHY_M_LEDC_STA0_CTRL(x)	(((x)<<0) & PHY_M_LEDC_STA0_MSK)

/* GMAC registers  */
/* Port Registers */
enum {
	GM_GP_STAT	= 0x0000,	/* 16 bit r/o	General Purpose Status */
	GM_GP_CTRL	= 0x0004,	/* 16 bit r/w	General Purpose Control */
	GM_TX_CTRL	= 0x0008,	/* 16 bit r/w	Transmit Control Reg. */
	GM_RX_CTRL	= 0x000c,	/* 16 bit r/w	Receive Control Reg. */
	GM_TX_FLOW_CTRL	= 0x0010,	/* 16 bit r/w	Transmit Flow-Control */
	GM_TX_PARAM	= 0x0014,	/* 16 bit r/w	Transmit Parameter Reg. */
	GM_SERIAL_MODE	= 0x0018,	/* 16 bit r/w	Serial Mode Register */
/* Source Address Registers */
	GM_SRC_ADDR_1L	= 0x001c,	/* 16 bit r/w	Source Address 1 (low) */
	GM_SRC_ADDR_1M	= 0x0020,	/* 16 bit r/w	Source Address 1 (middle) */
	GM_SRC_ADDR_1H	= 0x0024,	/* 16 bit r/w	Source Address 1 (high) */
	GM_SRC_ADDR_2L	= 0x0028,	/* 16 bit r/w	Source Address 2 (low) */
	GM_SRC_ADDR_2M	= 0x002c,	/* 16 bit r/w	Source Address 2 (middle) */
	GM_SRC_ADDR_2H	= 0x0030,	/* 16 bit r/w	Source Address 2 (high) */

/* Multicast Address Hash Registers */
	GM_MC_ADDR_H1	= 0x0034,	/* 16 bit r/w	Multicast Address Hash 1 */
	GM_MC_ADDR_H2	= 0x0038,	/* 16 bit r/w	Multicast Address Hash 2 */
	GM_MC_ADDR_H3	= 0x003c,	/* 16 bit r/w	Multicast Address Hash 3 */
	GM_MC_ADDR_H4	= 0x0040,	/* 16 bit r/w	Multicast Address Hash 4 */

/* Interrupt Source Registers */
	GM_TX_IRQ_SRC	= 0x0044,	/* 16 bit r/o	Tx Overflow IRQ Source */
	GM_RX_IRQ_SRC	= 0x0048,	/* 16 bit r/o	Rx Overflow IRQ Source */
	GM_TR_IRQ_SRC	= 0x004c,	/* 16 bit r/o	Tx/Rx Over. IRQ Source */

/* Interrupt Mask Registers */
	GM_TX_IRQ_MSK	= 0x0050,	/* 16 bit r/w	Tx Overflow IRQ Mask */
	GM_RX_IRQ_MSK	= 0x0054,	/* 16 bit r/w	Rx Overflow IRQ Mask */
	GM_TR_IRQ_MSK	= 0x0058,	/* 16 bit r/w	Tx/Rx Over. IRQ Mask */

/* Serial Management Interface (SMI) Registers */
	GM_SMI_CTRL	= 0x0080,	/* 16 bit r/w	SMI Control Register */
	GM_SMI_DATA	= 0x0084,	/* 16 bit r/w	SMI Data Register */
	GM_PHY_ADDR	= 0x0088,	/* 16 bit r/w	GPHY Address Register */
};

/* MIB Counters */
#define GM_MIB_CNT_BASE	0x0100		/* Base Address of MIB Counters */
#define GM_MIB_CNT_SIZE	44		/* Number of MIB Counters */

/*
 * MIB Counters base address definitions (low word) -
 * use offset 4 for access to high word	(32 bit r/o)
 */
enum {
	GM_RXF_UC_OK  = GM_MIB_CNT_BASE + 0,	/* Unicast Frames Received OK */
	GM_RXF_BC_OK	= GM_MIB_CNT_BASE + 8,	/* Broadcast Frames Received OK */
	GM_RXF_MPAUSE	= GM_MIB_CNT_BASE + 16,	/* Pause MAC Ctrl Frames Received */
	GM_RXF_MC_OK	= GM_MIB_CNT_BASE + 24,	/* Multicast Frames Received OK */
	GM_RXF_FCS_ERR	= GM_MIB_CNT_BASE + 32,	/* Rx Frame Check Seq. Error */
	/* GM_MIB_CNT_BASE + 40:	reserved */
	GM_RXO_OK_LO	= GM_MIB_CNT_BASE + 48,	/* Octets Received OK Low */
	GM_RXO_OK_HI	= GM_MIB_CNT_BASE + 56,	/* Octets Received OK High */
	GM_RXO_ERR_LO	= GM_MIB_CNT_BASE + 64,	/* Octets Received Invalid Low */
	GM_RXO_ERR_HI	= GM_MIB_CNT_BASE + 72,	/* Octets Received Invalid High */
	GM_RXF_SHT	= GM_MIB_CNT_BASE + 80,	/* Frames <64 Byte Received OK */
	GM_RXE_FRAG	= GM_MIB_CNT_BASE + 88,	/* Frames <64 Byte Received with FCS Err */
	GM_RXF_64B	= GM_MIB_CNT_BASE + 96,	/* 64 Byte Rx Frame */
	GM_RXF_127B	= GM_MIB_CNT_BASE + 104,	/* 65-127 Byte Rx Frame */
	GM_RXF_255B	= GM_MIB_CNT_BASE + 112,	/* 128-255 Byte Rx Frame */
	GM_RXF_511B	= GM_MIB_CNT_BASE + 120,	/* 256-511 Byte Rx Frame */
	GM_RXF_1023B	= GM_MIB_CNT_BASE + 128,	/* 512-1023 Byte Rx Frame */
	GM_RXF_1518B	= GM_MIB_CNT_BASE + 136,	/* 1024-1518 Byte Rx Frame */
	GM_RXF_MAX_SZ	= GM_MIB_CNT_BASE + 144,	/* 1519-MaxSize Byte Rx Frame */
	GM_RXF_LNG_ERR	= GM_MIB_CNT_BASE + 152,	/* Rx Frame too Long Error */
	GM_RXF_JAB_PKT	= GM_MIB_CNT_BASE + 160,	/* Rx Jabber Packet Frame */
	/* GM_MIB_CNT_BASE + 168:	reserved */
	GM_RXE_FIFO_OV	= GM_MIB_CNT_BASE + 176,	/* Rx FIFO overflow Event */
	/* GM_MIB_CNT_BASE + 184:	reserved */
	GM_TXF_UC_OK	= GM_MIB_CNT_BASE + 192,	/* Unicast Frames Xmitted OK */
	GM_TXF_BC_OK	= GM_MIB_CNT_BASE + 200,	/* Broadcast Frames Xmitted OK */
	GM_TXF_MPAUSE	= GM_MIB_CNT_BASE + 208,	/* Pause MAC Ctrl Frames Xmitted */
	GM_TXF_MC_OK	= GM_MIB_CNT_BASE + 216,	/* Multicast Frames Xmitted OK */
	GM_TXO_OK_LO	= GM_MIB_CNT_BASE + 224,	/* Octets Transmitted OK Low */
	GM_TXO_OK_HI	= GM_MIB_CNT_BASE + 232,	/* Octets Transmitted OK High */
	GM_TXF_64B	= GM_MIB_CNT_BASE + 240,	/* 64 Byte Tx Frame */
	GM_TXF_127B	= GM_MIB_CNT_BASE + 248,	/* 65-127 Byte Tx Frame */
	GM_TXF_255B	= GM_MIB_CNT_BASE + 256,	/* 128-255 Byte Tx Frame */
	GM_TXF_511B	= GM_MIB_CNT_BASE + 264,	/* 256-511 Byte Tx Frame */
	GM_TXF_1023B	= GM_MIB_CNT_BASE + 272,	/* 512-1023 Byte Tx Frame */
	GM_TXF_1518B	= GM_MIB_CNT_BASE + 280,	/* 1024-1518 Byte Tx Frame */
	GM_TXF_MAX_SZ	= GM_MIB_CNT_BASE + 288,	/* 1519-MaxSize Byte Tx Frame */

	GM_TXF_COL	= GM_MIB_CNT_BASE + 304,	/* Tx Collision */
	GM_TXF_LAT_COL	= GM_MIB_CNT_BASE + 312,	/* Tx Late Collision */
	GM_TXF_ABO_COL	= GM_MIB_CNT_BASE + 320,	/* Tx aborted due to Exces. Col. */
	GM_TXF_MUL_COL	= GM_MIB_CNT_BASE + 328,	/* Tx Multiple Collision */
	GM_TXF_SNG_COL	= GM_MIB_CNT_BASE + 336,	/* Tx Single Collision */
	GM_TXE_FIFO_UR	= GM_MIB_CNT_BASE + 344,	/* Tx FIFO Underrun Event */
};

/* GMAC Bit Definitions */
/*	GM_GP_STAT	16 bit r/o	General Purpose Status Register */
enum {
	GM_GPSR_SPEED		= 1<<15, /* Bit 15:	Port Speed (1 = 100 Mbps) */
	GM_GPSR_DUPLEX		= 1<<14, /* Bit 14:	Duplex Mode (1 = Full) */
	GM_GPSR_FC_TX_DIS	= 1<<13, /* Bit 13:	Tx Flow-Control Mode Disabled */
	GM_GPSR_LINK_UP		= 1<<12, /* Bit 12:	Link Up Status */
	GM_GPSR_PAUSE		= 1<<11, /* Bit 11:	Pause State */
	GM_GPSR_TX_ACTIVE	= 1<<10, /* Bit 10:	Tx in Progress */
	GM_GPSR_EXC_COL		= 1<<9,	/* Bit  9:	Excessive Collisions Occured */
	GM_GPSR_LAT_COL		= 1<<8,	/* Bit  8:	Late Collisions Occured */

	GM_GPSR_PHY_ST_CH	= 1<<5,	/* Bit  5:	PHY Status Change */
	GM_GPSR_GIG_SPEED	= 1<<4,	/* Bit  4:	Gigabit Speed (1 = 1000 Mbps) */
	GM_GPSR_PART_MODE	= 1<<3,	/* Bit  3:	Partition mode */
	GM_GPSR_FC_RX_DIS	= 1<<2,	/* Bit  2:	Rx Flow-Control Mode Disabled */
	GM_GPSR_PROM_EN		= 1<<1,	/* Bit  1:	Promiscuous Mode Enabled */
};

/*	GM_GP_CTRL	16 bit r/w	General Purpose Control Register */
enum {
	GM_GPCR_PROM_ENA	= 1<<14,	/* Bit 14:	Enable Promiscuous Mode */
	GM_GPCR_FC_TX_DIS	= 1<<13, /* Bit 13:	Disable Tx Flow-Control Mode */
	GM_GPCR_TX_ENA		= 1<<12, /* Bit 12:	Enable Transmit */
	GM_GPCR_RX_ENA		= 1<<11, /* Bit 11:	Enable Receive */
	GM_GPCR_BURST_ENA	= 1<<10, /* Bit 10:	Enable Burst Mode */
	GM_GPCR_LOOP_ENA	= 1<<9,	/* Bit  9:	Enable MAC Loopback Mode */
	GM_GPCR_PART_ENA	= 1<<8,	/* Bit  8:	Enable Partition Mode */
	GM_GPCR_GIGS_ENA	= 1<<7,	/* Bit  7:	Gigabit Speed (1000 Mbps) */
	GM_GPCR_FL_PASS		= 1<<6,	/* Bit  6:	Force Link Pass */
	GM_GPCR_DUP_FULL	= 1<<5,	/* Bit  5:	Full Duplex Mode */
	GM_GPCR_FC_RX_DIS	= 1<<4,	/* Bit  4:	Disable Rx Flow-Control Mode */
	GM_GPCR_SPEED_100	= 1<<3,   /* Bit  3:	Port Speed 100 Mbps */
	GM_GPCR_AU_DUP_DIS	= 1<<2,	/* Bit  2:	Disable Auto-Update Duplex */
	GM_GPCR_AU_FCT_DIS	= 1<<1,	/* Bit  1:	Disable Auto-Update Flow-C. */
	GM_GPCR_AU_SPD_DIS	= 1<<0,	/* Bit  0:	Disable Auto-Update Speed */
};

#define GM_GPCR_SPEED_1000	(GM_GPCR_GIGS_ENA | GM_GPCR_SPEED_100)
#define GM_GPCR_AU_ALL_DIS	(GM_GPCR_AU_DUP_DIS | GM_GPCR_AU_FCT_DIS|GM_GPCR_AU_SPD_DIS)

/*	GM_TX_CTRL			16 bit r/w	Transmit Control Register */
enum {
	GM_TXCR_FORCE_JAM	= 1<<15, /* Bit 15:	Force Jam / Flow-Control */
	GM_TXCR_CRC_DIS		= 1<<14, /* Bit 14:	Disable insertion of CRC */
	GM_TXCR_PAD_DIS		= 1<<13, /* Bit 13:	Disable padding of packets */
	GM_TXCR_COL_THR_MSK	= 1<<10, /* Bit 12..10:	Collision Threshold */
};

#define TX_COL_THR(x)		(((x)<<10) & GM_TXCR_COL_THR_MSK)
#define TX_COL_DEF		0x04

/*	GM_RX_CTRL			16 bit r/w	Receive Control Register */
enum {
	GM_RXCR_UCF_ENA	= 1<<15, /* Bit 15:	Enable Unicast filtering */
	GM_RXCR_MCF_ENA	= 1<<14, /* Bit 14:	Enable Multicast filtering */
	GM_RXCR_CRC_DIS	= 1<<13, /* Bit 13:	Remove 4-byte CRC */
	GM_RXCR_PASS_FC	= 1<<12, /* Bit 12:	Pass FC packets to FIFO */
};

/*	GM_TX_PARAM		16 bit r/w	Transmit Parameter Register */
enum {
	GM_TXPA_JAMLEN_MSK	= 0x03<<14,	/* Bit 15..14:	Jam Length */
	GM_TXPA_JAMIPG_MSK	= 0x1f<<9,	/* Bit 13..9:	Jam IPG */
	GM_TXPA_JAMDAT_MSK	= 0x1f<<4,	/* Bit  8..4:	IPG Jam to Data */

	TX_JAM_LEN_DEF		= 0x03,
	TX_JAM_IPG_DEF		= 0x0b,
	TX_IPG_JAM_DEF		= 0x1c,
};

#define TX_JAM_LEN_VAL(x)	(((x)<<14) & GM_TXPA_JAMLEN_MSK)
#define TX_JAM_IPG_VAL(x)	(((x)<<9)  & GM_TXPA_JAMIPG_MSK)
#define TX_IPG_JAM_DATA(x)	(((x)<<4)  & GM_TXPA_JAMDAT_MSK)


/*	GM_SERIAL_MODE			16 bit r/w	Serial Mode Register */
enum {
	GM_SMOD_DATABL_MSK	= 0x1f<<11, /* Bit 15..11:	Data Blinder (r/o) */
	GM_SMOD_LIMIT_4		= 1<<10, /* Bit 10:	4 consecutive Tx trials */
	GM_SMOD_VLAN_ENA	= 1<<9,	/* Bit  9:	Enable VLAN  (Max. Frame Len) */
	GM_SMOD_JUMBO_ENA	= 1<<8,	/* Bit  8:	Enable Jumbo (Max. Frame Len) */
	 GM_SMOD_IPG_MSK	= 0x1f	/* Bit 4..0:	Inter-Packet Gap (IPG) */
};

#define DATA_BLIND_VAL(x)	(((x)<<11) & GM_SMOD_DATABL_MSK)
#define DATA_BLIND_DEF		0x04

#define IPG_DATA_VAL(x)		(x & GM_SMOD_IPG_MSK)
#define IPG_DATA_DEF		0x1e

/*	GM_SMI_CTRL			16 bit r/w	SMI Control Register */
enum {
	GM_SMI_CT_PHY_A_MSK	= 0x1f<<11,/* Bit 15..11:	PHY Device Address */
	GM_SMI_CT_REG_A_MSK	= 0x1f<<6,/* Bit 10.. 6:	PHY Register Address */
	GM_SMI_CT_OP_RD		= 1<<5,	/* Bit  5:	OpCode Read (0=Write)*/
	GM_SMI_CT_RD_VAL	= 1<<4,	/* Bit  4:	Read Valid (Read completed) */
	GM_SMI_CT_BUSY		= 1<<3,	/* Bit  3:	Busy (Operation in progress) */
};

#define GM_SMI_CT_PHY_AD(x)	(((x)<<11) & GM_SMI_CT_PHY_A_MSK)
#define GM_SMI_CT_REG_AD(x)	(((x)<<6) & GM_SMI_CT_REG_A_MSK)

/*	GM_PHY_ADDR				16 bit r/w	GPHY Address Register */
enum {
	GM_PAR_MIB_CLR	= 1<<5,	/* Bit  5:	Set MIB Clear Counter Mode */
	GM_PAR_MIB_TST	= 1<<4,	/* Bit  4:	MIB Load Counter (Test Mode) */
};

/* Receive Frame Status Encoding */
enum {
	GMR_FS_LEN	= 0xffff<<16, /* Bit 31..16:	Rx Frame Length */
	GMR_FS_LEN_SHIFT = 16,
	GMR_FS_VLAN	= 1<<13, /* Bit 13:	VLAN Packet */
	GMR_FS_JABBER	= 1<<12, /* Bit 12:	Jabber Packet */
	GMR_FS_UN_SIZE	= 1<<11, /* Bit 11:	Undersize Packet */
	GMR_FS_MC	= 1<<10, /* Bit 10:	Multicast Packet */
	GMR_FS_BC	= 1<<9, /* Bit  9:	Broadcast Packet */
	GMR_FS_RX_OK	= 1<<8, /* Bit  8:	Receive OK (Good Packet) */
	GMR_FS_GOOD_FC	= 1<<7, /* Bit  7:	Good Flow-Control Packet */
	GMR_FS_BAD_FC	= 1<<6, /* Bit  6:	Bad  Flow-Control Packet */
	GMR_FS_MII_ERR	= 1<<5, /* Bit  5:	MII Error */
	GMR_FS_LONG_ERR	= 1<<4, /* Bit  4:	Too Long Packet */
	GMR_FS_FRAGMENT	= 1<<3, /* Bit  3:	Fragment */

	GMR_FS_CRC_ERR	= 1<<1, /* Bit  1:	CRC Error */
	GMR_FS_RX_FF_OV	= 1<<0, /* Bit  0:	Rx FIFO Overflow */

/*
 * GMR_FS_ANY_ERR (analogous to XMR_FS_ANY_ERR)
 */
	GMR_FS_ANY_ERR	= GMR_FS_CRC_ERR | GMR_FS_LONG_ERR |
		  	  GMR_FS_MII_ERR | GMR_FS_BAD_FC | GMR_FS_GOOD_FC |
			  GMR_FS_JABBER,
/* Rx GMAC FIFO Flush Mask (default) */
	RX_FF_FL_DEF_MSK = GMR_FS_CRC_ERR | GMR_FS_RX_FF_OV |GMR_FS_MII_ERR |
			   GMR_FS_BAD_FC | GMR_FS_GOOD_FC | GMR_FS_UN_SIZE |
			   GMR_FS_JABBER,
};

/*	RX_GMF_CTRL_T	32 bit	Rx GMAC FIFO Control/Test */
enum {
	GMF_WP_TST_ON	= 1<<14,	/* Write Pointer Test On */
	GMF_WP_TST_OFF	= 1<<13,	/* Write Pointer Test Off */
	GMF_WP_STEP	= 1<<12,	/* Write Pointer Step/Increment */

	GMF_RP_TST_ON	= 1<<10,	/* Read Pointer Test On */
	GMF_RP_TST_OFF	= 1<<9,		/* Read Pointer Test Off */
	GMF_RP_STEP	= 1<<8,		/* Read Pointer Step/Increment */
	GMF_RX_F_FL_ON	= 1<<7,		/* Rx FIFO Flush Mode On */
	GMF_RX_F_FL_OFF	= 1<<6,		/* Rx FIFO Flush Mode Off */
	GMF_CLI_RX_FO	= 1<<5,		/* Clear IRQ Rx FIFO Overrun */
	GMF_CLI_RX_FC	= 1<<4,		/* Clear IRQ Rx Frame Complete */
	GMF_OPER_ON	= 1<<3,		/* Operational Mode On */
	GMF_OPER_OFF	= 1<<2,		/* Operational Mode Off */
	GMF_RST_CLR	= 1<<1,		/* Clear GMAC FIFO Reset */
	GMF_RST_SET	= 1<<0,		/* Set   GMAC FIFO Reset */

	RX_GMF_FL_THR_DEF = 0xa,	/* flush threshold (default) */
};


/*	TX_GMF_CTRL_T	32 bit	Tx GMAC FIFO Control/Test */
enum {
	GMF_WSP_TST_ON	= 1<<18,/* Write Shadow Pointer Test On */
	GMF_WSP_TST_OFF	= 1<<17,/* Write Shadow Pointer Test Off */
	GMF_WSP_STEP	= 1<<16,/* Write Shadow Pointer Step/Increment */

	GMF_CLI_TX_FU	= 1<<6,	/* Clear IRQ Tx FIFO Underrun */
	GMF_CLI_TX_FC	= 1<<5,	/* Clear IRQ Tx Frame Complete */
	GMF_CLI_TX_PE	= 1<<4,	/* Clear IRQ Tx Parity Error */
};

/*	GMAC_TI_ST_CTRL	 8 bit	Time Stamp Timer Ctrl Reg (YUKON only) */
enum {
	GMT_ST_START	= 1<<2,	/* Start Time Stamp Timer */
	GMT_ST_STOP	= 1<<1,	/* Stop  Time Stamp Timer */
	GMT_ST_CLR_IRQ	= 1<<0,	/* Clear Time Stamp Timer IRQ */
};

/*	GMAC_CTRL		32 bit	GMAC Control Reg (YUKON only) */
enum {
	GMC_H_BURST_ON	= 1<<7,	/* Half Duplex Burst Mode On */
	GMC_H_BURST_OFF	= 1<<6,	/* Half Duplex Burst Mode Off */
	GMC_F_LOOPB_ON	= 1<<5,	/* FIFO Loopback On */
	GMC_F_LOOPB_OFF	= 1<<4,	/* FIFO Loopback Off */
	GMC_PAUSE_ON	= 1<<3,	/* Pause On */
	GMC_PAUSE_OFF	= 1<<2,	/* Pause Off */
	GMC_RST_CLR	= 1<<1,	/* Clear GMAC Reset */
	GMC_RST_SET	= 1<<0,	/* Set   GMAC Reset */
};

/*	GPHY_CTRL		32 bit	GPHY Control Reg (YUKON only) */
enum {
	GPC_SEL_BDT	= 1<<28, /* Select Bi-Dir. Transfer for MDC/MDIO */
	GPC_INT_POL_HI	= 1<<27, /* IRQ Polarity is Active HIGH */
	GPC_75_OHM	= 1<<26, /* Use 75 Ohm Termination instead of 50 */
	GPC_DIS_FC	= 1<<25, /* Disable Automatic Fiber/Copper Detection */
	GPC_DIS_SLEEP	= 1<<24, /* Disable Energy Detect */
	GPC_HWCFG_M_3	= 1<<23, /* HWCFG_MODE[3] */
	GPC_HWCFG_M_2	= 1<<22, /* HWCFG_MODE[2] */
	GPC_HWCFG_M_1	= 1<<21, /* HWCFG_MODE[1] */
	GPC_HWCFG_M_0	= 1<<20, /* HWCFG_MODE[0] */
	GPC_ANEG_0	= 1<<19, /* ANEG[0] */
	GPC_ENA_XC	= 1<<18, /* Enable MDI crossover */
	GPC_DIS_125	= 1<<17, /* Disable 125 MHz clock */
	GPC_ANEG_3	= 1<<16, /* ANEG[3] */
	GPC_ANEG_2	= 1<<15, /* ANEG[2] */
	GPC_ANEG_1	= 1<<14, /* ANEG[1] */
	GPC_ENA_PAUSE	= 1<<13, /* Enable Pause (SYM_OR_REM) */
	GPC_PHYADDR_4	= 1<<12, /* Bit 4 of Phy Addr */
	GPC_PHYADDR_3	= 1<<11, /* Bit 3 of Phy Addr */
	GPC_PHYADDR_2	= 1<<10, /* Bit 2 of Phy Addr */
	GPC_PHYADDR_1	= 1<<9,	 /* Bit 1 of Phy Addr */
	GPC_PHYADDR_0	= 1<<8,	 /* Bit 0 of Phy Addr */
						/* Bits  7..2:	reserved */
	GPC_RST_CLR	= 1<<1,	/* Clear GPHY Reset */
	GPC_RST_SET	= 1<<0,	/* Set   GPHY Reset */
};

#define GPC_HWCFG_GMII_COP (GPC_HWCFG_M_3|GPC_HWCFG_M_2 | GPC_HWCFG_M_1 | GPC_HWCFG_M_0)
#define GPC_HWCFG_GMII_FIB (GPC_HWCFG_M_2 | GPC_HWCFG_M_1 | GPC_HWCFG_M_0)
#define GPC_ANEG_ADV_ALL_M  (GPC_ANEG_3 | GPC_ANEG_2 | GPC_ANEG_1 | GPC_ANEG_0)

/* forced speed and duplex mode (don't mix with other ANEG bits) */
#define GPC_FRC10MBIT_HALF	0
#define GPC_FRC10MBIT_FULL	GPC_ANEG_0
#define GPC_FRC100MBIT_HALF	GPC_ANEG_1
#define GPC_FRC100MBIT_FULL	(GPC_ANEG_0 | GPC_ANEG_1)

/* auto-negotiation with limited advertised speeds */
/* mix only with master/slave settings (for copper) */
#define GPC_ADV_1000_HALF	GPC_ANEG_2
#define GPC_ADV_1000_FULL	GPC_ANEG_3
#define GPC_ADV_ALL		(GPC_ANEG_2 | GPC_ANEG_3)

/* master/slave settings */
/* only for copper with 1000 Mbps */
#define GPC_FORCE_MASTER	0
#define GPC_FORCE_SLAVE		GPC_ANEG_0
#define GPC_PREF_MASTER		GPC_ANEG_1
#define GPC_PREF_SLAVE		(GPC_ANEG_1 | GPC_ANEG_0)

/*	GMAC_IRQ_SRC	 8 bit	GMAC Interrupt Source Reg (YUKON only) */
/*	GMAC_IRQ_MSK	 8 bit	GMAC Interrupt Mask   Reg (YUKON only) */
enum {
	GM_IS_TX_CO_OV	= 1<<5,	/* Transmit Counter Overflow IRQ */
	GM_IS_RX_CO_OV	= 1<<4,	/* Receive Counter Overflow IRQ */
	GM_IS_TX_FF_UR	= 1<<3,	/* Transmit FIFO Underrun */
	GM_IS_TX_COMPL	= 1<<2,	/* Frame Transmission Complete */
	GM_IS_RX_FF_OR	= 1<<1,	/* Receive FIFO Overrun */
	GM_IS_RX_COMPL	= 1<<0,	/* Frame Reception Complete */

#define GMAC_DEF_MSK	(GM_IS_RX_FF_OR | GM_IS_TX_FF_UR)

/*	GMAC_LINK_CTRL	16 bit	GMAC Link Control Reg (YUKON only) */
						/* Bits 15.. 2:	reserved */
	GMLC_RST_CLR	= 1<<1,	/* Clear GMAC Link Reset */
	GMLC_RST_SET	= 1<<0,	/* Set   GMAC Link Reset */


/*	WOL_CTRL_STAT	16 bit	WOL Control/Status Reg */
	WOL_CTL_LINK_CHG_OCC		= 1<<15,
	WOL_CTL_MAGIC_PKT_OCC		= 1<<14,
	WOL_CTL_PATTERN_OCC		= 1<<13,
	WOL_CTL_CLEAR_RESULT		= 1<<12,
	WOL_CTL_ENA_PME_ON_LINK_CHG	= 1<<11,
	WOL_CTL_DIS_PME_ON_LINK_CHG	= 1<<10,
	WOL_CTL_ENA_PME_ON_MAGIC_PKT	= 1<<9,
	WOL_CTL_DIS_PME_ON_MAGIC_PKT	= 1<<8,
	WOL_CTL_ENA_PME_ON_PATTERN	= 1<<7,
	WOL_CTL_DIS_PME_ON_PATTERN	= 1<<6,
	WOL_CTL_ENA_LINK_CHG_UNIT	= 1<<5,
	WOL_CTL_DIS_LINK_CHG_UNIT	= 1<<4,
	WOL_CTL_ENA_MAGIC_PKT_UNIT	= 1<<3,
	WOL_CTL_DIS_MAGIC_PKT_UNIT	= 1<<2,
	WOL_CTL_ENA_PATTERN_UNIT	= 1<<1,
	WOL_CTL_DIS_PATTERN_UNIT	= 1<<0,
};

#define WOL_CTL_DEFAULT				\
	(WOL_CTL_DIS_PME_ON_LINK_CHG |	\
	WOL_CTL_DIS_PME_ON_PATTERN |	\
	WOL_CTL_DIS_PME_ON_MAGIC_PKT |	\
	WOL_CTL_DIS_LINK_CHG_UNIT |		\
	WOL_CTL_DIS_PATTERN_UNIT |		\
	WOL_CTL_DIS_MAGIC_PKT_UNIT)

/*	WOL_MATCH_CTL	 8 bit	WOL Match Control Reg */
#define WOL_CTL_PATT_ENA(x)	(1 << (x))


/* XMAC II registers				      */
enum {
	XM_MMU_CMD	= 0x0000, /* 16 bit r/w	MMU Command Register */
	XM_POFF		= 0x0008, /* 32 bit r/w	Packet Offset Register */
	XM_BURST	= 0x000c, /* 32 bit r/w	Burst Register for half duplex*/
	XM_1L_VLAN_TAG	= 0x0010, /* 16 bit r/w	One Level VLAN Tag ID */
	XM_2L_VLAN_TAG	= 0x0014, /* 16 bit r/w	Two Level VLAN Tag ID */
	XM_TX_CMD	= 0x0020, /* 16 bit r/w	Transmit Command Register */
	XM_TX_RT_LIM	= 0x0024, /* 16 bit r/w	Transmit Retry Limit Register */
	XM_TX_STIME	= 0x0028, /* 16 bit r/w	Transmit Slottime Register */
	XM_TX_IPG	= 0x002c, /* 16 bit r/w	Transmit Inter Packet Gap */
	XM_RX_CMD	= 0x0030, /* 16 bit r/w	Receive Command Register */
	XM_PHY_ADDR	= 0x0034, /* 16 bit r/w	PHY Address Register */
	XM_PHY_DATA	= 0x0038, /* 16 bit r/w	PHY Data Register */
	XM_GP_PORT	= 0x0040, /* 32 bit r/w	General Purpose Port Register */
	XM_IMSK		= 0x0044, /* 16 bit r/w	Interrupt Mask Register */
	XM_ISRC		= 0x0048, /* 16 bit r/o	Interrupt Status Register */
	XM_HW_CFG	= 0x004c, /* 16 bit r/w	Hardware Config Register */
	XM_TX_LO_WM	= 0x0060, /* 16 bit r/w	Tx FIFO Low Water Mark */
	XM_TX_HI_WM	= 0x0062, /* 16 bit r/w	Tx FIFO High Water Mark */
	XM_TX_THR	= 0x0064, /* 16 bit r/w	Tx Request Threshold */
	XM_HT_THR	= 0x0066, /* 16 bit r/w	Host Request Threshold */
	XM_PAUSE_DA	= 0x0068, /* NA reg r/w	Pause Destination Address */
	XM_CTL_PARA	= 0x0070, /* 32 bit r/w	Control Parameter Register */
	XM_MAC_OPCODE	= 0x0074, /* 16 bit r/w	Opcode for MAC control frames */
	XM_MAC_PTIME	= 0x0076, /* 16 bit r/w	Pause time for MAC ctrl frames*/
	XM_TX_STAT	= 0x0078, /* 32 bit r/o	Tx Status LIFO Register */

	XM_EXM_START	= 0x0080, /* r/w	Start Address of the EXM Regs */
#define XM_EXM(reg)	(XM_EXM_START + ((reg) << 3))
};

enum {
	XM_SRC_CHK	= 0x0100, /* NA reg r/w	Source Check Address Register */
	XM_SA		= 0x0108, /* NA reg r/w	Station Address Register */
	XM_HSM		= 0x0110, /* 64 bit r/w	Hash Match Address Registers */
	XM_RX_LO_WM	= 0x0118, /* 16 bit r/w	Receive Low Water Mark */
	XM_RX_HI_WM	= 0x011a, /* 16 bit r/w	Receive High Water Mark */
	XM_RX_THR	= 0x011c, /* 32 bit r/w	Receive Request Threshold */
	XM_DEV_ID	= 0x0120, /* 32 bit r/o	Device ID Register */
	XM_MODE		= 0x0124, /* 32 bit r/w	Mode Register */
	XM_LSA		= 0x0128, /* NA reg r/o	Last Source Register */
	XM_TS_READ	= 0x0130, /* 32 bit r/o	Time Stamp Read Register */
	XM_TS_LOAD	= 0x0134, /* 32 bit r/o	Time Stamp Load Value */
	XM_STAT_CMD	= 0x0200, /* 16 bit r/w	Statistics Command Register */
	XM_RX_CNT_EV	= 0x0204, /* 32 bit r/o	Rx Counter Event Register */
	XM_TX_CNT_EV	= 0x0208, /* 32 bit r/o	Tx Counter Event Register */
	XM_RX_EV_MSK	= 0x020c, /* 32 bit r/w	Rx Counter Event Mask */
	XM_TX_EV_MSK	= 0x0210, /* 32 bit r/w	Tx Counter Event Mask */
	XM_TXF_OK	= 0x0280, /* 32 bit r/o	Frames Transmitted OK Conuter */
	XM_TXO_OK_HI	= 0x0284, /* 32 bit r/o	Octets Transmitted OK High Cnt*/
	XM_TXO_OK_LO	= 0x0288, /* 32 bit r/o	Octets Transmitted OK Low Cnt */
	XM_TXF_BC_OK	= 0x028c, /* 32 bit r/o	Broadcast Frames Xmitted OK */
	XM_TXF_MC_OK	= 0x0290, /* 32 bit r/o	Multicast Frames Xmitted OK */
	XM_TXF_UC_OK	= 0x0294, /* 32 bit r/o	Unicast Frames Xmitted OK */
	XM_TXF_LONG	= 0x0298, /* 32 bit r/o	Tx Long Frame Counter */
	XM_TXE_BURST	= 0x029c, /* 32 bit r/o	Tx Burst Event Counter */
	XM_TXF_MPAUSE	= 0x02a0, /* 32 bit r/o	Tx Pause MAC Ctrl Frame Cnt */
	XM_TXF_MCTRL	= 0x02a4, /* 32 bit r/o	Tx MAC Ctrl Frame Counter */
	XM_TXF_SNG_COL	= 0x02a8, /* 32 bit r/o	Tx Single Collision Counter */
	XM_TXF_MUL_COL	= 0x02ac, /* 32 bit r/o	Tx Multiple Collision Counter */
	XM_TXF_ABO_COL	= 0x02b0, /* 32 bit r/o	Tx aborted due to Exces. Col. */
	XM_TXF_LAT_COL	= 0x02b4, /* 32 bit r/o	Tx Late Collision Counter */
	XM_TXF_DEF	= 0x02b8, /* 32 bit r/o	Tx Deferred Frame Counter */
	XM_TXF_EX_DEF	= 0x02bc, /* 32 bit r/o	Tx Excessive Deferall Counter */
	XM_TXE_FIFO_UR	= 0x02c0, /* 32 bit r/o	Tx FIFO Underrun Event Cnt */
	XM_TXE_CS_ERR	= 0x02c4, /* 32 bit r/o	Tx Carrier Sense Error Cnt */
	XM_TXP_UTIL	= 0x02c8, /* 32 bit r/o	Tx Utilization in % */
	XM_TXF_64B	= 0x02d0, /* 32 bit r/o	64 Byte Tx Frame Counter */
	XM_TXF_127B	= 0x02d4, /* 32 bit r/o	65-127 Byte Tx Frame Counter */
	XM_TXF_255B	= 0x02d8, /* 32 bit r/o	128-255 Byte Tx Frame Counter */
	XM_TXF_511B	= 0x02dc, /* 32 bit r/o	256-511 Byte Tx Frame Counter */
	XM_TXF_1023B	= 0x02e0, /* 32 bit r/o	512-1023 Byte Tx Frame Counter*/
	XM_TXF_MAX_SZ	= 0x02e4, /* 32 bit r/o	1024-MaxSize Byte Tx Frame Cnt*/
	XM_RXF_OK	= 0x0300, /* 32 bit r/o	Frames Received OK */
	XM_RXO_OK_HI	= 0x0304, /* 32 bit r/o	Octets Received OK High Cnt */
	XM_RXO_OK_LO	= 0x0308, /* 32 bit r/o	Octets Received OK Low Counter*/
	XM_RXF_BC_OK	= 0x030c, /* 32 bit r/o	Broadcast Frames Received OK */
	XM_RXF_MC_OK	= 0x0310, /* 32 bit r/o	Multicast Frames Received OK */
	XM_RXF_UC_OK	= 0x0314, /* 32 bit r/o	Unicast Frames Received OK */
	XM_RXF_MPAUSE	= 0x0318, /* 32 bit r/o	Rx Pause MAC Ctrl Frame Cnt */
	XM_RXF_MCTRL	= 0x031c, /* 32 bit r/o	Rx MAC Ctrl Frame Counter */
	XM_RXF_INV_MP	= 0x0320, /* 32 bit r/o	Rx invalid Pause Frame Cnt */
	XM_RXF_INV_MOC	= 0x0324, /* 32 bit r/o	Rx Frames with inv. MAC Opcode*/
	XM_RXE_BURST	= 0x0328, /* 32 bit r/o	Rx Burst Event Counter */
	XM_RXE_FMISS	= 0x032c, /* 32 bit r/o	Rx Missed Frames Event Cnt */
	XM_RXF_FRA_ERR	= 0x0330, /* 32 bit r/o	Rx Framing Error Counter */
	XM_RXE_FIFO_OV	= 0x0334, /* 32 bit r/o	Rx FIFO overflow Event Cnt */
	XM_RXF_JAB_PKT	= 0x0338, /* 32 bit r/o	Rx Jabber Packet Frame Cnt */
	XM_RXE_CAR_ERR	= 0x033c, /* 32 bit r/o	Rx Carrier Event Error Cnt */
	XM_RXF_LEN_ERR	= 0x0340, /* 32 bit r/o	Rx in Range Length Error */
	XM_RXE_SYM_ERR	= 0x0344, /* 32 bit r/o	Rx Symbol Error Counter */
	XM_RXE_SHT_ERR	= 0x0348, /* 32 bit r/o	Rx Short Event Error Cnt */
	XM_RXE_RUNT	= 0x034c, /* 32 bit r/o	Rx Runt Event Counter */
	XM_RXF_LNG_ERR	= 0x0350, /* 32 bit r/o	Rx Frame too Long Error Cnt */
	XM_RXF_FCS_ERR	= 0x0354, /* 32 bit r/o	Rx Frame Check Seq. Error Cnt */
	XM_RXF_CEX_ERR	= 0x035c, /* 32 bit r/o	Rx Carrier Ext Error Frame Cnt*/
	XM_RXP_UTIL	= 0x0360, /* 32 bit r/o	Rx Utilization in % */
	XM_RXF_64B	= 0x0368, /* 32 bit r/o	64 Byte Rx Frame Counter */
	XM_RXF_127B	= 0x036c, /* 32 bit r/o	65-127 Byte Rx Frame Counter */
	XM_RXF_255B	= 0x0370, /* 32 bit r/o	128-255 Byte Rx Frame Counter */
	XM_RXF_511B	= 0x0374, /* 32 bit r/o	256-511 Byte Rx Frame Counter */
	XM_RXF_1023B	= 0x0378, /* 32 bit r/o	512-1023 Byte Rx Frame Counter*/
	XM_RXF_MAX_SZ	= 0x037c, /* 32 bit r/o	1024-MaxSize Byte Rx Frame Cnt*/
};

/*	XM_MMU_CMD	16 bit r/w	MMU Command Register */
enum {
	XM_MMU_PHY_RDY	= 1<<12,/* Bit 12:	PHY Read Ready */
	XM_MMU_PHY_BUSY	= 1<<11,/* Bit 11:	PHY Busy */
	XM_MMU_IGN_PF	= 1<<10,/* Bit 10:	Ignore Pause Frame */
	XM_MMU_MAC_LB	= 1<<9,	/* Bit  9:	Enable MAC Loopback */
	XM_MMU_FRC_COL	= 1<<7,	/* Bit  7:	Force Collision */
	XM_MMU_SIM_COL	= 1<<6,	/* Bit  6:	Simulate Collision */
	XM_MMU_NO_PRE	= 1<<5,	/* Bit  5:	No MDIO Preamble */
	XM_MMU_GMII_FD	= 1<<4,	/* Bit  4:	GMII uses Full Duplex */
	XM_MMU_RAT_CTRL	= 1<<3,	/* Bit  3:	Enable Rate Control */
	XM_MMU_GMII_LOOP= 1<<2,	/* Bit  2:	PHY is in Loopback Mode */
	XM_MMU_ENA_RX	= 1<<1,	/* Bit  1:	Enable Receiver */
	XM_MMU_ENA_TX	= 1<<0,	/* Bit  0:	Enable Transmitter */
};


/*	XM_TX_CMD	16 bit r/w	Transmit Command Register */
enum {
	XM_TX_BK2BK	= 1<<6,	/* Bit  6:	Ignor Carrier Sense (Tx Bk2Bk)*/
	XM_TX_ENC_BYP	= 1<<5,	/* Bit  5:	Set Encoder in Bypass Mode */
	XM_TX_SAM_LINE	= 1<<4,	/* Bit  4: (sc)	Start utilization calculation */
	XM_TX_NO_GIG_MD	= 1<<3,	/* Bit  3:	Disable Carrier Extension */
	XM_TX_NO_PRE	= 1<<2,	/* Bit  2:	Disable Preamble Generation */
	XM_TX_NO_CRC	= 1<<1,	/* Bit  1:	Disable CRC Generation */
	XM_TX_AUTO_PAD	= 1<<0,	/* Bit  0:	Enable Automatic Padding */
};

/*	XM_TX_RT_LIM	16 bit r/w	Transmit Retry Limit Register */
#define XM_RT_LIM_MSK	0x1f	/* Bit  4..0:	Tx Retry Limit */


/*	XM_TX_STIME	16 bit r/w	Transmit Slottime Register */
#define XM_STIME_MSK	0x7f	/* Bit  6..0:	Tx Slottime bits */


/*	XM_TX_IPG	16 bit r/w	Transmit Inter Packet Gap */
#define XM_IPG_MSK		0xff	/* Bit  7..0:	IPG value bits */


/*	XM_RX_CMD	16 bit r/w	Receive Command Register */
enum {
	XM_RX_LENERR_OK	= 1<<8,	/* Bit  8	don't set Rx Err bit for */
				/*		inrange error packets */
	XM_RX_BIG_PK_OK	= 1<<7,	/* Bit  7	don't set Rx Err bit for */
				/*		jumbo packets */
	XM_RX_IPG_CAP	= 1<<6,	/* Bit  6	repl. type field with IPG */
	XM_RX_TP_MD	= 1<<5,	/* Bit  5:	Enable transparent Mode */
	XM_RX_STRIP_FCS	= 1<<4,	/* Bit  4:	Enable FCS Stripping */
	XM_RX_SELF_RX	= 1<<3,	/* Bit  3: 	Enable Rx of own packets */
	XM_RX_SAM_LINE	= 1<<2,	/* Bit  2: (sc)	Start utilization calculation */
	XM_RX_STRIP_PAD	= 1<<1,	/* Bit  1:	Strip pad bytes of Rx frames */
	XM_RX_DIS_CEXT	= 1<<0,	/* Bit  0:	Disable carrier ext. check */
};


/*	XM_GP_PORT	32 bit r/w	General Purpose Port Register */
enum {
	XM_GP_ANIP	= 1<<6,	/* Bit  6: (ro)	Auto-Neg. in progress */
	XM_GP_FRC_INT	= 1<<5,	/* Bit  5: (sc)	Force Interrupt */
	XM_GP_RES_MAC	= 1<<3,	/* Bit  3: (sc)	Reset MAC and FIFOs */
	XM_GP_RES_STAT	= 1<<2,	/* Bit  2: (sc)	Reset the statistics module */
	XM_GP_INP_ASS	= 1<<0,	/* Bit  0: (ro) GP Input Pin asserted */
};


/*	XM_IMSK		16 bit r/w	Interrupt Mask Register */
/*	XM_ISRC		16 bit r/o	Interrupt Status Register */
enum {
	XM_IS_LNK_AE	= 1<<14, /* Bit 14:	Link Asynchronous Event */
	XM_IS_TX_ABORT	= 1<<13, /* Bit 13:	Transmit Abort, late Col. etc */
	XM_IS_FRC_INT	= 1<<12, /* Bit 12:	Force INT bit set in GP */
	XM_IS_INP_ASS	= 1<<11,	/* Bit 11:	Input Asserted, GP bit 0 set */
	XM_IS_LIPA_RC	= 1<<10,	/* Bit 10:	Link Partner requests config */
	XM_IS_RX_PAGE	= 1<<9,	/* Bit  9:	Page Received */
	XM_IS_TX_PAGE	= 1<<8,	/* Bit  8:	Next Page Loaded for Transmit */
	XM_IS_AND	= 1<<7,	/* Bit  7:	Auto-Negotiation Done */
	XM_IS_TSC_OV	= 1<<6,	/* Bit  6:	Time Stamp Counter Overflow */
	XM_IS_RXC_OV	= 1<<5,	/* Bit  5:	Rx Counter Event Overflow */
	XM_IS_TXC_OV	= 1<<4,	/* Bit  4:	Tx Counter Event Overflow */
	XM_IS_RXF_OV	= 1<<3,	/* Bit  3:	Receive FIFO Overflow */
	XM_IS_TXF_UR	= 1<<2,	/* Bit  2:	Transmit FIFO Underrun */
	XM_IS_TX_COMP	= 1<<1,	/* Bit  1:	Frame Tx Complete */
	XM_IS_RX_COMP	= 1<<0,	/* Bit  0:	Frame Rx Complete */
};

#define XM_DEF_MSK	(~(XM_IS_INP_ASS | XM_IS_LIPA_RC | XM_IS_RX_PAGE | \
			   XM_IS_AND | XM_IS_RXC_OV | XM_IS_TXC_OV | \
			   XM_IS_RXF_OV | XM_IS_TXF_UR))


/*	XM_HW_CFG	16 bit r/w	Hardware Config Register */
enum {
	XM_HW_GEN_EOP	= 1<<3,	/* Bit  3:	generate End of Packet pulse */
	XM_HW_COM4SIG	= 1<<2,	/* Bit  2:	use Comma Detect for Sig. Det.*/
	XM_HW_GMII_MD	= 1<<0,	/* Bit  0:	GMII Interface selected */
};


/*	XM_TX_LO_WM	16 bit r/w	Tx FIFO Low Water Mark */
/*	XM_TX_HI_WM	16 bit r/w	Tx FIFO High Water Mark */
#define XM_TX_WM_MSK	0x01ff	/* Bit  9.. 0	Tx FIFO Watermark bits */

/*	XM_TX_THR	16 bit r/w	Tx Request Threshold */
/*	XM_HT_THR	16 bit r/w	Host Request Threshold */
/*	XM_RX_THR	16 bit r/w	Rx Request Threshold */
#define XM_THR_MSK		0x03ff	/* Bit 10.. 0	Rx/Tx Request Threshold bits */


/*	XM_TX_STAT	32 bit r/o	Tx Status LIFO Register */
enum {
	XM_ST_VALID	= (1UL<<31),	/* Bit 31:	Status Valid */
	XM_ST_BYTE_CNT	= (0x3fffL<<17),	/* Bit 30..17:	Tx frame Length */
	XM_ST_RETRY_CNT	= (0x1fL<<12),	/* Bit 16..12:	Retry Count */
	XM_ST_EX_COL	= 1<<11,	/* Bit 11:	Excessive Collisions */
	XM_ST_EX_DEF	= 1<<10,	/* Bit 10:	Excessive Deferral */
	XM_ST_BURST	= 1<<9,		/* Bit  9:	p. xmitted in burst md*/
	XM_ST_DEFER	= 1<<8,		/* Bit  8:	packet was defered */
	XM_ST_BC	= 1<<7,		/* Bit  7:	Broadcast packet */
	XM_ST_MC	= 1<<6,		/* Bit  6:	Multicast packet */
	XM_ST_UC	= 1<<5,		/* Bit  5:	Unicast packet */
	XM_ST_TX_UR	= 1<<4,		/* Bit  4:	FIFO Underrun occured */
	XM_ST_CS_ERR	= 1<<3,		/* Bit  3:	Carrier Sense Error */
	XM_ST_LAT_COL	= 1<<2,		/* Bit  2:	Late Collision Error */
	XM_ST_MUL_COL	= 1<<1,		/* Bit  1:	Multiple Collisions */
	XM_ST_SGN_COL	= 1<<0,		/* Bit  0:	Single Collision */
};

/*	XM_RX_LO_WM	16 bit r/w	Receive Low Water Mark */
/*	XM_RX_HI_WM	16 bit r/w	Receive High Water Mark */
#define XM_RX_WM_MSK	0x03ff		/* Bit 11.. 0:	Rx FIFO Watermark bits */


/*	XM_DEV_ID	32 bit r/o	Device ID Register */
#define XM_DEV_OUI	(0x00ffffffUL<<8)	/* Bit 31..8:	Device OUI */
#define XM_DEV_REV	(0x07L << 5)		/* Bit  7..5:	Chip Rev Num */


/*	XM_MODE		32 bit r/w	Mode Register */
enum {
	XM_MD_ENA_REJ	= 1<<26, /* Bit 26:	Enable Frame Reject */
	XM_MD_SPOE_E	= 1<<25, /* Bit 25:	Send Pause on Edge */
									/* 		extern generated */
	XM_MD_TX_REP	= 1<<24, /* Bit 24:	Transmit Repeater Mode */
	XM_MD_SPOFF_I	= 1<<23, /* Bit 23:	Send Pause on FIFO full */
									/*		intern generated */
	XM_MD_LE_STW	= 1<<22, /* Bit 22:	Rx Stat Word in Little Endian */
	XM_MD_TX_CONT	= 1<<21, /* Bit 21:	Send Continuous */
	XM_MD_TX_PAUSE	= 1<<20, /* Bit 20: (sc)	Send Pause Frame */
	XM_MD_ATS	= 1<<19, /* Bit 19:	Append Time Stamp */
	XM_MD_SPOL_I	= 1<<18, /* Bit 18:	Send Pause on Low */
									/*		intern generated */
	XM_MD_SPOH_I	= 1<<17, /* Bit 17:	Send Pause on High */
									/*		intern generated */
	XM_MD_CAP	= 1<<16, /* Bit 16:	Check Address Pair */
	XM_MD_ENA_HASH	= 1<<15, /* Bit 15:	Enable Hashing */
	XM_MD_CSA	= 1<<14, /* Bit 14:	Check Station Address */
	XM_MD_CAA	= 1<<13, /* Bit 13:	Check Address Array */
	XM_MD_RX_MCTRL	= 1<<12, /* Bit 12:	Rx MAC Control Frame */
	XM_MD_RX_RUNT	= 1<<11, /* Bit 11:	Rx Runt Frames */
	XM_MD_RX_IRLE	= 1<<10, /* Bit 10:	Rx in Range Len Err Frame */
	XM_MD_RX_LONG	= 1<<9,  /* Bit  9:	Rx Long Frame */
	XM_MD_RX_CRCE	= 1<<8,  /* Bit  8:	Rx CRC Error Frame */
	XM_MD_RX_ERR	= 1<<7,  /* Bit  7:	Rx Error Frame */
	XM_MD_DIS_UC	= 1<<6,  /* Bit  6:	Disable Rx Unicast */
	XM_MD_DIS_MC	= 1<<5,  /* Bit  5:	Disable Rx Multicast */
	XM_MD_DIS_BC	= 1<<4,  /* Bit  4:	Disable Rx Broadcast */
	XM_MD_ENA_PROM	= 1<<3,  /* Bit  3:	Enable Promiscuous */
	XM_MD_ENA_BE	= 1<<2,  /* Bit  2:	Enable Big Endian */
	XM_MD_FTF	= 1<<1,  /* Bit  1: (sc)	Flush Tx FIFO */
	XM_MD_FRF	= 1<<0,  /* Bit  0: (sc)	Flush Rx FIFO */
};

#define XM_PAUSE_MODE	(XM_MD_SPOE_E | XM_MD_SPOL_I | XM_MD_SPOH_I)
#define XM_DEF_MODE	(XM_MD_RX_RUNT | XM_MD_RX_IRLE | XM_MD_RX_LONG |\
			 XM_MD_RX_CRCE | XM_MD_RX_ERR | XM_MD_CSA)

/*	XM_STAT_CMD	16 bit r/w	Statistics Command Register */
enum {
	XM_SC_SNP_RXC	= 1<<5,	/* Bit  5: (sc)	Snap Rx Counters */
	XM_SC_SNP_TXC	= 1<<4,	/* Bit  4: (sc)	Snap Tx Counters */
	XM_SC_CP_RXC	= 1<<3,	/* Bit  3: 	Copy Rx Counters Continuously */
	XM_SC_CP_TXC	= 1<<2,	/* Bit  2:	Copy Tx Counters Continuously */
	XM_SC_CLR_RXC	= 1<<1,	/* Bit  1: (sc)	Clear Rx Counters */
	XM_SC_CLR_TXC	= 1<<0,	/* Bit  0: (sc) Clear Tx Counters */
};


/*	XM_RX_CNT_EV	32 bit r/o	Rx Counter Event Register */
/*	XM_RX_EV_MSK	32 bit r/w	Rx Counter Event Mask */
enum {
	XMR_MAX_SZ_OV	= 1<<31, /* Bit 31:	1024-MaxSize Rx Cnt Ov*/
	XMR_1023B_OV	= 1<<30, /* Bit 30:	512-1023Byte Rx Cnt Ov*/
	XMR_511B_OV	= 1<<29, /* Bit 29:	256-511 Byte Rx Cnt Ov*/
	XMR_255B_OV	= 1<<28, /* Bit 28:	128-255 Byte Rx Cnt Ov*/
	XMR_127B_OV	= 1<<27, /* Bit 27:	65-127 Byte Rx Cnt Ov */
	XMR_64B_OV	= 1<<26, /* Bit 26:	64 Byte Rx Cnt Ov */
	XMR_UTIL_OV	= 1<<25, /* Bit 25:	Rx Util Cnt Overflow */
	XMR_UTIL_UR	= 1<<24, /* Bit 24:	Rx Util Cnt Underrun */
	XMR_CEX_ERR_OV	= 1<<23, /* Bit 23:	CEXT Err Cnt Ov */
	XMR_FCS_ERR_OV	= 1<<21, /* Bit 21:	Rx FCS Error Cnt Ov */
	XMR_LNG_ERR_OV	= 1<<20, /* Bit 20:	Rx too Long Err Cnt Ov*/
	XMR_RUNT_OV	= 1<<19, /* Bit 19:	Runt Event Cnt Ov */
	XMR_SHT_ERR_OV	= 1<<18, /* Bit 18:	Rx Short Ev Err Cnt Ov*/
	XMR_SYM_ERR_OV	= 1<<17, /* Bit 17:	Rx Sym Err Cnt Ov */
	XMR_CAR_ERR_OV	= 1<<15, /* Bit 15:	Rx Carr Ev Err Cnt Ov */
	XMR_JAB_PKT_OV	= 1<<14, /* Bit 14:	Rx Jabb Packet Cnt Ov */
	XMR_FIFO_OV	= 1<<13, /* Bit 13:	Rx FIFO Ov Ev Cnt Ov */
	XMR_FRA_ERR_OV	= 1<<12, /* Bit 12:	Rx Framing Err Cnt Ov */
	XMR_FMISS_OV	= 1<<11, /* Bit 11:	Rx Missed Ev Cnt Ov */
	XMR_BURST	= 1<<10, /* Bit 10:	Rx Burst Event Cnt Ov */
	XMR_INV_MOC	= 1<<9,  /* Bit  9:	Rx with inv. MAC OC Ov*/
	XMR_INV_MP	= 1<<8,  /* Bit  8:	Rx inv Pause Frame Ov */
	XMR_MCTRL_OV	= 1<<7,  /* Bit  7:	Rx MAC Ctrl-F Cnt Ov */
	XMR_MPAUSE_OV	= 1<<6,  /* Bit  6:	Rx Pause MAC Ctrl-F Ov*/
	XMR_UC_OK_OV	= 1<<5,  /* Bit  5:	Rx Unicast Frame CntOv*/
	XMR_MC_OK_OV	= 1<<4,  /* Bit  4:	Rx Multicast Cnt Ov */
	XMR_BC_OK_OV	= 1<<3,  /* Bit  3:	Rx Broadcast Cnt Ov */
	XMR_OK_LO_OV	= 1<<2,  /* Bit  2:	Octets Rx OK Low CntOv*/
	XMR_OK_HI_OV	= 1<<1,  /* Bit  1:	Octets Rx OK Hi Cnt Ov*/
	XMR_OK_OV	= 1<<0,  /* Bit  0:	Frames Received Ok Ov */
};

#define XMR_DEF_MSK		(XMR_OK_LO_OV | XMR_OK_HI_OV)

/*	XM_TX_CNT_EV	32 bit r/o	Tx Counter Event Register */
/*	XM_TX_EV_MSK	32 bit r/w	Tx Counter Event Mask */
enum {
	XMT_MAX_SZ_OV	= 1<<25,	/* Bit 25:	1024-MaxSize Tx Cnt Ov*/
	XMT_1023B_OV	= 1<<24,	/* Bit 24:	512-1023Byte Tx Cnt Ov*/
	XMT_511B_OV	= 1<<23,	/* Bit 23:	256-511 Byte Tx Cnt Ov*/
	XMT_255B_OV	= 1<<22,	/* Bit 22:	128-255 Byte Tx Cnt Ov*/
	XMT_127B_OV	= 1<<21,	/* Bit 21:	65-127 Byte Tx Cnt Ov */
	XMT_64B_OV	= 1<<20,	/* Bit 20:	64 Byte Tx Cnt Ov */
	XMT_UTIL_OV	= 1<<19,	/* Bit 19:	Tx Util Cnt Overflow */
	XMT_UTIL_UR	= 1<<18,	/* Bit 18:	Tx Util Cnt Underrun */
	XMT_CS_ERR_OV	= 1<<17,	/* Bit 17:	Tx Carr Sen Err Cnt Ov*/
	XMT_FIFO_UR_OV	= 1<<16,	/* Bit 16:	Tx FIFO Ur Ev Cnt Ov */
	XMT_EX_DEF_OV	= 1<<15,	/* Bit 15:	Tx Ex Deferall Cnt Ov */
	XMT_DEF	= 1<<14,	/* Bit 14:	Tx Deferred Cnt Ov */
	XMT_LAT_COL_OV	= 1<<13,	/* Bit 13:	Tx Late Col Cnt Ov */
	XMT_ABO_COL_OV	= 1<<12,	/* Bit 12:	Tx abo dueto Ex Col Ov*/
	XMT_MUL_COL_OV	= 1<<11,	/* Bit 11:	Tx Mult Col Cnt Ov */
	XMT_SNG_COL	= 1<<10,	/* Bit 10:	Tx Single Col Cnt Ov */
	XMT_MCTRL_OV	= 1<<9,		/* Bit  9:	Tx MAC Ctrl Counter Ov*/
	XMT_MPAUSE	= 1<<8,		/* Bit  8:	Tx Pause MAC Ctrl-F Ov*/
	XMT_BURST	= 1<<7,		/* Bit  7:	Tx Burst Event Cnt Ov */
	XMT_LONG	= 1<<6,		/* Bit  6:	Tx Long Frame Cnt Ov */
	XMT_UC_OK_OV	= 1<<5,		/* Bit  5:	Tx Unicast Cnt Ov */
	XMT_MC_OK_OV	= 1<<4,		/* Bit  4:	Tx Multicast Cnt Ov */
	XMT_BC_OK_OV	= 1<<3,		/* Bit  3:	Tx Broadcast Cnt Ov */
	XMT_OK_LO_OV	= 1<<2,		/* Bit  2:	Octets Tx OK Low CntOv*/
	XMT_OK_HI_OV	= 1<<1,		/* Bit  1:	Octets Tx OK Hi Cnt Ov*/
	XMT_OK_OV	= 1<<0,		/* Bit  0:	Frames Tx Ok Ov */
};

#define XMT_DEF_MSK		(XMT_OK_LO_OV | XMT_OK_HI_OV)

struct skge_rx_desc {
	u32		control;
	u32		next_offset;
	u32		dma_lo;
	u32		dma_hi;
	u32		status;
	u32		timestamp;
	u16		csum2;
	u16		csum1;
	u16		csum2_start;
	u16		csum1_start;
};

struct skge_tx_desc {
	u32		control;
	u32		next_offset;
	u32		dma_lo;
	u32		dma_hi;
	u32		status;
	u32		csum_offs;
	u16		csum_write;
	u16		csum_start;
	u32		rsvd;
};

struct skge_element {
	struct skge_element	*next;
	void			*desc;
	struct sk_buff  	*skb;
	DECLARE_PCI_UNMAP_ADDR(mapaddr);
	DECLARE_PCI_UNMAP_LEN(maplen);
};

struct skge_ring {
	struct skge_element *to_clean;
	struct skge_element *to_use;
	struct skge_element *start;
	unsigned long	    count;
};


struct skge_hw {
	void __iomem  	     *regs;
	struct pci_dev	     *pdev;
	u32		     intr_mask;
	struct net_device    *dev[2];

	u8	     	     chip_id;
	u8		     chip_rev;
	u8		     copper;
	u8		     ports;

	u32	     	     ram_size;
	u32	     	     ram_offset;
	u16		     phy_addr;
	struct work_struct   phy_work;
	struct mutex	     phy_mutex;
};

enum {
	FLOW_MODE_NONE 		= 0, /* No Flow-Control */
	FLOW_MODE_LOC_SEND	= 1, /* Local station sends PAUSE */
	FLOW_MODE_REM_SEND	= 2, /* Symmetric or just remote */
	FLOW_MODE_SYMMETRIC	= 3, /* Both stations may send PAUSE */
};

struct skge_port {
	u32		     msg_enable;
	struct skge_hw	     *hw;
	struct net_device    *netdev;
	int		     port;

	spinlock_t	     tx_lock;
	struct skge_ring     tx_ring;
	struct skge_ring     rx_ring;

	struct net_device_stats net_stats;

	u8		     rx_csum;
	u8		     blink_on;
	u8		     flow_control;
	u8		     wol;
	u8		     autoneg;	/* AUTONEG_ENABLE, AUTONEG_DISABLE */
	u8		     duplex;	/* DUPLEX_HALF, DUPLEX_FULL */
	u16		     speed;	/* SPEED_1000, SPEED_100, ... */
	u32		     advertising;

	void		     *mem;	/* PCI memory for rings */
	dma_addr_t	     dma;
	unsigned long	     mem_size;
	unsigned int	     rx_buf_size;
};


/* Register accessor for memory mapped device */
static inline u32 skge_read32(const struct skge_hw *hw, int reg)
{
	return readl(hw->regs + reg);
}

static inline u16 skge_read16(const struct skge_hw *hw, int reg)
{
	return readw(hw->regs + reg);
}

static inline u8 skge_read8(const struct skge_hw *hw, int reg)
{
	return readb(hw->regs + reg);
}

static inline void skge_write32(const struct skge_hw *hw, int reg, u32 val)
{
	writel(val, hw->regs + reg);
}

static inline void skge_write16(const struct skge_hw *hw, int reg, u16 val)
{
	writew(val, hw->regs + reg);
}

static inline void skge_write8(const struct skge_hw *hw, int reg, u8 val)
{
	writeb(val, hw->regs + reg);
}

/* MAC Related Registers inside the device. */
#define SK_REG(port,reg)	(((port)<<7)+(reg))
#define SK_XMAC_REG(port, reg) \
	((BASE_XMAC_1 + (port) * (BASE_XMAC_2 - BASE_XMAC_1)) | (reg) << 1)

static inline u32 xm_read32(const struct skge_hw *hw, int port, int reg)
{
	u32 v;
	v = skge_read16(hw, SK_XMAC_REG(port, reg));
	v |= (u32)skge_read16(hw, SK_XMAC_REG(port, reg+2)) << 16;
	return v;
}

static inline u16 xm_read16(const struct skge_hw *hw, int port, int reg)
{
	return skge_read16(hw, SK_XMAC_REG(port,reg));
}

static inline void xm_write32(const struct skge_hw *hw, int port, int r, u32 v)
{
	skge_write16(hw, SK_XMAC_REG(port,r), v & 0xffff);
	skge_write16(hw, SK_XMAC_REG(port,r+2), v >> 16);
}

static inline void xm_write16(const struct skge_hw *hw, int port, int r, u16 v)
{
	skge_write16(hw, SK_XMAC_REG(port,r), v);
}

static inline void xm_outhash(const struct skge_hw *hw, int port, int reg,
				   const u8 *hash)
{
	xm_write16(hw, port, reg,   (u16)hash[0] | ((u16)hash[1] << 8));
	xm_write16(hw, port, reg+2, (u16)hash[2] | ((u16)hash[3] << 8));
	xm_write16(hw, port, reg+4, (u16)hash[4] | ((u16)hash[5] << 8));
	xm_write16(hw, port, reg+6, (u16)hash[6] | ((u16)hash[7] << 8));
}

static inline void xm_outaddr(const struct skge_hw *hw, int port, int reg,
				   const u8 *addr)
{
	xm_write16(hw, port, reg,   (u16)addr[0] | ((u16)addr[1] << 8));
	xm_write16(hw, port, reg+2, (u16)addr[2] | ((u16)addr[3] << 8));
	xm_write16(hw, port, reg+4, (u16)addr[4] | ((u16)addr[5] << 8));
}

#define SK_GMAC_REG(port,reg) \
	(BASE_GMAC_1 + (port) * (BASE_GMAC_2-BASE_GMAC_1) + (reg))

static inline u16 gma_read16(const struct skge_hw *hw, int port, int reg)
{
	return skge_read16(hw, SK_GMAC_REG(port,reg));
}

static inline u32 gma_read32(const struct skge_hw *hw, int port, int reg)
{
	return (u32) skge_read16(hw, SK_GMAC_REG(port,reg))
		| ((u32)skge_read16(hw, SK_GMAC_REG(port,reg+4)) << 16);
}

static inline void gma_write16(const struct skge_hw *hw, int port, int r, u16 v)
{
	skge_write16(hw, SK_GMAC_REG(port,r), v);
}

static inline void gma_set_addr(struct skge_hw *hw, int port, int reg,
				    const u8 *addr)
{
	gma_write16(hw, port, reg,  (u16) addr[0] | ((u16) addr[1] << 8));
	gma_write16(hw, port, reg+4,(u16) addr[2] | ((u16) addr[3] << 8));
	gma_write16(hw, port, reg+8,(u16) addr[4] | ((u16) addr[5] << 8));
}

#endif
