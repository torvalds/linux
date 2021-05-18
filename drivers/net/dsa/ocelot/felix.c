// SPDX-License-Identifier: GPL-2.0
/* Copyright 2019-2021 NXP Semiconductors
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
#include <linux/pcs-lynx.h>
#include <net/pkt_sched.h>
#include <net/dsa.h>
#include "felix.h"

static int felix_tag_8021q_rxvlan_add(struct felix *felix, int port, u16 vid,
				      bool pvid, bool untagged)
{
	struct ocelot_vcap_filter *outer_tagging_rule;
	struct ocelot *ocelot = &felix->ocelot;
	struct dsa_switch *ds = felix->ds;
	int key_length, upstream, err;

	/* We don't need to install the rxvlan into the other ports' filtering
	 * tables, because we're just pushing the rxvlan when sending towards
	 * the CPU
	 */
	if (!pvid)
		return 0;

	key_length = ocelot->vcap[VCAP_ES0].keys[VCAP_ES0_IGR_PORT].length;
	upstream = dsa_upstream_port(ds, port);

	outer_tagging_rule = kzalloc(sizeof(struct ocelot_vcap_filter),
				     GFP_KERNEL);
	if (!outer_tagging_rule)
		return -ENOMEM;

	outer_tagging_rule->key_type = OCELOT_VCAP_KEY_ANY;
	outer_tagging_rule->prio = 1;
	outer_tagging_rule->id.cookie = port;
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

