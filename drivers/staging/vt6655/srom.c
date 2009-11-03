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
 * File: srom.c
 *
 * Purpose:Implement functions to access eeprom
 *
 * Author: Jerry Chen
 *
 * Date: Jan 29, 2003
 *
 * Functions:
 *      SROMbyReadEmbedded - Embedded read eeprom via MAC
 *      SROMbWriteEmbedded - Embedded write eeprom via MAC
 *      SROMvRegBitsOn - Set Bits On in eeprom
 *      SROMvRegBitsOff - Clear Bits Off in eeprom
 *      SROMbIsRegBitsOn - Test if Bits On in eeprom
 *      SROMbIsRegBitsOff - Test if Bits Off in eeprom
 *      SROMvReadAllContents - Read all contents in eeprom
 *      SROMvWriteAllContents - Write all contents in eeprom
 *      SROMvReadEtherAddress - Read Ethernet Address in eeprom
 *      SROMvWriteEtherAddress - Write Ethernet Address in eeprom
 *      SROMvReadSubSysVenId - Read Sub_VID and Sub_SysId in eeprom
 *      SROMbAutoLoad - Auto Load eeprom to MAC register
 *
 * Revision History:
 *
 */

#include "upc.h"
#include "tmacro.h"
#include "tether.h"
#include "mac.h"
#include "srom.h"

/*---------------------  Static Definitions -------------------------*/

/*---------------------  Static Classes  ----------------------------*/

/*---------------------  Static Variables  --------------------------*/

/*---------------------  Static Functions  --------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/




/*
 * Description: Read a byte from EEPROM, by MAC I2C
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *      byContntOffset  - address of EEPROM
 *  Out:
 *      none
 *
 * Return Value: data read
 *
 */
BYTE SROMbyReadEmbedded(DWORD_PTR dwIoBase, BYTE byContntOffset)
{
    WORD    wDelay, wNoACK;
    BYTE    byWait;
    BYTE    byData;
    BYTE    byOrg;

    byData = 0xFF;
    VNSvInPortB(dwIoBase + MAC_REG_I2MCFG, &byOrg);
    // turn off hardware retry for getting NACK
    VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, (byOrg & (~I2MCFG_NORETRY)));
    for (wNoACK = 0; wNoACK < W_MAX_I2CRETRY; wNoACK++) {
        VNSvOutPortB(dwIoBase + MAC_REG_I2MTGID, EEP_I2C_DEV_ID);
        VNSvOutPortB(dwIoBase + MAC_REG_I2MTGAD, byContntOffset);

        // issue read command
        VNSvOutPortB(dwIoBase + MAC_REG_I2MCSR, I2MCSR_EEMR);
        // wait DONE be set
        for (wDelay = 0; wDelay < W_MAX_TIMEOUT; wDelay++) {
            VNSvInPortB(dwIoBase + MAC_REG_I2MCSR, &byWait);
            if (byWait & (I2MCSR_DONE | I2MCSR_NACK))
                break;
            PCAvDelayByIO(CB_DELAY_LOOP_WAIT);
        }
        if ((wDelay < W_MAX_TIMEOUT) &&
             ( !(byWait & I2MCSR_NACK))) {
            break;
        }
    }
    VNSvInPortB(dwIoBase + MAC_REG_I2MDIPT, &byData);
    VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, byOrg);
    return byData;
}


/*
 * Description: Write a byte to EEPROM, by MAC I2C
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *      byContntOffset  - address of EEPROM
 *      wData           - data to write
 *  Out:
 *      none
 *
 * Return Value: TRUE if succeeded; FALSE if failed.
 *
 */
