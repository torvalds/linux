/* SPDX-License-Identifier: GPL-2.0-only */
/* drivers/net/ethernet/micrel/ks8851.h
 *
 * Copyright 2009 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *
 * KS8851 register definitions
*/

#define KS_CCR					0x08
#define CCR_LE					(1 << 10)   /* KSZ8851-16MLL */
#define CCR_EEPROM				(1 << 9)
#define CCR_SPI					(1 << 8)    /* KSZ8851SNL    */
#define CCR_8BIT				(1 << 7)    /* KSZ8851-16MLL */
#define CCR_16BIT				(1 << 6)    /* KSZ8851-16MLL */
#define CCR_32BIT				(1 << 5)    /* KSZ8851-16MLL */
#define CCR_SHARED				(1 << 4)    /* KSZ8851-16MLL */
#define CCR_48PIN				(1 << 1)    /* KSZ8851-16MLL */
#define CCR_32PIN				(1 << 0)    /* KSZ8851SNL    */

/* MAC address registers */
#define KS_MAR(_m)				(0x15 - (_m))
#define KS_MARL					0x10
#define KS_MARM					0x12
#define KS_MARH					0x14

#define KS_OBCR					0x20
#define OBCR_ODS_16mA				(1 << 6)

#define KS_EEPCR				0x22
#define EEPCR_EESRWA				(1 << 5)
#define EEPCR_EESA				(1 << 4)
#define EEPCR_EESB				(1 << 3)
#define EEPCR_EEDO				(1 << 2)
#define EEPCR_EESCK				(1 << 1)
#define EEPCR_EECS				(1 << 0)

#define KS_MBIR					0x24
#define MBIR_TXMBF				(1 << 12)
#define MBIR_TXMBFA				(1 << 11)
#define MBIR_RXMBF				(1 << 4)
#define MBIR_RXMBFA				(1 << 3)

#define KS_GRR					0x26
#define GRR_QMU					(1 << 1)
#define GRR_GSR					(1 << 0)

#define KS_WFCR					0x2A
#define WFCR_MPRXE				(1 << 7)
#define WFCR_WF3E				(1 << 3)
#define WFCR_WF2E				(1 << 2)
#define WFCR_WF1E				(1 << 1)
#define WFCR_WF0E				(1 << 0)

#define KS_WF0CRC0				0x30
#define KS_WF0CRC1				0x32
#define KS_WF0BM0				0x34
#define KS_WF0BM1				0x36
#define KS_WF0BM2				0x38
#define KS_WF0BM3				0x3A

#define KS_WF1CRC0				0x40
#define KS_WF1CRC1				0x42
#define KS_WF1BM0				0x44
#define KS_WF1BM1				0x46
#define KS_WF1BM2				0x48
#define KS_WF1BM3				0x4A

#define KS_WF2CRC0				0x50
#define KS_WF2CRC1				0x52
#define KS_WF2BM0				0x54
#define KS_WF2BM1				0x56
#define KS_WF2BM2				0x58
#define KS_WF2BM3				0x5A

#define KS_WF3CRC0				0x60
#define KS_WF3CRC1				0x62
#define KS_WF3BM0				0x64
#define KS_WF3BM1				0x66
#define KS_WF3BM2				0x68
#define KS_WF3BM3				0x6A

#define KS_TXCR					0x70
#define TXCR_TCGICMP				(1 << 8)
#define TXCR_TCGUDP				(1 << 7)
#define TXCR_TCGTCP				(1 << 6)
#define TXCR_TCGIP				(1 << 5)
#define TXCR_FTXQ				(1 << 4)
#define TXCR_TXFCE				(1 << 3)
#define TXCR_TXPE				(1 << 2)
#define TXCR_TXCRC				(1 << 1)
#define TXCR_TXE				(1 << 0)

#define KS_TXSR					0x72
#define TXSR_TXLC				(1 << 13)
#define TXSR_TXMC				(1 << 12)
#define TXSR_TXFID_MASK				(0x3f << 0)
#define TXSR_TXFID_SHIFT			(0)
#define TXSR_TXFID_GET(_v)			(((_v) >> 0) & 0x3f)

#define KS_RXCR1				0x74
#define RXCR1_FRXQ				(1 << 15)
#define RXCR1_RXUDPFCC				(1 << 14)
#define RXCR1_RXTCPFCC				(1 << 13)
#define RXCR1_RXIPFCC				(1 << 12)
#define RXCR1_RXPAFMA				(1 << 11)
#define RXCR1_RXFCE				(1 << 10)
#define RXCR1_RXEFE				(1 << 9)
#define RXCR1_RXMAFMA				(1 << 8)
#define RXCR1_RXBE				(1 << 7)
#define RXCR1_RXME				(1 << 6)
#define RXCR1_RXUE				(1 << 5)
#define RXCR1_RXAE				(1 << 4)
#define RXCR1_RXINVF				(1 << 1)
#define RXCR1_RXE				(1 << 0)

