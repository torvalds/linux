/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTW_EEPROM_H__
#define __RTW_EEPROM_H__

#include "osdep_service.h"
#include "drv_types.h"

struct eeprom_priv {
	u8		bautoload_fail_flag;
	u8		mac_addr[ETH_ALEN] __aligned(2); /* PermanentAddress */
};

#endif  /* __RTL871X_EEPROM_H__ */
