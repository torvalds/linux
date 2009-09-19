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
	rtmp_mac.h

	Abstract:
	Ralink Wireless Chip MAC related definition & structures

	Revision History:
	Who			When		  What
	--------	----------	  ----------------------------------------------
*/

#ifndef __RTMP_MAC_H__
#define __RTMP_MAC_H__



// =================================================================================
// TX / RX ring descriptor format
// =================================================================================

// the first 24-byte in TXD is called TXINFO and will be DMAed to MAC block through TXFIFO.
// MAC block use this TXINFO to control the transmission behavior of this frame.
#define FIFO_MGMT                 0
#define FIFO_HCCA                 1
#define FIFO_EDCA                 2


//
// TXD Wireless Information format for Tx ring and Mgmt Ring
//
//txop : for txop mode
// 0:txop for the MPDU frame will be handles by ASIC by register
// 1/2/3:the MPDU frame is send after PIFS/backoff/SIFS
#ifdef RT_BIG_ENDIAN
typedef	struct	PACKED _TXWI_STRUC {
	// Word 0
	UINT32		PHYMODE:2;
	UINT32		TxBF:1;	// 3*3
	UINT32		rsv2:1;
//	UINT32		rsv2:2;
	UINT32		Ifs:1;	//
	UINT32		STBC:2;	//channel bandwidth 20MHz or 40 MHz
	UINT32		ShortGI:1;
	UINT32		BW:1;	//channel bandwidth 20MHz or 40 MHz
	UINT32		MCS:7;

	UINT32		rsv:6;
	UINT32		txop:2;	//tx back off mode 0:HT TXOP rule , 1:PIFS TX ,2:Backoff, 3:sifs only when previous frame exchange is successful.
	UINT32		MpduDensity:3;
	UINT32		AMPDU:1;

	UINT32		TS:1;
	UINT32		CFACK:1;
	UINT32		MIMOps:1;	// the remote peer is in dynamic MIMO-PS mode
	UINT32		FRAG:1;		// 1 to inform TKIP engine this is a fragment.
	// Word 1
	UINT32		PacketId:4;
	UINT32		MPDUtotalByteCount:12;
	UINT32		WirelessCliID:8;
	UINT32		BAWinSize:6;
	UINT32		NSEQ:1;
	UINT32		ACK:1;
	// Word 2
	UINT32		IV;
	// Word 3
	UINT32		EIV;
}	TXWI_STRUC, *PTXWI_STRUC;
#else
typedef	struct	PACKED _TXWI_STRUC {
	// Word	0
	// ex: 00 03 00 40 means txop = 3, PHYMODE = 1
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
//	UINT32		rsv2:2;	//channel bandwidth 20MHz or 40 MHz
	UINT32		rsv2:1;
	UINT32		TxBF:1;	// 3*3
	UINT32		PHYMODE:2;
	// Word1
	// ex:  1c ff 38 00 means ACK=0, BAWinSize=7, MPDUtotalByteCount = 0x38
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
#endif


//
// RXWI wireless information format, in PBF. invisible in driver.
//
#ifdef RT_BIG_ENDIAN
typedef	struct	PACKED _RXWI_STRUC {
	// Word 0
	UINT32		TID:4;
	UINT32		MPDUtotalByteCount:12;
	UINT32		UDF:3;
	UINT32		BSSID:3;
	UINT32		KeyIndex:2;
	UINT32		WirelessCliID:8;
	// Word 1
	UINT32		PHYMODE:2;              // 1: this RX frame is unicast to me
	UINT32		rsv:3;
	UINT32		STBC:2;
	UINT32		ShortGI:1;
	UINT32		BW:1;
	UINT32		MCS:7;
	UINT32		SEQUENCE:12;
	UINT32		FRAG:4;
	// Word 2
	UINT32		rsv1:8;
	UINT32		RSSI2:8;
	UINT32		RSSI1:8;
	UINT32		RSSI0:8;
	// Word 3
	/*UINT32		rsv2:16;*/
	UINT32		rsv2:8;
	UINT32		FOFFSET:8;	// RT35xx
	UINT32		SNR1:8;
	UINT32		SNR0:8;
}	RXWI_STRUC, *PRXWI_STRUC;
#else
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
	UINT32		FOFFSET:8;	// RT35xx
	UINT32		rsv2:8;
	/*UINT32		rsv2:16;*/
}	RXWI_STRUC, *PRXWI_STRUC;
#endif


// =================================================================================
// Register format
// =================================================================================


//
// SCH/DMA registers - base address 0x0200
//
// INT_SOURCE_CSR: Interrupt source register. Write one to clear corresponding bit
//
#define DMA_CSR0		0x200
#define INT_SOURCE_CSR		0x200
#ifdef RT_BIG_ENDIAN
typedef	union	_INT_SOURCE_CSR_STRUC	{
	struct	{
#ifdef TONE_RADAR_DETECT_SUPPORT
		UINT32			:11;
		UINT32			RadarINT:1;
		UINT32		rsv:2;
#else // original source code
		UINT32		:14;
#endif // TONE_RADAR_DETECT_SUPPORT //
		UINT32		TxCoherent:1;
		UINT32		RxCoherent:1;
		UINT32		GPTimer:1;
		UINT32		AutoWakeup:1;//bit14
		UINT32		TXFifoStatusInt:1;//FIFO Statistics is full, sw should read 0x171c
		UINT32		PreTBTT:1;
		UINT32		TBTTInt:1;
		UINT32		RxTxCoherent:1;
		UINT32		MCUCommandINT:1;
		UINT32		MgmtDmaDone:1;
		UINT32		HccaDmaDone:1;
		UINT32		Ac3DmaDone:1;
		UINT32		Ac2DmaDone:1;
		UINT32		Ac1DmaDone:1;
		UINT32		Ac0DmaDone:1;
		UINT32		RxDone:1;
		UINT32		TxDelayINT:1;	//delayed interrupt, not interrupt until several int or time limit hit
		UINT32		RxDelayINT:1; //dealyed interrupt
	}	field;
	UINT32			word;
}	INT_SOURCE_CSR_STRUC, *PINT_SOURCE_CSR_STRUC;
#else
typedef	union	_INT_SOURCE_CSR_STRUC	{
	struct	{
		UINT32		RxDelayINT:1;
		UINT32		TxDelayINT:1;
		UINT32		RxDone:1;
		UINT32		Ac0DmaDone:1;//4
		UINT32		Ac1DmaDone:1;
		UINT32		Ac2DmaDone:1;
		UINT32		Ac3DmaDone:1;
		UINT32		HccaDmaDone:1; // bit7
		UINT32		MgmtDmaDone:1;
		UINT32		MCUCommandINT:1;//bit 9
		UINT32		RxTxCoherent:1;
		UINT32		TBTTInt:1;
		UINT32		PreTBTT:1;
		UINT32		TXFifoStatusInt:1;//FIFO Statistics is full, sw should read 0x171c
		UINT32		AutoWakeup:1;//bit14
		UINT32		GPTimer:1;
		UINT32		RxCoherent:1;//bit16
		UINT32		TxCoherent:1;
#ifdef TONE_RADAR_DETECT_SUPPORT
		UINT32		rsv:2;
		UINT32			RadarINT:1;
		UINT32			:11;
#else
		UINT32		:14;
#endif // TONE_RADAR_DETECT_SUPPORT //
	}	field;
	UINT32			word;
} INT_SOURCE_CSR_STRUC, *PINT_SOURCE_CSR_STRUC;
#endif

//
// INT_MASK_CSR:   Interrupt MASK register.   1: the interrupt is mask OFF
//
#define INT_MASK_CSR        0x204
#ifdef RT_BIG_ENDIAN
typedef	union	_INT_MASK_CSR_STRUC	{
	struct	{
		UINT32		TxCoherent:1;
		UINT32		RxCoherent:1;
#ifdef TONE_RADAR_DETECT_SUPPORT
		UINT32			:9;
		UINT32			RadarINT:1;
		UINT32		rsv:10;
#else
		UINT32		:20;
#endif // TONE_RADAR_DETECT_SUPPORT //
		UINT32		MCUCommandINT:1;
		UINT32		MgmtDmaDone:1;
		UINT32		HccaDmaDone:1;
		UINT32		Ac3DmaDone:1;
		UINT32		Ac2DmaDone:1;
		UINT32		Ac1DmaDone:1;
		UINT32		Ac0DmaDone:1;
		UINT32		RxDone:1;
		UINT32		TxDelay:1;
		UINT32		RXDelay_INT_MSK:1;
	}	field;
	UINT32			word;
}INT_MASK_CSR_STRUC, *PINT_MASK_CSR_STRUC;
#else
typedef	union	_INT_MASK_CSR_STRUC	{
	struct	{
		UINT32		RXDelay_INT_MSK:1;
		UINT32		TxDelay:1;
		UINT32		RxDone:1;
		UINT32		Ac0DmaDone:1;
		UINT32		Ac1DmaDone:1;
		UINT32		Ac2DmaDone:1;
		UINT32		Ac3DmaDone:1;
		UINT32		HccaDmaDone:1;
		UINT32		MgmtDmaDone:1;
		UINT32		MCUCommandINT:1;
#ifdef TONE_RADAR_DETECT_SUPPORT
		UINT32		rsv:10;
		UINT32			RadarINT:1;
		UINT32			:9;
#else
		UINT32		:20;
#endif // TONE_RADAR_DETECT_SUPPORT //
		UINT32		RxCoherent:1;
		UINT32		TxCoherent:1;
	}	field;
	UINT32			word;
} INT_MASK_CSR_STRUC, *PINT_MASK_CSR_STRUC;
#endif