#define KS_RXCR2				0x76
#define RXCR2_SRDBL_MASK			(0x7 << 5)  /* KSZ8851SNL    */
#define RXCR2_SRDBL_SHIFT			(5)	    /* KSZ8851SNL    */
#define RXCR2_SRDBL_4B				(0x0 << 5)  /* KSZ8851SNL    */
#define RXCR2_SRDBL_8B				(0x1 << 5)  /* KSZ8851SNL    */
#define RXCR2_SRDBL_16B				(0x2 << 5)  /* KSZ8851SNL    */
#define RXCR2_SRDBL_32B				(0x3 << 5)  /* KSZ8851SNL    */
#define RXCR2_SRDBL_FRAME			(0x4 << 5)  /* KSZ8851SNL    */
#define RXCR2_IUFFP				(1 << 4)
#define RXCR2_RXIUFCEZ				(1 << 3)
#define RXCR2_UDPLFE				(1 << 2)
#define RXCR2_RXICMPFCC				(1 << 1)
#define RXCR2_RXSAF				(1 << 0)

#define KS_TXMIR				0x78

#define KS_RXFHSR				0x7C
#define RXFSHR_RXFV				(1 << 15)
#define RXFSHR_RXICMPFCS			(1 << 13)
#define RXFSHR_RXIPFCS				(1 << 12)
#define RXFSHR_RXTCPFCS				(1 << 11)
#define RXFSHR_RXUDPFCS				(1 << 10)
#define RXFSHR_RXBF				(1 << 7)
#define RXFSHR_RXMF				(1 << 6)
#define RXFSHR_RXUF				(1 << 5)
#define RXFSHR_RXMR				(1 << 4)
#define RXFSHR_RXFT				(1 << 3)
#define RXFSHR_RXFTL				(1 << 2)
#define RXFSHR_RXRF				(1 << 1)
#define RXFSHR_RXCE				(1 << 0)

#define KS_RXFHBCR				0x7E
#define RXFHBCR_CNT_MASK			(0xfff << 0)

#define KS_TXQCR				0x80
#define TXQCR_AETFE				(1 << 2)    /* KSZ8851SNL    */
#define TXQCR_TXQMAM				(1 << 1)
#define TXQCR_METFE				(1 << 0)

#define KS_RXQCR				0x82
#define RXQCR_RXDTTS				(1 << 12)
#define RXQCR_RXDBCTS				(1 << 11)
#define RXQCR_RXFCTS				(1 << 10)
#define RXQCR_RXIPHTOE				(1 << 9)
#define RXQCR_RXDTTE				(1 << 7)
#define RXQCR_RXDBCTE				(1 << 6)
#define RXQCR_RXFCTE				(1 << 5)
#define RXQCR_ADRFE				(1 << 4)
#define RXQCR_SDA				(1 << 3)
#define RXQCR_RRXEF				(1 << 0)

#define KS_TXFDPR				0x84
#define TXFDPR_TXFPAI				(1 << 14)
#define TXFDPR_TXFP_MASK			(0x7ff << 0)
#define TXFDPR_TXFP_SHIFT			(0)

#define KS_RXFDPR				0x86
#define RXFDPR_RXFPAI				(1 << 14)
#define RXFDPR_WST				(1 << 12)   /* KSZ8851-16MLL */
#define RXFDPR_EMS				(1 << 11)   /* KSZ8851-16MLL */
#define RXFDPR_RXFP_MASK			(0x7ff << 0)
#define RXFDPR_RXFP_SHIFT			(0)

#define KS_RXDTTR				0x8C
#define KS_RXDBCTR				0x8E

#define KS_IER					0x90
#define KS_ISR					0x92
#define IRQ_LCI					(1 << 15)
#define IRQ_TXI					(1 << 14)
#define IRQ_RXI					(1 << 13)
#define IRQ_RXOI				(1 << 11)
#define IRQ_TXPSI				(1 << 9)
#define IRQ_RXPSI				(1 << 8)
#define IRQ_TXSAI				(1 << 6)
#define IRQ_RXWFDI				(1 << 5)
#define IRQ_RXMPDI				(1 << 4)
#define IRQ_LDI					(1 << 3)
#define IRQ_EDI					(1 << 2)
#define IRQ_SPIBEI				(1 << 1)    /* KSZ8851SNL    */
#define IRQ_DEDI				(1 << 0)

#define KS_RXFCTR				0x9C
#define KS_RXFC					0x9D
#define RXFCTR_RXFC_MASK			(0xff << 8)
#define RXFCTR_RXFC_SHIFT			(8)
#define RXFCTR_RXFC_GET(_v)			(((_v) >> 8) & 0xff)
#define RXFCTR_RXFCT_MASK			(0xff << 0)
#define RXFCTR_RXFCT_SHIFT			(0)

