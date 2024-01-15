// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019-2021 NXP
 *
 * This is an umbrella module for all network switches that are
 * register-compatible with Ocelot and that perform I/O to their host CPU
 * through an NPI (Node Processor Interface) Ethernet port.
 */
#include <uapi/linux/if_bridge.h>
#include <soc/mscc/ocelot_vcap.h>
#include <soc/mscc/ocelot_qsys.h>
#include <soc/mscc/ocelot_sys.h>
#include <soc/mscc/ocelot_dev.h>
#include <soc/mscc/ocelot_ana.h>
#include <soc/mscc/ocelot_ptp.h>
#include <soc/mscc/ocelot.h>
#include <linux/dsa/8021q.h>
#include <linux/dsa/ocelot.h>
#include <linux/platform_device.h>
#include <linux/ptp_classify.h>
#include <linux/module.h>
#include <linux/of_net.h>
#include <linux/pci.h>
#include <linux/of.h>
#include <net/pkt_sched.h>
#include <net/dsa.h>
#include "felix.h"

/* Translate the DSA database API into the ocelot switch library API,
 * which uses VID 0 for all ports that aren't part of a bridge,
 * and expects the bridge_dev to be NULL in that case.
 */
static struct net_device *felix_classify_db(struct dsa_db db)
{
	switch (db.type) {
	case DSA_DB_PORT:
	case DSA_DB_LAG:
		return NULL;
	case DSA_DB_BRIDGE:
		return db.bridge.dev;
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}
}

static int felix_cpu_port_for_conduit(struct dsa_switch *ds,
				      struct net_device *conduit)
{
	struct ocelot *ocelot = ds->priv;
	struct dsa_port *cpu_dp;
	int lag;

	if (netif_is_lag_master(conduit)) {
		mutex_lock(&ocelot->fwd_domain_lock);
		lag = ocelot_bond_get_id(ocelot, conduit);
		mutex_unlock(&ocelot->fwd_domain_lock);

		return lag;
	}

	cpu_dp = conduit->dsa_ptr;
	return cpu_dp->index;
}

/* Set up VCAP ES0 rules for pushing a tag_8021q VLAN towards the CPU such that
 * the tagger can perform RX source port identification.
 */
static int felix_tag_8021q_vlan_add_rx(struct dsa_switch *ds, int port,
				       int upstream, u16 vid)
{
	struct ocelot_vcap_filter *outer_tagging_rule;
	struct ocelot *ocelot = ds->priv;
	unsigned long cookie;
	int key_length, err;

	key_length = ocelot->vcap[VCAP_ES0].keys[VCAP_ES0_IGR_PORT].length;

	outer_tagging_rule = kzalloc(sizeof(struct ocelot_vcap_filter),
				     GFP_KERNEL);
	if (!outer_tagging_rule)
		return -ENOMEM;

	cookie = OCELOT_VCAP_ES0_TAG_8021Q_RXVLAN(ocelot, port, upstream);

	outer_tagging_rule->key_type = OCELOT_VCAP_KEY_ANY;
	outer_tagging_rule->prio = 1;
	outer_tagging_rule->id.cookie = cookie;
	outer_tagging_rule->id.tc_offload = false;
	outer_tagging_rule->block_id = VCAP_ES0;
	outer_tagging_rule->type = OCELOT_VCAP_FILTER_OFFLOAD;
	outer_tagging_rule->lookup = 0;
	outer_tagging_rule->ingress_port.value = port;
	outer_tagging_rule->ingress_port.mask = GENMASK(key_length - 1, 0);
	outer_tagging_rule->egress_port.value = upstream;
	outer_tagging_rule->egress_port.mask = GENMASK(key_length - 1, 0);
	outer_tagging_rule->action.push_outer_tag = OCELOT_ES0_TAG;
	outer_tagging_rule->action.tag_a_tpid_sel = OCELOT_TAG_TPID_SEL_8021AD;
	outer_tagging_rule->action.tag_a_vid_sel = 1;
	outer_tagging_rule->action.vid_a_val = vid;

	err = ocelot_vcap_filter_add(ocelot, outer_tagging_rule, NULL);
	if (err)
		kfree(outer_tagging_rule);

	return err;
}

static int felix_tag_8021q_vlan_del_rx(struct dsa_switch *ds, int port,
				       int upstream, u16 vid)
{
	struct ocelot_vcap_filter *outer_tagging_rule;
	struct ocelot_vcap_block *block_vcap_es0;
	struct ocelot *ocelot = ds->priv;
	unsigned long cookie;

	block_vcap_es0 = &ocelot->block[VCAP_ES0];
	cookie = OCELOT_VCAP_ES0_TAG_8021Q_RXVLAN(ocelot, port, upstream);

	outer_tagging_rule = ocelot_vcap_block_find_filter_by_id(block_vcap_es0,
								 cookie, false);
	if (!outer_tagging_rule)
		return -ENOENT;

	return ocelot_vcap_filter_del(ocelot, outer_tagging_rule);
}

/* Set up VCAP IS1 rules for stripping the tag_8021q VLAN on TX and VCAP IS2
 * rules for steering those tagged packets towards the correct destination port
 */
static int felix_tag_8021q_vlan_add_tx(struct dsa_switch *ds, int port,
				       u16 vid)
{
	struct ocelot_vcap_filter *untagging_rule, *redirect_rule;
	unsigned long cpu_ports = dsa_cpu_ports(ds);
	struct ocelot *ocelot = ds->priv;
	unsigned long cookie;
	int err;

	untagging_rule = kzalloc(sizeof(struct ocelot_vcap_filter), GFP_KERNEL);
	if (!untagging_rule)
		return -ENOMEM;

	redirect_rule = kzalloc(sizeof(struct ocelot_vcap_filter), GFP_KERNEL);
	if (!redirect_rule) {
		kfree(untagging_rule);
		return -ENOMEM;
	}

	cookie = OCELOT_VCAP_IS1_TAG_8021Q_TXVLAN(ocelot, port);

	untagging_rule->key_type = OCELOT_VCAP_KEY_ANY;
	untagging_rule->ingress_port_mask = cpu_ports;
	untagging_rule->vlan.vid.value = vid;
	untagging_rule->vlan.vid.mask = VLAN_VID_MASK;
	untagging_rule->prio = 1;
	untagging_rule->id.cookie = cookie;
	untagging_rule->id.tc_offload = false;
	untagging_rule->block_id = VCAP_IS1;
	untagging_rule->type = OCELOT_VCAP_FILTER_OFFLOAD;
	untagging_rule->lookup = 0;
	untagging_rule->action.vlan_pop_cnt_ena = true;
	untagging_rule->action.vlan_pop_cnt = 1;
	untagging_rule->action.pag_override_mask = 0xff;
	untagging_rule->action.pag_val = port;

	err = ocelot_vcap_filter_add(ocelot, untagging_rule, NULL);
	if (err) {
		kfree(untagging_rule);
		kfree(redirect_rule);
		return err;
	}

	cookie = OCELOT_VCAP_IS2_TAG_8021Q_TXVLAN(ocelot, port);

	redirect_rule->key_type = OCELOT_VCAP_KEY_ANY;
	redirect_rule->ingress_port_mask = cpu_ports;
	redirect_rule->pag = port;
	redirect_rule->prio = 1;
	redirect_rule->id.cookie = cookie;
	redirect_rule->id.tc_offload = false;
	redirect_rule->block_id = VCAP_IS2;
	redirect_rule->type = OCELOT_VCAP_FILTER_OFFLOAD;
	redirect_rule->lookup = 0;
	redirect_rule->action.mask_mode = OCELOT_MASK_MODE_REDIRECT;
	redirect_rule->action.port_mask = BIT(port);

	err = ocelot_vcap_filter_add(ocelot, redirect_rule, NULL);
	if (err) {
		ocelot_vcap_filter_del(ocelot, untagging_rule);
		kfree(redirect_rule);
		return err;
	}

	return 0;
}

static int felix_tag_8021q_vlan_del_tx(struct dsa_switch *ds, int port, u16 vid)
{
	struct ocelot_vcap_filter *untagging_rule, *redirect_rule;
	struct ocelot_vcap_block *block_vcap_is1;
	struct ocelot_vcap_block *block_vcap_is2;
	struct ocelot *ocelot = ds->priv;
	unsigned long cookie;
	int err;

	block_vcap_is1 = &ocelot->block[VCAP_IS1];
	block_vcap_is2 = &ocelot->block[VCAP_IS2];

	cookie = OCELOT_VCAP_IS1_TAG_8021Q_TXVLAN(ocelot, port);
	untagging_rule = ocelot_vcap_block_find_filter_by_id(block_vcap_is1,
							     cookie, false);
	if (!untagging_rule)
		return -ENOENT;

	err = ocelot_vcap_filter_del(ocelot, untagging_rule);
	if (err)
		return err;

	cookie = OCELOT_VCAP_IS2_TAG_8021Q_TXVLAN(ocelot, port);
	redirect_rule = ocelot_vcap_block_find_filter_by_id(block_vcap_is2,
							    cookie, false);
	if (!redirect_rule)
		return -ENOENT;

	return ocelot_vcap_filter_del(ocelot, redirect_rule);
}

