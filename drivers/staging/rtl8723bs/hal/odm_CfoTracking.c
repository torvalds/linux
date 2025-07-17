// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "odm_precomp.h"

static void odm_SetCrystalCap(void *pDM_VOID, u8 CrystalCap)
{
	struct dm_odm_t *pDM_Odm = (struct dm_odm_t *)pDM_VOID;
	struct cfo_tracking *pCfoTrack = &pDM_Odm->DM_CfoTrack;

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
}

static u8 odm_GetDefaultCrytaltalCap(void *pDM_VOID)
{
	struct dm_odm_t *pDM_Odm = (struct dm_odm_t *)pDM_VOID;

	struct adapter *Adapter = pDM_Odm->Adapter;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	return pHalData->CrystalCap & 0x3f;
}

static void odm_SetATCStatus(void *pDM_VOID, bool ATCStatus)
{
	struct dm_odm_t *pDM_Odm = (struct dm_odm_t *)pDM_VOID;
	struct cfo_tracking *pCfoTrack = &pDM_Odm->DM_CfoTrack;

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
	struct dm_odm_t *pDM_Odm = (struct dm_odm_t *)pDM_VOID;

	ATCStatus = (bool)PHY_QueryBBReg(
		pDM_Odm->Adapter,
		ODM_REG(BB_ATC, pDM_Odm),
		ODM_BIT(BB_ATC, pDM_Odm)
	);
	return ATCStatus;
}

void ODM_CfoTrackingReset(void *pDM_VOID)
{
	struct dm_odm_t *pDM_Odm = (struct dm_odm_t *)pDM_VOID;
	struct cfo_tracking *pCfoTrack = &pDM_Odm->DM_CfoTrack;

	pCfoTrack->DefXCap = odm_GetDefaultCrytaltalCap(pDM_Odm);
	pCfoTrack->bAdjust = true;

	odm_SetCrystalCap(pDM_Odm, pCfoTrack->DefXCap);
	odm_SetATCStatus(pDM_Odm, true);
}

void ODM_CfoTrackingInit(void *pDM_VOID)
{
	struct dm_odm_t *pDM_Odm = (struct dm_odm_t *)pDM_VOID;
	struct cfo_tracking *pCfoTrack = &pDM_Odm->DM_CfoTrack;

	pCfoTrack->DefXCap =
		pCfoTrack->CrystalCap = odm_GetDefaultCrytaltalCap(pDM_Odm);
	pCfoTrack->bATCStatus = odm_GetATCStatus(pDM_Odm);
	pCfoTrack->bAdjust = true;
}

void ODM_CfoTracking(void *pDM_VOID)
{
	struct dm_odm_t *pDM_Odm = (struct dm_odm_t *)pDM_VOID;
	struct cfo_tracking *pCfoTrack = &pDM_Odm->DM_CfoTrack;
	int CFO_kHz_A, CFO_ave = 0;
	int CFO_ave_diff;
	int CrystalCap = (int)pCfoTrack->CrystalCap;
	u8 Adjust_Xtal = 1;

	/* 4 Support ability */
	if (!(pDM_Odm->SupportAbility & ODM_BB_CFO_TRACKING)) {
		return;
	}

	if (!pDM_Odm->bLinked || !pDM_Odm->bOneEntryOnly) {
		/* 4 No link or more than one entry */
		ODM_CfoTrackingReset(pDM_Odm);
	} else {
		/* 3 1. CFO Tracking */
		/* 4 1.1 No new packet */
		if (pCfoTrack->packetCount == pCfoTrack->packetCount_pre) {
			return;
		}
		pCfoTrack->packetCount_pre = pCfoTrack->packetCount;

		/* 4 1.2 Calculate CFO */
		CFO_kHz_A =  (int)(pCfoTrack->CFO_tail[0] * 3125)  / 1280;

		CFO_ave = CFO_kHz_A;

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
		}

		/* 4 1.6 Big jump */
		if (pCfoTrack->bAdjust) {
			if (CFO_ave > CFO_TH_XTAL_LOW)
				Adjust_Xtal = Adjust_Xtal + ((CFO_ave - CFO_TH_XTAL_LOW) >> 2);
			else if (CFO_ave < (-CFO_TH_XTAL_LOW))
				Adjust_Xtal = Adjust_Xtal + ((CFO_TH_XTAL_LOW - CFO_ave) >> 2);
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

		/* 3 2. Dynamic ATC switch */
		if (CFO_ave < CFO_TH_ATC && CFO_ave > -CFO_TH_ATC) {
			odm_SetATCStatus(pDM_Odm, false);
		} else {
			odm_SetATCStatus(pDM_Odm, true);
		}
	}
}

void odm_parsing_cfo(void *dm_void, void *pkt_info_void, s8 *cfotail)
{
	struct dm_odm_t *dm_odm = (struct dm_odm_t *)dm_void;
	struct odm_packet_info *pkt_info = pkt_info_void;
	struct cfo_tracking *cfo_track = &dm_odm->DM_CfoTrack;
	u8 i;

	if (!(dm_odm->SupportAbility & ODM_BB_CFO_TRACKING))
		return;

	if (pkt_info->station_id != 0) {
		/*
		 * 3 Update CFO report for path-A & path-B
		 * Only paht-A and path-B have CFO tail and short CFO
		 */
		for (i = RF_PATH_A; i <= RF_PATH_B; i++)
			cfo_track->CFO_tail[i] = (int)cfotail[i];

		/* 3 Update packet counter */
		if (cfo_track->packetCount == 0xffffffff)
			cfo_track->packetCount = 0;
		else
			cfo_track->packetCount++;
	}
}
