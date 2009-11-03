/*
 * Copyright(c) 2009 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Maintained at www.Open-FCoE.org
 */

/*
 * NPIV VN_Port helper functions for libfc
 */

#include <scsi/libfc.h>

/**
 * fc_vport_create() - Create a new NPIV vport instance
 * @vport: fc_vport structure from scsi_transport_fc
 * @privsize: driver private data size to allocate along with the Scsi_Host
 */

struct fc_lport *libfc_vport_create(struct fc_vport *vport, int privsize)
{
	struct Scsi_Host *shost = vport_to_shost(vport);
	struct fc_lport *n_port = shost_priv(shost);
	struct fc_lport *vn_port;

	vn_port = libfc_host_alloc(shost->hostt, privsize);
	if (!vn_port)
		goto err_out;
	if (fc_exch_mgr_list_clone(n_port, vn_port))
		goto err_put;

	vn_port->vport = vport;
	vport->dd_data = vn_port;

	mutex_lock(&n_port->lp_mutex);
	list_add_tail(&vn_port->list, &n_port->vports);
	mutex_unlock(&n_port->lp_mutex);

	return vn_port;

err_put:
	scsi_host_put(vn_port->host);
err_out:
	return NULL;
}
EXPORT_SYMBOL(libfc_vport_create);

/**
 * fc_vport_id_lookup() - find NPIV lport that matches a given fabric ID
 * @n_port: Top level N_Port which may have multiple NPIV VN_Ports
 * @port_id: Fabric ID to find a match for
 *
 * Returns: matching lport pointer or NULL if there is no match
 */
struct fc_lport *fc_vport_id_lookup(struct fc_lport *n_port, u32 port_id)
{
	struct fc_lport *lport = NULL;
	struct fc_lport *vn_port;

	if (fc_host_port_id(n_port->host) == port_id)
		return n_port;

	mutex_lock(&n_port->lp_mutex);
	list_for_each_entry(vn_port, &n_port->vports, list) {
		if (fc_host_port_id(vn_port->host) == port_id) {
			lport = vn_port;
			break;
		}
	}
	mutex_unlock(&n_port->lp_mutex);

	return lport;
}

