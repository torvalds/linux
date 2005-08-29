/******************************************************************************
 *
 * Name:	skxmac2.c
 * Project:	Gigabit Ethernet Adapters, Common Modules
 * Version:	$Revision: 1.102 $
 * Date:	$Date: 2003/10/02 16:53:58 $
 * Purpose:	Contains functions to initialize the MACs and PHYs
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2002 SysKonnect.
 *	(C)Copyright 2002-2003 Marvell.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

#include "h/skdrv1st.h"
#include "h/skdrv2nd.h"

/* typedefs *******************************************************************/

/* BCOM PHY magic pattern list */
typedef struct s_PhyHack {
	int		PhyReg;		/* Phy register */
	SK_U16	PhyVal;		/* Value to write */
} BCOM_HACK;

/* local variables ************************************************************/

#if (defined(DEBUG) || ((!defined(LINT)) && (!defined(SK_SLIM))))
static const char SysKonnectFileId[] =
	"@(#) $Id: skxmac2.c,v 1.102 2003/10/02 16:53:58 rschmidt Exp $ (C) Marvell.";
#endif

#ifdef GENESIS
BCOM_HACK BcomRegA1Hack[] = {
 { 0x18, 0x0c20 }, { 0x17, 0x0012 }, { 0x15, 0x1104 }, { 0x17, 0x0013 },
 { 0x15, 0x0404 }, { 0x17, 0x8006 }, { 0x15, 0x0132 }, { 0x17, 0x8006 },
 { 0x15, 0x0232 }, { 0x17, 0x800D }, { 0x15, 0x000F }, { 0x18, 0x0420 },
 { 0, 0 }
};
BCOM_HACK BcomRegC0Hack[] = {
 { 0x18, 0x0c20 }, { 0x17, 0x0012 }, { 0x15, 0x1204 }, { 0x17, 0x0013 },
 { 0x15, 0x0A04 }, { 0x18, 0x0420 },
 { 0, 0 }
};
#endif

/* function prototypes ********************************************************/
#ifdef GENESIS
static void	SkXmInitPhyXmac(SK_AC*, SK_IOC, int, SK_BOOL);
static void	SkXmInitPhyBcom(SK_AC*, SK_IOC, int, SK_BOOL);
static int	SkXmAutoNegDoneXmac(SK_AC*, SK_IOC, int);
static int	SkXmAutoNegDoneBcom(SK_AC*, SK_IOC, int);
#endif /* GENESIS */
#ifdef YUKON
static void	SkGmInitPhyMarv(SK_AC*, SK_IOC, int, SK_BOOL);
static int	SkGmAutoNegDoneMarv(SK_AC*, SK_IOC, int);
#endif /* YUKON */
#ifdef OTHER_PHY
static void	SkXmInitPhyLone(SK_AC*, SK_IOC, int, SK_BOOL);
static void	SkXmInitPhyNat (SK_AC*, SK_IOC, int, SK_BOOL);
static int	SkXmAutoNegDoneLone(SK_AC*, SK_IOC, int);
static int	SkXmAutoNegDoneNat (SK_AC*, SK_IOC, int);
#endif /* OTHER_PHY */


#ifdef GENESIS
/******************************************************************************
 *
 *	SkXmPhyRead() - Read from XMAC PHY register
 *
 * Description:	reads a 16-bit word from XMAC PHY or ext. PHY
 *
 * Returns:
 *	nothing
 */
void SkXmPhyRead(
SK_AC	*pAC,			/* Adapter Context */
SK_IOC	IoC,			/* I/O Context */
int		Port,			/* Port Index (MAC_1 + n) */
int		PhyReg,			/* Register Address (Offset) */
SK_U16	SK_FAR *pVal)	/* Pointer to Value */
{
	SK_U16		Mmu;
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port];
	
	/* write the PHY register's address */
	XM_OUT16(IoC, Port, XM_PHY_ADDR, PhyReg | pPrt->PhyAddr);
	
	/* get the PHY register's value */
	XM_IN16(IoC, Port, XM_PHY_DATA, pVal);
	
	if (pPrt->PhyType != SK_PHY_XMAC) {
		do {
			XM_IN16(IoC, Port, XM_MMU_CMD, &Mmu);
			/* wait until 'Ready' is set */
		} while ((Mmu & XM_MMU_PHY_RDY) == 0);

		/* get the PHY register's value */
		XM_IN16(IoC, Port, XM_PHY_DATA, pVal);
	}
}	/* SkXmPhyRead */


/******************************************************************************
 *
 *	SkXmPhyWrite() - Write to XMAC PHY register
 *
 * Description:	writes a 16-bit word to XMAC PHY or ext. PHY
 *
 * Returns:
 *	nothing
 */
void SkXmPhyWrite(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* I/O Context */
int		Port,		/* Port Index (MAC_1 + n) */
int		PhyReg,		/* Register Address (Offset) */
SK_U16	Val)		/* Value */
{
	SK_U16		Mmu;
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port];
	
	if (pPrt->PhyType != SK_PHY_XMAC) {
		do {
			XM_IN16(IoC, Port, XM_MMU_CMD, &Mmu);
			/* wait until 'Busy' is cleared */
		} while ((Mmu & XM_MMU_PHY_BUSY) != 0);
	}
	
	/* write the PHY register's address */
	XM_OUT16(IoC, Port, XM_PHY_ADDR, PhyReg | pPrt->PhyAddr);
	
	/* write the PHY register's value */
	XM_OUT16(IoC, Port, XM_PHY_DATA, Val);
	
	if (pPrt->PhyType != SK_PHY_XMAC) {
		do {
			XM_IN16(IoC, Port, XM_MMU_CMD, &Mmu);
			/* wait until 'Busy' is cleared */
		} while ((Mmu & XM_MMU_PHY_BUSY) != 0);
	}
}	/* SkXmPhyWrite */
#endif /* GENESIS */


#ifdef YUKON
/******************************************************************************
 *
 *	SkGmPhyRead() - Read from GPHY register
 *
 * Description:	reads a 16-bit word from GPHY through MDIO
 *
 * Returns:
 *	nothing
 */
void SkGmPhyRead(
SK_AC	*pAC,			/* Adapter Context */
SK_IOC	IoC,			/* I/O Context */
int		Port,			/* Port Index (MAC_1 + n) */
int		PhyReg,			/* Register Address (Offset) */
SK_U16	SK_FAR *pVal)	/* Pointer to Value */
{
	SK_U16	Ctrl;
	SK_GEPORT	*pPrt;
#ifdef VCPU
	u_long SimCyle;
	u_long SimLowTime;
	
	VCPUgetTime(&SimCyle, &SimLowTime);
	VCPUprintf(0, "SkGmPhyRead(%u), SimCyle=%u, SimLowTime=%u\n",
		PhyReg, SimCyle, SimLowTime);
#endif /* VCPU */
	
	pPrt = &pAC->GIni.GP[Port];
	
	/* set PHY-Register offset and 'Read' OpCode (= 1) */
	*pVal = (SK_U16)(GM_SMI_CT_PHY_AD(pPrt->PhyAddr) |
		GM_SMI_CT_REG_AD(PhyReg) | GM_SMI_CT_OP_RD);

	GM_OUT16(IoC, Port, GM_SMI_CTRL, *pVal);

	GM_IN16(IoC, Port, GM_SMI_CTRL, &Ctrl);
	
	/* additional check for MDC/MDIO activity */
	if ((Ctrl & GM_SMI_CT_BUSY) == 0) {
		*pVal = 0;
		return;
	}

	*pVal |= GM_SMI_CT_BUSY;
	
	do {
#ifdef VCPU
		VCPUwaitTime(1000);
#endif /* VCPU */

		GM_IN16(IoC, Port, GM_SMI_CTRL, &Ctrl);

	/* wait until 'ReadValid' is set */
	} while (Ctrl == *pVal);
	
	/* get the PHY register's value */
	GM_IN16(IoC, Port, GM_SMI_DATA, pVal);

#ifdef VCPU
	VCPUgetTime(&SimCyle, &SimLowTime);
	VCPUprintf(0, "VCPUgetTime(), SimCyle=%u, SimLowTime=%u\n",
		SimCyle, SimLowTime);
#endif /* VCPU */

}	/* SkGmPhyRead */


/******************************************************************************
 *
 *	SkGmPhyWrite() - Write to GPHY register
 *
 * Description:	writes a 16-bit word to GPHY through MDIO
 *
 * Returns:
 *	nothing
 */
void SkGmPhyWrite(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* I/O Context */
int		Port,		/* Port Index (MAC_1 + n) */
int		PhyReg,		/* Register Address (Offset) */
SK_U16	Val)		/* Value */
{
	SK_U16	Ctrl;
	SK_GEPORT	*pPrt;
#ifdef VCPU
	SK_U32	DWord;
	u_long	SimCyle;
	u_long	SimLowTime;
	
	VCPUgetTime(&SimCyle, &SimLowTime);
	VCPUprintf(0, "SkGmPhyWrite(Reg=%u, Val=0x%04x), SimCyle=%u, SimLowTime=%u\n",
		PhyReg, Val, SimCyle, SimLowTime);
#endif /* VCPU */
	
	pPrt = &pAC->GIni.GP[Port];
	
	/* write the PHY register's value */
	GM_OUT16(IoC, Port, GM_SMI_DATA, Val);
	
	/* set PHY-Register offset and 'Write' OpCode (= 0) */
	Val = GM_SMI_CT_PHY_AD(pPrt->PhyAddr) | GM_SMI_CT_REG_AD(PhyReg);

	GM_OUT16(IoC, Port, GM_SMI_CTRL, Val);

	GM_IN16(IoC, Port, GM_SMI_CTRL, &Ctrl);
	
	/* additional check for MDC/MDIO activity */
	if ((Ctrl & GM_SMI_CT_BUSY) == 0) {
		return;
	}
	
	Val |= GM_SMI_CT_BUSY;

	do {
#ifdef VCPU
		/* read Timer value */
		SK_IN32(IoC, B2_TI_VAL, &DWord);

		VCPUwaitTime(1000);
#endif /* VCPU */

		GM_IN16(IoC, Port, GM_SMI_CTRL, &Ctrl);

	/* wait until 'Busy' is cleared */
	} while (Ctrl == Val);
	
#ifdef VCPU
	VCPUgetTime(&SimCyle, &SimLowTime);
	VCPUprintf(0, "VCPUgetTime(), SimCyle=%u, SimLowTime=%u\n",
		SimCyle, SimLowTime);
#endif /* VCPU */

}	/* SkGmPhyWrite */
#endif /* YUKON */


#ifdef SK_DIAG
/******************************************************************************
 *
 *	SkGePhyRead() - Read from PHY register
 *
 * Description:	calls a read PHY routine dep. on board type
 *
 * Returns:
 *	nothing
 */
void SkGePhyRead(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* I/O Context */
int		Port,		/* Port Index (MAC_1 + n) */
int		PhyReg,		/* Register Address (Offset) */
SK_U16	*pVal)		/* Pointer to Value */
{
	void (*r_func)(SK_AC *pAC, SK_IOC IoC, int Port, int Reg, SK_U16 *pVal);

	if (pAC->GIni.GIGenesis) {
		r_func = SkXmPhyRead;
	}
	else {
		r_func = SkGmPhyRead;
	}
	
	r_func(pAC, IoC, Port, PhyReg, pVal);
}	/* SkGePhyRead */


/******************************************************************************
 *
 *	SkGePhyWrite() - Write to PHY register
 *
 * Description:	calls a write PHY routine dep. on board type
 *
 * Returns:
 *	nothing
 */
void SkGePhyWrite(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* I/O Context */
int		Port,		/* Port Index (MAC_1 + n) */
int		PhyReg,		/* Register Address (Offset) */
SK_U16	Val)		/* Value */
{
	void (*w_func)(SK_AC *pAC, SK_IOC IoC, int Port, int Reg, SK_U16 Val);

	if (pAC->GIni.GIGenesis) {
		w_func = SkXmPhyWrite;
	}
	else {
		w_func = SkGmPhyWrite;
	}
	
	w_func(pAC, IoC, Port, PhyReg, Val);
}	/* SkGePhyWrite */
#endif /* SK_DIAG */


/******************************************************************************
 *
 *	SkMacPromiscMode() - Enable / Disable Promiscuous Mode
 *
 * Description:
 *   enables / disables promiscuous mode by setting Mode Register (XMAC) or
 *   Receive Control Register (GMAC) dep. on board type   	
 *
 * Returns:
 *	nothing
 */
void SkMacPromiscMode(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port,	/* Port Index (MAC_1 + n) */
SK_BOOL	Enable)	/* Enable / Disable */
{
#ifdef YUKON
	SK_U16	RcReg;
#endif
#ifdef GENESIS
	SK_U32	MdReg;
#endif	

#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		
		XM_IN32(IoC, Port, XM_MODE, &MdReg);
		/* enable or disable promiscuous mode */
		if (Enable) {
			MdReg |= XM_MD_ENA_PROM;
		}
		else {
			MdReg &= ~XM_MD_ENA_PROM;
		}
		/* setup Mode Register */
		XM_OUT32(IoC, Port, XM_MODE, MdReg);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		
		GM_IN16(IoC, Port, GM_RX_CTRL, &RcReg);
		
		/* enable or disable unicast and multicast filtering */
		if (Enable) {
			RcReg &= ~(GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA);
		}
		else {
			RcReg |= (GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA);
		}
		/* setup Receive Control Register */
		GM_OUT16(IoC, Port, GM_RX_CTRL, RcReg);
	}
#endif /* YUKON */

}	/* SkMacPromiscMode*/


/******************************************************************************
 *
 *	SkMacHashing() - Enable / Disable Hashing
 *
 * Description:
 *   enables / disables hashing by setting Mode Register (XMAC) or
 *   Receive Control Register (GMAC) dep. on board type		
 *
 * Returns:
 *	nothing
 */
void SkMacHashing(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port,	/* Port Index (MAC_1 + n) */
SK_BOOL	Enable)	/* Enable / Disable */
{
#ifdef YUKON
	SK_U16	RcReg;
#endif	
#ifdef GENESIS
	SK_U32	MdReg;
#endif

#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		
		XM_IN32(IoC, Port, XM_MODE, &MdReg);
		/* enable or disable hashing */
		if (Enable) {
			MdReg |= XM_MD_ENA_HASH;
		}
		else {
			MdReg &= ~XM_MD_ENA_HASH;
		}
		/* setup Mode Register */
		XM_OUT32(IoC, Port, XM_MODE, MdReg);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		
		GM_IN16(IoC, Port, GM_RX_CTRL, &RcReg);
		
		/* enable or disable multicast filtering */
		if (Enable) {
			RcReg |= GM_RXCR_MCF_ENA;
		}
		else {
			RcReg &= ~GM_RXCR_MCF_ENA;
		}
		/* setup Receive Control Register */
		GM_OUT16(IoC, Port, GM_RX_CTRL, RcReg);
	}
#endif /* YUKON */

}	/* SkMacHashing*/


#ifdef SK_DIAG
/******************************************************************************
 *
 *	SkXmSetRxCmd() - Modify the value of the XMAC's Rx Command Register
 *
 * Description:
 *	The features
 *	 - FCS stripping,					SK_STRIP_FCS_ON/OFF
 *	 - pad byte stripping,				SK_STRIP_PAD_ON/OFF
 *	 - don't set XMR_FS_ERR in status	SK_LENERR_OK_ON/OFF
 *	   for inrange length error frames
 *	 - don't set XMR_FS_ERR in status	SK_BIG_PK_OK_ON/OFF
 *	   for frames > 1514 bytes
 *   - enable Rx of own packets         SK_SELF_RX_ON/OFF
 *
 *	for incoming packets may be enabled/disabled by this function.
 *	Additional modes may be added later.
 *	Multiple modes can be enabled/disabled at the same time.
 *	The new configuration is written to the Rx Command register immediately.
 *
 * Returns:
 *	nothing
 */
static void SkXmSetRxCmd(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
int		Mode)		/* Mode is SK_STRIP_FCS_ON/OFF, SK_STRIP_PAD_ON/OFF,
					   SK_LENERR_OK_ON/OFF, or SK_BIG_PK_OK_ON/OFF */
{
	SK_U16	OldRxCmd;
	SK_U16	RxCmd;

	XM_IN16(IoC, Port, XM_RX_CMD, &OldRxCmd);

	RxCmd = OldRxCmd;
	
	switch (Mode & (SK_STRIP_FCS_ON | SK_STRIP_FCS_OFF)) {
	case SK_STRIP_FCS_ON:
		RxCmd |= XM_RX_STRIP_FCS;
		break;
	case SK_STRIP_FCS_OFF:
		RxCmd &= ~XM_RX_STRIP_FCS;
		break;
	}

	switch (Mode & (SK_STRIP_PAD_ON | SK_STRIP_PAD_OFF)) {
	case SK_STRIP_PAD_ON:
		RxCmd |= XM_RX_STRIP_PAD;
		break;
	case SK_STRIP_PAD_OFF:
		RxCmd &= ~XM_RX_STRIP_PAD;
		break;
	}

	switch (Mode & (SK_LENERR_OK_ON | SK_LENERR_OK_OFF)) {
	case SK_LENERR_OK_ON:
		RxCmd |= XM_RX_LENERR_OK;
		break;
	case SK_LENERR_OK_OFF:
		RxCmd &= ~XM_RX_LENERR_OK;
		break;
	}

	switch (Mode & (SK_BIG_PK_OK_ON | SK_BIG_PK_OK_OFF)) {
	case SK_BIG_PK_OK_ON:
		RxCmd |= XM_RX_BIG_PK_OK;
		break;
	case SK_BIG_PK_OK_OFF:
		RxCmd &= ~XM_RX_BIG_PK_OK;
		break;
	}

	switch (Mode & (SK_SELF_RX_ON | SK_SELF_RX_OFF)) {
	case SK_SELF_RX_ON:
		RxCmd |= XM_RX_SELF_RX;
		break;
	case SK_SELF_RX_OFF:
		RxCmd &= ~XM_RX_SELF_RX;
		break;
	}

	/* Write the new mode to the Rx command register if required */
	if (OldRxCmd != RxCmd) {
		XM_OUT16(IoC, Port, XM_RX_CMD, RxCmd);
	}
}	/* SkXmSetRxCmd */


/******************************************************************************
 *
 *	SkGmSetRxCmd() - Modify the value of the GMAC's Rx Control Register
 *
 * Description:
 *	The features
 *	 - FCS (CRC) stripping,				SK_STRIP_FCS_ON/OFF
 *	 - don't set GMR_FS_LONG_ERR		SK_BIG_PK_OK_ON/OFF
 *	   for frames > 1514 bytes
 *   - enable Rx of own packets         SK_SELF_RX_ON/OFF
 *
 *	for incoming packets may be enabled/disabled by this function.
 *	Additional modes may be added later.
 *	Multiple modes can be enabled/disabled at the same time.
 *	The new configuration is written to the Rx Command register immediately.
 *
 * Returns:
 *	nothing
 */
