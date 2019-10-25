// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#define _RTW_DEBUG_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <hal_btcoex.h>

u32 GlobalDebugLevel = _drv_err_;

#ifdef DEBUG_RTL871X

	u64 GlobalDebugComponents = \
			_module_rtl871x_xmit_c_ |
			_module_xmit_osdep_c_ |
			_module_rtl871x_recv_c_ |
			_module_recv_osdep_c_ |
			_module_rtl871x_mlme_c_ |
			_module_mlme_osdep_c_ |
			_module_rtl871x_sta_mgt_c_ |
			_module_rtl871x_cmd_c_ |
			_module_cmd_osdep_c_ |
			_module_rtl871x_io_c_ |
			_module_io_osdep_c_ |
			_module_os_intfs_c_|
			_module_rtl871x_security_c_|
			_module_rtl871x_eeprom_c_|
			_module_hal_init_c_|
			_module_hci_hal_init_c_|
			_module_rtl871x_ioctl_c_|
			_module_rtl871x_ioctl_set_c_|
			_module_rtl871x_ioctl_query_c_|
			_module_rtl871x_pwrctrl_c_|
			_module_hci_intfs_c_|
			_module_hci_ops_c_|
			_module_hci_ops_os_c_|
			_module_rtl871x_ioctl_os_c|
			_module_rtl8712_cmd_c_|
			_module_hal_xmit_c_|
			_module_rtl8712_recv_c_ |
			_module_mp_ |
			_module_efuse_;

#endif /* DEBUG_RTL871X */

#include <rtw_version.h>

void dump_drv_version(void *sel)
{
	DBG_871X_SEL_NL(sel, "%s %s\n", "rtl8723bs", DRIVERVERSION);
}

void dump_log_level(void *sel)
{
	DBG_871X_SEL_NL(sel, "log_level:%d\n", GlobalDebugLevel);
}

void sd_f0_reg_dump(void *sel, struct adapter *adapter)
{
	int i;

	for (i = 0x0; i <= 0xff; i++) {
		if (i%16 == 0)
			DBG_871X_SEL_NL(sel, "0x%02x ", i);

		DBG_871X_SEL(sel, "%02x ", rtw_sd_f0_read8(adapter, i));

		if (i%16 == 15)
			DBG_871X_SEL(sel, "\n");
		else if (i%8 == 7)
			DBG_871X_SEL(sel, "\t");
	}
}

void mac_reg_dump(void *sel, struct adapter *adapter)
{
	int i, j = 1;

	DBG_871X_SEL_NL(sel, "======= MAC REG =======\n");

	for (i = 0x0; i < 0x800; i += 4) {
		if (j%4 == 1)
			DBG_871X_SEL_NL(sel, "0x%03x", i);
		DBG_871X_SEL(sel, " 0x%08x ", rtw_read32(adapter, i));
		if ((j++)%4 == 0)
			DBG_871X_SEL(sel, "\n");
	}
}

void bb_reg_dump(void *sel, struct adapter *adapter)
{
	int i, j = 1;

	DBG_871X_SEL_NL(sel, "======= BB REG =======\n");
	for (i = 0x800; i < 0x1000 ; i += 4) {
		if (j%4 == 1)
			DBG_871X_SEL_NL(sel, "0x%03x", i);
		DBG_871X_SEL(sel, " 0x%08x ", rtw_read32(adapter, i));
		if ((j++)%4 == 0)
			DBG_871X_SEL(sel, "\n");
	}
}

void rf_reg_dump(void *sel, struct adapter *adapter)
{
	int i, j = 1, path;
	u32 value;
	u8 rf_type = 0;
	u8 path_nums = 0;

	rtw_hal_get_hwreg(adapter, HW_VAR_RF_TYPE, (u8 *)(&rf_type));
	if ((RF_1T2R == rf_type) || (RF_1T1R == rf_type))
		path_nums = 1;
	else
		path_nums = 2;

	DBG_871X_SEL_NL(sel, "======= RF REG =======\n");

	for (path = 0; path < path_nums; path++) {
		DBG_871X_SEL_NL(sel, "RF_Path(%x)\n", path);
		for (i = 0; i < 0x100; i++) {
			value = rtw_hal_read_rfreg(adapter, path, i, 0xffffffff);
			if (j%4 == 1)
				DBG_871X_SEL_NL(sel, "0x%02x ", i);
			DBG_871X_SEL(sel, " 0x%08x ", value);
			if ((j++)%4 == 0)
				DBG_871X_SEL(sel, "\n");
		}
	}
}