static int felix_tag_8021q_txvlan_add(struct felix *felix, int port, u16 vid,
				      bool pvid, bool untagged)
{
	struct ocelot_vcap_filter *untagging_rule, *redirect_rule;
	struct ocelot *ocelot = &felix->ocelot;
	struct dsa_switch *ds = felix->ds;
	int upstream, err;

	/* tag_8021q.c assumes we are implementing this via port VLAN
	 * membership, which we aren't. So we don't need to add any VCAP filter
	 * for the CPU port.
	 */
	if (ocelot->ports[port]->is_dsa_8021q_cpu)
		return 0;

	untagging_rule = kzalloc(sizeof(struct ocelot_vcap_filter), GFP_KERNEL);
	if (!untagging_rule)
		return -ENOMEM;

	redirect_rule = kzalloc(sizeof(struct ocelot_vcap_filter), GFP_KERNEL);
	if (!redirect_rule) {
		kfree(untagging_rule);
		return -ENOMEM;
	}

	upstream = dsa_upstream_port(ds, port);

	untagging_rule->key_type = OCELOT_VCAP_KEY_ANY;
	untagging_rule->ingress_port_mask = BIT(upstream);
	untagging_rule->vlan.vid.value = vid;
	untagging_rule->vlan.vid.mask = VLAN_VID_MASK;
	untagging_rule->prio = 1;
	untagging_rule->id.cookie = port;
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

	redirect_rule->key_type = OCELOT_VCAP_KEY_ANY;
	redirect_rule->ingress_port_mask = BIT(upstream);
	redirect_rule->pag = port;
	redirect_rule->prio = 1;
	redirect_rule->id.cookie = port;
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

static int felix_tag_8021q_vlan_add(struct dsa_switch *ds, int port, u16 vid,
				    u16 flags)
{
	bool untagged = flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = flags & BRIDGE_VLAN_INFO_PVID;
	struct ocelot *ocelot = ds->priv;

	if (vid_is_dsa_8021q_rxvlan(vid))
		return felix_tag_8021q_rxvlan_add(ocelot_to_felix(ocelot),
						  port, vid, pvid, untagged);

	if (vid_is_dsa_8021q_txvlan(vid))
		return felix_tag_8021q_txvlan_add(ocelot_to_felix(ocelot),
						  port, vid, pvid, untagged);

	return 0;
}

static int felix_tag_8021q_rxvlan_del(struct felix *felix, int port, u16 vid)
{
	struct ocelot_vcap_filter *outer_tagging_rule;
	struct ocelot_vcap_block *block_vcap_es0;
	struct ocelot *ocelot = &felix->ocelot;

	block_vcap_es0 = &ocelot->block[VCAP_ES0];

	outer_tagging_rule = ocelot_vcap_block_find_filter_by_id(block_vcap_es0,
								 port, false);
	/* In rxvlan_add, we had the "if (!pvid) return 0" logic to avoid
	 * installing outer tagging ES0 rules where they weren't needed.
	 * But in rxvlan_del, the API doesn't give us the "flags" anymore,
	 * so that forces us to be slightly sloppy here, and just assume that
	 * if we didn't find an outer_tagging_rule it means that there was
	 * none in the first place, i.e. rxvlan_del is called on a non-pvid
	 * port. This is most probably true though.
	 */
	if (!outer_tagging_rule)
		return 0;

	return ocelot_vcap_filter_del(ocelot, outer_tagging_rule);
}

static int felix_tag_8021q_txvlan_del(struct felix *felix, int port, u16 vid)
{
	struct ocelot_vcap_filter *untagging_rule, *redirect_rule;
	struct ocelot_vcap_block *block_vcap_is1;
	struct ocelot_vcap_block *block_vcap_is2;
	struct ocelot *ocelot = &felix->ocelot;
	int err;

	if (ocelot->ports[port]->is_dsa_8021q_cpu)
		return 0;

	block_vcap_is1 = &ocelot->block[VCAP_IS1];
	block_vcap_is2 = &ocelot->block[VCAP_IS2];

	untagging_rule = ocelot_vcap_block_find_filter_by_id(block_vcap_is1,
							     port, false);
	if (!untagging_rule)
		return 0;

	err = ocelot_vcap_filter_del(ocelot, untagging_rule);
	if (err)
		return err;

	redirect_rule = ocelot_vcap_block_find_filter_by_id(block_vcap_is2,
							    port, false);
	if (!redirect_rule)
		return 0;

	return ocelot_vcap_filter_del(ocelot, redirect_rule);
}

static int felix_tag_8021q_vlan_del(struct dsa_switch *ds, int port, u16 vid)
{
	struct ocelot *ocelot = ds->priv;

	if (vid_is_dsa_8021q_rxvlan(vid))
		return felix_tag_8021q_rxvlan_del(ocelot_to_felix(ocelot),
						  port, vid);

	if (vid_is_dsa_8021q_txvlan(vid))
		return felix_tag_8021q_txvlan_del(ocelot_to_felix(ocelot),
						  port, vid);

	return 0;
}

static const struct dsa_8021q_ops felix_tag_8021q_ops = {
	.vlan_add	= felix_tag_8021q_vlan_add,
	.vlan_del	= felix_tag_8021q_vlan_del,
};

/* Alternatively to using the NPI functionality, that same hardware MAC
 * connected internally to the enetc or fman DSA master can be configured to
 * use the software-defined tag_8021q frame format. As far as the hardware is
 * concerned, it thinks it is a "dumb switch" - the queues of the CPU port
 * module are now disconnected from it, but can still be accessed through
 * register-based MMIO.
 */
static void felix_8021q_cpu_port_init(struct ocelot *ocelot, int port)
{
	ocelot->ports[port]->is_dsa_8021q_cpu = true;
	ocelot->npi = -1;

	/* Overwrite PGID_CPU with the non-tagging port */
	ocelot_write_rix(ocelot, BIT(port), ANA_PGID_PGID, PGID_CPU);

	ocelot_apply_bridge_fwd_mask(ocelot);
}

static void felix_8021q_cpu_port_deinit(struct ocelot *ocelot, int port)
{
	ocelot->ports[port]->is_dsa_8021q_cpu = false;

	/* Restore PGID_CPU */
	ocelot_write_rix(ocelot, BIT(ocelot->num_phys_ports), ANA_PGID_PGID,
			 PGID_CPU);

	ocelot_apply_bridge_fwd_mask(ocelot);
}

/* Set up a VCAP IS2 rule for delivering PTP frames to the CPU port module.
 * If the quirk_no_xtr_irq is in place, then also copy those PTP frames to the
 * tag_8021q CPU port.
 */
static int felix_setup_mmio_filtering(struct felix *felix)
{
	unsigned long user_ports = 0, cpu_ports = 0;
	struct ocelot_vcap_filter *redirect_rule;
	struct ocelot_vcap_filter *tagging_rule;
	struct ocelot *ocelot = &felix->ocelot;
	struct dsa_switch *ds = felix->ds;
	int port, ret;

	tagging_rule = kzalloc(sizeof(struct ocelot_vcap_filter), GFP_KERNEL);
	if (!tagging_rule)
		return -ENOMEM;

	redirect_rule = kzalloc(sizeof(struct ocelot_vcap_filter), GFP_KERNEL);
	if (!redirect_rule) {
		kfree(tagging_rule);
		return -ENOMEM;
	}

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		if (dsa_is_user_port(ds, port))
			user_ports |= BIT(port);
		if (dsa_is_cpu_port(ds, port))
			cpu_ports |= BIT(port);
	}

	tagging_rule->key_type = OCELOT_VCAP_KEY_ETYPE;
	*(__be16 *)tagging_rule->key.etype.etype.value = htons(ETH_P_1588);
	*(__be16 *)tagging_rule->key.etype.etype.mask = htons(0xffff);
	tagging_rule->ingress_port_mask = user_ports;
	tagging_rule->prio = 1;
	tagging_rule->id.cookie = ocelot->num_phys_ports;
	tagging_rule->id.tc_offload = false;
	tagging_rule->block_id = VCAP_IS1;
	tagging_rule->type = OCELOT_VCAP_FILTER_OFFLOAD;
	tagging_rule->lookup = 0;
	tagging_rule->action.pag_override_mask = 0xff;
	tagging_rule->action.pag_val = ocelot->num_phys_ports;

	ret = ocelot_vcap_filter_add(ocelot, tagging_rule, NULL);
	if (ret) {
		kfree(tagging_rule);
		kfree(redirect_rule);
		return ret;
	}

	redirect_rule->key_type = OCELOT_VCAP_KEY_ANY;
	redirect_rule->ingress_port_mask = user_ports;
	redirect_rule->pag = ocelot->num_phys_ports;
	redirect_rule->prio = 1;
	redirect_rule->id.cookie = ocelot->num_phys_ports;
	redirect_rule->id.tc_offload = false;
	redirect_rule->block_id = VCAP_IS2;
	redirect_rule->type = OCELOT_VCAP_FILTER_OFFLOAD;
	redirect_rule->lookup = 0;
	redirect_rule->action.cpu_copy_ena = true;
	if (felix->info->quirk_no_xtr_irq) {
		/* Redirect to the tag_8021q CPU but also copy PTP packets to
		 * the CPU port module
		 */
		redirect_rule->action.mask_mode = OCELOT_MASK_MODE_REDIRECT;
		redirect_rule->action.port_mask = cpu_ports;
	} else {
		/* Trap PTP packets only to the CPU port module (which is
		 * redirected to the NPI port)
		 */
		redirect_rule->action.mask_mode = OCELOT_MASK_MODE_PERMIT_DENY;
		redirect_rule->action.port_mask = 0;
	}

	ret = ocelot_vcap_filter_add(ocelot, redirect_rule, NULL);
	if (ret) {
		ocelot_vcap_filter_del(ocelot, tagging_rule);
		kfree(redirect_rule);
		return ret;
	}

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

static int felix_teardown_mmio_filtering(struct felix *felix)
{
	struct ocelot_vcap_filter *tagging_rule, *redirect_rule;
	struct ocelot_vcap_block *block_vcap_is1;
	struct ocelot_vcap_block *block_vcap_is2;
	struct ocelot *ocelot = &felix->ocelot;
	int err;

	block_vcap_is1 = &ocelot->block[VCAP_IS1];
	block_vcap_is2 = &ocelot->block[VCAP_IS2];

	tagging_rule = ocelot_vcap_block_find_filter_by_id(block_vcap_is1,
							   ocelot->num_phys_ports,
							   false);
	if (!tagging_rule)
		return -ENOENT;

	err = ocelot_vcap_filter_del(ocelot, tagging_rule);
	if (err)
		return err;

	redirect_rule = ocelot_vcap_block_find_filter_by_id(block_vcap_is2,
							    ocelot->num_phys_ports,
							    false);
	if (!redirect_rule)
		return -ENOENT;

	return ocelot_vcap_filter_del(ocelot, redirect_rule);
}

static int felix_setup_tag_8021q(struct dsa_switch *ds, int cpu)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	unsigned long cpu_flood;
	int port, err;

	felix_8021q_cpu_port_init(ocelot, cpu);

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

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
				 ANA_PORT_CPU_FWD_BPDU_CFG, port);
	}

	/* In tag_8021q mode, the CPU port module is unused, except for PTP
	 * frames. So we want to disable flooding of any kind to the CPU port
	 * module, since packets going there will end in a black hole.
	 */
	cpu_flood = ANA_PGID_PGID_PGID(BIT(ocelot->num_phys_ports));
	ocelot_rmw_rix(ocelot, 0, cpu_flood, ANA_PGID_PGID, PGID_UC);
	ocelot_rmw_rix(ocelot, 0, cpu_flood, ANA_PGID_PGID, PGID_MC);
	ocelot_rmw_rix(ocelot, 0, cpu_flood, ANA_PGID_PGID, PGID_BC);

	felix->dsa_8021q_ctx = kzalloc(sizeof(*felix->dsa_8021q_ctx),
				       GFP_KERNEL);
	if (!felix->dsa_8021q_ctx)
		return -ENOMEM;

	felix->dsa_8021q_ctx->ops = &felix_tag_8021q_ops;
	felix->dsa_8021q_ctx->proto = htons(ETH_P_8021AD);
	felix->dsa_8021q_ctx->ds = ds;

	err = dsa_8021q_setup(felix->dsa_8021q_ctx, true);
	if (err)
		goto out_free_dsa_8021_ctx;

	err = felix_setup_mmio_filtering(felix);
	if (err)
		goto out_teardown_dsa_8021q;

	return 0;