static void SkGmSetRxCmd(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
int		Mode)		/* Mode is SK_STRIP_FCS_ON/OFF, SK_STRIP_PAD_ON/OFF,
					   SK_LENERR_OK_ON/OFF, or SK_BIG_PK_OK_ON/OFF */
{
	SK_U16	OldRxCmd;
	SK_U16	RxCmd;

	if ((Mode & (SK_STRIP_FCS_ON | SK_STRIP_FCS_OFF)) != 0) {
		
		GM_IN16(IoC, Port, GM_RX_CTRL, &OldRxCmd);

		RxCmd = OldRxCmd;

		if ((Mode & SK_STRIP_FCS_ON) != 0) {
			RxCmd |= GM_RXCR_CRC_DIS;
		}
		else {
			RxCmd &= ~GM_RXCR_CRC_DIS;
		}
		/* Write the new mode to the Rx control register if required */
		if (OldRxCmd != RxCmd) {
			GM_OUT16(IoC, Port, GM_RX_CTRL, RxCmd);
		}
	}

	if ((Mode & (SK_BIG_PK_OK_ON | SK_BIG_PK_OK_OFF)) != 0) {
		
		GM_IN16(IoC, Port, GM_SERIAL_MODE, &OldRxCmd);

		RxCmd = OldRxCmd;

		if ((Mode & SK_BIG_PK_OK_ON) != 0) {
			RxCmd |= GM_SMOD_JUMBO_ENA;
		}
		else {
			RxCmd &= ~GM_SMOD_JUMBO_ENA;
		}
		/* Write the new mode to the Rx control register if required */
		if (OldRxCmd != RxCmd) {
			GM_OUT16(IoC, Port, GM_SERIAL_MODE, RxCmd);
		}
	}
}	/* SkGmSetRxCmd */


/******************************************************************************
 *
 *	SkMacSetRxCmd() - Modify the value of the MAC's Rx Control Register
 *
 * Description:	modifies the MAC's Rx Control reg. dep. on board type
 *
 * Returns:
 *	nothing
 */
void SkMacSetRxCmd(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
int		Mode)		/* Rx Mode */
{
	if (pAC->GIni.GIGenesis) {
		
		SkXmSetRxCmd(pAC, IoC, Port, Mode);
	}
	else {
		
		SkGmSetRxCmd(pAC, IoC, Port, Mode);
	}

}	/* SkMacSetRxCmd */


/******************************************************************************
 *
 *	SkMacCrcGener() - Enable / Disable CRC Generation
 *
 * Description:	enables / disables CRC generation dep. on board type
 *
 * Returns:
 *	nothing
 */
void SkMacCrcGener(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port,	/* Port Index (MAC_1 + n) */
SK_BOOL	Enable)	/* Enable / Disable */
{
	SK_U16	Word;

	if (pAC->GIni.GIGenesis) {
		
		XM_IN16(IoC, Port, XM_TX_CMD, &Word);

		if (Enable) {
			Word &= ~XM_TX_NO_CRC;
		}
		else {
			Word |= XM_TX_NO_CRC;
		}
		/* setup Tx Command Register */
		XM_OUT16(IoC, Port, XM_TX_CMD, Word);
	}
	else {
		
		GM_IN16(IoC, Port, GM_TX_CTRL, &Word);
		
		if (Enable) {
			Word &= ~GM_TXCR_CRC_DIS;
		}
		else {
			Word |= GM_TXCR_CRC_DIS;
		}
		/* setup Tx Control Register */
		GM_OUT16(IoC, Port, GM_TX_CTRL, Word);
	}

}	/* SkMacCrcGener*/

#endif /* SK_DIAG */


#ifdef GENESIS
/******************************************************************************
 *
 *	SkXmClrExactAddr() - Clear Exact Match Address Registers
 *
 * Description:
 *	All Exact Match Address registers of the XMAC 'Port' will be
 *	cleared starting with 'StartNum' up to (and including) the
 *	Exact Match address number of 'StopNum'.
 *
 * Returns:
 *	nothing
 */
void SkXmClrExactAddr(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
int		StartNum,	/* Begin with this Address Register Index (0..15) */
int		StopNum)	/* Stop after finished with this Register Idx (0..15) */
{
	int		i;
	SK_U16	ZeroAddr[3] = {0x0000, 0x0000, 0x0000};

	if ((unsigned)StartNum > 15 || (unsigned)StopNum > 15 ||
		StartNum > StopNum) {

		SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_HWI_E001, SKERR_HWI_E001MSG);
		return;
	}

	for (i = StartNum; i <= StopNum; i++) {
		XM_OUTADDR(IoC, Port, XM_EXM(i), &ZeroAddr[0]);
	}
}	/* SkXmClrExactAddr */
#endif /* GENESIS */


/******************************************************************************
 *
 *	SkMacFlushTxFifo() - Flush the MAC's transmit FIFO
 *
 * Description:
 *	Flush the transmit FIFO of the MAC specified by the index 'Port'
 *
 * Returns:
 *	nothing
 */
void SkMacFlushTxFifo(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
#ifdef GENESIS
	SK_U32	MdReg;

	if (pAC->GIni.GIGenesis) {
		
		XM_IN32(IoC, Port, XM_MODE, &MdReg);

		XM_OUT32(IoC, Port, XM_MODE, MdReg | XM_MD_FTF);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		/* no way to flush the FIFO we have to issue a reset */
		/* TBD */
	}
#endif /* YUKON */

}	/* SkMacFlushTxFifo */


/******************************************************************************
 *
 *	SkMacFlushRxFifo() - Flush the MAC's receive FIFO
 *
 * Description:
 *	Flush the receive FIFO of the MAC specified by the index 'Port'
 *
 * Returns:
 *	nothing
 */
void SkMacFlushRxFifo(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
#ifdef GENESIS
	SK_U32	MdReg;

	if (pAC->GIni.GIGenesis) {

		XM_IN32(IoC, Port, XM_MODE, &MdReg);

		XM_OUT32(IoC, Port, XM_MODE, MdReg | XM_MD_FRF);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		/* no way to flush the FIFO we have to issue a reset */
		/* TBD */
	}
#endif /* YUKON */

}	/* SkMacFlushRxFifo */


#ifdef GENESIS
/******************************************************************************
 *
 *	SkXmSoftRst() - Do a XMAC software reset
 *
 * Description:
 *	The PHY registers should not be destroyed during this
 *	kind of software reset. Therefore the XMAC Software Reset
 *	(XM_GP_RES_MAC bit in XM_GP_PORT) must not be used!
 *
 *	The software reset is done by
 *		- disabling the Rx and Tx state machine,
 *		- resetting the statistics module,
 *		- clear all other significant XMAC Mode,
 *		  Command, and Control Registers
 *		- clearing the Hash Register and the
 *		  Exact Match Address registers, and
 *		- flushing the XMAC's Rx and Tx FIFOs.
 *
 * Note:
 *	Another requirement when stopping the XMAC is to
 *	avoid sending corrupted frames on the network.
 *	Disabling the Tx state machine will NOT interrupt
 *	the currently transmitted frame. But we must take care
 *	that the Tx FIFO is cleared AFTER the current frame
 *	is complete sent to the network.
 *
 *	It takes about 12ns to send a frame with 1538 bytes.
 *	One PCI clock goes at least 15ns (66MHz). Therefore
 *	after reading XM_GP_PORT back, we are sure that the
 *	transmitter is disabled AND idle. And this means
 *	we may flush the transmit FIFO now.
 *
 * Returns:
 *	nothing
 */
static void SkXmSoftRst(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
	SK_U16	ZeroAddr[4] = {0x0000, 0x0000, 0x0000, 0x0000};
	
	/* reset the statistics module */
	XM_OUT32(IoC, Port, XM_GP_PORT, XM_GP_RES_STAT);

	/* disable all XMAC IRQs */
	XM_OUT16(IoC, Port, XM_IMSK, 0xffff);
	
	XM_OUT32(IoC, Port, XM_MODE, 0);		/* clear Mode Reg */
	
	XM_OUT16(IoC, Port, XM_TX_CMD, 0);		/* reset TX CMD Reg */
	XM_OUT16(IoC, Port, XM_RX_CMD, 0);		/* reset RX CMD Reg */
	
	/* disable all PHY IRQs */
	switch (pAC->GIni.GP[Port].PhyType) {
	case SK_PHY_BCOM:
			SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_INT_MASK, 0xffff);
			break;
#ifdef OTHER_PHY
		case SK_PHY_LONE:
			SkXmPhyWrite(pAC, IoC, Port, PHY_LONE_INT_ENAB, 0);
			break;
		case SK_PHY_NAT:
			/* todo: National
			 SkXmPhyWrite(pAC, IoC, Port, PHY_NAT_INT_MASK, 0xffff); */
			break;
#endif /* OTHER_PHY */
	}

	/* clear the Hash Register */
	XM_OUTHASH(IoC, Port, XM_HSM, &ZeroAddr);

	/* clear the Exact Match Address registers */
	SkXmClrExactAddr(pAC, IoC, Port, 0, 15);
	
	/* clear the Source Check Address registers */
	XM_OUTHASH(IoC, Port, XM_SRC_CHK, &ZeroAddr);

}	/* SkXmSoftRst */


/******************************************************************************
 *
 *	SkXmHardRst() - Do a XMAC hardware reset
 *
 * Description:
 *	The XMAC of the specified 'Port' and all connected devices
 *	(PHY and SERDES) will receive a reset signal on its *Reset pins.
 *	External PHYs must be reset by clearing a bit in the GPIO register
 *  (Timing requirements: Broadcom: 400ns, Level One: none, National: 80ns).
 *
 * ATTENTION:
 * 	It is absolutely necessary to reset the SW_RST Bit first
 *	before calling this function.
 *
 * Returns:
 *	nothing
 */
static void SkXmHardRst(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
	SK_U32	Reg;
	int		i;
	int		TOut;
	SK_U16	Word;

	for (i = 0; i < 4; i++) {
		/* TX_MFF_CTRL1 has 32 bits, but only the lowest 16 bits are used */
		SK_OUT16(IoC, MR_ADDR(Port, TX_MFF_CTRL1), MFF_CLR_MAC_RST);

		TOut = 0;
		do {
			if (TOut++ > 10000) {
				/*
				 * Adapter seems to be in RESET state.
				 * Registers cannot be written.
				 */
				return;
			}

			SK_OUT16(IoC, MR_ADDR(Port, TX_MFF_CTRL1), MFF_SET_MAC_RST);
			
			SK_IN16(IoC, MR_ADDR(Port, TX_MFF_CTRL1), &Word);
		
		} while ((Word & MFF_SET_MAC_RST) == 0);
	}

	/* For external PHYs there must be special handling */
	if (pAC->GIni.GP[Port].PhyType != SK_PHY_XMAC) {
		
		SK_IN32(IoC, B2_GP_IO, &Reg);
		
		if (Port == 0) {
			Reg |= GP_DIR_0; 	/* set to output */
			Reg &= ~GP_IO_0;	/* set PHY reset (active low) */
		}
		else {
			Reg |= GP_DIR_2;	/* set to output */
			Reg &= ~GP_IO_2;	/* set PHY reset (active low) */
		}
		/* reset external PHY */
		SK_OUT32(IoC, B2_GP_IO, Reg);

		/* short delay */
		SK_IN32(IoC, B2_GP_IO, &Reg);
	}
}	/* SkXmHardRst */


/******************************************************************************
 *
 *	SkXmClearRst() - Release the PHY & XMAC reset
 *
 * Description:
 *
 * Returns:
 *	nothing
 */
static void SkXmClearRst(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
	SK_U32	DWord;
	
	/* clear HW reset */
	SK_OUT16(IoC, MR_ADDR(Port, TX_MFF_CTRL1), MFF_CLR_MAC_RST);

	if (pAC->GIni.GP[Port].PhyType != SK_PHY_XMAC) {

		SK_IN32(IoC, B2_GP_IO, &DWord);

		if (Port == 0) {
			DWord |= (GP_DIR_0 | GP_IO_0); /* set to output */
		}
		else {
			DWord |= (GP_DIR_2 | GP_IO_2); /* set to output */
		}
		/* Clear PHY reset */
		SK_OUT32(IoC, B2_GP_IO, DWord);

		/* Enable GMII interface */
		XM_OUT16(IoC, Port, XM_HW_CFG, XM_HW_GMII_MD);
	}
}	/* SkXmClearRst */
#endif /* GENESIS */


#ifdef YUKON
/******************************************************************************
 *
 *	SkGmSoftRst() - Do a GMAC software reset
 *
 * Description:
 *	The GPHY registers should not be destroyed during this
 *	kind of software reset.
 *
 * Returns:
 *	nothing
 */
static void SkGmSoftRst(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
	SK_U16	EmptyHash[4] = {0x0000, 0x0000, 0x0000, 0x0000};
	SK_U16  RxCtrl;

	/* reset the statistics module */

	/* disable all GMAC IRQs */
	SK_OUT8(IoC, GMAC_IRQ_MSK, 0);
	
	/* disable all PHY IRQs */
	SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_INT_MASK, 0);
	
	/* clear the Hash Register */
	GM_OUTHASH(IoC, Port, GM_MC_ADDR_H1, EmptyHash);

	/* Enable Unicast and Multicast filtering */
	GM_IN16(IoC, Port, GM_RX_CTRL, &RxCtrl);
	
	GM_OUT16(IoC, Port, GM_RX_CTRL,
		(SK_U16)(RxCtrl | GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA));

}	/* SkGmSoftRst */


/******************************************************************************
 *
 *	SkGmHardRst() - Do a GMAC hardware reset
 *
 * Description:
 *
 * Returns:
 *	nothing
 */
static void SkGmHardRst(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
	SK_U32	DWord;
	
	/* WA code for COMA mode */
	if (pAC->GIni.GIYukonLite &&
		pAC->GIni.GIChipRev >= CHIP_REV_YU_LITE_A3) {
		
		SK_IN32(IoC, B2_GP_IO, &DWord);

		DWord |= (GP_DIR_9 | GP_IO_9);

		/* set PHY reset */
		SK_OUT32(IoC, B2_GP_IO, DWord);
	}

	/* set GPHY Control reset */
	SK_OUT32(IoC, MR_ADDR(Port, GPHY_CTRL), GPC_RST_SET);

	/* set GMAC Control reset */
	SK_OUT32(IoC, MR_ADDR(Port, GMAC_CTRL), GMC_RST_SET);

}	/* SkGmHardRst */


/******************************************************************************
 *
 *	SkGmClearRst() - Release the GPHY & GMAC reset
 *
 * Description:
 *
 * Returns:
 *	nothing
 */
static void SkGmClearRst(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
	SK_U32	DWord;
	
#ifdef XXX
		/* clear GMAC Control reset */
		SK_OUT32(IoC, MR_ADDR(Port, GMAC_CTRL), GMC_RST_CLR);

		/* set GMAC Control reset */
		SK_OUT32(IoC, MR_ADDR(Port, GMAC_CTRL), GMC_RST_SET);
#endif /* XXX */

	/* WA code for COMA mode */
	if (pAC->GIni.GIYukonLite &&
		pAC->GIni.GIChipRev >= CHIP_REV_YU_LITE_A3) {
		
		SK_IN32(IoC, B2_GP_IO, &DWord);

		DWord |= GP_DIR_9;		/* set to output */
		DWord &= ~GP_IO_9;		/* clear PHY reset (active high) */

		/* clear PHY reset */
		SK_OUT32(IoC, B2_GP_IO, DWord);
	}

	/* set HWCFG_MODE */
	DWord = GPC_INT_POL_HI | GPC_DIS_FC | GPC_DIS_SLEEP |
		GPC_ENA_XC | GPC_ANEG_ADV_ALL_M | GPC_ENA_PAUSE |
		(pAC->GIni.GICopperType ? GPC_HWCFG_GMII_COP :
		GPC_HWCFG_GMII_FIB);

	/* set GPHY Control reset */
	SK_OUT32(IoC, MR_ADDR(Port, GPHY_CTRL), DWord | GPC_RST_SET);

	/* release GPHY Control reset */
	SK_OUT32(IoC, MR_ADDR(Port, GPHY_CTRL), DWord | GPC_RST_CLR);

#ifdef VCPU
	VCpuWait(9000);
#endif /* VCPU */

	/* clear GMAC Control reset */
	SK_OUT32(IoC, MR_ADDR(Port, GMAC_CTRL), GMC_PAUSE_ON | GMC_RST_CLR);

#ifdef VCPU
	VCpuWait(2000);
	
	SK_IN32(IoC, MR_ADDR(Port, GPHY_CTRL), &DWord);
			
	SK_IN32(IoC, B0_ISRC, &DWord);
#endif /* VCPU */

}	/* SkGmClearRst */
#endif /* YUKON */


/******************************************************************************
 *
 *	SkMacSoftRst() - Do a MAC software reset
 *
 * Description:	calls a MAC software reset routine dep. on board type
 *
 * Returns:
 *	nothing
 */
void SkMacSoftRst(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port];

	/* disable receiver and transmitter */
	SkMacRxTxDisable(pAC, IoC, Port);

#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		
		SkXmSoftRst(pAC, IoC, Port);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		
		SkGmSoftRst(pAC, IoC, Port);
	}
#endif /* YUKON */

	/* flush the MAC's Rx and Tx FIFOs */
	SkMacFlushTxFifo(pAC, IoC, Port);
	
	SkMacFlushRxFifo(pAC, IoC, Port);

	pPrt->PState = SK_PRT_STOP;

}	/* SkMacSoftRst */


/******************************************************************************
 *
 *	SkMacHardRst() - Do a MAC hardware reset
 *
 * Description:	calls a MAC hardware reset routine dep. on board type
 *
 * Returns:
 *	nothing
 */
void SkMacHardRst(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
	
#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		
		SkXmHardRst(pAC, IoC, Port);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		
		SkGmHardRst(pAC, IoC, Port);
	}
#endif /* YUKON */

	pAC->GIni.GP[Port].PState = SK_PRT_RESET;

}	/* SkMacHardRst */


/******************************************************************************
 *
 *	SkMacClearRst() - Clear the MAC reset
 *
 * Description:	calls a clear MAC reset routine dep. on board type
 *
 * Returns:
 *	nothing
 */
void SkMacClearRst(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port)	/* Port Index (MAC_1 + n) */
{
	
#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		
		SkXmClearRst(pAC, IoC, Port);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		
		SkGmClearRst(pAC, IoC, Port);
	}
#endif /* YUKON */

}	/* SkMacClearRst */


