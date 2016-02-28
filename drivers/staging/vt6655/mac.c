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
 *      io_base    - Base Address for MAC
 *      byRegOfs    - Offset of MAC Register
 *      byTestBits  - Test bits
 *  Out:
 *      none
 *
 * Return Value: true if all test bits On; otherwise false
 *
 */
bool MACbIsRegBitsOn(struct vnt_private *priv, unsigned char byRegOfs,
		     unsigned char byTestBits)
{
	void __iomem *io_base = priv->PortOffset;

	return (ioread8(io_base + byRegOfs) & byTestBits) == byTestBits;
}

/*
 * Description:
 *      Test if all test bits off
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      byRegOfs    - Offset of MAC Register
 *      byTestBits  - Test bits
 *  Out:
 *      none
 *
 * Return Value: true if all test bits Off; otherwise false
 *
 */
bool MACbIsRegBitsOff(struct vnt_private *priv, unsigned char byRegOfs,
		      unsigned char byTestBits)
{
	void __iomem *io_base = priv->PortOffset;

	return !(ioread8(io_base + byRegOfs) & byTestBits);
}

/*
 * Description:
 *      Test if MAC interrupt disable
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if interrupt is disable; otherwise false
 *
 */
bool MACbIsIntDisable(struct vnt_private *priv)
{
	void __iomem *io_base = priv->PortOffset;

	if (ioread32(io_base + MAC_REG_IMR))
		return false;

	return true;
}

/*
 * Description:
 *      Set 802.11 Short Retry Limit
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      byRetryLimit- Retry Limit
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetShortRetryLimit(struct vnt_private *priv, unsigned char byRetryLimit)
{
	void __iomem *io_base = priv->PortOffset;
	/* set SRT */
	VNSvOutPortB(io_base + MAC_REG_SRT, byRetryLimit);
}


/*
 * Description:
 *      Set 802.11 Long Retry Limit
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      byRetryLimit- Retry Limit
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetLongRetryLimit(struct vnt_private *priv, unsigned char byRetryLimit)
{
	void __iomem *io_base = priv->PortOffset;
	/* set LRT */
	VNSvOutPortB(io_base + MAC_REG_LRT, byRetryLimit);
}

/*
 * Description:
 *      Set MAC Loopback mode
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *      byLoopbackMode  - Loopback Mode
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetLoopbackMode(struct vnt_private *priv, unsigned char byLoopbackMode)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned char byOrgValue;

	byLoopbackMode <<= 6;
	/* set TCR */
	VNSvInPortB(io_base + MAC_REG_TEST, &byOrgValue);
	byOrgValue = byOrgValue & 0x3F;
	byOrgValue = byOrgValue | byLoopbackMode;
	VNSvOutPortB(io_base + MAC_REG_TEST, byOrgValue);
}

/*
 * Description:
 *      Save MAC registers to context buffer
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      pbyCxtBuf   - Context buffer
 *
 * Return Value: none
 *
 */
void MACvSaveContext(struct vnt_private *priv, unsigned char *pbyCxtBuf)
{
	void __iomem *io_base = priv->PortOffset;

	/* read page0 register */
	memcpy_fromio(pbyCxtBuf, io_base, MAC_MAX_CONTEXT_SIZE_PAGE0);

	MACvSelectPage1(io_base);

	/* read page1 register */
	memcpy_fromio(pbyCxtBuf + MAC_MAX_CONTEXT_SIZE_PAGE0, io_base,
		      MAC_MAX_CONTEXT_SIZE_PAGE1);

	MACvSelectPage0(io_base);
}

