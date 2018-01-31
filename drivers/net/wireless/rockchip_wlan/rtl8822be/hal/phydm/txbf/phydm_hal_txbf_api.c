/* SPDX-License-Identifier: GPL-2.0 */
#include "mp_precomp.h"
#include "phydm_precomp.h"

#if (defined(CONFIG_BB_TXBF_API))
#if (RTL8822B_SUPPORT == 1)
/*Add by YuChen for 8822B MU-MIMO API*/

/*this function is only used for BFer*/
u1Byte
phydm_get_ndpa_rate(
	IN PVOID		pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte		NDPARate = ODM_RATE6M;

	if (pDM_Odm->RSSI_Min >= 30)	/*link RSSI > 30%*/
		NDPARate = ODM_RATE24M;
	else if (pDM_Odm->RSSI_Min <= 25)
		NDPARate = ODM_RATE6M;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_TRACE, ("[%s] NDPARate = 0x%x\n", __func__, NDPARate));

	return NDPARate;

}

/*this function is only used for BFer*/
u1Byte
phydm_get_beamforming_sounding_info(
	IN PVOID		pDM_VOID,
	IN pu2Byte	Troughput,
	IN u1Byte	Total_BFee_Num,
	IN pu1Byte	TxRate
	)
{
	u1Byte	idx = 0;
	u1Byte	soundingdecision = 0xff;
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;

	for (idx = 0; idx < Total_BFee_Num; idx++) {
		if (((TxRate[idx] >= ODM_RATEVHTSS3MCS7) && (TxRate[idx] <= ODM_RATEVHTSS3MCS9)))
			soundingdecision = soundingdecision & ~(1<<idx);
	}

	for (idx = 0; idx < Total_BFee_Num; idx++) {
		if (Troughput[idx] <= 10)
			soundingdecision = soundingdecision & ~(1<<idx);
	}

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_TRACE, ("[%s] soundingdecision = 0x%x\n", __func__, soundingdecision));

	return soundingdecision;

}

/*this function is only used for BFer*/
u1Byte
phydm_get_mu_bfee_snding_decision(
	IN PVOID		pDM_VOID,
	IN u2Byte	Throughput
	)
{
	u1Byte	snding_score = 0;
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;

	/*Throughput unit is Mbps*/
	if (Throughput >= 500)
		snding_score = 100;
	else if (Throughput >= 450)
		snding_score = 90;
	else if (Throughput >= 400)
		snding_score = 80;
	else if (Throughput >= 350)
		snding_score = 70;
	else if (Throughput >= 300)
		snding_score = 60;
	else if (Throughput >= 250)
		snding_score = 50;
	else if (Throughput >= 200)
		snding_score = 40;
	else if (Throughput >= 150)
		snding_score = 30;
	else if (Throughput >= 100)
		snding_score = 20;
	else if (Throughput >= 50)
		snding_score = 10;
	else
		snding_score = 0;
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_TRACE, ("[%s] snding_score = 0x%d\n", __func__, snding_score));

	return snding_score;

}


#endif
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
u1Byte
Beamforming_GetHTNDPTxRate(
	IN	PVOID	pDM_VOID,
	u1Byte	CompSteeringNumofBFer
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte Nr_index = 0;
	u1Byte NDPTxRate;
	/*Find Nr*/
#if (RTL8814A_SUPPORT == 1)	
	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		Nr_index = TxBF_Nr(halTxbf8814A_GetNtx(pDM_Odm), CompSteeringNumofBFer);
	else
#endif
		Nr_index = TxBF_Nr(1, CompSteeringNumofBFer);
	
	switch (Nr_index) {
	case 1:
	NDPTxRate = ODM_MGN_MCS8;
	break;

	case 2:
	NDPTxRate = ODM_MGN_MCS16;
	break;

	case 3:
	NDPTxRate = ODM_MGN_MCS24;
	break;
			
	default:
	NDPTxRate = ODM_MGN_MCS8;
	break;
	}

return NDPTxRate;

}

u1Byte
Beamforming_GetVHTNDPTxRate(
	IN	PVOID	pDM_VOID,
	u1Byte	CompSteeringNumofBFer
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte Nr_index = 0;
	u1Byte NDPTxRate;
	/*Find Nr*/
#if (RTL8814A_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_RTL8814A)
		Nr_index = TxBF_Nr(halTxbf8814A_GetNtx(pDM_Odm), CompSteeringNumofBFer);
	else
#endif
		Nr_index = TxBF_Nr(1, CompSteeringNumofBFer);
	
	switch (Nr_index) {
	case 1:
	NDPTxRate = ODM_MGN_VHT2SS_MCS0;
	break;

	case 2:
	NDPTxRate = ODM_MGN_VHT3SS_MCS0;
	break;

	case 3:
	NDPTxRate = ODM_MGN_VHT4SS_MCS0;
	break;
			
	default:
	NDPTxRate = ODM_MGN_VHT2SS_MCS0;
	break;
	}

return NDPTxRate;

}
#endif

#endif