#ifdef GENESIS
/******************************************************************************
 *
 *	SkXmInitMac() - Initialize the XMAC II
 *
 * Description:
 *	Initialize the XMAC of the specified port.
 *	The XMAC must be reset or stopped before calling this function.
 *
 * Note:
 *	The XMAC's Rx and Tx state machine is still disabled when returning.
 *
 * Returns:
 *	nothing
 */
void SkXmInitMac(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	int			i;
	SK_U16		SWord;

	pPrt = &pAC->GIni.GP[Port];

	if (pPrt->PState == SK_PRT_STOP) {
		/* Port State: SK_PRT_STOP */
		/* Verify that the reset bit is cleared */
		SK_IN16(IoC, MR_ADDR(Port, TX_MFF_CTRL1), &SWord);

		if ((SWord & MFF_SET_MAC_RST) != 0) {
			/* PState does not match HW state */
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_HWI_E006, SKERR_HWI_E006MSG);
			/* Correct it */
			pPrt->PState = SK_PRT_RESET;
		}
	}

	if (pPrt->PState == SK_PRT_RESET) {

		SkXmClearRst(pAC, IoC, Port);

		if (pPrt->PhyType != SK_PHY_XMAC) {
			/* read Id from external PHY (all have the same address) */
			SkXmPhyRead(pAC, IoC, Port, PHY_XMAC_ID1, &pPrt->PhyId1);

			/*
			 * Optimize MDIO transfer by suppressing preamble.
			 * Must be done AFTER first access to BCOM chip.
			 */
			XM_IN16(IoC, Port, XM_MMU_CMD, &SWord);
			
			XM_OUT16(IoC, Port, XM_MMU_CMD, SWord | XM_MMU_NO_PRE);

			if (pPrt->PhyId1 == PHY_BCOM_ID1_C0) {
				/*
				 * Workaround BCOM Errata for the C0 type.
				 * Write magic patterns to reserved registers.
				 */
				i = 0;
				while (BcomRegC0Hack[i].PhyReg != 0) {
					SkXmPhyWrite(pAC, IoC, Port, BcomRegC0Hack[i].PhyReg,
						BcomRegC0Hack[i].PhyVal);
					i++;
				}
			}
			else if (pPrt->PhyId1 == PHY_BCOM_ID1_A1) {
				/*
				 * Workaround BCOM Errata for the A1 type.
				 * Write magic patterns to reserved registers.
				 */
				i = 0;
				while (BcomRegA1Hack[i].PhyReg != 0) {
					SkXmPhyWrite(pAC, IoC, Port, BcomRegA1Hack[i].PhyReg,
						BcomRegA1Hack[i].PhyVal);
					i++;
				}
			}

			/*
			 * Workaround BCOM Errata (#10523) for all BCom PHYs.
			 * Disable Power Management after reset.
			 */
			SkXmPhyRead(pAC, IoC, Port, PHY_BCOM_AUX_CTRL, &SWord);
			
			SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_AUX_CTRL,
				(SK_U16)(SWord | PHY_B_AC_DIS_PM));

			/* PHY LED initialization is done in SkGeXmitLED() */
		}

		/* Dummy read the Interrupt source register */
		XM_IN16(IoC, Port, XM_ISRC, &SWord);
		
		/*
		 * The auto-negotiation process starts immediately after
		 * clearing the reset. The auto-negotiation process should be
		 * started by the SIRQ, therefore stop it here immediately.
		 */
		SkMacInitPhy(pAC, IoC, Port, SK_FALSE);

#ifdef TEST_ONLY
		/* temp. code: enable signal detect */
		/* WARNING: do not override GMII setting above */
		XM_OUT16(IoC, Port, XM_HW_CFG, XM_HW_COM4SIG);
#endif
	}

	/*
	 * configure the XMACs Station Address
	 * B2_MAC_2 = xx xx xx xx xx x1 is programmed to XMAC A
	 * B2_MAC_3 = xx xx xx xx xx x2 is programmed to XMAC B
	 */
	for (i = 0; i < 3; i++) {
		/*
		 * The following 2 statements are together endianess
		 * independent. Remember this when changing.
		 */
		SK_IN16(IoC, (B2_MAC_2 + Port * 8 + i * 2), &SWord);
		
		XM_OUT16(IoC, Port, (XM_SA + i * 2), SWord);
	}

	/* Tx Inter Packet Gap (XM_TX_IPG):	use default */
	/* Tx High Water Mark (XM_TX_HI_WM):	use default */
	/* Tx Low Water Mark (XM_TX_LO_WM):	use default */
	/* Host Request Threshold (XM_HT_THR):	use default */
	/* Rx Request Threshold (XM_RX_THR):	use default */
	/* Rx Low Water Mark (XM_RX_LO_WM):	use default */

	/* configure Rx High Water Mark (XM_RX_HI_WM) */
	XM_OUT16(IoC, Port, XM_RX_HI_WM, SK_XM_RX_HI_WM);

	/* Configure Tx Request Threshold */
	SWord = SK_XM_THR_SL;				/* for single port */

	if (pAC->GIni.GIMacsFound > 1) {
		switch (pAC->GIni.GIPortUsage) {
		case SK_RED_LINK:
			SWord = SK_XM_THR_REDL;		/* redundant link */
			break;
		case SK_MUL_LINK:
			SWord = SK_XM_THR_MULL;		/* load balancing */
			break;
		case SK_JUMBO_LINK:
			SWord = SK_XM_THR_JUMBO;	/* jumbo frames */
			break;
		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_HWI_E014, SKERR_HWI_E014MSG);
			break;
		}
	}
	XM_OUT16(IoC, Port, XM_TX_THR, SWord);

	/* setup register defaults for the Tx Command Register */
	XM_OUT16(IoC, Port, XM_TX_CMD, XM_TX_AUTO_PAD);

	/* setup register defaults for the Rx Command Register */
	SWord = XM_RX_STRIP_FCS | XM_RX_LENERR_OK;

	if (pAC->GIni.GIPortUsage == SK_JUMBO_LINK) {
		SWord |= XM_RX_BIG_PK_OK;
	}

	if (pPrt->PLinkMode == SK_LMODE_HALF) {
		/*
		 * If in manual half duplex mode the other side might be in
		 * full duplex mode, so ignore if a carrier extension is not seen
		 * on frames received
		 */
		SWord |= XM_RX_DIS_CEXT;
	}
	
	XM_OUT16(IoC, Port, XM_RX_CMD, SWord);

	/*
	 * setup register defaults for the Mode Register
	 *	- Don't strip error frames to avoid Store & Forward
	 *	  on the Rx side.
	 *	- Enable 'Check Station Address' bit
	 *	- Enable 'Check Address Array' bit
	 */
	XM_OUT32(IoC, Port, XM_MODE, XM_DEF_MODE);

	/*
	 * Initialize the Receive Counter Event Mask (XM_RX_EV_MSK)
	 *	- Enable all bits excepting 'Octets Rx OK Low CntOv'
	 *	  and 'Octets Rx OK Hi Cnt Ov'.
	 */
	XM_OUT32(IoC, Port, XM_RX_EV_MSK, XMR_DEF_MSK);

	/*
	 * Initialize the Transmit Counter Event Mask (XM_TX_EV_MSK)
	 *	- Enable all bits excepting 'Octets Tx OK Low CntOv'
	 *	  and 'Octets Tx OK Hi Cnt Ov'.
	 */
	XM_OUT32(IoC, Port, XM_TX_EV_MSK, XMT_DEF_MSK);

	/*
	 * Do NOT init XMAC interrupt mask here.
	 * All interrupts remain disable until link comes up!
	 */

	/*
	 * Any additional configuration changes may be done now.
	 * The last action is to enable the Rx and Tx state machine.
	 * This should be done after the auto-negotiation process
	 * has been completed successfully.
	 */
}	/* SkXmInitMac */
#endif /* GENESIS */


#ifdef YUKON
/******************************************************************************
 *
 *	SkGmInitMac() - Initialize the GMAC
 *
 * Description:
 *	Initialize the GMAC of the specified port.
 *	The GMAC must be reset or stopped before calling this function.
 *
 * Note:
 *	The GMAC's Rx and Tx state machine is still disabled when returning.
 *
 * Returns:
 *	nothing
 */
void SkGmInitMac(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	int			i;
	SK_U16		SWord;
	SK_U32		DWord;

	pPrt = &pAC->GIni.GP[Port];

	if (pPrt->PState == SK_PRT_STOP) {
		/* Port State: SK_PRT_STOP */
		/* Verify that the reset bit is cleared */
		SK_IN32(IoC, MR_ADDR(Port, GMAC_CTRL), &DWord);
		
		if ((DWord & GMC_RST_SET) != 0) {
			/* PState does not match HW state */
			SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_HWI_E006, SKERR_HWI_E006MSG);
			/* Correct it */
			pPrt->PState = SK_PRT_RESET;
		}
	}

	if (pPrt->PState == SK_PRT_RESET) {
		
		SkGmHardRst(pAC, IoC, Port);

		SkGmClearRst(pAC, IoC, Port);
		
		/* Auto-negotiation ? */
		if (pPrt->PLinkMode == SK_LMODE_HALF || pPrt->PLinkMode == SK_LMODE_FULL) {
			/* Auto-negotiation disabled */

			/* get General Purpose Control */
			GM_IN16(IoC, Port, GM_GP_CTRL, &SWord);

			/* disable auto-update for speed, duplex and flow-control */
			SWord |= GM_GPCR_AU_ALL_DIS;
			
			/* setup General Purpose Control Register */
			GM_OUT16(IoC, Port, GM_GP_CTRL, SWord);
			
			SWord = GM_GPCR_AU_ALL_DIS;
		}
		else {
			SWord = 0;
		}

		/* speed settings */
		switch (pPrt->PLinkSpeed) {
		case SK_LSPEED_AUTO:
		case SK_LSPEED_1000MBPS:
			SWord |= GM_GPCR_SPEED_1000 | GM_GPCR_SPEED_100;
			break;
		case SK_LSPEED_100MBPS:
			SWord |= GM_GPCR_SPEED_100;
			break;
		case SK_LSPEED_10MBPS:
			break;
		}

		/* duplex settings */
		if (pPrt->PLinkMode != SK_LMODE_HALF) {
			/* set full duplex */
			SWord |= GM_GPCR_DUP_FULL;
		}

		/* flow-control settings */
		switch (pPrt->PFlowCtrlMode) {
		case SK_FLOW_MODE_NONE:
			/* set Pause Off */
			SK_OUT32(IoC, MR_ADDR(Port, GMAC_CTRL), GMC_PAUSE_OFF);
			/* disable Tx & Rx flow-control */
			SWord |= GM_GPCR_FC_TX_DIS | GM_GPCR_FC_RX_DIS | GM_GPCR_AU_FCT_DIS;
			break;
		case SK_FLOW_MODE_LOC_SEND:
			/* disable Rx flow-control */
			SWord |= GM_GPCR_FC_RX_DIS | GM_GPCR_AU_FCT_DIS;
			break;
		case SK_FLOW_MODE_SYMMETRIC:
		case SK_FLOW_MODE_SYM_OR_REM:
			/* enable Tx & Rx flow-control */
			break;
		}

		/* setup General Purpose Control Register */
		GM_OUT16(IoC, Port, GM_GP_CTRL, SWord);

		/* dummy read the Interrupt Source Register */
		SK_IN16(IoC, GMAC_IRQ_SRC, &SWord);
		
#ifndef VCPU
		/* read Id from PHY */
		SkGmPhyRead(pAC, IoC, Port, PHY_MARV_ID1, &pPrt->PhyId1);
		
		SkGmInitPhyMarv(pAC, IoC, Port, SK_FALSE);
#endif /* VCPU */
	}

	(void)SkGmResetCounter(pAC, IoC, Port);

	/* setup Transmit Control Register */
	GM_OUT16(IoC, Port, GM_TX_CTRL, TX_COL_THR(pPrt->PMacColThres));

	/* setup Receive Control Register */
	GM_OUT16(IoC, Port, GM_RX_CTRL, GM_RXCR_UCF_ENA | GM_RXCR_MCF_ENA |
		GM_RXCR_CRC_DIS);

	/* setup Transmit Flow Control Register */
	GM_OUT16(IoC, Port, GM_TX_FLOW_CTRL, 0xffff);

	/* setup Transmit Parameter Register */
#ifdef VCPU
	GM_IN16(IoC, Port, GM_TX_PARAM, &SWord);
#endif /* VCPU */

    SWord = TX_JAM_LEN_VAL(pPrt->PMacJamLen) |
			TX_JAM_IPG_VAL(pPrt->PMacJamIpgVal) |
			TX_IPG_JAM_DATA(pPrt->PMacJamIpgData);
	
	GM_OUT16(IoC, Port, GM_TX_PARAM, SWord);

	/* configure the Serial Mode Register */
#ifdef VCPU
	GM_IN16(IoC, Port, GM_SERIAL_MODE, &SWord);
#endif /* VCPU */
	
	SWord = GM_SMOD_VLAN_ENA | IPG_DATA_VAL(pPrt->PMacIpgData);

	if (pPrt->PMacLimit4) {
		/* reset of collision counter after 4 consecutive collisions */
		SWord |= GM_SMOD_LIMIT_4;
	}

	if (pAC->GIni.GIPortUsage == SK_JUMBO_LINK) {
		/* enable jumbo mode (Max. Frame Length = 9018) */
		SWord |= GM_SMOD_JUMBO_ENA;
	}
	
	GM_OUT16(IoC, Port, GM_SERIAL_MODE, SWord);
	
	/*
	 * configure the GMACs Station Addresses
	 * in PROM you can find our addresses at:
	 * B2_MAC_1 = xx xx xx xx xx x0 virtual address
	 * B2_MAC_2 = xx xx xx xx xx x1 is programmed to GMAC A
	 * B2_MAC_3 = xx xx xx xx xx x2 is reserved for DualPort
	 */

	for (i = 0; i < 3; i++) {
		/*
		 * The following 2 statements are together endianess
		 * independent. Remember this when changing.
		 */
		/* physical address: will be used for pause frames */
		SK_IN16(IoC, (B2_MAC_2 + Port * 8 + i * 2), &SWord);

#ifdef WA_DEV_16
		/* WA for deviation #16 */
		if (pAC->GIni.GIChipId == CHIP_ID_YUKON && pAC->GIni.GIChipRev == 0) {
			/* swap the address bytes */
			SWord = ((SWord & 0xff00) >> 8)	| ((SWord & 0x00ff) << 8);

			/* write to register in reversed order */
			GM_OUT16(IoC, Port, (GM_SRC_ADDR_1L + (2 - i) * 4), SWord);
		}
		else {
			GM_OUT16(IoC, Port, (GM_SRC_ADDR_1L + i * 4), SWord);
		}
#else		
		GM_OUT16(IoC, Port, (GM_SRC_ADDR_1L + i * 4), SWord);
#endif /* WA_DEV_16 */
		
		/* virtual address: will be used for data */
		SK_IN16(IoC, (B2_MAC_1 + Port * 8 + i * 2), &SWord);

		GM_OUT16(IoC, Port, (GM_SRC_ADDR_2L + i * 4), SWord);
		
		/* reset Multicast filtering Hash registers 1-3 */
		GM_OUT16(IoC, Port, GM_MC_ADDR_H1 + 4*i, 0);
	}

	/* reset Multicast filtering Hash register 4 */
	GM_OUT16(IoC, Port, GM_MC_ADDR_H4, 0);

	/* enable interrupt mask for counter overflows */
	GM_OUT16(IoC, Port, GM_TX_IRQ_MSK, 0);
	GM_OUT16(IoC, Port, GM_RX_IRQ_MSK, 0);
	GM_OUT16(IoC, Port, GM_TR_IRQ_MSK, 0);

#if defined(SK_DIAG) || defined(DEBUG)
	/* read General Purpose Status */
	GM_IN16(IoC, Port, GM_GP_STAT, &SWord);
	
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("MAC Stat Reg.=0x%04X\n", SWord));
#endif /* SK_DIAG || DEBUG */

#ifdef SK_DIAG
	c_print("MAC Stat Reg=0x%04X\n", SWord);
#endif /* SK_DIAG */

}	/* SkGmInitMac */
#endif /* YUKON */


#ifdef GENESIS
/******************************************************************************
 *
 *	SkXmInitDupMd() - Initialize the XMACs Duplex Mode
 *
 * Description:
 *	This function initializes the XMACs Duplex Mode.
 *	It should be called after successfully finishing
 *	the Auto-negotiation Process
 *
 * Returns:
 *	nothing
 */
void SkXmInitDupMd(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	switch (pAC->GIni.GP[Port].PLinkModeStatus) {
	case SK_LMODE_STAT_AUTOHALF:
	case SK_LMODE_STAT_HALF:
		/* Configuration Actions for Half Duplex Mode */
		/*
		 * XM_BURST = default value. We are probable not quick
		 * 	enough at the 'XMAC' bus to burst 8kB.
		 *	The XMAC stops bursting if no transmit frames
		 *	are available or the burst limit is exceeded.
		 */
		/* XM_TX_RT_LIM = default value (15) */
		/* XM_TX_STIME = default value (0xff = 4096 bit times) */
		break;
	case SK_LMODE_STAT_AUTOFULL:
	case SK_LMODE_STAT_FULL:
		/* Configuration Actions for Full Duplex Mode */
		/*
		 * The duplex mode is configured by the PHY,
		 * therefore it seems to be that there is nothing
		 * to do here.
		 */
		break;
	case SK_LMODE_STAT_UNKNOWN:
	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_HWI_E007, SKERR_HWI_E007MSG);
		break;
	}
}	/* SkXmInitDupMd */


/******************************************************************************
 *
 *	SkXmInitPauseMd() - initialize the Pause Mode to be used for this port
 *
 * Description:
 *	This function initializes the Pause Mode which should
 *	be used for this port.
 *	It should be called after successfully finishing
 *	the Auto-negotiation Process
 *
 * Returns:
 *	nothing
 */
