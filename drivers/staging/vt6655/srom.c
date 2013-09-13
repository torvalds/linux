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
unsigned char SROMbyReadEmbedded(unsigned long dwIoBase, unsigned char byContntOffset)
{
	unsigned short wDelay, wNoACK;
	unsigned char byWait;
	unsigned char byData;
	unsigned char byOrg;

	byData = 0xFF;
	VNSvInPortB(dwIoBase + MAC_REG_I2MCFG, &byOrg);
	/* turn off hardware retry for getting NACK */
	VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, (byOrg & (~I2MCFG_NORETRY)));
	for (wNoACK = 0; wNoACK < W_MAX_I2CRETRY; wNoACK++) {
		VNSvOutPortB(dwIoBase + MAC_REG_I2MTGID, EEP_I2C_DEV_ID);
		VNSvOutPortB(dwIoBase + MAC_REG_I2MTGAD, byContntOffset);

		/* issue read command */
		VNSvOutPortB(dwIoBase + MAC_REG_I2MCSR, I2MCSR_EEMR);
		/* wait DONE be set */
		for (wDelay = 0; wDelay < W_MAX_TIMEOUT; wDelay++) {
			VNSvInPortB(dwIoBase + MAC_REG_I2MCSR, &byWait);
			if (byWait & (I2MCSR_DONE | I2MCSR_NACK))
				break;
			PCAvDelayByIO(CB_DELAY_LOOP_WAIT);
		}
		if ((wDelay < W_MAX_TIMEOUT) &&
		    (!(byWait & I2MCSR_NACK))) {
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
 * Return Value: true if succeeded; false if failed.
 *
 */
bool SROMbWriteEmbedded(unsigned long dwIoBase, unsigned char byContntOffset, unsigned char byData)
{
	unsigned short wDelay, wNoACK;
	unsigned char byWait;

	unsigned char byOrg;

	VNSvInPortB(dwIoBase + MAC_REG_I2MCFG, &byOrg);
	/* turn off hardware retry for getting NACK */
	VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, (byOrg & (~I2MCFG_NORETRY)));
	for (wNoACK = 0; wNoACK < W_MAX_I2CRETRY; wNoACK++) {
		VNSvOutPortB(dwIoBase + MAC_REG_I2MTGID, EEP_I2C_DEV_ID);
		VNSvOutPortB(dwIoBase + MAC_REG_I2MTGAD, byContntOffset);
		VNSvOutPortB(dwIoBase + MAC_REG_I2MDOPT, byData);

		/* issue write command */
		VNSvOutPortB(dwIoBase + MAC_REG_I2MCSR, I2MCSR_EEMW);
		/* wait DONE be set */
		for (wDelay = 0; wDelay < W_MAX_TIMEOUT; wDelay++) {
			VNSvInPortB(dwIoBase + MAC_REG_I2MCSR, &byWait);
			if (byWait & (I2MCSR_DONE | I2MCSR_NACK))
				break;
			PCAvDelayByIO(CB_DELAY_LOOP_WAIT);
		}

		if ((wDelay < W_MAX_TIMEOUT) &&
		    (!(byWait & I2MCSR_NACK))) {
			break;
		}
	}
	if (wNoACK == W_MAX_I2CRETRY) {
		VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, byOrg);
		return false;
	}
	VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, byOrg);
	return true;
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
void SROMvRegBitsOn(unsigned long dwIoBase, unsigned char byContntOffset, unsigned char byBits)
{
	unsigned char byOrgData;

	byOrgData = SROMbyReadEmbedded(dwIoBase, byContntOffset);
	SROMbWriteEmbedded(dwIoBase, byContntOffset, (unsigned char)(byOrgData | byBits));
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
void SROMvRegBitsOff(unsigned long dwIoBase, unsigned char byContntOffset, unsigned char byBits)
{
	unsigned char byOrgData;

	byOrgData = SROMbyReadEmbedded(dwIoBase, byContntOffset);
	SROMbWriteEmbedded(dwIoBase, byContntOffset, (unsigned char)(byOrgData & (~byBits)));
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
 * Return Value: true if all test bits on; otherwise false
 *
 */
bool SROMbIsRegBitsOn(unsigned long dwIoBase, unsigned char byContntOffset, unsigned char byTestBits)
{
	unsigned char byOrgData;

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
 * Return Value: true if all test bits off; otherwise false
 *
 */
bool SROMbIsRegBitsOff(unsigned long dwIoBase, unsigned char byContntOffset, unsigned char byTestBits)
{
	unsigned char byOrgData;

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
void SROMvReadAllContents(unsigned long dwIoBase, unsigned char *pbyEepromRegs)
{
	int     ii;

	/* ii = Rom Address */
	for (ii = 0; ii < EEP_MAX_CONTEXT_SIZE; ii++) {
		*pbyEepromRegs = SROMbyReadEmbedded(dwIoBase, (unsigned char)ii);
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
void SROMvWriteAllContents(unsigned long dwIoBase, unsigned char *pbyEepromRegs)
{
	int     ii;

	/* ii = Rom Address */
	for (ii = 0; ii < EEP_MAX_CONTEXT_SIZE; ii++) {
		SROMbWriteEmbedded(dwIoBase, (unsigned char)ii, *pbyEepromRegs);
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
void SROMvReadEtherAddress(unsigned long dwIoBase, unsigned char *pbyEtherAddress)
{
	unsigned char ii;

	/* ii = Rom Address */
	for (ii = 0; ii < ETH_ALEN; ii++) {
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
void SROMvWriteEtherAddress(unsigned long dwIoBase, unsigned char *pbyEtherAddress)
{
	unsigned char ii;

	/* ii = Rom Address */
	for (ii = 0; ii < ETH_ALEN; ii++) {
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
void SROMvReadSubSysVenId(unsigned long dwIoBase, unsigned long *pdwSubSysVenId)
{
	unsigned char *pbyData;

	pbyData = (unsigned char *)pdwSubSysVenId;
	/* sub vendor */
	*pbyData = SROMbyReadEmbedded(dwIoBase, 6);
	*(pbyData+1) = SROMbyReadEmbedded(dwIoBase, 7);
	/* sub system */
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
 * Return Value: true if success; otherwise false
 *
 */
bool SROMbAutoLoad(unsigned long dwIoBase)
{
	unsigned char byWait;
	int     ii;

	unsigned char byOrg;

	VNSvInPortB(dwIoBase + MAC_REG_I2MCFG, &byOrg);
	/* turn on hardware retry */
	VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, (byOrg | I2MCFG_NORETRY));

	MACvRegBitsOn(dwIoBase, MAC_REG_I2MCSR, I2MCSR_AUTOLD);

	/* ii = Rom Address */
	for (ii = 0; ii < EEP_MAX_CONTEXT_SIZE; ii++) {
		MACvTimer0MicroSDelay(dwIoBase, CB_EEPROM_READBYTE_WAIT);
		VNSvInPortB(dwIoBase + MAC_REG_I2MCSR, &byWait);
		if (!(byWait & I2MCSR_AUTOLD))
			break;
	}

	VNSvOutPortB(dwIoBase + MAC_REG_I2MCFG, byOrg);

	if (ii == EEP_MAX_CONTEXT_SIZE)
		return false;
	return true;
}
