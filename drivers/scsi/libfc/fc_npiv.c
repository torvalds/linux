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
		return vn_port;

	vn_port->vport = vport;
	vport->dd_data = vn_port;

	mutex_lock(&n_port->lp_mutex);
	list_add_tail(&vn_port->list, &n_port->vports);
	mutex_unlock(&n_port->lp_mutex);

	return vn_port;
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

	if (n_port->port_id == port_id)
		return n_port;

	if (port_id == FC_FID_FLOGI)
		return n_port;		/* for point-to-point */

	mutex_lock(&n_port->lp_mutex);
	list_for_each_entry(vn_port, &n_port->vports, list) {
		if (vn_port->port_id == port_id) {
			lport = vn_port;
			break;
		}
	}
	mutex_unlock(&n_port->lp_mutex);

	return lport;
}
EXPORT_SYMBOL(fc_vport_id_lookup);

/*
 * When setting the link state of vports during an lport state change, it's
 * necessary to hold the lp_mutex of both the N_Port and the VN_Port.
 * This tells the lockdep engine to treat the nested locking of the VN_Port
 * as a different lock class.
 */
enum libfc_lport_mutex_class {
	LPORT_MUTEX_NORMAL = 0,
	LPORT_MUTEX_VN_PORT = 1,
};

/**
 * __fc_vport_setlink() - update link and status on a VN_Port
 * @n_port: parent N_Port
 * @vn_port: VN_Port to update
 *
 * Locking: must be called with both the N_Port and VN_Port lp_mutex held
 */
static void __fc_vport_setlink(struct fc_lport *n_port,
			       struct fc_lport *vn_port)
{
	struct fc_vport *vport = vn_port->vport;

	if (vn_port->state == LPORT_ST_DISABLED)
		return;

	if (n_port->state == LPORT_ST_READY) {
		if (n_port->npiv_enabled) {
			fc_vport_set_state(vport, FC_VPORT_INITIALIZING);
			__fc_linkup(vn_port);
		} else {
			fc_vport_set_state(vport, FC_VPORT_NO_FABRIC_SUPP);
			__fc_linkdown(vn_port);
		}
	} else {
		fc_vport_set_state(vport, FC_VPORT_LINKDOWN);
		__fc_linkdown(vn_port);
	}
}

/**
 * fc_vport_setlink() - update link and status on a VN_Port
 * @vn_port: virtual port to update
 */
void fc_vport_setlink(struct fc_lport *vn_port)
{
	struct fc_vport *vport = vn_port->vport;
	struct Scsi_Host *shost = vport_to_shost(vport);
	struct fc_lport *n_port = shost_priv(shost);

	mutex_lock(&n_port->lp_mutex);
	mutex_lock_nested(&vn_port->lp_mutex, LPORT_MUTEX_VN_PORT);
	__fc_vport_setlink(n_port, vn_port);
	mutex_unlock(&vn_port->lp_mutex);
	mutex_unlock(&n_port->lp_mutex);
}
EXPORT_SYMBOL(fc_vport_setlink);

/**
 * fc_vports_linkchange() - change the link state of all vports
 * @n_port: Parent N_Port that has changed state
 *
 * Locking: called with the n_port lp_mutex held
 */
void fc_vports_linkchange(struct fc_lport *n_port)
{
	struct fc_lport *vn_port;

	list_for_each_entry(vn_port, &n_port->vports, list) {
		mutex_lock_nested(&vn_port->lp_mutex, LPORT_MUTEX_VN_PORT);
		__fc_vport_setlink(n_port, vn_port);
		mutex_unlock(&vn_port->lp_mutex);
	}
}