static int felix_tag_8021q_vlan_add(struct dsa_switch *ds, int port, u16 vid,
				    u16 flags)
{
	struct dsa_port *cpu_dp;
	int err;

	/* tag_8021q.c assumes we are implementing this via port VLAN
	 * membership, which we aren't. So we don't need to add any VCAP filter
	 * for the CPU port.
	 */
	if (!dsa_is_user_port(ds, port))
		return 0;

	dsa_switch_for_each_cpu_port(cpu_dp, ds) {
		err = felix_tag_8021q_vlan_add_rx(ds, port, cpu_dp->index, vid);
		if (err)
			return err;
	}

	err = felix_tag_8021q_vlan_add_tx(ds, port, vid);
	if (err)
		goto add_tx_failed;

	return 0;

add_tx_failed:
	dsa_switch_for_each_cpu_port(cpu_dp, ds)
		felix_tag_8021q_vlan_del_rx(ds, port, cpu_dp->index, vid);

	return err;
}

static int felix_tag_8021q_vlan_del(struct dsa_switch *ds, int port, u16 vid)
{
	struct dsa_port *cpu_dp;
	int err;

	if (!dsa_is_user_port(ds, port))
		return 0;

	dsa_switch_for_each_cpu_port(cpu_dp, ds) {
		err = felix_tag_8021q_vlan_del_rx(ds, port, cpu_dp->index, vid);
		if (err)
			return err;
	}

	err = felix_tag_8021q_vlan_del_tx(ds, port, vid);
	if (err)
		goto del_tx_failed;

	return 0;

del_tx_failed:
	dsa_switch_for_each_cpu_port(cpu_dp, ds)
		felix_tag_8021q_vlan_add_rx(ds, port, cpu_dp->index, vid);

	return err;
}

static int felix_trap_get_cpu_port(struct dsa_switch *ds,
				   const struct ocelot_vcap_filter *trap)
{
	struct dsa_port *dp;
	int first_port;

	if (WARN_ON(!trap->ingress_port_mask))
		return -1;

	first_port = __ffs(trap->ingress_port_mask);
	dp = dsa_to_port(ds, first_port);

	return dp->cpu_dp->index;
}

/* On switches with no extraction IRQ wired, trapped packets need to be
 * replicated over Ethernet as well, otherwise we'd get no notification of
 * their arrival when using the ocelot-8021q tagging protocol.
 */
static int felix_update_trapping_destinations(struct dsa_switch *ds,
					      bool using_tag_8021q)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	struct ocelot_vcap_block *block_vcap_is2;
	struct ocelot_vcap_filter *trap;
	enum ocelot_mask_mode mask_mode;
	unsigned long port_mask;
	bool cpu_copy_ena;
	int err;

	if (!felix->info->quirk_no_xtr_irq)
		return 0;

	/* We are sure that "cpu" was found, otherwise
	 * dsa_tree_setup_default_cpu() would have failed earlier.
	 */
	block_vcap_is2 = &ocelot->block[VCAP_IS2];

	/* Make sure all traps are set up for that destination */
	list_for_each_entry(trap, &block_vcap_is2->rules, list) {
		if (!trap->is_trap)
			continue;

		/* Figure out the current trapping destination */
		if (using_tag_8021q) {
			/* Redirect to the tag_8021q CPU port. If timestamps
			 * are necessary, also copy trapped packets to the CPU
			 * port module.
			 */
			mask_mode = OCELOT_MASK_MODE_REDIRECT;
			port_mask = BIT(felix_trap_get_cpu_port(ds, trap));
			cpu_copy_ena = !!trap->take_ts;
		} else {
			/* Trap packets only to the CPU port module, which is
			 * redirected to the NPI port (the DSA CPU port)
			 */
			mask_mode = OCELOT_MASK_MODE_PERMIT_DENY;
			port_mask = 0;
			cpu_copy_ena = true;
		}

		if (trap->action.mask_mode == mask_mode &&
		    trap->action.port_mask == port_mask &&
		    trap->action.cpu_copy_ena == cpu_copy_ena)
			continue;

		trap->action.mask_mode = mask_mode;
		trap->action.port_mask = port_mask;
		trap->action.cpu_copy_ena = cpu_copy_ena;

		err = ocelot_vcap_filter_replace(ocelot, trap);
		if (err)
			return err;
	}

	return 0;
}

/* The CPU port module is connected to the Node Processor Interface (NPI). This
 * is the mode through which frames can be injected from and extracted to an
 * external CPU, over Ethernet. In NXP SoCs, the "external CPU" is the ARM CPU
 * running Linux, and this forms a DSA setup together with the enetc or fman
 * DSA conduit.
 */
static void felix_npi_port_init(struct ocelot *ocelot, int port)
{
	ocelot->npi = port;

	ocelot_write(ocelot, QSYS_EXT_CPU_CFG_EXT_CPUQ_MSK_M |
		     QSYS_EXT_CPU_CFG_EXT_CPU_PORT(port),
		     QSYS_EXT_CPU_CFG);

	/* NPI port Injection/Extraction configuration */
	ocelot_fields_write(ocelot, port, SYS_PORT_MODE_INCL_XTR_HDR,
			    ocelot->npi_xtr_prefix);
	ocelot_fields_write(ocelot, port, SYS_PORT_MODE_INCL_INJ_HDR,
			    ocelot->npi_inj_prefix);

	/* Disable transmission of pause frames */
	ocelot_fields_write(ocelot, port, SYS_PAUSE_CFG_PAUSE_ENA, 0);
}

static void felix_npi_port_deinit(struct ocelot *ocelot, int port)
{
	/* Restore hardware defaults */
	int unused_port = ocelot->num_phys_ports + 2;

	ocelot->npi = -1;

	ocelot_write(ocelot, QSYS_EXT_CPU_CFG_EXT_CPU_PORT(unused_port),
		     QSYS_EXT_CPU_CFG);

	ocelot_fields_write(ocelot, port, SYS_PORT_MODE_INCL_XTR_HDR,
			    OCELOT_TAG_PREFIX_DISABLED);
	ocelot_fields_write(ocelot, port, SYS_PORT_MODE_INCL_INJ_HDR,
			    OCELOT_TAG_PREFIX_DISABLED);

	/* Enable transmission of pause frames */
	ocelot_fields_write(ocelot, port, SYS_PAUSE_CFG_PAUSE_ENA, 1);
}

static int felix_tag_npi_setup(struct dsa_switch *ds)
{
	struct dsa_port *dp, *first_cpu_dp = NULL;
	struct ocelot *ocelot = ds->priv;

	dsa_switch_for_each_user_port(dp, ds) {
		if (first_cpu_dp && dp->cpu_dp != first_cpu_dp) {
			dev_err(ds->dev, "Multiple NPI ports not supported\n");
			return -EINVAL;
		}

		first_cpu_dp = dp->cpu_dp;
	}

	if (!first_cpu_dp)
		return -EINVAL;

	felix_npi_port_init(ocelot, first_cpu_dp->index);

	return 0;
}

static void felix_tag_npi_teardown(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;

	felix_npi_port_deinit(ocelot, ocelot->npi);
}

static unsigned long felix_tag_npi_get_host_fwd_mask(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;

	return BIT(ocelot->num_phys_ports);
}

static int felix_tag_npi_change_conduit(struct dsa_switch *ds, int port,
					struct net_device *conduit,
					struct netlink_ext_ack *extack)
{
	struct dsa_port *dp = dsa_to_port(ds, port), *other_dp;
	struct ocelot *ocelot = ds->priv;

	if (netif_is_lag_master(conduit)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "LAG DSA conduit only supported using ocelot-8021q");
		return -EOPNOTSUPP;
	}

	/* Changing the NPI port breaks user ports still assigned to the old
	 * one, so only allow it while they're down, and don't allow them to
	 * come back up until they're all changed to the new one.
	 */
	dsa_switch_for_each_user_port(other_dp, ds) {
		struct net_device *user = other_dp->user;

		if (other_dp != dp && (user->flags & IFF_UP) &&
		    dsa_port_to_conduit(other_dp) != conduit) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Cannot change while old conduit still has users");
			return -EOPNOTSUPP;
		}
	}

	felix_npi_port_deinit(ocelot, ocelot->npi);
	felix_npi_port_init(ocelot, felix_cpu_port_for_conduit(ds, conduit));

	return 0;
}

