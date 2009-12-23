/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2006-2009 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */
/*
 * Driver for AMCC QT202x SFP+ and XFP adapters; see www.amcc.com for details
 */

#include <linux/timer.h>
#include <linux/delay.h>
#include "efx.h"
#include "mdio_10g.h"
#include "phy.h"
#include "nic.h"

#define QT202X_REQUIRED_DEVS (MDIO_DEVS_PCS |		\
			      MDIO_DEVS_PMAPMD |	\
			      MDIO_DEVS_PHYXS)

#define QT202X_LOOPBACKS ((1 << LOOPBACK_PCS) |		\
			  (1 << LOOPBACK_PMAPMD) |	\
			  (1 << LOOPBACK_PHYXS_WS))

/****************************************************************************/
/* Quake-specific MDIO registers */
#define MDIO_QUAKE_LED0_REG	(0xD006)

/* QT2025C only */
#define PCS_FW_HEARTBEAT_REG	0xd7ee
#define PCS_FW_HEARTB_LBN	0
#define PCS_FW_HEARTB_WIDTH	8
#define PCS_UC8051_STATUS_REG	0xd7fd
#define PCS_UC_STATUS_LBN	0
#define PCS_UC_STATUS_WIDTH	8
#define PCS_UC_STATUS_FW_SAVE	0x20
#define PMA_PMD_FTX_CTRL2_REG	0xc309
#define PMA_PMD_FTX_STATIC_LBN	13
#define PMA_PMD_VEND1_REG	0xc001
#define PMA_PMD_VEND1_LBTXD_LBN	15
#define PCS_VEND1_REG	   	0xc000
#define PCS_VEND1_LBTXD_LBN	5

void falcon_qt202x_set_led(struct efx_nic *p, int led, int mode)
{
	int addr = MDIO_QUAKE_LED0_REG + led;
	efx_mdio_write(p, MDIO_MMD_PMAPMD, addr, mode);
}

struct qt202x_phy_data {
	enum efx_phy_mode phy_mode;
};

#define QT2022C2_MAX_RESET_TIME 500
#define QT2022C2_RESET_WAIT 10

static int qt2025c_wait_reset(struct efx_nic *efx)
{
	unsigned long timeout = jiffies + 10 * HZ;
	int reg, old_counter = 0;

	/* Wait for firmware heartbeat to start */
	for (;;) {
		int counter;
		reg = efx_mdio_read(efx, MDIO_MMD_PCS, PCS_FW_HEARTBEAT_REG);
		if (reg < 0)
			return reg;
		counter = ((reg >> PCS_FW_HEARTB_LBN) &
			    ((1 << PCS_FW_HEARTB_WIDTH) - 1));
		if (old_counter == 0)
			old_counter = counter;
		else if (counter != old_counter)
			break;
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		msleep(10);
	}

	/* Wait for firmware status to look good */
	for (;;) {
		reg = efx_mdio_read(efx, MDIO_MMD_PCS, PCS_UC8051_STATUS_REG);
		if (reg < 0)
			return reg;
		if ((reg &
		     ((1 << PCS_UC_STATUS_WIDTH) - 1) << PCS_UC_STATUS_LBN) >=
		    PCS_UC_STATUS_FW_SAVE)
			break;
		if (time_after(jiffies, timeout))
			return -ETIMEDOUT;
		msleep(100);
	}

	return 0;
}

static int qt202x_reset_phy(struct efx_nic *efx)
{
	int rc;

	if (efx->phy_type == PHY_TYPE_QT2025C) {
		/* Wait for the reset triggered by falcon_reset_hw()
		 * to complete */
		rc = qt2025c_wait_reset(efx);
		if (rc < 0)
			goto fail;
	} else {
		/* Reset the PHYXS MMD. This is documented as doing
		 * a complete soft reset. */
		rc = efx_mdio_reset_mmd(efx, MDIO_MMD_PHYXS,
					QT2022C2_MAX_RESET_TIME /
					QT2022C2_RESET_WAIT,
					QT2022C2_RESET_WAIT);
		if (rc < 0)
			goto fail;
	}

	/* Wait 250ms for the PHY to complete bootup */
	msleep(250);

	/* Check that all the MMDs we expect are present and responding. We
	 * expect faults on some if the link is down, but not on the PHY XS */
	rc = efx_mdio_check_mmds(efx, QT202X_REQUIRED_DEVS, MDIO_DEVS_PHYXS);
	if (rc < 0)
		goto fail;

	falcon_board(efx)->type->init_phy(efx);

	return rc;

 fail:
	EFX_ERR(efx, "PHY reset timed out\n");
	return rc;
}