void SkXmInitPauseMd(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	SK_U32		DWord;
	SK_U16		Word;

	pPrt = &pAC->GIni.GP[Port];

	XM_IN16(IoC, Port, XM_MMU_CMD, &Word);
	
	if (pPrt->PFlowCtrlStatus == SK_FLOW_STAT_NONE ||
		pPrt->PFlowCtrlStatus == SK_FLOW_STAT_LOC_SEND) {

		/* Disable Pause Frame Reception */
		Word |= XM_MMU_IGN_PF;
	}
	else {
		/*
		 * enabling pause frame reception is required for 1000BT
		 * because the XMAC is not reset if the link is going down
		 */
		/* Enable Pause Frame Reception */
		Word &= ~XM_MMU_IGN_PF;
	}	
	
	XM_OUT16(IoC, Port, XM_MMU_CMD, Word);

	XM_IN32(IoC, Port, XM_MODE, &DWord);

	if (pPrt->PFlowCtrlStatus == SK_FLOW_STAT_SYMMETRIC ||
		pPrt->PFlowCtrlStatus == SK_FLOW_STAT_LOC_SEND) {

		/*
		 * Configure Pause Frame Generation
		 * Use internal and external Pause Frame Generation.
		 * Sending pause frames is edge triggered.
		 * Send a Pause frame with the maximum pause time if
		 * internal oder external FIFO full condition occurs.
		 * Send a zero pause time frame to re-start transmission.
		 */

		/* XM_PAUSE_DA = '010000C28001' (default) */

		/* XM_MAC_PTIME = 0xffff (maximum) */
		/* remember this value is defined in big endian (!) */
		XM_OUT16(IoC, Port, XM_MAC_PTIME, 0xffff);

		/* Set Pause Mode in Mode Register */
		DWord |= XM_PAUSE_MODE;

		/* Set Pause Mode in MAC Rx FIFO */
		SK_OUT16(IoC, MR_ADDR(Port, RX_MFF_CTRL1), MFF_ENA_PAUSE);
	}
	else {
		/*
		 * disable pause frame generation is required for 1000BT
		 * because the XMAC is not reset if the link is going down
		 */
		/* Disable Pause Mode in Mode Register */
		DWord &= ~XM_PAUSE_MODE;

		/* Disable Pause Mode in MAC Rx FIFO */
		SK_OUT16(IoC, MR_ADDR(Port, RX_MFF_CTRL1), MFF_DIS_PAUSE);
	}
	
	XM_OUT32(IoC, Port, XM_MODE, DWord);
}	/* SkXmInitPauseMd*/


/******************************************************************************
 *
 *	SkXmInitPhyXmac() - Initialize the XMAC Phy registers
 *
 * Description:	initializes all the XMACs Phy registers
 *
 * Note:
 *
 * Returns:
 *	nothing
 */
static void SkXmInitPhyXmac(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
SK_BOOL	DoLoop)		/* Should a Phy LoopBack be set-up? */
{
	SK_GEPORT	*pPrt;
	SK_U16		Ctrl;

	pPrt = &pAC->GIni.GP[Port];
	Ctrl = 0;
	
	/* Auto-negotiation ? */
	if (pPrt->PLinkMode == SK_LMODE_HALF || pPrt->PLinkMode == SK_LMODE_FULL) {
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("InitPhyXmac: no auto-negotiation Port %d\n", Port));
		/* Set DuplexMode in Config register */
		if (pPrt->PLinkMode == SK_LMODE_FULL) {
			Ctrl |= PHY_CT_DUP_MD;
		}

		/*
		 * Do NOT enable Auto-negotiation here. This would hold
		 * the link down because no IDLEs are transmitted
		 */
	}
	else {
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("InitPhyXmac: with auto-negotiation Port %d\n", Port));
		/* Set Auto-negotiation advertisement */

		/* Set Full/half duplex capabilities */
		switch (pPrt->PLinkMode) {
		case SK_LMODE_AUTOHALF:
			Ctrl |= PHY_X_AN_HD;
			break;
		case SK_LMODE_AUTOFULL:
			Ctrl |= PHY_X_AN_FD;
			break;
		case SK_LMODE_AUTOBOTH:
			Ctrl |= PHY_X_AN_FD | PHY_X_AN_HD;
			break;
		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E015,
				SKERR_HWI_E015MSG);
		}

		/* Set Flow-control capabilities */
		switch (pPrt->PFlowCtrlMode) {
		case SK_FLOW_MODE_NONE:
			Ctrl |= PHY_X_P_NO_PAUSE;
			break;
		case SK_FLOW_MODE_LOC_SEND:
			Ctrl |= PHY_X_P_ASYM_MD;
			break;
		case SK_FLOW_MODE_SYMMETRIC:
			Ctrl |= PHY_X_P_SYM_MD;
			break;
		case SK_FLOW_MODE_SYM_OR_REM:
			Ctrl |= PHY_X_P_BOTH_MD;
			break;
		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E016,
				SKERR_HWI_E016MSG);
		}

		/* Write AutoNeg Advertisement Register */
		SkXmPhyWrite(pAC, IoC, Port, PHY_XMAC_AUNE_ADV, Ctrl);

		/* Restart Auto-negotiation */
		Ctrl = PHY_CT_ANE | PHY_CT_RE_CFG;
	}

	if (DoLoop) {
		/* Set the Phy Loopback bit, too */
		Ctrl |= PHY_CT_LOOP;
	}

	/* Write to the Phy control register */
	SkXmPhyWrite(pAC, IoC, Port, PHY_XMAC_CTRL, Ctrl);
}	/* SkXmInitPhyXmac */


/******************************************************************************
 *
 *	SkXmInitPhyBcom() - Initialize the Broadcom Phy registers
 *
 * Description:	initializes all the Broadcom Phy registers
 *
 * Note:
 *
 * Returns:
 *	nothing
 */
static void SkXmInitPhyBcom(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
SK_BOOL	DoLoop)		/* Should a Phy LoopBack be set-up? */
{
	SK_GEPORT	*pPrt;
	SK_U16		Ctrl1;
	SK_U16		Ctrl2;
	SK_U16		Ctrl3;
	SK_U16		Ctrl4;
	SK_U16		Ctrl5;

	Ctrl1 = PHY_CT_SP1000;
	Ctrl2 = 0;
	Ctrl3 = PHY_SEL_TYPE;
	Ctrl4 = PHY_B_PEC_EN_LTR;
	Ctrl5 = PHY_B_AC_TX_TST;

	pPrt = &pAC->GIni.GP[Port];

	/* manually Master/Slave ? */
	if (pPrt->PMSMode != SK_MS_MODE_AUTO) {
		Ctrl2 |= PHY_B_1000C_MSE;
		
		if (pPrt->PMSMode == SK_MS_MODE_MASTER) {
			Ctrl2 |= PHY_B_1000C_MSC;
		}
	}
	/* Auto-negotiation ? */
	if (pPrt->PLinkMode == SK_LMODE_HALF || pPrt->PLinkMode == SK_LMODE_FULL) {
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("InitPhyBcom: no auto-negotiation Port %d\n", Port));
		/* Set DuplexMode in Config register */
		if (pPrt->PLinkMode == SK_LMODE_FULL) {
			Ctrl1 |= PHY_CT_DUP_MD;
		}

		/* Determine Master/Slave manually if not already done */
		if (pPrt->PMSMode == SK_MS_MODE_AUTO) {
			Ctrl2 |= PHY_B_1000C_MSE;	/* set it to Slave */
		}

		/*
		 * Do NOT enable Auto-negotiation here. This would hold
		 * the link down because no IDLES are transmitted
		 */
	}
	else {
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("InitPhyBcom: with auto-negotiation Port %d\n", Port));
		/* Set Auto-negotiation advertisement */

		/*
		 * Workaround BCOM Errata #1 for the C5 type.
		 * 1000Base-T Link Acquisition Failure in Slave Mode
		 * Set Repeater/DTE bit 10 of the 1000Base-T Control Register
		 */
		Ctrl2 |= PHY_B_1000C_RD;
		
		 /* Set Full/half duplex capabilities */
		switch (pPrt->PLinkMode) {
		case SK_LMODE_AUTOHALF:
			Ctrl2 |= PHY_B_1000C_AHD;
			break;
		case SK_LMODE_AUTOFULL:
			Ctrl2 |= PHY_B_1000C_AFD;
			break;
		case SK_LMODE_AUTOBOTH:
			Ctrl2 |= PHY_B_1000C_AFD | PHY_B_1000C_AHD;
			break;
		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E015,
				SKERR_HWI_E015MSG);
		}

		/* Set Flow-control capabilities */
		switch (pPrt->PFlowCtrlMode) {
		case SK_FLOW_MODE_NONE:
			Ctrl3 |= PHY_B_P_NO_PAUSE;
			break;
		case SK_FLOW_MODE_LOC_SEND:
			Ctrl3 |= PHY_B_P_ASYM_MD;
			break;
		case SK_FLOW_MODE_SYMMETRIC:
			Ctrl3 |= PHY_B_P_SYM_MD;
			break;
		case SK_FLOW_MODE_SYM_OR_REM:
			Ctrl3 |= PHY_B_P_BOTH_MD;
			break;
		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E016,
				SKERR_HWI_E016MSG);
		}

		/* Restart Auto-negotiation */
		Ctrl1 |= PHY_CT_ANE | PHY_CT_RE_CFG;
	}
	
	/* Initialize LED register here? */
	/* No. Please do it in SkDgXmitLed() (if required) and swap
	   init order of LEDs and XMAC. (MAl) */
	
	/* Write 1000Base-T Control Register */
	SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_1000T_CTRL, Ctrl2);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("Set 1000B-T Ctrl Reg=0x%04X\n", Ctrl2));
	
	/* Write AutoNeg Advertisement Register */
	SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_AUNE_ADV, Ctrl3);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("Set Auto-Neg.Adv.Reg=0x%04X\n", Ctrl3));
	
	if (DoLoop) {
		/* Set the Phy Loopback bit, too */
		Ctrl1 |= PHY_CT_LOOP;
	}

	if (pAC->GIni.GIPortUsage == SK_JUMBO_LINK) {
		/* configure FIFO to high latency for transmission of ext. packets */
		Ctrl4 |= PHY_B_PEC_HIGH_LA;

		/* configure reception of extended packets */
		Ctrl5 |= PHY_B_AC_LONG_PACK;

		SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_AUX_CTRL, Ctrl5);
	}

	/* Configure LED Traffic Mode and Jumbo Frame usage if specified */
	SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_P_EXT_CTRL, Ctrl4);
	
	/* Write to the Phy control register */
	SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_CTRL, Ctrl1);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("PHY Control Reg=0x%04X\n", Ctrl1));
}	/* SkXmInitPhyBcom */
#endif /* GENESIS */


#ifdef YUKON
#ifndef SK_SLIM
/******************************************************************************
 *
 *	SkGmEnterLowPowerMode()
 *
 * Description:	
 *	This function sets the Marvell Alaska PHY to the low power mode
 *	given by parameter mode.
 *	The following low power modes are available:
 *		
 *		- Coma Mode (Deep Sleep):
 *			Power consumption: ~15 - 30 mW
 *			The PHY cannot wake up on its own.
 *
 *		- IEEE 22.2.4.1.5 compatible power down mode
 *			Power consumption: ~240 mW
 *			The PHY cannot wake up on its own.
 *
 *		- energy detect mode
 *			Power consumption: ~160 mW
 *			The PHY can wake up on its own by detecting activity
 *			on the CAT 5 cable.
 *
 *		- energy detect plus mode
 *			Power consumption: ~150 mW
 *			The PHY can wake up on its own by detecting activity
 *			on the CAT 5 cable.
 *			Connected devices can be woken up by sending normal link
 *			pulses every one second.
 *
 * Note:
 *
 * Returns:
 *		0: ok
 *		1: error
 */
int SkGmEnterLowPowerMode(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (e.g. MAC_1) */
SK_U8	Mode)		/* low power mode */
{
	SK_U16	Word;
	SK_U32	DWord;
	SK_U8	LastMode;
	int		Ret = 0;

	if (pAC->GIni.GIYukonLite &&
	    pAC->GIni.GIChipRev >= CHIP_REV_YU_LITE_A3) {

		/* save current power mode */
		LastMode = pAC->GIni.GP[Port].PPhyPowerState;
		pAC->GIni.GP[Port].PPhyPowerState = Mode;

		switch (Mode) {
			/* coma mode (deep sleep) */
			case PHY_PM_DEEP_SLEEP:
				/* setup General Purpose Control Register */
				GM_OUT16(IoC, 0, GM_GP_CTRL, GM_GPCR_FL_PASS |
					GM_GPCR_SPEED_100 | GM_GPCR_AU_ALL_DIS);

				/* apply COMA mode workaround */
				SkGmPhyWrite(pAC, IoC, Port, 29, 0x001f);
				SkGmPhyWrite(pAC, IoC, Port, 30, 0xfff3);

				SK_IN32(IoC, PCI_C(PCI_OUR_REG_1), &DWord);

				SK_OUT8(IoC, B2_TST_CTRL1, TST_CFG_WRITE_ON);
				
				/* Set PHY to Coma Mode */
				SK_OUT32(IoC, PCI_C(PCI_OUR_REG_1), DWord | PCI_PHY_COMA);
				
				SK_OUT8(IoC, B2_TST_CTRL1, TST_CFG_WRITE_OFF);

			break;
			
			/* IEEE 22.2.4.1.5 compatible power down mode */
			case PHY_PM_IEEE_POWER_DOWN:
				/*
				 * - disable MAC 125 MHz clock
				 * - allow MAC power down
				 */
				SkGmPhyRead(pAC, IoC, Port, PHY_MARV_PHY_CTRL, &Word);
				Word |= PHY_M_PC_DIS_125CLK;
				Word &=	~PHY_M_PC_MAC_POW_UP;
				SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_PHY_CTRL, Word);

				/*
				 * register changes must be followed by a software
				 * reset to take effect
				 */
				SkGmPhyRead(pAC, IoC, Port, PHY_MARV_CTRL, &Word);
				Word |= PHY_CT_RESET;
				SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_CTRL, Word);

				/* switch IEEE compatible power down mode on */
				SkGmPhyRead(pAC, IoC, Port, PHY_MARV_CTRL, &Word);
				Word |= PHY_CT_PDOWN;
				SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_CTRL, Word);
			break;

			/* energy detect and energy detect plus mode */
			case PHY_PM_ENERGY_DETECT:
			case PHY_PM_ENERGY_DETECT_PLUS:
				/*
				 * - disable MAC 125 MHz clock
				 */
				SkGmPhyRead(pAC, IoC, Port, PHY_MARV_PHY_CTRL, &Word);
				Word |= PHY_M_PC_DIS_125CLK;
				SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_PHY_CTRL, Word);
				
				/* activate energy detect mode 1 */
				SkGmPhyRead(pAC, IoC, Port, PHY_MARV_PHY_CTRL, &Word);

				/* energy detect mode */
				if (Mode == PHY_PM_ENERGY_DETECT) {
					Word |= PHY_M_PC_EN_DET;
				}
				/* energy detect plus mode */
				else {
					Word |= PHY_M_PC_EN_DET_PLUS;
				}

				SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_PHY_CTRL, Word);

				/*
				 * reinitialize the PHY to force a software reset
				 * which is necessary after the register settings
				 * for the energy detect modes.
				 * Furthermore reinitialisation prevents that the
				 * PHY is running out of a stable state.
				 */
				SkGmInitPhyMarv(pAC, IoC, Port, SK_FALSE);
			break;

			/* don't change current power mode */
			default:
				pAC->GIni.GP[Port].PPhyPowerState = LastMode;
				Ret = 1;
			break;
		}
	}
	/* low power modes are not supported by this chip */
	else {
		Ret = 1;
	}

	return(Ret);

}	/* SkGmEnterLowPowerMode */

/******************************************************************************
 *
 *	SkGmLeaveLowPowerMode()
 *
 * Description:	
 *	Leave the current low power mode and switch to normal mode
 *
 * Note:
 *
 * Returns:
 *		0:	ok
 *		1:	error
 */
int SkGmLeaveLowPowerMode(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (e.g. MAC_1) */
{
	SK_U32	DWord;
	SK_U16	Word;
	SK_U8	LastMode;
	int		Ret = 0;

	if (pAC->GIni.GIYukonLite &&
		pAC->GIni.GIChipRev >= CHIP_REV_YU_LITE_A3) {

		/* save current power mode */
		LastMode = pAC->GIni.GP[Port].PPhyPowerState;
		pAC->GIni.GP[Port].PPhyPowerState = PHY_PM_OPERATIONAL_MODE;

		switch (LastMode) {
			/* coma mode (deep sleep) */
			case PHY_PM_DEEP_SLEEP:
				SK_IN32(IoC, PCI_C(PCI_OUR_REG_1), &DWord);

				SK_OUT8(IoC, B2_TST_CTRL1, TST_CFG_WRITE_ON);
				
				/* Release PHY from Coma Mode */
				SK_OUT32(IoC, PCI_C(PCI_OUR_REG_1), DWord & ~PCI_PHY_COMA);
				
				SK_OUT8(IoC, B2_TST_CTRL1, TST_CFG_WRITE_OFF);
				
				SK_IN32(IoC, B2_GP_IO, &DWord);

				/* set to output */
				DWord |= (GP_DIR_9 | GP_IO_9);

				/* set PHY reset */
				SK_OUT32(IoC, B2_GP_IO, DWord);

				DWord &= ~GP_IO_9; /* clear PHY reset (active high) */

				/* clear PHY reset */
				SK_OUT32(IoC, B2_GP_IO, DWord);
			break;
			
			/* IEEE 22.2.4.1.5 compatible power down mode */
			case PHY_PM_IEEE_POWER_DOWN:
				/*
				 * - enable MAC 125 MHz clock
				 * - set MAC power up
				 */
				SkGmPhyRead(pAC, IoC, Port, PHY_MARV_PHY_CTRL, &Word);
				Word &= ~PHY_M_PC_DIS_125CLK;
				Word |=	PHY_M_PC_MAC_POW_UP;
				SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_PHY_CTRL, Word);

				/*
				 * register changes must be followed by a software
				 * reset to take effect
				 */
				SkGmPhyRead(pAC, IoC, Port, PHY_MARV_CTRL, &Word);
				Word |= PHY_CT_RESET;
				SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_CTRL, Word);

				/* switch IEEE compatible power down mode off */
				SkGmPhyRead(pAC, IoC, Port, PHY_MARV_CTRL, &Word);
				Word &= ~PHY_CT_PDOWN;
				SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_CTRL, Word);
			break;

			/* energy detect and energy detect plus mode */
			case PHY_PM_ENERGY_DETECT:
			case PHY_PM_ENERGY_DETECT_PLUS:
				/*
				 * - enable MAC 125 MHz clock
				 */
				SkGmPhyRead(pAC, IoC, Port, PHY_MARV_PHY_CTRL, &Word);
				Word &= ~PHY_M_PC_DIS_125CLK;
				SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_PHY_CTRL, Word);
				
				/* disable energy detect mode */
				SkGmPhyRead(pAC, IoC, Port, PHY_MARV_PHY_CTRL, &Word);
				Word &= ~PHY_M_PC_EN_DET_MSK;
				SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_PHY_CTRL, Word);

				/*
				 * reinitialize the PHY to force a software reset
				 * which is necessary after the register settings
				 * for the energy detect modes.
				 * Furthermore reinitialisation prevents that the
				 * PHY is running out of a stable state.
				 */
				SkGmInitPhyMarv(pAC, IoC, Port, SK_FALSE);
			break;

			/* don't change current power mode */
			default:
				pAC->GIni.GP[Port].PPhyPowerState = LastMode;
				Ret = 1;
			break;
		}
	}
	/* low power modes are not supported by this chip */
	else {
		Ret = 1;
	}

	return(Ret);

}	/* SkGmLeaveLowPowerMode */
#endif /* !SK_SLIM */