#define WPDMA_GLO_CFG	0x208
#ifdef RT_BIG_ENDIAN
typedef	union	_WPDMA_GLO_CFG_STRUC	{
	struct	{
		UINT32		HDR_SEG_LEN:16;
		UINT32		RXHdrScater:8;
		UINT32		BigEndian:1;
		UINT32		EnTXWriteBackDDONE:1;
		UINT32		WPDMABurstSIZE:2;
		UINT32		RxDMABusy:1;
		UINT32		EnableRxDMA:1;
		UINT32		TxDMABusy:1;
		UINT32		EnableTxDMA:1;
	}	field;
	UINT32			word;
}WPDMA_GLO_CFG_STRUC, *PWPDMA_GLO_CFG_STRUC;
#else
typedef	union	_WPDMA_GLO_CFG_STRUC	{
	struct	{
		UINT32		EnableTxDMA:1;
		UINT32		TxDMABusy:1;
		UINT32		EnableRxDMA:1;
		UINT32		RxDMABusy:1;
		UINT32		WPDMABurstSIZE:2;
		UINT32		EnTXWriteBackDDONE:1;
		UINT32		BigEndian:1;
		UINT32		RXHdrScater:8;
		UINT32		HDR_SEG_LEN:16;
	}	field;
	UINT32			word;
} WPDMA_GLO_CFG_STRUC, *PWPDMA_GLO_CFG_STRUC;
#endif

#define WPDMA_RST_IDX	0x20c
#ifdef RT_BIG_ENDIAN
typedef	union	_WPDMA_RST_IDX_STRUC	{
	struct	{
		UINT32		:15;
		UINT32		RST_DRX_IDX0:1;
		UINT32		rsv:10;
		UINT32		RST_DTX_IDX5:1;
		UINT32		RST_DTX_IDX4:1;
		UINT32		RST_DTX_IDX3:1;
		UINT32		RST_DTX_IDX2:1;
		UINT32		RST_DTX_IDX1:1;
		UINT32		RST_DTX_IDX0:1;
	}	field;
	UINT32			word;
}WPDMA_RST_IDX_STRUC, *PWPDMA_RST_IDX_STRUC;
#else
typedef	union	_WPDMA_RST_IDX_STRUC	{
	struct	{
		UINT32		RST_DTX_IDX0:1;
		UINT32		RST_DTX_IDX1:1;
		UINT32		RST_DTX_IDX2:1;
		UINT32		RST_DTX_IDX3:1;
		UINT32		RST_DTX_IDX4:1;
		UINT32		RST_DTX_IDX5:1;
		UINT32		rsv:10;
		UINT32		RST_DRX_IDX0:1;
		UINT32		:15;
	}	field;
	UINT32			word;
} WPDMA_RST_IDX_STRUC, *PWPDMA_RST_IDX_STRUC;
#endif
#define DELAY_INT_CFG  0x0210
#ifdef RT_BIG_ENDIAN
typedef	union	_DELAY_INT_CFG_STRUC	{
	struct	{
		UINT32		TXDLY_INT_EN:1;
		UINT32		TXMAX_PINT:7;
		UINT32		TXMAX_PTIME:8;
		UINT32		RXDLY_INT_EN:1;
		UINT32		RXMAX_PINT:7;
		UINT32		RXMAX_PTIME:8;
	}	field;
	UINT32			word;
}DELAY_INT_CFG_STRUC, *PDELAY_INT_CFG_STRUC;
#else
typedef	union	_DELAY_INT_CFG_STRUC	{
	struct	{
		UINT32		RXMAX_PTIME:8;
		UINT32		RXMAX_PINT:7;
		UINT32		RXDLY_INT_EN:1;
		UINT32		TXMAX_PTIME:8;
		UINT32		TXMAX_PINT:7;
		UINT32		TXDLY_INT_EN:1;
	}	field;
	UINT32			word;
} DELAY_INT_CFG_STRUC, *PDELAY_INT_CFG_STRUC;
#endif
#define WMM_AIFSN_CFG   0x0214
#ifdef RT_BIG_ENDIAN
typedef	union	_AIFSN_CSR_STRUC	{
	struct	{
	    UINT32   Rsv:16;
	    UINT32   Aifsn3:4;       // for AC_VO
	    UINT32   Aifsn2:4;       // for AC_VI
	    UINT32   Aifsn1:4;       // for AC_BK
	    UINT32   Aifsn0:4;       // for AC_BE
	}	field;
	UINT32			word;
}	AIFSN_CSR_STRUC, *PAIFSN_CSR_STRUC;
#else
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
#endif
//
// CWMIN_CSR: CWmin for each EDCA AC
//
#define WMM_CWMIN_CFG   0x0218
#ifdef RT_BIG_ENDIAN
typedef	union	_CWMIN_CSR_STRUC	{
	struct	{
	    UINT32   Rsv:16;
	    UINT32   Cwmin3:4;       // for AC_VO
	    UINT32   Cwmin2:4;       // for AC_VI
	    UINT32   Cwmin1:4;       // for AC_BK
	    UINT32   Cwmin0:4;       // for AC_BE
	}	field;
	UINT32			word;
}	CWMIN_CSR_STRUC, *PCWMIN_CSR_STRUC;
#else
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
#endif

//
// CWMAX_CSR: CWmin for each EDCA AC
//
#define WMM_CWMAX_CFG   0x021c
#ifdef RT_BIG_ENDIAN
typedef	union	_CWMAX_CSR_STRUC	{
	struct	{
	    UINT32   Rsv:16;
	    UINT32   Cwmax3:4;       // for AC_VO
	    UINT32   Cwmax2:4;       // for AC_VI
	    UINT32   Cwmax1:4;       // for AC_BK
	    UINT32   Cwmax0:4;       // for AC_BE
	}	field;
	UINT32			word;
}	CWMAX_CSR_STRUC, *PCWMAX_CSR_STRUC;
#else
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
#endif


//
// AC_TXOP_CSR0: AC_BK/AC_BE TXOP register
//
#define WMM_TXOP0_CFG    0x0220
#ifdef RT_BIG_ENDIAN
typedef	union	_AC_TXOP_CSR0_STRUC	{
	struct	{
	    USHORT  Ac1Txop;        // for AC_BE, in unit of 32us
	    USHORT  Ac0Txop;        // for AC_BK, in unit of 32us
	}	field;
	UINT32			word;
}	AC_TXOP_CSR0_STRUC, *PAC_TXOP_CSR0_STRUC;
#else
typedef	union	_AC_TXOP_CSR0_STRUC	{
	struct	{
	    USHORT  Ac0Txop;        // for AC_BK, in unit of 32us
	    USHORT  Ac1Txop;        // for AC_BE, in unit of 32us
	}	field;
	UINT32			word;
}	AC_TXOP_CSR0_STRUC, *PAC_TXOP_CSR0_STRUC;
#endif

//
// AC_TXOP_CSR1: AC_VO/AC_VI TXOP register
//
#define WMM_TXOP1_CFG    0x0224
#ifdef RT_BIG_ENDIAN
typedef	union	_AC_TXOP_CSR1_STRUC	{
	struct	{
	    USHORT  Ac3Txop;        // for AC_VO, in unit of 32us
	    USHORT  Ac2Txop;        // for AC_VI, in unit of 32us
	}	field;
	UINT32			word;
}	AC_TXOP_CSR1_STRUC, *PAC_TXOP_CSR1_STRUC;
#else
typedef	union	_AC_TXOP_CSR1_STRUC	{
	struct	{
	    USHORT  Ac2Txop;        // for AC_VI, in unit of 32us
	    USHORT  Ac3Txop;        // for AC_VO, in unit of 32us
	}	field;
	UINT32			word;
}	AC_TXOP_CSR1_STRUC, *PAC_TXOP_CSR1_STRUC;
#endif


