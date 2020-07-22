/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 NXP Semiconductors
 */
#ifndef _MSCC_FELIX_H
#define _MSCC_FELIX_H

#define ocelot_to_felix(o)		container_of((o), struct felix, ocelot)

/* Platform-specific information */
struct felix_info {
	const struct resource		*target_io_res;
	const struct resource		*port_io_res;
	const struct resource		*imdio_res;
	const struct reg_field		*regfields;
	const u32 *const		*map;
	const struct ocelot_ops		*ops;
	int				shared_queue_sz;
	int				num_mact_rows;
	const struct ocelot_stat_layout	*stats_layout;
	unsigned int			num_stats;
	int				num_ports;
	struct vcap_field		*vcap_is2_keys;
	struct vcap_field		*vcap_is2_actions;
	const struct vcap_props		*vcap;
	int				switch_pci_bar;
	int				imdio_pci_bar;
	int	(*mdio_bus_alloc)(struct ocelot *ocelot);
	void	(*mdio_bus_free)(struct ocelot *ocelot);
	void	(*pcs_init)(struct ocelot *ocelot, int port,
			    unsigned int link_an_mode,
			    const struct phylink_link_state *state);
	void	(*pcs_an_restart)(struct ocelot *ocelot, int port);
	void	(*pcs_link_state)(struct ocelot *ocelot, int port,
				  struct phylink_link_state *state);
	int	(*prevalidate_phy_mode)(struct ocelot *ocelot, int port,
					phy_interface_t phy_mode);
};

extern struct felix_info		felix_info_vsc9959;

enum felix_instance {
	FELIX_INSTANCE_VSC9959		= 0,
};

/* DSA glue / front-end for struct ocelot */
struct felix {
	struct dsa_switch		*ds;
	struct pci_dev			*pdev;
	struct felix_info		*info;
	struct ocelot			ocelot;
	struct mii_bus			*imdio;
	struct phy_device		**pcs;
};

#endif