out_teardown_dsa_8021q:
	dsa_8021q_setup(felix->dsa_8021q_ctx, false);
out_free_dsa_8021_ctx:
	kfree(felix->dsa_8021q_ctx);
	return err;
}

static void felix_teardown_tag_8021q(struct dsa_switch *ds, int cpu)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	int err, port;

	err = felix_teardown_mmio_filtering(felix);
	if (err)
		dev_err(ds->dev, "felix_teardown_mmio_filtering returned %d",
			err);

	err = dsa_8021q_setup(felix->dsa_8021q_ctx, false);
	if (err)
		dev_err(ds->dev, "dsa_8021q_setup returned %d", err);

	kfree(felix->dsa_8021q_ctx);

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		/* Restore the logic from ocelot_init:
		 * do not forward BPDU frames to the front ports.
		 */
		ocelot_write_gix(ocelot,
				 ANA_PORT_CPU_FWD_BPDU_CFG_BPDU_REDIR_ENA(0xffff),
				 ANA_PORT_CPU_FWD_BPDU_CFG,
				 port);
	}

	felix_8021q_cpu_port_deinit(ocelot, cpu);
}

/* The CPU port module is connected to the Node Processor Interface (NPI). This
 * is the mode through which frames can be injected from and extracted to an
 * external CPU, over Ethernet. In NXP SoCs, the "external CPU" is the ARM CPU
 * running Linux, and this forms a DSA setup together with the enetc or fman
 * DSA master.
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

static int felix_setup_tag_npi(struct dsa_switch *ds, int cpu)
{
	struct ocelot *ocelot = ds->priv;
	unsigned long cpu_flood;

	felix_npi_port_init(ocelot, cpu);

	/* Include the CPU port module (and indirectly, the NPI port)
	 * in the forwarding mask for unknown unicast - the hardware
	 * default value for ANA_FLOODING_FLD_UNICAST excludes
	 * BIT(ocelot->num_phys_ports), and so does ocelot_init,
	 * since Ocelot relies on whitelisting MAC addresses towards
	 * PGID_CPU.
	 * We do this because DSA does not yet perform RX filtering,
	 * and the NPI port does not perform source address learning,
	 * so traffic sent to Linux is effectively unknown from the
	 * switch's perspective.
	 */
	cpu_flood = ANA_PGID_PGID_PGID(BIT(ocelot->num_phys_ports));
	ocelot_rmw_rix(ocelot, cpu_flood, cpu_flood, ANA_PGID_PGID, PGID_UC);
	ocelot_rmw_rix(ocelot, cpu_flood, cpu_flood, ANA_PGID_PGID, PGID_MC);
	ocelot_rmw_rix(ocelot, cpu_flood, cpu_flood, ANA_PGID_PGID, PGID_BC);

	return 0;
}

