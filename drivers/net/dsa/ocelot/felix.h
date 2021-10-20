/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 NXP
 */
#ifndef _MSCC_FELIX_H
#define _MSCC_FELIX_H

#define ocelot_to_felix(o)		container_of((o), struct felix, ocelot)
#define FELIX_MAC_QUIRKS		OCELOT_QUIRK_PCS_PERFORMS_RATE_ADAPTATION

/* Platform-specific information */
struct felix_info {
	const struct resource		*target_io_res;
	const struct resource		*port_io_res;
	const struct resource		*imdio_res;
	const struct reg_field		*regfields;
	const u32 *const		*map;
	const struct ocelot_ops		*ops;
	int				num_mact_rows;
	const struct ocelot_stat_layout	*stats_layout;
	unsigned int			num_stats;
	int				num_ports;
	int				num_tx_queues;
	struct vcap_props		*vcap;
	int				switch_pci_bar;
	int				imdio_pci_bar;
	const struct ptp_clock_info	*ptp_caps;

	/* Some Ocelot switches are integrated into the SoC without the
	 * extraction IRQ line connected to the ARM GIC. By enabling this
	 * workaround, the few packets that are delivered to the CPU port
	 * module (currently only PTP) are copied not only to the hardware CPU
	 * port module, but also to the 802.1Q Ethernet CPU port, and polling
	 * the extraction registers is triggered once the DSA tagger sees a PTP
	 * frame. The Ethernet frame is only used as a notification: it is
	 * dropped, and the original frame is extracted over MMIO and annotated
	 * with the RX timestamp.
	 */
	bool				quirk_no_xtr_irq;

	int	(*mdio_bus_alloc)(struct ocelot *ocelot);
	void	(*mdio_bus_free)(struct ocelot *ocelot);
	void	(*phylink_validate)(struct ocelot *ocelot, int port,
				    unsigned long *supported,
				    struct phylink_link_state *state);
	int	(*prevalidate_phy_mode)(struct ocelot *ocelot, int port,
					phy_interface_t phy_mode);
	int	(*port_setup_tc)(struct dsa_switch *ds, int port,
				 enum tc_setup_type type, void *type_data);
	void	(*port_sched_speed_set)(struct ocelot *ocelot, int port,
					u32 speed);
};

extern const struct dsa_switch_ops felix_switch_ops;

/* DSA glue / front-end for struct ocelot */
struct felix {
	struct dsa_switch		*ds;
	const struct felix_info		*info;
	struct ocelot			ocelot;
	struct mii_bus			*imdio;
	struct lynx_pcs			**pcs;
	resource_size_t			switch_base;
	resource_size_t			imdio_base;
	enum dsa_tag_protocol		tag_proto;
	struct kthread_worker		*xmit_worker;
};

struct net_device *felix_port_to_netdev(struct ocelot *ocelot, int port);
int felix_netdev_to_port(struct net_device *dev);

#endif
