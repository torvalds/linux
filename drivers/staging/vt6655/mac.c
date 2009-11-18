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
 *      MACvReadAllRegs - Read All MAC Registers to buffer
 *      MACbIsRegBitsOn - Test if All test Bits On
 *      MACbIsRegBitsOff - Test if All test Bits Off
 *      MACbIsIntDisable - Test if MAC interrupt disable
 *      MACbyReadMultiAddr - Read Multicast Address Mask Pattern
 *      MACvWriteMultiAddr - Write Multicast Address Mask Pattern
 *      MACvSetMultiAddrByHash - Set Multicast Address Mask by Hash value
 *      MACvResetMultiAddrByHash - Clear Multicast Address Mask by Hash value
 *      MACvSetRxThreshold - Set Rx Threshold value
 *      MACvGetRxThreshold - Get Rx Threshold value
 *      MACvSetTxThreshold - Set Tx Threshold value
 *      MACvGetTxThreshold - Get Tx Threshold value
 *      MACvSetDmaLength - Set Dma Length value
 *      MACvGetDmaLength - Get Dma Length value
 *      MACvSetShortRetryLimit - Set 802.11 Short Retry limit
 *      MACvGetShortRetryLimit - Get 802.11 Short Retry limit
 *      MACvSetLongRetryLimit - Set 802.11 Long Retry limit
 *      MACvGetLongRetryLimit - Get 802.11 Long Retry limit
 *      MACvSetLoopbackMode - Set MAC Loopback Mode
 *      MACbIsInLoopbackMode - Test if MAC in Loopback mode
 *      MACvSetPacketFilter - Set MAC Address Filter
 *      MACvSaveContext - Save Context of MAC Registers
 *      MACvRestoreContext - Restore Context of MAC Registers
 *      MACbCompareContext - Compare if values of MAC Registers same as Context
 *      MACbSoftwareReset - Software Reset MAC
 *      MACbSafeRxOff - Turn Off MAC Rx
 *      MACbSafeTxOff - Turn Off MAC Tx
 *      MACbSafeStop - Stop MAC function
 *      MACbShutdown - Shut down MAC
 *      MACvInitialize - Initialize MAC
 *      MACvSetCurrRxDescAddr - Set Rx Descriptos Address
 *      MACvSetCurrTx0DescAddr - Set Tx0 Descriptos Address
 *      MACvSetCurrTx1DescAddr - Set Tx1 Descriptos Address
 *      MACvTimer0MicroSDelay - Micro Second Delay Loop by MAC
 *
 * Revision History:
 *      08-22-2003 Kyle Hsu     :  Porting MAC functions from sim53
 *      09-03-2003 Bryan YC Fan :  Add MACvClearBusSusInd()& MACvEnableBusSusEn()
 *      09-18-2003 Jerry Chen   :  Add MACvSetKeyEntry & MACvDisableKeyEntry
 *
 */

#include "tmacro.h"
#include "tether.h"
#include "mac.h"

WORD TxRate_iwconfig;//2008-5-8 <add> by chester
/*---------------------  Static Definitions -------------------------*/
//static int          msglevel                =MSG_LEVEL_DEBUG;
static int          msglevel                =MSG_LEVEL_INFO;
/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/





/*
 * Description:
 *      Read All MAC Registers to buffer
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      pbyMacRegs  - buffer to read
 *
 * Return Value: none
 *
 */
VOID MACvReadAllRegs (DWORD_PTR dwIoBase, PBYTE pbyMacRegs)
{
    int ii;

    // read page0 register
    for (ii = 0; ii < MAC_MAX_CONTEXT_SIZE_PAGE0; ii++) {
        VNSvInPortB(dwIoBase + ii, pbyMacRegs);
        pbyMacRegs++;
    }

    MACvSelectPage1(dwIoBase);

    // read page1 register
    for (ii = 0; ii < MAC_MAX_CONTEXT_SIZE_PAGE1; ii++) {
        VNSvInPortB(dwIoBase + ii, pbyMacRegs);
        pbyMacRegs++;
    }

    MACvSelectPage0(dwIoBase);

}

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
 * Return Value: TRUE if all test bits On; otherwise FALSE
 *
 */
