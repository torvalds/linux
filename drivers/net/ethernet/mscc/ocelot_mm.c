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

void ocelot_port_update_active_preemptible_tcs(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_mm_state *mm = &ocelot->mm[port];
	u32 val = 0;

	lockdep_assert_held(&ocelot->fwd_domain_lock);

	/* Only commit preemptible TCs when MAC Merge is active.
	 * On NXP LS1028A, when using QSGMII, the port hangs if transmitting
	 * preemptible frames at any other link speed than gigabit, so avoid
	 * preemption at lower speeds in this PHY mode.
	 */
	if ((ocelot_port->phy_mode != PHY_INTERFACE_MODE_QSGMII ||
	     ocelot_port->speed == SPEED_1000) && mm->tx_active)
		val = mm->preemptible_tcs;

	/* Cut through switching doesn't work for preemptible priorities,
	 * so first make sure it is disabled. Also, changing the preemptible
	 * TCs affects the oversized frame dropping logic, so that needs to be
	 * re-triggered. And since tas_guard_bands_update() also implicitly
	 * calls cut_through_fwd(), we don't need to explicitly call it.
	 */
	mm->active_preemptible_tcs = val;
	ocelot->ops->tas_guard_bands_update(ocelot, port);

	dev_dbg(ocelot->dev,
		"port %d %s/%s, MM TX %s, preemptible TCs 0x%x, active 0x%x\n",
		port, phy_modes(ocelot_port->phy_mode),
		phy_speed_to_str(ocelot_port->speed),
		mm->tx_active ? "active" : "inactive", mm->preemptible_tcs,
		mm->active_preemptible_tcs);

	ocelot_rmw_rix(ocelot, QSYS_PREEMPTION_CFG_P_QUEUES(val),
		       QSYS_PREEMPTION_CFG_P_QUEUES_M,
		       QSYS_PREEMPTION_CFG, port);
}

void ocelot_port_change_fp(struct ocelot *ocelot, int port,
			   unsigned long preemptible_tcs)
{
	struct ocelot_mm_state *mm = &ocelot->mm[port];

	lockdep_assert_held(&ocelot->fwd_domain_lock);

	if (mm->preemptible_tcs == preemptible_tcs)
		return;

	mm->preemptible_tcs = preemptible_tcs;

	ocelot_port_update_active_preemptible_tcs(ocelot, port);
}

static void ocelot_mm_update_port_status(struct ocelot *ocelot, int port)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_mm_state *mm = &ocelot->mm[port];
	enum ethtool_mm_verify_status verify_status;
	u32 val, ack = 0;

	if (!mm->tx_enabled)
		return;

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
		ocelot_port_update_active_preemptible_tcs(ocelot, port);

		ack |= DEV_MM_STAT_MM_STATUS_PRMPT_ACTIVE_STICKY;
	}

	if (val & DEV_MM_STAT_MM_STATUS_UNEXP_RX_PFRM_STICKY) {
		dev_err(ocelot->dev,
			"Unexpected P-frame received on port %d while verification was unsuccessful or not yet verified\n",
			port);

		ack |= DEV_MM_STAT_MM_STATUS_UNEXP_RX_PFRM_STICKY;
	}

	if (val & DEV_MM_STAT_MM_STATUS_UNEXP_TX_PFRM_STICKY) {
		dev_err(ocelot->dev,
			"Unexpected P-frame requested to be transmitted on port %d while verification was unsuccessful or not yet verified, or MM_TX_ENA=0\n",
			port);

		ack |= DEV_MM_STAT_MM_STATUS_UNEXP_TX_PFRM_STICKY;
	}

	if (ack)
		ocelot_port_writel(ocelot_port, ack, DEV_MM_STATUS);
}

void ocelot_mm_irq(struct ocelot *ocelot)
{
	int port;

	mutex_lock(&ocelot->fwd_domain_lock);

	for (port = 0; port < ocelot->num_phys_ports; port++)
		ocelot_mm_update_port_status(ocelot, port);

	mutex_unlock(&ocelot->fwd_domain_lock);
}
EXPORT_SYMBOL_GPL(ocelot_mm_irq);

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

	mutex_lock(&ocelot->fwd_domain_lock);

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

	/* The switch will emit an IRQ when TX is disabled, to notify that it
	 * has become inactive. We optimize ocelot_mm_update_port_status() to
	 * not bother processing MM IRQs at all for ports with TX disabled,
	 * but we need to ACK this IRQ now, while mm->tx_enabled is still set,
	 * otherwise we get an IRQ storm.
	 */
	if (mm->tx_enabled && !cfg->tx_enabled) {
		ocelot_mm_update_port_status(ocelot, port);
		WARN_ON(mm->tx_active);
	}

	mm->tx_enabled = cfg->tx_enabled;

	mutex_unlock(&ocelot->fwd_domain_lock);

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

	mutex_lock(&ocelot->fwd_domain_lock);

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

	ocelot_mm_update_port_status(ocelot, port);
	state->verify_status = mm->verify_status;
	state->tx_active = mm->tx_active;

	mutex_unlock(&ocelot->fwd_domain_lock);

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
		ocelot_port = ocelot->ports[port];

		/* Update initial status variable for the
		 * verification state machine
		 */
		val = ocelot_port_readl(ocelot_port, DEV_MM_STATUS);
		mm->verify_status = ocelot_mm_verify_status(val);
	}

	return 0;
}
