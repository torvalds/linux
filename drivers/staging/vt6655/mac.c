/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
 * File: mac.c
 *
 * Purpose:  MAC routines
 *
 * Author: Tevin Chen
 *
 * Date: May 21, 1996
 *
 * Functions:
 *      MACbIsRegBitsOn - Test if All test Bits On
 *      MACbIsRegBitsOff - Test if All test Bits Off
 *      MACbIsIntDisable - Test if MAC interrupt disable
 *      MACvSetShortRetryLimit - Set 802.11 Short Retry limit
 *      MACvGetShortRetryLimit - Get 802.11 Short Retry limit
 *      MACvSetLongRetryLimit - Set 802.11 Long Retry limit
 *      MACvSetLoopbackMode - Set MAC Loopback Mode
 *      MACvSaveContext - Save Context of MAC Registers
 *      MACvRestoreContext - Restore Context of MAC Registers
 *      MACbSoftwareReset - Software Reset MAC
 *      MACbSafeRxOff - Turn Off MAC Rx
 *      MACbSafeTxOff - Turn Off MAC Tx
 *      MACbSafeStop - Stop MAC function
 *      MACbShutdown - Shut down MAC
 *      MACvInitialize - Initialize MAC
 *      MACvSetCurrRxDescAddr - Set Rx Descriptors Address
 *      MACvSetCurrTx0DescAddr - Set Tx0 Descriptors Address
 *      MACvSetCurrTx1DescAddr - Set Tx1 Descriptors Address
 *      MACvTimer0MicroSDelay - Micro Second Delay Loop by MAC
 *
 * Revision History:
 *      08-22-2003 Kyle Hsu     :  Porting MAC functions from sim53
 *      09-03-2003 Bryan YC Fan :  Add MACvClearBusSusInd()& MACvEnableBusSusEn()
 *      09-18-2003 Jerry Chen   :  Add MACvSetKeyEntry & MACvDisableKeyEntry
 *
 */

#include "tmacro.h"
#include "mac.h"

/*
 * Description:
 *      Test if all test bits on
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      byRegOfs    - Offset of MAC Register
 *      byTestBits  - Test bits
 *  Out:
 *      none
 *
 * Return Value: true if all test bits On; otherwise false
 *
 */
bool MACbIsRegBitsOn(void __iomem *dwIoBase, unsigned char byRegOfs, unsigned char byTestBits)
{
	unsigned char byData;

	VNSvInPortB(dwIoBase + byRegOfs, &byData);
	return (byData & byTestBits) == byTestBits;
}

/*
 * Description:
 *      Test if all test bits off
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      byRegOfs    - Offset of MAC Register
 *      byTestBits  - Test bits
 *  Out:
 *      none
 *
 * Return Value: true if all test bits Off; otherwise false
 *
 */
bool MACbIsRegBitsOff(void __iomem *dwIoBase, unsigned char byRegOfs, unsigned char byTestBits)
{
	unsigned char byData;

	VNSvInPortB(dwIoBase + byRegOfs, &byData);
	return !(byData & byTestBits);
}

/*
 * Description:
 *      Test if MAC interrupt disable
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if interrupt is disable; otherwise false
 *
 */
bool MACbIsIntDisable(void __iomem *dwIoBase)
{
	unsigned long dwData;

	VNSvInPortD(dwIoBase + MAC_REG_IMR, &dwData);
	if (dwData != 0)
		return false;

	return true;
}

/*
 * Description:
 *      Set 802.11 Short Retry Limit
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      byRetryLimit- Retry Limit
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetShortRetryLimit(void __iomem *dwIoBase, unsigned char byRetryLimit)
{
	// set SRT
	VNSvOutPortB(dwIoBase + MAC_REG_SRT, byRetryLimit);
}

/*
 * Description:
 *      Get 802.11 Short Retry Limit
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *  Out:
 *      pbyRetryLimit   - Retry Limit Get
 *
 * Return Value: none
 *
 */