BOOL MACbIsRegBitsOn (DWORD_PTR dwIoBase, BYTE byRegOfs, BYTE byTestBits)
{
    BYTE byData;

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
 * Return Value: TRUE if all test bits Off; otherwise FALSE
 *
 */
BOOL MACbIsRegBitsOff (DWORD_PTR dwIoBase, BYTE byRegOfs, BYTE byTestBits)
{
    BYTE byData;

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
 * Return Value: TRUE if interrupt is disable; otherwise FALSE
 *
 */
BOOL MACbIsIntDisable (DWORD_PTR dwIoBase)
{
    DWORD dwData;

    VNSvInPortD(dwIoBase + MAC_REG_IMR, &dwData);
    if (dwData != 0)
        return FALSE;

    return TRUE;
}

/*
 * Description:
 *      Read MAC Multicast Address Mask
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      uByteidx    - Index of Mask
 *  Out:
 *      none
 *
 * Return Value: Mask Value read
 *
 */
BYTE MACbyReadMultiAddr (DWORD_PTR dwIoBase, UINT uByteIdx)
{
    BYTE byData;

    MACvSelectPage1(dwIoBase);
    VNSvInPortB(dwIoBase + MAC_REG_MAR0 + uByteIdx, &byData);
    MACvSelectPage0(dwIoBase);
    return byData;
}

/*
 * Description:
 *      Write MAC Multicast Address Mask
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      uByteidx    - Index of Mask
 *      byData      - Mask Value to write
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
VOID MACvWriteMultiAddr (DWORD_PTR dwIoBase, UINT uByteIdx, BYTE byData)
{
    MACvSelectPage1(dwIoBase);
    VNSvOutPortB(dwIoBase + MAC_REG_MAR0 + uByteIdx, byData);
    MACvSelectPage0(dwIoBase);
}

/*
 * Description:
 *      Set this hash index into multicast address register bit
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      byHashIdx   - Hash index to set
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetMultiAddrByHash (DWORD_PTR dwIoBase, BYTE byHashIdx)
{
    UINT uByteIdx;
    BYTE byBitMask;
    BYTE byOrgValue;

    // calculate byte position
    uByteIdx = byHashIdx / 8;
    ASSERT(uByteIdx < 8);
    // calculate bit position
    byBitMask = 1;
    byBitMask <<= (byHashIdx % 8);
    // turn on the bit
    byOrgValue = MACbyReadMultiAddr(dwIoBase, uByteIdx);
    MACvWriteMultiAddr(dwIoBase, uByteIdx, (BYTE)(byOrgValue | byBitMask));
}

/*
 * Description:
 *      Reset this hash index into multicast address register bit
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      byHashIdx   - Hash index to clear
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvResetMultiAddrByHash (DWORD_PTR dwIoBase, BYTE byHashIdx)
{
    UINT uByteIdx;
    BYTE byBitMask;
    BYTE byOrgValue;

    // calculate byte position
    uByteIdx = byHashIdx / 8;
    ASSERT(uByteIdx < 8);
    // calculate bit position
    byBitMask = 1;
    byBitMask <<= (byHashIdx % 8);
    // turn off the bit
    byOrgValue = MACbyReadMultiAddr(dwIoBase, uByteIdx);
    MACvWriteMultiAddr(dwIoBase, uByteIdx, (BYTE)(byOrgValue & (~byBitMask)));
}

/*
 * Description:
 *      Set Rx Threshold
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      byThreshold - Threshold Value
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetRxThreshold (DWORD_PTR dwIoBase, BYTE byThreshold)
{
    BYTE byOrgValue;

    ASSERT(byThreshold < 4);

    // set FCR0
    VNSvInPortB(dwIoBase + MAC_REG_FCR0, &byOrgValue);
    byOrgValue = (byOrgValue & 0xCF) | (byThreshold << 4);
    VNSvOutPortB(dwIoBase + MAC_REG_FCR0, byOrgValue);
}

/*
 * Description:
 *      Get Rx Threshold
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      pbyThreshold- Threshold Value Get
 *
 * Return Value: none
 *
 */
void MACvGetRxThreshold (DWORD_PTR dwIoBase, PBYTE pbyThreshold)
{
    // get FCR0
    VNSvInPortB(dwIoBase + MAC_REG_FCR0, pbyThreshold);
    *pbyThreshold = (*pbyThreshold >> 4) & 0x03;
}

/*
 * Description:
 *      Set Tx Threshold
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      byThreshold - Threshold Value
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetTxThreshold (DWORD_PTR dwIoBase, BYTE byThreshold)
{
    BYTE byOrgValue;

    ASSERT(byThreshold < 4);

    // set FCR0
    VNSvInPortB(dwIoBase + MAC_REG_FCR0, &byOrgValue);
    byOrgValue = (byOrgValue & 0xF3) | (byThreshold << 2);
    VNSvOutPortB(dwIoBase + MAC_REG_FCR0, byOrgValue);
}

/*
 * Description:
 *      Get Tx Threshold
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      pbyThreshold- Threshold Value Get
 *
 * Return Value: none
 *
 */
void MACvGetTxThreshold (DWORD_PTR dwIoBase, PBYTE pbyThreshold)
{
    // get FCR0
    VNSvInPortB(dwIoBase + MAC_REG_FCR0, pbyThreshold);
    *pbyThreshold = (*pbyThreshold >> 2) & 0x03;
}

/*
 * Description:
 *      Set Dma Length
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      byDmaLength - Dma Length Value
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetDmaLength (DWORD_PTR dwIoBase, BYTE byDmaLength)
{
    BYTE byOrgValue;

    ASSERT(byDmaLength < 4);

    // set FCR0
    VNSvInPortB(dwIoBase + MAC_REG_FCR0, &byOrgValue);
    byOrgValue = (byOrgValue & 0xFC) | byDmaLength;
    VNSvOutPortB(dwIoBase + MAC_REG_FCR0, byOrgValue);
}

/*
 * Description:
 *      Get Dma Length
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *  Out:
 *      pbyDmaLength- Dma Length Value Get
 *
 * Return Value: none
 *
 */
void MACvGetDmaLength (DWORD_PTR dwIoBase, PBYTE pbyDmaLength)
{
    // get FCR0
    VNSvInPortB(dwIoBase + MAC_REG_FCR0, pbyDmaLength);
    *pbyDmaLength &= 0x03;
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
void MACvSetShortRetryLimit (DWORD_PTR dwIoBase, BYTE byRetryLimit)
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
void MACvGetShortRetryLimit (DWORD_PTR dwIoBase, PBYTE pbyRetryLimit)
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
void MACvSetLongRetryLimit (DWORD_PTR dwIoBase, BYTE byRetryLimit)
{
    // set LRT
    VNSvOutPortB(dwIoBase + MAC_REG_LRT, byRetryLimit);
}

/*
 * Description:
 *      Get 802.11 Long Retry Limit
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
void MACvGetLongRetryLimit (DWORD_PTR dwIoBase, PBYTE pbyRetryLimit)
{
    // get LRT
    VNSvInPortB(dwIoBase + MAC_REG_LRT, pbyRetryLimit);
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
void MACvSetLoopbackMode (DWORD_PTR dwIoBase, BYTE byLoopbackMode)
{
    BYTE byOrgValue;

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
 *      Test if MAC in Loopback mode
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *  Out:
 *      none
 *
 * Return Value: TRUE if in Loopback mode; otherwise FALSE
 *
 */
BOOL MACbIsInLoopbackMode (DWORD_PTR dwIoBase)
{
    BYTE byOrgValue;

    VNSvInPortB(dwIoBase + MAC_REG_TEST, &byOrgValue);
    if (byOrgValue & (TEST_LBINT | TEST_LBEXT))
        return TRUE;
    return FALSE;
}

/*
 * Description:
 *      Set MAC Address filter
 *
 * Parameters:
 *  In:
 *      dwIoBase        - Base Address for MAC
 *      wFilterType     - Filter Type
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void MACvSetPacketFilter (DWORD_PTR dwIoBase, WORD wFilterType)
{
    BYTE    byOldRCR;
    BYTE    byNewRCR = 0;

    // if only in DIRECTED mode, multicast-address will set to zero,
    // but if other mode exist (e.g. PROMISCUOUS), multicast-address
    // will be open
    if (wFilterType & PKT_TYPE_DIRECTED) {
        // set multicast address to accept none
        MACvSelectPage1(dwIoBase);
        VNSvOutPortD(dwIoBase + MAC_REG_MAR0, 0L);
        VNSvOutPortD(dwIoBase + MAC_REG_MAR0 + sizeof(DWORD), 0L);
        MACvSelectPage0(dwIoBase);
    }

    if (wFilterType & (PKT_TYPE_PROMISCUOUS | PKT_TYPE_ALL_MULTICAST)) {
        // set multicast address to accept all
        MACvSelectPage1(dwIoBase);
        VNSvOutPortD(dwIoBase + MAC_REG_MAR0, 0xFFFFFFFFL);
        VNSvOutPortD(dwIoBase + MAC_REG_MAR0 + sizeof(DWORD), 0xFFFFFFFFL);
        MACvSelectPage0(dwIoBase);
    }

    if (wFilterType & PKT_TYPE_PROMISCUOUS) {

        byNewRCR |= (RCR_RXALLTYPE | RCR_UNICAST | RCR_MULTICAST | RCR_BROADCAST);

        byNewRCR &= ~RCR_BSSID;
    }

    if (wFilterType & (PKT_TYPE_ALL_MULTICAST | PKT_TYPE_MULTICAST))
        byNewRCR |= RCR_MULTICAST;

    if (wFilterType & PKT_TYPE_BROADCAST)
        byNewRCR |= RCR_BROADCAST;

    if (wFilterType & PKT_TYPE_ERROR_CRC)
        byNewRCR |= RCR_ERRCRC;

    VNSvInPortB(dwIoBase + MAC_REG_RCR,  &byOldRCR);
    if (byNewRCR != byOldRCR) {
        // Modify the Receive Command Register
        VNSvOutPortB(dwIoBase + MAC_REG_RCR, byNewRCR);
    }
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
void MACvSaveContext (DWORD_PTR dwIoBase, PBYTE pbyCxtBuf)
{
    int         ii;

    // read page0 register
    for (ii = 0; ii < MAC_MAX_CONTEXT_SIZE_PAGE0; ii++) {
        VNSvInPortB((dwIoBase + ii), (pbyCxtBuf + ii));
    }

    MACvSelectPage1(dwIoBase);

    // read page1 register
    for (ii = 0; ii < MAC_MAX_CONTEXT_SIZE_PAGE1; ii++) {
        VNSvInPortB((dwIoBase + ii), (pbyCxtBuf + MAC_MAX_CONTEXT_SIZE_PAGE0 + ii));
    }

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
VOID MACvRestoreContext (DWORD_PTR dwIoBase, PBYTE pbyCxtBuf)
{
    int         ii;

    MACvSelectPage1(dwIoBase);
    // restore page1
    for (ii = 0; ii < MAC_MAX_CONTEXT_SIZE_PAGE1; ii++) {
        VNSvOutPortB((dwIoBase + ii), *(pbyCxtBuf + MAC_MAX_CONTEXT_SIZE_PAGE0 + ii));
    }
    MACvSelectPage0(dwIoBase);

    // restore RCR,TCR,IMR...
    for (ii = MAC_REG_RCR; ii < MAC_REG_ISR; ii++) {
        VNSvOutPortB(dwIoBase + ii, *(pbyCxtBuf + ii));
    }
    // restore MAC Config.
    for (ii = MAC_REG_LRT; ii < MAC_REG_PAGE1SEL; ii++) {
        VNSvOutPortB(dwIoBase + ii, *(pbyCxtBuf + ii));
    }
    VNSvOutPortB(dwIoBase + MAC_REG_CFG, *(pbyCxtBuf + MAC_REG_CFG));

    // restore PS Config.
    for (ii = MAC_REG_PSCFG; ii < MAC_REG_BBREGCTL; ii++) {
        VNSvOutPortB(dwIoBase + ii, *(pbyCxtBuf + ii));
    }

    // restore CURR_RX_DESC_ADDR, CURR_TX_DESC_ADDR
    VNSvOutPortD(dwIoBase + MAC_REG_TXDMAPTR0, *(PDWORD)(pbyCxtBuf + MAC_REG_TXDMAPTR0));
    VNSvOutPortD(dwIoBase + MAC_REG_AC0DMAPTR, *(PDWORD)(pbyCxtBuf + MAC_REG_AC0DMAPTR));
    VNSvOutPortD(dwIoBase + MAC_REG_BCNDMAPTR, *(PDWORD)(pbyCxtBuf + MAC_REG_BCNDMAPTR));


    VNSvOutPortD(dwIoBase + MAC_REG_RXDMAPTR0, *(PDWORD)(pbyCxtBuf + MAC_REG_RXDMAPTR0));

    VNSvOutPortD(dwIoBase + MAC_REG_RXDMAPTR1, *(PDWORD)(pbyCxtBuf + MAC_REG_RXDMAPTR1));

}

/*
 * Description:
 *      Compare if MAC registers same as context buffer
 *
 * Parameters:
 *  In:
 *      dwIoBase    - Base Address for MAC
 *      pbyCxtBuf   - Context buffer
 *  Out:
 *      none
 *
 * Return Value: TRUE if all values are the same; otherwise FALSE
 *
 */
BOOL MACbCompareContext (DWORD_PTR dwIoBase, PBYTE pbyCxtBuf)
{
    DWORD       dwData;

    // compare MAC context to determine if this is a power lost init,
    // return TRUE for power remaining init, return FALSE for power lost init

    // compare CURR_RX_DESC_ADDR, CURR_TX_DESC_ADDR
    VNSvInPortD(dwIoBase + MAC_REG_TXDMAPTR0, &dwData);
    if (dwData != *(PDWORD)(pbyCxtBuf + MAC_REG_TXDMAPTR0)) {
        return FALSE;
    }

    VNSvInPortD(dwIoBase + MAC_REG_AC0DMAPTR, &dwData);
    if (dwData != *(PDWORD)(pbyCxtBuf + MAC_REG_AC0DMAPTR)) {
        return FALSE;
    }

    VNSvInPortD(dwIoBase + MAC_REG_RXDMAPTR0, &dwData);
    if (dwData != *(PDWORD)(pbyCxtBuf + MAC_REG_RXDMAPTR0)) {
        return FALSE;
    }

    VNSvInPortD(dwIoBase + MAC_REG_RXDMAPTR1, &dwData);
    if (dwData != *(PDWORD)(pbyCxtBuf + MAC_REG_RXDMAPTR1)) {
        return FALSE;
    }


    return TRUE;
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
 * Return Value: TRUE if Reset Success; otherwise FALSE
 *
 */
BOOL MACbSoftwareReset (DWORD_PTR dwIoBase)
{
    BYTE    byData;
    WORD    ww;

    // turn on HOSTCR_SOFTRST, just write 0x01 to reset
    //MACvRegBitsOn(dwIoBase, MAC_REG_HOSTCR, HOSTCR_SOFTRST);
    VNSvOutPortB(dwIoBase+ MAC_REG_HOSTCR, 0x01);

    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortB(dwIoBase + MAC_REG_HOSTCR, &byData);
        if ( !(byData & HOSTCR_SOFTRST))
            break;
    }
    if (ww == W_MAX_TIMEOUT)
        return FALSE;
    return TRUE;

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
 * Return Value: TRUE if success; otherwise FALSE
 *
 */
BOOL MACbSafeSoftwareReset (DWORD_PTR dwIoBase)
{
    BYTE    abyTmpRegData[MAC_MAX_CONTEXT_SIZE_PAGE0+MAC_MAX_CONTEXT_SIZE_PAGE1];
    BOOL    bRetVal;

    // PATCH....
    // save some important register's value, then do
    // reset, then restore register's value

    // save MAC context
    MACvSaveContext(dwIoBase, abyTmpRegData);
    // do reset
    bRetVal = MACbSoftwareReset(dwIoBase);
    //BBvSoftwareReset(pDevice->PortOffset);
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
 * Return Value: TRUE if success; otherwise FALSE
 *
 */
BOOL MACbSafeRxOff (DWORD_PTR dwIoBase)
{
    WORD    ww;
    DWORD   dwData;
    BYTE    byData;

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
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x10)\n");
        return(FALSE);
    }
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortD(dwIoBase + MAC_REG_RXDMACTL1, &dwData);
        if ( !(dwData & DMACTL_RUN))
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x11);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x11)\n");
        return(FALSE);
    }

    // try to safe shutdown RX
    MACvRegBitsOff(dwIoBase, MAC_REG_HOSTCR, HOSTCR_RXON);
    // W_MAX_TIMEOUT is the timeout period
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortB(dwIoBase + MAC_REG_HOSTCR, &byData);
        if ( !(byData & HOSTCR_RXONST))
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x12);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x12)\n");
        return(FALSE);
    }
    return TRUE;
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
 * Return Value: TRUE if success; otherwise FALSE
 *
 */
