// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/*
 * Hardware library for MAC Merge Layer and Frame Preemption on TSN-capable
 * switches (VSC9959)
 *
 * Copyright 2022-2023 NXP
 */
#include <linux/ethtool.h>
#include <soc/mscc/ocelot.h>
#include <soc/mscc/ocelot_dev.h>
#include <soc/mscc/ocelot_qsys.h>

#include "ocelot.h"

static const char *
mm_verify_state_to_string(enum ethtool_mm_verify_status state)
{
	switch (state) {
	case ETHTOOL_MM_VERIFY_STATUS_INITIAL:
		return "INITIAL";
	case ETHTOOL_MM_VERIFY_STATUS_VERIFYING:
		return "VERIFYING";
	case ETHTOOL_MM_VERIFY_STATUS_SUCCEEDED:
		return "SUCCEEDED";
	case ETHTOOL_MM_VERIFY_STATUS_FAILED:
		return "FAILED";
	case ETHTOOL_MM_VERIFY_STATUS_DISABLED:
		return "DISABLED";
	default:
		return "UNKNOWN";
	}
}

static enum ethtool_mm_verify_status ocelot_mm_verify_status(u32 val)
{
	switch (DEV_MM_STAT_MM_STATUS_PRMPT_VERIFY_STATE_X(val)) {
	case 0:
		return ETHTOOL_MM_VERIFY_STATUS_INITIAL;
	case 1:
		return ETHTOOL_MM_VERIFY_STATUS_VERIFYING;
	case 2:
		return ETHTOOL_MM_VERIFY_STATUS_SUCCEEDED;
	case 3:
		return ETHTOOL_MM_VERIFY_STATUS_FAILED;
	case 4:
		return ETHTOOL_MM_VERIFY_STATUS_DISABLED;
	default:
		return ETHTOOL_MM_VERIFY_STATUS_UNKNOWN;
	}
}

void ocelot_port_mm_irq(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_mm_state *mm = &ocelot->mm[port];
	enum ethtool_mm_verify_status verify_status;
	u32 val;

	mutex_lock(&mm->lock);

	val = ocelot_port_readl(ocelot_port, DEV_MM_STATUS);

	verify_status = ocelot_mm_verify_status(val);
	if (mm->verify_status != verify_status) {
		dev_dbg(ocelot->dev,
			"Port %d MAC Merge verification state %s\n",
			port, mm_verify_state_to_string(verify_status));
		mm->verify_status = verify_status;
	}

	if (val & DEV_MM_STAT_MM_STATUS_PRMPT_ACTIVE_STICKY) {
		mm->tx_active = !!(val & DEV_MM_STAT_MM_STATUS_PRMPT_ACTIVE_STATUS);

		dev_dbg(ocelot->dev, "Port %d TX preemption %s\n",
			port, mm->tx_active ? "active" : "inactive");
	}

	if (val & DEV_MM_STAT_MM_STATUS_UNEXP_RX_PFRM_STICKY) {
		dev_err(ocelot->dev,
			"Unexpected P-frame received on port %d while verification was unsuccessful or not yet verified\n",
			port);
	}

	if (val & DEV_MM_STAT_MM_STATUS_UNEXP_TX_PFRM_STICKY) {
		dev_err(ocelot->dev,
			"Unexpected P-frame requested to be transmitted on port %d while verification was unsuccessful or not yet verified, or MM_TX_ENA=0\n",
			port);
	}

	ocelot_port_writel(ocelot_port, val, DEV_MM_STATUS);

	mutex_unlock(&mm->lock);
}
EXPORT_SYMBOL_GPL(ocelot_port_mm_irq);