/* Alternatively to using the NPI functionality, that same hardware MAC
 * connected internally to the enetc or fman DSA conduit can be configured to
 * use the software-defined tag_8021q frame format. As far as the hardware is
 * concerned, it thinks it is a "dumb switch" - the queues of the CPU port
 * module are now disconnected from it, but can still be accessed through
 * register-based MMIO.
 */
static const struct felix_tag_proto_ops felix_tag_npi_proto_ops = {
	.setup			= felix_tag_npi_setup,
	.teardown		= felix_tag_npi_teardown,
	.get_host_fwd_mask	= felix_tag_npi_get_host_fwd_mask,
	.change_conduit		= felix_tag_npi_change_conduit,
};

static int felix_tag_8021q_setup(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;
	struct dsa_port *dp;
	int err;

	err = dsa_tag_8021q_register(ds, htons(ETH_P_8021AD));
	if (err)
		return err;

	dsa_switch_for_each_cpu_port(dp, ds)
		ocelot_port_setup_dsa_8021q_cpu(ocelot, dp->index);

	dsa_switch_for_each_user_port(dp, ds)
		ocelot_port_assign_dsa_8021q_cpu(ocelot, dp->index,
						 dp->cpu_dp->index);

	dsa_switch_for_each_available_port(dp, ds)
		/* This overwrites ocelot_init():
		 * Do not forward BPDU frames to the CPU port module,
		 * for 2 reasons:
		 * - When these packets are injected from the tag_8021q
		 *   CPU port, we want them to go out, not loop back
		 *   into the system.
		 * - STP traffic ingressing on a user port should go to
		 *   the tag_8021q CPU port, not to the hardware CPU
		 *   port module.
		 */
		ocelot_write_gix(ocelot,
				 ANA_PORT_CPU_FWD_BPDU_CFG_BPDU_REDIR_ENA(0),
				 ANA_PORT_CPU_FWD_BPDU_CFG, dp->index);

	/* The ownership of the CPU port module's queues might have just been
	 * transferred to the tag_8021q tagger from the NPI-based tagger.
	 * So there might still be all sorts of crap in the queues. On the
	 * other hand, the MMIO-based matching of PTP frames is very brittle,
	 * so we need to be careful that there are no extra frames to be
	 * dequeued over MMIO, since we would never know to discard them.
	 */
	ocelot_drain_cpu_queue(ocelot, 0);

	return 0;
}

static void felix_tag_8021q_teardown(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;
	struct dsa_port *dp;

	dsa_switch_for_each_available_port(dp, ds)
		/* Restore the logic from ocelot_init:
		 * do not forward BPDU frames to the front ports.
		 */
		ocelot_write_gix(ocelot,
				 ANA_PORT_CPU_FWD_BPDU_CFG_BPDU_REDIR_ENA(0xffff),
				 ANA_PORT_CPU_FWD_BPDU_CFG,
				 dp->index);

	dsa_switch_for_each_user_port(dp, ds)
		ocelot_port_unassign_dsa_8021q_cpu(ocelot, dp->index);

	dsa_switch_for_each_cpu_port(dp, ds)
		ocelot_port_teardown_dsa_8021q_cpu(ocelot, dp->index);

	dsa_tag_8021q_unregister(ds);
}

static unsigned long felix_tag_8021q_get_host_fwd_mask(struct dsa_switch *ds)
{
	return dsa_cpu_ports(ds);
}

static int felix_tag_8021q_change_conduit(struct dsa_switch *ds, int port,
					  struct net_device *conduit,
					  struct netlink_ext_ack *extack)
{
	int cpu = felix_cpu_port_for_conduit(ds, conduit);
	struct ocelot *ocelot = ds->priv;

	ocelot_port_unassign_dsa_8021q_cpu(ocelot, port);
	ocelot_port_assign_dsa_8021q_cpu(ocelot, port, cpu);

	return felix_update_trapping_destinations(ds, true);
}

static const struct felix_tag_proto_ops felix_tag_8021q_proto_ops = {
	.setup			= felix_tag_8021q_setup,
	.teardown		= felix_tag_8021q_teardown,
	.get_host_fwd_mask	= felix_tag_8021q_get_host_fwd_mask,
	.change_conduit		= felix_tag_8021q_change_conduit,
};

static void felix_set_host_flood(struct dsa_switch *ds, unsigned long mask,
				 bool uc, bool mc, bool bc)
{
	struct ocelot *ocelot = ds->priv;
	unsigned long val;

	val = uc ? mask : 0;
	ocelot_rmw_rix(ocelot, val, mask, ANA_PGID_PGID, PGID_UC);

	val = mc ? mask : 0;
	ocelot_rmw_rix(ocelot, val, mask, ANA_PGID_PGID, PGID_MC);
	ocelot_rmw_rix(ocelot, val, mask, ANA_PGID_PGID, PGID_MCIPV4);
	ocelot_rmw_rix(ocelot, val, mask, ANA_PGID_PGID, PGID_MCIPV6);

	val = bc ? mask : 0;
	ocelot_rmw_rix(ocelot, val, mask, ANA_PGID_PGID, PGID_BC);
}

static void
felix_migrate_host_flood(struct dsa_switch *ds,
			 const struct felix_tag_proto_ops *proto_ops,
			 const struct felix_tag_proto_ops *old_proto_ops)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	unsigned long mask;

	if (old_proto_ops) {
		mask = old_proto_ops->get_host_fwd_mask(ds);
		felix_set_host_flood(ds, mask, false, false, false);
	}

	mask = proto_ops->get_host_fwd_mask(ds);
	felix_set_host_flood(ds, mask, !!felix->host_flood_uc_mask,
			     !!felix->host_flood_mc_mask, true);
}

static int felix_migrate_mdbs(struct dsa_switch *ds,
			      const struct felix_tag_proto_ops *proto_ops,
			      const struct felix_tag_proto_ops *old_proto_ops)
{
	struct ocelot *ocelot = ds->priv;
	unsigned long from, to;

	if (!old_proto_ops)
		return 0;

	from = old_proto_ops->get_host_fwd_mask(ds);
	to = proto_ops->get_host_fwd_mask(ds);

	return ocelot_migrate_mdbs(ocelot, from, to);
}

/* Configure the shared hardware resources for a transition between
 * @old_proto_ops and @proto_ops.
 * Manual migration is needed because as far as DSA is concerned, no change of
 * the CPU port is taking place here, just of the tagging protocol.
 */
static int
felix_tag_proto_setup_shared(struct dsa_switch *ds,
			     const struct felix_tag_proto_ops *proto_ops,
			     const struct felix_tag_proto_ops *old_proto_ops)
{
	bool using_tag_8021q = (proto_ops == &felix_tag_8021q_proto_ops);
	int err;

	err = felix_migrate_mdbs(ds, proto_ops, old_proto_ops);
	if (err)
		return err;

	felix_update_trapping_destinations(ds, using_tag_8021q);

	felix_migrate_host_flood(ds, proto_ops, old_proto_ops);

	return 0;
}

/* This always leaves the switch in a consistent state, because although the
 * tag_8021q setup can fail, the NPI setup can't. So either the change is made,
 * or the restoration is guaranteed to work.
 */
static int felix_change_tag_protocol(struct dsa_switch *ds,
				     enum dsa_tag_protocol proto)
{
	const struct felix_tag_proto_ops *old_proto_ops, *proto_ops;
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	int err;

	switch (proto) {
	case DSA_TAG_PROTO_SEVILLE:
	case DSA_TAG_PROTO_OCELOT:
		proto_ops = &felix_tag_npi_proto_ops;
		break;
	case DSA_TAG_PROTO_OCELOT_8021Q:
		proto_ops = &felix_tag_8021q_proto_ops;
		break;
	default:
		return -EPROTONOSUPPORT;
	}

	old_proto_ops = felix->tag_proto_ops;

	if (proto_ops == old_proto_ops)
		return 0;

	err = proto_ops->setup(ds);
	if (err)
		goto setup_failed;

	err = felix_tag_proto_setup_shared(ds, proto_ops, old_proto_ops);
	if (err)
		goto setup_shared_failed;

	if (old_proto_ops)
		old_proto_ops->teardown(ds);

	felix->tag_proto_ops = proto_ops;
	felix->tag_proto = proto;

	return 0;

setup_shared_failed:
	proto_ops->teardown(ds);
setup_failed:
	return err;
}

static enum dsa_tag_protocol felix_get_tag_protocol(struct dsa_switch *ds,
						    int port,
						    enum dsa_tag_protocol mp)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);

	return felix->tag_proto;
}

