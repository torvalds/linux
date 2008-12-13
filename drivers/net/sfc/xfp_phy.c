/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
/*
 * Driver for XFP optical PHYs (plus some support specific to the Quake 2022/32)
 * See www.amcc.com for details (search for qt2032)
 */

#include <linux/timer.h>
#include <linux/delay.h>
#include "efx.h"
#include "mdio_10g.h"
#include "xenpack.h"
#include "phy.h"
#include "falcon.h"

#define XFP_REQUIRED_DEVS (MDIO_MMDREG_DEVS_PCS |	\
			   MDIO_MMDREG_DEVS_PMAPMD |	\
			   MDIO_MMDREG_DEVS_PHYXS)

#define XFP_LOOPBACKS ((1 << LOOPBACK_PCS) |		\
		       (1 << LOOPBACK_PMAPMD) |		\
		       (1 << LOOPBACK_NETWORK))

/****************************************************************************/
/* Quake-specific MDIO registers */
#define MDIO_QUAKE_LED0_REG	(0xD006)

void xfp_set_led(struct efx_nic *p, int led, int mode)
{
	int addr = MDIO_QUAKE_LED0_REG + led;
	mdio_clause45_write(p, p->mii.phy_id, MDIO_MMD_PMAPMD, addr,
			    mode);
}

struct xfp_phy_data {
	enum efx_phy_mode phy_mode;
};

#define XFP_MAX_RESET_TIME 500
#define XFP_RESET_WAIT 10

/* Reset the PHYXS MMD. This is documented (for the Quake PHY) as doing
 * a complete soft reset.
 */
static int xfp_reset_phy(struct efx_nic *efx)
{
	int rc;

	rc = mdio_clause45_reset_mmd(efx, MDIO_MMD_PHYXS,
				     XFP_MAX_RESET_TIME / XFP_RESET_WAIT,
				     XFP_RESET_WAIT);
	if (rc < 0)
		goto fail;

	/* Wait 250ms for the PHY to complete bootup */
	msleep(250);

	/* Check that all the MMDs we expect are present and responding. We
	 * expect faults on some if the link is down, but not on the PHY XS */
	rc = mdio_clause45_check_mmds(efx, XFP_REQUIRED_DEVS,
				      MDIO_MMDREG_DEVS_PHYXS);
	if (rc < 0)
		goto fail;

	efx->board_info.init_leds(efx);

	return rc;

 fail:
	EFX_ERR(efx, "XFP: reset timed out!\n");
	return rc;
}

static int xfp_phy_init(struct efx_nic *efx)
{
	struct xfp_phy_data *phy_data;
	u32 devid = mdio_clause45_read_id(efx, MDIO_MMD_PHYXS);
	int rc;

	phy_data = kzalloc(sizeof(struct xfp_phy_data), GFP_KERNEL);
	if (!phy_data)
		return -ENOMEM;
	efx->phy_data = phy_data;

	EFX_INFO(efx, "XFP: PHY ID reg %x (OUI %x model %x revision"
		 " %x)\n", devid, MDIO_ID_OUI(devid), MDIO_ID_MODEL(devid),
		 MDIO_ID_REV(devid));

	phy_data->phy_mode = efx->phy_mode;

	rc = xfp_reset_phy(efx);

	EFX_INFO(efx, "XFP: PHY init %s.\n",
		 rc ? "failed" : "successful");
	if (rc < 0)
		goto fail;

	return 0;

 fail:
	kfree(efx->phy_data);
	efx->phy_data = NULL;
	return rc;
}

static void xfp_phy_clear_interrupt(struct efx_nic *efx)
{
	xenpack_clear_lasi_irqs(efx);
}

static int xfp_link_ok(struct efx_nic *efx)
{
	return mdio_clause45_links_ok(efx, XFP_REQUIRED_DEVS);
}

static void xfp_phy_poll(struct efx_nic *efx)
{
	int link_up = xfp_link_ok(efx);
	/* Simulate a PHY event if link state has changed */
	if (link_up != efx->link_up)
		falcon_sim_phy_event(efx);
}

static void xfp_phy_reconfigure(struct efx_nic *efx)
{
	struct xfp_phy_data *phy_data = efx->phy_data;

	/* Reset the PHY when moving from tx off to tx on */
	if (!(efx->phy_mode & PHY_MODE_TX_DISABLED) &&
	    (phy_data->phy_mode & PHY_MODE_TX_DISABLED))
		xfp_reset_phy(efx);

	mdio_clause45_transmit_disable(efx);
	mdio_clause45_phy_reconfigure(efx);

	phy_data->phy_mode = efx->phy_mode;
	efx->link_up = xfp_link_ok(efx);
	efx->link_speed = 10000;
	efx->link_fd = true;
	efx->link_fc = efx->wanted_fc;
}


static void xfp_phy_fini(struct efx_nic *efx)
{
	/* Clobber the LED if it was blinking */
	efx->board_info.blink(efx, false);

	/* Free the context block */
	kfree(efx->phy_data);
	efx->phy_data = NULL;
}

struct efx_phy_operations falcon_xfp_phy_ops = {
	.macs		 = EFX_XMAC,
	.init            = xfp_phy_init,
	.reconfigure     = xfp_phy_reconfigure,
	.poll            = xfp_phy_poll,
	.fini            = xfp_phy_fini,
	.clear_interrupt = xfp_phy_clear_interrupt,
	.get_settings    = mdio_clause45_get_settings,
	.set_settings	 = mdio_clause45_set_settings,
	.mmds            = XFP_REQUIRED_DEVS,
	.loopbacks       = XFP_LOOPBACKS,
};
