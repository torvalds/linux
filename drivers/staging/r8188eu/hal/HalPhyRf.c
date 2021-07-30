// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

 #include "odm_precomp.h"

/* 3============================================================ */
/* 3 IQ Calibration */
/* 3============================================================ */

#define ODM_TARGET_CHNL_NUM_2G_5G	59

u8 ODM_GetRightChnlPlaceforIQK(u8 chnl)
{
	u8	channel_all[ODM_TARGET_CHNL_NUM_2G_5G] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
		36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, 64,
		100, 102, 104, 106, 108, 110, 112, 114, 116, 118, 120, 122,
		124, 126, 128, 130, 132, 134, 136, 138, 140, 149, 151, 153,
		155, 157, 159, 161, 163, 165
	};
	u8	place = chnl;

	if (chnl > 14) {
		for (place = 14; place < sizeof(channel_all); place++) {
			if (channel_all[place] == chnl)
				return place-13;
		}
	}
	return 0;
}
