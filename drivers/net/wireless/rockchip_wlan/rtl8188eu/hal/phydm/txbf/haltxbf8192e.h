/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __HAL_TXBF_8192E_H__
#define __HAL_TXBF_8192E_H__

#if (RTL8192E_SUPPORT == 1)
#if (BEAMFORMING_SUPPORT == 1)

void
hal_txbf_8192e_set_ndpa_rate(
	void			*p_dm_void,
	u8	BW,
	u8	rate
);

void
hal_txbf_8192e_enter(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_8192e_leave(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_8192e_status(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_8192e_fw_tx_bf(
	void			*p_dm_void,
	u8				idx
);
#else

#define hal_txbf_8192e_set_ndpa_rate(p_dm_void, BW, rate)
#define hal_txbf_8192e_enter(p_dm_void, idx)
#define hal_txbf_8192e_leave(p_dm_void, idx)
#define hal_txbf_8192e_status(p_dm_void, idx)
#define hal_txbf_8192e_fw_tx_bf(p_dm_void, idx)

#endif

#else

#define hal_txbf_8192e_set_ndpa_rate(p_dm_void, BW, rate)
#define hal_txbf_8192e_enter(p_dm_void, idx)
#define hal_txbf_8192e_leave(p_dm_void, idx)
#define hal_txbf_8192e_status(p_dm_void, idx)
#define hal_txbf_8192e_fw_tx_bf(p_dm_void, idx)

#endif

#endif
