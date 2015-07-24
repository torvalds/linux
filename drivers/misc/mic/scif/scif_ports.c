/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#include <linux/idr.h>

#include "scif_main.h"

#define SCIF_PORT_COUNT	0x10000	/* Ports available */

struct idr scif_ports;

/*
 * struct scif_port - SCIF port information
 *
 * @ref_cnt - Reference count since there can be multiple endpoints
 *		created via scif_accept(..) simultaneously using a port.
 */
struct scif_port {
	int ref_cnt;
};

/**
 * __scif_get_port - Reserve a specified port # for SCIF and add it
 * to the global list.
 * @port : port # to be reserved.
 *
 * @return : Allocated SCIF port #, or -ENOSPC if port unavailable.
 *		On memory allocation failure, returns -ENOMEM.
 */
static int __scif_get_port(int start, int end)
{
	int id;
	struct scif_port *port = kzalloc(sizeof(*port), GFP_ATOMIC);

	if (!port)
		return -ENOMEM;
	spin_lock(&scif_info.port_lock);
	id = idr_alloc(&scif_ports, port, start, end, GFP_ATOMIC);
	if (id >= 0)
		port->ref_cnt++;
	spin_unlock(&scif_info.port_lock);
	return id;
}

/**
 * scif_rsrv_port - Reserve a specified port # for SCIF.
 * @port : port # to be reserved.
 *
 * @return : Allocated SCIF port #, or -ENOSPC if port unavailable.
 *		On memory allocation failure, returns -ENOMEM.
 */
int scif_rsrv_port(u16 port)
{
	return __scif_get_port(port, port + 1);
}

/**
 * scif_get_new_port - Get and reserve any port # for SCIF in the range
 *			SCIF_PORT_RSVD + 1 to SCIF_PORT_COUNT - 1.
 *
 * @return : Allocated SCIF port #, or -ENOSPC if no ports available.
 *		On memory allocation failure, returns -ENOMEM.
 */
int scif_get_new_port(void)
{
	return __scif_get_port(SCIF_PORT_RSVD + 1, SCIF_PORT_COUNT);
}

/**
 * scif_get_port - Increment the reference count for a SCIF port
 * @id : SCIF port
 *
 * @return : None
 */
void scif_get_port(u16 id)
{
	struct scif_port *port;

	if (!id)
		return;
	spin_lock(&scif_info.port_lock);
	port = idr_find(&scif_ports, id);
	if (port)
		port->ref_cnt++;
	spin_unlock(&scif_info.port_lock);
}

/**
 * scif_put_port - Release a reserved SCIF port
 * @id : SCIF port to be released.
 *
 * @return : None
 */
void scif_put_port(u16 id)
{
	struct scif_port *port;

	if (!id)
		return;
	spin_lock(&scif_info.port_lock);
	port = idr_find(&scif_ports, id);
	if (port) {
		port->ref_cnt--;
		if (!port->ref_cnt) {
			idr_remove(&scif_ports, id);
			kfree(port);
		}
	}
	spin_unlock(&scif_info.port_lock);
}
