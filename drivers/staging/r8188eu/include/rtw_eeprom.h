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

/*  Customer ID, note that: */
/*  This variable is initiailzed through EEPROM or registry, */
/*  however, its definition may be different with that in EEPROM for */
/*  EEPROM size consideration. So, we have to perform proper translation
 *  between them. */
/*  Besides, CustomerID of registry has precedence of that of EEPROM. */
/*  defined below. 060703, by rcnjko. */
enum RT_CUSTOMER_ID {
	RT_CID_DEFAULT = 0,
	RT_CID_8187_ALPHA0 = 1,
	RT_CID_8187_SERCOMM_PS = 2,
	RT_CID_8187_HW_LED = 3,
	RT_CID_8187_NETGEAR = 4,
	RT_CID_WHQL = 5,
	RT_CID_819x_CAMEO  = 6,
	RT_CID_819x_RUNTOP = 7,
	RT_CID_819x_Senao = 8,
	RT_CID_TOSHIBA = 9,	/*  Merge by Jacken, 2008/01/31. */
	RT_CID_819x_Netcore = 10,
	RT_CID_Nettronix = 11,
	RT_CID_DLINK = 12,
	RT_CID_PRONET = 13,
	RT_CID_COREGA = 14,
	RT_CID_CHINA_MOBILE = 15,
	RT_CID_819x_ALPHA = 16,
	RT_CID_819x_Sitecom = 17,
	RT_CID_CCX = 18, /*  It's set under CCX logo test and isn't demanded
			  * for CCX functions, but for test behavior like retry
			  * limit and tx report. By Bruce, 2009-02-17. */
	RT_CID_819x_Lenovo = 19,
	RT_CID_819x_QMI = 20,
	RT_CID_819x_Edimax_Belkin = 21,
	RT_CID_819x_Sercomm_Belkin = 22,
	RT_CID_819x_CAMEO1 = 23,
	RT_CID_819x_MSI = 24,
	RT_CID_819x_Acer = 25,
	RT_CID_819x_AzWave_ASUS = 26,
	RT_CID_819x_AzWave = 27, /*  For AzWave in PCIe,i
				  * The ID is AzWave use and not only Asus */
	RT_CID_819x_HP = 28,
	RT_CID_819x_WNC_COREGA = 29,
	RT_CID_819x_Arcadyan_Belkin = 30,
	RT_CID_819x_SAMSUNG = 31,
	RT_CID_819x_CLEVO = 32,
	RT_CID_819x_DELL = 33,
	RT_CID_819x_PRONETS = 34,
	RT_CID_819x_Edimax_ASUS = 35,
	RT_CID_819x_CAMEO_NETGEAR = 36,
	RT_CID_PLANEX = 37,
	RT_CID_CC_C = 38,
	RT_CID_819x_Xavi = 39,
	RT_CID_819x_FUNAI_TV = 40,
	RT_CID_819x_ALPHA_WD=41,
};

struct eeprom_priv {
	u8		bautoload_fail_flag;
	u8		bloadfile_fail_flag;
	u8		bloadmac_fail_flag;
	u8		mac_addr[6];	/* PermanentAddress */
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