BOOL SROMbWriteEmbedded (DWORD_PTR dwIoBase, BYTE byContntOffset, BYTE byData)
{
    WORD    wDelay, wNoACK;
    BYTE    byWait;

    BYTE    byOrg;

    VNSvInPortB(dwIoBase + MAC_REG_I2MCFG, &byOrg);
    // turn off hardware retry for getting NACK
    VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, (byOrg & (~I2MCFG_NORETRY)));
    for (wNoACK = 0; wNoACK < W_MAX_I2CRETRY; wNoACK++) {
        VNSvOutPortB(dwIoBase + MAC_REG_I2MTGID, EEP_I2C_DEV_ID);
        VNSvOutPortB(dwIoBase + MAC_REG_I2MTGAD, byContntOffset);
        VNSvOutPortB(dwIoBase + MAC_REG_I2MDOPT, byData);

        // issue write command
        VNSvOutPortB(dwIoBase + MAC_REG_I2MCSR, I2MCSR_EEMW);
        // wait DONE be set
        for (wDelay = 0; wDelay < W_MAX_TIMEOUT; wDelay++) {
            VNSvInPortB(dwIoBase + MAC_REG_I2MCSR, &byWait);
            if (byWait & (I2MCSR_DONE | I2MCSR_NACK))
                break;
            PCAvDelayByIO(CB_DELAY_LOOP_WAIT);
        }

        if ((wDelay < W_MAX_TIMEOUT) &&
             ( !(byWait & I2MCSR_NACK))) {
            break;
        }
    }
    if (wNoACK == W_MAX_I2CRETRY) {
        VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, byOrg);
        return FALSE;
    }
    VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, byOrg);
    return TRUE;
}


/*
 * Description: Turn bits on in eeprom
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *      byContntOffset  - address of EEPROM
 *      byBits          - bits to turn on
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void SROMvRegBitsOn (DWORD_PTR dwIoBase, BYTE byContntOffset, BYTE byBits)
{
    BYTE    byOrgData;

    byOrgData = SROMbyReadEmbedded(dwIoBase, byContntOffset);
    SROMbWriteEmbedded(dwIoBase, byContntOffset,(BYTE)(byOrgData | byBits));
}


/*
 * Description: Turn bits off in eeprom
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *      byContntOffset  - address of EEPROM
 *      byBits          - bits to turn off
 *  Out:
 *      none
 *
 */
void SROMvRegBitsOff (DWORD_PTR dwIoBase, BYTE byContntOffset, BYTE byBits)
{
    BYTE    byOrgData;

    byOrgData = SROMbyReadEmbedded(dwIoBase, byContntOffset);
    SROMbWriteEmbedded(dwIoBase, byContntOffset,(BYTE)(byOrgData & (~byBits)));
}


/*
 * Description: Test if bits on in eeprom
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *      byContntOffset  - address of EEPROM
 *      byTestBits      - bits to test
 *  Out:
 *      none
 *
 * Return Value: TRUE if all test bits on; otherwise FALSE
 *
 */
BOOL SROMbIsRegBitsOn (DWORD_PTR dwIoBase, BYTE byContntOffset, BYTE byTestBits)
{
    BYTE    byOrgData;

    byOrgData = SROMbyReadEmbedded(dwIoBase, byContntOffset);
    return (byOrgData & byTestBits) == byTestBits;
}


/*
 * Description: Test if bits off in eeprom
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *      byContntOffset  - address of EEPROM
 *      byTestBits      - bits to test
 *  Out:
 *      none
 *
 * Return Value: TRUE if all test bits off; otherwise FALSE
 *
 */
BOOL SROMbIsRegBitsOff (DWORD_PTR dwIoBase, BYTE byContntOffset, BYTE byTestBits)
{
    BYTE    byOrgData;

    byOrgData = SROMbyReadEmbedded(dwIoBase, byContntOffset);
    return !(byOrgData & byTestBits);
}


/*
 * Description: Read all contents of eeprom to buffer
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *  Out:
 *      pbyEepromRegs   - EEPROM content Buffer
 *
 * Return Value: none
 *
 */
void SROMvReadAllContents (DWORD_PTR dwIoBase, PBYTE pbyEepromRegs)
{
    int     ii;

    // ii = Rom Address
    for (ii = 0; ii < EEP_MAX_CONTEXT_SIZE; ii++) {
        *pbyEepromRegs = SROMbyReadEmbedded(dwIoBase,(BYTE) ii);
        pbyEepromRegs++;
    }
}


