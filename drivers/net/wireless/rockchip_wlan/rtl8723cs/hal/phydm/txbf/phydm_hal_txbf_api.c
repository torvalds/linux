#include "mp_precomp.h"
#include "phydm_precomp.h"

#if (defined(CONFIG_BB_TXBF_API))
#if (RTL8822B_SUPPORT == 1)
/*Add by YuChen for 8822B MU-MIMO API*/

/*this function is only used for BFer*/
u8
phydm_get_ndpa_rate(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8		ndpa_rate = ODM_RATE6M;

	if (p_dm_odm->rssi_min >= 30)	/*link RSSI > 30%*/
		ndpa_rate = ODM_RATE24M;
	else if (p_dm_odm->rssi_min <= 25)
		ndpa_rate = ODM_RATE6M;

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_TRACE, ("[%s] ndpa_rate = 0x%x\n", __func__, ndpa_rate));

	return ndpa_rate;

}

/*this function is only used for BFer*/
u8
phydm_get_beamforming_sounding_info(
	void		*p_dm_void,
	u16	*troughput,
	u8	total_bfee_num,
	u8	*tx_rate
)
{
	u8	idx = 0;
	u8	soundingdecision = 0xff;
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	for (idx = 0; idx < total_bfee_num; idx++) {
		if (((tx_rate[idx] >= ODM_RATEVHTSS3MCS7) && (tx_rate[idx] <= ODM_RATEVHTSS3MCS9)))
			soundingdecision = soundingdecision & ~(1 << idx);
	}

	for (idx = 0; idx < total_bfee_num; idx++) {
		if (troughput[idx] <= 10)
			soundingdecision = soundingdecision & ~(1 << idx);
	}

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_TRACE, ("[%s] soundingdecision = 0x%x\n", __func__, soundingdecision));

	return soundingdecision;

}

/*this function is only used for BFer*/
u8
phydm_get_mu_bfee_snding_decision(
	void		*p_dm_void,
	u16	throughput
)
{
	u8	snding_score = 0;
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	/*throughput unit is Mbps*/
	if (throughput >= 500)
		snding_score = 100;
	else if (throughput >= 450)
		snding_score = 90;
	else if (throughput >= 400)
		snding_score = 80;
	else if (throughput >= 350)
		snding_score = 70;
	else if (throughput >= 300)
		snding_score = 60;
	else if (throughput >= 250)
		snding_score = 50;
	else if (throughput >= 200)
		snding_score = 40;
	else if (throughput >= 150)
		snding_score = 30;
	else if (throughput >= 100)
		snding_score = 20;
	else if (throughput >= 50)
		snding_score = 10;
	else
		snding_score = 0;

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_TXBF, ODM_DBG_TRACE, ("[%s] snding_score = 0x%x\n", __func__, snding_score));

	return snding_score;

}


#endif
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
u8
beamforming_get_htndp_tx_rate(
	void	*p_dm_void,
	u8	comp_steering_num_of_bfer
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8 nr_index = 0;
	u8 ndp_tx_rate;
	/*Find nr*/
#if (RTL8814A_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8814A)
		nr_index = tx_bf_nr(hal_txbf_8814a_get_ntx(p_dm_odm), comp_steering_num_of_bfer);
	else
#endif
		nr_index = tx_bf_nr(1, comp_steering_num_of_bfer);

	switch (nr_index) {
	case 1:
		ndp_tx_rate = ODM_MGN_MCS8;
		break;

	case 2:
		ndp_tx_rate = ODM_MGN_MCS16;
		break;

	case 3:
		ndp_tx_rate = ODM_MGN_MCS24;
		break;

	default:
		ndp_tx_rate = ODM_MGN_MCS8;
		break;
	}

	return ndp_tx_rate;

}

u8
beamforming_get_vht_ndp_tx_rate(
	void	*p_dm_void,
	u8	comp_steering_num_of_bfer
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8 nr_index = 0;
	u8 ndp_tx_rate;
	/*Find nr*/
#if (RTL8814A_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8814A)
		nr_index = tx_bf_nr(hal_txbf_8814a_get_ntx(p_dm_odm), comp_steering_num_of_bfer);
	else
#endif
		nr_index = tx_bf_nr(1, comp_steering_num_of_bfer);

	switch (nr_index) {
	case 1:
		ndp_tx_rate = ODM_MGN_VHT2SS_MCS0;
		break;

	case 2:
		ndp_tx_rate = ODM_MGN_VHT3SS_MCS0;
		break;

	case 3:
		ndp_tx_rate = ODM_MGN_VHT4SS_MCS0;
		break;

	default:
		ndp_tx_rate = ODM_MGN_VHT2SS_MCS0;
		break;
	}

	return ndp_tx_rate;

}
#endif

#endif