static void felix_teardown_tag_npi(struct dsa_switch *ds, int cpu)
{
	struct ocelot *ocelot = ds->priv;

	felix_npi_port_deinit(ocelot, cpu);
}

static int felix_set_tag_protocol(struct dsa_switch *ds, int cpu,
				  enum dsa_tag_protocol proto)
{
	int err;

	switch (proto) {
	case DSA_TAG_PROTO_SEVILLE:
	case DSA_TAG_PROTO_OCELOT:
		err = felix_setup_tag_npi(ds, cpu);
		break;
	case DSA_TAG_PROTO_OCELOT_8021Q:
		err = felix_setup_tag_8021q(ds, cpu);
		break;
	default:
		err = -EPROTONOSUPPORT;
	}

	return err;
}

static void felix_del_tag_protocol(struct dsa_switch *ds, int cpu,
				   enum dsa_tag_protocol proto)
{
	switch (proto) {
	case DSA_TAG_PROTO_SEVILLE:
	case DSA_TAG_PROTO_OCELOT:
		felix_teardown_tag_npi(ds, cpu);
		break;
	case DSA_TAG_PROTO_OCELOT_8021Q:
		felix_teardown_tag_8021q(ds, cpu);
		break;
	default:
		break;
	}
}

/* This always leaves the switch in a consistent state, because although the
 * tag_8021q setup can fail, the NPI setup can't. So either the change is made,
 * or the restoration is guaranteed to work.
 */
