// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
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

#include "device.h"
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
 *      iobase          - I/O base address
 *      contnt_offset  - address of EEPROM
 *  Out:
 *      none
 *
 * Return Value: data read
 *
 */
unsigned char SROMbyReadEmbedded(void __iomem *iobase,
				 unsigned char contnt_offset)
{
	unsigned short wDelay, wNoACK;
	unsigned char byWait;
	unsigned char byData;
	unsigned char byOrg;

	byData = 0xFF;
	byOrg = ioread8(iobase + MAC_REG_I2MCFG);
	/* turn off hardware retry for getting NACK */
	iowrite8(byOrg & (~I2MCFG_NORETRY), iobase + MAC_REG_I2MCFG);
	for (wNoACK = 0; wNoACK < W_MAX_I2CRETRY; wNoACK++) {
		iowrite8(EEP_I2C_DEV_ID, iobase + MAC_REG_I2MTGID);
		iowrite8(contnt_offset, iobase + MAC_REG_I2MTGAD);

		/* issue read command */
		iowrite8(I2MCSR_EEMR, iobase + MAC_REG_I2MCSR);
		/* wait DONE be set */
		for (wDelay = 0; wDelay < W_MAX_TIMEOUT; wDelay++) {
			byWait = ioread8(iobase + MAC_REG_I2MCSR);
			if (byWait & (I2MCSR_DONE | I2MCSR_NACK))
				break;
			udelay(CB_DELAY_LOOP_WAIT);
		}
		if ((wDelay < W_MAX_TIMEOUT) &&
		    (!(byWait & I2MCSR_NACK))) {
			break;
		}
	}
	byData = ioread8(iobase + MAC_REG_I2MDIPT);
	iowrite8(byOrg, iobase + MAC_REG_I2MCFG);
	return byData;
}

/*
 * Description: Read all contents of eeprom to buffer
 *
 * Parameters:
 *  In:
 *      iobase          - I/O base address
 *  Out:
 *      pbyEepromRegs   - EEPROM content Buffer
 *
 * Return Value: none
 *
 */
void SROMvReadAllContents(void __iomem *iobase, unsigned char *pbyEepromRegs)
{
	int     ii;

	/* ii = Rom Address */
	for (ii = 0; ii < EEP_MAX_CONTEXT_SIZE; ii++) {
		*pbyEepromRegs = SROMbyReadEmbedded(iobase,
						    (unsigned char)ii);
		pbyEepromRegs++;
	}
}

/*
 * Description: Read Ethernet Address from eeprom to buffer
 *
 * Parameters:
 *  In:
 *      iobase          - I/O base address
 *  Out:
 *      pbyEtherAddress - Ethernet Address buffer
 *
 * Return Value: none
 *
 */
void SROMvReadEtherAddress(void __iomem *iobase,
			   unsigned char *pbyEtherAddress)
{
	unsigned char ii;

	/* ii = Rom Address */
	for (ii = 0; ii < ETH_ALEN; ii++) {
		*pbyEtherAddress = SROMbyReadEmbedded(iobase, ii);
		pbyEtherAddress++;
	}
}