#define RINGREG_DIFF			0x10
#define GPIO_CTRL_CFG    0x0228	//MAC_CSR13
#define MCU_CMD_CFG    0x022c
#define TX_BASE_PTR0     0x0230	//AC_BK base address
#define TX_MAX_CNT0      0x0234
#define TX_CTX_IDX0       0x0238
#define TX_DTX_IDX0      0x023c
#define TX_BASE_PTR1     0x0240		//AC_BE base address
#define TX_MAX_CNT1      0x0244
#define TX_CTX_IDX1       0x0248
#define TX_DTX_IDX1      0x024c
#define TX_BASE_PTR2     0x0250		//AC_VI base address
#define TX_MAX_CNT2      0x0254
#define TX_CTX_IDX2       0x0258
#define TX_DTX_IDX2      0x025c
#define TX_BASE_PTR3     0x0260		//AC_VO base address
#define TX_MAX_CNT3      0x0264
#define TX_CTX_IDX3       0x0268
#define TX_DTX_IDX3      0x026c
#define TX_BASE_PTR4     0x0270		//HCCA base address
#define TX_MAX_CNT4      0x0274
#define TX_CTX_IDX4       0x0278
#define TX_DTX_IDX4      0x027c
#define TX_BASE_PTR5     0x0280		//MGMT base address
#define  TX_MAX_CNT5     0x0284
#define TX_CTX_IDX5       0x0288
#define TX_DTX_IDX5      0x028c
#define TX_MGMTMAX_CNT      TX_MAX_CNT5
#define TX_MGMTCTX_IDX       TX_CTX_IDX5
#define TX_MGMTDTX_IDX      TX_DTX_IDX5
#define RX_BASE_PTR     0x0290	//RX base address
#define RX_MAX_CNT      0x0294
#define RX_CRX_IDX       0x0298
#define RX_DRX_IDX      0x029c


#define USB_DMA_CFG      0x02a0
#ifdef RT_BIG_ENDIAN
typedef	union	_USB_DMA_CFG_STRUC	{
	struct	{
	    UINT32  TxBusy:1;		//USB DMA TX FSM busy . debug only
	    UINT32  RxBusy:1;        //USB DMA RX FSM busy . debug only
	    UINT32  EpoutValid:6;        //OUT endpoint data valid. debug only
	    UINT32  TxBulkEn:1;        //Enable USB DMA Tx
	    UINT32  RxBulkEn:1;        //Enable USB DMA Rx
	    UINT32  RxBulkAggEn:1;        //Enable Rx Bulk Aggregation
	    UINT32  TxopHalt:1;        //Halt TXOP count down when TX buffer is full.
	    UINT32  TxClear:1;        //Clear USB DMA TX path
	    UINT32  rsv:2;
	    UINT32  phyclear:1;			//phy watch dog enable. write 1
	    UINT32  RxBulkAggLmt:8;        //Rx Bulk Aggregation Limit  in unit of 1024 bytes
	    UINT32  RxBulkAggTOut:8;        //Rx Bulk Aggregation TimeOut  in unit of 33ns
	}	field;
	UINT32			word;
}	USB_DMA_CFG_STRUC, *PUSB_DMA_CFG_STRUC;
#else
typedef	union	_USB_DMA_CFG_STRUC	{
	struct	{
	    UINT32  RxBulkAggTOut:8;        //Rx Bulk Aggregation TimeOut  in unit of 33ns
	    UINT32  RxBulkAggLmt:8;        //Rx Bulk Aggregation Limit  in unit of 256 bytes
	    UINT32  phyclear:1;			//phy watch dog enable. write 1
	    UINT32  rsv:2;
	    UINT32  TxClear:1;        //Clear USB DMA TX path
	    UINT32  TxopHalt:1;        //Halt TXOP count down when TX buffer is full.
	    UINT32  RxBulkAggEn:1;        //Enable Rx Bulk Aggregation
	    UINT32  RxBulkEn:1;        //Enable USB DMA Rx
	    UINT32  TxBulkEn:1;        //Enable USB DMA Tx
	    UINT32  EpoutValid:6;        //OUT endpoint data valid
	    UINT32  RxBusy:1;        //USB DMA RX FSM busy
	    UINT32  TxBusy:1;		//USB DMA TX FSM busy
	}	field;
	UINT32			word;
}	USB_DMA_CFG_STRUC, *PUSB_DMA_CFG_STRUC;
#endif


//
//  3  PBF  registers
//
//
// Most are for debug. Driver doesn't touch PBF register.
#define PBF_SYS_CTRL	 0x0400
#define PBF_CFG                 0x0408
#define PBF_MAX_PCNT	 0x040C
#define PBF_CTRL		0x0410
#define PBF_INT_STA	 0x0414
#define PBF_INT_ENA	 0x0418
#define TXRXQ_PCNT	 0x0438
#define PBF_DBG			 0x043c
#define PBF_CAP_CTRL     0x0440

#ifdef RT30xx
#ifdef RTMP_EFUSE_SUPPORT
// eFuse registers
#define EFUSE_CTRL				0x0580
#define EFUSE_DATA0				0x0590
#define EFUSE_DATA1				0x0594
#define EFUSE_DATA2				0x0598
#define EFUSE_DATA3				0x059c
#endif // RTMP_EFUSE_SUPPORT //
#endif // RT30xx //

#define OSC_CTRL		0x5a4
#define PCIE_PHY_TX_ATTENUATION_CTRL	0x05C8
#define LDO_CFG0				0x05d4
#define GPIO_SWITCH				0x05dc


//
//  4  MAC  registers
//
//
//  4.1 MAC SYSTEM  configuration registers (offset:0x1000)
//
#define MAC_CSR0            0x1000
#ifdef RT_BIG_ENDIAN
typedef	union	_ASIC_VER_ID_STRUC	{
	struct	{
	    USHORT  ASICVer;        // version : 2860
	    USHORT  ASICRev;        // reversion  : 0
	}	field;
	UINT32			word;
}	ASIC_VER_ID_STRUC, *PASIC_VER_ID_STRUC;
#else
typedef	union	_ASIC_VER_ID_STRUC	{
	struct	{
	    USHORT  ASICRev;        // reversion  : 0
	    USHORT  ASICVer;        // version : 2860
	}	field;
	UINT32			word;
}	ASIC_VER_ID_STRUC, *PASIC_VER_ID_STRUC;
#endif
#define MAC_SYS_CTRL            0x1004		//MAC_CSR1
#define MAC_ADDR_DW0				0x1008		// MAC ADDR DW0
#define MAC_ADDR_DW1			 0x100c		// MAC ADDR DW1
//
// MAC_CSR2: STA MAC register 0
//
#ifdef RT_BIG_ENDIAN
typedef	union	_MAC_DW0_STRUC	{
	struct	{
		UCHAR		Byte3;		// MAC address byte 3
		UCHAR		Byte2;		// MAC address byte 2
		UCHAR		Byte1;		// MAC address byte 1
		UCHAR		Byte0;		// MAC address byte 0
	}	field;
	UINT32			word;
}	MAC_DW0_STRUC, *PMAC_DW0_STRUC;
#else
typedef	union	_MAC_DW0_STRUC	{
	struct	{
		UCHAR		Byte0;		// MAC address byte 0
		UCHAR		Byte1;		// MAC address byte 1
		UCHAR		Byte2;		// MAC address byte 2
		UCHAR		Byte3;		// MAC address byte 3
	}	field;
	UINT32			word;
}	MAC_DW0_STRUC, *PMAC_DW0_STRUC;
#endif

//
// MAC_CSR3: STA MAC register 1
//
#ifdef RT_BIG_ENDIAN
typedef	union	_MAC_DW1_STRUC	{
	struct	{
		UCHAR		Rsvd1;
		UCHAR		U2MeMask;
		UCHAR		Byte5;		// MAC address byte 5
		UCHAR		Byte4;		// MAC address byte 4
	}	field;
	UINT32			word;
}	MAC_DW1_STRUC, *PMAC_DW1_STRUC;
#else
typedef	union	_MAC_DW1_STRUC	{
	struct	{
		UCHAR		Byte4;		// MAC address byte 4
		UCHAR		Byte5;		// MAC address byte 5
		UCHAR		U2MeMask;
		UCHAR		Rsvd1;
	}	field;
	UINT32			word;
}	MAC_DW1_STRUC, *PMAC_DW1_STRUC;
#endif

#define MAC_BSSID_DW0				0x1010		// MAC BSSID DW0
#define MAC_BSSID_DW1				0x1014		// MAC BSSID DW1

//
// MAC_CSR5: BSSID register 1
//
#ifdef RT_BIG_ENDIAN
typedef	union	_MAC_CSR5_STRUC	{
	struct	{
		USHORT		Rsvd:11;
		USHORT		MBssBcnNum:3;
		USHORT		BssIdMode:2; // 0: one BSSID, 10: 4 BSSID,  01: 2 BSSID , 11: 8BSSID
		UCHAR		Byte5;		 // BSSID byte 5
		UCHAR		Byte4;		 // BSSID byte 4
	}	field;
	UINT32			word;
}	MAC_CSR5_STRUC, *PMAC_CSR5_STRUC;
#else
typedef	union	_MAC_CSR5_STRUC	{
	struct	{
		UCHAR		Byte4;		 // BSSID byte 4
		UCHAR		Byte5;		 // BSSID byte 5
		USHORT		BssIdMask:2; // 0: one BSSID, 10: 4 BSSID,  01: 2 BSSID , 11: 8BSSID
		USHORT		MBssBcnNum:3;
		USHORT		Rsvd:11;
	}	field;
	UINT32			word;
}	MAC_CSR5_STRUC, *PMAC_CSR5_STRUC;
#endif