static void felix_port_set_host_flood(struct dsa_switch *ds, int port,
				      bool uc, bool mc)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	unsigned long mask;

	if (uc)
		felix->host_flood_uc_mask |= BIT(port);
	else
		felix->host_flood_uc_mask &= ~BIT(port);

	if (mc)
		felix->host_flood_mc_mask |= BIT(port);
	else
		felix->host_flood_mc_mask &= ~BIT(port);

	mask = felix->tag_proto_ops->get_host_fwd_mask(ds);
	felix_set_host_flood(ds, mask, !!felix->host_flood_uc_mask,
			     !!felix->host_flood_mc_mask, true);
}

static int felix_port_change_conduit(struct dsa_switch *ds, int port,
				     struct net_device *conduit,
				     struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);

	return felix->tag_proto_ops->change_conduit(ds, port, conduit, extack);
}

static int felix_set_ageing_time(struct dsa_switch *ds,
				 unsigned int ageing_time)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_set_ageing_time(ocelot, ageing_time);

	return 0;
}

static void felix_port_fast_age(struct dsa_switch *ds, int port)
{
	struct ocelot *ocelot = ds->priv;
	int err;

	err = ocelot_mact_flush(ocelot, port);
	if (err)
		dev_err(ds->dev, "Flushing MAC table on port %d returned %pe\n",
			port, ERR_PTR(err));
}

static int felix_fdb_dump(struct dsa_switch *ds, int port,
			  dsa_fdb_dump_cb_t *cb, void *data)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_fdb_dump(ocelot, port, cb, data);
}

static int felix_fdb_add(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid,
			 struct dsa_db db)
{
	struct net_device *bridge_dev = felix_classify_db(db);
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct ocelot *ocelot = ds->priv;

	if (IS_ERR(bridge_dev))
		return PTR_ERR(bridge_dev);

	if (dsa_port_is_cpu(dp) && !bridge_dev &&
	    dsa_fdb_present_in_other_db(ds, port, addr, vid, db))
		return 0;

	if (dsa_port_is_cpu(dp))
		port = PGID_CPU;

	return ocelot_fdb_add(ocelot, port, addr, vid, bridge_dev);
}

static int felix_fdb_del(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid,
			 struct dsa_db db)
{
	struct net_device *bridge_dev = felix_classify_db(db);
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct ocelot *ocelot = ds->priv;

	if (IS_ERR(bridge_dev))
		return PTR_ERR(bridge_dev);

	if (dsa_port_is_cpu(dp) && !bridge_dev &&
	    dsa_fdb_present_in_other_db(ds, port, addr, vid, db))
		return 0;

	if (dsa_port_is_cpu(dp))
		port = PGID_CPU;

	return ocelot_fdb_del(ocelot, port, addr, vid, bridge_dev);
}

static int felix_lag_fdb_add(struct dsa_switch *ds, struct dsa_lag lag,
			     const unsigned char *addr, u16 vid,
			     struct dsa_db db)
{
	struct net_device *bridge_dev = felix_classify_db(db);
	struct ocelot *ocelot = ds->priv;

	if (IS_ERR(bridge_dev))
		return PTR_ERR(bridge_dev);

	return ocelot_lag_fdb_add(ocelot, lag.dev, addr, vid, bridge_dev);
}

static int felix_lag_fdb_del(struct dsa_switch *ds, struct dsa_lag lag,
			     const unsigned char *addr, u16 vid,
			     struct dsa_db db)
{
	struct net_device *bridge_dev = felix_classify_db(db);
	struct ocelot *ocelot = ds->priv;

	if (IS_ERR(bridge_dev))
		return PTR_ERR(bridge_dev);

	return ocelot_lag_fdb_del(ocelot, lag.dev, addr, vid, bridge_dev);
}

static int felix_mdb_add(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb,
			 struct dsa_db db)
{
	struct net_device *bridge_dev = felix_classify_db(db);
	struct ocelot *ocelot = ds->priv;

	if (IS_ERR(bridge_dev))
		return PTR_ERR(bridge_dev);

	if (dsa_is_cpu_port(ds, port) && !bridge_dev &&
	    dsa_mdb_present_in_other_db(ds, port, mdb, db))
		return 0;

	if (port == ocelot->npi)
		port = ocelot->num_phys_ports;

	return ocelot_port_mdb_add(ocelot, port, mdb, bridge_dev);
}

static int felix_mdb_del(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb,
			 struct dsa_db db)
{
	struct net_device *bridge_dev = felix_classify_db(db);
	struct ocelot *ocelot = ds->priv;

	if (IS_ERR(bridge_dev))
		return PTR_ERR(bridge_dev);

	if (dsa_is_cpu_port(ds, port) && !bridge_dev &&
	    dsa_mdb_present_in_other_db(ds, port, mdb, db))
		return 0;

	if (port == ocelot->npi)
		port = ocelot->num_phys_ports;

	return ocelot_port_mdb_del(ocelot, port, mdb, bridge_dev);
}

static void felix_bridge_stp_state_set(struct dsa_switch *ds, int port,
				       u8 state)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_bridge_stp_state_set(ocelot, port, state);
}

static int felix_pre_bridge_flags(struct dsa_switch *ds, int port,
				  struct switchdev_brport_flags val,
				  struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_pre_bridge_flags(ocelot, port, val);
}

static int felix_bridge_flags(struct dsa_switch *ds, int port,
			      struct switchdev_brport_flags val,
			      struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;

	if (port == ocelot->npi)
		port = ocelot->num_phys_ports;

	ocelot_port_bridge_flags(ocelot, port, val);

	return 0;
}

static int felix_bridge_join(struct dsa_switch *ds, int port,
			     struct dsa_bridge bridge, bool *tx_fwd_offload,
			     struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_bridge_join(ocelot, port, bridge.dev, bridge.num,
				       extack);
}

static void felix_bridge_leave(struct dsa_switch *ds, int port,
			       struct dsa_bridge bridge)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_bridge_leave(ocelot, port, bridge.dev);
}

static int felix_lag_join(struct dsa_switch *ds, int port,
			  struct dsa_lag lag,
			  struct netdev_lag_upper_info *info,
			  struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;
	int err;

	err = ocelot_port_lag_join(ocelot, port, lag.dev, info, extack);
	if (err)
		return err;

	/* Update the logical LAG port that serves as tag_8021q CPU port */
	if (!dsa_is_cpu_port(ds, port))
		return 0;

	return felix_port_change_conduit(ds, port, lag.dev, extack);
}

static int felix_lag_leave(struct dsa_switch *ds, int port,
			   struct dsa_lag lag)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_lag_leave(ocelot, port, lag.dev);

	/* Update the logical LAG port that serves as tag_8021q CPU port */
	if (!dsa_is_cpu_port(ds, port))
		return 0;

	return felix_port_change_conduit(ds, port, lag.dev, NULL);
}

static int felix_lag_change(struct dsa_switch *ds, int port)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct ocelot *ocelot = ds->priv;

	ocelot_port_lag_change(ocelot, port, dp->lag_tx_enabled);

	return 0;
}

static int felix_vlan_prepare(struct dsa_switch *ds, int port,
			      const struct switchdev_obj_port_vlan *vlan,
			      struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;
	u16 flags = vlan->flags;

	/* Ocelot switches copy frames as-is to the CPU, so the flags:
	 * egress-untagged or not, pvid or not, make no difference. This
	 * behavior is already better than what DSA just tries to approximate
	 * when it installs the VLAN with the same flags on the CPU port.
	 * Just accept any configuration, and don't let ocelot deny installing
	 * multiple native VLANs on the NPI port, because the switch doesn't
	 * look at the port tag settings towards the NPI interface anyway.
	 */
	if (port == ocelot->npi)
		return 0;

	return ocelot_vlan_prepare(ocelot, port, vlan->vid,
				   flags & BRIDGE_VLAN_INFO_PVID,
				   flags & BRIDGE_VLAN_INFO_UNTAGGED,
				   extack);
}

static int felix_vlan_filtering(struct dsa_switch *ds, int port, bool enabled,
				struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_vlan_filtering(ocelot, port, enabled, extack);
}

static int felix_vlan_add(struct dsa_switch *ds, int port,
			  const struct switchdev_obj_port_vlan *vlan,
			  struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;
	u16 flags = vlan->flags;
	int err;

	err = felix_vlan_prepare(ds, port, vlan, extack);
	if (err)
		return err;

	return ocelot_vlan_add(ocelot, port, vlan->vid,
			       flags & BRIDGE_VLAN_INFO_PVID,
			       flags & BRIDGE_VLAN_INFO_UNTAGGED);
}

static int felix_vlan_del(struct dsa_switch *ds, int port,
			  const struct switchdev_obj_port_vlan *vlan)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_vlan_del(ocelot, port, vlan->vid);
}