static int qt202x_phy_probe(struct efx_nic *efx)
{
	struct qt202x_phy_data *phy_data;

	phy_data = kzalloc(sizeof(struct qt202x_phy_data), GFP_KERNEL);
	if (!phy_data)
		return -ENOMEM;
	efx->phy_data = phy_data;
	phy_data->phy_mode = efx->phy_mode;

	efx->mdio.mmds = QT202X_REQUIRED_DEVS;
	efx->mdio.mode_support = MDIO_SUPPORTS_C45 | MDIO_EMULATE_C22;
	efx->loopback_modes = QT202X_LOOPBACKS | FALCON_XMAC_LOOPBACKS;
	return 0;
}

static int qt202x_phy_init(struct efx_nic *efx)
{
	u32 devid;
	int rc;

	rc = qt202x_reset_phy(efx);
	if (rc) {
		EFX_ERR(efx, "PHY init failed\n");
		return rc;
	}

	devid = efx_mdio_read_id(efx, MDIO_MMD_PHYXS);
	EFX_INFO(efx, "PHY ID reg %x (OUI %06x model %02x revision %x)\n",
		 devid, efx_mdio_id_oui(devid), efx_mdio_id_model(devid),
		 efx_mdio_id_rev(devid));

	return 0;
}

static int qt202x_link_ok(struct efx_nic *efx)
{
	return efx_mdio_links_ok(efx, QT202X_REQUIRED_DEVS);
}

static bool qt202x_phy_poll(struct efx_nic *efx)
{
	bool was_up = efx->link_state.up;

	efx->link_state.up = qt202x_link_ok(efx);
	efx->link_state.speed = 10000;
	efx->link_state.fd = true;
	efx->link_state.fc = efx->wanted_fc;

	return efx->link_state.up != was_up;
}

static int qt202x_phy_reconfigure(struct efx_nic *efx)
{
	struct qt202x_phy_data *phy_data = efx->phy_data;

	if (efx->phy_type == PHY_TYPE_QT2025C) {
		/* There are several different register bits which can
		 * disable TX (and save power) on direct-attach cables
		 * or optical transceivers, varying somewhat between
		 * firmware versions.  Only 'static mode' appears to
		 * cover everything. */
		mdio_set_flag(
			&efx->mdio, efx->mdio.prtad, MDIO_MMD_PMAPMD,
			PMA_PMD_FTX_CTRL2_REG, 1 << PMA_PMD_FTX_STATIC_LBN,
			efx->phy_mode & PHY_MODE_TX_DISABLED ||
			efx->phy_mode & PHY_MODE_LOW_POWER ||
			efx->loopback_mode == LOOPBACK_PCS ||
			efx->loopback_mode == LOOPBACK_PMAPMD);
	} else {
		/* Reset the PHY when moving from tx off to tx on */
		if (!(efx->phy_mode & PHY_MODE_TX_DISABLED) &&
		    (phy_data->phy_mode & PHY_MODE_TX_DISABLED))
			qt202x_reset_phy(efx);

		efx_mdio_transmit_disable(efx);
	}

	efx_mdio_phy_reconfigure(efx);

	phy_data->phy_mode = efx->phy_mode;

	return 0;
}

static void qt202x_phy_get_settings(struct efx_nic *efx, struct ethtool_cmd *ecmd)
{
	mdio45_ethtool_gset(&efx->mdio, ecmd);
}

static void qt202x_phy_remove(struct efx_nic *efx)
{
	/* Free the context block */
	kfree(efx->phy_data);
	efx->phy_data = NULL;
}

struct efx_phy_operations falcon_qt202x_phy_ops = {
	.probe		 = qt202x_phy_probe,
	.init		 = qt202x_phy_init,
	.reconfigure	 = qt202x_phy_reconfigure,
	.poll	     	 = qt202x_phy_poll,
	.fini		 = efx_port_dummy_op_void,
	.remove	  	 = qt202x_phy_remove,
	.get_settings	 = qt202x_phy_get_settings,
	.set_settings	 = efx_mdio_set_settings,
};