static int felix_change_tag_protocol(struct dsa_switch *ds, int cpu,
				     enum dsa_tag_protocol proto)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	enum dsa_tag_protocol old_proto = felix->tag_proto;
	int err;

	if (proto != DSA_TAG_PROTO_SEVILLE &&
	    proto != DSA_TAG_PROTO_OCELOT &&
	    proto != DSA_TAG_PROTO_OCELOT_8021Q)
		return -EPROTONOSUPPORT;

	felix_del_tag_protocol(ds, cpu, old_proto);

	err = felix_set_tag_protocol(ds, cpu, proto);
	if (err) {
		felix_set_tag_protocol(ds, cpu, old_proto);
		return err;
	}

	felix->tag_proto = proto;

	return 0;
}

static enum dsa_tag_protocol felix_get_tag_protocol(struct dsa_switch *ds,
						    int port,
						    enum dsa_tag_protocol mp)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);

	return felix->tag_proto;
}

static int felix_set_ageing_time(struct dsa_switch *ds,
				 unsigned int ageing_time)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_set_ageing_time(ocelot, ageing_time);

	return 0;
}

static int felix_fdb_dump(struct dsa_switch *ds, int port,
			  dsa_fdb_dump_cb_t *cb, void *data)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_fdb_dump(ocelot, port, cb, data);
}

static int felix_fdb_add(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_fdb_add(ocelot, port, addr, vid);
}

static int felix_fdb_del(struct dsa_switch *ds, int port,
			 const unsigned char *addr, u16 vid)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_fdb_del(ocelot, port, addr, vid);
}

static int felix_mdb_add(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_mdb_add(ocelot, port, mdb);
}

static int felix_mdb_del(struct dsa_switch *ds, int port,
			 const struct switchdev_obj_port_mdb *mdb)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_mdb_del(ocelot, port, mdb);
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

	ocelot_port_bridge_flags(ocelot, port, val);

	return 0;
}

static int felix_bridge_join(struct dsa_switch *ds, int port,
			     struct net_device *br)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_bridge_join(ocelot, port, br);

	return 0;
}

static void felix_bridge_leave(struct dsa_switch *ds, int port,
			       struct net_device *br)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_bridge_leave(ocelot, port, br);
}

static int felix_lag_join(struct dsa_switch *ds, int port,
			  struct net_device *bond,
			  struct netdev_lag_upper_info *info)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_lag_join(ocelot, port, bond, info);
}

static int felix_lag_leave(struct dsa_switch *ds, int port,
			   struct net_device *bond)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_lag_leave(ocelot, port, bond);

	return 0;
}

static int felix_lag_change(struct dsa_switch *ds, int port)
{
	struct dsa_port *dp = dsa_to_port(ds, port);
	struct ocelot *ocelot = ds->priv;

	ocelot_port_lag_change(ocelot, port, dp->lag_tx_enabled);

	return 0;
}

static int felix_vlan_prepare(struct dsa_switch *ds, int port,
			      const struct switchdev_obj_port_vlan *vlan)
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
				   flags & BRIDGE_VLAN_INFO_UNTAGGED);
}

static int felix_vlan_filtering(struct dsa_switch *ds, int port, bool enabled,
				struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_vlan_filtering(ocelot, port, enabled);
}

static int felix_vlan_add(struct dsa_switch *ds, int port,
			  const struct switchdev_obj_port_vlan *vlan,
			  struct netlink_ext_ack *extack)
{
	struct ocelot *ocelot = ds->priv;
	u16 flags = vlan->flags;
	int err;

	err = felix_vlan_prepare(ds, port, vlan);
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

static int felix_port_enable(struct dsa_switch *ds, int port,
			     struct phy_device *phy)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_enable(ocelot, port, phy);

	return 0;
}

static void felix_port_disable(struct dsa_switch *ds, int port)
{
	struct ocelot *ocelot = ds->priv;

	return ocelot_port_disable(ocelot, port);
}

static void felix_phylink_validate(struct dsa_switch *ds, int port,
				   unsigned long *supported,
				   struct phylink_link_state *state)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);

	if (felix->info->phylink_validate)
		felix->info->phylink_validate(ocelot, port, supported, state);
}

