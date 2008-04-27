/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2007 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_BOARDS_H
#define EFX_BOARDS_H

/* Board IDs (must fit in 8 bits) */
enum efx_board_type {
	EFX_BOARD_INVALID = 0,
	EFX_BOARD_SFE4001 = 1,   /* SFE4001 (10GBASE-T) */
	EFX_BOARD_SFE4002 = 2,
	/* Insert new types before here */
	EFX_BOARD_MAX
};

extern int efx_set_board_info(struct efx_nic *efx, u16 revision_info);
extern int sfe4001_poweron(struct efx_nic *efx);
extern void sfe4001_poweroff(struct efx_nic *efx);

#endif
