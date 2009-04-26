/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

	Module Name:
	rt28xx.h

	Abstract:
	RT28xx ASIC related definition & structures

	Revision History:
	Who			When		  What
	--------	----------	  ----------------------------------------------
       Jan Lee           Jan-3-2006     created for RT2860c
*/

#ifndef	__RT28XX_H__
#define	__RT28XX_H__


//
// PCI registers - base address 0x0000
//
#define PCI_CFG			0x0000
#define PCI_EECTRL			0x0004
#define PCI_MCUCTRL			0x0008

//
// SCH/DMA registers - base address 0x0200
//
// INT_SOURCE_CSR: Interrupt source register. Write one to clear corresponding bit
//
#define DMA_CSR0      0x200
#define INT_SOURCE_CSR      0x200
typedef	union	_INT_SOURCE_CSR_STRUC	{
	struct	{
		UINT32		RxDelayINT:1;
		UINT32		TxDelayINT:1;
		UINT32		RxDone:1;
		UINT32		Ac0DmaDone:1;//4
		UINT32       	Ac1DmaDone:1;
		UINT32       	Ac2DmaDone:1;
		UINT32       	Ac3DmaDone:1;
		UINT32       	HccaDmaDone:1; // bit7
		UINT32       	MgmtDmaDone:1;
		UINT32       	MCUCommandINT:1;//bit 9
		UINT32       	RxTxCoherent:1;
		UINT32       	TBTTInt:1;
		UINT32       	PreTBTT:1;
		UINT32       	TXFifoStatusInt:1;//FIFO Statistics is full, sw should read 0x171c
		UINT32       	AutoWakeup:1;//bit14
		UINT32       	GPTimer:1;
		UINT32       	RxCoherent:1;//bit16
		UINT32       	TxCoherent:1;
		UINT32       	:14;
	}	field;
	UINT32			word;
} INT_SOURCE_CSR_STRUC, *PINT_SOURCE_CSR_STRUC;

//
// INT_MASK_CSR:   Interrupt MASK register.   1: the interrupt is mask OFF
//
#define INT_MASK_CSR        0x204
typedef	union	_INT_MASK_CSR_STRUC	{
	struct	{
		UINT32		RXDelay_INT_MSK:1;
		UINT32		TxDelay:1;
		UINT32		RxDone:1;
		UINT32		Ac0DmaDone:1;
		UINT32       	Ac1DmaDone:1;
		UINT32       	Ac2DmaDone:1;
		UINT32       	Ac3DmaDone:1;
		UINT32       	HccaDmaDone:1;
		UINT32       	MgmtDmaDone:1;
		UINT32       	MCUCommandINT:1;
		UINT32       	:20;
		UINT32       	RxCoherent:1;
		UINT32       	TxCoherent:1;
	}	field;
	UINT32			word;
} INT_MASK_CSR_STRUC, *PINT_MASK_CSR_STRUC;

#define WPDMA_GLO_CFG 	0x208
typedef	union	_WPDMA_GLO_CFG_STRUC	{
	struct	{
		UINT32		EnableTxDMA:1;
		UINT32		TxDMABusy:1;
		UINT32		EnableRxDMA:1;
		UINT32		RxDMABusy:1;
		UINT32       	WPDMABurstSIZE:2;
		UINT32       	EnTXWriteBackDDONE:1;
		UINT32       	BigEndian:1;
		UINT32       	RXHdrScater:8;
		UINT32       	HDR_SEG_LEN:16;
	}	field;
	UINT32			word;
} WPDMA_GLO_CFG_STRUC, *PWPDMA_GLO_CFG_STRUC;

#define WPDMA_RST_IDX 	0x20c
typedef	union	_WPDMA_RST_IDX_STRUC	{
	struct	{
		UINT32		RST_DTX_IDX0:1;
		UINT32		RST_DTX_IDX1:1;
		UINT32		RST_DTX_IDX2:1;
		UINT32		RST_DTX_IDX3:1;
		UINT32       	RST_DTX_IDX4:1;
		UINT32       	RST_DTX_IDX5:1;
		UINT32       	rsv:10;
		UINT32       	RST_DRX_IDX0:1;
		UINT32       	:15;
	}	field;
	UINT32			word;
} WPDMA_RST_IDX_STRUC, *PWPDMA_RST_IDX_STRUC;

#define DELAY_INT_CFG  0x0210
typedef	union	_DELAY_INT_CFG_STRUC	{
	struct	{
		UINT32		RXMAX_PTIME:8;
		UINT32       	RXMAX_PINT:7;
		UINT32       	RXDLY_INT_EN:1;
		UINT32       	TXMAX_PTIME:8;
		UINT32       	TXMAX_PINT:7;
		UINT32       	TXDLY_INT_EN:1;
	}	field;
	UINT32			word;
} DELAY_INT_CFG_STRUC, *PDELAY_INT_CFG_STRUC;

#define WMM_AIFSN_CFG   0x0214
typedef	union	_AIFSN_CSR_STRUC	{
	struct	{
	    UINT32   Aifsn0:4;       // for AC_BE
	    UINT32   Aifsn1:4;       // for AC_BK
	    UINT32   Aifsn2:4;       // for AC_VI
	    UINT32   Aifsn3:4;       // for AC_VO
	    UINT32   Rsv:16;
	}	field;
	UINT32			word;
}	AIFSN_CSR_STRUC, *PAIFSN_CSR_STRUC;

//
// CWMIN_CSR: CWmin for each EDCA AC
//
#define WMM_CWMIN_CFG   0x0218
typedef	union	_CWMIN_CSR_STRUC	{
	struct	{
	    UINT32   Cwmin0:4;       // for AC_BE
	    UINT32   Cwmin1:4;       // for AC_BK
	    UINT32   Cwmin2:4;       // for AC_VI
	    UINT32   Cwmin3:4;       // for AC_VO
	    UINT32   Rsv:16;
	}	field;
	UINT32			word;
}	CWMIN_CSR_STRUC, *PCWMIN_CSR_STRUC;

//
// CWMAX_CSR: CWmin for each EDCA AC
//
#define WMM_CWMAX_CFG   0x021c
typedef	union	_CWMAX_CSR_STRUC	{
	struct	{
	    UINT32   Cwmax0:4;       // for AC_BE
	    UINT32   Cwmax1:4;       // for AC_BK
	    UINT32   Cwmax2:4;       // for AC_VI
	    UINT32   Cwmax3:4;       // for AC_VO
	    UINT32   Rsv:16;
	}	field;
	UINT32			word;
}	CWMAX_CSR_STRUC, *PCWMAX_CSR_STRUC;

//
// AC_TXOP_CSR0: AC_BK/AC_BE TXOP register
//
#define WMM_TXOP0_CFG    0x0220
typedef	union	_AC_TXOP_CSR0_STRUC	{
	struct	{
	    USHORT  Ac0Txop;        // for AC_BK, in unit of 32us
	    USHORT  Ac1Txop;        // for AC_BE, in unit of 32us
	}	field;
	UINT32			word;
}	AC_TXOP_CSR0_STRUC, *PAC_TXOP_CSR0_STRUC;

//
// AC_TXOP_CSR1: AC_VO/AC_VI TXOP register
//
#define WMM_TXOP1_CFG    0x0224
typedef	union	_AC_TXOP_CSR1_STRUC	{
	struct	{
	    USHORT  Ac2Txop;        // for AC_VI, in unit of 32us
	    USHORT  Ac3Txop;        // for AC_VO, in unit of 32us
	}	field;
	UINT32			word;
}	AC_TXOP_CSR1_STRUC, *PAC_TXOP_CSR1_STRUC;

#define RINGREG_DIFF			0x10
#define GPIO_CTRL_CFG    0x0228	//MAC_CSR13
#define MCU_CMD_CFG    0x022c
#define TX_BASE_PTR0     0x0230	//AC_BK base address
#define TX_MAX_CNT0      0x0234
#define TX_CTX_IDX0       0x0238
#define TX_DTX_IDX0      0x023c
#define TX_BASE_PTR1     0x0240 	//AC_BE base address
#define TX_MAX_CNT1      0x0244
#define TX_CTX_IDX1       0x0248
#define TX_DTX_IDX1      0x024c
#define TX_BASE_PTR2     0x0250 	//AC_VI base address
#define TX_MAX_CNT2      0x0254
#define TX_CTX_IDX2       0x0258
#define TX_DTX_IDX2      0x025c
#define TX_BASE_PTR3     0x0260 	//AC_VO base address
#define TX_MAX_CNT3      0x0264
#define TX_CTX_IDX3       0x0268
#define TX_DTX_IDX3      0x026c
#define TX_BASE_PTR4     0x0270 	//HCCA base address
#define TX_MAX_CNT4      0x0274
#define TX_CTX_IDX4       0x0278
#define TX_DTX_IDX4      0x027c
#define TX_BASE_PTR5     0x0280 	//MGMT base address
#define  TX_MAX_CNT5     0x0284
#define TX_CTX_IDX5       0x0288
#define TX_DTX_IDX5      0x028c
#define TX_MGMTMAX_CNT      TX_MAX_CNT5
#define TX_MGMTCTX_IDX       TX_CTX_IDX5
#define TX_MGMTDTX_IDX      TX_DTX_IDX5
#define RX_BASE_PTR     0x0290 	//RX base address
#define RX_MAX_CNT      0x0294
#define RX_CRX_IDX       0x0298
#define RX_DRX_IDX      0x029c
#define USB_DMA_CFG      0x02a0