static void felix_phylink_get_caps(struct dsa_switch *ds, int port,
				   struct phylink_config *config)
{
	struct ocelot *ocelot = ds->priv;

	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
				   MAC_10 | MAC_100 | MAC_1000FD |
				   MAC_2500FD;

	__set_bit(ocelot->ports[port]->phy_mode,
		  config->supported_interfaces);
}

static void felix_phylink_mac_config(struct dsa_switch *ds, int port,
				     unsigned int mode,
				     const struct phylink_link_state *state)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);

	if (felix->info->phylink_mac_config)
		felix->info->phylink_mac_config(ocelot, port, mode, state);
}

static struct phylink_pcs *felix_phylink_mac_select_pcs(struct dsa_switch *ds,
							int port,
							phy_interface_t iface)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	struct phylink_pcs *pcs = NULL;

	if (felix->pcs && felix->pcs[port])
		pcs = felix->pcs[port];

	return pcs;
}

static void felix_phylink_mac_link_down(struct dsa_switch *ds, int port,
					unsigned int link_an_mode,
					phy_interface_t interface)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix;

	felix = ocelot_to_felix(ocelot);

	ocelot_phylink_mac_link_down(ocelot, port, link_an_mode, interface,
				     felix->info->quirks);
}

static void felix_phylink_mac_link_up(struct dsa_switch *ds, int port,
				      unsigned int link_an_mode,
				      phy_interface_t interface,
				      struct phy_device *phydev,
				      int speed, int duplex,
				      bool tx_pause, bool rx_pause)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);

	ocelot_phylink_mac_link_up(ocelot, port, phydev, link_an_mode,
				   interface, speed, duplex, tx_pause, rx_pause,
				   felix->info->quirks);

	if (felix->info->port_sched_speed_set)
		felix->info->port_sched_speed_set(ocelot, port, speed);
}

static int felix_port_enable(struct dsa_switch *ds, int port,
			     struct phy_device *phydev)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct ocelot *ocelot = ds->priv;

	if (!dsa_port_is_user(dp))
		return 0;

	if (ocelot->npi >= 0) {
		struct net_device *conduit = dsa_port_to_conduit(dp);

		if (felix_cpu_port_for_conduit(ds, conduit) != ocelot->npi) {
			dev_err(ds->dev, "Multiple conduits are not allowed\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void felix_port_qos_map_init(struct ocelot *ocelot, int port)
{
	int i;

	ocelot_rmw_gix(ocelot,
		       ANA_PORT_QOS_CFG_QOS_PCP_ENA,
		       ANA_PORT_QOS_CFG_QOS_PCP_ENA,
		       ANA_PORT_QOS_CFG,
		       port);

	for (i = 0; i < OCELOT_NUM_TC * 2; i++) {
		ocelot_rmw_ix(ocelot,
			      (ANA_PORT_PCP_DEI_MAP_DP_PCP_DEI_VAL & i) |
			      ANA_PORT_PCP_DEI_MAP_QOS_PCP_DEI_VAL(i),
			      ANA_PORT_PCP_DEI_MAP_DP_PCP_DEI_VAL |
			      ANA_PORT_PCP_DEI_MAP_QOS_PCP_DEI_VAL_M,
			      ANA_PORT_PCP_DEI_MAP,
			      port, i);
	}
}

static void felix_get_stats64(struct dsa_switch *ds, int port,
			      struct rtnl_link_stats64 *stats)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_get_stats64(ocelot, port, stats);
}

static void felix_get_pause_stats(struct dsa_switch *ds, int port,
				  struct ethtool_pause_stats *pause_stats)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_get_pause_stats(ocelot, port, pause_stats);
}

static void felix_get_rmon_stats(struct dsa_switch *ds, int port,
				 struct ethtool_rmon_stats *rmon_stats,
				 const struct ethtool_rmon_hist_range **ranges)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_get_rmon_stats(ocelot, port, rmon_stats, ranges);
}

static void felix_get_eth_ctrl_stats(struct dsa_switch *ds, int port,
				     struct ethtool_eth_ctrl_stats *ctrl_stats)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_get_eth_ctrl_stats(ocelot, port, ctrl_stats);
}

static void felix_get_eth_mac_stats(struct dsa_switch *ds, int port,
				    struct ethtool_eth_mac_stats *mac_stats)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_get_eth_mac_stats(ocelot, port, mac_stats);
}

static void felix_get_eth_phy_stats(struct dsa_switch *ds, int port,
				    struct ethtool_eth_phy_stats *phy_stats)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_get_eth_phy_stats(ocelot, port, phy_stats);
}

static void felix_get_strings(struct dsa_switch *ds, int port,
			      u32 stringset, u8 *data)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_strings(ocelot, port, stringset, data);
}

static void felix_get_ethtool_stats(struct dsa_switch *ds, int port, u64 *data)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_get_ethtool_stats(ocelot, port, data);
}

static int felix_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_sset_count(ocelot, port, sset);
}

static int felix_get_ts_info(struct dsa_switch *ds, int port,
			     struct ethtool_ts_info *info)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_ts_info(ocelot, port, info);
}

static const u32 felix_phy_match_table[PHY_INTERFACE_MODE_MAX] = {
	[PHY_INTERFACE_MODE_INTERNAL] = OCELOT_PORT_MODE_INTERNAL,
	[PHY_INTERFACE_MODE_SGMII] = OCELOT_PORT_MODE_SGMII,
	[PHY_INTERFACE_MODE_QSGMII] = OCELOT_PORT_MODE_QSGMII,
	[PHY_INTERFACE_MODE_USXGMII] = OCELOT_PORT_MODE_USXGMII,
	[PHY_INTERFACE_MODE_1000BASEX] = OCELOT_PORT_MODE_1000BASEX,
	[PHY_INTERFACE_MODE_2500BASEX] = OCELOT_PORT_MODE_2500BASEX,
};

static int felix_validate_phy_mode(struct felix *felix, int port,
				   phy_interface_t phy_mode)
{
	u32 modes = felix->info->port_modes[port];

	if (felix_phy_match_table[phy_mode] & modes)
		return 0;
	return -EOPNOTSUPP;
}

static int felix_parse_ports_node(struct felix *felix,
				  struct device_node *ports_node,
				  phy_interface_t *port_phy_modes)
{
	struct device *dev = felix->ocelot.dev;
	struct device_node *child;

	for_each_available_child_of_node(ports_node, child) {
		phy_interface_t phy_mode;
		u32 port;
		int err;

		/* Get switch port number from DT */
		if (of_property_read_u32(child, "reg", &port) < 0) {
			dev_err(dev, "Port number not defined in device tree "
				"(property \"reg\")\n");
			of_node_put(child);
			return -ENODEV;
		}

		/* Get PHY mode from DT */
		err = of_get_phy_mode(child, &phy_mode);
		if (err) {
			dev_err(dev, "Failed to read phy-mode or "
				"phy-interface-type property for port %d\n",
				port);
			of_node_put(child);
			return -ENODEV;
		}

		err = felix_validate_phy_mode(felix, port, phy_mode);
		if (err < 0) {
			dev_info(dev, "Unsupported PHY mode %s on port %d\n",
				 phy_modes(phy_mode), port);

			/* Leave port_phy_modes[port] = 0, which is also
			 * PHY_INTERFACE_MODE_NA. This will perform a
			 * best-effort to bring up as many ports as possible.
			 */
			continue;
		}

		port_phy_modes[port] = phy_mode;
	}

	return 0;
}

static int felix_parse_dt(struct felix *felix, phy_interface_t *port_phy_modes)
{
	struct device *dev = felix->ocelot.dev;
	struct device_node *switch_node;
	struct device_node *ports_node;
	int err;

	switch_node = dev->of_node;

	ports_node = of_get_child_by_name(switch_node, "ports");
	if (!ports_node)
		ports_node = of_get_child_by_name(switch_node, "ethernet-ports");
	if (!ports_node) {
		dev_err(dev, "Incorrect bindings: absent \"ports\" or \"ethernet-ports\" node\n");
		return -ENODEV;
	}

	err = felix_parse_ports_node(felix, ports_node, port_phy_modes);
	of_node_put(ports_node);

	return err;
}

static struct regmap *felix_request_regmap_by_name(struct felix *felix,
						   const char *resource_name)
{
	struct ocelot *ocelot = &felix->ocelot;
	struct resource res;
	int i;

	/* In an MFD configuration, regmaps are registered directly to the
	 * parent device before the child devices are probed, so there is no
	 * need to initialize a new one.
	 */
	if (!felix->info->resources)
		return dev_get_regmap(ocelot->dev->parent, resource_name);

	for (i = 0; i < felix->info->num_resources; i++) {
		if (strcmp(resource_name, felix->info->resources[i].name))
			continue;

		memcpy(&res, &felix->info->resources[i], sizeof(res));
		res.start += felix->switch_base;
		res.end += felix->switch_base;

		return ocelot_regmap_init(ocelot, &res);
	}

	return ERR_PTR(-ENOENT);
}