#define KS_TXNTFSR				0x9E

#define KS_MAHTR0				0xA0
#define KS_MAHTR1				0xA2
#define KS_MAHTR2				0xA4
#define KS_MAHTR3				0xA6

#define KS_FCLWR				0xB0
#define KS_FCHWR				0xB2
#define KS_FCOWR				0xB4

#define KS_CIDER				0xC0
#define CIDER_ID				0x8870
#define CIDER_REV_MASK				(0x7 << 1)
#define CIDER_REV_SHIFT				(1)
#define CIDER_REV_GET(_v)			(((_v) >> 1) & 0x7)

#define KS_CGCR					0xC6

#define KS_IACR					0xC8
#define IACR_RDEN				(1 << 12)
#define IACR_TSEL_MASK				(0x3 << 10)
#define IACR_TSEL_SHIFT				(10)
#define IACR_TSEL_MIB				(0x3 << 10)
#define IACR_ADDR_MASK				(0x1f << 0)
#define IACR_ADDR_SHIFT				(0)

#define KS_IADLR				0xD0
#define KS_IAHDR				0xD2

#define KS_PMECR				0xD4
#define PMECR_PME_DELAY				(1 << 14)
#define PMECR_PME_POL				(1 << 12)
#define PMECR_WOL_WAKEUP			(1 << 11)
#define PMECR_WOL_MAGICPKT			(1 << 10)
#define PMECR_WOL_LINKUP			(1 << 9)
#define PMECR_WOL_ENERGY			(1 << 8)
#define PMECR_AUTO_WAKE_EN			(1 << 7)
#define PMECR_WAKEUP_NORMAL			(1 << 6)
#define PMECR_WKEVT_MASK			(0xf << 2)
#define PMECR_WKEVT_SHIFT			(2)
#define PMECR_WKEVT_GET(_v)			(((_v) >> 2) & 0xf)
#define PMECR_WKEVT_ENERGY			(0x1 << 2)
#define PMECR_WKEVT_LINK			(0x2 << 2)
#define PMECR_WKEVT_MAGICPKT			(0x4 << 2)
#define PMECR_WKEVT_FRAME			(0x8 << 2)
#define PMECR_PM_MASK				(0x3 << 0)
#define PMECR_PM_SHIFT				(0)
#define PMECR_PM_NORMAL				(0x0 << 0)
#define PMECR_PM_ENERGY				(0x1 << 0)
#define PMECR_PM_SOFTDOWN			(0x2 << 0)
#define PMECR_PM_POWERSAVE			(0x3 << 0)

/* Standard MII PHY data */
#define KS_P1MBCR				0xE4
#define KS_P1MBSR				0xE6
#define KS_PHY1ILR				0xE8
#define KS_PHY1IHR				0xEA
#define KS_P1ANAR				0xEC
#define KS_P1ANLPR				0xEE

#define KS_P1SCLMD				0xF4

#define KS_P1CR					0xF6
#define P1CR_LEDOFF				(1 << 15)
#define P1CR_TXIDS				(1 << 14)
#define P1CR_RESTARTAN				(1 << 13)
#define P1CR_DISAUTOMDIX			(1 << 10)
#define P1CR_FORCEMDIX				(1 << 9)
#define P1CR_AUTONEGEN				(1 << 7)
#define P1CR_FORCE100				(1 << 6)
#define P1CR_FORCEFDX				(1 << 5)
#define P1CR_ADV_FLOW				(1 << 4)
#define P1CR_ADV_100BT_FDX			(1 << 3)
#define P1CR_ADV_100BT_HDX			(1 << 2)
#define P1CR_ADV_10BT_FDX			(1 << 1)
#define P1CR_ADV_10BT_HDX			(1 << 0)

#define KS_P1SR					0xF8
#define P1SR_HP_MDIX				(1 << 15)
#define P1SR_REV_POL				(1 << 13)
#define P1SR_OP_100M				(1 << 10)
#define P1SR_OP_FDX				(1 << 9)
#define P1SR_OP_MDI				(1 << 7)
#define P1SR_AN_DONE				(1 << 6)
#define P1SR_LINK_GOOD				(1 << 5)
#define P1SR_PNTR_FLOW				(1 << 4)
#define P1SR_PNTR_100BT_FDX			(1 << 3)
#define P1SR_PNTR_100BT_HDX			(1 << 2)
#define P1SR_PNTR_10BT_FDX			(1 << 1)
#define P1SR_PNTR_10BT_HDX			(1 << 0)

/* TX Frame control */
#define TXFR_TXIC				(1 << 15)
#define TXFR_TXFID_MASK				(0x3f << 0)
#define TXFR_TXFID_SHIFT			(0)