#define MAX_LEN_CFG              0x1018		// rt2860b max 16k bytes. bit12:13 Maximum PSDU length (power factor) 0:2^13, 1:2^14, 2:2^15, 3:2^16
#define BBP_CSR_CFG			0x101c		//
//
// BBP_CSR_CFG: BBP serial control register
//
#ifdef RT_BIG_ENDIAN
typedef	union	_BBP_CSR_CFG_STRUC	{
	struct	{
		UINT32		:12;
		UINT32		BBP_RW_MODE:1;		// 0: use serial mode  1:parallel
		UINT32		BBP_PAR_DUR:1;		    // 0: 4 MAC clock cycles  1: 8 MAC clock cycles
		UINT32		Busy:1;				// 1: ASIC is busy execute BBP programming.
		UINT32		fRead:1;		    // 0: Write	BBP, 1:	Read BBP
		UINT32		RegNum:8;			// Selected	BBP	register
		UINT32		Value:8;			// Register	value to program into BBP
	}	field;
	UINT32			word;
}	BBP_CSR_CFG_STRUC, *PBBP_CSR_CFG_STRUC;
#else
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
#endif
#define RF_CSR_CFG0			0x1020
//
// RF_CSR_CFG: RF control register
//
#ifdef RT_BIG_ENDIAN
typedef	union	_RF_CSR_CFG0_STRUC	{
	struct	{
		UINT32		Busy:1;		    // 0: idle 1: 8busy
		UINT32		Sel:1;				// 0:RF_LE0 activate  1:RF_LE1 activate
		UINT32		StandbyMode:1;		    // 0: high when stand by 1:	low when standby
		UINT32		bitwidth:5;			// Selected	BBP	register
		UINT32		RegIdAndContent:24;			// Register	value to program into BBP
	}	field;
	UINT32			word;
}	RF_CSR_CFG0_STRUC, *PRF_CSR_CFG0_STRUC;
#else
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
#endif
#define RF_CSR_CFG1			0x1024
#ifdef RT_BIG_ENDIAN
typedef	union	_RF_CSR_CFG1_STRUC	{
	struct	{
		UINT32		rsv:7;		    // 0: idle 1: 8busy
		UINT32		RFGap:5;			// Gap between BB_CONTROL_RF and RF_LE. 0: 3 system clock cycle (37.5usec) 1: 5 system clock cycle (62.5usec)
		UINT32		RegIdAndContent:24;			// Register	value to program into BBP
	}	field;
	UINT32			word;
}	RF_CSR_CFG1_STRUC, *PRF_CSR_CFG1_STRUC;
#else
typedef	union	_RF_CSR_CFG1_STRUC	{
	struct	{
		UINT32		RegIdAndContent:24;			// Register	value to program into BBP
		UINT32		RFGap:5;			// Gap between BB_CONTROL_RF and RF_LE. 0: 3 system clock cycle (37.5usec) 1: 5 system clock cycle (62.5usec)
		UINT32		rsv:7;		    // 0: idle 1: 8busy
	}	field;
	UINT32			word;
}	RF_CSR_CFG1_STRUC, *PRF_CSR_CFG1_STRUC;
#endif
#define RF_CSR_CFG2			0x1028		//
#ifdef RT_BIG_ENDIAN
typedef	union	_RF_CSR_CFG2_STRUC	{
	struct	{
		UINT32		rsv:8;		    // 0: idle 1: 8busy
		UINT32		RegIdAndContent:24;			// Register	value to program into BBP
	}	field;
	UINT32			word;
}	RF_CSR_CFG2_STRUC, *PRF_CSR_CFG2_STRUC;
#else
typedef	union	_RF_CSR_CFG2_STRUC	{
	struct	{
		UINT32		RegIdAndContent:24;			// Register	value to program into BBP
		UINT32		rsv:8;		    // 0: idle 1: 8busy
	}	field;
	UINT32			word;
}	RF_CSR_CFG2_STRUC, *PRF_CSR_CFG2_STRUC;
#endif
#define LED_CFG				0x102c		//  MAC_CSR14
#ifdef RT_BIG_ENDIAN
typedef	union	_LED_CFG_STRUC	{
	struct	{
		UINT32		:1;
		UINT32		LedPolar:1;			// Led Polarity.  0: active low1: active high
		UINT32		YLedMode:2;			// yellow Led Mode
		UINT32		GLedMode:2;			// green Led Mode
		UINT32		RLedMode:2;			// red Led Mode    0: off1: blinking upon TX2: periodic slow blinking3: always on
		UINT32		rsv:2;
		UINT32		SlowBlinkPeriod:6;			// slow blinking period. unit:1ms
		UINT32		OffPeriod:8;			// blinking off period unit 1ms
		UINT32		OnPeriod:8;			// blinking on period unit 1ms
	}	field;
	UINT32			word;
}	LED_CFG_STRUC, *PLED_CFG_STRUC;
#else
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
#endif
//
//  4.2 MAC TIMING  configuration registers (offset:0x1100)
//
#define XIFS_TIME_CFG             0x1100		 // MAC_CSR8  MAC_CSR9
#ifdef RT_BIG_ENDIAN
typedef	union	_IFS_SLOT_CFG_STRUC	{
	struct	{
	    UINT32  rsv:2;
	    UINT32  BBRxendEnable:1;        //  reference RXEND signal to begin XIFS defer
	    UINT32  EIFS:9;        //  unit 1us
	    UINT32  OfdmXifsTime:4;        //OFDM SIFS. unit 1us. Applied after OFDM RX when MAC doesn't reference BBP signal BBRXEND
	    UINT32  OfdmSifsTime:8;        //  unit 1us. Applied after OFDM RX/TX
	    UINT32  CckmSifsTime:8;        //  unit 1us. Applied after CCK RX/TX
	}	field;
	UINT32			word;
}	IFS_SLOT_CFG_STRUC, *PIFS_SLOT_CFG_STRUC;
#else
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
#endif

#define BKOFF_SLOT_CFG             0x1104		 //  mac_csr9 last 8 bits
#define NAV_TIME_CFG             0x1108		 // NAV  (MAC_CSR15)
#define CH_TIME_CFG             0x110C			// Count as channel busy
#define PBF_LIFE_TIMER             0x1110		 //TX/RX MPDU timestamp timer (free run)Unit: 1us
#define BCN_TIME_CFG             0x1114		 // TXRX_CSR9

#define BCN_OFFSET0				0x042C
#define BCN_OFFSET1				0x0430

//
// BCN_TIME_CFG : Synchronization control register
//
#ifdef RT_BIG_ENDIAN
typedef	union	_BCN_TIME_CFG_STRUC	{
	struct	{
		UINT32		TxTimestampCompensate:8;
        UINT32       :3;
		UINT32		bBeaconGen:1;		// Enable beacon generator
        UINT32       bTBTTEnable:1;
		UINT32		TsfSyncMode:2;		// Enable TSF sync, 00: disable, 01: infra mode, 10: ad-hoc mode
		UINT32		bTsfTicking:1;		// Enable TSF auto counting
		UINT32       BeaconInterval:16;  // in unit of 1/16 TU
	}	field;
	UINT32			word;
}	BCN_TIME_CFG_STRUC, *PBCN_TIME_CFG_STRUC;
#else
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
#endif
#define TBTT_SYNC_CFG            0x1118			// txrx_csr10
#define TSF_TIMER_DW0             0x111C		// Local TSF timer lsb 32 bits. Read-only
#define TSF_TIMER_DW1             0x1120		// msb 32 bits. Read-only.
#define TBTT_TIMER		0x1124			// TImer remains till next TBTT. Read-only.  TXRX_CSR14
#define INT_TIMER_CFG			0x1128			//
#define INT_TIMER_EN			0x112c			//  GP-timer and pre-tbtt Int enable
#define CH_IDLE_STA			0x1130			//  channel idle time
#define CH_BUSY_STA			0x1134			//  channle busy time
//
//  4.2 MAC POWER  configuration registers (offset:0x1200)
//
#define MAC_STATUS_CFG             0x1200		 // old MAC_CSR12
#define PWR_PIN_CFG             0x1204		 // old MAC_CSR12
#define AUTO_WAKEUP_CFG             0x1208		 // old MAC_CSR10
//
// AUTO_WAKEUP_CFG: Manual power control / status register
//
#ifdef RT_BIG_ENDIAN
typedef	union	_AUTO_WAKEUP_STRUC	{
	struct	{
		UINT32		:16;
		UINT32		EnableAutoWakeup:1;	// 0:sleep, 1:awake
		UINT32       NumofSleepingTbtt:7;          // ForceWake has high privilege than PutToSleep when both set
		UINT32       AutoLeadTime:8;
	}	field;
	UINT32			word;
}	AUTO_WAKEUP_STRUC, *PAUTO_WAKEUP_STRUC;
#else
typedef	union	_AUTO_WAKEUP_STRUC	{
	struct	{
		UINT32       AutoLeadTime:8;
		UINT32       NumofSleepingTbtt:7;          // ForceWake has high privilege than PutToSleep when both set
		UINT32		EnableAutoWakeup:1;	// 0:sleep, 1:awake
		UINT32		:16;
	}	field;
	UINT32			word;
}	AUTO_WAKEUP_STRUC, *PAUTO_WAKEUP_STRUC;
#endif
//
//  4.3 MAC TX  configuration registers (offset:0x1300)
//