/******************************************************************************
 *
 *	SkGmInitPhyMarv() - Initialize the Marvell Phy registers
 *
 * Description:	initializes all the Marvell Phy registers
 *
 * Note:
 *
 * Returns:
 *	nothing
 */
static void SkGmInitPhyMarv(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
SK_BOOL	DoLoop)		/* Should a Phy LoopBack be set-up? */
{
	SK_GEPORT	*pPrt;
	SK_U16		PhyCtrl;
	SK_U16		C1000BaseT;
	SK_U16		AutoNegAdv;
	SK_U16		ExtPhyCtrl;
	SK_U16		LedCtrl;
	SK_BOOL		AutoNeg;
#if defined(SK_DIAG) || defined(DEBUG)
	SK_U16		PhyStat;
	SK_U16		PhyStat1;
	SK_U16		PhySpecStat;
#endif /* SK_DIAG || DEBUG */

	pPrt = &pAC->GIni.GP[Port];

	/* Auto-negotiation ? */
	if (pPrt->PLinkMode == SK_LMODE_HALF || pPrt->PLinkMode == SK_LMODE_FULL) {
		AutoNeg = SK_FALSE;
	}
	else {
		AutoNeg = SK_TRUE;
	}
	
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("InitPhyMarv: Port %d, auto-negotiation %s\n",
		 Port, AutoNeg ? "ON" : "OFF"));

#ifdef VCPU
	VCPUprintf(0, "SkGmInitPhyMarv(), Port=%u, DoLoop=%u\n",
		Port, DoLoop);
#else /* VCPU */
	if (DoLoop) {
		/* Set 'MAC Power up'-bit, set Manual MDI configuration */
		SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_PHY_CTRL,
			PHY_M_PC_MAC_POW_UP);
	}
	else if (AutoNeg && pPrt->PLinkSpeed == SK_LSPEED_AUTO) {
		/* Read Ext. PHY Specific Control */
		SkGmPhyRead(pAC, IoC, Port, PHY_MARV_EXT_CTRL, &ExtPhyCtrl);
		
		ExtPhyCtrl &= ~(PHY_M_EC_M_DSC_MSK | PHY_M_EC_S_DSC_MSK |
			PHY_M_EC_MAC_S_MSK);
		
		ExtPhyCtrl |= PHY_M_EC_MAC_S(MAC_TX_CLK_25_MHZ) |
			PHY_M_EC_M_DSC(0) | PHY_M_EC_S_DSC(1);
	
		SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_EXT_CTRL, ExtPhyCtrl);
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("Set Ext. PHY Ctrl=0x%04X\n", ExtPhyCtrl));
	}

	/* Read PHY Control */
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_CTRL, &PhyCtrl);

	if (!AutoNeg) {
		/* Disable Auto-negotiation */
		PhyCtrl &= ~PHY_CT_ANE;
	}

	PhyCtrl |= PHY_CT_RESET;
	/* Assert software reset */
	SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_CTRL, PhyCtrl);
#endif /* VCPU */

	PhyCtrl = 0 /* PHY_CT_COL_TST */;
	C1000BaseT = 0;
	AutoNegAdv = PHY_SEL_TYPE;

	/* manually Master/Slave ? */
	if (pPrt->PMSMode != SK_MS_MODE_AUTO) {
		/* enable Manual Master/Slave */
		C1000BaseT |= PHY_M_1000C_MSE;
		
		if (pPrt->PMSMode == SK_MS_MODE_MASTER) {
			C1000BaseT |= PHY_M_1000C_MSC;	/* set it to Master */
		}
	}
	
	/* Auto-negotiation ? */
	if (!AutoNeg) {
		
		if (pPrt->PLinkMode == SK_LMODE_FULL) {
			/* Set Full Duplex Mode */
			PhyCtrl |= PHY_CT_DUP_MD;
		}

		/* Set Master/Slave manually if not already done */
		if (pPrt->PMSMode == SK_MS_MODE_AUTO) {
			C1000BaseT |= PHY_M_1000C_MSE;	/* set it to Slave */
		}

		/* Set Speed */
		switch (pPrt->PLinkSpeed) {
		case SK_LSPEED_AUTO:
		case SK_LSPEED_1000MBPS:
			PhyCtrl |= PHY_CT_SP1000;
			break;
		case SK_LSPEED_100MBPS:
			PhyCtrl |= PHY_CT_SP100;
			break;
		case SK_LSPEED_10MBPS:
			break;
		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E019,
				SKERR_HWI_E019MSG);
		}

		if (!DoLoop) {
			PhyCtrl |= PHY_CT_RESET;
		}
	}
	else {
		/* Set Auto-negotiation advertisement */
		
		if (pAC->GIni.GICopperType) {
			/* Set Speed capabilities */
			switch (pPrt->PLinkSpeed) {
			case SK_LSPEED_AUTO:
				C1000BaseT |= PHY_M_1000C_AHD | PHY_M_1000C_AFD;
				AutoNegAdv |= PHY_M_AN_100_FD | PHY_M_AN_100_HD |
					PHY_M_AN_10_FD | PHY_M_AN_10_HD;
				break;
			case SK_LSPEED_1000MBPS:
				C1000BaseT |= PHY_M_1000C_AHD | PHY_M_1000C_AFD;
				break;
			case SK_LSPEED_100MBPS:
				AutoNegAdv |= PHY_M_AN_100_FD | PHY_M_AN_100_HD |
					/* advertise 10Base-T also */
					PHY_M_AN_10_FD | PHY_M_AN_10_HD;
				break;
			case SK_LSPEED_10MBPS:
				AutoNegAdv |= PHY_M_AN_10_FD | PHY_M_AN_10_HD;
				break;
			default:
				SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E019,
					SKERR_HWI_E019MSG);
			}

			/* Set Full/half duplex capabilities */
			switch (pPrt->PLinkMode) {
			case SK_LMODE_AUTOHALF:
				C1000BaseT &= ~PHY_M_1000C_AFD;
				AutoNegAdv &= ~(PHY_M_AN_100_FD | PHY_M_AN_10_FD);
				break;
			case SK_LMODE_AUTOFULL:
				C1000BaseT &= ~PHY_M_1000C_AHD;
				AutoNegAdv &= ~(PHY_M_AN_100_HD | PHY_M_AN_10_HD);
				break;
			case SK_LMODE_AUTOBOTH:
				break;
			default:
				SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E015,
					SKERR_HWI_E015MSG);
			}
			
			/* Set Flow-control capabilities */
			switch (pPrt->PFlowCtrlMode) {
			case SK_FLOW_MODE_NONE:
				AutoNegAdv |= PHY_B_P_NO_PAUSE;
				break;
			case SK_FLOW_MODE_LOC_SEND:
				AutoNegAdv |= PHY_B_P_ASYM_MD;
				break;
			case SK_FLOW_MODE_SYMMETRIC:
				AutoNegAdv |= PHY_B_P_SYM_MD;
				break;
			case SK_FLOW_MODE_SYM_OR_REM:
				AutoNegAdv |= PHY_B_P_BOTH_MD;
				break;
			default:
				SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E016,
					SKERR_HWI_E016MSG);
			}
		}
		else {	/* special defines for FIBER (88E1011S only) */
			
			/* Set Full/half duplex capabilities */
			switch (pPrt->PLinkMode) {
			case SK_LMODE_AUTOHALF:
				AutoNegAdv |= PHY_M_AN_1000X_AHD;
				break;
			case SK_LMODE_AUTOFULL:
				AutoNegAdv |= PHY_M_AN_1000X_AFD;
				break;
			case SK_LMODE_AUTOBOTH:
				AutoNegAdv |= PHY_M_AN_1000X_AHD | PHY_M_AN_1000X_AFD;
				break;
			default:
				SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E015,
					SKERR_HWI_E015MSG);
			}
			
			/* Set Flow-control capabilities */
			switch (pPrt->PFlowCtrlMode) {
			case SK_FLOW_MODE_NONE:
				AutoNegAdv |= PHY_M_P_NO_PAUSE_X;
				break;
			case SK_FLOW_MODE_LOC_SEND:
				AutoNegAdv |= PHY_M_P_ASYM_MD_X;
				break;
			case SK_FLOW_MODE_SYMMETRIC:
				AutoNegAdv |= PHY_M_P_SYM_MD_X;
				break;
			case SK_FLOW_MODE_SYM_OR_REM:
				AutoNegAdv |= PHY_M_P_BOTH_MD_X;
				break;
			default:
				SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E016,
					SKERR_HWI_E016MSG);
			}
		}

		if (!DoLoop) {
			/* Restart Auto-negotiation */
			PhyCtrl |= PHY_CT_ANE | PHY_CT_RE_CFG;
		}
	}
	
#ifdef VCPU
	/*
	 * E-mail from Gu Lin (08-03-2002):
	 */
	
	/* Program PHY register 30 as 16'h0708 for simulation speed up */
	SkGmPhyWrite(pAC, IoC, Port, 30, 0x0700 /* 0x0708 */);
	
	VCpuWait(2000);

#else /* VCPU */
	
	/* Write 1000Base-T Control Register */
	SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_1000T_CTRL, C1000BaseT);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("Set 1000B-T Ctrl =0x%04X\n", C1000BaseT));
	
	/* Write AutoNeg Advertisement Register */
	SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_AUNE_ADV, AutoNegAdv);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("Set Auto-Neg.Adv.=0x%04X\n", AutoNegAdv));
#endif /* VCPU */
	
	if (DoLoop) {
		/* Set the PHY Loopback bit */
		PhyCtrl |= PHY_CT_LOOP;

#ifdef XXX
		/* Program PHY register 16 as 16'h0400 to force link good */
		SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_PHY_CTRL, PHY_M_PC_FL_GOOD);
#endif /* XXX */

#ifndef VCPU
		if (pPrt->PLinkSpeed != SK_LSPEED_AUTO) {
			/* Write Ext. PHY Specific Control */
			SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_EXT_CTRL,
				(SK_U16)((pPrt->PLinkSpeed + 2) << 4));
		}
#endif /* VCPU */
	}
#ifdef TEST_ONLY
	else if (pPrt->PLinkSpeed == SK_LSPEED_10MBPS) {
			/* Write PHY Specific Control */
			SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_PHY_CTRL,
				PHY_M_PC_EN_DET_MSK);
	}
#endif

	/* Write to the PHY Control register */
	SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_CTRL, PhyCtrl);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("Set PHY Ctrl Reg.=0x%04X\n", PhyCtrl));

#ifdef VCPU
	VCpuWait(2000);
#else

	LedCtrl = PHY_M_LED_PULS_DUR(PULS_170MS) | PHY_M_LED_BLINK_RT(BLINK_84MS);

	if ((pAC->GIni.GILedBlinkCtrl & SK_ACT_LED_BLINK) != 0) {
		LedCtrl |= PHY_M_LEDC_RX_CTRL | PHY_M_LEDC_TX_CTRL;
	}

	if ((pAC->GIni.GILedBlinkCtrl & SK_DUP_LED_NORMAL) != 0) {
		LedCtrl |= PHY_M_LEDC_DP_CTRL;
	}
	
	SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_LED_CTRL, LedCtrl);

	if ((pAC->GIni.GILedBlinkCtrl & SK_LED_LINK100_ON) != 0) {
		/* only in forced 100 Mbps mode */
		if (!AutoNeg && pPrt->PLinkSpeed == SK_LSPEED_100MBPS) {

			SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_LED_OVER,
				PHY_M_LED_MO_100(MO_LED_ON));
		}
	}

#ifdef SK_DIAG
	c_print("Set PHY Ctrl=0x%04X\n", PhyCtrl);
	c_print("Set 1000 B-T=0x%04X\n", C1000BaseT);
	c_print("Set Auto-Neg=0x%04X\n", AutoNegAdv);
	c_print("Set Ext Ctrl=0x%04X\n", ExtPhyCtrl);
#endif /* SK_DIAG */

#if defined(SK_DIAG) || defined(DEBUG)
	/* Read PHY Control */
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_CTRL, &PhyCtrl);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("PHY Ctrl Reg.=0x%04X\n", PhyCtrl));
	
	/* Read 1000Base-T Control Register */
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_1000T_CTRL, &C1000BaseT);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("1000B-T Ctrl =0x%04X\n", C1000BaseT));
	
	/* Read AutoNeg Advertisement Register */
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_AUNE_ADV, &AutoNegAdv);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("Auto-Neg.Adv.=0x%04X\n", AutoNegAdv));
	
	/* Read Ext. PHY Specific Control */
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_EXT_CTRL, &ExtPhyCtrl);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("Ext. PHY Ctrl=0x%04X\n", ExtPhyCtrl));
	
	/* Read PHY Status */
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_STAT, &PhyStat);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("PHY Stat Reg.=0x%04X\n", PhyStat));
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_STAT, &PhyStat1);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("PHY Stat Reg.=0x%04X\n", PhyStat1));
	
	/* Read PHY Specific Status */
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_PHY_STAT, &PhySpecStat);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("PHY Spec Stat=0x%04X\n", PhySpecStat));
#endif /* SK_DIAG || DEBUG */

#ifdef SK_DIAG
	c_print("PHY Ctrl Reg=0x%04X\n", PhyCtrl);
	c_print("PHY 1000 Reg=0x%04X\n", C1000BaseT);
	c_print("PHY AnAd Reg=0x%04X\n", AutoNegAdv);
	c_print("Ext Ctrl Reg=0x%04X\n", ExtPhyCtrl);
	c_print("PHY Stat Reg=0x%04X\n", PhyStat);
	c_print("PHY Stat Reg=0x%04X\n", PhyStat1);
	c_print("PHY Spec Reg=0x%04X\n", PhySpecStat);
#endif /* SK_DIAG */

#endif /* VCPU */

}	/* SkGmInitPhyMarv */
#endif /* YUKON */


#ifdef OTHER_PHY
/******************************************************************************
 *
 *	SkXmInitPhyLone() - Initialize the Level One Phy registers
 *
 * Description:	initializes all the Level One Phy registers
 *
 * Note:
 *
 * Returns:
 *	nothing
 */
static void SkXmInitPhyLone(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
SK_BOOL	DoLoop)		/* Should a Phy LoopBack be set-up? */
{
	SK_GEPORT	*pPrt;
	SK_U16		Ctrl1;
	SK_U16		Ctrl2;
	SK_U16		Ctrl3;

	Ctrl1 = PHY_CT_SP1000;
	Ctrl2 = 0;
	Ctrl3 = PHY_SEL_TYPE;

	pPrt = &pAC->GIni.GP[Port];

	/* manually Master/Slave ? */
	if (pPrt->PMSMode != SK_MS_MODE_AUTO) {
		Ctrl2 |= PHY_L_1000C_MSE;
		
		if (pPrt->PMSMode == SK_MS_MODE_MASTER) {
			Ctrl2 |= PHY_L_1000C_MSC;
		}
	}
	/* Auto-negotiation ? */
	if (pPrt->PLinkMode == SK_LMODE_HALF || pPrt->PLinkMode == SK_LMODE_FULL) {
		/*
		 * level one spec say: "1000 Mbps: manual mode not allowed"
		 * but lets see what happens...
		 */
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("InitPhyLone: no auto-negotiation Port %d\n", Port));
		/* Set DuplexMode in Config register */
		if (pPrt->PLinkMode == SK_LMODE_FULL) {
			Ctrl1 |= PHY_CT_DUP_MD;
		}

		/* Determine Master/Slave manually if not already done */
		if (pPrt->PMSMode == SK_MS_MODE_AUTO) {
			Ctrl2 |= PHY_L_1000C_MSE;	/* set it to Slave */
		}

		/*
		 * Do NOT enable Auto-negotiation here. This would hold
		 * the link down because no IDLES are transmitted
		 */
	}
	else {
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("InitPhyLone: with auto-negotiation Port %d\n", Port));
		/* Set Auto-negotiation advertisement */

		/* Set Full/half duplex capabilities */
		switch (pPrt->PLinkMode) {
		case SK_LMODE_AUTOHALF:
			Ctrl2 |= PHY_L_1000C_AHD;
			break;
		case SK_LMODE_AUTOFULL:
			Ctrl2 |= PHY_L_1000C_AFD;
			break;
		case SK_LMODE_AUTOBOTH:
			Ctrl2 |= PHY_L_1000C_AFD | PHY_L_1000C_AHD;
			break;
		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E015,
				SKERR_HWI_E015MSG);
		}

		/* Set Flow-control capabilities */
		switch (pPrt->PFlowCtrlMode) {
		case SK_FLOW_MODE_NONE:
			Ctrl3 |= PHY_L_P_NO_PAUSE;
			break;
		case SK_FLOW_MODE_LOC_SEND:
			Ctrl3 |= PHY_L_P_ASYM_MD;
			break;
		case SK_FLOW_MODE_SYMMETRIC:
			Ctrl3 |= PHY_L_P_SYM_MD;
			break;
		case SK_FLOW_MODE_SYM_OR_REM:
			Ctrl3 |= PHY_L_P_BOTH_MD;
			break;
		default:
			SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E016,
				SKERR_HWI_E016MSG);
		}

		/* Restart Auto-negotiation */
		Ctrl1 = PHY_CT_ANE | PHY_CT_RE_CFG;
	}
	
	/* Write 1000Base-T Control Register */
	SkXmPhyWrite(pAC, IoC, Port, PHY_LONE_1000T_CTRL, Ctrl2);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("1000B-T Ctrl Reg=0x%04X\n", Ctrl2));
	
	/* Write AutoNeg Advertisement Register */
	SkXmPhyWrite(pAC, IoC, Port, PHY_LONE_AUNE_ADV, Ctrl3);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("Auto-Neg.Adv.Reg=0x%04X\n", Ctrl3));

	if (DoLoop) {
		/* Set the Phy Loopback bit, too */
		Ctrl1 |= PHY_CT_LOOP;
	}

	/* Write to the Phy control register */
	SkXmPhyWrite(pAC, IoC, Port, PHY_LONE_CTRL, Ctrl1);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("PHY Control Reg=0x%04X\n", Ctrl1));
}	/* SkXmInitPhyLone */


/******************************************************************************
 *
 *	SkXmInitPhyNat() - Initialize the National Phy registers
 *
 * Description:	initializes all the National Phy registers
 *
 * Note:
 *
 * Returns:
 *	nothing
 */
static void SkXmInitPhyNat(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
SK_BOOL	DoLoop)		/* Should a Phy LoopBack be set-up? */
{
/* todo: National */
}	/* SkXmInitPhyNat */
#endif /* OTHER_PHY */


