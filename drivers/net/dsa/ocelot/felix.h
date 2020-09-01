/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 NXP Semiconductors
 */
#ifndef _MSCC_FELIX_H
#define _MSCC_FELIX_H

#define ocelot_to_felix(o)		container_of((o), struct felix, ocelot)
#define FELIX_NUM_TC			8

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
	int                             num_tx_queues;
	struct vcap_field		*vcap_is2_keys;
	struct vcap_field		*vcap_is2_actions;
	const struct vcap_props		*vcap;
	int				switch_pci_bar;
	int				imdio_pci_bar;
	int	(*mdio_bus_alloc)(struct ocelot *ocelot);
	void	(*mdio_bus_free)(struct ocelot *ocelot);
	void	(*pcs_config)(struct ocelot *ocelot, int port,
			      unsigned int link_an_mode,
			      const struct phylink_link_state *state);
	void	(*pcs_link_up)(struct ocelot *ocelot, int port,
			       unsigned int link_an_mode,
			       phy_interface_t interface,
			       int speed, int duplex);
	void	(*pcs_link_state)(struct ocelot *ocelot, int port,
				  struct phylink_link_state *state);
	void	(*phylink_validate)(struct ocelot *ocelot, int port,
				    unsigned long *supported,
				    struct phylink_link_state *state);
	int	(*prevalidate_phy_mode)(struct ocelot *ocelot, int port,
					phy_interface_t phy_mode);
	int	(*port_setup_tc)(struct dsa_switch *ds, int port,
				 enum tc_setup_type type, void *type_data);
	void	(*port_sched_speed_set)(struct ocelot *ocelot, int port,
					u32 speed);
	void	(*xmit_template_populate)(struct ocelot *ocelot, int port);
};

extern const struct dsa_switch_ops felix_switch_ops;
extern struct pci_driver felix_vsc9959_pci_driver;
extern struct platform_driver seville_vsc9953_driver;

/* DSA glue / front-end for struct ocelot */
struct felix {
	struct dsa_switch		*ds;
	const struct felix_info		*info;
	struct ocelot			ocelot;
	struct mii_bus			*imdio;
	struct phy_device		**pcs;
	resource_size_t			switch_base;
	resource_size_t			imdio_base;
};

void vsc9959_pcs_link_state(struct ocelot *ocelot, int port,
			    struct phylink_link_state *state);
void vsc9959_pcs_config(struct ocelot *ocelot, int port,
			unsigned int link_an_mode,
			const struct phylink_link_state *state);
void vsc9959_pcs_link_up(struct ocelot *ocelot, int port,
			 unsigned int link_an_mode,
			 phy_interface_t interface,
			 int speed, int duplex);
void vsc9959_mdio_bus_free(struct ocelot *ocelot);

#endif