#define EDCA_AC0_CFG	0x1300		//AC_TXOP_CSR0 0x3474
#define EDCA_AC1_CFG	0x1304
#define EDCA_AC2_CFG	0x1308
#define EDCA_AC3_CFG	0x130c
#ifdef RT_BIG_ENDIAN
typedef	union	_EDCA_AC_CFG_STRUC	{
	struct	{
	    UINT32  :12;        //
	    UINT32  Cwmax:4;        //unit power of 2
	    UINT32  Cwmin:4;        //
	    UINT32  Aifsn:4;        // # of slot time
	    UINT32  AcTxop:8;        //  in unit of 32us
	}	field;
	UINT32			word;
}	EDCA_AC_CFG_STRUC, *PEDCA_AC_CFG_STRUC;
#else
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
#endif

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

#ifdef RT_BIG_ENDIAN
typedef	union	_TX_RTS_CFG_STRUC	{
	struct	{
	    UINT32       rsv:7;
	    UINT32       RtsFbkEn:1;    // enable rts rate fallback
	    UINT32       RtsThres:16;    // unit:byte
	    UINT32       AutoRtsRetryLimit:8;
	}	field;
	UINT32			word;
}	TX_RTS_CFG_STRUC, *PTX_RTS_CFG_STRUC;
#else
typedef	union	_TX_RTS_CFG_STRUC	{
	struct	{
	    UINT32       AutoRtsRetryLimit:8;
	    UINT32       RtsThres:16;    // unit:byte
	    UINT32       RtsFbkEn:1;    // enable rts rate fallback
	    UINT32       rsv:7;     // 1: HT non-STBC control frame enable
	}	field;
	UINT32			word;
}	TX_RTS_CFG_STRUC, *PTX_RTS_CFG_STRUC;
#endif
#define TX_TIMEOUT_CFG	0x1348
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_TIMEOUT_CFG_STRUC	{
	struct	{
	    UINT32       rsv2:8;
	    UINT32       TxopTimeout:8;	//TXOP timeout value for TXOP truncation.  It is recommended that (SLOT_TIME) > (TX_OP_TIMEOUT) > (RX_ACK_TIMEOUT)
	    UINT32       RxAckTimeout:8;	// unit:slot. Used for TX precedure
	    UINT32       MpduLifeTime:4;    //  expiration time = 2^(9+MPDU LIFE TIME)  us
	    UINT32       rsv:4;
	}	field;
	UINT32			word;
}	TX_TIMEOUT_CFG_STRUC, *PTX_TIMEOUT_CFG_STRUC;
#else
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
#endif
#define TX_RTY_CFG	0x134c
#ifdef RT_BIG_ENDIAN
typedef	union PACKED _TX_RTY_CFG_STRUC	{
	struct	{
	    UINT32       rsv:1;
	    UINT32       TxautoFBEnable:1;    // Tx retry PHY rate auto fallback enable
	    UINT32       AggRtyMode:1;	// Aggregate MPDU retry mode.  0:expired by retry limit, 1: expired by mpdu life timer
	    UINT32       NonAggRtyMode:1;	// Non-Aggregate MPDU retry mode.  0:expired by retry limit, 1: expired by mpdu life timer
	    UINT32       LongRtyThre:12;	// Long retry threshoold
	    UINT32       LongRtyLimit:8;	//long retry limit
	    UINT32       ShortRtyLimit:8;	//  short retry limit

	}	field;
	UINT32			word;
}	TX_RTY_CFG_STRUC, *PTX_RTY_CFG_STRUC;
#else
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
#endif
#define TX_LINK_CFG	0x1350
#ifdef RT_BIG_ENDIAN
typedef	union	PACKED _TX_LINK_CFG_STRUC	{
	struct PACKED {
	    UINT32       RemotMFS:8;	//remote MCS feedback sequence number
	    UINT32       RemotMFB:8;    //  remote MCS feedback
	    UINT32       rsv:3;	//
	    UINT32       TxCFAckEn:1;	//   Piggyback CF-ACK enable
	    UINT32       TxRDGEn:1;	// RDG TX enable
	    UINT32       TxMRQEn:1;	//  MCS request TX enable
	    UINT32       RemoteUMFSEnable:1;	//  remote unsolicit  MFB enable.  0: not apply remote remote unsolicit (MFS=7)
	    UINT32       MFBEnable:1;	//  TX apply remote MFB 1:enable
	    UINT32       RemoteMFBLifeTime:8;	//remote MFB life time. unit : 32us
	}	field;
	UINT32			word;
}	TX_LINK_CFG_STRUC, *PTX_LINK_CFG_STRUC;
#else
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
#endif
#define HT_FBK_CFG0	0x1354
#ifdef RT_BIG_ENDIAN
typedef	union PACKED _HT_FBK_CFG0_STRUC	{
	struct	{
	    UINT32       HTMCS7FBK:4;
	    UINT32       HTMCS6FBK:4;
	    UINT32       HTMCS5FBK:4;
	    UINT32       HTMCS4FBK:4;
	    UINT32       HTMCS3FBK:4;
	    UINT32       HTMCS2FBK:4;
	    UINT32       HTMCS1FBK:4;
	    UINT32       HTMCS0FBK:4;
	}	field;
	UINT32			word;
}	HT_FBK_CFG0_STRUC, *PHT_FBK_CFG0_STRUC;
#else
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
#endif
#define HT_FBK_CFG1	0x1358
#ifdef RT_BIG_ENDIAN
typedef	union	_HT_FBK_CFG1_STRUC	{
	struct	{
	    UINT32       HTMCS15FBK:4;
	    UINT32       HTMCS14FBK:4;
	    UINT32       HTMCS13FBK:4;
	    UINT32       HTMCS12FBK:4;
	    UINT32       HTMCS11FBK:4;
	    UINT32       HTMCS10FBK:4;
	    UINT32       HTMCS9FBK:4;
	    UINT32       HTMCS8FBK:4;
	}	field;
	UINT32			word;
}	HT_FBK_CFG1_STRUC, *PHT_FBK_CFG1_STRUC;
#else
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
#endif
#define LG_FBK_CFG0	0x135c
#ifdef RT_BIG_ENDIAN
typedef	union	_LG_FBK_CFG0_STRUC	{
	struct	{
	    UINT32       OFDMMCS7FBK:4;	//initial value is 6
	    UINT32       OFDMMCS6FBK:4;	//initial value is 5
	    UINT32       OFDMMCS5FBK:4;	//initial value is 4
	    UINT32       OFDMMCS4FBK:4;	//initial value is 3
	    UINT32       OFDMMCS3FBK:4;	//initial value is 2
	    UINT32       OFDMMCS2FBK:4;	//initial value is 1
	    UINT32       OFDMMCS1FBK:4;	//initial value is 0
	    UINT32       OFDMMCS0FBK:4;	//initial value is 0
	}	field;
	UINT32			word;
}	LG_FBK_CFG0_STRUC, *PLG_FBK_CFG0_STRUC;
#else
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
#endif
#define LG_FBK_CFG1		0x1360
#ifdef RT_BIG_ENDIAN
typedef	union	_LG_FBK_CFG1_STRUC	{
	struct	{
	    UINT32       rsv:16;
	    UINT32       CCKMCS3FBK:4;	//initial value is 2
	    UINT32       CCKMCS2FBK:4;	//initial value is 1
	    UINT32       CCKMCS1FBK:4;	//initial value is 0
	    UINT32       CCKMCS0FBK:4;	//initial value is 0
	}	field;
	UINT32			word;
}	LG_FBK_CFG1_STRUC, *PLG_FBK_CFG1_STRUC;
#else
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
#endif


