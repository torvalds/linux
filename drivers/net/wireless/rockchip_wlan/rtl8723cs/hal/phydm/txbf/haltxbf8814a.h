#ifndef __HAL_TXBF_8814A_H__
#define __HAL_TXBF_8814A_H__

#if (RTL8814A_SUPPORT == 1)
#if (BEAMFORMING_SUPPORT == 1)

boolean
phydm_beamforming_set_iqgen_8814A(
	void			*p_dm_void
);

void
hal_txbf_8814a_set_ndpa_rate(
	void			*p_dm_void,
	u8	BW,
	u8	rate
);

u8
hal_txbf_8814a_get_ntx(
	void			*p_dm_void
);

void
hal_txbf_8814a_enter(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_8814a_leave(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_8814a_status(
	void			*p_dm_void,
	u8				idx
);

void
hal_txbf_8814a_reset_tx_path(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_8814a_get_tx_rate(
	void			*p_dm_void
);

void
hal_txbf_8814a_fw_txbf(
	void			*p_dm_void,
	u8				idx
);

#else

#define hal_txbf_8814a_set_ndpa_rate(p_dm_void,	BW,	rate)
#define hal_txbf_8814a_get_ntx(p_dm_void) 0
#define hal_txbf_8814a_enter(p_dm_void, idx)
#define hal_txbf_8814a_leave(p_dm_void, idx)
#define hal_txbf_8814a_status(p_dm_void, idx)
#define hal_txbf_8814a_reset_tx_path(p_dm_void,	idx)
#define hal_txbf_8814a_get_tx_rate(p_dm_void)
#define hal_txbf_8814a_fw_txbf(p_dm_void,	idx)
#define phydm_beamforming_set_iqgen_8814A(p_dm_void)	0

#endif

#else

#define hal_txbf_8814a_set_ndpa_rate(p_dm_void,	BW,	rate)
#define hal_txbf_8814a_get_ntx(p_dm_void) 0
#define hal_txbf_8814a_enter(p_dm_void, idx)
#define hal_txbf_8814a_leave(p_dm_void, idx)
#define hal_txbf_8814a_status(p_dm_void, idx)
#define hal_txbf_8814a_reset_tx_path(p_dm_void,	idx)
#define hal_txbf_8814a_get_tx_rate(p_dm_void)
#define hal_txbf_8814a_fw_txbf(p_dm_void,	idx)
#define phydm_beamforming_set_iqgen_8814A(p_dm_void)	0
#endif

#endif