/*
 * Description: Write all contents of buffer to eeprom
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *      pbyEepromRegs   - EEPROM content Buffer
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void SROMvWriteAllContents (DWORD_PTR dwIoBase, PBYTE pbyEepromRegs)
{
    int     ii;

    // ii = Rom Address
    for (ii = 0; ii < EEP_MAX_CONTEXT_SIZE; ii++) {
        SROMbWriteEmbedded(dwIoBase,(BYTE) ii, *pbyEepromRegs);
        pbyEepromRegs++;
    }
}


/*
 * Description: Read Ethernet Address from eeprom to buffer
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *  Out:
 *      pbyEtherAddress - Ethernet Address buffer
 *
 * Return Value: none
 *
 */
void SROMvReadEtherAddress (DWORD_PTR dwIoBase, PBYTE pbyEtherAddress)
{
    BYTE     ii;

    // ii = Rom Address
    for (ii = 0; ii < U_ETHER_ADDR_LEN; ii++) {
        *pbyEtherAddress = SROMbyReadEmbedded(dwIoBase, ii);
        pbyEtherAddress++;
    }
}


/*
 * Description: Write Ethernet Address from buffer to eeprom
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *      pbyEtherAddress - Ethernet Address buffer
 *  Out:
 *      none
 *
 * Return Value: none
 *
 */
void SROMvWriteEtherAddress (DWORD_PTR dwIoBase, PBYTE pbyEtherAddress)
{
    BYTE     ii;

    // ii = Rom Address
    for (ii = 0; ii < U_ETHER_ADDR_LEN; ii++) {
        SROMbWriteEmbedded(dwIoBase, ii, *pbyEtherAddress);
        pbyEtherAddress++;
    }
}


/*
 * Description: Read Sub_VID and Sub_SysId from eeprom to buffer
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *  Out:
 *      pdwSubSysVenId  - Sub_VID and Sub_SysId read
 *
 * Return Value: none
 *
 */
void SROMvReadSubSysVenId (DWORD_PTR dwIoBase, PDWORD pdwSubSysVenId)
{
    PBYTE   pbyData;

    pbyData = (PBYTE)pdwSubSysVenId;
    // sub vendor
    *pbyData = SROMbyReadEmbedded(dwIoBase, 6);
    *(pbyData+1) = SROMbyReadEmbedded(dwIoBase, 7);
    // sub system
    *(pbyData+2) = SROMbyReadEmbedded(dwIoBase, 8);
    *(pbyData+3) = SROMbyReadEmbedded(dwIoBase, 9);
}

/*
 * Description: Auto Load EEPROM to MAC register
 *
 * Parameters:
 *  In:
 *      dwIoBase        - I/O base address
 *  Out:
 *      none
 *
 * Return Value: TRUE if success; otherwise FALSE
 *
 */
BOOL SROMbAutoLoad (DWORD_PTR dwIoBase)
{
    BYTE    byWait;
    int     ii;

    BYTE    byOrg;

    VNSvInPortB(dwIoBase + MAC_REG_I2MCFG, &byOrg);
    // turn on hardware retry
    VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, (byOrg | I2MCFG_NORETRY));

    MACvRegBitsOn(dwIoBase, MAC_REG_I2MCSR, I2MCSR_AUTOLD);

    // ii = Rom Address
    for (ii = 0; ii < EEP_MAX_CONTEXT_SIZE; ii++) {
        MACvTimer0MicroSDelay(dwIoBase, CB_EEPROM_READBYTE_WAIT);
        VNSvInPortB(dwIoBase + MAC_REG_I2MCSR, &byWait);
        if ( !(byWait & I2MCSR_AUTOLD))
            break;
    }

    VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, byOrg);

    if (ii == EEP_MAX_CONTEXT_SIZE)
        return FALSE;
    return TRUE;
}