/*
 * Description:
 *      Restore MAC registers from context buffer
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      pbyCxtBuf   - Context buffer
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvRestoreContext(struct vnt_private *priv, unsigned char *pbyCxtBuf)
{
	void __iomem *io_base = priv->PortOffset;
	int         ii;

	MACvSelectPage1(io_base);
	/* restore page1 */
	for (ii = 0; ii < MAC_MAX_CONTEXT_SIZE_PAGE1; ii++)
		VNSvOutPortB((io_base + ii),
			     *(pbyCxtBuf + MAC_MAX_CONTEXT_SIZE_PAGE0 + ii));

	MACvSelectPage0(io_base);

	/* restore RCR,TCR,IMR... */
	for (ii = MAC_REG_RCR; ii < MAC_REG_ISR; ii++)
		VNSvOutPortB(io_base + ii, *(pbyCxtBuf + ii));

	/* restore MAC Config. */
	for (ii = MAC_REG_LRT; ii < MAC_REG_PAGE1SEL; ii++)
		VNSvOutPortB(io_base + ii, *(pbyCxtBuf + ii));

	VNSvOutPortB(io_base + MAC_REG_CFG, *(pbyCxtBuf + MAC_REG_CFG));

	/* restore PS Config. */
	for (ii = MAC_REG_PSCFG; ii < MAC_REG_BBREGCTL; ii++)
		VNSvOutPortB(io_base + ii, *(pbyCxtBuf + ii));

	/* restore CURR_RX_DESC_ADDR, CURR_TX_DESC_ADDR */
	VNSvOutPortD(io_base + MAC_REG_TXDMAPTR0,
		     *(unsigned long *)(pbyCxtBuf + MAC_REG_TXDMAPTR0));
	VNSvOutPortD(io_base + MAC_REG_AC0DMAPTR,
		     *(unsigned long *)(pbyCxtBuf + MAC_REG_AC0DMAPTR));
	VNSvOutPortD(io_base + MAC_REG_BCNDMAPTR,
		     *(unsigned long *)(pbyCxtBuf + MAC_REG_BCNDMAPTR));

	VNSvOutPortD(io_base + MAC_REG_RXDMAPTR0,
		     *(unsigned long *)(pbyCxtBuf + MAC_REG_RXDMAPTR0));

	VNSvOutPortD(io_base + MAC_REG_RXDMAPTR1,
		     *(unsigned long *)(pbyCxtBuf + MAC_REG_RXDMAPTR1));
}

/*
 * Description:
 *      Software Reset MAC
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if Reset Success; otherwise false
 *
 */
bool MACbSoftwareReset(struct vnt_private *priv)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned short ww;

	/* turn on HOSTCR_SOFTRST, just write 0x01 to reset */
	VNSvOutPortB(io_base + MAC_REG_HOSTCR, 0x01);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_HOSTCR) & HOSTCR_SOFTRST))
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
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbSafeSoftwareReset(struct vnt_private *priv)
{
	unsigned char abyTmpRegData[MAC_MAX_CONTEXT_SIZE_PAGE0+MAC_MAX_CONTEXT_SIZE_PAGE1];
	bool bRetVal;

	/* PATCH....
	 * save some important register's value, then do
	 * reset, then restore register's value
	 */
	/* save MAC context */
	MACvSaveContext(priv, abyTmpRegData);
	/* do reset */
	bRetVal = MACbSoftwareReset(priv);
	/* restore MAC context, except CR0 */
	MACvRestoreContext(priv, abyTmpRegData);

	return bRetVal;
}

/*
 * Description:
 *      Turn Off MAC Rx
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbSafeRxOff(struct vnt_private *priv)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned short ww;

	/* turn off wow temp for turn off Rx safely */

	/* Clear RX DMA0,1 */
	VNSvOutPortD(io_base + MAC_REG_RXDMACTL0, DMACTL_CLRRUN);
	VNSvOutPortD(io_base + MAC_REG_RXDMACTL1, DMACTL_CLRRUN);
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread32(io_base + MAC_REG_RXDMACTL0) & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x10)\n");
		return false;
	}
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread32(io_base + MAC_REG_RXDMACTL1) & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x11)\n");
		return false;
	}

	/* try to safe shutdown RX */
	MACvRegBitsOff(io_base, MAC_REG_HOSTCR, HOSTCR_RXON);
	/* W_MAX_TIMEOUT is the timeout period */
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_HOSTCR) & HOSTCR_RXONST))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x12)\n");
		return false;
	}
	return true;
}

