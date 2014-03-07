/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2007-2010 Solarflare Communications Inc.
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
extern const struct efx_phy_operations falcon_sfx7101_phy_ops;

void tenxpress_set_id_led(struct efx_nic *efx, enum efx_led_mode mode);

/****************************************************************************
 * AMCC/Quake QT202x PHYs
 */
extern const struct efx_phy_operations falcon_qt202x_phy_ops;

/* These PHYs provide various H/W control states for LEDs */
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

void falcon_qt202x_set_led(struct efx_nic *p, int led, int state);

/****************************************************************************
* Transwitch CX4 retimer
*/
extern const struct efx_phy_operations falcon_txc_phy_ops;

#define TXC_GPIO_DIR_INPUT	0
#define TXC_GPIO_DIR_OUTPUT	1

void falcon_txc_set_gpio_dir(struct efx_nic *efx, int pin, int dir);
void falcon_txc_set_gpio_val(struct efx_nic *efx, int pin, int val);

#endif