BOOL MACbSafeTxOff (DWORD_PTR dwIoBase)
{
    WORD    ww;
    DWORD   dwData;
    BYTE    byData;

    // Clear TX DMA
    //Tx0
    VNSvOutPortD(dwIoBase + MAC_REG_TXDMACTL0, DMACTL_CLRRUN);
    //AC0
    VNSvOutPortD(dwIoBase + MAC_REG_AC0DMACTL, DMACTL_CLRRUN);


    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortD(dwIoBase + MAC_REG_TXDMACTL0, &dwData);
        if ( !(dwData & DMACTL_RUN))
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x20);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x20)\n");
        return(FALSE);
    }
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortD(dwIoBase + MAC_REG_AC0DMACTL, &dwData);
        if ( !(dwData & DMACTL_RUN))
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x21);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x21)\n");
        return(FALSE);
    }

    // try to safe shutdown TX
    MACvRegBitsOff(dwIoBase, MAC_REG_HOSTCR, HOSTCR_TXON);

    // W_MAX_TIMEOUT is the timeout period
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortB(dwIoBase + MAC_REG_HOSTCR, &byData);
        if ( !(byData & HOSTCR_TXONST))
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x24);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x24)\n");
        return(FALSE);
    }
    return TRUE;
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
 * Return Value: TRUE if success; otherwise FALSE
 *
 */