typedef	union	_USB_DMA_CFG_STRUC	{
	struct	{
	    UINT32  RxBulkAggTOut:8;        //Rx Bulk Aggregation TimeOut  in unit of 33ns
	    UINT32  RxBulkAggLmt:8;        //Rx Bulk Aggregation Limit  in unit of 256 bytes
	    UINT32  phyclear:1;        		//phy watch dog enable. write 1
	    UINT32  rsv:2;
	    UINT32  TxClear:1;        //Clear USB DMA TX path
	    UINT32  TxopHalt:1;        //Halt TXOP count down when TX buffer is full.
	    UINT32  RxBulkAggEn:1;        //Enable Rx Bulk Aggregation
	    UINT32  RxBulkEn:1;        //Enable USB DMA Rx
	    UINT32  TxBulkEn:1;        //Enable USB DMA Tx
	    UINT32  EpoutValid:6;        //OUT endpoint data valid
	    UINT32  RxBusy:1;        //USB DMA RX FSM busy
	    UINT32  TxBusy:1;   	//USB DMA TX FSM busy
	}	field;
	UINT32			word;
}	USB_DMA_CFG_STRUC, *PUSB_DMA_CFG_STRUC;

//
//  3  PBF  registers
//
//
// Most are for debug. Driver doesn't touch PBF register.
#define 	PBF_SYS_CTRL 	 0x0400
#define     PBF_CFG                 0x0408
#define 	PBF_MAX_PCNT 	 0x040C
#define 	PBF_CTRL	 	0x0410
#define 	PBF_INT_STA	 0x0414
#define 	PBF_INT_ENA	 0x0418
#define 	TXRXQ_PCNT  	 0x0438
#define 	PBF_DBG 	 	 0x043c
#define     PBF_CAP_CTRL     0x0440

//
//  4  MAC  registers
//
//
//  4.1 MAC SYSTEM  configuration registers (offset:0x1000)
//
#define MAC_CSR0            0x1000
typedef	union	_ASIC_VER_ID_STRUC	{
	struct	{
	    USHORT  ASICRev;        // reversion  : 0
	    USHORT  ASICVer;        // version : 2860
	}	field;
	UINT32			word;
}	ASIC_VER_ID_STRUC, *PASIC_VER_ID_STRUC;

#define MAC_SYS_CTRL            0x1004		//MAC_CSR1
#define MAC_ADDR_DW0            		0x1008		// MAC ADDR DW0
#define MAC_ADDR_DW1           		 0x100c		// MAC ADDR DW1
//
// MAC_CSR2: STA MAC register 0
//
typedef	union	_MAC_DW0_STRUC	{
	struct	{
		UCHAR		Byte0;		// MAC address byte 0
		UCHAR		Byte1;		// MAC address byte 1
		UCHAR		Byte2;		// MAC address byte 2
		UCHAR		Byte3;		// MAC address byte 3
	}	field;
	UINT32			word;
}	MAC_DW0_STRUC, *PMAC_DW0_STRUC;

//
// MAC_CSR3: STA MAC register 1
//
typedef	union	_MAC_DW1_STRUC	{
	struct	{
		UCHAR		Byte4;		// MAC address byte 4
		UCHAR		Byte5;		// MAC address byte 5
		UCHAR		U2MeMask;
		UCHAR		Rsvd1;
	}	field;
	UINT32			word;
}	MAC_DW1_STRUC, *PMAC_DW1_STRUC;

#define MAC_BSSID_DW0            		0x1010		// MAC BSSID DW0
#define MAC_BSSID_DW1            		0x1014		// MAC BSSID DW1

//
// MAC_CSR5: BSSID register 1
//
typedef	union	_MAC_CSR5_STRUC	{
	struct	{
		UCHAR		Byte4;		 // BSSID byte 4
		UCHAR		Byte5;		 // BSSID byte 5
		USHORT      	BssIdMask:2; // 0: one BSSID, 10: 4 BSSID,  01: 2 BSSID , 11: 8BSSID
		USHORT		MBssBcnNum:3;
		USHORT		Rsvd:11;
	}	field;
	UINT32			word;
}	MAC_CSR5_STRUC, *PMAC_CSR5_STRUC;

#define MAX_LEN_CFG              0x1018		// rt2860b max 16k bytes. bit12:13 Maximum PSDU length (power factor) 0:2^13, 1:2^14, 2:2^15, 3:2^16
#define BBP_CSR_CFG            		0x101c		//
//
// BBP_CSR_CFG: BBP serial control register
//
typedef	union	_BBP_CSR_CFG_STRUC	{
	struct	{
		UINT32		Value:8;			// Register	value to program into BBP
		UINT32		RegNum:8;			// Selected	BBP	register
		UINT32		fRead:1;		    // 0: Write	BBP, 1:	Read BBP
		UINT32		Busy:1;				// 1: ASIC is busy execute BBP programming.
		UINT32		BBP_PAR_DUR:1;		     // 0: 4 MAC clock cycles  1: 8 MAC clock cycles
		UINT32		BBP_RW_MODE:1;		// 0: use serial mode  1:parallel
		UINT32		:12;
	}	field;
	UINT32			word;
}	BBP_CSR_CFG_STRUC, *PBBP_CSR_CFG_STRUC;

#define RF_CSR_CFG0            		0x1020
//
// RF_CSR_CFG: RF control register
//
typedef	union	_RF_CSR_CFG0_STRUC	{
	struct	{
		UINT32		RegIdAndContent:24;			// Register	value to program into BBP
		UINT32		bitwidth:5;			// Selected	BBP	register
		UINT32		StandbyMode:1;		    // 0: high when stand by 1:	low when standby
		UINT32		Sel:1;				// 0:RF_LE0 activate  1:RF_LE1 activate
		UINT32		Busy:1;		    // 0: idle 1: 8busy
	}	field;
	UINT32			word;
}	RF_CSR_CFG0_STRUC, *PRF_CSR_CFG0_STRUC;

#define RF_CSR_CFG1           		0x1024
typedef	union	_RF_CSR_CFG1_STRUC	{
	struct	{
		UINT32		RegIdAndContent:24;			// Register	value to program into BBP
		UINT32		RFGap:5;			// Gap between BB_CONTROL_RF and RF_LE. 0: 3 system clock cycle (37.5usec) 1: 5 system clock cycle (62.5usec)
		UINT32		rsv:7;		    // 0: idle 1: 8busy
	}	field;
	UINT32			word;
}	RF_CSR_CFG1_STRUC, *PRF_CSR_CFG1_STRUC;

#define RF_CSR_CFG2           		0x1028		//
typedef	union	_RF_CSR_CFG2_STRUC	{
	struct	{
		UINT32		RegIdAndContent:24;			// Register	value to program into BBP
		UINT32		rsv:8;		    // 0: idle 1: 8busy
	}	field;
	UINT32			word;
}	RF_CSR_CFG2_STRUC, *PRF_CSR_CFG2_STRUC;

#define LED_CFG           		0x102c		//  MAC_CSR14
typedef	union	_LED_CFG_STRUC	{
	struct	{
		UINT32		OnPeriod:8;			// blinking on period unit 1ms
		UINT32		OffPeriod:8;			// blinking off period unit 1ms
		UINT32		SlowBlinkPeriod:6;			// slow blinking period. unit:1ms
		UINT32		rsv:2;
		UINT32		RLedMode:2;			// red Led Mode    0: off1: blinking upon TX2: periodic slow blinking3: always on
		UINT32		GLedMode:2;			// green Led Mode
		UINT32		YLedMode:2;			// yellow Led Mode
		UINT32		LedPolar:1;			// Led Polarity.  0: active low1: active high
		UINT32		:1;
	}	field;
	UINT32			word;
}	LED_CFG_STRUC, *PLED_CFG_STRUC;

