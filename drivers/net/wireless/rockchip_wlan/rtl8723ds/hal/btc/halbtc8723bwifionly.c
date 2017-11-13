/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#include "mp_precomp.h"


VOID
ex_hal8723b_wifi_only_hw_config(
	IN struct wifi_only_cfg *pwifionlycfg
	)
{
	struct wifi_only_haldata *pwifionly_haldata = &pwifionlycfg->haldata_info;


	halwifionly_write1byte(pwifionlycfg, 0x778, 0x3); /* Set pta for wifi first priority, 0x1 need to reference pta table to determine wifi and bt priority */
	halwifionly_bitmaskwrite1byte(pwifionlycfg, 0x40, 0x20, 0x1);

	/* Set Antenna path to Wifi */
	halwifionly_write2byte(pwifionlycfg, 0x0765, 0x8); /* Set pta for wifi first priority, 0x0 need to reference pta table to determine wifi and bt priority */
	halwifionly_write2byte(pwifionlycfg, 0x076e, 0xc);

	halwifionly_write4byte(pwifionlycfg, 0x000006c0, 0xaaaaaaaa); /* pta table, 0xaaaaaaaa means wifi is higher priority than bt */
	halwifionly_write4byte(pwifionlycfg, 0x000006c4, 0xaaaaaaaa);

	halwifionly_bitmaskwrite1byte(pwifionlycfg, 0x67, 0x20, 0x1); /* BT select s0/s1 is controlled by WiFi */

	/* 0x948 setting */
	if (pwifionlycfg->chip_interface == WIFIONLY_INTF_PCI) {
		/* HP Foxconn NGFF at S0
		 not sure HP pg correct or not(EEPROMBluetoothSingleAntPath), so here we just write
		 0x948=0x280 for HP HW id NIC. */
		if (pwifionly_haldata->customer_id == CUSTOMER_HP_1) {
			halwifionly_write4byte(pwifionlycfg, 0x948, 0x280);
			halwifionly_phy_set_rf_reg(pwifionlycfg, 0, 0x1, 0xfffff, 0x0); /* WiFi TRx Mask off */
			return;
		}
	}

	if (pwifionly_haldata->efuse_pg_antnum == 2) {
		halwifionly_write4byte(pwifionlycfg, 0x948, 0x0);
	} else {
	/* 3Attention !!! For 8723BU  !!!!
	 For 8723BU single ant case: jira [USB-1237]
		   Because of 8723BU S1 has HW problem, we only can use S0 instead.
		   Whether Efuse 0xc3 [6] is 0 or 1, we should always use S0 and write 0x948 to 80/280

	 --------------------------------------------------
	 BT Team :
		  When in Single Ant case, Reg[0x948] has two case : 0x80 or 0x200
		 When in Two Ant case, Reg[0x948] has two case : 0x280 or 0x0
		  Efuse 0xc3 [6] Antenna Path
		  0xc3 [6] = 0	 ==>  S1	 ==>   0x948 = 0/40/200
		  0xc3 [6] = 1	 ==>  S0	 ==>   0x948 = 80/240/280 */

		if (pwifionlycfg->chip_interface == WIFIONLY_INTF_USB)
			halwifionly_write4byte(pwifionlycfg, 0x948, 0x80);
		else {
			if (pwifionly_haldata->efuse_pg_antpath == 0)
				halwifionly_write4byte(pwifionlycfg, 0x948, 0x0);
			else
				halwifionly_write4byte(pwifionlycfg, 0x948, 0x280);
		}

	}


	/* after 8723B F-cut, TRx Mask should be set when 0x948=0x0 or 0x280
	PHY_SetRFReg(Adapter, 0, 0x1, 0xfffff, 0x780); WiFi TRx Mask on */
	halwifionly_phy_set_rf_reg(pwifionlycfg, 0, 0x1, 0xfffff, 0x0); /*WiFi TRx Mask off */

}
