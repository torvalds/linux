// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "odm_precomp.h"

/*  This function is for inband noise test utility only */
/*  To obtain the inband noise level(dbm), do the following. */
/*  1. disable DIG and Power Saving */
/*  2. Set initial gain = 0x1a */
/*  3. Stop updating idle time pwer report (for driver read) */
/* - 0x80c[25] */

#define Valid_Min				-35
#define Valid_Max			10
#define ValidCnt				5

static s16 odm_InbandNoise_Monitor_NSeries(
	struct dm_odm_t *pDM_Odm,
	u8 bPauseDIG,
	u8 IGIValue,
	u32 max_time
)
{
	u32 tmp4b;
	u8 max_rf_path = 0, rf_path;
	u8 reg_c50, reg_c58, valid_done = 0;
	struct noise_level noise_data;
	u32 start  = 0;

	pDM_Odm->noise_level.noise_all = 0;

	max_rf_path = 1;

	memset(&noise_data, 0, sizeof(struct noise_level));

	/*  */
	/*  Step 1. Disable DIG && Set initial gain. */
	/*  */

	if (bPauseDIG)
		odm_PauseDIG(pDM_Odm, ODM_PAUSE_DIG, IGIValue);
	/*  */
	/*  Step 2. Disable all power save for read registers */
	/*  */
	/* dcmd_DebugControlPowerSave(padapter, PSDisable); */

	/*  */
	/*  Step 3. Get noise power level */
	/*  */
	start = jiffies;
	while (1) {

		/* Stop updating idle time pwer report (for driver read) */
		PHY_SetBBReg(pDM_Odm->Adapter, rFPGA0_TxGainStage, BIT25, 1);

		/* Read Noise Floor Report */
		tmp4b = PHY_QueryBBReg(pDM_Odm->Adapter, 0x8f8, bMaskDWord);

		/* PHY_SetBBReg(pDM_Odm->Adapter, rOFDM0_XAAGCCore1, bMaskByte0, TestInitialGain); */
		/* if (max_rf_path == 2) */
		/* PHY_SetBBReg(pDM_Odm->Adapter, rOFDM0_XBAGCCore1, bMaskByte0, TestInitialGain); */

		/* update idle time pwer report per 5us */
		PHY_SetBBReg(pDM_Odm->Adapter, rFPGA0_TxGainStage, BIT25, 0);

		noise_data.value[RF_PATH_A] = (u8)(tmp4b&0xff);
		noise_data.value[RF_PATH_B]  = (u8)((tmp4b&0xff00)>>8);

		for (rf_path = RF_PATH_A; rf_path < max_rf_path; rf_path++) {
			noise_data.sval[rf_path] = (s8)noise_data.value[rf_path];
			noise_data.sval[rf_path] /= 2;
		}
		/* mdelay(10); */
		/* msleep(10); */

		for (rf_path = RF_PATH_A; rf_path < max_rf_path; rf_path++) {
			if ((noise_data.valid_cnt[rf_path] < ValidCnt) && (noise_data.sval[rf_path] < Valid_Max && noise_data.sval[rf_path] >= Valid_Min)) {
				noise_data.valid_cnt[rf_path]++;
				noise_data.sum[rf_path] += noise_data.sval[rf_path];
				if (noise_data.valid_cnt[rf_path] == ValidCnt) {
					valid_done++;
				}

			}

		}

		/* printk("####### valid_done:%d #############\n", valid_done); */
		if ((valid_done == max_rf_path) || (jiffies_to_msecs(jiffies - start) > max_time)) {
			for (rf_path = RF_PATH_A; rf_path < max_rf_path; rf_path++) {
				/* printk("%s PATH_%d - sum = %d, valid_cnt = %d\n", __func__, rf_path, noise_data.sum[rf_path], noise_data.valid_cnt[rf_path]); */
				if (noise_data.valid_cnt[rf_path])
					noise_data.sum[rf_path] /= noise_data.valid_cnt[rf_path];
				else
					noise_data.sum[rf_path]  = 0;
			}
			break;
		}
	}
	reg_c50 = (s32)PHY_QueryBBReg(pDM_Odm->Adapter, rOFDM0_XAAGCCore1, bMaskByte0);
	reg_c50 &= ~BIT7;
	pDM_Odm->noise_level.noise[RF_PATH_A] = -110 + reg_c50 + noise_data.sum[RF_PATH_A];
	pDM_Odm->noise_level.noise_all += pDM_Odm->noise_level.noise[RF_PATH_A];

	if (max_rf_path == 2) {
		reg_c58 = (s32)PHY_QueryBBReg(pDM_Odm->Adapter, rOFDM0_XBAGCCore1, bMaskByte0);
		reg_c58 &= ~BIT7;
		pDM_Odm->noise_level.noise[RF_PATH_B] = -110 + reg_c58 + noise_data.sum[RF_PATH_B];
		pDM_Odm->noise_level.noise_all += pDM_Odm->noise_level.noise[RF_PATH_B];
	}
	pDM_Odm->noise_level.noise_all /= max_rf_path;

	/*  */
	/*  Step 4. Recover the Dig */
	/*  */
	if (bPauseDIG)
		odm_PauseDIG(pDM_Odm, ODM_RESUME_DIG, IGIValue);

	return pDM_Odm->noise_level.noise_all;

}

s16 ODM_InbandNoise_Monitor(void *pDM_VOID, u8 bPauseDIG, u8 IGIValue, u32 max_time)
{
	return odm_InbandNoise_Monitor_NSeries(pDM_VOID, bPauseDIG, IGIValue, max_time);
}
