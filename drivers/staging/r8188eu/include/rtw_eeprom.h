/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTW_EEPROM_H__
#define __RTW_EEPROM_H__

#include "osdep_service.h"
#include "drv_types.h"

#define	RTL8712_EEPROM_ID		0x8712

#define	HWSET_MAX_SIZE_512		512
#define	EEPROM_MAX_SIZE			HWSET_MAX_SIZE_512

#define	CLOCK_RATE			50	/* 100us */

/*  EEPROM opcodes */
#define EEPROM_READ_OPCODE		06
#define EEPROM_WRITE_OPCODE		05
#define EEPROM_ERASE_OPCODE		07
#define EEPROM_EWEN_OPCODE		19      /*  Erase/write enable */
#define EEPROM_EWDS_OPCODE		16      /*  Erase/write disable */

/* Country codes */
#define USA				0x555320
#define EUROPE				0x1 /* temp, should be provided later */
#define JAPAN				0x2 /* temp, should be provided later */

#define	EEPROM_CID_DEFAULT		0x0
#define	EEPROM_CID_ALPHA		0x1
#define	EEPROM_CID_Senao		0x3
#define	EEPROM_CID_NetCore		0x5
#define	EEPROM_CID_CAMEO		0X8
#define	EEPROM_CID_SITECOM		0x9
#define	EEPROM_CID_COREGA		0xB
#define	EEPROM_CID_EDIMAX_BELK		0xC
#define	EEPROM_CID_SERCOMM_BELK		0xE
#define	EEPROM_CID_CAMEO1		0xF
#define	EEPROM_CID_WNC_COREGA		0x12
#define	EEPROM_CID_CLEVO		0x13
#define	EEPROM_CID_WHQL			0xFE

struct eeprom_priv {
	u8		bautoload_fail_flag;
	u8		bloadfile_fail_flag;
	u8		bloadmac_fail_flag;
	u8		mac_addr[ETH_ALEN] __aligned(2); /* PermanentAddress */
	u16		channel_plan;
	u8		EepromOrEfuse;
	u8		efuse_eeprom_data[HWSET_MAX_SIZE_512] __aligned(4);
};

void eeprom_write16(struct adapter *padapter, u16 reg, u16 data);
u16 eeprom_read16(struct adapter *padapter, u16 reg);
void read_eeprom_content(struct adapter *padapter);
void eeprom_read_sz(struct adapter *adapt, u16 reg, u8 *data, u32 sz);
void read_eeprom_content_by_attrib(struct adapter *padapter);

#endif  /* __RTL871X_EEPROM_H__ */
