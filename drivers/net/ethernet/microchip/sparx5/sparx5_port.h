/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2021 Microchip Technology Inc. and its subsidiaries.
 */

#ifndef __SPARX5_PORT_H__
#define __SPARX5_PORT_H__

#include "sparx5_main.h"

static inline bool sparx5_port_is_2g5(int portno)
{
	return portno >= 16 && portno <= 47;
}

static inline bool sparx5_port_is_5g(int portno)
{
	return portno <= 11 || portno == 64;
}

static inline bool sparx5_port_is_10g(int portno)
{
	return (portno >= 12 && portno <= 15) || (portno >= 48 && portno <= 55);
}

static inline bool sparx5_port_is_25g(int portno)
{
	return portno >= 56 && portno <= 63;
}

static inline u32 sparx5_to_high_dev(int port)
{
	if (sparx5_port_is_5g(port))
		return TARGET_DEV5G;
	if (sparx5_port_is_10g(port))
		return TARGET_DEV10G;
	return TARGET_DEV25G;
}

static inline u32 sparx5_to_pcs_dev(int port)
{
	if (sparx5_port_is_5g(port))
		return TARGET_PCS5G_BR;
	if (sparx5_port_is_10g(port))
		return TARGET_PCS10G_BR;
	return TARGET_PCS25G_BR;
}

static inline int sparx5_port_dev_index(int port)
{
	if (sparx5_port_is_2g5(port))
		return port;
	if (sparx5_port_is_5g(port))
		return (port <= 11 ? port : 12);
	if (sparx5_port_is_10g(port))
		return (port >= 12 && port <= 15) ?
			port - 12 : port - 44;
	return (port - 56);
}

int sparx5_port_init(struct sparx5 *sparx5,
		     struct sparx5_port *spx5_port,
		     struct sparx5_port_config *conf);

int sparx5_port_config(struct sparx5 *sparx5,
		       struct sparx5_port *spx5_port,
		       struct sparx5_port_config *conf);

int sparx5_port_pcs_set(struct sparx5 *sparx5,
			struct sparx5_port *port,
			struct sparx5_port_config *conf);

int sparx5_serdes_set(struct sparx5 *sparx5,
		      struct sparx5_port *spx5_port,
		      struct sparx5_port_config *conf);

struct sparx5_port_status {
	bool link;
	bool link_down;
	int  speed;
	bool an_complete;
	int  duplex;
	int  pause;
};

int sparx5_get_port_status(struct sparx5 *sparx5,
			   struct sparx5_port *port,
			   struct sparx5_port_status *status);

void sparx5_port_enable(struct sparx5_port *port, bool enable);

#endif	/* __SPARX5_PORT_H__ */