//
//  4.2 MAC TIMING  configuration registers (offset:0x1100)
//
#define XIFS_TIME_CFG             0x1100		 // MAC_CSR8  MAC_CSR9
typedef	union	_IFS_SLOT_CFG_STRUC	{
	struct	{
	    UINT32  CckmSifsTime:8;        //  unit 1us. Applied after CCK RX/TX
	    UINT32  OfdmSifsTime:8;        //  unit 1us. Applied after OFDM RX/TX
	    UINT32  OfdmXifsTime:4;        //OFDM SIFS. unit 1us. Applied after OFDM RX when MAC doesn't reference BBP signal BBRXEND
	    UINT32  EIFS:9;        //  unit 1us
	    UINT32  BBRxendEnable:1;        //  reference RXEND signal to begin XIFS defer
	    UINT32  rsv:2;
	}	field;
	UINT32			word;
}	IFS_SLOT_CFG_STRUC, *PIFS_SLOT_CFG_STRUC;

#define BKOFF_SLOT_CFG             0x1104		 //  mac_csr9 last 8 bits
#define NAV_TIME_CFG             0x1108		 // NAV  (MAC_CSR15)
#define CH_TIME_CFG             0x110C		 	// Count as channel busy
#define PBF_LIFE_TIMER             0x1110		 //TX/RX MPDU timestamp timer (free run)Unit: 1us
#define BCN_TIME_CFG             0x1114		 // TXRX_CSR9

#define BCN_OFFSET0				0x042C
#define BCN_OFFSET1				0x0430

//
// BCN_TIME_CFG : Synchronization control register
//
typedef	union	_BCN_TIME_CFG_STRUC	{
	struct	{
		UINT32       BeaconInterval:16;  // in unit of 1/16 TU
		UINT32		bTsfTicking:1;		// Enable TSF auto counting
		UINT32		TsfSyncMode:2;		// Enable TSF sync, 00: disable, 01: infra mode, 10: ad-hoc mode
        UINT32       bTBTTEnable:1;
		UINT32		bBeaconGen:1;		// Enable beacon generator
        UINT32       :3;
		UINT32		TxTimestampCompensate:8;
	}	field;
	UINT32			word;
}	BCN_TIME_CFG_STRUC, *PBCN_TIME_CFG_STRUC;

#define TBTT_SYNC_CFG            0x1118  		// txrx_csr10
#define TSF_TIMER_DW0             0x111C  		// Local TSF timer lsb 32 bits. Read-only
#define TSF_TIMER_DW1             0x1120  		// msb 32 bits. Read-only.
#define TBTT_TIMER             	0x1124  		// TImer remains till next TBTT. Read-only.  TXRX_CSR14
#define INT_TIMER_CFG              	0x1128  		//
#define INT_TIMER_EN             	0x112c  		//  GP-timer and pre-tbtt Int enable
#define CH_IDLE_STA              	0x1130  		//  channel idle time
#define CH_BUSY_STA              	0x1134  		//  channle busy time
//
//  4.2 MAC POWER  configuration registers (offset:0x1200)
//
#define MAC_STATUS_CFG             0x1200		 // old MAC_CSR12
#define PWR_PIN_CFG             0x1204		 // old MAC_CSR12
#define AUTO_WAKEUP_CFG             0x1208		 // old MAC_CSR10
//
// AUTO_WAKEUP_CFG: Manual power control / status register
//
typedef	union	_AUTO_WAKEUP_STRUC	{
	struct	{
		UINT32       AutoLeadTime:8;
		UINT32       NumofSleepingTbtt:7;          // ForceWake has high privilege than PutToSleep when both set
		UINT32		EnableAutoWakeup:1;	// 0:sleep, 1:awake
		UINT32		:16;
	}	field;
	UINT32			word;
}	AUTO_WAKEUP_STRUC, *PAUTO_WAKEUP_STRUC;

//
//  4.3 MAC TX  configuration registers (offset:0x1300)
//

#define EDCA_AC0_CFG	0x1300		//AC_TXOP_CSR0 0x3474
#define EDCA_AC1_CFG	0x1304
#define EDCA_AC2_CFG	0x1308
#define EDCA_AC3_CFG	0x130c
typedef	union	_EDCA_AC_CFG_STRUC	{
	struct	{
	    UINT32  AcTxop:8;        //  in unit of 32us
	    UINT32  Aifsn:4;        // # of slot time
	    UINT32  Cwmin:4;        //
	    UINT32  Cwmax:4;        //unit power of 2
	    UINT32  :12;       //
	}	field;
	UINT32			word;
}	EDCA_AC_CFG_STRUC, *PEDCA_AC_CFG_STRUC;

#define EDCA_TID_AC_MAP	0x1310
#define TX_PWR_CFG_0	0x1314
#define TX_PWR_CFG_1	0x1318
#define TX_PWR_CFG_2	0x131C
#define TX_PWR_CFG_3	0x1320
#define TX_PWR_CFG_4	0x1324
#define TX_PIN_CFG		0x1328
#define TX_BAND_CFG	0x132c		// 0x1 use upper 20MHz. 0 juse lower 20MHz
#define TX_SW_CFG0		0x1330
#define TX_SW_CFG1		0x1334
#define TX_SW_CFG2		0x1338
#define TXOP_THRES_CFG		0x133c
#define TXOP_CTRL_CFG		0x1340
#define TX_RTS_CFG		0x1344

typedef	union	_TX_RTS_CFG_STRUC	{
	struct	{
	    UINT32       AutoRtsRetryLimit:8;
	    UINT32       RtsThres:16;    // unit:byte
	    UINT32       RtsFbkEn:1;    // enable rts rate fallback
	    UINT32       rsv:7;     // 1: HT non-STBC control frame enable
	}	field;
	UINT32			word;
}	TX_RTS_CFG_STRUC, *PTX_RTS_CFG_STRUC;

#define TX_TIMEOUT_CFG	0x1348
typedef	union	_TX_TIMEOUT_CFG_STRUC	{
	struct	{
	    UINT32       rsv:4;
	    UINT32       MpduLifeTime:4;    //  expiration time = 2^(9+MPDU LIFE TIME)  us
	    UINT32       RxAckTimeout:8;	// unit:slot. Used for TX precedure
	    UINT32       TxopTimeout:8;	//TXOP timeout value for TXOP truncation.  It is recommended that (SLOT_TIME) > (TX_OP_TIMEOUT) > (RX_ACK_TIMEOUT)
	    UINT32       rsv2:8;     // 1: HT non-STBC control frame enable
	}	field;
	UINT32			word;
}	TX_TIMEOUT_CFG_STRUC, *PTX_TIMEOUT_CFG_STRUC;

#define TX_RTY_CFG	0x134c
typedef	union PACKED _TX_RTY_CFG_STRUC	{
	struct	{
	    UINT32       ShortRtyLimit:8;	//  short retry limit
	    UINT32       LongRtyLimit:8;	//long retry limit
	    UINT32       LongRtyThre:12;	// Long retry threshoold
	    UINT32       NonAggRtyMode:1;	// Non-Aggregate MPDU retry mode.  0:expired by retry limit, 1: expired by mpdu life timer
	    UINT32       AggRtyMode:1;	// Aggregate MPDU retry mode.  0:expired by retry limit, 1: expired by mpdu life timer
	    UINT32       TxautoFBEnable:1;    // Tx retry PHY rate auto fallback enable
	    UINT32       rsv:1;     // 1: HT non-STBC control frame enable
	}	field;
	UINT32			word;
}	TX_RTY_CFG_STRUC, *PTX_RTY_CFG_STRUC;

#define TX_LINK_CFG	0x1350
typedef	union	PACKED _TX_LINK_CFG_STRUC	{
	struct PACKED {
	    UINT32       RemoteMFBLifeTime:8;	//remote MFB life time. unit : 32us
	    UINT32       MFBEnable:1;	//  TX apply remote MFB 1:enable
	    UINT32       RemoteUMFSEnable:1;	//  remote unsolicit  MFB enable.  0: not apply remote remote unsolicit (MFS=7)
	    UINT32       TxMRQEn:1;	//  MCS request TX enable
	    UINT32       TxRDGEn:1;	// RDG TX enable
	    UINT32       TxCFAckEn:1;	//   Piggyback CF-ACK enable
	    UINT32       rsv:3;	//
	    UINT32       RemotMFB:8;    //  remote MCS feedback
	    UINT32       RemotMFS:8;	//remote MCS feedback sequence number
	}	field;
	UINT32			word;
}	TX_LINK_CFG_STRUC, *PTX_LINK_CFG_STRUC;

