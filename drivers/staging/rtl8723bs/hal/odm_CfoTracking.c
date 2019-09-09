// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "odm_precomp.h"

static void odm_SetCrystalCap(void *pDM_VOID, u8 CrystalCap)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING pCfoTrack = &pDM_Odm->DM_CfoTrack;
	bool bEEPROMCheck;
	struct adapter *Adapter = pDM_Odm->Adapter;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	bEEPROMCheck = pHalData->EEPROMVersion >= 0x01;

	if (pCfoTrack->CrystalCap == CrystalCap)
		return;

	pCfoTrack->CrystalCap = CrystalCap;

	/*  0x2C[23:18] = 0x2C[17:12] = CrystalCap */
	CrystalCap = CrystalCap & 0x3F;
	PHY_SetBBReg(
		pDM_Odm->Adapter,
		REG_MAC_PHY_CTRL,
		0x00FFF000,
		(CrystalCap | (CrystalCap << 6))
	);

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_CFO_TRACKING,
		ODM_DBG_LOUD,
		(
			"odm_SetCrystalCap(): CrystalCap = 0x%x\n",
			CrystalCap
		)
	);
}

static u8 odm_GetDefaultCrytaltalCap(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u8 CrystalCap = 0x20;

	struct adapter *Adapter = pDM_Odm->Adapter;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	CrystalCap = pHalData->CrystalCap;

	CrystalCap = CrystalCap & 0x3f;

	return CrystalCap;
}

static void odm_SetATCStatus(void *pDM_VOID, bool ATCStatus)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING pCfoTrack = &pDM_Odm->DM_CfoTrack;

	if (pCfoTrack->bATCStatus == ATCStatus)
		return;

	PHY_SetBBReg(
		pDM_Odm->Adapter,
		ODM_REG(BB_ATC, pDM_Odm),
		ODM_BIT(BB_ATC, pDM_Odm),
		ATCStatus
	);
	pCfoTrack->bATCStatus = ATCStatus;
}

static bool odm_GetATCStatus(void *pDM_VOID)
{
	bool ATCStatus;
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ATCStatus = (bool)PHY_QueryBBReg(
		pDM_Odm->Adapter,
		ODM_REG(BB_ATC, pDM_Odm),
		ODM_BIT(BB_ATC, pDM_Odm)
	);
	return ATCStatus;
}

void ODM_CfoTrackingReset(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING pCfoTrack = &pDM_Odm->DM_CfoTrack;

	pCfoTrack->DefXCap = odm_GetDefaultCrytaltalCap(pDM_Odm);
	pCfoTrack->bAdjust = true;

	odm_SetCrystalCap(pDM_Odm, pCfoTrack->DefXCap);
	odm_SetATCStatus(pDM_Odm, true);
}

void ODM_CfoTrackingInit(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING pCfoTrack = &pDM_Odm->DM_CfoTrack;

	pCfoTrack->DefXCap =
		pCfoTrack->CrystalCap = odm_GetDefaultCrytaltalCap(pDM_Odm);
	pCfoTrack->bATCStatus = odm_GetATCStatus(pDM_Odm);
	pCfoTrack->bAdjust = true;
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_CFO_TRACKING,
		ODM_DBG_LOUD,
		("ODM_CfoTracking_init() =========>\n")
	);
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_CFO_TRACKING,
		ODM_DBG_LOUD,
		(
			"ODM_CfoTracking_init(): bATCStatus = %d, CrystalCap = 0x%x\n",
			pCfoTrack->bATCStatus,
			pCfoTrack->DefXCap
		)
	);
}

