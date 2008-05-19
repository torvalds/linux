/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2007 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef EFX_PHY_H
#define EFX_PHY_H

/****************************************************************************
 * 10Xpress (SFX7101) PHY
 */
extern struct efx_phy_operations falcon_tenxpress_phy_ops;

enum tenxpress_state {
	TENXPRESS_STATUS_OFF = 0,
	TENXPRESS_STATUS_OTEMP = 1,
	TENXPRESS_STATUS_NORMAL = 2,
};

extern void tenxpress_set_state(struct efx_nic *efx,
				enum tenxpress_state state);
extern void tenxpress_phy_blink(struct efx_nic *efx, int blink);
extern void tenxpress_crc_err(struct efx_nic *efx);

/****************************************************************************
 * Exported functions from the driver for XFP optical PHYs
 */
extern struct efx_phy_operations falcon_xfp_phy_ops;

/* The QUAKE XFP PHY provides various H/W control states for LEDs */
#define QUAKE_LED_LINK_INVAL	(0)
#define QUAKE_LED_LINK_STAT	(1)
#define QUAKE_LED_LINK_ACT	(2)
#define QUAKE_LED_LINK_ACTSTAT	(3)
#define QUAKE_LED_OFF		(4)
#define QUAKE_LED_ON		(5)
#define QUAKE_LED_LINK_INPUT	(6)	/* Pin is an input. */
/* What link the LED tracks */
#define QUAKE_LED_TXLINK	(0)
#define QUAKE_LED_RXLINK	(8)

extern void xfp_set_led(struct efx_nic *p, int led, int state);

#endif
