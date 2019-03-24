/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (c) 1996, 2003 VIA Networking Technologies, Inc.
 * All rights reserved.
 *
 * File: srom.h
 *
 * Purpose: Implement functions to access eeprom
 *
 * Author: Jerry Chen
 *
 * Date: Jan 29, 2003
 */

#ifndef __SROM_H__
#define __SROM_H__

/*---------------------  Export Definitions -------------------------*/

#define EEP_MAX_CONTEXT_SIZE    256

#define CB_EEPROM_READBYTE_WAIT 900     /* us */

#define W_MAX_I2CRETRY          0x0fff

/* Contents in the EEPROM */
#define EEP_OFS_PAR         0x00        /* physical address */
#define EEP_OFS_ANTENNA     0x16
#define EEP_OFS_RADIOCTL    0x17
#define EEP_OFS_RFTYPE      0x1B        /* for select RF */
#define EEP_OFS_MINCHANNEL  0x1C        /* Min Channel # */
#define EEP_OFS_MAXCHANNEL  0x1D        /* Max Channel # */
#define EEP_OFS_SIGNATURE   0x1E
#define EEP_OFS_ZONETYPE    0x1F
#define EEP_OFS_RFTABLE     0x20        /* RF POWER TABLE */
#define EEP_OFS_PWR_CCK     0x20
#define EEP_OFS_SETPT_CCK   0x21
#define EEP_OFS_PWR_OFDMG   0x23
#define EEP_OFS_SETPT_OFDMG 0x24
#define EEP_OFS_PWR_FORMULA_OST  0x26
#define EEP_OFS_MAJOR_VER 0x2E
#define EEP_OFS_MINOR_VER 0x2F
#define EEP_OFS_CCK_PWR_TBL     0x30
#define EEP_OFS_CCK_PWR_dBm     0x3F
#define EEP_OFS_OFDM_PWR_TBL    0x40
#define EEP_OFS_OFDM_PWR_dBm    0x4F
/*{{ RobertYu: 20041124 */
#define EEP_OFS_SETPT_OFDMA         0x4E
#define EEP_OFS_OFDMA_PWR_TBL       0x50
/*}}*/
#define EEP_OFS_OFDMA_PWR_dBm       0xD2

/*----------need to remove --------------------*/
#define EEP_OFS_BBTAB_LEN   0x70        /* BB Table Length */
#define EEP_OFS_BBTAB_ADR   0x71        /* BB Table Offset */
#define EEP_OFS_CHECKSUM    0xFF        /* reserved area for baseband 28h~78h */

#define EEP_I2C_DEV_ID      0x50        /* EEPROM device address on I2C bus */

/* Bits in EEP_OFS_ANTENNA */
#define EEP_ANTENNA_MAIN    0x01
#define EEP_ANTENNA_AUX     0x02
#define EEP_ANTINV          0x04

/* Bits in EEP_OFS_RADIOCTL */
#define EEP_RADIOCTL_ENABLE 0x80
#define EEP_RADIOCTL_INV    0x01

/*---------------------  Export Types  ------------------------------*/

/*---------------------  Export Macros ------------------------------*/

/*---------------------  Export Classes  ----------------------------*/

/*---------------------  Export Variables  --------------------------*/

/*---------------------  Export Functions  --------------------------*/

unsigned char SROMbyReadEmbedded(void __iomem *iobase,
				 unsigned char byContntOffset);

void SROMvReadAllContents(void __iomem *iobase, unsigned char *pbyEepromRegs);

void SROMvReadEtherAddress(void __iomem *iobase,
			   unsigned char *pbyEtherAddress);

#endif /* __EEPROM_H__*/