static void felix_phylink_mac_config(struct dsa_switch *ds, int port,
				     unsigned int link_an_mode,
				     const struct phylink_link_state *state)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	struct dsa_port *dp = dsa_to_port(ds, port);

	if (felix->pcs[port])
		phylink_set_pcs(dp->pl, &felix->pcs[port]->pcs);
}

static void felix_phylink_mac_link_down(struct dsa_switch *ds, int port,
					unsigned int link_an_mode,
					phy_interface_t interface)
{
	struct ocelot *ocelot = ds->priv;
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int err;

	ocelot_port_rmwl(ocelot_port, 0, DEV_MAC_ENA_CFG_RX_ENA,
			 DEV_MAC_ENA_CFG);

	ocelot_fields_write(ocelot, port, QSYS_SWITCH_PORT_MODE_PORT_ENA, 0);

	err = ocelot_port_flush(ocelot, port);
	if (err)
		dev_err(ocelot->dev, "failed to flush port %d: %d\n",
			port, err);

	/* Put the port in reset. */
	ocelot_port_writel(ocelot_port,
			   DEV_CLOCK_CFG_MAC_TX_RST |
			   DEV_CLOCK_CFG_MAC_RX_RST |
			   DEV_CLOCK_CFG_LINK_SPEED(OCELOT_SPEED_1000),
			   DEV_CLOCK_CFG);
}

