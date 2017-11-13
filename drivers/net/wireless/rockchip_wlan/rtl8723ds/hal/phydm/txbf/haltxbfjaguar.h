/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef __HAL_TXBF_JAGUAR_H__
#define __HAL_TXBF_JAGUAR_H__
#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
#if (BEAMFORMING_SUPPORT == 1)

void
hal_txbf_8812a_set_ndpa_rate(
	void			*p_dm_void,
	u8	BW,
	u8	rate
);


void
hal_txbf_jaguar_enter(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_jaguar_leave(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_jaguar_status(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_jaguar_fw_txbf(
	void			*p_dm_void,
	u8				idx
);


void
hal_txbf_jaguar_patch(
	void			*p_dm_void,
	u8				operation
);


void
hal_txbf_jaguar_clk_8812a(
	void			*p_dm_void
);
#else

#define hal_txbf_8812a_set_ndpa_rate(p_dm_void,	BW,	rate)
#define hal_txbf_jaguar_enter(p_dm_void, idx)
#define hal_txbf_jaguar_leave(p_dm_void, idx)
#define hal_txbf_jaguar_status(p_dm_void, idx)
#define hal_txbf_jaguar_fw_txbf(p_dm_void,	idx)
#define hal_txbf_jaguar_patch(p_dm_void, operation)
#define hal_txbf_jaguar_clk_8812a(p_dm_void)
#endif
#else

#define hal_txbf_8812a_set_ndpa_rate(p_dm_void,	BW,	rate)
#define hal_txbf_jaguar_enter(p_dm_void, idx)
#define hal_txbf_jaguar_leave(p_dm_void, idx)
#define hal_txbf_jaguar_status(p_dm_void, idx)
#define hal_txbf_jaguar_fw_txbf(p_dm_void,	idx)
#define hal_txbf_jaguar_patch(p_dm_void, operation)
#define hal_txbf_jaguar_clk_8812a(p_dm_void)
#endif

#endif	/*  #ifndef __HAL_TXBF_JAGUAR_H__ */
