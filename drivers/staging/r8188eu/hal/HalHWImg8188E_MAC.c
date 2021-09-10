// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "../include/odm_precomp.h"
#include "../include/rtw_iol.h"

static bool Checkcondition(const u32  condition, const u32  hex)
{
	u32 _board     = (hex & 0x000000FF);
	u32 _interface = (hex & 0x0000FF00) >> 8;
	u32 _platform  = (hex & 0x00FF0000) >> 16;
	u32 cond = condition;

	if (condition == 0xCDCDCDCD)
		return true;

	cond = condition & 0x000000FF;
	if ((_board == cond) && cond != 0x00)
		return false;

	cond = condition & 0x0000FF00;
	cond = cond >> 8;
	if ((_interface & cond) == 0 && cond != 0x07)
		return false;

	cond = condition & 0x00FF0000;
	cond = cond >> 16;
	if ((_platform & cond) == 0 && cond != 0x0F)
		return false;
	return true;
}

/******************************************************************************
*                           MAC_REG.TXT
******************************************************************************/

static u32 array_MAC_REG_8188E[] = {
		0x026, 0x00000041,
		0x027, 0x00000035,
		0x428, 0x0000000A,
		0x429, 0x00000010,
		0x430, 0x00000000,
		0x431, 0x00000001,
		0x432, 0x00000002,
		0x433, 0x00000004,
		0x434, 0x00000005,
		0x435, 0x00000006,
		0x436, 0x00000007,
		0x437, 0x00000008,
		0x438, 0x00000000,
		0x439, 0x00000000,
		0x43A, 0x00000001,
		0x43B, 0x00000002,
		0x43C, 0x00000004,
		0x43D, 0x00000005,
		0x43E, 0x00000006,
		0x43F, 0x00000007,
		0x440, 0x0000005D,
		0x441, 0x00000001,
		0x442, 0x00000000,
		0x444, 0x00000015,
		0x445, 0x000000F0,
		0x446, 0x0000000F,
		0x447, 0x00000000,
		0x458, 0x00000041,
		0x459, 0x000000A8,
		0x45A, 0x00000072,
		0x45B, 0x000000B9,
		0x460, 0x00000066,
		0x461, 0x00000066,
		0x480, 0x00000008,
		0x4C8, 0x000000FF,
		0x4C9, 0x00000008,
		0x4CC, 0x000000FF,
		0x4CD, 0x000000FF,
		0x4CE, 0x00000001,
		0x4D3, 0x00000001,
		0x500, 0x00000026,
		0x501, 0x000000A2,
		0x502, 0x0000002F,
		0x503, 0x00000000,
		0x504, 0x00000028,
		0x505, 0x000000A3,
		0x506, 0x0000005E,
		0x507, 0x00000000,
		0x508, 0x0000002B,
		0x509, 0x000000A4,
		0x50A, 0x0000005E,
		0x50B, 0x00000000,
		0x50C, 0x0000004F,
		0x50D, 0x000000A4,
		0x50E, 0x00000000,
		0x50F, 0x00000000,
		0x512, 0x0000001C,
		0x514, 0x0000000A,
		0x516, 0x0000000A,
		0x525, 0x0000004F,
		0x550, 0x00000010,
		0x551, 0x00000010,
		0x559, 0x00000002,
		0x55D, 0x000000FF,
		0x605, 0x00000030,
		0x608, 0x0000000E,
		0x609, 0x0000002A,
		0x620, 0x000000FF,
		0x621, 0x000000FF,
		0x622, 0x000000FF,
		0x623, 0x000000FF,
		0x624, 0x000000FF,
		0x625, 0x000000FF,
		0x626, 0x000000FF,
		0x627, 0x000000FF,
		0x652, 0x00000020,
		0x63C, 0x0000000A,
		0x63D, 0x0000000A,
		0x63E, 0x0000000E,
		0x63F, 0x0000000E,
		0x640, 0x00000040,
		0x66E, 0x00000005,
		0x700, 0x00000021,
		0x701, 0x00000043,
		0x702, 0x00000065,
		0x703, 0x00000087,
		0x708, 0x00000021,
		0x709, 0x00000043,
		0x70A, 0x00000065,
		0x70B, 0x00000087,
};

enum HAL_STATUS ODM_ReadAndConfig_MAC_REG_8188E(struct odm_dm_struct *dm_odm)
{
	#define READ_NEXT_PAIR(v1, v2, i) do { i += 2; v1 = array[i]; v2 = array[i + 1]; } while (0)

	u32     hex         = 0;
	u32     i;
	u8     platform    = dm_odm->SupportPlatform;
	u8     interface_val   = dm_odm->SupportInterface;
	u8     board       = dm_odm->BoardType;
	u32     array_len    = sizeof(array_MAC_REG_8188E) / sizeof(u32);
	u32    *array       = array_MAC_REG_8188E;
	bool	biol = false;

	struct adapter *adapt =  dm_odm->Adapter;
	struct xmit_frame	*pxmit_frame = NULL;
	u8 bndy_cnt = 1;
	enum HAL_STATUS rst = HAL_STATUS_SUCCESS;
	hex += board;
	hex += interface_val << 8;
	hex += platform << 16;
	hex += 0xFF000000;

	biol = rtw_IOL_applied(adapt);

	if (biol) {
		pxmit_frame = rtw_IOL_accquire_xmit_frame(adapt);
		if (!pxmit_frame) {
			pr_info("rtw_IOL_accquire_xmit_frame failed\n");
			return HAL_STATUS_FAILURE;
		}
	}

	for (i = 0; i < array_len; i += 2) {
		u32 v1 = array[i];
		u32 v2 = array[i + 1];

		/*  This (offset, data) pair meets the condition. */
		if (v1 < 0xCDCDCDCD) {
				if (biol) {
					if (rtw_IOL_cmd_boundary_handle(pxmit_frame))
						bndy_cnt++;
					rtw_IOL_append_WB_cmd(pxmit_frame, (u16)v1, (u8)v2, 0xFF);
				} else {
					odm_ConfigMAC_8188E(dm_odm, v1, (u8)v2);
				}
				continue;
		} else { /*  This line is the start line of branch. */
			if (!Checkcondition(array[i], hex)) {
				/*  Discard the following (offset, data) pairs. */
				READ_NEXT_PAIR(v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < array_len - 2) {
					READ_NEXT_PAIR(v1, v2, i);
				}
				i -= 2; /*  prevent from for-loop += 2 */
			} else { /*  Configure matched pairs and skip to end of if-else. */
				READ_NEXT_PAIR(v1, v2, i);
				while (v2 != 0xDEAD &&
				       v2 != 0xCDEF &&
				       v2 != 0xCDCD && i < array_len - 2) {
					if (biol) {
						if (rtw_IOL_cmd_boundary_handle(pxmit_frame))
							bndy_cnt++;
						rtw_IOL_append_WB_cmd(pxmit_frame, (u16)v1, (u8)v2, 0xFF);
					} else {
						odm_ConfigMAC_8188E(dm_odm, v1, (u8)v2);
					}

					READ_NEXT_PAIR(v1, v2, i);
				}
				while (v2 != 0xDEAD && i < array_len - 2)
					READ_NEXT_PAIR(v1, v2, i);
			}
		}
	}
	if (biol) {
		if (!rtl8188e_IOL_exec_cmds_sync(dm_odm->Adapter, pxmit_frame, 1000, bndy_cnt)) {
			pr_info("~~~ MAC IOL_exec_cmds Failed !!!\n");
			rst = HAL_STATUS_FAILURE;
		}
	}
	return rst;
}