#define HT_FBK_CFG0	0x1354
typedef	union PACKED _HT_FBK_CFG0_STRUC	{
	struct	{
	    UINT32       HTMCS0FBK:4;
	    UINT32       HTMCS1FBK:4;
	    UINT32       HTMCS2FBK:4;
	    UINT32       HTMCS3FBK:4;
	    UINT32       HTMCS4FBK:4;
	    UINT32       HTMCS5FBK:4;
	    UINT32       HTMCS6FBK:4;
	    UINT32       HTMCS7FBK:4;
	}	field;
	UINT32			word;
}	HT_FBK_CFG0_STRUC, *PHT_FBK_CFG0_STRUC;

#define HT_FBK_CFG1	0x1358
typedef	union	_HT_FBK_CFG1_STRUC	{
	struct	{
	    UINT32       HTMCS8FBK:4;
	    UINT32       HTMCS9FBK:4;
	    UINT32       HTMCS10FBK:4;
	    UINT32       HTMCS11FBK:4;
	    UINT32       HTMCS12FBK:4;
	    UINT32       HTMCS13FBK:4;
	    UINT32       HTMCS14FBK:4;
	    UINT32       HTMCS15FBK:4;
	}	field;
	UINT32			word;
}	HT_FBK_CFG1_STRUC, *PHT_FBK_CFG1_STRUC;

#define LG_FBK_CFG0	0x135c
typedef	union	_LG_FBK_CFG0_STRUC	{
	struct	{
	    UINT32       OFDMMCS0FBK:4;	//initial value is 0
	    UINT32       OFDMMCS1FBK:4;	//initial value is 0
	    UINT32       OFDMMCS2FBK:4;	//initial value is 1
	    UINT32       OFDMMCS3FBK:4;	//initial value is 2
	    UINT32       OFDMMCS4FBK:4;	//initial value is 3
	    UINT32       OFDMMCS5FBK:4;	//initial value is 4
	    UINT32       OFDMMCS6FBK:4;	//initial value is 5
	    UINT32       OFDMMCS7FBK:4;	//initial value is 6
	}	field;
	UINT32			word;
}	LG_FBK_CFG0_STRUC, *PLG_FBK_CFG0_STRUC;

#define LG_FBK_CFG1		0x1360
typedef	union	_LG_FBK_CFG1_STRUC	{
	struct	{
	    UINT32       CCKMCS0FBK:4;	//initial value is 0
	    UINT32       CCKMCS1FBK:4;	//initial value is 0
	    UINT32       CCKMCS2FBK:4;	//initial value is 1
	    UINT32       CCKMCS3FBK:4;	//initial value is 2
	    UINT32       rsv:16;
	}	field;
	UINT32			word;
}	LG_FBK_CFG1_STRUC, *PLG_FBK_CFG1_STRUC;

//=======================================================
//================ Protection Paramater================================
//=======================================================
#define CCK_PROT_CFG	0x1364		//CCK Protection
#define ASIC_SHORTNAV		1
#define ASIC_LONGNAV		2
#define ASIC_RTS		1
#define ASIC_CTS		2
typedef	union	_PROT_CFG_STRUC	{
	struct	{
	    UINT32       ProtectRate:16;	//Protection control frame rate for CCK TX(RTS/CTS/CFEnd).
	    UINT32       ProtectCtrl:2;	//Protection control frame type for CCK TX. 1:RTS/CTS, 2:CTS-to-self, 0:None, 3:rsv
	    UINT32       ProtectNav:2;	//TXOP protection type for CCK TX. 0:None, 1:ShortNAVprotect,  2:LongNAVProtect, 3:rsv
	    UINT32       TxopAllowCck:1;	//CCK TXOP allowance.0:disallow.
	    UINT32       TxopAllowOfdm:1;	//CCK TXOP allowance.0:disallow.
	    UINT32       TxopAllowMM20:1;	//CCK TXOP allowance. 0:disallow.
	    UINT32       TxopAllowMM40:1;	//CCK TXOP allowance.0:disallow.
	    UINT32       TxopAllowGF20:1;	//CCK TXOP allowance.0:disallow.
	    UINT32       TxopAllowGF40:1;	//CCK TXOP allowance.0:disallow.
	    UINT32       RTSThEn:1;	//RTS threshold enable on CCK TX
	    UINT32       rsv:5;
	}	field;
	UINT32			word;
}	PROT_CFG_STRUC, *PPROT_CFG_STRUC;

#define OFDM_PROT_CFG	0x1368		//OFDM Protection
#define MM20_PROT_CFG	0x136C		//MM20 Protection
#define MM40_PROT_CFG	0x1370		//MM40 Protection
#define GF20_PROT_CFG	0x1374		//GF20 Protection
#define GF40_PROT_CFG	0x1378		//GR40 Protection
#define EXP_CTS_TIME	0x137C		//
#define EXP_ACK_TIME	0x1380		//

//
//  4.4 MAC RX configuration registers (offset:0x1400)
//
#define RX_FILTR_CFG	0x1400			//TXRX_CSR0
#define AUTO_RSP_CFG	0x1404			//TXRX_CSR4
//
// TXRX_CSR4: Auto-Responder/
//
typedef union _AUTO_RSP_CFG_STRUC {
 struct {
     UINT32       AutoResponderEnable:1;
     UINT32       BACAckPolicyEnable:1;    // 0:long, 1:short preamble
     UINT32       CTS40MMode:1;  // Response CTS 40MHz duplicate mode
     UINT32       CTS40MRef:1;  // Response CTS 40MHz duplicate mode
     UINT32       AutoResponderPreamble:1;    // 0:long, 1:short preamble
     UINT32       rsv:1;   // Power bit value in conrtrol frame
     UINT32       DualCTSEn:1;   // Power bit value in conrtrol frame
     UINT32       AckCtsPsmBit:1;   // Power bit value in conrtrol frame
     UINT32        :24;
 } field;
 UINT32   word;
} AUTO_RSP_CFG_STRUC, *PAUTO_RSP_CFG_STRUC;

#define LEGACY_BASIC_RATE	0x1408	//  TXRX_CSR5           0x3054
#define HT_BASIC_RATE		0x140c
#define HT_CTRL_CFG		0x1410
#define SIFS_COST_CFG		0x1414
#define RX_PARSER_CFG		0x1418	//Set NAV for all received frames

//
//  4.5 MAC Security configuration (offset:0x1500)
//
#define TX_SEC_CNT0		0x1500		//
#define RX_SEC_CNT0		0x1504		//
#define CCMP_FC_MUTE		0x1508		//
//
//  4.6 HCCA/PSMP (offset:0x1600)
//
#define TXOP_HLDR_ADDR0		0x1600
#define TXOP_HLDR_ADDR1		0x1604
#define TXOP_HLDR_ET		0x1608
#define QOS_CFPOLL_RA_DW0		0x160c
#define QOS_CFPOLL_A1_DW1		0x1610
#define QOS_CFPOLL_QC		0x1614
//
//  4.7 MAC Statistis registers (offset:0x1700)
//
#define RX_STA_CNT0		0x1700		//
#define RX_STA_CNT1		0x1704		//
#define RX_STA_CNT2		0x1708		//

//
// RX_STA_CNT0_STRUC: RX PLCP error count & RX CRC error count
//
typedef	union	_RX_STA_CNT0_STRUC	{
	struct	{
	    USHORT  CrcErr;
	    USHORT  PhyErr;
	}	field;
	UINT32			word;
}	RX_STA_CNT0_STRUC, *PRX_STA_CNT0_STRUC;

//
// RX_STA_CNT1_STRUC: RX False CCA count & RX LONG frame count
//
typedef	union	_RX_STA_CNT1_STRUC	{
	struct	{
	    USHORT  FalseCca;
	    USHORT  PlcpErr;
	}	field;
	UINT32			word;
}	RX_STA_CNT1_STRUC, *PRX_STA_CNT1_STRUC;

//
// RX_STA_CNT2_STRUC:
//
typedef	union	_RX_STA_CNT2_STRUC	{
	struct	{
	    USHORT  RxDupliCount;
	    USHORT  RxFifoOverflowCount;
	}	field;
	UINT32			word;
}	RX_STA_CNT2_STRUC, *PRX_STA_CNT2_STRUC;

#define TX_STA_CNT0		0x170C		//
//
// STA_CSR3: TX Beacon count
//
typedef	union	_TX_STA_CNT0_STRUC	{
	struct	{
	    USHORT  TxFailCount;
	    USHORT  TxBeaconCount;
	}	field;
	UINT32			word;
}	TX_STA_CNT0_STRUC, *PTX_STA_CNT0_STRUC;

#define TX_STA_CNT1		0x1710		//
//
// TX_STA_CNT1: TX tx count
//
typedef	union	_TX_STA_CNT1_STRUC	{
	struct	{
	    USHORT  TxSuccess;
	    USHORT  TxRetransmit;
	}	field;
	UINT32			word;
}	TX_STA_CNT1_STRUC, *PTX_STA_CNT1_STRUC;