BOOL MACbSafeStop (DWORD_PTR dwIoBase)
{
    MACvRegBitsOff(dwIoBase, MAC_REG_TCR, TCR_AUTOBCNTX);

    if (MACbSafeRxOff(dwIoBase) == FALSE) {
        DBG_PORT80(0xA1);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" MACbSafeRxOff == FALSE)\n");
        MACbSafeSoftwareReset(dwIoBase);
        return FALSE;
    }
    if (MACbSafeTxOff(dwIoBase) == FALSE) {
        DBG_PORT80(0xA2);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" MACbSafeTxOff == FALSE)\n");
        MACbSafeSoftwareReset(dwIoBase);
        return FALSE;
    }

    MACvRegBitsOff(dwIoBase, MAC_REG_HOSTCR, HOSTCR_MACEN);

    return TRUE;
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
 * Return Value: TRUE if success; otherwise FALSE
 *
 */
BOOL MACbShutdown (DWORD_PTR dwIoBase)
{
    // disable MAC IMR
    MACvIntDisable(dwIoBase);
    MACvSetLoopbackMode(dwIoBase, MAC_LB_INTERNAL);
    // stop the adapter
    if (!MACbSafeStop(dwIoBase)) {
        MACvSetLoopbackMode(dwIoBase, MAC_LB_NONE);
        return FALSE;
    }
    MACvSetLoopbackMode(dwIoBase, MAC_LB_NONE);
    return TRUE;
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
void MACvInitialize (DWORD_PTR dwIoBase)
{
    // clear sticky bits
    MACvClearStckDS(dwIoBase);
    // disable force PME-enable
    VNSvOutPortB(dwIoBase + MAC_REG_PMC1, PME_OVR);
    // only 3253 A
    /*
    MACvPwrEvntDisable(dwIoBase);
    // clear power status
    VNSvOutPortW(dwIoBase + MAC_REG_WAKEUPSR0, 0x0F0F);
    */

    // do reset
    MACbSoftwareReset(dwIoBase);

    // issue AUTOLD in EECSR to reload eeprom
    //MACvRegBitsOn(dwIoBase, MAC_REG_I2MCSR, I2MCSR_AUTOLD);
    // wait until EEPROM loading complete
    //while (TRUE) {
    //    U8 u8Data;
    //    VNSvInPortB(dwIoBase + MAC_REG_I2MCSR, &u8Data);
    //    if ( !(u8Data & I2MCSR_AUTOLD))
    //        break;
    //}

    // reset TSF counter
    VNSvOutPortB(dwIoBase + MAC_REG_TFTCTL, TFTCTL_TSFCNTRST);
    // enable TSF counter
    VNSvOutPortB(dwIoBase + MAC_REG_TFTCTL, TFTCTL_TSFCNTREN);


    // set packet filter
    // receive directed and broadcast address

    MACvSetPacketFilter(dwIoBase, PKT_TYPE_DIRECTED | PKT_TYPE_BROADCAST);

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
void MACvSetCurrRx0DescAddr (DWORD_PTR dwIoBase, DWORD dwCurrDescAddr)
{
WORD    ww;
BYTE    byData;
BYTE    byOrgDMACtl;

    VNSvInPortB(dwIoBase + MAC_REG_RXDMACTL0, &byOrgDMACtl);
    if (byOrgDMACtl & DMACTL_RUN) {
        VNSvOutPortB(dwIoBase + MAC_REG_RXDMACTL0+2, DMACTL_RUN);
    }
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortB(dwIoBase + MAC_REG_RXDMACTL0, &byData);
        if ( !(byData & DMACTL_RUN))
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x13);
    }
    VNSvOutPortD(dwIoBase + MAC_REG_RXDMAPTR0, dwCurrDescAddr);
    if (byOrgDMACtl & DMACTL_RUN) {
        VNSvOutPortB(dwIoBase + MAC_REG_RXDMACTL0, DMACTL_RUN);
    }
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
void MACvSetCurrRx1DescAddr (DWORD_PTR dwIoBase, DWORD dwCurrDescAddr)
{
WORD    ww;
BYTE    byData;
BYTE    byOrgDMACtl;

    VNSvInPortB(dwIoBase + MAC_REG_RXDMACTL1, &byOrgDMACtl);
    if (byOrgDMACtl & DMACTL_RUN) {
        VNSvOutPortB(dwIoBase + MAC_REG_RXDMACTL1+2, DMACTL_RUN);
    }
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortB(dwIoBase + MAC_REG_RXDMACTL1, &byData);
        if ( !(byData & DMACTL_RUN))
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x14);
    }
    VNSvOutPortD(dwIoBase + MAC_REG_RXDMAPTR1, dwCurrDescAddr);
    if (byOrgDMACtl & DMACTL_RUN) {
        VNSvOutPortB(dwIoBase + MAC_REG_RXDMACTL1, DMACTL_RUN);
    }
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
void MACvSetCurrTx0DescAddrEx (DWORD_PTR dwIoBase, DWORD dwCurrDescAddr)
{
WORD    ww;
BYTE    byData;
BYTE    byOrgDMACtl;

    VNSvInPortB(dwIoBase + MAC_REG_TXDMACTL0, &byOrgDMACtl);
    if (byOrgDMACtl & DMACTL_RUN) {
        VNSvOutPortB(dwIoBase + MAC_REG_TXDMACTL0+2, DMACTL_RUN);
    }
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortB(dwIoBase + MAC_REG_TXDMACTL0, &byData);
        if ( !(byData & DMACTL_RUN))
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x25);
    }
    VNSvOutPortD(dwIoBase + MAC_REG_TXDMAPTR0, dwCurrDescAddr);
    if (byOrgDMACtl & DMACTL_RUN) {
        VNSvOutPortB(dwIoBase + MAC_REG_TXDMACTL0, DMACTL_RUN);
    }
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
void MACvSetCurrAC0DescAddrEx (DWORD_PTR dwIoBase, DWORD dwCurrDescAddr)
{
WORD    ww;
BYTE    byData;
BYTE    byOrgDMACtl;

    VNSvInPortB(dwIoBase + MAC_REG_AC0DMACTL, &byOrgDMACtl);
    if (byOrgDMACtl & DMACTL_RUN) {
        VNSvOutPortB(dwIoBase + MAC_REG_AC0DMACTL+2, DMACTL_RUN);
    }
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortB(dwIoBase + MAC_REG_AC0DMACTL, &byData);
        if (!(byData & DMACTL_RUN))
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x26);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x26)\n");
    }
    VNSvOutPortD(dwIoBase + MAC_REG_AC0DMAPTR, dwCurrDescAddr);
    if (byOrgDMACtl & DMACTL_RUN) {
        VNSvOutPortB(dwIoBase + MAC_REG_AC0DMACTL, DMACTL_RUN);
    }
}