static struct regmap *felix_request_regmap(struct felix *felix,
					   enum ocelot_target target)
{
	const char *resource_name = felix->info->resource_names[target];

	/* If the driver didn't provide a resource name for the target,
	 * the resource is optional.
	 */
	if (!resource_name)
		return NULL;

	return felix_request_regmap_by_name(felix, resource_name);
}

static struct regmap *felix_request_port_regmap(struct felix *felix, int port)
{
	char resource_name[32];

	sprintf(resource_name, "port%d", port);

	return felix_request_regmap_by_name(felix, resource_name);
}

static int felix_init_structs(struct felix *felix, int num_phys_ports)
{
	struct ocelot *ocelot = &felix->ocelot;
	phy_interface_t *port_phy_modes;
	struct regmap *target;
	int port, i, err;

	ocelot->num_phys_ports = num_phys_ports;
	ocelot->ports = devm_kcalloc(ocelot->dev, num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);
	if (!ocelot->ports)
		return -ENOMEM;

	ocelot->map		= felix->info->map;
	ocelot->num_mact_rows	= felix->info->num_mact_rows;
	ocelot->vcap		= felix->info->vcap;
	ocelot->vcap_pol.base	= felix->info->vcap_pol_base;
	ocelot->vcap_pol.max	= felix->info->vcap_pol_max;
	ocelot->vcap_pol.base2	= felix->info->vcap_pol_base2;
	ocelot->vcap_pol.max2	= felix->info->vcap_pol_max2;
	ocelot->ops		= felix->info->ops;
	ocelot->npi_inj_prefix	= OCELOT_TAG_PREFIX_SHORT;
	ocelot->npi_xtr_prefix	= OCELOT_TAG_PREFIX_SHORT;
	ocelot->devlink		= felix->ds->devlink;

	port_phy_modes = kcalloc(num_phys_ports, sizeof(phy_interface_t),
				 GFP_KERNEL);
	if (!port_phy_modes)
		return -ENOMEM;

	err = felix_parse_dt(felix, port_phy_modes);
	if (err) {
		kfree(port_phy_modes);
		return err;
	}

	for (i = 0; i < TARGET_MAX; i++) {
		target = felix_request_regmap(felix, i);
		if (IS_ERR(target)) {
			dev_err(ocelot->dev,
				"Failed to map device memory space: %pe\n",
				target);
			kfree(port_phy_modes);
			return PTR_ERR(target);
		}

		ocelot->targets[i] = target;
	}

	err = ocelot_regfields_init(ocelot, felix->info->regfields);
	if (err) {
		dev_err(ocelot->dev, "failed to init reg fields map\n");
		kfree(port_phy_modes);
		return err;
	}

	for (port = 0; port < num_phys_ports; port++) {
		struct ocelot_port *ocelot_port;

		ocelot_port = devm_kzalloc(ocelot->dev,
					   sizeof(struct ocelot_port),
					   GFP_KERNEL);
		if (!ocelot_port) {
			dev_err(ocelot->dev,
				"failed to allocate port memory\n");
			kfree(port_phy_modes);
			return -ENOMEM;
		}

		target = felix_request_port_regmap(felix, port);
		if (IS_ERR(target)) {
			dev_err(ocelot->dev,
				"Failed to map memory space for port %d: %pe\n",
				port, target);
			kfree(port_phy_modes);
			return PTR_ERR(target);
		}

		ocelot_port->phy_mode = port_phy_modes[port];
		ocelot_port->ocelot = ocelot;
		ocelot_port->target = target;
		ocelot_port->index = port;
		ocelot->ports[port] = ocelot_port;
	}

	kfree(port_phy_modes);

	if (felix->info->mdio_bus_alloc) {
		err = felix->info->mdio_bus_alloc(ocelot);
		if (err < 0)
			return err;
	}

	return 0;
}

static void ocelot_port_purge_txtstamp_skb(struct ocelot *ocelot, int port,
					   struct sk_buff *skb)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct sk_buff *clone = OCELOT_SKB_CB(skb)->clone;
	struct sk_buff *skb_match = NULL, *skb_tmp;
	unsigned long flags;

	if (!clone)
		return;

	spin_lock_irqsave(&ocelot_port->tx_skbs.lock, flags);

	skb_queue_walk_safe(&ocelot_port->tx_skbs, skb, skb_tmp) {
		if (skb != clone)
			continue;
		__skb_unlink(skb, &ocelot_port->tx_skbs);
		skb_match = skb;
		break;
	}

	spin_unlock_irqrestore(&ocelot_port->tx_skbs.lock, flags);

	WARN_ONCE(!skb_match,
		  "Could not find skb clone in TX timestamping list\n");
}

#define work_to_xmit_work(w) \
		container_of((w), struct felix_deferred_xmit_work, work)

static void felix_port_deferred_xmit(struct kthread_work *work)
{
	struct felix_deferred_xmit_work *xmit_work = work_to_xmit_work(work);
	struct dsa_switch *ds = xmit_work->dp->ds;
	struct sk_buff *skb = xmit_work->skb;
	u32 rew_op = ocelot_ptp_rew_op(skb);
	struct ocelot *ocelot = ds->priv;
	int port = xmit_work->dp->index;
	int retries = 10;

	do {
		if (ocelot_can_inject(ocelot, 0))
			break;

		cpu_relax();
	} while (--retries);

	if (!retries) {
		dev_err(ocelot->dev, "port %d failed to inject skb\n",
			port);
		ocelot_port_purge_txtstamp_skb(ocelot, port, skb);
		kfree_skb(skb);
		return;
	}

	ocelot_port_inject_frame(ocelot, port, 0, rew_op, skb);

	consume_skb(skb);
	kfree(xmit_work);
}

static int felix_connect_tag_protocol(struct dsa_switch *ds,
				      enum dsa_tag_protocol proto)
{
	struct ocelot_8021q_tagger_data *tagger_data;

	switch (proto) {
	case DSA_TAG_PROTO_OCELOT_8021Q:
		tagger_data = ocelot_8021q_tagger_data(ds);
		tagger_data->xmit_work_fn = felix_port_deferred_xmit;
		return 0;
	case DSA_TAG_PROTO_OCELOT:
	case DSA_TAG_PROTO_SEVILLE:
		return 0;
	default:
		return -EPROTONOSUPPORT;
	}
}

static int felix_setup(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	struct dsa_port *dp;
	int err;

	err = felix_init_structs(felix, ds->num_ports);
	if (err)
		return err;

	if (ocelot->targets[HSIO])
		ocelot_pll5_init(ocelot);

	err = ocelot_init(ocelot);
	if (err)
		goto out_mdiobus_free;

	if (ocelot->ptp) {
		err = ocelot_init_timestamp(ocelot, felix->info->ptp_caps);
		if (err) {
			dev_err(ocelot->dev,
				"Timestamp initialization failed\n");
			ocelot->ptp = 0;
		}
	}

	dsa_switch_for_each_available_port(dp, ds) {
		ocelot_init_port(ocelot, dp->index);

		if (felix->info->configure_serdes)
			felix->info->configure_serdes(ocelot, dp->index,
						      dp->dn);

		/* Set the default QoS Classification based on PCP and DEI
		 * bits of vlan tag.
		 */
		felix_port_qos_map_init(ocelot, dp->index);
	}

	err = ocelot_devlink_sb_register(ocelot);
	if (err)
		goto out_deinit_ports;

	/* The initial tag protocol is NPI which won't fail during initial
	 * setup, there's no real point in checking for errors.
	 */
	felix_change_tag_protocol(ds, felix->tag_proto);

	ds->mtu_enforcement_ingress = true;
	ds->assisted_learning_on_cpu_port = true;
	ds->fdb_isolation = true;
	ds->max_num_bridges = ds->num_ports;

	return 0;

out_deinit_ports:
	dsa_switch_for_each_available_port(dp, ds)
		ocelot_deinit_port(ocelot, dp->index);

	ocelot_deinit_timestamp(ocelot);
	ocelot_deinit(ocelot);

out_mdiobus_free:
	if (felix->info->mdio_bus_free)
		felix->info->mdio_bus_free(ocelot);

	return err;
}

static void felix_teardown(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	struct dsa_port *dp;

	rtnl_lock();
	if (felix->tag_proto_ops)
		felix->tag_proto_ops->teardown(ds);
	rtnl_unlock();

	dsa_switch_for_each_available_port(dp, ds)
		ocelot_deinit_port(ocelot, dp->index);

	ocelot_devlink_sb_unregister(ocelot);
	ocelot_deinit_timestamp(ocelot);
	ocelot_deinit(ocelot);

	if (felix->info->mdio_bus_free)
		felix->info->mdio_bus_free(ocelot);
}