/*
 * Description:
 *      Turn Off MAC Tx
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbSafeTxOff(struct vnt_private *priv)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned short ww;

	/* Clear TX DMA */
	/* Tx0 */
	VNSvOutPortD(io_base + MAC_REG_TXDMACTL0, DMACTL_CLRRUN);
	/* AC0 */
	VNSvOutPortD(io_base + MAC_REG_AC0DMACTL, DMACTL_CLRRUN);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread32(io_base + MAC_REG_TXDMACTL0) & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x20)\n");
		return false;
	}
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread32(io_base + MAC_REG_AC0DMACTL) & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
		pr_debug(" DBG_PORT80(0x21)\n");
		return false;
	}

	/* try to safe shutdown TX */
	MACvRegBitsOff(io_base, MAC_REG_HOSTCR, HOSTCR_TXON);

	/* W_MAX_TIMEOUT is the timeout period */
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_HOSTCR) & HOSTCR_TXONST))
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
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
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbSafeStop(struct vnt_private *priv)
{
	void __iomem *io_base = priv->PortOffset;

	MACvRegBitsOff(io_base, MAC_REG_TCR, TCR_AUTOBCNTX);

	if (!MACbSafeRxOff(priv)) {
		pr_debug(" MACbSafeRxOff == false)\n");
		MACbSafeSoftwareReset(priv);
		return false;
	}
	if (!MACbSafeTxOff(priv)) {
		pr_debug(" MACbSafeTxOff == false)\n");
		MACbSafeSoftwareReset(priv);
		return false;
	}

	MACvRegBitsOff(io_base, MAC_REG_HOSTCR, HOSTCR_MACEN);

	return true;
}

/*
 * Description:
 *      Shut Down MAC
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: true if success; otherwise false
 *
 */
bool MACbShutdown(struct vnt_private *priv)
{
	void __iomem *io_base = priv->PortOffset;
	/* disable MAC IMR */
	MACvIntDisable(io_base);
	MACvSetLoopbackMode(priv, MAC_LB_INTERNAL);
	/* stop the adapter */
	if (!MACbSafeStop(priv)) {
		MACvSetLoopbackMode(priv, MAC_LB_NONE);
		return false;
	}
	MACvSetLoopbackMode(priv, MAC_LB_NONE);
	return true;
}

/*
 * Description:
 *      Initialize MAC
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvInitialize(struct vnt_private *priv)
{
	void __iomem *io_base = priv->PortOffset;
	/* clear sticky bits */
	MACvClearStckDS(io_base);
	/* disable force PME-enable */
	VNSvOutPortB(io_base + MAC_REG_PMC1, PME_OVR);
	/* only 3253 A */

	/* do reset */
	MACbSoftwareReset(priv);

	/* reset TSF counter */
	VNSvOutPortB(io_base + MAC_REG_TFTCTL, TFTCTL_TSFCNTRST);
	/* enable TSF counter */
	VNSvOutPortB(io_base + MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);
}

