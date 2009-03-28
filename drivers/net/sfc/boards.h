/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2007-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_BOARDS_H
#define EFX_BOARDS_H

/* Board IDs (must fit in 8 bits) */
enum efx_board_type {
	EFX_BOARD_SFE4001 = 1,
	EFX_BOARD_SFE4002 = 2,
	EFX_BOARD_SFN4111T = 0x51,
	EFX_BOARD_SFN4112F = 0x52,
};

extern void efx_set_board_info(struct efx_nic *efx, u16 revision_info);

/* SFE4001 (10GBASE-T) */
extern int sfe4001_init(struct efx_nic *efx);
/* SFN4111T (100/1000/10GBASE-T) */
extern int sfn4111t_init(struct efx_nic *efx);

#endif
