/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2019 NXP Semiconductors
 */
#ifndef _MSCC_FELIX_H
#define _MSCC_FELIX_H

#define ocelot_to_felix(o)		container_of((o), struct felix, ocelot)

/* Platform-specific information */
struct felix_info {
	struct resource			*target_io_res;
	struct resource			*port_io_res;
	const struct reg_field		*regfields;
	const u32 *const		*map;
	const struct ocelot_ops		*ops;
	int				shared_queue_sz;
	const struct ocelot_stat_layout	*stats_layout;
	unsigned int			num_stats;
	int				num_ports;
	int				pci_bar;
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
};

#endif