void MACvSetCurrTXDescAddr (int iTxType, DWORD_PTR dwIoBase, DWORD dwCurrDescAddr)
{
    if(iTxType == TYPE_AC0DMA){
        MACvSetCurrAC0DescAddrEx(dwIoBase, dwCurrDescAddr);
    }else if(iTxType == TYPE_TXDMA0){
        MACvSetCurrTx0DescAddrEx(dwIoBase, dwCurrDescAddr);
    }
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
VOID MACvTimer0MicroSDelay (DWORD_PTR dwIoBase, UINT uDelay)
{
BYTE byValue;
UINT uu,ii;

    VNSvOutPortB(dwIoBase + MAC_REG_TMCTL0, 0);
    VNSvOutPortD(dwIoBase + MAC_REG_TMDATA0, uDelay);
    VNSvOutPortB(dwIoBase + MAC_REG_TMCTL0, (TMCTL_TMD | TMCTL_TE));
    for(ii=0;ii<66;ii++) {  // assume max PCI clock is 66Mhz
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
void MACvOneShotTimer0MicroSec (DWORD_PTR dwIoBase, UINT uDelayTime)
{
    VNSvOutPortB(dwIoBase + MAC_REG_TMCTL0, 0);
    VNSvOutPortD(dwIoBase + MAC_REG_TMDATA0, uDelayTime);
    VNSvOutPortB(dwIoBase + MAC_REG_TMCTL0, (TMCTL_TMD | TMCTL_TE));
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
void MACvOneShotTimer1MicroSec (DWORD_PTR dwIoBase, UINT uDelayTime)
{
    VNSvOutPortB(dwIoBase + MAC_REG_TMCTL1, 0);
    VNSvOutPortD(dwIoBase + MAC_REG_TMDATA1, uDelayTime);
    VNSvOutPortB(dwIoBase + MAC_REG_TMCTL1, (TMCTL_TMD | TMCTL_TE));
}


void MACvSetMISCFifo (DWORD_PTR dwIoBase, WORD wOffset, DWORD dwData)
{
    if (wOffset > 273)
        return;
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
    VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
}


BOOL MACbTxDMAOff (DWORD_PTR dwIoBase, UINT idx)
{
BYTE byData;
UINT ww = 0;

    if (idx == TYPE_TXDMA0) {
        VNSvOutPortB(dwIoBase + MAC_REG_TXDMACTL0+2, DMACTL_RUN);
        for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
            VNSvInPortB(dwIoBase + MAC_REG_TXDMACTL0, &byData);
            if ( !(byData & DMACTL_RUN))
                break;
        }
    } else if (idx == TYPE_AC0DMA) {
        VNSvOutPortB(dwIoBase + MAC_REG_AC0DMACTL+2, DMACTL_RUN);
        for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
            VNSvInPortB(dwIoBase + MAC_REG_AC0DMACTL, &byData);
            if ( !(byData & DMACTL_RUN))
                break;
        }
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x29);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x29)\n");
        return FALSE;
    }
    return TRUE;
}

