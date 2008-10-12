/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2007 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include "phy.h"
#include "boards.h"
#include "efx.h"

/* Macros for unpacking the board revision */
/* The revision info is in host byte order. */
#define BOARD_TYPE(_rev) (_rev >> 8)
#define BOARD_MAJOR(_rev) ((_rev >> 4) & 0xf)
#define BOARD_MINOR(_rev) (_rev & 0xf)

/* Blink support. If the PHY has no auto-blink mode so we hang it off a timer */
#define BLINK_INTERVAL (HZ/2)

static void blink_led_timer(unsigned long context)
{
	struct efx_nic *efx = (struct efx_nic *)context;
	struct efx_blinker *bl = &efx->board_info.blinker;
	efx->board_info.set_fault_led(efx, bl->state);
	bl->state = !bl->state;
	if (bl->resubmit)
		mod_timer(&bl->timer, jiffies + BLINK_INTERVAL);
}

static void board_blink(struct efx_nic *efx, bool blink)
{
	struct efx_blinker *blinker = &efx->board_info.blinker;

	/* The rtnl mutex serialises all ethtool ioctls, so
	 * nothing special needs doing here. */
	if (blink) {
		blinker->resubmit = true;
		blinker->state = false;
		setup_timer(&blinker->timer, blink_led_timer,
			    (unsigned long)efx);
		mod_timer(&blinker->timer, jiffies + BLINK_INTERVAL);
	} else {
		blinker->resubmit = false;
		if (blinker->timer.function)
			del_timer_sync(&blinker->timer);
		efx->board_info.set_fault_led(efx, false);
	}
}

/*****************************************************************************
 * Support for the SFE4002
 *
 */
/****************************************************************************/
/* LED allocations. Note that on rev A0 boards the schematic and the reality
 * differ: red and green are swapped. Below is the fixed (A1) layout (there
 * are only 3 A0 boards in existence, so no real reason to make this
 * conditional).
 */
#define SFE4002_FAULT_LED (2)	/* Red */
#define SFE4002_RX_LED    (0)	/* Green */
#define SFE4002_TX_LED    (1)	/* Amber */

static int sfe4002_init_leds(struct efx_nic *efx)
{
	/* Set the TX and RX LEDs to reflect status and activity, and the
	 * fault LED off */
	xfp_set_led(efx, SFE4002_TX_LED,
		    QUAKE_LED_TXLINK | QUAKE_LED_LINK_ACTSTAT);
	xfp_set_led(efx, SFE4002_RX_LED,
		    QUAKE_LED_RXLINK | QUAKE_LED_LINK_ACTSTAT);
	xfp_set_led(efx, SFE4002_FAULT_LED, QUAKE_LED_OFF);
	efx->board_info.blinker.led_num = SFE4002_FAULT_LED;
	return 0;
}

static void sfe4002_fault_led(struct efx_nic *efx, bool state)
{
	xfp_set_led(efx, SFE4002_FAULT_LED, state ? QUAKE_LED_ON :
			QUAKE_LED_OFF);
}

static int sfe4002_init(struct efx_nic *efx)
{
	efx->board_info.init_leds = sfe4002_init_leds;
	efx->board_info.set_fault_led = sfe4002_fault_led;
	efx->board_info.blink = board_blink;
	return 0;
}

/* This will get expanded as board-specific details get moved out of the
 * PHY drivers. */
struct efx_board_data {
	const char *ref_model;
	const char *gen_type;
	int (*init) (struct efx_nic *nic);
};

static int dummy_init(struct efx_nic *nic)
{
	return 0;
}

static struct efx_board_data board_data[] = {
	[EFX_BOARD_INVALID] =
	{NULL,	    NULL,                  dummy_init},
	[EFX_BOARD_SFE4001] =
	{"SFE4001", "10GBASE-T adapter",   sfe4001_init},
	[EFX_BOARD_SFE4002] =
	{"SFE4002", "XFP adapter",         sfe4002_init},
};

int efx_set_board_info(struct efx_nic *efx, u16 revision_info)
{
	int rc = 0;
	struct efx_board_data *data;

	if (BOARD_TYPE(revision_info) >= EFX_BOARD_MAX) {
		EFX_ERR(efx, "squashing unknown board type %d\n",
			BOARD_TYPE(revision_info));
		revision_info = 0;
	}

	if (BOARD_TYPE(revision_info) == 0) {
		efx->board_info.major = 0;
		efx->board_info.minor = 0;
		/* For early boards that don't have revision info. there is
		 * only 1 board for each PHY type, so we can work it out, with
		 * the exception of the PHY-less boards. */
		switch (efx->phy_type) {
		case PHY_TYPE_10XPRESS:
			efx->board_info.type = EFX_BOARD_SFE4001;
			break;
		case PHY_TYPE_XFP:
			efx->board_info.type = EFX_BOARD_SFE4002;
			break;
		default:
			efx->board_info.type = 0;
			break;
		}
	} else {
		efx->board_info.type = BOARD_TYPE(revision_info);
		efx->board_info.major = BOARD_MAJOR(revision_info);
		efx->board_info.minor = BOARD_MINOR(revision_info);
	}

	data = &board_data[efx->board_info.type];

	/* Report the board model number or generic type for recognisable
	 * boards. */
	if (efx->board_info.type != 0)
		EFX_INFO(efx, "board is %s rev %c%d\n",
			 (efx->pci_dev->subsystem_vendor == EFX_VENDID_SFC)
			 ? data->ref_model : data->gen_type,
			 'A' + efx->board_info.major, efx->board_info.minor);

	efx->board_info.init = data->init;

	return rc;
}