static void felix_phylink_mac_link_up(struct dsa_switch *ds, int port,
				      unsigned int link_an_mode,
				      phy_interface_t interface,
				      struct phy_device *phydev,
				      int speed, int duplex,
				      bool tx_pause, bool rx_pause)
{
	struct ocelot *ocelot = ds->priv;
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct felix *felix = ocelot_to_felix(ocelot);
	u32 mac_fc_cfg;

	/* Take port out of reset by clearing the MAC_TX_RST, MAC_RX_RST and
	 * PORT_RST bits in DEV_CLOCK_CFG. Note that the way this system is
	 * integrated is that the MAC speed is fixed and it's the PCS who is
	 * performing the rate adaptation, so we have to write "1000Mbps" into
	 * the LINK_SPEED field of DEV_CLOCK_CFG (which is also its default
	 * value).
	 */
	ocelot_port_writel(ocelot_port,
			   DEV_CLOCK_CFG_LINK_SPEED(OCELOT_SPEED_1000),
			   DEV_CLOCK_CFG);

	switch (speed) {
	case SPEED_10:
		mac_fc_cfg = SYS_MAC_FC_CFG_FC_LINK_SPEED(3);
		break;
	case SPEED_100:
		mac_fc_cfg = SYS_MAC_FC_CFG_FC_LINK_SPEED(2);
		break;
	case SPEED_1000:
	case SPEED_2500:
		mac_fc_cfg = SYS_MAC_FC_CFG_FC_LINK_SPEED(1);
		break;
	default:
		dev_err(ocelot->dev, "Unsupported speed on port %d: %d\n",
			port, speed);
		return;
	}

	/* handle Rx pause in all cases, with 2500base-X this is used for rate
	 * adaptation.
	 */
	mac_fc_cfg |= SYS_MAC_FC_CFG_RX_FC_ENA;

	if (tx_pause)
		mac_fc_cfg |= SYS_MAC_FC_CFG_TX_FC_ENA |
			      SYS_MAC_FC_CFG_PAUSE_VAL_CFG(0xffff) |
			      SYS_MAC_FC_CFG_FC_LATENCY_CFG(0x7) |
			      SYS_MAC_FC_CFG_ZERO_PAUSE_ENA;

	/* Flow control. Link speed is only used here to evaluate the time
	 * specification in incoming pause frames.
	 */
	ocelot_write_rix(ocelot, mac_fc_cfg, SYS_MAC_FC_CFG, port);

	ocelot_write_rix(ocelot, 0, ANA_POL_FLOWC, port);

	/* Undo the effects of felix_phylink_mac_link_down:
	 * enable MAC module
	 */
	ocelot_port_writel(ocelot_port, DEV_MAC_ENA_CFG_RX_ENA |
			   DEV_MAC_ENA_CFG_TX_ENA, DEV_MAC_ENA_CFG);

	/* Enable receiving frames on the port, and activate auto-learning of
	 * MAC addresses.
	 */
	ocelot_write_gix(ocelot, ANA_PORT_PORT_CFG_LEARNAUTO |
			 ANA_PORT_PORT_CFG_RECV_ENA |
			 ANA_PORT_PORT_CFG_PORTID_VAL(port),
			 ANA_PORT_PORT_CFG, port);

	/* Core: Enable port for frame transfer */
	ocelot_fields_write(ocelot, port,
			    QSYS_SWITCH_PORT_MODE_PORT_ENA, 1);

	if (felix->info->port_sched_speed_set)
		felix->info->port_sched_speed_set(ocelot, port, speed);
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

static int felix_parse_ports_node(struct felix *felix,
				  struct device_node *ports_node,
				  phy_interface_t *port_phy_modes)
{
	struct ocelot *ocelot = &felix->ocelot;
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

		err = felix->info->prevalidate_phy_mode(ocelot, port, phy_mode);
		if (err < 0) {
			dev_err(dev, "Unsupported PHY mode %s on port %d\n",
				phy_modes(phy_mode), port);
			of_node_put(child);
			return err;
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
	if (!ports_node) {
		dev_err(dev, "Incorrect bindings: absent \"ports\" node\n");
		return -ENODEV;
	}

	err = felix_parse_ports_node(felix, ports_node, port_phy_modes);
	of_node_put(ports_node);

	return err;
}

static int felix_init_structs(struct felix *felix, int num_phys_ports)
{
	struct ocelot *ocelot = &felix->ocelot;
	phy_interface_t *port_phy_modes;
	struct resource res;
	int port, i, err;

	ocelot->num_phys_ports = num_phys_ports;
	ocelot->ports = devm_kcalloc(ocelot->dev, num_phys_ports,
				     sizeof(struct ocelot_port *), GFP_KERNEL);
	if (!ocelot->ports)
		return -ENOMEM;

	ocelot->map		= felix->info->map;
	ocelot->stats_layout	= felix->info->stats_layout;
	ocelot->num_stats	= felix->info->num_stats;
	ocelot->num_mact_rows	= felix->info->num_mact_rows;
	ocelot->vcap		= felix->info->vcap;
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
		struct regmap *target;

		if (!felix->info->target_io_res[i].name)
			continue;

		memcpy(&res, &felix->info->target_io_res[i], sizeof(res));
		res.flags = IORESOURCE_MEM;
		res.start += felix->switch_base;
		res.end += felix->switch_base;

		target = ocelot_regmap_init(ocelot, &res);
		if (IS_ERR(target)) {
			dev_err(ocelot->dev,
				"Failed to map device memory space\n");
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
		struct regmap *target;

		ocelot_port = devm_kzalloc(ocelot->dev,
					   sizeof(struct ocelot_port),
					   GFP_KERNEL);
		if (!ocelot_port) {
			dev_err(ocelot->dev,
				"failed to allocate port memory\n");
			kfree(port_phy_modes);
			return -ENOMEM;
		}

		memcpy(&res, &felix->info->port_io_res[port], sizeof(res));
		res.flags = IORESOURCE_MEM;
		res.start += felix->switch_base;
		res.end += felix->switch_base;

		target = ocelot_regmap_init(ocelot, &res);
		if (IS_ERR(target)) {
			dev_err(ocelot->dev,
				"Failed to map memory space for port %d\n",
				port);
			kfree(port_phy_modes);
			return PTR_ERR(target);
		}

		ocelot_port->phy_mode = port_phy_modes[port];
		ocelot_port->ocelot = ocelot;
		ocelot_port->target = target;
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

/* Hardware initialization done here so that we can allocate structures with
 * devm without fear of dsa_register_switch returning -EPROBE_DEFER and causing
 * us to allocate structures twice (leak memory) and map PCI memory twice
 * (which will not work).
 */
static int felix_setup(struct dsa_switch *ds)
{
	struct ocelot *ocelot = ds->priv;
	struct felix *felix = ocelot_to_felix(ocelot);
	int port, err;

	err = felix_init_structs(felix, ds->num_ports);
	if (err)
		return err;

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

	for (port = 0; port < ds->num_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		ocelot_init_port(ocelot, port);

		/* Set the default QoS Classification based on PCP and DEI
		 * bits of vlan tag.
		 */
		felix_port_qos_map_init(ocelot, port);
	}

	err = ocelot_devlink_sb_register(ocelot);
	if (err)
		goto out_deinit_ports;

	for (port = 0; port < ds->num_ports; port++) {
		if (!dsa_is_cpu_port(ds, port))
			continue;

		/* The initial tag protocol is NPI which always returns 0, so
		 * there's no real point in checking for errors.
		 */
		felix_set_tag_protocol(ds, port, felix->tag_proto);
	}

	ds->mtu_enforcement_ingress = true;
	ds->assisted_learning_on_cpu_port = true;

	return 0;

out_deinit_ports:
	for (port = 0; port < ocelot->num_phys_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		ocelot_deinit_port(ocelot, port);
	}

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
	int port;

	for (port = 0; port < ds->num_ports; port++) {
		if (!dsa_is_cpu_port(ds, port))
			continue;

		felix_del_tag_protocol(ds, port, felix->tag_proto);
	}

	ocelot_devlink_sb_unregister(ocelot);
	ocelot_deinit_timestamp(ocelot);
	ocelot_deinit(ocelot);

	for (port = 0; port < ocelot->num_phys_ports; port++) {
		if (dsa_is_unused_port(ds, port))
			continue;

		ocelot_deinit_port(ocelot, port);
	}

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

	return ocelot_hwstamp_set(ocelot, port, ifr);
}

static bool felix_check_xtr_pkt(struct ocelot *ocelot, unsigned int ptp_type)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	int err, grp = 0;

	if (felix->tag_proto != DSA_TAG_PROTO_OCELOT_8021Q)
		return false;

	if (!felix->info->quirk_no_xtr_irq)
		return false;

	if (ptp_type == PTP_CLASS_NONE)
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
	if (err < 0)
		ocelot_drain_cpu_queue(ocelot, 0);

	return true;
}

static bool felix_rxtstamp(struct dsa_switch *ds, int port,
			   struct sk_buff *skb, unsigned int type)
{
	u8 *extraction = skb->data - ETH_HLEN - OCELOT_TAG_LEN;
	struct skb_shared_hwtstamps *shhwtstamps;
	struct ocelot *ocelot = ds->priv;
	u32 tstamp_lo, tstamp_hi;
	struct timespec64 ts;
	u64 tstamp, val;

	/* If the "no XTR IRQ" workaround is in use, tell DSA to defer this skb
	 * for RX timestamping. Then free it, and poll for its copy through
	 * MMIO in the CPU port module, and inject that into the stack from
	 * ocelot_xtr_poll().
	 */
	if (felix_check_xtr_pkt(ocelot, type)) {
		kfree_skb(skb);
		return true;
	}

	ocelot_ptp_gettime64(&ocelot->ptp_info, &ts);
	tstamp = ktime_set(ts.tv_sec, ts.tv_nsec);

	ocelot_xfh_get_rew_val(extraction, &val);
	tstamp_lo = (u32)val;

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

	if (ocelot_port_txtstamp_request(ocelot, port, skb, &clone))
		return;

	if (clone)
		OCELOT_SKB_CB(skb)->clone = clone;
}

static int felix_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
{
	struct ocelot *ocelot = ds->priv;

	ocelot_port_set_maxlen(ocelot, port, new_mtu);

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

	return ocelot_cls_flower_replace(ocelot, port, cls, ingress);
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

const struct dsa_switch_ops felix_switch_ops = {
	.get_tag_protocol		= felix_get_tag_protocol,
	.change_tag_protocol		= felix_change_tag_protocol,
	.setup				= felix_setup,
	.teardown			= felix_teardown,
	.set_ageing_time		= felix_set_ageing_time,
	.get_strings			= felix_get_strings,
	.get_ethtool_stats		= felix_get_ethtool_stats,
	.get_sset_count			= felix_get_sset_count,
	.get_ts_info			= felix_get_ts_info,
	.phylink_validate		= felix_phylink_validate,
	.phylink_mac_config		= felix_phylink_mac_config,
	.phylink_mac_link_down		= felix_phylink_mac_link_down,
	.phylink_mac_link_up		= felix_phylink_mac_link_up,
	.port_enable			= felix_port_enable,
	.port_disable			= felix_port_disable,
	.port_fdb_dump			= felix_fdb_dump,
	.port_fdb_add			= felix_fdb_add,
	.port_fdb_del			= felix_fdb_del,
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
};

struct net_device *felix_port_to_netdev(struct ocelot *ocelot, int port)
{
	struct felix *felix = ocelot_to_felix(ocelot);
	struct dsa_switch *ds = felix->ds;

	if (!dsa_is_user_port(ds, port))
		return NULL;

	return dsa_to_port(ds, port)->slave;
}

int felix_netdev_to_port(struct net_device *dev)
{
	struct dsa_port *dp;

	dp = dsa_port_from_netdev(dev);
	if (IS_ERR(dp))
		return -EINVAL;

	return dp->index;
}