/*
 * Description:
 *      Set the chip with current rx descriptor address
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *      dwCurrDescAddr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetCurrRx0DescAddr(struct vnt_private *priv, unsigned long dwCurrDescAddr)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned short ww;
	unsigned char byOrgDMACtl;

	VNSvInPortB(io_base + MAC_REG_RXDMACTL0, &byOrgDMACtl);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(io_base + MAC_REG_RXDMACTL0+2, DMACTL_RUN);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_RXDMACTL0) & DMACTL_RUN))
			break;
	}

	VNSvOutPortD(io_base + MAC_REG_RXDMAPTR0, dwCurrDescAddr);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(io_base + MAC_REG_RXDMACTL0, DMACTL_RUN);
}

/*
 * Description:
 *      Set the chip with current rx descriptor address
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *      dwCurrDescAddr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetCurrRx1DescAddr(struct vnt_private *priv, unsigned long dwCurrDescAddr)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned short ww;
	unsigned char byOrgDMACtl;

	VNSvInPortB(io_base + MAC_REG_RXDMACTL1, &byOrgDMACtl);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(io_base + MAC_REG_RXDMACTL1+2, DMACTL_RUN);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_RXDMACTL1) & DMACTL_RUN))
			break;
	}

	VNSvOutPortD(io_base + MAC_REG_RXDMAPTR1, dwCurrDescAddr);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(io_base + MAC_REG_RXDMACTL1, DMACTL_RUN);

}

/*
 * Description:
 *      Set the chip with current tx0 descriptor address
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *      dwCurrDescAddr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetCurrTx0DescAddrEx(struct vnt_private *priv,
			      unsigned long dwCurrDescAddr)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned short ww;
	unsigned char byOrgDMACtl;

	VNSvInPortB(io_base + MAC_REG_TXDMACTL0, &byOrgDMACtl);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(io_base + MAC_REG_TXDMACTL0+2, DMACTL_RUN);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_TXDMACTL0) & DMACTL_RUN))
			break;
	}

	VNSvOutPortD(io_base + MAC_REG_TXDMAPTR0, dwCurrDescAddr);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(io_base + MAC_REG_TXDMACTL0, DMACTL_RUN);
}

/*
 * Description:
 *      Set the chip with current AC0 descriptor address
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *      dwCurrDescAddr  - Descriptor Address
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
/* TxDMA1 = AC0DMA */
void MACvSetCurrAC0DescAddrEx(struct vnt_private *priv,
			      unsigned long dwCurrDescAddr)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned short ww;
	unsigned char byOrgDMACtl;

	VNSvInPortB(io_base + MAC_REG_AC0DMACTL, &byOrgDMACtl);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(io_base + MAC_REG_AC0DMACTL+2, DMACTL_RUN);

	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		if (!(ioread8(io_base + MAC_REG_AC0DMACTL) & DMACTL_RUN))
			break;
	}
	if (ww == W_MAX_TIMEOUT)
		pr_debug(" DBG_PORT80(0x26)\n");
	VNSvOutPortD(io_base + MAC_REG_AC0DMAPTR, dwCurrDescAddr);
	if (byOrgDMACtl & DMACTL_RUN)
		VNSvOutPortB(io_base + MAC_REG_AC0DMACTL, DMACTL_RUN);
}

void MACvSetCurrTXDescAddr(int iTxType, struct vnt_private *priv,
			   unsigned long dwCurrDescAddr)
{
	if (iTxType == TYPE_AC0DMA)
		MACvSetCurrAC0DescAddrEx(priv, dwCurrDescAddr);
	else if (iTxType == TYPE_TXDMA0)
		MACvSetCurrTx0DescAddrEx(priv, dwCurrDescAddr);
}

/*
 * Description:
 *      Micro Second Delay via MAC
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      uDelay      - Delay time (timer resolution is 4 us)
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvTimer0MicroSDelay(struct vnt_private *priv, unsigned int uDelay)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned char byValue;
	unsigned int uu, ii;

	VNSvOutPortB(io_base + MAC_REG_TMCTL0, 0);
	VNSvOutPortD(io_base + MAC_REG_TMDATA0, uDelay);
	VNSvOutPortB(io_base + MAC_REG_TMCTL0, (TMCTL_TMD | TMCTL_TE));
	for (ii = 0; ii < 66; ii++) {  /* assume max PCI clock is 66Mhz */
		for (uu = 0; uu < uDelay; uu++) {
			VNSvInPortB(io_base + MAC_REG_TMCTL0, &byValue);
			if ((byValue == 0) ||
			    (byValue & TMCTL_TSUSP)) {
				VNSvOutPortB(io_base + MAC_REG_TMCTL0, 0);
				return;
			}
		}
	}
	VNSvOutPortB(io_base + MAC_REG_TMCTL0, 0);
}

