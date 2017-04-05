/********************************************************************************/
/**/
/*Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.*/
/**/
/*This program is free software; you can redistribute it and/or modify it*/
/*under the terms of version 2 of the GNU General Public License as*/
/*published by the Free Software Foundation.*/
/**/
/*This program is distributed in the hope that it will be useful, but WITHOUT*/
/*ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or*/
/*FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for*/
/*more details.*/
/*You should have received a copy of the GNU General Public License along with*/
/*this program; if not, write to the Free Software Foundation, Inc.,*/
/*51 Franklin Street, Fifth Floor, Boston, MA 02110, USA*/
/**/
/**/
/********************************************************************************/
#ifndef	__PHYDM_HAL_TXBF_API_H__
#define __PHYDM_HAL_TXBF_API_H__

#if (defined(CONFIG_BB_TXBF_API))

#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
#define tx_bf_nr(a, b) ((a > b) ? (b) : (a))

u8
beamforming_get_htndp_tx_rate(
	void	*p_dm_void,
	u8	comp_steering_num_of_bfer
);

u8
beamforming_get_vht_ndp_tx_rate(
	void	*p_dm_void,
	u8	comp_steering_num_of_bfer
);

#endif

#if (RTL8822B_SUPPORT == 1)
u8
phydm_get_beamforming_sounding_info(
	void		*p_dm_void,
	u16	*troughput,
	u8	total_bfee_num,
	u8	*tx_rate
);

u8
phydm_get_ndpa_rate(
	void		*p_dm_void
);

u8
phydm_get_mu_bfee_snding_decision(
	void		*p_dm_void,
	u16	throughput
);

#else
#define phydm_get_beamforming_sounding_info(p_dm_void, troughput, total_bfee_num, tx_rate)
#define phydm_get_ndpa_rate(p_dm_void)
#define phydm_get_mu_bfee_snding_decision(p_dm_void, troughput)

#endif

#endif
#endif