#define TX_STA_CNT2		0x1714		//
//
// TX_STA_CNT2: TX tx count
//
typedef	union	_TX_STA_CNT2_STRUC	{
	struct	{
	    USHORT  TxZeroLenCount;
	    USHORT  TxUnderFlowCount;
	}	field;
	UINT32			word;
}	TX_STA_CNT2_STRUC, *PTX_STA_CNT2_STRUC;

#define TX_STA_FIFO		0x1718		//
//
// TX_STA_FIFO_STRUC: TX Result for specific PID status fifo register
//
typedef	union PACKED _TX_STA_FIFO_STRUC	{
	struct	{
		UINT32       	bValid:1;   // 1:This register contains a valid TX result
		UINT32       	PidType:4;
		UINT32       	TxSuccess:1;   // Tx No retry success
		UINT32       	TxAggre:1;    // Tx Retry Success
		UINT32       	TxAckRequired:1;    // Tx fail
		UINT32		wcid:8;		//wireless client index
//		UINT32		SuccessRate:16;	//include MCS, mode ,shortGI, BW settingSame format as TXWI Word 0 Bit 31-16.
		UINT32		SuccessRate:13;	//include MCS, mode ,shortGI, BW settingSame format as TXWI Word 0 Bit 31-16.
		UINT32		TxBF:1;
		UINT32		Reserve:2;
	}	field;
	UINT32			word;
}	TX_STA_FIFO_STRUC, *PTX_STA_FIFO_STRUC;

// Debug counter
#define TX_AGG_CNT	0x171c
typedef	union	_TX_AGG_CNT_STRUC	{
	struct	{
	    USHORT  NonAggTxCount;
	    USHORT  AggTxCount;
	}	field;
	UINT32			word;
}	TX_AGG_CNT_STRUC, *PTX_AGG_CNT_STRUC;