/*
 * Description:
 *      Micro Second One shot timer via MAC
 *
 * Parameters:
 *  In:
 *      io_base    - Base Address for MAC
 *      uDelay      - Delay time
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvOneShotTimer1MicroSec(struct vnt_private *priv, unsigned int uDelayTime)
{
	void __iomem *io_base = priv->PortOffset;

	VNSvOutPortB(io_base + MAC_REG_TMCTL1, 0);
	VNSvOutPortD(io_base + MAC_REG_TMDATA1, uDelayTime);
	VNSvOutPortB(io_base + MAC_REG_TMCTL1, (TMCTL_TMD | TMCTL_TE));
}

void MACvSetMISCFifo(struct vnt_private *priv, unsigned short wOffset,
		     unsigned long dwData)
{
	void __iomem *io_base = priv->PortOffset;

	if (wOffset > 273)
		return;
	VNSvOutPortW(io_base + MAC_REG_MISCFFNDEX, wOffset);
	VNSvOutPortD(io_base + MAC_REG_MISCFFDATA, dwData);
	VNSvOutPortW(io_base + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
}

bool MACbPSWakeup(struct vnt_private *priv)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned char byOrgValue;
	unsigned int ww;
	/* Read PSCTL */
	if (MACbIsRegBitsOff(priv, MAC_REG_PSCTL, PSCTL_PS))
		return true;

	/* Disable PS */
	MACvRegBitsOff(io_base, MAC_REG_PSCTL, PSCTL_PSEN);

	/* Check if SyncFlushOK */
	for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
		VNSvInPortB(io_base + MAC_REG_PSCTL, &byOrgValue);
		if (byOrgValue & PSCTL_WAKEDONE)
			break;
	}
	if (ww == W_MAX_TIMEOUT) {
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
 *      io_base        - Base Address for MAC
 *
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */

void MACvSetKeyEntry(struct vnt_private *priv, unsigned short wKeyCtl,
		     unsigned int uEntryIdx, unsigned int uKeyIdx,
		     unsigned char *pbyAddr, u32 *pdwKey,
		     unsigned char byLocalID)
{
	void __iomem *io_base = priv->PortOffset;
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

	VNSvOutPortW(io_base + MAC_REG_MISCFFNDEX, wOffset);
	VNSvOutPortD(io_base + MAC_REG_MISCFFDATA, dwData);
	VNSvOutPortW(io_base + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
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

	VNSvOutPortW(io_base + MAC_REG_MISCFFNDEX, wOffset);
	VNSvOutPortD(io_base + MAC_REG_MISCFFDATA, dwData);
	VNSvOutPortW(io_base + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
	wOffset++;

	wOffset += (uKeyIdx * 4);
	for (ii = 0; ii < 4; ii++) {
		/* always push 128 bits */
		pr_debug("3.(%d) wOffset: %d, Data: %X\n",
			 ii, wOffset+ii, *pdwKey);
		VNSvOutPortW(io_base + MAC_REG_MISCFFNDEX, wOffset+ii);
		VNSvOutPortD(io_base + MAC_REG_MISCFFDATA, *pdwKey++);
		VNSvOutPortW(io_base + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
	}
}

/*
 * Description:
 *      Disable the Key Entry by MISCFIFO
 *
 * Parameters:
 *  In:
 *      io_base        - Base Address for MAC
 *
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvDisableKeyEntry(struct vnt_private *priv, unsigned int uEntryIdx)
{
	void __iomem *io_base = priv->PortOffset;
	unsigned short wOffset;

	wOffset = MISCFIFO_KEYETRY0;
	wOffset += (uEntryIdx * MISCFIFO_KEYENTRYSIZE);

	VNSvOutPortW(io_base + MAC_REG_MISCFFNDEX, wOffset);
	VNSvOutPortD(io_base + MAC_REG_MISCFFDATA, 0);
	VNSvOutPortW(io_base + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
}