void ODM_CfoTracking(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PCFO_TRACKING pCfoTrack = &pDM_Odm->DM_CfoTrack;
	int CFO_kHz_A, CFO_kHz_B, CFO_ave = 0;
	int CFO_ave_diff;
	int CrystalCap = (int)pCfoTrack->CrystalCap;
	u8 Adjust_Xtal = 1;

	/* 4 Support ability */
	if (!(pDM_Odm->SupportAbility & ODM_BB_CFO_TRACKING)) {
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_CFO_TRACKING,
			ODM_DBG_LOUD,
			("ODM_CfoTracking(): Return: SupportAbility ODM_BB_CFO_TRACKING is disabled\n")
		);
		return;
	}

	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_CFO_TRACKING,
		ODM_DBG_LOUD,
		("ODM_CfoTracking() =========>\n")
	);

	if (!pDM_Odm->bLinked || !pDM_Odm->bOneEntryOnly) {
		/* 4 No link or more than one entry */
		ODM_CfoTrackingReset(pDM_Odm);
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_CFO_TRACKING,
			ODM_DBG_LOUD,
			(
				"ODM_CfoTracking(): Reset: bLinked = %d, bOneEntryOnly = %d\n",
				pDM_Odm->bLinked,
				pDM_Odm->bOneEntryOnly
			)
		);
	} else {
		/* 3 1. CFO Tracking */
		/* 4 1.1 No new packet */
		if (pCfoTrack->packetCount == pCfoTrack->packetCount_pre) {
			ODM_RT_TRACE(
				pDM_Odm,
				ODM_COMP_CFO_TRACKING,
				ODM_DBG_LOUD,
				(
					"ODM_CfoTracking(): packet counter doesn't change\n"
				)
			);
			return;
		}
		pCfoTrack->packetCount_pre = pCfoTrack->packetCount;

		/* 4 1.2 Calculate CFO */
		CFO_kHz_A =  (int)(pCfoTrack->CFO_tail[0] * 3125)  / 1280;
		CFO_kHz_B =  (int)(pCfoTrack->CFO_tail[1] * 3125)  / 1280;

		if (pDM_Odm->RFType < ODM_2T2R)
			CFO_ave = CFO_kHz_A;
		else
			CFO_ave = (int)(CFO_kHz_A + CFO_kHz_B) >> 1;
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_CFO_TRACKING,
			ODM_DBG_LOUD,
			(
				"ODM_CfoTracking(): CFO_kHz_A = %dkHz, CFO_kHz_B = %dkHz, CFO_ave = %dkHz\n",
				CFO_kHz_A,
				CFO_kHz_B,
				CFO_ave
			)
		);

		/* 4 1.3 Avoid abnormal large CFO */
		CFO_ave_diff =
			(pCfoTrack->CFO_ave_pre >= CFO_ave) ?
			(pCfoTrack->CFO_ave_pre-CFO_ave) :
			(CFO_ave-pCfoTrack->CFO_ave_pre);

		if (
			CFO_ave_diff > 20 &&
			pCfoTrack->largeCFOHit == 0 &&
			!pCfoTrack->bAdjust
		) {
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_CFO_TRACKING, ODM_DBG_LOUD, ("ODM_CfoTracking(): first large CFO hit\n"));
			pCfoTrack->largeCFOHit = 1;
			return;
		} else
			pCfoTrack->largeCFOHit = 0;
		pCfoTrack->CFO_ave_pre = CFO_ave;

		/* 4 1.4 Dynamic Xtal threshold */
		if (pCfoTrack->bAdjust == false) {
			if (CFO_ave > CFO_TH_XTAL_HIGH || CFO_ave < (-CFO_TH_XTAL_HIGH))
				pCfoTrack->bAdjust = true;
		} else {
			if (CFO_ave < CFO_TH_XTAL_LOW && CFO_ave > (-CFO_TH_XTAL_LOW))
				pCfoTrack->bAdjust = false;
		}

		/* 4 1.5 BT case: Disable CFO tracking */
		if (pDM_Odm->bBtEnabled) {
			pCfoTrack->bAdjust = false;
			odm_SetCrystalCap(pDM_Odm, pCfoTrack->DefXCap);
			ODM_RT_TRACE(
				pDM_Odm,
				ODM_COMP_CFO_TRACKING,
				ODM_DBG_LOUD,
				("ODM_CfoTracking(): Disable CFO tracking for BT!!\n")
			);
		}

		/* 4 1.6 Big jump */
		if (pCfoTrack->bAdjust) {
			if (CFO_ave > CFO_TH_XTAL_LOW)
				Adjust_Xtal = Adjust_Xtal+((CFO_ave-CFO_TH_XTAL_LOW)>>2);
			else if (CFO_ave < (-CFO_TH_XTAL_LOW))
				Adjust_Xtal = Adjust_Xtal+((CFO_TH_XTAL_LOW-CFO_ave)>>2);

			ODM_RT_TRACE(
				pDM_Odm,
				ODM_COMP_CFO_TRACKING,
				ODM_DBG_LOUD,
				(
					"ODM_CfoTracking(): Crystal cap offset = %d\n",
					Adjust_Xtal
				)
			);
		}

		/* 4 1.7 Adjust Crystal Cap. */
		if (pCfoTrack->bAdjust) {
			if (CFO_ave > CFO_TH_XTAL_LOW)
				CrystalCap = CrystalCap + Adjust_Xtal;
			else if (CFO_ave < (-CFO_TH_XTAL_LOW))
				CrystalCap = CrystalCap - Adjust_Xtal;

			if (CrystalCap > 0x3f)
				CrystalCap = 0x3f;
			else if (CrystalCap < 0)
				CrystalCap = 0;

			odm_SetCrystalCap(pDM_Odm, (u8)CrystalCap);
		}
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_CFO_TRACKING,
			ODM_DBG_LOUD,
			(
				"ODM_CfoTracking(): Crystal cap = 0x%x, Default Crystal cap = 0x%x\n",
				pCfoTrack->CrystalCap,
				pCfoTrack->DefXCap
			)
		);

		/* 3 2. Dynamic ATC switch */
		if (CFO_ave < CFO_TH_ATC && CFO_ave > -CFO_TH_ATC) {
			odm_SetATCStatus(pDM_Odm, false);
			ODM_RT_TRACE(
				pDM_Odm,
				ODM_COMP_CFO_TRACKING,
				ODM_DBG_LOUD,
				("ODM_CfoTracking(): Disable ATC!!\n")
			);
		} else {
			odm_SetATCStatus(pDM_Odm, true);
			ODM_RT_TRACE(
				pDM_Odm,
				ODM_COMP_CFO_TRACKING,
				ODM_DBG_LOUD,
				("ODM_CfoTracking(): Enable ATC!!\n")
			);
		}
	}
}

void ODM_ParsingCFO(void *pDM_VOID, void *pPktinfo_VOID, s8 *pcfotail)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	struct odm_packet_info *pPktinfo = (struct odm_packet_info *)pPktinfo_VOID;
	PCFO_TRACKING pCfoTrack = &pDM_Odm->DM_CfoTrack;
	u8 i;

	if (!(pDM_Odm->SupportAbility & ODM_BB_CFO_TRACKING))
		return;

	if (pPktinfo->station_id != 0) {
		/* 3 Update CFO report for path-A & path-B */
		/*  Only paht-A and path-B have CFO tail and short CFO */
		for (i = ODM_RF_PATH_A; i <= ODM_RF_PATH_B; i++)
			pCfoTrack->CFO_tail[i] = (int)pcfotail[i];

		/* 3 Update packet counter */
		if (pCfoTrack->packetCount == 0xffffffff)
			pCfoTrack->packetCount = 0;
		else
			pCfoTrack->packetCount++;
	}
}
