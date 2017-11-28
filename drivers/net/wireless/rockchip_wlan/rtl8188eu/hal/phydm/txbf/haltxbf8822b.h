#ifndef __HAL_TXBF_8822B_H__
#define __HAL_TXBF_8822B_H__

#if (RTL8822B_SUPPORT == 1)
#if (BEAMFORMING_SUPPORT == 1)

void
hal_txbf_8822b_enter(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_8822b_leave(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_8822b_status(
	void			*p_dm_void,
	u8				beamform_idx
);

void
hal_txbf_8822b_config_gtab(
	void			*p_dm_void
);

void
hal_txbf_8822b_fw_txbf(
	void			*p_dm_void,
	u8				idx
);
#else
#define hal_txbf_8822b_enter(p_dm_void, idx)
#define hal_txbf_8822b_leave(p_dm_void, idx)
#define hal_txbf_8822b_status(p_dm_void, idx)
#define hal_txbf_8822b_fw_txbf(p_dm_void, idx)
#define hal_txbf_8822b_config_gtab(p_dm_void)

#endif

#if (defined(CONFIG_BB_TXBF_API))
void
phydm_8822btxbf_rfmode(
	void		*p_dm_void,
	u8	su_bfee_cnt,
	u8	mu_bfee_cnt
);

void
phydm_8822b_sutxbfer_workaroud(
	void		*p_dm_void,
	bool	enable_su_bfer,
	u8	nc,
	u8	nr,
	u8	ng,
	u8	CB,
	u8	BW,
	bool	is_vht
);

#else
#define phydm_8822btxbf_rfmode(p_dm_void, su_bfee_cnt, mu_bfee_cnt)
#define phydm_8822b_sutxbfer_workaroud(p_dm_void, enable_su_bfer, nc, nr, ng, CB, BW, is_vht)
#endif

#else
#define hal_txbf_8822b_enter(p_dm_void, idx)
#define hal_txbf_8822b_leave(p_dm_void, idx)
#define hal_txbf_8822b_status(p_dm_void, idx)
#define hal_txbf_8822b_fw_txbf(p_dm_void, idx)
#define hal_txbf_8822b_config_gtab(p_dm_void)

#endif
#endif
