/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTL871X_EEPROM_H__
#define __RTL871X_EEPROM_H__

#include "osdep_service.h"

#define	RTL8712_EEPROM_ID		0x8712
#define	EEPROM_MAX_SIZE			256
#define	CLOCK_RATE			50	/*100us*/

/*- EEPROM opcodes*/
#define EEPROM_READ_OPCODE		06
#define EEPROM_WRITE_OPCODE		05
#define EEPROM_ERASE_OPCODE		07
#define EEPROM_EWEN_OPCODE		19      /* Erase/write enable*/
#define EEPROM_EWDS_OPCODE		16      /* Erase/write disable*/

#define	EEPROM_CID_DEFAULT		0x0
#define	EEPROM_CID_ALPHA		0x1
#define	EEPROM_CID_Senao		0x3
#define	EEPROM_CID_NetCore		0x5
#define	EEPROM_CID_CAMEO		0X8
#define	EEPROM_CID_SITECOM		0x9
#define	EEPROM_CID_COREGA		0xB
#define	EEPROM_CID_EDIMAX_BELKIN	0xC
#define	EEPROM_CID_SERCOMM_BELKIN	0xE
#define	EEPROM_CID_CAMEO1		0xF
#define	EEPROM_CID_WNC_COREGA		0x12
#define	EEPROM_CID_CLEVO		0x13
#define	EEPROM_CID_WHQL			0xFE

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
	RT_CID_TOSHIBA = 9,
	RT_CID_819x_Netcore = 10,
	RT_CID_Nettronix = 11,
	RT_CID_DLINK = 12,
	RT_CID_PRONET = 13,
	RT_CID_COREGA = 14,
	RT_CID_819x_ALPHA = 15,
	RT_CID_819x_Sitecom = 16,
	RT_CID_CCX = 17,
	RT_CID_819x_Lenovo = 18,
	RT_CID_819x_QMI = 19,
	RT_CID_819x_Edimax_Belkin = 20,
	RT_CID_819x_Sercomm_Belkin = 21,
	RT_CID_819x_CAMEO1 = 22,
	RT_CID_819x_MSI = 23,
	RT_CID_819x_Acer = 24,
	RT_CID_819x_AzWave_ASUS = 25,
	RT_CID_819x_AzWave = 26,
	RT_CID_819x_WNC_COREGA = 27,
	RT_CID_819x_CLEVO = 28,
};

struct eeprom_priv {
	u8 bautoload_fail_flag;
	u8 bempty;
	u8 sys_config;
	u8 mac_addr[6];
	u8 config0;
	u16 channel_plan;
	u8 country_string[3];
	u8 tx_power_b[15];
	u8 tx_power_g[15];
	u8 tx_power_a[201];
	u8 efuse_eeprom_data[EEPROM_MAX_SIZE];
	enum RT_CUSTOMER_ID CustomerID;
};

void r8712_eeprom_write16(struct _adapter *padapter, u16 reg, u16 data);
u16 r8712_eeprom_read16(struct _adapter *padapter, u16 reg);

#endif  /*__RTL871X_EEPROM_H__*/