void MACvClearBusSusInd (DWORD_PTR dwIoBase)
{
    DWORD dwOrgValue;
    UINT ww;
    // check if BcnSusInd enabled
    VNSvInPortD(dwIoBase + MAC_REG_ENCFG , &dwOrgValue);
    if( !(dwOrgValue & EnCFG_BcnSusInd))
        return;
    //Set BcnSusClr
    dwOrgValue = dwOrgValue | EnCFG_BcnSusClr;
    VNSvOutPortD(dwIoBase + MAC_REG_ENCFG, dwOrgValue);
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortD(dwIoBase + MAC_REG_ENCFG , &dwOrgValue);
        if( !(dwOrgValue & EnCFG_BcnSusInd))
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x33);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x33)\n");
    }
}

void MACvEnableBusSusEn (DWORD_PTR dwIoBase)
{
    BYTE  byOrgValue;
    DWORD dwOrgValue;
    UINT ww;
    // check if BcnSusInd enabled
    VNSvInPortB(dwIoBase + MAC_REG_CFG , &byOrgValue);

    //Set BcnSusEn
    byOrgValue = byOrgValue | CFG_BCNSUSEN;
    VNSvOutPortB(dwIoBase + MAC_REG_ENCFG, byOrgValue);
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortD(dwIoBase + MAC_REG_ENCFG , &dwOrgValue);
        if(dwOrgValue & EnCFG_BcnSusInd)
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x34);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x34)\n");
    }
}