/******************************************************************************
 *
 *	SkMacInitPhy() - Initialize the PHY registers
 *
 * Description:	calls the Init PHY routines dep. on board type
 *
 * Note:
 *
 * Returns:
 *	nothing
 */
void SkMacInitPhy(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
SK_BOOL	DoLoop)		/* Should a Phy LoopBack be set-up? */
{
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port];

#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		
		switch (pPrt->PhyType) {
		case SK_PHY_XMAC:
			SkXmInitPhyXmac(pAC, IoC, Port, DoLoop);
			break;
		case SK_PHY_BCOM:
			SkXmInitPhyBcom(pAC, IoC, Port, DoLoop);
			break;
#ifdef OTHER_PHY
		case SK_PHY_LONE:
			SkXmInitPhyLone(pAC, IoC, Port, DoLoop);
			break;
		case SK_PHY_NAT:
			SkXmInitPhyNat(pAC, IoC, Port, DoLoop);
			break;
#endif /* OTHER_PHY */
		}
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		
		SkGmInitPhyMarv(pAC, IoC, Port, DoLoop);
	}
#endif /* YUKON */

}	/* SkMacInitPhy */


#ifdef GENESIS
/******************************************************************************
 *
 *	SkXmAutoNegDoneXmac() - Auto-negotiation handling
 *
 * Description:
 *	This function handles the auto-negotiation if the Done bit is set.
 *
 * Returns:
 *	SK_AND_OK	o.k.
 *	SK_AND_DUP_CAP 	Duplex capability error happened
 *	SK_AND_OTHER 	Other error happened
 */
static int SkXmAutoNegDoneXmac(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	SK_U16		ResAb;		/* Resolved Ability */
	SK_U16		LPAb;		/* Link Partner Ability */

	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("AutoNegDoneXmac, Port %d\n", Port));

	pPrt = &pAC->GIni.GP[Port];

	/* Get PHY parameters */
	SkXmPhyRead(pAC, IoC, Port, PHY_XMAC_AUNE_LP, &LPAb);
	SkXmPhyRead(pAC, IoC, Port, PHY_XMAC_RES_ABI, &ResAb);

	if ((LPAb & PHY_X_AN_RFB) != 0) {
		/* At least one of the remote fault bit is set */
		/* Error */
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("AutoNegFail: Remote fault bit set Port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		return(SK_AND_OTHER);
	}

	/* Check Duplex mismatch */
	if ((ResAb & (PHY_X_RS_HD | PHY_X_RS_FD)) == PHY_X_RS_FD) {
		pPrt->PLinkModeStatus = (SK_U8)SK_LMODE_STAT_AUTOFULL;
	}
	else if ((ResAb & (PHY_X_RS_HD | PHY_X_RS_FD)) == PHY_X_RS_HD) {
		pPrt->PLinkModeStatus = (SK_U8)SK_LMODE_STAT_AUTOHALF;
	}
	else {
		/* Error */
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("AutoNegFail: Duplex mode mismatch Port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		return(SK_AND_DUP_CAP);
	}

	/* Check PAUSE mismatch */
	/* We are NOT using chapter 4.23 of the Xaqti manual */
	/* We are using IEEE 802.3z/D5.0 Table 37-4 */
	if ((pPrt->PFlowCtrlMode == SK_FLOW_MODE_SYMMETRIC ||
	     pPrt->PFlowCtrlMode == SK_FLOW_MODE_SYM_OR_REM) &&
	    (LPAb & PHY_X_P_SYM_MD) != 0) {
		/* Symmetric PAUSE */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_SYMMETRIC;
	}
	else if (pPrt->PFlowCtrlMode == SK_FLOW_MODE_SYM_OR_REM &&
		   (LPAb & PHY_X_RS_PAUSE) == PHY_X_P_ASYM_MD) {
		/* Enable PAUSE receive, disable PAUSE transmit */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_REM_SEND;
	}
	else if (pPrt->PFlowCtrlMode == SK_FLOW_MODE_LOC_SEND &&
		   (LPAb & PHY_X_RS_PAUSE) == PHY_X_P_BOTH_MD) {
		/* Disable PAUSE receive, enable PAUSE transmit */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_LOC_SEND;
	}
	else {
		/* PAUSE mismatch -> no PAUSE */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_NONE;
	}
	pPrt->PLinkSpeedUsed = (SK_U8)SK_LSPEED_STAT_1000MBPS;

	return(SK_AND_OK);
}	/* SkXmAutoNegDoneXmac */


/******************************************************************************
 *
 *	SkXmAutoNegDoneBcom() - Auto-negotiation handling
 *
 * Description:
 *	This function handles the auto-negotiation if the Done bit is set.
 *
 * Returns:
 *	SK_AND_OK	o.k.
 *	SK_AND_DUP_CAP 	Duplex capability error happened
 *	SK_AND_OTHER 	Other error happened
 */
static int SkXmAutoNegDoneBcom(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	SK_U16		LPAb;		/* Link Partner Ability */
	SK_U16		AuxStat;	/* Auxiliary Status */

#ifdef TEST_ONLY
01-Sep-2000 RA;:;:
	SK_U16		ResAb;		/* Resolved Ability */
#endif	/* 0 */

	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("AutoNegDoneBcom, Port %d\n", Port));
	pPrt = &pAC->GIni.GP[Port];

	/* Get PHY parameters */
	SkXmPhyRead(pAC, IoC, Port, PHY_BCOM_AUNE_LP, &LPAb);
#ifdef TEST_ONLY
01-Sep-2000 RA;:;:
	SkXmPhyRead(pAC, IoC, Port, PHY_BCOM_1000T_STAT, &ResAb);
#endif	/* 0 */
	
	SkXmPhyRead(pAC, IoC, Port, PHY_BCOM_AUX_STAT, &AuxStat);

	if ((LPAb & PHY_B_AN_RF) != 0) {
		/* Remote fault bit is set: Error */
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("AutoNegFail: Remote fault bit set Port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		return(SK_AND_OTHER);
	}

	/* Check Duplex mismatch */
	if ((AuxStat & PHY_B_AS_AN_RES_MSK) == PHY_B_RES_1000FD) {
		pPrt->PLinkModeStatus = (SK_U8)SK_LMODE_STAT_AUTOFULL;
	}
	else if ((AuxStat & PHY_B_AS_AN_RES_MSK) == PHY_B_RES_1000HD) {
		pPrt->PLinkModeStatus = (SK_U8)SK_LMODE_STAT_AUTOHALF;
	}
	else {
		/* Error */
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("AutoNegFail: Duplex mode mismatch Port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		return(SK_AND_DUP_CAP);
	}
	
#ifdef TEST_ONLY
01-Sep-2000 RA;:;:
	/* Check Master/Slave resolution */
	if ((ResAb & PHY_B_1000S_MSF) != 0) {
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("Master/Slave Fault Port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		pPrt->PMSStatus = SK_MS_STAT_FAULT;
		return(SK_AND_OTHER);
	}
	
	pPrt->PMSStatus = ((ResAb & PHY_B_1000S_MSR) != 0) ?
		SK_MS_STAT_MASTER : SK_MS_STAT_SLAVE;
#endif	/* 0 */

	/* Check PAUSE mismatch ??? */
	/* We are using IEEE 802.3z/D5.0 Table 37-4 */
	if ((AuxStat & PHY_B_AS_PAUSE_MSK) == PHY_B_AS_PAUSE_MSK) {
		/* Symmetric PAUSE */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_SYMMETRIC;
	}
	else if ((AuxStat & PHY_B_AS_PAUSE_MSK) == PHY_B_AS_PRR) {
		/* Enable PAUSE receive, disable PAUSE transmit */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_REM_SEND;
	}
	else if ((AuxStat & PHY_B_AS_PAUSE_MSK) == PHY_B_AS_PRT) {
		/* Disable PAUSE receive, enable PAUSE transmit */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_LOC_SEND;
	}
	else {
		/* PAUSE mismatch -> no PAUSE */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_NONE;
	}
	pPrt->PLinkSpeedUsed = (SK_U8)SK_LSPEED_STAT_1000MBPS;

	return(SK_AND_OK);
}	/* SkXmAutoNegDoneBcom */
#endif /* GENESIS */


#ifdef YUKON
/******************************************************************************
 *
 *	SkGmAutoNegDoneMarv() - Auto-negotiation handling
 *
 * Description:
 *	This function handles the auto-negotiation if the Done bit is set.
 *
 * Returns:
 *	SK_AND_OK	o.k.
 *	SK_AND_DUP_CAP 	Duplex capability error happened
 *	SK_AND_OTHER 	Other error happened
 */
static int SkGmAutoNegDoneMarv(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	SK_U16		LPAb;		/* Link Partner Ability */
	SK_U16		ResAb;		/* Resolved Ability */
	SK_U16		AuxStat;	/* Auxiliary Status */

	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("AutoNegDoneMarv, Port %d\n", Port));
	pPrt = &pAC->GIni.GP[Port];

	/* Get PHY parameters */
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_AUNE_LP, &LPAb);
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("Link P.Abil.=0x%04X\n", LPAb));
	
	if ((LPAb & PHY_M_AN_RF) != 0) {
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("AutoNegFail: Remote fault bit set Port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		return(SK_AND_OTHER);
	}

	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_1000T_STAT, &ResAb);
	
	/* Check Master/Slave resolution */
	if ((ResAb & PHY_B_1000S_MSF) != 0) {
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("Master/Slave Fault Port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		pPrt->PMSStatus = SK_MS_STAT_FAULT;
		return(SK_AND_OTHER);
	}
	
	pPrt->PMSStatus = ((ResAb & PHY_B_1000S_MSR) != 0) ?
		(SK_U8)SK_MS_STAT_MASTER : (SK_U8)SK_MS_STAT_SLAVE;
	
	/* Read PHY Specific Status */
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_PHY_STAT, &AuxStat);
	
	/* Check Speed & Duplex resolved */
	if ((AuxStat & PHY_M_PS_SPDUP_RES) == 0) {
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("AutoNegFail: Speed & Duplex not resolved, Port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		pPrt->PLinkModeStatus = (SK_U8)SK_LMODE_STAT_UNKNOWN;
		return(SK_AND_DUP_CAP);
	}
	
	if ((AuxStat & PHY_M_PS_FULL_DUP) != 0) {
		pPrt->PLinkModeStatus = (SK_U8)SK_LMODE_STAT_AUTOFULL;
	}
	else {
		pPrt->PLinkModeStatus = (SK_U8)SK_LMODE_STAT_AUTOHALF;
	}
	
	/* Check PAUSE mismatch ??? */
	/* We are using IEEE 802.3z/D5.0 Table 37-4 */
	if ((AuxStat & PHY_M_PS_PAUSE_MSK) == PHY_M_PS_PAUSE_MSK) {
		/* Symmetric PAUSE */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_SYMMETRIC;
	}
	else if ((AuxStat & PHY_M_PS_PAUSE_MSK) == PHY_M_PS_RX_P_EN) {
		/* Enable PAUSE receive, disable PAUSE transmit */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_REM_SEND;
	}
	else if ((AuxStat & PHY_M_PS_PAUSE_MSK) == PHY_M_PS_TX_P_EN) {
		/* Disable PAUSE receive, enable PAUSE transmit */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_LOC_SEND;
	}
	else {
		/* PAUSE mismatch -> no PAUSE */
		pPrt->PFlowCtrlStatus = SK_FLOW_STAT_NONE;
	}
	
	/* set used link speed */
	switch ((unsigned)(AuxStat & PHY_M_PS_SPEED_MSK)) {
	case (unsigned)PHY_M_PS_SPEED_1000:
		pPrt->PLinkSpeedUsed = (SK_U8)SK_LSPEED_STAT_1000MBPS;
		break;
	case PHY_M_PS_SPEED_100:
		pPrt->PLinkSpeedUsed = (SK_U8)SK_LSPEED_STAT_100MBPS;
		break;
	default:
		pPrt->PLinkSpeedUsed = (SK_U8)SK_LSPEED_STAT_10MBPS;
	}

	return(SK_AND_OK);
}	/* SkGmAutoNegDoneMarv */
#endif /* YUKON */


#ifdef OTHER_PHY
/******************************************************************************
 *
 *	SkXmAutoNegDoneLone() - Auto-negotiation handling
 *
 * Description:
 *	This function handles the auto-negotiation if the Done bit is set.
 *
 * Returns:
 *	SK_AND_OK	o.k.
 *	SK_AND_DUP_CAP 	Duplex capability error happened
 *	SK_AND_OTHER 	Other error happened
 */
static int SkXmAutoNegDoneLone(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	SK_U16		ResAb;		/* Resolved Ability */
	SK_U16		LPAb;		/* Link Partner Ability */
	SK_U16		QuickStat;	/* Auxiliary Status */

	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("AutoNegDoneLone, Port %d\n", Port));
	pPrt = &pAC->GIni.GP[Port];

	/* Get PHY parameters */
	SkXmPhyRead(pAC, IoC, Port, PHY_LONE_AUNE_LP, &LPAb);
	SkXmPhyRead(pAC, IoC, Port, PHY_LONE_1000T_STAT, &ResAb);
	SkXmPhyRead(pAC, IoC, Port, PHY_LONE_Q_STAT, &QuickStat);

	if ((LPAb & PHY_L_AN_RF) != 0) {
		/* Remote fault bit is set */
		/* Error */
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("AutoNegFail: Remote fault bit set Port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		return(SK_AND_OTHER);
	}

	/* Check Duplex mismatch */
	if ((QuickStat & PHY_L_QS_DUP_MOD) != 0) {
		pPrt->PLinkModeStatus = (SK_U8)SK_LMODE_STAT_AUTOFULL;
	}
	else {
		pPrt->PLinkModeStatus = (SK_U8)SK_LMODE_STAT_AUTOHALF;
	}
	
	/* Check Master/Slave resolution */
	if ((ResAb & PHY_L_1000S_MSF) != 0) {
		/* Error */
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("Master/Slave Fault Port %d\n", Port));
		pPrt->PAutoNegFail = SK_TRUE;
		pPrt->PMSStatus = SK_MS_STAT_FAULT;
		return(SK_AND_OTHER);
	}
	else if (ResAb & PHY_L_1000S_MSR) {
		pPrt->PMSStatus = SK_MS_STAT_MASTER;
	}
	else {
		pPrt->PMSStatus = SK_MS_STAT_SLAVE;
	}

	/* Check PAUSE mismatch */
	/* We are using IEEE 802.3z/D5.0 Table 37-4 */
	/* we must manually resolve the abilities here */
	pPrt->PFlowCtrlStatus = SK_FLOW_STAT_NONE;
	
	switch (pPrt->PFlowCtrlMode) {
	case SK_FLOW_MODE_NONE:
		/* default */
		break;
	case SK_FLOW_MODE_LOC_SEND:
		if ((QuickStat & (PHY_L_QS_PAUSE | PHY_L_QS_AS_PAUSE)) ==
			(PHY_L_QS_PAUSE | PHY_L_QS_AS_PAUSE)) {
			/* Disable PAUSE receive, enable PAUSE transmit */
			pPrt->PFlowCtrlStatus = SK_FLOW_STAT_LOC_SEND;
		}
		break;
	case SK_FLOW_MODE_SYMMETRIC:
		if ((QuickStat & PHY_L_QS_PAUSE) != 0) {
			/* Symmetric PAUSE */
			pPrt->PFlowCtrlStatus = SK_FLOW_STAT_SYMMETRIC;
		}
		break;
	case SK_FLOW_MODE_SYM_OR_REM:
		if ((QuickStat & (PHY_L_QS_PAUSE | PHY_L_QS_AS_PAUSE)) ==
			PHY_L_QS_AS_PAUSE) {
			/* Enable PAUSE receive, disable PAUSE transmit */
			pPrt->PFlowCtrlStatus = SK_FLOW_STAT_REM_SEND;
		}
		else if ((QuickStat & PHY_L_QS_PAUSE) != 0) {
			/* Symmetric PAUSE */
			pPrt->PFlowCtrlStatus = SK_FLOW_STAT_SYMMETRIC;
		}
		break;
	default:
		SK_ERR_LOG(pAC, SK_ERRCL_SW | SK_ERRCL_INIT, SKERR_HWI_E016,
			SKERR_HWI_E016MSG);
	}
	
	return(SK_AND_OK);
}	/* SkXmAutoNegDoneLone */


/******************************************************************************
 *
 *	SkXmAutoNegDoneNat() - Auto-negotiation handling
 *
 * Description:
 *	This function handles the auto-negotiation if the Done bit is set.
 *
 * Returns:
 *	SK_AND_OK	o.k.
 *	SK_AND_DUP_CAP 	Duplex capability error happened
 *	SK_AND_OTHER 	Other error happened
 */
static int SkXmAutoNegDoneNat(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
/* todo: National */
	return(SK_AND_OK);
}	/* SkXmAutoNegDoneNat */
#endif /* OTHER_PHY */


/******************************************************************************
 *
 *	SkMacAutoNegDone() - Auto-negotiation handling
 *
 * Description:	calls the auto-negotiation done routines dep. on board type
 *
 * Returns:
 *	SK_AND_OK	o.k.
 *	SK_AND_DUP_CAP 	Duplex capability error happened
 *	SK_AND_OTHER 	Other error happened
 */
int	SkMacAutoNegDone(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	int	Rtv;

	Rtv = SK_AND_OK;

	pPrt = &pAC->GIni.GP[Port];

#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		
		switch (pPrt->PhyType) {
		
		case SK_PHY_XMAC:
			Rtv = SkXmAutoNegDoneXmac(pAC, IoC, Port);
			break;
		case SK_PHY_BCOM:
			Rtv = SkXmAutoNegDoneBcom(pAC, IoC, Port);
			break;
#ifdef OTHER_PHY
		case SK_PHY_LONE:
			Rtv = SkXmAutoNegDoneLone(pAC, IoC, Port);
			break;
		case SK_PHY_NAT:
			Rtv = SkXmAutoNegDoneNat(pAC, IoC, Port);
			break;
#endif /* OTHER_PHY */
		default:
			return(SK_AND_OTHER);
		}
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		
		Rtv = SkGmAutoNegDoneMarv(pAC, IoC, Port);
	}
#endif /* YUKON */
	
	if (Rtv != SK_AND_OK) {
		return(Rtv);
	}

	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("AutoNeg done Port %d\n", Port));
	
	/* We checked everything and may now enable the link */
	pPrt->PAutoNegFail = SK_FALSE;

	SkMacRxTxEnable(pAC, IoC, Port);
	
	return(SK_AND_OK);
}	/* SkMacAutoNegDone */


#ifdef GENESIS
/******************************************************************************
 *
 *	SkXmSetRxTxEn() - Special Set Rx/Tx Enable and some features in XMAC
 *
 * Description:
 *  sets MAC or PHY LoopBack and Duplex Mode in the MMU Command Reg.
 *  enables Rx/Tx
 *
 * Returns: N/A
 */