void MACvGetShortRetryLimit(void __iomem *dwIoBase, unsigned char *pbyRetryLimit)
{
	// get SRT
	VNSvInPortB(dwIoBase + MAC_REG_SRT, pbyRetryLimit);
}

/*
 * Description:
 *      Set 802.11 Long Retry Limit
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      byRetryLimit- Retry Limit
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetLongRetryLimit(void __iomem *dwIoBase, unsigned char byRetryLimit)
{
	// set LRT
	VNSvOutPortB(dwIoBase + MAC_REG_LRT, byRetryLimit);
}

/*
 * Description:
 *      Set MAC Loopback mode
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *      byLoopbackMode  - Loopback Mode
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetLoopbackMode(void __iomem *dwIoBase, unsigned char byLoopbackMode)
{
	unsigned char byOrgValue;

	ASSERT(byLoopbackMode < 3);
	byLoopbackMode <<= 6;
	// set TCR
	VNSvInPortB(dwIoBase + MAC_REG_TEST, &byOrgValue);
	byOrgValue = byOrgValue & 0x3F;
	byOrgValue = byOrgValue | byLoopbackMode;
	VNSvOutPortB(dwIoBase + MAC_REG_TEST, byOrgValue);
}

/*
 * Description:
 *      Save MAC registers to context buffer
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      pbyCxtBuf   - Context buffer
 *
 * Return Value: none
 *
 */
void MACvSaveContext(void __iomem *dwIoBase, unsigned char *pbyCxtBuf)
{
	int         ii;

	// read page0 register
	for (ii = 0; ii < MAC_MAX_CONTEXT_SIZE_PAGE0; ii++)
		VNSvInPortB((dwIoBase + ii), (pbyCxtBuf + ii));

	MACvSelectPage1(dwIoBase);

	// read page1 register
	for (ii = 0; ii < MAC_MAX_CONTEXT_SIZE_PAGE1; ii++)
		VNSvInPortB((dwIoBase + ii), (pbyCxtBuf + MAC_MAX_CONTEXT_SIZE_PAGE0 + ii));

	MACvSelectPage0(dwIoBase);
}

/*
 * Description:
 *      Restore MAC registers from context buffer
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      pbyCxtBuf   - Context buffer
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvRestoreContext(void __iomem *dwIoBase, unsigned char *pbyCxtBuf)
{
	int         ii;

	MACvSelectPage1(dwIoBase);
	// restore page1
	for (ii = 0; ii < MAC_MAX_CONTEXT_SIZE_PAGE1; ii++)
		VNSvOutPortB((dwIoBase + ii), *(pbyCxtBuf + MAC_MAX_CONTEXT_SIZE_PAGE0 + ii));

	MACvSelectPage0(dwIoBase);

	// restore RCR,TCR,IMR...
	for (ii = MAC_REG_RCR; ii < MAC_REG_ISR; ii++)
		VNSvOutPortB(dwIoBase + ii, *(pbyCxtBuf + ii));

	// restore MAC Config.
	for (ii = MAC_REG_LRT; ii < MAC_REG_PAGE1SEL; ii++)
		VNSvOutPortB(dwIoBase + ii, *(pbyCxtBuf + ii));

	VNSvOutPortB(dwIoBase + MAC_REG_CFG, *(pbyCxtBuf + MAC_REG_CFG));

	// restore PS Config.
	for (ii = MAC_REG_PSCFG; ii < MAC_REG_BBREGCTL; ii++)
		VNSvOutPortB(dwIoBase + ii, *(pbyCxtBuf + ii));

	// restore CURR_RX_DESC_ADDR, CURR_TX_DESC_ADDR
	VNSvOutPortD(dwIoBase + MAC_REG_TXDMAPTR0, *(unsigned long *)(pbyCxtBuf + MAC_REG_TXDMAPTR0));
	VNSvOutPortD(dwIoBase + MAC_REG_AC0DMAPTR, *(unsigned long *)(pbyCxtBuf + MAC_REG_AC0DMAPTR));
	VNSvOutPortD(dwIoBase + MAC_REG_BCNDMAPTR, *(unsigned long *)(pbyCxtBuf + MAC_REG_BCNDMAPTR));

	VNSvOutPortD(dwIoBase + MAC_REG_RXDMAPTR0, *(unsigned long *)(pbyCxtBuf + MAC_REG_RXDMAPTR0));

	VNSvOutPortD(dwIoBase + MAC_REG_RXDMAPTR1, *(unsigned long *)(pbyCxtBuf + MAC_REG_RXDMAPTR1));
}

/*
 * Description:
 *      Software Reset MAC
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if Reset Success; otherwise false
 *
 */