int ocelot_port_set_mm(struct ocelot *ocelot, int port,
		       struct ethtool_mm_cfg *cfg,
		       struct netlink_ext_ack *extack)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	u32 mm_enable = 0, verify_disable = 0, add_frag_size;
	struct ocelot_mm_state *mm;
	int err;

	if (!ocelot->mm_supported)
		return -EOPNOTSUPP;

	mm = &ocelot->mm[port];

	err = ethtool_mm_frag_size_min_to_add(cfg->tx_min_frag_size,
					      &add_frag_size, extack);
	if (err)
		return err;

	if (cfg->pmac_enabled)
		mm_enable |= DEV_MM_CONFIG_ENABLE_CONFIG_MM_RX_ENA;

	if (cfg->tx_enabled)
		mm_enable |= DEV_MM_CONFIG_ENABLE_CONFIG_MM_TX_ENA;

	if (!cfg->verify_enabled)
		verify_disable = DEV_MM_CONFIG_VERIF_CONFIG_PRM_VERIFY_DIS;

	mutex_lock(&mm->lock);

	ocelot_port_rmwl(ocelot_port, mm_enable,
			 DEV_MM_CONFIG_ENABLE_CONFIG_MM_TX_ENA |
			 DEV_MM_CONFIG_ENABLE_CONFIG_MM_RX_ENA,
			 DEV_MM_ENABLE_CONFIG);

	ocelot_port_rmwl(ocelot_port, verify_disable |
			 DEV_MM_CONFIG_VERIF_CONFIG_PRM_VERIFY_TIME(cfg->verify_time),
			 DEV_MM_CONFIG_VERIF_CONFIG_PRM_VERIFY_DIS |
			 DEV_MM_CONFIG_VERIF_CONFIG_PRM_VERIFY_TIME_M,
			 DEV_MM_VERIF_CONFIG);

	ocelot_rmw_rix(ocelot,
		       QSYS_PREEMPTION_CFG_MM_ADD_FRAG_SIZE(add_frag_size),
		       QSYS_PREEMPTION_CFG_MM_ADD_FRAG_SIZE_M,
		       QSYS_PREEMPTION_CFG,
		       port);

	mutex_unlock(&mm->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(ocelot_port_set_mm);

int ocelot_port_get_mm(struct ocelot *ocelot, int port,
		       struct ethtool_mm_state *state)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_mm_state *mm;
	u32 val, add_frag_size;

	if (!ocelot->mm_supported)
		return -EOPNOTSUPP;

	mm = &ocelot->mm[port];

	mutex_lock(&mm->lock);

	val = ocelot_port_readl(ocelot_port, DEV_MM_ENABLE_CONFIG);
	state->pmac_enabled = !!(val & DEV_MM_CONFIG_ENABLE_CONFIG_MM_RX_ENA);
	state->tx_enabled = !!(val & DEV_MM_CONFIG_ENABLE_CONFIG_MM_TX_ENA);

	val = ocelot_port_readl(ocelot_port, DEV_MM_VERIF_CONFIG);
	state->verify_enabled = !(val & DEV_MM_CONFIG_VERIF_CONFIG_PRM_VERIFY_DIS);
	state->verify_time = DEV_MM_CONFIG_VERIF_CONFIG_PRM_VERIFY_TIME_X(val);
	state->max_verify_time = 128;

	val = ocelot_read_rix(ocelot, QSYS_PREEMPTION_CFG, port);
	add_frag_size = QSYS_PREEMPTION_CFG_MM_ADD_FRAG_SIZE_X(val);
	state->tx_min_frag_size = ethtool_mm_frag_size_add_to_min(add_frag_size);
	state->rx_min_frag_size = ETH_ZLEN;

	state->verify_status = mm->verify_status;
	state->tx_active = mm->tx_active;

	mutex_unlock(&mm->lock);

	return 0;
}
EXPORT_SYMBOL_GPL(ocelot_port_get_mm);

int ocelot_mm_init(struct ocelot *ocelot)
{
	struct ocelot_port *ocelot_port;
	struct ocelot_mm_state *mm;
	int port;

	if (!ocelot->mm_supported)
		return 0;

	ocelot->mm = devm_kcalloc(ocelot->dev, ocelot->num_phys_ports,
				  sizeof(*ocelot->mm), GFP_KERNEL);
	if (!ocelot->mm)
		return -ENOMEM;

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		u32 val;

		mm = &ocelot->mm[port];
		mutex_init(&mm->lock);
		ocelot_port = ocelot->ports[port];

		/* Update initial status variable for the
		 * verification state machine
		 */
		val = ocelot_port_readl(ocelot_port, DEV_MM_STATUS);
		mm->verify_status = ocelot_mm_verify_status(val);
	}

	return 0;
}