BOOL MACbFlushSYNCFifo (DWORD_PTR dwIoBase)
{
    BYTE  byOrgValue;
    UINT ww;
    // Read MACCR
    VNSvInPortB(dwIoBase + MAC_REG_MACCR , &byOrgValue);

    // Set SYNCFLUSH
    byOrgValue = byOrgValue | MACCR_SYNCFLUSH;
    VNSvOutPortB(dwIoBase + MAC_REG_MACCR, byOrgValue);

    // Check if SyncFlushOK
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortB(dwIoBase + MAC_REG_MACCR , &byOrgValue);
        if(byOrgValue & MACCR_SYNCFLUSHOK)
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x35);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x33)\n");
    }
    return TRUE;
}

BOOL MACbPSWakeup (DWORD_PTR dwIoBase)
{
    BYTE  byOrgValue;
    UINT ww;
    // Read PSCTL
    if (MACbIsRegBitsOff(dwIoBase, MAC_REG_PSCTL, PSCTL_PS)) {
        return TRUE;
    }
    // Disable PS
    MACvRegBitsOff(dwIoBase, MAC_REG_PSCTL, PSCTL_PSEN);

    // Check if SyncFlushOK
    for (ww = 0; ww < W_MAX_TIMEOUT; ww++) {
        VNSvInPortB(dwIoBase + MAC_REG_PSCTL , &byOrgValue);
        if(byOrgValue & PSCTL_WAKEDONE)
            break;
    }
    if (ww == W_MAX_TIMEOUT) {
        DBG_PORT80(0x36);
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO" DBG_PORT80(0x33)\n");
        return FALSE;
    }
    return TRUE;
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

void MACvSetKeyEntry (DWORD_PTR dwIoBase, WORD wKeyCtl, UINT uEntryIdx, UINT uKeyIdx, PBYTE pbyAddr, PDWORD pdwKey, BYTE byLocalID)
{
WORD    wOffset;
DWORD   dwData;
int     ii;

    if (byLocalID <= 1)
        return;


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"MACvSetKeyEntry\n");
    wOffset = MISCFIFO_KEYETRY0;
    wOffset += (uEntryIdx * MISCFIFO_KEYENTRYSIZE);

    dwData = 0;
    dwData |= wKeyCtl;
    dwData <<= 16;
    dwData |= MAKEWORD(*(pbyAddr+4), *(pbyAddr+5));
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"1. wOffset: %d, Data: %lX, KeyCtl:%X\n", wOffset, dwData, wKeyCtl);

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
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"2. wOffset: %d, Data: %lX\n", wOffset, dwData);

    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
    VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
    wOffset++;

    wOffset += (uKeyIdx * 4);
    for (ii=0;ii<4;ii++) {
        // alway push 128 bits
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"3.(%d) wOffset: %d, Data: %lX\n", ii, wOffset+ii, *pdwKey);
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
void MACvDisableKeyEntry (DWORD_PTR dwIoBase, UINT uEntryIdx)
{
WORD    wOffset;

    wOffset = MISCFIFO_KEYETRY0;
    wOffset += (uEntryIdx * MISCFIFO_KEYENTRYSIZE);

    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
    VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, 0);
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
}


