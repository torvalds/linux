/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2007 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_ENUM_H
#define EFX_ENUM_H

/*****************************************************************************/

/**
 * enum reset_type - reset types
 *
 * %RESET_TYPE_INVSIBLE, %RESET_TYPE_ALL, %RESET_TYPE_WORLD and
 * %RESET_TYPE_DISABLE specify the method/scope of the reset.  The
 * other valuesspecify reasons, which efx_schedule_reset() will choose
 * a method for.
 *
 * @RESET_TYPE_INVISIBLE: don't reset the PHYs or interrupts
 * @RESET_TYPE_ALL: reset everything but PCI core blocks
 * @RESET_TYPE_WORLD: reset everything, save & restore PCI config
 * @RESET_TYPE_DISABLE: disable NIC
 * @RESET_TYPE_MONITOR: reset due to hardware monitor
 * @RESET_TYPE_INT_ERROR: reset due to internal error
 * @RESET_TYPE_RX_RECOVERY: reset to recover from RX datapath errors
 * @RESET_TYPE_RX_DESC_FETCH: pcie error during rx descriptor fetch
 * @RESET_TYPE_TX_DESC_FETCH: pcie error during tx descriptor fetch
 * @RESET_TYPE_TX_SKIP: hardware completed empty tx descriptors
 */
enum reset_type {
	RESET_TYPE_NONE = -1,
	RESET_TYPE_INVISIBLE = 0,
	RESET_TYPE_ALL = 1,
	RESET_TYPE_WORLD = 2,
	RESET_TYPE_DISABLE = 3,
	RESET_TYPE_MAX_METHOD,
	RESET_TYPE_MONITOR,
	RESET_TYPE_INT_ERROR,
	RESET_TYPE_RX_RECOVERY,
	RESET_TYPE_RX_DESC_FETCH,
	RESET_TYPE_TX_DESC_FETCH,
	RESET_TYPE_TX_SKIP,
	RESET_TYPE_MAX,
};

#endif /* EFX_ENUM_H */