//=======================================================
//================ Protection Paramater================================
//=======================================================
#define CCK_PROT_CFG	0x1364		//CCK Protection
#define ASIC_SHORTNAV		1
#define ASIC_LONGNAV		2
#define ASIC_RTS		1
#define ASIC_CTS		2
#ifdef RT_BIG_ENDIAN
typedef	union	_PROT_CFG_STRUC	{
	struct	{
	    UINT32       rsv:5;
	    UINT32       RTSThEn:1;	//RTS threshold enable on CCK TX
	    UINT32       TxopAllowGF40:1;	//CCK TXOP allowance.0:disallow.
	    UINT32       TxopAllowGF20:1;	//CCK TXOP allowance.0:disallow.
	    UINT32       TxopAllowMM40:1;	//CCK TXOP allowance.0:disallow.
	    UINT32       TxopAllowMM20:1;	//CCK TXOP allowance. 0:disallow.
	    UINT32       TxopAllowOfdm:1;	//CCK TXOP allowance.0:disallow.
	    UINT32       TxopAllowCck:1;	//CCK TXOP allowance.0:disallow.
	    UINT32       ProtectNav:2;	//TXOP protection type for CCK TX. 0:None, 1:ShortNAVprotect,  2:LongNAVProtect, 3:rsv
	    UINT32       ProtectCtrl:2;	//Protection control frame type for CCK TX. 1:RTS/CTS, 2:CTS-to-self, 0:None, 3:rsv
	    UINT32       ProtectRate:16;	//Protection control frame rate for CCK TX(RTS/CTS/CFEnd).
	}	field;
	UINT32			word;
}	PROT_CFG_STRUC, *PPROT_CFG_STRUC;
#else
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
#endif

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
#ifdef RT_BIG_ENDIAN
typedef union _AUTO_RSP_CFG_STRUC {
 struct {
     UINT32        :24;
     UINT32       AckCtsPsmBit:1;   // Power bit value in conrtrol frame
     UINT32       DualCTSEn:1;   // Power bit value in conrtrol frame
     UINT32       rsv:1;   // Power bit value in conrtrol frame
     UINT32       AutoResponderPreamble:1;    // 0:long, 1:short preamble
     UINT32       CTS40MRef:1;  // Response CTS 40MHz duplicate mode
     UINT32       CTS40MMode:1;  // Response CTS 40MHz duplicate mode
     UINT32       BACAckPolicyEnable:1;    // 0:long, 1:short preamble
     UINT32       AutoResponderEnable:1;
 } field;
 UINT32   word;
} AUTO_RSP_CFG_STRUC, *PAUTO_RSP_CFG_STRUC;
#else
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
#endif

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
#ifdef RT_BIG_ENDIAN
typedef	union	_RX_STA_CNT0_STRUC	{
	struct	{
	    USHORT  PhyErr;
	    USHORT  CrcErr;
	}	field;
	UINT32			word;
}	RX_STA_CNT0_STRUC, *PRX_STA_CNT0_STRUC;
#else
typedef	union	_RX_STA_CNT0_STRUC	{
	struct	{
	    USHORT  CrcErr;
	    USHORT  PhyErr;
	}	field;
	UINT32			word;
}	RX_STA_CNT0_STRUC, *PRX_STA_CNT0_STRUC;
#endif

//
// RX_STA_CNT1_STRUC: RX False CCA count & RX LONG frame count
//
#ifdef RT_BIG_ENDIAN
typedef	union	_RX_STA_CNT1_STRUC	{
	struct	{
	    USHORT  PlcpErr;
	    USHORT  FalseCca;
	}	field;
	UINT32			word;
}	RX_STA_CNT1_STRUC, *PRX_STA_CNT1_STRUC;
#else
typedef	union	_RX_STA_CNT1_STRUC	{
	struct	{
	    USHORT  FalseCca;
	    USHORT  PlcpErr;
	}	field;
	UINT32			word;
}	RX_STA_CNT1_STRUC, *PRX_STA_CNT1_STRUC;
#endif

