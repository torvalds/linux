/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTW_EEPROM_H__
#define __RTW_EEPROM_H__

#include "osdep_service.h"
#include "drv_types.h"

#define	HWSET_MAX_SIZE_512		512

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