static void SkXmSetRxTxEn(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
int		Para)		/* Parameter to set: MAC or PHY LoopBack, Duplex Mode */
{
	SK_U16	Word;

	XM_IN16(IoC, Port, XM_MMU_CMD, &Word);

	switch (Para & (SK_MAC_LOOPB_ON | SK_MAC_LOOPB_OFF)) {
	case SK_MAC_LOOPB_ON:
		Word |= XM_MMU_MAC_LB;
		break;
	case SK_MAC_LOOPB_OFF:
		Word &= ~XM_MMU_MAC_LB;
		break;
	}

	switch (Para & (SK_PHY_LOOPB_ON | SK_PHY_LOOPB_OFF)) {
	case SK_PHY_LOOPB_ON:
		Word |= XM_MMU_GMII_LOOP;
		break;
	case SK_PHY_LOOPB_OFF:
		Word &= ~XM_MMU_GMII_LOOP;
		break;
	}
	
	switch (Para & (SK_PHY_FULLD_ON | SK_PHY_FULLD_OFF)) {
	case SK_PHY_FULLD_ON:
		Word |= XM_MMU_GMII_FD;
		break;
	case SK_PHY_FULLD_OFF:
		Word &= ~XM_MMU_GMII_FD;
		break;
	}
	
	XM_OUT16(IoC, Port, XM_MMU_CMD, Word | XM_MMU_ENA_RX | XM_MMU_ENA_TX);

	/* dummy read to ensure writing */
	XM_IN16(IoC, Port, XM_MMU_CMD, &Word);

}	/* SkXmSetRxTxEn */
#endif /* GENESIS */


#ifdef YUKON
/******************************************************************************
 *
 *	SkGmSetRxTxEn() - Special Set Rx/Tx Enable and some features in GMAC
 *
 * Description:
 *  sets MAC LoopBack and Duplex Mode in the General Purpose Control Reg.
 *  enables Rx/Tx
 *
 * Returns: N/A
 */
static void SkGmSetRxTxEn(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
int		Para)		/* Parameter to set: MAC LoopBack, Duplex Mode */
{
	SK_U16	Ctrl;
	
	GM_IN16(IoC, Port, GM_GP_CTRL, &Ctrl);

	switch (Para & (SK_MAC_LOOPB_ON | SK_MAC_LOOPB_OFF)) {
	case SK_MAC_LOOPB_ON:
		Ctrl |= GM_GPCR_LOOP_ENA;
		break;
	case SK_MAC_LOOPB_OFF:
		Ctrl &= ~GM_GPCR_LOOP_ENA;
		break;
	}

	switch (Para & (SK_PHY_FULLD_ON | SK_PHY_FULLD_OFF)) {
	case SK_PHY_FULLD_ON:
		Ctrl |= GM_GPCR_DUP_FULL;
		break;
	case SK_PHY_FULLD_OFF:
		Ctrl &= ~GM_GPCR_DUP_FULL;
		break;
	}
	
    GM_OUT16(IoC, Port, GM_GP_CTRL, (SK_U16)(Ctrl | GM_GPCR_RX_ENA |
		GM_GPCR_TX_ENA));

	/* dummy read to ensure writing */
	GM_IN16(IoC, Port, GM_GP_CTRL, &Ctrl);

}	/* SkGmSetRxTxEn */
#endif /* YUKON */


#ifndef SK_SLIM
/******************************************************************************
 *
 *	SkMacSetRxTxEn() - Special Set Rx/Tx Enable and parameters
 *
 * Description:	calls the Special Set Rx/Tx Enable routines dep. on board type
 *
 * Returns: N/A
 */
void SkMacSetRxTxEn(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
int		Para)
{
#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		
		SkXmSetRxTxEn(pAC, IoC, Port, Para);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		
		SkGmSetRxTxEn(pAC, IoC, Port, Para);
	}
#endif /* YUKON */

}	/* SkMacSetRxTxEn */
#endif /* !SK_SLIM */


/******************************************************************************
 *
 *	SkMacRxTxEnable() - Enable Rx/Tx activity if port is up
 *
 * Description:	enables Rx/Tx dep. on board type
 *
 * Returns:
 *	0	o.k.
 *	!= 0	Error happened
 */
int SkMacRxTxEnable(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	SK_U16		Reg;		/* 16-bit register value */
	SK_U16		IntMask;	/* MAC interrupt mask */
#ifdef GENESIS
	SK_U16		SWord;
#endif

	pPrt = &pAC->GIni.GP[Port];

	if (!pPrt->PHWLinkUp) {
		/* The Hardware link is NOT up */
		return(0);
	}

	if ((pPrt->PLinkMode == SK_LMODE_AUTOHALF ||
	     pPrt->PLinkMode == SK_LMODE_AUTOFULL ||
	     pPrt->PLinkMode == SK_LMODE_AUTOBOTH) &&
	     pPrt->PAutoNegFail) {
		/* Auto-negotiation is not done or failed */
		return(0);
	}

#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		/* set Duplex Mode and Pause Mode */
		SkXmInitDupMd(pAC, IoC, Port);
		
		SkXmInitPauseMd(pAC, IoC, Port);
	
		/*
		 * Initialize the Interrupt Mask Register. Default IRQs are...
		 *	- Link Asynchronous Event
		 *	- Link Partner requests config
		 *	- Auto Negotiation Done
		 *	- Rx Counter Event Overflow
		 *	- Tx Counter Event Overflow
		 *	- Transmit FIFO Underrun
		 */
		IntMask = XM_DEF_MSK;

#ifdef DEBUG
		/* add IRQ for Receive FIFO Overflow */
		IntMask &= ~XM_IS_RXF_OV;
#endif /* DEBUG */
		
		if (pPrt->PhyType != SK_PHY_XMAC) {
			/* disable GP0 interrupt bit */
			IntMask |= XM_IS_INP_ASS;
		}
		XM_OUT16(IoC, Port, XM_IMSK, IntMask);
	
		/* get MMU Command Reg. */
		XM_IN16(IoC, Port, XM_MMU_CMD, &Reg);
		
		if (pPrt->PhyType != SK_PHY_XMAC &&
			(pPrt->PLinkModeStatus == SK_LMODE_STAT_FULL ||
			 pPrt->PLinkModeStatus == SK_LMODE_STAT_AUTOFULL)) {
			/* set to Full Duplex */
			Reg |= XM_MMU_GMII_FD;
		}
		
		switch (pPrt->PhyType) {
		case SK_PHY_BCOM:
			/*
			 * Workaround BCOM Errata (#10523) for all BCom Phys
			 * Enable Power Management after link up
			 */
			SkXmPhyRead(pAC, IoC, Port, PHY_BCOM_AUX_CTRL, &SWord);
			SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_AUX_CTRL,
				(SK_U16)(SWord & ~PHY_B_AC_DIS_PM));
            SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_INT_MASK,
				(SK_U16)PHY_B_DEF_MSK);
			break;
#ifdef OTHER_PHY
		case SK_PHY_LONE:
			SkXmPhyWrite(pAC, IoC, Port, PHY_LONE_INT_ENAB, PHY_L_DEF_MSK);
			break;
		case SK_PHY_NAT:
			/* todo National:
			SkXmPhyWrite(pAC, IoC, Port, PHY_NAT_INT_MASK, PHY_N_DEF_MSK); */
			/* no interrupts possible from National ??? */
			break;
#endif /* OTHER_PHY */
		}
		
		/* enable Rx/Tx */
		XM_OUT16(IoC, Port, XM_MMU_CMD, Reg | XM_MMU_ENA_RX | XM_MMU_ENA_TX);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		/*
		 * Initialize the Interrupt Mask Register. Default IRQs are...
		 *	- Rx Counter Event Overflow
		 *	- Tx Counter Event Overflow
		 *	- Transmit FIFO Underrun
		 */
		IntMask = GMAC_DEF_MSK;

#ifdef DEBUG
		/* add IRQ for Receive FIFO Overrun */
		IntMask |= GM_IS_RX_FF_OR;
#endif /* DEBUG */
		
		SK_OUT8(IoC, GMAC_IRQ_MSK, (SK_U8)IntMask);
		
		/* get General Purpose Control */
		GM_IN16(IoC, Port, GM_GP_CTRL, &Reg);
		
		if (pPrt->PLinkModeStatus == SK_LMODE_STAT_FULL ||
			pPrt->PLinkModeStatus == SK_LMODE_STAT_AUTOFULL) {
			/* set to Full Duplex */
			Reg |= GM_GPCR_DUP_FULL;
		}
		
		/* enable Rx/Tx */
        GM_OUT16(IoC, Port, GM_GP_CTRL, (SK_U16)(Reg | GM_GPCR_RX_ENA |
			GM_GPCR_TX_ENA));

#ifndef VCPU
		/* Enable all PHY interrupts */
        SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_INT_MASK,
			(SK_U16)PHY_M_DEF_MSK);
#endif /* VCPU */
	}
#endif /* YUKON */
					
	return(0);

}	/* SkMacRxTxEnable */


/******************************************************************************
 *
 *	SkMacRxTxDisable() - Disable Receiver and Transmitter
 *
 * Description:	disables Rx/Tx dep. on board type
 *
 * Returns: N/A
 */
void SkMacRxTxDisable(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_U16	Word;

#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		
		XM_IN16(IoC, Port, XM_MMU_CMD, &Word);
		
		XM_OUT16(IoC, Port, XM_MMU_CMD, Word & ~(XM_MMU_ENA_RX | XM_MMU_ENA_TX));
	
		/* dummy read to ensure writing */
		XM_IN16(IoC, Port, XM_MMU_CMD, &Word);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		
		GM_IN16(IoC, Port, GM_GP_CTRL, &Word);

        GM_OUT16(IoC, Port, GM_GP_CTRL, (SK_U16)(Word & ~(GM_GPCR_RX_ENA |
			GM_GPCR_TX_ENA)));

		/* dummy read to ensure writing */
		GM_IN16(IoC, Port, GM_GP_CTRL, &Word);
	}
#endif /* YUKON */

}	/* SkMacRxTxDisable */


/******************************************************************************
 *
 *	SkMacIrqDisable() - Disable IRQ from MAC
 *
 * Description:	sets the IRQ-mask to disable IRQ dep. on board type
 *
 * Returns: N/A
 */
void SkMacIrqDisable(
SK_AC	*pAC,		/* Adapter Context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
#ifdef GENESIS
	SK_U16		Word;
#endif

	pPrt = &pAC->GIni.GP[Port];

#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		
		/* disable all XMAC IRQs */
		XM_OUT16(IoC, Port, XM_IMSK, 0xffff);	
		
		/* Disable all PHY interrupts */
		switch (pPrt->PhyType) {
			case SK_PHY_BCOM:
				/* Make sure that PHY is initialized */
				if (pPrt->PState != SK_PRT_RESET) {
					/* NOT allowed if BCOM is in RESET state */
					/* Workaround BCOM Errata (#10523) all BCom */
					/* Disable Power Management if link is down */
					SkXmPhyRead(pAC, IoC, Port, PHY_BCOM_AUX_CTRL, &Word);
					SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_AUX_CTRL,
						(SK_U16)(Word | PHY_B_AC_DIS_PM));
					SkXmPhyWrite(pAC, IoC, Port, PHY_BCOM_INT_MASK, 0xffff);
				}
				break;
#ifdef OTHER_PHY
			case SK_PHY_LONE:
				SkXmPhyWrite(pAC, IoC, Port, PHY_LONE_INT_ENAB, 0);
				break;
			case SK_PHY_NAT:
				/* todo: National
				SkXmPhyWrite(pAC, IoC, Port, PHY_NAT_INT_MASK, 0xffff); */
				break;
#endif /* OTHER_PHY */
		}
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		/* disable all GMAC IRQs */
		SK_OUT8(IoC, GMAC_IRQ_MSK, 0);
		
#ifndef VCPU
		/* Disable all PHY interrupts */
		SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_INT_MASK, 0);
#endif /* VCPU */
	}
#endif /* YUKON */

}	/* SkMacIrqDisable */


#ifdef SK_DIAG
/******************************************************************************
 *
 *	SkXmSendCont() - Enable / Disable Send Continuous Mode
 *
 * Description:	enable / disable Send Continuous Mode on XMAC
 *
 * Returns:
 *	nothing
 */
void SkXmSendCont(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port,	/* Port Index (MAC_1 + n) */
SK_BOOL	Enable)	/* Enable / Disable */
{
	SK_U32	MdReg;

	XM_IN32(IoC, Port, XM_MODE, &MdReg);

	if (Enable) {
		MdReg |= XM_MD_TX_CONT;
	}
	else {
		MdReg &= ~XM_MD_TX_CONT;
	}
	/* setup Mode Register */
	XM_OUT32(IoC, Port, XM_MODE, MdReg);

}	/* SkXmSendCont */


/******************************************************************************
 *
 *	SkMacTimeStamp() - Enable / Disable Time Stamp
 *
 * Description:	enable / disable Time Stamp generation for Rx packets
 *
 * Returns:
 *	nothing
 */
void SkMacTimeStamp(
SK_AC	*pAC,	/* adapter context */
SK_IOC	IoC,	/* IO context */
int		Port,	/* Port Index (MAC_1 + n) */
SK_BOOL	Enable)	/* Enable / Disable */
{
	SK_U32	MdReg;
	SK_U8	TimeCtrl;

	if (pAC->GIni.GIGenesis) {

		XM_IN32(IoC, Port, XM_MODE, &MdReg);

		if (Enable) {
			MdReg |= XM_MD_ATS;
		}
		else {
			MdReg &= ~XM_MD_ATS;
		}
		/* setup Mode Register */
		XM_OUT32(IoC, Port, XM_MODE, MdReg);
	}
	else {
		if (Enable) {
			TimeCtrl = GMT_ST_START | GMT_ST_CLR_IRQ;
		}
		else {
			TimeCtrl = GMT_ST_STOP | GMT_ST_CLR_IRQ;
		}
		/* Start/Stop Time Stamp Timer */
		SK_OUT8(IoC, GMAC_TI_ST_CTRL, TimeCtrl);
	}

}	/* SkMacTimeStamp*/

#else /* !SK_DIAG */

#ifdef GENESIS
/******************************************************************************
 *
 *	SkXmAutoNegLipaXmac() - Decides whether Link Partner could do auto-neg
 *
 *	This function analyses the Interrupt status word. If any of the
 *	Auto-negotiating interrupt bits are set, the PLipaAutoNeg variable
 *	is set true.
 */
void SkXmAutoNegLipaXmac(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
SK_U16	IStatus)	/* Interrupt Status word to analyse */
{
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port];

	if (pPrt->PLipaAutoNeg != SK_LIPA_AUTO &&
		(IStatus & (XM_IS_LIPA_RC | XM_IS_RX_PAGE | XM_IS_AND)) != 0) {

		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("AutoNegLipa: AutoNeg detected on Port %d, IStatus=0x%04X\n",
			Port, IStatus));
		pPrt->PLipaAutoNeg = SK_LIPA_AUTO;
	}
}	/* SkXmAutoNegLipaXmac */
#endif /* GENESIS */


/******************************************************************************
 *
 *	SkMacAutoNegLipaPhy() - Decides whether Link Partner could do auto-neg
 *
 *	This function analyses the PHY status word.
 *  If any of the Auto-negotiating bits are set, the PLipaAutoNeg variable
 *	is set true.
 */
void SkMacAutoNegLipaPhy(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
SK_U16	PhyStat)	/* PHY Status word to analyse */
{
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port];

	if (pPrt->PLipaAutoNeg != SK_LIPA_AUTO &&
		(PhyStat & PHY_ST_AN_OVER) != 0) {

		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("AutoNegLipa: AutoNeg detected on Port %d, PhyStat=0x%04X\n",
			Port, PhyStat));
		pPrt->PLipaAutoNeg = SK_LIPA_AUTO;
	}
}	/* SkMacAutoNegLipaPhy */


#ifdef GENESIS
/******************************************************************************
 *
 *	SkXmIrq() - Interrupt Service Routine
 *
 * Description:	services an Interrupt Request of the XMAC
 *
 * Note:
 *	With an external PHY, some interrupt bits are not meaningfull any more:
 *	- LinkAsyncEvent (bit #14)              XM_IS_LNK_AE
 *	- LinkPartnerReqConfig (bit #10)	XM_IS_LIPA_RC
 *	- Page Received (bit #9)		XM_IS_RX_PAGE
 *	- NextPageLoadedForXmt (bit #8)		XM_IS_TX_PAGE
 *	- AutoNegDone (bit #7)			XM_IS_AND
 *	Also probably not valid any more is the GP0 input bit:
 *	- GPRegisterBit0set			XM_IS_INP_ASS
 *
 * Returns:
 *	nothing
 */
