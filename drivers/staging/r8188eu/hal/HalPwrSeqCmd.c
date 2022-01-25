// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

/*++

Module Name:
	HalPwrSeqCmd.c

Abstract:
	Implement HW Power sequence configuration CMD handling routine for Realtek devices.

Major Change History:
	When       Who               What
	---------- ---------------   -------------------------------
	2011-10-26 Lucas            Modify to be compatible with SD4-CE driver.
	2011-07-07 Roger            Create.

--*/

#include "../include/HalPwrSeqCmd.h"

/*	Description: */
/*		This routine deals with the Power Configuration CMDs parsing
 *		for RTL8723/RTL8188E Series IC.
 *	Assumption:
 *		We should follow specific format which was released from HW SD.
 */
u8 HalPwrSeqCmdParsing(struct adapter *padapter, struct wl_pwr_cfg pwrseqcmd[])
{
	struct wl_pwr_cfg pwrcfgcmd = {0};
	u8 poll_bit = false;
	u32 aryidx = 0;
	u8 value = 0;
	u32 offset = 0;
	u32 poll_count = 0; /*  polling autoload done. */
	u32 max_poll_count = 5000;

	do {
		pwrcfgcmd = pwrseqcmd[aryidx];

		switch (GET_PWR_CFG_CMD(pwrcfgcmd)) {
		case PWR_CMD_WRITE:
			offset = GET_PWR_CFG_OFFSET(pwrcfgcmd);

			/*  Read the value from system register */
			value = rtw_read8(padapter, offset);

			value &= ~(GET_PWR_CFG_MASK(pwrcfgcmd));
			value |= (GET_PWR_CFG_VALUE(pwrcfgcmd) & GET_PWR_CFG_MASK(pwrcfgcmd));

			/*  Write the value back to system register */
			rtw_write8(padapter, offset, value);
			break;
		case PWR_CMD_POLLING:
			poll_bit = false;
			offset = GET_PWR_CFG_OFFSET(pwrcfgcmd);
			do {
				value = rtw_read8(padapter, offset);

				value &= GET_PWR_CFG_MASK(pwrcfgcmd);
				if (value == (GET_PWR_CFG_VALUE(pwrcfgcmd) & GET_PWR_CFG_MASK(pwrcfgcmd)))
					poll_bit = true;
				else
					udelay(10);

				if (poll_count++ > max_poll_count) {
					DBG_88E("Fail to polling Offset[%#x]\n", offset);
					return false;
				}
			} while (!poll_bit);
			break;
		case PWR_CMD_DELAY:
			if (GET_PWR_CFG_VALUE(pwrcfgcmd) == PWRSEQ_DELAY_US)
				udelay(GET_PWR_CFG_OFFSET(pwrcfgcmd));
			else
				udelay(GET_PWR_CFG_OFFSET(pwrcfgcmd) * 1000);
			break;
		case PWR_CMD_END:
			/*  When this command is parsed, end the process */
			return true;
			break;
		default:
			break;
		}

		aryidx++;/* Add Array Index */
	} while (1);
	return true;
}