static int felix_hwtstamp_get(struct dsa_switch *ds, int port,
			      struct ifreq *ifr)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_hwstamp_get(ocelot, port, ifr);
}

static int felix_hwtstamp_set(struct dsa_switch *ds, int port,
			      struct ifreq *ifr)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	bool using_tag_8021q;
	int err;

	err = ocelot_hwstamp_set(ocelot, port, ifr);
	if (err)
		return err;

	using_tag_8021q = felix->tag_proto == DSA_TAG_PROTO_OCELOT_8021Q;

	return felix_update_trapping_destinations(ds, using_tag_8021q);
}

static bool felix_check_xtr_pkt(struct ocelot *ocelot)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	int err = 0, grp = 0;

	if (felix->tag_proto != DSA_TAG_PROTO_OCELOT_8021Q)
		return false;

	if (!felix->info->quirk_no_xtr_irq)
		return false;

	while (ocelot_read(ocelot, QS_XTR_DATA_PRESENT) & BIT(grp)) {
		struct sk_buff *skb;
		unsigned int type;

		err = ocelot_xtr_poll_frame(ocelot, grp, &skb);
		if (err)
			goto out;

		/* We trap to the CPU port module all PTP frames, but
		 * felix_rxtstamp() only gets called for event frames.
		 * So we need to avoid sending duplicate general
		 * message frames by running a second BPF classifier
		 * here and dropping those.
		 */
		__skb_push(skb, ETH_HLEN);

		type = ptp_classify_raw(skb);

		__skb_pull(skb, ETH_HLEN);

		if (type == PTP_CLASS_NONE) {
			kfree_skb(skb);
			continue;
		}

		netif_rx(skb);
	}

out:
	if (err < 0) {
		dev_err_ratelimited(ocelot->dev,
				    "Error during packet extraction: %pe\n",
				    ERR_PTR(err));
		ocelot_drain_cpu_queue(ocelot, 0);
	}

	return true;
}

static bool felix_rxtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb, unsigned int type)
{
	u32 tstamp_lo = OCELOT_SKB_CB(skb)->tstamp_lo;
	struct skb_shared_hwtstamps *shhwtstamps;
	struct ocelot *ocelot = ds->priv;
	struct timespec64 ts;
	u32 tstamp_hi;
	u64 tstamp;

	switch (type & PTP_CLASS_PMASK) {
	case PTP_CLASS_L2:
		if (!(ocelot->ports[port]->trap_proto & OCELOT_PROTO_PTP_L2))
			return false;
		break;
	case PTP_CLASS_IPV4:
	case PTP_CLASS_IPV6:
		if (!(ocelot->ports[port]->trap_proto & OCELOT_PROTO_PTP_L4))
			return false;
		break;
	}

	/* If the "no XTR IRQ" workaround is in use, tell DSA to defer this skb
	 * for RX timestamping. Then free it, and poll for its copy through
	 * MMIO in the CPU port module, and inject that into the stack from
	 * ocelot_xtr_poll().
	 */
	if (felix_check_xtr_pkt(ocelot)) {
		kfree_skb(skb);
		return true;
	}

	ocelot_ptp_gettime64(&ocelot->ptp_info, &ts);
	tstamp = ktime_set(ts.tv_sec, ts.tv_nsec);

	tstamp_hi = tstamp >> 32;
	if ((tstamp & 0xffffffff) < tstamp_lo)
		tstamp_hi--;

	tstamp = ((u64)tstamp_hi << 32) | tstamp_lo;

	shhwtstamps = skb_hwtstamps(skb);
	memset(shhwtstamps, 0, sizeof(struct skb_shared_hwtstamps));
	shhwtstamps->hwtstamp = tstamp;
	return false;
}

static void felix_txtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb)
{
	struct ocelot *ocelot = ds->priv;
	struct sk_buff *clone = NULL;

	if (!ocelot->ptp)
		return;

	if (ocelot_port_txtstamp_request(ocelot, port, skb, &clone)) {
		dev_err_ratelimited(ds->dev,
				    "port %d delivering skb without TX timestamp\n",
				    port);
		return;
	}

	if (clone)
		OCELOT_SKB_CB(skb)->clone = clone;
}

static int felix_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct ocelot *ocelot = ds->priv;
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	ocelot_port_set_maxlen(ocelot, port, new_mtu);

	mutex_lock(&ocelot->fwd_domain_lock);

	if (ocelot_port->taprio && ocelot->ops->tas_guard_bands_update)
		ocelot->ops->tas_guard_bands_update(ocelot, port);

	mutex_unlock(&ocelot->fwd_domain_lock);

	return 0;
}

static int felix_get_max_mtu(struct dsa_switch *ds, int port)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_get_max_mtu(ocelot, port);
}

static int felix_cls_flower_add(struct dsa_switch *ds, int port,
				struct flow_cls_offload *cls, bool ingress)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	bool using_tag_8021q;
	int err;

	err = ocelot_cls_flower_replace(ocelot, port, cls, ingress);
	if (err)
		return err;

	using_tag_8021q = felix->tag_proto == DSA_TAG_PROTO_OCELOT_8021Q;

	return felix_update_trapping_destinations(ds, using_tag_8021q);
}

static int felix_cls_flower_del(struct dsa_switch *ds, int port,
				struct flow_cls_offload *cls, bool ingress)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_cls_flower_destroy(ocelot, port, cls, ingress);
}

static int felix_cls_flower_stats(struct dsa_switch *ds, int port,
				  struct flow_cls_offload *cls, bool ingress)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_cls_flower_stats(ocelot, port, cls, ingress);
}

static int felix_port_policer_add(struct dsa_switch *ds, int port,
				  struct dsa_mall_policer_tc_entry *policer)
{
	struct ocelot *ocelot = ds->priv;
	struct ocelot_policer pol = {
		.rate = div_u64(policer->rate_bytes_per_sec, 1000) * 8,
		.burst = policer->burst,
	};

	return ocelot_port_policer_add(ocelot, port, &pol);
}

static void felix_port_policer_del(struct dsa_switch *ds, int port)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_policer_del(ocelot, port);
}

static int felix_port_mirror_add(struct dsa_switch *ds, int port,
				 struct dsa_mall_mirror_tc_entry *mirror,
				 bool ingress, struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_mirror_add(ocelot, port, mirror->to_local_port,
				      ingress, extack);
}

static void felix_port_mirror_del(struct dsa_switch *ds, int port,
				  struct dsa_mall_mirror_tc_entry *mirror)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_mirror_del(ocelot, port, mirror->ingress);
}

static int felix_port_setup_tc(struct dsa_switch *ds, int port,
			       enum tc_setup_type type,
			       void *type_data)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);

	if (felix->info->port_setup_tc)
		return felix->info->port_setup_tc(ds, port, type, type_data);
	else
		return -EOPNOTSUPP;
}

static int felix_sb_pool_get(struct dsa_switch *ds, unsigned int sb_index,
			     u16 pool_index,
			     struct devlink_sb_pool_info *pool_info)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_sb_pool_get(ocelot, sb_index, pool_index, pool_info);
}

static int felix_sb_pool_set(struct dsa_switch *ds, unsigned int sb_index,
			     u16 pool_index, u32 size,
			     enum devlink_sb_threshold_type threshold_type,
			     struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_sb_pool_set(ocelot, sb_index, pool_index, size,
				  threshold_type, extack);
}

static int felix_sb_port_pool_get(struct dsa_switch *ds, int port,
				  unsigned int sb_index, u16 pool_index,
				  u32 *p_threshold)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_sb_port_pool_get(ocelot, port, sb_index, pool_index,
				       p_threshold);
}

static int felix_sb_port_pool_set(struct dsa_switch *ds, int port,
				  unsigned int sb_index, u16 pool_index,
				  u32 threshold, struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_sb_port_pool_set(ocelot, port, sb_index, pool_index,
				       threshold, extack);
}

static int felix_sb_tc_pool_bind_get(struct dsa_switch *ds, int port,
				     unsigned int sb_index, u16 tc_index,
				     enum devlink_sb_pool_type pool_type,
				     u16 *p_pool_index, u32 *p_threshold)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_sb_tc_pool_bind_get(ocelot, port, sb_index, tc_index,
					  pool_type, p_pool_index,
					  p_threshold);
}

static int felix_sb_tc_pool_bind_set(struct dsa_switch *ds, int port,
				     unsigned int sb_index, u16 tc_index,
				     enum devlink_sb_pool_type pool_type,
				     u16 pool_index, u32 threshold,
				     struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_sb_tc_pool_bind_set(ocelot, port, sb_index, tc_index,
					  pool_type, pool_index, threshold,
					  extack);
}