void SkXmIrq(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	SK_EVPARA	Para;
	SK_U16		IStatus;	/* Interrupt status read from the XMAC */
	SK_U16		IStatus2;
#ifdef SK_SLIM
    SK_U64      OverflowStatus;
#endif	

	pPrt = &pAC->GIni.GP[Port];
	
	XM_IN16(IoC, Port, XM_ISRC, &IStatus);
	
	/* LinkPartner Auto-negable? */
	if (pPrt->PhyType == SK_PHY_XMAC) {
		SkXmAutoNegLipaXmac(pAC, IoC, Port, IStatus);
	}
	else {
		/* mask bits that are not used with ext. PHY */
		IStatus &= ~(XM_IS_LNK_AE | XM_IS_LIPA_RC |
			XM_IS_RX_PAGE | XM_IS_TX_PAGE |
			XM_IS_AND | XM_IS_INP_ASS);
	}
	
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_IRQ,
		("XmacIrq Port %d Isr 0x%04X\n", Port, IStatus));

	if (!pPrt->PHWLinkUp) {
		/* Spurious XMAC interrupt */
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_IRQ,
			("SkXmIrq: spurious interrupt on Port %d\n", Port));
		return;
	}

	if ((IStatus & XM_IS_INP_ASS) != 0) {
		/* Reread ISR Register if link is not in sync */
		XM_IN16(IoC, Port, XM_ISRC, &IStatus2);

		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_IRQ,
			("SkXmIrq: Link async. Double check Port %d 0x%04X 0x%04X\n",
			 Port, IStatus, IStatus2));
		IStatus &= ~XM_IS_INP_ASS;
		IStatus |= IStatus2;
	}

	if ((IStatus & XM_IS_LNK_AE) != 0) {
		/* not used, GP0 is used instead */
	}

	if ((IStatus & XM_IS_TX_ABORT) != 0) {
		/* not used */
	}

	if ((IStatus & XM_IS_FRC_INT) != 0) {
		/* not used, use ASIC IRQ instead if needed */
	}

	if ((IStatus & (XM_IS_INP_ASS | XM_IS_LIPA_RC | XM_IS_RX_PAGE)) != 0) {
		SkHWLinkDown(pAC, IoC, Port);

		/* Signal to RLMT */
		Para.Para32[0] = (SK_U32)Port;
		SkEventQueue(pAC, SKGE_RLMT, SK_RLMT_LINK_DOWN, Para);

		/* Start workaround Errata #2 timer */
		SkTimerStart(pAC, IoC, &pPrt->PWaTimer, SK_WA_INA_TIME,
			SKGE_HWAC, SK_HWEV_WATIM, Para);
	}

	if ((IStatus & XM_IS_RX_PAGE) != 0) {
		/* not used */
	}

	if ((IStatus & XM_IS_TX_PAGE) != 0) {
		/* not used */
	}

	if ((IStatus & XM_IS_AND) != 0) {
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_IRQ,
			("SkXmIrq: AND on link that is up Port %d\n", Port));
	}

	if ((IStatus & XM_IS_TSC_OV) != 0) {
		/* not used */
	}

	/* Combined Tx & Rx Counter Overflow SIRQ Event */
	if ((IStatus & (XM_IS_RXC_OV | XM_IS_TXC_OV)) != 0) {
#ifdef SK_SLIM
		SkXmOverflowStatus(pAC, IoC, Port, IStatus, &OverflowStatus);
#else
		Para.Para32[0] = (SK_U32)Port;
		Para.Para32[1] = (SK_U32)IStatus;
		SkPnmiEvent(pAC, IoC, SK_PNMI_EVT_SIRQ_OVERFLOW, Para);
#endif /* SK_SLIM */
	}

	if ((IStatus & XM_IS_RXF_OV) != 0) {
		/* normal situation -> no effect */
#ifdef DEBUG
		pPrt->PRxOverCnt++;
#endif /* DEBUG */
	}

	if ((IStatus & XM_IS_TXF_UR) != 0) {
		/* may NOT happen -> error log */
		SK_ERR_LOG(pAC, SK_ERRCL_HW, SKERR_SIRQ_E020, SKERR_SIRQ_E020MSG);
	}

	if ((IStatus & XM_IS_TX_COMP) != 0) {
		/* not served here */
	}

	if ((IStatus & XM_IS_RX_COMP) != 0) {
		/* not served here */
	}
}	/* SkXmIrq */
#endif /* GENESIS */


#ifdef YUKON
/******************************************************************************
 *
 *	SkGmIrq() - Interrupt Service Routine
 *
 * Description:	services an Interrupt Request of the GMAC
 *
 * Note:
 *
 * Returns:
 *	nothing
 */
void SkGmIrq(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	SK_U8		IStatus;	/* Interrupt status */
#ifdef SK_SLIM
    SK_U64      OverflowStatus;
#else
	SK_EVPARA	Para;
#endif	

	pPrt = &pAC->GIni.GP[Port];
	
	SK_IN8(IoC, GMAC_IRQ_SRC, &IStatus);
	
#ifdef XXX
	/* LinkPartner Auto-negable? */
	SkMacAutoNegLipaPhy(pAC, IoC, Port, IStatus);
#endif /* XXX */
	
	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_IRQ,
		("GmacIrq Port %d Isr 0x%04X\n", Port, IStatus));

	/* Combined Tx & Rx Counter Overflow SIRQ Event */
	if (IStatus & (GM_IS_RX_CO_OV | GM_IS_TX_CO_OV)) {
		/* these IRQs will be cleared by reading GMACs register */
#ifdef SK_SLIM
        SkGmOverflowStatus(pAC, IoC, Port, IStatus, &OverflowStatus);
#else
		Para.Para32[0] = (SK_U32)Port;
		Para.Para32[1] = (SK_U32)IStatus;
		SkPnmiEvent(pAC, IoC, SK_PNMI_EVT_SIRQ_OVERFLOW, Para);
#endif		
	}

	if (IStatus & GM_IS_RX_FF_OR) {
		/* clear GMAC Rx FIFO Overrun IRQ */
		SK_OUT8(IoC, MR_ADDR(Port, RX_GMF_CTRL_T), (SK_U8)GMF_CLI_RX_FO);
#ifdef DEBUG
		pPrt->PRxOverCnt++;
#endif /* DEBUG */
	}

	if (IStatus & GM_IS_TX_FF_UR) {
		/* clear GMAC Tx FIFO Underrun IRQ */
		SK_OUT8(IoC, MR_ADDR(Port, TX_GMF_CTRL_T), (SK_U8)GMF_CLI_TX_FU);
		/* may NOT happen -> error log */
		SK_ERR_LOG(pAC, SK_ERRCL_HW, SKERR_SIRQ_E020, SKERR_SIRQ_E020MSG);
	}

	if (IStatus & GM_IS_TX_COMPL) {
		/* not served here */
	}

	if (IStatus & GM_IS_RX_COMPL) {
		/* not served here */
	}
}	/* SkGmIrq */
#endif /* YUKON */


/******************************************************************************
 *
 *	SkMacIrq() - Interrupt Service Routine for MAC
 *
 * Description:	calls the Interrupt Service Routine dep. on board type
 *
 * Returns:
 *	nothing
 */
void SkMacIrq(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
int		Port)		/* Port Index (MAC_1 + n) */
{
#ifdef GENESIS
	if (pAC->GIni.GIGenesis) {
		/* IRQ from XMAC */
		SkXmIrq(pAC, IoC, Port);
	}
#endif /* GENESIS */
	
#ifdef YUKON
	if (pAC->GIni.GIYukon) {
		/* IRQ from GMAC */
		SkGmIrq(pAC, IoC, Port);
	}
#endif /* YUKON */

}	/* SkMacIrq */

#endif /* !SK_DIAG */

#ifdef GENESIS
/******************************************************************************
 *
 *	SkXmUpdateStats() - Force the XMAC to output the current statistic
 *
 * Description:
 *	The XMAC holds its statistic internally. To obtain the current
 *	values a command must be sent so that the statistic data will
 *	be written to a predefined memory area on the adapter.
 *
 * Returns:
 *	0:  success
 *	1:  something went wrong
 */
int SkXmUpdateStats(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
unsigned int Port)	/* Port Index (MAC_1 + n) */
{
	SK_GEPORT	*pPrt;
	SK_U16		StatReg;
	int			WaitIndex;

	pPrt = &pAC->GIni.GP[Port];
	WaitIndex = 0;

	/* Send an update command to XMAC specified */
	XM_OUT16(IoC, Port, XM_STAT_CMD, XM_SC_SNP_TXC | XM_SC_SNP_RXC);

	/*
	 * It is an auto-clearing register. If the command bits
	 * went to zero again, the statistics are transferred.
	 * Normally the command should be executed immediately.
	 * But just to be sure we execute a loop.
	 */
	do {

		XM_IN16(IoC, Port, XM_STAT_CMD, &StatReg);
		
		if (++WaitIndex > 10) {

			SK_ERR_LOG(pAC, SK_ERRCL_HW, SKERR_HWI_E021, SKERR_HWI_E021MSG);

			return(1);
		}
	} while ((StatReg & (XM_SC_SNP_TXC | XM_SC_SNP_RXC)) != 0);
	
	return(0);
}	/* SkXmUpdateStats */


/******************************************************************************
 *
 *	SkXmMacStatistic() - Get XMAC counter value
 *
 * Description:
 *	Gets the 32bit counter value. Except for the octet counters
 *	the lower 32bit are counted in hardware and the upper 32bit
 *	must be counted in software by monitoring counter overflow interrupts.
 *
 * Returns:
 *	0:  success
 *	1:  something went wrong
 */
int SkXmMacStatistic(
SK_AC	*pAC,			/* adapter context */
SK_IOC	IoC,			/* IO context */
unsigned int Port,		/* Port Index (MAC_1 + n) */
SK_U16	StatAddr,		/* MIB counter base address */
SK_U32	SK_FAR *pVal)	/* ptr to return statistic value */
{
	if ((StatAddr < XM_TXF_OK) || (StatAddr > XM_RXF_MAX_SZ)) {
		
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_HWI_E022, SKERR_HWI_E022MSG);
		
		return(1);
	}
	
	XM_IN32(IoC, Port, StatAddr, pVal);

	return(0);
}	/* SkXmMacStatistic */


/******************************************************************************
 *
 *	SkXmResetCounter() - Clear MAC statistic counter
 *
 * Description:
 *	Force the XMAC to clear its statistic counter.
 *
 * Returns:
 *	0:  success
 *	1:  something went wrong
 */
int SkXmResetCounter(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
unsigned int Port)	/* Port Index (MAC_1 + n) */
{
	XM_OUT16(IoC, Port, XM_STAT_CMD, XM_SC_CLR_RXC | XM_SC_CLR_TXC);
	/* Clear two times according to Errata #3 */
	XM_OUT16(IoC, Port, XM_STAT_CMD, XM_SC_CLR_RXC | XM_SC_CLR_TXC);

	return(0);
}	/* SkXmResetCounter */


/******************************************************************************
 *
 *	SkXmOverflowStatus() - Gets the status of counter overflow interrupt
 *
 * Description:
 *	Checks the source causing an counter overflow interrupt. On success the
 *	resulting counter overflow status is written to <pStatus>, whereas the
 *	upper dword stores the XMAC ReceiveCounterEvent register and the lower
 *	dword the XMAC TransmitCounterEvent register.
 *
 * Note:
 *	For XMAC the interrupt source is a self-clearing register, so the source
 *	must be checked only once. SIRQ module does another check to be sure
 *	that no interrupt get lost during process time.
 *
 * Returns:
 *	0:  success
 *	1:  something went wrong
 */
int SkXmOverflowStatus(
SK_AC	*pAC,				/* adapter context */
SK_IOC	IoC,				/* IO context */
unsigned int Port,			/* Port Index (MAC_1 + n) */
SK_U16	IStatus,			/* Interupt Status from MAC */
SK_U64	SK_FAR *pStatus)	/* ptr for return overflow status value */
{
	SK_U64	Status;	/* Overflow status */
	SK_U32	RegVal;

	Status = 0;

	if ((IStatus & XM_IS_RXC_OV) != 0) {

		XM_IN32(IoC, Port, XM_RX_CNT_EV, &RegVal);
		Status |= (SK_U64)RegVal << 32;
	}
	
	if ((IStatus & XM_IS_TXC_OV) != 0) {

		XM_IN32(IoC, Port, XM_TX_CNT_EV, &RegVal);
		Status |= (SK_U64)RegVal;
	}

	*pStatus = Status;

	return(0);
}	/* SkXmOverflowStatus */
#endif /* GENESIS */


#ifdef YUKON
/******************************************************************************
 *
 *	SkGmUpdateStats() - Force the GMAC to output the current statistic
 *
 * Description:
 *	Empty function for GMAC. Statistic data is accessible in direct way.
 *
 * Returns:
 *	0:  success
 *	1:  something went wrong
 */
int SkGmUpdateStats(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
unsigned int Port)	/* Port Index (MAC_1 + n) */
{
	return(0);
}


/******************************************************************************
 *
 *	SkGmMacStatistic() - Get GMAC counter value
 *
 * Description:
 *	Gets the 32bit counter value. Except for the octet counters
 *	the lower 32bit are counted in hardware and the upper 32bit
 *	must be counted in software by monitoring counter overflow interrupts.
 *
 * Returns:
 *	0:  success
 *	1:  something went wrong
 */
int SkGmMacStatistic(
SK_AC	*pAC,			/* adapter context */
SK_IOC	IoC,			/* IO context */
unsigned int Port,		/* Port Index (MAC_1 + n) */
SK_U16	StatAddr,		/* MIB counter base address */
SK_U32	SK_FAR *pVal)	/* ptr to return statistic value */
{

	if ((StatAddr < GM_RXF_UC_OK) || (StatAddr > GM_TXE_FIFO_UR)) {
		
		SK_ERR_LOG(pAC, SK_ERRCL_SW, SKERR_HWI_E022, SKERR_HWI_E022MSG);
		
		SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
			("SkGmMacStat: wrong MIB counter 0x%04X\n", StatAddr));
		return(1);
	}
		
	GM_IN32(IoC, Port, StatAddr, pVal);

	return(0);
}	/* SkGmMacStatistic */


/******************************************************************************
 *
 *	SkGmResetCounter() - Clear MAC statistic counter
 *
 * Description:
 *	Force GMAC to clear its statistic counter.
 *
 * Returns:
 *	0:  success
 *	1:  something went wrong
 */
int SkGmResetCounter(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,		/* IO context */
unsigned int Port)	/* Port Index (MAC_1 + n) */
{
	SK_U16	Reg;	/* Phy Address Register */
	SK_U16	Word;
	int		i;

	GM_IN16(IoC, Port, GM_PHY_ADDR, &Reg);

	/* set MIB Clear Counter Mode */
	GM_OUT16(IoC, Port, GM_PHY_ADDR, Reg | GM_PAR_MIB_CLR);
	
	/* read all MIB Counters with Clear Mode set */
	for (i = 0; i < GM_MIB_CNT_SIZE; i++) {
		/* the reset is performed only when the lower 16 bits are read */
		GM_IN16(IoC, Port, GM_MIB_CNT_BASE + 8*i, &Word);
	}
	
	/* clear MIB Clear Counter Mode */
	GM_OUT16(IoC, Port, GM_PHY_ADDR, Reg);
	
	return(0);
}	/* SkGmResetCounter */


/******************************************************************************
 *
 *	SkGmOverflowStatus() - Gets the status of counter overflow interrupt
 *
 * Description:
 *	Checks the source causing an counter overflow interrupt. On success the
 *	resulting counter overflow status is written to <pStatus>, whereas the
 *	the following bit coding is used:
 *	63:56 - unused
 *	55:48 - TxRx interrupt register bit7:0
 *	32:47 - Rx interrupt register
 *	31:24 - unused
 *	23:16 - TxRx interrupt register bit15:8
 *	15:0  - Tx interrupt register
 *
 * Returns:
 *	0:  success
 *	1:  something went wrong
 */
int SkGmOverflowStatus(
SK_AC	*pAC,				/* adapter context */
SK_IOC	IoC,				/* IO context */
unsigned int Port,			/* Port Index (MAC_1 + n) */
SK_U16	IStatus,			/* Interupt Status from MAC */
SK_U64	SK_FAR *pStatus)	/* ptr for return overflow status value */
{
	SK_U64	Status;		/* Overflow status */
	SK_U16	RegVal;

	Status = 0;

	if ((IStatus & GM_IS_RX_CO_OV) != 0) {
		/* this register is self-clearing after read */
		GM_IN16(IoC, Port, GM_RX_IRQ_SRC, &RegVal);
		Status |= (SK_U64)RegVal << 32;
	}
	
	if ((IStatus & GM_IS_TX_CO_OV) != 0) {
		/* this register is self-clearing after read */
		GM_IN16(IoC, Port, GM_TX_IRQ_SRC, &RegVal);
		Status |= (SK_U64)RegVal;
	}
	
	/* this register is self-clearing after read */
	GM_IN16(IoC, Port, GM_TR_IRQ_SRC, &RegVal);
	/* Rx overflow interrupt register bits (LoByte)*/
	Status |= (SK_U64)((SK_U8)RegVal) << 48;
	/* Tx overflow interrupt register bits (HiByte)*/
	Status |= (SK_U64)(RegVal >> 8) << 16;

	*pStatus = Status;

	return(0);
}	/* SkGmOverflowStatus */


#ifndef SK_SLIM
/******************************************************************************
 *
 *	SkGmCableDiagStatus() - Starts / Gets status of cable diagnostic test
 *
 * Description:
 *  starts the cable diagnostic test if 'StartTest' is true
 *  gets the results if 'StartTest' is true
 *
 * NOTE:	this test is meaningful only when link is down
 *	
 * Returns:
 *	0:  success
 *	1:	no YUKON copper
 *	2:	test in progress
 */
int SkGmCableDiagStatus(
SK_AC	*pAC,		/* adapter context */
SK_IOC	IoC,   		/* IO context */
int		Port,		/* Port Index (MAC_1 + n) */
SK_BOOL	StartTest)	/* flag for start / get result */
{
	int		i;
	SK_U16	RegVal;
	SK_GEPORT	*pPrt;

	pPrt = &pAC->GIni.GP[Port];

	if (pPrt->PhyType != SK_PHY_MARV_COPPER) {
		
		return(1);
	}

	if (StartTest) {
		/* only start the cable test */
		if ((pPrt->PhyId1 & PHY_I1_REV_MSK) < 4) {
			/* apply TDR workaround from Marvell */
			SkGmPhyWrite(pAC, IoC, Port, 29, 0x001e);
			
			SkGmPhyWrite(pAC, IoC, Port, 30, 0xcc00);
			SkGmPhyWrite(pAC, IoC, Port, 30, 0xc800);
			SkGmPhyWrite(pAC, IoC, Port, 30, 0xc400);
			SkGmPhyWrite(pAC, IoC, Port, 30, 0xc000);
			SkGmPhyWrite(pAC, IoC, Port, 30, 0xc100);
		}

		/* set address to 0 for MDI[0] */
		SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_EXT_ADR, 0);

		/* Read Cable Diagnostic Reg */
		SkGmPhyRead(pAC, IoC, Port, PHY_MARV_CABLE_DIAG, &RegVal);

		/* start Cable Diagnostic Test */
		SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_CABLE_DIAG,
			(SK_U16)(RegVal | PHY_M_CABD_ENA_TEST));
	
		return(0);
	}
	
	/* Read Cable Diagnostic Reg */
	SkGmPhyRead(pAC, IoC, Port, PHY_MARV_CABLE_DIAG, &RegVal);

	SK_DBG_MSG(pAC, SK_DBGMOD_HWM, SK_DBGCAT_CTRL,
		("PHY Cable Diag.=0x%04X\n", RegVal));

	if ((RegVal & PHY_M_CABD_ENA_TEST) != 0) {
		/* test is running */
		return(2);
	}

	/* get the test results */
	for (i = 0; i < 4; i++)  {
		/* set address to i for MDI[i] */
		SkGmPhyWrite(pAC, IoC, Port, PHY_MARV_EXT_ADR, (SK_U16)i);

		/* get Cable Diagnostic values */
		SkGmPhyRead(pAC, IoC, Port, PHY_MARV_CABLE_DIAG, &RegVal);

		pPrt->PMdiPairLen[i] = (SK_U8)(RegVal & PHY_M_CABD_DIST_MSK);

		pPrt->PMdiPairSts[i] = (SK_U8)((RegVal & PHY_M_CABD_STAT_MSK) >> 13);
	}

	return(0);
}	/* SkGmCableDiagStatus */
#endif /* !SK_SLIM */
#endif /* YUKON */

/* End of file */