/*
 * Description:
 *      Set the default Key (KeyEntry[10]) by MISCFIFO
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

void MACvSetDefaultKeyEntry (DWORD_PTR dwIoBase, UINT uKeyLen, UINT uKeyIdx, PDWORD pdwKey, BYTE byLocalID)
{
WORD    wOffset;
DWORD   dwData;
int     ii;

    if (byLocalID <= 1)
        return;

    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"MACvSetDefaultKeyEntry\n");
    wOffset = MISCFIFO_KEYETRY0;
    wOffset += (10 * MISCFIFO_KEYENTRYSIZE);

    wOffset++;
    wOffset++;
    wOffset += (uKeyIdx * 4);
    // alway push 128 bits
    for (ii=0; ii<3; ii++) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"(%d) wOffset: %d, Data: %lX\n", ii, wOffset+ii, *pdwKey);
        VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset+ii);
        VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, *pdwKey++);
        VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
    }
    dwData = *pdwKey;
    if (uKeyLen == WLAN_WEP104_KEYLEN) {
        dwData |= 0x80000000;
    }
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset+3);
    VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"End. wOffset: %d, Data: %lX\n", wOffset+3, dwData);

}


/*
 * Description:
 *      Enable default Key (KeyEntry[10]) by MISCFIFO
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
/*
void MACvEnableDefaultKey (DWORD_PTR dwIoBase, BYTE byLocalID)
{
WORD    wOffset;
DWORD   dwData;


    if (byLocalID <= 1)
        return;

    wOffset = MISCFIFO_KEYETRY0;
    wOffset += (10 * MISCFIFO_KEYENTRYSIZE);

    dwData = 0xC0440000;
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
    VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"MACvEnableDefaultKey: wOffset: %d, Data: %lX\n", wOffset, dwData);

}
*/

/*
 * Description:
 *      Disable default Key (KeyEntry[10]) by MISCFIFO
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
void MACvDisableDefaultKey (DWORD_PTR dwIoBase)
{
WORD    wOffset;
DWORD   dwData;


    wOffset = MISCFIFO_KEYETRY0;
    wOffset += (10 * MISCFIFO_KEYENTRYSIZE);

    dwData = 0x0;
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
    VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"MACvDisableDefaultKey: wOffset: %d, Data: %lX\n", wOffset, dwData);
}

/*
 * Description:
 *      Set the default TKIP Group Key (KeyEntry[10]) by MISCFIFO
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
void MACvSetDefaultTKIPKeyEntry (DWORD_PTR dwIoBase, UINT uKeyLen, UINT uKeyIdx, PDWORD pdwKey, BYTE byLocalID)
{
WORD    wOffset;
DWORD   dwData;
int     ii;

    if (byLocalID <= 1)
        return;


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"MACvSetDefaultTKIPKeyEntry\n");
    wOffset = MISCFIFO_KEYETRY0;
    // Kyle test : change offset from 10 -> 0
    wOffset += (10 * MISCFIFO_KEYENTRYSIZE);

    dwData = 0xC0660000;
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
    VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
    wOffset++;

    dwData = 0;
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
    VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
    wOffset++;

    wOffset += (uKeyIdx * 4);
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"1. wOffset: %d, Data: %lX, idx:%d\n", wOffset, *pdwKey, uKeyIdx);
    // alway push 128 bits
    for (ii=0; ii<4; ii++) {
        DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"2.(%d) wOffset: %d, Data: %lX\n", ii, wOffset+ii, *pdwKey);
        VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset+ii);
        VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, *pdwKey++);
        VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);
    }

}



/*
 * Description:
 *      Set the Key Control by MISCFIFO
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

void MACvSetDefaultKeyCtl (DWORD_PTR dwIoBase, WORD wKeyCtl, UINT uEntryIdx, BYTE byLocalID)
{
WORD    wOffset;
DWORD   dwData;

    if (byLocalID <= 1)
        return;


    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"MACvSetKeyEntry\n");
    wOffset = MISCFIFO_KEYETRY0;
    wOffset += (uEntryIdx * MISCFIFO_KEYENTRYSIZE);

    dwData = 0;
    dwData |= wKeyCtl;
    dwData <<= 16;
    dwData |= 0xffff;
    DBG_PRT(MSG_LEVEL_DEBUG, KERN_INFO"1. wOffset: %d, Data: %lX, KeyCtl:%X\n", wOffset, dwData, wKeyCtl);

    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFNDEX, wOffset);
    VNSvOutPortD(dwIoBase + MAC_REG_MISCFFDATA, dwData);
    VNSvOutPortW(dwIoBase + MAC_REG_MISCFFCTL, MISCFFCTL_WRITE);

}