bool MACbSoftwareReset(void __iomem *dwIoBase)
{
	unsigned char byData;
	unsigned short ww;

	// turn on HOSTCR_SOFTRST, just write 0x01 to reset
	VNSvOutPortB(dwIoBase + MAC_REG_HOSTCR, 0x01);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortB(dwIoBase + MAC_REG_HOSTCR, &byData);
		if (!(byData & HOSTCR_SOFTRST))
			break;
	}
	if (ww == W_MAX_TIMEOUT)
		return false;
	return true;
}

/*
 * Description:
 *      save some important register's value, then do reset, then restore register's value
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbSafeSoftwareReset(void __iomem *dwIoBase)
{
	unsigned char abyTmpRegData[MAC_MAX_CONTEXT_SIZE_PAGE0+MAC_MAX_CONTEXT_SIZE_PAGE1];
	bool bRetVal;

	// PATCH....
	// save some important register's value, then do
	// reset, then restore register's value

	// save MAC context
	MACvSaveContext(dwIoBase, abyTmpRegData);
	// do reset
	bRetVal = MACbSoftwareReset(dwIoBase);
	// restore MAC context, except CR0
	MACvRestoreContext(dwIoBase, abyTmpRegData);

	return bRetVal;
}

/*
 * Description:
 *      Trun Off MAC Rx
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbSafeRxOff(void __iomem *dwIoBase)
{
	unsigned short ww;
	unsigned long dwData;
	unsigned char byData;

	// turn off wow temp for turn off Rx safely

	// Clear RX DMA0,1
	VNSvOutPortD(dwIoBase + MAC_REG_RXDMACTL0, DMACTL_CLRRUN);
	VNSvOutPortD(dwIoBase + MAC_REG_RXDMACTL1, DMACTL_CLRRUN);
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortD(dwIoBase + MAC_REG_RXDMACTL0, &dwData);
		if (!(dwData & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		DBG_PORT80(0x10);
		pr_debug(" DBG_PORT80(0x10)\n");
		return false;
	}
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortD(dwIoBase + MAC_REG_RXDMACTL1, &dwData);
		if (!(dwData & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		DBG_PORT80(0x11);
		pr_debug(" DBG_PORT80(0x11)\n");
		return false;
	}

	// try to safe shutdown RX
	MACvRegBitsOff(dwIoBase, MAC_REG_HOSTCR, HOSTCR_RXON);
	// W_MAX_TIMEOUT is the timeout period
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortB(dwIoBase + MAC_REG_HOSTCR, &byData);
		if (!(byData & HOSTCR_RXONST))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		DBG_PORT80(0x12);
		pr_debug(" DBG_PORT80(0x12)\n");
		return false;
	}
	return true;
}

/*
 * Description:
 *      Trun Off MAC Tx
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbSafeTxOff(void __iomem *dwIoBase)
{
	unsigned short ww;
	unsigned long dwData;
	unsigned char byData;

	// Clear TX DMA
	//Tx0
	VNSvOutPortD(dwIoBase + MAC_REG_TXDMACTL0, DMACTL_CLRRUN);
	//AC0
	VNSvOutPortD(dwIoBase + MAC_REG_AC0DMACTL, DMACTL_CLRRUN);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortD(dwIoBase + MAC_REG_TXDMACTL0, &dwData);
		if (!(dwData & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		DBG_PORT80(0x20);
		pr_debug(" DBG_PORT80(0x20)\n");
		return false;
	}
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortD(dwIoBase + MAC_REG_AC0DMACTL, &dwData);
		if (!(dwData & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		DBG_PORT80(0x21);
		pr_debug(" DBG_PORT80(0x21)\n");
		return false;
	}

	// try to safe shutdown TX
	MACvRegBitsOff(dwIoBase, MAC_REG_HOSTCR, HOSTCR_TXON);

	// W_MAX_TIMEOUT is the timeout period
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortB(dwIoBase + MAC_REG_HOSTCR, &byData);
		if (!(byData & HOSTCR_TXONST))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		DBG_PORT80(0x24);
		pr_debug(" DBG_PORT80(0x24)\n");
		return false;
	}
	return true;
}

/*
 * Description:
 *      Stop MAC function
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbSafeStop(void __iomem *dwIoBase)
{
	MACvRegBitsOff(dwIoBase, MAC_REG_TCR, TCR_AUTOBCNTX);

	if (!MACbSafeRxOff(dwIoBase)) {
		DBG_PORT80(0xA1);
		pr_debug(" MACbSafeRxOff == false)\n");
		MACbSafeSoftwareReset(dwIoBase);
		return false;
	}
	if (!MACbSafeTxOff(dwIoBase)) {
		DBG_PORT80(0xA2);
		pr_debug(" MACbSafeTxOff == false)\n");
		MACbSafeSoftwareReset(dwIoBase);
		return false;
	}

	MACvRegBitsOff(dwIoBase, MAC_REG_HOSTCR, HOSTCR_MACEN);

	return true;
}

/*
 * Description:
 *      Shut Down MAC
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbShutdown(void __iomem *dwIoBase)
{
	// disable MAC IMR
	MACvIntDisable(dwIoBase);
	MACvSetLoopbackMode(dwIoBase, MAC_LB_INTERNAL);
	// stop the adapter
	if (!MACbSafeStop(dwIoBase)) {
		MACvSetLoopbackMode(dwIoBase, MAC_LB_NONE);
		return false;
	}
	MACvSetLoopbackMode(dwIoBase, MAC_LB_NONE);
	return true;
}

/*
 * Description:
 *      Initialize MAC
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvInitialize(void __iomem *dwIoBase)
{
	// clear sticky bits
	MACvClearStckDS(dwIoBase);
	// disable force PME-enable
	VNSvOutPortB(dwIoBase + MAC_REG_PMC1, PME_OVR);
	// only 3253 A

	// do reset
	MACbSoftwareReset(dwIoBase);

	// reset TSF counter
	VNSvOutPortB(dwIoBase + MAC_REG_TFTCTL, TFTCTL_TSFCNTRST);
	// enable TSF counter
	VNSvOutPortB(dwIoBase + MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);
}

/*
 * Description:
 *      Set the chip with current rx descriptor address
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *      dwCurrDescAddr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetCurrRx0DescAddr(void __iomem *dwIoBase, unsigned long dwCurrDescAddr)
{
	unsigned short ww;
	unsigned char byData;
	unsigned char byOrgDMACtl;

	VNSvInPortB(dwIoBase + MAC_REG_RXDMACTL0, &byOrgDMACtl);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(dwIoBase + MAC_REG_RXDMACTL0+2, DMACTL_RUN);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortB(dwIoBase + MAC_REG_RXDMACTL0, &byData);
		if (!(byData & DMACTL_RUN))
			break;
	}

	if (ww == W_MAX_TIMEOUT)
		DBG_PORT80(0x13);

	VNSvOutPortD(dwIoBase + MAC_REG_RXDMAPTR0, dwCurrDescAddr);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(dwIoBase + MAC_REG_RXDMACTL0, DMACTL_RUN);
}

/*
 * Description:
 *      Set the chip with current rx descriptor address
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *      dwCurrDescAddr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetCurrRx1DescAddr(void __iomem *dwIoBase, unsigned long dwCurrDescAddr)
{
	unsigned short ww;
	unsigned char byData;
	unsigned char byOrgDMACtl;

	VNSvInPortB(dwIoBase + MAC_REG_RXDMACTL1, &byOrgDMACtl);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(dwIoBase + MAC_REG_RXDMACTL1+2, DMACTL_RUN);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortB(dwIoBase + MAC_REG_RXDMACTL1, &byData);
		if (!(byData & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT)
		DBG_PORT80(0x14);

	VNSvOutPortD(dwIoBase + MAC_REG_RXDMAPTR1, dwCurrDescAddr);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(dwIoBase + MAC_REG_RXDMACTL1, DMACTL_RUN);

}

/*
 * Description:
 *      Set the chip with current tx0 descriptor address
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *      dwCurrDescAddr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetCurrTx0DescAddrEx(void __iomem *dwIoBase, unsigned long dwCurrDescAddr)
{
	unsigned short ww;
	unsigned char byData;
	unsigned char byOrgDMACtl;

	VNSvInPortB(dwIoBase + MAC_REG_TXDMACTL0, &byOrgDMACtl);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(dwIoBase + MAC_REG_TXDMACTL0+2, DMACTL_RUN);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortB(dwIoBase + MAC_REG_TXDMACTL0, &byData);
		if (!(byData & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT)
		DBG_PORT80(0x25);

	VNSvOutPortD(dwIoBase + MAC_REG_TXDMAPTR0, dwCurrDescAddr);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(dwIoBase + MAC_REG_TXDMACTL0, DMACTL_RUN);
}

/*
 * Description:
 *      Set the chip with current AC0 descriptor address
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *      dwCurrDescAddr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
//TxDMA1 = AC0DMA
void MACvSetCurrAC0DescAddrEx(void __iomem *dwIoBase, unsigned long dwCurrDescAddr)
{
	unsigned short ww;
	unsigned char byData;
	unsigned char byOrgDMACtl;

	VNSvInPortB(dwIoBase + MAC_REG_AC0DMACTL, &byOrgDMACtl);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(dwIoBase + MAC_REG_AC0DMACTL+2, DMACTL_RUN);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortB(dwIoBase + MAC_REG_AC0DMACTL, &byData);
		if (!(byData & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		DBG_PORT80(0x26);
		pr_debug(" DBG_PORT80(0x26)\n");
	}
	VNSvOutPortD(dwIoBase + MAC_REG_AC0DMAPTR, dwCurrDescAddr);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(dwIoBase + MAC_REG_AC0DMACTL, DMACTL_RUN);
}

void MACvSetCurrTXDescAddr(int iTxType, void __iomem *dwIoBase, unsigned long dwCurrDescAddr)
{
	if (iTxType == TYPE_AC0DMA)
		MACvSetCurrAC0DescAddrEx(dwIoBase, dwCurrDescAddr);
	else if (iTxType == TYPE_TXDMA0)
		MACvSetCurrTx0DescAddrEx(dwIoBase, dwCurrDescAddr);
}

/*
 * Description:
 *      Micro Second Delay via MAC
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      uDelay      - Delay time (timer resolution is 4 us)
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvTimer0MicroSDelay(void __iomem *dwIoBase, unsigned int uDelay)
{
	unsigned char byValue;
	unsigned int uu, ii;

	VNSvOutPortB(dwIoBase + MAC_REG_TMCTL0, 0);
	VNSvOutPortD(dwIoBase + MAC_REG_TMDATA0, uDelay);
	VNSvOutPortB(dwIoBase + MAC_REG_TMCTL0, (TMCTL_TMD | TMCTL_TE));
	for (ii = 0; ii < 66; ii++) {  // assume max PCI clock is 66Mhz
		for (uu = 0; uu < uDelay; uu++) {
			VNSvInPortB(dwIoBase + MAC_REG_TMCTL0, &byValue);
			if ((byValue == 0) ||
			    (byValue & TMCTL_TSUSP)) {
				VNSvOutPortB(dwIoBase + MAC_REG_TMCTL0, 0);
				return;
			}
		}
	}
	VNSvOutPortB(dwIoBase + MAC_REG_TMCTL0, 0);
}

/*
 * Description:
 *      Micro Second One shot timer via MAC
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      uDelay      - Delay time
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvOneShotTimer1MicroSec(void __iomem *dwIoBase, unsigned int uDelayTime)
{
	VNSvOutPortB(dwIoBase + MAC_REG_TMCTL1, 0);
	VNSvOutPortD(dwIoBase + MAC_REG_TMDATA1, uDelayTime);
	VNSvOutPortB(dwIoBase + MAC_REG_TMCTL1, (TMCTL_TMD | TMCTL_TE));
}

void MACvSetMISCFifo(void __iomem *dwIoBase, unsigned short wOffset, unsigned long dwData)
{
	if (wOffset > 273)
		return;
	VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
	VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
	VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
}

bool MACbPSWakeup(void __iomem *dwIoBase)
{
	unsigned char byOrgValue;
	unsigned int ww;
	// Read PSCTL
	if (MACbIsRegBitsOff(dwIoBase, MAC_REG_PSCTL, PSCTL_PS))
		return true;

	// Disable PS
	MACvRegBitsOff(dwIoBase, MAC_REG_PSCTL, PSCTL_PSEN);

	// Check if SyncFlushOK
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortB(dwIoBase + MAC_REG_PSCTL , &byOrgValue);
		if (byOrgValue & PSCTL_WAKEDONE)
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		DBG_PORT80(0x36);
		pr_debug(" DBG_PORT80(0x33)\n");
		return false;
	}
	return true;
}

/*
 * Description:
 *      Set the Key by MISCFIFO
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */

void MACvSetKeyEntry(void __iomem *dwIoBase, unsigned short wKeyCtl, unsigned int uEntryIdx,
		     unsigned int uKeyIdx, unsigned char *pbyAddr, u32 *pdwKey, unsigned char byLocalID)
{
	unsigned short wOffset;
	u32 dwData;
	int     ii;

	if (byLocalID <= 1)
		return;

	pr_debug("MACvSetKeyEntry\n");
	wOffset = MISCFIFO_KEYETRY0;
	wOffset += (uEntryIdx * MISCFIFO_KEYENTRYSIZE);

	dwData = 0;
	dwData |= wKeyCtl;
	dwData <<= 16;
	dwData |= MAKEWORD(*(pbyAddr+4), *(pbyAddr+5));
	pr_debug("1. wOffset: %d, Data: %X, KeyCtl:%X\n",
		 wOffset, dwData, wKeyCtl);

	VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
	VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
	VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
	wOffset++;

	dwData = 0;
	dwData |= *(pbyAddr+3);
	dwData <<= 8;
	dwData |= *(pbyAddr+2);
	dwData <<= 8;
	dwData |= *(pbyAddr+1);
	dwData <<= 8;
	dwData |= *(pbyAddr+0);
	pr_debug("2. wOffset: %d, Data: %X\n", wOffset, dwData);

	VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
	VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
	VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
	wOffset++;

	wOffset += (uKeyIdx * 4);
	for (ii = 0; ii < 4; ii++) {
		// always push 128 bits
		pr_debug("3.(%d) wOffset: %d, Data: %X\n",
			 ii, wOffset+ii, *pdwKey);
		VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset+ii);
		VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, *pdwKey++);
		VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
	}
}

/*
 * Description:
 *      Disable the Key Entry by MISCFIFO
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvDisableKeyEntry(void __iomem *dwIoBase, unsigned int uEntryIdx)
{
	unsigned short wOffset;

	wOffset = MISCFIFO_KEYETRY0;
	wOffset += (uEntryIdx * MISCFIFO_KEYENTRYSIZE);

	VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
	VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, 0);
	VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
}