static int felix_sb_occ_snapshot(struct dsa_switch *ds,
				 unsigned int sb_index)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_sb_occ_snapshot(ocelot, sb_index);
}

static int felix_sb_occ_max_clear(struct dsa_switch *ds,
				  unsigned int sb_index)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_sb_occ_max_clear(ocelot, sb_index);
}

static int felix_sb_occ_port_pool_get(struct dsa_switch *ds, int port,
				      unsigned int sb_index, u16 pool_index,
				      u32 *p_cur, u32 *p_max)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_sb_occ_port_pool_get(ocelot, port, sb_index, pool_index,
					   p_cur, p_max);
}

static int felix_sb_occ_tc_port_bind_get(struct dsa_switch *ds, int port,
					 unsigned int sb_index, u16 tc_index,
					 enum devlink_sb_pool_type pool_type,
					 u32 *p_cur, u32 *p_max)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_sb_occ_tc_port_bind_get(ocelot, port, sb_index, tc_index,
					      pool_type, p_cur, p_max);
}

static int felix_mrp_add(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_mrp *mrp)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_mrp_add(ocelot, port, mrp);
}

static int felix_mrp_del(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_mrp *mrp)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_mrp_add(ocelot, port, mrp);
}

static int
felix_mrp_add_ring_role(struct dsa_switch *ds, int port,
			const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_mrp_add_ring_role(ocelot, port, mrp);
}

static int
felix_mrp_del_ring_role(struct dsa_switch *ds, int port,
			const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_mrp_del_ring_role(ocelot, port, mrp);
}

static int felix_port_get_default_prio(struct dsa_switch *ds, int port)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_get_default_prio(ocelot, port);
}

static int felix_port_set_default_prio(struct dsa_switch *ds, int port,
				       u8 prio)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_set_default_prio(ocelot, port, prio);
}

static int felix_port_get_dscp_prio(struct dsa_switch *ds, int port, u8 dscp)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_get_dscp_prio(ocelot, port, dscp);
}

static int felix_port_add_dscp_prio(struct dsa_switch *ds, int port, u8 dscp,
				    u8 prio)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_add_dscp_prio(ocelot, port, dscp, prio);
}

static int felix_port_del_dscp_prio(struct dsa_switch *ds, int port, u8 dscp,
				    u8 prio)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_del_dscp_prio(ocelot, port, dscp, prio);
}

static int felix_get_mm(struct dsa_switch *ds, int port,
			struct ethtool_mm_state *state)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_get_mm(ocelot, port, state);
}

static int felix_set_mm(struct dsa_switch *ds, int port,
			struct ethtool_mm_cfg *cfg,
			struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_set_mm(ocelot, port, cfg, extack);
}

static void felix_get_mm_stats(struct dsa_switch *ds, int port,
			       struct ethtool_mm_stats *stats)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_get_mm_stats(ocelot, port, stats);
}

const struct dsa_switch_ops felix_switch_ops = {
	.get_tag_protocol		= felix_get_tag_protocol,
	.change_tag_protocol		= felix_change_tag_protocol,
	.connect_tag_protocol		= felix_connect_tag_protocol,
	.setup				= felix_setup,
	.teardown			= felix_teardown,
	.set_ageing_time		= felix_set_ageing_time,
	.get_mm				= felix_get_mm,
	.set_mm				= felix_set_mm,
	.get_mm_stats			= felix_get_mm_stats,
	.get_stats64			= felix_get_stats64,
	.get_pause_stats		= felix_get_pause_stats,
	.get_rmon_stats			= felix_get_rmon_stats,
	.get_eth_ctrl_stats		= felix_get_eth_ctrl_stats,
	.get_eth_mac_stats		= felix_get_eth_mac_stats,
	.get_eth_phy_stats		= felix_get_eth_phy_stats,
	.get_strings			= felix_get_strings,
	.get_ethtool_stats		= felix_get_ethtool_stats,
	.get_sset_count			= felix_get_sset_count,
	.get_ts_info			= felix_get_ts_info,
	.phylink_get_caps		= felix_phylink_get_caps,
	.phylink_mac_config		= felix_phylink_mac_config,
	.phylink_mac_select_pcs		= felix_phylink_mac_select_pcs,
	.phylink_mac_link_down		= felix_phylink_mac_link_down,
	.phylink_mac_link_up		= felix_phylink_mac_link_up,
	.port_enable			= felix_port_enable,
	.port_fast_age			= felix_port_fast_age,
	.port_fdb_dump			= felix_fdb_dump,
	.port_fdb_add			= felix_fdb_add,
	.port_fdb_del			= felix_fdb_del,
	.lag_fdb_add			= felix_lag_fdb_add,
	.lag_fdb_del			= felix_lag_fdb_del,
	.port_mdb_add			= felix_mdb_add,
	.port_mdb_del			= felix_mdb_del,
	.port_pre_bridge_flags		= felix_pre_bridge_flags,
	.port_bridge_flags		= felix_bridge_flags,
	.port_bridge_join		= felix_bridge_join,
	.port_bridge_leave		= felix_bridge_leave,
	.port_lag_join			= felix_lag_join,
	.port_lag_leave			= felix_lag_leave,
	.port_lag_change		= felix_lag_change,
	.port_stp_state_set		= felix_bridge_stp_state_set,
	.port_vlan_filtering		= felix_vlan_filtering,
	.port_vlan_add			= felix_vlan_add,
	.port_vlan_del			= felix_vlan_del,
	.port_hwtstamp_get		= felix_hwtstamp_get,
	.port_hwtstamp_set		= felix_hwtstamp_set,
	.port_rxtstamp			= felix_rxtstamp,
	.port_txtstamp			= felix_txtstamp,
	.port_change_mtu		= felix_change_mtu,
	.port_max_mtu			= felix_get_max_mtu,
	.port_policer_add		= felix_port_policer_add,
	.port_policer_del		= felix_port_policer_del,
	.port_mirror_add		= felix_port_mirror_add,
	.port_mirror_del		= felix_port_mirror_del,
	.cls_flower_add			= felix_cls_flower_add,
	.cls_flower_del			= felix_cls_flower_del,
	.cls_flower_stats		= felix_cls_flower_stats,
	.port_setup_tc			= felix_port_setup_tc,
	.devlink_sb_pool_get		= felix_sb_pool_get,
	.devlink_sb_pool_set		= felix_sb_pool_set,
	.devlink_sb_port_pool_get	= felix_sb_port_pool_get,
	.devlink_sb_port_pool_set	= felix_sb_port_pool_set,
	.devlink_sb_tc_pool_bind_get	= felix_sb_tc_pool_bind_get,
	.devlink_sb_tc_pool_bind_set	= felix_sb_tc_pool_bind_set,
	.devlink_sb_occ_snapshot	= felix_sb_occ_snapshot,
	.devlink_sb_occ_max_clear	= felix_sb_occ_max_clear,
	.devlink_sb_occ_port_pool_get	= felix_sb_occ_port_pool_get,
	.devlink_sb_occ_tc_port_bind_get= felix_sb_occ_tc_port_bind_get,
	.port_mrp_add			= felix_mrp_add,
	.port_mrp_del			= felix_mrp_del,
	.port_mrp_add_ring_role		= felix_mrp_add_ring_role,
	.port_mrp_del_ring_role		= felix_mrp_del_ring_role,
	.tag_8021q_vlan_add		= felix_tag_8021q_vlan_add,
	.tag_8021q_vlan_del		= felix_tag_8021q_vlan_del,
	.port_get_default_prio		= felix_port_get_default_prio,
	.port_set_default_prio		= felix_port_set_default_prio,
	.port_get_dscp_prio		= felix_port_get_dscp_prio,
	.port_add_dscp_prio		= felix_port_add_dscp_prio,
	.port_del_dscp_prio		= felix_port_del_dscp_prio,
	.port_set_host_flood		= felix_port_set_host_flood,
	.port_change_conduit		= felix_port_change_conduit,
};
EXPORT_SYMBOL_GPL(felix_switch_ops);

struct net_device *felix_port_to_netdev(struct ocelot *ocelot, int port)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct dsa_switch *ds = felix->ds;

	if (!dsa_is_user_port(ds, port))
		return NULL;

	return dsa_to_port(ds, port)->user;
}
EXPORT_SYMBOL_GPL(felix_port_to_netdev);

int felix_netdev_to_port(struct net_device *dev)
{
	struct dsa_port *dp;

	dp = dsa_port_from_netdev(dev);
	if (IS_ERR(dp))
		return -EINVAL;

	return dp->index;
}
EXPORT_SYMBOL_GPL(felix_netdev_to_port);

MODULE_DESCRIPTION("Felix DSA library");
MODULE_LICENSE("GPL");