//
// RX_STA_CNT2_STRUC:
//
#ifdef RT_BIG_ENDIAN
typedef	union	_RX_STA_CNT2_STRUC	{
	struct	{
	    USHORT  RxFifoOverflowCount;
	    USHORT  RxDupliCount;
	}	field;
	UINT32			word;
}	RX_STA_CNT2_STRUC, *PRX_STA_CNT2_STRUC;
#else
typedef	union	_RX_STA_CNT2_STRUC	{
	struct	{
	    USHORT  RxDupliCount;
	    USHORT  RxFifoOverflowCount;
	}	field;
	UINT32			word;
}	RX_STA_CNT2_STRUC, *PRX_STA_CNT2_STRUC;
#endif
#define TX_STA_CNT0		0x170C		//
//
// STA_CSR3: TX Beacon count
//
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_STA_CNT0_STRUC	{
	struct	{
	    USHORT  TxBeaconCount;
	    USHORT  TxFailCount;
	}	field;
	UINT32			word;
}	TX_STA_CNT0_STRUC, *PTX_STA_CNT0_STRUC;
#else
typedef	union	_TX_STA_CNT0_STRUC	{
	struct	{
	    USHORT  TxFailCount;
	    USHORT  TxBeaconCount;
	}	field;
	UINT32			word;
}	TX_STA_CNT0_STRUC, *PTX_STA_CNT0_STRUC;
#endif
#define TX_STA_CNT1		0x1710		//
//
// TX_STA_CNT1: TX tx count
//
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_STA_CNT1_STRUC	{
	struct	{
	    USHORT  TxRetransmit;
	    USHORT  TxSuccess;
	}	field;
	UINT32			word;
}	TX_STA_CNT1_STRUC, *PTX_STA_CNT1_STRUC;
#else
typedef	union	_TX_STA_CNT1_STRUC	{
	struct	{
	    USHORT  TxSuccess;
	    USHORT  TxRetransmit;
	}	field;
	UINT32			word;
}	TX_STA_CNT1_STRUC, *PTX_STA_CNT1_STRUC;
#endif
#define TX_STA_CNT2		0x1714		//
//
// TX_STA_CNT2: TX tx count
//
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_STA_CNT2_STRUC	{
	struct	{
	    USHORT  TxUnderFlowCount;
	    USHORT  TxZeroLenCount;
	}	field;
	UINT32			word;
}	TX_STA_CNT2_STRUC, *PTX_STA_CNT2_STRUC;
#else
typedef	union	_TX_STA_CNT2_STRUC	{
	struct	{
	    USHORT  TxZeroLenCount;
	    USHORT  TxUnderFlowCount;
	}	field;
	UINT32			word;
}	TX_STA_CNT2_STRUC, *PTX_STA_CNT2_STRUC;
#endif
#define TX_STA_FIFO		0x1718		//
//
// TX_STA_FIFO_STRUC: TX Result for specific PID status fifo register
//
#ifdef RT_BIG_ENDIAN
typedef	union PACKED _TX_STA_FIFO_STRUC	{
	struct	{
		UINT32		Reserve:2;
		UINT32		TxBF:1; // 3*3
		UINT32		SuccessRate:13;	//include MCS, mode ,shortGI, BW settingSame format as TXWI Word 0 Bit 31-16.
//		UINT32		SuccessRate:16;	//include MCS, mode ,shortGI, BW settingSame format as TXWI Word 0 Bit 31-16.
		UINT32		wcid:8;		//wireless client index
		UINT32		TxAckRequired:1;    // ack required
		UINT32		TxAggre:1;    // Tx is aggregated
		UINT32		TxSuccess:1;   // Tx success. whether success or not
		UINT32		PidType:4;
		UINT32		bValid:1;   // 1:This register contains a valid TX result
	}	field;
	UINT32			word;
}	TX_STA_FIFO_STRUC, *PTX_STA_FIFO_STRUC;
#else
typedef	union PACKED _TX_STA_FIFO_STRUC	{
	struct	{
		UINT32		bValid:1;   // 1:This register contains a valid TX result
		UINT32		PidType:4;
		UINT32		TxSuccess:1;   // Tx No retry success
		UINT32		TxAggre:1;    // Tx Retry Success
		UINT32		TxAckRequired:1;    // Tx fail
		UINT32		wcid:8;		//wireless client index
//		UINT32		SuccessRate:16;	//include MCS, mode ,shortGI, BW settingSame format as TXWI Word 0 Bit 31-16.
		UINT32		SuccessRate:13;	//include MCS, mode ,shortGI, BW settingSame format as TXWI Word 0 Bit 31-16.
		UINT32		TxBF:1;
		UINT32		Reserve:2;
	}	field;
	UINT32			word;
}	TX_STA_FIFO_STRUC, *PTX_STA_FIFO_STRUC;
#endif
// Debug counter
#define TX_AGG_CNT	0x171c
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_AGG_CNT_STRUC	{
	struct	{
	    USHORT  AggTxCount;
	    USHORT  NonAggTxCount;
	}	field;
	UINT32			word;
}	TX_AGG_CNT_STRUC, *PTX_AGG_CNT_STRUC;
#else
typedef	union	_TX_AGG_CNT_STRUC	{
	struct	{
	    USHORT  NonAggTxCount;
	    USHORT  AggTxCount;
	}	field;
	UINT32			word;
}	TX_AGG_CNT_STRUC, *PTX_AGG_CNT_STRUC;
#endif
// Debug counter
#define TX_AGG_CNT0	0x1720
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_AGG_CNT0_STRUC	{
	struct	{
	    USHORT  AggSize2Count;
	    USHORT  AggSize1Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT0_STRUC, *PTX_AGG_CNT0_STRUC;
#else
typedef	union	_TX_AGG_CNT0_STRUC	{
	struct	{
	    USHORT  AggSize1Count;
	    USHORT  AggSize2Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT0_STRUC, *PTX_AGG_CNT0_STRUC;
#endif
// Debug counter
#define TX_AGG_CNT1	0x1724
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_AGG_CNT1_STRUC	{
	struct	{
	    USHORT  AggSize4Count;
	    USHORT  AggSize3Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT1_STRUC, *PTX_AGG_CNT1_STRUC;
#else
typedef	union	_TX_AGG_CNT1_STRUC	{
	struct	{
	    USHORT  AggSize3Count;
	    USHORT  AggSize4Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT1_STRUC, *PTX_AGG_CNT1_STRUC;
#endif
#define TX_AGG_CNT2	0x1728
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_AGG_CNT2_STRUC	{
	struct	{
	    USHORT  AggSize6Count;
	    USHORT  AggSize5Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT2_STRUC, *PTX_AGG_CNT2_STRUC;
#else
typedef	union	_TX_AGG_CNT2_STRUC	{
	struct	{
	    USHORT  AggSize5Count;
	    USHORT  AggSize6Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT2_STRUC, *PTX_AGG_CNT2_STRUC;
#endif
// Debug counter
#define TX_AGG_CNT3	0x172c
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_AGG_CNT3_STRUC	{
	struct	{
	    USHORT  AggSize8Count;
	    USHORT  AggSize7Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT3_STRUC, *PTX_AGG_CNT3_STRUC;
#else
typedef	union	_TX_AGG_CNT3_STRUC	{
	struct	{
	    USHORT  AggSize7Count;
	    USHORT  AggSize8Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT3_STRUC, *PTX_AGG_CNT3_STRUC;
#endif
// Debug counter
#define TX_AGG_CNT4	0x1730
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_AGG_CNT4_STRUC	{
	struct	{
	    USHORT  AggSize10Count;
	    USHORT  AggSize9Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT4_STRUC, *PTX_AGG_CNT4_STRUC;
#else
typedef	union	_TX_AGG_CNT4_STRUC	{
	struct	{
	    USHORT  AggSize9Count;
	    USHORT  AggSize10Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT4_STRUC, *PTX_AGG_CNT4_STRUC;
#endif
#define TX_AGG_CNT5	0x1734
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_AGG_CNT5_STRUC	{
	struct	{
	    USHORT  AggSize12Count;
	    USHORT  AggSize11Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT5_STRUC, *PTX_AGG_CNT5_STRUC;
#else
typedef	union	_TX_AGG_CNT5_STRUC	{
	struct	{
	    USHORT  AggSize11Count;
	    USHORT  AggSize12Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT5_STRUC, *PTX_AGG_CNT5_STRUC;
#endif
#define TX_AGG_CNT6		0x1738
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_AGG_CNT6_STRUC	{
	struct	{
	    USHORT  AggSize14Count;
	    USHORT  AggSize13Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT6_STRUC, *PTX_AGG_CNT6_STRUC;
#else
typedef	union	_TX_AGG_CNT6_STRUC	{
	struct	{
	    USHORT  AggSize13Count;
	    USHORT  AggSize14Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT6_STRUC, *PTX_AGG_CNT6_STRUC;
#endif
#define TX_AGG_CNT7		0x173c
#ifdef RT_BIG_ENDIAN
typedef	union	_TX_AGG_CNT7_STRUC	{
	struct	{
	    USHORT  AggSize16Count;
	    USHORT  AggSize15Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT7_STRUC, *PTX_AGG_CNT7_STRUC;
#else
typedef	union	_TX_AGG_CNT7_STRUC	{
	struct	{
	    USHORT  AggSize15Count;
	    USHORT  AggSize16Count;
	}	field;
	UINT32			word;
}	TX_AGG_CNT7_STRUC, *PTX_AGG_CNT7_STRUC;
#endif
#define MPDU_DENSITY_CNT		0x1740
#ifdef RT_BIG_ENDIAN
typedef	union	_MPDU_DEN_CNT_STRUC	{
	struct	{
	    USHORT  RXZeroDelCount;	//RX zero length delimiter count
	    USHORT  TXZeroDelCount;	//TX zero length delimiter count
	}	field;
	UINT32			word;
}	MPDU_DEN_CNT_STRUC, *PMPDU_DEN_CNT_STRUC;
#else
typedef	union	_MPDU_DEN_CNT_STRUC	{
	struct	{
	    USHORT  TXZeroDelCount;	//TX zero length delimiter count
	    USHORT  RXZeroDelCount;	//RX zero length delimiter count
	}	field;
	UINT32			word;
}	MPDU_DEN_CNT_STRUC, *PMPDU_DEN_CNT_STRUC;
#endif
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
#define WCID_RESERVED			0x6bfc
#define SHARED_KEY_TABLE_BASE       0x6c00      // 32-byte * 16-entry = 512-byte
#define SHARED_KEY_MODE_BASE       0x7000      // 32-byte * 16-entry = 512-byte
#define HW_SHARED_KEY_MODE_SIZE   4
#define SHAREDKEYTABLE			0
#define PAIRWISEKEYTABLE			1


#ifdef RT_BIG_ENDIAN
typedef	union	_SHAREDKEY_MODE_STRUC	{
	struct	{
		UINT32       :1;
		UINT32       Bss1Key3CipherAlg:3;
		UINT32       :1;
		UINT32       Bss1Key2CipherAlg:3;
		UINT32       :1;
		UINT32       Bss1Key1CipherAlg:3;
		UINT32       :1;
		UINT32       Bss1Key0CipherAlg:3;
		UINT32       :1;
		UINT32       Bss0Key3CipherAlg:3;
		UINT32       :1;
		UINT32       Bss0Key2CipherAlg:3;
		UINT32       :1;
		UINT32       Bss0Key1CipherAlg:3;
		UINT32       :1;
		UINT32       Bss0Key0CipherAlg:3;
	}	field;
	UINT32			word;
}	SHAREDKEY_MODE_STRUC, *PSHAREDKEY_MODE_STRUC;
#else
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
#endif
// 64-entry for pairwise key table
typedef struct _HW_WCID_ENTRY {  // 8-byte per entry
    UCHAR   Address[6];
    UCHAR   Rsv[2];
} HW_WCID_ENTRY, PHW_WCID_ENTRY;


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
#ifdef RT_BIG_ENDIAN
typedef	struct	_MAC_ATTRIBUTE_STRUC {
	UINT32		rsv:22;
	UINT32		RXWIUDF:3;
	UINT32		BSSIDIdx:3; //multipleBSS index for the WCID
	UINT32		PairKeyMode:3;
	UINT32		KeyTab:1;	// 0 for shared key table.  1 for pairwise key table
}	MAC_ATTRIBUTE_STRUC, *PMAC_ATTRIBUTE_STRUC;
#else
typedef	struct	_MAC_ATTRIBUTE_STRUC {
	UINT32		KeyTab:1;	// 0 for shared key table.  1 for pairwise key table
	UINT32		PairKeyMode:3;
	UINT32		BSSIDIdx:3; //multipleBSS index for the WCID
	UINT32		RXWIUDF:3;
	UINT32		rsv:22;
}	MAC_ATTRIBUTE_STRUC, *PMAC_ATTRIBUTE_STRUC;
#endif


// =================================================================================
// HOST-MCU communication data structure
// =================================================================================

//
// H2M_MAILBOX_CSR: Host-to-MCU Mailbox
//
#ifdef RT_BIG_ENDIAN
typedef union  _H2M_MAILBOX_STRUC {
    struct {
        UINT32       Owner:8;
        UINT32       CmdToken:8;    // 0xff tells MCU not to report CmdDoneInt after excuting the command
        UINT32       HighByte:8;
        UINT32       LowByte:8;
    }   field;
    UINT32           word;
} H2M_MAILBOX_STRUC, *PH2M_MAILBOX_STRUC;
#else
typedef union  _H2M_MAILBOX_STRUC {
    struct {
        UINT32       LowByte:8;
        UINT32       HighByte:8;
        UINT32       CmdToken:8;
        UINT32       Owner:8;
    }   field;
    UINT32           word;
} H2M_MAILBOX_STRUC, *PH2M_MAILBOX_STRUC;
#endif

//
// M2H_CMD_DONE_CSR: MCU-to-Host command complete indication
//
#ifdef RT_BIG_ENDIAN
typedef union _M2H_CMD_DONE_STRUC {
    struct  {
        UINT32       CmdToken3;
        UINT32       CmdToken2;
        UINT32       CmdToken1;
        UINT32       CmdToken0;
    } field;
    UINT32           word;
} M2H_CMD_DONE_STRUC, *PM2H_CMD_DONE_STRUC;
#else
typedef union _M2H_CMD_DONE_STRUC {
    struct  {
        UINT32       CmdToken0;
        UINT32       CmdToken1;
        UINT32       CmdToken2;
        UINT32       CmdToken3;
    } field;
    UINT32           word;
} M2H_CMD_DONE_STRUC, *PM2H_CMD_DONE_STRUC;
#endif


//NAV_TIME_CFG :NAV
#ifdef RT_BIG_ENDIAN
typedef	union	_NAV_TIME_CFG_STRUC	{
	struct	{
		USHORT		rsv:6;
		USHORT		ZeroSifs:1;               // Applied zero SIFS timer after OFDM RX 0: disable
		USHORT		Eifs:9;               // in unit of 1-us
		UCHAR       SlotTime;    // in unit of 1-us
		UCHAR		Sifs;               // in unit of 1-us
	}	field;
	UINT32			word;
}	NAV_TIME_CFG_STRUC, *PNAV_TIME_CFG_STRUC;
#else
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
#endif


//
// RX_FILTR_CFG:  /RX configuration register
//
#ifdef RT_BIG_ENDIAN
typedef	union	RX_FILTR_CFG_STRUC	{
	struct	{
		UINT32		:15;
		UINT32       DropRsvCntlType:1;

		UINT32		DropBAR:1;       //
		UINT32		DropBA:1;		//
		UINT32		DropPsPoll:1;		// Drop Ps-Poll
		UINT32		DropRts:1;		// Drop Ps-Poll

		UINT32		DropCts:1;		// Drop Ps-Poll
		UINT32		DropAck:1;		// Drop Ps-Poll
		UINT32		DropCFEnd:1;		// Drop Ps-Poll
		UINT32		DropCFEndAck:1;		// Drop Ps-Poll

		UINT32		DropDuplicate:1;		// Drop duplicate frame
		UINT32		DropBcast:1;		// Drop broadcast frames
		UINT32		DropMcast:1;		// Drop multicast frames
		UINT32		DropVerErr:1;	    // Drop version error frame

		UINT32		DropNotMyBSSID:1;			// Drop fram ToDs bit is true
		UINT32		DropNotToMe:1;		// Drop not to me unicast frame
		UINT32		DropPhyErr:1;		// Drop physical error
		UINT32		DropCRCErr:1;		// Drop CRC error
	}	field;
	UINT32			word;
}	RX_FILTR_CFG_STRUC, *PRX_FILTR_CFG_STRUC;
#else
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
		UINT32		DropBAR:1;       //

		UINT32		DropRsvCntlType:1;
		UINT32		:15;
	}	field;
	UINT32			word;
}	RX_FILTR_CFG_STRUC, *PRX_FILTR_CFG_STRUC;
#endif




//
// PHY_CSR4: RF serial control register
//
#ifdef RT_BIG_ENDIAN
typedef	union	_PHY_CSR4_STRUC	{
	struct	{
		UINT32		Busy:1;				// 1: ASIC is busy execute RF programming.
		UINT32		PLL_LD:1;			// RF PLL_LD status
		UINT32		IFSelect:1;			// 1: select IF	to program,	0: select RF to	program
		UINT32		NumberOfBits:5;		// Number of bits used in RFRegValue (I:20,	RFMD:22)
		UINT32		RFRegValue:24;		// Register	value (include register	id)	serial out to RF/IF	chip.
	}	field;
	UINT32			word;
}	PHY_CSR4_STRUC, *PPHY_CSR4_STRUC;
#else
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
#endif


//
// SEC_CSR5: shared key table security mode register
//
#ifdef RT_BIG_ENDIAN
typedef	union	_SEC_CSR5_STRUC	{
	struct	{
        UINT32       :1;
        UINT32       Bss3Key3CipherAlg:3;
        UINT32       :1;
        UINT32       Bss3Key2CipherAlg:3;
        UINT32       :1;
        UINT32       Bss3Key1CipherAlg:3;
        UINT32       :1;
        UINT32       Bss3Key0CipherAlg:3;
        UINT32       :1;
        UINT32       Bss2Key3CipherAlg:3;
        UINT32       :1;
        UINT32       Bss2Key2CipherAlg:3;
        UINT32       :1;
        UINT32       Bss2Key1CipherAlg:3;
        UINT32       :1;
        UINT32       Bss2Key0CipherAlg:3;
	}	field;
	UINT32			word;
}	SEC_CSR5_STRUC, *PSEC_CSR5_STRUC;
#else
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
#endif


//
// HOST_CMD_CSR: For HOST to interrupt embedded processor
//
#ifdef RT_BIG_ENDIAN
typedef	union	_HOST_CMD_CSR_STRUC	{
	struct	{
	    UINT32   Rsv:24;
	    UINT32   HostCommand:8;
	}	field;
	UINT32			word;
}	HOST_CMD_CSR_STRUC, *PHOST_CMD_CSR_STRUC;
#else
typedef	union	_HOST_CMD_CSR_STRUC	{
	struct	{
	    UINT32   HostCommand:8;
	    UINT32   Rsv:24;
	}	field;
	UINT32			word;
}	HOST_CMD_CSR_STRUC, *PHOST_CMD_CSR_STRUC;
#endif


//
// AIFSN_CSR: AIFSN for each EDCA AC
//



//
// E2PROM_CSR: EEPROM control register
//
#ifdef RT_BIG_ENDIAN
typedef	union	_E2PROM_CSR_STRUC	{
	struct	{
		UINT32		Rsvd:25;
		UINT32       LoadStatus:1;   // 1:loading, 0:done
		UINT32		Type:1;			// 1: 93C46, 0:93C66
		UINT32		EepromDO:1;
		UINT32		EepromDI:1;
		UINT32		EepromCS:1;
		UINT32		EepromSK:1;
		UINT32		Reload:1;		// Reload EEPROM content, write one to reload, self-cleared.
	}	field;
	UINT32			word;
}	E2PROM_CSR_STRUC, *PE2PROM_CSR_STRUC;
#else
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
#endif

//
// QOS_CSR0: TXOP holder address0 register
//
#ifdef RT_BIG_ENDIAN
typedef	union	_QOS_CSR0_STRUC	{
	struct	{
		UCHAR		Byte3;		// MAC address byte 3
		UCHAR		Byte2;		// MAC address byte 2
		UCHAR		Byte1;		// MAC address byte 1
		UCHAR		Byte0;		// MAC address byte 0
	}	field;
	UINT32			word;
}	QOS_CSR0_STRUC, *PQOS_CSR0_STRUC;
#else
typedef	union	_QOS_CSR0_STRUC	{
	struct	{
		UCHAR		Byte0;		// MAC address byte 0
		UCHAR		Byte1;		// MAC address byte 1
		UCHAR		Byte2;		// MAC address byte 2
		UCHAR		Byte3;		// MAC address byte 3
	}	field;
	UINT32			word;
}	QOS_CSR0_STRUC, *PQOS_CSR0_STRUC;
#endif

//
// QOS_CSR1: TXOP holder address1 register
//
#ifdef RT_BIG_ENDIAN
typedef	union	_QOS_CSR1_STRUC	{
	struct	{
		UCHAR		Rsvd1;
		UCHAR		Rsvd0;
		UCHAR		Byte5;		// MAC address byte 5
		UCHAR		Byte4;		// MAC address byte 4
	}	field;
	UINT32			word;
}	QOS_CSR1_STRUC, *PQOS_CSR1_STRUC;
#else
typedef	union	_QOS_CSR1_STRUC	{
	struct	{
		UCHAR		Byte4;		// MAC address byte 4
		UCHAR		Byte5;		// MAC address byte 5
		UCHAR		Rsvd0;
		UCHAR		Rsvd1;
	}	field;
	UINT32			word;
}	QOS_CSR1_STRUC, *PQOS_CSR1_STRUC;
#endif

#define	RF_CSR_CFG	0x500
#ifdef RT_BIG_ENDIAN
typedef	union	_RF_CSR_CFG_STRUC	{
	struct	{
		UINT	Rsvd1:14;				// Reserved
		UINT	RF_CSR_KICK:1;			// kick RF register read/write
		UINT	RF_CSR_WR:1;			// 0: read  1: write
		UINT	Rsvd2:3;				// Reserved
		UINT	TESTCSR_RFACC_REGNUM:5;	// RF register ID
		UINT	RF_CSR_DATA:8;			// DATA
	}	field;
	UINT	word;
}	RF_CSR_CFG_STRUC, *PRF_CSR_CFG_STRUC;
#else
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
#endif


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



// ================================================================
// Tx /	Rx / Mgmt ring descriptor definition
// ================================================================

// the following PID values are used to mark outgoing frame type in TXD->PID so that
// proper TX statistics can be collected based on these categories
// b3-2 of PID field -
#define PID_MGMT			0x05
#define PID_BEACON			0x0c
#define PID_DATA_NORMALUCAST		0x02
#define PID_DATA_AMPDU		0x04
#define PID_DATA_NO_ACK		0x08
#define PID_DATA_NOT_NORM_ACK		0x03
// value domain of pTxD->HostQId (4-bit: 0~15)
#define QID_AC_BK               1   // meet ACI definition in 802.11e
#define QID_AC_BE               0   // meet ACI definition in 802.11e
#define QID_AC_VI               2
#define QID_AC_VO               3
#define QID_HCCA                4
//#define NUM_OF_TX_RING          5
#define NUM_OF_TX_RING          4
#define QID_MGMT                13
#define QID_RX                  14
#define QID_OTHER               15

#endif // __RTMP_MAC_H__ //
