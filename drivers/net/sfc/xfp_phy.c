/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2006-2008 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
/*
 * Driver for XFP optical PHYs (plus some support specific to the Quake 2032)
 * See www.amcc.com for details (search for qt2032)
 */

#include <linux/timer.h>
#include <linux/delay.h>
#include "efx.h"
#include "gmii.h"
#include "mdio_10g.h"
#include "xenpack.h"
#include "phy.h"
#include "mac.h"

#define XFP_REQUIRED_DEVS (MDIO_MMDREG_DEVS0_PCS |	\
			   MDIO_MMDREG_DEVS0_PMAPMD |	\
			   MDIO_MMDREG_DEVS0_PHYXS)

/****************************************************************************/
/* Quake-specific MDIO registers */
#define MDIO_QUAKE_LED0_REG	(0xD006)

void xfp_set_led(struct efx_nic *p, int led, int mode)
{
	int addr = MDIO_QUAKE_LED0_REG + led;
	mdio_clause45_write(p, p->mii.phy_id, MDIO_MMD_PMAPMD, addr,
			    mode);
}

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
				      MDIO_MMDREG_DEVS0_PHYXS);
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
	u32 devid = mdio_clause45_read_id(efx, MDIO_MMD_PHYXS);
	int rc;

	EFX_INFO(efx, "XFP: PHY ID reg %x (OUI %x model %x revision"
		 " %x)\n", devid, MDIO_ID_OUI(devid), MDIO_ID_MODEL(devid),
		 MDIO_ID_REV(devid));

	rc = xfp_reset_phy(efx);

	EFX_INFO(efx, "XFP: PHY init %s.\n",
		 rc ? "failed" : "successful");

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

static int xfp_phy_check_hw(struct efx_nic *efx)
{
	int rc = 0;
	int link_up = xfp_link_ok(efx);
	/* Simulate a PHY event if link state has changed */
	if (link_up != efx->link_up)
		falcon_xmac_sim_phy_event(efx);

	return rc;
}

static void xfp_phy_reconfigure(struct efx_nic *efx)
{
	efx->link_up = xfp_link_ok(efx);
	efx->link_options = GM_LPA_10000FULL;
}


static void xfp_phy_fini(struct efx_nic *efx)
{
	/* Clobber the LED if it was blinking */
	efx->board_info.blink(efx, 0);
}

struct efx_phy_operations falcon_xfp_phy_ops = {
	.init            = xfp_phy_init,
	.reconfigure     = xfp_phy_reconfigure,
	.check_hw        = xfp_phy_check_hw,
	.fini            = xfp_phy_fini,
	.clear_interrupt = xfp_phy_clear_interrupt,
	.reset_xaui      = efx_port_dummy_op_void,
	.mmds            = XFP_REQUIRED_DEVS,
};