// Debug counter
#define TX_AGG_CNT0	0x1720
typedef	union	_TX_AGG_CNT0_STRUC	{
	struct	{
	    USHORT  AggSize1Count;
	    USHORT  AggSize2Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT0_STRUC, *PTX_AGG_CNT0_STRUC;

// Debug counter
#define TX_AGG_CNT1	0x1724
typedef	union	_TX_AGG_CNT1_STRUC	{
	struct	{
	    USHORT  AggSize3Count;
	    USHORT  AggSize4Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT1_STRUC, *PTX_AGG_CNT1_STRUC;

#define TX_AGG_CNT2	0x1728
typedef	union	_TX_AGG_CNT2_STRUC	{
	struct	{
	    USHORT  AggSize5Count;
	    USHORT  AggSize6Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT2_STRUC, *PTX_AGG_CNT2_STRUC;

// Debug counter
#define TX_AGG_CNT3	0x172c
typedef	union	_TX_AGG_CNT3_STRUC	{
	struct	{
	    USHORT  AggSize7Count;
	    USHORT  AggSize8Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT3_STRUC, *PTX_AGG_CNT3_STRUC;

// Debug counter
#define TX_AGG_CNT4	0x1730
typedef	union	_TX_AGG_CNT4_STRUC	{
	struct	{
	    USHORT  AggSize9Count;
	    USHORT  AggSize10Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT4_STRUC, *PTX_AGG_CNT4_STRUC;

#define TX_AGG_CNT5	0x1734
typedef	union	_TX_AGG_CNT5_STRUC	{
	struct	{
	    USHORT  AggSize11Count;
	    USHORT  AggSize12Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT5_STRUC, *PTX_AGG_CNT5_STRUC;

#define TX_AGG_CNT6		0x1738
typedef	union	_TX_AGG_CNT6_STRUC	{
	struct	{
	    USHORT  AggSize13Count;
	    USHORT  AggSize14Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT6_STRUC, *PTX_AGG_CNT6_STRUC;

#define TX_AGG_CNT7		0x173c
typedef	union	_TX_AGG_CNT7_STRUC	{
	struct	{
	    USHORT  AggSize15Count;
	    USHORT  AggSize16Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT7_STRUC, *PTX_AGG_CNT7_STRUC;

#define MPDU_DENSITY_CNT		0x1740
typedef	union	_MPDU_DEN_CNT_STRUC	{
	struct	{
	    USHORT  TXZeroDelCount;	//TX zero length delimiter count
	    USHORT  RXZeroDelCount;	//RX zero length delimiter count
	}	field;
	UINT32			word;
}	MPDU_DEN_CNT_STRUC, *PMPDU_DEN_CNT_STRUC;

//
// TXRX control registers - base address 0x3000
//
// rt2860b  UNKNOWN reg use R/O Reg Addr 0x77d0 first..
#define TXRX_CSR1           0x77d0

//
// Security key table memory, base address = 0x1000
//
#define MAC_WCID_BASE		0x1800 //8-bytes(use only 6-bytes) * 256 entry =
#define HW_WCID_ENTRY_SIZE   8
#define PAIRWISE_KEY_TABLE_BASE     0x4000      // 32-byte * 256-entry =  -byte
#define HW_KEY_ENTRY_SIZE           0x20
#define PAIRWISE_IVEIV_TABLE_BASE     0x6000      // 8-byte * 256-entry =  -byte
#define MAC_IVEIV_TABLE_BASE     0x6000      // 8-byte * 256-entry =  -byte
#define HW_IVEIV_ENTRY_SIZE   8
#define MAC_WCID_ATTRIBUTE_BASE     0x6800      // 4-byte * 256-entry =  -byte
#define HW_WCID_ATTRI_SIZE   4
#define WCID_RESERVED          		0x6bfc
#define SHARED_KEY_TABLE_BASE       0x6c00      // 32-byte * 16-entry = 512-byte
#define SHARED_KEY_MODE_BASE       0x7000      // 32-byte * 16-entry = 512-byte
#define HW_SHARED_KEY_MODE_SIZE   4
#define SHAREDKEYTABLE			0
#define PAIRWISEKEYTABLE			1

typedef	union	_SHAREDKEY_MODE_STRUC	{
	struct	{
		UINT32       Bss0Key0CipherAlg:3;
		UINT32       :1;
		UINT32       Bss0Key1CipherAlg:3;
		UINT32       :1;
		UINT32       Bss0Key2CipherAlg:3;
		UINT32       :1;
		UINT32       Bss0Key3CipherAlg:3;
		UINT32       :1;
		UINT32       Bss1Key0CipherAlg:3;
		UINT32       :1;
		UINT32       Bss1Key1CipherAlg:3;
		UINT32       :1;
		UINT32       Bss1Key2CipherAlg:3;
		UINT32       :1;
		UINT32       Bss1Key3CipherAlg:3;
		UINT32       :1;
	}	field;
	UINT32			word;
}	SHAREDKEY_MODE_STRUC, *PSHAREDKEY_MODE_STRUC;

// 64-entry for pairwise key table
typedef struct _HW_WCID_ENTRY {  // 8-byte per entry
    UCHAR   Address[6];
    UCHAR   Rsv[2];
} HW_WCID_ENTRY, PHW_WCID_ENTRY;



//
// Other on-chip shared memory space, base = 0x2000
//

// CIS space - base address = 0x2000
#define HW_CIS_BASE             0x2000

// Carrier-sense CTS frame base address. It's where mac stores carrier-sense frame for carrier-sense function.
#define HW_CS_CTS_BASE			0x7700
// DFS CTS frame base address. It's where mac stores CTS frame for DFS.
#define HW_DFS_CTS_BASE			0x7780
#define HW_CTS_FRAME_SIZE		0x80

// 2004-11-08 john - since NULL frame won't be that long (256 byte). We steal 16 tail bytes
// to save debugging settings
#define HW_DEBUG_SETTING_BASE   0x77f0  // 0x77f0~0x77ff total 16 bytes
#define HW_DEBUG_SETTING_BASE2   0x7770  // 0x77f0~0x77ff total 16 bytes

// In order to support maximum 8 MBSS and its maximum length is 512 for each beacon
// Three section discontinue memory segments will be used.
// 1. The original region for BCN 0~3
// 2. Extract memory from FCE table for BCN 4~5
// 3. Extract memory from Pair-wise key table for BCN 6~7
//	  It occupied those memory of wcid 238~253 for BCN 6
//						      and wcid 222~237 for BCN 7
#define HW_BEACON_MAX_SIZE      0x1000 /* unit: byte */
#define HW_BEACON_BASE0         0x7800
#define HW_BEACON_BASE1         0x7A00
#define HW_BEACON_BASE2         0x7C00
#define HW_BEACON_BASE3         0x7E00
#define HW_BEACON_BASE4         0x7200
#define HW_BEACON_BASE5         0x7400
#define HW_BEACON_BASE6         0x5DC0
#define HW_BEACON_BASE7         0x5BC0

#define HW_BEACON_MAX_COUNT     8
#define HW_BEACON_OFFSET		0x0200
#define HW_BEACON_CONTENT_LEN	(HW_BEACON_OFFSET - TXWI_SIZE)

// HOST-MCU shared memory - base address = 0x2100
#define HOST_CMD_CSR		0x404
#define H2M_MAILBOX_CSR         0x7010
#define H2M_MAILBOX_CID         0x7014
#define H2M_MAILBOX_STATUS      0x701c
#define H2M_INT_SRC             0x7024
#define H2M_BBP_AGENT           0x7028
#define M2H_CMD_DONE_CSR        0x000c
#define MCU_TXOP_ARRAY_BASE     0x000c   // TODO: to be provided by Albert
#define MCU_TXOP_ENTRY_SIZE     32       // TODO: to be provided by Albert
#define MAX_NUM_OF_TXOP_ENTRY   16       // TODO: must be same with 8051 firmware
#define MCU_MBOX_VERSION        0x01     // TODO: to be confirmed by Albert
#define MCU_MBOX_VERSION_OFFSET 5        // TODO: to be provided by Albert

//
// Host DMA registers - base address 0x200 .  TX0-3=EDCAQid0-3, TX4=HCCA, TX5=MGMT,
//
//
//  DMA RING DESCRIPTOR
//
#define E2PROM_CSR          0x0004
#define IO_CNTL_CSR         0x77d0

#ifdef RT2870
// 8051 firmware image for usb - use last-half base address = 0x3000
#define FIRMWARE_IMAGE_BASE     0x3000
#define MAX_FIRMWARE_IMAGE_SIZE 0x1000    // 4kbyte
#endif // RT2870 //

// ================================================================
// Tx /	Rx / Mgmt ring descriptor definition
// ================================================================

// the following PID values are used to mark outgoing frame type in TXD->PID so that
// proper TX statistics can be collected based on these categories
// b3-2 of PID field -
#define PID_MGMT			0x05
#define PID_BEACON			0x0c
#define PID_DATA_NORMALUCAST	 	0x02
#define PID_DATA_AMPDU	 	0x04
#define PID_DATA_NO_ACK    	0x08
#define PID_DATA_NOT_NORM_ACK	 	0x03

// value domain of pTxD->HostQId (4-bit: 0~15)
#define QID_AC_BK               1   // meet ACI definition in 802.11e
#define QID_AC_BE               0   // meet ACI definition in 802.11e
#define QID_AC_VI               2
#define QID_AC_VO               3
#define QID_HCCA                4
#define NUM_OF_TX_RING          5
#define QID_MGMT                13
#define QID_RX                  14
#define QID_OTHER               15


// ------------------------------------------------------
// BBP & RF	definition
// ------------------------------------------------------
#define	BUSY		                1
#define	IDLE		                0

#define	RF_R00					    0
#define	RF_R01					    1
#define	RF_R02					    2
#define	RF_R03					    3
#define	RF_R04					    4
#define	RF_R05					    5
#define	RF_R06					    6
#define	RF_R07					    7
#define	RF_R08					    8
#define	RF_R09					    9
#define	RF_R10					    10
#define	RF_R11					    11
#define	RF_R12					    12
#define	RF_R13					    13
#define	RF_R14					    14
#define	RF_R15					    15
#define	RF_R16					    16
#define	RF_R17					    17
#define	RF_R18					    18
#define	RF_R19					    19
#define	RF_R20					    20
#define	RF_R21					    21
#define	RF_R22					    22
#define	RF_R23					    23
#define	RF_R24					    24
#define	RF_R25					    25
#define	RF_R26					    26
#define	RF_R27					    27
#define	RF_R28					    28
#define	RF_R29					    29
#define	RF_R30					    30
#define	RF_R31					    31

#define	BBP_R0					    0  // version
#define	BBP_R1				        1  // TSSI
#define	BBP_R2          			2  // TX configure
#define BBP_R3                      3
#define BBP_R4                      4
#define BBP_R5                      5
#define BBP_R6                      6
#define	BBP_R14			            14 // RX configure
#define BBP_R16                     16
#define BBP_R17                     17 // RX sensibility
#define BBP_R18                     18
#define BBP_R21                     21
#define BBP_R22                     22
#define BBP_R24                     24
#define BBP_R25                     25
#define BBP_R49                     49 //TSSI
#define BBP_R50                     50
#define BBP_R51                     51
#define BBP_R52                     52
#define BBP_R55                     55
#define BBP_R62                     62 // Rx SQ0 Threshold HIGH
#define BBP_R63                     63
#define BBP_R64                     64
#define BBP_R65                     65
#define BBP_R66                     66
#define BBP_R67                     67
#define BBP_R68                     68
#define BBP_R69                     69
#define BBP_R70                     70 // Rx AGC SQ CCK Xcorr threshold
#define BBP_R73                     73
#define BBP_R75						75
#define BBP_R77                     77
#define BBP_R81                     81
#define BBP_R82                     82
#define BBP_R83                     83
#define BBP_R84                     84
#define BBP_R86						86
#define BBP_R91						91
#define BBP_R92						92
#define BBP_R94                     94 // Tx Gain Control
#define BBP_R103                    103
#define BBP_R105                    105
#define BBP_R113                    113
#define BBP_R114                    114
#define BBP_R115                    115
#define BBP_R116                    116
#define BBP_R117                    117
#define BBP_R118                    118
#define BBP_R119                    119
#define BBP_R120                    120
#define BBP_R121                    121
#define BBP_R122                    122
#define BBP_R123                    123


#define BBPR94_DEFAULT              0x06 // Add 1 value will gain 1db

#define RSSI_FOR_VERY_LOW_SENSIBILITY -35
#define RSSI_FOR_LOW_SENSIBILITY      -58
#define RSSI_FOR_MID_LOW_SENSIBILITY  -80
#define RSSI_FOR_MID_SENSIBILITY      -90

//-------------------------------------------------------------------------
// EEPROM definition
//-------------------------------------------------------------------------
#define EEDO                        0x08
#define EEDI                        0x04
#define EECS                        0x02
#define EESK                        0x01
#define EERL                        0x80

#define EEPROM_WRITE_OPCODE         0x05
#define EEPROM_READ_OPCODE          0x06
#define EEPROM_EWDS_OPCODE          0x10
#define EEPROM_EWEN_OPCODE          0x13

#define	NUM_EEPROM_BBP_PARMS		19			// Include NIC Config 0, 1, CR, TX ALC step, BBPs
#define	NUM_EEPROM_TX_G_PARMS		7
#define	EEPROM_NIC1_OFFSET          0x34		// The address is from NIC config 0, not BBP register ID
#define	EEPROM_NIC2_OFFSET          0x36		// The address is from NIC config 0, not BBP register ID
#define	EEPROM_BBP_BASE_OFFSET		0xf0		// The address is from NIC config 0, not BBP register ID
#define	EEPROM_G_TX_PWR_OFFSET		0x52
#define	EEPROM_G_TX2_PWR_OFFSET		0x60
#define EEPROM_LED1_OFFSET			0x3c
#define EEPROM_LED2_OFFSET			0x3e
#define EEPROM_LED3_OFFSET			0x40
#define EEPROM_LNA_OFFSET			0x44
#define EEPROM_RSSI_BG_OFFSET		0x46
#define EEPROM_RSSI_A_OFFSET		0x4a
#define EEPROM_DEFINE_MAX_TXPWR		0x4e
#define EEPROM_TXPOWER_BYRATE_20MHZ_2_4G	0xde	// 20MHZ 2.4G tx power.
#define EEPROM_TXPOWER_BYRATE_40MHZ_2_4G	0xee	// 40MHZ 2.4G tx power.
#define EEPROM_TXPOWER_BYRATE_20MHZ_5G		0xfa	// 20MHZ 5G tx power.
#define EEPROM_TXPOWER_BYRATE_40MHZ_5G		0x10a	// 40MHZ 5G tx power.
#define EEPROM_A_TX_PWR_OFFSET      0x78
#define EEPROM_A_TX2_PWR_OFFSET      0xa6
#define EEPROM_VERSION_OFFSET       0x02
#define	EEPROM_FREQ_OFFSET			0x3a
#define EEPROM_TXPOWER_BYRATE 	0xde	// 20MHZ power.
#define EEPROM_TXPOWER_DELTA		0x50	// 20MHZ AND 40 MHZ use different power. This is delta in 40MHZ.
#define VALID_EEPROM_VERSION        1

// PairKeyMode definition
#define PKMODE_NONE                 0
#define PKMODE_WEP64                1
#define PKMODE_WEP128               2
#define PKMODE_TKIP                 3
#define PKMODE_AES                  4
#define PKMODE_CKIP64               5
#define PKMODE_CKIP128              6
#define PKMODE_TKIP_NO_MIC          7       // MIC appended by driver: not a valid value in hardware key table

// =================================================================================
// WCID  format
// =================================================================================
//7.1	WCID  ENTRY  format  : 8bytes
typedef	struct	_WCID_ENTRY_STRUC {
	UCHAR		RXBABitmap7;    // bit0 for TID8, bit7 for TID 15
	UCHAR		RXBABitmap0;    // bit0 for TID0, bit7 for TID 7
	UCHAR		MAC[6];	// 0 for shared key table.  1 for pairwise key table
}	WCID_ENTRY_STRUC, *PWCID_ENTRY_STRUC;

//8.1.1	SECURITY  KEY  format  : 8DW
// 32-byte per entry, total 16-entry for shared key table, 64-entry for pairwise key table
typedef struct _HW_KEY_ENTRY {          // 32-byte per entry
    UCHAR   Key[16];
    UCHAR   TxMic[8];
    UCHAR   RxMic[8];
} HW_KEY_ENTRY, *PHW_KEY_ENTRY;

//8.1.2	IV/EIV  format  : 2DW

//8.1.3	RX attribute entry format  : 1DW
typedef	struct	_MAC_ATTRIBUTE_STRUC {
	UINT32		KeyTab:1;	// 0 for shared key table.  1 for pairwise key table
	UINT32		PairKeyMode:3;
	UINT32		BSSIDIdx:3; //multipleBSS index for the WCID
	UINT32		RXWIUDF:3;
	UINT32		rsv:22;
}	MAC_ATTRIBUTE_STRUC, *PMAC_ATTRIBUTE_STRUC;

// =================================================================================
// TX / RX ring descriptor format
// =================================================================================

// the first 24-byte in TXD is called TXINFO and will be DMAed to MAC block through TXFIFO.
// MAC block use this TXINFO to control the transmission behavior of this frame.
#define FIFO_MGMT                 0
#define FIFO_HCCA                 1
#define FIFO_EDCA                 2

//
// TX descriptor format, Tx	ring, Mgmt Ring
//
typedef	struct	PACKED _TXD_STRUC {
	// Word	0
	UINT32		SDPtr0;
	// Word	1
	UINT32		SDLen1:14;
	UINT32		LastSec1:1;
	UINT32		Burst:1;
	UINT32		SDLen0:14;
	UINT32		LastSec0:1;
	UINT32		DMADONE:1;
	//Word2
	UINT32		SDPtr1;
	//Word3
	UINT32		rsv2:24;
	UINT32		WIV:1;	// Wireless Info Valid. 1 if Driver already fill WI,  o if DMA needs to copy WI to correctposition
	UINT32		QSEL:2;	// select on-chip FIFO ID for 2nd-stage output scheduler.0:MGMT, 1:HCCA 2:EDCA
	UINT32		rsv:2;
	UINT32		TCO:1;	//
	UINT32		UCO:1;	//
	UINT32		ICO:1;	//
}	TXD_STRUC, *PTXD_STRUC;

//
// TXD Wireless Information format for Tx ring and Mgmt Ring
//
//txop : for txop mode
// 0:txop for the MPDU frame will be handles by ASIC by register
// 1/2/3:the MPDU frame is send after PIFS/backoff/SIFS
typedef	struct	PACKED _TXWI_STRUC {
	// Word	0
	UINT32		FRAG:1;		// 1 to inform TKIP engine this is a fragment.
	UINT32		MIMOps:1;	// the remote peer is in dynamic MIMO-PS mode
	UINT32		CFACK:1;
	UINT32		TS:1;

	UINT32		AMPDU:1;
	UINT32		MpduDensity:3;
	UINT32		txop:2;	//FOR "THIS" frame. 0:HT TXOP rule , 1:PIFS TX ,2:Backoff, 3:sifs only when previous frame exchange is successful.
	UINT32		rsv:6;

	UINT32		MCS:7;
	UINT32		BW:1;	//channel bandwidth 20MHz or 40 MHz
	UINT32		ShortGI:1;
	UINT32		STBC:2;	// 1: STBC support MCS =0-7,   2,3 : RESERVE
	UINT32		Ifs:1;	//
	UINT32		rsv2:1;
	UINT32		TxBF:1;	// 3*3
	UINT32		PHYMODE:2;
	// Word	1
	UINT32		ACK:1;
	UINT32		NSEQ:1;
	UINT32		BAWinSize:6;
	UINT32		WirelessCliID:8;
	UINT32		MPDUtotalByteCount:12;
	UINT32		PacketId:4;
	//Word2
	UINT32		IV;
	//Word3
	UINT32		EIV;
}	TXWI_STRUC, *PTXWI_STRUC;

//
// Rx descriptor format, Rx	Ring
//
//
// RXWI wireless information format, in PBF. invisible in driver.
//
typedef	struct	PACKED _RXWI_STRUC {
	// Word	0
	UINT32		WirelessCliID:8;
	UINT32		KeyIndex:2;
	UINT32		BSSID:3;
	UINT32		UDF:3;
	UINT32		MPDUtotalByteCount:12;
	UINT32		TID:4;
	// Word	1
	UINT32		FRAG:4;
	UINT32		SEQUENCE:12;
	UINT32		MCS:7;
	UINT32		BW:1;
	UINT32		ShortGI:1;
	UINT32		STBC:2;
	UINT32		rsv:3;
	UINT32		PHYMODE:2;              // 1: this RX frame is unicast to me
	//Word2
	UINT32		RSSI0:8;
	UINT32		RSSI1:8;
	UINT32		RSSI2:8;
	UINT32		rsv1:8;
	//Word3
	UINT32		SNR0:8;
	UINT32		SNR1:8;
	UINT32		rsv2:16;
}	RXWI_STRUC, *PRXWI_STRUC;

// =================================================================================
// HOST-MCU communication data structure
// =================================================================================

//
// H2M_MAILBOX_CSR: Host-to-MCU Mailbox
//
typedef union  _H2M_MAILBOX_STRUC {
    struct {
        UINT32       LowByte:8;
        UINT32       HighByte:8;
        UINT32       CmdToken:8;
        UINT32       Owner:8;
    }   field;
    UINT32           word;
} H2M_MAILBOX_STRUC, *PH2M_MAILBOX_STRUC;

//
// M2H_CMD_DONE_CSR: MCU-to-Host command complete indication
//
typedef union _M2H_CMD_DONE_STRUC {
    struct  {
        UINT32       CmdToken0;
        UINT32       CmdToken1;
        UINT32       CmdToken2;
        UINT32       CmdToken3;
    } field;
    UINT32           word;
} M2H_CMD_DONE_STRUC, *PM2H_CMD_DONE_STRUC;

//
// MCU_LEDCS: MCU LED Control Setting.
//
typedef union  _MCU_LEDCS_STRUC {
	struct	{
		UCHAR		LedMode:7;
		UCHAR		Polarity:1;
	} field;
	UCHAR			word;
} MCU_LEDCS_STRUC, *PMCU_LEDCS_STRUC;

// =================================================================================
// Register format
// =================================================================================



//NAV_TIME_CFG :NAV
typedef	union	_NAV_TIME_CFG_STRUC	{
	struct	{
		UCHAR		Sifs;               // in unit of 1-us
		UCHAR       SlotTime;    // in unit of 1-us
		USHORT		Eifs:9;               // in unit of 1-us
		USHORT		ZeroSifs:1;               // Applied zero SIFS timer after OFDM RX 0: disable
		USHORT		rsv:6;
	}	field;
	UINT32			word;
}	NAV_TIME_CFG_STRUC, *PNAV_TIME_CFG_STRUC;

//
// RX_FILTR_CFG:  /RX configuration register
//
typedef	union	_RX_FILTR_CFG_STRUC	{
	struct	{
		UINT32		DropCRCErr:1;		// Drop CRC error
		UINT32		DropPhyErr:1;		// Drop physical error
		UINT32		DropNotToMe:1;		// Drop not to me unicast frame
		UINT32		DropNotMyBSSID:1;			// Drop fram ToDs bit is true

		UINT32		DropVerErr:1;	    // Drop version error frame
		UINT32		DropMcast:1;		// Drop multicast frames
		UINT32		DropBcast:1;		// Drop broadcast frames
		UINT32		DropDuplicate:1;		// Drop duplicate frame

		UINT32		DropCFEndAck:1;		// Drop Ps-Poll
		UINT32		DropCFEnd:1;		// Drop Ps-Poll
		UINT32		DropAck:1;		// Drop Ps-Poll
		UINT32		DropCts:1;		// Drop Ps-Poll

		UINT32		DropRts:1;		// Drop Ps-Poll
		UINT32		DropPsPoll:1;		// Drop Ps-Poll
		UINT32		DropBA:1;		//
        	UINT32       	DropBAR:1;       //

		UINT32       	DropRsvCntlType:1;
		UINT32		:15;
	}	field;
	UINT32			word;
}	RX_FILTR_CFG_STRUC, *PRX_FILTR_CFG_STRUC;

//
// PHY_CSR4: RF serial control register
//
typedef	union	_PHY_CSR4_STRUC	{
	struct	{
		UINT32		RFRegValue:24;		// Register	value (include register	id)	serial out to RF/IF	chip.
		UINT32		NumberOfBits:5;		// Number of bits used in RFRegValue (I:20,	RFMD:22)
		UINT32		IFSelect:1;			// 1: select IF	to program,	0: select RF to	program
		UINT32		PLL_LD:1;			// RF PLL_LD status
		UINT32		Busy:1;				// 1: ASIC is busy execute RF programming.
	}	field;
	UINT32			word;
}	PHY_CSR4_STRUC, *PPHY_CSR4_STRUC;

//
// SEC_CSR5: shared key table security mode register
//
typedef	union	_SEC_CSR5_STRUC	{
	struct	{
        UINT32       Bss2Key0CipherAlg:3;
        UINT32       :1;
        UINT32       Bss2Key1CipherAlg:3;
        UINT32       :1;
        UINT32       Bss2Key2CipherAlg:3;
        UINT32       :1;
        UINT32       Bss2Key3CipherAlg:3;
        UINT32       :1;
        UINT32       Bss3Key0CipherAlg:3;
        UINT32       :1;
        UINT32       Bss3Key1CipherAlg:3;
        UINT32       :1;
        UINT32       Bss3Key2CipherAlg:3;
        UINT32       :1;
        UINT32       Bss3Key3CipherAlg:3;
        UINT32       :1;
	}	field;
	UINT32			word;
}	SEC_CSR5_STRUC, *PSEC_CSR5_STRUC;

//
// HOST_CMD_CSR: For HOST to interrupt embedded processor
//
typedef	union	_HOST_CMD_CSR_STRUC	{
	struct	{
	    UINT32   HostCommand:8;
	    UINT32   Rsv:24;
	}	field;
	UINT32			word;
}	HOST_CMD_CSR_STRUC, *PHOST_CMD_CSR_STRUC;

//
// AIFSN_CSR: AIFSN for each EDCA AC
//



//
// E2PROM_CSR: EEPROM control register
//
typedef	union	_E2PROM_CSR_STRUC	{
	struct	{
		UINT32		Reload:1;		// Reload EEPROM content, write one to reload, self-cleared.
		UINT32		EepromSK:1;
		UINT32		EepromCS:1;
		UINT32		EepromDI:1;
		UINT32		EepromDO:1;
		UINT32		Type:1;			// 1: 93C46, 0:93C66
		UINT32       LoadStatus:1;   // 1:loading, 0:done
		UINT32		Rsvd:25;
	}	field;
	UINT32			word;
}	E2PROM_CSR_STRUC, *PE2PROM_CSR_STRUC;

// -------------------------------------------------------------------
//  E2PROM data layout
// -------------------------------------------------------------------

//
// EEPROM antenna select format
//
typedef	union	_EEPROM_ANTENNA_STRUC	{
	struct	{
		USHORT		RxPath:4;	// 1: 1R, 2: 2R, 3: 3R
		USHORT		TxPath:4;	// 1: 1T, 2: 2T
		USHORT      RfIcType:4;             // see E2PROM document
		USHORT      Rsv:4;
	}	field;
	USHORT			word;
}	EEPROM_ANTENNA_STRUC, *PEEPROM_ANTENNA_STRUC;

typedef	union _EEPROM_NIC_CINFIG2_STRUC	{
	struct {
		USHORT		HardwareRadioControl:1;	// 1:enable, 0:disable
		USHORT		DynamicTxAgcControl:1;			//
		USHORT		ExternalLNAForG:1;				//
		USHORT		ExternalLNAForA:1;			// external LNA enable for 2.4G
		USHORT		CardbusAcceleration:1;	// !!! NOTE: 0 - enable, 1 - disable
		USHORT		BW40MSidebandForG:1;
		USHORT		BW40MSidebandForA:1;
		USHORT		EnableWPSPBC:1;                 // WPS PBC Control bit
		USHORT		BW40MAvailForG:1;			// 0:enable, 1:disable
		USHORT		BW40MAvailForA:1;			// 0:enable, 1:disable
		USHORT		Rsv2:6;                 // must be 0
	}	field;
	USHORT			word;
}	EEPROM_NIC_CONFIG2_STRUC, *PEEPROM_NIC_CONFIG2_STRUC;

//
// TX_PWR Value valid range 0xFA(-6) ~ 0x24(36)
//
typedef	union	_EEPROM_TX_PWR_STRUC	{
	struct	{
		CHAR	Byte0;				// Low Byte
		CHAR	Byte1;				// High Byte
	}	field;
	USHORT	word;
}	EEPROM_TX_PWR_STRUC, *PEEPROM_TX_PWR_STRUC;

typedef	union	_EEPROM_VERSION_STRUC	{
	struct	{
		UCHAR	FaeReleaseNumber;	// Low Byte
		UCHAR	Version;			// High Byte
	}	field;
	USHORT	word;
}	EEPROM_VERSION_STRUC, *PEEPROM_VERSION_STRUC;

typedef	union	_EEPROM_LED_STRUC	{
	struct	{
		USHORT	PolarityRDY_G:1;		// Polarity RDY_G setting.
		USHORT	PolarityRDY_A:1;		// Polarity RDY_A setting.
		USHORT	PolarityACT:1;		// Polarity ACT setting.
		USHORT	PolarityGPIO_0:1;	// Polarity GPIO#0 setting.
		USHORT	PolarityGPIO_1:1;	// Polarity GPIO#1 setting.
		USHORT	PolarityGPIO_2:1;	// Polarity GPIO#2 setting.
		USHORT	PolarityGPIO_3:1;	// Polarity GPIO#3 setting.
		USHORT	PolarityGPIO_4:1;	// Polarity GPIO#4 setting.
		USHORT	LedMode:5;			// Led mode.
		USHORT	Rsvd:3;				// Reserved
	}	field;
	USHORT	word;
}	EEPROM_LED_STRUC, *PEEPROM_LED_STRUC;

typedef	union	_EEPROM_TXPOWER_DELTA_STRUC	{
	struct	{
		UCHAR	DeltaValue:6;	// Tx Power dalta value (MAX=4)
		UCHAR	Type:1;			// 1: plus the delta value, 0: minus the delta value
		UCHAR	TxPowerEnable:1;// Enable
	}	field;
	UCHAR	value;
}	EEPROM_TXPOWER_DELTA_STRUC, *PEEPROM_TXPOWER_DELTA_STRUC;

//
// QOS_CSR0: TXOP holder address0 register
//
typedef	union	_QOS_CSR0_STRUC	{
	struct	{
		UCHAR		Byte0;		// MAC address byte 0
		UCHAR		Byte1;		// MAC address byte 1
		UCHAR		Byte2;		// MAC address byte 2
		UCHAR		Byte3;		// MAC address byte 3
	}	field;
	UINT32			word;
}	QOS_CSR0_STRUC, *PQOS_CSR0_STRUC;

//
// QOS_CSR1: TXOP holder address1 register
//
typedef	union	_QOS_CSR1_STRUC	{
	struct	{
		UCHAR		Byte4;		// MAC address byte 4
		UCHAR		Byte5;		// MAC address byte 5
		UCHAR		Rsvd0;
		UCHAR		Rsvd1;
	}	field;
	UINT32			word;
}	QOS_CSR1_STRUC, *PQOS_CSR1_STRUC;

#define	RF_CSR_CFG	0x500
typedef	union	_RF_CSR_CFG_STRUC	{
	struct	{
		UINT	RF_CSR_DATA:8;			// DATA
		UINT	TESTCSR_RFACC_REGNUM:5;	// RF register ID
		UINT	Rsvd2:3;				// Reserved
		UINT	RF_CSR_WR:1;			// 0: read  1: write
		UINT	RF_CSR_KICK:1;			// kick RF register read/write
		UINT	Rsvd1:14;				// Reserved
	}	field;
	UINT	word;
}	RF_CSR_CFG_STRUC, *PRF_CSR_CFG_STRUC;

#endif	// __RT28XX_H__
